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
#include "iterm2-canvas.h"
#include "kitty-canvas.h"
#include "renderer.h"
#include "terminal-canvas.h"
#include "termutils.h"
#include "thread-pool.h"
#include "timg-time.h"
#include "timg-version.h"
#include "unicode-block-canvas.h"
#include "buffered-write-sequencer.h"
// To display version number
#include "image-display.h"
#ifdef WITH_TIMG_VIDEO
#  include "video-display.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <Magick++.h>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifndef TIMG_VERSION
#  define TIMG_VERSION "(unknown)"
#endif

using timg::Duration;
using timg::Framebuffer;
using timg::ImageSource;
using timg::KittyGraphicsCanvas;
using timg::ITerm2GraphicsCanvas;
using timg::TerminalCanvas;
using timg::Time;
using timg::UnicodeBlockCanvas;
using timg::rgba_t;

enum class ExitCode {
    kSuccess         = 0,
    kImageReadError  = 1,
    kParameterError  = 2,
    kNotATerminal    = 3,
    kCantOpenOutput  = 4,
    kFilelistProblem = 5,
    // Keep in sync with error codes mentioned in manpage
};

enum class Pixelation {
    kNotChosen,
    kHalfBlock,
    kQuarterBlock,
    kKittyGraphics,
    kiTerm2Graphics,
};

enum class ClearScreen {
    kNot,
    kBeforeFirstImage,
    kBeforeEachImage,
};

namespace timg {
// Options configuring how images/videos are arranged and presented.
struct PresentationOptions {
    Duration duration_per_image = Duration::InfiniteFuture();
    int loops = timg::kNotInitialized;  // If animation: loop count through all
    int grid_cols = 1;                  // Grid arrangement
    int grid_rows = 1;
    bool hide_cursor = true;            // Hide cursor while emitting image
    ClearScreen clear_screen = ClearScreen::kNot; // Clear between images ?
    Duration duration_between_images;   // How long to wait between images
};
}  // namespace timg

// Image sources; as future as they are being filled while we start presenting
typedef std::vector<std::future<timg::ImageSource*>> LoadedImageSources;

// Modern processors with some sort of hyperthreading don't seem to scale
// much beyond their physical core count when doing image processing.
// So just keep thread count at half what we get reported.
static const int kDefaultThreadCount =
    std::max(1U, std::thread::hardware_concurrency() / 2);

volatile sig_atomic_t interrupt_received = 0;
static void InterruptHandler(int signo) {
  interrupt_received = 1;
}

static int usage(const char *progname, ExitCode exit_code,
                 int width, int height) {
#ifdef WITH_TIMG_VIDEO
    static constexpr char kFileType[] = "image/video";
#else
    static constexpr char kFileType[] = "image";
#endif
    fprintf(stderr, "usage: %s [options] <%s> [<%s>...]\n", progname,
            kFileType, kFileType);
    fprintf(stderr, "Options:\n"
            "\t-g<w>x<h>      : Output geometry in character cells. Default from terminal %dx%d.\n"
            "\t-p<pixelation> : Pixelation: 'h'=half blocks    'q'=quarter blocks\n"
            "\t                             'k'=kitty graphics 'i' = iTerm2 graphics\n"
            "\t                 Default: Auto-detect Kitty, iTerm2 or WezTerm otherwise 'quarter'\n"
            "\t--compress     : Only for -pk or -pi: PNG-compress image data before sending to\n"
            "\t                 terminal. More CPU use for timg, but less bandwidth needed.\n"
            "\t-C, --center   : Center image horizontally.\n"
            "\t-W, --fit-width: Scale to fit width of available space, even if it exceeds\n"
            "\t                 height. (default: scale to fit inside available rectangle)\n"
            "\t--grid=<cols>[x<rows>] : Arrange images in a grid (contact sheet).\n"
            "\t-w<seconds>    : If multiple images given: Wait time between (default: 0.0).\n"
            "\t-a             : Switch off anti aliasing (default: on)\n"
            "\t-b<str>        : Background color to use behind transparent images.\n"
            "\t                 format 'yellow', '#rrggbb' or 'auto' or 'none' (default 'auto').\n"
            "\t-B<str>        : Checkerboard pattern color to use on transparent (default '').\n"
            "\t--pattern-size=<n> : Integer factor scale of the checkerboard pattern\n"
            "\t--auto-crop[=<pre-crop>] : Crop away all same-color pixels around image.\n"
            "\t                 The optional pre-crop is the width of border to\n"
            "\t                 remove beforehand to get rid of an uneven border.\n"
            "\t--rotate=<exif|off> : Rotate according to included exif orientation or off.\n"
            "\t                      Default: exif.\n"
            "\t--clear        : Clear screen first. Optional argument 'every' will clean\n"
            "\t                 before every image (useful with -w)\n"
            "\t-U, --upscale[=i]: Allow Upscaling. If an image is smaller than the available\n"
            "\t                 frame (e.g. an icon), enlarge it to fit. Optional\n"
            "\t                 parameter 'i' only enlarges in sharp integer increments.\n"
#ifdef WITH_TIMG_VIDEO
            "\t-V             : Directly use Video subsystem. Don't probe image decoding first.\n"
            "\t                 (useful, if you stream video from stdin).\n"
            "\t-I             : Only  use Image subsystem. Don't attempt video decoding.\n"
#endif
            "\t-F, --title    : Print filename as title above each image.\n"
            "\t-f<filelist>   : Read newline-separated list of image files to show.\n"
            "\t                 (Can be provided multiple times.)\n"
            "\t-o<outfile>    : Write to <outfile> instead of stdout.\n"
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
            width, height, kDefaultThreadCount);
    return (int)exit_code;
}

