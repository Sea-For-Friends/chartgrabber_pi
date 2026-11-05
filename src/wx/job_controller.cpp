// SPDX-License-Identifier: GPL-3.0-or-later
#include "job_controller.h"

#include "area_tool.h"
#include "config_store.h"
#include "geo_math.h"
#include "guard.h"
#include "look.h"
#include "messages.h"
#include "plugin_log.h"
#include "portable.h"
#include "progress_strip.h"
#include "wx_util.h"

#include <wx/filename.h>
#include <wx/intl.h>
#include <wx/log.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <sstream>

#include <ocpn_plugin.h>

namespace {

constexpr std::size_t kMaxMessages = 200; // the last 200 stderr message lines

bool staleExists(const std::string& p) {
    return wxFileExists(cg::fromUtf8(p));
}
bool staleRemove(const std::string& p) {
    return wxRemoveFile(cg::fromUtf8(p));
}
std::vector<std::string> removeStaleFiles(const std::vector<std::string>& stale) {
    return cg::removeStale(stale, staleExists, staleRemove);
}

} // namespace

cg::Os& weatherSwapOs() {
    static cg::Os os = cg::kHostOs;
    return os;
}

JobController::JobController(ConfigStore& config, const Look& look, AreaTool& area)
    : config_(config), look_(look), area_(area), runner_(std::make_unique<CliRunner>()), tick_(this),
      staleTimer_(this) {
    Bind(wxEVT_TIMER, &JobController::onTick, this, tick_.GetId());
    Bind(wxEVT_TIMER, &JobController::onStaleTimer, this, staleTimer_.GetId());
}

bool& staleDropAllowed() {
    static bool allowed = false;
    return allowed;
}

// Plain removal; what OpenCPN still holds (Windows) is dropped from its chart list first when the
// host is OpenCPN 5.x, x < 14 (5.14+ hangs that call). Returns the paths that remain.
std::vector<std::string> JobController::dropStale(std::vector<std::string> stale) {
    stale = removeStaleFiles(stale);
    if (stale.empty() || !staleDropAllowed())
        return stale;
    const wxArrayString dirs = GetChartDBDirArrayString();
    std::vector<std::string> left;
    for (const std::string& p : stale) {
        std::vector<std::string> candidates; // the path as OpenCPN lists it, then the record's own
        for (const wxString& d : dirs) {
            if (cg::sameDir(cg::toUtf8(d), cg::parentDir(p))) {
                candidates.push_back(cg::chartListPath(cg::toUtf8(d), p, weatherSwapOs()));
                break;
            }
        }
        if (candidates.empty() || candidates[0] != p)
            candidates.push_back(p);
        bool gone = false;
        for (const std::string& c : candidates) {
            wxString path = cg::fromUtf8(c);
            RemoveChartFromDBInPlace(path);
            cg::logLine("old weather file dropped from the chart list");
            gone = staleRemove(p);
            if (gone)
                break;
        }
        if (!gone)
            left.push_back(p);
    }
    return left;
}

void JobController::cleanupStaleAtStart() {
    bool pending = false;
    for (cg::ChartRecord r : config_.loadCharts()) {
        if (r.stale.empty())
            continue;
        std::vector<std::string> left = removeStaleFiles(r.stale);
        pending = pending || !left.empty();
        if (left == r.stale)
            continue;
        staleRemoved_ = true;
        r.stale = std::move(left);
        config_.saveChart(r);
    }
    if (staleRemoved_ || pending)
        staleTimer_.StartOnce(120000);
}

void JobController::onStaleTimer(wxTimerEvent&) {
    if (closing_)
        return;
    for (cg::ChartRecord r : config_.loadCharts()) {
        if (r.stale.empty() || (busy() && r.id == record_.id))
            continue;
        std::vector<std::string> left = dropStale(r.stale);
        if (left == r.stale)
            continue;
        staleRemoved_ = true;
        r.stale = std::move(left);
        config_.saveChart(r);
    }
    if (!staleRemoved_ || rescanned_)
        return;
    cg::guarded("stale rescan", [] {
        UpdateChartDBInplace(GetChartDBDirArrayString(), false, false);
        cg::logLine("chart list updated (old weather files removed)");
    });
}

