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

#include "iterm2-canvas.h"

#include <assert.h>

#include "timg-base64.h"
#include "timg-png.h"

#define SCREEN_CURSOR_UP_FORMAT    "\033[%dA"  // Move cursor up given lines.
#define SCREEN_CURSOR_RIGHT_FORMAT "\033[%dC"  // Move cursor right given cols

namespace timg {
ITerm2GraphicsCanvas::ITerm2GraphicsCanvas(int fd, const DisplayOptions &opts)
    : TerminalCanvas(fd), options_(opts) {
}

ssize_t ITerm2GraphicsCanvas::Send(int x, int dy, const Framebuffer &fb) {
    char *const buffer = EnsureBuffer(fb.width(), fb.height());
    char *pos = buffer;
    if (dy < 0) {
        pos += sprintf(pos, SCREEN_CURSOR_UP_FORMAT,
                       (-dy + options_.cell_y_px - 1) / options_.cell_y_px);
    }
    if (x > 0) {
        pos += sprintf(pos, SCREEN_CURSOR_RIGHT_FORMAT, x / options_.cell_x_px);
    }

    int png_size = timg::EncodePNG(fb, options_.compress_pixel_format ? 1 : 0,
                                   options_.local_alpha_handling
                                   ? ColorEncoding::kRGB_24
                                   : ColorEncoding::kRGBA_32,
                                   png_buf_, png_buf_size_);

    if (!png_size) return pos - buffer;  // Error. Ignore.

    pos += sprintf(pos, "\e]1337;File=width=%dpx;height=%dpx;inline=1:",
                   fb.width(), fb.height());
    pos = timg::EncodeBase64(png_buf_, png_size, pos);

    *pos++ = '\007';
    *pos++ = '\n';  // Need one final cursor movement.

    return WriteBuffer(buffer, pos - buffer);
}

ITerm2GraphicsCanvas::~ITerm2GraphicsCanvas() {
    free(content_buffer_);
    free(png_buf_);
}

char *ITerm2GraphicsCanvas::EnsureBuffer(int width, int height) {
    // We don't really know how much size the encoded image takes, though one
    // would expect typically smaller, and hopefully not more than twice...
    const size_t png_compressed_size = (4 * width * height) * 2;
    const int encoded_base64_size = png_compressed_size * 4 / 3;
    const size_t new_content_size =
        strlen(SCREEN_CURSOR_UP_FORMAT) + strlen(SCREEN_CURSOR_RIGHT_FORMAT)
        + encoded_base64_size
        + strlen("\e\1337;File=width=9999px;height=9999px;inline=1:\007")
        + 4 + 1;  /* digit space for cursor up/right; \n */

    if (new_content_size > content_buffer_size_) {
        content_buffer_ = (char*)realloc(content_buffer_, new_content_size);
        content_buffer_size_ = new_content_size;
    }

    if (png_compressed_size > png_buf_size_) {
        png_buf_ = (char*)realloc(png_buf_, png_compressed_size);
        png_buf_size_ = png_compressed_size;
    }

    return content_buffer_;
}
}  // namespace timg
