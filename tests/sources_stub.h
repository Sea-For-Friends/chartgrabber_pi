// SPDX-License-Identifier: GPL-3.0-or-later
// A stand-in for `chart-grabber-cli sources --format=tsv` for the window tests (has not
// landed, so the real tsv cannot be used): a shell script in its own folder that prints the `source` records
// of records.tsv (the format of: type, guid, kind, name,
// contentType, epsg, cacheType, verdict, zmpFile, group, tab separated), then stderr.txt to stderr, appends
// its argument list to argv.log, waits while the file `hold` exists, and exits with the number in `exit` (0
// when absent). `--version` (run by probeCli before every first use of the file) prints the line in the file
// `version` ("chart-grabber-cli 2026.10.6 (test)" when absent) and appends a line to probes.log; it touches
// nothing else. `estimate` and `run` / `export` do the same with their own files: estimate.out
// (stdout), estimate.err (stderr), estimate.exit, estimate.log (the argument list), hold-estimate, and the
// same with `run`; the sources files above are not used for them. rawRecords holds the tsv
// records of the CLI. A test steers the next run by rewriting these files. The folder name has a space and a
// quote on purpose.
#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace stub {

struct Rec {
    std::string guid, kind, name;
    int epsg = 3857;
    std::string verdict = "ok", group;
    std::string navigationWarning =
        {}; // empty = a 10-column line, else a 15-column line ending in this
};

class Sources {
public:
    explicit Sources(const std::filesystem::path& root, const std::string& folder = "stub d'x")
        : dir_(root / folder) {
        std::filesystem::create_directories(dir_);
        std::ofstream(script())
            << "#!/bin/sh\n"
               "d=$(dirname \"$0\")\n"
               "[ \"$1\" = --version ] && { echo run >> \"$d/probes.log\"; cat \"$d/version\" 2>/dev/null ||"
               " echo 'chart-grabber-cli 2026.10.6 (test)'; exit 0; }\n"
               // `estimate` and `run` / `export` have their own files, so the sources log and the
               // sources tests stay as they were. Default: sources.
               "case \"$1\" in\n"
               "  estimate) p=estimate ;;\n"
               "  run|export) p=run ;;\n"
               "  *) p= ;;\n"
               "esac\n"
               "if [ -n \"$p\" ]; then\n"
               "  [ -f \"$d/$p.out\" ] && cat \"$d/$p.out\"\n"
               "  [ -f \"$d/$p.err\" ] && cat \"$d/$p.err\" >&2\n"
               "  echo \"$*\" >> \"$d/$p.log\"\n"
               "  echo \"${LC_ALL-unset}\" >> \"$d/$p-lcall.log\"\n"
               "  k=0; while [ -f \"$d/hold-$p\" ] && [ $k -lt 3000 ]; do sleep 0.02; k=$((k + 1)); done\n"
               "  exit $(cat \"$d/$p.exit\" 2>/dev/null || echo 0)\n"
               "fi\n"
               "[ -f \"$d/records.tsv\" ] && cat \"$d/records.tsv\"\n"
               "[ -f \"$d/stderr.txt\" ] && cat \"$d/stderr.txt\" >&2\n"
               "echo \"$*\" >> \"$d/argv.log\"\n"
               "echo \"${LC_ALL-unset}\" >> \"$d/lcall.log\"\n"
               "k=0; while [ -f \"$d/hold\" ] && [ $k -lt 3000 ]; do sleep 0.02; k=$((k + 1)); done\n"
               "exit $(cat \"$d/exit\" 2>/dev/null || echo 0)\n";
        chmod(script().c_str(), 0755);
    }
    std::string cli() const { return script(); }
    void records(const std::vector<Rec>& recs) {
        std::ofstream out(dir_ / "records.tsv", std::ios::trunc);
        for (const Rec& r : recs)
            out << "source\t" << r.guid << '\t' << (r.kind.empty() ? "map" : r.kind) << '\t' << r.name
                << "\timage/jpeg\t" << r.epsg << "\t0\t" << r.verdict << "\tfile.zmp\t" << r.group
                << (r.navigationWarning.empty() ? "" : "\tuser\tstatic\t\t\t" + r.navigationWarning) << '\n';
    }
    void rawRecords(const std::string& text) { std::ofstream(dir_ / "records.tsv", std::ios::trunc) << text; }
    void stderrText(const std::string& text) { std::ofstream(dir_ / "stderr.txt", std::ios::trunc) << text; }
    // The `--version` answer (default: chart-grabber-cli 2026.10.6). probeCli caches per file,
    // so set it before the first probe of this stand-in.
    void version(const std::string& line) {
        std::ofstream(dir_ / "version", std::ios::trunc) << line << '\n';
    }
    int probes() const {
        std::ifstream in(dir_ / "probes.log");
        int n = 0;
        for (std::string line; std::getline(in, line);)
            ++n;
        return n;
    }
    void exitWith(int code) { std::ofstream(dir_ / "exit", std::ios::trunc) << code; }
    void hold(bool on) {
        if (on)
            std::ofstream(dir_ / "hold") << "x";
        else
            std::filesystem::remove(dir_ / "hold");
    }
    void reset() { // a clean next run: no stderr, exit 0
        std::filesystem::remove(dir_ / "stderr.txt");
        std::filesystem::remove(dir_ / "exit");
    }
    // LC_ALL as the runs that printed their records saw it, one string per run.
    std::vector<std::string> lcAll() const {
        std::vector<std::string> out;
        std::ifstream in(dir_ / "lcall.log");
        for (std::string line; std::getline(in, line);)
            out.push_back(line);
        return out;
    }
    // The argument lists of the runs that got as far as printing their records, one string per run.
    std::vector<std::string> argv() const {
        std::vector<std::string> out;
        std::ifstream in(dir_ / "argv.log");
        for (std::string line; std::getline(in, line);)
            out.push_back(line);
        return out;
    }

    // estimate / run: the text the next run prints (tsv records), stderr, exit
    // code, and the argument lists of the runs so far.
    void estimateOut(const std::string& text) { put("estimate.out", text); }
    void estimateErr(const std::string& text) { put("estimate.err", text); }
    void estimateExit(int code) { put("estimate.exit", std::to_string(code)); }
    void runOut(const std::string& text) { put("run.out", text); }
    void runErr(const std::string& text) { put("run.err", text); }
    void runExit(int code) { put("run.exit", std::to_string(code)); }
    void holdEstimate(bool on) { holdFile("hold-estimate", on); }
    void holdRun(bool on) { holdFile("hold-run", on); }
    std::vector<std::string> estimateArgv() const { return lines("estimate.log"); }
    std::vector<std::string> runArgv() const { return lines("run.log"); }
    std::vector<std::string> estimateLcAll() const { return lines("estimate-lcall.log"); }
    // The folder of the stand-in (the test puts files next to it).
    const std::filesystem::path& dir() const { return dir_; }

private:
    void put(const char* file, const std::string& text) {
        std::ofstream(dir_ / file, std::ios::trunc) << text;
    }
    void holdFile(const char* file, bool on) {
        if (on)
            std::ofstream(dir_ / file) << "x";
        else
            std::filesystem::remove(dir_ / file);
    }
    std::vector<std::string> lines(const char* file) const {
        std::vector<std::string> out;
        std::ifstream in(dir_ / file);
        for (std::string line; std::getline(in, line);)
            out.push_back(line);
        return out;
    }
    std::string script() const { return (dir_ / "sources.sh").string(); }
    std::filesystem::path dir_;
};

} // namespace stub
