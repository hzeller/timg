// -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
// (c) 2016 Henner Zeller <h.zeller@acm.org>
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

#ifndef GRAPHICS_MAGICK_SOURCE_H_
#define GRAPHICS_MAGICK_SOURCE_H_

#include <signal.h>

#include <vector>

#include "display-options.h"
#include "image-source.h"
#include "renderer.h"
#include "terminal-canvas.h"
#include "timg-time.h"

namespace timg {

class GraphicsMagickSource final : public ImageSource {
public:
    explicit GraphicsMagickSource(const std::string &filename)
        : ImageSource(filename) {}
    ~GraphicsMagickSource() final;

    static const char *VersionInfo();

    // Attempt to load image(s) from filename and prepare for display.
    // Images are processed to fit in the given "display_width"x"display_height"
    // using ScaleOptions.
    // Transparent images are preprocessed with background and pattern_color
    // if set.
    // If this is not a loadable image, returns false, otherwise
    // We're ready for display.
    bool LoadAndScale(const DisplayOptions &options, int frame_offset,
                      int frame_count) final;

    // Display loaded image. If this is an animation, then
    // "duration", "max_frames" and "loops" will limit the duration of the
    // display. "max_frames" and "loops" with negative values mean infinite.
    //
    // The reference to the "interrupt_received" can be updated by a signal
    // while the method is running and shall be checked often.
    void SendFrames(const Duration &duration, int loops,
                    const volatile sig_atomic_t &interrupt_received,
                    const Renderer::WriteFramebufferFun &sink) final;

    // Format title according to the format-string.
    std::string FormatTitle(const std::string &format_string) const final;

    bool IsAnimationBeforeFrameLimit() const override {
        return is_animation_before_frame_limit_;
    }

private:
    class PreprocessedFrame;

    // Provide image scrolling in dx/dy direction for up to the given time.
    void Scroll(const Duration &duration, int loops,
                const volatile sig_atomic_t &interrupt_received, int dx, int dy,
                const Duration &scroll_delay,
                const Renderer::WriteFramebufferFun &write_fb);

    // Return how much we should indent a frame if centering is requested.
    int IndentationIfCentered(const PreprocessedFrame *frame) const;

    DisplayOptions options_;
    std::vector<PreprocessedFrame *> frames_;
    int orig_width_, orig_height_;
    int max_frames_;
    bool is_animation_before_frame_limit_ = false;
    bool is_animation_                    = false;
};

}  // namespace timg

#endif  // GRAPHICS_MAGICK_SOURCE_H_
