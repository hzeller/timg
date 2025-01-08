// -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
// (c) 2025 Henner Zeller <h.zeller@acm.org>
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

#include "image-scaler.h"

#ifdef WITH_TIMG_SWS_RESIZE
extern "C" {  // avutil is missing extern "C"
#include <libavutil/log.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}
#endif

#ifdef WITH_TIMG_STB_RESIZE
#define STB_IMAGE_RESIZE_IMPLEMENTATION
// Least amount of fuzziness on upscaling
#define STBIR_DEFAULT_FILTER_UPSAMPLE STBIR_FILTER_BOX
#include "stb/stb_image_resize2.h"
#endif

// TODO: there is also zimg.

namespace timg {
namespace {
#if WITH_TIMG_SWS_RESIZE
static void dummy_log(void *, int, const char *, va_list) {}

class SWSImageScaler final : public ImageScaler {
public:
    static SWSImageScaler *Create(int in_width, int in_height,
                                  ColorFmt in_color_format, int out_width,
                                  int out_height) {
        SwsContext *const sws_context = sws_getContext(
            in_width, in_height,
            in_color_format == ColorFmt::kRGBA ? AV_PIX_FMT_RGBA
                                               : AV_PIX_FMT_RGB32,
            out_width, out_height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr,
            nullptr, nullptr);
        if (!sws_context) return nullptr;
        av_log_set_callback(dummy_log);
        return new SWSImageScaler(sws_context);
    }

    ~SWSImageScaler() { sws_freeContext(sws_context_); }

    void Scale(Framebuffer &in, Framebuffer *out) final {
        sws_scale(sws_context_, in.row_data(), in.stride(), 0, in.height(),
                  out->row_data(), out->stride());
    }

private:
    explicit SWSImageScaler(SwsContext *context) : sws_context_(context) {}

    SwsContext *const sws_context_;
};
#endif
#if WITH_TIMG_STB_RESIZE
class STBImageScaler final : public ImageScaler {
public:
    static STBImageScaler *Create(int in_width, int in_height,
                                  ColorFmt in_color_format, int out_width,
                                  int out_height) {
        return new STBImageScaler(in_color_format);
    }

    void Scale(Framebuffer &in, Framebuffer *out) final {
        struct STBIR_RESIZE context {};
        stbir_resize_init(&context, (uint8_t *)in.begin(), in.width(),
                          in.height(), 0, (uint8_t *)out->begin(), out->width(),
                          out->height(), 0, STBIR_RGBA, STBIR_TYPE_UINT8);
        stbir_set_pixel_layouts(
            &context, in_fmt_ == ColorFmt::kRGBA ? STBIR_RGBA : STBIR_BGRA,
            STBIR_RGBA);
        stbir_resize_extended(&context);
    }

private:
    explicit STBImageScaler(ColorFmt in_fmt) : in_fmt_(in_fmt) {}
    const ColorFmt in_fmt_;
};
#endif
}  // namespace

std::unique_ptr<ImageScaler> ImageScaler::Create(int in_width, int in_height,
                                                 ColorFmt in_color_format,
                                                 int out_width,
                                                 int out_height) {
#if WITH_TIMG_SWS_RESIZE
    return std::unique_ptr<ImageScaler>(SWSImageScaler::Create(
        in_width, in_height, in_color_format, out_width, out_height));
#elif WITH_TIMG_STB_RESIZE
    return std::unique_ptr<ImageScaler>(STBImageScaler::Create(
        in_width, in_height, in_color_format, out_width, out_height));
#else
#error "Configure issue: no image scaler available"
    return nullptr;
#endif
}
}  // namespace timg
