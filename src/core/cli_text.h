// SPDX-License-Identifier: GPL-3.0-or-later
// The CLI's version line and the minimum supported version. Never throws; no
// user-visible text; no I/O.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace cg {

struct VersionNumber {
    int major = 0, minor = 0, patch = 0;
};
struct CliVersion {
    std::string program;
    VersionNumber number;
    std::string commit; // may be empty
};

// The one version the plugin supports at least (calendar versions YYYY.MM.N).
inline constexpr VersionNumber kMinimumCli{2026, 10, 9};

// First line of `--version` stdout (text up to the first '\n', a trailing '\r' and blanks dropped):
// <program> ' ' <major> '.' <minor> '.' <patch> [suffix: any non-blank chars] [' (' <commit> ')'] [anything].
// program = the first blank-free token (never checked); numbers = 1..9 ASCII digits each. Else nullopt.
std::optional<CliVersion> parseVersionLine(std::string_view stdoutText);
bool atLeast(const VersionNumber& v, const VersionNumber& min); // lexicographic (major, minor, patch)
// true = v set and atLeast(v->number, kMinimumCli).
bool supportedCli(const std::optional<CliVersion>& v);

} // namespace cg
