// SPDX-License-Identifier: GPL-3.0-or-later
#include "new_chart_panel.h"

#include "area_tool.h"
#include "cli_runner.h"
#include "config_store.h"
#include "guard.h"
#include "job_controller.h"
#include "job_files.h"
#include "look.h"
#include "messages.h"
#include "plugin_log.h"
#include "portable.h"
#include "settings_dialog.h"
#include "sources_dialog.h"
#include "wx_util.h"

#include <wx/arrstr.h>
#include <wx/datetime.h>
#include <wx/dir.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/intl.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <wx/richmsgdlg.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/utils.h>
#include <wx/window.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string_view>
#ifndef __WINDOWS__
#include <sys/stat.h>
#endif
#include <utility>
#include <variant>

#include <ocpn_plugin.h>

namespace {

constexpr double kCorridorMin = 0.5, kCorridorMax = 20, kCorridorStep = 0.5; // NM,
constexpr std::int64_t kHarbourMaxTiles = 20000; // z17 + z18 tiles of the area
constexpr int kEstimateDebounceMs = 400;         //

constexpr int kPageWidth = 800;                  // DIP: the page's width on the 800 x 480 screen of
constexpr int kMinScrollHeight = 100;            // DIP: the rows keep at least this height beside the footer
constexpr int kRowMargins = 60;                  // px: borders and the scroll bar around the rows
constexpr int kThreadsMax = 4;                   // Advanced threads 1-4

// curated table, in the order of the choice. The first five GUIDs were found in the Chart Grabber
// CLI's `sources` output of the user's chart-source set (a ZMP chart-source folder) on 2026-10-15; the five
// rows are the CLI package's own sources (core master 10440c1d, data/map-sources/<file>/params.txt).
// Each GUID is in the table once.
struct Curated {
    std::string_view guid;
    bool satellite; // in the curated satellite set: imagery line, seamarks default on
    bool terms;     // the terms-of-use note shows for it
};
constexpr Curated kCurated[] = {
    {"{7B743985-BC5F-4AB6-8915-AC5DBBB8F552}", true, true},   // Esri World Imagery: ESRI/ArcGIS.Imagery.zmp:3
    {"{F6574B06-E632-4D5F-BC75-C8FA658B57DF}", true, false},  // Google Satellite: Google/GoogleSat.zmp:4
    {"{9A360A54-7A70-402D-8A11-D570BD759B5B}", true, false},  // Bing Aerial: Bing/Bing_Sat.zmp:4
    {"{92122135-6B15-4800-AA9E-3E48C556DA17}", true, false},  // Satellite/Sentinel2-Cloudless-2025.zmp
    {"{E05EDD5E-C6C3-483C-8BA7-8DE1D9892ED8}", true, false},  // Satellite/USGS-Imagery-USA.zmp
    {"{A830C100-49C8-4623-957A-60B85C39FBE0}", false, false}, // Navionics: Navionics/navionics.zmp:2
    {"{D643C5A1-4331-4387-9BE2-DDDF185C117E}", false, false}, // OSM Mapnik: _osm/Mapnik.zmp:4
    {"{51C1079B-B55D-4DF7-A410-ADC118F53AB1}", false, true},  // Coast and land/OpenTopoMap.zmp
    {"{C44877A0-D455-4238-B5E2-C8BFF25FC0D4}", false, false}, // Depth/EMODnet-Bathymetry-World.zmp
    {"{91D4F1B4-A7CD-4FC8-95F1-637609068FB2}", false, false}, // Depth/EMODnet-Bathymetry-Europe.zmp
    // the two weather pictures are bases, core 9b2ef52c, line 2 of params.txt
    {"{FE84A175-2C0F-420F-86DF-5900DE16A33C}", false, false}, // Weather now/EUMETView-GeoColour.zmp:2
    {"{8B9C244E-E978-4267-8FE4-D703534C0789}", false, false}, // Weather now/NASA-GIBS-VIIRS-TrueColor.zmp:2
    // the two EMODnet vessel-density layers:
    // GUIDs from the CLI 2026.10.8 `sources` records, tests/fixtures/cli-2026.10.8/sources-bundled-all.stdout
    {"{99D06D6A-F7FA-4F06-8C00-D9F96307C0F3}", false,
     false}, // Seamarks and traffic/EMODnet-Vessel-Density.zmp
    {"{478CBB2F-70D1-4573-8C50-B5361CE2AA46}", false,
     false}, // Seamarks and traffic/EMODnet-Vessel-Density-Sailing.zmp
};
// order of the weather choice (the first present is the default), core 9b2ef52c,
// data/sources/Weather now/<file>.zmp/params.txt:2
constexpr std::string_view kWeatherOrder[] = {
    "{1A696D82-F258-41A4-BCD6-7648D9E08DE2}", // RainViewer-Radar.zmp
    "{25E4232E-08A5-47C5-8D97-E38B5F418A2D}", // DWD-Radar-Germany.zmp
    "{076D1F9A-DC94-4184-B785-77362CD01B5B}", // NOAA-nowCOAST-Radar-CONUS.zmp
    "{BC2EA862-0F3D-4933-8F71-00AFA0BAD114}", // EUMETView-H60B-Precipitation.zmp
    "{92E40853-04A2-46B7-8A5B-3665FCDDFDBE}", // NASA-GIBS-IMERG-Precipitation.zmp
};
// Seamark layer (asLayer=1, EPSG 3785 -> 3857): __anygis_filegenerator/Osm_OpenSeaMap_Layer_Seamark.zmp/
// params.txt:3
constexpr std::string_view kSeamarkGuid = "{AB8A8685-AEC6-4E16-80DA-DC4AC37E4777}";

constexpr cg::Band kBandOrder[] = {cg::Band::Passage, cg::Band::Coastal, cg::Band::Approach,
                                   cg::Band::Harbour};

bool sameGuid(std::string_view a, std::string_view b) { // ASCII case-insensitive (CR 2)
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

bool isCurated(std::string_view guid) {
    return std::any_of(std::begin(kCurated), std::end(kCurated),
                       [&](const Curated& c) { return sameGuid(c.guid, guid); });
}

bool needsTermsNote(const cg::SourceRecord& r) {
    for (const Curated& c : kCurated)
        if (c.terms && sameGuid(c.guid, r.guid))
            return true;
    return false;
}

bool needsNavigationNote(const cg::SourceRecord& r) {
    return r.navigationWarning;
}

bool isSatellite(const cg::SourceRecord& r) {
    for (const Curated& c : kCurated)
        if (c.satellite && sameGuid(c.guid, r.guid))
            return true;
    return false;
}

// `text` in lines of at most `width` px in the font of `w`: broken at spaces, and inside a word that alone is
// wider than a line (a path).
wxString wrapped(const wxWindow* w, const wxString& text, int width) {
    const auto fits = [&](const wxString& t) { return w->GetTextExtent(t).x <= width; };
    wxString out, line;
    const auto push = [&](const wxString& l) { out += (out.empty() ? wxString() : wxString("\n")) + l; };
    for (wxString word : wxSplit(text, ' ', '\0')) {
        if (!line.empty() && fits(line + " " + word)) {
            line += " " + word;
            continue;
        }
        if (!line.empty())
            push(line);
        while (word.length() > 1 && !fits(word)) {
            size_t cut = word.length() - 1; // the longest prefix that fits, at least one character
            while (cut > 1 && !fits(word.Left(cut)))
                --cut;
            push(word.Left(cut));
            word = word.Mid(cut);
        }
        line = word;
    }
    push(line);
    return out;
}

wxString scaleText(int zoom, double lat) { // "1:23k": the scale of a level at a latitude
    return wxString::FromAscii(cg::formatScale(cg::scaleDenominator(zoom, lat)).c_str());
}

// Shows / hides a window; true when the visibility changed (the caller refits the dialog).
bool showWindow(wxWindow* w, bool on) {
    if (w->IsShown() == on)
        return false;
    w->Show(on);
    return true;
}

// The nearest EXISTING ancestor of a folder;
// empty when none exists.
wxString existingAncestor(wxString dir) {
    while (!dir.empty() && !wxDirExists(dir)) {
        const wxString parent = wxFileName(dir).GetPath(); // the last component is taken as a name
        if (parent == dir)                                 // reached the top without finding a folder
            return wxString();
        dir = parent;
    }
    return dir;
}

// Free bytes on the file system of `dir`; nullopt = unknown (the folder's check is skipped, ).
std::optional<std::uint64_t> freeBytes(const wxString& dir) {
    const wxString at = existingAncestor(dir);
    wxDiskspaceSize_t free = 0;
    if (at.empty() || !wxGetDiskSpace(at, nullptr, &free))
        return std::nullopt;
    return static_cast<std::uint64_t>(free.GetValue());
}

// Equal st_dev of the two nearest existing ancestors.
bool sameFileSystem(const wxString& a, const wxString& b) {
    const wxString pa = existingAncestor(a), pb = existingAncestor(b);
#ifdef __WINDOWS__
    return !pa.empty() && !pb.empty() && cg::sameVolume(cg::toUtf8(pa), cg::toUtf8(pb));
#else
    struct stat sa{}, sb{};
    return !pa.empty() && !pb.empty() && ::stat(pa.fn_str(), &sa) == 0 && ::stat(pb.fn_str(), &sb) == 0 &&
           sa.st_dev == sb.st_dev;
#endif
}

bool writeText(const wxString& path, const std::string& text) {
    wxFile f(path, wxFile::write);
    return f.IsOpened() && f.Write(text.data(), text.size()) == text.size();
}

wxString joinPath(const wxString& dir, const wxString& name) {
    return wxFileName(dir, name).GetFullPath();
}

} // namespace

NewChartPanel::NewChartPanel(wxWindow* parent, ConfigStore& config, const Look& look, AreaTool& area,
                             JobController& jobs)
    : wxPanel(parent), look_(look), area_(area), jobs_(jobs), config_(config), estimateTimer_(this) {
    const ConfigStore::LastChoice last = config.loadLastChoice();
    routeGuid_ = cg::fromUtf8(last.routeGuid);
    lastSource_ = last.sourceGuid;
    lastSeamarks_ = last.seamarks;
    weatherOn_ = last.weather;
    weatherPick_ = last.weatherGuid;
    range_ = cg::rangeFromText(last.advancedZooms);
    if (!range_)
        range_ =
            cg::rangeForBands(last.bands.empty() ? std::set<cg::Band>{cg::Band::Passage, cg::Band::Coastal}
                                                 : cg::makeContiguous(last.bands));
    corridorNm_ = std::clamp(std::round(config.loadSettings().corridorNm / kCorridorStep) * kCorridorStep,
                             kCorridorMin, kCorridorMax);

    captionWidth_ = GetTextExtent(_("Detail")).x + 24; // one width for every row caption (adds rows)
    // The rows scroll (vertical only) when they do not fit 800 x 480; the status line and Close / Download
    // below them stay in view.
    body_ = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL, "pageBody");
    body_->SetScrollRate(0, FromDIP(10));
    column_ = new wxBoxSizer(wxVERTICAL);
    // Area row.
    view_ = new wxToggleButton(body_, wxID_ANY, _("Current view"));
    draw_ = new wxToggleButton(body_, wxID_ANY, _("Draw on chart"));
    route_ = new wxToggleButton(body_, wxID_ANY, _("Along route"));
    noRoutes_ = new wxStaticText(body_, wxID_ANY, uiText(UiText::NoRoutes));
    auto* areaRow = new wxBoxSizer(wxHORIZONTAL);
    areaRow->Add(caption(_("Area")), 0, wxALIGN_CENTER_VERTICAL);
    areaRow->Add(view_, 0, wxRIGHT, 6);
    areaRow->Add(draw_, 0, wxRIGHT, 6);
    areaRow->Add(route_, 0, wxRIGHT, 10);
    areaRow->Add(noRoutes_, 1, wxALIGN_CENTER_VERTICAL);
    column_->Add(areaRow, 0, wxEXPAND | wxALL, 6);
    // Route row (Route mode only): the route and the corridor width.
    routeChoice_ = new wxChoice(body_, wxID_ANY);
    narrower_ = new wxButton(body_, wxID_ANY, _("-"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    wider_ = new wxButton(body_, wxID_ANY, _("+"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    corridor_ = new wxStaticText(body_, wxID_ANY, corridorText(kCorridorMax), wxDefaultPosition,
                                 wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL | wxST_NO_AUTORESIZE);
    corridor_->SetMinSize(wxSize(corridor_->GetTextExtent(corridorText(kCorridorMax)).x + 8, -1));
    routeRow_ = new wxBoxSizer(wxHORIZONTAL);
    routeRow_->AddSpacer(captionWidth_); // aligns with the area controls
    routeRow_->Add(routeChoice_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    routeRow_->Add(new wxStaticText(body_, wxID_ANY, _("Corridor width")), 0,
                   wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    routeRow_->Add(narrower_, 0, wxRIGHT, 4);
    routeRow_->Add(corridor_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    routeRow_->Add(wider_, 0);
    column_->Add(routeRow_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    // Chart row, imagery line, seamarks row.
    sourceChoice_ = new wxChoice(body_, wxID_ANY);
    moreSources_ = new wxButton(body_, wxID_ANY, _("More sources..."));
    auto* chartRow = new wxBoxSizer(wxHORIZONTAL);
    chartRow->Add(caption(_("Chart")), 0, wxALIGN_CENTER_VERTICAL);
    chartRow->Add(sourceChoice_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    chartRow->Add(moreSources_, 0, wxALIGN_CENTER_VERTICAL);
    column_->Add(chartRow, 0, wxEXPAND | wxALL, 6);
    imagery_ = new wxStaticText(body_, wxID_ANY, imageryWarningText());
    auto* imageryRow = new wxBoxSizer(wxHORIZONTAL);
    imageryRow->AddSpacer(captionWidth_);
    imageryRow->Add(imagery_, 1, wxALIGN_CENTER_VERTICAL);
    column_->Add(imageryRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    termsNote_ = new wxStaticText(body_, wxID_ANY, termsNoteText());
    auto* termsRow = new wxBoxSizer(wxHORIZONTAL);
    termsRow->AddSpacer(captionWidth_);
    // The bottom margin belongs to the note: a hidden note leaves a zero-height row.
    termsRow->Add(termsNote_, 1, wxALIGN_CENTER_VERTICAL | wxBOTTOM, 6);
    column_->Add(termsRow, 0, wxEXPAND | wxLEFT | wxRIGHT, 6);
    navNote_ = new wxStaticText(body_, wxID_ANY, navigationNoteText());
    auto* navRow = new wxBoxSizer(wxHORIZONTAL);
    navRow->AddSpacer(captionWidth_);
    navRow->Add(navNote_, 1, wxALIGN_CENTER_VERTICAL | wxBOTTOM, 6);
    column_->Add(navRow, 0, wxEXPAND | wxLEFT | wxRIGHT, 6);
    // The fixed notes are the widest texts; they break to what 800 px leave beside the caption column.
    imagery_->Wrap(FromDIP(kPageWidth) - captionWidth_ - kRowMargins);
    termsNote_->Wrap(FromDIP(kPageWidth) - captionWidth_ - kRowMargins);
    navNote_->Wrap(FromDIP(kPageWidth) - captionWidth_ - kRowMargins);
    seamarks_ = new wxCheckBox(body_, wxID_ANY, _("Seamarks on top (OpenSeaMap)"));
    seamarks_->SetValue(lastSeamarks_);
    seamarksNote_ = new wxStaticText(body_, wxID_ANY, wxString());
    auto* seamarksRow = new wxBoxSizer(wxHORIZONTAL);
    seamarksRow->AddSpacer(captionWidth_);
    seamarksRow->Add(seamarks_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    seamarksRow->Add(seamarksNote_, 1, wxALIGN_CENTER_VERTICAL);
    column_->Add(seamarksRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    // Weather on top [ ]: its own row under the seamarks row, then two lines while ticked.
    weather_ = new wxCheckBox(body_, wxID_ANY, _("Weather on top"));
    weatherChoice_ = new wxChoice(body_, wxID_ANY);
    weatherChoice_->SetName("weatherLayer");
    weatherChoice_->SetMinSize(wxSize(FromDIP(120), -1));
    weatherRow_ = new wxBoxSizer(wxHORIZONTAL);
    weatherRow_->AddSpacer(captionWidth_);
    weatherRow_->Add(weather_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    weatherRow_->Add(weatherChoice_, 1, wxALIGN_CENTER_VERTICAL);
    column_->Add(weatherRow_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    weatherNote_ = new wxStaticText(body_, wxID_ANY, wxString(), wxDefaultPosition, wxDefaultSize,
                                    wxST_ELLIPSIZE_END | wxST_NO_AUTORESIZE);
    weatherNote_->SetMinSize(wxSize(FromDIP(60), -1));
    weatherCoverage_ = new wxStaticText(body_, wxID_ANY, weatherCoverageText());
    weatherNoteRow_ = new wxBoxSizer(wxHORIZONTAL);
    weatherNoteRow_->AddSpacer(captionWidth_);
    weatherNoteRow_->Add(weatherNote_, 1, wxALIGN_CENTER_VERTICAL);
    column_->Add(weatherNoteRow_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    weatherCoverageRow_ = new wxBoxSizer(wxHORIZONTAL);
    weatherCoverageRow_->AddSpacer(captionWidth_);
    weatherCoverageRow_->Add(weatherCoverage_, 1, wxALIGN_CENTER_VERTICAL);
    column_->Add(weatherCoverageRow_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    // Detail row: four bands with their scale ranges, then one note line.
    auto* detailRow = new wxBoxSizer(wxHORIZONTAL);
    detailRow->Add(caption(_("Detail")), 0, wxALIGN_TOP);
    const wxString bandNames[] = {_("Passage"), _("Coastal"), _("Approach"), _("Harbour")};
    for (std::size_t i = 0; i < bandBoxes_.size(); ++i) {
        bandBoxes_[i] = new wxCheckBox(body_, wxID_ANY, bandNames[i]);
        bandScales_[i] = new wxStaticText(body_, wxID_ANY, wxString());
        auto* one = new wxBoxSizer(wxVERTICAL);
        one->Add(bandBoxes_[i], 0);
        one->Add(bandScales_[i], 0, wxLEFT, 22);
        detailRow->Add(one, 0, wxRIGHT, 14);
    }
    column_->Add(detailRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    detailNote_ = new wxStaticText(body_, wxID_ANY, wxString());
    auto* noteRow = new wxBoxSizer(wxHORIZONTAL);
    noteRow->AddSpacer(captionWidth_);
    noteRow->Add(detailNote_, 1, wxALIGN_CENTER_VERTICAL);
    column_->Add(noteRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    // Estimate line.
    estimate_ = new wxStaticText(body_, wxID_ANY, wxString());
    auto* estimateRow = new wxBoxSizer(wxHORIZONTAL);
    estimateRow->AddSpacer(captionWidth_);
    estimateRow->Add(estimate_, 1, wxALIGN_CENTER_VERTICAL);
    column_->Add(estimateRow, 0, wxEXPAND | wxLEFT | wxRIGHT, 6); // the Name row has its own top margin
    nextLevel_ = new wxStaticText(body_, wxID_ANY, wxString());   // what one more level would add
    auto* nextLevelRow = new wxBoxSizer(wxHORIZONTAL);
    nextLevelRow->AddSpacer(captionWidth_);
    nextLevelRow->Add(nextLevel_, 1, wxALIGN_CENTER_VERTICAL);
    column_->Add(nextLevelRow, 0, wxEXPAND | wxLEFT | wxRIGHT, 6);
    // Name row.
    name_ = new wxTextCtrl(body_, wxID_ANY);
    auto* nameRow = new wxBoxSizer(wxHORIZONTAL);
    nameRow->Add(caption(_("Name")), 0, wxALIGN_CENTER_VERTICAL);
    nameRow->Add(name_, 1, wxALIGN_CENTER_VERTICAL);
    column_->Add(nameRow, 0, wxEXPAND | wxALL, 6);
    // Advanced fold (collapsed): the detail range as two drop-downs threads and the output folder
    // .
    advanced_ = new wxCollapsiblePane(body_, wxID_ANY, _("Advanced"));
    const int detailCaptionWidth =
        std::max(GetTextExtent(_("Least detail")).x, GetTextExtent(_("Most detail")).x) + 6;
    const auto choiceRow = [&](const wxString& text, const char* name, wxChoice*& choice) {
        choice = new wxChoice(advanced_->GetPane(), wxID_ANY);
        choice->SetName(name);
        for (int z = 0; z <= cg::kMaxZoom; ++z)
            choice->Append(zoomEntryText(z, 0)); // the texts follow the area (updateDetail)
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* label = new wxStaticText(advanced_->GetPane(), wxID_ANY, text);
        label->SetMinSize(wxSize(detailCaptionWidth, -1));
        row->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        row->Add(choice, 1, wxALIGN_CENTER_VERTICAL);
        return row;
    };
    auto* leastRow = choiceRow(_("Least detail"), "leastDetail", least_);
    auto* mostRow = choiceRow(_("Most detail"), "mostDetail", most_);
    levels_ = new wxStaticText(advanced_->GetPane(), wxID_ANY, wxString());
    threads_ = new wxSpinCtrl(advanced_->GetPane(), wxID_ANY, wxString(), wxDefaultPosition, wxDefaultSize,
                              wxSP_ARROW_KEYS, 1, kThreadsMax,
                              std::clamp(config.loadSettings().threads, 1, kThreadsMax));
    auto* threadsRow = new wxBoxSizer(wxHORIZONTAL);
    threadsRow->Add(new wxStaticText(advanced_->GetPane(), wxID_ANY, _("Parallel downloads")), 0,
                    wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    threadsRow->Add(threads_, 0, wxALIGN_CENTER_VERTICAL);
    folder_ = new wxStaticText(advanced_->GetPane(), wxID_ANY, wxString(), wxDefaultPosition, wxDefaultSize,
                               wxST_ELLIPSIZE_MIDDLE);
    folder_->SetMinSize(wxSize(300, -1));
    chooseFolder_ = new wxButton(advanced_->GetPane(), wxID_ANY, _("Choose..."));
    auto* folderRow = new wxBoxSizer(wxHORIZONTAL);
    folderRow->Add(new wxStaticText(advanced_->GetPane(), wxID_ANY, _("Save to")), 0,
                   wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    folderRow->Add(folder_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    folderRow->Add(chooseFolder_, 0, wxALIGN_CENTER_VERTICAL);
    auto* paneColumn = new wxBoxSizer(wxVERTICAL);
    paneColumn->Add(leastRow, 0, wxEXPAND);
    paneColumn->Add(mostRow, 0, wxEXPAND | wxTOP, 4);
    paneColumn->Add(levels_, 0, wxEXPAND | wxTOP, 4);
    paneColumn->Add(threadsRow, 0, wxEXPAND | wxTOP, 4);
    paneColumn->Add(folderRow, 0, wxEXPAND | wxTOP, 4);
    advanced_->GetPane()->SetSizer(paneColumn);
    column_->Add(advanced_, 0, wxEXPAND | wxALL, 6);
    body_->SetSizer(column_);
    auto* page = new wxBoxSizer(wxVERTICAL);
    page->Add(body_, 1, wxEXPAND);
    // Status line (with the Preferences button) and the bottom row.
    status_ = new wxStaticText(this, wxID_ANY, wxString());
    preferences_ = new wxButton(this, wxID_ANY, preferencesButtonText());
    auto* statusRow = new wxBoxSizer(wxHORIZONTAL);
    statusRow->Add(status_, 1, wxALIGN_CENTER_VERTICAL);
    statusRow->Add(preferences_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 10);
    footer_ = new wxBoxSizer(wxVERTICAL);
    footer_->Add(statusRow, 0, wxEXPAND | wxALL, 6);
    statusWrap_ = FromDIP(kPageWidth) - 12 - 10 - preferences_->GetBestSize().x;
    auto* close = new wxButton(this, wxID_CLOSE, _("Close"));
    download_ = new wxButton(this, wxID_ANY, _("Download"));
    busy_ = new wxStaticText(this, wxID_ANY, uiText(UiText::DownloadRunning));
    auto* bottom = new wxBoxSizer(wxHORIZONTAL);
    bottom->Add(close, 0, wxALIGN_CENTER_VERTICAL);
    bottom->AddStretchSpacer(1);
    bottom->Add(busy_, 0, wxALIGN_CENTER_VERTICAL);
    bottom->Add(download_, 0, wxALIGN_CENTER_VERTICAL);
    footer_->Add(bottom, 0, wxEXPAND | wxALL, 6);
    page->Add(footer_, 0, wxEXPAND);
    SetSizer(page);

    // Touch: every control at least minControlHeight high. Zero outside touch mode.
    if (const int minHeight = look.minControlHeight(); minHeight > 0)
        for (wxWindow* w : std::initializer_list<wxWindow*>{
                 view_, draw_, route_, routeChoice_, narrower_, wider_, name_, close, download_,
                 sourceChoice_, moreSources_, seamarks_, weather_, weatherChoice_, least_, most_,
                 preferences_, threads_, chooseFolder_})
            w->SetMinSize(wxSize(-1, minHeight));

    view_->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) {
        cg::guarded("new chart view", [&] {
            if (!closing_)
                setArea(cg::AreaKind::View, std::nullopt);
        });
    });
    route_->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) {
        cg::guarded("new chart route", [&] {
            if (!closing_)
                setArea(cg::AreaKind::Route, std::nullopt);
        });
    });
    draw_->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) {
        cg::guarded("new chart draw", [&] {
            if (closing_)
                return;
            showKind(); // the kind changes only when the drawing ends with a box
            if (onDraw)
                onDraw();
        });
    });
    routeChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        cg::guarded("new chart route choice", [&] {
            const int i = routeChoice_->GetSelection();
            if (closing_ || i == wxNOT_FOUND || std::size_t(i) >= routes_.size())
                return;
            routeGuid_ = routes_[i].guid;
            checkRouteArea();
            updateArea();
            fitDialog();
            scheduleEstimate();
            updateDownloadState();
        });
    });
    for (auto [button, delta] : {std::pair{narrower_, -kCorridorStep}, std::pair{wider_, kCorridorStep}})
        button->Bind(wxEVT_BUTTON, [this, delta](wxCommandEvent&) {
            cg::guarded("new chart corridor", [&] {
                if (closing_)
                    return;
                corridorNm_ = std::clamp(corridorNm_ + delta, kCorridorMin, kCorridorMax);
                showCorridor();
                updateArea();
                fitDialog();
                scheduleEstimate();
                updateDownloadState();
            });
        });
    name_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
        cg::guarded("new chart name", [&] { // ChangeValue (the default name) sends no event
            if (closing_)
                return;
            nameEdited_ = !name_->GetValue().empty(); // an emptied field is default again
            updateDownloadState();
        });
    });
    download_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        cg::guarded("new chart download", [&] {
            if (!closing_)
                startDownload();
        });
    });
    chooseFolder_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        cg::guarded("new chart folder", [&] {
            if (!closing_)
                chooseOutputFolder();
        });
    });
    Bind(
        wxEVT_TIMER,
        [this](wxTimerEvent&) { cg::guarded("new chart estimate timer", [&] { startEstimate(); }); },
        estimateTimer_.GetId());
    sourceChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        cg::guarded("new chart source", [&] {
            if (!closing_)
                chartChosen();
        });
    });
    moreSources_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        cg::guarded("new chart more sources", [&] {
            if (!closing_)
                openMoreSources();
        });
    });
    preferences_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        cg::guarded("new chart preferences", [&] {
            if (!closing_)
                openPreferences();
        });
    });
    seamarks_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        cg::guarded("new chart seamarks", [&] {
            if (closing_)
                return;
            scheduleEstimate();
            updateDownloadState();
        });
    });
    weather_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        cg::guarded("new chart weather", [&] { // the weather chart is not estimated
            if (closing_)
                return;
            weatherOn_ = weather_->GetValue();
            updateWeather();
            fitDialog();
        });
    });
    weatherChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        cg::guarded("new chart weather layer", [&] {
            const int i = weatherChoice_->GetSelection();
            if (closing_ || i == wxNOT_FOUND || std::size_t(i) >= weatherGuids_.size())
                return;
            weatherPick_ = weatherGuids_[i];
            updateWeather();
            fitDialog();
        });
    });
    for (std::size_t i = 0; i < bandBoxes_.size(); ++i)
        bandBoxes_[i]->Bind(wxEVT_CHECKBOX, [this, i](wxCommandEvent&) {
            cg::guarded("new chart band", [&] {
                if (!closing_)
                    bandToggled(i);
            });
        });
    least_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        cg::guarded("new chart least detail", [&] {
            if (const int z = least_->GetSelection(); !closing_ && z != wxNOT_FOUND) {
                range_ = cg::pickLeast(range_, z);
                rangeChanged();
                warnAfterPick(false, z);
            }
        });
    });
    most_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        cg::guarded("new chart most detail", [&] {
            if (const int z = most_->GetSelection(); !closing_ && z != wxNOT_FOUND) {
                range_ = cg::pickMost(range_, z);
                rangeChanged();
                warnAfterPick(true, z);
            }
        });
    });
    advanced_->Bind(wxEVT_COLLAPSIBLEPANE_CHANGED, [this](wxCollapsiblePaneEvent&) {
        cg::guarded("new chart advanced", [&] {
            if (!closing_)
                fitDialog();
        });
    });

    look.apply(this);
    lookToken_ = look.subscribe([this] {
        if (!closing_)
            look_.apply(this);
    });
    showCorridor();
    setBusy(jobs.busy());
    setArea(cg::AreaKind::View, std::nullopt); // a page is never without an area kind
    reloadSources();
}

