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

#include "svg-image-source.h"

#include <cairo.h>
#include <librsvg/rsvg.h>
#include <stdlib.h>

#include <algorithm>

#include "framebuffer.h"

// There is a thread-safety issue with RSVG + Cairo that manifests
// as we LoadAndScale() in a thread-pool.
// Typically crashing at _cairo_ft_scaled_glyph_load_glyph in the
// call stack of rsvg_handle_render_document().
//
// Workaound: add a global mutex around rsvg_handle_render_document(), which
// fixes it.
//
// TODO: figure out what the issue is, file bug upstream and make this
// macro dependent on Cairo and RSVG Version macros if it is known which
// version fixed it.
#define RSVG_THREADSAFE_ISSUE 1

#if RSVG_THREADSAFE_ISSUE
#    include <mutex>
#endif

namespace timg {

std::string SVGImageSource::FormatTitle(
    const std::string &format_string) const {
    return FormatFromParameters(format_string, filename_, (int)orig_width_,
                                (int)orig_height_, "svg");
}

bool SVGImageSource::LoadAndScale(const DisplayOptions &opts, int, int) {
    options_        = opts;
    RsvgHandle *svg = rsvg_handle_new_from_file(filename_.c_str(), nullptr);
    if (!svg) return false;

    RsvgRectangle viewbox;
    gboolean out_has_width, out_has_height, out_has_viewbox;
    RsvgLength svg_width, svg_height;
    rsvg_handle_get_intrinsic_dimensions(svg, &out_has_width, &svg_width,
                                         &out_has_height, &svg_height,
                                         &out_has_viewbox, &viewbox);
    if (out_has_viewbox) {
        orig_width_  = viewbox.width;
        orig_height_ = viewbox.height;
    }
    else if (out_has_width && out_has_height) {
        // We ignore the unit, but this will still result in proper aspect ratio
        orig_width_  = svg_width.length;
        orig_height_ = svg_height.length;
    }

    // Filter out suspicious dimensions
    if (orig_width_ <= 0 || orig_width_ > 1e6 || orig_height_ <= 0 ||
        orig_height_ > 1e6) {
        g_object_unref(svg);
        return false;
    }

    int target_width;
    int target_height;
    CalcScaleToFitDisplay(orig_width_, orig_height_, opts, false, &target_width,
                          &target_height);

    const auto kCairoFormat = CAIRO_FORMAT_ARGB32;
    int stride = cairo_format_stride_for_width(kCairoFormat, target_width);
    image_.reset(new timg::Framebuffer(stride / 4, target_height));

    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        (uint8_t *)image_->begin(), kCairoFormat, target_width, target_height,
        stride);
    cairo_t *cr = cairo_create(surface);
    cairo_scale(cr, 1.0 * target_width / orig_width_,
                1.0 * target_height / orig_height_);
    cairo_save(cr);

    RsvgRectangle viewport = {
        .x      = 0.0,  // TODO: could we have an offset ?
        .y      = 0.0,
        .width  = orig_width_,
        .height = orig_height_,
    };

#if RSVG_THREADSAFE_ISSUE
    static std::mutex render_mutex;
    render_mutex.lock();
#endif

    bool success = rsvg_handle_render_document(svg, cr, &viewport, nullptr);

#if RSVG_THREADSAFE_ISSUE
    render_mutex.unlock();
#endif

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    g_object_unref(svg);

    // Cairo stores A (high-byte), R, G, B (low-byte). We need ABGR.
    for (rgba_t &pixel : *image_) {
        std::swap(pixel.r, pixel.b);
    }

    // TODO: if there ever could be the condition of
    // int(stride / sizeof(rgba_t)) != render_width (for alignment?) : copy over

    // If requested, merge background with pattern.
    image_->AlphaComposeBackground(
        options_.bgcolor_getter, options_.bg_pattern_color,
        options_.pattern_size * options_.cell_x_px,
        options_.pattern_size * options_.cell_y_px / 2);

    return success;
}

int SVGImageSource::IndentationIfCentered(
    const timg::Framebuffer &image) const {
    return options_.center_horizontally ? (options_.width - image.width()) / 2
                                        : 0;
}

void SVGImageSource::SendFrames(const Duration &duration, int loops,
                                const volatile sig_atomic_t &interrupt_received,
                                const Renderer::WriteFramebufferFun &sink) {
    sink(IndentationIfCentered(*image_), 0, *image_, SeqType::FrameImmediate,
         {});
}

}  // namespace timg
