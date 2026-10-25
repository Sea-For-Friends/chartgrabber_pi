// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings_dialog.h"

#include "cli_runner.h"
#include "config_store.h"
#include "guard.h"
#include "job_files.h"
#include "look.h"
#include "messages.h"
#include "plugin_log.h"
#include "portable.h"
#include "sources_folder.h"
#include "wx_util.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/collpane.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/intl.h>
#include <wx/log.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <ocpn_plugin.h>

namespace {

// Size of the cache root in bytes; a missing or unreadable folder is 0.
std::int64_t cacheBytes(const std::string& root) {
    if (!wxDir::Exists(cg::fromUtf8(root)))
        return 0;
    const wxULongLong total = wxDir::GetTotalSize(cg::fromUtf8(root));
    return total == wxInvalidSize ? 0 : static_cast<std::int64_t>(total.GetValue());
}

// Touch: every control at least `minHeight` high.
void sizeControls(wxWindow* w, int minHeight) {
    for (wxWindow* c : w->GetChildren()) {
        if (dynamic_cast<wxButton*>(c) || dynamic_cast<wxTextCtrl*>(c) || dynamic_cast<wxSpinCtrl*>(c) ||
            dynamic_cast<wxCheckBox*>(c))
            c->SetMinSize(wxSize(-1, minHeight));
        sizeControls(c, minHeight);
    }
}

// A one-line text that never changes the dialog's size : ellipsized, width set by the layout.
wxStaticText* statusLine(wxWindow* parent, const char* name) {
    auto* t = new wxStaticText(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                               wxST_ELLIPSIZE_END | wxST_NO_AUTORESIZE, name);
    t->SetMinSize(wxSize(parent->FromDIP(60), t->GetCharHeight() + 2));
    return t;
}

void click(wxButton* b, std::function<void()> fn) {
    b->Bind(wxEVT_BUTTON, [fn = std::move(fn)](wxCommandEvent&) { cg::guarded("settings button", fn); });
}

} // namespace

