// SPDX-License-Identifier: GPL-3.0-or-later
// CliRunner (child process, pipes, record callbacks, terminate / kill / abandon, version log)
// and resolveCli. Children are small `sh` scripts (inline or written to a temp directory), never the real
// CLI. Needs a wx event loop: a global environment starts a wxAppConsole, each test runs inside its own loop.
#include "cli_runner.h"
#include "ocpn_fakes.h"
#include "plugin_log.h"
#include "sources_stub.h"
#include "wx_env.h"
#include "wx_loop.h"

#include <gtest/gtest.h>

#include <wx/app.h>
#include <wx/apptrait.h>
#include <wx/evtloop.h>
#include <wx/init.h>
#include <wx/log.h>
#include <wx/timer.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <regex>
#include <spawn.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <variant>
#include <vector>

using namespace cg;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

extern char** environ;

namespace {

struct Capture {
    std::vector<StdoutRecord> records;
    std::vector<StderrLine> lines;
    std::vector<std::string> events; // "record", "stderr", "exit" in arrival order
    int exitCode = 12345;
    int exits = 0;
    bool startFailedFlag = true;
    CliRunner::Callbacks callbacks() {
        CliRunner::Callbacks cb;
        cb.onRecord = [this](const StdoutRecord& r) {
            records.push_back(r);
            events.push_back("record");
        };
        cb.onStderr = [this](const StderrLine& l) {
            lines.push_back(l);
            events.push_back("stderr");
        };
        cb.onExit = [this](int code, bool failed) {
            exitCode = code;
            startFailedFlag = failed;
            ++exits;
            events.push_back("exit");
        };
        return cb;
    }
    bool sawStderrText(const std::string& text) const {
        for (const StderrLine& l : lines) {
            if (const auto* m = std::get_if<MessageLine>(&l); m && m->text == text) {
                return true;
            }
        }
        return false;
    }
};

std::vector<std::string> sh(const std::string& script) {
    return {"/bin/sh", "-c", script};
}

// A stand-in that is stopped: the stop line becomes a SIGTERM (stub::kStopPrelude).
std::vector<std::string> shStop(const std::string& script) {
    return sh(std::string(stub::kStopPrelude) + script);
}

// Temp directory whose name has a space, a quote and Cyrillic.
class TempDir {
public:
    TempDir() {
        std::string tmpl = (fs::temp_directory_path() / "cgrun.XXXXXX").string();
        EXPECT_NE(mkdtemp(tmpl.data()), nullptr);
        path_ = fs::path(tmpl) / "Карта d'x";
        fs::create_directories(path_);
        root_ = tmpl;
    }
    ~TempDir() {
        std::error_code ec;
        expectNoProcessesUnder(root_.string());
        fs::remove_all(root_, ec);
    }
    std::string write(const std::string& name, const std::string& text, bool executable) const {
        const fs::path p = path_ / name;
        {
            std::ofstream f(p);
            f << text;
        }
        chmod(p.c_str(), executable ? 0755 : 0644);
        return p.string();
    }
    const fs::path& dir() const { return path_; }

private:
    fs::path path_, root_;
};

class EnvGuard {
public:
    explicit EnvGuard(const char* name) : name_(name) {
        if (const char* v = std::getenv(name)) {
            old_ = v;
            had_ = true;
        }
    }
    ~EnvGuard() {
        if (had_) {
            setenv(name_, old_.c_str(), 1);
        } else {
            unsetenv(name_);
        }
    }

private:
    const char* name_;
    std::string old_;
    bool had_ = false;
};

const std::string kSourceLine =
    "source\t{AAAA-1}\tmap\tOSM Mapnik\timage/png\t3857\t4\tok\tosm.zmp\tOpen maps";
const std::string kSourceLine2 =
    "source\t{BBBB-2}\tlayer\tСпутник Ё\timage/jpeg\t3395\t7\tprojection\tg.zmp\tСпутник";

// done(), progress(): the number of events seen so far.
using Waiter = std::function<bool(const std::function<bool()>&, const std::function<size_t()>&)>;

} // namespace

class CliRunnerTest : public LoopFixture {
protected:
    // Progress-based wait: dispatch events until done(); fails only when `progress()` did
    // not change for 20 s (a deadlock or a lost event), however slow the machine is; 10 minutes is a hard
    // cap.
    int quietMs = 20000; // the no-progress limit (a test shortens it)
    bool runUntilQuiet(const std::function<bool()>& done, const std::function<size_t()>& progress) {
        size_t seen = progress();
        auto lastProgress = Clock::now();
        bool stalled = false;
        const bool ok = runUntil(
            [&] {
                if (size_t now = progress(); now != seen) {
                    seen = now;
                    lastProgress = Clock::now();
                }
                if (done()) {
                    return true;
                }
                stalled = Clock::now() - lastProgress > std::chrono::milliseconds(quietMs);
                return stalled;
            },
            600000);
        return ok && !stalled;
    }
    Waiter wait() {
        return [this](const std::function<bool()>& done, const std::function<size_t()>& progress) {
            return runUntilQuiet(done, progress);
        };
    }
};

// the progress-based wait fails after the quiet limit without progress, passes on done(), and keeps
// waiting while progress goes on.
TEST_F(CliRunnerTest, QuietWaitFailsWithoutProgressAndKeepsWaitingWithProgress) {
    quietMs = 300;
    EXPECT_FALSE(runUntilQuiet([] { return false; }, [] { return size_t{0}; }));
    EXPECT_TRUE(runUntilQuiet([] { return true; }, [] { return size_t{0}; }));
    size_t n = 0;
    const auto start = Clock::now();
    EXPECT_TRUE(runUntilQuiet([&] { return Clock::now() - start > std::chrono::milliseconds(1000); },
                              [&] { return ++n; }));
}

// Proof 1: nothing reads the pipes in the child's lifetime but the runner; 2 MB of stderr first (fills the
// pipe), 1 MB of stdout after it.
TEST_F(CliRunnerTest, TwoMegabytesOnStderrAndOneOnStdoutNoDeadlockNoLossInOrder) {
    const std::string script =
        "awk 'BEGIN { for (i = 1; i <= 20000; i++) printf \"e%d %0100d\\n\", i, 0 > \"/dev/stderr\";"
        " for (i = 1; i <= 17000; i++) printf \"written\\t{G}\\t/p/%d\\t%d\\t5\\n\", i, i;"
        " printf \"written\\t{G}\\t/p/last\\t17001\\t5\" }'";
    Capture run;
    CliRunner runner;
    std::string error;
    ASSERT_TRUE(runner.start(sh(script), run.callbacks(), &error)) << error;
    runUntilQuiet([&] { return run.exits > 0; }, [&] { return run.events.size(); });
    ASSERT_GT(run.exits, 0) << "pipe deadlock: no output for 20 s";
    EXPECT_EQ(run.exitCode, 0);
    ASSERT_EQ(run.records.size(), 17001u); // the last record has no newline: flushed at the end
    for (size_t i = 0; i < run.records.size(); ++i) {
        const auto& w = std::get<WrittenRecord>(run.records[i]);
        ASSERT_EQ(w.tiles, static_cast<std::int64_t>(i + 1)) << "record " << i;
    }
    ASSERT_EQ(run.lines.size(), 20000u);
    for (size_t i = 0; i < run.lines.size(); ++i) {
        const std::string& text = std::get<MessageLine>(run.lines[i]).text;
        ASSERT_EQ(text.substr(0, text.find(' ')), std::string("e") + std::to_string(i + 1)) << "line " << i;
        ASSERT_EQ(text.size(), text.find(' ') + 1 + 100) << "line " << i;
    }
    EXPECT_EQ(run.events.back(), "exit");
    EXPECT_FALSE(run.startFailedFlag);
    EXPECT_FALSE(runner.running());
}

// Proof 2 (stand-in for the CLI): records arrive parsed, a bad line is dropped, onExit(0,false) comes after
// the last record and after the last stderr line.
TEST_F(CliRunnerTest, SourcesRecordsThenExitLast) {
    TempDir tmp;
    const std::string cli = tmp.write("fakecli.sh",
                                      "#!/bin/sh\ncat <<'EOF'\n" + kSourceLine + "\nnot a record\n" +
                                          kSourceLine2 + "\nEOF\necho 'warning: two sources' >&2\n",
                                      true);
    Capture run;
    CliRunner runner;
    std::string error;
    ASSERT_TRUE(runner.start({cli, "list-only"}, run.callbacks(), &error)) << error;
    ASSERT_TRUE(runUntil([&] { return run.exits > 0; }, 20000));
    ASSERT_EQ(run.records.size(), 2u);
    const auto& a = std::get<SourceRecord>(run.records[0]);
    EXPECT_EQ(a.guid, "{AAAA-1}");
    EXPECT_EQ(a.name, "OSM Mapnik");
    EXPECT_EQ(a.epsg, 3857);
    EXPECT_EQ(a.verdict, "ok");
    EXPECT_EQ(a.group, "Open maps");
    const auto& b = std::get<SourceRecord>(run.records[1]);
    EXPECT_EQ(b.guid, "{BBBB-2}");
    EXPECT_EQ(b.name, "Спутник Ё");
    EXPECT_EQ(b.epsg, 3395);
    ASSERT_EQ(run.lines.size(), 1u);
    EXPECT_EQ(std::get<MessageLine>(run.lines[0]).kind, MessageLine::Kind::Warning);
    EXPECT_EQ(run.exitCode, 0);
    EXPECT_FALSE(run.startFailedFlag);
    EXPECT_EQ(run.exits, 1);
    ASSERT_EQ(run.events.size(), 4u);
    EXPECT_EQ(run.events.back(), "exit");
}

// Exit codes as onExit delivers them: wx 3.2 reports a normal code as signed 8 bits (143 -> -113);
// CliRunner restores it. Measured with wx 3.2 GTK3: sh "exit 130" -> -126, 143 -> -113. (Codes 192..255 are
// not restored and read as a signal; the CLI uses 0-5, 130 and 143, so this is not pinned.)
TEST_F(CliRunnerTest, NormalExitCodesArriveUnsigned) {
    const std::vector<std::pair<std::string, int>> cases = {
        {"exit 0", 0}, {"exit 3", 3}, {"exit 5", 5}, {"exit 130", 130}, {"exit 143", 143}, {"exit 128", 128}};
    for (const auto& [script, code] : cases) {
        Capture run;
        CliRunner runner;
        std::string error;
        ASSERT_TRUE(runner.start(sh(script), run.callbacks(), &error)) << error;
        ASSERT_TRUE(runUntil([&] { return run.exits > 0; }, 20000)) << script;
        EXPECT_EQ(run.exitCode, code) << script;
    }
}

// Proof 3: terminate() on a child that traps TERM like the CLI (saved-session message, exit 143).
TEST_F(CliRunnerTest, TerminateGivesTheChildsOwnExit143AndItsMessage) {
    Capture run;
    bool ready = false;
    CliRunner::Callbacks cb = run.callbacks();
    cb.onStderr = [&, inner = cb.onStderr](const StderrLine& l) {
        inner(l);
        if (const auto* m = std::get_if<MessageLine>(&l); m && m->text == "ready") {
            ready = true;
        }
    };
    CliRunner runner;
    std::string error;
    ASSERT_TRUE(
        runner.start(shStop("trap 'echo \"session saved; run the same command again\" >&2; exit 143' TERM;"
                            " echo ready >&2; k=0; while [ $k -lt 1200 ]; do sleep 0.05; k=$((k + 1)); done"),
                     cb, &error))
        << error;
    ASSERT_TRUE(runUntil([&] { return ready; }, 20000));
    runner.terminate();
    runner.terminate(); // a second call must not change the outcome
    ASSERT_TRUE(runUntil([&] { return run.exits > 0; }, 20000));
    EXPECT_EQ(run.exitCode, 143);
    EXPECT_TRUE(run.sawStderrText("session saved; run the same command again"));
    EXPECT_EQ(run.exits, 1);
}

