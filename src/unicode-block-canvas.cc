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

#include "unicode-block-canvas.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#define SCREEN_CURSOR_UP_FORMAT    "\033[%dA"  // Move cursor up given lines.
#define SCREEN_CURSOR_DN_FORMAT    "\033[%dB"  // Move cursor down given lines.
#define SCREEN_CURSOR_RIGHT_FORMAT "\033[%dC"  // Move cursor right given cols

// Each character on the screen is divided in a top pixel and bottom pixel.
// We use a block character to fill one half with the foreground color,
// the other half is shown as background color.
// Two pixels one stone. Or something.
// Some fonts display the top block worse than the bottom block, so use the
// bottom block by default. Both UTF-8 sequences have the same length.
#define PIXEL_UPPER_HALF_BLOCK_CHARACTER  "\u2580"  // |▀|
#define PIXEL_LOWER_HALF_BLOCK_CHARACTER  "\u2584"  // |▄|
#define PIXEL_BLOCK_CHARACTER_LEN strlen(PIXEL_UPPER_HALF_BLOCK_CHARACTER)

#define PIXEL_SET_FG_COLOR     "38;2;"
#define PIXEL_SET_BG_COLOR     "48;2;"
#define PIXEL_SET_COLOR_LEN     strlen(PIXEL_SET_FG_COLOR)

// Maximum length of the color value sequence
#define ESCAPE_COLOR_MAX_LEN    strlen("rrr;ggg;bbb")

// We reset the terminal at the end of a line
#define SCREEN_END_OF_LINE      "\033[0m\n"
#define SCREEN_END_OF_LINE_LEN  strlen(SCREEN_END_OF_LINE)

