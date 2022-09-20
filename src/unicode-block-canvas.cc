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

#define PIXEL_BLOCK_CHARACTER_LEN strlen("\u2584")  // blocks are 3 bytes UTF8

// 24 bit color setting
#define PIXEL_SET_FG_COLOR24 "38;2;"
#define PIXEL_SET_BG_COLOR24 "48;2;"

// 8 bit color setting
#define PIXEL_SET_FG_COLOR8 "38;5;"
#define PIXEL_SET_BG_COLOR8 "48;5;"

#define PIXEL_SET_COLOR_LEN strlen(PIXEL_SET_FG_COLOR24)  // all the same

// Maximum length of the color value sequence
#define ESCAPE_COLOR_MAX_LEN strlen("rrr;ggg;bbb")

// We reset the terminal at the end of a line
#define SCREEN_END_OF_LINE     "\033[0m\n"
#define SCREEN_END_OF_LINE_LEN strlen(SCREEN_END_OF_LINE)

namespace timg {
enum BlockChoice : uint8_t {
    kBackground,
    kTopLeft,
    kTopRight,
    kBotLeft,
    kBotRight,
    kLeftBar,
    kTopLeftBotRight,

    kLowerBlock,  // Depending on user choice, one of these is used.
    kUpperBlock,
};

// Half block rendering:
// Each character on the screen is divided in a top pixel and bottom pixel.
// We use a block character to fill one half with the foreground color,
// the other half is shown as background color.
// Some fonts display the top block worse than the bottom block, so use the
// bottom block by default, but allow to choose.
// Two pixels one stone. Or something.
//
// Quarter block rendering: similar, but more choices, which means we have
// to distribute foreground/background color as averages of the 'real' color.
static constexpr const char *kBlockGlyphs[9] = {
    /*[kBackground] =      */ " ",  // space
    /*[kTopLeft] =         */ "▘",  // U+2598 Quadrant upper left
    /*[kTopRight] =        */ "▝",  // U+259D Quadrant upper right
    /*[kBotLeft] =         */ "▖",  // U+2596 Quadrant lower left
    /*[kBotRight] =        */ "▗",  // U+2597 Quadrant lower right
    /*[kLeftBar] =         */ "▌",  // U+258C Left half block
    /*[kTopLeftBotRight] = */ "▚",  // U+259A Quadrant upper left & lower right

    /*[kLowerBlock] =      */ "▄",  // U+2584 Lower half block
    /*[kUpperBlock] =      */ "▀",  // U+2580 Upper half block
};

UnicodeBlockCanvas::UnicodeBlockCanvas(BufferedWriteSequencer *ws,
                                       bool use_quarter,
                                       bool use_upper_half_block,
                                       bool use_256_color)
    : TerminalCanvas(ws),
      use_quarter_blocks_(use_quarter),
      use_upper_half_block_(use_upper_half_block),
      use_256_color_(use_256_color) {}

UnicodeBlockCanvas::~UnicodeBlockCanvas() {
    free(backing_buffer_);
    free(empty_line_);
}

static char *int_append_with_semicolon(char *buf, uint8_t val);
template <int colorbits>
static inline const char *AnsiSetFG() {
    return colorbits == 8 ? PIXEL_SET_FG_COLOR8 : PIXEL_SET_FG_COLOR24;
}
template <int colorbits>
static inline const char *AnsiSetBG() {
    return colorbits == 8 ? PIXEL_SET_BG_COLOR8 : PIXEL_SET_BG_COLOR24;
}
template <int colorbits>
static char *AnsiWriteColor(char *buf, rgba_t color) {
    static_assert(colorbits == 8 || colorbits == 24, "unsupported color bits");
    if (colorbits == 8)
        return int_append_with_semicolon(buf, color.As256TermColor());

    buf = int_append_with_semicolon(buf, color.r);
    buf = int_append_with_semicolon(buf, color.g);
    return int_append_with_semicolon(buf, color.b);
}

static inline char *str_append(char *pos, const char *value, size_t len) {
    memcpy(pos, value, len);
    return pos + len;
}

// Compare pixels of top and bottom row with backing store (see StoreBacking())
template <int N>
inline bool EqualToBacking(const rgba_t *top, const rgba_t *bottom,
                           const rgba_t *backing) {
    if (N == 1) return *top == backing[0] && *bottom == backing[1];
    return *top == backing[0] && *(top + 1) == backing[1] &&
           *bottom == backing[2] && *(bottom + 1) == backing[3];
}

// Store pixels of top and bottom row into backing store.
template <int N>
inline void StoreBacking(rgba_t *backing, const rgba_t *top,
                         const rgba_t *bottom) {
    if (N == 1) {
        backing[0] = *top;
        backing[1] = *bottom;
    }
    else {
        backing[0] = top[0];
        backing[1] = top[1];
        backing[2] = bottom[0];
        backing[3] = bottom[1];
    }
}

inline bool is_transparent(rgba_t c) { return c.a < 0x60; }

struct UnicodeBlockCanvas::GlyphPick {
    rgba_t fg;
    rgba_t bg;
    BlockChoice block;
};

template <int N>
UnicodeBlockCanvas::GlyphPick UnicodeBlockCanvas::FindBestGlyph(
    const rgba_t *top, const rgba_t *bottom) const {
    if (N == 1) {
        if (*top == *bottom ||
            (is_transparent(*top) && is_transparent(*bottom))) {
            return {*top, *bottom, kBackground};
        }
        if (use_upper_half_block_) return {*top, *bottom, kUpperBlock};
        return {*bottom, *top, kLowerBlock};
    }
    // N == 2
    const LinearColor tl(top[0]);
    const LinearColor tr(top[1]);
    const LinearColor bl(bottom[0]);
    const LinearColor br(bottom[1]);

    // If we're all transparent at the top and/or bottom, the choices
    // we can make for foreground and background are limited.
    // Even though this adds branches, special casing is worthile.
    if (is_transparent(top[0]) && is_transparent(top[1]) &&
        is_transparent(bottom[0]) && is_transparent(bottom[1])) {
        return {bottom[0], top[0], kBackground};
    }
    if (is_transparent(top[0]) && is_transparent(top[1])) {
        return {linear_average({bl, br}).repack(), top[0], kLowerBlock};
    }
    if (is_transparent(bottom[0]) && is_transparent(bottom[1])) {
        return {linear_average({tl, tr}).repack(), bottom[0], kUpperBlock};
    }

    struct Result {
        LinearColor fg, bg;
        BlockChoice block = kBackground;
    } best;
    float best_distance = 1e12;
    for (int b = 0; b < 8; ++b) {
        float d;  // Sum of color distance for each sub-block to average color
        LinearColor fg, bg;
        // We can't fix all the blocks that the user tries to work around
        // with TIMG_USE_UPPER_BLOCK. But fix the half-blocks at least.
        const BlockChoice block =
            (BlockChoice)(b < 7 ? b
                                : (use_upper_half_block_ ? kUpperBlock
                                                         : kLowerBlock));
        // clang-format off
        switch (block) {
        case kBackground:      d = avd(&bg, {tl, tr, bl, br}); fg = bg;   break;
        case kTopLeft:         d = avd(&bg, {tr, bl, br});     fg = tl;   break;
        case kTopRight:        d = avd(&bg, {tl, bl, br});     fg = tr;   break;
        case kBotLeft:         d = avd(&bg, {tl, tr, br});     fg = bl;   break;
        case kBotRight:        d = avd(&bg, {tl, tr, bl});     fg = br;   break;
        case kLeftBar:         d = avd(&bg, {tr, br})+avd(&fg, {tl, bl}); break;
        case kTopLeftBotRight: d = avd(&bg, {tr, bl})+avd(&fg, {tl, br}); break;
        case kLowerBlock:      d = avd(&bg, {tl, tr})+avd(&fg, {bl, br}); break;
        case kUpperBlock:      d = avd(&bg, {bl, br})+avd(&fg, {tl, tr}); break;
        }
        // clang-format on
        if (d < best_distance) {
            best = {fg, bg, block};
            if (d < 1) break;  // Essentially zero.
            best_distance = d;
        }
    }
    return {best.fg.repack(), best.bg.repack(), best.block};
}

// Append two rows of pixels at once.
template <int N, int colorbits>  // Advancing N x-pixels per char
char *UnicodeBlockCanvas::AppendDoubleRow(char *pos, int indent, int width,
                                          const rgba_t *tline,
                                          const rgba_t *bline, bool emit_diff,
                                          int *y_skip) {
    static constexpr char kStartEscape[] = "\033[";
    GlyphPick last                       = {};
    rgba_t last_foreground               = {};
    bool last_fg_unknown                 = true;
    bool last_bg_unknown                 = true;
    int x_skip                           = indent;
    const char *start                    = pos;
    for (int x = 0; x < width;
         x += N, prev_content_it_ += 2 * N, tline += N, bline += N) {
        if (emit_diff && EqualToBacking<N>(tline, bline, prev_content_it_)) {
            ++x_skip;
            continue;
        }

        if (*y_skip) {  // Emit cursor down or newlines, whatever is shorter
            if (*y_skip <= 4) {
                memset(pos, '\n', *y_skip);
                pos += *y_skip;
            }
            else {
                pos += sprintf(pos, SCREEN_CURSOR_DN_FORMAT, *y_skip);
            }
            *y_skip = 0;
        }

        if (x_skip > 0) {
            pos += sprintf(pos, SCREEN_CURSOR_RIGHT_FORMAT, x_skip);
            x_skip = 0;
        }

        const GlyphPick pick = FindBestGlyph<N>(tline, bline);

        bool color_emitted = false;

        // Foreground. Only consider if we're not having background.
        if (pick.block != kBackground &&
            (last_fg_unknown || pick.fg != last_foreground)) {
            // Appending prefix. At this point, it can only be kStartEscape
            pos = str_append(pos, kStartEscape, strlen(kStartEscape));
            pos = str_append(pos, AnsiSetFG<colorbits>(), PIXEL_SET_COLOR_LEN);
            pos = AnsiWriteColor<colorbits>(pos, pick.fg);
            color_emitted   = true;
            last_foreground = pick.fg;
            last_fg_unknown = false;
        }

        // Background
        if (last_bg_unknown || pick.bg != last.bg) {
            if (!color_emitted) {
                pos = str_append(pos, kStartEscape, strlen(kStartEscape));
            }
            if (is_transparent(pick.bg)) {
                // This is best effort and only happens with -b none
                pos = str_append(pos, "49;", 3);  // Reset background color
            }
            else {
                pos = str_append(pos, AnsiSetBG<colorbits>(),
                                 PIXEL_SET_COLOR_LEN);
                pos = AnsiWriteColor<colorbits>(pos, pick.bg);
            }
            color_emitted   = true;
            last_bg_unknown = false;
        }

        if (color_emitted) {
            *(pos - 1) = 'm';  // overwrite semicolon with finish ESC seq.
        }
        if (pick.block == kBackground) {
            *pos++ = ' ';  // Simple background 'block'. One character.
        }
        else {
            pos = str_append(pos, kBlockGlyphs[pick.block],
                             PIXEL_BLOCK_CHARACTER_LEN);
        }
        last = pick;
        StoreBacking<N>(prev_content_it_, tline, bline);
    }

    if (pos == start) {  // Nothing emitted for whole line
        (*y_skip)++;
    }
    else {
        pos = str_append(pos, SCREEN_END_OF_LINE, SCREEN_END_OF_LINE_LEN);
    }

    return pos;
}

void UnicodeBlockCanvas::Send(int x, int dy, const Framebuffer &framebuffer,
                              SeqType seq_type, Duration end_of_frame) {
    const int width          = framebuffer.width();
    const int height         = framebuffer.height();
    char *const start_buffer = RequestBuffers(width, height);
    char *pos                = start_buffer;

    if (dy < 0) MoveCursorDY((dy - 1) / 2);

    pos = AppendPrefixToBuffer(pos);

    if (use_quarter_blocks_) x /= 2;  // That is in character cell units.

    const char *before_image_emission = pos;

    const rgba_t *const pixels = framebuffer.begin();
    const rgba_t *top_row, *bottom_row;

    // If we just got requested to move back where we started the last image,
    // we just need to emit pixels that changed.
    prev_content_it_           = backing_buffer_;
    const bool emit_difference = (x == last_x_indent_) &&
                                 (last_framebuffer_height_ > 0) &&
                                 abs(dy) == last_framebuffer_height_;

    // We are always writing two lines at once with one character, which
    // requires to leave an empty line if the height of the framebuffer is odd.
    // We want to make sure that this empty line is written in natural terminal
    // background color to match the chosen terminal color.
    // Depending on if we use the upper or lower half block character to show
    // pixels, we might need to shift displaying by one pixel to make sure
    // the empty line matches up with the background part of that character.
    // This it the row_offset we calculate here.
    const bool needs_empty_line   = (height % 2 != 0);
    const bool top_optional_blank = !use_upper_half_block_;
    const int row_offset = (needs_empty_line && top_optional_blank) ? -1 : 0;

    int y_skip = 0;
    for (int y = 0; y < height; y += 2) {
        const int row = y + row_offset;
        top_row       = row < 0 ? empty_line_ : &pixels[width * row];
        bottom_row =
            (row + 1) >= height ? empty_line_ : &pixels[width * (row + 1)];

        if (use_256_color_) {
            if (use_quarter_blocks_) {
                pos = AppendDoubleRow<2, 8>(pos, x, width, top_row, bottom_row,
                                            emit_difference, &y_skip);
            }
            else {
                pos = AppendDoubleRow<1, 8>(pos, x, width, top_row, bottom_row,
                                            emit_difference, &y_skip);
            }
        }
        else {
            if (use_quarter_blocks_) {
                pos = AppendDoubleRow<2, 24>(pos, x, width, top_row, bottom_row,
                                             emit_difference, &y_skip);
            }
            else {
                pos = AppendDoubleRow<1, 24>(pos, x, width, top_row, bottom_row,
                                             emit_difference, &y_skip);
            }
        }
    }
    last_framebuffer_height_ = height;
    last_x_indent_           = x;
    if (before_image_emission == pos) {
        // Don't even emit cursor up/dn jump, but make sure to return buffer.
        write_sequencer_->WriteBuffer(start_buffer, 0, seq_type, end_of_frame);
        return;
    }

    if (y_skip) {
        pos += sprintf(pos, SCREEN_CURSOR_DN_FORMAT, y_skip);
    }
    write_sequencer_->WriteBuffer(start_buffer, pos - start_buffer, seq_type,
                                  end_of_frame);
}

char *UnicodeBlockCanvas::RequestBuffers(int width, int height) {
    // Pixels will be variable size depending on if we need to change colors
    // between two adjacent pixels. This is the maximum size they can be.
    static const int max_pixel_size =
        strlen("\033[")                               //
        + PIXEL_SET_COLOR_LEN + ESCAPE_COLOR_MAX_LEN  //
        + 1                                           /* ; */
        + PIXEL_SET_COLOR_LEN + ESCAPE_COLOR_MAX_LEN  //
        + 1                                           /* m */
        + PIXEL_BLOCK_CHARACTER_LEN;
    // Few extra space for number printed in the format.
    static const int opt_cursor_up    = strlen(SCREEN_CURSOR_UP_FORMAT) + 3;
    static const int opt_cursor_right = strlen(SCREEN_CURSOR_RIGHT_FORMAT) + 3;
    const int vertical_characters = (height + 1) / 2;  // two pixels, one glyph
    const size_t content_size =
        opt_cursor_up  // Jump up
        +
        vertical_characters * (opt_cursor_right            // Horizontal jump
                               + width * max_pixel_size    // pixels in one row
                               + SCREEN_END_OF_LINE_LEN);  // Finishing a line.

    // Depending on even/odd situation, we might need one extra row.
    const size_t new_backing = width * (height + 1) * sizeof(rgba_t);
    if (new_backing > backing_buffer_size_) {
        backing_buffer_      = (rgba_t *)realloc(backing_buffer_, new_backing);
        backing_buffer_size_ = new_backing;
    }

    const size_t new_empty = width * sizeof(rgba_t);
    if (new_empty > empty_line_size_) {
        empty_line_      = (rgba_t *)realloc(empty_line_, new_empty);
        empty_line_size_ = new_empty;
        memset(empty_line_, 0x00, empty_line_size_);
    }
    return write_sequencer_->RequestBuffer(content_size);
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
static constexpr digit_convert convert_lookup __attribute__((aligned(64))) = {
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
    "240;241;242;243;244;245;246;247;248;249;250;251;252;253;254;255;"};

// Append decimal representation plus semicolon of given "value" to "buffer".
// Does not \0-terminate. Might write one byte beyond number.
static char *int_append_with_semicolon(char *buffer, uint8_t value) {
    // We cheat a little here: for the beauty of initizliaing the above array
    // with a block of text, we manually aligned the data array to 4 to
    // be able to interpret it as uint-array generating fast accesses like
    //    mov eax, DWORD PTR convert_lookup[0+rax*4]
    // (only slightly invokong undefined behavior with this type punning :) )
    const uint32_t *const four_bytes = (const uint32_t *)convert_lookup.data;
    if (value >= 100) {
        memcpy(buffer, &four_bytes[value], 4);
        return buffer + 4;
    }
    if (value >= 10) {
        memcpy(buffer, &four_bytes[value], 4);  // copy 4 cheaper than 3
        return buffer + 3;
    }
    memcpy(buffer, &four_bytes[value], 2);
    return buffer + 2;
}

}  // namespace timg
