// SPDX-License-Identifier: GPL-3.0-or-later
#include "job_files.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <clocale>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace cg {
namespace {

using Args = std::vector<std::string>;

JobSpec viewBase() {
    JobSpec j;
    j.sourceGuid = "AAAA-1";
    j.areaKind = AreaKind::View;
    j.bbox = {-1.5, 50.25, 2.125, 51.0};
    j.zooms = {8, 9, 10, 12};
    j.outFile = "/c/Solent - Sat.mbtiles";
    j.name = "Solent - Sat";
    j.threads = 2;
    return j;
}

JobSpec rectSeamarks() {
    JobSpec j = viewBase();
    j.areaKind = AreaKind::Rectangle;
    j.overlayGuid = "BBBB-2";
    j.overlayOutFile = "/c/Solent - Sat - Seamarks.mbtiles";
    return j;
}

JobSpec routeOverlayOnly() {
    JobSpec j;
    j.overlayGuid = "BBBB-2";
    j.areaKind = AreaKind::Route;
    j.routeGpxPath = "/j/route.gpx";
    j.corridorNm = 2;
    j.zooms = {15, 16};
    j.overlayOutFile = "/c/Route - Seamarks.mbtiles";
    j.name = "ignored";
    j.threads = 1;
    return j;
}

TEST(JobIni, ViewBase) {
    EXPECT_EQ(jobIni(viewBase()), "[Job]\n"
                                  "Source=AAAA-1\n"
                                  "BBox=-1.50000000,50.25000000,2.12500000,51.00000000\n"
                                  "Zooms=8-10,12\n"
                                  "Out=/c/Solent - Sat.mbtiles\n"
                                  "Name=Solent - Sat\n"
                                  "[Download]\n"
                                  "Threads=2\n");
}

TEST(JobIni, RectangleBaseSeamarks) {
    EXPECT_EQ(jobIni(rectSeamarks()), "[Job]\n"
                                      "Source=AAAA-1\n"
                                      "Overlay=BBBB-2\n"
                                      "BBox=-1.50000000,50.25000000,2.12500000,51.00000000\n"
                                      "Zooms=8-10,12\n"
                                      "Out=/c/Solent - Sat.mbtiles\n"
                                      "OverlayOut=/c/Solent - Sat - Seamarks.mbtiles\n"
                                      "Name=Solent - Sat\n"
                                      "[Download]\n"
                                      "Threads=2\n");
}

TEST(JobIni, RouteOverlayOnly) {
    EXPECT_EQ(jobIni(routeOverlayOnly()), "[Job]\n"
                                          "Overlay=BBBB-2\n"
                                          "Route=/j/route.gpx\n"
                                          "CorridorNm=2.0\n"
                                          "Zooms=15-16\n"
                                          "Out=/c/Route - Seamarks.mbtiles\n"
                                          "[Download]\n"
                                          "Threads=1\n");
}

TEST(JobIni, NewlineInNameDoesNotInjectKeys) {
    JobSpec j = viewBase();
    j.name = "A\nOut=/etc/x\r\nB";
    j.outFile = "/c/a\nb.mbtiles";
    const std::string ini = jobIni(j);
    EXPECT_NE(ini.find("Name=A Out=/etc/x  B\n"), std::string::npos);
    EXPECT_NE(ini.find("Out=/c/a b.mbtiles\n"), std::string::npos);
    EXPECT_EQ(ini.find('\r'), std::string::npos);
    EXPECT_EQ(std::count(ini.begin(), ini.end(), '\n'), 8);
}

Settings fullSettings() {
    Settings s;
    s.cliMaps = {"/b d'x", "/m"};
    s.cacheDir = "/ca";
    s.proxy = "http://p:3128";
    return s;
}

TEST(Arguments, Sources) {
    // sources always takes --format=tsv and never --progress
    EXPECT_EQ(sourcesArguments(Settings{}), (Args{"sources", "--format=tsv"}));
    EXPECT_EQ(sourcesArguments(fullSettings()),
              (Args{"sources", "--format=tsv", "--maps", "/b d'x", "--maps", "/m", "--cache", "/ca"}));
}

// every command carries one `--maps D` pair per cliMaps entry, in order (the sailor's folder
// last), before --cache; --user-maps never; none set = no --maps.
TEST(Arguments, MapsPairsOnEveryCommand) {
    Settings s = fullSettings();
    s.cliMaps = {"/b d'x", "/u \"y\" \xC3\xA9"};
    const Args all[] = {
        sourcesArguments(s),
        estimateArguments(viewBase(), s),
        runArguments("/j/job.ini", RunKind::Download, false, s, ""),
        runArguments("/j/job.ini", RunKind::BuildFromSaved, false, s, ""),
    };
    const char* const first[] = {"sources", "estimate", "run", "export"};
    for (std::size_t n = 0; n < 4; ++n) {
        const Args& a = all[n];
        SCOPED_TRACE(first[n]);
        EXPECT_EQ(a[0], first[n]);
        EXPECT_EQ(std::count(a.begin(), a.end(), "--user-maps"), 0);
        const auto m = std::find(a.begin(), a.end(), "--maps");
        ASSERT_LT(m + 3, a.end());
        EXPECT_EQ((Args{m, m + 4}), (Args{"--maps", "/b d'x", "--maps", "/u \"y\" \xC3\xA9"}));
        EXPECT_EQ(m + 4,
                  std::find(a.begin(), a.end(), "--cache")); // nothing after the sailor's folder but --cache
        EXPECT_EQ(std::count(a.begin(), a.end(), "--maps"), 2);
    }
    s.cliMaps.clear();
    EXPECT_EQ(sourcesArguments(s), (Args{"sources", "--format=tsv", "--cache", "/ca"}));
}

TEST(Arguments, Estimate) {
    const std::string bbox = "-1.50000000,50.25000000,2.12500000,51.00000000";
    EXPECT_EQ(estimateArguments(rectSeamarks(), Settings{}),
              (Args{"estimate", "--format=tsv", "--progress=lines", "--source", "AAAA-1", "--overlay",
                    "BBBB-2", "--bbox", bbox, "--zooms", "8-10,12"}));
    EXPECT_EQ(estimateArguments(viewBase(), fullSettings()),
              (Args{"estimate", "--format=tsv", "--progress=lines", "--maps", "/b d'x", "--maps", "/m",
                    "--cache", "/ca", "--source", "AAAA-1", "--bbox", bbox, "--zooms", "8-10,12"}));
}

TEST(Arguments, EstimateRouteOverlayOnly) {
    JobSpec j = routeOverlayOnly();
    j.corridorNm = 2.5;
    EXPECT_EQ(estimateArguments(j, Settings{}),
              (Args{"estimate", "--format=tsv", "--progress=lines", "--overlay", "BBBB-2", "--route",
                    "/j/route.gpx", "--corridor-nm", "2.5", "--zooms", "15-16"}));
}

TEST(Arguments, Run) {
    EXPECT_EQ(runArguments("/j/job.ini", RunKind::Download, false, Settings{}),
              (Args{"run", "--job", "/j/job.ini", "--format=tsv", "--progress=lines"}));
}

TEST(Arguments, RunBuildFromSavedOverwriteAll) {
    Settings s = fullSettings();
    s.slowConnection = true;
    EXPECT_EQ(runArguments("/j/job.ini", RunKind::BuildFromSaved, true, s),
              (Args{"export", "--job", "/j/job.ini", "--format=tsv", "--progress=lines", "--maps", "/b d'x",
                    "--maps", "/m", "--cache", "/ca", "--proxy", "http://p:3128", "--threads", "1",
                    "--overwrite"}));
}

TEST(Arguments, RunSlowWithoutOverwrite) {
    Settings s;
    s.slowConnection = true;
    EXPECT_EQ(runArguments("/j/job.ini", RunKind::Download, false, s),
              (Args{"run", "--job", "/j/job.ini", "--format=tsv", "--progress=lines", "--threads", "1"}));
}

TEST(Arguments, RunUpdateAddsReplaceAndTheDate) {
    Settings s = fullSettings();
    s.slowConnection = true;
    EXPECT_EQ(runArguments("/j/job.ini", RunKind::Update, true, s, "2026-09-16T10:00:00"),
              (Args{"run", "--job", "/j/job.ini", "--format=tsv", "--progress=lines", "--maps", "/b d'x",
                    "--maps", "/m", "--cache", "/ca", "--proxy", "http://p:3128", "--threads", "1",
                    "--overwrite", "--replace", "--replace-older-than=2026-09-16T10:00:00"}));
    // the date is ignored by the other kinds
}

TEST(OlderThanDate, MonthsBackClampTheDayAndCrossTheYear) {
    EXPECT_EQ(olderThanDate(2026, 2, 5, 7, 8, 9, 0), "2026-02-05T07:08:09");
    EXPECT_EQ(olderThanDate(2026, 3, 31, 23, 59, 58, 1), "2026-02-28T23:59:58");
    EXPECT_EQ(olderThanDate(2028, 3, 31, 0, 0, 0, 1), "2028-02-29T00:00:00");
    EXPECT_EQ(olderThanDate(2026, 1, 15, 12, 0, 0, 1), "2025-12-15T12:00:00");
    EXPECT_EQ(olderThanDate(2026, 10, 16, 1, 2, 3, 12), "2025-10-16T01:02:03");
    EXPECT_EQ(olderThanDate(2026, 3, 31, 1, 2, 3, 6), "2025-09-30T01:02:03");
    EXPECT_EQ(olderThanDate(2024, 2, 29, 1, 2, 3, 12), "2023-02-28T01:02:03");
    EXPECT_EQ(olderThanDate(2100, 3, 31, 1, 2, 3, 1), "2100-02-28T01:02:03");
}

TEST(Arguments, Niced) {
    EXPECT_EQ(niced("/usr/bin/cli", {"run", "--job", "x"}),
              (Args{"nice", "-n", "10", "/usr/bin/cli", "run", "--job", "x"}));
    EXPECT_EQ(niced("cli", {}), (Args{"nice", "-n", "10", "cli"}));
}

TEST(Arguments, EnvThenOptionalNiceCommand) {
    EXPECT_EQ(cliCommand("/a b/cli", {"run", "it's"}, false),
              (Args{"env", "LC_ALL=C.UTF-8", "/a b/cli", "run", "it's"}));
    EXPECT_EQ(cliCommand("/a b/cli", {"run", "it's"}, true),
              (Args{"env", "LC_ALL=C.UTF-8", "nice", "-n", "10", "/a b/cli", "run", "it's"}));
    EXPECT_EQ(cliCommand("cli", {}, false), (Args{"env", "LC_ALL=C.UTF-8", "cli"}));
}

TEST(Locale, NumbersUseDotUnderGermanLocale) {
    const std::string old = std::setlocale(LC_ALL, nullptr);
    if (std::setlocale(LC_ALL, "de_DE.UTF-8") == nullptr) {
        std::setlocale(LC_ALL, old.c_str());
        GTEST_SKIP() << "locale de_DE.UTF-8 is not installed: the locale assertion was NOT run";
    }
    const std::string ini = jobIni(rectSeamarks());
    const Args est = estimateArguments(routeOverlayOnly(), Settings{});
    std::setlocale(LC_ALL, old.c_str());
    EXPECT_NE(ini.find("BBox=-1.50000000,50.25000000,2.12500000,51.00000000\n"), std::string::npos);
    EXPECT_EQ(ini.find("1,5"), std::string::npos);
    EXPECT_EQ(est[7], "2.0");
}

TEST(DisplayCommand, QuotesSpaceAndApostrophe) {
    EXPECT_EQ(displayCommand({"nice", "-n", "10", "/a b/cli", "it's", "--x=1", ""}),
              "nice -n 10 '/a b/cli' 'it'\\''s' --x=1 ''");
}

TEST(Names, DefaultSeamarksAndFile) {
    EXPECT_EQ(defaultChartName("Solent", "Google Sat"), "Solent - Google Sat");
    EXPECT_EQ(seamarksName("Solent - Sat"), "Solent - Sat - Seamarks");
    EXPECT_EQ(chartFileName("A/B"), "A_B.mbtiles");
    EXPECT_EQ(chartFileName(std::string("a\tb\nc\x01") + "d"), "a_b_c_d.mbtiles");
    EXPECT_EQ(chartFileName("Skipper's"), "Skippers.mbtiles");
    EXPECT_EQ(chartFileName("Plain"), "Plain.mbtiles");
}

TEST(Names, AreaPlace) {
    EXPECT_EQ(areaPlace({-1.5, 50.0, -1.1, 51.5}), "Area 50.75N 1.30W");
    EXPECT_EQ(areaPlace({10.0, -34.0, 11.0, -33.0}), "Area 33.50S 10.50E");
    EXPECT_EQ(areaPlace({-1.0, -1.0, 1.0, 1.0}), "Area 0.00N 0.00E");
}

// Minimal XML reader: checks nesting and returns start tags with their attributes.
struct Tag {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attrs;
};

bool readXml(const std::string& x, std::vector<Tag>& tags, std::string& text) {
    std::vector<std::string> stack;
    std::size_t i = 0;
    if (x.compare(0, 5, "<?xml") == 0) {
        i = x.find("?>");
        if (i == std::string::npos) {
            return false;
        }
        i += 2;
    }
    while (i < x.size()) {
        if (x[i] != '<') {
            if (x[i] == '>' || x[i] == '"' || x[i] == '\'') {
                // raw '>' is legal in text but our escaping never leaves one
                return false;
            }
            if (x[i] == '&') {
                const auto semi = x.find(';', i);
                if (semi == std::string::npos) {
                    return false;
                }
                const std::string ent = x.substr(i, semi - i + 1);
                const char* known[] = {"&amp;", "&lt;", "&gt;", "&quot;", "&apos;"};
                const char dec[] = {'&', '<', '>', '"', '\''};
                bool ok = false;
                for (int k = 0; k < 5; ++k) {
                    if (ent == known[k]) {
                        text += dec[k];
                        ok = true;
                    }
                }
                if (!ok) {
                    return false;
                }
                i = semi + 1;
                continue;
            }
            if (!stack.empty() && stack.back() == "name") {
                text += x[i];
            }
            ++i;
            continue;
        }
        const auto end = x.find('>', i);
        if (end == std::string::npos) {
            return false;
        }
        std::string body = x.substr(i + 1, end - i - 1);
        i = end + 1;
        if (body[0] == '/') {
            if (stack.empty() || stack.back() != body.substr(1)) {
                return false;
            }
            stack.pop_back();
            continue;
        }
        const bool selfClose = body.back() == '/';
        if (selfClose) {
            body.pop_back();
        }
        Tag t;
        std::size_t p = body.find(' ');
        t.name = body.substr(0, p);
        while (p != std::string::npos && p < body.size()) {
            while (p < body.size() && body[p] == ' ') {
                ++p;
            }
            if (p >= body.size()) {
                break;
            }
            const auto eq = body.find("=\"", p);
            if (eq == std::string::npos) {
                return false;
            }
            const auto q = body.find('"', eq + 2);
            if (q == std::string::npos) {
                return false;
            }
            t.attrs.emplace_back(body.substr(p, eq - p), body.substr(eq + 2, q - eq - 2));
            p = q + 1;
        }
        tags.push_back(t);
        if (!selfClose) {
            stack.push_back(t.name);
        }
    }
    return stack.empty();
}

TEST(RouteGpx, EscapesAndEightDecimals) {
    const std::string name = "A&B <c> \"d\" 'e'";
    const std::string gpx = routeGpx(name, {{50.5, -1.25}, {-33.123456789, 151.0}});
    EXPECT_NE(gpx.find("<name>A&amp;B &lt;c&gt; &quot;d&quot; &apos;e&apos;</name>"), std::string::npos);
    EXPECT_NE(gpx.find("<rtept lat=\"50.50000000\" lon=\"-1.25000000\"/>"), std::string::npos);
    EXPECT_NE(gpx.find("<rtept lat=\"-33.12345679\" lon=\"151.00000000\"/>"), std::string::npos);
    EXPECT_LT(gpx.find("-1.25000000"), gpx.find("151.00000000")); // order kept

    std::vector<Tag> tags;
    std::string text;
    ASSERT_TRUE(readXml(gpx, tags, text));
    EXPECT_EQ(text, name);
    ASSERT_EQ(tags.size(), 5U); // gpx rte name rtept rtept
    EXPECT_EQ(tags[0].name, "gpx");
    EXPECT_EQ(tags[0].attrs[0], (std::pair<std::string, std::string>{"version", "1.1"}));
    EXPECT_EQ(tags[0].attrs[2],
              (std::pair<std::string, std::string>{"xmlns", "http://www.topografix.com/GPX/1/1"}));
    EXPECT_EQ(tags[3].name, "rtept");
    EXPECT_EQ(tags[3].attrs[0].first, "lat");
    EXPECT_EQ(tags[3].attrs[1].first, "lon");
}

class PathTest : public testing::Test {
protected:
    void SetUp() override {
        root = std::filesystem::temp_directory_path() / ("cg_path_test_" + std::to_string(::getpid()));
        std::filesystem::create_directories(root / "a");
        std::filesystem::create_directories(root / "b");
    }
    void TearDown() override { std::filesystem::remove_all(root); }
    void make(const std::filesystem::path& p, mode_t mode) {
        std::ofstream(p) << "x";
        ::chmod(p.c_str(), mode);
    }
    std::filesystem::path root;
};

TEST_F(PathTest, FoundInSecondDirectory) {
    make(root / "b" / "tool", 0755);
    const std::string env = (root / "a").string() + ":" + (root / "b").string();
    EXPECT_EQ(findOnPath("tool", env), (root / "b" / "tool").string());
}

TEST_F(PathTest, NonExecutableSkipped) {
    make(root / "a" / "tool", 0644);
    make(root / "b" / "tool", 0755);
    const std::string env = (root / "a").string() + ":" + (root / "b").string();
    EXPECT_EQ(findOnPath("tool", env), (root / "b" / "tool").string());
    EXPECT_EQ(findOnPath("tool", (root / "a").string()), std::nullopt);
}

TEST_F(PathTest, EmptyEntriesSkipped) {
    // An empty entry must not mean the current directory or the root: "/tool" must not be probed.
    make(root / "b" / "tool", 0755);
    const std::string env = ":" + (root / "a").string() + "::" + (root / "b").string() + ":";
    EXPECT_EQ(findOnPath("tool", env), (root / "b" / "tool").string());
    EXPECT_EQ(findOnPath("tool", "::"), std::nullopt);
    // With the program in the current directory an empty entry still finds nothing.
    const auto here = std::filesystem::current_path();
    std::filesystem::current_path(root / "b");
    const auto viaEmpty = findOnPath("tool", ":");
    std::filesystem::current_path(here);
    EXPECT_EQ(viaEmpty, std::nullopt);
}

TEST_F(PathTest, NotFound) {
    EXPECT_EQ(findOnPath("tool", (root / "a").string() + ":" + (root / "b").string()), std::nullopt);
    EXPECT_EQ(findOnPath("tool", ""), std::nullopt);
}

TEST_F(PathTest, DirectoryNamedLikeProgramIsNotFound) {
    std::filesystem::create_directories(root / "a" / "tool");
    EXPECT_EQ(findOnPath("tool", (root / "a").string()), std::nullopt);
}

ChartRecord fullRecord() {
    ChartRecord r;
    r.id = "20261015-101500";
    r.name = "Solent - Sat";
    r.sourceGuid = "AAAA-1";
    r.overlayGuid = "BBBB-2";
    r.sourceName = "Google Sat";
    r.bbox = {-1.5, 50.25, 2.125, 51.0};
    r.zooms = {8, 9, 10, 12};
    r.created = "2026-10-15T10:15:00";
    r.files = {"/c/a.mbtiles", "/c/b.mbtiles", "/c/c.mbtiles"};
    r.state = ChartState::Interrupted;
    r.tiles = 1234567890123;
    r.missing = 7;
    r.bytes = 98765432101;
    r.lastError = "disk full";
    r.updateBefore = "2026-09-16T10:00:00";
    r.done = 565;
    return r;
}

TEST(ChartIni, ExactText) {
    EXPECT_EQ(chartIni(fullRecord()), "Name=Solent - Sat\n"
                                      "SourceGuid=AAAA-1\n"
                                      "OverlayGuid=BBBB-2\n"
                                      "SourceName=Google Sat\n"
                                      "BBox=-1.50000000,50.25000000,2.12500000,51.00000000\n"
                                      "Zooms=8-10,12\n"
                                      "Created=2026-10-15T10:15:00\n"
                                      "File0=/c/a.mbtiles\n"
                                      "File1=/c/b.mbtiles\n"
                                      "File2=/c/c.mbtiles\n"
                                      "State=interrupted\n"
                                      "Tiles=1234567890123\n"
                                      "Missing=7\n"
                                      "Bytes=98765432101\n"
                                      "Done=565\n"
                                      "LastError=disk full\n"
                                      "UpdateBefore=2026-09-16T10:00:00\n"
                                      "Weather=0\n"
                                      "Snapshot=\n"
                                      "Credit=\n");
}

TEST(ChartIni, RoundTripEveryField) {
    const ChartRecord in = fullRecord();
    const auto out = parseChartIni(chartIni(in), in.id);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->id, in.id);
    EXPECT_EQ(out->name, in.name);
    EXPECT_EQ(out->sourceGuid, in.sourceGuid);
    EXPECT_EQ(out->overlayGuid, in.overlayGuid);
    EXPECT_EQ(out->sourceName, in.sourceName);
    EXPECT_EQ(out->bbox.west, in.bbox.west);
    EXPECT_EQ(out->bbox.south, in.bbox.south);
    EXPECT_EQ(out->bbox.east, in.bbox.east);
    EXPECT_EQ(out->bbox.north, in.bbox.north);
    EXPECT_EQ(out->zooms, in.zooms);
    EXPECT_EQ(out->created, in.created);
    EXPECT_EQ(out->files, in.files);
    EXPECT_EQ(out->state, in.state);
    EXPECT_EQ(out->tiles, in.tiles);
    EXPECT_EQ(out->missing, in.missing);
    EXPECT_EQ(out->bytes, in.bytes);
    EXPECT_EQ(out->lastError, in.lastError);
    EXPECT_EQ(out->updateBefore, in.updateBefore);
    EXPECT_EQ(out->done, 565);
}

