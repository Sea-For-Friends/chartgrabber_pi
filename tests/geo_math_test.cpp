// SPDX-License-Identifier: GPL-3.0-or-later
// Tests of geo_math. Expected values are literals from the geo_math definitions, or computed
// by hand from the Web Mercator formulas (see the comments).
#include "geo_math.h"

#include <gtest/gtest.h>

#include <clocale>
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace cg {
namespace {

constexpr double kMax = 85.0511;

// ---- normalizeBox ----

TEST(NormalizeBox, CornersInAnyOrderGiveTheSameBox) {
    const auto one = normalizeBox({10, 20}, {5, 15});
    const auto two = normalizeBox({5, 15}, {10, 20});
    const auto three = normalizeBox({5, 20}, {10, 15});
    for (const auto& r : {one, two, three}) {
        ASSERT_TRUE(r.has_value());
        EXPECT_EQ(r->west, 15);
        EXPECT_EQ(r->south, 5);
        EXPECT_EQ(r->east, 20);
        EXPECT_EQ(r->north, 10);
    }
}

TEST(NormalizeBox, LatitudeIsClampedToMercatorLimit) {
    const auto r = normalizeBox({-89, 0}, {89, 10});
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->south, -kMax);
    EXPECT_EQ(r->north, kMax);
}

TEST(NormalizeBox, NothingLeftAfterClampIsOutsideMercator) {
    const auto north = normalizeBox({86, 0}, {89, 10});
    ASSERT_FALSE(north.has_value());
    EXPECT_EQ(north.error(), AreaError::OutsideMercator);
    const auto south = normalizeBox({-89, 0}, {-86, 10});
    ASSERT_FALSE(south.has_value());
    EXPECT_EQ(south.error(), AreaError::OutsideMercator);
}

TEST(NormalizeBox, SpanOf360OrMoreBecomesWholeWorld) {
    for (const auto& lons : {std::pair{-200.0, 200.0}, std::pair{0.0, 360.0}}) {
        const auto r = normalizeBox({0, lons.first}, {1, lons.second});
        ASSERT_TRUE(r.has_value());
        EXPECT_EQ(r->west, -180);
        EXPECT_EQ(r->east, 180);
    }
}

TEST(NormalizeBox, BoxBeyond180IsShiftedWest) {
    const auto r = normalizeBox({0, 200}, {1, 190});
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->west, -170);
    EXPECT_EQ(r->east, -160);
}

TEST(NormalizeBox, BoxAcrossTheAntimeridianIsRefused) {
    const auto r = normalizeBox({0, 170}, {1, 190});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), AreaError::CrossesAntimeridian);
}

TEST(NormalizeBox, EqualEdgesAreEmpty) {
    const auto sameLat = normalizeBox({5, 10}, {5, 20});
    ASSERT_FALSE(sameLat.has_value());
    EXPECT_EQ(sameLat.error(), AreaError::Empty);
    const auto sameLon = normalizeBox({5, 10}, {6, 10});
    ASSERT_FALSE(sameLon.has_value());
    EXPECT_EQ(sameLon.error(), AreaError::Empty);
}

TEST(NormalizeBox, ResultIsRoundedToEightDecimals) {
    const auto r = normalizeBox({1.123456781, 10.123456789}, {2.987654321, 20.000000004});
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->west, 10.12345679);
    EXPECT_EQ(r->south, 1.12345678);
    EXPECT_EQ(r->east, 20.0);
    EXPECT_EQ(r->north, 2.98765432);
}

// ---- checkRoute / routeBounds ----

TEST(CheckRoute, OnePointIsTooFew) {
    const auto r = checkRoute({{10, 10}});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), AreaError::TooFewRoutePoints);
}

TEST(CheckRoute, LongitudesFarApartCrossTheAntimeridian) {
    const auto r = checkRoute({{0, -179}, {0, 179}});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), AreaError::CrossesAntimeridian);
}

