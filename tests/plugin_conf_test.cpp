// SPDX-License-Identifier: GPL-3.0-or-later
// The .conf family (src/core/plugin_conf.h) and the sync of data/source-descriptions.conf
// with data/sources and the "What it is for" column of docs/user-guide.md.
#include "cli_records.h"
#include "plugin_conf.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <utility>
#include <variant>

namespace cg {
namespace {

const std::string kA = "{7B743985-BC5F-4AB6-8915-AC5DBBB8F552}";
const std::string kB = "{51C1079B-B55D-4DF7-A410-ADC118F53AB1}";
const std::string kC = "{25dba489-d484-4a13-b65d-aba8967ff3da}";

SourceDescriptions parse(const std::string& text, std::vector<std::string>& problems, int* format = nullptr) {
    const ConfFile f = parseConf(text);
    EXPECT_TRUE(f.found);
    if (format)
        *format = f.format;
    return descriptionsFrom(f, problems);
}

TEST(PluginConf, ParsesBomCrlfCommentsBlankLinesExtraColumnsAndKeepsUtf8ByteForByte) {
    const std::string cyr = "\xD0\x9F\xD0\xBB\xD0\xB0\xD0\xBD \xC3\xA9 d'x"; // "План é d'x"
    const std::string text = "\xEF\xBB\xBF# comment\r\n\r\nformat\t1\r\n" + kA + "\tFirst\textra\tmore\r\n" +
                             kB + "\t" + cyr + "\r\n#" + kC + "\tcommented\n";
    const ConfFile f = parseConf(text);
    EXPECT_EQ(f.format, 1);
    ASSERT_EQ(f.rows.size(), 2u);
    EXPECT_EQ(f.rows[0].line, 4);
    EXPECT_EQ(f.rows[0].fields, (std::vector<std::string>{kA, "First", "extra", "more"}));
    EXPECT_EQ(f.rows[1].line, 5);
    EXPECT_EQ(f.rows[1].fields[1], cyr);
    std::vector<std::string> problems;
    const SourceDescriptions d = descriptionsFrom(f, problems);
    EXPECT_TRUE(problems.empty());
    EXPECT_EQ(d.size(), 2u);
    EXPECT_EQ(d.at(kB), cyr);
    EXPECT_EQ(d.at(kA), "First");
}

TEST(PluginConf, TheFormatLineIsAbsentOneAnotherOrBad) {
    struct Row {
        const char* text;
        int format;
        std::vector<std::string> problems;
    };
    const std::vector<Row> rows = {
        {"", 0, {}},
        {"format\t1\n", 1, {}},
        {"format\t7\n", 7, {"format 7 is newer than 1: read as format 1"}},
        {"format\t0\n", -1, {"bad format line: read as format 1"}},
        {"format\tx1\n", -1, {"bad format line: read as format 1"}},
        {"format\n", -1, {"bad format line: read as format 1"}},
        {"format\t-3\n", -1, {"bad format line: read as format 1"}},
    };
    for (const Row& r : rows) {
        SCOPED_TRACE(r.text);
        std::vector<std::string> problems;
        int format = 99;
        parse(r.text, problems, &format);
        EXPECT_EQ(format, r.format);
        EXPECT_EQ(problems, r.problems);
    }
    // a later line starting with "format" is an ordinary row, not a second format line
    const ConfFile f = parseConf("format\t1\nformat\t2\n");
    EXPECT_EQ(f.format, 1);
    EXPECT_EQ(f.rows.size(), 1u);
}

TEST(PluginConf, BadRowsGiveOneProblemWithTheirLineAndTheGoodRowsAroundThemSurvive) {
    const std::string text = "format\t1\n" + kA + "\tone\nnot-a-guid\tx\n" + kB + "\n" + kB + "\t\n" + kC +
                             "\tthree\n" + "{51C1079B-B55D-4DF7-A410-ADC118F53AB1\tno brace\n" + kC +
                             "\trepeat\n";
    std::vector<std::string> problems;
    const SourceDescriptions d = parse(text, problems);
    EXPECT_EQ(problems, (std::vector<std::string>{"line 3: not a GUID", "line 4: no text", "line 5: no text",
                                                  "line 7: not a GUID", "line 8: repeated GUID"}));
    ASSERT_EQ(d.size(), 2u);
    EXPECT_EQ(d.at(kA), "one");
    EXPECT_EQ(d.at("{25DBA489-D484-4A13-B65D-ABA8967FF3DA}"), "three") << "key upper-cased; the first wins";
}

TEST(PluginConf, ALowerCaseGuidIsFoundByItsUpperCaseForm) {
    std::vector<std::string> problems;
    const SourceDescriptions d = parse(kC + "\ttext\n", problems);
    EXPECT_EQ(d.count("{25DBA489-D484-4A13-B65D-ABA8967FF3DA}"), 1u);
    EXPECT_EQ(d.count(kC), 0u);
}

TEST(PluginConf, LooksLikeGuid) {
    EXPECT_TRUE(looksLikeGuid(kA));
    EXPECT_TRUE(looksLikeGuid(kC));
    for (const char* bad :
         {"", "{}", "{7B743985-BC5F-4AB6-8915-AC5DBBB8F55}", "7B743985-BC5F-4AB6-8915-AC5DBBB8F552",
          "{7B743985-BC5F-4AB6-8915-AC5DBBB8F55G}", "{7B743985_BC5F-4AB6-8915-AC5DBBB8F552}",
          "{7B743985-BC5F-4AB6-8915-AC5DBBB8F5522}"})
        EXPECT_FALSE(looksLikeGuid(bad)) << bad;
}

TEST(PluginConf, ReadConfOfAMissingFileIsNotFoundAndAPathWithSpaceQuoteAndCyrillicReads) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "cg-conf-test \xD0\x9F d'x";
    std::filesystem::create_directories(dir);
    EXPECT_FALSE(readConf((dir / "missing.conf").string()).found);
    EXPECT_FALSE(readConf("").found);
    {
        std::ofstream(dir / "a.conf", std::ios::binary) << "format\t1\n" << kA << "\tok\n";
    }
    const ConfFile f = readConf((dir / "a.conf").string());
    EXPECT_TRUE(f.found);
    EXPECT_EQ(f.format, 1);
    ASSERT_EQ(f.rows.size(), 1u);
    EXPECT_EQ(f.rows[0].fields[1], "ok");
    std::filesystem::remove_all(dir);
}

TEST(PluginConf, ConfBesideSourcesTakesTheParentAndDropsTrailingSeparators) {
    EXPECT_EQ(confBesideSources("/a b/d'x/sources-1", "n.conf"), "/a b/d'x/n.conf");
    EXPECT_EQ(confBesideSources("/a/sources-1/", "n.conf"), "/a/n.conf");
    EXPECT_EQ(confBesideSources("C:\\p q\\sources-1\\", "n.conf"), "C:\\p q\\n.conf");
    EXPECT_EQ(confBesideSources("sources-1", "n.conf"), "n.conf");
    EXPECT_EQ(confBesideSources("", "n.conf"), "");
    EXPECT_EQ(confBesideSources("//", "n.conf"), "");
}

// ---- proof 3: the shipped file, the shipped sources and the guide agree ----

std::string slurp(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

TEST(PluginConf, TheShippedDescriptionsMatchTheSourcesAndTheGuide) {
    const std::filesystem::path root = CG_SOURCE_DIR;
    std::map<std::string, std::string> guidOfName; // from data/sources
    std::set<std::string> guids;
    const std::regex guidRe("\"guid\"\\s*:\\s*\"([^\"]*)\""), nameRe("\"name\"\\s*:\\s*\"([^\"]*)\"");
    for (const auto& e : std::filesystem::recursive_directory_iterator(root / "data" / "sources")) {
        const std::string n = e.path().filename().string();
        if (n.size() < 12 || n.compare(n.size() - 12, 12, ".source.json") != 0)
            continue;
        const std::string json = slurp(e.path());
        std::smatch g, nm;
        ASSERT_TRUE(std::regex_search(json, g, guidRe) && std::regex_search(json, nm, nameRe)) << n;
        EXPECT_TRUE(guidOfName.emplace(nm[1], g[1]).second) << "two sources named " << nm[1];
        guids.insert(g[1]);
    }
    const ConfFile conf = readConf((root / "data" / "source-descriptions.conf").string());
    ASSERT_TRUE(conf.found);
    EXPECT_EQ(conf.format, 1);
    std::vector<std::string> problems;
    const SourceDescriptions d = descriptionsFrom(conf, problems);
    EXPECT_TRUE(problems.empty()) << problems.size() << " problems, the first: " << problems.at(0);
    std::set<std::string> described;
    for (const auto& kv : d)
        described.insert(kv.first);
    EXPECT_EQ(described, guids);

    // the guide: the rows of "Bundled charts" (| Group | Source | What it is for | Area |)
    std::istringstream guide(slurp(root / "docs" / "user-guide.md"));
    std::string line;
    bool in = false;
    std::size_t rows = 0;
    while (std::getline(guide, line)) {
        if (line.rfind("| Group | Source | What it is for", 0) == 0) {
            in = true;
            continue;
        }
        if (!in)
            continue;
        if (line.rfind("|", 0) != 0)
            break;
        if (line.rfind("|---", 0) == 0)
            continue;
        std::vector<std::string> cells;
        for (std::size_t p = 2;;) { // after "| "
            const std::size_t q = line.find(" | ", p);
            cells.push_back(line.substr(p, q == std::string::npos ? line.size() - 2 - p : q - p));
            if (q == std::string::npos)
                break;
            p = q + 3;
        }
        ASSERT_EQ(cells.size(), 4u) << line;
        ++rows;
        const auto src = guidOfName.find(cells[1]);
        ASSERT_NE(src, guidOfName.end()) << "guide source without a definition: " << cells[1];
        std::string key = src->second;
        for (char& c : key)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        const auto it = d.find(key);
        EXPECT_TRUE(it != d.end() && it->second == cells[2]) << "expected line:\n"
                                                             << src->second << "\t" << cells[2];
    }
    EXPECT_EQ(rows, guids.size()) << "every source has exactly one guide row";
}

// ---- favourite-sources.conf

using Kind = FavouriteProblem::Kind;
using Problems = std::vector<std::pair<Kind, int>>;

struct FavCase {
    const char* what;
    std::string text;
    std::vector<std::string> chart, weather;
    std::string seamarks;
    Problems problems;
};

Problems kinds(const std::vector<FavouriteProblem>& p) {
    Problems out;
    for (const FavouriteProblem& x : p)
        out.emplace_back(x.kind, x.line);
    return out;
}

const std::string kBLower = "{51c1079b-b55d-4df7-a410-adc118f53ab1}"; // kB in lower case

TEST(FavouriteSources, EveryProblemKindWithItsLineAndTheGoodLinesAroundItAreUsed) {
    const std::vector<FavCase> cases = {
        {"BOM, CRLF, comments, a Cyrillic name column, unsorted rows: file order kept",
         "\xEF\xBB\xBF# c\r\nformat\t1\r\nchart\t" + kB +
             "\t\xD0\x9F\xD0\xBB\xD0\xB0\xD0\xBD d'x\r\nweather\t" + kC + "\r\nchart\t" + kA +
             "\textra\r\nseamarks\t" + kA + "\r\n",
         {kB, kA},
         {kC},
         kA,
         {}},
        {"no format line: used, reported", "chart\t" + kA + "\n", {kA}, {}, "", {{Kind::NoFormatLine, 0}}},
        {"format 2: no line used", "format\t2\nchart\t" + kA + "\n", {}, {}, "", {{Kind::UnknownFormat, 0}}},
        {"a bad format number: no line used",
         "format\tx\nchart\t" + kA + "\n",
         {},
         {},
         "",
         {{Kind::UnknownFormat, 0}}},
        {"format 1 and no row", "format\t1\n# only\n", {}, {}, "", {{Kind::NoEntries, 0}}},
        {"a one-field row",
         "format\t1\nchart\t" + kA + "\nchart\nweather\t" + kC + "\n",
         {kA},
         {kC},
         "",
         {{Kind::TooFewFields, 3}}},
        {"an unknown role",
         "format\t1\nboat\t" + kA + "\nchart\t" + kB + "\n",
         {kB},
         {},
         "",
         {{Kind::UnknownRole, 2}}},
        {"not a GUID: the seamarks line before the good one is skipped",
         "format\t1\nseamarks\tnope\nseamarks\t" + kA + "\n",
         {},
         {},
         kA,
         {{Kind::NotGuid, 2}}},
        {"a repeat inside a role (case-insensitive) is dropped, the same GUID in another role is not",
         "format\t1\nchart\t" + kB + "\nchart\t" + kBLower + "\nweather\t" + kB + "\n",
         {kB},
         {kB},
         "",
         {{Kind::Repeated, 3}}},
        {"a second seamarks line: the first wins",
         "format\t1\nseamarks\t" + kA + "\nseamarks\t" + kB + "\n",
         {},
         {},
         kA,
         {{Kind::SecondSeamarks, 3}}},
        {"several problems in file order",
         "chart\t" + kA + "\nx\ty\nweather\t" + kC + "\nweather\t" + kC + "\n",
         {kA},
         {kC},
         "",
         {{Kind::NoFormatLine, 0}, {Kind::UnknownRole, 2}, {Kind::Repeated, 4}}},
    };
    for (const FavCase& c : cases) {
        SCOPED_TRACE(c.what);
        std::vector<FavouriteProblem> problems;
        const FavouriteSources f = favouriteSourcesFrom(parseConf(c.text), problems);
        EXPECT_EQ(f.chart, c.chart);
        EXPECT_EQ(f.weather, c.weather);
        EXPECT_EQ(f.seamarks, c.seamarks);
        EXPECT_EQ(kinds(problems), c.problems);
    }
}

TEST(FavouriteSources, AMissingFileIsOneProblemAndNoLine) {
    std::vector<FavouriteProblem> problems;
    const FavouriteSources f = favouriteSourcesFrom(ConfFile{}, problems);
    EXPECT_TRUE(f.chart.empty() && f.weather.empty() && f.seamarks.empty());
    EXPECT_EQ(kinds(problems), (Problems{{Kind::FileNotFound, 0}}));
}

TEST(FavouriteSources, TheUrlNamesTheTagOfTheVersion) {
    EXPECT_EQ(
        favouriteSourcesUrl("2026.11.1"),
        "https://github.com/Sea-For-Friends/chartgrabber_pi/blob/v2026.11.1/data/favourite-sources.conf");
}

std::map<std::string, SourceRecord> fixtureRecords() {
    std::map<std::string, SourceRecord> out;
    std::ifstream in(std::string(CG_FIXTURES_DIR) + "/cli-2026.11.1/sources-bundled-all.stdout",
                     std::ios::binary);
    for (std::string line; std::getline(in, line);) {
        const auto r = parseStdoutLine(line);
        if (r && std::holds_alternative<SourceRecord>(*r)) {
            std::string key = std::get<SourceRecord>(*r).guid;
            out.emplace(key, std::get<SourceRecord>(*r));
        }
    }
    return out;
}

TEST(FavouriteSources, TheShippedFileParsesCleanlyAndEveryGuidIsAFittingBundledRecord) {
    const auto records = fixtureRecords();
    ASSERT_EQ(records.size(), 81u);
    const ConfFile conf =
        readConf((std::filesystem::path(CG_SOURCE_DIR) / "data" / "favourite-sources.conf").string());
    ASSERT_TRUE(conf.found);
    std::vector<FavouriteProblem> problems;
    const FavouriteSources f = favouriteSourcesFrom(conf, problems);
    EXPECT_TRUE(problems.empty()) << problems.size() << " problems, the first: " << int(problems.at(0).kind);
    EXPECT_EQ(f.chart.size(), 5u);
    EXPECT_EQ(f.weather.size(), 5u);
    EXPECT_EQ(f.weather.front(), "{1A696D82-F258-41A4-BCD6-7648D9E08DE2}") << "RainViewer first";
    for (const std::string& g : f.chart) {
        ASSERT_TRUE(records.count(g)) << g;
        EXPECT_TRUE(records.at(g).kind == "map" || records.at(g).isLayer()) << g;
        EXPECT_TRUE(records.at(g).usable()) << g;
    }
    for (const std::string& g : f.weather) {
        ASSERT_TRUE(records.count(g)) << g;
        EXPECT_TRUE(records.at(g).isWeather() && records.at(g).isLayer()) << g;
    }
    ASSERT_TRUE(records.count(f.seamarks));
    EXPECT_TRUE(records.at(f.seamarks).isLayer());
    // the file holds no field that only the shipped copy needs: every row has the role, the GUID and a name
    for (const ConfRow& r : conf.rows)
        EXPECT_EQ(r.fields.size(), 3u) << "line " << r.line;
}

// The imagery rule leans on the group name "Satellite" never being
// translated.
TEST(FavouriteSources, EverySatelliteSourceFileAndFixtureRecordHasTheGroupSatellite) {
    const std::regex groupRe("\"group\"\\s*:\\s*\"([^\"]*)\"");
    std::size_t files = 0;
    for (const auto& e : std::filesystem::directory_iterator(std::filesystem::path(CG_SOURCE_DIR) / "data" /
                                                             "sources" / "Satellite")) {
        if (e.path().string().size() < 12 ||
            e.path().string().compare(e.path().string().size() - 12, 12, ".source.json") != 0)
            continue;
        ++files;
        const std::string json = slurp(e.path());
        std::smatch m;
        ASSERT_TRUE(std::regex_search(json, m, groupRe)) << e.path();
        EXPECT_EQ(m[1].str(), "Satellite") << e.path();
    }
    EXPECT_GT(files, 0u);
    std::size_t rows = 0;
    for (const auto& kv : fixtureRecords())
        if (kv.second.zmpFile.rfind("Satellite/", 0) == 0) {
            ++rows;
            EXPECT_EQ(kv.second.group, "Satellite") << kv.second.name;
        }
    EXPECT_EQ(rows, 11u);
}

} // namespace
} // namespace cg
