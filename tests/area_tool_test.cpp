// SPDX-License-Identifier: GPL-3.0-or-later
// Tests of the area tool. The OpenCPN API functions AreaTool calls are the shared fakes of
// ocpn_fakes.cpp (linear: view_scale_ppm pixels per degree, canvas centre (100,100) = (clat, clon)), so every
// expected box and pixel is a literal worked out by hand. The GL functions are recorders (no GL context, no
// display).
#include "area_tool.h"
#include "ocpn_fakes.h"

#include <gtest/gtest.h>

#include <wx/brush.h>
#include <wx/dc.h>
#include <wx/log.h>
#include <wx/pen.h>
#include <wx/window.h>

#include <GL/gl.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

// ---- fake OpenCPN ----

int g_dummy[3];
wxWindow* const kCanvas0 = reinterpret_cast<wxWindow*>(&g_dummy[0]); // never dereferenced
wxWindow* const kCanvas1 = reinterpret_cast<wxWindow*>(&g_dummy[1]);
wxWindow* const kStranger = reinterpret_cast<wxWindow*>(&g_dummy[2]);

struct Fake {
    std::vector<std::string> gl; // GL call log
    void reset() {
        *this = Fake();
        ocpn_fake::focus = nullptr; // the canvas fakes live in ocpn_fakes.cpp
        ocpn_fake::underMouse = 0;
        ocpn_fake::canvases = {kCanvas0, kCanvas1};
        ocpn_fake::contentScale = 1.0;
        ocpn_fake::refreshed.clear();
    }
} g_fake;

std::vector<std::string> g_log; // wxLog messages

class Capture : public wxLog {
public:
    void DoLogRecord(wxLogLevel, const wxString& msg, const wxLogRecordInfo&) override {
        g_log.push_back(msg.ToStdString());
    }
};

} // namespace

// GL recorders (declared by GL/gl.h; defined here, libGL is not linked).
void glPushAttrib(GLbitfield) {
    g_fake.gl.push_back("push");
}
void glPopAttrib() {
    g_fake.gl.push_back("pop");
}
void glDisable(GLenum c) {
    g_fake.gl.push_back("disable " + std::to_string(c));
}
void glEnable(GLenum c) {
    g_fake.gl.push_back("enable " + std::to_string(c));
}
void glBlendFunc(GLenum, GLenum) {}
void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a) {
    g_fake.gl.push_back("color " + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) +
                        "," + std::to_string(a));
}
void glLineWidth(GLfloat w) {
    g_fake.gl.push_back("width " + std::to_string(int(w)));
}
void glBegin(GLenum m) {
    g_fake.gl.push_back(m == GL_LINE_LOOP ? "loop" : m == GL_LINES ? "lines" : "other");
}
void glEnd() {
    g_fake.gl.push_back("end");
}
void glVertex2i(GLint x, GLint y) {
    g_fake.gl.push_back("v " + std::to_string(x) + "," + std::to_string(y));
}

