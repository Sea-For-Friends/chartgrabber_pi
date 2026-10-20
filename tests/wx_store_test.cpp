// SPDX-License-Identifier: GPL-3.0-or-later
// ConfigStore and Look (src/wx) against a temporary wxFileConfig and stubbed OpenCPN API functions.
// The settings dialog needs a display and is not covered here.
#include "config_store.h"
#include "log_capture.h"
#include "look.h"
#include "ocpn_fakes.h"
#include "wx_env.h"

#include <gtest/gtest.h>

#include <wx/app.h>
#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/string.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <ocpn_plugin.h>

namespace {

namespace fs = std::filesystem;

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

void spit(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

class Store : public ::testing::Test {
protected:
    void SetUp() override {
        char tmpl[] = "/tmp/cg-wxstore-XXXXXX";
        ASSERT_NE(mkdtemp(tmpl), nullptr);
        root_ = tmpl;
        conf_ = root_ / "opencpn.conf";
        ocpn_fake::privateData = wxString::FromUTF8((root_ / "private").string());
        ocpn_fake::touch = false;
        ocpn_fake::scale = 1.0;
        oldHome_ = getenv("HOME") ? getenv("HOME") : "";
        setenv("HOME", (root_ / "home").c_str(), 1);
        open();
    }
    void TearDown() override {
        store_.reset();
        config_.reset(); // before the directory goes: the destructor would flush into it
        setenv("HOME", oldHome_.c_str(), 1);
        fs::remove_all(root_);
    }
    void open() {
        config_ = std::make_unique<wxFileConfig>(wxString("cgtest"), wxEmptyString,
                                                 wxString::FromUTF8(conf_.string()), wxEmptyString,
                                                 wxCONFIG_USE_LOCAL_FILE);
        store_ = std::make_unique<ConfigStore>(config_.get());
    }
    fs::path root_, conf_;
    std::string oldHome_;
    std::unique_ptr<wxFileConfig> config_;
    std::unique_ptr<ConfigStore> store_;
};

cg::ChartRecord record(const std::string& id, const std::string& name) {
    cg::ChartRecord r;
    r.id = id;
    r.name = name;
    r.sourceGuid = "AAAA-1";
    r.bbox = {-1.5, 50.25, 2.125, 51.0};
    r.zooms = {8, 9};
    r.created = "2026-10-15T10:15:00";
    r.files = {"/c/a.mbtiles"};
    return r;
}

// Places a chart folder the way a finished job leaves it, without ConfigStore::saveChart.
void placeChart(const fs::path& jobs, const cg::ChartRecord& r) {
    spit(jobs / r.id / "chart.ini", cg::chartIni(r));
}

TEST_F(Store, DefaultsWhenNothingStored) {
    const cg::Settings s = store_->loadSettings();
    EXPECT_EQ(s.cliPath, "");
    EXPECT_EQ(s.proxy, "");
    EXPECT_EQ(s.threads, 2);
    EXPECT_DOUBLE_EQ(s.corridorNm, 2.0);
    EXPECT_FALSE(s.slowConnection);
    EXPECT_EQ(s.dataBudgetMb, 100);
    EXPECT_TRUE(s.registerCharts);
    EXPECT_FALSE(s.stripOnChart) << "StripOnChart missing: its own window";
    EXPECT_FALSE(s.debugLog) << "DebugLog missing: normal log detail";
    EXPECT_TRUE(s.warnDetailLevels) << "WarnDetailLevels missing: the warning is on";
    EXPECT_FALSE(store_->disclaimerShown());
    const auto c = store_->loadLastChoice();
    EXPECT_TRUE(c.seamarks);
    EXPECT_TRUE(c.bands.empty());
}

TEST_F(Store, SaveWritesSpecKeysToDiskUnderGroupAndLoadsBack) {
    cg::Settings s;
    s.cliPath = "/opt/my tools/tiles-\xC3\xA9";
    s.mapsDir = "/m/Karty d'or";
    s.cacheDir = "/c/\xD0\x9A\xD1\x8D\xD1\x88";
    s.chartDir = "/charts";
    s.proxy = "http://p:3128";
    s.threads = 5;
    s.corridorNm = 2.5;
    s.slowConnection = true;
    s.dataBudgetMb = 321;
    s.registerCharts = false;
    s.stripOnChart = true;
    s.debugLog = true;
    store_->saveSettings(s);

    // Flush after save: the file already holds the keys, with the config still open.
    const std::string file = slurp(conf_);
    EXPECT_NE(file.find("[PlugIns/ChartGrabber]"), std::string::npos) << file;
    EXPECT_NE(file.find("CliPath=/opt/my tools/tiles-\xC3\xA9"), std::string::npos) << file;
    EXPECT_NE(file.find("Threads=5"), std::string::npos) << file;
    EXPECT_NE(file.find("SlowConnection=1"), std::string::npos) << file;
    EXPECT_NE(file.find("DataBudgetMb=321"), std::string::npos) << file;
    EXPECT_NE(file.find("RegisterCharts=0"), std::string::npos) << file;
    EXPECT_NE(file.find("StripOnChart=1"), std::string::npos) << file;
    EXPECT_NE(file.find("DebugLog=1"), std::string::npos) << file;
    // The number is text with a dot, never a comma.
    wxString corridor;
    config_->Read("/PlugIns/ChartGrabber/CorridorNm", &corridor);
    EXPECT_EQ(corridor.find(','), wxString::npos);
    EXPECT_EQ(corridor.substr(0, 3), "2.5");

    config_.reset(); // "restart": a fresh config object reads the file
    open();
    const cg::Settings r = store_->loadSettings();
    EXPECT_EQ(r.cliPath, s.cliPath);
    EXPECT_EQ(r.mapsDir, s.mapsDir);
    EXPECT_EQ(r.cacheDir, s.cacheDir);
    EXPECT_EQ(r.chartDir, s.chartDir);
    EXPECT_EQ(r.proxy, s.proxy);
    EXPECT_EQ(r.threads, 5);
    EXPECT_DOUBLE_EQ(r.corridorNm, 2.5);
    EXPECT_TRUE(r.slowConnection);
    EXPECT_EQ(r.dataBudgetMb, 321);
    EXPECT_FALSE(r.registerCharts);
}

TEST_F(Store, KeysLiveInTheGroupAndThePathIsRestored) {
    config_->SetPath("/Other");
    cg::Settings s;
    s.proxy = "px";
    store_->saveSettings(s);
    store_->setDisclaimerShown();
    EXPECT_EQ(config_->GetPath(), "/Other");
    EXPECT_EQ(config_->Read("/PlugIns/ChartGrabber/Proxy", "?"), "px");
    EXPECT_EQ(config_->Read("/PlugIns/ChartGrabber/DisclaimerShown", 0L), 1L);
    EXPECT_FALSE(config_->HasEntry("/Other/Proxy"));
    EXPECT_TRUE(store_->loadSettings().proxy == "px");
}

TEST_F(Store, NumbersAreReadWithADotAndBadTextFallsBack) {
    config_->Write("/PlugIns/ChartGrabber/CorridorNm", wxString("3.75"));
    EXPECT_DOUBLE_EQ(store_->loadSettings().corridorNm, 3.75);
    config_->Write("/PlugIns/ChartGrabber/CorridorNm", wxString("abc"));
    EXPECT_DOUBLE_EQ(store_->loadSettings().corridorNm, 2.0);
    config_->Write("/PlugIns/ChartGrabber/CorridorNm", wxString("nan"));
    EXPECT_DOUBLE_EQ(store_->loadSettings().corridorNm, 2.0);
}

// : an invalid value gives the default and exactly one log line
// naming the key and the value; a valid value is kept silently.
TEST_F(Store, InvalidValuesGiveTheDefaultAndOneLogLineEach) {
    struct Case {
        const char* key;
        wxString raw;
        std::function<bool(const cg::Settings&)> isDefault;
        std::string line;
    };
    const auto threads = [](const cg::Settings& s) { return s.threads == 2; };
    const auto budget = [](const cg::Settings& s) { return s.dataBudgetMb == 100; };
    const auto corridor = [](const cg::Settings& s) { return s.corridorNm == 2.0; };
    const std::vector<Case> cases = {
        {"Threads", "0", threads, "ChartGrabber: setting Threads=0 is invalid; using 2"},
        {"Threads", "65", threads, "ChartGrabber: setting Threads=65 is invalid; using 2"},
        {"Threads", "x", threads, "ChartGrabber: setting Threads=x is invalid; using 2"},
        {"Threads", "3x", threads, "ChartGrabber: setting Threads=3x is invalid; using 2"},
        {"DataBudgetMb", "0", budget, "ChartGrabber: setting DataBudgetMb=0 is invalid; using 100"},
        {"DataBudgetMb", "100001", budget, "ChartGrabber: setting DataBudgetMb=100001 is invalid; using 100"},
        {"CorridorNm", "nan", corridor, "ChartGrabber: setting CorridorNm=nan is invalid; using 2"},
        {"CorridorNm", "30", corridor, "ChartGrabber: setting CorridorNm=30 is invalid; using 2"},
        {"CorridorNm", "0.4", corridor, "ChartGrabber: setting CorridorNm=0.4 is invalid; using 2"},
        {"SlowConnection", "2", [](const cg::Settings& s) { return !s.slowConnection; },
         "ChartGrabber: setting SlowConnection=2 is invalid; using 0"},
        {"RegisterCharts", "yes", [](const cg::Settings& s) { return s.registerCharts; },
         "ChartGrabber: setting RegisterCharts=yes is invalid; using 1"},
        {"StripOnChart", "2", [](const cg::Settings& s) { return !s.stripOnChart; },
         "ChartGrabber: setting StripOnChart=2 is invalid; using 0"},
        {"DebugLog", "7", [](const cg::Settings& s) { return !s.debugLog; },
         "ChartGrabber: setting DebugLog=7 is invalid; using 0"},
        {"WarnDetailLevels", "2", [](const cg::Settings& s) { return s.warnDetailLevels; },
         "ChartGrabber: setting WarnDetailLevels=2 is invalid; using 1"},
        {"WarnDetailLevels", "yes", [](const cg::Settings& s) { return s.warnDetailLevels; },
         "ChartGrabber: setting WarnDetailLevels=yes is invalid; using 1"},
        {"WarnDetailLevels", "", [](const cg::Settings& s) { return s.warnDetailLevels; },
         "ChartGrabber: setting WarnDetailLevels= is invalid; using 1"},
        {"MapsDir", "relative/dir", [](const cg::Settings& s) { return s.mapsDir.empty(); },
         "ChartGrabber: setting MapsDir=relative/dir is invalid; using (empty)"},
        {"CliPath", wxString::FromUTF8("~/\xD1\x91"), [](const cg::Settings& s) { return s.cliPath.empty(); },
         "ChartGrabber: setting CliPath=~/\xD1\x91 is invalid; using (empty)"},
        {"CacheDir", "cache", [](const cg::Settings& s) { return s.cacheDir.empty(); },
         "ChartGrabber: setting CacheDir=cache is invalid; using (empty)"},
        {"ChartDir",
         wxString("/charts/a\x01"
                  "b"),
         [](const cg::Settings& s) { return s.chartDir.empty(); },
         "ChartGrabber: setting ChartDir=/charts/a?b is invalid; using (empty)"},
        {"ChartDir",
         wxString("/charts/c\x7f"
                  "d"),
         [](const cg::Settings& s) { return s.chartDir.empty(); },
         "ChartGrabber: setting ChartDir=/charts/c?d is invalid; using (empty)"},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(std::string(c.key) + "=" + c.raw.utf8_string());
        config_->Write(wxString("/PlugIns/ChartGrabber/") + c.key, c.raw);
        store_ = std::make_unique<ConfigStore>(config_.get()); // a new session: nothing logged yet
        LogCapture log;
        EXPECT_TRUE(c.isDefault(store_->loadSettings()));
        EXPECT_EQ(log.lines, std::vector<std::string>{c.line});
        store_->loadSettings(); // the same session: once per key
        EXPECT_EQ(log.lines.size(), 1u);
        config_->DeleteEntry(wxString("/PlugIns/ChartGrabber/") + c.key);
    }
}

// loading an invalid value does not write it back; the saved flag and the one-key writer.
TEST_F(Store, WarnDetailLevelsIsNotWrittenBackByLoadingAndSetWritesOnlyItsKey) {
    config_->Write("/PlugIns/ChartGrabber/WarnDetailLevels", wxString("2"));
    store_->loadSettings();
    wxString raw;
    config_->Read("/PlugIns/ChartGrabber/WarnDetailLevels", &raw);
    EXPECT_EQ(raw, "2") << "loading never repairs the value";
    cg::Settings s = store_->loadSettings();
    s.threads = 7;
    store_->saveSettings(s);
    EXPECT_NE(slurp(conf_).find("WarnDetailLevels=1"), std::string::npos) << "a save writes the loaded value";
    s.warnDetailLevels = false;
    store_->saveSettings(s);
    EXPECT_NE(slurp(conf_).find("WarnDetailLevels=0"), std::string::npos);
    EXPECT_FALSE(store_->loadSettings().warnDetailLevels);
    config_->Write("/PlugIns/ChartGrabber/SlowConnection", wxString("x")); // a save would rewrite it as 0
    config_->Write("/PlugIns/ChartGrabber/WarnDetailLevels", wxString("1"));
    store_->setWarnDetailLevels(false);
    const std::string file = slurp(conf_); // Flush already happened
    EXPECT_NE(file.find("WarnDetailLevels=0"), std::string::npos) << file;
    EXPECT_NE(file.find("SlowConnection=x"), std::string::npos) << "no other key is touched";
}

TEST_F(Store, ValidValuesAreKeptAndLogNothing) {
    LogCapture log;
    const std::vector<std::pair<const char*, wxString>> values = {
        {"Threads", "64"},       {"DataBudgetMb", "100000"},
        {"CorridorNm", "0.5"},   {"SlowConnection", "1"},
        {"RegisterCharts", "0"}, {"StripOnChart", "1"},
        {"DebugLog", "1"},       {"MapsDir", wxString::FromUTF8("/\xD0\xBA a'b")},
        {"ChartDir", ""},
    };
    for (const auto& [key, raw] : values)
        config_->Write(wxString("/PlugIns/ChartGrabber/") + key, raw);
    const cg::Settings s = store_->loadSettings();
    EXPECT_EQ(s.threads, 64);
    EXPECT_EQ(s.dataBudgetMb, 100000);
    EXPECT_DOUBLE_EQ(s.corridorNm, 0.5);
    EXPECT_TRUE(s.slowConnection);
    EXPECT_FALSE(s.registerCharts);
    EXPECT_TRUE(s.stripOnChart);
    EXPECT_TRUE(s.debugLog);
    EXPECT_EQ(s.mapsDir, "/\xD0\xBA a'b");
    EXPECT_EQ(s.chartDir, "");
    config_->Write("/PlugIns/ChartGrabber/Threads", wxString("1"));
    config_->Write("/PlugIns/ChartGrabber/CorridorNm", wxString("20"));
    EXPECT_EQ(store_->loadSettings().threads, 1);
    EXPECT_DOUBLE_EQ(store_->loadSettings().corridorNm, 20.0);
    EXPECT_TRUE(log.lines.empty());
}

TEST_F(Store, AnInvalidFlagOfTheLastChoiceIsLoggedToo) {
    LogCapture log;
    config_->Write("/PlugIns/ChartGrabber/LastSeamarks", wxString("7"));
    EXPECT_TRUE(store_->loadLastChoice().seamarks);
    EXPECT_EQ(log.lines,
              std::vector<std::string>{"ChartGrabber: setting LastSeamarks=7 is invalid; using 1"});
}

TEST_F(Store, TheWeatherKeysRoundTripAndAnInvalidLastWeatherIsLoggedAndReadAsZero) {
    ConfigStore::LastChoice c;
    c.weather = true;
    c.weatherGuid = "{1a696d82-F258}-\xC3\xA9";
    store_->saveLastChoice(c);
    const std::string file = slurp(conf_);
    EXPECT_NE(file.find("LastWeather=1"), std::string::npos) << file;
    EXPECT_NE(file.find("LastWeatherSource={1a696d82-F258}-\xC3\xA9"), std::string::npos) << file;
    config_.reset();
    open();
    const auto r = store_->loadLastChoice();
    EXPECT_TRUE(r.weather);
    EXPECT_EQ(r.weatherGuid, c.weatherGuid);
    LogCapture log;
    config_->Write("/PlugIns/ChartGrabber/LastWeather", wxString("yes"));
    EXPECT_FALSE(store_->loadLastChoice().weather);
    EXPECT_EQ(log.lines,
              std::vector<std::string>{"ChartGrabber: setting LastWeather=yes is invalid; using 0"});
}

TEST_F(Store, DisclaimerIsFlushedToDisk) {
    store_->setDisclaimerShown();
    EXPECT_NE(slurp(conf_).find("DisclaimerShown=1"), std::string::npos);
}

TEST_F(Store, LastChoiceRoundTripAndBandLettersInSpecOrder) {
    ConfigStore::LastChoice c;
    c.sourceGuid = "GUID-\xC3\xA9";
    c.seamarks = false;
    c.bands = {cg::Band::Harbour, cg::Band::Passage, cg::Band::Approach}; // not in letter order
    c.advancedZooms = "8-10,12";
    c.routeGuid = "ROUTE-7";
    store_->saveLastChoice(c);
    const std::string file = slurp(conf_);
    EXPECT_NE(file.find("LastBands=P,A,H"), std::string::npos) << file;
    EXPECT_NE(file.find("LastSeamarks=0"), std::string::npos) << file;
    config_.reset();
    open();
    const auto r = store_->loadLastChoice();
    EXPECT_EQ(r.sourceGuid, c.sourceGuid);
    EXPECT_FALSE(r.seamarks);
    EXPECT_EQ(r.bands, c.bands);
    EXPECT_EQ(r.advancedZooms, "8-10,12");
    EXPECT_EQ(r.routeGuid, "ROUTE-7");
}

TEST_F(Store, LastBandsIgnoresUnknownLetters) {
    config_->Write("/PlugIns/ChartGrabber/LastBands", wxString("C,x,H"));
    const std::set<cg::Band> want{cg::Band::Coastal, cg::Band::Harbour};
    EXPECT_EQ(store_->loadLastChoice().bands, want);
}

TEST_F(Store, ChartDirDefaultAndOverride) {
    cg::Settings s;
    EXPECT_EQ(store_->chartDir(s), (root_ / "home" / "Charts" / "ChartGrabber").string());
    s.chartDir = "/my charts";
    EXPECT_EQ(store_->chartDir(s), "/my charts");
}

// Decision 0003: the plugin's own cache, not the CLI's; it does not depend on cliPath.
TEST_F(Store, CliCacheRootRules) {
    cg::Settings s;
    const std::string def = (root_ / "private").string() + "/plugins/chartgrabber/cache";
    EXPECT_EQ(store_->cliCacheRoot(s), def);
    s.cliPath = "/opt/my tools/tiles-\xC3\xA9";
    EXPECT_EQ(store_->cliCacheRoot(s), def); // two different cliPath values, one root
    s.cliPath = "/usr/bin/other";
    EXPECT_EQ(store_->cliCacheRoot(s), def);
    s.cacheDir = "/var/cache dir";
    EXPECT_EQ(store_->cliCacheRoot(s), "/var/cache dir");
}

TEST_F(Store, CliCacheRootWithoutAPrivateDataLocationIsUnderHomeOpencpn) {
    ocpn_fake::privateDataNull = true;
    const std::string root = store_->cliCacheRoot(cg::Settings{});
    ocpn_fake::privateDataNull = false;
    EXPECT_EQ(root, (root_ / "home").string() + "/.opencpn/plugins/chartgrabber/cache");
}

TEST_F(Store, SourcesDirIsTheKeyOrTheDefaultUnderHome) {
    cg::Settings s;
    EXPECT_EQ(store_->sourcesDir(s), (root_ / "home").string() + "/Charts/Chart sources");
    s.mapsDir = "/maps d'x";
    EXPECT_EQ(store_->sourcesDir(s), "/maps d'x");
}

TEST_F(Store, CliSettingsResolveEmptyValuesOnlyInTheCopy) {
    cg::Settings s;
    s.proxy = "http://p:3128";
    s.slowConnection = true;
    const cg::Settings out = store_->cliSettings(s);
    EXPECT_TRUE(out.cliMaps.empty()); // no bundled folder set, the default folder does not exist
    EXPECT_EQ(out.cacheDir, (root_ / "private").string() + "/plugins/chartgrabber/cache");
    EXPECT_EQ(out.proxy, "http://p:3128"); // every other field passes through
    EXPECT_TRUE(out.slowConnection);
    EXPECT_TRUE(s.mapsDir.empty()); // the argument is untouched
    EXPECT_TRUE(s.cacheDir.empty());
    // A save of the unresolved settings stores empty keys, so the defaults stay defaults.
    store_->saveSettings(s);
    EXPECT_TRUE(store_->loadSettings().mapsDir.empty());
    EXPECT_TRUE(store_->loadSettings().cacheDir.empty());
    s.cacheDir = "/ca\xC3\xA9 che";
    EXPECT_EQ(store_->cliSettings(s).cacheDir, "/ca\xC3\xA9 che");
}

// cliMaps = the bundled folder, then the sailor's folder only when it holds a source (the
// nested one included); the bundled folder is not checked (the dialog does); --cache always.
TEST_F(Store, CliSettingsMapsAreBundledThenTheFolderWhenItHoldsASource) {
    const fs::path def = root_ / "home" / "Charts" / "Chart sources";
    const std::string bundled = (root_ / "bundl\xC3\xA9 d'x").string();
    store_->setBundledSourcesDir(bundled);
    EXPECT_EQ(store_->bundledSourcesDir(), bundled);
    cg::Settings s;
    EXPECT_EQ(store_->cliSettings(s).cliMaps, (std::vector<std::string>{bundled})) << "missing folder";
    fs::create_directories(def / "Sea" / "Extra");
    EXPECT_EQ(store_->cliSettings(s).cliMaps, (std::vector<std::string>{bundled})) << "empty folder";
    std::ofstream(def / "Sea" / "Extra" / "OpenSeaMap.zmp") << "x";
    EXPECT_EQ(store_->cliSettings(s).cliMaps, (std::vector<std::string>{bundled, def.string()})) << "nested";
    EXPECT_FALSE(store_->cliSettings(s).cacheDir.empty());
    const fs::path mine = root_ / "my d'x";
    fs::create_directories(mine);
    s.mapsDir = mine.string();
    EXPECT_EQ(store_->cliSettings(s).cliMaps, (std::vector<std::string>{bundled})) << "a chosen empty folder";
    std::ofstream(mine / "a.ZMP") << "x";
    EXPECT_EQ(store_->cliSettings(s).cliMaps, (std::vector<std::string>{bundled, mine.string()}));
    EXPECT_TRUE(s.cliMaps.empty()) << "the argument is untouched";
    EXPECT_EQ(s.mapsDir, mine.string());
}

TEST_F(Store, JobsDirIsUnderThePrivateDataLocation) {
    EXPECT_EQ(store_->jobsDir(), (root_ / "private").string() + "/plugins/chartgrabber/jobs");
}

TEST_F(Store, ChartsAreSavedLoadedNewestFirstAndBadOnesSkipped) {
    const fs::path jobs = store_->jobsDir();
    placeChart(jobs, record("20261014-090000", "old"));
    placeChart(jobs, record("20261015-101500-2", "newest"));
    placeChart(jobs, record("20261015-101500", "middle"));
    fs::create_directories(jobs / "20261016-000000");          // no chart.ini
    spit(jobs / "20261017-000000" / "chart.ini", "Zooms=1\n"); // no Name: rejected
    spit(jobs / "stray-file.txt", "x");
    const auto charts = store_->loadCharts();
    ASSERT_EQ(charts.size(), 3u);
    EXPECT_EQ(charts[0].id, "20261015-101500-2");
    EXPECT_EQ(charts[0].name, "newest");
    EXPECT_EQ(charts[1].id, "20261015-101500");
    EXPECT_EQ(charts[2].id, "20261014-090000");
    EXPECT_EQ(charts[2].bbox.west, -1.5);
}

TEST_F(Store, SaveChartCreatesMissingJobsFolderAndWritesChartIni) {
    store_->saveChart(record("20261014-090000", "old"));
    const fs::path ini = fs::path(store_->jobsDir()) / "20261014-090000" / "chart.ini";
    EXPECT_EQ(slurp(ini), cg::chartIni(record("20261014-090000", "old")));
    store_->saveChart(record("20261014-090000", "renamed")); // overwrites
    EXPECT_EQ(store_->loadCharts().at(0).name, "renamed");
}

TEST_F(Store, ClearCacheDirsRemovesCacheFoldersButSkipsSymlinks) {
    const fs::path cache = root_ / "cache-root";
    const fs::path target = root_ / "elsewhere";
    spit(cache / "cache1" / "t.png", "x");
    spit(cache / "other" / "t.png", "keep");
    spit(target / "precious.txt", "keep");
    fs::create_directory_symlink(target, cache / "cache-link");
    ConfigStore::clearCacheDirs(cache.string());
    EXPECT_FALSE(fs::exists(cache / "cache1"));
    EXPECT_TRUE(fs::exists(cache / "other" / "t.png"));
    EXPECT_TRUE(fs::is_symlink(cache / "cache-link"));
    EXPECT_TRUE(fs::exists(target / "precious.txt"));
}

TEST_F(Store, LoadChartsWithoutJobsFolderIsEmpty) {
    EXPECT_TRUE(store_->loadCharts().empty());
}

TEST_F(Store, DeleteChartDirRemovesOnlyThatDirectoryAndRefusesSeparators) {
    const fs::path jobs = store_->jobsDir();
    placeChart(jobs, record("20261014-090000", "a"));
    placeChart(jobs, record("20261015-101500", "b"));
    spit(root_ / "private" / "plugins" / "chartgrabber" / "outside.txt", "keep");
    store_->deleteChartDir("../outside.txt");
    store_->deleteChartDir("");
    store_->deleteChartDir("a/b");
    EXPECT_TRUE(fs::exists(root_ / "private" / "plugins" / "chartgrabber" / "outside.txt"));
    EXPECT_TRUE(fs::exists(jobs / "20261014-090000"));
    store_->deleteChartDir("20261014-090000");
    EXPECT_FALSE(fs::exists(jobs / "20261014-090000"));
    EXPECT_TRUE(fs::exists(jobs / "20261015-101500" / "chart.ini"));
}

TEST(LookScheme, OutlineColourPerScheme) {
    Look look;
    EXPECT_EQ(look.scheme(), PI_GLOBAL_COLOR_SCHEME_DAY);
    EXPECT_EQ(look.outline(), wxColour(255, 0, 255));
    look.setScheme(PI_GLOBAL_COLOR_SCHEME_DUSK);
    EXPECT_EQ(look.scheme(), PI_GLOBAL_COLOR_SCHEME_DUSK);
    EXPECT_EQ(look.outline(), wxColour(200, 0, 0));
    look.setScheme(PI_GLOBAL_COLOR_SCHEME_NIGHT);
    EXPECT_EQ(look.outline(), wxColour(128, 0, 0));
    look.setScheme(PI_GLOBAL_COLOR_SCHEME_RGB);
    EXPECT_EQ(look.outline(), wxColour(255, 0, 255));
}

TEST(LookScheme, SubscribersAreNotifiedAfterTheSchemeChanged) {
    Look look;
    int first = 0, second = 0;
    PI_ColorScheme seen = PI_GLOBAL_COLOR_SCHEME_DAY;
    look.subscribe([&] {
        ++first;
        seen = look.scheme();
    });
    look.subscribe([&] { second += 10; });
    look.setScheme(PI_GLOBAL_COLOR_SCHEME_NIGHT);
    EXPECT_EQ(first, 1);
    EXPECT_EQ(second, 10);
    EXPECT_EQ(seen, PI_GLOBAL_COLOR_SCHEME_NIGHT);
}

TEST(LookTouch, MinControlHeightIsZeroOutsideTouchMode) {
    ocpn_fake::touch = false;
    ocpn_fake::scale = 2.0;
    Look look;
    EXPECT_FALSE(look.touch());
    EXPECT_EQ(look.minControlHeight(), 0);
}

TEST(LookTouch, MinControlHeightIs48TimesScaleRoundedUp) {
    ocpn_fake::touch = true;
    Look look;
    EXPECT_TRUE(look.touch());
    ocpn_fake::scale = 1.0;
    EXPECT_EQ(look.minControlHeight(), 48);
    ocpn_fake::scale = 1.25;
    EXPECT_EQ(look.minControlHeight(), 60);
    ocpn_fake::scale = 1.01; // 48.48 rounds up, not to nearest
    EXPECT_EQ(look.minControlHeight(), 49);
    ocpn_fake::touch = false;
    ocpn_fake::scale = 1.0;
}

TEST_F(Store, FirstSuccessShownDefaultsFalseAndSurvivesANewStore) {
    EXPECT_FALSE(store_->firstSuccessShown());
    EXPECT_FALSE(store_->disclaimerShown());
    store_->setFirstSuccessShown();
    EXPECT_TRUE(store_->firstSuccessShown());
    EXPECT_FALSE(store_->disclaimerShown()); // a different key
    EXPECT_NE(slurp(conf_).find("FirstSuccessShown=1"), std::string::npos);
    open(); // a new ConfigStore on a new wxFileConfig of the same file
    EXPECT_TRUE(store_->firstSuccessShown());
}

// the strip's place round-trips through the three keys (DIP); unreadable keys give nullopt and ONE
// log line per key per session; nullopt deletes the keys; StripHintShown is a
// flag.
TEST_F(Store, StripAnchorRoundTripsAndNulloptDeletesTheKeys) {
    EXPECT_FALSE(store_->stripAnchor());
    store_->setStripAnchor(cg::StripAnchor{cg::StripCorner::TopRight, 17, 29});
    const std::string text = slurp(conf_);
    EXPECT_NE(text.find("StripCorner=tr"), std::string::npos);
    EXPECT_NE(text.find("StripDx=17"), std::string::npos);
    EXPECT_NE(text.find("StripDy=29"), std::string::npos);
    open();
    const auto a = store_->stripAnchor();
    ASSERT_TRUE(a);
    EXPECT_EQ(a->corner, cg::StripCorner::TopRight);
    EXPECT_EQ(a->dx, 17);
    EXPECT_EQ(a->dy, 29);
    store_->setStripAnchor(std::nullopt);
    EXPECT_FALSE(store_->stripAnchor());
    EXPECT_EQ(slurp(conf_).find("StripD"), std::string::npos);
    EXPECT_EQ(slurp(conf_).find("StripCorner"), std::string::npos);
}

TEST_F(Store, UnreadableStripKeysGiveNulloptAndOneLogLineEach) {
    const wxString g = "/PlugIns/ChartGrabber/";
    config_->Write(g + "StripCorner", "x");
    config_->Write(g + "StripDx", "-3");
    config_->Write(g + "StripDy", "7");
    store_ = std::make_unique<ConfigStore>(config_.get());
    LogCapture log;
    EXPECT_FALSE(store_->stripAnchor());
    EXPECT_EQ(log.lines, (std::vector<std::string>{"ChartGrabber: setting StripCorner=x is invalid; using bl",
                                                   "ChartGrabber: setting StripDx=-3 is invalid; using 8"}));
    EXPECT_FALSE(store_->stripAnchor()); // the same session: nothing more
    EXPECT_EQ(log.lines.size(), 2u);
    config_->Write(g + "StripCorner", "bl");
    config_->Write(g + "StripDx", "8");
    config_->Write(g + "StripDy", "7.5");
    store_ = std::make_unique<ConfigStore>(config_.get());
    log.lines.clear();
    EXPECT_FALSE(store_->stripAnchor());
    EXPECT_EQ(log.lines, std::vector<std::string>{"ChartGrabber: setting StripDy=7.5 is invalid; using 32"});
    config_->DeleteEntry(g + "StripDy"); // a missing key is the default, silently
    store_ = std::make_unique<ConfigStore>(config_.get());
    log.lines.clear();
    EXPECT_FALSE(store_->stripAnchor());
    EXPECT_TRUE(log.lines.empty());
}

TEST_F(Store, StripHintShownDefaultsFalseAndSurvivesANewStore) {
    EXPECT_FALSE(store_->stripHintShown());
    store_->setStripHintShown();
    EXPECT_TRUE(store_->stripHintShown());
    EXPECT_NE(slurp(conf_).find("StripHintShown=1"), std::string::npos);
    open();
    EXPECT_TRUE(store_->stripHintShown());
}

TEST(LookTokens, UnsubscribedCallbackIsNotCalledAgain) {
    Look look;
    int a = 0, b = 0;
    const int ta = look.subscribe([&] { ++a; });
    look.subscribe([&] { ++b; });
    look.setScheme(PI_GLOBAL_COLOR_SCHEME_DUSK);
    look.unsubscribe(ta);
    look.setScheme(PI_GLOBAL_COLOR_SCHEME_NIGHT);
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 2);
}

TEST(LookTokens, CallbackMayUnsubscribeItselfDuringSetScheme) {
    Look look;
    int self = 0, before = 0, after = 0, token = 0;
    look.subscribe([&] { ++before; });
    token = look.subscribe([&] {
        ++self;
        look.unsubscribe(token);
    });
    look.subscribe([&] { ++after; });
    look.setScheme(PI_GLOBAL_COLOR_SCHEME_DUSK);
    EXPECT_EQ(self, 1);
    EXPECT_EQ(before, 1);
    EXPECT_EQ(after, 1); // the one after it still ran in the same pass
    look.setScheme(PI_GLOBAL_COLOR_SCHEME_NIGHT);
    EXPECT_EQ(self, 1);
    EXPECT_EQ(before, 2);
    EXPECT_EQ(after, 2);
}

TEST(LookTokens, UnknownTokenIsIgnored) {
    Look look;
    int n = 0;
    const int t = look.subscribe([&] { ++n; });
    look.unsubscribe(0);
    look.unsubscribe(t + 100);
    look.unsubscribe(-5);
    look.setScheme(PI_GLOBAL_COLOR_SCHEME_DUSK);
    EXPECT_EQ(n, 1);
    look.unsubscribe(t);
    look.unsubscribe(t); // twice is also unknown
    look.setScheme(PI_GLOBAL_COLOR_SCHEME_NIGHT);
    EXPECT_EQ(n, 1);
}

TEST(LookTokens, TokensAreNeverReused) {
    Look look;
    const int t1 = look.subscribe([] {});
    const int t2 = look.subscribe([] {});
    EXPECT_NE(t1, t2);
    look.unsubscribe(t2);
    const int t3 = look.subscribe([] {});
    EXPECT_NE(t3, t1);
    EXPECT_NE(t3, t2);
    // a stale token must not remove the newer subscriber
    int n = 0;
    const int t4 = look.subscribe([&] { ++n; });
    look.unsubscribe(t2);
    look.setScheme(PI_GLOBAL_COLOR_SCHEME_DUSK);
    EXPECT_EQ(n, 1);
    EXPECT_NE(t4, t2);
}

// ---- LastAreaKind / LastRect ----

TEST_F(Store, LastAreaKindAndRectRoundTripPerKindInTheSpecFormat) {
    ConfigStore::LastChoice c;
    c.areaKind = cg::AreaKind::Rectangle;
    c.rectangle = cg::BBox{-1.5, 50.25, 2.125, 51.0};
    store_->saveLastChoice(c);
    const std::string file = slurp(conf_);
    EXPECT_NE(file.find("LastAreaKind=rectangle"), std::string::npos) << file;
    EXPECT_NE(file.find("LastRect=-1.50000000,50.25000000,2.12500000,51.00000000"), std::string::npos)
        << file;
    config_.reset();
    open();
    auto r = store_->loadLastChoice();
    EXPECT_EQ(r.areaKind, cg::AreaKind::Rectangle);
    ASSERT_TRUE(r.rectangle.has_value());
    EXPECT_EQ(r.rectangle->west, -1.5);
    EXPECT_EQ(r.rectangle->south, 50.25);
    EXPECT_EQ(r.rectangle->east, 2.125);
    EXPECT_EQ(r.rectangle->north, 51.0);

    c.areaKind = cg::AreaKind::Route; // a rectangle is not written for another kind
    store_->saveLastChoice(c);
    EXPECT_NE(slurp(conf_).find("LastAreaKind=route"), std::string::npos);
    r = store_->loadLastChoice();
    EXPECT_EQ(r.areaKind, cg::AreaKind::Route);
    EXPECT_FALSE(r.rectangle.has_value());
    EXPECT_EQ(config_->Read("/PlugIns/ChartGrabber/LastRect", "unset"), "");

    c.areaKind = cg::AreaKind::View;
    store_->saveLastChoice(c);
    EXPECT_NE(slurp(conf_).find("LastAreaKind=view"), std::string::npos);
    r = store_->loadLastChoice();
    EXPECT_EQ(r.areaKind, cg::AreaKind::View);
    EXPECT_FALSE(r.rectangle.has_value());
    EXPECT_EQ(config_->Read("/PlugIns/ChartGrabber/LastRect", "unset"), "");
}

TEST_F(Store, RectangleKindWithoutARectangleWritesNoRect) {
    ConfigStore::LastChoice c;
    c.areaKind = cg::AreaKind::Rectangle;
    store_->saveLastChoice(c);
    const auto r = store_->loadLastChoice();
    EXPECT_EQ(r.areaKind, cg::AreaKind::Rectangle);
    EXPECT_FALSE(r.rectangle.has_value());
    EXPECT_EQ(config_->Read("/PlugIns/ChartGrabber/LastRect", "unset"), "");
}

TEST_F(Store, DefaultAreaKindIsViewWithoutRectangle) {
    const auto r = store_->loadLastChoice();
    EXPECT_EQ(r.areaKind, cg::AreaKind::View);
    EXPECT_FALSE(r.rectangle.has_value());
}

TEST_F(Store, UnknownOrDifferentlyCasedAreaKindLoadsAsView) {
    for (const char* text : {"", "Rectangle", "ROUTE", "circle", "rectangle ", "1"}) {
        config_->Write("/PlugIns/ChartGrabber/LastAreaKind", wxString(text));
        EXPECT_EQ(store_->loadLastChoice().areaKind, cg::AreaKind::View) << "[" << text << "]";
    }
    config_->Write("/PlugIns/ChartGrabber/LastAreaKind", wxString("route"));
    EXPECT_EQ(store_->loadLastChoice().areaKind, cg::AreaKind::Route);
}

TEST_F(Store, ARectangleIsLoadedWhateverTheKindSays) {
    config_->Write("/PlugIns/ChartGrabber/LastAreaKind", wxString("view"));
    config_->Write("/PlugIns/ChartGrabber/LastRect", wxString("-180,-85.0511,180,85.0511")); // the extremes
    const auto r = store_->loadLastChoice();
    EXPECT_EQ(r.areaKind, cg::AreaKind::View);
    ASSERT_TRUE(r.rectangle.has_value());
    EXPECT_EQ(r.rectangle->west, -180);
    EXPECT_EQ(r.rectangle->north, 85.0511);
}

TEST_F(Store, EveryRefusedLastRectFormGivesNoRectangle) {
    const std::vector<std::string> refused = {
        "",              // empty
        "1,2,3",         // 3 parts
        "1,2,3,4,5",     // 5 parts
        "1,2,3,4,",      // a trailing empty part counts
        "1,,3,4",        // an empty part
        "1,2,3,",        // an empty last part
        ",1,2,3",        // an empty first part
        "x,2,3,4",       // not a number
        "1,2,3,4x",      // trailing garbage in a part
        "nan,1,2,3",     // not finite
        "1,2,inf,4",     //

        "-inf,1,2,3",    //

        " 1,2,3,4",      // no trimming
        "1,2,3,4 ",      //

        "3,1,3,2",       // W = E
        "4,1,3,2",       // W > E
        "1,2,3,2",       // S = N
        "1,3,3,2",       // S > N
        "-181,1,2,3",    // W < -180
        "0,1,181,3",     // E > 180
        "0,-86,1,3",     // S < -85.0511
        "0,0,1,85.06",   // N > 85.0511
        "1;2;3;4",       // wrong separator
        "1.5,2,3,4,5,6", // many parts
    };
    for (const std::string& text : refused) {
        config_->Write("/PlugIns/ChartGrabber/LastRect", wxString::FromUTF8(text));
        EXPECT_FALSE(store_->loadLastChoice().rectangle.has_value()) << "[" << text << "]";
    }
}

// : the job directories loadCharts cannot use, newest first; readable ones, stray files
// and a missing jobs folder give none.
TEST_F(Store, UnreadableChartIdsAreTheDirectoriesWithoutAUsableChartIniNewestFirst) {
    EXPECT_TRUE(store_->unreadableChartIds().empty()) << "no jobs folder";
    const fs::path jobs = store_->jobsDir();
    placeChart(jobs, record("20261014-090000", "fine"));
    fs::create_directories(jobs / "20261012-000000");          // no chart.ini
    spit(jobs / "20261017-000000" / "chart.ini", "Zooms=1\n"); // no Name
    spit(jobs / "20261015-000000" / "chart.ini", "");          // empty
    spit(jobs / "stray-file.txt", "x");
    EXPECT_EQ(store_->unreadableChartIds(),
              (std::vector<std::string>{"20261017-000000", "20261015-000000", "20261012-000000"}));
    EXPECT_EQ(store_->loadCharts().size(), 1u);
}

} // namespace
