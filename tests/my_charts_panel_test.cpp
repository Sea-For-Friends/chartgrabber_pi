// SPDX-License-Identifier: GPL-3.0-or-later
// the My charts page inside the real MainDialog, against a
// stand-in CLI (a small sh script in a temp directory, never the real CLI; the records it prints are the CLI
// records of cli-requirements.md 2 and 3). Expected texts and arguments are the literals of the spec. The
// popup menu
// ("More...") and the default Yes / No box need a user and are not driven: the tests call
// MyChartsPanel::act, the one dispatch the buttons and the menu share, and the confirmDelete hook.
// NOT covered: the menu items themselves, wxLaunchDefaultApplication (Show in folder), a CLI doing
// a real top-up, sizes and colour schemes.
#include "area_tool.h"
#include "config_store.h"
#include "job_controller.h"
#include "job_files.h"
#include "log_capture.h"
#include "look.h"
#include "main_dialog.h"
#include "my_charts_panel.h"
#include "ocpn_fakes.h"
#include "question_probe.h"
#include "wx_env.h"

#include <gtest/gtest.h>

#include <wx/app.h>
#include <wx/apptrait.h>
#include <wx/bitmap.h>
#include <wx/button.h>
#include <wx/dcmemory.h>
#include <wx/evtloop.h>
#include <wx/fileconf.h>
#include <wx/frame.h>
#include <wx/gauge.h>
#include <wx/image.h>
#include <wx/menu.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/stattext.h>
#include <wx/tglbtn.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include <ocpn_plugin.h>

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

const std::string kBase = "{11111111-1111-1111-1111-111111111111}";
const std::string kCyr = "\xD0\x9A\xD0\xB0\xD1\x80\xD1\x82\xD0\xB0 d'x"; // "Карта d'x"

const char* const kScript = R"SH(#!/bin/sh
[ "$1" = --version ] && { echo 'chart-grabber-cli 2026.10.6 (test)'; exit 0; } # probeCli: supported
prev=
for a in "$@"; do
  if [ "$prev" = --job ]; then ini=$a; fi
  prev=$a
done
[ -n "$ini" ] || exit 0 # not a job run (the sources query of the page): touch nothing
dir=$(dirname "$ini")
n=$(cat "$dir/runs" 2>/dev/null || echo 0)
n=$((n + 1))
echo $n > "$dir/runs"
printf '%s\n' "$@" > "$dir/args.$n"
mode=$(cat "$dir/mode.$n" 2>/dev/null)
case $mode in
  wait|feed) trap 'exit 143' TERM ;;
esac
if [ -f "$dir/out.$n" ]; then cat "$dir/out.$n"; fi
if [ -f "$dir/err.$n" ]; then cat "$dir/err.$n" >&2; fi
case $mode in
  wait) k=0; while [ $k -lt 1200 ]; do sleep 0.05; k=$((k + 1)); done ;;
  feed) k=0 #  the test appends stderr lines by renaming a file `feed.<run>` into place
    while [ $k -lt 1200 ]; do
      if [ -f "$dir/feed.$n" ]; then cat "$dir/feed.$n" >&2; rm -f "$dir/feed.$n"; fi
      sleep 0.05; k=$((k + 1))
    done ;;
esac
exit $(cat "$dir/code.$n" 2>/dev/null || echo 0)
)SH";

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}
void spit(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}
std::string u(const wxString& s) {
    return std::string(s.utf8_str());
}
std::string tsv(std::initializer_list<std::string> fields) {
    std::string out;
    for (const std::string& f : fields) {
        if (!out.empty())
            out += '\t';
        out += f;
    }
    return out + "\n";
}
std::string written(const std::string& path, int tiles, long bytes) {
    return tsv({"written", kBase, path, std::to_string(tiles), std::to_string(bytes)});
}
std::string fill(int tiles, int missing) {
    return tsv({"fill", kBase, "8", std::to_string(tiles), std::to_string(tiles - missing), "0",
                std::to_string(missing), "5000"});
}

template <class T> void collect(wxWindow* w, std::vector<T*>& out) {
    if (auto* t = dynamic_cast<T*>(w))
        out.push_back(t);
    for (wxWindow* c : w->GetChildren())
        collect(c, out);
}

struct Row {
    std::vector<std::string> texts;   // name, zoom, size, date, state (unreadable: name, state)
    std::vector<std::string> buttons; // labels left to right
    std::vector<bool> enabled;
    std::vector<wxButton*> widgets;
};

class ChartsTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!wxTheApp || !wxTheApp->IsGUI())
            GTEST_SKIP() << "no display: the My charts tests were NOT run";
        loop_.reset(wxTheApp->GetTraits()->CreateEventLoop());
        ASSERT_NE(loop_, nullptr);
        activator_ = std::make_unique<wxEventLoopActivator>(loop_.get());
        char tmpl[] = "/tmp/cg-mycharts-XXXXXX";
        ASSERT_NE(mkdtemp(tmpl), nullptr);
        root_ = tmpl;
        ocpn_fake::privateData = wxString::FromUTF8((root_ / "private").string());
        oldHome_ = getenv("HOME") ? getenv("HOME") : "";
        // The default maps folder is under HOME; not inside root_ (GTK's icon loader keeps HOME in the
        // command line of a sandbox process that outlives the test; expectNoProcessesUnder(root_) would
        // report it).
        home_ = (root_.parent_path() / ("home-of-" + root_.filename().string())).string();
        setenv("HOME", home_.c_str(), 1);
        ocpn_fake::touch = false;
        ocpn_fake::scale = 1.0;
        ocpn_fake::contentScale = 1.0;
        ocpn_fake::routes.clear();
        ocpn_fake::selectedRoute.clear();
        ocpn_fake::activeRoute.clear();
        ocpn_fake::underMouse = 0;
        ocpn_fake::refreshed.clear();
        ocpn_fake::jump = {};
        ocpn_fake::updates.clear();
        ocpn_fake::chartDirs.Clear();
        ocpn_fake::removed.clear();
        ocpn_fake::onUpdate = nullptr;
        cli_ = (root_ / "fake-cli").string();
        spit(cli_, kScript);
        chmod(cli_.c_str(), 0755);
        config_ = std::make_unique<wxFileConfig>(wxString("cgchartstest"), wxEmptyString,
                                                 wxString::FromUTF8((root_ / "opencpn.conf").string()),
                                                 wxEmptyString, wxCONFIG_USE_LOCAL_FILE);
        store_ = std::make_unique<ConfigStore>(config_.get());
        store_->setFirstSuccessShown();
        cg::Settings s = store_->loadSettings();
        s.cliPath = cli_;
        store_->saveSettings(s);
        frame_ = new wxFrame(nullptr, wxID_ANY, "canvas", wxPoint(50, 60), wxSize(800, 480));
        ocpn_fake::canvasWindow = frame_;
        ocpn_fake::canvases = {frame_};
        ocpn_fake::focus = frame_;
        area_ = std::make_unique<AreaTool>([] { return wxColour(10, 200, 30); }, [] { return false; });
        PlugIn_ViewPort vp{};
        vp.clat = 50;
        vp.clon = 10;
        vp.view_scale_ppm = 10;
        vp.pix_width = vp.pix_height = 200;
        vp.bValid = true;
        vp.lat_min = 45;
        vp.lat_max = 55;
        vp.lon_min = 5;
        vp.lon_max = 15;
        area_->rememberViewport(0, vp);
    }
    void TearDown() override {
        EXPECT_TRUE(ocpn_fake::removed.empty())
            << "RemoveChartFromDBInPlace hangs OpenCPN 5.14+: never called";
        ocpn_fake::onUpdate = nullptr;
        if (!frame_)
            return;
        if (jobs_) {
            jobs_->onBusyChanged = nullptr;
            jobs_->onChartsChanged = nullptr;
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
        activator_.reset();
        loop_.reset();
        std::error_code ec;
        setenv("HOME", oldHome_.c_str(), 1);
        std::error_code homeEc;
        expectNoProcessesUnder(root_.string());
        fs::remove_all(home_, homeEc);
        fs::remove_all(root_, ec);
    }

    // ---- disk
    fs::path jobDir(const std::string& id) const {
        return root_ / "private" / "plugins" / "chartgrabber" / "jobs" / id;
    }
    // A chart directory as the plugin leaves it: chart.ini and job.ini.
    cg::ChartRecord save(const std::string& id, const std::string& name, cg::ChartState state,
                         std::vector<int> zooms = {8, 9}, std::int64_t bytes = 0,
                         const std::string& created = "2026-10-15T10:15:00") {
        cg::ChartRecord r;
        r.id = id;
        r.name = name;
        r.sourceGuid = kBase;
        r.sourceName = "Source";
        r.bbox = {-1.5, 50.25, 2.125, 51.0};
        r.zooms = std::move(zooms);
        r.created = created;
        r.state = state;
        r.bytes = bytes;
        cg::JobSpec spec;
        spec.sourceGuid = kBase;
        spec.bbox = r.bbox;
        spec.zooms = r.zooms;
        spec.outFile = "/planned/a.mbtiles";
        spec.name = name;
        spit(jobDir(id) / "job.ini", cg::jobIni(spec));
        store_->saveChart(r);
        return r;
    }
    void damage(const std::string& id, const std::string& text = "garbage, no name\n") {
        spit(jobDir(id) / "chart.ini", text);
    }
    void scenario(const std::string& id, int run, const std::string& out, int code,
                  const std::string& mode = "", const std::string& err = "") {
        const std::string n = std::to_string(run);
        if (!out.empty())
            spit(jobDir(id) / ("out." + n), out);
        if (!err.empty())
            spit(jobDir(id) / ("err." + n), err);
        spit(jobDir(id) / ("code." + n), std::to_string(code));
        if (!mode.empty())
            spit(jobDir(id) / ("mode." + n), mode);
    }
    int runs(const std::string& id) const {
        const std::string t = slurp(jobDir(id) / "runs");
        return t.empty() ? 0 : std::stoi(t);
    }
    std::vector<std::string> argsOf(const std::string& id, int run) const {
        std::vector<std::string> out;
        std::istringstream in(slurp(jobDir(id) / ("args." + std::to_string(run))));
        for (std::string line; std::getline(in, line);)
            out.push_back(line);
        return out;
    }
    std::vector<std::string> runArgs(const std::string& id, const char* verb) const {
        return {verb,
                "--job",
                (jobDir(id) / "job.ini").string(),
                "--format=tsv",
                "--progress=lines",
                "--cache",
                (root_ / "private" / "plugins" / "chartgrabber" / "cache").string(),
                "--overwrite"};
    }

    // ---- dialog and window access
    void makeDialog() {
        jobs_ = std::make_unique<JobController>(*store_, look_, *area_);
        dialog_ = new MainDialog(frame_, *store_, look_, *area_, *jobs_);
        jobs_->onChartsChanged = [this] { dialog_->chartsChanged(); };
        jobs_->onBusyChanged = [this](bool b) { dialog_->busyChanged(b); };
    }
    MyChartsPanel* panel() {
        std::vector<MyChartsPanel*> v;
        collect(dialog_, v);
        return v.empty() ? nullptr : v.front();
    }
    std::string pageButton() {
        std::vector<wxToggleButton*> v;
        collect(dialog_, v);
        for (wxToggleButton* b : v)
            if (b->GetLabel().StartsWith("My charts"))
                return u(b->GetLabel());
        return "<none>";
    }
    std::vector<Row> rows() {
        wxTheApp->ProcessIdle(); // the deferred deletion of the old rows
        std::vector<wxScrolledWindow*> lists;
        collect(panel(), lists);
        std::vector<Row> out;
        if (lists.empty())
            return out;
        for (wxWindow* child : lists.front()->GetChildren()) {
            if (!child->IsShown() || !dynamic_cast<wxPanel*>(child))
                continue;
            std::vector<wxStaticText*> texts;
            std::vector<wxButton*> buttons;
            collect(child, texts);
            collect(child, buttons);
            Row r;
            for (wxStaticText* t : texts)
                r.texts.push_back(u(t->GetLabel()));
            for (wxButton* b : buttons) {
                r.buttons.push_back(u(b->GetLabel()));
                r.enabled.push_back(b->IsEnabled());
                r.widgets.push_back(b);
            }
            out.push_back(r);
        }
        return out;
    }
    static void click(wxButton* b) {
        ASSERT_NE(b, nullptr);
        wxCommandEvent e(wxEVT_BUTTON, b->GetId());
        e.SetEventObject(b);
        b->GetEventHandler()->ProcessEvent(e);
    }
    bool runUntil(const std::function<bool()>& done, int timeoutMs = 15000) {
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        while (!done()) {
            if (Clock::now() >= deadline)
                return false;
            loop_->DispatchTimeout(20);
        }
        return true;
    }
    bool waitIdle(int timeoutMs = 15000) {
        return runUntil([&] { return !jobs_->busy(); }, timeoutMs);
    }

    std::unique_ptr<wxEventLoopBase> loop_;
    std::unique_ptr<wxEventLoopActivator> activator_;
    fs::path root_;
    std::string cli_, oldHome_, home_;
    std::unique_ptr<wxFileConfig> config_;
    std::unique_ptr<ConfigStore> store_;
    Look look_;
    std::unique_ptr<AreaTool> area_;
    wxFrame* frame_ = nullptr;
    std::unique_ptr<JobController> jobs_;
    MainDialog* dialog_ = nullptr;
};