SettingsDialog::SettingsDialog(wxWindow* parent, ConfigStore& config, const Look& look,
                               std::function<bool()> jobBusy)
    : wxDialog(parent, wxID_ANY, _("Chart Grabber settings")), config_(config), look_(look),
      pending_(config.loadSettings()), jobBusy_(std::move(jobBusy)), busy_(jobBusy_ && jobBusy_()),
      checkTimer_(this) {
    choosePath = [this](PathKind kind, const wxString& current) -> std::optional<wxString> {
        wxString path;
        int rc = wxID_CANCEL;
        switch (kind) {
        case PathKind::Sources:
            rc = PlatformDirSelectorDialog(
                this, &path, _("Choose the folder with your chart sources (.zmp or .source.json)"), current);
            break;
        case PathKind::Charts:
            rc = PlatformDirSelectorDialog(this, &path, _("Choose the folder for new charts"), current);
            break;
        case PathKind::Cache:
            rc = PlatformDirSelectorDialog(this, &path, _("Choose where to keep saved tiles"), current);
            break;
        case PathKind::Cli:
            rc = PlatformFileSelectorDialog(this, &path, _("Choose Chart Grabber CLI"),
                                            wxFileName(current).GetPath(), wxEmptyString, "*");
            break;
        }
        if (rc != wxID_OK || path.empty())
            return std::nullopt;
        return path;
    };
    openFolder = [](const wxString& folder) {
        if (!wxLaunchDefaultApplication(folder))
            cg::logLine(wxString::Format("cannot open %s", folder));
    };

    // One caption column of common width.
    for (const wxString& c : {_("Chart sources"), _("Connection"), _("Saved tiles"), _("Save charts in"),
                              _("Saved tiles in"), _("Chart Grabber CLI"), _("Log file")})
        captionWidth_ = std::max(captionWidth_, GetTextExtent(c).x);

    rows_ = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL, "rows");
    rows_->SetScrollRate(0, FromDIP(10));
    auto* column = new wxBoxSizer(wxVERTICAL);
    rows_->SetSizer(column);

    makePathRow(rows_, column, PathKind::Sources, _("Chart sources"), "sources", true);
    sourcesStatus_ = statusLine(rows_, "sourcesStatus");
    newRow(rows_, column, wxEmptyString)->Add(sourcesStatus_, 1, wxALIGN_CENTER_VERTICAL);

    slow_ = new wxCheckBox(rows_, wxID_ANY, _("Slow or paid by the MB (mobile data, satellite)"),
                           wxDefaultPosition, wxDefaultSize, 0, wxDefaultValidator, "slow");
    slow_->SetValue(pending_.slowConnection);
    newRow(rows_, column, _("Connection"))->Add(slow_, 1, wxALIGN_CENTER_VERTICAL);
    budgetMb_ = new wxSpinCtrl(rows_, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                               wxSP_ARROW_KEYS, 1, 100000, pending_.dataBudgetMb);
    budgetMb_->SetName("limit");
    budgetMb_->Enable(pending_.slowConnection);
    wxBoxSizer* limit = newRow(rows_, column, wxEmptyString);
    limit->Add(new wxStaticText(rows_, wxID_ANY, _("Limit per download")), 0,
               wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    limit->Add(budgetMb_, 0, wxALIGN_CENTER_VERTICAL);
    limit->Add(new wxStaticText(rows_, wxID_ANY, _("MB  (asks first, then stops)")), 0,
               wxALIGN_CENTER_VERTICAL | wxLEFT, 6);

    cacheSize_ =
        new wxStaticText(rows_, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, "cacheSize");
    clear_ = new wxButton(rows_, wxID_ANY, _("Clear..."), wxDefaultPosition, wxDefaultSize, 0,
                          wxDefaultValidator, "clear");
    wxBoxSizer* tiles = newRow(rows_, column, _("Saved tiles"));
    tiles->Add(cacheSize_, 1, wxALIGN_CENTER_VERTICAL);
    tiles->Add(clear_, 0, wxLEFT, 6);

    advanced_ = new wxCollapsiblePane(rows_, wxID_ANY, _("Advanced"), wxDefaultPosition, wxDefaultSize,
                                      wxCP_DEFAULT_STYLE, wxDefaultValidator, "advanced");
    column->Add(advanced_, 0, wxEXPAND | wxTOP, 4);
    wxWindow* pane = advanced_->GetPane();
    auto* paneColumn = new wxBoxSizer(wxVERTICAL);
    pane->SetSizer(paneColumn);
    makePathRow(pane, paneColumn, PathKind::Charts, _("Save charts in"), "charts", false);
    chartsStatus_ = statusLine(pane, "chartsStatus");
    newRow(pane, paneColumn, wxEmptyString)->Add(chartsStatus_, 1, wxALIGN_CENTER_VERTICAL);
    makePathRow(pane, paneColumn, PathKind::Cache, _("Saved tiles in"), "cache", false);
    makePathRow(pane, paneColumn, PathKind::Cli, _("Chart Grabber CLI"), "cli", false);
    register_ = new wxCheckBox(pane, wxID_ANY, _("Add new charts to OpenCPN's chart list"), wxDefaultPosition,
                               wxDefaultSize, 0, wxDefaultValidator, "register");
    register_->SetValue(pending_.registerCharts);
    newRow(pane, paneColumn, wxEmptyString)->Add(register_, 1, wxALIGN_CENTER_VERTICAL);
    warnDetail_ = new wxCheckBox(pane, wxID_ANY, warnDetailCheckText(), wxDefaultPosition, wxDefaultSize, 0,
                                 wxDefaultValidator, "warnDetailLevels");
    warnDetail_->SetValue(pending_.warnDetailLevels);
    newRow(pane, paneColumn, wxEmptyString)->Add(warnDetail_, 1, wxALIGN_CENTER_VERTICAL);
    stripOnChart_ = new wxCheckBox(pane, wxID_ANY, _("Show download progress on the chart"),
                                   wxDefaultPosition, wxDefaultSize, 0, wxDefaultValidator, "stripOnChart");
    stripOnChart_->SetValue(pending_.stripOnChart);
    newRow(pane, paneColumn, wxEmptyString)->Add(stripOnChart_, 1, wxALIGN_CENTER_VERTICAL);
    stripNote_ =
        new wxStaticText(pane, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, "stripNote");
    stripNote_->SetLabelText(
        _("Off: progress has its own small window. With OpenGL on Wayland the chart can "
          "hide progress shown on it. Applies after OpenCPN restarts. On Wayland the window "
          "cannot remember its place; the option above keeps the strip on the chart."));
    stripNote_->Wrap(FromDIP(380));
    newRow(pane, paneColumn, wxEmptyString)->Add(stripNote_, 1, wxALIGN_CENTER_VERTICAL);
    // The plugin's own log file : its path (empty when no file is written) and Debug.
    const wxString logPath = cg::fromUtf8(cg::pluginLogPath());
    logField_ = new wxTextCtrl(pane, wxID_ANY, logPath, wxDefaultPosition, wxDefaultSize, wxTE_READONLY,
                               wxDefaultValidator, "logFile");
    logOpen_ = new wxButton(pane, wxID_ANY, _("Open"), wxDefaultPosition, wxDefaultSize, 0,
                            wxDefaultValidator, "logOpen");
    logOpen_->Enable(!logPath.empty() && wxFileName::FileExists(logPath));
    click(logOpen_, [this] { openFolder(logField_->GetValue()); });
    wxBoxSizer* logRow = newRow(pane, paneColumn, _("Log file"));
    logRow->Add(logField_, 1, wxALIGN_CENTER_VERTICAL);
    logRow->Add(logOpen_, 0, wxLEFT, 6);
    debug_ = new wxCheckBox(pane, wxID_ANY, _("Debug: write more detail to the log file"), wxDefaultPosition,
                            wxDefaultSize, 0, wxDefaultValidator, "debugLog");
    debug_->SetValue(pending_.debugLog);
    newRow(pane, paneColumn, wxEmptyString)->Add(debug_, 1, wxALIGN_CENTER_VERTICAL);

    cliStatus_ = statusLine(this, "cliStatus");
    cliStatus_->SetMinSize(wxSize(FromDIP(150), cliStatus_->GetMinSize().y));
    wxStdDialogButtonSizer* buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    auto* bottom = new wxBoxSizer(wxHORIZONTAL);
    bottom->Add(cliStatus_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    bottom->Add(buttons, 0, wxALIGN_CENTER_VERTICAL);
    auto* main = new wxBoxSizer(wxVERTICAL);
    main->Add(rows_, 1, wxEXPAND | wxALL, 10);
    main->Add(bottom, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    SetSizer(main);

    slow_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { budgetMb_->Enable(slow_->GetValue()); });
    click(clear_, [this] { clearSavedTiles(); });
    const int minHeight = look.minControlHeight();
    minHeight_ = minHeight;
    onAdvancedChanged(); // the collapsed header's touch height; refit follows at the end
    advanced_->Bind(wxEVT_COLLAPSIBLEPANE_CHANGED, [this](wxCollapsiblePaneEvent&) {
        cg::guarded("settings advanced", [&] { onAdvancedChanged(); });
    });
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { cg::guarded("settings check", [&] { startCheck(); }); });
    Bind(
        wxEVT_BUTTON,
        [this](wxCommandEvent&) {
            cg::guarded("settings OK", [&] {
                pending_.dataBudgetMb = budgetMb_->GetValue();
                pending_.slowConnection = slow_->GetValue();
                pending_.registerCharts = register_->GetValue();
                pending_.warnDetailLevels = warnDetail_->GetValue();
                pending_.stripOnChart = stripOnChart_->GetValue();
                if (const bool debug = debug_->GetValue(); debug != pending_.debugLog) {
                    pending_.debugLog = debug; // applied at once
                    cg::setPluginLogDebug(debug);
                    cg::logFile(debug ? "debug on" : "debug off");
                }
                config_.saveSettings(pending_);
                finish(wxID_OK);
            });
        },
        wxID_OK);
    // wx's default EndDialog only hides a modeless dialog: Cancel, Escape and the close box end it here.
    Bind(
        wxEVT_BUTTON,
        [this](wxCommandEvent&) { cg::guarded("settings cancel", [&] { finish(wxID_CANCEL); }); },
        wxID_CANCEL);
    Bind(wxEVT_CLOSE_WINDOW,
         [this](wxCloseEvent&) { cg::guarded("settings close", [&] { finish(wxID_CANCEL); }); });

    if (minHeight > 0)
        sizeControls(this, minHeight);
    look.apply(this);
    lookToken_ = look.subscribe([this] {
        if (!closing_)
            look_.apply(this);
    });
    for (PathKind k : {PathKind::Sources, PathKind::Charts, PathKind::Cache, PathKind::Cli})
        showRow(k);
    bottomHeight_ = bottom->CalcMin().y;
    refit();
    constructed_ = true;
    CallAfter([this] {
        if (!closing_)
            cg::guarded("settings check", [&] { startCheck(); });
    });
}

