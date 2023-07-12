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

#include <string>

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
    // can be called on, or nullptr if file can not be opened.
    // In case of an error, the error message is filled into "error" out param.
    static ImageSource *Create(const std::string &filename,
                               const DisplayOptions &options, int frame_offset,
                               int frames_count, bool attempt_image_loading,
                               bool attempt_video_loading, std::string *error);

    virtual ~ImageSource() {}

    // Send preprocessed frames for a maximum of given, max frames and loops,
    // whatever comes first. Stop loop when "interrupt_received" is true.
    // Send all frames to "sink", a callback that accepts Framebuffers.
    virtual void SendFrames(const Duration &duration, int loops,
                            const volatile sig_atomic_t &interrupt_received,
                            const Renderer::WriteFramebufferFun &sink) = 0;

    // Format title according to the format-string.
    virtual std::string FormatTitle(const std::string &format_string) const = 0;

    // Return filename, this ImageSource has loaded.
    const std::string &filename() const { return filename_; }

    // FYI if this image source is a multi-frame animation, e.g. if it
    // would require to skip up to the beginning, independent if this was
    // limited by frames_count (Context: Issue #86)
    virtual bool IsAnimationBeforeFrameLimit() const { return false; }

protected:
    explicit ImageSource(const std::string &filename) : filename_(filename) {}

    // Attempt to load image(s) from filename and prepare for display.
    // Images are processed using the parameters in DisplayOptions.
    // Return 'true' if successful. Only then, it is allowed to call
    // SendFrames().
    // Implementations of this will be called by the ImageSource::Create
    // factory.
    virtual bool LoadAndScale(const DisplayOptions &options, int frame_offset,
                              int frame_count) = 0;

    // Utility function that derived ImageSources are interested in.
    //
    // Given an image with size "img_width" and "img_height", determine the
    // target width and height satisfying the desired fit and size defined in
    // the "display_options" and if it should "fit_in_rotated_frame" of that
    // display options defined frame.
    //
    // As result, modifies "target_width" and "target_height";
    // returns 'true' if the image has to be scaled, i.e. target size
    // is different than image size.
    static bool CalcScaleToFitDisplay(int img_width, int img_height,
                                      const DisplayOptions &display_options,
                                      bool fit_in_rotated_frame,
                                      int *target_width, int *target_height);

    // Utility function to format
    static std::string FormatFromParameters(const std::string &fmt_string,
                                            const std::string &filename,
                                            int orig_width, int orig_height,
                                            const char *decoder);

    // Utility function to determine if a file is an APNG picture. Used
    // by various sources to choose to pass it on to the video subsystem.
    static bool LooksLikeAPNG(const std::string &filename);

protected:
    const std::string filename_;
};
}  // namespace timg

#endif  // IMAGE_SOURCE_H_
