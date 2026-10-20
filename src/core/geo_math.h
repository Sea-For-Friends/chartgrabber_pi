// SPDX-License-Identifier: GPL-3.0-or-later
// Area arithmetic, Web Mercator tile counts, scale labels, detail bands and zoom lists.
// Pure functions: no wx, no state, no
// user-visible text (errors are enum values, except the rejected text of parseZoomList).
#pragma once

#include "types.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cg {

// The CLI's bound, (verified 2026-10-15).
inline constexpr double kMaxLat = 85.0511;

enum class AreaError { OutsideMercator, CrossesAntimeridian, Empty, TooFewRoutePoints };

// Corners in any order; section 6.4.
std::expected<BBox, AreaError> normalizeBox(LatLon a, LatLon b);
// Section 6.3: fewer than two points, or longitudes spanning more than 180 degrees.
std::expected<void, AreaError> checkRoute(const std::vector<LatLon>& points);
// Bounds of the points, grown by half the corridor width (corridorNm is the total width, nautical miles).
BBox routeBounds(const std::vector<LatLon>& points, double corridorNm);
// Section 6.5: zoom level of a view with the given scale (pixels per metre, ground) at centerLat.
double viewZoom(double viewScalePpm, double centerLat);
// Web Mercator tile count of the box at zoom (rows x columns); an edge exactly on a tile
// boundary counts one extra row/column (a safe over-count).
std::int64_t bboxTileCount(const BBox& box, int zoom);
// Scale denominator at zoom and lat (ground metres per pixel, converted at dpi): 156543.034 × cos(lat) / 2^z
// × dpi / 0.0254.
double scaleDenominator(int zoom, double lat, double dpi = 96);
// "1:2.3M", "1:290k", "1:4.5k", "1:900" (two significant digits).
std::string formatScale(double denominator);
// Passage 8-11, Coastal 12-14, Approach 15-16, Harbour 17-18.
std::pair<int, int> bandZooms(Band b);
// Contiguous: the bands from the lowest to the highest ticked band (ticked ones included).
std::vector<int> zoomsForBands(const std::set<Band>& ticked);
std::set<Band> makeContiguous(const std::set<Band>& ticked);
// The CLI's grammar (zoom list of the core's job.cpp), zooms 0..21; the error holds the rejected text.
std::expected<std::vector<int>, std::string> parseZoomList(std::string_view text);
// Sorted, de-duplicated, runs collapsed: {8, 9, 10, 12} → "8-10,12".
std::string formatZoomList(const std::vector<int>& zooms);
// Pixels per metre (ground) at which the box's larger relative side fills the given pixels.
double fitScalePpm(const BBox& box, int pixWidth, int pixHeight);
LatLon centre(const BBox& box);
// Zoom limits; `zooms` sorted and unique. nullopt limit: unchanged; else the zooms <= limit, or
// {limit} when that leaves none of a non-empty input (the source's last level).
std::vector<int> limitZooms(const std::vector<int>& zooms, std::optional<int> limit);
// Zooms of a base + overlay job: limitZooms by the base limit; when the overlay limit is set, the result is
// not empty and every zoom is above it, the overlay limit is inserted (sorted).
std::vector<int> jobZooms(const std::vector<int>& zooms, std::optional<int> baseLimit,
                          std::optional<int> overlayLimit);

// The detail of the New chart page is ONE contiguous range of levels.
inline constexpr int kMaxZoom = 21; // the drop-downs list 0..kMaxZoom (the CLI's grammar, parseZoomList)
struct ZoomRange {                  // 0 <= lo <= hi <= kMaxZoom; "nothing chosen" = std::nullopt
    int lo = 0, hi = 0;
    bool operator==(const ZoomRange&) const = default;
};
// zoomsForBands as a range; an empty set gives nullopt.
std::optional<ZoomRange> rangeForBands(const std::set<Band>& ticked);
// The bands whose bandZooms lie wholly inside r; nullopt gives {}.
std::set<Band> bandsInRange(std::optional<ZoomRange> r);
// Bands that share a level with r but are not wholly inside it, in band order (at most the two end bands).
std::vector<Band> partialBands(std::optional<ZoomRange> r);
// Picks in the drop-downs: the other end follows so that lo <= hi.
ZoomRange pickLeast(std::optional<ZoomRange> r, int z);
ZoomRange pickMost(std::optional<ZoomRange> r, int z);
// `LastZooms` text: parseZoomList ok and not empty gives {min, max} (a gapped "8,14" gives 8-14); else
// nullopt.
std::optional<ZoomRange> rangeFromText(std::string_view text);
std::vector<int> rangeZooms(ZoomRange r); // lo, lo+1, ..., hi

// the extreme-level warning. Pure arithmetic over bboxTileCount; no CLI run.
inline constexpr std::int64_t kDetailWarnTiles = 20000;
std::int64_t rangeTileCount(const BBox& box, int lo,
                            int hi); // sum of bboxTileCount, z = lo..hi; lo > hi -> 0
enum class DetailWarning { None, TooMuch, TooLittle };
struct DetailAdvice {
    DetailWarning kind = DetailWarning::None;
    ZoomRange suggested{0, 0};       // the whole range after "Use"; valid unless kind == None
    std::int64_t tiles = 0;          // rangeTileCount of the PICKED range
    std::int64_t suggestedTiles = 0; // rangeTileCount of `suggested`
};
// `pickedMost`: the pick was in "Most detail" (else "Least detail"); `r` = the range AFTER the pick.
DetailAdvice detailAdvice(const BBox& box, ZoomRange r, bool pickedMost);

} // namespace cg
