// SPDX-License-Identifier: GPL-3.0-or-later
// the toolbar tool of ChartGrabberPi against the recording
// fakes of ocpn_fakes.cpp. The expected file names and call order are the literals of the spec.
#include "chartgrabber_pi.h"
#include "config_store.h"
#include "job_files.h"
#include "main_dialog.h"
#include "ocpn_fakes.h"
#include "plugin_log.h"
#include "progress_strip.h"
#include "settings_dialog.h"
#include "sources_stub.h"
#include "wx_env.h"

#include <gtest/gtest.h>

#include <wx/app.h>
#include <wx/apptrait.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dcmemory.h>
#include <wx/evtloop.h>
#include <wx/fileconf.h>
#include <wx/frame.h>
#include <wx/weakref.h>

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <regex>
#include <sstream>
#include <sys/stat.h>

#include <ocpn_plugin.h>

namespace {

using Kind = ocpn_fake::ToolCall::Kind;

class PluginToolTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!wxTheApp || !wxTheApp->IsGUI())
            GTEST_SKIP() << "no display: the plugin toolbar test was NOT run";
        char tmpl[] = "/tmp/cgpi-XXXXXX";
        ASSERT_NE(mkdtemp(tmpl), nullptr);
        root_ = tmpl;
        ocpn_fake::privateData = wxString::FromUTF8((root_ / "private").string());
        ocpn_fake::dataDir = wxString::FromUTF8("/opt/pl\xC3\xA5gin 'd'"); // a space, a quote, non-ASCII
        ocpn_fake::toolbar.clear();
        config_ = std::make_unique<wxFileConfig>(wxString("cgpitest"), wxEmptyString,
                                                 wxString::FromUTF8((root_ / "opencpn.conf").string()),
                                                 wxEmptyString, wxCONFIG_USE_LOCAL_FILE);
        ocpn_fake::config = config_.get();
        frame_ = new wxFrame(nullptr, wxID_ANY, "canvas", wxPoint(50, 60), wxSize(800, 480));
        ocpn_fake::canvasWindow = frame_;
        ocpn_fake::canvases = {frame_};
        ocpn_fake::focus = frame_;
        pi_ = std::make_unique<ChartGrabberPi>(nullptr);
        ASSERT_NE(pi_->Init(), 0);
    }
    void TearDown() override {
        if (!frame_)
            return;
        pi_->DeInit();
        pi_.reset();
        ocpn_fake::canvasWindow = nullptr;
        ocpn_fake::canvases.clear();
        ocpn_fake::focus = nullptr;
        ocpn_fake::config = nullptr;
        frame_->Destroy();
        wxTheApp->ProcessPendingEvents();
        wxTheApp->ProcessIdle();
        config_.reset();
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }
    static wxString file(const char* name) {
        return wxString::FromUTF8("/opt/pl\xC3\xA5gin 'd'/data/") + name;
    }

    std::filesystem::path root_;
    std::unique_ptr<wxFileConfig> config_;
    wxFrame* frame_ = nullptr;
    std::unique_ptr<ChartGrabberPi> pi_;
};

} // namespace

TEST_F(PluginToolTest, InitInsertsACheckToolWithTheDarkSquareFile) {
    ASSERT_EQ(ocpn_fake::toolbar.size(), 1u);
    const auto& c = ocpn_fake::toolbar[0];
    EXPECT_EQ(c.kind, Kind::Insert);
    EXPECT_EQ(c.itemKind, int(wxITEM_CHECK));
    EXPECT_EQ(c.normal, file("chartgrabber_tool.svg"));
    EXPECT_EQ(c.rollover, file("chartgrabber_tool.svg"));
    EXPECT_EQ(c.toggled, file("chartgrabber_tool_toggled.svg"));
    EXPECT_EQ(ocpn_fake::dataDirAsked, "chartgrabber_pi");
}

