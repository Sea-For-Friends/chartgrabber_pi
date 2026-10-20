// SPDX-License-Identifier: GPL-3.0-or-later
// Texts and number formats of src/wx/messages.cpp against literals. Pure functions; no window, no OpenCPN call.
#include "messages.h"
#include "wx_env.h"

#include <gtest/gtest.h>

#include <wx/string.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

std::string u(const wxString& s) {
    return s.utf8_string();
}

wxString w(const char* utf8) {
    return wxString::FromUTF8(utf8);
}

constexpr const char* kKarta = "\xD0\x9A\xD0\xB0\xD1\x80\xD1\x82\xD0\xB0"; // a Cyrillic word

TEST(MessagesNumbers, FormatCount) {
    const std::vector<std::pair<std::int64_t, std::string>> rows = {
        {0, "0"},        {7, "7"},        {950, "950"},           {999, "999"},
        {1000, "1 000"}, {1950, "1 950"}, {1300000, "1 300 000"}, {123456789, "123 456 789"}};
    for (const auto& [n, text] : rows)
        EXPECT_EQ(u(formatCount(n)), text) << n;
}

// the words of the My charts rows.
TEST(MessagesRows, ChartStateTextCallsInterruptedStopped) {
    cg::ChartRecord r;
    r.state = cg::ChartState::Interrupted;
    EXPECT_EQ(u(chartStateText(r)), "Stopped");
}

TEST(MessagesRows, FrozenRowText) {
    struct Row {
        cg::ChartState state;
        int done;
        std::int64_t tiles, missing;
        const char* text;
    };
    const std::vector<Row> rows = {
        {cg::ChartState::Interrupted, 630, 0, 0, "Stopped at 63 %"},
        {cg::ChartState::Interrupted, 1000, 0, 0, "Stopped at 99 %"},
        {cg::ChartState::Interrupted, 0, 0, 0, "Stopped"},
        {cg::ChartState::Failed, 280, 0, 0, "Failed at 28 %"},
        {cg::ChartState::Failed, 0, 0, 0, "Failed"},
        {cg::ChartState::Missing, 5, 14200, 312, "312 of 14 200 tiles missing"},
        {cg::ChartState::Complete, 500, 0, 0, "Complete"},
    };
    for (const Row& row : rows) {
        cg::ChartRecord r;
        r.state = row.state;
        r.done = row.done;
        r.tiles = row.tiles;
        r.missing = row.missing;
        EXPECT_EQ(u(frozenRowText(r)), row.text);
    }
}

TEST(MessagesRows, LiveRowText) {
    const auto live = [](cg::LiveStage stage, int permille, int done, std::optional<std::int64_t> left,
                         int retry) {
        cg::LiveProgress p;
        p.stage = stage;
        p.permille = permille;
        p.donePermille = done;
        p.secondsLeft = left;
        p.retrySeconds = retry;
        return u(liveRowText(p));
    };
    using S = cg::LiveStage;
    EXPECT_EQ(live(S::Sizing, -1, 0, std::nullopt, 0), "Working out the size...");
    EXPECT_EQ(live(S::Downloading, 633, 633, 720, 0), "63 %  about 12 min left");
    EXPECT_EQ(live(S::Downloading, 633, 633, std::nullopt, 0), "63 %  time left unknown");
    EXPECT_EQ(live(S::Checking, 633, 633, std::nullopt, 0), "Checking saved tiles...");
    EXPECT_EQ(live(S::Building, 805, 633, std::nullopt, 0), "Building the chart: 80 %");
    EXPECT_EQ(live(S::RetryWait, 633, 633, std::nullopt, 40), "No connection, trying again in 40 s");
    EXPECT_EQ(live(S::Stopping, 700, 633, 720, 0), "Stopping at 63 %");
    EXPECT_EQ(u(stoppingButtonText()), "Stopping...");
    EXPECT_EQ(u(oneJobAtATimeText()),
              "Chart Grabber downloads one chart at a time. The other actions return when it ends.");
}

TEST(MessagesRows, WeatherTexts) {
    const std::pair<std::int64_t, const char*> ages[] = {
        {-1, ""},          {0, "1 min old"},     {59, "1 min old"},      {3599, "59 min old"},
        {3600, "1 h old"}, {172799, "47 h old"}, {172800, "2 days old"}, {1000000, "11 days old"}};
    for (const auto& [seconds, text] : ages)
        EXPECT_EQ(u(snapshotAgeText(seconds)), text) << seconds;
    EXPECT_EQ(u(creditText("Weather data by Pr\xC3\xA9"
                           "visions")),
              "Credit: Weather data by Pr\xC3\xA9"
              "visions");
    EXPECT_EQ(u(updateWeatherMenuText()), "Update to latest weather");
}

TEST(MessagesNumbers, FormatBytes) {
    const std::vector<std::pair<std::int64_t, std::string>> rows = {
        {0, "0.0 MB"},       {9400000, "9.4 MB"},   {10000000, "10 MB"},    {38000000, "38 MB"},
        {38600000, "39 MB"}, {999000000, "999 MB"}, {1000000000, "1.0 GB"}, {9100000000, "9.1 GB"}};
    for (const auto& [n, text] : rows)
        EXPECT_EQ(u(formatBytes(n)), text) << n;
}

TEST(MessagesNumbers, FormatDuration) {
    const std::vector<std::pair<std::int64_t, std::string>> rows = {
        {0, "0 s"},           {45, "45 s"},         {59, "59 s"},        {60, "1 min"},
        {61, "2 min"},        {240, "4 min"},       {3599, "60 min"},    {3600, "1 h 00 min"},
        {3601, "1 h 01 min"}, {3900, "1 h 05 min"}, {7500, "2 h 05 min"}};
    for (const auto& [n, text] : rows)
        EXPECT_EQ(u(formatDuration(n)), text) << n;
}

TEST(MessagesStrip, ProgressLineMatchesTheSpecExample) {
    cg::DownloadProgress p;
    p.zoom = 13;
    p.processed = 412;
    p.total = 1300;
    p.downloadedBytes = 9400000;
    p.elapsedSec = 111; // 111 * 888 / 412 = 239 s left -> "4 min"
    EXPECT_EQ(u(progressLine("Google - Satellite", p)),
              "Google - Satellite, Coastal: 31 %  9.4 MB  about 4 min left");
}

