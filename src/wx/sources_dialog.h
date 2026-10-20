// SPDX-License-Identifier: GPL-3.0-or-later
// "More sources..." of the New chart page : a filter, the
// sources grouped by `group`, refused rows greyed with their reason and not selectable. Modeless (the plugin
// shows it with Show, ; ShowModal still works); GUI thread.
#pragma once

#include "cli_records.h"

#include <wx/dialog.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

class Look;
class wxButton;
class wxListCtrl;
class wxSearchCtrl;

class SourcesDialog : public wxDialog {
public:
    // The dialog keeps its own copy of `sources` (the page's list may be replaced while it is open).
    SourcesDialog(wxWindow* parent, const std::vector<cg::SourceRecord>& sources, const Look& look);

    std::string selectedGuid() const;                // empty unless OK was pressed on a usable row
    bool reloadRequested() const { return reload_; } // "Reload" pressed: the dialog ended with wxID_RETRY

    // Modeless use: called once after the window is hidden and destroyed : wxID_OK with
    // the picked GUID, wxID_RETRY for Reload (guid ""), wxID_CANCEL (Cancel, Escape, close box) with "". Not
    // called when the dialog is ShowModal'd (EndModal returns the code) or deleted with its parent.
    std::function<void(int result, std::string guid)> onDone;

private:
    void fillList();
    const cg::SourceRecord* selectedUsable() const; // nullptr: nothing, a header or a refused row
    void accept();
    void finish(int rc); // modal: EndModal(rc); modeless: Hide, take onDone, Destroy, call it

    std::vector<cg::SourceRecord> sources_;
    std::vector<std::size_t> order_; // cg::sourcesDialogOrder(sources_)
    bool grouped_ = false;           // some record has a group: header rows (none: a flat list)
    wxSearchCtrl* filter_ = nullptr;
    wxListCtrl* list_ = nullptr;
    wxButton* ok_ = nullptr;
    std::string picked_;
    bool reload_ = false;
};