using Texts = std::vector<std::string>;

// Proof 1: every job directory, charts newest first by directory name (created in the "wrong" order, the
// date shown is `created`, not the id), then the unreadable ones; the texts and buttons of the table.
TEST_F(ChartsTest, ListsEveryDirectoryNewestFirstWithTextsAndButtonsOfTheTable) {
    save("20261013-070000", "Failed one", cg::ChartState::Failed, {8}, 1000000);
    save("20261015-090000", kCyr, cg::ChartState::Complete, {8, 9, 10, 11, 12, 13, 14}, 12345678,
         "2026-09-30T23:59:59");
    save("20261012-060000", "Busy one", cg::ChartState::Running, {8}, 0);
    auto missing = save("20261016-120000", "Miss", cg::ChartState::Missing, {8, 9, 10, 12}, 4567000,
                        "2026-10-16T12:00:00");
    missing.tiles = 1234;
    missing.missing = 56;
    store_->saveChart(missing);
    save("20261014-080000", "Half", cg::ChartState::Interrupted, {10}, 0, "2026-10-14T08:00:00");
    save("20261017-000000", "x", cg::ChartState::Complete);
    damage("20261017-000000");
    makeDialog();

    const std::vector<Row> r = rows();
    ASSERT_EQ(r.size(), 6u);
    EXPECT_EQ(r[0].texts, (Texts{"Miss", "z8-10,12", "4.6 MB", "2026-10-16", "56 of 1 234 tiles missing"}));
    EXPECT_EQ(r[0].buttons, (Texts{"Show", "More..."}));
    EXPECT_EQ(r[1].texts, (Texts{kCyr, "z8-14", "12 MB", "2026-09-30", "Complete"}));
    EXPECT_EQ(r[1].buttons, (Texts{"Show", "More..."}));
    EXPECT_EQ(r[2].texts, (Texts{"Half", "z10", "0.0 MB", "2026-10-14", "Stopped"}));
    EXPECT_EQ(r[2].buttons, (Texts{"Resume", "More..."}));
    EXPECT_EQ(r[3].texts, (Texts{"Failed one", "z8", "1.0 MB", "2026-10-15", "Failed"}));
    EXPECT_EQ(r[3].buttons, (Texts{"More..."}));
    EXPECT_EQ(r[4].texts, (Texts{"Busy one", "z8", "0.0 MB", "2026-10-15", "Downloading..."}));
    EXPECT_EQ(r[4].buttons, (Texts{"More..."}));
    // Unreadable: after the charts although its id is the newest; the id is the name; Delete only.
    EXPECT_EQ(r[5].texts, (Texts{"20261017-000000", "Unreadable"}));
    EXPECT_EQ(r[5].buttons, (Texts{"Delete"}));
    EXPECT_EQ(pageButton(), "My charts (6)");
}

// Proof 1 (refresh): the page button and the list follow chartsChanged and the page being shown.
TEST_F(ChartsTest, ListAndPageButtonFollowChartsChangedAndTheShownPage) {
    makeDialog();
    EXPECT_EQ(pageButton(), "My charts (0)");
    EXPECT_TRUE(rows().empty());

    save("20261015-090000", "One", cg::ChartState::Complete);
    dialog_->chartsChanged();
    EXPECT_EQ(rows().size(), 1u);
    EXPECT_EQ(pageButton(), "My charts (1)");

    save("20261016-090000", "Two", cg::ChartState::Complete);
    damage("20261014-000000", "no equals sign here");
    wxShowEvent hidden(panel()->GetId(), false);
    hidden.SetEventObject(panel());
    panel()->GetEventHandler()->ProcessEvent(hidden);
    EXPECT_EQ(rows().size(), 1u) << "hiding the page re-reads nothing";
    wxShowEvent shown(panel()->GetId(), true);
    shown.SetEventObject(panel());
    panel()->GetEventHandler()->ProcessEvent(shown);
    ASSERT_EQ(rows().size(), 3u);
    EXPECT_EQ(rows()[0].texts.front(), "Two");
    EXPECT_EQ(rows()[2].texts.front(), "20261014-000000");
}

// Proof 2: Show moves the view to the centre of the chart box, and only Show does.
TEST_F(ChartsTest, ShowJumpsToTheCentreOfTheBoxAndNoOtherActionMovesTheView) {
    save("20261015-090000", "Done", cg::ChartState::Complete);
    save("20261014-090000", "Half", cg::ChartState::Interrupted);
    makeDialog();
    scenario("20261014-090000", 1, written("/charts/h.mbtiles", 1, 1), 0);
    const std::vector<Row> r = rows();
    ASSERT_EQ(r.size(), 2u);
    EXPECT_EQ(ocpn_fake::jump.calls, 0);
    click(r[1].widgets[0]); // Resume
    ASSERT_TRUE(waitIdle());
    panel()->act(MyChartsPanel::Action::BuildFromSaved, "20261015-090000");
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(ocpn_fake::jump.calls, 0);
    click(rows()[0].widgets[0]); // Show of "Done" (the list is rebuilt after the jobs)
    ASSERT_EQ(ocpn_fake::jump.calls, 1);
    EXPECT_DOUBLE_EQ(ocpn_fake::jump.lat, 50.625);
    EXPECT_DOUBLE_EQ(ocpn_fake::jump.lon, 0.3125);
}

// Proof 2: Resume (button), Top up and Build from saved tiles (menu actions) start the stored job with the
// arguments of.x; the list shows the new state when the job ends.
TEST_F(ChartsTest, ResumeTopUpAndBuildStartTheStoredJobAndTheListShowsTheEnd) {
    auto half = save("20261014-080000", "Half", cg::ChartState::Interrupted);
    auto part = save("20261015-080000", "Part", cg::ChartState::Missing);
    part.tiles = 1000;
    part.missing = 7;
    store_->saveChart(part);
    save("20261016-080000", "Broken", cg::ChartState::Failed);
    makeDialog();

    scenario(half.id, 1, fill(500, 0) + written("/charts/half.mbtiles", 500, 9), 0);
    scenario(part.id, 1, fill(1000, 0) + written("/charts/part.mbtiles", 1000, 9), 0);
    scenario("20261016-080000", 1, written("/charts/broken.mbtiles", 3, 9), 0);
    ASSERT_EQ(rows().size(), 3u);

    EXPECT_EQ(rows()[0].texts.back(), "Failed");
    panel()->act(MyChartsPanel::Action::BuildFromSaved, "20261016-080000");
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(runs("20261016-080000"), 1);
    EXPECT_EQ(argsOf("20261016-080000", 1), runArgs("20261016-080000", "export"));
    EXPECT_EQ(rows()[0].texts.back(), "Complete");

    EXPECT_EQ(rows()[1].texts.back(), "7 of 1 000 tiles missing");
    panel()->act(MyChartsPanel::Action::TopUp, part.id);
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(argsOf(part.id, 1), runArgs(part.id, "run"));
    EXPECT_EQ(rows()[1].texts.back(), "Complete");

    EXPECT_EQ(rows()[2].buttons, (Texts{"Resume", "More..."}));
    click(rows()[2].widgets[0]);
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(argsOf(half.id, 1), runArgs(half.id, "run"));
    EXPECT_EQ(rows()[2].buttons, (Texts{"Show", "More..."}));
    EXPECT_EQ(rows()[2].texts.back(), "Complete");
}

