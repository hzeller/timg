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

#ifndef TERMINAL_CANVAS_H_
#define TERMINAL_CANVAS_H_

#include <stddef.h>
#include <sys/types.h>

#include <string>

#include "buffered-write-sequencer.h"
#include "framebuffer.h"

namespace timg {

// Canvas that can send a framebuffer to a terminal.
class TerminalCanvas {
public:
    // Create a terminal canvas, sending to given file-descriptor.
    explicit TerminalCanvas(BufferedWriteSequencer *write_sequencer);
    TerminalCanvas(const TerminalCanvas &) = delete;
    virtual ~TerminalCanvas();

    virtual int cell_height_for_pixels(int pixels) const = 0;

    // Send frame to terminal. Move to xposition (relative to the left
    // of the screen, and delta y (relative to the current position) first.
    virtual void Send(int x, int dy, const Framebuffer &framebuffer,
                      SeqType sequence_type, Duration end_of_frame) = 0;

    // The following methods add content that is emitted before the next Send()

    void AddPrefixNextSend(const char *data, int len);

    void ClearScreen();
    void CursorOff();
    void CursorOn();

    void MoveCursorDY(int rows);  // -: up^, +: downV
    void MoveCursorDX(int cols);  // -: <-left, +: right->

protected:
    char *AppendPrefixToBuffer(char *buffer);

    BufferedWriteSequencer *const write_sequencer_;  // not owned

private:
    std::string prefix_send_;
};
}  // namespace timg

#endif  // TERMINAL_CANVAS_H_
