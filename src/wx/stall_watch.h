// SPDX-License-Identifier: GPL-3.0-or-later
// A 100 ms wx timer on the GUI thread that logs each late tick (a stall of the main loop) to the plugin's log
// file. Runs only while Debug log is on (plugin_log.cpp owns it). GUI thread only.
#pragma once

#include "stall_meter.h"

#include <wx/event.h>
#include <wx/timer.h>

namespace cg {

class StallWatch : private wxEvtHandler {
public:
    StallWatch();
    ~StallWatch();
    StallWatch(const StallWatch&) = delete;
    StallWatch& operator=(const StallWatch&) = delete;

    void start();
    void stop(); // writes the summary when a stall was seen, then resets the counts
    bool running() const { return timer_.IsRunning(); }

private:
    void onTick(wxTimerEvent&);

    wxTimer timer_;
    StallMeter meter_;
};

} // namespace cg
