// SPDX-License-Identifier: GPL-3.0-or-later
// Rules that differ between Linux and Windows and need no window. Every
// function takes an Os argument that defaults to the host, so the Windows rules are unit-tested on Linux. No
// wx.
#pragma once

#include <ctime>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cg {

enum class Os { Posix, Windows };
#ifdef _WIN32
inline constexpr Os kHostOs = Os::Windows;
#else
inline constexpr Os kHostOs = Os::Posix;
#endif

// One command line that CommandLineToArgvW / the MS C runtime splits back into `argv` (UTF-8 in, UTF-8 out;
// the caller converts to UTF-16). Arguments joined by one space. An argument is quoted ("...") when it is
// empty or holds a space or a tab. Inside an argument: a run of n backslashes followed by '"' becomes 2n
// backslashes + \"; a run of n backslashes at the END of a quoted argument becomes 2n backslashes (before the
// closing quote); any other backslash is copied. Equal to Python's subprocess.list2cmdline.
std::string windowsCommandLine(const std::vector<std::string>& argv);

// Posix: '/' first. Windows: "X:\" or "X:/" (X an ASCII letter), or "\\" / "//" first (UNC). "C:foo", "\foo",
// "foo" are not absolute. Empty -> false.
bool isAbsolutePath(std::string_view path, Os os = kHostOs);
// dir + one separator + name: no separator added when dir is empty or already ends in one ('/' Posix; '/' or
// '\' Windows); the added separator is '/' (Posix) or '\' (Windows).
std::string joinPath(std::string_view dir, std::string_view name, Os os = kHostOs);
// Windows: every '/' becomes '\' (the CLI prints its paths the Qt way); Posix: unchanged, a '\' stays.
std::string nativeSeparators(std::string_view path, Os os = kHostOs);
// The part before the last separator ('/' Posix; '/' or '\' Windows). The separator is KEPT when it
// is the root: "/x" -> "/", "C:\x" -> "C:\"; no separator -> "".
std::string parentDir(std::string_view path, Os os = kHostOs);
// Equal after removing ONE trailing separator from each (not from a root "/" or "C:\"). Windows also maps
// '/' to '\' and compares ASCII letters case-insensitively (non-ASCII compared as bytes: ponytail, NTFS
// folds more).
bool sameDir(std::string_view a, std::string_view b, Os os = kHostOs);
// Windows paths only: the volume of each ("X:" letter case-insensitive, or "\\server\share" /
// "//server/share" ASCII case-insensitive, separators mapped) is non-empty and equal. Used for the disk check
// of section 7.4 on Windows.
bool sameVolume(std::string_view a, std::string_view b);
// A program file: a regular file AND (Posix: access(X_OK) == 0; Windows: the name ends in ".exe",
// ASCII case-insensitive; no PATHEXT). The Posix branch is compiled only without _WIN32 (unistd.h).
bool isProgramFile(const std::string& path, Os os = kHostOs);
// True when the name ends in ".exe" (ASCII case-insensitive). Used by findOnPath to add the suffix once.
bool hasExeSuffix(std::string_view name);
// The automatic places of the CLI, in order, before PATH (section 8.2, 18.3). Posix:
// joinPath(joinPath(pluginDataDir, "cli"), kDefaultCliName) when pluginDataDir is not empty (the CLI bundled
// in the tarball, ), then "/usr/bin/" + kDefaultCliName (programFiles ignored). Windows:
// joinPath(joinPath(pluginDataDir, "cli"), exe) when pluginDataDir is not empty, then
// joinPath(joinPath(programFiles, kCliWindowsFolder), exe) when programFiles is not empty; exe =
// kDefaultCliName + ".exe".
std::vector<std::string> defaultCliPaths(Os os, std::string_view pluginDataDir,
                                         std::string_view programFiles);
// UTF-8 std::string <-> std::filesystem::path through char8_t (MSVC would read a narrow string in the ANSI
// code page: a Cyrillic user name breaks). Identity of the bytes on Linux.
std::filesystem::path fsPath(std::string_view utf8);
std::string utf8(const std::filesystem::path& p);
// Local broken-down time: localtime_r (Posix) / localtime_s (_WIN32).
std::tm localTime(std::time_t t);

} // namespace cg
