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

#include "stb-image-source.h"

#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
// Least amount of fuzziness on upscaling
#define STBIR_DEFAULT_FILTER_UPSAMPLE STBIR_FILTER_BOX
#include "../third_party/stb_image_resize.h"

namespace timg {
STBImageSource::STBImageSource(const std::string &filename)
    : ImageSource(filename) {
}

// Really only for fallback, so we don't worry about animated images. Also
// scaling is worse than sws_scale, but fewer dependencies are better.
bool STBImageSource::LoadAndScale(const DisplayOptions &options,
                                  int frame_offset, int frame_count) {
    // Normalize on RGBA, so that it fits our Framebuffer format.
    const int kDesiredChannels = 4;

    int w, h, channels;
    uint8_t *data = stbi_load(filename_.c_str(), &w, &h, &channels,
                              kDesiredChannels);
    if (!data) return false;

    int target_width;
    int target_height;
    CalcScaleToFitDisplay(w, h, options, false, &target_width, &target_height);

    channels = kDesiredChannels;

    image_.reset(new timg::Framebuffer(target_width, target_height));
    stbir_resize_uint8(data, w, h, 0,
                       (uint8_t*) image_->begin(),
                       target_width, target_height, 0,
                       channels);
    stbi_image_free(data);

    image_->AlphaComposeBackground(
        options.bgcolor_getter, options.bg_pattern_color,
        options.pattern_size * options.cell_x_px,
        options.pattern_size * options.cell_y_px/2);

    indentation_ = options.center_horizontally
        ? (options.width - image_->width()) / 2
        : 0;

    return true;
}

void STBImageSource::SendFrames(Duration duration, int loops,
                                const volatile sig_atomic_t &interrupt_received,
                                const Renderer::WriteFramebufferFun &sink) {
    sink(indentation_, 0, *image_, SeqType::FrameImmediate, {});
}
}  // namespace timg
