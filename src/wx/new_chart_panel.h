// SPDX-License-Identifier: GPL-3.0-or-later
// The New chart page of the main dialog : the
// area rows (current view, drawn rectangle, route corridor), the name, the status line, Close / Download.
// the chart and detail rows (sources run, seamarks, bands, Advanced zooms). fills the four
// private hooks. GUI thread only.
#pragma once

#include "cli_records.h"
#include "geo_math.h"
#include "job_files.h"
#include "question.h"
#include "settings.h"
#include "settings_dialog.h"
#include "sources_dialog.h"
#include "types.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/collpane.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/tglbtn.h>
#include <wx/timer.h>

#include <array>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

class AreaTool;
class CliRunner;
class ConfigStore;
class JobController;
class Look;

class NewChartPanel : public wxPanel {
public:
    NewChartPanel(wxWindow* parent, ConfigStore& config, const Look& look, AreaTool& area,
                  JobController& jobs);
    ~NewChartPanel() override; // closing_ = true first (the runners of / are reset after it)

    // the routes are re-read; View captures the view now; Rectangle takes box (none: View); Route
    // preselects a route (none listed: View). Ends with outlineArea(true).
    void setArea(cg::AreaKind kind, std::optional<cg::BBox> box, bool keepRoute = false);
    void refreshArea(); // setArea(current kind, current rectangle, keepRoute)
    void
    outlineArea(bool show);  // show and kind Rectangle: the rectangle is outlined on the chart, else nothing
    void reloadSources();    //

    void setBusy(bool busy); // Download <-> "A download is running" in its place
    // The height the page may take (the screen minus what the dialog adds around it); the rows above the
    // status line scroll beyond it.
    void limitHeight(int pixels);

    std::function<void()> onDraw;    // "Draw on chart" pressed -> MainDialog::drawArea
    std::function<void()> onStarted; // a job was started -> MainDialog::hideDialog

private:
    struct RouteInfo {
        wxString guid, name;
        std::vector<cg::LatLon> points;
    };

    void openMoreSources();          // modeless
    void scheduleEstimate();         // called on every input change
    void updateDownloadState();      // the enable order and texts of
    void startDownload();            // the first of the three modeless questions (steps 1-3)
    void continueDownload(int step); // the steps from `step` on (4: the replace question was answered Yes)
    void warnAfterPick(bool pickedMost, int picked); //

    void askDownload(Question::Buttons buttons, const wxString& text, const wxString& checkBox, int next);
    void beginDownload(bool overwrite); // steps 5-8: the job directory, the files, the job
    void chartFiles(wxString* base, wxString* overlay,
                    wxString* weather) const; // the output files of the page

    // . The estimate runner is the panel's own, never the job controller's.
    enum class EstimateState { None, Waiting, Running, Ok, Failed };
    wxString
    inputReason() const; // the first failing input reason (area, source, zooms, refusal); empty = valid
    cg::JobSpec jobSpec() const; // sources, area, corridor, zooms of the page (no paths, name, threads)
    const cg::SourceRecord* overlayRecord() const; // the seamark layer when "Seamarks on top" is ticked
    const cg::SourceRecord*
    weatherRecord() const; // the weather layer when "Weather on top" is ticked
    std::vector<int> zoomList() const;
    void startEstimate(); // the timer fired
    void estimateFailed(const wxString& text);
    void estimateFinished(int code);
    void showEstimate();
    wxString outputFolder() const; // the Advanced folder, else the chart folder
    wxString chartName() const;    // the Name field trimmed, without apostrophes
    void updateDefaultName();      // defaultChartName(place, source) until the user edits Name
    cg::Preflight preflight(const cg::Settings& settings, cg::PreflightInput* in) const;
    void chooseOutputFolder();

    // .
    void fillSources();                       // the chart choice from sources_ after an exit-0 run
    void sourceFailed(const wxString& text);  // status reason + Preferences button
    void pickSource(const std::string& guid); // the rest of openMoreSources: the pick into the choice
    void openPreferences();
    void chartChosen();    // wxEVT_CHOICE: base changed
    void updateSeamarks(); // the layer rules; checkbox, note (caller refits)
    void updateImagery();  // the line under the Chart row
    void fillWeather();    // the weather choice from sources_ (caller refits)
    void updateWeather();  // the row's state and the note texts (caller refits)
    void updateArea();     // area box from the kind, then updateDetail
    void updateDetail();   // an area change: Harbour rule, scale labels, drop-down texts, views
    void showRange();      // the views of range_: boxes, note, drop-down selections, levels line
    void rangeChanged();   // a tap or a pick: views, refit, estimate, Download state
    std::set<cg::Band> shownBands() const; // the boxes shown ticked
    void updateCostLines();          // the main page and Advanced lines (called by showEstimate)
    void bandToggled(std::size_t i); // a user tick / untick
    const cg::SourceRecord* findSource(std::string_view guid) const;
    const cg::SourceRecord* selectedSource() const; // nullptr: nothing selectable in the choice

    void readRoutes();
    int routeIndex(const wxString& guid) const; // -1: empty or not listed
    void captureView();
    void checkRouteArea();
    void showKind();
    void showCorridor();
    void fitDialog();
    void showStatus(const wxString& text);
    wxStaticText* caption(const wxString& text);

