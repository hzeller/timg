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

#define SCREEN_CURSOR_UP_FORMAT    "\033[%dA"  // Move cursor up given lines.
#define SCREEN_CURSOR_RIGHT_FORMAT "\033[%dC"  // Move cursor right given cols

namespace timg {
ITerm2GraphicsCanvas::ITerm2GraphicsCanvas(int fd, const DisplayOptions &opts)
    : TerminalCanvas(fd), options_(opts) {
}

// ITerm2 does not read pixles directly but image file content. Closest to our
// representation is PPM (netpbm), so use that to encode; but might be
// worthwhile re-encoding things as PNG for more compact transmission ?
static char* EncodeFramebuffer(char *pos, const Framebuffer &fb) {
    static constexpr char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    pos += sprintf(pos, "\e]1337;File=width=%dpx;height=%dpx;inline=1:",
                   fb.width(), fb.height());

    char ppm_header[32];
    int header_len = snprintf(ppm_header, sizeof(ppm_header),
                              "P6\n%4d  %4d\n255\n", fb.width(), fb.height());
    assert(header_len % 3 == 0);
    // For ease of encoding later, we padded the header to multiple of 3 bytes.
    // That way, we get a multiple of 4 bytes encoded.
    for (const char *h = ppm_header; h < ppm_header + header_len; h += 3) {
        *pos++ = b64[(h[0] >> 2) & 0x3f];
        *pos++ = b64[((h[0] & 0x03) << 4) | ((int) (h[1] & 0xf0) >> 4)];
        *pos++ = b64[((h[1] & 0x0f) << 2) | ((int) (h[2] & 0xc0) >> 6)];
        *pos++ = b64[h[2] & 0x3f];
    }

    // Image content. Each RGB triple is encoded to 4 base64 bytes.
    const rgba_t *end = fb.data() + fb.width() * fb.height();
    for (const rgba_t *it = fb.data(); it < end; ++it) {
        *pos++ = b64[(it->r >> 2) & 0x3f];
        *pos++ = b64[((it->r & 0x03) << 4) | ((int) (it->g & 0xf0) >> 4)];
        *pos++ = b64[((it->g & 0x0f) << 2) | ((int) (it->b & 0xc0) >> 6)];
        *pos++ = b64[it->b & 0x3f];
    }

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
    // Allocate enough to do RGBA encoding, though currently, we only do RGB
    const int encoded_base64_rgba_size =  (4 * width * height + 2) * 4 / 3;
    const size_t new_content_size =
        strlen(SCREEN_CURSOR_UP_FORMAT) + 2        // extra space for digits
        + strlen(SCREEN_CURSOR_RIGHT_FORMAT) + 2
        + encoded_base64_rgba_size
        + strlen("\e\1337;File=inline=1:")
        + 2;  /* \b\n */

    if (new_content_size > content_buffer_size_) {
        content_buffer_ = (char*)realloc(content_buffer_, new_content_size);
        content_buffer_size_ = new_content_size;
    }

    return content_buffer_;
}
}  // namespace timg
