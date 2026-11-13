// SPDX-License-Identifier: GPL-3.0-or-later
#include "open_folder.h"

#include "chart_folder.h"
#include "plugin_log.h"

#include <wx/intl.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <wx/process.h>
#include <wx/utils.h>

#include <utility>

namespace cg {
namespace {

// Reports the end of one xdg-open; wxProcess deletes itself once OnTerminate has returned.
class OpenProcess : public wxProcess {
public:
    OpenProcess(wxString folder, std::function<void(const wxString&)> onFailed)
        : folder_(std::move(folder)), onFailed_(std::move(onFailed)) {}

    void OnTerminate(int, int status) override {
        if (status != 0) {
            logWarning(wxString::Format("xdg-open exited with %d for %s", status, folder_));
            onFailed_(folder_);
        }
        delete this;
    }

private:
    wxString folder_;
    std::function<void(const wxString&)> onFailed_;
};

} // namespace

void openFolderInFileManager(const wxString& folder, std::function<void(const wxString&)> onFailed) {
    if (!sandboxProbe()().flatpak) {
        if (!wxLaunchDefaultApplication(folder))
            logLine(wxString::Format("cannot open %s", folder));
        return;
    }
    if (!onFailed)
        onFailed = [](const wxString& f) {
            wxMessageBox(cannotOpenFolderText(f), _("Chart Grabber"), wxOK | wxICON_INFORMATION);
        };
    auto* process = new OpenProcess(folder, onFailed);
    const wchar_t* argv[] = {L"xdg-open", folder.wc_str(), nullptr};
    long pid;
    {
        wxLogNull quiet; // a failure goes to onFailed, not to a wx dialog
        pid = wxExecute(argv, wxEXEC_ASYNC, process);
    }
    if (pid == 0) {
        delete process;
        logWarning(wxString::Format("cannot start xdg-open for %s", folder));
        onFailed(folder);
    }
}

wxString cannotOpenFolderText(const wxString& folder) {
    return wxString::Format(_("Chart Grabber could not open a file manager window. The folder is:\n%s"),
                            folder);
}

} // namespace cg
