// SPDX-License-Identifier: GPL-3.0-or-later
// wxString <-> UTF-8 std::string, for the boundary between src/wx and src/core.
#pragma once

#include <wx/string.h>

#include <string>
#include <string_view>

namespace cg {

inline std::string toUtf8(const wxString& s) {
    const wxScopedCharBuffer buf = s.utf8_str();
    return std::string(buf.data(), buf.length());
}

inline wxString fromUtf8(std::string_view s) {
    return wxString::FromUTF8(s.data(), s.size());
}

} // namespace cg