TEST_F(PluginToolTest, BusySwapsTheRolloverFileThenToggles) {
    ocpn_fake::toolbar.clear();
    pi_->setToolBusy(true);
    ASSERT_EQ(ocpn_fake::toolbar.size(), 2u);
    EXPECT_EQ(ocpn_fake::toolbar[0].kind, Kind::Bitmaps); // bitmaps first, then the state
    EXPECT_EQ(ocpn_fake::toolbar[0].id, 7);
    EXPECT_EQ(ocpn_fake::toolbar[0].normal, file("chartgrabber_tool.svg"));
    EXPECT_EQ(ocpn_fake::toolbar[0].rollover, file("chartgrabber_tool_rollover_toggled.svg"));
    EXPECT_EQ(ocpn_fake::toolbar[0].toggled, file("chartgrabber_tool_toggled.svg"));
    EXPECT_EQ(ocpn_fake::toolbar[1].kind, Kind::State);
    EXPECT_EQ(ocpn_fake::toolbar[1].id, 7);
    EXPECT_TRUE(ocpn_fake::toolbar[1].state);

    ocpn_fake::toolbar.clear();
    pi_->setToolBusy(false);
    ASSERT_EQ(ocpn_fake::toolbar.size(), 2u);
    EXPECT_EQ(ocpn_fake::toolbar[0].kind, Kind::Bitmaps);
    EXPECT_EQ(ocpn_fake::toolbar[0].normal, file("chartgrabber_tool.svg"));
    EXPECT_EQ(ocpn_fake::toolbar[0].rollover, file("chartgrabber_tool.svg"));
    EXPECT_EQ(ocpn_fake::toolbar[0].toggled, file("chartgrabber_tool_toggled.svg"));
    EXPECT_EQ(ocpn_fake::toolbar[1].kind, Kind::State);
    EXPECT_FALSE(ocpn_fake::toolbar[1].state);
}

TEST_F(PluginToolTest, ClickReassertsTheIdleStateBeforeAnythingElse) {
    ocpn_fake::toolbar.clear();
    pi_->OnToolbarToolCallback(7);
    ASSERT_FALSE(ocpn_fake::toolbar.empty());
    EXPECT_EQ(ocpn_fake::toolbar[0].kind, Kind::State); // undoes the click's own flip of the CHECK tool
    EXPECT_EQ(ocpn_fake::toolbar[0].id, 7);
    EXPECT_FALSE(ocpn_fake::toolbar[0].state);
}

TEST_F(PluginToolTest, ClickReassertsTheBusyStateThatSetToolBusyStored) {
    pi_->setToolBusy(true);
    ocpn_fake::toolbar.clear();
    pi_->OnToolbarToolCallback(7);
    ASSERT_FALSE(ocpn_fake::toolbar.empty());
    EXPECT_EQ(ocpn_fake::toolbar[0].kind, Kind::State);
    EXPECT_EQ(ocpn_fake::toolbar[0].id, 7);
    EXPECT_TRUE(ocpn_fake::toolbar[0].state);

    pi_->setToolBusy(false);
    ocpn_fake::toolbar.clear();
    pi_->OnToolbarToolCallback(7);
    ASSERT_FALSE(ocpn_fake::toolbar.empty());
    EXPECT_EQ(ocpn_fake::toolbar[0].kind, Kind::State);
    EXPECT_FALSE(ocpn_fake::toolbar[0].state);
}

TEST_F(PluginToolTest, InitResetsTheStoredBusyState) {
    pi_->setToolBusy(true);
    pi_->DeInit();
    ASSERT_NE(pi_->Init(), 0);
    ocpn_fake::toolbar.clear();
    pi_->OnToolbarToolCallback(7);
    ASSERT_FALSE(ocpn_fake::toolbar.empty());
    EXPECT_EQ(ocpn_fake::toolbar[0].kind, Kind::State);
    EXPECT_FALSE(ocpn_fake::toolbar[0].state);
}

TEST_F(PluginToolTest, PluginManagerPictureIsTheToolFileAt32) {
    ocpn_fake::svgAsked.clear();
    pi_->GetPlugInBitmap();
    EXPECT_EQ(ocpn_fake::svgAsked, file("chartgrabber_tool.svg"));
    EXPECT_EQ(ocpn_fake::svgSize, (std::pair<unsigned, unsigned>(32, 32)));
}

