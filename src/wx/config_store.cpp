// SPDX-License-Identifier: GPL-3.0-or-later
#include "config_store.h"

#include "geo_math.h"
#include "plugin_log.h"
#include "sources_folder.h"
#include "wx_util.h"

#include <wx/confbase.h>
#include <wx/dir.h>
#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/utils.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

#include <ocpn_plugin.h>

namespace {

constexpr const char* kGroup = "/PlugIns/ChartGrabber/";

// The key with its group. wxConfigPathChanger moves to the group for one access and back, so the shared
// OpenCPN config keeps its current path.
wxString keyOf(const char* name) {
    return wxString(kGroup) + name;
}

wxString readText(const wxConfigBase* cfg, const char* name, const wxString& def = wxString()) {
    const wxConfigPathChanger path(cfg, keyOf(name));
    wxString value;
    cfg->Read(path.Name(), &value, def);
    return value;
}

// an invalid value gives the default and ONE log line per key per session
// (`logged` is the store's set). Control characters in the logged value are shown as '?'.
using Logged = std::set<std::string>;

void logInvalid(Logged& logged, const char* name, const wxString& raw, const wxString& def) {
    if (!logged.insert(name).second)
        return;
    wxString shown;
    for (wxUniChar c : raw)
        shown += c.GetValue() < 0x20 || c.GetValue() == 0x7f ? wxUniChar('?') : c;
    cg::logLine(wxString::Format("setting %s=%s is invalid; using %s", name, shown, def));
}

// The stored text, or nullopt when the key is absent (the default applies, silently).
std::optional<wxString> readRaw(const wxConfigBase* cfg, const char* name) {
    const wxConfigPathChanger path(cfg, keyOf(name));
    if (!cfg->HasEntry(path.Name()))
        return std::nullopt;
    return readText(cfg, name);
}

// A whole-string integer in lo..hi.
long readLong(const wxConfigBase* cfg, Logged& logged, const char* name, long def, long lo, long hi) {
    const std::optional<wxString> raw = readRaw(cfg, name);
    if (!raw)
        return def;
    long value = 0;
    if (raw->ToCLong(&value) && value >= lo && value <= hi)
        return value;
    logInvalid(logged, name, *raw, wxString::Format("%ld", def));
    return def;
}

bool readFlag(const wxConfigBase* cfg, Logged& logged, const char* name, bool def) {
    return readLong(cfg, logged, name, def ? 1 : 0, 0, 1) != 0;
}

// Numbers are read from text: wxString::ToCDouble is locale independent (a German locale reads "2.5").
double readDouble(const wxConfigBase* cfg, Logged& logged, const char* name, double def, double lo,
                  double hi) {
    const std::optional<wxString> raw = readRaw(cfg, name);
    if (!raw)
        return def;
    double value = 0;
    if (raw->ToCDouble(&value) && std::isfinite(value) && value >= lo && value <= hi)
        return value;
    logInvalid(logged, name, *raw, wxString::FromCDouble(def));
    return def;
}

// Empty, or absolute (`/` first) without a control character.
std::string readPath(const wxConfigBase* cfg, Logged& logged, const char* name) {
    const wxString raw = readText(cfg, name);
    bool ok = raw.empty() || raw[0] == '/';
    for (wxUniChar c : raw)
        ok = ok && c.GetValue() >= 0x20 && c.GetValue() != 0x7f;
    if (ok)
        return cg::toUtf8(raw);
    logInvalid(logged, name, raw, "(empty)");
    return {};
}

void writeText(wxConfigBase* cfg, const char* name, std::string_view value) {
    const wxConfigPathChanger path(cfg, keyOf(name));
    cfg->Write(path.Name(), cg::fromUtf8(value));
}

void writeLong(wxConfigBase* cfg, const char* name, long value) {
    const wxConfigPathChanger path(cfg, keyOf(name));
    cfg->Write(path.Name(), value);
}

void writeFlag(wxConfigBase* cfg, const char* name, bool value) {
    writeLong(cfg, name, value ? 1 : 0);
}

char bandLetter(cg::Band b) {
    switch (b) {
    case cg::Band::Passage:
        return 'P';
    case cg::Band::Coastal:
        return 'C';
    case cg::Band::Approach:
        return 'A';
    case cg::Band::Harbour:
        return 'H';
    }
    return '?';
}

const char* areaKindName(cg::AreaKind k) {
    switch (k) {
    case cg::AreaKind::View:
        return "view";
    case cg::AreaKind::Rectangle:
        return "rectangle";
    case cg::AreaKind::Route:
        return "route";
    }
    return "view";
}

// LastRect : "W,S,E,N", exactly four non-empty parts (a trailing empty part counts), each a
// whole-string C-locale number (a leading blank is refused too: wx would skip it), finite, inside the box
// normalizeBox can give, west < east, south < north. Anything else: no rectangle.
std::optional<cg::BBox> parseRect(const std::string& text) {
    std::vector<double> v;
    std::size_t from = 0;
    while (true) {
        const std::size_t comma = text.find(',', from);
        const std::string part = text.substr(from, comma == std::string::npos ? comma : comma - from);
        double d = 0;
        if (part.empty() || std::isspace(static_cast<unsigned char>(part[0])) ||
            !wxString::FromUTF8(part.c_str()).ToCDouble(&d) || !std::isfinite(d))
            return std::nullopt;
        v.push_back(d);
        if (comma == std::string::npos)
            break;
        from = comma + 1;
    }
    if (v.size() != 4)
        return std::nullopt;
    const cg::BBox b{v[0], v[1], v[2], v[3]};
    if (!(b.west < b.east && b.south < b.north) || b.west < -180 || b.east > 180 || b.south < -cg::kMaxLat ||
        b.north > cg::kMaxLat)
        return std::nullopt;
    return b;
}

std::string homeDir() {
    return cg::toUtf8(wxGetHomeDir());
}

} // namespace

