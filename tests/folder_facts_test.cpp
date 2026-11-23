// SPDX-License-Identifier: GPL-3.0-or-later
// The folder-facts cache of the New chart page (one measurement per folder per 5 s, at most 4 folders) and
// the once-per-session sandbox probe. A fake clock and a counting measure stand in for the file system.
#include "chart_folder.h"
#include "folder_facts.h"

#include <gtest/gtest.h>

#include <chrono>
#include <map>
#include <string>

namespace {

using std::chrono::milliseconds;
using std::chrono::seconds;

struct Fixture {
    std::chrono::steady_clock::time_point now{seconds(1000)};
    std::map<std::string, int> calls;
    std::uint64_t nextFree = 100; // each measurement returns a new value, so a reuse is visible
    cg::FolderFactsCache cache{[this](const std::string& folder) {
                                   ++calls[folder];
                                   cg::FolderFacts f;
                                   f.exists = true;
                                   f.writable = true;
                                   f.ancestor = folder;
                                   f.freeBytes = nextFree++;
                                   return f;
                               },
                               [this] { return now; }};
};

TEST(FolderFactsCache, TypingForFiveSecondsMeasuresEachFolderOnce_P0299) {
    Fixture f;
    for (int key = 0; key < 40; ++key) { // forty keystrokes, 100 ms apart, asking both folders
        f.now += milliseconds(100);
        f.cache.get("/charts");
        f.cache.get("/cache");
    }
    EXPECT_EQ(f.calls["/charts"], 1);
    EXPECT_EQ(f.calls["/cache"], 1);
}

TEST(FolderFactsCache, ReusedAnswerIsTheMeasuredOne_P0299) {
    Fixture f;
    EXPECT_EQ(f.cache.get("/charts").freeBytes, std::optional<std::uint64_t>(100));
    f.now += milliseconds(4999);
    EXPECT_EQ(f.cache.get("/charts").freeBytes, std::optional<std::uint64_t>(100));
}

TEST(FolderFactsCache, FiveSecondsAfterTheMeasurementTheNextGetMeasuresAgain_P0299) {
    Fixture f;
    f.cache.get("/charts");
    f.now += milliseconds(4999);
    f.cache.get("/charts");
    EXPECT_EQ(f.calls["/charts"], 1) << "4.999 s: still the cached answer";
    f.now += milliseconds(1); // exactly 5 s after the measurement
    EXPECT_EQ(f.cache.get("/charts").freeBytes, std::optional<std::uint64_t>(101));
    EXPECT_EQ(f.calls["/charts"], 2);
    f.now += milliseconds(4999); // the age counts from the new measurement
    f.cache.get("/charts");
    EXPECT_EQ(f.calls["/charts"], 2);
}

TEST(FolderFactsCache, AClockThatWentBackMeasuresAgain_P0299) {
    Fixture f;
    f.cache.get("/charts");
    f.now -= seconds(1);
    f.cache.get("/charts");
    EXPECT_EQ(f.calls["/charts"], 2);
}

TEST(FolderFactsCache, DropForgetsOnlyThatFolderAndClearForgetsAll_P0299) {
    Fixture f;
    f.cache.get("/charts");
    f.cache.get("/cache");
    f.cache.drop("/charts");
    f.cache.get("/charts");
    f.cache.get("/cache");
    EXPECT_EQ(f.calls["/charts"], 2);
    EXPECT_EQ(f.calls["/cache"], 1);
    f.cache.clear();
    f.cache.get("/charts");
    f.cache.get("/cache");
    EXPECT_EQ(f.calls["/charts"], 3);
    EXPECT_EQ(f.calls["/cache"], 2);
}

TEST(FolderFactsCache, FoldersAreKeyedByTheirPathAndKeepTheirOwnFacts_P0299) {
    Fixture f;
    const cg::FolderFacts a = f.cache.get("/mnt/boat charts/d'x");
    const cg::FolderFacts b = f.cache.get("/mnt/\xD0\x90\xD0\xBB\xD1\x84\xD0\xB0");
    EXPECT_EQ(a.ancestor, "/mnt/boat charts/d'x");
    EXPECT_EQ(b.ancestor, "/mnt/\xD0\x90\xD0\xBB\xD1\x84\xD0\xB0");
    EXPECT_EQ(f.cache.get("/mnt/boat charts/d'x").freeBytes, a.freeBytes);
}

TEST(FolderFactsCache, FiveFoldersKeepFourAndTheOldestGoes_P0299) {
    Fixture f;
    for (const char* folder : {"/a", "/b", "/c", "/d", "/e"})
        f.cache.get(folder); // /a is the oldest and goes when /e comes
    f.cache.get("/b");
    f.cache.get("/c");
    f.cache.get("/d");
    f.cache.get("/e");
    EXPECT_EQ(f.calls["/b"], 1);
    EXPECT_EQ(f.calls["/c"], 1);
    EXPECT_EQ(f.calls["/d"], 1);
    EXPECT_EQ(f.calls["/e"], 1);
    f.cache.get("/a");
    EXPECT_EQ(f.calls["/a"], 2) << "the oldest entry was dropped";
}

TEST(FolderFactsCache, TheFactsOfAMissingFolderCarryItsAncestor_P0299) {
    cg::FolderFactsCache cache([](const std::string&) {
        cg::FolderFacts f;
        f.ancestor = "/mnt";
        f.freeBytes = 7;
        return f;
    });
    const cg::FolderFacts f = cache.get("/mnt/none/deeper");
    EXPECT_FALSE(f.exists);
    EXPECT_FALSE(f.writable);
    EXPECT_EQ(f.ancestor, "/mnt");
    EXPECT_EQ(f.freeBytes, std::optional<std::uint64_t>(7));
}

TEST(FolderFactsCache, TheDefaultClockIsTheRealOne_P0299) {
    int calls = 0;
    cg::FolderFactsCache cache([&](const std::string&) {
        ++calls;
        return cg::FolderFacts{};
    });
    cache.get("/x");
    cache.get("/x");
    EXPECT_EQ(calls, 1) << "two gets a few microseconds apart share one measurement";
}

TEST(SandboxProbe, AMemoizedProbeReadsOnceAndKeepsTheFirstResult_P0299) {
    int reads = 0;
    auto probe = cg::memoizedProbe([&] {
        ++reads;
        return cg::Sandbox{reads == 1, "/srv/skipper", {"/mnt/boat"}};
    });
    EXPECT_EQ(reads, 0) << "nothing is read before the first use";
    const cg::Sandbox first = probe();
    const cg::Sandbox second = probe();
    const cg::Sandbox third = probe();
    EXPECT_EQ(reads, 1);
    EXPECT_TRUE(first.flatpak);
    EXPECT_TRUE(second.flatpak) << "the second call does not see a changed answer";
    EXPECT_TRUE(third.flatpak);
    EXPECT_EQ(second.granted, (std::vector<std::string>{"/mnt/boat"}));
    EXPECT_EQ(second.home, "/srv/skipper");
}

} // namespace
