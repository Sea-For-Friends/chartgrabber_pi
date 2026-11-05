// SPDX-License-Identifier: GPL-3.0-or-later
// Everything the plugin writes for the CLI or for itself, as exact text: job file, argument vectors, chart
// record, route GPX, program lookup. No wx.
#pragma once

#include "portable.h"
#include "settings.h"
#include "types.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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
    // [Download] Threads of job.ini (set by the caller from Settings::threads, 1 in slow mode).
    int threads = 2;
};

// Fixed notation, '.', whatever the C and C++ locales are (a classic-locale stream: the floating <charconv>
// overloads are missing in Apple libc++ before macOS 13.3 / 26.0).
std::string formatNumber(double value, int decimals);
// Whole text is one number as std::from_chars(double) accepts it (no space, no '+', no hex; "inf", "nan");
// false otherwise, also out of range. Locale-independent. Known difference: macOS libc++ streams reject
// subnormals ("1e-310", ERANGE) that from_chars accepts; irrelevant for coordinates and shares.
bool parseDouble(std::string_view text, double& out);
std::string jobIni(const JobSpec& job); // exact text
// `--format=tsv` always; `--progress=lines` for estimate, run and export.
// The path values (--cache, --maps, --route, --job) are in the form of `os` (native separators on
// Windows); the default is this system.
// the `--version` probe: "--version" and, when Settings::cliDataDir is set, "--datadir=<dir>" (the
// CLI writes nothing outside its data folder).
std::vector<std::string> versionArguments(const Settings& s, Os os = kHostOs);
std::vector<std::string> sourcesArguments(const Settings& s, Os os = kHostOs);
std::vector<std::string> estimateArguments(const JobSpec& job, const Settings& s, Os os = kHostOs);
enum class RunKind { Download, BuildFromSaved, Update };
// Update: the Download argv plus "--replace" and "--replace-older-than=<replaceOlderThan>";
// an empty date gives the Download argv.
std::vector<std::string> runArguments(const std::string& jobIniPath, RunKind kind, bool overwrite,
                                      const Settings& s, std::string_view replaceOlderThan = {},
                                      Os os = kHostOs);
// Local time minus monthsBack calendar months, "YYYY-MM-DDThh:mm:ss"; the day is clamped to the target month.
std::string olderThanDate(int y, int mo, int d, int h, int mi, int s, int monthsBack);
// "nice", "-n", "10", program, args...
std::vector<std::string> niced(const std::string& program, const std::vector<std::string>& args);
// The argv of every child: "env", "LC_ALL=C.UTF-8", then niced(program, args) when `nice`, else
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
// A source name for a drop-down: at most 50 code points; longer: the first 47 + "..." (UTF-8, cut
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
    std::vector<std::string> files; // written .mbtiles, absolute
    ChartState state = ChartState::Running;
    std::int64_t tiles = 0, missing = 0, bytes = 0;
    std::string lastError;
    std::string updateBefore; // chart.ini UpdateBefore: --replace-older-than date of an unfinished update
    bool weather = false;     // chart.ini Weather: the chart's source is a weather (snapshot) source
    std::string snapshot;     // chart.ini Snapshot: ISO time of the written file's picture
    std::string modelRun;     // chart.ini ModelRun: the picture's model run; empty = none
    std::string credit;       // chart.ini Credit: the source's credit text
    int done = 0;             // chart.ini Done: how far the job got, permille 0..1000; 0 = unknown
    std::vector<std::string> stale; // chart.ini Stale0..n: old files of a Windows weather update
};
std::string chartIni(const ChartRecord& r); // exact text
std::optional<ChartRecord> parseChartIni(std::string_view text, std::string id);
// Does a weather update's file hold a new picture? Previous = the record when the job started.
// An empty new run (older CLI without the model run) leaves the time alone to decide.
bool pictureChanged(std::string_view prevSnapshot, std::string_view prevModelRun,
                    std::string_view newSnapshot, std::string_view newModelRun);

// The charts "Update all weather charts" runs: weather records not Running, order kept.
std::vector<ChartRecord> weatherBatch(const std::vector<ChartRecord>& charts);

// The file a Windows weather update writes: the first of "<base>.mbtiles", "<base>
// (2).mbtiles"
// ... "<base> (9).mbtiles" that is not `current` (byte compare after nativeSeparators(.., Os::Windows) of
// both and ASCII case folding) and for which exists(path) is false. <base> = `current` without the last
// ".mbtiles" (ASCII case-insensitive) and without one trailing " (N)", N = 2..9; same folder as `current`,
// native separators of `os`. nullopt when all ten are taken or `current` does not end in ".mbtiles".
std::optional<std::string> alternateWeatherFile(const std::string& current,
                                                const std::function<bool(const std::string&)>& exists,
                                                Os os = kHostOs);
// ---- job.ini (§8.1) ----
// `text` with the value of the line starting "Out=" replaced by `out` and, when `overlayOut` is not empty,
// the value of the line starting "OverlayOut=" replaced by it; every other byte unchanged; CR/LF in a value
// become a space (8.1). nullopt when there is no "Out=" line, or `overlayOut` is not empty and there is no
// "OverlayOut=" line.
std::optional<std::string> rewriteJobOutputs(std::string_view text, std::string_view out,
                                             std::string_view overlayOut);
// Tries remove(p) for each path in order; returns the paths that remain (remove(p) false), in order.
// A path for which exists(p) is false counts as removed.
std::vector<std::string> removeStale(const std::vector<std::string>& stale,
                                     const std::function<bool(const std::string&)>& exists,
                                     const std::function<bool(const std::string&)>& remove);

// OpenCPN's "OpenCPN Config" plugin message (5.10.2 pluginmanager.cpp:2084-2113, wxJSONWriter text):
// the integers after "OpenCPN Version Major" and "OpenCPN Version Minor" (key in double quotes, optional
// whitespace, ':', optional whitespace, decimal digits). nullopt when either key or its integer is missing.
std::optional<std::pair<int, int>> openCpnVersion(std::string_view configMessage);
// RemoveChartFromDBInPlace is safe: major == 5 && minor < 14 (5.14.0 added the worker pool that
// hangs it).
bool chartListRemoveIsSafe(int major, int minor);
// The path under which OpenCPN lists `file` when its folder is listed as `listedDir`
// (wxDir::GetAllFiles builds "<dir as given><separator><name>"): listedDir without trailing '/' or '\', the
// separator of `os`, the last path component of `file`.
std::string chartListPath(const std::string& listedDir, const std::string& file, Os os = kHostOs);

} // namespace cg
