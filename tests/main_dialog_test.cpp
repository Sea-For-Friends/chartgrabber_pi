// SPDX-License-Identifier: GPL-3.0-or-later
// MainDialog and NewChartPanel in
// cg_gui_tests. The OpenCPN API is the shared fakes of ocpn_fakes.cpp: one canvas window, a linear viewport
// (10 px per degree, centre (100,100) = lat 50 / lon 10) and routes steered through ocpn_fake::routes. The
// area tool is the real one; its outline is read back by drawing it into a bitmap. Expected texts are the
// literals of the spec (sections 4.2, 6, 7.6), not the functions that produce them. The entry points of
// chartgrabber_pi.cpp (toolbar / context menu bodies, g_closing) need the plugin object and are NOT covered
// here.
#include "area_tool.h"
#include "config_store.h"
#include "job_controller.h"
#include "look.h"
#include "main_dialog.h"
#include "ocpn_fakes.h"
#include "progress_strip.h"
#include "sources_stub.h"
#include "wx_env.h"

#include <gtest/gtest.h>

#include <wx/app.h>
#include <wx/apptrait.h>
#include <wx/bitmap.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/collpane.h>
#include <wx/dcmemory.h>
#include <wx/evtloop.h>
#include <wx/fileconf.h>
#include <wx/frame.h>
#include <wx/image.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/tglbtn.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <ocpn_plugin.h>