ConfigStore::ConfigStore(wxFileConfig* config) : config_(config) {}

cg::Settings ConfigStore::loadSettings() const {
    cg::Settings s;
    s.cliPath = readPath(config_, invalidLogged_, "CliPath");
    s.mapsDir = readPath(config_, invalidLogged_, "MapsDir");
    s.cacheDir = readPath(config_, invalidLogged_, "CacheDir");
    s.chartDir = readPath(config_, invalidLogged_, "ChartDir");
    s.proxy = cg::toUtf8(readText(config_, "Proxy"));
    s.threads = static_cast<int>(readLong(config_, invalidLogged_, "Threads", s.threads, 1, 64));
    s.corridorNm = readDouble(config_, invalidLogged_, "CorridorNm", s.corridorNm, 0.5, 20);
    s.slowConnection = readFlag(config_, invalidLogged_, "SlowConnection", s.slowConnection);
    s.dataBudgetMb =
        static_cast<int>(readLong(config_, invalidLogged_, "DataBudgetMb", s.dataBudgetMb, 1, 100000));
    s.registerCharts = readFlag(config_, invalidLogged_, "RegisterCharts", s.registerCharts);
    s.stripOnChart = readFlag(config_, invalidLogged_, "StripOnChart", s.stripOnChart);
    s.debugLog = readFlag(config_, invalidLogged_, "DebugLog", s.debugLog);
    s.warnDetailLevels = readFlag(config_, invalidLogged_, "WarnDetailLevels", s.warnDetailLevels);
    return s;
}

