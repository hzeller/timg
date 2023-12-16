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
#include <initializer_list>

namespace timg {
struct rgba_t {
    uint8_t r, g, b;  // Color components, gamma corrected (non-linear)
    uint8_t a;        // Alpha channel. Linear. [transparent..opaque]=0..255

    inline bool operator==(const rgba_t &that) const {
        // Using memcmp() slower, so force uint-compare with type-punning.
        return *((uint32_t *)this) == *((uint32_t *)&that);
    }
    inline bool operator!=(const rgba_t &o) const { return !(*this == o); }

    // Rough mapping to the 256 color modes, a 6x6x6 cube.
    inline uint8_t As256TermColor() const {
        if (r == g && g == b) {
            // gray scale, using the 232-255 range.
            return 232 + (r * 23 / 255);
        }

        auto v2cube = [](uint8_t v) {  // middle of cut-off points for cube.
            return v < 0x5f / 2            ? 0
                   : v < (0x5f + 0x87) / 2 ? 1
                   : v < (0x87 + 0xaf) / 2 ? 2
                   : v < (0xaf + 0xd7) / 2 ? 3
                   : v < (0xd7 + 0xff) / 2 ? 4
                                           : 5;
        };
        return 16 + 36 * v2cube(r) + 6 * v2cube(g) + v2cube(b);
    }

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
    typedef rgba_t *iterator;
    typedef const rgba_t *const_iterator;
    class rgb_iterator;

    Framebuffer() = delete;
    Framebuffer(int width, int height);
    explicit Framebuffer(const Framebuffer &other);

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
    // "start_row" is to start the transparency filling further down; it is
    // only really needed as an optimization in the sixels canvas.
    //
    // This Alpha compositing merges in the linearized colors domain.
    using bgcolor_query = std::function<rgba_t()>;
    void AlphaComposeBackground(const bgcolor_query &get_bg, rgba_t pattern,
                                int pattern_width, int pattern_height,
                                int start_row = 0);

    // The raw internal buffer containing width()*height() pixels organized
    // from top left to bottom right.
    const_iterator begin() const { return pixels_; }
    iterator begin() { return pixels_; }
    const_iterator end() const { return end_; }
    iterator end() { return end_; }

    /* the following two methods are useful with line-oriented sws_scale()
     * to allow it to directly write into our frame-buffer
     */

    // Return an array containing the amount of bytes for each line.
    // This is returned as an array.
    const int *stride() const { return strides_; }

    // Return an array containing pointers to the data for each line.
    uint8_t **row_data();

private:
    Framebuffer(int width, int height, const rgba_t *from_data);

    const int width_;
    const int height_;
    rgba_t *const pixels_;
    rgba_t *const end_;
    int strides_[2];
    uint8_t **row_data_ = nullptr;  // Only allocated if requested.
};

// Unpacked rgba_t into linear color space, useful to do any blending ops on.
class LinearColor {
public:
    LinearColor() : r(0), g(0), b(0), a(0) {}
    // We approximate x^2.2 with x^2
    /* implicit */ LinearColor(rgba_t c)  // NOLINT
        : r(c.r * c.r), g(c.g * c.g), b(c.b * c.b), a(c.a) {}

    inline float dist(const LinearColor &other) const {
        // quadratic distance. Not bothering sqrt()ing them.
        return sq(other.r - r) + sq(other.g - g) + sq(other.b - b);
    }

    inline rgba_t repack() const {
        return {gamma(r), gamma(g), gamma(b), (uint8_t)a};
    }

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
    float a;

private:
    static uint8_t gamma(float v) {
        const float vg = sqrtf(v);
        return (vg > 255) ? 255 : vg;
    }
    static float sq(float x) { return x * x; }
};

// Average "values" into "res" and return sum of distance of all values to avg
inline float avd(LinearColor *res, std::initializer_list<LinearColor> values) {
    for (const LinearColor &c : values) {
        res->r += c.r;
        res->g += c.g;
        res->b += c.b;
        res->a += c.a;
    }
    const size_t n = values.size();
    res->r /= n;
    res->g /= n;
    res->b /= n;
    res->a /= n;
    float sum = 0;
    for (const LinearColor &c : values) {
        sum += res->dist(c);
    }
    return sum;
}

inline LinearColor linear_average(std::initializer_list<LinearColor> values) {
    LinearColor result;
    avd(&result, values);
    return result;
}

}  // namespace timg

#endif  // TIMG_FRAMEBUFFER_H
