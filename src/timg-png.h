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

#include <stdint.h>
#include <unistd.h>

namespace timg {
class Framebuffer;

// Encode framebuffer as PNG into given buffer.
// "compression_level" is the compression level; 0 means essentially plain
// bytes without compression, 1 and more compresses. For our use-case probably
// only 1 is ever needed (we want to be fast)
//
// If "do_32_bit_alpha" is enabled, then RGBA data is encoded, otherwise
// 24 bit RGB.
enum class ColorEncoding {
    kRGBA_32,
    kRGB_24,
};
size_t EncodePNG(const Framebuffer &fb, int compression_level,
                 ColorEncoding encoding, char *buffer, size_t size);

}  // namespace timg
#endif  // TIMG_PNG_H