// Update... is in the More... menu of Complete and Missing rows only, disabled while a job
// runs.
TEST_F(ChartsTest, UpdateIsInTheMenuOfCompleteAndMissingRowsOnly) {
    makeDialog();
    const auto items = [&](cg::ChartState state) {
        cg::ChartRecord r;
        r.state = state;
        wxMenu menu;
        panel()->fillMenu(menu, r);
        std::vector<std::string> out;
        for (wxMenuItem* i : menu.GetMenuItems())
            out.push_back(u(i->GetItemLabel()) + (i->IsEnabled() ? "" : " (off)"));
        return out;
    };
    const Texts withUpdate{"Top up", "Update...", "Build from saved tiles", "Delete", "Show in folder"};
    EXPECT_EQ(items(cg::ChartState::Complete), withUpdate);
    EXPECT_EQ(items(cg::ChartState::Missing), withUpdate);
    EXPECT_EQ(items(cg::ChartState::Interrupted),
              (Texts{"Build from saved tiles", "Delete", "Show in folder"}));
    EXPECT_EQ(items(cg::ChartState::Failed), (Texts{"Build from saved tiles", "Delete", "Show in folder"}));
    EXPECT_EQ(items(cg::ChartState::Running), (Texts{"Show in folder"}));

    auto busy = save("20261014-080000", "Busy", cg::ChartState::Interrupted);
    scenario(busy.id, 1, "", 0, "wait");
    panel()->refresh();
    panel()->act(MyChartsPanel::Action::Resume, busy.id);
    ASSERT_TRUE(jobs_->busy());
    EXPECT_EQ(items(cg::ChartState::Complete),
              (Texts{"Top up (off)", "Update... (off)", "Build from saved tiles (off)", "Delete (off)",
                     "Show in folder"}));
    jobs_->stop();
    ASSERT_TRUE(waitIdle());
}

// and 6: "1 month" on a fixed clock starts a job whose argv carries the date; Cancel starts
// nothing and leaves the record; over the slow-mode budget the question comes first (No: nothing, no date);
// the row's Resume of an interrupted update starts it again with the same date.
TEST_F(ChartsTest, UpdateChoiceStartsTheJobWithTheDateCancelAndBudgetNoStartNothing) {
    const auto chart = save("20261014-080000", kCyr, cg::ChartState::Complete, {8, 9}, 5'000'000);
    makeDialog();
    wxString asked;
    int choice = 1;
    int budgetAsks = 0;
    bool budgetAnswer = false;
    panel()->now = [] { return wxDateTime(31, wxDateTime::Mar, 2026, 7, 8, 9); };
    panel()->chooseUpdate = [&](const wxString& q, std::function<void(int)> answer) {
        asked = q;
        answer(choice);
    };
    panel()->confirmBudget = [&](const wxString&, std::function<void(bool)> answer) {
        ++budgetAsks;
        answer(budgetAnswer);
    };
    const std::vector<std::string> head = {
        "run",
        "--job",
        (jobDir(chart.id) / "job.ini").string(),
        "--format=tsv",
        "--progress=lines",
        "--cache",
        (root_ / "private" / "plugins" / "chartgrabber" / "cache").string(),
        "--overwrite"};
    const std::string before = slurp(jobDir(chart.id) / "chart.ini");

    choice = -1; // Cancel
    panel()->act(MyChartsPanel::Action::Update, chart.id);
    EXPECT_EQ(u(asked), "Download again the tiles of \"" + kCyr + "\" that are older than:");
    EXPECT_EQ(runs(chart.id), 0);
    EXPECT_EQ(slurp(jobDir(chart.id) / "chart.ini"), before);

    cg::Settings s = store_->loadSettings(); // slow mode, 1 MB budget < the chart's 5 MB
    s.slowConnection = true;
    s.dataBudgetMb = 1;
    store_->saveSettings(s);
    choice = 1;
    panel()->act(MyChartsPanel::Action::Update, chart.id); // No
    EXPECT_EQ(budgetAsks, 1);
    EXPECT_EQ(runs(chart.id), 0);
    EXPECT_EQ(slurp(jobDir(chart.id) / "chart.ini"), before) << "No sets no date";

    s.dataBudgetMb = 5; // 5 MB is not above 5 MB: no question
    store_->saveSettings(s);
    panel()->act(MyChartsPanel::Action::Update, chart.id);
    EXPECT_EQ(budgetAsks, 1);
    ASSERT_TRUE(jobs_->busy());
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(argsOf(chart.id, 1),
              (std::vector<std::string>{"run", "--job", head[2], "--format=tsv", "--progress=lines",
                                        "--cache", head[6], "--threads", "1", "--overwrite", "--replace",
                                        "--replace-older-than=2026-02-28T07:08:09"}));
    EXPECT_EQ(rows()[0].texts.back(), "Complete");

    s.dataBudgetMb = 1; // Yes: starts, with the 6 months date
    store_->saveSettings(s);
    budgetAnswer = true;
    choice = 2;
    scenario(chart.id, 2, "", 143, "wait");
    panel()->act(MyChartsPanel::Action::Update, chart.id);
    EXPECT_EQ(budgetAsks, 2);
    ASSERT_TRUE(jobs_->busy());
    ASSERT_TRUE(runUntil([&] { return !argsOf(chart.id, 2).empty(); }));
    jobs_->stop();
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(argsOf(chart.id, 2).back(), "--replace-older-than=2025-09-30T07:08:09");

    s.slowConnection = false;
    store_->saveSettings(s);
    EXPECT_EQ(rows()[0].buttons, (Texts{"Resume", "More..."}));
    click(rows()[0].widgets[0]);
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(argsOf(chart.id, 3),
              (std::vector<std::string>{"run", "--job", head[2], "--format=tsv", "--progress=lines",
                                        "--cache", head[6], "--overwrite", "--replace",
                                        "--replace-older-than=2025-09-30T07:08:09"}));
    EXPECT_EQ(rows()[0].texts.back(), "Complete");
}

// Proof 3: while a job runs every action but Show is disabled and refused; Show still works; the buttons
// come back when the job ends.
TEST_F(ChartsTest, WhileAJobRunsOnlyShowIsEnabledAndTheRowsFollowTheEnd) {
    save("20261014-080000", "Done", cg::ChartState::Complete);
    save("20261013-080000", "Half", cg::ChartState::Interrupted);
    save("20261012-080000", "bad", cg::ChartState::Complete);
    damage("20261012-080000");
    auto run = save("20261015-080000", "Runner", cg::ChartState::Interrupted);
    scenario(run.id, 1, "", 0, "wait");
    makeDialog();
    jobs_->start(run, cg::RunKind::Download, true);
    ASSERT_TRUE(jobs_->busy());

    std::vector<Row> r = rows();
    ASSERT_EQ(r.size(), 4u);
    EXPECT_EQ(r[0].texts.back(), "Working out the size..."); // the live row
    EXPECT_EQ(r[1].buttons, (Texts{"Show", "More..."}));
    EXPECT_EQ(r[1].enabled, (std::vector<bool>{true, true}));
    EXPECT_EQ(r[2].buttons, (Texts{"Resume", "More..."}));
    EXPECT_FALSE(r[2].enabled[0]);
    EXPECT_EQ(r[3].buttons, (Texts{"Delete"}));
    EXPECT_FALSE(r[3].enabled[0]);

    bool asked = false;
    panel()->confirmDelete = [&](const wxString&, std::function<void(bool)> answer) {
        asked = true;
        answer(true);
    };
    panel()->act(MyChartsPanel::Action::Resume, "20261013-080000");
    panel()->act(MyChartsPanel::Action::TopUp, "20261014-080000");
    panel()->act(MyChartsPanel::Action::BuildFromSaved, "20261014-080000");
    panel()->act(MyChartsPanel::Action::Delete, "20261014-080000");
    panel()->act(MyChartsPanel::Action::Delete, "20261012-080000");
    EXPECT_EQ(runs("20261013-080000") + runs("20261014-080000"), 0);
    EXPECT_FALSE(asked);
    EXPECT_TRUE(fs::exists(jobDir("20261014-080000")));
    EXPECT_TRUE(fs::exists(jobDir("20261012-080000")));
    EXPECT_TRUE(ocpn_fake::updates.empty());

    click(r[1].widgets[0]); // Show works
    EXPECT_EQ(ocpn_fake::jump.calls, 1);

    jobs_->stop();
    ASSERT_TRUE(waitIdle(10000));
    r = rows();
    ASSERT_EQ(r.size(), 4u);
    EXPECT_EQ(r[0].texts.back(), "Stopped");
    EXPECT_EQ(r[0].buttons, (Texts{"Resume", "More..."}));
    EXPECT_TRUE(r[2].enabled[0]);
    EXPECT_TRUE(r[3].enabled[0]);
}

