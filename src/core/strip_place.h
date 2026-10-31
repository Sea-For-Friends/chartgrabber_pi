// SPDX-License-Identifier: GPL-3.0-or-later
// The job strip's place on the primary canvas. Pure integer arithmetic in canvas client pixels; the wx caller converts DIP. No wx, never
// throws.
#pragma once

#include <optional>
#include <string_view>

namespace cg {
enum class StripCorner { BottomLeft, BottomRight, TopLeft, TopRight };
struct StripRect {
    int x = 0, y = 0, width = 0, height = 0;
};
// >= 0 each: distance from the safe area's corner to the strip's nearest corner (dx horizontal, dy vertical)
struct StripAnchor {
    StripCorner corner = StripCorner::BottomLeft;
    int dx = 0, dy = 0;
};
inline constexpr int kStripMargin = 8;    // DIP; also the clamp margin
inline constexpr int kStripDefaultDx = 8; // DIP
inline constexpr int kStripDefaultDy =
    32; // DIP above the chart bar: clears OpenCPN's scale bar (section 9.1)
inline constexpr int kStripMinWidth = 320;     // DIP
inline constexpr int kStripMinTextWidth = 260; // DIP: the headline never gets less

// Canvas client area minus the chart bar at the bottom: {0, 0, w, max(0, h - chartbarHeight)}.
StripRect safeArea(int canvasWidth, int canvasHeight, int chartbarHeight);
// The strip's rectangle for `a` with size (w, h) inside `safe`, then CLAMPED fully inside `safe` with
// `margin`; a strip wider (higher) than the room gets x = safe.x + margin (y = safe.y + margin). The anchor
// is never changed.
StripRect placeStrip(const StripAnchor& a, int w, int h, const StripRect& safe, int margin);
// Window host (screen pixels): `frame` fitted into the display's work area `work` with `margin`. The height
// is capped at work.height - 2 * margin (never < 1); a right edge past the work area moves the frame left, a
// bottom edge past it moves it up (the other side is kept); then x >= work.x + margin and y >= work.y +
// margin. Width unchanged.
StripRect fitInWorkArea(const StripRect& frame, const StripRect& work, int margin);
// After a drop at `strip` (already clamped): the corner of `safe` nearest to the strip's centre (ties: bottom
// before top, left before right) and the distances from that corner to the strip's nearest corner (>= 0).
StripAnchor anchorFromDrop(const StripRect& strip, const StripRect& safe);
// A press becomes a drag when the pointer moved MORE than 6 px (12 in touch mode) on either axis.
bool dragStarted(int movedX, int movedY, bool touch);
// Width: min(bestWidth, max(minWidth, canvasWidth * 60 / 100)), then at most canvasWidth - 2 * margin; never
// < 1.
int stripWidth(int bestWidth, int canvasWidth, int minWidth, int margin);
// Config text of a corner: "bl" "br" "tl" "tr" (exactly, lower case); anything else gives nullopt.
std::string_view cornerKey(StripCorner c);
std::optional<StripCorner> parseCornerKey(std::string_view key);
} // namespace cg
