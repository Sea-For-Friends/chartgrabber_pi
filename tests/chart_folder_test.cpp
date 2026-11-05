// SPDX-License-Identifier: GPL-3.0-or-later
#include "chart_folder.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {

const std::string kInfo = "[Application]\nname=org.opencpn.OpenCPN\n\n[Context]\nshared=network;ipc;\n"
                          "filesystems=home;/mnt/\xD0\x94\xD0\xB8\xD1\x81\xD0\xBA "
                          "1:rw;/media/ro:ro;!/srv;xdg-documents;~/Charts x;host-os;"
                          "/data:create;\n\n[Instance]\nfilesystems=/not-this-section;\n";

cg::Sandbox flatpak(std::vector<std::string> granted = {}) {
    return {true, "/home/u\xC3\xA9rg", std::move(granted)};
}

} // namespace

TEST(ChartFolder, ParsesOnlyReadWriteAbsoluteGrantsOfTheContextSection) {
    // "xdg-documents", "host-os", ":ro", "!" and the [Instance] section give nothing; "~/" is under home
    EXPECT_EQ(cg::flatpakGranted(kInfo, "/home/u"),
              (std::vector<std::string>{"/home/u", "/mnt/\xD0\x94\xD0\xB8\xD1\x81\xD0\xBA 1",
                                        "/home/u/Charts x", "/data"}));
    EXPECT_EQ(cg::flatpakGranted("[Context]\r\nfilesystems=host;\r\n", "/h"),
              (std::vector<std::string>{"/"}));
    EXPECT_TRUE(cg::flatpakGranted("[Context]\nfilesystems=home;~/x;\n", "").empty()) << "no HOME: nothing";
}

TEST(ChartFolder, DetectionReadsTheInfoFileAndAMissingFileIsNoFlatpak) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "cg-chart-folder-test";
    std::filesystem::create_directories(dir);
    const std::string info = (dir / "flatpak-info").string();
    std::ofstream(info) << "[Context]\nfilesystems=/mnt/boat;\n";
    const cg::Sandbox on = cg::detectSandbox(info);
    EXPECT_TRUE(on.flatpak);
    EXPECT_EQ(on.granted, (std::vector<std::string>{"/mnt/boat"}));
    EXPECT_FALSE(cg::detectSandbox((dir / "absent").string()).flatpak);
    std::filesystem::remove_all(dir);
}

TEST(ChartFolder, GeneralRuleOnEveryPlatform) {
    using V = cg::FolderVerdict;
    const cg::Sandbox plain;
    // folder, isDefault, exists, writable -> verdict
    EXPECT_EQ(cg::checkChartFolder("/x/a", false, true, true, plain), V::Ok);
    EXPECT_EQ(cg::checkChartFolder("/x/a", false, true, false, plain), V::CannotWrite);
    EXPECT_EQ(cg::checkChartFolder("/x/a", false, false, false, plain), V::CannotWrite)
        << "chosen and missing";
    EXPECT_EQ(cg::checkChartFolder("/x/a", true, false, false, plain), V::Ok) << "the default may be missing";
    EXPECT_EQ(cg::checkChartFolder("/x/a", true, true, false, plain), V::CannotWrite)
        << "the default that exists must be writable";
}

TEST(ChartFolder, FlatpakRefusesOutsideHomeAndAllowsHomeAndGrants) {
    using V = cg::FolderVerdict;
    EXPECT_EQ(cg::checkChartFolder("/mnt/charts", false, true, true, flatpak()), V::FlatpakOutsideHome);
    EXPECT_EQ(cg::checkChartFolder("/home/u\xC3\xA9rg/Charts/a b", false, true, true, flatpak()), V::Ok);
    EXPECT_EQ(cg::checkChartFolder("/home/u\xC3\xA9rg", false, true, true, flatpak()), V::Ok)
        << "home itself";
    EXPECT_EQ(cg::checkChartFolder("/home/u\xC3\xA9rgey/c", false, true, true, flatpak()),
              V::FlatpakOutsideHome)
        << "a sibling that shares the prefix";
    EXPECT_EQ(cg::checkChartFolder("/home/u\xC3\xA9rg/../other/c", false, true, true, flatpak()),
              V::FlatpakOutsideHome);
    EXPECT_EQ(cg::checkChartFolder("/mnt/boat/c", false, true, true, flatpak({"/mnt/boat/"})), V::Ok);
    EXPECT_EQ(cg::checkChartFolder("/mnt/other", false, true, true, flatpak({"/mnt/boat"})),
              V::FlatpakOutsideHome);
    EXPECT_EQ(cg::checkChartFolder("/mnt/any", false, true, true, flatpak({"/"})), V::Ok)
        << "filesystems=host";
    EXPECT_EQ(cg::checkChartFolder("/mnt/c", true, false, false, flatpak()), V::FlatpakOutsideHome)
        << "outside wins over the default exception";
    EXPECT_EQ(cg::checkChartFolder("/home/u\xC3\xA9rg/c", false, true, false, flatpak()), V::CannotWrite);
}
