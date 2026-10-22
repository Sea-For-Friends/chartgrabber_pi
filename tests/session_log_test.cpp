// SPDX-License-Identifier: GPL-3.0-or-later
// cg::SessionLog : open, line format, flush, cap and roll.
#include "session_log.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
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

void put(const fs::path& p, const std::string& text) {
    std::ofstream(p, std::ios::binary) << text;
}

class SessionLogTest : public ::testing::Test {
protected:
    void SetUp() override {
        char tmpl[] = "/tmp/cgsl-XXXXXX";
        ASSERT_NE(mkdtemp(tmpl), nullptr);
        root_ = tmpl;
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    fs::path root_;
};

TEST_F(SessionLogTest, OpenTruncatesAndRemovesTheRolledFile) {
    const fs::path file = root_ / "a.log";
    put(file, "old text\n");
    put(root_ / "a.log.1", "older\n");
    cg::SessionLog log;
    ASSERT_TRUE(log.open(file.string()));
    EXPECT_EQ(slurp(file), "");
    EXPECT_FALSE(fs::exists(root_ / "a.log.1"));
    log.write("S", "", "new");
    EXPECT_EQ(slurp(file), "S new\n");
    EXPECT_EQ(log.path(), file.string());
}

TEST_F(SessionLogTest, UnwritablePathFailsWithAReasonAndLaterWritesDoNothing) {
    put(root_ / "blocker", "a regular file");
    cg::SessionLog log;
    std::string why;
    EXPECT_FALSE(log.open((root_ / "blocker" / "sub" / "x.log").string(), cg::kSessionLogCapBytes, &why));
    EXPECT_FALSE(why.empty());
    EXPECT_FALSE(log.isOpen());
    EXPECT_EQ(log.path(), "");
    log.write("S", "", "lost");
    log.endHeader();
    EXPECT_FALSE(fs::exists(root_ / "blocker" / "sub"));
}

TEST_F(SessionLogTest, MissingParentFoldersAreCreated) {
    const fs::path file = root_ / "a b" / "it's" / "\xD0\x9F\xD0\xB0\xD0\xBF\xD0\xBA\xD0\xB0" / "x.log";
    cg::SessionLog log;
    ASSERT_TRUE(log.open(file.string()));
    log.write("S", "", "hello");
    EXPECT_EQ(slurp(file), "S hello\n");
}

TEST_F(SessionLogTest, LineFormatCleansControlBytes) {
    const fs::path file = root_ / "a.log";
    cg::SessionLog log;
    ASSERT_TRUE(log.open(file.string()));
    log.write("2026-10-16 09:05:03.007", "debug: ",
              std::string_view("a\tb\nc\x01"
                               "d"));
    EXPECT_EQ(slurp(file), "2026-10-16 09:05:03.007 debug: a\tb c d\n");
}

TEST(SessionLogFormat, StampLiteralsAndClamping) {
    std::tm t{};
    t.tm_year = 126; // 2026
    t.tm_mon = 9;
    t.tm_mday = 16;
    t.tm_hour = 9;
    t.tm_min = 5;
    t.tm_sec = 3;
    EXPECT_EQ(cg::formatLogStamp(t, 7), "2026-10-16 09:05:03.007");
    EXPECT_EQ(cg::formatLogStamp(t, 999), "2026-10-16 09:05:03.999");
    EXPECT_EQ(cg::formatLogStamp(t, 1000), "2026-10-16 09:05:03.999");
    EXPECT_EQ(cg::formatLogStamp(t, -1), "2026-10-16 09:05:03.000");
}

TEST(SessionLogFormat, LongTextIsCutBeforeAnUnfinishedUtf8Sequence) {
    // 4095 ASCII bytes, then a 2-byte letter (bytes 4095 and 4096), then more text.
    std::string text(4095, 'x');
    text += "\xD0\x96";
    text += std::string(903, 'y');
    ASSERT_EQ(text.size(), 5000u);
    const std::string cut = cg::cleanLogText(text);
    EXPECT_EQ(cut.size(), 4095u);
    EXPECT_EQ(cut, std::string(4095, 'x'));
    // A cut that falls between two whole sequences keeps both.
    std::string whole(4094, 'x');
    whole += "\xD0\x96";
    whole += "tail";
    EXPECT_EQ(cg::cleanLogText(whole).size(), 4096u);
}

TEST_F(SessionLogTest, TheLineIsInTheFileRightAfterWrite) {
    const fs::path file = root_ / "a.log";
    cg::SessionLog log;
    ASSERT_TRUE(log.open(file.string()));
    log.write("S", "warning: ", "now");
    EXPECT_EQ(slurp(file), "S warning: now\n"); // a separate stream, the log is not closed
}

// Header "H1"/"H2" lines are 5 bytes each ("S Hn\n"); the cap is 60 bytes.
class SessionLogRollTest : public SessionLogTest {
protected:
    void SetUp() override {
        SessionLogTest::SetUp();
        file_ = root_ / "c.log";
        ASSERT_TRUE(log_.open(file_.string(), 60));
        log_.write("S", "", "H1");
        log_.write("S", "", "H2");
        log_.endHeader();
    }
    cg::SessionLog log_;
    fs::path file_;
};

TEST_F(SessionLogRollTest, RollKeepsTheHeaderAndAddsAMarker) {
    log_.write("S", "", "L1"); // 15 bytes
    log_.write("S", "", "L2"); // 20
    log_.write("S", "", "L3"); // 25
    log_.write("S", "", "L4"); // 30
    EXPECT_FALSE(fs::exists(root_ / "c.log.1"));
    for (const char* n : {"L5", "L6", "L7", "L8", "L9"}) // 35 40 45 50 55
        log_.write("S", "", n);
    EXPECT_FALSE(fs::exists(root_ / "c.log.1"));
    log_.write("T", "", "L10"); // 55 + 7 > 60: roll
    EXPECT_EQ(slurp(root_ / "c.log.1"), "S H1\nS H2\nS L1\nS L2\nS L3\nS L4\nS L5\nS L6\nS L7\nS L8\nS L9\n");
    EXPECT_EQ(slurp(file_), "S H1\nS H2\nT log continued from c.log.1\nT L10\n");
}

TEST_F(SessionLogRollTest, SecondRollReplacesTheRolledFileAndNeverMoreThanTwoFiles) {
    for (int i = 0; i < 40; ++i)
        log_.write("S", "", std::string("line ") + std::to_string(i % 10));
    int files = 0;
    for (const auto& e : fs::directory_iterator(root_))
        files += e.is_regular_file();
    EXPECT_EQ(files, 2);
    EXPECT_LE(fs::file_size(file_), 60u);
    EXPECT_LE(fs::file_size(root_ / "c.log.1"), 60u);
    const std::string current = slurp(file_), rolled = slurp(root_ / "c.log.1");
    EXPECT_EQ(current.substr(0, 10), "S H1\nS H2\n");
    EXPECT_EQ(rolled.substr(0, 10), "S H1\nS H2\n");
    EXPECT_NE(rolled.find("log continued from c.log.1"),
              std::string::npos);                             // the.1 of the 2nd roll is a continued file
    EXPECT_NE(current.find("S line 9\n"), std::string::npos); // the newest line is never lost
}

TEST_F(SessionLogRollTest, ALineLargerThanTheRestIsNotSplitAndDoesNotLoop) {
    log_.write("S", "", std::string(100, 'z')); // alone larger than the cap
    EXPECT_FALSE(fs::exists(root_ / "c.log.1")) << "a file that holds only its header does not roll";
    log_.write("S", "", std::string(100, 'y'));
    EXPECT_NE(slurp(file_).find(std::string(100, 'y') + "\n"), std::string::npos);
    EXPECT_NE(slurp(root_ / "c.log.1").find(std::string(100, 'z') + "\n"), std::string::npos);
}

TEST_F(SessionLogRollTest, FailedRenameStopsWritingWithoutThrowing) {
    fs::create_directories(root_ / "c.log.1" / "inner"); // a non-empty folder: rename cannot replace it
    for (int i = 0; i < 20; ++i)
        EXPECT_NO_THROW(log_.write("S", "", std::string("line ") + std::to_string(i)));
    EXPECT_FALSE(log_.isOpen());
    EXPECT_EQ(log_.path(), "");
    EXPECT_LE(slurp(file_).size(), 60u);
    EXPECT_EQ(slurp(file_).find("S line 19"), std::string::npos); // nothing was written after the failure
}

} // namespace
