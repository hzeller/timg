// -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
// (c) 2020 Henner Zeller <h.zeller@acm.org>
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

#ifndef RENDERER_H_
#define RENDERER_H_

#include <functional>
#include <memory>
#include <string>

#include "display-options.h"
#include "terminal-canvas.h"

namespace timg {
// A renderer for framebuffers. The renderer provides a callback to which
// a framebuffer can be sent; the renderer then takes care of actually
// sending it to the screen, depending on the grid configuration.
class Renderer {
public:
    // A function to write a framebuffer and its offset with timing.
    // This callback is handed over to each ImageSource::SendFrames to
    // emit their content.
    using WriteFramebufferFun =
        std::function<void(int x, int dy, const Framebuffer &fb,
                           SeqType seq_type, Duration end_of_frame)>;

    // Create a renderer that writes to the terminal canvas.
    // The single column vs. multi column are different implementations.
    static std::unique_ptr<Renderer> Create(timg::TerminalCanvas *output,
                                            const DisplayOptions &display_opts,
                                            int cols, int rows,
                                            Duration wait_between_images,
                                            Duration wait_between_rows);

    virtual ~Renderer() {}

    // Create a sink for a new rendering with a title.
    // Returns a callback in which the receiver can send a frambebufer to be
    // rendered, to be called by whoever provides a framebuffer.
    // The returned call can be used to output multiple frames with the usual
    // (x, dy) positioning.
    virtual WriteFramebufferFun render_cb(const std::string &title) = 0;

    // Wait if needed between image sources.
    virtual void MaybeWaitBetweenImageSources() const = 0;

protected:
    Renderer(timg::TerminalCanvas *canvas, const DisplayOptions &display_opts);

    // Trim and potentially title for rendering, obeying the column-width
    // and centering constraints. Adds a newline as this needs to be printed.
    std::string TrimTitle(const std::string &title, int width) const;

    timg::TerminalCanvas *const canvas_;
    const DisplayOptions &options_;
};
}  // namespace timg
#endif  // RENDERER_H_
