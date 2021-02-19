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
#include <string.h>

namespace timg {
class TerminalCanvas;

// Very simple framebuffer, storing widht*height pixels in RGBA format
// (always R=first byte, B=second byte; independent of architecture)
class Framebuffer {
public:
    // Note, this is always in little endian
    // 'red' is stored in the first byte, 'green' 2nd, 'blue' 3d, 'alpha' 4th
    typedef uint32_t rgba_t;

    Framebuffer(int width, int height);
    Framebuffer() = delete;
    Framebuffer(const Framebuffer &other) = delete;
    ~Framebuffer();

    void SetPixel(int x, int y, rgba_t value);
    void SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        SetPixel(x, y, to_rgba(r, g, b, 0xFF));
    }

    // Get pixel data at given position.
    rgba_t at(int x, int y) const;

    // Clear to fully transparent black pixels.
    void Clear();

    inline int width() const { return width_; }
    inline int height() const { return height_; }

    // Blend all transparent pixels with the given background and make
    // them a solid color.
    void AlphaComposeBackground(rgba_t bgcolor);

    // The raw internal buffer containing width()*height() pixels organized
    // from top left to bottom right.
    rgba_t *data() { return pixels_; }
    const rgba_t *data() const { return pixels_; }

    // -- the following two methods are useful with line-oriented sws_scale()
    // Return an array containing the amount of bytes for each line.
    // This is returned as an array.
    const int* stride() const { return strides_; }

    // Return an array containing pointers to the data for each line.
    uint8_t** row_data();

public:
    // Utility function to generate an rgba_t value from components.
    // Given red, green, blue and alpha value: convert to rgba_t type to the
    // correct byte order.
    static rgba_t to_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    // Parse a color given as string.
    static rgba_t ParseColor(const char *color);

private:
    const int width_;
    const int height_;
    rgba_t *const pixels_;
    int strides_[2];
    uint8_t** row_data_ = nullptr;  // Only allocated if requested.
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

    // Write buffer to the file descriptor this canvas is configured to.
    // Public, so can be used by other components that might need to write
    // informatin between framebuffer writes.
    void WriteBuffer(const char *buffer, size_t len);

private:
    const int fd_;

    const char *const set_upper_color_;
    const char *const set_lower_color_;
    const char *const pixel_character_;
    const bool top_optional_blank_;   // For odd height frames.

    // Return a buffer large enough to hold the whole ANSI-color encoded text.
    char *EnsureBuffer(int width, int height);

    char *AppendDoubleRow(char *pos, int indent, int width,
                          const Framebuffer::rgba_t *top_line,
                          const Framebuffer::rgba_t *bottom_line);
    char *content_buffer_ = nullptr;  // Buffer containing content to write out
    size_t buffer_size_ = 0;
};
}  // namespace timg

#endif // TERMINAL_CANVAS_H_
