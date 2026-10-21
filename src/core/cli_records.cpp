// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli_records.h"

#include "geo_math.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <type_traits>

namespace cg {
namespace {

std::vector<std::string_view> splitTabs(std::string_view line) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    for (;;) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string_view::npos) {
            out.push_back(line.substr(start));
            return out;
        }
        out.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
}

// Whole field must be a number; locale-independent; no exceptions.
template <class T> bool num(std::string_view f, T& out) {
    if (f.empty()) {
        return false;
    }
    T v{};
    const auto r = std::from_chars(f.data(), f.data() + f.size(), v);
    if (r.ec != std::errc() || r.ptr != f.data() + f.size()) {
        return false;
    }
    if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(v)) { // nan / inf are not numbers of the contract
            return false;
        }
    }
    out = v;
    return true;
}

bool flag(std::string_view f, bool& out) {
    if (f == "0" || f == "1") {
        out = f == "1";
        return true;
    }
    return false;
}

std::optional<StdoutRecord> parseSource(const std::vector<std::string_view>& f) {
    if (f.size() < 10) {
        return std::nullopt;
    }
    SourceRecord r;
    r.guid = f[1];
    r.kind = f[2];
    r.name = f[3];
    r.contentType = f[4];
    if (!num(f[5], r.epsg) || !num(f[6], r.cacheType)) {
        return std::nullopt;
    }
    r.verdict = f[7];
    r.zmpFile = f[8];
    r.group = f[9];
    if (f.size() > 11) {
        r.refresh = f[11];
    }
    if (f.size() > 12) {
        r.license = f[12];
    }
    if (f.size() > 13) {
        int z = 0;
        if (num(f[13], z) && z >= 0 && z <= 23) {
            r.maxZoom = z;
        }
    }
    r.navigationWarning = f.size() > 14 && f[14] == "1";
    return r;
}

std::optional<StdoutRecord> parseEstimate(const std::vector<std::string_view>& f) {
    if (f.size() < 10) {
        return std::nullopt;
    }
    EstimateRecord r;
    r.guid = f[1];
    if (!num(f[2], r.zoom) || !num(f[3], r.tiles) || !num(f[4], r.bboxTiles) || !num(f[5], r.sharePresent) ||
        !num(f[6], r.shareMarkedMissing) || !num(f[7], r.bytesToDownload) || !flag(f[8], r.assumed) ||
        !num(f[9], r.bytesOnDisk)) {
        return std::nullopt;
    }
    return r;
}

std::optional<StdoutRecord> parseFill(const std::vector<std::string_view>& f) {
    if (f.size() < 8) {
        return std::nullopt;
    }
    FillRecord r;
    r.guid = f[1];
    if (!num(f[2], r.zoom) || !num(f[3], r.tiles) || !num(f[4], r.present) || !num(f[5], r.markedMissing) ||
        !num(f[6], r.missing) || !num(f[7], r.bytes)) {
        return std::nullopt;
    }
    return r;
}

MessageLine other(std::string_view line) {
    return MessageLine{MessageLine::Kind::Other, std::string(line)};
}

bool isLetter(char c) {
    const auto u = static_cast<unsigned char>(c);
    return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u >= 0x80;
}

std::string lowerAscii(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

constexpr std::int64_t kBytesPerMb = 1'000'000;

} // namespace

SourceRefusal sourceRefusal(const SourceRecord& r) {
    SourceRefusal out;
    if (r.epsg != kSupportedEpsg) {
        out.kind = SourceRefusal::Kind::Projection;
        out.epsg = r.epsg;
    } else if (r.verdict != "ok") {
        out.kind = SourceRefusal::Kind::Verdict;
        out.verdict = r.verdict;
    }
    return out;
}

