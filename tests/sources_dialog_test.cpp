// SPDX-License-Identifier: GPL-3.0-or-later
// SourcesDialog on records the test
// builds: the filter, the flat list (every `group` empty) and the grouped list of records,
// refused rows greyed with their reason and not selectable, OK only on a usable row, Reload, touch sizes and
// the 800 x 480 fit. A window test (cg_gui_tests); the dialog is modal and a timer inside its loop drives it.
// Expected texts are literals; the GUIDs and EPSG values are those of the user's map set captured
// from the pinned CLI on 2026-10-15. Dusk / Night colours are OpenCPN's
// (GetGlobalColor is a fake that gives none here): NOT proven in this file.
#include "look.h"
#include "ocpn_fakes.h"
#include "sources_dialog.h"
#include "wx_env.h"

#include <gtest/gtest.h>

#include <wx/app.h>
#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/srchctrl.h>
#include <wx/stattext.h>
#include <wx/timer.h>

#include <functional>
#include <string>
#include <vector>

#include <ocpn_plugin.h>

namespace {

const char* const kYandex = "{5E04039A-331E-4D2C-B051-B00F8871BB39}"; // Yandex - Satellite, EPSG:3395
const char* const kEarth = "{FD6877DE-9885-4B92-9D01-F3746F2DD239}";  // Earth - GoogleEarth Cache, EPSG:4326
const char* const kProjection3395 = "Cannot be made into an OpenCPN chart (projection EPSG:3395)";
const char* const kProjection4326 = "Cannot be made into an OpenCPN chart (projection EPSG:4326)";
// The verdict of a WebP source: core src/sas/app/job_checks.cpp:192.
const char* const kWebp =
    "WebP tiles are not shown by every OpenCPN version; pass --allow-webp to export them anyway";

cg::SourceRecord rec(const char* guid, const char* name, int epsg = 3857, const char* verdict = "ok",
                     const char* group = "") {
    cg::SourceRecord r;
    r.guid = guid;
    r.kind = "map";
    r.name = name;
    r.epsg = epsg;
    r.verdict = verdict;
    r.group = group;
    return r;
}

std::string u(const wxString& s) {
    return s.utf8_string();
}

template <class T> void collect(wxWindow* top, std::vector<T*>& out) {
    for (wxWindow* child : top->GetChildren()) {
        if (auto* t = dynamic_cast<T*>(child))
            out.push_back(t);
        collect(child, out);
    }
}

// The dialog with its controls found; `drive` runs once, from a timer inside ShowModal, and must end it.
class Fixture {
public:
    Fixture(const std::vector<cg::SourceRecord>& sources, const Look& look,
            const cg::SourceDescriptions& descriptions = {})
        : sources_(sources), dlg_(nullptr, sources_, look, descriptions) {
        std::vector<wxListCtrl*> lists;
        collect(&dlg_, lists);
        list_ = lists.at(0);
        std::vector<wxSearchCtrl*> filters;
        collect(&dlg_, filters);
        filter_ = filters.at(0);
        std::vector<wxStaticText*> texts;
        collect(&dlg_, texts);
        detail_ = texts.at(0);
    }
    int run(const std::function<void()>& drive) {
        wxEvtHandler handler;
        wxTimer timer(&handler);
        handler.Bind(wxEVT_TIMER, [&](wxTimerEvent&) {
            timer.Stop();
            drive();
        });
        timer.Start(30);
        return dlg_.ShowModal();
    }
    // ---- what the dialog shows
    std::vector<std::string> rows() const { // column 0 of every row
        std::vector<std::string> out;
        for (long i = 0; i < list_->GetItemCount(); ++i)
            out.push_back(u(list_->GetItemText(i, 0)));
        return out;
    }
    std::string detail() const { return u(detail_->GetLabel()); }
    std::string note(long row) const { return u(list_->GetItemText(row, 1)); }
    bool okEnabled() const { return dlg_.FindWindow(wxID_OK)->IsEnabled(); }
    // ---- user actions
    void filter(const std::string& text) {
        filter_->ChangeValue(wxString::FromUTF8(text));
        wxCommandEvent e(wxEVT_TEXT, filter_->GetId());
        e.SetEventObject(filter_);
        e.SetString(filter_->GetValue());
        filter_->GetEventHandler()->ProcessEvent(e);
    }
    void select(long row) { list_->SetItemState(row, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED); }
    void activate(long row) {
        wxListEvent e(wxEVT_LIST_ITEM_ACTIVATED, list_->GetId());
        e.SetEventObject(list_);
        e.m_itemIndex = row;
        list_->GetEventHandler()->ProcessEvent(e);
    }
    void click(wxWindow* b) {
        wxCommandEvent e(wxEVT_BUTTON, b->GetId());
        e.SetEventObject(b);
        b->GetEventHandler()->ProcessEvent(e);
    }
    void clickId(int id) { click(dlg_.FindWindow(id)); }
    void clickLabel(const char* label) { click(wxWindow::FindWindowByLabel(label, &dlg_)); }