namespace {

// A wxDC that records what AreaTool draws as text lines: "pen R,G,B,W", "poly x,y x,y...", "line x1,y1
// x2,y2".
class RecordingImpl : public wxDCImpl {
public:
    RecordingImpl(wxDC* owner, std::string* out) : wxDCImpl(owner), out_(out) {}
    bool CanDrawBitmap() const override { return false; }
    bool CanGetTextExtent() const override { return false; }
    void DoGetSize(int* w, int* h) const override { *w = *h = 200; }
    void DoGetSizeMM(int* w, int* h) const override { *w = *h = 50; }
    int GetDepth() const override { return 24; }
    wxSize GetPPI() const override { return wxSize(96, 96); }
    void SetFont(const wxFont&) override {}
    void SetPen(const wxPen& pen) override {
        m_pen = pen;
        const wxColour c = pen.GetColour();
        *out_ += "pen " + std::to_string(c.Red()) + "," + std::to_string(c.Green()) + "," +
                 std::to_string(c.Blue()) + "," + std::to_string(pen.GetWidth()) + "\n";
    }
    void SetBrush(const wxBrush& brush) override {
        m_brush = brush;
        if (brush.GetStyle() == wxBRUSHSTYLE_TRANSPARENT)
            *out_ += "brush transparent\n";
    }
    void SetBackground(const wxBrush&) override {}
    void SetBackgroundMode(int) override {}
    void SetPalette(const wxPalette&) override {}
    void SetLogicalFunction(wxRasterOperationMode) override {}
    wxCoord GetCharHeight() const override { return 10; }
    wxCoord GetCharWidth() const override { return 6; }
    void DoGetTextExtent(const wxString&, wxCoord*, wxCoord*, wxCoord*, wxCoord*,
                         const wxFont*) const override {}
    void Clear() override {}
    void DoSetClippingRegion(wxCoord, wxCoord, wxCoord, wxCoord) override {}
    void DoSetDeviceClippingRegion(const wxRegion&) override {}
    bool DoFloodFill(wxCoord, wxCoord, const wxColour&, wxFloodFillStyle) override { return false; }
    bool DoGetPixel(wxCoord, wxCoord, wxColour*) const override { return false; }
    void DoDrawPoint(wxCoord, wxCoord) override {}
    void DoDrawLine(wxCoord x1, wxCoord y1, wxCoord x2, wxCoord y2) override {
        *out_ += "line " + pt(x1, y1) + " " + pt(x2, y2) + "\n";
    }
    void DoDrawArc(wxCoord, wxCoord, wxCoord, wxCoord, wxCoord, wxCoord) override {}
    void DoDrawEllipticArc(wxCoord, wxCoord, wxCoord, wxCoord, double, double) override {}
    void DoDrawRectangle(wxCoord, wxCoord, wxCoord, wxCoord) override {}
    void DoDrawRoundedRectangle(wxCoord, wxCoord, wxCoord, wxCoord, double) override {}
    void DoDrawEllipse(wxCoord, wxCoord, wxCoord, wxCoord) override {}
    void DoCrossHair(wxCoord, wxCoord) override {}
    void DoDrawIcon(const wxIcon&, wxCoord, wxCoord) override {}
    void DoDrawBitmap(const wxBitmap&, wxCoord, wxCoord, bool) override {}
    void DoDrawText(const wxString&, wxCoord, wxCoord) override {}
    void DoDrawRotatedText(const wxString&, wxCoord, wxCoord, double) override {}
    bool DoBlit(wxCoord, wxCoord, wxCoord, wxCoord, wxDC*, wxCoord, wxCoord, wxRasterOperationMode, bool,
                wxCoord, wxCoord) override {
        return false;
    }
    void DoDrawLines(int, const wxPoint[], wxCoord, wxCoord) override {}
    void DoDrawPolygon(int n, const wxPoint p[], wxCoord, wxCoord, wxPolygonFillMode) override {
        *out_ += "poly";
        for (int i = 0; i < n; ++i)
            out_->append(" ").append(pt(p[i].x, p[i].y));
        *out_ += "\n";
    }

private:
    static std::string pt(int x, int y) { return std::to_string(x) + "," + std::to_string(y); }
    std::string* out_;
};

class RecordingDC : public wxDC {
public:
    explicit RecordingDC(std::string* out) : wxDC(new RecordingImpl(this, out)) {}
};

using Done = std::vector<std::optional<cg::BBox>>;

PlugIn_ViewPort makeVp(double clat, double clon) {
    PlugIn_ViewPort vp{};
    vp.clat = clat;
    vp.clon = clon;
    vp.view_scale_ppm = 10;
    vp.pix_width = vp.pix_height = 200;
    vp.bValid = true;
    return vp;
}

wxMouseEvent ev(wxEventType type, int x, int y, bool leftDown = false) {
    wxMouseEvent e(type);
    e.SetPosition(wxPoint(x, y));
    e.SetLeftDown(leftDown);
    return e;
}

class AreaToolTest : public ::testing::Test {
protected:
    void SetUp() override {
        g_fake.reset();
        g_log.clear();
        old_ = wxLog::SetActiveTarget(new Capture);
        tool = std::make_unique<AreaTool>([this] { return colour; }, [this] { return touch; });
        tool->rememberViewport(0, makeVp(50, 10));
        tool->rememberViewport(1, makeVp(-20, 100));
    }
    void TearDown() override { delete wxLog::SetActiveTarget(old_); }

