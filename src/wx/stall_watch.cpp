// SPDX-License-Identifier: GPL-3.0-or-later
#include "stall_watch.h"

#include "guard.h"
#include "plugin_log.h"
#include "wx_util.h"

#include <chrono>
#include <cstdint>

namespace {

std::int64_t steadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

namespace cg {

StallWatch::StallWatch() : timer_(this) {
    Bind(wxEVT_TIMER, &StallWatch::onTick, this);
}

StallWatch::~StallWatch() {
    timer_.Stop();
}

void StallWatch::start() {
    // A second start only restarts the period; the counts of the running session are kept.
    timer_.Start(StallMeter::kPeriodMs); // continuous: wx does not queue missed ticks
}

void StallWatch::stop() {
    timer_.Stop();
    if (meter_.count() > 0)
        logFile(fromUtf8(stallSummary(meter_)));
    meter_.reset();
}

void StallWatch::onTick(wxTimerEvent&) {
    guarded("stall watch", [&] {
        const int ms = meter_.tick(steadyMs());
        if (ms > 0)
            logFile(fromUtf8(stallLine(ms)));
    });
}

} // namespace cg
