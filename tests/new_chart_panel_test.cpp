// SPDX-License-Identifier: GPL-3.0-or-later
// the chart and detail rows of the New chart page :
// the sources run, the chart choice from the curated table, projection refusals, the seamarks checkbox, the
// imagery line, the four bands with their scale labels, the Harbour rule, the Advanced zooms and the status
// reasons source / zooms. The tile program is a stand-in script (tests/sources_stub.h) that prints
// `source` records (the real tsv is not used). Expected texts are
// literals, not the functions that produce them.
// The page is reached through a real MainDialog with the shared OpenCPN fakes, as in main_dialog_test.cpp.
#include "area_tool.h"
#include "config_store.h"
#include "job_controller.h"
#include "look.h"
#include "main_dialog.h"
#include "new_chart_panel.h"
#include "ocpn_fakes.h"
#include "question_probe.h"
#include "settings_dialog.h"
#include "sources_dialog.h"
#include "sources_stub.h"
#include "wx_env.h"

#include <gtest/gtest.h>

#include <wx/app.h>
#include <wx/apptrait.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/collpane.h>
#include <wx/evtloop.h>
#include <wx/fileconf.h>
#include <wx/frame.h>
#include <wx/listctrl.h>
#include <wx/log.h>
#include <wx/simplebook.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/tglbtn.h>
#include <wx/timer.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <ocpn_plugin.h>

