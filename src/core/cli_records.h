// SPDX-License-Identifier: GPL-3.0-or-later
// CLI records (cli-requirements.md sections 2 and 3) and everything derived from them: line splitting, sums,
// exit-code meaning, preflight, retry policy, source refusal. Never
// throws.
#pragma once

#include "settings.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace cg {

inline constexpr int kSupportedEpsg = 3857; // the CLI refuses every other projection (app-cli 1.7 S-5)

struct SourceRecord {
    std::string guid, kind, name, contentType;
    int epsg = 0;
    int cacheType = 0;
    std::string verdict, zmpFile, group;
    // field 10 is `origin` (always "user" since 2026.10.4): not read
    std::string refresh;            // field 11: "snapshot" / "static"; empty = static
    std::string license;            // field 12: the credit text, may be empty
    std::optional<int> maxZoom;     // field 13: decimal 0..23; empty / bad / out of range = nullopt
    bool navigationWarning = false; // field 14, the last of 15: "1" = not an official nautical chart
    bool isWeather() const { return refresh == "snapshot"; }
    bool usable() const { return epsg == kSupportedEpsg && verdict == "ok"; }
    bool isLayer() const { return kind == "layer"; }
};
struct EstimateRecord {
    std::string guid;
    int zoom = 0;
    std::int64_t tiles = 0, bboxTiles = 0;
    double sharePresent = 0, shareMarkedMissing = 0;
    std::int64_t bytesToDownload = 0;
    bool assumed = false;
    std::int64_t bytesOnDisk = 0;
};
struct FillRecord {
    std::string guid;
    int zoom = 0;
    std::int64_t tiles = 0, present = 0, markedMissing = 0, missing = 0, bytes = 0;
};
struct SessionRecord {
    std::string guid, time, file;
};
struct WrittenRecord {
    std::string guid, path;
    std::int64_t tiles = 0, bytes = 0;
    std::string snapshot; // field 5: ISO time of the picture the file holds; empty = none
};
struct SnapshotRecord {
    std::string guid, time, version; // the picture a run took (CR section 2)
};
using StdoutRecord =
    std::variant<SourceRecord, EstimateRecord, FillRecord, SessionRecord, WrittenRecord, SnapshotRecord>;

inline constexpr int kWeatherZoomCap = 9; // highest zoom ever asked of a weather source
// Weather: min(maxZoom or the cap, cap); other sources: maxZoom (nullopt = no limit).
std::optional<int> zoomLimit(const SourceRecord& r);
// Exit 4's text: lastError starts with "cannot take the snapshot of " (byte compare).
bool isSnapshotFailure(std::string_view lastError);
// Seconds since 1970-01-01 UTC of "yyyy-MM-ddTHH:mm:ssZ" or "yyyy-MM-dd" (midnight); anything else nullopt.
std::optional<std::int64_t> snapshotEpoch(std::string_view iso);
// "2026-10-15T20:10:00Z" -> "2026-10-15 20:10 UTC"; any other text unchanged.
std::string snapshotTimeText(std::string_view iso);

// Why a source cannot be used. Projection is decided from the `epsg` field FIRST, whatever the verdict says
// (the CLI checks "not bitmap" and "unknown content type" before the projection, so a non-3857 map's verdict
// may name another reason). The verdict text is never parsed.
struct SourceRefusal {
    enum class Kind { None, Projection, Verdict } kind = Kind::None;
    int epsg = 0;        // Projection: the record's epsg
    std::string verdict; // Verdict: the record's verdict text, unchanged
};
SourceRefusal sourceRefusal(const SourceRecord& r);
// Row order of "More sources": groups in order of first appearance, the empty group last; inside a group the
// usable rows first, then the refused ones, each in CLI order. Returns indices into `s`.
std::vector<std::size_t> sourcesDialogOrder(const std::vector<SourceRecord>& s);
// Imagery guess: the name contains, ASCII case-insensitive, "satellite", "imagery", "aerial", "ortho" or
// "photo", or the word "sat" (delimited by a non-letter or the ends; bytes >= 0x80 count as letters).
bool nameLooksLikeImagery(std::string_view name);

