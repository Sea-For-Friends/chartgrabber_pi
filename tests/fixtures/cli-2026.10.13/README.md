# Fixtures: the output of the pinned CLI 2026.10.13

Captured 2026-10-23 by `capture.sh` (bash, not run by the gate). The binary is
`<pin-dir>/2026.10.13-a970b543/chart-grabber-cli-2026.10.13-linux-x86_64/chart-grabber-cli` (the unpacked Linux x86_64 bundle; `version.stdout`:
`chart-grabber-cli 2026.10.13 (a970b543798b)`). Scratch HOME, cache and datadir; the scratch directory is replaced by the
literal `$SCRATCH`; `LC_ALL=C.UTF-8`. All cases are real and unedited otherwise, except the HAND-MADE ones marked below.

(2026-10-30): `capture.sh` was run WITHOUT `--all` on 2026.10.13 (the x86_64 bundle), WITHOUT the `/etc/fonts` workaround: no capture file holds a "Fontconfig" line (the bundle uses its own fontconfig since 2026.10.13), so the workaround is dropped. Only `version.stdout` changed (the version and commit); every other re-captured file is byte-identical to the 2026.10.12 one; the nine KEEP network cases are untouched. The scratch HOME of the run holds `.cache/chart-grabber/fontroot`: `capture.sh` runs `version` without `--datadir` (the release note: that is where the CLI then keeps its fontconfig sysroot).

(2026-10-29, on 2026.10.12; the workaround below is no longer needed): `capture.sh` was run WITHOUT `--all` on 2026.10.12 (the x86_64 bundle; no other binary exists in this release). The host's fontconfig files are newer than the bundled libfontconfig reads and it prints "Fontconfig warning" lines on stderr; the run was made with `/etc/fonts` and `/usr/share/fontconfig` hidden (`unshare -Urm`, bind mounts of an empty directory) so that the bundle uses its own `fonts/fonts.conf` and no such line is captured. Only `version.stdout` changed (the version and commit); every other re-captured file is byte-identical to the 2026.10.10 one; the nine KEEP network cases are untouched.

(2026-10-23): `capture.sh` was run WITHOUT `--all` on 2026.10.10. Only `version.stdout` changed (the version and commit); every other re-captured
file is byte-identical to the 2026.10.9 one. The nine KEEP network cases (see "Recapturing") are 2026.10.9 output kept by the rule.

Folder `A` = `OpenTopoMap` (`data/sources/Coast and land`), `OpenSeaMap-Seamarks` (`Seamarks and traffic`), `RainViewer-Radar` (`Weather now`); folder `B` = a copy of
`OpenTopoMap.source.json` named `Copy-Topo.source.json` (the SAME GUID) and `Esri-World-Imagery.source.json`. The bundled set is `*.source.json`. Only the `sources-*` files were taken from the run; the `run-*` files that carry live network numbers (sizes, snapshot times) stay from the capture of the same CLI (`run` prints no file names, so the JSON set cannot change them). The sailor-folder cases (`Flagged.zmp`, `BerkeleyDB.zmp`) are made from the frozen `../sources-zmp/OpenTopoMap.zmp/params.txt`.

