// SPDX-License-Identifier: GPL-3.0-or-later
#include "messages.h"

#include "job_files.h"
#include "settings.h"
#include "wx_util.h"

#include <wx/intl.h>

#include <algorithm>
#include <cmath>

namespace {

// The package name of the CLI, spelled once (cg::kDefaultCliName) for the texts that name it.
wxString packageName() {
    return wxString::FromAscii(std::string(cg::kDefaultCliName).c_str());
}

// The Windows file name of the CLI, built from the one place that names it.
wxString exeName() {
    return packageName() + ".exe";
}

wxString num(std::int64_t n) {
    return wxString::FromAscii(std::to_string(n).c_str());
}

wxString decimals(double v, int places) {
    return wxString::FromAscii(cg::formatNumber(v, places).c_str());
}

bool endsWith(std::string_view s, std::string_view tail) {
    return s.size() >= tail.size() && s.substr(s.size() - tail.size()) == tail;
}

bool contains(std::string_view s, std::string_view part) {
    return s.find(part) != std::string_view::npos;
}

wxString joinNames(const std::vector<wxString>& names) {
    wxString out;
    for (const wxString& n : names) {
        if (!out.empty())
            out += ", ";
        out += n;
    }
    return out;
}

} // namespace

wxString formatCount(std::int64_t n) {
    const bool negative = n < 0;
    std::string digits = std::to_string(n);
    if (negative)
        digits.erase(0, 1);
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0)
            out += ' ';
        out += digits[i];
    }
    return wxString::FromAscii((negative ? "-" + out : out).c_str());
}

wxString formatBytes(std::int64_t bytes) {
    const double b = static_cast<double>(std::max<std::int64_t>(bytes, 0));
    if (b < 10e6)
        return decimals(b / 1e6, 1) + " MB";
    if (b < 1e9)
        return num(std::llround(b / 1e6)) + " MB";
    return decimals(b / 1e9, 1) + " GB";
}

wxString formatDuration(std::int64_t sec) {
    sec = std::max<std::int64_t>(sec, 0);
    if (sec < 60)
        return num(sec) + " s";
    const std::int64_t minutes = (sec + 59) / 60; // rounded up
    if (sec < 3600)
        return num(minutes) + " min";
    const std::int64_t rest = minutes % 60;
    return num(minutes / 60) + " h " + (rest < 10 ? "0" : "") + num(rest) + " min";
}

wxString savedTilesText(std::int64_t bytes, bool busy) {
    wxString text = bytes > 0 ? formatBytes(bytes) + _(" of saved tiles") : _("No saved tiles yet.");
    if (busy)
        text += _(" (a download is running)");
    return text;
}

wxString clearSavedTilesQuestion(std::int64_t bytes, const wxString& path) {
    return _("Delete ") + formatBytes(bytes) + _(" of saved tiles in\n") + path +
           _("?\n\nYour charts stay. Top up and Build from saved tiles will then download everything again.");
}

wxString bandName(cg::Band b) {
    switch (b) {
    case cg::Band::Passage:
        return _("Passage");
    case cg::Band::Coastal:
        return _("Coastal");
    case cg::Band::Approach:
        return _("Approach");
    case cg::Band::Harbour:
        break;
    }
    return _("Harbour");
}

wxString progressLine(const wxString& source, const cg::DownloadProgress& p) {
    const std::int64_t pct = p.total > 0 ? p.processed * 100 / p.total : 0;
    const auto left = cg::secondsLeft(p);
    wxString band = _("zoom ") + num(p.zoom);
    for (cg::Band b : {cg::Band::Passage, cg::Band::Coastal, cg::Band::Approach, cg::Band::Harbour}) {
        const auto [lo, hi] = cg::bandZooms(b);
        if (p.zoom >= lo && p.zoom <= hi) {
            band = bandName(b);
            break;
        }
    }
    return source + ", " + band + ": " + num(pct) + " %  " + formatBytes(p.downloadedBytes) + "  " +
           (left ? _("about ") + formatDuration(*left) + _(" left") : wxString(_("time left unknown")));
}

wxString exportLine(const wxString& chartName, int permille) {
    return _("Building the chart ") + chartName + ": " + num(permille / 10) + " %";
}

