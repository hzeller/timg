// -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
// (c) 2023 Henner Zeller <h.zeller@acm.org>
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

#ifndef SIXEL_CANVAS_H
#define SIXEL_CANVAS_H

#include "buffered-write-sequencer.h"
#include "display-options.h"
#include "framebuffer.h"
#include "term-query.h"
#include "terminal-canvas.h"
#include "timg-time.h"

namespace timg {
class ThreadPool;

class SixelCanvas final : public TerminalCanvas {
public:
    SixelCanvas(BufferedWriteSequencer *ws, ThreadPool *thread_pool,
                const SixelOptions &sixel_options,
                const DisplayOptions &display_opts);

    int cell_height_for_pixels(int pixels) const final;

    void Send(int x, int dy, const Framebuffer &framebuffer,
              SeqType sequence_type, Duration end_of_frame) override;

private:
    const DisplayOptions &options_;
    const bool full_cell_jump_;
    ThreadPool *const executor_;
    const char *cursor_move_before_;
    const char *cursor_move_after_;
};
}  // namespace timg
#endif  // SIXEL_CANVAS_H