// Proof 2: Delete asks first (the text of the name with a quote and Cyrillic), No keeps
// everything and rescans nothing; Yes removes the existing files (one is missing) and the whole job
// directory, THEN makes one UpdateChartDBInplace with the array GetChartDBDirArrayString returned
// (force and progress off); the list shrinks. Another chart is untouched. RemoveChartFromDBInPlace is never
// called (checked in TearDown).
TEST_F(ChartsTest, DeleteAsksThenRemovesTheFilesAndTheDirectoryThenRescansOnce) {
    const fs::path charts = root_ / "my charts";
    const std::string a = (charts / "\xD0\x9A \x27q.mbtiles").string(); // "К 'q.mbtiles" with a space
    const std::string b = (charts / "b.mbtiles").string();
    const std::string gone = (charts / "never written.mbtiles").string();
    spit(a, "a");
    spit(b, "b");
    auto doomed = save("20261015-090000", kCyr, cg::ChartState::Complete);
    doomed.files = {a, gone, b};
    store_->saveChart(doomed);
    spit(jobDir(doomed.id) / "args.1", "leftover");
    save("20261016-090000", "Keep", cg::ChartState::Complete);
    makeDialog();
    ocpn_fake::chartDirs.Add("/charts/one");
    ocpn_fake::chartDirs.Add("/charts/\xD0\x96 two");
    bool goneAtCall = false;
    ocpn_fake::onUpdate = [&] {
        goneAtCall = !fs::exists(a) && !fs::exists(b) && !fs::exists(jobDir(doomed.id));
    };

    std::vector<std::string> questions;
    bool answer = false;
    panel()->confirmDelete = [&](const wxString& q, std::function<void(bool)> done) {
        questions.push_back(u(q));
        done(answer);
    };
    panel()->act(MyChartsPanel::Action::Delete, doomed.id);
    ASSERT_EQ(questions.size(), 1u);
    EXPECT_EQ(questions[0],
              "Delete \"" + kCyr + "\"? The chart is removed from OpenCPN and its files are deleted.");
    EXPECT_TRUE(ocpn_fake::updates.empty());
    EXPECT_TRUE(fs::exists(a) && fs::exists(b) && fs::exists(jobDir(doomed.id)));
    EXPECT_EQ(rows().size(), 2u);

    answer = true;
    panel()->act(MyChartsPanel::Action::Delete, doomed.id);
    ASSERT_EQ(ocpn_fake::updates.size(), 1u);
    EXPECT_TRUE(goneAtCall) << "the rescan comes after the files and the job directory are gone";
    EXPECT_EQ(ocpn_fake::updates[0].dirs, ocpn_fake::chartDirs);
    EXPECT_FALSE(ocpn_fake::updates[0].force);
    EXPECT_FALSE(ocpn_fake::updates[0].progress);
    EXPECT_FALSE(fs::exists(a));
    EXPECT_FALSE(fs::exists(b));
    EXPECT_FALSE(fs::exists(jobDir(doomed.id)));
    EXPECT_TRUE(fs::exists(jobDir("20261016-090000") / "chart.ini"));
    ASSERT_EQ(rows().size(), 1u);
    EXPECT_EQ(rows()[0].texts.front(), "Keep");
    EXPECT_EQ(pageButton(), "My charts (1)");
}

// Proof 3: an unreadable directory is removed by its Delete button; OpenCPN is not asked to remove anything.
TEST_F(ChartsTest, UnreadableDirectoryIsDeletedByItsButton) {
    save("20261015-090000", "x", cg::ChartState::Complete);
    damage("20261015-090000");
    save("20261016-090000", "Keep", cg::ChartState::Complete);
    makeDialog();
    std::vector<std::string> questions;
    bool answer = false;
    panel()->confirmDelete = [&](const wxString& q, std::function<void(bool)> done) {
        questions.push_back(u(q));
        done(answer);
    };
    const std::vector<Row> r = rows();
    ASSERT_EQ(r.size(), 2u);
    ASSERT_EQ(r[1].buttons, (Texts{"Delete"}));
    click(r[1].widgets[0]);
    ASSERT_EQ(questions.size(), 1u);
    EXPECT_EQ(questions[0],
              "Delete \"20261015-090000\"? The chart is removed from OpenCPN and its files are deleted.");
    EXPECT_TRUE(fs::exists(jobDir("20261015-090000")));
    EXPECT_EQ(pageButton(), "My charts (2)");
    answer = true;
    click(rows()[1].widgets[0]);
    EXPECT_FALSE(fs::exists(jobDir("20261015-090000")));
    EXPECT_TRUE(fs::exists(jobDir("20261016-090000")));
    EXPECT_TRUE(ocpn_fake::updates.empty());
    EXPECT_EQ(rows().size(), 1u);
    EXPECT_EQ(pageButton(), "My charts (1)");
}

// ---- : the questions are modeless Question windows; the action happens only on the answer
// ----

// Delete (a chart row and an unreadable row) with the DEFAULT seam: the call returns at once with the
// question shown and the panel disabled; nothing happens before the answer; a second request raises the same
// window; No changes nothing and enables the panel; a new request after the answer opens a new question; Yes
// acts.
TEST_F(ChartsTest, TheDeleteQuestionIsAModelessWindowAndTheActionHappensOnlyOnTheAnswer) {
    const fs::path file = root_ / "my charts" / "a.mbtiles";
    spit(file, "a");
    auto doomed = save("20261015-090000", kCyr, cg::ChartState::Complete);
    doomed.files = {file.string()};
    store_->saveChart(doomed);
    save("20261014-090000", "bad", cg::ChartState::Complete);
    damage("20261014-090000");
    makeDialog();
    int deleted = 0;
    panel()->onDeleted = [&] { ++deleted; };
    struct Row {
        const char* name;
        std::string id, text;
        fs::path gone;
    };
    const std::vector<Row> rows = {
        {"chart row", doomed.id,
         "Delete \"" + kCyr + "\"? The chart is removed from OpenCPN and its files are deleted.", file},
        {"unreadable row", "20261014-090000",
         "Delete \"20261014-090000\"? The chart is removed from OpenCPN and its files are deleted.",
         jobDir("20261014-090000")},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        ASSERT_TRUE(qprobe::shown().empty());
        const int deletedBefore = deleted;
        const std::size_t updatesBefore = ocpn_fake::updates.size();
        panel()->act(MyChartsPanel::Action::Delete, row.id);
        ASSERT_EQ(qprobe::shown().size(), 1u) << "returned at once with the question shown";
        Question* q = qprobe::shown()[0];
        EXPECT_EQ(qprobe::text(q), row.text);
        EXPECT_EQ(q->GetTitle(), "Chart Grabber");
        EXPECT_EQ(q->GetParent(), static_cast<wxWindow*>(dialog_)) << "the top-level window of the panel";
        EXPECT_FALSE(q->IsModal());
        EXPECT_FALSE(panel()->IsEnabled());
        EXPECT_TRUE(fs::exists(row.gone));
        EXPECT_EQ(ocpn_fake::updates.size(), updatesBefore);
        panel()->act(MyChartsPanel::Action::Delete, row.id); // a second request raises it
        EXPECT_EQ(qprobe::shown().size(), 1u);
        qprobe::press(q, wxID_NO);
        EXPECT_TRUE(panel()->IsEnabled());
        EXPECT_TRUE(fs::exists(row.gone)) << "No keeps everything";
        EXPECT_EQ(deleted, deletedBefore);
        ASSERT_TRUE(qprobe::shown().empty());
        panel()->act(MyChartsPanel::Action::Delete, row.id); // after the answer: a new question
        ASSERT_EQ(qprobe::shown().size(), 1u);
        EXPECT_NE(qprobe::shown()[0], q);
        qprobe::press(qprobe::shown()[0], wxID_YES);
        EXPECT_TRUE(panel()->IsEnabled());
        EXPECT_FALSE(fs::exists(row.gone));
        EXPECT_EQ(deleted, deletedBefore + 1);
        EXPECT_EQ(ocpn_fake::updates.size(), updatesBefore + (row.id == doomed.id ? 1u : 0u))
            << "one rescan for the chart row only";
        wxTheApp->ProcessIdle();
    }
}

// Five deletes in a row: each one removes its files and rescans exactly once.
TEST_F(ChartsTest, FiveDeletesInARowRescanOncePerDelete) {
    std::vector<std::string> ids;
    for (int i = 0; i < 5; ++i) {
        std::string file = "c", id = "2026101", name = "C"; // GCC 12 -O3 -Wrestrict: no "lit" + to_string(i)
        file += std::to_string(i) + ".mbtiles";
        id += std::to_string(i) + "-090000";
        name += std::to_string(i);
        const fs::path f = root_ / "my charts" / file;
        spit(f, "x");
        auto c = save(id, name, cg::ChartState::Complete);
        c.files = {f.string()};
        store_->saveChart(c);
        ids.push_back(c.id);
    }
    makeDialog();
    panel()->confirmDelete = [](const wxString&, std::function<void(bool)> done) { done(true); };
    for (std::size_t i = 0; i < ids.size(); ++i) {
        panel()->act(MyChartsPanel::Action::Delete, ids[i]);
        EXPECT_EQ(ocpn_fake::updates.size(), i + 1);
        EXPECT_EQ(rows().size(), ids.size() - i - 1);
    }
}

// An answer that arrives after the state changed does nothing: the chart is gone / a job runs.
TEST_F(ChartsTest, ADeleteAnswerAfterTheChartWentOrAJobStartedDoesNothing) {
    const fs::path file = root_ / "my charts" / "b.mbtiles";
    spit(file, "b");
    auto doomed = save("20261015-090000", "Doomed", cg::ChartState::Complete);
    doomed.files = {file.string()};
    store_->saveChart(doomed);
    save("20261014-090000", "bad", cg::ChartState::Complete);
    damage("20261014-090000");
    auto runner = save("20261013-090000", "Runner", cg::ChartState::Interrupted);
    scenario(runner.id, 1, "", 0, "wait");
    makeDialog();
    int deleted = 0;
    panel()->onDeleted = [&] { ++deleted; };
    std::function<void(bool)> answer;
    int asked = 0;
    panel()->confirmDelete = [&](const wxString&, std::function<void(bool)> a) {
        ++asked;
        answer = std::move(a);
    };

    panel()->act(MyChartsPanel::Action::Delete, doomed.id); // the chart goes while the question is open
    ASSERT_EQ(asked, 1);
    EXPECT_FALSE(panel()->IsEnabled());
    store_->deleteChartDir(doomed.id);
    panel()->refresh();
    answer(true);
    EXPECT_TRUE(panel()->IsEnabled());
    EXPECT_EQ(deleted, 0);
    EXPECT_TRUE(ocpn_fake::updates.empty());
    EXPECT_TRUE(fs::exists(file)) << "the answer for a vanished chart deletes nothing";

    save(doomed.id, "Doomed", cg::ChartState::Complete); // back; a job starts while the question is open
    panel()->refresh();
    panel()->act(MyChartsPanel::Action::Delete, doomed.id);
    ASSERT_EQ(asked, 2);
    jobs_->start(runner, cg::RunKind::Download, true);
    ASSERT_TRUE(jobs_->busy());
    answer(true);
    EXPECT_TRUE(panel()->IsEnabled());
    EXPECT_TRUE(fs::exists(jobDir(doomed.id)));
    EXPECT_EQ(deleted, 0);
    EXPECT_TRUE(ocpn_fake::updates.empty()) << "no rescan while a job runs";

    // The unreadable row: the same, with the job still running.
    jobs_->stop();
    ASSERT_TRUE(waitIdle(10000));
    panel()->act(MyChartsPanel::Action::Delete, "20261014-090000");
    ASSERT_EQ(asked, 3);
    jobs_->start(runner, cg::RunKind::Download, true);
    scenario(runner.id, 2, "", 0, "wait");
    ASSERT_TRUE(jobs_->busy());
    answer(true);
    EXPECT_TRUE(fs::exists(jobDir("20261014-090000")));
    EXPECT_EQ(deleted, 0);
    jobs_->stop();
    ASSERT_TRUE(waitIdle(10000));
}

