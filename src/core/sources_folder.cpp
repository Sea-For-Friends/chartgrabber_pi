// SPDX-License-Identifier: GPL-3.0-or-later
#include "sources_folder.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <filesystem>
#include <vector>

namespace cg {

namespace {

namespace fs = std::filesystem;

constexpr int kMaxFolderDepth = 6;        // the CLI's walk
constexpr int kMaxVisitedEntries = 20000; // a guard against a root such as the home folder

bool isZmpName(const std::string& name) {
    constexpr std::string_view ext = ".zmp";
    if (name.size() < ext.size())
        return false;
    return std::equal(ext.begin(), ext.end(), name.end() - ext.size(),
                      [](char a, char b) { return a == std::tolower(static_cast<unsigned char>(b)); });
}

struct Pending {
    fs::path path;
    int depth;
};

} // namespace

int countZmpEntries(const std::string& root, int limit) {
    std::error_code ec;
    if (limit <= 0 || !fs::is_directory(root, ec))
        return 0;
    int count = 0, visited = 0;
    std::deque<Pending> folders{{fs::path(root), 0}};
    while (!folders.empty()) {
        const Pending folder = folders.front();
        folders.pop_front();
        std::vector<fs::directory_entry> entries;
        std::error_code it_ec;
        fs::directory_iterator it(folder.path, it_ec), end;
        if (it_ec)
            continue; // unreadable: skipped
        for (; it != end; it.increment(it_ec)) {
            if (it_ec)
                break;
            entries.push_back(*it);
        }
        std::ranges::sort(entries, {},
                          [](const fs::directory_entry& e) { return e.path().filename().string(); });
        for (const fs::directory_entry& e : entries) {
            if (++visited > kMaxVisitedEntries)
                return count;
            const std::string name = e.path().filename().string();
            std::error_code dir_ec;
            const bool isDir = fs::is_directory(e.path(), dir_ec); // follows links, as QDir does
            if (folder.depth < kMaxFolderDepth && isDir && !isZmpName(name) && name.front() != '.')
                folders.push_back({e.path(), folder.depth + 1});
            if (isZmpName(name) && ++count >= limit)
                return count;
        }
    }
    return count;
}

} // namespace cg
