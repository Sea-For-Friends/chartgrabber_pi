// SPDX-License-Identifier: GPL-3.0-or-later
// The plugin's own tab-separated data files (".conf" family: source-descriptions.conf,
// favourite-sources.conf). They live beside the bundled sources folder, never inside it.
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace cg {

// UTF-8; a BOM at the start is skipped; LF or CRLF (a trailing '\r' is removed); a line that is empty or
// whose first byte is '#' is skipped; fields are split on TAB. The first other line may be `format<TAB>N` (N
// decimal, > 0).
struct ConfRow {
    int line = 0; // 1-based line number in the file
    std::vector<std::string> fields;
};
struct ConfFile {
    bool found = false;        // readConf: opened and read; parseConf: always true
    int format = 0;            // 0: no format line; a `format` line with a bad N: -1
    std::vector<ConfRow> rows; // the other lines, in file order
};
ConfFile parseConf(std::string_view text);
ConfFile readConf(const std::string& utf8Path); // missing or unreadable: found = false

// <parent of bundledSourcesDir>/<name>; a trailing '/' or '\\' of the dir is removed first; "" when the dir
// is empty.
std::string confBesideSources(const std::string& bundledSourcesDir, std::string_view name);

bool looksLikeGuid(std::string_view s); // '{' 8-4-4-4-12 hex digits (either case) '}'

using SourceDescriptions = std::map<std::string, std::string>; // key: GUID, ASCII upper-cased

// Format 0 or 1: read; another format: still read with one problem line. A row is bad when field 1 is not a
// GUID or field 2 is missing or empty: skipped, one problem "line N: <reason>". A repeated GUID: the first
// wins, the repeat is a problem.
SourceDescriptions descriptionsFrom(const ConfFile& f, std::vector<std::string>& problems);

// The default Chart / Weather on top lists and the seamark layer; the same
// format can be a user file later. Rows: <role><TAB><GUID>[<TAB>name, ignored]; roles chart, weather,
// seamarks.
struct FavouriteSources {
    std::vector<std::string> chart, weather; // GUIDs in file order, duplicates inside a role dropped
    std::string seamarks;                    // "" when no usable seamarks line
};
struct FavouriteProblem {
    enum class Kind {
        FileNotFound,
        NoFormatLine,  // the lines are still used
        UnknownFormat, // no line is used
        NoEntries,
        TooFewFields,
        UnknownRole,
        NotGuid,
        Repeated,
        SecondSeamarks,
    };
    Kind kind;
    int line = 0; // 1-based, for the row problems; 0 otherwise
};
// Every good line is used and every problem reported, except an unknown format (not 0, not 1): no line is
// used. Problems come in file order (the format first, "no entries" last); the first repeat / second
// seamarks line loses, the first wins.
FavouriteSources favouriteSourcesFrom(const ConfFile& f, std::vector<FavouriteProblem>& problems);

// https://github.com/Sea-For-Friends/chartgrabber_pi/blob/v<version>/data/favourite-sources.conf
std::string favouriteSourcesUrl(std::string_view pluginVersion);

} // namespace cg
