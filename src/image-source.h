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

#ifndef IMAGE_SOURCE_H_
#define IMAGE_SOURCE_H_

#include <signal.h>

#include "display-options.h"
#include "renderer.h"
#include "timg-time.h"

namespace timg {
// Base class for everything providing image content.
// Construction is provided by the factory.
class ImageSource {
public:
    // Factory that creates an image source, trying all available
    // implementations until one can deal with the filename.
    // Returns a fully LoadAndScale()'d ImageSource that SendFrame()
    // can be called on, or nullptr if file can not be opened
    static ImageSource *Create(const char *filename,
                               const DisplayOptions &options,
                               bool attempt_image_loading,
                               bool attempt_video_loading);

    virtual ~ImageSource() {}

    // Send preprocessed frames for a maximum of given, max frames and loops,
    // whatever comes first. Stop loop when "interrupt_received" is true.
    // Send all frames to "sink", a callback that accepts Framebuffers.
    virtual void SendFrames(Duration duration, int max_frames, int loops,
                            const volatile sig_atomic_t &interrupt_received,
                            const Renderer::WriteFramebufferFun &sink) = 0;

    // Return filename, this ImageSource has loaded.
    const std::string& filename() const { return filename_; }

protected:
    ImageSource(const char *filename) : filename_(filename) {}

    // Attempt to load image(s) from filename and prepare for display.
    // Images are processed using the parameters in DisplayOptions.
    // Return 'true' if successful. Only then, it is allowed to call
    // SendFrames().
    // Implementations of this will be called by the ImageSource::Create
    // factory.
    virtual bool LoadAndScale(const DisplayOptions &options) = 0;


    // Utility function that derived ImageSources are interested in.
    //
    // Given an image with size "img_width" and "img_height", determine the
    // target width and height satisfying the desired fit and size defined in
    // the "display_options".
    //
    // As result, modifies "target_width" and "target_height";
    // returns 'true' if the image has to be scaled, i.e. target size
    // is different than image size.
    static bool CalcScaleToFitDisplay(int img_width, int img_height,
                                      const DisplayOptions &display_options,
                                      int *target_width, int *target_height);
protected:
    const std::string filename_;
};
}  // namespace timg

#endif  // IMAGE_SOURCE_H_