namespace {

namespace fs = std::filesystem;

// Texts of the spec, as literals.
const char* const kOutside = "The area is too far north or south (beyond 85 degrees).";
const char* const kAntimeridian =
    "The area crosses the 180 degree meridian. Make two charts, one on each side.";
const char* const kFewPoints = "The route needs at least two points.";
const char* const kNoViewport = "Move the chart once, then try again.";
const char* const kEnterName = "Enter a chart name";
const char* const kEstimating = "Working out the size..."; // the estimate comes before the name
const char* const kNoRoutes = "No routes. Create a route first.";
const char* const kRunning = "A download is running";
const char* const kHint = "Drag a rectangle on the chart, or tap two corners. Right-click or Cancel to stop.";

const char* const kGuidZulu = "aaaaaaaa-1111-4111-8111-000000000001";
const char* const kGuidNone = "bbbbbbbb-2222-4222-8222-000000000002";
const char* const kGuidAlfa = "cccccccc-3333-4333-8333-000000000003";
const char* const kUnlisted = "dddddddd-4444-4444-8444-000000000004";
const char* const kAlfaName = "\xD0\x90\xD0\xBB\xD1\x84\xD0\xB0 d'x"; // "Алфа d'x"

wxString w(const std::string& utf8) {
    return wxString::FromUTF8(utf8.c_str());
}
std::string u(const wxString& s) {
    return s.utf8_string();
}

template <class T> void collect(wxWindow* top, std::vector<T*>& out) {
    for (wxWindow* child : top->GetChildren()) {
        if (auto* t = dynamic_cast<T*>(child))
            out.push_back(t);
        collect(child, out);
    }
}

class DialogTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!wxTheApp || !wxTheApp->IsGUI())
            GTEST_SKIP() << "no display: the main dialog tests were NOT run";
        loop_.reset(wxTheApp->GetTraits()->CreateEventLoop());
        ASSERT_NE(loop_, nullptr);
        activator_ = std::make_unique<wxEventLoopActivator>(loop_.get());
        char tmpl[] = "/tmp/cg-dialog-XXXXXX";
        ASSERT_NE(mkdtemp(tmpl), nullptr);
        root_ = tmpl;
        ocpn_fake::privateData = w((root_ / "private").string());
        ocpn_fake::touch = false;
        ocpn_fake::scale = 1.0;
        ocpn_fake::contentScale = 1.0;
        ocpn_fake::routes.clear();
        ocpn_fake::selectedRoute.clear();
        ocpn_fake::activeRoute.clear();
        ocpn_fake::waypointsCreated = ocpn_fake::waypointsDeleted = 0;
        ocpn_fake::underMouse = 0;
        ocpn_fake::refreshed.clear();
        config_ = std::make_unique<wxFileConfig>(wxString("cgdialogtest"), wxEmptyString,
                                                 w((root_ / "opencpn.conf").string()), wxEmptyString,
                                                 wxCONFIG_USE_LOCAL_FILE);
        store_ = std::make_unique<ConfigStore>(config_.get());
        // the page runs the tile program for its sources. A stand-in script (not the real CLI)
        // answers with one usable map, so the status line shows the area and name reasons these tests are
        // about.
        sources_ = std::make_unique<stub::Sources>(root_);
        sources_->records({{"{D643C5A1-4331-4387-9BE2-DDDF185C117E}", "map", "OSM OpenStreetMap.org - MAPNIK",
                            3857, "ok", ""}});
        cg::Settings settings = store_->loadSettings();
        settings.cliPath = sources_->cli();
        settings.stripOnChart = true; // the landed tests are about the chart host (window host: own tests)
        store_->saveSettings(settings);
        frame_ = new wxFrame(nullptr, wxID_ANY, "canvas", wxPoint(50, 60),
                             wxSize(1600, 480)); // the strip takes <= 60 %
        ocpn_fake::canvasWindow = frame_;
        ocpn_fake::canvases = {frame_};
        ocpn_fake::focus = frame_;
        area_ = std::make_unique<AreaTool>([] { return wxColour(10, 200, 30); }, [] { return false; });
        setViewport(45, 55, 5, 15);
    }
    void TearDown() override {
        if (!frame_)
            return;
        delete dialog_;
        dialog_ = nullptr;
        jobs_.reset();
        area_.reset();
        ocpn_fake::canvasWindow = nullptr;
        ocpn_fake::canvases.clear();
        ocpn_fake::focus = nullptr;
        frame_->Destroy();
        wxTheApp->ProcessPendingEvents();
        wxTheApp->ProcessIdle(); // OnIdle deletes the frame
        store_.reset();
        config_.reset();
        sources_.reset();
        activator_.reset();
        loop_.reset();
        expectNoProcessesUnder(root_.string());
        fs::remove_all(root_);
    }

    // ---- fixture helpers
    void setViewport(double latMin, double latMax, double lonMin, double lonMax) {
        PlugIn_ViewPort vp{};
        vp.clat = 50;
        vp.clon = 10;
        vp.view_scale_ppm = 10;
        vp.pix_width = vp.pix_height = 200;
        vp.bValid = true;
        vp.lat_min = latMin;
        vp.lat_max = latMax;
        vp.lon_min = lonMin;
        vp.lon_max = lonMax;
        area_->rememberViewport(0, vp);
    }
    void addRoute(const std::string& guid, const std::string& name, int points) {
        ocpn_fake::Route r;
        r.guid = w(guid);
        r.name = w(name);
        for (int i = 0; i < points; ++i)
            r.points.emplace_back(40.0 + i, 5.0 + i);
        ocpn_fake::routes.push_back(r);
    }
    // list order is NOT alphabetical: Zulu, unnamed, the Cyrillic one
    void threeRoutes() {
        addRoute(kGuidZulu, "Zulu route", 3);
        addRoute(kGuidNone, "", 2);
        addRoute(kGuidAlfa, kAlfaName, 2);
    }
    void makeDialog() {
        jobs_ = std::make_unique<JobController>(*store_, look_, *area_);
        dialog_ = new MainDialog(frame_, *store_, look_, *area_, *jobs_);
        waitForSources();
    }
    // Dispatches events until the sources run of the page has ended (the chart choice no longer says
    // "Loading").
    void waitForSources() {
        std::vector<wxChoice*> choices;
        collect(dialog_, choices);
        ASSERT_GE(choices.size(), 2u); // the route choice, then the chart choice
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (choices[1]->GetCount() == 1 && choices[1]->GetString(0) == "Loading chart sources...") {
            ASSERT_LT(std::chrono::steady_clock::now(), deadline) << "the sources run did not end";
            loop_->DispatchTimeout(20);
        }
    }
    // Dispatches events until the estimate of the page ended (the status no longer says "Working out the
    // size...").
    void waitForEstimate() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (shows(kEstimating)) {
            ASSERT_LT(std::chrono::steady_clock::now(), deadline) << "the estimate did not end";
            loop_->DispatchTimeout(20);
        }
    }
    void saveLastRoute(const std::string& guid) {
        ConfigStore::LastChoice c = store_->loadLastChoice();
        c.routeGuid = guid;
        store_->saveLastChoice(c);
    }
    void saveCorridor(double nm) {
        cg::Settings s = store_->loadSettings();
        s.corridorNm = nm;
        store_->saveSettings(s);
    }

    // ---- window access
    template <class T> T* labelled(const char* label, wxWindow* in = nullptr) {
        return dynamic_cast<T*>(wxWindow::FindWindowByLabel(label, in ? in : dialog_));
    }
    wxToggleButton* view() { return labelled<wxToggleButton>("Current view"); }
    wxToggleButton* draw() { return labelled<wxToggleButton>("Draw on chart"); }
    wxToggleButton* route() { return labelled<wxToggleButton>("Along route"); }
    wxToggleButton* newChartTab() { return labelled<wxToggleButton>("New chart"); }
    wxToggleButton* myChartsTab() {
        return labelled<wxToggleButton>("My charts (0)");
    } // "My charts (<rows>)",
    wxButton* download() { return labelled<wxButton>("Download"); }
    wxButton* narrower() { return labelled<wxButton>("-"); }
    wxButton* wider() { return labelled<wxButton>("+"); }
    wxChoice* routeChoice() {
        std::vector<wxChoice*> v;
        collect(dialog_, v);
        return v.empty() ? nullptr : v.front();
    }
    wxTextCtrl* nameField() {
        std::vector<wxTextCtrl*> v;
        collect(dialog_, v);
        return v.empty() ? nullptr : v.front();
    }
    std::vector<wxString> shownTexts() {
        std::vector<wxStaticText*> v;
        collect(dialog_, v);
        std::vector<wxString> out;
        for (wxStaticText* t : v)
            if (t->IsShown())
                out.push_back(t->GetLabel());
        return out;
    }
    bool shows(const char* text) {
        for (const wxString& t : shownTexts())
            if (t == text)
                return true;
        return false;
    }
    // Which of the known failure reasons are on the status line now.
    std::vector<std::string> reasons() {
        std::vector<std::string> out;
        for (const char* r : {kOutside, kAntimeridian, kFewPoints, kNoViewport, kEstimating, kEnterName})
            if (shows(r))
                out.push_back(r);
        return out;
    }
    std::string corridorShown() {
        for (const wxString& t : shownTexts())
            if (t.EndsWith(" NM"))
                return u(t);
        return "<none>";
    }
    ProgressStrip* hintStrip() {
        for (wxWindow* c : frame_->GetChildren()) // the strip is a child of the canvas
            if (auto* s = dynamic_cast<ProgressStrip*>(c))
                return s;
        return nullptr;
    }
    std::string hintText() {
        std::vector<wxStaticText*> v;
        if (hintStrip())
            collect(hintStrip(), v);
        return v.empty() ? "<no text>" : u(v.front()->GetLabelText());
    }

    // ---- user actions
    static void press(wxToggleButton* b) {
        b->SetValue(!b->GetValue()); // what the click does before the event
        wxCommandEvent e(wxEVT_TOGGLEBUTTON, b->GetId());
        e.SetEventObject(b);
        e.SetInt(b->GetValue());
        b->GetEventHandler()->ProcessEvent(e);
    }
    static void click(wxButton* b) {
        ASSERT_NE(b, nullptr);
        wxCommandEvent e(wxEVT_BUTTON, b->GetId());
        e.SetEventObject(b);
        b->GetEventHandler()->ProcessEvent(e);
    }
    static void choose(wxChoice* c, int i) {
        c->SetSelection(i);
        wxCommandEvent e(wxEVT_CHOICE, c->GetId());
        e.SetEventObject(c);
        e.SetInt(i);
        c->GetEventHandler()->ProcessEvent(e);
    }
    bool mouse(wxEventType t, int x, int y, bool left = false) {
        wxMouseEvent e(t);
        e.SetPosition(wxPoint(x, y));
        e.SetLeftDown(left);
        return area_->onMouse(e);
    }
    void dragBox(int x1, int y1, int x2, int y2) {
        mouse(wxEVT_LEFT_DOWN, x1, y1);
        mouse(wxEVT_MOTION, x2, y2, true);
        mouse(wxEVT_LEFT_UP, x2, y2);
    }
    void pump() { wxTheApp->ProcessPendingEvents(); }

    // ---- the outline, read back from what the area tool draws
    wxRect outline() {
        wxBitmap bmp(200, 200, 24);
        wxMemoryDC dc(bmp);
        dc.SetBackground(*wxWHITE_BRUSH);
        dc.Clear();
        PlugIn_ViewPort vp{};
        vp.clat = 50;
        vp.clon = 10;
        vp.view_scale_ppm = 10;
        vp.pix_width = vp.pix_height = 200;
        vp.bValid = true;
        area_->drawDC(dc, vp);
        dc.SelectObject(wxNullBitmap);
        const wxImage img = bmp.ConvertToImage();
        int x0 = 1000, y0 = 1000, x1 = -1, y1 = -1;
        for (int y = 0; y < 200; ++y)
            for (int x = 0; x < 200; ++x)
                if (img.GetRed(x, y) != 255 || img.GetGreen(x, y) != 255 || img.GetBlue(x, y) != 255) {
                    x0 = std::min(x0, x);
                    y0 = std::min(y0, y);
                    x1 = std::max(x1, x);
                    y1 = std::max(y1, y);
                }
        return x1 < 0 ? wxRect() : wxRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
    }
    bool outlined() { return !outline().IsEmpty(); }
    // box lon 10..15, lat 50..54 is the rectangle (100,100)-(150,60) of the linear fake (2 px pen)
    static void expectBoxOutline(const wxRect& r) {
        EXPECT_NEAR(r.GetLeft(), 100, 2);
        EXPECT_NEAR(r.GetRight(), 150, 2);
        EXPECT_NEAR(r.GetTop(), 60, 2);
        EXPECT_NEAR(r.GetBottom(), 100, 2);
    }
    inline static const cg::BBox kBox{10, 50, 15, 54};

    fs::path root_;
    std::unique_ptr<wxEventLoopBase> loop_;
    std::unique_ptr<wxEventLoopActivator> activator_;
    std::unique_ptr<stub::Sources> sources_;
    std::unique_ptr<wxFileConfig> config_;
    std::unique_ptr<ConfigStore> store_;
    Look look_;
    wxFrame* frame_ = nullptr;
    std::unique_ptr<AreaTool> area_;
    std::unique_ptr<JobController> jobs_;
    MainDialog* dialog_ = nullptr;
};

