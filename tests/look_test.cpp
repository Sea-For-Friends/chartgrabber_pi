// SPDX-License-Identifier: GPL-3.0-or-later
// Look::apply : nothing by day, DimeWindow only at dusk / night.
#include "look.h"
#include "ocpn_fakes.h"
#include "wx_env.h"

#include <gtest/gtest.h>

#include <wx/app.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <vector>

#include <ocpn_plugin.h>

namespace {

struct Tree {
    wxDialog* dialog;
    wxPanel* panel;
    wxStaticText* text;
    wxCheckBox* check;
    wxButton* button;
    wxTextCtrl* field;
    wxStaticText* grandchild;
    std::vector<wxWindow*> all() const { return {dialog, panel, text, check, button, field, grandchild}; }
};

Tree makeTree() {
    Tree t;
    t.dialog = new wxDialog(nullptr, wxID_ANY, "look");
    t.panel = new wxPanel(t.dialog);
    t.text = new wxStaticText(t.panel, wxID_ANY, "label");
    t.check = new wxCheckBox(t.panel, wxID_ANY, "check");
    t.button = new wxButton(t.panel, wxID_ANY, "button");
    t.field = new wxTextCtrl(t.panel, wxID_ANY, "field");
    auto* inner = new wxPanel(t.panel);
    t.grandchild = new wxStaticText(inner, wxID_ANY, "deep");
    return t;
}

// The colours of the fresh controls (the system theme), in all order.
std::vector<wxColour> themeColours(const Tree& t) {
    std::vector<wxColour> v;
    for (wxWindow* w : t.all()) {
        v.push_back(w->GetBackgroundColour());
        v.push_back(w->GetForegroundColour());
    }
    return v;
}

class LookApply : public ::testing::Test {
protected:
    void SetUp() override {
        if (!wxTheApp || !wxTheApp->IsGUI())
            GTEST_SKIP() << "no display: the Look::apply test was NOT run";
        ocpn_fake::globalColorCalls = 0;
        ocpn_fake::dimeCalls = 0;
        ocpn_fake::dimeLast = nullptr;
    }
};

TEST_F(LookApply, DayAndRgbSetNoColourAndDoNotDime) {
    for (PI_ColorScheme cs : {PI_GLOBAL_COLOR_SCHEME_DAY, PI_GLOBAL_COLOR_SCHEME_RGB}) {
        Look look;
        look.setScheme(cs);
        Tree t = makeTree();
        const auto theme = themeColours(t);
        look.apply(t.dialog);
        for (wxWindow* w : t.all())
            EXPECT_FALSE(w->UseBgCol()) << "scheme " << cs;
        EXPECT_EQ(theme, themeColours(t)) << "scheme " << cs;
        EXPECT_EQ(0, ocpn_fake::dimeCalls);
        EXPECT_EQ(0, ocpn_fake::globalColorCalls);
        t.dialog->Destroy();
    }
}

TEST_F(LookApply, DuskAndNightCallDimeWindowOnceAndSetNoColourThemselves) {
    for (PI_ColorScheme cs : {PI_GLOBAL_COLOR_SCHEME_DUSK, PI_GLOBAL_COLOR_SCHEME_NIGHT}) {
        ocpn_fake::dimeCalls = 0;
        Look look;
        look.setScheme(cs);
        Tree t = makeTree();
        look.apply(t.dialog);
        EXPECT_EQ(1, ocpn_fake::dimeCalls) << "scheme " << cs;
        EXPECT_EQ(t.dialog, ocpn_fake::dimeLast);
        for (wxWindow* w : t.all()) {
            EXPECT_EQ(wxColour(1, 2, 3), w->GetBackgroundColour()) << "scheme " << cs;
            EXPECT_EQ(wxColour(4, 5, 6), w->GetForegroundColour()) << "scheme " << cs;
        }
        EXPECT_EQ(0, ocpn_fake::globalColorCalls);
        t.dialog->Destroy();
    }
}

TEST_F(LookApply, NightThenDayRemovesTheMarkerFromTheWindowAndADescendant) {
    Look look;
    Tree t = makeTree();
    const auto theme = themeColours(t);
    const int token = look.subscribe([&] { look.apply(t.dialog); });
    look.setScheme(PI_GLOBAL_COLOR_SCHEME_NIGHT);
    ASSERT_EQ(1, ocpn_fake::dimeCalls);
    ASSERT_TRUE(t.grandchild->UseBgCol());
    look.setScheme(PI_GLOBAL_COLOR_SCHEME_DAY);
    EXPECT_EQ(1, ocpn_fake::dimeCalls);
    for (wxWindow* w : t.all())
        EXPECT_FALSE(w->UseBgCol());
    EXPECT_EQ(theme, themeColours(t));
    look.unsubscribe(token);
    t.dialog->Destroy();
}

// ---- the opaque body colours of the strip ----

class LookBody : public ::testing::Test {
protected:
    void SetUp() override {
        if (!wxTheApp || !wxTheApp->IsGUI())
            GTEST_SKIP() << "no display: the Look body test was NOT run";
        ocpn_fake::globalColors.clear();
        ocpn_fake::globalColorAsked.clear();
    }
    void TearDown() override {
        ocpn_fake::globalColors.clear();
        ocpn_fake::globalColorAsked.clear();
    }
};

TEST_F(LookBody, DayAndRgbGiveTheSystemButtonColoursAndDuskAndNightOpenCpnsOwn) {
    const wxColour face = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
    const wxColour text = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
    const wxColour uibck(11, 222, 33), uitx1(201, 12, 133);
    ASSERT_NE(face, uibck);
    ASSERT_NE(text, uitx1);
    ocpn_fake::globalColors["UIBCK"] = uibck;
    ocpn_fake::globalColors["UITX1"] = uitx1;
    for (PI_ColorScheme cs : {PI_GLOBAL_COLOR_SCHEME_DAY, PI_GLOBAL_COLOR_SCHEME_RGB}) {
        Look look;
        look.setScheme(cs);
        EXPECT_EQ(face, look.bodyBackground()) << cs;
        EXPECT_EQ(text, look.bodyText()) << cs;
    }
    EXPECT_TRUE(ocpn_fake::globalColorAsked.empty()) << "day does not ask OpenCPN";
    for (PI_ColorScheme cs : {PI_GLOBAL_COLOR_SCHEME_DUSK, PI_GLOBAL_COLOR_SCHEME_NIGHT}) {
        ocpn_fake::globalColorAsked.clear();
        Look look;
        look.setScheme(cs);
        EXPECT_EQ(uibck, look.bodyBackground()) << cs;
        ASSERT_EQ(1u, ocpn_fake::globalColorAsked.size());
        EXPECT_EQ(wxString("UIBCK"), ocpn_fake::globalColorAsked[0]);
        EXPECT_EQ(uitx1, look.bodyText()) << cs;
        ASSERT_EQ(2u, ocpn_fake::globalColorAsked.size());
        EXPECT_EQ(wxString("UITX1"), ocpn_fake::globalColorAsked[1]);
    }
}

TEST_F(LookBody, NightWithoutAnOpenCpnColourGivesTheDayValues) {
    Look look;
    look.setScheme(PI_GLOBAL_COLOR_SCHEME_NIGHT); // the fake has no colour
    EXPECT_EQ(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE), look.bodyBackground());
    EXPECT_EQ(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT), look.bodyText());
    EXPECT_EQ(2u, ocpn_fake::globalColorAsked.size()) << "it did ask";
}

} // namespace

