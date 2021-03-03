// -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
// (c) 2016-2021 Henner Zeller <h.zeller@acm.org>
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

#include <math.h>
#include <stdint.h>
#include <string.h>

#include <functional>

namespace timg {
struct rgba_t {
    uint8_t r, g, b;  // Color components, gamma corrected (non-linear)
    uint8_t a;        // Alpha channel. Linear.

    inline bool operator==(const rgba_t &that) const {
        // Using memcmp() slower, so force uint-compare with type-punning.
        return *((uint32_t*)this) == *((uint32_t*)&that);
    }
    inline bool operator!=(const rgba_t &o) const { return !(*this == o); }

    // Parse a color given as string. Supported are numeric formats are
    // "#rrggbb" and "rgb(r, g, b)", and also common textual X11/HTML names
    // such as 'red' or 'MediumAquaMarine'.
    // Returned alpha channel is solid 0xff unless color could not be
    // decoded, in which case it is all-transparent 0x00
    static rgba_t ParseColor(const char *color);
};
static_assert(sizeof(rgba_t) == 4, "Unexpected size for rgba_t struct");

// Very simple framebuffer, storing widht*height pixels in RGBA format.
class Framebuffer {
public:
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

    // Blend all transparent pixels with a background color and an optional
    // alternative pattern color to make them a solid (alpha=0xff) color.
    // The Background color is queried using the provided callback and only
    // requested when needed, i.e. if there any transparent pixels to be
    // blended.
    //
    // If the alpha value of "pattern" is set (alpha=0xff), then every other
    // pixel will be the pattern color. That creates a 'checkerboard-pattern'
    // sometimes used to display transparent pictures.
    //
    // This Alpha compositing merges in the linearized colors domain.
    using bgcolor_query = std::function<rgba_t()>;
    void AlphaComposeBackground(bgcolor_query get_bg, rgba_t pattern);

    // The raw internal buffer containing width()*height() pixels organized
    // from top left to bottom right.
    rgba_t *data() { return pixels_; }
    const rgba_t *data() const { return pixels_; }

    /* the following two methods are useful with line-oriented sws_scale()
     * to allow it to directly write into our frame-buffer
     */

    // Return an array containing the amount of bytes for each line.
    // This is returned as an array.
    const int* stride() const { return strides_; }

    // Return an array containing pointers to the data for each line.
    uint8_t** row_data();

private:
    const int width_;
    const int height_;
    rgba_t *const pixels_;
    int strides_[2];
    uint8_t **row_data_ = nullptr;  // Only allocated if requested.
};

// Unpacked rgba_t into linear color space, useful to do any blending ops on.
class LinearColor {
public:
    LinearColor() : r(0), g(0), b(0), a(0) {}
    // We approximate x^2.2 with x^2
    LinearColor(rgba_t c) : r(c.r*c.r), g(c.g*c.g), b(c.b*c.b), a(c.a) {}

    inline rgba_t repack() const { return { gamma(r), gamma(g), gamma(b), a }; }

    // If this color is transparent, blend in the background according to alpha
    LinearColor &AlphaBlend(const LinearColor &background) {
        r = (r * a + background.r * (0xff - a)) / 0xff;
        g = (g * a + background.g * (0xff - a)) / 0xff;
        b = (b * a + background.b * (0xff - a)) / 0xff;
        a = 0xff;  // We're a fully solid color now.
        return *this;
    }

    float r;
    float g;
    float b;
    uint8_t a;

private:
    static uint8_t gamma(float v) {
        const float vg = sqrtf(v);
        return (vg > 255) ? 255 : vg;
    }
};

}  // namespace timg

#endif  // TIMG_FRAMEBUFFER_H