// ---- window basics ----

TEST_F(DialogTest, TheDialogIsTitledFixedSizeAndShownByShowNewChart) {
    makeDialog();
    EXPECT_EQ(u(dialog_->GetTitle()), "Chart Grabber");
    EXPECT_EQ(dialog_->GetWindowStyleFlag() & wxRESIZE_BORDER, 0);
    EXPECT_FALSE(dialog_->IsShown()) << "the constructor shows nothing";
    dialog_->showNewChart(cg::AreaKind::View, std::nullopt);
    EXPECT_TRUE(dialog_->IsShown());
    EXPECT_TRUE(newChartTab()->GetValue());
    EXPECT_FALSE(myChartsTab()->GetValue());
}

TEST_F(DialogTest, TheSizeFollowsTheContentWhenTheRouteRowsAppearAndGoAway) {
    threeRoutes();
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::View, std::nullopt);
    const wxSize viewSize = dialog_->GetSize();
    dialog_->showNewChart(cg::AreaKind::Route, std::nullopt);
    const wxSize routeSize = dialog_->GetSize();
    EXPECT_GT(routeSize.y, viewSize.y);
    EXPECT_LE(routeSize.x, 800);
    EXPECT_LE(routeSize.y, 480);
    dialog_->showNewChart(cg::AreaKind::View, std::nullopt);
    EXPECT_EQ(dialog_->GetSize(), viewSize);
}

TEST_F(DialogTest, TouchMakesEveryControlAtLeast48PixelsHigh) {
    ocpn_fake::touch = true;
    ocpn_fake::scale = 1.0;
    threeRoutes();
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::Route, std::nullopt);
    for (wxWindow* c :
         std::vector<wxWindow*>{newChartTab(), myChartsTab(), view(), draw(), route(), routeChoice(),
                                narrower(), wider(), nameField(), download(), labelled<wxButton>("Close")}) {
        ASSERT_NE(c, nullptr);
        EXPECT_GE(c->GetSize().y, 48) << u(c->GetLabel());
    }
}

// touch mode caps the New chart body by the window manager's decoration, so dialog + decoration fits
// 480; the allowance (40 DIP) holds where the toolkit reports none (no window manager under Xvfb, or GTK
// before the window is mapped); non-touch has no cap.
TEST_F(DialogTest, TouchCapCountsTheDecorationAndAllowsFortyWhereNoneIsReported) {
    ocpn_fake::touch = true;
    ocpn_fake::scale = 1.0;
    threeRoutes();
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::Route, std::nullopt);
    std::vector<wxCollapsiblePane*> panes;
    collect(dialog_, panes);
    ASSERT_FALSE(panes.empty());
    panes[0]->Collapse(false);
    wxCommandEvent changed(wxEVT_COLLAPSIBLEPANE_CHANGED, panes[0]->GetId());
    changed.SetEventObject(panes[0]);
    panes[0]->GetEventHandler()->ProcessEvent(changed);
    ASSERT_EQ(dialog_->GetSize().y - dialog_->GetClientSize().y, 0) << "no window manager here";
    EXPECT_LE(dialog_->GetSize().y, 440) << "no decoration reported: the 40 DIP allowance";
    dialog_->capHeight(90);
    EXPECT_LE(dialog_->GetSize().y, 390);
    dialog_->capHeight(0);
    EXPECT_GT(dialog_->GetSize().y, 440) << "the cap, not the page, held the size";
    EXPECT_LE(dialog_->GetSize().y, 480);
}

// ---- Current view ----

TEST_F(DialogTest, CurrentViewIsCapturedWhenThePageOpensAndOnEachPressNotLive) {
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::View, std::nullopt);
    EXPECT_EQ(reasons(), std::vector<std::string>{kEstimating});
    setViewport(86, 89, 5, 15); // the chart moved north of the Mercator limit while the dialog is open
    EXPECT_EQ(reasons(), std::vector<std::string>{kEstimating}) << "not followed live";
    press(view());
    EXPECT_EQ(reasons(), std::vector<std::string>{kOutside});
    setViewport(45, 55, 5, 15);
    press(view());
    EXPECT_EQ(reasons(), std::vector<std::string>{kEstimating});
}

