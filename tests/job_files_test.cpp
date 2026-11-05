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

// on Windows every path value the CLI gets is in native form; the other values keep their
// text.
Settings nativeSettings() {
    Settings s;
    s.cliMaps = {"C:/Maps/\xC3\xA9t\xC3\xA9 charts/a'b", "D:/Chart Nav/m"};
    s.cacheDir = "C:\\ProgramData\\opencpn/plugins/chartgrabber/cache";
    s.proxy = "http://h:3128/";
    return s;
}

TEST(ArgumentsNativePath, WindowsSourcesAndEstimateRoute) {
    const Settings s = nativeSettings();
    EXPECT_EQ(
        sourcesArguments(s, Os::Windows),
        (Args{"sources", "--format=tsv", "--maps", "C:\\Maps\\\xC3\xA9t\xC3\xA9 charts\\a'b", "--maps",
              "D:\\Chart Nav\\m", "--cache", "C:\\ProgramData\\opencpn\\plugins\\chartgrabber\\cache"}));
    JobSpec j = routeOverlayOnly();
    j.corridorNm = 2.5;
    j.routeGpxPath = "C:/Users/me/My Routes/route.gpx";
    EXPECT_EQ(
        estimateArguments(j, s, Os::Windows),
        (Args{"estimate", "--format=tsv", "--progress=lines", "--maps",
              "C:\\Maps\\\xC3\xA9t\xC3\xA9 charts\\a'b", "--maps", "D:\\Chart Nav\\m", "--cache",
              "C:\\ProgramData\\opencpn\\plugins\\chartgrabber\\cache", "--overlay", "BBBB-2", "--route",
              "C:\\Users\\me\\My Routes\\route.gpx", "--corridor-nm", "2.5", "--zooms", "15-16"}));
}

TEST(ArgumentsNativePath, WindowsRunJobAndProxyKeepsItsSlash) {
    EXPECT_EQ(runArguments("C:/ProgramData/opencpn/plugins/chartgrabber/jobs/20261022-140040/job.ini",
                           RunKind::Update, false, nativeSettings(), "2026-09-16T10:00:00", Os::Windows),
              (Args{"run", "--job",
                    "C:\\ProgramData\\opencpn\\plugins\\chartgrabber\\jobs\\20261022-140040\\job.ini",
                    "--format=tsv", "--progress=lines", "--maps", "C:\\Maps\\\xC3\xA9t\xC3\xA9 charts\\a'b",
                    "--maps", "D:\\Chart Nav\\m", "--cache",
                    "C:\\ProgramData\\opencpn\\plugins\\chartgrabber\\cache", "--proxy", "http://h:3128/",
                    "--replace", "--replace-older-than=2026-09-16T10:00:00"}));
}

TEST(ArgumentsNativePath, PosixKeepsEveryByte) {
    Settings s;
    s.cliMaps = {"/m\\y"};
    s.cacheDir = "/c\\d";
    s.proxy = "http://h:3128/";
    EXPECT_EQ(sourcesArguments(s, Os::Posix),
              (Args{"sources", "--format=tsv", "--maps", "/m\\y", "--cache", "/c\\d"}));
    EXPECT_EQ(runArguments("/j\\job.ini", RunKind::Download, false, s, "", Os::Posix),
              (Args{"run", "--job", "/j\\job.ini", "--format=tsv", "--progress=lines", "--maps", "/m\\y",
                    "--cache", "/c\\d", "--proxy", "http://h:3128/"}));
}

