// SPDX-License-Identifier: GPL-3.0-or-later
// : the Settings dialog in one column, every folder and the Chart
// Grabber CLI as a visible read-only field with "Change..." and "Use default", saved tiles, status lines.
// A window test (cg_gui_tests). "Change..." is always replaced by a script (no modal picker); the Clear...
// confirmation is a modeless Question answered by button events (question_probe.h).
#include "config_store.h"
#include "job_files.h"
#include "look.h"
#include "ocpn_fakes.h"
#include "plugin_log.h"
#include "question_probe.h"
#include "settings_dialog.h"
#include "sources_stub.h"
#include "wx_env.h"

#include <gtest/gtest.h>

#include <wx/app.h>
#include <wx/apptrait.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/collpane.h>
#include <wx/event.h>
#include <wx/evtloop.h>
#include <wx/fileconf.h>
#include <wx/log.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <gtk/gtk.h>
#include <memory>
#include <optional>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include <ocpn_plugin.h>

namespace {

namespace fs = std::filesystem;
using Kind = SettingsDialog::PathKind;

std::string u(const wxString& s) {
    return s.utf8_string();
}

// The four path rows: control name prefix, kind and the settings key.
struct PathCase {
    const char* name;
    Kind kind;
    std::string cg::Settings::* key;
};
const PathCase kPathCases[] = {
    {"sources", Kind::Sources, &cg::Settings::mapsDir},
    {"charts", Kind::Charts, &cg::Settings::chartDir},
    {"cache", Kind::Cache, &cg::Settings::cacheDir},
    {"cli", Kind::Cli, &cg::Settings::cliPath},
};

class SettingsPage : public ::testing::Test {
protected:
    void SetUp() override {
        if (!wxTheApp || !wxTheApp->IsGUI())
            GTEST_SKIP() << "no display: the settings dialog test was NOT run";
        char tmpl[] = "/tmp/cgset-XXXXXX";
        ASSERT_NE(mkdtemp(tmpl), nullptr);
        root_ = tmpl;
        home_ = root_ / "home";
        fs::create_directories(home_);
        oldHome_ = getenv("HOME") ? getenv("HOME") : "";
        setenv("HOME", home_.c_str(), 1);
        ocpn_fake::privateData = wxString::FromUTF8((root_ / "private").string());
        ocpn_fake::touch = false;
        ocpn_fake::scale = 1.0;
        config_ = std::make_unique<wxFileConfig>(wxString("cgsettest"), wxEmptyString,
                                                 wxString::FromUTF8((root_ / "opencpn.conf").string()),
                                                 wxEmptyString, wxCONFIG_USE_LOCAL_FILE);
        store_ = std::make_unique<ConfigStore>(config_.get());
        bundled_ = root_ / "bundled d'x";
        fs::create_directories(bundled_);
        std::ofstream(bundled_ / "a.zmp") << "x";
        store_->setBundledSourcesDir(bundled_.string());
        fs::create_directories(root_ / "emptybin");
        fs::create_directories(root_ / "bin");
        standIn_ = (root_ / "bin" / "chart-grabber-cli").string();
        std::ofstream(standIn_) << "#!/bin/sh\n";
        chmod(standIn_.c_str(), 0755);
    }
    void TearDown() override {
        if (!store_)
            return;
        dlg_.reset();
        cg::closePluginLog();
        store_.reset();
        config_.reset();
        ocpn_fake::touch = false;
        setenv("HOME", oldHome_.c_str(), 1);
        std::error_code ec;
        fs::permissions(root_ / "ro", fs::perms::owner_all, ec);
        fs::remove_all(root_, ec);
    }

    // A dialog whose "Change..." answers with `next_` (and records the call) and whose CLI lookup sees only
    // the temp folders: nothing of the host's /usr/bin or PATH.
    SettingsDialog& open(bool busy = false) {
        dlg_ = std::make_unique<SettingsDialog>(nullptr, *store_, look_, [busy] { return busy; });
        dlg_->choosePath = [this](Kind kind, const wxString& current) -> std::optional<wxString> {
            asked_.emplace_back(kind, u(current));
            return next_;
        };
        dlg_->openFolder = [this](const wxString& f) { opened_.push_back(u(f)); };
        dlg_->cliDefaultPaths = {(root_ / "no-such" / "chart-grabber-cli").string()};
        dlg_->cliPathEnv = (root_ / "emptybin").string();
        return *dlg_;
    }
    template <class T> T* find(const std::string& name) {
        return dynamic_cast<T*>(wxWindow::FindWindowByName(name, dlg_.get()));
    }
    void click(const std::string& name) {
        auto* b = find<wxButton>(name);
        ASSERT_NE(b, nullptr) << name;
        click(b);
    }
    static void click(wxWindow* b) {
        wxCommandEvent e(wxEVT_BUTTON, b->GetId());
        e.SetEventObject(b);
        b->GetEventHandler()->ProcessEvent(e);
    }
    // Change... answering `path` (nullopt = Cancel of the picker).
    void change(const std::string& prefix, std::optional<std::string> path) {
        next_ = path ? std::optional<wxString>(wxString::FromUTF8(*path)) : std::nullopt;
        click(prefix + "Change");
    }
    std::string field(const std::string& prefix) { return u(find<wxTextCtrl>(prefix + "Field")->GetValue()); }
    bool defaultEnabled(const std::string& prefix) { return find<wxButton>(prefix + "Default")->IsEnabled(); }
    std::string label(const std::string& name) { return u(find<wxStaticText>(name)->GetLabel()); }

    // Shows the dialog modally; a timer inside the modal loop runs `before`, then presses `id`.
    int closeWith(int id, std::function<void()> before = {}) {
        wxEvtHandler handler;
        wxTimer timer(&handler);
        handler.Bind(wxEVT_TIMER, [&](wxTimerEvent&) {
            timer.Stop();
            if (before)
                before();
            if (dlg_->IsModal()) // `before` may have closed it (Escape)
                click(dlg_->FindWindow(id));
        });
        timer.Start(30, wxTIMER_ONE_SHOT);
        return dlg_->ShowModal();
    }
    std::string home(const std::string& rest) const { return home_.string() + rest; }