TEST_F(DialogTest, ViewsWithoutAViewportOrBeyondTheLimitsShowTheirReason) {
    AreaTool fresh([] { return wxColour(); }, [] { return false; }); // no frame drawn yet
    jobs_ = std::make_unique<JobController>(*store_, look_, fresh);
    dialog_ = new MainDialog(frame_, *store_, look_, fresh, *jobs_);
    dialog_->showNewChart(cg::AreaKind::View, std::nullopt);
    EXPECT_EQ(reasons(), std::vector<std::string>{kNoViewport});
    delete dialog_;
    dialog_ = nullptr;
    jobs_.reset();

    makeDialog();
    setViewport(45, 55, 170, 190); // lon_max beyond 180
    dialog_->showNewChart(cg::AreaKind::View, std::nullopt);
    EXPECT_EQ(reasons(), std::vector<std::string>{kAntimeridian});
}

TEST_F(DialogTest, TheViewOfTheFocusCanvasIsTheOneCaptured) {
    wxFrame other(nullptr, wxID_ANY, "second canvas");
    ocpn_fake::canvases = {frame_, &other};
    ocpn_fake::focus = &other; // canvas 1 has no viewport yet
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::View, std::nullopt);
    EXPECT_EQ(reasons(), std::vector<std::string>{kNoViewport});
    ocpn_fake::focus = frame_;
    press(view());
    EXPECT_EQ(reasons(), std::vector<std::string>{kEstimating});
    ocpn_fake::canvases = {frame_};
    ocpn_fake::focus = frame_;
}

// ---- area toggles ----

TEST_F(DialogTest, ExactlyOneAreaToggleShowsPressedAlsoAfterPressingThePressedOne) {
    threeRoutes();
    makeDialog();
    auto state = [&] {
        return std::vector<bool>{view()->GetValue(), draw()->GetValue(), route()->GetValue()};
    };
    dialog_->showNewChart(cg::AreaKind::View, std::nullopt);
    EXPECT_EQ(state(), (std::vector<bool>{true, false, false}));
    press(view());
    EXPECT_EQ(state(), (std::vector<bool>{true, false, false}));
    press(route());
    EXPECT_EQ(state(), (std::vector<bool>{false, false, true}));
    press(route());
    EXPECT_EQ(state(), (std::vector<bool>{false, false, true}));
    press(view());
    EXPECT_EQ(state(), (std::vector<bool>{true, false, false}));
    dialog_->showNewChart(cg::AreaKind::Rectangle, kBox);
    EXPECT_EQ(state(), (std::vector<bool>{false, true, false}));
    press(draw()); // the drawing starts; the kind stays until a box arrives
    EXPECT_EQ(state(), (std::vector<bool>{false, true, false}));
}

TEST_F(DialogTest, DrawOnChartLeavesTheKindUnchangedUntilABoxArrives) {
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::View, std::nullopt);
    press(draw());
    EXPECT_TRUE(area_->drawing());
    EXPECT_TRUE(view()->GetValue());
    EXPECT_FALSE(draw()->GetValue());
    EXPECT_FALSE(outlined());
    dragBox(100, 100, 150, 60);
    EXPECT_TRUE(draw()->GetValue());
    EXPECT_FALSE(view()->GetValue());
}

TEST_F(DialogTest, ARectangleIsOutlinedAndRectangleWithoutABoxIsTheView) {
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::Rectangle, kBox);
    EXPECT_TRUE(draw()->GetValue());
    expectBoxOutline(outline());
    EXPECT_EQ(reasons(), std::vector<std::string>{kEstimating}) << "a rectangle needs no viewport";
    dialog_->showNewChart(cg::AreaKind::View, std::nullopt);
    EXPECT_FALSE(outlined()) << "the view's box is never outlined";
    dialog_->showNewChart(cg::AreaKind::Rectangle, std::nullopt);
    EXPECT_TRUE(view()->GetValue());
    EXPECT_FALSE(draw()->GetValue());
    EXPECT_FALSE(outlined());
}

// ---- Along route ----

TEST_F(DialogTest, RoutesAreListedInListOrderWithAnUnnamedRouteByItsGuid) {
    threeRoutes();
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::Route, std::nullopt);
    ASSERT_NE(routeChoice(), nullptr);
    ASSERT_EQ(routeChoice()->GetCount(), 3u);
    EXPECT_EQ(u(routeChoice()->GetString(0)), "Zulu route");
    EXPECT_EQ(u(routeChoice()->GetString(1)), "(unnamed bbbbbbbb)");
    EXPECT_EQ(u(routeChoice()->GetString(2)), kAlfaName);
    EXPECT_TRUE(route()->IsEnabled());
    EXPECT_FALSE(shows(kNoRoutes));
    EXPECT_TRUE(routeChoice()->IsShown());
}

TEST_F(DialogTest, PreselectionOrderIsSelectedActiveLastFirstEachOnlyWhenListed) {
    struct Row {
        const char *selected, *active, *last;
        int expect;
    };
    const std::vector<Row> rows = {
        {kGuidAlfa, kGuidNone, kGuidZulu, 2}, // selected beats active and last
        {"", kGuidNone, kGuidZulu, 1},        // active beats last
        {kUnlisted, kGuidNone, kGuidZulu, 1}, // an unlisted selected route is skipped
        {"", kUnlisted, kGuidAlfa, 2},        // an unlisted active route is skipped; last
        {"", "", kUnlisted, 0},               // an unlisted last route: the first
        {"", "", "", 0},                      // nothing: the first
        {kGuidNone, "", kGuidAlfa, 1},        // selected beats last
    };
    int n = 0;
    for (const Row& row : rows) {
        SCOPED_TRACE(std::string("row ") + std::to_string(n++));
        ocpn_fake::routes.clear();
        threeRoutes();
        ocpn_fake::selectedRoute = row.selected;
        ocpn_fake::activeRoute = row.active;
        saveLastRoute(row.last);
        makeDialog();
        dialog_->showNewChart(cg::AreaKind::Route, std::nullopt);
        ASSERT_NE(routeChoice(), nullptr);
        EXPECT_EQ(routeChoice()->GetSelection(), row.expect);
        delete dialog_;
        dialog_ = nullptr;
        jobs_.reset();
    }
}

