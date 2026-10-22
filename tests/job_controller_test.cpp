// SPDX-License-Identifier: GPL-3.0-or-later
// JobController and showOnChart, against a stand-in
// CLI. The stand-in is a small sh script written into a temp directory (never the real CLI): it saves its
// arguments, prints the stdout / stderr text the test put next to the job file, then exits with the given
// code or waits for a signal. The records it prints are the CLI records of cli-requirements.md 2 and 3; the
// error texts are the core's
// download_runner.cpp:419). The real CLI is not used here: the proofs that need it are manual.
// A window test: it lives in cg_gui_tests and skips (and says so) without a display.
//

// the stand-in answers `--version` from the file `version` next to it ("chart-grabber-cli 2026.10.0
// (test)" when absent, the tsv level) and counts the probes in `probes`; it records its LC_ALL per run.
//

// The 1 s timer of the controller is stopped in the tests that count ticks and onTick is called by hand, so
// "60 s" and "30 s" cost no time; one test lets the real timer run and checks that it ticks.
#include "area_tool.h"
#include "cli_runner.h"
#include "config_store.h"
#include "job_files.h"
#include "look.h"
#include "messages.h"
#include "ocpn_fakes.h"
#include "progress_strip.h"
#include "settings.h"
#include "sources_stub.h"
#include "wx_env.h"

#include <gtest/gtest.h>

#include <wx/app.h>
#include <wx/apptrait.h>
#include <wx/button.h>
#include <wx/evtloop.h>
#include <wx/fileconf.h>
#include <wx/frame.h>
#include <wx/gauge.h>
#include <wx/log.h>
#include <wx/panel.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <vector>

// Test-only access to the controller's tick (the 1 s timer handler) and timer. The standard and wx headers
// are already in, so the macro touches only job_controller.h.
#define private public
#include "job_controller.h"
#undef private

#include <ocpn_plugin.h>

namespace {
// Collects the wxLog lines of one scope and restores the previous target.
class LogCollector : public wxLog {
public:
    std::vector<std::string> messages;
    LogCollector() : previous_(wxLog::SetActiveTarget(this)) {}
    ~LogCollector() override { wxLog::SetActiveTarget(previous_); }

protected:
    void DoLogRecord(wxLogLevel, const wxString& msg, const wxLogRecordInfo&) override {
        messages.push_back(msg.utf8_string());
    }

private:
    wxLog* previous_;
};

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

const std::string kBase = "{11111111-1111-1111-1111-111111111111}";
const std::string kOver = "{22222222-2222-2222-2222-222222222222}";
const std::string kNone = "{33333333-3333-3333-3333-333333333333}";
const std::string kName = "\xD0\x9A\xD0\xB0\xD1\x80\xD1\x82\xD0\xB0 d'x"; // "Карта d'x"
const std::string kSource =
    "\xD0\xA1\xD0\xBF\xD1\x83\xD1\x82\xD0\xBD\xD0\xB8\xD0\xBA Google"; // "Спутник Google"

const std::string kScript =
    std::string("#!/bin/sh\n") + stub::kStopPrelude +
    R"SH(if [ "$1" = --version ]; then # probeCli: counted, the answer is in the file `version` next to the script
  d0=$(dirname "$0")
  echo probe >> "$d0/probes"
  cat "$d0/version" 2>/dev/null || echo 'chart-grabber-cli 2026.10.8 (test)'
  exit 0
fi
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
printf '%s\n' "${LC_ALL-unset}" > "$dir/lcall.$n"
mode=$(cat "$dir/mode.$n" 2>/dev/null)
case $mode in
  wait) trap 'exit 143' TERM ;;
  slowterm) trap 'sleep 0.4; exit 143' TERM ;;
  termzero) trap 'sleep 0.3; exit 0' TERM ;;
  ignore) trap '' TERM ;;
esac
if [ -f "$dir/out.$n" ]; then cat "$dir/out.$n"; fi
if [ -f "$dir/err.$n" ]; then cat "$dir/err.$n" >&2; fi
case $mode in
  wait|slowterm|termzero|ignore) k=0; while [ $k -lt 1200 ]; do sleep 0.05; k=$((k + 1)); done ;;
  selfkill) kill -9 $$ ;;
esac
exit $(cat "$dir/code.$n" 2>/dev/null || echo 0)
)SH";

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

std::string progressDownload(const std::string& guid, int zoom, int processed, int total, std::int64_t bytes,
                             int elapsed) {
    return tsv({"progress", "download", guid, std::to_string(zoom), std::to_string(processed),
                std::to_string(total), "0", std::to_string(bytes), std::to_string(elapsed)});
}

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

template <class T> void collect(wxWindow* w, std::vector<T*>& out) {
    if (auto* t = dynamic_cast<T*>(w))
        out.push_back(t);
    for (wxWindow* c : w->GetChildren())
        collect(c, out);
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

class JobTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!wxTheApp || !wxTheApp->IsGUI())
            GTEST_SKIP() << "no display: the job controller test was NOT run";
        loop_.reset(wxTheApp->GetTraits()->CreateEventLoop());
        ASSERT_NE(loop_, nullptr);
        activator_ = std::make_unique<wxEventLoopActivator>(loop_.get());
        char tmpl[] = "/tmp/cgjob-XXXXXX";
        ASSERT_NE(mkdtemp(tmpl), nullptr);
        root_ = tmpl;
        ocpn_fake::privateData = wxString::FromUTF8((root_ / "private").string());
        ocpn_fake::touch = false;
        ocpn_fake::scale = 1.0;
        ocpn_fake::contentScale = 1.0;
        ocpn_fake::jump = {};
        ocpn_fake::updates.clear();
        ocpn_fake::chartDirs.Clear();
        oldHome_ = getenv("HOME") ? getenv("HOME") : "";
        setenv("HOME", (root_ / "home").c_str(), 1);
        cli_ = (root_ / kName / "fake-cli").string(); // a name with a space, a quote and Cyrillic
        spit(cli_, kScript);
        chmod(cli_.c_str(), 0755);
        config_ = std::make_unique<wxFileConfig>(wxString("cgjobtest"), wxEmptyString,
                                                 wxString::FromUTF8((root_ / "opencpn.conf").string()),
                                                 wxEmptyString, wxCONFIG_USE_LOCAL_FILE);
        store_ = std::make_unique<ConfigStore>(config_.get());
        // the strip takes at most 60 % of the canvas width : wide enough for the longest line
        canvas_ = new wxFrame(nullptr, wxID_ANY, "canvas", wxPoint(-4000, -4000), wxSize(4000, 600));
        store_->setStripHintShown(); // the drag line at the top of Details has its own test
        ocpn_fake::canvasWindow = canvas_;
        area_ = std::make_unique<AreaTool>([] { return wxColour(); }, [] { return false; });
        setSettings([](cg::Settings&) {});
    }
    void TearDown() override {
        if (!wxTheApp || !wxTheApp->IsGUI())
            return;
        jc_.reset();
        ocpn_fake::canvasWindow = nullptr;
        if (canvas_) {
            canvas_->Close();
            delete canvas_;
        }
        area_.reset();
        store_.reset();
        config_.reset();
        activator_.reset();
        loop_.reset();
        setenv("HOME", oldHome_.c_str(), 1);
        std::error_code ec;
        expectNoProcessesUnder(root_.string());
        fs::remove_all(root_, ec);
    }

    // ---- loop
    bool runUntil(const std::function<bool()>& done, int timeoutMs = 15000) {
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        while (!done()) {
            if (Clock::now() >= deadline)
                return false;
            loop_->DispatchTimeout(20);
        }
        return true;
    }
    void runFor(int ms) {
        const auto until = Clock::now() + std::chrono::milliseconds(ms);
        while (Clock::now() < until)
            loop_->DispatchTimeout(20);
    }

    // ---- settings, records, job files
    // The folders of the i-th UpdateChartDBInplace call as UTF-8.
    static std::vector<std::string> dirs(std::size_t i) {
        std::vector<std::string> v;
        for (const wxString& d : ocpn_fake::updates.at(i).dirs)
            v.push_back(d.utf8_string());
        return v;
    }
    void setSettings(const std::function<void(cg::Settings&)>& edit) {
        cg::Settings s = store_->loadSettings();
        s.cliPath = cli_;
        s.stripOnChart = true; // the landed tests are about the chart host; the window host sets it false
        edit(s);
        store_->saveSettings(s);
    }
    cg::ChartRecord makeRecord(const std::string& id, bool withJobIni = true) {
        cg::ChartRecord r;
        r.id = id;
        r.name = kName;
        r.sourceGuid = kBase;
        r.overlayGuid = kOver;
        r.sourceName = kSource;
        r.bbox = {-1.5, 50.25, 2.125, 51.0};
        r.zooms = {8, 9};
        r.created = "2026-10-15T10:15:00";
        r.files = {"/planned/a.mbtiles", "/planned/b.mbtiles"};
        r.state = cg::ChartState::Running; // a new chart, as the dialog hands it over
        if (withJobIni) {
            cg::JobSpec spec;
            spec.sourceGuid = kBase;
            spec.overlayGuid = kOver;
            spec.bbox = r.bbox;
            spec.zooms = r.zooms;
            spec.outFile = "/planned/a.mbtiles";
            spec.overlayOutFile = "/planned/b.mbtiles";
            spec.name = kName;
            spit(jobDir(id) / "job.ini", cg::jobIni(spec));
        }
        return r;
    }
    fs::path jobDir(const std::string& id) const {
        return root_ / "private" / "plugins" / "chartgrabber" / "jobs" / id;
    }
    // What the next run of the stand-in does (runs are numbered from 1).
    void scenario(const std::string& id, int run, const std::string& out, const std::string& err, int code,
                  const std::string& mode = "") {
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
        std::istringstream in(slurp(jobDir(id) / (std::string("args.") + std::to_string(run))));
        for (std::string line; std::getline(in, line);)
            out.push_back(line);
        return out;
    }
    // ---- level of the stand-in, arguments of the default maps / cache folders
    void setVersion(const std::string& line, const std::string& folder = kName) {
        spit(root_ / folder / "version", line + "\n");
    }
    // A second stand-in (own folder, own `version`, own `probes`) for the tests that need two programs.
    std::string standIn(const std::string& folder, const std::string& versionLine) {
        const std::string path = (root_ / folder / "fake-cli").string();
        spit(path, kScript);
        chmod(path.c_str(), 0755);
        setVersion(versionLine, folder);
        return path;
    }
    int probes(const std::string& folder = kName) const {
        std::istringstream in(slurp(root_ / folder / "probes"));
        int n = 0;
        for (std::string line; std::getline(in, line);)
            ++n;
        return n;
    }
    std::string lcAll(const std::string& id, int run) const {
        std::string t = slurp(jobDir(id) / (std::string("lcall.") + std::to_string(run)));
        if (!t.empty() && t.back() == '\n')
            t.pop_back();
        return t;
    }
    std::string defaultMaps() const { return (root_ / "home" / ".config" / "Charts" / "Maps").string(); }
    std::string defaultCache() const {
        return (root_ / "private" / "plugins" / "chartgrabber" / "cache").string();
    }
    std::string iniOf(const std::string& id) const { return (jobDir(id) / "job.ini").string(); }
    // displayCommand text of the child: env, nice, the program (quoted), then `args`
    std::string shownCommand(const std::string& args) const {
        std::string quoted;
        for (char c : cli_)
            quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
        return "env LC_ALL=C.UTF-8 nice -n 10 '" + quoted + "' " + args;
    }
    cg::ChartRecord chart(const std::string& id) const {
        for (const cg::ChartRecord& r : store_->loadCharts())
            if (r.id == id)
                return r;
        ADD_FAILURE() << "no chart.ini for " << id;
        return {};
    }
    std::string chartIniText(const std::string& id) const { return slurp(jobDir(id) / "chart.ini"); }

    // ---- controller
    JobController& jc() {
        if (!jc_) {
            jc_ = std::make_unique<JobController>(*store_, look_, *area_);
            jc_->onBusyChanged = [this](bool b) { busyCalls_.push_back(b); };
            jc_->onChartsChanged = [this] { ++changes_; };
            jc_->onPreferences = [this] { ++prefs_; };
        }
        return *jc_;
    }
    void start(const cg::ChartRecord& r, cg::RunKind kind = cg::RunKind::Download, bool overwrite = true) {
        jc().start(r, kind, overwrite);
    }
    bool waitIdle(int timeoutMs = 15000) {
        return runUntil([&] { return !jc().busy(); }, timeoutMs);
    }

    // ---- strip
    ProgressStrip* strip() const {
        std::vector<ProgressStrip*> v; // a child of the canvas
        collect(canvas_, v);
        return v.empty() ? nullptr : v.front();
    }
    std::string stripText() const {
        std::vector<wxStaticText*> v;
        if (strip())
            collect(strip(), v);
        if (v.empty())
            return "<no text>";
        // The strip wraps long text onto several lines: fold each newline (wrap or explicit) to one space.
        std::string text = u(v.front()->GetLabelText());
        for (char& c : text)
            if (c == '\n')
                c = ' ';
        return text;
    }
    bool waitText(const std::string& text, int timeoutMs = 15000) {
        return runUntil([&] { return stripText() == text; }, timeoutMs);
    }
    bool waitTextStarts(const std::string& prefix, int timeoutMs = 15000) {
        return runUntil([&] { return stripText().rfind(prefix, 0) == 0; }, timeoutMs);
    }
    int gaugeValue() const {
        std::vector<wxGauge*> v;
        if (strip())
            collect(strip(), v);
        return v.empty() ? -1 : v.front()->GetValue();
    }
    std::string detailsText() const {
        std::vector<wxTextCtrl*> v;
        if (strip())
            collect(strip(), v);
        return v.empty() ? std::string() : u(v.front()->GetValue());
    }
    // The visible buttons left to right, without Details (Stop included while running).
    std::vector<std::string> buttons() const {
        std::vector<wxButton*> v;
        if (strip())
            collect(strip(), v);
        std::vector<std::string> out;
        for (wxButton* b : v)
            if (b->IsShown() && b->GetLabel() != "Details")
                out.push_back(u(b->GetLabel()));
        return out;
    }
    bool click(const std::string& label, bool pump = true) {
        std::vector<wxButton*> v;
        if (strip())
            collect(strip(), v);
        for (wxButton* b : v) {
            if (!b->IsShown() || u(b->GetLabel()) != label)
                continue;
            wxCommandEvent ev(wxEVT_BUTTON, b->GetId());
            ev.SetEventObject(b);
            b->GetEventHandler()->ProcessEvent(ev);
            wxTheApp->ProcessPendingEvents();
            if (pump)
                runFor(60); // a result button runs its function after the handler returns
            return true;
        }
        return false;
    }
    void tick(int n) {
        wxTimerEvent ev(jc_->tick_);
        for (int i = 0; i < n; ++i)
            jc_->onTick(ev);
    }

    std::unique_ptr<wxEventLoopBase> loop_;
    std::unique_ptr<wxEventLoopActivator> activator_;
    fs::path root_;
    std::string cli_, oldHome_;
    std::unique_ptr<wxFileConfig> config_;
    std::unique_ptr<ConfigStore> store_;
    Look look_;
    std::unique_ptr<AreaTool> area_;
    wxFrame* canvas_ = nullptr;
    std::unique_ptr<JobController> jc_;
    std::vector<bool> busyCalls_;
    int changes_ = 0, prefs_ = 0;
};