NewChartPanel::~NewChartPanel() {
    closing_ = true;
    delete detailQuestion_.get();
    delete question_.get(); // its callback holds this panel; immediate, not Destroy
    delete sourcesDialog_.get();
    delete settingsDialog_.get();
    estimateTimer_.Stop();
    sourcesRunner_.reset(); // may pump events up to 15 s; its callbacks only return (closing_)
    estimateRunner_.reset();
    look_.unsubscribe(lookToken_);
}

void NewChartPanel::limitHeight(int pixels) {
    heightLimit_ = pixels;
    fitDialog();
}

// The status text, broken into lines that fit statusWrap_ (a path or a CLI line may be any length).
void NewChartPanel::showStatus(const wxString& text) {
    statusText_ = text;
    status_->SetLabelText(wrapped(status_, text, statusWrap_));
}

wxStaticText* NewChartPanel::caption(const wxString& text) {
    auto* t = new wxStaticText(body_, wxID_ANY, text);
    t->SetMinSize(wxSize(captionWidth_, -1));
    return t;
}

// Layout and Fit of the dialog after any change of what is visible; the best size of every ancestor is
// invalidated first (a hidden line gives its space back).
void NewChartPanel::fitDialog() {
    for (wxWindow* w = this; w; w = w->GetParent())
        w->InvalidateBestSize();
    // The rows take their height up to what the screen leaves; beyond that they scroll and the scroll bar's
    // width is added so no row is narrower than its best size. The footer (status, Close, Download) stays.
    const wxSize want = column_->CalcMin();
    const int room = std::max(heightLimit_ - footer_->CalcMin().y, FromDIP(kMinScrollHeight));
    const int bar = want.y > room ? wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, body_) : 0;
    body_->ShowScrollbars(wxSHOW_SB_NEVER, bar ? wxSHOW_SB_ALWAYS : wxSHOW_SB_NEVER); // none left over
    body_->SetMinSize(wxSize(want.x + bar, std::min(want.y, room)));
    Layout();
    if (wxWindow* top = wxGetTopLevelParent(this)) {
        top->SetMinSize(
            wxDefaultSize); // Fit pinned the last size as the minimum: a narrower page must shrink it
        top->Layout();
        top->Fit();
    }
}