TEST(MessagesStrip, ProgressLineWithoutProcessedTilesEndsWithQuestionMark) {
    cg::DownloadProgress p;
    p.zoom = 9;
    p.processed = 0;
    p.total = 50;
    p.elapsedSec = 5;
    EXPECT_EQ(u(progressLine(w(kKarta), p)),
              std::string(kKarta) + ", Passage: 0 %  0.0 MB  time left unknown");
}

TEST(MessagesStrip, ProgressLineWithZeroTotalHasZeroPercent) {
    cg::DownloadProgress p;
    p.zoom = 3;
    p.processed = 0;
    p.total = 0;
    EXPECT_EQ(u(progressLine("S", p)), "S, zoom 3: 0 %  0.0 MB  time left unknown");
}

TEST(MessagesStrip, ExportLineUsesTenthsOfPermille) {
    EXPECT_EQ(u(exportLine("Solent", 0)), "Building the chart Solent: 0 %");
    EXPECT_EQ(u(exportLine("Solent", 505)), "Building the chart Solent: 50 %");
    EXPECT_EQ(u(exportLine(w(kKarta), 1000)), std::string("Building the chart ") + kKarta + ": 100 %");
}

TEST(MessagesStrip, PhaseLines) {
    EXPECT_EQ(u(phaseLine("estimate", "Src")), "Src: working out the size...");
    EXPECT_EQ(u(phaseLine("fill", "Src")), "Src: checking the saved tiles...");
    EXPECT_EQ(u(phaseLine("download", "Src")), "Src: downloading...");
    EXPECT_EQ(u(phaseLine("export", "Src")), "Src: building the chart...");
    EXPECT_EQ(u(phaseLine("verify", "Src")), "Src: verify");
}

TEST(MessagesStrip, StripTextsAndButtonLabels) {
    EXPECT_EQ(u(stoppingText()), "Stopping - saving progress...");
    EXPECT_EQ(u(stopButtonText()), "Stop");
    EXPECT_EQ(u(forceStopButtonText()), "Force stop");
    EXPECT_EQ(u(showButtonText()), "Show");
    EXPECT_EQ(u(resumeButtonText()), "Resume");
    EXPECT_EQ(u(closeButtonText()), "Close");
    EXPECT_EQ(u(buildFromSavedButtonText()), "Build from saved tiles");
    EXPECT_EQ(u(updateMenuText()), "Update...");
    EXPECT_EQ(u(updateTitleText()), "Update chart");
    EXPECT_EQ(u(updateQuestion(wxString::FromUTF8("Solent \xD0\x9A d'x"))),
              "Download again the tiles of \"Solent \xD0\x9A d'x\" that are older than:");
    EXPECT_EQ(u(updateChoiceText(0)), "Today (all tiles)");
    EXPECT_EQ(u(updateChoiceText(1)), "1 month");
    EXPECT_EQ(u(updateChoiceText(2)), "6 months");
    EXPECT_EQ(u(updateChoiceText(3)), "1 year");
    EXPECT_EQ(u(preferencesButtonText()), "Preferences...");
    EXPECT_EQ(u(stripHintText()),
              "You can drag this strip anywhere on the chart. Double-click it to put it back.");
}

TEST(MessagesStrip, RetryAndBudgetTexts) {
    EXPECT_EQ(u(retryText(1, 60)), "No connection. Retry in 60 s (1 of 3)");
    EXPECT_EQ(u(retryText(3, 7)), "No connection. Retry in 7 s (3 of 3)");
    EXPECT_EQ(u(budgetStopText(100)), "Stopped at your data limit (100 MB). Resume to continue.");
}

TEST(MessagesOutcome, DoneSingularPluralAndMissing) {
    const cg::FillTotals none;
    EXPECT_EQ(u(outcomeHeadline(cg::Outcome::Done, none, {"Solent"}, "")), "Chart added: Solent.");
    EXPECT_EQ(u(outcomeHeadline(cg::Outcome::Done, none, {"Solent", "Solent seamarks"}, "")),
              "2 charts added: Solent, Solent seamarks.");
    EXPECT_EQ(u(outcomeHeadline(cg::Outcome::Done, none, {"A", "B", "C"}, "")), "3 charts added: A, B, C.");
    cg::FillTotals fill;
    fill.tiles = 1300;
    fill.missing = 12;
    EXPECT_EQ(u(outcomeHeadline(cg::Outcome::Done, fill, {"Solent"}, "")),
              "Chart added, 12 of 1 300 tiles are missing. Top up later to fill them.");
    EXPECT_EQ(u(outcomeHeadline(cg::Outcome::Done, fill, {"A", "B"}, "")),
              "2 charts added, 12 of 1 300 tiles are missing. Top up later to fill them.");
}

TEST(MessagesOutcome, CyrillicChartNamePassesThrough) {
    const cg::FillTotals none;
    EXPECT_EQ(u(outcomeHeadline(cg::Outcome::Done, none, {w(kKarta)}, "")),
              std::string("Chart added: ") + kKarta + ".");
    EXPECT_EQ(u(outcomeHeadline(cg::Outcome::Done, none, {w(kKarta), "B"}, "")),
              std::string("2 charts added: ") + kKarta + ", B.");
}

TEST(MessagesOutcome, FixedHeadlinesOfSection93) {
    const cg::FillTotals none;
    const std::vector<std::pair<cg::Outcome, std::string>> rows = {
        {cg::Outcome::Stalled, "No connection. Download paused; your progress is saved."},
        {cg::Outcome::StorageError, "Cannot save tiles: the disk is full or cannot be written."},
        {cg::Outcome::DownloadIncomplete, "The download did not complete; your progress is saved."},
        {cg::Outcome::ExportFailed, "The chart could not be written. Nothing was added."},
        {cg::Outcome::Interrupted, "Stopped. Resume any time from My charts."},
        {cg::Outcome::Killed,
         "Stopped by force. Resume continues from the last save (up to 5 minutes lost)."},
        {cg::Outcome::UsageError, "The download failed. See Details."},
        {cg::Outcome::InternalError, "The download failed. See Details."},
        {cg::Outcome::StartFailed,
         "Chart Grabber CLI was not found. Install the chart-grabber-cli package, or choose the program "
         "under Advanced in Preferences."}};
    for (const auto& [o, text] : rows)
        EXPECT_EQ(u(outcomeHeadline(o, none, {}, "ignored")), text) << static_cast<int>(o);
}

