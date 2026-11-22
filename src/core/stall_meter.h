// SPDX-License-Identifier: GPL-3.0-or-later
// Measures how late a periodic timer ticks: a late tick means the GUI thread was busy for that long. No wx.
#pragma once

#include <cstdint>
#include <string>

namespace cg {

class StallMeter {
public:
    static constexpr int kPeriodMs = 100;    // the timer period
    static constexpr int kThresholdMs = 250; // lateness that counts as a stall

    // nowMs: a steady clock in ms. Returns the stall in ms (gap - kPeriodMs) when that is >= kThresholdMs,
    // else 0. The first tick after construction or reset() only stores the time and returns 0. A backwards
    // clock step counts as no stall and re-bases the time.
    int tick(std::int64_t nowMs);
    int count() const { return count_; }            // stalls since reset
    int longestMs() const { return longest_; }      // longest stall since reset
    std::int64_t totalMs() const { return total_; } // sum of the stalls since reset
    void reset();

private:
    bool hasLast_ = false;
    std::int64_t last_ = 0;
    int count_ = 0;
    int longest_ = 0;
    std::int64_t total_ = 0;
};

std::string stallLine(int ms);                 // "main loop stalled 1234 ms"
std::string stallSummary(const StallMeter& m); // "main loop stalls: 3, longest 15012 ms, total 18000 ms"

} // namespace cg