struct PhaseRecord {
    std::string name, guid;
};
struct DownloadProgress {
    std::string guid;
    int zoom = 0;
    std::int64_t processed = 0, total = 0, downloaded = 0, downloadedBytes = 0, elapsedSec = 0;
};
struct ExportProgress {
    std::string guid;
    int permille = 0;
};
struct MessageLine {
    enum class Kind { Error, Warning, Other } kind = Kind::Other;
    std::string text; // prefix removed
};
using StderrLine = std::variant<PhaseRecord, DownloadProgress, ExportProgress, MessageLine>;

// The CLI's default --assume-tile-kb (core job.h:39): meanTileBytes' fallback.
inline constexpr std::int64_t kAssumedTileBytes = 20 * 1024;

std::optional<StdoutRecord> parseStdoutLine(std::string_view line); // nullopt: unknown type / bad field
StderrLine parseStderrLine(std::string_view line);                  // bad protocol line -> Other

class LineSplitter {
public:
    void feed(std::string_view bytes,
              const std::function<void(std::string_view)>& onLine); // '\n', drops '\r'
    void finish(const std::function<void(std::string_view)>& onLine);

private:
    std::string pending_;
};

struct EstimateTotals {
    std::int64_t tiles = 0, bytesToDownload = 0, bytesOnDisk = 0;
    bool assumed = false;
};
EstimateTotals sumEstimates(const std::vector<EstimateRecord>& records); // all sources and zooms
struct FillTotals {
    std::int64_t tiles = 0, missing = 0, bytes = 0;
};
FillTotals sumFill(const std::vector<FillRecord>& records, std::string_view guid);
// Seconds left: elapsed * (total - processed) / processed, integer division, never negative; nullopt when
// processed == 0.
std::optional<std::int64_t> secondsLeft(const DownloadProgress& p);
// What the finest level costs and what one more would add, from the `estimate` records
// of the last exit-0 estimate (all sources). nullopt when hi >= kMaxZoom or no record has zoom == hi.
struct LevelCost {
    std::int64_t finestBytes = 0; // sum of bytesToDownload of the records with zoom == hi
    std::int64_t totalBytes = 0;  // sum of bytesToDownload of all records
    // 4 x sum of bytesOnDisk of the records with zoom == hi (the next level has 4x tiles)
    std::int64_t nextBytes = 0;
};
std::optional<LevelCost> levelCost(const std::vector<EstimateRecord>& records, int hi);
// mean bytes per tile: sum bytesOnDisk / sum tiles over the records with tiles > 0 (integer
// division); no such record -> kAssumedTileBytes.
std::int64_t meanTileBytes(const std::vector<EstimateRecord>& records);

enum class Outcome {
    Done,
    InternalError,
    UsageError,
    Refused,
    Stalled,
    StorageError,
    DownloadIncomplete,
    ExportFailed,
    Interrupted,
    Killed,
    StartFailed
};
// exitCode as wx reports it (>= 0 normal, < 0 = -signal). lastError: last MessageLine Error text.
Outcome classifyExit(int exitCode, std::string_view lastError, bool startFailed);
bool resumable(Outcome o); // Stalled, StorageError, DownloadIncomplete, Interrupted, Killed

enum class Preflight {
    Ok,
    TooLarge,
    ChartDiskFull,
    CacheDiskFull,
    OverBudget
}; // first failing, in this order
struct PreflightInput {
    EstimateTotals totals;
    std::uint64_t freeChartBytes = 0, freeCacheBytes = 0;
    bool sameFileSystem = false;
    const Settings* settings = nullptr;
};
inline constexpr std::int64_t kTooLargeTiles = 200000, kTooLargeBytes = 2'000'000'000;
inline constexpr std::uint64_t kDiskReserve = 512ull << 20;
// TooLarge: tiles > kTooLargeTiles or bytesOnDisk > kTooLargeBytes. Chart folder needs 2 x bytesOnDisk, cache
// folder needs bytesToDownload; on one file system the sum (reported as ChartDiskFull); each against
// free - kDiskReserve. OverBudget: slowConnection and bytesToDownload > dataBudgetMb * 1e6.
Preflight checkPreflight(const PreflightInput& in);

struct RetryState {
    int attempt = 0; // automatic retries done for this job run
};
inline constexpr int kMaxRetries = 3, kRetryDelaySec = 60;
// true: retry after kRetryDelaySec: outcome Stalled, !slowConnection, attempt < kMaxRetries.
bool shouldRetry(Outcome o, const Settings& s, const RetryState& st);

} // namespace cg