// Windows weather update: job.ini Out= names another file, so the CLI's rename never meets the
// file OpenCPN holds open. Any failure: logged, the run goes on as today.
void JobController::swapWeatherOutput(const std::string& iniPath) {
    record_.stale = removeStaleFiles(record_.stale);
    const auto name = cg::alternateWeatherFile(record_.files[0], staleExists);
    if (!name)
        return;
    const wxString ini = cg::fromUtf8(iniPath), tmp = ini + ".cg-swap";
    std::ostringstream text;
    text << std::ifstream(cg::fsPath(iniPath), std::ios::binary).rdbuf();
    const auto rewritten = cg::rewriteJobOutputs(text.str(), *name, "");
    bool ok = false;
    if (rewritten) {
        {
            std::ofstream o(cg::fsPath(iniPath + ".cg-swap"), std::ios::binary);
            o << *rewritten;
            ok = o.good();
        }
        ok = ok && wxRenameFile(tmp, ini, true);
    }
    if (!ok) {
        cg::logLine("cannot switch the weather file name");
        return;
    }
    swapFrom_ = record_.files[0];
}

JobController::~JobController() {
    closing_ = true; // in this order
    if (next_)
        cg::logLine("weather chart not started");
    if (inBatch_ && !batch_.empty())
        cg::logLine(wxString::Format("%d weather charts not started", static_cast<int>(batch_.size())));
    tick_.Stop();
    staleTimer_.Stop();
    onChartsChanged = nullptr;
    onBusyChanged = nullptr;
    onPreferences = nullptr;
    runner_.reset();             // stop and wait up to 15 s; callbacks still arrive and only record
    if (phase_ != Phase::Idle) { // no exit arrived (detached, or a retry wait): the record says so
        record_.state = cg::ChartState::Interrupted;
        if (!lastError_.empty())
            record_.lastError = lastError_;
        saveRecord();
        phase_ = Phase::Idle;
    }
    if (strip_)
        strip_->Destroy();
}

ProgressStrip& JobController::strip() {
    if (!strip_) {
        // GetOCPNCanvasWindow() is never null while the plugin is initialised (caller: any strip() use)
        strip_ = new ProgressStrip(GetOCPNCanvasWindow(), look_, ProgressStrip::Role::Job, &config_,
                                   config_.loadSettings().stripOnChart ? ProgressStrip::Host::Canvas
                                                                       : ProgressStrip::Host::Window);
        strip_->onStop = [this] { stop(); };
    }
    return *strip_;
}

// How far the job got goes into every save of the record; an export does not change it.
void JobController::stampDone() {
    if (kind_ == cg::RunKind::BuildFromSaved)
        return;
    const bool finished =
        record_.state == cg::ChartState::Complete || record_.state == cg::ChartState::Missing;
    record_.done = finished ? 1000 : std::min(done_, 999);
}

std::optional<cg::LiveProgress> JobController::live() const {
    if (!busy())
        return std::nullopt;
    cg::LiveProgress p;
    p.chartId = record_.id;
    p.stage = phase_ == Phase::Stopping    ? cg::LiveStage::Stopping
              : phase_ == Phase::RetryWait ? cg::LiveStage::RetryWait
                                           : stage_;
    p.donePermille = done_;
    p.permille = p.stage == cg::LiveStage::Building ? exportPermille_ : (sawTotal_ || done_ > 0) ? done_ : -1;
    if (p.stage == cg::LiveStage::Downloading)
        p.secondsLeft = secondsLeft_;
    if (p.stage == cg::LiveStage::RetryWait)
        p.retrySeconds = countdown_;
    return p;
}

void JobController::saveRecord() {
    stampDone();
    config_.saveChart(record_);
    if (onChartsChanged)
        onChartsChanged();
}

void JobController::setPhase(Phase p) {
    const bool was = busy();
    phase_ = p;
    if (busy() != was && onBusyChanged)
        onBusyChanged(busy());
}

wxString JobController::sourceLabel(const std::string& guid) const {
    if (guid == record_.sourceGuid && !record_.sourceName.empty())
        return cg::fromUtf8(record_.sourceName);
    if (guid == record_.overlayGuid && !guid.empty()) {
        if (record_.sourceGuid.empty()) // an overlay-only (weather) chart: the layer is the chart's source
            return cg::fromUtf8(record_.sourceName.empty() ? guid : record_.sourceName);
        return uiText(UiText::Seamarks);
    }
    return cg::fromUtf8(guid);
}

wxString JobController::chartNameOf(const std::string& guid) const {
    if (guid == record_.overlayGuid && !guid.empty() && !record_.sourceGuid.empty())
        return cg::fromUtf8(cg::seamarksName(record_.name));
    return cg::fromUtf8(record_.name);
}