// ONE `--datadir=DIR` element right after the --cache pair on sources, estimate, run,
// export; never the two-element form; none when cliDataDir is empty.
TEST(Arguments, DatadirIsOneEqualsElementAfterCacheOnEveryCommand) {
    struct Case {
        Os os;
        const char* cache;
        const char* data;
        const char* want;
    };
    const Case cases[] = {
        {Os::Posix, "/ca che", "/d\xC3\xA9 a'b/cli-data", "--datadir=/d\xC3\xA9 a'b/cli-data"},
        {Os::Windows, "C:/P D/cache", "C:/P D/\xC3\xA9'/cli-data", "--datadir=C:\\P D\\\xC3\xA9'\\cli-data"},
    };
    for (const Case& c : cases) {
        Settings s = fullSettings();
        s.cacheDir = c.cache;
        s.cliDataDir = c.data;
        const Args all[] = {
            sourcesArguments(s, c.os),
            estimateArguments(viewBase(), s, c.os),
            runArguments("/j/job.ini", RunKind::Download, false, s, "", c.os),
            runArguments("/j/job.ini", RunKind::BuildFromSaved, false, s, "", c.os),
        };
        for (const Args& a : all) {
            int n = 0;
            std::size_t at = 0;
            for (std::size_t i = 0; i < a.size(); ++i) {
                if (a[i].rfind("--datadir", 0) == 0) {
                    ++n;
                    at = i;
                }
            }
            ASSERT_EQ(n, 1) << a[0];
            EXPECT_EQ(a[at], c.want) << a[0];
            ASSERT_GE(at, 2u);
            EXPECT_EQ(a[at - 2], "--cache") << a[0];
        }
        s.cliDataDir.clear();
        for (const Args& a : {sourcesArguments(s, c.os), estimateArguments(viewBase(), s, c.os),
                              runArguments("/j/job.ini", RunKind::Update, false, s, "x", c.os)}) {
            for (const std::string& e : a)
                EXPECT_NE(e.rfind("--datadir", 0), 0u) << a[0];
        }
    }
}

