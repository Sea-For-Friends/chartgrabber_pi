// SPDX-License-Identifier: GPL-3.0-or-later
// The timer event the plugin queues to OpenCPN's main frame after a chart-list update when no position fix
// arrives. The real one-shot timer runs (5 s per check), so a test waits in real time. A frame that is the
// canvas' parent and the application's top window stands for OpenCPN's main frame.
#include "frame_timer_revive.h"
#include "log_capture.h"
#include "ocpn_fakes.h"
#include "wx_env.h"

#include <gtest/gtest.h>

#include <wx/app.h>
#include <wx/frame.h>
#include <wx/panel.h>
#include <wx/timer.h>
#include <wx/utils.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

class FrameTimerReviveTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!wxTheApp || !wxTheApp->IsGUI())
            GTEST_SKIP() << "no display: the frame timer revive test was NOT run";
        oldTop_ = wxTheApp->GetTopWindow();
        frame_ = new wxFrame(nullptr, wxID_ANY, "main frame");
        wxTheApp->SetTopWindow(frame_);
        canvas_ = new wxPanel(frame_);
        ocpn_fake::canvasWindow = canvas_;
        frame_->Bind(wxEVT_TIMER, &FrameTimerReviveTest::onFrameTimer, this, wxID_HIGHEST + 3);
        revive_ = std::make_unique<FrameTimerRevive>();
    }
    void TearDown() override {
        if (!frame_)
            return;
        revive_.reset();
        ocpn_fake::canvasWindow = nullptr;
        wxTheApp->SetTopWindow(oldTop_);
        frame_->Destroy();
        wxTheApp->ProcessPendingEvents();
    }
    void onFrameTimer(wxTimerEvent& ev) {
        ids_.push_back(ev.GetId());
        types_.push_back(ev.GetEventType());
        restartedLinesSeen_.push_back(count("frame timer restarted"));
    }
    int count(const char* text) const {
        return static_cast<int>(
            std::count_if(log_.lines.begin(), log_.lines.end(),
                          [&](const std::string& l) { return l.find(text) != std::string::npos; }));
    }
    static void pumpFor(int ms) {
        const auto end = Clock::now() + std::chrono::milliseconds(ms);
        while (Clock::now() < end) {
            wxTheApp->Yield(true);
            wxMilliSleep(5);
        }
    }
    bool pumpUntil(const std::function<bool()>& done, int timeoutMs) {
        const auto end = Clock::now() + std::chrono::milliseconds(timeoutMs);
        while (!done()) {
            if (Clock::now() >= end)
                return false;
            wxTheApp->Yield(true);
            wxMilliSleep(5);
        }
        return true;
    }

    LogCapture log_;
    wxWindow* oldTop_ = nullptr;
    wxFrame* frame_ = nullptr;
    wxPanel* canvas_ = nullptr;
    std::unique_ptr<FrameTimerRevive> revive_;
    std::vector<int> ids_;
    std::vector<wxEventType> types_;
    std::vector<int> restartedLinesSeen_; // "frame timer restarted" lines in the log when the handler ran
};

// Three posts 5 s apart, each one timer event with id 6002 (FRAME_TIMER_1) and type wxEVT_TIMER that runs
// from the queue after the post returned (the log line comes after the queueing), then one give-up line, no
// fourth.
TEST_F(FrameTimerReviveTest, NoHeartbeatQueuesThreeTimerEventsThenGivesUp) {
    revive_->setOpenCpnVersion(5, 14);
    revive_->chartListUpdated();
    wxTheApp->ProcessPendingEvents();
    EXPECT_TRUE(ids_.empty());
    ASSERT_TRUE(pumpUntil([&] { return ids_.size() == 1; }, 7000));
    EXPECT_EQ(ids_[0], 6002);
    EXPECT_EQ(types_[0], wxEVT_TIMER);
    // queued: the handler ran after post() wrote its line; a direct call would have run it before
    EXPECT_EQ(restartedLinesSeen_[0], 1);
    ASSERT_TRUE(pumpUntil([&] { return ids_.size() == 3; }, 12000));
    EXPECT_EQ(count("did not restart"), 0);
    ASSERT_TRUE(pumpUntil([&] { return count("frame timer did not restart") == 1; }, 7000));
    pumpFor(1000);
    EXPECT_EQ(ids_.size(), 3u);
    EXPECT_EQ(count("frame timer restarted"), 3);
    EXPECT_EQ(count("frame timer did not restart"), 1);
    for (int id : ids_)
        EXPECT_EQ(id, 6002);
}