wxString phaseLine(const std::string& phase, const wxString& source) {
    if (phase == "estimate")
        return source + _(": working out the size...");
    if (phase == "fill")
        return source + _(": checking the saved tiles...");
    if (phase == "download")
        return source + _(": downloading...");
    if (phase == "export")
        return source + _(": building the chart...");
    return source + ": " + cg::fromUtf8(phase);
}

wxString cacheRefusedText() {
    return _("The folder for saved tiles cannot be used. Choose another one under Advanced in Preferences "
             "(Saved tiles in).");
}

wxString tooLargeText() {
    return _("Area too large for this detail - choose a smaller area or less detail");
}

wxString refusalText(std::string_view lastError) {
    if (contains(lastError, "BerkeleyDB"))
        return _("This chart source asks for a BerkeleyDB tile cache, which Chart Grabber CLI no longer "
                 "supports.");
    if (contains(lastError, ": cannot open the cache: ") ||
        contains(lastError, ": cannot write the cache folder "))
        return cacheRefusedText();
    if (lastError.starts_with("the area is too large at zoom "))
        return tooLargeText();
    if (lastError == "the area holds no tile at the chosen zooms")
        return _("The area has no tiles at the chosen detail.");
    if (endsWith(lastError, "; pass --overwrite to replace it"))
        return _("A chart with this name exists.");
    for (std::string_view key : {"does not hold bitmap tiles", "only PNG, JPEG and WebP", "only EPSG:3857",
                                 "pass --allow-webp", "crop or cut after download"}) {
        if (contains(lastError, key))
            return _("This chart source cannot be made into an OpenCPN chart: ") + cg::fromUtf8(lastError) +
                   ".";
    }
    if (lastError.empty())
        return _("Chart Grabber CLI could not start this download.");
    return _("Chart Grabber CLI could not start this download: ") + cg::fromUtf8(lastError) + ".";
}

wxString outcomeHeadline(cg::Outcome o, const cg::FillTotals& fill, const std::vector<wxString>& chartNames,
                         std::string_view lastError) {
    switch (o) {
    case cg::Outcome::Done: {
        const std::size_t n = chartNames.size();
        if (fill.missing > 0) {
            const wxString gap = ", " + formatCount(fill.missing) + " of " + formatCount(fill.tiles) +
                                 _(" tiles are missing. Top up later to fill them.");
            return n >= 2 ? num(static_cast<std::int64_t>(n)) + _(" charts added") + gap
                          : _("Chart added") + gap;
        }
        if (n >= 2)
            return num(static_cast<std::int64_t>(n)) + _(" charts added: ") + joinNames(chartNames) + ".";
        if (n == 1) // n == 0 cannot happen after exit 0 (the CLI writes at least one file); plain text then
            return _("Chart added: ") + chartNames[0] + ".";
        return _("Chart added.");
    }
    case cg::Outcome::Stalled:
        return _("No connection. Download paused; your progress is saved.");
    case cg::Outcome::StorageError:
        return _("Cannot save tiles: the disk is full or cannot be written.");
    case cg::Outcome::DownloadIncomplete:
        return cg::isSnapshotFailure(lastError) ? snapshotFailedText()
                                                : _("The download did not complete; your progress is saved.");
    case cg::Outcome::Refused:
        return refusalText(lastError);
    case cg::Outcome::ExportFailed:
        return _("The chart could not be written. Nothing was added.");
    case cg::Outcome::Interrupted:
        return _("Stopped. Resume any time from My charts.");
    case cg::Outcome::Killed:
        return _("Stopped by force. Resume continues from the last save (up to 5 minutes lost).");
    case cg::Outcome::UsageError:
    case cg::Outcome::InternalError:
        return _("The download failed. See Details.");
    case cg::Outcome::StartFailed:
        return cliNotFoundText();
    }
    return _("The download failed. See Details."); // not an Outcome value
}

wxString retryText(int attempt, int secondsLeft) {
    return _("No connection. Retry in ") + num(secondsLeft) + " s (" + num(attempt) + _(" of ") +
           num(cg::kMaxRetries) + ")";
}

wxString budgetStopText(int budgetMb) {
    return _("Stopped at your data limit (") + num(budgetMb) + _(" MB). Resume to continue.");
}

wxString firstSuccessHint() {
    return _("If the chart is not visible, click it in the chart bar at the bottom. MBTiles charts need "
             "OpenGL (Options > Display > Advanced).");
}

