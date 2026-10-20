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
# pass it), owner 0:0, sorted names, fixed modes (dirs 755, files 644, the library 755), gzip -n.
# Build the tarball INSIDE the VM of the target architecture (never cross-built).
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
[[ $# -ge 1 ]] || { echo "usage: tarball.sh <build-dir> [out-dir]" >&2; exit 2; }
build=$(cd "$1" && pwd)
out=${2:-$build}
mkdir -p "$out"
out=$(cd "$out" && pwd)

lib=$build/libchartgrabber_pi.so
for f in "$lib" "$build/metadata.xml" "$build/tarball-name.txt" \
         "$here/data/chartgrabber_tool.svg" "$here/data/chartgrabber_tool_toggled.svg" \
         "$here/data/chartgrabber_tool_rollover_toggled.svg"; do
    [[ -f $f ]] || { echo "tarball.sh: missing $f (build first)" >&2; exit 1; }
done
target=$(sed -n 's|.*<target>\(.*\)</target>.*|\1|p' "$build/metadata.xml" | head -1)
if [[ -z $target || ${target,,} == all ]]; then
    echo "tarball.sh: metadata.xml <target> is '${target}': empty or 'all' is forbidden;" \
         "configure with the host tuple of the build host's OpenCPN, e.g. -DCG_TARGET=debian-x86_64" >&2
    exit 2
fi
src=$here/data/sources
refuse() { echo "tarball.sh: $1" >&2; exit 2; }
[[ -d $src ]] || refuse "data/sources is missing"
# the CLI's walk: depth 6, entries ending .zmp (any case), file or directory; hidden entries do not count
[[ -n $(find "$src" -mindepth 1 -maxdepth 6 -name '.*' -prune -o -iname '*.zmp' -print -quit) ]] ||
    refuse "data/sources holds no .zmp entry"
for t in LICENSE NOTICE; do
    [[ -n $(find "$src" -mindepth 1 -maxdepth 1 -type f -name "$t*" -print -quit) ]] ||
        refuse "data/sources has no top-level $t* file (terms travel with the sources)"
done
name=$(<"$build/tarball-name.txt")

epoch=${SOURCE_DATE_EPOCH:-$(git -C "$here" log -1 --format=%ct 2>/dev/null || true)}
[[ $epoch =~ ^[0-9]+$ ]] || { echo "tarball.sh: no commit time: set SOURCE_DATE_EPOCH (a tree without git)" >&2; exit 1; }

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
top=$stage/$name
data=$top/usr/share/opencpn/plugins/chartgrabber_pi/data
mkdir -p "$top/usr/lib/opencpn" "$data"
cp "$build/metadata.xml" "$top/metadata.xml"
install -m 0755 "$lib" "$top/usr/lib/opencpn/libchartgrabber_pi.so"
for f in chartgrabber_tool chartgrabber_tool_toggled chartgrabber_tool_rollover_toggled; do
    cp "$here/data/$f.svg" "$data/"
done
cp -r "$src" "$data/sources"
find "$data/sources" -name '.*' -prune -exec rm -rf {} +

# explicit modes: the result must not depend on the umask or on the modes of the source files
find "$top" -type d -exec chmod 0755 {} +
find "$top" -type f -exec chmod 0644 {} +
chmod 0755 "$top/usr/lib/opencpn/libchartgrabber_pi.so"

tar --sort=name --mtime="@$epoch" --owner=0 --group=0 --numeric-owner -C "$stage" -cf - "$name" |
    gzip -n >"$out/$name.tar.gz"
echo "$out/$name.tar.gz"
