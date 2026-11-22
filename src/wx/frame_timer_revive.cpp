// SPDX-License-Identifier: GPL-3.0-or-later
#include "frame_timer_revive.h"

#include "plugin_log.h"

#include <wx/app.h>
#include <wx/window.h>

#include <chrono>
#include <cstdint>

#include <ocpn_plugin.h>

namespace {

std::int64_t steadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

FrameTimerRevive::FrameTimerRevive() : check_(this), reviveId_(this) {
    Bind(wxEVT_TIMER, &FrameTimerRevive::onCheck, this, check_.GetId());
}

FrameTimerRevive::~FrameTimerRevive() {
    check_.Stop();
}

void FrameTimerRevive::setOpenCpnVersion(int major, int minor) {
    enabled_ = cg::frameTimerReviveSupported(major, minor);
}

void FrameTimerRevive::heartbeat() {
    watch_.heartbeat(steadyMs());
}

void FrameTimerRevive::chartListUpdated() {
    if (!enabled_ || down_)
        return;
    watch_.chartListUpdated(steadyMs());
    check_.StartOnce(cg::FrameTimerWatch::kCheckDelayMs);
}

void FrameTimerRevive::shutdown() {
    down_ = true;
    check_.Stop();
    reviveId_.Stop();
}

void FrameTimerRevive::onCheck(wxTimerEvent&) {
    if (down_)
        return;
    switch (watch_.check(steadyMs())) {
    case cg::FrameTimerWatch::Action::Post:
        if (post()) // no main window: the check is not re-armed (the watch gives up)
            check_.StartOnce(cg::FrameTimerWatch::kCheckDelayMs);
        break;
    case cg::FrameTimerWatch::Action::GiveUp:
        cg::logLine("frame timer did not restart");
        break;
    case cg::FrameTimerWatch::Action::None:
        break;
    }
}

// QueueEvent, never ProcessEvent: OpenCPN's handler runs later from the main loop, not in this call stack.
// 6002 is FRAME_TIMER_1 (wxID_HIGHEST + 3) in OpenCPN 5.14 and 5.15; the version gate keeps it to those.
bool FrameTimerRevive::post() {
    wxWindow* frame = wxTheApp ? wxTheApp->GetTopWindow() : nullptr;
    wxWindow* canvas = GetOCPNCanvasWindow();
    if (!frame || !canvas || canvas->GetParent() != frame) {
        cg::logLine("frame timer not restarted: main window not found");
        return false;
    }
    reviveId_.SetOwner(frame, wxID_HIGHEST + 3);
    frame->GetEventHandler()->QueueEvent(new wxTimerEvent(reviveId_));
    cg::logLine("frame timer restarted");
    return true;
}
