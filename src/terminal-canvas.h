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
    TerminalCanvas(int width, int heigh);

    int width() const { return internal_width_ - 1; }
    int height() const { return height_; }

    void SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);

    // Send image to terminal. If jump_back_first is set, jump up the
    // number of rows first.
    void Send(int fd, bool jump_back_first);

    static void ClearScreen(int fd);
    static void CursorOff(int fd);
    static void CursorOn(int fd);

private:
    const int internal_width_;
    const int height_;
    size_t initial_offset_;
    size_t pixel_offset_;
    size_t lower_row_pixel_offset_;
    std::string buffer_;
};

#endif // TERMINAL_CANVAS_H_
