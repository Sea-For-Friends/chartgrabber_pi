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
The keyed sources are not in this repository (they live in a folder outside it).

Choices of 2026-10-22: sea ice ships as GHRSST MUR (the AMSR2 default date is a year old); Copernicus Marine is not shipped until
the CLI solves range-time snapshots; Finland ships as the single WMTS layer (MaxZoom 15), the loss of coarse layers at low zoom is accepted.
A nearest-now frame key and an always-refetch key for the CLI are wanted; RTOFS currents and nowCOAST cyclones may come later.


## 2026-10-23: the 24 definitions are JSON files

The 24 definitions are now `*.source.json` (format 1 of the CLI's sources guide), copied byte for byte from the CLI repository's
`tests/fixtures/maptype/url-differential/base-json/` at commit `9e9f0eeefec5`, where they were written from these same
`params.txt` files by a key-by-key mapping that leaves no key over; `EPSG=3857` is the default and is not written. GUID, name,
group and `NameInCache` are unchanged, so caches stay valid. The earlier entries above stay as history; wherever they say
`.zmp` folder or `params.txt` read the `.source.json` file of the same name. sha256 of the 24 files (2026-11-05: the `license` of Esri World Imagery and both EMODnet Bathymetry files was edited locally after
the licence check, see README; the three hashes below are of the edited files):

    8beeb987742fca43d06697ea4244a98c267c19b5e4b4dd8a1f799d995d968136  Coast and land/OpenTopoMap.source.json
    1131ccfc96c07725e0756e1f60a13fdd8d6e1fe33ac3623ee244f275cc0b418d  Depth/EMODnet-Bathymetry-Europe.source.json
    a7d92cd96407a7746ea95313273408e629b7c4b50834757cd2adf530294c2ab1  Depth/EMODnet-Bathymetry-World.source.json
    d6141ced65ce4537642b26d5edf6e8e03d1fb37609bf08904012bfaf77133896  Depth/GEBCO-Bathymetry.source.json
    bd548b47d8e0f09cd3356a9e59367634d2b371f4be52df75366761425774e359  Depth/NOAA-BlueTopo-US.source.json
    3c6235aa9899127861fa725ac0d95ab389e5cf4fb90dfae8b2af66a77a77f78c  Nautical charts/Finland-Sea-Chart-Traficom.source.json
    4bece38b4b92e9af69f05e841796611a25425fd68d9152e88a773bf977d2068e  Nautical charts/NOAA-Chart-US.source.json
    5433593cefa42a996b64d2bad843777bfbf07c10d70e0d7e38123e01fc73cd7e  Nautical charts/Norway-Sea-Chart-Kartverket.source.json
    84ec056eb9d87c0856426da1b7a9498e8b714309a9ec013a97e1fcc15fed0466  Ocean/NASA-GIBS-Sea-Ice.source.json
    840e55af12729ea4893e83e483df55129bcf38b886167c319334b2b12f190db7  Ocean/NASA-GIBS-Sea-Surface-Temperature.source.json
    42d3ef7fb4d803d6e1acb01b4b50867c9ed82ba5375d4312b48fc44afff779d2  Satellite/Esri-World-Imagery.source.json
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

## 2026-11-05: CHS chart (Canada) added

One new file, written by this repository from the service's capabilities (WMS 1.3.0, the same ArcGIS Maritime Chart Service extension as the
NOAA chart) and the open.canada.ca record (read 2026-11-04), in the same `.source.json` format as the others. Downloaded once with the CLI
2026.10.14 (one connection, 18 tiles). sha256:

    2397811e52aaadb3dfcfb486b915385d58f241fb56c588e36a1c81efaf555159  Nautical charts/CHS-Chart-Canada.source.json

## 2026-11-05: CHS NONNA-10 bathymetry added