| Case | Command | Exit |
|---|---|---|
| `version` | `--version` | 0 |
| `sources-two-folders` | `sources --format=tsv --maps A --maps B`: 4 rows of 15 columns, field 10 (`origin`) `user` on all, the last column (`navigationWarning`) `0`; the duplicate GUID gives the later folder's row (`Copy-Topo.source.json`); stderr's first line `warning: map source {51C1079B-...}: $SCRATCH/B/Copy-Topo.source.json replaces $SCRATCH/A/OpenTopoMap.source.json` | 0 |
| `sources-no-maps` | `sources --format=tsv` (no `--maps`): stdout empty, stderr `error: no map sources given: pass --maps DIR, ...` | 3 |
| `sources-navigation-warning` | `sources --format=tsv --maps N-bundled --maps N-sailor`: bundled = `OpenTopoMap`; sailor folder = `Flagged.zmp`, a copy of OpenTopoMap with a new GUID, the name `Flagged copy` and `NavigationWarning=1` added; 2 rows, the last column `1` for `Flagged copy`, `0` for OpenTopoMap | 0 |
| `sources-bundled-all` | `sources --format=tsv --maps <repo>/data/sources`: 28 rows in seven groups, the last column `1` for exactly the seven sources of `Nautical charts` and `Depth`, `0` for the other 21 | 0 |
| `run-navigation-warning` | `run --source={C44877A0-...} --bbox=23.5,37.9,23.7,38.0 --zooms=6-7 --format=tsv --progress=lines --maps <repo>/data/sources` into an EMPTY cache (network): stderr has the `warning: "EMODnet Bathymetry (world)": not for navigation. ...` line once, after `phase estimate` and before the first `progress download` | 0 |
| `export-navigation-warning` | the same job as `export` from the cache the run filled: no navigation line | 0 |
| `estimate-refused-3395` | `estimate --source=5E04039A-... --bbox=-1.6,50.6,-1.0,50.9 --zooms=9-12 --maps <scratch>/E-maps`: stdout empty, stderr `error: the projection of "Yandex - Satellite" is EPSG:3395; only EPSG:3857 ...` | 3 |
| `usage-bad-zooms` | the same with `--zooms=x`: stderr `error: --zooms: "x" is not a zoom list ...` and the `Usage:` line | 2 |
| `usage-unknown-option` | the same with `--zooms=9-12 --nonsense=1`: stderr `error: Unknown option 'nonsense'.` and the `Usage:` line | 2 |
| `run-stopped` | `run --job job.ini --format=tsv --progress=lines --stop-on-stdin --maps <repo>/data/sources`, job: base Esri World Imagery `{7B743985-...}` + overlay OpenSeaMap Seamarks `{AB8A8685-...}`, bbox `-1.6,50.6,-1.0,50.9`, zooms 9-13, threads 1; stdin a pipe from `{ sleep 20; echo stop; sleep 60; }` (the LINE stops it, the pipe stays open; no kill): the seamark download is interrupted, stderr ends with the two `warning:` lines (session saved, how to continue) and then `chart-grabber-cli: stop requested on stdin; exit 143` | 143 |
| `run-continued` | the same command again WITHOUT `--stop-on-stdin`, to the end: phases, progress, `fill` sums, `written` of both files; stderr has `warning: continuing the session of <date time>` after `phase download` of the seamarks | 0 |
| `estimate-weather-maxzoom` | `estimate --format=tsv --progress=lines --overlay={1A696D82-...} --bbox=3,51,8,55 --zooms=5-8 --maps <repo>/data/sources`: estimate records of zooms 5-7 only (RainViewer stops at 7), stderr `warning: ... zooms 8 skipped ...`; no `snapshot` record | 0 |
| `run-weather-layer` | `run --job job-layer/job.ini` (overlay-only RainViewer, `Zooms=5-6`, area `BBox=3,51,8,55`, threads 2; , network): the `snapshot` record before the `estimate` records, `written` field 5 = the snapshot time, stderr `warning: --threads 2 lowered to 1 ...` | 0 |
| `run-weather-refetch` | `run --job job-refetch/job.ini --overwrite` (overlay-only NWS warnings `{901E0ED9-...}`, `none-refetch`, `BBox=-82,24,-79,27`, `Zooms=5`, 1 thread; , network) run TWICE into the same `--cache`; the files are the SECOND run. `run-weather-refetch.first-snapshot` is the first run's `snapshot` line. Times change per capture | 0 |
| `run-weather-nearest-now` | `run --job job-nearest/job.ini` (overlay-only RTOFS western Atlantic currents `{F022DF76-...}`, `nearest-now`, `BBox=-80,25,-77,28`, `Zooms=5`; network). Right before it `capture.sh` saved the capabilities `Dimension name="time"` line of the index URL (`.capabilities`, cut to 300 characters: the default and the first instants) and the UTC time (`.time`) | 0 |
| `run-weather-base-seamarks` | `run --job job-base/job.ini` (base EUMETSAT GeoColour `{FE84A175-...}` + overlay seamarks, `Zooms=4`; , network): `snapshot` of the base only, two `written` records, the base's field 5 set, the seamark overlay's field 5 EMPTY (a trailing TAB) | 0 |
| `run-weather-snapshot-failed` | `run --job job-layer/job.ini --overwrite --proxy=http://127.0.0.1:9`: stdout empty, stderr the one line `error: cannot take the snapshot of "RainViewer radar (worldwide, current)": no answer from https://api.rainviewer.com/public/weather-maps.json` | 4 |
| `sources-first-start-note` | `sources --format=tsv --maps Nt` WITHOUT `--datadir`; `HOME` = a fresh dir holding empty `.config/SAS.Planet/` and `.local/share/SAS.Planet/`: stdout the 3 records (OpenSeaMap Seamarks, OpenTopoMap, RainViewer), stderr one `note: moved ... to ...` line first, then the terms-of-use `warning:` of OpenTopoMap | 0 |
| `run-cache-refused` | `run`, not `estimate`: `estimate` with `--cache=<a regular file>` exits 0 and writes nothing. The refusal is in `run` (offline, before any request): `run --source={51C1079B-...} --bbox=-1.6,50.6,-1.0,50.9 --zooms=9-10 --maps <repo>/data/sources --cache=<scratch>/a-file --out=...`: stderr the one line `error: "OpenTopoMap": cannot write the cache folder $SCRATCH/a-file/cache_sqlite/Osm_Topo_Map/` (the `cannot write the cache folder` form; `cannot open the cache` not produced) | 3 |
| `estimate-area-too-large` | `estimate --source={51C1079B-...} --bbox=-180,-85,180,85 --zooms=16 --maps <repo>/data/sources` (offline): stderr `error: the area is too large at zoom 16 (0-based): more than 2147483647 tiles; choose a smaller area or lower zooms` | 3 |
| `run-connection-lost` | `run --source={51C1079B-...} --bbox=-1.6,50.6,-1.0,50.9 --zooms=10 --threads=1 --max-connection-errors=3 --proxy=http://127.0.0.1:9 --format=tsv --progress=lines` (nothing listens on the proxy port, no request leaves the machine; the proxy refusal IS counted as a connection error, no scratch ZMP needed): stderr has `error: no connection to the server for 3 tiles in a row: Error connecting to server` followed by a non-record line `Wait 5 seconds...`, one more `progress`, the two `warning: session saved ...` lines and `phase	fill` - the `error:` line is NOT the last stderr line | 4 |
| `run-stdin-eof-at-once` | the job of `run-stopped` with `--stop-on-stdin`, stdin `true \|` (EOF at once = stop), its OWN `--cache $SCRATCH/E0-cache` / `--datadir $SCRATCH/E0-data`: three `progress download` records with 0 tiles, then the two `warning:` session lines and `chart-grabber-cli: stop requested on stdin; exit 143`; no tile downloaded | 143 |
| `usage-stdin-not-a-pipe` | the `estimate-area-too-large` command with `--stop-on-stdin` and `</dev/null`: stderr `error: --stop-on-stdin needs stdin to be a pipe` and the `Usage:` line | 2 |
| `run-cache-berkeleydb` | `run --source={D0C0FFEE-...0006} --bbox=-1.6,50.6,-1.0,50.9 --zooms=9 --maps $SCRATCH/BDB --format=tsv --progress=lines`, run with the working directory = the scratch dir; `BDB/Topo/BerkeleyDB.zmp` = a copy of `OpenTopoMap.zmp` with that GUID, `name=BerkeleyDB copy` and an appended `CacheType=6` line (the `Flagged.zmp` way): stderr the one line `error: "BerkeleyDB copy": cannot open the cache: $SCRATCH/Osm_Topo_Map/ (cache type 6): BerkeleyDB caches (cache types 6 and 61) are no longer supported` | 3 |
| `run-weather-stepback` | HAND-MADE. Derived from `run-weather-layer`: stdout = the same file with a SECOND `snapshot` record for the same GUID (time 10 min earlier, `2026-10-23T08:10:00Z`, its own version `/v2/radar/b7d19e03a4c8`) after the `estimate` records, and `written` field 5 = `2026-10-23T08:10:00Z` (the LAST record is the frame used, core `cli_main.cpp:449`). stderr = `run-weather-layer.stderr` plus, before `phase	fill`, the line `warning: the newest frame 2026-10-23 08:20 UTC of "RainViewer radar (worldwide, current)" is not served yet; using the previous frame 2026-10-23 08:10 UTC` (wording: core `cli_main.cpp:446-447`; time text `yyyy-MM-dd HH:mm UTC` from `snapshotTimeText`, `snapshot.cpp:770`). Where the second record falls among the other stdout lines is an assumption (stdout and stderr order cannot be seen from one capture); the plugin does not depend on it | 0 |
| `run-phase-without-progress.stderr` | HAND-MADE (core `download_runner.cpp:511-512`): `phase download` of A with no `progress`, then the export phase. The CLI cannot produce it | - |
| `resume-refused-b.stderr` | HAND-MADE (moved unchanged from the older fixture directory): `phase download` A, `phase download` B, `error: cannot continue <file of B>: ...`, then `progress download` of A. The plugin never runs `resume`; defensive | - |
| `progress-empty-guid.stderr` | HAND-MADE (moved unchanged from the older fixture directory): a `progress download` with an empty GUID field, then one with a GUID. 2026.10.4 never prints an empty GUID ; the case stays defensive | - |

