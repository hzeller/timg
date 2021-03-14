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

#define SCREEN_CURSOR_UP_FORMAT    "\033[%dA"  // Move cursor up given lines.
#define SCREEN_CURSOR_RIGHT_FORMAT "\033[%dC"  // Move cursor right given cols

namespace timg {
ITerm2GraphicsCanvas::ITerm2GraphicsCanvas(int fd, const DisplayOptions &opts)
    : TerminalCanvas(fd), options_(opts) {
}

// iTerm2 does not read pixels directly but image file content. Filetype
// closest to our representation is PPM (netpbm), so use that to encode: just
// a short header is needed.
// Might be worthwhile compressing as PNG for more compact transmission ?
static char* EncodeFramebuffer(char *pos, const Framebuffer &fb) {
    pos += sprintf(pos, "\e]1337;File=width=%dpx;height=%dpx;inline=1:",
                   fb.width(), fb.height());

    char ppm_header[32];
    int header_len = snprintf(ppm_header, sizeof(ppm_header),
                              "P6\n%4d  %4d\n255\n", fb.width(), fb.height());
    assert(header_len % 3 == 0);  // Don't want do deal with base64== suffix
    pos = timg::EncodeBase64(ppm_header, header_len, pos);
    pos = timg::EncodeBase64(fb.rgb_begin(), 3 * fb.width()*fb.height(), pos);
    *pos++ = '\007';

    *pos++ = '\n';  // Need one final cursor movement.
    return pos;
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

    pos = EncodeFramebuffer(pos, fb);
    return WriteBuffer(buffer, pos - buffer);
}

ITerm2GraphicsCanvas::~ITerm2GraphicsCanvas() {
    free(content_buffer_);
}

char *ITerm2GraphicsCanvas::EnsureBuffer(int width, int height) {
    const int encoded_base64_rgb_size = (3 * width * height) * 4 / 3;
    const size_t new_content_size =
        strlen(SCREEN_CURSOR_UP_FORMAT) + strlen(SCREEN_CURSOR_RIGHT_FORMAT)
        + encoded_base64_rgb_size + 100 /* header */
        + strlen("\e\1337;File=width=9999px;height=9999px;inline=1:\007")
        + 4 + 1;  /* digit space for cursor up/right; \n */

    if (new_content_size > content_buffer_size_) {
        content_buffer_ = (char*)realloc(content_buffer_, new_content_size);
        content_buffer_size_ = new_content_size;
    }

    return content_buffer_;
}
}  // namespace timg