wxString JobController::detailsText() const {
    if (argv_.empty() && messages_.empty())
        return lead_;
    wxString t;
    if (!lead_.empty())
        t << lead_ << "\n";
    for (const auto& [guid, p] : progress_)
        t << sourceLabel(guid) << ": z" << p.zoom << " " << static_cast<long long>(p.processed) << "/"
          << static_cast<long long>(p.total) << "\n";
    t << "\n" << cg::fromUtf8(cg::displayCommand(argv_)) << "\n";
    for (const std::string& m : messages_)
        t << cg::fromUtf8(m) << "\n";
    return t;
}

// A result ends the job: a follow-up still queued is dropped (finishJob moved it out when it starts it).
void JobController::showResult(const wxString& headline, Buttons buttons) {
    if (next_) {
        cg::logLine("weather chart not started");
        next_.reset();
    }
    wxString shown = headline;
    const bool openDetails = keptFirstSuccess_;
    if (!keptHeadline_.empty()) // the follow-up's result: the first job's headline above it
        shown = keptHeadline_ + "\n" + headline;
    keptHeadline_.clear();
    keptFirstSuccess_ = false;
    strip().showResult(shown, detailsText(), std::move(buttons));
    if (openDetails)
        strip().openDetails(true);
}

void JobController::queueAfter(cg::ChartRecord next) {
    if (!busy() || closing_) {
        cg::logLine("no job to follow");
        return;
    }
    if (next_)
        cg::logLine("weather chart replaced by a newer one");
    next_ = std::move(next);
}

void JobController::addMessage(std::string line) {
    messages_.push_back(std::move(line));
    if (messages_.size() > kMaxMessages)
        messages_.pop_front();
    if (phase_ != Phase::Idle && strip_)
        strip_->setDetails(detailsText());
}

// Every Resume continues an unfinished update with the same date; a weather chart is
// never updated by date.
cg::RunKind JobController::resumeKind(const cg::ChartRecord& r) {
    return r.weather || r.updateBefore.empty() ? cg::RunKind::Download : cg::RunKind::Update;
}

void JobController::start(cg::ChartRecord record, cg::RunKind kind, bool overwrite) {
    cg::guarded("job start", [&] {
        if (busy() || closing_) {
            cg::logLine("job already running");
            return;
        }
        record_ = std::move(record);
        lead_ = std::move(keptDetails_); // the first job's lines of a weather follow-up, else empty
        keptDetails_.clear();
        kind_ = kind;
        settings_ = config_.loadSettings();
        newChart_ = record_.state == cg::ChartState::Running;
        restoreState_ = record_.state;
        argv_.clear();
        messages_.clear();
        progress_.clear();
        runBytes_.clear();
        fills_.clear();
        written_.clear();
        pictureLines_.clear();
        prevSnapshot_ = record_.snapshot;
        prevModelRun_ = record_.modelRun;
        newModelRun_.clear();
        lastError_.clear();
        finishedBytes_ = 0;
        // a Resume starts the bar where the job stopped; Top up, Update, an export and a weather chart (it
        // may take a newer picture) start at 0
        done_ = restoreState_ == cg::ChartState::Interrupted && kind != cg::RunKind::BuildFromSaved &&
                        !record_.weather
                    ? record_.done
                    : 0;
        stage_ = cg::LiveStage::Sizing;
        exportPermille_ = 0;
        sawTotal_ = false;
        secondsLeft_.reset();
        quietTicks_ = 0;
        retry_ = {};
        userStopped_ = budgetStopped_ = false;
        swapFrom_.clear();
        const Buttons closeOnly{{closeButtonText(), [this] { strip().Hide(); }}};

        const std::string iniPath = config_.jobsDir() + "/" + record_.id + "/job.ini";
        if (!wxFileName::FileExists(cg::fromUtf8(iniPath))) {
            record_.state = cg::ChartState::Failed;
            saveRecord();
            if (batchNext(BatchEnd::Failed, false))
                return;
            showResult(jobFileMissingText(), closeOnly);
            return;
        }
        if (weatherSwapOs() == cg::Os::Windows && record_.weather && !newChart_ &&
            kind == cg::RunKind::Download && record_.files.size() == 1 && staleExists(record_.files[0]))
            swapWeatherOutput(iniPath);
        // A chart that is already on the list (Resume, Update, Top up) goes back to its folder; a new chart's
        // folder is checked by the New chart page (its only caller), where it may differ from Settings.
        if (!newChart_ && !record_.files.empty()) {
            const wxString folder = wxFileName(cg::fromUtf8(record_.files.front())).GetPath();
            const wxString refusal =
                chartFolderRefusal(folder, cg::sameDir(cg::toUtf8(folder), config_.chartDir(cg::Settings{})));
            if (!refusal.empty()) {
                startFailed({}, refusal);
                return;
            }
        }
        std::string why;
        const auto cli = resolveCli(settings_, &why, cliDefaultPaths, cliPathEnv);
        if (!cli) {
            startFailed(why);
            return;
        }
        const cg::Settings cliSettings = config_.cliSettings(settings_);
        if (!probeCli(*cli, cliSettings)) {
            startFailed({}, cliTooOldText());
            return;
        }
        // The plugin's own cache root: created here so the CLI and the disk check find it; failure only
        // logged.
        if (kind != cg::RunKind::BuildFromSaved &&
            !wxFileName::Mkdir(cg::fromUtf8(cliSettings.cacheDir), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL))
            cg::logLine(wxString::Format("cannot create %s", cg::fromUtf8(cliSettings.cacheDir)));
        argv_ = cg::cliCommand(
            *cli, cg::runArguments(iniPath, kind, overwrite, cliSettings, record_.updateBefore), true);
#ifndef NDEBUG
        if (const char* extra = std::getenv("CG_EXTRA_ARGS")) { // debug builds only: split on ASCII spaces
            std::string part;
            for (const char* c = extra;; ++c) {
                if (*c == ' ' || *c == '\0') {
                    if (!part.empty())
                        argv_.push_back(part);
                    part.clear();
                    if (*c == '\0')
                        break;
                } else {
                    part += *c;
                }
            }
        }
#endif
        record_.state = cg::ChartState::Running;
        saveRecord();
        setPhase(Phase::Running);
        const std::string& first = record_.sourceGuid.empty() ? record_.overlayGuid : record_.sourceGuid;
        strip().showProgress(
            phaseLine(kind != cg::RunKind::BuildFromSaved ? "download" : "export", sourceLabel(first)), -1,
            stopButtonText());
        strip().setDetails(detailsText());
        tick_.Start(1000);
        beginRun();
    });
}