TEST_F(CliRunnerTest, SignalledChildIsMinusTheSignal) {
    struct Case {
        bool kill;
        int expected;
    };
    for (const Case c : {Case{false, -15}, Case{true, -9}}) {
        Capture run;
        bool ready = false;
        CliRunner::Callbacks cb = run.callbacks();
        cb.onStderr = [&](const StderrLine& l) {
            if (const auto* m = std::get_if<MessageLine>(&l); m && m->text == "ready") {
                ready = true;
            }
        };
        CliRunner runner;
        std::string error;
        // SIGTERM: the child sends it to itself (the runner never does); SIGKILL: runner.kill()
        ASSERT_TRUE(runner.start(
            sh(c.kill ? "echo ready >&2; exec sleep 30" : "echo ready >&2; kill -TERM $$; sleep 30"), cb,
            &error))
            << error;
        ASSERT_TRUE(runUntil([&] { return ready; }, 20000));
        if (c.kill) {
            runner.kill();
        }
        ASSERT_TRUE(runUntil([&] { return run.exits > 0; }, 20000));
        EXPECT_EQ(run.exitCode, c.expected) << (c.kill ? "SIGKILL" : "SIGTERM");
        EXPECT_FALSE(runner.running());
    }
}

// A child that exits on the stop line: abandon() returns as soon as it has ended, dispatches no event (a
// 10 ms timer set before the call has not fired), calls no exit callback, and wx reaps
// the zombie on the next turns of the loop.
TEST_F(CliRunnerTest, AbandonReturnsAsSoonAsAChildThatExitsOnTheStopLineEndedAndRunsNoEvent) {
    Capture run;
    bool ready = false;
    long pid = 0;
    CliRunner::Callbacks cb = run.callbacks();
    cb.onStderr = [&](const StderrLine& l) {
        if (const auto* m = std::get_if<MessageLine>(&l)) {
            if (m->text == "ready")
                ready = true;
            else
                pid = std::atol(m->text.c_str());
        }
    };
    CliRunner runner;
    std::string error;
    ASSERT_TRUE(
        runner.start(shStop("trap 'exit 143' TERM; echo $$ >&2; echo ready >&2; k=0; while [ $k -lt 1200 ]; "
                            "do sleep 0.05; k=$((k + 1)); done"),
                     cb, &error))
        << error;
    ASSERT_TRUE(runUntil([&] { return ready; }, 20000));
    ASSERT_GT(pid, 1);
    bool timerRan = false;
    wxEvtHandler handler;
    wxTimer timer(&handler);
    handler.Bind(wxEVT_TIMER, [&](wxTimerEvent&) { timerRan = true; });
    timer.StartOnce(10);
    std::this_thread::sleep_for(std::chrono::milliseconds(30)); // the timer is due when abandon runs
    const auto t0 = Clock::now();
    runner.abandon(2000);
    EXPECT_LT(Clock::now() - t0, std::chrono::milliseconds(1500));
    EXPECT_FALSE(runner.running());
    EXPECT_FALSE(timerRan) << "an event was dispatched inside abandon";
    EXPECT_EQ(run.exits, 0);
    EXPECT_TRUE(runUntil([&] { return ::kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH; }, 3000))
        << "the zombie stays after the next turns of the loop";
    runFor(50);
    EXPECT_TRUE(timerRan) << "the timer was live";
    EXPECT_EQ(run.exits, 0);
}

// A child that ignores the stop line is killed when the wait is over: after about waitMs, at once for 0;
// the process is gone, no callback.
TEST_F(CliRunnerTest, AbandonKillsAChildThatIgnoresTheStopLineAfterTheWait) {
    struct Row {
        int waitMs;
        int minMs, maxMs;
    };
    for (const Row& row : {Row{300, 290, 1500}, Row{0, 0, 200}}) {
        SCOPED_TRACE(row.waitMs);
        Capture run;
        bool ready = false;
        long pid = 0;
        CliRunner::Callbacks cb = run.callbacks();
        cb.onStderr = [&](const StderrLine& l) {
            if (const auto* m = std::get_if<MessageLine>(&l)) {
                if (m->text == "ready")
                    ready = true;
                else
                    pid = std::atol(m->text.c_str());
            }
        };
        CliRunner runner;
        std::string error;
        ASSERT_TRUE(runner.start(sh("echo $$ >&2; echo ready >&2; exec sleep 30"), cb, &error)) << error;
        ASSERT_TRUE(runUntil([&] { return ready; }, 20000));
        ASSERT_GT(pid, 1);
        const auto t0 = Clock::now();
        runner.abandon(row.waitMs);
        const auto took = Clock::now() - t0;
        EXPECT_GE(took, std::chrono::milliseconds(row.minMs));
        EXPECT_LT(took, std::chrono::milliseconds(row.maxMs));
        EXPECT_FALSE(runner.running());
        EXPECT_TRUE(runUntil([&] { return ::kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH; }, 3000))
            << "the child was killed and reaped";
        EXPECT_EQ(run.exits, 0);
    }
}

// A child that has ended but is not reaped yet (a zombie: wx reaps only from the loop, which abandon does not
// run) or that somebody else reaped counts as ended: abandon returns at once and kills nothing.
TEST_F(CliRunnerTest, AbandonTakesAZombieAndAReapedChildAsEnded) {
    for (const bool reap : {false, true}) {
        SCOPED_TRACE(reap);
        TempDir tmp;
        const std::string pidFile = (tmp.dir() / "pid").string();
        Capture run;
        CliRunner runner;
        std::string error;
        ASSERT_TRUE(runner.start({"/bin/sh", "-c", "echo $$ > \"$0\"; sleep 0.3; exit 0", pidFile},
                                 run.callbacks(), &error))
            << error;
        long pid = 0;
        for (int i = 0; i < 200 && pid <= 0; ++i) { // no event is dispatched while the child ends
            std::ifstream(pidFile) >> pid;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        ASSERT_GT(pid, 1);
        siginfo_t info{};
        for (int i = 0; i < 300 && info.si_pid != pid; ++i) {
            ::waitid(P_PID, static_cast<id_t>(pid), &info, WEXITED | WNOHANG | WNOWAIT);
            if (info.si_pid != pid)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        ASSERT_EQ(info.si_pid, static_cast<pid_t>(pid)) << "the child did not end as a zombie";
        if (reap) {
            ASSERT_EQ(::waitpid(static_cast<pid_t>(pid), nullptr, 0), static_cast<pid_t>(pid));
        }
        const auto t0 = Clock::now();
        runner.abandon(2000);
        EXPECT_LT(Clock::now() - t0, std::chrono::milliseconds(500));
        EXPECT_FALSE(runner.running());
        runFor(100);
        EXPECT_EQ(run.exits, 0);
    }
}

TEST_F(CliRunnerTest, AbandonWithoutAProcessReturnsAtOnce) {
    CliRunner runner;
    const auto t0 = Clock::now();
    runner.abandon(15000);
    EXPECT_LT(Clock::now() - t0, std::chrono::milliseconds(200));
}

// --stop-on-stdin is the LAST argument (after the caller's), and stdin is a pipe.
TEST_F(CliRunnerTest, TheChildGetsTheStopOptionLastAndAPipeAsStdin) {
    Capture run;
    CliRunner runner;
    std::string error;
    ASSERT_TRUE(runner.start({"/bin/sh", "-c",
                              "[ -p /dev/stdin ] && echo pipe >&2; echo \"$0\" >&2; "
                              "for a; do l=$a; done; echo \"last:$l\" >&2",
                              "n0", "a1", "a2"},
                             run.callbacks(), &error))
        << error;
    ASSERT_TRUE(runUntil([&] { return run.exits > 0; }, 20000));
    EXPECT_TRUE(run.sawStderrText("pipe"));
    EXPECT_TRUE(run.sawStderrText("n0"));
    EXPECT_TRUE(run.sawStderrText("last:--stop-on-stdin"));
}

// the stdin stays open until terminate(); then the child reads exactly `stop` and EOF;
// it exits 143 itself (its TERM trap would give 99: no signal was sent).
TEST_F(CliRunnerTest, StdinStaysOpenUntilTerminateThenStopAndEofAndNoSignal) {
    Capture run;
    bool ready = false;
    CliRunner::Callbacks cb = run.callbacks();
    cb.onStderr = [&, inner = cb.onStderr](const StderrLine& l) {
        inner(l);
        if (const auto* m = std::get_if<MessageLine>(&l); m && m->text == "ready")
            ready = true;
    };
    CliRunner runner;
    std::string error;
    ASSERT_TRUE(runner.start(sh("trap 'exit 99' TERM; echo ready >&2; read l; echo \"got:$l\" >&2; "
                                "if read m; then echo \"more:$m\" >&2; else echo eof >&2; fi; exit 143"),
                             cb, &error))
        << error;
    ASSERT_TRUE(runUntil([&] { return ready; }, 20000));
    runFor(500); // five timer periods: a closed stdin would have ended the child
    EXPECT_EQ(run.exits, 0);
    EXPECT_FALSE(run.sawStderrText("got:"));
    runner.terminate();
    runner.terminate(); // once per run: no second line
    ASSERT_TRUE(runUntil([&] { return run.exits > 0; }, 20000));
    EXPECT_EQ(run.exitCode, 143);
    EXPECT_TRUE(run.sawStderrText("got:stop"));
    EXPECT_TRUE(run.sawStderrText("eof"));
    EXPECT_FALSE(run.sawStderrText("more:"));
}

// the write end of the stdin pipe is close-on-exec. A process spawned later by other means
// (posix_spawn, as OpenCPN's system() would) must not keep it open: the child sees EOF while `sleep 5` runs.
TEST_F(CliRunnerTest, ProcessSpawnedByOthersDoesNotHoldTheStdinWriteEnd) {
    Capture run;
    bool ready = false;
    CliRunner::Callbacks cb = run.callbacks();
    cb.onStderr = [&, inner = cb.onStderr](const StderrLine& l) {
        inner(l);
        if (const auto* m = std::get_if<MessageLine>(&l); m && m->text == "ready")
            ready = true;
    };
    CliRunner runner;
    std::string error;
    ASSERT_TRUE(runner.start(sh("echo ready >&2; cat >/dev/null"), cb, &error)) << error;
    ASSERT_TRUE(runUntil([&] { return ready; }, 20000));
    pid_t other = 0;
    char arg0[] = "sleep", arg1[] = "5";
    char* const argv[] = {arg0, arg1, nullptr};
    ASSERT_EQ(posix_spawnp(&other, "sleep", nullptr, nullptr, argv, environ), 0);
    runner.terminate();
    const bool ended = runUntil([&] { return run.exits > 0; }, 3000);
    const bool otherAlive = ::waitpid(other, nullptr, WNOHANG) == 0;
    ::kill(other, SIGKILL);
    ::waitpid(other, nullptr, 0);
    EXPECT_TRUE(ended) << "the child did not see EOF while the other process lived";
    EXPECT_TRUE(otherAlive);
    EXPECT_EQ(run.exitCode, 0);
}

// terminate() on a child that closed its stdin raises SIGPIPE; the guard keeps the test
// process alive and the write logs nothing. Void unless SIGPIPE is the default here.
TEST_F(CliRunnerTest, TerminateOnAChildThatClosedItsStdinSurvivesSigpipe) {
    struct sigaction sa{};
    ASSERT_EQ(sigaction(SIGPIPE, nullptr, &sa), 0);
    ASSERT_TRUE(sa.sa_handler == SIG_DFL) << "SIGPIPE is not the default here: the proof would be void";
    wxLogBuffer* buffer = new wxLogBuffer;
    wxLog* old = wxLog::SetActiveTarget(buffer);
    Capture run;
    bool ready = false;
    CliRunner::Callbacks cb = run.callbacks();
    cb.onStderr = [&, inner = cb.onStderr](const StderrLine& l) {
        inner(l);
        if (const auto* m = std::get_if<MessageLine>(&l); m && m->text == "ready")
            ready = true;
    };
    {
        CliRunner runner;
        runner.setStopGraceMs(300);
        std::string error;
        ASSERT_TRUE(runner.start(sh("exec 0<&-; echo ready >&2; exec sleep 30"), cb, &error)) << error;
        ASSERT_TRUE(runUntil([&] { return ready; }, 20000));
        runner.terminate();
        ASSERT_TRUE(runUntil([&] { return run.exits > 0; }, 10000));
        EXPECT_EQ(run.exitCode, -9);
    }
    wxLog::SetActiveTarget(old);
    wxLog::FlushActive();
    EXPECT_TRUE(buffer->GetBuffer().empty()) << buffer->GetBuffer();
    delete buffer;
}

// The production wait of the destructor is 2 s.
TEST_F(CliRunnerTest, DestructorWaitDefaultsToTwoSeconds) {
    CliRunner runner;
    EXPECT_EQ(runner.destructorWaitMs(), 2000);
}

// The destructor of a running runner is abandon(destructorWaitMs()): it waits for a child that ignores the
// stop line for the set time (here 300 ms, not the production 2 s), then kills it; no callback.
TEST_F(CliRunnerTest, TheDestructorAbandonsARunningChildAfterItsWait) {
    Capture run;
    bool ready = false;
    long pid = 0;
    CliRunner::Callbacks cb = run.callbacks();
    cb.onStderr = [&](const StderrLine& l) {
        if (const auto* m = std::get_if<MessageLine>(&l)) {
            if (m->text == "ready")
                ready = true;
            else
                pid = std::atol(m->text.c_str());
        }
    };
    auto runner = std::make_unique<CliRunner>();
    runner->setDestructorWaitMs(300);
    std::string error;
    ASSERT_TRUE(runner->start(sh("echo $$ >&2; echo ready >&2; exec sleep 30"), cb, &error)) << error;
    ASSERT_TRUE(runUntil([&] { return ready; }, 20000));
    ASSERT_GT(pid, 1);
    const auto t0 = Clock::now();
    runner.reset();
    EXPECT_GE(Clock::now() - t0, std::chrono::milliseconds(290));
    EXPECT_LT(Clock::now() - t0, std::chrono::milliseconds(1500));
    EXPECT_EQ(run.exits, 0);
    EXPECT_TRUE(runUntil([&] { return ::kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH; }, 3000));
}

// Proof 5.
TEST_F(CliRunnerTest, StartFailsWithAnErrorAndNoExit) {
    TempDir tmp;
    const std::string notExecutable = tmp.write("plain.txt", "#!/bin/sh\nexit 0\n", false);
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"/nonexistent/prog", "Cannot start /nonexistent/prog."},
        {"no-such-prog-zz", "Cannot start no-such-prog-zz."},
        {notExecutable, "Cannot start " + notExecutable + "."},
        {tmp.dir().string(), std::string("Cannot start ") + tmp.dir().string() + "."},
    };
    for (const auto& [program, message] : cases) {
        Capture run;
        CliRunner runner;
        std::string error;
        EXPECT_FALSE(runner.start({program, "arg"}, run.callbacks(), &error)) << program;
        EXPECT_EQ(error, message);
        EXPECT_FALSE(runner.running());
        runFor(300);
        EXPECT_EQ(run.exits, 0) << program;
        EXPECT_TRUE(run.events.empty()) << program;
    }
    CliRunner runner;
    EXPECT_FALSE(runner.start({}, Capture().callbacks(), nullptr)); // a null error pointer is allowed
    std::string error;
    EXPECT_FALSE(runner.start({""}, Capture().callbacks(), &error));
    EXPECT_EQ(error, "No program given.");
}

