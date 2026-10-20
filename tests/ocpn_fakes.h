// SPDX-License-Identifier: GPL-3.0-or-later
// Values the shared OpenCPN fakes (ocpn_fakes.cpp) return; a test sets them and resets them in its SetUp.
#pragma once
#include <wx/arrstr.h>
#include <wx/colour.h>
#include <wx/string.h>

#include <functional>
#include <map>
#include <utility>
#include <vector>

class wxWindow;

namespace ocpn_fake {
extern wxString privateData; // GetpPrivateApplicationDataLocation
extern bool privateDataNull; // true: GetpPrivateApplicationDataLocation returns nullptr (a test resets it)
extern bool touch;           // IsTouchInterface_PlugIn
extern double scale;         // GetOCPNGUIToolScaleFactor_PlugIn
// DimeWindow(w) counts, remembers w and marks w and every descendant with background (1,2,3) and foreground
// (4,5,6) so a test can see a reset; GetGlobalColor counts its calls (a test resets them) and answers from
// globalColors below (default: no colour).
extern int globalColorCalls, dimeCalls;
// GetGlobalColor(name) answers globalColors[name] (true) when a test set it; default empty = no colour.
// globalColorAsked lists the names asked, in call order.
extern std::map<wxString, wxColour> globalColors;
extern std::vector<wxString> globalColorAsked;
extern wxWindow* dimeLast;
} // namespace ocpn_fake

namespace ocpn_fake {
extern wxWindow* canvasWindow; // GetOCPNCanvasWindow; null = no canvas (the strip does not place itself)
extern int chartbarHeight;     // GetChartbarHeight; 0 = chart bar hidden (a test resets it)
extern double contentScale;    // OCPN_GetDisplayContentScaleFactor
struct Jump {
    int calls = 0;
    double lat = 0, lon = 0, scale = 0; // the last JumpToPosition
};
extern Jump jump;
// GetChartDBDirArrayString returns chartDirs; UpdateChartDBInplace(dirs, force, progress) appends to
// updates and returns true; onUpdate (if set) is called first.
struct DbUpdate {
    wxArrayString dirs;
    bool force = false, progress = false;
};
extern wxArrayString chartDirs;
extern std::vector<DbUpdate> updates;
extern std::function<void()> onUpdate;
// RemoveChartFromDBInPlace(path) in call order; returns 1. The plugin must never call it (hangs 5.14+).
extern std::vector<wxString> removed;

// Canvases: GetCanvasCount = canvases.size, GetCanvasByIndex(i) = canvases[i] (nullptr outside);
// PluginGetFocusCanvas = focus; GetCanvasIndexUnderMouse = underMouse; RequestRefresh(w) appends to
// refreshed. GetCanvasPixLL / GetCanvasLLPix are linear: 100 + 1 pixel = view_scale_ppm per degree, canvas
// centre (100,100).
extern std::vector<wxWindow*> canvases;
extern wxWindow* focus;
extern int underMouse;
extern std::vector<wxWindow*> refreshed;

// Routes (GetRouteGUIDArray / GetRoute_Plugin / GetSelectedRouteGUID_Plugin / GetActiveRouteGUID).
// GetRoute_Plugin `new`s every PlugIn_Waypoint as OpenCPN 5.10.2 does (the caller owns them);
// `waypointsCreated` and `waypointsDeleted` count the constructions and destructions of PlugIn_Waypoint
// (equal = none leaked).
struct Route {
    wxString guid, name;
    std::vector<std::pair<double, double>> points; // lat, lon
    int linksPerPoint =
        0; // hyperlinks cloned onto every waypoint, `new`ed as 5.10.2 PlugInFromRoutePoint does
};
extern std::vector<Route> routes;
extern wxString selectedRoute, activeRoute;
extern int waypointsCreated, waypointsDeleted;
// PlatformDirSelectorDialog(parent, &spec, title, initDir): sets spec = dirSelected, keeps initDir in
// dirInit, returns dirResult (a test sets wxID_OK or wxID_CANCEL).
extern int dirResult;
extern wxString dirSelected, dirInit, dirTitle;
// PlatformFileSelectorDialog(parent, &spec, title, initDir, suggestedName, wildcard): the same for files.
extern int fileResult;
extern wxString fileSelected, fileInit, fileTitle, fileWildcard;
} // namespace ocpn_fake

// The plugin-level API of ChartGrabberPi. Toolbar calls are appended to `toolbar` in call order.
class wxFileConfig;
namespace ocpn_fake {
struct ToolCall {
    enum class Kind { Insert, Bitmaps, State, Remove } kind;
    int id = 0;
    wxString normal, rollover, toggled; // Insert, Bitmaps
    int itemKind = 0;                   // Insert: the wxItemKind
    bool state = false;                 // State
};
extern std::vector<ToolCall> toolbar;
extern wxString dataDir; // GetPluginDataDir(name) (name is recorded in dataDirAsked)
extern wxString dataDirAsked;
extern wxString svgAsked; // GetBitmapFromSVGFile(file, w, h): the last file and size
extern std::pair<unsigned, unsigned> svgSize;
extern wxFileConfig* config; // GetOCPNConfigObject
} // namespace ocpn_fake