TEST_F(DialogTest, TheUsersRouteChoiceIsTheLastRouteOfTheSession) {
    threeRoutes();
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::Route, std::nullopt);
    EXPECT_EQ(routeChoice()->GetSelection(), 0);
    choose(routeChoice(), 2);
    dialog_->showNewChart(cg::AreaKind::Route, std::nullopt); // nothing selected or active: the last choice
    EXPECT_EQ(routeChoice()->GetSelection(), 2);
}

TEST_F(DialogTest, WithoutRoutesAlongRouteIsDisabledAndARouteRequestFallsBackToTheView) {
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::Route, std::nullopt);
    EXPECT_FALSE(route()->IsEnabled());
    EXPECT_TRUE(shows(kNoRoutes));
    EXPECT_TRUE(view()->GetValue());
    EXPECT_FALSE(route()->GetValue());
    EXPECT_FALSE(routeChoice()->IsShown());
}

TEST_F(DialogTest, ARouteCreatedLaterIsOfferedTheNextTimeThePageOpens) {
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::View, std::nullopt);
    EXPECT_FALSE(route()->IsEnabled());
    addRoute(kGuidZulu, "Zulu route", 3);
    EXPECT_FALSE(route()->IsEnabled()) << "the list is read when the page opens";
    dialog_->showNewChart(cg::AreaKind::View, std::nullopt);
    EXPECT_TRUE(route()->IsEnabled());
    EXPECT_FALSE(shows(kNoRoutes));
    // also through the toolbar's way back: a hidden dialog re-reads the routes
    ocpn_fake::routes.clear();
    dialog_->showNewChart(cg::AreaKind::View, std::nullopt);
    EXPECT_FALSE(route()->IsEnabled());
    click(labelled<wxButton>("Close"));
    addRoute(kGuidZulu, "Zulu route", 3);
    dialog_->present();
    EXPECT_TRUE(route()->IsEnabled());
}

TEST_F(DialogTest, PressingAlongRouteRereadsTheRoutes) {
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::View, std::nullopt);
    addRoute(kGuidZulu, "Zulu route", 3);
    EXPECT_FALSE(route()->IsEnabled());
    press(view()); // re-captures and re-reads
    EXPECT_TRUE(route()->IsEnabled());
    press(route());
    EXPECT_TRUE(route()->GetValue());
    EXPECT_EQ(routeChoice()->GetCount(), 1u);
}

TEST_F(DialogTest, CorridorStepsByHalfANauticalMileWithinTheLimitsAndDisablesTheEndButtons) {
    threeRoutes();
    saveCorridor(1.0);
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::Route, std::nullopt);
    EXPECT_EQ(corridorShown(), "1.0 NM");
    click(wider());
    EXPECT_EQ(corridorShown(), "1.5 NM");
    click(narrower());
    click(narrower());
    EXPECT_EQ(corridorShown(), "0.5 NM");
    EXPECT_FALSE(narrower()->IsEnabled());
    EXPECT_TRUE(wider()->IsEnabled());
    click(wider());
    EXPECT_TRUE(narrower()->IsEnabled());
    for (int i = 0; i < 40; ++i)
        click(wider()); // far more presses than the range holds
    EXPECT_EQ(corridorShown(), "20.0 NM");
    EXPECT_FALSE(wider()->IsEnabled());
    EXPECT_TRUE(narrower()->IsEnabled());
}

TEST_F(DialogTest, TheStoredCorridorIsRoundedToHalfStepsAndAnOutOfRangeOneIsTheDefault) {
    struct Row {
        double stored;
        const char* shown;
    };
    for (const Row& row : {Row{2.0, "2.0 NM"}, Row{3.3, "3.5 NM"}, Row{3.2, "3.0 NM"}, Row{0.1, "2.0 NM"},
                           Row{25, "2.0 NM"}}) {
        SCOPED_TRACE(row.stored);
        ocpn_fake::routes.clear();
        threeRoutes();
        saveCorridor(row.stored);
        makeDialog();
        dialog_->showNewChart(cg::AreaKind::Route, std::nullopt);
        EXPECT_EQ(corridorShown(), row.shown);
        delete dialog_;
        dialog_ = nullptr;
        jobs_.reset();
    }
}

TEST_F(DialogTest, TheRouteRowsAreShownOnlyInRouteMode) {
    threeRoutes();
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::View, std::nullopt);
    EXPECT_FALSE(routeChoice()->IsShown());
    EXPECT_FALSE(narrower()->IsShown());
    EXPECT_EQ(corridorShown(), "<none>");
    press(route());
    EXPECT_TRUE(routeChoice()->IsShown());
    EXPECT_TRUE(narrower()->IsShown());
    EXPECT_TRUE(wider()->IsShown());
    EXPECT_EQ(corridorShown(), "2.0 NM");
}

TEST_F(DialogTest, ARouteOfOnePointShowsTheTooFewPointsReasonAndAChosenGoodRouteClearsIt) {
    addRoute(kGuidZulu, "One point", 1);
    addRoute(kGuidNone, "Two points", 2);
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::Route, std::nullopt);
    EXPECT_EQ(routeChoice()->GetSelection(), 0);
    EXPECT_EQ(reasons(), std::vector<std::string>{kFewPoints});
    EXPECT_FALSE(download()->IsEnabled());
    choose(routeChoice(), 1);
    EXPECT_EQ(reasons(), std::vector<std::string>{kEstimating});
    choose(routeChoice(), 0);
    EXPECT_EQ(reasons(), std::vector<std::string>{kFewPoints});
}

TEST_F(DialogTest, EveryWaypointTheApiReturnsIsDeletedByThePlugin) {
    threeRoutes(); // 3 + 2 + 2 points
    makeDialog();
    EXPECT_EQ(ocpn_fake::waypointsCreated, 7) << "the constructor reads the routes once";
    EXPECT_EQ(ocpn_fake::waypointsDeleted, 7);
    dialog_->showNewChart(cg::AreaKind::Route, std::nullopt);
    press(view());
    press(route());
    EXPECT_GE(ocpn_fake::waypointsCreated, 28) << "four reads of 7 points";
    EXPECT_EQ(ocpn_fake::waypointsDeleted, ocpn_fake::waypointsCreated);
}

