// -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
// (c) 2021 Henner Zeller <h.zeller@acm.org>
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

#ifndef KITTY_CANVAS_H
#define KITTY_CANVAS_H

#include "display-options.h"
#include "terminal-canvas.h"
#include "thread-pool.h"

namespace timg {
// Implements https://sw.kovidgoyal.net/kitty/graphics-protocol.html
class KittyGraphicsCanvas final : public TerminalCanvas {
public:
    KittyGraphicsCanvas(BufferedWriteSequencer *ws, ThreadPool *thread_pool,
                        bool tmux_passthrough_needed,
                        const DisplayOptions &opts);

    int cell_height_for_pixels(int pixels) const final;

    void Send(int x, int dy, const Framebuffer &framebuffer,
              SeqType sequence_type, Duration end_of_frame) override;

private:
    const DisplayOptions &options_;
    const bool tmux_passthrough_needed_;
    ThreadPool *const executor_;

    char *RequestBuffer(int width, int height);
};
}  // namespace timg
#endif  // KITTY_CANVAS_H
