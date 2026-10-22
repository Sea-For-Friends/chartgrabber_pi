// SPDX-License-Identifier: GPL-3.0-or-later
// Everything the plugin writes for the CLI or for itself, as exact text: job file, argument vectors, chart
// record, route GPX, program lookup. No wx.
#pragma once

#include "portable.h"
#include "settings.h"
#include "types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cg {

struct JobSpec {
    std::string sourceGuid, overlayGuid; // overlay empty = none
    AreaKind areaKind = AreaKind::View;
    BBox bbox; // View / Rectangle
    std::string routeGpxPath;
    double corridorNm = 0;               // Route (absolute path)
    std::vector<int> zooms;              // sorted, unique
    std::string outFile, overlayOutFile; // absolute
    std::string name;                    // MBTiles name of the base file; no '\''
    // [Download] Threads of job.ini (lists the key but JobSpec of 4.1 has no field for it; added
    // here, the caller sets it from Settings::threads, 1 in slow mode).
    int threads = 2;
};

std::string formatNumber(double value, int decimals); // std::to_chars, fixed, '.', locale-independent
std::string jobIni(const JobSpec& job);               // exact text
// `--format=tsv` always; `--progress=lines` for estimate, run and export.
// The path values (--cache, --maps, --route, --job) are in the form of `os` (native separators on
// Windows); the default is this system.
std::vector<std::string> sourcesArguments(const Settings& s, Os os = kHostOs);
std::vector<std::string> estimateArguments(const JobSpec& job, const Settings& s, Os os = kHostOs);
enum class RunKind { Download, BuildFromSaved, Update };
// Update: the Download argv plus "--replace" and "--replace-older-than=<replaceOlderThan>" ;
// an empty date gives the Download argv.
std::vector<std::string> runArguments(const std::string& jobIniPath, RunKind kind, bool overwrite,
                                      const Settings& s, std::string_view replaceOlderThan = {},
                                      Os os = kHostOs);
// Local time minus monthsBack calendar months, "YYYY-MM-DDThh:mm:ss"; the day is clamped to the target month.
std::string olderThanDate(int y, int mo, int d, int h, int mi, int s, int monthsBack);
// "nice", "-n", "10", program, args...
std::vector<std::string> niced(const std::string& program, const std::vector<std::string>& args);
// The argv of every child : "env", "LC_ALL=C.UTF-8", then niced(program, args) when `nice`, else
// program, args... (env and nice both exec: the pid stays the CLI's).
// Windows: {program} + args, no env and no nice (the runner sets the priority, 18.4).
std::vector<std::string> cliCommand(const std::string& program, const std::vector<std::string>& args,
                                    bool nice, Os os = kHostOs);
// Posix: shell-quoted, for Details; Windows: windowsCommandLine(argv), the line the CLI got.
std::string displayCommand(const std::vector<std::string>& argv, Os os = kHostOs);
std::string defaultChartName(const std::string& place, const std::string& sourceName); // "<place> - <source>"
std::string areaPlace(const BBox& box); // "Area 50.75N 1.30W" (centre, 2 decimals)
// One rule on every system: '\'' removed; each of / \ : * ? " < > | and every byte < 0x20 or 0x7f
// -> '_'; a Windows device name (CON, NUL, COM1...) gets a '_' in front; ".mbtiles".
std::string chartFileName(const std::string& chartName);
std::string seamarksName(const std::string& chartName); // "<name> - Seamarks"
std::string weatherName(const std::string& chartName);  // "<name> - Weather"
// A source name for a drop-down : at most 50 code points; longer: the first 47 + "..." (UTF-8, cut
// at a code point boundary).
std::string choiceLabel(std::string_view name);
std::string routeGpx(const std::string& name, const std::vector<LatLon>& points); // GPX 1.1, one <rte>
// Posix: ':' separated, access(X_OK). Windows: ';' separated, quotes around an entry removed, ".exe" added to
// the name when missing.
std::optional<std::string> findOnPath(std::string_view name, std::string_view pathEnv, Os os = kHostOs);

enum class ChartState { Running, Complete, Missing, Interrupted, Failed };
struct ChartRecord {
    std::string id; // job directory name: yyyyMMdd-HHmmss[-n]
    std::string name, sourceGuid, overlayGuid, sourceName;
    BBox bbox;
    std::vector<int> zooms;
    std::string created;            // ISO local time
    std::vector<std::string> files; // written.mbtiles, absolute
    ChartState state = ChartState::Running;
    std::int64_t tiles = 0, missing = 0, bytes = 0;
    std::string lastError;
    std::string updateBefore; // chart.ini UpdateBefore: --replace-older-than date of an unfinished update
    bool weather = false;     // chart.ini Weather: the chart's source is a weather (snapshot) source
    std::string snapshot;     // chart.ini Snapshot: ISO time of the written file's picture
    std::string credit;       // chart.ini Credit: the source's credit text
    int done = 0;             // chart.ini Done: how far the job got, permille 0..1000; 0 = unknown
};
std::string chartIni(const ChartRecord& r); // exact text
std::optional<ChartRecord> parseChartIni(std::string_view text, std::string id);

} // namespace cg
