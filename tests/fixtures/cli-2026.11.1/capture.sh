#!/usr/bin/env bash
# Re-creates every file of this directory from the pinned CLI 2026.11.1 (see README.md). NOT run by the gate.
# Usage: capture.sh [--all] SCRATCH_DIR   (an empty directory of yours; HOME, cache, datadir, folders live in it)
# By default the kept NETWORK captures (KEEP below: live sizes and times, read as literals by 9 tests) are NOT overwritten
# when their files exist; --all rewrites them too.
# Environment: CG_CLI = the chart-grabber-cli 2026.11.1 binary (core tag v2026.11.1, abbe831b4db3), built out of tree.
set -u
ALL=0
[ "${1:-}" = --all ] && { ALL=1; shift; }
S=${1:?usage: capture.sh [--all] SCRATCH_DIR}
OUT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SRC=$OUT/../../../data/sources
ZMP=$OUT/../sources-zmp # the frozen .zmp copies of a sailor's folder
mkdir -p "$S/home" "$S/cache" "$S/data" "$S/A" "$S/B"
export HOME=$S/home
P=${CG_CLI:?set CG_CLI to the chart-grabber-cli binary}
COMMON=("--cache=$S/cache" "--datadir=$S/data")
# Folder A: three definitions of data/sources. Folder B: a copy of A's OpenTopoMap (the SAME GUID) and one more.
cp "$SRC/Coast and land/OpenTopoMap.source.json" "$SRC/Seamarks and traffic/OpenSeaMap-Seamarks.source.json" "$SRC/Weather now/RainViewer-Radar.source.json" "$S/A/"
cp "$SRC/Coast and land/OpenTopoMap.source.json" "$S/B/Copy-Topo.source.json"
cp "$SRC/Satellite/Esri-World-Imagery.source.json" "$S/B/"

KEEP=" run-navigation-warning export-navigation-warning run-stdin-eof-at-once run-stopped run-continued run-weather-layer run-weather-base-seamarks run-weather-refetch run-weather-nearest-now run-rtofs-snapshot "
# kept NAME: true when the case's files in $OUT stay as they are
kept() { [ "$ALL" = 0 ] && [[ "$KEEP" == *" $1 "* ]] && [ -e "$OUT/$1.stdout" ]; }

# cap NAME CMD... : stdout / stderr / exit of one command, $S replaced by the literal $SCRATCH
cap() {
    local name=$1
    shift
    LC_ALL=C.UTF-8 "$@" >"$S/$name.out" 2>"$S/$name.err"
    echo $? >"$S/$name.exit"
    kept "$name" && return
    for e in out:stdout err:stderr exit:exit; do
        sed "s|$S|\$SCRATCH|g" "$S/$name.${e%%:*}" >"$OUT/$name.${e##*:}"
    done
}
cap version "$P" --version
cap sources-two-folders "$P" sources --format=tsv --maps "$S/A" --maps "$S/B" "${COMMON[@]}"
cap sources-no-maps "$P" sources --format=tsv "${COMMON[@]}"

# navigationWarning. Bundled = the whole data/sources (EMODnet Bathymetry carry NavigationWarning=1).
mkdir -p "$S/N-bundled" "$S/N-sailor" "$S/N-cache" "$S/N-out"
cp "$SRC/Coast and land/OpenTopoMap.source.json" "$S/N-bundled/"
# sailor folder: a copy of OpenTopoMap with a new GUID and the flag (the file keeps its BOM and CRLF line ends)
mkdir "$S/N-sailor/Flagged.zmp"
sed -e 's/^GUID=.*/GUID={D0C0FFEE-0000-4000-8000-000000000078}\r/' -e 's/^name=.*/name=Flagged copy\r/' \
    "$ZMP/OpenTopoMap.zmp/params.txt" >"$S/N-sailor/Flagged.zmp/params.txt"
printf 'NavigationWarning=1\r\n' >>"$S/N-sailor/Flagged.zmp/params.txt"
cap sources-navigation-warning "$P" sources --format=tsv --maps "$S/N-bundled" --maps "$S/N-sailor" "${COMMON[@]}"
cap sources-bundled-all "$P" sources --format=tsv --maps "$SRC" "${COMMON[@]}"
# network, polite: EMODnet Bathymetry (world), zooms 6-7 of a small bbox, into an EMPTY cache; then the export from that cache
NAV=("--source={C44877A0-D455-4238-B5E2-C8BFF25FC0D4}" --bbox=23.5,37.9,23.7,38.0 --zooms=6-7 --format=tsv
    --progress=lines --maps "$SRC" "--cache=$S/N-cache" "--datadir=$S/data")
