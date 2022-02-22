// -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
// (c) 2021 Leonardo Romor <leonardo.romor@gmail.com>,
//          Henner Zeller <h.zeller@acm.org>
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

#include "openslide-source.h"

#include <openslide.h>
#include <unistd.h>

#include <cstdio>
#include <limits>
#include <string>
#include <utility>

#include "framebuffer.h"
#include "terminal-canvas.h"

extern "C" {
#include <libswscale/swscale.h>
}

namespace timg {
const char *OpenSlideSource::VersionInfo() { return openslide_get_version(); }

std::string OpenSlideSource::FormatTitle(
    const std::string &format_string) const {
    return FormatFromParameters(format_string, filename_, orig_width_,
                                orig_height_, "openslide");
}

static void dummy_log(void *, int, const char *, va_list) {}

// Scoped clean-up of c objects.
struct ScopeGuard {
    explicit ScopeGuard(const std::function<void()> &f) : f_(f) {}
    ~ScopeGuard() { f_(); }
    std::function<void()> f_;
};

static inline bool invalid_dimensions(int64_t width, int64_t height) {
    return (width < 0 || height < 0) ||
           (width > std::numeric_limits<int>::max() ||
            height > std::numeric_limits<int>::max());
}

bool OpenSlideSource::LoadAndScale(const DisplayOptions &opts, int, int) {
    options_ = opts;
    if (opts.scroll_animation || filename() == "/dev/stdin" ||
        filename() == "-") {
        return false;  // Not dealing with these now.
    }

    // Check if this format is supported
    openslide_t *osr = openslide_open(filename().c_str());
    if (!osr) return false;

    // Automatically cleanup openslide context
    ScopeGuard s([osr]() { openslide_close(osr); });

    std::unique_ptr<timg::Framebuffer> source_image;
    int64_t width, height, level0_width, level0_height;
    int target_width;
    int target_height;

    // We need to use one of the layers of the pyramid.
    // Get native the highest resolution image.
    openslide_get_level0_dimensions(osr, &level0_width, &level0_height);
    if (invalid_dimensions(level0_width, level0_height)) return false;

    orig_width_  = level0_width;
    orig_height_ = level0_height;

    // Get the target size
    CalcScaleToFitDisplay(level0_width, level0_height, opts, false,
                          &target_width, &target_height);

    // First check if there's a thumbnail with enough resolution.
    for (const auto *n = openslide_get_associated_image_names(osr); *n; ++n) {
        const char *const name = *n;
        if (strcmp(name, "thumbnail") != 0)
            continue;  // Only interested in thumbnail.

        openslide_get_associated_image_dimensions(osr, name, &width, &height);
        if (invalid_dimensions(width, height)) break;

        // If the thumbnail width is smaller than the target,
        // means we have to sample from a larger image.
        if (width < target_width) break;

        source_image.reset(new timg::Framebuffer(width, height));
        openslide_read_associated_image(osr, name,
                                        (uint32_t *)source_image->begin());
        break;
    }

    if (!source_image) {
        // Get the best layer to downsample with scaling
        // computed from layer 0 (the highest resolution one).
        const double downscale_factor = (double)level0_width / target_width;
        const int32_t level =
            openslide_get_best_level_for_downsample(osr, downscale_factor);
        if (level < 0) return false;

        // Get target layer dimensions.
        openslide_get_level_dimensions(osr, level, &width, &height);
        if (invalid_dimensions(width, height)) return false;

        source_image.reset(new timg::Framebuffer(width, height));
        openslide_read_region(osr, (uint32_t *)source_image->begin(), 0, 0,
                              level, width, height);
        if (openslide_get_error(osr)) return false;
    }

    // Further scaling to desired target width/height
    av_log_set_callback(dummy_log);
    SwsContext *swsCtx = sws_getContext(
        width, height, AV_PIX_FMT_RGB32, target_width, target_height,
        AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
    if (!swsCtx) return false;
    image_.reset(new timg::Framebuffer(target_width, target_height));
    sws_scale(swsCtx, source_image->row_data(), source_image->stride(), 0,
              height, image_->row_data(), image_->stride());
    sws_freeContext(swsCtx);
    image_->AlphaComposeBackground(opts.bgcolor_getter, opts.bg_pattern_color,
                                   opts.pattern_size * options_.cell_x_px,
                                   opts.pattern_size * options_.cell_y_px / 2);
    return true;
}

int OpenSlideSource::IndentationIfCentered(
    const timg::Framebuffer &image) const {
    return options_.center_horizontally ? (options_.width - image.width()) / 2
                                        : 0;
}

void OpenSlideSource::SendFrames(const Duration &duration, int loops,
                                 const volatile sig_atomic_t &,
                                 const Renderer::WriteFramebufferFun &sink) {
    sink(IndentationIfCentered(*image_), 0, *image_, SeqType::FrameImmediate,
         {});
}

}  // namespace timg