int NewChartPanel::routeIndex(const wxString& guid) const {
    if (guid.empty())
        return -1;
    for (std::size_t i = 0; i < routes_.size(); ++i)
        if (routes_[i].guid == guid)
            return int(i);
    return -1;
}

// the routes and their points are copied while the list is read; the waypoints GetRoute_Plugin
// returns are the caller's (5.10.2 pluginmanager.cpp:7278-7303, ~PlugIn_Route does not delete them).
void NewChartPanel::readRoutes() {
    routes_.clear();
    const wxArrayString guids = GetRouteGUIDArray();
    for (const wxString& guid : guids) {
        const auto r = GetRoute_Plugin(guid);
        if (!r)
            continue;
        RouteInfo info{guid, r->m_NameString.empty() ? unnamedRouteText(guid) : r->m_NameString, {}};
        if (r->pWaypointList) {
            for (auto* node = r->pWaypointList->GetFirst(); node; node = node->GetNext()) {
                info.points.push_back({node->GetData()->m_lat, node->GetData()->m_lon});
                auto* wp = node->GetData();
                if (wp->m_HyperlinkList) { // 5.10.2 PlugInFromRoutePoint news the list and each link
                    for (auto* link = wp->m_HyperlinkList->GetFirst(); link; link = link->GetNext())
                        delete link->GetData();
                    delete wp->m_HyperlinkList;
                }
                delete wp;
            }
        }
        routes_.push_back(std::move(info));
    }
    routeChoice_->Clear();
    for (const RouteInfo& r : routes_)
        routeChoice_->Append(r.name);
    if (!routes_.empty())
        routeChoice_->SetSelection(std::max(0, routeIndex(routeGuid_)));
}