// Starts (or restarts, after a retry wait) the child with argv_; false = it did not start (StartFailed
// shown).
bool JobController::beginRun() {
    progress_.clear();
    runBytes_.clear();
    fills_.clear();
    written_.clear();
    lastError_.clear(); // a retry run starts without the error line of the run before
    CliRunner::Callbacks cb;
    cb.onRecord = [this](const cg::StdoutRecord& r) { cg::guarded("job record", [&] { onRecord(r); }); };
    cb.onStderr = [this](const cg::StderrLine& l) { cg::guarded("job stderr", [&] { onStderr(l); }); };
    cb.onExit = [this](int code, bool) { cg::guarded("job exit", [&] { onExit(code); }); };
    std::string error;
    if (runner_->start(argv_, std::move(cb), &error))
        return true;
    startFailed(error);
    return false;
}

// StartFailed: a new chart becomes Interrupted, any other keeps the state it had.
void JobController::startFailed(const std::string& why, const wxString& headline) {
    tick_.Stop();
    record_.state = newChart_ ? cg::ChartState::Interrupted : restoreState_;
    saveRecord();
    if (!why.empty())
        messages_.push_back("error: " + why);
    setPhase(Phase::Idle);
    const wxString shown =
        headline.empty() ? outcomeHeadline(cg::Outcome::StartFailed, {}, {}, {}) : headline;
    const Buttons buttons{{preferencesButtonText(),
                           [this] {
                               if (onPreferences)
                                   onPreferences();
                           }},
                          {closeButtonText(), [this] { strip().Hide(); }}};
    if (batchNext(BatchEnd::NotStarted, true, shown, buttons)) // the CLI would be missing for every chart
        return;
    showResult(shown, buttons);
}

void JobController::onRecord(const cg::StdoutRecord& r) {
    if (closing_)
        return;
    if (const auto* f = std::get_if<cg::FillRecord>(&r))
        fills_.push_back(*f);
    else if (const auto* w = std::get_if<cg::WrittenRecord>(&r))
        written_.push_back(*w);
    else if (const auto* s = std::get_if<cg::SnapshotRecord>(&r)) {
        newModelRun_ = s->modelRun; // the LAST snapshot record is the frame used
        addPictureLine(s->guid,
                       weatherPictureLine(cg::fromUtf8(cg::snapshotTimeText(s->time))).utf8_string());
    }
}