TEST(CheckRoute, SpanOf180IsAcceptedAndMoreIsNot) {
    // The CLI refuses only east - west > 180 (core job_area.cpp:122).
    EXPECT_TRUE(checkRoute({{0, 90}, {0, -90}}).has_value());
    EXPECT_EQ(checkRoute({{0, 91}, {0, -90}}).error(), AreaError::CrossesAntimeridian);
}

TEST(RouteBounds, GrowsByHalfTheWidthAtTheEquator) {
    // 10 NM wide: half = 5 NM = 9260 m; latitude degrees = 9260 / 111320.
    const BBox b = routeBounds({{0, 10}, {0, 11}}, 10);
    const double d = 9260.0 / 111320.0;
    EXPECT_NEAR(b.south, -d, 1e-9);
    EXPECT_NEAR(b.north, d, 1e-9);
    EXPECT_NEAR(b.west, 10 - d, 1e-9);
    EXPECT_NEAR(b.east, 11 + d, 1e-9);
}

TEST(RouteBounds, LongitudeGrowthIsDividedByCosLatitude) {
    const BBox b = routeBounds({{60, 10}, {60, 11}}, 10);
    const double d = 9260.0 / 111320.0;
    EXPECT_NEAR(b.north, 60 + d, 1e-9);
    EXPECT_NEAR(b.south, 60 - d, 1e-9);
    EXPECT_NEAR(b.west, 10 - d / 0.5, 1e-9);
    EXPECT_NEAR(b.east, 11 + d / 0.5, 1e-9);
}

// ---- viewZoom ----

TEST(ViewZoom, ScaleOfZoom10AtTheEquator) {
    // ppm of zoom 10: 2^10 * 256 / 40075016.686 m.
    EXPECT_NEAR(viewZoom(262144.0 / 40075016.686, 0), 10.0, 1e-9);
}

TEST(ViewZoom, SameScaleAtSixtyNorthIsOneZoomLower) {
    // cos 60 = 0.5: the same ppm covers half the tile size, log2(0.5) = -1.
    EXPECT_NEAR(viewZoom(262144.0 / 40075016.686, 60), 9.0, 1e-9);
    EXPECT_NEAR(viewZoom(262144.0 / (40075016.686 * 0.5), 60), 10.0, 1e-9);
}

// ---- bboxTileCount ----

TEST(BboxTileCount, WholeWorld) {
    const BBox world{-180, -kMax, 180, kMax};
    EXPECT_EQ(bboxTileCount(world, 0), 1);
    EXPECT_EQ(bboxTileCount(world, 1), 4);
    EXPECT_EQ(bboxTileCount(world, 2), 16);
}

TEST(BboxTileCount, SmallBoxAtZoom17) {
    // Box lon 10.0..10.02, lat 50.0..50.01. Hand-computed: x = floor((lon+180)/360 * 2^17)
    // gives 69176..69184 (9 columns); y = floor((1 - asinh(tan(lat))/pi)/2 * 2^17) gives
    // 44446 (north).. 44452 (south) (7 rows). 9 * 7 = 63.
    EXPECT_EQ(bboxTileCount(BBox{10.0, 50.0, 10.02, 50.01}, 17), 63);
}

TEST(BboxTileCount, EdgeOnTileBoundaryCountsExtra) {
    // At zoom 1 the lon 0 and lat 0 lines are tile boundaries: the box 0..90 / 0..45 lies in one
    // tile but its west and north edges sit on a boundary, so the count is the safe over-count.
    EXPECT_EQ(bboxTileCount(BBox{0.0, 0.0, 90.0, 45.0}, 1), 2);
}

// ---- scaleDenominator / formatScale ----

TEST(FormatScale, EquatorLabels) {
    EXPECT_EQ(formatScale(scaleDenominator(8, 0)), "1:2.3M");
    EXPECT_EQ(formatScale(scaleDenominator(11, 0)), "1:290k");
    EXPECT_EQ(formatScale(scaleDenominator(17, 0)), "1:4.5k");
}

