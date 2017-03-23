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

#include "terminal-canvas.h"

#include <assert.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>

#include <Magick++.h>

#include <vector>

#define TIMG_VERSION "0.9.1beta"

volatile sig_atomic_t interrupt_received = 0;
static void InterruptHandler(int signo) {
  interrupt_received = 1;
}

typedef int64_t tmillis_t;

static tmillis_t GetTimeInMillis() {
    struct timeval tp;
    gettimeofday(&tp, NULL);
    return tp.tv_sec * 1000 + tp.tv_usec / 1000;
}

static void SleepMillis(tmillis_t milli_seconds) {
    if (milli_seconds <= 0) return;
    struct timespec ts;
    ts.tv_sec = milli_seconds / 1000;
    ts.tv_nsec = (milli_seconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

void CopyToCanvas(const Magick::Image &img, TerminalCanvas *result) {
    assert(result->width() >= (int) img.columns()
           && result->height() >= (int) img.rows());
    for (size_t y = 0; y < img.rows(); ++y) {
        for (size_t x = 0; x < img.columns(); ++x) {
            const Magick::Color &c = img.pixelColor(x, y);
            if (c.alphaQuantum() >= 255)
                continue;
            result->SetPixel(x, y,
                             ScaleQuantumToChar(c.redQuantum()),
                             ScaleQuantumToChar(c.greenQuantum()),
                             ScaleQuantumToChar(c.blueQuantum()));
        }
    }
}

// Frame already prepared as the buffer to be sent, so copy to terminal-buffer
// does not have to be done online. Also knows about the animation delay.
class PreprocessedFrame {
public:
    PreprocessedFrame(const Magick::Image &img, int w, int h)
        : content_(new TerminalCanvas(w, h)) {
        int delay_time = img.animationDelay();  // in 1/100s of a second.
        if (delay_time < 1) delay_time = 10;
        delay_millis_ = delay_time * 10;
        CopyToCanvas(img, content_);
    }
    ~PreprocessedFrame() { delete content_; }

    void Send(int fd, bool jump_back) { content_->Send(fd, jump_back); }
    int delay_millis() const { return delay_millis_; }

private:
    TerminalCanvas *content_;
    int delay_millis_;
};

// Load still image or animation.
// Scale, so that it fits in "width" and "height" and store in "result".
static bool LoadImageAndScale(const char *filename,
                              int target_width, int target_height,
                              bool fill_width, bool fill_height,
                              std::vector<Magick::Image> *result) {
    std::vector<Magick::Image> frames;
    try {
        readImages(&frames, filename);
    } catch (std::exception& e) {
        fprintf(stderr, "Trouble loading %s (%s)\n", filename, e.what());
        return false;
    }
    if (frames.size() == 0) {
        fprintf(stderr, "No image found.");
        return false;
    }

    // Put together the animation from single frames. GIFs can have nasty
    // disposal modes, but they are handled nicely by coalesceImages()
    if (frames.size() > 1) {
        Magick::coalesceImages(result, frames.begin(), frames.end());
    } else {
        result->push_back(frames[0]);   // just a single still image.
    }

    const int img_width = (*result)[0].columns();
    const int img_height = (*result)[0].rows();
    const float width_fraction = (float)target_width / img_width;
    const float height_fraction = (float)target_height / img_height;
    if (fill_width && fill_height) {
        // Scrolling diagonally. Fill as much as we can get in available space.
        // Largest scale fraction determines that.
        const float larger_fraction = (width_fraction > height_fraction)
            ? width_fraction
            : height_fraction;
        target_width = (int) roundf(larger_fraction * img_width);
        target_height = (int) roundf(larger_fraction * img_height);
    }
    else if (fill_height) {
        // Horizontal scrolling: Make things fit in vertical space.
        // While the height constraint stays the same, we can expand to full
        // width as we scroll along that axis.
        target_width = (int) roundf(height_fraction * img_width);
    }
    else if (fill_width) {
        // dito, vertical. Make things fit in horizontal space.
        target_height = (int) roundf(width_fraction * img_height);
    }

    for (size_t i = 0; i < result->size(); ++i) {
        (*result)[i].scale(Magick::Geometry(target_width, target_height));
    }

    return true;
}

void DisplayAnimation(const std::vector<Magick::Image> &image_sequence,
                      tmillis_t duration_ms, int loops, int w, int h, int fd) {
    std::vector<PreprocessedFrame*> frames;
    // Convert to preprocessed frames.
    for (size_t i = 0; i < image_sequence.size(); ++i) {
        frames.push_back(new PreprocessedFrame(image_sequence[i], w, h));
    }

    const tmillis_t end_time_ms = GetTimeInMillis() + duration_ms;
    bool is_first = true;
    if (frames.size() == 1)
        loops = 1;   // If there is no animation, nothing to repeat.
    for (int k = 0;
         (loops < 0 || k < loops)
             && !interrupt_received
             && GetTimeInMillis() < end_time_ms;
         ++k) {
        for (unsigned int i = 0; i < frames.size() && !interrupt_received; ++i) {
            if (interrupt_received || GetTimeInMillis() > end_time_ms)
                break;
            PreprocessedFrame *frame = frames[i];
            frame->Send(fd, !is_first);  // Simple. just send it.
            is_first = false;
            SleepMillis(frame->delay_millis());
        }
    }
    for (size_t i = 0; i < frames.size(); ++i) {
        delete frames[i];
    }
}

static int gcd(int a, int b) { return b == 0 ? a : gcd(b, a % b); }

void DisplayScrolling(const Magick::Image &img, int scroll_delay_ms,
                      tmillis_t duration_ms, int loops, int w, int h,
                      int dx, int dy, int fd) {
    TerminalCanvas display(w, h);
    const int img_width = img.columns();
    const int img_height = img.rows();

    // Since the user can choose the number of cycles we go through it,
    // we need to calculate what the minimum number of steps is we need
    // to do the scroll. If this is just in one direction, that is simple: the
    // number of pixel in that direction. If we go diagonal, then it is
    // essentially the least common multiple of steps.
    const int x_steps = (dx == 0)
        ? 1
        : ((img_width % abs(dx) == 0) ? img_width / abs(dx) : img_width);
    const int y_steps = (dy == 0)
        ? 1
        : ((img_height % abs(dy) == 0) ? img_height / abs(dy) : img_height);
    const int64_t cycle_steps =  x_steps * y_steps / gcd(x_steps, y_steps);

    // Depending if we go forward or backward, we want to start out aligned
    // right or left.
    // For negative direction, guarantee that we never run into negative numbers.
    const int64_t x_init = (dx < 0)
        ? (img_width - display.width() - dx*cycle_steps) : 0;
    const int64_t y_init = (dy < 0)
        ? (img_height - display.height() - dy*cycle_steps) : 0;
    bool is_first = true;

    // Accessing the original image in a loop is very slow with ImageMagic, so
    // we preprocess this first and create our own fast copy.
    struct RGBCol { RGBCol() : r(0), g(0), b(0){} uint8_t r, g, b; };
    RGBCol *fast_image = new RGBCol[ img_width * img_height ];
    for (int y = 0; y < img_height; ++y) {
        for (int x = 0; x < img_width; ++x) {
            const Magick::Color &src = img.pixelColor(x, y);
            if (src.alphaQuantum() >= 255)
                continue;
            RGBCol &dest = fast_image[y * img_width + x];
            dest.r = ScaleQuantumToChar(src.redQuantum());
            dest.g = ScaleQuantumToChar(src.greenQuantum());
            dest.b = ScaleQuantumToChar(src.blueQuantum());
        }
    }

    const tmillis_t end_time_ms = GetTimeInMillis() + duration_ms;
    for (int k = 0;
         (loops < 0 || k < loops)
             && !interrupt_received
             && GetTimeInMillis() < end_time_ms;
         ++k) {
        for (int64_t cycle_pos = 0; cycle_pos <= cycle_steps; ++cycle_pos) {
            if (interrupt_received || GetTimeInMillis() > end_time_ms)
                break;
            const int64_t x_cycle_pos = dx*cycle_pos;
            const int64_t y_cycle_pos = dy*cycle_pos;
            for (int y = 0; y < display.height(); ++y) {
                for (int x = 0; x < display.width(); ++x) {
                    const int x_src = (x_init + x_cycle_pos + x) % img_width;
                    const int y_src = (y_init + y_cycle_pos + y) % img_height;
                    const RGBCol &c = fast_image[y_src * img_width + x_src];
                    display.SetPixel(x, y, c.r, c.g, c.b);
                }
            }
            display.Send(fd, !is_first);
            is_first = false;
            SleepMillis(scroll_delay_ms);
        }
    }

    delete [] fast_image;
}


static int usage(const char *progname, int w, int h) {
    fprintf(stderr, "usage: %s [options] <image> [<image>...]\n", progname);
    fprintf(stderr, "Options:\n"
            "\t-g<w>x<h>  : Output pixel geometry. Default from terminal %dx%d\n"
            "\t-s[<ms>]   : Scroll horizontally (optionally: delay ms (60)).\n"
            "\t-d<dx:dy>  : delta x and delta y when scrolling (default: 1:0).\n"
            "\t-w<seconds>: If multiple images given: Wait time between (default: 0.0).\n"
            "\t-t<seconds>: Only animation or scrolling: stop after this time.\n"
            "\t-c<num>    : Only Animation or scrolling: number of runs through a full cycle.\n"
            "\t-C         : Clear screen before showing image.\n"
            "\t-F         : Print filename before showing picture.\n"
            "\t-v         : Print version and exit.\n"
            "If both -c and -t are given, whatever comes first stops.\n"
            "If both -w and -t are given for some animation/scroll, -t "
            "takes precedence\n",
            w, h);
    return 1;
}

int main(int argc, char *argv[]) {
    Magick::InitializeMagick(*argv);

    struct winsize w = {0, 0};
    const bool winsize_success = (ioctl(1, TIOCGWINSZ, &w) == 0);
    const int term_width = w.ws_col - 1;       // Space for right black edge.
    const int term_height = 2 * (w.ws_row-1);  // double number of pixels high.

    bool do_scroll = false;
    bool do_clear = false;
    bool show_filename = false;
    int width = term_width;
    int height = term_height;
    int scroll_delay_ms = 50;
    tmillis_t duration_ms = (1LL<<40);  // that is a while.
    tmillis_t wait_ms = 0;
    int loops  = -1;
    int dx = 1;
    int dy = 0;

    int opt;
    while ((opt = getopt(argc, argv, "vg:s::w:t:c:hCFd:")) != -1) {
        switch (opt) {
        case 'g':
            if (sscanf(optarg, "%dx%d", &width, &height) < 2) {
                fprintf(stderr, "Invalid size spec '%s'", optarg);
                return usage(argv[0], term_width, term_height);
            }
            break;
        case 'w':
            wait_ms = roundf(atof(optarg) * 1000.0f);
            break;
        case 't':
            duration_ms = roundf(atof(optarg) * 1000.0f);
            break;
        case 'c':
            loops = atoi(optarg);
            break;
        case 's':
            do_scroll = true;
            if (optarg != NULL) {
                scroll_delay_ms = atoi(optarg);
            }
            break;
        case 'd':
            if (sscanf(optarg, "%d:%d", &dx, &dy) < 1) {
                fprintf(stderr, "-d%s: At least dx paramter needed e.g. -d1."
                        "Or you can give dx, dy like so: -d1:-1", optarg);
                return usage(argv[0], term_width, term_height);
            }
            break;
        case 'C':
            do_clear = true;
            break;
        case 'F':
            show_filename = !show_filename;
            break;
        case 'v':
            fprintf(stderr, "timg " TIMG_VERSION
                    " <https://github.com/hzeller/timg>\n"
                    "Copyright (c) 2016 Henner Zeller. "
                    "This program is free software; license GPL 2.0.\n");
            return 0;
        case 'h':
        default:
            return usage(argv[0], term_width, term_height);
        }
    }

    if (width < 1 || height < 1) {
        if (!winsize_success || term_height < 0 || term_width < 0) {
            fprintf(stderr, "Failed to read size from terminal; "
                    "Please supply -g<width>x<height> directly.\n");
        } else {
            fprintf(stderr, "%dx%d is a rather unusual size\n", width, height);
        }
        return usage(argv[0], term_width, term_height);
    }

    if (optind >= argc) {
        fprintf(stderr, "Expected image filename.\n");
        return usage(argv[0], term_width, term_height);
    }

    // There is no scroll if there is no movement.
    if (dx == 0 && dy == 0) {
        fprintf(stderr, "Scrolling chosen, but dx:dy = 0:0. "
                "Just showing image, no scroll.\n");
        do_scroll = false;
    }

    // If we scroll in one direction (so have 'infinite' space) we want fill
    // the available screen space fully in the other direction.
    const bool fill_width  = do_scroll && dy != 0; // scroll vert, fill hor
    const bool fill_height = do_scroll && dx != 0; // scroll hor, fill vert

    signal(SIGTERM, InterruptHandler);
    signal(SIGINT, InterruptHandler);

    for (int imgarg = optind; imgarg < argc && !interrupt_received; ++imgarg) {
        const char *filename = argv[imgarg];
        if (show_filename) {
            printf("%s\n", filename);
        }

        std::vector<Magick::Image> frames;
        if (!LoadImageAndScale(filename, width, height,
                               fill_width, fill_height, &frames)) {
            continue;
        }

        if (do_scroll && frames.size() > 1) {
            fprintf(stderr, "This is an animated image format, "
                    "scrolling on top of that is not supported. "
                    "Just doing the scrolling of the first frame.\n");
            // TODO: do both.
        }

        // Adjust width/height after scaling.
        const int display_width = std::min(width, (int)frames[0].columns());
        const int display_height = std::min(height, (int)frames[0].rows());

        if (do_clear) {
            TerminalCanvas::ClearScreen(STDOUT_FILENO);
        }
        TerminalCanvas::CursorOff(STDOUT_FILENO);
        if (do_scroll) {
            DisplayScrolling(frames[0], scroll_delay_ms, duration_ms, loops,
                             display_width, display_height, dx, dy,
                             STDOUT_FILENO);
        } else {
            DisplayAnimation(frames, duration_ms, loops,
                             display_width, display_height, STDOUT_FILENO);
            if (frames.size() == 1 && wait_ms > 0)
                SleepMillis(wait_ms);
        }
        TerminalCanvas::CursorOn(STDOUT_FILENO);
    }

    if (interrupt_received)   // Make 'Ctrl-C' appear on new line.
        printf("\n");

    return 0;
}