// the `--version` probe argv is "--version" and ONE `--datadir=DIR` element (the
// '=' form, native separators of `os`); only "--version" when cliDataDir is empty.
TEST(Arguments, VersionProbeCarriesTheDatadir) {
    Settings s = fullSettings();
    s.cliDataDir = "/d\xC3\xA9 a'b/cli-data";
    EXPECT_EQ(versionArguments(s, Os::Posix), (Args{"--version", "--datadir=/d\xC3\xA9 a'b/cli-data"}));
    s.cliDataDir = "C:/P D/\xC3\xA9'/cli-data";
    EXPECT_EQ(versionArguments(s, Os::Windows),
              (Args{"--version", "--datadir=C:\\P D\\\xC3\xA9'\\cli-data"}));
    s.cliDataDir.clear();
    EXPECT_EQ(versionArguments(s, Os::Posix), (Args{"--version"}));
    EXPECT_EQ(versionArguments(s, Os::Windows), (Args{"--version"}));
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

TEST(Arguments, WindowsCommandHasNoEnvAndNoNice) {
    EXPECT_EQ(cliCommand("C:\\Program Files\\cli.exe", {"run", "it's"}, false, Os::Windows),
              (Args{"C:\\Program Files\\cli.exe", "run", "it's"}));
    EXPECT_EQ(cliCommand("C:\\cli.exe", {"run"}, true, Os::Windows), (Args{"C:\\cli.exe", "run"}));
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

TEST(DisplayCommand, WindowsIsTheCommandLineTheCliGets) {
    EXPECT_EQ(displayCommand({"C:\\My dir\\cli.exe", "--cache", "D:\\cache\\"}, Os::Windows),
              "\"C:\\My dir\\cli.exe\" --cache D:\\cache\\");
}

TEST(Names, DefaultSeamarksAndFile) {
    EXPECT_EQ(defaultChartName("Solent", "Google Sat"), "Solent - Google Sat");
    EXPECT_EQ(seamarksName("Solent - Sat"), "Solent - Sat - Seamarks");
    EXPECT_EQ(chartFileName("A/B"), "A_B.mbtiles");
    EXPECT_EQ(chartFileName(std::string("a\tb\nc\x01") + "d"), "a_b_c_d.mbtiles");
    EXPECT_EQ(chartFileName("Skipper's"), "Skippers.mbtiles");
    EXPECT_EQ(chartFileName("Plain"), "Plain.mbtiles");
    EXPECT_EQ(chartFileName("Карты моря"), "Карты моря.mbtiles");
}

// one rule on every system; a chart named on Linux is named the same on Windows.
TEST(Names, ChartFileNameWindowsCharactersAndDeviceNames) {
    EXPECT_EQ(chartFileName("a/b\\c:d*e?f\"g<h>i|j"), "a_b_c_d_e_f_g_h_i_j.mbtiles");
    EXPECT_EQ(chartFileName(std::string("a\x01") + "\x7f" + "b"), "a__b.mbtiles");
    EXPECT_EQ(chartFileName("CON"), "_CON.mbtiles");
    EXPECT_EQ(chartFileName("con"), "_con.mbtiles");
    EXPECT_EQ(chartFileName("Nul"), "_Nul.mbtiles");
    EXPECT_EQ(chartFileName("COM1"), "_COM1.mbtiles");
    EXPECT_EQ(chartFileName("LPT9"), "_LPT9.mbtiles");
    EXPECT_EQ(chartFileName("aux.backup"), "_aux.backup.mbtiles");
    EXPECT_EQ(chartFileName("CON "), "_CON .mbtiles");
    EXPECT_EQ(chartFileName("CONSOLE"), "CONSOLE.mbtiles");
    EXPECT_EQ(chartFileName("COM10"), "COM10.mbtiles");
    EXPECT_EQ(chartFileName("COM0"), "COM0.mbtiles");
    EXPECT_EQ(chartFileName("Icon"), "Icon.mbtiles");
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
        root = std::filesystem::temp_directory_path() /
               (std::string("cg_path_test_") + std::to_string(::getpid()));
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

TEST_F(PathTest, WindowsSemicolonsQuotesEmptyEntriesAndExeSuffix) {
    // Directories given with a trailing "/" so the joined candidate is a real Linux path (portable_test
    // covers "\").
    make(root / "a" / "tool.exe", 0644);
    make(root / "b" / "tool.exe", 0644);
    std::filesystem::create_directories(root / "c");
    make(root / "c" / "tool", 0755); // no .exe: not a program on Windows
    const std::string a = (root / "a").string() + "/";
    const std::string b = (root / "b").string() + "/";
    const std::string c = (root / "c").string() + "/";
    const std::string want = (root / "b" / "tool.exe").string();
    EXPECT_EQ(findOnPath("tool", "\"" + b + "\";;" + a, Os::Windows),
              want);                                               // quoted, empty skipped, first wins
    EXPECT_EQ(findOnPath("tool.exe", ";" + b, Os::Windows), want); // no second ".exe"
    EXPECT_EQ(findOnPath("tool", c, Os::Windows), std::nullopt);
    EXPECT_EQ(findOnPath("tool", ";;", Os::Windows), std::nullopt);
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
                                      "ModelRun=\n"
                                      "Credit=\n");
}

// Stale<i> lines follow the File<i> lines; none when empty; a file without them parses with stale
// empty.
TEST(ChartIni, StaleLinesFollowTheFilesAndRoundTrip) {
    ChartRecord r = fullRecord();
    r.stale = {"C:\\Charts\\\xD0\x9A\xD0\xB0 d'x (2).mbtiles", "/c/old.mbtiles"};
    const std::string t = chartIni(r);
    EXPECT_NE(t.find("File2=/c/c.mbtiles\n"
                     "Stale0=C:\\Charts\\\xD0\x9A\xD0\xB0 d'x (2).mbtiles\n"
                     "Stale1=/c/old.mbtiles\n"
                     "State=interrupted\n"),
              std::string::npos)
        << t;
    const auto back = parseChartIni(t, r.id);
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->stale, r.stale);
    EXPECT_EQ(back->files, r.files);
    EXPECT_EQ(chartIni(fullRecord()).find("Stale"), std::string::npos);
    const auto none = parseChartIni(chartIni(fullRecord()), "x");
    ASSERT_TRUE(none.has_value());
    EXPECT_TRUE(none->stale.empty());
    // index order, not file order
    const auto shuffled = parseChartIni("Name=n\nStale1=/b\nStale0=/a\n", "x");
    ASSERT_TRUE(shuffled.has_value());
    EXPECT_EQ(shuffled->stale, (std::vector<std::string>{"/a", "/b"}));
}

// alternateWeatherFile.
TEST(AlternateWeatherFile, TableOfNames) {
    struct Row {
        const char* what;
        std::string current;
        std::vector<std::string> taken; // existing files
        Os os;
        std::optional<std::string> expect;
    };
    const std::string cyr = "C:\\\xD0\x9A\xD0\xB0\xD1\x80\xD1\x82\xD0\xB0 d'x\\";
    const std::vector<Row> rows = {
        {"first swap", "/c/X.mbtiles", {"/c/X.mbtiles"}, Os::Posix, "/c/X (2).mbtiles"},
        {"cycle back", "/c/X (2).mbtiles", {"/c/X (2).mbtiles"}, Os::Posix, "/c/X.mbtiles"},
        {"taken name skipped",
         "/c/X.mbtiles",
         {"/c/X.mbtiles", "/c/X (2).mbtiles"},
         Os::Posix,
         "/c/X (3).mbtiles"},
        {"cycle back skips a leftover X",
         "/c/X (2).mbtiles",
         {"/c/X (2).mbtiles", "/c/X.mbtiles"},
         Os::Posix,
         "/c/X (3).mbtiles"},
        {"ninth",
         "/c/X (8).mbtiles",
         {"/c/X.mbtiles", "/c/X (2).mbtiles", "/c/X (3).mbtiles", "/c/X (4).mbtiles", "/c/X (5).mbtiles",
          "/c/X (6).mbtiles", "/c/X (7).mbtiles"},
         Os::Posix,
         "/c/X (9).mbtiles"},
        {"all ten taken",
         "/c/X (4).mbtiles",
         {"/c/X.mbtiles", "/c/X (2).mbtiles", "/c/X (3).mbtiles", "/c/X (5).mbtiles", "/c/X (6).mbtiles",
          "/c/X (7).mbtiles", "/c/X (8).mbtiles", "/c/X (9).mbtiles"},
         Os::Posix,
         std::nullopt},
        {"two digits stay in the base", "/c/X (12).mbtiles", {}, Os::Posix, "/c/X (12) (2).mbtiles"},
        {"(1) stays in the base", "/c/X (1).mbtiles", {}, Os::Posix, "/c/X (1) (2).mbtiles"},
        {"not mbtiles", "/c/X.png", {}, Os::Posix, std::nullopt},
        {"no extension", "", {}, Os::Posix, std::nullopt},
        {"windows upper case, mixed separators",
         "C:/Ch/X.MBTILES",
         {"C:\\Ch\\X.MBTILES"},
         Os::Windows,
         "C:\\Ch\\X (2).mbtiles"},
        {"windows case-folded current is skipped",
         "C:\\Ch\\x (2).MbTiles",
         {},
         Os::Windows,
         "C:\\Ch\\x.mbtiles"},
        {"windows cyrillic with space and quote",
         cyr + "\xD0\x9A d'x.mbtiles",
         {},
         Os::Windows,
         cyr + "\xD0\x9A d'x (2).mbtiles"},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.what);
        const auto got = alternateWeatherFile(
            row.current,
            [&](const std::string& p) {
                return std::find(row.taken.begin(), row.taken.end(), p) != row.taken.end();
            },
            row.os);
        EXPECT_EQ(got, row.expect);
    }
}