One new file, written by this repository from the WMTS capabilities (GeoServer GeoWebCache, matrix set `EPSG:900913`) and the
open.canada.ca record (read 2026-11-04), in the same `.source.json` format as the others. Downloaded once with the CLI 2026.10.14
(one thread, 18 tiles). sha256:

    6fd66cd6aa744275cb8713468e0ef89c360c315a2f4f3892048b88b6f29916a8  Depth/CHS-NONNA-10-Canada.source.json

## 2026-11-05: three national orthophotos added

Three new files, written by this repository from the providers' WMTS capabilities (IGN Geoplateforme, PNOA, PDOK, read 2026-11-05) and
their terms pages, in the same `.source.json` format as the others. Downloaded once each with the CLI 2026.10.14 (one thread, a few
tiles; IGN also with `--zooms=5,6`, where level 5 is not on the server). sha256:

    4f44976bbab382ad2ae1e7b3fb00d4e00eb1a92489bbd8003479aa06be2bf3a2  Satellite/IGN-France-Ortho-20cm.source.json
    b8f83b87a7934d7e07c3b87edbd166284b0297f20e9466ee431d4f041f0e69d9  Satellite/PNOA-Spain-Ortho.source.json
    b8b657f336d14dcf78c60011b50cba7ae9a326058ca5bd85b526dc9164a1da5b  Satellite/PDOK-Luchtfoto-NL.source.json

## 2026-11-05: Coast and land overlays

Four new files, written by this repository from the providers' capabilities (read 2026-11-05), in the same
`.source.json` format as the others. Downloaded once each with the CLI 2026.10.14 (one connection, a few tiles). sha256:

    cff3e5392678408210f11cb4dcf98f9660f6571e5060afc6f95872e270e5bb08  Coast and land/Balearics-IDEIB-Posidonia.source.json
    8bb73371ad5c5768d5c92538892b3244ac7c065695225070eb723c0d0e57b4a3  Coast and land/EMODnet-Seagrass-Substrate.source.json
    675ae6765de832ccee5e15b5f7a4cf6ff937e1a217948fa04939634068fcf36b  Coast and land/EMODnet-MPA-Natura2000.source.json
    c09f3d8a85f6c965f1e8324773f84b1c15448e96c5df6c4884182ea63c7d4e4f  Coast and land/Queensland-Marine-Parks.source.json

## 2026-11-05: SHOM depth layers

Nine new files, written by this repository from the providers' capabilities (SHOM WMTS `/INSPIRE/`, read 2026-11-05) (read 2026-11-05), in the same
`.source.json` format as the others. Downloaded once each with the CLI 2026.10.14 (one connection, a few tiles). sha256:

    5e706fd02dc978bc7f44e9f635ad1356d5735bcdc0062420fd8345b5e38e6c53  Depth/SHOM-Litto3D-Martinique.source.json
    54318b7f521da4cd6172d794afc7d54f4ee6783a7504b5a014cd54fcfee4fb8c  Depth/SHOM-Litto3D-Guadeloupe.source.json
    492b578cba4edead658c8f349d29d3adcdc2899e523bce816973de8dd9b2f62c  Depth/SHOM-Litto3D-Saint-Martin.source.json
    557b6ade7a6df145f5f92fe0c229bf9a4b245ba13d25e940feb8e159b5f08fe2  Depth/SHOM-Litto3D-Saint-Barthelemy.source.json
    ac67853eef06af3457d905ecb8734f7aea36d3f0b030fc26a6abfcf38b45e11e  Depth/SHOM-Lidar-Corsica.source.json
    9ab337b4b47688ec0e173365d46cbced82e4e8be7bafca5e164c1c82f7611cbd  Depth/SHOM-DEM-100m-Guadeloupe-Martinique.source.json
    b249a9326c523c75f38d769512f44fea17e15494186795a4005b52b9244cfa43  Depth/SHOM-DEM-100m-Saint-Martin-Saint-Barthelemy.source.json
    d7f54bb57d48ff7b92652970386177ce36841e9bf038a3a33fb486cbfac9b796  Depth/SHOM-DEM-100m-Corsica.source.json
    f1bdedf06617f34efb7ba59035d72110bd9c9df19d7785ef75d41e2149aad175  Depth/SHOM-DEM-100m-Gulf-of-Lion-Cote-dAzur.source.json

