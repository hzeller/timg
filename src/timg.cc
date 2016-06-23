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

#define TIMG_VERSION "0.9.0"

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
  interrupt_received = true;
}

static int64_t GetTimeInMillis() {
    struct timeval tp;
    gettimeofday(&tp, NULL);
    return tp.tv_sec * 1000 + tp.tv_usec / 1000;
}

static void SleepMillis(long milli_seconds) {
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

// Frame already prepared as the buffer to be sent over so that animation
// and re-sizing operations don't have to be done online. Also knows about the
// animation delay.
class PreprocessedFrame {
public:
    PreprocessedFrame(const Magick::Image &img, int w, int h)
        : content_(new TerminalCanvas(w, h)) {
        int delay_time = img.animationDelay();  // in 1/100s of a second.
        if (delay_time < 1) delay_time = 1;
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
                              bool fit_height,
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

    const int img_width = (int)(*result)[0].columns();
    const int img_height = (int)(*result)[0].rows();
    if (fit_height) {
        // In case of scrolling, we only want to scale down to height
        target_width = (int) (1.0*img_width * target_height / img_height);
    }
    for (size_t i = 0; i < result->size(); ++i) {
        (*result)[i].scale(Magick::Geometry(target_width, target_height));
    }

    return true;
}

void DisplayAnimation(const std::vector<Magick::Image> &image_sequence,
                      time_t end_time, int loops, int w, int h, int fd) {
    std::vector<PreprocessedFrame*> frames;
    // Convert to preprocessed frames.
    for (size_t i = 0; i < image_sequence.size(); ++i) {
        frames.push_back(new PreprocessedFrame(image_sequence[i], w, h));
    }

    bool is_first = true;
    if (frames.size() == 1)
        loops = 1;   // If there is no animation, nothing to repeat.
    for (int k = 0; k < loops && !interrupt_received && time(NULL) < end_time;
         ++k) {
        for (unsigned int i = 0; i < frames.size() && !interrupt_received; ++i) {
            if (time(NULL) > end_time)
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

void DisplayScrolling(const Magick::Image &img, int scroll_delay_ms,
                      time_t end_time, int loops, int w, int h, int fd) {
    TerminalCanvas display(w, h);
    const int scroll_dir = scroll_delay_ms < 0 ? -1 : 1;
    const int initial_pos = scroll_dir < 0 ? img.columns()-display.width() : 0;
    if (scroll_delay_ms < 0)
        scroll_delay_ms = -scroll_delay_ms;
    bool is_first = true;

    // Accessing the original image in a loop is very slow with the ImageMagic
    // access, so we preprocess this first.
    struct RGBCol { RGBCol() : r(0), g(0), b(0){} uint8_t r, g, b; };
    RGBCol *fast_image = new RGBCol[ img.columns() * img.rows() ];
    for (size_t y = 0; y < img.rows(); ++y) {
        for (size_t x = 0; x < img.columns(); ++x) {
            const Magick::Color &src = img.pixelColor(x, y);
            if (src.alphaQuantum() >= 255)
                continue;
            RGBCol &dest = fast_image[y * img.columns() + x];
            dest.r = ScaleQuantumToChar(src.redQuantum());
            dest.g = ScaleQuantumToChar(src.greenQuantum());
            dest.b = ScaleQuantumToChar(src.blueQuantum());
        }
    }

    for (int k = 0; k < loops && !interrupt_received && time(NULL) <= end_time;
         ++k) {
        for (size_t start = 0; start < img.columns(); ++start) {
            if (interrupt_received) break;
            const int64_t start_time = GetTimeInMillis();
            for (int y = 0; y < display.height(); ++y) {
                for (int x = 0; x < display.width(); ++x) {
                    const int x_source = (x + scroll_dir*start + initial_pos
                                          + img.columns()) % img.columns();
                    const RGBCol &c = fast_image[y * img.columns() + x_source];
                    display.SetPixel(x, y, c.r, c.g, c.b);
                }
            }
            display.Send(fd, !is_first);
            is_first = false;
            const int64_t elapsed = GetTimeInMillis() - start_time;
            const int64_t remaining_wait_ms = scroll_delay_ms - elapsed;
            if (remaining_wait_ms > 0) SleepMillis(remaining_wait_ms);
        }
    }

    delete [] fast_image;
}


static int usage(const char *progname, int w, int h) {
    fprintf(stderr, "usage: %s [options] <image> [<image>...]\n", progname);
    fprintf(stderr, "Options:\n"
            "\t-g<w>x<h>  : Output pixel geometry. Default from terminal %dx%d\n"
            "\t-s[<ms>]   : Scroll horizontally (optionally: delay ms (60)).\n"
            "\t-t<timeout>: Animation or scrolling: only display for this number of seconds.\n"
            "\t-c<num>    : Animation or scrolling: number of runs through a full cycle.\n"
            "\t-C         : Clear screen first before display.\n"
            "\t-F         : Print filename before showing picture.\n"
            "\t-v         : Print version and exit.\n"
            "If both -c and -t are given, whatever comes first stops.\n",
            w, h);
    return 1;
}

int main(int argc, char *argv[]) {
    Magick::InitializeMagick(*argv);

    struct winsize w;
    ioctl(0, TIOCGWINSZ, &w);
    const int term_width = w.ws_col - 1;       // Keep right black edge.
    const int term_height = 2 * (w.ws_row-1);  // double number of pixels high.

    bool do_scroll = false;
    bool show_filename = false;
    int width = term_width;
    int height = term_height;
    int scroll_delay_ms = 50;
    time_t  timeout = 100000000;  // "infinity"; make sure end fits in 32bit
    int loops       = 100000000;  // "infinity"

    int opt;
    while ((opt = getopt(argc, argv, "vg:s::t:c:hCF")) != -1) {
        switch (opt) {
        case 'g':
            if (sscanf(optarg, "%dx%d", &width, &height) < 2) {
                fprintf(stderr, "Invalid size spec '%s'", optarg);
                return usage(argv[0], term_width, term_height);
            }
            break;
        case 't':
            timeout = atoi(optarg);
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
        case 'C':
            TerminalCanvas::ClearScreen(STDOUT_FILENO);
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
        fprintf(stderr, "%dx%d is a rather unusual size\n", width, height);
        return usage(argv[0], term_width, term_height);
    }

    if (optind >= argc) {
        fprintf(stderr, "Expected image filename.\n");
        return usage(argv[0], term_width, term_height);
    }

    signal(SIGTERM, InterruptHandler);
    signal(SIGINT, InterruptHandler);

    for (int imgarg = optind; imgarg < argc && !interrupt_received; ++imgarg) {
        const char *filename = argv[imgarg];
        if (show_filename) {
            printf("%s\n", filename);
        }

        std::vector<Magick::Image> frames;
        if (!LoadImageAndScale(filename, width, height, do_scroll, &frames)) {
            continue;
        }

        if (do_scroll && frames.size() > 1) {
            fprintf(stderr, "This is an animated image format, "
                    "scrolling on top of that is not supported.\n");
            continue;
        }

        // Adjust width/height after scaling.
        const int display_width = std::min(width, (int)frames[0].columns());
        const int display_height = std::min(height, (int)frames[0].rows());

        TerminalCanvas::CursorOff(STDOUT_FILENO);
        const time_t end_time = time(NULL) + timeout;
        if (do_scroll) {
            DisplayScrolling(frames[0], scroll_delay_ms,
                             end_time, loops, display_width, display_height,
                             STDOUT_FILENO);
        } else {
            DisplayAnimation(frames, end_time, loops,
                             display_width, display_height, STDOUT_FILENO);
        }
        TerminalCanvas::CursorOn(STDOUT_FILENO);

    }

    if (interrupt_received)   // Make 'Ctrl-C' appear on new line.
        printf("\n");

    return 0;
}
