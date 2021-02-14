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

namespace timg {
class TerminalCanvas;

// Very simple framebuffer.
class Framebuffer {
public:
    typedef uint32_t rgb_t;

    Framebuffer(int width, int height);
    Framebuffer() = delete;
    Framebuffer(const Framebuffer &other) = delete;
    ~Framebuffer();

    void SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);
    void SetPixel(int x, int y, rgb_t value);
    rgb_t at(int x, int y) const;

    void Clear();

    inline int width() const { return width_; }
    inline int height() const { return height_; }

    rgb_t *data() { return pixels_; }

private:
    friend class TerminalCanvas;
    const int width_;
    const int height_;
    rgb_t *const pixels_;
};

// Canvas that can send a framebuffer to a terminal.
class TerminalCanvas {
public:
    // Create a terminal canvas, sending to given file-descriptor.
    // Using either 'upper half block' or 'lower half block' to display
    // pixels. Which look depends on the font.
    TerminalCanvas(int fd, bool use_upper_half_block);
    ~TerminalCanvas();

    // Send frame to terminal. Move to xposition (relative to the left
    // of the screen, and delta y (relative to the current position) first.
    void Send(int x, int dy, const Framebuffer &framebuffer);

    void ClearScreen();
    void CursorOff();
    void CursorOn();

    void MoveCursorDY(int rows);  // negative: up^, positive: downV
    void MoveCursorDX(int cols);  // negative: <-left, positive: right->

    // Filedescriptor we're writing to.
    int fd() const { return fd_; }
private:
    const int fd_;

    const char *const set_upper_color_;
    const char *const set_lower_color_;
    const char *const pixel_character_;
    const bool top_optional_blank_;   // For odd height frames.

    // Return a buffer large enough to hold the whole ANSI-color encoded text.
    char *EnsureBuffer(int width, int height, int indent);

    char *content_buffer_ = nullptr;  // Buffer containing content to write out
    size_t buffer_size_ = 0;
};
}  // namespace timg

#endif // TERMINAL_CANVAS_H_