TEST_F(CliRunnerTest, SecondStartWhileRunningFailsAndTheFirstRunIsUnharmed) {
    Capture first, second;
    CliRunner runner;
    std::string error;
    ASSERT_TRUE(runner.start(sh("sleep 0.3; echo first >&2; exit 4"), first.callbacks(), &error)) << error;
    EXPECT_TRUE(runner.running());
    EXPECT_FALSE(runner.start(sh("exit 9"), second.callbacks(), &error));
    EXPECT_EQ(error, "Chart Grabber CLI is already running.");
    ASSERT_TRUE(runUntil([&] { return first.exits > 0; }, 20000));
    EXPECT_EQ(first.exitCode, 4);
    EXPECT_TRUE(first.sawStderrText("first"));
    runFor(200);
    EXPECT_EQ(second.exits, 0);
    // The runner is reusable after the exit, also from inside onExit's turn.
    Capture third;
    ASSERT_TRUE(runner.start(sh("exit 7"), third.callbacks(), &error)) << error;
    ASSERT_TRUE(runUntil([&] { return third.exits > 0; }, 20000));
    EXPECT_EQ(third.exitCode, 7);
}

// Arguments reach the child unchanged (space, quote, Cyrillic) and its stderr text is decoded as UTF-8.
TEST_F(CliRunnerTest, ArgumentsWithSpaceQuoteAndCyrillicArriveUnchanged) {
    Capture run;
    CliRunner runner;
    std::string error;
    ASSERT_TRUE(runner.start({"/bin/sh", "-c", "printf '%s\\n' \"$1\" >&2", "sh", "Карта 'ё' два"},
                             run.callbacks(), &error))
        << error;
    ASSERT_TRUE(runUntil([&] { return run.exits > 0; }, 20000));
    EXPECT_TRUE(run.sawStderrText("Карта 'ё' два"));
}

// resolveCli: the order, one case each, distinct files so a swap is caught.
TEST(ResolveCli, DefaultPathWinsOverTheOneOnPath) {
    TempDir tmp;
    const std::string def = tmp.write("installed-cli", "#!/bin/sh\n", true);
    const std::string onPath = tmp.write(std::string(kDefaultCliName), "#!/bin/sh\n", true);
    std::string why = "untouched";
    const auto found = resolveCli(Settings(), &why, {def}, tmp.dir().string());
    ASSERT_TRUE(found);
    EXPECT_EQ(*found, def);
    EXPECT_NE(*found, onPath);
    EXPECT_EQ(why, "untouched"); // why is only written on failure
}

TEST(ResolveCli, MissingOrNonExecutableDefaultFallsToPath) {
    TempDir tmp;
    const std::string onPath = tmp.write(std::string(kDefaultCliName), "#!/bin/sh\n", true);
    const std::string plain = tmp.write("plain-default", "x", false);
    for (const std::string& def : {(tmp.dir() / "missing").string(), plain}) {
        const auto found = resolveCli(Settings(), nullptr, {def}, tmp.dir().string() + ":/nonexistent-dir");
        ASSERT_TRUE(found) << def;
        EXPECT_EQ(*found, onPath);
    }
}

TEST(ResolveCli, HostCliPathsIsTheBundledThenTheInstalledOneOnLinux) {
    ocpn_fake::dataDir = wxString::FromUTF8("/home/Иван/plugin dir");
    EXPECT_EQ(hostCliPaths(), (std::vector<std::string>{"/home/Иван/plugin dir/cli/chart-grabber-cli",
                                                        "/usr/bin/chart-grabber-cli"}));
    EXPECT_EQ(ocpn_fake::dataDirAsked, "chartgrabber_pi");
    ocpn_fake::dataDir.clear();
}

// the first runnable entry of the list wins; an empty list goes to PATH.
TEST(ResolveCli, FirstRunnableEntryOfTheListWins) {
    TempDir tmp;
    const std::string missing = (tmp.dir() / "missing").string();
    const std::string plain = tmp.write("plain-first", "x", false);
    const std::string a = tmp.write("cli-a", "#!/bin/sh\n", true);
    const std::string b = tmp.write("cli-b", "#!/bin/sh\n", true);
    const std::string onPath = tmp.write(std::string(kDefaultCliName), "#!/bin/sh\n", true);
    const std::string pathEnv = tmp.dir().string();
    EXPECT_EQ(resolveCli(Settings(), nullptr, {a, b}, pathEnv), a);
    EXPECT_EQ(resolveCli(Settings(), nullptr, {missing, b}, pathEnv), b);
    EXPECT_EQ(resolveCli(Settings(), nullptr, {plain, b, a}, pathEnv), b);
    EXPECT_EQ(resolveCli(Settings(), nullptr, {}, pathEnv), onPath);
    std::string why;
    EXPECT_FALSE(resolveCli(Settings(), &why, {missing, plain}, (tmp.dir() / "none").string()));
    EXPECT_NE(why.find("was not found"), std::string::npos);
}

TEST(ResolveCli, PathEnvDefaultsToTheEnvironment) {
    TempDir tmp;
    const std::string cli = tmp.write(std::string(kDefaultCliName), "#!/bin/sh\n", true);
    EnvGuard guard("PATH");
    setenv("PATH", tmp.dir().string().c_str(), 1);
    const auto found = resolveCli(Settings(), nullptr, {(tmp.dir() / "missing").string()});
    ASSERT_TRUE(found);
    EXPECT_EQ(*found, cli);
}

TEST(ResolveCli, NeitherGivesTheNotFoundReason) {
    TempDir tmp;
    std::string why;
    EXPECT_FALSE(resolveCli(Settings(), &why, {(tmp.dir() / "missing").string()}, tmp.dir().string()));
    EXPECT_EQ(why,
              "Chart Grabber CLI was not found. Install the chart-grabber-cli package, or choose the program "
              "under Advanced in Preferences."); // messages.h cliNotFoundText
    EXPECT_FALSE(resolveCli(Settings(), nullptr, {(tmp.dir() / "missing").string()}, tmp.dir().string()));
}

TEST(ResolveCli, ChosenPathWinsAndNeverFallsBack) {
    TempDir tmp;
    const std::string def = tmp.write("installed-cli", "#!/bin/sh\n", true);
    tmp.write(std::string(kDefaultCliName), "#!/bin/sh\n", true); // on pathEnv too
    const std::string plain = tmp.write("plain.txt", "x", false);
    const std::string other = tmp.write("other-cli", "#!/bin/sh\n", true);
    const std::string pathEnv = tmp.dir().string();
    const std::vector<std::string> wrong = {plain, tmp.dir().string(), (tmp.dir() / "missing").string()};
    for (const std::string& w : wrong) {
        Settings s;
        s.cliPath = w;
        std::string why;
        EXPECT_FALSE(resolveCli(s, &why, {def}, pathEnv)) << w;
        EXPECT_EQ(why, w + " cannot be run as a program.");
        EXPECT_FALSE(resolveCli(s, nullptr, {def}, pathEnv));
    }
    Settings s;
    s.cliPath = other;
    const auto found = resolveCli(s, nullptr, {def}, pathEnv);
    ASSERT_TRUE(found);
    EXPECT_EQ(*found, other);
}

namespace {
class CollectLog : public wxLog {
public:
    std::vector<std::string> messages;

protected:
    void DoLogRecord(wxLogLevel level, const wxString& msg, const wxLogRecordInfo&) override {
        if (level ==
            wxLOG_Debug) // wx's own diagnostics (a child reaped by another test): not the plugin's lines
            return;
        messages.push_back(msg.utf8_string());
    }
};

// Installs a collecting log target for one test and restores the old one.
class LogScope {
public:
    LogScope() : log_(new CollectLog), previous_(wxLog::SetActiveTarget(log_)) {}
    ~LogScope() { delete wxLog::SetActiveTarget(previous_); }
    CollectLog* operator->() { return log_; }

private:
    CollectLog* log_;
    wxLog* previous_;
};

// A stand-in that appends a line to `count` (next to it) at every run and then does `body`.
std::string countingScript(const std::string& body) {
    return "#!/bin/sh\necho run >> \"$(dirname \"$0\")/count\"\n" + body + "\n";
}
int runsOf(const TempDir& tmp) {
    std::ifstream in(tmp.dir() / "count");
    int n = 0;
    for (std::string line; std::getline(in, line);) {
        ++n;
    }
    return n;
}
} // namespace

