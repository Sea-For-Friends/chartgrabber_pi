// SPDX-License-Identifier: GPL-3.0-or-later
#include "look.h"

#include <wx/settings.h>
#include <wx/window.h>

#include <cmath>

void Look::setScheme(PI_ColorScheme cs) {
    scheme_ = cs;
    const auto copy = subscribers_;
    for (const auto& entry : copy)
        entry.second();
}

PI_ColorScheme Look::scheme() const {
    return scheme_;
}

// Gives the system theme back: no plugin colour on the window or any descendant.
static void resetColours(wxWindow* window) {
    window->SetBackgroundColour(wxNullColour);
    window->SetForegroundColour(wxNullColour);
    for (wxWindow* child : window->GetChildren())
        resetColours(child);
}

void Look::apply(wxWindow* top) const {
    if (!top) // unreachable: every caller passes the window it is creating
        return;
    if (scheme_ == PI_GLOBAL_COLOR_SCHEME_DUSK || scheme_ == PI_GLOBAL_COLOR_SCHEME_NIGHT)
        DimeWindow(top);
    else
        resetColours(top);
}

wxColour Look::outline() const {
    switch (scheme_) {
    case PI_GLOBAL_COLOR_SCHEME_DUSK:
        return wxColour(200, 0, 0);
    case PI_GLOBAL_COLOR_SCHEME_NIGHT:
        return wxColour(128, 0, 0);
    default:
        return wxColour(255, 0, 255);
    }
}

static wxColour bodyColour(PI_ColorScheme scheme, const char* name, wxSystemColour fallback) {
    if (scheme == PI_GLOBAL_COLOR_SCHEME_DUSK || scheme == PI_GLOBAL_COLOR_SCHEME_NIGHT) {
        wxColour c;
        if (GetGlobalColor(name, &c) && c.IsOk())
            return c;
    }
    return wxSystemSettings::GetColour(fallback);
}

wxColour Look::bodyBackground() const {
    return bodyColour(scheme_, "UIBCK", wxSYS_COLOUR_BTNFACE);
}

wxColour Look::bodyText() const {
    return bodyColour(scheme_, "UITX1", wxSYS_COLOUR_BTNTEXT);
}

static wxColour controlColour(PI_ColorScheme scheme, const char* name) {
    if (scheme == PI_GLOBAL_COLOR_SCHEME_DUSK || scheme == PI_GLOBAL_COLOR_SCHEME_NIGHT) {
        wxColour c;
        if (GetGlobalColor(name, &c) && c.IsOk())
            return c;
    }
    return wxNullColour;
}

wxColour Look::controlBackground() const {
    return controlColour(scheme_, "DILG1");
}

wxColour Look::controlText() const {
    return controlColour(scheme_, "DILG3");
}

int Look::minControlHeight() const {
    if (!touch())
        return 0;
    return static_cast<int>(std::ceil(48.0 * GetOCPNGUIToolScaleFactor_PlugIn()));
}

bool Look::touch() const {
    return IsTouchInterface_PlugIn();
}

int Look::subscribe(std::function<void()> onChange) const {
    const int token = nextToken_++;
    subscribers_.emplace_back(token, std::move(onChange));
    return token;
}

void Look::unsubscribe(int token) const {
    std::erase_if(subscribers_, [token](const auto& entry) { return entry.first == token; });
}