    std::vector<cg::SourceRecord> sources_;
    SourcesDialog dlg_;
    wxListCtrl* list_ = nullptr;
    wxSearchCtrl* filter_ = nullptr;
    wxStaticText* detail_ = nullptr;
};

class SourcesDialogTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!wxTheApp || !wxTheApp->IsGUI())
            GTEST_SKIP() << "no display: the sources dialog tests were NOT run";
        ocpn_fake::touch = false;
        ocpn_fake::scale = 1.0;
    }
    Look look_;
};

// A flat list: every group is empty, so the list is flat (no header rows); the usable rows come first in CLI
// order, the refused ones after them in CLI order, each with its reason as the Note.
TEST_F(SourcesDialogTest, AFlatListHasUsableRowsFirstThenTheRefusedOnesWithTheirReason) {
    // Unsorted on purpose: refused rows are interleaved with the usable ones.
    Fixture f({rec(kYandex, "Yandex - Satellite", 3395, "projection"), rec("{A}", "Zeta"),
               rec(kEarth, "Earth - GoogleEarth Cache", 4326, "projection"), rec("{B}", "Alpha Sat"),
               rec("{C}", "Webp map", 3857, kWebp)},
              look_);
    EXPECT_EQ(f.rows(), (std::vector<std::string>{"Zeta", "Alpha Sat", "Yandex - Satellite",
                                                  "Earth - GoogleEarth Cache", "Webp map"}));
    EXPECT_EQ(f.note(0), "");
    EXPECT_EQ(f.note(1), "");
    EXPECT_EQ(f.note(2), kProjection3395);
    EXPECT_EQ(f.note(3), kProjection4326);
    EXPECT_EQ(f.note(4), kWebp) << "a verdict is shown as the CLI wrote it";
    // Greyed: the colour halfway between the list's foreground and background; usable rows keep the default.
    const wxColour fg = f.list_->GetForegroundColour(), bg = f.list_->GetBackgroundColour();
    const wxColour half((fg.Red() + bg.Red()) / 2, (fg.Green() + bg.Green()) / 2,
                        (fg.Blue() + bg.Blue()) / 2);
    for (long refused : {2L, 3L, 4L})
        EXPECT_EQ(f.list_->GetItemTextColour(refused), half) << refused;
    EXPECT_NE(f.list_->GetItemTextColour(0), half);
    EXPECT_NE(f.list_->GetItemTextColour(1), half);
    EXPECT_NE(half, fg) << "the test needs a foreground that differs from the background";
}

TEST_F(SourcesDialogTest, TheGroupedListHasBoldHeadersGroupsInFirstSeenOrderAndTheEmptyGroupLastAsOther) {
    // Records (the tsv level carries `group`). Groups are given interleaved and unsorted.
    Fixture f({rec("{1}", "Plain map"), rec(kYandex, "Yandex - Satellite", 3395, "projection", "Sat\\Russia"),
               rec("{2}", "Esri Sat", 3857, "ok", "Sat\\World"),
               rec("{3}", "Kosmo Sat", 3857, "ok", "Sat\\Russia"), rec("{4}", "Other plain"),
               rec("{5}", "Esri Roads", 3857, "ok", "Sat\\World")},
              look_);
    // group "Sat\Russia" first seen first: usable Kosmo, then the refused Yandex; then "Sat\World"; then "".
    EXPECT_EQ(f.rows(),
              (std::vector<std::string>{"Sat / Russia", "Kosmo Sat", "Yandex - Satellite", "Sat / World",
                                        "Esri Sat", "Esri Roads", "Other", "Plain map", "Other plain"}));
    EXPECT_EQ(f.note(2), kProjection3395);
    for (long header : {0L, 3L, 6L}) {
        EXPECT_EQ(f.list_->GetItemData(header), static_cast<wxUIntPtr>(-1)) << "item data -1 marks a header";
        EXPECT_EQ(f.list_->GetItemFont(header).GetWeight(), wxFONTWEIGHT_BOLD);
    }
    const wxFont plain = f.list_->GetItemFont(1); // a source row has no font of its own
    EXPECT_TRUE(!plain.IsOk() || plain.GetWeight() != wxFONTWEIGHT_BOLD);
    // A header shows only while some row of its group matches the filter.
    f.filter("esri");
    EXPECT_EQ(f.rows(), (std::vector<std::string>{"Sat / World", "Esri Sat", "Esri Roads"}));
}

