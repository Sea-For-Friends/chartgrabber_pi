# Changelog

Versions are calendar-based: `YYYY.MM.N` (year and month of the release, N = release count within that month, from 0).

## 2026.11.6

- 116 chart sources are bundled, 30 more. New orthophotos cover the German coast (Hamburg, Bremen, Lower Saxony, Schleswig-Holstein), Belgium (Flanders) and Sweden (Gothenburg). They also cover Finland (Helsinki, Turku), Estonia, Albania, Croatia, Portugal and the Dutch Caribbean (Bonaire, Saba, Sint Eustatius). In Italy they cover Emilia-Romagna, and the AGEA 2022 orthophotos cover Abruzzo, Liguria, Puglia, Sicily and Sardinia. New SHOM lidar depths cover the Gulf of Morbihan, Mayotte, Réunion and the Îles Éparses. New seabed type charts cover European seas (EMODnet), Ireland (GSI) and Norway (NGU). The AGEA 2022 orthophotos replace the Sicily 2012-13 and Abruzzo 2018-19 orthophotos.
- Better stability on OpenCPN 5.14 and 5.15. The plugin works around two OpenCPN problems. It does not cure them. After a chart list update, the ship position and timers of OpenCPN could freeze (OpenCPN issue https://github.com/OpenCPN/OpenCPN/issues/5518, proposed fix https://github.com/OpenCPN/OpenCPN/pull/5519, both still open). If no position fix arrives after such an update, the plugin now starts the timers again. Each chart list update makes OpenCPN read all charts again (https://github.com/OpenCPN/OpenCPN/issues/4261, still open). The plugin now sends at most one update in 30 s. It holds the update while Options is open.
- OpenCPN no longer crashes when a download ends while the **More** menu is open.
- The plugin waits while OpenCPN is busy and checks chart folders less often, so OpenCPN pauses less.
- When you quit OpenCPN or close the Chart Grabber window while a download runs, the window no longer freezes. The plugin tells the download program to stop and waits up to 2 s. Then it ends the program.
- The plugin checks the Chart Grabber CLI version in Settings, in New chart and when a download starts. This check no longer freezes the window. It stops after 10 s.
- When the plugin cancels a download, it ends the CLI and all programs that the CLI started. This also works on Windows.
- When **Debug log** is on, the plugin log records each time OpenCPN stopped for 250 ms or more.
- The user guide has new screenshots and a route example over open water.
- Chart Grabber CLI 2026.11.3 is bundled and recommended (unchanged).
- Updating from 2026.11.3: Import the new file over the old one.

2026.11.4 and 2026.11.5 were not released; their changes are in 2026.11.6.

## 2026.11.3

- 86 chart sources are bundled: new are the Hong Kong aerial photo (LandsD), the Madeira orthophoto 2018-19 (DROTe) and the NOAA seabed habitat chart of Saipan; the Digital Earth Africa mosaic is back.
- On OpenCPN 5.14 (Flatpak 25.08) the Chart Grabber chart folder now stays in Options > Charts > Chart Files after a restart, so the downloaded charts still draw. If the folder is still missing from an older download, add it there once.
- When a server refuses a download, the result line now says so instead of a general failure.
- **Open folder** says so when the Flatpak desktop opens no file manager window.
- Replacing a chart on the New chart page leaves one row per replaced file in My charts, not two.
- The notes on the New chart page wrap at the real width of the window, and there is no doubled gap above the Chart row.
- A download that ends at once, for example one that is refused, is now reported as finished instead of staying "running".
- **Update all weather charts** now removes the old weather file of every chart, not only the first, also when Windows still holds the file.
- Chart Grabber CLI 2026.11.3 is bundled and recommended.
- Updating from 2026.11.1: Import the new file over the old one.

## 2026.11.1

- 81 chart sources are bundled: Canadian Hydrographic Service charts and NONNA-10 depths, national orthophotos of France (IGN), Spain (PNOA) and the Netherlands (PDOK), French and Portuguese coast, depth, seamark, tide, current and satellite layers, and sources for Uruguay, Argentina and other Latin American countries.
- **More sources** says in a line what each source is for, and the search finds sources by that text.
- The default **Chart** and **Weather on top** lists come from the file `favourite-sources.conf` in the plugin's data folder. If the file is missing or damaged, the New chart page warns and shows a **Download a fresh copy** link; the Chart list then shows **Choose in More sources...** until you pick a source.
- **Update all weather charts** (My charts): one click updates every weather chart one after another, then refreshes OpenCPN's chart list once. A reason line says why a batch ended early.
- After a weather update OpenCPN's chart list is rescanned only when the picture changed.
- Windows with OpenCPN 5.10.2: updating a weather chart that OpenCPN has open now works; the new picture is shown and the old file is dropped from the chart list.
- The New chart window fits an 800 × 480 screen, also with the warning about the favourites file.
- Needs Chart Grabber CLI 2026.10.9 or newer (2026.11.1 is bundled and recommended). It refuses a second download of the same chart at the same time.
- The plugin file carries no documentation any more, only the licence and a short README with the GitHub link; the source terms and the user guide are on GitHub.
- Updating from 2026.10.7: Import the new file over the old one.

## 2026.10.7

- New systems: macOS 12 or newer (Apple and Intel Macs, one file); OpenCPN from Flathub (Flatpak) versions 5.12 and 5.14 on 64-bit PC and ARM 64-bit; Raspberry Pi OS 32-bit (Trixie) and Debian 12 and 13 on 32-bit ARM (armhf). OpenCPN 5.10.2 on Flatpak is not served: Flathub no longer has it.
- Needs Chart Grabber CLI 2026.10.9 or newer (2026.10.14 is bundled and recommended).
- The bundled CLI is 2026.10.14: it also runs on 32-bit ARM and macOS, and the 64-bit Linux files work on systems with glibc 2.35 or newer.
- The New chart page warns "This is OpenCPN's starting view. Move the chart to your area first." when **Current view** is still the view OpenCPN opens with.
- Updating from 2026.10.6: Import the new file over the old one.

## 2026.10.6

- Every plugin file now carries the Chart Grabber CLI, Arch and CachyOS too: Import the plugin and nothing else is needed.
- Needs Chart Grabber CLI 2026.10.9 or newer (2026.10.13 is bundled and recommended).
- The bundled CLI no longer writes a font cache outside OpenCPN's data folder, and prints no font warnings on newer systems.
- The Linux CLI needs the graphics libraries `libopengl0`, `libglx0` and `libegl1`; OpenCPN's own package installs them on Debian 12, Debian 13 and Ubuntu 24.04.
- New file for Debian 12, ARM 64-bit (Raspberry Pi OS bookworm); Ubuntu 24.04 ARM 64-bit takes it too.
- The CLI runs with a data folder of the plugin (`--datadir`), so it leaves nothing in `~/.config/chart-grabber` or `~/.local/share/chart-grabber`.
- The progress window stays on the screen and can be moved; on Windows it can also be closed with Escape or the window's Close.
- Updating from 2026.10.4 or older: Import the new file; a separately installed CLI is no longer needed. A CLI chosen under Settings > Advanced is still used: **Use default** there switches to the bundled one.

## 2026.10.4

- Needs Chart Grabber CLI 2026.10.9 or newer (2026.10.10 recommended).
- The bundled chart sources are now `.source.json` files.
- Four more bundled chart sources: NOAA tropical cyclones, NOAA weather warnings (US), and NOAA surface currents (western Atlantic, eastern Pacific); 28 in all.
- When the newest weather picture is not served yet, Details shows only the picture the chart holds.
- Windows: the plugin file is named `chartgrabber_pi-<version>-windows-x86.tar.gz`.
- A user guide in English and Russian (`docs/user-guide.md`).
- Ubuntu 24.04 ships OpenCPN 5.8.4, which is too old: use the OpenCPN PPA. Debian 12 gets 5.10.2 from bookworm-backports.

## 2026.10.3

- Needs Chart Grabber CLI 2026.10.8 or newer.
- Bundled chart sources install in a folder per plugin version; sources left by an older Import are no longer read.
- Windows: the plugin for OpenCPN 5.10.2 and newer (32-bit), with the Chart Grabber CLI from its own zip.
- Windows: folders and files are passed to the CLI in Windows form, and chart paths from the CLI are read in Windows form.
- Windows: OpenCPN no longer crashes when the plugin is removed or disabled while a job notice is shown.
- A chart file saved by a release before 2026.10.2 under its old name (before characters Windows forbids were replaced) no longer brings the "Replace it?" question.
- The plugin builds with GCC 12 (Debian 12) without warnings.
- Ubuntu 24.04 ships OpenCPN 5.8.4, which is too old: use the OpenCPN PPA. Debian 12 gets 5.10.2 from bookworm-backports.

## 2026.10.2

- Needs Chart Grabber CLI 2026.10.8 or newer.
- Stop now asks the CLI to stop and save, and ends it after 10 seconds if it does not; the Force stop button is gone.
- Each weather Update gets its own chart name with the time of the picture.
- The chart sources are regrouped into seven groups (Nautical charts, Seamarks and traffic, Depth, Satellite, Coast and
  land, Weather now, Ocean), with eight new sources (24 in all). Sources that need a key are not bundled.
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