// : the strip's "Preferences..." (shown by a start that failed: the CLI path is not there)
// opens ONE modeless Settings window with the canvas as parent; a second press raises it; DeInit deletes it
// at once, before anything else, so its callbacks cannot outlive the library.
TEST_F(PluginToolTest, TheStripsPreferencesOpensOneModelessSettingsWindowThatDeInitDeletesAtOnce) {
    ConfigStore store(config_.get());
    cg::Settings s = store.loadSettings();
    s.cliPath = (root_ / "no-such" / "chart-grabber-cli").string();
    s.stripOnChart = true; // these tests find the strip as a child of the canvas (the chart host)
    store.saveSettings(s);
    cg::ChartRecord r;
    r.id = "20261015-090000";
    r.name = "Resumable";
    r.sourceGuid = "{B}";
    r.bbox = {-1.5, 50.25, 2.125, 51.0};
    r.zooms = {8, 9};
    r.created = "2026-10-15T10:15:00";
    r.state = cg::ChartState::Interrupted;
    store.saveChart(r);
    std::ofstream(store.jobsDir() + "/" + r.id + "/job.ini") << "[Job]\n";
    pi_->OnToolbarToolCallback(7); // opens the main dialog
    wxWindow* resume = wxWindow::FindWindowByLabel("Resume");
    ASSERT_NE(resume, nullptr);
    wxCommandEvent press(wxEVT_BUTTON, resume->GetId());
    press.SetEventObject(resume);
    resume->GetEventHandler()->ProcessEvent(press);
    wxWindow* prefs = nullptr; // the strip's button, not the New chart page's
    for (wxWindow* child : frame_->GetChildren())
        if (dynamic_cast<ProgressStrip*>(child))
            prefs = wxWindow::FindWindowByLabel("Preferences...", child);
    ASSERT_NE(prefs, nullptr) << "the strip shows the start failure with Preferences...";
    ASSERT_EQ(SettingsDialog::shown(), nullptr);
    wxCommandEvent open(wxEVT_BUTTON, prefs->GetId());
    open.SetEventObject(prefs);
    prefs->GetEventHandler()->ProcessEvent(open);
    wxTheApp->ProcessPendingEvents(); // the strip runs the action with CallAfter
    SettingsDialog* dlg = SettingsDialog::shown();
    ASSERT_NE(dlg, nullptr) << "returned with the window shown";
    EXPECT_FALSE(dlg->IsModal());
    EXPECT_EQ(dlg->GetParent(), static_cast<wxWindow*>(frame_));
    prefs->GetEventHandler()->ProcessEvent(open); // a second press raises the same window
    wxTheApp->ProcessPendingEvents();
    EXPECT_EQ(SettingsDialog::shown(), dlg);
    int windows = 0;
    for (auto* n = wxTopLevelWindows.GetFirst(); n; n = n->GetNext())
        windows += dynamic_cast<SettingsDialog*>(n->GetData()) ? 1 : 0;
    EXPECT_EQ(windows, 1) << "no second window";
    wxWeakRef<SettingsDialog> ref(dlg);
    pi_->DeInit();
    EXPECT_FALSE(ref) << "no idle ran in between";
    EXPECT_EQ(SettingsDialog::shown(), nullptr);
}

namespace {
// The source choice of the open main dialog: the last wxChoice below it (the route choice comes first); the
// weather layer choice (right after it) and the two detail drop-downs of the Advanced fold
// are skipped.
wxChoice* sourceChoiceOf(wxWindow* w) {
    wxChoice* found = nullptr;
    for (wxWindow* c : w->GetChildren()) {
        if (auto* ch = dynamic_cast<wxChoice*>(c)) {
            if (ch->GetName() != "weatherLayer" && ch->GetName() != "leastDetail" &&
                ch->GetName() != "mostDetail")
                found = ch;
        } else if (auto* deeper = sourceChoiceOf(c))
            found = deeper;
    }
    return found;
}
} // namespace

