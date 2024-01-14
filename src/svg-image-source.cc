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

extern "C" {
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

namespace timg {

static void dummy_log(void *, int, const char *, va_list) {}

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

    // If we have block graphics with double resolution in one direction
    // (-pquarter), then we got an aspect ratio from the
    // CalcScaleToFitDisplay() that is twice as wide, so we need to stretch.
    // TODO(hzeller): This is hacky, this predicate should be somehwere else.
    const bool needs_x_double_resolution =
        opts.cell_x_px == 2 && opts.cell_y_px == 2;

    // In the case of a required x double resolution, just cairo_scale()
    // the x-axis by 2 did not result in satisfactorial results, maybe due
    // to rounding on an already heavily quantized size ? Let's just render
    // in double resolution in both directions then manually scale down height.
    // TODO: figure out how to get good low-res rendering without detour.
    const int render_width = target_width;
    const int render_height =
        needs_x_double_resolution ? target_height * 2 : target_height;

    const auto kCairoFormat = CAIRO_FORMAT_ARGB32;
    int stride = cairo_format_stride_for_width(kCairoFormat, render_width);
    image_.reset(new timg::Framebuffer(stride / 4, render_height));

    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        (uint8_t *)image_->begin(), kCairoFormat, render_width, render_height,
        stride);
    cairo_t *cr = cairo_create(surface);

    RsvgRectangle viewport = {
        .x      = 0.0,
        .y      = 0.0,
        .width  = (double)render_width,
        .height = (double)render_height,
    };

    bool success = rsvg_handle_render_document(svg, cr, &viewport, nullptr);

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    g_object_unref(svg);

    // Cairo stores A (high-byte), R, G, B (low-byte). We need ABGR.
    for (rgba_t &pixel : *image_) {
        std::swap(pixel.r, pixel.b);
    }

    // TODO: if there ever could be the condition of
    // int(stride / sizeof(rgba_t)) != render_width (for alignment?) : copy over

    // In the case of the non 1:1 aspect ratio, un-stretch the height.
    if (needs_x_double_resolution) {
        av_log_set_callback(dummy_log);
        SwsContext *swsCtx = sws_getContext(
            render_width, render_height, AV_PIX_FMT_RGBA, target_width,
            target_height, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
        if (!swsCtx) return false;
        auto scaled =
            std::make_unique<Framebuffer>(target_width, target_height);

        sws_scale(swsCtx, image_->row_data(), image_->stride(), 0,
                  render_height, scaled->row_data(), scaled->stride());
        sws_freeContext(swsCtx);

        image_ = std::move(scaled);
    }

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