// ---- CliProbe: the version check without waiting ------------------------------------------------------
namespace {
using ProbeResult = CliProbe::Result;

// A stand-in that notes its pid next to itself and never ends (a hung program). No `exec`: the script itself
// stays the child, so the kill reaches it.
std::string hangScript() {
    return countingScript("d=$(dirname \"$0\")\necho $$ > \"$d/pid.tmp\"\nmv \"$d/pid.tmp\" \"$d/pid\"\n"
                          "while :; do sleep 0.05; done");
}

// Counts the 100 ms ticks of the loop while a probe waits.
class Ticker : public wxTimer {
public:
    int ticks = 0;
    void Notify() override { ++ticks; }
};
} // namespace

class CliProbeTest : public CliRunnerTest {
protected:
    // The pid the hung stand-in wrote (it appears when the script runs); -1 when it never did.
    int pidOf(const TempDir& tmp) {
        const fs::path file = tmp.dir() / "pid";
        if (!runUntil([&] { return fs::exists(file); }, 10000))
            return -1;
        std::ifstream in(file);
        int pid = -1;
        in >> pid;
        return pid;
    }
    // True once the process is gone and reaped (a zombie still answers kill(pid, 0)).
    bool waitGone(int pid) {
        return runUntil([&] { return ::kill(pid, 0) != 0 && errno == ESRCH; }, 3000);
    }
    static CliProbe::Done record(std::vector<ProbeResult>& into) {
        return [&into](ProbeResult r) { into.push_back(r); };
    }
};

// A child that never answers: check returns at once, the loop keeps running, done(NoAnswer) arrives once
// after the limit, the child is killed and reaped, nothing is cached and the next check starts it again.
TEST_F(CliProbeTest, AChildThatNeverAnswersEndsInNoAnswerAfterTheLimitWithoutBlockingTheLoop) {
    TempDir tmp;
    CliProbe probe;
    const std::string cli = tmp.write("hang-cli", hangScript(), true);
    LogScope log;
    std::vector<ProbeResult> results;
    Ticker ticker;
    ticker.Start(100);
    probe.setTimeoutMs(800);
    probe.check(cli, cg::Settings{}, record(results));
    EXPECT_TRUE(results.empty()) << "check does not wait";
    EXPECT_TRUE(probe.running());
    const int pid = pidOf(tmp);
    ASSERT_GT(pid, 0) << "the child started";
    ASSERT_TRUE(runUntil([&] { return !results.empty(); }, 10000));
    EXPECT_EQ(results, std::vector<ProbeResult>{ProbeResult::NoAnswer});
    EXPECT_GE(ticker.ticks, 2) << "the loop kept running during the wait";
    EXPECT_FALSE(probe.running());
    EXPECT_TRUE(waitGone(pid));
    runFor(200);
    EXPECT_EQ(results.size(), 1u) << "done runs once; the kill's exit is not another answer";
    ASSERT_EQ(log->messages.size(), 1u);
    EXPECT_EQ(log->messages[0], "ChartGrabber: tile program did not answer in 10 s; stopped");
    EXPECT_EQ(probedCliVersion(cli), "") << "a NoAnswer is not cached";

    fs::remove(tmp.dir() / "pid");
    probe.check(cli, cg::Settings{}, record(results));
    EXPECT_TRUE(probe.running()) << "not a cache hit: the program starts again";
    ASSERT_TRUE(runUntil([&] { return results.size() == 2; }, 10000));
    EXPECT_EQ(results[1], ProbeResult::NoAnswer);
    EXPECT_EQ(runsOf(tmp), 2);
    const int second = pidOf(tmp);
    ASSERT_GT(second, 0);
    EXPECT_NE(second, pid);
    EXPECT_TRUE(waitGone(second));
}

// A supported answer: done(Supported) once and the one log line; the second check of the same file is a
// cache hit (done before check returns, no process, no log); a newer modification time probes again.
TEST_F(CliProbeTest, ASupportedAnswerIsCachedPerFileAndANewerModificationTimeProbesAgain) {
    TempDir tmp;
    CliProbe probe;
    const std::string cli =
        tmp.write("probe-cli", countingScript("echo 'chart-grabber-cli 2026.10.9 (x)'"), true);
    const auto t0 = fs::last_write_time(cli) - std::chrono::hours(2);
    fs::last_write_time(cli, t0);
    LogScope log;
    std::vector<ProbeResult> results;
    probe.check(cli, cg::Settings{}, record(results));
    EXPECT_TRUE(results.empty()) << "a first probe is answered by the child, not inside check";
    ASSERT_TRUE(runUntil([&] { return !results.empty(); }, 10000));
    runFor(100);
    EXPECT_EQ(results, std::vector<ProbeResult>{ProbeResult::Supported});
    EXPECT_FALSE(probe.running());
    ASSERT_EQ(log->messages.size(), 1u);
    EXPECT_EQ(log->messages[0], "ChartGrabber: tile program chart-grabber-cli 2026.10.9 (x) - supported");
    EXPECT_EQ(probedCliVersion(cli), "2026.10.9") << "the cache is the probe cache";

    probe.check(cli, cg::Settings{}, record(results));
    EXPECT_EQ(results.size(), 2u) << "a cache hit answers before check returns";
    EXPECT_EQ(results[1], ProbeResult::Supported);
    EXPECT_FALSE(probe.running());
    runFor(100);
    EXPECT_EQ(runsOf(tmp), 1) << "no process for a cache hit";
    EXPECT_EQ(log->messages.size(), 1u) << "no log line for a cache hit";

    fs::last_write_time(cli, t0 + std::chrono::hours(1));
    probe.check(cli, cg::Settings{}, record(results));
    EXPECT_EQ(results.size(), 2u) << "a changed file is probed again";
    ASSERT_TRUE(runUntil([&] { return results.size() == 3; }, 10000));
    EXPECT_EQ(results[2], ProbeResult::Supported);
    EXPECT_EQ(runsOf(tmp), 2);
    EXPECT_EQ(log->messages.size(), 2u);
}

// Too old, no version line, exit != 0, a program that is missing: done(TooOld) once each and the log line is
// the one the probe writes.
TEST_F(CliProbeTest, TooOldNoVersionLineBadExitOrAMissingProgramAreTooOldWithTheUsualLogLine) {
    struct Row {
        const char* name;
        std::string body;
        std::string logged;
    };
    const std::vector<Row> rows = {
        {"too old", "echo 'chart-grabber-cli 2026.10.8 (x)'",
         "ChartGrabber: tile program chart-grabber-cli 2026.10.8 (x) - too old"},
        {"not the program", "echo 'hello world'", "ChartGrabber: tile program hello world - too old"},
        {"exit 2", "exit 2", "ChartGrabber: tile program no version (exit 2) - too old"},
        {"version line but exit 3", "echo 'chart-grabber-cli 2026.10.9 (x)'; exit 3",
         "ChartGrabber: tile program no version (exit 3) - too old"},
        {"no output", "exit 0", "ChartGrabber: tile program no version (exit 0) - too old"},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        TempDir tmp;
        CliProbe probe;
        const std::string cli = tmp.write("probe-cli", countingScript(row.body), true);
        LogScope log;
        std::vector<ProbeResult> results;
        probe.check(cli, cg::Settings{}, record(results));
        ASSERT_TRUE(runUntil([&] { return !results.empty(); }, 10000));
        runFor(100);
        EXPECT_EQ(results, std::vector<ProbeResult>{ProbeResult::TooOld});
        ASSERT_EQ(log->messages.size(), 1u);
        EXPECT_EQ(log->messages[0], row.logged);
    }
    TempDir tmp;
    CliProbe probe;
    LogScope log;
    std::vector<ProbeResult> results;
    probe.check((tmp.dir() / "no such program").string(), cg::Settings{}, record(results));
    ASSERT_TRUE(runUntil([&] { return !results.empty(); }, 10000));
    runFor(100);
    EXPECT_EQ(results, std::vector<ProbeResult>{ProbeResult::TooOld}) << "missing program";
    ASSERT_EQ(log->messages.size(), 1u);
    EXPECT_EQ(log->messages[0], "ChartGrabber: tile program no version (exit 127) - too old");
}

// The first text line is the version line: a CLI that prints the version only on a later line is not
// accepted, and a record line before it is no text.
TEST_F(CliProbeTest, OnlyTheFirstTextLineCountsAsTheVersionLine) {
    TempDir tmp;
    CliProbe probe;
    const std::string cli =
        tmp.write("probe-cli", countingScript("echo 'banner'; echo 'chart-grabber-cli 2026.10.9 (x)'"), true);
    LogScope log;
    std::vector<ProbeResult> results;
    probe.check(cli, cg::Settings{}, record(results));
    ASSERT_TRUE(runUntil([&] { return !results.empty(); }, 10000));
    EXPECT_EQ(results, std::vector<ProbeResult>{ProbeResult::TooOld});
    ASSERT_EQ(log->messages.size(), 1u);
    EXPECT_EQ(log->messages[0], "ChartGrabber: tile program banner - too old");

    const std::string other = tmp.write(
        "probe2-cli",
        countingScript("printf 'written\\t{G}\\t/p\\t1\\t5\\n'; echo 'chart-grabber-cli 2026.10.9 (x)'"),
        true);
    probe.check(other, cg::Settings{}, record(results));
    ASSERT_TRUE(runUntil([&] { return results.size() == 2; }, 10000));
    EXPECT_EQ(results[1], ProbeResult::Supported);
}

// A path with a space, a quote and Cyrillic (the temp folder has all three) works.
TEST_F(CliProbeTest, AProgramPathWithASpaceAQuoteAndCyrillicIsProbed) {
    TempDir tmp;
    CliProbe probe;
    const std::string cli =
        tmp.write("\xD0\x9F d'x-cli", countingScript("echo 'chart-grabber-cli 2026.10.9 (x)'"), true);
    ASSERT_NE(cli.find(' '), std::string::npos);
    ASSERT_NE(cli.find('\''), std::string::npos);
    ASSERT_NE(cli.find("\xD0\x9F"), std::string::npos);
    std::vector<ProbeResult> results;
    probe.check(cli, cg::Settings{}, record(results));
    ASSERT_TRUE(runUntil([&] { return !results.empty(); }, 10000));
    EXPECT_EQ(results, std::vector<ProbeResult>{ProbeResult::Supported});
    EXPECT_EQ(runsOf(tmp), 1);
}

// The data folder is passed as one argument next to --version, and --stop-on-stdin comes last.
TEST_F(CliProbeTest, TheChildGetsTheVersionOptionTheDataFolderAndTheStopOptionLast) {
    TempDir tmp;
    CliProbe probe;
    const std::string body = "for a; do printf '%s\\n' \"$a\" >> \"$(dirname \"$0\")/argv\"; done\n"
                             "echo 'chart-grabber-cli 2026.10.9 (abc)'";
    const std::string cli = tmp.write("argv-cli", countingScript(body), true);
    cg::Settings s;
    s.cliDataDir = (tmp.dir() / "d \xD0\x96 a'b" / "cli-data").string();
    std::vector<ProbeResult> results;
    probe.check(cli, s, record(results));
    ASSERT_TRUE(runUntil([&] { return !results.empty(); }, 10000));
    std::vector<std::string> got;
    std::ifstream in(tmp.dir() / "argv");
    for (std::string l; std::getline(in, l);)
        got.push_back(l);
    EXPECT_EQ(got, (std::vector<std::string>{"--version", "--datadir=" + s.cliDataDir, "--stop-on-stdin"}));
}

