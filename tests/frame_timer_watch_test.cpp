// SPDX-License-Identifier: GPL-3.0-or-later
// When OpenCPN's frame timers need a restart after a chart-list update: the version table and the watch's
// counting (no wx, no clock).
#include "frame_timer_watch.h"

#include <gtest/gtest.h>

namespace {

using Action = cg::FrameTimerWatch::Action;

TEST(FrameTimerWatchTest, OnlyOpenCpn514And515AreSupported) {
    struct Row {
        int major, minor;
        bool supported;
    };
    const Row rows[] = {
        {5, 14, true},  {5, 15, true}, {5, 10, false}, {5, 12, false}, {5, 13, false},
        {5, 16, false}, {6, 0, false}, {4, 8, false},  {4, 14, false}, {0, 0, false},
    };
    for (const Row& r : rows)
        EXPECT_EQ(cg::frameTimerReviveSupported(r.major, r.minor), r.supported) << r.major << "." << r.minor;
}

TEST(FrameTimerWatchTest, TheConstantsAreFiveSecondsAndThreePosts) {
    EXPECT_EQ(cg::FrameTimerWatch::kCheckDelayMs, 5000);
    EXPECT_EQ(cg::FrameTimerWatch::kMaxPosts, 3);
}

TEST(FrameTimerWatchTest, NothingIsArmedUntilAnUpdate) {
    cg::FrameTimerWatch w;
    EXPECT_FALSE(w.armed());
    EXPECT_EQ(w.check(0), Action::None);
    w.heartbeat(1);
    EXPECT_EQ(w.check(2), Action::None);
}

TEST(FrameTimerWatchTest, AHeartbeatAfterTheUpdateMeansNoPostAndDisarms) {
    cg::FrameTimerWatch w;
    w.chartListUpdated(0);
    EXPECT_TRUE(w.armed());
    w.heartbeat(1000);
    EXPECT_EQ(w.check(5000), Action::None);
    EXPECT_FALSE(w.armed());
    EXPECT_EQ(w.check(10000), Action::None);
}

TEST(FrameTimerWatchTest, NoHeartbeatPostsThreeTimesThenGivesUpAndNeverPostsAFourthTime) {
    cg::FrameTimerWatch w;
    w.chartListUpdated(0);
    EXPECT_EQ(w.check(5000), Action::Post);
    EXPECT_TRUE(w.armed());
    EXPECT_EQ(w.check(10000), Action::Post);
    EXPECT_EQ(w.check(15000), Action::Post);
    EXPECT_TRUE(w.armed());
    EXPECT_EQ(w.check(20000), Action::GiveUp);
    EXPECT_FALSE(w.armed());
    EXPECT_EQ(w.check(25000), Action::None);
    EXPECT_EQ(w.check(30000), Action::None);
}

TEST(FrameTimerWatchTest, AHeartbeatBetweenTheFirstAndSecondCheckStopsFurtherPosts) {
    cg::FrameTimerWatch w;
    w.chartListUpdated(0);
    EXPECT_EQ(w.check(5000), Action::Post);
    w.heartbeat(6000);
    EXPECT_EQ(w.check(10000), Action::None);
    EXPECT_FALSE(w.armed());
    EXPECT_EQ(w.check(15000), Action::None);
}

TEST(FrameTimerWatchTest, AHeartbeatBeforeTheUpdateDoesNotCount) {
    cg::FrameTimerWatch w;
    w.heartbeat(0);
    w.chartListUpdated(1000);
    EXPECT_EQ(w.check(6000), Action::Post);
}

TEST(FrameTimerWatchTest, ANewUpdateRearmsWithTheCountAtZero) {
    cg::FrameTimerWatch w;
    w.chartListUpdated(0);
    EXPECT_EQ(w.check(5000), Action::Post);
    EXPECT_EQ(w.check(10000), Action::Post);
    EXPECT_EQ(w.check(15000), Action::Post);
    EXPECT_EQ(w.check(20000), Action::GiveUp);
    w.chartListUpdated(30000);
    EXPECT_TRUE(w.armed());
    EXPECT_EQ(w.check(35000), Action::Post);
    EXPECT_EQ(w.check(40000), Action::Post);
    EXPECT_EQ(w.check(45000), Action::Post);
    EXPECT_EQ(w.check(50000), Action::GiveUp);
}

TEST(FrameTimerWatchTest, ANewUpdateInTheMiddleOfAnArmedWatchResetsTheCountAndTheHeartbeat) {
    cg::FrameTimerWatch w;
    w.chartListUpdated(0);
    EXPECT_EQ(w.check(5000), Action::Post);
    EXPECT_EQ(w.check(10000), Action::Post);
    w.heartbeat(11000);
    w.chartListUpdated(12000); // the beat before this update does not count
    EXPECT_EQ(w.check(17000), Action::Post);
    EXPECT_EQ(w.check(22000), Action::Post);
    EXPECT_EQ(w.check(27000), Action::Post);
    EXPECT_EQ(w.check(32000), Action::GiveUp);
}

} // namespace
