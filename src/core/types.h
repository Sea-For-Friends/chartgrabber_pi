// SPDX-License-Identifier: GPL-3.0-or-later
// Shared plain types of the plugin core. No wx, no user-visible text.
#pragma once

namespace cg {

struct LatLon {
    double lat = 0, lon = 0;
};

// Degrees, west < east, south < north.
struct BBox {
    double west = 0, south = 0, east = 0, north = 0;
};

enum class AreaKind { View, Rectangle, Route };

// Detail bands of the New chart page.
enum class Band { Passage, Coastal, Approach, Harbour };

} // namespace cg
