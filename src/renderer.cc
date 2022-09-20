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

#include "renderer.h"

#include <string.h>
#include <unistd.h>

namespace timg {
std::string Renderer::TrimTitle(const std::string &title,
                                int requested_width) const {
    std::string result = title;
    // Columns can be too narrow. We might need to trim what we print.
    if ((int)result.length() > requested_width) {
        result.replace(0, result.length() - requested_width + 3, "...");
    }
    else if (options_.center_horizontally) {
        const int start_spaces = (requested_width - result.length()) / 2;
        result.insert(0, std::string(start_spaces, ' '));
    }
    result += "\n";
    return result;
}

namespace {
// Use the full canvas to display framebuffer; writes framebuffer directly.
class SingleColumnRenderer final : public Renderer {
public:
    SingleColumnRenderer(timg::TerminalCanvas *canvas,
                         const DisplayOptions &display_opts)
        : Renderer(canvas, display_opts) {}

    WriteFramebufferFun render_cb(const std::string &title) final {
        // For single column mode, implementation is straightforward
        RenderTitle(title);
        return [this](int x, int dy, const Framebuffer &fb, SeqType seq_type,
                      const Duration &end_of_frame) {
            canvas_->Send(x, dy, fb, seq_type, end_of_frame);
        };
    }

private:
    void RenderTitle(const std::string &title) {
        if (!options_.show_title) return;
        const std::string tout =
            TrimTitle(title, options_.width / options_.cell_x_px);
        canvas_->AddPrefixNextSend(tout.data(), tout.size());
    }
};

// The multi column renderer positions every update in a new column.
// It keeps track which column it is in and if a new row needs to be started
// and uses cursor movements to get to the right place.
class MultiColumnRenderer final : public Renderer {
public:
    MultiColumnRenderer(timg::TerminalCanvas *canvas,
                        const DisplayOptions &display_opts, int cols, int rows)
        : Renderer(canvas, display_opts),
          columns_(cols),
          column_width_(display_opts.width) {}

    ~MultiColumnRenderer() final {
        if (current_column_ != 0) {
            const int down = highest_fb_column_height_ - last_fb_height_;
            if (down > 0) {
                canvas_->MoveCursorDY(down / options_.cell_y_px);
            }
        }
    }

    WriteFramebufferFun render_cb(const std::string &title) final {
        ++current_column_;
        if (current_column_ >= columns_) {
            // If our current image is shorter than the previous one,
            // we need to make up the difference to be ready for the next
            const int down = highest_fb_column_height_ - last_fb_height_;
            if (down > 0) canvas_->MoveCursorDY(down);
            current_column_           = 0;
            highest_fb_column_height_ = 0;
        }

        PrepareTitle(title);
        first_render_call_ = true;
        return [this](int x, int dy, const Framebuffer &fb, SeqType seq_type,
                      const Duration &end_of_frame) {
            const int x_offset = current_column_ * column_width_;
            int y_offset;
            if (first_render_call_) {
                // Unless we're in the first column, we've to move up from last
                y_offset = current_column_ > 0 ? -last_fb_height_ : 0;
            }
            else {
                y_offset = dy;
            }
            if (options_.show_title && first_render_call_) {
                // If we have to show the headline, we've to do the move up
                // before Send() would do it.
                // We need to move one more up to be in the 'headline' ypos
                if (y_offset) {
                    // Move pixels needed for cell, then one more
                    // then one line more to reach the place of the title.
                    const int y_move = (-y_offset + options_.cell_y_px - 1) /
                                       options_.cell_y_px;
                    const int kSpaceForTitle = 1;
                    canvas_->MoveCursorDY(-y_move - kSpaceForTitle);
                }
                canvas_->MoveCursorDX(x_offset / options_.cell_x_px);
                canvas_->AddPrefixNextSend(title_.c_str(), title_.length());
                y_offset = 0;  // No move by Send() needed anymore.
            }

            canvas_->Send(x + x_offset, y_offset, fb, seq_type, end_of_frame);
            last_fb_height_ = fb.height();
            if (last_fb_height_ > highest_fb_column_height_)
                highest_fb_column_height_ = last_fb_height_;
            first_render_call_ = false;
        };
    }

private:
    void PrepareTitle(const std::string &title) {
        if (!options_.show_title) return;
        title_ = TrimTitle(title, column_width_ / options_.cell_x_px);
    }

    const int columns_;
    const int column_width_;
    std::string title_;
    bool first_render_call_       = true;
    int current_column_           = -1;
    int highest_fb_column_height_ = 0;  // maximum seen in this row
    int last_fb_height_           = 0;
};

}  // namespace

Renderer::Renderer(timg::TerminalCanvas *canvas,
                   const DisplayOptions &display_opts)
    : canvas_(canvas), options_(display_opts) {}

std::unique_ptr<Renderer> Renderer::Create(timg::TerminalCanvas *output,
                                           const DisplayOptions &display_opts,
                                           int cols, int rows) {
    if (cols > 1) {
        return std::make_unique<MultiColumnRenderer>(output, display_opts, cols,
                                                     rows);
    }
    return std::make_unique<SingleColumnRenderer>(output, display_opts);
}

}  // namespace timg
