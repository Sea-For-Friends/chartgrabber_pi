// SPDX-License-Identifier: GPL-3.0-or-later
// / the CLI version line and the supported minimum.
#include "cli_text.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

using namespace cg;

// ---- version ----------------------------------------------------------------------------------------

struct VersionCase {
    const char* text;
    bool parsed;
    const char* program;
    int major, minor, patch;
    const char* commit;
    bool supported;
};

TEST(CliVersion, GrammarAndSupport) {
    const VersionCase cases[] = {
        {"chart-grabber-cli 2026.10.0 (abc)\n", true, "chart-grabber-cli", 2026, 10, 0, "abc", false},
        {"chart-grabber-cli 2026.10.0-rc1 (abc)\n", true, "chart-grabber-cli", 2026, 10, 0, "abc", false},
        {"chart-grabber-cli 2026.10.0\r\n", true, "chart-grabber-cli", 2026, 10, 0, "", false},
        {"\xD1\x88\xD0\xB0\xD1\x80\xD1\x82 10.20.30 (x y)\nsecond line 9.9.9\n", true,
         "\xD1\x88\xD0\xB0\xD1\x80\xD1\x82", 10, 20, 30, "x y", false},
        {"old-tiles 0.1.7 (c1)", true, "old-tiles", 0, 1, 7, "c1", false},
        {"old-tiles 0.0.9 (c1)\n", true, "old-tiles", 0, 0, 9, "c1", false},
        {"x 2027.1.0 (c)\n", true, "x", 2027, 1, 0, "c", true},
        {"chart-grabber-cli 2026.10.8-rc1 (abc)\n", true, "chart-grabber-cli", 2026, 10, 8, "abc", true},
        {"", false, "", 0, 0, 0, "", false},
        {"error: Unknown option 'version'.\n", false, "", 0, 0, 0, "", false},
        {"old-tiles 0.2 (abc)\n", false, "", 0, 0, 0, "", false},
        {"old-tiles 0.2.x\n", false, "", 0, 0, 0, "", false},
        {"old-tiles 1234567890.0.0\n", false, "", 0, 0, 0, "", false},
    };
    for (const VersionCase& c : cases) {
        SCOPED_TRACE(c.text);
        const auto v = parseVersionLine(c.text);
        ASSERT_EQ(v.has_value(), c.parsed);
        if (v) {
            EXPECT_EQ(v->program, c.program);
            EXPECT_EQ(v->number.major, c.major);
            EXPECT_EQ(v->number.minor, c.minor);
            EXPECT_EQ(v->number.patch, c.patch);
            EXPECT_EQ(v->commit, c.commit);
        }
        EXPECT_EQ(supportedCli(v), c.supported);
    }
    EXPECT_TRUE(atLeast({0, 10, 0}, {0, 9, 99}));
    EXPECT_FALSE(atLeast({0, 9, 99}, {0, 10, 0}));
    EXPECT_FALSE(atLeast({1, 0, 0}, {2, 0, 0}));
    EXPECT_TRUE(atLeast({2, 0, 0}, {2, 0, 0}));
}

// / calendar versions compare numerically field by field (2026.9.30 is OLDER than 2026.10.8,
// which a string compare gets wrong); 2026.10.8 is the minimum, 2026.10.7 is too old (no
// --stop-on-stdin).
TEST(CliVersion, SupportedFromTheMinimumOn) {
    struct Row {
        const char* line;
        bool supported;
    };
    const Row rows[] = {
        {"chart-grabber-cli 2026.10.5 (a)\n", false},
        {"chart-grabber-cli 2026.9.9 (a)\n", false},
        {"chart-grabber-cli 2026.10.8 (a)\n", true},
        {"chart-grabber-cli 2026.10.7 (a)\n", false},
        {"chart-grabber-cli 2026.10.6 (a)\n", false},
        {"chart-grabber-cli 2026.10.4 (a)\n", false},
        {"chart-grabber-cli 2026.11.0 (a)\n", true},
        {"chart-grabber-cli 2027.1.0 (a)\n", true},
        {"old-tiles 0.1.0 (a)\n", false},
    };
    for (const Row& r : rows) {
        SCOPED_TRACE(r.line);
        EXPECT_EQ(supportedCli(parseVersionLine(r.line)), r.supported);
    }
    EXPECT_FALSE(supportedCli(std::nullopt));
    EXPECT_TRUE(atLeast({2026, 10, 1}, {2026, 9, 3}));
    EXPECT_FALSE(atLeast({2026, 9, 30}, {2026, 10, 0}));
    EXPECT_TRUE(atLeast({2027, 1, 0}, {2026, 10, 1}));
    EXPECT_FALSE(atLeast({2026, 10, 1}, {2027, 1, 0}));
}

// / the pinned release (a real capture) is supported.
TEST(CliVersion, PinnedReleaseCapturedVersionLine) {
    std::ifstream in(std::string(CG_FIXTURES_DIR) + "/cli-2026.10.8/version.stdout", std::ios::binary);
    ASSERT_TRUE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    const auto v = parseVersionLine(ss.str());
    ASSERT_TRUE(v);
    EXPECT_EQ(v->commit, "e0a207dadb59");
    EXPECT_TRUE(supportedCli(v));
}