// The hyperlink list and links of a linked waypoint are the caller's too (5.10.2 PlugInFromRoutePoint); the
// leak shows under LSan (tools/check.sh --asan).
TEST_F(DialogTest, TheHyperlinksOfALinkedWaypointAreFreedByThePlugin) {
    addRoute(kGuidZulu, "Linked", 3);
    ocpn_fake::routes.back().linksPerPoint = 2;
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::Route, std::nullopt);
    press(view());
    press(route());
    EXPECT_EQ(routeChoice()->GetCount(), 1u);
}

TEST_F(DialogTest, ARouteTheUserPickedSurvivesHidingAndTheToolbarWayBack) {
    threeRoutes();
    ocpn_fake::selectedRoute = kGuidZulu;
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::Route, std::nullopt);
    EXPECT_EQ(routeChoice()->GetSelection(), 0);
    choose(routeChoice(), 2);
    click(labelled<wxButton>("Close"));
    dialog_->present();
    EXPECT_EQ(routeChoice()->GetSelection(), 2) << "OpenCPN's selected route does not win on the way back";
}

TEST_F(DialogTest, TheAutoPreselectedRouteSurvivesTheWayBackButIsNotTheLastChosen) {
    threeRoutes();
    ocpn_fake::selectedRoute = kGuidAlfa;
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::Route, std::nullopt);
    EXPECT_EQ(routeChoice()->GetSelection(), 2);
    click(labelled<wxButton>("Close"));
    ocpn_fake::selectedRoute = kGuidNone;
    dialog_->present();
    EXPECT_EQ(routeChoice()->GetSelection(), 2) << "kept while still listed";
    ocpn_fake::selectedRoute.clear();
    dialog_->showNewChart(cg::AreaKind::Route, std::nullopt);
    EXPECT_EQ(routeChoice()->GetSelection(), 0) << "the auto choice was not stored as the last route";
    ocpn_fake::selectedRoute = kGuidNone;
    click(labelled<wxButton>("Close"));
    dialog_->present();
    EXPECT_EQ(routeChoice()->GetSelection(), 0);
    ocpn_fake::selectedRoute = kGuidAlfa;
    ocpn_fake::routes.erase(ocpn_fake::routes.begin());
    click(labelled<wxButton>("Close"));
    dialog_->present();
    EXPECT_EQ(routeChoice()->GetSelection(), 1) << "a route no longer listed: full preselection";
}

// ---- status line and busy ----

TEST_F(DialogTest, TheFirstFailingInputReasonIsShownAndDownloadStaysDisabled) {
    makeDialog();
    setViewport(86, 89, 5, 15);
    dialog_->showNewChart(cg::AreaKind::View, std::nullopt);
    EXPECT_EQ(reasons(), std::vector<std::string>{kOutside}) << "the area before the name";
    EXPECT_FALSE(download()->IsEnabled());
    setViewport(45, 55, 5, 15);
    press(view());
    EXPECT_EQ(reasons(), std::vector<std::string>{kEstimating});
    EXPECT_FALSE(download()->IsEnabled());
    nameField()->SetValue("   "); // blanks are no name, and the estimate is still first
    EXPECT_EQ(reasons(), std::vector<std::string>{kEstimating});
    waitForEstimate();
    EXPECT_EQ(reasons(), std::vector<std::string>{kEnterName}) << "the estimate is in, the name is blank";
    EXPECT_FALSE(download()->IsEnabled());
    nameField()->SetValue(w("\xD0\x9A\xD0\xB0\xD1\x80\xD1\x82\xD0\xB0 d'x"));
    EXPECT_TRUE(reasons().empty());
    EXPECT_TRUE(download()->IsEnabled()) << "every input is valid and the estimate is in";
    nameField()->SetValue("");
    EXPECT_EQ(reasons(), std::vector<std::string>{kEnterName});
    EXPECT_FALSE(download()->IsEnabled());
}

TEST_F(DialogTest, BusyReplacesDownloadByTheRunningTextAndBack) {
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::View, std::nullopt);
    EXPECT_TRUE(download()->IsShown());
    EXPECT_FALSE(shows(kRunning));
    dialog_->busyChanged(true);
    EXPECT_FALSE(download()->IsShown());
    EXPECT_TRUE(shows(kRunning));
    dialog_->busyChanged(false);
    EXPECT_TRUE(download()->IsShown());
    EXPECT_FALSE(shows(kRunning));
}

// ---- Close, Escape, close box, pages ----

TEST_F(DialogTest, TheCloseButtonHidesTheDialogAndTheOutlineAndPresentBringsThemBack) {
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::Rectangle, kBox);
    ASSERT_TRUE(outlined());
    click(labelled<wxButton>("Close"));
    EXPECT_FALSE(dialog_->IsShown());
    EXPECT_FALSE(outlined());
    dialog_->present();
    EXPECT_TRUE(dialog_->IsShown());
    EXPECT_TRUE(draw()->GetValue()) << "the area the user left";
    expectBoxOutline(outline());
}

TEST_F(DialogTest, EscapeHidesButAnotherKeyIsSkipped) {
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::Rectangle, kBox);
    wxKeyEvent other(wxEVT_CHAR_HOOK);
    other.m_keyCode = 'A';
    other.SetEventObject(dialog_);
    dialog_->GetEventHandler()->ProcessEvent(other);
    EXPECT_TRUE(other.GetSkipped());
    EXPECT_TRUE(dialog_->IsShown());
    EXPECT_TRUE(outlined());
    wxKeyEvent esc(wxEVT_CHAR_HOOK);
    esc.m_keyCode = WXK_ESCAPE;
    esc.SetEventObject(dialog_);
    dialog_->GetEventHandler()->ProcessEvent(esc);
    EXPECT_FALSE(dialog_->IsShown());
    EXPECT_FALSE(outlined());
}

TEST_F(DialogTest, TheCloseBoxHidesEvenWhenForcedAndTheDialogLivesOn) {
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::Rectangle, kBox);
    EXPECT_TRUE(dialog_->Close(true)); // force: CanVeto is false, the handler still only hides
    EXPECT_FALSE(dialog_->IsShown());
    EXPECT_FALSE(outlined());
    EXPECT_FALSE(wxPendingDelete.Find(dialog_)) << "the plugin owns the dialog; nothing may destroy it";
    dialog_->showNewChart(cg::AreaKind::Rectangle, kBox);
    EXPECT_TRUE(dialog_->IsShown());
    EXPECT_TRUE(dialog_->Close());
    EXPECT_FALSE(dialog_->IsShown());
    EXPECT_FALSE(wxPendingDelete.Find(dialog_));
}