namespace {

namespace fs = std::filesystem;

// ---- the curated table of (GUIDs as the CLI prints them) and the names of a typical map set
const char* const kEsri = "{7B743985-BC5F-4AB6-8915-AC5DBBB8F552}";
const char* const kGoogle = "{F6574B06-E632-4D5F-BC75-C8FA658B57DF}";
const char* const kBing = "{9A360A54-7A70-402D-8A11-D570BD759B5B}";
const char* const kNavionics = "{A830C100-49C8-4623-957A-60B85C39FBE0}";
const char* const kMapnik = "{D643C5A1-4331-4387-9BE2-DDDF185C117E}";
const char* const kMapnikLower = "{d643c5a1-4331-4387-9be2-dddf185c117e}";
const char* const kSentinel = "{92122135-6B15-4800-AA9E-3E48C556DA17}"; // bundled sources
const char* const kUsgs = "{E05EDD5E-C6C3-483C-8BA7-8DE1D9892ED8}";
const char* const kTopo = "{51C1079B-B55D-4DF7-A410-ADC118F53AB1}";
const char* const kEmodWorld = "{C44877A0-D455-4238-B5E2-C8BFF25FC0D4}";
const char* const kEmodEurope = "{91D4F1B4-A7CD-4FC8-95F1-637609068FB2}";
const char* const kSeamarkLayer = "{AB8A8685-AEC6-4E16-80DA-DC4AC37E4777}";
const char* const kNameEsri = "ESRI ArcGIS.Imagery";
const char* const kNameGoogle = "Google - Satellite";
const char* const kNameBing = "Bing - Satellite";
const char* const kNameNavionics = "Navionics Marine Charts";
const char* const kNameMapnik = "OSM OpenStreetMap.org - MAPNIK";
const char* const kNameSentinel = "Sentinel-2 Cloudless 2025 (EOX)";
const char* const kNameUsgs = "USGS Imagery (USA)";
const char* const kNameTopo = "OpenTopoMap";
const char* const kNameEmodWorld = "EMODnet Bathymetry (world)";
const char* const kNameEmodEurope = "EMODnet Bathymetry (European seas, depth shading)";
const char* const kNameSeamark = "OSM OpenSeaMap";
// Not curated.
const char* const kDelta = "{11111111-AAAA-4AAA-8AAA-000000000001}";
const char* const kBeta = "{22222222-BBBB-4BBB-8BBB-000000000002}";
const char* const kAlfa = "{33333333-CCCC-4CCC-8CCC-000000000003}";
const char* const kYandex = "{5E04039A-331E-4D2C-B051-B00F8871BB39}";     // Yandex - Satellite, EPSG:3395
const char* const kNameCyrillic = "\xD0\x90\xD0\xBB\xD1\x84\xD0\xB0 d'x"; // "Алфа d'x"

// The `verdict` of a source the CLI refuses:.
const char* const kRefusedVerdict =
    "WebP tiles are not shown by every OpenCPN version; pass --allow-webp to export them anyway";

// ---- texts of the spec
const char* const kLoading = "Loading chart sources..."; // 7.2
const char* const kNotFound =
    "Chart Grabber CLI was not found. Install the chart-grabber-cli package, or choose "
    "the program under Advanced in Preferences."; // 7.2 / 8.2
const char* const kTooOld = "Chart Grabber CLI is too old. Install the current chart-grabber-cli package.";
const char* const kNoSources = "No usable chart source was found. Reinstall Chart Grabber, or check the "
                               "files in your chart sources folder.";
const char* const kNoUsable = "No usable chart source. Use More sources... or Preferences.";
const char* const kSeamarksMissing = "The seamarks source (OpenSeaMap) is not among your chart sources";
const char* const kChooseDetail = "Choose the detail";
const char* const kHarbourTooLarge = "Too many tiles for this area - draw a smaller area for harbour detail";
const char* const kImagery =
    "Satellite images can be 40-170 m off the chart: background only, not for navigation near dangers.";
const char* const kTermsNote = "The owner of this chart source may not allow saving its tiles for offline "
                               "use. Check its terms of use before you "
                               "download.";
const char* const kNavNote =
    "Not for navigation. This source is not an official nautical chart; do not use it to navigate.";
const char* const kEnterName = "Enter a chart name";
const char* const kEstimating = "Working out the size..."; // 7.4 / 7.5
const char* const kOutside = "The area is too far north or south (beyond 85 degrees).";
const char* const kProjection3395 = "Cannot be made into an OpenCPN chart (projection EPSG:3395)";

// Scale labels; messages_test.cpp recomputes them): equator and 60 degrees.
const char* const kScalesEquator[4] = {"1:2.3M-1:290k", "1:140k-1:36k", "1:18k-1:9.0k", "1:4.5k-1:2.3k"};
const char* const kScales60[4] = {"1:1.2M-1:140k", "1:72k-1:18k", "1:9.0k-1:4.5k", "1:2.3k-1:1.1k"};
const char* const kBandNames[4] = {"Passage", "Coastal", "Approach", "Harbour"};

using stub::Rec;
Rec map(const char* guid, const char* name, int epsg = 3857, const char* verdict = "ok",
        const char* group = "") {
    return {guid, "map", name, epsg, verdict, group};
}
Rec layer(const char* guid, const char* name, int epsg = 3857, const char* verdict = "ok") {
    return {guid, "layer", name, epsg, verdict, ""};
}
// Every curated base and the seamark layer, in an order that is not the order of the table.
std::vector<Rec> allCurated() {
    return {map(kMapnik, kNameMapnik),       layer(kSeamarkLayer, kNameSeamark), map(kBing, kNameBing),
            map(kNavionics, kNameNavionics), map(kGoogle, kNameGoogle),          map(kEsri, kNameEsri)};
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

class EnvGuard {
public:
    explicit EnvGuard(const char* name) : name_(name) {
        if (const char* v = std::getenv(name)) {
            old_ = v;
            had_ = true;
        }
    }
    ~EnvGuard() {
        if (had_)
            setenv(name_, old_.c_str(), 1);
        else
            unsetenv(name_);
    }

private:
    const char* name_;
    std::string old_;
    bool had_ = false;
};

class CollectLog : public wxLog {
public:
    std::vector<std::string> messages;

protected:
    void DoLogRecord(wxLogLevel, const wxString& msg, const wxLogRecordInfo&) override {
        messages.push_back(msg.utf8_string());
    }
};

class PanelTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!wxTheApp || !wxTheApp->IsGUI())
            GTEST_SKIP() << "no display: the New chart page tests were NOT run";
        loop_.reset(wxTheApp->GetTraits()->CreateEventLoop());
        ASSERT_NE(loop_, nullptr);
        activator_ = std::make_unique<wxEventLoopActivator>(loop_.get());
        char tmpl[] = "/tmp/cg-panel-XXXXXX";
        ASSERT_NE(mkdtemp(tmpl), nullptr);
        root_ = tmpl;
        ocpn_fake::privateData = wxString::FromUTF8((root_ / "private").string());
        oldHome_ = getenv("HOME") ? getenv("HOME") : "";
        // The default maps folder is under HOME. Not inside root_: GTK's icon loader (a bwrap sandbox) keeps
        // HOME in its command line and outlives the test, which expectNoProcessesUnder(root_) would report.
        home_ = (root_.parent_path() / ("home-of-" + root_.filename().string())).string();
        setenv("HOME", home_.c_str(), 1);
        ocpn_fake::touch = false;
        ocpn_fake::scale = 1.0;
        ocpn_fake::contentScale = 1.0;
        ocpn_fake::routes.clear();
        ocpn_fake::selectedRoute.clear();
        ocpn_fake::activeRoute.clear();
        ocpn_fake::waypointsCreated = ocpn_fake::waypointsDeleted = 0;
        ocpn_fake::underMouse = 0;
        ocpn_fake::refreshed.clear();
        config_ = std::make_unique<wxFileConfig>(wxString("cgpaneltest"), wxEmptyString,
                                                 wxString::FromUTF8((root_ / "opencpn.conf").string()),
                                                 wxEmptyString, wxCONFIG_USE_LOCAL_FILE);
        store_ = std::make_unique<ConfigStore>(config_.get());
        sources_ = std::make_unique<stub::Sources>(root_);
        sources_->records(allCurated());
        setSettings([&](cg::Settings& s) { s.chartDir = (root_ / "charts d'x").string(); });
        frame_ = new wxFrame(nullptr, wxID_ANY, "canvas", wxPoint(50, 60), wxSize(800, 480));
        ocpn_fake::canvasWindow = frame_;
        ocpn_fake::canvases = {frame_};
        ocpn_fake::focus = frame_;
        area_ = std::make_unique<AreaTool>([] { return wxColour(10, 200, 30); }, [] { return false; });
        setViewport(45, 55, 5, 15);
    }
    void TearDown() override {
        if (!frame_)
            return;
        if (sources_) {
            sources_->hold(false);
            sources_->holdEstimate(false);
            sources_->holdRun(false);
        }
        delete dialog_;
        dialog_ = nullptr;
        jobs_.reset();
        area_.reset();
        ocpn_fake::canvasWindow = nullptr;
        ocpn_fake::canvases.clear();
        ocpn_fake::focus = nullptr;
        frame_->Destroy();
        wxTheApp->ProcessPendingEvents();
        wxTheApp->ProcessIdle();
        store_.reset();
        config_.reset();
        sources_.reset();
        activator_.reset();
        loop_.reset();
        setenv("HOME", oldHome_.c_str(), 1);
        std::error_code homeEc;
        expectNoProcessesUnder(root_.string());
        fs::remove_all(home_, homeEc);
        fs::remove_all(root_);
    }

    // The arguments of a sources run with empty MapsDir / CacheDir, ).
    std::string defaultSourcesArgv(const std::string& format = " --format=tsv") const {
        return "sources" + format + " --cache " + (root_ / "private").string() +
               "/plugins/chartgrabber/cache";
    }

    // ---- fixture
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
    void setSettings(const std::function<void(cg::Settings&)>& edit) {
        cg::Settings s = store_->loadSettings();
        s.cliPath = sources_->cli();
        edit(s);
        store_->saveSettings(s);
    }
    void saveLast(const std::function<void(ConfigStore::LastChoice&)>& edit) {
        ConfigStore::LastChoice c = store_->loadLastChoice();
        edit(c);
        store_->saveLastChoice(c);
    }
    void addRoute(const std::string& guid, const std::string& name,
                  std::vector<std::pair<double, double>> points) {
        ocpn_fake::Route r;
        r.guid = wxString::FromUTF8(guid);
        r.name = wxString::FromUTF8(name);
        r.points = std::move(points);
        ocpn_fake::routes.push_back(r);
    }
    // Creates the dialog and the page; with `wait` the sources run is waited for.
    void makeDialog(bool wait = true) {
        jobs_ = std::make_unique<JobController>(*store_, look_, *area_);
        dialog_ = new MainDialog(frame_, *store_, look_, *area_, *jobs_);
        // The relays of chartgrabber_pi.cpp:61-69 (the plugin object itself is not under test).
        jobs_->onBusyChanged = [this](bool busy) {
            if (dialog_)
                dialog_->busyChanged(busy);
        };
        dialog_->showNewChart(cg::AreaKind::View, std::nullopt);
        if (wait)
            waitLoaded();
    }
    void destroyDialog() {
        delete dialog_;
        dialog_ = nullptr;
        jobs_.reset();
    }
    bool runUntil(const std::function<bool()>& done, int timeoutMs = 15000) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (!done()) {
            if (std::chrono::steady_clock::now() >= deadline)
                return false;
            loop_->DispatchTimeout(20);
        }
        return true;
    }
    // The dialogs the page opens are modeless: the click returns at once; a driver (a timer in the
    // test's loop) answers the window, and its `found` says it did.
    template <class Driver> void clickUntilFound(wxButton* b, Driver& driver) {
        click(b);
        ASSERT_TRUE(runUntil([&] { return driver.found; }));
    }
    bool loading() { return chart()->GetCount() == 1 && chart()->GetString(0) == kLoading; }
    void waitLoaded() {
        ASSERT_TRUE(runUntil([&] { return !loading(); })) << "the sources run did not end";
    }
    // the estimate of the current inputs has ended (the status no longer says "Working out the
    // size...").
    void waitEstimate() {
        ASSERT_TRUE(runUntil([&] { return !shows(kEstimating); })) << "the estimate did not end";
    }
    void waitEstimateRuns(std::size_t n) {
        ASSERT_TRUE(runUntil([&] { return sources_->estimateArgv().size() >= n; })) << "no estimate run";
    }
    void waitRuns(std::size_t n) {
        ASSERT_TRUE(runUntil([&] { return sources_->argv().size() >= n; }))
            << "the stand-in was not run: " << sources_->argv().size() << " runs, wanted " << n;
    }

    void waitSourcesDialogGone() {
        ASSERT_TRUE(runUntil([&] {
            for (auto* n = wxTopLevelWindows.GetFirst(); n; n = n->GetNext())
                if (dynamic_cast<SourcesDialog*>(n->GetData()))
                    return false;
            return true;
        })) << "the closed More sources window was not destroyed";
    }

    // ---- window access
    template <class T> T* labelled(const char* label) {
        return dynamic_cast<T*>(wxWindow::FindWindowByLabel(label, dialog_));
    }
    template <class T> std::vector<T*> all() {
        std::vector<T*> v;
        collect(dialog_, v);
        return v;
    }
    NewChartPanel* panel() { return all<NewChartPanel>().front(); }
    wxChoice* chart() { return all<wxChoice>().at(1); } // the route choice is created first
    wxChoice* routeChoice() { return all<wxChoice>().at(0); }
    wxTextCtrl* nameField() { return all<wxTextCtrl>().at(0); }
    wxChoice* least() { return dynamic_cast<wxChoice*>(wxWindow::FindWindowByName("leastDetail", dialog_)); }
    wxChoice* most() { return dynamic_cast<wxChoice*>(wxWindow::FindWindowByName("mostDetail", dialog_)); }
    // The user sets the range lo..hi through the two drop-downs (Least first, then Most).
    void pickZooms(int lo, int hi) {
        choose(least(), lo);
        choose(most(), hi);
    }
    // The shown detail note ("Detail set under Advanced: ..."), else "".
    std::string rangeNote() {
        for (wxStaticText* s : all<wxStaticText>())
            if (s->IsShown() && u(s->GetLabel()).rfind("Detail set under Advanced", 0) == 0)
                return u(s->GetLabel());
        return "";
    }
    // The shown cost line of the main page ("One more level..."), else "".
    std::string nextLevelLine() {
        for (wxStaticText* s : all<wxStaticText>())
            if (s->IsShown() && u(s->GetLabel()).rfind("One more level", 0) == 0)
                return u(s->GetLabel());
        return "";
    }
    wxCheckBox* seamarks() { return labelled<wxCheckBox>("Seamarks on top (OpenSeaMap)"); }
    wxCheckBox* band(int i) { return labelled<wxCheckBox>(kBandNames[i]); }
    wxButton* preferences() { return labelled<wxButton>("Preferences..."); }
    wxButton* moreSources() { return labelled<wxButton>("More sources..."); }
    wxButton* wider() { return labelled<wxButton>("+"); }
    wxButton* narrower() { return labelled<wxButton>("-"); }
    wxToggleButton* view() { return labelled<wxToggleButton>("Current view"); }
    wxToggleButton* route() { return labelled<wxToggleButton>("Along route"); }
    static wxString
    flat(wxStaticText* s) { // a long text is wrapped at spaces; the words are the same
        wxString l = s->GetLabel();
        l.Replace("\n", " ");
        return l;
    }
    wxStaticText* text(const char* t) { // the shown static text with exactly this label, or null
        for (wxStaticText* s : all<wxStaticText>())
            if (s->IsShown() && flat(s) == t)
                return s;
        return nullptr;
    }
    bool shows(const char* t) { return text(t) != nullptr; }
    // The estimate line : the shown static text that starts with "About ", else "".
    std::string estimateLine() {
        for (wxStaticText* s : all<wxStaticText>())
            if (s->IsShown() && u(flat(s)).rfind("About ", 0) == 0)
                return u(flat(s));
        return "";
    }
    std::vector<std::string> entries() {
        std::vector<std::string> out;
        for (unsigned i = 0; i < chart()->GetCount(); ++i)
            out.push_back(u(chart()->GetString(i)));
        return out;
    }
    std::string selected() {
        return chart()->GetSelection() < 0 ? "<none>" : u(chart()->GetStringSelection());
    }
    std::vector<bool> ticks() {
        return {band(0)->GetValue(), band(1)->GetValue(), band(2)->GetValue(), band(3)->GetValue()};
    }
    // The first known reason on the status line (the status is the one line the reasons share).
    std::string status() {
        for (const char* r : {kOutside, kLoading, kNotFound, kTooOld, kNoSources, kNoUsable, kChooseDetail,
                              kEstimating, kEnterName})
            if (shows(r))
                return r;
        for (wxStaticText* s : all<wxStaticText>())
            if (s->IsShown() && u(s->GetLabel()).rfind("Chart Grabber CLI does not work", 0) == 0)
                return u(s->GetLabel());
        return "";
    }

    // ---- user actions
    static void choose(wxChoice* c, int i) {
        c->SetSelection(i);
        wxCommandEvent e(wxEVT_CHOICE, c->GetId());
        e.SetEventObject(c);
        e.SetInt(i);
        c->GetEventHandler()->ProcessEvent(e);
    }
    static void tick(wxCheckBox* b) { // what a click does
        b->SetValue(!b->GetValue());
        wxCommandEvent e(wxEVT_CHECKBOX, b->GetId());
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
    static void press(wxToggleButton* b) {
        b->SetValue(!b->GetValue());
        wxCommandEvent e(wxEVT_TOGGLEBUTTON, b->GetId());
        e.SetEventObject(b);
        e.SetInt(b->GetValue());
        b->GetEventHandler()->ProcessEvent(e);
    }
    static void typeInto(wxTextCtrl* t, const std::string& s) {
        t->ChangeValue(wxString::FromUTF8(s));
        wxCommandEvent e(wxEVT_TEXT, t->GetId());
        e.SetEventObject(t);
        e.SetString(t->GetValue());
        t->GetEventHandler()->ProcessEvent(e);
    }

    fs::path root_;
    std::string oldHome_, home_;
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

// ---- the chart choice ----

TEST_F(PanelTest, TheChoiceListsTheCuratedMapsPresentInTableOrderAndNoOtherMap) {
    // Records are unsorted; Navionics and Bing are absent from the user's maps; Mapnik's GUID is lower case.
    sources_->records({map(kMapnikLower, kNameMapnik), map(kDelta, "Delta"),
                       layer(kSeamarkLayer, kNameSeamark), map(kGoogle, kNameGoogle),
                       map(kAlfa, kNameCyrillic), map(kEsri, kNameEsri)});
    makeDialog();
    EXPECT_EQ(entries(), (std::vector<std::string>{kNameEsri, kNameGoogle, kNameMapnik}));
    EXPECT_EQ(selected(), kNameEsri);
    EXPECT_TRUE(chart()->IsEnabled());
    EXPECT_TRUE(moreSources()->IsEnabled());
    EXPECT_FALSE(shows(kLoading));
    EXPECT_FALSE(preferences()->IsShown());
    EXPECT_EQ(sources_->argv(), std::vector<std::string>{defaultSourcesArgv()});
}

TEST_F(PanelTest, LastSourceAndTheFallbackFollowSpec72) {
    struct Row {
        const char* name;
        std::vector<Rec> records;
        const char* last;
        std::vector<std::string> expected;
        std::string selected;
    };
    const std::string refusedGroupA = "A";
    const std::vector<Row> rows = {
        {"a usable non-curated LastSource is listed after the curated ones and preselected",
         {map(kMapnik, kNameMapnik), map(kEsri, kNameEsri), map(kDelta, kNameCyrillic)},
         kDelta,
         {kNameEsri, kNameMapnik, kNameCyrillic},
         kNameCyrillic},
        {"a curated LastSource is preselected",
         {map(kMapnik, kNameMapnik), map(kEsri, kNameEsri), map(kDelta, "Delta")},
         kMapnik,
         {kNameEsri, kNameMapnik},
         kNameMapnik},
        {"an absent LastSource is not listed",
         {map(kMapnik, kNameMapnik), map(kEsri, kNameEsri)},
         kDelta,
         {kNameEsri, kNameMapnik},
         kNameEsri},
        {"a refused (EPSG 3395) LastSource is not listed, the first curated one is preselected",
         {map(kYandex, "Yandex - Satellite", 3395), map(kMapnik, kNameMapnik), map(kEsri, kNameEsri)},
         kYandex,
         {kNameEsri, kNameMapnik},
         kNameEsri},
        {"a curated map with EPSG 4326 is not listed",
         {map(kGoogle, kNameGoogle, 4326), map(kEsri, kNameEsri)},
         "",
         {kNameEsri},
         kNameEsri},
        {"a curated map the CLI refuses (verdict) is not listed",
         {map(kNavionics, kNameNavionics, 3857, kRefusedVerdict), map(kMapnik, kNameMapnik)},
         "",
         {kNameMapnik},
         kNameMapnik},
        {"no curated map: the first usable map in sourcesDialogOrder order (group A, usable first)",
         {map(kYandex, "Refused", 3395, "ok", "A"), map(kBeta, "Beta", 3857, "ok", "B"),
          map(kAlfa, kNameCyrillic, 3857, "ok", "A")},
         "",
         {kNameCyrillic},
         kNameCyrillic},
        {"a LastSource that is not curated stops the fallback",
         {map(kBeta, "Beta"), map(kDelta, "Delta")},
         kDelta,
         {"Delta"},
         "Delta"},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        sources_->records(row.records);
        saveLast([&](ConfigStore::LastChoice& c) { c.sourceGuid = row.last; });
        makeDialog();
        EXPECT_EQ(entries(), row.expected);
        EXPECT_EQ(selected(), row.selected);
        EXPECT_TRUE(chart()->IsEnabled());
        destroyDialog();
    }
}

TEST_F(PanelTest, WithNoUsableMapTheStatusSaysSoWithoutAPreferencesButton) {
    sources_->records(
        {map(kEsri, kNameEsri, 3395), layer(kBeta, "Only a layer"), map(kGoogle, kNameGoogle, 3857, "no")});
    makeDialog();
    EXPECT_EQ(status(), kNoUsable);
    EXPECT_EQ(chart()->GetCount(), 0u);
    EXPECT_FALSE(chart()->IsEnabled());
    EXPECT_FALSE(preferences()->IsShown()) << "the sources run itself worked";
    EXPECT_TRUE(moreSources()->IsEnabled());
}

// ---- the sources run: loading, failures, Preferences, reload ----

TEST_F(PanelTest, WhileTheRunLastsTheChoiceSaysLoadingAndIsDisabled) {
    sources_->hold(true);
    makeDialog(false);
    EXPECT_EQ(entries(), std::vector<std::string>{kLoading});
    EXPECT_FALSE(chart()->IsEnabled());
    EXPECT_EQ(status(), kLoading);
    EXPECT_FALSE(moreSources()->IsEnabled());
    EXPECT_FALSE(seamarks()->IsEnabled());
    EXPECT_TRUE(seamarks()->GetValue()) << "keeps its value (LastSeamarks default on)";
    EXPECT_FALSE(preferences()->IsShown());
    waitRuns(1);
    EXPECT_TRUE(loading()) << "still running";
    sources_->hold(false);
    waitLoaded();
    EXPECT_TRUE(chart()->IsEnabled());
    EXPECT_EQ(status(), kEstimating) << "the estimate comes before the name";
    EXPECT_TRUE(seamarks()->IsEnabled());
}

TEST_F(PanelTest, EveryFailureOfTheRunShowsItsTextAndThePreferencesButton) {
    struct Row {
        const char* name;
        int exitCode;
        std::string stderrText, records, expected;
    };
    const std::string oneSource = "source\t" + std::string(kEsri) + "\tmap\t" + kNameEsri +
                                  "\timage/jpeg\t3857\t0\tok\tfile.zmp\t\n" + "source\t" + kSeamarkLayer +
                                  "\tlayer\t" + kNameSeamark + "\timage/png\t3857\t0\tok\tfile.zmp\t\n";
    const std::string estimate =
        "estimate\t" + std::string(kEsri) + "\t8\t10\t10\t0.0000\t0.0000\t100\t0\t0\n";
    const std::vector<Row> rows = {
        // (usageError, exit 2) prints Qt's text through console.cpp:64 ("error:
        // ").
        {"exit 2 mentioning the format is the generic failure", 2, "error: Unknown option 'format'.\n", "",
         "Chart Grabber CLI does not work: Unknown option 'format'."},
        {"another exit with a last error line", 3,
         "warning: be careful\nerror: first\nerror: the maps folder is empty\nwarning: late\n", "",
         "Chart Grabber CLI does not work: the maps folder is empty"},
        {"an error without a text", 4, "", "", "Chart Grabber CLI does not work: error 4"},
        {"exit 3 without an error line", 3, "", "", "Chart Grabber CLI does not work: error 3"},
        {"exit 2 whose error does not mention the format is not 'too old'", 2, "error: no command given\n",
         "", "Chart Grabber CLI does not work: no command given"},
        {"exit 0 without a source record (other records are ignored)", 0, "", estimate, kNoSources},
        {"a failed run's records (a base and the seamark layer) are not used", 3, "error: broken\n",
         oneSource, "Chart Grabber CLI does not work: broken"},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        sources_->reset();
        sources_->exitWith(row.exitCode);
        sources_->stderrText(row.stderrText);
        sources_->rawRecords(row.records);
        makeDialog();
        EXPECT_EQ(status(), row.expected);
        EXPECT_TRUE(preferences()->IsShown());
        EXPECT_EQ(chart()->GetCount(), 0u);
        EXPECT_FALSE(chart()->IsEnabled());
        EXPECT_FALSE(moreSources()->IsEnabled());
        EXPECT_FALSE(seamarks()->IsEnabled());
        destroyDialog();
    }
}

TEST_F(PanelTest, ACliThatIsNotThereShowsTheNotFoundTextAndLogsWhy) {
    const std::string missing = (root_ / "no such d'x" / std::string(cg::kDefaultCliName)).string();
    setSettings([&](cg::Settings& s) { s.cliPath = missing; });
    auto* log = new CollectLog;
    wxLog* previous = wxLog::SetActiveTarget(log);
    makeDialog();
    const std::vector<std::string> logged = log->messages;
    delete wxLog::SetActiveTarget(previous);
    EXPECT_EQ(status(), kNotFound);
    EXPECT_TRUE(preferences()->IsShown());
    EXPECT_EQ(chart()->GetCount(), 0u);
    ASSERT_EQ(logged.size(), 1u);
    EXPECT_EQ(logged[0],
              "ChartGrabber: " + missing + " cannot be run as a program."); // cli_runner.cpp resolveCli
    EXPECT_TRUE(sources_->argv().empty());
}

std::string slurpText(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The real `note:` line of the first start (cli-2026.10.6/sources-first-start-note: stdout the 3 records, stderr
// a `note:` line and a `warning:`, exit 0) changes nothing: the same choice and status as without stderr.
TEST_F(PanelTest, TheFirstStartNoteOfTheCliChangesNothingInTheChoice) {
    const fs::path fx = fs::path(CG_FIXTURES_DIR) / "cli-2026.10.6";
    const std::string err = slurpText(fx / "sources-first-start-note.stderr");
    ASSERT_EQ(err.rfind("note: moved ", 0), 0u) << err;
    sources_->rawRecords(slurpText(fx / "sources-first-start-note.stdout"));
    makeDialog();
    waitLoaded();
    const std::vector<std::string> without = entries();
    const std::string statusWithout = status();
    destroyDialog();
    sources_->stderrText(err);
    makeDialog();
    waitLoaded();
    EXPECT_EQ(entries(), (std::vector<std::string>{"OpenTopoMap"}));
    EXPECT_EQ(entries(), without);
    EXPECT_EQ(status(), statusWithout);
    EXPECT_EQ(status().find("does not work"), std::string::npos) << status();
}

// / a program that is too old (0.1.0, 2026.10.3) or no tile program shows
// cliTooOldText with the Preferences button, and the stand-in is not run for sources.
TEST_F(PanelTest, ATooOldOrUnknownProgramShowsTheTooOldTextWithPreferences) {
    int i = 0;
    for (const char* line :
         {"old-tiles 0.1.0 (test)", "chart-grabber-cli 2026.10.3 (test)", "hello world", ""}) {
        SCOPED_TRACE(line);
        stub::Sources other(root_, "old stub " + std::to_string(++i));
        other.version(line);
        other.records(allCurated());
        setSettings([&](cg::Settings& s) { s.cliPath = other.cli(); });
        makeDialog();
        EXPECT_EQ(status(), kTooOld);
        EXPECT_TRUE(preferences()->IsShown());
        EXPECT_EQ(chart()->GetCount(), 0u);
        EXPECT_FALSE(chart()->IsEnabled());
        EXPECT_TRUE(other.argv().empty()) << "no sources run for a program that is too old";
        EXPECT_EQ(other.probes(), 1);
        destroyDialog();
    }
}

// Clicks OK or Cancel of the Preferences dialog from a timer that runs inside its modal loop.
struct DialogCloser {
    explicit DialogCloser(int id, std::function<void()> before = {}) : timer(&handler) {
        handler.Bind(wxEVT_TIMER, [this, id, before](wxTimerEvent&) {
            for (auto* node = wxTopLevelWindows.GetFirst(); node; node = node->GetNext()) {
                auto* dlg = dynamic_cast<SettingsDialog*>(node->GetData());
                if (!dlg || !dlg->IsShown())
                    continue;
                timer.Stop();
                found = true;
                if (before)
                    before();
                wxWindow* button = dlg->FindWindow(id);
                wxCommandEvent e(wxEVT_BUTTON, id);
                e.SetEventObject(button);
                dlg->GetEventHandler()->ProcessEvent(e);
                return;
            }
        });
        timer.Start(30);
    }
    wxEvtHandler handler;
    wxTimer timer;
    bool found = false;
};

TEST_F(PanelTest, PreferencesOkRunsTheSourcesAgainAndCancelDoesNot) {
    const fs::path bundled =
        root_ / "bundled d'x"; // the dialog's own check runs the CLI only with a bundled source
    fs::create_directories(bundled);
    std::ofstream(bundled / "a.zmp") << "x";
    store_->setBundledSourcesDir(bundled.string());
    const std::string kBroken = "Chart Grabber CLI does not work: broken";
    sources_->exitWith(3);
    sources_->stderrText("error: broken\n");
    sources_->rawRecords("");
    makeDialog();
    ASSERT_EQ(status(), kBroken);
    ASSERT_TRUE(preferences()->IsShown());
    {
        // The dialog's own check of this opening is in the stand-in's log (run 2). Not the dialog's status
        // label: the dialog of the previous opening is destroyed in idle time and FindWindowByName may find
        // its label.
        DialogCloser cancel(wxID_CANCEL, [&] { waitRuns(2); });
        clickUntilFound(preferences(), cancel);
        EXPECT_TRUE(cancel.found);
    }
    // panel run + the dialog's own check; Cancel adds no rerun.
    EXPECT_EQ(sources_->argv().size(), 2u);
    EXPECT_EQ(status(), kBroken);
    {
        // The user installs a current tile program while the dialog is open: the next run succeeds.
        DialogCloser ok(wxID_OK, [&] {
            // the dialog's check of this opening (run 3) is in the log before the stand-in changes
            waitRuns(3);
            sources_->reset();
            sources_->records({map(kMapnik, kNameMapnik)});
        });
        clickUntilFound(preferences(), ok);
        EXPECT_TRUE(ok.found);
    }
    // Runs so far: panel + dialog (open 1), dialog (open 2) = 3; the rerun on OK is the 4th: wait for its
    // start (a count of 2 was already true), then for its end.
    waitRuns(4);
    waitLoaded();
    // The closed Settings window is destroyed in idle time; until then its status labels (its own check said
    // "No usable chart source was found" for run 3) are children of the dialog and status would read them.
    ASSERT_TRUE(runUntil([&] { return !wxWindow::FindWindowByName("sourcesStatus"); }))
        << "the Settings window was not destroyed";
    EXPECT_EQ(entries(), std::vector<std::string>{kNameMapnik});
    EXPECT_EQ(status(), kEstimating);
    EXPECT_FALSE(preferences()->IsShown());
    // panel + dialog (open 1), dialog (open 2), and exactly one panel rerun on OK.
    EXPECT_EQ(sources_->argv().size(), 4u);
}

TEST_F(PanelTest, ReloadingDuringARunDiscardsTheFirstRunsResult) {
    // Run 1 prints its record and then waits; the reload terminates it, so its (signal) exit would be an
    // error text if it were not discarded. Run 2 must be the one whose records stay.
    sources_->records({map(kMapnik, "Old chart")});
    sources_->hold(true);
    makeDialog(false);
    waitRuns(1);
    sources_->records({map(kEsri, "New chart")});
    panel()->reloadSources();
    EXPECT_TRUE(loading());
    waitRuns(2);
    sources_->hold(false);
    waitLoaded();
    EXPECT_EQ(entries(), std::vector<std::string>{"New chart"});
    EXPECT_EQ(status(), kEstimating);
    EXPECT_FALSE(preferences()->IsShown());
    EXPECT_EQ(sources_->argv().size(), 2u);
}

TEST_F(PanelTest, AReloadReadsTheSettingsAgainAndKeepsTheUsersPick) {
    makeDialog();
    choose(chart(), 4); // Mapnik of [Esri, Google, Bing, Navionics, Mapnik]
    ASSERT_EQ(selected(), kNameMapnik);
    const std::string mapsDir = (root_ / "maps d'x").string();
    fs::create_directories(mapsDir);
    std::ofstream(fs::path(mapsDir) / "a.zmp") << "x"; // --maps is passed only when the folder holds a source
    const std::string bundled = (root_ / "bundl\xC3\xA9 d'x").string();
    store_->setBundledSourcesDir(bundled);
    stub::Sources other(root_, "other stub");
    other.records({map(kEsri, kNameEsri), map(kMapnik, kNameMapnik), map(kBeta, "Beta")});
    setSettings([&](cg::Settings& s) {
        s.cliPath = other.cli();
        s.mapsDir = mapsDir;
    });
    panel()->reloadSources();
    waitLoaded();
    EXPECT_EQ(entries(), (std::vector<std::string>{kNameEsri, kNameMapnik}));
    EXPECT_EQ(selected(), kNameMapnik) << "the user's pick is LastSource of the session";
    EXPECT_EQ(other.argv(), std::vector<std::string>{"sources --format=tsv --maps " + bundled + " --maps " +
                                                     mapsDir + " --cache " + (root_ / "private").string() +
                                                     "/plugins/chartgrabber/cache"});
    EXPECT_EQ(sources_->argv().size(), 1u) << "the old settings were not used again";
}

// the bundled sources and NO folder of the sailor's: the choice is filled (no step asks for a
// folder), a curated GUID without a record is skipped, a record outside the list is not in the choice.
TEST_F(PanelTest, WithTheBundledSourcesAndNoFolderTheChoiceIsFilled) {
    makeDialog();
    const std::string bundled = (root_ / "bundled d'x").string();
    store_->setBundledSourcesDir(bundled);
    stub::Sources other(root_, "other stub");
    other.records({map(kEsri, kNameEsri), map(kBeta, "Beta"), layer(kSeamarkLayer, kNameSeamark),
                   map(kMapnik, kNameMapnik)});
    setSettings([&](cg::Settings& s) { s.cliPath = other.cli(); });
    panel()->reloadSources();
    waitLoaded();
    EXPECT_EQ(entries(), (std::vector<std::string>{kNameEsri, kNameMapnik}))
        << "Bing, Navionics, Google have no record (skipped); Beta and the seamark layer are not curated "
           "bases";
    EXPECT_EQ(selected(), kNameEsri) << "no LastSource: the first curated base";
    EXPECT_EQ(other.argv(),
              std::vector<std::string>{"sources --format=tsv --maps " + bundled + " --cache " +
                                       (root_ / "private").string() + "/plugins/chartgrabber/cache"});
}

// ---- the seamarks checkbox ----

TEST_F(PanelTest, TheFirstFillAppliesLastSeamarksAndEveryBaseChangeFollowsTheSatelliteSet) {
    struct Row {
        bool last;
        const char* base;
    };
    for (const Row& row : {Row{false, kGoogle}, Row{true, kMapnik}}) {
        SCOPED_TRACE(std::string("LastSeamarks ") + (row.last ? "on, " : "off, ") + row.base);
        saveLast([&](ConfigStore::LastChoice& c) {
            c.seamarks = row.last;
            c.sourceGuid = row.base;
        });
        makeDialog();
        EXPECT_EQ(seamarks()->GetValue(), row.last) << "first fill";
        EXPECT_TRUE(seamarks()->IsEnabled());
        EXPECT_FALSE(text(kSeamarksMissing));
        // [Esri, Google, Bing, Navionics, Mapnik]; satellite set = the first three.
        const std::vector<std::pair<int, bool>> steps = {
            {4, false}, {0, true}, {3, false}, {2, true}, {1, true}};
        for (const auto& [index, expected] : steps) {
            choose(chart(), index);
            EXPECT_EQ(seamarks()->GetValue(), expected) << selected();
        }
        destroyDialog();
    }
}

TEST_F(PanelTest, AReloadFollowsTheBaseRuleOnlyWhenTheSelectedSourceChanged) {
    saveLast([&](ConfigStore::LastChoice& c) { c.sourceGuid = kGoogle; });
    makeDialog();
    ASSERT_EQ(selected(), kNameGoogle);
    ASSERT_TRUE(seamarks()->GetValue());
    tick(seamarks()); // the user unticks
    panel()->reloadSources();
    waitLoaded();
    EXPECT_EQ(selected(), kNameGoogle);
    EXPECT_FALSE(seamarks()->GetValue()) << "same base after the reload: the user's choice stays";
    sources_->records({map(kEsri, kNameEsri), map(kMapnik, kNameMapnik), layer(kSeamarkLayer, kNameSeamark)});
    panel()->reloadSources();
    waitLoaded();
    EXPECT_EQ(selected(), kNameEsri) << "Google is gone";
    EXPECT_TRUE(seamarks()->GetValue()) << "another base: the satellite rule";
}

TEST_F(PanelTest, APreselectionNeverReplacesTheSavedSourceButAUserPickDoes) {
    saveLast([&](ConfigStore::LastChoice& c) { c.sourceGuid = kDelta; });
    sources_->records({map(kMapnik, kNameMapnik), map(kEsri, kNameEsri)});
    makeDialog();
    ASSERT_EQ(selected(), kNameEsri) << "the saved source is missing: the first curated one";
    sources_->records({map(kMapnik, kNameMapnik), map(kEsri, kNameEsri), map(kDelta, kNameCyrillic)});
    panel()->reloadSources();
    waitLoaded();
    EXPECT_EQ(selected(), kNameCyrillic) << "the saved source is back and was not forgotten";
    choose(chart(), 0);
    ASSERT_EQ(selected(), kNameEsri);
    sources_->records({map(kMapnik, kNameMapnik), map(kEsri, kNameEsri)});
    panel()->reloadSources();
    waitLoaded();
    sources_->records({map(kMapnik, kNameMapnik), map(kEsri, kNameEsri), map(kDelta, kNameCyrillic)});
    panel()->reloadSources();
    waitLoaded();
    EXPECT_EQ(selected(), kNameEsri) << "the user's pick replaced the saved source";
}

TEST_F(PanelTest, TheSeamarksValueIsKeptAfterAFailedSourcesRun) {
    saveLast([&](ConfigStore::LastChoice& c) { c.sourceGuid = kGoogle; });
    makeDialog();
    ASSERT_TRUE(seamarks()->GetValue());
    sources_->exitWith(1);
    panel()->reloadSources();
    waitLoaded();
    EXPECT_FALSE(seamarks()->IsEnabled());
    EXPECT_TRUE(seamarks()->GetValue()) << "the value is kept while the run failed";
}

TEST_F(PanelTest, AMissingSeamarkLayerDisablesAndUnticksTheCheckboxWithItsNote) {
    sources_->records({map(kEsri, kNameEsri), map(kMapnik, kNameMapnik)});
    makeDialog();
    EXPECT_FALSE(seamarks()->IsEnabled());
    EXPECT_FALSE(seamarks()->GetValue()) << "LastSeamarks is on, the layer is not there";
    wxStaticText* note = text(kSeamarksMissing);
    ASSERT_NE(note, nullptr);
    EXPECT_GT(note->GetPosition().x, seamarks()->GetPosition().x) << "right of the checkbox";
    choose(chart(), 0);
    EXPECT_FALSE(seamarks()->GetValue()) << "a satellite base does not tick a missing layer";
}

TEST_F(PanelTest, ARefusedSeamarkLayerDisablesTheCheckboxAndShowsTheRefusal) {
    struct Row {
        Rec layerRecord;
        std::string note;
    };
    const std::vector<Row> rows = {
        {layer(kSeamarkLayer, kNameSeamark, 3395), kProjection3395},
        {layer(kSeamarkLayer, kNameSeamark, 3857, kRefusedVerdict), kRefusedVerdict},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.note);
        sources_->records({map(kEsri, kNameEsri), row.layerRecord});
        makeDialog();
        EXPECT_FALSE(seamarks()->IsEnabled());
        EXPECT_FALSE(seamarks()->GetValue()) << "unticked as well";
        EXPECT_TRUE(text(row.note.c_str()));
        EXPECT_FALSE(text(kSeamarksMissing));
        destroyDialog();
    }
}

// ---- the imagery line ----

TEST_F(PanelTest, TheImageryLineShowsForSatelliteBasesUnderTheChartRowAndTheDialogShrinksBack) {
    saveLast([&](ConfigStore::LastChoice& c) { c.sourceGuid = kGoogle; });
    makeDialog();
    wxStaticText* line = text(kImagery);
    ASSERT_NE(line, nullptr);
    EXPECT_GT(line->GetPosition().y, chart()->GetPosition().y);
    EXPECT_LT(line->GetPosition().y, seamarks()->GetPosition().y) << "directly under the Chart row";
    const wxSize withLine = dialog_->GetSize();
    choose(chart(), 4); // Mapnik
    EXPECT_FALSE(shows(kImagery));
    EXPECT_LT(dialog_->GetSize().y, withLine.y) << "the hidden line gives its space back";
    choose(chart(), 1); // Google
    EXPECT_TRUE(shows(kImagery));
    EXPECT_EQ(dialog_->GetSize(), withLine);
    choose(chart(), 3); // Navionics: a chart, not imagery
    EXPECT_FALSE(shows(kImagery));
    for (int satellite : {0, 2}) { // Esri, Bing
        choose(chart(), satellite);
        EXPECT_TRUE(shows(kImagery)) << selected();
    }
}

TEST_F(PanelTest, ABaseWhoseNameLooksLikeImageryShowsTheLineAndOtherNamesDoNot) {
    const std::vector<std::pair<const char*, bool>> names = {{"Regional Aerial 2020", true},
                                                             {"Regional Roads", false}};
    for (const auto& [name, expected] : names) {
        SCOPED_TRACE(name);
        sources_->records({map(kDelta, name)});
        saveLast([&](ConfigStore::LastChoice& c) { c.sourceGuid = kDelta; });
        makeDialog();
        EXPECT_EQ(shows(kImagery), expected);
        destroyDialog();
    }
}

TEST_F(PanelTest, NoImageryLineWhileNoBaseIsSelected) {
    saveLast([&](ConfigStore::LastChoice& c) { c.sourceGuid = kGoogle; });
    sources_->exitWith(3);
    makeDialog();
    EXPECT_FALSE(shows(kImagery));
    sources_->hold(true);
    sources_->reset();
    panel()->reloadSources();
    EXPECT_FALSE(shows(kImagery)) << "while loading";
    sources_->hold(false);
    waitLoaded();
    EXPECT_TRUE(shows(kImagery));
}

// ---- the bands ----

TEST_F(PanelTest, BandsStartAsPassageAndCoastalAndStayContiguous) {
    setViewport(50, 50.05, 10, 10.05); // small: Harbour is allowed
    makeDialog();
    EXPECT_EQ(ticks(), (std::vector<bool>{true, true, false, false}));
    tick(band(1)); // untick Coastal: Passage remains below, nothing above
    EXPECT_EQ(ticks(), (std::vector<bool>{true, false, false, false}));
    tick(band(2)); // tick Approach: Passage + Approach -> Coastal ticks
    EXPECT_EQ(ticks(), (std::vector<bool>{true, true, true, false}));
    tick(band(1)); // untick Coastal: bands remain below and above -> Approach goes too
    EXPECT_EQ(ticks(), (std::vector<bool>{true, false, false, false}));
    EXPECT_FALSE(shows(kChooseDetail));
    tick(band(0));
    EXPECT_EQ(ticks(), (std::vector<bool>{false, false, false, false}));
    EXPECT_EQ(status(), kChooseDetail);
    tick(band(3)); // Harbour alone ticks everything between: nothing ticked below, so only Harbour
    EXPECT_EQ(ticks(), (std::vector<bool>{false, false, false, true}));
    EXPECT_NE(status(), kChooseDetail);
    tick(band(0));
    EXPECT_EQ(ticks(), (std::vector<bool>{true, true, true, true}));
}

TEST_F(PanelTest, LastBandsAreMadeContiguousAtCreation) {
    saveLast([&](ConfigStore::LastChoice& c) { c.bands = {cg::Band::Passage, cg::Band::Approach}; });
    makeDialog();
    EXPECT_EQ(ticks(), (std::vector<bool>{true, true, true, false}));
    destroyDialog();
    saveLast([&](ConfigStore::LastChoice& c) { c.bands = {cg::Band::Coastal}; });
    makeDialog();
    EXPECT_EQ(ticks(), (std::vector<bool>{false, true, false, false}));
}

TEST_F(PanelTest, ScaleLabelsFollowTheLatitudeOfTheArea) {
    auto shownAs = [&](const char* const(&expected)[4]) {
        for (int i = 0; i < 4; ++i)
            EXPECT_TRUE(shows(expected[i])) << kBandNames[i] << " " << expected[i];
    };
    addRoute("cccccccc-0000-4000-8000-000000000001", "North", {{59.5, 5.0}, {60.5, 6.0}});
    setViewport(-1, 1, 5, 15); // centre latitude 0
    makeDialog();
    shownAs(kScalesEquator);
    EXPECT_FALSE(shows(kScales60[0]));
    setViewport(59, 61, 5, 15); // centre latitude 60
    press(view());
    shownAs(kScales60);
    EXPECT_FALSE(shows(kScalesEquator[0]));
    setViewport(-1, 1, 5, 15);
    press(view());
    shownAs(kScalesEquator);
    press(route()); // route bounds centre: latitude 60
    shownAs(kScales60);
}

TEST_F(PanelTest, WithoutAnAreaTheLabelsUseLatitudeZeroAndHarbourStaysEnabled) {
    area_ = std::make_unique<AreaTool>([] { return wxColour(); }, [] { return false; }); // no viewport yet
    saveLast([&](ConfigStore::LastChoice& c) {
        c.bands = {cg::Band::Passage, cg::Band::Coastal, cg::Band::Approach, cg::Band::Harbour};
    });
    makeDialog();
    ASSERT_TRUE(shows("Move the chart once, then try again."));
    for (int i = 0; i < 4; ++i)
        EXPECT_TRUE(shows(kScalesEquator[i]));
    EXPECT_TRUE(band(3)->IsEnabled());
    EXPECT_TRUE(band(3)->GetValue());
    EXPECT_FALSE(shows(kHarbourTooLarge));
}

// ---- Harbour ----

TEST_F(PanelTest, HarbourIsDisabledAndUntickedForALargeAreaAndStaysUntickedWhenEnabledAgain) {
    saveLast([&](ConfigStore::LastChoice& c) {
        c.bands = {cg::Band::Passage, cg::Band::Coastal, cg::Band::Approach, cg::Band::Harbour};
    });
    setViewport(40, 50, 0, 10); // 94 million tiles at z17 + z18
    makeDialog();
    EXPECT_FALSE(band(3)->IsEnabled());
    EXPECT_EQ(ticks(), (std::vector<bool>{true, true, true, false}));
    EXPECT_TRUE(shows(kHarbourTooLarge));
    EXPECT_TRUE(band(2)->IsEnabled());
    setViewport(50, 50.05, 10, 10.05); // 2 746 tiles
    press(view());
    EXPECT_TRUE(band(3)->IsEnabled());
    EXPECT_FALSE(band(3)->GetValue()) << "stays unticked";
    EXPECT_FALSE(shows(kHarbourTooLarge));
    tick(band(3));
    EXPECT_EQ(ticks(), (std::vector<bool>{true, true, true, true}));
}

TEST_F(PanelTest, TheHarbourLimitIs20000TilesAtZoom17And18OfTheRouteCorridor) {
    // Route (50,10)-(50.02,10.02): 19 144 tiles at a 5.5 NM corridor, 22 021 at 6.0 NM (hand computation of
    // the formula).
    addRoute("cccccccc-0000-4000-8000-000000000002", "Short", {{50.0, 10.0}, {50.02, 10.02}});
    saveLast([&](ConfigStore::LastChoice& c) {
        c.bands = {cg::Band::Passage, cg::Band::Coastal, cg::Band::Approach, cg::Band::Harbour};
    });
    setSettings([&](cg::Settings& s) { s.corridorNm = 5.5; });
    setViewport(50, 50.05, 10, 10.05); // the view is small, so Harbour starts ticked
    makeDialog();
    press(route());
    ASSERT_EQ(routeChoice()->GetSelection(), 0);
    EXPECT_TRUE(band(3)->IsEnabled());
    EXPECT_TRUE(band(3)->GetValue());
    click(wider()); // 6.0 NM
    EXPECT_FALSE(band(3)->IsEnabled());
    EXPECT_FALSE(band(3)->GetValue());
    EXPECT_TRUE(shows(kHarbourTooLarge));
    click(narrower()); // 5.5 NM again
    EXPECT_TRUE(band(3)->IsEnabled());
    EXPECT_FALSE(band(3)->GetValue());
    EXPECT_FALSE(shows(kHarbourTooLarge));
}

// ---- Advanced zooms ----

TEST_F(PanelTest, TheAdvancedFoldIsCollapsedAtCreationAndTheDialogFollowsItsState) {
    makeDialog();
    auto panes = all<wxCollapsiblePane>();
    ASSERT_EQ(panes.size(), 1u);
    EXPECT_TRUE(panes[0]->IsCollapsed());
    const wxSize closed = dialog_->GetSize();
    auto* body = wxWindow::FindWindowByName("pageBody");
    const int rows = body->GetVirtualSize().y;
    panes[0]->Collapse(false);
    wxCollapsiblePaneEvent e(panes[0], panes[0]->GetId(), false);
    panes[0]->GetEventHandler()->ProcessEvent(e);
    EXPECT_GT(body->GetVirtualSize().y, rows) << "the fold's rows join the page";
    EXPECT_GT(dialog_->GetSize().y, closed.y)
        << "non-touch: no cap, the dialog takes the rows' natural height";
    panes[0]->Collapse(true);
    wxCollapsiblePaneEvent back(panes[0], panes[0]->GetId(), true);
    panes[0]->GetEventHandler()->ProcessEvent(back);
    EXPECT_EQ(dialog_->GetSize(), closed) << "the dialog shrinks back";
}

TEST_F(PanelTest, TheFoldHasTwoDetailDropDownsOf22EntriesAndNoTextBox) {
    setViewport(-1, 1, 5, 15); // centre latitude 0
    makeDialog();
    for (wxChoice* c : {least(), most()}) {
        ASSERT_NE(c, nullptr);
        ASSERT_EQ(c->GetCount(), 22u);
        EXPECT_EQ(u(c->GetString(10)), "1:580k  (zoom 10)");
        EXPECT_EQ(u(c->GetString(21)), "1:280  (zoom 21)");
    }
    EXPECT_TRUE(labelled<wxStaticText>("Least detail") != nullptr);
    EXPECT_TRUE(labelled<wxStaticText>("Most detail") != nullptr);
    // the old "Zoom levels" text box is gone: the only text field left on the page is the Name
    EXPECT_EQ(all<wxTextCtrl>().size(), 1u);
    EXPECT_EQ(labelled<wxStaticText>("Zoom levels"), nullptr);
    // an area at another latitude: the texts change, the selections stay on the same levels
    pickZooms(9, 13);
    setViewport(59, 61, 5, 15); // centre latitude 60: the denominators halve
    press(view());
    for (wxChoice* c : {least(), most()})
        EXPECT_EQ(u(c->GetString(10)), "1:290k  (zoom 10)");
    EXPECT_EQ(least()->GetSelection(), 9);
    EXPECT_EQ(most()->GetSelection(), 13);
}

TEST_F(PanelTest, BandTapsSetTheRange) {
    setViewport(50, 50.05, 10, 10.05); // small: Harbour is allowed
    makeDialog();
    EXPECT_EQ(ticks(), (std::vector<bool>{true, true, false, false}));
    EXPECT_EQ(least()->GetSelection(), 8);
    EXPECT_EQ(most()->GetSelection(), 14);
    tick(band(3)); // Harbour: the gap is closed, 8-18
    EXPECT_EQ(ticks(), (std::vector<bool>{true, true, true, true}));
    EXPECT_EQ(least()->GetSelection(), 8);
    EXPECT_EQ(most()->GetSelection(), 18);
    tick(
        band(1)); // untick Coastal with Passage below and Approach, Harbour above: Passage only (rule)
    EXPECT_EQ(ticks(), (std::vector<bool>{true, false, false, false}));
    EXPECT_EQ(least()->GetSelection(), 8);
    EXPECT_EQ(most()->GetSelection(), 11);
    tick(band(2)); // Approach with Passage: Coastal in between ticks
    EXPECT_EQ(ticks(), (std::vector<bool>{true, true, true, false}));
    EXPECT_EQ(most()->GetSelection(), 16);
}

TEST_F(PanelTest, TheRangeShowsInTheBoxesAndTheNoteNamesThePartlyCoveredBands) {
    setViewport(50, 50.05, 10, 10.05); // centre latitude 50.025: z10 1:370k, z14 1:23k, z17 1:2.9k
    makeDialog();
    EXPECT_EQ(rangeNote(), "") << "the range equals the ticked bands";
    choose(least(), 10); // 10-14
    EXPECT_EQ(ticks(), (std::vector<bool>{false, true, false, false}));
    EXPECT_EQ(rangeNote(), "Detail set under Advanced: 1:370k to 1:23k (part of Passage)");
    choose(most(), 17); // 10-17
    EXPECT_EQ(ticks(), (std::vector<bool>{false, true, true, false}));
    EXPECT_EQ(rangeNote(), "Detail set under Advanced: 1:370k to 1:2.9k (part of Passage and Harbour)");
    pickZooms(12, 14);
    EXPECT_EQ(ticks(), (std::vector<bool>{false, true, false, false}));
    EXPECT_EQ(rangeNote(), "") << "12-14 is exactly Coastal";
    pickZooms(10, 14);
    tick(band(0)); // a tap on the partly covered Passage ticks it fully: the range is the ticked bands
    EXPECT_EQ(ticks(), (std::vector<bool>{true, true, false, false}));
    EXPECT_EQ(least()->GetSelection(), 8);
    EXPECT_EQ(most()->GetSelection(), 14);
    EXPECT_EQ(rangeNote(), "");
    pickZooms(19, 21); // wholly outside the bands: no band named
    EXPECT_EQ(ticks(), (std::vector<bool>{false, false, false, false}));
    EXPECT_EQ(rangeNote(), "Detail set under Advanced: 1:730 to 1:180");
}

TEST_F(PanelTest, PicksKeepLeastBelowMostAndNothingChosenHasNoSelection) {
    makeDialog();
    choose(least(), 16); // above Most (14): Most moves to it
    EXPECT_EQ(least()->GetSelection(), 16);
    EXPECT_EQ(most()->GetSelection(), 16);
    choose(most(), 5); // below Least: Least moves
    EXPECT_EQ(least()->GetSelection(), 5);
    EXPECT_EQ(most()->GetSelection(), 5);
    EXPECT_NE(status(), kChooseDetail);
    pickZooms(8, 14);
    tick(band(1));
    tick(band(0)); // nothing chosen
    EXPECT_EQ(least()->GetSelection(), wxNOT_FOUND);
    EXPECT_EQ(most()->GetSelection(), wxNOT_FOUND);
    EXPECT_EQ(status(), kChooseDetail);
    choose(most(), 12); // a pick in either gives lo = hi = that level
    EXPECT_EQ(least()->GetSelection(), 12);
    EXPECT_EQ(most()->GetSelection(), 12);
    pickZooms(8, 14);
    tick(band(1));
    tick(band(0));
    ASSERT_EQ(least()->GetSelection(), wxNOT_FOUND);
    choose(least(), 9);
    EXPECT_EQ(least()->GetSelection(), 9);
    EXPECT_EQ(most()->GetSelection(), 9);
}

TEST_F(PanelTest, TheHarbourRuleTrimsTheRangeOnAnAreaChangeOnly) {
    saveLast([&](ConfigStore::LastChoice& c) { c.advancedZooms = "8-18"; });
    setViewport(50, 50.05, 10, 10.05);
    makeDialog();
    ASSERT_EQ(most()->GetSelection(), 18);
    EXPECT_EQ(ticks(), (std::vector<bool>{true, true, true, true}));
    setViewport(40, 50, 0, 10); // Harbour too large (centre latitude 45)
    press(view());
    EXPECT_FALSE(band(3)->IsEnabled());
    EXPECT_EQ(least()->GetSelection(), 8);
    EXPECT_EQ(most()->GetSelection(), 16) << "8-18 becomes 8-16";
    EXPECT_EQ(ticks(), (std::vector<bool>{true, true, true, false}));
    EXPECT_TRUE(shows(kHarbourTooLarge));
    choose(most(), 18); // a later pick is kept, no trim, no refusal
    EXPECT_EQ(most()->GetSelection(), 18);
    EXPECT_EQ(ticks(), (std::vector<bool>{true, true, true, false}));
    EXPECT_EQ(rangeNote(), "Detail set under Advanced: 1:1.6M to 1:1.6k") << "no Harbour part is named";
    EXPECT_FALSE(shows(kHarbourTooLarge)) << "one note line only";
    press(view()); // the next area change trims it
    EXPECT_EQ(most()->GetSelection(), 16);
}

TEST_F(PanelTest, TheHarbourRuleLeavesNothingChosenWhenOnlyHarbourLevelsWereChosen) {
    saveLast([&](ConfigStore::LastChoice& c) { c.advancedZooms = "17-18"; });
    setViewport(50, 50.05, 10, 10.05);
    makeDialog();
    ASSERT_EQ(least()->GetSelection(), 17);
    setViewport(40, 50, 0, 10);
    press(view());
    EXPECT_EQ(least()->GetSelection(), wxNOT_FOUND);
    EXPECT_EQ(most()->GetSelection(), wxNOT_FOUND);
    EXPECT_EQ(status(), kChooseDetail);
}

TEST_F(PanelTest, TheRangeAtCreationComesFromLastZoomsThenLastBandsThenTheDefault) {
    saveLast([&](ConfigStore::LastChoice& c) { c.advancedZooms = "8,14"; });
    makeDialog();
    EXPECT_EQ(least()->GetSelection(), 8) << "an old gapped list loads as its extremes";
    EXPECT_EQ(most()->GetSelection(), 14);
    destroyDialog();
    saveLast([&](ConfigStore::LastChoice& c) {
        c.advancedZooms = "";
        c.bands = {cg::Band::Coastal, cg::Band::Approach};
    });
    makeDialog();
    EXPECT_EQ(least()->GetSelection(), 12);
    EXPECT_EQ(most()->GetSelection(), 16);
    destroyDialog();
    saveLast([&](ConfigStore::LastChoice& c) {
        c.advancedZooms.assign(1, 'x');
        c.bands = {};
    });
    makeDialog();
    EXPECT_EQ(least()->GetSelection(), 8) << "a bad LastZooms and no LastBands: Passage + Coastal";
    EXPECT_EQ(most()->GetSelection(), 14);
}

// ---- the order of the status reasons ----

TEST_F(PanelTest, TheStatusShowsAreaThenSourceThenZoomsThenName) {
    setViewport(86, 89, 5, 15);
    setSettings([&](cg::Settings& s) { s.cliPath = (root_ / "missing" / "cli").string(); });
    makeDialog();
    EXPECT_EQ(status(), kOutside);
    EXPECT_FALSE(preferences()->IsShown()) << "Preferences only beside a sources failure";
    EXPECT_EQ(status(), kOutside);
    setViewport(45, 55, 5, 15);
    press(view());
    EXPECT_EQ(status(), kNotFound) << "the source comes before the zooms";
    EXPECT_TRUE(preferences()->IsShown());
    setSettings([&](cg::Settings&) {});
    panel()->reloadSources();
    waitLoaded();
    EXPECT_FALSE(preferences()->IsShown());
    tick(band(1));
    tick(band(0)); // nothing chosen
    EXPECT_EQ(status(), kChooseDetail) << "the zooms come before the name";
    tick(band(0));
    EXPECT_EQ(status(), kEstimating) << "the estimate comes before the name";
    typeInto(nameField(), "");
    EXPECT_EQ(status(), kEstimating) << "an empty name does not hide the pending estimate";
    waitEstimate();
    EXPECT_EQ(status(), kEnterName);
    typeInto(nameField(), "Solent");
    EXPECT_EQ(status(), "");
}

// ---- layout and touch ----

TEST_F(PanelTest, TheLargestPageFitsEightHundredByFourEighty) {
    // Route rows, the imagery line, one note (Harbour), Preferences hidden, Advanced collapsed.
    addRoute("cccccccc-0000-4000-8000-000000000003", "Long", {{45.0, 0.0}, {46.0, 10.0}});
    saveLast([&](ConfigStore::LastChoice& c) { c.sourceGuid = kGoogle; });
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::Route, std::nullopt);
    ASSERT_TRUE(shows(kImagery));
    ASSERT_TRUE(shows(kHarbourTooLarge));
    ASSERT_TRUE(routeChoice()->IsShown());
    const wxSize size = dialog_->GetSize();
    EXPECT_LE(size.x, 800);
    EXPECT_LE(size.y, 480);
    // Nothing is cut off: every control that is on screen lies inside the client area of the dialog.
    ASSERT_TRUE(dialog_->IsShownOnScreen());
    const wxRect client(dialog_->ClientToScreen(wxPoint(0, 0)), dialog_->GetClientSize());
    int checked = 0;
    for (wxWindow* w : all<wxWindow>()) {
        if (!w->IsShownOnScreen())
            continue;
        ++checked;
        EXPECT_TRUE(client.Contains(w->GetScreenRect())) << u(w->GetLabel());
    }
    EXPECT_GT(checked, 30);
}

TEST_F(PanelTest, TouchMakesTheNewControlsAtLeast48PixelsHigh) {
    ocpn_fake::touch = true;
    ocpn_fake::scale = 1.0;
    sources_->exitWith(3); // so the Preferences button is shown too
    makeDialog();
    for (wxWindow* c : std::vector<wxWindow*>{chart(), moreSources(), seamarks(), preferences()}) {
        ASSERT_NE(c, nullptr);
        EXPECT_GE(c->GetSize().y, 48) << u(c->GetLabel());
    }
    EXPECT_TRUE(preferences()->IsShown());
    for (wxChoice* c : {least(), most()})
        EXPECT_GE(c->GetMinSize().y, 48) << "inside the collapsed fold";
}

// ---- More sources through the page ----

// Drives the More sources dialog from a timer inside its modal loop; `body` must end the dialog.
struct MoreSourcesDriver {
    explicit MoreSourcesDriver(std::function<void(SourcesDialog&, wxListCtrl&)> body) : timer(&handler) {
        handler.Bind(wxEVT_TIMER, [this, body](wxTimerEvent&) {
            for (auto* node = wxTopLevelWindows.GetFirst(); node; node = node->GetNext()) {
                auto* dlg = dynamic_cast<SourcesDialog*>(node->GetData());
                if (!dlg || !dlg->IsShown())
                    continue;
                timer.Stop();
                found = true;
                std::vector<wxListCtrl*> lists;
                collect(dlg, lists);
                body(*dlg, *lists.at(0));
                return;
            }
        });
        timer.Start(30);
    }
    static std::vector<std::string> rows(wxListCtrl& list) {
        std::vector<std::string> out;
        for (long i = 0; i < list.GetItemCount(); ++i)
            out.push_back(list.GetItemText(i, 0).utf8_string());
        return out;
    }
    static long rowOf(wxListCtrl& list, const std::string& name) {
        for (long i = 0; i < list.GetItemCount(); ++i)
            if (list.GetItemText(i, 0).utf8_string() == name)
                return i;
        return -1;
    }
    static void press(SourcesDialog& dlg, int id) {
        wxCommandEvent e(wxEVT_BUTTON, id);
        e.SetEventObject(dlg.FindWindow(id));
        dlg.FindWindow(id)->GetEventHandler()->ProcessEvent(e);
    }
    // Selects the row of `name` and presses OK.
    static void pick(SourcesDialog& dlg, wxListCtrl& list, const std::string& name) {
        const long row = rowOf(list, name);
        ASSERT_GE(row, 0) << name;
        list.SetItemState(row, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
        ASSERT_TRUE(dlg.FindWindow(wxID_OK)->IsEnabled()) << name;
        press(dlg, wxID_OK);
    }
    wxEvtHandler handler;
    wxTimer timer;
    bool found = false;
};

// the terms-of-use note follows the GUID not the name.
TEST_F(PanelTest, TheTermsNoteShowsOnlyForTheEsriAndOpenTopoMapGuids) {
    const std::vector<std::tuple<const char*, const char*, bool>> rows = {
        {kEsri, "ESRI ArcGIS.Imagery", true},                     // the user's catalogue name
        {"{7b743985-bc5f-4ab6-8915-ac5dbbb8f552}", "Esri", true}, // ASCII case-insensitive
        {kTopo, "Topo", true},
        {kDelta, "OpenTopoMap", false}, // the name rule is gone
        {kBeta, "Esri World Imagery", false},
        {kUsgs, kNameUsgs, false},
        {kMapnik, kNameMapnik, false}};
    for (const auto& [guid, name, expected] : rows) {
        SCOPED_TRACE(std::string(guid) + " " + name);
        sources_->records({map(guid, name)});
        saveLast([&](ConfigStore::LastChoice& c) { c.sourceGuid = guid; });
        makeDialog();
        EXPECT_EQ(shows(kTermsNote), expected);
        destroyDialog();
    }
}

// all ten maps, scrambled, in the order of the choice; then only the seven the CLI ships.
TEST_F(PanelTest, TheChoiceHoldsTheTenCuratedMapsInTableOrderAndTheShippedSevenWithoutTheOthers) {
    const std::vector<Rec> all = {
        map(kEmodEurope, kNameEmodEurope),  map(kMapnik, kNameMapnik), map(kSentinel, kNameSentinel),
        map(kNavionics, kNameNavionics),    map(kTopo, kNameTopo),     map(kBing, kNameBing),
        layer(kSeamarkLayer, kNameSeamark), map(kUsgs, kNameUsgs),     map(kGoogle, kNameGoogle),
        map(kEmodWorld, kNameEmodWorld),    map(kEsri, kNameEsri)};
    sources_->records(all);
    makeDialog();
    EXPECT_EQ(entries(), (std::vector<std::string>{kNameEsri, kNameGoogle, kNameBing, kNameSentinel,
                                                   kNameUsgs, kNameNavionics, kNameMapnik, kNameTopo,
                                                   kNameEmodWorld, kNameEmodEurope}));
    destroyDialog();
    sources_->records({map(kEmodEurope, kNameEmodEurope), map(kTopo, kNameTopo), map(kUsgs, kNameUsgs),
                       layer(kSeamarkLayer, kNameSeamark), map(kEmodWorld, kNameEmodWorld),
                       map(kSentinel, kNameSentinel), map(kEsri, kNameEsri)});
    makeDialog();
    EXPECT_EQ(entries(), (std::vector<std::string>{kNameEsri, kNameSentinel, kNameUsgs, kNameTopo,
                                                   kNameEmodWorld, kNameEmodEurope}));
}

// the satellite set is the five GUIDs with "yes": imagery line and seamarks default on.
TEST_F(PanelTest, SentinelAndUsgsAreSatelliteAndEmodnetAndOpenTopoMapAreNot) {
    sources_->records({map(kEmodEurope, kNameEmodEurope), map(kTopo, kNameTopo), map(kUsgs, kNameUsgs),
                       layer(kSeamarkLayer, kNameSeamark), map(kEmodWorld, kNameEmodWorld),
                       map(kSentinel, kNameSentinel)});
    makeDialog();
    ASSERT_EQ(entries().size(), 5u);
    // [Sentinel-2, USGS, OpenTopoMap, EMODnet world, EMODnet Europe]
    const std::vector<std::pair<int, bool>> steps = {
        {3, false}, {0, true}, {2, false}, {1, true}, {4, false}};
    for (const auto& [index, satellite] : steps) {
        choose(chart(), index);
        SCOPED_TRACE(selected());
        EXPECT_EQ(seamarks()->GetValue(), satellite);
        EXPECT_EQ(shows(kImagery), satellite || index == 1) << "USGS also has 'Imagery' in its name";
    }
}

TEST_F(PanelTest, TheTermsNoteFollowsTheSelectedBase) {
    sources_->records({map(kEsri, "Esri World Imagery"), map(kMapnik, kNameMapnik)});
    saveLast([&](ConfigStore::LastChoice& c) { c.sourceGuid = kEsri; });
    makeDialog();
    ASSERT_EQ(selected(), "Esri World Imagery");
    EXPECT_TRUE(shows(kTermsNote));
    choose(chart(), 1);
    ASSERT_EQ(selected(), kNameMapnik);
    EXPECT_FALSE(shows(kTermsNote));
}

// the note follows the record's navigationWarning (field 14), not a GUID; terms note first.
TEST_F(PanelTest, TheNavigationNoteFollowsTheRecordFlagAndLeavesNoMarginWhenHidden) {
    const auto flagged = [&](const char* guid, const char* name, const char* flag) {
        Rec r = map(guid, name);
        r.navigationWarning = flag;
        sources_->records({r});
        saveLast([&](ConfigStore::LastChoice& c) { c.sourceGuid = guid; });
        makeDialog();
    };
    flagged(kEmodWorld, kNameEmodWorld, "1");
    EXPECT_TRUE(shows(kNavNote));
    EXPECT_FALSE(shows(kTermsNote));
    const bool downloadWith = labelled<wxButton>("Download")->IsEnabled();
    const std::string estimateWith = estimateLine();
    const wxSize withNote = dialog_->GetSize();
    EXPECT_LE(withNote.y, 480) << "the note alone fits (measured 442)";
    ASSERT_TRUE(text(kNavNote));
    const int noteHeight = text(kNavNote)->GetSize().y;
    destroyDialog();
    for (const char* flag : {"0", "x", ""}) { // "" = a 10-column line without the field
        SCOPED_TRACE(std::string("flag '") + flag + "'");
        flagged(kEmodWorld, kNameEmodWorld, flag);
        EXPECT_FALSE(shows(kNavNote));
        EXPECT_EQ(withNote.y - dialog_->GetSize().y, noteHeight + 6)
            << "a hidden note leaves no margin: the note's own height and its 6 px bottom border";
        EXPECT_EQ(labelled<wxButton>("Download")->IsEnabled(), downloadWith);
        EXPECT_EQ(estimateLine(), estimateWith);
        destroyDialog();
    }
    // Any GUID: OpenTopoMap has the terms note too; both show, terms above navigation. MEASURED on Xvfb: the
    // page is 736 x 486 without touch (terms note alone 461: the second note adds one line, 6 px over 480, no
    // source has both today) and 753 x 440 in touch mode (the rows scroll), so 480 is asserted for touch
    // only.
    for (const bool touch : {false, true}) {
        SCOPED_TRACE(touch ? "touch" : "no touch");
        ocpn_fake::touch = touch;
        flagged(kTopo, kNameTopo, "1");
        wxStaticText* terms = text(kTermsNote);
        wxStaticText* nav = text(kNavNote);
        ASSERT_TRUE(terms && nav);
        EXPECT_LT(terms->GetScreenRect().GetBottom(), nav->GetScreenRect().GetTop());
        EXPECT_LE(dialog_->GetSize().x, 800);
        if (touch) {
            EXPECT_LE(dialog_->GetSize().y, 480);
        }
        destroyDialog();
    }
}

TEST_F(PanelTest, TheNavigationNoteFollowsTheSelectedBase) {
    Rec flagged = map(kEmodWorld, kNameEmodWorld);
    flagged.navigationWarning = "1";
    sources_->records({flagged, map(kMapnik, kNameMapnik)});
    saveLast([&](ConfigStore::LastChoice& c) { c.sourceGuid = kEmodWorld; });
    makeDialog();
    ASSERT_EQ(selected(), kNameEmodWorld);
    EXPECT_TRUE(shows(kNavNote));
    choose(chart(), 0);
    ASSERT_EQ(selected(), kNameMapnik);
    EXPECT_FALSE(shows(kNavNote));
}

TEST_F(PanelTest, AMoreSourcesPickTakesTheLastSlotOfTheChoiceAndTheImageryLineFollowsItsName) {
    sources_->records({map(kMapnik, kNameMapnik), map(kGoogle, kNameGoogle), map(kEsri, kNameEsri),
                       layer(kSeamarkLayer, kNameSeamark), map(kDelta, "Delta"), map(kBeta, "Beta Sat"),
                       map(kYandex, "Yandex - Satellite", 3395, "projection")});
    saveLast([&](ConfigStore::LastChoice& c) { c.sourceGuid = kDelta; }); // LastSource holds the last slot
    makeDialog();
    ASSERT_EQ(entries(), (std::vector<std::string>{kNameEsri, kNameGoogle, kNameMapnik, "Delta"}));
    ASSERT_EQ(selected(), "Delta");
    ASSERT_TRUE(moreSources()->IsEnabled());

    struct Row {
        const char* name;
        const char* pick;
        std::vector<std::string> expectedEntries;
        bool imagery;
    };
    const std::vector<Row> rows = {
        // a pick replaces the previous pick / LastSource slot; "... Sat" shows the imagery line
        {"a new pick replaces the last slot",
         "Beta Sat",
         {kNameEsri, kNameGoogle, kNameMapnik, "Beta Sat"},
         true},
        // a pick that is already in the choice is only selected
        {"a curated pick is selected, nothing is added",
         kNameMapnik,
         {kNameEsri, kNameGoogle, kNameMapnik, "Beta Sat"},
         false},
        {"a name without a satellite word", "Delta", {kNameEsri, kNameGoogle, kNameMapnik, "Delta"}, false},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        const std::size_t runs = sources_->estimateArgv().size();
        MoreSourcesDriver driver(
            [&](SourcesDialog& dlg, wxListCtrl& list) { MoreSourcesDriver::pick(dlg, list, row.pick); });
        clickUntilFound(moreSources(), driver);
        ASSERT_TRUE(driver.found);
        EXPECT_EQ(entries(), row.expectedEntries);
        EXPECT_EQ(selected(), row.pick);
        EXPECT_EQ(shows(kImagery), row.imagery);
        EXPECT_EQ(u(nameField()->GetValue()), std::string("Area 50.00N 10.00E - ") + row.pick);
        waitEstimateRuns(runs + 1);
        waitEstimate();
        EXPECT_EQ(status(), "");
    }
    // The pick is the source of the estimate that followed it (Delta, the last pick).
    EXPECT_NE(sources_->estimateArgv().back().find(std::string(" --source ") + kDelta + " "),
              std::string::npos);
}

TEST_F(PanelTest, CancelChangesNothingAndReloadRunsTheSourcesAgainWithTheNewRecords) {
    makeDialog();
    ASSERT_EQ(entries().size(), 5u);
    {
        MoreSourcesDriver driver(
            [&](SourcesDialog& dlg, wxListCtrl&) { MoreSourcesDriver::press(dlg, wxID_CANCEL); });
        clickUntilFound(moreSources(), driver);
        ASSERT_TRUE(driver.found);
    }
    // A closed window is shown once more by wx's deferred show until its deferred Destroy runs: a driver
    // would find that corpse (its answer is gone) and a click on More sources would only raise it.
    waitSourcesDialogGone();
    EXPECT_EQ(entries().size(), 5u);
    EXPECT_EQ(selected(), kNameEsri);
    EXPECT_EQ(sources_->argv().size(), 1u) << "Cancel runs nothing";
    // Reload: the dialog ends with wxID_RETRY and the page runs `sources` again.
    sources_->records({map(kEsri, kNameEsri), map(kDelta, "Delta"), layer(kSeamarkLayer, kNameSeamark)});
    {
        MoreSourcesDriver driver([&](SourcesDialog& dlg, wxListCtrl&) {
            wxWindow* reload = wxWindow::FindWindowByLabel("Reload", &dlg);
            wxCommandEvent e(wxEVT_BUTTON, reload->GetId());
            e.SetEventObject(reload);
            reload->GetEventHandler()->ProcessEvent(e);
        });
        clickUntilFound(moreSources(), driver);
        ASSERT_TRUE(driver.found);
    }
    waitRuns(2);
    waitLoaded();
    EXPECT_EQ(entries(), (std::vector<std::string>{kNameEsri}));
    EXPECT_EQ(sources_->argv().size(), 2u);
    waitSourcesDialogGone();
    // The dialog now offers the new records.
    std::vector<std::string> shown;
    MoreSourcesDriver driver([&](SourcesDialog& dlg, wxListCtrl& list) {
        shown = MoreSourcesDriver::rows(list);
        MoreSourcesDriver::press(dlg, wxID_CANCEL);
    });
    clickUntilFound(moreSources(), driver);
    EXPECT_EQ(shown, (std::vector<std::string>{kNameEsri, "Delta", kNameSeamark}));
}

// ---- : More sources and Preferences from the page are modeless ----

template <class T> std::vector<T*> shownWindows() { // the windows of this type that are shown now
    std::vector<T*> out;
    for (auto* n = wxTopLevelWindows.GetFirst(); n; n = n->GetNext())
        if (auto* w = dynamic_cast<T*>(n->GetData()); w && w->IsShown())
            out.push_back(w);
    return out;
}

TEST_F(PanelTest, MoreSourcesIsAModelessWindowThePageIsDisabledAndThePickHappensOnTheAnswer) {
    sources_->records({map(kEsri, kNameEsri), map(kDelta, "Delta"), layer(kSeamarkLayer, kNameSeamark)});
    makeDialog();
    const std::vector<std::string> before = entries();
    ASSERT_EQ(before.size(), 1u);
    click(moreSources());
    ASSERT_EQ(shownWindows<SourcesDialog>().size(), 1u) << "the click returned with the window shown";
    SourcesDialog* dlg = shownWindows<SourcesDialog>()[0];
    EXPECT_FALSE(dlg->IsModal());
    EXPECT_EQ(dlg->GetParent(), static_cast<wxWindow*>(dialog_));
    EXPECT_FALSE(panel()->IsEnabled());
    EXPECT_EQ(entries(), before);
    click(moreSources()); // a second request raises the same window
    EXPECT_EQ(shownWindows<SourcesDialog>().size(), 1u);
    dlg->Close(); // the close box: nothing changes
    EXPECT_TRUE(shownWindows<SourcesDialog>().empty());
    EXPECT_TRUE(panel()->IsEnabled());
    EXPECT_EQ(entries(), before);
    EXPECT_EQ(sources_->argv().size(), 1u) << "nothing was run";

    click(moreSources()); // after the answer: a new window
    ASSERT_EQ(shownWindows<SourcesDialog>().size(), 1u);
    EXPECT_NE(shownWindows<SourcesDialog>()[0], dlg);
    std::vector<wxListCtrl*> lists;
    collect(shownWindows<SourcesDialog>()[0], lists);
    MoreSourcesDriver::pick(*shownWindows<SourcesDialog>()[0], *lists.at(0), "Delta");
    EXPECT_TRUE(panel()->IsEnabled());
    EXPECT_EQ(selected(), "Delta");
    EXPECT_EQ(entries(), (std::vector<std::string>{kNameEsri, "Delta"}));
    wxTheApp->ProcessIdle();
}

TEST_F(PanelTest, PreferencesFromThePageIsOneModelessWindowAtATimeEvenWithAnotherSettingsWindowOpen) {
    makeDialog();
    click(preferences());
    ASSERT_EQ(shownWindows<SettingsDialog>().size(), 1u);
    SettingsDialog* dlg = shownWindows<SettingsDialog>()[0];
    EXPECT_FALSE(dlg->IsModal());
    EXPECT_EQ(dlg->GetParent(), static_cast<wxWindow*>(dialog_));
    EXPECT_FALSE(panel()->IsEnabled());
    click(preferences());
    EXPECT_EQ(shownWindows<SettingsDialog>().size(), 1u) << "raised, not repeated";
    click(dynamic_cast<wxButton*>(dlg->FindWindow(wxID_CANCEL)));
    EXPECT_TRUE(panel()->IsEnabled());
    EXPECT_TRUE(shownWindows<SettingsDialog>().empty());
    wxTheApp->ProcessIdle();

    // A Settings window of another opener (the strip's) is open: the page opens none and enables nothing new.
    auto* other = new SettingsDialog(frame_, *store_, look_);
    other->Show();
    click(preferences());
    ASSERT_EQ(shownWindows<SettingsDialog>().size(), 1u);
    EXPECT_EQ(shownWindows<SettingsDialog>()[0], other);
    EXPECT_TRUE(panel()->IsEnabled());
    click(dynamic_cast<wxButton*>(other->FindWindow(wxID_CANCEL)));
    wxTheApp->ProcessIdle();
}

// What DeInit does to the main dialog: the page goes while its windows are open; they go at once and their
// callbacks never run. The windows are children of the dialog, which lives on: only the page's own delete
// removes them.
TEST_F(PanelTest, DeletingThePageDeletesItsOpenMoreSourcesAndSettingsWindowsAtOnce) {
    makeDialog();
    click(moreSources());
    ASSERT_EQ(shownWindows<SourcesDialog>().size(), 1u);
    wxWeakRef<SourcesDialog> sourcesWindow(shownWindows<SourcesDialog>()[0]);
    static_cast<wxSimplebook*>(panel()->GetParent())->DeleteAllPages();
    EXPECT_FALSE(sourcesWindow) << "no idle ran in between";
    destroyDialog();

    makeDialog();
    click(preferences());
    ASSERT_EQ(shownWindows<SettingsDialog>().size(), 1u);
    wxWeakRef<SettingsDialog> settingsWindow(shownWindows<SettingsDialog>()[0]);
    static_cast<wxSimplebook*>(panel()->GetParent())->DeleteAllPages();
    EXPECT_FALSE(settingsWindow);
    destroyDialog();
    EXPECT_EQ(sources_->argv().size(), 1u + 1u) << "the page's run, and the Settings window's own check";
}

// with CR 2 records that carry a `group` (the tsv level): header rows, the
// refused row of the user's set after the usable ones of its group.
TEST_F(PanelTest, GroupedRecordsGiveHeaderRowsAndTheRefusedRowIsGreyedAndNotSelectable) {
    sources_->records({map(kMapnik, kNameMapnik, 3857, "ok", ""),
                       map(kYandex, "Yandex - Satellite", 3395, "projection", "Sat\\Russia"),
                       map(kDelta, "Delta Sat", 3857, "ok", "Sat\\Russia"),
                       map(kEsri, kNameEsri, 3857, "ok", "Sat\\World")});
    makeDialog();
    std::vector<std::string> shown;
    std::string note;
    bool okOnRefused = true;
    MoreSourcesDriver driver([&](SourcesDialog& dlg, wxListCtrl& list) {
        shown = MoreSourcesDriver::rows(list);
        const long refused = MoreSourcesDriver::rowOf(list, "Yandex - Satellite");
        note = list.GetItemText(refused, 1).utf8_string();
        list.SetItemState(refused, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
        okOnRefused = dlg.FindWindow(wxID_OK)->IsEnabled();
        MoreSourcesDriver::pick(dlg, list, "Delta Sat");
    });
    clickUntilFound(moreSources(), driver);
    EXPECT_EQ(shown, (std::vector<std::string>{"Sat / Russia", "Delta Sat", "Yandex - Satellite",
                                               "Sat / World", kNameEsri, "Other", kNameMapnik}));
    EXPECT_EQ(note, kProjection3395);
    EXPECT_FALSE(okOnRefused);
    EXPECT_EQ(selected(), "Delta Sat");
}

// A refused source is never in the choice and the next curated one is preselected, so the refusal check of the
// Download button is not reachable through the choice. This test pins what the page really does.
TEST_F(PanelTest, AChosenBaseThatBecomesRefusedAfterReloadLeavesTheChoiceAndTheNextOneIsPreselected) {
    makeDialog();
    ASSERT_EQ(selected(), kNameEsri);
    ASSERT_EQ(status(), kEstimating);
    sources_->records({map(kEsri, kNameEsri, 3395, "projection"), map(kGoogle, kNameGoogle),
                       layer(kSeamarkLayer, kNameSeamark)});
    panel()->reloadSources();
    waitLoaded();
    EXPECT_EQ(entries(), (std::vector<std::string>{kNameGoogle}));
    EXPECT_EQ(selected(), kNameGoogle);
    EXPECT_FALSE(shows(kProjection3395)) << "the refused source is simply absent";
    waitEstimate();
    EXPECT_EQ(status(), "");
    EXPECT_NE(sources_->estimateArgv().back().find(std::string(" --source ") + kGoogle + " "),
              std::string::npos);
    // Only refused maps left: nothing to choose.
    sources_->records({map(kEsri, kNameEsri, 3395, "projection")});
    panel()->reloadSources();
    waitLoaded();
    EXPECT_EQ(status(), kNoUsable);
    EXPECT_FALSE(labelled<wxButton>("Download")->IsEnabled());
}

// =====================================================================================================
// the estimate the enable order and the Download flow Advanced threads and
// chart folder. The tile program is the stand-in of tests/sources_stub.h; its `estimate` and `run`
// answers are CR 2 records (the tsv level, version 2026.10.0); the real CLI is NOT used here (the real CLI
// is covered by contract_test.cpp). Expected values are literals or the captures in
// tests/fixtures/cli-2026.10.6/.
// =====================================================================================================

const char* const kTooLarge = "Area too large for this detail - choose a smaller area or less detail"; // 7.5
const char* const kRunning = "A download is running";                                                  // 7.5
const char* const kDisclaimer = "Not official charts. Do not use for navigation alone. Respect the terms of "
                                "use of each chart source.";                     // 7.5 step 2
const char* const kReplace = "A chart with this name exists. Replace it?";       // 7.5 step 3
const char* const kNameDefaultEsri = "Area 50.00N 10.00E - ESRI ArcGIS.Imagery"; // areaPlace + " - " + name

std::string tsvLine(const std::vector<std::string>& fields) {
    std::string out;
    for (const std::string& f : fields)
        out += (out.empty() ? "" : "\t") + f;
    return out + "\n";
}
// A CR 2 `estimate` record: guid, zoom, tiles, bboxTiles, sharePresent, shareMarkedMissing, bytesToDownload,
// assumed, bytesOnDisk.
std::string estimateRecord(const char* guid, int zoom, long long tiles, long long toDownload, bool assumed,
                           long long onDisk) {
    return tsvLine({"estimate", guid, std::to_string(zoom), std::to_string(tiles), std::to_string(tiles),
                    "0.0", "0.0", std::to_string(toDownload), assumed ? "1" : "0", std::to_string(onDisk)});
}
// 1 950 tiles, 38 000 000 bytes to download, 70 000 000 on disk: the example of over three records.
std::string estimate1950(bool assumed = false) {
    return estimateRecord(kEsri, 12, 1000, 20000000, false, 40000000) +
           estimateRecord(kEsri, 13, 500, 10000000, assumed, 20000000) +
           estimateRecord(kSeamarkLayer, 12, 450, 8000000, false, 10000000);
}
// One record with the totals the test wants (tiles, to download, on disk).
std::string estimateOf(long long tiles, long long toDownload, long long onDisk) {
    return estimateRecord(kEsri, 12, tiles, toDownload, false, onDisk);
}

// Answers the modeless questions (class Question) of the Download flow from a timer that runs in the
// test's loop: the question is found among the top-level windows and answered by its button event. Each
// answer is used once, in order; a question with no answer left is declined and counted.
class Answerer {
public:
    struct Answer {
        bool yes;
        bool tick = false; // the notice: tick its check box before OK
    };
    explicit Answerer(std::vector<Answer> answers)
        : answers_(std::move(answers)), alive_(std::make_shared<bool>(true)), timer_(&handler_) {
        handler_.Bind(wxEVT_TIMER, [this](wxTimerEvent&) { poll(); });
        timer_.Start(30);
    }
    ~Answerer() {
        *alive_ = false;
        timer_.Stop();
    }
    std::vector<std::string> shown; // the message of every question, in order
    std::string checkBoxLabel;      // of the rich dialog
    int unexpected = 0;             // questions asked with no answer left

private:
    Answer next() {
        if (answers_.empty()) {
            ++unexpected;
            return {false};
        }
        const Answer a = answers_.front();
        answers_.erase(answers_.begin());
        return a;
    }
    void answered() { // the timer stays off until the answered dialog is gone
        timer_.Stop();
        wxTheApp->CallAfter([this, alive = alive_] {
            if (*alive)
                timer_.Start(30);
        });
    }
    void poll() {
        for (auto* node = wxTopLevelWindows.GetFirst(); node; node = node->GetNext()) {
            auto* rich = dynamic_cast<Question*>(node->GetData());
            if (!rich || !rich->IsShown())
                continue;
            const Answer a = next();
            std::vector<wxStaticText*> texts;
            collect(rich, texts);
            wxString text = texts.at(0)->GetLabel();
            text.Replace("\n", " "); // Wrap breaks the label into lines
            shown.push_back(text.utf8_string());
            std::vector<wxCheckBox*> boxes;
            collect(rich, boxes);
            if (!boxes.empty()) {
                checkBoxLabel = boxes[0]->GetLabel().utf8_string();
                if (a.tick)
                    boxes[0]->SetValue(true);
            }
            const int id =
                rich->FindWindow(wxID_OK) ? (a.yes ? wxID_OK : wxID_CANCEL) : (a.yes ? wxID_YES : wxID_NO);
            wxCommandEvent e(wxEVT_BUTTON, id);
            e.SetEventObject(rich->FindWindow(id));
            rich->GetEventHandler()->ProcessEvent(e);
            answered();
            return;
        }
    }
    std::vector<Answer> answers_;
    std::shared_ptr<bool> alive_;
    wxEvtHandler handler_;
    wxTimer timer_;
};

struct IniFile { // a key=value file read back as lines
    std::vector<std::string> lines;
    std::string get(const std::string& key) const {
        for (const std::string& l : lines)
            if (l.rfind(key + "=", 0) == 0)
                return l.substr(key.size() + 1);
        return "<missing>";
    }
};
IniFile readIni(const fs::path& p) {
    IniFile f;
    std::ifstream in(p, std::ios::binary);
    for (std::string line; std::getline(in, line);)
        f.lines.push_back(line);
    return f;
}

class DownloadTest : public PanelTest {
protected:
    fs::path jobsRoot() const { return root_ / "private" / "plugins" / "chartgrabber" / "jobs"; }
    std::vector<fs::path> jobDirs() const {
        std::vector<fs::path> out;
        if (fs::exists(jobsRoot()))
            for (const auto& e : fs::directory_iterator(jobsRoot()))
                if (e.is_directory())
                    out.push_back(e.path());
        return out;
    }
    std::string chartFolder() const { return (root_ / "charts d'x").string(); }
    wxSpinCtrl* threads() { return all<wxSpinCtrl>().at(0); }
    wxButton* chooseFolder() { return labelled<wxButton>("Choose..."); }
    // The page up to a ready Download: the sources and the estimate are in.
    void ready() {
        makeDialog();
        waitEstimate();
    }
    // The press, then the loop runs until the questions are answered and gone (the Answerer answers them).
    void download() {
        click(labelled<wxButton>("Download"));
        EXPECT_TRUE(runUntil([] { return !anyQuestion(); })) << "a question stayed open";
    }
    static bool anyQuestion() {
        for (auto* n = wxTopLevelWindows.GetFirst(); n; n = n->GetNext())
            if (dynamic_cast<Question*>(n->GetData()))
                return true;
        return false;
    }
    bool showsMatching(const std::string& regex) { // a shown static text that matches the whole regex
        for (wxStaticText* s : all<wxStaticText>())
            if (s->IsShown() && std::regex_match(u(flat(s)), std::regex(regex)))
                return true;
        return false;
    }
    std::string shownTexts() { // for a failure message: what the page really shows
        std::string out;
        for (wxStaticText* s : all<wxStaticText>())
            if (s->IsShown() && !s->GetLabel().empty())
                (out += "[") += u(flat(s)) + "] ";
        return out;
    }
    static std::string escaped(const std::string& text) { // ECMAScript regex metacharacters
        return std::regex_replace(text, std::regex("[.+*?()\\[\\]{}|^$\\\\]"), "\\$&");
    }
    wxButton* downloadButton() { return labelled<wxButton>("Download"); }
    void stopJob() {
        sources_->holdRun(false);
        ASSERT_TRUE(runUntil([&] { return !jobs_->busy(); })) << "the job did not end";
    }
    void useRoute() { // two points on the equator, 1 degree apart
        addRoute("aaaaaaaa-1111-4111-8111-000000000001", kNameCyrillic, {{0.0, 10.0}, {0.0, 11.0}});
    }
};

// ---- the estimate ----

TEST_F(DownloadTest, TheEstimateLineShowsTheSumsAndTheCliGetsTheArgumentsOfSpec81) {
    sources_->estimateOut(estimate1950());
    ready();
    EXPECT_TRUE(
        std::regex_match(estimateLine(), std::regex("About 1 950 tiles, 38 MB to download, 70 MB on disk "
                                                    "\\([0-9]+(\\.[0-9])? (MB|GB) free\\)")))
        << estimateLine();
    // Base and seamarks (the satellite set ticks them), --maps / --cache from, no network flag.
    EXPECT_EQ(sources_->estimateArgv(),
              std::vector<std::string>{
                  "estimate --format=tsv --progress=lines --cache " + (root_ / "private").string() +
                  "/plugins/chartgrabber/cache --source " + kEsri + " --overlay " + kSeamarkLayer +
                  " --bbox 5.00000000,45.00000000,15.00000000,55.00000000 --zooms 8-14"});
    EXPECT_EQ(sources_->estimateLcAll(), std::vector<std::string>{"C.UTF-8"});
    EXPECT_EQ(status(), "");
    EXPECT_EQ(u(nameField()->GetValue()), kNameDefaultEsri);
    EXPECT_TRUE(downloadButton()->IsEnabled());
    EXPECT_EQ(sources_->probes(), 1) << "the estimate uses the cached probe of the sources run";
}

// with a folder picked in Preferences the estimate and the run get --maps (a path with a
// space, a quote and non-ASCII bytes); the empty case is pinned by the tests above and below.
TEST_F(DownloadTest, APickedMapsFolderIsPassedToTheEstimateAndTheRun) {
    const std::string maps = (root_ / "maps \xD1\x91 d'x").string();
    fs::create_directories(fs::path(maps) / "Sea" / "Extra"); // the user ruling: a nested source counts
    std::ofstream(fs::path(maps) / "Sea" / "Extra" / "OpenSeaMap.zmp")
        << "x"; // --maps is passed only when the folder holds a source
    setSettings([&](cg::Settings& s) { s.mapsDir = maps; });
    const std::string bundled = (root_ / "bundled d'x").string();
    store_->setBundledSourcesDir(bundled);
    store_->setDisclaimerShown();
    sources_->holdRun(true);
    sources_->estimateOut(estimate1950());
    ready();
    const std::string cache = (root_ / "private").string() + "/plugins/chartgrabber/cache";
    EXPECT_EQ(sources_->estimateArgv(),
              std::vector<std::string>{
                  "estimate --format=tsv --progress=lines --maps " + bundled + " --maps " + maps +
                  " --cache " + cache + " --source " + kEsri + " --overlay " + kSeamarkLayer +
                  " --bbox 5.00000000,45.00000000,15.00000000,55.00000000 --zooms 8-14"});
    Answerer answers({});
    download();
    ASSERT_TRUE(runUntil([&] { return !sources_->runArgv().empty(); }));
    ASSERT_EQ(jobDirs().size(), 1u);
    EXPECT_EQ(sources_->runArgv(),
              std::vector<std::string>{"run --job " + (jobDirs()[0] / "job.ini").string() +
                                       " --format=tsv --progress=lines --maps " + bundled + " --maps " +
                                       maps + " --cache " + cache});
    sources_->holdRun(false);
    destroyDialog();
}

TEST_F(DownloadTest, SizeAssumedIsAddedOnlyWhenTheCliSaysAssumed) {
    struct Row {
        const char* name;
        bool assumed;
        const char* tail; // regex of the end of the line, after "70 MB on disk "
    };
    for (const Row& row : {Row{"measured", false, "\\([0-9]+(\\.[0-9])? (MB|GB) free\\)"},
                           Row{"assumed", true, "\\([0-9]+(\\.[0-9])? (MB|GB) free\\) \\(rough size\\)"}}) {
        SCOPED_TRACE(row.name);
        sources_->estimateOut(estimate1950(row.assumed));
        ready();
        const std::string line = estimateLine();
        const std::string base = "About 1 950 tiles, 38 MB to download, 70 MB on disk ";
        ASSERT_EQ(line.rfind(base, 0), 0u) << line;
        const std::string tail = line.substr(base.size());
        EXPECT_TRUE(std::regex_match(tail, std::regex(row.tail))) << tail;
        destroyDialog();
    }
}

TEST_F(DownloadTest, ChangesAreDebouncedAndOneRunGetsTheLastInputs) {
    sources_->estimateOut(estimate1950());
    ready();
    ASSERT_EQ(sources_->estimateArgv().size(), 1u);
    // Three changes 100 ms apart: one run, started 400 ms after the last one, with the last zoom list.
    const auto last = [&] {
        // The debounce under test is a duration: the changes must be 100 ms apart, so these pumps are timed.
        pickZooms(9, 9);
        runUntil([] { return false; }, 100);
        pickZooms(9, 10);
        runUntil([] { return false; }, 100);
        pickZooms(9, 11);
        return std::chrono::steady_clock::now();
    }();
    EXPECT_EQ(estimateLine(), "") << "the old estimate is void at once";
    EXPECT_TRUE(shows(kEstimating));
    // Absence-wait: no run may start inside the 400 ms debounce, so the time is the subject here.
    runUntil([] { return false; }, 250);
    const auto idle = std::chrono::steady_clock::now() - last;
    if (idle < std::chrono::milliseconds(380)) { // a stalled test thread proves nothing about the debounce
        EXPECT_EQ(sources_->estimateArgv().size(), 1u) << "no run before the 400 ms are over";
    }
    waitEstimate();
    const auto took = std::chrono::steady_clock::now() - last;
    ASSERT_EQ(sources_->estimateArgv().size(), 2u);
    EXPECT_NE(sources_->estimateArgv()[1].find(" --zooms 9-11"), std::string::npos);
    EXPECT_GE(took, std::chrono::milliseconds(380)) << "the debounce is 400 ms";
    // No wall-clock upper bound (it would depend on the host load): the run having started and the estimate
    // having ended within waitEstimate's timeout is what U-03 needs; the debounce itself is the lower
    // bound.
}

TEST_F(DownloadTest, ANewChangeCancelsTheRunningEstimateAndItsResultIsDiscarded) {
    const std::string first = estimateOf(111, 1000000, 2000000);  // "About 111 tiles, 1.0 MB..., 2.0 MB"
    const std::string second = estimateOf(222, 3000000, 4000000); // "About 222 tiles, 3.0 MB..., 4.0 MB"
    sources_->estimateOut(estimate1950());
    ready();
    sources_->estimateOut(first);
    sources_->holdEstimate(true);
    pickZooms(9, 10);
    waitEstimateRuns(2); // run 2 prints `first` and waits
    ASSERT_EQ(estimateLine(), "");
    ASSERT_TRUE(shows(kEstimating));
    sources_->estimateOut(second);
    pickZooms(9, 11); // 400 ms later: run 2 gets SIGTERM, run 3 starts
    waitEstimateRuns(3);
    ASSERT_GE(sources_->estimateArgv().size(), 3u);
    EXPECT_EQ(estimateLine(), "") << "the result of the cancelled run is not shown";
    EXPECT_EQ(status(), kEstimating);
    EXPECT_NE(sources_->estimateArgv()[2].find(" --zooms 9-11"), std::string::npos);
    sources_->holdEstimate(false);
    waitEstimate();
    EXPECT_TRUE(
        std::regex_match(estimateLine(), std::regex("About 222 tiles, 3\\.0 MB to download, 4\\.0 MB on disk "
                                                    "\\([0-9]+(\\.[0-9])? (MB|GB) free\\)")))
        << estimateLine();
    EXPECT_EQ(status(), "") << "the SIGTERM exit of run 2 is no error";
    EXPECT_EQ(sources_->estimateArgv().size(), 3u);
}

TEST_F(DownloadTest, ARouteIsEstimatedAsACorridorFromTheGpxFileOfTheJobsFolder) {
    useRoute();
    sources_->estimateOut(estimate1950());
    makeDialog();
    press(route());
    waitEstimate();
    const std::string gpx = (jobsRoot() / "estimate-route.gpx").string();
    const std::string expected = "estimate --format=tsv --progress=lines --cache " +
                                 (root_ / "private").string() + "/plugins/chartgrabber/cache --source " +
                                 kEsri + " --overlay " + kSeamarkLayer + " --route " + gpx +
                                 " --corridor-nm 2.0 --zooms 8-14";
    EXPECT_EQ(sources_->estimateArgv().back(), expected) << "a route is a corridor, never a --bbox";
    const std::string text = slurpText(gpx);
    EXPECT_NE(text.find("<name>\xD0\x90\xD0\xBB\xD1\x84\xD0\xB0 d&apos;x</name>"), std::string::npos) << text;
    EXPECT_NE(text.find("<rtept lat=\"0.00000000\" lon=\"10.00000000\"/>"), std::string::npos);
    EXPECT_NE(text.find("<rtept lat=\"0.00000000\" lon=\"11.00000000\"/>"), std::string::npos);
    const std::size_t runs = sources_->estimateArgv().size();
    click(wider()); // 2.5 NM: a new estimate
    waitEstimateRuns(runs + 1);
    waitEstimate();
    EXPECT_NE(sources_->estimateArgv().back().find(" --corridor-nm 2.5 "), std::string::npos);
    EXPECT_EQ(u(nameField()->GetValue()), std::string(kNameCyrillic) + " - " + kNameEsri)
        << "the route name is the place of the default name";
}

TEST_F(DownloadTest, EveryEstimateFailureShowsItsTextInPlaceOfTheLineAndDownloadStaysOff) {
    const fs::path fx = fs::path(CG_FIXTURES_DIR) / "cli-2026.10.6";
    struct Row {
        const char* name;
        int exitCode;
        std::string err;
        std::string expected;
    };
    const std::vector<Row> rows = {
        // 3 = refused: estimate-refused-3395.stderr of the pinned CLI, through refusalText
        {"refused", 3, slurpText(fx / "estimate-refused-3395.stderr"),
         "This chart source cannot be made into an OpenCPN chart: the projection of \"Yandex - Satellite\" "
         "is "
         "EPSG:3395; only EPSG:3857 (Web Mercator) can be exported."},
        //

        {"no tile", 3, "error: the area holds no tile at the chosen zooms\n",
         "The area has no tiles at the chosen detail."},
        // usage-unknown-option.stderr: exit 2 with an unknown option is a plain failure
        {"exit 2 unknown option", 2, slurpText(fx / "usage-unknown-option.stderr"),
         "Cannot work out the size: Unknown option 'nonsense'."},
        // usage-bad-zooms.stderr: exit 2 without "format"
        {"usage", 2, slurpText(fx / "usage-bad-zooms.stderr"),
         "Cannot work out the size: --zooms: \"x\" is not a zoom list (0-based, such as 8-12,14; levels 0 to "
         "23)"},
        {"no message", 7, "", "Cannot work out the size (error 7)."},
    };
    sources_->estimateOut(estimate1950());
    ready();
    ASSERT_TRUE(downloadButton()->IsEnabled());
    int n = 1;
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        sources_->estimateExit(row.exitCode);
        sources_->estimateErr(row.err);
        if (n % 2)
            pickZooms(9, 10);
        else
            pickZooms(9, 12); // a different input every time
        ++n;
        waitEstimate();
        EXPECT_TRUE(shows(row.expected.c_str())) << "the status reason";
        EXPECT_EQ(estimateLine(), "");
        EXPECT_FALSE(downloadButton()->IsEnabled());
    }
    // The next successful estimate clears the failure.
    sources_->estimateExit(0);
    sources_->estimateErr("");
    pickZooms(9, 13);
    waitEstimate();
    EXPECT_EQ(status(), "");
    EXPECT_NE(estimateLine(), "");
    EXPECT_TRUE(downloadButton()->IsEnabled());
}

TEST_F(DownloadTest, ACliThatDisappearsAfterTheSourcesRunShowsTheNotFoundTextAsTheReason) {
    sources_->estimateOut(estimate1950());
    ready();
    ASSERT_TRUE(downloadButton()->IsEnabled());
    fs::remove(fs::path(sources_->cli()));
    pickZooms(9, 10);
    waitEstimate();
    EXPECT_EQ(status(), kNotFound);
    EXPECT_EQ(estimateLine(), "");
    EXPECT_FALSE(downloadButton()->IsEnabled());
}

// ---- the enable order ----

TEST_F(DownloadTest, TheReasonsFollowTheOrderEstimateNameLargeDiskAndTheirBoundaries) {
    struct Row {
        const char* name;
        std::string estimate;
        std::string chartDir; // "" = the fixture's
        std::string typed;    // typed into Name ("" = leave the default)
        std::string reason;   // regex of the status; "" = none
        bool enabled;
    };
    const std::string free = "[0-9]+(\\.[0-9])? (MB|GB)";
    const std::string cache = escaped((root_ / "private").string() + "/plugins/chartgrabber/cache");
    // checkPreflight: TooLarge = tiles > 200 000 or bytesOnDisk > 2 000 000 000 (200 000 / 2e9 are fine). A
    // folder whose free space is unknown (a relative path, ) skips its disk check, so the 2e9 row
    // does not depend on the free space of the test machine.
    const std::vector<Row> rows = {
        {"200000 tiles are fine", estimateOf(200000, 1000, 1000), "", "", "", true},
        {"200001 tiles", estimateOf(200001, 1000, 1000), "", "", escaped(kTooLarge), false},
        {"2e9 bytes on disk are fine", estimateOf(10, 1000, 2000000000), "cg-no-such-folder/sub", "", "",
         true},
        {"2e9+1 bytes on disk", estimateOf(10, 1000, 2000000001), "cg-no-such-folder/sub", "",
         escaped(kTooLarge), false},
        // the name comes before the size check
        {"blank name beats too large", estimateOf(200001, 1000, 1000), "", "   ", escaped(kEnterName), false},
        {"blank name", estimateOf(10, 1000, 1000), "", "   ", escaped(kEnterName), false},
        // cache folder (the chart folder's free space is unknown, so only the cache is checked): need =
        // bytesToDownload + 512 MiB = 5 000 000 536 870 912 -> "5000000.5 GB"
        {"cache disk", estimateOf(10, 5000000000000000LL, 1000), "cg-no-such-folder/sub", "",
         "Needs about 5000000\\.5 GB, only " + free + " free on " + cache, false},
        // chart and cache on one file system: the sum is reported on the chart folder (checkPreflight)
        {"one file system", estimateOf(10, 5000000000000000LL, 1000), "", "",
         "Needs about 5000000\\.5 GB, only " + free + " free on " + escaped((root_ / "charts d'x").string()),
         false},
        // chart folder on /proc (0 bytes free): need = 2 x 1e9 + 512 MiB = 2 536 870 912 -> "2.5 GB"
        {"chart disk", estimateOf(10, 1000, 1000000000), "/proc/cg charts", "",
         "Needs about 2\\.5 GB, only 0\\.0 MB free on /proc/cg charts", false},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        sources_->estimateOut(row.estimate);
        setSettings([&](cg::Settings& s) { s.chartDir = (root_ / "charts d'x").string(); });
        ready();
        if (!row.chartDir.empty()) { // chosen with the folder button: a loaded setting must be absolute (8.7)
            ocpn_fake::dirResult = wxID_OK;
            ocpn_fake::dirSelected = wxString::FromUTF8(row.chartDir);
            click(chooseFolder());
        }
        if (!row.typed.empty())
            typeInto(nameField(), row.typed);
        if (row.reason.empty())
            EXPECT_EQ(status(), "");
        else
            EXPECT_TRUE(showsMatching(row.reason)) << row.reason << "\nshown: " << shownTexts();
        EXPECT_EQ(downloadButton()->IsEnabled(), row.enabled);
        destroyDialog();
    }
    ocpn_fake::dirResult = wxID_CANCEL;
    ocpn_fake::dirSelected.clear();
}

TEST_F(DownloadTest, AFolderWhoseFreeSpaceIsUnknownSaysSoAndItsDiskCheckIsSkipped) {
    // A relative folder has no existing ancestor in the walk of "free unknown", check skipped (free
    // = max), so even 1e9 bytes on disk do not stop Download.
    // (A loaded setting cannot be relative, the folder is chosen with the button.)
    sources_->estimateOut(estimateOf(10, 1000, 1000000000));
    ready();
    ocpn_fake::dirResult = wxID_OK;
    ocpn_fake::dirSelected = "cg-no-such-folder/sub";
    click(chooseFolder());
    EXPECT_EQ(estimateLine(), "About 10 tiles, 0.0 MB to download, 1.0 GB on disk (free unknown)");
    EXPECT_EQ(status(), "");
    EXPECT_TRUE(downloadButton()->IsEnabled());
    ocpn_fake::dirResult = wxID_CANCEL;
    ocpn_fake::dirSelected.clear();
}

TEST_F(DownloadTest, WhileTheEstimateRunsDownloadIsOffAndTheStatusSaysEstimating) {
    sources_->holdEstimate(true);
    makeDialog();
    ASSERT_TRUE(runUntil([&] { return !sources_->estimateArgv().empty(); }));
    EXPECT_EQ(status(), kEstimating);
    EXPECT_EQ(estimateLine(), "");
    EXPECT_FALSE(downloadButton()->IsEnabled());
    EXPECT_EQ(u(nameField()->GetValue()), kNameDefaultEsri) << "the name does not wait for the estimate";
    sources_->holdEstimate(false);
    waitEstimate();
    EXPECT_TRUE(downloadButton()->IsEnabled());
}

// ---- the name ----

TEST_F(DownloadTest, TheDefaultNameFollowsAreaAndSourceUntilTheUserTypesAndAnEmptiedFieldIsDefaultAgain) {
    ready();
    EXPECT_EQ(u(nameField()->GetValue()), kNameDefaultEsri);
    choose(chart(), 1); // Google
    EXPECT_EQ(u(nameField()->GetValue()), "Area 50.00N 10.00E - Google - Satellite");
    setViewport(40, 42, -2, 0);
    press(view());
    EXPECT_EQ(u(nameField()->GetValue()), "Area 41.00N 1.00W - Google - Satellite");
    typeInto(nameField(), "Solent");
    choose(chart(), 0);
    EXPECT_EQ(u(nameField()->GetValue()), "Solent") << "typed: no more default";
    typeInto(nameField(), "");
    choose(chart(), 1);
    EXPECT_EQ(u(nameField()->GetValue()), "Area 41.00N 1.00W - Google - Satellite")
        << "an emptied field is default again at the next change";
}

// ---- Download ----

TEST_F(DownloadTest, DownloadWritesTheJobFilesSavesTheChoiceStartsTheJobAndHidesTheDialog) {
    struct Row {
        const char* name;
        bool slow;
        int threads;    // the Advanced spin
        int jobThreads; // job.ini
    };
    for (const Row& row : {Row{"normal", false, 3, 3}, Row{"slow connection", true, 4, 1}}) {
        SCOPED_TRACE(row.name);
        fs::remove_all(jobsRoot());
        fs::remove_all(chartFolder());
        fs::remove(sources_->dir() / "run.log");
        sources_->holdRun(true);
        setSettings([&](cg::Settings& s) {
            s.slowConnection = row.slow;
            s.dataBudgetMb = 100000;
        });
        store_->setDisclaimerShown();
        sources_->estimateOut(estimate1950());
        ready();
        threads()->SetValue(row.threads);
        typeInto(nameField(), "  \xD0\x90\xD0\xBB\xD1\x84\xD0\xB0 d'x  "); // " Алфа d'x ": trimmed, no '
        Answerer answers({});
        download();
        EXPECT_TRUE(answers.shown.empty()) << "no question: no budget issue, disclaimer shown, no file";
        ASSERT_EQ(jobDirs().size(), 1u);
        const fs::path job = jobDirs()[0];
        EXPECT_TRUE(std::regex_match(job.filename().string(), std::regex("[0-9]{8}-[0-9]{6}")));
        const std::string out = chartFolder() + "/\xD0\x90\xD0\xBB\xD1\x84\xD0\xB0 dx.mbtiles";
        const std::string overlay = chartFolder() + "/\xD0\x90\xD0\xBB\xD1\x84\xD0\xB0 dx - Seamarks.mbtiles";
        EXPECT_EQ(slurpText(job / "job.ini"),
                  std::string("[Job]\nSource=") + kEsri + "\nOverlay=" + kSeamarkLayer +
                      "\nBBox=5.00000000,45.00000000,15.00000000,55.00000000\n"
                      "Zooms=8-14\nOut=" +
                      out + "\nOverlayOut=" + overlay +
                      "\nName=\xD0\x90\xD0\xBB\xD1\x84\xD0\xB0 dx\n[Download]\nThreads=" +
                      std::to_string(row.jobThreads) + "\n");
        EXPECT_FALSE(fs::exists(job / "route.gpx"));
        const IniFile chart = readIni(job / "chart.ini");
        EXPECT_EQ(chart.get("Name"), "\xD0\x90\xD0\xBB\xD1\x84\xD0\xB0 dx");
        EXPECT_EQ(chart.get("SourceGuid"), kEsri);
        EXPECT_EQ(chart.get("OverlayGuid"), kSeamarkLayer);
        EXPECT_EQ(chart.get("SourceName"), kNameEsri);
        EXPECT_EQ(chart.get("BBox"), "5.00000000,45.00000000,15.00000000,55.00000000");
        EXPECT_EQ(chart.get("Zooms"), "8-14");
        EXPECT_EQ(chart.get("File0"), out);
        EXPECT_EQ(chart.get("File1"), overlay);
        EXPECT_EQ(chart.get("State"), "running");
        EXPECT_TRUE(
            std::regex_match(chart.get("Created"), std::regex("[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9:]{8}")));
        // the job runs: runArguments of for the tsv level
        ASSERT_TRUE(runUntil([&] { return !sources_->runArgv().empty(); }));
        EXPECT_EQ(sources_->runArgv(),
                  std::vector<std::string>{"run --job " + (job / "job.ini").string() +
                                           " --format=tsv --progress=lines --cache " +
                                           (root_ / "private").string() + "/plugins/chartgrabber/cache" +
                                           (row.slow ? " --threads 1" : "")});
        EXPECT_FALSE(dialog_->IsShown()) << "onStarted hides the dialog";
        EXPECT_TRUE(jobs_->busy());
        // LastChoice and the settings of the page (step 8)
        const ConfigStore::LastChoice last = store_->loadLastChoice();
        EXPECT_EQ(last.sourceGuid, kEsri);
        EXPECT_TRUE(last.seamarks);
        EXPECT_EQ(last.bands, (std::set<cg::Band>{cg::Band::Passage, cg::Band::Coastal}));
        EXPECT_EQ(last.advancedZooms, "8-14");
        EXPECT_EQ(last.areaKind, cg::AreaKind::View);
        EXPECT_EQ(store_->loadSettings().threads, row.threads) << "the page's value, also in slow mode";
        EXPECT_EQ(store_->loadSettings().corridorNm, 2.0);
        EXPECT_EQ(store_->loadSettings().slowConnection, row.slow) << "the loaded settings are kept";
        sources_->holdRun(false);
        destroyDialog();
    }
}

TEST_F(DownloadTest, ARouteDownloadWritesRouteGpxAndTheCorridorAndSavesTheRouteChoice) {
    useRoute();
    store_->setDisclaimerShown();
    sources_->holdRun(true);
    sources_->estimateOut(estimate1950());
    makeDialog();
    press(route());
    for (int i = 0; i < 3; ++i)
        click(wider()); // 3.5 NM
    waitEstimate();
    ASSERT_EQ(status(), "");
    Answerer answers({});
    download();
    ASSERT_EQ(jobDirs().size(), 1u);
    const fs::path job = jobDirs()[0];
    const std::string name = "\xD0\x90\xD0\xBB\xD1\x84\xD0\xB0 dx - ESRI ArcGIS.Imagery"; // ' removed
    const std::string out = chartFolder() + "/" + name + ".mbtiles";
    EXPECT_EQ(slurpText(job / "job.ini"),
              std::string("[Job]\nSource=") + kEsri + "\nOverlay=" + kSeamarkLayer +
                  "\nRoute=" + (job / "route.gpx").string() + "\nCorridorNm=3.5\nZooms=8-14\nOut=" + out +
                  "\nOverlayOut=" + chartFolder() + "/" + name + " - Seamarks.mbtiles\nName=" + name +
                  "\n[Download]\nThreads=2\n");
    const std::string gpx = slurpText(job / "route.gpx");
    EXPECT_NE(gpx.find("<rtept lat=\"0.00000000\" lon=\"10.00000000\"/>"), std::string::npos) << gpx;
    EXPECT_NE(gpx.find("<rtept lat=\"0.00000000\" lon=\"11.00000000\"/>"), std::string::npos);
    EXPECT_EQ(readIni(job / "chart.ini").get("BBox"), "9.97088573,-0.02911427,11.02911427,0.02911427")
        << "routeBounds of the route with the 3.5 NM corridor";
    const ConfigStore::LastChoice last = store_->loadLastChoice();
    EXPECT_EQ(last.areaKind, cg::AreaKind::Route);
    EXPECT_EQ(last.routeGuid, "aaaaaaaa-1111-4111-8111-000000000001");
    EXPECT_FALSE(last.rectangle.has_value());
    EXPECT_EQ(store_->loadSettings().corridorNm, 3.5);
    sources_->holdRun(false);
}

TEST_F(DownloadTest, ARectangleIsSavedInTheLastChoice) {
    store_->setDisclaimerShown();
    sources_->holdRun(true);
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::Rectangle, cg::BBox{-1.5, 50.0, -1.1, 51.5});
    waitEstimate();
    ASSERT_EQ(status(), "");
    EXPECT_EQ(u(nameField()->GetValue()), "Area 50.75N 1.30W - ESRI ArcGIS.Imagery");
    Answerer answers({});
    download();
    ASSERT_EQ(jobDirs().size(), 1u);
    const ConfigStore::LastChoice last = store_->loadLastChoice();
    EXPECT_EQ(last.areaKind, cg::AreaKind::Rectangle);
    ASSERT_TRUE(last.rectangle.has_value());
    EXPECT_EQ(last.rectangle->west, -1.5);
    EXPECT_EQ(last.rectangle->north, 51.5);
    EXPECT_NE(
        slurpText(jobDirs()[0] / "job.ini").find("BBox=-1.50000000,50.00000000,-1.10000000,51.50000000\n"),
        std::string::npos);
    sources_->holdRun(false);
}

TEST_F(DownloadTest, OverBudgetAsksOnlyInSlowModeAndNoStartsNothing) {
    struct Row {
        const char* name;
        bool slow;
        bool yes;
        bool starts;
        std::size_t questions;
    };
    // 38 MB to download against a 1 MB limit; the limit applies only with the slow connection.
    for (const Row& row : {Row{"slow, No", true, false, false, 1}, Row{"slow, Yes", true, true, true, 1},
                           Row{"not slow", false, false, true, 0}}) {
        SCOPED_TRACE(row.name);
        fs::remove_all(jobsRoot());
        sources_->holdRun(true);
        setSettings([&](cg::Settings& s) {
            s.slowConnection = row.slow;
            s.dataBudgetMb = 1;
        });
        store_->setDisclaimerShown();
        sources_->estimateOut(estimate1950());
        ready();
        ASSERT_TRUE(downloadButton()->IsEnabled()) << "OverBudget does not disable Download";
        Answerer answers(std::vector<Answerer::Answer>{{row.yes}});
        download();
        ASSERT_EQ(answers.shown.size(), row.questions);
        if (row.questions) {
            EXPECT_EQ(answers.shown[0], "This download is about 38 MB. Your limit is 1 MB. Download anyway?");
        }
        EXPECT_EQ(answers.unexpected, 0);
        EXPECT_EQ(jobDirs().size(), row.starts ? 1u : 0u);
        EXPECT_EQ(dialog_->IsShown(), !row.starts);
        sources_->holdRun(false);
        destroyDialog();
    }
}

TEST_F(DownloadTest, TheDisclaimerIsAskedOnceWithDontShowAgainAndCancelStartsNothing) {
    sources_->holdRun(true);
    ready();
    {
        Answerer answers(std::vector<Answerer::Answer>{{false}}); // Cancel
        download();
        ASSERT_EQ(answers.shown.size(), 1u);
        EXPECT_EQ(answers.shown[0], kDisclaimer);
        EXPECT_EQ(answers.checkBoxLabel, "Don't show again");
        EXPECT_EQ(jobDirs().size(), 0u);
        EXPECT_FALSE(store_->disclaimerShown());
        EXPECT_TRUE(dialog_->IsShown());
    }
    {
        Answerer answers(
            std::vector<Answerer::Answer>{{true, false}}); // OK without the tick: asked again next time
        download();
        EXPECT_EQ(answers.shown.size(), 1u);
        EXPECT_EQ(jobDirs().size(), 1u);
        EXPECT_FALSE(store_->disclaimerShown());
    }
    sources_->holdRun(false);
    destroyDialog();
    fs::remove_all(jobsRoot());
    {
        makeDialog();
        waitEstimate();
        Answerer answers(std::vector<Answerer::Answer>{{true, true}}); // OK with the tick
        download();
        EXPECT_EQ(answers.shown.size(), 1u);
        EXPECT_TRUE(store_->disclaimerShown());
        EXPECT_EQ(jobDirs().size(), 1u);
    }
    sources_->holdRun(false);
    ASSERT_TRUE(runUntil([&] { return !jobs_->busy(); }));
    destroyDialog();
    fs::remove_all(jobsRoot());
    {
        makeDialog();
        waitEstimate();
        Answerer answers({});
        download();
        EXPECT_TRUE(answers.shown.empty()) << "ticked: never asked again";
        EXPECT_EQ(jobDirs().size(), 1u);
    }
}

TEST_F(DownloadTest, AnExistingChartFileAsksReplaceAndNoStartsNothingYesRunsWithOverwrite) {
    struct Row {
        const char* name;
        const char* existing; // file name in the chart folder
        bool seamarks;
        bool yes;
        bool asked;
    };
    const char* const base = "T.mbtiles";
    const char* const over = "T - Seamarks.mbtiles";
    const std::vector<Row> rows = {
        {"base exists, No", base, true, false, true},
        {"base exists, Yes", base, true, true, true},
        {"overlay file exists, Yes", over, true, true, true},
        {"overlay file exists but no seamarks", over, false, false, false},
        {"none exists", "other.mbtiles", true, false, false},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        fs::remove_all(jobsRoot());
        fs::remove_all(chartFolder());
        fs::create_directories(chartFolder());
        std::ofstream(fs::path(chartFolder()) / row.existing) << "x";
        sources_->holdRun(true);
        store_->setDisclaimerShown();
        ready();
        if (seamarks()->GetValue() != row.seamarks)
            tick(seamarks());
        typeInto(nameField(), "T");
        waitEstimate();
        Answerer answers(std::vector<Answerer::Answer>{{row.yes}});
        download();
        ASSERT_EQ(answers.shown.size(), row.asked ? 1u : 0u);
        if (row.asked) {
            EXPECT_EQ(answers.shown[0], kReplace);
        }
        const bool starts = !row.asked || row.yes;
        EXPECT_EQ(jobDirs().size(), starts ? 1u : 0u);
        if (starts) {
            ASSERT_TRUE(runUntil([&] { return !sources_->runArgv().empty(); }));
            const std::string args = sources_->runArgv().back();
            EXPECT_EQ(args.find(" --overwrite") != std::string::npos, row.asked) << args;
        }
        EXPECT_EQ(dialog_->IsShown(), !starts);
        sources_->holdRun(false);
        destroyDialog();
        fs::remove(sources_->dir() / "run.log");
    }
}

// All three questions in a row: the order, the texts, what each answer leaves behind.
TEST_F(DownloadTest, TheThreeQuestionsComeInOrderAndTheLastAnswerDecidesTheStart) {
    for (const bool last : {true, false}) {
        SCOPED_TRACE(last ? "replace: Yes" : "replace: No");
        fs::remove_all(jobsRoot());
        fs::remove_all(chartFolder());
        fs::create_directories(chartFolder());
        std::ofstream(fs::path(chartFolder()) / "T.mbtiles") << "x";
        config_->DeleteAll(); // the disclaimer is asked again
        setSettings([&](cg::Settings& s) {
            s.chartDir = chartFolder();
            s.slowConnection = true;
            s.dataBudgetMb = 1;
        });
        sources_->holdRun(true);
        sources_->estimateOut(estimate1950());
        ready();
        typeInto(nameField(), "T");
        waitEstimate();
        Answerer answers(std::vector<Answerer::Answer>{{true}, {true, true}, {last}});
        download();
        ASSERT_EQ(answers.shown.size(), 3u);
        EXPECT_EQ(answers.shown[0], "This download is about 38 MB. Your limit is 1 MB. Download anyway?");
        EXPECT_EQ(answers.shown[1], kDisclaimer);
        EXPECT_EQ(answers.shown[2], kReplace);
        EXPECT_EQ(answers.unexpected, 0);
        EXPECT_TRUE(store_->disclaimerShown()) << "ticked OK of step 2 is kept whatever step 3 says";
        EXPECT_EQ(jobDirs().size(), last ? 1u : 0u);
        EXPECT_TRUE(panel()->IsEnabled()) << "the page is enabled again";
        if (last) {
            ASSERT_TRUE(runUntil([&] { return !sources_->runArgv().empty(); }));
            EXPECT_NE(sources_->runArgv().back().find(" --overwrite"), std::string::npos);
        }
        sources_->holdRun(false);
        destroyDialog();
        fs::remove(sources_->dir() / "run.log");
    }
}

// The question is a window of its own: the page waits (disabled), a second press opens no second question,
// the answer re-enables the page and the flow goes on.
TEST_F(DownloadTest, WhileAQuestionIsOpenThePageIsDisabledAndASecondPressOpensNoSecondOne) {
    fs::remove_all(chartFolder());
    fs::create_directories(chartFolder());
    std::ofstream(fs::path(chartFolder()) / "T.mbtiles") << "x";
    store_->setDisclaimerShown();
    sources_->holdRun(true);
    ready();
    typeInto(nameField(), "T");
    waitEstimate();
    auto questions = [] {
        std::vector<Question*> out;
        for (auto* n = wxTopLevelWindows.GetFirst(); n; n = n->GetNext())
            if (auto* q = dynamic_cast<Question*>(n->GetData()))
                out.push_back(q);
        return out;
    };
    click(downloadButton());
    ASSERT_EQ(questions().size(), 1u);
    EXPECT_FALSE(panel()->IsEnabled());
    EXPECT_TRUE(dialog_->IsEnabled()) << "OpenCPN's windows and the dialog itself stay usable";
    click(downloadButton());
    EXPECT_EQ(questions().size(), 1u) << "raised, not repeated";
    EXPECT_TRUE(jobDirs().empty());
    wxButton* yes = dynamic_cast<wxButton*>(questions()[0]->FindWindow(wxID_YES));
    ASSERT_NE(yes, nullptr);
    click(yes);
    EXPECT_TRUE(panel()->IsEnabled());
    ASSERT_EQ(jobDirs().size(), 1u);
    ASSERT_TRUE(runUntil([&] { return questions().empty(); }));
    sources_->holdRun(false);
}

// What DeInit does: the main dialog is deleted while the replace question is open.
TEST_F(DownloadTest, DeletingTheDialogWithTheReplaceQuestionOpenLeavesNoQuestionAndStartsNothing) {
    fs::remove_all(chartFolder());
    fs::create_directories(chartFolder());
    std::ofstream(fs::path(chartFolder()) / "T.mbtiles") << "x";
    store_->setDisclaimerShown();
    sources_->holdRun(true);
    ready();
    typeInto(nameField(), "T");
    waitEstimate();
    click(downloadButton());
    ASSERT_TRUE(anyQuestion());
    destroyDialog();
    // Absence-wait: nothing may appear after the delete; drain every event already pending, then look.
    for (int i = 0; i < 1000 && loop_->DispatchTimeout(20) == 1; ++i) {
    }
    EXPECT_FALSE(anyQuestion());
    EXPECT_TRUE(jobDirs().empty());
    EXPECT_TRUE(sources_->runArgv().empty());
    sources_->holdRun(false);
}

// an answered question is hidden and waits for its deferred Destroy; a press in that gap asks again.
TEST_F(DownloadTest, ADownloadPressAfterANoBeforeTheQuestionIsDestroyedAsksAgain) {
    fs::remove_all(chartFolder());
    fs::create_directories(chartFolder());
    std::ofstream(fs::path(chartFolder()) / "T.mbtiles") << "x";
    store_->setDisclaimerShown();
    ready();
    typeInto(nameField(), "T");
    waitEstimate();
    auto firstQuestion = [] {
        for (auto* n = wxTopLevelWindows.GetFirst(); n; n = n->GetNext())
            if (auto* q = dynamic_cast<Question*>(n->GetData()))
                return wxWeakRef<Question>(q);
        return wxWeakRef<Question>();
    };
    click(downloadButton());
    wxWeakRef<Question> first = firstQuestion();
    ASSERT_TRUE(first);
    click(dynamic_cast<wxButton*>(first->FindWindow(wxID_NO)));
    ASSERT_TRUE(first) << "answered, hidden, not yet destroyed (no idle has run)";
    ASSERT_FALSE(first->IsShown());
    click(downloadButton());
    int shown = 0;
    for (auto* n = wxTopLevelWindows.GetFirst(); n; n = n->GetNext())
        if (auto* q = dynamic_cast<Question*>(n->GetData()))
            shown += q->IsShown() ? 1 : 0;
    EXPECT_EQ(shown, 1) << "the second press asked a new question";
    Answerer answers({});
    ASSERT_TRUE(runUntil([] { return !anyQuestion(); }));
}

// deleting the panel deletes an open question at once; its callback (which holds the panel) never
// runs.
TEST_F(DownloadTest, DeletingThePanelDeletesTheOpenQuestionAtOnce) {
    fs::remove_all(chartFolder());
    fs::create_directories(chartFolder());
    std::ofstream(fs::path(chartFolder()) / "T.mbtiles") << "x";
    store_->setDisclaimerShown();
    ready();
    typeInto(nameField(), "T");
    waitEstimate();
    click(downloadButton());
    wxWeakRef<Question> q;
    for (auto* n = wxTopLevelWindows.GetFirst(); n; n = n->GetNext())
        if (auto* c = dynamic_cast<Question*>(n->GetData()))
            q = c;
    ASSERT_TRUE(q);
    // What the dialog does: the panel goes, the dialog (the question's parent) lives on.
    static_cast<wxSimplebook*>(panel()->GetParent())->DeleteAllPages();
    EXPECT_FALSE(q) << "no idle ran in between";
    destroyDialog();
}

TEST_F(DownloadTest, AnUnusableChartFolderShowsCannotWriteAndStartsNothing) {
    // The chart folder lies under a regular file: Mkdir fails (step 5); the job directory is removed.
    std::ofstream(root_ / "a file") << "x";
    const std::string folder = (root_ / "a file" / "sub").string();
    setSettings([&](cg::Settings& s) { s.chartDir = folder; });
    store_->setDisclaimerShown();
    ready();
    ASSERT_TRUE(downloadButton()->IsEnabled());
    Answerer answers({});
    download();
    EXPECT_TRUE(shows(("Cannot write " + folder + ".").c_str()));
    EXPECT_TRUE(jobDirs().empty()) << "the job directory is removed";
    EXPECT_TRUE(sources_->runArgv().empty());
    EXPECT_TRUE(dialog_->IsShown());
    EXPECT_FALSE(jobs_->busy());
}

TEST_F(DownloadTest, WhileAJobRunsTheButtonSaysSoAndDownloadComesBackWhenItEnds) {
    store_->setDisclaimerShown();
    sources_->holdRun(true);
    ready();
    Answerer answers({});
    download();
    ASSERT_TRUE(jobs_->busy());
    EXPECT_TRUE(shows(kRunning)); // the dialog is hidden; nothing else re-checks the page
    EXPECT_FALSE(downloadButton()->IsShown());
    EXPECT_FALSE(downloadButton()->IsEnabled()) << "also disabled while the job runs";
    stopJob();
    EXPECT_FALSE(shows(kRunning));
    ASSERT_TRUE(downloadButton()->IsShown());
    EXPECT_TRUE(downloadButton()->IsEnabled()) << "the same inputs: Download is on again";
}

// ---- Advanced: threads and chart folder ----

TEST_F(DownloadTest, AdvancedThreadsStartAtTheSettingsValueAndTheChartFolderIsForThisChartOnly) {
    setSettings([&](cg::Settings& s) { s.threads = 3; });
    store_->setDisclaimerShown();
    sources_->holdRun(true);
    ready();
    EXPECT_EQ(threads()->GetValue(), 3);
    EXPECT_EQ(threads()->GetMin(), 1);
    EXPECT_EQ(threads()->GetMax(), 4);
    // Cancel keeps the folder; the dialog is started in the current one.
    ocpn_fake::dirResult = wxID_CANCEL;
    ocpn_fake::dirSelected = "/should/not/be/used";
    click(chooseFolder());
    EXPECT_EQ(u(ocpn_fake::dirInit), chartFolder());
    // OK takes the folder (with a space and a quote); the free space of that folder is shown.
    const std::string other = (root_ / "other d'x").string();
    ocpn_fake::dirResult = wxID_OK;
    ocpn_fake::dirSelected = wxString::FromUTF8(other);
    click(chooseFolder());
    waitEstimate();
    Answerer answers({});
    download();
    ASSERT_EQ(jobDirs().size(), 1u);
    EXPECT_EQ(readIni(jobDirs()[0] / "job.ini").get("Out"), other + "/" + kNameDefaultEsri + ".mbtiles");
    EXPECT_TRUE(fs::is_directory(other)) << "step 5 creates the chart folder";
    EXPECT_EQ(store_->loadSettings().chartDir, chartFolder()) << "never saved";
    sources_->holdRun(false);
    ocpn_fake::dirResult = wxID_CANCEL;
    ocpn_fake::dirSelected.clear();
}

TEST_F(DownloadTest, AResultThatArrivesAfterANewChangeIsDiscardedAndTheNewInputsAreEstimated) {
    sources_->estimateOut(estimate1950());
    ready();
    sources_->estimateOut(estimateOf(111, 1000000, 2000000));
    sources_->holdEstimate(true);
    pickZooms(9, 10);
    waitEstimateRuns(2); // run 2 prints its records and waits
    sources_->estimateOut(estimateOf(222, 3000000, 4000000));
    pickZooms(9, 11);              // the 400 ms timer is now waiting...
    sources_->holdEstimate(false); // ... and run 2 ends before it fires: its result is for old inputs
    bool sawOld = false;
    ASSERT_TRUE(runUntil([&] {
        sawOld = sawOld || estimateLine().rfind("About 111 tiles", 0) == 0;
        return !shows(kEstimating);
    }));
    EXPECT_FALSE(sawOld) << "the result of the old inputs was shown";
    EXPECT_EQ(estimateLine().rfind("About 222 tiles, 3.0 MB to download, 4.0 MB on disk (", 0), 0u)
        << estimateLine();
    EXPECT_EQ(sources_->estimateArgv().size(), 3u);
}

// Step 5: the job folder is the time stamp, `-2` is appended while the folder exists. Folders for the current
// second and the next ones exist before Download is pressed, so whichever second the press falls into, its
// plain name is taken.
TEST_F(DownloadTest, AJobFolderThatAlreadyExistsGetsTheSuffixMinusTwo) {
    store_->setDisclaimerShown();
    sources_->holdRun(true);
    ready();
    const std::time_t now = std::time(nullptr);
    std::set<std::string> before;
    for (int i = -1; i <= 4; ++i) {
        const std::time_t t = now + i;
        std::tm tm{};
        localtime_r(&t, &tm);
        char stamp[32];
        std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
        fs::create_directories(jobsRoot() / stamp);
        before.insert(stamp);
    }
    Answerer answers({});
    download();
    std::vector<fs::path> made;
    for (const fs::path& d : jobDirs())
        if (fs::exists(d / "job.ini"))
            made.push_back(d);
    ASSERT_EQ(made.size(), 1u);
    const std::string name = made[0].filename().string();
    ASSERT_TRUE(std::regex_match(name, std::regex("[0-9]{8}-[0-9]{6}-2"))) << name;
    EXPECT_TRUE(before.count(name.substr(0, name.size() - 2))) << "the plain name was taken";
    EXPECT_EQ(slurpText(made[0] / "chart.ini").find("Name="), 0u);
}

// ---- the range in the job, LastZooms and the cost lines ----

// The 10 `estimate` records of the captured run (cli-2026.10.6/run-continued.stdout, base + seamarks, zooms
// 9-13).
static std::string capturedEstimates() {
    std::istringstream in(slurpText(fs::path(CG_FIXTURES_DIR) / "cli-2026.10.6" / "run-continued.stdout"));
    std::string out;
    for (std::string line; std::getline(in, line);)
        if (line.rfind("estimate\t", 0) == 0)
            out += line + "\n";
    return out;
}

TEST_F(DownloadTest, TheRangeIsTheZoomListOfTheEstimateAndTheJobAndLastZoomsIsSaved) {
    struct Row {
        int lo, hi;
        const char* zooms; // formatZoomList
    };
    for (const Row& row : {Row{10, 12, "10-12"}, Row{10, 10, "10"}}) {
        SCOPED_TRACE(row.zooms);
        fs::remove_all(jobsRoot());
        fs::remove_all(chartFolder());
        sources_->holdRun(true);
        store_->setDisclaimerShown();
        sources_->estimateOut(estimate1950());
        ready();
        pickZooms(row.lo, row.hi);
        waitEstimate();
        EXPECT_NE(sources_->estimateArgv().back().find(std::string(" --zooms ") + row.zooms),
                  std::string::npos)
            << sources_->estimateArgv().back();
        Answerer answers({});
        download();
        ASSERT_EQ(jobDirs().size(), 1u);
        EXPECT_NE(slurpText(jobDirs()[0] / "job.ini").find(std::string("\nZooms=") + row.zooms + "\n"),
                  std::string::npos);
        EXPECT_EQ(store_->loadLastChoice().advancedZooms, row.zooms);
        sources_->holdRun(false);
        destroyDialog();
    }
}

TEST_F(DownloadTest, TheCostLinesFollowTheEstimateOfTheCapturedRecords) {
    sources_->estimateOut(capturedEstimates());
    ready();
    EXPECT_EQ(nextLevelLine(), "") << "range 8-14: the records end at 13, no record at 14";
    pickZooms(9, 13);
    waitEstimate();
    // centre latitude 50: z13 1:46k, z14 1:23k; 4 x (1795874 + 411675) = 8830196 bytes
    EXPECT_EQ(nextLevelLine(), "One more level (1:23k) would add about 8.7 MB.");
    EXPECT_TRUE(
        shows("5 levels. The finest, 1:46k, is about 0.4 MB of 0.4 MB; one more level (1:23k) would add "
              "about 8.7 MB."));
    // while estimating the main line is hidden and the Advanced line has no cost
    sources_->holdEstimate(true);
    pickZooms(9, 12);
    EXPECT_EQ(nextLevelLine(), "");
    EXPECT_TRUE(shows("4 levels."));
    sources_->holdEstimate(false);
    waitEstimate();
    EXPECT_EQ(nextLevelLine(), "One more level (1:46k) would add about 2.7 MB."); // 4 x (501463 + 182320)
    EXPECT_TRUE(
        shows("4 levels. The finest, 1:93k, is about 0.0 MB of 0.4 MB; one more level (1:46k) would add "
              "about 2.7 MB."));
    // after a failed estimate
    sources_->estimateExit(7);
    pickZooms(9, 11);
    waitEstimate();
    EXPECT_EQ(nextLevelLine(), "");
    EXPECT_TRUE(shows("3 levels."));
    sources_->estimateExit(0);
    // level 21 has no next level; a level without a record has no cost
    pickZooms(20, 21);
    waitEstimate();
    EXPECT_EQ(nextLevelLine(), "");
    EXPECT_TRUE(shows("2 levels."));
    pickZooms(9, 14);
    waitEstimate();
    EXPECT_EQ(nextLevelLine(), "") << "no record at 14";
    EXPECT_TRUE(shows("6 levels."));
}

TEST_F(DownloadTest, NoCostIsShownWhenTheNextLevelIsAboveTheSourceLimit) {
    // The 14th column of a `source` line is maxZoom; the second row has the limit one above the range's end.
    for (const int limit : {13, 14}) {
        SCOPED_TRACE(limit);
        sources_->rawRecords(std::string("source\t") + kEsri + "\tmap\t" + kNameEsri +
                             "\timage/jpeg\t3857\t0\tok\tfile.zmp\t\t\t\t\t" + std::to_string(limit) + "\n");
        sources_->estimateOut(capturedEstimates());
        ready();
        pickZooms(9, 13);
        waitEstimate();
        EXPECT_EQ(nextLevelLine(), limit == 13 ? "" : "One more level (1:23k) would add about 8.7 MB.");
        destroyDialog();
    }
}

// ---- layout and touch of the rows ----

TEST_F(DownloadTest, TheLargestPageStillFitsEightHundredByFourEightyWithTheLongestEstimateLine) {
    addRoute("cccccccc-0000-4000-8000-000000000003", "Long", {{45.0, 0.0}, {46.0, 10.0}});
    saveLast([&](ConfigStore::LastChoice& c) { c.sourceGuid = kGoogle; });
    sources_->estimateOut(estimateRecord(kGoogle, 12, 123456, 1234000000LL, true, 1234000000LL));
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::Route, std::nullopt);
    waitEstimate();
    ASSERT_EQ(estimateLine().rfind("About 123 456 tiles, 1.2 GB to download, 1.2 GB on disk (", 0), 0u)
        << estimateLine();
    ASSERT_NE(estimateLine().find(" (rough size)"), std::string::npos);
    ASSERT_TRUE(shows(kImagery));
    ASSERT_TRUE(shows(kHarbourTooLarge));
    EXPECT_LE(dialog_->GetSize().x, 800);
    EXPECT_LE(dialog_->GetSize().y, 480);
    const wxRect client(dialog_->ClientToScreen(wxPoint(0, 0)), dialog_->GetClientSize());
    for (wxWindow* w : all<wxWindow>())
        if (w->IsShownOnScreen()) {
            EXPECT_TRUE(client.Contains(w->GetScreenRect())) << u(w->GetLabel());
        }
}

