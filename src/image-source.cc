// -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
// (c) 2016-2021 Henner Zeller <h.zeller@acm.org>
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

#include "image-source.h"

// Various implementations we try in the constructor
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>
#include <sstream>
#include <utility>

#include "image-display.h"
#include "jpeg-source.h"
#include "openslide-source.h"
#include "qoi-image-source.h"
#include "stb-image-source.h"
#include "video-display.h"

namespace timg {

// Returns 'true' if image needs scaling.
bool ImageSource::CalcScaleToFitDisplay(int img_width, int img_height,
                                        const DisplayOptions &orig_options,
                                        bool fit_in_rotated, int *target_width,
                                        int *target_height) {
    DisplayOptions options = orig_options;
    if (fit_in_rotated) {
        std::swap(options.width, options.height);
        std::swap(options.fill_width, options.fill_height);
        options.width_stretch = 1.0f / orig_options.width_stretch;
    }

    // Clamp stretch to reasonable values.
    float width_stretch          = options.width_stretch;
    const float kMaxAcceptFactor = 5.0;  // Clamp to reasonable factor.
    if (width_stretch > kMaxAcceptFactor) width_stretch = kMaxAcceptFactor;
    if (width_stretch < 1 / kMaxAcceptFactor)
        width_stretch = 1 / kMaxAcceptFactor;

    if (width_stretch > 1.0f) {
        options.width /= width_stretch;  // pretend to have less space
    }
    else {
        options.height *= width_stretch;
    }
    const float width_fraction  = (float)options.width / img_width;
    const float height_fraction = (float)options.height / img_height;

    // If the image < screen, only upscale if do_upscale requested
    if (!options.upscale && (options.fill_height || width_fraction > 1.0) &&
        (options.fill_width || height_fraction > 1.0)) {
        *target_width  = img_width;
        *target_height = img_height;
        if (options.cell_x_px == 2) {
            // The quarter block feels a bit like good old EGA graphics
            // with some broken aspect ratio...
            *target_width *= 2;
            return true;
        }
        return false;
    }

    *target_width  = options.width;
    *target_height = options.height;

    if (options.fill_width && options.fill_height) {
        // Fill as much as we can get in available space.
        // Largest scale fraction determines that. This is for some diagonal
        // scroll modes.
        const float larger_fraction = (width_fraction > height_fraction)
                                          ? width_fraction
                                          : height_fraction;
        *target_width               = (int)roundf(larger_fraction * img_width);
        *target_height              = (int)roundf(larger_fraction * img_height);
    }
    else if (options.fill_height) {
        // Make things fit in vertical space.
        // While the height constraint stays the same, we can expand width to
        // wider than screen.
        *target_width = (int)roundf(height_fraction * img_width);
    }
    else if (options.fill_width) {
        // dito, vertical. Make things fit in horizontal, overflow vertical.
        *target_height = (int)roundf(width_fraction * img_height);
    }
    else {
        // Typical situation: whatever limits first
        const float smaller_fraction = (width_fraction < height_fraction)
                                           ? width_fraction
                                           : height_fraction;
        *target_width  = (int)roundf(smaller_fraction * img_width);
        *target_height = (int)roundf(smaller_fraction * img_height);
    }

    if (width_stretch > 1.0f) {
        *target_width *= width_stretch;
    }
    else {
        *target_height /= width_stretch;
    }

    // floor() to next full character cell size but only if we in one of
    // the block modes.
    if (options.cell_x_px > 0 && options.cell_x_px <= 2 &&
        options.cell_y_px > 0 && options.cell_y_px <= 2) {
        *target_width  = *target_width / options.cell_x_px * options.cell_x_px;
        *target_height = *target_height / options.cell_y_px * options.cell_y_px;
    }

    // Don't scale down to nothing...
    if (*target_width <= 0) *target_width = 1;
    if (*target_height <= 0) *target_height = 1;

    if (options.upscale_integer && *target_width > img_width &&
        *target_height > img_height) {
        // Correct for aspect ratio mismatch of quarter rendering.
        const float aspect_correct = options.cell_x_px == 2 ? 2 : 1;
        const float wf = 1.0f * *target_width / aspect_correct / img_width;
        const float hf = 1.0f * *target_height / img_height;
        const float smaller_factor = wf < hf ? wf : hf;
        if (smaller_factor > 1.0f) {
            *target_width  = aspect_correct * floor(smaller_factor) * img_width;
            *target_height = floor(smaller_factor) * img_height;
        }
    }

    return *target_width != img_width || *target_height != img_height;
}

ImageSource *ImageSource::Create(const std::string &filename,
                                 const DisplayOptions &options,
                                 int frame_offset, int frame_count,
                                 bool attempt_image_loading,
                                 bool attempt_video_loading,
                                 bool print_errors) {
    std::unique_ptr<ImageSource> result;
    if (attempt_image_loading) {
#ifdef WITH_TIMG_OPENSLIDE_SUPPORT
        result.reset(new OpenSlideSource(filename));
        if (result->LoadAndScale(options, frame_offset, frame_count)) {
            return result.release();
        }
#endif

#ifdef WITH_TIMG_QOI
        result.reset(new QOIImageSource(filename));
        if (result->LoadAndScale(options, frame_offset, frame_count)) {
            return result.release();
        }
#endif

#ifdef WITH_TIMG_JPEG
        result.reset(new JPEGSource(filename));
        if (result->LoadAndScale(options, frame_offset, frame_count)) {
            return result.release();
        }
#endif

#ifdef WITH_TIMG_GRPAPHICSMAGICK
        result.reset(new ImageLoader(filename));
        if (result->LoadAndScale(options, frame_offset, frame_count)) {
            return result.release();
        }
#endif

#ifdef WITH_TIMG_STB
        // STB image loading always last as last fallback resort.
        result.reset(new STBImageSource(filename));
        if (result->LoadAndScale(options, frame_offset, frame_count)) {
            return result.release();
        }
#endif
    }  // end attempt image loading

#ifdef WITH_TIMG_VIDEO
    if (attempt_video_loading) {
        result.reset(new VideoLoader(filename));
        if (result->LoadAndScale(options, frame_offset, frame_count)) {
            return result.release();
        }
    }  // end attempt video loading
#endif

    // Ran into trouble opening. Let's see if this is even an accessible file.
    if (filename != "-" && print_errors) {
        struct stat statresult;
        if (stat(filename.c_str(), &statresult) < 0) {
            fprintf(stderr, "%s: %s\n", filename.c_str(), strerror(errno));
        }
        else if (S_ISDIR(statresult.st_mode)) {
            fprintf(stderr, "%s: is a directory\n", filename.c_str());
        }
        else if (access(filename.c_str(), R_OK) < 0) {
            fprintf(stderr, "%s: %s\n", filename.c_str(), strerror(errno));
        }
    }

    // We either loaded, played and continue'ed, or we end up here.
    // fprintf(stderr, "%s: couldn't load\n", filename);
#ifdef WITH_TIMG_VIDEO
    if (filename == "-" || filename == "/dev/stdin") {
        fprintf(stderr,
                "If this is a video on stdin, use '-V' to "
                "skip image probing\n");
    }
#endif
    return nullptr;
}

static std::string Basename(const std::string &filename) {
    size_t last_slash_pos = filename.find_last_of("/\\");

    return last_slash_pos == std::string::npos
               ? filename
               : filename.substr(last_slash_pos + 1);
}

std::string ImageSource::FormatFromParameters(const std::string &fmt_string,
                                              const std::string &filename,
                                              int orig_width, int orig_height,
                                              const char *decoder) {
    std::stringstream result;
    for (size_t i = 0; i < fmt_string.length(); ++i) {
        if (fmt_string[i] != '%' || i >= fmt_string.length() - 1) {
            result << fmt_string[i];
            continue;
        }

        ++i;
        switch (fmt_string[i]) {
        case 'f': result << filename; break;
        case 'b': result << Basename(filename); break;
        case 'w': result << orig_width; break;
        case 'h': result << orig_height; break;
        case 'D': result << decoder; break;
        default: result << fmt_string[i]; break;
        }
    }

    return result.str();
}

static bool HasAPNGHeader(const std::string &filename) {
    // Somewhat handwavy: the "acTL" chunk could of course be at other places as
    // well, let's assume it is just after IHDR.
    int fd = open(filename.c_str(), O_RDONLY);
    if (fd < 0) return false;
    char actl_sig[4]                   = {};
    static constexpr int kPngHeaderLen = 8;
    static constexpr int kPngIHDRLen   = 8 + 13 + 4;
    const ssize_t len = pread(fd, actl_sig, 4, kPngHeaderLen + kPngIHDRLen + 4);
    close(fd);
    return len == 4 && memcmp(actl_sig, "acTL", 4) == 0;
}

bool ImageSource::LooksLikeAPNG(const std::string &filename) {
    const char *const file = filename.c_str();
    for (const char *ending : {".png", ".apng"}) {
        if (strcasecmp(file + strlen(file) - strlen(ending), ending) == 0 &&
            HasAPNGHeader(filename)) {
            return true;
        }
    }
    return false;
}

}  // namespace timg