SettingsDialog::~SettingsDialog() {
    closing_ = true;
    delete question_.get(); // its callback holds this dialog; immediate, not Destroy
    checkTimer_.Stop();
    runner_.reset(); // may pump events; its callbacks see closing_
    look_.unsubscribe(lookToken_);
}

SettingsDialog* SettingsDialog::shown() {
    for (wxWindowList::compatibility_iterator node = wxTopLevelWindows.GetFirst(); node;
         node = node->GetNext())
        if (auto* d = dynamic_cast<SettingsDialog*>(node->GetData()); d && d->IsShown())
            return d;
    return nullptr;
}

std::string& SettingsDialog::key(PathKind kind) {
    switch (kind) {
    case PathKind::Sources:
        return pending_.mapsDir;
    case PathKind::Charts:
        return pending_.chartDir;
    case PathKind::Cache:
        return pending_.cacheDir;
    case PathKind::Cli:
        break;
    }
    return pending_.cliPath;
}

// with no chosen program: /usr/bin, else PATH; empty = none.
std::string SettingsDialog::automaticCli() const {
    std::string why;
    return resolveCli(cg::Settings{}, &why, cliDefaultPaths, cliPathEnv).value_or(std::string());
}

std::string SettingsDialog::defaultPath(PathKind kind) const {
    switch (kind) {
    case PathKind::Sources:
        return config_.sourcesDir(cg::Settings{});
    case PathKind::Charts:
        return config_.chartDir(cg::Settings{});
    case PathKind::Cache:
        return config_.cliCacheRoot(cg::Settings{});
    case PathKind::Cli:
        break;
    }
    return automaticCli();
}

