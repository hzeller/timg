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

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
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

// Read and allocate background color
char* DetermineBackgroundColor() {
    // There might be redirects.
    // Let's see if we have at least one file descriptor that is connected
    // to our terminal. We can then open that terminal directly.
    const char *ttypath;
    for (int fd : { STDOUT_FILENO, STDERR_FILENO, STDIN_FILENO }) {
        if (isatty(fd) && (ttypath = ttyname(fd)) != nullptr)
            break;
    }
    if (!ttypath) return nullptr;
    const int tty_fd = open(ttypath, O_RDWR);
    if (tty_fd < 0) return nullptr;

    struct termios orig;
    struct termios raw;

    if (tcgetattr(tty_fd, &orig) != 0) return nullptr;

    raw = orig;
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    raw.c_iflag = 0;
    raw.c_lflag &= ~(ICANON | ECHO);

    if (tcsetattr(tty_fd, TCSANOW, &raw) != 0) return nullptr;

    const int prefix_len = 5;   // "\33]11;"
    const int postfix_len = 2;  // "\33\\"
    const char query[] = "\033]11;?\033\\";
    const int query_len = sizeof(query) - 1;  // No \nul byte.
    if (write(tty_fd, query, query_len) != query_len)
        return nullptr;   // Don't bother. We're best effort here.

    char input[128];
    const int r = read(tty_fd, input, sizeof(input));
    tcsetattr(tty_fd, TCSAFLUSH, &orig);

    const int expected_response_len =
        prefix_len + strlen("rgb:1234/1234/1234") + postfix_len;
    if (r < expected_response_len)
        return nullptr;
    if (memcmp(input, query, prefix_len) != 0)
        return nullptr;
    if (memcmp(input + r - postfix_len, query + sizeof(query)-1 - postfix_len,
               postfix_len) != 0)
        return nullptr;
    input[r - postfix_len] = '\0';
    close(tty_fd);
    return strdup(input + prefix_len);
}

}  // namespace timg