// OK of the Settings window opened from the strip tells the open main dialog to read
// its sources again; Cancel does not.
TEST_F(PluginToolTest, TheStripsSettingsOkMakesTheOpenPageReadItsSourcesAgain) {
    std::unique_ptr<wxEventLoopBase> loop(wxApp::GetInstance()->GetTraits()->CreateEventLoop());
    wxEventLoopActivator active(loop.get());
    const auto runUntil = [&](const std::function<bool()>& done, int ms = 15000) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (!done() && std::chrono::steady_clock::now() < deadline)
            loop->DispatchTimeout(20);
        return done();
    };
    const char* const kMapnik = "{D643C5A1-4331-4387-9BE2-DDDF185C117E}";
    const char* const kEsri = "{7B743985-BC5F-4AB6-8915-AC5DBBB8F552}";
    stub::Sources cli(root_);
    cli.records({{kMapnik, "", "OSM OpenStreetMap.org - MAPNIK", 3857, "ok", ""},
                 {kEsri, "", "ESRI ArcGIS.Imagery", 3857, "ok", ""}});
    ConfigStore store(config_.get());
    cg::Settings s = store.loadSettings();
    s.cliPath = cli.cli();
    s.stripOnChart = true; // these tests find the strip as a child of the canvas (the chart host)
    store.saveSettings(s);
    cg::ChartRecord r;
    r.id = "20261015-090000";
    r.name = "Resumable";
    r.sourceGuid = "{B}";
    r.bbox = {-1.5, 50.25, 2.125, 51.0};
    r.zooms = {8, 9};
    r.created = "2026-10-15T10:15:00";
    r.state = cg::ChartState::Interrupted;
    store.saveChart(r);
    std::ofstream(store.jobsDir() + "/" + r.id + "/job.ini") << "[Job]\n";
    pi_->OnToolbarToolCallback(7);
    MainDialog* main = nullptr;
    for (auto* n = wxTopLevelWindows.GetFirst(); n; n = n->GetNext())
        if (auto* m = dynamic_cast<MainDialog*>(n->GetData()))
            main = m;
    ASSERT_NE(main, nullptr);
    wxChoice* choice = sourceChoiceOf(main);
    ASSERT_NE(choice, nullptr);
    ASSERT_TRUE(runUntil([&] { return choice->IsEnabled() && choice->GetCount() == 2; }));

    chmod(cli.cli().c_str(), 0644); // the start fails and the strip offers Preferences...
    wxWindow* resume = wxWindow::FindWindowByLabel("Resume");
    ASSERT_NE(resume, nullptr);
    wxCommandEvent press(wxEVT_BUTTON, resume->GetId());
    press.SetEventObject(resume);
    resume->GetEventHandler()->ProcessEvent(press);
    wxWindow* prefs = nullptr;
    ASSERT_TRUE(runUntil([&] {
        for (wxWindow* child : frame_->GetChildren())
            if (dynamic_cast<ProgressStrip*>(child))
                prefs = wxWindow::FindWindowByLabel("Preferences...", child);
        return prefs && prefs->IsShown();
    }));
    chmod(cli.cli().c_str(), 0755);
    cli.records({{kMapnik, "", "OSM OpenStreetMap.org - MAPNIK", 3857, "ok", ""}});
    for (const bool ok : {false, true}) {
        SCOPED_TRACE(ok ? "OK" : "Cancel");
        wxCommandEvent open(wxEVT_BUTTON, prefs->GetId());
        open.SetEventObject(prefs);
        prefs->GetEventHandler()->ProcessEvent(open);
        // the strip runs the action with CallAfter: wait for the Settings window
        ASSERT_TRUE(runUntil([] { return SettingsDialog::shown() != nullptr; })) << "no Settings window";
        SettingsDialog* dlg = SettingsDialog::shown();
        ASSERT_NE(dlg, nullptr);
        const int id = ok ? wxID_OK : wxID_CANCEL;
        wxCommandEvent e(wxEVT_BUTTON, id);
        e.SetEventObject(dlg->FindWindow(id));
        dlg->GetEventHandler()->ProcessEvent(e);
        if (ok) {
            EXPECT_TRUE(runUntil([&] { return choice->IsEnabled() && choice->GetCount() == 1; }));
        } else {
            // Absence-wait: Cancel must read nothing again; drain every pending event, then look.
            for (int i = 0; i < 1000 && loop->DispatchTimeout(20) == 1; ++i) {
            }
            EXPECT_EQ(choice->GetCount(), 2u) << "Cancel reads nothing again";
        }
        // the idle delete of the Settings window (shown is false already after the Hide)
        EXPECT_TRUE(runUntil([] {
            for (auto* n = wxTopLevelWindows.GetFirst(); n; n = n->GetNext())
                if (dynamic_cast<SettingsDialog*>(n->GetData()))
                    return false;
            return true;
        })) << "the Settings window stayed";
    }
}

// : the plugin's own log file over Init / DeInit.
namespace {

std::string slurpFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

// The file's lines without the 23-character stamp and its space (each line must carry a stamp).
std::vector<std::string> unstamped(const std::filesystem::path& p) {
    std::vector<std::string> lines;
    std::istringstream in(slurpFile(p));
    for (std::string line; std::getline(in, line);) {
        EXPECT_TRUE(std::regex_match(line, std::regex(R"(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3} .*)")))
            << line;
        lines.push_back(line.size() > 24 ? line.substr(24) : line);
    }
    return lines;
}

bool matches(const std::string& line, const std::string& re) {
    return std::regex_match(line, std::regex(re));
}

// Restores the fakes the log tests steer.
struct FakeReset {
    ~FakeReset() {
        ocpn_fake::contentScale = 1.0;
        ocpn_fake::chartbarHeight = 0;
        ocpn_fake::privateDataNull = false;
    }
};

} // namespace

