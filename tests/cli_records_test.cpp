// SPDX-License-Identifier: GPL-3.0-or-later
// CLI records, line splitting, sums, exit classification, preflight, retry, source refusal.
#include "cli_records.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace cg;

namespace {

std::vector<std::string> fixtureLines(const std::string& name) {
    std::ifstream in(std::string(CG_FIXTURES_DIR) + "/cli-2026.10.6/" + name, std::ios::binary);
    EXPECT_TRUE(in.good()) << name;
    std::vector<std::string> out;
    for (std::string line; std::getline(in, line);) {
        out.push_back(line);
    }
    return out;
}

// and 8: the captured lines of the pinned 2026.10.6 (15 columns: field 10 `origin` is `user`,
// the last is `navigationWarning`); the record is read by position and `origin` changes nothing.
TEST(Records, SourcesOfThePinnedRelease) {
    std::ifstream in(std::string(CG_FIXTURES_DIR) + "/cli-2026.10.6/sources-two-folders.stdout",
                     std::ios::binary);
    ASSERT_TRUE(in.good());
    std::vector<SourceRecord> recs;
    for (std::string line; std::getline(in, line);) {
        const auto cols = static_cast<std::size_t>(std::count(line.begin(), line.end(), '\t')) + 1;
        EXPECT_EQ(cols, 15u) << line;
        EXPECT_EQ(line.substr(line.rfind('\t') + 1), "0") << line; // navigationWarning, not read here
        const auto r = parseStdoutLine(line);
        ASSERT_TRUE(r && std::holds_alternative<SourceRecord>(*r)) << line;
        recs.push_back(std::get<SourceRecord>(*r));
    }
    ASSERT_EQ(recs.size(), 4u);
    EXPECT_EQ(recs[0].guid, "{7B743985-BC5F-4AB6-8915-AC5DBBB8F552}");
    EXPECT_EQ(recs[0].kind, "map");
    EXPECT_EQ(recs[0].name, "Esri World Imagery");
    EXPECT_EQ(recs[0].group, "Satellite");
    EXPECT_EQ(recs[0].zmpFile, "Esri-World-Imagery.zmp");
    EXPECT_EQ(recs[0].refresh, "static");
    EXPECT_EQ(recs[0].license, "Esri, Maxar, Earthstar Geographics, and the GIS User Community");
    EXPECT_FALSE(recs[0].maxZoom);
    EXPECT_EQ(recs[2].zmpFile, "Copy-Topo.zmp"); // the later folder's row of the duplicate GUID
    EXPECT_EQ(recs[3].refresh, "snapshot");
    EXPECT_EQ(recs[3].license, "Weather data by RainViewer (https://www.rainviewer.com)");
    EXPECT_EQ(recs[3].maxZoom, 7);
    EXPECT_TRUE(recs[3].isWeather());
    for (const auto& r : recs)
        EXPECT_FALSE(r.navigationWarning) << r.guid;
    // literal: whatever field 10 holds changes nothing
    for (const char* origin : {"user", "shipped", ""}) {
        const auto w =
            parseStdoutLine(std::string("source\t{A}\tmap\tN\timage/png\t3857\t0\tok\tf.zmp\tG\t") + origin +
                            "\tsnapshot\tCredit\t7\t0");
        ASSERT_TRUE(w) << origin;
        const auto& rec = std::get<SourceRecord>(*w);
        EXPECT_EQ(rec.group, "G") << origin;
        EXPECT_EQ(rec.refresh, "snapshot") << origin;
        EXPECT_EQ(rec.license, "Credit") << origin;
        EXPECT_EQ(rec.maxZoom, 7) << origin;
    }
}

const std::string kSource = "source\t{AAAA-1}\tmap\tOSM Mapnik\timage/png\t3857\t4\tok\tosm.zmp\tOpen maps\n";

std::vector<std::string> splitAll(const std::vector<std::string>& chunks, bool finish = true) {
    LineSplitter sp;
    std::vector<std::string> out;
    const auto take = [&out](std::string_view l) { out.emplace_back(l); };
    for (const std::string& c : chunks) {
        sp.feed(c, take);
    }
    if (finish) {
        sp.finish(take);
    }
    return out;
}

SourceRecord src(int epsg, std::string verdict, std::string group = "") {
    SourceRecord r;
    r.epsg = epsg;
    r.verdict = std::move(verdict);
    r.group = std::move(group);
    return r;
}

} // namespace

TEST(CliRecordsStdout, SourceAllFields) {
    const auto r = parseStdoutLine(
        "source\t{AAAA-1}\tlayer\tGoogle Карта\timage/jpeg\t3395\t7\tprojection\tg.zmp\tСпутник");
    ASSERT_TRUE(r);
    const auto& s = std::get<SourceRecord>(*r);
    EXPECT_EQ(s.guid, "{AAAA-1}");
    EXPECT_EQ(s.kind, "layer");
    EXPECT_TRUE(s.isLayer());
    EXPECT_EQ(s.name, "Google Карта");
    EXPECT_EQ(s.contentType, "image/jpeg");
    EXPECT_EQ(s.epsg, 3395);
    EXPECT_EQ(s.cacheType, 7);
    EXPECT_EQ(s.verdict, "projection");
    EXPECT_EQ(s.zmpFile, "g.zmp");
    EXPECT_EQ(s.group, "Спутник");
}

TEST(CliRecordsStdout, SourceEmptyGroupAndExtraFields) {
    const auto r = parseStdoutLine("source\t{B}\tmap\tN\tt\t3857\t1\tok\tz.zmp\t");
    ASSERT_TRUE(r);
    EXPECT_EQ(std::get<SourceRecord>(*r).group, "");
    EXPECT_FALSE(std::get<SourceRecord>(*r).isLayer());
    const auto e = parseStdoutLine("source\t{B}\tmap\tN\tt\t3857\t1\tok\tz.zmp\tG\tfuture\tmore");
    ASSERT_TRUE(e);
    EXPECT_EQ(std::get<SourceRecord>(*e).group, "G");
}

