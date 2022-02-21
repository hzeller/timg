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

#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
// Type-safe representation of time and duration.
// Inspired by golang and absl.

namespace timg {
class Duration {
public:
    constexpr Duration(const Duration &other) : duration_(other.duration_) {}
    constexpr Duration() : duration_({0, 0}) {}

    Duration &operator=(const Duration &other) {
        duration_ = other.duration_;
        return *this;
    }

    bool operator==(const Duration &other) const {
        return (duration_.tv_sec == other.duration_.tv_sec &&
                duration_.tv_nsec == other.duration_.tv_nsec);
    }

    bool operator<(const Duration &other) const {
        if (duration_.tv_sec > other.duration_.tv_sec) return false;
        if (duration_.tv_sec < other.duration_.tv_sec) return true;
        return duration_.tv_nsec < other.duration_.tv_nsec;
    }
    bool operator>(const Duration &other) const {
        if (duration_.tv_sec > other.duration_.tv_sec) return true;
        if (duration_.tv_sec < other.duration_.tv_sec) return false;
        return duration_.tv_nsec > other.duration_.tv_nsec;
    }

    static constexpr Duration Millis(int64_t ms) {
        return Duration(ms / 1000, (ms % 1000) * 1000000);
    }
    static constexpr Duration Micros(int64_t usec) {
        return Duration(usec / 1000, (usec % 1000000) * 1000);
    }
    static constexpr Duration Nanos(int64_t nanos) {
        return Duration(nanos / 1000000000, nanos % 1000000000);
    }
    static constexpr Duration InfiniteFuture() {
        return Duration(1000000000, 0);  // a few years; infinite enough :)
    }

    struct timespec AsTimespec() const {
        return duration_;
    }
    struct timeval AsTimeval() const {
        return {duration_.tv_sec, (suseconds_t)(duration_.tv_nsec / 1000)};
    }

    int64_t nanoseconds() const {
        return (int64_t)duration_.tv_sec * 1000000000 + duration_.tv_nsec;
    }

    bool is_zero() const {
        return duration_.tv_sec <= 0 && duration_.tv_nsec == 0;
    }

    void Add(const Duration &d) {
        duration_.tv_sec += d.duration_.tv_sec;
        duration_.tv_nsec += d.duration_.tv_nsec;
        while (duration_.tv_nsec > 1000000000) {
            duration_.tv_nsec -= 1000000000;
            duration_.tv_sec += 1;
        }
    }

private:
    constexpr Duration(time_t sec, long ns) : duration_({sec, ns}) {}  // NOLINT
    struct timespec duration_;
};

// Calculate a value per second.
inline float operator/(float value, const Duration &d) {
    return 1e9 * value / d.nanoseconds();
}

class Time {
public:
    static Time Now() { return Time(); }

    Time() {
#ifdef CLOCK_MONOTONIC
        clock_gettime(CLOCK_MONOTONIC, &time_);
#else
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        time_.tv_sec  = tv.tv_sec;
        time_.tv_nsec = 1000L * (long)tv.tv_usec;
#endif
    }

    Time(const Time &other) : time_(other.time_) {}

    inline int64_t nanoseconds() const {
        return (int64_t)time_.tv_sec * 1000000000 + time_.tv_nsec;
    }

    Duration operator-(const Time &other) const {
        return Duration::Nanos(nanoseconds() - other.nanoseconds());
    }

    Time &operator=(const Time &other) {
        time_ = other.time_;
        return *this;
    }

    bool operator<(const Time &other) const {
        if (time_.tv_sec > other.time_.tv_sec) return false;
        if (time_.tv_sec < other.time_.tv_sec) return true;
        return time_.tv_nsec < other.time_.tv_nsec;
    }

    bool operator>=(const Time &other) const { return !(*this < other); }

    void Add(const Duration &d) {
        time_.tv_sec += d.AsTimespec().tv_sec;
        time_.tv_nsec += d.AsTimespec().tv_nsec;
        while (time_.tv_nsec > 1000000000) {
            time_.tv_nsec -= 1000000000;
            time_.tv_sec += 1;
        }
    }

    void WaitUntil() const {
#if defined(CLOCK_MONOTONIC) && defined(TIMER_ABSTIME) && !defined(__OpenBSD__)
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &time_, nullptr);
#else
        Time now;
        const int64_t ns = nanoseconds() - now.nanoseconds();
        if (ns > 0) usleep(ns / 1000);
#endif
    }

private:
    struct timespec time_;
};

inline Time operator+(const Time &t, const Duration &d) {
    Time result = t;
    result.Add(d);
    return result;
}

}  // namespace timg

#endif  // TIMG_TIME_H_