void NewChartPanel::captureView() {
    const auto vp = area_.focusViewport();
    if (!vp) {
        areaReason_ = uiText(UiText::NoViewport);
        return;
    }
    cg::AreaError e = cg::AreaError::Empty;
    viewBox_ = checkedBox({vp->lat_min, vp->lon_min}, {vp->lat_max, vp->lon_max}, &e);
    if (!viewBox_)
        areaReason_ = areaErrorText(e);
}

void NewChartPanel::checkRouteArea() {
    areaReason_.clear();
    const int i = routeChoice_->GetSelection();
    if (i == wxNOT_FOUND || std::size_t(i) >= routes_.size())
        return; // unreachable in Route mode: setArea falls back to View without routes
    if (const auto ok = cg::checkRoute(routes_[i].points); !ok)
        areaReason_ = areaErrorText(ok.error());
}

void NewChartPanel::showKind() {
    view_->SetValue(kind_ == cg::AreaKind::View);
    draw_->SetValue(kind_ == cg::AreaKind::Rectangle);
    route_->SetValue(kind_ == cg::AreaKind::Route);
}

void NewChartPanel::showCorridor() {
    corridor_->SetLabelText(corridorText(corridorNm_));
    narrower_->Enable(corridorNm_ > kCorridorMin);
    wider_->Enable(corridorNm_ < kCorridorMax);
}

// keepRoute (the way back from the toolbar): the route being shown stays while it is still listed; otherwise
// (first open, menu, 'Along route') the full preselection runs and only a user's choice becomes routeGuid_.
void NewChartPanel::setArea(cg::AreaKind kind, std::optional<cg::BBox> box, bool keepRoute) {
    wxString shown;
    if (const int s = routeChoice_->GetSelection();
        keepRoute && s != wxNOT_FOUND && std::size_t(s) < routes_.size())
        shown = routes_[s].guid;
    readRoutes();
    if (kind == cg::AreaKind::Rectangle && !box)
        kind = cg::AreaKind::View;
    if (kind == cg::AreaKind::Route && routes_.empty())
        kind = cg::AreaKind::View;
    kind_ = kind;
    areaReason_.clear();
    viewBox_.reset();
    if (kind == cg::AreaKind::Rectangle) {
        rectangle_ = box;
    } else if (kind == cg::AreaKind::View) {
        captureView();
    } else {
        int i = routeIndex(shown);
        if (i < 0)
            i = routeIndex(GetSelectedRouteGUID_Plugin()); // selected, active, last chosen, first
        if (i < 0)
            i = routeIndex(GetActiveRouteGUID());
        if (i < 0)
            i = routeIndex(routeGuid_);
        i = std::max(i, 0);
        routeChoice_->SetSelection(i);
        checkRouteArea();
    }
    showKind();
    route_->Enable(!routes_.empty());
    noRoutes_->Show(routes_.empty());
    column_->Show(routeRow_, kind_ == cg::AreaKind::Route);
    updateArea();
    fitDialog();
    outlineArea(true);
    scheduleEstimate();
    updateDownloadState();
}

void NewChartPanel::refreshArea() {
    setArea(kind_, rectangle_, true);
}

void NewChartPanel::outlineArea(bool show) {
    area_.setShownArea(show && kind_ == cg::AreaKind::Rectangle ? rectangle_ : std::nullopt);
}

void NewChartPanel::setBusy(bool busy) {
    download_->Show(!busy);
    busy_->Show(busy);
    fitDialog();
    updateDownloadState(); // Download is enabled again when the job ends
}

// ---- More sources ----

void NewChartPanel::openMoreSources() {
    if (sourcesDialog_ && sourcesDialog_->IsShown()) { // a second request raises the open window
        sourcesDialog_->Raise();
        return;
    }
    Enable(false);
    auto* dlg = new SourcesDialog(wxGetTopLevelParent(this), sources_, look_);
    dlg->onDone = [this](int result, std::string guid) {
        if (closing_)
            return;
        Enable(true);
        if (result == wxID_RETRY) {
            reloadSources();
            return;
        }
        pickSource(result == wxID_OK ? guid : std::string());
    };
    sourcesDialog_ = dlg;
    dlg->Show();
    dlg->Raise();
}

