// SPDX-License-Identifier: GPL-3.0-or-later
// Preferences of the plugin : one column, top to bottom: chart
// sources, connection, saved tiles, Advanced (folders, Chart Grabber CLI, register charts), status line and
// Cancel / OK. Every folder and the program is a visible read-only field with "Change..." and "Use default".
#pragma once

#include "cli_runner.h"
#include "question.h"
#include "settings.h"

#include <wx/collpane.h> // not forward-declared: wxCollapsiblePane is a macro for wxGenericCollapsiblePane on MSW
#include <wx/dialog.h>
#include <wx/string.h>
#include <wx/timer.h>
#include <wx/weakref.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

class CliRunner;
class ConfigStore;
class Look;
class wxButton;
class wxCheckBox;
class wxScrolledWindow;
class wxSizer;
class wxBoxSizer;
class wxSpinCtrl;
class wxStaticText;
class wxTextCtrl;

class SettingsDialog : public wxDialog {
public:
    // jobBusy: read here and again when Clear... is pressed and answered (it is disabled while a download
    // runs, ); empty = never busy.
    SettingsDialog(wxWindow* parent, ConfigStore& config, const Look& look,
                   std::function<bool()> jobBusy = {});
    ~SettingsDialog()
        override; // closing_, the open question deleted, runner reset Look unsubscribed

    // The Settings window that is shown now (modal or modeless, any opener), nullptr if none: the page and
    // the strip raise it instead of opening a second one.
    static SettingsDialog* shown();

    // Modeless use : called once after the window is hidden and destroyed; `saved` = OK
    // pressed. Not called when the dialog is ShowModal'd (EndModal returns the code) or deleted by its
    // parent.
    std::function<void(bool saved)> onDone;

    // What "Change..." opens; tests replace it (no modal in a test). `current` = the path shown in the field.
    // Returns the chosen path, nullopt on Cancel. Default: PlatformDirSelectorDialog for the folders,
    // PlatformFileSelectorDialog for the program.
    enum class PathKind { Sources, Charts, Cache, Cli };
    std::function<std::optional<wxString>(PathKind kind, const wxString& current)> choosePath;
    // What "Open folder" does with the sources folder once it exists, and "Open" with the log file (a folder
    // or the log file). Default: wxLaunchDefaultApplication.
    std::function<void(const wxString& folder)> openFolder;
    // What the dialog passes to resolveCli; tests set a temp file and a temp PATH.
    std::vector<std::string> cliDefaultPaths = hostCliPaths();
    std::optional<std::string> cliPathEnv; // nullopt = the PATH variable
    //: a change of the program or the sources folder starts a check after this delay (tests may
    // shorten it before the first change; the default is the spec's 500 ms).
    int checkDelayMs = 500;

private:
    struct PathRow {
        wxTextCtrl* field = nullptr;
        wxButton* change = nullptr;
        wxButton* useDefault = nullptr;
    };
    std::string& key(PathKind kind);
    std::string automaticCli() const;
    std::string defaultPath(PathKind kind) const;
    wxString resolvedText(PathKind kind) const;
    wxBoxSizer* newRow(wxWindow* parent, wxSizer* column, const wxString& caption);
    PathRow makePathRow(wxWindow* parent, wxSizer* column, PathKind kind, const wxString& caption,
                        const char* name, bool withOpen);
    void showRow(PathKind kind);
    void changePath(PathKind kind);
    void showChartsStatus();
    void showCacheSize();
    void openSourcesFolder();
    void clearSavedTiles();
    void finish(int rc); // modal: EndModal(rc); modeless: Hide, take onDone, Destroy, call it
    bool refreshBusy();  // busy_ = jobBusy_; true when a download runs (the saved-tiles line is redone)
    void refit();
    void onAdvancedChanged();
    void expandAdvanced();
    void scheduleCheck();
    void startCheck();
    void notReady(const wxString& cliText, const wxString& sourcesText, bool expand = true);

    ConfigStore& config_;
    const Look& look_;
    cg::Settings pending_; // the loaded settings with the changes so far; saved by OK only
    std::function<bool()> jobBusy_;
    bool busy_ = false; // jobBusy at construction and at each Clear...
    wxWeakRef<Question> question_;
    std::int64_t cacheBytes_ = 0;
    int captionWidth_ = 0;
    int bottomHeight_ = 0;
    PathRow rowsByKind_[4];
    wxScrolledWindow* rows_ = nullptr;
    wxStaticText* sourcesStatus_ = nullptr;
    wxStaticText* chartsStatus_ = nullptr;
    wxStaticText* cliStatus_ = nullptr;
    wxCollapsiblePane* advanced_ = nullptr;
    int minHeight_ = 0; // Look::minControlHeight at construction
    int lookToken_ = 0;
    bool closing_ = false;
    bool constructed_ = false;
    // The automatic check : one runner, a generation per check; a change while it runs terminates
    // it and onExit starts the next one (rerun_).
    std::unique_ptr<CliRunner> runner_;
    wxTimer checkTimer_;
    int generation_ = 0;
    bool rerun_ = false;
    wxStaticText* cacheSize_ = nullptr;
    wxButton* clear_ = nullptr;
    wxSpinCtrl* budgetMb_ = nullptr;
    wxCheckBox* slow_ = nullptr;
    wxCheckBox* register_ = nullptr;
    wxCheckBox* warnDetail_ = nullptr;
    wxCheckBox* stripOnChart_ = nullptr;
    wxStaticText* stripNote_ = nullptr;
    wxTextCtrl* logField_ = nullptr;
    wxButton* logOpen_ = nullptr;
    wxCheckBox* debug_ = nullptr;
};
