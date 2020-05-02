// -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
// (c) 2020 Henner Zeller <h.zeller@acm.org>
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

#ifndef VIDEO_DISPLAY_H_
#define VIDEO_DISPLAY_H_

#include <signal.h>

#include "terminal-canvas.h"
#include "timg-time.h"

struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace timg {
struct ScaleOptions;

// Video loader, meant for one video to load, and if successful, Play().
class VideoLoader {
public:
    ~VideoLoader();

    // Attempt to load given filename as video, open stream and set-up scaling.
    // Returns true on success.
    bool LoadAndScale(const char *filename,
                      int display_width, int display_height,
                      const ScaleOptions &options);

    // Play video up to given duration.
    //
    // The reference to the "interrupt_received" can be updated by a signal
    // while the method is running and shall be checked often.
    void Play(Duration duration,
              const volatile sig_atomic_t &interrupt_received,
              timg::TerminalCanvas *canvas);

private:
    void CopyToFramebuffer(const AVFrame *av_frame);
    bool DecodePacket(AVPacket *packet, AVFrame *output_frame);

    int video_stream_index_ = -1;
    AVFormatContext *format_context_ = nullptr;
    AVCodecContext *codec_context_ = nullptr;
    AVFrame *output_frame_ = nullptr;
    SwsContext *sws_context_ = nullptr;
    timg::Duration frame_duration_;  // 1/fps
    timg::Framebuffer *terminal_fb_ = nullptr;
};

}  // namespace timg

#endif // VIDEO_DISPLAY_H_
