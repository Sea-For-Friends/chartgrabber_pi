// SPDX-License-Identifier: GPL-3.0-or-later
// open_folder.cpp: under Flatpak the sailor hears about a failed xdg-open; elsewhere nothing changes. The
// xdg-open is a small script in a temp folder that is the only entry of PATH. Not covered: a real portal or
// file manager (checked by hand in a Flatpak sandbox).
#include "chart_folder.h"
#include "open_folder.h"
#include "wx_env.h"

#include <gtest/gtest.h>

#include <wx/app.h>
#include <wx/event.h>
#include <wx/process.h>
#include <wx/utils.h>

#include <chrono>
#include <cstdlib>
#include <execinfo.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

class OpenFolderTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!wxTheApp || !wxTheApp->IsGUI())
            GTEST_SKIP() << "no display: the open folder tests were NOT run";
        root_ = fs::temp_directory_path() / ("cg_open_folder_" + std::to_string(::getpid()));
        fs::create_directories(root_ / "bin");
        const char* old = std::getenv("PATH");
        oldPath_ = old ? old : "";
        ::setenv("PATH", (root_ / "bin").c_str(), 1);
        cg::sandboxProbe() = [this] { return cg::Sandbox{flatpak_, "/home/nobody", {}}; };
    }
    void TearDown() override {
        if (!wxTheApp || !wxTheApp->IsGUI())
            return;
        cg::sandboxProbe() = [] { return cg::detectSandbox(); };
        cg::shutdownOpenFolder();
        ::setenv("PATH", oldPath_.c_str(), 1);
        expectNoProcessesUnder(root_.string());
        fs::remove_all(root_);
    }

    // xdg-open: records its first argument, then ends with `code`.
    void fakeXdgOpen(int code) {
        const fs::path script = root_ / "bin" / "xdg-open";
        // Only builtins: PATH holds nothing else. A partial write never equals the expected argument.
        std::ofstream(script) << "#!/bin/sh\nprintf '%s' \"$1\" > '" << (root_ / "arg").string() << "'\nexit "
                              << code << "\n";
        fs::permissions(script, fs::perms::owner_all);
        fs::remove(root_ / "arg");
    }
    // Runs the event loop until `done` or 10 s.
    bool pumpUntil(const std::function<bool()>& done) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!done() && std::chrono::steady_clock::now() < end) {
            wxTheApp->Yield(true);
            wxMilliSleep(5);
        }
        return done();
    }
    // The first argument the fake got, once it ran.
    std::string arg() {
        std::ifstream in(root_ / "arg");
        std::ostringstream s;
        s << in.rdbuf();
        return s.str();
    }

    fs::path root_;
    std::string oldPath_;
    bool flatpak_ = true;
    std::vector<std::string> failed_;
    std::function<void(const wxString&)> record() {
        return [this](const wxString& f) { failed_.push_back(std::string(f.utf8_str())); };
    }
};

// A path with a space, a quote and a non-ASCII letter reaches xdg-open as ONE argument.
const std::string kFolder = "/home/u\xC3\xA9/Charts/Chart 'sources'";

TEST_F(OpenFolderTest, P0254FlatpakReportsAnXdgOpenThatExitsWithAnError) {
    fakeXdgOpen(3);
    cg::openFolderInFileManager(wxString::FromUTF8(kFolder), record());
    ASSERT_TRUE(pumpUntil([&] { return !failed_.empty(); }));
    EXPECT_EQ(failed_, std::vector<std::string>{kFolder});
    EXPECT_EQ(arg(), kFolder);
}

TEST_F(OpenFolderTest, P0254FlatpakSaysNothingWhenXdgOpenSucceeds) {
    fakeXdgOpen(0);
    cg::openFolderInFileManager(wxString::FromUTF8("/home/u/ok"), record());
    ASSERT_TRUE(pumpUntil([&] { return arg() == "/home/u/ok"; }));
    fakeXdgOpen(5); // a later failure shows the loop did run past the first end
    cg::openFolderInFileManager(wxString::FromUTF8("/home/u/bad"), record());
    ASSERT_TRUE(pumpUntil([&] { return !failed_.empty(); }));
    EXPECT_EQ(failed_, std::vector<std::string>{"/home/u/bad"});
}

TEST_F(OpenFolderTest, P0254FlatpakReportsAMissingXdgOpen) {
    cg::openFolderInFileManager(wxString::FromUTF8(kFolder), record());
    ASSERT_TRUE(pumpUntil([&] { return !failed_.empty(); }));
    EXPECT_EQ(failed_, std::vector<std::string>{kFolder});
}

TEST_F(OpenFolderTest, P0254OutsideFlatpakTheFailureIsNotReported) {
    flatpak_ = false;
    fakeXdgOpen(7);
    cg::openFolderInFileManager(wxString::FromUTF8(kFolder), record());
    wxTheApp->Yield(true);
    ASSERT_TRUE(pumpUntil([&] { return arg() == kFolder; }))
        << "wxLaunchDefaultApplication did not run xdg-open";
    for (int i = 0; i < 40; ++i) { // let the end of the child be seen
        wxTheApp->Yield(true);
        wxMilliSleep(5);
    }
    EXPECT_TRUE(failed_.empty());
}

