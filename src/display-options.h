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
#ifndef DISPLAY_OPTIONS_H
#define DISPLAY_OPTIONS_H

#include <stdlib.h>

#include <functional>
#include <limits>
#include <string>

#include "framebuffer.h"
#include "timg-time.h"

namespace timg {
// Special sentinel value so signify a not initialized value on the command
// line.
static constexpr int kNotInitialized = std::numeric_limits<int>::min();

// Options influencing the rendering, chosen on the command-line or
// programmatically.
struct DisplayOptions {
    DisplayOptions() {
        static constexpr const char *kFormatEnv = "TIMG_DEFAULT_TITLE";
        if (getenv(kFormatEnv)) title_format = getenv(kFormatEnv);
    }

    int width  = -1;  // Output size. These need to be set to...
    int height = -1;  // ... valid values.

    int cell_x_px = 1;  // Pixels shown in one character cell
    int cell_y_px = 2;  // This depends on TerminalCanvas

    // Terminals that allow to transfer high-resolution pixels (Kitty,
    // iTerm2, and WezTerm) often allow some compressed option. This reduces
    // the amount of data that has to be sent to the terminal (in particular
    // useful when SSH-ed in remotely), at the expense of more CPU time
    // used by timg to re-compress (usefulness might be negative when playing
    // a video locally). Compression is done in separate thread.
    int compress_pixel_level = 1;

    float width_stretch = 1.0;  // To correct font squareness aspect ratio

    bool upscale         = false;  // enlarging image only if this is true
    bool upscale_integer = false;  // Upscale only in integer increments.
    bool fill_width      = false;  // Fill screen width, allow overflow height.
    bool fill_height     = false;  // Fill screen height, allow overflow width.
    bool antialias       = true;   // Try a pleasing antialiasing while scaling.
    bool center_horizontally = false;  // Try to center image
    int crop_border          = 0;      // Pixels to be cropped around image.
    bool auto_crop = false;    // Autocrop, removing 'boring' space around.
                               // Done after crop-border has been applied.
    bool exif_rotate = true;   // Rotate image according to exif data found.
    bool show_title  = false;  // show title.

    // Format for title, can contain %-format like place holders
    //  %f = filename
    //  %w = width
    //  %h = height
    //  %D = decoder used (image, video, jpeg, ...)
    std::string title_format = "%f";  // Default: just filename

    // Scrolling specific
    bool scroll_animation = false;  // Create an image scroll animation.
    int scroll_dx         = 1;  // scroll direction in x-axis. Positive: left
    int scroll_dy         = 0;  // scroll in y-direction.
    Duration scroll_delay = Duration::Millis(50);  // delay between updates.

    bool allow_frame_skipping = false;  // skip frame if CPU or terminal slow

    //-- Background options for transparent images --
    bool local_alpha_handling = true;  // If we alpha blend locally
    // "bgcolor_getter" is a function that can be called to retrieve the
    // desired background color that transparent parts of images should be
    // alpha-blended with.
    //
    // Return value is either a solid (alpha=0xff) color to merge, or
    // fully transparent (alpha=0x00) to indicate that no alpha-blending
    // should happen.
    //
    // Unlike all the other direct values in this struct, this is a function
    // to allow for asynchonrous retrieval of the value, so it doesn't have to
    // be ready at the time this DisplayOptions is created (Background: some
    // terminal emulator take a while to respond to the background color query).
    std::function<rgba_t()> bgcolor_getter;

    // In case of background color alpha merging, this is the optional
    // 'checkerboard' color if alpha=0xff, or no checkerboard if alpha=0x00.
    rgba_t bg_pattern_color = {0x00, 0x00, 0x00, 0x00};

    // Factor of pattern-size from the default
    int pattern_size = 1;
};
}  // namespace timg
#endif  // DISPLAY_OPTIONS_H
