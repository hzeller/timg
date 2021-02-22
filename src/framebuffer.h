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

#ifndef TIMG_FRAMEBUFFER_H
#define TIMG_FRAMEBUFFER_H

#include <stdint.h>

#ifdef __APPLE__
#    include <libkern/OSByteOrder.h>
#    define htole32(x) OSSwapHostToLittleInt32(x)
#    define le32toh(x) OSSwapLittleToHostInt32(x)
#else
#    include <endian.h>
#endif

namespace timg {
// Very simple framebuffer, storing widht*height pixels in RGBA format
// (always R=first byte, B=second byte; independent of architecture)
class Framebuffer {
public:
    // Note, this is always in little endian
    // 'red' is stored in the first byte, 'green' 2nd, 'blue' 3d, 'alpha' 4th
    // Consider this type opaque, use ParseColor() and to_rgba() to interact.
    typedef uint32_t rgba_t;

    Framebuffer(int width, int height);
    Framebuffer() = delete;
    Framebuffer(const Framebuffer &other) = delete;
    ~Framebuffer();

    // Set a pixel at position X/Y with rgba_t color value. use to_rgba() to
    // convert a value.
    void SetPixel(int x, int y, rgba_t value);

    // Get pixel data at given position.
    rgba_t at(int x, int y) const;

    // Clear to fully transparent black pixels.
    void Clear();

    inline int width() const { return width_; }
    inline int height() const { return height_; }

    // Blend all transparent pixels with the given background "bgcolor" and
    // them a solid color. If the alpha value of "pattern" is set (alpha=0xff),
    // then every other pixel will be the pattern color. That creates a
    // checkerboard-pattern sometimes used to display transparent pictures.
    // This Alpha compositing merges linearized colors, so unlike many other
    // tools such as GraphicsMagick, it will create a pleasent output.
    void AlphaComposeBackground(rgba_t bgcolor, rgba_t pattern);

    // The raw internal buffer containing width()*height() pixels organized
    // from top left to bottom right.
    rgba_t *data() { return pixels_; }
    const rgba_t *data() const { return pixels_; }

    // -- the following two methods are useful with line-oriented sws_scale()
    // -- to allow it to directly write into our frame-buffer

    // Return an array containing the amount of bytes for each line.
    // This is returned as an array.
    const int* stride() const { return strides_; }

    // Return an array containing pointers to the data for each line.
    uint8_t** row_data();

public:
    // Utility function to generate an rgba_t value from components.
    // Given red, green, blue and alpha value: convert to rgba_t type to the
    // correct byte order.
    static rgba_t to_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    // Parse a color given as string. Supported are numeric formats are
    // "#rrggbb" and "rgb(r, g, b)", but also common textual X11/HTML names.
    static rgba_t ParseColor(const char *color);

    // Convert RGBA value to host byte-order, so that r is at
    // lowest (value&0xff)
    static inline uint32_t rgba_to_host(rgba_t color) { return le32toh(color); }

private:
    const int width_;
    const int height_;
    rgba_t *const pixels_;
    int strides_[2];
    uint8_t** row_data_ = nullptr;  // Only allocated if requested.
};
}  // namespace timg

#endif  // TIMG_FRAMEBUFFER_H