    void start() {
        tool->startDrawing([this](std::optional<cg::BBox> b) { done.push_back(b); });
    }
    bool send(wxEventType t, int x, int y, bool leftDown = false) {
        wxMouseEvent e = ev(t, x, y, leftDown);
        return tool->onMouse(e);
    }
    void down(int x, int y) { send(wxEVT_LEFT_DOWN, x, y); }
    void drag(int x, int y) { send(wxEVT_MOTION, x, y, true); }
    void up(int x, int y) { send(wxEVT_LEFT_UP, x, y); }
    void tap(int x, int y) {
        down(x, y);
        up(x, y);
    }

    // What one DC drawing recorded (see RecordingImpl).
    std::string svg(PlugIn_ViewPort vp) {
        std::string out;
        RecordingDC dc(&out);
        tool->drawDC(dc, vp);
        return out;
    }
    static bool has(const std::string& s, const std::string& part) {
        return s.find(part) != std::string::npos;
    }
    std::vector<std::string> gl(PlugIn_ViewPort vp) {
        g_fake.gl.clear();
        tool->drawGL(vp);
        return g_fake.gl;
    }

    std::unique_ptr<AreaTool> tool;
    wxColour colour{128, 0, 0};
    bool touch = false;
    Done done;
    wxLog* old_ = nullptr;
};

// The box of the first canvas viewport, drag (100,100) -> (150,60): lon 10..15, lat 50..54.
void expectBox(const std::optional<cg::BBox>& b, double w, double s, double e, double n) {
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->west, w);
    EXPECT_EQ(b->south, s);
    EXPECT_EQ(b->east, e);
    EXPECT_EQ(b->north, n);
}

// ---- checkedBox (proof 6) ----

TEST_F(AreaToolTest, CheckedBoxReturnsTheNormalizedBox) {
    expectBox(checkedBox({54, 15}, {50, 10}), 10, 50, 15, 54);
    EXPECT_TRUE(g_log.empty());
}

TEST_F(AreaToolTest, CheckedBoxLogsEachAreaErrorByName) {
    EXPECT_FALSE(checkedBox({10, 170}, {20, 190}).has_value()); // east of the antimeridian
    EXPECT_FALSE(checkedBox({86, 0}, {89, 10}).has_value());    // nothing left inside +-85.0511
    EXPECT_FALSE(checkedBox({10, 5}, {20, 5}).has_value());     // zero width
    ASSERT_EQ(g_log.size(), 3u);
    EXPECT_EQ(g_log[0], "ChartGrabber: area error: CrossesAntimeridian");
    EXPECT_EQ(g_log[1], "ChartGrabber: area error: OutsideMercator");
    EXPECT_EQ(g_log[2], "ChartGrabber: area error: Empty");
}

TEST_F(AreaToolTest, CheckedBoxClampsLatitudeBeyondTheLimitWithoutAnError) {
    const auto b = checkedBox({-89, 0}, {89, 10});
    expectBox(b, 0, -85.0511, 10, 85.0511);
    EXPECT_TRUE(g_log.empty());
}

// ---- viewport cache and focus (proof 1) ----

TEST_F(AreaToolTest, FocusViewportIsTheFocusedCanvas) {
    ocpn_fake::focus = kCanvas1;
    ASSERT_TRUE(tool->focusViewport().has_value());
    EXPECT_EQ(tool->focusViewport()->clat, -20);
    EXPECT_EQ(tool->focusViewport()->clon, 100);
    ocpn_fake::focus = kCanvas0;
    EXPECT_EQ(tool->focusViewport()->clat, 50);
}

TEST_F(AreaToolTest, FocusOnNoKnownCanvasFallsBackToCanvasZero) {
    ocpn_fake::focus = nullptr;
    EXPECT_EQ(tool->focusViewport()->clat, 50);
    ocpn_fake::focus = kStranger;
    EXPECT_EQ(tool->focusViewport()->clat, 50);
}

TEST_F(AreaToolTest, FocusViewportIsEmptyUntilThatCanvasWasDrawnAndFollowsLaterFrames) {
    AreaTool fresh([] { return wxColour(); }, [] { return false; });
    EXPECT_FALSE(fresh.focusViewport().has_value());
    fresh.rememberViewport(0, makeVp(1, 2));
    fresh.rememberViewport(0, makeVp(3, 4));
    EXPECT_EQ(fresh.focusViewport()->clat, 3);
    ocpn_fake::focus = kCanvas1; // canvas 1 has no viewport yet
    EXPECT_FALSE(fresh.focusViewport().has_value());
}