// After a step back the CLI prints two snapshot records of one source; the LAST is the frame used (CLI
// 2026.10.10), so a later line of the same GUID replaces the earlier one in Details.
void JobController::addPictureLine(const std::string& guid, std::string line) {
    const auto old = pictureLines_.find(guid);
    if (old == pictureLines_.end()) {
        pictureLines_[guid] = line;
        addMessage(std::move(line));
        return;
    }
    const auto it = std::find(messages_.rbegin(), messages_.rend(), old->second);
    old->second = line;
    if (it == messages_.rend()) { // dropped by the 200-line cap
        addMessage(std::move(line));
        return;
    }
    *it = std::move(line);
    if (phase_ != Phase::Idle && strip_)
        strip_->setDetails(detailsText());
}

void JobController::onStderr(const cg::StderrLine& l) {
    if (const auto* m = std::get_if<cg::MessageLine>(&l)) {
        if (m->kind == cg::MessageLine::Kind::Error)
            lastError_ = m->text;
        if (closing_)
            return;
        const char* prefix = m->kind == cg::MessageLine::Kind::Error     ? "error: "
                             : m->kind == cg::MessageLine::Kind::Warning ? "warning: "
                                                                         : "";
        addMessage(prefix + m->text);
        return;
    }
    if (closing_)
        return;
    const bool running = phase_ == Phase::Running;
    if (const auto* ph = std::get_if<cg::PhaseRecord>(&l)) {
        if (ph->name == "fill") {
            stage_ = cg::LiveStage::Checking;
        } else if (ph->name == "export") {
            stage_ = cg::LiveStage::Building;
            exportPermille_ = 0;
        } else {
            stage_ = sawTotal_ ? cg::LiveStage::Downloading : cg::LiveStage::Sizing;
        }
        if (running)
            strip().showProgress(phaseLine(ph->name, sourceLabel(ph->guid)), -1, stopButtonText());
    } else if (const auto* p = std::get_if<cg::DownloadProgress>(&l)) {
        auto it = std::find_if(progress_.begin(), progress_.end(),
                               [&](const auto& e) { return e.first == p->guid; });
        if (it == progress_.end())
            progress_.emplace_back(p->guid, *p);
        else
            it->second = *p;
        runBytes_[p->guid] = p->downloadedBytes;
        done_ = std::max(done_, cg::jobPermille(progress_, {record_.sourceGuid, record_.overlayGuid}));
        secondsLeft_ = cg::secondsLeft(*p);
        if (p->total > 0) {
            sawTotal_ = true;
            stage_ = cg::LiveStage::Downloading;
        }
        if (running)
            strip().showProgress(progressLine(sourceLabel(p->guid), *p),
                                 p->total > 0 ? static_cast<int>(p->processed * 1000 / p->total) : -1,
                                 stopButtonText());
        if (strip_)
            strip_->setDetails(detailsText());
        // the sum over the finished runs and the current one against the budget.
        std::int64_t total = finishedBytes_;
        for (const auto& [guid, bytes] : runBytes_)
            total += bytes;
        if (running && settings_.slowConnection && !budgetStopped_ &&
            total > static_cast<std::int64_t>(settings_.dataBudgetMb) * 1'000'000) {
            budgetStopped_ = true;
            requestTerminate();
        }
    } else if (const auto* e = std::get_if<cg::ExportProgress>(&l)) {
        stage_ = cg::LiveStage::Building;
        exportPermille_ = e->permille;
        if (running)
            strip().showProgress(exportLine(chartNameOf(e->guid), e->permille), e->permille,
                                 stopButtonText());
    }
}

void JobController::requestTerminate() {
    setPhase(Phase::Stopping);
    runner_->terminate();
    strip().showProgress(stoppingText(), -1, stopButtonText());
}

void JobController::stop() {
    cg::guarded("job stop", [&] {
        if (closing_)
            return;
        switch (phase_) {
        case Phase::Running:
            userStopped_ = true;
            requestTerminate();
            break;
        case Phase::Stopping: // a second press does nothing: the runner kills after its grace
            break;
        case Phase::RetryWait:
            tick_.Stop();
            record_.state = cg::ChartState::Interrupted;
            saveRecord();
            setPhase(Phase::Idle);
            if (batchNext(BatchEnd::Failed, true))
                break;
            showResult(outcomeHeadline(cg::Outcome::Interrupted, {}, {}, {}),
                       buttonsFor(cg::Outcome::Interrupted));
            break;
        case Phase::Idle:
            break;
        }
    });
}

