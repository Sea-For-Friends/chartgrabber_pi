// SPDX-License-Identifier: GPL-3.0-or-later
#include "folder_facts.h"

#include <algorithm>
#include <utility>

namespace cg {

FolderFactsCache::FolderFactsCache(Measure measure, Clock clock)
    : measure_(std::move(measure)), clock_(std::move(clock)) {
    if (!clock_)
        clock_ = [] { return std::chrono::steady_clock::now(); };
}

FolderFacts FolderFactsCache::get(const std::string& folder) {
    const auto now = clock_();
    const auto it =
        std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.folder == folder; });
    if (it != entries_.end()) {
        if (now >= it->taken && now - it->taken < kTtl)
            return it->facts;
        entries_.erase(it);
    }
    if (entries_.size() >= kMaxEntries)
        entries_.erase(entries_.begin());
    entries_.push_back({folder, measure_(folder), now});
    return entries_.back().facts;
}

void FolderFactsCache::drop(const std::string& folder) {
    std::erase_if(entries_, [&](const Entry& e) { return e.folder == folder; });
}

void FolderFactsCache::clear() {
    entries_.clear();
}

} // namespace cg