// exit 4 whose last error is the CLI's snapshot line (a real line, tests/fixtures/cli-2026.10.6/
// run-weather-snapshot-failed.stderr) gives the weather text; any other last error keeps the landed one.
TEST(MessagesOutcome, ASnapshotFailureHasItsOwnHeadline) {
    EXPECT_EQ(u(outcomeHeadline(cg::Outcome::DownloadIncomplete, {}, {},
                                "cannot take the snapshot of \"RainViewer radar (worldwide, current)\": no "
                                "answer from https://api.rainviewer.com/public/weather-maps.json")),
              "Could not get the latest weather picture. Try again later.");
    EXPECT_EQ(u(outcomeHeadline(cg::Outcome::DownloadIncomplete, {}, {}, "12 tiles failed")),
              "The download did not complete; your progress is saved.");
}

TEST(MessagesOutcome, RefusedGoesThroughRefusalText) {
    EXPECT_EQ(u(outcomeHeadline(cg::Outcome::Refused, {}, {}, "the area holds no tile at the chosen zooms")),
              "The area has no tiles at the chosen detail.");
}

TEST(MessagesOutcome, RefusalTextBranches) {
    const std::vector<std::pair<std::string, std::string>> rows = {
        {"the area holds no tile at the chosen zooms", "The area has no tiles at the chosen detail."},
        {"\"/c/x.mbtiles\" exists; pass --overwrite to replace it", "A chart with this name exists."},
        {"\"Karta\" does not hold bitmap tiles",
         "This chart source cannot be made into an OpenCPN chart: \"Karta\" does not hold bitmap tiles."},
        {"the tiles of \"Karta\" are stored as \"pbf\"; only PNG, JPEG and WebP can be exported",
         "This chart source cannot be made into an OpenCPN chart: the tiles of \"Karta\" are stored as "
         "\"pbf\"; only "
         "PNG, "
         "JPEG and WebP can be exported."},
        {"the projection of \"Karta\" is EPSG:4326; only EPSG:3857 (Web Mercator) can be exported",
         "This chart source cannot be made into an OpenCPN chart: the projection of \"Karta\" is EPSG:4326; "
         "only "
         "EPSG:3857 (Web Mercator) can be exported."},
        {"WebP tiles are not shown by every OpenCPN version; pass --allow-webp to export them anyway",
         "This chart source cannot be made into an OpenCPN chart: WebP tiles are not shown by every OpenCPN "
         "version; "
         "pass --allow-webp to export them anyway."},
        {"\"Karta\" has crop or cut after download switched on; this is not supported",
         "This chart source cannot be made into an OpenCPN chart: \"Karta\" has crop or cut after download "
         "switched "
         "on; "
         "this is not supported."},
        {"something new", "Chart Grabber CLI could not start this download: something new."},
        {"", "Chart Grabber CLI could not start this download."},
        // an exact-match line with extra text is not the "no tile" refusal
        {"the area holds no tile at the chosen zooms, sorry",
         "Chart Grabber CLI could not start this download: the area holds no tile at the chosen zooms, "
         "sorry."}};
    for (const auto& [in, text] : rows)
        EXPECT_EQ(u(refusalText(in)), text) << in;
}

// The S-15 and S-16 refusals. The first and third lines are the captured ones (tests/fixtures/
// cli-2026.10.6/estimate-cache-refused.stderr, estimate-area-too-large.stderr); the "cannot open the cache"
// forms are HAND-MADE from core v2026.10.6 src/cg/app/job_checks.cpp (the capture produced the other form).
TEST(MessagesOutcome, TheCacheAndAreaSizeRefusals) {
    const std::string cache =
        "The folder for saved tiles cannot be used. Choose another one under Advanced in Preferences (Saved "
        "tiles in).";
    const std::string large = "Area too large for this detail - choose a smaller area or less detail";
    EXPECT_EQ(u(refusalText("\"OpenTopoMap\": cannot write the cache folder $SCRATCH/a-file/cache_sqlite/"
                            "Osm_Topo_Map/")),
              cache);
    EXPECT_EQ(u(refusalText("\"OpenTopoMap\": cannot open the cache: unable to open database file")), cache);
    EXPECT_EQ(u(refusalText(std::string("\"") + kKarta + "\": cannot open the cache: disk I/O error")),
              cache);
    EXPECT_EQ(
        u(refusalText("the area is too large at zoom 16 (0-based): more than 2147483647 tiles; choose a "
                      "smaller area or lower zooms")),
        large);
    EXPECT_EQ(u(tooLargeText()), large);
    EXPECT_EQ(u(cacheRefusedText()), cache);
}

TEST(MessagesOutcome, RefusalTextKeepsUtf8) {
    const std::string name = std::string("map ") + kKarta + " only EPSG:3857";
    EXPECT_EQ(u(refusalText(name)), "This chart source cannot be made into an OpenCPN chart: " + name + ".");
}

TEST(MessagesCli, FixedTexts) {
    EXPECT_EQ(u(cliNotFoundText()),
              "Chart Grabber CLI was not found. Install the chart-grabber-cli package, or choose the program "
              "under Advanced in Preferences.");
    EXPECT_EQ(u(cliTooOldText()),
              "Chart Grabber CLI is too old. Install the current chart-grabber-cli package.");
    EXPECT_EQ(u(noSourcesText()),
              "No usable chart source was found. Reinstall Chart Grabber, or check the files in your chart "
              "sources folder.");
}