TEST(FormatScale, LevelsZeroToFiveHaveNoDecimalFromTenMillion) {
    EXPECT_EQ(formatScale(scaleDenominator(0, 0)), "1:590M");
    EXPECT_EQ(formatScale(scaleDenominator(1, 0)), "1:300M");
    EXPECT_EQ(formatScale(scaleDenominator(2, 0)), "1:150M");
    EXPECT_EQ(formatScale(scaleDenominator(3, 0)), "1:74M");
    EXPECT_EQ(formatScale(scaleDenominator(4, 0)), "1:37M");
    EXPECT_EQ(formatScale(scaleDenominator(5, 0)), "1:18M");
    EXPECT_EQ(formatScale(9.9e6), "1:9.9M");
}

TEST(FormatScale, UnitBoundaries) {
    EXPECT_EQ(formatScale(900), "1:900");
    EXPECT_EQ(formatScale(45000), "1:45k");
    EXPECT_EQ(formatScale(4514), "1:4.5k");
    EXPECT_EQ(formatScale(145000), "1:150k");
    EXPECT_EQ(formatScale(2311000), "1:2.3M");
}

TEST(FormatScale, BranchEdges) {
    EXPECT_EQ(formatScale(1e7), "1:10M");
    EXPECT_EQ(formatScale(9.96e6), "1:10M");
    EXPECT_EQ(formatScale(1e6), "1:1.0M");
    EXPECT_EQ(formatScale(9996), "1:10k");
    EXPECT_EQ(formatScale(1e4), "1:10k");
    EXPECT_EQ(formatScale(1e3), "1:1.0k");
    EXPECT_EQ(formatScale(999), "1:1.0k");
    EXPECT_EQ(formatScale(990), "1:990");
    EXPECT_EQ(formatScale(1), "1:1");
}

TEST(FormatScale, IndependentOfNumericLocale) {
    const char* old = std::setlocale(LC_NUMERIC, nullptr);
    const std::string saved = old ? old : "C";
    const char* set = nullptr;
    for (const char* name : {"de_DE.UTF-8", "de_DE.utf8", "ru_RU.UTF-8", "fr_FR.UTF-8"})
        if ((set = std::setlocale(LC_NUMERIC, name)))
            break;
    if (!set)
        GTEST_SKIP() << "no comma-decimal locale installed";
    EXPECT_EQ(formatScale(4514), "1:4.5k");
    EXPECT_EQ(formatScale(2311000), "1:2.3M");
    std::setlocale(LC_NUMERIC, saved.c_str());
}

TEST(ScaleDenominator, SixtyNorthIsHalfTheEquatorValue) {
    // Equator z8: 156543.034 / 256 * 96 / 0.0254 = 2 311 166.84.
    EXPECT_NEAR(scaleDenominator(8, 0), 2311166.84, 0.01);
    EXPECT_NEAR(scaleDenominator(8, 60), 1155583.42, 0.01);
    EXPECT_EQ(formatScale(scaleDenominator(8, 60)), "1:1.2M");
    EXPECT_EQ(formatScale(scaleDenominator(11, 60)), "1:140k");
}

TEST(ScaleDenominator, DpiScalesLinearly) {
    EXPECT_NEAR(scaleDenominator(8, 0, 192), 2.0 * 2311166.84, 0.02);
}

// ---- bands ----

TEST(Bands, ZoomRanges) {
    EXPECT_EQ(bandZooms(Band::Passage), (std::pair{8, 11}));
    EXPECT_EQ(bandZooms(Band::Coastal), (std::pair{12, 14}));
    EXPECT_EQ(bandZooms(Band::Approach), (std::pair{15, 16}));
    EXPECT_EQ(bandZooms(Band::Harbour), (std::pair{17, 18}));
}