// rewriteJobOutputs.
TEST(RewriteJobOutputs, ReplacesOnlyTheValues) {
    const std::string base =
        "[Job]\nSource=S\nOverlay=O\nZooms=8\nOut=/old/a.mbtiles\nOverlayOut=/old/b.mbtiles\n"
        "Name=n\n[Download]\nThreads=2\n";
    EXPECT_EQ(rewriteJobOutputs(base, "/new/a \xD0\x9A'.mbtiles", "/new/b.mbtiles"),
              "[Job]\nSource=S\nOverlay=O\nZooms=8\nOut=/new/a \xD0\x9A'.mbtiles\nOverlayOut=/new/b.mbtiles\n"
              "Name=n\n[Download]\nThreads=2\n");
    EXPECT_EQ(rewriteJobOutputs(base, "/new/a.mbtiles", ""),
              "[Job]\nSource=S\nOverlay=O\nZooms=8\nOut=/new/a.mbtiles\nOverlayOut=/old/b.mbtiles\n"
              "Name=n\n[Download]\nThreads=2\n");
    // a weather job: Out only, CRLF kept, a CR/LF inside the value becomes a space
    EXPECT_EQ(rewriteJobOutputs("[Job]\r\nOut=/o\r\nZooms=1", "/n\r\nx", ""),
              "[Job]\r\nOut=/n  x\r\nZooms=1");
    // only a line start counts; the first Out= wins
    EXPECT_EQ(rewriteJobOutputs("X Out=1\nOut=2\nOut=3", "N", ""), "X Out=1\nOut=N\nOut=3");
    EXPECT_EQ(rewriteJobOutputs("[Job]\nOverlayOut=/x\n", "N", ""), std::nullopt);
    EXPECT_EQ(rewriteJobOutputs("[Job]\nOut=/x\n", "N", "M"), std::nullopt);
    EXPECT_EQ(rewriteJobOutputs("", "N", ""), std::nullopt);
}