    fs::path root_, home_, bundled_;
    std::string oldHome_, standIn_;
    std::unique_ptr<wxFileConfig> config_;
    std::unique_ptr<ConfigStore> store_;
    std::unique_ptr<SettingsDialog> dlg_;
    Look look_;
    std::optional<wxString> next_;
    std::vector<std::pair<Kind, std::string>> asked_;
    std::vector<std::string> opened_;
};

// Proof 1: Change twice, Use default, choosing the default itself; "Use default" is enabled exactly when the
// pending key is not empty. The user's finding 2026-10-16 (a second choice never took) is the first part.
TEST_F(SettingsPage, ChangeTwiceThenUseDefaultForEveryPathRow) {
    for (const PathCase& c : kPathCases) {
        SCOPED_TRACE(c.name);
        const std::string defaultText = c.kind == Kind::Sources  ? home("/Charts/Chart sources")
                                        : c.kind == Kind::Charts ? home("/Charts/ChartGrabber")
                                        : c.kind == Kind::Cache
                                            ? (root_ / "private").string() + "/plugins/chartgrabber/cache"
                                            : standIn_;
        const std::string a = "/pick/a d'\xc3\xa9", b = "/pick/b";
        {
            SettingsDialog& dlg = open();
            dlg.cliDefaultPaths = {standIn_}; // the automatic program of this row
            EXPECT_FALSE(defaultEnabled(c.name)) << "nothing chosen yet";
            change(c.name, a);
            change(c.name, b);
            EXPECT_EQ(field(c.name), b);
            EXPECT_TRUE(defaultEnabled(c.name));
            ASSERT_EQ(asked_.size(), 2u);
            EXPECT_EQ(asked_[1].second, a) << "the picker is opened at the current path";
            change(c.name, std::nullopt); // Cancel of the picker changes nothing
            EXPECT_EQ(field(c.name), b);
            EXPECT_EQ(closeWith(wxID_OK), wxID_OK);
            EXPECT_EQ(store_->loadSettings().*c.key, b);
        }
        {
            SettingsDialog& dlg = open();
            dlg.cliDefaultPaths = {standIn_};
            EXPECT_EQ(field(c.name), b);
            EXPECT_TRUE(defaultEnabled(c.name));
            click(std::string(c.name) + "Default");
            EXPECT_EQ(field(c.name), defaultText);
            EXPECT_FALSE(defaultEnabled(c.name));
            EXPECT_EQ(closeWith(wxID_OK), wxID_OK);
            EXPECT_EQ(store_->loadSettings().*c.key, "");
            (void)dlg;
        }
        {
            SettingsDialog& dlg = open();
            dlg.cliDefaultPaths = {standIn_};
            change(c.name, b);
            EXPECT_TRUE(defaultEnabled(c.name));
            change(c.name, defaultText + "/"); // the default itself, one trailing '/' ignored
            EXPECT_FALSE(defaultEnabled(c.name)) << "a chosen path equal to the default is the empty key";
            EXPECT_EQ(closeWith(wxID_OK), wxID_OK);
            EXPECT_EQ(store_->loadSettings().*c.key, "");
            (void)dlg;
        }
        asked_.clear();
    }
}

// The default "Change..." goes to the platform's selectors (api/ocpn_plugin.h:1259, :1263), faked here.
TEST_F(SettingsPage, TheDefaultPickerCallsTheDirAndFileSelectors) {
    SettingsDialog fresh(nullptr, *store_, look_);
    ocpn_fake::dirResult = wxID_OK;
    ocpn_fake::dirSelected = "/chosen/dir";
    const auto dir = fresh.choosePath(Kind::Charts, "/start/here");
    ASSERT_TRUE(dir);
    EXPECT_EQ(u(*dir), "/chosen/dir");
    EXPECT_EQ(u(ocpn_fake::dirInit), "/start/here");
    EXPECT_EQ(u(ocpn_fake::dirTitle), "Choose the folder for new charts");
    ocpn_fake::fileResult = wxID_OK;
    ocpn_fake::fileSelected = "/chosen/prog";
    const auto file = fresh.choosePath(Kind::Cli, "/usr/bin/chart-grabber-cli");
    ASSERT_TRUE(file);
    EXPECT_EQ(u(*file), "/chosen/prog");
    EXPECT_EQ(u(ocpn_fake::fileInit), "/usr/bin") << "the folder of the current program";
    EXPECT_EQ(u(ocpn_fake::fileTitle), "Choose Chart Grabber CLI");
    EXPECT_EQ(u(ocpn_fake::fileWildcard), "*");
    ocpn_fake::dirResult = wxID_CANCEL;
    EXPECT_FALSE(fresh.choosePath(Kind::Sources, "/x"));
    ocpn_fake::fileResult = wxID_CANCEL;
    EXPECT_FALSE(fresh.choosePath(Kind::Cli, "/x"));
}

// Proof 2: the new value reaches the next run's argv through cliSettings(loadSettings).
TEST_F(SettingsPage, TheChosenSourcesFolderIsTheMapsArgumentOfTheNextRun) {
    const fs::path folder = root_ / "my sources d'x";
    fs::create_directories(folder / "one" / "two");
    std::ofstream(folder / "one" / "two" / "Local.zmp") << "x";
    auto argvs = [&] {
        const cg::Settings s = store_->cliSettings(store_->loadSettings());
        return std::vector<std::vector<std::string>>{
            cg::sourcesArguments(s), cg::estimateArguments(cg::JobSpec{}, s),
            cg::runArguments("/j/job.ini", cg::RunKind::Download, false, s)};
    };
    auto maps = [](const std::vector<std::string>& v) {
        const auto it =
            std::find(v.rbegin(), v.rend(), "--maps"); // the LAST --maps: the sailor's folder wins
        return it == v.rend() ? std::string("<none>") : (it == v.rbegin() ? "<last>" : *(it - 1));
    };
    open();
    change("sources", folder.string());
    EXPECT_EQ(closeWith(wxID_OK), wxID_OK);
    for (const auto& v : argvs())
        EXPECT_EQ(maps(v), folder.string()) << v.front();
    open();
    click("sourcesDefault"); // the default folder does not exist: no source in it, only the bundled one
    EXPECT_EQ(closeWith(wxID_OK), wxID_OK);
    for (const auto& v : argvs())
        EXPECT_EQ(maps(v), bundled_.string()) << v.front();
}

// Proof 3: Cancel and Escape after changes store nothing.
TEST_F(SettingsPage, CancelAndEscapeStoreNothing) {
    cg::Settings seed;
    seed.mapsDir = "/seed/maps";
    seed.chartDir = "/seed/charts";
    seed.cacheDir = "/seed/cache";
    seed.cliPath = "/seed/cli";
    seed.dataBudgetMb = 250;
    seed.slowConnection = false;
    seed.registerCharts = false;
    store_->saveSettings(seed);
    for (const bool escape : {false, true}) {
        SCOPED_TRACE(escape ? "Escape" : "Cancel");
        open();
        const int rc = closeWith(wxID_CANCEL, [&] {
            for (const PathCase& c : kPathCases)
                change(c.name, std::string("/changed/") + std::string(c.name));
            find<wxCheckBox>("slow")->SetValue(true);
            find<wxCheckBox>("register")->SetValue(true);
            find<wxCheckBox>("warnDetailLevels")->SetValue(false);
            find<wxCheckBox>("stripOnChart")->SetValue(true);
            find<wxSpinCtrl>("limit")->SetValue(999);
            if (escape) {
                wxKeyEvent key(wxEVT_CHAR_HOOK);
                key.m_keyCode = WXK_ESCAPE;
                key.SetEventObject(dlg_.get());
                dlg_->GetEventHandler()->ProcessEvent(key);
                EXPECT_FALSE(dlg_->IsModal()) << "Escape closes the dialog by itself";
            }
        });
        EXPECT_EQ(rc, wxID_CANCEL);
        const cg::Settings now = store_->loadSettings();
        EXPECT_EQ(now.mapsDir, "/seed/maps");
        EXPECT_EQ(now.chartDir, "/seed/charts");
        EXPECT_EQ(now.cacheDir, "/seed/cache");
        EXPECT_EQ(now.cliPath, "/seed/cli");
        EXPECT_EQ(now.dataBudgetMb, 250);
        EXPECT_FALSE(now.slowConnection);
        EXPECT_FALSE(now.registerCharts);
        EXPECT_FALSE(now.stripOnChart);
        EXPECT_TRUE(now.warnDetailLevels);
    }
}

// the warning check box is ticked by default, carries its label, and OK stores the tick.
TEST_F(SettingsPage, TheDetailWarningCheckBoxIsOnByDefaultAndOkStoresIt) {
    open();
    wxCheckBox* box = find<wxCheckBox>("warnDetailLevels");
    EXPECT_EQ(box->GetLabel(), "Warn before very large or very small detail choices");
    EXPECT_TRUE(box->GetValue());
    EXPECT_EQ(closeWith(wxID_OK), wxID_OK);
    EXPECT_TRUE(store_->loadSettings().warnDetailLevels);
    open();
    find<wxCheckBox>("warnDetailLevels")->SetValue(false);
    EXPECT_EQ(closeWith(wxID_OK), wxID_OK);
    EXPECT_FALSE(store_->loadSettings().warnDetailLevels);
    open();
    EXPECT_FALSE(find<wxCheckBox>("warnDetailLevels")->GetValue()) << "the stored value is shown";
    EXPECT_EQ(closeWith(wxID_CANCEL), wxID_CANCEL);
}

// the option is off by default, with its note under it; OK stores the tick.
TEST_F(SettingsPage, ProgressOnTheChartIsOffByDefaultWithItsNoteAndOkStoresIt) {
    open();
    auto* box = find<wxCheckBox>("stripOnChart");
    EXPECT_EQ("Show download progress on the chart", box->GetLabel());
    EXPECT_FALSE(box->GetValue());
    wxString note = find<wxStaticText>("stripNote")->GetLabelText();
    note.Replace("\n", " ");
    EXPECT_EQ(
        "Off: progress has its own small window. With OpenGL on Wayland the chart can hide progress shown "
        "on it. Applies after OpenCPN restarts. On Wayland the window cannot remember its place; the option "
        "above keeps the strip on the chart.",
        note);
    box->SetValue(true);
    EXPECT_EQ(closeWith(wxID_OK), wxID_OK);
    EXPECT_TRUE(store_->loadSettings().stripOnChart);
}

// : the Log file row and the Debug check box.
TEST_F(SettingsPage, TheLogFileRowShowsTheOpenFileAndOpenCallsTheSeamWithItsPath) {
    const std::string path = (root_ / "log d'\xD0\x96" / "chartgrabber.log").string();
    ASSERT_TRUE(cg::openPluginLog(path));
    open();
    auto* shown = find<wxTextCtrl>("logFile");
    ASSERT_NE(shown, nullptr);
    EXPECT_EQ(u(shown->GetValue()), path);
    auto* openButton = find<wxButton>("logOpen");
    ASSERT_NE(openButton, nullptr);
    EXPECT_EQ("Open", openButton->GetLabel());
    EXPECT_TRUE(openButton->IsEnabled());
    click(openButton);
    EXPECT_EQ(opened_, std::vector<std::string>{path});
    cg::closePluginLog();
}

TEST_F(SettingsPage, WithoutAFileOrWithTheFileGoneTheFieldIsEmptyOrOpenIsDisabled) {
    open();
    EXPECT_EQ(u(find<wxTextCtrl>("logFile")->GetValue()), "");
    EXPECT_FALSE(find<wxButton>("logOpen")->IsEnabled());
    dlg_.reset();
    const std::string path = (root_ / "gone" / "chartgrabber.log").string();
    ASSERT_TRUE(cg::openPluginLog(path));
    fs::remove(path);
    open();
    EXPECT_EQ(u(find<wxTextCtrl>("logFile")->GetValue()), path);
    EXPECT_FALSE(find<wxButton>("logOpen")->IsEnabled());
    cg::closePluginLog();
}

TEST_F(SettingsPage, DebugIsOffByDefaultOkStoresAndAppliesItAtOnceCancelChangesNothing) {
    const std::string path = (root_ / "dbg" / "chartgrabber.log").string();
    ASSERT_TRUE(cg::openPluginLog(path));
    const auto fileText = [&] {
        std::ifstream in(path);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
    open();
    auto* box = find<wxCheckBox>("debugLog");
    ASSERT_NE(box, nullptr);
    EXPECT_EQ("Debug: write more detail to the log file", box->GetLabel());
    EXPECT_FALSE(box->GetValue());
    box->SetValue(true);
    EXPECT_EQ(closeWith(wxID_CANCEL), wxID_CANCEL);
    EXPECT_FALSE(store_->loadSettings().debugLog);
    EXPECT_FALSE(cg::pluginLogDebug());
    EXPECT_EQ(fileText().find("debug on"), std::string::npos);

    open();
    EXPECT_FALSE(find<wxCheckBox>("debugLog")->GetValue());
    find<wxCheckBox>("debugLog")->SetValue(true);
    EXPECT_EQ(closeWith(wxID_OK), wxID_OK);
    EXPECT_TRUE(store_->loadSettings().debugLog);
    EXPECT_TRUE(cg::pluginLogDebug());
    EXPECT_NE(fileText().find(" debug on\n"), std::string::npos) << fileText();

    open(); // unchanged: OK writes no line
    EXPECT_TRUE(find<wxCheckBox>("debugLog")->GetValue());
    EXPECT_EQ(closeWith(wxID_OK), wxID_OK);
    EXPECT_EQ(fileText().find("debug off"), std::string::npos);
    EXPECT_EQ(fileText().find(" debug on\n"), fileText().rfind(" debug on\n"))
        << "OK without a change writes nothing";

    open();
    find<wxCheckBox>("debugLog")->SetValue(false);
    EXPECT_EQ(closeWith(wxID_OK), wxID_OK);
    EXPECT_FALSE(store_->loadSettings().debugLog);
    EXPECT_FALSE(cg::pluginLogDebug());
    EXPECT_NE(fileText().find(" debug off\n"), std::string::npos);
    cg::closePluginLog();
}

// OK saves the spin and the two check boxes; untouched folders stay empty.
TEST_F(SettingsPage, OkStoresTheSpinAndTheTwoCheckBoxes) {
    open();
    EXPECT_FALSE(find<wxCheckBox>("slow")->GetValue());
    EXPECT_TRUE(find<wxCheckBox>("register")->GetValue());
    find<wxSpinCtrl>("limit")->SetValue(777);
    find<wxCheckBox>("slow")->SetValue(true);
    find<wxCheckBox>("register")->SetValue(false);
    EXPECT_EQ(closeWith(wxID_OK), wxID_OK);
    const cg::Settings s = store_->loadSettings();
    EXPECT_EQ(s.dataBudgetMb, 777);
    EXPECT_TRUE(s.slowConnection);
    EXPECT_FALSE(s.registerCharts);
    EXPECT_EQ(s.mapsDir, "");
    EXPECT_EQ(s.cacheDir, "");
}

// Proof 3b: the CLI field shows the automatic program of in each case.
TEST_F(SettingsPage, TheCliFieldShowsTheAutomaticProgramOfEachCase) {
    const std::string onPath = (root_ / "pathbin" / "chart-grabber-cli").string();
    fs::create_directories(root_ / "pathbin");
    std::ofstream(onPath) << "#!/bin/sh\n";
    chmod(onPath.c_str(), 0755);
    struct Row {
        const char* name;
        std::string installed; // the /usr/bin stand-in
        std::string pathDir;   // the temp PATH
        std::string shown;
    };
    const std::string gone = (root_ / "gone" / "chart-grabber-cli").string();
    const std::vector<Row> rows = {
        {"/usr/bin stand-in wins over PATH", standIn_, (root_ / "pathbin").string(), standIn_},
        {"PATH when the installed one is absent", gone, (root_ / "pathbin").string(), onPath},
        {"neither", gone, (root_ / "emptybin").string(), ""},
    };
    for (const Row& r : rows) {
        SCOPED_TRACE(r.name);
        SettingsDialog& dlg = open();
        dlg.cliDefaultPaths = {r.installed};
        dlg.cliPathEnv = r.pathDir;
        change("cli", "/some/chosen");
        click("cliDefault");
        EXPECT_EQ(field("cli"), r.shown);
    }
    // A chosen program is shown as chosen, whatever the automatic one is.
    SettingsDialog& dlg = open();
    dlg.cliDefaultPaths = {standIn_};
    change("cli", onPath);
    EXPECT_EQ(field("cli"), onPath);
}

// Proof 6: the limit spin follows the slow check box, also live.
TEST_F(SettingsPage, TheLimitSpinIsEnabledExactlyWhenSlowIsTicked) {
    for (const bool slow : {false, true}) {
        cg::Settings s = store_->loadSettings();
        s.slowConnection = slow;
        store_->saveSettings(s);
        open();
        auto* box = find<wxCheckBox>("slow");
        auto* spin = find<wxSpinCtrl>("limit");
        EXPECT_EQ(spin->IsEnabled(), slow);
        for (const bool tick : {!slow, slow}) {
            box->SetValue(tick);
            wxCommandEvent e(wxEVT_CHECKBOX, box->GetId());
            e.SetEventObject(box);
            e.SetInt(tick);
            box->GetEventHandler()->ProcessEvent(e);
            EXPECT_EQ(spin->IsEnabled(), tick);
        }
    }
}

// Proof 8: the status line of "Save charts in".
TEST_F(SettingsPage, TheChartsStatusLineSaysWhatIsTrueOfTheFolder) {
    const fs::path writable = root_ / "charts w", missing = root_ / "charts missing", ro = root_ / "ro";
    fs::create_directories(writable);
    fs::create_directories(ro);
    fs::permissions(ro, fs::perms::owner_read | fs::perms::owner_exec);
    open();
    EXPECT_EQ(label("chartsStatus"), "The folder will be created at the first download.")
        << "the default folder under the temp home does not exist";
    change("charts", writable.string());
    const std::string ok = label("chartsStatus");
    EXPECT_NE(ok.find("B free. OpenCPN shows the charts from this folder."), std::string::npos) << ok;
    if (access(ro.c_str(), W_OK) != 0) {
        change("charts", ro.string());
        EXPECT_EQ(label("chartsStatus"), "This folder cannot be written. Choose another one.");
    } else {
        ADD_FAILURE() << "running as root: the read-only folder case was NOT run";
    }
    change("charts", missing.string());
    EXPECT_EQ(label("chartsStatus"), "The folder will be created at the first download.");
}

// Proof 9: Open folder creates the missing sources folder; the launcher is the injected hook.
TEST_F(SettingsPage, OpenFolderCreatesTheMissingSourcesFolder) {
    const fs::path def = home_ / "Charts" / "Chart sources";
    open();
    ASSERT_FALSE(fs::exists(def));
    click("sourcesOpen");
    EXPECT_TRUE(fs::is_directory(def));
    EXPECT_EQ(opened_, (std::vector<std::string>{def.string()}));
    const fs::path chosen = root_ / "my d'x" / "sub";
    change("sources", chosen.string());
    click("sourcesOpen");
    EXPECT_TRUE(fs::is_directory(chosen)) << "the folder the dialog shows, not the saved one";
    EXPECT_EQ(opened_.back(), chosen.string());
}

// Proof 7: Saved tiles.
class SavedTiles : public SettingsPage {
protected:
    // 3 000 000 bytes under cache1 (deleted by Clear...), 2 000 000 under other (kept): 5.0 MB in all.
    void makeCache(const fs::path& cache) {
        fs::create_directories(cache / "cache1" / "z1");
        fs::create_directories(cache / "keep");
        fs::create_directories(cache / "other");
        std::ofstream(cache / "cache1" / "z1" / "a.bin") << std::string(1500000, 'x');
        std::ofstream(cache / "cache1" / "b.bin") << std::string(1500000, 'x');
        std::ofstream(cache / "other" / "c.bin") << std::string(2000000, 'x');
    }
};

TEST_F(SavedTiles, TheSizeTextAndTheStateOfClear) {
    const fs::path cache = root_ / "cache d'x \xc3\xa9";
    struct Row {
        const char* name;
        bool files, busy;
        std::string text;
        bool clearEnabled;
    };
    const std::vector<Row> rows = {
        {"files", true, false, "5.0 MB of saved tiles", true},
        {"none", false, false, "No saved tiles yet.", false},
        {"files, a download runs", true, true, "5.0 MB of saved tiles (a download is running)", false},
        {"none, a download runs", false, true, "No saved tiles yet. (a download is running)", false},
    };
    for (const Row& r : rows) {
        SCOPED_TRACE(r.name);
        std::error_code ec;
        fs::remove_all(cache, ec);
        if (r.files)
            makeCache(cache);
        cg::Settings s = store_->loadSettings();
        s.cacheDir = cache.string();
        store_->saveSettings(s);
        open(r.busy);
        EXPECT_EQ(label("cacheSize"), r.text);
        EXPECT_EQ(find<wxButton>("clear")->IsEnabled(), r.clearEnabled);
    }
}

TEST_F(SavedTiles, TheSizeFollowsTheCacheFolderOfTheDialog) {
    const fs::path cache = root_ / "other cache";
    makeCache(cache);
    open();
    EXPECT_EQ(label("cacheSize"), "No saved tiles yet.");
    change("cache", cache.string());
    EXPECT_EQ(label("cacheSize"), "5.0 MB of saved tiles");
    EXPECT_TRUE(find<wxButton>("clear")->IsEnabled());
    click("cacheDefault");
    EXPECT_EQ(label("cacheSize"), "No saved tiles yet.");
    EXPECT_FALSE(find<wxButton>("clear")->IsEnabled());
}

// , 8.9: Clear... asks with a modeless Question labelled Delete / Cancel and returns at
// once; only Delete clears the cache* folders; Cancel, Escape and the close box keep everything.
TEST_F(SavedTiles, TheQuestionHasDeleteAndCancelAndOnlyDeleteClearsTheCacheFolders) {
    const fs::path cache = root_ / "cache d'x \xc3\xa9";
    makeCache(cache);
    cg::Settings s = store_->loadSettings();
    s.cacheDir = cache.string();
    store_->saveSettings(s);
    open();
    ASSERT_TRUE(qprobe::shown().empty());
    click("clear");
    ASSERT_EQ(qprobe::shown().size(), 1u) << "returned at once with the question shown";
    Question* q = qprobe::shown()[0];
    EXPECT_FALSE(q->IsModal());
    EXPECT_EQ(qprobe::text(q),
              std::string("Delete 5.0 MB of saved tiles in ") + cache.string() +
                  "?  Your charts stay. Top up and Build from saved tiles will then download "
                  "everything again.");
    EXPECT_EQ(qprobe::button(q, wxID_YES)->GetLabel(), "Delete");
    EXPECT_EQ(qprobe::button(q, wxID_NO)->GetLabel(), "Cancel");
    EXPECT_TRUE(fs::exists(cache / "cache1")) << "nothing before the answer";
    click("clear"); // a second press raises the open question
    EXPECT_EQ(qprobe::shown().size(), 1u);
    const std::vector<std::pair<const char*, std::function<void(Question*)>>> keeps = {
        {"Cancel", [](Question* x) { qprobe::press(x, wxID_NO); }},
        {"Escape", [](Question* x) { qprobe::escape(x); }},
        {"close box", [](Question* x) { x->Close(); }},
    };
    for (const auto& [name, act] : keeps) {
        SCOPED_TRACE(name);
        if (qprobe::shown().empty())
            click("clear");
        ASSERT_EQ(qprobe::shown().size(), 1u);
        act(qprobe::shown()[0]);
        EXPECT_TRUE(qprobe::shown().empty());
        EXPECT_TRUE(fs::exists(cache / "cache1")) << "nothing is deleted";
        EXPECT_EQ(label("cacheSize"), "5.0 MB of saved tiles");
    }
    click("clear");
    ASSERT_EQ(qprobe::shown().size(), 1u);
    qprobe::press(qprobe::shown()[0], wxID_YES);
    EXPECT_FALSE(fs::exists(cache / "cache1"));
    EXPECT_TRUE(fs::exists(cache / "keep"));
    EXPECT_TRUE(fs::exists(cache / "other" / "c.bin")) << "only cache* sub-folders go";
    EXPECT_EQ(label("cacheSize"), "2.0 MB of saved tiles") << "the size is refreshed";
}

//: opened by OpenCPN's plugin list (modal), Clear... asks MODALLY: the question
// joins the nested loop; Delete clears, Cancel keeps.
TEST_F(SavedTiles, WhenTheSettingsWindowIsModalTheClearQuestionIsModalToo) {
    const fs::path cache = root_ / "cache modal";
    makeCache(cache);
    cg::Settings s = store_->loadSettings();
    s.cacheDir = cache.string();
    store_->saveSettings(s);
    for (const bool yes : {false, true}) {
        SCOPED_TRACE(yes ? "Delete" : "Cancel");
        open();
        bool modalQuestion = false;
        int questions = 0;
        wxEvtHandler handler;
        wxTimer answer(&handler);
        handler.Bind(wxEVT_TIMER, [&](wxTimerEvent&) { // runs inside the question's own loop
            if (qprobe::shown().empty())
                return;
            answer.Stop();
            questions = int(qprobe::shown().size());
            modalQuestion = qprobe::shown()[0]->IsModal();
            qprobe::press(qprobe::shown()[0], yes ? wxID_YES : wxID_NO);
        });
        EXPECT_EQ(closeWith(wxID_CANCEL,
                            [&] {
                                answer.Start(20);
                                click("clear"); // returns when the question is answered
                            }),
                  wxID_CANCEL);
        EXPECT_EQ(questions, 1);
        EXPECT_TRUE(modalQuestion);
        EXPECT_EQ(fs::exists(cache / "cache1"), !yes);
        wxTheApp->ProcessIdle();
        makeCache(cache);
    }
}

// a download may start while the window is open: Clear... re-reads jobBusy when pressed and in
// the answer; busy -> no question / nothing deleted, the button disabled and the line says so.
TEST_F(SavedTiles, ADownloadThatStartsWhileTheWindowIsOpenRefusesClearAtThePressAndAtTheAnswer) {
    const fs::path cache = root_ / "cache busy";
    makeCache(cache);
    cg::Settings s = store_->loadSettings();
    s.cacheDir = cache.string();
    store_->saveSettings(s);
    bool busy = false;
    dlg_ = std::make_unique<SettingsDialog>(nullptr, *store_, look_, [&busy] { return busy; });
    dlg_->cliDefaultPaths = {(root_ / "no-such" / "chart-grabber-cli").string()};
    dlg_->cliPathEnv = (root_ / "emptybin").string();
    EXPECT_TRUE(find<wxButton>("clear")->IsEnabled());

    busy = true; // started after the window opened, before the press
    click("clear");
    EXPECT_TRUE(qprobe::shown().empty()) << "no question";
    EXPECT_FALSE(find<wxButton>("clear")->IsEnabled());
    EXPECT_EQ(label("cacheSize"), "5.0 MB of saved tiles (a download is running)");
    EXPECT_TRUE(fs::exists(cache / "cache1"));

    busy = false; // the question is open, then a download starts
    find<wxButton>("clear")->Enable(true);
    click("clear");
    ASSERT_EQ(qprobe::shown().size(), 1u);
    busy = true;
    qprobe::press(qprobe::shown()[0], wxID_YES);
    EXPECT_TRUE(fs::exists(cache / "cache1")) << "the answer arrived after the state changed";
    EXPECT_FALSE(find<wxButton>("clear")->IsEnabled());
    EXPECT_EQ(label("cacheSize"), "5.0 MB of saved tiles (a download is running)");
}

// the dialog's destructor deletes the open question at once; its callback never runs.
TEST_F(SavedTiles, DeletingTheDialogWithTheClearQuestionOpenDeletesTheQuestionAtOnce) {
    const fs::path cache = root_ / "cache gone";
    makeCache(cache);
    cg::Settings s = store_->loadSettings();
    s.cacheDir = cache.string();
    store_->saveSettings(s);
    auto* dlg = new SettingsDialog(nullptr, *store_, look_);
    dlg->cliDefaultPaths = {(root_ / "no-such" / "chart-grabber-cli").string()};
    dlg->cliPathEnv = (root_ / "emptybin").string();
    dlg_.reset(dlg);
    click("clear");
    ASSERT_EQ(qprobe::shown().size(), 1u);
    wxWeakRef<Question> q(qprobe::shown()[0]);
    // The question is a child of the dialog, so wxWindow's own child cleanup would delete it as well: this
    // pins the result (gone at once, nothing deleted), not the destructor's early delete.
    dlg_.reset();
    EXPECT_FALSE(q) << "no idle ran in between";
    wxTheApp->ProcessIdle();
    EXPECT_TRUE(fs::exists(cache / "cache1"));
}

// "Dialog end": What wx 3.2 does with a MODELESS plain dialog (the ticket's NV): Cancel,
// Escape and the close box only hide it; it is not destroyed and no handler of ours runs. That is why
// SettingsDialog and SourcesDialog end through their own finish.
TEST_F(SettingsPage, WxOnlyHidesAModelessDialogOnCancelEscapeAndTheCloseBox) {
    const std::vector<std::pair<const char*, std::function<void(wxDialog*)>>> acts = {
        {"Cancel", [](wxDialog* d) { click(d->FindWindow(wxID_CANCEL)); }},
        {"Escape",
         [](wxDialog* d) {
             wxKeyEvent e(wxEVT_CHAR_HOOK);
             e.m_keyCode = WXK_ESCAPE;
             e.SetEventObject(d);
             d->GetEventHandler()->ProcessEvent(e);
         }},
        {"close box", [](wxDialog* d) { d->Close(); }},
    };
    for (const auto& [name, act] : acts) {
        SCOPED_TRACE(name);
        auto* d = new wxDialog(nullptr, wxID_ANY, "plain");
        d->SetSizerAndFit(d->CreateStdDialogButtonSizer(wxOK | wxCANCEL));
        wxWeakRef<wxDialog> ref(d);
        d->Show();
        act(d);
        EXPECT_FALSE(d->IsShown());
        wxTheApp->ProcessIdle();
        EXPECT_TRUE(ref) << "hidden, not destroyed";
        d->Destroy();
        wxTheApp->ProcessIdle();
        EXPECT_FALSE(ref);
    }
}

// OK saves and ends with onDone(true); Cancel, Escape and the close box end with onDone(false) and save
// nothing; every way calls onDone once, hides the window and deletes it at the next idle.
TEST_F(SettingsPage, AModelessDialogEndsOnOkCancelEscapeAndTheCloseBoxWithOneCallAndIsDeleted) {
    struct Row {
        const char* name;
        std::function<void(SettingsDialog*)> act;
        bool saved;
    };
    const std::vector<Row> rows = {
        {"OK", [](SettingsDialog* d) { click(d->FindWindow(wxID_OK)); }, true},
        {"Cancel", [](SettingsDialog* d) { click(d->FindWindow(wxID_CANCEL)); }, false},
        {"Escape",
         [](SettingsDialog* d) {
             wxKeyEvent e(wxEVT_CHAR_HOOK);
             e.m_keyCode = WXK_ESCAPE;
             e.SetEventObject(d);
             d->GetEventHandler()->ProcessEvent(e);
         },
         false},
        {"close box", [](SettingsDialog* d) { d->Close(); }, false},
    };
    int budget = 100;
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        ++budget;
        auto* d = new SettingsDialog(nullptr, *store_, look_);
        d->cliDefaultPaths = {(root_ / "no-such" / "chart-grabber-cli").string()};
        d->cliPathEnv = (root_ / "emptybin").string();
        wxWeakRef<SettingsDialog> ref(d);
        int calls = 0;
        bool saved = !row.saved;
        d->onDone = [&](bool s) {
            ++calls;
            saved = s;
        };
        d->Show();
        dynamic_cast<wxSpinCtrl*>(wxWindow::FindWindowByName("limit", d))->SetValue(budget);
        row.act(d);
        EXPECT_EQ(calls, 1);
        EXPECT_EQ(saved, row.saved);
        EXPECT_FALSE(d->IsShown()) << "hidden before the callback returned";
        EXPECT_EQ(store_->loadSettings().dataBudgetMb == budget, row.saved);
        wxTheApp->ProcessIdle();
        EXPECT_FALSE(ref) << "deleted";
        EXPECT_EQ(calls, 1);
    }
}

// one plugin Settings window at a time; shown finds it (modal or modeless) so the
// page and the strip raise it instead of opening a second one.
TEST_F(SettingsPage, ShownFindsTheOpenSettingsWindowAndNotAHiddenOne) {
    EXPECT_EQ(SettingsDialog::shown(), nullptr);
    auto* d = new SettingsDialog(nullptr, *store_, look_);
    d->cliDefaultPaths = {(root_ / "no-such" / "chart-grabber-cli").string()};
    d->cliPathEnv = (root_ / "emptybin").string();
    EXPECT_EQ(SettingsDialog::shown(), nullptr) << "not shown yet";
    d->Show();
    EXPECT_EQ(SettingsDialog::shown(), d);
    click(d->FindWindow(wxID_CANCEL));
    EXPECT_EQ(SettingsDialog::shown(), nullptr) << "hidden, not yet deleted";
    wxTheApp->ProcessIdle();
}

// Proofs 4 and 5: layout at 800 x 480 in touch mode, order top to bottom.
class SettingsLayout : public SettingsPage {
protected:
    void SetUp() override {
        SettingsPage::SetUp();
        ocpn_fake::touch = true;
        ocpn_fake::scale = 1.0;
    }
    static void controls(wxWindow* w, std::vector<wxWindow*>& out) {
        for (wxWindow* c : w->GetChildren()) {
            if (dynamic_cast<wxButton*>(c) || dynamic_cast<wxTextCtrl*>(c) || dynamic_cast<wxSpinCtrl*>(c) ||
                dynamic_cast<wxCheckBox*>(c) || dynamic_cast<wxCollapsiblePane*>(c))
                out.push_back(c);
            controls(c, out);
        }
    }
    // The y of a control inside the (unscrolled) rows.
    int y(const std::string& name) {
        wxWindow* w = wxWindow::FindWindowByName(name, dlg_.get());
        int sum = 0;
        for (; w && w->GetName() != "rows"; w = w->GetParent())
            sum += w->GetPosition().y;
        return sum;
    }
    // What does by code: open Advanced and tell the dialog.
    void expand() {
        auto* pane = find<wxCollapsiblePane>("advanced");
        pane->Collapse(false);
        wxCollapsiblePaneEvent e(pane, pane->GetId(), false);
        pane->GetEventHandler()->ProcessEvent(e);
    }
};

TEST_F(SettingsLayout, CollapsedFitsWithoutScrollingExpandedScrollsAndKeepsTheButtons) {
    open();
    wxSize size = dlg_->GetSize();
    auto* rows = find<wxScrolledWindow>("rows");
    EXPECT_LE(size.x, 800);
    EXPECT_LE(size.y, 480);
    EXPECT_LE(rows->GetVirtualSize().y, rows->GetClientSize().y) << "no scrolling with Advanced collapsed";
    std::vector<wxWindow*> all;
    controls(dlg_.get(), all);
    ASSERT_GE(all.size(), 12u);
    // Collapsed, only the pane's header is shown: the controls inside it are hidden.
    EXPECT_GE(find<wxCollapsiblePane>("advanced")->GetSize().y, look_.minControlHeight())
        << "advanced collapsed";

    expand();
    size = dlg_->GetSize();
    EXPECT_LE(size.x, 800);
    EXPECT_LE(size.y, 480);
    EXPECT_GT(rows->GetVirtualSize().y, rows->GetClientSize().y) << "the rows scroll";
    const wxRect client(wxPoint(0, 0), dlg_->GetClientSize());
    for (int id : {wxID_OK, wxID_CANCEL}) {
        const wxRect r = dlg_->FindWindow(id)->GetRect();
        EXPECT_TRUE(client.Contains(r)) << "button " << id << " outside the client area";
    }
    EXPECT_GE(dlg_->FindWindow(wxID_OK)->GetPosition().y, rows->GetRect().GetBottom())
        << "the button row is under the rows";
    for (const char* name : {"sourcesOpen", "chartsDefault", "cacheDefault", "cliDefault"}) {
        wxWindow* w = wxWindow::FindWindowByName(name, dlg_.get());
        int right = w->GetSize().x;
        for (; w && w->GetName() != "rows"; w = w->GetParent())
            right += w->GetPosition().x;
        EXPECT_LE(right, rows->GetClientSize().x) << name << " is cut off at the right";
    }
    all.clear();
    controls(dlg_.get(), all);
    EXPECT_EQ(look_.minControlHeight(), 48);
    for (wxWindow* w : all)
        EXPECT_GE(w->GetSize().y, look_.minControlHeight()) << w->GetName();
}

TEST_F(SettingsLayout, ALongPathDoesNotWidenTheDialog) {
    open();
    expand();
    const int width = dlg_->GetSize().x;
    const std::string longPath = std::string("/") + std::string(150, 'a') + "/" + std::string(148, 'b');
    ASSERT_EQ(longPath.size(), 300u);
    for (const PathCase& c : kPathCases)
        change(c.name, longPath);
    EXPECT_EQ(field("sources"), longPath);
    EXPECT_EQ(dlg_->GetSize().x, width);
    EXPECT_LE(dlg_->GetSize().y, 480);
    // The same paths already saved: the fields hold them at the first layout.
    cg::Settings s;
    s.mapsDir = s.chartDir = s.cacheDir = s.cliPath = longPath;
    store_->saveSettings(s);
    open();
    expand();
    EXPECT_EQ(field("cli"), longPath);
    EXPECT_EQ(dlg_->GetSize().x, width);
    EXPECT_LE(dlg_->GetSize().y, 480);
}

// The three one-line status labels (fills two of them) never change the dialog's size.
TEST_F(SettingsLayout, ALongStatusTextDoesNotChangeTheSize) {
    open();
    expand();
    const wxSize before = dlg_->GetSize();
    const std::string text = std::string("Chart Grabber CLI does not work: ") + std::string(300, 'x');
    for (const char* name : {"sourcesStatus", "chartsStatus", "cliStatus"}) {
        const long style = find<wxStaticText>(name)->GetWindowStyle(); // one line, ellipsized
        EXPECT_TRUE(style & wxST_ELLIPSIZE_END) << name;
        EXPECT_TRUE(style & wxST_NO_AUTORESIZE) << name;
        find<wxStaticText>(name)->SetLabel(wxString::FromUTF8(text));
        dlg_->Layout();
        EXPECT_EQ(dlg_->GetSize(), before) << name;
        EXPECT_EQ(find<wxStaticText>(name)->GetSize().y, find<wxStaticText>(name)->GetMinSize().y) << name;
    }
}

TEST_F(SettingsLayout, TheRowsAreInTheSailorsOrderAndTheStatusLabelsAreEmpty) {
    open();
    expand();
    const std::vector<std::string> order = {
        "sourcesField", "slow",       "limit",    "clear",    "advanced",
        "chartsField",  "cacheField", "cliField", "register", "warnDetailLevels",
        "stripOnChart", "stripNote",  "logFile",  "debugLog"};
    for (std::size_t i = 1; i < order.size(); ++i)
        EXPECT_LT(y(order[i - 1]), y(order[i])) << order[i - 1] << " before " << order[i];
    EXPECT_LT(y("sourcesField"), y("sourcesStatus"));
    EXPECT_LT(y("sourcesStatus"), y("slow"));
    EXPECT_LT(y("chartsField"), y("chartsStatus"));
    EXPECT_LT(y("chartsStatus"), y("cacheField"));
    wxWindow* ok = dlg_->FindWindow(wxID_OK);
    wxWindow* status = wxWindow::FindWindowByName("cliStatus", dlg_.get());
    wxWindow* rows = wxWindow::FindWindowByName("rows", dlg_.get());
    EXPECT_GE(ok->GetPosition().y, rows->GetRect().GetBottom());
    EXPECT_GE(status->GetPosition().y, rows->GetRect().GetBottom());
    EXPECT_EQ(label("cliStatus"), "");
    EXPECT_EQ(label("sourcesStatus"), "");
}

// ---- : the automatic checks. The CLI is the sources stand-in of tests/sources_stub.h; the
// 500 ms timer is the real one (checkDelayMs is left at its default), the waits dispatch events until a
// condition holds (a deadline only guards a hang). ----

class CollectLog : public wxLog {
public:
    std::vector<std::string> messages;

protected:
    void DoLogRecord(wxLogLevel, const wxString& msg, const wxLogRecordInfo&) override {
        messages.push_back(msg.utf8_string());
    }
};

// Installs a collecting log target for one scope and restores (and frees) the old one.
class LogScope {
public:
    LogScope() : log_(new CollectLog), previous_(wxLog::SetActiveTarget(log_)) {}
    ~LogScope() { delete wxLog::SetActiveTarget(previous_); }
    CollectLog* operator->() { return log_; }

private:
    CollectLog* log_;
    wxLog* previous_;
};

class SettingsCheck : public SettingsLayout {
protected:
    using Clock = std::chrono::steady_clock;
    void SetUp() override {
        SettingsLayout::SetUp();
        if (!store_)
            return;
        loop_.reset(wxTheApp->GetTraits()->CreateEventLoop());
        activator_ = std::make_unique<wxEventLoopActivator>(loop_.get());
        stub_ = std::make_unique<stub::Sources>(root_ / "stubs", "a d'x");
    }
    void TearDown() override {
        if (store_) {
            dlg_.reset(); // the runner's stopAndWait pumps the loop
            expectNoProcessesUnder((root_ / "stubs").string());
            activator_.reset();
            loop_.reset();
        }
        SettingsLayout::TearDown();
    }
    bool runUntil(const std::function<bool()>& done, int timeoutMs = 15000) {
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        while (!done()) {
            if (Clock::now() >= deadline)
                return false;
            loop_->DispatchTimeout(10);
        }
        return true;
    }
    void runFor(int ms) {
        const auto until = Clock::now() + std::chrono::milliseconds(ms);
        while (Clock::now() < until)
            loop_->DispatchTimeout(10);
    }
    // The first check of an open dialog has ended: both lines show an answer.
    bool settle() {
        return runUntil([&] {
            const std::string c = label("cliStatus"), s = label("sourcesStatus");
            return !c.empty() && c != "Checking Chart Grabber CLI..." && !s.empty() &&
                   s != "Looking for chart sources...";
        });
    }
    void save(const std::function<void(cg::Settings&)>& edit) {
        cg::Settings s = store_->loadSettings();
        edit(s);
        store_->saveSettings(s);
    }
    // A dialog whose saved program is the stand-in.
    void openStub() {
        save([&](cg::Settings& s) { s.cliPath = stub_->cli(); });
        open();
    }
    static std::vector<stub::Rec> recs(int n) {
        std::vector<stub::Rec> out;
        for (int i = 0; i < n; ++i) {
            stub::Rec r;
            r.guid = "g"; // named strings, not "g" + to_string(i): GCC 12 -O3 -Wrestrict false positive
            r.guid += std::to_string(i);
            r.kind = i == 1 ? "layer" : "map";
            r.name = "Source ";
            r.name += std::to_string(i);
            out.push_back(r);
        }
        return out;
    }
    bool advancedOpen() { return !find<wxCollapsiblePane>("advanced")->IsCollapsed(); }
    // An executable that is not a program the dialog can run: a regular file without the x bit.
    std::string plainFile() {
        const fs::path f = root_ / "plain.txt";
        std::ofstream(f) << "text";
        return f.string();
    }