The hand-made files have no `.stdout` or `.exit`. `sources-bundled-all` also serves the tests that need the two EMODnet
vessel-density rows (group `Seamarks and traffic`, kind `layer`, `maxZoom` 9) and the weather rows (group `Weather now`). The network cases (run-navigation-warning,
run-stopped, run-continued, the weather runs) were captured 2026-10-23 on 2026.10.9 (ESRI, OpenSeaMap, RainViewer, EUMETView, EMODnet; polite: the
areas and zooms above); `run-connection-lost`, `run-cache-refused`, `estimate-area-too-large` send no request. `estimate-refused-3395`
and `usage-bad-zooms` need a local `YaSat.zmp` (copied, never written to); the 2026.10.9 re-run produced the same text as 2026.10.4.

## MBTiles metadata of the weather runs
Read with `sqlite3 <file> "select name,value from metadata where name in ('name','description')"` on the files the 2026-10-23 capture wrote (the 2026.10.6 capture of had a `name` without the frame time; 2026.10.7 added it):
- `run-weather-layer` (`North Sea - Weather.mbtiles`): `name` = `RainViewer radar (worldwide, current) 2026-10-23 08:20 UTC` (ends with the frame time),
  `description` = `RainViewer radar (worldwide, current), snapshot 2026-10-23 08:20 UTC`.
