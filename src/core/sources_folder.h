// SPDX-License-Identifier: GPL-3.0-or-later
// The chart-sources folder. No wx; std only.
#pragma once

#include <string>
#include <string_view>

namespace cg {

inline constexpr std::string_view kSourcesFolderName = "Chart sources"; // default <home>/Charts/<this>

// How many chart-source entries (names ending in .zmp, file or folder, or a regular file ending in
// .source.json, a link to one counts; ASCII case-insensitive; a FOLDER named x.source.json is not counted and
// is entered) the CLI would load from root, by the CLI's own breadth-first walk (core
// zmp_file_names_iterator_factory.cpp at 37c15672, the .source.json part at 9e9f0eee): entries of a folder in
// name order; a folder not ending in .zmp and not starting with '.' is entered while its parent's depth < 6;
// symbolic links are followed; an unreadable folder is skipped. Stops at `limit` entries. A missing root or a
// root that is not a folder gives 0. At most 20000 folder entries are visited in total. Never throws, never
// opens a file.
int countSourceEntries(const std::string& root, int limit);

} // namespace cg
