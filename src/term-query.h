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

#ifndef TIMG_TERM_QUERY_H
#define TIMG_TERM_QUERY_H

namespace timg {
// Attempt to determine the background color of current termninal.
// Returns static string if successful or nullptr if not.
const char* QueryBackgroundColor();

// Query the terminal if it supports the Kitty graphics protocol.
bool QueryHasKittyGraphics();

// Query if the terminal supports the iTerm2 graphics.
bool QueryHasITerm2Graphics();
}  // namespace timg

#endif  // TIMG_TERM_QUERY_H