wxString stoppingText() {
    return _("Stopping - saving progress...");
}
wxString stopButtonText() {
    return _("Stop");
}
wxString showButtonText() {
    return _("Show");
}
wxString resumeButtonText() {
    return _("Resume");
}
wxString closeButtonText() {
    return _("Close");
}
wxString buildFromSavedButtonText() {
    return _("Build from saved tiles");
}
wxString updateMenuText() {
    return _("Update...");
}
wxString updateTitleText() {
    return _("Update chart");
}
wxString updateQuestion(const wxString& name) {
    return wxString::Format(_("Download again the tiles of \"%s\" that are older than:"), name);
}
wxString updateChoiceText(int i) {
    switch (i) {
    case 0:
        return _("Today (all tiles)");
    case 1:
        return _("1 month");
    case 2:
        return _("6 months");
    default:
        return _("1 year"); // i = 3 (caller: the four choices of the Update dialog)
    }
}
wxString preferencesButtonText() {
    return _("Preferences...");
}
wxString stripHintText() {
    return _("You can drag this strip anywhere on the chart. Double-click it to put it back.");
}

wxString jobFileMissingText() {
    return _("The download settings of this chart are missing. Delete the chart and make it again.");
}

wxString snapshotFailedText() {
    return _("Could not get the latest weather picture. Try again later.");
}

wxString weatherPictureLine(const wxString& time) {
    return _("Weather picture: ") + time;
}

wxString preflightText(cg::Preflight p, const cg::PreflightInput& in, const wxString& chartFolder,
                       const wxString& cacheFolder) {
    const std::int64_t reserve = static_cast<std::int64_t>(cg::kDiskReserve);
    switch (p) {
    case cg::Preflight::TooLarge:
        return tooLargeText();
    case cg::Preflight::ChartDiskFull: {
        std::int64_t need = 2 * in.totals.bytesOnDisk;
        if (in.sameFileSystem)
            need += in.totals.bytesToDownload;
        return _("Needs about ") + formatBytes(need + reserve) + _(", only ") +
               formatBytes(static_cast<std::int64_t>(in.freeChartBytes)) + _(" free on ") + chartFolder;
    }
    case cg::Preflight::CacheDiskFull:
        return _("Needs about ") + formatBytes(in.totals.bytesToDownload + reserve) + _(", only ") +
               formatBytes(static_cast<std::int64_t>(in.freeCacheBytes)) + _(" free on ") + cacheFolder;
    case cg::Preflight::Ok:
    case cg::Preflight::OverBudget:
        break;
    }
    return wxString();
}

wxString areaErrorText(cg::AreaError e) {
    switch (e) {
    case cg::AreaError::OutsideMercator:
        return _("The area is too far north or south (beyond 85 degrees).");
    case cg::AreaError::CrossesAntimeridian:
        return _("The area crosses the 180 degree meridian. Make two charts, one on each side.");
    case cg::AreaError::Empty:
        return _("The area is empty. Draw a larger area.");
    case cg::AreaError::TooFewRoutePoints:
        return _("The route needs at least two points.");
    }
    return wxString(); // not an AreaError value
}

wxString sourceRefusalText(const cg::SourceRefusal& r) {
    switch (r.kind) {
    case cg::SourceRefusal::Kind::Projection:
        return _("Cannot be made into an OpenCPN chart (projection EPSG:") + num(r.epsg) + ")";
    case cg::SourceRefusal::Kind::Verdict:
        return cg::fromUtf8(r.verdict);
    case cg::SourceRefusal::Kind::None:
        break;
    }
    return wxString();
}

wxString imageryWarningText() {
    return _("Satellite images can be 40-170 m off the chart: background only, not for navigation near "
             "dangers.");
}

wxString termsNoteText() {
    return _("The owner of this chart source may not allow saving its tiles for offline use. Check its terms "
             "of use "
             "before you download.");
}

wxString navigationNoteText() {
    return _("Not for navigation. This source is not an official nautical chart; do not use it to navigate.");
}

wxString weatherNoteText(const wxString& scale, const wxString& credit) {
    wxString text = wxString::Format(_("Made as its own chart, detail up to %s."), scale);
    if (!credit.empty())
        text += wxString(_(" Credit: ")) + credit;
    return text;
}

wxString weatherCoverageText() {
    return _("Radar reaches about 80-130 NM off the coast; far offshore only satellite pictures show the "
             "weather.");
}

wxString sourceLimitText(const wxString& name, const wxString& scale) {
    return wxString::Format(_("%s has detail only up to %s; the chart stops there."), name, scale);
}