cap run-navigation-warning "$P" run "${NAV[@]}" "--out=$S/N-out/run.mbtiles"
cap export-navigation-warning "$P" export "${NAV[@]}" "--out=$S/N-out/export.mbtiles"

# Offline estimate failures (the New chart page tests). The EPSG:3395 source is Yandex/YaSat.zmp from CG_MAPS_SRC,
# copied into the scratch maps folder, never written to.
MSRC=${CG_MAPS_SRC:?set CG_MAPS_SRC to a maps folder that holds Yandex/YaSat.zmp}
mkdir -p "$S/E-maps/Yandex"
cp -r "$MSRC/Yandex/YaSat.zmp" "$S/E-maps/Yandex/"
EST=(estimate --source=5E04039A-331E-4D2C-B051-B00F8871BB39 --bbox=-1.6,50.6,-1.0,50.9 "--maps=$S/E-maps" "${COMMON[@]}")
cap estimate-refused-3395 "$P" "${EST[@]}" --zooms=9-12
cap usage-bad-zooms "$P" "${EST[@]}" --zooms=x
cap usage-unknown-option "$P" "${EST[@]}" --zooms=9-12 --nonsense=1

# The run / weather records the tests read (network, polite: small areas, low zooms; times change per capture).
# All use --maps <repo>/data/sources. The two jobs below are job.ini files as the plugin writes them (spec 8.1).
RUNMAPS=("--maps=$SRC")
SEA='{AB8A8685-AEC6-4E16-80DA-DC4AC37E4777}'
RAIN='{1A696D82-F258-41A4-BCD6-7648D9E08DE2}'
GEOCOLOUR='{FE84A175-2C0F-420F-86DF-5900DE16A33C}'
BOX=3.00000000,51.00000000,8.00000000,55.00000000
mkdir -p "$S/R-cache" "$S/R-data" "$S/charts/My Charts" "$S/job" "$S/job-layer" "$S/job-base" "$S/out"
RC=("--cache=$S/R-cache" "--datadir=$S/R-data")
cat >"$S/job/job.ini" <<EOJ
[Job]
Source={7B743985-BC5F-4AB6-8915-AC5DBBB8F552}
Overlay=$SEA
BBox=-1.60000000,50.60000000,-1.00000000,50.90000000
Zooms=9-13
Out=$S/charts/My Charts/Solent - ESRI.mbtiles
OverlayOut=$S/charts/My Charts/Solent - Seamarks.mbtiles
Name=Solent - ESRI
[Download]
Threads=1
EOJ
# CLI 2026.10.8
# run-stdin-eof-at-once: the job of run-stopped, stdin closed at once (EOF = stop); its OWN cache and datadir
# so the session of run-stopped is untouched; it runs BEFORE run-stopped (the job output must not exist yet). Exit 143 and the stop line, before the first tile.
mkdir -p "$S/E0-cache" "$S/E0-data"
cap run-stdin-eof-at-once bash -c 'true | "$@"' _ "$P" run --job "$S/job/job.ini" --format=tsv --progress=lines \
    --stop-on-stdin "${RUNMAPS[@]}" "--cache=$S/E0-cache" "--datadir=$S/E0-data"
# run-stopped: the plugin's way of stopping: --stop-on-stdin, stdin a pipe that stays open; the line `stop` after
# 20 s stops the run (exit 143), no kill. Then run-continued: the same command again without the option, to the end.
{ sleep 20; echo stop; sleep 60; } | LC_ALL=C.UTF-8 "$P" run --job "$S/job/job.ini" --format=tsv --progress=lines \
    --stop-on-stdin "${RUNMAPS[@]}" "${RC[@]}" >"$S/run-stopped.out" 2>"$S/run-stopped.err"
echo ${PIPESTATUS[1]} >"$S/run-stopped.exit"
kept run-stopped || for e in out:stdout err:stderr exit:exit; do sed "s|$S|\$SCRATCH|g" "$S/run-stopped.${e%%:*}" >"$OUT/run-stopped.${e##*:}"; done
cap run-continued "$P" run --job "$S/job/job.ini" --format=tsv --progress=lines "${RUNMAPS[@]}" "${RC[@]}"

printf '[Job]\nOverlay=%s\nBBox=%s\nZooms=5-6\nOut=%s/out/North Sea - Weather.mbtiles\n[Download]\nThreads=2\n' \
    "$RAIN" "$BOX" "$S" >"$S/job-layer/job.ini"
printf '[Job]\nSource=%s\nOverlay=%s\nBBox=%s\nZooms=4\nOut=%s/out/Clouds.mbtiles\nOverlayOut=%s/out/Clouds - Seamarks.mbtiles\nName=Clouds\n[Download]\nThreads=2\n' \
    "$GEOCOLOUR" "$SEA" "$BOX" "$S" "$S" >"$S/job-base/job.ini"
