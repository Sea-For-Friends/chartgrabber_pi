// SPDX-License-Identifier: GPL-3.0-or-later
// The one logging path of src/wx: OpenCPN's log and the file.
#include "log_capture.h"
#include "ocpn_fakes.h"
#include "plugin_log.h"
#include "wx_env.h"

#include <gtest/gtest.h>

#include <wx/app.h>
#include <wx/utils.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

namespace {

namespace fs = std::filesystem;

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

class PluginLogTest : public ::testing::Test {
protected:
    void SetUp() override {
        char tmpl[] = "/tmp/cgpl-XXXXXX";
        ASSERT_NE(mkdtemp(tmpl), nullptr);
        root_ = tmpl;
        file_ = root_ / "log" / "chartgrabber.log";
    }
    void TearDown() override {
        cg::closePluginLog();
        ocpn_fake::privateData.clear();
        ocpn_fake::privateDataNull = false;
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    // The file's lines with the 23-character stamp and the space after it removed.
    std::vector<std::string> fileLines() const {
        std::vector<std::string> lines;
        std::istringstream in(slurp(file_));
        for (std::string line; std::getline(in, line);) {
            EXPECT_TRUE(std::regex_match(line, std::regex(R"(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3} .*)")))
                << line;
            lines.push_back(line.size() > 24 ? line.substr(24) : line);
        }
        return lines;
    }
    fs::path root_, file_;
};

TEST_F(PluginLogTest, LogLineGoesToOpenCpnAndTheFileWithAPercentKeptLiteral) {
    ASSERT_TRUE(cg::openPluginLog(file_.string()));
    LogCapture capture;
    cg::logLine(wxString::FromUTF8("x %s y \xD0\x96"));
    EXPECT_EQ(capture.lines, std::vector<std::string>{"ChartGrabber: x %s y \xD0\x96"});
    EXPECT_EQ(fileLines(), std::vector<std::string>{"x %s y \xD0\x96"});
}

TEST_F(PluginLogTest, WarningAddsAPrefixInTheFileOnly) {
    ASSERT_TRUE(cg::openPluginLog(file_.string()));
    LogCapture capture;
    cg::logWarning("careful");
    EXPECT_EQ(capture.lines, std::vector<std::string>{"ChartGrabber: careful"});
    EXPECT_EQ(fileLines(), std::vector<std::string>{"warning: careful"});
}

TEST_F(PluginLogTest, LogFileWritesTheFileOnly) {
    ASSERT_TRUE(cg::openPluginLog(file_.string()));
    LogCapture capture;
    cg::logFile("quiet");
    EXPECT_TRUE(capture.lines.empty());
    EXPECT_EQ(fileLines(), std::vector<std::string>{"quiet"});
}

TEST_F(PluginLogTest, DebugLinesNeedTheSwitch) {
    ASSERT_TRUE(cg::openPluginLog(file_.string()));
    LogCapture capture;
    EXPECT_FALSE(cg::pluginLogDebug());
    cg::logDebug("hidden");
    EXPECT_TRUE(fileLines().empty());
    cg::setPluginLogDebug(true);
    EXPECT_TRUE(cg::pluginLogDebug());
    cg::logDebug("shown");
    EXPECT_EQ(fileLines(), std::vector<std::string>{"debug: shown"});
    EXPECT_TRUE(capture.lines.empty());
    cg::closePluginLog();
    EXPECT_FALSE(cg::pluginLogDebug()); // closing switches debug off
}

TEST_F(PluginLogTest, WithoutAFileEveryHelperStillReachesOpenCpnsLog) {
    LogCapture capture;
    cg::logLine("a");
    cg::logWarning("b");
    cg::logFile("c");
    cg::setPluginLogDebug(true);
    cg::logDebug("d");
    cg::setPluginLogDebug(false);
    EXPECT_EQ(capture.lines, (std::vector<std::string>{"ChartGrabber: a", "ChartGrabber: b"}));
    EXPECT_EQ(cg::pluginLogPath(), "");
    EXPECT_FALSE(cg::openPluginLog("")); // an empty path: false, nothing logged
    EXPECT_EQ(capture.lines.size(), 2u);
}

TEST_F(PluginLogTest, AnUnwritablePathLogsOneLineAndFails) {
    std::ofstream(root_ / "blocker") << "x";
    LogCapture capture;
    EXPECT_FALSE(cg::openPluginLog((root_ / "blocker" / "x.log").string()));
    ASSERT_EQ(capture.lines.size(), 1u);
    EXPECT_EQ(capture.lines[0].rfind("ChartGrabber: cannot write the log file " +
                                         (root_ / "blocker" / "x.log").string() + ": ",
                                     0),
              0u)
        << capture.lines[0];
    EXPECT_EQ(cg::pluginLogPath(), "");
}

TEST_F(PluginLogTest, DefaultPathFollowsThePrivateLocation) {
    ocpn_fake::privateData = wxString::FromUTF8("/data/\xD0\x96 d");
    EXPECT_EQ(cg::defaultPluginLogPath(), "/data/\xD0\x96 d/plugins/chartgrabber/chartgrabber.log");
    ocpn_fake::privateData.clear();
    EXPECT_EQ(cg::defaultPluginLogPath(), "");
    ocpn_fake::privateData = "/data";
    ocpn_fake::privateDataNull = true;
    EXPECT_EQ(cg::defaultPluginLogPath(), "");
}

TEST_F(PluginLogTest, WaylandWithOpenGlIsTheOnlyCoveredCase) {
    EXPECT_TRUE(cg::waylandOpenGlCovers("wayland", "on"));
    EXPECT_FALSE(cg::waylandOpenGlCovers("wayland", "off"));
    EXPECT_FALSE(cg::waylandOpenGlCovers("wayland", "unknown"));
    EXPECT_FALSE(cg::waylandOpenGlCovers("x11", "on"));
    EXPECT_FALSE(cg::waylandOpenGlCovers("none", "on"));
}

// Yields to the event loop for `span`, so the stall timer ticks and CallAfter actions run.
void pumpFor(std::chrono::milliseconds span) {
    const auto end = std::chrono::steady_clock::now() + span;
    while (std::chrono::steady_clock::now() < end) {
        wxTheApp->Yield(true);
        wxMilliSleep(5);
    }
}

// Blocks the GUI thread for `span`: a busy wait inside a CallAfter action, so no event is dispatched
// meanwhile.
void blockGuiThread(std::chrono::milliseconds span) {
    wxTheApp->CallAfter([span] {
        const auto end = std::chrono::steady_clock::now() + span;
        while (std::chrono::steady_clock::now() < end) {
        }
    });
}

// The values of the "main loop stalled N ms" lines, in order.
std::vector<int> stallValues(const std::vector<std::string>& lines) {
    std::vector<int> values;
    const std::regex line(R"(main loop stalled (\d+) ms)");
    std::smatch match;
    for (const std::string& text : lines) {
        if (std::regex_search(text, match, line))
            values.push_back(std::stoi(match[1].str()));
    }
    return values;
}

TEST_F(PluginLogTest, TheStallTimerRunsOnlyWhileDebugIsOn) {
    if (!wxTheApp || !wxTheApp->IsGUI())
        GTEST_SKIP() << "no display: the GUI environment was NOT run";
    ASSERT_TRUE(cg::openPluginLog(file_.string()));
    EXPECT_FALSE(cg::stallWatchRunning());
    cg::setPluginLogDebug(true);
    EXPECT_TRUE(cg::stallWatchRunning());
    cg::setPluginLogDebug(false);
    EXPECT_FALSE(cg::stallWatchRunning());
}

TEST_F(PluginLogTest, ClosingThePluginLogStopsTheStallTimer) {
    if (!wxTheApp || !wxTheApp->IsGUI())
        GTEST_SKIP() << "no display: the GUI environment was NOT run";
    ASSERT_TRUE(cg::openPluginLog(file_.string()));
    cg::setPluginLogDebug(true);
    ASSERT_TRUE(cg::stallWatchRunning());
    cg::closePluginLog();
    EXPECT_FALSE(cg::stallWatchRunning());
}

TEST_F(PluginLogTest, NoStallIsLoggedWhileDebugIsOff) {
    if (!wxTheApp || !wxTheApp->IsGUI())
        GTEST_SKIP() << "no display: the GUI environment was NOT run";
    ASSERT_TRUE(cg::openPluginLog(file_.string()));
    blockGuiThread(std::chrono::milliseconds(600));
    pumpFor(std::chrono::milliseconds(800));
    EXPECT_TRUE(stallValues(fileLines()).empty());
}

TEST_F(PluginLogTest, ABlockedGuiThreadWritesALineWhileDebugIsOn) {
    if (!wxTheApp || !wxTheApp->IsGUI())
        GTEST_SKIP() << "no display: the GUI environment was NOT run";
    ASSERT_TRUE(cg::openPluginLog(file_.string()));
    cg::setPluginLogDebug(true);
    pumpFor(std::chrono::milliseconds(350)); // ticks first, so the block is measured against a tick
    blockGuiThread(std::chrono::milliseconds(600));
    pumpFor(std::chrono::milliseconds(700));
    cg::setPluginLogDebug(false);
    const std::vector<int> stalls = stallValues(fileLines());
    ASSERT_FALSE(stalls.empty());
    EXPECT_GE(*std::max_element(stalls.begin(), stalls.end()), 450);
}

TEST_F(PluginLogTest, ClosingWritesTheSummaryBeforeTheSessionEndLine) {
    if (!wxTheApp || !wxTheApp->IsGUI())
        GTEST_SKIP() << "no display: the GUI environment was NOT run";
    ASSERT_TRUE(cg::openPluginLog(file_.string()));
    cg::setPluginLogDebug(true);
    pumpFor(std::chrono::milliseconds(350));
    blockGuiThread(std::chrono::milliseconds(600));
    pumpFor(std::chrono::milliseconds(700));
    cg::closePluginLog();
    const std::vector<std::string> lines = fileLines();
    ASSERT_GE(lines.size(), 2u);
    EXPECT_EQ(lines.back(), "session end");
    EXPECT_TRUE(std::regex_match(lines[lines.size() - 2],
                                 std::regex(R"(main loop stalls: [1-9]\d*, longest \d+ ms, total \d+ ms)")))
        << lines[lines.size() - 2];
}

TEST_F(PluginLogTest, TurningDebugOnAgainKeepsTheCountOfTheRunningTimer) {
    if (!wxTheApp || !wxTheApp->IsGUI())
        GTEST_SKIP() << "no display: the GUI environment was NOT run";
    ASSERT_TRUE(cg::openPluginLog(file_.string()));
    cg::setPluginLogDebug(true);
    pumpFor(std::chrono::milliseconds(350));
    blockGuiThread(std::chrono::milliseconds(600));
    pumpFor(std::chrono::milliseconds(700));
    cg::setPluginLogDebug(true); // the Settings OK applies the switch again: no restart of the count
    cg::closePluginLog();
    const std::vector<std::string> lines = fileLines();
    ASSERT_GE(lines.size(), 2u);
    EXPECT_TRUE(std::regex_match(lines[lines.size() - 2],
                                 std::regex(R"(main loop stalls: [1-9]\d*, longest \d+ ms, total \d+ ms)")))
        << lines[lines.size() - 2];
}

TEST_F(PluginLogTest, ClosingWithoutStallsWritesNoSummary) {
    if (!wxTheApp || !wxTheApp->IsGUI())
        GTEST_SKIP() << "no display: the GUI environment was NOT run";
    ASSERT_TRUE(cg::openPluginLog(file_.string()));
    cg::setPluginLogDebug(true);
    pumpFor(std::chrono::milliseconds(350));
    cg::closePluginLog();
    const std::vector<std::string> lines = fileLines();
    ASSERT_FALSE(lines.empty());
    EXPECT_EQ(lines.back(), "session end");
    EXPECT_TRUE(stallValues(lines).empty());
    for (const std::string& text : lines)
        EXPECT_EQ(text.find("main loop stalls"), std::string::npos) << text;
}

} // namespace
