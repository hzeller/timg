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

namespace timg {

// Canvas that can send a framebuffer to a terminal.
class TerminalCanvas {
public:
    // Create a terminal canvas, sending to given file-descriptor.
    // Using either 'upper half block' or 'lower half block' to display
    // pixels. Which look depends on the font.
    TerminalCanvas(int fd, bool use_upper_half_block);
    TerminalCanvas(const TerminalCanvas &) = delete;
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

    // Ensure that all buffers needed for emitting the framebuffer have
    // enough space.
    // Return a buffer large enough to hold the whole ANSI-color encoded text.
    char *EnsureBuffers(int width, int height);

    char *AppendDoubleRow(char *pos, int indent, int width,
                          const Framebuffer::rgba_t *top_line,
                          const Framebuffer::rgba_t *bottom_line,
                          bool emit_difference);
    char *content_buffer_ = nullptr;  // Buffer containing content to write out
    size_t content_buffer_size_ = 0;

    struct DoubleRowColor {
        Framebuffer::rgba_t top;
        Framebuffer::rgba_t bottom;
        bool operator==(const DoubleRowColor &other) const {
            return top == other.top && bottom == other.bottom;
        }
    };
    DoubleRowColor *backing_buffer_ = nullptr;  // Remembering last frame
    size_t backing_buffer_size_ = 0;
    DoubleRowColor *last_content_iterator_;
    int last_framebuffer_height_ = 0;
    int last_x_indent_ = 0;

    Framebuffer::rgba_t *empty_line_ = nullptr;
    size_t empty_line_size_ = 0;
};
}  // namespace timg

#endif // TERMINAL_CANVAS_H_
