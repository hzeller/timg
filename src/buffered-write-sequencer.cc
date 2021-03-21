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
BufferedWriteSequencer::BufferedWriteSequencer(int fd, bool allow_frame_skip)
    : fd_(fd), allow_frame_skipping_(allow_frame_skip) {}

struct BufferedWriteSequencer::MemBlock {
    size_t size;
    uint32_t sentinel;
    char data[];
};

char *BufferedWriteSequencer::RequestBuffer(size_t size) {
    if (!current_block_ || current_block_->size < size) {
        free(current_block_);
        size_t new_size = sizeof(MemBlock) + size + kChunkSize - 1;
        new_size -= new_size % kChunkSize;    // Nice multiple of kChunkSize
        current_block_ = (MemBlock*) malloc(new_size);
        current_block_->size = new_size - sizeof(MemBlock);
        current_block_->sentinel = kSentinel;
    }
    return current_block_->data;
}

int64_t BufferedWriteSequencer::bytes_total() const {
    return stats_bytes_total_;
}

int64_t BufferedWriteSequencer::bytes_skipped() const {
    return stats_bytes_skipped_;
}

int64_t BufferedWriteSequencer::frames_total() const {
    return stats_frames_total_;
}

int64_t BufferedWriteSequencer::frames_skipped() const {
    return stats_frames_skipped_;
}

static ssize_t ReliableWrite(int fd, const char *buffer, const size_t size) {
    int written = 0;
    size_t remaining = size;
    while (remaining && (written = write(fd, buffer, remaining)) > 0) {
        remaining -= written;
        buffer += written;
    }
    if (written < 0) return -1;
    return size;
}

void BufferedWriteSequencer::WriteBuffer(char *buffer, size_t size,
                                         SeqType sequence_type,
                                         Duration end_of_frame) {
    // Currently not doing anything special with the block, as in this stage,
    // this is the only available block.
    MemBlock *block = (MemBlock*) (buffer - offsetof(MemBlock, data));
    assert(block->sentinel == kSentinel);  // Did we allocate it ?
    assert(block->size >= size);           // No buffer overrun
    assert(block == current_block_);       // Currently the only block.

    bool do_skip = false;
    switch (sequence_type) {
    case SeqType::StartOfAnimation:
        animation_start_ = Time::Now();
        break;
    case SeqType::AnimationFrame:
        if (!last_frame_end_.is_zero()) {
            const Time finish_time = animation_start_ + last_frame_end_;
            // Only consider skipping if not Immediate or first in frame.
            do_skip = allow_frame_skipping_ && finish_time < Time::Now();
            finish_time.WaitUntil();
        }
        break;
    case SeqType::Immediate:
        break;
    }
    last_frame_end_ = end_of_frame;

    stats_bytes_total_ += size;
    ++stats_frames_total_;
    if (!do_skip) {
        ReliableWrite(fd_, buffer, size);
    } else {
        stats_bytes_skipped_ += size;
        ++stats_frames_skipped_;
    }
}

}  // namespace timg
