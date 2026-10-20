// SPDX-License-Identifier: GPL-3.0-or-later
// Colour scheme and touch sizing shared by every plugin window and the overlay.
#pragma once

#include <wx/colour.h>

#include <cstdint> // ocpn_plugin.h uses uint8_t / uint64_t without including it
#include <functional>
#include <utility>
#include <vector>

#include <ocpn_plugin.h>

class wxWindow;

class Look {
public:
    void setScheme(PI_ColorScheme cs);
    PI_ColorScheme scheme() const;
    void apply(wxWindow* top) const; // day / RGB: reset colours of the window and descendants to
                                     // wxNullColour; dusk / night: DimeWindow only
    wxColour outline() const;        // day magenta 255,0,255; dusk 200,0,0; night 128,0,0 (RGB = day)
    // Opaque body of the strip / hint: day / RGB the system button face / text colours; dusk / night
    // OpenCPN's UIBCK / UITX1 (the day value when GetGlobalColor gives none).
    wxColour bodyBackground() const;
    wxColour bodyText() const;
    // Controls of the strip (buttons, gauge track): dusk / night OpenCPN's DILG1 / DILG3 (what DimeControl
    // gives its buttons); day / RGB or GetGlobalColor false: wxNullColour (nothing set, the system theme).
    wxColour controlBackground() const;
    wxColour controlText() const;
    int minControlHeight() const; // touch: 48 x GetOCPNGUIToolScaleFactor_PlugIn, rounded up; else 0
    bool touch() const;           // IsTouchInterface_PlugIn
    // Tokens start at 1 and are never reused; unsubscribe of an unknown token does nothing; setScheme
    // iterates over a copy of the list, so a callback may unsubscribe. A window that subscribes unsubscribes
    // in its destructor. Both are const so a window holding `const Look&` can use them; the list is mutable.
    int subscribe(std::function<void()> onChange) const;
    void unsubscribe(int token) const;

private:
    PI_ColorScheme scheme_ = PI_GLOBAL_COLOR_SCHEME_DAY;
    mutable std::vector<std::pair<int, std::function<void()>>> subscribers_;
    mutable int nextToken_ = 1;
};