wxString SettingsDialog::resolvedText(PathKind kind) const {
    switch (kind) {
    case PathKind::Sources:
        return cg::fromUtf8(config_.sourcesDir(pending_));
    case PathKind::Charts:
        return cg::fromUtf8(config_.chartDir(pending_));
    case PathKind::Cache:
        return cg::fromUtf8(config_.cliCacheRoot(pending_));
    case PathKind::Cli:
        break;
    }
    return cg::fromUtf8(pending_.cliPath.empty() ? automaticCli() : pending_.cliPath);
}

// A row: the caption of common width, then the content added by the caller.
wxBoxSizer* SettingsDialog::newRow(wxWindow* parent, wxSizer* column, const wxString& caption) {
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    auto* label = new wxStaticText(parent, wxID_ANY, caption);
    label->SetMinSize(wxSize(captionWidth_, -1));
    row->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    column->Add(row, 0, wxEXPAND | wxTOP | wxBOTTOM, 3);
    return row;
}

SettingsDialog::PathRow SettingsDialog::makePathRow(wxWindow* parent, wxSizer* column, PathKind kind,
                                                    const wxString& caption, const char* name,
                                                    bool withOpen) {
    const std::string n = name;
    PathRow r;
    r.field = new wxTextCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY,
                             wxDefaultValidator, n + "Field");
    r.change = new wxButton(parent, wxID_ANY, _("Change..."), wxDefaultPosition, wxDefaultSize, 0,
                            wxDefaultValidator, n + "Change");
    r.useDefault = new wxButton(parent, wxID_ANY, _("Use default"), wxDefaultPosition, wxDefaultSize, 0,
                                wxDefaultValidator, n + "Default");
    wxBoxSizer* row = newRow(parent, column, caption);
    row->Add(r.field, 1, wxALIGN_CENTER_VERTICAL);
    row->Add(r.change, 0, wxLEFT, 6);
    row->Add(r.useDefault, 0, wxLEFT, 6);
    if (withOpen) {
        auto* open = new wxButton(parent, wxID_ANY, _("Open folder"), wxDefaultPosition, wxDefaultSize, 0,
                                  wxDefaultValidator, n + "Open");
        row->Add(open, 0, wxLEFT, 6);
        click(open, [this] { openSourcesFolder(); });
    }
    click(r.change, [this, kind] { changePath(kind); });
    click(r.useDefault, [this, kind] {
        key(kind).clear();
        showRow(kind);
    });
    rowsByKind_[static_cast<int>(kind)] = r;
    return r;
}