TEST(Bands, MakeContiguousFillsTheGap) {
    EXPECT_EQ(makeContiguous({Band::Approach, Band::Passage}),
              (std::set<Band>{Band::Passage, Band::Coastal, Band::Approach}));
    EXPECT_EQ(makeContiguous({Band::Harbour}), (std::set<Band>{Band::Harbour}));
}

TEST(Bands, ZoomsForPassageAndApproach) {
    EXPECT_EQ(zoomsForBands({Band::Approach, Band::Passage}),
              (std::vector<int>{8, 9, 10, 11, 12, 13, 14, 15, 16}));
    EXPECT_EQ(zoomsForBands({Band::Harbour}), (std::vector<int>{17, 18}));
}

// ---- zoom lists (grammar of core job.cpp:454-476) ----

TEST(ParseZoomList, AcceptedForms) {
    EXPECT_EQ(parseZoomList("8-12,14").value(), (std::vector<int>{8, 9, 10, 11, 12, 14}));
    EXPECT_EQ(parseZoomList("3").value(), (std::vector<int>{3}));
    EXPECT_EQ(parseZoomList("0").value(), (std::vector<int>{0}));
    EXPECT_EQ(parseZoomList("21").value(), (std::vector<int>{21}));
    // Items are trimmed, as the CLI does (job.cpp:460).
    EXPECT_EQ(parseZoomList(" 8 , 9-10 ").value(), (std::vector<int>{8, 9, 10}));
}

TEST(ParseZoomList, SortsAndDeduplicates) {
    EXPECT_EQ(parseZoomList("14,8-10,9,14").value(), (std::vector<int>{8, 9, 10, 14}));
}

TEST(ParseZoomList, RefusedInputsReturnTheirText) {
    // " 8 - 9 " is refused by the CLI regex (job.cpp:455, no inner spaces).
    for (const char* bad : {"12-8", "22", "a", "", " 8 - 9 ", "8-", "-8", "8,,9", "8,", "1234", "8-22"}) {
        const auto r = parseZoomList(bad);
        ASSERT_FALSE(r.has_value()) << bad;
        EXPECT_EQ(r.error(), bad);
    }
}

TEST(FormatZoomList, CollapsesRunsAndRoundTrips) {
    EXPECT_EQ(formatZoomList({12, 8, 10, 9, 9}), "8-10,12");
    EXPECT_EQ(formatZoomList({5}), "5");
    EXPECT_EQ(formatZoomList({3, 4}), "3-4");
    EXPECT_EQ(formatZoomList({}), "");
    const std::vector<int> zooms{2, 3, 4, 7, 9, 10, 20};
    EXPECT_EQ(formatZoomList(zooms), "2-4,7,9-10,20");
    EXPECT_EQ(parseZoomList(formatZoomList(zooms)).value(), zooms);
}

// ---- fitScalePpm ----

TEST(FitScalePpm, WidthLimits) {
    // lon 0..4, lat -1..1 at the equator: 445280 m wide, 222640 m high. 800x600 px: 800/445280 < 600/222640.
    EXPECT_NEAR(fitScalePpm(BBox{0, -1, 4, 1}, 800, 600), 800.0 / 445280.0, 1e-12);
}

TEST(FitScalePpm, HeightLimits) {
    // lon 0..1, lat -2..2: 111320 m wide, 445280 m high. 800x600 px: 600/445280 < 800/111320.
    EXPECT_NEAR(fitScalePpm(BBox{0, -2, 1, 2}, 800, 600), 600.0 / 445280.0, 1e-12);
}

TEST(FitScalePpm, LongitudeShrinksWithCosOfCentreLatitude) {
    // lon 0..2, lat 59..61 (centre 60): 2 * 111320 * 0.5 = 111320 m wide, 222640 m high. 300x1000 px.
    EXPECT_NEAR(fitScalePpm(BBox{0, 59, 2, 61}, 300, 1000), 300.0 / 111320.0, 1e-12);
}