// Update with the DEFAULT seams: the choice window (four radio buttons, OK / Cancel), then in slow mode over
// the budget the question, then the start with the date taken from now AFTER the last answer.
TEST_F(ChartsTest, TheUpdateChoiceAndTheBudgetQuestionAreWindowsAndTheJobStartsAfterTheLastAnswer) {
    const auto chart = save("20261014-080000", kCyr, cg::ChartState::Complete, {8, 9}, 5'000'000);
    cg::Settings s = store_->loadSettings();
    s.slowConnection = true;
    s.dataBudgetMb = 1;
    store_->saveSettings(s);
    makeDialog();
    int nowCalls = 0;
    panel()->now = [&] {
        ++nowCalls;
        return wxDateTime(31, wxDateTime::Mar, 2026, 7, 8, 9);
    };
    panel()->act(MyChartsPanel::Action::Update, chart.id);
    ASSERT_EQ(qprobe::shown().size(), 1u);
    Question* choice = qprobe::shown()[0];
    EXPECT_EQ(choice->GetTitle(), "Update chart");
    EXPECT_EQ(qprobe::text(choice), "Download again the tiles of \"" + kCyr + "\" that are older than:");
    const auto radios = qprobe::radios(choice);
    ASSERT_EQ(radios.size(), 4u);
    EXPECT_EQ(radios[0]->GetLabel(), "Today (all tiles)");
    EXPECT_EQ(radios[1]->GetLabel(), "1 month");
    EXPECT_EQ(radios[2]->GetLabel(), "6 months");
    EXPECT_EQ(radios[3]->GetLabel(), "1 year");
    EXPECT_TRUE(radios[0]->GetValue());
    EXPECT_FALSE(panel()->IsEnabled());
    panel()->act(MyChartsPanel::Action::Update, chart.id); // raises, no second window
    EXPECT_EQ(qprobe::shown().size(), 1u);
    EXPECT_EQ(runs(chart.id), 0);

    qprobe::press(choice, wxID_CANCEL); // Cancel: nothing
    EXPECT_TRUE(panel()->IsEnabled());
    EXPECT_EQ(runs(chart.id), 0);
    EXPECT_TRUE(qprobe::shown().empty());

    panel()->act(MyChartsPanel::Action::Update, chart.id);
    ASSERT_EQ(qprobe::shown().size(), 1u);
    qprobe::radios(qprobe::shown()[0])[1]->SetValue(true);
    qprobe::press(qprobe::shown()[0], wxID_OK); // "1 month" -> the budget question, asked in this answer
    ASSERT_EQ(qprobe::shown().size(), 1u);
    Question* budget = qprobe::shown()[0];
    EXPECT_EQ(qprobe::text(budget), "This download is about 5.0 MB. Your limit is 1 MB. Download anyway?");
    EXPECT_FALSE(panel()->IsEnabled());
    EXPECT_EQ(nowCalls, 0) << "the clock is read after the last answer";
    EXPECT_EQ(runs(chart.id), 0);
    nowCalls = 0;
    qprobe::press(budget, wxID_NO);
    EXPECT_TRUE(panel()->IsEnabled());
    EXPECT_EQ(runs(chart.id), 0);
    EXPECT_EQ(nowCalls, 0);

    panel()->act(MyChartsPanel::Action::Update, chart.id);
    qprobe::radios(qprobe::shown().at(0))[1]->SetValue(true);
    qprobe::press(qprobe::shown()[0], wxID_OK);
    ASSERT_EQ(qprobe::shown().size(), 1u);
    qprobe::press(qprobe::shown()[0], wxID_YES);
    EXPECT_EQ(nowCalls, 1);
    ASSERT_TRUE(jobs_->busy());
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(argsOf(chart.id, 1).back(), "--replace-older-than=2026-02-28T07:08:09");
}

// Stale Update answers: a job started while the choice / budget question was open starts nothing.
TEST_F(ChartsTest, AnUpdateAnswerAfterAJobStartedOrTheChartWentStartsNothing) {
    const auto chart = save("20261014-080000", "Upd", cg::ChartState::Complete, {8, 9}, 5'000'000);
    auto runner = save("20261013-080000", "Runner", cg::ChartState::Interrupted);
    scenario(runner.id, 1, "", 0, "wait");
    cg::Settings s =
        store_->loadSettings(); // slow mode, 1 MB budget < the chart's 5 MB: the budget question follows
    s.slowConnection = true;
    s.dataBudgetMb = 1;
    store_->saveSettings(s);
    makeDialog();
    std::function<void(int)> choose;
    std::function<void(bool)> budget;
    int budgetAsks = 0;
    panel()->chooseUpdate = [&](const wxString&, std::function<void(int)> a) { choose = std::move(a); };
    panel()->confirmBudget = [&](const wxString&, std::function<void(bool)> a) {
        ++budgetAsks;
        budget = std::move(a);
    };
    panel()->now = [] { return wxDateTime(31, wxDateTime::Mar, 2026, 7, 8, 9); };

    panel()->act(MyChartsPanel::Action::Update, chart.id); // a job starts while the choice is open
    ASSERT_TRUE(choose);
    EXPECT_FALSE(panel()->IsEnabled());
    jobs_->start(runner, cg::RunKind::Download, true);
    ASSERT_TRUE(jobs_->busy());
    choose(1);
    EXPECT_TRUE(panel()->IsEnabled());
    EXPECT_EQ(runs(chart.id), 0);
    EXPECT_EQ(budgetAsks, 0);
    jobs_->stop();
    ASSERT_TRUE(waitIdle(10000));

    choose = nullptr; // the budget question is open, then a job starts
    panel()->act(MyChartsPanel::Action::Update, chart.id);
    choose(2);
    ASSERT_EQ(budgetAsks, 1);
    EXPECT_FALSE(panel()->IsEnabled());
    scenario(runner.id, 2, "", 0, "wait");
    jobs_->start(runner, cg::RunKind::Download, true);
    ASSERT_TRUE(jobs_->busy());
    {
        LogCapture
            log; // JobController::start would log (and refuse) a start of its own: the panel must not call it
        budget(true);
        for (const std::string& line : log.lines)
            EXPECT_EQ(line.find("job already running"), std::string::npos) << line;
    }
    EXPECT_TRUE(panel()->IsEnabled());
    EXPECT_EQ(runs(chart.id), 0);
    jobs_->stop();
    ASSERT_TRUE(waitIdle(10000));

    panel()->act(MyChartsPanel::Action::Update, chart.id); // the budget question is open, the chart goes
    choose(2);
    ASSERT_EQ(budgetAsks, 2);
    store_->deleteChartDir(chart.id);
    panel()->refresh();
    budget(true);
    EXPECT_EQ(runs(chart.id), 0);
}

// The panel's destructor deletes an open question at once and its callback never runs. The panel is created
// on its own under the frame: its question is a child of the frame, so only the panel's own delete removes
// it.
TEST_F(ChartsTest, DeletingThePanelDeletesTheOpenQuestionAtOnce) {
    const auto chart = save("20261015-090000", "Doomed", cg::ChartState::Complete);
    makeDialog();
    auto* standalone = new MyChartsPanel(frame_, *store_, look_, *area_, *jobs_);
    standalone->act(MyChartsPanel::Action::Delete, chart.id);
    ASSERT_EQ(qprobe::shown().size(), 1u);
    wxWeakRef<Question> q(qprobe::shown()[0]);
    EXPECT_EQ(q->GetParent(), static_cast<wxWindow*>(frame_));
    delete standalone;
    EXPECT_FALSE(q) << "no idle ran in between";
    EXPECT_TRUE(fs::exists(jobDir(chart.id))) << "its callback never ran";

    auto* second = new MyChartsPanel(frame_, *store_, look_, *area_, *jobs_); // the Update choice, same
    second->act(MyChartsPanel::Action::Update, chart.id);
    ASSERT_EQ(qprobe::shown().size(), 1u);
    wxWeakRef<Question> u(qprobe::shown()[0]);
    delete second;
    EXPECT_FALSE(u);
}

// ------------------------------------------------------------------------------------- the bars

std::string progressLine(int processed) {
    return tsv({"progress", "download", kBase, "9", std::to_string(processed), "100", "0", "0", "20"});
}

// the weather rows. The date column is the picture's time and age (clock from the epoch, no zone);
// the credit line is below the third line, ellipsized, with the full text as tool tip.
struct MyChartsWeatherTest : ChartsTest {
    cg::ChartRecord weather(const std::string& id, cg::ChartState state, const std::string& snapshot,
                            const std::string& credit) {
        auto r = save(id, "Rain", state);
        r.weather = true;
        r.snapshot = snapshot;
        r.credit = credit;
        store_->saveChart(r);
        return r;
    }
    wxStaticText* creditLabel() {
        std::vector<wxStaticText*> t;
        collect(panel(), t);
        for (wxStaticText* x : t)
            if (u(x->GetLabel()).rfind("Credit: ", 0) == 0)
                return x;
        return nullptr;
    }
    std::vector<std::string> weatherItems(const cg::ChartRecord& r) {
        wxMenu menu;
        panel()->fillMenu(menu, r);
        std::vector<std::string> out;
        for (wxMenuItem* i : menu.GetMenuItems())
            out.push_back(u(i->GetItemLabel()) + (i->IsEnabled() ? "" : " (off)"));
        return out;
    }
};