TEST_F(PluginToolTest, InitStartsTheLogFileWithTheSupportHeaderAndDeInitEndsIt) {
    FakeReset reset;
    const auto logFile = root_ / "private" / "plugins" / "chartgrabber" / "chartgrabber.log";
    pi_->DeInit();
    std::filesystem::create_directories(logFile.parent_path());
    std::ofstream(logFile) << "old session\n";
    std::ofstream(logFile.string() + ".1") << "older session\n";
    const std::string cli = (root_ / "tile cli").string();
    std::ofstream(cli) << "#!/bin/sh\n";
    chmod(cli.c_str(), 0755);
    config_->Write("/Settings/ConfigVersionString", "Version 5.10.2 Build 2026-01-02");
    config_->Write("/Settings/OpenGL", "1");
    config_->Write("/PlugIns/ChartGrabber/CliPath", wxString::FromUTF8(cli));
    config_->Write("/PlugIns/ChartGrabber/StripOnChart",
                   7L); // invalid: the 8.7 line, before the settings line
    ocpn_fake::contentScale = 1.5;
    ocpn_fake::chartbarHeight = 37;
    ASSERT_NE(pi_->Init(), 0);

    EXPECT_FALSE(std::filesystem::exists(logFile.string() + ".1"));
    const std::vector<std::string> lines = unstamped(logFile);
    ASSERT_GE(lines.size(), 7u);
    EXPECT_TRUE(matches(lines[0], R"(Chart Grabber )" CG_VERSION_STRING
                                  R"(, library \S.* \(\d+ bytes, modified \d{4}-\d\d-\d\d \d\d:\d\d:\d\d\))"))
        << lines[0];
    EXPECT_TRUE(matches(
        lines[1],
        R"(OpenCPN \(config saved by\) Version 5\.10\.2 Build 2026-01-02, program /.+, plugin API 1\.18)"))
        << lines[1];
    EXPECT_TRUE(matches(lines[2], R"(wxWidgets 3\.2\.\d+.* \(built with wxWidgets 3\.2\.\d+.*\))"))
        << lines[2];
    EXPECT_TRUE(matches(
        lines[3],
        R"(display x11, WAYLAND_DISPLAY (set|unset), GDK_BACKEND=\S+, OCPN_DISABLE_X11_GDK_BACKEND (set|unset), XDG_SESSION_TYPE=\S+, content scale 1\.50)"))
        << lines[3];
    EXPECT_EQ(lines[4], "OpenGL on");
    const wxSize client = frame_->GetClientSize();
    EXPECT_EQ(lines[5], "canvases 1, primary canvas " + std::to_string(client.x) + "x" +
                            std::to_string(client.y) + " px, chart bar 37 px");
    EXPECT_EQ(lines[6], "setting StripOnChart=7 is invalid; using 0");
    ASSERT_GE(lines.size(), 8u);
    EXPECT_EQ(lines[7], "settings: CLI " + cli + ", chart sources " + std::string(getenv("HOME")) +
                            "/Charts/Chart sources, charts " + std::string(getenv("HOME")) +
                            "/Charts/ChartGrabber, saved tiles " + (root_ / "private").string() +
                            "/plugins/chartgrabber/cache, progress on the chart off, debug off");
    EXPECT_EQ(lines[2].rfind("old", 0), std::string::npos);

    pi_->DeInit();
    EXPECT_EQ(unstamped(logFile).back(), "session end");
    ASSERT_NE(pi_->Init(), 0); // a new session starts a fresh file
    const std::vector<std::string> second = unstamped(logFile);
    EXPECT_TRUE(matches(second[0], "Chart Grabber .*"));
    EXPECT_EQ(std::count(second.begin(), second.end(), "session end"), 0);
}

