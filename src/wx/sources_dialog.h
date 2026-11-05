// SPDX-License-Identifier: GPL-3.0-or-later
// "More sources..." of the New chart page: a filter, the
// sources grouped by `group`, refused rows greyed with their reason and not selectable. Modeless (the plugin
// shows it with Show(); ShowModal still works); GUI thread.
#pragma once

#include "cli_records.h"
#include "plugin_conf.h"

#include <wx/dialog.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

class Look;
class wxButton;
class wxListCtrl;
class wxSearchCtrl;
class wxStaticText;

class SourcesDialog : public wxDialog {
public:
    // The dialog keeps its own copy of `sources` and `descriptions` (the page's may be replaced while it is
    // open). descriptions: what each source is for (key: GUID upper-cased): the Note of a usable
    // row, the detail line and a part of what the filter searches.
    SourcesDialog(wxWindow* parent, const std::vector<cg::SourceRecord>& sources, const Look& look,
                  const cg::SourceDescriptions& descriptions = {});

    std::string selectedGuid() const;                // empty unless OK was pressed on a usable row
    bool reloadRequested() const { return reload_; } // "Reload" pressed: the dialog ended with wxID_RETRY

    // Modeless use: called once after the window is hidden and destroyed: wxID_OK with
    // the picked GUID, wxID_RETRY for Reload (guid ""), wxID_CANCEL (Cancel, Escape, close box) with "". Not
    // called when the dialog is ShowModal()'d (EndModal returns the code) or deleted with its parent.
    std::function<void(int result, std::string guid)> onDone;

private:
    void fillList();
    void updateDetail(); // the line under the list: the selected row's refusal and / or purpose
    std::string purposeOf(const cg::SourceRecord& r) const; // "" when the source has none
    const cg::SourceRecord* selectedUsable() const;         // nullptr: nothing, a header or a refused row
    void accept();
    void finish(int rc); // modal: EndModal(rc); modeless: Hide, take onDone, Destroy, call it

    std::vector<cg::SourceRecord> sources_;
    cg::SourceDescriptions descriptions_;
    std::vector<std::size_t> order_; // cg::sourcesDialogOrder(sources_)
    bool grouped_ = false;           // some record has a group: header rows (none: a flat list)
    wxSearchCtrl* filter_ = nullptr;
    wxListCtrl* list_ = nullptr;
    wxStaticText* detail_ = nullptr;
    wxButton* ok_ = nullptr;
    std::string picked_;
    bool reload_ = false;
};
