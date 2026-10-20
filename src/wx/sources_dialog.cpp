// SPDX-License-Identifier: GPL-3.0-or-later
#include "sources_dialog.h"

#include "guard.h"
#include "look.h"
#include "messages.h"
#include "wx_util.h"

#include <wx/button.h>
#include <wx/intl.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>
#include <wx/srchctrl.h>

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <utility>

namespace {

constexpr long kHeaderRow = -1; // item data of a group header row

std::string asciiLower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Header text: `\` shown as " / ", the empty group "Other" .
wxString groupTitle(const std::string& group) {
    if (group.empty())
        return _("Other");
    wxString t = cg::fromUtf8(group);
    t.Replace("\\", " / ");
    return t;
}

} // namespace

SourcesDialog::SourcesDialog(wxWindow* parent, const std::vector<cg::SourceRecord>& sources, const Look& look)
    : wxDialog(parent, wxID_ANY, _("More sources")), sources_(sources),
      order_(cg::sourcesDialogOrder(sources)) {
    grouped_ = std::any_of(sources.begin(), sources.end(),
                           [](const cg::SourceRecord& r) { return !r.group.empty(); });
    filter_ = new wxSearchCtrl(this, wxID_ANY);
    list_ =
        new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(690, 230), wxLC_REPORT | wxLC_SINGLE_SEL);
    list_->InsertColumn(0, _("Source"), wxLIST_FORMAT_LEFT, 270);
    list_->InsertColumn(1, _("Note"), wxLIST_FORMAT_LEFT, 400);
    auto* reload = new wxButton(this, wxID_ANY, _("Reload"));
    auto* cancel = new wxButton(this, wxID_CANCEL, _("Cancel"));
    ok_ = new wxButton(this, wxID_OK, _("OK"));
    ok_->Enable(false);

    if (const int minHeight = look.minControlHeight(); minHeight > 0)
        for (wxWindow* w : std::initializer_list<wxWindow*>{filter_, reload, cancel, ok_})
            w->SetMinSize(wxSize(-1, minHeight));

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->Add(reload, 0);
    buttons->AddStretchSpacer(1);
    buttons->Add(cancel, 0, wxRIGHT, 6);
    buttons->Add(ok_, 0);
    auto* column = new wxBoxSizer(wxVERTICAL);
    column->Add(filter_, 0, wxEXPAND | wxALL, 8);
    column->Add(list_, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);
    column->Add(buttons, 0, wxEXPAND | wxALL, 8);
    SetSizerAndFit(column);

    filter_->Bind(wxEVT_TEXT,
                  [this](wxCommandEvent&) { cg::guarded("sources filter", [&] { fillList(); }); });
    auto changed = [this](wxListEvent&) {
        cg::guarded("sources selection", [&] { ok_->Enable(selectedUsable() != nullptr); });
    };
    list_->Bind(wxEVT_LIST_ITEM_SELECTED, changed);
    list_->Bind(wxEVT_LIST_ITEM_DESELECTED, changed);
    list_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) {
        cg::guarded("sources activate", [&] {
            if (selectedUsable())
                accept();
        });
    });
    ok_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { cg::guarded("sources OK", [&] { accept(); }); });
    reload->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        cg::guarded("sources reload", [&] {
            reload_ = true;
            finish(wxID_RETRY);
        });
    });
    // wx's default EndDialog only hides a modeless dialog: Cancel, Escape and the close box end it here.
    Bind(
        wxEVT_BUTTON,
        [this](wxCommandEvent&) { cg::guarded("sources cancel", [&] { finish(wxID_CANCEL); }); },
        wxID_CANCEL);
    Bind(wxEVT_CLOSE_WINDOW,
         [this](wxCloseEvent&) { cg::guarded("sources close", [&] { finish(wxID_CANCEL); }); });

    look.apply(this);
    fillList(); // after apply: the greyed colour is taken from the list's own colours
}

// ASCII case-insensitive substring of name or group; rows in sourcesDialogOrder order; header rows
// only when some record has a group, before each group that has a matching row.
void SourcesDialog::fillList() {
    list_->DeleteAllItems();
    const std::string needle = asciiLower(cg::toUtf8(filter_->GetValue()));
    const wxColour fg = list_->GetForegroundColour(), bg = list_->GetBackgroundColour();
    const wxColour grey((fg.Red() + bg.Red()) / 2, (fg.Green() + bg.Green()) / 2,
                        (fg.Blue() + bg.Blue()) / 2);
    wxFont bold = list_->GetFont();
    bold.SetWeight(wxFONTWEIGHT_BOLD);
    bool first = true;
    std::string group;
    for (const std::size_t i : order_) {
        const cg::SourceRecord& r = sources_[i];
        if (!needle.empty() && asciiLower(r.name).find(needle) == std::string::npos &&
            asciiLower(r.group).find(needle) == std::string::npos)
            continue;
        if (grouped_ && (first || r.group != group)) {
            const long h = list_->InsertItem(list_->GetItemCount(), groupTitle(r.group));
            list_->SetItemData(h, kHeaderRow);
            list_->SetItemFont(h, bold);
            group = r.group;
            first = false;
        }
        const long row = list_->InsertItem(list_->GetItemCount(), cg::fromUtf8(r.name));
        list_->SetItemData(row, static_cast<long>(i));
        const cg::SourceRefusal refusal = cg::sourceRefusal(r);
        if (refusal.kind != cg::SourceRefusal::Kind::None) {
            list_->SetItem(row, 1, sourceRefusalText(refusal));
            list_->SetItemTextColour(row, grey);
        }
    }
    ok_->Enable(selectedUsable() != nullptr);
}

const cg::SourceRecord* SourcesDialog::selectedUsable() const {
    const long item = list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (item < 0)
        return nullptr;
    const long data = list_->GetItemData(item);
    if (data == kHeaderRow || std::size_t(data) >= sources_.size() || !sources_[data].usable())
        return nullptr;
    return &sources_[data];
}

void SourcesDialog::accept() {
    if (const cg::SourceRecord* r = selectedUsable()) { // unreachable otherwise: OK is disabled, see changed
        picked_ = r->guid;
        finish(wxID_OK);
    }
}

void SourcesDialog::finish(int rc) {
    if (IsModal()) {
        EndModal(rc);
        return;
    }
    const auto done = std::move(onDone); // empty on a second event before the deferred Destroy
    onDone = nullptr;
    const std::string guid = rc == wxID_OK ? picked_ : std::string();
    Hide();
    Destroy(); // deferred to idle: this handler is still running
    if (done)
        done(rc, guid);
}

std::string SourcesDialog::selectedGuid() const {
    return picked_;
}