// removeStale: a missing file counts as removed; a failed removal stays, in order.
TEST(RemoveStale, KeepsOnlyWhatCannotBeRemoved) {
    std::vector<std::string> tried;
    const auto left = removeStale(
        {"/c/gone", "/c/held", "/c/ok", "/c/held2"}, [](const std::string& p) { return p != "/c/gone"; },
        [&](const std::string& p) {
            tried.push_back(p);
            return p == "/c/ok";
        });
    EXPECT_EQ(left, (std::vector<std::string>{"/c/held", "/c/held2"}));
    EXPECT_EQ(tried, (std::vector<std::string>{"/c/held", "/c/ok", "/c/held2"}));
}

// openCpnVersion. Hand-made input (OpenCPN cannot run in a unit test): the layout of wxJSONWriter
// text as pluginmanager.cpp:2087-2091 fills it, indented, other keys before and after.
TEST(OpenCpnVersion, ReadsBothIntegersOrNothing) {
    const std::string real =
        "{\n  \"OpenCPN Version Date\" : \"2024-1-2\",\n  \"OpenCPN Version Major\" : 5,\n"
        "  \"OpenCPN Version Minor\"\t:\n 10,\n  \"OpenCPN Version Patch\" : 2,\n  \"OpenCPN Platform\": "
        "\"x\"\n}";
    EXPECT_EQ(openCpnVersion(real), (std::pair<int, int>{5, 10}));
    EXPECT_EQ(openCpnVersion("{\"OpenCPN Version Major\":5,\"OpenCPN Version Minor\":14}"),
              (std::pair<int, int>{5, 14}));
    EXPECT_EQ(openCpnVersion("{\"OpenCPN Version Major\": 5}"), std::nullopt);
    EXPECT_EQ(openCpnVersion("{\"OpenCPN Version Major\": \"5\", \"OpenCPN Version Minor\": 10}"),
              std::nullopt);
    EXPECT_EQ(openCpnVersion("{\"OpenCPN Version MajorX\": 5, \"OpenCPN Version Minor\": 10}"), std::nullopt);
    EXPECT_EQ(openCpnVersion(""), std::nullopt);
}

TEST(OpenCpnVersion, RemoveIsSafeOnlyOnFiveBelowFourteen) {
    EXPECT_TRUE(chartListRemoveIsSafe(5, 10));
    EXPECT_TRUE(chartListRemoveIsSafe(5, 12));
    EXPECT_TRUE(chartListRemoveIsSafe(5, 13));
    EXPECT_FALSE(chartListRemoveIsSafe(5, 14));
    EXPECT_FALSE(chartListRemoveIsSafe(5, 15));
    EXPECT_FALSE(chartListRemoveIsSafe(6, 0));
}

