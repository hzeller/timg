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

#include <sixel.h>

#include <algorithm>
#include <cassert>

#include "thread-pool.h"

#define CSI "\033["

namespace timg {

SixelCanvas::SixelCanvas(BufferedWriteSequencer *ws, ThreadPool *thread_pool,
                         bool required_cursor_placement_workaround,
                         const DisplayOptions &opts)
    : TerminalCanvas(ws), options_(opts), executor_(thread_pool) {
    // Terminals might have different understanding where the curosr is placed
    // after an image is sent.
    // Apparently the original dec terminal placed it afterwards, but some
    // terminals now have also place the cursor in the _next_ line.
    //
    // There seem to be two DECSET settings 7730 and 8452 which influence that,
    // ... and not all terminals implement all of these. Also xterm apparently
    // implements CIS 80 backwards ?
    //
    // So the below is an attempt to get it working on all of these.
    // Assuming that xterm is slightly less common these days, but a bunch of
    // modern terminals are built based on libvte (e.g. gnome-terminal,
    // xfce4-terminal,..), let's go with that in the common case and try
    // to detect the exceptions in the graphics query.
    //
    // To test: test with one animation and some images with --grid=3x2 --title
    //
    // Also see:
    //   * https://vt100.net/dec/ek-vt382-rm-001.pdf#page=112
    //   * https://vt100.net/dec/ek-vt38t-ug-001.pdf#page=132
    // Plese send PR or issue if you know a less ugly way to deal with this.
    if (!required_cursor_placement_workaround) {
        //** The default way of doing things; works with most terminals.
        // works: konsole, mlterm, libvte-based, alacritty-sixel
        // breaks: xterm, wezterm
        cursor_move_before_ = CSI "80h" CSI "?7730h" CSI "?8452l";
        cursor_move_after_  = "\r";
    }
    else {
        //** The workaround enabled for xterm and wezterm.
        // works: xterm, mlterm, wezterm, alacritty-sixel
        // break: konsole, libvte-based
        cursor_move_before_ = CSI "80l" CSI "?7730l" CSI "?8452h";
        cursor_move_after_  = "\n";
    }
}

// Char needs to be non-const to be compatible sixel-callback.
static int WriteToOutBuffer(char *data, int size, void *outbuf_param) {
    OutBuffer *outbuffer = (OutBuffer *)outbuf_param;
    // TODO realloc
    memcpy(outbuffer->data + outbuffer->size, data, size);
    outbuffer->size += size;
    return size;
}

static inline int round_to_sixel(int pixels) {
    pixels += 5;
    return pixels - pixels % 6;
}

static void WriteStringToOutBuffer(const char *str, OutBuffer *outbuffer) {
    WriteToOutBuffer(const_cast<char *>(str), strlen(str), outbuffer);
}

void SixelCanvas::Send(int x, int dy, const Framebuffer &fb_orig,
                       SeqType seq_type, Duration end_of_frame) {
    if (dy < 0) {
        MoveCursorDY(cell_height_for_pixels(dy));
    }
    MoveCursorDX(x / options_.cell_x_px);

    // Create copy to be used in threads.

    // Round height to next possible sixel cut-off treat the remaining strip
    // at the bottom as transparent.
    Framebuffer *const fb =
        new Framebuffer(fb_orig.width(), round_to_sixel(fb_orig.height()));
    // First, make it transparent with whatever choosen (ideally, we only
    // do the last couple of rows)
    fb->AlphaComposeBackground(
        options_.bgcolor_getter, options_.bg_pattern_color,
        options_.pattern_size * options_.cell_x_px,
        options_.pattern_size * options_.cell_y_px / 2, fb_orig.height());
    // .. overwrite with whatever is in the orig.
    std::copy(fb_orig.begin(), fb_orig.end(), fb->begin());

    // TODO: this should be realloced as needed.
    char *const buffer = new char[1024 + fb->width() * fb->height() * 5];
    char *const offset = AppendPrefixToBuffer(buffer);
    // avoid capture whole 'this', so copy values locally
    const char *const cursor_handling_start = cursor_move_before_;
    const char *const cursor_handling_end   = cursor_move_after_;
    std::function<OutBuffer()> encode_fun =
        [fb, buffer, offset, cursor_handling_start, cursor_handling_end]() {
            std::unique_ptr<const Framebuffer> auto_delete(fb);

            OutBuffer out(buffer, offset - buffer);
            WriteStringToOutBuffer(cursor_handling_start, &out);
            WriteStringToOutBuffer("\033Pq", &out);  // Start sixel data
            sixel_output_t *sixel_out = nullptr;
            sixel_output_new(&sixel_out, WriteToOutBuffer, &out, nullptr);

            sixel_dither_t *sixel_dither = nullptr;
            sixel_dither_new(&sixel_dither, 256, nullptr);
            sixel_dither_initialize(
                sixel_dither, (unsigned char *)fb->begin(), fb->width(),
                fb->height(), SIXEL_PIXELFORMAT_RGBA8888, SIXEL_LARGE_LUM,
                SIXEL_REP_AVERAGE_COLORS, SIXEL_QUALITY_AUTO);

            sixel_encode((unsigned char *)fb->begin(), fb->width(),
                         fb->height(), 0, sixel_dither, sixel_out);

            sixel_dither_destroy(sixel_dither);
            sixel_output_destroy(sixel_out);

            WriteStringToOutBuffer("\033\\", &out);  // end sixel data
            WriteStringToOutBuffer(cursor_handling_end, &out);
            return out;
        };
    write_sequencer_->WriteBuffer(executor_->ExecAsync(encode_fun), seq_type,
                                  end_of_frame);
}

int SixelCanvas::cell_height_for_pixels(int pixels) const {
    assert(pixels <= 0);  // Currently only use-case
    pixels = -pixels;
    // Unlike the other exact pixel canvases where we have to round to the
    // next even cell_y_px, here we first need to round to the next even 6
    // first.
    return -((round_to_sixel(pixels) + options_.cell_y_px - 1) /
             options_.cell_y_px);
}

}  // namespace timg