// : the two status tables, word for word.
TEST(MessagesSettingsStatus, CliStatusTextsOfSection112) {
    EXPECT_EQ(u(cliStatusText(CliStatus::Checking, "9.9.9", "x")), "Checking Chart Grabber CLI...");
    EXPECT_EQ(u(cliStatusText(CliStatus::Ready, "2026.10.0", "x")), "Chart Grabber CLI 2026.10.0 is ready.");
    EXPECT_EQ(u(cliStatusText(CliStatus::Ready, "", "x")), "Chart Grabber CLI is ready.");
    EXPECT_EQ(u(cliStatusText(CliStatus::NotInstalled, "1.2.3", "x")),
              "Chart Grabber CLI was not found. Install the chart-grabber-cli package, or choose the program "
              "under Advanced.");
    EXPECT_EQ(u(cliStatusText(CliStatus::NotRunnable, "1.2.3", "x")),
              "This file cannot be run as a program. Use default to go back to the installed one.");
    EXPECT_EQ(u(cliStatusText(CliStatus::TooOld, "0.0.9", "x")),
              "Chart Grabber CLI 0.0.9 is too old. Install the current chart-grabber-cli package.");
    EXPECT_EQ(u(cliStatusText(CliStatus::TooOld, "", "x")),
              "Chart Grabber CLI is too old. Install the current chart-grabber-cli package.");
    EXPECT_EQ(u(cliStatusText(CliStatus::Fails, "1.2.3", wxString::FromUTF8("\xD0\x81 broke"))),
              "Chart Grabber CLI does not work: \xD0\x81 broke");
}

// the rows of the sources table of section 11.2, word for word.
TEST(MessagesSettingsStatus, SourcesStatusTextsOfSection112) {
    constexpr FolderState kHolds = FolderState::Holds;
    EXPECT_EQ(u(sourcesStatusText(SourcesStatus::Checking, 5, kHolds)), "Looking for chart sources...");
    EXPECT_EQ(u(sourcesStatusText(SourcesStatus::CliNotReady, 5, kHolds)),
              "Chart sources are checked when Chart Grabber CLI is ready.");
    EXPECT_EQ(
        u(sourcesStatusText(SourcesStatus::BundledMissing, 5, kHolds)),
        "The chart sources that come with Chart Grabber are missing. Reinstall the Chart Grabber plugin.");
    EXPECT_EQ(u(sourcesStatusText(SourcesStatus::RunFailed, 5, kHolds)),
              "Chart sources could not be checked.");
    const std::string none =
        "No usable chart source was found. Reinstall Chart Grabber, or check the files in "
        "your chart sources folder.";
    for (FolderState f : {FolderState::Missing, FolderState::Empty, kHolds})
        EXPECT_EQ(u(sourcesStatusText(SourcesStatus::Found, 0, f)), none);
    const std::string missing =
        ". Your own chart sources folder was not found: press Open folder to create it.";
    const std::string empty = ". Put your own chart source (.zmp) files in your folder to add more.";
    const std::string holds = ", with those in your folder.";
    EXPECT_EQ(u(sourcesStatusText(SourcesStatus::Found, 1, FolderState::Missing)),
              "1 chart source available" + missing);
    EXPECT_EQ(u(sourcesStatusText(SourcesStatus::Found, 3, FolderState::Missing)),
              "3 chart sources available" + missing);
    EXPECT_EQ(u(sourcesStatusText(SourcesStatus::Found, 1, FolderState::Empty)),
              "1 chart source available" + empty);
    EXPECT_EQ(u(sourcesStatusText(SourcesStatus::Found, 16, FolderState::Empty)),
              "16 chart sources available" + empty);
    EXPECT_EQ(u(sourcesStatusText(SourcesStatus::Found, 1, kHolds)), "1 chart source available" + holds);
    EXPECT_EQ(u(sourcesStatusText(SourcesStatus::Found, 1950, kHolds)),
              "1 950 chart sources available" + holds);
}

TEST(MessagesRegistration, Texts) {
    EXPECT_EQ(u(firstSuccessHint()),
              "If the chart is not visible, click it in the chart bar at the bottom. MBTiles charts need "
              "OpenGL (Options > Display > Advanced).");
    EXPECT_EQ(u(imageryWarningText()),
              "Satellite images can be 40-170 m off the chart: background only, not for navigation near "
              "dangers.");
    EXPECT_EQ(u(imageryWarningText()).find('\n'), std::string::npos);
    EXPECT_EQ(u(jobFileMissingText()),
              "The download settings of this chart are missing. Delete the chart and make it again.");
    EXPECT_EQ(u(snapshotFailedText()), "Could not get the latest weather picture. Try again later.");
    EXPECT_EQ(u(weatherPictureLine("2026-10-16 06:40 UTC")), "Weather picture: 2026-10-16 06:40 UTC");
}

TEST(MessagesArea, ErrorTexts) {
    EXPECT_EQ(u(areaErrorText(cg::AreaError::OutsideMercator)),
              "The area is too far north or south (beyond 85 degrees).");
    EXPECT_EQ(u(areaErrorText(cg::AreaError::CrossesAntimeridian)),
              "The area crosses the 180 degree meridian. Make two charts, one on each side.");
    EXPECT_EQ(u(areaErrorText(cg::AreaError::Empty)), "The area is empty. Draw a larger area.");
    EXPECT_EQ(u(areaErrorText(cg::AreaError::TooFewRoutePoints)), "The route needs at least two points.");
}

TEST(MessagesWeather, WeatherAndSourceLimitTexts) {
    EXPECT_EQ(u(weatherNoteText("1:580k", "Weather data by RainViewer (https://www.rainviewer.com)")),
              "Made as its own chart, detail up to 1:580k. Credit: Weather data by RainViewer "
              "(https://www.rainviewer.com)");
    EXPECT_EQ(u(weatherNoteText("1:580k", "")), "Made as its own chart, detail up to 1:580k.");
    EXPECT_EQ(
        u(weatherNoteText("1:580k", wxString::FromUTF8("\xD0\x9A\xD1\x80\xD0\xB5\xD0\xB4\xD0\xB8\xD1\x82"))),
        "Made as its own chart, detail up to 1:580k. Credit: "
        "\xD0\x9A\xD1\x80\xD0\xB5\xD0\xB4\xD0\xB8\xD1\x82");
    EXPECT_EQ(u(weatherCoverageText()),
              "Radar reaches about 80-130 NM off the coast; far offshore only satellite pictures show the "
              "weather.");
    EXPECT_EQ(u(sourceLimitText("EUMETSAT MTG GeoColour (Europe, Africa, Atlanti...", "1:580k")),
              "EUMETSAT MTG GeoColour (Europe, Africa, Atlanti... has detail only up to 1:580k; the chart "
              "stops there.");
}

