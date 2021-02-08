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
namespace {
// Use the full canvas to display framebuffer; writes framebuffer directly.
class SingleColumnRenderer final : public Renderer {
public:
    SingleColumnRenderer(timg::TerminalCanvas *canvas,
                         const DisplayOptions &display_opts)
        : Renderer(canvas, display_opts) {}

    WriteFramebufferFun render_cb(const char *title) final {
        // For single column mode, implementation is straightforward
        RenderTitle(title);
        return [this](int x, int dy, const Framebuffer &fb) {
            canvas_->Send(x, dy, fb);
        };
    }

private:
    void RenderTitle(const char *title) {
        if (!title || !options_.show_filename) return;
        std::string tout(title);
        if (options_.center_horizontally) {
            const int start_spaces = (options_.width - tout.length())/2;
            tout.insert(0, std::string(start_spaces, ' '));
        }
        tout += "\n";
        write(STDOUT_FILENO, tout.c_str(), tout.length());
    }
};

// The multi column renderer positions every update in a new column.
// It keeps track which column it is in and if a new row needs to be started
// and uses cursor movements to get to the right place.
class MultiColumnRenderer final : public Renderer {
public:
    MultiColumnRenderer(timg::TerminalCanvas *canvas,
                        const DisplayOptions &display_opts,
                        int cols, int rows)
        : Renderer(canvas, display_opts),
          columns_(cols), column_width_(display_opts.width / cols) {
    }

    ~MultiColumnRenderer() final {
        if (current_column_ != 0) {
            const int down = highest_fb_column_height_ - last_fb_height_;
            if (down > 0) canvas_->MoveCursorDY(down);
        }
    }

    WriteFramebufferFun render_cb(const char *title) final {
        ++current_column_;
        if (current_column_ >= columns_) {
            // If our current image is shorter than the previous one,
            // we need to make up the difference to be ready for the next
            const int down = highest_fb_column_height_ - last_fb_height_;
            if (down > 0) canvas_->MoveCursorDY(down);
            current_column_ = 0;
            highest_fb_column_height_ = 0;
        }

        PrepareTitle(title);
        first_render_call_ = true;
        return [this](int x, int dy, const Framebuffer &fb) {
            const int x_offset = current_column_ * column_width_;
            int y_offset;
            if (first_render_call_) {
                // Unless we're in the first column, we've to move up from last
                y_offset = current_column_ > 0 ? -last_fb_height_ : 0;
            } else {
                y_offset = dy;
            }
            if (options_.show_filename && first_render_call_) {
                // If we have to show the headline, we've to do the move up
                // before Send() would do it.
                // We need to move one more up to be in the 'headline' ypos
                if (y_offset) {
                    // two pixels is one line. So move half the pixels, and
                    // then one line more to reach the place of the title.
                    canvas_->MoveCursorDY((y_offset-1)/2 - 1);
                }
                canvas_->MoveCursorDX(x_offset);
                write(STDOUT_FILENO, title_.c_str(), title_.length());
                y_offset = 0;  // No move by Send() needed anymore.
            }

            canvas_->Send(x + x_offset, y_offset, fb);
            last_fb_height_ = fb.height();
            if (last_fb_height_ > highest_fb_column_height_)
                highest_fb_column_height_ = last_fb_height_;
            first_render_call_ = false;
        };
    }

private:
    void PrepareTitle(const char *title) {
        if (!title || !options_.show_filename) return;
        title_ = title;
        // Columns are often narrow. We might need to trim what we print.
        if ((int)title_.length() > column_width_) {
            title_.replace(0, title_.length() - column_width_ + 3, "...");
        } else if (options_.center_horizontally) {
            const int start_spaces = (column_width_ - title_.length())/2;
            title_.insert(0, std::string(start_spaces, ' '));
        }
        title_ += "\n";
    }

    const int columns_;
    const int column_width_;
    std::string title_;
    bool first_render_call_ = true;
    int current_column_ = -1;
    int highest_fb_column_height_ = 0;  // maximum seen in this row
    int last_fb_height_ = 0;
};

}  // namespace

Renderer::Renderer(timg::TerminalCanvas *canvas,
             const DisplayOptions &display_opts)
    : canvas_(canvas), options_(display_opts) {}

std::unique_ptr<Renderer> Renderer::Create(timg::TerminalCanvas *output,
                                           const DisplayOptions &display_opts,
                                           int cols, int rows) {
    if (cols > 1) {
        return std::make_unique<MultiColumnRenderer>(output, display_opts,
                                                     cols, rows);
    }
    return std::make_unique<SingleColumnRenderer>(output, display_opts);
}

}