    std::unique_ptr<wxEventLoopBase> loop_;
    std::unique_ptr<wxEventLoopActivator> activator_;
    std::unique_ptr<stub::Sources> stub_;
};

const char* const kReadyNone = "Chart sources are checked when Chart Grabber CLI is ready.";

// and 4: a check by itself on open, no button; the sailor's folder missing, then empty.
TEST_F(SettingsCheck, OnOpenTheLinesSayReadyAndWhatTheSourcesAre) {
    stub_->records(recs(3));
    openStub();
    EXPECT_EQ(label("cliStatus"), "") << "nothing before the event loop runs";
    ASSERT_TRUE(settle());
    EXPECT_EQ(label("cliStatus"), "Chart Grabber CLI 2026.10.9 is ready.");
    EXPECT_EQ(label("sourcesStatus"),
              "3 chart sources available. Your own chart sources folder was not found: press Open folder to "
              "create it.");
    EXPECT_FALSE(advancedOpen());
    ASSERT_EQ(stub_->argv().size(), 1u);
    EXPECT_NE(
        stub_->argv()[0].find(std::string("sources --format=tsv --maps ") + bundled_.string() + " --cache"),
        std::string::npos)
        << "an absent folder is not passed: " << stub_->argv()[0];
    fs::create_directories(home_ / "Charts" / "Chart sources");
    dlg_.reset();
    open();
    ASSERT_TRUE(settle());
    EXPECT_EQ(label("sourcesStatus"), "3 chart sources available. Put your own chart source (.zmp or "
                                      ".source.json) files in your folder to add more.");
    ASSERT_EQ(stub_->argv().size(), 2u);
    EXPECT_EQ(stub_->argv()[1].find("--maps", stub_->argv()[1].find("--maps") + 1), std::string::npos)
        << "an empty folder is not passed: " << stub_->argv()[1];
}

TEST_F(SettingsCheck, AFolderWithASourceTwoLevelsDownIsPassedLastAndCounted) {
    const fs::path folder = root_ / "my sources d'x";
    fs::create_directories(folder / "one" / "two");
    std::ofstream(folder / "one" / "two" / "Local.zmp") << "x";
    stub_->records(recs(1));
    save([&](cg::Settings& s) { s.mapsDir = folder.string(); });
    openStub();
    ASSERT_TRUE(settle());
    EXPECT_EQ(label("sourcesStatus"), "1 chart source available, with those in your folder.");
    ASSERT_EQ(stub_->argv().size(), 1u);
    EXPECT_NE(stub_->argv()[0].find(std::string("--maps ") + bundled_.string() + " --maps " +
                                    folder.string() + " --cache"),
              std::string::npos)
        << stub_->argv()[0];
}

// the JSON kind alone is a source, in the bundled folder and in the sailor's.
TEST_F(SettingsCheck, ASourceJsonFileAloneCountsInTheBundledAndTheSailorFolder) {
    fs::remove(bundled_ / "a.zmp");
    std::ofstream(bundled_ / "b.source.json") << "{}";
    const fs::path folder = root_ / "json sources";
    fs::create_directories(folder / "one" / "two");
    std::ofstream(folder / "one" / "two" / "Local.source.json") << "{}";
    stub_->records(recs(1));
    save([&](cg::Settings& s) { s.mapsDir = folder.string(); });
    openStub();
    ASSERT_TRUE(settle());
    EXPECT_NE(
        label("sourcesStatus"),
        "The chart sources that come with Chart Grabber are missing. Reinstall the Chart Grabber plugin.");
    ASSERT_EQ(stub_->argv().size(), 1u);
    EXPECT_NE(stub_->argv()[0].find(std::string("--maps ") + bundled_.string() + " --maps " +
                                    folder.string() + " --cache"),
              std::string::npos)
        << stub_->argv()[0];
}

// rows 3 and 5: no bundled source means no CLI run; a run without a record says so.
TEST_F(SettingsCheck, WithoutBundledSourcesTheCliIsNotRun) {
    fs::create_directories(root_ / "empty bundled");
    const std::string bundledMissing =
        "The chart sources that come with Chart Grabber are missing. Reinstall the Chart Grabber plugin.";
    for (const char* dir : {"", "nowhere", "empty bundled"}) {
        SCOPED_TRACE(dir);
        store_->setBundledSourcesDir(*dir ? (root_ / dir).string() : "");
        stub_->records(recs(3));
        openStub();
        ASSERT_TRUE(settle());
        EXPECT_EQ(label("cliStatus"), "Chart Grabber CLI 2026.10.9 is ready.");
        EXPECT_EQ(label("sourcesStatus"), bundledMissing);
        EXPECT_FALSE(advancedOpen()) << "the plugin's own files are missing, not the CLI";
        EXPECT_TRUE(stub_->argv().empty()) << "sources was not run";
        dlg_.reset();
    }
    // the race: the folder went away after the check passed; the CLI refuses with exit 3
    store_->setBundledSourcesDir(bundled_.string());
    stub_->exitWith(3);
    stub_->stderrText("error: no map sources given: pass --maps DIR, a folder of map-source definitions "
                      "(repeat --maps for more folders)\n");
    openStub();
    ASSERT_TRUE(settle());
    EXPECT_EQ(label("sourcesStatus"), bundledMissing);
    EXPECT_EQ(label("cliStatus"), "Chart Grabber CLI 2026.10.9 is ready.");
    EXPECT_FALSE(advancedOpen()) << "the race keeps Advanced folded too";
    stub_->exitWith(0);
    stub_->stderrText("");
    stub_->records({});
    dlg_.reset();
    open();
    ASSERT_TRUE(settle());
    EXPECT_EQ(label("sourcesStatus"),
              "No usable chart source was found. Reinstall Chart Grabber, or check the files in your chart "
              "sources folder.");
}

// Proof 3 and 7a: the program is changed to a file that cannot run.
TEST_F(SettingsCheck, ANotRunnableProgramIsReportedAfterTheDelayAndOpensAdvancedLikeAClick) {
    openStub();
    ASSERT_TRUE(settle());
    change("cli", plainFile());
    EXPECT_EQ(label("cliStatus"), "Chart Grabber CLI 2026.10.9 is ready.") << "not before the delay";
    const auto t0 = Clock::now();
    ASSERT_TRUE(runUntil([&] { return label("cliStatus").find("cannot be run") != std::string::npos; }));
    EXPECT_GE(Clock::now() - t0, std::chrono::milliseconds(400));
    EXPECT_EQ(label("cliStatus"),
              "This file cannot be run as a program. Use default to go back to the installed one.");
    EXPECT_EQ(label("sourcesStatus"), kReadyNone);
    ASSERT_TRUE(advancedOpen());
    // 7a: the same layout as after a click on the header.
    const wxSize byCode = dlg_->GetSize();
    auto* rows = find<wxScrolledWindow>("rows");
    EXPECT_LT(find<wxCollapsiblePane>("advanced")->GetMinSize().y, look_.minControlHeight())
        << "the touch header minimum is dropped";
    EXPECT_LE(byCode.y, 480);
    EXPECT_GT(rows->GetVirtualSize().y, rows->GetClientSize().y) << "the rows scroll";
    const wxRect client(wxPoint(0, 0), dlg_->GetClientSize());
    for (int id : {wxID_OK, wxID_CANCEL})
        EXPECT_TRUE(client.Contains(dlg_->FindWindow(id)->GetRect())) << "button " << id;
    dlg_.reset();
    openStub();
    ASSERT_TRUE(settle());
    expand();
    EXPECT_EQ(dlg_->GetSize(), byCode);
}

// Proof 3b: "Use default" (an empty CliPath) through the dialog's default path and PATH.
TEST_F(SettingsCheck, TheAutomaticProgramIsTheDefaultPathThenPathThenNone) {
    stub::Sources second(root_ / "stubs", "b d'x");
    stub_->version("chart-grabber-cli 2026.10.9 (a)");
    second.version("chart-grabber-cli 2026.11.2 (b)");
    fs::create_directories(root_ / "pathbin");
    const std::string onPath = (root_ / "pathbin" / "chart-grabber-cli").string();
    std::ofstream(onPath) << "#!/bin/sh\nexec \"" << second.cli() << "\" \"$@\"\n";
    chmod(onPath.c_str(), 0755);
    const std::string absent = (root_ / "nowhere" / "chart-grabber-cli").string();
    struct Row {
        const char* name;
        std::string installed, pathDir, cli, sources;
        bool advanced;
        int firstRuns, secondRuns;
    };
    const std::vector<Row> rows = {
        {"default path wins", stub_->cli(), (root_ / "pathbin").string(),
         "Chart Grabber CLI 2026.10.9 is ready.", "", false, 1, 0},
        {"PATH when the default is absent", absent, (root_ / "pathbin").string(),
         "Chart Grabber CLI 2026.11.2 is ready.", "", false, 1, 1},
        {"neither", absent, (root_ / "emptybin").string(),
         "Chart Grabber CLI was not found. Install the chart-grabber-cli package, or choose the program "
         "under "
         "Advanced.",
         kReadyNone, true, 1, 1},
    };
    for (const Row& r : rows) {
        SCOPED_TRACE(r.name);
        open();
        dlg_->cliDefaultPaths = {r.installed};
        dlg_->cliPathEnv = r.pathDir;
        ASSERT_TRUE(settle());
        EXPECT_EQ(label("cliStatus"), r.cli);
        if (!r.sources.empty()) {
            EXPECT_EQ(label("sourcesStatus"), r.sources);
        }
        EXPECT_EQ(advancedOpen(), r.advanced);
        EXPECT_EQ(static_cast<int>(stub_->argv().size()), r.firstRuns);
        EXPECT_EQ(static_cast<int>(second.argv().size()), r.secondRuns);
        dlg_.reset();
    }
}

// Proof 4: too old, and a CLI that fails.
TEST_F(SettingsCheck, ATooOldProgramAndAFailingOne) {
    struct Old {
        const char* line;
        const char* shown;
    };
    int n = 0;
    for (const Old& o : {Old{"old-tiles 0.1.0 (x)", "Chart Grabber CLI 0.1.0 is too old. "},
                         Old{"chart-grabber-cli 2026.10.3 (x)", "Chart Grabber CLI 2026.10.3 is too old. "},
                         Old{"", "Chart Grabber CLI is too old. "}}) {
        SCOPED_TRACE(o.line);
        dlg_.reset();
        stub::Sources old(root_ / "stubs", std::string("old ") + std::to_string(++n));
        old.version(o.line);
        save([&](cg::Settings& s) { s.cliPath = old.cli(); });
        open();
        ASSERT_TRUE(settle());
        EXPECT_EQ(label("cliStatus"),
                  std::string(o.shown) + "Install the current chart-grabber-cli package.");
        EXPECT_EQ(label("sourcesStatus"), kReadyNone);
        EXPECT_TRUE(advancedOpen());
        EXPECT_TRUE(old.argv().empty()) << "sources was not run";
    }

    struct Row {
        const char* name;
        std::string stderrText, shown;
    };
    for (const Row& r :
         {Row{"an error line", "warning: first\nerror: Broken \xD0\x81 'x'\n", "Broken \xD0\x81 'x'"},
          Row{"no error line", "", "error 3"}}) {
        SCOPED_TRACE(r.name);
        dlg_.reset();
        stub::Sources failing(root_ / "stubs", std::string("fail ") + (r.stderrText.empty() ? "b" : "a"));
        failing.exitWith(3);
        if (!r.stderrText.empty())
            failing.stderrText(r.stderrText);
        save([&](cg::Settings& s) { s.cliPath = failing.cli(); });
        open();
        ASSERT_TRUE(settle());
        EXPECT_EQ(label("cliStatus"), "Chart Grabber CLI does not work: " + r.shown);
        EXPECT_EQ(label("sourcesStatus"), "Chart sources could not be checked.");
        EXPECT_TRUE(advancedOpen());
    }
}

// Proof 5: two changes 100 ms apart start one check; the check that follows uses the later value.
TEST_F(SettingsCheck, TwoChangesOneHundredMsApartStartOneCheck) {
    const fs::path first = root_ / "first", second = root_ / "second";
    fs::create_directories(first);
    fs::create_directories(second / "sub");
    std::ofstream(second / "sub" / "Local.zmp") << "x";
    stub_->records(recs(1));
    openStub();
    ASSERT_TRUE(settle());
    ASSERT_EQ(stub_->argv().size(), 1u);
    change("sources", first.string());
    runFor(100);
    change("sources", second.string());
    ASSERT_TRUE(runUntil(
        [&] { return stub_->argv().size() >= 2 && label("sourcesStatus").find("1 chart source") == 0; }));
    runFor(900); // a third check would have started by now
    ASSERT_EQ(stub_->argv().size(), 2u);
    EXPECT_NE(stub_->argv()[1].find(std::string("--maps ") + second.string()), std::string::npos)
        << stub_->argv()[1];
}

// Proof 5: a change while the sources run is slow terminates it; only the later run's answer is shown.
TEST_F(SettingsCheck, AChangeWhileTheRunIsSlowTerminatesItAndOnlyTheLaterAnswerIsShown) {
    stub_->records(recs(3));
    stub_->hold(true);
    LogScope log;
    openStub();
    ASSERT_TRUE(runUntil([&] { return stub_->argv().size() == 1; })) << "the first run holds";
    EXPECT_EQ(label("cliStatus"), "Checking Chart Grabber CLI...");
    stub_->records(recs(1));
    const fs::path folder = root_ / "other";
    fs::create_directories(folder);
    change("sources", folder.string());
    // The second run starts only after the first one was terminated and its exit arrived.
    ASSERT_TRUE(runUntil([&] { return stub_->argv().size() == 2; }));
    stub_->hold(false);
    ASSERT_TRUE(runUntil([&] { return label("sourcesStatus").find("available") != std::string::npos; }));
    EXPECT_EQ(label("sourcesStatus"), "1 chart source available. Put your own chart source (.zmp or "
                                      ".source.json) files in your folder to add more.");
    EXPECT_EQ(label("cliStatus"), "Chart Grabber CLI 2026.10.9 is ready.");
    runFor(100);
    for (const std::string& m : log->messages)
        EXPECT_EQ(m.find("3 chart sources"), std::string::npos) << m;
}

// Proof 6: one line whatever the text; the full text goes to the log once.
TEST_F(SettingsCheck, ALongErrorKeepsTheSizeAndIsLoggedOnce) {
    const std::string error(2000, 'e');
    stub_->exitWith(3);
    stub_->stderrText("error: " + error + "\n");
    openStub();
    expand();
    dlg_->Show();
    const wxSize before = dlg_->GetSize();
    LogScope log;
    ASSERT_TRUE(settle());
    EXPECT_EQ(dlg_->GetSize(), before);
    EXPECT_EQ(label("cliStatus"), "Chart Grabber CLI does not work: " + error);
    int full = 0;
    for (const std::string& m : log->messages)
        full += m.find(error) != std::string::npos;
    EXPECT_EQ(full, 1);
    wxWindow* status = wxWindow::FindWindowByName("cliStatus", dlg_.get());
    runFor(50);
    GtkLabel* gtkLabel = GTK_LABEL(status->GetHandle());
    EXPECT_EQ(gtk_label_get_ellipsize(gtkLabel), PANGO_ELLIPSIZE_END);
    EXPECT_TRUE(pango_layout_is_ellipsized(gtk_label_get_layout(gtkLabel))) << "the shown text ends in ...";
    EXPECT_LE(status->GetSize().x, before.x);
}

// Proof 7: closing while a check runs (or while the delay is pending) leaves nothing behind.
TEST_F(SettingsCheck, ClosingWhileACheckRunsLeavesNoChildAndNoCallback) {
    stub_->records(recs(1));
    stub_->hold(true);
    openStub();
    ASSERT_TRUE(runUntil([&] { return stub_->argv().size() == 1; }));
    dlg_.reset();
    EXPECT_TRUE(processesUnder((root_ / "stubs").string()).empty()) << "the child is gone";
    runFor(100);
    stub_->hold(false);
    openStub();
    ASSERT_TRUE(settle());
    change("sources", (root_ / "elsewhere").string());
    dlg_.reset(); // the 500 ms timer was pending
    runFor(700);
    EXPECT_EQ(stub_->argv().size(), 2u) << "no check after the dialog was gone";
}

// Proof 7b: a scheme change re-applies Look to the open dialog and touches nothing after it is destroyed.
TEST_F(SettingsCheck, ASchemeChangeReachesTheOpenDialogAndNotTheDestroyedOne) {
    open();
    ocpn_fake::dimeCalls = 0;
    ocpn_fake::dimeLast = nullptr;
    look_.setScheme(PI_GLOBAL_COLOR_SCHEME_DUSK);
    EXPECT_EQ(ocpn_fake::dimeCalls, 1);
    EXPECT_EQ(ocpn_fake::dimeLast, dlg_.get());
    dlg_.reset();
    ocpn_fake::dimeCalls = 0;
    look_.setScheme(PI_GLOBAL_COLOR_SCHEME_NIGHT);
    EXPECT_EQ(ocpn_fake::dimeCalls, 0);
}

} // namespace
