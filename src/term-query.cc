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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include <functional>
#include <initializer_list>

#include "termutils.h"
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
    if (write(s_tty_fd, query, query_len) != query_len)
        return nullptr;  // Don't bother. We're best effort here.

    const char *found_pos     = nullptr;
    size_t available          = buflen;
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
        found_pos               = response_found_p(buffer, total_read);
        if (found_pos) break;
        now = Time::Now();
    } while (available && now < deadline);

    clean_up_terminal();

    return found_pos;
}

bool QueryHasKittyGraphics() {
    const char *term = getenv("TERM");
    return term && (strcmp(term, "xterm-kitty") == 0);

    // The following would be the corrct query, but the graphics query seems
    // to disturb other terminals such as Konsole (which spits out the graphics
    // query on the screen) or iTerm2 (in which the terminal sometimes puts
    // the query as the window title ?
    // Need to find a less intrusive way. Ideally with a "\033[>q"
    // For now, the above environment variable hack is probably an ok
    // work-around as xterm-kitty is sufficiently unique
#if 0
    const Duration kTimeBudget = Duration::Millis(50);

    // We send out two queries: one to determine if the graphics capabilities
    // are available, and one standard simple DSR 5. If we only get a response
    // to the innocuous status report request, we don't have graphics
    // capability.
    // (Unfortunately, Konsole has a bug, in which it actually spills the
    // query to the terminal. We work around that in main()).
    constexpr char graphics_query[] =
        "\033_Ga=q,i=42,s=1,v=1,t=d,f=24;AAAA\033\\"  // Graphics query
        "\033[5n";                                    // general status report.
    bool found_graphics_response = false;
    char buffer[512];
    QueryTerminal(
        graphics_query, buffer, sizeof(buffer), kTimeBudget,
        [&found_graphics_response](const char *data, size_t len) {
            found_graphics_response = (memmem(data, len, "\033_G", 3) != 0);
            // We finish once we found the response to DSR 5
            return (const char*) memmem(data, len, "\033[0n", 3);
        });
    return found_graphics_response;
#endif
}

bool QueryHasITerm2Graphics() {
    const Duration kTimeBudget = Duration::Millis(250);

    // We send out two queries: one CSI for terminal version detection that
    // is supported at leasht by the terminals we're interested in. From the
    // returned string we can determine if they are in supported set.
    //
    // This is followed by DSR 5 that is always expected to work.
    // If we only get a response to the innocuous status report request,
    // we don't have a terminal that supports the CSI >q
    constexpr char term_query[] =
        "\033[>q"   // terminal version query
        "\033[5n";  // general status report.
    bool has_iterm_graphics = false;
    char buffer[512];
    QueryTerminal(
        term_query, buffer, sizeof(buffer), kTimeBudget,
        [&has_iterm_graphics](const char *data, size_t len) {
            has_iterm_graphics |= (memmem(data, len, "iTerm2", 6) != 0);
            has_iterm_graphics |= (memmem(data, len, "WezTerm", 7) != 0);
            // We finish once we found the response to DSR5
            return (const char *)memmem(data, len, "\033[0n", 3);
        });
    return has_iterm_graphics;
}
}  // namespace timg