TEST(Centre, IsTheMidpoint) {
    const LatLon c = centre(BBox{10, 20, 14, 30});
    EXPECT_EQ(c.lat, 25);
    EXPECT_EQ(c.lon, 12);
}

using Zooms = std::vector<int>;

TEST(LimitZooms, CutsAboveTheLimitAndKeepsTheLastLevelWhenNoneIsLeft) {
    EXPECT_EQ(limitZooms({8, 9, 10, 11, 12, 13, 14}, 7), Zooms({7}));
    EXPECT_EQ(limitZooms({5, 6, 7, 8}, 7), Zooms({5, 6, 7}));
    EXPECT_EQ(limitZooms({8, 9}, std::nullopt), Zooms({8, 9}));
    EXPECT_EQ(limitZooms({}, 7), Zooms());
}

TEST(JobZooms, InsertsTheOverlayLimitOnlyWhenEveryZoomIsAboveIt) {
    const Zooms z{8, 9, 10, 11, 12, 13, 14};
    EXPECT_EQ(jobZooms(z, std::nullopt, 7), Zooms({7, 8, 9, 10, 11, 12, 13, 14}));
    EXPECT_EQ(jobZooms(z, 10, 7), Zooms({7, 8, 9, 10}));
    EXPECT_EQ(jobZooms(z, std::nullopt, 12), z);
    EXPECT_EQ(jobZooms({7, 8}, std::nullopt, 7), Zooms({7, 8})); // already there: no duplicate
    EXPECT_EQ(jobZooms({6}, std::nullopt, 9), Zooms({6}));
    EXPECT_EQ(jobZooms(z, std::nullopt, std::nullopt), z);
}

// ---- the detail range ----

TEST(DetailRange, RangeForBandsIsTheLevelsOfTheLowestToTheHighestBand) {
    struct Row {
        std::set<Band> in;
        std::optional<ZoomRange> out;
    };
    const std::vector<Row> rows = {
        {{}, std::nullopt},
        {{Band::Coastal, Band::Passage}, ZoomRange{8, 14}},
        {{Band::Harbour, Band::Passage}, ZoomRange{8, 18}}, // a gap is closed, as zoomsForBands does
        {{Band::Approach}, ZoomRange{15, 16}},
    };
    for (const Row& r : rows)
        EXPECT_EQ(rangeForBands(r.in), r.out);
}

TEST(DetailRange, BandsInRangeAndPartialBands) {
    struct Row {
        std::optional<ZoomRange> r;
        std::set<Band> inside;
        std::vector<Band> partial;
    };
    const std::vector<Row> rows = {
        {std::nullopt, {}, {}},
        {ZoomRange{0, 5}, {}, {}},                 // wholly below 8-18
        {ZoomRange{19, 21}, {}, {}},               // wholly above
        {ZoomRange{15, 16}, {Band::Approach}, {}}, // one band exactly
        {ZoomRange{8, 14}, {Band::Passage, Band::Coastal}, {}},
        {ZoomRange{10, 14}, {Band::Coastal}, {Band::Passage}},
        {ZoomRange{9, 17}, {Band::Coastal, Band::Approach}, {Band::Passage, Band::Harbour}},
        {ZoomRange{11, 12}, {}, {Band::Passage, Band::Coastal}},
        {ZoomRange{0, 21}, {Band::Passage, Band::Coastal, Band::Approach, Band::Harbour}, {}},
    };
    for (const Row& r : rows) {
        EXPECT_EQ(bandsInRange(r.r), r.inside);
        EXPECT_EQ(partialBands(r.r), r.partial);
    }
}

