// SPDX-License-Identifier: GPL-3.0-or-later
// the one logging path of src/wx : OpenCPN's log and the file.
#include "log_capture.h"
#include "ocpn_fakes.h"
#include "plugin_log.h"
#include "wx_env.h"

#include <gtest/gtest.h>

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

} // namespace
