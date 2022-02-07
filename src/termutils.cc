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

#include "termutils.h"

#include <strings.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace timg {
// Probe all file descriptors that might be connect to tty for term size.
TermSizeResult DetermineTermSize() {
    TermSizeResult result;
    for (int fd : {STDOUT_FILENO, STDERR_FILENO, STDIN_FILENO}) {
        struct winsize w = {};
        if (ioctl(fd, TIOCGWINSZ, &w) != 0) continue;
        // If we get the size of the terminals in pixels, we can determine
        // what aspect ratio the pixels have and correct if they not 1:2
        // Infer the font size if we have window pixel size available.
        // Do some basic plausibility check here.
        if (w.ws_xpixel >= 2 * w.ws_col && w.ws_ypixel >= 4 * w.ws_row &&
            w.ws_col > 0 && w.ws_row > 0) {
            result.font_width_px  = w.ws_xpixel / w.ws_col;
            result.font_height_px = w.ws_ypixel / w.ws_row;
        }
        result.cols = w.ws_col;
        result.rows = w.ws_row;
        break;
    }
    return result;
}

bool GetBoolenEnv(const char *env_name, bool default_value) {
    const char *const value = getenv(env_name);
    if (!value) return default_value;
    return (atoi(value) > 0 || strcasecmp(value, "on") == 0 ||
            strcasecmp(value, "yes") == 0);
}

float GetFloatEnv(const char *env_var, float default_value) {
    const char *value = getenv(env_var);
    if (!value) return default_value;
    char *err    = nullptr;
    float result = strtof(value, &err);
    return (*err == '\0' ? result : default_value);
}

std::string HumanReadableByteValue(int64_t byte_count) {
    float print_bytes = byte_count;
    const char *unit  = "Bytes";
    if (print_bytes > (10LL << 30)) {
        print_bytes /= (1 << 30);
        unit = "GiB";
    }
    else if (print_bytes > (10 << 20)) {
        print_bytes /= (1 << 20);
        unit = "MiB";
    }
    else if (print_bytes > (10 << 10)) {
        print_bytes /= (1 << 10);
        unit = "KiB";
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f %s", print_bytes, unit);
    return buf;
}

}  // namespace timg
