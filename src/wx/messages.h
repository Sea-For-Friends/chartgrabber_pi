// SPDX-License-Identifier: GPL-3.0-or-later
// Every English text of the plugin that is not a label, in one place
// 7.5, 7.6, 8.2, 9.1-9.4, 11). Pure functions: no window, no OpenCPN call. ASCII only in every literal
// ; names that come from data are converted with fromUtf8 and pass through unchanged.
#pragma once

#include "cli_records.h"
#include "geo_math.h"
#include "job_files.h"
#include "job_progress.h"
#include "portable.h"

#include <wx/string.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

wxString formatCount(std::int64_t n);      // groups of 3 with ' ': 950 -> "950", 1950 -> "1 950"
wxString formatBytes(std::int64_t bytes);  // MB = 1e6: < 10e6 "9.4 MB"; < 1e9 "38 MB"; else "9.1 GB"
wxString formatDuration(std::int64_t sec); // "45 s", "4 min", "1 h 05 min"
// Settings, Saved tiles : "<size> of saved tiles" or "No saved tiles yet." for 0 bytes, with
// " (a download is running)" appended when busy; and the question of Clear... (path as shown).
wxString savedTilesText(std::int64_t bytes, bool busy);
wxString clearSavedTilesQuestion(std::int64_t bytes, const wxString& path);

wxString bandName(cg::Band b); // "Passage" / "Coastal" / "Approach" / "Harbour"
wxString progressLine(const wxString& source, const cg::DownloadProgress& p);
wxString exportLine(const wxString& chartName, int permille);
wxString phaseLine(const std::string& phase, const wxString& source);
wxString outcomeHeadline(cg::Outcome o, const cg::FillTotals& fill, const std::vector<wxString>& chartNames,
                         std::string_view lastError);
wxString refusalText(std::string_view lastError);
wxString cacheRefusedText();
wxString tooLargeText();
wxString retryText(int attempt, int secondsLeft); // attempt is 1-based
wxString budgetStopText(int budgetMb);
wxString firstSuccessHint();
wxString jobFileMissingText();
wxString snapshotFailedText(); // exit 4: the weather source's latest picture could not be taken
wxString weatherPictureLine(const wxString& time); // Details: "Weather picture: <time>"
// Strip texts and button labels of the job controller.
wxString stoppingText();
wxString stopButtonText();
wxString showButtonText();
wxString resumeButtonText();
wxString closeButtonText();
wxString buildFromSavedButtonText();
wxString updateMenuText();                     // "Update..." (My charts More... menu, )
wxString updateTitleText();                    // title of the Update dialog
wxString updateQuestion(const wxString& name); // the Update dialog's question
wxString updateChoiceText(int i);              // i = 0..3: today / 1 month / 6 months / 1 year
wxString preferencesButtonText();
wxString stripHintText(); // the drag line at the top of the strip's Details until the first drop
// Ok and OverBudget give an empty text (the budget question is overBudgetQuestion).
wxString preflightText(cg::Preflight p, const cg::PreflightInput& in, const wxString& chartFolder,
                       const wxString& cacheFolder);
wxString areaErrorText(cg::AreaError e);
wxString sourceRefusalText(const cg::SourceRefusal& r);
wxString imageryWarningText();
wxString navigationNoteText(); // under the terms note for a source the CLI flags navigationWarning
wxString termsNoteText(); // one line under the Chart row for a source with a terms-of-use warning
// . The credit is data (not translated); empty: no credit part.
wxString weatherNoteText(const wxString& scale, const wxString& credit);
wxString weatherCoverageText();
wxString sourceLimitText(const wxString& name, const wxString& scale);
// The CLI texts of the host system; `os` selects the Windows texts, tested on Linux.
wxString cliNotFoundText(cg::Os os = cg::kHostOs);
wxString cliTooOldText(cg::Os os = cg::kHostOs);
wxString noSourcesText();

// Settings status lines. `version` may be empty; `line` is the last error line (Fails).
enum class CliStatus { Checking, Ready, NotInstalled, NotRunnable, TooOld, Fails };
wxString cliStatusText(CliStatus status, const std::string& version, const wxString& line,
                       cg::Os os = cg::kHostOs);
