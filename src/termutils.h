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

#include <string>

namespace timg {

// Determine size of terminal in pixels we can display.
struct TermSizeResult {
    // Not available values will be negative.
    int cols           = -1;  // cell rows and columns
    int rows           = -1;
    int font_width_px  = -1;  // cell width and height in screen pixels.
    int font_height_px = -2;  // Negative, but right ratio if not available.
};
TermSizeResult DetermineTermSize();

// Get boolean value from named environment variable.
bool GetBoolenEnv(const char *env_name, bool default_value = false);

// Get float value from named environment variable.
float GetFloatEnv(const char *env_var, float default_value);

// Given number of bytes, return a human-readable version of that
// (e.g. "13.2 MiB").
// TODO: this is more a regular util function, not a termutil. Move in separate
// header.
std::string HumanReadableByteValue(int64_t byte_count);
}  // namespace timg

#endif  // TIMG_TERMUTILS_H