// The "modified" stamp of the library line is the file's mtime in local time: the
// mtime is set through utimensat, the expectation is built from the same time_t.
TEST_F(PluginToolTest, TheHeaderLibraryLineShowsTheFileModificationTimeInLocalTime) {
    const auto logFile = root_ / "private" / "plugins" / "chartgrabber" / "chartgrabber.log";
    pi_->DeInit();
    const std::string exe = std::filesystem::read_symlink("/proc/self/exe").string();
    struct stat before{};
    ASSERT_EQ(stat(exe.c_str(), &before), 0);
    const timespec saved[2] = {before.st_atim, before.st_mtim};
    const std::time_t secs =
        1615000000 + 7; // 2021-03-06, the half second keeps the conversion error off the edge
    const timespec set[2] = {{secs, 500000000}, {secs, 500000000}};
    ASSERT_EQ(utimensat(AT_FDCWD, exe.c_str(), set, 0), 0);
    ASSERT_NE(pi_->Init(), 0);
    utimensat(AT_FDCWD, exe.c_str(), saved, 0);
    std::tm tm{};
    localtime_r(&secs, &tm);
    char want[32];
    std::strftime(want, sizeof want, "%Y-%m-%d %H:%M:%S", &tm);
    const std::vector<std::string> lines = unstamped(logFile);
    ASSERT_FALSE(lines.empty());
    EXPECT_NE(lines[0].find(std::string(", modified ") + want + ")"), std::string::npos)
        << lines[0] << " want " << want;
}

TEST_F(PluginToolTest, TheHeaderSaysOpenGlUnknownWithoutTheConfigKeyAndDebugOnWhenStored) {
    const auto logFile = root_ / "private" / "plugins" / "chartgrabber" / "chartgrabber.log";
    pi_->DeInit();
    config_->Write("/PlugIns/ChartGrabber/DebugLog", 1L);
    ASSERT_NE(pi_->Init(), 0);
    const std::vector<std::string> lines = unstamped(logFile);
    ASSERT_GE(lines.size(), 7u);
    EXPECT_EQ(lines[4], "OpenGL unknown");
    EXPECT_NE(lines[6].find(", debug on"), std::string::npos) << lines[6];
    EXPECT_TRUE(cg::pluginLogDebug());

    pi_->DeInit();
    config_->Write("/Settings/OpenGL", "0");
    ASSERT_NE(pi_->Init(), 0);
    EXPECT_EQ(unstamped(logFile)[4], "OpenGL off");
    pi_->DeInit();
    config_->Write("/Settings/OpenGL", "x");
    ASSERT_NE(pi_->Init(), 0);
    EXPECT_EQ(unstamped(logFile)[4], "OpenGL unknown");
}

TEST_F(PluginToolTest, WithoutAConfigObjectTheSettingsLineSaysSo) {
    const auto logFile = root_ / "private" / "plugins" / "chartgrabber" / "chartgrabber.log";
    pi_->DeInit();
    ocpn_fake::config = nullptr;
    ASSERT_NE(pi_->Init(), 0);
    const std::vector<std::string> lines = unstamped(logFile);
    ASSERT_GE(lines.size(), 7u);
    EXPECT_EQ(lines[4], "OpenGL unknown");
    EXPECT_EQ(lines[6], "settings: none (no OpenCPN config)");
}

TEST_F(PluginToolTest, ANullPrivateLocationWritesNoLogFileAndInitStillSucceeds) {
    FakeReset reset;
    pi_->DeInit();
    std::error_code ec;
    std::filesystem::remove_all(root_ / "private", ec);
    ocpn_fake::privateDataNull = true;
    ASSERT_NE(pi_->Init(), 0);
    EXPECT_EQ(cg::pluginLogPath(), "");
    pi_->DeInit();
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root_))
        EXPECT_NE(entry.path().filename(), "chartgrabber.log") << entry.path();
}

TEST_F(PluginToolTest, TheFirstOverlayCallOfEachKindWritesOneLine) {
    const auto logFile = root_ / "private" / "plugins" / "chartgrabber" / "chartgrabber.log";
    const auto count = [&](const std::string& text) {
        const std::vector<std::string> lines = unstamped(logFile);
        return std::count(lines.begin(), lines.end(), text);
    };
    wxBitmap bitmap(10, 10);
    wxMemoryDC dc(bitmap);
    PlugIn_ViewPort vp{};
    pi_->RenderOverlayMultiCanvas(dc, &vp, 0, OVERLAY_OVER_SHIPS);
    EXPECT_EQ(count("first chart draw: no OpenGL"), 1);
    pi_->RenderOverlayMultiCanvas(dc, &vp, 0, OVERLAY_OVER_SHIPS);
    EXPECT_EQ(count("first chart draw: no OpenGL"), 1);
    EXPECT_EQ(count("first chart draw: OpenGL"), 0);
    pi_->RenderGLOverlayMultiCanvas(nullptr, &vp, 0, OVERLAY_OVER_SHIPS);
    pi_->RenderGLOverlayMultiCanvas(nullptr, &vp, 0, OVERLAY_OVER_SHIPS);
    EXPECT_EQ(count("first chart draw: OpenGL"), 1);
}
