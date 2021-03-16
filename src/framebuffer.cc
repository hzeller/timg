// -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
// (c) 2016 Henner Zeller <h.zeller@acm.org>
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

#include "framebuffer.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <png.h>

namespace timg {

rgba_t rgba_t::ParseColor(const char *color) {
    if (!color) return { 0, 0, 0, 0 };

    // If it is a named color, convert it first to its #rrggbb string.
#include "html-colors.inc"
    for (const auto &c : html_colors) {
        if (strcasecmp(color, c.name) == 0) {
            color = c.translation;
            break;
        }
    }
    uint32_t r, g, b;
    if ((sscanf(color, "#%02x%02x%02x", &r, &g, &b) == 3) ||
        (sscanf(color, "rgb(%d, %d, %d)", &r, &g, &b) == 3) ||
        (sscanf(color, "rgb(0x%x, 0x%x, 0x%x)", &r, &g, &b) == 3)) {
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        return { (uint8_t)r, (uint8_t)g, (uint8_t)b, 0xff };
    }
    if (strcasecmp(color, "none") != 0)  // 'allowed' non-color. Don't complain.
        fprintf(stderr, "Couldn't parse color '%s'\n", color);
    return { 0, 0, 0, 0 };
}

Framebuffer::Framebuffer(int w, int h)
    : width_(w), height_(h), pixels_(new rgba_t [ width_ * height_]),
      end_(pixels_ + width_ * height_) {
    strides_[0] = (int)sizeof(rgba_t) * width_;
    strides_[1] = 0;  // empty sentinel value.
    Clear();
}

Framebuffer::~Framebuffer() {
    delete [] row_data_;
    delete [] pixels_;
}

void Framebuffer::SetPixel(int x, int y, rgba_t value) {
    if (x < 0 || x >= width() || y < 0 || y >= height()) return;
    pixels_[width_ * y + x] = value;
}

rgba_t Framebuffer::at(int x, int y) const {
    assert(x >= 0 && x < width() && y >= 0 && y < height());
    return pixels_[width_ * y + x];
}

void Framebuffer::Clear() {
    memset(pixels_, 0, sizeof(*pixels_) * width_ * height_);
}

uint8_t** Framebuffer::row_data() const {
    if (!row_data_) {
        row_data_ = new uint8_t* [ height_ + 1];
        for (int i = 0; i < height_; ++i)
            row_data_[i] = (uint8_t*)pixels_ + i * width_ * sizeof(rgba_t);
        row_data_[height_] = nullptr;  // empty sentinel value.
    }
    return row_data_;
}

void Framebuffer::AlphaComposeBackground(bgcolor_query get_bg,
                                         rgba_t pattern_col,
                                         int pwidth, int pheight) {
    if (!get_bg) return;  // -b none

    iterator pos = begin();
    for (/**/; pos < end(); ++pos) {
        if (pos->a < 0xff) break;  // First pixel that is transparent.
    }
    if (pos == end()) return;  // Nothing transparent all the way to the end.

    // Need to do alpha blending, so only now we have to retrieve the bgcolor.
    const rgba_t bgcolor = get_bg();
    if (bgcolor.a == 0x00) return; // nothing to do.

    // Fast path if we don't have a pattern color.
    if (pattern_col.a == 0x00 || pattern_col == bgcolor ||
        pwidth <= 0 || pheight <= 0 ) {
        const LinearColor bg(bgcolor);
        for (/**/; pos < end(); ++pos) {
            if (pos->a == 0xff) continue;
            *pos = LinearColor(*pos).AlphaBlend(bg).repack();
        }
        return;
    }

    // If we have a pattern color, use that as alternating choice.
    const LinearColor bg_choice[2] = { bgcolor, pattern_col };

    // Pos moved to the first pixel that required alpha blending.
    // From this pos, we need to recover the x/y position to be in the
    // right place for the checkerboard pattern.
    const int start_x = (pos - begin()) % width_;
    const int start_y = (pos - begin()) / width_;
    for (int y = start_y; y < height_; ++y) {
        const int y_pattern_pos = y / pheight;
        for (int x = (y == start_y ? start_x : 0); x < width_; ++x, pos++) {
            if (pos->a == 0xff) continue;
            const auto &bg = bg_choice[((x / pwidth) + y_pattern_pos) % 2];
            *pos = LinearColor(*pos).AlphaBlend(bg).repack();
        }
    }
}

// Our own IO-data to write to memory.
struct PNGMemIO {
    char *buffer;
    const char *end;
};
static void Encode_png_write_fn(png_structp png, png_bytep data, size_t len) {
    PNGMemIO *out_state = (PNGMemIO*)png_get_io_ptr(png);
    memcpy(out_state->buffer, data, len);
    out_state->buffer += len;
    assert(out_state->buffer < out_state->end);
}

// Encode image as PNG and store in buffer. Returns bytes written.
size_t EncodePNG(const Framebuffer &fb, char *const buffer, size_t size) {
    static constexpr int compression_level = 1;
    static constexpr int zlib_strategy = 3;  // Z_RLE.
    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                                  nullptr, nullptr, nullptr);
    if (!png_ptr) return 0;
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_write_struct(&png_ptr, nullptr);
        return 0;
    }

    png_set_IHDR(png_ptr, info_ptr, fb.width(), fb.height(), 8,
                 PNG_COLOR_TYPE_RGB_ALPHA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);

    png_set_filter(png_ptr, PNG_FILTER_TYPE_BASE, PNG_FILTER_SUB);
    png_set_compression_level(png_ptr, compression_level);
    png_set_compression_strategy(png_ptr, zlib_strategy);

    PNGMemIO output = { buffer, buffer + size };
    png_set_write_fn(png_ptr, &output, &Encode_png_write_fn, nullptr);

    png_set_rows(png_ptr, info_ptr, (png_bytepp) fb.row_data());
    png_write_png(png_ptr, info_ptr, 0, nullptr);
    png_destroy_write_struct(&png_ptr, &info_ptr);

    return output.buffer - buffer;
}

}  // namespace timg
