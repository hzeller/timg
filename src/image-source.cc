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
#include "image-display.h"
#include "video-display.h"

#include <string.h>
#include <math.h>

#include <utility>
#include <memory>

namespace timg {
// Returns 'true' if image needs scaling.
bool ImageSource::CalcScaleToFitDisplay(int img_width, int img_height,
                                        const DisplayOptions &orig_options,
                                        bool fit_in_rotated,
                                        int *target_width, int *target_height) {
    DisplayOptions options = orig_options;
    if (fit_in_rotated) {
        std::swap(options.width, options.height);
        std::swap(options.fill_width, options.fill_height);
    }

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

    // Don't scale down to nothing...
    if (*target_width <= 0) *target_width = 1;
    if (*target_height <= 0) *target_height = 1;

    return *target_width != img_width || *target_height != img_height;
}

ImageSource *ImageSource::Create(const std::string &filename,
                                 const DisplayOptions &options,
                                 int max_frames,
                                 bool attempt_image_loading,
                                 bool attempt_video_loading) {
    std::unique_ptr<ImageSource> result;
    if (attempt_image_loading) {
        result.reset(new ImageLoader(filename));
        if (result->LoadAndScale(options, max_frames)) {
            return result.release();
        }
    }

#ifdef WITH_TIMG_VIDEO
    if (attempt_video_loading) {
        result.reset(new VideoLoader(filename));
        if (result->LoadAndScale(options, max_frames)) {
            return result.release();
        }
    }
#endif

    // We either loaded, played and continue'ed, or we end up here.
    //fprintf(stderr, "%s: couldn't load\n", filename);
#ifdef WITH_TIMG_VIDEO
    if (filename == "-" || filename == "/dev/stdin") {
        fprintf(stderr, "If this is a video on stdin, use '-V' to "
                "skip image probing\n");
    }
#endif
    return nullptr;
}

}  // namespace timg
