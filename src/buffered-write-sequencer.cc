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
BufferedWriteSequencer::BufferedWriteSequencer(int fd) : fd_(fd) {}

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

int64_t BufferedWriteSequencer::total_bytes_written() const {
    return total_bytes_written_;
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

    if (sequence_type == SeqType::StartOfAnimation) {
        animation_start_ = Time::Now();
    }
    ReliableWrite(fd_, buffer, size);
    total_bytes_written_ += size;
    if (sequence_type == SeqType::StartOfAnimation ||
        sequence_type == SeqType::AnimationFrame) {
        (animation_start_ + end_of_frame).WaitUntil();
    }
}

}  // namespace timg
