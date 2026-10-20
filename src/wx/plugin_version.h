// SPDX-License-Identifier: GPL-3.0-or-later
// The plugin version, calendar-based YYYY.MM.N taken from the CMake project
// version. OpenCPN 5.10.2 parses it with sscanf("%d.%d.%d.%d") (semantic_vers.cpp:51) and reads major / minor
// / patch through the plugin class.
#pragma once

namespace cg {

inline constexpr int kPluginVersionMajor = CG_VERSION_MAJOR;
inline constexpr int kPluginVersionMinor = CG_VERSION_MINOR;
inline constexpr int kPluginVersionPatch = CG_VERSION_PATCH;
inline constexpr const char* kPluginVersionString = CG_VERSION_STRING;

} // namespace cg