cap estimate-weather-maxzoom "$P" estimate --format=tsv --progress=lines --overlay="$RAIN" --bbox="$BOX" --zooms=5-8 \
    "${RUNMAPS[@]}" "${COMMON[@]}"
cap run-weather-layer "$P" run --job "$S/job-layer/job.ini" --format=tsv --progress=lines "${RUNMAPS[@]}" "${COMMON[@]}"
cap run-weather-base-seamarks "$P" run --job "$S/job-base/job.ini" --format=tsv --progress=lines "${RUNMAPS[@]}" "${COMMON[@]}"
# no answer from the index: a proxy on the discard port of the loopback (nothing listens there)
cap run-weather-snapshot-failed "$P" run --job "$S/job-layer/job.ini" --format=tsv --progress=lines --overwrite \
    --proxy=http://127.0.0.1:9 "${RUNMAPS[@]}" "${COMMON[@]}"

# The four CLI behaviours of 2026.10.5 / 2026.10.6.
# sources-first-start-note: NO --datadir; HOME holds the old SAS.Planet config and data dirs, so the CLI prints one `note:` line.
mkdir -p "$S/home-note/.config/SAS.Planet" "$S/home-note/.local/share/SAS.Planet"
mkdir -p "$S/Nt"
cp "$SRC/Coast and land/OpenTopoMap.source.json" "$SRC/Seamarks and traffic/OpenSeaMap-Seamarks.source.json" "$SRC/Weather now/RainViewer-Radar.source.json" "$S/Nt/"
HOME=$S/home-note cap sources-first-start-note "$P" sources --format=tsv --maps "$S/Nt"
# run-cache-refused: --cache names a regular file. `run` refuses it offline, before any request; `estimate` writes
# nothing and does not refuse it (exit 0).
echo x >"$S/a-file"
OTM=$(sed -n 's/.*"guid": "\(.*\)".*/\1/p' "$SRC/Coast and land/OpenTopoMap.source.json")
cap run-cache-refused "$P" run --source="$OTM" --bbox=-1.6,50.6,-1.0,50.9 --zooms=9-10 "${RUNMAPS[@]}" \
    "--cache=$S/a-file" "--datadir=$S/data" "--out=$S/out/refused.mbtiles"
# estimate-area-too-large: the world at zoom 16 (offline)
cap estimate-area-too-large "$P" estimate --source="$OTM" --bbox=-180,-85,180,85 --zooms=16 "${RUNMAPS[@]}" "${COMMON[@]}"
# run-connection-lost: a proxy on the discard port; three failed tiles in a row stop the run (exit 4)
cap run-connection-lost "$P" run --source="$OTM" --bbox=-1.6,50.6,-1.0,50.9 --zooms=10 --threads=1 \
    --max-connection-errors=3 --proxy=http://127.0.0.1:9 --format=tsv --progress=lines "${RUNMAPS[@]}" \
    "--cache=$S/R-cache-lost" "--datadir=$S/data" "--out=$S/out/lost.mbtiles"

# The offline cases of CLI 2026.10.8: not a pipe, BerkeleyDB.
# usage-stdin-not-a-pipe (offline): stdin is /dev/null, not a pipe: exit 2
cap usage-stdin-not-a-pipe "$P" estimate --source="$OTM" --bbox=-180,-85,180,85 --zooms=16 --stop-on-stdin "${RUNMAPS[@]}" \
    "${COMMON[@]}" </dev/null
# run-cache-berkeleydb (offline): a copy of OpenTopoMap with a new GUID, a new name and CacheType=6 (BerkeleyDB)
mkdir -p "$S/BDB/Topo/BerkeleyDB.zmp"
sed -e 's/^GUID=.*/GUID={D0C0FFEE-0000-4000-8000-000000000006}\r/' -e 's/^name=.*/name=BerkeleyDB copy\r/' \
    "$ZMP/OpenTopoMap.zmp/params.txt" >"$S/BDB/Topo/BerkeleyDB.zmp/params.txt"
printf 'CacheType=6\r\n' >>"$S/BDB/Topo/BerkeleyDB.zmp/params.txt"
# the cache type 6 path is relative to the working directory: run from $S so the line says $SCRATCH/Osm_Topo_Map/
(cd "$S" && cap run-cache-berkeleydb "$P" run --source='{D0C0FFEE-0000-4000-8000-000000000006}' --bbox=-1.6,50.6,-1.0,50.9 --zooms=9 \
    "--maps=$S/BDB" "${COMMON[@]}" "--out=$S/out/bdb.mbtiles" --format=tsv --progress=lines)

