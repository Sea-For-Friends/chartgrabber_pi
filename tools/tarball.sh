#!/usr/bin/env bash
# Pack the built plugin into the Plugin Manager tarball .
# Usage: tools/tarball.sh <build-dir> [out-dir] [--cli-bundle <dir>]     (out-dir default: <build-dir>)
# --cli-bundle: the content of <dir> (the unpacked core bundle, named
# chart-grabber-cli-<N.N.N>-(linux-x86_64|linux-arm64|win64)) goes to .../chartgrabber_pi/cli/; modes: the program and every
# *.so / *.so.* 755, the rest 644. Exit 2: name off the pattern, platform not fitting <target>/<target-arch> of metadata.xml,
# no top-level executable chart-grabber-cli[.exe] (Windows: a regular file, exec bit not needed), any symlink, a chart-grabber.conf or portable.txt (file or folder, any depth: the CLI would go portable), or no NOTICE, LICENSE, licenses/. The bundled program is never run.
# The two BUILT-FROM.txt lines "cli: <dir name>" and "cli-content-sha256: <hex>" go to stderr (the release record takes them).
# The target/version/arch of metadata.xml are fixed at configure time (tools/vm.sh reads the tuple in the VM):
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
bundle=
args=()
while (($#)); do
    if [[ $1 == --cli-bundle ]]; then
        [[ $# -ge 2 && -d $2 ]] || { echo "tarball.sh: --cli-bundle needs a directory" >&2; exit 2; }
        bundle=$(cd "$2" && pwd); shift 2
    else args+=("$1"); shift; fi
done
set -- "${args[@]}"
[[ $# -ge 1 && $# -le 2 ]] || { echo "usage: tarball.sh <build-dir> [out-dir] [--cli-bundle <dir>]" >&2; exit 2; }
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
# the CLI's walk: depth 6, entries ending .zmp (any case), file or directory, or a regular file ending .source.json;
# hidden entries do not count
[[ -n $(find "$src" -mindepth 1 -maxdepth 6 -name '.*' -prune -o \( -iname '*.zmp' -o -type f -iname '*.source.json' \) -print -quit) ]] ||
    refuse "data/sources holds no .zmp entry and no .source.json file"
for t in LICENSE NOTICE; do
    [[ -n $(find "$src" -mindepth 1 -maxdepth 1 -type f -name "$t*" -print -quit) ]] ||
        refuse "data/sources has no top-level $t* file (terms travel with the sources)"
done
# the CLI bundle: checked before anything is written
cliname=
if [[ -n $bundle ]]; then
    cliname=$(basename "$bundle")
    clire='^chart-grabber-cli-[0-9]+\.[0-9]+\.[0-9]+-(linux-x86_64|linux-arm64|win64)$'
    [[ $cliname =~ $clire ]] || refuse "bundle dir name '$cliname' does not match chart-grabber-cli-N.N.N-(linux-x86_64|linux-arm64|win64)"
    plat=${BASH_REMATCH[1]}
    arch=$(sed -n 's|.*<target-arch>\(.*\)</target-arch>|\1|p' "$build/metadata.xml" | head -1)
    if ((win)); then want=win64; exe=chart-grabber-cli.exe; else want=linux-$arch; exe=chart-grabber-cli; fi
    [[ $plat == "$want" ]] || refuse "bundle $plat does not fit this build (target '$target', arch '$arch')"
    if ((win)); then [[ -f $bundle/$exe ]]; else [[ -f $bundle/$exe && -x $bundle/$exe ]]; fi ||
        refuse "bundle has no executable top-level $exe"
    for t in NOTICE LICENSE; do [[ -f $bundle/$t ]] || refuse "bundle has no $t"; done
    [[ -d $bundle/licenses ]] || refuse "bundle has no licenses/ folder"
    l=$(find "$bundle" -type l -print -quit)
    [[ -z $l ]] || refuse "bundle holds a symlink (${l#"$bundle"/}): ship real files named by SONAME"
    p=$(find "$bundle" \( -name chart-grabber.conf -o -name portable.txt \) -print -quit)
    [[ -z $p ]] || refuse "bundle holds ${p#"$bundle"/}: the CLI would run portable and ignore --datadir"
fi
name=$(tr -d '\r' <"$build/tarball-name.txt") # a Windows CMake writes CRLF into both files
# the Windows FILE and top dir say windows-x86; metadata.xml keeps <target>msvc-wx32 (OpenCPN needs it)
((win)) && name=${name/-msvc-wx32-10-x86/-windows-x86}

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
if [[ -n $bundle ]]; then
    clidst=$(dirname "$data")/cli
    mkdir -p "$clidst"
    cp -r "$bundle/." "$clidst/"
fi

# explicit modes: the result must not depend on the umask or on the modes of the source files
find "$top" -type d -exec chmod 0755 {} +
find "$top" -type f -exec chmod 0644 {} +
chmod 0755 "$libdst"
if [[ -n $bundle ]]; then
    find "$clidst" -type f \( -name '*.so' -o -name '*.so.*' \) -exec chmod 0755 {} +
    chmod 0755 "$clidst/$exe"
    sum=$(cd "$bundle" && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum | sha256sum | cut -d' ' -f1)
    echo "cli: $cliname" >&2
    echo "cli-content-sha256: $sum" >&2
fi

tar --sort=name --mtime="@$epoch" --owner=0 --group=0 --numeric-owner -C "$stage" -cf - "$name" |
    gzip -n >"$out/$name.tar.gz"
echo "$out/$name.tar.gz"