// Read list of filenames from newline separated file. Non-absolute files
// are resolved relative to the filelist_file
bool AppendToFileList(const std::string &filelist_file,
                      std::vector<std::string> *filelist) {
    std::ifstream filelist_stream(
        filelist_file == "-" ? "/dev/stdin" : filelist_file, std::ifstream::in);
    if (!filelist_stream) {
        fprintf(stderr, "%s: %s\n", filelist_file.c_str(), strerror(errno));
        return false;
    }
    const size_t last_slash = filelist_file.find_last_of('/');
    // Following works as expected if last_slash == npos (lsat_slash+1 == 0)
    const std::string prefix = filelist_file.substr(0, last_slash + 1);
    std::string filename;
    for (std::string filename; std::getline(filelist_stream, filename); /**/) {
        if (filename.empty()) continue;
        if (filename[0] != '/' && !prefix.empty()) filename = prefix + filename;
        filelist->push_back(filename);
    }
    return true;
}

static void PresentImages(LoadedImageSources &loaded_sources,
                          const timg::DisplayOptions &display_opts,
                          const timg::PresentationOptions &present,
                          Pixelation pixelation, bool terminal_use_upper_block,
                          timg::BufferedWriteSequencer *sequencer) {
    std::unique_ptr<TerminalCanvas> canvas;
    switch (pixelation) {
    case Pixelation::kKittyGraphics:
        canvas.reset(new KittyGraphicsCanvas(sequencer, display_opts));
        break;
    case Pixelation::kiTerm2Graphics:
        canvas.reset(new ITerm2GraphicsCanvas(sequencer, display_opts));
        break;
    case Pixelation::kHalfBlock:
    case Pixelation::kQuarterBlock:
    case Pixelation::kNotChosen:  // Should not happen.
        canvas.reset(new UnicodeBlockCanvas(
                         sequencer, pixelation == Pixelation::kQuarterBlock,
                         terminal_use_upper_block));
    }

    auto renderer = timg::Renderer::Create(
        canvas.get(), display_opts,
        present.grid_cols, present.grid_rows);


    // Things to do before and after we show an image. Our goal is to keep
    // the terminal always in a good state (cursor on!) while also reacting
    // to Ctrl-C or terminations.
    // While showing an image we switch off the cursor but also arm the
    // signal handler to intercept and have a chance to bring terminal output
    // to a controlled stop.
    // Between showing images we _do_ want the default signal handler to be
    // active so that we can interrupt picture loading (because the internals
    // of the image loading libraries don' know about "interrupt_received".
    auto before_image_show = [present, &canvas](bool first) {
        signal(SIGTERM, InterruptHandler);
        signal(SIGINT, InterruptHandler);
        if (present.hide_cursor) canvas->CursorOff();
        if ((present.clear_screen == ClearScreen::kBeforeFirstImage && first) ||
            (present.clear_screen == ClearScreen::kBeforeEachImage)) {
            canvas->ClearScreen();
        }
    };

    auto after_image_show = [present, &canvas]() {
        if (present.hide_cursor) canvas->CursorOn();
        signal(SIGTERM, SIG_DFL);
        signal(SIGINT, SIG_DFL);
    };

    // Showing them in order of files on the command line.
    bool is_first = true;
    for (auto &source_future : loaded_sources) {
        if (interrupt_received) break;
        std::unique_ptr<timg::ImageSource> source(source_future.get());
        if (!source) continue;
        before_image_show(is_first);
        source->SendFrames(present.duration_per_image,
                           present.loops, interrupt_received,
                           renderer->render_cb(source->filename().c_str()));
        after_image_show();
        if (!present.duration_between_images.is_zero()) {
            (Time::Now() + present.duration_between_images).WaitUntil();
        }
        is_first = false;
    }
}

