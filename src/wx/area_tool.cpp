// SPDX-License-Identifier: GPL-3.0-or-later
#include "area_tool.h"

#include "geo_math.h"
#include "plugin_log.h"

#include <wx/log.h>
#include <wx/pen.h>

#include <GL/gl.h>
#include <algorithm>
#include <cmath>
#include <utility>

namespace {

const char* errorName(cg::AreaError e) {
    switch (e) {
    case cg::AreaError::OutsideMercator:
        return "OutsideMercator";
    case cg::AreaError::CrossesAntimeridian:
        return "CrossesAntimeridian";
    case cg::AreaError::Empty:
        return "Empty";
    case cg::AreaError::TooFewRoutePoints:
        return "TooFewRoutePoints";
    }
    return "Unknown";
}

double distancePx(const wxPoint& a, const wxPoint& b) {
    return std::hypot(double(a.x - b.x), double(a.y - b.y));
}

// The two corners as they are, no validation: the live outline while dragging.
cg::BBox rawBox(cg::LatLon a, cg::LatLon b) {
    return {std::min(a.lon, b.lon), std::min(a.lat, b.lat), std::max(a.lon, b.lon), std::max(a.lat, b.lat)};
}

} // namespace

std::optional<cg::BBox> checkedBox(cg::LatLon a, cg::LatLon b, cg::AreaError* error) {
    const auto box = cg::normalizeBox(a, b);
    if (!box) {
        if (error)
            *error = box.error();
        cg::logLine(wxString::Format("area error: %s", errorName(box.error())));
        return std::nullopt;
    }
    return *box;
}

AreaTool::AreaTool(std::function<wxColour()> outline, std::function<bool()> touch)
    : outline_(std::move(outline)), touch_(std::move(touch)) {}

void AreaTool::rememberViewport(int canvasIndex, const PlugIn_ViewPort& vp) {
    viewports_[canvasIndex] = vp;
}

std::optional<PlugIn_ViewPort> AreaTool::focusViewport() const {
    int index = 0;
    wxWindow* focus = PluginGetFocusCanvas();
    for (int i = 0; i < GetCanvasCount(); ++i) {
        if (focus && GetCanvasByIndex(i) == focus) {
            index = i;
            break;
        }
    }
    const auto it = viewports_.find(index);
    if (it == viewports_.end())
        return std::nullopt;
    return it->second;
}

void AreaTool::startDrawing(std::function<void(std::optional<cg::BBox>)> done) {
    if (drawing_)
        finish(std::nullopt);
    done_ = std::move(done);
    drawing_ = true;
    pressed_ = false;
    live_.reset();
    first_.reset();
    refreshAll();
}

void AreaTool::setShownArea(std::optional<cg::BBox> box) {
    shown_ = box;
    refreshAll();
}

void AreaTool::abandon() {
    if (!drawing_)
        return;
    done_ = nullptr;
    finish(std::nullopt);
}

void AreaTool::refreshAll() const {
    for (int i = 0; i < GetCanvasCount(); ++i)
        if (wxWindow* w = GetCanvasByIndex(i))
            RequestRefresh(w);
}

void AreaTool::finish(std::optional<cg::BBox> box) {
    if (!drawing_)
        return;
    drawing_ = false;
    pressed_ = false;
    live_.reset();
    first_.reset();
    if (box)
        shown_ = box;
    auto done = std::move(done_);
    done_ = nullptr;
    refreshAll();
    if (done)
        done(box);
}

cg::LatLon AreaTool::latLonAt(const wxPoint& p) const {
    PlugIn_ViewPort vp = viewports_.at(pressCanvas_); // LeftDown stores pressCanvas_ only with a viewport
    cg::LatLon ll;
    // Event pixels are logical (DIP); the viewport is in physical pixels (chcanv.cpp:755-758, 6625-6629).
    const double scale = OCPN_GetDisplayContentScaleFactor();
    GetCanvasLLPix(&vp, wxPoint(int(std::lround(p.x * scale)), int(std::lround(p.y * scale))), &ll.lat,
                   &ll.lon);
    return ll;
}

