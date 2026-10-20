// SPDX-License-Identifier: GPL-3.0-or-later
// Area arithmetic, Web Mercator tile counts, scale labels, detail bands and zoom lists (geo_math.h).
#include "geo_math.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <numbers>
#include <optional>

namespace cg {

namespace {

constexpr double kEarthCircumferenceM = 40075016.686; // Web Mercator world width in metres (section 6.5)
constexpr double kTileSizePx = 256.0;                 // Web Mercator tile size (section 6.5)
constexpr double kEquatorMetresPerPixelZ0 = 156543.034;
constexpr double kMetresPerInch = 0.0254;
constexpr double kMetresPerNauticalMile = 1852.0;
constexpr double kMetresPerDegreeLat = 111320.0; // section 6.3: latitude degrees = m / 111 320

double toRadians(double degrees) {
    return degrees * std::numbers::pi / 180.0;
}

// Web Mercator tile coordinates as fractions of the 2^z grid: x from the west edge, y from the north edge.
double tileX(double lon, double n) {
    return (lon + 180.0) / 360.0 * n;
}
double tileY(double lat, double n) {
    const double rad = toRadians(std::clamp(lat, -kMaxLat, kMaxLat));
    return (1.0 - std::asinh(std::tan(rad)) / std::numbers::pi) / 2.0 * n;
}
// Index of the tile holding the fraction, kept inside the grid (the east and south edges sit exactly on n).
double tileIndex(double fraction, double n) {
    return std::clamp(std::floor(fraction), 0.0, n - 1.0);
}

std::string_view trimmed(std::string_view s) {
    constexpr std::string_view kSpace = " \t\r\n\v\f";
    const std::size_t first = s.find_first_not_of(kSpace);
    if (first == std::string_view::npos)
        return {};
    return s.substr(first, s.find_last_not_of(kSpace) - first + 1);
}

// One to three ASCII digits, the CLI's \d{1,3}.
std::optional<int> parseNumber(std::string_view s) {
    if (s.empty() || s.size() > 3)
        return std::nullopt;
    int value = 0;
    for (char c : s) {
        if (c < '0' || c > '9')
            return std::nullopt;
        value = value * 10 + (c - '0');
    }
    return value;
}

} // namespace

std::expected<BBox, AreaError> normalizeBox(LatLon a, LatLon b) {
    if (a.lat == b.lat || a.lon == b.lon)
        return std::unexpected(AreaError::Empty);
    const double south = std::clamp(std::min(a.lat, b.lat), -kMaxLat, kMaxLat);
    const double north = std::clamp(std::max(a.lat, b.lat), -kMaxLat, kMaxLat);
    if (south >= north)
        return std::unexpected(AreaError::OutsideMercator);

    double west = std::min(a.lon, b.lon);
    double east = std::max(a.lon, b.lon);
    if (east - west >= 360.0) {
        west = -180.0;
        east = 180.0;
    } else {
        const double shift = 360.0 * std::floor((west + 180.0) / 360.0);
        west -= shift;
        east -= shift;
        if (east > 180.0)
            return std::unexpected(AreaError::CrossesAntimeridian);
    }
    const auto round8 = [](double v) { return std::round(v * 1e8) / 1e8; };
    return BBox{round8(west), round8(south), round8(east), round8(north)};
}

std::expected<void, AreaError> checkRoute(const std::vector<LatLon>& points) {
    if (points.size() < 2)
        return std::unexpected(AreaError::TooFewRoutePoints);
    double west = points.front().lon;
    double east = west;
    for (const LatLon& p : points) {
        west = std::min(west, p.lon);
        east = std::max(east, p.lon);
    }
    // The CLI's own refusal: core job_area.cpp:122-124 (east - west > 180).
    if (east - west > 180.0)
        return std::unexpected(AreaError::CrossesAntimeridian);
    return {};
}

BBox routeBounds(const std::vector<LatLon>& points, double corridorNm) {
    if (points.empty())
        return {}; // unreachable: the route page runs checkRoute() first (two points or more)
    BBox box{points.front().lon, points.front().lat, points.front().lon, points.front().lat};
    for (const LatLon& p : points) {
        box.west = std::min(box.west, p.lon);
        box.east = std::max(box.east, p.lon);
        box.south = std::min(box.south, p.lat);
        box.north = std::max(box.north, p.lat);
    }
    // Half the total width, in metres (the CLI grows the route by the same radius: job_area.cpp:167).
    const double halfMetres = corridorNm * kMetresPerNauticalMile / 2.0;
    const double dLat = halfMetres / kMetresPerDegreeLat;
    // Longitude degrees widen towards the poles: use the edge latitude nearest a pole (the widest case).
    const double edgeLat = std::max(std::fabs(box.south), std::fabs(box.north));
    const double dLon = halfMetres / (kMetresPerDegreeLat * std::cos(toRadians(edgeLat)));
    box.west -= dLon;
    box.east += dLon;
    box.south -= dLat;
    box.north += dLat;
    return box;
}

double viewZoom(double viewScalePpm, double centerLat) {
    return std::log2(viewScalePpm * kEarthCircumferenceM * std::cos(toRadians(centerLat)) / kTileSizePx);
}

std::int64_t bboxTileCount(const BBox& box, int zoom) {
    const double n = std::ldexp(1.0, zoom);
    const double columns = tileIndex(tileX(box.east, n), n) - tileIndex(tileX(box.west, n), n) + 1.0;
    const double rows = tileIndex(tileY(box.south, n), n) - tileIndex(tileY(box.north, n), n) + 1.0;
    return static_cast<std::int64_t>(columns * rows);
}

double scaleDenominator(int zoom, double lat, double dpi) {
    return kEquatorMetresPerPixelZ0 * std::cos(toRadians(lat)) / std::ldexp(1.0, zoom) * dpi / kMetresPerInch;
}

std::string formatScale(double denominator) {
    if (!(denominator > 0))
        return {}; // unreachable: the page passes scaleDenominator() with dpi > 0
    // Two significant digits, then the unit.
    const double step = std::pow(10.0, std::floor(std::log10(denominator)) - 1.0);
    const double r = std::round(denominator / step) * step;
    // to_chars: locale-independent, libstdc++ 11+ (the C++20 format header needs 13; Debian 12 has GCC 12).
    const auto text = [](double v, int decimals, const char* unit) {
        char buf[32];
        const auto end = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::fixed, decimals).ptr;
        return "1:" + std::string(buf, end) + unit;
    };
    if (r >= 1e7)
        return text(r / 1e6, 0, "M");
    if (r >= 1e6)
        return text(r / 1e6, 1, "M");
    if (r >= 1e4)
        return text(r / 1e3, 0, "k");
    if (r >= 1e3)
        return text(r / 1e3, 1, "k");
    return text(r, 0, "");
}

