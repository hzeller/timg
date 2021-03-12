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

#include "terminal-canvas.h"
#include "display-options.h"

namespace timg {
// Implements https://iterm2.com/documentation-images.html
class ITerm2GraphicsCanvas final : public TerminalCanvas {
public:
    ITerm2GraphicsCanvas(int fd, const DisplayOptions &opts);
    ~ITerm2GraphicsCanvas() override;

    ssize_t Send(int x, int dy, const Framebuffer &framebuffer) override;

private:
    const DisplayOptions &options_;

    char *EnsureBuffer(int width, int height);

    char *content_buffer_ = nullptr;
    size_t content_buffer_size_ = 0;
};
}  // namespace timg
#endif  // ITERM2_CANVAS_H
