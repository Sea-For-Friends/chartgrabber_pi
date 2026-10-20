// SPDX-License-Identifier: GPL-3.0-or-later
// The guards of the test environment itself (tests/wx_env.cpp): a wx assertion fails the test instead of
// opening a dialog, and a child process left behind by a fixture is found, reported and killed.
#include "wx_env.h"

#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

#include <wx/debug.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace {

namespace fs = std::filesystem;

TEST(TestGuards, AWxAssertionFailsTheTestAndOpensNoDialog) {
#if wxDEBUG_LEVEL
    EXPECT_NONFATAL_FAILURE(wxFAIL_MSG("provoked by TestGuards"), "provoked by TestGuards");
#else
    GTEST_SKIP() << "wx is built without assertions: NOT run";
#endif
}

// A /bin/sh that blocks in `read` on a pipe the test holds (no child of its own, no CPU); `dir` is its $0.
class Sleeper {
public:
    explicit Sleeper(const std::string& dir) {
        int fds[2];
        EXPECT_EQ(pipe(fds), 0);
        write_ = fds[1];
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, fds[0], 0);
        posix_spawn_file_actions_addclose(&actions, fds[1]);
        const char* argv[] = {"/bin/sh", "-c", "read x", dir.c_str(), nullptr};
        EXPECT_EQ(posix_spawn(&pid_, "/bin/sh", &actions, nullptr, const_cast<char**>(argv), environ), 0);
        posix_spawn_file_actions_destroy(&actions);
        close(fds[0]);
    }
    ~Sleeper() {
        kill(pid_, SIGKILL);
        waitpid(pid_, nullptr, 0);
        close(write_);
    }
    pid_t pid() const { return pid_; }
    // Waits (bounded) until the child's exec has happened: its /proc/<pid>/cmdline names the dir. Before that
    // it still has the test binary's command line.
    bool waitExec(const std::string& dir) const {
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < until) {
            std::ifstream in("/proc/" + std::to_string(pid_) + "/cmdline", std::ios::binary);
            const std::string argv((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (argv.find(dir) != std::string::npos)
                return true;
            usleep(5000);
        }
        return false;
    }

private:
    pid_t pid_ = 0;
    int write_ = -1;
};

TEST(TestGuards, AProcessWithTheDirInItsCommandLineIsFoundAndAnUnrelatedDirFindsNothing) {
    char tmpl[] = "/tmp/cg-guard-XXXXXX";
    ASSERT_NE(mkdtemp(tmpl), nullptr);
    const std::string dir = tmpl;
    {
        Sleeper s(dir);
        ASSERT_TRUE(s.waitExec(dir));
        const auto found = processesUnder(dir);
        EXPECT_NE(std::find(found.begin(), found.end(), s.pid()), found.end());
        EXPECT_TRUE(processesUnder(dir + "-other").empty());
    }
    fs::remove_all(dir);
}

TEST(TestGuards, ALeftoverChildFailsTheTestAndIsKilled) {
    char tmpl[] = "/tmp/cg-guard-XXXXXX";
    ASSERT_NE(mkdtemp(tmpl), nullptr);
    const std::string dir = tmpl;
    {
        Sleeper s(dir);
        ASSERT_TRUE(s.waitExec(dir));
        EXPECT_NONFATAL_FAILURE(expectNoProcessesUnder(dir, std::chrono::milliseconds(100)),
                                "still runs under");
        int status = 0;
        pid_t got = 0;
        for (int i = 0; i < 100 && got == 0; ++i) { // the kill has been sent: a short poll, never a hang
            got = waitpid(s.pid(), &status, WNOHANG);
            if (got == 0)
                usleep(20000);
        }
        EXPECT_EQ(got, s.pid());
        EXPECT_TRUE(WIFSIGNALED(status));
    }
    EXPECT_TRUE(processesUnder(dir).empty());
    fs::remove_all(dir);
}

TEST(TestGuards, NoChildMeansNoFailure) {
    char tmpl[] = "/tmp/cg-guard-XXXXXX";
    ASSERT_NE(mkdtemp(tmpl), nullptr);
    expectNoProcessesUnder(tmpl);
    fs::remove_all(tmpl);
}

} // namespace
