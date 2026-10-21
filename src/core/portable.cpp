// SPDX-License-Identifier: GPL-3.0-or-later
#include "portable.h"

#include "settings.h"

#include <cstddef>
#include <ctime>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace cg {
namespace {

bool isSep(char c, Os os) {
    return c == '/' || (os == Os::Windows && c == '\\');
}

char lowerAscii(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool isAsciiLetter(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

// Windows key of a directory: '/' -> '\', ASCII lower case, one trailing separator removed (not from a root).
std::string dirKey(std::string_view path, Os os) {
    std::string out(path);
    if (os == Os::Windows) {
        for (char& c : out) {
            c = lowerAscii(c == '/' ? '\\' : c);
        }
    }
    const bool root = out.size() == 1 || (os == Os::Windows && out.size() == 3 && out[1] == ':');
    if (!out.empty() && isSep(out.back(), os) && !root) {
        out.pop_back();
    }
    return out;
}

// "c:" for a drive path, "\\server\share" for a UNC path (ASCII lower case); empty when neither.
std::string volumeOf(std::string_view path) {
    if (path.size() >= 2 && isAsciiLetter(path[0]) && path[1] == ':') {
        return {lowerAscii(path[0]), ':'};
    }
    if (!path.starts_with("\\\\") && !path.starts_with("//")) {
        return {};
    }
    const std::string_view rest = path.substr(2);
    const auto serverEnd = rest.find_first_of("/\\");
    if (serverEnd == std::string_view::npos || serverEnd == 0) {
        return {};
    }
    const std::string_view afterServer = rest.substr(serverEnd + 1);
    const std::string_view share = afterServer.substr(0, afterServer.find_first_of("/\\"));
    if (share.empty()) {
        return {};
    }
    std::string out = "\\\\";
    for (const char c : rest.substr(0, serverEnd)) {
        out += lowerAscii(c);
    }
    out += '\\';
    for (const char c : share) {
        out += lowerAscii(c);
    }
    return out;
}

} // namespace

std::string windowsCommandLine(const std::vector<std::string>& argv) {
    std::string out;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        const std::string& arg = argv[i];
        if (i > 0) {
            out += ' ';
        }
        const bool quote = arg.empty() || arg.find_first_of(" \t") != std::string::npos;
        if (quote) {
            out += '"';
        }
        std::size_t backslashes = 0;
        for (const char c : arg) {
            if (c == '\\') {
                ++backslashes;
            } else if (c == '"') {
                out.append(backslashes * 2 + 1, '\\'); // 2n backslashes, then \"
                out += '"';
                backslashes = 0;
            } else {
                out.append(backslashes, '\\');
                out += c;
                backslashes = 0;
            }
        }
        out.append(quote ? backslashes * 2 : backslashes,
                   '\\'); // a run at the end of a quoted argument doubles
        if (quote) {
            out += '"';
        }
    }
    return out;
}

bool isAbsolutePath(std::string_view path, Os os) {
    if (os == Os::Posix) {
        return path.starts_with('/');
    }
    const bool drive = path.size() >= 3 && isAsciiLetter(path[0]) && path[1] == ':' && isSep(path[2], os);
    return drive || path.starts_with("\\\\") || path.starts_with("//");
}

std::string joinPath(std::string_view dir, std::string_view name, Os os) {
    std::string out(dir);
    if (!out.empty() && !isSep(out.back(), os)) {
        out += os == Os::Windows ? '\\' : '/';
    }
    out += name;
    return out;
}

std::string parentDir(std::string_view path, Os os) {
    const auto at = path.find_last_of(os == Os::Windows ? "/\\" : "/");
    if (at == std::string_view::npos) {
        return {};
    }
    if (at == 0) { // "/x" -> "/", "\x" -> "\"
        return std::string(path.substr(0, 1));
    }
    if (os == Os::Windows && at == 2 && path[1] == ':') { // "C:\x" -> "C:\"
        return std::string(path.substr(0, 3));
    }
    return std::string(path.substr(0, at));
}

bool sameDir(std::string_view a, std::string_view b, Os os) {
    return dirKey(a, os) == dirKey(b, os);
}

bool sameVolume(std::string_view a, std::string_view b) {
    const std::string va = volumeOf(a);
    return !va.empty() && va == volumeOf(b);
}

bool hasExeSuffix(std::string_view name) {
    if (name.size() < 4) {
        return false;
    }
    const std::string_view tail = name.substr(name.size() - 4);
    return tail[0] == '.' && lowerAscii(tail[1]) == 'e' && lowerAscii(tail[2]) == 'x' &&
           lowerAscii(tail[3]) == 'e';
}

bool isProgramFile(const std::string& path, Os os) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(fsPath(path), ec)) {
        return false;
    }
    if (os == Os::Windows) {
        return hasExeSuffix(path);
    }
#ifndef _WIN32
    return ::access(path.c_str(), X_OK) == 0;
#else
    return false; // a Posix query on Windows: no execute bit exists
#endif
}

std::vector<std::string> defaultCliPaths(Os os, std::string_view pluginDataDir,
                                         std::string_view programFiles) {
    if (os == Os::Posix) {
        return {std::string(kCliInstallDir) + std::string(kDefaultCliName)};
    }
    const std::string exe = std::string(kDefaultCliName) + ".exe";
    std::vector<std::string> out;
    if (!pluginDataDir.empty()) {
        out.push_back(joinPath(joinPath(pluginDataDir, "cli", Os::Windows), exe, Os::Windows));
    }
    if (!programFiles.empty()) {
        out.push_back(joinPath(joinPath(programFiles, kCliWindowsFolder, Os::Windows), exe, Os::Windows));
    }
    return out;
}

std::filesystem::path fsPath(std::string_view utf8) {
    return std::filesystem::path(
        std::u8string_view(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string utf8(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

std::tm localTime(std::time_t t) {
    std::tm out{};
#ifdef _WIN32
    localtime_s(&out, &t);
#else
    localtime_r(&t, &out);
#endif
    return out;
}

} // namespace cg