// ---------------------------------------------------------------------------------------------- strip lines

// Proof 1: the strip shows the source name, zoom, done/total, MB and time left of every progress record,
// the example included; the gauge follows processed / total.
TEST_F(JobTest, ProgressRecordsUpdateTheStripLine) {
    auto r = makeRecord("20261015-101500");
    scenario(r.id, 1, "",
             tsv({"phase", "download", kBase}) + progressDownload(kBase, 13, 412, 1300, 9400000, 111), 0,
             "wait");
    start(r);
    EXPECT_EQ(busyCalls_, std::vector<bool>{true});
    ASSERT_TRUE(waitText(kSource + ", Coastal: 31 %  9.4 MB  about 4 min left")) << stripText();
    EXPECT_EQ(gaugeValue(), 316); // 412 * 1000 / 1300
    EXPECT_EQ(buttons(), std::vector<std::string>{"Stop"});
    EXPECT_TRUE(strip()->IsShown());
    EXPECT_EQ(chart(r.id).state, cg::ChartState::Running);
}

// Proof 1 (overlay): the seamarks source is named "Seamarks", a GUID of neither source stays a GUID.
TEST_F(JobTest, PhaseAndProgressNameTheSourceByGuid) {
    const std::vector<std::pair<std::string, std::string>> rows = {
        {kBase, kSource}, {kOver, "Seamarks"}, {kNone, kNone}};
    int i = 0;
    for (const auto& [guid, label] : rows) {
        SCOPED_TRACE(label);
        auto r = makeRecord(std::string("20261015-11000") + std::to_string(i++));
        scenario(r.id, 1, "", tsv({"phase", "fill", guid}), 0, "wait");
        start(r);
        ASSERT_TRUE(waitText(label + ": checking the saved tiles...")) << stripText();
        jc().stop();
        ASSERT_TRUE(waitIdle(10000));
    }
    auto r = makeRecord("20261015-110100");
    scenario(r.id, 1, "", progressDownload(kOver, 7, 3, 10, 2000000, 6), 0, "wait");
    start(r);
    ASSERT_TRUE(waitText("Seamarks, zoom 7: 30 %  2.0 MB  about 14 s left")) << stripText(); // 6 * 7 / 3
}

// Proof 1: an export shows the percent and fills the gauge; the overlay file is named "<name> - Seamarks".
TEST_F(JobTest, ExportProgressShowsPercentOfTheNamedChart) {
    auto r = makeRecord("20261015-102000");
    scenario(r.id, 1, "",
             tsv({"phase", "export", kBase}) + tsv({"progress", "export", kBase, "455"}) +
                 tsv({"progress", "export", kOver, "100"}),
             0, "wait");
    start(r);
    ASSERT_TRUE(waitText("Building the chart " + kName + " - Seamarks: 10 %")) << stripText();
    EXPECT_EQ(gaugeValue(), 100);
    jc().stop();
    ASSERT_TRUE(waitIdle(10000));
}

// ------------------------------------------------------------------------------------------- Done, 9.3 / 9.4

std::string written(const std::string& guid, const std::string& path, int tiles, std::int64_t bytes) {
    return tsv({"written", guid, path, std::to_string(tiles), std::to_string(bytes)});
}
std::string fill(const std::string& guid, int zoom, int tiles, int missing) {
    return tsv({"fill", guid, std::to_string(zoom), std::to_string(tiles), std::to_string(tiles - missing),
                "0", std::to_string(missing), "5000"});
}

// Proofs 1 and 7: base + seamarks job. Headline, buttons, both files registered in order (refresh on the last
// only), first-success hint and imagery warning once, chart.ini on disk, busy hook, chart hook.
TEST_F(JobTest, TwoChartsAddedRegisteredAndFirstSuccessHintOnce) {
    canvas_->SetSize(wxSize(4000, 600)); // wide enough for the full headline on the strip's three lines
    auto r = makeRecord("20261015-103000");
    const std::string a = "/charts/" + kName + ".mbtiles", b = "/charts/" + kName + " - Seamarks.mbtiles";
    scenario(r.id, 1,
             fill(kBase, 8, 100, 0) + fill(kBase, 9, 300, 0) + fill(kOver, 8, 50, 0) +
                 written(kBase, a, 400, 321) + written(kOver, b, 50, 679),
             "", 0);
    ASSERT_FALSE(store_->firstSuccessShown());
    start(r);
    ASSERT_TRUE(waitIdle());
    const std::string headline = "2 charts added: " + kName + ", " + kName + " - Seamarks.";
    EXPECT_EQ(stripText(), headline); // the hint and the warning are in Details
    ASSERT_NE(strip(), nullptr);
    std::vector<wxTextCtrl*> texts;
    collect(strip(), texts);
    ASSERT_EQ(texts.size(), 1u);
    EXPECT_TRUE(texts.front()->IsShown()) << "Details opens by itself on the first success";
    EXPECT_EQ(detailsText().rfind("If the chart is not visible, click it in the chart bar at the bottom. "
                                  "MBTiles charts need OpenGL (Options > Display > Advanced).\n"
                                  "Satellite images can be 40-170 m off the chart: background only, not for "
                                  "navigation near dangers.\n\n",
                                  0),
              0u)
        << detailsText();
    EXPECT_EQ(buttons(), (std::vector<std::string>{"Show", "Close"}));
    ASSERT_EQ(ocpn_fake::updates.size(), 1u); // one rescan for both files (same folder)
    EXPECT_EQ(dirs(0), (std::vector<std::string>{"/charts"}));
    EXPECT_FALSE(ocpn_fake::updates[0].force);
    EXPECT_FALSE(ocpn_fake::updates[0].progress);
    EXPECT_EQ(busyCalls_, (std::vector<bool>{true, false}));
    EXPECT_EQ(changes_, 2); // Running saved at start, the result saved at the end
    EXPECT_TRUE(store_->firstSuccessShown());
    const cg::ChartRecord c = chart(r.id);
    EXPECT_EQ(c.state, cg::ChartState::Complete);
    EXPECT_EQ(c.files, (std::vector<std::string>{a, b}));
    EXPECT_EQ(c.bytes, 1000);
    EXPECT_EQ(c.tiles, 450);
    EXPECT_EQ(c.missing, 0);
    EXPECT_NE(chartIniText(r.id).find("\nState=complete\n"), std::string::npos);

    // Second success (a Top up of the same chart): no hint, no warning.
    ocpn_fake::updates.clear();
    scenario(r.id, 2, fill(kBase, 8, 100, 0) + written(kBase, a, 400, 321) + written(kOver, b, 50, 679), "",
             0);
    start(chart(r.id));
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(stripText(), "2 charts added: " + kName + ", " + kName + " - Seamarks.");
    EXPECT_EQ(ocpn_fake::updates.size(), 1u); // registered again, once
    EXPECT_EQ(detailsText().find("If the chart is not visible"), std::string::npos) << detailsText();
    EXPECT_EQ(detailsText().find("Satellite images"), std::string::npos);
}

// with the drag line not yet shown, Details starts with it (then the first-success lines);
// after a drop of a strip (the key StripHintShown) a later job has no drag line.
TEST_F(JobTest, TheDragLineStartsDetailsUntilTheFirstDrop) {
    auto r = makeRecord("20261015-103100");
    scenario(r.id, 1, fill(kBase, 8, 100, 0) + written(kBase, "/charts/a.mbtiles", 400, 321), "", 0);
    config_->DeleteEntry("/PlugIns/ChartGrabber/StripHintShown");
    ASSERT_FALSE(store_->stripHintShown());
    start(r);
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(detailsText().rfind("You can drag this strip anywhere on the chart. Double-click it to put it "
                                  "back.\n\nIf the chart is not visible",
                                  0),
              0u)
        << detailsText();
    store_->setStripHintShown();
    scenario(r.id, 2, fill(kBase, 8, 100, 0) + written(kBase, "/charts/a.mbtiles", 400, 321), "", 0);
    start(chart(r.id));
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(detailsText().find("You can drag"), std::string::npos) << detailsText();
}

// OpenCPN deleting the canvas (and so the strip) before the plugin is torn down crashes
// nothing: the controller's weak reference is null and it is destroyed without touching the strip.
TEST_F(JobTest, TheCanvasDeletedBeforeTheControllerTakesTheStripWithIt) {
    auto r = makeRecord("20261015-103200");
    scenario(r.id, 1, fill(kBase, 8, 100, 0) + written(kBase, "/charts/a.mbtiles", 400, 321), "", 0);
    start(r);
    ASSERT_TRUE(waitIdle());
    wxWeakRef<ProgressStrip> weak(strip());
    ASSERT_TRUE(weak);
    ocpn_fake::canvasWindow = nullptr;
    delete canvas_;
    canvas_ = nullptr;
    EXPECT_FALSE(weak);
    EXPECT_NO_THROW(jc_.reset());
}

// StripOnChart off (the default): the job strip is the child of a floating frame of the canvas's
// top-level window; on: a child of the canvas. The host is read when the strip is created.
TEST_F(JobTest, TheJobStripIsInAFrameByDefaultAndAChildOfTheCanvasWhenTheOptionIsOn) {
    for (const bool onChart : {false, true}) {
        setSettings([&](cg::Settings& s) { s.stripOnChart = onChart; });
        auto r = makeRecord(onChart ? "20261015-103201" : "20261015-103200");
        scenario(r.id, 1, fill(kBase, 8, 100, 0) + written(kBase, "/charts/a.mbtiles", 400, 321), "", 0);
        start(r);
        ASSERT_TRUE(waitIdle());
        ASSERT_NE(strip(), nullptr);
        if (onChart) {
            EXPECT_EQ(strip()->GetParent(), canvas_);
            EXPECT_EQ(ProgressStrip::Host::Canvas, strip()->host());
        } else {
            auto* frame = wxDynamicCast(strip()->GetParent(), wxFrame);
            ASSERT_NE(frame, nullptr);
            EXPECT_EQ(frame->GetParent(), canvas_);
            EXPECT_NE(0, frame->GetWindowStyleFlag() & wxFRAME_FLOAT_ON_PARENT);
            EXPECT_NE(0, frame->GetWindowStyleFlag() & wxFRAME_NO_TASKBAR);
            EXPECT_TRUE(frame->IsShown());
            EXPECT_TRUE(strip()->IsShownOnScreen());
            // The option changed while the strip lives: it keeps its host.
            setSettings([&](cg::Settings& s) { s.stripOnChart = true; });
            EXPECT_EQ(ProgressStrip::Host::Window, strip()->host());
            wxWeakRef<wxFrame> weakFrame(frame);
            jc_.reset(); // ~JobController ends the strip at once: no frame of the plugin is left
            EXPECT_FALSE(weakFrame);
        }
        jc_.reset();
    }
}

// OpenCPN deleting its main window first takes the frame with it.
TEST_F(JobTest, TheWindowHostFrameGoesWithTheMainWindowAndTheControllerSkipsItsDestroy) {
    setSettings([](cg::Settings& s) { s.stripOnChart = false; });
    auto r = makeRecord("20261015-103200");
    scenario(r.id, 1, fill(kBase, 8, 100, 0) + written(kBase, "/charts/a.mbtiles", 400, 321), "", 0);
    start(r);
    ASSERT_TRUE(waitIdle());
    wxWeakRef<ProgressStrip> weak(strip());
    ASSERT_TRUE(weak);
    ocpn_fake::canvasWindow = nullptr;
    delete canvas_;
    canvas_ = nullptr;
    EXPECT_FALSE(weak);
    EXPECT_NO_THROW(jc_.reset());
}

TEST_F(JobTest, NothingIsRegisteredWhenTheSettingIsOff) {
    setSettings([](cg::Settings& s) { s.registerCharts = false; });
    store_->setFirstSuccessShown();
    auto r = makeRecord("20261015-103100");
    scenario(r.id, 1, written(kBase, "/charts/one.mbtiles", 4, 9), "", 0);
    start(r);
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(stripText(), "Chart added: one.");
    EXPECT_TRUE(ocpn_fake::updates.empty());
}

// a job that ends Done without a written file calls nothing.
TEST_F(JobTest, NothingIsRegisteredWhenNoFileWasWritten) {
    store_->setFirstSuccessShown();
    ocpn_fake::chartDirs.Add("/x");
    auto r = makeRecord("20261016-100100");
    scenario(r.id, 1, "", "", 0);
    start(r);
    ASSERT_TRUE(waitIdle());
    EXPECT_TRUE(ocpn_fake::updates.empty());
}

