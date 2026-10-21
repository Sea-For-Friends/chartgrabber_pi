# Changelog

Versions are calendar-based: `YYYY.MM.N` (year and month of the release, N = release count within that month, from 0).

## 2026.10.2

- Needs Chart Grabber CLI 2026.10.8 or newer.
- Stop now asks the CLI to stop and save, and ends it after 10 seconds if it does not; the Force stop button is gone.
- Each weather Update gets its own chart name with the time of the picture.
- The chart sources are regrouped into seven groups (Nautical charts, Seamarks and traffic, Depth, Satellite, Coast and
  land, Weather now, Ocean), with eight new sources (24 in all). Sources that need a key, and Navionics, are not bundled.
- Chart Grabber CLI 2026.10.8 no longer supports BerkeleyDB tile caches; the default SQLite cache is unchanged.

## 2026.10.1

- Needs Chart Grabber CLI 2026.10.6 or newer.
- A lost connection now pauses the download and retries it.
- Clearer messages when the folder for saved tiles cannot be used or the area is too large.

## 2026.10.0

First release.

- OpenCPN plugin `chartgrabber_pi` ("Chart Grabber"): pick an area (the current view, a drawn rectangle or a corridor
  along a route), a chart source and the detail levels; the plugin runs Chart Grabber CLI as a child process, which
  downloads the tiles and exports them to MBTiles; the plugin adds the result to OpenCPN's chart database.
- New chart page, My charts page (show, resume / top up, build from saved tiles, delete), progress strip, settings.
- Bundled chart sources (`data/sources`, 16 definitions) with their licence and notices.
- Needs Chart Grabber CLI 2026.10.4 or newer.
