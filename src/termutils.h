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

#ifndef TIMG_TERMUTILS_H
#define TIMG_TERMUTILS_H

namespace timg {

// Determine size of terminal in pixels we can display.
struct TermSizeResult {
    // Not available values will be negative.
    int cols = -1;      // cell rows and columns
    int rows = -1;
    int font_width_px = -1;   // cell width and height in screen pixels.
    int font_height_px = -2;  // Negative, but right ratio if not available.
};
TermSizeResult DetermineTermSize();

// Attempt to determine the background color of current termninal.
// Returns allocated string if successful or nullptr if not.
char *DetermineBackgroundColor();

// Query the terminal if it supports the Kitty graphics protocol.
bool QueryHasKittyGraphics();

// Query if the terminal supports the iTerm2 graphics.
bool QueryHasITerm2Graphics();

// Get boolean value from named environment variable.
bool GetBoolenEnv(const char *env_name, bool default_value = false);

// Get float value from named environment variable.
float GetFloatEnv(const char *env_var, float default_value);

}  // namespace timg

#endif  // TIMG_TERMUTILS_H
