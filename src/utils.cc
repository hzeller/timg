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

#include "utils.h"

#include <strings.h>
#include <unistd.h>

namespace timg {

bool GetBoolenEnv(const char *env_name, bool default_value) {
    const char *const value = getenv(env_name);
    if (!value) return default_value;
    return (atoi(value) > 0 || strcasecmp(value, "on") == 0 ||
            strcasecmp(value, "yes") == 0);
}

float GetFloatEnv(const char *env_var, float default_value) {
    const char *value = getenv(env_var);
    if (!value) return default_value;
    char *err    = nullptr;
    float result = strtof(value, &err);
    return (*err == '\0' ? result : default_value);
}

std::string HumanReadableByteValue(int64_t byte_count) {
    float print_bytes = byte_count;
    const char *unit  = "Bytes";
    if (print_bytes > (10LL << 30)) {
        print_bytes /= (1 << 30);
        unit = "GiB";
    }
    else if (print_bytes > (10 << 20)) {
        print_bytes /= (1 << 20);
        unit = "MiB";
    }
    else if (print_bytes > (10 << 10)) {
        print_bytes /= (1 << 10);
        unit = "KiB";
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f %s", print_bytes, unit);
    return buf;
}

}  // namespace timg