// Done is a whole decimal 0..1000; a file without it, or anything else, is 0 (unknown).
TEST(ChartIni, DoneParsesWholeDecimalsOnly) {
    const std::vector<std::pair<std::string, int>> rows = {
        {"Done=0\n", 0},   {"Done=1000\n", 1000}, {"Done=565\n", 565}, {"", 0},
        {"Done=\n", 0},    {"Done=-5\n", 0},      {"Done=1001\n", 0},  {"Done=12x\n", 0},
        {"Done= 50\n", 0}, {"Done=50 \n", 0},     {"Done=+50\n", 0},
    };
    for (const auto& [line, expected] : rows) {
        const auto out = parseChartIni("Name=x\n" + line, "id");
        ASSERT_TRUE(out.has_value()) << line;
        EXPECT_EQ(out->done, expected) << line;
    }
}

TEST(ChartIni, UpdateBeforeEmptyIsWrittenAndALandedFileParsesWithItEmpty) {
    ChartRecord r = fullRecord();
    r.updateBefore.clear();
    const std::string t = chartIni(r);
    EXPECT_NE(t.find("LastError=disk full\nUpdateBefore=\nWeather=0\n"), std::string::npos);
    std::string landed = t;
    landed.erase(landed.find("UpdateBefore=\n"), 14);
    ASSERT_EQ(landed.find("UpdateBefore"), std::string::npos);
    const auto out = parseChartIni(landed, "x");
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->updateBefore, "");
    EXPECT_EQ(out->lastError, "disk full");
}

