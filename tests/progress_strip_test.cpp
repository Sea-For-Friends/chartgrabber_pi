// SPDX-License-Identifier: GPL-3.0-or-later
// The strip needs a display; without one the tests skip (and say so).
#include "config_store.h"
#include "log_capture.h"
#include "look.h"
#include "ocpn_fakes.h"
#include "plugin_log.h"
#include "progress_strip.h"
#include "wx_env.h"

#include <gtest/gtest.h>

#include <wx/app.h>
#include <wx/button.h>
#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <wx/dcscreen.h>
#include <wx/display.h>
#include <wx/fileconf.h>
#include <wx/frame.h>
#include <wx/gauge.h>
#include <wx/image.h>
#include <wx/settings.h>
#include <wx/stattext.h>
#include <wx/stopwatch.h>
#include <wx/textctrl.h>
#include <wx/tokenzr.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <gdk/gdk.h>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

#include <ocpn_plugin.h>

namespace {

// Yields to the event loop until `done` or two seconds pass.
bool pump(const std::function<bool()>& done) {
    wxStopWatch watch;
    while (!done() && watch.Time() < 2000) {
        wxTheApp->Yield(true);
        wxMilliSleep(20);
    }
    return done();
}

// A result button whose function replaces the result on the same strip must not free the button under its
// own handler (wxWindow::Destroy on a child is immediate). Run under --asan to see the use-after-free.
TEST(ProgressStrip, ResultButtonMayShowResultOnSameStrip) {
    if (!wxTheApp || !wxTheApp->IsGUI())
        GTEST_SKIP() << "no display: the strip test was NOT run";
    Look look;
    auto* parent = new wxFrame(nullptr, wxID_ANY, "parent");
    auto* strip = new ProgressStrip(parent, look);
    int calls = 0;
    // The captured string lives in the copy of the function held by the button: reading it after showResult
    // is a use-after-free when the button was destroyed under its own handler.
    strip->showResult("done", "", {{"Again", [&calls, strip, held = std::string(64, 'x')] {
                                        strip->showResult("second", "", {});
                                        calls += static_cast<int>(held.size());
                                    }}});
    auto* button = wxDynamicCast(wxWindow::FindWindowByLabel("Again", strip), wxButton);
    ASSERT_NE(button, nullptr);
    wxCommandEvent click(wxEVT_BUTTON, button->GetId());
    click.SetEventObject(button);
    button->GetEventHandler()->ProcessEvent(click);
    wxTheApp->ProcessPendingEvents();
    EXPECT_EQ(calls, 64);
    delete strip;
    parent->Destroy();
}

// a handler that throws is contained (Bind lambda: the Stop button; CallAfter lambda: a
// result button's function); the line is logged with the where text and the strip still works.
TEST(ProgressStrip, ThrowingButtonHandlersAreLoggedAndTheStripStaysUsable) {
    if (!wxTheApp || !wxTheApp->IsGUI())
        GTEST_SKIP() << "no display: the strip test was NOT run";
    Look look;
    LogCapture log;
    auto* parent = new wxFrame(nullptr, wxID_ANY, "parent");
    auto* strip = new ProgressStrip(parent, look);
    int stops = 0;
    strip->onStop = [&stops] {
        ++stops;
        throw std::runtime_error("stop failed");
    };
    strip->showProgress("working", 500, "Halt");
    auto click = [](wxWindow* b) {
        wxCommandEvent e(wxEVT_BUTTON, b->GetId());
        e.SetEventObject(b);
        b->GetEventHandler()->ProcessEvent(e);
    };
    wxWindow* halt = wxWindow::FindWindowByLabel("Halt", strip);
    ASSERT_NE(halt, nullptr);
    EXPECT_NO_THROW(click(halt));
    EXPECT_EQ(stops, 1);
    EXPECT_EQ(log.lines, std::vector<std::string>{"ChartGrabber: strip stop: stop failed"});
    EXPECT_NO_THROW(click(halt)); // still wired
    EXPECT_EQ(stops, 2);

    log.lines.clear();
    strip->showResult("done", "", {{"Again", [] { throw 7; }}});
    wxWindow* again = wxWindow::FindWindowByLabel("Again", strip);
    ASSERT_NE(again, nullptr);
    EXPECT_NO_THROW(click(again));
    EXPECT_NO_THROW(wxTheApp->ProcessPendingEvents());
    EXPECT_EQ(log.lines, std::vector<std::string>{"ChartGrabber: strip result action: unknown exception"});
    strip->showProgress("again", 10, "Halt");
    EXPECT_NE(wxWindow::FindWindowByLabel("Halt", strip), nullptr);
    delete strip;
    parent->Destroy();
}

// (width = best width, at most r.width - 16) with the text rule: the headline wraps to
// the strip, at most three lines, the third ellipsized; a widened canvas re-flows it (500 ms placement
// timer).
class StripWrapTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!wxTheApp || !wxTheApp->IsGUI())
            GTEST_SKIP() << "no display: the strip test was NOT run";
        canvas_ = new wxFrame(nullptr, wxID_ANY, "canvas", wxPoint(20, 20), wxSize(1000, 400));
        canvas_->Show();
        ocpn_fake::canvasWindow = canvas_;
        parent_ = new wxFrame(nullptr, wxID_ANY, "parent");
        strip_ = new ProgressStrip(canvas_, look_);
    }
    void TearDown() override {
        ocpn_fake::canvasWindow = nullptr;
        delete strip_;
        if (parent_)
            parent_->Destroy();
        if (canvas_)
            canvas_->Destroy();
    }
    void setCanvasWidth(int width) {
        canvas_->SetClientSize(width, 400);
        pump([&] { return canvas_->GetClientSize().x == width; });
    }
    int room() const { return canvas_->GetClientSize().x - 16; }
    wxStaticText* label() const {
        std::function<wxStaticText*(wxWindow*)> find = [&](wxWindow* w) -> wxStaticText* {
            for (wxWindow* c : w->GetChildren()) {
                if (auto* t = dynamic_cast<wxStaticText*>(c))
                    return t;
                if (auto* t = find(c))
                    return t;
            }
            return nullptr;
        };
        return find(strip_);
    }
    std::vector<wxString> lines() const {
        std::vector<wxString> out;
        wxStringTokenizer tk(label()->GetLabelText(), "\n", wxTOKEN_RET_EMPTY_ALL);
        while (tk.HasMoreTokens())
            out.push_back(tk.GetNextToken());
        return out;
    }
    int extent(const wxString& line) const {
        wxClientDC dc(label());
        dc.SetFont(label()->GetFont());
        return dc.GetTextExtent(line).x;
    }
    // Words of a text separated by blanks and newlines.
    static std::vector<wxString> words(const wxString& text) {
        std::vector<wxString> out;
        wxStringTokenizer tk(text, " \n");
        while (tk.HasMoreTokens())
            out.push_back(tk.GetNextToken());
        return out;
    }

    Look look_;
    wxFrame* canvas_ = nullptr;
    wxFrame* parent_ = nullptr;
    ProgressStrip* strip_ = nullptr;
};

const char* const kTwoLines = "Grabber: writing Mediterranean overview chart to the chart directory";

TEST_F(StripWrapTest, NarrowCanvasWrapsToTheStripAndLosesNothing) {
    setCanvasWidth(400); // the grip takes 22 px of the row
    strip_->showResult(kTwoLines, "", {});
    const auto got = lines();
    EXPECT_GE(got.size(), 2u); // it did not fit one line: the text was wrapped, not cut off
    EXPECT_LE(got.size(), 3u);
    EXPECT_LE(strip_->GetSize().x, room());
    for (const wxString& line : got)
        EXPECT_LE(extent(line), strip_->GetSize().x) << line.utf8_string();
    EXPECT_EQ(words(label()->GetLabelText()), words(kTwoLines)); // no word lost before the third line
}

