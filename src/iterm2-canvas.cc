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
ITerm2GraphicsCanvas::ITerm2GraphicsCanvas(BufferedWriteSequencer *ws,
                                           const DisplayOptions &opts)
    : TerminalCanvas(ws), options_(opts) {}

void ITerm2GraphicsCanvas::Send(int x, int dy, const Framebuffer &fb,
                                SeqType seq_type, Duration end_of_frame) {
    char *const buffer = RequestBuffer(fb.width(), fb.height());
    char *pos          = buffer;

    if (dy < 0)
        MoveCursorDY(-((-dy + options_.cell_y_px - 1) / options_.cell_y_px));
    MoveCursorDX(x / options_.cell_x_px);

    pos = AppendPrefixToBuffer(pos);

    int png_size =
        timg::EncodePNG(fb, options_.compress_pixel_format ? 1 : 0,
                        options_.local_alpha_handling ? ColorEncoding::kRGB_24
                                                      : ColorEncoding::kRGBA_32,
                        png_buf_, png_buf_size_);

    if (!png_size) return;  // Error. Ignore.

    pos += sprintf(pos,
                   "\e]1337;File=width=%dpx;height=%dpx;inline=1:", fb.width(),
                   fb.height());
    pos = timg::EncodeBase64(png_buf_, png_size, pos);

    *pos++ = '\007';
    *pos++ = '\n';  // Need one final cursor movement.

    write_sequencer_->WriteBuffer(buffer, pos - buffer, seq_type, end_of_frame);
}

ITerm2GraphicsCanvas::~ITerm2GraphicsCanvas() { free(png_buf_); }

char *ITerm2GraphicsCanvas::RequestBuffer(int width, int height) {
    // We don't really know how much size the encoded image takes, though one
    // would expect typically smaller, and hopefully not more than twice...
    const size_t png_compressed_size = (4 * width * height) * 2;
    const int encoded_base64_size    = png_compressed_size * 4 / 3;
    const size_t content_size =
        strlen(SCREEN_CURSOR_UP_FORMAT) + strlen(SCREEN_CURSOR_RIGHT_FORMAT) +
        encoded_base64_size  //
        + strlen("\e\1337;File=width=9999px;height=9999px;inline=1:\007") + 4 +
        1; /* digit space for cursor up/right; \n */

    if (png_compressed_size > png_buf_size_) {
        png_buf_      = (char *)realloc(png_buf_, png_compressed_size);
        png_buf_size_ = png_compressed_size;
    }

    return write_sequencer_->RequestBuffer(content_size);
}
}  // namespace timg
