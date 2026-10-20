// SPDX-License-Identifier: GPL-3.0-or-later
// The area tool : remembers the viewport of
// every canvas, takes the current view, draws a rectangle by mouse drag or by two taps, and outlines the
// shown area. GUI thread only. Mouse event pixels are canvas client pixels in
// logical (DIP) units; PlugIn_ViewPort, GetCanvasLLPix and GetCanvasPixLL use physical pixels = logical x
// OCPN_GetDisplayContentScaleFactor.
#pragma once

#include "geo_math.h"
#include "types.h"

#include <wx/colour.h>
#include <wx/dc.h>
#include <wx/event.h>
#include <wx/gdicmn.h>

#include <array>
#include <cstdint> // ocpn_plugin.h uses uint8_t / uint64_t without including it
#include <functional>
#include <map>
#include <optional>

#include <ocpn_plugin.h>

// normalizeBox of the two corners; an AreaError is logged ("ChartGrabber: area error: <name>") and
// nothing is returned. Used for the current view and for a drawn rectangle.
// On an AreaError *error is set when error != nullptr.
std::optional<cg::BBox> checkedBox(cg::LatLon a, cg::LatLon b, cg::AreaError* error = nullptr);

class AreaTool {
public:
    // outline: colour of the outline now (Look::outline); touch: Look::touch. Both are called on
    // every use.
    AreaTool(std::function<wxColour()> outline, std::function<bool()> touch);

    void rememberViewport(int canvasIndex, const PlugIn_ViewPort& vp);
    // Viewport of the focus canvas (PluginGetFocusCanvas against GetCanvasByIndex(i)); canvas 0 when the
    // focus canvas is none of them; nullopt when that canvas has not been drawn yet.
    std::optional<PlugIn_ViewPort> focusViewport() const;

    // Starts drawing; done gets the box (outline kept) or nullopt (cancelled). A running drawing is
    // cancelled.
    void startDrawing(std::function<void(std::optional<cg::BBox>)> done);
    bool drawing() const { return drawing_; }
    bool onMouse(wxMouseEvent& event); // true = consumed

    void setShownArea(std::optional<cg::BBox> box);
    void drawDC(wxDC& dc, PlugIn_ViewPort& vp) const;
    void drawGL(PlugIn_ViewPort& vp) const;

    // Cancels a running drawing with done(nullopt) (Cancel button of the hint bar, DeInit).
    void cancel() { finish(std::nullopt); }
    // A running drawing ends WITHOUT calling done (its owner is going away); the shown area is unchanged.
    void abandon();

    // Called from onMouse when the second tap / the drag gives an AreaError, after the first corner was
    // cleared and the canvases refreshed; the tool stays in drawing mode. Empty: nothing.
    std::function<void(cg::AreaError)> onAreaError;

private:
    static constexpr int kTapPx = 5;     // a move of fewer pixels is a tap
    static constexpr int kCrossArm = 8;  // half length of the first-corner cross, px
    static constexpr int kLineWidth = 2; // px

    void finish(std::optional<cg::BBox> box);
    void refreshAll() const;
    cg::LatLon latLonAt(const wxPoint& p) const; // canvas pressCanvas_, its remembered viewport
    // Outline polygon and cross of one canvas, in pixels of that canvas's viewport.
    struct Shapes {
        std::optional<std::array<wxPoint, 4>> box;
        std::optional<std::array<wxPoint, 4>> cross; // two segments: p0-p1, p2-p3
    };
    Shapes shapes(PlugIn_ViewPort& vp) const;

    std::function<wxColour()> outline_;
    std::function<bool()> touch_;
    std::function<void(std::optional<cg::BBox>)> done_;
    std::map<int, PlugIn_ViewPort> viewports_;
    std::optional<cg::BBox> shown_;   // outline kept after a drawing / set by the New chart page
    std::optional<cg::BBox> live_;    // outline while dragging
    std::optional<cg::LatLon> first_; // first tap corner (cross)
    bool drawing_ = false;
    bool pressed_ = false; // left button down in drawing mode
    int pressCanvas_ = 0;
    wxPoint pressPx_;
    cg::LatLon pressLL_;
};