TEST_F(StripWrapTest, WideningTheCanvasReflowsTheTextToOneLine) {
    setCanvasWidth(300);
    strip_->showResult(kTwoLines, "", {});
    ASSERT_GE(lines().size(), 2u);
    setCanvasWidth(1000);
    // no show* call: the 500 ms placement timer re-wraps
    EXPECT_TRUE(pump([&] { return lines().size() == 1; }));
    EXPECT_EQ(label()->GetLabelText(), wxString(kTwoLines));
}

TEST_F(StripWrapTest, NarrowingTheCanvasWrapsAShownStripAgain) {
    setCanvasWidth(1000);
    strip_->showResult(kTwoLines, "", {});
    ASSERT_EQ(lines().size(), 1u);
    setCanvasWidth(300);
    EXPECT_TRUE(pump([&] { return lines().size() >= 2; }));
    EXPECT_LE(strip_->GetSize().x, room());
}

TEST_F(StripWrapTest, TextBeyondThreeLinesEndsInAnEllipsisOnTheThirdLine) {
    setCanvasWidth(300);
    std::string text;
    for (int i = 0; i < 12; ++i)
        text += "Grabber: writing chart overview " + std::to_string(i) + " ";
    const wxString headline(text);
    strip_->showResult(headline, "", {});
    const auto got = lines();
    ASSERT_EQ(got.size(), 3u);
    EXPECT_TRUE(got[2].EndsWith("...")) << got[2].utf8_string();
    EXPECT_LE(strip_->GetSize().x, room());
    for (const wxString& line : got)
        EXPECT_LE(extent(line), strip_->GetSize().x) << line.utf8_string();
    // the lines before the third are the beginning of the headline, word for word
    const auto all = words(headline);
    const auto first = words(got[0] + " " + got[1]);
    ASSERT_LE(first.size(), all.size());
    EXPECT_TRUE(std::equal(first.begin(), first.end(), all.begin()));
}

TEST_F(StripWrapTest, TextThatFitsIsShownUnchanged) {
    setCanvasWidth(1000);
    strip_->showResult("Stopped.", "", {});
    EXPECT_EQ(label()->GetLabelText(), wxString("Stopped."));
}

TEST_F(StripWrapTest, ExplicitNewlinesOfAHeadlineStay) {
    setCanvasWidth(1000);
    strip_->showResult("first line\nsecond line", "", {});
    EXPECT_EQ(label()->GetLabelText(), wxString("first line\nsecond line"));
}

TEST_F(StripWrapTest, RunningRowWithGaugeAndButtonsStaysInsideTheCanvas) {
    setCanvasWidth(480);
    // the running line of
    strip_->showProgress("Grabber: Google - Satellite z13 412/1 300 (31 %) 9.4 MB ~4 min", 314, "Stop");
    EXPECT_LE(strip_->GetSize().x, room());
    EXPECT_LE(lines().size(), 3u);
    EXPECT_GE(lines().size(), 2u); // the gauge and the buttons leave the text less than its one-line width
}

// ---- the opaque themed body and the headline floor ----

TEST_F(StripWrapTest, TheBodyColoursFollowTheSchemeOnBothRolesAndComeBackByDay) {
    const wxColour uibck(11, 222, 33), uitx1(201, 12, 133);
    ocpn_fake::globalColors["UIBCK"] = uibck;
    ocpn_fake::globalColors["UITX1"] = uitx1;
    const wxColour face = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
    const wxColour text = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
    ASSERT_NE(face, uibck);
    ASSERT_NE(text, uitx1);
    for (const auto role : {ProgressStrip::Role::Job, ProgressStrip::Role::Hint}) {
        delete strip_;
        look_.setScheme(PI_GLOBAL_COLOR_SCHEME_DAY);
        strip_ = new ProgressStrip(canvas_, look_, role);
        strip_->showResult("Done", "", {});
        std::vector<wxWindow*> body = {strip_, label()};
        for (wxWindow* c : strip_->GetChildren())
            if (wxString(c->GetClassInfo()->GetClassName()) == "wxWindow")
                body.push_back(c); // the grip
        ASSERT_EQ(role == ProgressStrip::Role::Job ? 3u : 2u, body.size());
        const auto expect = [&](const wxColour& bg, const wxColour& fg, const char* when) {
            for (wxWindow* w : body) {
                EXPECT_EQ(bg, w->GetBackgroundColour()) << when;
                EXPECT_EQ(fg, w->GetForegroundColour()) << when;
            }
        };
        expect(face, text, "after construction by day");
        look_.setScheme(PI_GLOBAL_COLOR_SCHEME_NIGHT);
        expect(uibck, uitx1, "night");
        look_.setScheme(PI_GLOBAL_COLOR_SCHEME_DAY);
        expect(face, text, "back to day");
    }
    ocpn_fake::globalColors.clear();
}

// the buttons (Stop, Details, the result buttons) and the gauge follow Dusk / Night by explicit
// colours. The fake DimeWindow paints everything (1,2,3) / (4,5,6): the controls must come out DILG1 / DILG3
// anyway.
TEST_F(StripWrapTest, TheControlsCarryDilgColoursAtDuskAndNightAndNothingByDay) {
    const wxColour dilg1(110, 111, 112), dilg3(130, 131, 132);
    ocpn_fake::globalColors["DILG1"] = dilg1;
    ocpn_fake::globalColors["DILG3"] = dilg3;
    ocpn_fake::globalColors["UIBCK"] = wxColour(7, 7, 7);
    ocpn_fake::globalColors["UITX1"] = wxColour(201, 12, 133);
    const auto controls = [&] {
        std::vector<wxButton*> b;
        wxGauge* g = nullptr;
        for (wxWindow* c : strip_->GetChildren()) {
            if (auto* x = dynamic_cast<wxButton*>(c))
                b.push_back(x);
            if (auto* x = dynamic_cast<wxGauge*>(c))
                g = x;
        }
        return std::make_pair(b, g);
    };
    const auto expect = [&](size_t buttons, const wxColour& bg, const wxColour& fg, const char* when) {
        const auto [b, g] = controls();
        ASSERT_EQ(buttons, b.size()) << when;
        ASSERT_NE(nullptr, g) << when;
        for (wxButton* x : b) {
            EXPECT_EQ(bg, x->GetBackgroundColour()) << when;
            EXPECT_EQ(fg, x->GetForegroundColour()) << when;
        }
        EXPECT_EQ(bg, g->GetBackgroundColour()) << when;
    };
    const wxColour faceBg = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
    const wxColour faceFg = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
    strip_->showProgress("Grabber: running", 300, "Stop");
    for (PI_ColorScheme dark : {PI_GLOBAL_COLOR_SCHEME_DUSK, PI_GLOBAL_COLOR_SCHEME_NIGHT}) {
        look_.setScheme(dark);
        expect(2, dilg1, dilg3, "running, dark");
        strip_->showResult("Done", "", {{"Open", nullptr}, {"Close", nullptr}});
        expect(4, dilg1, dilg3, "result buttons, dark"); // Stop and Details are hidden, still styled
        look_.setScheme(PI_GLOBAL_COLOR_SCHEME_DAY);
        expect(4, faceBg, faceFg, "back to day");
        strip_->showProgress("Grabber: running", 300, "Stop");
        look_.setScheme(PI_GLOBAL_COLOR_SCHEME_RGB);
        expect(2, faceBg, faceFg, "rgb");
    }
    ocpn_fake::globalColors.clear();
}