wxString cliNotFoundText(cg::Os os) {
    if (os == cg::Os::Windows)
        return wxString::Format(_("Chart Grabber CLI was not found. Download the Chart Grabber CLI zip from "
                                  "the project's Releases page, unpack it and choose %s under Advanced in "
                                  "Preferences."),
                                exeName());
    return wxString::Format(
        _("Chart Grabber CLI was not found. Install the %s package, or choose the program "
          "under Advanced in Preferences."),
        packageName());
}

wxString cliTooOldText(cg::Os os) {
    if (os == cg::Os::Windows)
        return _("Chart Grabber CLI is too old. Download the current Chart Grabber CLI zip from the "
                 "project's Releases page.");
    return wxString::Format(_("Chart Grabber CLI is too old. Install the current %s package."),
                            packageName());
}

wxString noSourcesText() {
    return _("No usable chart source was found. Reinstall Chart Grabber, or check the files in your chart "
             "sources folder.");
}

wxString cliStatusText(CliStatus status, const std::string& version, const wxString& line, cg::Os os) {
    const wxString v = wxString::FromAscii(version.c_str());
    switch (status) {
    case CliStatus::Checking:
        return _("Checking Chart Grabber CLI...");
    case CliStatus::Ready:
        return version.empty() ? _("Chart Grabber CLI is ready.")
                               : wxString::Format(_("Chart Grabber CLI %s is ready."), v);
    case CliStatus::NotInstalled:
        if (os == cg::Os::Windows)
            return wxString::Format(_("Chart Grabber CLI was not found. Download the Chart Grabber CLI zip "
                                      "from the project's Releases page, unpack it and choose %s under "
                                      "Advanced."),
                                    exeName());
        return wxString::Format(_("Chart Grabber CLI was not found. Install the %s package, or choose the "
                                  "program under Advanced."),
                                packageName());
    case CliStatus::NotRunnable:
        return _("This file cannot be run as a program. Use default to go back to the installed one.");
    case CliStatus::TooOld:
        if (os == cg::Os::Windows)
            return version.empty() ? cliTooOldText(os)
                                   : wxString::Format(_("Chart Grabber CLI %s is too old. Download the "
                                                        "current Chart Grabber CLI zip from the project's "
                                                        "Releases page."),
                                                      v);
        return version.empty()
                   ? cliTooOldText(os)
                   : wxString::Format(_("Chart Grabber CLI %s is too old. Install the current %s package."),
                                      v, packageName());
    case CliStatus::Fails:
        break;
    }
    return wxString::Format(_("Chart Grabber CLI does not work: %s"), line);
}

wxString sourcesStatusText(SourcesStatus status, int records, FolderState folder) {
    switch (status) {
    case SourcesStatus::Checking:
        return _("Looking for chart sources...");
    case SourcesStatus::CliNotReady:
        return _("Chart sources are checked when Chart Grabber CLI is ready.");
    case SourcesStatus::BundledMissing:
        return _("The chart sources that come with Chart Grabber are missing. Reinstall the Chart Grabber "
                 "plugin.");
    case SourcesStatus::RunFailed:
        return _("Chart sources could not be checked.");
    case SourcesStatus::Found:
        break;
    }
    if (records == 0)
        return noSourcesText();
    const wxString n = formatCount(records);
    switch (folder) {
    case FolderState::Missing:
        return records == 1
                   ? _("1 chart source available. Your own chart sources folder was not found: press Open "
                       "folder "
                       "to create it.")
                   : wxString::Format(
                         _("%s chart sources available. Your own chart sources folder was not found: "
                           "press Open folder to create it."),
                         n);
    case FolderState::Empty:
        return records == 1
                   ? _("1 chart source available. Put your own chart source (.zmp or .source.json) files in "
                       "your folder "
                       "to add more.")
                   : wxString::Format(
                         _("%s chart sources available. Put your own chart source (.zmp or .source.json) "
                           "files in your folder to add more."),
                         n);
    case FolderState::Holds:
        break;
    }
    return records == 1 ? _("1 chart source available, with those in your folder.")
                        : wxString::Format(_("%s chart sources available, with those in your folder."), n);
}

wxString estimatingText() {
    return _("Working out the size...");
}

wxString enterNameText() {
    return _("Enter a chart name");
}

wxString cannotWriteText(const wxString& path) {
    return _("Cannot write ") + path + ".";
}