TEST(MessagesSource, RefusalText) {
    cg::SourceRefusal r;
    r.kind = cg::SourceRefusal::Kind::Projection;
    r.epsg = 3395;
    r.verdict = "ignored";
    EXPECT_EQ(u(sourceRefusalText(r)), "Cannot be made into an OpenCPN chart (projection EPSG:3395)");
    r.epsg = 4326;
    EXPECT_EQ(u(sourceRefusalText(r)), "Cannot be made into an OpenCPN chart (projection EPSG:4326)");
    r.kind = cg::SourceRefusal::Kind::Verdict;
    r.epsg = 3857;
    r.verdict = std::string("does not hold bitmap tiles ") + kKarta;
    EXPECT_EQ(u(sourceRefusalText(r)), std::string("does not hold bitmap tiles ") + kKarta);
    r.kind = cg::SourceRefusal::Kind::None;
    EXPECT_EQ(u(sourceRefusalText(r)), "");
}

TEST(MessagesPreflight, TextsOfSection75) {
    cg::PreflightInput in;
    in.totals.bytesOnDisk = 100000000;
    in.totals.bytesToDownload = 50000000;
    in.freeChartBytes = 300000000;
    in.freeCacheBytes = 9400000;
    EXPECT_EQ(u(preflightText(cg::Preflight::TooLarge, in, "/c", "/k")),
              "Area too large for this detail - choose a smaller area or less detail");
    // chart: 2 x 100 MB + 512 MiB = 736.87 MB (separate file systems)
    EXPECT_EQ(u(preflightText(cg::Preflight::ChartDiskFull, in, "/ch art", "/k")),
              "Needs about 737 MB, only 300 MB free on /ch art");
    // same file system adds bytesToDownload: 250 MB + 512 MiB = 786.87 MB
    in.sameFileSystem = true;
    EXPECT_EQ(u(preflightText(cg::Preflight::ChartDiskFull, in, w(kKarta), "/k")),
              std::string("Needs about 787 MB, only 300 MB free on ") + kKarta);
    // cache: 50 MB + 512 MiB = 586.87 MB
    EXPECT_EQ(u(preflightText(cg::Preflight::CacheDiskFull, in, "/c", "/cache")),
              "Needs about 587 MB, only 9.4 MB free on /cache");
    EXPECT_EQ(u(preflightText(cg::Preflight::Ok, in, "/c", "/k")), "");
}

TEST(MessagesDownload, TextsOfSection75AndSettings) {
    EXPECT_EQ(u(estimatingText()), "Working out the size...");
    EXPECT_EQ(u(enterNameText()), "Enter a chart name");
    EXPECT_EQ(u(cannotWriteText("/j/job.ini")), "Cannot write /j/job.ini.");
    EXPECT_EQ(u(overBudgetQuestion(250000000, 100)),
              "This download is about 250 MB. Your limit is 100 MB. Download anyway?");
    EXPECT_EQ(u(disclaimerText()), "Not official charts. Do not use for navigation alone. Respect the terms "
                                   "of use of each chart source.");
    EXPECT_EQ(u(navigationNoteText()),
              "Not for navigation. This source is not an official nautical chart; do "
              "not use it to navigate."); //

    EXPECT_EQ(u(dontShowAgainText()), "Don't show again");
    EXPECT_EQ(u(replaceQuestion()), "A chart with this name exists. Replace it?");
    EXPECT_EQ(u(savedTilesText(3000000, false)), "3.0 MB of saved tiles");
    EXPECT_EQ(u(savedTilesText(0, false)), "No saved tiles yet.");
    EXPECT_EQ(u(savedTilesText(1300000000, true)), "1.3 GB of saved tiles (a download is running)");
    EXPECT_EQ(u(savedTilesText(0, true)), "No saved tiles yet. (a download is running)");
    EXPECT_EQ(u(clearSavedTilesQuestion(38000000, "/c/cache d'x")),
              "Delete 38 MB of saved tiles in\n/c/cache d'x?\n\nYour charts stay. Top up and Build from "
              "saved tiles will then download everything again.");
}

TEST(MessagesAscii, EveryFixedTextIsAscii) {
    const std::vector<wxString> all = {firstSuccessHint(),
                                       imageryWarningText(),
                                       navigationNoteText(),
                                       weatherNoteText("1:580k", "Credit data"),
                                       weatherCoverageText(),
                                       sourceLimitText("Some source", "1:580k"),
                                       cliNotFoundText(),
                                       cliTooOldText(),
                                       noSourcesText(),
                                       estimatingText(),
                                       disclaimerText(),
                                       replaceQuestion(),
                                       savedTilesText(5000000, true),
                                       clearSavedTilesQuestion(5000000, "/c"),
                                       jobFileMissingText(),
                                       snapshotFailedText(),
                                       weatherPictureLine("2026-10-16 06:40 UTC"),
                                       enterNameText(),
                                       areaErrorText(cg::AreaError::OutsideMercator),
                                       areaErrorText(cg::AreaError::CrossesAntimeridian),
                                       outcomeHeadline(cg::Outcome::Killed, {}, {}, ""),
                                       creditText("x"),
                                       updateWeatherMenuText(),
                                       snapshotAgeText(60)};
    for (const wxString& t : all) {
        EXPECT_FALSE(t.empty());
        for (unsigned char c : u(t))
            EXPECT_LT(c, 0x80) << u(t);
    }
}

// ---- UiText, unnamedRouteText, corridorText, bandScaleText ----