// ---- drag mode (proof 2) ----

TEST_F(AreaToolTest, DragGivesTheBoxOfTheTwoCornersAndIsConsumed) {
    start();
    EXPECT_TRUE(tool->drawing());
    EXPECT_TRUE(send(wxEVT_LEFT_DOWN, 100, 100));
    EXPECT_TRUE(send(wxEVT_MOTION, 150, 60, true));
    EXPECT_TRUE(done.empty());
    EXPECT_TRUE(send(wxEVT_LEFT_UP, 150, 60));
    ASSERT_EQ(done.size(), 1u);
    expectBox(done[0], 10, 50, 15, 54);
    EXPECT_FALSE(tool->drawing());
}

TEST_F(AreaToolTest, EventPixelsAreScaledToPhysicalPixelsBeforeTheViewport) {
    ocpn_fake::contentScale = 2.0; // HiDPI: event (50,50) is physical (100,100)
    start();
    down(50, 50);
    drag(75, 30);
    up(75, 30);
    ASSERT_EQ(done.size(), 1u);
    expectBox(done[0], 10, 50, 15, 54);
}

TEST_F(AreaToolTest, DragInTheOtherDirectionGivesTheSameBox) {
    start();
    down(150, 60);
    drag(100, 100);
    up(100, 100);
    ASSERT_EQ(done.size(), 1u);
    expectBox(done[0], 10, 50, 15, 54);
}

TEST_F(AreaToolTest, DragUsesTheViewportOfTheCanvasUnderTheMouse) {
    ocpn_fake::underMouse = 1;
    start();
    down(100, 100);
    drag(120, 120);
    up(120, 120);
    ASSERT_EQ(done.size(), 1u);
    expectBox(done[0], 100, -22, 102, -20);
}

TEST_F(AreaToolTest, EventsOutsideDrawingModePassThrough) {
    EXPECT_FALSE(send(wxEVT_LEFT_DOWN, 100, 100));
    EXPECT_FALSE(send(wxEVT_MOTION, 120, 120, true));
    EXPECT_FALSE(send(wxEVT_LEFT_UP, 120, 120));
    EXPECT_FALSE(send(wxEVT_RIGHT_DOWN, 1, 1));
    EXPECT_FALSE(send(wxEVT_RIGHT_UP, 1, 1)); // no cancel happened: the OpenCPN menu must open
}

TEST_F(AreaToolTest, WhileDrawingMovingAndWheelPassThroughButDoubleClickIsConsumed) {
    start();
    EXPECT_FALSE(send(wxEVT_MOTION, 10, 10)); // no button: the cursor position stays live in OpenCPN
    EXPECT_FALSE(send(wxEVT_MOUSEWHEEL, 10, 10));
    EXPECT_TRUE(send(wxEVT_LEFT_DCLICK, 10, 10));
}

TEST_F(AreaToolTest, DraggingWithAnotherButtonPassesThrough) {
    start();
    wxMouseEvent e(wxEVT_MOTION);
    e.SetPosition(wxPoint(10, 10));
    e.SetMiddleDown(true);
    ASSERT_TRUE(e.Dragging());
    EXPECT_FALSE(tool->onMouse(e));
}

TEST_F(AreaToolTest, LeftUpWithoutPressIsConsumedAndDoesNothing) {
    start();
    EXPECT_TRUE(send(wxEVT_LEFT_UP, 150, 60));
    EXPECT_TRUE(done.empty());
    EXPECT_TRUE(tool->drawing());
}

TEST_F(AreaToolTest, PressOnACanvasNeverDrawnIsIgnoredButConsumed) {
    tool = std::make_unique<AreaTool>([] { return wxColour(); }, [] { return false; });
    tool->rememberViewport(0, makeVp(50, 10));
    ocpn_fake::underMouse = 1; // canvas 1 has no viewport
    start();
    EXPECT_TRUE(send(wxEVT_LEFT_DOWN, 100, 100));
    EXPECT_TRUE(send(wxEVT_LEFT_UP, 150, 60));
    EXPECT_TRUE(done.empty());
    EXPECT_TRUE(tool->drawing());
}

