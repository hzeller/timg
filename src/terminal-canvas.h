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

#include "framebuffer.h"

#include <stddef.h>
#include <sys/types.h>

namespace timg {

// Canvas that can send a framebuffer to a terminal.
class TerminalCanvas {
public:
    // Create a terminal canvas, sending to given file-descriptor.
    TerminalCanvas(int fd);
    TerminalCanvas(const TerminalCanvas &) = delete;
    virtual ~TerminalCanvas() {}

    // Send frame to terminal. Move to xposition (relative to the left
    // of the screen, and delta y (relative to the current position) first.
    // Returns number of bytes written.
    virtual ssize_t Send(int x, int dy, const Framebuffer &framebuffer) = 0;

    ssize_t ClearScreen();
    ssize_t CursorOff();
    ssize_t CursorOn();

    ssize_t MoveCursorDY(int rows);  // negative: up^, positive: downV
    ssize_t MoveCursorDX(int cols);  // negative: <-left, positive: right->

    // Write buffer to the file descriptor this canvas is configured to.
    // Public, so can be used by other components that might need to write
    // text between framebuffer writes. Returns number of bytes written, which
    // should be the same as len.
    ssize_t WriteBuffer(const char *buffer, size_t len);

private:
    const int fd_;
};
}  // namespace timg

#endif // TERMINAL_CANVAS_H_
