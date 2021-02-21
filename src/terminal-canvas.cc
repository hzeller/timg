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
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef __APPLE__
#    include <libkern/OSByteOrder.h>
#    define htole32(x) OSSwapHostToLittleInt32(x)
#    define le32toh(x) OSSwapLittleToHostInt32(x)
#else
#    include <endian.h>
#endif

namespace timg {

Framebuffer::Framebuffer(int w, int h)
    : width_(w), height_(h), pixels_(new rgba_t [ width_ * height_]) {
    strides_[0] = (int)sizeof(rgba_t) * width_;
    strides_[1] = 0;  // empty sentinel value.
    Clear();
}

Framebuffer::~Framebuffer() {
    delete [] row_data_;
    delete [] pixels_;
}

void Framebuffer::SetPixel(int x, int y, rgba_t value) {
    if (x < 0 || x >= width() || y < 0 || y >= height()) return;
    pixels_[width_ * y + x] = value;
}

Framebuffer::rgba_t Framebuffer::at(int x, int y) const {
    assert(x >= 0 && x < width() && y >= 0 && y < height());
    return pixels_[width_ * y + x];
}

void Framebuffer::Clear() {
    memset(pixels_, 0, sizeof(*pixels_) * width_ * height_);
}

Framebuffer::rgba_t Framebuffer::to_rgba(
    uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return htole32((uint32_t)r <<  0 |
                   (uint32_t)g <<  8 |
                   (uint32_t)b << 16 |
                   (uint32_t)a << 24);
}

uint8_t** Framebuffer::row_data() {
    if (!row_data_) {
        row_data_ = new uint8_t* [ height_ + 1];
        for (int i = 0; i < height_; ++i)
            row_data_[i] = (uint8_t*)pixels_ + i * width_ * sizeof(rgba_t);
        row_data_[height_] = nullptr;  // empty sentinel value.
    }
    return row_data_;
}

Framebuffer::rgba_t Framebuffer::ParseColor(const char *color) {
    if (!color) return to_rgba(0, 0, 0, 0);
    uint32_t r, g, b;
    if ((sscanf(color, "#%02x%02x%02x", &r, &g, &b) == 3) ||
        (sscanf(color, "rgb(%d, %d, %d)", &r, &g, &b) == 3)) {
        return to_rgba(r, g, b, 0xff);
    }
    fprintf(stderr, "Couldn't parse color '%s'\n", color);
    return to_rgba(0, 0, 0, 0);
}

static Framebuffer::rgba_t AlphaBlend(const uint32_t *bg,
                                      Framebuffer::rgba_t c) {
    const uint32_t col = le32toh(c);  // NOP on common LE platforms
    const uint32_t alpha = c >> 24;
    if (alpha == 0xff) return c;

    uint32_t fgc[3] = { (col>>0) & 0xff, (col>>8) & 0xff, (col>>16) & 0xff };
    uint32_t out[3];
    for (int i = 0; i < 3; ++i) {
        out[i] = sqrtf((fgc[i]*fgc[i]        * alpha +
                        bg[i] * (0xFF - alpha)) / 0xff);
    }
    return Framebuffer::to_rgba(out[0], out[1], out[2], 0xff);
}

void Framebuffer::AlphaComposeBackground(rgba_t bgcolor, rgba_t pattern_col) {
    uint32_t premultiplied_bg[3];
    uint32_t premultiplied_pattern[3];
    uint32_t *bg_choice[2] = { premultiplied_bg, premultiplied_bg };

    // Create pre-multiplied version
    const uint32_t bcol = le32toh(bgcolor);
    const uint32_t bg_alpha = bcol >> 24;
    if (bg_alpha == 0x00) return; // nothing to do.
    assert(bg_alpha == 0xff);   // We don't support partially transparent bg.

    premultiplied_bg[0] = (bcol & 0xff) * (bcol & 0xff);
    premultiplied_bg[1] = ((bcol>> 8) & 0xff) * ((bcol>> 8) & 0xff);
    premultiplied_bg[2] = ((bcol>>16) & 0xff) * ((bcol>>16) & 0xff);

    // If alpha channel indicates that pattern is used, also prepare it and
    // use as 2nd choice.
    const uint32_t pcol = le32toh(pattern_col);
    if (pcol >> 24 == 0xff) {
        premultiplied_pattern[0] = (pcol & 0xff) * (pcol & 0xff);
        premultiplied_pattern[1] = ((pcol>> 8) & 0xff) * ((pcol>> 8) & 0xff);
        premultiplied_pattern[2] = ((pcol>>16) & 0xff) * ((pcol>>16) & 0xff);
        bg_choice[1] = premultiplied_pattern;
    }

    rgba_t *pos = pixels_;
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            *pos = AlphaBlend(bg_choice[(x + y) % 2], *pos);
            pos++;
        }
    }
}

