// SPDX-License-Identifier: GPL-3.0-or-later
// Finds and answers the modeless Question windows that a test's opener has open: by the
// top-level window list and by button events, with no timer and no loop. Header only.
#pragma once

#include "question.h"

#include <gtest/gtest.h>

#include <wx/button.h>
#include <wx/radiobut.h>
#include <wx/stattext.h>
#include <wx/toplevel.h>

#include <string>
#include <vector>

namespace qprobe {

// Every Question that is shown now (an answered one is hidden and waits for the idle delete).
inline std::vector<Question*> shown() {
    std::vector<Question*> out;
    for (auto* n = wxTopLevelWindows.GetFirst(); n; n = n->GetNext())
        if (auto* q = dynamic_cast<Question*>(n->GetData()); q && q->IsShown())
            out.push_back(q);
    return out;
}

inline wxButton* button(Question* q, int id) {
    return dynamic_cast<wxButton*>(q->FindWindow(id));
}

inline void press(Question* q, int id) {
    wxButton* b = button(q, id);
    ASSERT_NE(b, nullptr) << id;
    wxCommandEvent e(wxEVT_BUTTON, id);
    e.SetEventObject(b);
    b->GetEventHandler()->ProcessEvent(e);
}

inline void escape(Question* q) {
    wxKeyEvent e(wxEVT_CHAR_HOOK);
    e.m_keyCode = WXK_ESCAPE;
    e.SetEventObject(q);
    q->GetEventHandler()->ProcessEvent(e);
}

// The question text (the first static text; Wrap breaks it into lines).
inline std::string text(Question* q) {
    for (wxWindow* w : q->GetChildren())
        if (auto* t = dynamic_cast<wxStaticText*>(w)) {
            wxString s = t->GetLabel();
            s.Replace("\n", " ");
            return s.utf8_string();
        }
    return "<none>";
}

inline std::vector<wxRadioButton*> radios(Question* q) {
    std::vector<wxRadioButton*> out;
    for (wxWindow* w : q->GetChildren())
        if (auto* r = dynamic_cast<wxRadioButton*>(w))
            out.push_back(r);
    return out;
}

} // namespace qprobe
