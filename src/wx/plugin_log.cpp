// SPDX-License-Identifier: GPL-3.0-or-later
#include "plugin_log.h"

#include "job_files.h"
#include "plugin_version.h"
#include "portable.h"
#include "session_log.h"
#include "wx_util.h"

#include <wx/fileconf.h>
#include <wx/log.h>
#include <wx/utils.h>
#include <wx/window.h>

#include <chrono>
#include <cstdint>
#include <ctime>
#ifndef __WINDOWS__
#include <dlfcn.h>
#endif
#include <filesystem>
#include <memory>
#include <system_error>
#include <vector>

#include <ocpn_plugin.h>

namespace {

cg::SessionLog g_file;
bool g_debug = false;
std::string g_display, g_openGl; // header lines 4 and 5: what line 8 decides on

std::string stamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t secs = std::chrono::system_clock::to_time_t(now);
    const int ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    return cg::formatLogStamp(cg::localTime(secs), ms);
}

void write(const char* prefix, const wxString& text) {
    g_file.write(stamp(), prefix, cg::toUtf8(text));
}

// The environment variable as "set" / "unset".
const char* setText(const char* name) {
    return wxGetEnv(name, nullptr) ? "set" : "unset";
}

wxString envValue(const char* name) {
    wxString v;
    return wxGetEnv(name, &v) ? v : wxString("unset");
}

std::string libraryText() {
#ifdef __WINDOWS__
    // ponytail: GetModuleHandleExW(FROM_ADDRESS) + GetModuleFileNameW when the UAT log needs the DLL path
    return "library unknown";
#else
    Dl_info info{};
    if (!dladdr(reinterpret_cast<void*>(&cg::writePluginLogHeader), &info) || !info.dli_fname)
        return "library unknown";
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path file = info.dli_fname;
    const std::uintmax_t size = fs::file_size(file, ec);
    std::string text = "library " + file.string();
    if (ec)
        return text;
    const auto time = fs::last_write_time(file, ec);
    if (ec)
        return text + " (" + std::to_string(size) + " bytes)";
    // Not the C++20 clock conversion (libstdc++ 13+ only): shift by the two clocks' present readings, exact
    // to the gap between the two now() calls.
    const std::time_t secs = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now() + std::chrono::duration_cast<std::chrono::system_clock::duration>(
                                               time - fs::file_time_type::clock::now()));
    return text + " (" + std::to_string(size) + " bytes, modified " +
           cg::formatLogStamp(cg::localTime(secs), 0).substr(0, 19) + ")";
#endif
}

std::string programText() {
    std::error_code ec;
    const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? "unknown" : exe.string();
}

} // namespace

namespace cg {

std::string defaultPluginLogPath() {
    const wxString* dir = GetpPrivateApplicationDataLocation();
    if (!dir || dir->empty())
        return std::string();
    return toUtf8(*dir) + "/plugins/chartgrabber/" + std::string(kSessionLogName);
}

bool openPluginLog(const std::string& path) {
    if (path.empty())
        return false;
    std::string why;
    if (g_file.open(path, kSessionLogCapBytes, &why))
        return true;
    logLine(wxString::Format("cannot write the log file %s: %s", fromUtf8(path), fromUtf8(why)));
    return false;
}

void closePluginLog() {
    logFile("session end");
    g_file.close();
    g_debug = false;
}

std::string pluginLogPath() {
    return g_file.path();
}

bool waylandOpenGlCovers(const std::string& display, const std::string& openGl) {
    return display == "wayland" && openGl == "on";
}

void writePluginLogHeader() {
    logFile(wxString::Format("Chart Grabber %s, %s", kPluginVersionString, fromUtf8(libraryText())));

    wxFileConfig* cfg = GetOCPNConfigObject();
    wxString saved;
    const bool haveSaved = cfg && cfg->Read("/Settings/ConfigVersionString", &saved) && !saved.empty();
    logFile(wxString::Format("OpenCPN (config saved by) %s, program %s, plugin API %d.%d",
                             haveSaved ? saved : wxString("version unknown"), fromUtf8(programText()),
                             API_VERSION_MAJOR, API_VERSION_MINOR));

    logFile(wxString::Format("%s (built with %s)", wxGetLibraryVersionInfo().GetVersionString(),
                             wxVERSION_STRING));

    g_display = "unknown";
#ifdef __WXGTK__
    switch (wxGetDisplayInfo().type) {
    case wxDisplayX11:
        g_display = "x11";
        break;
    case wxDisplayWayland:
        g_display = "wayland";
        break;
    default:
        g_display = "none";
    }
#endif
    logFile(wxString::Format("display %s, WAYLAND_DISPLAY %s, GDK_BACKEND=%s, OCPN_DISABLE_X11_GDK_BACKEND "
                             "%s, XDG_SESSION_TYPE=%s, "
                             "content scale %s",
                             g_display, setText("WAYLAND_DISPLAY"), envValue("GDK_BACKEND"),
                             setText("OCPN_DISABLE_X11_GDK_BACKEND"), envValue("XDG_SESSION_TYPE"),
                             formatNumber(OCPN_GetDisplayContentScaleFactor(), 2)));

    wxString gl;
    const bool haveGl = cfg && cfg->Read("/Settings/OpenGL", &gl);
    g_openGl = !haveGl ? "unknown" : gl == "1" ? "on" : gl == "0" ? "off" : "unknown";
    logFile(wxString::Format("OpenGL %s", g_openGl));

    wxString canvas = "none";
    if (const wxWindow* w = GetOCPNCanvasWindow()) {
        const wxSize size = w->GetClientSize();
        canvas = wxString::Format("%dx%d px", size.x, size.y);
    }
    logFile(wxString::Format("canvases %d, primary canvas %s, chart bar %d px", GetCanvasCount(), canvas,
                             GetChartbarHeight()));
}

void writeWaylandGlLine(bool stripOnChart) {
    if (!waylandOpenGlCovers(g_display, g_openGl))
        return;
    write(
        "warning: ",
        wxString::Format(
            "native Wayland with OpenGL: a window inside the chart is covered by the chart's OpenGL surface "
            "(wxWidgets 3.2 EGL subsurface); progress on the chart %s",
            stripOnChart ? "is on: it will not be visible" : "is off: progress uses its own window"));
}

void endPluginLogHeader() {
    g_file.endHeader();
}

void setPluginLogDebug(bool on) {
    g_debug = on;
}

bool pluginLogDebug() {
    return g_debug;
}

void logLine(const wxString& text) {
    wxLogMessage("%s", "ChartGrabber: " + text);
    write("", text);
}

void logWarning(const wxString& text) {
    wxLogWarning("%s", "ChartGrabber: " + text);
    write("warning: ", text);
}

void logFile(const wxString& text) {
    write("", text);
}

void logDebug(const wxString& text) {
    if (g_debug)
        write("debug: ", text);
}

} // namespace cg
