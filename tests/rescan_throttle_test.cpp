// SPDX-License-Identifier: GPL-3.0-or-later
// The chart-list update throttle: literal times in milliseconds, no wx.
#include "rescan_throttle.h"

#include <gtest/gtest.h>

namespace {

using cg::RescanThrottle;
using D = RescanThrottle::Decision;

constexpr std::int64_t kT0 = 5000000; // an arbitrary start; only the differences count

// On OpenCPN 5.14+ (asyncDb) the first request calls at once; requests 1 s and 29 s later make no call and
// one call comes 30 s after the first call; a request 31 s after a call calls at once.
TEST(P0277Throttle, MergesRequestsIntoOneCallThirtySecondsAfterTheLastCall) {
    RescanThrottle t;
    EXPECT_EQ(t.request(kT0, true, false), D::CallNow);
    t.called(kT0);
    EXPECT_FALSE(t.pending());

    EXPECT_EQ(t.request(kT0 + 1000, true, false), D::Wait);
    EXPECT_TRUE(t.pending());
    EXPECT_EQ(t.waitMs(kT0 + 1000, false), 29000);
    EXPECT_EQ(t.request(kT0 + 29000, true, false), D::Wait); // merged into the same pending call
    EXPECT_EQ(t.waitMs(kT0 + 29000, false), 1000);

    EXPECT_EQ(t.due(kT0 + 29999, true, false), D::Wait);
    EXPECT_EQ(t.due(kT0 + 30000, true, false), D::CallNow);
    t.called(kT0 + 30000);
    EXPECT_FALSE(t.pending());
    EXPECT_EQ(t.due(kT0 + 70000, true, false), D::Wait) << "nothing pending: nothing to call";
    EXPECT_EQ(t.waitMs(kT0 + 70000, false), 0);

    EXPECT_EQ(t.request(kT0 + 30000 + 29999, true, false), D::Wait);
    t.called(kT0 + 60000);
    EXPECT_EQ(t.request(kT0 + 60000 + 31000, true, false), D::CallNow);
}

// The gap is exactly 30 s: a request at 30 s calls, one at 29.999 s waits.
TEST(P0277Throttle, TheGapIsThirtySecondsExactly) {
    RescanThrottle t;
    t.called(kT0);
    EXPECT_EQ(t.request(kT0 + 29999, true, false), D::Wait);
    EXPECT_EQ(t.waitMs(kT0 + 29999, false), 1);
    t.called(kT0 + 30000);
    EXPECT_EQ(t.request(kT0 + 60000, true, false), D::CallNow);
}

// With the Options dialog shown nothing is called, the poll is 2 s, and the pending call comes at the first
// due after the dialog is hidden, not before the 30 s gap.
TEST(P0277Throttle, WhileOptionsIsShownNothingIsCalledAndTheCallFollowsTheDialog) {
    RescanThrottle t;
    EXPECT_EQ(t.request(kT0, true, true), D::Wait) << "the very first request waits too";
    EXPECT_TRUE(t.pending());
    EXPECT_EQ(t.waitMs(kT0, true), 2000);
    EXPECT_EQ(t.due(kT0 + 2000, true, true), D::Wait);
    EXPECT_EQ(t.waitMs(kT0 + 2000, true), 2000);
    EXPECT_EQ(t.due(kT0 + 4000, true, false), D::CallNow) << "hidden and no earlier call";
    t.called(kT0 + 4000);

    // a call 5 s ago, then Options opens and closes: the call waits for the rest of the gap
    EXPECT_EQ(t.request(kT0 + 9000, true, true), D::Wait);
    EXPECT_EQ(t.due(kT0 + 11000, true, false), D::Wait);
    EXPECT_EQ(t.waitMs(kT0 + 11000, false), 23000);
    EXPECT_EQ(t.due(kT0 + 34000, true, false), D::CallNow);

    // a long hold: gap over when the dialog closes: at once
    t.called(kT0 + 34000);
    EXPECT_EQ(t.request(kT0 + 40000, true, true), D::Wait);
    EXPECT_EQ(t.due(kT0 + 90000, true, false), D::CallNow);
}

// On 5.10 / 5.12 (asyncDb false) every request calls at once, also right after a call, unless Options is
// shown.
TEST(P0277Throttle, WithoutAsyncDbEveryRequestCallsAtOnceUnlessOptionsIsShown) {
    RescanThrottle t;
    EXPECT_EQ(t.request(kT0, false, false), D::CallNow);
    t.called(kT0);
    EXPECT_EQ(t.request(kT0 + 1, false, false), D::CallNow);
    t.called(kT0 + 1);
    EXPECT_EQ(t.request(kT0 + 2, false, true), D::Wait);
    EXPECT_EQ(t.waitMs(kT0 + 2, true), 2000);
    EXPECT_EQ(t.due(kT0 + 3, false, true), D::Wait);
    EXPECT_EQ(t.due(kT0 + 4, false, false), D::CallNow);
}

// A clock step backwards counts as "gap over".
TEST(P0277Throttle, AClockStepBackwardsCountsAsGapOver) {
    RescanThrottle t;
    t.called(kT0);
    EXPECT_EQ(t.request(kT0 - 1, true, false), D::CallNow);
    EXPECT_EQ(t.waitMs(kT0 - 1, false), 0);
    EXPECT_EQ(t.due(kT0 - 3600000, true, false), D::CallNow);
}

// Nothing pending: waitMs is 0 even while Options is shown.
TEST(P0277Throttle, NothingPendingMeansNoWait) {
    RescanThrottle t;
    EXPECT_FALSE(t.pending());
    EXPECT_EQ(t.waitMs(kT0, true), 0);
    EXPECT_EQ(t.waitMs(kT0, false), 0);
    EXPECT_EQ(t.due(kT0, true, false), D::Wait);
}

} // namespace
