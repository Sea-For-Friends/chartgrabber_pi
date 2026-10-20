// SPDX-License-Identifier: GPL-3.0-or-later
// The ONE definition per test binary of the OpenCPN API functions the code under test calls (see the rule in
// wx_env.h). cg_tests and cg_gui_tests both compile this file; a test file never defines these itself.
#include "ocpn_fakes.h"

#include <wx/arrstr.h>
#include <wx/colour.h>
#include <wx/dynarray.h>
#include <wx/string.h>
#include <wx/window.h>

#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include <ocpn_plugin.h>

namespace ocpn_fake {
wxString privateData;
bool privateDataNull = false;
bool touch = false;
double scale = 1.0;
} // namespace ocpn_fake

namespace ocpn_fake {
int globalColorCalls = 0;
int dimeCalls = 0;
wxWindow* dimeLast = nullptr;
} // namespace ocpn_fake

namespace ocpn_fake {
std::map<wxString, wxColour> globalColors;
std::vector<wxString> globalColorAsked;
} // namespace ocpn_fake

extern "C" bool GetGlobalColor(wxString name, wxColour* colour) {
    ++ocpn_fake::globalColorCalls;
    ocpn_fake::globalColorAsked.push_back(name);
    const auto it = ocpn_fake::globalColors.find(name);
    if (it == ocpn_fake::globalColors.end())
        return false;
    *colour = it->second;
    return true;
}
static void markColours(wxWindow* window) {
    window->SetBackgroundColour(wxColour(1, 2, 3));
    window->SetForegroundColour(wxColour(4, 5, 6));
    for (wxWindow* child : window->GetChildren())
        markColours(child);
}
extern "C" void DimeWindow(wxWindow* window) {
    ++ocpn_fake::dimeCalls;
    ocpn_fake::dimeLast = window;
    markColours(window);
}
extern "C" wxString* GetpPrivateApplicationDataLocation() {
    return ocpn_fake::privateDataNull ? nullptr : &ocpn_fake::privateData;
}
double GetOCPNGUIToolScaleFactor_PlugIn() {
    return ocpn_fake::scale;
}
bool IsTouchInterface_PlugIn() {
    return ocpn_fake::touch;
}

namespace ocpn_fake {
wxWindow* canvasWindow = nullptr;
int chartbarHeight = 0;
double contentScale = 1.0;
Jump jump;
wxArrayString chartDirs;
std::vector<DbUpdate> updates;
std::function<void()> onUpdate;
std::vector<wxString> removed;
std::vector<wxWindow*> canvases;
wxWindow* focus = nullptr;
int underMouse = 0;
std::vector<wxWindow*> refreshed;
std::vector<Route> routes;
wxString selectedRoute, activeRoute;
int waypointsCreated = 0, waypointsDeleted = 0;
int dirResult = 0;
wxString dirSelected, dirInit, dirTitle;
int fileResult = 0;
wxString fileSelected, fileInit, fileTitle, fileWildcard;
} // namespace ocpn_fake

int PlatformDirSelectorDialog(wxWindow*, wxString* file_spec, wxString title, wxString initDir) {
    ocpn_fake::dirTitle = title;
    ocpn_fake::dirInit = initDir;
    *file_spec = ocpn_fake::dirSelected;
    return ocpn_fake::dirResult;
}

int PlatformFileSelectorDialog(wxWindow*, wxString* file_spec, wxString title, wxString initDir, wxString,
                               wxString wildcard) {
    ocpn_fake::fileTitle = title;
    ocpn_fake::fileInit = initDir;
    ocpn_fake::fileWildcard = wildcard;
    *file_spec = ocpn_fake::fileSelected;
    return ocpn_fake::fileResult;
}

