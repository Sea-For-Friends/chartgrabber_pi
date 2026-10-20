// SPDX-License-Identifier: GPL-3.0-or-later
#include "chartgrabber_pi.h"

#include "area_tool.h"
#include "guard.h"
#include "job_files.h"
#include "main_dialog.h"
#include "plugin_log.h"
#include "plugin_version.h"
#include "wx_util.h"

#include <wx/intl.h>
#include <wx/log.h>
#include <wx/menuitem.h>

#include <dlfcn.h>
#include <optional>
#include <utility>

// The unique_ptr members of the complete header need their types here. MainDialog is held by raw pointer.
#include "cli_runner.h"
#include "config_store.h"
#include "job_controller.h"
#include "look.h"
#include "settings_dialog.h"

namespace {

// Set first in DeInit, reset in Init : while set, the entry points that OpenCPN may call during the
// event loop that DeInit pumps return at once.
bool g_closing = false;

// Set by Init when the load guard refuses; every later callback returns at once.
bool g_disabled = false;

// The first overlay call of each kind writes one log line; reset by Init.
bool g_firstGl = true, g_firstDc = true;

constexpr const char* kPluginDirName = "chartgrabber_pi";

wxString dataFile(const char* name) {
    return GetPluginDataDir(kPluginDirName) + "/data/" + name;
}

int addContextItem(const wxString& text, opencpn_plugin* owner, std::unique_ptr<wxMenuItem>& item) {
    item = std::make_unique<wxMenuItem>(nullptr, wxID_ANY, text);
    return AddCanvasContextMenuItem(item.get(), owner);
}

// Header line 7 of the plugin's log file : the settings the plugin runs with.
void logSettingsLine(const ConfigStore& config, const cg::Settings& settings) {
    std::string why;
    const std::optional<std::string> program = resolveCli(settings, &why);
    cg::logFile(wxString::Format(
        "settings: %s, chart sources %s, charts %s, saved tiles %s, progress on the chart %s, debug %s",
        program ? "CLI " + cg::fromUtf8(*program) : "CLI not found: " + cg::fromUtf8(why),
        cg::fromUtf8(config.sourcesDir(settings)), cg::fromUtf8(config.chartDir(settings)),
        cg::fromUtf8(config.cliCacheRoot(settings)), settings.stripOnChart ? "on" : "off",
        settings.debugLog ? "on" : "off"));
}

} // namespace

ChartGrabberPi::ChartGrabberPi(void* ppimgr) : opencpn_plugin_118(ppimgr) {}

ChartGrabberPi::~ChartGrabberPi() = default;

int ChartGrabberPi::Init() {
    g_closing = false;
    // Load guard : the host wx has hotkey support exactly when it exports this symbol.
    if (cg::loadGuardOff(dlsym(RTLD_DEFAULT, "_ZN12wxWindowBase14RegisterHotKeyEiii") != nullptr,
                         wxUSE_HOTKEY != 0)) {
        g_disabled = true;
        cg::logLine("this build of Chart Grabber does not match this wxWidgets (hotkey support "
                    "differs); the plugin stays off");
        return 0;
    }
    g_disabled = false;
    toolBusy_ = false;
    return cg::guardedOr("Init", 0, [this] {
        g_firstGl = g_firstDc = true;
        cg::openPluginLog(cg::defaultPluginLogPath());
        cg::writePluginLogHeader();
        if (wxFileConfig* cfg = GetOCPNConfigObject())
            config_ = std::make_unique<ConfigStore>(cfg);
        if (config_)
            config_->setBundledSourcesDir(cg::toUtf8(dataFile("sources")));
        cg::Settings settings;
        if (config_) {
            settings = config_->loadSettings(); // logs the invalid values before the settings line
            logSettingsLine(*config_, settings);
        } else {
            cg::logFile("settings: none (no OpenCPN config)");
        }
        cg::writeWaylandGlLine(settings.stripOnChart);
        cg::endPluginLogHeader();
        cg::setPluginLogDebug(settings.debugLog);
        look_ = std::make_unique<Look>();
        // wxITEM_CHECK: OpenCPN 5.10.2 toggles only tools that CanBeToggled (toolbar.cpp:1947-1954).
        toolId_ = InsertPlugInToolSVG(
            _("Chart Grabber"), dataFile("chartgrabber_tool.svg"), dataFile("chartgrabber_tool.svg"),
            dataFile("chartgrabber_tool_toggled.svg"), wxITEM_CHECK, _("Chart Grabber"),
            _("Download a chart of an area for use offline"), nullptr, -1, 0, this);
        areaTool_ = std::make_unique<AreaTool>([l = look_.get()] { return l->outline(); },
                                               [l = look_.get()] { return l->touch(); });
        if (config_) { // OpenCPN ended during a job last time: never auto-resumed
            for (cg::ChartRecord r : config_->loadCharts()) {
                if (r.state != cg::ChartState::Running)
                    continue;
                r.state = cg::ChartState::Interrupted;
                config_->saveChart(r);
            }
            jobs_ = std::make_unique<JobController>(*config_, *look_, *areaTool_);
            jobs_->onBusyChanged = [this](bool busy) {
                setToolBusy(busy);
                if (dialog_)
                    dialog_->busyChanged(busy);
            };
            jobs_->onChartsChanged = [this] {
                if (dialog_)
                    dialog_->chartsChanged();
            };
            jobs_->onPreferences = [this] { openSettings(); };
        }
        menuViewId_ = addContextItem(_("Chart Grabber: new chart of this view..."), this, menuViewItem_);
        menuDrawId_ = addContextItem(_("Chart Grabber: draw an area..."), this, menuDrawItem_);
        menuRouteId_ = addContextItem(_("Chart Grabber: charts along route..."), this, menuRouteItem_);
        cg::logLine(wxString::Format("Init API %d.%d, plugin %s", GetAPIVersionMajor(), GetAPIVersionMinor(),
                                     wxString(cg::kPluginVersionString)));
        return WANTS_OVERLAY_CALLBACK | WANTS_OPENGL_OVERLAY_CALLBACK | WANTS_MOUSE_EVENTS |
               INSTALLS_TOOLBAR_TOOL | WANTS_TOOLBAR_CALLBACK | INSTALLS_CONTEXTMENU_ITEMS | WANTS_CONFIG |
               WANTS_PREFERENCES;
    });
}