// The body colour reaches the screen: the strip's own pixel (inside the column border, no child covers it)
// is the body colour while the canvas under it paints pure magenta. This does NOT prove the paint handler
// runs: under Xvfb GTK fills the panel itself once SetBackgroundColour is set, so the test is green with
// the handler removed (measured). The handler rests on the VM record (F1F0EE, OpenGL on and off).
TEST_F(StripWrapTest, TheStripPaintsItsBodyOverTheCanvas) {
    canvas_->SetBackgroundStyle(wxBG_STYLE_PAINT);
    canvas_->Bind(wxEVT_PAINT, [](wxPaintEvent& e) { // no fixture pointer: the frame outlives the test
        wxPaintDC dc(static_cast<wxWindow*>(e.GetEventObject()));
        dc.SetBackground(wxBrush(wxColour(255, 0, 255)));
        dc.Clear();
    });
    canvas_->Refresh();
    strip_->showResult("Done", "", {});
    const wxColour want = look_.bodyBackground();
    ASSERT_NE(want, wxColour(255, 0, 255));
    const auto pixel = [&](wxPoint screen) { // the X root window (Xvfb) read back through GDK
        wxColour c;
        GdkPixbuf* pb = gdk_pixbuf_get_from_window(gdk_get_default_root_window(), screen.x, screen.y, 1, 1);
        if (pb) {
            const guchar* p = gdk_pixbuf_get_pixels(pb);
            c = wxColour(p[0], p[1], p[2]);
            g_object_unref(pb);
        }
        return c;
    };
    const wxPoint at = strip_->ClientToScreen(wxPoint(2, 2));
    const wxPoint outside = canvas_->ClientToScreen(wxPoint(500, 5));
    EXPECT_TRUE(pump([&] { return pixel(outside) == wxColour(255, 0, 255); }))
        << "the canvas paint is on the screen";
    EXPECT_TRUE(pump([&] { return pixel(at) == want; }))
        << pixel(at).GetAsString().utf8_string() << " against " << want.GetAsString().utf8_string();
}

const char* const kSeamarks = "Grabber: Seamarks z9 212/1 300 (16 %) 3.1 MB ~2 min";

// The line (336 px) is wrapped to the 260 DIP the floor leaves it: two lines, on the split and on the full
// canvas.
TEST_F(StripWrapTest, ASplitCanvasKeepsTheRunningStripAsWideAsTheFullOne) {
    setCanvasWidth(1000);
    strip_->showProgress(kSeamarks, 160, "Stop");
    const int full = strip_->GetSize().x;
    setCanvasWidth(701);
    strip_->showProgress(kSeamarks, 160, "Stop");
    EXPECT_EQ(full, strip_->GetSize().x);
    EXPECT_LE(strip_->GetSize().x, 701 - 16);
    ASSERT_EQ(2u, lines().size()) << "not the fragments of a 60 px headline";
    EXPECT_GE(extent(lines()[0]), 200);
}

// A headline wider than the floor: the 60 % cap (420 px) would leave the label about 34 px (the 3 fragments).
TEST_F(StripWrapTest, ASplitCanvasLeavesALongRunningHeadlineItsFloor) {
    setCanvasWidth(701);
    strip_->showProgress(std::string(kSeamarks) + " (writing the chart overview, then the index)", 160,
                         "Stop");
    EXPECT_GT(strip_->GetSize().x, 701 * 60 / 100);
    EXPECT_LE(strip_->GetSize().x, 701 - 16);
    EXPECT_GE(extent(lines()[0]), 200);
}

TEST_F(StripWrapTest, TheFloorGrowsWithTheResultButtonsOfAFinishedJob) {
    setCanvasWidth(701);
    const std::string headline = std::string(kSeamarks) + " finished with a long tail of words " + kSeamarks;
    strip_->showResult(headline, "", {});
    const int plain = strip_->GetSize().x;
    strip_->showResult(headline, "", {{"Open folder", [] {}}, {"Show chart", [] {}}});
    int buttons = 0;
    for (wxWindow* c : strip_->GetChildren())
        if (auto* b = dynamic_cast<wxButton*>(c); b && b->IsShown())
            buttons += b->GetSize().x;
    ASSERT_GT(buttons, 120);
    EXPECT_GT(strip_->GetSize().x, plain);
    EXPECT_GE(strip_->GetSize().x, buttons + strip_->FromDIP(260));
    EXPECT_GE(extent(lines()[0]), 200);
    EXPECT_LE(strip_->GetSize().x, 701 - 16);
}

// ---- the strip is a child of the canvas, placed above the chart bar, draggable, remembered ----

const char* const kConfigGroup = "/PlugIns/ChartGrabber/";

class StripPlaceTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!wxTheApp || !wxTheApp->IsGUI())
            GTEST_SKIP() << "no display: the strip test was NOT run";
        char tmpl[] = "/tmp/cgstrip-XXXXXX";
        ASSERT_NE(mkdtemp(tmpl), nullptr);
        dir_ = tmpl;
        openStore();
        ocpn_fake::chartbarHeight = 40;
        ocpn_fake::touch = false;
        canvas_ = new wxFrame(nullptr, wxID_ANY, "canvas", wxPoint(20, 20), wxSize(800, 600));
        canvas_->Show();
        setClient(800, 600);
        ocpn_fake::canvasWindow = canvas_;
    }
    void TearDown() override {
        ocpn_fake::canvasWindow = nullptr;
        ocpn_fake::chartbarHeight = 0;
        delete strip_;
        if (canvas_)
            canvas_->Destroy();
        wxTheApp->ProcessPendingEvents();
        store_.reset();
        config_.reset();
        if (!dir_.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }
    }
    void openStore() { // a new store on a new wxFileConfig of the same file: as after an OpenCPN restart
        store_.reset();
        if (config_)
            config_->Flush(); // the new config reads the file
        config_ =
            std::make_unique<wxFileConfig>("cgstriptest", wxEmptyString, wxString(dir_ + "/opencpn.conf"),
                                           wxEmptyString, wxCONFIG_USE_LOCAL_FILE);
        store_ = std::make_unique<ConfigStore>(config_.get());
    }
    void setClient(int w, int h) {
        canvas_->SetClientSize(w, h);
        ASSERT_TRUE(pump([&] { return canvas_->GetClientSize() == wxSize(w, h); }));
    }
    void makeStrip(ProgressStrip::Role role = ProgressStrip::Role::Job) {
        delete strip_;
        strip_ = new ProgressStrip(canvas_, look_, role,
                                   role == ProgressStrip::Role::Job ? store_.get() : nullptr);
    }
    void run() { strip_->showProgress("Grabber: working", 300, "Stop"); }
    wxRect rect() const { return strip_->GetRect(); }
    wxStaticText* text() const {
        for (wxWindow* c : strip_->GetChildren())
            if (auto* t = dynamic_cast<wxStaticText*>(c))
                return t;
        return nullptr;
    }
    wxTextCtrl* details() const {
        for (wxWindow* c : strip_->GetChildren())
            if (auto* t = dynamic_cast<wxTextCtrl*>(c))
                return t;
        return nullptr;
    }
    wxPoint centre(wxWindow* w) const {
        return canvas_->ScreenToClient(w->GetScreenRect().GetTopLeft()) +
               wxPoint(w->GetSize().x / 2, w->GetSize().y / 2);
    }
    // A synthetic mouse event for `win` at a point given in canvas client pixels.
    void mouse(wxWindow* win, wxEventType type, wxPoint canvasPoint, bool leftDown) {
        wxMouseEvent e(type);
        e.SetEventObject(win);
        e.SetPosition(win->ScreenToClient(canvas_->ClientToScreen(canvasPoint)));
        e.SetLeftDown(leftDown);
        win->ProcessWindowEvent(e);
    }
    // press, motion by `step` (a drag), release; returns nothing, the caller reads the strip and the keys
    void dragBy(wxWindow* via, wxPoint from, wxPoint step) {
        mouse(via, wxEVT_LEFT_DOWN, from, true);
        mouse(via, wxEVT_MOTION, from + step, true);
        mouse(via, wxEVT_LEFT_UP, from + step, false);
    }
    wxString key(const char* name) const {
        wxString v = "<absent>";
        config_->Read(wxString(kConfigGroup) + name, &v);
        return v;
    }

    Look look_;
    std::string dir_;
    std::unique_ptr<wxFileConfig> config_;
    std::unique_ptr<ConfigStore> store_;
    wxFrame* canvas_ = nullptr;
    ProgressStrip* strip_ = nullptr;
};

TEST_F(StripPlaceTest, TheStripIsAHiddenChildOfTheCanvasShownByTheShowCalls) {
    makeStrip();
    EXPECT_EQ(strip_->GetParent(), canvas_);
    EXPECT_FALSE(strip_->IsTopLevel());
    EXPECT_FALSE(strip_->IsShown());
    run();
    EXPECT_TRUE(strip_->IsShown());
    strip_->Hide();
    strip_->showResult("done", "", {});
    EXPECT_TRUE(strip_->IsShown());
}

