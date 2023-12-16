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

#include "framebuffer.h"

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

    RsvgRectangle viewport = {
        .x      = 0.0,
        .y      = 0.0,
        .width  = (double)target_width,
        .height = (double)target_height,
    };

    bool success = rsvg_handle_render_document(svg, cr, &viewport, nullptr);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    g_object_unref(svg);

    // TODO: for non-1:1 aspect ratio of the output (e.g. pixelation=quarter),
    //       the resulting aspect ratio is squished, as we have to do the
    //       distortion ourself.
    // TODO: if (int(stride / sizeof(rgba_t)) != target_width) : copy over

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