void NewChartPanel::pickSource(const std::string& guid) {
    const cg::SourceRecord* pick = guid.empty() ? nullptr : findSource(guid);
    if (!pick)
        return;
    // The pick takes the last slot of the choice (the LastSource slot), replacing a previous pick; a curated
    // source that is in the choice is simply selected.
    int at = -1;
    for (std::size_t i = 0; i < choiceGuids_.size(); ++i)
        if (sameGuid(choiceGuids_[i], pick->guid))
            at = int(i);
    if (at < 0) {
        if (!choiceGuids_.empty() && !isCurated(choiceGuids_.back())) { // the previous LastSource / pick slot
            choiceGuids_.pop_back();
            sourceChoice_->Delete(sourceChoice_->GetCount() - 1);
        }
        choiceGuids_.push_back(pick->guid);
        sourceChoice_->Append(cg::fromUtf8(cg::choiceLabel(pick->name)));
        at = int(choiceGuids_.size()) - 1;
    }
    sourceChoice_->SetSelection(at);
    sourceChoice_->Enable(true);
    sourceReason_.clear();
    chartChosen();
}

// ---- Sources ----

void NewChartPanel::reloadSources() {
    settings_ = config_.loadSettings();
    if (sourcesRunner_ && sourcesRunner_->running()) {
        sourcesRunner_->terminate();
        pendingReload_ = true; // onExit discards this run's result and calls reloadSources again
        return;
    }
    sources_.clear();
    lastError_.clear();
    choiceGuids_.clear();
    sourcesRunning_ = true;
    sourceFailure_ = false;
    sourceChoice_->Clear();
    sourceChoice_->Append(uiText(UiText::LoadingSources));
    sourceChoice_->SetSelection(0);
    sourceChoice_->Enable(false);
    moreSources_->Enable(false);
    sourceReason_ = uiText(UiText::LoadingSources);
    updateSeamarks();
    updateImagery();
    fillWeather();
    updateDownloadState();
    fitDialog();

    std::string why;
    const auto cli = resolveCli(settings_, &why);
    if (!cli) {
        cg::logLine(cg::fromUtf8(why));
        sourceFailed(cliNotFoundText());
        return;
    }
    if (!sourcesRunner_)
        sourcesRunner_ = std::make_unique<CliRunner>();
    if (!probeCli(*cli)) {
        sourceFailed(cliTooOldText());
        return;
    }
    const std::vector<std::string> argv =
        cg::cliCommand(*cli, cg::sourcesArguments(config_.cliSettings(settings_)), false);
    CliRunner::Callbacks cb;
    cb.onRecord = [this](const cg::StdoutRecord& r) {
        if (const auto* s = std::get_if<cg::SourceRecord>(&r))
            sources_.push_back(*s);
    };
    cb.onStderr = [this](const cg::StderrLine& l) {
        if (const auto* m = std::get_if<cg::MessageLine>(&l); m && m->kind == cg::MessageLine::Kind::Error)
            lastError_ = m->text;
    };
    cb.onExit = [this](int code, bool) {
        if (closing_)
            return;
        if (pendingReload_) {
            pendingReload_ = false;
            reloadSources();
            return;
        }
        if (code == 0 && sources_.empty()) {
            sourceFailed(noSourcesText());
        } else if (code != 0) {
            sourceFailed(cliStatusText(CliStatus::Fails, "",
                                       lastError_.empty() ? wxString::Format(_("error %d"), code)
                                                          : cg::fromUtf8(lastError_)));
        } else {
            fillSources();
        }
    };
    std::string error;
    if (!sourcesRunner_->start(argv, std::move(cb), &error)) {
        cg::logLine(cg::fromUtf8(error));
        sourceFailed(cliNotFoundText());
    }
}

// A failed run's records are not used; the text is the status reason and offers Preferences.
void NewChartPanel::sourceFailed(const wxString& text) {
    sourcesRunning_ = false;
    sources_.clear();
    choiceGuids_.clear();
    sourceChoice_->Clear();
    sourceChoice_->Enable(false);
    moreSources_->Enable(false);
    sourceFailure_ = true;
    sourceReason_ = text;
    updateSeamarks();
    updateImagery();
    fillWeather();
    updateDownloadState();
    fitDialog();
}

const cg::SourceRecord* NewChartPanel::findSource(std::string_view guid) const {
    for (const cg::SourceRecord& r : sources_)
        if (sameGuid(r.guid, guid))
            return &r;
    return nullptr;
}

const cg::SourceRecord* NewChartPanel::selectedSource() const {
    const int i = sourceChoice_->GetSelection();
    if (i == wxNOT_FOUND || std::size_t(i) >= choiceGuids_.size())
        return nullptr;
    return findSource(choiceGuids_[i]);
}

// chart choice: curated chart sources present and usable, then LastSource, else the first usable
// base source.
void NewChartPanel::fillSources() {
    sourcesRunning_ = false;
    std::vector<const cg::SourceRecord*> entries;
    auto add = [&](const cg::SourceRecord* r) {
        if (r && (r->kind == "map" || (r->isLayer() && isCurated(r->guid))) && r->usable() && //

            std::none_of(entries.begin(), entries.end(),
                         [&](const cg::SourceRecord* e) { return sameGuid(e->guid, r->guid); }))
            entries.push_back(r);
    };
    for (const Curated& c : kCurated)
        add(findSource(c.guid));
    if (!lastSource_.empty())
        add(findSource(lastSource_));
    if (entries.empty()) {
        for (const std::size_t i : cg::sourcesDialogOrder(sources_)) {
            add(&sources_[i]);
            if (!entries.empty())
                break;
        }
    }
    sourceChoice_->Clear();
    choiceGuids_.clear();
    int selected = 0;
    for (const cg::SourceRecord* r : entries) {
        if (!lastSource_.empty() && sameGuid(r->guid, lastSource_))
            selected = int(choiceGuids_.size());
        sourceChoice_->Append(cg::fromUtf8(cg::choiceLabel(r->name)));
        choiceGuids_.push_back(r->guid);
    }
    sourceFailure_ = false;
    sourceReason_ = entries.empty() ? uiText(UiText::NoUsableSource) : wxString();
    sourceChoice_->Enable(!entries.empty());
    moreSources_->Enable(true);
    if (!entries.empty()) {
        sourceChoice_->SetSelection(selected);
        const cg::SourceRecord* base = entries[selected];
        const bool changed = firstFill_ || !sameGuid(base->guid, baseGuid_);
        baseGuid_ = base->guid; // a preselection is not a pick: lastSource_ stays
        const cg::SourceRecord* layer = findSource(kSeamarkGuid);
        if (layer && layer->usable() && changed)
            seamarks_->SetValue(firstFill_ ? lastSeamarks_ : isSatellite(*base));
        firstFill_ = false;
    }
    updateSeamarks();
    updateImagery();
    fillWeather();
    showRange(); // the source-limit note follows the base
    updateDefaultName();
    scheduleEstimate();
    updateDownloadState();
    fitDialog();
}

void NewChartPanel::chartChosen() {
    const int i = sourceChoice_->GetSelection();
    if (i == wxNOT_FOUND || std::size_t(i) >= choiceGuids_.size())
        return;
    baseGuid_ = lastSource_ = choiceGuids_[i];
    if (const cg::SourceRecord* base = selectedSource(); base && seamarks_->IsEnabled())
        seamarks_->SetValue(isSatellite(*base));
    updateImagery();
    showRange(); // the source-limit note follows the base
    updateDefaultName();
    fitDialog();
    scheduleEstimate();
    updateDownloadState();
}

// the checkbox follows the seamark layer; while the sources run or failed it keeps its value.
void NewChartPanel::updateSeamarks() {
    if (sourcesRunning_ || sources_.empty()) {
        seamarks_->Enable(false);
        showWindow(seamarksNote_, false);
        return;
    }
    wxString note;
    if (const cg::SourceRecord* layer = findSource(kSeamarkGuid); !layer)
        note = uiText(UiText::SeamarksMissing);
    else if (const cg::SourceRefusal r = cg::sourceRefusal(*layer); r.kind != cg::SourceRefusal::Kind::None)
        note = sourceRefusalText(r);
    seamarks_->Enable(note.empty());
    if (!note.empty())
        seamarks_->SetValue(false);
    seamarksNote_->SetLabelText(note);
    showWindow(seamarksNote_, !note.empty());
}

void NewChartPanel::updateImagery() {
    const cg::SourceRecord* base = selectedSource();
    showWindow(imagery_, base && (isSatellite(*base) || cg::nameLooksLikeImagery(base->name)));
    showWindow(termsNote_, base && needsTermsNote(*base));
    showWindow(navNote_, base && needsNavigationNote(*base));
    sourceChoice_->SetToolTip(base ? cg::fromUtf8(base->name) : wxString()); // the full name
}

// the choice lists the usable weather layers (table order first); the user's pick
// is kept across a reload while it is still listed, else the first entry.
void NewChartPanel::fillWeather() {
    weatherGuids_.clear();
    weatherChoice_->Clear();
    if (!sourcesRunning_) {
        std::vector<const cg::SourceRecord*> layers;
        for (const cg::SourceRecord& r : sources_)
            if (r.isWeather() && r.isLayer() && r.usable())
                layers.push_back(&r);
        // the table first, in its order, then the others in CLI order
        const auto rank = [](const cg::SourceRecord* r) {
            return std::find_if(std::begin(kWeatherOrder), std::end(kWeatherOrder),
                                [&](std::string_view g) { return sameGuid(g, r->guid); }) -
                   std::begin(kWeatherOrder); // not listed: after the table
        };
        std::stable_sort(
            layers.begin(), layers.end(),
            [&](const cg::SourceRecord* a, const cg::SourceRecord* b) { return rank(a) < rank(b); });
        for (const cg::SourceRecord* r : layers) {
            weatherGuids_.push_back(r->guid);
            weatherChoice_->Append(cg::fromUtf8(cg::choiceLabel(r->name)));
        }
    }
    int at = 0;
    for (std::size_t i = 0; i < weatherGuids_.size(); ++i)
        if (sameGuid(weatherGuids_[i], weatherPick_))
            at = int(i);
    if (!weatherGuids_.empty())
        weatherChoice_->SetSelection(at);
    updateWeather();
}