TEST_F(StripPlaceTest, TheDefaultPlaceIs8FromTheLeftAnd32AboveTheChartBarOrTheCanvasBottom) {
    makeStrip();
    run();
    EXPECT_EQ(rect().x, 8);
    EXPECT_EQ(rect().GetBottom() + 1, 600 - 40 - 32);
    ocpn_fake::chartbarHeight = 0; // chart bar hidden: re-placed by the 500 ms timer
    EXPECT_TRUE(pump([&] { return rect().GetBottom() + 1 == 600 - 32; }));
}

TEST_F(StripPlaceTest, ASmallCanvasPushesTheStripInsideAndTheTimerBringsItBack) {
    makeStrip();
    run();
    setClient(800, 110); // safe height 70: the bottom anchor (38) leaves no room above it
    ASSERT_TRUE(pump([&] { return rect().y == 8; }));
    EXPECT_LE(rect().GetBottom() + 1, 110 - 40) << "never over the chart bar rows";
    setClient(800, 600);
    EXPECT_TRUE(pump([&] { return rect().GetBottom() + 1 == 600 - 40 - 32; }));
}

TEST_F(StripPlaceTest, ADragNeedsMoreThanSixPixelsThenFollowsThePointerAndTheDropStoresTheCorner) {
    makeStrip();
    run();
    const wxRect r0 = rect();
    const wxPoint from = r0.GetTopLeft() + wxPoint(4, 4);
    mouse(strip_, wxEVT_LEFT_DOWN, from, true);
    mouse(strip_, wxEVT_MOTION, from + wxPoint(5, 0), true);
    EXPECT_EQ(rect(), r0) << "5 px is a shaky tap";
    mouse(strip_, wxEVT_MOTION, from + wxPoint(30, -20), true);
    EXPECT_EQ(rect().GetTopLeft(), r0.GetTopLeft() + wxPoint(30, -20));
    EXPECT_TRUE(strip_->HasCapture());
    EXPECT_EQ(key("StripCorner"), "<absent>") << "nothing stored before the drop";
    mouse(strip_, wxEVT_LEFT_UP, from + wxPoint(30, -20), false);
    EXPECT_FALSE(strip_->HasCapture());
    EXPECT_EQ(key("StripCorner"), "bl");
    EXPECT_EQ(key("StripDx"), "38"); // 8 + 30
    EXPECT_EQ(key("StripDy"), "52"); // 32 + 20
    EXPECT_EQ(key("StripHintShown"), "1");
    // the same drag on the text child (it gets its own mouse events)
    wxWindow* t = text();
    ASSERT_NE(t, nullptr);
    dragBy(t, centre(t), wxPoint(30, 0));
    EXPECT_EQ(key("StripDx"), "68");
    EXPECT_EQ(key("StripDy"), "52");
}

TEST_F(StripPlaceTest, ADropAtTheTopRightIsRestoredByANewStripFromTheStoredKeys) {
    makeStrip();
    run();
    const wxPoint from = rect().GetTopLeft() + wxPoint(4, 4);
    dragBy(strip_, from, wxPoint(5000, -5000)); // clamped to the top-right corner of the safe area
    EXPECT_EQ(key("StripCorner"), "tr");
    EXPECT_EQ(key("StripDx"), "8");
    EXPECT_EQ(key("StripDy"), "8");
    const wxRect dropped = rect();
    EXPECT_EQ(dropped.y, 8);
    EXPECT_EQ(dropped.GetRight() + 1, 800 - 8);
    openStore();
    makeStrip();
    run();
    EXPECT_EQ(rect(), dropped);
}

TEST_F(StripPlaceTest, ADragFarBelowStopsAtTheSafeBottomMinusTheMarginHeldAndDropped) {
    makeStrip();
    run();
    const wxPoint from = rect().GetTopLeft() + wxPoint(4, 4);
    mouse(strip_, wxEVT_LEFT_DOWN, from, true);
    mouse(strip_, wxEVT_MOTION, from + wxPoint(0, 5000), true);
    ASSERT_TRUE(strip_->HasCapture());
    // canvas 600 high, chart bar 40, margin 8: the bottom edge (exclusive) is at 552
    EXPECT_EQ(rect().GetBottom() + 1, 552) << "while the pointer is held";
    mouse(strip_, wxEVT_LEFT_UP, from + wxPoint(0, 5000), false);
    EXPECT_EQ(rect().GetBottom() + 1, 552) << "after the drop";
}

TEST_F(StripPlaceTest, ANewLineDuringADragDoesNotMoveTheStripAndTheDropUsesTheNewAnchor) {
    makeStrip();
    run();
    const wxRect r0 = rect();
    const wxPoint from = r0.GetTopLeft() + wxPoint(4, 4);
    mouse(strip_, wxEVT_LEFT_DOWN, from, true);
    mouse(strip_, wxEVT_MOTION, from + wxPoint(30, -20), true);
    ASSERT_TRUE(strip_->HasCapture());
    strip_->showProgress("Grabber: a different line of the progress", 120,
                         "Stop"); // as a CLI record mid-drag
    EXPECT_EQ(rect().GetTopLeft(), r0.GetTopLeft() + wxPoint(30, -20)) << "the pointer places it";
    mouse(strip_, wxEVT_LEFT_UP, from + wxPoint(30, -20), false);
    EXPECT_EQ(rect().x, 38);
    EXPECT_EQ(rect().GetBottom() + 1, 600 - 40 - 52);
}

TEST_F(StripPlaceTest, APressOnAButtonNeverMovesTheStrip) {
    makeStrip();
    run();
    const wxRect r0 = rect();
    auto* stop = wxDynamicCast(wxWindow::FindWindowByLabel("Stop", strip_), wxButton);
    ASSERT_NE(stop, nullptr);
    const wxPoint from = centre(stop);
    dragBy(stop, from, wxPoint(60, -40));
    EXPECT_EQ(rect(), r0);
    EXPECT_EQ(key("StripCorner"), "<absent>");
}

TEST_F(StripPlaceTest, ACaptureLostDuringADragEndsItAsADrop) {
    makeStrip();
    run();
    const wxPoint from = rect().GetTopLeft() + wxPoint(4, 4);
    mouse(strip_, wxEVT_LEFT_DOWN, from, true);
    mouse(strip_, wxEVT_MOTION, from + wxPoint(30, 0), true);
    ASSERT_TRUE(strip_->HasCapture());
    wxMouseCaptureLostEvent lost(strip_->GetId());
    lost.SetEventObject(strip_);
    strip_->ProcessWindowEvent(lost);
    EXPECT_FALSE(strip_->HasCapture());
    EXPECT_EQ(key("StripDx"), "38");
    mouse(strip_, wxEVT_MOTION, from + wxPoint(90, 0), true); // no longer dragging
    EXPECT_EQ(rect().x, 38);
}

TEST_F(StripPlaceTest, ADoubleClickDeletesTheKeysAndPutsTheStripBack) {
    makeStrip();
    run();
    const wxPoint from = rect().GetTopLeft() + wxPoint(4, 4);
    dragBy(strip_, from, wxPoint(100, -80));
    ASSERT_EQ(key("StripCorner"), "bl");
    ASSERT_NE(rect().x, 8);
    mouse(strip_, wxEVT_LEFT_DCLICK, rect().GetTopLeft() + wxPoint(4, 4), true);
    for (const char* name : {"StripCorner", "StripDx", "StripDy"})
        EXPECT_EQ(key(name), "<absent>") << name;
    EXPECT_EQ(rect().x, 8);
    EXPECT_EQ(rect().GetBottom() + 1, 600 - 40 - 32);
    EXPECT_EQ(key("StripHintShown"), "1") << "only the place is forgotten";
}

