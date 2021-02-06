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
struct DisplayOptions {
    int width = -1;             // These need to be set to valid values.
    int height = -1;
    // If image is smaller than screen, only upscale if do_upscale is set.
    bool upscale = false;
    bool fill_width = false;   // Fill screen width, allow overflow height.
    bool fill_height = false;  // Fill screen height, allow overflow width.
    bool antialias = true;     // Try a pleasing antialiasing while scaling.
    bool center_horizontally = false;  // Try to center image
    int crop_border = 0;       // Pixels to be cropped around image.
    bool auto_crop = false;    // Autocrop, removing 'boring' space around.
                               // Done after crop-border has been applied.
    bool exif_rotate = true;   // Rotate image according to exif data found.

    // Transparency options for background shown.
    const char *bg_color = nullptr;          // Background color
    const char *bg_pattern_color = nullptr;  // Checkerboard other color.
};

// Given an image with size "img_width" and "img_height", determine the
// target width and height satisfying the desired fit and size defined in
// the "display_options".
//
// As result, modfieis "target_width" and "target_height"; returns 'true' if
// the image has to be scaled, i.e. target size is different than image size.
bool CalcScaleToFitDisplay(int img_width, int img_height,
                           const DisplayOptions &display_options,
                           int *target_width, int *target_height);

class ImageLoader {
public:
    ~ImageLoader();

    static const char *VersionInfo();

    // Attempt to load image(s) from filename and prepare for display.
    // Images are processed to fit in the given "display_width"x"display_height"
    // using ScaleOptions.
    // Transparent images are preprocessed with background and pattern_color
    // if set.
    // If this is not a loadable image, returns false, otherwise
    // We're ready for display.
    bool LoadAndScale(const char *filename,
                      const DisplayOptions &options);

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

    // Return how much we should indent a frame if centering is requested.
    int IndentationIfCentered(const PreprocessedFrame *frame) const;

    int display_width_;
    int display_height_;
    std::vector<PreprocessedFrame *> frames_;
    bool is_animation_ = false;
    bool center_horizontally_ = false;
};

}  // namespace timg

#endif // IMAGE_DISPLAY_H_
