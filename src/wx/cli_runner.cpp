// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli_runner.h"

#include "guard.h"
#include "job_files.h"
#include "messages.h"
#include "plugin_log.h"
#include "portable.h"
#include "wx_util.h"

#include <wx/app.h>
#include <wx/evtloop.h>
#include <wx/intl.h>
#include <wx/log.h>
#include <wx/process.h>
#include <wx/stream.h>
#include <wx/utils.h>
#include <wx/wfstream.h>

#include <algorithm>
#include <chrono>
#include <cstdint> // ocpn_plugin.h uses uint8_t / uint64_t without including it
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <system_error>
#include <tuple>
#include <vector>

#include <ocpn_plugin.h> // GetPluginDataDir
#ifndef __WINDOWS__
#include <csignal>
#include <fcntl.h>
#include <pthread.h>
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

} // namespace

CliRunner::CliRunner() : timer_(this) {
    Bind(wxEVT_TIMER, &CliRunner::onTimer, this);
    Bind(wxEVT_END_PROCESS, &CliRunner::onEnd, this);
}

CliRunner::~CliRunner() {
    if (process_) {
        stopAndWait(destructorWaitMs_);
    }
    if (process_) { // still alive: leave it running; wx deletes the process object when it ends
        timer_.Stop();
        process_->Detach();
        process_ = nullptr;
    }
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
    if (pid == 0) {
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
                const timespec zero{0, 0};
                while (sigtimedwait(&block, nullptr, &zero) == SIGPIPE) {
                }
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

bool CliRunner::stopAndWait(int timeoutMs) {
    if (!process_) {
        return true;
    }
    terminate();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    wxEventLoopBase* loop = wxEventLoopBase::GetActive();
    if (!loop) { // the end of the process arrives as an event: without a loop it cannot be seen
        return false;
    }
    while (process_) {
        drain();
        graceCheck();
        // The end of the process arrives as an event: let the loop run for a short while (a console loop
        // does not deliver it inside YieldFor).
        loop->DispatchTimeout(20);
        if (!process_) {
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
    }
    return true;
}

void CliRunner::onTimer(wxTimerEvent&) {
    cg::guarded("runner timer", [this] {
        drain();
        graceCheck();
    });
}

void CliRunner::onEnd(wxProcessEvent& event) {
    cg::guarded("runner end", [&] {
        if (!process_) { // not ours any more (Detach race)
            return;
        }
        drain();
        const int raw = event.GetExitCode();
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
    if (cb.onExit) { // last: it may call start again
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
    if (!cb_.onRecord)
        return;
    if (const std::optional<cg::StdoutRecord> rec = cg::parseStdoutLine(line)) // text lines: nullopt
        cg::guarded("runner record callback", [&] { cb_.onRecord(*rec); });
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