// Field text, "Use default" and whatever depends on the folder; called after every change of the key.
void SettingsDialog::showRow(PathKind kind) {
    const PathRow& r = rowsByKind_[static_cast<int>(kind)];
    r.field->ChangeValue(resolvedText(kind));
    r.useDefault->Enable(!key(kind).empty());
    if (kind == PathKind::Charts)
        showChartsStatus();
    else if (kind == PathKind::Cache)
        showCacheSize();
    else if (constructed_)
        scheduleCheck(); // Sources or Cli changed
}

// a chosen path equal to the default is stored empty.
void SettingsDialog::changePath(PathKind kind) {
    const PathRow& r = rowsByKind_[static_cast<int>(kind)];
    const std::optional<wxString> chosen = choosePath(kind, r.field->GetValue());
    if (!chosen)
        return;
    const std::string path = cg::toUtf8(*chosen);
    key(kind) = cg::sameDir(path, defaultPath(kind)) ? std::string() : path;
    showRow(kind);
}

// chartsStatus_: synchronous, no child process.
void SettingsDialog::showChartsStatus() {
    const wxString folder = cg::fromUtf8(config_.chartDir(pending_));
    wxString text;
    if (!wxFileName::DirExists(folder)) {
        text = _("The folder will be created at the first download.");
    } else if (!wxFileName::IsDirWritable(folder)) {
        text = _("This folder cannot be written. Choose another one.");
    } else {
        wxDiskspaceSize_t freeBytes = 0;
        text = _("OpenCPN shows the charts from this folder.");
        if (wxGetDiskSpace(folder, nullptr, &freeBytes))
            text = formatBytes(static_cast<std::int64_t>(freeBytes.GetValue())) + _(" free. ") + text;
    }
    chartsStatus_->SetLabel(text);
}

void SettingsDialog::showCacheSize() {
    cacheBytes_ = cacheBytes(config_.cliCacheRoot(pending_));
    cacheSize_->SetLabel(savedTilesText(cacheBytes_, busy_));
    clear_->Enable(cacheBytes_ > 0 && !busy_);
}

// Open folder: the sailor drops.zmp files in; a missing folder is created first.
void SettingsDialog::openSourcesFolder() {
    const wxString folder = cg::fromUtf8(config_.sourcesDir(pending_));
    if (!wxFileName::DirExists(folder) && !wxFileName::Mkdir(folder, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL))
        cg::logLine(wxString::Format("cannot create %s", folder));
    openFolder(folder);
}

