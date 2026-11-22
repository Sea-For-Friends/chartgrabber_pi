// SPDX-License-Identifier: GPL-3.0-or-later
// Restarts OpenCPN's frame timers after the plugin's chart-list update when they stopped (OpenCPN 5.14 and
// 5.15 stop them there and never restart them). A position fix to the plugin is the heartbeat; when none
// arrives 5 s after the update, one timer event for the main frame runs OpenCPN's own tick handler, which
// restarts the timers. GUI thread only.
#pragma once

#include "frame_timer_watch.h"

#include <wx/event.h>
#include <wx/timer.h>

class FrameTimerRevive : private wxEvtHandler {
public:
    FrameTimerRevive();
    ~FrameTimerRevive();
    FrameTimerRevive(const FrameTimerRevive&) = delete;
    FrameTimerRevive& operator=(const FrameTimerRevive&) = delete;

    void setOpenCpnVersion(int major, int minor); // until called the revive is disabled
    void heartbeat();
    void chartListUpdated();
    void shutdown(); // stops the timers; nothing is posted afterwards

private:
    void onCheck(wxTimerEvent&);
    bool post();

    cg::FrameTimerWatch watch_;
    wxTimer check_;    // one shot, re-armed after each post
    wxTimer reviveId_; // never started: the source of the event's id and type
    bool enabled_ = false;
    bool down_ = false;
};