TEST(MessagesUiText, EveryValueIsTheLiteralOfSpecSection42) {
    const std::vector<std::pair<UiText, std::string>> rows = {
        {UiText::Seamarks, "Seamarks"},
        {UiText::Cancel, "Cancel"},
        {UiText::DrawHint,
         "Drag a rectangle on the chart, or tap two corners. Right-click or Cancel to stop."},
        {UiText::NoViewport, "Move the chart once, then try again."},
        {UiText::NoRoutes, "No routes. Create a route first."},
        {UiText::DownloadRunning, "A download is running"},
        {UiText::LoadingSources, "Loading chart sources..."},
        {UiText::NoUsableSource, "No usable chart source. Use More sources... or Preferences."},
        {UiText::SeamarksMissing, "The seamarks source (OpenSeaMap) is not among your chart sources"},
        {UiText::ChooseDetail, "Choose the detail"},
        {UiText::HarbourTooLarge, "Too many tiles for this area - draw a smaller area for harbour detail"},
    };
    EXPECT_EQ(rows.size(), 11u);
    for (const auto& [id, text] : rows)
        EXPECT_EQ(u(uiText(id)), text); // equal to an ASCII literal, so ASCII itself
}

TEST(MessagesUiText, UnnamedRouteTextIsTheFirstEightCharactersOfTheGuid) {
    EXPECT_EQ(u(unnamedRouteText("a1b2c3d4-e5f6-4711-8899-aabbccddeeff")), "(unnamed a1b2c3d4)");
    EXPECT_EQ(u(unnamedRouteText("abc")), "(unnamed abc)");
    EXPECT_EQ(u(unnamedRouteText("12345678")), "(unnamed 12345678)");
    EXPECT_EQ(u(unnamedRouteText("")), "(unnamed )");
    // Characters, not bytes: a Cyrillic GUID text of 9 letters keeps 8 letters.
    EXPECT_EQ(u(unnamedRouteText(w("\xD0\x9A\xD0\xB0\xD1\x80\xD1\x82\xD0\xB0-\xD0\xBC\xD0\xB0\xD1\x80"))),
              std::string("(unnamed ") + kKarta + "-\xD0\xBC\xD0\xB0)");
}

TEST(MessagesUiText, CorridorTextHasOneDecimalAndTheUnit) {
    EXPECT_EQ(u(corridorText(0.5)), "0.5 NM");
    EXPECT_EQ(u(corridorText(2)), "2.0 NM");
    EXPECT_EQ(u(corridorText(7.5)), "7.5 NM");
    EXPECT_EQ(u(corridorText(20)), "20.0 NM");
}

TEST(MessagesUiText, BandScaleTextIsTheScaleRangeOfTheBandAtTheLatitude) {
    // Equator rows: (recomputed from geo_math.cpp 2026-10-15).
    EXPECT_EQ(u(bandScaleText(cg::Band::Passage, 0)), "1:2.3M-1:290k");
    EXPECT_EQ(u(bandScaleText(cg::Band::Coastal, 0)), "1:140k-1:36k");
    EXPECT_EQ(u(bandScaleText(cg::Band::Approach, 0)), "1:18k-1:9.0k");
    EXPECT_EQ(u(bandScaleText(cg::Band::Harbour, 0)), "1:4.5k-1:2.3k");
    // 60 degrees north: cos = 0.5, so the denominators halve: 2.31M/2 = 1.16M -> 1.2M, 289k/2 = 144k -> 140k.
    EXPECT_EQ(u(bandScaleText(cg::Band::Passage, 60)), "1:1.2M-1:140k");
}

// . Scales by hand: 156543.034 x cos(lat) / 2^z x 96 / 0.0254; lat 0: z10 577.8k,
// z13 72.2k, z14 36.1k, z21 282; lat 50.7: z10 366.0k, z14 22.87k, z15 11.44k, z17 2.859k.
TEST(MessagesDetail, ZoomEntryTextHasTwoSpacesBeforeTheZoom) {
    EXPECT_EQ(u(zoomEntryText(10, 0)), "1:580k  (zoom 10)");
    EXPECT_EQ(u(zoomEntryText(21, 0)), "1:280  (zoom 21)");
    EXPECT_EQ(u(zoomEntryText(10, 50.7)), "1:370k  (zoom 10)");
}

TEST(MessagesDetail, RangeNoteNamesThePartlyCoveredBands) {
    EXPECT_EQ(u(detailRangeNote(10, 14, 50.7, {cg::Band::Passage})),
              "Detail set under Advanced: 1:370k to 1:23k (part of Passage)");
    EXPECT_EQ(u(detailRangeNote(10, 17, 50.7, {cg::Band::Passage, cg::Band::Harbour})),
              "Detail set under Advanced: 1:370k to 1:2.9k (part of Passage and Harbour)");
    EXPECT_EQ(u(detailRangeNote(9, 13, 0, {})), "Detail set under Advanced: 1:1.2M to 1:72k");
    EXPECT_EQ(u(detailRangeNote(14, 14, 50.7, {})), "Detail set under Advanced: 1:23k");
}

TEST(MessagesDetail, LevelsTextAndNextLevelText) {
    cg::LevelCost cost;
    cost.finestBytes = 52000000;
    cost.totalBytes = 70000000;
    cost.nextBytes = 210000000;
    EXPECT_EQ(u(levelsText(10, 14, 50.7, cost)),
              "5 levels. The finest, 1:23k, is about 52 MB of 70 MB; one more level (1:11k) would add about "
              "210 MB.");
    EXPECT_EQ(u(levelsText(10, 14, 50.7, std::nullopt)), "5 levels.");
    EXPECT_EQ(u(levelsText(10, 10, 0, std::nullopt)), "1 level.");
    EXPECT_EQ(u(nextLevelText(13, 0, 7558272)), "One more level (1:36k) would add about 7.6 MB.");
}

