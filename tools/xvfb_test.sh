#!/usr/bin/env bash
# Runs one command (a window test) on its own private Xvfb display. Used by ctest as the test launcher of
# cg_gui_tests, so window tests run in parallel without sharing a display. Never the live desktop: the caller's
# DISPLAY is replaced by the private one, WAYLAND_DISPLAY is unset, GDK_BACKEND=x11, and a failing Xvfb fails the test.
# xvfb-run is not used: it sleeps 3 s per run (-w 0 is not reliable), Xvfb -displayfd picks a free display itself.
# Usage: xvfb_test.sh <command> [args...]
set -u
unset WAYLAND_DISPLAY
export GDK_BACKEND=x11
if [[ $# -eq 0 ]]; then
    echo "xvfb_test.sh: usage: xvfb_test.sh <command> [args...]" >&2
    exit 2
fi
xvfb_bin=${CG_XVFB:-Xvfb}
exec {dfd}< <(exec "$xvfb_bin" -displayfd 1 -nolisten tcp -screen 0 1920x1080x24 2>/dev/null)
xpid=$!
trap 'kill "$xpid" 2>/dev/null; wait "$xpid" 2>/dev/null' EXIT
num=
if ! read -r -t 30 num <&"$dfd" || [[ ! $num =~ ^[0-9]+$ ]]; then
    echo "xvfb_test.sh: FAIL: Xvfb did not report a display (install xorg-server-xvfb)" >&2
    exit 1
fi
export DISPLAY=:$num
"$@"
