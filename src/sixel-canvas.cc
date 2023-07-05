// -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
// (c) 2023 Henner Zeller <h.zeller@acm.org>
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

#include "sixel-canvas.h"

#include "thread-pool.h"
#include <sixel.h>

// Terminals might have different understanding where the curosr is placed
// after an image is sent. This is a somewhat undocumented feature of xterm.
// This will make sure that in particular images shown in columns will align
// properly.
// This works in xterm, mlterm and wezterm.
// Konsole does _not_ seem to understand this (use iterm2 mode there instead)
#define PLACE_CURSOR_AFTER_SIXEL_IMAGE "\033[?8452h"

namespace timg {

// Char needs to be non-const to be compatible sixel-callback.
static int WriteToOutBuffer(char *data, int size, void *outbuf_param) {
    OutBuffer *outbuffer = (OutBuffer*) outbuf_param;
    // TODO realloc
    memcpy(outbuffer->data + outbuffer->size, data, size);
    outbuffer->size += size;
    return size;
}

static void WriteStringToOutBuffer(const char *str, OutBuffer *outbuffer) {
    WriteToOutBuffer(const_cast<char *>(str), strlen(str), outbuffer);
}

void SixelCanvas::Send(int x, int dy, const Framebuffer &fb_orig,
                       SeqType seq_type, Duration end_of_frame) {
    if (dy < 0) {
        MoveCursorDY(-((-dy + options_.cell_y_px - 1) / options_.cell_y_px));
    }
    MoveCursorDX(x / options_.cell_x_px);

    // Create copy to be used in threads.
    const Framebuffer *const fb = new Framebuffer(fb_orig);
    // TODO: this should be realloced as needed.
    char *const buffer = new char[1024 + fb->width() * fb->height() * 5];
    char *const offset = AppendPrefixToBuffer(buffer);

    std::function<OutBuffer()> encode_fun = [fb, buffer, offset]() {
        std::unique_ptr<const Framebuffer> auto_delete(fb);

        OutBuffer out(buffer, offset - buffer);
        WriteStringToOutBuffer(PLACE_CURSOR_AFTER_SIXEL_IMAGE "\033Pq", &out);
        sixel_output_t *sixel_out = nullptr;
        sixel_output_new(&sixel_out, WriteToOutBuffer, &out, nullptr);

        sixel_dither_t *sixel_dither = nullptr;
        sixel_dither_new(&sixel_dither, 256, nullptr);
        sixel_dither_initialize(sixel_dither, (unsigned char *)fb->begin(),
                                fb->width(), fb->height(),
                                SIXEL_PIXELFORMAT_RGBA8888, SIXEL_LARGE_LUM,
                                SIXEL_REP_AVERAGE_COLORS, SIXEL_QUALITY_AUTO);

        sixel_encode((unsigned char *)fb->begin(), fb->width(), fb->height(), 0,
                     sixel_dither, sixel_out);

        sixel_dither_destroy(sixel_dither);
        sixel_output_destroy(sixel_out);

        WriteStringToOutBuffer("\033\\\n", &out);
        return out;
    };
    write_sequencer_->WriteBuffer(executor_->ExecAsync(encode_fun), seq_type,
                                  end_of_frame);
}

}  // namespace timg