// and 7: the captured `sources` answers of the pinned CLI. Bundled data/sources after the
// data edit: the flag is 1 for exactly the two EMODnet Bathymetry GUIDs and 0 for the other 14 rows. A
// sailor-folder definition with NavigationWarning=1 gets the flag too; the plain bundled one does not.
TEST(Records, NavigationWarningOfThePinnedReleaseSources) {
    const auto flags = [](const char* file) {
        std::ifstream in(std::string(CG_FIXTURES_DIR) + "/cli-2026.10.6/" + file, std::ios::binary);
        EXPECT_TRUE(in.good()) << file;
        std::map<std::string, bool> out;
        for (std::string line; std::getline(in, line);) {
            const auto r = parseStdoutLine(line);
            EXPECT_TRUE(r && std::holds_alternative<SourceRecord>(*r)) << line;
            if (r)
                out[std::get<SourceRecord>(*r).guid] = std::get<SourceRecord>(*r).navigationWarning;
        }
        return out;
    };
    const auto all = flags("sources-bundled-all.stdout");
    ASSERT_EQ(all.size(), 16u);
    for (const auto& [guid, flag] : all)
        EXPECT_EQ(flag, guid == "{C44877A0-D455-4238-B5E2-C8BFF25FC0D4}" ||
                            guid == "{91D4F1B4-A7CD-4FC8-95F1-637609068FB2}")
            << guid;
    const auto sailor = flags("sources-navigation-warning.stdout");
    ASSERT_EQ(sailor.size(), 2u);
    EXPECT_TRUE(sailor.at("{D0C0FFEE-0000-4000-8000-000000000078}"));
    EXPECT_FALSE(sailor.at("{51C1079B-B55D-4DF7-A410-ADC118F53AB1}"));
}

// Hand-made from core weather-sources.md 5.4 (the CLI cannot produce weather records yet; protocol rule 9).
namespace {
std::string srcLine(const std::string& tail) {
    return "source\t{G}\tmap\tRain radar\timage/png\t3857\t0\tok\tz.zmp\tWeather" + tail;
}
} // namespace

TEST(CliRecordsWeather, SourceFieldsTenToFourteen) {
    const auto rec = [](const std::string& tail) {
        const auto r = parseStdoutLine(srcLine(tail));
        EXPECT_TRUE(r) << tail;
        return std::get<SourceRecord>(*r);
    };
    const SourceRecord s10 = rec("");
    EXPECT_EQ(s10.name, "Rain radar");
    EXPECT_EQ(s10.group, "Weather");
    EXPECT_TRUE(s10.refresh.empty());
    EXPECT_TRUE(s10.license.empty());
    EXPECT_FALSE(s10.maxZoom);
    EXPECT_FALSE(s10.isWeather());
    EXPECT_EQ(rec("\t").refresh, ""); // 11 fields: origin only
    const SourceRecord s12 = rec("\to\tsnapshot");
    EXPECT_EQ(s12.refresh, "snapshot");
    EXPECT_TRUE(s12.isWeather());
    EXPECT_TRUE(s12.license.empty());
    EXPECT_FALSE(s12.maxZoom);
    const SourceRecord s13 = rec("\to\tsnapshot\tDonn\xC3\xA9"
                                 "es \xC2\xA9");
    EXPECT_EQ(s13.license, "Donn\xC3\xA9"
                           "es \xC2\xA9");
    EXPECT_FALSE(s13.maxZoom);
    const SourceRecord s14 = rec("\to\tsnapshot\tCredit\t7");
    EXPECT_EQ(s14.license, "Credit");
    ASSERT_TRUE(s14.maxZoom);
    EXPECT_EQ(*s14.maxZoom, 7);
}

// navigationWarning is field 14, the last of 15 columns; only "1" sets it.
TEST(CliRecordsWeather, NavigationWarningIsTheLastOfFifteenColumnsAndOnlyOneSetsIt) {
    const auto flag = [](const std::string& tail) {
        const auto r = parseStdoutLine(srcLine(tail));
        EXPECT_TRUE(r) << tail;
        return std::get<SourceRecord>(*r).navigationWarning;
    };
    const std::string head = "\tuser\tstatic\tCredit\t7"; // fields 10-13
    EXPECT_TRUE(flag(head + "\t1"));
    for (const char* no : {"0", "x", "", "true", "11", " 1"})
        EXPECT_FALSE(flag(head + "\t" + no)) << no;
    EXPECT_FALSE(flag(head)) << "14 columns: maxZoom is the last, a '1' there is not the flag";
    EXPECT_FALSE(flag("\tuser\tstatic\tCredit\t1"));
    EXPECT_FALSE(flag(""));
}

TEST(CliRecordsWeather, MaxZoomRangeAndIsWeatherExact) {
    for (const auto& [text, want] :
         std::vector<std::pair<std::string, int>>{{"7", 7}, {"0", 0}, {"23", 23}}) {
        const auto r = parseStdoutLine(srcLine("\to\tsnapshot\tc\t" + text));
        ASSERT_TRUE(r);
        ASSERT_TRUE(std::get<SourceRecord>(*r).maxZoom) << text;
        EXPECT_EQ(*std::get<SourceRecord>(*r).maxZoom, want);
    }
    for (const char* bad : {"24", "-1", "x", "7.0", ""}) {
        const auto r = parseStdoutLine(srcLine(std::string("\to\tsnapshot\tc\t") + bad));
        ASSERT_TRUE(r) << bad;
        EXPECT_FALSE(std::get<SourceRecord>(*r).maxZoom) << bad;
    }
    for (const char* v : {"Snapshot", "static", "snapshot ", ""}) {
        const auto r = parseStdoutLine(srcLine(std::string("\to\t") + v));
        ASSERT_TRUE(r);
        EXPECT_FALSE(std::get<SourceRecord>(*r).isWeather()) << v;
    }
}

TEST(CliRecordsWeather, SnapshotRecord) {
    const auto r = parseStdoutLine("snapshot\t{G}\t2026-10-15T20:10:00Z\t/v2/radar/7b6bac246650");
    ASSERT_TRUE(r);
    const auto& s = std::get<SnapshotRecord>(*r);
    EXPECT_EQ(s.guid, "{G}");
    EXPECT_EQ(s.time, "2026-10-15T20:10:00Z");
    EXPECT_EQ(s.version, "/v2/radar/7b6bac246650");
    EXPECT_FALSE(parseStdoutLine("snapshot\t{G}\t2026-10-15T20:10:00Z"));
    const auto e = parseStdoutLine("snapshot\t{G}\t\tv1");
    ASSERT_TRUE(e);
    EXPECT_EQ(std::get<SnapshotRecord>(*e).time, "");
}