TEST_F(AreaToolTest, TapThresholdIsFivePixels) {
    start();
    down(100, 100);
    up(104, 103); // 4,3 is exactly 5.0 px: a drag, the box is done
    ASSERT_EQ(done.size(), 1u);
    done.clear();
    start();
    down(100, 100);
    up(103, 103); // 4.24 px: a tap, only the first corner
    EXPECT_TRUE(done.empty());
    EXPECT_TRUE(tool->drawing());
}

// ---- right click cancels ----

TEST_F(AreaToolTest, RightDownCancels) {
    start();
    down(100, 100);
    EXPECT_TRUE(send(wxEVT_RIGHT_DOWN, 100, 100));
    ASSERT_EQ(done.size(), 1u);
    EXPECT_FALSE(done[0].has_value());
    EXPECT_FALSE(tool->drawing());
    EXPECT_FALSE(send(wxEVT_RIGHT_UP, 100, 100)); // passes through, nothing is swallowed
}

TEST_F(AreaToolTest, CancelAfterDragStartedEndsWithoutABox) {
    start();
    down(100, 100);
    drag(150, 60);
    send(wxEVT_RIGHT_DOWN, 150, 60);
    send(wxEVT_LEFT_UP, 150, 60); // the left release after the cancel is not a drawing any more
    ASSERT_EQ(done.size(), 1u);
    EXPECT_FALSE(done[0].has_value());
    EXPECT_EQ(svg(makeVp(50, 10)).find("poly"), std::string::npos);
}

TEST_F(AreaToolTest, CancelFunctionEndsARunningDrawingOnlyOnce) {
    tool->cancel(); // nothing running
    EXPECT_TRUE(done.empty());
    start();
    tool->cancel();
    tool->cancel();
    ASSERT_EQ(done.size(), 1u);
    EXPECT_FALSE(done[0].has_value());
}

TEST_F(AreaToolTest, StartingAgainCancelsTheRunningDrawingFirst) {
    start();
    Done second;
    tool->startDrawing([&](std::optional<cg::BBox> b) { second.push_back(b); });
    ASSERT_EQ(done.size(), 1u);
    EXPECT_FALSE(done[0].has_value());
    down(100, 100);
    drag(150, 60);
    up(150, 60);
    EXPECT_EQ(done.size(), 1u);
    ASSERT_EQ(second.size(), 1u);
    EXPECT_TRUE(second[0].has_value());
}

// ---- two taps (proof 3) ----

TEST_F(AreaToolTest, TwoTapsGiveTheBoxAndTheFirstTapShowsACross) {
    start();
    tap(100, 100);
    EXPECT_TRUE(done.empty());
    const std::string s = svg(makeVp(50, 10));
    // cross at pixel (100,100), arms of 8 px
    EXPECT_TRUE(has(s, "line 92,100 108,100\n")) << s;
    EXPECT_TRUE(has(s, "line 100,92 100,108\n")) << s;
    EXPECT_FALSE(has(s, "poly"));
    tap(150, 60);
    ASSERT_EQ(done.size(), 1u);
    expectBox(done[0], 10, 50, 15, 54);
}

TEST_F(AreaToolTest, TwoTapsWorkWithTouchOnAndADragMoveDoesNothing) {
    touch = true;
    start();
    down(100, 100);
    drag(150, 60); // touch: a drag draws nothing
    EXPECT_FALSE(has(svg(makeVp(50, 10)), "poly"));
    up(150, 60); // moved >= 5 px: neither a tap nor a rectangle
    EXPECT_TRUE(done.empty());
    EXPECT_FALSE(has(svg(makeVp(50, 10)), "line")) << "no cross after a moved touch";
    tap(100, 100);
    tap(150, 60);
    ASSERT_EQ(done.size(), 1u);
    expectBox(done[0], 10, 50, 15, 54);
}

TEST_F(AreaToolTest, ADragAfterTheFirstTapDropsTheCross) {
    start();
    tap(100, 100);
    down(120, 120);
    drag(160, 160);
    EXPECT_FALSE(has(svg(makeVp(50, 10)), "line"));
    up(160, 160);
    ASSERT_EQ(done.size(), 1u);
    expectBox(done[0], 12, 44, 16, 48);
}

// ---- AreaError while drawing (proof 6) ----