// MEASURED on Xvfb in touch mode (48 px controls): the page with route rows, the imagery line
// and the Harbour note is 668 x 591 before the cost line, the cost line adds one text line (610 high). So 800
// x 480 is NOT met in touch mode. The test pins what the cost line adds (width within 800, one more text
// line, the new controls are 48 px high).
TEST_F(DownloadTest, InTouchModeTheCostLineAddsOneTextLineAndTheDropDownsAre48PixelsHigh) {
    ocpn_fake::touch = true;
    ocpn_fake::scale = 1.0;
    addRoute("cccccccc-0000-4000-8000-000000000003", "Long", {{45.0, 0.0}, {46.0, 10.0}});
    saveLast([&](ConfigStore::LastChoice& c) { c.sourceGuid = kGoogle; });
    sources_->estimateOut(capturedEstimates());
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::Route, std::nullopt);
    waitEstimate();
    ASSERT_TRUE(shows(kImagery));
    ASSERT_TRUE(shows(kHarbourTooLarge));
    ASSERT_TRUE(routeChoice()->IsShown());
    ASSERT_EQ(nextLevelLine(), "") << "range 8-14: no record at 14";
    auto* pageBody = wxWindow::FindWindowByName("pageBody");
    const int rowsBefore = pageBody->GetVirtualSize().y;
    pickZooms(9, 13);
    waitEstimate();
    const int rowsAfter = pageBody->GetVirtualSize().y;
    ASSERT_NE(nextLevelLine(), "");
    ASSERT_NE(rangeNote(), "") << "the range note takes the place of the Harbour note: one note line";
    const wxSize after = dialog_->GetSize();
    EXPECT_LE(after.x, 800);
    // the dialog is at its 480 cap here and the rows scroll, so the new row is read off the rows'
    // height.
    EXPECT_GT(rowsAfter, rowsBefore) << "the cost line is a new row";
    EXPECT_LE(rowsAfter - rowsBefore, 25) << "one text line";
    for (wxChoice* c : {least(), most()})
        EXPECT_GE(c->GetMinSize().y, 48) << "inside the collapsed fold";
}