    const Look& look_;
    AreaTool& area_;
    JobController& jobs_;
    int lookToken_ = 0;
    bool closing_ = false;
    wxWeakRef<Question> detailQuestion_;     // the open extreme-level warning
    wxWeakRef<Question> question_;           // the open Download question; the page is disabled meanwhile
    wxWeakRef<SourcesDialog> sourcesDialog_; // modeless windows of the page; the page is disabled
    wxWeakRef<SettingsDialog> settingsDialog_; // while one is open
    int captionWidth_ = 0;
    int heightLimit_ = std::numeric_limits<int>::max(); // px, limitHeight; none (no scrolling) until set
    int statusWrap_ = 0;                                // px: a status line is broken to fit this
    wxString statusText_;                               // what status_ shows, unbroken

    cg::AreaKind kind_ = cg::AreaKind::View;
    std::optional<cg::BBox> rectangle_;
    std::vector<RouteInfo> routes_;
    wxString routeGuid_; // the last route chosen: LastChoice at creation, then the user's choice
    double corridorNm_ = 2.0;
    wxString areaReason_; // why the area is unusable; empty = usable

    ConfigStore& config_;
    cg::Settings settings_; // reloaded on every sources run (Preferences may have changed it)
    std::unique_ptr<CliRunner> sourcesRunner_;
    std::vector<cg::SourceRecord> sources_; // CLI order; kept for 's SourcesDialog
    std::string lastError_;
    bool pendingReload_ = false;
    bool sourcesRunning_ = false;
    bool firstFill_ = true; // the first fill applies LastSeamarks
    bool lastSeamarks_ = true;
    bool sourceFailure_ = false; // sourceReason_ is a failure that offers Preferences
    wxString sourceReason_;      // why no source is usable yet (empty: a source is chosen)
    std::string baseGuid_;       // the GUID selected after the last fill / choice
    std::string lastSource_;     // LastSource at creation, then the GUID the user picked in this session
    std::vector<std::string> choiceGuids_; // GUID of every choice entry
    // the weather row
    std::vector<std::string> weatherGuids_; // GUID of every weather choice entry, CLI order
    bool weatherOn_ = false;                // the user's wish: LastWeather, then the tick
    std::string weatherPick_;               // LastWeatherSource, then the user's pick
    std::optional<cg::BBox> viewBox_, areaBox_;
    std::optional<cg::ZoomRange> range_; // THE detail state; nullopt = nothing chosen
    bool harbourOk_ = true;

    // .
    std::unique_ptr<CliRunner> estimateRunner_;
    EstimateState estimateState_ = EstimateState::None;
    int estimateGen_ = 0, runGen_ = 0; // bumped on every input change / at every run start
    bool estimatePending_ = false;     // the running estimate was terminated: start again on its exit
    std::vector<cg::EstimateRecord> estimateRecords_;
    std::string estimateError_; // last Error line of the estimate run
    cg::EstimateTotals totals_;
    wxString estimateFailure_; // the status reason while estimateState_ == Failed
    bool nameEdited_ = false;  // the user typed in Name: no more default
    wxString outputDir_;       // Advanced output folder; empty = the chart folder

    wxScrolledWindow* body_ = nullptr; // every row down to the Advanced fold
    wxBoxSizer* column_ = nullptr;     // the sizer of body_
    wxBoxSizer* footer_ = nullptr;     // status line and Close / Download, below body_
    wxBoxSizer* routeRow_ = nullptr;
    wxToggleButton* view_ = nullptr;
    wxToggleButton* draw_ = nullptr;
    wxToggleButton* route_ = nullptr;
    wxStaticText* noRoutes_ = nullptr;
    wxChoice* routeChoice_ = nullptr;
    wxButton* narrower_ = nullptr;
    wxButton* wider_ = nullptr;
    wxStaticText* corridor_ = nullptr;
    wxChoice* sourceChoice_ = nullptr;
    wxButton* moreSources_ = nullptr;
    wxStaticText* imagery_ = nullptr;
    wxStaticText* termsNote_ = nullptr; //

    wxStaticText* navNote_ = nullptr;   //

    wxCheckBox* seamarks_ = nullptr;
    wxStaticText* seamarksNote_ = nullptr;
    wxBoxSizer* weatherRow_ = nullptr; // the three rows of the weather control
    wxBoxSizer* weatherNoteRow_ = nullptr;
    wxBoxSizer* weatherCoverageRow_ = nullptr;
    wxCheckBox* weather_ = nullptr;
    wxChoice* weatherChoice_ = nullptr;
    wxStaticText* weatherNote_ = nullptr;
    wxStaticText* weatherCoverage_ = nullptr;
    std::array<wxCheckBox*, 4> bandBoxes_{};
    std::array<wxStaticText*, 4> bandScales_{};
    wxStaticText* detailNote_ = nullptr;
    wxCollapsiblePane* advanced_ = nullptr;
    wxChoice* least_ = nullptr;
    wxChoice* most_ = nullptr;
    wxStaticText* levels_ = nullptr;
    wxStaticText* nextLevel_ = nullptr;
    wxSpinCtrl* threads_ = nullptr;
    wxStaticText* folder_ = nullptr;
    wxButton* chooseFolder_ = nullptr;
    wxStaticText* estimate_ = nullptr;
    wxButton* preferences_ = nullptr;
    wxTextCtrl* name_ = nullptr;
    wxStaticText* status_ = nullptr;
    wxButton* download_ = nullptr;
    wxStaticText* busy_ = nullptr;
    wxTimer estimateTimer_; // one-shot 400 ms debounce
};
