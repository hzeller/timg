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
// Create ID unique enough for our purposes.
static uint32_t CreateId() {
    static const uint32_t kStart = (uint32_t)time(nullptr) << 7;
    static uint32_t counter      = 0;
    counter++;
    return kStart + counter;
}

// Placehholder unicode characters to be used in tmux image output.
// https://sw.kovidgoyal.net/kitty/graphics-protocol/#unicode-placeholders
static char *append_xy_msb(char *buffer, int x, int y, uint8_t msb);
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

static void EnableTmuxPassthrough() {
    const bool tmux_is_local     = getenv("TMUX");  // simplistic way
    bool set_passthrough_success = false;
    // We need to tell tmux to allow pass-through of the image data
    // that we send behind its back to kitty...
    const int ret = system("tmux set -p allow-passthrough on > /dev/null 2>&1");
    switch (ret) {
    case 0: set_passthrough_success = true; break;
    case 1:
        // Exit code 1: we were able to call tmux set, but option was unknown
        fprintf(stderr, "Can't set passthrough; need tmux >= 3.3.\n");
        break;
    default:
        // "command not found" is typically in upper region of exit codes
        if (tmux_is_local) {
            fprintf(stderr, "Can't set passthrough, tmux set exit-code=%d\n",
                    ret);
        }
        else {
            // Probably remote, and no tmux installed there.
        }
    }
    if (!set_passthrough_success) {
        // Could not successfully set the passthrough mode, but we
        // actually don't know if it might work anyway.
        // TODO: Maybe send a kitty graphics query to determine if it would
        // work and only if not, provide a error message.
    }
}

KittyGraphicsCanvas::KittyGraphicsCanvas(BufferedWriteSequencer *ws,
                                         ThreadPool *thread_pool,
                                         bool tmux_passthrough_needed,
                                         const DisplayOptions &opts)
    : TerminalCanvas(ws),
      options_(opts),
      tmux_passthrough_needed_(tmux_passthrough_needed),
      executor_(thread_pool) {
    if (tmux_passthrough_needed) {
        EnableTmuxPassthrough();
    }
}

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
    bool wrap_tmux   = tmux_passthrough_needed_;
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
    // https://sw.kovidgoyal.net/kitty/_downloads/f0a0de9ec8d9ff4456206db8e0814937/rowcolumn-diacritics.txt
#if 0
 cat rowcolumn-diacritics.txt | awk -F";" '\
  BEGIN { i=0; printf("static const char *const kRowColEncode[] = {"); } \
  /^[0-9A-F]/ { if (i++ % 6 == 0) printf("\n"); printf("\"\\u%s\", ", $1); }\
  END { printf("\n}; /* %d */\n", i); }'
