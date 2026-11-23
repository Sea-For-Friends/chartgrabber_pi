// SPDX-License-Identifier: GPL-3.0-or-later
// What the file system says about a folder, measured once and reused for a short time. No wx; std only.
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace cg {

// The facts of one folder, taken at one moment. A folder that does not exist reports its nearest existing
// ancestor: free space and device are those of the ancestor.
struct FolderFacts {
    bool exists = false;
    bool writable = false;                  // false when the folder does not exist
    std::string ancestor;                   // the nearest existing ancestor; empty when none exists
    std::optional<std::uint64_t> freeBytes; // nullopt = unknown
    std::optional<std::uint64_t>
        device; // st_dev; nullopt = unknown (Windows compares the volume of `ancestor`)
};

// A small cache of FolderFacts keyed by folder path. An entry answers for `kTtl` after it was measured; an
// older or missing entry is measured again. The cache holds at most `kMaxEntries` folders (the oldest goes).
// Not thread safe: the caller is the GUI thread.
class FolderFactsCache {
public:
    using Measure = std::function<FolderFacts(const std::string&)>;
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    static constexpr std::chrono::seconds kTtl{5};
    static constexpr std::size_t kMaxEntries = 4;

    // `clock` defaults to steady_clock::now.
    explicit FolderFactsCache(Measure measure, Clock clock = {});

    // The cached facts, or a new measurement when none is younger than kTtl.
    FolderFacts get(const std::string& folder);
    // Drops the entry of `folder` (the next get measures).
    void drop(const std::string& folder);
    // Drops every entry.
    void clear();

private:
    struct Entry {
        std::string folder;
        FolderFacts facts;
        std::chrono::steady_clock::time_point taken;
    };
    Measure measure_;
    Clock clock_;
    std::vector<Entry> entries_; // oldest first
};

} // namespace cg