bool ChartGrabberPi::DeInit() {
    if (g_disabled)
        return true;
    g_closing = true;
    return cg::guardedOr("DeInit", true, [this] {
        // The strip's Settings window first: its callbacks must not outlive the library.
        delete settings_.get();
        if (MainDialog* d =
                std::exchange(dialog_, nullptr)) { // hooks check dialog_, nulled before the delete
            d->Close();
            delete d;
        }
        jobs_.reset(); // waits up to 15 s for the child, saves the record Interrupted
        if (toolId_ != -1) {
            RemovePlugInTool(toolId_);
            toolId_ = -1;
        }
        for (int* id : {&menuViewId_, &menuDrawId_, &menuRouteId_}) {
            if (*id != -1)
                RemoveCanvasContextMenuItem(*id);
            *id = -1;
        }
        // RemoveCanvasContextMenuItem deletes only the container (pluginmanager.cpp:1751-1762): the items are
        // ours.
        menuViewItem_.reset();
        menuDrawItem_.reset();
        menuRouteItem_.reset();
        if (areaTool_)
            areaTool_->cancel();
        areaTool_.reset();
        bitmap_.reset();
        look_.reset();
        config_.reset();
        cg::closePluginLog();
        return true;
    });
}

int ChartGrabberPi::GetAPIVersionMajor() {
    return API_VERSION_MAJOR;
}
int ChartGrabberPi::GetAPIVersionMinor() {
    return API_VERSION_MINOR;
}
int ChartGrabberPi::GetPlugInVersionMajor() {
    return cg::kPluginVersionMajor;
}
int ChartGrabberPi::GetPlugInVersionMinor() {
    return cg::kPluginVersionMinor;
}
int ChartGrabberPi::GetPlugInVersionPatch() {
    return cg::kPluginVersionPatch;
}

wxBitmap* ChartGrabberPi::GetPlugInBitmap() {
    if (!bitmap_)
        bitmap_ = std::make_unique<wxBitmap>(GetBitmapFromSVGFile(dataFile("chartgrabber_tool.svg"), 32, 32));
    return bitmap_.get();
}

wxString ChartGrabberPi::GetCommonName() {
    return "ChartGrabber";
}

wxString ChartGrabberPi::GetShortDescription() {
    return _("Chart Grabber: download charts for use offline");
}

wxString ChartGrabberPi::GetLongDescription() {
    return _(
        "Pick an area on the chart, a chart source and the detail; Chart Grabber downloads it with Chart "
        "Grabber CLI and adds it to OpenCPN as a chart that works without internet.");
}

int ChartGrabberPi::GetToolbarToolCount() {
    return 1;
}

void ChartGrabberPi::setToolBusy(bool busy) {
    toolBusy_ = busy;
    if (toolId_ == -1)
        return;
    // The rollover file wins over the toggled one while the pointer is over the tool (toolbar.cpp:1582-1588).
    SetToolbarToolBitmapsSVG(
        toolId_, dataFile("chartgrabber_tool.svg"),
        dataFile(busy ? "chartgrabber_tool_rollover_toggled.svg" : "chartgrabber_tool.svg"),
        dataFile("chartgrabber_tool_toggled.svg"));
    SetToolbarItemState(toolId_, busy);
}

