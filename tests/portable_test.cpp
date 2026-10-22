// SPDX-License-Identifier: GPL-3.0-or-later
// the Windows rules of src/core/portable.h, Linux-tested through the Os argument.
#include "portable.h"
#include "settings.h"

#include <gtest/gtest.h>

#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace cg {
namespace {

struct Win {
    std::string_view in;
    Os os;
    bool want;
};

TEST(WindowsCommandLine, SpecQuotingTable) {
    const std::vector<std::pair<std::vector<std::string>, std::string>> rows = {
        {{"C:\\Program Files\\Chart Grabber CLI\\chart-grabber-cli.exe", "run"},
         "\"C:\\Program Files\\Chart Grabber CLI\\chart-grabber-cli.exe\" run"},
        {{"--cache", "C:\\My dir\\"}, "--cache \"C:\\My dir\\\\\""},
        {{"a\"b"}, "a\\\"b"},
        {{""}, "\"\""},
        {{"C:\\a\\\\b"}, "C:\\a\\\\b"},
        {{"x\\\\\"y"}, "x\\\\\\\\\\\"y"},
        {{"no\\trailing\\"}, "no\\trailing\\"},
        {{"Карты моря"}, "\"Карты моря\""},
        {{"--bbox", "-1.5,50,2,51"}, "--bbox -1.5,50,2,51"},
        // Python 3 subprocess.list2cmdline, run 2026-10-22: a backslash run before a quote
        // inside a quoted argument, and a run of 2 at the end of a quoted argument.
        {{"a b\\\"c"}, "\"a b\\\\\\\"c\""},
        {{"x y\\\\"}, "\"x y\\\\\\\\\""},
    };
    for (const auto& [argv, want] : rows) {
        EXPECT_EQ(windowsCommandLine(argv), want) << want;
    }
}

TEST(IsAbsolutePath, Table) {
    const std::vector<Win> rows = {
        {"C:\\x", Os::Windows, true},
        {"c:/x", Os::Windows, true},
        {"\\\\srv\\share\\x", Os::Windows, true},
        {"//srv/share", Os::Windows, true},
        {"C:x", Os::Windows, false},
        {"\\x", Os::Windows, false},
        {"x", Os::Windows, false},
        {"", Os::Windows, false},
        {"/x", Os::Posix, true},
        {"C:\\x", Os::Posix, false},
        {"x", Os::Posix, false},
    };
    for (const Win& r : rows) {
        EXPECT_EQ(isAbsolutePath(r.in, r.os), r.want) << r.in;
    }
}

TEST(JoinPath, Table) {
    struct Row {
        std::string_view dir, name;
        Os os;
        std::string want;
    };
    const std::vector<Row> rows = {
        {"/a", "b", Os::Posix, "/a/b"},
        {"/a/", "b", Os::Posix, "/a/b"},
        {"", "b", Os::Posix, "b"},
        {"Карты", "море", Os::Posix, "Карты/море"},
        {"C:\\x", "y", Os::Windows, "C:\\x\\y"},
        {"C:\\x\\", "y", Os::Windows, "C:\\x\\y"},
        {"C:/x", "y", Os::Windows, "C:/x\\y"},
        {"C:/x/", "y", Os::Windows, "C:/x/y"},
        {"", "y", Os::Windows, "y"},
    };
    for (const Row& r : rows) {
        EXPECT_EQ(joinPath(r.dir, r.name, r.os), r.want) << r.dir << " + " << r.name;
    }
}

TEST(NativeSeparators, Table) {
    struct Row {
        std::string_view in;
        Os os;
        std::string want;
    };
    const std::vector<Row> rows = {
        {"C:/Users/Ann/Charts/Карта моря.mbtiles", Os::Windows, "C:\\Users\\Ann\\Charts\\Карта моря.mbtiles"},
        {"//srv/share/x.mbtiles", Os::Windows, "\\\\srv\\share\\x.mbtiles"},
        {"C:\\Users\\Ann\\a.mbtiles", Os::Windows, "C:\\Users\\Ann\\a.mbtiles"},
        {"C:\\a/b", Os::Windows, "C:\\a\\b"},
        {"", Os::Windows, ""},
        {"/home/a b/x'y.mbtiles", Os::Posix, "/home/a b/x'y.mbtiles"},
        {"/home/a\\b.mbtiles", Os::Posix, "/home/a\\b.mbtiles"},
    };
    for (const Row& r : rows) {
        EXPECT_EQ(nativeSeparators(r.in, r.os), r.want) << r.in;
    }
}

TEST(ParentDir, Table) {
    struct Row {
        std::string_view path;
        Os os;
        std::string want;
    };
    const std::vector<Row> rows = {
        {"/x", Os::Posix, "/"},
        {"/a/b", Os::Posix, "/a"},
        {"/x/", Os::Posix, "/x"},
        {"b", Os::Posix, ""},
        {"/Карты/море", Os::Posix, "/Карты"},
        {"C:\\x", Os::Windows, "C:\\"},
        {"C:\\a\\b", Os::Windows, "C:\\a"},
        {"C:/a/b", Os::Windows, "C:/a"},
        {"\\x", Os::Windows, "\\"},
        {"b", Os::Windows, ""},
        // UNC: the parent of a file under a share is the share folder; of the share itself, the server part
        //.
        {"\\\\srv\\share\\charts\\a.mbtiles", Os::Windows, "\\\\srv\\share\\charts"},
        {"\\\\srv\\share\\a.mbtiles", Os::Windows, "\\\\srv\\share"},
    };
    for (const Row& r : rows) {
        EXPECT_EQ(parentDir(r.path, r.os), r.want) << r.path;
    }
}

TEST(SameDir, Table) {
    struct Row {
        std::string_view a, b;
        Os os;
        bool want;
    };
    const std::vector<Row> rows = {
        {"/x", "/x/", Os::Posix, true},
        {"/", "/", Os::Posix, true},
        {"/a", "/b", Os::Posix, false},
        {"Карты", "Карты/", Os::Posix, true},
        {"C:\\Charts", "c:/charts/", Os::Windows, true},
        {"C:\\Charts", "c:/charts/", Os::Posix, false},
        {"C:\\", "C:/", Os::Windows, true},
        {"C:\\x\\", "C:\\x", Os::Windows, true},
        {"C:\\Карты", "c:\\карты", Os::Windows, false}, // non-ASCII compared as bytes
        {"/", "", Os::Posix, false},
        {"C:\\", "C:", Os::Windows, false}, // a root keeps its separator
    };
    for (const Row& r : rows) {
        EXPECT_EQ(sameDir(r.a, r.b, r.os), r.want) << r.a << " / " << r.b;
    }
}

TEST(SameVolume, Table) {
    struct Row {
        std::string_view a, b;
        bool want;
    };
    const std::vector<Row> rows = {
        {"C:\\a", "c:/b", true},
        {"C:\\a", "D:\\a", false},
        {"\\\\srv\\share\\a", "//SRV/Share/b", true},
        {"\\\\srv\\share\\a", "\\\\srv\\other\\b", false},
        {"\\\\srv\\share", "C:\\a", false},
        {"a", "a", false}, // no volume
    };
    for (const Row& r : rows) {
        EXPECT_EQ(sameVolume(r.a, r.b), r.want) << r.a << " / " << r.b;
    }
}

TEST(DefaultCliPaths, PosixIsTheInstallPath) {
    const std::vector<std::string> want = {"/usr/bin/chart-grabber-cli"};
    EXPECT_EQ(defaultCliPaths(Os::Posix, "C:\\Data", "C:\\PF"), want);
    EXPECT_EQ(defaultCliPaths(Os::Posix, "", ""), want);
}

TEST(DefaultCliPaths, WindowsOrderWithOrWithoutTrailingSeparator) {
    const std::vector<std::string> want = {"C:\\Data\\CG\\cli\\chart-grabber-cli.exe",
                                           "C:\\Program Files\\Chart Grabber CLI\\chart-grabber-cli.exe"};
    EXPECT_EQ(defaultCliPaths(Os::Windows, "C:\\Data\\CG", "C:\\Program Files"), want);
    EXPECT_EQ(defaultCliPaths(Os::Windows, "C:\\Data\\CG\\", "C:\\Program Files\\"), want);
}

TEST(DefaultCliPaths, EmptyStringDropsItsEntry) {
    EXPECT_EQ(defaultCliPaths(Os::Windows, "", "C:\\PF"),
              (std::vector<std::string>{"C:\\PF\\Chart Grabber CLI\\chart-grabber-cli.exe"}));
    EXPECT_EQ(defaultCliPaths(Os::Windows, "C:\\Data", ""),
              (std::vector<std::string>{"C:\\Data\\cli\\chart-grabber-cli.exe"}));
    EXPECT_TRUE(defaultCliPaths(Os::Windows, "", "").empty());
}

TEST(FsPath, CyrillicPathWithSpaceAndQuoteRoundTrips) {
    const std::string name = "Карты моря/it's";
    EXPECT_EQ(utf8(fsPath(name)), name);
    EXPECT_EQ(fsPath(name).string(), name);
}

TEST(LocalTime, MatchesLocaltimeR) {
    const std::time_t t = 1700000000;
    std::tm want{};
    ASSERT_NE(localtime_r(&t, &want), nullptr);
    const std::tm got = localTime(t);
    EXPECT_EQ(got.tm_year, want.tm_year);
    EXPECT_EQ(got.tm_mon, want.tm_mon);
    EXPECT_EQ(got.tm_mday, want.tm_mday);
    EXPECT_EQ(got.tm_hour, want.tm_hour);
    EXPECT_EQ(got.tm_min, want.tm_min);
    EXPECT_EQ(got.tm_sec, want.tm_sec);
}

class ProgramFileTest : public testing::Test {
protected:
    void SetUp() override {
        root = std::filesystem::temp_directory_path() /
               (std::string("cg_portable_test_") + std::to_string(::getpid()));
        std::filesystem::create_directories(root);
    }
    void TearDown() override { std::filesystem::remove_all(root); }
    std::string file(const std::string& name, mode_t mode) {
        const auto p = root / name;
        std::ofstream(p) << "x";
        ::chmod(p.c_str(), mode);
        return p.string();
    }
    std::filesystem::path root;
};

TEST_F(ProgramFileTest, PosixNeedsTheExecuteBit) {
    EXPECT_TRUE(isProgramFile(file("run", 0755), Os::Posix));
    EXPECT_FALSE(isProgramFile(file("data", 0644), Os::Posix));
    std::filesystem::create_directories(root / "d");
    EXPECT_FALSE(isProgramFile((root / "d").string(), Os::Posix));
}

TEST_F(ProgramFileTest, WindowsNeedsExeSuffixAnyCase) {
    EXPECT_TRUE(isProgramFile(file("x.exe", 0644), Os::Windows));
    EXPECT_TRUE(isProgramFile(file("X.EXE", 0644), Os::Windows));
    EXPECT_FALSE(isProgramFile(file("x", 0755), Os::Windows)); // executable bit does not count
    std::filesystem::create_directories(root / "d.exe");
    EXPECT_FALSE(isProgramFile((root / "d.exe").string(), Os::Windows));
}

TEST(HasExeSuffix, Table) {
    EXPECT_TRUE(hasExeSuffix("tool.exe"));
    EXPECT_TRUE(hasExeSuffix("Tool.EXE"));
    EXPECT_FALSE(hasExeSuffix("tool"));
    EXPECT_FALSE(hasExeSuffix("tool.exe.bak"));
    EXPECT_FALSE(hasExeSuffix("exe"));
}

} // namespace
} // namespace cg
