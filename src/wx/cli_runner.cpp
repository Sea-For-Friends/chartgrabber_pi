// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli_runner.h"

#include "guard.h"
#include "job_files.h"
#include "messages.h"
#include "plugin_log.h"
#include "portable.h"
#include "wx_util.h"

#include <wx/app.h>
#include <wx/intl.h>
#include <wx/log.h>
#include <wx/process.h>
#include <wx/stream.h>
#include <wx/utils.h>
#include <wx/wfstream.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint> // ocpn_plugin.h uses uint8_t / uint64_t without including it
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <system_error>
#include <thread>
#include <tuple>
#include <vector>

#include <ocpn_plugin.h> // GetPluginDataDir
#ifndef __WINDOWS__
#include <csignal>
#include <fcntl.h>
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <wx/msw/wrapwin.h>
#endif

namespace {

constexpr int kPollMs = 100;
constexpr size_t kChunk = 4096;
// probeCli's cache: (path, size, mtime) -> the supported version and the version text; GUI thread only.
using ProbeKey = std::tuple<std::string, std::uintmax_t, std::filesystem::file_time_type>;
struct ProbeEntry {
    std::optional<cg::CliVersion> supported; // nullopt = too old
    std::string version;                     // "major.minor.patch" of the version line; empty = none parsed
};
std::map<ProbeKey, ProbeEntry> g_probeCache;

std::string versionText(const cg::VersionNumber& v) {
    return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
}

// The PATH variable as UTF-8; empty when unset. Windows: wxGetEnv (std::getenv is ANSI there). Linux: the raw
// bytes (wxGetEnv decodes through the locale: under the C locale a non-ASCII folder of PATH is lost).
std::string pathFromEnv() {
#ifdef __WINDOWS__
    wxString path;
    return wxGetEnv("PATH", &path) ? cg::toUtf8(path) : std::string();
#else
    const char* path = std::getenv("PATH");
    return path ? path : "";
#endif
}

// The cache key of a program; nullopt when its size or modification time cannot be read (never cached).
std::optional<ProbeKey> probeKey(const std::string& program) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(cg::fsPath(program), ec);
    if (ec)
        return std::nullopt;
    const auto mtime = std::filesystem::last_write_time(cg::fsPath(program), ec);
    if (ec)
        return std::nullopt;
    return ProbeKey{program, size, mtime};
}

// wxExecute (wx 3.2, Unix) returns a pid even when the program does not exist: the forked child prints
// "execvp(...) failed" to its stderr and exits (reported as -1). So the program is checked before the fork:
// a program with a directory part must be a program file, a bare name is looked up on PATH.
bool programExists(const std::string& program) {
    if (!cg::parentDir(program).empty()) {
        return cg::isProgramFile(program);
    }
    return cg::findOnPath(program, pathFromEnv()).has_value();
}

// Parses the version text of a finished probe, caches the answer under `key` (when there is one) and writes
// the one log line. Shared by probeCli and CliProbe.
std::optional<cg::CliVersion> settleProbe(const std::optional<ProbeKey>& key, long rc,
                                          const std::string& out) {
    const auto version = rc == 0 ? cg::parseVersionLine(out) : std::nullopt;
    const auto supported = cg::supportedCli(version) ? version : std::nullopt;
    if (key) {
        g_probeCache[*key] = {supported, version ? versionText(version->number) : std::string()};
    }
    wxString line;
    if (rc != 0 || out.empty()) {
        line = wxString::Format("no version (exit %ld)", rc);
    } else {
        std::string_view first = std::string_view(out).substr(0, out.find('\n'));
        std::size_t cut = std::min<std::size_t>(first.size(), 200);
        while (cut > 0 && cut < first.size() && (static_cast<unsigned char>(first[cut]) & 0xC0) == 0x80) {
            --cut; // never inside a UTF-8 sequence
        }
        line = cg::fromUtf8(first.substr(0, cut)).Trim();
    }
    cg::logLine(wxString::Format("tile program %s - %s", line, supported ? "supported" : "too old"));
    return supported;
}

} // namespace

CliRunner::CliRunner() : timer_(this) {
    Bind(wxEVT_TIMER, &CliRunner::onTimer, this);
    Bind(wxEVT_END_PROCESS, &CliRunner::onEnd, this);
}

CliRunner::~CliRunner() {
    if (process_)
        abandon(destructorWaitMs_);
}