TEST(ChartIni, EveryStateRoundTrips) {
    const char* names[] = {"running", "complete", "missing", "interrupted", "failed"};
    const ChartState states[] = {ChartState::Running, ChartState::Complete, ChartState::Missing,
                                 ChartState::Interrupted, ChartState::Failed};
    for (int i = 0; i < 5; ++i) {
        ChartRecord r = fullRecord();
        r.state = states[i];
        const std::string t = chartIni(r);
        EXPECT_NE(t.find(std::string("State=") + names[i] + "\n"), std::string::npos) << names[i];
        EXPECT_EQ(parseChartIni(t, "x")->state, states[i]) << names[i];
    }
}

TEST(ChartIni, NewlineInLastErrorBecomesSpace) {
    ChartRecord r = fullRecord();
    r.lastError = "line one\nline two\r\nthree";
    const std::string t = chartIni(r);
    EXPECT_NE(t.find("LastError=line one line two  three\n"), std::string::npos);
    EXPECT_EQ(parseChartIni(t, "x")->lastError, "line one line two  three");
}

TEST(ChartIni, WeatherName) {
    EXPECT_EQ(weatherName("Solent"), "Solent - Weather");
    EXPECT_EQ(weatherName("Финский залив"), "Финский залив - Weather");
}

TEST(ChoiceLabel, FiftyCodePointsStayAndLongerNamesCutAtFortySevenWithoutBreakingASequence) {
    const std::string fifty(50, 'a'), fiftyOne(51, 'a');
    EXPECT_EQ(choiceLabel(fifty), fifty);
    EXPECT_EQ(choiceLabel(fiftyOne), std::string(47, 'a') + "...");
    std::string cyr60, cyr47; // 60 letters of 2 bytes each
    for (int i = 0; i < 60; ++i)
        cyr60 += i % 2 ? "\xD0\xB1" : "\xD0\x91"; // Б б
    for (int i = 0; i < 47; ++i)
        cyr47 += i % 2 ? "\xD0\xB1" : "\xD0\x91";
    EXPECT_EQ(cyr60.size(), 120u);
    EXPECT_EQ(choiceLabel(cyr60), cyr47 + "...");
    std::string cyr50;
    for (int i = 0; i < 50; ++i)
        cyr50 += "\xD0\x91";
    EXPECT_EQ(choiceLabel(cyr50), cyr50) << "50 letters = 100 bytes: counted as code points";
    EXPECT_EQ(choiceLabel(""), "");
}

