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

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

namespace timg {
Framebuffer::Framebuffer(int w, int h)
    // Height is rounded up to the next even number as we output two vertical
    // pixels with one glyph.
    : width_(w), height_(h), pixels_(new uint32_t [ width_ * (height_ + 1)]) {
    memset(pixels_, 0, sizeof(uint32_t) * width_ * (height_ + 1));  // black.
}

Framebuffer::~Framebuffer() {
    delete [] pixels_;
}

void Framebuffer::SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    SetPixel(x, y, (r << 16) | (g << 8) | b);
}

void Framebuffer::SetPixel(int x, int y, rgb_t value) {
    if (x < 0 || x >= width() || y < 0 || y >= height()) return;
    pixels_[width_ * y + x] = value;
}

Framebuffer::rgb_t Framebuffer::at(int x, int y) const {
    assert(x >= 0 && x < width() && y >= 0 && y < height());
    return pixels_[width_ * y + x];
}

#define SCREEN_CLEAR    "\033c"
#define SCREEN_RESET    "\033[0m"           // reset terminal settings
#define SCREEN_CURSOR_UP_FORMAT "\033[%dA"  // Move cursor up given lines.

// Interestingly, cursor-on does not take effect until the next newline on
// the tested terminals. Not sure why that is, but adding a newline sounds
// like waste of vertical space, so let's not do it here but rather try
// to understand the actual reason why this is happening and fix it then.
#define CURSOR_ON       "\033[?25h"
#define CURSOR_OFF      "\033[?25l"

// Now, pixels on the even row will get the foreground color changed, pixels
// on odd rows the background color. Two pixels one stone. Or something.
#define ESCAPE_COLOR_TEMPLATE "rrr;ggg;bbb"

// Each character on the screen is divided in a top pixel and bottom pixel.
// We use a block character to fill one half, the rest is shown as background.
// Some fonts display the top block worse than the bottom block, so use the
// bottom block by default, but use it for the last line.

#define PIXEL_UPPER_HALF_BLOCK_CHARACTER  "▀"
#define PIXEL_LOWER_HALF_BLOCK_CHARACTER  "▄"
#define PIXEL_SET_FOREGROUND_COLOR       "38;2;"
#define PIXEL_SET_BACKGROUND_COLOR        "48;2;"

#if PIXEL_USE_UPPER_BLOCK
// Top foreground color, bottom background color
#  define PIXEL_CHARACTER      PIXEL_UPPER_HALF_BLOCK_CHARACTER
#  define TOP_PIXEL_COLOR      PIXEL_SET_FOREGROUND_COLOR
#  define BOTTOM_PIXEL_COLOR   PIXEL_SET_BACKGROUND_COLOR
#else
// Top background color, bottom foreground color
#  define PIXEL_CHARACTER      PIXEL_LOWER_HALF_BLOCK_CHARACTER
#  define TOP_PIXEL_COLOR      PIXEL_SET_BACKGROUND_COLOR
#  define BOTTOM_PIXEL_COLOR   PIXEL_SET_FOREGROUND_COLOR
#endif

static void reliable_write(int fd, const char *buf, size_t size) {
    int written;
    while (size && (written = write(fd, buf, size)) > 0) {
        size -= written;
        buf += written;
    }
}

char *TerminalCanvas::EnsureBuffer(int width, int height, int indent) {
    // Pixels will be variable size depending on if we need to change colors
    // between two adjacent pixels. This is the maximum size they can be.
    static const int max_pixel_size = strlen("\033[")
        + strlen(TOP_PIXEL_COLOR) + strlen(ESCAPE_COLOR_TEMPLATE)
        + 1 /* ; */
        + strlen(BOTTOM_PIXEL_COLOR) + strlen(ESCAPE_COLOR_TEMPLATE)
        + 1 /* m */
        + strlen(PIXEL_CHARACTER);

    const int vertical_characters = (height+1) / 2;   // two pixels, one glyph
    const size_t character_buffer_size = vertical_characters *
        (indent                    // Horizontal indentation with spaces
         + width * max_pixel_size  // pixels in one row
         + strlen(SCREEN_RESET)    // Reset at each EOL
         + 1);                     // one \n per line

    if (character_buffer_size > buffer_size_) {
        if (!content_buffer_) {
            content_buffer_ = (char*)malloc(character_buffer_size);
        } else {
            content_buffer_ = (char*)realloc(content_buffer_,
                                             character_buffer_size);
        }
        buffer_size_ = character_buffer_size;
    }
    return content_buffer_;
}

