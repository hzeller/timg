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

#ifndef TIMG_IMAGE_SCALER_H
#define TIMG_IMAGE_SCALER_H

#include <memory>

#include "framebuffer.h"

namespace timg {
class ImageScaler {
public:
    enum class ColorFmt {
        kRGBA,
        kRGB32,
    };
    virtual ~ImageScaler() {}

    // Create an image scaler implementation depending on compile-time choices.
    static std::unique_ptr<ImageScaler> Create(int in_width, int in_height,
                                               ColorFmt in_color_format,
                                               int out_width, int out_height);

    // Scale an image stored in data put result into output framebuffer.
    // Output framebuffer must be sized to hold result.
    virtual void Scale(Framebuffer &in, Framebuffer *out) = 0;
};
}  // namespace timg
#endif  // TIMG_IMAGE_SCALER_H