TEST(ChartListPath, ListedDirAsGivenPlusSeparatorPlusName) {
    const std::string cyr = "\xD0\x9F\xD0\xBE\xD0\xB3\xD0\xBE\xD0\xB4\xD0\xB0 'x'.mbtiles"; // a space and a '
    struct Row {
        const char* dir;
        std::string file;
        Os os;
        std::string want;
    };
    const std::vector<Row> rows = {
        {"C:\\Charts\\", "C:\\Charts\\W.mbtiles", Os::Windows, "C:\\Charts\\W.mbtiles"},
        {"C:\\Charts", "C:/Charts/W.mbtiles", Os::Windows, "C:\\Charts\\W.mbtiles"},
        {"C:/Users/a\\Charts/", "C:\\Users\\a\\Charts\\W (2).mbtiles", Os::Windows,
         "C:/Users/a\\Charts\\W (2).mbtiles"},
        {"/home/a/Charts/", "/home/a/Charts/W.mbtiles", Os::Posix, "/home/a/Charts/W.mbtiles"},
        {"/home/a/Charts", "/home/a/Charts/" + cyr, Os::Posix, "/home/a/Charts/" + cyr},
        {"D:\\Карты", "D:\\Карты\\" + cyr, Os::Windows, "D:\\Карты\\" + cyr},
    };
    for (const Row& r : rows) {
        SCOPED_TRACE(r.dir);
        EXPECT_EQ(chartListPath(r.dir, r.file, r.os), r.want);
    }
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
    r.modelRun = "2026-10-15T18:00:00Z";
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
    EXPECT_EQ(out->modelRun, r.modelRun);
    EXPECT_EQ(out->credit, r.credit);
}

TEST(ChartIni, NoWeatherWritesZeroAndEmpty) {
    const std::string t = chartIni(fullRecord());
    EXPECT_NE(t.find("\nWeather=0\nSnapshot=\nModelRun=\nCredit=\n"), std::string::npos);
}

TEST(ChartIni, MissingOrOddWeatherKeysParseAsNone) {
    const auto old = parseChartIni("Name=N\n", "x");
    ASSERT_TRUE(old.has_value());
    EXPECT_FALSE(old->weather);
    EXPECT_TRUE(old->snapshot.empty());
    EXPECT_TRUE(old->modelRun.empty());
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
        r.files.push_back(std::string("/c/f") + std::to_string(i));
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

// the rule that decides whether OpenCPN's chart list is rescanned after a weather update.
TEST(PictureChanged, Table) {
    const std::string t1 = "2026-10-23T08:20:00Z", t2 = "2026-10-23T08:30:00Z";
    const std::string r1 = "2026-10-23T06:00:00Z", r2 = "2026-10-23T12:00:00Z";
    struct Row {
        const char *prevT, *prevR, *newT, *newR;
        bool changed;
    };
    const Row rows[] = {
        {t1.c_str(), r1.c_str(), t1.c_str(), r1.c_str(), false}, // same time and run
        {t1.c_str(), "", t1.c_str(), "", false},                 // same time, no run known
        {t1.c_str(), r1.c_str(), t2.c_str(), r1.c_str(), true},  // other time
        {"", "", t1.c_str(), "", true},                          // nothing known before
        {"", "", "", "", true},                                  // nothing known before or after
        {t1.c_str(), "", t1.c_str(), r1.c_str(), true},          // same time, a run now
        {t1.c_str(), r1.c_str(), t1.c_str(), r2.c_str(), true},  // same time, another run
        {t1.c_str(), r1.c_str(), t1.c_str(), "", false},         // same time, new run empty
        {t1.c_str(), "", "", "", true},                          // the new file reports no time
    };
    int i = 0;
    for (const Row& r : rows)
        EXPECT_EQ(cg::pictureChanged(r.prevT, r.prevR, r.newT, r.newR), r.changed) << "row " << i++;
}

// weather records that are not Running, in the order given.
TEST(WeatherBatch, KeepsWeatherRecordsThatAreNotRunningInOrder) {
    const auto rec = [](const char* id, bool weather, cg::ChartState state) {
        cg::ChartRecord r;
        r.id = id;
        r.weather = weather;
        r.state = state;
        return r;
    };
    const std::vector<cg::ChartRecord> in = {
        rec("e", true, cg::ChartState::Interrupted), rec("b", false, cg::ChartState::Complete),
        rec("c", true, cg::ChartState::Running),     rec("a", true, cg::ChartState::Missing),
        rec("d", true, cg::ChartState::Failed),      rec("f", false, cg::ChartState::Failed),
        rec("g", true, cg::ChartState::Complete)};
    std::string ids;
    for (const auto& r : cg::weatherBatch(in))
        ids += r.id;
    EXPECT_EQ(ids, "eadg");
    EXPECT_TRUE(cg::weatherBatch({}).empty());
}
