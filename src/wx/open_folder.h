// SPDX-License-Identifier: GPL-3.0-or-later
// Opens a folder in the desktop file manager.
#pragma once

#include <wx/string.h>

#include <functional>

namespace cg {

// Under Flatpak the request goes through xdg-open and the host portal, and wxLaunchDefaultApplication cannot
// see it fail (it only starts the program). There xdg-open is run here and `onFailed(folder)` is called on
// the main loop when it cannot start or exits with an error (default: a message box with the folder).
// Elsewhere wxLaunchDefaultApplication is used as it always was and a failure is only logged.
void openFolderInFileManager(const wxString& folder,
                             std::function<void(const wxString& folder)> onFailed = {});

// Detaches every xdg-open still running and drops the pending failure calls: nothing of the plugin runs
// when one of them ends later. Called from DeInit.
void shutdownOpenFolder();

// The sailor's sentence when no file manager window could be opened.
wxString cannotOpenFolderText(const wxString& folder);

} // namespace cg
