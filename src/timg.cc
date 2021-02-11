// -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
// (c) 2016 Henner Zeller <h.zeller@acm.org>
//
// timg - a terminal image viewer.
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
// To compile this image viewer, first get image-magick development files
// $ sudo apt-get install libgraphicsmagick++-dev

#include "display-options.h"
#include "image-source.h"
#include "renderer.h"
#include "terminal-canvas.h"
#include "thread-pool.h"
#include "timg-time.h"
#include "timg-version.h"

// To display version number
#include "image-display.h"
#ifdef WITH_TIMG_VIDEO
#  include "video-display.h"
#endif


#include <getopt.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <thread>
#include <future>
#include <vector>
#include <Magick++.h>

#ifndef TIMG_VERSION
#  define TIMG_VERSION "(unknown)"
#endif

using timg::Duration;
using timg::Time;
using timg::ImageSource;
using timg::Framebuffer;

enum class ExitCode {
    kSuccess        = 0,
    kImageReadError = 1,
    kParameterError = 2,
    kNotATerminal   = 3,
    // Keep in sync with error codes mentioned in manpage
};

// Modern processors with some sort of hyperthreading don't seem to scale
// much beyond their physical core count when doing image processing.
// So just keep thread count at half what we get reported.
static const int kDefaultThreadCount =
    std::max(1U, std::thread::hardware_concurrency() / 2);

volatile sig_atomic_t interrupt_received = 0;
static void InterruptHandler(int signo) {
  interrupt_received = 1;
}

static int usage(const char *progname, ExitCode exit_code, int w, int h) {
#ifdef WITH_TIMG_VIDEO
    static constexpr char kFileType[] = "image/video";
#else
    static constexpr char kFileType[] = "image";
#endif
    fprintf(stderr, "usage: %s [options] <%s> [<%s>...]\n", progname,
            kFileType, kFileType);
    fprintf(stderr, "Options:\n"
            "\t-g<w>x<h>      : Output pixel geometry. Default from terminal %dx%d.\n"
            "\t-C, --center   : Center image horizontally.\n"
            "\t-W, --fit-width: Scale to fit width of available space, even if it exceeds height.\n"
            "\t                 (default: scale to fit inside available rectangle)\n"
            "\t--grid=<cols>[x<rows>] : Arrange images in a grid (contact sheet).\n"
            "\t-w<seconds>    : If multiple images given: Wait time between (default: 0.0).\n"
            "\t-a             : Switch off anti aliasing (default: on)\n"
            "\t-b<str>        : Background color to use on transparent images (default '').\n"
            "\t-B<str>        : Checkerboard pattern color to use on transparent images (default '').\n"
            "\t--auto-crop[=<pre-crop>] : Crop away all same-color pixels around image.\n"
            "\t                 The optional pre-crop is the width of border to\n"
            "\t                 remove beforehand to get rid of an uneven border.\n"
            "\t--rotate=<exif|off> : Rotate according to included exif orientation or off. Default: exif.\n"
            "\t-U             : Toggle Upscale. If an image is smaller (e.g. an icon) than the \n"
            "\t                 available frame, enlarge it to fit.\n"
#ifdef WITH_TIMG_VIDEO
            "\t-V             : Only use Video subsystem. Don't attempt to probe image decoding first.\n"
            "\t                 (useful, if you stream video from stdin).\n"
            "\t-I             : Only  use Image subsystem. Don't attempt video decoding.\n"
#endif
            "\t-F, --title    : Print filename as title above each image.\n"
            "\t-E             : Don't hide the cursor while showing images.\n"
            "\t--threads=<n>  : Run image decoding in parallel with n threads\n"
            "\t                 (Default %d, half #cores on this machine)\n"
            "\t--version      : Print version and exit.\n"
            "\t-h, --help     : Print this help and exit.\n"

            "\n  Scrolling\n"
            "\t--scroll=[<ms>]       : Scroll horizontally (optionally: delay ms (60)).\n"
            "\t--delta-move=<dx:dy>  : delta x and delta y when scrolling (default: 1:0).\n"

            "\n  For Animations, Scrolling, or Video\n"
            "  These options influence how long/often and what is shown.\n"
            "\t--loops=<num> : Number of runs through a full cycle. Use -1 to mean 'forever'.\n"
            "\t                If not set, videos loop once, animated images forever\n"
            "\t                unless there is more than one file to show (then: just once)\n"
            "\t--frames=<num>: Only show first num frames (if looping, loop only these)\n"
            "\t-t<seconds>   : Stop after this time, no matter what --loops or --frames say.\n",
            w, h, kDefaultThreadCount);
    return (int)exit_code;
}