// A check while one runs: the first done never runs (not even when its limit would have passed), its child
// is killed, the second done runs.
TEST_F(CliProbeTest, ACheckWhileOneRunsCancelsTheFirstWithoutItsDone) {
    TempDir tmp;
    CliProbe probe;
    const std::string hang = tmp.write("hang-cli", hangScript(), true);
    const std::string good =
        tmp.write("good-cli", countingScript("echo 'chart-grabber-cli 2026.10.9 (x)'"), true);
    std::vector<ProbeResult> first, second;
    probe.setTimeoutMs(400);
    probe.check(hang, cg::Settings{}, record(first));
    const int pid = pidOf(tmp);
    ASSERT_GT(pid, 0);
    probe.check(good, cg::Settings{}, record(second));
    ASSERT_TRUE(runUntil([&] { return !second.empty(); }, 10000));
    EXPECT_EQ(second, std::vector<ProbeResult>{ProbeResult::Supported});
    EXPECT_TRUE(waitGone(pid)) << "the first child is killed";
    runFor(700);
    EXPECT_TRUE(first.empty()) << "neither the kill nor the first limit calls the first done";
    EXPECT_EQ(second.size(), 1u);
}

// cancel: no done (the limit passes during the wait), the child is gone, running() is false.
TEST_F(CliProbeTest, CancelKillsTheChildAndNeverCallsDone) {
    TempDir tmp;
    CliProbe probe;
    const std::string cli = tmp.write("hang-cli", hangScript(), true);
    std::vector<ProbeResult> results;
    probe.setTimeoutMs(300);
    probe.check(cli, cg::Settings{}, record(results));
    const int pid = pidOf(tmp);
    ASSERT_GT(pid, 0);
    ASSERT_TRUE(probe.running());
    probe.cancel();
    EXPECT_FALSE(probe.running());
    EXPECT_TRUE(waitGone(pid));
    runFor(600);
    EXPECT_TRUE(results.empty());
    EXPECT_EQ(probedCliVersion(cli), "");

    probe.cancel();                                    // nothing running: harmless
    probe.check(cli, cg::Settings{}, record(results)); // and the probe is usable again
    EXPECT_TRUE(probe.running());
}

// Destroying a probe during a check: no done, no child left (the TempDir checks that no process stays).
TEST_F(CliProbeTest, DestroyingAProbeDuringACheckKillsTheChildAndNeverCallsDone) {
    TempDir tmp;
    const std::string cli = tmp.write("hang-cli", hangScript(), true);
    std::vector<ProbeResult> results;
    auto probe = std::make_unique<CliProbe>();
    probe->setTimeoutMs(300);
    probe->check(cli, cg::Settings{}, record(results));
    const int pid = pidOf(tmp);
    ASSERT_GT(pid, 0);
    probe.reset();
    EXPECT_TRUE(waitGone(pid));
    runFor(600);
    EXPECT_TRUE(results.empty());
}

// The log line is cut at 200 bytes, never inside a UTF-8 sequence. "hello " + 'a' filler makes the first line
// 199 bytes, then a 2-byte Cyrillic capital: its second byte would be byte 200 (the cut); the character is
// dropped whole. When the character ends exactly at byte 200 it stays.
TEST_F(CliProbeTest, TheLogLineIsCutOnACharacterBoundary) {
    const std::string yo = "\xD0\x81";
    const std::string head199 = std::string("hello ") + std::string(193, 'a');
    const std::string head198 = std::string("hello ") + std::string(192, 'a');
    struct Row {
        const char* name;
        std::string line, logged;
    };
    const std::vector<Row> rows = {
        {"split character dropped", head199 + yo + "bbb", head199},
        {"character ending at the limit stays", head198 + yo + "bbb", head198 + yo},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        TempDir tmp;
        CliProbe probe;
        const std::string cli = tmp.write("probe-cli", countingScript("echo '" + row.line + "'"), true);
        LogScope log;
        std::vector<ProbeResult> results;
        probe.check(cli, cg::Settings{}, record(results));
        ASSERT_TRUE(runUntil([&] { return !results.empty(); }, 10000));
        runFor(50);
        EXPECT_EQ(results, std::vector<ProbeResult>{ProbeResult::TooOld});
        ASSERT_EQ(log->messages.size(), 1u);
        EXPECT_EQ(log->messages[0], "ChartGrabber: tile program " + row.logged + " - too old");
    }
}

// The cache key is the path, the size and the modification time, not the content: a changed size or only a
// changed time asks the program again, the same size and time is answered from the cache; a too-old answer
// is cached too, and the version text of the cache is read without a process.
TEST_F(CliProbeTest, TheCacheKeyIsPathSizeAndModificationTimeAndATooOldAnswerIsCachedToo) {
    TempDir tmp;
    CliProbe probe;
    const std::string cli =
        tmp.write("probe-cli", countingScript("echo 'chart-grabber-cli 2026.10.3 (a)'"), true);
    const auto t0 = fs::last_write_time(cli) - std::chrono::hours(2);
    fs::last_write_time(cli, t0);
    LogScope log;
    std::vector<ProbeResult> results;
    const auto ask = [&](const std::string& program, std::size_t expected) {
        probe.check(program, cg::Settings{}, record(results));
        ASSERT_TRUE(runUntil([&] { return results.size() == expected; }, 10000));
        runFor(50);
    };
    EXPECT_EQ(probedCliVersion(cli), "") << "never probed";
    ask(cli, 1);
    EXPECT_EQ(results[0], ProbeResult::TooOld);
    EXPECT_EQ(runsOf(tmp), 1);
    EXPECT_EQ(probedCliVersion(cli), "2026.10.3") << "a too old program still has its version";
    ask(cli, 2);
    EXPECT_EQ(results[1], ProbeResult::TooOld);
    EXPECT_EQ(runsOf(tmp), 1) << "a refusal is cached";
    EXPECT_EQ(log->messages.size(), 1u);

    // Only the size changes (same modification time): the program runs again.
    tmp.write("probe-cli", countingScript("echo 'chart-grabber-cli 2026.10.9 (aa)'"), true);
    fs::last_write_time(cli, t0);
    ask(cli, 3);
    EXPECT_EQ(results[2], ProbeResult::Supported);
    EXPECT_EQ(runsOf(tmp), 2);
    EXPECT_EQ(log->messages.size(), 2u);

    // Same size, same time, other content: the identity is the same, the answer comes from the cache.
    tmp.write("probe-cli", countingScript("echo 'chart-grabber-cli 2026.10.3 (aa)'"), true);
    fs::last_write_time(cli, t0);
    ask(cli, 4);
    EXPECT_EQ(results[3], ProbeResult::Supported);
    EXPECT_EQ(runsOf(tmp), 2);

    // Same size, only the modification time moves: the program runs again and the new answer is used.
    fs::last_write_time(cli, t0 + std::chrono::hours(1));
    ask(cli, 5);
    EXPECT_EQ(results[4], ProbeResult::TooOld);
    EXPECT_EQ(runsOf(tmp), 3);
    EXPECT_EQ(log->messages.size(), 3u);

    // No version line: not cached as a version; a file that is not there cannot be cached at all.
    const std::string none = tmp.write("none-cli", countingScript("echo 'hello'"), true);
    ask(none, 6);
    EXPECT_EQ(probedCliVersion(none), "") << "no version line";
    EXPECT_EQ(probedCliVersion((tmp.dir() / "gone").string()), "") << "no file";
    const std::string gone = (tmp.dir() / "gone").string();
    ask(gone, 7);
    ask(gone, 8);
    EXPECT_EQ(log->messages.size(), 6u) << "a missing program is asked again (logged each time)";
}

// The data folder is one argument even with a space, a quote and Cyrillic; with an empty data folder the
// child gets only the version option and the stop option.
TEST_F(CliProbeTest, AnEmptyDataFolderIsNotPassedAndAnAwkwardOneStaysOneArgument) {
    TempDir tmp;
    CliProbe probe;
    const std::string body = "for a; do printf '%s\\n' \"$a\" >> \"$(dirname \"$0\")/argv\"; done\n"
                             "echo 'chart-grabber-cli 2026.10.9 (abc)'";
    std::vector<ProbeResult> results;
    const auto argvOf = [&](const std::string& name, const std::string& dataDir) {
        const std::string cli = tmp.write(name, countingScript(body), true);
        cg::Settings s;
        s.cliDataDir = dataDir;
        const std::size_t before = results.size();
        probe.check(cli, s, record(results));
        EXPECT_TRUE(runUntil([&] { return results.size() == before + 1; }, 10000));
        std::vector<std::string> lines;
        std::ifstream in(tmp.dir() / "argv");
        for (std::string l; std::getline(in, l);)
            lines.push_back(l);
        fs::remove(tmp.dir() / "argv");
        return lines;
    };
    const std::string dir = (tmp.dir() / "d \xD0\x96 a'b" / "cli-data").string();
    EXPECT_EQ(argvOf("with-data", dir),
              (std::vector<std::string>{"--version", "--datadir=" + dir, "--stop-on-stdin"}));
    EXPECT_EQ(argvOf("no-data", ""), (std::vector<std::string>{"--version", "--stop-on-stdin"}));
}

// 60 real probes of the sources stand-in (a new file each: the cache is keyed on the file) all see the
// version line: the stand-in's stop prelude never ends a `--version` run early.
TEST_F(CliProbeTest, TheSourcesStandInAnswersEveryProbe) {
    TempDir tmp;
    CliProbe probe;
    for (int i = 0; i < 60; ++i) {
        stub::Sources src(tmp.dir(), std::string("p") + std::to_string(i));
        std::vector<ProbeResult> results;
        probe.check(src.cli(), cg::Settings{}, record(results));
        ASSERT_TRUE(runUntil([&] { return !results.empty(); }, 10000)) << "probe " << i;
        ASSERT_EQ(results, std::vector<ProbeResult>{ProbeResult::Supported}) << "probe " << i;
    }
}

// ---- stdout lines are CLI records or nothing, stderr lines are parsed one by one --
namespace {
std::string describe(const StdoutRecord& r) {
    if (const auto* w = std::get_if<WrittenRecord>(&r)) {
        return "W " + w->guid + " " + w->path + " " + std::to_string(w->tiles) + " " +
               std::to_string(w->bytes);
    }
    return "other record";
}
std::string describe(const StderrLine& l) {
    if (const auto* p = std::get_if<PhaseRecord>(&l)) {
        return "P " + p->name + " " + p->guid;
    }
    if (const auto* d = std::get_if<DownloadProgress>(&l)) {
        return "D " + d->guid + " z" + std::to_string(d->zoom) + " " + std::to_string(d->processed) + "/" +
               std::to_string(d->total) + " dl" + std::to_string(d->downloaded) + " " +
               std::to_string(d->downloadedBytes) + "B";
    }
    if (const auto* x = std::get_if<ExportProgress>(&l)) {
        return "X " + x->guid + " " + std::to_string(x->permille);
    }
    const auto& m = std::get<MessageLine>(l);
    const char* kind[] = {"error ", "warning ", "other "};
    return std::string("M ") + kind[static_cast<int>(m.kind)] + m.text;
}
} // namespace