TEST_F(LookBody, ControlColoursAreDilgAtDuskAndNightAndNullElsewhere) {
    const wxColour dilg1(110, 111, 112), dilg3(130, 131, 132);
    ocpn_fake::globalColors["DILG1"] = dilg1;
    ocpn_fake::globalColors["DILG3"] = dilg3;
    for (PI_ColorScheme cs : {PI_GLOBAL_COLOR_SCHEME_DAY, PI_GLOBAL_COLOR_SCHEME_RGB}) {
        Look look;
        look.setScheme(cs);
        EXPECT_FALSE(look.controlBackground().IsOk()) << cs;
        EXPECT_FALSE(look.controlText().IsOk()) << cs;
    }
    for (PI_ColorScheme cs : {PI_GLOBAL_COLOR_SCHEME_DUSK, PI_GLOBAL_COLOR_SCHEME_NIGHT}) {
        Look look;
        look.setScheme(cs);
        EXPECT_EQ(dilg1, look.controlBackground()) << cs;
        EXPECT_EQ(dilg3, look.controlText()) << cs;
    }
    ocpn_fake::globalColors.clear(); // GetGlobalColor fails
    Look night;
    night.setScheme(PI_GLOBAL_COLOR_SCHEME_NIGHT);
    EXPECT_FALSE(night.controlBackground().IsOk());
    EXPECT_FALSE(night.controlText().IsOk());
}
