# Fixtures: the output of the pinned CLI 2026.10.8

Captured 2026-10-21 by `capture.sh` (bash, not run by the gate). The binary is
`<pin-dir>/2026.10.8-e0a207da/chart-grabber-cli` (`version.stdout`:
`chart-grabber-cli 2026.10.8 (e0a207dadb59)`). Scratch HOME, cache and datadir; the scratch directory is replaced by the
literal `$SCRATCH`; `LC_ALL=C.UTF-8`. All cases are real and unedited otherwise.

Folder `A` = `OpenTopoMap` (`data/sources/Coast and land`), `OpenSeaMap-Seamarks` (`Seamarks and traffic`), `RainViewer-Radar` (`Weather now`); folder `B` = a copy of
`OpenTopoMap.zmp` named `Copy-Topo.zmp` (the SAME GUID) and `Esri-World-Imagery.zmp`.

| Case | Command | Exit |
|---|---|---|
| `version` | `--version` | 0 |
| `sources-two-folders` | `sources --format=tsv --maps A --maps B`: 4 rows of 15 columns, field 10 (`origin`) `user` on all, the last column (`navigationWarning`) `0`; the duplicate GUID gives the later folder's row (`Copy-Topo.zmp`); stderr's first line `warning: map source {51C1079B-...}: $SCRATCH/B/Copy-Topo.zmp replaces $SCRATCH/A/OpenTopoMap.zmp` | 0 |
| `sources-no-maps` | `sources --format=tsv` (no `--maps`): stdout empty, stderr `error: no map sources given: pass --maps DIR, ...` | 3 |
| `sources-navigation-warning` | `sources --format=tsv --maps N-bundled --maps N-sailor`: bundled = `OpenTopoMap`; sailor folder = `Flagged.zmp`, a copy of OpenTopoMap with a new GUID, the name `Flagged copy` and `NavigationWarning=1` added; 2 rows, the last column `1` for `Flagged copy`, `0` for OpenTopoMap | 0 |
| `sources-bundled-all` | `sources --format=tsv --maps <repo>/data/sources`: 24 rows in seven groups, the last column `1` for exactly the seven sources of `Nautical charts` and `Depth`, `0` for the other 17 | 0 |
| `run-navigation-warning` | `run --source={C44877A0-...} --bbox=23.5,37.9,23.7,38.0 --zooms=6-7 --format=tsv --progress=lines --maps <repo>/data/sources` into an EMPTY cache (network): stderr has the `warning: "EMODnet Bathymetry (world)": not for navigation. ...` line once, after `phase estimate` and before the first `progress download` | 0 |
| `export-navigation-warning` | the same job as `export` from the cache the run filled: no navigation line | 0 |
| `estimate-refused-3395` | `estimate --source=5E04039A-... --bbox=-1.6,50.6,-1.0,50.9 --zooms=9-12 --maps <scratch>/E-maps`: stdout empty, stderr `error: the projection of "Yandex - Satellite" is EPSG:3395; only EPSG:3857 ...` | 3 |
| `usage-bad-zooms` | the same with `--zooms=x`: stderr `error: --zooms: "x" is not a zoom list ...` and the `Usage:` line | 2 |
| `usage-unknown-option` | the same with `--zooms=9-12 --nonsense=1`: stderr `error: Unknown option 'nonsense'.` and the `Usage:` line | 2 |
| `run-stopped` | `run --job job.ini --format=tsv --progress=lines --stop-on-stdin --maps <repo>/data/sources`, job: base Esri World Imagery `{7B743985-...}` + overlay OpenSeaMap Seamarks `{AB8A8685-...}`, bbox `-1.6,50.6,-1.0,50.9`, zooms 9-13, threads 1; stdin a pipe from `{ sleep 20; echo stop; sleep 60; }` (the LINE stops it, the pipe stays open; no kill): the seamark download is interrupted, stderr ends with the two `warning:` lines (session saved, how to continue) and then `chart-grabber-cli: stop requested on stdin; exit 143` | 143 |
| `run-continued` | the same command again WITHOUT `--stop-on-stdin`, to the end: phases, progress, `fill` sums, `written` of both files; stderr has `warning: continuing the session of <date time>` after `phase download` of the seamarks | 0 |
| `estimate-weather-maxzoom` | `estimate --format=tsv --progress=lines --overlay={1A696D82-...} --bbox=3,51,8,55 --zooms=5-8 --maps <repo>/data/sources`: estimate records of zooms 5-7 only (RainViewer stops at 7), stderr `warning: ... zooms 8 skipped ...`; no `snapshot` record | 0 |
| `run-weather-layer` | `run --job job-layer/job.ini`: the `snapshot` record before the `estimate` records, `written` field 5 = the snapshot time, stderr `warning: --threads 2 lowered to 1 ...` | 0 |
| `run-weather-base-seamarks` | `run --job job-base/job.ini`: `snapshot` of the base only, two `written` records, the base's field 5 set, the seamark overlay's field 5 EMPTY (a trailing TAB) | 0 |
| `run-weather-snapshot-failed` | `run --job job-layer/job.ini --overwrite --proxy=http://127.0.0.1:9`: stdout empty, stderr the one line `error: cannot take the snapshot of "RainViewer radar (worldwide, current)": no answer from https://api.rainviewer.com/public/weather-maps.json` | 4 |
| `sources-first-start-note` | `sources --format=tsv --maps Nt` WITHOUT `--datadir`; `HOME` = a fresh dir holding empty `.config/SAS.Planet/` and `.local/share/SAS.Planet/`: stdout the 3 records (OpenSeaMap Seamarks, OpenTopoMap, RainViewer), stderr one `note: moved ... to ...` line first, then the terms-of-use `warning:` of OpenTopoMap | 0 |
| `run-cache-refused` | `run`, not `estimate`: `estimate` with `--cache=<a regular file>` exits 0 and writes nothing. The refusal is in `run` (offline, before any request): `run --source={51C1079B-...} --bbox=-1.6,50.6,-1.0,50.9 --zooms=9-10 --maps <repo>/data/sources --cache=<scratch>/a-file --out=...`: stderr the one line `error: "OpenTopoMap": cannot write the cache folder $SCRATCH/a-file/cache_sqlite/Osm_Topo_Map/` (the `cannot write the cache folder` form; `cannot open the cache` not produced) | 3 |
| `estimate-area-too-large` | `estimate --source={51C1079B-...} --bbox=-180,-85,180,85 --zooms=16 --maps <repo>/data/sources` (offline): stderr `error: the area is too large at zoom 16 (0-based): more than 2147483647 tiles; choose a smaller area or lower zooms` | 3 |
| `run-connection-lost` | `run --source={51C1079B-...} --bbox=-1.6,50.6,-1.0,50.9 --zooms=10 --threads=1 --max-connection-errors=3 --proxy=http://127.0.0.1:9 --format=tsv --progress=lines` (nothing listens on the proxy port, no request leaves the machine; the proxy refusal IS counted as a connection error, no scratch ZMP needed): stderr has `error: no connection to the server for 3 tiles in a row: Error connecting to server` followed by a non-record line `Wait 5 seconds...`, one more `progress`, the two `warning: session saved ...` lines and `phase	fill` - the `error:` line is NOT the last stderr line | 4 |
| `run-stdin-eof-at-once` | the job of `run-stopped` with `--stop-on-stdin`, stdin `true \|` (EOF at once = stop), its OWN `--cache $SCRATCH/E0-cache` / `--datadir $SCRATCH/E0-data`: three `progress download` records with 0 tiles, then the two `warning:` session lines and `chart-grabber-cli: stop requested on stdin; exit 143`; no tile downloaded | 143 |
| `usage-stdin-not-a-pipe` | the `estimate-area-too-large` command with `--stop-on-stdin` and `</dev/null`: stderr `error: --stop-on-stdin needs stdin to be a pipe` and the `Usage:` line | 2 |
| `run-cache-berkeleydb` | `run --source={D0C0FFEE-...0006} --bbox=-1.6,50.6,-1.0,50.9 --zooms=9 --maps $SCRATCH/BDB --format=tsv --progress=lines`, run with the working directory = the scratch dir; `BDB/Topo/BerkeleyDB.zmp` = a copy of `OpenTopoMap.zmp` with that GUID, `name=BerkeleyDB copy` and an appended `CacheType=6` line (the `Flagged.zmp` way): stderr the one line `error: "BerkeleyDB copy": cannot open the cache: $SCRATCH/Osm_Topo_Map/ (cache type 6): BerkeleyDB caches (cache types 6 and 61) are no longer supported` | 3 |
| `run-phase-without-progress.stderr` | HAND-MADE (core `download_runner.cpp:511-512`): `phase download` of A with no `progress`, then the export phase. The CLI cannot produce it | - |
| `resume-refused-b.stderr` | HAND-MADE (moved unchanged from the older fixture directory): `phase download` A, `phase download` B, `error: cannot continue <file of B>: ...`, then `progress download` of A. The plugin never runs `resume`; defensive | - |
| `progress-empty-guid.stderr` | HAND-MADE (moved unchanged from the older fixture directory): a `progress download` with an empty GUID field, then one with a GUID. 2026.10.4 never prints an empty GUID; the case stays defensive | - |

