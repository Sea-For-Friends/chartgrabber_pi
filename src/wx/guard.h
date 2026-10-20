// SPDX-License-Identifier: GPL-3.0-or-later
// Error containment : an exception that leaves a plugin callback ends
// OpenCPN, so every entry point OpenCPN or wx calls runs through guarded / guardedOr. Header-only.
#pragma once

#include "plugin_log.h"

#include <wx/string.h>

#include <exception>
#include <utility>

namespace cg {

// Logging must not throw out of a noexcept helper.
inline void logCaught(const char* where, const char* what) noexcept {
    try {
        wxString text = wxString::FromUTF8(what); // what may be UTF-8 (a path with Cyrillic)
        if (text.empty() && *what != '\0')
            text = wxString::From8BitData(what);
        logLine(wxString::Format("%s: %s", wxString::FromAscii(where), text));
    } catch (...) {
    }
}

// Runs f; catches std::exception (logs "ChartGrabber: <where>: <what>") and... (logs "ChartGrabber:
// <where>: unknown exception"); never rethrows. Returns true when f returned normally.
template <class F> bool guarded(const char* where, F&& f) noexcept {
    try {
        std::forward<F>(f)();
        return true;
    } catch (const std::exception& e) {
        logCaught(where, e.what());
    } catch (...) {
        logCaught(where, "unknown exception");
    }
    return false;
}

// For callbacks with a result: `fallback` when f throws.
template <class R, class F> R guardedOr(const char* where, R fallback, F&& f) noexcept {
    try {
        return std::forward<F>(f)();
    } catch (const std::exception& e) {
        logCaught(where, e.what());
    } catch (...) {
        logCaught(where, "unknown exception");
    }
    return fallback;
}

// Load guard : the plugin stays off exactly when the running wx library and the build disagree
// about hotkey support.
inline bool loadGuardOff(bool hostHasHotkeySymbol, bool buildHasHotkey) {
    return hostHasHotkeySymbol != buildHasHotkey;
}

} // namespace cg
