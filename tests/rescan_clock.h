// SPDX-License-Identifier: GPL-3.0-or-later
// A steerable clock for the chart-list update throttle: a test member; the real clock comes back when the
// test ends.
#pragma once
#include "job_controller.h"

#include <cstdint>
#include <functional>

class FakeRescanClock {
public:
    FakeRescanClock() : saved_(rescanClockMs()) {
        rescanClockMs() = [this] { return now_; };
    }
    ~FakeRescanClock() { rescanClockMs() = saved_; }
    FakeRescanClock(const FakeRescanClock&) = delete;
    FakeRescanClock& operator=(const FakeRescanClock&) = delete;
    void advance(std::int64_t ms) { now_ += ms; }

private:
    std::function<std::int64_t()> saved_;
    std::int64_t now_ = 1000000; // the throttle has no earlier call: the first request calls at once
};