TEST_F(StripPlaceTest, UnreadableKeysGiveTheDefaultPlaceAndAValidSetIsUsed) {
    struct Row {
        const char* corner;
        const char* dx;
        const char* dy;
        bool valid;
    };
    const Row rows[] = {{"x", "5", "5", false},
                        {"tr", "-3", "5", false},
                        {"tr", "20", nullptr, false}, // a missing key
                        {"tr", "20", "30", true}};
    for (const Row& row : rows) {
        for (const char* name : {"StripCorner", "StripDx", "StripDy"})
            config_->DeleteEntry(wxString(kConfigGroup) + name);
        config_->Write(wxString(kConfigGroup) + "StripCorner", wxString(row.corner));
        config_->Write(wxString(kConfigGroup) + "StripDx", wxString(row.dx));
        if (row.dy)
            config_->Write(wxString(kConfigGroup) + "StripDy", wxString(row.dy));
        openStore();
        makeStrip();
        run();
        if (row.valid) {
            EXPECT_EQ(rect().GetRight() + 1, 800 - 20) << row.corner;
            EXPECT_EQ(rect().y, 30) << row.corner;
        } else {
            EXPECT_EQ(rect().x, 8) << row.corner << " " << row.dx;
            EXPECT_EQ(rect().GetBottom() + 1, 600 - 40 - 32) << row.corner << " " << row.dx;
        }
    }
}

TEST_F(StripPlaceTest, AHandlerThatThrowsIsLoggedAndTheStripStaysUsable) {
    LogCapture log;
    makeStrip();
    run();
    strip_->mouseHookForTest = [] { throw std::runtime_error("boom"); };
    const wxPoint from = rect().GetTopLeft() + wxPoint(4, 4);
    EXPECT_NO_THROW(mouse(strip_, wxEVT_LEFT_DOWN, from, true));
    EXPECT_EQ(log.lines, std::vector<std::string>{"ChartGrabber: strip mouse: boom"});
    strip_->mouseHookForTest = nullptr;
    dragBy(strip_, from, wxPoint(30, 0));
    EXPECT_EQ(key("StripDx"), "38");
    EXPECT_NO_THROW(strip_->showResult("again", "", {}));
}

TEST_F(StripPlaceTest, OpeningDetailsKeepsTheBottomEdgeOfABottomAnchorAndTheTopEdgeOfATopAnchor) {
    makeStrip();
    strip_->showResult("done", "line one\nline two", {});
    const wxRect closed = rect();
    strip_->openDetails(true);
    EXPECT_EQ(rect().GetBottom(), closed.GetBottom());
    EXPECT_GT(rect().height, closed.height);
    strip_->openDetails(false);
    EXPECT_EQ(rect(), closed) << "closing gives the old place back";

    store_->setStripAnchor(cg::StripAnchor{cg::StripCorner::TopLeft, 20, 30});
    makeStrip();
    strip_->showResult("done", "line one\nline two", {});
    const wxRect top = rect();
    EXPECT_EQ(top.y, 30);
    strip_->openDetails(true);
    EXPECT_EQ(rect().y, 30);
    EXPECT_GT(rect().height, top.height);
}

TEST_F(StripPlaceTest, TheDragLineStartsDetailsUntilTheFirstDrop) {
    makeStrip();
    strip_->showResult("done", "the details", {});
    EXPECT_EQ(
        details()->GetValue(),
        "You can drag this strip anywhere on the chart. Double-click it to put it back.\n\nthe details");
    dragBy(strip_, rect().GetTopLeft() + wxPoint(4, 4), wxPoint(30, 0));
    EXPECT_EQ(details()->GetValue(), "the details");
    makeStrip(); // a later strip of the same store
    strip_->showResult("done", "the details", {});
    EXPECT_EQ(details()->GetValue(), "the details");
}

TEST_F(StripPlaceTest, AHeadlineCutToThreeLinesIsTheFirstParagraphOfDetails) {
    store_->setStripHintShown();
    makeStrip();
    setClient(300, 600);
    std::string text;
    for (int i = 0; i < 12; ++i)
        text += "Grabber: writing chart overview " + std::to_string(i) + " ";
    strip_->showResult(text, "the details", {});
    EXPECT_EQ(details()->GetValue(), wxString(text) + "\n\nthe details");
    strip_->showResult("short", "the details", {});
    EXPECT_EQ(details()->GetValue(), "the details") << "a headline that fits is not repeated";
}

TEST_F(StripPlaceTest, TheHintStripSitsAtTheCanvasTopLeftWithoutAGripAndDoesNotMove) {
    makeStrip();
    const size_t jobChildren = strip_->GetChildren().GetCount();
    makeStrip(ProgressStrip::Role::Hint);
    strip_->showResult("Draw a box", "", {});
    EXPECT_EQ(rect().GetTopLeft(), wxPoint(8, 8));
    EXPECT_EQ(strip_->GetChildren().GetCount() + 1, jobChildren) << "no grip";
    dragBy(strip_, rect().GetTopLeft() + wxPoint(4, 4), wxPoint(200, 100));
    EXPECT_EQ(rect().GetTopLeft(), wxPoint(8, 8));
    EXPECT_EQ(key("StripCorner"), "<absent>");
}

TEST_F(StripPlaceTest, AStripDeletedWithItsCanvasLeavesTheWeakReferenceNull) {
    makeStrip();
    run();
    wxWeakRef<ProgressStrip> weak(strip_);
    ASSERT_TRUE(weak);
    strip_ = nullptr; // owned by the canvas from here
    delete canvas_;
    canvas_ = nullptr;
    ocpn_fake::canvasWindow = nullptr;
    EXPECT_FALSE(weak);
}

// ---- the window host ----

class StripWindowHostTest : public StripPlaceTest {
protected:
    void TearDown() override {
        if (strip_)
            strip_->Destroy();
        strip_ = nullptr;
        ProgressStrip::isWaylandForTest = nullptr;
        StripPlaceTest::TearDown();
    }
    void makeWindowStrip(ProgressStrip::Role role = ProgressStrip::Role::Job) {
        if (strip_)
            strip_->Destroy();
        strip_ =
            new ProgressStrip(canvas_, look_, role, role == ProgressStrip::Role::Job ? store_.get() : nullptr,
                              ProgressStrip::Host::Window);
    }
    wxFrame* frame() const { return wxDynamicCast(strip_->GetParent(), wxFrame); }
    // The size the chart host gives the same content (a Hint strip: the job role has no grip in the window),
    // the place from the spec's numbers: Job {BottomLeft 8, 32}, Hint {TopLeft 8, 8}, the chart bar 40, in
    // screen pixels.
    wxRect expected(ProgressStrip::Role role) {
        ProgressStrip onChart(canvas_, look_, ProgressStrip::Role::Hint, nullptr,
                              ProgressStrip::Host::Canvas);
        onChart.showResult("Grabber: ready", "", {});
        const wxSize size = onChart.GetSize();
        const int y = role == ProgressStrip::Role::Job ? canvas_->GetClientSize().y - 40 - 32 - size.y : 8;
        return wxRect(canvas_->ClientToScreen(wxPoint(8, y)), size);
    }
    static bool listed(const wxWindow* w) {
        return wxTopLevelWindows.Find(const_cast<wxWindow*>(w)) != nullptr;
    }
};

TEST_F(StripWindowHostTest, BothRolesLiveInAFloatingFrameOfTheCanvasWindowAndTheChartHostStaysAChild) {
    for (const auto role : {ProgressStrip::Role::Job, ProgressStrip::Role::Hint}) {
        makeWindowStrip(role);
        ASSERT_NE(frame(), nullptr);
        EXPECT_EQ(ProgressStrip::Host::Window, strip_->host());
        EXPECT_TRUE(frame()->IsTopLevel());
        EXPECT_EQ(canvas_, frame()->GetParent());
        EXPECT_NE(0, frame()->GetWindowStyleFlag() & wxFRAME_FLOAT_ON_PARENT);
        EXPECT_NE(0, frame()->GetWindowStyleFlag() & wxFRAME_NO_TASKBAR);
        EXPECT_NE(0, frame()->GetWindowStyleFlag() & wxBORDER_NONE);
        EXPECT_EQ(0, frame()->GetWindowStyleFlag() & wxCAPTION);
        EXPECT_EQ("Chart Grabber", frame()->GetTitle());
        for (wxWindow* c : canvas_->GetChildren())
            EXPECT_EQ(nullptr, dynamic_cast<ProgressStrip*>(c));
    }
    makeStrip();
    EXPECT_EQ(ProgressStrip::Host::Canvas, strip_->host());
    EXPECT_EQ(canvas_, strip_->GetParent());
}

