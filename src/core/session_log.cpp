// SPDX-License-Identifier: GPL-3.0-or-later
#include "session_log.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace cg {

std::string formatLogStamp(const std::tm& t, int ms) {
    if (ms < 0)
        ms = 0;
    if (ms > 999)
        ms = 999;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d.%03d", t.tm_year + 1900, t.tm_mon + 1,
                  t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, ms);
    return buf;
}

std::string cleanLogText(std::string_view text) {
    std::size_t cut = text.size();
    if (cut > kSessionLogMaxLine) {
        cut = kSessionLogMaxLine;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
            --cut; // text[cut] would be the tail of a sequence: step back, dropping its lead byte too
    }
    std::string out(text.substr(0, cut));
    for (char& c : out)
        if (static_cast<unsigned char>(c) < 0x20 && c != '\t')
            c = ' ';
    return out;
}

SessionLog::~SessionLog() {
    close();
}

bool SessionLog::open(const std::string& path, std::uintmax_t capBytes, std::string* why) {
    close();
    auto fail = [&](const std::string& reason) {
        if (why)
            *why = reason;
        return false;
    };
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path file(path);
    if (file.has_parent_path()) {
        fs::create_directories(file.parent_path(), ec);
        if (ec)
            return fail(ec.message());
    }
    fs::remove(fs::path(path + ".1"), ec); // a failed removal is not an error: the roll replaces it
    file_ = std::fopen(path.c_str(), "w");
    if (!file_)
        return fail(std::strerror(errno));
    path_ = path;
    cap_ = capBytes;
    return true;
}

void SessionLog::close() {
    if (file_)
        std::fclose(file_);
    file_ = nullptr;
    path_.clear();
    header_.clear();
    size_ = base_ = 0;
    headerDone_ = false;
}

void SessionLog::endHeader() {
    headerDone_ = true;
}

// Moves the full file to "<path>.1" and starts a new one with the header and the marker. false = writing
// ended for the session (file_ is null).
bool SessionLog::roll(std::string_view stamp) {
    std::fclose(file_);
    file_ = nullptr;
    std::error_code ec;
    std::filesystem::rename(path_, path_ + ".1", ec);
    if (!ec)
        file_ = std::fopen(path_.c_str(), "w");
    if (!file_) {
        path_.clear();
        return false;
    }
    const std::string name = std::filesystem::path(path_).filename().string();
    const std::string head = header_ + std::string(stamp) + " log continued from " + name + ".1\n";
    std::fwrite(head.data(), 1, head.size(), file_);
    size_ = base_ = head.size();
    return true;
}

void SessionLog::write(std::string_view stamp, std::string_view prefix, std::string_view text) {
    if (!file_)
        return;
    std::string line;
    line.append(stamp).push_back(' ');
    line.append(prefix).append(cleanLogText(text)).push_back('\n');
    // base_: the bytes every file starts with (header, marker). A file holding only those does not roll
    // again.
    if (size_ + line.size() > cap_ && size_ > base_ && !roll(stamp))
        return;
    std::fwrite(line.data(), 1, line.size(), file_);
    std::fflush(file_);
    size_ += line.size();
    if (!headerDone_) {
        header_ += line;
        base_ = size_;
    }
}

} // namespace cg
