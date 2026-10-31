// SPDX-License-Identifier: GPL-3.0-or-later
#include "strip_place.h"

#include <algorithm>
#include <cstdlib>

namespace cg {

StripRect safeArea(int canvasWidth, int canvasHeight, int chartbarHeight) {
    return {0, 0, canvasWidth, std::max(0, canvasHeight - chartbarHeight)};
}

namespace {
bool isRight(StripCorner c) {
    return c == StripCorner::BottomRight || c == StripCorner::TopRight;
}
bool isBottom(StripCorner c) {
    return c == StripCorner::BottomLeft || c == StripCorner::BottomRight;
}
// Clamp into [lo, hi]; when the room is too small (hi < lo) the low edge wins.
int clampLow(int v, int lo, int hi) {
    return std::max(lo, std::min(v, hi));
}
} // namespace

StripRect placeStrip(const StripAnchor& a, int w, int h, const StripRect& safe, int margin) {
    int x = isRight(a.corner) ? safe.x + safe.width - a.dx - w : safe.x + a.dx;
    int y = isBottom(a.corner) ? safe.y + safe.height - a.dy - h : safe.y + a.dy;
    x = clampLow(x, safe.x + margin, safe.x + safe.width - margin - w);
    y = clampLow(y, safe.y + margin, safe.y + safe.height - margin - h);
    return {x, y, w, h};
}

StripRect fitInWorkArea(const StripRect& frame, const StripRect& work, int margin) {
    StripRect r = frame;
    r.height = std::max(std::min(r.height, work.height - 2 * margin), 1);
    r.x = std::max(std::min(r.x, work.x + work.width - margin - r.width), work.x + margin);
    r.y = std::max(std::min(r.y, work.y + work.height - margin - r.height), work.y + margin);
    return r;
}

StripAnchor anchorFromDrop(const StripRect& strip, const StripRect& safe) {
    // Doubled coordinates keep the centre exact: 2 * centre = 2 * origin + size.
    const bool right = 2 * strip.x + strip.width > 2 * safe.x + safe.width;
    const bool bottom = 2 * strip.y + strip.height >= 2 * safe.y + safe.height;
    StripAnchor a;
    a.corner = bottom ? (right ? StripCorner::BottomRight : StripCorner::BottomLeft)
                      : (right ? StripCorner::TopRight : StripCorner::TopLeft);
    a.dx = right ? safe.x + safe.width - (strip.x + strip.width) : strip.x - safe.x;
    a.dy = bottom ? safe.y + safe.height - (strip.y + strip.height) : strip.y - safe.y;
    a.dx = std::max(0, a.dx);
    a.dy = std::max(0, a.dy);
    return a;
}

bool dragStarted(int movedX, int movedY, bool touch) {
    const int limit = touch ? 12 : 6;
    return std::abs(movedX) > limit || std::abs(movedY) > limit;
}

int stripWidth(int bestWidth, int canvasWidth, int minWidth, int margin) {
    int w = std::min(bestWidth, std::max(minWidth, canvasWidth * 60 / 100));
    w = std::min(w, canvasWidth - 2 * margin);
    return std::max(w, 1);
}

std::string_view cornerKey(StripCorner c) {
    switch (c) {
    case StripCorner::BottomLeft:
        return "bl";
    case StripCorner::BottomRight:
        return "br";
    case StripCorner::TopLeft:
        return "tl";
    case StripCorner::TopRight:
        return "tr";
    }
    return "bl"; // unreachable for a valid enum value
}

std::optional<StripCorner> parseCornerKey(std::string_view key) {
    if (key == "bl")
        return StripCorner::BottomLeft;
    if (key == "br")
        return StripCorner::BottomRight;
    if (key == "tl")
        return StripCorner::TopLeft;
    if (key == "tr")
        return StripCorner::TopRight;
    return std::nullopt;
}

} // namespace cg
