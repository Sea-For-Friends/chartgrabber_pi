// SPDX-License-Identifier: GPL-3.0-or-later
// The CLI's default executable name is chart-grabber-cli. The names are literals here, never kDefaultCliName itself.
#include "cli_runner.h"
#include "settings.h"
#include "wx_env.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>

namespace fs = std::filesystem;

namespace {

class CliNameTest : public testing::Test {
protected:
    void SetUp() override {
        std::string tmpl = (fs::temp_directory_path() / "cgname.XXXXXX").string();
        ASSERT_NE(mkdtemp(tmpl.data()), nullptr);
        dir_ = tmpl;
        if (const char* v = std::getenv("PATH")) {
            oldPath_ = v;
        }
    }
    void TearDown() override {
        setenv("PATH", oldPath_.c_str(), 1);
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }
    std::string makeExecutable(const std::string& name) const {
        const fs::path p = dir_ / name;
        {
            std::ofstream f(p);
            f << "#!/bin/sh\n";
        }
        chmod(p.c_str(), 0755);
        return p.string();
    }
    fs::path dir_;
    std::string oldPath_;
};

} // namespace

TEST(CliName, DefaultNameIsChartGrabberCli) {
    EXPECT_EQ(std::string(cg::kDefaultCliName), "chart-grabber-cli");
}

TEST_F(CliNameTest, PathSearchFindsChartGrabberCliAndNotTheOldName) {
    const std::string oldOnly = makeExecutable("old-tiles");
    setenv("PATH", dir_.string().c_str(), 1);
    std::string why;
    EXPECT_FALSE(resolveCli(cg::Settings(), &why, {"/nonexistent/none"}, std::string(getenv("PATH"))))
        << "an old old-tiles on PATH must not be picked by default";
    const std::string fresh = makeExecutable("chart-grabber-cli");
    const auto found =
        resolveCli(cg::Settings(), nullptr, {"/nonexistent/none"}, std::string(getenv("PATH")));
    ASSERT_TRUE(found);
    EXPECT_EQ(*found, fresh);
    (void)oldOnly;
}

TEST_F(CliNameTest, ExplicitPathToAnOldOldTilesStillWorks) {
    const std::string oldCli = makeExecutable("old-tiles");
    makeExecutable("chart-grabber-cli");
    setenv("PATH", dir_.string().c_str(), 1);
    cg::Settings s;
    s.cliPath = oldCli;
    std::string why = "untouched";
    const auto found = resolveCli(s, &why, {"/nonexistent/none"});
    ASSERT_TRUE(found);
    EXPECT_EQ(*found, oldCli);
    EXPECT_EQ(why, "untouched");
}