// 3, 5. Touch mode (48 px controls) on an 800 x 480 screen: the longest page (route rows,
// imagery line, cost line, "One more level...") fits, its rows scroll, and the last row and Download stay
// reachable. Measured before: the page 668 x 610, the dialog 952 x 547 after a job ended.
class TouchFitTest : public DownloadTest {
protected:
    wxScrolledWindow* body() {
        return dynamic_cast<wxScrolledWindow*>(wxWindow::FindWindowByName("pageBody"));
    }
    wxRect clientRect() { return wxRect(dialog_->ClientToScreen(wxPoint(0, 0)), dialog_->GetClientSize()); }
    // On screen after the rows are scrolled to the top or the bottom.
    bool reachable(wxWindow* c, bool bottom) {
        wxScrolledWindow* b = body();
        b->Scroll(0, bottom ? b->GetScrollRange(wxVERTICAL) : 0);
        b->Update();
        return clientRect().Contains(c->GetScreenRect()) && b->GetScreenRect().Contains(c->GetScreenRect());
    }
    void longestPage() {
        ocpn_fake::touch = true;
        ocpn_fake::scale = 1.0;
        store_->setDisclaimerShown();
        sources_->holdRun(true);
        addRoute("cccccccc-0000-4000-8000-000000000003", "Long", {{45.0, 0.0}, {46.0, 10.0}});
        saveLast([&](ConfigStore::LastChoice& c) { c.sourceGuid = kGoogle; });
        sources_->estimateOut(capturedEstimates());
        makeDialog();
        dialog_->showNewChart(cg::AreaKind::Route, std::nullopt);
        waitEstimate();
        pickZooms(9, 13);
        waitEstimate();
        ASSERT_TRUE(shows(kImagery));
        ASSERT_NE(nextLevelLine(), "");
        ASSERT_NE(rangeNote(), "");
    }
    wxTextCtrl* nameField() { return all<wxTextCtrl>().back(); }
    wxCollapsiblePane* fold() { return all<wxCollapsiblePane>().at(0); }
};