TEST(CliRecordsWeather, WrittenField5IsTheSnapshot) {
    const auto w = [](const std::string& line) { return std::get<WrittenRecord>(*parseStdoutLine(line)); };
    EXPECT_EQ(w("written\t{G}\t/a.mbtiles\t3\t4").snapshot, "");
    EXPECT_EQ(w("written\t{G}\t/a.mbtiles\t3\t4\t2026-10-15T20:10:00Z").snapshot, "2026-10-15T20:10:00Z");
    EXPECT_EQ(w("written\t{G}\t/a.mbtiles\t3\t4\t").snapshot, "");
    EXPECT_EQ(w("written\t{G}\t/a.mbtiles\t3\t4").bytes, 4);
}

TEST(CliRecordsWeather, SnapshotEpoch) {
    EXPECT_EQ(snapshotEpoch("1970-01-01T00:00:00Z"), 0);
    EXPECT_EQ(snapshotEpoch("2026-10-15T20:10:00Z"), 1792095000);
    EXPECT_EQ(snapshotEpoch("2028-02-29"), 1835395200);
    EXPECT_EQ(snapshotEpoch("2000-02-29"), 951782400);
    for (const char* bad : {"2026-02-29", "2026-04-31", "2100-02-29", "2026-13-01", "2026-00-10",
                            "2026-10-00", "2026-10-15T24:00:00Z", "2026-10-15T20:10:00",
                            "2026-10-15T20:10:00+02:00", "2026-10-15T20:10:00.000Z", " 2026-10-15", ""}) {
        EXPECT_FALSE(snapshotEpoch(bad)) << bad;
    }
}

TEST(CliRecordsWeather, SnapshotTimeText) {
    EXPECT_EQ(snapshotTimeText("2026-10-15T20:10:00Z"), "2026-10-15 20:10 UTC");
    EXPECT_EQ(snapshotTimeText("2026-10-15"), "2026-10-15");
    EXPECT_EQ(snapshotTimeText(""), "");
    EXPECT_EQ(snapshotTimeText("garbage"), "garbage");
    EXPECT_EQ(snapshotTimeText("2026-13-45T20:10:00Z"), "2026-13-45T20:10:00Z"); // 20 chars, not a time
}

TEST(CliRecordsWeather, ZoomLimit) {
    SourceRecord w;
    w.refresh = "snapshot";
    EXPECT_EQ(zoomLimit(w), 9);
    w.maxZoom = 7;
    EXPECT_EQ(zoomLimit(w), 7);
    w.maxZoom = 12;
    EXPECT_EQ(zoomLimit(w), 9);
    SourceRecord s;
    s.maxZoom = 12;
    EXPECT_EQ(zoomLimit(s), 12);
    s.maxZoom.reset();
    EXPECT_FALSE(zoomLimit(s));
}

TEST(CliRecordsWeather, IsSnapshotFailure) {
    EXPECT_TRUE(isSnapshotFailure("cannot take the snapshot of \"Rain radar\": no answer from https://x"));
    EXPECT_FALSE(isSnapshotFailure("no progress for 60 s: ..."));
    EXPECT_FALSE(isSnapshotFailure(""));
    EXPECT_FALSE(isSnapshotFailure("cannot take the snapshot"));
    EXPECT_FALSE(isSnapshotFailure(" cannot take the snapshot of"));
}

TEST(CliRecordsStdout, EstimateAllFields) {
    const auto r = parseStdoutLine("estimate\t{G}\t12\t900\t1000\t0.2500\t0.0500\t123456\t1\t987654");
    ASSERT_TRUE(r);
    const auto& e = std::get<EstimateRecord>(*r);
    EXPECT_EQ(e.guid, "{G}");
    EXPECT_EQ(e.zoom, 12);
    EXPECT_EQ(e.tiles, 900);
    EXPECT_EQ(e.bboxTiles, 1000);
    EXPECT_DOUBLE_EQ(e.sharePresent, 0.25);
    EXPECT_DOUBLE_EQ(e.shareMarkedMissing, 0.05);
    EXPECT_EQ(e.bytesToDownload, 123456);
    EXPECT_TRUE(e.assumed);
    EXPECT_EQ(e.bytesOnDisk, 987654);
    const auto f = parseStdoutLine("estimate\t{G}\t0\t1\t2\t0.0000\t1.0000\t3\t0\t4\textra");
    ASSERT_TRUE(f);
    EXPECT_FALSE(std::get<EstimateRecord>(*f).assumed);
    EXPECT_EQ(std::get<EstimateRecord>(*f).bytesOnDisk, 4);
}

TEST(CliRecordsStdout, FillSessionWritten) {
    const auto f = parseStdoutLine("fill\t{G}\t14\t100\t60\t10\t30\t5000000");
    ASSERT_TRUE(f);
    const auto& fr = std::get<FillRecord>(*f);
    EXPECT_EQ(fr.guid, "{G}");
    EXPECT_EQ(fr.zoom, 14);
    EXPECT_EQ(fr.tiles, 100);
    EXPECT_EQ(fr.present, 60);
    EXPECT_EQ(fr.markedMissing, 10);
    EXPECT_EQ(fr.missing, 30);
    EXPECT_EQ(fr.bytes, 5000000);

    const auto s = parseStdoutLine("session\t{G}\t2026-10-15 05:17:55\t/home/u/My Maps/o'x/a.sls");
    ASSERT_TRUE(s);
    const auto& sr = std::get<SessionRecord>(*s);
    EXPECT_EQ(sr.guid, "{G}");
    EXPECT_EQ(sr.time, "2026-10-15 05:17:55");
    EXPECT_EQ(sr.file, "/home/u/My Maps/o'x/a.sls");

    const auto w = parseStdoutLine("written\t{G}\t/home/u/Charts/Карта 1.mbtiles\t321\t7654321\tx");
    ASSERT_TRUE(w);
    const auto& wr = std::get<WrittenRecord>(*w);
    EXPECT_EQ(wr.guid, "{G}");
    EXPECT_EQ(wr.path, "/home/u/Charts/Карта 1.mbtiles");
    EXPECT_EQ(wr.tiles, 321);
    EXPECT_EQ(wr.bytes, 7654321);
}

