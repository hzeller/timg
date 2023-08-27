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

#include <cassert>

#include "timg-base64.h"
#include "timg-png.h"

#define SCREEN_CURSOR_UP_FORMAT    "\033[%dA"  // Move cursor up given lines.
#define SCREEN_CURSOR_RIGHT_FORMAT "\033[%dC"  // Move cursor right given cols

namespace timg {
ITerm2GraphicsCanvas::ITerm2GraphicsCanvas(BufferedWriteSequencer *ws,
                                           ThreadPool *thread_pool,
                                           const DisplayOptions &opts)
    : TerminalCanvas(ws), options_(opts), executor_(thread_pool) {}

void ITerm2GraphicsCanvas::Send(int x, int dy, const Framebuffer &fb_orig,
                                SeqType seq_type, Duration end_of_frame) {
    if (dy < 0) {
        MoveCursorDY(cell_height_for_pixels(dy));
    }
    MoveCursorDX(x / options_.cell_x_px);

    // Create copy to be used in threads.
    const Framebuffer *const fb = new Framebuffer(fb_orig);
    char *const buffer          = RequestBuffer(fb->width(), fb->height());
    char *const offset          = AppendPrefixToBuffer(buffer);

    const auto &options                   = options_;
    std::function<OutBuffer()> encode_fun = [options, fb, buffer, offset]() {
        std::unique_ptr<const Framebuffer> auto_delete(fb);
        const size_t png_buf_size = png::UpperBound(fb->width(), fb->height());
        std::unique_ptr<char[]> png_buf(new char[png_buf_size]);

        const int png_size = png::Encode(*fb, options.compress_pixel_level,
                                         options.local_alpha_handling
                                             ? png::ColorEncoding::kRGB_24
                                             : png::ColorEncoding::kRGBA_32,
                                         png_buf.get(), png_buf_size);

        char *pos = offset;
        pos += sprintf(pos,
                       "\e]1337;File=size=%d;width=%dpx;height=%dpx;inline=1:",
                       png_size, fb->width(), fb->height());
        pos = timg::EncodeBase64(png_buf.get(), png_size, pos);

        *pos++ = '\007';
        *pos++ = '\n';  // Need one final cursor movement.
        return OutBuffer(buffer, pos - buffer);
    };
    write_sequencer_->WriteBuffer(executor_->ExecAsync(encode_fun), seq_type,
                                  end_of_frame);
}

char *ITerm2GraphicsCanvas::RequestBuffer(int width, int height) {
    const size_t png_compressed_size = png::UpperBound(width, height);
    const int encoded_base64_size    = png_compressed_size * 4 / 3;
    const size_t content_size =
        strlen(SCREEN_CURSOR_UP_FORMAT) + strlen(SCREEN_CURSOR_RIGHT_FORMAT) +
        encoded_base64_size  //
        + strlen("\e\1337;File=width=9999px;height=9999px;inline=1:\007") + 4 +
        1; /* digit space for cursor up/right; \n */

    return new char[content_size];
}

int ITerm2GraphicsCanvas::cell_height_for_pixels(int pixels) const {
    assert(pixels <= 0);  // Currently only use-case
    // Round up to next full pixel cell.
    return -((-pixels + options_.cell_y_px - 1) / options_.cell_y_px);
}

}  // namespace timg
