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

// TODO: preprocessed frame and SendFrames() are similar to
// image-display.cc. Maybe things can be consolidated.

// Normalize on RGBA, so that it fits our Framebuffer format.
static constexpr int kDesiredChannels = 4;

namespace timg {
class STBImageSource::PreprocessedFrame {
public:
    PreprocessedFrame(const uint8_t *image_data, int source_w, int source_h,
                      int target_w, int target_h, const Duration &delay,
                      const DisplayOptions &opt)
        : delay_(delay), framebuffer_(target_w, target_h) {
        stbir_resize_uint8(image_data, source_w, source_h, 0,
                           (uint8_t *)framebuffer_.begin(), target_w, target_h,
                           0, kDesiredChannels /*RGBA*/);
        framebuffer_.AlphaComposeBackground(
            opt.bgcolor_getter, opt.bg_pattern_color,
            opt.pattern_size * opt.cell_x_px,
            opt.pattern_size * opt.cell_y_px / 2);
    }
    Duration delay() const { return delay_; }
    const timg::Framebuffer &framebuffer() const { return framebuffer_; }

private:
    const Duration delay_;
    timg::Framebuffer framebuffer_;
};

STBImageSource::STBImageSource(const std::string &filename)
    : ImageSource(filename) {}

STBImageSource::~STBImageSource() {
    for (PreprocessedFrame *f : frames_) delete f;
}

std::string STBImageSource::FormatTitle(
    const std::string &format_string) const {
    return FormatFromParameters(format_string, filename_, orig_width_,
                                orig_height_, "stb");
}

// NB: Animation is using some internal API of stb image, so is somewhat
// dependent on the implemantation used; but since we ship it in third_party/,
// this is not an issue.
bool STBImageSource::LoadAndScale(const DisplayOptions &options,
                                  int frame_offset, int frame_count) {
#ifdef WITH_TIMG_VIDEO
    if (LooksLikeAPNG(filename())) {
        return false;  // STB can't apng animate. Let Video do it.
    }
#endif

    FILE *img_file = stbi__fopen(filename().c_str(), "rb");
    if (!img_file) return false;

    stbi__context context;
    stbi__start_file(&context, img_file);

    int channels;
    int target_width  = 0;
    int target_height = 0;

    // There is no public API yet for accessing gif, so use some internals.
    if (stbi__gif_test(&context)) {
        stbi__gif gdata;
        memset(&gdata, 0, sizeof(gdata));
        uint8_t *data;
        while ((data = stbi__gif_load_next(&context, &gdata, &channels,
                                           kDesiredChannels, 0))) {
            if (data == (const uint8_t *)&context) break;
            orig_width_  = gdata.w;
            orig_height_ = gdata.h;

            CalcScaleToFitDisplay(gdata.w, gdata.h, options, false,
                                  &target_width, &target_height);
            frames_.push_back(new PreprocessedFrame(
                data, gdata.w, gdata.h, target_width, target_height,
                Duration::Millis(gdata.delay), options));
        }
        STBI_FREE(gdata.out);
        STBI_FREE(gdata.history);
        STBI_FREE(gdata.background);
    }
    else {
        int w, h;
        uint8_t *data = stbi__load_and_postprocess_8bit(
            &context, &w, &h, &channels, kDesiredChannels);
        if (!data) return false;

        orig_width_  = w;
        orig_height_ = h;

        CalcScaleToFitDisplay(w, h, options, false, &target_width,
                              &target_height);
        frames_.push_back(new PreprocessedFrame(
            data, w, h, target_width, target_height, Duration(), options));
        stbi_image_free(data);
    }

    indentation_ =
        options.center_horizontally ? (options.width - target_width) / 2 : 0;

    max_frames_ = (frame_count < 0)
                      ? (int)frames_.size()
                      : std::min(frame_count, (int)frames_.size());

    return !frames_.empty();
}

void STBImageSource::SendFrames(const Duration &duration, int loops,
                                const volatile sig_atomic_t &interrupt_received,
                                const Renderer::WriteFramebufferFun &sink) {
    int last_height         = -1;  // First image emit will not have a height.
    const bool is_animation = frames_.size() > 1;
    if (frames_.size() == 1 || !is_animation)
        loops = 1;  // If there is no animation, nothing to repeat.

    // Not initialized or negative value wants us to loop forever.
    // (note, kNotInitialized is actually negative, but here for clarity
    const bool loop_forever = (loops < 0) || (loops == timg::kNotInitialized);

    timg::Duration time_from_first_frame;
    bool is_first = true;
    for (int k = 0; (loop_forever || k < loops) && !interrupt_received &&
                    time_from_first_frame < duration;
         ++k) {
        for (int f = 0; f < max_frames_ && !interrupt_received; ++f) {
            const auto &frame = frames_[f];
            time_from_first_frame.Add(frame->delay());
            const int dx = indentation_;
            const int dy = is_animation && last_height > 0 ? -last_height : 0;
            SeqType seq_type = SeqType::FrameImmediate;
            if (is_animation) {
                seq_type = is_first ? SeqType::StartOfAnimation
                                    : SeqType::AnimationFrame;
            }
            sink(dx, dy, frame->framebuffer(), seq_type,
                 std::min(time_from_first_frame, duration));
            last_height = frame->framebuffer().height();
            if (time_from_first_frame > duration) break;
            is_first = false;
        }
    }
}
}  // namespace timg