#define SCREEN_CLEAR            "\033c"

#define SCREEN_CURSOR_UP_FORMAT    "\033[%dA"  // Move cursor up given lines.
#define SCREEN_CURSOR_DN_FORMAT    "\033[%dB"  // Move cursor down given lines.
#define SCREEN_CURSOR_RIGHT_FORMAT "\033[%dC"  // Move cursor right given cols
#define SCREEN_CURSOR_LEFT_FORMAT  "\033[%dD"  // Move cursor left given cols

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

void TerminalCanvas::WriteBuffer(const char *buffer, size_t size) {
    int written;
    while (size && (written = write(fd_, buffer, size)) > 0) {
        size -= written;
        buffer += written;
    }
}

char *TerminalCanvas::EnsureBuffer(int width, int height) {
    // Pixels will be variable size depending on if we need to change colors
    // between two adjacent pixels. This is the maximum size they can be.
    static const int max_pixel_size = strlen("\033[")
        + PIXEL_SET_COLOR_LEN + ESCAPE_COLOR_MAX_LEN
        + 1 /* ; */
        + PIXEL_SET_COLOR_LEN + ESCAPE_COLOR_MAX_LEN
        + 1 /* m */
        + PIXEL_BLOCK_CHARACTER_LEN;
    // Few extra space for number printed in the format.
    static const int opt_cursor_up = strlen(SCREEN_CURSOR_UP_FORMAT) + 3;
    static const int opt_cursor_right = strlen(SCREEN_CURSOR_RIGHT_FORMAT) + 3;
    const int vertical_characters = (height+1) / 2;   // two pixels, one glyph
    const size_t character_buffer_size = opt_cursor_up  // Jump up
        + vertical_characters
        * (opt_cursor_right            // Horizontal jump
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

static char *int_append_with_semicolon(char *buf, uint8_t val);
static char *WriteAnsiColor(char *buf, Framebuffer::rgba_t color) {
    const uint32_t col = le32toh(color);  // NOP on common LE platforms
    buf = int_append_with_semicolon(buf, (col & 0x0000ff) >>  0);  // r
    buf = int_append_with_semicolon(buf, (col & 0x00ff00) >>  8);  // g
    return int_append_with_semicolon(buf,(col & 0xff0000) >> 16);  // b
}

static inline char *str_append(char *pos, const char *value, size_t len) {
    memcpy(pos, value, len);
    return pos + len;
}

// Append two rows of pixels at once, by writing a half-block character with
// foreground/background.
char *TerminalCanvas::AppendDoubleRow(char *pos, int indent, int width,
                                      const Framebuffer::rgba_t *top_line,
                                      const Framebuffer::rgba_t *bottom_line) {
    static constexpr char kStartEscape[] = "\033[";
    // Since we're not dealing with alpha yet, we can use that as a sentinel.
    Framebuffer::rgba_t last_top_color = Framebuffer::to_rgba(0, 0, 0, 0xaa);
    Framebuffer::rgba_t last_bottom_color = Framebuffer::to_rgba(0, 0, 0, 0xaa);
    if (indent > 0) {
        pos += sprintf(pos, SCREEN_CURSOR_RIGHT_FORMAT, indent);
    }
    for (int x = 0; x < width; ++x) {
        bool color_emitted = false;
        if (top_line) {
            const Framebuffer::rgba_t top_color = *top_line++;
            if (top_color != last_top_color) {
                // Appending prefix. At this point, it can only be kStartEscape
                pos = str_append(pos, kStartEscape, strlen(kStartEscape));
                pos = str_append(pos, set_upper_color_, PIXEL_SET_COLOR_LEN);
                pos = WriteAnsiColor(pos, top_color);
                last_top_color = top_color;
                color_emitted = true;
            }
        }
        if (bottom_line) {
            const Framebuffer::rgba_t bot_color = *bottom_line++;
            if (bot_color != last_bottom_color) {
                if (!color_emitted) {
                    pos = str_append(pos, kStartEscape, strlen(kStartEscape));
                }
                pos = str_append(pos, set_lower_color_, PIXEL_SET_COLOR_LEN);
                pos = WriteAnsiColor(pos, bot_color);
                last_bottom_color = bot_color;
                color_emitted = true;
            }
        }
        if (color_emitted) {
            *(pos-1) = 'm';   // overwrite semicolon with finish ESC seq.
        }
        pos = str_append(pos, pixel_character_, PIXEL_BLOCK_CHARACTER_LEN);
    }

    pos = str_append(pos, SCREEN_END_OF_LINE, SCREEN_END_OF_LINE_LEN);

    return pos;
}

void TerminalCanvas::Send(int x, int dy, const Framebuffer &framebuffer) {
    const int width = framebuffer.width();
    const int height = framebuffer.height();
    char *const start_buffer = EnsureBuffer(width, height);
    char *pos = start_buffer;

    if (dy < 0) {
        pos += sprintf(pos, SCREEN_CURSOR_UP_FORMAT, (abs(dy) + 1) / 2);
    }

    const Framebuffer::rgba_t *const pixels = framebuffer.data();
    const Framebuffer::rgba_t *top_line;
    const Framebuffer::rgba_t *bottom_line;

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

        pos = AppendDoubleRow(pos, x, width,
                              top_line, bottom_line);
    }
    WriteBuffer(start_buffer, pos - start_buffer);
}

void TerminalCanvas::MoveCursorDY(int rows) {
    if (rows == 0) return;
    char buf[32];
    const size_t len = sprintf(buf, rows < 0
                               ? SCREEN_CURSOR_UP_FORMAT
                               : SCREEN_CURSOR_DN_FORMAT,
                               abs(rows));
    WriteBuffer(buf, len);
}

void TerminalCanvas::MoveCursorDX(int cols) {
    if (cols == 0) return;
    char buf[32];
    const size_t len = sprintf(buf, cols < 0
                               ? SCREEN_CURSOR_LEFT_FORMAT
                               : SCREEN_CURSOR_RIGHT_FORMAT,
                               abs(cols));
    WriteBuffer(buf, len);
}

void TerminalCanvas::ClearScreen() {
    WriteBuffer(SCREEN_CLEAR, strlen(SCREEN_CLEAR));
}

void TerminalCanvas::CursorOff() {
    WriteBuffer(CURSOR_OFF, strlen(CURSOR_OFF));
}

void TerminalCanvas::CursorOn() {
    WriteBuffer(CURSOR_ON, strlen(CURSOR_ON));
}

// Converting the colors requires fast uint8 -> ASCII decimal digits with
// appended semicolon. There are probably faster ways (send a pull request
// if you know one), but this is a good start.
// Approach is to specify exactly how many digits to memcpy(), which helps the
// compiler create good instructions.

// Make sure we're 4 aligned so that we can quickly access chunks of 4 bytes.
// While at it, let's go further and align it to 64 byte cache lines.
struct digit_convert {
    char data[1025];
};
static constexpr digit_convert convert_lookup __attribute__ ((aligned(64))) = {
    "0;  1;  2;  3;  4;  5;  6;  7;  8;  9;  10; 11; 12; 13; 14; 15; "
    "16; 17; 18; 19; 20; 21; 22; 23; 24; 25; 26; 27; 28; 29; 30; 31; "
    "32; 33; 34; 35; 36; 37; 38; 39; 40; 41; 42; 43; 44; 45; 46; 47; "
    "48; 49; 50; 51; 52; 53; 54; 55; 56; 57; 58; 59; 60; 61; 62; 63; "
    "64; 65; 66; 67; 68; 69; 70; 71; 72; 73; 74; 75; 76; 77; 78; 79; "
    "80; 81; 82; 83; 84; 85; 86; 87; 88; 89; 90; 91; 92; 93; 94; 95; "
    "96; 97; 98; 99; 100;101;102;103;104;105;106;107;108;109;110;111;"
    "112;113;114;115;116;117;118;119;120;121;122;123;124;125;126;127;"
    "128;129;130;131;132;133;134;135;136;137;138;139;140;141;142;143;"
    "144;145;146;147;148;149;150;151;152;153;154;155;156;157;158;159;"
    "160;161;162;163;164;165;166;167;168;169;170;171;172;173;174;175;"
    "176;177;178;179;180;181;182;183;184;185;186;187;188;189;190;191;"
    "192;193;194;195;196;197;198;199;200;201;202;203;204;205;206;207;"
    "208;209;210;211;212;213;214;215;216;217;218;219;220;221;222;223;"
    "224;225;226;227;228;229;230;231;232;233;234;235;236;237;238;239;"
    "240;241;242;243;244;245;246;247;248;249;250;251;252;253;254;255;"
};

// Append decimal representation plus semicolon of given "value" to "buffer".
// Does not \0-terminate. Might write one byte beyond number.
static char *int_append_with_semicolon(char *buffer, uint8_t value) {
    // We cheat a little here: for the beauty of initizliaing the above array
    // with a block of text, we manually aligned the data array to 4 to
    // be able to interpret it as uint-array generating fast accesses like
    //    mov eax, DWORD PTR convert_lookup[0+rax*4]
    // (only slightly invokong undefined behavior with this type punning :) )
    const uint32_t *const four_bytes = (const uint32_t*) convert_lookup.data;
    if (value >= 100) {
        memcpy(buffer, &four_bytes[value], 4);
        return buffer + 4;
    }
    if (value >= 10) {
        memcpy(buffer, &four_bytes[value], 4); // copy 4 cheaper than 3
        return buffer + 3;
    }
    memcpy(buffer, &four_bytes[value], 2);
    return buffer + 2;
}

}  // namespace timg