void JobController::onTick(wxTimerEvent&) {
    cg::guarded("job timer", [&] {
        if (closing_)
            return;
        if (phase_ == Phase::Running && kind_ != cg::RunKind::BuildFromSaved && ++quietTicks_ >= 60) {
            // a power cut leaves a value at most a minute old; no onChartsChanged (it rebuilds every row)
            quietTicks_ = 0;
            stampDone();
            config_.saveChart(record_);
        } else if (phase_ == Phase::RetryWait) {
            if (--countdown_ > 0) {
                strip().showProgress(retryText(retry_.attempt, countdown_), -1, stopButtonText());
            } else {
                setPhase(Phase::Running);
                beginRun();
            }
        }
    });
}

void JobController::onExit(int exitCode) {
    if (closing_) { // whatever the exit, the record says Interrupted and nothing else happens
        record_.state = cg::ChartState::Interrupted;
        if (!lastError_.empty())
            record_.lastError = lastError_;
        saveRecord();
        phase_ = Phase::Idle;
        return;
    }
    for (const auto& [guid, bytes] : runBytes_)
        finishedBytes_ += bytes;
    runBytes_.clear();
    const cg::Outcome outcome = cg::classifyExit(exitCode, lastError_, false);
    if (outcome == cg::Outcome::Stalled && !userStopped_ && !budgetStopped_ &&
        cg::shouldRetry(outcome, settings_, retry_)) {
        ++retry_.attempt;
        countdown_ = cg::kRetryDelaySec;
        setPhase(Phase::RetryWait);
        strip().showProgress(retryText(retry_.attempt, countdown_), -1, stopButtonText());
        return;
    }
    finishJob(outcome);
}

JobController::Buttons JobController::buttonsFor(cg::Outcome o) {
    const wxString close = closeButtonText();
    auto closeFn = [this] { strip().Hide(); };
    switch (o) {
    case cg::Outcome::Done:
        return {{showButtonText(), [this, box = record_.bbox] { showOnChart(box, area_); }},
                {close, closeFn}};
    case cg::Outcome::ExportFailed:
        if (record_.weather) // an export of a weather source takes the source's last picture
            return {{resumeButtonText(), [this, rec = record_] { start(rec, cg::RunKind::Download, true); }},
                    {close, closeFn}};
        return {{buildFromSavedButtonText(),
                 [this, rec = record_] { start(rec, cg::RunKind::BuildFromSaved, true); }},
                {close, closeFn}};
    case cg::Outcome::Stalled:
    case cg::Outcome::StorageError:
    case cg::Outcome::DownloadIncomplete:
    case cg::Outcome::Interrupted:
    case cg::Outcome::Killed:
        return {{resumeButtonText(), [this, rec = record_] { start(rec, resumeKind(rec), true); }},
                {close, closeFn}};
    default:
        return {{close, closeFn}};
    }
}

