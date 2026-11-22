// SPDX-License-Identifier: GPL-3.0-or-later
// The stall meter's arithmetic on given tick times (no wx, no clock).
#include "stall_meter.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>

namespace {

struct EdgeRow {
    std::int64_t gapMs;
    int stallMs;
};

TEST(StallMeterTest, OnlyAGapOfPeriodPlusThresholdOrMoreIsAStall) {
    // Period 100 ms, threshold 250 ms: a gap of 350 ms is the first stall (250 ms of it).
    const EdgeRow rows[] = {
        {100, 0}, {349, 0}, {350, 250}, {351, 251}, {1100, 1000},
    };
    for (const EdgeRow& row : rows) {
        cg::StallMeter meter;
        EXPECT_EQ(meter.tick(1000), 0);
        EXPECT_EQ(meter.tick(1000 + row.gapMs), row.stallMs) << "gap " << row.gapMs;
        EXPECT_EQ(meter.count(), row.stallMs > 0 ? 1 : 0) << "gap " << row.gapMs;
    }
}

TEST(StallMeterTest, TheFirstTickIsNeverAStall) {
    cg::StallMeter meter;
    EXPECT_EQ(meter.tick(5000000), 0);
    EXPECT_EQ(meter.count(), 0);
    EXPECT_EQ(meter.longestMs(), 0);
    EXPECT_EQ(meter.totalMs(), 0);
}

TEST(StallMeterTest, CountLongestAndTotalFollowAnUnsortedSeriesOfGaps) {
    // Gaps 700 100 350 1000 349 5000 100 ms: stalls 600, 0, 250, 900, 0, 4900, 0 ms.
    const std::int64_t gaps[] = {700, 100, 350, 1000, 349, 5000, 100};
    const int stalls[] = {600, 0, 250, 900, 0, 4900, 0};
    cg::StallMeter meter;
    std::int64_t now = 1000;
    EXPECT_EQ(meter.tick(now), 0);
    for (int i = 0; i < 7; ++i) {
        now += gaps[i];
        EXPECT_EQ(meter.tick(now), stalls[i]) << "gap " << gaps[i];
    }
    EXPECT_EQ(meter.count(), 4);
    EXPECT_EQ(meter.longestMs(), 4900);
    EXPECT_EQ(meter.totalMs(), 6650);
}

TEST(StallMeterTest, ABackwardsClockStepReportsNothingAndRebasesTheTime) {
    cg::StallMeter meter;
    EXPECT_EQ(meter.tick(10000), 0);
    EXPECT_EQ(meter.tick(9000), 0); // step back: no stall, the base moves to 9000
    EXPECT_EQ(meter.tick(9100), 0); // 100 ms after the new base
    EXPECT_EQ(meter.tick(9450), 250);
    EXPECT_EQ(meter.count(), 1);
}

TEST(StallMeterTest, AStallLongerThanTheIntRangeIsClampedNotWrapped) {
    cg::StallMeter meter;
    meter.tick(0);
    EXPECT_EQ(meter.tick(3000000000), std::numeric_limits<int>::max());
    EXPECT_EQ(meter.count(), 1);
    EXPECT_EQ(meter.totalMs(), std::numeric_limits<int>::max());
}

TEST(StallMeterTest, ResetForgetsTheCountsAndTheBase) {
    cg::StallMeter meter;
    meter.tick(0);
    EXPECT_EQ(meter.tick(1000), 900);
    meter.reset();
    EXPECT_EQ(meter.count(), 0);
    EXPECT_EQ(meter.longestMs(), 0);
    EXPECT_EQ(meter.totalMs(), 0);
    EXPECT_EQ(meter.tick(5000), 0); // the first tick after a reset only stores the time
    EXPECT_EQ(meter.tick(5350), 250);
}

TEST(StallMeterTest, LineAndSummaryTexts) {
    EXPECT_EQ(cg::stallLine(1234), "main loop stalled 1234 ms");
    cg::StallMeter empty;
    EXPECT_EQ(cg::stallSummary(empty), "main loop stalls: 0, longest 0 ms, total 0 ms");
    cg::StallMeter meter;
    meter.tick(0);
    meter.tick(700);
    meter.tick(1050);
    meter.tick(5050);
    EXPECT_EQ(cg::stallSummary(meter), "main loop stalls: 3, longest 3900 ms, total 4750 ms");
}

} // namespace
