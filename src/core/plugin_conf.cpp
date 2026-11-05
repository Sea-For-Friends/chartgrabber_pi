// SPDX-License-Identifier: GPL-3.0-or-later
#include "plugin_conf.h"

#include "portable.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <iterator>

namespace cg {

namespace {

std::string upperAscii(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string at(const std::string& what, int line) {
    std::string s = "line ";
    s += std::to_string(line);
    s += ": ";
    s += what;
    return s;
}

} // namespace

ConfFile parseConf(std::string_view text) {
    ConfFile f;
    f.found = true;
    if (text.substr(0, 3) == "\xEF\xBB\xBF")
        text.remove_prefix(3);
    bool first = true; // no non-skipped line seen yet: the format line may come
    int line = 0;
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        std::string_view l = text.substr(0, nl);
        text.remove_prefix(nl == std::string_view::npos ? text.size() : nl + 1);
        ++line;
        if (!l.empty() && l.back() == '\r')
            l.remove_suffix(1);
        if (l.empty() || l[0] == '#')
            continue;
        ConfRow row;
        row.line = line;
        for (std::size_t p = 0;;) {
            const std::size_t t = l.find('\t', p);
            row.fields.emplace_back(l.substr(p, t == std::string_view::npos ? t : t - p));
            if (t == std::string_view::npos)
                break;
            p = t + 1;
        }
        if (first && row.fields[0] == "format") {
            int n = 0;
            const std::string& v = row.fields.size() > 1 ? row.fields[1] : std::string();
            const auto r = std::from_chars(v.data(), v.data() + v.size(), n);
            f.format = (!v.empty() && r.ec == std::errc() && r.ptr == v.data() + v.size() && n > 0) ? n : -1;
        } else {
            f.rows.push_back(std::move(row));
        }
        first = false;
    }
    return f;
}

ConfFile readConf(const std::string& utf8Path) {
    std::ifstream in(fsPath(utf8Path), std::ios::binary);
    if (!in)
        return {};
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad())
        return {};
    return parseConf(text);
}

std::string confBesideSources(const std::string& bundledSourcesDir, std::string_view name) {
    std::string d = bundledSourcesDir;
    while (!d.empty() && (d.back() == '/' || d.back() == '\\'))
        d.pop_back();
    if (d.empty())
        return {};
    const std::size_t cut = d.find_last_of("/\\");
    std::string out = cut == std::string::npos ? std::string() : d.substr(0, cut + 1);
    out += name;
    return out;
}

bool looksLikeGuid(std::string_view s) {
    if (s.size() != 38 || s.front() != '{' || s.back() != '}')
        return false;
    for (std::size_t i = 1; i + 1 < s.size(); ++i) {
        const bool dash = i == 9 || i == 14 || i == 19 || i == 24;
        if (dash ? s[i] != '-' : !std::isxdigit(static_cast<unsigned char>(s[i])))
            return false;
    }
    return true;
}

SourceDescriptions descriptionsFrom(const ConfFile& f, std::vector<std::string>& problems) {
    SourceDescriptions out;
    if (f.format > 1) {
        std::string m = "format ";
        m += std::to_string(f.format);
        m += " is newer than 1: read as format 1";
        problems.push_back(std::move(m));
    } else if (f.format < 0)
        problems.push_back("bad format line: read as format 1");
    for (const ConfRow& r : f.rows) {
        if (!looksLikeGuid(r.fields[0])) {
            problems.push_back(at("not a GUID", r.line));
        } else if (r.fields.size() < 2 || r.fields[1].empty()) {
            problems.push_back(at("no text", r.line));
        } else if (!out.emplace(upperAscii(r.fields[0]), r.fields[1]).second) {
            problems.push_back(at("repeated GUID", r.line));
        }
    }
    return out;
}

FavouriteSources favouriteSourcesFrom(const ConfFile& f, std::vector<FavouriteProblem>& problems) {
    using Kind = FavouriteProblem::Kind;
    FavouriteSources out;
    if (!f.found) {
        problems.push_back({Kind::FileNotFound});
        return out;
    }
    if (f.format == 0) {
        problems.push_back({Kind::NoFormatLine});
    } else if (f.format != 1) {
        problems.push_back({Kind::UnknownFormat});
        return out;
    }
    std::vector<std::string> seenChart, seenWeather; // upper-cased
    const auto addOnce = [](std::vector<std::string>& list, std::vector<std::string>& seen,
                            const std::string& guid) {
        const std::string key = upperAscii(guid);
        if (std::find(seen.begin(), seen.end(), key) != seen.end())
            return false;
        seen.push_back(key);
        list.push_back(guid);
        return true;
    };
    for (const ConfRow& r : f.rows) {
        if (r.fields.size() < 2) {
            problems.push_back({Kind::TooFewFields, r.line});
            continue;
        }
        const std::string& role = r.fields[0];
        if (role != "chart" && role != "weather" && role != "seamarks") {
            problems.push_back({Kind::UnknownRole, r.line});
        } else if (!looksLikeGuid(r.fields[1])) {
            problems.push_back({Kind::NotGuid, r.line});
        } else if (role == "seamarks") {
            if (!out.seamarks.empty())
                problems.push_back({Kind::SecondSeamarks, r.line});
            else
                out.seamarks = r.fields[1];
        } else {
            const bool chart = role == "chart";
            if (!addOnce(chart ? out.chart : out.weather, chart ? seenChart : seenWeather, r.fields[1]))
                problems.push_back({Kind::Repeated, r.line});
        }
    }
    if (f.rows.empty())
        problems.push_back({Kind::NoEntries});
    return out;
}

std::string favouriteSourcesUrl(std::string_view pluginVersion) {
    std::string s = "https://github.com/Sea-For-Friends/chartgrabber_pi/blob/v";
    s += pluginVersion;
    s += "/data/favourite-sources.conf";
    return s;
}

} // namespace cg