wxString overBudgetQuestion(std::int64_t bytesToDownload, int budgetMb) {
    return _("This download is about ") + formatBytes(bytesToDownload) + _(". Your limit is ") +
           num(budgetMb) + _(" MB. Download anyway?");
}

wxString disclaimerText() {
    return _("Not official charts. Do not use for navigation alone. Respect the terms of use of each chart "
             "source.");
}

wxString dontShowAgainText() {
    return _("Don't show again");
}

wxString replaceQuestion() {
    return _("A chart with this name exists. Replace it?");
}

wxString uiText(UiText t) {
    switch (t) {
    case UiText::Seamarks:
        return _("Seamarks");
    case UiText::Cancel:
        return _("Cancel");
    case UiText::DrawHint:
        return _("Drag a rectangle on the chart, or tap two corners. Right-click or Cancel to stop.");
    case UiText::NoViewport:
        return _("Move the chart once, then try again.");
    case UiText::NoRoutes:
        return _("No routes. Create a route first.");
    case UiText::DownloadRunning:
        return _("A download is running");
    case UiText::LoadingSources:
        return _("Loading chart sources...");
    case UiText::NoUsableSource:
        return _("No usable chart source. Use More sources... or Preferences.");
    case UiText::SeamarksMissing:
        return _("The seamarks source (OpenSeaMap) is not among your chart sources");
    case UiText::ChooseDetail:
        return _("Choose the detail");
    case UiText::HarbourTooLarge:
        return _("Too many tiles for this area - draw a smaller area for harbour detail");
    }
    return wxString();
}

wxString unnamedRouteText(const wxString& guid) {
    return _("(unnamed ") + guid.Left(8) + ")";
}

wxString corridorText(double nm) {
    return decimals(nm, 1) + " NM";
}

wxString bandScaleText(cg::Band b, double lat) {
    const auto [lo, hi] = cg::bandZooms(b);
    return wxString::FromAscii(cg::formatScale(cg::scaleDenominator(lo, lat)).c_str()) + "-" +
           wxString::FromAscii(cg::formatScale(cg::scaleDenominator(hi, lat)).c_str());
}

namespace {
wxString scaleOf(int zoom, double lat) {
    return wxString::FromAscii(cg::formatScale(cg::scaleDenominator(zoom, lat)).c_str());
}
} // namespace

wxString zoomEntryText(int zoom, double lat) {
    return wxString::Format(_("%s  (zoom %d)"), scaleOf(zoom, lat), zoom);
}

wxString detailRangeNote(int lo, int hi, double lat, const std::vector<cg::Band>& partly) {
    wxString text = lo == hi ? wxString::Format(_("Detail set under Advanced: %s"), scaleOf(lo, lat))
                             : wxString::Format(_("Detail set under Advanced: %s to %s"), scaleOf(lo, lat),
                                                scaleOf(hi, lat));
    if (partly.size() == 1)
        text += wxString::Format(_(" (part of %s)"), bandName(partly[0]));
    else if (partly.size() >= 2)
        text += wxString::Format(_(" (part of %s and %s)"), bandName(partly[0]), bandName(partly[1]));
    return text;
}

wxString levelsText(int lo, int hi, double lat, const std::optional<cg::LevelCost>& cost) {
    const int n = hi - lo + 1;
    wxString text = n == 1 ? wxString(_("1 level.")) : wxString::Format(_("%d levels."), n);
    if (cost)
        text +=
            wxString::Format(_(" The finest, %s, is about %s of %s; one more level (%s) would add about %s."),
                             scaleOf(hi, lat), formatBytes(cost->finestBytes), formatBytes(cost->totalBytes),
                             scaleOf(hi + 1, lat), formatBytes(cost->nextBytes));
    return text;
}

wxString nextLevelText(int hi, double lat, std::int64_t nextBytes) {
    return wxString::Format(_("One more level (%s) would add about %s."), scaleOf(hi + 1, lat),
                            formatBytes(nextBytes));
}