std::pair<int, int> bandZooms(Band b) {
    switch (b) {
    case Band::Passage:
        return {8, 11};
    case Band::Coastal:
        return {12, 14};
    case Band::Approach:
        return {15, 16};
    case Band::Harbour:
        return {17, 18};
    }
    return {0, 0}; // unreachable: every Band is listed above
}

std::set<Band> makeContiguous(const std::set<Band>& ticked) {
    std::set<Band> out;
    if (ticked.empty())
        return out;
    for (int b = static_cast<int>(*ticked.begin()); b <= static_cast<int>(*ticked.rbegin()); ++b) {
        out.insert(static_cast<Band>(b));
    }
    return out;
}

std::vector<int> zoomsForBands(const std::set<Band>& ticked) {
    std::vector<int> zooms;
    for (Band b : makeContiguous(ticked)) {
        const auto [from, to] = bandZooms(b);
        for (int z = from; z <= to; ++z)
            zooms.push_back(z);
    }
    return zooms;
}

std::expected<std::vector<int>, std::string> parseZoomList(std::string_view text) {
    std::vector<int> zooms;
    std::string_view rest = text;
    while (true) {
        const std::size_t comma = rest.find(',');
        const std::string_view part = trimmed(rest.substr(0, comma));
        const std::size_t dash = part.find('-');
        const std::optional<int> from = parseNumber(part.substr(0, dash));
        const std::optional<int> to =
            dash == std::string_view::npos ? from : parseNumber(part.substr(dash + 1));
        if (!from || !to || *from > *to || *to > kMaxZoom)
            return std::unexpected(std::string(text));
        for (int z = *from; z <= *to; ++z)
            zooms.push_back(z);
        if (comma == std::string_view::npos)
            break;
        rest.remove_prefix(comma + 1);
    }
    std::ranges::sort(zooms);
    const auto duplicates = std::ranges::unique(zooms);
    zooms.erase(duplicates.begin(), duplicates.end());
    return zooms;
}

std::string formatZoomList(const std::vector<int>& zooms) {
    std::vector<int> sorted = zooms;
    std::ranges::sort(sorted);
    const auto duplicates = std::ranges::unique(sorted);
    sorted.erase(duplicates.begin(), duplicates.end());

    std::string out;
    std::size_t i = 0;
    while (i < sorted.size()) {
        std::size_t last = i;
        while (last + 1 < sorted.size() && sorted[last + 1] == sorted[last] + 1)
            ++last;
        if (!out.empty())
            out += ',';
        out += std::to_string(sorted[i]);
        if (last > i)
            out += '-' + std::to_string(sorted[last]);
        i = last + 1;
    }
    return out;
}

