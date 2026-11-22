// SPDX-License-Identifier: GPL-3.0-or-later
#include "frame_timer_watch.h"

namespace cg {

bool frameTimerReviveSupported(int major, int minor) {
    return major == 5 && (minor == 14 || minor == 15);
}

void FrameTimerWatch::chartListUpdated(std::int64_t) {
    armed_ = true;
    beat_ = false;
    posts_ = 0;
}

void FrameTimerWatch::heartbeat(std::int64_t) {
    if (armed_)
        beat_ = true;
}

FrameTimerWatch::Action FrameTimerWatch::check(std::int64_t) {
    if (!armed_)
        return Action::None;
    if (beat_) {
        armed_ = false;
        return Action::None;
    }
    if (posts_ >= kMaxPosts) {
        armed_ = false;
        return Action::GiveUp;
    }
    ++posts_;
    return Action::Post;
}

} // namespace cg