// onText: the stdout lines that are not records, in order, before the exit; the records go to onRecord only.
TEST_F(CliRunnerTest, StdoutTextLinesReachOnTextInOrderAndRecordsOnlyOnRecord) {
    std::vector<std::string> texts, events;
    std::vector<StdoutRecord> records;
    CliRunner::Callbacks cb;
    cb.onText = [&](std::string_view line) {
        texts.emplace_back(line);
        events.push_back("text");
    };
    cb.onRecord = [&](const StdoutRecord& r) {
        records.push_back(r);
        events.push_back("record");
    };
    int exits = 0;
    cb.onExit = [&](int, bool) {
        ++exits;
        events.push_back("exit");
    };
    CliRunner runner;
    std::string error;
    ASSERT_TRUE(runner.start(sh("printf 'chart-grabber-cli 2026.10.9 (x)\\n"
                                "written\\t{G}\\t/p/\xD0\xB0 b\\t3\\t9\\n"
                                "{A-1} map 3857 cache Solent\\n"
                                "written: /x y/a  5 tiles  1.5 KB\\n"
                                "written\\t{H}\\t/q\\t4\\t8\\n"
                                "last line without a newline'"),
                             cb, &error))
        << error;
    ASSERT_TRUE(runUntil([&] { return exits > 0; }, 20000));
    EXPECT_EQ(texts,
              (std::vector<std::string>{"chart-grabber-cli 2026.10.9 (x)", "{A-1} map 3857 cache Solent",
                                        "written: /x y/a  5 tiles  1.5 KB", "last line without a newline"}));
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(describe(records[0]), "W {G} /p/\xD0\xB0 b 3 9");
    EXPECT_EQ(describe(records[1]), "W {H} /q 4 8");
    EXPECT_EQ(events, (std::vector<std::string>{"text", "record", "text", "text", "record", "text", "exit"}));
}

// onText alone (no onRecord): the text lines still arrive.
TEST_F(CliRunnerTest, OnTextWorksWithoutOnRecord) {
    std::vector<std::string> texts;
    CliRunner::Callbacks cb;
    cb.onText = [&](std::string_view line) { texts.emplace_back(line); };
    int exits = 0;
    cb.onExit = [&](int, bool) { ++exits; };
    CliRunner runner;
    std::string error;
    ASSERT_TRUE(runner.start(sh("printf 'one\\nwritten\\t{G}\\t/p\\t3\\t9\\ntwo\\n'"), cb, &error)) << error;
    ASSERT_TRUE(runUntil([&] { return exits > 0; }, 20000));
    EXPECT_EQ(texts, (std::vector<std::string>{"one", "two"})) << "onText without onRecord";
}

// finish() flushes the stderr splitter through the parser: a last stderr line without a newline
// reaches onStderr as the parsed event, after the complete line before it.
TEST_F(CliRunnerTest, LastStderrLineWithoutNewlineReachesOnStderr) {
    Capture run;
    CliRunner runner;
    std::string error;
    ASSERT_TRUE(runner.start(sh("printf 'progress\\texport\\t{G}\\t300\\nprogress\\texport\\t{G}\\t450' >&2"),
                             run.callbacks(), &error))
        << error;
    ASSERT_TRUE(runUntil([&] { return run.exits > 0; }, 20000));
    ASSERT_EQ(run.lines.size(), 2u);
    EXPECT_EQ(describe(run.lines[0]), "X {G} 300");
    EXPECT_EQ(describe(run.lines[1]), "X {G} 450");
    EXPECT_EQ(run.events.back(), "exit");
}

// stdout mixes records with text lines (a `sources` table row, a `written: ...` line, a blank
// line): exactly the records arrive, in order, the last one without a newline. Stderr: records as their
// events, a text progress line of the old format and any other text as Other, error: / warning: with their
// kind, the last line without a newline too.
TEST_F(CliRunnerTest, StdoutDeliversOnlyRecordsAndStderrEveryLineParsed) {
    Capture run;
    CliRunner runner;
    std::string error;
    ASSERT_TRUE(runner.start(sh("printf 'written\\t{G}\\t/p/\xD0\xB0 b\\t3\\t9\\n"
                                "{A-1} map 3857 cache Solent\\n\\nwritten: /x y/a  5 tiles  1.5 KB\\n"
                                "written\\t{H}\\t/q\\t4\\t8'; "
                                "printf 'phase\\testimate\\t{G}\\n"
                                "progress\\tdownload\\t{G}\\t7\\t1\\t2\\t1\\t160499\\t1\\n"
                                "Solent z10  0/0 (~%%)  downloaded 0 tiles 0.0 KB  left ~~ Kb ~~\\n"
                                "error: it broke\\nwarning: \xD0\xB1 odd\\nplain text\\n"
                                "progress\\texport\\t{G}\\t450' >&2"),
                             run.callbacks(), &error))
        << error;
    ASSERT_TRUE(runUntil([&] { return run.exits > 0; }, 20000));
    ASSERT_EQ(run.records.size(), 2u);
    EXPECT_EQ(describe(run.records[0]), "W {G} /p/\xD0\xB0 b 3 9");
    EXPECT_EQ(describe(run.records[1]), "W {H} /q 4 8");
    std::vector<std::string> got;
    for (const StderrLine& l : run.lines)
        got.push_back(describe(l));
    EXPECT_EQ(got, (std::vector<std::string>{
                       "P estimate {G}", "D {G} z7 1/2 dl1 160499B",
                       "M other Solent z10  0/0 (~%)  downloaded 0 tiles 0.0 KB  left ~~ Kb ~~",
                       "M error it broke", "M warning \xD0\xB1 odd", "M other plain text", "X {G} 450"}));
}

// a callback that throws is contained at each Callbacks call: the line is logged with the
// where text and the next callbacks still arrive (the exit one included).
TEST_F(CliRunnerTest, ThrowingCallbacksAreLoggedAndTheRunnerFinishes) {
    LogScope log;
    int exits = 0;
    int records = 0;
    CliRunner::Callbacks cb;
    cb.onRecord = [&](const StdoutRecord&) {
        ++records;
        throw std::runtime_error("record failed");
    };
    cb.onStderr = [](const StderrLine&) { throw 3; };
    cb.onExit = [&](int, bool) {
        ++exits;
        throw std::runtime_error("exit failed");
    };
    CliRunner runner;
    std::string error;
    ASSERT_TRUE(runner.start(sh("printf 'written\\t{G}\\t/p/1\\t1\\t5\\n'; echo oops >&2"), cb, &error))
        << error;
    ASSERT_TRUE(runUntil([&] { return exits > 0; }, 20000));
    EXPECT_EQ(records, 1);
    EXPECT_FALSE(runner.running());
    const auto has = [&](const std::string& l) {
        return std::count(log->messages.begin(), log->messages.end(), l);
    };
    EXPECT_EQ(has("ChartGrabber: runner record callback: record failed"), 1);
    EXPECT_EQ(has("ChartGrabber: runner stderr callback: unknown exception"), 1);
    EXPECT_EQ(has("ChartGrabber: runner exit callback: exit failed"), 1);
}

// ---- the CLI lines of the plugin's log file ----

class CliLogTest : public CliRunnerTest {
protected:
    void SetUp() override {
        CliRunnerTest::SetUp();
        ASSERT_TRUE(cg::openPluginLog((dir_.dir() / "cli.log").string()));
    }
    void TearDown() override {
        cg::closePluginLog();
        CliRunnerTest::TearDown();
    }
    // The file's lines after the 24-character stamp.
    std::vector<std::string> lines() const {
        std::vector<std::string> out;
        std::ifstream in(dir_.dir() / "cli.log");
        for (std::string line; std::getline(in, line);) {
            out.push_back(line.size() > 24 ? line.substr(24) : line);
        }
        return out;
    }
    size_t count(const std::string& prefix) const {
        size_t n = 0;
        for (const std::string& l : lines()) {
            n += l.rfind(prefix, 0) == 0;
        }
        return n;
    }
    bool has(const std::string& text) const {
        const auto all = lines();
        return std::find(all.begin(), all.end(), text) != all.end();
    }
    // Runs `script` with the arguments after the script's name to the end; returns the exit code.
    int run(const std::string& script, const std::vector<std::string>& args) {
        Capture cap;
        CliRunner runner;
        std::vector<std::string> argv = {"/bin/sh", "-c", script, "child"};
        argv.insert(argv.end(), args.begin(), args.end());
        std::string error;
        EXPECT_TRUE(runner.start(argv, cap.callbacks(), &error)) << error;
        EXPECT_TRUE(runUntil([&] { return cap.exits > 0; }, 20000));
        return cap.exitCode;
    }
    TempDir dir_;
};

const char* const kChatty =
    "printf 'written\\t{G}\\t/p/1\\t1\\t5\\n'; echo 'error: oops' >&2; "
    "printf 'progress\\tdownload\\t{G}\\t13\\t50\\t200\\t45\\t1234567\\t30\\n' >&2; exit 3";

TEST_F(CliLogTest, NormalLevelWritesTheStartAndExitLinesOnly) {
    EXPECT_EQ(run(kChatty, {"--proxy", "http://user:secret@proxy:3128", "x"}), 3);
    const std::vector<std::string> all = lines();
    ASSERT_EQ(all.size(), 2u);
    std::smatch start, exit;
    ASSERT_TRUE(
        std::regex_match(all[0], start, std::regex(R"(cli start pid (\d+): /bin/sh \(7 arguments\))")))
        << all[0];
    ASSERT_TRUE(std::regex_match(all[1], exit, std::regex(R"(cli exit pid (\d+): code 3)"))) << all[1];
    EXPECT_EQ(start[1].str(), exit[1].str());
}

TEST_F(CliLogTest, DebugAddsArgvWithoutTheProxyEveryOutputLineAndStderrMessagesButNoProgress) {
    cg::setPluginLogDebug(true);
    EXPECT_EQ(run(kChatty, {"--proxy", "http://user:secret@proxy:3128", "x"}), 3);
    cg::setPluginLogDebug(false);
    EXPECT_EQ(count("debug: cli argv: "), 1u);
    for (const std::string& l : lines()) {
        EXPECT_EQ(l.find("secret"), std::string::npos) << l;
    }
    EXPECT_NE(lines()[1].find("--proxy '<proxy>' x"), std::string::npos) << lines()[1];
    EXPECT_TRUE(has("debug: cli out: written\t{G}\t/p/1\t1\t5"));
    EXPECT_TRUE(has("debug: cli err: error: oops"));
    EXPECT_EQ(count("debug: cli err: "), 1u) << "the progress record is not written";
    EXPECT_EQ(count("debug: cli out: "), 1u);
}

TEST_F(CliLogTest, TheProxyValueIsRedactedInEveryCopyOfAChildLineButTheParserKeepsTheLine) {
    // The URL arrives as $2 (the script text itself would show it in the "cli argv:" line).
    const char* const script = "echo \"connection to $2 failed\" >&2; "
                               "echo \"connection to ${2#http://} failed\"; exit 0";
    const std::string url = "http://user:secret@proxy.example:3128";
    cg::setPluginLogDebug(true);
    Capture cap;
    CliRunner runner;
    std::string error;
    ASSERT_TRUE(runner.start({"/bin/sh", "-c", script, "child", "--proxy", url}, cap.callbacks(), &error))
        << error;
    ASSERT_TRUE(runUntil([&] { return cap.exits > 0; }, 20000));
    EXPECT_TRUE(has("debug: cli err: connection to <proxy> failed"));
    EXPECT_TRUE(has("debug: cli out: connection to <proxy>@proxy.example:3128 failed"));
    for (const std::string& l : lines())
        EXPECT_EQ(l.find("secret"), std::string::npos) << l;
    EXPECT_TRUE(cap.sawStderrText("connection to " + url + " failed")) << "the parser got the original";
    // A later start without --proxy does not keep redacting the old value; lines are copied unchanged.
    cap.exits = 0;
    ASSERT_TRUE(runner.start({"/bin/sh", "-c", script, "child", "-x", url}, cap.callbacks(), &error))
        << error;
    ASSERT_TRUE(runUntil([&] { return cap.exits > 0; }, 20000));
    cg::setPluginLogDebug(false);
    EXPECT_TRUE(has("debug: cli err: connection to " + url + " failed"));
    EXPECT_TRUE(has("debug: cli out: connection to user:secret@proxy.example:3128 failed"));
}

