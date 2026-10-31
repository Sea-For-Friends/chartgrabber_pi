// SPDX-License-Identifier: GPL-3.0-or-later
// Plugin settings value type and the CLI's default executable name.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace cg {

// The CLI's default executable name. This is the ONLY place in the code that names it.
inline constexpr std::string_view kDefaultCliName = "chart-grabber-cli";
// Where the package installs the CLI.
inline constexpr std::string_view kCliInstallDir = "/usr/bin/";
// The folder of the CLI in Program Files on Windows.
inline constexpr std::string_view kCliWindowsFolder = "Chart Grabber CLI";

struct Settings {
    std::string cliPath; // empty: the automatic places (cg::defaultCliPaths: bundled, /usr/bin), else
                         // kDefaultCliName on PATH
    std::string
        mapsDir; // the chart-sources folder; empty: <home>/Charts/Chart sources (ConfigStore::sourcesDir)
    std::vector<std::string> cliMaps; // only in the cliSettings copy: the folders passed as --maps, in order
                                      // (bundled sources, then the sailor's folder when it holds a source)
    std::string cacheDir;             // empty: --cache not passed
    std::string cliDataDir;  // only in the cliSettings copy: --datadir=DIR for every run; empty: not passed
    std::string chartDir;    // empty: <home>/Charts/ChartGrabber
    std::string proxy;       // empty: not passed
    int threads = 2;         // 1 in slow mode
    double corridorNm = 2.0; // total width (app-cli Q-5)
    bool slowConnection = false; //: 1 thread, no auto-retry, budget on
    int dataBudgetMb = 100;      // applies only when slowConnection
    bool registerCharts = true;
    bool stripOnChart =
        false; // key StripOnChart: the progress strip is a child of the chart canvas
    bool debugLog = false;        // key DebugLog: debug lines in the plugin's log file
    bool warnDetailLevels = true; // key WarnDetailLevels: the extreme-level warning
};

} // namespace cg
