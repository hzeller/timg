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
#define SCREEN_RESET    "\033[0m"           // reset terminal settings
#define SCREEN_CURSOR_UP_FORMAT "\033[%dA"  // Move cursor up given lines.

// Interestingly, cursor-on does not take effect until the next newline on
// the tested terminals. Not sure why that is, but adding a newline sounds
// like waste of vertical space, so let's not do it here but rather try
// to understand the actual reason why this is happening and fix it then.
#define CURSOR_ON       "\033[?25h"
#define CURSOR_OFF      "\033[?25l"

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
    // Height is rounded up to the next even number as we output two vertical
    // pixels with one glyph.
    : width_(w), height_(h), pixels_(new uint32_t [ width_ * (height_ + 1)]),
      any_change_(true) {
    memset(pixels_, 0, sizeof(uint32_t) * width_ * (height_ + 1));  // black.

    // Preallocate buffer that we use to assemble the output before writing.

    // Pixels will be variable size depending on if we need to change colors
    // between two adjacent pixels. This is the maximum size they can be.
    const int max_pixel_size = strlen("\033[")
        + strlen(TOP_PIXEL_COLOR) + strlen(ESCAPE_COLOR_TEMPLATE)
        + 1 /* ; */
        + strlen(BOTTOM_PIXEL_COLOR) + strlen(ESCAPE_COLOR_TEMPLATE)
        + 1 /* m */
        + strlen(PIXEL_CHARACTER);

    const int vertical_characters = (height_+1) / 2;   // two pixels, one glyph
    const int character_buffer_size = vertical_characters *
        (width_ * max_pixel_size           // pixels in one row
         + strlen(SCREEN_RESET)            // Reset at each EOL
         + 1);                             // one \n per line

    // Piece of ESC-snippet to emit to go back to the top of the screen.
    char goto_top[64];
    const int goto_top_len = snprintf(goto_top, sizeof(goto_top),
                                      SCREEN_CURSOR_UP_FORMAT, (height_+1)/2);

    // The 'goto top' part of the buffer is prefixed the text content, and
    // is chosen at emit time if it is needed.
    content_buffer_ = new char [ goto_top_len + character_buffer_size ];
    memcpy(content_buffer_, goto_top, goto_top_len);
    goto_top_prefix_ = content_buffer_;
    ansi_text_buffer_start_ = content_buffer_ + goto_top_len;
}

TerminalCanvas::~TerminalCanvas() {
    delete [] content_buffer_;
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
    pixels_[width_ * y + x] = (r << 16) | (g << 8) | b;
}

void TerminalCanvas::Send(int fd, bool jump_back_first) {
    static constexpr char kStartEscape[] = "\033[";
    if (any_change_) {
        char *pos = ansi_text_buffer_start_;
        for (int y = 0; y < height_; y+=2) {
            uint32_t last_fg = 0xff000000;  // Guaranteed != first color
            uint32_t last_bg = 0xff000000;
            for (int x = 0; x < width_; ++x) {
                const uint32_t fg = pixels_[width_ * y + x];
                const uint32_t bg = pixels_[width_ * (y+1) + x];
                const char *prefix = kStartEscape;
                if (fg != last_fg) {
                    pos = str_append(pos, prefix);
                    pos = str_append(pos, TOP_PIXEL_COLOR);
                    pos = WriteAnsiColor(pos, fg);
                    last_fg = fg;
                    prefix = ";";
                }
                if (bg != last_bg && y + 1 != height_) {
                    pos = str_append(pos, prefix);
                    pos = str_append(pos, BOTTOM_PIXEL_COLOR);
                    pos = WriteAnsiColor(pos, bg);
                    last_bg = bg;
                    prefix = nullptr;   // Sentinel for next if()
                }
                if (prefix != kStartEscape) {
                    *pos++ = 'm';   // We emitted color; need to finish ESC seq
                }
                pos = str_append(pos, PIXEL_CHARACTER);
            }
            pos = str_append(pos, SCREEN_RESET); // end-of-line
            *pos++ = '\n';
        }
        ansi_text_end_ = pos;
        any_change_ = false;
    }
    const char *start_buf = (jump_back_first
                             ? goto_top_prefix_
                             : ansi_text_buffer_start_);
    reliable_write(fd, start_buf, ansi_text_end_ - start_buf);
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
