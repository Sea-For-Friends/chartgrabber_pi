// SPDX-License-Identifier: GPL-3.0-or-later
// Tests of strip_place. Expected values are literals worked out by hand from / 9.1.
#include "strip_place.h"

#include <gtest/gtest.h>

#include <utility>

namespace cg {
namespace {

// Safe area with a non-zero origin so a missing offset is caught: x 10..1010, y 20..620.
const StripRect kSafe{10, 20, 1000, 600};

void expectRect(const StripRect& r, int x, int y, int w, int h) {
    EXPECT_EQ(r.x, x);
    EXPECT_EQ(r.y, y);
    EXPECT_EQ(r.width, w);
    EXPECT_EQ(r.height, h);
}

TEST(StripPlace, SafeAreaCutsTheChartBarFromTheBottom) {
    expectRect(safeArea(1280, 720, 45), 0, 0, 1280, 675);
    expectRect(safeArea(1280, 720, 0), 0, 0, 1280, 720);
    expectRect(safeArea(1280, 40, 45), 0, 0, 1280, 0); // a chart bar taller than the canvas
}

TEST(StripPlace, PlaceEachCornerAtDistinctDistances) {
    // strip 200 x 40, dx 17, dy 29
    expectRect(placeStrip({StripCorner::BottomLeft, 17, 29}, 200, 40, kSafe, 8), 27, 551, 200, 40);
    expectRect(placeStrip({StripCorner::BottomRight, 17, 29}, 200, 40, kSafe, 8), 793, 551, 200, 40);
    expectRect(placeStrip({StripCorner::TopLeft, 17, 29}, 200, 40, kSafe, 8), 27, 49, 200, 40);
    expectRect(placeStrip({StripCorner::TopRight, 17, 29}, 200, 40, kSafe, 8), 793, 49, 200, 40);
}

TEST(StripPlace, ATooSmallCanvasPushesTheStripInsideWithTheMarginAndKeepsTheAnchor) {
    const StripAnchor bl{StripCorner::BottomLeft, 3, 2};
    expectRect(placeStrip(bl, 200, 40, kSafe, 8), 18, 572, 200, 40);
    const StripAnchor br{StripCorner::BottomRight, 1, 5};
    expectRect(placeStrip(br, 200, 40, kSafe, 8), 802, 572, 200, 40);
    const StripAnchor tl{StripCorner::TopLeft, 0, 0};
    expectRect(placeStrip(tl, 200, 40, kSafe, 8), 18, 28, 200, 40);
    const StripAnchor tr{StripCorner::TopRight, 4, 6};
    expectRect(placeStrip(tr, 200, 40, kSafe, 8), 802, 28, 200, 40);
    EXPECT_EQ(bl.corner, StripCorner::BottomLeft);
    EXPECT_EQ(bl.dx, 3);
    EXPECT_EQ(bl.dy, 2);
    EXPECT_EQ(br.dx, 1);
    EXPECT_EQ(br.dy, 5);
}

TEST(StripPlace, AStripLargerThanTheRoomSitsAtTheMarginOfTheLeftAndTopEdge) {
    const StripRect small{10, 20, 150, 30};
    for (auto c :
         {StripCorner::BottomLeft, StripCorner::BottomRight, StripCorner::TopLeft, StripCorner::TopRight})
        expectRect(placeStrip({c, 11, 13}, 200, 40, small, 8), 18, 28, 200, 40);
}

TEST(StripPlace, DropPicksTheNearestCornerOfTheCentreInEachQuadrant) {
    auto a = anchorFromDrop({30, 50, 200, 40}, kSafe);
    EXPECT_EQ(a.corner, StripCorner::TopLeft);
    EXPECT_EQ(a.dx, 20);
    EXPECT_EQ(a.dy, 30);
    a = anchorFromDrop({700, 60, 200, 40}, kSafe);
    EXPECT_EQ(a.corner, StripCorner::TopRight);
    EXPECT_EQ(a.dx, 110);
    EXPECT_EQ(a.dy, 40);
    a = anchorFromDrop({40, 500, 200, 40}, kSafe);
    EXPECT_EQ(a.corner, StripCorner::BottomLeft);
    EXPECT_EQ(a.dx, 30);
    EXPECT_EQ(a.dy, 80);
    a = anchorFromDrop({750, 520, 200, 40}, kSafe);
    EXPECT_EQ(a.corner, StripCorner::BottomRight);
    EXPECT_EQ(a.dx, 60);
    EXPECT_EQ(a.dy, 60);
}

TEST(StripPlace, DropTiesGoToTheBottomAndTheLeft) {
    // The safe centre is (510, 320); a 200 x 40 strip at (410, 300) is centred exactly on it.
    auto a = anchorFromDrop({410, 300, 200, 40}, kSafe);
    EXPECT_EQ(a.corner, StripCorner::BottomLeft);
    EXPECT_EQ(a.dx, 400);
    EXPECT_EQ(a.dy, 280);
    a = anchorFromDrop({410, 100, 200, 40}, kSafe); // x tie only, above the middle
    EXPECT_EQ(a.corner, StripCorner::TopLeft);
    a = anchorFromDrop({411, 300, 200, 40}, kSafe); // y tie only, right of the middle
    EXPECT_EQ(a.corner, StripCorner::BottomRight);
    a = anchorFromDrop({411, 299, 200, 40}, kSafe); // just above and just right
    EXPECT_EQ(a.corner, StripCorner::TopRight);
}

TEST(StripPlace, DropDistancesAreNeverNegative) {
    const auto a = anchorFromDrop({0, 0, 200, 40}, kSafe); // outside the safe area on the top left
    EXPECT_EQ(a.corner, StripCorner::TopLeft);
    EXPECT_EQ(a.dx, 0);
    EXPECT_EQ(a.dy, 0);
    const auto b = anchorFromDrop({900, 600, 200, 40}, kSafe); // beyond the bottom right
    EXPECT_EQ(b.corner, StripCorner::BottomRight);
    EXPECT_EQ(b.dx, 0);
    EXPECT_EQ(b.dy, 0);
}

TEST(StripPlace, PlacingTheDroppedAnchorGivesBackTheDroppedRectangle) {
    const StripRect drops[] = {
        {30, 50, 200, 40}, {700, 60, 200, 40}, {40, 500, 200, 40}, {750, 520, 200, 40}};
    for (const auto& d : drops) {
        const auto r = placeStrip(anchorFromDrop(d, kSafe), d.width, d.height, kSafe, 8);
        expectRect(r, d.x, d.y, d.width, d.height);
    }
}

TEST(StripPlace, DragStartsOnlyBeyondSixPixelsTwelveInTouchMode) {
    EXPECT_FALSE(dragStarted(6, 0, false));
    EXPECT_FALSE(dragStarted(0, 6, false));
    EXPECT_FALSE(dragStarted(-6, 5, false));
    EXPECT_TRUE(dragStarted(7, 0, false));
    EXPECT_TRUE(dragStarted(0, 7, false));
    EXPECT_TRUE(dragStarted(-7, 2, false));
    EXPECT_TRUE(dragStarted(3, -7, false));
    EXPECT_FALSE(dragStarted(12, 0, true));
    EXPECT_FALSE(dragStarted(0, -12, true));
    EXPECT_TRUE(dragStarted(13, 0, true));
    EXPECT_TRUE(dragStarted(0, 13, true));
    EXPECT_TRUE(dragStarted(-13, 1, true));
    EXPECT_TRUE(dragStarted(1, -13, true));
}

TEST(StripPlace, WidthKeepsBestCapsAtSixtyPercentAndTheMinimumAndTheMargins) {
    EXPECT_EQ(stripWidth(500, 1000, 320, 8), 500); // fits
    EXPECT_EQ(stripWidth(900, 1000, 320, 8), 600); // 60 % of the canvas
    EXPECT_EQ(stripWidth(900, 1001, 320, 8), 600); // integer division: 1001 * 60 / 100 = 600
    EXPECT_EQ(stripWidth(900, 500, 320, 8), 320);  // 60 % = 300 < minimum
    EXPECT_EQ(stripWidth(200, 500, 320, 8), 200);  // best below the minimum stays
    EXPECT_EQ(stripWidth(900, 330, 320, 8), 314);  // the minimum, then canvas - 2 * margin
    EXPECT_EQ(stripWidth(900, 10, 320, 8), 1);     // canvas - 16 < 1
    EXPECT_EQ(stripWidth(0, 1000, 320, 8), 1);     // never below 1
}

TEST(StripPlace, CornerKeysRoundTripAndOnlyTheExactLowerCaseParses) {
    const std::pair<StripCorner, const char*> all[] = {{StripCorner::TopRight, "tr"},
                                                       {StripCorner::BottomLeft, "bl"},
                                                       {StripCorner::TopLeft, "tl"},
                                                       {StripCorner::BottomRight, "br"}};
    for (const auto& [c, k] : all) {
        EXPECT_EQ(cornerKey(c), k);
        const auto p = parseCornerKey(k);
        ASSERT_TRUE(p.has_value()) << k;
        EXPECT_EQ(*p, c);
    }
    for (const char* bad : {"BL", "", "bottom-left", " bl", "bl ", "Br", "BR", "tR", "xx"})
        EXPECT_FALSE(parseCornerKey(bad).has_value()) << "[" << bad << "]";
}

} // namespace
} // namespace cg