TEST(ChartIni, WeatherKeysRoundTrip) {
    ChartRecord r = fullRecord();
    r.weather = true;
    r.snapshot = "2026-10-15T20:10:00Z";
    r.credit = "Weather data by RainViewer (https://www.rainviewer.com)";
    const std::string t = chartIni(r);
    EXPECT_NE(t.find("\nWeather=1\n"), std::string::npos);
    EXPECT_NE(t.find("\nSnapshot=2026-10-15T20:10:00Z\n"), std::string::npos);
    EXPECT_NE(t.find("\nCredit=Weather data by RainViewer (https://www.rainviewer.com)\n"),
              std::string::npos);
    const auto out = parseChartIni(t, "x");
    ASSERT_TRUE(out.has_value());
    EXPECT_TRUE(out->weather);
    EXPECT_EQ(out->snapshot, r.snapshot);
    EXPECT_EQ(out->credit, r.credit);
}

TEST(ChartIni, NoWeatherWritesZeroAndEmpty) {
    const std::string t = chartIni(fullRecord());
    EXPECT_NE(t.find("\nWeather=0\nSnapshot=\nCredit=\n"), std::string::npos);
}

TEST(ChartIni, MissingOrOddWeatherKeysParseAsNone) {
    const auto old = parseChartIni("Name=N\n", "x");
    ASSERT_TRUE(old.has_value());
    EXPECT_FALSE(old->weather);
    EXPECT_TRUE(old->snapshot.empty());
    EXPECT_TRUE(old->credit.empty());
    EXPECT_FALSE(parseChartIni("Name=N\nWeather=yes\n", "x")->weather);
    EXPECT_FALSE(parseChartIni("Name=N\nWeather=\n", "x")->weather);
}