// The row, its choice, its two lines and the layer's tool tip. The caller refits.
void NewChartPanel::updateWeather() {
    const bool listed = !weatherGuids_.empty();
    weather_->SetValue(listed && weatherOn_);
    const cg::SourceRecord* layer = weatherRecord();
    weatherChoice_->Enable(layer != nullptr);
    column_->Show(weatherRow_, listed);
    column_->Show(weatherNoteRow_, layer != nullptr);
    column_->Show(weatherCoverageRow_, layer != nullptr);
    if (layer)
        weatherChoice_->SetToolTip(cg::fromUtf8(layer->name)); // the full name, whatever the label cuts
    if (!layer)
        return;
    const double lat = areaBox_ ? cg::centre(*areaBox_).lat : 0;
    const std::optional<int> limit = cg::zoomLimit(*layer);
    const std::vector<int> zooms = cg::limitZooms(zoomList(), limit);
    const wxString text =
        weatherNoteText(scaleText(zooms.empty() ? limit.value_or(cg::kWeatherZoomCap) : zooms.back(), lat),
                        cg::fromUtf8(layer->license));
    weatherNote_->SetLabelText(text);
    weatherNote_->SetToolTip(text);
}

// ---- Detail ----

void NewChartPanel::updateArea() {
    areaBox_.reset();
    if (kind_ == cg::AreaKind::View) {
        areaBox_ = viewBox_;
    } else if (kind_ == cg::AreaKind::Rectangle) {
        areaBox_ = rectangle_;
    } else if (const int i = routeChoice_->GetSelection();
               areaReason_.empty() && i != wxNOT_FOUND && std::size_t(i) < routes_.size()) {
        areaBox_ = cg::routeBounds(routes_[i].points, corridorNm_);
    }
    updateDetail();
    updateDefaultName();
}

// The band boxes shown ticked: the bands inside the range, Harbour only while it is allowed.
std::set<cg::Band> NewChartPanel::shownBands() const {
    std::set<cg::Band> shown = cg::bandsInRange(range_);
    if (!harbourOk_)
        shown.erase(cg::Band::Harbour);
    return shown;
}

// An AREA change (the callers schedule the estimate and refit): the Harbour rule, then the labels and the
// views.
void NewChartPanel::updateDetail() {
    const double lat = areaBox_ ? cg::centre(*areaBox_).lat : 0;
    harbourOk_ =
        !areaBox_ || cg::bboxTileCount(*areaBox_, 17) + cg::bboxTileCount(*areaBox_, 18) <= kHarbourMaxTiles;
    if (!harbourOk_ && range_ && range_->lo <= 17 && range_->hi >= 18) // not on a pick, not on a tap
        range_ =
            range_->lo <= 16 ? std::optional<cg::ZoomRange>(cg::ZoomRange{range_->lo, 16}) : std::nullopt;
    for (std::size_t i = 0; i < bandBoxes_.size(); ++i)
        bandScales_[i]->SetLabelText(bandScaleText(kBandOrder[i], lat));
    for (wxChoice* choice : {least_, most_})
        for (int z = 0; z <= cg::kMaxZoom; ++z)
            if (const wxString text = zoomEntryText(z, lat); choice->GetString(z) != text)
                choice->SetString(z, text);
    showRange();
    updateDefaultName();
}

// The views of range_: band boxes, note, drop-down selections, the levels line. The caller refits.
void NewChartPanel::showRange() {
    const double lat = areaBox_ ? cg::centre(*areaBox_).lat : 0;
    const std::set<cg::Band> shown = shownBands();
    for (std::size_t i = 0; i < bandBoxes_.size(); ++i) {
        bandBoxes_[i]->SetValue(shown.count(kBandOrder[i]) != 0);
        bandBoxes_[i]->Enable(kBandOrder[i] != cg::Band::Harbour || harbourOk_);
    }
    least_->SetSelection(range_ ? range_->lo : wxNOT_FOUND);
    most_->SetSelection(range_ ? range_->hi : wxNOT_FOUND);
    wxString note;
    const cg::SourceRecord* base = selectedSource();
    const std::optional<int> limit = base ? cg::zoomLimit(*base) : std::nullopt;
    if (limit && range_ && cg::limitZooms(zoomList(), limit) != zoomList()) // first
        note = sourceLimitText(cg::fromUtf8(cg::choiceLabel(base->name)), scaleText(*limit, lat));
    else if (range_ && cg::rangeZooms(*range_) != cg::zoomsForBands(shown))
        note = detailRangeNote(range_->lo, range_->hi, lat, cg::partialBands(range_));
    else if (!harbourOk_)
        note = uiText(UiText::HarbourTooLarge);
    detailNote_->SetLabelText(note);
    showWindow(detailNote_, !note.empty());
    updateWeather(); // the weather scale follows the range and the latitude
}

// A tap or a pick: the views, the estimate, the Download state.
void NewChartPanel::rangeChanged() {
    showRange();
    fitDialog();
    scheduleEstimate();
    updateDownloadState();
}

void NewChartPanel::bandToggled(std::size_t i) {
    const cg::Band b = kBandOrder[i];
    std::set<cg::Band> ticked = shownBands();
    if (bandBoxes_[i]->GetValue()) {
        ticked.insert(b);
        ticked = cg::makeContiguous(ticked);
    } else {
        ticked.erase(b);
        const bool below = std::any_of(ticked.begin(), ticked.end(), [b](cg::Band x) { return x < b; });
        const bool above = std::any_of(ticked.begin(), ticked.end(), [b](cg::Band x) { return x > b; });
        if (below && above)
            std::erase_if(ticked, [b](cg::Band x) { return x > b; });
    }
    range_ = cg::rangeForBands(ticked);
    rangeChanged();
}

void NewChartPanel::openPreferences() {
    if (SettingsDialog* open =
            SettingsDialog::shown()) { // one Settings window at a time: raise it
        open->Raise();
        return;
    }
    Enable(false);
    auto* dlg =
        new SettingsDialog(wxGetTopLevelParent(this), config_, look_, [this] { return jobs_.busy(); });
    dlg->onDone = [this](bool saved) {
        if (closing_)
            return;
        Enable(true);
        if (saved)
            reloadSources();
    };
    settingsDialog_ = dlg;
    dlg->Show();
    dlg->Raise();
}

// ---- Estimate ----

// The first failing input reason (area, source, zooms, then the refusal of a chosen source or
// layer: unreachable through the choice; kept for the 7.5 order); empty = the inputs are valid.
wxString NewChartPanel::inputReason() const {
    if (!areaReason_.empty())
        return areaReason_;
    if (!sourceReason_.empty())
        return sourceReason_;
    if (!range_)
        return uiText(UiText::ChooseDetail);
    if (!areaBox_) // only before the first setArea (the constructor's setBusy)
        return uiText(UiText::NoViewport);
    const cg::SourceRecord* base = selectedSource();
    if (!base)
        return uiText(UiText::NoUsableSource);
    if (const cg::SourceRefusal r = cg::sourceRefusal(*base); r.kind != cg::SourceRefusal::Kind::None)
        return sourceRefusalText(r);
    if (const cg::SourceRecord* layer = overlayRecord())
        if (const cg::SourceRefusal r = cg::sourceRefusal(*layer); r.kind != cg::SourceRefusal::Kind::None)
            return sourceRefusalText(r);
    return wxString();
}

const cg::SourceRecord* NewChartPanel::overlayRecord() const {
    return seamarks_->IsEnabled() && seamarks_->GetValue() ? findSource(kSeamarkGuid) : nullptr;
}

const cg::SourceRecord* NewChartPanel::weatherRecord() const {
    const int i = weatherChoice_->GetSelection();
    if (!weather_->GetValue() || i == wxNOT_FOUND || std::size_t(i) >= weatherGuids_.size())
        return nullptr;
    return findSource(weatherGuids_[i]);
}

std::vector<int> NewChartPanel::zoomList() const {
    return range_ ? cg::rangeZooms(*range_) : std::vector<int>{};
}

cg::JobSpec NewChartPanel::jobSpec() const {
    cg::JobSpec spec;
    const cg::SourceRecord* base = selectedSource();
    const cg::SourceRecord* layer = overlayRecord();
    if (base)
        spec.sourceGuid = base->guid;
    if (layer)
        spec.overlayGuid = layer->guid;
    spec.areaKind = kind_;
    if (areaBox_)
        spec.bbox = *areaBox_;
    spec.corridorNm = corridorNm_;
    // the sources' highest zooms : the estimate, job.ini and the record follow
    spec.zooms = cg::jobZooms(zoomList(), base ? cg::zoomLimit(*base) : std::nullopt,
                              layer ? cg::zoomLimit(*layer) : std::nullopt);
    return spec;
}

// Called on every input change: the estimate of the old inputs is void at once, a valid page restarts the
// 400 ms timer.
void NewChartPanel::scheduleEstimate() {
    if (closing_)
        return;
    ++estimateGen_;
    estimateTimer_.Stop();
    estimateState_ = inputReason().empty() ? EstimateState::Waiting : EstimateState::None;
    if (estimateState_ == EstimateState::Waiting)
        estimateTimer_.StartOnce(kEstimateDebounceMs);
    showEstimate();
}

void NewChartPanel::estimateFailed(const wxString& text) {
    estimateState_ = EstimateState::Failed;
    estimateFailure_ = text;
    showEstimate();
    updateDownloadState();
}