TEST_F(MyChartsWeatherTest, TheRowShowsThePictureTimeAgeAndTheCreditLine) {
    const std::string rain = "Weather data by RainViewer (https://www.rainviewer.com)";
    weather("20261016-050000", cg::ChartState::Complete, "2026-10-15T20:10:00Z", rain);
    weather("20261016-040000", cg::ChartState::Complete, "", "");        // no picture time, no credit
    weather("20261016-030000", cg::ChartState::Complete, "garbage", ""); // unparseable
    auto h = weather("20261016-020000", cg::ChartState::Interrupted, "2026-10-15T20:10:00Z",
                     "Cr\xC3\xA9"
                     "dit");
    h.done = 600;
    store_->saveChart(h);
    save("20261016-010000", "Plain", cg::ChartState::Complete); // not weather: unchanged
    makeDialog();
    panel()->now = [] { return wxDateTime(static_cast<time_t>(1792106400)); }; // 2026-10-15T23:20:00Z
    panel()->refresh();
    const std::vector<Row> r = rows();
    ASSERT_EQ(r.size(), 5u);
    EXPECT_EQ(r[0].texts, (Texts{"Rain", "z8-9", "0.0 MB", "2026-10-15 20:10 UTC (3 h old)",
                                 "Credit: " + rain, "Complete"}));
    EXPECT_EQ(r[1].texts, (Texts{"Rain", "z8-9", "0.0 MB", "2026-10-15", "Complete"}));
    EXPECT_EQ(r[2].texts, (Texts{"Rain", "z8-9", "0.0 MB", "2026-10-15", "Complete"}));
    EXPECT_EQ(r[3].texts,
              (Texts{"Rain", "z8-9", "0.0 MB", "2026-10-15 20:10 UTC (3 h old)", "Stopped at 60 %",
                     "Credit: Cr\xC3\xA9"
                     "dit"}));
    EXPECT_EQ(r[4].texts, (Texts{"Plain", "z8-9", "0.0 MB", "2026-10-15", "Complete"}));
}

TEST_F(MyChartsWeatherTest, ALongCreditIsEllipsizedAndItsToolTipIsTheFullText) {
    const std::string nasa = "Precipitation: NASA GPM IMERG via NASA Global Imagery Browse Services (GIBS), "
                             "part of the NASA Earth "
                             "Science Data and Information System (ESDIS)";
    ASSERT_EQ(nasa.size(), 144u);
    weather("20261016-050000", cg::ChartState::Complete, "2026-10-15T20:10:00Z", nasa);
    makeDialog();
    wxStaticText* label = creditLabel();
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(u(label->GetLabel()), "Credit: " + nasa);
    // the sizer takes the minimum size (not GetBestSize, which stays the text's): the row cannot widen the
    // page
    EXPECT_LT(label->GetEffectiveMinSize().x, label->GetTextExtent(label->GetLabel()).x / 2);
    ASSERT_NE(label->GetToolTip(), nullptr);
    EXPECT_EQ(u(label->GetToolTipText()), "Credit: " + nasa);
}

TEST_F(MyChartsWeatherTest, TheMenusOfAWeatherRow) {
    makeDialog();
    const auto items = [&](cg::ChartState state) {
        cg::ChartRecord r;
        r.weather = true;
        r.state = state;
        return weatherItems(r);
    };
    const Texts update{"Update to latest weather", "Delete", "Show in folder"};
    EXPECT_EQ(items(cg::ChartState::Complete), update);
    EXPECT_EQ(items(cg::ChartState::Missing), update);
    EXPECT_EQ(items(cg::ChartState::Failed), update);
    EXPECT_EQ(items(cg::ChartState::Interrupted), (Texts{"Delete", "Show in folder"}));
    EXPECT_EQ(items(cg::ChartState::Running), (Texts{"Show in folder"}));
}

TEST_F(MyChartsWeatherTest, UpdateToLatestWeatherRunsWithoutReplaceAsksOverBudgetAndIsOffWhileBusy) {
    auto w = weather("20261016-050000", cg::ChartState::Complete, "2026-10-15T20:10:00Z", "c");
    w.updateBefore = "2026-02-28T07:08:09"; // an old Update... date must not reach the argv
    w.bytes = 5'000'000;
    store_->saveChart(w);
    makeDialog();
    int budgetAsks = 0;
    bool budgetAnswer = false;
    panel()->confirmBudget = [&](const wxString&, std::function<void(bool)> answer) {
        ++budgetAsks;
        answer(budgetAnswer);
    };
    cg::Settings s = store_->loadSettings();
    s.slowConnection = true;
    s.dataBudgetMb = 1;
    store_->saveSettings(s);
    panel()->act(MyChartsPanel::Action::UpdateWeather, w.id); // No
    EXPECT_EQ(budgetAsks, 1);
    EXPECT_EQ(runs(w.id), 0);

    s.slowConnection = false;
    store_->saveSettings(s);
    scenario(w.id, 1, "", 143, "wait");
    panel()->act(MyChartsPanel::Action::UpdateWeather, w.id);
    EXPECT_EQ(budgetAsks, 1);
    ASSERT_TRUE(jobs_->busy());
    ASSERT_TRUE(runUntil([&] { return !argsOf(w.id, 1).empty(); }));
    EXPECT_EQ(weatherItems(w), (Texts{"Update to latest weather (off)", "Delete (off)", "Show in folder"}));
    jobs_->stop();
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(argsOf(w.id, 1), runArgs(w.id, "run")); // ends with --overwrite: no --replace, no date
}

struct BarsTest : ChartsTest {
    // The row panels of the list, in order.
    std::vector<wxPanel*> rowPanels() {
        wxTheApp->ProcessIdle();
        std::vector<wxScrolledWindow*> lists;
        collect(panel(), lists);
        std::vector<wxPanel*> out;
        for (wxWindow* child : lists.front()->GetChildren())
            if (child->IsShown() && dynamic_cast<wxPanel*>(child))
                out.push_back(static_cast<wxPanel*>(child));
        return out;
    }
    wxGauge* gaugeOf(wxPanel* row) {
        std::vector<wxGauge*> g;
        collect(row, g);
        EXPECT_LE(g.size(), 1u);
        return g.empty() ? nullptr : g.front();
    }
    FrozenBar* barOf(wxPanel* row) {
        std::vector<FrozenBar*> b;
        collect(row, b);
        EXPECT_LE(b.size(), 1u);
        return b.empty() ? nullptr : b.front();
    }
    // The bar painted into a bitmap: the pixels of its middle line.
    static std::vector<wxColour> pixels(const FrozenBar* bar) {
        const wxSize s = bar->GetClientSize();
        wxBitmap bmp(s.x, s.y, 24);
        wxMemoryDC dc(bmp);
        dc.SetBackground(*wxWHITE_BRUSH);
        dc.Clear();
        bar->draw(dc);
        dc.SelectObject(wxNullBitmap);
        const wxImage img = bmp.ConvertToImage();
        std::vector<wxColour> line;
        for (int x = 0; x < s.x; ++x)
            line.emplace_back(img.GetRed(x, s.y / 2), img.GetGreen(x, s.y / 2), img.GetBlue(x, s.y / 2));
        return line;
    }
    // The x where the fill ends: the first pixel (after the border) that is not the fill's colour.
    static int fillEnd(const std::vector<wxColour>& line) {
        int x = 1;
        while (x < static_cast<int>(line.size()) - 1 && line[x] == line[1])
            ++x;
        return x;
    }
    // A feed line for the stand-in's run 1 (renamed into place so the script never reads half a file).
    void feed(const std::string& id, const std::string& text) {
        spit(jobDir(id) / "feed.tmp", text);
        fs::rename(jobDir(id) / "feed.tmp", jobDir(id) / "feed.1");
    }
    wxStaticText* topLine() {
        std::vector<wxStaticText*> t;
        collect(panel(), t);
        for (wxStaticText* x : t)
            if (u(x->GetLabel()).rfind("Chart Grabber downloads one chart", 0) == 0)
                return x;
        return nullptr;
    }
    // Starts a Resume of an Interrupted chart whose stand-in prints `err` and waits (`feed`: and takes more).
    cg::ChartRecord startRunning(const std::string& id, int done, const std::string& err,
                                 const std::string& mode = "wait") {
        auto r = save(id, "Runner", cg::ChartState::Interrupted);
        r.done = done;
        store_->saveChart(r);
        scenario(id, 1, "", 0, mode, err);
        jobs_->start(r, cg::RunKind::Download, true);
        return r;
    }
};

// Proof 5: the frozen rows. Interrupted 630 -> a grey bar at 630, "Stopped at 63 %", Resume;
// Interrupted 0 -> no gauge, "Stopped"; Failed 280 -> grey bar, "Failed at 28 %", no primary; Missing
// -> grey bar at (1000 - 250) * 1000 / 1000 = 750 and the missing words; Complete -> no gauge, the
// state text stays right of the details (two-line row). The words appear once per row.
TEST_F(BarsTest, FrozenRowsShowAGreyBarAndTheirWords) {
    auto a = save("20261016-050000", "Stopped63", cg::ChartState::Interrupted);
    a.done = 630;
    store_->saveChart(a);
    save("20261016-040000", "Stopped0", cg::ChartState::Interrupted);
    auto c = save("20261016-030000", "Failed28", cg::ChartState::Failed);
    c.done = 280;
    store_->saveChart(c);
    auto d = save("20261016-020000", "Gaps", cg::ChartState::Missing);
    d.tiles = 1000;
    d.missing = 250;
    store_->saveChart(d);
    auto e = save("20261016-010000", "Whole", cg::ChartState::Complete);
    e.done = 1000;
    store_->saveChart(e);
    makeDialog();
    dialog_->showMyCharts();
    const std::vector<Row> r = rows();
    const std::vector<wxPanel*> p = rowPanels();
    ASSERT_EQ(r.size(), 5u);
    ASSERT_EQ(p.size(), 5u);
    EXPECT_EQ(r[0].texts, (Texts{"Stopped63", "z8-9", "0.0 MB", "2026-10-15", "Stopped at 63 %"}));
    EXPECT_EQ(r[0].buttons, (Texts{"Resume", "More..."}));
    ASSERT_NE(barOf(p[0]), nullptr);
    EXPECT_EQ(barOf(p[0])->permille(), 630);
    EXPECT_EQ(gaugeOf(p[0]), nullptr); // no wxGauge: a disabled one draws an empty track on GTK
    EXPECT_FALSE(barOf(p[0])->AcceptsFocus());
    EXPECT_NEAR(fillEnd(pixels(barOf(p[0]))), 180 * 630 / 1000.0, 1.0);
    EXPECT_EQ(r[1].texts.back(), "Stopped");
    EXPECT_EQ(gaugeOf(p[1]), nullptr);
    EXPECT_EQ(barOf(p[1]), nullptr);
    EXPECT_EQ(r[2].texts, (Texts{"Failed28", "z8-9", "0.0 MB", "2026-10-15", "Failed at 28 %"}));
    EXPECT_EQ(r[2].buttons, (Texts{"More..."}));
    ASSERT_NE(barOf(p[2]), nullptr);
    EXPECT_EQ(barOf(p[2])->permille(), 280);
    EXPECT_EQ(gaugeOf(p[2]), nullptr); // no wxGauge: a disabled one draws an empty track on GTK
    EXPECT_FALSE(barOf(p[2])->AcceptsFocus());
    EXPECT_NEAR(fillEnd(pixels(barOf(p[2]))), 180 * 280 / 1000.0, 1.0);
    EXPECT_EQ(r[3].texts.back(), "250 of 1 000 tiles missing");
    EXPECT_EQ(r[3].buttons, (Texts{"Show", "More..."}));
    ASSERT_NE(barOf(p[3]), nullptr);
    EXPECT_EQ(barOf(p[3])->permille(), 750);
    EXPECT_EQ(gaugeOf(p[3]), nullptr); // no wxGauge: a disabled one draws an empty track on GTK
    EXPECT_FALSE(barOf(p[3])->AcceptsFocus());
    EXPECT_NEAR(fillEnd(pixels(barOf(p[3]))), 180 * 750 / 1000.0, 1.0);
    EXPECT_EQ(r[4].texts, (Texts{"Whole", "z8-9", "0.0 MB", "2026-10-15", "Complete"}));
    EXPECT_EQ(gaugeOf(p[4]), nullptr);
    EXPECT_EQ(barOf(p[4]), nullptr);
    // the Complete row keeps its two lines: lower than an unfinished row, which has a third line
    EXPECT_LT(p[4]->GetSize().y, p[1]->GetSize().y);
}

