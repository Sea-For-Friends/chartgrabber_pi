# Chart Grabber

Chart Grabber is an OpenCPN plugin that saves charts for offline use. Pick an area on the chart: the current view, a
rectangle you draw, or a corridor along your route. Choose a chart source and how much detail you want. Chart Grabber
downloads it and adds it to OpenCPN's chart list. The chart then works with no internet, far from shore.

![New chart dialog: area, chart source, seamarks overlay and download estimate](docs/images/new-chart.png)
*New chart: pick an area, a source and seamarks on top, and see the estimate before downloading.*

![My charts dialog: list of downloaded charts with stop, resume and update](docs/images/my-charts.png)
*My charts: your downloaded charts; stop, resume and update.*

![Rain radar shown as its own chart over the Kartverket chart near Bergen](docs/images/weather.png)
*Weather on top: today's rain radar over your chart, refreshed with Update to latest weather.*

The download itself is done by a separate program, the Chart Grabber CLI. Chart Grabber CLI is an independent port of
SAS.Planet ([sasgis/sas.planet.src](https://github.com/sasgis/sas.planet.src)) to C++ and Qt. It is not an official
SAS.Planet project. Thanks to the SAS.Planet authors.

User guide: [docs/user-guide.md](docs/user-guide.md).

**Not for navigation.** The charts are copies of online sources. Always carry official charts.

## What you need

OpenCPN 5.10.2 or newer, on one of these systems:

- Debian 12: 64-bit PC
- Debian 13: 64-bit PC and ARM 64-bit
- Raspberry Pi OS 64-bit (trixie, which is Debian 13): ARM 64-bit
- Ubuntu 24.04 and 26.04: 64-bit PC and ARM 64-bit
- Arch and CachyOS: 64-bit PC
- Windows 10/11: 64-bit PC

Ubuntu 24.04 needs OpenCPN from the OpenCPN PPA.
Debian 12 gets OpenCPN from bookworm-backports.

## Install

1. Install the Chart Grabber CLI for your system.
2. Download the plugin file for your system.
3. In OpenCPN: Options > Plugins > **Import plugin...**, pick the plugin file, **Enable** it, then **OK** (or restart OpenCPN).

## Downloads

| System | Plugin | Chart Grabber CLI |
|---|---|---|
| Debian 12, 64-bit PC | [chartgrabber_pi-2026.10.4-debian-x86_64-12-x86_64.tar.gz](https://github.com/Sea-For-Friends/chartgrabber_pi/releases/download/v2026.10.4/chartgrabber_pi-2026.10.4-debian-x86_64-12-x86_64.tar.gz) | [chart-grabber-cli_2026.10.11_debian12_amd64.deb](https://github.com/Sea-For-Friends/chart-grabber-cli/releases/download/v2026.10.11/chart-grabber-cli_2026.10.11_debian12_amd64.deb) |
| Debian 13, 64-bit PC | [chartgrabber_pi-2026.10.4-debian-x86_64-13-x86_64.tar.gz](https://github.com/Sea-For-Friends/chartgrabber_pi/releases/download/v2026.10.4/chartgrabber_pi-2026.10.4-debian-x86_64-13-x86_64.tar.gz) | [chart-grabber-cli_2026.10.11_debian13_amd64.deb](https://github.com/Sea-For-Friends/chart-grabber-cli/releases/download/v2026.10.11/chart-grabber-cli_2026.10.11_debian13_amd64.deb) |
| Debian 13 and Raspberry Pi OS, ARM 64-bit | [chartgrabber_pi-2026.10.4-debian-arm64-13-arm64.tar.gz](https://github.com/Sea-For-Friends/chartgrabber_pi/releases/download/v2026.10.4/chartgrabber_pi-2026.10.4-debian-arm64-13-arm64.tar.gz) | [chart-grabber-cli_2026.10.11_debian13_arm64.deb](https://github.com/Sea-For-Friends/chart-grabber-cli/releases/download/v2026.10.11/chart-grabber-cli_2026.10.11_debian13_arm64.deb) |
| Ubuntu 24.04, 64-bit PC | [chartgrabber_pi-2026.10.4-debian-x86_64-12-x86_64.tar.gz](https://github.com/Sea-For-Friends/chartgrabber_pi/releases/download/v2026.10.4/chartgrabber_pi-2026.10.4-debian-x86_64-12-x86_64.tar.gz) | [chart-grabber-cli_2026.10.11_ubuntu2404_amd64.deb](https://github.com/Sea-For-Friends/chart-grabber-cli/releases/download/v2026.10.11/chart-grabber-cli_2026.10.11_ubuntu2404_amd64.deb) |
| Ubuntu 24.04, ARM 64-bit | [chartgrabber_pi-2026.10.4-ubuntu-arm64-24.04-arm64.tar.gz](https://github.com/Sea-For-Friends/chartgrabber_pi/releases/download/v2026.10.4/chartgrabber_pi-2026.10.4-ubuntu-arm64-24.04-arm64.tar.gz) | [chart-grabber-cli_2026.10.11_ubuntu2404_arm64.deb](https://github.com/Sea-For-Friends/chart-grabber-cli/releases/download/v2026.10.11/chart-grabber-cli_2026.10.11_ubuntu2404_arm64.deb) |
| Ubuntu 26.04, 64-bit PC | [chartgrabber_pi-2026.10.4-ubuntu-x86_64-26.04-x86_64.tar.gz](https://github.com/Sea-For-Friends/chartgrabber_pi/releases/download/v2026.10.4/chartgrabber_pi-2026.10.4-ubuntu-x86_64-26.04-x86_64.tar.gz) | [chart-grabber-cli_2026.10.11_ubuntu2604_amd64.deb](https://github.com/Sea-For-Friends/chart-grabber-cli/releases/download/v2026.10.11/chart-grabber-cli_2026.10.11_ubuntu2604_amd64.deb) |
| Ubuntu 26.04, ARM 64-bit | [chartgrabber_pi-2026.10.4-ubuntu-arm64-26.04-arm64.tar.gz](https://github.com/Sea-For-Friends/chartgrabber_pi/releases/download/v2026.10.4/chartgrabber_pi-2026.10.4-ubuntu-arm64-26.04-arm64.tar.gz) | [chart-grabber-cli_2026.10.11_ubuntu2604_arm64.deb](https://github.com/Sea-For-Friends/chart-grabber-cli/releases/download/v2026.10.11/chart-grabber-cli_2026.10.11_ubuntu2604_arm64.deb) |
| Arch, 64-bit PC | [chartgrabber_pi-2026.10.4-arch-x86_64-rolling-x86_64.tar.gz](https://github.com/Sea-For-Friends/chartgrabber_pi/releases/download/v2026.10.4/chartgrabber_pi-2026.10.4-arch-x86_64-rolling-x86_64.tar.gz) | [chart-grabber-cli-2026.10.11-linux-x86_64](https://github.com/Sea-For-Friends/chart-grabber-cli/releases/download/v2026.10.11/chart-grabber-cli-2026.10.11-linux-x86_64) |
| CachyOS, 64-bit PC | [chartgrabber_pi-2026.10.4-cachyos-x86_64-rolling-x86_64.tar.gz](https://github.com/Sea-For-Friends/chartgrabber_pi/releases/download/v2026.10.4/chartgrabber_pi-2026.10.4-cachyos-x86_64-rolling-x86_64.tar.gz) | [chart-grabber-cli-2026.10.11-linux-x86_64](https://github.com/Sea-For-Friends/chart-grabber-cli/releases/download/v2026.10.11/chart-grabber-cli-2026.10.11-linux-x86_64) |
| Windows 10/11, 64-bit PC | [chartgrabber_pi-2026.10.4-windows-x86.tar.gz](https://github.com/Sea-For-Friends/chartgrabber_pi/releases/download/v2026.10.4/chartgrabber_pi-2026.10.4-windows-x86.tar.gz) | [chart-grabber-cli-2026.10.11-win64.zip](https://github.com/Sea-For-Friends/chart-grabber-cli/releases/download/v2026.10.11/chart-grabber-cli-2026.10.11-win64.zip) |

Checksums are on the release page. All releases: [plugin](https://github.com/Sea-For-Friends/chartgrabber_pi/releases),
[Chart Grabber CLI](https://github.com/Sea-For-Friends/chart-grabber-cli/releases).

## Chart sources

The plugin ships 28 chart sources in `data/sources/` as `*.source.json` files (the CLI's JSON format), in seven groups that the New chart page shows as menus. Each was
tried with the tile program and a real small download. Details, terms and credits per source: `data/sources/README.md`.

| Group | What it is for a sailor |
|---|---|
| Nautical charts | Official sea charts as tiles: Norway (Kartverket), Finland (Traficom), the US coast (NOAA). Not for navigation. |
| Seamarks and traffic | OpenSeaMap seamarks (buoys, lights) and EMODnet ship traffic density, as overlays on a chart. |
| Depth | Depth shading: EMODnet (Europe, world), GEBCO (world), NOAA BlueTopo (US coast). Not for navigation. |
| Satellite | Sentinel-2 (10 m, worldwide), USGS (USA), Esri World Imagery (grey: its terms may forbid offline use). |
| Coast and land | OpenTopoMap, a land and relief chart (grey: throttled, one connection). |
| Weather now | One picture of the weather as it is now: rain radar, clouds, precipitation, tropical cyclones, US weather warnings (Europe, US, worldwide). |
| Ocean | Sea surface temperature and sea ice (one daily picture), surface currents of the US East and West coast waters (a model's picture). |

The satellite gap: what sailors ask for most, Google, Bing and Esri high-resolution imagery for reefs, cannot be shipped;
those providers forbid saving their tiles. The imagery that can be shipped, Sentinel-2 at 10 m and USGS, shows reef edges,
banks and passes, but not single coral heads.

Sources that need a key, and Navionics, are not in this repository. They live in a separate folder outside it, one folder
with a short README per source. To add one, put its `.zmp` folder or `.source.json` file (with your own key where the file says `KEY`) into a
folder of your own (by default `Charts/Chart sources` in your home folder) and, if you use another folder, choose it in
Preferences, row "Chart sources"; it is read after the bundled sources.

## Licence

GPL-3.0-or-later, see `LICENSE`. `api/ocpn_plugin.h` is OpenCPN's own header (GPL-2.0-or-later). The chart source
definitions in `data/sources` are CC0-1.0; see `data/sources/LICENSE`, `NOTICE` and `PROVENANCE.md`.

## For developers

How to build the plugin: [docs/build.md](docs/build.md).
