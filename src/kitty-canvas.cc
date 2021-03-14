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

#include <algorithm>

#include "timg-base64.h"

#define SCREEN_CURSOR_UP_FORMAT    "\033[%dA"  // Move cursor up given lines.
#define SCREEN_CURSOR_RIGHT_FORMAT "\033[%dC"  // Move cursor right given cols

static constexpr int kBase64EncodedChunkSize = 4096;  // Max allowed: 4096.

namespace timg {
KittyGraphicsCanvas::KittyGraphicsCanvas(int fd, const DisplayOptions &opts)
    : TerminalCanvas(fd), options_(opts) {
}

// Currently writing 24bit RGB to minimize bytes written to terminal and to
// have nice even mapping to base64 encoding.
// We already did all the transparency blending upstream anyway.
//
// TODO: send image with an ID so that in an animation we can just replace
// the image with the same ID.
// First tests didn't work: sending them with a=t,i=<id>,q=1
// right afterwards attempting to a=p,i=<id>, in the same write()
// does not work at all (timing ? Maybe it first has to store it somewhere
// and only then the ID is available ?)
// Sending them with a=T,i=id,q=1 works somewhat, but is very flickery
// (probably need two different ids to switch around)
//
// Anyway, for now, just a=T direct placement, and not using andy ID. Which
// means, we probably cycle through a lot of memory in the Graphics adapter
// when showing videos :)
//
// TODO: consider zlib compression of raw data. Protocol allows for that.
// Then even RGBA might be very cheap and we don't have to go out of our way
// to skip all the A bytes.
static char* EncodeFramebufferChunked(char *pos, const Framebuffer &fb) {
    // Number of pixels to be emitted per chunk.
    // One pixel encodes to 4 base64 bytes.
    static constexpr int kPixelChunk = kBase64EncodedChunkSize / 4;

    int pixels_left = fb.width() * fb.height();
    pos += sprintf(pos, "\e_Ga=T,f=24,s=%d,v=%d,m=%d;",
                   fb.width(), fb.height(), pixels_left > kPixelChunk);

    int offset = 0;
    while (pixels_left) {
        int chunk_pixels = std::min(pixels_left, kPixelChunk);
        pos = timg::EncodeBase64(fb.rgb_begin(offset), 3 * chunk_pixels, pos);
        pixels_left -= chunk_pixels;
        offset += chunk_pixels;

        if (pixels_left)
            pos += sprintf(pos, "\e\\\e_Gm=%d;", pixels_left > kPixelChunk);
    }

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
    const int encoded_base64_rgba =  (4 * width * height + 2) * 4 / 3;
    const size_t new_content_size =
        strlen(SCREEN_CURSOR_UP_FORMAT) + strlen(SCREEN_CURSOR_RIGHT_FORMAT)
        + encoded_base64_rgba
        + strlen("\e_Ga=T,f=XX,s=9999,v=9999,m=1;\e\\")
        + (encoded_base64_rgba / kBase64EncodedChunkSize)*strlen("\e_Gm=0;\e\\")
        + 4 + 1;  /* digit space for cursor up/right; \n */

    if (new_content_size > content_buffer_size_) {
        content_buffer_ = (char*)realloc(content_buffer_, new_content_size);
        content_buffer_size_ = new_content_size;
    }

    return content_buffer_;
}
}  // namespace timg