// one UpdateChartDBInplace per job; OpenCPN's folders first (in order), then each folder of
// the written files that is not listed (a trailing '/' ignored), once each, in record order.
TEST_F(JobTest, RegistrationIsOneRescanOfListedFoldersPlusTheJobsFolders) {
    store_->setFirstSuccessShown();
    struct Row {
        const char* name;
        std::vector<std::string> listed;
        std::vector<std::string> files;
        std::vector<std::string> expect;
    };
    const std::vector<Row> rows = {
        {"listed", {"/x", "/charts"}, {"/charts/a.mbtiles"}, {"/x", "/charts"}},
        {"listed with slash", {"/charts/"}, {"/charts/a.mbtiles"}, {"/charts/"}},
        {"listed with slash, spaced name", {"/my charts/"}, {"/my charts/a.mbtiles"}, {"/my charts/"}},
        {"not listed, two files", {"/x"}, {"/charts/a.mbtiles", "/charts/b.mbtiles"}, {"/x", "/charts"}},
        {"two folders", {"/x"}, {"/p/a.mbtiles", "/q/b.mbtiles"}, {"/x", "/p", "/q"}},
        {"odd path",
         {},
         {"/ch a'rts/\xd0\x9c\xd0\xbe\xd1\x80\xd0\xb5/a.mbtiles"},
         {"/ch a'rts/\xd0\x9c\xd0\xbe\xd1\x80\xd0\xb5"}},
    };
    int n = 0;
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        ocpn_fake::updates.clear();
        ocpn_fake::chartDirs.Clear();
        for (const auto& d : row.listed)
            ocpn_fake::chartDirs.Add(wxString::FromUTF8(d));
        auto r = makeRecord(std::string("20261016-10000") + std::to_string(n++));
        std::string out;
        for (const auto& f : row.files)
            out += written(kBase, f, 4, 9);
        scenario(r.id, 1, out, "", 0);
        LogCollector log;
        start(r);
        ASSERT_TRUE(waitIdle());
        ASSERT_EQ(ocpn_fake::updates.size(), 1u);
        EXPECT_EQ(dirs(0), row.expect);
        EXPECT_FALSE(ocpn_fake::updates[0].force);
        EXPECT_FALSE(ocpn_fake::updates[0].progress);
        const std::string line = std::string("ChartGrabber: chart list updated (") +
                                 std::to_string(row.expect.size()) + " folders)";
        EXPECT_NE(std::find(log.messages.begin(), log.messages.end(), line), log.messages.end());
        EXPECT_EQ(stripText().find("did not accept"), std::string::npos);
    }
}

// Proof 2: missing tiles -> the headline with the numbers, chart.ini State missing with Tiles / Missing.
TEST_F(JobTest, MissingTilesGiveTheMissingStateAndTheNumbers) {
    store_->setFirstSuccessShown();
    auto one = makeRecord("20261015-104000");
    one.overlayGuid.clear();
    scenario(one.id, 1,
             fill(kBase, 8, 1000, 3) + fill(kBase, 9, 300, 2) + written(kBase, "/charts/m.mbtiles", 1300, 77),
             "", 0);
    start(one);
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(stripText(), "Chart added, 5 of 1 300 tiles are missing. Top up later to fill them.");
    EXPECT_EQ(buttons(), (std::vector<std::string>{"Show", "Close"}));
    cg::ChartRecord c = chart(one.id);
    EXPECT_EQ(c.state, cg::ChartState::Missing);
    EXPECT_EQ(c.tiles, 1300);
    EXPECT_EQ(c.missing, 5);
    EXPECT_NE(chartIniText(one.id).find("\nState=missing\nTiles=1300\nMissing=5\n"), std::string::npos);

    auto two = makeRecord("20261015-104100"); // base + seamarks: the numbers of both sources add up
    scenario(two.id, 1,
             fill(kBase, 8, 1000, 5) + fill(kOver, 8, 400, 2) + written(kBase, "/charts/n.mbtiles", 1000, 5) +
                 written(kOver, "/charts/n - Seamarks.mbtiles", 400, 6),
             "", 0);
    start(two);
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(stripText(), "2 charts added, 7 of 1 400 tiles are missing. Top up later to fill them.");
    c = chart(two.id);
    EXPECT_EQ(c.tiles, 1400);
    EXPECT_EQ(c.missing, 7);
}

// Success clears an old error text; the fill records of a GUID that is neither source are not counted.
TEST_F(JobTest, DoneClearsLastErrorAndIgnoresForeignFillRecords) {
    store_->setFirstSuccessShown();
    auto r = makeRecord("20261015-104200");
    r.lastError = "old trouble";
    scenario(r.id, 1,
             fill(kNone, 8, 999, 999) + fill(kBase, 8, 10, 0) + written(kBase, "/charts/z.mbtiles", 10, 1),
             "", 0);
    start(r);
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(stripText(), "Chart added: z.");
    const cg::ChartRecord c = chart(r.id);
    EXPECT_EQ(c.lastError, "");
    EXPECT_EQ(c.tiles, 10);
    EXPECT_EQ(c.missing, 0);
}

// 9.3 outcome table, one job per row: headline, state on disk, buttons, last error kept.
struct OutcomeRow {
    const char* name;
    std::string err;
    int code;
    std::string mode;
    std::string headline;
    cg::ChartState state;
    std::vector<std::string> buttons;
    std::string lastError;
};

TEST_F(JobTest, OutcomeTable) {
    using S = cg::ChartState;
    const std::vector<OutcomeRow> rows = {
        {"storage",
         "error: cannot store tiles: no space left\n",
         4,
         "",
         "Cannot save tiles: the disk is full or cannot be written.",
         S::Interrupted,
         {"Resume", "Close"},
         "cannot store tiles: no space left"},
        {"incomplete",
         "error: 12 tiles failed\n",
         4,
         "",
         "The download did not complete; your progress is saved.",
         S::Interrupted,
         {"Resume", "Close"},
         "12 tiles failed"},
        {"refused",
         "error: the area holds no tile at the chosen zooms\n",
         3,
         "",
         "The area has no tiles at the chosen detail.",
         S::Failed,
         {"Close"},
         "the area holds no tile at the chosen zooms"},
        {"exportfailed",
         "error: disk full\n",
         5,
         "",
         "The chart could not be written. Nothing was added.",
         S::Failed,
         {"Build from saved tiles", "Close"},
         "disk full"},
        {"usage",
         "error: --format: expected text or tsv\n",
         2,
         "",
         "The download failed. See Details.",
         S::Failed,
         {"Close"},
         "--format: expected text or tsv"},
        {"internal", "", 1, "", "The download failed. See Details.", S::Failed, {"Close"}, ""},
        {"interrupted",
         "",
         130,
         "",
         "Stopped. Resume any time from My charts.",
         S::Interrupted,
         {"Resume", "Close"},
         ""},
        {"killed",
         "",
         0,
         "selfkill",
         "Stopped by force. Resume continues from the last save (up to 5 minutes lost).",
         S::Interrupted,
         {"Resume", "Close"},
         ""},
    };
    int i = 0;
    for (const OutcomeRow& row : rows) {
        SCOPED_TRACE(row.name);
        auto r = makeRecord(std::string("20261015-12000") + std::to_string(i++));
        scenario(r.id, 1, "", row.err, row.code, row.mode);
        start(r);
        ASSERT_TRUE(waitIdle());
        EXPECT_EQ(stripText(), row.headline);
        EXPECT_EQ(buttons(), row.buttons);
        const cg::ChartRecord c = chart(r.id);
        EXPECT_EQ(c.state, row.state);
        EXPECT_EQ(c.lastError, row.lastError);
        EXPECT_TRUE(ocpn_fake::updates.empty());
    }
    EXPECT_EQ(busyCalls_.size(), rows.size() * 2);
}

// ExportFailed offers "Build from saved tiles": an `export` run with --overwrite and no network flags; the
// numbers of the record stay (the CLI prints no fill records for an export) and decide the state.
TEST_F(JobTest, BuildFromSavedTilesRunsExportAndKeepsTheRecordNumbers) {
    store_->setFirstSuccessShown();
    auto r = makeRecord("20261015-105000");
    r.overlayGuid.clear();
    r.tiles = 1000;
    r.missing = 7;
    scenario(r.id, 1, "", "error: disk full\n", 5);
    scenario(r.id, 2, written(kBase, "/charts/built.mbtiles", 1000, 5), "", 0);
    start(r);
    ASSERT_TRUE(waitIdle());
    ASSERT_TRUE(click("Build from saved tiles"));
    ASSERT_TRUE(runUntil([&] { return runs(r.id) == 2 && !jc().busy(); }));
    const std::vector<std::string> args = argsOf(r.id, 2);
    EXPECT_EQ(args,
              (std::vector<std::string>{"export", "--job", iniOf(r.id), "--format=tsv", "--progress=lines",
                                        "--cache", defaultCache(), "--overwrite", "--stop-on-stdin"}));
    EXPECT_EQ(stripText(), "Chart added, 7 of 1 000 tiles are missing. Top up later to fill them.");
    const cg::ChartRecord c = chart(r.id);
    EXPECT_EQ(c.state, cg::ChartState::Missing);
    EXPECT_EQ(c.tiles, 1000);
    EXPECT_EQ(c.missing, 7);
    EXPECT_EQ(c.lastError, "");
}

// ------------------------------------------------------------------------------------------------- Stop

// Proof 3: Stop -> "Stopping..." at once, the child gets SIGTERM, the result and chart.ini say Interrupted
// within 10 s; Resume runs again (`run --overwrite`) and Details shows the CLI's "continuing the session".
TEST_F(JobTest, StopThenResumeContinuesTheSession) {
    auto r = makeRecord("20261015-106000");
    scenario(r.id, 1, "",
             tsv({"phase", "download", kBase}) + progressDownload(kBase, 13, 412, 1300, 9400000, 111), 0,
             "wait");
    start(r);
    ASSERT_TRUE(waitTextStarts(kSource + ", Coastal"));
    ASSERT_TRUE(click("Stop", false));
    EXPECT_EQ(stripText(), "Stopping - saving progress...");
    ASSERT_TRUE(waitIdle(10000));
    EXPECT_EQ(stripText(), "Stopped. Resume any time from My charts.");
    EXPECT_EQ(buttons(), (std::vector<std::string>{"Resume", "Close"}));
    EXPECT_EQ(chart(r.id).state, cg::ChartState::Interrupted);

    scenario(r.id, 2, "",
             tsv({"phase", "download", kBase}) + "warning: continuing the session of 2026-10-15 05:17:55\n" +
                 progressDownload(kBase, 14, 5, 1300, 1, 1),
             0, "wait");
    ASSERT_TRUE(click("Resume"));
    ASSERT_TRUE(runUntil([&] {
        const std::string d = detailsText();
        return d.find("continuing the session") != std::string::npos &&
               d.find("z14 5/1300") != std::string::npos;
    }));
    std::string withQuote; // the program path as displayCommand shows it: ' becomes '\''
    for (char c : cli_)
        withQuote += c == '\'' ? std::string("'\\''") : std::string(1, c);
    // a CLI with --progress=lines has no progress note in the fold.
    EXPECT_EQ(detailsText(), kSource + ": z14 5/1300\n\nenv LC_ALL=C.UTF-8 nice -n 10 '" + withQuote +
                                 "' run --job " + iniOf(r.id) + " --format=tsv --progress=lines --cache " +
                                 defaultCache() +
                                 " --overwrite\n"
                                 "warning: continuing the session of 2026-10-15 05:17:55\n");
    EXPECT_EQ(argsOf(r.id, 2).front(), "run");
    EXPECT_EQ(chart(r.id).state, cg::ChartState::Running);
    EXPECT_TRUE(jc().busy());
}

// and 6: an Update runs the Download argv plus --replace and the date; Stop keeps the date in
// chart.ini; the strip's Resume starts the same update with the same date; Done clears the date.
TEST_F(JobTest, UpdateRunsWithTheDateKeepsItOnStopAndClearsItWhenDone) {
    auto r = makeRecord("20261015-106100");
    r.state = cg::ChartState::Complete;
    r.updateBefore = "2026-09-16T10:00:00";
    store_->saveChart(r);
    const std::vector<std::string> expected{"run",
                                            "--job",
                                            iniOf(r.id),
                                            "--format=tsv",
                                            "--progress=lines",
                                            "--cache",
                                            defaultCache(),
                                            "--overwrite",
                                            "--replace",
                                            "--replace-older-than=2026-09-16T10:00:00",
                                            "--stop-on-stdin"};
    scenario(r.id, 1, "", tsv({"phase", "download", kBase}), 0, "wait");
    start(r, cg::RunKind::Update, true);
    ASSERT_TRUE(runUntil([&] { return !argsOf(r.id, 1).empty(); }));
    EXPECT_EQ(argsOf(r.id, 1), expected);
    ASSERT_TRUE(click("Stop", false));
    ASSERT_TRUE(waitIdle(10000));
    EXPECT_EQ(chart(r.id).state, cg::ChartState::Interrupted);
    EXPECT_EQ(chart(r.id).updateBefore, "2026-09-16T10:00:00");
    EXPECT_NE(chartIniText(r.id).find("LastError=\nUpdateBefore=2026-09-16T10:00:00\n"), std::string::npos);

    scenario(r.id, 2, "", "", 0);
    ASSERT_TRUE(click("Resume"));
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(argsOf(r.id, 2), expected);
    EXPECT_EQ(chart(r.id).state, cg::ChartState::Complete);
    EXPECT_EQ(chart(r.id).updateBefore, "");
    EXPECT_NE(chartIniText(r.id).find("UpdateBefore=\nWeather="), std::string::npos);
}

// every Done clears the date: a finished chart has no pending update.
TEST_F(JobTest, BuildFromSavedEndingDoneClearsTheUpdateDate) {
    auto r = makeRecord("20261015-106200");
    r.state = cg::ChartState::Interrupted;
    r.updateBefore = "2026-09-16T10:00:00";
    store_->saveChart(r);
    scenario(r.id, 1, "", "", 0);
    start(r, cg::RunKind::BuildFromSaved, true);
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(chart(r.id).state, cg::ChartState::Complete);
    EXPECT_EQ(chart(r.id).updateBefore, "");
    EXPECT_EQ(argsOf(r.id, 1).front(), "export");
}

