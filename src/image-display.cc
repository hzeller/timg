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

#include "terminal-canvas.h"
#include "timg-time.h"

#include <algorithm>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <Magick++.h>

namespace timg {
// Returns 'true' if anything is to do to the picture.
bool CalcScaleToFitDisplay(int img_width, int img_height,
                           const DisplayOptions &options,
                           int *target_width, int *target_height) {
    const float width_fraction = (float)options.width / img_width;
    const float height_fraction = (float)options.height / img_height;

    // If the image < screen, only upscale if do_upscale requested
    if (!options.upscale &&
        (options.fill_height || width_fraction > 1.0) &&
        (options.fill_width || height_fraction > 1.0)) {
        *target_width = img_width;
        *target_height = img_height;
        return false;
    }

    *target_width = options.width;
    *target_height = options.height;

    if (options.fill_width && options.fill_height) {
        // Fill as much as we can get in available space.
        // Largest scale fraction determines that. This is for some diagonal
        // scroll modes.
        const float larger_fraction = (width_fraction > height_fraction)
            ? width_fraction
            : height_fraction;
        *target_width = (int) roundf(larger_fraction * img_width);
        *target_height = (int) roundf(larger_fraction * img_height);
    }
    else if (options.fill_height) {
        // Make things fit in vertical space.
        // While the height constraint stays the same, we can expand width to
        // wider than screen.
        *target_width = (int) roundf(height_fraction * img_width);
    }
    else if (options.fill_width) {
        // dito, vertical. Make things fit in horizontal, overflow vertical.
        *target_height = (int) roundf(width_fraction * img_height);
    }
    else {
        // Typical situation: whatever limits first
        const float smaller_fraction = (width_fraction < height_fraction)
            ? width_fraction
            : height_fraction;
        *target_width = (int) roundf(smaller_fraction * img_width);
        *target_height = (int) roundf(smaller_fraction * img_height);
    }
    return *target_width != img_width || *target_height != img_height;
}

void CopyToFramebuffer(const Magick::Image &img, timg::Framebuffer *result) {
    assert(result->width() >= (int) img.columns()
           && result->height() >= (int) img.rows());
    for (size_t y = 0; y < img.rows(); ++y) {
        for (size_t x = 0; x < img.columns(); ++x) {
            const Magick::Color &c = img.pixelColor(x, y);
            if (c.alphaQuantum() >= 255)
                continue;
            result->SetPixel(x, y,
                             ScaleQuantumToChar(c.redQuantum()),
                             ScaleQuantumToChar(c.greenQuantum()),
                             ScaleQuantumToChar(c.blueQuantum()));
        }
    }
}

// Frame already prepared as the buffer to be sent, so copy to terminal-buffer
// does not have to be done online. Also knows about the animation delay.
class ImageLoader::PreprocessedFrame {
public:
    PreprocessedFrame(const Magick::Image &img, bool is_part_of_animation)
        : delay_(DurationFromImgDelay(img, is_part_of_animation)),
          framebuffer_(img.columns(), img.rows()) {
        CopyToFramebuffer(img, &framebuffer_);
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

static void RenderBackground(int width, int height,
                             const char *bg, const char *pattern,
                             Magick::Image *bgimage) {
    *bgimage = Magick::Image(Magick::Geometry(width, height),
                             Magick::Color(bg ? bg : "black"));
    if (pattern && strlen(pattern)) {
        bgimage->fillColor(pattern);
        for (int x = 0; x < width; x++) {
            for (int y = 0; y < height; y++) {
                if ((x + y) % 2 == 0) {
                    bgimage->draw(Magick::DrawablePoint(x, y));
                }
            }
        }
    }
}

ImageLoader::~ImageLoader() {
    for (PreprocessedFrame *f : frames_) delete f;
}

const char *ImageLoader::VersionInfo() {
    return "GraphicsMagick " MagickLibVersionText " (" MagickReleaseDate ")";
}

static bool EndsWith(const char *filename, const char *suffix) {
    size_t flen = strlen(filename);
    size_t slen = strlen(suffix);
    if (flen < slen) return false;
    return strcasecmp(filename + flen - slen, suffix) == 0;
}

bool ImageLoader::LoadAndScale(const char *filename,
                               const DisplayOptions &opts) {
    display_width_ = opts.width;
    display_height_ = opts.height;
    center_horizontally_ = opts.center_horizontally;

    std::vector<Magick::Image> frames;
    try {
        readImages(&frames, filename);
    }
    catch(Magick::Warning &warning) {
        //fprintf(stderr, "Meh: %s (%s)\n", filename, warning.what());
    }
    catch (std::exception& e) {
        // No message, let that file be handled by the next handler.
        return false;
    }

    if (frames.size() == 0) {
        fprintf(stderr, "No image found.");
        return false;
    }

    // We don't really know if something is an animation from the frames we
    // got back (or is there ?), so we use a blacklist approach here: filenames
    // that are known to be containers for multiple independent images are
    // considered not an animation.
    const bool could_be_animation =
        !EndsWith(filename, "ico") && !EndsWith(filename, "pdf");

    std::vector<Magick::Image> result;
    // Put together the animation from single frames. GIFs can have nasty
    // disposal modes, but they are handled nicely by coalesceImages()
    if (frames.size() > 1 && could_be_animation) {
        Magick::coalesceImages(&result, frames.begin(), frames.end());
        is_animation_ = true;
    } else {
        result.insert(result.end(), frames.begin(), frames.end());
        is_animation_ = false;
    }

    for (Magick::Image &img : result) {
        // We do trimming only if this is not an animation, which will likely
        // not create a pleasent result.
        if (!is_animation_) {
            if (opts.crop_border > 0) {
                const int c = opts.crop_border;
                const int w = std::max(1, (int)img.columns() - 2*c);
                const int h = std::max(1, (int)img.rows() - 2*c);
                img.crop(Magick::Geometry(w, h, c, c));
            }
            if (opts.auto_crop) {
                img.trim();
            }
        }

        // Figure out scaling for the image.
        int target_width = 0, target_height = 0;
        if (CalcScaleToFitDisplay(img.columns(), img.rows(), opts,
                                  &target_width, &target_height)) {
            if (opts.antialias)
                img.scale(Magick::Geometry(target_width, target_height));
            else
                img.sample(Magick::Geometry(target_width, target_height));
        }

        // If these are transparent and should get a background, apply that.
        if (opts.bg_color || opts.bg_pattern_color) {
            Magick::Image target;
            try {
                RenderBackground(img.columns(), img.rows(),
                                 opts.bg_color, opts.bg_pattern_color, &target);
            }
            catch (std::exception& e) {
                fprintf(stderr, "Trouble rendering background (%s)\n",
                        e.what());
                return false;
            }
            target.composite(img, 0, 0, Magick::OverCompositeOp);
            target.animationDelay(img.animationDelay());  // lost otherwise.
            img = target;
        }
        frames_.push_back(new PreprocessedFrame(img, result.size() > 1));
    }

    return true;
}

int ImageLoader::IndentationIfCentered(const PreprocessedFrame *frame) const {
    return center_horizontally_
        ? (display_width_ - frame->framebuffer().width()) / 2
        : 0;
}

void ImageLoader::Display(Duration duration, int max_frames, int loops,
                          const volatile sig_atomic_t &interrupt_received,
                          timg::TerminalCanvas *canvas) {
    if (max_frames == -1) {
        max_frames = (int)frames_.size();
    } else {
        max_frames = std::min(max_frames, (int)frames_.size());
    }

    const Time end_time = Time::Now() + duration;
    int last_height = -1;  // First one will not have a height.
    if (frames_.size() == 1 || !is_animation_)
        loops = 1;   // If there is no animation, nothing to repeat.
    for (int k = 0;
         (loops < 0 || k < loops)
             && !interrupt_received
             && Time::Now() < end_time;
         ++k) {
        for (int f = 0; f < max_frames; ++f) {
            const auto &frame = frames_[f];
            const Time frame_start = Time::Now();
            if (interrupt_received || frame_start >= end_time)
                break;
            if (is_animation_ && last_height > 0) {
                canvas->JumpUpPixels(last_height);
            }
            canvas->Send(frame->framebuffer(), IndentationIfCentered(frame));
            last_height = frame->framebuffer().height();
            if (!frame->delay().is_zero()) {
                const Time frame_finish = frame_start + frame->delay();
                frame_finish.WaitUntil();
            }
        }
    }
}

static int gcd(int a, int b) { return b == 0 ? a : gcd(b, a % b); }

void ImageLoader::Scroll(Duration duration, int loops,
                         const volatile sig_atomic_t &interrupt_received,
                         int dx, int dy, Duration scroll_delay,
                         timg::TerminalCanvas *canvas) {
    if (frames_.size() > 1) {
        fprintf(stderr, "This is an %simage format, "
                "scrolling on top of that is not supported. "
                "Just doing the scrolling of the first frame.\n",
                is_animation_ ? "animated " : "multi-");
        // TODO: do both.
    }

    const Framebuffer &img = frames_[0]->framebuffer();
    const int img_width = img.width();
    const int img_height = img.height();

    const int display_w = std::min(display_width_, img_width);
    const int display_h = std::min(display_height_, img_height);

    // Since the user can choose the number of cycles we go through it,
    // we need to calculate what the minimum number of steps is we need
    // to do the scroll. If this is just in one direction, that is simple: the
    // number of pixel in that direction. If we go diagonal, then it is
    // essentially the least common multiple of steps.
    const int x_steps = (dx == 0)
        ? 1
        : ((img_width % abs(dx) == 0) ? img_width / abs(dx) : img_width);
    const int y_steps = (dy == 0)
        ? 1
        : ((img_height % abs(dy) == 0) ? img_height / abs(dy) : img_height);
    const int64_t cycle_steps =  x_steps * y_steps / gcd(x_steps, y_steps);

    // Depending if we go forward or backward, we want to start out aligned
    // right or left.
    // For negative direction, guarantee that we never run into negative numbers.
    const int64_t x_init = (dx < 0)
        ? (img_width - display_w - dx*cycle_steps) : 0;
    const int64_t y_init = (dy < 0)
        ? (img_height - display_h - dy*cycle_steps) : 0;
    bool is_first = true;

    timg::Framebuffer display_fb(display_w, display_h);
    const Time end_time = Time::Now() + duration;
    for (int k = 0;
         (loops < 0 || k < loops)
             && !interrupt_received
             && Time::Now() < end_time;
         ++k) {
        for (int64_t cycle_pos = 0; cycle_pos <= cycle_steps; ++cycle_pos) {
            const Time frame_start = Time::Now();
            if (interrupt_received || frame_start >= end_time)
                break;
            const int64_t x_cycle_pos = dx*cycle_pos;
            const int64_t y_cycle_pos = dy*cycle_pos;
            for (int y = 0; y < display_h; ++y) {
                for (int x = 0; x < display_w; ++x) {
                    const int x_src = (x_init + x_cycle_pos + x) % img_width;
                    const int y_src = (y_init + y_cycle_pos + y) % img_height;
                    display_fb.SetPixel(x, y, img.at(x_src, y_src));
                }
            }
            if (!is_first) {
                canvas->JumpUpPixels(display_fb.height());
            }
            canvas->Send(display_fb, 0);
            is_first = false;
            if (!scroll_delay.is_zero()) {
                const Time frame_finish = frame_start + scroll_delay;
                frame_finish.WaitUntil();
            }
        }
    }
}

}  // namespace timg
