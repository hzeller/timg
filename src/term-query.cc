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
#include <string.h>  // NOLINT for memmem()
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <functional>

#include "timg-time.h"
#include "utils.h"

#define TERM_CSI "\033["

namespace timg {
static struct termios s_orig_terminal_setting;
static int s_tty_fd = -1;
static void clean_up_terminal() {
    if (s_tty_fd < 0) return;
    tcsetattr(s_tty_fd, TCSAFLUSH, &s_orig_terminal_setting);
    close(s_tty_fd);
    s_tty_fd = -1;
}

// Global variable; ok for debug logging cause.
static bool s_log_terminal_queries = false;
void EnableTerminalQueryLogging(bool on) { s_log_terminal_queries = on; }

// Debug print a message and c-escaped data.
static void debug_data(FILE *f, const char *msg, const char *data, size_t len) {
    fprintf(f, "\033[1m%s\033[0m'", msg);
    for (const char *const end = data + len; data < end; ++data) {
        if (*data == '\\') {
            fprintf(f, "\\\\");
        }
        else if (*data < 0x20) {
            fprintf(f, "\\%03o", *data);
        }
        else {
            fprintf(f, "%c", *data);
        }
    }
    fprintf(f, "'");
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
    for (const int fd : {STDOUT_FILENO, STDERR_FILENO, STDIN_FILENO}) {
        if (!isatty(fd)) continue;
        ttypath = ttyname(fd);
        if (ttypath != nullptr) break;
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

    if (tcsetattr(s_tty_fd, TCSANOW, &raw_terminal_setting) != 0) {
        return nullptr;
    }
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
    const timg::Time start    = Time::Now();
    const timg::Time deadline = start + time_budget;
    timg::Time now            = start;
    do {
        struct timeval timeout = (deadline - now).AsTimeval();
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(s_tty_fd, &read_fds);
        if (select(s_tty_fd + 1, &read_fds, nullptr, nullptr, &timeout) <= 0) {
            break;
        }
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
    if (s_log_terminal_queries) {
        debug_data(stderr, "Query: ", query, query_len);
        debug_data(stderr, " Response: ", buffer, pos - buffer);
        fprintf(stderr, " (%dms)\n",
                (int)((timg::Time() - start).nanoseconds() / 1'000'000));
    }
    return found_pos;
}

// Return pointer to substring in "haystack" of known length. The substring
// "s" we look for is nul-terminated.
// Not found is nullptr/false; if found, returns the start of the match.
static const char *find_str(const char *haystack, int64_t len, const char *s) {
    if (len < 0) return nullptr;
    return (const char *)memmem(haystack, len, s, strlen(s));
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
    const char *start_color = QueryTerminal(
        query_background_color, buffer, sizeof(buffer), kTimeBudget,
        [](const char *data, size_t len) -> const char * {
            // We might've gotten some spurious bytes in the beginning from
            // keypresses, so find where the color starts.
            const char *found = find_str(data, len, "rgb:");
            return strchr(found, '\\') ? found : nullptr;
        });

    if (!start_color) return nullptr;

    // Assemble a standard #rrggbb string into global static buffer.
    static char result[8];
    strcpy(result, "#000000");  // Some default.
    result[0] = '#';
    start_color += strlen("rgb:");
    for (int pos = 1; pos < 6 && isxdigit(*start_color); pos += 2) {
        memcpy(&result[pos], start_color, 2);
        start_color = strchr(start_color, '/');  // color separator.
        if (!start_color) return result;
        start_color += 1;
    }
    result[7] = '\0';

    return result;
}

TermGraphicsInfo QuerySupportedGraphicsProtocol() {
    TermGraphicsInfo result{};
    result.preferred_graphics = GraphicsProtocol::kNone;
    const int sixel_env_bits = timg::GetIntEnv("TIMG_SIXEL_NEWLINE_WORKAROUND");
    result.sixel.known_broken_cursor_placement = sixel_env_bits & 0b01;
    result.sixel.full_cell_jump                = sixel_env_bits & 0b10;
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

    // Some terminals with Windows heritage often don't support query of
    // the terminal, but are set TERM_PROGRAM
    const char *const term_program = getenv("TERM_PROGRAM");
    if (term_program) {
        if (strcmp(term_program, "vscode") == 0) {
            result.preferred_graphics = GraphicsProtocol::kIterm2;
            // In case the user chooses sixel.
            result.sixel.known_broken_cursor_placement = true;
        }
        else if (strcmp(term_program, "WarpTerminal") == 0) {
            // At least on Mac and Linux according to #151
            // (Windows support unclear)
            result.preferred_graphics = GraphicsProtocol::kIterm2;
        }
    }

    // Note, there is a kitty protocol way to determine if kitty is supported,
    // unfortunately that escape sequence messes up some other terminals, so
    // we don't do that here. Instead, we match known terminals and then
    // fall back to sixel detection.

    const Duration kTimeBudget = Duration::Millis(250);
    char buffer[512];

    // We send out two queries: one CSI for terminal version detection that
    // is supported at least by the terminals we're interested in. From the
    // returned string we can determine if they are in supported set.
    //
    // This is followed by DSR 5 that is always expected to work.
    // If we only get a response to the innocuous status report request,
    // we don't have a terminal that supports the CSI >q
    constexpr char kTermVersionQuery[] =
        TERM_CSI ">q"   // terminal version query
        TERM_CSI "5n";  // general status report.
    QueryTerminal(kTermVersionQuery, buffer, sizeof(buffer), kTimeBudget,
                  [&result](const char *data, size_t len) {
                      if (find_str(data, len, "iTerm2") ||
                          find_str(data, len, "Konsole 2")) {
                          result.preferred_graphics = GraphicsProtocol::kIterm2;
                      }
                      if (find_str(data, len, "WezTerm")) {
                          result.preferred_graphics = GraphicsProtocol::kIterm2;
                          result.sixel.known_broken_cursor_placement = true;
                      }
                      if (find_str(data, len, "kitty")) {
                          result.preferred_graphics = GraphicsProtocol::kKitty;
                      }
                      if (find_str(data, len, "ghostty")) {
                          result.preferred_graphics = GraphicsProtocol::kKitty;
                      }
                      if (find_str(data, len, "mlterm")) {
                          result.preferred_graphics = GraphicsProtocol::kSixel;
                      }
                      if (find_str(data, len, "XTerm")) {
                          // Don't know yet if supports sixel, will query below
                          result.sixel.known_broken_cursor_placement = true;
                      }
                      if (find_str(data, len, "foot")) {
                          result.preferred_graphics = GraphicsProtocol::kSixel;
                          result.sixel.known_broken_cursor_placement = true;
                      }
                      if (find_str(data, len, "tmux")) {
                          result.in_tmux = true;
                      }
                      if (find_str(data, len, "WindowsTerminal")) {
                          // TODO: check again once name is established
                          // https://github.com/microsoft/terminal/issues/18382
                          result.sixel.known_broken_cursor_placement = true;
                          result.sixel.full_cell_jump                = true;
                      }
                      // We finish once we found the response to DSR5
                      return find_str(data, len, TERM_CSI "0");
                  });
    if (result.preferred_graphics != GraphicsProtocol::kNone) {
        return result;
    }

    // Still not known. Let's see if if we can at determine if this might
    // be sixel as some terminals implement DA1 (primary device attributes)
    // with ";4" in its response if it supports sixel.
    QueryTerminal(TERM_CSI "c", buffer, sizeof(buffer), kTimeBudget,
                  [&result](const char *data, size_t len) {
                      // https://vt100.net/docs/vt510-rm/DA1.html
                      // says CSI ?64 is returned, but not all terminals do
                      // that. So, let's just watch for CSI ?
                      const char *const end    = data + len;
                      constexpr char kExpect[] = TERM_CSI "?";
                      const char *start        = find_str(data, len, kExpect);
                      if (!start) return start;
                      // Look for length - 1 as we expect another char we test
                      // below.
                      const char *found_4 =
                          find_str(start, end - start - 1, ";4");
                      if (found_4 && (found_4[2] == ';' || found_4[2] == 'c')) {
                          result.preferred_graphics = GraphicsProtocol::kSixel;
                      }
                      return data;
                  });
    return result;
}

static bool QueryCellWidthHeight(int *width, int *height) {
    const Duration kTimeBudget      = Duration::Millis(50);
    constexpr char kQuery[]         = TERM_CSI "16t";
    constexpr char kResponseStart[] = TERM_CSI "6;";
    char buffer[512];
    const char *const result = QueryTerminal(
        kQuery, buffer, sizeof(buffer), kTimeBudget,
        [kResponseStart](const char *data, size_t len) -> const char * {
            return find_str(data, len, kResponseStart);
        });
    int w;
    int h;
    if (!result ||
        sscanf(result + strlen(kResponseStart), "%d;%dt", &h, &w) != 2) {
        return false;
    }
    *width  = w;
    *height = h;
    return true;
}

// Probe all file descriptors that might be connect to tty for term size.
TermSizeResult DetermineTermSize() {
    TermSizeResult result;
    for (const int fd : {STDOUT_FILENO, STDERR_FILENO, STDIN_FILENO}) {
        struct winsize w = {};
        if (ioctl(fd, TIOCGWINSZ, &w) != 0) {
            if (s_log_terminal_queries) {
                fprintf(stderr, "ioctl(%d, TIOCGWINSZ) failing.\n", fd);
            }
            continue;
        }
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
            if (s_log_terminal_queries) {
                fprintf(stderr, "No usable TIOCGWINSZ, trying cell query.\n");
            }
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