namespace timg {

char *UnicodeBlockCanvas::EnsureBuffers(int width, int height) {
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
    const size_t new_content_size = opt_cursor_up     // Jump up
        + vertical_characters
        * (opt_cursor_right            // Horizontal jump
           + width * max_pixel_size    // pixels in one row
           + SCREEN_END_OF_LINE_LEN);  // Finishing a line.

    if (new_content_size > content_buffer_size_) {
        content_buffer_ = (char*)realloc(content_buffer_, new_content_size);
        content_buffer_size_ = new_content_size;
    }

    const size_t new_backing = width * (height+1)/2 * sizeof(DoubleRowColor);
    if (new_backing > backing_buffer_size_) {
        backing_buffer_ =(DoubleRowColor*)realloc(backing_buffer_, new_backing);
        backing_buffer_size_ = new_backing;
    }

    const size_t new_empty = width * sizeof(rgba_t);
    if (new_empty > empty_line_size_) {
        empty_line_ = (rgba_t*)realloc(empty_line_, new_empty);
        empty_line_size_ = new_empty;
        memset(empty_line_, 0x00, empty_line_size_);
    }
    return content_buffer_;
}

UnicodeBlockCanvas::UnicodeBlockCanvas(int fd, bool use_upper_half_block)
    : TerminalCanvas(fd),
      pixel_character_(use_upper_half_block
                       ? PIXEL_UPPER_HALF_BLOCK_CHARACTER
                       : PIXEL_LOWER_HALF_BLOCK_CHARACTER),
      use_upper_half_block_(use_upper_half_block) {
}

UnicodeBlockCanvas::~UnicodeBlockCanvas() {
    free(content_buffer_);
    free(backing_buffer_);
    free(empty_line_);
}

static char *int_append_with_semicolon(char *buf, uint8_t val);
static char *WriteAnsiColor(char *buf, rgba_t color) {
    buf = int_append_with_semicolon(buf, color.r);
    buf = int_append_with_semicolon(buf, color.g);
    return int_append_with_semicolon(buf, color.b);
}

static inline char *str_append(char *pos, const char *value, size_t len) {
    memcpy(pos, value, len);
    return pos + len;
}

// Append two rows of pixels at once, by writing a half-block character with
// foreground/background. The caller already chose which lines are written
// in foreground and background (depending on the used block)
inline bool is_transparent(rgba_t c) {  return c.a < 0x60; }
char *UnicodeBlockCanvas::AppendDoubleRow(char *pos, int indent, int width,
                                      const rgba_t *fg_line,
                                      const rgba_t *bg_line,
                                      bool emit_difference,
                                      int *y_skip) {
    static constexpr char kStartEscape[] = "\033[";
    DoubleRowColor last = {};
    bool last_color_unknown = true;
    int x_skip = indent;
    const char *start = pos;
    for (int x = 0; x < width; ++x, ++previous_content_iterator_) {
        const DoubleRowColor current_color = { *fg_line++, *bg_line++ };

        if (emit_difference && current_color == *previous_content_iterator_) {
            ++x_skip;
            continue;
        }

        if (*y_skip) {  // Emit cursor down or newlines, whatever is shorter
            if (*y_skip <= 4) {
                memset(pos, '\n', *y_skip);
                pos += *y_skip;
            } else {
                pos += sprintf(pos, SCREEN_CURSOR_DN_FORMAT, *y_skip);
            }
            *y_skip = 0;
        }

        if (x_skip > 0) {
            pos += sprintf(pos, SCREEN_CURSOR_RIGHT_FORMAT, x_skip);
            x_skip = 0;
        }

        // NOTE, not implemented:
        // If background has a solid color and _foreground_ is transparent, we
        // could switch to reverse \033[7m and swap coloring (as we can't switch
        // the double-block we use). However, given that this is such a niche
        // application (only if someone chooses -b none) with small quality
        // improvements, or worse even on a transparent kitty, it is not worth
        // the branches, so not implementing for now.

        bool color_emitted = false;
        bool both_transparent = false;

        // Foreground
        if (last_color_unknown || current_color.fg != last.fg) {
            // Appending prefix. At this point, it can only be kStartEscape
            pos = str_append(pos, kStartEscape, strlen(kStartEscape));
            pos = str_append(pos, PIXEL_SET_FG_COLOR, PIXEL_SET_COLOR_LEN);
            pos = WriteAnsiColor(pos, current_color.fg);
            color_emitted = true;
        }

        // Background
        if (last_color_unknown || current_color.bg != last.bg) {
            if (!color_emitted) {
                pos = str_append(pos, kStartEscape, strlen(kStartEscape));
            }
            if (is_transparent(current_color.bg)) {
                // This is best effort and only happens with -b none
                pos = str_append(pos, "49;", 3);  // Reset background color
                both_transparent = is_transparent(current_color.fg);
            } else {
                pos = str_append(pos, PIXEL_SET_BG_COLOR, PIXEL_SET_COLOR_LEN);
                pos = WriteAnsiColor(pos, current_color.bg);
            }
            color_emitted = true;
        }

        if (color_emitted) {
            *(pos-1) = 'm';   // overwrite semicolon with finish ESC seq.
        }
        if (current_color.fg == current_color.bg || both_transparent) {
            *pos++ = ' ';  // Simple background 'block'
        } else {
            pos = str_append(pos, pixel_character_, PIXEL_BLOCK_CHARACTER_LEN);
        }
        last = current_color;
        last_color_unknown = false;
        *previous_content_iterator_ = current_color;
    }

    if (pos == start) {  // Nothing emitted for whole line
        (*y_skip)++;
    } else {
        pos = str_append(pos, SCREEN_END_OF_LINE, SCREEN_END_OF_LINE_LEN);
    }

    return pos;
}

ssize_t UnicodeBlockCanvas::Send(int x, int dy, const Framebuffer &framebuffer) {
    const int width = framebuffer.width();
    const int height = framebuffer.height();
    char *const start_buffer = EnsureBuffers(width, height);
    char *pos = start_buffer;

    if (dy < 0) {
        pos += sprintf(pos, SCREEN_CURSOR_UP_FORMAT, (abs(dy) + 1) / 2);
    }
    const char *before_image_emission = pos;

    const rgba_t *const pixels = framebuffer.data();
    const rgba_t *fg_line, *bg_line, *top_line, *bottom_line;

    // If we just got requested to move back where we started the last image,
    // we just need to emit pixels that changed.
    previous_content_iterator_ = backing_buffer_;
    const bool should_emit_difference = (x == last_x_indent_) &&
        (last_framebuffer_height_ > 0) && abs(dy) == last_framebuffer_height_;

    // We are always writing two pixels at once with one character, which
    // requires to leave an empty line if the height of the framebuffer is odd.
    // We want to make sure that this empty line is written in natural terminal
    // background color to match the chosen terminal color.
    // Depending on if we use the upper or lower half block character to show
    // pixels, we might need to shift displaying by one pixel to make sure
    // the empty line matches up with the background part of that character.
    // This it the row_offset we calculate here.
    const bool needs_empty_line = (height % 2 != 0);
    const bool top_optional_blank = !use_upper_half_block_;
    const int row_offset = (needs_empty_line && top_optional_blank) ? -1 : 0;

    int accumulated_y_skip = 0;
    for (int y = 0; y < height; y+=2) {
        const int row = y + row_offset;
        top_line = row < 0 ? empty_line_ : &pixels[width*row];
        bottom_line = (row+1) >= height ? empty_line_ : &pixels[width*(row+1)];

        fg_line = use_upper_half_block_ ? top_line : bottom_line;
        bg_line = use_upper_half_block_ ? bottom_line : top_line;
        pos = AppendDoubleRow(pos, x, width,
                              fg_line, bg_line,
                              should_emit_difference,
                              &accumulated_y_skip);
    }
    last_framebuffer_height_ = height;
    last_x_indent_ = x;
    if (before_image_emission == pos)
        return 0;   // Don't even emit cursor up/dn jump.

    if (accumulated_y_skip) {
        pos += sprintf(pos, SCREEN_CURSOR_DN_FORMAT, accumulated_y_skip);
    }
    return WriteBuffer(start_buffer, pos - start_buffer);
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