// The names of the functions on the calling stack, one text.
std::string stackNow() {
    void* frames[64];
    const int n = ::backtrace(frames, 64);
    char** names = ::backtrace_symbols(frames, n);
    std::string all;
    for (int i = 0; names && i < n; ++i)
        all += std::string(names[i]) + "\n";
    std::free(names);
    return all;
}

// Takes the end of a plain wxProcess the way wx delivers it: its handler runs inside wx's process-end
// callback.
class EndWatcher : public wxEvtHandler {
public:
    EndWatcher() { Bind(wxEVT_END_PROCESS, &EndWatcher::onEnd, this); }
    std::string stack;
    bool ended = false;

private:
    void onEnd(wxProcessEvent&) {
        stack = stackNow();
        ended = true;
    }
};

// One failure report per failed xdg-open, and it comes from the event loop, not from inside wx's process-end
// callback (the failure box is a modal loop). The control run shows that the stack text tells the two apart.
TEST_F(OpenFolderTest, P0298TheFailureIsReportedOnceAndOutsideTheProcessEndCallback) {
    fakeXdgOpen(1);
    EndWatcher watcher;
    auto* control = new wxProcess(&watcher);
    const wchar_t* argv[] = {L"xdg-open", L"/control", nullptr};
    ASSERT_NE(wxExecute(argv, wxEXEC_ASYNC, control), 0);
    ASSERT_TRUE(pumpUntil([&] { return watcher.ended; }));
    delete control; // a process with a parent handler is deleted by it, here after its end callback returned
    ASSERT_NE(watcher.stack.find("wxProcess"), std::string::npos)
        << "the stack text does not show wx's process-end callback; the check below proves nothing\n"
        << watcher.stack;

    std::vector<std::string> stacks;
    cg::openFolderInFileManager(wxString::FromUTF8(kFolder), [&](const wxString& f) {
        failed_.push_back(std::string(f.utf8_str()));
        stacks.push_back(stackNow());
    });
    EXPECT_TRUE(failed_.empty()) << "not called before the start returned";
    ASSERT_TRUE(pumpUntil([&] { return !failed_.empty(); }));
    for (int i = 0; i < 40; ++i) { // a second report would come from the same end
        wxTheApp->Yield(true);
        wxMilliSleep(5);
    }
    EXPECT_EQ(failed_, std::vector<std::string>{kFolder});
    ASSERT_EQ(stacks.size(), 1u);
    EXPECT_EQ(stacks[0].find("wxProcess"), std::string::npos) << stacks[0];
}

// A xdg-open that is still running when the plugin is shut down: the shutdown returns at once and the later
// end of the child reaches nothing of the plugin (no failure report; ASan sees no use of freed objects).
TEST_F(OpenFolderTest, P0298ShutdownDoesNotWaitForARunningXdgOpenAndItsLaterEndCallsNothing) {
    const fs::path fifo = root_ / "fifo";
    ASSERT_EQ(::mkfifo(fifo.c_str(), 0600), 0);
    const fs::path script = root_ / "bin" / "xdg-open";
    // `read` is a builtin: the stub waits until the test writes into the fifo, then leaves a mark and fails.
    std::ofstream(script) << "#!/bin/sh\nprintf '%s' started > '" << (root_ / "started").string()
                          << "'\nread line < '" << fifo.string() << "'\nprintf '%s' ended > '"
                          << (root_ / "ended").string() << "'\nexit 1\n";
    fs::permissions(script, fs::perms::owner_all);
    cg::openFolderInFileManager(wxString::FromUTF8(kFolder), record());
    ASSERT_TRUE(pumpUntil([&] { return fs::exists(root_ / "started"); }));

    const auto t0 = std::chrono::steady_clock::now();
    cg::shutdownOpenFolder();
    EXPECT_LT(std::chrono::steady_clock::now() - t0, std::chrono::milliseconds(500));

    {
        std::ofstream release(fifo); // the stub's read returns
        release << "go\n";
    }
    ASSERT_TRUE(pumpUntil([&] { return fs::exists(root_ / "ended"); }));
    for (int i = 0; i < 100; ++i) { // the end of the child is seen by wx, which deletes the process itself
        wxTheApp->Yield(true);
        wxMilliSleep(5);
    }
    EXPECT_TRUE(failed_.empty());
}

TEST(OpenFolderText, P0254NamesTheFolder) {
    EXPECT_EQ(std::string(cg::cannotOpenFolderText(wxString::FromUTF8("/home/u\xC3\xA9/a b")).utf8_str()),
              "Chart Grabber could not open a file manager window. The folder is:\n/home/u\xC3\xA9/a b");
}

} // namespace
