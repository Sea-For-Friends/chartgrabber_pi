// SPDX-License-Identifier: GPL-3.0-or-later
// A stand-in for `chart-grabber-cli sources --format=tsv` for the window tests: a shell script in its own
// folder that prints the `source` records of records.tsv (type, guid, kind, name, contentType, epsg,
// cacheType, verdict, zmpFile, group, tab separated), then stderr.txt to stderr, appends its argument list to
// argv.log, waits while the file `hold` exists, and exits with the number in `exit` (0 when absent).
// `--version` (run by probeCli before every first use of the file) prints the line in the file `version`
// ("chart-grabber-cli 2026.10.9 (test)" when absent) and appends a line to probes.log; it touches nothing
// else. `estimate` and `run` / `export` do the same with their own files: estimate.out (stdout), estimate.err
// (stderr), estimate.exit, estimate.log (the argument list), hold-estimate, and the same with `run`; the
// sources files above are not used for them. rawRecords() holds the tsv records of the CLI. A test steers the
// next run by rewriting these files. The folder name has a space and a quote on purpose.
#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace stub {

// The earlier curated tables as a favourite-sources.conf, so a window test that is not about the
// file keeps its lists and shows no warning: written to <root>/data/, the bundled sources folder returned
// (<root>/data/sources-x; the caller sets it with ConfigStore::setBundledSourcesDir). Tests of the file
// itself write their own.
inline std::string installFavourites(const std::filesystem::path& root) {
    std::string t = "format\t1\n";
    for (const char* g : {"{7B743985-BC5F-4AB6-8915-AC5DBBB8F552}", "{F6574B06-E632-4D5F-BC75-C8FA658B57DF}",
                          "{9A360A54-7A70-402D-8A11-D570BD759B5B}", "{92122135-6B15-4800-AA9E-3E48C556DA17}",
                          "{E05EDD5E-C6C3-483C-8BA7-8DE1D9892ED8}", "{D643C5A1-4331-4387-9BE2-DDDF185C117E}",
                          "{51C1079B-B55D-4DF7-A410-ADC118F53AB1}", "{C44877A0-D455-4238-B5E2-C8BFF25FC0D4}",
                          "{91D4F1B4-A7CD-4FC8-95F1-637609068FB2}", "{FE84A175-2C0F-420F-86DF-5900DE16A33C}",
                          "{8B9C244E-E978-4267-8FE4-D703534C0789}", "{99D06D6A-F7FA-4F06-8C00-D9F96307C0F3}",
                          "{478CBB2F-70D1-4573-8C50-B5361CE2AA46}"})
        t += std::string("chart\t") + g + "\n";
    for (const char* g : {"{1A696D82-F258-41A4-BCD6-7648D9E08DE2}", "{25E4232E-08A5-47C5-8D97-E38B5F418A2D}",
                          "{076D1F9A-DC94-4184-B785-77362CD01B5B}", "{BC2EA862-0F3D-4933-8F71-00AFA0BAD114}",
                          "{92E40853-04A2-46B7-8A5B-3665FCDDFDBE}"})
        t += std::string("weather\t") + g + "\n";
    t += "seamarks\t{AB8A8685-AEC6-4E16-80DA-DC4AC37E4777}\n";
    std::filesystem::create_directories(root / "data");
    std::ofstream(root / "data" / "favourite-sources.conf", std::ios::binary | std::ios::trunc) << t;
    return (root / "data" / "sources-x").string();
}

// Right after `#!/bin/sh` in every stand-in that a test stops: the CLI's `--stop-on-stdin`, as a
// shell does it. Only when the option is among the arguments (like the real CLI: `--version` reads
// no stdin): a script gets it in "$@", an `sh -c` stand-in as $0 (the option is appended last). Then a
// reader (fd 3: a background list's own stdin is /dev/null) turns the line `stop` or EOF into a SIGTERM of
// the script itself, so the script's own TERM trap acts as before.
inline const char* const kStopPrelude =
    "case \" $0 $* \" in *\" --stop-on-stdin \"*)\n"
    "exec 3<&0\n"
    "{ while IFS= read -r l <&3; do [ \"$l\" = stop ] && break; done; kill -TERM $$; } >/dev/null 2>&1 &\n"
    "w=$!; trap 'kill $w 2>/dev/null' EXIT ;;\n"
    "esac\n";

struct Rec {
    std::string guid, kind, name;
    int epsg = 3857;
    std::string verdict = "ok", group;
    std::string navigationWarning = {}; // empty = a 10-column line, else a 15-column line ending in this
};

class Sources {
public:
    explicit Sources(const std::filesystem::path& root, const std::string& folder = "stub d'x")
        : dir_(root / folder) {
        std::filesystem::create_directories(dir_);
        std::ofstream(script())
            << "#!/bin/sh\n"
            << kStopPrelude
            << "d=$(dirname \"$0\")\n"
               "[ \"$1\" = --version ] && { echo run >> \"$d/probes.log\"; cat \"$d/version\" 2>/dev/null ||"
               " echo 'chart-grabber-cli 2026.10.9 (test)'; exit 0; }\n"
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
    // The `--version` answer (default: chart-grabber-cli 2026.10.9). probeCli caches per file,
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