bool CliRunner::start(const std::vector<std::string>& argv, Callbacks cb, std::string* error) {
    auto fail = [&](const wxString& why) {
        if (error) {
            *error = cg::toUtf8(why);
        }
        return false;
    };
    if (process_) {
        return fail(_("Chart Grabber CLI is already running."));
    }
    if (argv.empty() || argv[0].empty()) {
        return fail(_("No program given."));
    }
    if (!programExists(argv[0])) {
        return fail(wxString::Format(_("Cannot start %s."), cg::fromUtf8(argv[0])));
    }
    std::vector<wxString> text;
    for (const std::string& a : argv) {
        text.push_back(cg::fromUtf8(a));
    }
    text.push_back("--stop-on-stdin"); // last; the caller's argv stays without it
    auto* process = new wxProcess(this);
    process->Redirect();
    long pid;
    starting_ = true;
    {
        wxLogNull quiet; // a failure is reported through *error, not as a dialog
#ifdef __WINDOWS__
        // Our own command line: the argv overload of wxExecute breaks a trailing backslash.
        std::vector<std::string> withStop = argv;
        withStop.push_back("--stop-on-stdin");
        process->SetPriority(40); // BELOW_NORMAL_PRIORITY_CLASS, the twin of nice -n 10
        pid = wxExecute(cg::fromUtf8(cg::windowsCommandLine(withStop)), wxEXEC_ASYNC | wxEXEC_HIDE_CONSOLE,
                        process);
#else
        std::vector<const wchar_t*> raw;
        for (const wxString& t : text) {
            raw.push_back(t.wc_str());
        }
        raw.push_back(nullptr);
        pid = wxExecute(raw.data(), wxEXEC_ASYNC, process);
#endif
    }
    starting_ = false;
    if (testEnd_) {
        startExit_ = testEnd_;
        testEnd_.reset();
    }
    if (pid == 0) {
        startExit_.reset();
        delete process;
        return fail(wxString::Format(_("Cannot start %s."), text[0]));
    }
#ifndef __WINDOWS__
    // The stdin write end must not leak into children forked by others (OpenCPN, other plugins): they would
    // hide the EOF. Ceiling: a fork between pipe() and this fcntl inside wxExecute still inherits it
    // (cannot be closed with wxExecute).
    if (auto* out = dynamic_cast<wxFileOutputStream*>(process->GetOutputStream()))
        ::fcntl(out->GetFile()->fd(), F_SETFD, FD_CLOEXEC);
    else
        cg::logFile("cli start: no stdin pipe to mark close-on-exec");
#endif
    process_ = process;
    pid_ = pid;
    cb_ = std::move(cb);
    outSplit_ = cg::LineSplitter();
    errSplit_ = cg::LineSplitter();
    terminated_ = false;
    stopped_ = false;
    killed_ = false;
    killSent_ = false;
    proxy_.clear();
    for (std::size_t i = 0; i + 1 < argv.size(); ++i)
        if (argv[i] == "--proxy")
            proxy_ = argv[i + 1];
    timer_.Start(kPollMs);
    cg::logFile(wxString::Format("cli start pid %ld: %s (%d arguments)", pid, text[0],
                                 static_cast<int>(text.size()) - 1));
    if (cg::pluginLogDebug()) {
        std::vector<std::string> shown = argv;
        for (std::size_t i = 0; i + 1 < shown.size(); ++i)
            if (shown[i] == "--proxy") // a proxy URL can hold a password
                shown[i + 1] = "<proxy>";
        cg::logDebug("cli argv: " + cg::fromUtf8(cg::displayCommand(shown)));
    }
    return true;
}

void CliRunner::terminate() {
    if (process_)
        stopped_ = true;
    if (process_ && !terminated_) {
        terminated_ = true;
        stopDeadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(stopGraceMs_);
        wxOutputStream* in = process_->GetOutputStream();
        if (in) {
            wxLogNull quiet; // wx logs EPIPE as a dialog
#ifndef __WINDOWS__
            // OpenCPN does not ignore SIGPIPE: a write to a child that closed its stdin must not end it.
            sigset_t block, old, pending;
            sigemptyset(&block);
            sigaddset(&block, SIGPIPE);
            pthread_sigmask(SIG_BLOCK, &block, &old);
            sigpending(&pending);
            const bool wasPending = sigismember(&pending, SIGPIPE) == 1;
#endif
            in->Write("stop\n", 5); // an error is ignored: the grace kill covers it
#ifndef __WINDOWS__
            if (!wasPending) {
#ifdef __APPLE__
                // macOS has no sigtimedwait: ignoring the signal discards a pending one (POSIX sigaction),
                // then restore.
                struct sigaction ignore{}, prev{};
                ignore.sa_handler = SIG_IGN;
                sigaction(SIGPIPE, &ignore, &prev);
                sigaction(SIGPIPE, &prev, nullptr);
#else
                const timespec zero{0, 0};
                while (sigtimedwait(&block, nullptr, &zero) == SIGPIPE) {
                }
#endif
            }
            pthread_sigmask(SIG_SETMASK, &old, nullptr);
#endif
            process_->CloseOutput();
        }
    }
}

