// SPDX-License-Identifier: GPL-3.0-or-later
#include "job_progress.h"

#include <algorithm>

namespace cg {

int jobPermille(const std::vector<std::pair<std::string, DownloadProgress>>& seen,
                const std::vector<std::string>& guids) {
    std::int64_t sum = 0, count = 0;
    for (const std::string& guid : guids) {
        if (guid.empty())
            continue;
        ++count;
        const auto it = std::ranges::find(seen, guid, &std::pair<std::string, DownloadProgress>::first);
        if (it != seen.end() && it->second.total > 0)
            sum += std::clamp<std::int64_t>(it->second.processed * 1000 / it->second.total, 0, 1000);
    }
    return count == 0 ? 0 : static_cast<int>(sum / count);
}

std::optional<int> frozenPermille(const ChartRecord& r) {
    switch (r.state) {
    case ChartState::Interrupted:
    case ChartState::Failed:
        if (r.done >= 1 && r.done <= 1000)
            return std::min(r.done, 999);
        return std::nullopt;
    case ChartState::Missing:
        if (r.tiles > 0 && r.missing >= 0 && r.missing <= r.tiles)
            return static_cast<int>((r.tiles - r.missing) * 1000 / r.tiles);
        return std::nullopt;
    case ChartState::Complete:
    case ChartState::Running:
        break;
    }
    return std::nullopt;
}

} // namespace cg
