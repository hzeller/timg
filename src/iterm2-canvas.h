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

#ifndef ITERM2_CANVAS_H
#define ITERM2_CANVAS_H

#include "display-options.h"
#include "terminal-canvas.h"
#include "thread-pool.h"

namespace timg {
// Implements https://iterm2.com/documentation-images.html
class ITerm2GraphicsCanvas final : public TerminalCanvas {
public:
    ITerm2GraphicsCanvas(BufferedWriteSequencer *ws, ThreadPool *thread_pool,
                         const DisplayOptions &opts);

    int cell_height_for_pixels(int pixels) const final;

    void Send(int x, int dy, const Framebuffer &framebuffer,
              SeqType sequence_type, Duration end_of_frame) override;

private:
    const DisplayOptions &options_;
    ThreadPool *const executor_;

    char *RequestBuffer(int width, int height);
};
}  // namespace timg
#endif  // ITERM2_CANVAS_H
