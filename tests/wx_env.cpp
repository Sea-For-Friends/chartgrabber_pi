// SPDX-License-Identifier: GPL-3.0-or-later
#include "wx_env.h"

#include <gtest/gtest.h>

#include <wx/app.h>
#include <wx/debug.h>
#include <wx/display.h>
#include <wx/init.h>
#include <wx/log.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <execinfo.h>
#include <filesystem>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

// A wx assertion (wxASSERT, wxCHECK) must never open wxGTK's modal "Assert failure" box: on a display it
// waits for a click and hangs the gate. It is printed to stderr with a stack and fails the running test.
void failOnWxAssert(const wxString& file, int line, const wxString& func, const wxString& cond,
                    const wxString& msg) {
    const std::string text =
        wxString::Format("wx assertion failed: %s(%d): %s: '%s' %s", file, line, func, cond, msg)
            .utf8_string();
    std::fprintf(stderr, "%s\n", text.c_str());
    void* frames[40];
    backtrace_symbols_fd(frames, backtrace(frames, 40), 2);
    ADD_FAILURE() << text;
}

#ifdef CG_WX_GUI_TESTS
// The GUI app of the window-test binary. On a display server with no output it keeps running (so wxTheApp is
// a real wxApp) but reports IsGUI false: the window tests skip on that and no window is created.
bool g_noOutput = false;
class TestApp : public wxApp {
public:
    bool IsGUI() const override { return !g_noOutput && wxApp::IsGUI(); }
};
#endif

bool isDigits(const std::string& s) {
    return !s.empty() && s.find_first_not_of("0123456789") == std::string::npos;
}

class WxEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        // drop WAYLAND_DISPLAY and force GDK_BACKEND=x11. This does NOT protect a direct run: with a
        // live DISPLAY (:0, or XWayland) the windows still open on the desktop. Only running under xvfb-run
        // -a does.
        unsetenv("WAYLAND_DISPLAY");
        setenv("GDK_BACKEND", "x11", 1);
        wxSetAssertHandler(failOnWxAssert);
        int argc = 0;
        wxChar** argv = nullptr;
#ifdef CG_WX_GUI_TESTS
        // The window-test binary (cg_gui_tests): a GUI app with real windows. Without a display, or when it
        // cannot be opened, the console app is used and the window tests skip.
        const char* display = std::getenv("DISPLAY");
        if (display != nullptr && *display != '\0') {
            wxApp::SetInstance(new TestApp);
            started_ = wxEntryStart(argc, argv);
            if (!started_)
                delete wxApp::GetInstance();
            else if (wxDisplay::GetCount() < 1) {
                // A display server with no output (monitor off): every top-level window asserts in
                // wxDisplay::GetClientArea (P7 landing, 2026-10-15). The window tests skip.
                std::fprintf(stderr, "wx_env: the display has no output: window tests are NOT run\n");
                g_noOutput = true;
            }
        }
#endif
        if (!started_) {
            wxAppConsole::SetInstance(new wxAppConsole);
            started_ = wxEntryStart(argc, argv);
        }
        // A GUI app logs into a message box that waits for a click (at wxEntryCleanup): a logged error made
        // a test process hang for ever. Every test process logs to stderr instead.
        delete wxLog::SetActiveTarget(new wxLogStderr);
    }
    void TearDown() override {
        if (started_)
            wxEntryCleanup();
    }

private:
    bool started_ = false;
};

[[maybe_unused]] ::testing::Environment* const kWxEnvironment =
    ::testing::AddGlobalTestEnvironment(new WxEnvironment);

} // namespace

std::vector<int> processesUnder(const std::string& dir) {
    namespace fs = std::filesystem;
    std::vector<int> found;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator("/proc", ec)) {
        const std::string name = entry.path().filename().string();
        if (!isDigits(name) || std::stoi(name) == getpid())
            continue;
        std::string argv; // a process may exit while it is read: whatever was read counts
        if (FILE* f = std::fopen((entry.path() / "cmdline").c_str(), "rb")) {
            char buf[4096];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
                argv.append(buf, n);
            std::fclose(f);
        }
        std::error_code cwdEc;
        const std::string cwd = fs::read_symlink(entry.path() / "cwd", cwdEc).string();
        if (argv.find(dir) != std::string::npos || (!cwdEc && cwd.compare(0, dir.size(), dir) == 0))
            found.push_back(std::stoi(name));
    }
    return found;
}

void expectNoProcessesUnder(const std::string& dir, std::chrono::milliseconds grace) {
    auto left = processesUnder(dir);
    const auto until = std::chrono::steady_clock::now() + grace; // a child that was just told to stop
    while (!left.empty() && std::chrono::steady_clock::now() < until) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        left = processesUnder(dir);
    }
    for (int pid : left) {
        ADD_FAILURE() << "process " << pid << " still runs under " << dir << " after the test";
        kill(pid, SIGKILL);
    }
}