TEST_F(StripWindowHostTest, TheFrameIsShownAtTheDefaultAnchorOfTheRoleAndFollowsTheCanvas) {
    for (const auto role : {ProgressStrip::Role::Job, ProgressStrip::Role::Hint}) {
        makeWindowStrip(role);
        EXPECT_FALSE(frame()->IsShown());
        strip_->showResult("Grabber: ready", "", {});
        EXPECT_TRUE(frame()->IsShown());
        EXPECT_TRUE(strip_->IsShownOnScreen());
        EXPECT_EQ(expected(role), frame()->GetRect()) << (role == ProgressStrip::Role::Job ? "job" : "hint");
        EXPECT_EQ(frame()->GetClientSize(), strip_->GetSize()) << "the panel fills the frame";
        setClient(800, 500);
        const wxRect want = expected(role);
        EXPECT_TRUE(pump([&] { return frame()->GetRect() == want; })) << "the 500 ms timer moves the frame";
        strip_->Hide();
        EXPECT_FALSE(frame()->IsShown());
        strip_->showProgress("Grabber: working", 300, "Stop");
        EXPECT_TRUE(frame()->IsShown());
        EXPECT_TRUE(strip_->IsShownOnScreen());
        setClient(800, 600);
    }
}

TEST_F(StripWindowHostTest, TheWindowHostHasNoGripNoDragLineAndNoHintShownKey) {
    makeWindowStrip();
    run();
    EXPECT_TRUE(details()->GetValue().empty()) << "no drag line";
    EXPECT_EQ("<absent>", key("StripHintShown"));
    for (wxWindow* c : strip_->GetChildren())
        EXPECT_NE("wxWindow", wxString(c->GetClassInfo()->GetClassName())) << "no grip";
}

// ---- drag by the body through the compositor ----

class StripWindowDragTest : public StripWindowHostTest {
protected:
    struct Call {
        int x, y;
        unsigned time;
    };
    void makeDraggable(bool wayland, ProgressStrip::Role role = ProgressStrip::Role::Job) {
        ProgressStrip::isWaylandForTest = [wayland] {
            return wayland;
        }; // before the constructor reads the store
        makeWindowStrip(role);
        calls_.clear();
        strip_->beginMoveDragForTest = [this](int x, int y, unsigned t) { calls_.push_back({x, y, t}); };
        run();
    }
    // `win` gets the event at a point given in screen pixels
    void send(wxWindow* win, wxEventType type, wxPoint screen, bool leftDown, long time = 0) {
        wxMouseEvent e(type);
        e.SetEventObject(win);
        e.SetPosition(win->ScreenToClient(screen));
        e.SetLeftDown(leftDown);
        e.SetTimestamp(time);
        win->ProcessWindowEvent(e);
    }
    wxGauge* gauge() const {
        for (wxWindow* c : strip_->GetChildren())
            if (auto* g = dynamic_cast<wxGauge*>(c))
                return g;
        return nullptr;
    }
    wxPoint inside(wxWindow* w) const { return w->ClientToScreen(wxPoint(4, 4)); }
    std::vector<Call> calls_;
};

TEST_F(StripWindowDragTest, MotionBeyondTheThresholdBeginsTheCompositorMoveOnceFromPanelHeadlineAndGauge) {
    makeDraggable(true);
    const wxRect before = frame()->GetRect();
    for (wxWindow* via :
         {static_cast<wxWindow*>(strip_), static_cast<wxWindow*>(text()), static_cast<wxWindow*>(gauge())}) {
        calls_.clear();
        const wxPoint from = inside(via);
        send(via, wxEVT_MOTION, from + wxPoint(40, 0), true);
        EXPECT_TRUE(calls_.empty()) << "motion without a press";
        send(via, wxEVT_LEFT_DOWN, from, true);
        send(via, wxEVT_LEFT_UP, from, false);
        send(via, wxEVT_MOTION, from + wxPoint(40, 0), true);
        EXPECT_TRUE(calls_.empty()) << "a press and release without motion; motion after the release";
        send(via, wxEVT_LEFT_DOWN, from, true);
        send(via, wxEVT_MOTION, from + wxPoint(5, -5), true);
        EXPECT_TRUE(calls_.empty()) << "5 px is a shaky tap";
        send(via, wxEVT_MOTION, from + wxPoint(30, 0), true, 1234);
        ASSERT_EQ(1u, calls_.size());
        EXPECT_EQ(from.x + 30, calls_[0].x);
        EXPECT_EQ(from.y, calls_[0].y);
        EXPECT_EQ(1234u, calls_[0].time);
        send(via, wxEVT_MOTION, from + wxPoint(60, 0), true);
        send(via, wxEVT_MOTION, from + wxPoint(90, 0), true);
        EXPECT_EQ(1u, calls_.size()) << "once per press";
        send(via, wxEVT_LEFT_UP, from + wxPoint(90, 0), false);
        EXPECT_EQ(before, frame()->GetRect()) << "the strip itself does not move";
        EXPECT_FALSE(strip_->HasCapture());
    }
}

TEST_F(StripWindowDragTest, ButtonsAndTheDetailsFieldNeverStartTheMove) {
    makeDraggable(true);
    strip_->openDetails(true);
    for (wxWindow* c : strip_->GetChildren()) {
        if (!dynamic_cast<wxButton*>(c) && !dynamic_cast<wxTextCtrl*>(c))
            continue;
        const wxPoint from = inside(c);
        send(c, wxEVT_LEFT_DOWN, from, true);
        send(c, wxEVT_MOTION, from + wxPoint(60, 0), true);
        send(c, wxEVT_LEFT_UP, from + wxPoint(60, 0), false);
    }
    EXPECT_TRUE(calls_.empty());
    strip_->showResult("done", "", {{"Open", [] {}}});
    for (wxWindow* c : strip_->GetChildren())
        if (auto* b = dynamic_cast<wxButton*>(c)) {
            send(b, wxEVT_LEFT_DOWN, inside(b), true);
            send(b, wxEVT_MOTION, inside(b) + wxPoint(60, 0), true);
        }
    EXPECT_TRUE(calls_.empty());
}

TEST_F(StripWindowDragTest, TheHintRoleIsNotDraggable) {
    makeDraggable(true, ProgressStrip::Role::Hint);
    strip_->showResult("hint", "", {});
    const wxPoint from = inside(strip_);
    send(strip_, wxEVT_LEFT_DOWN, from, true);
    send(strip_, wxEVT_MOTION, from + wxPoint(60, 0), true);
    EXPECT_TRUE(calls_.empty());
}

TEST_F(StripWindowDragTest, AfterTheMoveTheTimerAndPlaceKeepTheSizeRuleButNotThePosition) {
    makeDraggable(true);
    const wxPoint from = inside(strip_);
    send(strip_, wxEVT_LEFT_DOWN, from, true);
    send(strip_, wxEVT_MOTION, from + wxPoint(30, 0), true);
    ASSERT_EQ(1u, calls_.size());
    frame()->Move(canvas_->ClientToScreen(wxPoint(300, 200))); // what the compositor does
    const wxPoint moved = frame()->GetPosition();
    strip_->showProgress("Grabber: a much longer line of progress text than before, to change the width", 500,
                         "Stop");
    wxStopWatch watch;
    while (watch.Time() < 1300) { // two timer ticks
        wxTheApp->Yield(true);
        wxMilliSleep(20);
    }
    EXPECT_EQ(moved, frame()->GetPosition()) << "not pulled back to the default corner";
    EXPECT_EQ(strip_->GetSize(), frame()->GetClientSize());
    setClient(300, 600); // the width rule still applies
    EXPECT_TRUE(pump([&] { return frame()->GetSize().x <= 300; }));
    EXPECT_EQ(moved, frame()->GetPosition());
}