TEST_F(FrameTimerReviveTest, AHeartbeatBeforeTheCheckMeansNoPost) {
    revive_->setOpenCpnVersion(5, 15);
    revive_->chartListUpdated();
    pumpFor(1000);
    revive_->heartbeat();
    pumpFor(5000);
    EXPECT_TRUE(ids_.empty());
    EXPECT_EQ(count("frame timer"), 0);
}

TEST_F(FrameTimerReviveTest, AHeartbeatAfterTheFirstPostStopsTheNextOnes) {
    revive_->setOpenCpnVersion(5, 14);
    revive_->chartListUpdated();
    ASSERT_TRUE(pumpUntil([&] { return ids_.size() == 1; }, 7000));
    revive_->heartbeat();
    pumpFor(5500);
    EXPECT_EQ(ids_.size(), 1u);
    EXPECT_EQ(count("did not restart"), 0);
}

// The canvas' parent is not the top window: nothing is queued, the log says so, and the watch is not
// re-armed.
TEST_F(FrameTimerReviveTest, ACanvasWhoseParentIsNotTheTopWindowGetsNoPost) {
    auto* other = new wxFrame(nullptr, wxID_ANY, "other");
    auto* otherCanvas = new wxPanel(other);
    ocpn_fake::canvasWindow = otherCanvas;
    revive_->setOpenCpnVersion(5, 14);
    revive_->chartListUpdated();
    ASSERT_TRUE(pumpUntil([&] { return count("main window not found") == 1; }, 7000));
    pumpFor(5500);
    EXPECT_TRUE(ids_.empty());
    EXPECT_EQ(count("main window not found"), 1);
    EXPECT_EQ(count("frame timer restarted"), 0);
    EXPECT_EQ(count("did not restart"), 0);
    ocpn_fake::canvasWindow = canvas_;
    other->Destroy();
}

TEST_F(FrameTimerReviveTest, NoCanvasGetsNoPost) {
    ocpn_fake::canvasWindow = nullptr;
    revive_->setOpenCpnVersion(5, 14);
    revive_->chartListUpdated();
    ASSERT_TRUE(pumpUntil([&] { return count("main window not found") == 1; }, 7000));
    EXPECT_TRUE(ids_.empty());
}

// Version unknown, 5.12 and 5.16: an update arms nothing and posts nothing.
TEST_F(FrameTimerReviveTest, DisabledVersionsPostNothing) {
    FrameTimerRevive unknown, old, newer;
    old.setOpenCpnVersion(5, 12);
    newer.setOpenCpnVersion(5, 16);
    unknown.chartListUpdated();
    old.chartListUpdated();
    newer.chartListUpdated();
    revive_->chartListUpdated(); // never told the version
    pumpFor(5800);
    EXPECT_TRUE(ids_.empty());
    EXPECT_EQ(count("frame timer"), 0);
}

// DeInit while a check is armed: no post afterwards; an update after the shutdown arms nothing; an instance
// destroyed while armed leaves no timer behind.
TEST_F(FrameTimerReviveTest, ShutdownAndDestructionStopAnArmedCheck) {
    revive_->setOpenCpnVersion(5, 14);
    revive_->chartListUpdated();
    revive_->shutdown();
    revive_->chartListUpdated();
    {
        FrameTimerRevive gone;
        gone.setOpenCpnVersion(5, 14);
        gone.chartListUpdated();
    }
    pumpFor(5800);
    EXPECT_TRUE(ids_.empty());
    EXPECT_EQ(count("frame timer"), 0);
}

} // namespace
