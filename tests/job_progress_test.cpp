// SPDX-License-Identifier: GPL-3.0-or-later
// : the job's permille and the frozen bar of a row.
// Literals come from the spec and from tests/fixtures/cli-2026.10.13/run-continued.stderr line 12.
#include "job_progress.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

namespace {

using Seen = std::vector<std::pair<std::string, cg::DownloadProgress>>;

cg::DownloadProgress progress(const std::string& guid, std::int64_t processed, std::int64_t total) {
    cg::DownloadProgress p;
    p.guid = guid;
    p.processed = processed;
    p.total = total;
    return p;
}

struct PermilleRow {
    const char* name;
    Seen seen;
    std::vector<std::string> guids;
    int expected;
};

TEST(JobPermille, Table) {
    const std::vector<PermilleRow> rows = {
        {"one source half done", {{"A", progress("A", 5, 10)}}, {"A"}, 500},
        {"two sources, the second not seen: mean with 0", {{"A", progress("A", 10, 10)}}, {"A", "B"}, 500},
        {"total 0 counts 0", {{"A", progress("A", 4, 0)}, {"B", progress("B", 10, 10)}}, {"A", "B"}, 500},
        {"the fixture pair (run-continued.stderr line 12)",
         {{"A", progress("A", 243, 243)}, {"B", progress("B", 75, 243)}},
         {"A", "B"},
         654},
        {"empty overlay GUID is skipped", {{"A", progress("A", 5, 10)}}, {"A", ""}, 500},
        {"processed above total clamps to 1000", {{"A", progress("A", 30, 10)}}, {"A"}, 1000},
        {"clamped per source, not on the mean",
         {{"A", progress("A", 30, 10)}, {"B", progress("B", 0, 10)}},
         {"A", "B"},
         500},
        {"floor", {{"A", progress("A", 1, 3)}}, {"A"}, 333},
        {"no GUIDs", {{"A", progress("A", 5, 10)}}, {}, 0},
        {"only empty GUIDs", {{"A", progress("A", 5, 10)}}, {""}, 0},
        {"nothing seen", {}, {"A"}, 0},
    };
    for (const PermilleRow& r : rows)
        EXPECT_EQ(cg::jobPermille(r.seen, r.guids), r.expected) << r.name;
}

cg::ChartRecord record(cg::ChartState state, int done, std::int64_t tiles = 0, std::int64_t missing = 0) {
    cg::ChartRecord r;
    r.state = state;
    r.done = done;
    r.tiles = tiles;
    r.missing = missing;
    return r;
}

TEST(FrozenPermille, InterruptedAndFailedUseDoneCappedAt999) {
    for (cg::ChartState s : {cg::ChartState::Interrupted, cg::ChartState::Failed}) {
        EXPECT_EQ(cg::frozenPermille(record(s, 0)), std::nullopt);
        EXPECT_EQ(cg::frozenPermille(record(s, 1)), 1);
        EXPECT_EQ(cg::frozenPermille(record(s, 999)), 999);
        EXPECT_EQ(cg::frozenPermille(record(s, 1000)), 999);
        EXPECT_EQ(cg::frozenPermille(record(s, 1001)), std::nullopt); // never parsed so; the guard is 1..1000
    }
}

TEST(FrozenPermille, MissingUsesTilesAndMissing) {
    EXPECT_EQ(cg::frozenPermille(record(cg::ChartState::Missing, 0, 14200, 312)), 978);
    EXPECT_EQ(cg::frozenPermille(record(cg::ChartState::Missing, 0, 10, 10)), 0);
    EXPECT_EQ(cg::frozenPermille(record(cg::ChartState::Missing, 0, 0, 0)), std::nullopt);
    EXPECT_EQ(cg::frozenPermille(record(cg::ChartState::Missing, 0, 10, 11)), std::nullopt);
    EXPECT_EQ(cg::frozenPermille(record(cg::ChartState::Missing, 0, 10, -1)), std::nullopt);
}

TEST(FrozenPermille, CompleteAndRunningHaveNone) {
    EXPECT_EQ(cg::frozenPermille(record(cg::ChartState::Complete, 500, 10, 2)), std::nullopt);
    EXPECT_EQ(cg::frozenPermille(record(cg::ChartState::Running, 500, 10, 2)), std::nullopt);
}

} // namespace