TEST(CliRecordsStdout, BadLinesAreNullopt) {
    const std::vector<std::string> bad = {
        // one field too short, per record type
        "source\t{G}\tmap\tN\tt\t3857\t1\tok\tz.zmp",
        "estimate\t{G}\t12\t900\t1000\t0.25\t0.05\t123456\t1",
        "fill\t{G}\t14\t100\t60\t10\t30",
        "session\t{G}\t2026-10-15 05:17:55",
        "written\t{G}\t/p\t321",
        // non-numbers
        "source\t{G}\tmap\tN\tt\tabc\t1\tok\tz.zmp\tg",
        "source\t{G}\tmap\tN\tt\t3857\t1x\tok\tz.zmp\tg",
        "estimate\t{G}\t1x\t900\t1000\t0.25\t0.05\t1\t1\t9",
        "estimate\t{G}\t12\t900\t1000\tx\t0.05\t1\t1\t9",
        "estimate\t{G}\t12\t900\t1000\tnan\t0.05\t1\t1\t9",
        "estimate\t{G}\t12\t900\t1000\t0.25\tinf\t1\t1\t9",
        "estimate\t{G}\t12\t900\t1000\t0.25\t0.05\t1\t1\t",
        "fill\t{G}\t14\t100\t60\t10\tzz\t5",
        "written\t{G}\t/p\t3.5\t7",
        // booleans other than 0 / 1
        "estimate\t{G}\t12\t900\t1000\t0.25\t0.05\t1\t2\t9",
        "estimate\t{G}\t12\t900\t1000\t0.25\t0.05\t1\ttrue\t9",
        "estimate\t{G}\t12\t900\t1000\t0.25\t0.05\t1\t\t9",
        // unknown type, empty line
        "progressive\t1",
        "",
    };
    for (const std::string& l : bad) {
        EXPECT_FALSE(parseStdoutLine(l)) << l;
    }
}

TEST(CliRecordsStderr, ProgressAndPhase) {
    const auto d = parseStderrLine("progress\tdownload\t{G}\t13\t50\t200\t45\t1234567\t30");
    const auto& dp = std::get<DownloadProgress>(d);
    EXPECT_EQ(dp.guid, "{G}");
    EXPECT_EQ(dp.zoom, 13);
    EXPECT_EQ(dp.processed, 50);
    EXPECT_EQ(dp.total, 200);
    EXPECT_EQ(dp.downloaded, 45);
    EXPECT_EQ(dp.downloadedBytes, 1234567);
    EXPECT_EQ(dp.elapsedSec, 30);
    EXPECT_TRUE(std::holds_alternative<DownloadProgress>(
        parseStderrLine("progress\tdownload\t{G}\t13\t50\t200\t45\t1234567\t30\tfuture")));

    const auto e = parseStderrLine("progress\texport\t{G}\t750");
    EXPECT_EQ(std::get<ExportProgress>(e).guid, "{G}");
    EXPECT_EQ(std::get<ExportProgress>(e).permille, 750);

    const auto p = parseStderrLine("phase\tfill\t{G}");
    EXPECT_EQ(std::get<PhaseRecord>(p).name, "fill");
    EXPECT_EQ(std::get<PhaseRecord>(p).guid, "{G}");
}

// The `note:` line of the first start (captured, 2026.10.6) is a message of kind Other with
// its whole text; the line next to it is a Warning and no line of the capture is an Error (lastError stays
// empty).
TEST(CliRecordsStderr, TheFirstStartNoteIsOtherAndNeverAnError) {
    const auto lines = fixtureLines("sources-first-start-note.stderr");
    ASSERT_EQ(lines.size(), 2u);
    const auto note = std::get<MessageLine>(parseStderrLine(lines[0]));
    EXPECT_EQ(note.kind, MessageLine::Kind::Other);
    EXPECT_EQ(note.text, lines[0]);
    EXPECT_EQ(note.text.rfind("note: moved $SCRATCH/home-note/.config/SAS.Planet to ", 0), 0u);
    EXPECT_EQ(std::get<MessageLine>(parseStderrLine(lines[1])).kind, MessageLine::Kind::Warning);
    int stdoutRecords = 0;
    for (const std::string& l : fixtureLines("sources-first-start-note.stdout")) {
        const auto r = parseStdoutLine(l);
        ASSERT_TRUE(r && std::holds_alternative<SourceRecord>(*r)) << l;
        ++stdoutRecords;
    }
    EXPECT_EQ(stdoutRecords, 3);
}

// The three other new stop cases of 2026.10.6 are the real output, read as the plugin reads
// it: the exit code and the one Error message line (the connection-lost run: its Error line is followed by
// more lines).
TEST(CliRecordsStderr, TheNewStopCasesOfThePinnedRelease) {
    struct Row {
        const char* name;
        const char* exitCode;
        const char* error;
    };
    const Row rows[] = {
        {"estimate-cache-refused", "3",
         "\"OpenTopoMap\": cannot write the cache folder $SCRATCH/a-file/cache_sqlite/Osm_Topo_Map/"},
        {"estimate-area-too-large", "3",
         "the area is too large at zoom 16 (0-based): more than 2147483647 tiles; choose a smaller area or "
         "lower "
         "zooms"},
        // the CLI's line ends in "\r\n" (the curl text): parseStderrLine trims the "\r"
        {"run-connection-lost", "4",
         "no connection to the server for 3 tiles in a row: Error connecting to server"},
    };
    for (const Row& r : rows) {
        SCOPED_TRACE(r.name);
        EXPECT_EQ(fixtureLines(std::string(r.name) + ".exit"), (std::vector<std::string>{r.exitCode}));
        std::vector<std::string> errors;
        for (const std::string& l : fixtureLines(std::string(r.name) + ".stderr")) {
            const StderrLine parsed = parseStderrLine(l);
            const auto* m = std::get_if<MessageLine>(&parsed);
            if (m && m->kind == MessageLine::Kind::Error)
                errors.push_back(m->text);
        }
        EXPECT_EQ(errors, (std::vector<std::string>{r.error}));
    }
}

