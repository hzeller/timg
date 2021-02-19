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

#include "termutils.h"

#include "timg-time.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include <initializer_list>

namespace timg {
// Probe all file descriptors that might be connect to tty for term size.
TermSizeResult DetermineTermSize() {
    for (int fd : { STDOUT_FILENO, STDERR_FILENO, STDIN_FILENO }) {
        struct winsize w = {};
        if (ioctl(fd, TIOCGWINSZ, &w) == 0) {
            return { true, w.ws_col, 2 * (w.ws_row-1) }; // pixels = 2*height
        }
    }
    return { false, -1, -1 };
}

// Read and allocate background color queried from terminal emulator.
// Might leak a file-descriptor when bailing out early. Accepted for brevity.
char* DetermineBackgroundColor() {
    // There might be pipes and redirects.
    // Let's see if we have at least one file descriptor that is connected
    // to our terminal. We can then open that terminal directly RD/WR.
    const char *ttypath;
    for (int fd : { STDOUT_FILENO, STDERR_FILENO, STDIN_FILENO }) {
        if (isatty(fd) && (ttypath = ttyname(fd)) != nullptr)
            break;
    }
    if (!ttypath) return nullptr;
    const int tty_fd = open(ttypath, O_RDWR);
    if (tty_fd < 0) return nullptr;

    struct termios orig_terminal_setting;
    struct termios raw_terminal_setting;

    if (tcgetattr(tty_fd, &orig_terminal_setting) != 0)
        return nullptr;

    // Get terminal into non-blocking 'raw' mode.
    raw_terminal_setting = orig_terminal_setting;

    // There might be terminals that don't support the query. So our minimum
    // expectation is to receive zero bytes.
    raw_terminal_setting.c_cc[VMIN] = 0;
    raw_terminal_setting.c_cc[VTIME] = 0;  // We handle timeout with select()
    raw_terminal_setting.c_iflag = 0;
    raw_terminal_setting.c_lflag &= ~(ICANON | ECHO);

    if (tcsetattr(tty_fd, TCSANOW, &raw_terminal_setting) != 0)
        return nullptr;

    // Many terminals accept a specially formulated escape sequence as 'query'
    // in which the question marks is replaced with the result in the response.
    // The '11' asks for the background color.
    // The response comes back from the terminal as if it was typed in.
    constexpr char query[] = "\033]11;?\033\\";
    constexpr int query_len = sizeof(query) - 1;  // No \nul byte.

    // clang++ does not constexpr strlen(), so manually give constant.
    constexpr int kPrefixLen   = 5;  // strlen("\033]11;")
    constexpr int kColorLen    = 18; // strlen("rgb:1234/1234/1234")
    constexpr int kPostfixLen  = 2;  // strlen("\033\\")
    constexpr int kExpectedResponseLen = kPrefixLen + kColorLen + kPostfixLen;

    if (write(tty_fd, query, query_len) != query_len)
        return nullptr;   // Don't bother. We're best effort here.

    // Reading back the response. It is the query-string with the question mark
    // replaced with the requested information.
    //
    // We have to deal with two situations
    //  * The response might take a while. Typically, this should be only a
    //    few milliseconds, but there can be situations over slow ssh
    //    connections where it takes a little. Allocate some overall budget
    //    of time we allow for this to finish. But not too long as there are
    //    terminals such as cool-retro-term that do not respond to the query
    //    and have to wait for the full time budget.
    //  * The terminal outputs the response as if it was 'typed in', so we
    //    might not only get the response from the terminal itself, but also
    //    characters from user pressing a key while we do our query.
    //    So we might get random bytes before the actual response, possibly
    //    in multiple read calls until we actually get something we expect.
    //    Make sure to accumulate reads in a more spacious buffer than the
    //    expected response and finish once we found what we're looking for.

    const Duration kTimeBudget = Duration::Millis(200);

    char buffer[512];  // The meat of what we expect is kExpectedResponseLen=25
    size_t available = sizeof(buffer);
    char *pos = buffer;
    char *found_start = nullptr;
    timg::Time now = Time::Now();
    const timg::Time deadline = now + kTimeBudget;
    do {
        const int64_t remaining_ns = deadline.nanoseconds() - now.nanoseconds();
        struct timeval timeout{0, remaining_ns / 1000};
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(tty_fd, &read_fds);
        if (select(tty_fd + 1, &read_fds, nullptr, nullptr, &timeout) == 0)
            break;
        const int r = read(tty_fd, pos, available);
        if (r < 0)
            break;
        pos += r;
        available -= r;
        const size_t total_read = pos - buffer;
        if (total_read >= kExpectedResponseLen) { // starts to be interesting
            // We might've gotten some spurious bytes in the beginning, so find
            // where the escape code starts. It is the same beginning as query.
            found_start = (char*)memmem(buffer, total_read, query, kPrefixLen);
            if (found_start &&
                total_read - (found_start - buffer) >= kExpectedResponseLen) {
                break;  // Found start of escape sequence and enough bytes.
            }
        }
        now = Time::Now();
    } while (available && now < deadline);

    tcsetattr(tty_fd, TCSAFLUSH, &orig_terminal_setting);
    close(tty_fd);

    if (!found_start)
        return nullptr;

    char *const start_color = found_start + kPrefixLen;

    // Assemble a standard #rrggbb string; NB, just not overlapping buffer areas
    buffer[0] = '#';
    memcpy(&buffer[1], &start_color[4], 2);
    memcpy(&buffer[3], &start_color[9], 2);
    memcpy(&buffer[5], &start_color[14], 2);
    buffer[7] = '\0';

    return strdup(buffer);
}

}  // namespace timg