TEST_F(AreaToolTest, AnAreaErrorIsLoggedKeepsDrawingAndDoesNotCallDone) {
    start();
    tap(100, 100);
    tap(100, 100 + 0); // same point: Empty
    ASSERT_EQ(g_log.size(), 1u);
    EXPECT_EQ(g_log[0], "ChartGrabber: area error: Empty");
    EXPECT_TRUE(done.empty());
    EXPECT_TRUE(tool->drawing());
    EXPECT_FALSE(has(svg(makeVp(50, 10)), "line")) << "the cross is cleared";
    // drawing again works
    down(100, 100);
    drag(150, 60);
    up(150, 60);
    ASSERT_EQ(done.size(), 1u);
}

TEST_F(AreaToolTest, ADragAcrossTheAntimeridianIsAnErrorAndNothingElse) {
    tool->rememberViewport(0, makeVp(0, 175));
    start();
    down(100, 100); // lon 175
    drag(180, 80);
    up(180, 80); // lon 183
    ASSERT_EQ(g_log.size(), 1u);
    EXPECT_EQ(g_log[0], "ChartGrabber: area error: CrossesAntimeridian");
    EXPECT_TRUE(done.empty());
    EXPECT_TRUE(tool->drawing());
}

TEST_F(AreaToolTest, ADragNorthOfTheMercatorLimitIsClampedNotAnError) {
    tool->rememberViewport(0, makeVp(84, 0));
    start();
    down(100, 100); // lat 84
    drag(150, 0);   // lat 94
    up(150, 0);
    ASSERT_EQ(done.size(), 1u);
    expectBox(done[0], 0, 84, 5, 85.0511);
    EXPECT_TRUE(g_log.empty());
}

// ---- outline, DC (proof 4) ----

TEST_F(AreaToolTest, ShownAreaIsReprojectedInEachViewport) {
    tool->setShownArea(cg::BBox{10, 50, 15, 54});
    const std::string a = svg(makeVp(50, 10)); // corners (100,100) (150,100) (150,60) (100,60)
    EXPECT_TRUE(has(a, "poly 100,100 150,100 150,60 100,60\n")) << a;
    const std::string b = svg(makeVp(51, 11)); // panned by 1 degree: shifted -10 px in x, +10 px in y
    EXPECT_TRUE(has(b, "poly 90,110 140,110 140,70 90,70\n")) << b;
}

TEST_F(AreaToolTest, OutlineIsTwoPixelsOnATransparentBrushInTheCurrentColour) {
    tool->setShownArea(cg::BBox{10, 50, 15, 54});
    std::string s = svg(makeVp(50, 10));
    EXPECT_TRUE(has(s, "pen 128,0,0,2\n")) << s;
    EXPECT_TRUE(has(s, "brush transparent")) << s;
    colour = wxColour(1, 2, 3); // the scheme changed: the colour is asked again on every frame
    s = svg(makeVp(50, 10));
    EXPECT_TRUE(has(s, "pen 1,2,3,2\n")) << s;
}

TEST_F(AreaToolTest, NothingIsDrawnWithoutAnAreaOrCross) {
    const std::string s = svg(makeVp(50, 10));
    EXPECT_FALSE(has(s, "poly"));
    EXPECT_FALSE(has(s, "line"));
    EXPECT_TRUE(gl(makeVp(50, 10)).empty());
}

TEST_F(AreaToolTest, LiveOutlineFollowsTheDragAndIsGoneAfterwards) {
    start();
    down(100, 100);
    drag(150, 60);
    EXPECT_TRUE(has(svg(makeVp(50, 10)), "100,100 150,100 150,60 100,60"));
    drag(130, 80);
    EXPECT_TRUE(has(svg(makeVp(50, 10)), "100,100 130,100 130,80 100,80"));
    up(130, 80);
    // the finished drawing is kept as the shown area
    EXPECT_TRUE(has(svg(makeVp(50, 10)), "100,100 130,100 130,80 100,80"));
}

TEST_F(AreaToolTest, SmallMoveBelowThresholdShowsNoLiveOutline) {
    start();
    down(100, 100);
    drag(102, 102);
    EXPECT_FALSE(has(svg(makeVp(50, 10)), "poly"));
}

