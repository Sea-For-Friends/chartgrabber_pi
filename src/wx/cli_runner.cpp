// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli_runner.h"

#include "guard.h"
#include "job_files.h"
#include "messages.h"
#include "plugin_log.h"
#include "wx_util.h"

#include <wx/app.h>
#include <wx/evtloop.h>
#include <wx/intl.h>
#include <wx/log.h>
#include <wx/process.h>
#include <wx/stream.h>
#include <wx/utils.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <system_error>
#include <tuple>
#include <unistd.h>

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

// The cache key of a program; nullopt when its size or modification time cannot be read (never cached).
std::optional<ProbeKey> probeKey(const std::string& program) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(program, ec);
    if (ec)
        return std::nullopt;
    const auto mtime = std::filesystem::last_write_time(program, ec);
    if (ec)
        return std::nullopt;
    return ProbeKey{program, size, mtime};
}

// wx 3.2 on Unix reports a normal exit code as a signed 8-bit value (exit 143 -> -113, 130 -> -126) and a
// signalled child as -signal (SIGTERM -15, SIGKILL -9).
// Signals are 1..64, so a value below -64 is an exit code 129..191 and is restored here; the CLI's own codes
// are 0-5, 130 and 143. -1 is ambiguous (execvp failure or SIGHUP); start checks the program first, so it
// is practically SIGHUP.
int normalizeExit(int wxCode) {
    return wxCode < -64 ? wxCode + 256 : wxCode;
}

bool isExecutableFile(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec) && ::access(path.c_str(), X_OK) == 0;
}

// wxExecute (wx 3.2, Unix) returns a pid even when the program does not exist: the forked child prints
// "execvp(...) failed" to its stderr and exits (reported as -1). So the program is checked before the fork.
bool programExists(const std::string& program) {
    if (program.find('/') != std::string::npos) {
        return isExecutableFile(program);
    }
    const char* path = std::getenv("PATH");
    return cg::findOnPath(program, path ? path : "").has_value();
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
    std::vector<const wchar_t*> raw;
    for (const wxString& t : text) {
        raw.push_back(t.wc_str());
    }
    raw.push_back(nullptr);

    auto* process = new wxProcess(this);
    process->Redirect();
    long pid;
    {
        wxLogNull quiet; // a failure is reported through *error, not as a dialog
        pid = wxExecute(raw.data(), wxEXEC_ASYNC, process);
    }
    if (pid == 0) {
        delete process;
        return fail(wxString::Format(_("Cannot start %s."), text[0]));
    }
    process->CloseOutput(); // the CLI reads no stdin: give it EOF instead of an open pipe
    process_ = process;
    pid_ = pid;
    cb_ = std::move(cb);
    outSplit_ = cg::LineSplitter();
    errSplit_ = cg::LineSplitter();
    terminated_ = false;
    stopped_ = false;
    proxy_.clear();
    for (std::size_t i = 0; i + 1 < argv.size(); ++i)
        if (argv[i] == "--proxy")
            proxy_ = argv[i + 1];
    timer_.Start(kPollMs);
    cg::logFile(wxString::Format("cli start pid %ld: %s (%d arguments)", pid, text[0],
                                 static_cast<int>(argv.size()) - 1));
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
        wxProcess::Kill(static_cast<int>(pid_), wxSIGTERM);
    }
}

void CliRunner::kill() {
    if (process_) {
        stopped_ = true;
        wxProcess::Kill(static_cast<int>(pid_), wxSIGKILL);
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
    cg::guarded("runner timer", [this] { drain(); });
}

void CliRunner::onEnd(wxProcessEvent& event) {
    cg::guarded("runner end", [&] {
        if (!process_) { // not ours any more (Detach race)
            return;
        }
        drain();
        finish(normalizeExit(event.GetExitCode()));
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

std::optional<cg::CliVersion> probeCli(const std::string& program) {
    const std::optional<ProbeKey> key = probeKey(program);
    if (key) {
        if (const auto it = g_probeCache.find(*key); it != g_probeCache.end()) {
            return it->second.supported;
        }
    }
    const wxString prog = cg::fromUtf8(program);
    const wxString opt = "--version";
    const wchar_t* argv[] = {prog.wc_str(), opt.wc_str(), nullptr};
    wxProcess child;
    child.Redirect();
    long rc;
    {
        wxLogNull quiet; // a failure is the answer "no version", not a dialog
        rc = wxExecute(argv, wxEXEC_SYNC | wxEXEC_NOEVENTS, &child);
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

std::optional<std::string> resolveCli(const cg::Settings& s, std::string* why, const std::string& defaultPath,
                                      std::optional<std::string> pathEnv) {
    auto fail = [&](const wxString& reason) -> std::optional<std::string> {
        if (why) {
            *why = cg::toUtf8(reason);
        }
        return std::nullopt;
    };
    if (!s.cliPath.empty()) {
        if (!isExecutableFile(s.cliPath)) {
            return fail(wxString::Format(_("%s cannot be run as a program."), cg::fromUtf8(s.cliPath)));
        }
        return s.cliPath;
    }
    if (isExecutableFile(defaultPath)) {
        return defaultPath;
    }
    if (!pathEnv) {
        const char* path = std::getenv("PATH");
        pathEnv = path ? path : "";
    }
    if (auto found = cg::findOnPath(cg::kDefaultCliName, *pathEnv)) {
        return found;
    }
    return fail(cliNotFoundText());
}
