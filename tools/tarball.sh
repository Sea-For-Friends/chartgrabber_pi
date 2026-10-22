#!/usr/bin/env bash
# Pack the built plugin into the Plugin Manager tarball.
# Usage: tools/tarball.sh <build-dir> [out-dir]     (out-dir default: <build-dir>)
# The target/version/arch of metadata.xml are fixed at configure time:
#   cmake -S . -B build -DCG_TARGET=debian-arm64 -DCG_TARGET_VERSION=n/a -DCG_TARGET_ARCH=arm64
# Exit 2 when the build's <target> is empty or "all" (a wx-linked library never claims all).
# Exit 2 also when data/sources (the bundled chart sources) is missing, holds no .zmp entry or no top-level
# LICENSE* / NOTICE* file; hidden files and .git inside it are not packed.
# Byte-reproducible the same build dir and the same commit give the same sha256. Every entry gets the
# mtime SOURCE_DATE_EPOCH (default: the commit time of HEAD in this tree; a tree without git needs the variable,
# tools/vm.sh passes it), owner 0:0, sorted names, fixed modes (dirs 755, files 644, the library 755), gzip -n.
# A <target> starting with msvc: <build>/chartgrabber_pi.dll, entries metadata.xml, plugins/chartgrabber_pi.dll (755),
# plugins/chartgrabber_pi/data/ (the same icons and sources-<version>/).
# The Linux tarball is built inside the VM of its architecture; the Windows one is packed on the host by tools/vm.sh from the DLL
# fetched from plugin-win10 and the data of the synced commit.
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
[[ $# -ge 1 ]] || { echo "usage: tarball.sh <build-dir> [out-dir]" >&2; exit 2; }
build=$(cd "$1" && pwd)
out=${2:-$build}
mkdir -p "$out"
out=$(cd "$out" && pwd)

target=$(sed -n 's|.*<target>\(.*\)</target>.*|\1|p' "$build/metadata.xml" 2>/dev/null | head -1 || true)
# Windows: <target> msvc*: the DLL of the Visual Studio build, layout plugins/ (the Plugin Manager import moves *.dll to bin\
# and plugins/ to share\, 5.10.2 model/src/plugin_handler.cpp:515-535); tools/vm.sh packs it on the host from the fetched files.
win=0
[[ ${target,,} == msvc* ]] && win=1
if ((win)); then lib=$build/chartgrabber_pi.dll; else lib=$build/libchartgrabber_pi.so; fi
for f in "$lib" "$build/metadata.xml" "$build/tarball-name.txt" \
         "$here/data/chartgrabber_tool.svg" "$here/data/chartgrabber_tool_toggled.svg" \
         "$here/data/chartgrabber_tool_rollover_toggled.svg"; do
    [[ -f $f ]] || { echo "tarball.sh: missing $f (build first)" >&2; exit 1; }
done
if [[ -z $target || ${target,,} == all ]]; then
    echo "tarball.sh: metadata.xml <target> is '${target}': empty or 'all' is forbidden;" \
         "configure with the host tuple of the build host's OpenCPN, e.g. -DCG_TARGET=debian-x86_64" >&2
    exit 2
fi
version=$(sed -n 's|.*<version>\(.*\)</version>.*|\1|p' "$build/metadata.xml" | head -1)
src=$here/data/sources
refuse() { echo "tarball.sh: $1" >&2; exit 2; }
[[ -n $version && $version != */* ]] || refuse "metadata.xml <version> is '${version}': empty or holding a / (the folder is data/sources-<version>)"
[[ -d $src ]] || refuse "data/sources is missing"
# the CLI's walk: depth 6, entries ending .zmp (any case), file or directory; hidden entries do not count
[[ -n $(find "$src" -mindepth 1 -maxdepth 6 -name '.*' -prune -o -iname '*.zmp' -print -quit) ]] ||
    refuse "data/sources holds no .zmp entry"
for t in LICENSE NOTICE; do
    [[ -n $(find "$src" -mindepth 1 -maxdepth 1 -type f -name "$t*" -print -quit) ]] ||
        refuse "data/sources has no top-level $t* file (terms travel with the sources)"
done
name=$(tr -d '\r' <"$build/tarball-name.txt") # a Windows CMake writes CRLF into both files

epoch=${SOURCE_DATE_EPOCH:-$(git -C "$here" log -1 --format=%ct 2>/dev/null || true)}
[[ $epoch =~ ^[0-9]+$ ]] || { echo "tarball.sh: no commit time: set SOURCE_DATE_EPOCH (a tree without git)" >&2; exit 1; }

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
top=$stage/$name
if ((win)); then
    libdst=$top/plugins/chartgrabber_pi.dll
    data=$top/plugins/chartgrabber_pi/data
else
    libdst=$top/usr/lib/opencpn/libchartgrabber_pi.so
    data=$top/usr/share/opencpn/plugins/chartgrabber_pi/data
fi
mkdir -p "$(dirname "$libdst")" "$data"
tr -d '\r' <"$build/metadata.xml" >"$top/metadata.xml"
install -m 0755 "$lib" "$libdst"
for f in chartgrabber_tool chartgrabber_tool_toggled chartgrabber_tool_rollover_toggled; do
    cp "$here/data/$f.svg" "$data/"
done
cp -r "$src" "$data/sources-$version"
find "$data/sources-$version" -name '.*' -prune -exec rm -rf {} +

# explicit modes: the result must not depend on the umask or on the modes of the source files
find "$top" -type d -exec chmod 0755 {} +
find "$top" -type f -exec chmod 0644 {} +
chmod 0755 "$libdst"

tar --sort=name --mtime="@$epoch" --owner=0 --group=0 --numeric-owner -C "$stage" -cf - "$name" |
    gzip -n >"$out/$name.tar.gz"
echo "$out/$name.tar.gz"