void ConfigStore::saveSettings(const cg::Settings& s) {
    writeText(config_, "CliPath", s.cliPath);
    writeText(config_, "MapsDir", s.mapsDir);
    writeText(config_, "CacheDir", s.cacheDir);
    writeText(config_, "ChartDir", s.chartDir);
    writeText(config_, "Proxy", s.proxy);
    writeLong(config_, "Threads", s.threads);
    // Format not fixed by the spec (section 11 says "numbers via formatNumber"): 2 decimals, open question.
    writeText(config_, "CorridorNm", cg::formatNumber(s.corridorNm, 2));
    writeFlag(config_, "SlowConnection", s.slowConnection);
    writeLong(config_, "DataBudgetMb", s.dataBudgetMb);
    writeFlag(config_, "RegisterCharts", s.registerCharts);
    writeFlag(config_, "StripOnChart", s.stripOnChart);
    writeFlag(config_, "DebugLog", s.debugLog);
    writeFlag(config_, "WarnDetailLevels", s.warnDetailLevels);
    config_->Flush();
}

ConfigStore::LastChoice ConfigStore::loadLastChoice() const {
    LastChoice c;
    c.sourceGuid = cg::toUtf8(readText(config_, "LastSource"));
    c.seamarks = readFlag(config_, invalidLogged_, "LastSeamarks", c.seamarks);
    c.weather = readFlag(config_, invalidLogged_, "LastWeather", c.weather);
    c.weatherGuid = cg::toUtf8(readText(config_, "LastWeatherSource"));
    const std::string letters = cg::toUtf8(readText(config_, "LastBands"));
    for (char ch : letters) {
        switch (ch) {
        case 'P':
            c.bands.insert(cg::Band::Passage);
            break;
        case 'C':
            c.bands.insert(cg::Band::Coastal);
            break;
        case 'A':
            c.bands.insert(cg::Band::Approach);
            break;
        case 'H':
            c.bands.insert(cg::Band::Harbour);
            break;
        default:
            break; // commas and unknown letters
        }
    }
    c.advancedZooms = cg::toUtf8(readText(config_, "LastZooms"));
    c.routeGuid = cg::toUtf8(readText(config_, "LastRoute"));
    const wxString kind = readText(config_, "LastAreaKind"); // case-sensitive; anything else is View
    if (kind == "rectangle")
        c.areaKind = cg::AreaKind::Rectangle;
    else if (kind == "route")
        c.areaKind = cg::AreaKind::Route;
    c.rectangle = parseRect(cg::toUtf8(readText(config_, "LastRect"))); // whatever the kind says
    return c;
}

void ConfigStore::saveLastChoice(const LastChoice& c) {
    writeText(config_, "LastSource", c.sourceGuid);
    writeFlag(config_, "LastSeamarks", c.seamarks);
    writeFlag(config_, "LastWeather", c.weather);
    writeText(config_, "LastWeatherSource", c.weatherGuid);
    std::string letters;
    for (cg::Band b : c.bands) { // std::set order: P, C, A, H
        if (!letters.empty())
            letters += ',';
        letters += bandLetter(b);
    }
    writeText(config_, "LastBands", letters);
    writeText(config_, "LastZooms", c.advancedZooms);
    writeText(config_, "LastRoute", c.routeGuid);
    writeText(config_, "LastAreaKind", areaKindName(c.areaKind));
    std::string rect; // written only with kind rectangle
    if (c.areaKind == cg::AreaKind::Rectangle && c.rectangle)
        rect = cg::formatNumber(c.rectangle->west, 8) + "," + cg::formatNumber(c.rectangle->south, 8) + "," +
               cg::formatNumber(c.rectangle->east, 8) + "," + cg::formatNumber(c.rectangle->north, 8);
    writeText(config_, "LastRect", rect);
    config_->Flush();
}

bool ConfigStore::disclaimerShown() const {
    return readFlag(config_, invalidLogged_, "DisclaimerShown", false);
}

void ConfigStore::setDisclaimerShown() {
    writeFlag(config_, "DisclaimerShown", true);
    config_->Flush();
}

void ConfigStore::setWarnDetailLevels(bool on) {
    writeFlag(config_, "WarnDetailLevels", on);
    config_->Flush();
}

