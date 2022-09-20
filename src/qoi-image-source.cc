// -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
// (c) 2022 Henner Zeller <h.zeller@acm.org>
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

#include "qoi-image-source.h"

#define QOI_IMPLEMENTATION
#include <stdlib.h>

#include "../third_party/qoi.h"
#include "framebuffer.h"

extern "C" {
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

namespace timg {

static void dummy_log(void *, int, const char *, va_list) {}

std::string QOIImageSource::FormatTitle(
    const std::string &format_string) const {
    return FormatFromParameters(format_string, filename_, orig_width_,
                                orig_height_, "qoi");
}

bool QOIImageSource::LoadAndScale(const DisplayOptions &opts, int, int) {
    options_ = opts;
    qoi_desc desc;
    void *const qoi_pic = qoi_read(filename().c_str(), &desc, 4);
    if (!qoi_pic) return false;

    // TODO: would be good if Framebuffer supported adopting foreign buffer.
    timg::Framebuffer image_in(desc.width, desc.height);
    memcpy((void *)image_in.begin(), qoi_pic,
           (size_t)image_in.width() * image_in.height() * sizeof(rgba_t));
    free(qoi_pic);

    orig_width_  = image_in.width();
    orig_height_ = image_in.height();

    int target_width;
    int target_height;
    CalcScaleToFitDisplay(desc.width, desc.height, opts, false, &target_width,
                          &target_height);

    // Further scaling to desired target width/height
    av_log_set_callback(dummy_log);
    SwsContext *swsCtx =
        sws_getContext(desc.width, desc.height, AV_PIX_FMT_RGBA,      //  in
                       target_width, target_height, AV_PIX_FMT_RGBA,  // out
                       SWS_BILINEAR, NULL, NULL, NULL);
    if (!swsCtx) return false;
    image_.reset(new timg::Framebuffer(target_width, target_height));

    sws_scale(swsCtx, image_in.row_data(), image_in.stride(), 0,
              image_in.height(), image_->row_data(), image_->stride());
    sws_freeContext(swsCtx);

    if (desc.channels == 4) {
        image_->AlphaComposeBackground(
            options_.bgcolor_getter, options_.bg_pattern_color,
            options_.pattern_size * options_.cell_x_px,
            options_.pattern_size * options_.cell_y_px / 2);
    }
    return true;
}

int QOIImageSource::IndentationIfCentered(
    const timg::Framebuffer &image) const {
    return options_.center_horizontally ? (options_.width - image.width()) / 2
                                        : 0;
}

void QOIImageSource::SendFrames(const Duration &duration, int loops,
                                const volatile sig_atomic_t &interrupt_received,
                                const Renderer::WriteFramebufferFun &sink) {
    sink(IndentationIfCentered(*image_), 0, *image_, SeqType::FrameImmediate,
         {});
}

}  // namespace timg
