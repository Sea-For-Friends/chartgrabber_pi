// SPDX-License-Identifier: GPL-3.0-or-later
// Decides when OpenCPN's frame timers need a restart after a chart-list update. No wx.
#pragma once

#include <cstdint>

namespace cg {

// OpenCPN versions whose FRAME_TIMER_1 id (wxID_HIGHEST + 3) and OnFrameTimer1 restart were checked in
// source: 5.14 and 5.15. Anything else (5.10, 5.12, 5.16+, 6.x, unknown): false.
bool frameTimerReviveSupported(int major, int minor);

class FrameTimerWatch {
public:
    static constexpr int kCheckDelayMs = 5000; // a slow paint on a Pi can delay one tick by more than 1 s
    static constexpr int kMaxPosts = 3;
    enum class Action { None, Post, GiveUp };

    void chartListUpdated(std::int64_t nowMs); // arms; posts counted from 0
    void heartbeat(std::int64_t nowMs);        // a position fix arrived
    // The check fired: None = a heartbeat came after the update or the last post (disarmed);
    // Post = no heartbeat, fewer than kMaxPosts posts so far (stays armed, the caller re-arms the check);
    // GiveUp = kMaxPosts posted without a heartbeat (disarmed).
    Action check(std::int64_t nowMs);
    bool armed() const { return armed_; }

private:
    bool armed_ = false;
    bool beat_ = false; // a heartbeat since the update or the last post
    int posts_ = 0;
};

} // namespace cg