// a child that ignores the stop: the strip shows only "Stop" while stopping, a second press
// neither kills early nor changes the text, the runner kills after its grace: Killed headline.
TEST_F(JobTest, AChildThatIgnoresTheStopIsKilledByTheRunnerAfterTheGrace) {
    auto r = makeRecord("20261015-107000");
    // the phase text comes from the child (start itself shows "downloading"), so the trap is installed
    scenario(r.id, 1, "", tsv({"phase", "fill", kBase}), 0, "ignore");
    start(r);
    jc_->runner_->setStopGraceMs(800);
    ASSERT_TRUE(waitTextStarts(kSource + ": checking the saved tiles"));
    ASSERT_TRUE(click("Stop", false));
    EXPECT_EQ(stripText(), "Stopping - saving progress...");
    ASSERT_TRUE(click("Stop", false)); // a second press does nothing
    runFor(200);
    EXPECT_TRUE(jc().busy());
    EXPECT_EQ(buttons(), std::vector<std::string>{"Stop"});
    EXPECT_EQ(stripText(), "Stopping - saving progress...");
    ASSERT_TRUE(waitIdle(10000));
    EXPECT_EQ(stripText(), "Stopped by force. Resume continues from the last save (up to 5 minutes lost).");
    EXPECT_EQ(chart(r.id).state, cg::ChartState::Interrupted);
}

// A plain Stop in slow mode below the limit is not a budget stop.
TEST_F(JobTest, UserStopInSlowModeShowsTheStoppedTextNotTheBudgetText) {
    setSettings([](cg::Settings& s) { s.slowConnection = true; });
    auto r = makeRecord("20261015-107100");
    scenario(r.id, 1, "", progressDownload(kBase, 13, 1, 10, 500, 1), 0, "wait");
    start(r);
    ASSERT_TRUE(waitTextStarts(kSource + ", Coastal"));
    jc().stop();
    ASSERT_TRUE(waitIdle(10000));
    EXPECT_EQ(stripText(), "Stopped. Resume any time from My charts.");
    EXPECT_EQ(argsOf(r.id, 1), (std::vector<std::string>{
                                   "run", "--job", iniOf(r.id), "--format=tsv", "--progress=lines", "--cache",
                                   defaultCache(), "--threads", "1", "--overwrite", "--stop-on-stdin"}));
}

// ------------------------------------------------------------------------------------------ retry, budget

const char* const kStall = "error: no progress for 300 s: tile server silent\n";

// Proof 4: Stalled in normal mode -> three automatic retries with a 60 s countdown each, the same argv,
// the busy flag never drops between them; the fourth stall ends the job.
TEST_F(JobTest, StallRetriesThreeTimesWithTheSameArgumentsThenPauses) {
    auto r = makeRecord("20261015-108000");
    for (int n = 1; n <= 4; ++n)
        scenario(r.id, n, "", kStall, 4);
    start(r);
    jc_->tick_.Stop();
    for (int attempt = 1; attempt <= 3; ++attempt) {
        SCOPED_TRACE(attempt);
        const std::string att = std::to_string(attempt);
        ASSERT_TRUE(waitText("No connection. Retry in 60 s (" + att + " of 3)")) << stripText();
        EXPECT_EQ(runs(r.id), attempt);
        EXPECT_EQ(buttons(), std::vector<std::string>{"Stop"});
        EXPECT_TRUE(jc().busy());
        tick(1);
        EXPECT_EQ(stripText(), "No connection. Retry in 59 s (" + att + " of 3)");
        tick(58);
        EXPECT_EQ(stripText(), "No connection. Retry in 1 s (" + att + " of 3)");
        tick(1); // countdown over: the child runs again
        ASSERT_TRUE(runUntil([&] { return runs(r.id) == attempt + 1; }));
    }
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(runs(r.id), 4);
    EXPECT_EQ(argsOf(r.id, 2), argsOf(r.id, 1));
    EXPECT_EQ(argsOf(r.id, 4), argsOf(r.id, 1));
    EXPECT_EQ(stripText(), "No connection. Download paused; your progress is saved.");
    EXPECT_EQ(buttons(), (std::vector<std::string>{"Resume", "Close"}));
    EXPECT_EQ(chart(r.id).state, cg::ChartState::Interrupted);
    EXPECT_EQ(chart(r.id).lastError, "no progress for 300 s: tile server silent");
    EXPECT_EQ(busyCalls_, (std::vector<bool>{true, false}));
}

// beginRun forgets the error line of the run before: run 2 exits 4 without one, so it is not a stall.
TEST_F(JobTest, ARetryRunStartsWithoutTheErrorOfTheRunBefore) {
    auto r = makeRecord("20261015-108050");
    scenario(r.id, 1, "", kStall, 4);
    scenario(r.id, 2, "", "", 4);
    start(r);
    jc_->tick_.Stop();
    ASSERT_TRUE(waitText("No connection. Retry in 60 s (1 of 3)")) << stripText();
    tick(60);
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(runs(r.id), 2);
    EXPECT_EQ(stripText().find("No connection"), std::string::npos) << stripText();
    EXPECT_EQ(buttons(), (std::vector<std::string>{"Resume", "Close"}));
}

// Slow mode: no automatic retry.
TEST_F(JobTest, SlowModeDoesNotRetryAStall) {
    setSettings([](cg::Settings& s) { s.slowConnection = true; });
    auto r = makeRecord("20261015-108100");
    scenario(r.id, 1, "", kStall, 4);
    start(r);
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(stripText(), "No connection. Download paused; your progress is saved.");
    EXPECT_EQ(runs(r.id), 1);
}

// Stop during the countdown: Interrupted at once, no further run, the timer cannot restart anything.
TEST_F(JobTest, StopDuringTheCountdownInterrupts) {
    auto r = makeRecord("20261015-108200");
    scenario(r.id, 1, "", kStall, 4);
    start(r);
    jc_->tick_.Stop();
    ASSERT_TRUE(waitText("No connection. Retry in 60 s (1 of 3)")) << stripText();
    tick(3);
    ASSERT_TRUE(click("Stop"));
    EXPECT_EQ(stripText(), "Stopped. Resume any time from My charts.");
    EXPECT_EQ(buttons(), (std::vector<std::string>{"Resume", "Close"}));
    EXPECT_FALSE(jc().busy());
    EXPECT_EQ(chart(r.id).state, cg::ChartState::Interrupted);
    EXPECT_EQ(busyCalls_, (std::vector<bool>{true, false}));
    tick(60); // a late tick must not start a run
    runFor(300);
    EXPECT_EQ(runs(r.id), 1);
}

// The 1 s timer really ticks: the countdown moves on its own.
TEST_F(JobTest, TheRealTimerCountsTheRetryDown) {
    auto r = makeRecord("20261015-108300");
    scenario(r.id, 1, "", kStall, 4);
    start(r);
    ASSERT_TRUE(waitTextStarts("No connection. Retry in"));
    ASSERT_TRUE(runUntil([&] { return stripText() != "No connection. Retry in 60 s (1 of 3)"; }, 4000))
        << stripText();
    const std::string t = stripText();
    EXPECT_TRUE(t == "No connection. Retry in 59 s (1 of 3)" || t == "No connection. Retry in 58 s (1 of 3)")
        << t;
}

// Proof 5: slow mode with a budget: the sum of the latest bytes per GUID over the data limit stops the job
// (strictly more than the limit); the stop shows the budget text; normal mode has no budget.
struct BudgetRow {
    const char* name;
    bool slow;
    std::string progress;
    bool stops;
};

TEST_F(JobTest, BudgetStopsASlowJobWhenTheLimitIsCrossed) {
    const std::vector<BudgetRow> rows = {
        {"over", true, progressDownload(kBase, 13, 1, 9, 1000001, 1), true},
        {"exactly the limit", true, progressDownload(kBase, 13, 1, 9, 1000000, 1), false},
        {"two sources add up", true,
         progressDownload(kBase, 13, 1, 9, 600000, 1) + progressDownload(kOver, 13, 1, 9, 600000, 1), true},
        {"latest value per source, not a sum of records", true,
         progressDownload(kBase, 13, 1, 9, 600000, 1) + progressDownload(kBase, 13, 2, 9, 700000, 2), false},
        {"normal mode has no budget", false, progressDownload(kBase, 13, 1, 9, 5000000, 1), false},
    };
    store_->setFirstSuccessShown();
    int i = 0;
    for (const BudgetRow& row : rows) {
        SCOPED_TRACE(row.name);
        setSettings([&](cg::Settings& s) {
            s.slowConnection = row.slow;
            s.dataBudgetMb = 1;
        });
        auto r = makeRecord(std::string("20261015-13000") + std::to_string(i++));
        r.overlayGuid = kOver;
        // The child always waits for a signal: a job that must NOT be stopped is seen to be still running.
        scenario(r.id, 1, "", row.progress, 0, "wait");
        start(r);
        if (row.stops) {
            ASSERT_TRUE(waitIdle(10000));
            EXPECT_EQ(stripText(), "Stopped at your data limit (1 MB). Resume to continue.");
            EXPECT_EQ(buttons(), (std::vector<std::string>{"Resume", "Close"}));
            EXPECT_EQ(chart(r.id).state, cg::ChartState::Interrupted);
        } else {
            ASSERT_TRUE(waitTextStarts(kSource + ", Coastal"));
            runFor(300);
            EXPECT_TRUE(jc().busy());
            EXPECT_EQ(stripText().rfind(kSource + ", Coastal", 0), 0u) << stripText();
            jc().stop();
            ASSERT_TRUE(waitIdle(10000));
            EXPECT_EQ(stripText(), "Stopped. Resume any time from My charts.");
        }
    }
}

// ------------------------------------------------------------------------------------- Details and messages

TEST_F(JobTest, DetailsHoldTheLast200MessageLinesWithTheirPrefixes) {
    auto r = makeRecord("20261015-109000");
    std::string err = "error: first error\nwarning: first warning\nplain note\n";
    for (int i = 1; i <= 250; ++i) {
        char line[32];
        std::snprintf(line, sizeof line, "warning: line %03d\n", i);
        err += line;
    }
    scenario(r.id, 1, "", err, 0, "wait");
    start(r);
    ASSERT_TRUE(runUntil([&] { return detailsText().find("warning: line 250\n") != std::string::npos; }));
    const std::string d = detailsText();
    EXPECT_NE(d.find("warning: line 051\n"), std::string::npos); // 3 + 250 = 253 lines, the last 200 stay
    EXPECT_EQ(d.find("warning: line 050\n"), std::string::npos);
    EXPECT_EQ(d.find("plain note"), std::string::npos);
    EXPECT_EQ(d.find("first error"), std::string::npos);
    // the lines kept are exactly 200
    std::size_t count = 0;
    for (std::size_t p = d.find("warning: line"); p != std::string::npos; p = d.find("warning: line", p + 1))
        ++count;
    EXPECT_EQ(count, 200u);
    jc().stop();
    ASSERT_TRUE(waitIdle(10000));
}

TEST_F(JobTest, DetailsKeepThePrefixesOfErrorsWarningsAndPlainLines) {
    auto r = makeRecord("20261015-109100");
    scenario(r.id, 1, "", "error: e1\nwarning: w1\nplain note\n", 1);
    start(r);
    ASSERT_TRUE(waitIdle());
    const std::string d = detailsText();
    EXPECT_NE(d.find("\nerror: e1\nwarning: w1\nplain note\n"), std::string::npos) << d;
}

// ------------------------------------------------------------------------------------ StartFailed, job file

// Proof 8: a wrong CliPath -> the headline, a working Preferences... button, Close hides the strip; a new
// chart becomes Interrupted, another keeps its state.
TEST_F(JobTest, StartFailedOffersPreferencesAndKeepsOrInterruptsTheRecord) {
    setSettings([&](cg::Settings& s) { s.cliPath = (root_ / "nowhere" / "cli").string(); });
    auto fresh = makeRecord("20261015-110000");
    start(fresh);
    EXPECT_FALSE(jc().busy());
    EXPECT_EQ(stripText(), "Chart Grabber CLI was not found. Install the chart-grabber-c"
                           "li package, or choose the program under Advanced in Preferences.");
    EXPECT_EQ(buttons(), (std::vector<std::string>{"Preferences...", "Close"}));
    EXPECT_EQ(chart(fresh.id).state, cg::ChartState::Interrupted);
    EXPECT_EQ(prefs_, 0);
    ASSERT_TRUE(click("Preferences..."));
    EXPECT_EQ(prefs_, 1);
    ASSERT_TRUE(click("Close"));
    EXPECT_FALSE(strip()->IsShown());
    EXPECT_TRUE(busyCalls_.empty());

    auto old = makeRecord("20261015-110100");
    old.state = cg::ChartState::Complete;
    store_->saveChart(old);
    start(old);
    EXPECT_EQ(chart(old.id).state, cg::ChartState::Complete);
    EXPECT_EQ(buttons(), (std::vector<std::string>{"Preferences...", "Close"}));
    EXPECT_TRUE(strip()->IsShown()); // the next start shows the strip again
}

// Carried item (one "program not found" text): with no CliPath and no Chart Grabber CLI on PATH the headline
// AND the `error:` line under Details are cliNotFoundText; nothing is probed, no child starts.
TEST_F(JobTest, NoProgramOnPathGivesTheSameTextInTheHeadlineAndTheDetails) {
    // Through the seam, so an installed /usr/bin/chart-grabber-cli cannot be found.
    fs::create_directories(root_ / "empty path");
    jc().cliDefaultPaths = {(root_ / "no-such" / "chart-grabber-cli").string()};
    jc().cliPathEnv = (root_ / "empty path").string();
    setSettings([](cg::Settings& s) { s.cliPath.clear(); });
    auto r = makeRecord("20261015-120700");
    start(r);
    EXPECT_FALSE(jc().busy());
    const std::string text = "Chart Grabber CLI was not found. Install the chart-grabber-c"
                             "li package, or choose the program under Advanced in Preferences.";
    EXPECT_EQ(stripText(), text);
    EXPECT_NE(detailsText().find("error: " + text + "\n"), std::string::npos) << detailsText();
    EXPECT_EQ(probes(), 0);
    EXPECT_EQ(runs(r.id), 0);
}

