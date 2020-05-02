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

#ifndef IMAGE_DISPLAY_H_
#define IMAGE_DISPLAY_H_

#include <vector>
#include <signal.h>

#include "timg-time.h"
#include "terminal-canvas.h"

namespace timg {
struct ScaleOptions {
    // If image is smaller than screen, only upscale if do_upscale is set.
    bool do_upscale = false;
    bool fill_width = false;   // Fill screen width, allow overflow height.
    bool fill_height = false;  // Fill screen height, allow overflow width.
    bool do_antialias = true;  // Try a pleasing antialiasing while scaling.
};

// Determine the target width and height given the incoming image size
// and desired fit in screen-width.
// Returns 'true' if the image has to be scaled.
bool ScaleWithOptions(int img_width, int img_height,
                      int screen_width, int screen_height,
                      const ScaleOptions &options,
                      int *target_width, int *target_height);

class ImageLoader {
public:
    ~ImageLoader();

    // Attempt to load image(s) from filename and prepare for display.
    // Images are processed to fit in the given "display_width"x"display_height"
    // using ScaleOptions.
    // Transparent images are preprocessed with background and pattern_color
    // if set.
    // If this is not a loadable image, returns false, otherwise
    // We're ready for display.
    bool LoadAndScale(const char *filename,
                      int display_width, int display_height,
                      const ScaleOptions &options,
                      const char *bg_color, const char *pattern_color);

    // Display loaded image. If this is an animation, then
    // "duration", "max_frames" and "loops" will limit the duration of the
    // display. "max_frames" and "loops" with negative values mean infinite.
    //
    // The reference to the "interrupt_received" can be updated by a signal
    // while the method is running and shall be checked often.
    void Display(Duration duration, int max_frames, int loops,
                 const volatile sig_atomic_t &interrupt_received,
                 timg::TerminalCanvas *canvas);

    // Provide image scrolling in dx/dy direction for up to the given time.
    void Scroll(Duration duration, int loops,
                const volatile sig_atomic_t &interrupt_received,
                int dx, int dy,  Duration scroll_delay,
                timg::TerminalCanvas *canvas);

    bool is_animation() const { return is_animation_; }

private:
    class PreprocessedFrame;
    int display_width_;
    int display_height_;
    std::vector<PreprocessedFrame *> frames_;
    bool is_animation_ = false;
};

}  // namespace timg

#endif // IMAGE_DISPLAY_H_