// wx can report the end of a child that ends at once inside wxExecute, before start() returned: the end is
// remembered and the run finishes with that code, once, and the runner is free for the next start.
TEST_F(CliRunnerTest, AnEndReportedInsideStartFinishesTheRunWithThatCode) {
    Capture cap;
    CliRunner runner;
    runner.setEndDuringNextStart(7);
    std::string error;
    ASSERT_TRUE(runner.start(sh("exit 5"), cap.callbacks(), &error)) << error;
    EXPECT_TRUE(runner.running()) << "finished by the loop, not inside start()";
    EXPECT_EQ(cap.exits, 0);
    ASSERT_TRUE(runUntil([&] { return cap.exits > 0; }, 20000));
    runFor(300);
    EXPECT_EQ(cap.exits, 1);
    EXPECT_EQ(cap.exitCode, 7);
    EXPECT_FALSE(runner.running());
    ASSERT_TRUE(runner.start(sh("exit 4"), cap.callbacks(), &error)) << error;
    ASSERT_TRUE(runUntil([&] { return cap.exits > 1; }, 20000));
    EXPECT_EQ(cap.exitCode, 4);
}

// a child that ignores the line is killed once the grace has passed (exit -9), by the timer;
// the kill is logged once.
TEST_F(CliLogTest, AChildThatIgnoresTheStopLineIsKilledAfterTheGrace) {
    Capture run;
    bool ready = false;
    CliRunner::Callbacks cb = run.callbacks();
    cb.onStderr = [&, inner = cb.onStderr](const StderrLine& l) {
        inner(l);
        if (const auto* m = std::get_if<MessageLine>(&l); m && m->text == "ready")
            ready = true;
    };
    CliRunner runner;
    runner.setStopGraceMs(300);
    std::string error;
    ASSERT_TRUE(runner.start(sh("echo ready >&2; exec sleep 30"), cb, &error)) << error;
    ASSERT_TRUE(runUntil([&] { return ready; }, 20000));
    const auto t0 = Clock::now();
    runner.terminate();
    runFor(150);
    EXPECT_EQ(run.exits, 0) << "killed before the grace";
    ASSERT_TRUE(runUntil([&] { return run.exits > 0; }, 10000));
    EXPECT_GE(Clock::now() - t0, std::chrono::milliseconds(290));
    EXPECT_LT(Clock::now() - t0, std::chrono::seconds(5));
    EXPECT_EQ(run.exitCode, -9);
    size_t killed = 0;
    for (const std::string& l : lines())
        killed += std::regex_match(l, std::regex(R"(cli stop pid \d+: no exit after 300 ms; killed)"));
    EXPECT_EQ(killed, 1u);
}

// abandon logs one line with the outcome, and not the grace line.
TEST_F(CliLogTest, AbandonWritesOneLineWithTheOutcome) {
    struct Row {
        const char* script;
        bool stopLine; // the script ends on the stop line
        int waitMs;
        const char* outcome;
    };
    const std::vector<Row> rows = {
        {"echo ready >&2; trap 'exit 143' TERM; k=0; while [ $k -lt 1200 ]; do sleep 0.05; k=$((k + 1)); "
         "done",
         true, 2000, "exited"},
        {"echo ready >&2; exec sleep 30", false, 100, "killed"},
    };
    size_t total = 0;
    for (const Row& row : rows) {
        SCOPED_TRACE(row.outcome);
        Capture run;
        bool ready = false;
        CliRunner::Callbacks cb = run.callbacks();
        cb.onStderr = [&](const StderrLine& l) {
            if (const auto* m = std::get_if<MessageLine>(&l); m && m->text == "ready")
                ready = true;
        };
        CliRunner runner;
        std::string error;
        ASSERT_TRUE(runner.start(row.stopLine ? shStop(row.script) : sh(row.script), cb, &error)) << error;
        ASSERT_TRUE(runUntil([&] { return ready; }, 20000));
        runner.abandon(row.waitMs);
        size_t seen = 0;
        for (const std::string& l : lines())
            seen +=
                std::regex_match(l, std::regex(std::string(R"(cli stop pid \d+: abandoned after \d+ ms \()") +
                                               row.outcome + R"(\))"));
        EXPECT_EQ(seen, 1u);
        EXPECT_EQ(count("cli stop pid"), ++total) << "no grace line";
        runFor(100);
    }
}

TEST_F(CliLogTest, ATerminatedRunSaysStopped) {
    Capture cap;
    bool ready = false;
    CliRunner::Callbacks cb = cap.callbacks();
    cb.onStderr = [&, inner = cb.onStderr](const StderrLine& l) {
        inner(l);
        if (const auto* m = std::get_if<MessageLine>(&l); m && m->text == "ready") {
            ready = true;
        }
    };
    CliRunner runner;
    std::string error;
    ASSERT_TRUE(runner.start(
        shStop("trap 'exit 143' TERM; echo ready >&2; k=0; while [ $k -lt 1200 ]; do sleep 0.05; "
               "k=$((k + 1)); done"),
        cb, &error))
        << error;
    ASSERT_TRUE(runUntil([&] { return ready; }, 20000));
    runner.terminate();
    ASSERT_TRUE(runUntil([&] { return cap.exits > 0; }, 20000));
    const std::vector<std::string> all = lines();
    ASSERT_EQ(all.size(), 2u);
    EXPECT_TRUE(std::regex_match(all[1], std::regex(R"(cli exit pid \d+: code 143, stopped)"))) << all[1];
    // The next run of the same runner is not "stopped".
    cap.exits = 0;
    ASSERT_TRUE(runner.start(sh("exit 0"), cap.callbacks(), &error)) << error;
    ASSERT_TRUE(runUntil([&] { return cap.exits > 0; }, 20000));
    EXPECT_TRUE(std::regex_match(lines().back(), std::regex(R"(cli exit pid \d+: code 0)")))
        << lines().back();
}

// ---- a give-up ends everything the child started ------------------------------------------------------
namespace {
// The pids a stand-in reports on stderr: its own, then the grandchild it started, then "ready". Whatever
// is still alive at the end of the test is killed, so a failed test leaks no process into the next.
struct TreePids {
    long child = 0, grand = 0;
    pid_t group = 0; // the grandchild's process group, read while it was known to run
    bool ready = false;
    // Signals only what is still provably ours: the child while this process has not reaped it (its pid
    // cannot be reused before), the grandchild while it still reports its recorded group.
    ~TreePids() {
        if (child > 1) {
            siginfo_t i{};
            if (::waitid(P_PID, static_cast<id_t>(child), &i, WEXITED | WNOHANG | WNOWAIT) == 0 ||
                errno != ECHILD)
                ::kill(static_cast<pid_t>(child), SIGKILL);
        }
        if (grand > 1 && group > 1 && ::getpgid(static_cast<pid_t>(grand)) == group)
            ::kill(static_cast<pid_t>(grand), SIGKILL);
    }
    CliRunner::Callbacks watch(CliRunner::Callbacks cb) {
        int seen = 0;
        cb.onStderr = [this, seen, inner = cb.onStderr](const StderrLine& l) mutable {
            inner(l);
            const auto* m = std::get_if<MessageLine>(&l);
            if (!m)
                return;
            if (m->text == "ready")
                ready = true;
            else if (seen++ == 0)
                child = std::atol(m->text.c_str());
            else {
                grand = std::atol(m->text.c_str());
                group = ::getpgid(static_cast<pid_t>(grand));
            }
        };
        return cb;
    }
};

// Gone: no such process, or a zombie that init has not reaped yet (the orphan is not the test's child).
bool isGone(long pid) {
    if (::kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH)
        return true;
    std::ifstream in("/proc/" + std::to_string(pid) + "/stat");
    std::string all;
    std::getline(in, all);
    const size_t close = all.rfind(')');
    return close != std::string::npos && close + 2 < all.size() && all[close + 2] == 'Z';
}

// A stand-in that starts a grandchild, reports both pids and then waits (the stop line is ignored).
const char* const kTreeIgnoringStop = "sleep 30 & echo $$ >&2; echo $! >&2; echo ready >&2; "
                                      "while :; do sleep 0.05; done";
} // namespace

TEST_F(CliRunnerTest, TheChildLeadsItsOwnProcessGroup_P0283) {
    TreePids pids;
    Capture run;
    CliRunner runner;
    std::string error;
    ASSERT_TRUE(runner.start(sh(kTreeIgnoringStop), pids.watch(run.callbacks()), &error)) << error;
    ASSERT_TRUE(runUntil([&] { return pids.ready; }, 20000));
    EXPECT_EQ(::getpgid(static_cast<pid_t>(pids.child)), static_cast<pid_t>(pids.child));
    EXPECT_NE(::getpgid(static_cast<pid_t>(pids.child)), ::getpgrp());
    EXPECT_EQ(::getpgid(static_cast<pid_t>(pids.grand)), static_cast<pid_t>(pids.child));
    runner.abandon(0);
}

TEST_F(CliRunnerTest, AbandonKillsTheGrandchildOfAChildThatIgnoresTheStopLine_P0283) {
    TreePids pids;
    Capture run;
    CliRunner runner;
    std::string error;
    ASSERT_TRUE(runner.start(sh(kTreeIgnoringStop), pids.watch(run.callbacks()), &error)) << error;
    ASSERT_TRUE(runUntil([&] { return pids.ready; }, 20000));
    ASSERT_GT(pids.grand, 1);
    runner.abandon(0);
    EXPECT_TRUE(runUntil([&] { return isGone(pids.child) && isGone(pids.grand); }, 3000));
}

TEST_F(CliRunnerTest, AbandonKillsTheGrandchildOfAChildThatExitedOnTheStopLine_P0283) {
    TreePids pids;
    Capture run;
    CliRunner runner;
    std::string error;
    ASSERT_TRUE(runner.start(shStop("trap 'exit 143' TERM; sleep 30 & echo $$ >&2; echo $! >&2; "
                                    "echo ready >&2; while :; do sleep 0.05; done"),
                             pids.watch(run.callbacks()), &error))
        << error;
    ASSERT_TRUE(runUntil([&] { return pids.ready; }, 20000));
    ASSERT_GT(pids.grand, 1);
    const auto t0 = Clock::now();
    runner.abandon(2000);
    EXPECT_LT(Clock::now() - t0, std::chrono::milliseconds(1500));
    EXPECT_TRUE(runUntil([&] { return isGone(pids.grand); }, 3000)) << "the grandchild outlived the leader";
}