wxString detailWarningText(const cg::DetailAdvice& a, cg::ZoomRange picked, double lat,
                           std::int64_t meanBytes) {
    if (a.kind == cg::DetailWarning::TooMuch) {
        wxString text =
            wxString::Format(_("Most detail %s needs about %s tiles for this area, about %s to download."),
                             scaleOf(picked.hi, lat), formatCount(a.tiles), formatBytes(a.tiles * meanBytes));
        text +=
            _(" Detail finer than 1:5k is for a harbour: draw a small area around it for that, as a second "
              "chart.");
        text += "\n\n";
        if (a.suggestedTiles > cg::kDetailWarnTiles)
            text += wxString::Format(_("Suggested: a smaller area, or most detail %s."),
                                     scaleOf(a.suggested.hi, lat));
        else
            text += wxString::Format(_("Suggested: most detail %s - about %s tiles, about %s."),
                                     scaleOf(a.suggested.hi, lat), formatCount(a.suggestedTiles),
                                     formatBytes(a.suggestedTiles * meanBytes));
        return text;
    }
    if (a.kind == cg::DetailWarning::TooLittle)
        return wxString::Format(_("Least detail %s shows whole oceans. It adds little to the download, but "
                                  "OpenCPN's own world chart already shows this."),
                                scaleOf(picked.lo, lat)) +
               "\n\n" + wxString::Format(_("Suggested: least detail %s."), scaleOf(a.suggested.lo, lat));
    return wxString();
}

wxString useScaleText(int zoom, double lat) {
    return wxString::Format(_("Use %s"), scaleOf(zoom, lat));
}

wxString keepScaleText(int zoom, double lat) {
    return wxString::Format(_("Keep %s"), scaleOf(zoom, lat));
}

wxString warnDetailCheckText() {
    return _("Warn before very large or very small detail choices");
}

wxString chartStateText(const cg::ChartRecord& r) {
    switch (r.state) {
    case cg::ChartState::Complete:
        return _("Complete");
    case cg::ChartState::Missing:
        return formatCount(r.missing) + _(" of ") + formatCount(r.tiles) + _(" tiles missing");
    case cg::ChartState::Interrupted:
        return _("Stopped");
    case cg::ChartState::Failed:
        return _("Failed");
    case cg::ChartState::Running:
        break;
    }
    return _("Downloading...");
}

wxString unreadableStateText() {
    return _("Unreadable");
}

wxString deleteChartQuestion(const wxString& name) {
    return _("Delete \"") + name + _("\"? The chart is removed from OpenCPN and its files are deleted.");
}

namespace {
wxString percentText(int permille) {
    return num(std::max(permille, 0) / 10) + " %";
}
} // namespace

wxString frozenRowText(const cg::ChartRecord& r) {
    const auto frozen = cg::frozenPermille(r);
    if (frozen && r.state == cg::ChartState::Interrupted)
        return _("Stopped at ") + percentText(*frozen);
    if (frozen && r.state == cg::ChartState::Failed)
        return _("Failed at ") + percentText(*frozen);
    return chartStateText(r);
}

wxString liveRowText(const cg::LiveProgress& p) {
    switch (p.stage) {
    case cg::LiveStage::Sizing:
        return _("Working out the size...");
    case cg::LiveStage::Downloading:
        return percentText(p.donePermille) + "  " +
               (p.secondsLeft ? _("about ") + formatDuration(*p.secondsLeft) + _(" left")
                              : wxString(_("time left unknown")));
    case cg::LiveStage::Checking:
        return _("Checking saved tiles...");
    case cg::LiveStage::Building:
        return _("Building the chart: ") + percentText(p.permille);
    case cg::LiveStage::RetryWait:
        return _("No connection, trying again in ") + num(p.retrySeconds) + _(" s");
    case cg::LiveStage::Stopping:
        break;
    }
    return _("Stopping at ") + percentText(p.donePermille);
}

wxString stoppingButtonText() {
    return _("Stopping...");
}

wxString oneJobAtATimeText() {
    return _("Chart Grabber downloads one chart at a time. The other actions return when it ends.");
}

wxString myChartsButtonText(std::size_t rows) {
    return _("My charts (") + num(static_cast<std::int64_t>(rows)) + _(")");
}

wxString creditText(const std::string& credit) {
    return _("Credit: ") + cg::fromUtf8(credit); // the credit is data, not translated
}

wxString updateWeatherMenuText() {
    return _("Update to latest weather");
}

wxString snapshotAgeText(std::int64_t seconds) {
    if (seconds < 0)
        return {}; // a clock behind the picture
    if (seconds < 3600)
        return num(std::max<std::int64_t>(1, seconds / 60)) + _(" min old");
    if (seconds < 172800)
        return num(seconds / 3600) + _(" h old");
    return num(seconds / 86400) + _(" days old");
}
