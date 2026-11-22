// SPDX-License-Identifier: GPL-3.0-or-later
// CliRunner: one CLI child process with redirected pipes, parsed record callbacks, terminate / kill /
// stop-and-wait. GUI thread only; no std::thread.
// Needs a wx event loop (wxTimer, wxEVT_END_PROCESS); abandon() runs no events.
// The child gets --stop-on-stdin and an open stdin pipe; terminate() writes "stop\n" and closes it, kill()
// follows stopGraceMs() later when the child has not ended.
#pragma once

#include "cli_records.h"
#include "cli_text.h"
#include "settings.h"

#include <wx/event.h>
#include <wx/timer.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

class wxProcess;
class wxProcessEvent;

class CliRunner : public wxEvtHandler {
public:
    struct Callbacks {
        std::function<void(const cg::StdoutRecord&)> onRecord;
        std::function<void(const cg::StderrLine&)> onStderr;
        // Every stdout line that is not a record, in order, before onExit.
        std::function<void(std::string_view line)> onText;
        std::function<void(int exitCode, bool startFailed)> onExit; // after the pipes are drained
    };

    CliRunner();
    ~CliRunner(); // running: abandon(destructorWaitMs())

    // The destructor's wait (2 s). setDestructorWaitMs is for tests only.
    static constexpr int kDefaultDestructorWaitMs = 2000;
    int destructorWaitMs() const { return destructorWaitMs_; }
    void setDestructorWaitMs(int ms) { destructorWaitMs_ = ms; }

    // argv[0] = program. false (with *error, may be null): already running, empty argv, or the process did
    // not start; onExit is then not called. Each stdout line goes through cg::parseStdoutLine to onRecord
    // (text lines: onText), each stderr line through cg::parseStderrLine to onStderr, in order.
    bool start(const std::vector<std::string>& argv, Callbacks cb, std::string* error);
    bool running() const { return process_ != nullptr; }
    void terminate(); // the stop line on the child's stdin, then EOF; once per run
    void kill();      // SIGKILL (Linux) / TerminateProcess (Windows)
    // terminate(), then poll for the child's end every 50 ms without dispatching any event, at most waitMs;
    // a child still running is killed. Then the process is detached (wx reaps it later); no callback runs.
    void abandon(int waitMs);

    // Grace between the stop line and the kill (10 s). setStopGraceMs is for tests only.
    static constexpr int kDefaultStopGraceMs = 10000;
    int stopGraceMs() const { return stopGraceMs_; }
    void setStopGraceMs(int ms) { stopGraceMs_ = ms; }
    // For tests only: the next start() sees the child's end (raw exit code) as already reported inside
    // wxExecute.
    void setEndDuringNextStart(int raw) { testEnd_ = raw; }

private:
    void onTimer(wxTimerEvent&);
    void onEnd(wxProcessEvent& event);
    void drain();
    void graceCheck();
    bool childReaped() const;
    bool childExited() const; // ended: a zombie counts, unlike childReaped
    void endRun(int raw);
    void finish(int exitCode);
    void onStdoutLine(std::string_view line);
    void onStderrLine(std::string_view line);
    std::string redacted(std::string_view line) const;

    wxProcess* process_ = nullptr;
    long pid_ = 0;
    wxTimer timer_;
    Callbacks cb_;
    cg::LineSplitter outSplit_, errSplit_;
    bool terminated_ = false;
    bool stopped_ = false; // terminate() / kill() / abandon ran for this run (the exit log line)
    bool draining_ = false;
    std::string proxy_; // the --proxy value of the last start: redacted in the debug copies of CLI lines
    int destructorWaitMs_ = kDefaultDestructorWaitMs;
    int stopGraceMs_ = kDefaultStopGraceMs;
    std::chrono::steady_clock::time_point stopDeadline_;
    bool killed_ = false; // the grace kill ran for this run
    std::optional<int> testEnd_;
    bool starting_ = false;        // inside wxExecute: wx may report the end before start() returns
    std::optional<int> startExit_; // that end (raw exit code), finished by the next timer tick
    bool killSent_ = false;        // kill() ran for this run: Windows reports our kill as -1 (normalizeExit)
};

// The version check as a runner client: "<program> --version [--datadir=...]" without waiting. GUI thread
// only; needs a wx event loop. Shares the cache of probeCli (path, size, modification time); a timeout is not
// cached.
class CliProbe : private wxEvtHandler {
public:
    enum class Result {
        Supported,
        TooOld,
        NoAnswer
    }; // TooOld also: no version line, not the CLI, start failed
    using Done = std::function<void(Result)>;
    static constexpr int kDefaultTimeoutMs = 10000;

    CliProbe();
    ~CliProbe(); // a probe in flight is killed; done is not called

    // A cache hit runs done before check() returns and starts no process. Else the program is started and
    // done runs once, from the child's end or the time limit (the child is killed then). A check() while one
    // runs cancels the running one first (its done is not called).
    void check(const std::string& program, const cg::Settings& cliSettings, Done done);
    // done is not called; the child is killed. Destroying the runner polls briefly for the child's end
    // (milliseconds after SIGKILL) without dispatching events.
    void cancel();
    // False after a timeout, although the killed child may live until it is reaped.
    bool running() const { return done_ != nullptr; }
    void setTimeoutMs(int ms) { timeoutMs_ = ms; } // tests only

private:
    using Key = std::tuple<std::string, std::uintmax_t, std::filesystem::file_time_type>;
    void onTimeout(wxTimerEvent&);
    void onExit(int exitCode);

    std::unique_ptr<CliRunner> runner_; // kept after the end of a probe: it may still be inside its callback
    wxTimer timer_;
    Done done_;
    std::optional<Key> key_;
    std::string line_; // the first text line of the child's stdout
    bool haveLine_ = false;
    int timeoutMs_ = kDefaultTimeoutMs;
};

// The automatic places of the CLI before PATH on this system.
std::vector<std::string> hostCliPaths();

// Settings::cliPath if set (a program file, no fallback), else the first of defaultPaths that
// is a program file, else kDefaultCliName on pathEnv (nullopt = the PATH variable).
// nullopt: *why (may be null) says what is wrong, in English.
// "<program> --version [--datadir=<cliSettings.cliDataDir>]" synchronously (no env, no nice), the
// supported version from the version line; cached per (path, size, modification time) for the session; the
// first probe of a key writes one log line. nullopt = too old or not the Chart Grabber CLI (the caller shows
// cliTooOldText()). GUI thread only.
std::optional<cg::CliVersion> probeCli(const std::string& program, const cg::Settings& cliSettings);

// "<major>.<minor>.<patch>" of the version line probeCli parsed for `program` (same cache key, no
// new process); empty when the program was not probed, gave no version, or its probe was not cached.
std::string probedCliVersion(const std::string& program);

std::optional<std::string> resolveCli(const cg::Settings& s, std::string* why,
                                      const std::vector<std::string>& defaultPaths = hostCliPaths(),
                                      std::optional<std::string> pathEnv = std::nullopt);