TEST(CliRecordsStderr, MessagesAndBadProtocolLinesAreOther) {
    const auto err = std::get<MessageLine>(parseStderrLine("error: no progress for 300 s: Ошибка"));
    EXPECT_EQ(err.kind, MessageLine::Kind::Error);
    EXPECT_EQ(err.text, "no progress for 300 s: Ошибка");
    const auto warn = std::get<MessageLine>(parseStderrLine("warning: continuing the session"));
    EXPECT_EQ(warn.kind, MessageLine::Kind::Warning);
    EXPECT_EQ(warn.text, "continuing the session");

    const std::vector<std::string> other = {
        "progressive",
        "phases\tfill\t{G}",
        "phase\tfill",                                       // one field short
        "progress\tdownload\t{G}\t13\t50\t200\t45\t1234567", // one field short
        "progress\tdownload\t{G}\t13\tx\t200\t45\t1234567\t30",
        "progress\texport\t{G}",
        "progress\texport\t{G}\tabc",
        "progress\tother\t{G}\t1",
        "something else",
    };
    for (const std::string& l : other) {
        const auto m = parseStderrLine(l);
        ASSERT_TRUE(std::holds_alternative<MessageLine>(m)) << l;
        EXPECT_EQ(std::get<MessageLine>(m).kind, MessageLine::Kind::Other) << l;
        EXPECT_EQ(std::get<MessageLine>(m).text, l);
    }
}

// every stderr line of the captured CLI 2026.10.6 runs is a record or a message; a line
// starting "phase<TAB>" / "progress<TAB>" is never "other", the `warning:` lines are warnings, and the
// numbers of a record are the fields of the line (the first progress record of the stopped run, literal from
// the capture).
TEST(CliRecordsStderr, EveryLineOfTheCaptured2026Dot10RunsIsARecordOrAMessage) {
    int phases = 0, progresses = 0, exports = 0, warnings = 0;
    for (const char* name : {"run-stopped.stderr", "run-continued.stderr"}) {
        const auto lines = fixtureLines(name);
        ASSERT_GT(lines.size(), 20u) << name;
        for (const std::string& l : lines) {
            const StderrLine r = parseStderrLine(l);
            if (l.rfind("phase\t", 0) == 0) {
                ++phases;
                EXPECT_TRUE(std::holds_alternative<PhaseRecord>(r)) << l;
            } else if (l.rfind("progress\tdownload\t", 0) == 0) {
                ++progresses;
                EXPECT_TRUE(std::holds_alternative<DownloadProgress>(r)) << l;
            } else if (l.rfind("progress\texport\t", 0) == 0) {
                ++exports;
                EXPECT_TRUE(std::holds_alternative<ExportProgress>(r)) << l;
            } else {
                ASSERT_TRUE(l.rfind("warning: ", 0) == 0) << l;
                ++warnings;
                const auto& m = std::get<MessageLine>(r);
                EXPECT_EQ(m.kind, MessageLine::Kind::Warning) << l;
                EXPECT_EQ(m.text, l.substr(9));
            }
        }
    }
    EXPECT_GT(phases, 10);
    EXPECT_GT(progresses, 50);
    EXPECT_EQ(exports, 2);
    EXPECT_EQ(warnings, 5); // per run: the Esri terms-of-use warning; the stopped run: 2 (session); the
                            // continued: 1 (continuing)

    const auto first = std::get<DownloadProgress>(parseStderrLine(fixtureLines("run-stopped.stderr")[4]));
    EXPECT_EQ(first.guid, "{7B743985-BC5F-4AB6-8915-AC5DBBB8F552}");
    EXPECT_EQ(first.zoom, 9);
    EXPECT_EQ(first.processed, 0);
    EXPECT_EQ(first.total, 243); // 2026.10.4 printed 0 here (no total yet); 2026.10.6 prints it at once
    EXPECT_EQ(first.downloadedBytes, 0);
    const auto second = std::get<DownloadProgress>(parseStderrLine(fixtureLines("run-stopped.stderr")[5]));
    EXPECT_EQ(second.zoom, 10);
    EXPECT_EQ(second.processed, 9);
    EXPECT_EQ(second.downloaded, 9);
    EXPECT_EQ(second.downloadedBytes, 127639);
    EXPECT_EQ(second.elapsedSec, 1);
    const auto ex = std::get<ExportProgress>(parseStderrLine("progress\texport\t{G}\t1000"));
    EXPECT_EQ(ex.permille, 1000);
}

// / 5 / 6 : the hand-made streams parse in order and to the right
// kinds.
TEST(CliRecordsStderr, AResumeShapedStreamParsesInOrderAndKeepsTheFiguresOfTheRecord) {
    const auto lines = fixtureLines("resume-refused-b.stderr");
    ASSERT_EQ(lines.size(), 4u);
    const std::string a = "{7B743985-BC5F-4AB6-8915-AC5DBBB8F552}",
                      b = "{AB8A8685-AEC6-4E16-80DA-DC4AC37E4777}";
    const StderrLine l0 = parseStderrLine(lines[0]), l1 = parseStderrLine(lines[1]),
                     l2 = parseStderrLine(lines[2]), l3 = parseStderrLine(lines[3]);
    EXPECT_EQ(std::get<PhaseRecord>(l0).name, "download");
    EXPECT_EQ(std::get<PhaseRecord>(l0).guid, a);
    EXPECT_EQ(std::get<PhaseRecord>(l1).name, "download");
    EXPECT_EQ(std::get<PhaseRecord>(l1).guid, b);
    const auto& err = std::get<MessageLine>(l2);
    EXPECT_EQ(err.kind, MessageLine::Kind::Error);
    EXPECT_EQ(err.text, "cannot continue $SCRATCH/data/AutoSave/25574d75ff1b/" + b +
                            "/final.sls: the session does not match the job");
    const auto& p = std::get<DownloadProgress>(l3);
    EXPECT_EQ(p.guid, a);
    EXPECT_EQ(p.zoom, 11);
    EXPECT_EQ(p.processed, 19);
    EXPECT_EQ(p.total, 243);
    EXPECT_EQ(p.downloaded, 19);
    EXPECT_EQ(p.downloadedBytes, 291609);
    EXPECT_EQ(p.elapsedSec, 1);
}

