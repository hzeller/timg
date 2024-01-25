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

#include "pdf-image-source.h"

#include <cairo.h>
#include <poppler.h>
#include <stdlib.h>

#include <algorithm>
#include <filesystem>

#include "framebuffer.h"

namespace fs = std::filesystem;

namespace timg {

std::string PDFImageSource::FormatTitle(
    const std::string &format_string) const {
    return FormatFromParameters(format_string, filename_, (int)orig_width_,
                                (int)orig_height_, "pdf");
}

bool PDFImageSource::LoadAndScale(const DisplayOptions &opts, int frame_offset,
                                  int frame_count) {
    options_ = opts;

    // Poppler wants a URI as input.
    const std::string uri = "file://" + fs::absolute(filename_).string();

    PopplerDocument *const document =
        poppler_document_new_from_file(uri.c_str(), nullptr, nullptr);
    if (!document) {
        return false;
    }

    bool success         = true;
    const int page_count = poppler_document_get_n_pages(document);
    const int start_page = std::max(0, frame_offset);
    const int max_display_page =
        (frame_count < 0) ? page_count
                          : std::min(page_count, start_page + frame_count);
    PopplerRectangle bounding_box;
    for (int page_num = start_page; page_num < max_display_page; ++page_num) {
        PopplerPage *const page = poppler_document_get_page(document, page_num);
        if (page == nullptr) {
            success = false;
            break;
        }

#if POPPLER_CHECK_VERSION(0, 88, 0)
        if (opts.auto_crop) {
            poppler_page_get_bounding_box(page, &bounding_box);
            orig_width_  = bounding_box.x2 - bounding_box.x1;
            orig_height_ = bounding_box.y2 - bounding_box.y1;
        }
        else
#endif
        {
            poppler_page_get_size(page, &orig_width_, &orig_height_);
            bounding_box = PopplerRectangle{
                .x1 = 0, .y1 = 0, .x2 = orig_width_, .y2 = orig_height_};
        }

        int render_width;
        int render_height;
        CalcScaleToFitDisplay(orig_width_, orig_height_, opts, false,
                              &render_width, &render_height);

        const auto kCairoFormat = CAIRO_FORMAT_ARGB32;
        int stride = cairo_format_stride_for_width(kCairoFormat, render_width);
        std::unique_ptr<timg::Framebuffer> image(
            new timg::Framebuffer(stride / 4, render_height));

        cairo_surface_t *surface = cairo_image_surface_create_for_data(
            (uint8_t *)image->begin(), kCairoFormat, render_width,
            render_height, stride);

        cairo_t *cr = cairo_create(surface);
        cairo_scale(cr, 1.0 * render_width / orig_width_,
                    1.0 * render_height / orig_height_);
        cairo_translate(cr, -bounding_box.x1, -bounding_box.y1);
        cairo_save(cr);

        // Fill background with page color.
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);

        poppler_page_render(page, cr);

        cairo_restore(cr);
        g_object_unref(page);

        cairo_destroy(cr);
        cairo_surface_destroy(surface);

        // Cairo stores A (high-byte), R, G, B (low-byte). We need ABGR.
        for (rgba_t &pixel : *image) {
            std::swap(pixel.r, pixel.b);
        }

        pages_.emplace_back(std::move(image));
    }
    g_object_unref(document);

    return success && !pages_.empty();
}

int PDFImageSource::IndentationIfCentered(
    const timg::Framebuffer &image) const {
    return options_.center_horizontally ? (options_.width - image.width()) / 2
                                        : 0;
}

void PDFImageSource::SendFrames(const Duration &duration, int loops,
                                const volatile sig_atomic_t &interrupt_received,
                                const Renderer::WriteFramebufferFun &sink) {
    for (const auto &page : pages_) {
        const int dx = IndentationIfCentered(*page);
        sink(dx, 0, *page, SeqType::FrameImmediate, {});
    }
}

}  // namespace timg
