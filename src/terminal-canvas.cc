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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#define SCREEN_CURSOR_UP_FORMAT    "\033[%dA"  // Move cursor up given lines.
#define SCREEN_CURSOR_DN_FORMAT    "\033[%dB"  // Move cursor down given lines.
#define SCREEN_CURSOR_RIGHT_FORMAT "\033[%dC"  // Move cursor right given cols
#define SCREEN_CURSOR_LEFT_FORMAT  "\033[%dD"  // Move cursor left given cols

#define SCREEN_CLEAR "\033c"

// Interestingly, cursor-on does not take effect until the next newline on
// the tested terminals. Not sure why that is, but adding a newline sounds
// like waste of vertical space, so let's not do it here but rather try
// to understand the actual reason why this is happening and fix it then.
#define CURSOR_ON  "\033[?25h"
#define CURSOR_OFF "\033[?25l"

namespace timg {

TerminalCanvas::TerminalCanvas(BufferedWriteSequencer *write_sequencer)
    : write_sequencer_(write_sequencer) {}

TerminalCanvas::~TerminalCanvas() {
    if (!prefix_send_.empty()) {
        // The final 'cursor on' might still be in the buffer.
        char *buf = write_sequencer_->RequestBuffer(prefix_send_.size());
        char *end = AppendPrefixToBuffer(buf);
        write_sequencer_->WriteBuffer(buf, end - buf, SeqType::ControlWrite);
    }
}
void TerminalCanvas::AddPrefixNextSend(const char *data, int len) {
    if (!data || len <= 0) return;
    prefix_send_.append(data, len);
}

char *TerminalCanvas::AppendPrefixToBuffer(char *buffer) {
    if (prefix_send_.empty()) return buffer;
    const size_t len = prefix_send_.length();
    memcpy(buffer, prefix_send_.data(), len);
    prefix_send_.clear();
    return buffer + len;
}

void TerminalCanvas::MoveCursorDY(int rows) {
    if (rows == 0) return;
    char buf[32];
    const size_t len = sprintf(
        buf, rows < 0 ? SCREEN_CURSOR_UP_FORMAT : SCREEN_CURSOR_DN_FORMAT,
        abs(rows));
    AddPrefixNextSend(buf, len);
}

void TerminalCanvas::MoveCursorDX(int cols) {
    if (cols == 0) return;
    char buf[32];
    const size_t len = sprintf(
        buf, cols < 0 ? SCREEN_CURSOR_LEFT_FORMAT : SCREEN_CURSOR_RIGHT_FORMAT,
        abs(cols));
    AddPrefixNextSend(buf, len);
}

void TerminalCanvas::ClearScreen() {
    AddPrefixNextSend(SCREEN_CLEAR, strlen(SCREEN_CLEAR));
}

void TerminalCanvas::CursorOff() {
    AddPrefixNextSend(CURSOR_OFF, strlen(CURSOR_OFF));
}

void TerminalCanvas::CursorOn() {
    // Cursor on after displaying an image should be processed ASAP, so
    // that a Ctrl-C on an image that takes forever to load will leave cursor.
    // TODO: Arguably, Send() should do that at end. AddPostfixNextSend() ?
    const size_t kCursorOnSize = strlen(CURSOR_ON);
    char *buf                  = write_sequencer_->RequestBuffer(kCursorOnSize);
    memcpy(buf, CURSOR_ON, kCursorOnSize);  // NOLINT
    write_sequencer_->WriteBuffer(buf, kCursorOnSize, SeqType::ControlWrite);
}

}  // namespace timg