// . Scales at lat 50.7 (156543.034 x cos(lat) / 2^z x 96 / 0.0254, two digits):
// z18 1:1.4k, z16 1:5.7k, z5 1:12M, z2 1:94M.
TEST(MessagesDetail, DetailWarningTexts) {
    cg::DetailAdvice much;
    much.kind = cg::DetailWarning::TooMuch;
    much.suggested = {10, 16};
    much.tiles = 87000;
    much.suggestedTiles = 5700;
    // 87 000 x 20 000 B = 1.74 GB; 5 700 x 20 000 B = 114 MB
    EXPECT_EQ(u(detailWarningText(much, {10, 18}, 50.7, 20000)),
              "Most detail 1:1.4k needs about 87 000 tiles for this area, about 1.7 GB to download. Detail "
              "finer than 1:5k is for a harbour: draw a small area around it for that, as a second "
              "chart.\n\nSuggested: most detail 1:5.7k - about 5 700 tiles, about 114 MB.");
    much.suggestedTiles = 20001; // the suggestion itself is over the limit: no counts
    EXPECT_EQ(u(detailWarningText(much, {10, 18}, 50.7, 20000)),
              "Most detail 1:1.4k needs about 87 000 tiles for this area, about 1.7 GB to download. Detail "
              "finer than 1:5k is for a harbour: draw a small area around it for that, as a second "
              "chart.\n\nSuggested: a smaller area, or most detail 1:5.7k.");
    much.suggestedTiles = 20000; // exactly the limit still gets the counts
    EXPECT_NE(u(detailWarningText(much, {10, 18}, 50.7, 20000)).find("about 20 000 tiles, about 400 MB."),
              std::string::npos);
    cg::DetailAdvice little;
    little.kind = cg::DetailWarning::TooLittle;
    little.suggested = {5, 10};
    EXPECT_EQ(
        u(detailWarningText(little, {2, 10}, 50.7, 20000)),
        "Least detail 1:94M shows whole oceans. It adds little to the download, but OpenCPN's own world "
        "chart already shows this.\n\nSuggested: least detail 1:12M.");
    EXPECT_EQ(u(detailWarningText(cg::DetailAdvice{}, {2, 10}, 50.7, 20000)), "");
    EXPECT_EQ(u(useScaleText(16, 50.7)), "Use 1:5.7k");
    EXPECT_EQ(u(keepScaleText(18, 50.7)), "Keep 1:1.4k");
    EXPECT_EQ(u(warnDetailCheckText()), "Warn before very large or very small detail choices");
}

// : the band of the zoom, "zoom <n>" outside every band; the literals are the bands of
// (Passage 8-11, Coastal 12-14, Approach 15-16, Harbour 17-18).
TEST(MessagesStrip, ProgressLineNamesTheBandOfTheZoom) {
    const std::vector<std::pair<int, std::string>> rows = {
        {3, "zoom 3"},   {7, "zoom 7"},   {8, "Passage"},   {11, "Passage"},
        {12, "Coastal"}, {14, "Coastal"}, {15, "Approach"}, {16, "Approach"},
        {17, "Harbour"}, {18, "Harbour"}, {19, "zoom 19"},  {21, "zoom 21"}};
    for (const auto& [zoom, band] : rows) {
        cg::DownloadProgress p;
        p.zoom = zoom;
        p.processed = 5;
        p.total = 20;
        p.downloadedBytes = 2000000;
        p.elapsedSec = 10; // 10 * 15 / 5 = 30 s left
        EXPECT_EQ(u(progressLine("Src", p)), "Src, " + band + ": 25 %  2.0 MB  about 30 s left") << zoom;
    }
}

TEST(MessagesStrip, ExportAndPhaseLinesKeepCyrillicNames) {
    EXPECT_EQ(u(phaseLine("verify", w(kKarta))), std::string(kKarta) + ": verify");
    EXPECT_EQ(u(phaseLine("export", w(kKarta))), std::string(kKarta) + ": building the chart...");
}

// ---- sweep : no word "map" / "maps", no "tile program", only ASCII ----

bool hasWord(const std::string& text, const std::string& word) {
    const auto lower = [](std::string x) {
        for (char& c : x)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        return x;
    };
    const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
    const std::string t = lower(text);
    for (std::size_t at = t.find(word); at != std::string::npos; at = t.find(word, at + 1)) {
        const bool before = at == 0 || !letter(t[at - 1]);
        const bool after = at + word.size() >= t.size() || !letter(t[at + word.size()]);
        if (before && after)
            return true;
    }
    return false;
}

TEST(MessagesSweep, TheWordMatcherFindsWholeWordsOnly) {
    EXPECT_TRUE(hasWord("Open the Map now", "map"));
    EXPECT_TRUE(hasWord("map", "map"));
    EXPECT_TRUE(hasWord("two maps.", "maps"));
    EXPECT_TRUE(hasWord("(MAP)", "map"));
    EXPECT_FALSE(hasWord("OpenSeaMap", "map"));
    EXPECT_FALSE(hasWord("Mapnik", "map"));
    EXPECT_FALSE(hasWord("bitmap", "map"));
}