// The strip places itself over the canvas; with none, place returns early.
wxWindow* GetOCPNCanvasWindow() {
    return ocpn_fake::canvasWindow;
}
int GetChartbarHeight() {
    return ocpn_fake::chartbarHeight;
}
wxWindow* PluginGetFocusCanvas() {
    return ocpn_fake::focus;
}
double OCPN_GetDisplayContentScaleFactor() {
    return ocpn_fake::contentScale;
}
extern "C" void JumpToPosition(double lat, double lon, double scale) {
    ocpn_fake::jump = {ocpn_fake::jump.calls + 1, lat, lon, scale};
}
bool UpdateChartDBInplace(wxArrayString dir_array, bool b_force_update, bool b_ProgressDialog) {
    if (ocpn_fake::onUpdate)
        ocpn_fake::onUpdate();
    ocpn_fake::updates.push_back({dir_array, b_force_update, b_ProgressDialog});
    return true;
}
wxArrayString GetChartDBDirArrayString() {
    return ocpn_fake::chartDirs;
}
extern "C" int RemoveChartFromDBInPlace(wxString& path) {
    ocpn_fake::removed.push_back(path);
    return 1;
}

// AreaTool calls these.
extern "C" void RequestRefresh(wxWindow* w) {
    ocpn_fake::refreshed.push_back(w);
}
extern "C" void GetCanvasPixLL(PlugIn_ViewPort* vp, wxPoint* pp, double lat, double lon) {
    *pp = wxPoint(int(std::lround(100 + (lon - vp->clon) * vp->view_scale_ppm)),
                  int(std::lround(100 - (lat - vp->clat) * vp->view_scale_ppm)));
}
extern "C" void GetCanvasLLPix(PlugIn_ViewPort* vp, wxPoint p, double* plat, double* plon) {
    *plat = vp->clat - (p.y - 100) / vp->view_scale_ppm;
    *plon = vp->clon + (p.x - 100) / vp->view_scale_ppm;
}
int GetCanvasIndexUnderMouse() {
    return ocpn_fake::underMouse;
}
wxWindow* GetCanvasByIndex(int i) {
    return i >= 0 && std::size_t(i) < ocpn_fake::canvases.size() ? ocpn_fake::canvases[i] : nullptr;
}
int GetCanvasCount() {
    return int(ocpn_fake::canvases.size());
}

// Waypoints and routes as OpenCPN 5.10.2 defines them (gui/src/pluginmanager.cpp:240, 3098-3125).
#include <wx/listimpl.cpp>
WX_DEFINE_LIST(Plugin_HyperlinkList)
WX_DEFINE_LIST(Plugin_WaypointList)

PlugIn_Waypoint::PlugIn_Waypoint() : m_lat(0), m_lon(0), m_IsVisible(true), m_HyperlinkList(nullptr) {
    ++ocpn_fake::waypointsCreated;
}
PlugIn_Waypoint::~PlugIn_Waypoint() {
    ++ocpn_fake::waypointsDeleted;
}
PlugIn_Route::PlugIn_Route() : pWaypointList(new Plugin_WaypointList) {}
PlugIn_Route::~PlugIn_Route() {
    pWaypointList->DeleteContents(false); // do not delete Waypoints, as 5.10.2
    delete pWaypointList;
}

wxArrayString GetRouteGUIDArray(void) {
    wxArrayString a;
    for (const auto& r : ocpn_fake::routes)
        a.Add(r.guid);
    return a;
}
wxString GetSelectedRouteGUID_Plugin() {
    return ocpn_fake::selectedRoute;
}
wxString GetActiveRouteGUID(void) {
    return ocpn_fake::activeRoute;
}
std::unique_ptr<PlugIn_Route> GetRoute_Plugin(const wxString& guid) {
    for (const auto& r : ocpn_fake::routes) {
        if (r.guid != guid)
            continue;
        auto route = std::make_unique<PlugIn_Route>();
        route->m_GUID = r.guid;
        route->m_NameString = r.name;
        for (const auto& [lat, lon] : r.points) {
            auto* w = new PlugIn_Waypoint; // the caller owns it
            w->m_lat = lat;
            w->m_lon = lon;
            if (r.linksPerPoint > 0) {
                w->m_HyperlinkList = new Plugin_HyperlinkList;
                for (int i = 0; i < r.linksPerPoint; ++i)
                    w->m_HyperlinkList->Append(new Plugin_Hyperlink);
            }
            route->pWaypointList->Append(w);
        }
        return route;
    }
    return nullptr;
}

namespace ocpn_fake {
std::vector<ToolCall> toolbar;
wxString dataDir;
wxString dataDirAsked;
wxString svgAsked;
std::pair<unsigned, unsigned> svgSize;
wxFileConfig* config = nullptr;
} // namespace ocpn_fake