# The new snapshot modes of CLI 2026.10.9 (network, polite: one source, a few tiles). Times change per capture.
WARN='{901E0ED9-78CD-41C4-BA6A-0E93CF08C4F0}'
CURW='{F022DF76-6F80-4294-9403-F1A6123EDD80}'
UA='Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:146.0) Gecko/20100101 Firefox/146.0'
mkdir -p "$S/W-cache" "$S/W-data" "$S/job-refetch" "$S/job-nearest"
printf '[Job]\nOverlay=%s\nBBox=-82,24,-79,27\nZooms=5\nOut=%s/out/Warnings.mbtiles\n[Download]\nThreads=1\n' \
    "$WARN" "$S" >"$S/job-refetch/job.ini"
printf '[Job]\nOverlay=%s\nBBox=-80,25,-77,28\nZooms=5\nOut=%s/out/Currents.mbtiles\n[Download]\nThreads=1\n' \
    "$CURW" "$S" >"$S/job-nearest/job.ini"
# run-weather-refetch: the NWS warnings (none-refetch) run TWICE into the same cache; the files are the SECOND run
WC=("--cache=$S/W-cache" "--datadir=$S/W-data")
"$P" run --job "$S/job-refetch/job.ini" --format=tsv --progress=lines "${RUNMAPS[@]}" "${WC[@]}" >"$S/first.out" 2>/dev/null
echo $? >"$S/run-weather-refetch.first-exit"
if [ "$(cat "$S/run-weather-refetch.first-exit")" != 0 ]; then
    echo "capture.sh: the first refetch run exited $(cat "$S/run-weather-refetch.first-exit"); stop" >&2
    exit 1
fi
grep '^snapshot' "$S/first.out" >"$S/first.snapshot"
kept run-weather-refetch || {
    cp "$S/first.snapshot" "$OUT/run-weather-refetch.first-snapshot"
    cp "$S/run-weather-refetch.first-exit" "$OUT/run-weather-refetch.first-exit"
}
sleep 5
cap run-weather-refetch "$P" run --job "$S/job-refetch/job.ini" --format=tsv --progress=lines --overwrite "${RUNMAPS[@]}" "${WC[@]}"
# run-weather-nearest-now: the western Atlantic currents (nearest-now); right before, the capabilities time dimension of the index URL
curl -s -A "$UA" 'https://nowcoast.noaa.gov/geoserver/grtofs/rtofs_east_sfc_currents/ows?service=WMS&version=1.3.0&request=GetCapabilities' |
    grep -o '<Dimension name="time"[^>]*>[^<]*' | cut -c1-300 >"$S/nn.capabilities"
date -u +%Y-%m-%dT%H:%M:%SZ >"$S/nn.time"
kept run-weather-nearest-now || {
    cp "$S/nn.capabilities" "$OUT/run-weather-nearest-now.capabilities"
    cp "$S/nn.time" "$OUT/run-weather-nearest-now.time"
}
cap run-weather-nearest-now "$P" run --job "$S/job-nearest/job.ini" --format=tsv --progress=lines "${RUNMAPS[@]}" "${WC[@]}"

# The model run is field 4 of `snapshot` (RTOFS currents, Western Atlantic, network)
mkdir -p "$S/job-rtofs" "$S/R-out"
printf '[Job]\nOverlay=%s\nBBox=-75,35,-70,38\nZooms=3\nOut=%s/out/Atlantic - Currents.mbtiles\n[Download]\nThreads=1\n' \
    "$CURW" "$S" >"$S/job-rtofs/job.ini"
cap run-rtofs-snapshot "$P" run --job "$S/job-rtofs/job.ini" --format=tsv --progress=lines "${RUNMAPS[@]}" \
    "--cache=$S/RT-cache" "--datadir=$S/RT-data"

# CLI 2026.11.1: a second run of the same job is refused with exit 3 and prints no `phase download`.
# The first run (the job of run-stopped, its OWN cache and datadir, stopped after 10 s through stdin) holds the session lock.
mkdir -p "$S/B-cache" "$S/B-data" "$S/B-out"
sed "s|$S/charts/My Charts|$S/B-out|" "$S/job/job.ini" >"$S/B-job.ini"
BC=("--cache=$S/B-cache" "--datadir=$S/B-data")
{ sleep 10; echo stop; sleep 20; } | "$P" run --job "$S/B-job.ini" --format=tsv --progress=lines --stop-on-stdin "${RUNMAPS[@]}" "${BC[@]}" >/dev/null 2>&1 &
FIRST=$!
sleep 4
cap run-refused-busy "$P" run --job "$S/B-job.ini" --format=tsv --progress=lines "${RUNMAPS[@]}" "${BC[@]}"
wait "$FIRST"