// with CliPath empty the automatic program is cliDefaultPaths, and it is probed and run.
TEST_F(JobTest, EmptyCliPathUsesTheDefaultPathSeam) {
    const std::string other = standIn("default seam", "chart-grabber-cli 2026.10.8 (test)");
    fs::create_directories(root_ / "empty path");
    jc().cliDefaultPaths = {other};
    jc().cliPathEnv = (root_ / "empty path").string();
    setSettings([](cg::Settings& s) { s.cliPath.clear(); });
    auto r = makeRecord("20261015-120701");
    start(r);
    EXPECT_EQ(probes("default seam"), 1);
    EXPECT_EQ(probes(), 0) << "the fixture's own stand-in is not the one used";
    EXPECT_NE(stripText(), "");
}

// Proof 8: no job.ini -> its headline, state Failed, Close only, no child.
TEST_F(JobTest, MissingJobFileFailsTheChartWithoutRunning) {
    auto r = makeRecord("20261015-111000", false);
    start(r);
    EXPECT_FALSE(jc().busy());
    EXPECT_EQ(stripText(),
              "The download settings of this chart are missing. Delete the chart and make it again.");
    EXPECT_EQ(buttons(), std::vector<std::string>{"Close"});
    EXPECT_EQ(chart(r.id).state, cg::ChartState::Failed);
    EXPECT_EQ(runs(r.id), 0);
    EXPECT_TRUE(busyCalls_.empty());
}

// CG_EXTRA_ARGS (debug builds only): split on spaces, empty parts dropped, appended after the run arguments.
TEST_F(JobTest, ExtraArgumentsAreAppendedInDebugBuilds) {
#ifdef NDEBUG
    GTEST_SKIP() << "CG_EXTRA_ARGS exists in debug builds only: NOT run";
#else
    EnvGuard guard("CG_EXTRA_ARGS");
    setenv("CG_EXTRA_ARGS", "  --stall-timeout   5 x ", 1);
    auto r = makeRecord("20261015-112000");
    start(r, cg::RunKind::Download, true);
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(argsOf(r.id, 1),
              (std::vector<std::string>{"run", "--job", iniOf(r.id), "--format=tsv", "--progress=lines",
                                        "--cache", defaultCache(), "--overwrite", "--stall-timeout", "5", "x",
                                        "--stop-on-stdin"}));
#endif
}

// ---------------------------------------------------------------------------------------- shutdown, 8.6

// Proof 6: the controller is destroyed while the child runs (DeInit). Events arriving during the wait
// (a strip Stop, a new start, busy) change nothing; whatever the exit (143 or a clean 0) the record on disk
// says Interrupted with the last error and the planned files, nothing is registered, no strip is left.
TEST_F(JobTest, DestroyedWhileRunningSavesInterruptedAndIgnoresEventsDuringTheWait) {
    for (const char* mode : {"slowterm", "termzero"}) {
        SCOPED_TRACE(mode);
        busyCalls_.clear();
        ocpn_fake::updates.clear();
        auto r = makeRecord(std::string("20261015-1140") + (mode[0] == 's' ? "00" : "01"));
        scenario(r.id, 1, written(kBase, "/charts/late.mbtiles", 4, 9),
                 tsv({"phase", "fill", kBase}) + "error: disk is slow\n", 0, mode);
        start(r);
        ASSERT_TRUE(waitTextStarts(kSource + ": checking the saved tiles"));
        runFor(100);
        JobController* raw = jc_.get();
        const cg::ChartRecord second = makeRecord("20261015-114999");
        bool reentered = false;
        wxEvtHandler handler;
        wxTimer timer(&handler);
        handler.Bind(wxEVT_TIMER, [&](wxTimerEvent&) {
            reentered = true;
            raw->stop();
            raw->start(second, cg::RunKind::Download, true);
        });
        timer.StartOnce(100);
        const std::size_t before = busyCalls_.size();
        const int changesBefore = changes_;
        jc_.reset();
        timer.Stop();
        EXPECT_TRUE(reentered);
        EXPECT_EQ(runs("20261015-114999"), 0);
        EXPECT_EQ(runs(r.id), 1);
        const cg::ChartRecord c = chart(r.id);
        EXPECT_EQ(c.state, cg::ChartState::Interrupted);
        EXPECT_EQ(c.lastError, "disk is slow");
        EXPECT_EQ(c.files, (std::vector<std::string>{"/planned/a.mbtiles", "/planned/b.mbtiles"}));
        EXPECT_NE(chartIniText(r.id).find("\nState=interrupted\n"), std::string::npos);
        EXPECT_TRUE(ocpn_fake::updates.empty());
        EXPECT_EQ(busyCalls_.size(), before); // the hooks were cleared first
        EXPECT_EQ(changes_, changesBefore);
        EXPECT_EQ(strip(), nullptr);
        EXPECT_FALSE(fs::exists(jobDir("20261015-114999") / "chart.ini")); // start was ignored
    }
}

// 8.6: no exit arrived (here: a retry wait, the child is gone) -> the destructor saves Interrupted itself.
TEST_F(JobTest, DestroyedInARetryWaitSavesInterrupted) {
    auto r = makeRecord("20261015-115000");
    scenario(r.id, 1, "", kStall, 4);
    start(r);
    jc_->tick_.Stop();
    ASSERT_TRUE(waitText("No connection. Retry in 60 s (1 of 3)")) << stripText();
    const std::size_t before = busyCalls_.size();
    jc_.reset();
    const cg::ChartRecord c = chart(r.id);
    EXPECT_EQ(c.state, cg::ChartState::Interrupted);
    EXPECT_EQ(c.lastError, "no progress for 300 s: tile server silent");
    EXPECT_EQ(runs(r.id), 1);
    EXPECT_EQ(busyCalls_.size(), before);
    EXPECT_EQ(strip(), nullptr);
}

// ------------------------------------------------------------------------------------------------ Show

// 9.4: Show moves the view to the centre of the chart's box, the scale fits the box into the canvas; the
// content scale factor multiplies the client size; nothing else moves the view.
TEST_F(JobTest, ShowButtonJumpsToTheChartAndNothingElseDoes) {
    store_->setFirstSuccessShown();
    auto canvas = new wxPanel(canvas_, wxID_ANY, wxDefaultPosition, wxSize(800, 600));
    canvas->SetClientSize(800, 600);
    ocpn_fake::canvasWindow = canvas;
    auto r = makeRecord("20261015-116000");
    r.bbox = {-1.5, 50.25, 2.125, 51.0};
    r.overlayGuid.clear();
    scenario(r.id, 1, written(kBase, "/charts/s.mbtiles", 1, 1), "", 0);
    start(r);
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(ocpn_fake::jump.calls, 0);
    ASSERT_TRUE(click("Show"));
    ASSERT_EQ(ocpn_fake::jump.calls, 1);
    EXPECT_DOUBLE_EQ(ocpn_fake::jump.lat, 50.625);
    EXPECT_DOUBLE_EQ(ocpn_fake::jump.lon, 0.3125);
    EXPECT_GT(ocpn_fake::jump.scale, 0);
    ocpn_fake::canvasWindow = canvas_;
}

TEST_F(JobTest, ShowOnChartFitsTheBoxIntoTheCanvasOrTheFocusViewport) {
    auto* canvas = new wxPanel(canvas_, wxID_ANY, wxDefaultPosition, wxSize(800, 600));
    canvas->SetClientSize(800, 600);
    ASSERT_EQ(canvas->GetClientSize(), wxSize(800, 600));
    ocpn_fake::canvasWindow = canvas;
    const cg::BBox box{-1, -1, 1,
                       1}; // 2 x 2 degrees at the equator: 222 640 m each way (111 320 m per degree)
    showOnChart(box, *area_);
    ASSERT_EQ(ocpn_fake::jump.calls, 1);
    EXPECT_DOUBLE_EQ(ocpn_fake::jump.lat, 0);
    EXPECT_DOUBLE_EQ(ocpn_fake::jump.lon, 0);
    EXPECT_NEAR(ocpn_fake::jump.scale, 600.0 / 222640.0, 1e-12);

    ocpn_fake::contentScale = 2.0; // physical pixels
    showOnChart(box, *area_);
    EXPECT_NEAR(ocpn_fake::jump.scale, 1200.0 / 222640.0, 1e-12);
    ocpn_fake::contentScale = 1.0;

    const cg::BBox north{10, 49, 12, 51}; // width shrinks with cos(50 deg) = 0.6427876096865393
    canvas->SetClientSize(300, 800);
    ASSERT_EQ(canvas->GetClientSize(), wxSize(300, 800));
    showOnChart(north, *area_);
    EXPECT_DOUBLE_EQ(ocpn_fake::jump.lat, 50);
    EXPECT_DOUBLE_EQ(ocpn_fake::jump.lon, 11);
    EXPECT_NEAR(ocpn_fake::jump.scale, 300.0 / (222640.0 * 0.6427876096865393), 1e-12);

    PlugIn_ViewPort vp{}; // the focus canvas viewport wins over the window
    vp.pix_width = 1000;
    vp.pix_height = 500;
    area_->rememberViewport(0, vp);
    showOnChart(box, *area_);
    EXPECT_NEAR(ocpn_fake::jump.scale, 500.0 / 222640.0, 1e-12);
    EXPECT_EQ(ocpn_fake::jump.calls, 4);
    ocpn_fake::canvasWindow = canvas_;
}

TEST_F(JobTest, ShowOnChartDoesNothingWithoutAnySizeToFit) {
    ocpn_fake::canvasWindow = nullptr;
    showOnChart({-1, -1, 1, 1}, *area_);
    EXPECT_EQ(ocpn_fake::jump.calls, 0);
    ocpn_fake::canvasWindow = canvas_;
}

// ------------------------------------------------------------------- levels, locale

// Proofs 1 and 6, tsv level: the child is `env LC_ALL=C.UTF-8 nice -n 10 <cli> ...`, sees LC_ALL=C.UTF-8
// whatever the user's locale, Stop still ends it (SIGTERM reaches the stand-in itself: env and nice exec) and
// the result is Interrupted; the cache folder is created before the run; one probe ran.
TEST_F(JobTest, TheChildGetsLcAllAndTheTsvArgumentsAndStopStillEndsIt) {
    EnvGuard guard("LC_ALL");
    setenv("LC_ALL", "ru_RU.UTF-8", 1);
    ASSERT_FALSE(fs::exists(defaultCache()));
    auto r = makeRecord("20261015-120000");
    scenario(r.id, 1, "", progressDownload(kBase, 13, 412, 1300, 9400000, 111), 0, "wait");
    start(r);
    EXPECT_TRUE(fs::is_directory(defaultCache())) << "created before the child starts";
    ASSERT_TRUE(waitTextStarts(kSource + ", Coastal")) << stripText(); // the trap is set by then
    EXPECT_EQ(lcAll(r.id, 1), "C.UTF-8");
    EXPECT_EQ(argsOf(r.id, 1),
              (std::vector<std::string>{"run", "--job", iniOf(r.id), "--format=tsv", "--progress=lines",
                                        "--cache", defaultCache(), "--overwrite", "--stop-on-stdin"}));
    EXPECT_NE(detailsText().find("\n" +
                                 shownCommand("run --job " + iniOf(r.id) +
                                              " --format=tsv --progress=lines --cache " + defaultCache() +
                                              " --overwrite") +
                                 "\n"),
              std::string::npos)
        << detailsText();
    EXPECT_EQ(probes(), 1);
    jc().stop();
    ASSERT_TRUE(waitIdle(10000));
    EXPECT_EQ(stripText(), "Stopped. Resume any time from My charts.");
    EXPECT_EQ(chart(r.id).state, cg::ChartState::Interrupted);
}

// Proof 4: the folders given in Preferences pass through unchanged after --format/--progress and the cache
// folder is created with its parents.
TEST_F(JobTest, GivenFoldersPassThroughUnchanged) {
    const std::string maps = (root_ / "maps \xD1\x91 d'x").string();
    const std::string cache = (root_ / "\xD0\xBA\xD1\x8D\xD1\x88 d'x" / "a b").string();
    fs::create_directories(maps);
    std::ofstream(fs::path(maps) / "a.zmp") << "x"; // --maps is passed only when the folder holds a source
    setSettings([&](cg::Settings& s) {
        s.mapsDir = maps;
        s.cacheDir = cache;
    });
    const std::string bundled = (root_ / "bundled d'x").string();
    store_->setBundledSourcesDir(bundled);
    auto r = makeRecord("20261015-120100");
    scenario(r.id, 1, "", "", 0);
    start(r);
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(argsOf(r.id, 1),
              (std::vector<std::string>{"run", "--job", iniOf(r.id), "--format=tsv", "--progress=lines",
                                        "--maps", bundled, "--maps", maps, "--cache", cache, "--overwrite",
                                        "--stop-on-stdin"}));
    EXPECT_TRUE(fs::is_directory(cache));
    EXPECT_FALSE(fs::exists(defaultCache()));
    EXPECT_EQ(store_->loadSettings().cacheDir, cache);
}

// ---- the records of --progress=lines (CLI 2026.10.8), captured in tests/fixtures/cli-2026.10.8/

namespace {
const std::string kEsri = "{7B743985-BC5F-4AB6-8915-AC5DBBB8F552}";
const std::string kMarks = "{AB8A8685-AEC6-4E16-80DA-DC4AC37E4777}";

// A capture of the pinned CLI 2026.10.8 with the scratch directory of its capture replaced by /scr.
std::string fixtureNew(const std::string& name) {
    std::string t = slurp(fs::path(CG_FIXTURES_DIR) / "cli-2026.10.8" / name);
    EXPECT_FALSE(t.empty()) << name;
    for (std::size_t at; (at = t.find("$SCRATCH")) != std::string::npos;)
        t.replace(at, 8, "/scr");
    return t;
}

// The first n lines of a fixture text.
std::string headLines(const std::string& text, int n) {
    std::istringstream in(text);
    std::string out, line;
    for (int i = 0; i < n && std::getline(in, line); ++i)
        out += line + "\n";
    return out;
}
} // namespace