// Saved tiles. The button is disabled for 0 bytes and while a download runs.
void SettingsDialog::clearSavedTiles() {
    if (question_ && question_->IsShown()) { // a second press: the open question is raised
        question_->Raise();
        return;
    }
    if (refreshBusy())
        return;
    const std::string root = config_.cliCacheRoot(pending_);
    const auto clear = [this, root](bool yes) {
        if (closing_ || !yes || refreshBusy())
            return;
        ConfigStore::clearCacheDirs(root);
        showCacheSize();
    };
    const wxString text = clearSavedTilesQuestion(cacheBytes_, cg::fromUtf8(root));
    if (IsModal()) { // opened by OpenCPN's plugin list: its nested loop is running, the question joins
                     // it
        Question::askModal(this, look_, _("Clear saved tiles"), text, Question::Buttons::DeleteCancel, clear);
        return;
    }
    question_ = Question::ask(this, look_, _("Clear saved tiles"), text, Question::Buttons::DeleteCancel,
                              wxString(), [clear](bool yes, bool) { clear(yes); });
}

// A download may start while the window is open : true when one runs now (the line and the button
// follow it).
bool SettingsDialog::refreshBusy() {
    busy_ = jobBusy_ && jobBusy_();
    if (busy_)
        showCacheSize();
    return busy_;
}

void SettingsDialog::finish(int rc) {
    if (IsModal()) {
        EndModal(rc);
        return;
    }
    const auto done = std::move(onDone); // empty on a second event before the deferred Destroy
    onDone = nullptr;
    Hide();
    Destroy(); // deferred to idle: this handler is still running
    if (done)
        done(rc == wxID_OK);
}

// The rows scroll when they do not fit 800 x 480; the button row stays visible.
void SettingsDialog::refit() {
    rows_->Layout();
    const wxSize want = rows_->GetSizer()->CalcMin();
    // 34 DIP title bar is an allowance: with no window manager (Xvfb) the tests measure the client area only.
    const int room = FromDIP(480 - 34 - 40) - bottomHeight_; // title bar, four margins of 10
    rows_->SetMinSize(wxSize(want.x, std::min(want.y, std::max(room, FromDIP(100)))));
    GetSizer()->SetSizeHints(this);
    Layout();
}

// The touch height of the collapsed header, then the dialog's fit; the body of wxEVT_COLLAPSIBLEPANE_CHANGED.
void SettingsDialog::onAdvancedChanged() {
    advanced_->SetMinSize(minHeight_ > 0 && advanced_->IsCollapsed() ? wxSize(-1, minHeight_)
                                                                     : wxDefaultSize);
    if (constructed_)
        refit();
}

// on GTK Collapse(false) sends no wxEVT_COLLAPSIBLEPANE_CHANGED, so the handler's body is called.
void SettingsDialog::expandAdvanced() {
    if (!advanced_->IsCollapsed())
        return;
    advanced_->Collapse(false);
    onAdvancedChanged();
}

void SettingsDialog::scheduleCheck() {
    checkTimer_.Start(checkDelayMs, wxTIMER_ONE_SHOT); // restarts a running delay
}

void SettingsDialog::notReady(const wxString& cliText, const wxString& sourcesText, bool expand) {
    cliStatus_->SetLabel(cliText);
    sourcesStatus_->SetLabel(sourcesText);
    cg::logLine(wxString::Format("settings check: %s", cliText));
    cg::logLine(wxString::Format("settings check: %s", sourcesText));
    if (expand)
        expandAdvanced();
}