TEST_F(DialogTest, MyChartsHidesTheOutlineAndNewChartBringsItBack) {
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::Rectangle, kBox);
    press(myChartsTab());
    EXPECT_TRUE(myChartsTab()->GetValue());
    EXPECT_FALSE(newChartTab()->GetValue());
    EXPECT_FALSE(outlined());
    press(myChartsTab()); // the pressed one stays on
    EXPECT_TRUE(myChartsTab()->GetValue());
    EXPECT_FALSE(newChartTab()->GetValue());
    press(newChartTab());
    EXPECT_TRUE(newChartTab()->GetValue());
    EXPECT_FALSE(myChartsTab()->GetValue());
    expectBoxOutline(outline());
}

TEST_F(DialogTest, ShowMyChartsShowsThatPageAndPresentKeepsItWithoutTheOutline) {
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::Rectangle, kBox);
    dialog_->showMyCharts();
    EXPECT_TRUE(dialog_->IsShown());
    EXPECT_TRUE(myChartsTab()->GetValue());
    EXPECT_FALSE(outlined());
    click(labelled<wxButton>("Close"));
    EXPECT_FALSE(dialog_->IsShown());
    dialog_->present();
    EXPECT_TRUE(dialog_->IsShown());
    EXPECT_TRUE(myChartsTab()->GetValue()) << "the page the user left";
    EXPECT_FALSE(outlined());
}

// ---- present ----

TEST_F(DialogTest, PresentOnAShownDialogDoesNotRecaptureAHiddenOneDoes) {
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::View, std::nullopt);
    setViewport(86, 89, 5, 15);
    dialog_->present();
    EXPECT_EQ(reasons(), std::vector<std::string>{kEstimating}) << "shown: only raised";
    click(labelled<wxButton>("Close"));
    dialog_->present();
    EXPECT_TRUE(dialog_->IsShown());
    EXPECT_EQ(reasons(), std::vector<std::string>{kOutside}) << "hidden: the view is captured again";
}

TEST_F(DialogTest, ARectangleDrawnInThisSessionSurvivesCloseAndPresent) {
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::View, std::nullopt);
    press(draw());
    dragBox(100, 100, 150, 60);
    ASSERT_TRUE(dialog_->IsShown());
    click(labelled<wxButton>("Close"));
    dialog_->present();
    EXPECT_TRUE(draw()->GetValue());
    expectBoxOutline(outline());
}

// ---- drawing ----

TEST_F(DialogTest, DrawingHidesTheDialogShowsTheHintWithCancelAtTheTopLeftAndAFinishedBoxOpensThePage) {
    makeDialog();
    EXPECT_EQ(hintStrip(), nullptr) << "created on the first drawArea()";
    dialog_->showNewChart(cg::AreaKind::View, std::nullopt);
    press(myChartsTab());
    dialog_->drawArea();
    EXPECT_FALSE(dialog_->IsShown());
    ASSERT_NE(hintStrip(), nullptr);
    EXPECT_TRUE(hintStrip()->IsShown());
    EXPECT_EQ(hintText(), kHint);
    EXPECT_NE(labelled<wxButton>("Cancel", hintStrip()), nullptr);
    EXPECT_TRUE(area_->drawing());
    EXPECT_EQ(hintStrip()->GetParent(), frame_);
    EXPECT_EQ(hintStrip()->GetPosition(), wxPoint(8, 8))
        << "canvas top-left (8, 8) DIP, not the bottom corner";

    dragBox(100, 100, 150, 60);
    EXPECT_FALSE(area_->drawing());
    EXPECT_FALSE(hintStrip()->IsShown());
    EXPECT_FALSE(static_cast<bool>(area_->onAreaError));
    EXPECT_TRUE(dialog_->IsShown());
    EXPECT_TRUE(newChartTab()->GetValue()) << "the New chart page, also from My charts";
    EXPECT_TRUE(draw()->GetValue());
    expectBoxOutline(outline());
}

TEST_F(DialogTest, AnAreaAcrossTheAntimeridianShowsItsTextAboveTheHintAndDrawingContinues) {
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::View, std::nullopt);
    dialog_->drawArea();
    dragBox(100, 100, 1900, 60); // lon 10.. 190
    EXPECT_TRUE(area_->drawing());
    EXPECT_FALSE(dialog_->IsShown());
    EXPECT_EQ(hintText(), std::string(kAntimeridian) + "\n" + kHint);
    EXPECT_NE(labelled<wxButton>("Cancel", hintStrip()), nullptr);
    dragBox(100, 100, 150, 60); // a good one ends it
    EXPECT_FALSE(area_->drawing());
    EXPECT_TRUE(dialog_->IsShown());
    EXPECT_TRUE(draw()->GetValue());
}

TEST_F(DialogTest, CancelAndRightClickReturnToThePageWithItsPreviousAreaNotRecaptured) {
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::Rectangle, kBox);
    dialog_->drawArea();
    EXPECT_FALSE(outlined()) << "the tool hides the outline while drawing";
    setViewport(86, 89, 5, 15); // would be a reason if the page re-captured a view
    click(labelled<wxButton>("Cancel", hintStrip()));
    EXPECT_FALSE(dialog_->IsShown()) << "the button works through CallAfter";
    pump();
    EXPECT_FALSE(area_->drawing());
    EXPECT_TRUE(dialog_->IsShown());
    EXPECT_FALSE(hintStrip()->IsShown());
    EXPECT_TRUE(draw()->GetValue());
    expectBoxOutline(outline());
    EXPECT_EQ(reasons(), std::vector<std::string>{kEstimating});

    // right-click, from a page showing the View: still the View, not re-captured
    dialog_->showNewChart(cg::AreaKind::View, std::nullopt);
    EXPECT_EQ(reasons(), std::vector<std::string>{kOutside});
    setViewport(45, 55, 5, 15);
    press(myChartsTab());
    dialog_->drawArea();
    EXPECT_TRUE(mouse(wxEVT_RIGHT_DOWN, 100, 100));
    EXPECT_FALSE(area_->drawing());
    EXPECT_TRUE(dialog_->IsShown());
    EXPECT_TRUE(newChartTab()->GetValue()) << "the page New chart, also from My charts";
    EXPECT_FALSE(myChartsTab()->GetValue());
    EXPECT_TRUE(view()->GetValue());
    EXPECT_EQ(reasons(), std::vector<std::string>{kOutside}) << "the page keeps what it had";
    EXPECT_FALSE(outlined());
}