TEST_F(TouchFitTest, TheLongestPageFitsInTouchModeAndScrollsToTheLastRowAndDownload) {
    longestPage();
    EXPECT_LE(dialog_->GetSize().x, 800);
    EXPECT_LE(dialog_->GetSize().y, 480);
    ASSERT_GT(body()->GetScrollRange(wxVERTICAL), body()->GetScrollThumb(wxVERTICAL)) << "the rows scroll";
    EXPECT_TRUE(clientRect().Contains(downloadButton()->GetScreenRect())) << "Download never scrolls away";
    EXPECT_TRUE(reachable(routeChoice(), false));
    EXPECT_TRUE(reachable(all<wxChoice>().at(1), false)) << "the chart drop-down";
    EXPECT_TRUE(reachable(fold(), true)) << "the last row is reached by scrolling";
    EXPECT_TRUE(reachable(nameField(), true));
    for (wxWindow* w : all<wxWindow>())
        if (w->IsShownOnScreen() && w != body() && !body()->IsDescendant(w) && w != dialog_) {
            EXPECT_TRUE(clientRect().Contains(w->GetScreenRect())) << u(w->GetLabel());
        }
}

TEST_F(TouchFitTest, AfterAJobEndsTheDialogStaysWithinEightHundredByFourEighty) {
    longestPage();
    const wxSize idle = dialog_->GetSize();
    {
        Answerer answers({});
        download();
    }
    EXPECT_LE(dialog_->GetSize().x, 800);
    EXPECT_LE(dialog_->GetSize().y, 480);
    stopJob();
    dialog_->Show(); // the dialog hid itself when the job started
    ASSERT_TRUE(dialog_->IsShownOnScreen());
    EXPECT_LE(dialog_->GetSize().x, 800);
    EXPECT_LE(dialog_->GetSize().y, 480);
    EXPECT_LE(std::abs(dialog_->GetSize().x - idle.x), 20) << "the busy row only";
    EXPECT_LE(std::abs(dialog_->GetSize().y - idle.y), 60) << "the busy row only";
    EXPECT_TRUE(clientRect().Contains(downloadButton()->GetScreenRect()));
}

