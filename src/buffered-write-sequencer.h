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

#include <signal.h>
#include <stddef.h>

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

#include "timg-time.h"

namespace timg {
// The last step towards writing content to the terminal.
// The sequencer provides queueing of write calls with details about the
// requested timings. Built-in buffer management provides buffers that are
// re-used to minimize memory allocations.
//
// That allows for best possible smooth timing even if the upstream pipeline
// steps (decode, scale, compression, base64 encoding..) might have varying
// latency. Since this is the last step where everything to be written is
// already determined, the timing can be fixed best here.
//
// Write requests are queued and written asynchronously in a separate
// thread to be de-coupled from the timing of the incoming calls.
enum class SeqType {
    ControlWrite,      // Control information to be written. Do delay, no skip.
    FrameImmediate,    // Don't delay when frame is written.
    StartOfAnimation,  // First frame of an Animation. Hold for duration.
    AnimationFrame,    // Write frame; finish duration relative to start anim
};
class BufferedWriteSequencer {
public:
    // Create a BufferedWriteSequencer that writes to the given
    // filedescriptor "fd". The "allow_frame_skipping" tells the sequencer
    // if it is ok to skip frames that can't be shown in time anymore.
    // The "max_queue_len" determines the number of the queued-up requests.
    // If "debug_no_frame_delay" is set, frames are written as fast as possible
    // without time between frames.
    //
    // Writes that are still pending when the "interrupt_received" flag
    // is set externally (e.g. through a signal handler) are discarded to
    // finish quickly (except ControlWrite calls).
    BufferedWriteSequencer(int fd, bool allow_frame_skipping, int max_queue_len,
                           bool debug_no_frame_delay,
                           const volatile sig_atomic_t &interrupt_received);
    ~BufferedWriteSequencer();

    // Request a buffer that provides at least the requested size.
    // This buffer is to be used with whatever should be written, then,
    // after filling, needs to be handed over to the WriteBuffer() function.
    // Conceptually, the returned buffer is owned by this object;
    // buffers received here are returned in calls to WriteBuffer().
    char *RequestBuffer(size_t size);

    // Write out "size" bytes from the "buffer".
    // The "buffer" must be from the a previous call to RequestBuffer(); it
    // will be pooled for re-use after the write is complete.
    //
    // The "sequence_type" determines the kind and timing of the writes.
    //
    // SeqType::ControlWrite are meant for short writes that are not associated
    // with picture frames, e.g. switching cursor on or off. They are
    // always written, even after interrupt_received became true and are
    // not subject to frame skipping. The "end_of_frame" parameter is ignored.
    //
    // The other SeqTypes are used for emitting image content:
    //
    // With a SeqType::Immediate, buffer is written, no wait afterwards;
    // the "end_of_frame" duration is ignored.
    //
    // With SeqType::StartOfAnimation and SeqType::AnimationFrame, the
    // "end_of_frame" duration determines how long after the beginning of the
    // animation the sent buffer is finished.
    // For an animation, the first frame is marked with StartOfAnimation, all
    // following frames are AnimationFrames with an increasing duration from
    // the start. This establishes the absolute time difference between the
    // start of the Animation and the current frame. So the duration for
    // StartOfAnimation is 1/fps, next AnimationFrame at 2/fps and so on.
    // This allows to ...
    //   (a) be independent of upstream latencies as only when the first
    //       frame of an animation arrives here and is emitted, the clock
    //       starts ticking.
    //   (b) avoid time-skews. Even if a frame takes a little bit longer, the
    //       next will try to finish in earlier as the frame-time is absolute.
    //
    // No return value: the write happens asynchronously.
    void WriteBuffer(char *buffer, size_t size, SeqType sequence_type,
                     const Duration &end_of_frame = {});

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
    const bool debug_no_frame_delay_;
    const volatile sig_atomic_t &interrupt_received_;

    // Work queue
    struct WorkItem {
        MemBlock *block;
        size_t size;
        SeqType sequence_type;
        Duration end_of_frame;
    };
    std::mutex work_lock_;
    std::queue<WorkItem> work_;
    std::condition_variable work_sync_;
    std::thread *work_executor_;

    // A stash of re-usable memory blocks
    std::mutex mempool_lock_;
    std::queue<MemBlock *> mempool_;

    // Statistics.
    mutable std::mutex stats_lock_;
    int64_t stats_bytes_total_    = 0;
    int64_t stats_bytes_skipped_  = 0;
    int64_t stats_frames_total_   = 0;
    int64_t stats_frames_skipped_ = 0;
};
}  // namespace timg
#endif  // BUFFERED_WRITE_SEQUENCER_H_
