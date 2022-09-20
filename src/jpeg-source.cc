// -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
// (c) 2016 Henner Zeller <h.zeller@acm.org>
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

#include "jpeg-source.h"

#include <fcntl.h>
#include <libexif/exif-data.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <turbojpeg.h>
#include <unistd.h>

#include <utility>

#include "terminal-canvas.h"

extern "C" {
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

namespace timg {
namespace {
// Scoped clean-up of c objects.
struct ScopeGuard {
    explicit ScopeGuard(const std::function<void()> &f) : f_(f) {}
    ~ScopeGuard() { f_(); }
    std::function<void()> f_;
};

static void dummy_log(void *, int, const char *, va_list) {}

// Note, ExifImageOp is slightly different here than in image-source, as
// that is using graphics-magick operations, where 'flip' has a different
// meaning
struct ExifImageOp {
    int angle   = 0;
    bool mirror = false;
};
static ExifImageOp ReadExifOrientation(const uint8_t *buffer, size_t len) {
    ExifData *ed = exif_data_new_from_data(buffer, len);
    if (!ed) return {};
    ScopeGuard s([ed]() { exif_data_unref(ed); });
    ExifEntry *entry =
        exif_content_get_entry(ed->ifd[EXIF_IFD_0], EXIF_TAG_ORIENTATION);
    if (!entry || entry->format != EXIF_FORMAT_SHORT) return {};
    // clang-format off
    switch (exif_get_short(entry->data, exif_data_get_byte_order(ed))) {
    case 2: return {   0, true  };
    case 3: return { 180, false };
    case 4: return { 180, true  };
    case 5: return {  90, false };
    case 6: return { -90, false };
    case 7: return { -90, true  };
    case 8: return {  90, true  };
    }
    // clang-format on
    return {};
}

static timg::Framebuffer *ApplyExifOp(timg::Framebuffer *orig,
                                      const ExifImageOp &op) {
    const int h = orig->height();
    const int w = orig->width();
    if (op.mirror) {
        for (int y = 0; y < h; ++y) {
            Framebuffer::iterator left  = &orig->begin()[y * w];
            Framebuffer::iterator right = &orig->begin()[(y + 1) * w - 1];
            while (left < right) {
                std::swap(*left++, *right--);
            }
        }
    }
    if (op.angle == 180) {
        Framebuffer::iterator top_left     = orig->begin();
        Framebuffer::iterator bottom_right = orig->end() - 1;
        while (top_left < bottom_right) {
            std::swap(*top_left++, *bottom_right--);
        }
    }
    else if (op.angle == 90 || op.angle == -90) {
        // TODO: rotate in place without new alloc.
        // OTOH, the affected images are small.
        std::unique_ptr<timg::Framebuffer> discard(orig);
        timg::Framebuffer *result = new timg::Framebuffer(h, w);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; ++x) {
                int new_x = (op.angle == -90) ? result->width() - y - 1 : y;
                result->SetPixel(new_x, x, orig->at(x, y));
            }
        }
        return result;
    }

    return orig;
}

}  // namespace

const char *JPEGSource::VersionInfo() {
    return "TurboJPEG ";  // TODO: version number ?
}

std::string JPEGSource::FormatTitle(const std::string &format_string) const {
    return FormatFromParameters(format_string, filename_, orig_width_,
                                orig_height_, "jpeg");
}

bool JPEGSource::LoadAndScale(const DisplayOptions &opts, int, int) {
    options_ = opts;
    if (opts.scroll_animation || filename() == "/dev/stdin" ||
        filename() == "-") {
        return false;  // Not dealing with these now.
    }
    const int fd = open(filename().c_str(), O_RDONLY);
    if (fd < 0) return false;
    struct stat statresult;
    if (fstat(fd, &statresult) < 0) return false;
    const size_t filesize = statresult.st_size;
    if (filesize == 0) return false;
    void *file_buf = mmap(nullptr, filesize, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (file_buf == MAP_FAILED) return false;
    const uint8_t *jpeg_content = (uint8_t *)file_buf;

    tjhandle handle = tjInitDecompress();
    ScopeGuard s([handle, file_buf, filesize]() {  // cleanup C-objects
        tjDestroy(handle);
        munmap(file_buf, filesize);
    });

    // Figure out the original size of the image
    int width, height, jpegSubsamp, jpegColorspace;
    if (tjDecompressHeader3(handle, jpeg_content, filesize, &width, &height,
                            &jpegSubsamp, &jpegColorspace) != 0) {
        return false;
    }

    // TODO: consider applying exif rotation to width/height or leave as
    // original as these are the 'true' dimensions ?
    orig_width_  = width;
    orig_height_ = height;

    ExifImageOp exif_op;
    if (opts.exif_rotate) exif_op = ReadExifOrientation(jpeg_content, filesize);

    int target_width;
    int target_height;
    CalcScaleToFitDisplay(width, height, opts, abs(exif_op.angle) == 90,
                          &target_width, &target_height);

    // Output is larger and we request integer upscaling. That looks fuzzy
    // with our bilinear upscaling, so bail here and let generic graphicsmagick
    // image loader take care of it: that does crisp integer upscaling.
    if (opts.upscale_integer &&
        (target_width / width || target_height / height)) {
        return false;
    }

    // Find the scaling factor that creates the smallest image that is
    // larger than our target size.
    int factors_size;
    tjscalingfactor *factors = tjGetScalingFactors(&factors_size);
    int decode_width         = width;
    int decode_height        = height;
    // Looking backwards: later scale factors generate smaller images.
    for (tjscalingfactor *f = factors + factors_size; f >= factors; --f) {
        decode_width  = TJSCALED(width, (*f));
        decode_height = TJSCALED(height, (*f));
        if (decode_width >= target_width && decode_height >= target_height)
            break;
    }

    // Decode into an RGB buffer
    const TJPF decode_pixel_format = TJPF_RGBA;
    const int decode_pixel_width   = tjPixelSize[decode_pixel_format];
    const int decode_row_bytes     = decode_width * decode_pixel_width;
    timg::Framebuffer decode_image(decode_width, decode_height);
    if (tjDecompress2(handle, jpeg_content, filesize,
                      (uint8_t *)decode_image.begin(), decode_width,
                      decode_row_bytes, decode_height, decode_pixel_format,
                      0) != 0) {
        return false;
    }

    // Further scaling to desired target width/height
    av_log_set_callback(dummy_log);
    SwsContext *swsCtx = sws_getContext(
        decode_width, decode_height, AV_PIX_FMT_RGBA, target_width,
        target_height, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
    if (!swsCtx) return false;
    image_.reset(new timg::Framebuffer(target_width, target_height));

    sws_scale(swsCtx, decode_image.row_data(), decode_image.stride(), 0,
              decode_height, image_->row_data(), image_->stride());
    sws_freeContext(swsCtx);

    image_.reset(ApplyExifOp(image_.release(), exif_op));
    return true;
}

int JPEGSource::IndentationIfCentered(const timg::Framebuffer &image) const {
    return options_.center_horizontally ? (options_.width - image.width()) / 2
                                        : 0;
}

void JPEGSource::SendFrames(const Duration &duration, int loops,
                            const volatile sig_atomic_t &interrupt_received,
                            const Renderer::WriteFramebufferFun &sink) {
    sink(IndentationIfCentered(*image_), 0, *image_, SeqType::FrameImmediate,
         {});
}

}  // namespace timg
