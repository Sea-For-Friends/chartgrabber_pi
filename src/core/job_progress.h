// SPDX-License-Identifier: GPL-3.0-or-later
// The progress bar of a My charts row. No wx.
#pragma once

#include "cli_records.h"
#include "job_files.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cg {

// The job's download fraction in permille: the mean over `guids` (empty ones skipped) of processed * 1000 /
// total of the latest `seen` entry of that GUID; a GUID not seen, or with total <= 0, counts 0; each source
// is clamped to 0..1000; guids without a non-empty entry -> 0.
int jobPermille(const std::vector<std::pair<std::string, DownloadProgress>>& seen,
                const std::vector<std::string>& guids);

// The frozen bar of a row that is not running: Interrupted / Failed -> `done` when 1..1000, capped at 999;
// Missing -> (tiles - missing) * 1000 / tiles when tiles > 0 and 0 <= missing <= tiles; else nullopt.
std::optional<int> frozenPermille(const ChartRecord& r);

// What the running job is doing, for the live row (JobController::live).
enum class LiveStage { Sizing, Downloading, Checking, Building, RetryWait, Stopping };
struct LiveProgress {
    std::string chartId; // the running record's id
    LiveStage stage = LiveStage::Sizing;
    int permille = -1;                       // the bar: -1 = pulse; Building: the export permille
    int donePermille = 0;                    // the job's download permille so far
    std::optional<std::int64_t> secondsLeft; // Downloading
    int retrySeconds = 0;                    // RetryWait
};

} // namespace cg
