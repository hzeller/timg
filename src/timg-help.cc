// -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
// (c) 2023 Henner Zeller <h.zeller@acm.org>
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

#include "timg-help.h"

#include <libdeflate.h>
#include <stdlib.h>

// Build  from create-manpage-inc.sh
#include "timg-manpage.inc"

void InvokeHelpPager() {
    libdeflate_decompressor *const decompress = libdeflate_alloc_decompressor();

    // TODO(hzeller): include in the inc-file the original size.
    char uncompressed[1 << 16];
    size_t uncompressed_size = 0;
    libdeflate_result result = libdeflate_gzip_decompress(
        decompress, kGzippedManpage, sizeof(kGzippedManpage), uncompressed,
        sizeof(uncompressed), &uncompressed_size);

    libdeflate_free_decompressor(decompress);
    if (result == LIBDEFLATE_SUCCESS) {
        setenv("LESS", "-R", 1);  // Output terminal escape codes.
        FILE *myout = popen("${PAGER:-less}", "w");
        fwrite(uncompressed, 1, uncompressed_size, myout);
        pclose(myout);
    }
}
