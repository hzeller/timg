// -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
// (c) 2016 Henner Zeller <h.zeller@acm.org>
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

#include "terminal-canvas.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#define SCREEN_CLEAR    "\033c"
#define SCREEN_POSTFIX  "\033[0m"           // reset terminal settings
#define SCREEN_CURSOR_UP_FORMAT "\033[%dA"  // Move cursor up given lines.
#define CURSOR_OFF      "\033[?25l"

// Interestingly, cursor-on does not take effect until the next newline on
// the tested terminals. Not sure why that is, but adding a newline sounds
// like waste of vertical space, so let's not do it here but rather try
// to understand the actual reason why this is happening and fix it then.
#define CURSOR_ON       "\033[?25h"

// Each character on the screen is divided in a top pixel and bottom pixel.
// We use the following character which fills the top block:
#define PIXEL_CHARACTER  "▀"  // Top foreground color, bottom background color

// Now, pixels on the even row will get the foreground color changed, pixels
// on odd rows the background color. Two pixels one stone. Or something.
#define ESCAPE_COLOR_TEMPLATE "rrr;ggg;bbb"
#define TOP_PIXEL_COLOR       "38;2;"
#define BOTTOM_PIXEL_COLOR    "48;2;"

static void reliable_write(int fd, const char *buf, size_t size) {
    int written;
    while (size && (written = write(fd, buf, size)) > 0) {
        size -= written;
        buf += written;
    }
}

TerminalCanvas::TerminalCanvas(int w, int h)
    // Internal width is one pixel wider to have a black right edge, otherwise
    //   terminals generate strange artifacts when they start scrolling.
    // Height is rounded up to the next even number.
    : internal_width_(w+1), height_(h), any_change_(true) {
    pixels_ = new uint32_t [ internal_width_ * (height_ + 1)];
    memset(pixels_, 0, sizeof(uint32_t) * internal_width_ * (height_ + 1));

    // Preallocate buffer that we use to assemble the output before writing.
    const int max_pixel_size = strlen("\033[")
        + strlen(TOP_PIXEL_COLOR) + strlen(ESCAPE_COLOR_TEMPLATE)
        + 1 /* ; */
        + strlen(BOTTOM_PIXEL_COLOR) + strlen(ESCAPE_COLOR_TEMPLATE)
        + 1 /* m */
        + strlen(PIXEL_CHARACTER);
    const int vertical_characters = (height_+1) / 2;  // two pixels, one glyph
    int buffer_size =
        vertical_characters * internal_width_ * max_pixel_size  // pixel count
        + vertical_characters;  // one \n per line

    buffer_size += strlen(SCREEN_POSTFIX);

    char scratch[64];
    snprintf(scratch, sizeof(scratch), SCREEN_CURSOR_UP_FORMAT, (height_+1)/2);
    goto_top_ = scratch;

    ansi_text_buffer_ = new char [ buffer_size + goto_top_.size() ];
}

TerminalCanvas::~TerminalCanvas() {
    delete [] ansi_text_buffer_;
    delete [] pixels_;
}

static inline char *int_append(char *buf, uint8_t val) {
    // There are probably faster ways.
    if (val >= 100) { *buf++ = (val / 100) + '0'; }
    if (val >= 10)  { *buf++ = ((val / 10) % 10) + '0'; }
    *buf++ = (val % 10) + '0';
    return buf;
}
static inline char *str_append(char *buf, const char *str) {
    int l = strlen(str);
    memcpy(buf, str, l);
    return buf + l;
}
static char *WriteAnsiColor(char *buf, uint32_t col) {
    buf = int_append(buf, (col & 0xff0000) >> 16);
    *buf++ = ';';
    buf = int_append(buf, (col & 0xff00) >> 8);
    *buf++ = ';';
    return int_append(buf, (col & 0xff));
}

void TerminalCanvas::SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || x >= width() || y < 0 || y >= height()) return;
    any_change_ = true;
    pixels_[internal_width_ * y + x] = (r << 16) | (g << 8) | b;
}

void TerminalCanvas::Send(int fd, bool jump_back_first) {
    if (any_change_) {
        char *pos = ansi_text_buffer_;
        memcpy(pos, goto_top_.data(), goto_top_.size());
        pos += goto_top_.size();
        uint32_t last_fg = 0xff000000;  // Guaranteed != first color
        uint32_t last_bg = 0xff000000;
        for (int y = 0; y < height_; y+=2) {
            for (int x = 0; x < internal_width_; ++x) {
                const uint32_t fg = pixels_[internal_width_ * y + x];
                const uint32_t bg = pixels_[internal_width_ * (y+1) + x];
                const char *prefix = "\033[";
                if (fg != last_fg) {
                    pos = str_append(pos, prefix);
                    pos = str_append(pos, TOP_PIXEL_COLOR);
                    pos = WriteAnsiColor(pos, fg);
                    prefix = ";";
                }
                if (bg != last_bg) {
                    pos = str_append(pos, prefix);
                    pos = str_append(pos, BOTTOM_PIXEL_COLOR);
                    pos = WriteAnsiColor(pos, bg);
                }
                if (fg != last_fg || bg != last_bg) {
                    *pos++ = 'm';
                }
                last_fg = fg;
                last_bg = bg;
                memcpy(pos, PIXEL_CHARACTER, strlen(PIXEL_CHARACTER));
                pos += strlen(PIXEL_CHARACTER);
            }
            *pos++ = '\n';
        }
        pos = str_append(pos, SCREEN_POSTFIX);
        end_ansi_ = pos;
        any_change_ = false;
    }
    const char *start_buf = (jump_back_first
                             ? ansi_text_buffer_
                             : ansi_text_buffer_ + goto_top_.size());
    reliable_write(fd, start_buf, end_ansi_ - start_buf);
}

void TerminalCanvas::ClearScreen(int fd) {
    reliable_write(fd, SCREEN_CLEAR, strlen(SCREEN_CLEAR));
}

void TerminalCanvas::CursorOff(int fd) {
    reliable_write(fd, CURSOR_OFF, strlen(CURSOR_OFF));
}

void TerminalCanvas::CursorOn(int fd) {
    reliable_write(fd, CURSOR_ON, strlen(CURSOR_ON));
}
