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

#include "timg-print-version.h"

#include <cstdio>

// Includes to display version numbers
#ifdef WITH_TIMG_OPENSLIDE_SUPPORT
#include "openslide-source.h"
#endif
#ifdef WITH_TIMG_VIDEO
#include "video-source.h"
#endif
#ifdef WITH_TIMG_GRPAPHICSMAGICK
#include "graphics-magick-source.h"
#endif
#ifdef WITH_TIMG_SIXEL
#include <sixel.h>
#endif
#ifdef WITH_TIMG_RSVG
#include <cairo-version.h>
#include <librsvg/rsvg.h>
#endif
#ifdef WITH_TIMG_POPPLER
#include <cairo-version.h>
#include <poppler.h>
#endif

#ifdef WITH_TIMG_JPEG
#include <jconfig.h>
#endif
#include <libdeflate.h>

#ifdef WITH_TIMG_SWS_RESIZE
#include <libswscale/swscale.h>  // NOLINT used for version.
#endif

#include "timg-version.h"  // generated.
#ifndef TIMG_VERSION
#define TIMG_VERSION "(unknown)"
#endif

namespace timg {
const char *timgVersion() { return TIMG_VERSION; }

int PrintComponentVersions(FILE *stream) {
    // Print our version and various version numbers from our dependencies.
    fprintf(stream, "timg " TIMG_VERSION
                    " <https://timg.sh/>\n"
                    "Copyright (c) 2016..2024 Henner Zeller. "
                    "This program is free software; license GPL 2.0.\n\n");
#ifdef WITH_TIMG_GRPAPHICSMAGICK
    fprintf(stream, "Image decoding %s\n",
            timg::GraphicsMagickSource::VersionInfo());
#endif
#ifdef WITH_TIMG_OPENSLIDE_SUPPORT
    fprintf(stream, "Openslide %s\n", timg::OpenSlideSource::VersionInfo());
#endif
#ifdef WITH_TIMG_JPEG
    fprintf(stream, "Turbo JPEG ");
#ifdef LIBJPEG_TURBO_VERSION
#define jpeg_xstr(s) jpeg_str(s)
#define jpeg_str(s)  #s
    fprintf(stream, "%s", jpeg_xstr(LIBJPEG_TURBO_VERSION));
#endif
    fprintf(stream, "\n");
#endif
#ifdef WITH_TIMG_RSVG
    fprintf(stream, "librsvg %d.%d.%d + cairo %d.%d.%d\n",
            LIBRSVG_MAJOR_VERSION, LIBRSVG_MINOR_VERSION, LIBRSVG_MICRO_VERSION,
            CAIRO_VERSION_MAJOR, CAIRO_VERSION_MINOR, CAIRO_VERSION_MICRO);
#endif
#ifdef WITH_TIMG_POPPLER
    fprintf(stream, "PDF rendering with poppler %s + cairo %d.%d.%d",
            poppler_get_version(), CAIRO_VERSION_MAJOR, CAIRO_VERSION_MINOR,
            CAIRO_VERSION_MICRO);
#if not POPPLER_CHECK_VERSION(0, 88, 0)
    // Too old versions of poppler don't have a bounding-box function
    fprintf(stream, " (no --auto-crop)");
#endif
    fprintf(stream, "\n");
#endif
#ifdef WITH_TIMG_QOI
    fprintf(stream, "QOI image loading\n");
#endif
#ifdef WITH_TIMG_STB
    fprintf(stream,
            "STB image loading"

#ifdef WITH_TIMG_GRPAPHICSMAGICK
            // If we have graphics magic, that will take images first,
            // so STB will only really be called as fallback.
            " (fallback)"
#endif
            "\n");
#endif

#ifdef WITH_TIMG_SWS_RESIZE
    fprintf(stream, "Resize: swscale %s\n", AV_STRINGIFY(LIBSWSCALE_VERSION));
#else
    fprintf(stream, "Resize: STB resize\n");
#endif

#ifdef WITH_TIMG_VIDEO
    fprintf(stream, "Video decoding %s\n", timg::VideoSource::VersionInfo());
#endif
#ifdef WITH_TIMG_SIXEL
    fprintf(stream, "Libsixel version %s\n", LIBSIXEL_VERSION);
#endif
    fprintf(stream, "libdeflate %s\n", LIBDEFLATE_VERSION_STRING);
    fprintf(stream,
            "Half, quarter, iterm2, and kitty graphics output: "
            "timg builtin.\n");
    return 0;
}
}  // namespace timg