// Proofs 6: while a job runs the running row has an enabled gauge, the live words and Stop; a press stops the
// job (Stopping), the button reads "Stopping..." disabled before the CLI exits; after the end the row is
// rebuilt with Resume and the frozen bar. The stored Done (640) is where the Resume bar starts.
TEST_F(BarsTest, TheLiveRowStopsFromItsButtonAndFreezesAtTheEnd) {
    makeDialog();
    startRunning("20261016-060000", 640,
                 progressLine(10)); // 10 % of one source = mean over {base} = 100, < 640
    ASSERT_TRUE(jobs_->busy());
    ASSERT_TRUE(runUntil([&] { return jobs_->live()->stage == cg::LiveStage::Downloading; }));
    panel()->updateLiveRow();
    std::vector<Row> r = rows();
    std::vector<wxPanel*> p = rowPanels();
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].buttons, (Texts{"Stop", "More..."}));
    EXPECT_TRUE(r[0].enabled[0]);
    ASSERT_NE(gaugeOf(p[0]), nullptr);
    EXPECT_TRUE(gaugeOf(p[0])->IsEnabled());
    EXPECT_EQ(gaugeOf(p[0])->GetValue(), 640);
    EXPECT_EQ(r[0].texts.back(), "64 %  about 3 min left"); // elapsed 20 * 90 / 10 = 180 s
    click(r[0].widgets[0]);
    EXPECT_EQ(jobs_->live()->stage, cg::LiveStage::Stopping);
    r = rows();
    EXPECT_EQ(r[0].buttons, (Texts{"Stopping...", "More..."}));
    EXPECT_FALSE(r[0].enabled[0]);
    EXPECT_EQ(r[0].texts.back(), "Stopping at 64 %");
    ASSERT_TRUE(waitIdle(10000));
    r = rows();
    p = rowPanels();
    EXPECT_EQ(r[0].buttons, (Texts{"Resume", "More..."}));
    EXPECT_TRUE(r[0].enabled[0]);
    EXPECT_EQ(r[0].texts.back(), "Stopped at 64 %");
    EXPECT_EQ(gaugeOf(p[0]), nullptr); // rebuilt as the grey bar
    ASSERT_NE(barOf(p[0]), nullptr);
    EXPECT_EQ(barOf(p[0])->permille(), 640);
}

// Proof 6: a Stop for a chart that is not the live job does nothing.
TEST_F(BarsTest, StopOfAnotherChartDoesNothing) {
    save("20261016-060100", "Other", cg::ChartState::Interrupted);
    makeDialog();
    startRunning("20261016-060200", 0, progressLine(10));
    panel()->act(MyChartsPanel::Action::Stop, "20261016-060100");
    EXPECT_NE(jobs_->live()->stage, cg::LiveStage::Stopping);
    panel()->act(MyChartsPanel::Action::Stop, "20261016-060200");
    EXPECT_EQ(jobs_->live()->stage, cg::LiveStage::Stopping);
    ASSERT_TRUE(waitIdle(10000));
}

bool enabledItem(wxMenu& m, MyChartsPanel::Action a, bool* found) {
    wxMenuItem* item = m.FindItem(1 + static_cast<int>(a));
    *found = item != nullptr;
    return item && item->IsEnabled();
}

// Proof 6: the menu of the live row has Show and Show in folder, both enabled; on the other rows Show in
// folder is enabled and every other item is greyed while busy; when idle all are enabled.
TEST_F(BarsTest, TheMenusWhileBusy) {
    save("20261016-070000", "Whole", cg::ChartState::Complete);
    makeDialog();
    using A = MyChartsPanel::Action;
    const auto menuOf = [&](const std::string& id, std::vector<std::pair<A, bool>>& out, size_t& count) {
        wxMenu m;
        for (const cg::ChartRecord& c : store_->loadCharts())
            if (c.id == id)
                panel()->fillMenu(m, c);
        count = m.GetMenuItemCount();
        for (A a : {A::Show, A::TopUp, A::Update, A::BuildFromSaved, A::Delete, A::ShowFile}) {
            bool found = false;
            const bool on = enabledItem(m, a, &found);
            if (found)
                out.emplace_back(a, on);
        }
    };
    std::vector<std::pair<A, bool>> idle;
    size_t n = 0;
    menuOf("20261016-070000", idle, n);
    EXPECT_EQ(n, 5u);
    for (const auto& [a, on] : idle)
        EXPECT_TRUE(on);

    startRunning("20261016-070100", 0, progressLine(10));
    std::vector<std::pair<A, bool>> live, other;
    menuOf("20261016-070100", live, n);
    EXPECT_EQ(n, 2u);
    ASSERT_EQ(live.size(), 2u);
    EXPECT_EQ(live[0], (std::pair<A, bool>{A::Show, true}));
    EXPECT_EQ(live[1], (std::pair<A, bool>{A::ShowFile, true}));
    menuOf("20261016-070000", other, n);
    ASSERT_EQ(other.size(), 5u);
    for (const auto& [a, on] : other)
        EXPECT_EQ(on, a == A::ShowFile);
    jobs_->stop();
    ASSERT_TRUE(waitIdle(10000));
}

// Proof 7: ten timer ticks with changing progress leave the same row windows; the gauge value and the words
// change; nothing is updated while the page is hidden.
TEST_F(BarsTest, TimerTicksUpdateTheLiveRowWithoutRebuildingAnyRow) {
    save("20261016-080000", "Whole", cg::ChartState::Complete);
    makeDialog();
    dialog_->showMyCharts();
    startRunning("20261016-080100", 0, progressLine(10), "feed");
    ASSERT_TRUE(runUntil([&] { return jobs_->live()->donePermille == 100; }));
    const std::vector<wxPanel*> before = rowPanels();
    ASSERT_EQ(before.size(), 2u);
    ASSERT_TRUE(panel()->IsShownOnScreen());
    wxPanel* liveRow = before[0];
    auto wordsWidth = [&] { // the live words: the last static text of the row (as rows.texts.back)
        std::vector<wxStaticText*> t;
        collect(liveRow, t);
        return t.empty() ? -1 : t.back()->GetSize().x;
    };
    const int wordsWidth0 = wordsWidth();
    EXPECT_GT(wordsWidth0, 0);
    for (int step = 2; step <= 9; ++step) {
        feed("20261016-080100", progressLine(10 * step));
        ASSERT_TRUE(runUntil([&] { return jobs_->live()->donePermille == 100 * step; }));
        panel()->onLiveTick();
        EXPECT_EQ(gaugeOf(liveRow)->GetValue(), 100 * step);
        EXPECT_EQ(rows()[0].texts.back().rfind(std::to_string(10 * step) + " %  ", 0), 0u) << step;
        EXPECT_EQ(rowPanels(), before);
        EXPECT_EQ(wordsWidth(), wordsWidth0) << step; // wxST_NO_AUTORESIZE: the sizer owns the width
    }
    dialog_->Hide();
    feed("20261016-080100", progressLine(100));
    ASSERT_TRUE(runUntil([&] { return jobs_->live()->donePermille == 1000; }));
    panel()->onLiveTick();
    EXPECT_EQ(gaugeOf(liveRow)->GetValue(), 900); // hidden: not updated
    dialog_->Show();
    panel()->onLiveTick();
    EXPECT_EQ(gaugeOf(liveRow)->GetValue(), 1000);
    EXPECT_EQ(rowPanels(), before);
    jobs_->stop();
    ASSERT_TRUE(waitIdle(10000));
}

// Proof 7: the panel's own 1 s timer runs while a job runs and moves the live row, with no call from outside.
TEST_F(BarsTest, TheRealTimerMovesTheLiveRowWhileTheJobRuns) {
    makeDialog();
    dialog_->showMyCharts();
    startRunning("20261016-085000", 0, progressLine(10), "feed");
    ASSERT_TRUE(runUntil([&] { return jobs_->live()->donePermille == 100; }));
    const std::vector<wxPanel*> before = rowPanels();
    ASSERT_EQ(before.size(), 1u);
    ASSERT_TRUE(runUntil([&] { return gaugeOf(before[0])->GetValue() == 100; }, 3000));
    feed("20261016-085000", progressLine(40));
    ASSERT_TRUE(runUntil([&] { return gaugeOf(before[0])->GetValue() == 400; }, 4000));
    EXPECT_EQ(rowPanels(), before);
    jobs_->stop();
    ASSERT_TRUE(waitIdle(10000));
}