// Found: `records` source records, folder = the state of the sailor's own folder.
// BundledMissing: the plugin's own sources are missing (the CLI is not run).
enum class SourcesStatus { Checking, CliNotReady, BundledMissing, RunFailed, Found };
enum class FolderState { Missing, Empty, Holds };
wxString sourcesStatusText(SourcesStatus status, int records = 0, FolderState folder = FolderState::Empty);

// Texts of section 7.5 (Download) and section 11 (Settings) that are not part of the functions above.
wxString estimatingText();
wxString enterNameText();
wxString cannotWriteText(const wxString& path);
wxString overBudgetQuestion(std::int64_t bytesToDownload, int budgetMb);
wxString disclaimerText();
wxString dontShowAgainText();
wxString replaceQuestion();

// The fixed sentences of the New chart page, the drawing hint and the strip's seamark label.
enum class UiText {
    Seamarks,
    Cancel,
    DrawHint,
    NoViewport,
    NoRoutes,
    DownloadRunning,
    LoadingSources,
    NoUsableSource,
    SeamarksMissing,
    ChooseDetail,
    HarbourTooLarge,
};
wxString uiText(UiText t);
wxString unnamedRouteText(const wxString& guid); // "(unnamed " + first 8 characters + ")"
wxString corridorText(double nm);                // "2.0 NM"
wxString bandScaleText(cg::Band b, double lat);  // "1:2.3M-1:290k"
// The detail range texts; scales at the area's latitude.
wxString zoomEntryText(int zoom, double lat); // "1:580k (zoom 10)" at lat 0 (two spaces)
// "Detail set under Advanced: <scale lo> to <scale hi>" + "" / " (part of <band>)" / " (part of <band> and
// <band>)"
wxString detailRangeNote(int lo, int hi, double lat, const std::vector<cg::Band>& partly);
// "5 levels." and, with a cost, " The finest, <scale hi>, is about <MB> of <MB>; one more level (<scale
// hi+1>) would add about <MB>."
wxString levelsText(int lo, int hi, double lat, const std::optional<cg::LevelCost>& cost);
// "One more level (<scale hi+1>) would add about <MB>."
wxString nextLevelText(int hi, double lat, std::int64_t nextBytes);
// the extreme-level warning. `a` from cg::detailAdvice for the `picked` range;
// meanBytes = cg::meanTileBytes(records of the last exit-0 estimate, or {}); None -> empty.
wxString detailWarningText(const cg::DetailAdvice& a, cg::ZoomRange picked, double lat,
                           std::int64_t meanBytes);
wxString useScaleText(int zoom, double lat);  // "Use <scale>" (the default button)
wxString keepScaleText(int zoom, double lat); // "Keep <scale>" (Escape and the close box)
wxString warnDetailCheckText();               // "Warn before very large or very small detail choices"

// My charts rows.
wxString chartStateText(const cg::ChartRecord& r); // "Complete", "N of M tiles missing", "Interrupted", ...
wxString unreadableStateText();                    // "Unreadable"
wxString deleteChartQuestion(const wxString& name);
// Frozen rows: "Stopped at 63 %" / "Failed at 28 %" when frozenPermille is set, else chartStateText.
wxString frozenRowText(const cg::ChartRecord& r);
// The live row's words: "63 % about 12 min left", "Working out the size...", "Stopping at 63 %", ...
wxString liveRowText(const cg::LiveProgress& p);
wxString stoppingButtonText();                  // "Stopping..."
wxString oneJobAtATimeText();                   // the line above the list while a job runs
wxString myChartsButtonText(std::size_t rows);  // "My charts (3)"
wxString creditText(const std::string& credit); // "Credit: <credit>" (data, not translated)
wxString updateWeatherMenuText();               // "Update to latest weather"
// A weather picture's age: "" (seconds < 0), "1 min old", "59 min old", "1 h old", "47 h old", "2 days old".
wxString snapshotAgeText(std::int64_t seconds);
