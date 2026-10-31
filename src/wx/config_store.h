// SPDX-License-Identifier: GPL-3.0-or-later
// OpenCPN's config as the plugin's settings, last choices and My charts records.
// Group /PlugIns/ChartGrabber. Values are core types; no user-visible text.
#pragma once

#include "job_files.h"
#include "settings.h"
#include "strip_place.h"
#include "types.h"

#include <optional>
#include <set>
#include <string>
#include <vector>

class wxFileConfig;

class ConfigStore {
public:
    explicit ConfigStore(wxFileConfig* config);

    cg::Settings loadSettings() const;
    void saveSettings(const cg::Settings& s);

    struct LastChoice {
        std::string sourceGuid;
        bool seamarks = true;
        bool weather = false;    // LastWeather
        std::string weatherGuid; // LastWeatherSource
        std::set<cg::Band> bands;
        std::string advancedZooms;
        cg::AreaKind areaKind = cg::AreaKind::View;
        std::optional<cg::BBox> rectangle;
        std::string routeGuid;
    };
    LastChoice loadLastChoice() const;
    void saveLastChoice(const LastChoice& c);

    bool disclaimerShown() const;
    void setDisclaimerShown();
    void setWarnDetailLevels(bool on); // key WarnDetailLevels only, then Flush
    bool firstSuccessShown() const;    // key FirstSuccessShown
    void setFirstSuccessShown();

    // The job strip's place : nullopt when a
    // key is missing or invalid (an invalid one is logged once per session); nullopt to set deletes the keys.
    std::optional<cg::StripAnchor> stripAnchor() const;
    void setStripAnchor(std::optional<cg::StripAnchor> a);
    bool stripHintShown() const; // key StripHintShown: the drag line stands in Details until the first drop
    void setStripHintShown();

    std::string chartDir(const cg::Settings& s) const; // chartDir or <home>/Charts/ChartGrabber
    std::string jobsDir() const;                       // private data dir + /plugins/chartgrabber/jobs
    // cacheDir, else <private data>/plugins/chartgrabber/cache (null location: <home>/.opencpn/...); decision
    // 0003
    std::string cliCacheRoot(const cg::Settings& s) const;
    // <private data>/plugins/chartgrabber/cli-data (empty/null location: <home>/.opencpn/...): --datadir of
    // every CLI run, so the CLI never writes its own home folders
    std::string cliDataDir() const;
    // mapsDir, else <home>/Charts/<cg::kSourcesFolderName>; never created by the plugin
    std::string sourcesDir(const cg::Settings& s) const;
    // The bundled sources folder: set once by Init; empty until set.
    void setBundledSourcesDir(std::string dir);
    std::string bundledSourcesDir() const;
    // s with cacheDir and cliDataDir resolved and cliMaps = {bundled folder} + {sourcesDir(s) when it holds a
    // chart source}: every argument builder call in src/wx takes this copy
    cg::Settings cliSettings(const cg::Settings& s) const;
    std::vector<cg::ChartRecord> loadCharts() const; // every <jobsDir>/*/chart.ini, newest first
    std::vector<std::string>
    unreadableChartIds() const; // job dirs whose chart.ini is missing or does not parse
    void saveChart(const cg::ChartRecord& r);
    void deleteChartDir(const std::string& id);
    static void clearCacheDirs(const std::string& root); // cache* sub-directories, symlinks skipped

private:
    wxFileConfig* config_; // owned by OpenCPN
    std::string bundledSourcesDir_;
    mutable std::set<std::string> invalidLogged_; // keys whose invalid value was logged (once per session)
};