// The grace is over and the child still runs: SIGKILL, once.
void CliRunner::graceCheck() {
    if (process_ && terminated_ && !killed_ && std::chrono::steady_clock::now() >= stopDeadline_) {
        killed_ = true;
        cg::logFile(wxString::Format("cli stop pid %ld: no exit after %d ms; killed", pid_, stopGraceMs_));
        kill();
    }
}

void CliRunner::kill() {
    if (process_) {
        stopped_ = true;
        killSent_ = true;
        wxProcess::Kill(static_cast<int>(pid_), wxSIGKILL, wxKILL_NOCHILDREN);
    }
}

// No event is dispatched here: OpenCPN's timers, paints and other plugins' handlers must not run inside
// a destructor (quit, plugin disabled in the Options loop). A zombie counts as ended: wx reaps it from the
// event loop later.
void CliRunner::abandon(int waitMs) {
    if (!process_)
        return;
    terminate();
    cb_ = Callbacks();
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::milliseconds(waitMs);
    bool exited = childExited();
    int pollMs =
        2; // a just-killed child is a zombie within about a millisecond: poll short first, then 50 ms
    while (!exited && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(pollMs));
        pollMs = std::min(pollMs * 4, 50);
        exited = childExited();
    }
    if (!exited)
        kill();
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    cg::logFile(wxString::Format("cli stop pid %ld: abandoned after %lld ms (%s)", pid_,
                                 static_cast<long long>(ms.count()), exited ? "exited" : "killed"));
    timer_.Stop();
    process_->Detach();
    process_ = nullptr;
}

void CliRunner::onTimer(wxTimerEvent&) {
    cg::guarded("runner timer", [this] {
        if (startExit_) {      // wx ended the child inside wxExecute: finish it here, not inside start()
            if (childReaped()) // wx has let go of its wxProcess
                endRun(*startExit_);
            return;
        }
        drain();
        graceCheck();
    });
}

// True when the child has ended, reaped by wx or not (a zombie).
bool CliRunner::childExited() const {
#ifdef __WINDOWS__
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid_));
    if (!h)
        return true;
    const bool done = WaitForSingleObject(h, 0) == WAIT_OBJECT_0;
    CloseHandle(h);
    return done;
#else
    siginfo_t info{};
    const int rc = waitid(P_PID, static_cast<id_t>(pid_), &info, WEXITED | WNOHANG | WNOWAIT);
    return (rc == 0 && info.si_pid == static_cast<pid_t>(pid_)) || (rc == -1 && errno == ECHILD);
#endif
}

// True once wx has waited for the child (nothing left to wait for); always true on Windows.
bool CliRunner::childReaped() const {
#ifdef __WINDOWS__
    return true;
#else
    siginfo_t info{};
    return waitid(P_PID, static_cast<id_t>(pid_), &info, WEXITED | WNOHANG | WNOWAIT) == -1 &&
           errno == ECHILD;
#endif
}

void CliRunner::onEnd(wxProcessEvent& event) {
    cg::guarded("runner end", [&] {
        if (!process_) {
            if (starting_) // wx ended the child inside wxExecute: the timer finishes it
                startExit_ = event.GetExitCode();
            return; // else not ours any more (Detach race)
        }
        if (startExit_) // the end is already remembered: the timer finishes the run
            return;
        endRun(event.GetExitCode());
    });
}

void CliRunner::endRun(int raw) {
    startExit_.reset();
    cg::guarded("runner end", [&] {
        drain();
        const int code = cg::normalizeExit(raw, cg::kHostOs, killSent_);
#ifdef __WINDOWS__
        if (code != raw) // an NTSTATUS crash code stays visible
            cg::logFile(wxString::Format("cli exit pid %ld: raw code %d", pid_, raw));
#endif
        finish(code);
    });
}