TEST(CliRecordsStderr, ARunPhaseWithoutProgressAndAnEmptyGuidAreRecordsNotOther) {
    const auto none = fixtureLines("run-phase-without-progress.stderr");
    ASSERT_EQ(none.size(), 3u);
    EXPECT_EQ(std::get<PhaseRecord>(parseStderrLine(none[0])).name, "download");
    EXPECT_EQ(std::get<PhaseRecord>(parseStderrLine(none[1])).name, "export");
    EXPECT_EQ(std::get<ExportProgress>(parseStderrLine(none[2])).permille, 1000);

    const auto empty = fixtureLines("progress-empty-guid.stderr");
    ASSERT_EQ(empty.size(), 3u);
    const auto p = std::get<DownloadProgress>(parseStderrLine(empty[1]));
    EXPECT_EQ(p.guid, "");
    EXPECT_EQ(p.zoom, 3);
    EXPECT_EQ(p.processed, 5);
    EXPECT_EQ(p.total, 243);
    EXPECT_EQ(p.downloadedBytes, 60000);
    EXPECT_EQ(std::get<DownloadProgress>(parseStderrLine(empty[2])).guid,
              "{7B743985-BC5F-4AB6-8915-AC5DBBB8F552}");
    // the same shape typed by hand, literal fields of the ticket
    EXPECT_EQ(
        std::get<DownloadProgress>(parseStderrLine("progress\tdownload\t\t3\t5\t243\t5\t60000\t1")).zoom, 3);
}

TEST(LineSplitter, SplitAtEveryBytePositionGivesSameLines) {
    const std::string whole = kSource + "estimate\t{G}\t12\t900\t1000\t0.25\t0.05\t1\t1\t9\n" +
                              "written\t{G}\t/Карта/ё.mbtiles\t3\t7\n";
    const std::vector<std::string> expected = {
        "source\t{AAAA-1}\tmap\tOSM Mapnik\timage/png\t3857\t4\tok\tosm.zmp\tOpen maps",
        "estimate\t{G}\t12\t900\t1000\t0.25\t0.05\t1\t1\t9", "written\t{G}\t/Карта/ё.mbtiles\t3\t7"};
    EXPECT_EQ(splitAll({whole}), expected);
    for (std::size_t i = 0; i <= whole.size(); ++i) {
        EXPECT_EQ(splitAll({whole.substr(0, i), whole.substr(i)}), expected) << "split at " << i;
    }
    std::vector<std::string> bytes;
    for (char c : whole) {
        bytes.emplace_back(1, c);
    }
    EXPECT_EQ(splitAll(bytes), expected);
}

TEST(LineSplitter, CrlfAndFinish) {
    EXPECT_EQ(splitAll({"a\r\nb\r\n"}), (std::vector<std::string>{"a", "b"}));
    EXPECT_EQ(splitAll({"a\r", "\nb"}), (std::vector<std::string>{"a", "b"}));
    EXPECT_EQ(splitAll({"a\nlast"}), (std::vector<std::string>{"a", "last"}));
    EXPECT_EQ(splitAll({"a\nlast"}, false), (std::vector<std::string>{"a"}));
    EXPECT_EQ(splitAll({"a\n"}), (std::vector<std::string>{"a"}));
    EXPECT_TRUE(splitAll({}).empty());
}

TEST(Sums, EstimatesOverGuidsAndZooms) {
    const auto rec = [](const char* g, int z, std::int64_t tiles, std::int64_t dl, bool assumed,
                        std::int64_t disk) {
        EstimateRecord r;
        r.guid = g;
        r.zoom = z;
        r.tiles = tiles;
        r.bytesToDownload = dl;
        r.assumed = assumed;
        r.bytesOnDisk = disk;
        return r;
    };
    const std::vector<EstimateRecord> v = {rec("{A}", 10, 100, 1000, false, 5000),
                                           rec("{B}", 11, 20, 30, false, 700),
                                           rec("{A}", 12, 3, 4, false, 80)};
    auto t = sumEstimates(v);
    EXPECT_EQ(t.tiles, 123);
    EXPECT_EQ(t.bytesToDownload, 1034);
    EXPECT_EQ(t.bytesOnDisk, 5780);
    EXPECT_FALSE(t.assumed);
    auto w = v;
    w[1].assumed = true;
    EXPECT_TRUE(sumEstimates(w).assumed);
    EXPECT_EQ(sumEstimates({}).tiles, 0);
}

TEST(Sums, FillCountsOnlyTheGuid) {
    const auto rec = [](const char* g, std::int64_t tiles, std::int64_t missing, std::int64_t bytes) {
        FillRecord r;
        r.guid = g;
        r.tiles = tiles;
        r.missing = missing;
        r.bytes = bytes;
        return r;
    };
    const std::vector<FillRecord> v = {rec("{A}", 100, 10, 1000), rec("{B}", 7, 5, 9),
                                       rec("{A}", 50, 2, 300)};
    const auto a = sumFill(v, "{A}");
    EXPECT_EQ(a.tiles, 150);
    EXPECT_EQ(a.missing, 12);
    EXPECT_EQ(a.bytes, 1300);
    const auto b = sumFill(v, "{B}");
    EXPECT_EQ(b.tiles, 7);
    EXPECT_EQ(b.missing, 5);
    EXPECT_EQ(b.bytes, 9);
    EXPECT_EQ(sumFill(v, "{C}").tiles, 0);
}

TEST(Sums, SecondsLeft) {
    DownloadProgress p;
    p.total = 200;
    p.elapsedSec = 30;
    p.processed = 0;
    EXPECT_FALSE(secondsLeft(p));
    p.processed = 50; // 30 * 150 / 50
    EXPECT_EQ(secondsLeft(p), 90);
    p.processed = 70; // 30 * 130 / 70 = 55.7 -> integer division
    EXPECT_EQ(secondsLeft(p), 55);
    p.processed = 200;
    EXPECT_EQ(secondsLeft(p), 0);
    p.processed = 250; // processed > total: clamped, never negative
    EXPECT_EQ(secondsLeft(p), 0);
}