static bool GetBoolenEnv(const char *env_name) {
    const char *const value = getenv(env_name);
    return value && atoi(value) != 0;
}

// Probe all file descriptors that might be connect to tty for term size.
struct TermSizeResult { bool size_valid; int width; int height; };
TermSizeResult DetermineTermSize() {
    for (int fd : { STDOUT_FILENO, STDERR_FILENO, STDIN_FILENO }) {
        struct winsize w = {};
        if (ioctl(fd, TIOCGWINSZ, &w) == 0) {
            return { true, w.ws_col, 2 * (w.ws_row-1) }; // pixels = 2*height
        }
    }
    return { false, -1, -1 };
}

int main(int argc, char *argv[]) {
    Magick::InitializeMagick(*argv);

    const TermSizeResult term = DetermineTermSize();
    const bool terminal_use_upper_block = GetBoolenEnv("TIMG_USE_UPPER_BLOCK");

    timg::DisplayOptions display_opts;
    display_opts.width = term.width;
    display_opts.height = term.height;

    bool hide_cursor = true;
    Duration duration = Duration::InfiniteFuture();
    Duration between_images_duration = Duration::Millis(0);
    int max_frames = timg::kNotInitialized;
    int loops  = timg::kNotInitialized;
    int grid_rows = 1;
    int grid_cols = 1;
    bool do_image_loading = true;
    bool do_video_loading = true;
    int thread_count = kDefaultThreadCount;

    enum LongOptionIds {
        OPT_FRAMES = 1000,
        OPT_GRID,
        OPT_ROTATE,
        OPT_THREADS,
        OPT_VERSION,
    };

    // Flags with optional parameters need to be long-options, as on MacOS,
    // there is no way to have single-character options with
    static constexpr struct option long_options[] = {
        { "auto-crop",   optional_argument, NULL, 'T' },
        { "center",      no_argument,       NULL, 'C' },
        { "delta-move",  required_argument, NULL, 'd' },
        { "fit-width",   no_argument,       NULL, 'W' },
        { "frames",      required_argument, NULL, OPT_FRAMES },
        { "grid",        required_argument, NULL, OPT_GRID },
        { "help",        no_argument,       NULL, 'h' },
        { "loops",       required_argument, NULL, 'c' },
        { "rotate",      required_argument, NULL, OPT_ROTATE },
        { "scroll",      optional_argument, NULL, 's' },
        { "threads",     required_argument, NULL, OPT_THREADS },
        { "title",       no_argument,       NULL, 'F' },
        { "version",     no_argument,       NULL, OPT_VERSION },
        // TODO: add more long-options
        { 0, 0, 0, 0}
    };
    // BSD's don't have a getopt() that has the GNU extension to allow
    // optional parameters on single-character flags, so we now only document
    // the new --trim and --scroll but, for a while, will also silently
    // support these old options.
#define OLD_COMPAT_FLAGS "T::s::"

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv,
                              "vg:w:t:c:f:b:B:hCFEd:UWaVI" OLD_COMPAT_FLAGS,
                              long_options, &option_index))!=-1) {
        switch (opt) {
        case 'g':
            if (sscanf(optarg, "%dx%d",
                       &display_opts.width, &display_opts.height) < 2) {
                fprintf(stderr, "Invalid size spec '%s'", optarg);
                return usage(argv[0], ExitCode::kParameterError,
                             term.width, term.height);
            }
            break;
        case 'w':
            between_images_duration
                = Duration::Millis(roundf(atof(optarg) * 1000.0f));
            break;
        case 't':
            duration = Duration::Millis(roundf(atof(optarg) * 1000.0f));
            break;
        case 'c':  // Legacy option, now long opt. Keep for now.
            loops = atoi(optarg);
            break;
        case OPT_FRAMES:
            max_frames = atoi(optarg);
            // TODO: also provide an option for frame offset ?
            break;
        case 'a':
            display_opts.antialias = false;
            break;
        case 'b':
            display_opts.bg_color = strdup(optarg);
            break;
        case 'B':
            display_opts.bg_pattern_color = strdup(optarg);
            break;
        case 's':
            display_opts.scroll_animation = true;
            if (optarg != NULL) {
                display_opts.scroll_delay = Duration::Millis(atoi(optarg));
            }
            break;
        case 'V':
#ifdef WITH_TIMG_VIDEO
            do_image_loading = false;
            do_video_loading = true;
#else
            fprintf(stderr, "-V: Video support not compiled in\n");
#endif
            break;
        case 'I':
            do_image_loading = true;
#if WITH_TIMG_VIDEO
            do_video_loading = false;
#endif
            break;
        case OPT_ROTATE:
            // TODO(hzeller): Maybe later also pass angles ?
            if (strcasecmp(optarg, "exif") == 0) {
                display_opts.exif_rotate = true;
            } else if (strcasecmp(optarg, "off") == 0) {
                display_opts.exif_rotate = false;
            } else {
                fprintf(stderr, "--rotate=%s: expected 'exif' or 'off'\n",
                        optarg);
                return usage(argv[0], ExitCode::kParameterError,
                             term.width, term.height);
            }
            break;
        case OPT_GRID:
            switch (sscanf(optarg, "%dx%d", &grid_cols, &grid_rows)) {
            case 0:
                fprintf(stderr, "Invalid grid spec '%s'", optarg);
                return usage(argv[0], ExitCode::kParameterError,
                             term.width, term.height);
            case 1:
                grid_rows = grid_cols;
                break;
            }
            break;
        case OPT_THREADS:
            thread_count = atoi(optarg);
            break;
        case 'd':
            if (sscanf(optarg, "%d:%d",
                       &display_opts.scroll_dx, &display_opts.scroll_dy) < 1) {
                fprintf(stderr, "--delta-move=%s: At least dx parameter needed"
                        " e.g. --delta-move=1."
                        "Or you can give dx, dy like so: -d1:-1", optarg);
                return usage(argv[0], ExitCode::kParameterError,
                             term.width, term.height);
            }
            break;
        case 'C':
            display_opts.center_horizontally = true;
            break;
        case 'U':
            display_opts.upscale = !display_opts.upscale;
            break;
        case 'T':
            display_opts.auto_crop = true;
            if (optarg) {
                display_opts.crop_border = atoi(optarg);
            }
            break;
        case 'F':
            display_opts.show_filename = !display_opts.show_filename;
            break;
        case 'E':
            hide_cursor = false;
            break;
        case 'W':
            display_opts.fill_width = true;
            break;
        case OPT_VERSION:
            fprintf(stderr, "timg " TIMG_VERSION
                    " <https://github.com/hzeller/timg>\n"
                    "Copyright (c) 2016.. Henner Zeller. "
                    "This program is free software; license GPL 2.0.\n\n");
            fprintf(stderr, "Image decoding %s\n",
                    timg::ImageLoader::VersionInfo());
#ifdef WITH_TIMG_VIDEO
            fprintf(stderr, "Video decoding %s\n",
                    timg::VideoLoader::VersionInfo());
#endif
            return 0;
        case 'h':
        default:
            return usage(argv[0], (opt == 'h'
                                   ? ExitCode::kSuccess
                                   : ExitCode::kParameterError),
                         term.width, term.height);
        }
    }

    if (display_opts.width < 1 || display_opts.height < 1) {
        if (!term.size_valid || term.height < 0 || term.width < 0) {
            fprintf(stderr, "Failed to read size from terminal; "
                    "Please supply -g<width>x<height> directly.\n");
        } else {
            fprintf(stderr, "%dx%d is a rather unusual size\n",
                    display_opts.width, display_opts.height);
        }
        return usage(argv[0], ExitCode::kNotATerminal, term.width, term.height);
    }

    const int provided_file_count = argc - optind;
    if (provided_file_count <= 0) {
        fprintf(stderr, "Expected image filename.\n");
        return usage(argv[0], ExitCode::kImageReadError,
                     term.width, term.height);
    }

    // -- Some sanity checks and configuration editing.
    // There is no scroll if there is no movement.
    if (display_opts.scroll_dx == 0 && display_opts.scroll_dy == 0) {
        fprintf(stderr, "Scrolling chosen, but dx:dy = 0:0. "
                "Just showing image, no scroll.\n");
        display_opts.scroll_animation = false;
    }

    // If we scroll in one direction (so have 'infinite' space) we want fill
    // the available screen space fully in the other direction.
    display_opts.fill_width  = display_opts.fill_width ||
        (display_opts.scroll_animation && display_opts.scroll_dy != 0);
    display_opts.fill_height = display_opts.scroll_animation &&
        display_opts.scroll_dx != 0; // scroll h, fill v

    // Showing exactly one frame implies animation behaves as static image
    if (max_frames == 1) {
        loops = 1;
    }

    if (provided_file_count > 1 && loops == timg::kNotInitialized) {
        loops = 1;  // Don't want to get stuck on the first endless-loop anim.
    }

    if (display_opts.show_filename) {
        display_opts.height -= 2*grid_rows;  // Leave space for text 2px = 1row
    }

    timg::TerminalCanvas canvas(STDOUT_FILENO, terminal_use_upper_block);
    if (hide_cursor) {
        canvas.CursorOff();
    }

    auto renderer = timg::Renderer::Create(&canvas, display_opts,
                                           grid_cols, grid_rows);

    // The image sources might need to write to a smaller area if grid.
    display_opts.width /= grid_cols;
    display_opts.height /= grid_rows;

    signal(SIGTERM, InterruptHandler);
    signal(SIGINT, InterruptHandler);
    ExitCode exit_code = ExitCode::kSuccess;

    // Async image loading, preparing them in a thread pool
    thread_count = (thread_count > 0 ? thread_count : kDefaultThreadCount);
    timg::ThreadPool pool(thread_count);
    std::vector<std::future<timg::ImageSource*>> loaded_sources;
    for (int imgarg = optind; imgarg < argc && !interrupt_received; ++imgarg) {
        const char *const filename = argv[imgarg];
        std::function<timg::ImageSource*()> f =
            [filename, do_image_loading, do_video_loading,
             &display_opts, &exit_code]() -> timg::ImageSource* {
                if (interrupt_received) return nullptr;
                auto result = ImageSource::Create(filename, display_opts,
                                                  do_image_loading,
                                                  do_video_loading);
                if (!result) exit_code = ExitCode::kImageReadError;
                return result;
            };
        loaded_sources.push_back(pool.ExecAsync(f));
    }

    // Showing them in order of files on the command line.
    for (auto &source_future : loaded_sources) {
        if (interrupt_received) break;
        std::unique_ptr<timg::ImageSource> source(source_future.get());
        if (!source) continue;
        source->SendFrames(duration, max_frames, loops, interrupt_received,
                           renderer->render_cb(source->filename().c_str()));
        if (!between_images_duration.is_zero()) {
            (Time::Now() + between_images_duration).WaitUntil();
        }
    }

    if (hide_cursor) {
        canvas.CursorOn();
    }
    if (interrupt_received)   // Make 'Ctrl-C' appear on new line.
        printf("\n");

    return (int)exit_code;
}
