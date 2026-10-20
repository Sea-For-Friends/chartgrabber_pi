// SPDX-License-Identifier: GPL-3.0-or-later
#include "job_files.h"

#include "geo_math.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <system_error>
#include <unistd.h>

namespace cg {

namespace {

std::optional<int> parseInt(std::string_view t) {
    int v = 0;
    const auto r = std::from_chars(t.data(), t.data() + t.size(), v);
    if (t.empty() || r.ec != std::errc() || r.ptr != t.data() + t.size()) {
        return std::nullopt;
    }
    return v;
}

std::string bboxText(const BBox& b) {
    return formatNumber(b.west, 8) + ',' + formatNumber(b.south, 8) + ',' + formatNumber(b.east, 8) + ',' +
           formatNumber(b.north, 8);
}

std::optional<BBox> parseBBox(std::string_view text) {
    double v[4];
    for (int i = 0; i < 4; ++i) {
        const auto comma = text.find(',');
        const std::string_view part = text.substr(0, comma);
        if (part.empty() || (i < 3) != (comma != std::string_view::npos)) {
            return std::nullopt;
        }
        const auto r = std::from_chars(part.data(), part.data() + part.size(), v[i]);
        if (r.ec != std::errc() || r.ptr != part.data() + part.size()) {
            return std::nullopt;
        }
        text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
    }
    return BBox{v[0], v[1], v[2], v[3]};
}

std::string oneLine(std::string v) {
    std::replace(v.begin(), v.end(), '\n', ' ');
    std::replace(v.begin(), v.end(), '\r', ' ');
    return v;
}

void addIf(std::vector<std::string>& v, const char* flag, const std::string& value) {
    if (!value.empty()) {
        v.emplace_back(flag);
        v.push_back(value);
    }
}

constexpr const char* kStateNames[] = {"running", "complete", "missing", "interrupted", "failed"};

std::string xmlEscape(const std::string& s) {
    std::string out;
    for (const char c : s) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '\'':
            out += "&apos;";
            break;
        default:
            // Control characters are not allowed in XML 1.0 (tab, CR and LF included here: a name is one
            // line).
            out += static_cast<unsigned char>(c) < 0x20 || c == 0x7f ? ' ' : c;
        }
    }
    return out;
}

} // namespace

std::string formatNumber(double value, int decimals) {
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, value, std::chars_format::fixed, decimals);
    return std::string(buf, r.ptr);
}

std::string jobIni(const JobSpec& job) {
    // A value is one line: CR and LF become a space so a typed name cannot inject keys.
    const auto kv = [](const char* key, std::string value) {
        for (char& c : value) {
            if (c == '\r' || c == '\n') {
                c = ' ';
            }
        }
        return key + value + '\n';
    };
    std::string t = "[Job]\n";
    if (!job.sourceGuid.empty()) {
        t += kv("Source=", job.sourceGuid);
    }
    if (!job.overlayGuid.empty()) {
        t += kv("Overlay=", job.overlayGuid);
    }
    if (job.areaKind == AreaKind::Route) {
        t += kv("Route=", job.routeGpxPath);
        t += kv("CorridorNm=", formatNumber(job.corridorNm, 1));
    } else {
        t += kv("BBox=", bboxText(job.bbox));
    }
    t += kv("Zooms=", formatZoomList(job.zooms));
    const bool baseless = job.sourceGuid.empty();
    t += kv("Out=", baseless ? job.overlayOutFile : job.outFile);
    if (!baseless && !job.overlayGuid.empty()) {
        t += kv("OverlayOut=", job.overlayOutFile);
    }
    if (!baseless && !job.name.empty()) {
        t += kv("Name=", job.name);
    }
    t += "[Download]\n" + kv("Threads=", std::to_string(job.threads));
    return t;
}

namespace {
// One `--maps D` pair per entry of cliMaps, in order (the CLI lets a later folder win a duplicate GUID).
void addMaps(std::vector<std::string>& a, const Settings& s) {
    for (const std::string& d : s.cliMaps) {
        a.emplace_back("--maps");
        a.push_back(d);
    }
}
} // namespace

std::vector<std::string> sourcesArguments(const Settings& s) {
    std::vector<std::string> a{"sources"};
    a.emplace_back("--format=tsv");
    addMaps(a, s);
    addIf(a, "--cache", s.cacheDir);
    return a;
}

std::vector<std::string> estimateArguments(const JobSpec& job, const Settings& s) {
    std::vector<std::string> a{"estimate"};
    a.emplace_back("--format=tsv");
    a.emplace_back("--progress=lines");
    addMaps(a, s);
    addIf(a, "--cache", s.cacheDir);
    addIf(a, "--source", job.sourceGuid);
    addIf(a, "--overlay", job.overlayGuid);
    if (job.areaKind == AreaKind::Route) {
        addIf(a, "--route", job.routeGpxPath);
        a.emplace_back("--corridor-nm");
        a.push_back(formatNumber(job.corridorNm, 1));
    } else {
        a.emplace_back("--bbox");
        a.push_back(bboxText(job.bbox));
    }
    a.emplace_back("--zooms");
    a.push_back(formatZoomList(job.zooms));
    return a;
}