TEST(MessagesSweep, NoTextOfMessagesHSaysMapOrTileProgramAndAllAreAscii) {
    std::vector<wxString> all;
    const auto add = [&](const wxString& t) { all.push_back(t); };
    const std::vector<std::string> data = {"",
                                           "Src",
                                           "the area holds no tile at the chosen zooms",
                                           "x; pass --overwrite to replace it",
                                           "\"Src\" does not hold bitmap tiles",
                                           "something new"};
    // enums, iterated from the first to their LAST value; the literal counts fail when a value is added
    constexpr int kUiTexts = static_cast<int>(UiText::HarbourTooLarge) + 1;
    constexpr int kOutcomes = static_cast<int>(cg::Outcome::StartFailed) + 1;
    constexpr int kAreaErrors = static_cast<int>(cg::AreaError::TooFewRoutePoints) + 1;
    constexpr int kCliStatuses = static_cast<int>(CliStatus::Fails) + 1;
    constexpr int kSourcesStatuses = static_cast<int>(SourcesStatus::Found) + 1;
    constexpr int kPreflights = static_cast<int>(cg::Preflight::OverBudget) + 1;
    constexpr int kChartStates = static_cast<int>(cg::ChartState::Failed) + 1;
    constexpr int kBands = static_cast<int>(cg::Band::Harbour) + 1;
    EXPECT_EQ(kUiTexts, 11);
    EXPECT_EQ(kOutcomes, 11);
    EXPECT_EQ(kAreaErrors, 4);
    EXPECT_EQ(kCliStatuses, 6);
    EXPECT_EQ(kSourcesStatuses, 5);
    EXPECT_EQ(kPreflights, 5);
    EXPECT_EQ(kChartStates, 5);
    EXPECT_EQ(kBands, 4);
    for (int i = 0; i < kUiTexts; ++i) {
        add(uiText(static_cast<UiText>(i)));
        EXPECT_FALSE(uiText(static_cast<UiText>(i)).empty()) << "UiText " << i << " has no case";
    }
    cg::FillTotals missing;
    missing.tiles = 10;
    missing.missing = 3;
    for (int i = 0; i < kOutcomes; ++i)
        for (const std::string& line : data)
            for (const cg::FillTotals& fill : {cg::FillTotals{}, missing}) {
                add(outcomeHeadline(static_cast<cg::Outcome>(i), fill, {"Src"}, line));
                add(outcomeHeadline(static_cast<cg::Outcome>(i), fill, {"Src", "Seamarks"}, line));
                add(outcomeHeadline(static_cast<cg::Outcome>(i), fill, {}, line));
            }
    for (const std::string& line : data)
        add(refusalText(line));
    for (int i = 0; i < kAreaErrors; ++i)
        add(areaErrorText(static_cast<cg::AreaError>(i)));
    cg::PreflightInput in;
    for (int i = 0; i < kPreflights; ++i)
        for (bool same : {false, true}) {
            in.sameFileSystem = same;
            add(preflightText(static_cast<cg::Preflight>(i), in, "/c", "/k"));
        }
    for (int i = 0; i < kCliStatuses; ++i)
        for (const char* version : {"", "2026.10.0"})
            add(cliStatusText(static_cast<CliStatus>(i), version, "boom"));
    for (int i = 0; i < kSourcesStatuses; ++i)
        for (FolderState folder : {FolderState::Missing, FolderState::Empty, FolderState::Holds})
            for (int records : {0, 1, 5})
                add(sourcesStatusText(static_cast<SourcesStatus>(i), records, folder));
    for (int i = 0; i < kChartStates; ++i) {
        cg::ChartRecord r;
        r.state = static_cast<cg::ChartState>(i);
        r.tiles = 10;
        r.missing = 2;
        add(chartStateText(r));
    }
    for (int i = 0; i < kBands; ++i) {
        add(bandName(static_cast<cg::Band>(i)));
        add(bandScaleText(static_cast<cg::Band>(i), 45));
    }
    cg::SourceRefusal projection;
    projection.kind = cg::SourceRefusal::Kind::Projection;
    projection.epsg = 4326;
    add(sourceRefusalText(projection));
    for (int zoom : {3, 9, 13, 19})
        for (std::int64_t processed : {0, 5}) {
            cg::DownloadProgress p;
            p.zoom = zoom;
            p.processed = processed;
            p.total = 10;
            p.elapsedSec = 4;
            add(progressLine("Src", p));
        }
    add(exportLine("Solent", 400));
    for (const char* phase : {"estimate", "fill", "download", "export", "verify"})
        add(phaseLine(phase, "Src"));
    for (int i = 0; i < 4; ++i)
        add(updateChoiceText(i));
    for (const wxString& t :
         {formatCount(1950),
          formatBytes(9400000),
          formatDuration(3900),
          savedTilesText(3000000, true),
          savedTilesText(0, false),
          clearSavedTilesQuestion(5000000, "/c"),
          retryText(1, 60),
          budgetStopText(100),
          firstSuccessHint(),
          jobFileMissingText(),
          snapshotFailedText(),
          weatherPictureLine("2026-10-16 06:40 UTC"),
          stoppingText(),
          stopButtonText(),
          forceStopButtonText(),
          showButtonText(),
          resumeButtonText(),
          closeButtonText(),
          buildFromSavedButtonText(),
          updateMenuText(),
          updateTitleText(),
          updateQuestion("Solent"),
          preferencesButtonText(),
          stripHintText(),
          imageryWarningText(),
          termsNoteText(),
          navigationNoteText(),
          weatherNoteText("1:580k", "Credit data"),
          weatherCoverageText(),
          sourceLimitText("Some source", "1:580k"),
          cliNotFoundText(),
          cliTooOldText(),
          noSourcesText(),
          estimatingText(),
          enterNameText(),
          cannotWriteText("/c/a"),
          overBudgetQuestion(250000000, 100),
          disclaimerText(),
          dontShowAgainText(),
          replaceQuestion(),
          unnamedRouteText("a1b2c3d4-e5"),
          corridorText(2),
          zoomEntryText(10, 0),
          detailRangeNote(10, 17, 45, {cg::Band::Passage, cg::Band::Harbour}),
          levelsText(9, 13, 45, cg::LevelCost{1, 2, 3}),
          nextLevelText(13, 45, 3),
          detailWarningText(cg::DetailAdvice{cg::DetailWarning::TooMuch, {10, 16}, 87000, 5700}, {10, 18}, 45,
                            20000),
          useScaleText(16, 45),
          keepScaleText(18, 45),
          warnDetailCheckText(),
          unreadableStateText(),
          deleteChartQuestion("Solent"),
          stoppingButtonText(),
          oneJobAtATimeText(),
          myChartsButtonText(3),
          creditText("x"),
          updateWeatherMenuText(),
          snapshotAgeText(60)})
        add(t);
    EXPECT_GT(all.size(), 400u);
    for (const wxString& t : all) {
        const std::string text = u(t);
        EXPECT_FALSE(hasWord(text, "map")) << text;
        EXPECT_FALSE(hasWord(text, "maps")) << text;
        EXPECT_EQ(text.find("tile program"), std::string::npos) << text;
        EXPECT_EQ(text.find("Tile program"), std::string::npos) << text;
        for (unsigned char c : text)
            EXPECT_LT(c, 0x80) << text;
    }
}

} // namespace