// Proof 8: the top line is shown while busy and hidden when idle.
TEST_F(BarsTest, TheTopLineFollowsBusy) {
    makeDialog();
    ASSERT_NE(topLine(), nullptr);
    EXPECT_EQ(u(topLine()->GetLabel()),
              "Chart Grabber downloads one chart at a time. The other actions return when it ends.");
    EXPECT_FALSE(topLine()->IsShown());
    startRunning("20261016-090000", 0, progressLine(10));
    EXPECT_TRUE(topLine()->IsShown());
    jobs_->stop();
    ASSERT_TRUE(waitIdle(10000));
    EXPECT_FALSE(topLine()->IsShown());
}

// Gauges follow the strip's rule: after every look.apply the LIVE gauge carries the control colour (Dusk:
// DILG1; the fake DimeWindow paints (1,2,3) first).
TEST_F(BarsTest, GaugesCarryTheControlColourAtDusk) {
    const wxColour dilg1(110, 111, 112);
    ocpn_fake::globalColors["DILG1"] = dilg1;
    makeDialog();
    startRunning("20261016-100000", 400, progressLine(10));
    panel()->refresh();
    look_.setScheme(PI_GLOBAL_COLOR_SCHEME_DUSK);
    // the colour is set after the dialog applied the look: wait for it
    ASSERT_TRUE(runUntil(
        [&] {
            const auto rows = rowPanels();
            return rows.size() == 1 && gaugeOf(rows[0])->GetBackgroundColour() == dilg1;
        },
        15000))
        << "the gauge never took the Dusk control colour";
    std::vector<wxPanel*> p = rowPanels();
    ASSERT_EQ(p.size(), 1u);
    EXPECT_EQ(gaugeOf(p[0])->GetBackgroundColour(), dilg1);
    panel()->refresh();
    p = rowPanels();
    EXPECT_EQ(gaugeOf(p[0])->GetBackgroundColour(), dilg1);
    look_.setScheme(PI_GLOBAL_COLOR_SCHEME_DAY);
    ASSERT_TRUE(runUntil([&] { return gaugeOf(rowPanels()[0])->GetBackgroundColour() != dilg1; }, 15000))
        << "the gauge kept the Dusk colour after the switch to Day";
    EXPECT_NE(gaugeOf(rowPanels()[0])->GetBackgroundColour(), dilg1);
    ocpn_fake::globalColors.clear();
    jobs_->stop();
    ASSERT_TRUE(waitIdle(10000));
}

// in Day, Dusk and Night the grey fill differs from the track and from the row body, has no
// hue (the live fill is blue), and a scheme switch repaints the bar at once (no row rebuild).
TEST_F(BarsTest, FrozenBarFillIsToldFromTrackAndBodyInEveryScheme) {
    auto a = save("20261016-100000", "Frozen", cg::ChartState::Interrupted);
    a.done = 400;
    store_->saveChart(a);
    makeDialog();
    dialog_->showMyCharts();
    FrozenBar* bar = barOf(rowPanels()[0]);
    ASSERT_NE(bar, nullptr);
    int paints = 0;
    bar->Bind(wxEVT_PAINT, [&](wxPaintEvent& e) {
        ++paints;
        e.Skip();
    });
    struct Scheme {
        PI_ColorScheme cs;
        wxColour dilg1, body;
    };
    for (const Scheme& s :
         {Scheme{PI_GLOBAL_COLOR_SCHEME_DAY, wxColour(), wxColour(237, 237, 237)},
          Scheme{PI_GLOBAL_COLOR_SCHEME_DUSK, wxColour(60, 60, 60), wxColour(40, 40, 40)},
          Scheme{PI_GLOBAL_COLOR_SCHEME_NIGHT, wxColour(30, 20, 20), wxColour(15, 10, 10)},
          // mid-grey track and body: the fill must still stand out
          Scheme{PI_GLOBAL_COLOR_SCHEME_DUSK, wxColour(120, 120, 120), wxColour(130, 130, 130)}}) {
        if (s.dilg1.IsOk())
            ocpn_fake::globalColors["DILG1"] = s.dilg1;
        else
            ocpn_fake::globalColors.clear();
        bar->GetParent()->SetBackgroundColour(s.body);
        paints = 0;
        look_.setScheme(s.cs);
        ASSERT_TRUE(runUntil(
            [&] {
                bar->Update();
                return paints > 0;
            },
            15000))
            << "the bar was not repainted by the switch " << s.cs;
        EXPECT_GT(paints, 0) << s.cs; // repainted by the switch, the row not rebuilt
        EXPECT_EQ(barOf(rowPanels()[0]), bar) << s.cs;
        const std::vector<wxColour> line = pixels(bar);
        const wxColour fill = line[2], track = line[line.size() - 3];
        EXPECT_EQ(fill.Red(), fill.Green()) << s.cs; // grey: no hue, not the live blue
        EXPECT_EQ(fill.Green(), fill.Blue()) << s.cs;
        EXPECT_NE(fill, track) << s.cs;
        EXPECT_NE(fill, s.body) << s.cs;
        if (s.dilg1.IsOk()) {
            EXPECT_EQ(track, s.dilg1) << s.cs;
        }
        EXPECT_GE(std::abs(fill.Red() - track.Red()), 40) << s.cs;
        EXPECT_GE(std::abs(fill.Red() - s.body.Red()), 40) << s.cs;
    }
    ocpn_fake::globalColors.clear();
}

// Proof 9 (touch): every row button, Stop and Stopping... included, is at least minControlHeight high; the
// dialog keeps its size when unfinished rows (with their third line) are listed.
TEST_F(BarsTest, TouchButtonsAreTallEnoughAndTheDialogKeepsItsSize) {
    ocpn_fake::touch = true;
    ocpn_fake::scale = 1.0;
    save("20261016-110400", "Whole", cg::ChartState::Complete);
    makeDialog();
    dialog_->showMyCharts();
    const int minHeight = look_.minControlHeight();
    ASSERT_GT(minHeight, 0);
    const wxSize size = dialog_->GetSize();
    for (int i = 0; i < 4; ++i) {
        std::string id = "20261016-11010", name = "Half";
        id += std::to_string(i);
        name += std::to_string(i);
        auto r = save(id, name, i % 2 ? cg::ChartState::Failed : cg::ChartState::Interrupted);
        r.done = 500;
        store_->saveChart(r);
    }
    panel()->refresh();
    startRunning("20261016-110500", 300, progressLine(10));
    const auto heights = [&](const char* when) {
        for (const Row& row : rows())
            for (wxButton* b : row.widgets)
                EXPECT_GE(b->GetSize().y, minHeight) << when << " " << u(b->GetLabel());
    };
    const auto widths = [&](const char* when) { // touch-sized both ways, Stop / Stopping / Resume too
        for (const Row& row : rows())
            for (wxButton* b : row.widgets)
                EXPECT_GE(b->GetSize().x, minHeight) << when << " " << u(b->GetLabel());
    };
    heights("running");
    widths("running");
    EXPECT_EQ(dialog_->GetSize(), size);
    EXPECT_LE(dialog_->GetSize().x, 800) << "touch mode, the live progress line";
    EXPECT_LE(dialog_->GetSize().y, 480);
    panel()->act(MyChartsPanel::Action::Stop, "20261016-110500");
    bool stopping = false;
    for (const Row& row : rows())
        stopping = stopping || (!row.buttons.empty() && row.buttons[0] == "Stopping...");
    EXPECT_TRUE(stopping);
    heights("stopping");
    widths("stopping");
    EXPECT_EQ(dialog_->GetSize(), size);
    ASSERT_TRUE(waitIdle(10000));
    heights("stopped");
    widths("stopped");
}

// outside touch mode the live row's Stop keeps its natural width (no minimum).
TEST_F(BarsTest, StopKeepsItsNaturalWidthOutsideTouchMode) {
    makeDialog();
    dialog_->showMyCharts();
    startRunning("20261016-110500", 300, progressLine(10));
    int width = 0;
    for (const Row& row : rows())
        for (wxButton* b : row.widgets)
            if (b->GetLabel() == "Stop")
                width = b->GetSize().x;
    EXPECT_GT(width, 0);
    EXPECT_LT(width, 48);
}

// Diagnostic (diag-delete-hang): Delete -> Yes through act and the Question's own button, then the idle
// cycles (the old rows and the FrozenBar are destroyed there), with a watchdog that aborts a hang.
TEST_F(BarsTest, DeleteYesOnEveryKindOfRowReturnsAndTheLoopGoesOn) {
    struct Dog {
        Dog() { alarm(40); }
        ~Dog() { alarm(0); }
    } dog;
    const fs::path file = root_ / "my charts" / "c.mbtiles";
    spit(file, "c");
    auto complete = save("20261015-090000", "Complete", cg::ChartState::Complete);
    complete.files = {file.string()};
    store_->saveChart(complete);
    auto frozen = save("20261015-080000", "Frozen", cg::ChartState::Interrupted);
    frozen.done = 630;
    store_->saveChart(frozen);
    makeDialog();
    ASSERT_NE(barOf(rowPanels().back()), nullptr) << "a FrozenBar is on the frozen row";
    for (const std::string& id : {frozen.id, complete.id}) {
        SCOPED_TRACE(id);
        panel()->act(MyChartsPanel::Action::Delete, id);
        ASSERT_EQ(qprobe::shown().size(), 1u);
        qprobe::press(qprobe::shown()[0], wxID_YES);
        for (int i = 0; i < 20; ++i) {
            wxTheApp->ProcessPendingEvents();
            wxTheApp->ProcessIdle();
            wxMilliSleep(5);
        }
        EXPECT_TRUE(panel()->IsEnabled());
        EXPECT_TRUE(qprobe::shown().empty());
    }
    EXPECT_EQ(rowPanels().size(), 0u);
    EXPECT_EQ(ocpn_fake::updates.size(), 2u) << "one rescan per confirmed delete";
    startRunning("20261016-120000", 300, progressLine(10)); // a running job: Delete does nothing
    panel()->act(MyChartsPanel::Action::Delete, "20261016-120000");
    for (Question* q : qprobe::shown())
        qprobe::press(q, wxID_YES);
    panel()->act(MyChartsPanel::Action::Stop, "20261016-120000");
    ASSERT_TRUE(waitIdle(10000));
}

} // namespace