double fitScalePpm(const BBox& box, int pixWidth, int pixHeight) {
    // Ground metres of the box: longitude degrees shrink by cos(latitude), as in routeBounds.
    const double widthM = (box.east - box.west) * kMetresPerDegreeLat * std::cos(toRadians(centre(box).lat));
    const double heightM = (box.north - box.south) * kMetresPerDegreeLat;
    return std::min(pixWidth / widthM, pixHeight / heightM);
}

LatLon centre(const BBox& box) {
    return LatLon{(box.south + box.north) / 2.0, (box.west + box.east) / 2.0};
}

std::vector<int> limitZooms(const std::vector<int>& zooms, std::optional<int> limit) {
    if (!limit) {
        return zooms;
    }
    std::vector<int> out;
    std::copy_if(zooms.begin(), zooms.end(), std::back_inserter(out), [&](int z) { return z <= *limit; });
    if (out.empty() && !zooms.empty()) {
        out.push_back(*limit);
    }
    return out;
}

std::vector<int> jobZooms(const std::vector<int>& zooms, std::optional<int> baseLimit,
                          std::optional<int> overlayLimit) {
    std::vector<int> z = limitZooms(zooms, baseLimit);
    if (overlayLimit && !z.empty() && z.front() > *overlayLimit) {
        z.insert(z.begin(), *overlayLimit);
    }
    return z;
}

std::optional<ZoomRange> rangeForBands(const std::set<Band>& ticked) {
    if (ticked.empty())
        return std::nullopt;
    return ZoomRange{bandZooms(*ticked.begin()).first, bandZooms(*ticked.rbegin()).second};
}

std::set<Band> bandsInRange(std::optional<ZoomRange> r) {
    std::set<Band> out;
    if (!r)
        return out;
    for (Band b : {Band::Passage, Band::Coastal, Band::Approach, Band::Harbour}) {
        const auto [from, to] = bandZooms(b);
        if (from >= r->lo && to <= r->hi)
            out.insert(b);
    }
    return out;
}

std::vector<Band> partialBands(std::optional<ZoomRange> r) {
    std::vector<Band> out;
    if (!r)
        return out;
    for (Band b : {Band::Passage, Band::Coastal, Band::Approach, Band::Harbour}) {
        const auto [from, to] = bandZooms(b);
        const bool shares = from <= r->hi && to >= r->lo;
        const bool inside = from >= r->lo && to <= r->hi;
        if (shares && !inside)
            out.push_back(b);
    }
    return out;
}

ZoomRange pickLeast(std::optional<ZoomRange> r, int z) {
    return ZoomRange{z, r ? std::max(r->hi, z) : z};
}

ZoomRange pickMost(std::optional<ZoomRange> r, int z) {
    return ZoomRange{r ? std::min(r->lo, z) : z, z};
}

std::optional<ZoomRange> rangeFromText(std::string_view text) {
    const auto zooms = parseZoomList(text); // sorted; an empty text is an error of parseZoomList
    if (!zooms || zooms->empty())
        return std::nullopt;
    return ZoomRange{zooms->front(), zooms->back()};
}

std::vector<int> rangeZooms(ZoomRange r) {
    std::vector<int> out;
    for (int z = r.lo; z <= r.hi; ++z)
        out.push_back(z);
    return out;
}

std::int64_t rangeTileCount(const BBox& box, int lo, int hi) {
    std::int64_t sum = 0;
    for (int z = lo; z <= hi; ++z)
        sum += bboxTileCount(box, z);
    return sum;
}

DetailAdvice detailAdvice(const BBox& box, ZoomRange r, bool pickedMost) {
    DetailAdvice a;
    a.tiles = rangeTileCount(box, r.lo, r.hi);
    if (pickedMost) {
        if (r.hi < 19 && !(r.hi >= 17 && a.tiles > kDetailWarnTiles))
            return a;
        a.kind = DetailWarning::TooMuch;
        // The highest z < hi, z <= 18, whose range fits: from lo when lo < hi, the single level z when lo ==
        // hi (z = 0 always fits; TooMuch has hi >= 17, so the loop below starts at 16 or more).
        const bool single = r.lo == r.hi;
        a.suggested = {r.lo, r.lo};
        for (int z = std::min(r.hi - 1, 18); z >= (single ? 0 : r.lo); --z)
            if (const int from = single ? z : r.lo; rangeTileCount(box, from, z) <= kDetailWarnTiles) {
                a.suggested = {from, z};
                break;
            }
    } else {
        if (r.lo > 4)
            return a;
        a.kind = DetailWarning::TooLittle;
        a.suggested = (r.hi >= 5 || r.lo == r.hi) ? ZoomRange{5, std::max(5, r.hi)} : ZoomRange{r.hi, r.hi};
    }
    a.suggestedTiles = rangeTileCount(box, a.suggested.lo, a.suggested.hi);
    return a;
}

} // namespace cg
