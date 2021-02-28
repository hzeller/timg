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

#include "timg-time.h"

#include <limits>
#include "terminal-canvas.h"

namespace timg {
// Special sentinel value so signify a not initialized value on the command
// line.
static constexpr int kNotInitialized = std::numeric_limits<int>::min();

// Options influencing the rendering, chosen on the command-line or
// programmatically.
struct DisplayOptions {
    int width = -1;             // Output size. These need to be set to...
    int height = -1;            // ... valid values.
    float width_stretch = 1.0;  // To correct font squareness aspect ratio

    bool upscale = false;       // enlarging image only if this is true
    bool fill_width = false;    // Fill screen width, allow overflow height.
    bool fill_height = false;   // Fill screen height, allow overflow width.
    bool antialias = true;      // Try a pleasing antialiasing while scaling.
    bool center_horizontally = false;  // Try to center image
    int crop_border = 0;        // Pixels to be cropped around image.
    bool auto_crop = false;     // Autocrop, removing 'boring' space around.
                                // Done after crop-border has been applied.
    bool exif_rotate = true;    // Rotate image according to exif data found.
    bool show_filename = false; // show filename as title.

    // Scrolling specific
    bool scroll_animation = false; // Create an image scroll animation.
    int scroll_dx = 1;          // scroll direction in x-axis. Positive: left
    int scroll_dy = 0;          // scroll in y-direction.
    Duration scroll_delay = Duration::Millis(50); // delay between updates.

    bool allow_frame_skipping = false;  // skip frame if CPU or terminal slow

    // Transparency options for background shown.
    rgba_t bg_color;            // Background color
    rgba_t bg_pattern_color;    // Checkerboard other color.
};
}  // namespace timg
#endif  // DISPLAY_OPTIONS_H