TEST(ChartIni, NewlineInCreditBecomesSpace) {
    ChartRecord r = fullRecord();
    r.credit = "a\nb";
    const std::string t = chartIni(r);
    EXPECT_NE(t.find("Credit=a b\n"), std::string::npos);
    EXPECT_EQ(parseChartIni(t, "x")->credit, "a b");
}

TEST(ChartIni, TenFilesStayInNumericOrder) {
    ChartRecord r = fullRecord();
    r.files.clear();
    for (int i = 0; i < 11; ++i) {
        r.files.push_back("/c/f" + std::to_string(i));
    }
    EXPECT_EQ(parseChartIni(chartIni(r), "x")->files, r.files);
}

TEST(ChartIni, UnknownKeyIgnoredAndMissingNameRejected) {
    const auto ok = parseChartIni("Future=1\nName=N\nTiles=5\n", "id1");
    ASSERT_TRUE(ok.has_value());
    EXPECT_EQ(ok->name, "N");
    EXPECT_EQ(ok->tiles, 5);
    EXPECT_EQ(ok->id, "id1");
    EXPECT_FALSE(parseChartIni("Tiles=5\nState=complete\n", "id1").has_value());
    EXPECT_FALSE(parseChartIni("", "id1").has_value());
}

} // namespace
} // namespace cg