TEST_F(SourcesDialogTest, TheFilterIsAnAsciiCaseInsensitiveSubstringOfNameOrGroup) {
    const std::string cyrillic = "\xD0\x90\xD0\xBB\xD1\x84\xD0\xB0 d'x"; // "Алфа d'x"
    Fixture f({rec("{1}", "OSM Mapnik"), rec("{2}", cyrillic.c_str()),
               rec("{3}", "Kosmo", 3857, "ok", "Sat\\Russia"), rec("{4}", "Caf\xC3\xA9 Map")},
              look_);
    struct Row {
        const char* name;
        std::string text;
        std::vector<std::string> expected;
    };
    // The list is grouped (one record has a group): matching rows keep their headers.
    const std::vector<Row> rows = {
        {"name, other case", "mapNIK", {"Other", "OSM Mapnik"}},
        {"group", "RUSSIA", {"Sat / Russia", "Kosmo"}},
        {"cyrillic name with a quote", "d'x", {"Other", cyrillic}},
        {"ASCII folding only: lower-case Cyrillic does not find the capital",
         "\xD0\xB0\xD0\xBB\xD1\x84\xD0\xB0",
         {}},
        {"accented name", "caf\xC3\xA9", {"Other", "Caf\xC3\xA9 Map"}},
        {"accent is not folded", "CAF\xC3\x89", {}},
        {"no match", "zzz", {}},
        {"cleared", "", {"Sat / Russia", "Kosmo", "Other", "OSM Mapnik", cyrillic, "Caf\xC3\xA9 Map"}},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        f.filter(row.text);
        EXPECT_EQ(f.rows(), row.expected);
    }
}

TEST_F(SourcesDialogTest, OkIsEnabledOnlyForAUsableRowAndActivatingAUsableRowIsOk) {
    Fixture f({rec("{1}", "Plain"), rec(kYandex, "Yandex - Satellite", 3395, "projection", "Sat"),
               rec("{2}", "Grouped", 3857, "ok", "Sat")},
              look_);
    // rows: "Sat" header, "Grouped", "Yandex - Satellite", "Other" header, "Plain"
    ASSERT_EQ(f.rows(), (std::vector<std::string>{"Sat", "Grouped", "Yandex - Satellite", "Other", "Plain"}));
    EXPECT_FALSE(f.okEnabled()) << "nothing selected";
    const int result = f.run([&] {
        f.select(1);
        EXPECT_TRUE(f.okEnabled()) << "a usable row";
        f.select(2);
        EXPECT_FALSE(f.okEnabled()) << "a refused row";
        f.select(0);
        EXPECT_FALSE(f.okEnabled()) << "a header row";
        f.select(4);
        EXPECT_TRUE(f.okEnabled());
        f.select(2);
        f.activate(2); // a refused row: nothing happens, the dialog goes on
        f.select(4);
        f.activate(4); // a usable row = OK
    });
    EXPECT_EQ(result, wxID_OK);
    EXPECT_EQ(f.dlg_.selectedGuid(), "{1}");
    EXPECT_FALSE(f.dlg_.reloadRequested());
}

TEST_F(SourcesDialogTest, OkReturnsTheSelectedGuidCancelReturnsNothingAndReloadAsksForARun) {
    struct Row {
        const char* name;
        int expectedResult;
        std::string guid;
        bool reload;
    };
    for (const Row& row : {Row{"OK", wxID_OK, "{2}", false}, Row{"Cancel", wxID_CANCEL, "", false},
                           Row{"Reload", wxID_RETRY, "", true}}) {
        SCOPED_TRACE(row.name);
        Fixture f({rec("{1}", "First"), rec("{2}", "Second")}, look_);
        const int result = f.run([&] {
            f.select(1);
            if (row.expectedResult == wxID_OK)
                f.clickId(wxID_OK);
            else if (row.expectedResult == wxID_CANCEL)
                f.clickId(wxID_CANCEL);
            else
                f.clickLabel("Reload");
        });
        EXPECT_EQ(result, row.expectedResult);
        EXPECT_EQ(f.dlg_.selectedGuid(), row.guid);
        EXPECT_EQ(f.dlg_.reloadRequested(), row.reload);
    }
}