#endif
    static const char *const kRowColEncode[] = {
        "\u0305",  "\u030D",  "\u030E",  "\u0310",  "\u0312",  "\u033D",
        "\u033E",  "\u033F",  "\u0346",  "\u034A",  "\u034B",  "\u034C",
        "\u0350",  "\u0351",  "\u0352",  "\u0357",  "\u035B",  "\u0363",
        "\u0364",  "\u0365",  "\u0366",  "\u0367",  "\u0368",  "\u0369",
        "\u036A",  "\u036B",  "\u036C",  "\u036D",  "\u036E",  "\u036F",
        "\u0483",  "\u0484",  "\u0485",  "\u0486",  "\u0487",  "\u0592",
        "\u0593",  "\u0594",  "\u0595",  "\u0597",  "\u0598",  "\u0599",
        "\u059C",  "\u059D",  "\u059E",  "\u059F",  "\u05A0",  "\u05A1",
        "\u05A8",  "\u05A9",  "\u05AB",  "\u05AC",  "\u05AF",  "\u05C4",
        "\u0610",  "\u0611",  "\u0612",  "\u0613",  "\u0614",  "\u0615",
        "\u0616",  "\u0617",  "\u0657",  "\u0658",  "\u0659",  "\u065A",
        "\u065B",  "\u065D",  "\u065E",  "\u06D6",  "\u06D7",  "\u06D8",
        "\u06D9",  "\u06DA",  "\u06DB",  "\u06DC",  "\u06DF",  "\u06E0",
        "\u06E1",  "\u06E2",  "\u06E4",  "\u06E7",  "\u06E8",  "\u06EB",
        "\u06EC",  "\u0730",  "\u0732",  "\u0733",  "\u0735",  "\u0736",
        "\u073A",  "\u073D",  "\u073F",  "\u0740",  "\u0741",  "\u0743",
        "\u0745",  "\u0747",  "\u0749",  "\u074A",  "\u07EB",  "\u07EC",
        "\u07ED",  "\u07EE",  "\u07EF",  "\u07F0",  "\u07F1",  "\u07F3",
        "\u0816",  "\u0817",  "\u0818",  "\u0819",  "\u081B",  "\u081C",
        "\u081D",  "\u081E",  "\u081F",  "\u0820",  "\u0821",  "\u0822",
        "\u0823",  "\u0825",  "\u0826",  "\u0827",  "\u0829",  "\u082A",
        "\u082B",  "\u082C",  "\u082D",  "\u0951",  "\u0953",  "\u0954",
        "\u0F82",  "\u0F83",  "\u0F86",  "\u0F87",  "\u135D",  "\u135E",
        "\u135F",  "\u17DD",  "\u193A",  "\u1A17",  "\u1A75",  "\u1A76",
        "\u1A77",  "\u1A78",  "\u1A79",  "\u1A7A",  "\u1A7B",  "\u1A7C",
        "\u1B6B",  "\u1B6D",  "\u1B6E",  "\u1B6F",  "\u1B70",  "\u1B71",
        "\u1B72",  "\u1B73",  "\u1CD0",  "\u1CD1",  "\u1CD2",  "\u1CDA",
        "\u1CDB",  "\u1CE0",  "\u1DC0",  "\u1DC1",  "\u1DC3",  "\u1DC4",
        "\u1DC5",  "\u1DC6",  "\u1DC7",  "\u1DC8",  "\u1DC9",  "\u1DCB",
        "\u1DCC",  "\u1DD1",  "\u1DD2",  "\u1DD3",  "\u1DD4",  "\u1DD5",
        "\u1DD6",  "\u1DD7",  "\u1DD8",  "\u1DD9",  "\u1DDA",  "\u1DDB",
        "\u1DDC",  "\u1DDD",  "\u1DDE",  "\u1DDF",  "\u1DE0",  "\u1DE1",
        "\u1DE2",  "\u1DE3",  "\u1DE4",  "\u1DE5",  "\u1DE6",  "\u1DFE",
        "\u20D0",  "\u20D1",  "\u20D4",  "\u20D5",  "\u20D6",  "\u20D7",
        "\u20DB",  "\u20DC",  "\u20E1",  "\u20E7",  "\u20E9",  "\u20F0",
        "\u2CEF",  "\u2CF0",  "\u2CF1",  "\u2DE0",  "\u2DE1",  "\u2DE2",
        "\u2DE3",  "\u2DE4",  "\u2DE5",  "\u2DE6",  "\u2DE7",  "\u2DE8",
        "\u2DE9",  "\u2DEA",  "\u2DEB",  "\u2DEC",  "\u2DED",  "\u2DEE",
        "\u2DEF",  "\u2DF0",  "\u2DF1",  "\u2DF2",  "\u2DF3",  "\u2DF4",
        "\u2DF5",  "\u2DF6",  "\u2DF7",  "\u2DF8",  "\u2DF9",  "\u2DFA",
        "\u2DFB",  "\u2DFC",  "\u2DFD",  "\u2DFE",  "\u2DFF",  "\uA66F",
        "\uA67C",  "\uA67D",  "\uA6F0",  "\uA6F1",  "\uA8E0",  "\uA8E1",
        "\uA8E2",  "\uA8E3",  "\uA8E4",  "\uA8E5",  "\uA8E6",  "\uA8E7",
        "\uA8E8",  "\uA8E9",  "\uA8EA",  "\uA8EB",  "\uA8EC",  "\uA8ED",
        "\uA8EE",  "\uA8EF",  "\uA8F0",  "\uA8F1",  "\uAAB0",  "\uAAB2",
        "\uAAB3",  "\uAAB7",  "\uAAB8",  "\uAABE",  "\uAABF",  "\uAAC1",
        "\uFE20",  "\uFE21",  "\uFE22",  "\uFE23",  "\uFE24",  "\uFE25",
        "\uFE26",  "\u10A0F", "\u10A38", "\u1D185", "\u1D186", "\u1D187",
        "\u1D188", "\u1D189", "\u1D1AA", "\u1D1AB", "\u1D1AC", "\u1D1AD",
        "\u1D242", "\u1D243", "\u1D244",
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