int main(int argc, char *argv[]) {
    Magick::InitializeMagick(*argv);

    bool verbose = false;
    const timg::TermSizeResult term = timg::DetermineTermSize();
    const bool terminal_use_upper_block =
        timg::GetBoolenEnv("TIMG_USE_UPPER_BLOCK");

    timg::DisplayOptions display_opts;
    timg::PresentationOptions presentation;

    const char *bg_color = "auto";
    const char *bg_pattern_color = nullptr;
    display_opts.allow_frame_skipping =
        timg::GetBoolenEnv("TIMG_ALLOW_FRAME_SKIP");

    int output_fd = STDOUT_FILENO;
    std::vector<std::string> filelist;  // from -f<filelist> and command line
    int frame_offset = 0;
    int max_frames = timg::kNotInitialized;
    bool do_img_loading = true;
    bool do_vid_loading = true;
    int thread_count = kDefaultThreadCount;
    int geometry_width = (term.cols - 2);
    int geometry_height = (term.rows - 2);
    Pixelation pixelation = Pixelation::kNotChosen;

    // Convenience predicate: pixelation sending high-res images, no blocks.
    const auto is_pixel_direct_p = [](Pixelation p) {
        return p==Pixelation::kKittyGraphics || p==Pixelation::kiTerm2Graphics;
    };

    enum LongOptionIds {
        OPT_CLEAR_SCREEN = 1000,
        OPT_COMPRESS_PIXEL,
        OPT_FRAME_COUNT,
        OPT_FRAME_OFFSET,
        OPT_GRID,
        OPT_PATTERN_SIZE,
        OPT_ROTATE,
        OPT_THREADS,
        OPT_VERBOSE,
        OPT_VERSION,
    };

    // Flags with optional parameters need to be long-options, as on MacOS,
    // there is no way to have single-character options with
    static constexpr struct option long_options[] = {
        { "auto-crop",   optional_argument, NULL, 'T' },
        { "center",      no_argument,       NULL, 'C' },
        { "clear",       optional_argument, NULL, OPT_CLEAR_SCREEN },
        { "compress",    no_argument,       NULL, OPT_COMPRESS_PIXEL },
        { "delta-move",  required_argument, NULL, 'd' },
        { "experimental-frame-offset",required_argument, NULL, OPT_FRAME_OFFSET },
        { "fit-width",   no_argument,       NULL, 'W' },
        { "frames",      required_argument, NULL, OPT_FRAME_COUNT },
        { "grid",        required_argument, NULL, OPT_GRID },
        { "help",        no_argument,       NULL, 'h' },
        { "loops",       optional_argument, NULL, 'c' },
        { "pattern-size", required_argument,NULL, OPT_PATTERN_SIZE },
        { "pixelation",  required_argument, NULL, 'p' },
        { "rotate",      required_argument, NULL, OPT_ROTATE },
        { "scroll",      optional_argument, NULL, 's' },
        { "threads",     required_argument, NULL, OPT_THREADS },
        { "title",       no_argument,       NULL, 'F' },
        { "upscale",     optional_argument, NULL, 'U' },
        { "verbose",     no_argument,       NULL, OPT_VERBOSE },
        { "version",     no_argument,       NULL, OPT_VERSION },
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
                              "vg:w:t:c:f:b:B:hCFEd:UWaVIo:f:p:"
                              OLD_COMPAT_FLAGS,
                              long_options, &option_index))!=-1) {
        switch (opt) {
        case 'g':
            if (sscanf(optarg, "%dx%d",
                       &geometry_width, &geometry_height) < 2) {
                fprintf(stderr, "Invalid size spec '%s'", optarg);
                return usage(argv[0], ExitCode::kParameterError,
                             geometry_width, geometry_height);
            }
            break;
        case 'w':
            presentation.duration_between_images
                = Duration::Millis(roundf(atof(optarg) * 1000.0f));
            break;
        case 't':
            presentation.duration_per_image
                = Duration::Millis(roundf(atof(optarg) * 1000.0f));
            if (presentation.duration_per_image.is_zero()) {
                fprintf(stderr, "Note, -t<zero-duration> will effectively "
                        "skip animations/movies\n");
            }
            break;
        case 'c':  // Legacy option, now long opt. Keep for now.
            // No parameter --loop essentially defaults to loop forever.
            presentation.loops = optarg ? atoi(optarg) : -1;
            break;
        case OPT_CLEAR_SCREEN:
            if (optarg) {
                const int optlen = strlen(optarg);
                if (optlen <= 5 && strncasecmp(optarg, "every", optlen) == 0)
                    presentation.clear_screen = ClearScreen::kBeforeEachImage;
                else {
                    fprintf(stderr, "Paramter for --clear can be 'every', "
                            "got %s\n", optarg);
                    return usage(argv[0], ExitCode::kParameterError,
                                 geometry_width, geometry_height);
                }
            } else {
                presentation.clear_screen = ClearScreen::kBeforeFirstImage;
            }
            break;
        case OPT_FRAME_OFFSET:
            frame_offset = atoi(optarg);
            break;
        case OPT_FRAME_COUNT:
            max_frames = atoi(optarg);
            break;
        case 'a':
            display_opts.antialias = false;
            break;
        case 'b':
            bg_color = strdup(optarg);
            break;
        case 'B':
            bg_pattern_color = strdup(optarg);
            break;
        case OPT_PATTERN_SIZE:
            display_opts.pattern_size = atoi(optarg);
            break;
        case 's':
            display_opts.scroll_animation = true;
            if (optarg != NULL) {
                display_opts.scroll_delay = Duration::Millis(atoi(optarg));
            }
            break;
        case 'V':
#ifdef WITH_TIMG_VIDEO
            do_img_loading = false;
            do_vid_loading = true;
#else
            fprintf(stderr, "-V: Video support not compiled in\n");
#endif
            break;
        case 'I':
            do_img_loading = true;
#if WITH_TIMG_VIDEO
            do_vid_loading = false;
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
                             geometry_width, geometry_height);
            }
            break;
        case OPT_GRID:
            switch (sscanf(optarg, "%dx%d",
                           &presentation.grid_cols, &presentation.grid_rows)) {
            case 0:
                fprintf(stderr, "Invalid grid spec '%s'", optarg);
                return usage(argv[0], ExitCode::kParameterError,
                             geometry_width, geometry_height);
            case 1:
                presentation.grid_rows = presentation.grid_cols;
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
                             geometry_width, geometry_height);
            }
            break;
        case 'C':
            display_opts.center_horizontally = true;
            break;
        case 'U':
            display_opts.upscale = !display_opts.upscale;
            display_opts.upscale_integer = (optarg && optarg[0] == 'i');
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
            presentation.hide_cursor = false;
            break;
        case 'W':
            display_opts.fill_width = true;
            break;
        case OPT_VERSION:
            fprintf(stderr, "timg " TIMG_VERSION
                    " <https://timg.sh/>\n"
                    "Copyright (c) 2016..2021 Henner Zeller. "
                    "This program is free software; license GPL 2.0.\n\n");
            fprintf(stderr, "Image decoding %s\n",
                    timg::ImageLoader::VersionInfo());
