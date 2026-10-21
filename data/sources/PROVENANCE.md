# Provenance of the bundled chart sources

Copied on 2026-10-17 from Chart Grabber CLI 2026.10.3 (tag `v2026.10.3`, commit `014be7f3`), no local edits.

- Package: `chart-grabber-cli_2026.10.3_amd64.deb`, sha256 `cdca8c05cf199471f8b01fda02ebcd99da7b590414d233244f9146e5deaa5f75`.
- `<group>/<name>.zmp/params.txt` (16 definitions: Satellite, Sea, Topo, Weather, Marine; 14 from the CLI package + 2 Marine EMODnet vessel-density sources that are not in that package): `usr/share/chart-grabber-cli/maps/` of the package.
- `LICENSE` (CC0-1.0): `usr/share/doc/chart-grabber-cli/map-sources/LICENSE`.
- `README.md` (terms notes): `usr/share/doc/chart-grabber-cli/map-sources/README.md`.
- `NOTICE`: `usr/share/doc/chart-grabber-cli/copyright`.

Rule: a definition changes by a new copy from a newer CLI release OR by a local edit recorded here.

Second copy, 2026-10-17: the Marine group, 2 definitions (`Marine/EMODnet-Vessel-Density.zmp/params.txt` and
`Marine/EMODnet-Vessel-Density-Sailing.zmp/params.txt`), copied byte for byte (no local edits) from a development branch
of the CLI (commit `fa1a0432`, "shipped EMODnet vessel density overlays"). It is not a release and not part of the 2026.10.3
package. sha256: all types `4f0a14fe73eb5d7e195ef816417493ddccb920082e6703ad1f92e3b9b67db97e`, sailing and pleasure craft
`4c5dfa4285fc7fe5d173e0ef60ec033852046c980121a5554f6db4c7f9720d4f`. With the 14 definitions above, the folder holds 16 definitions.

Local edit, 2026-10-18: `Sea/EMODnet-Bathymetry-World.zmp/params.txt` and
`Sea/EMODnet-Bathymetry-Europe.zmp/params.txt` each got one line `NavigationWarning=1` at the end of `[PARAMS]` (after
`MaxConnectToServerCount=2`; UTF-8 with BOM and CRLF kept, the new line ends with CRLF). Why: since CLI 2026.10.4 the CLI
reads the key, reports `navigationWarning` on the `source` record and prints a notice on download / run; depth shading is
not an official nautical chart. Nothing else changed. sha256 after the edit: world
`d3d4aaeb7404176a855865a087a7c87cb671de424426baf18b153015b5637b4a`, Europe
`0dad6956ca8a932aea31ecf413eb1f1b64950abc2676875e3a7d34890a190582`.

## Regrouped base chart set, 2026-10-22

Folder = menu group = `ParentSubMenu` value. The paths above are the state before this entry.

Moves (`git mv`; the `ParentSubMenu=` line is the only change, GUID and `NameInCache` byte-identical, BOM and CRLF kept; checked
by comparing the `GUID=` and `NameInCache=` lines of all 16 shipped files before and after; the 3 Satellite files did not move):
- `Topo/OpenTopoMap.zmp` -> `Coast and land/OpenTopoMap.zmp`
- `Sea/EMODnet-Bathymetry-Europe.zmp` -> `Depth/EMODnet-Bathymetry-Europe.zmp`
- `Sea/EMODnet-Bathymetry-World.zmp` -> `Depth/EMODnet-Bathymetry-World.zmp`
- `Sea/OpenSeaMap-Seamarks.zmp` -> `Seamarks and traffic/OpenSeaMap-Seamarks.zmp`
- `Marine/EMODnet-Vessel-Density.zmp` -> `Seamarks and traffic/EMODnet-Vessel-Density.zmp`
- `Marine/EMODnet-Vessel-Density-Sailing.zmp` -> `Seamarks and traffic/EMODnet-Vessel-Density-Sailing.zmp`
- `Weather/DWD-Radar-Germany.zmp` -> `Weather now/DWD-Radar-Germany.zmp`
- `Weather/EUMETView-GeoColour.zmp` -> `Weather now/EUMETView-GeoColour.zmp`
- `Weather/EUMETView-H60B-Precipitation.zmp` -> `Weather now/EUMETView-H60B-Precipitation.zmp`
- `Weather/NASA-GIBS-IMERG-Precipitation.zmp` -> `Weather now/NASA-GIBS-IMERG-Precipitation.zmp`
- `Weather/NASA-GIBS-VIIRS-TrueColor.zmp` -> `Weather now/NASA-GIBS-VIIRS-TrueColor.zmp`
- `Weather/NOAA-nowCOAST-Radar-CONUS.zmp` -> `Weather now/NOAA-nowCOAST-Radar-CONUS.zmp`
- `Weather/RainViewer-Radar.zmp` -> `Weather now/RainViewer-Radar.zmp`

New files, written by this repository from the providers' public capabilities (not copied from the CLI package or the big list),
each proven with a real tiny download by CLI 2026.10.8:
- `Nautical charts/Norway-Sea-Chart-Kartverket.zmp`, `Nautical charts/Finland-Sea-Chart-Traficom.zmp`, `Nautical charts/NOAA-Chart-US.zmp`
- `Depth/GEBCO-Bathymetry.zmp`, `Depth/NOAA-BlueTopo-US.zmp`
- `Weather now/NOAA-nowCOAST-Clouds-Infrared.zmp`
- `Ocean/NASA-GIBS-Sea-Surface-Temperature.zmp`, `Ocean/NASA-GIBS-Sea-Ice.zmp`

Removals: none of the 16 shipped; two candidates changed on the way: the Traficom one-request WMS form was dropped for the WMTS layer
(GeoWebCache refuses a list of layers), and the sea ice layer is `GHRSST_L4_MUR_Sea_Ice_Concentration` instead of the planned
`AMSRU2_Sea_Ice_Concentration_12km` (that layer's time dimension ends 2025-09-01). Not shipped, with reasons: `README.md`, "Not shipped".
Navionics and the keyed sources are not in this repository (they live in a folder outside it).

Choices of 2026-10-22: sea ice ships as GHRSST MUR (the AMSR2 default date is a year old); Copernicus Marine is not shipped until
the CLI solves range-time snapshots; Finland ships as the single WMTS layer (MaxZoom 15), the loss of coarse layers at low zoom is accepted.
A nearest-now frame key and an always-refetch key for the CLI are wanted; RTOFS currents and nowCOAST cyclones may come later.