// The result of a job whose child ended: record update, registration, hints, strip.
void JobController::finishJob(cg::Outcome outcome) {
    tick_.Stop();
    cg::FillTotals fill;
    for (const std::string& guid : {record_.sourceGuid, record_.overlayGuid}) {
        if (guid.empty())
            continue;
        const cg::FillTotals f = cg::sumFill(fills_, guid);
        fill.tiles += f.tiles;
        fill.missing += f.missing;
        fill.bytes += f.bytes;
    }
    std::vector<wxString> names;
    std::vector<std::string> files;
    std::int64_t bytes = 0;
    std::string newSnapshot;
    for (const cg::WrittenRecord& w : written_) {
        const std::string path = cg::nativeSeparators(w.path); // the CLI prints '/' on Windows
        names.push_back(wxFileName(cg::fromUtf8(path)).GetName());
        files.push_back(path);
        bytes += w.bytes;
        if (!w.snapshot.empty()) // the last non-empty one: a seamark overlay's empty field leaves the base's
            newSnapshot = record_.snapshot = w.snapshot;
    }
    // a weather update that took the picture the file already holds leaves OpenCPN's list alone
    const bool samePicture = outcome == cg::Outcome::Done && record_.weather &&
                             !cg::pictureChanged(prevSnapshot_, prevModelRun_, newSnapshot, newModelRun_);
    if (!newModelRun_.empty() && !newSnapshot.empty()) // only with the file it belongs to
        record_.modelRun = newModelRun_;
    if (samePicture && !swapFrom_.empty() && !files.empty() && files[0] != swapFrom_) {
        removeStaleFiles({files[0]}); // OpenCPN never listed the new name: nothing holds it
        files = {swapFrom_};
    }
    if (!files.empty()) {
        record_.files = files;
        record_.bytes = bytes;
    }
    const bool budget = budgetStopped_ && outcome == cg::Outcome::Interrupted;
    // another run of this job holds the lock; nothing ran, the chart stays as it was
    const bool locked = cg::runLocked(outcome, lastError_);
    if (locked) {
        record_.state = restoreState_;
    } else if (outcome == cg::Outcome::Done) {
        if (!fills_.empty()) { // a build from saved tiles prints no fill records: its numbers stay
            record_.tiles = fill.tiles;
            record_.missing = fill.missing;
        }
        fill.tiles = record_.tiles;
        fill.missing = record_.missing;
        record_.state = record_.missing > 0 ? cg::ChartState::Missing : cg::ChartState::Complete;
        record_.lastError.clear();
        // a finished chart has no pending update; any other end keeps its date for Resume
        record_.updateBefore.clear();
    } else {
        record_.lastError = lastError_;
        switch (outcome) {
        case cg::Outcome::Refused:
        case cg::Outcome::ExportFailed:
        case cg::Outcome::UsageError:
        case cg::Outcome::InternalError:
            record_.state = cg::ChartState::Failed;
            break;
        default:
            record_.state = cg::ChartState::Interrupted;
        }
    }
    bool firstSuccess = false;
    wxString headline = locked   ? runLockedText()
                        : budget ? budgetStopText(settings_.dataBudgetMb)
                                 : outcomeHeadline(outcome, fill, names, lastError_);
    const bool swapped = outcome == cg::Outcome::Done && !swapFrom_.empty() && !record_.files.empty() &&
                         record_.files[0] != swapFrom_;
    if (swapped) { // the old file leaves the record; OpenCPN may still hold it
        record_.stale.push_back(swapFrom_);
        record_.stale = removeStaleFiles(record_.stale);
    }
    swapFrom_.clear();
    if (outcome == cg::Outcome::Done) {
        if (samePicture) {
            cg::logLine("same weather picture; chart list not updated");
        } else if (settings_.registerCharts && !files.empty() && inBatch_) {
            batchFiles_.insert(batchFiles_.end(), files.begin(), files.end()); // one rescan at the batch end
            batchIds_.push_back(record_.id);
        } else if (settings_.registerCharts && !files.empty()) {
            const wxArrayString dirs = rescanChartList(files);
            if (!record_.stale.empty()) {
                const std::size_t before = record_.stale.size();
                record_.stale = removeStaleFiles(record_.stale);
                if (record_.stale.size() < before) { // the list now holds a missing file: once more
                    UpdateChartDBInplace(dirs, false, false);
                    cg::logLine("chart list updated again (old weather file removed)");
                }
                if (!record_.stale.empty())
                    record_.stale = dropStale(record_.stale); // no rescan: the call reloads the list itself
                if (!record_.stale.empty())
                    lead_ += staleWeatherText() + "\n";
            }
        }
        if (!config_.firstSuccessShown()) {
            lead_ +=
                firstSuccessHint() + "\n" + imageryWarningText() + "\n"; // after the first job's kept lines
            firstSuccess = true;
            config_.setFirstSuccessShown();
        }
    }
    saveRecord();
    setPhase(Phase::Idle);
    if (batchNext(locked                         ? BatchEnd::Busy
                  : outcome != cg::Outcome::Done ? BatchEnd::Failed
                  : samePicture                  ? BatchEnd::Unchanged
                                                 : BatchEnd::Updated,
                  userStopped_ || budgetStopped_ || outcome == cg::Outcome::Killed,
                  budget ? headline : wxString()))
        return;
    // the weather chart runs next, unless the sailor ended this job
    if (next_ && !userStopped_ && !budgetStopped_ && outcome != cg::Outcome::Killed) {
        keptHeadline_ = headline;
        keptDetails_ = detailsText();
        keptFirstSuccess_ = firstSuccess;
        cg::ChartRecord next = std::move(*next_);
        next_.reset();
        start(std::move(next), cg::RunKind::Download, true);
        return;
    }
    showResult(headline, buttonsFor(outcome));
    if (firstSuccess)
        strip().openDetails(true);
}

// One rescan of OpenCPN's chart folders plus the folders of `files` (a per-file add hangs 5.14+).
wxArrayString JobController::rescanChartList(const std::vector<std::string>& files) {
    wxArrayString dirs = GetChartDBDirArrayString();
    for (const std::string& f : files) {
        const std::string dir = cg::parentDir(f);
        bool listed = false;
        for (const wxString& d : dirs)
            listed = listed || cg::sameDir(cg::toUtf8(d), dir);
        if (!listed)
            dirs.Add(cg::fromUtf8(dir));
    }
    UpdateChartDBInplace(dirs, false, false);
    cg::logLine(wxString::Format("chart list updated (%d folders)", static_cast<int>(dirs.GetCount())));
    rescanned_ = true;
    return dirs;
}