## 2026-11-05: SHOM Pacific depth layers, SHOM seabed type, Digital Earth Pacific satellite-derived depth

Seven new files, written by this repository from the providers' capabilities (read 2026-11-05), in the same
`.source.json` format as the others. Downloaded once each with the CLI 2026.10.14 (one connection, a few tiles). sha256:

    0b38de33ecb3b0fb62dc9f6da46659105c647656272b492b0e4af85c4ffcb921  Depth/SHOM-Lidar-Bora-Bora.source.json
    688d8ffef5f47dd5d00e61c3d92d7b19285f092496e4d40326fe44bb520f8b99  Depth/SHOM-DEM-Tahiti.source.json
    d535dfb57218c2f7e84a83f2aa56b197824ff444d6a22a65ef62155a622d0eea  Depth/SHOM-DEM-Moorea.source.json
    e25a43a1985a5cf214fa803dc4a47552c8fd9ab76f4bc88ff28ad7d867aeddd7  Depth/SHOM-DEM-100m-Tahiti-Moorea.source.json
    e24aa417ab802df2e4eb82a1c29de64f4f8848510ab1ffae5c48ca089677272a  Depth/SHOM-DEM-100m-New-Caledonia.source.json
    9f082237747ece37f1254d4b47d94ea3d9e4f3628f34263c6d9bdd83a89e670f  Depth/SHOM-Seabed-Type.source.json
    37ccda82fccfa320ab6400d4790cde7b9a30b5d242e5cd27c2149ab969c69246  Depth/DEP-Satellite-Bathymetry.source.json

## 2026-11-05: SHOM buoyage, wrecks, navigation rules, tidal ranges, tidal streams, 3D currents, tide zones

Nine new files, written by this repository from the providers' capabilities (read 2026-11-05), in the same
`.source.json` format as the others. Downloaded once each with the CLI 2026.10.14 (one connection, a few tiles). sha256:

    de8ad4174234bb26a6595a2645e895de271b21ae4d1cabf0421d1359b147d59c  Seamarks and traffic/SHOM-Buoyage.source.json
    7f80bdcee9fc4320986bcf011a915494bf71748caad0334647eb8a654ec092a6  Seamarks and traffic/SHOM-Wrecks.source.json
    328957f23b9db5577c573f7a05974050e4049a1cd4938b2bb977d1bda23f38d1  Seamarks and traffic/SHOM-Navigation-Rules.source.json
    0ae809e0a9f4d663f044ed7c7278fbe1752b4118b4de7fabc0376d2f5337d68a  Ocean/SHOM-Tidal-Range-Spring.source.json
    dd455bbe8700c525053cabedacb259b31276d6d694cebeadbc1703a722ad94f8  Ocean/SHOM-Tidal-Range-Neap.source.json
    d2dd1e0b157fe287c611464af8dc8d474ac6cc47336a40109e061991971a1f21  Ocean/SHOM-Tidal-Stream-Max.source.json
    b166663b88fa712ae59f9ed6342742e4645fbe4ba0a2ebba22f97b926bd42922  Ocean/SHOM-Currents-3D-Channel.source.json
    e1e97e49b3407f9590af8afc1800cfec8518ac76a60d259325c83f412edcce50  Ocean/SHOM-Currents-3D-Fromveur.source.json
    583feba32a1c67b51c2910c0e1cb48977dbbd6454021b896f156b7d327751385  Ocean/SHOM-Tide-Zones.source.json

## 2026-11-05: Satellite and IH Portugal chart

