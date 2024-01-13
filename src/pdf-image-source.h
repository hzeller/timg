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

#ifndef PDF_SOURCE_H_
#define PDF_SOURCE_H_

#include <memory>
#include <vector>

#include "display-options.h"
#include "image-source.h"
#include "terminal-canvas.h"

namespace timg {
class PDFImageSource final : public ImageSource {
public:
    explicit PDFImageSource(const std::string &filename)
        : ImageSource(filename) {}

    bool LoadAndScale(const DisplayOptions &options, int frame_offset,
                      int frame_count) final;

    void SendFrames(const Duration &duration, int loops,
                    const volatile sig_atomic_t &interrupt_received,
                    const Renderer::WriteFramebufferFun &sink) final;

    std::string FormatTitle(const std::string &format_string) const final;

private:
    int IndentationIfCentered(const timg::Framebuffer &image) const;

    DisplayOptions options_;
    double orig_width_, orig_height_;
    std::vector<std::unique_ptr<timg::Framebuffer>> pages_;
};

}  // namespace timg

#endif  // QOI_SOURCE_H_