wxString GetPluginDataDir(const char* name) {
    ocpn_fake::dataDirAsked = name;
    return ocpn_fake::dataDir;
}
extern "C" wxFileConfig* GetOCPNConfigObject(void) {
    return ocpn_fake::config;
}
extern "C" int InsertPlugInToolSVG(wxString, wxString normal, wxString rollover, wxString toggled,
                                   wxItemKind kind, wxString, wxString, wxObject*, int, int,
                                   opencpn_plugin*) {
    using ocpn_fake::ToolCall;
    ocpn_fake::toolbar.push_back({ToolCall::Kind::Insert, 7, normal, rollover, toggled, int(kind), false});
    return 7;
}
extern "C" void SetToolbarToolBitmapsSVG(int id, wxString normal, wxString rollover, wxString toggled) {
    using ocpn_fake::ToolCall;
    ocpn_fake::toolbar.push_back({ToolCall::Kind::Bitmaps, id, normal, rollover, toggled, 0, false});
}
extern "C" void SetToolbarItemState(int id, bool toggle) {
    using ocpn_fake::ToolCall;
    ocpn_fake::toolbar.push_back({ToolCall::Kind::State, id, {}, {}, {}, 0, toggle});
}
extern "C" void RemovePlugInTool(int id) {
    using ocpn_fake::ToolCall;
    ocpn_fake::toolbar.push_back({ToolCall::Kind::Remove, id, {}, {}, {}, 0, false});
}
extern "C" int AddCanvasContextMenuItem(wxMenuItem*, opencpn_plugin*) {
    static int next = 100;
    return next++;
}
extern "C" void RemoveCanvasContextMenuItem(int) {}
wxBitmap GetBitmapFromSVGFile(wxString file, unsigned int w, unsigned int h) {
    ocpn_fake::svgAsked = file;
    ocpn_fake::svgSize = {w, h};
    return wxBitmap();
}