std::vector<std::string> runArguments(const std::string& jobIniPath, RunKind kind, bool overwrite,
                                      const Settings& s, std::string_view replaceOlderThan) {
    std::vector<std::string> a{kind == RunKind::BuildFromSaved ? "export" : "run", "--job", jobIniPath};
    a.emplace_back("--format=tsv");
    a.emplace_back("--progress=lines");
    addMaps(a, s);
    addIf(a, "--cache", s.cacheDir);
    addIf(a, "--proxy", s.proxy);
    if (s.slowConnection) {
        a.emplace_back("--threads");
        a.emplace_back("1");
    }
    if (overwrite) {
        a.emplace_back("--overwrite");
    }
    if (kind == RunKind::Update && !replaceOlderThan.empty()) { // the caller always passes a date
        a.emplace_back("--replace");
        a.push_back("--replace-older-than=" + std::string(replaceOlderThan));
    }
    return a;
}

std::string olderThanDate(int y, int mo, int d, int h, int mi, int s, int monthsBack) {
    int m0 = y * 12 + (mo - 1) - monthsBack;
    const int ny = m0 / 12, nm = m0 % 12 + 1;
    const bool leap = (ny % 4 == 0 && ny % 100 != 0) || ny % 400 == 0;
    static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const int last = nm == 2 && leap ? 29 : kDays[nm - 1];
    char buf[40];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d", ny, nm, std::min(d, last), h, mi, s);
    return buf;
}

std::vector<std::string> niced(const std::string& program, const std::vector<std::string>& args) {
    std::vector<std::string> a{"nice", "-n", "10", program};
    a.insert(a.end(), args.begin(), args.end());
    return a;
}

std::vector<std::string> cliCommand(const std::string& program, const std::vector<std::string>& args,
                                    bool nice) {
    std::vector<std::string> a{"env", "LC_ALL=C.UTF-8"};
    if (nice) {
        const std::vector<std::string> n = niced(program, args);
        a.insert(a.end(), n.begin(), n.end());
    } else {
        a.push_back(program);
        a.insert(a.end(), args.begin(), args.end());
    }
    return a;
}

std::string displayCommand(const std::vector<std::string>& argv) {
    std::string out;
    for (const std::string& arg : argv) {
        if (!out.empty()) {
            out += ' ';
        }
        const bool plain = !arg.empty() && std::all_of(arg.begin(), arg.end(), [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) ||
                   std::string_view("_@%+=:,./-").find(c) != std::string_view::npos ||
                   static_cast<unsigned char>(c) >= 0x80;
        });
        if (plain) {
            out += arg;
            continue;
        }
        out += '\'';
        for (const char c : arg) {
            out += c == '\'' ? std::string("'\\''") : std::string(1, c);
        }
        out += '\'';
    }
    return out;
}

std::string defaultChartName(const std::string& place, const std::string& sourceName) {
    return place + " - " + sourceName;
}

std::string areaPlace(const BBox& box) {
    const double lat = (box.south + box.north) / 2;
    const double lon = (box.west + box.east) / 2;
    return "Area " + formatNumber(std::fabs(lat), 2) + (lat < 0 ? 'S' : 'N') + ' ' +
           formatNumber(std::fabs(lon), 2) + (lon < 0 ? 'W' : 'E');
}

std::string chartFileName(const std::string& chartName) {
    std::string out;
    for (const char c : chartName) {
        if (c == '\'') {
            continue;
        }
        out += (c == '/' || static_cast<unsigned char>(c) < 0x20 || c == 0x7f) ? '_' : c;
    }
    return out + ".mbtiles";
}

std::string seamarksName(const std::string& chartName) {
    return chartName + " - Seamarks";
}

std::string weatherName(const std::string& chartName) {
    return chartName + " - Weather";
}

std::string choiceLabel(std::string_view name) {
    constexpr std::size_t kMax = 50, kKeep = 47;
    std::size_t points = 0, cut = name.size();
    for (std::size_t i = 0; i < name.size(); ++i) {
        if ((static_cast<unsigned char>(name[i]) & 0xC0) == 0x80) // a continuation byte
            continue;
        if (points == kKeep)
            cut = i;
        ++points;
    }
    return points <= kMax ? std::string(name) : std::string(name.substr(0, cut)) + "...";
}