// steps 1-5. One check at a time: a running one is terminated and its onExit starts this again.
void SettingsDialog::startCheck() {
    if (closing_)
        return;
    checkTimer_.Stop();
    if (runner_ && runner_->running()) {
        rerun_ = true;
        ++generation_; // the running check's results are stale
        runner_->terminate();
        return;
    }
    const int generation = ++generation_;
    cliStatus_->SetLabel(cliStatusText(CliStatus::Checking, {}, {}));
    sourcesStatus_->SetLabel(sourcesStatusText(SourcesStatus::Checking));

    std::string why;
    const auto cli = resolveCli(pending_, &why, cliDefaultPaths, cliPathEnv);
    if (!cli) {
        notReady(cliStatusText(pending_.cliPath.empty() ? CliStatus::NotInstalled : CliStatus::NotRunnable,
                               {}, {}),
                 sourcesStatusText(SourcesStatus::CliNotReady));
        return;
    }
    if (!probeCli(*cli)) {
        notReady(cliStatusText(CliStatus::TooOld, probedCliVersion(*cli), {}),
                 sourcesStatusText(SourcesStatus::CliNotReady));
        return;
    }
    const std::string version = probedCliVersion(*cli);
    if (cg::countSourceEntries(config_.bundledSourcesDir(), 1) == 0) { // no CLI run
        notReady(cliStatusText(CliStatus::Ready, version, {}),
                 sourcesStatusText(SourcesStatus::BundledMissing),
                 false); // the plugin's own files are missing, not the CLI: Advanced stays folded
        return;
    }
    const cg::Settings forCli = config_.cliSettings(pending_);
    const std::string folder = config_.sourcesDir(pending_);
    std::error_code ec;
    const FolderState folderState = forCli.cliMaps.size() > 1 ? FolderState::Holds
                                    : std::filesystem::is_directory(cg::fsPath(folder), ec)
                                        ? FolderState::Empty
                                        : FolderState::Missing;
    if (!runner_)
        runner_ = std::make_unique<CliRunner>();
    struct Run {
        int records = 0;
        wxString lastError;
    };
    auto run = std::make_shared<Run>();
    CliRunner::Callbacks cb;
    cb.onRecord = [run](const cg::StdoutRecord& r) {
        if (std::holds_alternative<cg::SourceRecord>(r)) {
            ++run->records;
        }
    };
    cb.onStderr = [run](const cg::StderrLine& l) {
        if (const auto* m = std::get_if<cg::MessageLine>(&l); m && m->kind == cg::MessageLine::Kind::Error)
            run->lastError = cg::fromUtf8(m->text);
    };
    cb.onExit = [this, generation, run, version, folderState](int code, bool) {
        if (closing_)
            return;
        if (generation != generation_) { // terminated by a newer check: nothing of this run is shown
            if (rerun_) {
                rerun_ = false;
                startCheck();
            }
            return;
        }
        wxString cliText, sourcesText;
        bool raceBundled = false;
        if (code == 3 && run->lastError.StartsWith("no map sources given")) { // a race: the folder went away
            cliText = cliStatusText(CliStatus::Ready, version, {});
            sourcesText = sourcesStatusText(SourcesStatus::BundledMissing);
            raceBundled = true;
        } else if (code == 0) {
            cliText = cliStatusText(CliStatus::Ready, version, {});
            sourcesText = sourcesStatusText(SourcesStatus::Found, run->records, folderState);
        } else {
            cliText =
                cliStatusText(CliStatus::Fails, {},
                              run->lastError.empty() ? wxString::Format("error %d", code) : run->lastError);
            sourcesText = sourcesStatusText(SourcesStatus::RunFailed);
        }
        cliStatus_->SetLabel(cliText);
        sourcesStatus_->SetLabel(sourcesText);
        cg::logLine(wxString::Format("settings check: %s", cliText));
        cg::logLine(wxString::Format("settings check: %s", sourcesText));
        if (code != 0 && !raceBundled)
            expandAdvanced();
    };
    std::string error;
    if (!runner_->start(cg::cliCommand(*cli, cg::sourcesArguments(forCli), false), std::move(cb), &error)) {
        notReady(cliStatusText(CliStatus::Fails, {}, cg::fromUtf8(error)),
                 sourcesStatusText(SourcesStatus::RunFailed));
    }
}
