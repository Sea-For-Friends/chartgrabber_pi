// SPDX-License-Identifier: GPL-3.0-or-later
// The plugin class.
#pragma once

#include "settings_dialog.h"

#include <wx/bitmap.h>
#include <wx/fileconf.h>
#include <wx/string.h>
#include <wx/weakref.h>
#include <wx/window.h>

#include <cstdint> // ocpn_plugin.h uses uint8_t / uint64_t without including it
#include <memory>

#include <ocpn_plugin.h>

class ConfigStore;
class Look;
class AreaTool;
class JobController;
class MainDialog;

class ChartGrabberPi final : public opencpn_plugin_118 {
public:
    explicit ChartGrabberPi(void* ppimgr);
    ~ChartGrabberPi() override;
    int Init() override;
    bool DeInit() override;
    int GetAPIVersionMajor() override;
    int GetAPIVersionMinor() override; // 1, 18
    int GetPlugInVersionMajor() override;
    int GetPlugInVersionMinor() override; // CMake project version YYYY.MM.N: 2026 / 10
    int GetPlugInVersionPatch() override; // N (OpenCPN 5.10.2 reads it)
    wxBitmap* GetPlugInBitmap() override;
    wxString GetCommonName() override;       // "ChartGrabber" (= metadata <name>)
    wxString GetShortDescription() override; // "Chart Grabber: download charts for use offline"
    wxString GetLongDescription() override;
    int GetToolbarToolCount() override; // 1
    void OnToolbarToolCallback(int id) override;
    void OnContextMenuItemCallback(int id) override;
    void ShowPreferencesDialog(wxWindow* parent) override;
    void SetColorScheme(PI_ColorScheme cs) override;
    bool MouseEventHook(wxMouseEvent& event) override;
    bool RenderOverlayMultiCanvas(wxDC& dc, PlugIn_ViewPort* vp, int canvasIndex, int priority) override;
    bool RenderGLOverlayMultiCanvas(wxGLContext* ctx, PlugIn_ViewPort* vp, int canvasIndex,
                                    int priority) override;

    // The toolbar tool shows the running state: bitmaps first, then the toggle
    // (JobController::onBusyChanged).
    void setToolBusy(bool busy);

private:
    void openSettings(); // the strip's Preferences...: modeless Settings window

    std::unique_ptr<ConfigStore> config_;
    std::unique_ptr<Look> look_;
    std::unique_ptr<AreaTool> areaTool_;
    std::unique_ptr<JobController> jobs_;
    MainDialog* dialog_ = nullptr;       // modeless, parent GetOCPNCanvasWindow, created on first use
    wxWeakRef<SettingsDialog> settings_; // opened from the strip; deleted first in DeInit
    bool toolBusy_ = false;              // the last setToolBusy value; the click re-asserts it
    int toolId_ = -1, menuViewId_ = -1, menuDrawId_ = -1, menuRouteId_ = -1;
    std::unique_ptr<wxMenuItem> menuViewItem_, menuDrawItem_, menuRouteItem_;
    std::unique_ptr<wxBitmap> bitmap_;
};

extern "C" DECL_EXP opencpn_plugin* create_pi(void* ppimgr);
extern "C" DECL_EXP void destroy_pi(opencpn_plugin* p);
