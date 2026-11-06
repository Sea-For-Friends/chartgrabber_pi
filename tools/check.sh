#!/usr/bin/env bash
# The gate: clang-format first, then configure, build, ctest.
# Usage: CG_JOBS=4 tools/check.sh [--asan]
#   --asan   AddressSanitizer + UBSan (+ LeakSanitizer) in build-asan/, no format check
# Environment: CG_JOBS (default: cores), CG_TEST_TIMEOUT (seconds per test, default 600),
#   CG_GATE_PRIORITY=low|normal (default low), CG_BUILD_DIR (default build),
#   CCACHE_DIR (default ~/.cache/ccache-chartgrabber, max_size 10G when the dir has no ccache.conf).
#   Window tests run on a private Xvfb each (tools/xvfb_test.sh, the TEST_LAUNCHER of cg_gui_tests; package
#   xorg-server-xvfb), in parallel under ctest -j, never on the live desktop (a window test must not open a window on
#   the user's screen). Without Xvfb the gate FAILS; so it does when a window test is skipped (a skip is not a pass).
#   The last line before "check.sh: OK" is "check.sh: window tests: xvfb run=<n> skipped=0".
set -euo pipefail
# The whole gate (also the build-time gtest_discover_tests run of cg_gui_tests) never sees the live desktop; each window
# test gets its own DISPLAY from tools/xvfb_test.sh.
unset DISPLAY WAYLAND_DISPLAY
export GDK_BACKEND=x11
cd "$(dirname "${BASH_SOURCE[0]}")/.."
root=$PWD
# shellcheck source=tools/gate_env.sh
source "$root/tools/gate_env.sh"

usage() { sed -n '2,10p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

mode=plain
for arg in "$@"; do
    case $arg in
    --asan)
        mode=asan
        ;;
    -h | --help)
        usage
        exit 0
        ;;
    *)
        echo "check.sh: unknown argument '$arg'" >&2
        usage >&2
        exit 2
        ;;
    esac
done

# Checked first: a missing Xvfb must not cost a full build.
if ! command -v Xvfb >/dev/null 2>&1; then
    echo "check.sh: FAIL: Xvfb not found: install xorg-server-xvfb (window tests never use the live desktop)" >&2
    exit 1
fi
# libstdc++ 13+ only (Debian 12 has GCC 12): a failure here must not cost a build.
if grep -rnE 'std::format|clock_cast|<format>' src tests; then
    echo "check.sh: FAIL: the lines above use std::format / clock_cast / <format>: GCC 12 (Debian 12) lacks it" >&2
    exit 1
fi
jobs=${CG_JOBS:-$(nproc)}
timeout=${CG_TEST_TIMEOUT:-600}
build=${CG_BUILD_DIR:-build}
sanitize=
case $mode in
asan)
    build=$build-asan
    sanitize=address
    # Set here, never appended to inherited values.
    export ASAN_OPTIONS=halt_on_error=1:detect_stack_use_after_return=1
    export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
    # Third-party libraries only (see the file); a leak in our own code is still reported.
    export LSAN_OPTIONS=suppressions=$root/tools/lsan.supp:print_suppressions=0
    ;;
esac

cg_gate_priority

# Worktrees share ccache entries (the -ffile-prefix-map half is in CMakeLists.txt).
export CCACHE_BASEDIR=$root CCACHE_NOHASHDIR=1

# Own compile cache (the shared ~/.cache/ccache was full). A caller's CCACHE_DIR wins. max_size only when the dir
# has no ccache.conf yet.
export CCACHE_DIR=${CCACHE_DIR:-$HOME/.cache/ccache-chartgrabber}
if [[ ! -e $CCACHE_DIR/ccache.conf ]]; then
    mkdir -p "$CCACHE_DIR" && echo "max_size = 10G" >"$CCACHE_DIR/ccache.conf"
fi

if [[ $mode == plain ]]; then
    # Format first: a format error must not cost a build.
    echo "== clang-format"
    mapfile -d '' files < <(find src tests -type f \( -name '*.h' -o -name '*.cpp' \) -print0 2>/dev/null | sort -z)
    if [[ ${#files[@]} -gt 0 ]]; then
        clang-format --dry-run --Werror "${files[@]}"
    fi
fi

echo "== configure ($mode)"
# CG_TARGET= explicit: a gate build never carries a tarball target, and a stale CMakeCache.txt (CG_TARGET=all, left by an
# older configure) must not survive into the configure.
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug "-DCG_SANITIZE=$sanitize" -DCG_TARGET=
echo "== build (jobs $jobs)"
cmake --build "$build" -j "$jobs"
echo "== ctest (timeout ${timeout}s per test)"
ctest_log=$(mktemp)
trap 'rm -f "$ctest_log"' EXIT
# cg_gui_tests: each test on its own Xvfb through its TEST_LAUNCHER; ctest itself inherits no display, so
# nothing can reach the live desktop.
ctest_cmd=(env -u WAYLAND_DISPLAY -u DISPLAY GDK_BACKEND=x11 ctest --test-dir "$build" --output-on-failure -j "$jobs" --timeout "$timeout")
if ! "${ctest_cmd[@]}" 2>&1 | tee "$ctest_log"; then
    if grep -q 'Timeout' "$ctest_log"; then
        echo "check.sh: FAIL: a test exceeded the ${timeout}s timeout (CG_TEST_TIMEOUT)" >&2
    fi
    exit 1
fi
# Window tests (label gui): a skipped one fails the gate.
mapfile -t gui_names < <(ctest --test-dir "$build" -N -L gui 2>/dev/null | sed -nE 's/^ *Test +#[0-9]+: +//p')
gui_skipped=0
if [[ ${#gui_names[@]} -gt 0 ]]; then
    gui_skipped=$(sed -nE 's/^.*Test +#[0-9]+: +([^ ]+) +\.+ *\*+(Skipped|Not Run).*$/\1/p' "$ctest_log" |
        grep -cFxf <(printf '%s\n' "${gui_names[@]}") || true)
fi
if ((gui_skipped > 0)); then
    echo "check.sh: FAIL: $gui_skipped window test(s) skipped under Xvfb (the display has no output?)" >&2
    exit 1
fi
window_line="check.sh: window tests: xvfb run=${#gui_names[@]} skipped=0"
echo "$window_line"
echo "check.sh: OK"