std::vector<std::size_t> sourcesDialogOrder(const std::vector<SourceRecord>& s) {
    std::vector<std::string> groups; // non-empty groups in order of first appearance
    for (const SourceRecord& r : s) {
        if (!r.group.empty() && std::find(groups.begin(), groups.end(), r.group) == groups.end()) {
            groups.push_back(r.group);
        }
    }
    groups.emplace_back(); // the empty group last
    std::vector<std::size_t> order;
    order.reserve(s.size());
    for (const std::string& g : groups) {
        for (const bool wantUsable : {true, false}) {
            for (std::size_t i = 0; i < s.size(); ++i) {
                if (s[i].group == g &&
                    (sourceRefusal(s[i]).kind == SourceRefusal::Kind::None) == wantUsable) {
                    order.push_back(i);
                }
            }
        }
    }
    return order;
}

bool nameLooksLikeImagery(std::string_view name) {
    const std::string n = lowerAscii(name);
    for (const char* w : {"satellite", "imagery", "aerial", "ortho", "photo"}) {
        if (n.find(w) != std::string::npos) {
            return true;
        }
    }
    for (std::size_t pos = n.find("sat"); pos != std::string::npos; pos = n.find("sat", pos + 1)) {
        const bool leftOk = pos == 0 || !isLetter(n[pos - 1]);
        const bool rightOk = pos + 3 == n.size() || !isLetter(n[pos + 3]);
        if (leftOk && rightOk) {
            return true;
        }
    }
    return false;
}

std::optional<StdoutRecord> parseStdoutLine(std::string_view line) {
    const auto f = splitTabs(line);
    const std::string_view type = f[0];
    if (type == "source") {
        return parseSource(f);
    }
    if (type == "estimate") {
        return parseEstimate(f);
    }
    if (type == "fill") {
        return parseFill(f);
    }
    if (type == "session") {
        if (f.size() < 4) {
            return std::nullopt;
        }
        return SessionRecord{std::string(f[1]), std::string(f[2]), std::string(f[3])};
    }
    if (type == "written") {
        if (f.size() < 5) {
            return std::nullopt;
        }
        WrittenRecord r;
        r.guid = f[1];
        r.path = f[2];
        if (!num(f[3], r.tiles) || !num(f[4], r.bytes)) {
            return std::nullopt;
        }
        if (f.size() > 5) {
            r.snapshot = f[5];
        }
        return r;
    }
    if (type == "snapshot") {
        if (f.size() < 4) {
            return std::nullopt;
        }
        return SnapshotRecord{std::string(f[1]), std::string(f[2]), std::string(f[3])};
    }
    return std::nullopt;
}

std::optional<int> zoomLimit(const SourceRecord& r) {
    if (r.isWeather()) {
        return std::min(r.maxZoom.value_or(kWeatherZoomCap), kWeatherZoomCap);
    }
    return r.maxZoom;
}

bool isSnapshotFailure(std::string_view lastError) {
    return lastError.starts_with("cannot take the snapshot of ");
}

std::optional<std::int64_t> snapshotEpoch(std::string_view iso) {
    if (iso.size() != 10 && iso.size() != 20) {
        return std::nullopt;
    }
    // digits at [pos, pos+n) as a number, or -1
    const auto digits = [&](std::size_t pos, std::size_t n) {
        int v = 0;
        for (std::size_t i = pos; i < pos + n; ++i) {
            if (iso[i] < '0' || iso[i] > '9') {
                return -1;
            }
            v = v * 10 + (iso[i] - '0');
        }
        return v;
    };
    const int y = digits(0, 4), mo = digits(5, 2), d = digits(8, 2);
    if (y < 0 || mo < 1 || mo > 12 || d < 1 || iso[4] != '-' || iso[7] != '-') {
        return std::nullopt;
    }
    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (d > kDays[mo - 1] + (mo == 2 && leap ? 1 : 0)) {
        return std::nullopt;
    }
    std::int64_t secs = 0;
    if (iso.size() == 20) {
        const int h = digits(11, 2), mi = digits(14, 2), s = digits(17, 2);
        if (iso[10] != 'T' || iso[13] != ':' || iso[16] != ':' || iso[19] != 'Z' || h < 0 || h > 23 ||
            mi < 0 || mi > 59 || s < 0 || s > 59) {
            return std::nullopt;
        }
        secs = h * 3600 + mi * 60 + s;
    }
    // days from civil (Howard Hinnant)
    const int yy = y - (mo <= 2 ? 1 : 0);
    const int era = yy / 400; // yy >= 0
    const int yoe = yy - era * 400;
    const int doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const std::int64_t days = static_cast<std::int64_t>(era) * 146097 + doe - 719468;
    return days * 86400 + secs;
}

