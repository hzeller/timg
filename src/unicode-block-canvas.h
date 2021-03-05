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

#ifndef UNICODE_BLOCK_CANVAS_H_
#define UNICODE_BLOCK_CANVAS_H_

#include "terminal-canvas.h"

#include <stddef.h>
#include <sys/types.h>

namespace timg {

// Canvas that can send a framebuffer to a terminal.
class UnicodeBlockCanvas final : public TerminalCanvas {
public:
    // Create a terminal canvas, sending to given file-descriptor.
    UnicodeBlockCanvas(int fd, bool use_upper_half_block);
    ~UnicodeBlockCanvas() override;

    ssize_t Send(int x, int dy, const Framebuffer &framebuffer) override;

private:
    struct GlyphPick;
    const bool use_upper_half_block_;

    // Ensure that all buffers needed for emitting the framebuffer have
    // enough space.
    // Return a buffer large enough to hold the whole ANSI-color encoded text.
    char *EnsureBuffers(int width, int height);

    template <int N>
    char *AppendDoubleRow(char *pos, int indent, int width,
                          const rgba_t *top_line,
                          const rgba_t *bottom_line,
                          bool emit_difference, int *y_skip);

    // Find best glyph for two rows of color.
    template <int N>
    GlyphPick FindBestGlyph(const rgba_t *top, const rgba_t *bottom) const;

    char *content_buffer_ = nullptr;  // Buffer containing content to write out
    size_t content_buffer_size_ = 0;

    // Backing buffer stores a flattened view of last frame, storing top and
    // bottom pixel linearly.
    rgba_t *backing_buffer_ = nullptr;  // Remembering last frame
    size_t backing_buffer_size_ = 0;
    rgba_t *prev_content_it_;
    int last_framebuffer_height_ = 0;
    int last_x_indent_ = 0;

    rgba_t *empty_line_ = nullptr;
    size_t empty_line_size_ = 0;
};
}  // namespace timg

#endif  // UNICODE_BLOCK_CANVAS_H_