std::string routeGpx(const std::string& name, const std::vector<LatLon>& points) {
    std::string t =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<gpx version=\"1.1\" creator=\"Chart Grabber\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
        "<rte>\n<name>" +
        xmlEscape(name) + "</name>\n";
    for (const LatLon& p : points) {
        t += "<rtept lat=\"" + formatNumber(p.lat, 8) + "\" lon=\"" + formatNumber(p.lon, 8) + "\"/>\n";
    }
    return t + "</rte>\n</gpx>\n";
}

std::optional<std::string> findOnPath(std::string_view name, std::string_view pathEnv) {
    while (!pathEnv.empty()) {
        const auto colon = pathEnv.find(':');
        const std::string_view dir = pathEnv.substr(0, colon);
        pathEnv = colon == std::string_view::npos ? std::string_view() : pathEnv.substr(colon + 1);
        if (dir.empty()) {
            continue;
        }
        const std::string candidate = std::string(dir) + '/' + std::string(name);
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec) && ::access(candidate.c_str(), X_OK) == 0) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::string chartIni(const ChartRecord& r) {
    std::string t;
    const auto line = [&t](const std::string& key, const std::string& value) {
        t += key + '=' + oneLine(value) + '\n';
    };
    line("Name", r.name);
    line("SourceGuid", r.sourceGuid);
    line("OverlayGuid", r.overlayGuid);
    line("SourceName", r.sourceName);
    line("BBox", bboxText(r.bbox));
    line("Zooms", formatZoomList(r.zooms));
    line("Created", r.created);
    for (std::size_t i = 0; i < r.files.size(); ++i) {
        line("File" + std::to_string(i), r.files[i]);
    }
    line("State", kStateNames[static_cast<int>(r.state)]);
    line("Tiles", std::to_string(r.tiles));
    line("Missing", std::to_string(r.missing));
    line("Bytes", std::to_string(r.bytes));
    line("Done", std::to_string(r.done));
    line("LastError", r.lastError);
    line("UpdateBefore", r.updateBefore);
    line("Weather", r.weather ? "1" : "0");
    line("Snapshot", r.snapshot);
    line("Credit", r.credit);
    return t;
}

std::optional<ChartRecord> parseChartIni(std::string_view text, std::string id) {
    ChartRecord r;
    r.id = std::move(id);
    bool haveName = false;
    std::map<int, std::string> files;
    const auto number = [](std::string_view v, std::int64_t& out) {
        std::from_chars(v.data(), v.data() + v.size(), out);
    };
    while (!text.empty()) {
        const auto nl = text.find('\n');
        std::string_view ln = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view() : text.substr(nl + 1);
        if (!ln.empty() && ln.back() == '\r') {
            ln.remove_suffix(1);
        }
        const auto eq = ln.find('=');
        if (eq == std::string_view::npos) {
            continue;
        }
        const std::string_view key = ln.substr(0, eq);
        const std::string value(ln.substr(eq + 1));
        if (key == "Name") {
            r.name = value;
            haveName = true;
        } else if (key == "SourceGuid") {
            r.sourceGuid = value;
        } else if (key == "OverlayGuid") {
            r.overlayGuid = value;
        } else if (key == "SourceName") {
            r.sourceName = value;
        } else if (key == "BBox") {
            r.bbox = parseBBox(value).value_or(BBox{});
        } else if (key == "Zooms") {
            r.zooms = parseZoomList(value).value_or(std::vector<int>{});
        } else if (key == "Created") {
            r.created = value;
        } else if (key == "State") {
            for (int i = 0; i < 5; ++i) {
                if (value == kStateNames[i]) {
                    r.state = static_cast<ChartState>(i);
                }
            }
        } else if (key == "Tiles") {
            number(value, r.tiles);
        } else if (key == "Missing") {
            number(value, r.missing);
        } else if (key == "Bytes") {
            number(value, r.bytes);
        } else if (key == "Done") {
            // a whole decimal 0..1000, anything else is unknown (0)
            int n = 0;
            const auto res = std::from_chars(value.data(), value.data() + value.size(), n);
            r.done = res.ec == std::errc() && res.ptr == value.data() + value.size() && n >= 0 && n <= 1000
                         ? n
                         : 0;
        } else if (key == "LastError") {
            r.lastError = value;
        } else if (key == "UpdateBefore") {
            r.updateBefore = value;
        } else if (key == "Weather") {
            r.weather = value == "1";
        } else if (key == "Snapshot") {
            r.snapshot = value;
        } else if (key == "Credit") {
            r.credit = value;
        } else if (key.starts_with("File")) {
            if (const auto n = parseInt(key.substr(4))) {
                files[*n] = value;
            }
        }
    }
    if (!haveName) {
        return std::nullopt;
    }
    for (auto& [n, path] : files) {
        r.files.push_back(std::move(path));
    }
    return r;
}

} // namespace cg