// shown with Show(), every way of ending calls onDone ONCE with the result
// and the GUID, hides the window and deletes it at the next idle. wx alone would only hide it on Cancel /
// Escape / the close box (proven in settings_dialog_test.cpp).
TEST_F(SourcesDialogTest, AModelessDialogEndsOnOkReloadCancelEscapeAndTheCloseBoxWithOneCall) {
    struct Row {
        const char* name;
        int result;
        std::string guid;
        std::function<void(SourcesDialog*)> act;
    };
    const auto click = [](wxWindow* b) {
        wxCommandEvent e(wxEVT_BUTTON, b->GetId());
        e.SetEventObject(b);
        b->GetEventHandler()->ProcessEvent(e);
    };
    const std::vector<Row> rows = {
        {"OK", wxID_OK, "{2}", [&](SourcesDialog* d) { click(d->FindWindow(wxID_OK)); }},
        {"Reload", wxID_RETRY, "",
         [&](SourcesDialog* d) { click(wxWindow::FindWindowByLabel("Reload", d)); }},
        {"Cancel", wxID_CANCEL, "", [&](SourcesDialog* d) { click(d->FindWindow(wxID_CANCEL)); }},
        {"Escape", wxID_CANCEL, "",
         [](SourcesDialog* d) {
             wxKeyEvent e(wxEVT_CHAR_HOOK);
             e.m_keyCode = WXK_ESCAPE;
             e.SetEventObject(d);
             d->GetEventHandler()->ProcessEvent(e);
         }},
        {"close box", wxID_CANCEL, "", [](SourcesDialog* d) { d->Close(); }},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        std::vector<cg::SourceRecord> records = {rec("{1}", "First"), rec("{2}", "Second")};
        auto* d = new SourcesDialog(nullptr, records, look_);
        wxWeakRef<SourcesDialog> ref(d);
        records
            .clear(); // the page's list may be replaced while the dialog is open: the dialog has its own copy
        std::vector<wxListCtrl*> lists;
        collect(d, lists);
        ASSERT_EQ(lists.at(0)->GetItemCount(), 2);
        int calls = 0, result = 0;
        std::string guid = "<unset>";
        d->onDone = [&](int r, std::string g) {
            ++calls;
            result = r;
            guid = g;
        };
        d->Show();
        lists[0]->SetItemState(1, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
        row.act(d);
        EXPECT_EQ(calls, 1);
        EXPECT_EQ(result, row.result);
        EXPECT_EQ(guid, row.guid);
        EXPECT_FALSE(d->IsShown()) << "hidden before the callback";
        wxTheApp->ProcessIdle();
        EXPECT_FALSE(ref) << "deleted";
        EXPECT_EQ(calls, 1);
    }
}

// what a source is for (source-descriptions.conf): Note, detail line, search.
TEST_F(SourcesDialogTest,
       ThePurposeIsTheNoteOfAUsableRowTheDetailLineAndSearchedButARefusedRowKeepsItsReason) {
    const std::string dash = "\xE2\x80\x94";
    const cg::SourceDescriptions purposes = {
        {"{1}", "Mooring and anchorage detail"},
        {"{2}", "Caf\xC3\xA9 \xD0\x9F\xD0\xBB\xD0\xB0\xD0\xBD d'x"},
        {kYandex, "Satellite view of anchorages and reefs"}, // a refused row with a purpose
        {"{ABC}", "Upper-cased key"},
        {"{9}", "Purpose of a source that is not listed"},
    };
    Fixture f({rec("{1}", "Plain", 3857, "ok", "Sat"), rec("{2}", "Second", 3857, "ok", "Sat"),
               rec(kYandex, "Yandex", 3395, "projection", "Sat"), rec("{abc}", "Lower", 3857, "ok", "Sat"),
               rec("{4}", "Nothing", 3857, "ok", "Sat")},
              look_, purposes);
    // rows: 0 "Sat" header, 1 Plain, 2 Second, 3 Lower, 4 Nothing, 5 Yandex
    ASSERT_EQ(f.rows(), (std::vector<std::string>{"Sat", "Plain", "Second", "Lower", "Nothing", "Yandex"}));
    EXPECT_EQ(f.note(1), "Mooring and anchorage detail");
    EXPECT_EQ(f.note(2), purposes.at("{2}"));
    EXPECT_EQ(f.note(3), "Upper-cased key") << "the GUID is looked up upper-cased";
    EXPECT_EQ(f.note(4), "") << "no purpose: empty";
    EXPECT_EQ(f.note(5), kProjection3395) << "a refused row keeps its reason";
    EXPECT_EQ(f.detail(), "") << "no selection";
    struct Row {
        long row;
        std::string detail;
    };
    const std::vector<Row> rows = {
        {1, "Mooring and anchorage detail"},
        {4, ""},
        {5, std::string(kProjection3395) + " " + dash + " Satellite view of anchorages and reefs"},
        {0, ""},
        {2, purposes.at("{2}")},
    };
    for (const Row& r : rows) {
        SCOPED_TRACE(r.row);
        f.select(r.row);
        EXPECT_EQ(f.detail(), r.detail);
    }
    f.list_->SetItemState(2, 0, wxLIST_STATE_SELECTED);
    EXPECT_EQ(f.detail(), "") << "deselected";
    // search: a word only in a purpose lists the row; a word only in the refusal text does not
    f.filter("ANCHORAGE");
    EXPECT_EQ(f.rows(), (std::vector<std::string>{"Sat", "Plain", "Yandex"}));
    f.filter("projection");
    EXPECT_EQ(f.rows(), (std::vector<std::string>{})) << "the refusal text is not searched";
    f.filter("\xD0\x9F\xD0\xBB\xD0\xB0\xD0\xBD");
    EXPECT_EQ(f.rows(), (std::vector<std::string>{"Sat", "Second"})) << "a Cyrillic purpose";
    f.filter("");
    EXPECT_EQ(f.rows().size(), 6u);
    // a refused row without a purpose: the refusal alone
    Fixture g({rec(kYandex, "Yandex", 3395)}, look_, {});
    g.select(0);
    EXPECT_EQ(g.detail(), kProjection3395);
}

TEST_F(SourcesDialogTest, ALongListFitsEightHundredByFourEightyAndTouchMakesTheButtonsHigh) {
    std::vector<cg::SourceRecord> many;
    for (int i = 0; i < 60; ++i)
        many.push_back(
            rec("{X}", (std::string("Source ") + std::to_string(i)).c_str(), i % 7 == 0 ? 3395 : 3857));
    // the longest real purpose (the NOAA chart row, 220 characters) is selected in the detail line
    const std::string longest =
        "US chart picture for planning. For official US charts with updates, use OpenCPN's Chart Downloader "
        "(Options \xE2\x86\x92 Charts \xE2\x86\x92 Chart Downloader). This NOAA source is a picture of the "
        "same charts for viewing next to other layers.";
    cg::SourceDescriptions purposes = {{"{X}", longest}};
    {
        Fixture f(many, look_, purposes);
        f.select(0);
        EXPECT_EQ(f.detail().find("US chart picture"), 0u);
        f.dlg_.Layout();
        EXPECT_GE(f.detail_->GetSize().y, f.detail_->GetBestSize().y) << "the wrapped text is not cut";
        EXPECT_LE(f.dlg_.GetSize().x, 800);
        EXPECT_LE(f.dlg_.GetSize().y, 480);
    }
    ocpn_fake::touch = true;
    ocpn_fake::scale = 1.0;
    Fixture f(many, look_, purposes);
    f.select(0);
    f.dlg_.Layout();
    EXPECT_GE(f.detail_->GetSize().y, f.detail_->GetBestSize().y) << "touch: the wrapped text is not cut";
    EXPECT_LE(f.dlg_.GetSize().x, 800);
    EXPECT_LE(f.dlg_.GetSize().y, 480);
    std::vector<wxButton*> buttons;
    collect(&f.dlg_, buttons);
    ASSERT_EQ(buttons.size(), 3u); // Reload, Cancel, OK
    for (wxButton* b : buttons)
        EXPECT_GE(b->GetSize().y, 48) << u(b->GetLabel());
    EXPECT_GE(f.filter_->GetSize().y, 48);
}

} // namespace