// the status line breaks a long path into lines; it no longer makes the dialog wider
// (measured before: 952 wide after a job ended with a long text on the status line). The terms note of this
// source is the other wide text (820 before it was wrapped).
TEST_F(TouchFitTest, AVeryLongPathOnTheStatusLineIsBrokenAndTheDialogStaysEightHundredWide) {
    ocpn_fake::touch = true;
    ocpn_fake::scale = 1.0;
    std::ofstream(root_ / "a file") << "x";
    std::string folder = (root_ / "a file").string();
    for (int i = 0; i < 12; ++i)
        folder += "/very_long_directory_name_" + std::to_string(i);
    setSettings([&](cg::Settings& s) { s.chartDir = folder; });
    store_->setDisclaimerShown();
    ready();
    ASSERT_TRUE(downloadButton()->IsEnabled());
    Answerer answers({});
    download();
    wxString status;
    for (wxStaticText* s : all<wxStaticText>())
        if (s->IsShown() && u(s->GetLabel()).rfind("Cannot write", 0) == 0)
            status = s->GetLabel();
    ASSERT_FALSE(status.empty());
    EXPECT_NE(status.Find('\n'), wxNOT_FOUND) << "broken into lines";
    EXPECT_LE(dialog_->GetSize().x, 800);
    EXPECT_LE(dialog_->GetSize().y, 480);
    EXPECT_TRUE(clientRect().Contains(downloadButton()->GetScreenRect()));
}