bool AreaTool::onMouse(wxMouseEvent& event) {
    if (!drawing_)
        return false;
    if (event.RightDown()) { // in touch mode a long press arrives as RightDown (chcanv.cpp:1052-1089)
        finish(std::nullopt);
        return true;
    }
    if (event.LeftDown()) {
        pressed_ = false;
        const int index = GetCanvasIndexUnderMouse();
        if (!viewports_.contains(index)) // a canvas OpenCPN has not drawn yet: ignore the press
            return true;
        pressCanvas_ = index;
        pressPx_ = event.GetPosition();
        pressLL_ = latLonAt(pressPx_);
        pressed_ = true;
        return true;
    }
    if (event.Dragging() && event.LeftIsDown()) {
        if (pressed_ && !touch_() && distancePx(event.GetPosition(), pressPx_) >= kTapPx) {
            live_ = rawBox(pressLL_, latLonAt(event.GetPosition()));
            first_.reset();
            if (wxWindow* w = GetCanvasByIndex(pressCanvas_))
                RequestRefresh(w);
        }
        return true;
    }
    if (event.LeftUp()) {
        if (!pressed_)
            return true;
        pressed_ = false;
        live_.reset();
        const wxPoint pos = event.GetPosition();
        const cg::LatLon ll = latLonAt(pos);
        cg::LatLon a = ll;
        if (distancePx(pos, pressPx_) >= kTapPx) {
            if (touch_()) { // touch: only two taps draw
                refreshAll();
                return true;
            }
            a = pressLL_;
        } else if (!first_) { // first tap
            first_ = ll;
            refreshAll();
            return true;
        } else {
            a = *first_;
        }
        cg::AreaError error = cg::AreaError::Empty;
        if (const auto box = checkedBox(a, ll, &error))
            finish(box);
        else {
            first_.reset(); // AreaError logged: the user draws again
            refreshAll();
            if (onAreaError)
                onAreaError(error);
        }
        return true;
    }
    return event.LeftDClick();
}

AreaTool::Shapes AreaTool::shapes(PlugIn_ViewPort& vp) const {
    Shapes s;
    const std::optional<cg::BBox> box = live_ ? live_ : (drawing_ ? std::nullopt : shown_);
    if (box) {
        std::array<wxPoint, 4> p;
        GetCanvasPixLL(&vp, &p[0], box->south, box->west);
        GetCanvasPixLL(&vp, &p[1], box->south, box->east);
        GetCanvasPixLL(&vp, &p[2], box->north, box->east);
        GetCanvasPixLL(&vp, &p[3], box->north, box->west);
        s.box = p;
    }
    if (first_) {
        wxPoint c;
        GetCanvasPixLL(&vp, &c, first_->lat, first_->lon);
        s.cross = std::array<wxPoint, 4>{wxPoint(c.x - kCrossArm, c.y), wxPoint(c.x + kCrossArm, c.y),
                                         wxPoint(c.x, c.y - kCrossArm), wxPoint(c.x, c.y + kCrossArm)};
    }
    return s;
}

void AreaTool::drawDC(wxDC& dc, PlugIn_ViewPort& vp) const {
    const Shapes s = shapes(vp);
    if (!s.box && !s.cross)
        return;
    dc.SetPen(wxPen(outline_(), kLineWidth));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    if (s.box)
        dc.DrawPolygon(4, s.box->data());
    if (s.cross) {
        dc.DrawLine((*s.cross)[0], (*s.cross)[1]);
        dc.DrawLine((*s.cross)[2], (*s.cross)[3]);
    }
}

void AreaTool::drawGL(PlugIn_ViewPort& vp) const {
    const Shapes s = shapes(vp);
    if (!s.box && !s.cross)
        return;
    const wxColour c = outline_();
    glPushAttrib(GL_CURRENT_BIT | GL_LINE_BIT | GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4ub(c.Red(), c.Green(), c.Blue(), 255);
    glLineWidth(kLineWidth);
    if (s.box) {
        glBegin(GL_LINE_LOOP);
        for (const wxPoint& p : *s.box)
            glVertex2i(p.x, p.y);
        glEnd();
    }
    if (s.cross) {
        glBegin(GL_LINES);
        for (const wxPoint& p : *s.cross)
            glVertex2i(p.x, p.y);
        glEnd();
    }
    glPopAttrib();
}