TEST_F(AreaToolTest, ShownAreaIsHiddenWhileDrawingAndComesBackOnCancelOrIsReplacedOnSuccess) {
    tool->setShownArea(cg::BBox{10, 50, 15, 54});
    start();
    EXPECT_FALSE(has(svg(makeVp(50, 10)), "poly"));
    tool->cancel();
    EXPECT_TRUE(has(svg(makeVp(50, 10)), "100,100 150,100 150,60 100,60"));
    start();
    down(100, 100);
    drag(130, 80);
    up(130, 80);
    const std::string s = svg(makeVp(50, 10));
    EXPECT_TRUE(has(s, "100,100 130,100 130,80 100,80"));
    EXPECT_FALSE(has(s, "150,100"));
}

TEST_F(AreaToolTest, ClearingTheShownAreaRemovesTheOutline) {
    tool->setShownArea(cg::BBox{10, 50, 15, 54});
    tool->setShownArea(std::nullopt);
    EXPECT_FALSE(has(svg(makeVp(50, 10)), "poly"));
}

// ---- outline, GL (proof 4) ----

TEST_F(AreaToolTest, GlDrawsTheBoxAsALineLoopInTheCurrentColour) {
    tool->setShownArea(cg::BBox{10, 50, 15, 54});
    const auto calls = gl(makeVp(50, 10));
    const std::vector<std::string> loop{"loop", "v 100,100", "v 150,100", "v 150,60", "v 100,60", "end"};
    auto it = std::search(calls.begin(), calls.end(), loop.begin(), loop.end());
    EXPECT_NE(it, calls.end());
    EXPECT_NE(std::find(calls.begin(), calls.end(), "color 128,0,0,255"), calls.end());
    EXPECT_NE(std::find(calls.begin(), calls.end(), "width 2"), calls.end());
    EXPECT_EQ(calls.front(), "push");
    EXPECT_EQ(calls.back(), "pop");
    EXPECT_EQ(std::count(calls.begin(), calls.end(), "push"), 1);
    EXPECT_EQ(std::count(calls.begin(), calls.end(), "pop"), 1);
    EXPECT_NE(std::find(calls.begin(), calls.end(), "disable " + std::to_string(GL_TEXTURE_2D)), calls.end());
    colour = wxColour(9, 8, 7);
    const auto again = gl(makeVp(50, 10));
    EXPECT_NE(std::find(again.begin(), again.end(), "color 9,8,7,255"), again.end());
}

TEST_F(AreaToolTest, GlDrawsTheFirstCornerCrossAsTwoLineSegments) {
    start();
    tap(100, 100);
    const auto calls = gl(makeVp(50, 10));
    const std::vector<std::string> cross{"lines", "v 92,100", "v 108,100", "v 100,92", "v 100,108", "end"};
    EXPECT_NE(std::search(calls.begin(), calls.end(), cross.begin(), cross.end()), calls.end());
    EXPECT_EQ(std::find(calls.begin(), calls.end(), "loop"), calls.end());
}

// ---- refresh requests ----

TEST_F(AreaToolTest, ChangesRefreshEveryCanvasButADragStepOnlyThePressedOne) {
    tool->setShownArea(cg::BBox{10, 50, 15, 54});
    EXPECT_EQ(ocpn_fake::refreshed, (std::vector<wxWindow*>{kCanvas0, kCanvas1}));
    ocpn_fake::underMouse = 1;
    start();
    down(100, 100);
    ocpn_fake::refreshed.clear();
    drag(150, 60);
    EXPECT_EQ(ocpn_fake::refreshed, (std::vector<wxWindow*>{kCanvas1}));
}

// ---- checkedBox's error, onAreaError, abandon ----

TEST_F(AreaToolTest, CheckedBoxReportsTheErrorThroughTheOutParameter) {
    cg::AreaError e = cg::AreaError::TooFewRoutePoints; // a value checkedBox never gives
    EXPECT_FALSE(checkedBox({10, 170}, {20, 190}, &e).has_value());
    EXPECT_EQ(e, cg::AreaError::CrossesAntimeridian);
    EXPECT_FALSE(checkedBox({86, 0}, {89, 10}, &e).has_value());
    EXPECT_EQ(e, cg::AreaError::OutsideMercator);
    EXPECT_FALSE(checkedBox({10, 5}, {20, 5}, &e).has_value());
    EXPECT_EQ(e, cg::AreaError::Empty);
    e = cg::AreaError::TooFewRoutePoints; // a good box leaves it alone
    EXPECT_TRUE(checkedBox({50, 10}, {54, 15}, &e).has_value());
    EXPECT_EQ(e, cg::AreaError::TooFewRoutePoints);
}