TEST_F(TouchFitTest, AnExpandedAdvancedFoldScrollsAndDownloadStaysReachable) {
    longestPage();
    fold()->Collapse(false);
    wxCollapsiblePaneEvent e(fold(), fold()->GetId(), false);
    fold()->GetEventHandler()->ProcessEvent(e);
    EXPECT_LE(dialog_->GetSize().x, 800);
    EXPECT_LE(dialog_->GetSize().y, 480);
    EXPECT_TRUE(reachable(chooseFolder(), true)) << "the last control of the fold";
    EXPECT_TRUE(clientRect().Contains(downloadButton()->GetScreenRect()));
}

// item 4: the 480 px cap and the scrolling are touch mode only; the non-touch page keeps its natural
// size.
TEST_F(TouchFitTest, InNonTouchModeTheLongestPageWithAdvancedExpandedIsNotCappedNorScrolled) {
    ocpn_fake::touch = false;
    store_->setDisclaimerShown();
    sources_->holdRun(true);
    addRoute("cccccccc-0000-4000-8000-000000000003", "Long", {{45.0, 0.0}, {46.0, 10.0}});
    saveLast([&](ConfigStore::LastChoice& c) { c.sourceGuid = kGoogle; });
    sources_->estimateOut(capturedEstimates());
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::Route, std::nullopt);
    waitEstimate();
    pickZooms(9, 13);
    waitEstimate();
    ASSERT_NE(nextLevelLine(), "");
    const wxSize before = dialog_->GetSize();
    fold()->Collapse(false);
    wxCollapsiblePaneEvent e(fold(), fold()->GetId(), false);
    fold()->GetEventHandler()->ProcessEvent(e);
    wxScrolledWindow* b = body();
    EXPECT_LE(b->GetVirtualSize().y, b->GetClientSize().y) << "not scrolled: natural height";
    EXPECT_EQ(b->GetMinSize().y, b->GetVirtualSize().y);
    EXPECT_LE(b->GetVirtualSize().x, b->GetClientSize().x) << "no scroll bar width added";
    EXPECT_GT(dialog_->GetSize().y, before.y) << "the fold adds its rows to the height";
    EXPECT_LE(dialog_->GetSize().x - before.x, 2) << "not 17 px wider";
}

TEST_F(DownloadTest, TouchMakesThreadsTheChartFolderButtonAndDownloadAtLeast48PixelsHigh) {
    ocpn_fake::touch = true;
    ocpn_fake::scale = 1.0;
    ready();
    for (wxWindow* c : std::vector<wxWindow*>{threads(), chooseFolder(), downloadButton()}) {
        ASSERT_NE(c, nullptr);
        EXPECT_GE(c->GetMinSize().y, 48) << u(c->GetLabel());
    }
}

// ---- the warning on an extreme level ----
// The area is the box kMed of tests/geo_math_test.cpp (west 10, south 50, east 10.4, north 50.2; centre
// latitude 50.1): 1 tile per level at z0-9, 4 at z10, 12, 30, 80, 285, 1110, 4292 (z16), 16905 (z17), 66804
// (z18). T(8,18) = 89 524 (2 + 5 813 up to z16 + 16 905 + 66 804); T(8,16) = 5 815; the estimate stand-in
// answers 1 000 tiles / 20 000 000 bytes on disk = 20 000 bytes per tile. Scales at 50.1: z18 1:1.4k, z16
// 1:5.8k, z5 1:12M, z2 1:95M. The harbour is disabled for this area (T17 + T18 over 20 000).
class DetailWarningTest : public DownloadTest {
protected:
    void SetUp() override {
        DownloadTest::SetUp();
        setViewport(50, 50.2, 10, 10.4);
        sources_->estimateOut(estimateOf(1000, 0, 20000000));
    }
    // The question the last pick opened (it opens from a posted event): waits for one to appear (hang guard
    // only); nullptr when none came or when several are open.
    Question* open() {
        runUntil([] { return !qprobe::shown().empty(); });
        const auto q = qprobe::shown();
        return q.size() == 1 ? q[0] : nullptr;
    }
    // The answer is delivered; the window goes at the next idle.
    void settle() {
        ASSERT_TRUE(runUntil([] { return qprobe::shown().empty(); }));
    }
    void setWarn(bool on) {
        setSettings([&](cg::Settings& s) { s.warnDetailLevels = on; });
    }
    std::string rawWarnKey() {
        wxString raw = "<absent>";
        config_->Read("/PlugIns/ChartGrabber/WarnDetailLevels", &raw);
        return u(raw);
    }
    std::pair<int, int> range() { return {least()->GetSelection(), most()->GetSelection()}; }
    static constexpr const char* kMuchText =
        "Most detail 1:1.4k needs about 89 524 tiles for this area, about 1.8 GB to download. Detail finer "
        "than "
        "1:5k is for a harbour: draw a small area around it for that, as a second chart.  Suggested: most "
        "detail 1:5.8k - about 5 815 tiles, about 116 MB.";
};

TEST_F(DetailWarningTest, APickInMostDetailThatIsTooMuchAsksOnceAndDisablesThePage) {
    ready();
    ASSERT_EQ(range(), std::make_pair(8, 14));
    EXPECT_TRUE(qprobe::shown().empty());
    choose(most(), 18);
    Question* q = open();
    ASSERT_NE(q, nullptr) << "exactly one question";
    EXPECT_EQ(qprobe::text(q), kMuchText);
    EXPECT_EQ(qprobe::button(q, wxID_YES)->GetLabel(), "Use 1:5.8k");
    EXPECT_EQ(qprobe::button(q, wxID_NO)->GetLabel(), "Keep 1:1.4k");
    EXPECT_FALSE(panel()->IsEnabled());
    EXPECT_EQ(range(), std::make_pair(8, 18)) << "the pick stays until the answer";
    qprobe::press(q, wxID_NO);
    settle();
    EXPECT_TRUE(panel()->IsEnabled());
}

TEST_F(DetailWarningTest, UseAppliesTheSuggestionKeepAndEscapeChangeNothing) {
    ready();
    waitEstimateRuns(1);
    choose(most(), 18);
    ASSERT_NE(open(), nullptr) << "no question opened";
    qprobe::press(open(), wxID_YES);
    EXPECT_TRUE(panel()->IsEnabled());
    EXPECT_EQ(range(), std::make_pair(8, 16)) << "both drop-downs show the suggested range";
    EXPECT_EQ(ticks(), (std::vector<bool>{true, true, true, false})) << "the bands follow";
    EXPECT_EQ(rangeNote(), "");
    settle();
    EXPECT_TRUE(qprobe::shown().empty()) << "no second warning after Use";
    waitEstimateRuns(2); // the estimate for the new range is scheduled and runs
    // Keep and Escape
    choose(most(), 18);
    ASSERT_NE(open(), nullptr) << "no question opened";
    qprobe::press(open(), wxID_NO);
    EXPECT_EQ(range(), std::make_pair(8, 18));
    settle();
    choose(most(), 17); // 17 is over 20 000 tiles too: asks again
    ASSERT_NE(open(), nullptr) << "no question opened";
    qprobe::escape(open());
    EXPECT_EQ(range(), std::make_pair(8, 17));
    EXPECT_TRUE(panel()->IsEnabled());
    settle();
}

TEST_F(DetailWarningTest, DontShowAgainWritesTheSettingWithUseAndWithKeepOnly) {
    ready();
    ASSERT_EQ(rawWarnKey(), "1");
    for (const bool use : {true, false}) {
        SCOPED_TRACE(use ? "Use" : "Keep");
        setWarn(true);
        choose(most(), 18);
        Question* q = open();
        ASSERT_NE(q, nullptr);
        qprobe::press(q, use ? wxID_YES : wxID_NO); // not ticked: nothing is written
        settle();
        EXPECT_EQ(rawWarnKey(), "1");
        pickZooms(8, 14);
        choose(most(), 18);
        q = open();
        ASSERT_NE(q, nullptr);
        for (wxWindow* w : q->GetChildren())
            if (auto* box = dynamic_cast<wxCheckBox*>(w)) {
                EXPECT_EQ(box->GetLabel(), "Don't show again");
                box->SetValue(true);
            }
        qprobe::press(q, use ? wxID_YES : wxID_NO);
        settle();
        EXPECT_EQ(rawWarnKey(), "0");
        EXPECT_FALSE(store_->loadSettings().warnDetailLevels);
        pickZooms(8, 14);
        choose(most(), 18);
        EXPECT_TRUE(qprobe::shown().empty()) << "a later extreme pick opens no question";
    }
}

TEST_F(DetailWarningTest, NoWarningAtCreationAfterAnAreaChangeABandTapOrWithoutAnAreaOrTheSetting) {
    saveLast([&](ConfigStore::LastChoice& c) { c.advancedZooms = "0-21"; });
    setViewport(50, 50.05, 10, 10.05); // small: Harbour is allowed, 0-21 stays
    ready();
    EXPECT_EQ(range(), std::make_pair(0, 21)) << "LastZooms 0-21 is loaded";
    EXPECT_TRUE(qprobe::shown().empty()) << "at creation";
    dialog_->showNewChart(cg::AreaKind::View, std::nullopt); // an area change
    EXPECT_TRUE(qprobe::shown().empty());
    // No area box: a view without extent is an area error
    setViewport(50, 50, 10, 10);
    dialog_->showNewChart(cg::AreaKind::View, std::nullopt);
    pickZooms(2, 21);
    EXPECT_TRUE(qprobe::shown().empty()) << "no area, no warning";
    // The setting is off
    setViewport(50, 50.05, 10, 10.05);
    dialog_->showNewChart(cg::AreaKind::View, std::nullopt);
    setWarn(false);
    pickZooms(2, 21);
    EXPECT_TRUE(qprobe::shown().empty());
}

TEST_F(DetailWarningTest, ABandTapThatReachesHarbourDoesNotWarn) {
    setViewport(50, 50.05, 10, 10.05); // small: Harbour is allowed
    ready();
    tick(band(3));
    EXPECT_EQ(range(), std::make_pair(8, 18));
    EXPECT_TRUE(qprobe::shown().empty());
}

TEST_F(DetailWarningTest, ThePageReadsTheSettingAtThePickNotAtCreation) {
    ready();
    setWarn(false);
    choose(most(), 18);
    EXPECT_TRUE(qprobe::shown().empty());
    pickZooms(8, 14);
    setWarn(true);
    choose(most(), 18);
    ASSERT_NE(open(), nullptr) << "no question opened";
    qprobe::press(open(), wxID_NO);
    settle();
}

TEST_F(DetailWarningTest, ALeastDetailPickBelowFiveAsksWithTheOceanText) {
    ready();
    choose(least(), 2); // 2-14
    Question* q = open();
    ASSERT_NE(q, nullptr);
    EXPECT_EQ(
        qprobe::text(q),
        "Least detail 1:95M shows whole oceans. It adds little to the download, but OpenCPN's own world "
        "chart already shows this.  Suggested: least detail 1:12M.");
    EXPECT_EQ(qprobe::button(q, wxID_YES)->GetLabel(), "Use 1:12M");
    EXPECT_EQ(qprobe::button(q, wxID_NO)->GetLabel(), "Keep 1:95M");
    qprobe::press(q, wxID_YES);
    EXPECT_EQ(range(), std::make_pair(5, 14));
    settle();
}

TEST_F(DetailWarningTest, AnAnswerAfterTheDropDownMovedOnChangesNothing) {
    ready();
    choose(most(), 18);
    ASSERT_NE(open(), nullptr) << "no question opened";
    most()->SetSelection(15); // something else set the drop-down while the question was open
    qprobe::press(open(), wxID_YES);
    EXPECT_EQ(most()->GetSelection(), 15);
    EXPECT_TRUE(panel()->IsEnabled());
    settle();
}

TEST_F(DetailWarningTest, DeletingThePanelDeletesTheOpenQuestionAtOnceAndCallsNothing) {
    ready();
    choose(most(), 18);
    wxWeakRef<Question> q(open());
    ASSERT_TRUE(q);
    static_cast<wxSimplebook*>(panel()->GetParent())->DeleteAllPages();
    EXPECT_FALSE(q) << "no idle ran in between";
    destroyDialog();
}

TEST_F(DetailWarningTest, TheQuestionFitsEightHundredByFourEightyInTouchMode) {
    ocpn_fake::touch = true;
    ocpn_fake::scale = 1.0;
    ready();
    choose(most(), 18);
    Question* q = open();
    ASSERT_NE(q, nullptr);
    EXPECT_LE(q->GetSize().x, 800);
    EXPECT_LE(q->GetSize().y, 480);
    EXPECT_GE(qprobe::button(q, wxID_YES)->GetSize().y, look_.minControlHeight());
    EXPECT_GE(qprobe::button(q, wxID_NO)->GetSize().y, look_.minControlHeight());
    EXPECT_GE(look_.minControlHeight(), 48);
    qprobe::press(q, wxID_NO);
    settle();
}

// ---- Weather on top ----
// The stand-in prints the lines of the real CLI 2026.10.6 `sources` output (tests/fixtures/cli-2026.10.6/
// sources-bundled-all.stdout, 16 rows) unless a case edits one line; every edit is named at its test.

const char* const kRainViewer = "{1A696D82-F258-41A4-BCD6-7648D9E08DE2}";
const char* const kDwd = "{25E4232E-08A5-47C5-8D97-E38B5F418A2D}";
const char* const kH60b = "{BC2EA862-0F3D-4933-8F71-00AFA0BAD114}";
const char* const kImerg = "{92E40853-04A2-46B7-8A5B-3665FCDDFDBE}";
const char* const kNowcoast = "{076D1F9A-DC94-4184-B785-77362CD01B5B}";
const char* const kGeoColour = "{FE84A175-2C0F-420F-86DF-5900DE16A33C}";
const char* const kRainViewerCredit = "Weather data by RainViewer (https://www.rainviewer.com)";
const char* const kCoverage =
    "Radar reaches about 80-130 NM off the coast; far offshore only satellite pictures show the weather.";
// The five weather layers in the order of as the choice labels them (50 code points or
// 47 + "...").
const std::vector<std::string> kWeatherLabels = {
    "RainViewer radar (worldwide, current)", "DWD weather radar (Germany, North Sea and weste...",
    "NOAA nowCOAST weather radar (contiguous US)", "EUMETSAT H SAF precipitation rate H60B (Europe,...",
    "NASA IMERG precipitation rate (30 min, worldwide)"};

class PanelWeatherTest : public DownloadTest {
protected:
    // Removes the Marine rows (the two vessel-density layers) of the fixture: the weather tests that count
    // the choice entries predate them (hand-made from the real lines: a line is dropped, none edited).
    static void dropMarine(std::vector<std::string>& lines) {
        lines.erase(
            std::remove_if(lines.begin(), lines.end(),
                           [](const std::string& l) { return l.find("\tMarine\t") != std::string::npos; }),
            lines.end());
    }
    // Edits the line of the fixture whose second field is `guid`: `from` becomes `to` (the line must have
    // it).
    static void editLine(std::vector<std::string>& lines, const char* guid, const std::string& from,
                         const std::string& to) {
        bool done = false;
        for (std::string& l : lines)
            if (l.find(std::string("\t") + guid + "\t") != std::string::npos) {
                const auto at = l.find(from);
                ASSERT_NE(at, std::string::npos) << l;
                l.replace(at, from.size(), to);
                done = true;
            }
        ASSERT_TRUE(done) << guid;
    }
    std::vector<std::string> fixture(const char* dir) const {
        std::vector<std::string> lines;
        std::istringstream in(slurpText(fs::path(CG_FIXTURES_DIR) / dir / "sources-bundled-all.stdout"));
        for (std::string l; std::getline(in, l);)
            lines.push_back(l);
        return lines;
    }
    // The stand-in prints the fixture of `dir`, after `edit`.
    void useFixture(const char* dir = "cli-2026.10.6",
                    const std::function<void(std::vector<std::string>&)>& edit = {}) {
        std::vector<std::string> lines = fixture(dir);
        if (edit)
            edit(lines);
        std::string text;
        for (const std::string& l : lines)
            text += l + "\n";
        sources_->rawRecords(text);
    }
    // Field `n` (0 = "source") of the fixture line of `guid`.
    std::string fieldOf(const char* guid, int n) const {
        for (const std::string& l : fixture("cli-2026.10.6"))
            if (l.find(std::string("\t") + guid + "\t") != std::string::npos) {
                std::vector<std::string> f;
                std::istringstream in(l);
                for (std::string x; std::getline(in, x, '\t');)
                    f.push_back(x);
                return f.at(n);
            }
        return "<no such line>";
    }
    wxCheckBox* weather() { return labelled<wxCheckBox>("Weather on top"); }
    wxChoice* weatherChoice() {
        return dynamic_cast<wxChoice*>(wxWindow::FindWindowByName("weatherLayer", dialog_));
    }
    std::vector<std::string> weatherEntries() {
        std::vector<std::string> out;
        for (unsigned i = 0; i < weatherChoice()->GetCount(); ++i)
            out.push_back(u(weatherChoice()->GetString(i)));
        return out;
    }
    // The shown weather line 1 ("Made as its own chart..."), else "".
    std::string weatherLine() {
        for (wxStaticText* s : all<wxStaticText>())
            if (s->IsShown() && u(s->GetLabel()).rfind("Made as its own chart", 0) == 0)
                return u(s->GetLabel());
        return "";
    }
    // The detail note: "Detail set under Advanced..." or "... has detail only up to...", else "".
    std::string sourceLimitLine() {
        for (wxStaticText* s : all<wxStaticText>())
            if (s->IsShown() && u(s->GetLabel()).find(" has detail only up to ") != std::string::npos)
                return u(flat(s));
        return "";
    }
    void tickWeather(int layerIndex) {
        if (!weather()->GetValue())
            tick(weather());
        choose(weatherChoice(), layerIndex);
    }
    std::vector<fs::path> sortedJobDirs() const {
        std::vector<fs::path> d = jobDirs();
        std::sort(d.begin(), d.end());
        return d;
    }
    // Waits for the middle of a second so that the job ids of one Download share one stamp.
    void waitForTheStartOfASecond() {
        ASSERT_TRUE(runUntil([] {
            const long ms = wxDateTime::UNow().GetMillisecond();
            return ms > 100 && ms < 400;
        }));
    }
};

// Proof 1: a stand-in without field 11: no control, and the page is lower than the page of a CLI with field
// 11 (the weather row shown) by at least one control height. Sizes are compared in one run (no pixel
// constant: fonts and theme differ per machine).
TEST_F(PanelWeatherTest, WithoutFieldElevenThereIsNoWeatherControlAndThePageKeepsItsSize) {
    saveLast([&](ConfigStore::LastChoice& c) {
        c.weather = true; // a saved wish does not show a control that has no layers
        c.weatherGuid = kRainViewer;
    });
    // The dialog size after the stand-in printed the lines of the fixture, `edit`ed.
    const auto sizeWith = [&](const std::function<void(std::vector<std::string>&)>& edit) {
        useFixture("cli-2026.10.6", edit);
        makeDialog();
        waitEstimate();
        const wxSize size = dialog_->GetSize();
        if (edit) { // the cut fixture: the checks of the missing control
            EXPECT_FALSE(weather()->IsShown());
            EXPECT_FALSE(weather()->GetValue()) << "LastWeather with an empty list: unticked";
            EXPECT_FALSE(weatherChoice()->IsShown());
            EXPECT_EQ(weatherChoice()->GetCount(), 0u);
            EXPECT_EQ(weatherLine(), "");
            EXPECT_FALSE(shows(kCoverage));
        } else {
            EXPECT_TRUE(weather()->IsShown()) << "the fixture with field 11 shows the weather row";
        }
        destroyDialog();
        return size;
    };
    // The lines cut to the 11 fields of a CLI before 2026.10.1 (the type and fields 1-10): hand-made from the
    // real ones.
    const auto cut = [](std::vector<std::string>& lines) {
        for (std::string& l : lines) {
            std::size_t at = 0;
            for (int i = 0; i < 11 && at != std::string::npos; ++i)
                at = l.find('\t', at + (i ? 1 : 0));
            if (at != std::string::npos)
                l.resize(at);
        }
    };
    const wxSize withRow = sizeWith({});
    const wxSize without = sizeWith(cut);
    const wxSize again = sizeWith(cut);
    EXPECT_EQ(without, again) << "the same inputs build the same page";
    EXPECT_LE(without.y + std::max(look_.minControlHeight(), 1), withRow.y) // 0 off touch: strictly lower
        << "the weather row is not reserved without field 11";
}

// Proof 2. Edited fixture line: the DWD row's verdict `ok` replaced by the verdict of a refused source
// (kRefusedVerdict); hand-made from the real line.
TEST_F(PanelWeatherTest,
       TheChoiceListsTheUsableWeatherLayersInCliOrderWithShortLabelsAndTheFullNameAsToolTip) {
    useFixture();
    makeDialog();
    waitEstimate();
    ASSERT_TRUE(weather()->IsShown());
    EXPECT_EQ(weatherEntries(), kWeatherLabels) << "never GeoColour, VIIRS or the seamark layer";
    EXPECT_FALSE(weather()->GetValue());
    EXPECT_FALSE(weatherChoice()->IsEnabled());
    EXPECT_EQ(weatherLine(), "");
    EXPECT_FALSE(shows(kCoverage));
    tick(weather());
    EXPECT_TRUE(weatherChoice()->IsEnabled());
    const std::vector<std::string> fullNames = {fieldOf(kRainViewer, 3), fieldOf(kDwd, 3),
                                                fieldOf(kNowcoast, 3), fieldOf(kH60b, 3), fieldOf(kImerg, 3)};
    EXPECT_EQ(fullNames[3], "EUMETSAT H SAF precipitation rate H60B (Europe, Africa, Atlantic)");
    for (int i : {3, 0, 1}) { // the tool tip follows every selection
        choose(weatherChoice(), i);
        EXPECT_EQ(u(weatherChoice()->GetToolTipText()), fullNames[i]);
    }
    destroyDialog();
    useFixture("cli-2026.10.6", [&](std::vector<std::string>& lines) {
        editLine(lines, kDwd, "\tok\t", std::string("\t") + kRefusedVerdict + "\t");
    });
    makeDialog();
    waitEstimate();
    std::vector<std::string> withoutDwd = kWeatherLabels;
    withoutDwd.erase(withoutDwd.begin() + 1);
    EXPECT_EQ(weatherEntries(), withoutDwd) << "a weather layer whose verdict is not ok is not listed";
}

// Proof 3. Edited fixture line: the RainViewer row's license (field 12) emptied.
TEST_F(PanelWeatherTest, TickingShowsTheScaleAndCreditLineAndTheCoverageLineAndUntickingHidesBoth) {
    useFixture();
    makeDialog();
    waitEstimate();
    tickWeather(0); // RainViewer, maxZoom 7: the page's 8-14 become {7}; latitude 50 -> 1:3.0M
    EXPECT_EQ(weatherLine(),
              std::string("Made as its own chart, detail up to 1:3.0M. Credit: ") + kRainViewerCredit);
    EXPECT_TRUE(shows(kCoverage));
    const std::string credit = fieldOf(kImerg, 12);
    ASSERT_EQ(credit.size(), 144u);
    choose(weatherChoice(), 4); // NASA IMERG, maxZoom 6 -> 1:5.9M, the 144-character credit
    EXPECT_EQ(weatherLine(), "Made as its own chart, detail up to 1:5.9M. Credit: " + credit);
    choose(weatherChoice(), 1); // DWD, maxZoom 9: the highest page zoom at most 9 is 9 -> 1:740k
    EXPECT_EQ(weatherLine().substr(0, 44), "Made as its own chart, detail up to 1:740k. ");
    pickZooms(10, 12); // the weather zoom follows the range: nothing of 10-12 is below 9 -> the limit itself
    EXPECT_EQ(weatherLine().substr(0, 44), "Made as its own chart, detail up to 1:740k. ");
    tick(weather());
    EXPECT_EQ(weatherLine(), "");
    EXPECT_FALSE(shows(kCoverage));
    EXPECT_FALSE(weatherChoice()->IsEnabled());
    destroyDialog();
    useFixture("cli-2026.10.6",
               [&](std::vector<std::string>& lines) { editLine(lines, kRainViewer, kRainViewerCredit, ""); });
    makeDialog();
    waitEstimate();
    tickWeather(0);
    EXPECT_EQ(weatherLine(), "Made as its own chart, detail up to 1:3.0M.");
    EXPECT_TRUE(shows(kCoverage)) << "line 2 stays when there is no credit";
}