- `run-weather-base-seamarks` base (`Clouds.mbtiles`): `name` = `Clouds` (given, no time), `description` = `EUMETSAT MTG GeoColour (Europe, Africa, Atlantic; clouds day and night), snapshot 2026-10-23 08:10 UTC`.
- the seamark overlay (`Clouds - Seamarks.mbtiles`): `name` = `OpenSeaMap Seamarks`, `description` = `Created by chart-grabber-cli 2026.10.9`.

## Cosmetic wording, not a fixture
`sources` on the BerkeleyDB copy (exit 0) prints the row `source	{D0C0FFEE-...0006}	map	BerkeleyDB copy	image/png	3857	6	the download subsystem of this map is disabled	...`:
the `sources` listing still words the refusal as "download subsystem of this map is disabled", while `run` says "BerkeleyDB caches ... are no longer supported".
The wording is cosmetic; the plugin reads neither.

## re-capture (2026-10-22)
The regroup of `data/sources` (seven groups, 24 sources; paths in `capture.sh` follow the moves) changes the `group` field (10) and the
file path field (9) of the rows. Re-captured with the same commands from the same pinned CLI: `sources-bundled-all`, `sources-two-folders`,
`sources-navigation-warning` and `sources-first-start-note` (the four `sources-*` cases that read `data/sources`; stderr and exit files
did not change). Every network case (`run-*`, `export-navigation-warning`, `estimate-weather-maxzoom`) and every offline error case stays as
captured on 2026-10-21 (their text names no group).

`run-weather-base-seamarks` was captured by a second run of the same job in a fresh cache and datadir: the first run (08:2x UTC) picked the EUMETView frame 08:00 whose tile was not served yet (9 connection errors, exit 5, `error: no tiles of Clouds in the cache for this area`); the next frame 08:10 worked. Not a fixture case here.

## Recapturing
`capture.sh [--all] SCRATCH_DIR`: by default the nine network cases that tests read as literals (`run-navigation-warning`,
`export-navigation-warning`, `run-stdin-eof-at-once`, `run-stopped`, `run-continued`, `run-weather-layer`, `run-weather-base-seamarks`,
`run-weather-refetch` with its `.first-*` files, `run-weather-nearest-now` with `.capabilities` and `.time`) are NOT overwritten when their
files exist, so a recapture does not churn the test literals. `--all` rewrites them (then the tests' literals must be re-read).
The first refetch run's exit code is saved as `run-weather-refetch.first-exit`; a non-zero code stops the script.

(2026-10-23): `capture.sh` was re-run; only `sources-bundled-all.stdout` (28 rows) and the two new cases were kept. The other
network cases stay as captured before (their text names no group; tests read literals from them).
