// SPDX-License-Identifier: GPL-3.0-or-later
#include "open_folder.h"

#include "chart_folder.h"
#include "plugin_log.h"

#include <wx/event.h>
#include <wx/intl.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <wx/process.h>
#include <wx/utils.h>

#include <map>
#include <memory>
#include <utility>

namespace cg {
namespace {

#ifndef __WXMSW__
// The owner of every running xdg-open: wx sends the end of a process to its parent handler, which owns the
// process then. Destroyed in shutdownOpenFolder, so no plugin code is called after the library is unloaded.
class OpenHandler : public wxEvtHandler {
public:
    OpenHandler() { Bind(wxEVT_END_PROCESS, &OpenHandler::onEnd, this); }

    // The process is told apart by its event id (the event carries no object).
    wxProcess* make(wxString folder, std::function<void(const wxString&)> onFailed, int* id) {
        *id = ++lastId_;
        auto* process = new wxProcess(this, *id);
        live_[*id] = {process, std::move(folder), std::move(onFailed)};
        return process;
    }
    void forget(int id) { live_.erase(id); }
    // wx deletes a detached process itself at its end, without any event.
    void detachAll() {
        for (auto& entry : live_)
            entry.second.process->Detach();
        live_.clear();
    }

private:
    struct Entry {
        wxProcess* process;
        wxString folder;
        std::function<void(const wxString&)> onFailed;
    };

    void onEnd(wxProcessEvent& event) {
        const auto it = live_.find(event.GetId());
        if (it == live_.end())
            return;
        Entry entry = std::move(it->second);
        live_.erase(it);
        delete entry.process;
        if (event.GetExitCode() != 0) {
            logWarning(wxString::Format("xdg-open exited with %d for %s", event.GetExitCode(), entry.folder));
            // From the event loop, not inside wx's process-end callback (the failure box is modal).
            CallAfter([entry] { entry.onFailed(entry.folder); });
        }
    }

    std::map<int, Entry> live_;
    int lastId_ = 0;
};

std::unique_ptr<OpenHandler> g_handler;
#endif // Flatpak exists on Linux only; Windows builds no wxProcess here

} // namespace

void openFolderInFileManager(const wxString& folder, std::function<void(const wxString&)> onFailed) {
#ifdef __WXMSW__
    (void)onFailed;
    if (!wxLaunchDefaultApplication(folder))
        logLine(wxString::Format("cannot open %s", folder));
#else
    if (!sandboxProbe()().flatpak) {
        if (!wxLaunchDefaultApplication(folder))
            logLine(wxString::Format("cannot open %s", folder));
        return;
    }
    if (!onFailed)
        onFailed = [](const wxString& f) {
            wxMessageBox(cannotOpenFolderText(f), _("Chart Grabber"), wxOK | wxICON_INFORMATION);
        };
    if (!g_handler)
        g_handler = std::make_unique<OpenHandler>();
    int id;
    wxProcess* process = g_handler->make(folder, onFailed, &id); // before wxExecute: it may report the end
    const wchar_t* argv[] = {L"xdg-open", folder.wc_str(), nullptr};
    long pid;
    {
        wxLogNull quiet; // a failure goes to onFailed, not to a wx dialog
        pid = wxExecute(argv, wxEXEC_ASYNC, process);
    }
    if (pid == 0) {
        g_handler->forget(id);
        delete process;
        logWarning(wxString::Format("cannot start xdg-open for %s", folder));
        onFailed(folder);
    }
#endif
}

void shutdownOpenFolder() {
#ifndef __WXMSW__
    if (g_handler)
        g_handler->detachAll();
    g_handler.reset(); // its queued CallAfter events die with it
#endif
}

wxString cannotOpenFolderText(const wxString& folder) {
    return wxString::Format(_("Chart Grabber could not open a file manager window. The folder is:\n%s"),
                            folder);
}

} // namespace cg