// Proof 4: the saved wish, the pick and what a reload and Close do.
TEST_F(PanelWeatherTest, LastWeatherAndItsSourceSelectTheLayerAndCloseSavesNothing) {
    useFixture();
    saveLast([&](ConfigStore::LastChoice& c) {
        c.weather = true;
        c.weatherGuid = "{bc2ea862-0f3d-4933-8f71-00afa0bad114}"; // H60B (fourth in the order), lower case
    });
    makeDialog();
    waitEstimate();
    EXPECT_TRUE(weather()->GetValue());
    EXPECT_EQ(weatherChoice()->GetSelection(), 3);
    EXPECT_TRUE(weatherChoice()->IsEnabled());
    EXPECT_NE(weatherLine(), "");
    choose(weatherChoice(), 2);
    panel()->reloadSources();
    waitLoaded();
    waitEstimate();
    EXPECT_EQ(weatherChoice()->GetSelection(), 2) << "a reload keeps the pick while it is listed";
    EXPECT_TRUE(weather()->GetValue());
    tick(weather());
    click(labelled<wxButton>("Close"));
    const ConfigStore::LastChoice last = store_->loadLastChoice();
    EXPECT_TRUE(last.weather) << "Close saved nothing: the stored wish is the one the test wrote";
    EXPECT_EQ(last.weatherGuid, "{bc2ea862-0f3d-4933-8f71-00afa0bad114}");
    destroyDialog();
    saveLast([&](ConfigStore::LastChoice& c) { c.weatherGuid = "{00000000-0000-4000-8000-000000000000}"; });
    makeDialog();
    waitEstimate();
    EXPECT_TRUE(weather()->GetValue());
    EXPECT_EQ(weatherChoice()->GetSelection(), 0) << "a GUID that is not listed: the first layer";
}

// . Edited fixture lines (hand-made from the RainViewer line): an extra layer with another GUID
// and a Cyrillic name put FIRST in CLI order; the RainViewer line dropped.
TEST_F(PanelWeatherTest, TheShippedLayersComeFirstInTheFixedOrderAndRainViewerIsTheDefault) {
    const char* const kExtra = "{00000000-0000-4000-8000-0000000000AA}";
    useFixture("cli-2026.10.6", [&](std::vector<std::string>& lines) {
        std::string extra;
        for (const std::string& l : lines)
            if (l.find(kRainViewer) != std::string::npos)
                extra = l;
        ASSERT_FALSE(extra.empty());
        extra.replace(extra.find(kRainViewer), std::string_view(kRainViewer).size(), kExtra);
        extra.replace(extra.find("RainViewer radar (worldwide, current)"), 37,
                      "\xD0\x94\xD0\xBE\xD0\xB6\xD0\xB4\xD1\x8C radar");
        lines.insert(lines.begin(), extra);
    });
    makeDialog();
    waitEstimate();
    std::vector<std::string> expected = kWeatherLabels;
    expected.push_back("\xD0\x94\xD0\xBE\xD0\xB6\xD0\xB4\xD1\x8C radar");
    EXPECT_EQ(weatherEntries(), expected);
    EXPECT_EQ(weatherChoice()->GetSelection(), 0) << "no LastWeatherSource: RainViewer";
    destroyDialog();
    useFixture("cli-2026.10.6", [&](std::vector<std::string>& lines) {
        lines.erase(
            std::remove_if(lines.begin(), lines.end(),
                           [&](const std::string& l) { return l.find(kRainViewer) != std::string::npos; }),
            lines.end());
    });
    makeDialog();
    waitEstimate();
    ASSERT_GT(weatherChoice()->GetCount(), 0u);
    EXPECT_EQ(weatherChoice()->GetSelection(), 0);
    EXPECT_EQ(u(weatherChoice()->GetString(0)), kWeatherLabels[1]) << "without RainViewer: DWD";
}

// and 3.
TEST_F(PanelWeatherTest, TheWeatherPicturesAreBasesAfterTheCuratedMapsWithoutImageryLineTermsNoteOrSeamarks) {
    useFixture("cli-2026.10.6", dropMarine);
    makeDialog();
    waitEstimate();
    const std::string geoLabel = "EUMETSAT MTG GeoColour (Europe, Africa, Atlanti...";
    const std::vector<std::string> expected = {"Esri World Imagery",
                                               "Sentinel-2 Cloudless 2025 (EOX)",
                                               "USGS Imagery (USA)",
                                               "OpenTopoMap",
                                               "EMODnet Bathymetry (world)",
                                               "EMODnet Bathymetry (European seas, depth shading)",
                                               geoLabel,
                                               "NASA VIIRS NOAA-20 true colour (daily, clouds)"};
    ASSERT_EQ(entries(), expected);
    EXPECT_EQ(u(chart()->GetToolTipText()), "Esri World Imagery") << "a short name is its own tool tip";
    EXPECT_TRUE(seamarks()->GetValue());
    EXPECT_TRUE(shows(kImagery));
    EXPECT_TRUE(shows(kTermsNote));
    choose(chart(), 6);
    EXPECT_FALSE(shows(kImagery));
    EXPECT_FALSE(shows(kTermsNote));
    EXPECT_FALSE(seamarks()->GetValue());
    const std::string full = fieldOf(kGeoColour, 3);
    ASSERT_EQ(full.size(), 71u);
    EXPECT_EQ(u(chart()->GetToolTipText()), full);
    choose(chart(), 7);
    EXPECT_FALSE(shows(kImagery));
    EXPECT_FALSE(shows(kTermsNote));
    EXPECT_FALSE(seamarks()->GetValue());
    choose(chart(), 0);
    EXPECT_TRUE(seamarks()->GetValue());
    EXPECT_TRUE(shows(kImagery));
}

// the test GUIDs are the fixture's; kind and field 11 as the spec's tables say.
TEST_F(PanelWeatherTest, EveryGuidOfTheSpecTablesIsInTheFixtureWithKindAndSnapshot) {
    for (const char* g : {kRainViewer, kDwd, kNowcoast, kH60b, kImerg}) {
        EXPECT_EQ(fieldOf(g, 2), "layer") << g;
        EXPECT_EQ(fieldOf(g, 11), "snapshot") << g;
    }
    for (const char* g : {kGeoColour, "{8B9C244E-E978-4267-8FE4-D703534C0789}"}) {
        EXPECT_EQ(fieldOf(g, 2), "map") << g;
        EXPECT_EQ(fieldOf(g, 11), "snapshot") << g;
    }
}

// Proof 5 and 6 and 7: the second job. Route mode: each directory has its own route.gpx.
TEST_F(PanelWeatherTest, DownloadMakesTheChartAndAWeatherChartThatRunsRightAfterIt) {
    useFixture();
    useRoute();
    store_->setDisclaimerShown();
    sources_->holdRun(true);
    sources_->estimateOut(estimate1950());
    saveLast([&](ConfigStore::LastChoice& c) {
        c.weather = true;
        c.weatherGuid = kRainViewer;
    });
    makeDialog();
    press(route());
    waitEstimate();
    ASSERT_TRUE(weather()->GetValue());
    typeInto(nameField(), "Solent");
    waitEstimate();
    Answerer answers({});
    download();
    EXPECT_TRUE(answers.shown.empty());
    const std::vector<fs::path> dirs = sortedJobDirs();
    ASSERT_EQ(dirs.size(), 2u);
    const std::string weatherFile = chartFolder() + "/Solent - Weather.mbtiles";
    EXPECT_EQ(slurpText(dirs[1] / "job.ini"),
              std::string("[Job]\nOverlay=") + kRainViewer + "\nRoute=" + (dirs[1] / "route.gpx").string() +
                  "\nCorridorNm=2.0\nZooms=7\nOut=" + weatherFile + "\n[Download]\nThreads=2\n");
    EXPECT_NE(slurpText(dirs[0] / "job.ini").find(std::string("Route=") + (dirs[0] / "route.gpx").string()),
              std::string::npos);
    EXPECT_TRUE(fs::exists(dirs[0] / "route.gpx"));
    EXPECT_TRUE(fs::exists(dirs[1] / "route.gpx"));
    const IniFile second = readIni(dirs[1] / "chart.ini");
    EXPECT_EQ(second.get("State"), "interrupted");
    EXPECT_EQ(second.get("Weather"), "1");
    EXPECT_EQ(second.get("Credit"), kRainViewerCredit);
    EXPECT_EQ(second.get("Name"), "Solent - Weather");
    EXPECT_EQ(second.get("SourceGuid"), "");
    EXPECT_EQ(second.get("OverlayGuid"), kRainViewer);
    EXPECT_EQ(second.get("SourceName"), "RainViewer radar (worldwide, current)");
    EXPECT_EQ(second.get("Zooms"), "7");
    EXPECT_EQ(second.get("File0"), weatherFile);
    const IniFile first = readIni(dirs[0] / "chart.ini");
    EXPECT_EQ(first.get("State"), "running");
    EXPECT_EQ(first.get("Weather"), "0");
    EXPECT_EQ(first.get("Credit"), fieldOf(kEsri, 12));
    EXPECT_EQ(first.get("Zooms"), "8-14");
    // jobs.start got the chart; the weather record follows it when the first job ends.
    ASSERT_TRUE(runUntil([&] { return sources_->runArgv().size() == 1; }));
    EXPECT_NE(sources_->runArgv()[0].find((dirs[0] / "job.ini").string()), std::string::npos);
    sources_->holdRun(false);
    ASSERT_TRUE(runUntil([&] { return sources_->runArgv().size() == 2; })) << "the weather chart did not run";
    EXPECT_NE(sources_->runArgv()[1].find((dirs[1] / "job.ini").string()), std::string::npos);
    EXPECT_EQ(sources_->runArgv()[1].find("--replace"), std::string::npos) << sources_->runArgv()[1];
    EXPECT_EQ(sources_->runArgv()[1].find("--restart"), std::string::npos) << sources_->runArgv()[1];
    ASSERT_TRUE(runUntil([&] { return !jobs_->busy(); }));
    // Saved with the other values (step 8).
    const ConfigStore::LastChoice last = store_->loadLastChoice();
    EXPECT_TRUE(last.weather);
    EXPECT_EQ(last.weatherGuid, kRainViewer);
}

// Proof 6: the second directory cannot be made -> both gone, nothing started. A FILE named like the second
// job's id (`<stamp>-2`) takes the place of the directory (a test cannot chmod a directory the page has not
// made yet); the test starts in the middle of a second so both ids share one stamp.
TEST_F(PanelWeatherTest, AWriteFailureOfTheSecondJobRemovesBothDirectoriesAndStartsNothing) {
    useFixture();
    store_->setDisclaimerShown();
    sources_->holdRun(true);
    saveLast([&](ConfigStore::LastChoice& c) {
        c.weather = true;
        c.weatherGuid = kRainViewer;
    });
    makeDialog();
    waitEstimate();
    waitForTheStartOfASecond();
    const std::string stamp = wxDateTime::Now().Format("%Y%m%d-%H%M%S").ToStdString();
    fs::create_directories(jobsRoot());
    std::ofstream(jobsRoot() / (stamp + "-2")) << "x";
    Answerer answers({});
    download();
    EXPECT_TRUE(shows(("Cannot write " + (jobsRoot() / (stamp + "-2")).string() + ".").c_str()))
        << shownTexts();
    EXPECT_TRUE(jobDirs().empty()) << "both job directories are gone";
    EXPECT_TRUE(sources_->runArgv().empty());
    EXPECT_FALSE(jobs_->busy());
    EXPECT_TRUE(dialog_->IsShown());
    EXPECT_FALSE(fs::exists(chartFolder() + "/" + "x"));
}

// Proof 7: the weather file alone triggers the replace question.
TEST_F(PanelWeatherTest, AnExistingWeatherFileAloneAsksToReplace) {
    useFixture();
    store_->setDisclaimerShown();
    sources_->holdRun(true);
    saveLast([&](ConfigStore::LastChoice& c) {
        c.weather = true;
        c.weatherGuid = kRainViewer;
    });
    fs::create_directories(chartFolder());
    std::ofstream(fs::path(chartFolder()) / "T - Weather.mbtiles") << "x";
    makeDialog();
    typeInto(nameField(), "T");
    waitEstimate();
    {
        Answerer answers(std::vector<Answerer::Answer>{{false}});
        download();
        ASSERT_EQ(answers.shown.size(), 1u);
        EXPECT_EQ(answers.shown[0], kReplace);
    }
    EXPECT_TRUE(jobDirs().empty());
    tick(weather()); // without the weather box the weather file is not looked at
    waitEstimate();
    {
        Answerer answers({});
        download();
        EXPECT_TRUE(answers.shown.empty());
    }
    EXPECT_EQ(jobDirs().size(), 1u);
}

// Proof 8. Edited fixture line: the seamark layer's maxZoom (field 13, empty) set to 6; hand-made from the
// real line.
TEST_F(PanelWeatherTest, TheSeamarkLayersLimitAddsItsZoomAndTheEstimateAndJobIniCarryTheList) {
    useFixture("cli-2026.10.6", [&](std::vector<std::string>& lines) {
        editLine(lines, kSeamarkLayer, "CC-BY-SA\t", "CC-BY-SA\t6");
    });
    store_->setDisclaimerShown();
    sources_->holdRun(true);
    makeDialog();
    ASSERT_TRUE(seamarks()->GetValue()) << "Esri is a satellite base: seamarks on";
    waitEstimate();
    EXPECT_NE(sources_->estimateArgv().back().find(" --zooms 6,8-14"), std::string::npos)
        << sources_->estimateArgv().back();
    EXPECT_EQ(sourceLimitLine(), "") << "the note is about the base";
    Answerer answers({});
    download();
    ASSERT_EQ(jobDirs().size(), 1u);
    EXPECT_NE(slurpText(jobDirs()[0] / "job.ini").find("\nZooms=6,8-14\n"), std::string::npos);
    EXPECT_EQ(readIni(jobDirs()[0] / "chart.ini").get("Zooms"), "6,8-14");
}

// Proof 8 and 9: a base with a limit (GeoColour, maxZoom 8). It is the saved LastSource (a slot of the
// choice, as a More sources pick would take); the note comes first, follows every base change and the job
// stops at the limit.
TEST_F(PanelWeatherTest, ABasesZoomLimitCutsTheJobAndItsNoteComesBeforeTheRangeNote) {
    useFixture();
    store_->setDisclaimerShown();
    sources_->holdRun(true);
    saveLast([&](ConfigStore::LastChoice& c) { c.sourceGuid = kGeoColour; });
    makeDialog();
    ASSERT_EQ(selected(), "EUMETSAT MTG GeoColour (Europe, Africa, Atlanti...");
    const std::string note =
        "EUMETSAT MTG GeoColour (Europe, Africa, Atlanti... has detail only up to 1:1.5M; "
        "the chart stops there.";
    EXPECT_EQ(sourceLimitLine(), note) << "range 8-14 against maxZoom 8";
    EXPECT_EQ(rangeNote(), "");
    pickZooms(9, 14); // the range note applies as well: the source limit is shown instead (first match)
    EXPECT_EQ(sourceLimitLine(), note);
    EXPECT_EQ(rangeNote(), "");
    waitEstimate();
    EXPECT_NE(sources_->estimateArgv().back().find(" --zooms 8"), std::string::npos);
    EXPECT_EQ(sources_->estimateArgv().back().find(" --zooms 8-"), std::string::npos);
    choose(chart(), 0); // a base change: Esri has no limit, the range note takes the line
    EXPECT_EQ(sourceLimitLine(), "");
    EXPECT_EQ(rangeNote().substr(0, 25), "Detail set under Advanced");
    choose(chart(), 6); // and back: GeoColour is a curated entry, no longer a LastSource slot
    EXPECT_EQ(sourceLimitLine(), note);
    waitEstimate();
    Answerer answers({});
    download();
    ASSERT_EQ(jobDirs().size(), 1u);
    const std::string ini = slurpText(jobDirs()[0] / "job.ini");
    EXPECT_NE(ini.find("\nZooms=8\n"), std::string::npos) << ini;
    const IniFile chart = readIni(jobDirs()[0] / "chart.ini");
    EXPECT_EQ(chart.get("Zooms"), "8");
    EXPECT_EQ(chart.get("Weather"), "1");
    EXPECT_EQ(chart.get("Credit"), "Contains modified EUMETSAT Meteosat data 2026");
}

// Proof 9: a static base gives Weather=0 and its own license as the credit.
TEST_F(PanelWeatherTest, AStaticBasesCreditIsItsLicenseAndItIsNoWeatherChart) {
    useFixture();
    store_->setDisclaimerShown();
    sources_->holdRun(true);
    saveLast([&](ConfigStore::LastChoice& c) { c.sourceGuid = kTopo; });
    makeDialog();
    ASSERT_EQ(selected(), kNameTopo);
    waitEstimate();
    Answerer answers({});
    download();
    ASSERT_EQ(jobDirs().size(), 1u);
    const IniFile chart = readIni(jobDirs()[0] / "chart.ini");
    EXPECT_EQ(chart.get("Weather"), "0");
    EXPECT_EQ(chart.get("Credit"),
              "Map data (c) OpenStreetMap contributors, SRTM; map style (c) OpenTopoMap (CC-BY-SA)");
}

// Proof 3 (touch): route rows, the imagery line, a Detail note and the weather row with the longest credit
// still fit 800 x 480 by 's rule: the dialog is at most 800 x 480, the rows scroll, the status line and
// Close / Download stay in view.
class PanelWeatherTouchTest : public TouchFitTest {};

TEST_F(PanelWeatherTouchTest, TheLongestCreditStillFitsEightHundredByFourEightyAndScrolls) {
    ocpn_fake::touch = true;
    ocpn_fake::scale = 1.0;
    store_->setDisclaimerShown();
    sources_->rawRecords(
        slurpText(fs::path(CG_FIXTURES_DIR) / "cli-2026.10.6" / "sources-bundled-all.stdout"));
    sources_->holdRun(true);
    addRoute("cccccccc-0000-4000-8000-000000000003", "Long", {{45.0, 0.0}, {46.0, 10.0}});
    saveLast([&](ConfigStore::LastChoice& c) {
        c.sourceGuid = kEsri;
        c.weather = true;
        c.weatherGuid = kImerg;
    });
    makeDialog();
    dialog_->showNewChart(cg::AreaKind::Route, std::nullopt);
    waitEstimate();
    pickZooms(9, 13);
    waitEstimate();
    ASSERT_TRUE(shows(kImagery));
    ASSERT_NE(rangeNote(), "");
    std::string line;
    for (wxStaticText* s : all<wxStaticText>())
        if (s->IsShown() && u(s->GetLabel()).rfind("Made as its own chart", 0) == 0)
            line = u(s->GetLabel());
    ASSERT_GT(line.size(), 144u) << "the 144-character credit is in the line";
    fprintf(stderr, "measured size: %d x %d\n", dialog_->GetSize().x, dialog_->GetSize().y);
    EXPECT_LE(dialog_->GetSize().x, 800);
    EXPECT_LE(dialog_->GetSize().y, 480);
    ASSERT_GT(body()->GetScrollRange(wxVERTICAL), body()->GetScrollThumb(wxVERTICAL)) << "the rows scroll";
    EXPECT_TRUE(clientRect().Contains(downloadButton()->GetScreenRect())) << "Download never scrolls away";
    wxWindow* box = labelled<wxCheckBox>("Weather on top");
    ASSERT_NE(box, nullptr);
    EXPECT_GE(box->GetSize().y, 48);
    EXPECT_GE(wxWindow::FindWindowByName("weatherLayer", dialog_)->GetSize().y, 48);
    for (wxWindow* w : {box, wxWindow::FindWindowByName("weatherLayer", dialog_)}) {
        // the cap counts the window manager's decoration, so the body is shorter: a row in the middle
        // is reached at some scroll position, not only at the top or the bottom
        bool seen = false;
        for (int pos = 0; !seen && pos <= body()->GetScrollRange(wxVERTICAL); ++pos) {
            body()->Scroll(0, pos);
            body()->Update();
            seen = clientRect().Contains(w->GetScreenRect()) &&
                   body()->GetScreenRect().Contains(w->GetScreenRect());
        }
        EXPECT_TRUE(seen) << "scrolled into view: " << u(w->GetName());
    }
    EXPECT_TRUE(reachable(fold(), true)) << "the last row is reached by scrolling";
    for (wxWindow* w : all<wxWindow>())
        if (w->IsShownOnScreen() && w != body() && !body()->IsDescendant(w) && w != dialog_) {
            EXPECT_TRUE(clientRect().Contains(w->GetScreenRect())) << u(w->GetLabel());
        }
}

// ---- Marine vessel-density layers ----
// The stand-in prints tests/fixtures/cli-2026.10.6/sources-bundled-all.stdout: it holds the two
// vessel-density records of data/sources/Marine (kind layer, maxZoom 9), verbatim.
const char* const kVesselAll = "{99D06D6A-F7FA-4F06-8C00-D9F96307C0F3}";
const char* const kVesselSailing = "{478CBB2F-70D1-4573-8C50-B5361CE2AA46}";
const char* const kVesselAllName = "EMODnet vessel density 2023, all ship types (AIS, European seas)";

class PanelVesselTest : public PanelWeatherTest {
protected:
    void useVessels(const std::function<void(std::vector<std::string>&)>& edit = {}) {
        useFixture("cli-2026.10.6", edit);
    }
};

// Proofs 1 and 2: after the weather bases, in order; labels from the CLI names; no extra lines; the job's
// base is the layer and there is no overlay.
TEST_F(PanelVesselTest, TheTwoLayersFollowTheWeatherBasesAndDownloadRunsThemAsABase) {
    useVessels();
    store_->setDisclaimerShown();
    sources_->holdRun(true);
    makeDialog();
    waitEstimate();
    const std::vector<std::string> all = entries();
    ASSERT_EQ(all.size(), 10u);
    EXPECT_EQ(all[7], "NASA VIIRS NOAA-20 true colour (daily, clouds)");
    EXPECT_EQ(all[8], "EMODnet vessel density 2023, all ship types (AI...");
    EXPECT_EQ(all[9], "EMODnet vessel density 2023, sailing and pleasu...");
    choose(chart(), 8);
    EXPECT_EQ(u(chart()->GetToolTipText()), kVesselAllName);
    EXPECT_FALSE(seamarks()->GetValue());
    EXPECT_FALSE(shows(kImagery));
    EXPECT_FALSE(shows(kTermsNote));
    choose(chart(), 9);
    EXPECT_FALSE(seamarks()->GetValue());
    waitEstimate();
    Answerer answers({});
    download();
    ASSERT_EQ(jobDirs().size(), 1u);
    const std::string ini = slurpText(jobDirs()[0] / "job.ini");
    EXPECT_NE(ini.find(std::string("Source=") + kVesselSailing + "\n"), std::string::npos) << ini;
    EXPECT_EQ(ini.find("Overlay="), std::string::npos) << ini;
}

// Proof 3: a CLI without them leaves the choice as it was; a refused record is not offered.
TEST_F(PanelVesselTest, WithoutTheRecordsOrWithARefusedOneTheChoiceIsUnchanged) {
    useFixture("cli-2026.10.6", dropMarine);
    makeDialog();
    waitEstimate();
    EXPECT_EQ(entries().size(), 8u);
}
TEST_F(PanelVesselTest, ARefusedRecordIsNotInTheChoice) {
    useVessels([](std::vector<std::string>& lines) {
        editLine(lines, kVesselAll, "\tok\t", std::string("\t") + kRefusedVerdict + "\t");
    });
    makeDialog();
    waitEstimate();
    const std::vector<std::string> all = entries();
    ASSERT_EQ(all.size(), 9u);
    EXPECT_EQ(all[8], "EMODnet vessel density 2023, sailing and pleasu...");
}

// Proof 4: the record's maxZoom 9 cuts the default 8-14 range.
TEST_F(PanelVesselTest, TheRecordsZoomLimitCutsTheJob) {
    useVessels();
    store_->setDisclaimerShown();
    sources_->holdRun(true);
    makeDialog();
    choose(chart(), 8);
    EXPECT_NE(sourceLimitLine(), "") << "the note names the limit";
    waitEstimate();
    EXPECT_NE(sources_->estimateArgv().back().find(" --zooms 8-9"), std::string::npos)
        << sources_->estimateArgv().back();
}

} // namespace