bool ConfigStore::firstSuccessShown() const {
    return readFlag(config_, invalidLogged_, "FirstSuccessShown", false);
}

void ConfigStore::setFirstSuccessShown() {
    writeFlag(config_, "FirstSuccessShown", true);
    config_->Flush();
}

std::optional<cg::StripAnchor> ConfigStore::stripAnchor() const {
    const auto rawCorner = readRaw(config_, "StripCorner");
    const auto rawDx = readRaw(config_, "StripDx");
    const auto rawDy = readRaw(config_, "StripDy");
    if (!rawCorner || !rawDx || !rawDy)
        return std::nullopt;
    cg::StripAnchor a;
    bool ok = true;
    if (const auto c = cg::parseCornerKey(rawCorner->utf8_string()))
        a.corner = *c;
    else {
        ok = false;
        logInvalid(invalidLogged_, "StripCorner", *rawCorner,
                   cg::cornerKey(cg::StripCorner::BottomLeft).data());
    }
    const auto distance = [&](const char* name, const wxString& raw, int& out, int def) {
        long v = 0;
        if (raw.ToCLong(&v) && v >= 0 && v <= 100000) {
            out = static_cast<int>(v);
            return;
        }
        ok = false;
        logInvalid(invalidLogged_, name, raw, wxString::Format("%d", def));
    };
    distance("StripDx", *rawDx, a.dx, cg::kStripDefaultDx);
    distance("StripDy", *rawDy, a.dy, cg::kStripDefaultDy);
    return ok ? std::optional<cg::StripAnchor>(a) : std::nullopt;
}

void ConfigStore::setStripAnchor(std::optional<cg::StripAnchor> a) {
    if (a) {
        writeText(config_, "StripCorner", cg::cornerKey(a->corner));
        writeLong(config_, "StripDx", a->dx);
        writeLong(config_, "StripDy", a->dy);
    } else {
        for (const char* name : {"StripCorner", "StripDx", "StripDy"}) {
            const wxConfigPathChanger path(config_, keyOf(name));
            config_->DeleteEntry(path.Name());
        }
    }
    config_->Flush();
}

bool ConfigStore::stripHintShown() const {
    return readFlag(config_, invalidLogged_, "StripHintShown", false);
}

void ConfigStore::setStripHintShown() {
    writeFlag(config_, "StripHintShown", true);
    config_->Flush();
}

std::string ConfigStore::chartDir(const cg::Settings& s) const {
    if (!s.chartDir.empty())
        return s.chartDir;
    return homeDir() + "/Charts/ChartGrabber";
}

std::string ConfigStore::jobsDir() const {
    // Unreachable: OpenCPN returns its private data location at plugin Init.
    const wxString* location = GetpPrivateApplicationDataLocation();
    if (!location)
        return {};
    return cg::toUtf8(*location) + "/plugins/chartgrabber/jobs";
}

// Decision 0003: the plugin's own tile cache, beside the jobs folder; never derived from the CLI's name.
std::string ConfigStore::cliCacheRoot(const cg::Settings& s) const {
    if (!s.cacheDir.empty())
        return s.cacheDir;
    const wxString* location = GetpPrivateApplicationDataLocation();
    const std::string base = location ? cg::toUtf8(*location) : homeDir() + "/.opencpn";
    return base + "/plugins/chartgrabber/cache";
}

// Decision 0004: the user's own chart sources, outside the folder OpenCPN scans.
std::string ConfigStore::sourcesDir(const cg::Settings& s) const {
    if (!s.mapsDir.empty())
        return s.mapsDir;
    return homeDir() + "/Charts/" + std::string(cg::kSourcesFolderName);
}

void ConfigStore::setBundledSourcesDir(std::string dir) {
    bundledSourcesDir_ = std::move(dir);
}