// Reads both pipes empty (a full pipe would block the CLI) and feeds the line splitters.
void CliRunner::drain() {
    if (!process_ || draining_) {
        return;
    }
    draining_ = true;
    char buf[kChunk];
    auto pump = [&](wxInputStream* s, cg::LineSplitter& split, bool isStdout) {
        while (process_ && s && s->CanRead()) {
            s->Read(buf, sizeof buf);
            const size_t n = s->LastRead();
            if (n == 0) {
                break;
            }
            split.feed(std::string_view(buf, n), [&](std::string_view line) {
                if (isStdout) {
                    onStdoutLine(line);
                } else {
                    onStderrLine(line);
                }
            });
        }
    };
    pump(process_->GetInputStream(), outSplit_, true);
    pump(process_->GetErrorStream(), errSplit_, false);
    draining_ = false;
}

void CliRunner::finish(int exitCode) {
    timer_.Stop();
    outSplit_.finish([&](std::string_view line) { onStdoutLine(line); });
    errSplit_.finish([&](std::string_view line) { onStderrLine(line); });
    cg::logFile(wxString::Format("cli exit pid %ld: code %d%s", pid_, exitCode, stopped_ ? ", stopped" : ""));
    delete process_;
    process_ = nullptr;
    Callbacks cb = std::move(cb_);
    cb_ = Callbacks();
    if (cb.onExit) { // last: it may call start() again
        cg::guarded("runner exit callback", [&] { cb.onExit(exitCode, false); });
    }
}

// The line for the Debug log: the --proxy value (and its userinfo, which the CLI may print without the
// scheme) becomes <proxy>; an empty value is never replaced.
std::string CliRunner::redacted(std::string_view line) const {
    std::string out(line);
    const auto replaceAll = [&out](const std::string& what) {
        if (what.empty())
            return;
        for (std::size_t at = out.find(what); at != std::string::npos; at = out.find(what, at + 7))
            out.replace(at, what.size(), "<proxy>");
    };
    replaceAll(proxy_);
    const std::size_t scheme = proxy_.find("://"), at = proxy_.rfind('@');
    if (scheme != std::string::npos && at != std::string::npos && at > scheme + 3)
        replaceAll(proxy_.substr(scheme + 3, at - scheme - 3));
    return out;
}

void CliRunner::onStdoutLine(std::string_view line) {
    if (cg::pluginLogDebug())
        cg::logDebug("cli out: " + cg::fromUtf8(redacted(line)));
    if (!cb_.onRecord && !cb_.onText)
        return;
    if (const std::optional<cg::StdoutRecord> rec = cg::parseStdoutLine(line)) { // text lines: nullopt
        if (cb_.onRecord)
            cg::guarded("runner record callback", [&] { cb_.onRecord(*rec); });
    } else if (cb_.onText) {
        cg::guarded("runner text callback", [&] { cb_.onText(line); });
    }
}

void CliRunner::onStderrLine(std::string_view line) {
    const cg::StderrLine parsed = cg::parseStderrLine(line);
    if (cg::pluginLogDebug() && // progress records come about once a second and add nothing the strip did not
                                // show
        !std::holds_alternative<cg::DownloadProgress>(parsed) &&
        !std::holds_alternative<cg::ExportProgress>(parsed))
        cg::logDebug("cli err: " + cg::fromUtf8(redacted(line)));
    if (!cb_.onStderr)
        return;
    cg::guarded("runner stderr callback", [&] { cb_.onStderr(parsed); });
}

std::optional<cg::CliVersion> probeCli(const std::string& program, const cg::Settings& cliSettings) {
    const std::optional<ProbeKey> key = probeKey(program);
    if (key) {
        if (const auto it = g_probeCache.find(*key); it != g_probeCache.end()) {
            return it->second.supported;
        }
    }
    std::vector<std::string> args{program};
    for (std::string& a : cg::versionArguments(cliSettings))
        args.push_back(std::move(a));
    wxProcess child;
    child.Redirect();
    long rc;
    {
        wxLogNull quiet; // a failure is the answer "no version", not a dialog
#ifdef __WINDOWS__
        rc = wxExecute(cg::fromUtf8(cg::windowsCommandLine(args)),
                       wxEXEC_SYNC | wxEXEC_NOEVENTS | wxEXEC_HIDE_CONSOLE, &child);
#else
        std::vector<wxString> strs;
        for (const std::string& a : args)
            strs.push_back(cg::fromUtf8(a));
        std::vector<const wchar_t*> argv;
        for (const wxString& w : strs)
            argv.push_back(w.wc_str());
        argv.push_back(nullptr);
        rc = wxExecute(argv.data(), wxEXEC_SYNC | wxEXEC_NOEVENTS, &child);
#endif
    }
    std::string out;
    if (rc == 0 && child.GetInputStream()) {
        char buf[kChunk];
        wxInputStream* s = child.GetInputStream();
        while (s->CanRead()) {
            s->Read(buf, sizeof buf);
            if (s->LastRead() == 0) {
                break;
            }
            out.append(buf, s->LastRead());
        }
    }
    return settleProbe(key, rc, out);
}