// Proof 5: the Details fold of a running job starts with the progress lines, never with a progress note; the
// real stderr of the captured run (phases, a `warning:` between records, the final record printed twice) is
// read as records and messages.
TEST_F(JobTest, TheDetailsStartWithTheProgressLines) {
    auto r = makeRecord("20261015-121000");
    r.sourceGuid = kEsri;
    r.overlayGuid = kMarks;
    scenario(r.id, 1, "", fixtureNew("run-continued.stderr"), 0, "wait");
    start(r);
    ASSERT_TRUE(waitText("Building the chart " + kName + " - Seamarks: 100 %")) << stripText();
    EXPECT_EQ(
        detailsText(),
        kSource + ": z13 243/243\nSeamarks: z13 243/243\n\n" +
            shownCommand("run --job " + iniOf(r.id) + " --format=tsv --progress=lines --cache " +
                         defaultCache() + " --overwrite") +
            "\nwarning: \"Esri World Imagery\": the provider's terms of use may not allow downloading tiles "
            "for "
            "offline use. Read https://www.arcgis.com/home/item.html?id=10df2279f9684e4a9f6a7f08febac2a9 "
            "before downloading; you are responsible for complying.\n"
            "warning: continuing the session of 2026-10-21 21:39:37\n");
    jc().stop();
    ASSERT_TRUE(waitIdle(10000));
}

// Proof 4 (R-a), run: `phase download` with no progress record before the export phase (core
// download_runner.cpp:511-512). The strip goes from the "downloading" line to the "writing" line, the Details
// fold has no progress line, and the whole run ends Done with the chart registered.
TEST_F(JobTest, ARunWhosePhaseDownloadHasNoProgressGoesOnToWritingAndEndsDone) {
    store_->setFirstSuccessShown();
    const std::string err = fixtureNew("run-phase-without-progress.stderr");
    auto r = makeRecord("20261015-121100");
    r.sourceGuid = kEsri;
    r.overlayGuid.clear();
    scenario(r.id, 1, "", headLines(err, 1), 0, "wait");
    start(r);
    ASSERT_TRUE(waitText(kSource + ": downloading...")) << stripText();
    EXPECT_EQ(detailsText(),
              "\n" +
                  shownCommand("run --job " + iniOf(r.id) + " --format=tsv --progress=lines --cache " +
                               defaultCache() + " --overwrite") +
                  "\n");
    jc().stop();
    ASSERT_TRUE(waitIdle(10000));

    auto w = makeRecord("20261015-121101");
    w.sourceGuid = kEsri;
    w.overlayGuid.clear();
    scenario(w.id, 1, "", headLines(err, 2), 0, "wait");
    start(w);
    ASSERT_TRUE(waitText(kSource + ": building the chart...")) << stripText();
    EXPECT_EQ(detailsText().find(": z"), std::string::npos) << detailsText();
    jc().stop();
    ASSERT_TRUE(waitIdle(10000));

    auto d = makeRecord("20261015-121102");
    d.sourceGuid = kEsri;
    d.overlayGuid.clear();
    scenario(d.id, 1, fill(kEsri, 9, 4, 0) + written(kEsri, "/charts/a.mbtiles", 4, 99), err, 0);
    start(d);
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(chart(d.id).state, cg::ChartState::Complete);
    ASSERT_EQ(ocpn_fake::updates.size(), 1u);
    EXPECT_EQ(dirs(0), (std::vector<std::string>{"/charts"}));
    EXPECT_EQ(detailsText().find(": z"), std::string::npos) << detailsText();
}

// Proofs 4 / 5 (R-a, R-b), resume-shaped stream: phase A, phase B, `error: cannot continue <file of B>`, then
// the progress record of A. The figures are those of the record and are shown for A; B gets no progress line;
// the error is kept as an error message.
TEST_F(JobTest, AResumeShapedStreamShowsTheProgressRecordForItsGuidAsGiven) {
    auto r = makeRecord("20261015-121200");
    r.sourceGuid = kEsri;
    r.overlayGuid = kMarks;
    scenario(r.id, 1, "", fixtureNew("resume-refused-b.stderr"), 0, "wait");
    start(r);
    ASSERT_TRUE(waitText(kSource + ", Passage: 7 %  0.3 MB  about 11 s left")) << stripText();
    EXPECT_EQ(gaugeValue(), 78); // 19 * 1000 / 243
    EXPECT_EQ(detailsText(),
              kSource + ": z11 19/243\n\n" +
                  shownCommand("run --job " + iniOf(r.id) + " --format=tsv --progress=lines --cache " +
                               defaultCache() + " --overwrite") +
                  "\nerror: cannot continue /scr/data/AutoSave/25574d75ff1b/" + kMarks +
                  "/final.sls: the session does not match the job\n");
    EXPECT_TRUE(jc().busy());
    jc().stop();
    ASSERT_TRUE(waitIdle(10000));
}

// Proof 5 (R-b), sums: two phase records and one progress record count the record's bytes once. The limit is
// 1 MB and the record says 999 999 bytes: not exceeded; 1 000 001 bytes of the same stream stop the job.
TEST_F(JobTest, TheDataLimitCountsAResumeShapedStreamOnce) {
    setSettings([](cg::Settings& s) {
        s.slowConnection = true;
        s.dataBudgetMb = 1;
    });
    const std::string err = fixtureNew("resume-refused-b.stderr");
    const auto withBytes = [&](const std::string& bytes) {
        std::string t = err;
        t.replace(t.find("291609"), 6, bytes);
        return t;
    };
    auto r = makeRecord("20261015-121300");
    r.sourceGuid = kEsri;
    r.overlayGuid = kMarks;
    scenario(r.id, 1, "", withBytes("999999"), 0, "wait");
    start(r);
    ASSERT_TRUE(waitTextStarts(kSource + ", Passage: 7 %")) << stripText();
    runFor(300);
    EXPECT_TRUE(jc().busy());
    jc().stop();
    ASSERT_TRUE(waitIdle(10000));
    EXPECT_EQ(stripText(), "Stopped. Resume any time from My charts.");

    auto o = makeRecord("20261015-121301");
    o.sourceGuid = kEsri;
    o.overlayGuid = kMarks;
    scenario(o.id, 1, "", withBytes("1000001"), 0, "wait");
    start(o);
    ASSERT_TRUE(waitIdle(10000));
    EXPECT_EQ(stripText(), "Stopped at your data limit (1 MB). Resume to continue.");
}

// Proof 6 (R-c): a progress record with an empty GUID is a progress record: it neither crashes
// nor stops the job, and the next record with a GUID is shown.
TEST_F(JobTest, AProgressRecordWithAnEmptyGuidKeepsTheJobRunningAndTheNextRecordIsShown) {
    const std::string err = fixtureNew("progress-empty-guid.stderr");
    auto r = makeRecord("20261015-121400");
    r.sourceGuid = kEsri;
    r.overlayGuid.clear();
    scenario(r.id, 1, "", headLines(err, 2), 0, "wait");
    start(r);
    ASSERT_TRUE(runUntil([&] { return detailsText().find(": z3 5/243\n") != std::string::npos; }))
        << detailsText();
    runFor(300);
    EXPECT_TRUE(jc().busy());
    EXPECT_EQ(chart(r.id).state, cg::ChartState::Running);
    EXPECT_NE(stripText().find(", zoom 3: 2 %"), std::string::npos) << stripText();
    jc().stop();
    ASSERT_TRUE(waitIdle(10000));

    auto n = makeRecord("20261015-121401");
    n.sourceGuid = kEsri;
    n.overlayGuid.clear();
    scenario(n.id, 1, "", err, 0, "wait");
    start(n);
    ASSERT_TRUE(waitText(kSource + ", Passage: 7 %  0.3 MB  about 23 s left")) << stripText();
    EXPECT_EQ(detailsText().substr(0, detailsText().find("\n\n")), ": z3 5/243\n" + kSource + ": z11 19/243");
    EXPECT_TRUE(jc().busy());
    jc().stop();
    ASSERT_TRUE(waitIdle(10000));
}

// the real stderr of the pinned 2026.10.8 CLI. A run (the plugin's command) puts the CLI's
// navigation notice into Details exactly once, after the phase lines and before the first tile; an export
// capture of the same job holds none. Nothing in the plugin matches the wording: the line is shown as it
// came.
TEST_F(JobTest, TheCliNavigationNoticeReachesDetailsOnceForARunAndNotForAnExport) {
    store_->setFirstSuccessShown();
    const auto capture = [](const char* name) {
        return slurp(fs::path(CG_FIXTURES_DIR) / "cli-2026.10.8" / name);
    };
    const std::string notice =
        "warning: \"EMODnet Bathymetry (world)\": not for navigation. This source is not an "
        "official nautical chart; do not use it to navigate.\n";
    auto r = makeRecord("20261018-078000");
    r.sourceGuid = "{C44877A0-D455-4238-B5E2-C8BFF25FC0D4}";
    ASSERT_NE(capture("run-navigation-warning.stderr").find(notice), std::string::npos);
    scenario(r.id, 1, capture("run-navigation-warning.stdout"), capture("run-navigation-warning.stderr"), 0);
    start(r);
    ASSERT_TRUE(waitIdle());
    const std::string d = detailsText();
    const std::size_t at = d.find(notice);
    ASSERT_NE(at, std::string::npos) << d;
    EXPECT_EQ(d.find(notice, at + 1), std::string::npos) << "once";
    EXPECT_EQ(d.find("not for navigation"),
              at + std::string("warning: \"EMODnet Bathymetry (world)\": ").size());

    auto e = makeRecord("20261018-078100");
    e.sourceGuid = "{C44877A0-D455-4238-B5E2-C8BFF25FC0D4}";
    scenario(e.id, 1, capture("export-navigation-warning.stdout"),
             capture("export-navigation-warning.stderr"), 0);
    start(e, cg::RunKind::BuildFromSaved, true);
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(detailsText().find("not for navigation"), std::string::npos) << detailsText();
}

// Proof 5, step 3a: a program that is too old (0.1.0, 2026.10.3) or is no tile program: no child for the job
// (only the probe ran), the result line is cliTooOldText with Preferences..., a new chart becomes
// Interrupted and another keeps its state.
TEST_F(JobTest, ATooOldProgramStartsNoChildAndKeepsTheRecordState) {
    int i = 0;
    for (const char* line : {"old-tiles 0.1.0 (test)", "chart-grabber-cli 2026.10.3 (test)", "hello world"}) {
        SCOPED_TRACE(line);
        const std::string folder = std::string("old ") + std::to_string(++i);
        const std::string old = standIn(folder, line);
        setSettings([&](cg::Settings& s) { s.cliPath = old; });
        busyCalls_.clear();
        auto fresh = makeRecord(std::string("20261015-12060") + std::to_string(i));
        start(fresh);
        EXPECT_FALSE(jc().busy());
        EXPECT_EQ(stripText(),
                  "Chart Grabber CLI is too old. Install the current chart-grabber-cli package.");
        EXPECT_EQ(buttons(), (std::vector<std::string>{"Preferences...", "Close"}));
        EXPECT_EQ(chart(fresh.id).state, cg::ChartState::Interrupted);
        EXPECT_EQ(runs(fresh.id), 0);
        EXPECT_FALSE(fs::exists(jobDir(fresh.id) / "args.1"));
        EXPECT_EQ(detailsText(), "");
        EXPECT_EQ(probes(folder), 1) << "only the probe ran";
        EXPECT_TRUE(busyCalls_.empty());
        EXPECT_FALSE(fs::exists(defaultCache())) << "no cache folder for a job that does not start";
        ASSERT_TRUE(click("Preferences..."));
        EXPECT_EQ(prefs_, i);

        auto kept = makeRecord(std::string("20261015-12061") + std::to_string(i));
        kept.state = cg::ChartState::Missing;
        store_->saveChart(kept);
        start(kept);
        EXPECT_EQ(chart(kept.id).state, cg::ChartState::Missing);
        EXPECT_EQ(stripText(),
                  "Chart Grabber CLI is too old. Install the current chart-grabber-cli package.");
        EXPECT_EQ(runs(kept.id), 0);
        EXPECT_EQ(probes(folder), 1) << "the second start uses the cached probe";
        EXPECT_TRUE(busyCalls_.empty());
    }
}

// ------------------------------------------------------------------------------------- the bar

std::string exportProgress(const std::string& guid, int permille) {
    return tsv({"progress", "export", guid, std::to_string(permille)});
}

// Proof 3: live is nullopt when idle; right after the start it is Sizing with the pulse; a progress record
// with a total gives Downloading with the MEAN of the two sources (base 100/200, overlay not seen: 250) and
// the seconds left (elapsed 40 * 100 / 100); Stop gives Stopping with the bar kept.
TEST_F(JobTest, LiveFollowsSizingDownloadingAndStopping) {
    auto r = makeRecord("20261016-100000");
    EXPECT_EQ(jc().live(), std::nullopt);
    scenario(r.id, 1, "", tsv({"phase", "download", kBase}) + progressDownload(kBase, 13, 100, 200, 1000, 40),
             0, "wait");
    start(r);
    auto l = jc().live();
    ASSERT_TRUE(l.has_value());
    EXPECT_EQ(l->chartId, r.id);
    EXPECT_EQ(l->stage, cg::LiveStage::Sizing);
    EXPECT_EQ(l->permille, -1);
    ASSERT_TRUE(runUntil([&] { return jc().live()->stage == cg::LiveStage::Downloading; }));
    l = jc().live();
    EXPECT_EQ(l->permille, 250);
    EXPECT_EQ(l->donePermille, 250);
    EXPECT_EQ(l->secondsLeft, std::optional<std::int64_t>(40));
    jc().stop();
    l = jc().live();
    EXPECT_EQ(l->stage, cg::LiveStage::Stopping);
    EXPECT_EQ(l->permille, 250);
    ASSERT_TRUE(waitIdle(10000));
    EXPECT_EQ(jc().live(), std::nullopt);
}

