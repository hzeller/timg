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

#include "image-display.h"

#include <Magick++.h>
#include <assert.h>
#include <magick/image.h>
#include <math.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>

#include "terminal-canvas.h"
#include "timg-time.h"

static constexpr bool kDebug = false;

namespace timg {
static void CopyToFramebuffer(const Magick::Image &img,
                              timg::Framebuffer *result) {
    assert(result->width() >= (int)img.columns() &&
           result->height() >= (int)img.rows());
    for (size_t y = 0; y < img.rows(); ++y) {
        for (size_t x = 0; x < img.columns(); ++x) {
            const Magick::Color &c = img.pixelColor(x, y);
            result->SetPixel(
                x, y,
                {ScaleQuantumToChar(c.redQuantum()),
                 ScaleQuantumToChar(c.greenQuantum()),
                 ScaleQuantumToChar(c.blueQuantum()),
                 (uint8_t)(0xff - ScaleQuantumToChar(c.alphaQuantum()))});
        }
    }
}

// Frame already prepared as the buffer to be sent, so copy to terminal-buffer
// does not have to be done online. Also knows about the animation delay.
class ImageLoader::PreprocessedFrame {
public:
    PreprocessedFrame(const Magick::Image &img, const DisplayOptions &opt,
                      bool is_part_of_animation)
        : delay_(DurationFromImgDelay(img, is_part_of_animation)),
          framebuffer_(img.columns(), img.rows()) {
        CopyToFramebuffer(img, &framebuffer_);
        framebuffer_.AlphaComposeBackground(
            opt.bgcolor_getter, opt.bg_pattern_color,
            opt.pattern_size * opt.cell_x_px,
            opt.pattern_size * opt.cell_y_px / 2);
    }
    Duration delay() const { return delay_; }
    const timg::Framebuffer &framebuffer() const { return framebuffer_; }

private:
    static Duration DurationFromImgDelay(const Magick::Image &img,
                                         bool is_part_of_animation) {
        if (!is_part_of_animation) return Duration::Millis(0);
        int delay_time = img.animationDelay();  // in 1/100s of a second.
        if (delay_time < 1) delay_time = 10;
        return Duration::Millis(delay_time * 10);
    }
    const Duration delay_;
    timg::Framebuffer framebuffer_;
};

ImageLoader::~ImageLoader() {
    for (PreprocessedFrame *f : frames_) delete f;
}

const char *ImageLoader::VersionInfo() {
    return "GraphicsMagick " MagickLibVersionText " (" MagickReleaseDate ")";
}

std::string ImageLoader::FormatTitle(const std::string &format_string) const {
    return FormatFromParameters(format_string, filename_, orig_width_,
                                orig_height_, "image");
}

static bool EndsWith(const std::string &filename, const char *suffix) {
    const size_t flen = filename.length();
    const size_t slen = strlen(suffix);
    if (flen < slen) return false;
    return strcasecmp(filename.c_str() + flen - slen, suffix) == 0;
}

struct ExifImageOp {
    int angle = 0;
    bool flip = false;
};
// Parameter const in spirit, but img.attribute() is unfortunately non-const.
static ExifImageOp GetExifOp(Magick::Image &img) {  // NOLINT
    const std::string rotation_tag = img.attribute("EXIF:Orientation");
    if (rotation_tag.empty() || rotation_tag.size() != 1)
        return {};  // Nothing to do or broken tag.
    // clang-format off
    switch (rotation_tag[0]) {
    case '2': return { 180, true  };
    case '3': return { 180, false };
    case '4': return {   0, true  };
    case '5': return {  90, true  };
    case '6': return {  90, false };
    case '7': return { -90, true  };
    case '8': return { -90, false };
    }
    // clang-format on
    return {};
}

// An extended version of Magick::readImages that requests a
// decoding/raster with a transparent background by priming
// the opacity in the image info.
static void readImagesWithTransparentBackground(
    std::vector<Magick::Image> *sequence, const std::string &filename) {
    MagickLib::ImageInfo *image_info = MagickLib::CloneImageInfo(nullptr);

    // ScaleCharToQuantum resolves to ((Quantum)(257U * (value)))
    // but Quantum is undefined...
    using Quantum = MagickLib::Quantum;

    // Set the opacity quantum to maximum value for transparent background.
    image_info->background_color.opacity = ScaleCharToQuantum(255);
    filename.copy(image_info->filename, MaxTextExtent - 1);
    image_info->filename[filename.length()] = 0;
    MagickLib::ExceptionInfo exception_info;
    MagickLib::GetExceptionInfo(&exception_info);
    MagickLib::Image *images =
        MagickLib::ReadImage(image_info, &exception_info);
    MagickLib::DestroyImageInfo(image_info);
    insertImages(sequence, images);
    Magick::throwException(exception_info);
}

bool ImageLoader::LoadAndScale(const DisplayOptions &opts, int frame_offset,
                               int frame_count) {
    options_ = opts;

#ifdef WITH_TIMG_VIDEO
    if (LooksLikeAPNG(filename())) {
        return false;  // ImageMagick can't apng animate. Let Video do it.
    }
#endif

    std::vector<Magick::Image> frames;
    try {
        readImagesWithTransparentBackground(
            &frames, filename());  // ideally, we could set max_frames
    }
    catch (Magick::Warning &warning) {
        if (kDebug)
            fprintf(stderr, "Meh: %s (%s)\n", filename().c_str(),
                    warning.what());
    }
    catch (std::exception &e) {
        // No message, let that file be handled by the next handler.
        if (kDebug)
            fprintf(stderr, "Exception: %s (%s)\n", filename().c_str(),
                    e.what());
        return false;
    }

    if (frames.empty()) {
        if (kDebug) fprintf(stderr, "No image found.");
        return false;
    }

    orig_width_  = frames.front().columns();
    orig_height_ = frames.front().rows();

    // We don't really know if something is an animation from the frames we
    // got back (or is there ?), so we use a blacklist approach here: filenames
    // that are known to be containers for multiple independent images are
    // considered not an animation.
    const bool could_be_animation =
        !EndsWith(filename(), ".ico") && !EndsWith(filename(), ".pdf") &&
        !EndsWith(filename(), ".ps") && !EndsWith(filename(), ".txt");

    is_animation_before_frame_limit_ = could_be_animation && frames.size() > 1;

    // We can't remove the offset yet as the coalesceImages() might need images
    // prior to our desired set.
    if (frame_count > 0 && frame_offset + frame_count < (int)frames.size()) {
        frames.resize(frame_offset + frame_count);
    }

    std::vector<Magick::Image> result;
    // Put together the animation from single frames. GIFs can have nasty
    // disposal modes, but they are handled nicely by coalesceImages()
    if (frames.size() > 1 && could_be_animation) {
        Magick::coalesceImages(&result, frames.begin(), frames.end());
        is_animation_ = true;
    }
    else {
        result.insert(result.end(), frames.begin(), frames.end());
        is_animation_ = false;
    }

    if (frame_offset > 0) {
        frame_offset = std::min(frame_offset, (int)result.size() - 1);
        result.erase(result.begin(), result.begin() + frame_offset);
    }

    for (Magick::Image &img : result) {
        ExifImageOp exif_op;
        if (opts.exif_rotate) exif_op = GetExifOp(img);

        // We do trimming only if this is not an animation, which will likely
        // not create a pleasent result.
        if (!is_animation_) {
            if (opts.crop_border > 0) {
                const int c = opts.crop_border;
                const int w = std::max(1, (int)img.columns() - 2 * c);
                const int h = std::max(1, (int)img.rows() - 2 * c);
                img.crop(Magick::Geometry(w, h, c, c));
            }
            if (opts.auto_crop) {
                img.trim();
            }
        }

        // Figure out scaling for the image.
        int target_width = 0, target_height = 0;
        if (CalcScaleToFitDisplay(img.columns(), img.rows(), opts,
                                  abs(exif_op.angle) == 90, &target_width,
                                  &target_height)) {
            try {
                auto geometry = Magick::Geometry(target_width, target_height);
                geometry.aspect(true);  // Force to scale to given size.
                if (opts.antialias)
                    img.scale(geometry);
                else
                    img.sample(geometry);
            }
            catch (const std::exception &e) {
                if (kDebug)
                    fprintf(stderr, "%s: %s\n", filename().c_str(), e.what());
                return false;
            }
        }

        // Now that the image is nice and small, the following ops are cheap
        if (exif_op.flip) img.flip();
        img.rotate(exif_op.angle);

        frames_.push_back(new PreprocessedFrame(img, opts, result.size() > 1));
    }

    max_frames_ = (frame_count < 0)
                      ? (int)frames_.size()
                      : std::min(frame_count, (int)frames_.size());

    return true;
}

int ImageLoader::IndentationIfCentered(const PreprocessedFrame *frame) const {
    return options_.center_horizontally
               ? (options_.width - frame->framebuffer().width()) / 2
               : 0;
}

void ImageLoader::SendFrames(const Duration &duration, int loops,
                             const volatile sig_atomic_t &interrupt_received,
                             const Renderer::WriteFramebufferFun &sink) {
    if (options_.scroll_animation) {
        Scroll(duration, loops, interrupt_received, options_.scroll_dx,
               options_.scroll_dy, options_.scroll_delay, sink);
        return;
    }

    int last_height = -1;  // First image emit will not have a height.
    if (frames_.size() == 1 || !is_animation_)
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
            const int dx = IndentationIfCentered(frame);
            const int dy = is_animation_ && last_height > 0 ? -last_height : 0;
            SeqType seq_type = SeqType::FrameImmediate;
            if (is_animation_) {
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

static int gcd(int a, int b) { return b == 0 ? a : gcd(b, a % b); }

void ImageLoader::Scroll(const Duration &duration, int loops,
                         const volatile sig_atomic_t &interrupt_received,
                         int dx, int dy, const Duration &scroll_delay,
                         const Renderer::WriteFramebufferFun &write_fb) {
    if (frames_.size() > 1) {
        if (kDebug)
            fprintf(stderr,
                    "This is an %simage format, "
                    "scrolling on top of that is not supported. "
                    "Just doing the scrolling of the first frame.\n",
                    is_animation_ ? "animated " : "multi-");
        // TODO: do both.
    }

    const Framebuffer &img = frames_[0]->framebuffer();
    const int img_width    = img.width();
    const int img_height   = img.height();

    const int display_w = std::min(options_.width, img_width);
    const int display_h = std::min(options_.height, img_height);

    // Since the user can choose the number of cycles we go through it,
    // we need to calculate what the minimum number of steps is we need
    // to do the scroll. If this is just in one direction, that is simple: the
    // number of pixel in that direction. If we go diagonal, then it is
    // essentially the least common multiple of steps.
    const int x_steps =
        (dx == 0)
            ? 1
            : ((img_width % abs(dx) == 0) ? img_width / abs(dx) : img_width);
    const int y_steps =
        (dy == 0)
            ? 1
            : ((img_height % abs(dy) == 0) ? img_height / abs(dy) : img_height);
    const int64_t cycle_steps = x_steps * y_steps / gcd(x_steps, y_steps);

    // Depending if we go forward or backward, we want to start out aligned
    // right or left.
    // For negative direction, guarantee that we never run into negative
    // numbers.
    const int64_t x_init =
        (dx < 0) ? (img_width - display_w - dx * cycle_steps) : 0;
    const int64_t y_init =
        (dy < 0) ? (img_height - display_h - dy * cycle_steps) : 0;
    bool is_first = true;

    timg::Framebuffer display_fb(display_w, display_h);
    timg::Duration time_from_first_frame;
    for (int k = 0; (loops < 0 || k < loops) && !interrupt_received &&
                    time_from_first_frame < duration;
         ++k) {
        for (int64_t cycle_pos = 0; cycle_pos <= cycle_steps; ++cycle_pos) {
            if (interrupt_received || time_from_first_frame > duration) break;
            const int64_t x_cycle_pos = dx * cycle_pos;
            const int64_t y_cycle_pos = dy * cycle_pos;
            for (int y = 0; y < display_h; ++y) {
                for (int x = 0; x < display_w; ++x) {
                    const int x_src = (x_init + x_cycle_pos + x) % img_width;
                    const int y_src = (y_init + y_cycle_pos + y) % img_height;
                    display_fb.SetPixel(x, y, img.at(x_src, y_src));
                }
            }
            time_from_first_frame.Add(scroll_delay);
            write_fb(
                0, is_first ? 0 : -display_fb.height(), display_fb,
                is_first ? SeqType::StartOfAnimation : SeqType::AnimationFrame,
                time_from_first_frame);
            is_first = false;
        }
    }
}

}  // namespace timg