TEST_F(DialogTest, ADrawingIsAbandonedWithoutItsDoneByShowNewChartDrawAreaAndTheDestructor) {
    makeDialog();
    int calls = 0;
    auto startForeign = [&] { area_->startDrawing([&](std::optional<cg::BBox>) { ++calls; }); };

    startForeign();
    dialog_->showNewChart(cg::AreaKind::View, std::nullopt);
    EXPECT_FALSE(area_->drawing());
    EXPECT_EQ(calls, 0);
    EXPECT_TRUE(dialog_->IsShown());

    startForeign();
    dialog_->drawArea(); // replaces the drawing, then runs its own
    EXPECT_EQ(calls, 0);
    EXPECT_TRUE(area_->drawing());
    EXPECT_FALSE(dialog_->IsShown());

    delete dialog_; // a drawing of the dialog's own: abandoned
    dialog_ = nullptr;
    EXPECT_FALSE(area_->drawing());
    EXPECT_FALSE(static_cast<bool>(area_->onAreaError));
    jobs_.reset();

    makeDialog();
    startForeign();
    delete dialog_;
    dialog_ = nullptr;
    EXPECT_FALSE(area_->drawing());
    EXPECT_EQ(calls, 0);
}

TEST_F(DialogTest, PresentWhileDrawingCancelsItAndItsDoneShowsTheDialog) {
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::Rectangle, kBox);
    dialog_->drawArea();
    ASSERT_FALSE(dialog_->IsShown());
    dialog_->present();
    EXPECT_FALSE(area_->drawing());
    EXPECT_TRUE(dialog_->IsShown());
    EXPECT_TRUE(draw()->GetValue());
    expectBoxOutline(outline());

    int calls = 0; // a drawing that is not the dialog's: present still cancels it, done(nullopt) once
    area_->startDrawing([&](std::optional<cg::BBox> b) { calls += b ? 100 : 1; });
    dialog_->present();
    EXPECT_EQ(calls, 1);
    EXPECT_FALSE(area_->drawing());
}

// ---- destruction ----

TEST_F(DialogTest, DestroyingAShownDialogClearsTheOutlineAndLaterLookChangesAreHarmless) {
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::Rectangle, kBox);
    ASSERT_TRUE(outlined());
    delete dialog_;
    dialog_ = nullptr;
    EXPECT_FALSE(outlined());
    look_.setScheme(PI_GLOBAL_COLOR_SCHEME_NIGHT); // a leaked subscription would touch the dead windows
}

TEST_F(DialogTest, DestroyingWhileDrawingWithACancelClickPendingLeavesNoDoneAndNoStrip) {
    threeRoutes();
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::Route, std::nullopt);
    dialog_->drawArea();
    ASSERT_NE(hintStrip(), nullptr);
    click(labelled<wxButton>("Cancel", hintStrip())); // its CallAfter is still queued
    delete dialog_;
    dialog_ = nullptr;
    pump(); // the queued call must be gone with the strip
    EXPECT_FALSE(area_->drawing());
    EXPECT_EQ(hintStrip(), nullptr);
    EXPECT_EQ(ocpn_fake::waypointsDeleted, ocpn_fake::waypointsCreated);
}

// the drawing hint is in a floating frame of the canvas window by default (no key), a child of the
// canvas with StripOnChart=1; the dialog's destructor takes the frame with it at once.
TEST_F(DialogTest, TheHintIsInAFrameByDefaultAndAChildOfTheCanvasWhenTheOptionIsOn) {
    for (const bool onChart : {false, true}) {
        cg::Settings s = store_->loadSettings();
        s.stripOnChart = onChart;
        store_->saveSettings(s);
        makeDialog();
        dialog_->drawArea();
        std::vector<ProgressStrip*> found;
        collect(frame_, found);
        ASSERT_EQ(found.size(), 1u);
        ProgressStrip* hint = found.front();
        if (onChart) {
            EXPECT_EQ(hint->GetParent(), frame_);
        } else {
            auto* fr = wxDynamicCast(hint->GetParent(), wxFrame);
            ASSERT_NE(fr, nullptr);
            EXPECT_EQ(fr->GetParent(), frame_);
            EXPECT_NE(0, fr->GetWindowStyleFlag() & wxFRAME_FLOAT_ON_PARENT);
            EXPECT_NE(0, fr->GetWindowStyleFlag() & wxFRAME_NO_TASKBAR);
            EXPECT_TRUE(fr->IsShown());
            EXPECT_TRUE(hint->IsShownOnScreen());
            EXPECT_EQ(frame_->ClientToScreen(wxPoint(8, 8)), fr->GetPosition());
            dragBox(100, 100, 150, 60);
            EXPECT_FALSE(fr->IsShown()) << "the owner's Hide() hides the frame";
            dialog_->drawArea();
            EXPECT_TRUE(fr->IsShown());
        }
        wxWeakRef<wxWindow> weak(onChart ? static_cast<wxWindow*>(hint) : hint->GetParent());
        delete dialog_;
        dialog_ = nullptr;
        jobs_.reset();
        EXPECT_FALSE(weak) << "gone at once, before any idle event";
    }
}

// OpenCPN deleting the canvas (and the hint strip with it) before the plugin goes: the weak
// reference is null and destroying the dialog crashes nothing (run under --asan).
TEST_F(DialogTest, TheCanvasDeletedBeforeTheDialogTakesTheHintStripWithIt) {
    auto* canvas = new wxFrame(nullptr, wxID_ANY, "other canvas", wxPoint(50, 60), wxSize(1600, 480));
    ocpn_fake::canvasWindow = canvas;
    makeDialog();
    dialog_->drawArea();
    std::vector<ProgressStrip*> found;
    collect(canvas, found);
    ASSERT_EQ(found.size(), 1u);
    wxWeakRef<ProgressStrip> weak(found.front());
    ASSERT_TRUE(weak);
    ocpn_fake::canvasWindow = nullptr;
    delete canvas;
    EXPECT_FALSE(weak);
    delete dialog_;
    dialog_ = nullptr;
    jobs_.reset();
    ocpn_fake::canvasWindow = frame_;
}

} // namespace