Four new files (a fifth, a Satellite inland aerial layer, was dropped), written by this repository from the providers' capabilities (read 2026-11-05), in the same
`.source.json` format as the others. Downloaded once each with the CLI 2026.10.14 (one connection, a few tiles). sha256:

    eb231a010451178015e6a1f7e3da85a2c95bc4844b39f99c441373d210a8ee79  Satellite/DEP-GeoMAD-2025.source.json
    a26541345c9a4ba111ce623f89b72b61a79569aff12206f6d69a9f851c2f7e11  Satellite/Tuscany-Ortho-2023.source.json
    36b51e843955e60a64e47250832625a1a63c7ec2fb7f22b541dc3e0b405a3a6f  Satellite/Tasmania-LIST-Ortho.source.json
    829c7d21e3143c8bd781b1fe8e4adb220da35bf780bf7f4762b36866a61195d3  Nautical charts/Portugal-IH-ENC.source.json

## 2026-11-05: Weather now

Eight new files, written by this repository from the providers' capabilities (read 2026-11-05), in the same
`.source.json` format as the others. Downloaded once each with the CLI 2026.10.14 (one connection, a few tiles). sha256:

    a18dba7567a4ae6b4d7e8b71cbdd1cfe21bfc5f1f731501049bd13d949700f13  Weather now/NASA-GIBS-GOES-East-GeoColor.source.json
    e54aa030ff5c1b05107cac21b2df1af5031433881b79ee30af71fca0da0953e7  Weather now/NASA-GIBS-GOES-West-GeoColor.source.json
    06fa5d2416faa40e79607748d649e806a18b2471d3482483c7f0ddfcec5e2cb7  Weather now/NASA-GIBS-GOES-East-Infrared.source.json
    7cf6d84d2e1f5c60b6227863dc45812552cdc7786e4567660d18adc5e4a23b98  Weather now/NASA-GIBS-GOES-West-Infrared.source.json
    4148240f2ab93ceb81c5e1a49bb95fced8220e12af7e50c3a75c0fbc559e5c25  Weather now/NOAA-STOFS-3D-Water-Level.source.json
    f773b77278cd65aad32782a8c5129fc3ec3e835a48f76c868e65c8ef155fb909  Weather now/NOAA-STOFS-2D-Max-Water-Level.source.json
    d19ceeea67b5088d1313f0558644dcf1ecc29784dbf873eba1116c1bf31d397d  Weather now/FMI-Radar-Finland.source.json
    f88af133d44ad8a32f4599007a27939a5951193edb0bc7af8e67b764b306ab61  Weather now/KNMI-Radar-Netherlands.source.json

## 2026-11-05: Ocean

Four new files, written by this repository from the providers' capabilities (read 2026-11-05), in the same
`.source.json` format as the others. Downloaded once each with the CLI 2026.10.14 (one connection, a few tiles). sha256:

    c5c3084c985584fd6ec4bdc2a82331824bdc94eded77f9acb1c357261303b600  Ocean/NOAA-OFS-Currents-US.source.json
    f039697bf6f124a6742940d67784290ce232b490126095da5872549de918f982  Ocean/Copernicus-Currents-Global.source.json
    5d443274a2772b9e3aac1253456995e716288fbe38edca7c0f4cc9c314c80bd0  Ocean/Copernicus-Currents-NW-Shelf.source.json
    78082377ddf1c3e2f9a1865519d1a2d2042439064dfdb98211803120d6b7512e  Ocean/Copernicus-Waves-Global.source.json

## 2026-11-05: Latin America

Three new files (a fourth, the INEGI Mexico orthophoto, was dropped: the photos are too old), written by this repository from the providers' capabilities (read 2026-11-05), in the same
`.source.json` format as the others. Downloaded once each with the CLI 2026.10.14 (one connection, a few tiles). sha256:

    05b176f92c647c801157f79fdf11e6efd6065d25826d09a1514d16ed995254b9  Satellite/Uruguay-IDE-Ortho-2017.source.json
    b4e203fa3f9f34c76ff567efcba30573cc5641acfbf8b3c5415937da139d946e  Satellite/Argentina-IGN-Aerial-Mosaics.source.json
    bcbc92a24a8dbd092da243a632a389802f21b783aacd31ef79c9a6bfaa23ab02  Coast and land/Argentina-IGN-Protected-Areas.source.json
