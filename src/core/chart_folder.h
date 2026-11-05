// SPDX-License-Identifier: GPL-3.0-or-later
// The chart-folder rules. No wx; std only.
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace cg {

// What the sandbox lets the plugin write. Outside Flatpak only `flatpak` (false) matters.
struct Sandbox {
    bool flatpak = false;
    std::string home;                 // $HOME
    std::vector<std::string> granted; // absolute folders of [Context] filesystems= (read-write ones)
};

// The folders of a `filesystems=` value that are read-write and absolute: "home" -> home, "host" -> "/",
// "/abs[:rw|:create]" as is; "~/x" -> home/x; ":ro", "!x" and the xdg-* names are skipped (ponytail: an xdg-*
// grant outside home is refused; add them when a sailor needs one).
std::vector<std::string> flatpakGranted(std::string_view infoText, std::string_view home);

// Reads `infoPath` (/.flatpak-info): present -> flatpak = true. A missing file gives flatpak = false.
Sandbox detectSandbox(const std::string& infoPath = "/.flatpak-info");

// The one injectable detection: tests assign it; the default is detectSandbox().
std::function<Sandbox()>& sandboxProbe();

enum class FolderVerdict { Ok, CannotWrite, FlatpakOutsideHome };

// `exists` and `writable` are the caller's facts about `folder`. A missing folder is Ok only for the default
// chart folder (created at the first download). Under Flatpak a folder neither under home nor under a granted
// folder is FlatpakOutsideHome (checked first). Lexical: symbolic links are not resolved.
FolderVerdict checkChartFolder(const std::string& folder, bool isDefault, bool exists, bool writable,
                               const Sandbox& sandbox);

} // namespace cg