std::string ConfigStore::bundledSourcesDir() const {
    return bundledSourcesDir_;
}

// cliMaps: the bundled folder (none set: no --maps, the CLI refuses), then the sailor's folder when it holds
// a source.
cg::Settings ConfigStore::cliSettings(const cg::Settings& s) const {
    cg::Settings out = s;
    out.cliMaps.clear();
    if (!bundledSourcesDir_.empty())
        out.cliMaps.push_back(bundledSourcesDir_);
    const std::string folder = sourcesDir(s);
    if (cg::countZmpEntries(folder, 1) > 0)
        out.cliMaps.push_back(folder);
    out.cacheDir = cliCacheRoot(s);
    return out;
}

namespace {

// The job directory names under root, descending: ids are yyyyMMdd-HHmmss[-n], so string order is time order.
std::vector<std::string> jobIdsNewestFirst(const std::string& root) {
    std::vector<std::string> ids;
    if (root.empty() || !wxDir::Exists(cg::fromUtf8(root)))
        return ids;
    wxDir dir(cg::fromUtf8(root));
    wxString name;
    for (bool more = dir.GetFirst(&name, wxEmptyString, wxDIR_DIRS); more; more = dir.GetNext(&name))
        ids.push_back(cg::toUtf8(name));
    std::ranges::sort(ids, std::greater<>());
    return ids;
}

std::optional<cg::ChartRecord> readChartIni(const std::string& root, const std::string& id) {
    std::ifstream in(root + "/" + id + "/chart.ini", std::ios::binary);
    if (!in)
        return std::nullopt;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return cg::parseChartIni(text, id);
}

} // namespace

std::vector<cg::ChartRecord> ConfigStore::loadCharts() const {
    std::vector<cg::ChartRecord> charts;
    const std::string root = jobsDir();
    for (const std::string& id : jobIdsNewestFirst(root))
        if (auto record = readChartIni(root, id))
            charts.push_back(*std::move(record));
    return charts;
}

std::vector<std::string> ConfigStore::unreadableChartIds() const {
    std::vector<std::string> unreadable;
    const std::string root = jobsDir();
    for (const std::string& id : jobIdsNewestFirst(root))
        if (!readChartIni(root, id))
            unreadable.push_back(id);
    return unreadable;
}

void ConfigStore::saveChart(const cg::ChartRecord& r) {
    const std::string dir = jobsDir() + "/" + r.id;
    if (!wxFileName::Mkdir(cg::fromUtf8(dir), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL))
        cg::logWarning(wxString::Format("cannot create %s", dir.c_str()));
    std::ofstream out(dir + "/chart.ini", std::ios::binary | std::ios::trunc);
    const std::string text = cg::chartIni(r);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out)
        cg::logWarning(wxString::Format("cannot write %s/chart.ini", dir.c_str()));
}

void ConfigStore::deleteChartDir(const std::string& id) {
    // The id is a job directory name; a separator would name a path outside the jobs folder.
    if (id.empty() || id.find_first_of("/\\") != std::string::npos)
        return;
    wxFileName::Rmdir(cg::fromUtf8(jobsDir() + "/" + id), wxPATH_RMDIR_RECURSIVE);
}

// Deletes only the sub-directories of the cache root whose name starts with "cache" .
// A symbolic link is never followed or removed.
void ConfigStore::clearCacheDirs(const std::string& root) {
    const wxString wxRoot = cg::fromUtf8(root);
    wxDir dir(wxRoot);
    if (!dir.IsOpened())
        return;
    wxString name;
    for (bool more = dir.GetFirst(&name, "cache*", wxDIR_DIRS); more; more = dir.GetNext(&name)) {
        const wxString path = wxRoot + "/" + name;
        if (wxFileName::Exists(path, wxFILE_EXISTS_SYMLINK))
            continue;
        wxFileName::Rmdir(path, wxPATH_RMDIR_RECURSIVE);
    }
}