The hand-made files have no `.stdout` or `.exit`. `sources-bundled-all` also serves the tests that need the two EMODnet
vessel-density rows (group `Seamarks and traffic`, kind `layer`, `maxZoom` 9) and the weather rows (group `Weather now`). The network cases (run-navigation-warning,
run-stopped, run-continued, the weather runs) were captured 2026-10-21 on 2026.10.8 (ESRI, OpenSeaMap, RainViewer, EUMETView, EMODnet; polite: the
areas and zooms above); `run-connection-lost`, `run-cache-refused`, `estimate-area-too-large` send no request. `estimate-refused-3395`
and `usage-bad-zooms` need the owner's `YaSat.zmp` (copied, never written to); the 2026.10.8 re-run produced the same text as 2026.10.4.

## MBTiles metadata of the weather runs
Read with `sqlite3 <file> "select name,value from metadata where name in ('name','description')"` on the files the 2026-10-21 capture wrote:
- `run-weather-layer` (`North Sea - Weather.mbtiles`): `name` = `RainViewer radar (worldwide, current) 2026-10-21 17:40 UTC` (ends with the frame time),
  `description` = `RainViewer radar (worldwide, current), snapshot 2026-10-21 17:40 UTC`.
- `run-weather-base-seamarks` base (`Clouds.mbtiles`): `name` = `Clouds` (given, no time), `description` = `EUMETSAT MTG GeoColour (Europe, Africa, Atlantic; clouds day and night), snapshot 2026-10-21 17:10 UTC`.
- the seamark overlay (`Clouds - Seamarks.mbtiles`): `name` = `OpenSeaMap Seamarks`, `description` = `Created by chart-grabber-cli 2026.10.8`.

## Cosmetic wording, not a fixture
`sources` on the BerkeleyDB copy (exit 0) prints the row `source	{D0C0FFEE-...0006}	map	BerkeleyDB copy	image/png	3857	6	the download subsystem of this map is disabled	...`:
the `sources` listing still words the refusal as "download subsystem of this map is disabled", while `run` says "BerkeleyDB caches ... are no longer supported".
The Tech Lead ruled the wording cosmetic; the plugin reads neither.

## re-capture (2026-10-22)
The regroup of `data/sources` (seven groups, 24 sources; paths in `capture.sh` follow the moves) changes the `group` field (10) and the
file path field (9) of the rows. Re-captured with the same commands from the same pinned CLI: `sources-bundled-all`, `sources-two-folders`,
`sources-navigation-warning` and `sources-first-start-note` (the four `sources-*` cases that read `data/sources`; stderr and exit files
did not change). Every network case (`run-*`, `export-navigation-warning`, `estimate-weather-maxzoom`) and every offline error case stays as
captured on 2026-10-21 (their text names no group).
