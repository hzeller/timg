// -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
// (c) 2021 Henner Zeller <h.zeller@acm.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation version 2.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://gnu.org/licenses/gpl-2.0.txt>

#include "buffered-write-sequencer.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

// Allocate memory with nice round sizes
static constexpr int kChunkSize = 1 << 16;
static constexpr uint32_t kSentinel = 0x5e9719e1;

namespace timg {
BufferedWriteSequencer::BufferedWriteSequencer(
    int fd, bool allow_frame_skip, int max_queu_len,
    const volatile sig_atomic_t &interrupt_received)
    : fd_(fd), allow_frame_skipping_(allow_frame_skip),
      max_queue_len_(max_queu_len),
      interrupt_received_(interrupt_received),
      work_executor_(new std::thread(&BufferedWriteSequencer::ProcessQueue,
                                     this)) {
}

BufferedWriteSequencer::~BufferedWriteSequencer() {
    Flush();
    {
        std::lock_guard<std::mutex> l(work_lock_);
        exiting_ = true;
    }
    work_sync_.notify_all();
    work_executor_->join();
}

struct BufferedWriteSequencer::MemBlock {
    size_t size;
    uint32_t sentinel;
    char data[];
};

char *BufferedWriteSequencer::RequestBuffer(size_t size) {
    {
        std::unique_lock<std::mutex> l(mempool_lock_);
        while (!mempool_.empty()) {
            MemBlock *const block = mempool_.front();
            mempool_.pop();
            if (block->size < size) {  // Don't bother with too small blocks.
                free(block);
                continue;
            }
            return block->data;
        }
    }

    // Nothing appropriate found. Create a new block. When it comes back, it
    // will be added to our pool for re-use.
    size_t new_size = sizeof(MemBlock) + size + kChunkSize - 1;
    new_size -= new_size % kChunkSize;    // Nice multiple of kChunkSize
    MemBlock *block = (MemBlock*) malloc(new_size);
    block->size = new_size - sizeof(MemBlock);
    block->sentinel = kSentinel;

    return block->data;
}

void BufferedWriteSequencer::ReturnMemblock(MemBlock *b) {
    std::unique_lock<std::mutex> l(mempool_lock_);
    mempool_.push(b);
}

int64_t BufferedWriteSequencer::bytes_total() const {
    std::lock_guard<std::mutex> l(stats_lock_);
    return stats_bytes_total_;
}

int64_t BufferedWriteSequencer::bytes_skipped() const {
    std::lock_guard<std::mutex> l(stats_lock_);
    return stats_bytes_skipped_;
}

int64_t BufferedWriteSequencer::frames_total() const {
    std::lock_guard<std::mutex> l(stats_lock_);
    return stats_frames_total_;
}

int64_t BufferedWriteSequencer::frames_skipped() const {
    std::lock_guard<std::mutex> l(stats_lock_);
    return stats_frames_skipped_;
}

static ssize_t ReliableWrite(int fd, const char *buffer, const size_t size) {
    if (size == 0) return 0;
    int written = 0;
    size_t remaining = size;
    while (remaining && (written = write(fd, buffer, remaining)) > 0) {
        remaining -= written;
        buffer += written;
    }
    if (written < 0) return -1;
    return size;
}

void BufferedWriteSequencer::Flush() {
    // Sending an empty dummy-write so that we know that this is the
    // last write-in-progress when queue is empty (as the queue is already
    // empty while the last write is still in progress)
    WriteBuffer(RequestBuffer(0), 0, SeqType::ControlWrite, {});
    {
        std::unique_lock<std::mutex> l(work_lock_);
        work_sync_.wait(l, [this](){ return work_.empty(); });
    }
}

void BufferedWriteSequencer::WriteBuffer(char *buffer, size_t size,
                                         SeqType sequence_type,
                                         Duration end_of_frame) {
    // Recover the block from the raw data (our metadata starts a bit
    // before that)
    MemBlock *block = (MemBlock*) (buffer - offsetof(MemBlock, data));
    assert(block->sentinel == kSentinel);  // Did we allocate it ?
    assert(block->size >= size);           // No buffer overrun

    {
        std::unique_lock<std::mutex> l(work_lock_);
        work_sync_.wait(l, [this](){ return work_.size() < max_queue_len_; });
        work_.push({block, size, sequence_type, end_of_frame});
    }
    work_sync_.notify_all();
}

void BufferedWriteSequencer::ProcessQueue() {
    timg::Time animation_start;
    timg::Duration last_frame_end;

    for (;;) {
        WorkItem work_item;
        {
            std::unique_lock<std::mutex> l(work_lock_);
            if (exiting_ && work_.empty()) return;
            work_sync_.wait(l, [this](){ return !work_.empty() || exiting_; });
            if (work_.empty()) continue;
            work_item = work_.front();
            work_.pop();
        }
        work_sync_.notify_all();

        bool do_skip = false;
        if (interrupt_received_ &&
            work_item.sequence_type != SeqType::ControlWrite) {
            continue;  // Finish quickly, discard any queued-up frames.
        }
        switch (work_item.sequence_type) {
        case SeqType::StartOfAnimation:
            animation_start = Time::Now();
            break;
        case SeqType::AnimationFrame:
            if (!last_frame_end.is_zero()) {
                const Time finish_time = animation_start + last_frame_end;
                // Only consider skipping if not Immediate or first in frame.
                static constexpr Duration kAllowedSkew = Duration::Millis(150);
                do_skip = (allow_frame_skipping_ &&
                           finish_time + kAllowedSkew < Time::Now());
                finish_time.WaitUntil();
            }
            break;
        case SeqType::FrameImmediate:
        case SeqType::ControlWrite:
            break;
        }
        last_frame_end = work_item.end_of_frame;

        if (!do_skip)
            ReliableWrite(fd_, work_item.block->data, work_item.size);

        ReturnMemblock(work_item.block);

        if (work_item.sequence_type != SeqType::ControlWrite) {
            std::lock_guard<std::mutex> l(stats_lock_);
            stats_bytes_total_ += work_item.size;
            ++stats_frames_total_;
            if (do_skip) {
                stats_bytes_skipped_ += work_item.size;
                ++stats_frames_skipped_;
            }
        }
    }
}

}  // namespace timg