TEST_F(StripWindowDragTest, OnX11TheRestingFrameStoresItsAnchorOncePerDropAndANewStripStartsThere) {
    makeDraggable(false);
    const wxPoint from = inside(strip_);
    send(strip_, wxEVT_LEFT_DOWN, from, true);
    send(strip_, wxEVT_MOTION, from + wxPoint(30, 0), true);
    ASSERT_EQ(1u, calls_.size());
    frame()->Move(canvas_->ClientToScreen(wxPoint(300, 60)));
    ASSERT_TRUE(pump([&] { return key("StripCorner") != "<absent>"; }));
    const wxPoint at = frame()->GetPosition();
    const auto corner = cg::parseCornerKey(key("StripCorner").ToStdString());
    ASSERT_TRUE(corner);
    const cg::StripAnchor saved{*corner, wxAtoi(key("StripDx")), wxAtoi(key("StripDy"))};
    EXPECT_EQ(60, saved.dy) << "distance to the top edge of the canvas";
    config_->DeleteGroup(kConfigGroup);
    wxStopWatch watch;
    while (watch.Time() < 1300) {
        wxTheApp->Yield(true);
        wxMilliSleep(20);
    }
    EXPECT_EQ("<absent>", key("StripCorner")) << "stored once per drop";
    EXPECT_EQ(at, frame()->GetPosition());

    store_->setStripAnchor(saved);
    makeWindowStrip();
    run();
    EXPECT_EQ(at, frame()->GetPosition()) << "the next session is placed from the keys";
}

TEST_F(StripWindowDragTest, OnX11ADoubleClickDeletesTheKeysAndPutsTheDefaultPlaceBack) {
    makeDraggable(false);
    const wxPoint def = frame()->GetPosition();
    store_->setStripAnchor(cg::StripAnchor{cg::StripCorner::TopLeft, 50, 70});
    makeDraggable(false);
    ASSERT_EQ(canvas_->ClientToScreen(wxPoint(50, 70)), frame()->GetPosition());
    send(strip_, wxEVT_LEFT_DCLICK, inside(strip_), true);
    EXPECT_EQ("<absent>", key("StripCorner"));
    EXPECT_EQ(def, frame()->GetPosition());
}

TEST_F(StripWindowDragTest, OnWaylandNothingIsStoredAndADoubleClickDoesNothing) {
    makeDraggable(true);
    const wxPoint def = frame()->GetPosition();
    store_->setStripAnchor(cg::StripAnchor{cg::StripCorner::TopLeft, 50, 70});
    makeDraggable(true);
    EXPECT_EQ(def, frame()->GetPosition()) << "the stored anchor is not read on Wayland";
    const wxPoint from = inside(strip_);
    send(strip_, wxEVT_LEFT_DOWN, from, true);
    send(strip_, wxEVT_MOTION, from + wxPoint(30, 0), true);
    ASSERT_EQ(1u, calls_.size());
    frame()->Move(canvas_->ClientToScreen(wxPoint(500, 60)));
    config_->DeleteGroup(kConfigGroup);
    wxStopWatch watch;
    while (watch.Time() < 1300) {
        wxTheApp->Yield(true);
        wxMilliSleep(20);
    }
    EXPECT_EQ("<absent>", key("StripCorner"));
    store_->setStripAnchor(cg::StripAnchor{cg::StripCorner::TopLeft, 50, 70});
    send(strip_, wxEVT_LEFT_DCLICK, inside(strip_), true);
    EXPECT_EQ("tl", key("StripCorner")) << "the keys stay";
    EXPECT_EQ(canvas_->ClientToScreen(wxPoint(500, 60)), frame()->GetPosition());
}

TEST_F(StripWindowDragTest, AThrowingMouseHandlerIsLoggedAndTheStripStaysUsable) {
    LogCapture log;
    makeDraggable(true);
    strip_->mouseHookForTest = [] { throw std::runtime_error("boom"); };
    const wxPoint from = inside(strip_);
    EXPECT_NO_THROW(send(strip_, wxEVT_LEFT_DOWN, from, true));
    EXPECT_EQ(log.lines, std::vector<std::string>{"ChartGrabber: strip mouse: boom"});
    strip_->mouseHookForTest = nullptr;
    send(strip_, wxEVT_LEFT_DOWN, from, true);
    send(strip_, wxEVT_MOTION, from + wxPoint(30, 0), true);
    EXPECT_EQ(1u, calls_.size());
    EXPECT_NO_THROW(strip_->showResult("again", "", {}));
}

// OpenCPN's canvas sits inside the main frame; a canvas that is itself a top-level window (the other
// fixtures) hides every "top-level parent of the canvas" rule. An intermediate panel catches GetParent.
TEST_F(StripWindowHostTest, TheFramesParentIsTheTopLevelWindowOfTheCanvasNotTheCanvasNorItsParent) {
    auto* top = new wxFrame(nullptr, wxID_ANY, "main", wxPoint(30, 30), wxSize(900, 700));
    auto* middle = new wxPanel(top, wxID_ANY);
    auto* inner = new wxPanel(middle, wxID_ANY, wxPoint(0, 0), wxSize(800, 600));
    top->Show();
    ocpn_fake::canvasWindow = inner;
    auto* strip =
        new ProgressStrip(inner, look_, ProgressStrip::Role::Hint, nullptr, ProgressStrip::Host::Window);
    auto* f = wxDynamicCast(strip->GetParent(), wxFrame);
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(top, f->GetParent());
    EXPECT_NE(static_cast<wxWindow*>(inner), f->GetParent());
    EXPECT_NE(static_cast<wxWindow*>(middle), f->GetParent());
    strip->Destroy();
    ocpn_fake::canvasWindow = canvas_;
    top->Destroy();
    wxTheApp->ProcessPendingEvents();
}

TEST_F(StripWindowHostTest, DestroyRemovesTheFrameAtOnceAndNullsTheOwnersWeakReference) {
    makeWindowStrip();
    run();
    wxFrame* f = frame();
    wxWeakRef<ProgressStrip> weak(strip_);
    ASSERT_TRUE(listed(f));
    EXPECT_TRUE(strip_->Destroy());
    strip_ = nullptr;
    EXPECT_FALSE(listed(f)) << "before any idle event";
    EXPECT_FALSE(weak);
}

TEST_F(StripWindowHostTest, TheMainWindowDeletedFirstTakesTheFrameAndTheOwnersDestroyIsSkipped) {
    makeWindowStrip();
    run();
    wxWeakRef<ProgressStrip> weak(strip_);
    wxFrame* f = frame();
    delete canvas_; // OpenCPN closing: its child frame goes with it
    canvas_ = nullptr;
    ocpn_fake::canvasWindow = nullptr;
    EXPECT_FALSE(weak);
    EXPECT_FALSE(listed(f));
    if (weak) // the owner's code
        weak->Destroy();
    strip_ = nullptr;
}

// The real OpenCPN order: the canvas (a child) is deleted at once, the main frame lives on while the CLI is
// stopped (events are pumped), and only then does the owner's Destroy run. The shown strip's timer must not
// touch the freed canvas (run under --asan).
TEST_F(StripWindowHostTest, TheCanvasChildDeletedWhileTheMainFrameLivesLeavesTheShownStripsTimerHarmless) {
    auto* main = new wxFrame(nullptr, wxID_ANY, "main", wxPoint(20, 20), wxSize(800, 600));
    auto* kid = new wxPanel(main, wxID_ANY, wxPoint(0, 0), wxSize(800, 600));
    main->Show();
    ocpn_fake::canvasWindow = kid;
    delete strip_;
    strip_ = new ProgressStrip(kid, look_, ProgressStrip::Role::Job, nullptr, ProgressStrip::Host::Window);
    run();
    ASSERT_TRUE(frame()->IsShown());
    delete kid; // MyFrame::OnCloseWindow deletes the canvases first
    ocpn_fake::canvasWindow = nullptr;
    wxStopWatch watch;
    while (watch.Time() < 800) { // longer than the 500 ms placement timer
        wxTheApp->Yield(true);
        wxMilliSleep(20);
    }
    EXPECT_TRUE(strip_->Destroy());
    strip_ = nullptr;
    main->Destroy();
    wxTheApp->ProcessPendingEvents();
}

