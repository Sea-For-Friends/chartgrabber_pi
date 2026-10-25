// SPDX-License-Identifier: GPL-3.0-or-later
// The plugin's own runner and parsers against a real
// CLI. Skipped unless CG_CLI and CG_MAPS are set. Environment: CG_CLI (program), CG_MAPS (--maps), CG_BBOX
// (west,south,east,north; default -1.5,50.5,-1.0,50.9), CG_ZOOMS (default 0-2), CG_RUN=1 (also download and
// export), CG_OQ6=1 with CG_RUN=1 (OQ-6: an estimate beside a running download, CG_OQ6_ZOOMS, default 13-14,
// at most 900 tiles, one thread). --cache is always a fresh temp directory, never the user's.
#include "cli_runner.h"
#include "job_files.h"
#include "wx_env.h"
#include "wx_loop.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdlib.h>
#include <string>
#include <variant>
#include <vector>

using namespace cg;
namespace fs = std::filesystem;

namespace {

std::optional<std::string> env(const char* name) {
    const char* v = std::getenv(name);
    if (v == nullptr || *v == '\0') {
        return std::nullopt;
    }
    return std::string(v);
}

std::optional<BBox> parseBBox(const std::string& text) {
    BBox b;
    char extra = 0;
    if (std::sscanf(text.c_str(), "%lf,%lf,%lf,%lf%c", &b.west, &b.south, &b.east, &b.north, &extra) != 4) {
        return std::nullopt;
    }
    return b;
}

// "a-b" or "a,b,c" (and mixes).
std::optional<std::vector<int>> parseZooms(const std::string& text) {
    std::vector<int> z;
    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t comma = std::min(text.find(',', pos), text.size());
        const std::string part = text.substr(pos, comma - pos);
        int a = 0, b = 0;
        char extra = 0;
        if (std::sscanf(part.c_str(), "%d-%d%c", &a, &b, &extra) == 2 && a <= b) {
            for (int i = a; i <= b; ++i) {
                z.push_back(i);
            }
        } else if (std::sscanf(part.c_str(), "%d%c", &a, &extra) == 1) {
            z.push_back(a);
        } else {
            return std::nullopt;
        }
        pos = comma + 1;
    }
    std::sort(z.begin(), z.end());
    z.erase(std::unique(z.begin(), z.end()), z.end());
    return z;
}

struct Result {
    std::vector<StdoutRecord> records;
    std::vector<StderrLine> lines;
    int exitCode = 12345;
    int exits = 0;
    bool startFailed = true;
    CliRunner::Callbacks callbacks() {
        CliRunner::Callbacks cb;
        cb.onRecord = [this](const StdoutRecord& r) { records.push_back(r); };
        cb.onStderr = [this](const StderrLine& l) { lines.push_back(l); };
        cb.onExit = [this](int code, bool failed) {
            exitCode = code;
            startFailed = failed;
            ++exits;
        };
        return cb;
    }
    template <class T> std::vector<T> all() const {
        std::vector<T> out;
        for (const StdoutRecord& r : records) {
            if (const auto* p = std::get_if<T>(&r)) {
                out.push_back(*p);
            }
        }
        return out;
    }
    std::vector<DownloadProgress> downloads() const {
        std::vector<DownloadProgress> out;
        for (const StderrLine& l : lines) {
            if (const auto* p = std::get_if<DownloadProgress>(&l)) {
                out.push_back(*p);
            }
        }
        return out;
    }
    bool hasStderr(const std::function<bool(const StderrLine&)>& pred) const {
        return std::any_of(lines.begin(), lines.end(), pred);
    }
    // The last `n` message lines (errors, warnings, other text), for the log.
    std::string tail(size_t n) const {
        std::vector<std::string> msgs;
        for (const StderrLine& l : lines) {
            if (const auto* m = std::get_if<MessageLine>(&l)) {
                msgs.push_back(m->text);
            }
        }
        std::string out;
        for (size_t i = msgs.size() > n ? msgs.size() - n : 0; i < msgs.size(); ++i) {
            out += "    | " + msgs[i] + "\n";
        }
        return out.empty() ? "    | (no message lines)\n" : out;
    }
};

