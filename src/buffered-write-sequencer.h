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
#ifndef BUFFERED_WRITE_SEQUENCER_H_
#define BUFFERED_WRITE_SEQUENCER_H_

#include <stddef.h>

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

#include "timg-time.h"

namespace timg {
// The last step towards writing content to the terminal.
// All writes are enqueued in sequence, this Sequencer is writing them out in
// that same sequence and applies timing necessary for proper playback of
// animations.
//
// Up to the write, the preparation of the buffers might have taken some
// time (scaling, encoding as PNG and base64 etc.), but the timing of frames
// is time-sensitive (frame rate in animations and videos), so should
// be applied when everything else is settled.
//
// The sequencer provides pre-allocated large buffers for re-use,
// and a write command that takes the details about requested timing.
//
// That allows for best possible smooth timing even if the upstream pipeline
// steps might have varying latency. Since this is the last step where
// everything to be written is already determined, the timing can be fixed
// best here.
//
// (This is the first step, establishing the interface but leaving the original
// synchronous behaviour. In the next, the writing will be made asynchronous
// with a queue and threaded writing)
enum class SeqType {
    Immediate,         // Don't delay when buffer is written.
    StartOfAnimation,  // Remember start time. Possibly delay.
    AnimationFrame,    // Write buffer, delay until alloted frame time is up.
};
class BufferedWriteSequencer {
public:
    // Create a BufferedWriteSequencer that writes to the given
    // filedescriptor.
    BufferedWriteSequencer(int fd, bool allow_frame_skipping,
                           int max_queue_len);
    ~BufferedWriteSequencer();

    // Request a buffer that provides at least the requested size.
    // This buffer is to be filled with whatever should be written, then,
    // after filling, needs to be handed over to the WriteBuffer() function.
    char *RequestBuffer(size_t size);

    // Write out buffer with the given size.
    // The "buffer" has to be from the a previous call to RequestBuffer(); it
    // will be stored for re-use after the write is complete.
    //
    // The "sequence_type" determines the timing of the writes.
    // With a SeqType::Immediate, buffer is written, no wait afterwards.
    // The "end_of_frame" duration is ignored.
    //
    // With StartOfAnimation and AnimationFrame, the "end_of_frame" duration
    // determines how long after the beginning of the animation the sent
    // buffer is finished. For an animation, the first frame is marked with
    // StartOfAnimation, all following frames an AnimationFrame with an
    // increaing duration from the start. So this essentially determines the
    // absolute time difference between the start of the Animation and the
    // current frame. The first frame also has a positive end-of-frame duration.
    // This allows to
    //   (a) be independent of upstream latencies as only when the first
    //       frame of an animation arrives here, the clock starts ticking.
    //   (b) the duration from the beginning establishes an absolute time
    //       since beginning of the animation, thus allows to avoid
    //       time-creeps. Even if a frame takes a little bit longer, the next
    //       will try to finish in earlier.
    //
    // No return value: the write might happen asynchronously.
    void WriteBuffer(char *buffer, size_t size, SeqType sequence_type,
                     Duration end_of_frame = {});

    // Flush all pending writes.
    void Flush();

    // -- Stats
    int64_t bytes_total() const;
    int64_t bytes_skipped() const;
    int64_t frames_total() const;
    int64_t frames_skipped() const;

private:
    struct MemBlock;

    void ReturnMemblock(MemBlock *b);
    void ProcessQueue();  // Runs in thread.

    const int fd_;
    const bool allow_frame_skipping_;
    const size_t max_queue_len_;

    struct WorkItem {
        MemBlock *block;
        size_t size;
        SeqType sequence_type;
        Duration end_of_frame;
    };
    std::mutex work_lock_;
    std::queue<WorkItem> work_;
    bool exiting_ = false;
    std::condition_variable work_sync_;
    std::thread *work_executor_;

    // A stash of re-usable memory blocks
    std::mutex mempool_lock_;
    std::queue<MemBlock*> mempool_;

    // Statistics.
    mutable std::mutex stats_lock_;
    int64_t stats_bytes_total_ = 0;
    int64_t stats_bytes_skipped_ = 0;
    int64_t stats_frames_total_ = 0;
    int64_t stats_frames_skipped_ = 0;
};
}  // namespace timg
#endif  // BUFFERED_WRITE_SEQUENCER_H_