TEST(Exit, EveryRowOfTheOutcomeTable) {
    struct Row {
        int code;
        const char* lastError;
        bool startFailed;
        Outcome want;
    };
    const Row rows[] = {
        {0, "", false, Outcome::Done},
        {0, "error: stale", false, Outcome::Done},
        {1, "", false, Outcome::InternalError},
        {2, "", false, Outcome::UsageError},
        {3, "", false, Outcome::Refused},
        {4, "no progress for 300 s: tile server silent", false, Outcome::Stalled},
        {4, "no connection to the server for 3 tiles in a row: Error connecting to server", false,
         Outcome::Stalled},
        {4, "cannot take the snapshot of x: y", false, Outcome::DownloadIncomplete},
        {4, "cannot store tiles: disk full", false, Outcome::StorageError},
        {4, "some other failure", false, Outcome::DownloadIncomplete},
        {4, "", false, Outcome::DownloadIncomplete},
        {5, "", false, Outcome::ExportFailed},
        {130, "", false, Outcome::Interrupted},
        {143, "", false, Outcome::Interrupted},
        {-9, "", false, Outcome::Killed},
        {-15, "", false, Outcome::Killed},
        {77, "", false, Outcome::InternalError},
        {0, "", true, Outcome::StartFailed},
        {-1, "", true, Outcome::StartFailed},
    };
    for (const Row& r : rows) {
        EXPECT_EQ(classifyExit(r.code, r.lastError, r.startFailed), r.want)
            << r.code << " '" << r.lastError << "' " << r.startFailed;
    }
    // The texts matter only for exit 4.
    EXPECT_EQ(classifyExit(3, "no progress for 300 s: x", false), Outcome::Refused);
    EXPECT_EQ(classifyExit(3, "no connection to the server for 3 tiles in a row: x", false),
              Outcome::Refused);
}

TEST(Exit, ResumableExactlyFive) {
    const Outcome all[] = {
        Outcome::Done,        Outcome::InternalError, Outcome::UsageError,         Outcome::Refused,
        Outcome::Stalled,     Outcome::StorageError,  Outcome::DownloadIncomplete, Outcome::ExportFailed,
        Outcome::Interrupted, Outcome::Killed,        Outcome::StartFailed};
    for (Outcome o : all) {
        const bool want = o == Outcome::Stalled || o == Outcome::StorageError ||
                          o == Outcome::DownloadIncomplete || o == Outcome::Interrupted ||
                          o == Outcome::Killed;
        EXPECT_EQ(resumable(o), want) << static_cast<int>(o);
    }
}

namespace {
constexpr std::uint64_t kRes = 536870912; // 512 MiB
constexpr std::uint64_t kHuge = 1ull << 40;

PreflightInput pre(std::int64_t tiles, std::int64_t disk, std::int64_t dl) {
    PreflightInput in;
    in.totals.tiles = tiles;
    in.totals.bytesOnDisk = disk;
    in.totals.bytesToDownload = dl;
    in.freeChartBytes = kHuge;
    in.freeCacheBytes = kHuge;
    return in;
}
// the cost of one more level from the captured `estimate` records (run-continued.stdout, base
// + seamarks, zooms 9-13). Literals: finest = sum of field 7 at the zoom; next = 4 x sum of field 9 at the
// zoom.
TEST(LevelCostTest, FromTheCapturedEstimateRecords) {
    std::vector<EstimateRecord> records;
    for (const std::string& line : fixtureLines("run-continued.stdout"))
        if (line.rfind("estimate\t", 0) == 0) {
            const auto r = parseStdoutLine(line);
            ASSERT_TRUE(r && std::holds_alternative<EstimateRecord>(*r)) << line;
            records.push_back(std::get<EstimateRecord>(*r));
        }
    ASSERT_EQ(records.size(), 10u);
    const auto c13 = levelCost(records, 13);
    ASSERT_TRUE(c13);
    EXPECT_EQ(c13->finestBytes, 360766);
    EXPECT_EQ(c13->totalBytes, 360766);
    EXPECT_EQ(c13->nextBytes, 8700092);
    const auto c12 = levelCost(records, 12);
    ASSERT_TRUE(c12);
    EXPECT_EQ(c12->finestBytes, 0);
    EXPECT_EQ(c12->nextBytes, 2697840);
    EXPECT_FALSE(levelCost(records, 14)); // no record at 14
    EXPECT_FALSE(levelCost(records, 8));
    std::vector<EstimateRecord> deep(1);
    deep[0].zoom = 21;
    deep[0].bytesOnDisk = 5;
    EXPECT_FALSE(levelCost(deep, 21)); // nothing finer than 21
    EXPECT_FALSE(levelCost({}, 10));
}

// mean bytes per tile = sum bytesOnDisk / sum tiles over the captured records
// (run-continued.stdout, 10 estimate records): bytesOnDisk 2673211 + 569649 = 3242860, tiles 243 + 243 = 486,
// 3242860 / 486 = 6672 (integer division; 6672.3). No usable record: kAssumedTileBytes = 20 x 1024.
TEST(MeanTileBytesTest, FromTheCapturedRecordsAndTheAssumedDefault) {
    std::vector<EstimateRecord> records;
    for (const std::string& line : fixtureLines("run-continued.stdout"))
        if (line.rfind("estimate\t", 0) == 0)
            records.push_back(std::get<EstimateRecord>(*parseStdoutLine(line)));
    ASSERT_EQ(records.size(), 10u);
    EXPECT_EQ(meanTileBytes(records), 6672);
    EXPECT_EQ(meanTileBytes({}), 20480);
    std::vector<EstimateRecord> empty(2); // tiles 0: their bytes count for nothing
    empty[0].bytesOnDisk = 999;
    empty[1].bytesOnDisk = 7;
    EXPECT_EQ(meanTileBytes(empty), 20480);
    empty.push_back(records[0]); // 4 tiles, 54540 bytes: the zero-tile records stay out of the sum
    EXPECT_EQ(meanTileBytes(empty), 13635);
}

} // namespace