bool mentionsSqlite(const Result& r) {
    for (const StderrLine& l : r.lines) {
        if (const auto* m = std::get_if<MessageLine>(&l)) {
            std::string t = m->text;
            std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return std::tolower(c); });
            if (t.find("sqlite") != std::string::npos || t.find("database is locked") != std::string::npos ||
                t.find("database table is locked") != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

// The detector OQ-6 relies on: the real CLI prints no message at all when all is well, so the positive case
// is proven here with made-up lines.
TEST(ContractHelpers, SqliteDetectorSeesLockedDatabaseAndIgnoresOtherMessages) {
    Result locked, fine;
    locked.lines.push_back(MessageLine{MessageLine::Kind::Error, "error: Database Is Locked"});
    fine.lines.push_back(MessageLine{MessageLine::Kind::Warning, "warning: tile 3 missing"});
    fine.lines.push_back(DownloadProgress{});
    EXPECT_TRUE(mentionsSqlite(locked));
    EXPECT_FALSE(mentionsSqlite(fine));
    Result sql;
    sql.lines.push_back(MessageLine{MessageLine::Kind::Error, "SQLite error 5"});
    EXPECT_TRUE(mentionsSqlite(sql));
}

class ContractTest : public LoopFixture {
protected:
    void SetUp() override {
        LoopFixture::SetUp();
        if (testing::Test::HasFatalFailure()) {
            return;
        }
        const auto cli = env("CG_CLI"), maps = env("CG_MAPS");
        if (!cli || (needsMaps() && !maps)) {
            GTEST_SKIP() << "CG_CLI" << (needsMaps() ? " and CG_MAPS are" : " is")
                         << " not set: the contract test is NOT run";
        }
        std::string tmpl = (fs::temp_directory_path() / "cgcontract.XXXXXX").string();
        ASSERT_NE(mkdtemp(tmpl.data()), nullptr);
        tmp_ = tmpl;
        settings_.cliPath = *cli;
        if (maps)
            settings_.cliMaps = {*maps};
        settings_.cacheDir = (tmp_ / "cache").string();
        fs::create_directories(settings_.cacheDir);
        const auto box = parseBBox(env("CG_BBOX").value_or("-1.5,50.5,-1.0,50.9"));
        ASSERT_TRUE(box) << "CG_BBOX is not west,south,east,north";
        bbox_ = *box;
        ASSERT_TRUE(probeCli(*cli)) << "probeCli(" << *cli << ") failed: too old or not a tile program";
    }
    void TearDown() override {
        if (!tmp_.empty()) {
            expectNoProcessesUnder(tmp_.string());
            std::error_code ec;
            fs::remove_all(tmp_, ec);
        }
        LoopFixture::TearDown();
    }

    virtual bool needsMaps() const { return true; }

    // Starts the CLI on `runner` (env + program + args; nice only for run / export, as the plugin does).
    bool start(CliRunner& runner, Result& out, const std::vector<std::string>& args, bool nice) {
        std::string error;
        const bool ok = runner.start(cliCommand(settings_.cliPath, args, nice), out.callbacks(), &error);
        EXPECT_TRUE(ok) << error;
        return ok;
    }
    // One CLI process to its end (timeout in ms).
    bool exec(Result& out, const std::vector<std::string>& args, bool nice, int timeoutMs) {
        CliRunner runner;
        if (!start(runner, out, args, nice)) {
            return false;
        }
        const bool done = runUntil([&] { return out.exits > 0; }, timeoutMs);
        EXPECT_TRUE(done) << "the CLI did not end in " << timeoutMs << " ms";
        return done;
    }
    // "The map": the first usable record of kind map.
    std::optional<SourceRecord> findMap() {
        Result sources;
        if (!exec(sources, sourcesArguments(settings_), false, 60000)) {
            return std::nullopt;
        }
        EXPECT_EQ(sources.exitCode, 0) << sources.tail(5);
        const auto records = sources.all<SourceRecord>();
        EXPECT_GE(records.size(), 1u) << "no source record";
        for (const SourceRecord& r : records) {
            if (r.usable() && r.kind == "map") {
                return r;
            }
        }
        ADD_FAILURE() << "no usable source of kind map in " << records.size() << " records";
        return std::nullopt;
    }
    JobSpec job(const SourceRecord& map, const std::vector<int>& zooms, const std::string& file) const {
        JobSpec j;
        j.sourceGuid = map.guid;
        j.areaKind = AreaKind::View;
        j.bbox = bbox_;
        j.zooms = zooms;
        j.outFile = (tmp_ / file).string();
        j.name = "Contract";
        j.threads = 1;
        return j;
    }
    std::string writeJob(const JobSpec& j) const {
        const fs::path p = tmp_ / "job.ini";
        std::FILE* f = std::fopen(p.c_str(), "w");
        EXPECT_NE(f, nullptr);
        if (f != nullptr) {
            const std::string text = jobIni(j);
            std::fwrite(text.data(), 1, text.size(), f);
            std::fclose(f);
        }
        return p.string();
    }

    fs::path tmp_;
    Settings settings_;
    BBox bbox_;
};

// the CLI reads exactly the --maps folders; the union of a bundled and a sailor's folder, the
// later folder wins a duplicate GUID, across the two source kinds with one `map source` warning; no --maps at
// all is exit 3. CG_MAPS is not needed: the folders are built from the repository's data/sources.
class MapsContractTest : public ContractTest {
protected:
    bool needsMaps() const override { return false; }
};

TEST_F(MapsContractTest, TheUnionOfTwoFoldersAndTheLaterOneWinsADuplicate) {
    const fs::path src = fs::path(CG_SOURCE_DIR) / "data" / "sources";
    const fs::path bundled = tmp_ / "bundled", sailor = tmp_ / "sailor d'x";
    fs::create_directories(bundled);
    fs::create_directories(sailor);
    fs::copy_file(src / "Coast and land" / "OpenTopoMap.source.json", bundled / "OpenTopoMap.source.json");
    fs::copy_file(src / "Satellite" / "Esri-World-Imagery.source.json",
                  bundled / "Esri-World-Imagery.source.json");
    // the sailor's folder holds the other kind: a frozen .zmp folder of the same GUID
    fs::copy(fs::path(CG_SOURCE_DIR) / "tests" / "fixtures" / "sources-zmp" / "OpenTopoMap.zmp",
             sailor / "Copy-Topo.zmp", fs::copy_options::recursive);
    settings_.cliMaps = {bundled.string(), sailor.string()};
    Result r;
    ASSERT_TRUE(exec(r, sourcesArguments(settings_), false, 60000));
    EXPECT_EQ(r.exitCode, 0) << r.tail(5);
    const std::string topo = "{51C1079B-B55D-4DF7-A410-ADC118F53AB1}";
    std::vector<std::string> files;
    for (const SourceRecord& s : r.all<SourceRecord>())
        files.push_back(s.guid == topo ? "topo:" + s.zmpFile : s.zmpFile);
    std::sort(files.begin(), files.end());
    EXPECT_EQ(files, (std::vector<std::string>{"Esri-World-Imagery.source.json", "topo:Copy-Topo.zmp"}));
    int replaced = 0;
    for (const StderrLine& l : r.lines)
        if (const auto* m = std::get_if<MessageLine>(&l);
            m && m->kind == MessageLine::Kind::Warning && m->text.rfind("map source " + topo, 0) == 0)
            ++replaced;
    EXPECT_EQ(replaced, 1) << r.tail(6);
}

TEST_F(MapsContractTest, WithoutAnyMapsFolderTheCliRefusesWithExit3) {
    settings_.cliMaps.clear();
    Result r;
    ASSERT_TRUE(exec(r, sourcesArguments(settings_), false, 60000));
    EXPECT_EQ(r.exitCode, 3) << r.tail(5);
    EXPECT_EQ(r.all<SourceRecord>().size(), 0u);
    int refused = 0;
    for (const StderrLine& l : r.lines)
        if (const auto* m = std::get_if<MessageLine>(&l);
            m && m->kind == MessageLine::Kind::Error &&
            m->text == "no map sources given: pass --maps DIR, a folder of map-source definitions (repeat "
                       "--maps for more folders)")
            ++refused;
    EXPECT_EQ(refused, 1) << r.tail(6);
}

TEST_F(ContractTest, SourcesEstimateAndWhenAskedRunExport) {
    const auto zooms = parseZooms(env("CG_ZOOMS").value_or("0-2"));
    ASSERT_TRUE(zooms) << "CG_ZOOMS is not a zoom list";
    const auto map = findMap();
    ASSERT_TRUE(map);

    // estimate: one record per zoom, bytesOnDisk derived; the phase line only with --progress=lines.
    const JobSpec spec = job(*map, *zooms, "contract.mbtiles");
    Result est;
    ASSERT_TRUE(exec(est, estimateArguments(spec, settings_), false, 60000));
    EXPECT_EQ(est.exitCode, 0) << est.tail(5);
    const auto estimates = est.all<EstimateRecord>();
    ASSERT_EQ(estimates.size(), zooms->size());
    for (size_t i = 0; i < estimates.size(); ++i) {
        EXPECT_EQ(estimates[i].guid, map->guid);
        EXPECT_EQ(estimates[i].zoom, (*zooms)[i]);
        // The CLI reports 0 tiles for a zoom whose bbox cell the map does not serve: no bytes then.
        EXPECT_EQ(estimates[i].bytesOnDisk > 0, estimates[i].tiles > 0) << "zoom " << estimates[i].zoom;
    }
    EXPECT_GT(sumEstimates(estimates).bytesOnDisk, 0) << "no zoom has a tile";
    const bool phaseEstimate = est.hasStderr([](const StderrLine& l) {
        const auto* p = std::get_if<PhaseRecord>(&l);
        return p != nullptr && p->name == "estimate";
    });
    EXPECT_TRUE(phaseEstimate);

    if (env("CG_RUN").value_or("") != "1") {
        return;
    }
    const std::string outFile = spec.outFile;
    const std::string ini = writeJob(spec);
    Result run;
    ASSERT_TRUE(exec(run, runArguments(ini, RunKind::Download, false, settings_), true, 600000));
    EXPECT_EQ(run.exitCode, 0) << run.tail(10);
    const auto written = run.all<WrittenRecord>();
    ASSERT_GE(written.size(), 1u) << run.tail(10);
    EXPECT_EQ(written[0].guid, map->guid);
    EXPECT_TRUE(fs::exists(written[0].path)) << written[0].path;
    const auto downloads = run.downloads();
    ASSERT_GE(downloads.size(), 1u) << "no progress download";
    EXPECT_EQ(downloads[0].guid, map->guid);
    const bool exportProgress =
        run.hasStderr([](const StderrLine& l) { return std::holds_alternative<ExportProgress>(l); });
    EXPECT_TRUE(exportProgress);

    Result exp;
    ASSERT_TRUE(exec(exp, runArguments(ini, RunKind::BuildFromSaved, true, settings_), true, 300000));
    EXPECT_EQ(exp.exitCode, 0) << exp.tail(10);
    const auto written2 = exp.all<WrittenRecord>();
    ASSERT_GE(written2.size(), 1u) << exp.tail(10);
    EXPECT_EQ(written2[0].guid, map->guid);
    EXPECT_TRUE(fs::exists(written2[0].path)) << written2[0].path;
}

// the stop line through CliRunner against the pinned CLI. An estimate
// stopped at once ends 143 with the CLI's own last line; with CG_RUN=1 a run stopped after its first progress
// record ends 143 within the grace and the same run again ends 0. A kill would show as exit -9 and as a run
// slower than the grace.
TEST_F(ContractTest, StopThroughStdinGives143AndTheSessionContinues) {
    const std::string line = "chart-grabber-cli: stop requested on stdin; exit 143";
    const auto stopLine = [&](const Result& r) {
        return r.hasStderr([&](const StderrLine& l) {
            const auto* m = std::get_if<MessageLine>(&l);
            return m != nullptr && m->kind == MessageLine::Kind::Other && m->text == line;
        });
    };
    const auto map = findMap();
    ASSERT_TRUE(map);
    const JobSpec spec = job(*map, {0, 1, 2}, "stop.mbtiles");
    {
        Result est;
        CliRunner runner;
        ASSERT_TRUE(start(runner, est, estimateArguments(spec, settings_), false));
        runner.terminate();
        ASSERT_TRUE(runUntil([&] { return est.exits > 0; }, runner.stopGraceMs() + 5000));
        EXPECT_EQ(est.exitCode, 143) << est.tail(5);
        EXPECT_TRUE(stopLine(est)) << est.tail(5);
    }
    if (env("CG_RUN").value_or("") != "1") {
        return;
    }
    const auto zooms = parseZooms(env("CG_STOP_ZOOMS").value_or("9-12"));
    ASSERT_TRUE(zooms);
    const JobSpec big = job(*map, *zooms, "stop-run.mbtiles");
    const std::string ini = writeJob(big);
    {
        Result run;
        CliRunner runner;
        ASSERT_TRUE(start(runner, run, runArguments(ini, RunKind::Download, false, settings_), true));
        ASSERT_TRUE(runUntil([&] { return !run.downloads().empty() || run.exits > 0; }, 120000));
        ASSERT_EQ(run.exits, 0) << "the run ended before it could be stopped: " << run.tail(5);
        const auto t0 = Clock::now();
        runner.terminate();
        ASSERT_TRUE(runUntil([&] { return run.exits > 0; }, runner.stopGraceMs() + 5000));
        EXPECT_LT(Clock::now() - t0, std::chrono::milliseconds(runner.stopGraceMs()))
            << "killed, not stopped";
        EXPECT_EQ(run.exitCode, 143) << run.tail(5);
        EXPECT_TRUE(stopLine(run)) << run.tail(5);
    }
    Result again;
    ASSERT_TRUE(exec(again, runArguments(ini, RunKind::Download, false, settings_), true, 600000));
    EXPECT_EQ(again.exitCode, 0) << again.tail(10);
}

// OQ-6: an estimate of the same source with the same --cache while a run downloads: both end with
// exit 0 and no SQLite error.
TEST_F(ContractTest, EstimateBesideRunningDownloadSharesTheCache) {
    if (env("CG_RUN").value_or("") != "1" || env("CG_OQ6").value_or("") != "1") {
        GTEST_SKIP() << "CG_RUN=1 and CG_OQ6=1 are not both set: OQ-6 is NOT run";
    }
    const auto zooms = parseZooms(env("CG_OQ6_ZOOMS").value_or("13-14"));
    ASSERT_TRUE(zooms);
    const auto map = findMap();
    ASSERT_TRUE(map);
    const JobSpec spec = job(*map, *zooms, "oq6.mbtiles");

    Result pre;
    ASSERT_TRUE(exec(pre, estimateArguments(spec, settings_), false, 60000));
    const EstimateTotals totals = sumEstimates(pre.all<EstimateRecord>());
    ASSERT_GT(totals.tiles, 0);
    ASSERT_LE(totals.tiles, 900) << "polite limit: at most 900 tiles";
    std::printf("[ oq6 ] tiles to download: %lld\n", static_cast<long long>(totals.tiles));

    const std::string ini = writeJob(spec);
    Result run;
    CliRunner runner;
    const auto t0 = Clock::now();
    ASSERT_TRUE(start(runner, run, runArguments(ini, RunKind::Download, false, settings_), true));
    // The download is under way (the first progress line) and has had time to open and write the cache.
    ASSERT_TRUE(runUntil([&] { return !run.downloads().empty() || run.exits > 0; }, 120000));
    ASSERT_EQ(run.exits, 0) << "the run ended before the estimate could start: " << run.tail(5);
    runFor(3000);
    ASSERT_EQ(run.exits, 0) << "the run ended before the estimate could start: " << run.tail(5);
    // Estimates back to back (each a second process on the same --cache) for as long as the download runs.
    int overlapped = 0, estimates = 0, badExit = 0;
    bool sqlite = false;
    Result est;
    while (run.exits == 0 && estimates < 200) {
        Result one;
        CliRunner second;
        ASSERT_TRUE(start(second, one, estimateArguments(spec, settings_), false));
        ASSERT_TRUE(runUntil([&] { return one.exits > 0; }, 120000));
        ++estimates;
        overlapped += run.exits == 0 ? 1 : 0;
        badExit += one.exitCode != 0 ? 1 : 0;
        sqlite = sqlite || mentionsSqlite(one);
        EXPECT_EQ(one.all<EstimateRecord>().size(), zooms->size());
        est = std::move(one);
        runFor(500);
    }
    const auto estimateAt = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - t0).count();
    ASSERT_TRUE(runUntil([&] { return run.exits > 0; }, 900000)) << "the run did not end";
    const auto runSeconds = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - t0).count();

    std::printf(
        "[ oq6 ] %d estimates (%d ended while the run was running, %d with a bad exit), the last at %llds; "
        "the run ended after %llds\n",
        estimates, overlapped, badExit, static_cast<long long>(estimateAt),
        static_cast<long long>(runSeconds));
    std::printf("[ oq6 ] last estimate exit %d, stderr tail:\n%s", est.exitCode, est.tail(5).c_str());
    std::printf("[ oq6 ] run exit %d, stderr tail:\n%s", run.exitCode, run.tail(5).c_str());
    EXPECT_GE(overlapped, 1) << "no estimate ended during the download: use more zooms";
    EXPECT_EQ(badExit, 0);
    EXPECT_FALSE(sqlite);
    EXPECT_EQ(run.exitCode, 0);
    EXPECT_FALSE(mentionsSqlite(run)) << run.tail(10);
}

} // namespace