void NewChartPanel::startEstimate() {
    if (closing_ || estimateState_ != EstimateState::Waiting)
        return;
    if (estimateRunner_ && estimateRunner_->running()) {
        estimateRunner_->terminate(); // its result is discarded; onExit starts again with the current inputs
        estimatePending_ = true;
        return;
    }
    const cg::Settings settings = config_.loadSettings();
    std::string why;
    const auto cli = resolveCli(settings, &why);
    if (!cli) {
        cg::logLine(cg::fromUtf8(why));
        estimateFailed(cliNotFoundText());
        return;
    }
    if (!probeCli(*cli)) {
        estimateFailed(cliTooOldText());
        return;
    }
    cg::JobSpec spec = jobSpec();
    if (kind_ == cg::AreaKind::Route) { // the corridor needs the route as a GPX file
        const wxString dir = cg::fromUtf8(config_.jobsDir());
        const wxString gpx = joinPath(dir, "estimate-route.gpx");
        const int i = routeChoice_->GetSelection();
        // unreachable otherwise: Route mode with a valid area has a listed route (checkRouteArea)
        const std::string text = i != wxNOT_FOUND && std::size_t(i) < routes_.size()
                                     ? cg::routeGpx(cg::toUtf8(routes_[i].name), routes_[i].points)
                                     : std::string();
        if (!wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL) || !writeText(gpx, text)) {
            estimateFailed(cannotWriteText(gpx));
            return;
        }
        spec.routeGpxPath = cg::toUtf8(gpx);
    }
    const std::vector<std::string> argv =
        cg::cliCommand(*cli, cg::estimateArguments(spec, config_.cliSettings(settings)), false);
    if (!estimateRunner_)
        estimateRunner_ = std::make_unique<CliRunner>();
    estimateRecords_.clear();
    estimateError_.clear();
    runGen_ = estimateGen_;
    CliRunner::Callbacks cb;
    cb.onRecord = [this](const cg::StdoutRecord& r) {
        if (const auto* e = std::get_if<cg::EstimateRecord>(&r))
            estimateRecords_.push_back(*e);
    };
    cb.onStderr = [this](const cg::StderrLine& l) {
        if (const auto* m = std::get_if<cg::MessageLine>(&l); m && m->kind == cg::MessageLine::Kind::Error)
            estimateError_ = m->text;
    };
    cb.onExit = [this](int code, bool) {
        if (!closing_)
            estimateFinished(code);
    };
    std::string error;
    estimateState_ = EstimateState::Running;
    if (!estimateRunner_->start(argv, std::move(cb), &error)) {
        cg::logLine(cg::fromUtf8(error));
        estimateFailed(cliNotFoundText());
        return;
    }
    showEstimate();
    updateDownloadState();
}

void NewChartPanel::estimateFinished(int code) {
    if (estimatePending_) { // terminated by a later change: discard, run again with the current inputs
        estimatePending_ = false;
        if (!estimateTimer_.IsRunning()) // else the timer of the newer change starts it
            startEstimate();
        return;
    }
    if (runGen_ != estimateGen_) // inputs changed after the start: the timer is waiting; discard
        return;
    if (code == 0) {
        totals_ = cg::sumEstimates(estimateRecords_);
        estimateState_ = EstimateState::Ok;
        showEstimate();
        updateDownloadState();
    } else if (code == 3) {
        estimateFailed(refusalText(estimateError_));
    } else if (estimateError_.empty()) {
        estimateFailed(wxString::Format(_("Cannot work out the size (error %d)."), code));
    } else {
        estimateFailed(
            wxString::Format(_("Cannot work out the size: %s"), cg::fromUtf8(estimateError_).c_str()));
    }
}

// "~1 950 tiles, ~38 MB to download, ~70 MB on disk, 9.1 GB free" (+ " (size assumed)"); empty unless the
// estimate of the current inputs is in.
void NewChartPanel::showEstimate() {
    wxString text;
    if (estimateState_ == EstimateState::Waiting || estimateState_ == EstimateState::Running) {
        text = estimatingText();
    } else if (estimateState_ == EstimateState::Ok) {
        const auto free = freeBytes(outputFolder());
        const wxString freeText = free ? wxString::Format(_("%s free"), formatBytes(std::int64_t(*free)))
                                       : wxString(_("free unknown"));
        text = wxString::Format(_("About %s tiles, %s to download, %s on disk (%s)"),
                                formatCount(totals_.tiles), formatBytes(totals_.bytesToDownload),
                                formatBytes(totals_.bytesOnDisk), freeText);
        if (totals_.assumed)
            text += _(" (rough size)");
    }
    if (estimate_->GetLabel() != text) {
        estimate_->SetLabelText(text);
        estimate_->GetParent()->Layout();
    }
    updateCostLines();
}

// cost lines: the main page line and the Advanced line; only an exit-0 estimate of the current
// inputs gives a cost, and not when the next level is above the base source's limit.
void NewChartPanel::updateCostLines() {
    const double lat = areaBox_ ? cg::centre(*areaBox_).lat : 0;
    std::optional<cg::LevelCost> cost;
    if (range_ && estimateState_ == EstimateState::Ok) {
        cost = cg::levelCost(estimateRecords_, range_->hi);
        const cg::SourceRecord* base = selectedSource();
        if (const auto limit = base ? cg::zoomLimit(*base) : std::nullopt; limit && range_->hi + 1 > *limit)
            cost.reset();
    }
    bool refit = false;
    if (cost)
        nextLevel_->SetLabelText(nextLevelText(range_->hi, lat, cost->nextBytes));
    refit |= showWindow(nextLevel_, cost.has_value());
    if (range_)
        levels_->SetLabelText(levelsText(range_->lo, range_->hi, lat, cost));
    refit |= showWindow(levels_, range_.has_value());
    if (refit)
        fitDialog();
    else
        levels_->GetParent()->Layout();
}

// ---- Download ----

wxString NewChartPanel::outputFolder() const {
    return outputDir_.empty() ? cg::fromUtf8(config_.chartDir(config_.loadSettings())) : outputDir_;
}

// The Name field trimmed, with every apostrophe removed (the CLI refuses one in --name).
wxString NewChartPanel::chartName() const {
    wxString n = name_->GetValue();
    n.Replace("'", wxString());
    return n.Trim(true).Trim(false);
}

// defaultChartName(place, source) while the user has not typed a name; recomputed on every area / source
// change.
void NewChartPanel::updateDefaultName() {
    const cg::SourceRecord* base = selectedSource();
    const int route = routeChoice_->GetSelection();
    if (nameEdited_ || !base || !areaBox_)
        return;
    std::string place = cg::areaPlace(*areaBox_);
    if (kind_ == cg::AreaKind::Route && route != wxNOT_FOUND && std::size_t(route) < routes_.size())
        place = cg::toUtf8(routes_[route].name);
    name_->ChangeValue(cg::fromUtf8(cg::defaultChartName(place, base->name)));
}

cg::Preflight NewChartPanel::preflight(const cg::Settings& settings, cg::PreflightInput* in) const {
    const wxString chart = outputFolder(), cache = cg::fromUtf8(config_.cliCacheRoot(settings));
    constexpr std::uint64_t kUnknown =
        std::numeric_limits<std::uint64_t>::max(); // the folder's check is skipped
    in->totals = totals_;
    in->freeChartBytes = freeBytes(chart).value_or(kUnknown);
    in->freeCacheBytes = freeBytes(cache).value_or(kUnknown);
    in->sameFileSystem = sameFileSystem(chart, cache);
    in->settings = &settings;
    return cg::checkPreflight(*in);
}

// The enable order of; the first failing reason is the status text. Nothing failing -> empty and
// Download enabled (unless a job runs: the button is then replaced by "A download is running").
void NewChartPanel::updateDownloadState() {
    wxString reason = inputReason();
    if (reason.empty() && estimateState_ != EstimateState::Ok)
        reason = estimateState_ == EstimateState::Failed ? estimateFailure_ : estimatingText();
    if (reason.empty() && chartName().empty())
        reason = enterNameText();
    if (reason.empty()) {
        const cg::Settings settings = config_.loadSettings();
        cg::PreflightInput in;
        const cg::Preflight p = preflight(settings, &in);
        reason = preflightText(p, in, outputFolder(), cg::fromUtf8(config_.cliCacheRoot(settings)));
    }
    bool changed = showWindow(preferences_, sourceFailure_ && !reason.empty() && reason == sourceReason_);
    if (statusText_ != reason) {
        showStatus(reason);
        changed = true;
    }
    if (folder_->GetLabel() != outputFolder()) {
        folder_->SetLabelText(outputFolder());
        folder_->SetToolTip(outputFolder());
    }
    if (changed)
        fitDialog();
    download_->Enable(reason.empty() && !jobs_.busy());
}

void NewChartPanel::chooseOutputFolder() {
    wxString dir = outputFolder();
    if (PlatformDirSelectorDialog(this, &dir, _("Choose the chart folder"), outputFolder()) != wxID_OK ||
        closing_)
        return;
    outputDir_ = dir; // for this chart only, never saved
    showEstimate();
    updateDownloadState();
}

void NewChartPanel::chartFiles(wxString* base, wxString* overlay, wxString* weather) const {
    const wxString folder = outputFolder();
    const std::string name = cg::toUtf8(chartName());
    *base = joinPath(folder, cg::fromUtf8(cg::chartFileName(name)));
    *overlay = overlayRecord() ? joinPath(folder, cg::fromUtf8(cg::chartFileName(cg::seamarksName(name))))
                               : wxString();
    *weather = weatherRecord() ? joinPath(folder, cg::fromUtf8(cg::chartFileName(cg::weatherName(name))))
                               : wxString();
}

