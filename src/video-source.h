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

#ifndef VIDEO_SOURCE_H_
#define VIDEO_SOURCE_H_

#include <signal.h>

#include "image-source.h"
#include "terminal-canvas.h"
#include "timg-time.h"

struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace timg {

// Video source, meant for one video to load, and if successful, Play().
class VideoSource final : public ImageSource {
public:
    explicit VideoSource(const std::string &filename);
    ~VideoSource() final;

    static const char *VersionInfo();

    // Attempt to load given filename as video, open stream and set-up scaling.
    // Returns true on success.
    bool LoadAndScale(const DisplayOptions &options, int frame_offset,
                      int frame_count) final;

    // Play video up to given duration.
    //
    // The reference to the "interrupt_received" can be updated by a signal
    // while the method is running and shall be checked often.
    void SendFrames(const Duration &duration, int loops,
                    const volatile sig_atomic_t &interrupt_received,
                    const Renderer::WriteFramebufferFun &sink) final;

    // Format title according to the format-string.
    std::string FormatTitle(const std::string &format_string) const final;

    bool IsAnimationBeforeFrameLimit() const override { return true; }

private:
    void AlphaBlendFramebuffer();

    DisplayOptions options_;
    bool maybe_transparent_ = false;
    int frame_offset_       = 0;
    int frame_count_        = -1;
    int orig_width_, orig_height_;

    int video_stream_index_          = -1;
    AVFormatContext *format_context_ = nullptr;
    AVCodecContext *codec_context_   = nullptr;
    SwsContext *sws_context_         = nullptr;
    timg::Duration frame_duration_;  // 1/fps
    timg::Framebuffer *terminal_fb_ = nullptr;
    int center_indentation_         = 0;
};

}  // namespace timg

#endif  // VIDEO_SOURCE_H_
