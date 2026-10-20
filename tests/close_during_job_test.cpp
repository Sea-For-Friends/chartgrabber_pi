// SPDX-License-Identifier: GPL-3.0-or-later
// regression guard for "OpenCPN freezes when the Chart Grabber window is closed while a download
// runs". A stand-in CLI job is held alive (holdRun); the main dialog is opened by the toolbar callback and
// closed by the close box, the Close button and Escape, with the New chart and the My charts page shown,
// reopened and closed 20 times, then the job ends while the dialog is hidden. Every step must return within
// kLimitMs and the event loop must keep turning. The strip is run as its own window and, in the second test,
// on the chart. A real hang blocks the main thread, which no in-process check can interrupt: the ctest
// --timeout of tools/check.sh (CG_TEST_TIMEOUT) is the watchdog: it kills the binary and fails the test. Not
// covered by fakes: OpenCPN's own UpdateChartDBInplace and real window-manager stacking.
#include "chartgrabber_pi.h"
#include "config_store.h"
#include "job_files.h"
#include "main_dialog.h"
#include "ocpn_fakes.h"
#include "progress_strip.h"
#include "sources_stub.h"
#include "wx_env.h"

#include <gtest/gtest.h>

#include <wx/app.h>
#include <wx/apptrait.h>
#include <wx/choice.h>
#include <wx/evtloop.h>
#include <wx/fileconf.h>
#include <wx/frame.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>

#include <ocpn_plugin.h>

namespace {

using Kind = ocpn_fake::ToolCall::Kind;

class CloseDuringJobTest : public ::testing::Test {
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

    void scenario(bool stripOnChart); // stripOnChart selects the host of the progress strip

    std::filesystem::path root_;
    std::unique_ptr<wxFileConfig> config_;
    wxFrame* frame_ = nullptr;
    std::unique_ptr<ChartGrabberPi> pi_;
};

} // namespace

namespace {
constexpr long kLimitMs = 500;

// The source choice: the weather layer choice and the detail drop-downs are skipped.
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
bool lastStateBusy() {
    for (auto it = ocpn_fake::toolbar.rbegin(); it != ocpn_fake::toolbar.rend(); ++it)
        if (it->kind == ocpn_fake::ToolCall::Kind::State)
            return it->state;
    return false;
}
// The progress strip, wherever its host put it (own frame: a top-level window; chart: a child of the canvas).
ProgressStrip* findStrip(wxWindow* w) {
    if (auto* s = dynamic_cast<ProgressStrip*>(w))
        return s;
    for (auto* n = w->GetChildren().GetFirst(); n; n = n->GetNext())
        if (auto* s = findStrip(n->GetData()))
            return s;
    return nullptr;
}
ProgressStrip* stripOf(wxWindow* canvas) {
    for (auto* n = wxTopLevelWindows.GetFirst(); n; n = n->GetNext())
        if (auto* s = findStrip(n->GetData()))
            return s;
    return findStrip(canvas);
}
MainDialog* mainDialog() {
    for (auto* n = wxTopLevelWindows.GetFirst(); n; n = n->GetNext())
        if (auto* m = dynamic_cast<MainDialog*>(n->GetData()))
            return m;
    return nullptr;
}
long msSince(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t)
        .count();
}
} // namespace