TEST(DetailRange, PicksKeepLoBelowHiTheOtherEndFollows) {
    struct Row {
        std::optional<ZoomRange> r;
        int z;
        ZoomRange least, most;
    };
    const std::vector<Row> rows = {
        {std::nullopt, 7, {7, 7}, {7, 7}},         {ZoomRange{8, 14}, 10, {10, 14}, {8, 10}},
        {ZoomRange{8, 14}, 16, {16, 16}, {8, 16}}, // above hi
        {ZoomRange{8, 14}, 5, {5, 14}, {5, 5}},    // below lo
        {ZoomRange{8, 14}, 8, {8, 14}, {8, 8}},
    };
    for (const Row& r : rows) {
        EXPECT_EQ(pickLeast(r.r, r.z), r.least) << r.z;
        EXPECT_EQ(pickMost(r.r, r.z), r.most) << r.z;
    }
}

TEST(DetailRange, RangeFromTextAndRangeZooms) {
    struct Row {
        const char* text;
        std::optional<ZoomRange> out;
    };
    const std::vector<Row> rows = {
        {"10-14", ZoomRange{10, 14}}, {"8,14", ZoomRange{8, 14}}, {"14,8,9", ZoomRange{8, 14}},
        {"", std::nullopt},           {"x", std::nullopt},        {"22", std::nullopt},
        {"-1", std::nullopt},         {"21", ZoomRange{21, 21}},  {"0", ZoomRange{0, 0}},
    };
    for (const Row& r : rows)
        EXPECT_EQ(rangeFromText(r.text), r.out) << r.text;
    EXPECT_EQ(rangeZooms({10, 13}), (std::vector<int>{10, 11, 12, 13}));
    EXPECT_EQ(rangeZooms({7, 7}), (std::vector<int>{7}));
}

} // namespace
// the tile ranges below were worked out with an independent Web Mercator script (x =
// floor((lon
// + 180) / 360 x 2^z), y = floor((1 - asinh(tan(lat)) / pi) / 2 x 2^z)); counts are columns x rows per level.
// kTiny (west 10, south 50, east 10.0001, north 50.0001): 1 tile at z0-19 (x 276707, y 177809 at z19), 2
// at z20, 21. kSmall (10, 50, 10.05, 50.05): z0-10 1 each, z11 2, z12 4, z13 6, z14 12, z15 40, z16 150,
// z17 580. kMed (10, 50, 10.4, 50.2): z0-9 1 each; z10 4, z11 12, z12 30, z13 80, z14 285, z15 1110, z16
// 4292,
// z17 16905 (x 69176-69322, y 44338-44452), z18 66804, z19 265720.
// kBig (0, 40, 20, 60): z0-2 1 each, z3 2, z4 3, z5 8, z6 28, z7 96, z8 345, z9 1334, z10 5187, z11
// 20748;
// T(0,10) = 7006, T(0,11) = 27754, T(0,16) = 28067231, T(0,17) = 112239869.
const BBox kTiny{10.0, 50.0, 10.0001, 50.0001};
const BBox kSmall{10.0, 50.0, 10.05, 50.05};
const BBox kMed{10.0, 50.0, 10.4, 50.2};
const BBox kBig{0.0, 40.0, 20.0, 60.0};

TEST(DetailAdviceTest, RangeTileCountSumsTheLevelsAndAnEmptyRangeIsZero) {
    EXPECT_EQ(rangeTileCount(kMed, 10, 12), 46);   // 4 + 12 + 30
    EXPECT_EQ(rangeTileCount(kMed, 0, 17), 22728); // 10 + 46 + 80 + 285 + 1110 + 4292 + 16905
    EXPECT_EQ(rangeTileCount(kMed, 17, 17), 16905);
    EXPECT_EQ(rangeTileCount(kMed, 13, 12), 0);
}

