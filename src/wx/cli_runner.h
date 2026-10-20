// SPDX-License-Identifier: GPL-3.0-or-later
// CliRunner: one CLI child process with redirected pipes, parsed record callbacks, terminate / kill /
// stop-and-wait. GUI thread only; no std::thread.
// Needs a wx event loop (wxTimer, wxEVT_END_PROCESS); stopAndWait pumps it itself (false without one).
// The child's stdin is closed right after start.
#pragma once

#include "cli_records.h"
#include "cli_text.h"
#include "settings.h"

#include <wx/event.h>
#include <wx/timer.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

class wxProcess;
class wxProcessEvent;

class CliRunner : public wxEvtHandler {
public:
    struct Callbacks {
        std::function<void(const cg::StdoutRecord&)> onRecord;
        std::function<void(const cg::StderrLine&)> onStderr;
        std::function<void(int exitCode, bool startFailed)> onExit; // after the pipes are drained
    };

    CliRunner();
    ~CliRunner(); // running: stopAndWait(destructorWaitMs), then Detach

    // The destructor's wait. setDestructorWaitMs is for tests only.
    static constexpr int kDefaultDestructorWaitMs = 15000;
    int destructorWaitMs() const { return destructorWaitMs_; }
    void setDestructorWaitMs(int ms) { destructorWaitMs_ = ms; }

    // argv[0] = program. false (with *error, may be null): already running, empty argv, or the process did
    // not start; onExit is then not called. Each stdout line goes through cg::parseStdoutLine to onRecord
    // (text lines: nothing), each stderr line through cg::parseStderrLine to onStderr, in order.
    bool start(const std::vector<std::string>& argv, Callbacks cb, std::string* error);
    bool running() const { return process_ != nullptr; }
    void terminate();                // SIGTERM, once per run
    void kill();                     // SIGKILL
    bool stopAndWait(int timeoutMs); // SIGTERM, then drain and poll; true = the process ended

private:
    void onTimer(wxTimerEvent&);
    void onEnd(wxProcessEvent& event);
    void drain();
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
    bool stopped_ = false; // terminate / kill / stopAndWait ran for this run (the exit log line)
    bool draining_ = false;
    std::string proxy_; // the --proxy value of the last start: redacted in the debug copies of CLI lines
    int destructorWaitMs_ = kDefaultDestructorWaitMs;
};

// Settings::cliPath if set (an executable regular file, no fallback), else defaultPath when
// executable, else kDefaultCliName on pathEnv (nullopt = getenv("PATH")).
// nullopt: *why (may be null) says what is wrong, in English.
// "<program> --version" synchronously (no env, no nice), the supported version from the version
// line; cached per (path, size, modification time) for the session; the first probe of a key writes one log
// line. nullopt = too old or not the Chart Grabber CLI (the caller shows cliTooOldText). GUI thread only.
std::optional<cg::CliVersion> probeCli(const std::string& program);

// "<major>.<minor>.<patch>" of the version line probeCli parsed for `program` (same cache key, no
// new process); empty when the program was not probed, gave no version, or its probe was not cached.
std::string probedCliVersion(const std::string& program);

std::optional<std::string> resolveCli(const cg::Settings& s, std::string* why,
                                      const std::string& defaultPath = cg::defaultCliPath(),
                                      std::optional<std::string> pathEnv = std::nullopt);