std::string probedCliVersion(const std::string& program) {
    const std::optional<ProbeKey> key = probeKey(program);
    if (!key)
        return std::string();
    const auto it = g_probeCache.find(*key);
    return it == g_probeCache.end() ? std::string() : it->second.version;
}

std::vector<std::string> hostCliPaths() {
#ifdef __WINDOWS__
    // ProgramW6432 first: OpenCPN is a 32-bit process, where %ProgramFiles% is "Program Files (x86)".
    wxString programFiles;
    if (!wxGetEnv("ProgramW6432", &programFiles))
        wxGetEnv("ProgramFiles", &programFiles);
    return cg::defaultCliPaths(cg::Os::Windows, cg::toUtf8(GetPluginDataDir("chartgrabber_pi")),
                               cg::toUtf8(programFiles));
#else
    return cg::defaultCliPaths(cg::Os::Posix, cg::toUtf8(GetPluginDataDir("chartgrabber_pi")), "");
#endif
}

std::optional<std::string> resolveCli(const cg::Settings& s, std::string* why,
                                      const std::vector<std::string>& defaultPaths,
                                      std::optional<std::string> pathEnv) {
    auto fail = [&](const wxString& reason) -> std::optional<std::string> {
        if (why) {
            *why = cg::toUtf8(reason);
        }
        return std::nullopt;
    };
    if (!s.cliPath.empty()) {
        if (!cg::isProgramFile(s.cliPath)) {
            return fail(wxString::Format(_("%s cannot be run as a program."), cg::fromUtf8(s.cliPath)));
        }
        return s.cliPath;
    }
    for (const std::string& path : defaultPaths) {
        if (cg::isProgramFile(path)) {
            return path;
        }
    }
    if (auto found = cg::findOnPath(cg::kDefaultCliName, pathEnv ? *pathEnv : pathFromEnv())) {
        return found;
    }
    return fail(cliNotFoundText());
}

CliProbe::CliProbe() : timer_(this) {
    Bind(wxEVT_TIMER, &CliProbe::onTimeout, this);
}

CliProbe::~CliProbe() {
    cancel();
}

void CliProbe::cancel() {
    timer_.Stop();
    done_ = nullptr;
    if (runner_ && runner_->running()) { // an idle runner stays: cancel may run inside its exit callback
        runner_->kill();
        runner_.reset();
    }
}

void CliProbe::check(const std::string& program, const cg::Settings& cliSettings, Done done) {
    cancel();
    key_ = probeKey(program);
    if (key_) {
        if (const auto it = g_probeCache.find(*key_); it != g_probeCache.end()) {
            done(it->second.supported ? Result::Supported : Result::TooOld);
            return;
        }
    }
    line_.clear();
    haveLine_ = false;
    done_ = std::move(done);
    if (!runner_)
        runner_ = std::make_unique<CliRunner>();
    CliRunner::Callbacks cb;
    cb.onText = [this](std::string_view line) {
        if (!haveLine_) {
            haveLine_ = true;
            line_ = std::string(line);
        }
    };
    cb.onExit = [this](int code, bool) { onExit(code); };
    std::string error;
    if (!runner_->start(cg::cliCommand(program, cg::versionArguments(cliSettings), false), std::move(cb),
                        &error)) {
        const bool supported = static_cast<bool>(settleProbe(key_, -1, std::string()));
        Done d = std::move(done_);
        done_ = nullptr;
        d(supported ? Result::Supported : Result::TooOld);
        return;
    }
    timer_.StartOnce(timeoutMs_);
}

void CliProbe::onExit(int exitCode) {
    if (!done_) // cancelled or timed out: the child's end is of no interest
        return;
    timer_.Stop();
    const bool supported =
        static_cast<bool>(settleProbe(key_, exitCode, exitCode == 0 ? line_ : std::string()));
    Done d = std::move(done_);
    done_ = nullptr;
    d(supported ? Result::Supported : Result::TooOld);
}

void CliProbe::onTimeout(wxTimerEvent&) {
    if (!done_)
        return;
    if (runner_)
        runner_->kill();
    cg::logLine("tile program did not answer in 10 s; stopped");
    Done d = std::move(done_);
    done_ = nullptr;
    d(Result::NoAnswer);
}
