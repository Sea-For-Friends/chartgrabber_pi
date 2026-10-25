// SPDX-License-Identifier: GPL-3.0-or-later
// countSourceEntries: the CLI's own walk of a chart-sources folder. Names only, no
// wx.
#include "sources_folder.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

class SourcesFolder : public ::testing::Test {
protected:
    void SetUp() override {
        char tmpl[] = "/tmp/cg-srcfolder-XXXXXX";
        ASSERT_NE(mkdtemp(tmpl), nullptr);
        root_ = tmpl;
    }
    void TearDown() override {
        std::error_code ec;
        fs::permissions(root_ / "locked", fs::perms::owner_all, ec); // so remove_all can enter it
        fs::remove_all(root_, ec);
    }
    void file(const fs::path& rel) {
        fs::create_directories((root_ / rel).parent_path());
        std::ofstream(root_ / rel) << "x";
    }
    void dir(const fs::path& rel) { fs::create_directories(root_ / rel); }
    int count(int limit = 100) const { return cg::countSourceEntries(root_.string(), limit); }
    fs::path root_;
};

} // namespace

TEST_F(SourcesFolder, DefaultFolderNameIsChartSources) {
    EXPECT_EQ(std::string(cg::kSourcesFolderName), "Chart sources");
}

TEST_F(SourcesFolder, FileAndFolderEntriesCountWhateverTheCase) {
    file("a.zmp");
    dir("B.ZMP");
    file("c.txt");
    EXPECT_EQ(count(), 2);
}

TEST_F(SourcesFolder, NestedUserSourceIsCounted) { // the user ruling: Sea/Extra/OpenSeaMap.zmp
    file("Sea/Extra/OpenSeaMap.zmp");
    EXPECT_EQ(count(), 1);
}

TEST_F(SourcesFolder, DepthSixCountsDepthSevenDoesNot) {
    file("1/2/3/4/5/6/a.zmp"); // inside the folder of depth 6
    EXPECT_EQ(count(), 1);
    file("1/2/3/4/5/6/7/b.zmp"); // depth 7: never reached
    EXPECT_EQ(count(), 1);
}

TEST_F(SourcesFolder, DotFoldersAreNotEnteredButDotFilesCount) {
    file(".hidden/a.zmp");
    EXPECT_EQ(count(), 0);
    file(".x.zmp");
    EXPECT_EQ(count(), 1);
}

TEST_F(SourcesFolder, AZmpFolderCountsOnceAndIsNotEntered) {
    file("y.zmp/z.zmp");
    EXPECT_EQ(count(), 1);
}

TEST_F(SourcesFolder, SourceJsonFilesCountWhateverTheCase) {
    file("a.source.json");
    file("B.SOURCE.JSON");
    file("c.json");
    file("d.source.json.txt");
    EXPECT_EQ(count(), 2);
}

TEST_F(SourcesFolder, ASymlinkToASourceJsonFileCounts) {
    file("real.txt");
    fs::create_symlink(root_ / "real.txt", root_ / "l.source.json");
    EXPECT_EQ(count(), 1);
}

TEST_F(SourcesFolder, ASourceJsonFolderIsNotCountedButEntered) {
    file("x.source.json/inner/a.zmp");
    file("x.source.json/b.source.json");
    EXPECT_EQ(count(), 2);
    file("y.source.json/z.txt");
    EXPECT_EQ(count(), 2);
}

TEST_F(SourcesFolder, ASourceJsonAtDepthSixCountsDepthSevenDoesNot) {
    file("1/2/3/4/5/6/7/b.source.json");
    EXPECT_EQ(count(), 0);
    file("1/2/3/4/5/6/a.source.json");
    EXPECT_EQ(count(), 1);
}

TEST_F(SourcesFolder, LimitStopsTheCount) {
    file("a.zmp");
    file("b.zmp");
    file("c.zmp");
    EXPECT_EQ(count(1), 1);
    EXPECT_EQ(count(2), 2);
    EXPECT_EQ(count(0), 0);
}

TEST_F(SourcesFolder, MissingRootAndFileRootGiveZero) {
    EXPECT_EQ(cg::countSourceEntries((root_ / "nope").string(), 1), 0);
    file("a.zmp");
    EXPECT_EQ(cg::countSourceEntries((root_ / "a.zmp").string(), 1), 0);
}

TEST_F(SourcesFolder, UnreadableSubfolderIsSkippedTheRestCounts) {
    if (geteuid() == 0)
        GTEST_SKIP() << "root reads every folder";
    file("locked/a.zmp");
    file("b.zmp");
    fs::permissions(root_ / "locked", fs::perms::none);
    EXPECT_EQ(count(), 1);
}

TEST_F(SourcesFolder, ASymlinkLoopIsBounded) {
    dir("a");
    fs::create_directory_symlink(root_, root_ / "a" / "loop");
    file("a/s.zmp");
    const int n = count(1000000);
    EXPECT_EQ(n, 3); // the folder "a" is seen at depths 1, 3 and 5 only
}

TEST_F(SourcesFolder, SpacesQuotesAndCyrillicNamesWork) {
    file("my sources/it's/Карта.zmp");
    EXPECT_EQ(count(), 1);
}

TEST_F(SourcesFolder, TheVisitedEntriesAreCapped) {
    dir("d");
    for (int i = 0; i < 20100; ++i) {
        std::string name = "f"; // not "f" + to_string(i): GCC 12 -O3 -Wrestrict false positive
        name += std::to_string(i);
        std::ofstream(root_ / "d" / name) << "";
    }
    file("d/zzz.zmp"); // sorted after the 20000th entry of d: never reached
    file("z.zmp");
    EXPECT_EQ(count(), 1); // only z.zmp of the root
}