// Proof 3: a `phase fill` is Checking (the bar kept); a later `phase download` is Downloading again; `phase
// export` alone is Building at 0, with export 400 at 400. A trailing message line marks "all records read".
TEST_F(JobTest, LiveShowsCheckingAndBuilding) {
    const std::string marker = "warning: marker\n";
    int n = 0;
    const auto liveAfter = [&](const std::string& err) {
        auto r = makeRecord(std::string("20261016-10010") + std::to_string(n++));
        scenario(r.id, 1, "", err + marker, 0, "wait");
        start(r);
        EXPECT_TRUE(runUntil([&] { return detailsText().find("warning: marker") != std::string::npos; }));
        const auto l = jc().live();
        jc().stop();
        EXPECT_TRUE(waitIdle(10000));
        return l;
    };
    const std::string progress = progressDownload(kBase, 13, 100, 200, 1000, 40);
    auto l = liveAfter(progress + tsv({"phase", "fill", kBase}));
    ASSERT_TRUE(l.has_value());
    EXPECT_EQ(l->stage, cg::LiveStage::Checking);
    EXPECT_EQ(l->permille, 250);
    l = liveAfter(progress + tsv({"phase", "fill", kBase}) + tsv({"phase", "download", kOver}));
    EXPECT_EQ(l->stage, cg::LiveStage::Downloading);
    l = liveAfter(tsv({"phase", "export", kBase}));
    EXPECT_EQ(l->stage, cg::LiveStage::Building);
    EXPECT_EQ(l->permille, 0);
    l = liveAfter(tsv({"phase", "export", kBase}) + exportProgress(kBase, 400));
    EXPECT_EQ(l->stage, cg::LiveStage::Building);
    EXPECT_EQ(l->permille, 400);
    EXPECT_EQ(l->donePermille, 0);
    l = liveAfter(exportProgress(kBase, 650)); // an export record without a phase record before it
    EXPECT_EQ(l->stage, cg::LiveStage::Building);
    EXPECT_EQ(l->permille, 650);
}

// Proof 3: a stall retry wait is RetryWait with the countdown; the bar of the run before is kept; a second
// run whose first progress is LOWER (20/200 -> 50) keeps the higher value (250).
TEST_F(JobTest, RetryWaitCountsDownAndTheBarNeverGoesDownInAJob) {
    auto r = makeRecord("20261016-100300");
    scenario(r.id, 1, "", progressDownload(kBase, 7, 100, 200, 0, 40) + kStall, 4);
    scenario(r.id, 2, "", progressDownload(kBase, 7, 20, 200, 0, 10), 0, "wait");
    start(r);
    jc_->tick_.Stop();
    ASSERT_TRUE(runUntil([&] { return jc().live()->stage == cg::LiveStage::RetryWait; }));
    EXPECT_EQ(jc().live()->retrySeconds, 60);
    EXPECT_EQ(jc().live()->permille, 250);
    tick(3);
    EXPECT_EQ(jc().live()->retrySeconds, 57);
    tick(57);
    ASSERT_TRUE(runUntil([&] { return detailsText().find("z7 20/200") != std::string::npos; }));
    ASSERT_EQ(runs(r.id), 2);
    EXPECT_EQ(jc().live()->stage, cg::LiveStage::Downloading);
    EXPECT_EQ(jc().live()->donePermille, 250);
    EXPECT_EQ(jc().live()->permille, 250);
    jc().stop();
    ASSERT_TRUE(waitIdle(10000));
}

// Proof 4: the record written at a user Stop holds Done = the bar (250), capped at 999 when both sources
// finished (1000).
TEST_F(JobTest, TheStopRecordHoldsDoneCappedAt999) {
    auto a = makeRecord("20261016-100400");
    scenario(a.id, 1, "", progressDownload(kBase, 13, 100, 200, 1000, 40), 0, "wait");
    start(a);
    ASSERT_TRUE(runUntil([&] { return jc().live()->donePermille == 250; }));
    jc().stop();
    ASSERT_TRUE(waitIdle(10000));
    EXPECT_EQ(chart(a.id).state, cg::ChartState::Interrupted);
    EXPECT_EQ(chart(a.id).done, 250);
    EXPECT_NE(chartIniText(a.id).find("\nBytes=0\nDone=250\n"), std::string::npos);

    auto b = makeRecord("20261016-100500");
    scenario(b.id, 1, "",
             progressDownload(kBase, 13, 200, 200, 1000, 40) +
                 progressDownload(kOver, 13, 200, 200, 1000, 40),
             0, "wait");
    start(b);
    ASSERT_TRUE(runUntil([&] { return jc().live()->donePermille == 1000; }));
    jc().stop();
    ASSERT_TRUE(waitIdle(10000));
    EXPECT_EQ(chart(b.id).done, 999);
}

// Proof 4: Done at the end of a job (Complete and Missing) is 1000.
TEST_F(JobTest, ADoneJobHolds1000) {
    store_->setFirstSuccessShown();
    auto a = makeRecord("20261016-100600");
    a.overlayGuid.clear();
    scenario(a.id, 1, fill(kBase, 8, 100, 0) + written(kBase, "/charts/c.mbtiles", 100, 5),
             progressDownload(kBase, 8, 50, 100, 0, 1), 0);
    start(a);
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(chart(a.id).state, cg::ChartState::Complete);
    EXPECT_EQ(chart(a.id).done, 1000);

    auto b = makeRecord("20261016-100700");
    b.overlayGuid.clear();
    scenario(b.id, 1, fill(kBase, 8, 100, 3) + written(kBase, "/charts/d.mbtiles", 100, 5),
             progressDownload(kBase, 8, 50, 100, 0, 1), 0);
    start(b);
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(chart(b.id).state, cg::ChartState::Missing);
    EXPECT_EQ(chart(b.id).done, 1000);
}

// Proof 4: a build from saved tiles leaves the stored Done as it is, also when the build ends Complete.
TEST_F(JobTest, ABuildFromSavedLeavesDoneUnchanged) {
    store_->setFirstSuccessShown();
    auto r = makeRecord("20261016-100800");
    r.overlayGuid.clear();
    r.state = cg::ChartState::Failed;
    r.done = 280;
    store_->saveChart(r);
    scenario(r.id, 1, written(kBase, "/charts/e.mbtiles", 100, 5), "", 0);
    start(r, cg::RunKind::BuildFromSaved);
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(chart(r.id).state, cg::ChartState::Complete);
    EXPECT_EQ(chart(r.id).done, 280);
}

// Proof 4: a Resume (state Interrupted, Download) starts at the stored Done even when the child's first
// progress is lower (20/200 -> 50 against 640); Top up (Complete) starts at 0 (pulse).
TEST_F(JobTest, AResumeStartsAtTheStoredDoneAndATopUpAtZero) {
    auto r = makeRecord("20261016-100900");
    r.state = cg::ChartState::Interrupted;
    r.done = 640;
    store_->saveChart(r);
    scenario(r.id, 1, "", progressDownload(kBase, 13, 20, 200, 0, 5), 0, "wait");
    start(r);
    EXPECT_EQ(jc().live()->donePermille, 640);
    EXPECT_EQ(jc().live()->permille, 640);
    ASSERT_TRUE(runUntil([&] { return detailsText().find("z13 20/200") != std::string::npos; }));
    EXPECT_EQ(jc().live()->stage, cg::LiveStage::Downloading);
    EXPECT_EQ(jc().live()->permille, 640);
    jc().stop();
    ASSERT_TRUE(waitIdle(10000));
    EXPECT_EQ(chart(r.id).done, 640);

    auto t = makeRecord("20261016-101000");
    t.state = cg::ChartState::Complete;
    t.done = 1000;
    store_->saveChart(t);
    scenario(t.id, 1, "", "", 0, "wait");
    start(t);
    EXPECT_EQ(jc().live()->donePermille, 0);
    EXPECT_EQ(jc().live()->permille, -1);
    EXPECT_EQ(chart(t.id).done, 0);
    jc().stop();
    ASSERT_TRUE(waitIdle(10000));
}

// Proof 4: the quiet save: the 60th tick of a running job writes the current Done to chart.ini and does NOT
// call onChartsChanged; the 59th did not.
TEST_F(JobTest, TheSixtiethTickSavesDoneQuietly) {
    auto r = makeRecord("20261016-101100");
    scenario(r.id, 1, "", progressDownload(kBase, 13, 100, 200, 1000, 40), 0, "wait");
    start(r);
    jc_->tick_.Stop();
    ASSERT_TRUE(runUntil([&] { return jc().live()->donePermille == 250; }));
    const int changes = changes_;
    ASSERT_EQ(chart(r.id).done, 0);
    tick(59);
    EXPECT_EQ(chart(r.id).done, 0);
    tick(1);
    EXPECT_EQ(chart(r.id).done, 250);
    EXPECT_EQ(chart(r.id).state, cg::ChartState::Running);
    EXPECT_EQ(changes_, changes);
    jc().stop();
    ASSERT_TRUE(waitIdle(10000));
}

// ---- the weather follow-up, the picture's time, the snapshot failure text.
// The weather records are the captured lines of the pinned CLI 2026.10.8 (tests/fixtures/cli-2026.10.8/, read
// from the files, `$SCRATCH` replaced by /scr); only the first job's lines are made by hand (a normal chart).

namespace {
const std::string kRain = "{1A696D82-F258-41A4-BCD6-7648D9E08DE2}";
const std::string kRainName = "RainViewer radar (worldwide, current)";

std::string fixtureWeather(const std::string& name) {
    std::string t = slurp(fs::path(CG_FIXTURES_DIR) / "cli-2026.10.8" / name);
    EXPECT_FALSE(t.empty()) << name;
    for (std::size_t at; (at = t.find("$SCRATCH")) != std::string::npos;)
        t.replace(at, 8, "/scr");
    return t;
}
} // namespace

class WeatherTest : public JobTest {
protected:
    // The overlay-only weather chart of step 7a (a new chart: state Running).
    cg::ChartRecord weatherRecord(const std::string& id) {
        cg::ChartRecord r = makeRecord(id);
        r.sourceGuid.clear();
        r.overlayGuid = kRain;
        r.sourceName = kRainName;
        r.name = "North Sea - Weather";
        r.weather = true;
        r.files = {"/planned/North Sea - Weather.mbtiles"};
        return r;
    }
    // A normal chart that ends Done with a `warning:` line, then the weather chart queued behind it.
    cg::ChartRecord firstChart(const std::string& id) {
        auto r = makeRecord(id);
        r.overlayGuid.clear();
        scenario(r.id, 1, written(kBase, "/charts/" + kName + ".mbtiles", 4, 900) + fill(kBase, 8, 4, 0),
                 "warning: the tiles are slow\n", 0);
        return r;
    }
    cg::ChartRecord queuedWeather(const std::string& id, int code = 0) {
        auto w = weatherRecord(id);
        w.state = cg::ChartState::Interrupted; // what the page saved before the first job
        store_->saveChart(w);
        w.state = cg::ChartState::Running;
        scenario(id, 1, fixtureWeather("run-weather-layer.stdout"),
                 fixtureWeather("run-weather-layer.stderr"), code);
        return w;
    }
    static std::string firstHeadline() { return "Chart added: " + kName + "."; }
};

// Proofs 1 and 6: the first job's result is not shown; the follow-up runs `run --overwrite` (no --replace)
// with its own job.ini; one headline of both, the follow-up's buttons, the first job's `warning:` line in
// Details; each job registers its own files; the picture's time is a Details line and in the follow-up's
// chart.ini.
TEST_F(WeatherTest, AChartDoneStartsTheFollowUpAndTheResultsJoin) {
    store_->setFirstSuccessShown();
    auto first = firstChart("20261016-200000");
    auto next = queuedWeather("20261016-200001");
    start(first);
    jc().queueAfter(next);
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(runs(first.id), 1);
    ASSERT_EQ(runs(next.id), 1);
    EXPECT_EQ(argsOf(next.id, 1),
              (std::vector<std::string>{"run", "--job", iniOf(next.id), "--format=tsv", "--progress=lines",
                                        "--cache", defaultCache(), "--overwrite", "--stop-on-stdin"}));
    EXPECT_EQ(stripText(), firstHeadline() + " Chart added: North Sea - Weather.");
    EXPECT_EQ(buttons(), (std::vector<std::string>{"Show", "Close"}));
    const std::string d = detailsText();
    const std::size_t warn = d.find("warning: the tiles are slow\n");
    const std::size_t pic = d.find("Weather picture: 2026-10-21 17:40 UTC\n");
    ASSERT_NE(warn, std::string::npos) << d;
    ASSERT_NE(pic, std::string::npos) << d;
    EXPECT_LT(warn, pic);
    EXPECT_EQ(chart(first.id).state, cg::ChartState::Complete);
    EXPECT_EQ(chart(next.id).state, cg::ChartState::Complete);
    EXPECT_EQ(chart(next.id).snapshot, "2026-10-21T17:40:00Z");
    EXPECT_EQ(ocpn_fake::updates.size(), 2u);
    EXPECT_EQ(busyCalls_, (std::vector<bool>{true, false, true, false}));
}

