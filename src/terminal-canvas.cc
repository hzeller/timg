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
    : width_(w), height_(h), pixels_(new rgb_t [ width_ * height_]) {
    memset(pixels_, 0, sizeof(*pixels_) * width_ * height_);
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

#define SCREEN_CLEAR            "\033c"
#define SCREEN_CURSOR_UP_FORMAT "\033[%dA"  // Move cursor up given lines.

// Interestingly, cursor-on does not take effect until the next newline on
// the tested terminals. Not sure why that is, but adding a newline sounds
// like waste of vertical space, so let's not do it here but rather try
// to understand the actual reason why this is happening and fix it then.
#define CURSOR_ON       "\033[?25h"
#define CURSOR_OFF      "\033[?25l"

// Each character on the screen is divided in a top pixel and bottom pixel.
// We use a block character to fill one half with the foreground color,
// the other half is shown as background color.
// Two pixels one stone. Or something.
// Some fonts display the top block worse than the bottom block, so use the
// bottom block by default. Both UTF-8 sequences have the same length.
#define PIXEL_UPPER_HALF_BLOCK_CHARACTER  "\u2580"  // |▀|
#define PIXEL_LOWER_HALF_BLOCK_CHARACTER  "\u2584"  // |▄|
#define PIXEL_BLOCK_CHARACTER_LEN strlen(PIXEL_UPPER_HALF_BLOCK_CHARACTER)

#define PIXEL_SET_FOREGROUND_COLOR  "38;2;"
#define PIXEL_SET_BACKGROUND_COLOR  "48;2;"
#define PIXEL_SET_COLOR_LEN         strlen(PIXEL_SET_FOREGROUND_COLOR)

// Maximum length of the color value sequence
#define ESCAPE_COLOR_MAX_LEN strlen("rrr;ggg;bbb")

// We reset the terminal at the end of a line
#define SCREEN_END_OF_LINE          "\033[0m\n"
#define SCREEN_END_OF_LINE_LEN      strlen(SCREEN_END_OF_LINE)

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
        + PIXEL_SET_COLOR_LEN + ESCAPE_COLOR_MAX_LEN
        + 1 /* ; */
        + PIXEL_SET_COLOR_LEN + ESCAPE_COLOR_MAX_LEN
        + 1 /* m */
        + PIXEL_BLOCK_CHARACTER_LEN;

    const int vertical_characters = (height+1) / 2;   // two pixels, one glyph
    const size_t character_buffer_size = vertical_characters *
        (indent                      // Horizontal indentation with spaces
         + width * max_pixel_size    // pixels in one row
         + SCREEN_END_OF_LINE_LEN);  // Finishing a line.

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

TerminalCanvas::TerminalCanvas(int fd, bool use_upper_half_block)
    : fd_(fd),
      set_upper_color_(use_upper_half_block
                       ? PIXEL_SET_FOREGROUND_COLOR
                       : PIXEL_SET_BACKGROUND_COLOR),
      set_lower_color_(use_upper_half_block
                       ? PIXEL_SET_BACKGROUND_COLOR
                       : PIXEL_SET_FOREGROUND_COLOR),
      pixel_character_(use_upper_half_block
                       ? PIXEL_UPPER_HALF_BLOCK_CHARACTER
                       : PIXEL_LOWER_HALF_BLOCK_CHARACTER),
      top_optional_blank_(!use_upper_half_block) {
}

TerminalCanvas::~TerminalCanvas() {
    free(content_buffer_);
}

static char *int_append(char *buf, uint8_t val);
static char *WriteAnsiColor(char *buf, uint32_t col) {
    buf = int_append(buf, (col & 0xff0000) >> 16);
    *buf++ = ';';
    buf = int_append(buf, (col & 0xff00) >> 8);
    *buf++ = ';';
    return int_append(buf, (col & 0xff));
}

static inline char *str_append(char *pos, const char *value, size_t len) {
    memcpy(pos, value, len);
    return pos + len;
}

// Append two rows of pixels at once, by writing a half-block character with
// foreground/background.
static char *AppendDoubleRow(
    char *pos, int indent, int width,
    const Framebuffer::rgb_t *top_line,    const char *set_top_pixel_color,
    const Framebuffer::rgb_t *bottom_line, const char *set_btm_pixel_color,
    const char *pixel_glyph) {
    static constexpr char kStartEscape[] = "\033[";
    Framebuffer::rgb_t last_top_color = 0xff000000;  // Guaranteed != first
    Framebuffer::rgb_t last_bottom_color = 0xff000000;
    if (indent > 0) {
        memset(pos, ' ', indent);
        pos += indent;
    }
    for (int x = 0; x < width; ++x) {
        bool color_emitted = false;
        if (top_line) {
            const Framebuffer::rgb_t top_color = *top_line++;
            if (top_color != last_top_color) {
                // Appending prefix. At this point, it can only be kStartEscape
                pos = str_append(pos, kStartEscape, strlen(kStartEscape));
                pos = str_append(pos, set_top_pixel_color, PIXEL_SET_COLOR_LEN);
                pos = WriteAnsiColor(pos, top_color);
                last_top_color = top_color;
                color_emitted = true;
            }
        }
        if (bottom_line) {
            const Framebuffer::rgb_t bottom_color = *bottom_line++;
            if (bottom_color != last_bottom_color) {
                if (color_emitted) {
                    *pos++ = ';';
                } else {
                    pos = str_append(pos, kStartEscape, strlen(kStartEscape));
                }
                pos = str_append(pos, set_btm_pixel_color, PIXEL_SET_COLOR_LEN);
                pos = WriteAnsiColor(pos, bottom_color);
                last_bottom_color = bottom_color;
                color_emitted = true;
            }
        }
        if (color_emitted) {
            *pos++ = 'm';   // We emitted color; need to finish ESC seq
        }
        pos = str_append(pos, pixel_glyph, PIXEL_BLOCK_CHARACTER_LEN);
    }

    pos = str_append(pos, SCREEN_END_OF_LINE, SCREEN_END_OF_LINE_LEN);

    return pos;
}

void TerminalCanvas::Send(const Framebuffer &framebuffer, int indent) {
    const int width = framebuffer.width();
    const int height = framebuffer.height();
    char *start_buffer = EnsureBuffer(width, height, indent);
    char *pos = start_buffer;
    const Framebuffer::rgb_t *pixels = framebuffer.pixels_;
    const Framebuffer::rgb_t *top_line;
    const Framebuffer::rgb_t *bottom_line;

    // We are always writing two pixels at once with one character, which
    // requires to leave an empty line if the height of the framebuffer is odd.
    // We want to make sure that this empty line is written in natural terminal
    // background color to match the chosen terminal color.
    // Depending on if we use the upper or lower half block character to show
    // pixels, we might need to shift displaying by one pixel to make sure
    // the empty line matches up with the background part of that character.
    // This it the row_offset we calculate here.
    const bool needs_empty_line = (height % 2 != 0);
    const int row_offset = (needs_empty_line && top_optional_blank_) ? -1 : 0;

    for (int y = 0; y < height; y+=2) {
        const int row = y + row_offset;
        top_line = row < 0 ? nullptr : &pixels[width*row];
        bottom_line = (row+1) >= height ? nullptr : &pixels[width*(row + 1)];

        pos = AppendDoubleRow(pos, indent, width,
                              top_line, set_upper_color_,
                              bottom_line, set_lower_color_,
                              pixel_character_);
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

// Somewhat fast conversion uint8 -> ASCII digits. There are probably faster
// ways (send a pull request if you know one), but this is a good start.
// Approach is to specify exactly how many digits to memcpy(), which helps the
// compiler create good instructions.

// Jumping to multiples of 4 can be cheaper than 3.
static const char digit_convert[256][4] = {
  // Note: Single digits not used, just for appropriate array space-holding.
  {'0',       }, {'1',       }, {'2',       }, {'3',       }, {'4',       },
  {'5',       }, {'6',       }, {'7',       }, {'8',       }, {'9',       },

  // Two digit numbers. They are left aligned, as we copy the first 2 bytes.
  {'1','0',   }, {'1','1',   }, {'1','2',   }, {'1','3',   }, {'1','4',   },
  {'1','5',   }, {'1','6',   }, {'1','7',   }, {'1','8',   }, {'1','9',   },
  {'2','0',   }, {'2','1',   }, {'2','2',   }, {'2','3',   }, {'2','4',   },
  {'2','5',   }, {'2','6',   }, {'2','7',   }, {'2','8',   }, {'2','9',   },
  {'3','0',   }, {'3','1',   }, {'3','2',   }, {'3','3',   }, {'3','4',   },
  {'3','5',   }, {'3','6',   }, {'3','7',   }, {'3','8',   }, {'3','9',   },
  {'4','0',   }, {'4','1',   }, {'4','2',   }, {'4','3',   }, {'4','4',   },
  {'4','5',   }, {'4','6',   }, {'4','7',   }, {'4','8',   }, {'4','9',   },
  {'5','0',   }, {'5','1',   }, {'5','2',   }, {'5','3',   }, {'5','4',   },
  {'5','5',   }, {'5','6',   }, {'5','7',   }, {'5','8',   }, {'5','9',   },
  {'6','0',   }, {'6','1',   }, {'6','2',   }, {'6','3',   }, {'6','4',   },
  {'6','5',   }, {'6','6',   }, {'6','7',   }, {'6','8',   }, {'6','9',   },
  {'7','0',   }, {'7','1',   }, {'7','2',   }, {'7','3',   }, {'7','4',   },
  {'7','5',   }, {'7','6',   }, {'7','7',   }, {'7','8',   }, {'7','9',   },
  {'8','0',   }, {'8','1',   }, {'8','2',   }, {'8','3',   }, {'8','4',   },
  {'8','5',   }, {'8','6',   }, {'8','7',   }, {'8','8',   }, {'8','9',   },
  {'9','0',   }, {'9','1',   }, {'9','2',   }, {'9','3',   }, {'9','4',   },
  {'9','5',   }, {'9','6',   }, {'9','7',   }, {'9','8',   }, {'9','9',   },

  // All three digit numbers up to 255, copying out 3.
  // In fact, we copy 4 bytes, as 32-bit handling is cheaper natively.
  // Despite memory look-up overhead, this measures still faster than adding
  // more branches or calculations to int_append() (at lesat on x86).
  {'1','0','0'}, {'1','0','1'}, {'1','0','2'}, {'1','0','3'}, {'1','0','4'},
  {'1','0','5'}, {'1','0','6'}, {'1','0','7'}, {'1','0','8'}, {'1','0','9'},
  {'1','1','0'}, {'1','1','1'}, {'1','1','2'}, {'1','1','3'}, {'1','1','4'},
  {'1','1','5'}, {'1','1','6'}, {'1','1','7'}, {'1','1','8'}, {'1','1','9'},
  {'1','2','0'}, {'1','2','1'}, {'1','2','2'}, {'1','2','3'}, {'1','2','4'},
  {'1','2','5'}, {'1','2','6'}, {'1','2','7'}, {'1','2','8'}, {'1','2','9'},
  {'1','3','0'}, {'1','3','1'}, {'1','3','2'}, {'1','3','3'}, {'1','3','4'},
  {'1','3','5'}, {'1','3','6'}, {'1','3','7'}, {'1','3','8'}, {'1','3','9'},
  {'1','4','0'}, {'1','4','1'}, {'1','4','2'}, {'1','4','3'}, {'1','4','4'},
  {'1','4','5'}, {'1','4','6'}, {'1','4','7'}, {'1','4','8'}, {'1','4','9'},
  {'1','5','0'}, {'1','5','1'}, {'1','5','2'}, {'1','5','3'}, {'1','5','4'},
  {'1','5','5'}, {'1','5','6'}, {'1','5','7'}, {'1','5','8'}, {'1','5','9'},
  {'1','6','0'}, {'1','6','1'}, {'1','6','2'}, {'1','6','3'}, {'1','6','4'},
  {'1','6','5'}, {'1','6','6'}, {'1','6','7'}, {'1','6','8'}, {'1','6','9'},
  {'1','7','0'}, {'1','7','1'}, {'1','7','2'}, {'1','7','3'}, {'1','7','4'},
  {'1','7','5'}, {'1','7','6'}, {'1','7','7'}, {'1','7','8'}, {'1','7','9'},
  {'1','8','0'}, {'1','8','1'}, {'1','8','2'}, {'1','8','3'}, {'1','8','4'},
  {'1','8','5'}, {'1','8','6'}, {'1','8','7'}, {'1','8','8'}, {'1','8','9'},
  {'1','9','0'}, {'1','9','1'}, {'1','9','2'}, {'1','9','3'}, {'1','9','4'},
  {'1','9','5'}, {'1','9','6'}, {'1','9','7'}, {'1','9','8'}, {'1','9','9'},
  {'2','0','0'}, {'2','0','1'}, {'2','0','2'}, {'2','0','3'}, {'2','0','4'},
  {'2','0','5'}, {'2','0','6'}, {'2','0','7'}, {'2','0','8'}, {'2','0','9'},
  {'2','1','0'}, {'2','1','1'}, {'2','1','2'}, {'2','1','3'}, {'2','1','4'},
  {'2','1','5'}, {'2','1','6'}, {'2','1','7'}, {'2','1','8'}, {'2','1','9'},
  {'2','2','0'}, {'2','2','1'}, {'2','2','2'}, {'2','2','3'}, {'2','2','4'},
  {'2','2','5'}, {'2','2','6'}, {'2','2','7'}, {'2','2','8'}, {'2','2','9'},
  {'2','3','0'}, {'2','3','1'}, {'2','3','2'}, {'2','3','3'}, {'2','3','4'},
  {'2','3','5'}, {'2','3','6'}, {'2','3','7'}, {'2','3','8'}, {'2','3','9'},
  {'2','4','0'}, {'2','4','1'}, {'2','4','2'}, {'2','4','3'}, {'2','4','4'},
  {'2','4','5'}, {'2','4','6'}, {'2','4','7'}, {'2','4','8'}, {'2','4','9'},
  {'2','5','0'}, {'2','5','1'}, {'2','5','2'}, {'2','5','3'}, {'2','5','4'},
  {'2','5','5'}
};

// Append decimal representation of given "value" to "buffer".
// Does not \0-terminate. Might write one byte beyond number.
static char *int_append(char *buffer, uint8_t value) {
    if (value >= 100) {
        memcpy(buffer, digit_convert[value], 4);  // copy 4 cheaper than 3
        return buffer + 3;
    }
    if (value >= 10) {
        memcpy(buffer, digit_convert[value], 2);
        return buffer + 2;
    }
    *buffer = value + '0';
    return buffer + 1;
}
}  // namespace timg