TEST(Preflight, TooLargeThresholds) {
    EXPECT_EQ(checkPreflight(pre(200000, 1, 1)), Preflight::Ok);
    EXPECT_EQ(checkPreflight(pre(200001, 1, 1)), Preflight::TooLarge);
    EXPECT_EQ(checkPreflight(pre(1, 2'000'000'000, 1)), Preflight::Ok);
    EXPECT_EQ(checkPreflight(pre(1, 2'000'000'001, 1)), Preflight::TooLarge);
}

TEST(Preflight, ChartFolderNeedsTwiceBytesOnDiskPlusReserve) {
    auto in = pre(10, 100'000'000, 5);
    in.freeChartBytes = kRes + 200'000'000;
    EXPECT_EQ(checkPreflight(in), Preflight::Ok);
    in.freeChartBytes = kRes + 200'000'000 - 1;
    EXPECT_EQ(checkPreflight(in), Preflight::ChartDiskFull);
    in.freeChartBytes = 200'000'001; // without the reserve it would fit
    EXPECT_EQ(checkPreflight(in), Preflight::ChartDiskFull);
    in.freeChartBytes = 100; // below the reserve
    EXPECT_EQ(checkPreflight(in), Preflight::ChartDiskFull);
}

TEST(Preflight, CacheFolderNeedsBytesToDownloadPlusReserve) {
    auto in = pre(10, 1, 70'000'000);
    in.freeCacheBytes = kRes + 70'000'000;
    EXPECT_EQ(checkPreflight(in), Preflight::Ok);
    in.freeCacheBytes = kRes + 70'000'000 - 1;
    EXPECT_EQ(checkPreflight(in), Preflight::CacheDiskFull);
    in.freeCacheBytes = 70'000'001;
    EXPECT_EQ(checkPreflight(in), Preflight::CacheDiskFull);
}

TEST(Preflight, SeparateFileSystemsAreNotSummed) {
    auto in = pre(10, 100'000'000, 70'000'000);
    in.freeChartBytes = kRes + 200'000'000;
    in.freeCacheBytes = kRes + 70'000'000;
    EXPECT_EQ(checkPreflight(in), Preflight::Ok);
}

TEST(Preflight, SameFileSystemSumsAgainstOneFreeValue) {
    auto in = pre(10, 100'000'000, 70'000'000);
    in.sameFileSystem = true;
    in.freeCacheBytes = 0; // not used on one file system
    in.freeChartBytes = kRes + 270'000'000;
    EXPECT_EQ(checkPreflight(in), Preflight::Ok);
    in.freeChartBytes = kRes + 270'000'000 - 1;
    EXPECT_EQ(checkPreflight(in), Preflight::ChartDiskFull);
}

TEST(Preflight, BudgetOnlyInSlowMode) {
    Settings s;
    s.dataBudgetMb = 100;
    auto in = pre(10, 1, 100'000'001);
    in.settings = &s;
    EXPECT_EQ(checkPreflight(in), Preflight::Ok); // not slow
    s.slowConnection = true;
    EXPECT_EQ(checkPreflight(in), Preflight::OverBudget);
    in.totals.bytesToDownload = 100'000'000;
    EXPECT_EQ(checkPreflight(in), Preflight::Ok);
    s.dataBudgetMb = 30;
    EXPECT_EQ(checkPreflight(in), Preflight::OverBudget);
}

TEST(Preflight, PrecedenceTooLargeThenDiskThenBudget) {
    Settings s;
    s.slowConnection = true;
    s.dataBudgetMb = 1;
    // all failing: TooLarge wins
    auto in = pre(200001, 100'000'000, 70'000'000);
    in.settings = &s;
    in.freeChartBytes = 0;
    in.freeCacheBytes = 0;
    EXPECT_EQ(checkPreflight(in), Preflight::TooLarge);
    // disk before budget; chart disk before cache disk
    in.totals.tiles = 10;
    EXPECT_EQ(checkPreflight(in), Preflight::ChartDiskFull);
    in.freeChartBytes = kHuge;
    EXPECT_EQ(checkPreflight(in), Preflight::CacheDiskFull);
    in.freeCacheBytes = kHuge;
    EXPECT_EQ(checkPreflight(in), Preflight::OverBudget);
}

TEST(Retry, OnlyStalledNotSlowUnderThree) {
    Settings s;
    for (int a = 0; a < 3; ++a) {
        EXPECT_TRUE(shouldRetry(Outcome::Stalled, s, RetryState{a})) << a;
    }
    EXPECT_FALSE(shouldRetry(Outcome::Stalled, s, RetryState{3}));
    EXPECT_FALSE(shouldRetry(Outcome::Stalled, s, RetryState{4}));
    EXPECT_FALSE(shouldRetry(Outcome::StorageError, s, RetryState{0}));
    EXPECT_FALSE(shouldRetry(Outcome::Interrupted, s, RetryState{0}));
    s.slowConnection = true;
    EXPECT_FALSE(shouldRetry(Outcome::Stalled, s, RetryState{0}));
}

TEST(Sources, RefusalProjectionWinsOverVerdict) {
    auto a = sourceRefusal(src(3395, "ok"));
    EXPECT_EQ(a.kind, SourceRefusal::Kind::Projection);
    EXPECT_EQ(a.epsg, 3395);
    auto b = sourceRefusal(src(4326, "not a bitmap map"));
    EXPECT_EQ(b.kind, SourceRefusal::Kind::Projection);
    EXPECT_EQ(b.epsg, 4326);
    auto c = sourceRefusal(src(3857, "unknown content type: Ж"));
    EXPECT_EQ(c.kind, SourceRefusal::Kind::Verdict);
    EXPECT_EQ(c.verdict, "unknown content type: Ж");
    EXPECT_EQ(sourceRefusal(src(3857, "ok")).kind, SourceRefusal::Kind::None);
}

TEST(Sources, UsableOnlyWhen3857AndOk) {
    EXPECT_TRUE(src(3857, "ok").usable());
    EXPECT_FALSE(src(3395, "ok").usable());
    EXPECT_FALSE(src(4326, "not a bitmap map").usable());
    EXPECT_FALSE(src(3857, "unknown content type").usable());
}

TEST(Sources, DialogOrder) {
    const std::vector<SourceRecord> s = {
        src(3857, "bad", ""),           // 0
        src(3857, "ok", ""),            // 1
        src(3395, "ok", "Яндекс"),      // 2
        src(3857, "ok", "Google"),      // 3
        src(3857, "ok", "Яндекс"),      // 4
        src(3857, "refused", "Google"), // 5
        src(3857, "ok", "Google"),      // 6
        src(3857, "ok", ""),            // 7
        src(3857, "refused", "Яндекс"), // 8
    };
    EXPECT_EQ(sourcesDialogOrder(s), (std::vector<std::size_t>{4, 2, 8, 3, 6, 5, 1, 7, 0}));
    EXPECT_TRUE(sourcesDialogOrder({}).empty());
}

TEST(Sources, ImageryNames) {
    for (const char* n : {"Google Satellite", "ESRI Imagery", "Bing Aerial", "Yandex Sat", "Карты Sat",
                          "Sat maps", "ORTHO-photo", "Aerial", "My Photos"}) {
        EXPECT_TRUE(nameLooksLikeImagery(n)) << n;
    }
    for (const char* n :
         {"OSM Mapnik", "Saturn charts", "Navionics", "Карта", "Sa t", "", "КартаSat", "SatКарта"}) {
        EXPECT_FALSE(nameLooksLikeImagery(n)) << n;
    }
}