std::string snapshotTimeText(std::string_view iso) {
    if (iso.size() == 20 && snapshotEpoch(iso)) {
        return std::string(iso.substr(0, 10)) + ' ' + std::string(iso.substr(11, 5)) + " UTC";
    }
    return std::string(iso);
}

StderrLine parseStderrLine(std::string_view line) {
    const auto f = splitTabs(line);
    if (f[0] == "phase") {
        if (f.size() >= 3) {
            return PhaseRecord{std::string(f[1]), std::string(f[2])};
        }
        return other(line);
    }
    if (f[0] == "progress" && f.size() >= 2) {
        if (f[1] == "download" && f.size() >= 9) {
            DownloadProgress p;
            p.guid = f[2];
            if (num(f[3], p.zoom) && num(f[4], p.processed) && num(f[5], p.total) &&
                num(f[6], p.downloaded) && num(f[7], p.downloadedBytes) && num(f[8], p.elapsedSec)) {
                return p;
            }
        } else if (f[1] == "export" && f.size() >= 4) {
            ExportProgress p;
            p.guid = f[2];
            if (num(f[3], p.permille)) {
                return p;
            }
        }
        return other(line);
    }
    constexpr std::string_view kError = "error: ", kWarning = "warning: ";
    if (line.substr(0, kError.size()) == kError) {
        std::string text(line.substr(kError.size()));
        if (!text.empty() && text.back() == '\r') { // the curl text of a connection error ends in "\r"
            text.pop_back();
        }
        return MessageLine{MessageLine::Kind::Error, std::move(text)};
    }
    if (line.substr(0, kWarning.size()) == kWarning) {
        return MessageLine{MessageLine::Kind::Warning, std::string(line.substr(kWarning.size()))};
    }
    return other(line);
}

void LineSplitter::feed(std::string_view bytes, const std::function<void(std::string_view)>& onLine) {
    while (!bytes.empty()) {
        const std::size_t nl = bytes.find('\n');
        if (nl == std::string_view::npos) {
            pending_.append(bytes);
            return;
        }
        pending_.append(bytes.substr(0, nl));
        bytes.remove_prefix(nl + 1);
        if (!pending_.empty() && pending_.back() == '\r') {
            pending_.pop_back();
        }
        const std::string line = std::move(pending_);
        pending_.clear();
        onLine(line);
    }
}

void LineSplitter::finish(const std::function<void(std::string_view)>& onLine) {
    if (!pending_.empty() && pending_.back() == '\r') {
        pending_.pop_back();
    }
    const std::string line = std::move(pending_);
    pending_.clear();
    if (!line.empty()) {
        onLine(line);
    }
}

EstimateTotals sumEstimates(const std::vector<EstimateRecord>& records) {
    EstimateTotals t;
    for (const EstimateRecord& r : records) {
        t.tiles += r.tiles;
        t.bytesToDownload += r.bytesToDownload;
        t.bytesOnDisk += r.bytesOnDisk;
        t.assumed = t.assumed || r.assumed;
    }
    return t;
}

std::optional<LevelCost> levelCost(const std::vector<EstimateRecord>& records, int hi) {
    if (hi >= kMaxZoom)
        return std::nullopt;
    LevelCost c;
    bool found = false;
    for (const EstimateRecord& r : records) {
        c.totalBytes += r.bytesToDownload;
        if (r.zoom == hi) {
            found = true;
            c.finestBytes += r.bytesToDownload;
            c.nextBytes += 4 * r.bytesOnDisk;
        }
    }
    if (!found)
        return std::nullopt;
    return c;
}