void JobController::startBatch(std::vector<cg::ChartRecord> charts) {
    cg::guarded("batch start", [&] {
        if (busy() || closing_ || inBatch_) {
            cg::logLine("job already running");
            return;
        }
        if (charts.empty())
            return;
        batch_.assign(std::make_move_iterator(charts.begin()), std::make_move_iterator(charts.end()));
        inBatch_ = true;
        batchFiles_.clear();
        batchIds_.clear();
        failedNames_.clear();
        nUpdated_ = nUnchanged_ = nFailed_ = nBusy_ = nNotStarted_ = 0;
        cg::ChartRecord first = std::move(batch_.front());
        batch_.pop_front();
        start(std::move(first), cg::RunKind::Download, true);
    });
}

// A chart of the batch ended (or could not start): count it; then the next chart, or the batch result.
bool JobController::batchNext(BatchEnd how, bool stop, const wxString& reason, const Buttons& buttons) {
    if (!inBatch_)
        return false;
    switch (how) {
    case BatchEnd::Updated:
        ++nUpdated_;
        break;
    case BatchEnd::Unchanged:
        ++nUnchanged_;
        break;
    case BatchEnd::Failed:
        ++nFailed_;
        failedNames_.push_back(cg::fromUtf8(record_.name));
        break;
    case BatchEnd::Busy:
        ++nBusy_;
        break;
    case BatchEnd::NotStarted:
        ++nNotStarted_;
        break;
    }
    if (stop || batch_.empty()) {
        finishBatch(reason, buttons);
        return true;
    }
    cg::ChartRecord next = std::move(batch_.front());
    batch_.pop_front();
    start(std::move(next), cg::RunKind::Download, true);
    return true;
}

void JobController::finishBatch(const wxString& reason, const Buttons& buttons) {
    inBatch_ = false;
    if (!batchFiles_.empty()) {
        const wxArrayString dirs = rescanChartList(batchFiles_);
        // the earlier second pass: the old files OpenCPN held until the rescan
        bool removed = false, left = false;
        const std::vector<cg::ChartRecord> charts = config_.loadCharts();
        for (const std::string& id : batchIds_) {
            const auto it = std::ranges::find(charts, id, &cg::ChartRecord::id);
            if (it == charts.end() || it->stale.empty())
                continue;
            cg::ChartRecord r = *it;
            const std::size_t before = r.stale.size();
            r.stale = removeStaleFiles(r.stale);
            if (r.stale.size() < before) {
                removed = true;
                config_.saveChart(r);
            }
            left = left || !r.stale.empty();
        }
        if (removed) {
            UpdateChartDBInplace(dirs, false, false);
            cg::logLine("chart list updated again (old weather file removed)");
        }
        if (left)
            lead_ += staleWeatherText() + "\n";
    } else if (nUpdated_ == 0 && nUnchanged_ > 0) {
        cg::logLine("same weather pictures; chart list not updated");
    }
    if (nNotStarted_ + static_cast<int>(batch_.size()) > 0)
        cg::logLine(wxString::Format("%d weather charts not started",
                                     nNotStarted_ + static_cast<int>(batch_.size())));
    const int notUpdated = nNotStarted_ + static_cast<int>(batch_.size());
    batch_.clear();
    if (onChartsChanged)
        onChartsChanged();
    // a start failure or the budget stop: the reason headline above the counts, as a single update shows it
    const wxString counts =
        updateAllResultText(nUpdated_, nUnchanged_, nFailed_, nBusy_, notUpdated, failedNames_);
    showResult(reason.empty() ? counts : reason + "\n" + counts,
               buttons.empty() ? Buttons{{closeButtonText(), [this] { strip().Hide(); }}} : buttons);
}

void showOnChart(const cg::BBox& box, const AreaTool& area) {
    int w = 0, h = 0;
    if (const auto vp = area.focusViewport()) {
        w = vp->pix_width;
        h = vp->pix_height;
    } else if (wxWindow* canvas = GetOCPNCanvasWindow()) {
        const wxSize s = canvas->GetClientSize();
        const double f = OCPN_GetDisplayContentScaleFactor();
        w = static_cast<int>(s.x * f);
        h = static_cast<int>(s.y * f);
    }
    if (w <= 0 || h <= 0)
        return;
    const cg::LatLon c = cg::centre(box);
    JumpToPosition(c.lat, c.lon, cg::fitScalePpm(box, w, h));
}