// The leader was reaped by somebody else: its pid may belong to another group now, so nothing is sent.
TEST_F(CliRunnerTest, AbandonSendsNothingWhenTheLeaderWasReapedByAnother_P0283) {
    TempDir tmp;
    TreePids pids;
    const std::string pidFile = (tmp.dir() / "pid").string();
    Capture run;
    CliRunner runner;
    std::string error;
    ASSERT_TRUE(runner.start(
        {"/bin/sh", "-c", "sleep 30 & echo $! > \"$0.g\"; echo $$ > \"$0\"; sleep 0.3; exit 0", pidFile},
        run.callbacks(), &error))
        << error;
    for (int i = 0; i < 200 && (pids.child <= 0 || pids.grand <= 0); ++i) {
        std::ifstream(pidFile) >> pids.child;
        std::ifstream(pidFile + ".g") >> pids.grand;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_GT(pids.child, 1);
    ASSERT_GT(pids.grand, 1);
    pids.group = ::getpgid(static_cast<pid_t>(pids.grand));
    siginfo_t info{};
    for (int i = 0; i < 300 && info.si_pid != pids.child; ++i) {
        ::waitid(P_PID, static_cast<id_t>(pids.child), &info, WEXITED | WNOHANG | WNOWAIT);
        if (info.si_pid != pids.child)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_EQ(info.si_pid, static_cast<pid_t>(pids.child));
    ASSERT_EQ(::waitpid(static_cast<pid_t>(pids.child), nullptr, 0), static_cast<pid_t>(pids.child));
    runner.abandon(2000);
    runFor(300);
    EXPECT_FALSE(isGone(pids.grand)) << "the group was killed although its leader was reaped";
}

TEST_F(CliRunnerTest, TheGraceKillAlsoEndsTheGrandchild_P0283) {
    TreePids pids;
    Capture run;
    CliRunner runner;
    runner.setStopGraceMs(200);
    std::string error;
    ASSERT_TRUE(runner.start(sh(kTreeIgnoringStop), pids.watch(run.callbacks()), &error)) << error;
    ASSERT_TRUE(runUntil([&] { return pids.ready; }, 20000));
    runner.terminate();
    ASSERT_TRUE(runUntil([&] { return run.exits > 0; }, 10000));
    EXPECT_EQ(run.exitCode, -9);
    EXPECT_TRUE(runUntil([&] { return isGone(pids.grand); }, 3000));
    runFor(100);
    EXPECT_EQ(run.exits, 1);
}

TEST_F(CliProbeTest, ANoAnswerAlsoEndsTheGrandchild_P0283) {
    TempDir tmp;
    TreePids pids;
    CliProbe probe;
    const std::string cli = tmp.write("tree-cli",
                                      "#!/bin/sh\nd=$(dirname \"$0\")\nsleep 30 &\necho $! > \"$d/g.tmp\"\n"
                                      "mv \"$d/g.tmp\" \"$d/g\"\nwhile :; do sleep 0.05; done\n",
                                      true);
    std::vector<ProbeResult> results;
    probe.setTimeoutMs(300);
    probe.check(cli, cg::Settings{}, record(results));
    ASSERT_TRUE(runUntil([&] { return fs::exists(tmp.dir() / "g"); }, 10000));
    std::ifstream(tmp.dir() / "g") >> pids.grand;
    ASSERT_GT(pids.grand, 1);
    pids.group = ::getpgid(static_cast<pid_t>(pids.grand));
    ASSERT_TRUE(runUntil([&] { return !results.empty(); }, 10000));
    EXPECT_EQ(results, std::vector<ProbeResult>{ProbeResult::NoAnswer});
    EXPECT_TRUE(runUntil([&] { return isGone(pids.grand); }, 3000));
}

// The destructor signals nothing it no longer owns: a pid that is neither this process's unreaped child
// nor in the recorded group belongs to somebody else.
TEST_F(CliRunnerTest, TheTreeCleanupSignalsNoForeignProcess_P0285) {
    FILE* p = ::popen("sh -c 'setsid sh -c \"echo \\$\\$; exec sleep 30 >/dev/null 2>&1\" &'", "r");
    ASSERT_NE(p, nullptr);
    long pid = 0;
    const int got = std::fscanf(p, "%ld", &pid);
    ::pclose(p);
    ASSERT_EQ(got, 1);
    ASSERT_GT(pid, 1);
    ASSERT_EQ(::getpgid(static_cast<pid_t>(pid)), static_cast<pid_t>(pid));
    ASSERT_NE(::getpgid(static_cast<pid_t>(pid)), ::getpgrp());
    {
        TreePids pids;
        pids.child = pid;
        pids.grand = pid;
        pids.group = ::getpgrp();
    }
    runFor(200); // a signal sent by mistake has acted by now
    EXPECT_EQ(::kill(static_cast<pid_t>(pid), 0), 0);
    EXPECT_FALSE(isGone(pid));
    ::kill(static_cast<pid_t>(pid), SIGKILL);
}

// A destructor that ends while the runner still runs cleans up the tree it owns.
TEST_F(CliRunnerTest, TheTreeCleanupEndsTheGrandchildItOwns_P0285) {
    Capture run;
    CliRunner runner;
    std::string error;
    long grand = 0;
    {
        TreePids pids;
        ASSERT_TRUE(runner.start(sh(kTreeIgnoringStop), pids.watch(run.callbacks()), &error)) << error;
        ASSERT_TRUE(runUntil([&] { return pids.ready; }, 20000));
        ASSERT_GT(pids.grand, 1);
        ASSERT_GT(pids.group, 1);
        grand = pids.grand;
    }
    EXPECT_TRUE(runUntil([&] { return isGone(grand); }, 3000));
    runner.abandon(0);
}

// ---- bounded reads of the child's output ----

namespace {
// Ends the `yes` writers g1..gN a stand-in recorded next to itself, if they are still there.
void killWriters(const TempDir& tmp, int count) {
    for (int n = 1; n <= count; ++n) {
        std::ifstream in(tmp.dir() / ("g" + std::to_string(n)));
        long pid = 0;
        in >> pid;
        std::ifstream comm("/proc/" + std::to_string(pid) + "/comm");
        std::string what;
        comm >> what;
        if (pid > 1 && what == "yes")
            ::kill(static_cast<pid_t>(pid), SIGKILL);
    }
}
} // namespace

// A child that never stops writing must not hold the main loop: the limit of the version check still
// fires (the loop keeps ticking), done(NoAnswer) arrives within seconds and the child is killed.
TEST_F(CliProbeTest, AChildThatWritesWithoutPauseDoesNotHoldTheLoopAndEndsInNoAnswer_P0298) {
    TempDir tmp;
    CliProbe probe;
    // The child and four `yes` writers on the same pipe: several writers keep the pipe full.
    const std::string cli = tmp.write(
        "flood-cli",
        countingScript("d=$(dirname \"$0\")\necho $$ > \"$d/pid.tmp\"\nmv \"$d/pid.tmp\" \"$d/pid\"\n"
                       "line=$(printf '%0100d' 0)\n"
                       "for n in 1 2 3 4; do\n  (exec yes \"$line\") &\n  echo $! > \"$d/g$n\"\ndone\n"
                       "while :; do printf '%01000d\\n' 0; done"),
        true);
    LogScope log;
    std::vector<ProbeResult> results;
    Ticker ticker;
    ticker.Start(100);
    probe.setTimeoutMs(300);
    const auto t0 = Clock::now();
    probe.check(cli, cg::Settings{}, record(results));
    const int pid = pidOf(tmp);
    ASSERT_GT(pid, 0) << "the child started";
    ASSERT_TRUE(runUntil([&] { return !results.empty(); }, 15000));
    EXPECT_LT(Clock::now() - t0, std::chrono::seconds(3));
    EXPECT_EQ(results, std::vector<ProbeResult>{ProbeResult::NoAnswer});
    EXPECT_GE(ticker.ticks, 1) << "timers ran while the child was writing";
    EXPECT_TRUE(waitGone(pid)) << "the flooding child was killed";
    killWriters(tmp, 4); // the probe ends the child's tree; this only makes sure
}

// A child that prints a lot and ends at once: every line arrives, the budget loses no tail.
TEST_F(CliRunnerTest, ALargeOutputOfAChildThatEndsAtOnceIsDeliveredCompletely_P0298) {
    // 25000 lines of 9 bytes: 225000 bytes, more than three budgets
    const std::string script = "awk 'BEGIN { for (i = 1; i <= 25000; i++) printf \"L%07d\\n\", i }'";
    int lines = 0;
    std::string last;
    Capture run;
    CliRunner::Callbacks cb = run.callbacks();
    cb.onText = [&](std::string_view l) {
        ++lines;
        last = std::string(l);
    };
    CliRunner runner;
    std::string error;
    ASSERT_TRUE(runner.start(sh(script), cb, &error)) << error;
    runUntilQuiet([&] { return run.exits > 0; }, [&] { return static_cast<size_t>(lines); });
    ASSERT_EQ(run.exits, 1);
    EXPECT_EQ(run.exitCode, 0);
    EXPECT_EQ(lines, 25000);
    EXPECT_EQ(last, "L0025000");
}

// Grandchildren that keep the pipe open and keep writing after the child ended: the final read stops at
// 4 MiB with one log line, so the end of the run is not held for ever. The writers race the reader: when the
// pipe happens to run empty first, the final read legitimately ends early (and logs nothing), so a run is
// repeated until the cap was reached; the cap's rules are checked on that run.
TEST_F(CliRunnerTest, TheFinalReadAfterTheEndStopsAt4MibWithOneLogLine_P0298) {
    bool reached = false;
    for (int attempt = 0; attempt < 8 && !reached; ++attempt) {
        TempDir tmp;
        const std::string cli =
            tmp.write("grand-cli",
                      "#!/bin/sh\nd=$(dirname \"$0\")\nline=$(printf '%0100d' 0)\n"
                      "for n in 1 2 3 4; do\n  (exec yes \"$line\") &\n  echo $! > \"$d/g$n\"\ndone\n"
                      "sleep 0.3\nexit 0\n",
                      true);
        LogScope log;
        Capture run;
        CliRunner::Callbacks cb = run.callbacks();
        size_t lines = 0;
        cb.onText = [&](std::string_view) { ++lines; };
        CliRunner runner;
        std::string error;
        ASSERT_TRUE(runner.start({cli}, cb, &error)) << error;
        const bool ended = runUntil([&] { return run.exits > 0; }, 30000);
        killWriters(tmp, 4); // the greedy children are ended whatever the runner did
        ASSERT_TRUE(ended) << "the end of the run was held by a child that never stops writing";
        EXPECT_EQ(run.exitCode, 0);
        const auto cuts =
            std::count(log->messages.begin(), log->messages.end(), "ChartGrabber: cli output cut");
        // 101 bytes a line: 4 MiB are 41527 lines; at most one more read of 64 KiB, plus what the ticks read
        EXPECT_LE(lines, 52000u);
        if (cuts == 1) {
            EXPECT_GE(lines, 40000u) << "the line is logged only once the cap was read";
        }
        EXPECT_LE(cuts, 1);
        reached = cuts == 1;
    }
    EXPECT_TRUE(reached) << "the cap was not reached in 8 runs";
}

// A line longer than 64 KiB is cut and delivered once; "cli line cut" is logged once per run, again in the
// next run of the same runner.
TEST_F(CliRunnerTest, ACutLineIsLoggedOncePerRun_P0298) {
    const std::string script =
        "awk 'BEGIN { s = sprintf(\"%0100000d\", 0); print s; print s; print \"ok\" }'";
    LogScope log;
    CliRunner runner;
    for (int run = 1; run <= 2; ++run) {
        std::vector<size_t> sizes;
        Capture cap;
        CliRunner::Callbacks cb = cap.callbacks();
        cb.onText = [&](std::string_view l) { sizes.push_back(l.size()); };
        std::string error;
        ASSERT_TRUE(runner.start(sh(script), cb, &error)) << error;
        ASSERT_TRUE(runUntil([&] { return cap.exits > 0; }, 20000));
        EXPECT_EQ(sizes, (std::vector<size_t>{65536, 65536, 2})) << "run " << run;
        EXPECT_EQ(std::count(log->messages.begin(), log->messages.end(), "ChartGrabber: cli line cut"), run);
    }
}

// The version check's time limit is guarded: a done that throws is logged by the handler and not rethrown.
TEST_F(CliProbeTest, AThrowingDoneAtTheTimeLimitIsLoggedAndNotRethrown_P0298) {
    TempDir tmp;
    CliProbe probe;
    const std::string cli = tmp.write("hang-cli", hangScript(), true);
    LogScope log;
    int calls = 0;
    probe.setTimeoutMs(300);
    probe.check(cli, cg::Settings{}, [&](ProbeResult) {
        ++calls;
        throw std::runtime_error("done failed");
    });
    const int pid = pidOf(tmp);
    ASSERT_GT(pid, 0);
    ASSERT_TRUE(runUntil([&] { return calls > 0; }, 15000));
    runFor(200);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(std::count(log->messages.begin(), log->messages.end(),
                         "ChartGrabber: version check timeout: done failed"),
              1);
    EXPECT_FALSE(probe.running());
    EXPECT_TRUE(waitGone(pid));
}
