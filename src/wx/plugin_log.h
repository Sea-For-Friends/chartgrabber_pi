// SPDX-License-Identifier: GPL-3.0-or-later
// The ONE logging path of src/wx : OpenCPN's log and the plugin's own file.
// Free functions over one static cg::SessionLog (one plugin instance per process). GUI thread only. None
// throws.
#pragma once

#include <wx/string.h>

#include <string>

namespace cg {

// *GetpPrivateApplicationDataLocation + "/plugins/chartgrabber/" + kSessionLogName; empty when null or
// empty.
std::string defaultPluginLogPath();
// Opens the file truncated. An empty path gives false and logs nothing; a failure gives ONE OpenCPN log line
// and false.
bool openPluginLog(const std::string& path);
void closePluginLog();       // "session end", then close; debug off
std::string pluginLogPath(); // the open file's path, else empty
void writePluginLogHeader(); // header lines 1-6 of (Init, right after openPluginLog)
// Header line 8: written when the display (header line 4) is Wayland and OpenGL (line 5) is on.
void writeWaylandGlLine(bool stripOnChart);
bool waylandOpenGlCovers(const std::string& display, const std::string& openGl); // the decision of line 8
void endPluginLogHeader(); // after the settings line (and line 8)
void setPluginLogDebug(bool on);
bool pluginLogDebug();
void logLine(const wxString& text);    // OpenCPN's log ("ChartGrabber: " + text) and the file
void logWarning(const wxString& text); // the same as a warning ("warning: " in the file)
void logFile(const wxString& text);    // the file only
void logDebug(const wxString& text);   // the file only ("debug: "), only while pluginLogDebug

} // namespace cg
