// -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
// (c) 2020 Henner Zeller <h.zeller@acm.org>
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
//
// Header named timg-base64.h to avoid potential clashes with headers on system.

#ifndef TIMG_BASE64_H_
#define TIMG_BASE64_H_

#include <stdint.h>

namespace timg {
// Encode data as base64.
// input_iterator "begin" yields chars, output_iterator "out" receives chars.
// State of output iterator after end of encoding is returned.
template <typename input_iterator, typename output_iterator>
inline output_iterator EncodeBase64(input_iterator begin, int input_len,
                                    output_iterator out) {
    static constexpr char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (/**/; input_len >= 3; input_len -= 3) {
        uint8_t b0 = *begin;
        ++begin;
        uint8_t b1 = *begin;
        ++begin;
        uint8_t b2 = *begin;
        ++begin;
        *out++ = b64[(b0 >> 2) & 0x3f];
        *out++ = b64[((b0 & 0x03) << 4) | ((int)(b1 & 0xf0) >> 4)];
        *out++ = b64[((b1 & 0x0f) << 2) | ((int)(b2 & 0xc0) >> 6)];
        *out++ = b64[b2 & 0x3f];
    }
    if (input_len > 0) {
        uint8_t b0 = *begin;
        ++begin;
        uint8_t b1 = input_len > 1 ? *begin : 0;
        *out++     = b64[(b0 >> 2) & 0x3f];
        *out++     = b64[((b0 & 0x03) << 4) | ((int)(b1 & 0xf0) >> 4)];
        *out++     = input_len > 1 ? b64[((b1 & 0x0f) << 2)] : '=';
        *out++     = '=';
    }
    return out;
}
}  // namespace timg

#endif  // TIMG_BASE64_H_
