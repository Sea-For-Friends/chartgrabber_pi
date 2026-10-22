// SPDX-License-Identifier: GPL-3.0-or-later
// the plugin version is calendar-based YYYY.MM.N and is the same in the
// CMake project version, the plugin class, metadata.xml and the tarball name; CHANGELOG.md has [Unreleased].
#include "plugin_version.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

namespace {

std::string slurp(const std::string& path) {
    std::ifstream f(path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

TEST(PluginVersion, CalendarShapeAndPartsMatchTheString) {
    const std::string v = cg::kPluginVersionString;
    EXPECT_TRUE(std::regex_match(v, std::regex("[0-9]{4}\\.[0-9]{1,2}\\.[0-9]+"))) << v;
    EXPECT_GE(cg::kPluginVersionMajor, 2026);
    EXPECT_GE(cg::kPluginVersionMinor, 1);
    EXPECT_LE(cg::kPluginVersionMinor, 12);
    EXPECT_GE(cg::kPluginVersionPatch, 0);
    EXPECT_EQ(v, std::to_string(cg::kPluginVersionMajor) + "." + std::to_string(cg::kPluginVersionMinor) +
                     std::string(".") + std::to_string(cg::kPluginVersionPatch));
}

TEST(PluginVersion, OpenCpnParserReadsTheFourDigitMajor) {
    // The parse of OpenCPN 5.10.2 (model/src/semantic_vers.cpp:51), applied to the metadata version.
    int major = -1, minor = -1, patch = -1, post = 0;
    const int r = std::sscanf(cg::kPluginVersionString, "%d.%d.%d.%d", &major, &minor, &patch, &post);
    EXPECT_EQ(r, 3);
    EXPECT_EQ(major, cg::kPluginVersionMajor);
    EXPECT_EQ(minor, cg::kPluginVersionMinor);
    EXPECT_EQ(patch, cg::kPluginVersionPatch);
}

TEST(PluginVersion, MetadataAndTarballNameCarryTheVersion) {
    const std::string v = cg::kPluginVersionString;
    const std::string meta = slurp(CG_BUILD_DIR "/metadata.xml");
    EXPECT_NE(meta.find("<version>" + v + "</version>"), std::string::npos) << meta;
    std::string name = slurp(CG_BUILD_DIR "/tarball-name.txt");
    EXPECT_EQ(name.rfind("chartgrabber_pi-" + v + "-", 0), 0u) << name;
}

TEST(PluginVersion, ChangelogHasTheCurrentVersionSection) {
    const std::string log = slurp(CG_SOURCE_DIR "/CHANGELOG.md");
    EXPECT_NE(log.find(std::string("\n## ") + CG_VERSION_STRING + "\n"), std::string::npos);
}
