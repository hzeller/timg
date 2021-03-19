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

#include "timg-png.h"

#include <assert.h>
#include <png.h>
#include <string.h>

#include "framebuffer.h"

namespace timg {
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