TerminalCanvas::~TerminalCanvas() {
    free(content_buffer_);
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

// Append two rows of pixels at once, by writing a half-block character with
// foreground/background.
static char *AppendDoubleRow(
    char *pos, int indent, int width,
    const Framebuffer::rgb_t *top_line, const char *top_pixel_color,
    const Framebuffer::rgb_t *bottom_line, const char *bottom_pixel_color,
    const char *pixel_glyph) {
    static constexpr char kStartEscape[] = "\033[";
    Framebuffer::rgb_t last_top = 0xff000000;  // Guaranteed != first color
    Framebuffer::rgb_t last_btm = 0xff000000;
    if (indent > 0) {
        memset(pos, ' ', indent);
        pos += indent;
    }
    for (int x = 0; x < width; ++x) {
        const char *prefix = kStartEscape;
        const Framebuffer::rgb_t top = *top_line++;
        if (top != last_top) {
            pos = str_append(pos, prefix);
            pos = str_append(pos, top_pixel_color);
            pos = WriteAnsiColor(pos, top);
            last_top = top;
            prefix = ";";
        }
        if (bottom_line) {  // Might not be there if odd-height image.
            const Framebuffer::rgb_t btm = *bottom_line++;
            if (btm != last_btm) {
                pos = str_append(pos, prefix);
                pos = str_append(pos, bottom_pixel_color);
                pos = WriteAnsiColor(pos, btm);
                last_btm = btm;
                prefix = nullptr;   // Sentinel for next if()
            }
        }
        if (prefix != kStartEscape) {
            *pos++ = 'm';   // We emitted color; need to finish ESC seq
        }
        pos = str_append(pos, pixel_glyph);
    }
    return pos;
}

void TerminalCanvas::Send(const Framebuffer &framebuffer, int indent) {
    const int width = framebuffer.width();
    const int height = framebuffer.height();
    char *start_buffer = EnsureBuffer(width, height, indent);
    char *pos = start_buffer;
    for (int y = 0; y < height; y+=2) {
        const Framebuffer::rgb_t *top_line = &framebuffer.pixels_[width*y];
        const Framebuffer::rgb_t *bottom_line =
            (y+1) == height ? nullptr : &framebuffer.pixels_[width*(y+1)];
        if (bottom_line) {
            pos = AppendDoubleRow(pos, indent, width,
                                  top_line, TOP_PIXEL_COLOR,
                                  bottom_line, BOTTOM_PIXEL_COLOR,
                                  PIXEL_CHARACTER);
        } else {
            // If we don't have to display anything at the bottom, we always
            // use the upper half character: that way we can set the 'meat'
            // of the character to the color in question, while leaving the
            // bottom line just with the terminal reset background.
            // That way it will always look good in black/white/colored
            // backgrounds.
            pos = AppendDoubleRow(pos, indent, width,
                                  top_line, PIXEL_SET_FOREGROUND_COLOR,
                                  nullptr, nullptr,
                                  PIXEL_UPPER_HALF_BLOCK_CHARACTER);
        }
        pos = str_append(pos, SCREEN_RESET); // end-of-line
        *pos++ = '\n';
    }
    reliable_write(fd_, start_buffer, pos - start_buffer);
}

void TerminalCanvas::JumpUpPixels(int pixels) {
    if (pixels <= 0) return;
    dprintf(fd_, SCREEN_CURSOR_UP_FORMAT, (pixels+1)/2);
}

void TerminalCanvas::ClearScreen() {
    reliable_write(fd_, SCREEN_CLEAR, strlen(SCREEN_CLEAR));
}

void TerminalCanvas::CursorOff() {
    reliable_write(fd_, CURSOR_OFF, strlen(CURSOR_OFF));
}

void TerminalCanvas::CursorOn() {
    reliable_write(fd_, CURSOR_ON, strlen(CURSOR_ON));
}
}  // namespace timg
