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
#include <cassert>
#include <cstdint>

#include "timg-base64.h"
#include "timg-png.h"

#define SCREEN_CURSOR_UP_FORMAT    "\033[%dA"  // Move cursor up given lines.
#define SCREEN_CURSOR_RIGHT_FORMAT "\033[%dC"  // Move cursor right given cols

#define TMUX_START_PASSTHROUGH "\ePtmux;"
#define TMUX_END_PASSTHROUGH   "\e\\"

static constexpr int kBase64EncodedChunkSize = 4096;  // Max allowed: 4096.
static constexpr int kByteChunk              = kBase64EncodedChunkSize / 4 * 3;

namespace timg {
static char *append_xy_msb(char *buffer, int x, int y, uint8_t msb);

// Create ID unique enough for our purposes.
static uint32_t CreateId() {
    static const uint32_t kStart = (uint32_t)time(nullptr) << 7;
    static uint32_t counter      = 0;
    counter++;
    return kStart + counter;
}

// Placehholder unicode characters to be used in tmux image output.
// https://sw.kovidgoyal.net/kitty/graphics-protocol/#unicode-placeholders
static char *AppendUnicodePicureTiles(char *pos, uint32_t id, int indent,
                                      int rows, int cols) {
    *pos++ = '\r';
    for (int r = 0; r < rows; ++r) {
        if (indent > 0) {
            pos += sprintf(pos, SCREEN_CURSOR_RIGHT_FORMAT, indent);
        }
        pos += sprintf(pos, "\e[38:2:%u:%u:%um",  //
                       (id >> 16) & 0xff, (id >> 8) & 0xff, id & 0xff);
        for (int c = 0; c < cols; ++c) {
            pos += sprintf(pos, "\xf4\x8e\xbb\xae");  // \u10ffff
            pos = append_xy_msb(pos, r, c, (id >> 24) & 0xff);
        }
        pos += sprintf(pos, "\e[39m\n\r");
    }
    return pos;
}

static char *AppendEscaped(char *pos, char c, bool wrap_tmux) {
    *pos++ = '\e';
    if (wrap_tmux) *pos++ = '\e';  // in tmux: escape the escape
    *pos++ = c;
    return pos;
};

KittyGraphicsCanvas::KittyGraphicsCanvas(BufferedWriteSequencer *ws,
                                         ThreadPool *thread_pool,
                                         bool tmux_workaround_needed,
                                         const DisplayOptions &opts)
    : TerminalCanvas(ws),
      options_(opts),
      tmux_workaround_needed_(tmux_workaround_needed),
      executor_(thread_pool) {}

void KittyGraphicsCanvas::Send(int x, int dy, const Framebuffer &fb_orig,
                               SeqType seq_type, Duration end_of_frame) {
    if (dy < 0) {
        MoveCursorDY(cell_height_for_pixels(dy));
    }
    MoveCursorDX(x / options_.cell_x_px);

    // Create independent copy of frame buffer for use in thread.
    Framebuffer *const fb = new Framebuffer(fb_orig);
    char *const buffer    = RequestBuffer(fb->width(), fb->height());
    char *const offset    = AppendPrefixToBuffer(buffer);

    const auto &opts = options_;

    // Creating a new ID. Some terminals store the images in a GPU texture
    // buffer (looking at you, wezterm) and index by the ID, so we need to be
    // economical with IDs.
    uint32_t id                  = 0;
    static uint32_t animation_id = 0;
    static uint8_t flip_buffer   = 0;
    switch (seq_type) {
    case SeqType::FrameImmediate:
        // Ideally we use the content hash here. However, that means that
        // if we have the same image in the same timg session, this would
        // replace images. This could be addressed by remembering the ID
        // and do a placement with the ID, however this is only really
        // supported by Kitty directly while other compatible terminals can't
        // deal with it yet reliably.
        // So compromise: create unique ID for regular images.
        id = CreateId();
        break;
    case SeqType::StartOfAnimation:
        // Sending a bunch of images with different IDs overwhelms some
        // terminals. So, for animations, just use two IDs back/forth.
        id           = CreateId();
        animation_id = id;
        break;
    case SeqType::AnimationFrame:
        ++flip_buffer;
        id = animation_id + (flip_buffer % 2);
        break;
    case SeqType::ControlWrite: {
        // should not happen.
    }
    }

    const int cols   = fb->width() / opts.cell_x_px;
    const int rows   = -cell_height_for_pixels(-fb->height());
    const int indent = x / opts.cell_x_px;
    bool wrap_tmux   = tmux_workaround_needed_;
    std::function<OutBuffer()> encode_fun = [opts, fb, id, buffer, offset, rows,
                                             cols, indent, wrap_tmux]() {
        std::unique_ptr<const Framebuffer> auto_delete(fb);
        const size_t png_buf_size = png::UpperBound(fb->width(), fb->height());
        std::unique_ptr<char[]> png_buf(new char[png_buf_size]);

        int png_size = png::Encode(*fb, opts.compress_pixel_level,
                                   opts.local_alpha_handling
                                       ? png::ColorEncoding::kRGB_24
                                       : png::ColorEncoding::kRGBA_32,
                                   png_buf.get(), png_buf_size);

        char *pos = offset;  // Appending to the partially populated buffer.

        // Need to send an image with an id (i=...) as some terminals interpet
        // no id with id=0 and just keep replacing one picture.
        // Also need q=2 to prevent getting terminal feedback back we don't
        // read.
        if (wrap_tmux) pos += sprintf(pos, TMUX_START_PASSTHROUGH);
        pos = AppendEscaped(pos, '_', wrap_tmux);
        pos +=
            sprintf(pos, "Ga=T,i=%u,q=2,f=100,m=%d", id, png_size > kByteChunk);
        if (wrap_tmux) {
            pos += sprintf(pos, ",U=1,c=%d,r=%d", cols, rows);
        }
        *pos++ = ';';  // End of Kitty command

        // Write out binary data base64-encoded in chunks of limited size.
        const char *png_data = png_buf.get();
        while (png_size) {
            int chunk_bytes = std::min(png_size, kByteChunk);
            pos             = timg::EncodeBase64(png_data, chunk_bytes, pos);
            png_data += chunk_bytes;
            png_size -= chunk_bytes;
            if (png_size) {  // More to come. Finish chunk and start next.
                pos = AppendEscaped(pos, '\\', wrap_tmux);  // finish
                if (wrap_tmux) {
                    pos += sprintf(pos,
                                   TMUX_END_PASSTHROUGH TMUX_START_PASSTHROUGH);
                }
                pos = AppendEscaped(pos, '_', wrap_tmux);
                pos += sprintf(pos, "Gq=2,m=%d;", png_size > kByteChunk);
            }
        }
        pos = AppendEscaped(pos, '\\', wrap_tmux);

        if (wrap_tmux) {
            pos += sprintf(pos, TMUX_END_PASSTHROUGH);
            pos = AppendUnicodePicureTiles(pos, id, indent, rows, cols);
        }
        else {
            *pos++ = '\n';  // Need one final cursor movement.
        }
        return OutBuffer(buffer, pos - buffer);
    };

    write_sequencer_->WriteBuffer(executor_->ExecAsync(encode_fun), seq_type,
                                  end_of_frame);
}

char *KittyGraphicsCanvas::RequestBuffer(int width, int height) {
    const size_t png_compressed_size = png::UpperBound(width, height);
    const int encoded_base64_size    = png_compressed_size * 4 / 3;
    const int cols                   = width / options_.cell_x_px;
    const int rows                   = -cell_height_for_pixels(-height);

    const size_t content_size =
        strlen(SCREEN_CURSOR_UP_FORMAT) + strlen(SCREEN_CURSOR_RIGHT_FORMAT) +
        encoded_base64_size  //
        + strlen("\e_Ga=T,f=XX,s=9999,v=9999,m=1;\e\\") +
        (encoded_base64_size / kBase64EncodedChunkSize) *
            strlen("\e_Gm=0;\e\\") +
        4 + 1 +            // digit space for cursor up/right; \n
        rows * cols * 16;  // Some space for unicode tiles with diacritics.
    return new char[content_size];
}

int KittyGraphicsCanvas::cell_height_for_pixels(int pixels) const {
    assert(pixels <= 0);  // Currently only use-case
    return -((-pixels + options_.cell_y_px - 1) / options_.cell_y_px);
}

// Unicode diacritics to convey extra bytes.
//
// https://sw.kovidgoyal.net/kitty/graphics-protocol/#unicode-placeholders
// In general, this is a hack around tmux refusing to work with graphics.
//
// The Kitty terminal trick is to send real unicode characters that tmux
// can deal with, including scrolling etc.
// But these contain extra information which then refer to previously
// pass-throughed image information.
// A \u10ffff unicode is decorated with diacritics to convey x/y position
// and most significant byte.
static char *append_value_diacritic(char *buffer, int value) {
    // Diacritics used are provided in
    // https://sw.kovidgoyal.net/kitty/_downloads/1792bad15b12979994cd6ecc54c967a6/rowcolumn-diacritics.txt
#if 0
 cat rowcolumn-diacritics.txt | awk -F";" '\
  BEGIN { i=0; printf("static const char *const kRowColEncode[] = {"); } \
  /^[0-9A-F]/ { if (i++ % 5 == 0) printf("\n"); printf("u8\"\\u%s\", ", $1); }\
  END { printf("\n}; /* %d */\n", i); }'
#endif
    static const char *const kRowColEncode[] = {
        u8"\u0305",  u8"\u030D",  u8"\u030E",  u8"\u0310",  u8"\u0312",
        u8"\u033D",  u8"\u033E",  u8"\u033F",  u8"\u0346",  u8"\u034A",
        u8"\u034B",  u8"\u034C",  u8"\u0350",  u8"\u0351",  u8"\u0352",
        u8"\u0357",  u8"\u035B",  u8"\u0363",  u8"\u0364",  u8"\u0365",
        u8"\u0366",  u8"\u0367",  u8"\u0368",  u8"\u0369",  u8"\u036A",
        u8"\u036B",  u8"\u036C",  u8"\u036D",  u8"\u036E",  u8"\u036F",
        u8"\u0483",  u8"\u0484",  u8"\u0485",  u8"\u0486",  u8"\u0487",
        u8"\u0592",  u8"\u0593",  u8"\u0594",  u8"\u0595",  u8"\u0597",
        u8"\u0598",  u8"\u0599",  u8"\u059C",  u8"\u059D",  u8"\u059E",
        u8"\u059F",  u8"\u05A0",  u8"\u05A1",  u8"\u05A8",  u8"\u05A9",
        u8"\u05AB",  u8"\u05AC",  u8"\u05AF",  u8"\u05C4",  u8"\u0610",
        u8"\u0611",  u8"\u0612",  u8"\u0613",  u8"\u0614",  u8"\u0615",
        u8"\u0616",  u8"\u0617",  u8"\u0657",  u8"\u0658",  u8"\u0659",
        u8"\u065A",  u8"\u065B",  u8"\u065D",  u8"\u065E",  u8"\u06D6",
        u8"\u06D7",  u8"\u06D8",  u8"\u06D9",  u8"\u06DA",  u8"\u06DB",
        u8"\u06DC",  u8"\u06DF",  u8"\u06E0",  u8"\u06E1",  u8"\u06E2",
        u8"\u06E4",  u8"\u06E7",  u8"\u06E8",  u8"\u06EB",  u8"\u06EC",
        u8"\u0730",  u8"\u0732",  u8"\u0733",  u8"\u0735",  u8"\u0736",
        u8"\u073A",  u8"\u073D",  u8"\u073F",  u8"\u0740",  u8"\u0741",
        u8"\u0743",  u8"\u0745",  u8"\u0747",  u8"\u0749",  u8"\u074A",
        u8"\u07EB",  u8"\u07EC",  u8"\u07ED",  u8"\u07EE",  u8"\u07EF",
        u8"\u07F0",  u8"\u07F1",  u8"\u07F3",  u8"\u0816",  u8"\u0817",
        u8"\u0818",  u8"\u0819",  u8"\u081B",  u8"\u081C",  u8"\u081D",
        u8"\u081E",  u8"\u081F",  u8"\u0820",  u8"\u0821",  u8"\u0822",
        u8"\u0823",  u8"\u0825",  u8"\u0826",  u8"\u0827",  u8"\u0829",
        u8"\u082A",  u8"\u082B",  u8"\u082C",  u8"\u082D",  u8"\u0951",
        u8"\u0953",  u8"\u0954",  u8"\u0F82",  u8"\u0F83",  u8"\u0F86",
        u8"\u0F87",  u8"\u135D",  u8"\u135E",  u8"\u135F",  u8"\u17DD",
        u8"\u193A",  u8"\u1A17",  u8"\u1A75",  u8"\u1A76",  u8"\u1A77",
        u8"\u1A78",  u8"\u1A79",  u8"\u1A7A",  u8"\u1A7B",  u8"\u1A7C",
        u8"\u1B6B",  u8"\u1B6D",  u8"\u1B6E",  u8"\u1B6F",  u8"\u1B70",
        u8"\u1B71",  u8"\u1B72",  u8"\u1B73",  u8"\u1CD0",  u8"\u1CD1",
        u8"\u1CD2",  u8"\u1CDA",  u8"\u1CDB",  u8"\u1CE0",  u8"\u1DC0",
        u8"\u1DC1",  u8"\u1DC3",  u8"\u1DC4",  u8"\u1DC5",  u8"\u1DC6",
        u8"\u1DC7",  u8"\u1DC8",  u8"\u1DC9",  u8"\u1DCB",  u8"\u1DCC",
        u8"\u1DD1",  u8"\u1DD2",  u8"\u1DD3",  u8"\u1DD4",  u8"\u1DD5",
        u8"\u1DD6",  u8"\u1DD7",  u8"\u1DD8",  u8"\u1DD9",  u8"\u1DDA",
        u8"\u1DDB",  u8"\u1DDC",  u8"\u1DDD",  u8"\u1DDE",  u8"\u1DDF",
        u8"\u1DE0",  u8"\u1DE1",  u8"\u1DE2",  u8"\u1DE3",  u8"\u1DE4",
        u8"\u1DE5",  u8"\u1DE6",  u8"\u1DFE",  u8"\u20D0",  u8"\u20D1",
        u8"\u20D4",  u8"\u20D5",  u8"\u20D6",  u8"\u20D7",  u8"\u20DB",
        u8"\u20DC",  u8"\u20E1",  u8"\u20E7",  u8"\u20E9",  u8"\u20F0",
        u8"\u2CEF",  u8"\u2CF0",  u8"\u2CF1",  u8"\u2DE0",  u8"\u2DE1",
        u8"\u2DE2",  u8"\u2DE3",  u8"\u2DE4",  u8"\u2DE5",  u8"\u2DE6",
        u8"\u2DE7",  u8"\u2DE8",  u8"\u2DE9",  u8"\u2DEA",  u8"\u2DEB",
        u8"\u2DEC",  u8"\u2DED",  u8"\u2DEE",  u8"\u2DEF",  u8"\u2DF0",
        u8"\u2DF1",  u8"\u2DF2",  u8"\u2DF3",  u8"\u2DF4",  u8"\u2DF5",
        u8"\u2DF6",  u8"\u2DF7",  u8"\u2DF8",  u8"\u2DF9",  u8"\u2DFA",
        u8"\u2DFB",  u8"\u2DFC",  u8"\u2DFD",  u8"\u2DFE",  u8"\u2DFF",
        u8"\uA66F",  u8"\uA67C",  u8"\uA67D",  u8"\uA6F0",  u8"\uA6F1",
        u8"\uA8E0",  u8"\uA8E1",  u8"\uA8E2",  u8"\uA8E3",  u8"\uA8E4",
        u8"\uA8E5",  u8"\uA8E6",  u8"\uA8E7",  u8"\uA8E8",  u8"\uA8E9",
        u8"\uA8EA",  u8"\uA8EB",  u8"\uA8EC",  u8"\uA8ED",  u8"\uA8EE",
        u8"\uA8EF",  u8"\uA8F0",  u8"\uA8F1",  u8"\uAAB0",  u8"\uAAB2",
        u8"\uAAB3",  u8"\uAAB7",  u8"\uAAB8",  u8"\uAABE",  u8"\uAABF",
        u8"\uAAC1",  u8"\uFE20",  u8"\uFE21",  u8"\uFE22",  u8"\uFE23",
        u8"\uFE24",  u8"\uFE25",  u8"\uFE26",  u8"\u10A0F", u8"\u10A38",
        u8"\u1D185", u8"\u1D186", u8"\u1D187", u8"\u1D188", u8"\u1D189",
        u8"\u1D1AA", u8"\u1D1AB", u8"\u1D1AC", u8"\u1D1AD", u8"\u1D242",
        u8"\u1D243", u8"\u1D244",
    }; /* 297 */

    if (value < 0 || value >= 297) return buffer;
    const char *src = kRowColEncode[value];
    while ((*buffer++ = *src++)) {
        /**/
    }
    return buffer - 1;
}
static char *append_xy_msb(char *buffer, int x, int y, uint8_t msb) {
    buffer = append_value_diacritic(append_value_diacritic(buffer, x), y);
    if (msb) buffer = append_value_diacritic(buffer, msb);
    return buffer;
}
}  // namespace timg
