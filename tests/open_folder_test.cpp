// SPDX-License-Identifier: GPL-3.0-or-later
// open_folder.cpp: under Flatpak the sailor hears about a failed xdg-open; elsewhere nothing changes. The
// xdg-open is a small script in a temp folder that is the only entry of PATH. Not covered: a real portal or
// file manager (checked by hand in a Flatpak lab clone).
#include "chart_folder.h"
#include "open_folder.h"
#include "wx_env.h"

#include <gtest/gtest.h>

#include <wx/app.h>
#include <wx/utils.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
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

TEST(OpenFolderText, P0254NamesTheFolder) {
    EXPECT_EQ(std::string(cg::cannotOpenFolderText(wxString::FromUTF8("/home/u\xC3\xA9/a b")).utf8_str()),
              "Chart Grabber could not open a file manager window. The folder is:\n/home/u\xC3\xA9/a b");
}

} // namespace