// Proof 2: the sailor's Stop, the budget stop, a force stop and the closing of the plugin start nothing: the
// first job's own result shows and the saved follow-up stays as the page left it.
TEST_F(WeatherTest, AJobEndedByTheSailorOrByForceStartsNoFollowUp) {
    store_->setFirstSuccessShown();
    struct Row {
        const char* name;
        std::string out, err;
        int code;
        const char* mode;
        bool slow;
        std::string headline;
    };
    const std::vector<Row> rows = {
        {"stop", "", tsv({"phase", "fill", kBase}), 0, "wait", false,
         "Stopped. Resume any time from My charts."},
        {"budget", "", progressDownload(kBase, 13, 1, 9, 1000001, 1), 0, "wait", true,
         "Stopped at your data limit (1 MB). Resume to continue."},
        {"force", "", "", 0, "selfkill", false,
         "Stopped by force. Resume continues from the last save (up to 5 minutes lost)."},
    };
    int i = 0;
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        setSettings([&](cg::Settings& s) {
            s.slowConnection = row.slow;
            s.dataBudgetMb = 1;
        });
        auto first = makeRecord(std::string("20261016-20100") + std::to_string(i));
        scenario(first.id, 1, row.out, row.err, row.code, row.mode);
        const auto next = queuedWeather(std::string("20261016-20110") + std::to_string(i++));
        start(first);
        jc().queueAfter(next);
        if (std::string(row.name) == "stop") {
            ASSERT_TRUE(waitTextStarts(kSource + ": checking")); // the child has its trap by now
            jc().stop();
        }
        ASSERT_TRUE(waitIdle(10000));
        EXPECT_EQ(stripText(), row.headline);
        EXPECT_EQ(runs(next.id), 0);
        EXPECT_EQ(chart(next.id).state, cg::ChartState::Interrupted);
        EXPECT_EQ(busyCalls_.size() % 2, 0u);
    }
    // closing: the controller goes away while the first job runs
    auto first = makeRecord("20261016-201500");
    scenario(first.id, 1, "", "", 0, "wait");
    const auto next = queuedWeather("20261016-201501");
    start(first);
    jc().queueAfter(next);
    ASSERT_TRUE(waitTextStarts(kSource));
    jc_.reset();
    EXPECT_EQ(runs(next.id), 0);
    EXPECT_EQ(chart(next.id).state, cg::ChartState::Interrupted);
    EXPECT_EQ(chart(first.id).state, cg::ChartState::Interrupted);
}

// Proof 3: a Refused first job (its `error:` line stays in Details) and a Stalled one after the three retries
// still start the follow-up; the headline shows both.
TEST_F(WeatherTest, ARefusedOrStalledFirstJobStillStartsTheFollowUp) {
    store_->setFirstSuccessShown();
    auto refused = makeRecord("20261016-202000");
    scenario(refused.id, 1, "", "error: the area holds no tile at the chosen zooms\n", 3);
    auto next = queuedWeather("20261016-202001");
    start(refused);
    jc().queueAfter(next);
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(stripText(), "The area has no tiles at the chosen detail. Chart added: North Sea - Weather.");
    EXPECT_EQ(buttons(), (std::vector<std::string>{"Show", "Close"}));
    EXPECT_NE(detailsText().find("error: the area holds no tile at the chosen zooms\n"), std::string::npos);
    EXPECT_EQ(chart(refused.id).state, cg::ChartState::Failed);
    EXPECT_EQ(runs(next.id), 1);

    auto stalled = makeRecord("20261016-202100");
    for (int n = 1; n <= 4; ++n)
        scenario(stalled.id, n, "", kStall, 4);
    auto next2 = queuedWeather("20261016-202101");
    start(stalled);
    jc().queueAfter(next2);
    jc_->tick_.Stop();
    for (int attempt = 1; attempt <= 3; ++attempt) {
        ASSERT_TRUE(
            waitText(std::string("No connection. Retry in 60 s (") + std::to_string(attempt) + " of 3)"))
            << stripText();
        EXPECT_EQ(runs(next2.id), 0);
        tick(60);
        ASSERT_TRUE(runUntil([&] { return runs(stalled.id) == attempt + 1; }));
    }
    ASSERT_TRUE(runUntil([&] { return runs(next2.id) == 1; }));
    jc_->tick_.Stop();
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(stripText(),
              "No connection. Download paused; your progress is saved. Chart added: North Sea - Weather.");
    EXPECT_EQ(chart(stalled.id).state, cg::ChartState::Interrupted);
}

// Proof 4: a follow-up that cannot start (no job.ini; no program) still shows the first headline above its
// own.
TEST_F(WeatherTest, AFollowUpThatCannotStartShowsTheJoinedHeadline) {
    store_->setFirstSuccessShown();
    auto first = firstChart("20261016-203000");
    auto noIni = weatherRecord("20261016-203001");
    std::error_code ec;
    fs::remove(jobDir(noIni.id) / "job.ini", ec);
    start(first);
    jc().queueAfter(noIni);
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(stripText(),
              firstHeadline() +
                  " The download settings of this chart are missing. Delete the chart and make it again.");
    EXPECT_EQ(buttons(), std::vector<std::string>{"Close"});
    EXPECT_EQ(chart(noIni.id).state, cg::ChartState::Failed);
    EXPECT_NE(detailsText().find("warning: the tiles are slow\n"), std::string::npos) << detailsText();

    auto first2 = firstChart("20261016-203100");
    auto next2 = queuedWeather("20261016-203101");
    start(first2);
    jc().queueAfter(next2);
    setSettings(
        [&](cg::Settings& s) { s.cliPath = (root_ / "nowhere" / "cli").string(); }); // read fresh by start
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(stripText().rfind(firstHeadline() + " Chart Grabber CLI was not found.", 0), 0u) << stripText();
    EXPECT_EQ(buttons(), (std::vector<std::string>{"Preferences...", "Close"}));
    EXPECT_EQ(chart(next2.id).state, cg::ChartState::Interrupted);
    EXPECT_EQ(runs(next2.id), 0);
}

// Proof 5: queueAfter while idle is ignored; a second call replaces the first.
TEST_F(WeatherTest, QueueAfterNeedsABusyJobAndTheLastCallWins) {
    store_->setFirstSuccessShown();
    const auto a = queuedWeather("20261016-204001");
    const auto b = queuedWeather("20261016-204002");
    jc().queueAfter(a); // idle: dropped
    auto first = firstChart("20261016-204000");
    start(first);
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(runs(a.id), 0);
    EXPECT_EQ(stripText(), firstHeadline());

    auto second = firstChart("20261016-204003");
    start(second);
    jc().queueAfter(a);
    jc().queueAfter(b);
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(runs(a.id), 0);
    EXPECT_EQ(runs(b.id), 1);
    EXPECT_EQ(chart(a.id).state, cg::ChartState::Interrupted);
}

// Proof 6: the very first success: the joined result opens Details and the hint is there once.
TEST_F(WeatherTest, TheFirstSuccessHintOfTheFirstJobOpensTheJoinedDetailsOnce) {
    auto first = firstChart("20261016-205000");
    auto next = queuedWeather("20261016-205001");
    ASSERT_FALSE(store_->firstSuccessShown());
    start(first);
    jc().queueAfter(next);
    ASSERT_TRUE(waitIdle());
    ASSERT_NE(strip(), nullptr);
    std::vector<wxTextCtrl*> texts;
    collect(strip(), texts);
    ASSERT_EQ(texts.size(), 1u);
    EXPECT_TRUE(texts.front()->IsShown());
    const std::string d = detailsText();
    EXPECT_EQ(d.rfind("If the chart is not visible", 0), 0u) << d;
    EXPECT_EQ(d.find("If the chart is not visible", 1), std::string::npos);
    EXPECT_NE(d.find("Weather picture: 2026-10-21 17:40 UTC"), std::string::npos);
    EXPECT_TRUE(store_->firstSuccessShown());
}

// The first-ever chart ends Refused, its weather chart Done: the refusal line stays readable, the hint
// follows it once, and the Details are open.
TEST_F(WeatherTest, ARefusedFirstEverChartKeepsItsErrorLineAboveTheHint) {
    auto refused = makeRecord("20261016-205100");
    scenario(refused.id, 1, "", "error: the area holds no tile at the chosen zooms\n", 3);
    auto next = queuedWeather("20261016-205101");
    ASSERT_FALSE(store_->firstSuccessShown());
    start(refused);
    jc().queueAfter(next);
    ASSERT_TRUE(waitIdle());
    std::vector<wxTextCtrl*> texts;
    collect(strip(), texts);
    ASSERT_EQ(texts.size(), 1u);
    EXPECT_TRUE(texts.front()->IsShown());
    const std::string d = detailsText();
    const std::size_t err = d.find("error: the area holds no tile at the chosen zooms\n");
    const std::size_t hint = d.find("If the chart is not visible");
    ASSERT_NE(err, std::string::npos) << d;
    ASSERT_NE(hint, std::string::npos) << d;
    EXPECT_LT(err, hint);
    EXPECT_EQ(d.find("If the chart is not visible", hint + 1), std::string::npos);
}

// Proof 7: the picture's time from the real records.
TEST_F(WeatherTest, TheSnapshotOfTheWrittenRecordIsKeptAndShownInDetails) {
    store_->setFirstSuccessShown();
    auto layer = weatherRecord("20261016-206000");
    scenario(layer.id, 1, fixtureWeather("run-weather-layer.stdout"),
             fixtureWeather("run-weather-layer.stderr"), 0);
    start(layer);
    ASSERT_TRUE(waitIdle());
    EXPECT_NE(detailsText().find("Weather picture: 2026-10-21 17:40 UTC\n"), std::string::npos)
        << detailsText();
    EXPECT_NE(chartIniText(layer.id).find("\nSnapshot=2026-10-21T17:40:00Z\n"), std::string::npos);

    auto base = makeRecord("20261016-206100"); // a weather base with seamarks: the overlay's field 5 is empty
    base.sourceGuid = "{FE84A175-2C0F-420F-86DF-5900DE16A33C}";
    base.overlayGuid = "{AB8A8685-AEC6-4E16-80DA-DC4AC37E4777}";
    base.weather = true;
    scenario(base.id, 1, fixtureWeather("run-weather-base-seamarks.stdout"),
             fixtureWeather("run-weather-base-seamarks.stderr"), 0);
    start(base);
    ASSERT_TRUE(waitIdle());
    EXPECT_NE(chartIniText(base.id).find("\nSnapshot=2026-10-21T17:10:00Z\n"), std::string::npos)
        << chartIniText(base.id);
    EXPECT_NE(detailsText().find("Weather picture: 2026-10-21 17:10 UTC\n"), std::string::npos);

    auto none = weatherRecord("20261016-206200"); // a run with no `written` record keeps the previous time
    none.snapshot = "2026-10-01T00:00:00Z";
    scenario(none.id, 1, "", "", 0);
    start(none);
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(chart(none.id).snapshot, "2026-10-01T00:00:00Z");
}

// Proof 8: exit 4 with the real snapshot line.
TEST_F(WeatherTest, ASnapshotFailureGivesItsHeadlineAndResume) {
    auto r = weatherRecord("20261016-207000");
    const std::string line = fixtureWeather("run-weather-snapshot-failed.stderr");
    scenario(r.id, 1, "", line, 4);
    start(r);
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(stripText(), "Could not get the latest weather picture. Try again later.");
    EXPECT_EQ(chart(r.id).state, cg::ChartState::Interrupted);
    EXPECT_EQ(buttons(), (std::vector<std::string>{"Resume", "Close"}));
    EXPECT_NE(detailsText().find(line), std::string::npos) << detailsText();

    auto other = weatherRecord("20261016-207100"); // any other last error keeps the landed headline
    scenario(other.id, 1, "", "error: 12 tiles failed\n", 4);
    start(other);
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(stripText(), "The download did not complete; your progress is saved.");
}

// Proof 9: an overlay-only record names the source, never "Seamarks", and the export line names the chart.
TEST_F(WeatherTest, AnOverlayOnlyRecordNamesItsSourceAndItsChart) {
    auto r = weatherRecord("20261016-208000");
    scenario(r.id, 1, "",
             tsv({"phase", "fill", kRain}) + progressDownload(kRain, 7, 3, 10, 2000000, 6) +
                 tsv({"progress", "export", kRain, "455"}),
             0, "wait");
    start(r);
    ASSERT_TRUE(waitText("Building the chart North Sea - Weather: 45 %")) << stripText();
    EXPECT_NE(detailsText().find(kRainName + ": z7 3/10"), std::string::npos) << detailsText();
    jc().stop();
    ASSERT_TRUE(waitIdle(10000));

    auto q = weatherRecord("20261016-208100");
    scenario(q.id, 1, "", tsv({"phase", "fill", kRain}), 0, "wait");
    start(q);
    ASSERT_TRUE(waitText(kRainName + ": checking the saved tiles...")) << stripText();
    jc().stop();
    ASSERT_TRUE(waitIdle(10000));
    auto p = weatherRecord("20261016-208200");
    scenario(p.id, 1, "", progressDownload(kRain, 7, 3, 10, 2000000, 6), 0, "wait");
    start(p);
    ASSERT_TRUE(waitText(kRainName + ", zoom 7: 30 %  2.0 MB  about 14 s left")) << stripText();
    jc().stop();
    ASSERT_TRUE(waitIdle(10000));
}

// Proof 10: a weather chart is never updated by date, its Resume starts the bar at 0, a failed export offers
// Resume.
TEST_F(WeatherTest, AWeatherChartResumesAsADownloadFromZero) {
    cg::ChartRecord w = weatherRecord("20261016-209000");
    w.updateBefore = "2026-09-16T10:00:00";
    cg::ChartRecord n = makeRecord("20261016-209001");
    n.updateBefore = "2026-09-16T10:00:00";
    EXPECT_EQ(JobController::resumeKind(w), cg::RunKind::Download);
    EXPECT_EQ(JobController::resumeKind(n), cg::RunKind::Update);

    w.state = cg::ChartState::Interrupted;
    w.done = 600;
    store_->saveChart(w);
    scenario(w.id, 1, "", "", 0, "wait");
    start(w, JobController::resumeKind(w));
    EXPECT_EQ(jc().live()->donePermille, 0);
    EXPECT_EQ(chart(w.id).done, 0);
    jc().stop();
    ASSERT_TRUE(waitIdle(10000));

    auto x = weatherRecord("20261016-209100");
    scenario(x.id, 1, "", "error: disk full\n", 5);
    start(x);
    ASSERT_TRUE(waitIdle());
    EXPECT_EQ(stripText(), "The chart could not be written. Nothing was added.");
    EXPECT_EQ(buttons(), (std::vector<std::string>{"Resume", "Close"}));
    scenario(x.id, 2, "", "", 0, "wait");
    ASSERT_TRUE(click("Resume"));
    ASSERT_TRUE(runUntil([&] { return runs(x.id) == 2; }));
    EXPECT_EQ(argsOf(x.id, 2).front(), "run");
    jc().stop();
    ASSERT_TRUE(waitIdle(10000));
}

} // namespace
