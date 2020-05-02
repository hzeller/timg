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

#ifndef TIMG_TIME_H_
#define TIMG_TIME_H_

#include <time.h>

// Type-safe representation of time and duration.
// Inspired by golang and absl.

namespace timg {
class Duration {
public:
    constexpr Duration(const Duration &other) : duration_(other.duration_) {}
    constexpr Duration() : duration_({}) {}

    Duration &operator=(const Duration &other) {
        duration_ = other.duration_;
        return *this;
    }

    static constexpr Duration Millis(long ms) {
        return Duration(ms / 1000, (ms % 1000) * 1000000);
    }
    static constexpr Duration Micros(long usec) {
        return Duration(usec / 1000, (usec % 1000000) * 1000);
    }
    static constexpr Duration Nanos(long nanos) {
        return Duration(nanos / 1000000000, nanos % 1000000000);
    }
    static constexpr Duration InfiniteFuture() {
        return Duration(1000000000, 0);  // a few years; infinite enough :)
    }

    struct timespec duration() const { return duration_; }

private:
    constexpr Duration(long sec, long ns) : duration_({sec, ns}) {}
    struct timespec duration_;
};

class Time {
public:
    static Time Now() { return Time(); }

    Time() { clock_gettime(CLOCK_MONOTONIC, &time_); }
    Time(const Time &other) : time_(other.time_) {}

    bool operator <(const Time &other) const {
        if (time_.tv_sec > other.time_.tv_sec) return false;
        if (time_.tv_sec < other.time_.tv_sec) return true;
        return time_.tv_nsec < other.time_.tv_nsec;
    }

    bool operator >=(const Time &other) const { return !(*this < other); }

    void Add(Duration d) {
        time_.tv_sec += d.duration().tv_sec;
        time_.tv_nsec += d.duration().tv_nsec;
        while (time_.tv_nsec > 1000000000) {
            time_.tv_nsec -= 1000000000;
            time_.tv_sec += 1;
        }
    }

    void WaitUntil() const {
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &time_, nullptr);
    }

private:
    struct timespec time_;
};

Time operator+(const Time &t, Duration d) {
    Time result = t;
    result.Add(d);
    return result;
}

}  // namespace timg

#endif  // TIMG_TIME_H_
