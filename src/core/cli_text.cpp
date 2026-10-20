// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli_text.h"

namespace cg {
namespace {

bool isBlank(char c) {
    return c == ' ' || c == '\t';
}

// Whole text is ASCII digits (1..maxDigits), no sign.
bool digits(std::string_view s, int& out, std::size_t maxDigits = 9) {
    if (s.empty() || s.size() > maxDigits) {
        return false;
    }
    int v = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') {
            return false;
        }
        v = v * 10 + (c - '0');
    }
    out = v;
    return true;
}

} // namespace

std::optional<CliVersion> parseVersionLine(std::string_view text) {
    std::string_view line = text.substr(0, text.find('\n'));
    while (!line.empty() && (isBlank(line.back()) || line.back() == '\r')) {
        line.remove_suffix(1);
    }
    std::size_t i = 0;
    while (i < line.size() && isBlank(line[i])) {
        ++i;
    }
    const std::size_t pb = i;
    while (i < line.size() && !isBlank(line[i])) {
        ++i;
    }
    CliVersion v;
    v.program = std::string(line.substr(pb, i - pb));
    if (v.program.empty()) {
        return std::nullopt;
    }
    while (i < line.size() && isBlank(line[i])) {
        ++i;
    }
    const std::size_t vb = i;
    while (i < line.size() && !isBlank(line[i])) {
        ++i;
    }
    std::string_view token = line.substr(vb, i - vb);
    int parts[3] = {};
    for (int k = 0; k < 3; ++k) {
        std::size_t e = 0;
        while (e < token.size() && token[e] >= '0' && token[e] <= '9') {
            ++e;
        }
        if (!digits(token.substr(0, e), parts[k])) {
            return std::nullopt;
        }
        token.remove_prefix(e);
        if (k < 2) {
            if (token.empty() || token.front() != '.') {
                return std::nullopt;
            }
            token.remove_prefix(1);
        }
    }
    v.number = {parts[0], parts[1], parts[2]};
    while (i < line.size() && isBlank(line[i])) {
        ++i;
    }
    if (i < line.size() && line[i] == '(') {
        const std::size_t close = line.find(')', i);
        if (close != std::string_view::npos) {
            v.commit = std::string(line.substr(i + 1, close - i - 1));
        }
    }
    return v;
}

bool atLeast(const VersionNumber& v, const VersionNumber& min) {
    if (v.major != min.major) {
        return v.major > min.major;
    }
    if (v.minor != min.minor) {
        return v.minor > min.minor;
    }
    return v.patch >= min.patch;
}

bool supportedCli(const std::optional<CliVersion>& v) {
    return v && atLeast(v->number, kMinimumCli);
}

} // namespace cg