#ifdef WITH_TIMG_VIDEO
            fprintf(stderr, "Video decoding %s\n",
                    timg::VideoLoader::VersionInfo());
#endif
            return 0;
        case 'o':
            output_fd = open(optarg, O_WRONLY|O_CREAT|O_TRUNC, 0664);
            if (output_fd < 0) {
                fprintf(stderr, "%s: %s\n", optarg, strerror(errno));
                return usage(argv[0], ExitCode::kCantOpenOutput,
                             geometry_width, geometry_height);
            }
            break;
        case 'f':
            if (!AppendToFileList(optarg, &filelist)) {
                return usage(argv[0], ExitCode::kFilelistProblem,
                             geometry_width, geometry_height);
            }
            break;
        case 'p':
            switch (optarg[0]) {  // Just looking at first character sufficient
            case 'h': case 'H': pixelation = Pixelation::kHalfBlock; break;
            case 'q': case 'Q': pixelation = Pixelation::kQuarterBlock; break;
            case 'k': case 'K': pixelation = Pixelation::kKittyGraphics; break;
            case 'i': case 'I': pixelation = Pixelation::kiTerm2Graphics; break;
            }
            break;
        case OPT_COMPRESS_PIXEL:
            display_opts.compress_pixel_format = true;
            break;
        case OPT_VERBOSE:
            verbose = true;
            break;
        case 'h':
        default:
            return usage(argv[0], (opt == 'h'
                                   ? ExitCode::kSuccess
                                   : ExitCode::kParameterError),
                         geometry_width, geometry_height);
        }
    }

    // -- A sieve of sanity checks and configuration refinement.

    if (geometry_width < 1 || geometry_height < 1) {
        if (term.cols < 0 || term.rows < 0) {
            fprintf(stderr, "Failed to read size from terminal; "
                    "Please supply -g<width>x<height> directly.\n");
        } else {
            fprintf(stderr, "%dx%d is a rather unusual size\n",
                    geometry_width, geometry_height);
        }
        return usage(argv[0], ExitCode::kNotATerminal,
                     geometry_width, geometry_height);
    }

    if ((term.font_width_px < 0 || term.font_height_px < 0) &&
        is_pixel_direct_p(pixelation)) {
        // Best effort mode if someone requests graphics protocol, but
        // we don't know cell size in pixels.
        //
        // Either they want to create some output for another terminal, or
        // they are on a terminal that supports the graphics protocol but
        // not much else.
        //
        // For instance, Chromium Secure Shell (hterm) extension supports
        // iTerm2 graphics, but unfortunately does not report size :(
        fprintf(stderr, "Terminal does not support pixel size query, "
                "but graphics protocol requested.\n"
                "Can't show animations or have columns in grid.\n");
        max_frames = 1;   // Since don't know how many cells move up next frame
        // We need a cell size to have something to scale the image into.
        display_opts.cell_x_px = 9;  // Make up some typical values.
        display_opts.cell_y_px = 18;
        // hterm does _not_ support PNM, always convert to PNG.
        display_opts.compress_pixel_format = true;
        // Because we don't know how much to move up and right. Also, hterm
        // does not seem to place an image in X-direction in the first place.
        presentation.grid_cols = 1;
    }

    // Determine best default to pixelate images.
    if (pixelation == Pixelation::kNotChosen) {
        pixelation = Pixelation::kQuarterBlock;  // Good default.
        // Konsole has the bad behaviour that it does not absorb the kitty
        // graphics query but spills it on the screen. "Luckily", Konsole has
        // another bug not returning the window pixel size, so we can use that
        // to avoid the query :)
        if (term.font_width_px > 0 && term.font_height_px > 0) {
            if (timg::QueryHasITerm2Graphics())
                pixelation = Pixelation::kiTerm2Graphics;
            else if (timg::QueryHasKittyGraphics())
                pixelation = Pixelation::kKittyGraphics;
        }
    }

    // If 'none' is chosen for background color, then using the
    // PNG compression with alpha channels gives us compositing on client side
    if (is_pixel_direct_p(pixelation) && strcasecmp(bg_color, "none") == 0) {
        display_opts.compress_pixel_format = true;
        display_opts.local_alpha_handling = false;
    }

    // If we're using block graphics, we might need to adapt the aspect ratio
    // slightly depending if the font-cell has a 1:2 ratio.
    // Terminals using direct pixels don't need this.
    const float stretch_correct = is_pixel_direct_p(pixelation)
        ? 1.0f
        : 0.5f * term.font_height_px / term.font_width_px;
    display_opts.width_stretch = timg::GetFloatEnv("TIMG_FONT_WIDTH_CORRECT",
                                                   stretch_correct);

    switch (pixelation) {
    case Pixelation::kHalfBlock:
        display_opts.cell_x_px = 1;
        display_opts.cell_y_px = 2;
        break;
    case Pixelation::kQuarterBlock:
        display_opts.width_stretch *= 2;
        display_opts.cell_x_px = 2;
        display_opts.cell_y_px = 2;
        break;
    case Pixelation::kKittyGraphics:
    case Pixelation::kiTerm2Graphics:
        if (term.font_width_px>0)  display_opts.cell_x_px = term.font_width_px;
        if (term.font_height_px>0) display_opts.cell_y_px = term.font_height_px;
        break;
    case Pixelation::kNotChosen:
        break;  // Should not happen. Was set above.
    }
    display_opts.width = geometry_width * display_opts.cell_x_px;
    display_opts.height = geometry_height * display_opts.cell_y_px;

    for (int imgarg = optind; imgarg < argc && !interrupt_received; ++imgarg) {
        filelist.push_back(argv[imgarg]);
    }

    if (filelist.size() <= 0) {
        fprintf(stderr, "Expected image filename(s) on command line "
                "or via -f\n");
        return usage(argv[0], ExitCode::kImageReadError,
                     geometry_width, geometry_height);
    }

    // There is no scroll if there is no movement.
    if (display_opts.scroll_dx == 0 && display_opts.scroll_dy == 0) {
        fprintf(stderr, "Scrolling chosen, but dx:dy = 0:0. "
                "Just showing image, no scroll.\n");
        display_opts.scroll_animation = false;
    }

    if (presentation.clear_screen == ClearScreen::kBeforeEachImage &&
        (presentation.grid_cols != 1 || presentation.grid_rows != 1)) {
        // Clear every only makes sense with no grid.
        presentation.clear_screen = ClearScreen::kBeforeFirstImage;
    }

    // If we scroll in one direction (so have 'infinite' space) we want fill
    // the available screen space fully in the other direction.
    display_opts.fill_width  = display_opts.fill_width ||
        (display_opts.scroll_animation && display_opts.scroll_dy != 0);
    display_opts.fill_height = display_opts.scroll_animation &&
        display_opts.scroll_dx != 0; // scroll h, fill v

    // Showing exactly one frame implies animation behaves as static image
    if (max_frames == 1) {
        presentation.loops = 1;
    }

    // If nothing is set to limit animations but we have multiple images,
    // set some sensible limit.
    if (filelist.size() > 1 && presentation.loops == timg::kNotInitialized
        && presentation.duration_between_images == Duration::InfiniteFuture()) {
        presentation.loops = 1;  // Don't get stuck on the first endless-loop
    }

    if (display_opts.show_filename) {
        // Leave space for text.
        display_opts.height -= display_opts.cell_y_px * presentation.grid_rows;
    }

    // Asynconrous image loading (filelist.size()) and terminal query (+1)
    thread_count = (thread_count > 0 ? thread_count : kDefaultThreadCount);
    auto pool = new timg::ThreadPool(std::min(thread_count,
                                              (int)filelist.size() + 1));

    std::future<rgba_t> background_color_future;
    if (bg_color) {
        if (strcasecmp(bg_color, "auto") == 0) {
            std::function<rgba_t()> query_terminal = []() {
                return rgba_t::ParseColor(timg::DetermineBackgroundColor());
            };
            background_color_future = pool->ExecAsync(query_terminal);
            display_opts.bgcolor_getter = [&background_color_future]() {
                static rgba_t value = background_color_future.get(); // once
                return value;
            };
        }
        else {
            const rgba_t bg = rgba_t::ParseColor(bg_color);
            display_opts.bgcolor_getter = [bg]() { return bg; };
        }
    }

    display_opts.bg_pattern_color = rgba_t::ParseColor(bg_pattern_color);

    // In a grid, we have less space per picture.
    display_opts.width /= presentation.grid_cols;
    display_opts.height /= presentation.grid_rows;

    ExitCode exit_code = ExitCode::kSuccess;

    // Async image loading, preparing them in a thread pool
    LoadedImageSources loaded_sources;
    for (const std::string &filename : filelist) {
        if (interrupt_received) break;
        std::function<timg::ImageSource*()> f =
            [filename, frame_offset, max_frames, do_img_loading, do_vid_loading,
             &display_opts, &exit_code]() -> timg::ImageSource* {
                if (interrupt_received) return nullptr;
                auto result = ImageSource::Create(filename, display_opts,
                                                  frame_offset, max_frames,
                                                  do_img_loading,
                                                  do_vid_loading);
                if (!result) exit_code = ExitCode::kImageReadError;
                return result;
            };
        loaded_sources.push_back(pool->ExecAsync(f));
    }

    static constexpr int kAsyncWriteQueueSize = 3;
    // Since Unicode blocks emit differences, we can't skip frames in output.
    const bool buffer_allow_skipping = (display_opts.allow_frame_skipping &&
                                        is_pixel_direct_p(pixelation));
    timg::BufferedWriteSequencer sequencer(output_fd,
                                           buffer_allow_skipping,
                                           kAsyncWriteQueueSize,
                                           interrupt_received);
    const Time start_show = Time::Now();
    PresentImages(loaded_sources, display_opts, presentation,
                  pixelation, terminal_use_upper_block,
                  &sequencer);
    sequencer.Flush();
    const Time end_show = Time::Now();

    if (interrupt_received) {
        // Even though we completed the write, some terminals sometimes seem
        // to get messed up, maybe interrupted escape sequence ?
        // Make sure to move to the very bottom and also reset attributes.
        // But do it on stderr, to not send it to a potentially redirected fd.
        fprintf(stderr, "\033[0m\033[%dB\n", term.rows);
        fflush(stderr);
    }

    if (verbose) {
        fprintf(stderr, "Terminal cells: %dx%d  cell-pixels: %dx%d\n",
                term.cols, term.rows, term.font_width_px, term.font_height_px);
        const Duration d = end_show - start_show;
        const uint64_t written_bytes =
            sequencer.bytes_total() - sequencer.bytes_skipped();
        fprintf(stderr,
                "%d file%s; %s written (%s/s) "
                "%" PRId64 " frames",
                (int)filelist.size(), filelist.size() == 1 ? "" : "s",
                timg::HumanReadableByteValue(written_bytes).c_str(),
                timg::HumanReadableByteValue(written_bytes / d).c_str(),
                sequencer.frames_total());
        // Only show FPS if we have one video or animation
        if (filelist.size() == 1 && sequencer.frames_total() > 100) {
            fprintf(stderr, "; %.1ffps", sequencer.frames_total()/d);
        }
        if (display_opts.allow_frame_skipping && sequencer.frames_total() > 0) {
            fprintf(stderr, " (%" PRId64 " skipped, %.1f%%)\n",
                    sequencer.frames_skipped(),
                    100.0 * sequencer.frames_skipped()
                    /sequencer.frames_total());
        }
        fprintf(stderr, "\n");
    }

    // If we were super-fast decoding and showing images that didn't need
    // transparency, the query might still be running. Wait a tiny bit so
    // that the terminal does not spill the result on the screen once we've
    // returned. If the result is already there, this will return immediately
    if (background_color_future.valid())
        background_color_future.wait_for(std::chrono::milliseconds(200));

    // Deliberately leaking thread pool as we don't bother waiting for
    // lingering threads

    return (int)exit_code;
}