TEST_F(AreaToolTest, OnAreaErrorGetsTheErrorOnceAfterTheCornerWasClearedAndTheToolKeepsDrawing) {
    std::vector<cg::AreaError> errors;
    bool drawingInside = false;
    std::size_t refreshedInside = 0;
    tool->onAreaError = [&](cg::AreaError e) {
        errors.push_back(e);
        drawingInside = tool->drawing();
        refreshedInside = ocpn_fake::refreshed.size();
    };
    start();
    tap(100, 100);
    ocpn_fake::refreshed.clear();
    tap(100, 100); // the same point: Empty
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0], cg::AreaError::Empty);
    EXPECT_TRUE(drawingInside);
    EXPECT_EQ(refreshedInside, 2u) << "the canvases are refreshed before the callback";
    EXPECT_TRUE(done.empty());
    EXPECT_FALSE(has(svg(makeVp(50, 10)), "line")) << "the first corner is cleared";
    // A drag across the antimeridian (viewport lon 10, 10 px per degree: x = 1900 is lon 190).
    down(100, 100);
    drag(1900, 60);
    up(1900, 60);
    ASSERT_EQ(errors.size(), 2u);
    EXPECT_EQ(errors[1], cg::AreaError::CrossesAntimeridian);
    // The next two taps are a fresh pair (the cleared corner is not reused): the box of THEIR corners.
    tap(100, 100);
    EXPECT_EQ(errors.size(), 2u);
    tap(150, 60);
    ASSERT_EQ(done.size(), 1u);
    expectBox(done[0], 10, 50, 15, 54);
    EXPECT_EQ(errors.size(), 2u);
}

TEST_F(AreaToolTest, OnAreaErrorIsNotCalledForAGoodBoxOrWhenNotDrawing) {
    int calls = 0;
    tool->onAreaError = [&](cg::AreaError) { ++calls; };
    start();
    down(100, 100);
    drag(150, 60);
    up(150, 60);
    EXPECT_EQ(calls, 0);
    ASSERT_EQ(done.size(), 1u);
    tap(100, 100); // not drawing any more: passes through
    tap(100, 100);
    EXPECT_EQ(calls, 0);
}

TEST_F(AreaToolTest, AnAreaErrorWithoutACallbackStillKeepsDrawing) {
    start();
    tap(100, 100);
    tap(100, 100);
    EXPECT_TRUE(tool->drawing());
    EXPECT_TRUE(done.empty());
}

TEST_F(AreaToolTest, AbandonEndsTheDrawingWithoutCallingDoneAndKeepsTheShownArea) {
    tool->setShownArea(cg::BBox{10, 50, 15, 54});
    start();
    tap(120, 120); // a first corner
    ocpn_fake::refreshed.clear();
    tool->abandon();
    EXPECT_FALSE(tool->drawing());
    EXPECT_TRUE(done.empty());
    EXPECT_EQ(ocpn_fake::refreshed, (std::vector<wxWindow*>{kCanvas0, kCanvas1}));
    const std::string s = svg(makeVp(50, 10));
    EXPECT_TRUE(has(s, "100,100 150,100 150,60 100,60")) << "the shown area is unchanged";
    EXPECT_FALSE(has(s, "line")) << "the first corner is gone";
    EXPECT_FALSE(send(wxEVT_LEFT_DOWN, 100, 100)) << "mouse events pass through again";
    tool->abandon(); // not drawing: nothing
    EXPECT_TRUE(done.empty());
}

TEST_F(AreaToolTest, AbandonedDoneIsNotCalledByALaterDrawingAndCancelStillCallsIt) {
    start();
    tool->abandon();
    std::vector<std::optional<cg::BBox>> second;
    tool->startDrawing([&](std::optional<cg::BBox> b) { second.push_back(b); });
    down(100, 100);
    drag(150, 60);
    up(150, 60);
    EXPECT_TRUE(done.empty());
    ASSERT_EQ(second.size(), 1u);
    start();
    tool->cancel();
    ASSERT_EQ(done.size(), 1u);
    EXPECT_FALSE(done[0].has_value());
}

} // namespace