// The host-side bodies of the plugin base-class chain (OpenCPN defines them in its own sources, not in
// ocpn_plugin.h): empty stand-ins so that ChartGrabberPi can be built and driven in a test.
#ifdef CG_WX_GUI_TESTS // only cg_gui_tests builds ChartGrabberPi
opencpn_plugin::~opencpn_plugin() = default;
int opencpn_plugin::Init() {
    return 0;
}
bool opencpn_plugin::DeInit() {
    return true;
}
int opencpn_plugin::GetAPIVersionMajor() {
    return 0;
}
int opencpn_plugin::GetAPIVersionMinor() {
    return 0;
}
int opencpn_plugin::GetPlugInVersionMajor() {
    return 0;
}
int opencpn_plugin::GetPlugInVersionMinor() {
    return 0;
}
wxBitmap* opencpn_plugin::GetPlugInBitmap() {
    return nullptr;
}
wxString opencpn_plugin::GetCommonName() {
    return wxString();
}
wxString opencpn_plugin::GetShortDescription() {
    return wxString();
}
wxString opencpn_plugin::GetLongDescription() {
    return wxString();
}
int opencpn_plugin::GetToolbarToolCount() {
    return 0;
}
void opencpn_plugin::ShowPreferencesDialog(wxWindow*) {}
void opencpn_plugin::SetColorScheme(PI_ColorScheme) {}
void opencpn_plugin::OnToolbarToolCallback(int) {}
void opencpn_plugin::OnContextMenuItemCallback(int) {}
bool opencpn_plugin_112::MouseEventHook(wxMouseEvent&) {
    return false;
}
opencpn_plugin_117::opencpn_plugin_117(void* pmgr) : opencpn_plugin_116(pmgr) {}
opencpn_plugin_116::opencpn_plugin_116(void* pmgr) : opencpn_plugin_115(pmgr) {}
opencpn_plugin_115::opencpn_plugin_115(void* pmgr) : opencpn_plugin_114(pmgr) {}
opencpn_plugin_114::opencpn_plugin_114(void* pmgr) : opencpn_plugin_113(pmgr) {}
opencpn_plugin_113::opencpn_plugin_113(void* pmgr) : opencpn_plugin_112(pmgr) {}
opencpn_plugin_112::opencpn_plugin_112(void* pmgr) : opencpn_plugin_111(pmgr) {}
opencpn_plugin_111::opencpn_plugin_111(void* pmgr) : opencpn_plugin_110(pmgr) {}
opencpn_plugin_110::opencpn_plugin_110(void* pmgr) : opencpn_plugin_19(pmgr) {}
opencpn_plugin_19::opencpn_plugin_19(void* pmgr) : opencpn_plugin_18(pmgr) {}
opencpn_plugin_18::opencpn_plugin_18(void* pmgr) : opencpn_plugin(pmgr) {}
void opencpn_plugin::SetDefaults() {}
int opencpn_plugin::GetToolboxPanelCount() {
    return 0;
}
void opencpn_plugin::SetupToolboxPanel(int, wxNotebook*) {}
void opencpn_plugin::OnCloseToolboxPanel(int, int) {}
bool opencpn_plugin::RenderOverlay(wxMemoryDC*, PlugIn_ViewPort*) {
    return false;
}
void opencpn_plugin::SetCursorLatLon(double, double) {}
void opencpn_plugin::SetCurrentViewPort(PlugIn_ViewPort&) {}
void opencpn_plugin::SetPositionFix(PlugIn_Position_Fix&) {}
void opencpn_plugin::SetNMEASentence(wxString&) {}
void opencpn_plugin::SetAISSentence(wxString&) {}
void opencpn_plugin::ProcessParentResize(int, int) {}
void opencpn_plugin::UpdateAuiStatus() {}
wxArrayString opencpn_plugin::GetDynamicChartClassNameArray() {
    return wxArrayString();
}
opencpn_plugin_116::~opencpn_plugin_116() = default;
opencpn_plugin_18::~opencpn_plugin_18() = default;
opencpn_plugin_19::~opencpn_plugin_19() = default;
opencpn_plugin_110::~opencpn_plugin_110() = default;
opencpn_plugin_111::~opencpn_plugin_111() = default;
opencpn_plugin_112::~opencpn_plugin_112() = default;
opencpn_plugin_113::~opencpn_plugin_113() = default;
opencpn_plugin_114::~opencpn_plugin_114() = default;
opencpn_plugin_115::~opencpn_plugin_115() = default;
bool opencpn_plugin_18::RenderOverlay(wxDC&, PlugIn_ViewPort*) {
    return false;
}
bool opencpn_plugin_18::RenderGLOverlay(wxGLContext*, PlugIn_ViewPort*) {
    return false;
}
void opencpn_plugin_18::SetPluginMessage(wxString&, wxString&) {}
void opencpn_plugin_18::SetPositionFixEx(PlugIn_Position_Fix_Ex&) {}
void opencpn_plugin_19::OnSetupOptions() {}
void opencpn_plugin_110::LateInit() {}
void opencpn_plugin_112::SendVectorChartObjectInfo(wxString&, wxString&, wxString&, double, double, double,
                                                   int) {}
bool opencpn_plugin_113::KeyboardEventHook(wxKeyEvent&) {
    return false;
}
void opencpn_plugin_113::OnToolbarToolDownCallback(int) {}
void opencpn_plugin_113::OnToolbarToolUpCallback(int) {}
void opencpn_plugin_116::PrepareContextMenu(int) {}
bool opencpn_plugin_116::RenderGLOverlayMultiCanvas(wxGLContext*, PlugIn_ViewPort*, int) {
    return false;
}
bool opencpn_plugin_116::RenderOverlayMultiCanvas(wxDC&, PlugIn_ViewPort*, int) {
    return false;
}
int opencpn_plugin_117::GetPlugInVersionPatch() {
    return 0;
}
int opencpn_plugin_117::GetPlugInVersionPost() {
    return 0;
}
const char* opencpn_plugin_117::GetPlugInVersionPre() {
    return "";
}
const char* opencpn_plugin_117::GetPlugInVersionBuild() {
    return "";
}
void opencpn_plugin_117::SetActiveLegInfo(Plugin_Active_Leg_Info&) {}
opencpn_plugin_118::opencpn_plugin_118(void* pmgr) : opencpn_plugin_117(pmgr) {}
bool opencpn_plugin_118::RenderGLOverlayMultiCanvas(wxGLContext*, PlugIn_ViewPort*, int, int) {
    return false;
}
bool opencpn_plugin_118::RenderOverlayMultiCanvas(wxDC&, PlugIn_ViewPort*, int, int) {
    return false;
}
#endif
