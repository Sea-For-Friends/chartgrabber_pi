// SPDX-License-Identifier: GPL-3.0-or-later
#include "chart_folder.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace cg {
namespace {

namespace fs = std::filesystem;

bool under(const fs::path& path, const fs::path& root) {
    if (root.empty())
        return false;
    const fs::path p = path.lexically_normal(), r = root.lexically_normal();
    auto pi = p.begin();
    for (auto ri = r.begin(); ri != r.end(); ++ri) {
        if (ri->empty()) // the trailing separator of "/a/"
            continue;
        if (pi == p.end() || *pi != *ri)
            return false;
        ++pi;
    }
    return true;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

} // namespace

std::vector<std::string> flatpakGranted(std::string_view text, std::string_view home) {
    std::vector<std::string> out;
    bool inContext = false;
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        const std::string_view line = trim(text.substr(0, nl));
        text = nl == std::string_view::npos ? std::string_view() : text.substr(nl + 1);
        if (!line.empty() && line.front() == '[') {
            inContext = line == "[Context]";
            continue;
        }
        if (!inContext || line.substr(0, 12) != "filesystems=")
            continue;
        std::string_view list = line.substr(12);
        while (!list.empty()) {
            const std::size_t semi = list.find(';');
            std::string_view e = trim(list.substr(0, semi));
            list = semi == std::string_view::npos ? std::string_view() : list.substr(semi + 1);
            if (e.empty() || e.front() == '!' || (e.size() > 3 && e.substr(e.size() - 3) == ":ro"))
                continue;
            if (const std::size_t colon = e.rfind(':'); colon != std::string_view::npos && colon > 0)
                e = e.substr(0, colon); // :rw, :create
            if (e == "home" && !home.empty())
                out.emplace_back(home);
            else if (e == "host")
                out.emplace_back("/");
            else if (e.front() == '/')
                out.emplace_back(e);
            else if (e.substr(0, 2) == "~/" && !home.empty())
                out.push_back(std::string(home) + "/" + std::string(e.substr(2)));
        }
    }
    return out;
}

Sandbox detectSandbox(const std::string& infoPath) {
    Sandbox s;
    std::ifstream in(infoPath, std::ios::binary);
    if (!in)
        return s;
    s.flatpak = true;
    const char* home = std::getenv("HOME");
    s.home = home ? home : "";
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    s.granted = flatpakGranted(text, s.home);
    return s;
}

std::function<Sandbox()>& sandboxProbe() {
    static std::function<Sandbox()> probe = [] { return detectSandbox(); };
    return probe;
}

FolderVerdict checkChartFolder(const std::string& folder, bool isDefault, bool exists, bool writable,
                               const Sandbox& sandbox) {
    if (sandbox.flatpak) {
        bool visible = under(folder, sandbox.home);
        for (const std::string& g : sandbox.granted)
            visible = visible || under(folder, g);
        if (!visible)
            return FolderVerdict::FlatpakOutsideHome;
    }
    if (!exists)
        return isDefault ? FolderVerdict::Ok : FolderVerdict::CannotWrite;
    return writable ? FolderVerdict::Ok : FolderVerdict::CannotWrite;
}

} // namespace cg
