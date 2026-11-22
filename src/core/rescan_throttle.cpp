// SPDX-License-Identifier: GPL-3.0-or-later
#include "rescan_throttle.h"

#include <algorithm>

namespace cg {

bool RescanThrottle::gapOver(std::int64_t nowMs) const {
    return !last_ || nowMs < *last_ || nowMs - *last_ >= kMinGapMs;
}

RescanThrottle::Decision RescanThrottle::request(std::int64_t nowMs, bool asyncDb, bool optionsShown) {
    pending_ = true;
    if (optionsShown)
        return Decision::Wait;
    return (!asyncDb || gapOver(nowMs)) ? Decision::CallNow : Decision::Wait;
}

RescanThrottle::Decision RescanThrottle::due(std::int64_t nowMs, bool asyncDb, bool optionsShown) {
    if (!pending_ || optionsShown)
        return Decision::Wait;
    return (!asyncDb || gapOver(nowMs)) ? Decision::CallNow : Decision::Wait;
}

void RescanThrottle::called(std::int64_t nowMs) {
    pending_ = false;
    last_ = nowMs;
}

std::int64_t RescanThrottle::waitMs(std::int64_t nowMs, bool optionsShown) const {
    if (!pending_)
        return 0;
    if (optionsShown)
        return kOptionsPollMs;
    return gapOver(nowMs) ? 0 : std::max<std::int64_t>(1, *last_ + kMinGapMs - nowMs);
}

} // namespace cg