void CloseDuringJobTest::scenario(bool stripOnChart) {
    std::unique_ptr<wxEventLoopBase> loop(wxApp::GetInstance()->GetTraits()->CreateEventLoop());
    wxEventLoopActivator active(loop.get());
    long iterations = 0;
    const auto runUntil = [&](const std::function<bool()>& done, int ms = 15000) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (!done() && std::chrono::steady_clock::now() < deadline) {
            loop->DispatchTimeout(20);
            ++iterations;
        }
        return done();
    };
    // Timed on purpose: the test measures the loop over a duration (timer ticks, the iteration count), so the
    // pump is an absence-wait / a stress, not a wait for an event. Event waits use runUntil(predicate).
    const auto pump = [&](int ms) { runUntil([] { return false; }, ms); };
    // One step: it returns within kLimitMs and the dialog is in the wanted state (shown or not).
    const auto step = [&](const char* name, bool shownAfter, const std::function<void()>& fn) {
        const auto t = std::chrono::steady_clock::now();
        fn();
        EXPECT_LT(msSince(t), kLimitMs) << name;
        MainDialog* m = mainDialog();
        EXPECT_NE(m, nullptr) << name;
        if (m) {
            EXPECT_EQ(m->IsShown(), shownAfter) << name;
        }
        const auto t2 = std::chrono::steady_clock::now();
        pump(100); // the 1 s My charts timer may tick here: a slow tick counts too
        EXPECT_LT(msSince(t2), 100 + kLimitMs) << name << " (pump)";
    };
    const char* const kMapnik = "{D643C5A1-4331-4387-9BE2-DDDF185C117E}";
    stub::Sources cli(root_);
    cli.records({{kMapnik, "", "OSM OpenStreetMap.org - MAPNIK", 3857, "ok", ""}});
    ConfigStore store(config_.get());
    cg::Settings s = store.loadSettings();
    s.cliPath = cli.cli();
    s.stripOnChart = stripOnChart;
    store.saveSettings(s);
    cg::ChartRecord r;
    r.id = "20261015-090000";
    r.name = "Resumable";
    r.sourceGuid = kMapnik;
    r.bbox = {-1.5, 50.25, 2.125, 51.0};
    r.zooms = {8, 9};
    r.created = "2026-10-15T10:15:00";
    r.state = cg::ChartState::Interrupted;
    store.saveChart(r);
    std::ofstream(store.jobsDir() + "/" + r.id + "/job.ini") << "[Job]\n";
    cli.holdRun(true);
    pi_->OnToolbarToolCallback(7);
    MainDialog* main = mainDialog();
    ASSERT_NE(main, nullptr);
    wxChoice* choice = sourceChoiceOf(main);
    ASSERT_TRUE(runUntil([&] { return choice->IsEnabled() && choice->GetCount() == 1; }));
    wxWindow* resume = wxWindow::FindWindowByLabel("Resume", main);
    ASSERT_NE(resume, nullptr);
    wxCommandEvent press(wxEVT_BUTTON, resume->GetId());
    press.SetEventObject(resume);
    resume->GetEventHandler()->ProcessEvent(press);
    ASSERT_TRUE(runUntil([&] { return !cli.runArgv().empty(); })) << "the job child started";
    ASSERT_TRUE(lastStateBusy());
    ASSERT_TRUE(runUntil([&] {
        ProgressStrip* st = stripOf(ocpn_fake::canvasWindow);
        return st && st->IsShown();
    })) << "the progress strip did not appear";
    ProgressStrip* strip = stripOf(ocpn_fake::canvasWindow);
    ASSERT_NE(strip, nullptr) << "the progress strip exists";
    EXPECT_TRUE(strip->IsShown()) << "the progress strip is shown";
    EXPECT_EQ(strip->host(), stripOnChart ? ProgressStrip::Host::Canvas : ProgressStrip::Host::Window);

    const auto open = [&](const char* name) { step(name, true, [&] { pi_->OnToolbarToolCallback(7); }); };
    const auto closeBox = [&](const char* name) { step(name, false, [&] { main->Close(); }); };
    // New chart page: the close box, the Close button, Escape; each time reopened by the toolbar.
    closeBox("close box");
    open("reopen 1");
    step("Close button", false, [&] {
        wxCommandEvent b(wxEVT_BUTTON, wxID_CLOSE);
        b.SetEventObject(main);
        main->GetEventHandler()->ProcessEvent(b);
    });
    open("reopen 2");
    step("Escape", false, [&] {
        wxKeyEvent esc(wxEVT_CHAR_HOOK);
        esc.m_keyCode = WXK_ESCAPE;
        esc.SetEventObject(main);
        main->GetEventHandler()->ProcessEvent(esc);
    });
    // My charts page with the live row: the 1 s timer ticks (1.3 s) before the close.
    open("reopen 3");
    main->showMyCharts();
    {
        const auto t = std::chrono::steady_clock::now();
        pump(1300); // timer ticks: DispatchTimeout(20) returns each 20 ms, a blocked tick shows as overrun
        EXPECT_LT(msSince(t), 1300 + kLimitMs) << "My charts timer tick";
    }
    closeBox("close box on My charts");
    open("reopen 4");
    pump(1300);
    closeBox("close box on My charts again");
    for (int i = 0; i < 20; ++i) {
        SCOPED_TRACE(i);
        open("cycle open");
        if (i % 2)
            main->showMyCharts();
        else
            main->showNewChart(cg::AreaKind::View, std::nullopt);
        closeBox("cycle close");
    }
    // Hidden with the job running: the loop turns, no spin.
    iterations = 0;
    pump(1000);
    EXPECT_GT(iterations, 5) << "the loop keeps turning";
    EXPECT_LT(iterations, 500) << "no spin";
    EXPECT_TRUE(lastStateBusy()) << "the job is still running";
    // The job ends while the dialog is hidden, then open and close once more.
    cli.holdRun(false);
    EXPECT_TRUE(runUntil([] { return !lastStateBusy(); })) << "the job end was seen";
    open("open after end");
    closeBox("close after end");
}

TEST_F(CloseDuringJobTest, ClosingTheDialogWhileAJobRunsReturnsAtOnceWithTheStripInItsOwnWindow) {
    scenario(false);
}
TEST_F(CloseDuringJobTest, ClosingTheDialogWhileAJobRunsReturnsAtOnceWithTheStripOnTheChart) {
    scenario(true);
}
