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

#ifndef TIMG_PNG_H
#define TIMG_PNG_H

// This implements a simple and fast PNG encoder https://w3.org/TR/png/

#include <stdint.h>
#include <unistd.h>

namespace timg {
class Framebuffer;

namespace png {
// Encode framebuffer as PNG into given buffer and return encoded size.
//
// Provided buffer needs to be large enough; use UpperSizeEstimate() to prepare.
//
// "compression_level" is the compression level; 0 means essentially plain
// bytes without compression, 1 and more compresses. For our use-case probably
// only 1 is ever needed (we want to be fast).
//
// The ColorEncoding enum requests if 24Bit RGB or full 32Bit RGBA is encoded.
enum class ColorEncoding {
    kRGBA_32,
    kRGB_24,
};
size_t Encode(const Framebuffer &fb, int compression_level,
              ColorEncoding encoding, char *buffer, size_t size);

// Return estimate of maximum size needed to encode image of given size.
size_t UpperBound(int width, int height);

}  // namespace png
}  // namespace timg
#endif  // TIMG_PNG_H
