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
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>
#include <Magick++.h>
#include <magick/image.h>
#include <vector>

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
  interrupt_received = true;
}

static int64_t GetTimeInUsec() {
    struct timeval tp;
    gettimeofday(&tp, NULL);
    return tp.tv_sec * 1000000 + tp.tv_usec;
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
        delay_micros_ = delay_time * 10000;
        CopyToCanvas(img, content_);
    }
    ~PreprocessedFrame() { delete content_; }

    void Send(int fd) { content_->Send(fd); }
    int delay_micros() const { return delay_micros_; }

private:
    TerminalCanvas *content_;
    int delay_micros_;
};

// Load still image or animation.
// Scale, so that it fits in "width" and "height" and store in "result".
static bool LoadImageAndScale(const char *filename,
                              int target_width, int target_height,
                              bool fit_height,
                              std::vector<Magick::Image> *result) {
    std::vector<Magick::Image> frames;
    readImages(&frames, filename);
    if (frames.size() == 0) {
        fprintf(stderr, "No image found.");
        return false;
    }

    // Put together the animation from single frames. GIFs can have nasty
    // disposal modes, but they are handled nicely by coalesceImages()
    if (frames.size() > 1) {
        fprintf(stderr, "Assembling animation with %d frames.\n",
                (int)frames.size());
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
                      time_t end_time, int w, int h, int fd) {
    std::vector<PreprocessedFrame*> frames;
    // Convert to preprocessed frames.
    for (size_t i = 0; i < image_sequence.size(); ++i) {
        frames.push_back(new PreprocessedFrame(image_sequence[i], w, h));
    }

    for (unsigned int i = 0; !interrupt_received; ++i) {
        if (time(NULL) > end_time)
            break;
        if (i == frames.size()) i = 0;
        PreprocessedFrame *frame = frames[i];
        frame->Send(fd);  // Simple. just send it.
        if (frames.size() == 1) {
            return;  // We are done.
        } else {
            usleep(frame->delay_micros());
        }
    }
    // Leaking PreprocessedFrames. Don't care.
}

void DisplayScrolling(const Magick::Image &img, int scroll_delay_ms,
                      time_t end_time, TerminalCanvas *display,
                      int fd) {
    const uint64_t scroll_time_usec = 1000LL * scroll_delay_ms;
    while (!interrupt_received && time(NULL) <= end_time) {
        for (size_t start = 0; start < img.columns(); ++start) {
            if (interrupt_received) break;
            const int64_t start_time = GetTimeInUsec();
            for (int y = 0; y < display->height(); ++y) {
                for (int x = 0; x < display->width(); ++x) {
                    const Magick::Color &c =
                        img.pixelColor((x + start) % img.columns(), y);
                    if (c.alphaQuantum() >= 255)
                        continue;
                    display->SetPixel(x, y,
                                      ScaleQuantumToChar(c.redQuantum()),
                                      ScaleQuantumToChar(c.greenQuantum()),
                                      ScaleQuantumToChar(c.blueQuantum()));
                }
            }
            display->Send(fd);
            const int64_t elapsed = GetTimeInUsec() - start_time;
            int64_t remaining_wait = scroll_time_usec - elapsed;
            if (remaining_wait > 0) usleep(remaining_wait);
        }
    }
}


static int usage(const char *progname, int w, int h) {
    fprintf(stderr, "usage: %s [options] <image>\n", progname);
    fprintf(stderr, "Options:\n"
            "\t-g<w>x<h>  : Output pixel geometry. Default from terminal %dx%d\n"
            "\t-s[<ms>]   : Scroll horizontally (optionally: delay ms (60)).\n"
            "\t-t<timeout>: Animation or scrolling: only display for this number of seconds.\n",
            w, h);
    return 1;
}

int main(int argc, char *argv[]) {
    Magick::InitializeMagick(*argv);

    struct winsize w;
    ioctl(0, TIOCGWINSZ, &w);
    const int term_width = w.ws_col;
    const int term_height = 2 * (w.ws_row-1);  // double number of pixels high.

    bool do_scroll = false;
    int width = term_width;
    int height = term_height;
    int scroll_delay_ms = 50;
    int timeout = 1000000;
    
    int opt;
    while ((opt = getopt(argc, argv, "g:s::t:h")) != -1) {
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
        case 's':
            do_scroll = true;
            if (optarg != NULL) {
                scroll_delay_ms = atoi(optarg);
                if (scroll_delay_ms < 5) scroll_delay_ms = 5;
                if (scroll_delay_ms > 999) scroll_delay_ms = 999;
            }
            break;
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

    const char *filename = argv[optind];

    std::vector<Magick::Image> frames;
    if (!LoadImageAndScale(filename, width, height, do_scroll, &frames)) {
        return 1;
    }

    if (do_scroll && frames.size() > 1) {
        fprintf(stderr, "This is an animated image format, "
                "scrolling does not make sense\n");
        return 1;
    }

    signal(SIGTERM, InterruptHandler);
    signal(SIGINT, InterruptHandler);

    // Adjust width/height after scaling.
    width = std::min(width, (int)frames[0].columns());
    height = std::min(height, (int)frames[0].rows());
    TerminalCanvas display(width, height);

    TerminalCanvas::GlobalInit(STDOUT_FILENO);
    const time_t end_time = time(NULL) + timeout;
    if (do_scroll) {
        DisplayScrolling(frames[0], scroll_delay_ms, end_time, &display,
                         STDOUT_FILENO);
    } else {
        DisplayAnimation(frames, end_time, width, height, STDOUT_FILENO);
    }

    TerminalCanvas::GlobalFinish(STDOUT_FILENO);

    if (interrupt_received)   // Make 'Ctrl-C' appear on new line.
        printf("\n");

    return 0;
}
