# Fixtures: the output of the pinned CLI 2026.10.6

Captured 2026-10-21 (P-0099) by `capture.sh` (bash, not run by the gate). The binary is
`<pin-dir>/2026.10.6-3016525c/chart-grabber-cli` (`version.stdout`:
`chart-grabber-cli 2026.10.6 (3016525c4521)`). Scratch HOME, cache and datadir; the scratch directory is replaced by the
literal `$SCRATCH`; `LC_ALL=C.UTF-8`. All cases are real and unedited otherwise.

Folder `A` = `OpenTopoMap`, `OpenSeaMap-Seamarks`, `RainViewer-Radar` of `data/sources`; folder `B` = a copy of
`OpenTopoMap.zmp` named `Copy-Topo.zmp` (the SAME GUID) and `Esri-World-Imagery.zmp`.

| Case | Command | Exit |
|---|---|---|
| `version` | `--version` | 0 |
| `sources-two-folders` | `sources --format=tsv --maps A --maps B`: 4 rows of 15 columns, field 10 (`origin`) `user` on all, the last column (`navigationWarning`) `0`; the duplicate GUID gives the later folder's row (`Copy-Topo.zmp`); stderr's first line `warning: map source {51C1079B-...}: $SCRATCH/B/Copy-Topo.zmp replaces $SCRATCH/A/OpenTopoMap.zmp` | 0 |
| `sources-no-maps` | `sources --format=tsv` (no `--maps`): stdout empty, stderr `error: no map sources given: pass --maps DIR, ...` | 3 |
| `sources-navigation-warning` | `sources --format=tsv --maps N-bundled --maps N-sailor` (P-0078): bundled = `OpenTopoMap`; sailor folder = `Flagged.zmp`, a copy of OpenTopoMap with a new GUID, the name `Flagged copy` and `NavigationWarning=1` added; 2 rows, the last column `1` for `Flagged copy`, `0` for OpenTopoMap | 0 |
| `sources-bundled-all` | `sources --format=tsv --maps <repo>/data/sources` (P-0078, after the data edit): 16 rows, the last column `1` for exactly the two EMODnet Bathymetry GUIDs, `0` for the other 14 | 0 |
| `run-navigation-warning` | `run --source={C44877A0-...} --bbox=23.5,37.9,23.7,38.0 --zooms=6-7 --format=tsv --progress=lines --maps <repo>/data/sources` into an EMPTY cache (network): stderr has the `warning: "EMODnet Bathymetry (world)": not for navigation. ...` line once, after `phase estimate` and before the first `progress download` | 0 |
| `export-navigation-warning` | the same job as `export` from the cache the run filled: no navigation line | 0 |
| `estimate-refused-3395` | `estimate --source=5E04039A-... --bbox=-1.6,50.6,-1.0,50.9 --zooms=9-12 --maps <scratch>/E-maps` (P-0085, offline; `Yandex/YaSat.zmp`, EPSG:3395, copied from the owner's `sas.maps`; captured 2026-10-18): stdout empty, stderr `error: the projection of "Yandex - Satellite" is EPSG:3395; only EPSG:3857 ...` | 3 |
| `usage-bad-zooms` | the same with `--zooms=x` (P-0085): stderr `error: --zooms: "x" is not a zoom list ...` and the `Usage:` line | 2 |
| `usage-unknown-option` | the same with `--zooms=9-12 --nonsense=1` (P-0085): stderr `error: Unknown option 'nonsense'.` and the `Usage:` line | 2 |
| `run-stopped` | `run --job job.ini --format=tsv --progress=lines --maps <repo>/data/sources`, job: base Esri World Imagery `{7B743985-...}` + overlay OpenSeaMap Seamarks `{AB8A8685-...}`, bbox `-1.6,50.6,-1.0,50.9`, zooms 9-13, threads 1; SIGTERM by pid after 20 s (P-0086, network; captured 2026-10-18): the seamark download is interrupted, stderr ends with the two `warning:` lines (session saved, how to continue) | 143 |
| `run-continued` | the same command again, to the end: phases, progress, `fill` sums, `written` of both files; stderr has `warning: continuing the session of <date time>` after `phase download` of the seamarks | 0 |
| `estimate-weather-maxzoom` | `estimate --format=tsv --progress=lines --overlay={1A696D82-...} --bbox=3,51,8,55 --zooms=5-8 --maps <repo>/data/sources` (P-0086): estimate records of zooms 5-7 only (RainViewer stops at 7), stderr `warning: ... zooms 8 skipped ...`; no `snapshot` record | 0 |
| `run-weather-layer` | `run --job job-layer/job.ini` (overlay-only RainViewer, `Zooms=5-6`, area `BBox=3,51,8,55`, threads 2; P-0086, network): the `snapshot` record before the `estimate` records, `written` field 5 = the snapshot time, stderr `warning: --threads 2 lowered to 1 ...` | 0 |
| `run-weather-base-seamarks` | `run --job job-base/job.ini` (base EUMETSAT GeoColour `{FE84A175-...}` + overlay seamarks, `Zooms=4`; P-0086, network): `snapshot` of the base only, two `written` records, the base's field 5 set, the seamark overlay's field 5 EMPTY (a trailing TAB) | 0 |
| `run-weather-snapshot-failed` | `run --job job-layer/job.ini --overwrite --proxy=http://127.0.0.1:9` (P-0086): stdout empty, stderr the one line `error: cannot take the snapshot of "RainViewer radar (worldwide, current)": no answer from https://api.rainviewer.com/public/weather-maps.json` | 4 |
| `sources-first-start-note` | `sources --format=tsv --maps Nt` WITHOUT `--datadir`; `HOME` = a fresh dir holding empty `.config/SAS.Planet/` and `.local/share/SAS.Planet/` (P-0099): stdout the 3 records (OpenSeaMap Seamarks, OpenTopoMap, RainViewer), stderr one `note: moved ... to ...` line first, then the terms-of-use `warning:` of OpenTopoMap | 0 |
| `estimate-cache-refused` | NOT `estimate` (P-0099 ticket said so): `estimate` with `--cache=<a regular file>` exits 0 and writes nothing. The refusal is in `run` (offline, before any request): `run --source={51C1079B-...} --bbox=-1.6,50.6,-1.0,50.9 --zooms=9-10 --maps <repo>/data/sources --cache=<scratch>/a-file --out=...`: stderr the one line `error: "OpenTopoMap": cannot write the cache folder $SCRATCH/a-file/cache_sqlite/Osm_Topo_Map/` (the `cannot write the cache folder` form; `cannot open the cache` not produced) | 3 |
| `estimate-area-too-large` | `estimate --source={51C1079B-...} --bbox=-180,-85,180,85 --zooms=16 --maps <repo>/data/sources` (offline): stderr `error: the area is too large at zoom 16 (0-based): more than 2147483647 tiles; choose a smaller area or lower zooms` | 3 |
| `run-connection-lost` | `run --source={51C1079B-...} --bbox=-1.6,50.6,-1.0,50.9 --zooms=10 --threads=1 --max-connection-errors=3 --proxy=http://127.0.0.1:9 --format=tsv --progress=lines` (nothing listens on the proxy port, no request leaves the machine; the proxy refusal IS counted as a connection error, no scratch ZMP needed): stderr has `error: no connection to the server for 3 tiles in a row: Error connecting to server` followed by a non-record line `Wait 5 seconds...`, one more `progress`, the two `warning: session saved ...` lines and `phase	fill` - the `error:` line is NOT the last stderr line | 4 |
| `run-phase-without-progress.stderr` | HAND-MADE (core `download_runner.cpp:511-512`): `phase download` of A with no `progress`, then the export phase (spec 8.8 R-a). The CLI cannot produce it | - |
| `resume-refused-b.stderr` | HAND-MADE (moved unchanged from the older fixture directory): `phase download` A, `phase download` B, `error: cannot continue <file of B>: ...`, then `progress download` of A (spec 8.8 R-a / R-b). The plugin never runs `resume`; defensive | - |
| `progress-empty-guid.stderr` | HAND-MADE (moved unchanged from the older fixture directory): a `progress download` with an empty GUID field, then one with a GUID (spec 8.8 R-c). 2026.10.4 never prints an empty GUID (core T-0231); the case stays defensive | - |

The hand-made files have no `.stdout` or `.exit`. `sources-bundled-all` also serves the tests that need the two EMODnet
vessel-density rows (group `Marine`, kind `layer`, `maxZoom` 9) and the weather rows. The network cases (run-navigation-warning,
run-stopped, run-continued, the weather runs) were captured 2026-10-21 (ESRI, OpenSeaMap, RainViewer, EUMETView, EMODnet; polite: the
areas and zooms above); `run-connection-lost`, `estimate-cache-refused`, `estimate-area-too-large` send no request. `estimate-refused-3395`
and `usage-bad-zooms` need the owner's `YaSat.zmp` (copied, never written to); the 2026.10.6 re-run produced the same text as 2026.10.4.

## MBTiles metadata of the weather runs (P-0099 proof 6)
Read with `sqlite3 <file> "select name,value from metadata where name in ('name','description')"` on the files the
2026-10-21 capture wrote:
- `run-weather-layer` (`North Sea - Weather.mbtiles`, overlay only, no `Name=` in the job): `name` = `RainViewer radar (worldwide, current)`,
  `description` = `RainViewer radar (worldwide, current), snapshot 2026-10-21 07:10 UTC`. The `name` does NOT end with the frame time
  that CR section 11 / cli-requirements.md line 391 announce for a derived name; the time is in `description` only.
- `run-weather-base-seamarks` base (`Clouds.mbtiles`, `Name=Clouds`): `name` = `Clouds` (given, unchanged), `description` = `EUMETSAT MTG GeoColour (Europe, Africa, Atlantic; clouds day and night), snapshot 2026-10-21 06:50 UTC`.
- the seamark overlay (`Clouds - Seamarks.mbtiles`): `name` = `OpenSeaMap Seamarks`, `description` = `Created by chart-grabber-cli 2026.10.6`.