TEST_F(StripWindowHostTest, TheFramePanelAndHeadlineCarryTheBodyAndControlColoursAtNightAndNothingByDay) {
    const wxColour uibck(11, 222, 33), uitx1(201, 12, 133), dilg1(110, 111, 112), dilg3(130, 131, 132);
    ocpn_fake::globalColors["UIBCK"] = uibck;
    ocpn_fake::globalColors["UITX1"] = uitx1;
    ocpn_fake::globalColors["DILG1"] = dilg1;
    ocpn_fake::globalColors["DILG3"] = dilg3;
    const wxColour face = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
    const wxColour ink = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
    makeWindowStrip();
    strip_->showResult("Done", "", {{"Open", nullptr}});
    const auto check = [&](const wxColour& bg, const wxColour& fg, const wxColour& cbg, const wxColour& cfg,
                           const char* when) {
        EXPECT_EQ(bg, strip_->GetBackgroundColour()) << when;
        EXPECT_EQ(fg, strip_->GetForegroundColour()) << when;
        EXPECT_EQ(bg, frame()->GetBackgroundColour()) << when;
        EXPECT_EQ(bg, text()->GetBackgroundColour()) << when;
        EXPECT_EQ(fg, text()->GetForegroundColour()) << when;
        size_t buttons = 0;
        for (wxWindow* c : strip_->GetChildren())
            if (auto* b = dynamic_cast<wxButton*>(c)) {
                ++buttons;
                EXPECT_EQ(cbg, b->GetBackgroundColour()) << when;
                EXPECT_EQ(cfg, b->GetForegroundColour()) << when;
            }
        EXPECT_EQ(3u, buttons) << when;
    };
    check(face, ink, face, ink, "day");
    look_.setScheme(PI_GLOBAL_COLOR_SCHEME_NIGHT);
    check(uibck, uitx1, dilg1, dilg3, "night");
    look_.setScheme(PI_GLOBAL_COLOR_SCHEME_DAY);
    check(face, ink, face, ink, "back to day");
    ocpn_fake::globalColors.clear();
}

} // namespace

// ---- the strip lines of the plugin's log file ----

class StripLogTest : public StripWindowHostTest {
protected:
    void SetUp() override {
        StripWindowHostTest::SetUp();
        if (!canvas_)
            return;
        ASSERT_TRUE(cg::openPluginLog(dir_ + "/strip.log"));
    }
    void TearDown() override {
        cg::closePluginLog();
        StripWindowHostTest::TearDown();
    }
    // The file's lines after the 24-character stamp, `strip ` lines only.
    std::vector<std::string> stripLines() const {
        std::vector<std::string> lines;
        std::ifstream in(dir_ + "/strip.log");
        for (std::string line; std::getline(in, line);)
            if (line.size() > 24 && line.find("strip ", 24) != std::string::npos)
                lines.push_back(line.substr(24));
        return lines;
    }
    static std::string placedLine(const std::string& role, const wxRect& r, int canvasHeight) {
        return "strip " + role + " placed: " + std::to_string(r.x) + "," + std::to_string(r.y) + " " +
               std::to_string(r.width) + "x" + std::to_string(r.height) + ", corner " +
               (role == "job" ? "bl" : "tl") + ", canvas 800x" + std::to_string(canvasHeight) +
               ", chart bar 40, on screen yes";
    }
};

TEST_F(StripLogTest, TheChartHostWritesCreatedShownOnePlacedHiddenAndDestroyed) {
    makeStrip();
    const wxSize canvasSize = canvas_->GetSize();
    run();
    const auto placed = [&] { // the first placement after a show is the 500 ms timer's
        for (const std::string& l : stripLines())
            if (l.find(" placed: ") != std::string::npos)
                return true;
        return false;
    };
    ASSERT_TRUE(pump(placed));
    const wxRect shownAt = rect();
    for (const auto scheme : {PI_GLOBAL_COLOR_SCHEME_DUSK, PI_GLOBAL_COLOR_SCHEME_NIGHT,
                              PI_GLOBAL_COLOR_SCHEME_DAY}) // each one places the strip again, same numbers
        look_.setScheme(scheme);
    strip_->Hide();
    delete strip_;
    strip_ = nullptr;
    EXPECT_EQ(stripLines(), (std::vector<std::string>{
                                "strip job created on chart: parent wxFrame " + std::to_string(canvasSize.x) +
                                    "x" + std::to_string(canvasSize.y),
                                "strip job shown",
                                placedLine("job", shownAt, 600),
                                "strip job hidden",
                                "strip job destroyed",
                            }));
    EXPECT_EQ(shownAt.x, 8);
    EXPECT_EQ(shownAt.y + shownAt.height, 600 - 40 - 32);
}

TEST_F(StripLogTest, TheWindowHostWritesTheFramesParentAndScreenPixels) {
    makeWindowStrip(ProgressStrip::Role::Hint);
    const wxSize canvasSize = canvas_->GetSize(); // the frame's parent is the canvas window
    strip_->showResult("Grabber: ready", "", {});
    const wxRect at = frame()->GetRect();
    strip_->Hide();
    strip_->Destroy();
    strip_ = nullptr;
    EXPECT_EQ(stripLines(), (std::vector<std::string>{
                                "strip hint created on window: parent wxFrame " +
                                    std::to_string(canvasSize.x) + "x" + std::to_string(canvasSize.y),
                                "strip hint shown",
                                placedLine("hint", at, 600),
                                "strip hint hidden",
                                "strip hint destroyed",
                            }));
}

TEST_F(StripLogTest, AfterEachShowThePlacementIsWrittenAgainAndDebugAddsChangedNumbers) {
    makeStrip();
    const auto placed = [&] {
        size_t n = 0;
        for (const std::string& l : stripLines())
            n += l.find(" placed: ") != std::string::npos;
        return n;
    };
    run();
    ASSERT_TRUE(pump([&] { return placed() == 1u; }));
    strip_->Hide();
    run();
    ASSERT_TRUE(pump([&] { return placed() == 2u; })) << "one per show";
    setClient(800, 500); // the timer places the strip in the new canvas; debug is off: no new line
    ASSERT_TRUE(pump([&] { return rect().GetBottom() + 1 == 500 - 40 - 32; }));
    EXPECT_EQ(placed(), 2u);
    cg::setPluginLogDebug(true);
    setClient(800, 560);
    ASSERT_TRUE(pump([&] { return placed() == 3u; }));
    const std::vector<std::string> lines = stripLines();
    EXPECT_EQ(lines.back(), "debug: " + placedLine("job", rect(), 560));
    pump([] { return false; }); // two seconds of 500 ms timers with the same numbers
    EXPECT_EQ(placed(), 3u);
}

TEST_F(StripLogTest, WithDebugOffTheTimerWritesNoFurtherPlacedLineNotEvenForChangedNumbers) {
    makeStrip();
    const auto placed = [&] {
        size_t n = 0;
        for (const std::string& l : stripLines())
            n += l.find(" placed: ") != std::string::npos;
        return n;
    };
    run();
    ASSERT_TRUE(pump([&] { return placed() == 1u; }));
    pump([] { return false; }); // timers with the same numbers
    EXPECT_EQ(placed(), 1u);
    setClient(800, 500);
    ASSERT_TRUE(pump([&] { return rect().GetBottom() + 1 == 500 - 40 - 32; }));
    EXPECT_EQ(placed(), 1u) << "changed numbers, debug off: nothing";
    cg::setPluginLogDebug(true);
    setClient(800, 560);
    ASSERT_TRUE(pump([&] { return placed() == 2u; })) << "debug on writes the changed line";
    cg::setPluginLogDebug(false);
}
