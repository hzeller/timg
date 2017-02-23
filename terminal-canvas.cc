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
#define ESCAPE_COLOR_FORMAT   "%03d;%03d;%03d"
#define TOP_PIXEL_COLOR       "\033[38;2;"
#define BOTTOM_PIXEL_COLOR    "\033[48;2;"

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
    : internal_width_(w+1), height_(h) {
    char scratch[64];
    initial_offset_ = buffer_.size();
    snprintf(scratch, sizeof(scratch),
             TOP_PIXEL_COLOR    ESCAPE_COLOR_FORMAT "m"
             BOTTOM_PIXEL_COLOR ESCAPE_COLOR_FORMAT "m"
             PIXEL_CHARACTER,
             0, 0, 0, 0, 0, 0); // black.
    pixel_offset_ = strlen(scratch);
    const int vertical_characters = (height_+1) / 2;  // two pixels, one glyph
    for (int y = 0; y < vertical_characters; ++y) {
        for (int x = 0; x < internal_width_; ++x) {
            buffer_.append(scratch);
        }
        buffer_.append("\n");
    }

    buffer_.append(SCREEN_POSTFIX);

    // Some useful precalculated length.
    snprintf(scratch, sizeof(scratch),
             ESCAPE_COLOR_FORMAT "m" BOTTOM_PIXEL_COLOR, 0, 0, 0);
    lower_row_pixel_offset_ = strlen(scratch);

    snprintf(scratch, sizeof(scratch), SCREEN_CURSOR_UP_FORMAT, (height_+1)/2);
    goto_top_ = scratch;
}

static void WriteByteDecimal(char *buf, uint8_t val) {
    buf[2] = (val % 10) + '0'; val /= 10;
    buf[1] = (val % 10) + '0'; val /= 10;
    buf[0] = val + '0';
}

void TerminalCanvas::SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || x >= width() || y < 0 || y >= height()) return;
    const int double_row = y/2;
    const int pos = initial_offset_
        + (internal_width_ * double_row + x) * pixel_offset_
        + strlen(TOP_PIXEL_COLOR)            // go where the color fmt starts
        + (y % 2) * lower_row_pixel_offset_  // offset for odd-row y-pixels
        + double_row;                        // 1 newline per double row
    char *buf = const_cast<char*>(buffer_.data()) + pos;  // Living on the edge
    WriteByteDecimal(buf, r);      // rrr;___;___
    WriteByteDecimal(buf + 4, g);  // ___;ggg;___
    WriteByteDecimal(buf + 8, b);  // ___;___;bbb
}

void TerminalCanvas::Send(int fd, bool jump_back_first) {
    if (jump_back_first) reliable_write(fd, goto_top_.data(), goto_top_.size());
    reliable_write(fd, buffer_.data(), buffer_.size());
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
