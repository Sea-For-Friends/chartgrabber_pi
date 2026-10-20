// SPDX-License-Identifier: GPL-3.0-or-later
// The plugin's own log file : stamp, clean text, cap and roll. std only, no wx;
// one instance, GUI thread only, no locking; never throws.
#pragma once

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <string_view>

namespace cg {

inline constexpr std::string_view kSessionLogName = "chartgrabber.log"; // the rolled file: <path> + ".1"
inline constexpr std::uintmax_t kSessionLogCapBytes = 2 * 1024 * 1024;  // per file
inline constexpr std::size_t kSessionLogMaxLine = 4096;                 // bytes of <text> per line

// "YYYY-MM-DD HH:MM:SS.mmm" from a broken-down time (tm_year + 1900, tm_mon + 1) and ms clamped to 0..999.
std::string formatLogStamp(const std::tm& t, int ms);
// Bytes < 0x20 except '\t' become ' '; then cut to kSessionLogMaxLine bytes, never inside a UTF-8 sequence.
std::string cleanLogText(std::string_view text);

class SessionLog {
public:
    SessionLog() = default;
    ~SessionLog();
    SessionLog(const SessionLog&) = delete;
    SessionLog& operator=(const SessionLog&) = delete;
    // Closes a file already open; creates the parent folders; removes "<path>.1"; opens `path` truncated.
    // false when any step but the removal fails (*why = a short English reason, may be null): writes are then
    // no-ops.
    bool open(const std::string& path, std::uintmax_t capBytes = kSessionLogCapBytes,
              std::string* why = nullptr);
    void close(); // no-op when closed; forgets the header
    bool isOpen() const { return file_ != nullptr; }
    const std::string& path() const { return path_; } // the open file, else empty
    // "<stamp> <prefix><cleanLogText(text)>\n", then fflush; rolls first when the line would pass the cap.
    void write(std::string_view stamp, std::string_view prefix, std::string_view text);
    void endHeader(); // the lines written so far are the header (once)

private:
    bool roll(std::string_view stamp);

    std::FILE* file_ = nullptr;
    std::string path_, header_;                           // header_: the exact bytes of the header lines
    std::uintmax_t cap_ = kSessionLogCapBytes, size_ = 0; // size_: bytes in the current file
    std::uintmax_t base_ = 0;
    bool headerDone_ = false;
};

} // namespace cg
