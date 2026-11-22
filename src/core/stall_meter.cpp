// SPDX-License-Identifier: GPL-3.0-or-later
#include "stall_meter.h"

#include <algorithm>
#include <limits>

namespace cg {

int StallMeter::tick(std::int64_t nowMs) {
    // The first tick has no previous one: a gap of 0 is no stall. A backwards step gives a negative gap,
    // which the threshold rejects; the base is re-set to nowMs either way.
    const std::int64_t gap = hasLast_ ? nowMs - last_ : 0;
    hasLast_ = true;
    last_ = nowMs;
    const std::int64_t stall = gap - kPeriodMs;
    if (stall < kThresholdMs)
        return 0;
    // A stall longer than INT_MAX ms needs a 24-day block; clamp instead of overflowing.
    const int ms = static_cast<int>(std::min<std::int64_t>(stall, std::numeric_limits<int>::max()));
    ++count_;
    longest_ = std::max(longest_, ms);
    total_ += ms;
    return ms;
}

void StallMeter::reset() {
    *this = StallMeter();
}

std::string stallLine(int ms) {
    std::string text = "main loop stalled ";
    text += std::to_string(ms);
    text += " ms";
    return text;
}

std::string stallSummary(const StallMeter& m) {
    std::string text = "main loop stalls: ";
    text += std::to_string(m.count());
    text += ", longest ";
    text += std::to_string(m.longestMs());
    text += " ms, total ";
    text += std::to_string(m.totalMs());
    text += " ms";
    return text;
}

} // namespace cg
