// SPDX-License-Identifier: GPL-3.0-or-later
// JobController: the one running job (download or build from saved tiles), its progress strip, stall
// retries, data budget, chart registration and the OpenCPN shutdown rule.
// GUI thread only; needs a display and OpenCPN, so it is proven by the window tests
// (tests/job_controller_test.cpp).
#pragma once

#include "cli_runner.h"
#include "job_files.h"
#include "job_progress.h"
#include "progress_strip.h"
#include "settings.h"

#include <wx/event.h>
#include <wx/timer.h>
#include <wx/weakref.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class AreaTool;
class ConfigStore;
class Look;

class JobController : private wxEvtHandler {
public:
    JobController(ConfigStore& config, const Look& look, AreaTool& area); // creates no window
    // What start passes to resolveCli; production keeps the defaults, tests set a temp path and
    // PATH.
    std::string cliDefaultPath = cg::defaultCliPath();
    std::optional<std::string> cliPathEnv; // nullopt = getenv("PATH")
    ~JobController(); // closing_, timers, hooks cleared, runner reset, record saved, strip deleted

    bool busy() const { return phase_ != Phase::Idle; } // from start until the result is shown
    // Precondition !busy (else logged and ignored). Reads the settings fresh; the job file is
    // jobsDir/record.id/job.ini. Sets record.state = Running, saves it, then starts the child.
    void start(cg::ChartRecord record, cg::RunKind kind, bool overwrite);
    // Download, or Update with the saved date when the record has an unfinished update: what every Resume
    // starts.
    static cg::RunKind resumeKind(const cg::ChartRecord& r);
    // The live row of My charts; nullopt when not busy. Polled once a second by MyChartsPanel.
    std::optional<cg::LiveProgress> live() const;
    // Decision 0007: when the running job ends (not by Stop, budget stop, force stop or closing) `next`
    // starts as a Download with overwrite and the two results show as one. Ignored when not busy; a second
    // call replaces it.
    void queueAfter(cg::ChartRecord next);
    void stop(); // user Stop; also cancels a retry countdown and, once offered, forces the stop

    std::function<void()> onChartsChanged;        // after every saveChart of the job
    std::function<void(bool busy)> onBusyChanged; // on every busy change
    std::function<void()> onPreferences;          // the strip's "Preferences..." button (StartFailed)

private:
    enum class Phase { Idle, Running, RetryWait, Stopping };
    using Buttons = std::vector<std::pair<wxString, std::function<void()>>>;

    bool beginRun();
    void onRecord(const cg::StdoutRecord& r);
    void onStderr(const cg::StderrLine& l);
    void onExit(int exitCode);
    void onTick(wxTimerEvent&);
    void requestTerminate();
    void finishJob(cg::Outcome outcome);
    void startFailed(const std::string& why, const wxString& headline = wxString());
    void saveRecord();
    void addMessage(std::string line); // one Details line (the last 200 are kept)
    void stampDone();                  // record_.done before a save
    void setPhase(Phase p);
    void showResult(const wxString& headline, Buttons buttons);
    Buttons buttonsFor(cg::Outcome o);
    wxString sourceLabel(const std::string& guid) const;
    wxString chartNameOf(const std::string& guid) const;
    wxString detailsText() const;
    ProgressStrip& strip();

    ConfigStore& config_;
    const Look& look_;
    AreaTool& area_;
    std::unique_ptr<CliRunner> runner_;
    wxWeakRef<ProgressStrip> strip_; // a child of the canvas: null once OpenCPN deleted it
    wxTimer tick_;
    Phase phase_ = Phase::Idle;
    bool closing_ = false;

    // The job in progress.
    cg::ChartRecord record_;
    cg::RunKind kind_ = cg::RunKind::Download;
    cg::Settings settings_;
    std::vector<std::string> argv_;
    bool newChart_ = false;                                     // the record was Running when start got it
    cg::ChartState restoreState_ = cg::ChartState::Interrupted; // the state start got
    cg::RetryState retry_;
    int countdown_ = 0;         // seconds left of a retry wait
    int stopSeconds_ = 0;       // seconds since the stop request
    bool forceOffered_ = false; // the button reads "Force stop"
    bool userStopped_ = false, budgetStopped_ = false;
    std::int64_t finishedBytes_ = 0;               // downloaded bytes of the runs of this job that ended
    std::map<std::string, std::int64_t> runBytes_; // downloaded bytes per GUID of the current run
    std::vector<std::pair<std::string, cg::DownloadProgress>> progress_; // latest per GUID, first seen first
    std::vector<cg::FillRecord> fills_;
    std::vector<cg::WrittenRecord> written_;
    std::deque<std::string> messages_; // last 200 message lines, prefixed
    std::string lastError_;
    // The bar of the job : never down within a job.
    int done_ = 0;
    cg::LiveStage stage_ = cg::LiveStage::Sizing;
    int exportPermille_ = 0;
    bool sawTotal_ = false;
    std::optional<std::int64_t> secondsLeft_; // of the DownloadProgress last received
    int quietTicks_ = 0;                      // ticks since the last quiet save of the record
    wxString lead_; // the first-success lines at the top of Details; empty otherwise
    // The weather follow-up: queued record, and what the first job hands to the second.
    std::optional<cg::ChartRecord> next_;
    wxString keptHeadline_, keptDetails_;
    bool keptFirstSuccess_ = false;
};

// Show on chart shared by the strip and My charts: JumpToPosition to the centre of the box with
// the scale that fits it into the focus canvas viewport, else into the client size of the canvas window.
void showOnChart(const cg::BBox& box, const AreaTool& area);
