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


## 2026-10-23: the 24 definitions are JSON files

The 24 definitions are now `*.source.json` (format 1 of the CLI's sources guide), copied byte for byte from the CLI repository's
`tests/fixtures/maptype/url-differential/base-json/` at commit `9e9f0eeefec5`, where they were written from these same
`params.txt` files by a key-by-key mapping that leaves no key over; `EPSG=3857` is the default and is not written. GUID, name,
group and `NameInCache` are unchanged, so caches stay valid. The earlier entries above stay as history; wherever they say
`.zmp` folder or `params.txt` read the `.source.json` file of the same name. sha256 of the 24 files:

    8beeb987742fca43d06697ea4244a98c267c19b5e4b4dd8a1f799d995d968136  Coast and land/OpenTopoMap.source.json
    f15bc93595b17bc5b2b863f5f82d798d6ec3d62dee5bce279dd486a17adb0050  Depth/EMODnet-Bathymetry-Europe.source.json
    692c3c9d089a69b4663d8fe4f015b5442efbc78ae7949b2c30c99c1973e07cab  Depth/EMODnet-Bathymetry-World.source.json
    d6141ced65ce4537642b26d5edf6e8e03d1fb37609bf08904012bfaf77133896  Depth/GEBCO-Bathymetry.source.json
    bd548b47d8e0f09cd3356a9e59367634d2b371f4be52df75366761425774e359  Depth/NOAA-BlueTopo-US.source.json
    3c6235aa9899127861fa725ac0d95ab389e5cf4fb90dfae8b2af66a77a77f78c  Nautical charts/Finland-Sea-Chart-Traficom.source.json
    4bece38b4b92e9af69f05e841796611a25425fd68d9152e88a773bf977d2068e  Nautical charts/NOAA-Chart-US.source.json
    5433593cefa42a996b64d2bad843777bfbf07c10d70e0d7e38123e01fc73cd7e  Nautical charts/Norway-Sea-Chart-Kartverket.source.json
    84ec056eb9d87c0856426da1b7a9498e8b714309a9ec013a97e1fcc15fed0466  Ocean/NASA-GIBS-Sea-Ice.source.json
    840e55af12729ea4893e83e483df55129bcf38b886167c319334b2b12f190db7  Ocean/NASA-GIBS-Sea-Surface-Temperature.source.json
    caeafbaa3fe3b0149c9e01b35f474a51eb0cf04689aaef0b0427f3e5f90b6ab4  Satellite/Esri-World-Imagery.source.json
    c1f01a63c87680ec824c639a6b3620176029abd893b46bae75e685d95389603a  Satellite/Sentinel2-Cloudless-2025.source.json
    5bb26718053b4d6f806f26336c5a75a440e789f888e03bf09d424465d1b4b181  Satellite/USGS-Imagery-USA.source.json
    c601025618b818669f208a95cd25a41e718e962bda1413a03b4b016c627ab5aa  Seamarks and traffic/EMODnet-Vessel-Density-Sailing.source.json
    8d9eee4a1667b4daee21f8f462ba17e83be2bba0f172452e15c7ce2d25eb343e  Seamarks and traffic/EMODnet-Vessel-Density.source.json
    55ac4b0805067db798fef86711acffde0a1111db846a6eedc64ef4312491a016  Seamarks and traffic/OpenSeaMap-Seamarks.source.json
    3a2adec893544547674dd5eba325c87fa04f07b66b79b7c1ead203a260467c8e  Weather now/DWD-Radar-Germany.source.json
    41c8387d87ff527ed23028588c25520d38768428d86530a3fc7d8f7e3057e9e3  Weather now/EUMETView-GeoColour.source.json
    af020ac9eee01410b3b4744e14d0277dcbb8396492e5612ba06edc6aa8b16ce2  Weather now/EUMETView-H60B-Precipitation.source.json
    909737786c11e1ce2ec500a49df362a259a84ce3f74376ac6134b75e54c5b0cb  Weather now/NASA-GIBS-IMERG-Precipitation.source.json
    349106df72bbcff1589c5a577815f9b24f29a20c3bd43b917df4417fcaeb52cf  Weather now/NASA-GIBS-VIIRS-TrueColor.source.json
    211a0d6cd8f13f4b0401cfa1033189f9987b325cc77df3b36f256421a59d07dc  Weather now/NOAA-nowCOAST-Clouds-Infrared.source.json
    e24f2485a54889c9f0a193766b980289f9f2de18d33f72740a0960067f9c74cb  Weather now/NOAA-nowCOAST-Radar-CONUS.source.json
    de10100358c80a847ac36f425b7aa7c3f307e6092a489a1c64ccc7b68d2f87a7  Weather now/RainViewer-Radar.source.json

## 2026-10-23: four weather sources added

Four new files, written by this repository from the providers' capabilities documents (nowCOAST `tropical_cyclones`, `alerts`
and `grtofs`, read 2026-10-23) in the same `.source.json` format as the other 24: the active tropical cyclones and the NWS watches
and warnings (`none-refetch`), and the RTOFS surface currents east and west (`wms-default` with `nearest-now`). Each was
downloaded once with the CLI 2026.10.9 (one connection, a few tiles). sha256:

    ccb5d57fd8f624e5e409053ab8f0f063e86bcb621e36b0dca01f88622cea3c67  Weather now/NOAA-nowCOAST-Tropical-Cyclones.source.json
    0c695c7b6a98fd8764be8b46fecd046b41f6b29c5d16eadcf485aadd9d903f2d  Weather now/NOAA-nowCOAST-NWS-Warnings.source.json
    b22aeb3ca78ccd9c1dd509a5d092b9efc4082ffb0eee666bc5b7687754a579bf  Ocean/NOAA-RTOFS-Currents-Eastern-Pacific.source.json
    9ec5dabcfd7f76e393e71d8d3ada1ba07f791c1052f3c6fa579fa6d91940d405  Ocean/NOAA-RTOFS-Currents-Western-Atlantic.source.json