TEST(DetailAdviceTest, TableOfPicks) {
    struct Row {
        const char* what;
        const BBox* box;
        ZoomRange range;
        bool most;
        DetailWarning kind;
        ZoomRange suggested;
        std::int64_t tiles, suggestedTiles;
    };
    const DetailWarning N = DetailWarning::None, M = DetailWarning::TooMuch, L = DetailWarning::TooLittle;
    const std::vector<Row> rows = {
        // TooMuch by hi >= 19 on a tiny box (T = 12 is far below 20000): z18 is the highest below 19
        {"hi 19 tiny", &kTiny, {8, 19}, true, M, {8, 18}, 12, 11},
        // the cap z <= 18: without it hi 21 would suggest {8, 20}
        {"hi 21 tiny", &kTiny, {8, 21}, true, M, {8, 18}, 16, 11},
        // hi 17 and T = 22728 > 20000: z16 (5823) is the highest that fits
        {"hi 17 over", &kMed, {0, 17}, true, M, {0, 16}, 22728, 5823},
        {"hi 17 over, lo 10", &kMed, {10, 17}, true, M, {10, 16}, 22718, 5813},
        // the highest, not any: z17 (22706) does not fit, z16 (5797) does, z15 would fit as well
        {"highest qualifying", &kMed, {12, 18}, true, M, {12, 16}, 89506, 5797},
        {"big box", &kBig, {0, 17}, true, M, {0, 10}, 112239869, 7006},
        // none fits: lo itself is above 20000 -> {lo, lo}; suggestedTiles > kDetailWarnTiles
        {"none fits", &kMed, {18, 19}, true, M, {18, 18}, 332524, 66804},
        // lo == hi: a single level below, the highest that fits
        {"single 18", &kMed, {18, 18}, true, M, {17, 17}, 66804, 16905},
        {"single 19 skips 18", &kMed, {19, 19}, true, M, {17, 17}, 265720, 16905},
        {"single 19 tiny", &kTiny, {19, 19}, true, M, {18, 18}, 1, 1},
        // not TooMuch: hi 17 with T <= 20000; hi 17 alone at 16905; hi 16 with T over 20000; a Least pick
        // with hi 19
        {"hi 17 fits", &kSmall, {8, 17}, true, N, {}, 797, 0},
        {"single 17 fits", &kMed, {17, 17}, true, N, {}, 16905, 0},
        {"hi 16 is never too much", &kBig, {0, 16}, true, N, {}, 28067231, 0},
        {"a Least pick belongs to its own rule", &kTiny, {8, 19}, false, N, {}, 12, 0},
        // TooLittle: lo <= 4
        {"lo 2 hi 10", &kMed, {2, 10}, false, L, {5, 10}, 12, 9},
        {"lo 0 hi 21 tiny", &kTiny, {0, 21}, false, L, {5, 21}, 24, 19},
        {"lo 4 hi 5", &kMed, {4, 5}, false, L, {5, 5}, 2, 1},
        {"hi below 5, lo < hi", &kMed, {1, 3}, false, L, {3, 3}, 3, 1},
        {"lo == hi <= 4", &kMed, {3, 3}, false, L, {5, 5}, 1, 1},
        {"lo == hi == 0", &kMed, {0, 0}, false, L, {5, 5}, 1, 1},
        {"lo 5 is useful", &kMed, {5, 9}, false, N, {}, 5, 0},
        {"a Most pick with lo 0 is not too little", &kMed, {0, 10}, true, N, {}, 14, 0},
    };
    for (const Row& r : rows) {
        const DetailAdvice a = detailAdvice(*r.box, r.range, r.most);
        EXPECT_EQ(a.kind, r.kind) << r.what;
        EXPECT_EQ(a.tiles, r.tiles) << r.what;
        if (r.kind == N)
            continue;
        EXPECT_EQ(a.suggested, r.suggested) << r.what;
        EXPECT_EQ(a.suggestedTiles, r.suggestedTiles) << r.what;
    }
    EXPECT_GT(detailAdvice(kMed, {18, 19}, true).suggestedTiles, kDetailWarnTiles);
}

} // namespace cg
