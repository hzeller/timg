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
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include <initializer_list>
#include <functional>

namespace timg {
// Probe all file descriptors that might be connect to tty for term size.
TermSizeResult DetermineTermSize() {
    TermSizeResult result;
    for (int fd : { STDOUT_FILENO, STDERR_FILENO, STDIN_FILENO }) {
        struct winsize w = {};
        if (ioctl(fd, TIOCGWINSZ, &w) == 0) {
            // If we get the size of the terminals in pixels, we can determine
            // what aspect ratio the pixels have and correct if they not 1:2
            // Infer the font size if we have window pixel size available.
            // Do some basic plausibility check here.
            if (w.ws_xpixel >= 2*w.ws_col && w.ws_ypixel >= 4*w.ws_row &&
                w.ws_col > 0 && w.ws_row > 0) {
                result.font_height_px = w.ws_ypixel / w.ws_row;
                result.font_width_px = w.ws_xpixel / w.ws_col;
            }
            result.cols = w.ws_col;
            result.rows = w.ws_row;
            break;
        }
    }
    return result;
}


static struct termios s_orig_terminal_setting;
static int s_tty_fd;
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
using ResponseFinder = std::function<const char*(const char *, size_t len)>;
static const char *QueryTerminal(const char *query,
                                 char *const buffer, const size_t buflen,
                                 Duration time_budget,
                                 ResponseFinder response_found_p) {
    // There might be pipes and redirects.
    // Let's see if we have at least one file descriptor that is connected
    // to our terminal. We can then open that terminal directly RD/WR.
    const char *ttypath;
    for (int fd : { STDOUT_FILENO, STDERR_FILENO, STDIN_FILENO }) {
        if (isatty(fd) && (ttypath = ttyname(fd)) != nullptr)
            break;
    }
    if (!ttypath) return nullptr;
    s_tty_fd = open(ttypath, O_RDWR);
    if (s_tty_fd < 0) return nullptr;

    struct termios raw_terminal_setting;

    if (tcgetattr(s_tty_fd, &s_orig_terminal_setting) != 0)
        return nullptr;

    // Get terminal into non-blocking 'raw' mode.
    raw_terminal_setting = s_orig_terminal_setting;

    // There might be terminals that don't support the query. So our minimum
    // expectation is to receive zero bytes.
    raw_terminal_setting.c_cc[VMIN] = 0;
    raw_terminal_setting.c_cc[VTIME] = 0;  // We handle timeout with select()
    raw_terminal_setting.c_iflag = 0;
    raw_terminal_setting.c_lflag &= ~(ICANON | ECHO);

    if (tcsetattr(s_tty_fd, TCSANOW, &raw_terminal_setting) != 0)
        return nullptr;

    // No matter what happens exiting early for some reason, make sure we
    // leave the terminal in a good state.
    atexit(clean_up_terminal);

    const int query_len = strlen(query);
    if (write(s_tty_fd, query, query_len) != query_len)
        return nullptr;   // Don't bother. We're best effort here.

    const char *found_pos = nullptr;
    size_t available = buflen;
    char *pos = buffer;
    timg::Time now = Time::Now();
    const timg::Time deadline = now + time_budget;
    do {
        const int64_t remaining_ns = deadline.nanoseconds() - now.nanoseconds();
        struct timeval timeout{0, (suseconds_t)(remaining_ns / 1000)};
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(s_tty_fd, &read_fds);
        if (select(s_tty_fd + 1, &read_fds, nullptr, nullptr, &timeout) <= 0)
            break;
        const int r = read(s_tty_fd, pos, available);
        if (r < 0)
            break;
        pos += r;
        available -= r;
        const size_t total_read = pos - buffer;
        found_pos = response_found_p(buffer, total_read);
        if (found_pos)
            break;
        now = Time::Now();
    } while (available && now < deadline);

    clean_up_terminal();

    return found_pos;
}

// Read and allocate background color queried from terminal emulator.
// Might leak a file-descriptor when bailing out early. Accepted for brevity.
char* DetermineBackgroundColor() {
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
    constexpr int kPrefixLen   = 5;  // strlen("\033]11;")
    constexpr int kColorLen    = 18; // strlen("rgb:1234/1234/1234")
    constexpr int kPostfixLen  = 2;  // strlen("\033\\")
    constexpr int kExpectedResponseLen = kPrefixLen + kColorLen + kPostfixLen;

    // Query and testing the response. It is the query-string with the
    // question mark replaced with the requested information.
    //
    // We have to deal with two situations
    //  * The response might take a while (see above in kTimeBudget)
    //    and have to wait for the full time budget.
    //  * The terminal outputs the response as if it was 'typed in', so we
    //    might not only get the response from the terminal itself, but also
    //    characters from user pressing a key while we do our query.
    //    So we might get random bytes before the actual response, possibly
    //    in multiple read calls until we actually get something we expect.
    //    Make sure to accumulate reads in a more spacious buffer than the
    //    expected response and finish once we found what we're looking for.
    char buffer[512];
    const char *found = QueryTerminal(
        query_background_color, buffer, sizeof(buffer), kTimeBudget,
        [query_background_color, kPrefixLen, kExpectedResponseLen]
        (const char *buffer, size_t len) -> const char* {
            if (len < kExpectedResponseLen) return nullptr;
            const char *found;
            // We might've gotten some spurious bytes in the beginning, so find
            // where the escape code starts. It is the same beginning as query.
            found = (const char*)memmem(buffer, len,
                                        query_background_color, kPrefixLen);
            if (found && len - (found - buffer) >= kExpectedResponseLen)
                return found; // Found start of esc sequence and enough bytes.
            return nullptr;
        });

    if (!found) return nullptr;

    const char *const start_color = found + kPrefixLen;

    // Assemble a standard #rrggbb string into our existing buffer.
    // NB, save, as this is not overlapping buffer areas
    buffer[0] = '#';
    memcpy(&buffer[1], &start_color[4], 2);
    memcpy(&buffer[3], &start_color[9], 2);
    memcpy(&buffer[5], &start_color[14], 2);
    buffer[7] = '\0';

    return strdup(buffer);
}

bool GetBoolenEnv(const char *env_name, bool default_value) {
    const char *const value = getenv(env_name);
    if (!value) return default_value;
    return (atoi(value) > 0
            || strcasecmp(value, "on") == 0 || strcasecmp(value, "yes") == 0);
}

float GetFloatEnv(const char *env_var, float default_value) {
    const char *value = getenv(env_var);
    if (!value) return default_value;
    char *err = nullptr;
    float result = strtof(value, &err);
    return (*err == '\0' ? result : default_value);
}

}  // namespace timg
