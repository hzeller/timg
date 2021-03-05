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

#include "kitty-canvas.h"

#include <assert.h>

#define SCREEN_CURSOR_UP_FORMAT    "\033[%dA"  // Move cursor up given lines.
#define SCREEN_CURSOR_RIGHT_FORMAT "\033[%dC"  // Move cursor right given cols

static constexpr int kChunkSize = 4096;  // Max allowed: 4096.

namespace timg {
KittyGraphicsCanvas::KittyGraphicsCanvas(int fd, const DisplayOptions &opts)
    : TerminalCanvas(fd), options_(opts) {
}

// Currently writing 24bit RGB to minimize bytes written to terminal and to
// have nice even mapping to base64 encoding.
// We already did all the transparency blending upstream anyway.
static char* EncodeFramebufferChunked(char *pos, const Framebuffer &fb) {
    static constexpr char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const int total_encoded =  (3 * fb.width() * fb.height()) * 4 / 3;
    pos += sprintf(pos, "\e_Ga=T,f=24,s=%d,v=%d,m=%d;",
                   fb.width(), fb.height(), total_encoded > kChunkSize);
    int written = 0;
    // TODO: consider zlib compression of raw data. Protocol allows for that.
    const rgba_t *end = fb.data() + fb.width() * fb.height();
    for (const rgba_t *it = fb.data(); it < end; ++it) {
        *pos++ = b64[(it->r >> 2) & 0x3f];
        *pos++ = b64[((it->r & 0x03) << 4) | ((int) (it->g & 0xf0) >> 4)];
        *pos++ = b64[((it->g & 0x0f) << 2) | ((int) (it->b & 0xc0) >> 6)];
        *pos++ = b64[it->b & 0x3f];
        written += 4;
        if (written % kChunkSize == 0 && total_encoded - written > 0) {
            pos += sprintf(pos, "\e\\\e_Gm=%d;",
                           (total_encoded - written) > kChunkSize);
        }
    }
    // (No base64 trailer needed as right now, we have multiple of 4 bytes.)
    *pos++ = '\e';
    *pos++ = '\\';

    *pos++ = '\n';  // Need one final cursor movement.
    return pos;
}

ssize_t KittyGraphicsCanvas::Send(int x, int dy, const Framebuffer &fb) {
    char *const buffer = EnsureBuffer(fb.width(), fb.height());
    char *pos = buffer;
    if (dy < 0) {
        pos += sprintf(pos, SCREEN_CURSOR_UP_FORMAT,
                       (-dy + options_.cell_y_px - 1) / options_.cell_y_px);
    }
    if (x > 0) {
        pos += sprintf(pos, SCREEN_CURSOR_RIGHT_FORMAT, x / options_.cell_x_px);
    }

    pos = EncodeFramebufferChunked(pos, fb);
    return WriteBuffer(buffer, pos - buffer);
}

KittyGraphicsCanvas::~KittyGraphicsCanvas() {
    free(content_buffer_);
}

char *KittyGraphicsCanvas::EnsureBuffer(int width, int height) {
    // Allocate enough to do RGBA encoding, though currently, we only do RGB
    const int encoded_base64_rgba_size =  (4 * width * height + 2) * 4 / 3;
    const size_t new_content_size =
        strlen(SCREEN_CURSOR_UP_FORMAT) + 2        // extra space for digits
        + strlen(SCREEN_CURSOR_RIGHT_FORMAT) + 2
        + encoded_base64_rgba_size
        + strlen("\e_Ga=T,f=XX,s=9999,v=9999,m=1;\e\\")
        + (encoded_base64_rgba_size / kChunkSize) * strlen("\e_Gm=0;\e\\")
        + 1;  /* \n */

    if (new_content_size > content_buffer_size_) {
        content_buffer_ = (char*)realloc(content_buffer_, new_content_size);
        content_buffer_size_ = new_content_size;
    }

    return content_buffer_;
}
}  // namespace timg