void ChartGrabberPi::OnToolbarToolCallback(int) {
    if (g_disabled || g_closing)
        return;
    if (toolId_ != -1) // a click flips a CHECK tool and OpenCPN does not undo it (toolbar.cpp:1483-1488)
        SetToolbarItemState(toolId_, toolBusy_);
    cg::guarded("toolbar", [this] {
        const bool created = !dialog_;
        if (created) {
            if (!config_ || !jobs_) {
                cg::logLine("no config");
                return;
            }
            dialog_ = new MainDialog(GetOCPNCanvasWindow(), *config_, *look_, *areaTool_, *jobs_);
        }
        if (!created) {
            dialog_->present();
            return;
        }
        const ConfigStore::LastChoice last = config_->loadLastChoice();
        const bool rect = last.areaKind == cg::AreaKind::Rectangle && last.rectangle;
        dialog_->showNewChart(rect || last.areaKind == cg::AreaKind::Route ? last.areaKind
                                                                           : cg::AreaKind::View,
                              rect ? last.rectangle : std::nullopt);
    });
}
void ChartGrabberPi::OnContextMenuItemCallback(int id) {
    if (g_disabled || g_closing)
        return;
    cg::guarded("context menu", [this, id] {
        if (!dialog_) {
            if (!config_ || !jobs_) {
                cg::logLine("no config");
                return;
            }
            dialog_ = new MainDialog(GetOCPNCanvasWindow(), *config_, *look_, *areaTool_, *jobs_);
        }
        if (id == menuViewId_)
            dialog_->showNewChart(cg::AreaKind::View, std::nullopt);
        else if (id == menuDrawId_)
            dialog_->drawArea();
        else if (id == menuRouteId_)
            dialog_->showNewChart(cg::AreaKind::Route, std::nullopt);
    });
}
void ChartGrabberPi::ShowPreferencesDialog(wxWindow* parent) {
    if (g_disabled || g_closing || !config_ ||
        !look_) // g_closing: DeInit pumps events; the others are unreachable
        return;
    cg::guarded("preferences", [this, parent] {
        SettingsDialog dialog(parent, *config_, *look_, [this] { return jobs_ && jobs_->busy(); });
        dialog.ShowModal();
    });
}
// The strip's "Preferences...": a modeless window one Settings window at a time
// ): an open one, the page's or this, is raised.
void ChartGrabberPi::openSettings() {
    if (g_disabled || g_closing || !config_ ||
        !look_) // g_closing: DeInit pumps events; the others are unreachable
        return;
    cg::guarded("settings", [this] {
        if (SettingsDialog* open = SettingsDialog::shown()) {
            open->Raise();
            return;
        }
        auto* dlg = new SettingsDialog(GetOCPNCanvasWindow(), *config_, *look_,
                                       [this] { return jobs_ && jobs_->busy(); });
        dlg->onDone = [this](bool saved) { // an open main dialog reads its sources again
            if (saved && dialog_)
                dialog_->settingsSaved();
        };
        settings_ = dlg;
        dlg->Show();
        dlg->Raise();
    });
}
void ChartGrabberPi::SetColorScheme(PI_ColorScheme cs) {
    if (g_disabled)
        return;
    cg::guarded("colour scheme", [this, cs] {
        if (look_) // unreachable before Init; the guard keeps a scheme change from crashing a half-built
                   // plugin
            look_->setScheme(cs);
    });
}
bool ChartGrabberPi::MouseEventHook(wxMouseEvent& event) {
    if (g_disabled || g_closing)
        return false;
    return cg::guardedOr("mouse", false, [this, &event] { return areaTool_ && areaTool_->onMouse(event); });
}
bool ChartGrabberPi::RenderOverlayMultiCanvas(wxDC& dc, PlugIn_ViewPort* vp, int canvasIndex, int priority) {
    if (g_disabled)
        return false;
    return cg::guardedOr("overlay", false, [&, this] {
        if (std::exchange(g_firstDc, false))
            cg::logFile("first chart draw: no OpenGL");
        if (!areaTool_ || !vp)
            return false;
        areaTool_->rememberViewport(canvasIndex, *vp);
        if (priority != OVERLAY_LEGACY)
            return false;
        areaTool_->drawDC(dc, *vp);
        return true;
    });
}
bool ChartGrabberPi::RenderGLOverlayMultiCanvas(wxGLContext*, PlugIn_ViewPort* vp, int canvasIndex,
                                                int priority) {
    if (g_disabled)
        return false;
    return cg::guardedOr("GL overlay", false, [&, this] {
        if (std::exchange(g_firstGl, false))
            cg::logFile("first chart draw: OpenGL");
        if (!areaTool_ || !vp)
            return false;
        areaTool_->rememberViewport(canvasIndex, *vp);
        if (priority != OVERLAY_LEGACY)
            return false;
        areaTool_->drawGL(*vp);
        return true;
    });
}

extern "C" DECL_EXP opencpn_plugin* create_pi(void* ppimgr) {
    return new ChartGrabberPi(ppimgr);
}
extern "C" DECL_EXP void destroy_pi(opencpn_plugin* p) {
    delete p;
}
