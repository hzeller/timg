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

#include "term-query.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include <functional>
#include <initializer_list>

#include "timg-time.h"

namespace timg {
static struct termios s_orig_terminal_setting;
static int s_tty_fd = -1;
static void clean_up_terminal() {
    if (s_tty_fd < 0) return;
    tcsetattr(s_tty_fd, TCSAFLUSH, &s_orig_terminal_setting);
    close(s_tty_fd);
    s_tty_fd = -1;
}

// Send "query" to terminal and wait for response to arrive within
// "time_budget". Use "buffer" with "len" to store results.
// Whenever new data arrives, the caller's "response_found_p" response finder
// is called to determine if we have everything needed. If it returns a
// non-nullptr, QueryTerminal will return with that result before
// "time_budget" is reached.
// Otherwise, returns with nullptr.
// Only one query can be going on in parallel (due to cleanup considerations)
using ResponseFinder = std::function<const char *(const char *, size_t len)>;
static const char *QueryTerminal(const char *query, char *const buffer,
                                 const size_t buflen,
                                 const Duration &time_budget,
                                 const ResponseFinder &response_found_p) {
    // There might be pipes and redirects.
    // Let's see if we have at least one file descriptor that is connected
    // to our terminal. We can then open that terminal directly RD/WR.
    const char *ttypath = nullptr;
    for (int fd : {STDOUT_FILENO, STDERR_FILENO, STDIN_FILENO}) {
        if (isatty(fd) && (ttypath = ttyname(fd)) != nullptr) break;
    }
    if (!ttypath) return nullptr;
    s_tty_fd = open(ttypath, O_RDWR);
    if (s_tty_fd < 0) return nullptr;

    struct termios raw_terminal_setting;

    if (tcgetattr(s_tty_fd, &s_orig_terminal_setting) != 0) return nullptr;

    // Get terminal into non-blocking 'raw' mode.
    raw_terminal_setting = s_orig_terminal_setting;

    // There might be terminals that don't support the query. So our minimum
    // expectation is to receive zero bytes.
    raw_terminal_setting.c_cc[VMIN]  = 0;
    raw_terminal_setting.c_cc[VTIME] = 0;  // We handle timeout with select()
    raw_terminal_setting.c_iflag     = 0;
    raw_terminal_setting.c_lflag &= ~(ICANON | ECHO);

    if (tcsetattr(s_tty_fd, TCSANOW, &raw_terminal_setting) != 0)
        return nullptr;

    // No matter what happens exiting early for some reason, make sure we
    // leave the terminal in a good state.
    atexit(clean_up_terminal);

    const int query_len = strlen(query);
    if (write(s_tty_fd, query, query_len) != query_len) {
        return nullptr;  // Don't bother. We're best effort here.
    }

    const char *found_pos     = nullptr;
    size_t available          = buflen - 1;  // Allow for nul termination.
    char *pos                 = buffer;
    timg::Time now            = Time::Now();
    const timg::Time deadline = now + time_budget;
    do {
        struct timeval timeout = (deadline - now).AsTimeval();
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(s_tty_fd, &read_fds);
        if (select(s_tty_fd + 1, &read_fds, nullptr, nullptr, &timeout) <= 0)
            break;
        const int r = read(s_tty_fd, pos, available);
        if (r < 0) break;
        pos += r;
        available -= r;
        const size_t total_read = pos - buffer;
        *pos                    = '\0';  // nul-terminate for c-functions.
        found_pos               = response_found_p(buffer, total_read);
        if (found_pos) break;
        now = Time::Now();
    } while (available && now < deadline);

    clean_up_terminal();

    return found_pos;
}

// Read background color queried from terminal emulator.
// Might leak a file-descriptor when bailing out early. Accepted for brevity.
const char *QueryBackgroundColor() {
    // The response might take a while. Typically, this should be only a
    // few milliseconds, but there can be situations over slow ssh
    // connections or very slow machines where it takes a little.
    // Allocate some overall budget of time we allow for this to finish.
    // We're running this asynchronously, so we already can start decoding
    // images while this query is still running. Only the first image that
    // actually needs transparency alpha blending would have to wait for the
    // result if it is not there already. No impact on other images.
    //
    // Budget relatively high to accomodate for slow machine/flaky
    // network (testing on a Raspberry Pi Zero W over flaky wireless
    // connection resulted in up to 1.2ish seconds).
    const Duration kTimeBudget = Duration::Millis(1500);

    constexpr char query_background_color[] = "\033]11;?\033\\";
    constexpr size_t kColorLen = 18;  // strlen("rgb:1234/1234/1234")

    // Query and testing the response. It is the query-string with the
    // question mark replaced with the requested information.
    //
    // We have to deal with two situations
    //  * The response might take a while (see above in kTimeBudget)
    //    and have to wait for the full time budget.
    //  * Unfortunately, we can't shorten that time with the trick we do
    //    below with a DSR 5 dummy query: turns out that alacritty answers
    //    these out-of-order https://github.com/alacritty/alacritty/issues/4872
    //  * The terminal outputs the response as if it was 'typed in', so we
    //    might not only get the response from the terminal itself, but also
    //    characters from user pressing a key while we do our query.
    //    So we might get random bytes before the actual response, possibly
    //    in multiple read calls until we actually get something we expect.
    //    Make sure to accumulate reads in a more spacious buffer than the
    //    expected response and finish once we found what we're looking for.
    char buffer[512];
    const char *const start_color = QueryTerminal(
        query_background_color, buffer, sizeof(buffer), kTimeBudget,
        [](const char *data, size_t len) -> const char * {
            // We might've gotten some spurious bytes in the beginning from
            // keypresses, so find where the color starts.
            const char *found = (const char *)memmem(data, len, "rgb:", 4);
            if (found && len - (found - data) > kColorLen)  // at least 1 more
                return found;  // Found start of color sequence and enough
                               // bytes.
            return nullptr;
        });

    if (!start_color) return nullptr;

    // Assemble a standard #rrggbb string into global static buffer.
    static char result[8];
    result[0] = '#';
    memcpy(&result[1], &start_color[4], 2);
    memcpy(&result[3], &start_color[9], 2);
    memcpy(&result[5], &start_color[14], 2);
    result[7] = '\0';

    return result;
}

static inline bool contains(const char *data, size_t len, const char *str) {
    return memmem(data, len, str, strlen(str)) != nullptr;
}

TermGraphicsInfo QuerySupportedGraphicsProtocol() {
    TermGraphicsInfo result;
    result.preferred_graphics                  = GraphicsProtocol::kNone;
    result.known_broken_sixel_cursor_placement = false;
    result.in_tmux                             = false;

    // Environment variables can be changed, so guesses from environment
    // variables are just that: guesses.
    // They will help as a fallback if the [>q query does not work.
    // Only testing environment variables with very specific content.

    const char *const term = getenv("TERM");
    if (term && ((strcmp(term, "xterm-kitty") == 0) ||
                 (strcmp(term, "xterm-ghostty") == 0))) {
        result.preferred_graphics = GraphicsProtocol::kKitty;
        // Fall through, as we still have to determine if we're in tmux
    }

    // vscode doesn't provide a way to query the terminal, but can do
    // iterm graphics.
    const char *const term_program = getenv("TERM_PROGRAM");
    if (term_program && strcmp(term_program, "vscode") == 0) {
        result.preferred_graphics = GraphicsProtocol::kIterm2;
        // In case the user chooses sixel.
        result.known_broken_sixel_cursor_placement = true;
    }

    const Duration kTimeBudget = Duration::Millis(250);

    // We send out two queries: one CSI for terminal version detection that
    // is supported at least by the terminals we're interested in. From the
    // returned string we can determine if they are in supported set.
    //
    // This is followed by DSR 5 that is always expected to work.
    // If we only get a response to the innocuous status report request,
    // we don't have a terminal that supports the CSI >q
    constexpr char term_query[] =
        "\033[>q"   // terminal version query
        "\033[5n";  // general status report.
    char buffer[512];
    QueryTerminal(term_query, buffer, sizeof(buffer), kTimeBudget,
                  [&result](const char *data, size_t len) {
                      if (contains(data, len, "iTerm2") ||
                          contains(data, len, "Konsole 2")) {
                          result.preferred_graphics = GraphicsProtocol::kIterm2;
                      }
                      if (contains(data, len, "WezTerm")) {
                          result.preferred_graphics = GraphicsProtocol::kIterm2;
                          result.known_broken_sixel_cursor_placement = true;
                      }
                      if (contains(data, len, "kitty")) {
                          result.preferred_graphics = GraphicsProtocol::kKitty;
                      }
                      if (contains(data, len, "ghostty")) {
                          result.preferred_graphics = GraphicsProtocol::kKitty;
                      }
                      if (contains(data, len, "mlterm")) {
                          result.preferred_graphics = GraphicsProtocol::kSixel;
                      }
                      if (contains(data, len, "XTerm")) {
                          result.preferred_graphics = GraphicsProtocol::kSixel;
                          result.known_broken_sixel_cursor_placement = true;
                      }
                      if (contains(data, len, "foot")) {
                          result.preferred_graphics = GraphicsProtocol::kSixel;
                          result.known_broken_sixel_cursor_placement = true;
                      }
                      if (contains(data, len, "tmux")) {
                          result.in_tmux = true;
                      }
                      // We finish once we found the response to DSR5
                      return (const char *)memmem(data, len, "\033[0n", 3);
                  });
    return result;
}

static bool QueryCellWidthHeight(int *width, int *height) {
    const Duration kTimeBudget      = Duration::Millis(50);
    constexpr char query[]          = "\033[16t";
    constexpr char response_start[] = "\033[6;";
    char buffer[512];
    const char *const result = QueryTerminal(
        query, buffer, sizeof(buffer), kTimeBudget,
        [response_start](const char *data, size_t len) -> const char * {
            return (const char *)memmem(data, len, response_start,
                                        strlen(response_start));
        });
    int w, h;
    if (!result ||
        sscanf(result + strlen(response_start), "%d;%dt", &h, &w) != 2) {
        return false;
    }
    *width  = w;
    *height = h;
    return true;
}

// Probe all file descriptors that might be connect to tty for term size.
TermSizeResult DetermineTermSize() {
    TermSizeResult result;
    for (int fd : {STDOUT_FILENO, STDERR_FILENO, STDIN_FILENO}) {
        struct winsize w = {};
        if (ioctl(fd, TIOCGWINSZ, &w) != 0) continue;
        // If we get the size of the terminals in pixels, we can determine
        // what aspect ratio the pixels. This is also needed to
        // jump up the exact number of character cells needed for animations
        // and grid display.
        //
        // If TIOCGWINSZ does not return ws_xpixel/ws_ypixel, we attempt to
        // query the terminal.
        if (w.ws_xpixel >= 2 * w.ws_col && w.ws_ypixel >= 4 * w.ws_row &&
            w.ws_col > 0 && w.ws_row > 0) {
            // Infer the font size if we have window pixel size available
            // after a plausibility check indicates that values look good.
            result.font_width_px  = w.ws_xpixel / w.ws_col;
            result.font_height_px = w.ws_ypixel / w.ws_row;
        }
        else {
            // Alright, TIOCGWINSZ did not return the terminal size, let's
            // see if it reports character cell size otherwise
            QueryCellWidthHeight(&result.font_width_px, &result.font_height_px);
        }
        result.cols = w.ws_col;
        result.rows = w.ws_row;
        break;
    }
    return result;
}

}  // namespace timg