std::int64_t meanTileBytes(const std::vector<EstimateRecord>& records) {
    std::int64_t bytes = 0, tiles = 0;
    for (const EstimateRecord& r : records)
        if (r.tiles > 0) {
            bytes += r.bytesOnDisk;
            tiles += r.tiles;
        }
    return tiles > 0 ? bytes / tiles : kAssumedTileBytes;
}

FillTotals sumFill(const std::vector<FillRecord>& records, std::string_view guid) {
    FillTotals t;
    for (const FillRecord& r : records) {
        if (r.guid == guid) {
            t.tiles += r.tiles;
            t.missing += r.missing;
            t.bytes += r.bytes;
        }
    }
    return t;
}

std::optional<std::int64_t> secondsLeft(const DownloadProgress& p) {
    if (p.processed <= 0) {
        return std::nullopt;
    }
    return std::max<std::int64_t>(0, p.elapsedSec * (p.total - p.processed) / p.processed);
}

Outcome classifyExit(int exitCode, std::string_view lastError, bool startFailed) {
    if (startFailed) {
        return Outcome::StartFailed;
    }
    switch (exitCode) {
    case 0:
        return Outcome::Done;
    case 2:
        return Outcome::UsageError;
    case 3:
        return Outcome::Refused;
    case 4:
        if (lastError.starts_with("no progress for") ||
            lastError.starts_with("no connection to the server for")) {
            return Outcome::Stalled;
        }
        if (lastError.starts_with("cannot store tiles")) {
            return Outcome::StorageError;
        }
        return Outcome::DownloadIncomplete;
    case 5:
        return Outcome::ExportFailed;
    case 130:
    case 143:
        return Outcome::Interrupted;
    default:
        return exitCode < 0 ? Outcome::Killed : Outcome::InternalError; // 1 and unknown codes: InternalError
    }
}

int normalizeExit(int wxCode, Os os, bool killedByUs) {
    if (os == Os::Posix) {
        return wxCode < -64 ? wxCode + 256 : wxCode;
    }
    if (killedByUs && wxCode == -1) {
        return -9;
    }
    return wxCode < 0 ? 1 : wxCode;
}

bool resumable(Outcome o) {
    return o == Outcome::Stalled || o == Outcome::StorageError || o == Outcome::DownloadIncomplete ||
           o == Outcome::Interrupted || o == Outcome::Killed;
}

Preflight checkPreflight(const PreflightInput& in) {
    const EstimateTotals& t = in.totals;
    if (t.tiles > kTooLargeTiles || t.bytesOnDisk > kTooLargeBytes) {
        return Preflight::TooLarge;
    }
    const auto avail = [](std::uint64_t freeBytes) {
        return freeBytes > kDiskReserve ? freeBytes - kDiskReserve : 0;
    };
    const std::uint64_t chartNeed = 2 * static_cast<std::uint64_t>(std::max<std::int64_t>(0, t.bytesOnDisk));
    const std::uint64_t cacheNeed = static_cast<std::uint64_t>(std::max<std::int64_t>(0, t.bytesToDownload));
    if (in.sameFileSystem) {
        if (chartNeed + cacheNeed > avail(in.freeChartBytes)) {
            return Preflight::ChartDiskFull;
        }
    } else {
        if (chartNeed > avail(in.freeChartBytes)) {
            return Preflight::ChartDiskFull;
        }
        if (cacheNeed > avail(in.freeCacheBytes)) {
            return Preflight::CacheDiskFull;
        }
    }
    // settings is set by every caller (JobController, NewChartPanel); null means no budget.
    if (in.settings && in.settings->slowConnection &&
        t.bytesToDownload > static_cast<std::int64_t>(in.settings->dataBudgetMb) * kBytesPerMb) {
        return Preflight::OverBudget;
    }
    return Preflight::Ok;
}

bool shouldRetry(Outcome o, const Settings& s, const RetryState& st) {
    return o == Outcome::Stalled && !s.slowConnection && st.attempt < kMaxRetries;
}

} // namespace cg