void NewChartPanel::startDownload() {
    if (closing_)
        return;
    if (question_ &&
        question_->IsShown()) { // a second press while a question is open (an answered one is hidden)
        question_->Raise();
        return;
    }
    if (!download_->IsEnabled()) // unreachable otherwise: bound to the Download button only
        return;
    continueDownload(1);
}

// steps 1-3. Each question is modeless: the page waits, OpenCPN keeps running.
void NewChartPanel::continueDownload(int step) {
    const cg::Settings settings = config_.loadSettings();
    cg::PreflightInput in;
    if (step <= 1 && preflight(settings, &in) == cg::Preflight::OverBudget) // 1
        return askDownload(Question::Buttons::YesNo,
                           overBudgetQuestion(totals_.bytesToDownload, settings.dataBudgetMb), wxString(), 2);
    if (step <= 2 && !config_.disclaimerShown()) // 2
        return askDownload(Question::Buttons::OkCancel, disclaimerText(), dontShowAgainText(), 3);
    if (!selectedSource() ||
        !areaBox_) // unreachable: Download is enabled only with valid inputs (inputReason)
        return;
    wxString outFile, overlayFile, weatherFile; // 3: files
    chartFiles(&outFile, &overlayFile, &weatherFile);
    const auto exists = [&](const std::string& chartName, bool wanted) {
        return wanted && wxFileExists(joinPath(outputFolder(), cg::fromUtf8(cg::chartFileName(chartName))));
    };
    const std::string chartTitle = cg::toUtf8(chartName());
    if (step <= 3 &&
        (exists(chartTitle, true) || exists(cg::seamarksName(chartTitle), !overlayFile.empty()) ||
         exists(cg::weatherName(chartTitle), !weatherFile.empty())))
        return askDownload(Question::Buttons::YesNo, replaceQuestion(), wxString(), 4);
    beginDownload(step == 4);
}

void NewChartPanel::askDownload(Question::Buttons buttons, const wxString& text, const wxString& checkBox,
                                int next) {
    Enable(false);
    question_ = Question::ask(wxGetTopLevelParent(this), look_, _("Chart Grabber"), text, buttons, checkBox,
                              [this, next](bool yes, bool ticked) {
                                  if (closing_)
                                      return;
                                  Enable(true);
                                  if (!yes)
                                      return;
                                  if (ticked) // only the notice has a check box
                                      config_.setDisclaimerShown();
                                  updateDownloadState();
                                  if (download_->IsEnabled()) // a job may have started from My charts
                                      continueDownload(next);
                              });
}

// only after a user pick in a drop-down; one modeless question, the page disabled meanwhile.
void NewChartPanel::warnAfterPick(bool pickedMost, int picked) {
    if (!areaBox_ || !range_ || !config_.loadSettings().warnDetailLevels)
        return; // range_ was just set by the caller
    const cg::DetailAdvice advice = cg::detailAdvice(*areaBox_, *range_, pickedMost);
    if (advice.kind == cg::DetailWarning::None)
        return;
    const double lat = cg::centre(*areaBox_).lat;
    const wxString text = detailWarningText(advice, *range_, lat, cg::meanTileBytes(estimateRecords_));
    const wxString use = useScaleText(
        advice.kind == cg::DetailWarning::TooMuch ? advice.suggested.hi : advice.suggested.lo, lat);
    Enable(false);
    detailQuestion_ = Question::askCustom(wxGetTopLevelParent(this), look_, _("Chart Grabber"), text, use,
                                          keepScaleText(picked, lat), dontShowAgainText(),
                                          [this, pickedMost, picked, advice](bool yes, bool ticked) {
                                              if (closing_)
                                                  return;
                                              Enable(true);
                                              if (ticked)
                                                  config_.setWarnDetailLevels(false);
                                              wxChoice* box = pickedMost ? most_ : least_;
                                              if (!yes || box->GetSelection() != picked)
                                                  return;
                                              range_ = advice.suggested;
                                              rangeChanged();
                                          });
}

void NewChartPanel::beginDownload(bool overwrite) {
    const cg::Settings settings = config_.loadSettings();
    const cg::SourceRecord* base = selectedSource();
    const cg::SourceRecord* layer = overlayRecord();
    const cg::SourceRecord* weather = weatherRecord(); // step 7a
    const wxString folder = outputFolder();
    const std::string name = cg::toUtf8(chartName()); // 4
    wxString outFile, overlayFile, weatherFile;
    chartFiles(&outFile, &overlayFile, &weatherFile);
    const wxString jobsRoot = cg::fromUtf8(config_.jobsDir());
    std::vector<wxString> jobDirs; // every directory made so far: removed again on a write failure
    auto cannotWrite = [&](const wxString& path) {
        for (const wxString& dir : jobDirs)
            wxFileName::Rmdir(dir, wxPATH_RMDIR_RECURSIVE);
        showStatus(cannotWriteText(path));
        fitDialog();
    };
    // 5: a job directory with the next free id; 6: its route.gpx and job.ini. False: cannotWrite ran.
    auto makeJob = [&](cg::JobSpec spec, wxString* id) {
        const wxString stamp = wxDateTime::Now().Format("%Y%m%d-%H%M%S");
        *id = stamp;
        for (int n = 2; wxDirExists(joinPath(jobsRoot, *id)); ++n)
            *id = wxString::Format("%s-%d", stamp, n);
        const wxString jobDir = joinPath(jobsRoot, *id);
        jobDirs.push_back(jobDir);
        if (!wxFileName::Mkdir(jobDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
            cannotWrite(jobDir);
            return false;
        }
        if (kind_ == cg::AreaKind::Route) {
            const int i =
                routeChoice_->GetSelection(); // valid: the area box of a Route page needs a listed route
            const wxString gpx = joinPath(jobDir, "route.gpx");
            if (!writeText(gpx, cg::routeGpx(cg::toUtf8(routes_[i].name), routes_[i].points))) {
                cannotWrite(gpx);
                return false;
            }
            spec.routeGpxPath = cg::toUtf8(gpx);
        }
        spec.threads = settings.slowConnection ? 1 : threads_->GetValue();
        const wxString iniPath = joinPath(jobDir, "job.ini");
        if (!writeText(iniPath, cg::jobIni(spec))) {
            cannotWrite(iniPath);
            return false;
        }
        return true;
    };
    wxString id, weatherId;
    cg::JobSpec spec = jobSpec();
    spec.outFile = cg::toUtf8(outFile);
    spec.overlayOutFile = cg::toUtf8(overlayFile);
    spec.name = name;
    if (!makeJob(spec, &id))
        return;
    if (!wxFileName::Mkdir(folder, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
        cannotWrite(folder);
        return;
    }
    cg::JobSpec weatherSpec; // 7a: an overlay-only job, no Source / Name / OverlayOut
    if (weather) {
        weatherSpec = jobSpec();
        weatherSpec.sourceGuid.clear();
        weatherSpec.overlayGuid = weather->guid;
        weatherSpec.zooms = cg::limitZooms(zoomList(), cg::zoomLimit(*weather));
        weatherSpec.overlayOutFile = cg::toUtf8(weatherFile); // jobIni: Out= of an overlay-only job
        if (!makeJob(weatherSpec, &weatherId))
            return;
    }
    // 7
    const std::string created = cg::toUtf8(wxDateTime::Now().Format("%Y-%m-%dT%H:%M:%S"));
    cg::ChartRecord record;
    record.id = cg::toUtf8(id);
    record.name = name;
    record.sourceGuid = spec.sourceGuid;
    record.overlayGuid = spec.overlayGuid;
    record.sourceName = base->name;
    record.bbox = *areaBox_;
    record.zooms = spec.zooms;
    record.created = created;
    record.files.push_back(spec.outFile);
    if (layer)
        record.files.push_back(spec.overlayOutFile);
    record.state = cg::ChartState::Running;
    record.weather = base->isWeather(); // the credit of every base
    record.credit = base->license;
    cg::ChartRecord followUp; // 7a: saved Interrupted, queued as Running
    if (weather) {
        followUp.id = cg::toUtf8(weatherId);
        followUp.name = cg::weatherName(name);
        followUp.overlayGuid = weather->guid;
        followUp.sourceName = weather->name;
        followUp.bbox = *areaBox_;
        followUp.zooms = weatherSpec.zooms;
        followUp.created = created;
        followUp.files.push_back(weatherSpec.overlayOutFile);
        followUp.state = cg::ChartState::Interrupted;
        followUp.weather = true;
        followUp.credit = weather->license;
        config_.saveChart(followUp);
    }
    // 8
    ConfigStore::LastChoice last;
    last.sourceGuid = base->guid;
    last.seamarks = seamarks_->GetValue();
    last.weather = weatherOn_;
    last.weatherGuid = weather ? weather->guid : weatherPick_;
    last.bands = shownBands();
    last.advancedZooms = range_ ? cg::formatZoomList(cg::rangeZooms(*range_)) : std::string();
    last.areaKind = kind_;
    last.rectangle = rectangle_;
    const int route = routeChoice_->GetSelection();
    last.routeGuid =
        kind_ == cg::AreaKind::Route && route != wxNOT_FOUND && std::size_t(route) < routes_.size()
            ? cg::toUtf8(routes_[route].guid)
            : cg::toUtf8(routeGuid_);
    config_.saveLastChoice(last);
    cg::Settings saved = settings;
    saved.corridorNm = corridorNm_;
    saved.threads = threads_->GetValue();
    config_.saveSettings(saved);
    jobs_.start(record, cg::RunKind::Download, overwrite);
    if (weather &&
        jobs_.busy()) { // not busy: the first start failed at once, the weather chart stays Interrupted
        followUp.state = cg::ChartState::Running;
        jobs_.queueAfter(followUp);
    }
    if (onStarted)
        onStarted();
}
