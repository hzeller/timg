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

#ifndef TERMINAL_CANVAS_H_
#define TERMINAL_CANVAS_H_

#include <string>
#include <stdint.h>

class TerminalCanvas {
public:
    TerminalCanvas(int width, int heigh, bool dont_clear);

    int width() const { return width_; }
    int height() const { return height_; }

    void SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);
    void Send(int fd);

    // To be called once
    static void GlobalInit(int fd, bool dont_clear);
    static void GlobalFinish(int fd);

private:
    const int width_;
    const int height_;
    size_t initial_offset_;
    size_t pixel_offset_;
    size_t lower_row_pixel_offset_;
    size_t color_fmt_length_;
    std::string buffer_;
    bool dont_clear_;
};

#endif // TERMINAL_CANVAS_H_
