# Shipped chart-source definitions

One hundred and eleven JSON source files (`<group>/<name>.source.json`, format 1 of the CLI's sources guide), written by us from each service's public documentation. Nothing is
copied from the SAS.Planet map catalogue; the GUID and `NameInCache` are reused only for the three services the
catalogue also has (OpenSeaMap seamarks, Esri World Imagery, OpenTopoMap), so an existing tile cache stays valid.
The folder name is the menu group (the `group` value of the definitions); seven groups, below. The definition
files are CC0-1.0 (see `LICENSE`). The terms of each source were read before it was added (between 2026-10-15 and 2026-11-06; the
date is given where a section needs it), and every source was downloaded once with the Chart Grabber CLI (2026.10.8 to 2026.11.3; one connection, a few tiles,
one or two zoom levels) to check that it works. The folder `data/sources` is passed to the CLI first; a sailor's own
folder (Preferences) is passed after it.

The two groups `Nautical charts` and `Depth` carry `"navigationWarning": true`: they are tile pictures, not charts to navigate
by. Kartverket and Traficom say "not for navigation" themselves. Sources outside these two groups whose provider says "not for navigation", or calls its map a draft, carry the flag too.

## Nautical charts

### Norway sea chart (Kartverket)
- GUID `{25DBA489-D484-4A13-B65D-ABA8967FF3DA}` (base chart), `"navigationWarning": true`
- Service: `https://cache.kartverket.no/v1/wmts/1.0.0/sjokartraster/default/webmercator/{z}/{y}/{x}.png` (WMTS, matrix set `webmercator`, levels 0-18)
- Terms: capabilities (https://cache.kartverket.no/v1/wmts/1.0.0/WMTSCapabilities.xml) `<ows:Fees>Ingen`, `<ows:AccessConstraints>Ingen`;
  CC BY 4.0 (https://data.norge.no/en/data-services/e3454491-066a-3276-a482-f63069728021, https://proxy.kartverket.no/en/api-and-data/terms-of-use);
  the cache service is CC BY 4.0 ("Ikke til navigasjon" is not in the WMTS capabilities, whose layer abstract is empty; the Geonorge
  metadata says "Sjøkartraster er ikke egnet til navigasjon", https://geonorge.no/geonetworktest/srv/api/records/8f381180-1a47-4453-bee7-9a3d64843efa).
- Attribution: (c) Kartverket, CC BY 4.0 (not for navigation). Max zoom 18 (the last level of the capabilities).
- Coverage: the Norwegian coast. Kartverket ends the raster Kystkart download on 2026-08-28; tiles remain.

### Finland sea chart (Traficom)
- GUID `{8A9C3A53-0512-4401-A6D3-050E08934949}` (base chart), `"navigationWarning": true`
- Service: WMTS `https://julkinen.traficom.fi/rasteripalvelu/wmts`, layer `Traficom:Merikarttasarjat public`, matrix set `WGS84_Pseudo-Mercator`, matrix id `WGS84_Pseudo-Mercator:{z}` (key-value request, `TILEROW={y}&TILECOL={x}`, levels 5-15 for this layer)
- Terms: capabilities of the WMS (https://julkinen.traficom.fi/rasteripalvelu/wms?SERVICE=WMS&VERSION=1.3.0&REQUEST=GetCapabilities, 2026-10-22):
  `<Fees>NONE`; `AccessConstraints`: "This dataset produced by the Finnish Transport Agency is licensed under a Creative Commons
  Attribution 4.0 International License - http://creativecommons.org/licenses/by/4.0/ "Source: Finnish Transport Agency.
  Not for navigational use. Does not meet the requirements for official nautical charts.""
  (https://traficom.fi/en/hydrography/finnish-nautical-charts).
- Attribution: Source: Traficom, CC BY 4.0 (not for navigational use). Max zoom 15.

### NOAA chart (US coast)
- GUID `{5DBBE570-0FA6-403E-B490-7A27AF8A3814}` (base chart), `"navigationWarning": true`
- Service: WMS `https://gis.charttools.noaa.gov/arcgis/rest/services/MCS/NOAAChartDisplay/MapServer/exts/MaritimeChartService/WMSServer`,
  layers `1,2,3,4,5,6,7,11` of its capabilities (1 natural and man-made features, 2 depths and currents, 3 seabed and obstructions,
  4 traffic routes, 5 special areas, 6 buoys, beacons and lights, 7 services and small-craft facilities, 11 shallow-water pattern),
  WMS 1.3.0, EPSG:3857, PNG
- Not drawn: layer 0 (information about the chart display), 8 (data quality), 9 (low accuracy), 10 (additional chart information) and 12
  (overscale warning). The overscale layer 12 hatches whole tiles with vertical lines at zoom 12 over Newport, RI (checked 2026-10-22).
- Terms: US Government work, public domain (https://www.datacommons.psu.edu/metadata/NOAA%20Electronic%20Navigational%20Charts-metadata.html;
  NOAA offers offline MBTiles itself, https://nauticalcharts.noaa.gov/data/gis-data-and-services.html). The capabilities' `Fees` and
  `AccessConstraints` are empty. Not re-read beyond the capabilities.
- Attribution: NOAA Office of Coast Survey, NOAA ENC chart display (US government, public domain). Max zoom 16.

### CHS chart (Canada coast)
- GUID `{7939ED8E-731B-4393-920D-D5DC9917D985}` (base chart), `"navigationWarning": true`
- Service: WMS `https://egisp.dfo-mpo.gc.ca/arcgis/rest/services/chs/ENC_MaritimeChartService/MapServer/exts/MaritimeChartService/WMSServer`,
  the same ArcGIS Maritime Chart Service extension and the same layers `1,2,3,4,5,6,7,11` as the NOAA chart (same layer names and ids),
  WMS 1.3.0, EPSG:3857, PNG. Dynamic rendering (no tile cache on the server), about 1 tile/s with one connection.
- Terms: open.canada.ca record https://open.canada.ca/data/en/dataset/12b769c8-48b8-4562-80fb-4a2e5ee9b45b, licence
  "Open Government Licence - Canada" (https://open.canada.ca/en/open-government-licence-canada): copy, publish, distribute in any
  medium with attribution. The capabilities' `Fees` and `AccessConstraints` are empty; the service says "This service is not for
  navigation." (read 2026-11-04).
- Attribution: Contains information licensed under the Open Government Licence - Canada. Source: Canadian Hydrographic Service
  (Fisheries and Oceans Canada), ENC Maritime Chart Service. Not for navigation. Max zoom 16.
- Coverage: Canadian ENC coverage; outside it the server answers a 363-byte transparent tile (stored like any tile, as the NOAA chart
  does outside the US).

### IH chart (Portugal coast, not for navigation)
- GUID `{3F0A6175-2933-4E3C-9495-57042D2D6EDA}` (base chart), `"navigationWarning": true`
- Service: WMS 1.3.0 `https://enc.hidrografico.pt/`, layer `ENC`, EPSG:3857, PNG, one connection
- Terms: capabilities `AccessConstraints` "Usage Licence - CC BY-NC-SA 4.0", abstract "This service must not be used for navigational purposes", "Access is free of charge for non-commercial purposes"; Hidrográfico+ terms §6: do not compromise the infrastructure's availability.
- Attribution: © Instituto Hidrográfico, ENC WMS, CC BY-NC-SA 4.0. Not for navigation. Max zoom 17.
- Coverage: Portuguese ENC coverage (mainland seen; Azores and Madeira not checked).

## Seamarks and traffic

### OpenSeaMap Seamarks
- GUID `{AB8A8685-AEC6-4E16-80DA-DC4AC37E4777}` (overlay layer)
- Service: `https://tiles.openseamap.org/seamark/{z}/{x}/{y}.png`
- Terms: https://wiki.openstreetmap.org/wiki/OpenSeaMap - "Chart tiles are available under CC-BY-SA"; and
  https://openseamap.org/index.php?id=kartendownload&L=1 - "OpenSeaMap-charts can be used on board even without access to internet".
- Attribution: Seamarks (c) OpenSeaMap contributors, CC-BY-SA

### EMODnet vessel density 2023, all ship types
- GUID `{99D06D6A-F7FA-4F06-8C00-D9F96307C0F3}` (overlay layer)
- Service: `https://ows.emodnet-humanactivities.eu/wms`, layer `vesseldensity_allavg`, WMS 1.3.0, EPSG:3857, PNG, `TIME=2023-01-01`
- Index: `https://ows.emodnet-humanactivities.eu/wms?SERVICE=WMS&VERSION=1.3.0&REQUEST=GetCapabilities` (GeoServer, workspace `emodnet`)
- Data: AIS shipping density, hours per square kilometre per month, 1 x 1 km grid, annual averages 2017-2024. `TIME` is a
  fixed year: 2023, the newest complete one (the provider lost satellite data from June 2024, so the 2024 average is thin).
  Tiles in a cache never mix years. For another year change `TIME` in `url`.
- Terms: the capabilities state `Fees` "NONE" and `AccessConstraints` "NONE"; the catalogue record
  (https://ows.emodnet-humanactivities.eu/geonetwork, id 0f2f3ff1-30ef-49e1-96e7-8ca78d58a07c) says
  "Creative Commons CC-BY 4.0 https://creativecommons.org/licenses/by/4.0/".
- Attribution: EMODnet Human Activities, Vessel Density Map 2023 (Cogea for EMODnet), CC BY 4.0. Max zoom 9 (a tile pixel is
  305 m at the equator, about 180 m at 54 N; the grid cell is 1 km).
- Coverage: European seas (the layer extent is -88 to 98 E, 15 to 87 N).

### EMODnet vessel density 2023, sailing and pleasure craft
- GUID `{478CBB2F-70D1-4573-8C50-B5361CE2AA46}` (overlay layer)
- Service: as above, `LAYERS=vesseldensity_04avg,vesseldensity_05avg` (the server has no single layer for both; the two are
  drawn together in one request, sailing and pleasure craft). Same `TIME`, terms, attribution and max zoom.
- Layers of the service (annual averages `vesseldensity_NNavg`, monthly totals `vesseldensity_NN`; other types are not shipped):
  `00` Other, `01` Fishing, `02` Service, `03` Dredging or underwater ops, `04` Sailing, `05` Pleasure Craft, `06` High speed
  craft, `07` Tug and towing, `08` Passenger, `09` Cargo, `10` Tanker, `11` Military and Law Enforcement, `12` Unknown,
  `all` / `allavg` All types.

### SHOM buoyage (France)
- GUID `{F8736AA0-56B2-49E8-9D55-9EF7263BD128}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://services.data.shom.fr/INSPIRE/wmts`, layer `BALISAGE_PYR_PNG_3857_WMTS`, style `normal`, matrix set `3857` (ids `0`..`21`, XYZ grid), PNG
- Terms: datasets under Licence Ouverte 2.0; the `/INSPIRE/` capabilities say `Fees` "Soumis à abonnement" and the WMS/r "sauf pour les thématiques INSPIRE qui sont en accès libre"; the data.shom.fr CGU was not found.
- Attribution: © Shom — Licence Ouverte 2.0. Non certifié pour la navigation. Max zoom 17.
- Coverage: French coasts.

### SHOM wrecks and obstructions (France)
- GUID `{B3F0F721-11B0-4806-AC4C-2A7219913F06}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://services.data.shom.fr/INSPIRE/wmts`, layer `EPAVES_PYR-PNG_WLD_3857_WMTS`, style `normal`, matrix set `3857` (ids `0`..`21`, XYZ grid), PNG
- Terms: datasets under Licence Ouverte 2.0; the `/INSPIRE/` capabilities say `Fees` "Soumis à abonnement" and the WMS/r "sauf pour les thématiques INSPIRE qui sont en accès libre"; the data.shom.fr CGU was not found.
- Attribution: © Shom — Licence Ouverte 2.0. Non certifié pour la navigation. Max zoom 17.
- Coverage: French waters.

### SHOM navigation rules and zones (France)
- GUID `{8D8C7EC7-8857-4419-9EDC-AD6F1CBB223A}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://services.data.shom.fr/INSPIRE/wmts`, layer `REGLEMENTATION_NAVIGATION_PYR_PNG_3857_WMTS`, style `normal`, matrix set `3857` (ids `0`..`21`, XYZ grid), PNG
- Terms: datasets under Licence Ouverte 2.0; the `/INSPIRE/` capabilities say `Fees` "Soumis à abonnement" and the WMS/r "sauf pour les thématiques INSPIRE qui sont en accès libre"; the data.shom.fr CGU was not found.
- Attribution: © Shom — Licence Ouverte 2.0. Non certifié pour la navigation. Max zoom 17.
- Coverage: French waters.


## Depth

### EMODnet Bathymetry (world)
- GUID `{C44877A0-D455-4238-B5E2-C8BFF25FC0D4}`
- Service: `https://tiles.emodnet-bathymetry.eu/2020/baselayer/web_mercator/{z}/{x}/{y}.png`
- Terms: https://tiles.emodnet-bathymetry.eu/wmts/1.0.0/WMTSCapabilities.xml - `<ows:Fees>None`, `<ows:AccessConstraints>none`;
  https://emodnet.ec.europa.eu/en/emodnet-web-service-documentation - "As images are pre-rendered and can be cached (locally or remotely)".
  Terms of use (https://emodnet.ec.europa.eu/en/terms-use-emodnet-online-services-data-and-data-products): EMODnet data products are CC BY 4.0.
  The `baselayer` layer is a compilation of EMODnet Bathymetry 2018, GEBCO 2019 and land DEM sources, so GEBCO is credited too.
- Attribution: EMODnet Bathymetry Consortium, CC BY 4.0; includes GEBCO 2019

### EMODnet Bathymetry (European seas, depth shading)
- GUID `{91D4F1B4-A7CD-4FC8-95F1-637609068FB2}`
- Service: `https://tiles.emodnet-bathymetry.eu/latest/mean_atlas_land/web_mercator/{z}/{x}/{y}.png`
- Terms: as above. Attribution: EMODnet Bathymetry Consortium

### CHS NONNA-10 bathymetry (Canadian waters)
- GUID `{B645CE30-A8D2-4B40-9E04-6823908F8EF3}` (base chart), `"navigationWarning": true`
- Service: WMTS (GeoServer GeoWebCache) `https://nonna-geoserver.data.chs-shc.ca/geoserver/gwc/service/wmts`, layer `nonna:NONNA 10`,
  matrix set `EPSG:900913`, matrix id `EPSG:900913:{z}` (key-value request, `TILEROW={y}&TILECOL={x}`), PNG.
- Terms: open.canada.ca record https://open.canada.ca/data/en/dataset/d3881c4c-650d-4070-bf9b-1e00aabf0a1d: Open Government
  Licence - Canada plus the CHS NONNA Licence (PDF attached to the record), "for NON-NAVIGATIONAL USE ONLY"; capabilities `Fees NONE`,
  `AccessConstraints NONE` (read 2026-11-04). Licence clauses (English part):
  - 12: it "comes into effect on the date the User confirms acceptance by clicking “I Agree” or otherwise accesses or uses the Data".
  - 3: "The Data shall not be used for navigation. The User acknowledges that the Data does not meet the requirements of the Navigation
    Safety Regulations, 2020 under the Canada Shipping Act, 2001 nor will any product created by the User from the Data."
  - 7: "Where the User creates a derivative product from the Data, the User shall include the following notice in a prominent location
    on such derivative product: "This product was made by [insert User's name or corporate name] and contains intellectual property of
    the Canadian Hydrographic Service (CHS) of the Department of Fisheries and Oceans. This product does not meet the requirements of
    the Navigation Safety Regulations, 2020 under the Canada Shipping Act, 2001. ..."".
  - 14: "The User shall destroy all copies of the Data in their possession within 15 days of termination of this Licence."

  The bundled definition carries the gist in its licence line; the clause 7 notice names the user, so a sailor who shares a chart file
  made from NONNA adds it himself.
- Attribution: Contains intellectual property of the Canadian Hydrographic Service (CHS), Fisheries and Oceans Canada; CHS NONNA Licence
  and Open Government Licence - Canada. Not for navigation. Max zoom 16 (a 10 m cell is about 4 px at z16 at 45 N).
- Coverage: Canadian waters, survey grid, patchy inshore; outside data a 1 225-byte transparent tile. Slow on a server cache miss (1-8 s
  per tile), hence 2 connections. No `termsOfUseWarning`: the CLI's fixed text ("may not allow downloading tiles for offline use") is wrong for NONNA.

### GEBCO bathymetry (world)
- GUID `{87D82F0F-4841-434F-ADB5-1D63B1BFE5DA}` (base chart), `"navigationWarning": true`
- Service: WMS `https://wms.gebco.net/mapserv`, layer `GEBCO_LATEST`, WMS 1.3.0, EPSG:3857, PNG
- Terms: grid is public domain, attribution required (https://www.gebco.net/data-products/gridded-bathymetry/terms-of-use);
  WMS `AccessConstraints` (2026-10-22): "Imagery from this WMS is not to be used for navigation or any purpose relating to safety at sea."
- Attribution: GEBCO Compilation Group, GEBCO_LATEST grid (public domain, attribution required). Max zoom 12 (the grid is about 450 m).

### NOAA BlueTopo bathymetry (US coast)
- GUID `{D73ACBDA-4E01-4C81-A36A-A069F38CF90C}` (base chart), `"navigationWarning": true`
- Service: WMS `https://nowcoast.noaa.gov/geoserver/bluetopo/wms`, layer `bathymetry` (the workspace also has `hillshade`, not used), WMS 1.3.0, EPSG:3857, PNG
- Terms: the capabilities state `Fees` "NONE" and `AccessConstraints` "NONE"; NOAA data, public domain (https://www.weather.gov/disclaimer).
- Attribution: NOAA National Bathymetric Source, BlueTopo via nowCOAST (US government, public domain). Max zoom 16.
- Coverage: US nearshore waters where BlueTopo exists; elsewhere the tile is transparent.

SHOM serves each survey as its own WMTS layer and its WMS refuses layer lists, so each is its own source.

### SHOM Litto3D lidar depth, Martinique 2016
- GUID `{967427CF-72EC-45F3-8DBA-0CCCC8762CE2}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://services.data.shom.fr/INSPIRE/wmts`, layer `LITTO3D_MART_2016_PYR_3857_WMTS`, style `normal`, matrix set `3857` (ids `0`..`21`, XYZ grid), PNG
- Terms: datasets under Licence Ouverte 2.0; the `/INSPIRE/` capabilities say `Fees` "Soumis à abonnement" and the WMS/r "sauf pour les thématiques INSPIRE qui sont en accès libre"; the data.shom.fr CGU was not found.
- Attribution: © Shom / IGN — Litto3D® / HOMONIM, Licence Ouverte 2.0. Non certifié pour la navigation. Max zoom 17.
- Coverage: Martinique coast (survey 2016).

### SHOM Litto3D lidar depth, Guadeloupe 2016
- GUID `{426323EC-65E1-493F-A2FA-DAB5D02E0D62}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://services.data.shom.fr/INSPIRE/wmts`, layer `LITTO3D_GUAD_2016_PYR_3857_WMTS`, style `normal`, matrix set `3857` (ids `0`..`21`, XYZ grid), PNG
- Terms: datasets under Licence Ouverte 2.0; the `/INSPIRE/` capabilities say `Fees` "Soumis à abonnement" and the WMS/r "sauf pour les thématiques INSPIRE qui sont en accès libre"; the data.shom.fr CGU was not found.
- Attribution: © Shom / IGN — Litto3D® / HOMONIM, Licence Ouverte 2.0. Non certifié pour la navigation. Max zoom 17.
- Coverage: Guadeloupe coast (survey 2016).

### SHOM Litto3D lidar depth, Saint-Martin 2019
- GUID `{C72F0919-06A3-4653-A452-02241CB692C0}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://services.data.shom.fr/INSPIRE/wmts`, layer `LITTO3D_STMARTIN_2019_PYR_3857_WMTS`, style `normal`, matrix set `3857` (ids `0`..`21`, XYZ grid), PNG
- Terms: datasets under Licence Ouverte 2.0; the `/INSPIRE/` capabilities say `Fees` "Soumis à abonnement" and the WMS/r "sauf pour les thématiques INSPIRE qui sont en accès libre"; the data.shom.fr CGU was not found.
- Attribution: © Shom / IGN — Litto3D® / HOMONIM, Licence Ouverte 2.0. Non certifié pour la navigation. Max zoom 17.
- Coverage: Saint-Martin coast (survey 2019).

### SHOM Litto3D lidar depth, Saint-Barthélemy 2019
- GUID `{A60F19C2-D083-4272-8EE8-46D248B6D9F3}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://services.data.shom.fr/INSPIRE/wmts`, layer `LITTO3D_STBARTHELEMY_2019_PYR_3857_WMTS`, style `normal`, matrix set `3857` (ids `0`..`21`, XYZ grid), PNG
- Terms: datasets under Licence Ouverte 2.0; the `/INSPIRE/` capabilities say `Fees` "Soumis à abonnement" and the WMS/r "sauf pour les thématiques INSPIRE qui sont en accès libre"; the data.shom.fr CGU was not found.
- Attribution: © Shom / IGN — Litto3D® / HOMONIM, Licence Ouverte 2.0. Non certifié pour la navigation. Max zoom 17.
- Coverage: Saint-Barthélemy coast (survey 2019).

### SHOM Litto3D lidar depth, Mayotte 2012
- GUID `{BACDAA84-2A28-48A1-8AD3-DAE910A37367}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://services.data.shom.fr/INSPIRE/wmts`, layer `LITTO3D_MAYOT_2012_PYR_3857_WMTS`, style `normal`, matrix set `3857` (ids `0`..`21`, XYZ grid), PNG
- Terms: datasets under Licence Ouverte 2.0; the `/INSPIRE/` capabilities say `Fees` "Soumis à abonnement" and the WMS/r "sauf pour les thématiques INSPIRE qui sont en accès libre"; the data.shom.fr CGU was not found. Dataset "Litto3D Mayotte 2012" on data.gouv.fr: Licence Ouverte 2.0 (read 2026-11-23).
- Attribution: © Shom / IGN — Litto3D®, Licence Ouverte 2.0. Non certifié pour la navigation. Max zoom 17.
- Coverage: Mayotte lagoon and coast (survey 2012).

### SHOM Litto3D lidar depth, Réunion 2016
- GUID `{E57F0098-371E-4477-A23E-A6330FF9075D}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://services.data.shom.fr/INSPIRE/wmts`, layer `LITTO3D_REUNION_2016_PYR_3857_WMTS`, style `normal`, matrix set `3857` (ids `0`..`21`, XYZ grid), PNG
- Terms: datasets under Licence Ouverte 2.0; the `/INSPIRE/` capabilities say `Fees` "Soumis à abonnement" and the WMS/r "sauf pour les thématiques INSPIRE qui sont en accès libre"; the data.shom.fr CGU was not found. Dataset "Litto3D Réunion 2016" on data.gouv.fr: Licence Ouverte 2.0 (read 2026-11-23).
- Attribution: © Shom / IGN — Litto3D®, Licence Ouverte 2.0. Non certifié pour la navigation. Max zoom 16.
- Coverage: Réunion coast (survey 2016).

### SHOM Litto3D lidar depth, Îles Éparses 2012
- GUID `{A24D9447-118F-4C65-B649-84F3DC66D88C}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://services.data.shom.fr/INSPIRE/wmts`, layer `LITTO3D_EPARSES_2012_PYR_3857_WMTS`, style `normal`, matrix set `3857` (ids `0`..`21`, XYZ grid), PNG
- Terms: datasets under Licence Ouverte 2.0; the `/INSPIRE/` capabilities say `Fees` "Soumis à abonnement" and the WMS/r "sauf pour les thématiques INSPIRE qui sont en accès libre"; the data.shom.fr CGU was not found. Dataset "Litto3D Iles Eparses 2012" on data.gouv.fr: Licence Ouverte 2.0 (read 2026-11-23).
- Attribution: © Shom / IGN — Litto3D®, Licence Ouverte 2.0. Non certifié pour la navigation. Max zoom 17.
- Coverage: Îles Éparses: Europa, Bassas da India, Juan de Nova, Glorieuses (survey 2012); transparent between the islands.

### SHOM lidar depth, Corsica 2017-2018
- GUID `{49DBB292-F25F-4FF3-AF2C-D2CFB2D56775}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://services.data.shom.fr/INSPIRE/wmts`, layer `L3D_LIDAR_CORSE_2017_2018_PYR_3857_WMTS`, style `normal`, matrix set `3857` (ids `0`..`21`, XYZ grid), PNG
- Terms: datasets under Licence Ouverte 2.0; the `/INSPIRE/` capabilities say `Fees` "Soumis à abonnement" and the WMS/r "sauf pour les thématiques INSPIRE qui sont en accès libre"; the data.shom.fr CGU was not found.
- Attribution: © Shom / IGN — Litto3D® / HOMONIM, Licence Ouverte 2.0. Non certifié pour la navigation. Max zoom 17.
- Coverage: Corsican coast (survey 2017-2018).

### SHOM lidar depth, Gulf of Morbihan 2015
- GUID `{935B45A2-1B70-4F02-8C6E-11878F5CADCB}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://services.data.shom.fr/INSPIRE/wmts`, layer `L3D_MAR_MORBIHAN_2015_PYR_3857_WMTS`, style `normal`, matrix set `3857` (ids `0`..`21`, XYZ grid), PNG
- Terms: datasets under Licence Ouverte 2.0; the `/INSPIRE/` capabilities say `Fees` "Soumis à abonnement" and the WMS/r "sauf pour les thématiques INSPIRE qui sont en accès libre"; the data.shom.fr CGU was not found.
- Attribution: © Shom / IGN — Litto3D®, Licence Ouverte 2.0. Non certifié pour la navigation. Max zoom 17.
- Coverage: Gulf of Morbihan (product dated 2015; the lidar itself was flown in 2005, with echo-sounder data of 2003 and 2013).

### SHOM 100 m depth, Guadeloupe and Martinique
- GUID `{02109A1A-0782-4E8D-AFC6-E26EFDF7411E}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://services.data.shom.fr/INSPIRE/wmts`, layer `MNT_ANTS100m_HOMONIM_3857_WMTS`, style `normal`, matrix set `3857` (ids `0`..`21`, XYZ grid), PNG
- Terms: datasets under Licence Ouverte 2.0; the `/INSPIRE/` capabilities say `Fees` "Soumis à abonnement" and the WMS/r "sauf pour les thématiques INSPIRE qui sont en accès libre"; the data.shom.fr CGU was not found.
- Attribution: © Shom / IGN — Litto3D® / HOMONIM, Licence Ouverte 2.0. Non certifié pour la navigation. Max zoom 13.
- Coverage: waters around Guadeloupe and Martinique (HOMONIM).

### SHOM 100 m depth, Saint-Martin, Saint-Barthélemy
- GUID `{F8D0CFB0-4844-4A0D-A87E-3D9FBA78FFB8}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://services.data.shom.fr/INSPIRE/wmts`, layer `MNT_ANTN100m_HOMONIM_PBMA_3857_WMTS`, style `normal`, matrix set `3857` (ids `0`..`21`, XYZ grid), PNG
- Terms: datasets under Licence Ouverte 2.0; the `/INSPIRE/` capabilities say `Fees` "Soumis à abonnement" and the WMS/r "sauf pour les thématiques INSPIRE qui sont en accès libre"; the data.shom.fr CGU was not found.
- Attribution: © Shom / IGN — Litto3D® / HOMONIM, Licence Ouverte 2.0. Non certifié pour la navigation. Max zoom 13.
- Coverage: waters around Saint-Martin and Saint-Barthélemy (HOMONIM).

### SHOM 100 m depth, Corsica
- GUID `{04C99FA6-041B-4E56-A154-0464F26F3620}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://services.data.shom.fr/INSPIRE/wmts`, layer `MNT_MED100m_CORSE_HOMONIM_PBMA_3857_WMTS`, style `normal`, matrix set `3857` (ids `0`..`21`, XYZ grid), PNG
- Terms: datasets under Licence Ouverte 2.0; the `/INSPIRE/` capabilities say `Fees` "Soumis à abonnement" and the WMS/r "sauf pour les thématiques INSPIRE qui sont en accès libre"; the data.shom.fr CGU was not found.
- Attribution: © Shom / IGN — Litto3D® / HOMONIM, Licence Ouverte 2.0. Non certifié pour la navigation. Max zoom 13.
- Coverage: waters around Corsica (HOMONIM).

### SHOM 100 m depth, Gulf of Lion and Côte d'Azur
- GUID `{EC8D7131-80C6-4B43-B77D-2B585B3772CE}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://services.data.shom.fr/INSPIRE/wmts`, layer `MNT_MED100m_GDL_CA_HOMONIM_PBMA_3857_WMTS`, style `normal`, matrix set `3857` (ids `0`..`21`, XYZ grid), PNG
- Terms: datasets under Licence Ouverte 2.0; the `/INSPIRE/` capabilities say `Fees` "Soumis à abonnement" and the WMS/r "sauf pour les thématiques INSPIRE qui sont en accès libre"; the data.shom.fr CGU was not found.
- Attribution: © Shom / IGN — Litto3D® / HOMONIM, Licence Ouverte 2.0. Non certifié pour la navigation. Max zoom 13.
- Coverage: French Mediterranean mainland waters (HOMONIM).

### SHOM lidar depth, Bora Bora 2015
- GUID `{CE3F4D53-EE39-412E-9C9F-29722410DD5B}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://services.data.shom.fr/INSPIRE/wmts`, layer `L3D_LIDAR_POLYNESIE_BOR_2015_WMTS_3857`, style `normal`, matrix set `3857` (ids `0`..`21`, XYZ grid), PNG
- Terms: datasets under Licence Ouverte 2.0; the `/INSPIRE/` capabilities say `Fees` "Soumis à abonnement" and the WMS/r "sauf pour les thématiques INSPIRE qui sont en accès libre"; the data.shom.fr CGU was not found.
- Attribution: © Shom / IGN — Litto3D® / HOMONIM, Licence Ouverte 2.0. Non certifié pour la navigation. Max zoom 17.
- Coverage: Bora Bora (survey 2015).

### SHOM coastal depth model, Tahiti
- GUID `{147FA47B-9A99-4437-A490-56A3339D1CFD}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://services.data.shom.fr/INSPIRE/wmts`, layer `MNT_COTIER_TAHITI_PBMA_3857_WMTS`, style `normal`, matrix set `3857` (ids `0`..`21`, XYZ grid), PNG
- Terms: datasets under Licence Ouverte 2.0; the `/INSPIRE/` capabilities say `Fees` "Soumis à abonnement" and the WMS/r "sauf pour les thématiques INSPIRE qui sont en accès libre"; the data.shom.fr CGU was not found.
- Attribution: © Shom / IGN — Litto3D® / HOMONIM, Licence Ouverte 2.0. Non certifié pour la navigation. Max zoom 17.
- Coverage: Tahiti coastal waters (lidar and soundings merged).

### SHOM coastal depth model, Moorea
- GUID `{00B62B3C-C5E1-4F2D-BB60-78B330D9C4AF}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://services.data.shom.fr/INSPIRE/wmts`, layer `MNT_COTIER_MOOREA_PBMA_3857_WMTS`, style `normal`, matrix set `3857` (ids `0`..`21`, XYZ grid), PNG
- Terms: datasets under Licence Ouverte 2.0; the `/INSPIRE/` capabilities say `Fees` "Soumis à abonnement" and the WMS/r "sauf pour les thématiques INSPIRE qui sont en accès libre"; the data.shom.fr CGU was not found.
- Attribution: © Shom / IGN — Litto3D® / HOMONIM, Licence Ouverte 2.0. Non certifié pour la navigation. Max zoom 17.
- Coverage: Moorea coastal waters (lidar and soundings merged).

### SHOM 100 m depth, Tahiti and Moorea
- GUID `{75AD4560-CDA0-4C4B-B185-CBB90795FAB1}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://services.data.shom.fr/INSPIRE/wmts`, layer `MNT_FACADE_TAHITI_MOOREA_100m_PBMA_3857_WMTS`, style `normal`, matrix set `3857` (ids `0`..`21`, XYZ grid), PNG
- Terms: datasets under Licence Ouverte 2.0; the `/INSPIRE/` capabilities say `Fees` "Soumis à abonnement" and the WMS/r "sauf pour les thématiques INSPIRE qui sont en accès libre"; the data.shom.fr CGU was not found.
- Attribution: © Shom / IGN — Litto3D® / HOMONIM, Licence Ouverte 2.0. Non certifié pour la navigation. Max zoom 15.
- Coverage: waters around Tahiti and Moorea; levels 3-15.

### SHOM 100 m depth, New Caledonia
- GUID `{9441ED23-4236-4D96-BF7D-FDFB4C8BCF4C}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://services.data.shom.fr/INSPIRE/wmts`, layer `MNT_NC100m_PBMA_3857_WMTS`, style `normal`, matrix set `3857` (ids `0`..`21`, XYZ grid), PNG
- Terms: datasets under Licence Ouverte 2.0; the `/INSPIRE/` capabilities say `Fees` "Soumis à abonnement" and the WMS/r "sauf pour les thématiques INSPIRE qui sont en accès libre"; the data.shom.fr CGU was not found.
- Attribution: © Shom / IGN — Litto3D® / HOMONIM, Licence Ouverte 2.0. Non certifié pour la navigation. Max zoom 13.
- Coverage: New Caledonia (on SHOM's `/INSPIRE/` service).

### SHOM seabed type (France)
- GUID `{8BC59B43-9257-40CA-B59C-807A2652EAE5}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://services.data.shom.fr/INSPIRE/wmts`, layer `NDF_PYR-PNG_WLD_3857_WMTS`, style `normal`, matrix set `3857` (ids `0`..`21`, XYZ grid), PNG
- Terms: datasets under Licence Ouverte 2.0; the `/INSPIRE/` capabilities say `Fees` "Soumis à abonnement" and the WMS/r "sauf pour les thématiques INSPIRE qui sont en accès libre"; the data.shom.fr CGU was not found.
- Attribution: © Shom — Licence Ouverte 2.0. Non certifié pour la navigation. Max zoom 13.
- Coverage: mainland France ("Nature des fonds métropole", 1:50 000).

### EMODnet seabed type (European seas)
- GUID `{A6633591-1547-41BF-A6C3-6F8B74112EBE}` (overlay layer), `"navigationWarning": true`
- Service: WMS 1.3.0 `https://drive.emodnet-geology.eu/geoserver/gtk/wms`, the 14 `seabed_substrate_*` layers in one request, coarse first (`1m`, `250k`, `100k`, then the `*_multiscale` layers from `70k` down to `1k5`), so a finer layer covers a coarser one where it has data; default style (Folk 5 classes: mud to sandy mud, sand, coarse sediment, mixed sediment, rock and boulders), EPSG:3857, PNG, two connections
- Terms: EMODnet terms of use (https://emodnet.ec.europa.eu/en/terms-use-emodnet-online-services-data-and-data-products): "Unless indicated otherwise, data products created by EMODnet are owned by the EU and therefore licensed under Creative Commons CC-BY 4.0." Capabilities `Fees` "NONE". Read 2026-11-23.
- Attribution: Licensed under CC BY 4.0 from EMODnet Geology. Not for navigation. Max zoom 15.
- Coverage: European seas. The 1:1 000 000 and 1:250 000 layers are general; the fine layers (1:70 000 to 1:1 500) cover only some areas. Polygons are opaque.

### Geological Survey Ireland seabed type (Irish waters)
- GUID `{32A5CE5D-0FE1-4AD0-A14C-E0B147B3E392}` (overlay layer), `"navigationWarning": true`
- Service: WMS 1.3.0 (ArcGIS) `https://gsi.geodata.gov.ie/server/services/Marine/IE_GSI_MI_Seabed_Sediment_Classification_IE_Waters_WGS84/MapServer/WMSServer`, layer `IE_GSI_MI_Seabed_Sediment_Classification_IE_Waters_WGS84`, EPSG:3857, PNG, two connections
- Terms: capabilities `AccessConstraints`: "Data that is produced directly by the Geological Survey Ireland (GSI) is free for use under the conditions of Creative Commons Attribution 4.0 International license ... users must acknowledge the source of the Information in their product or application." `Fees` "none". Read 2026-11-23.
- Attribution: Contains Irish Public Sector Data (Geological Survey Ireland & Marine Institute), CC BY 4.0. Not for navigation. Max zoom 15.
- Coverage: Irish waters (-25.0 to -5.5 E, 46.8 to 57.4 N). Classified from multibeam backscatter and grab samples (INFOMAR); grey polygons are "No data at this level of Folk". Polygons are partly see-through.

### NGU seabed type (Norway)
- GUID `{F85B7E2A-801F-464A-B064-8F6A418489EC}` (overlay layer), `"navigationWarning": true`
- Service: WMS 1.3.0 `https://geo.ngu.no/mapserver/MarinBunnsedimenterWMS/`, layers `Kornstorrelse_Reg` (grain size, 1:100 000), `Kornstorrelse_Det` (grain size, 1:20 000 to 1:150 000, drawn from about zoom 8) and `Bunntype` (bottom-type points from hydrographic surveys), in that order; EPSG:3857, PNG, two connections
- Terms: Norwegian Licence for Open Government Data (NLOD) 1.0 (data.norge.no record "Bunnsedimenter, kornstørrelse, detaljert", which lists this WMS); capabilities `Fees` "no conditions apply", `AccessConstraints` "Copyright The Geological Survey of Norway (NGU) 2024". Read 2026-11-23.
- Attribution: © NGU, NLOD. Not for navigation. Max zoom 15.
- Coverage: Norwegian fjords and coastal waters where NGU has mapped; patchy (no data at the Lofoten and Bergen points checked). Polygons and symbols are opaque.

### Satellite-derived depth (Pacific islands, DEP)
- GUID `{F12728E1-8C83-4B3C-9763-E0651ED9371C}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://ows.prod.digitalearthpacific.io/wmts` (key-value GetTile), layer `sdb`, style `blues_depth`, matrix set `WholeWorld_WebMercator` (XYZ grid, 0..14), PNG
- Terms: DEP STAC `dep_s2_sdb` licence "CC-BY-4.0"; WMTS `Fees`/`AccessConstraints` none.
- Attribution: Digital Earth Pacific (SPC), CC BY 4.0; contains modified Copernicus Sentinel data. Not for navigation. Max zoom 14.
- Coverage: Pacific island countries of the SPC.


## Satellite

Imagery of all thirty-three below. Google, Bing and Esri high-resolution imagery cannot be shipped (see "Not shipped"):
Sentinel-2 at 10 m and USGS show reef edges, banks and pass entrances, not single coral heads; the national orthophotos (France, Spain, the Netherlands, Germany, Flanders, Estonia, Gothenburg, Helsinki, Turku, Uruguay, Argentina, Japan, Hong Kong, Madeira, the Caribbean Netherlands, Portugal, Emilia-Romagna, Abruzzo, Albania, Sicily) are far sharper but cover only their own waters.

### Sentinel-2 Cloudless 2025 (EOX)
- GUID `{92122135-6B15-4800-AA9E-3E48C556DA17}`
- Service: `https://tiles.maps.eox.at/wmts/1.0.0/s2cloudless-2025_3857/default/g/{z}/{y}/{x}.jpg` (row before column)
- Terms: https://cloudless.eox.at/documentation/license - "You may use EOxCloudless freely for non-commercial purposes
  under the terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License" (CC BY-NC-SA 4.0).
  The server rate-limits (https://maps.eox.at/), so the definition allows one connection.
- Attribution: EOxCloudless https://cloudless.eox.at by EOX IT Services GmbH (Contains modified Copernicus Sentinel data 2025)

### USGS Imagery (USA)
- GUID `{E05EDD5E-C6C3-483C-8BA7-8DE1D9892ED8}`
- Service: `https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer/tile/{z}/{y}/{x}` (US coverage only;
  the server refuses a non-browser User-Agent)
- Terms: https://www.usgs.gov/information-policies-and-instructions/copyrights-and-credits - "USGS-authored or produced data
  and information are considered to be in the U.S. Public Domain".
- Attribution: U.S. Geological Survey, public domain
- Not Alaska: over Alaska and the Aleutians the service shows a 2020 mosaic whose images say "(C) COPYRIGHT 2020 Maxar Technologies, Inc." (read 2026-11-06), not public-domain imagery. The definition's script asks for no tile that touches one of three boxes: 180° W to 129.2° W, 54.5° N to 71.5° N; 180° W to 162.0° W, 51.1° N to 54.5° N; 172.3° E to 180°, 51.3° N to 53.1° N. These tiles count as missing, and a chart wholly inside the boxes is not made. The boxes also take the Yukon, the British Columbia coast north of 54.5° N, eastern Chukotka, and at zoom 4 and below the large tiles that reach into a box.

### IGN orthophoto 20 cm (France and overseas)
- GUID `{5BFDF522-5EC4-4C7C-A7DB-BB1B827C2622}`
- Service: WMTS `https://data.geopf.fr/wmts`, layer `HR.ORTHOIMAGERY.ORTHOPHOTOS` ("Ortho 20 cm", BD ORTHO V3), matrix set `PM_6_19`
  (levels 6-19, XYZ-compatible; nothing below 6: the server answers HTTP 404, which the CLI counts as "not on the server", not a failure).
- Terms: capabilities `Fees` "none", `AccessConstraints` "Conditions Générales d'Utilisation disponibles ici : https://cartes.gouv.fr/cgu";
  CGU art. 3.2: "API Géoplateforme - Diffusion d'images tuilées WMTS: Non soumis à limite d'usage", and IGN "peut limiter ou bloquer
  l'accès" on "usage abusif" (hence 2 connections). Data under the Licence Ouverte / Open Licence Etalab 2.0, which asks for the source
  and the date of last update, satisfiable by the URL (https://github.com/etalab/licence-ouverte/blob/master/LO.md, lines 15 and 17).
- Attribution: IGN France, BD ORTHO 20 cm, https://data.geopf.fr (Licence Ouverte Etalab 2.0). Max zoom 19.
- Coverage: mainland France, Corsica, Guadeloupe, Martinique, Saint-Martin, Saint-Barthélemy, Guyane, Réunion, Mayotte,
  Saint-Pierre-et-Miquelon (20 cm), Wallis-et-Futuna (50 cm); NOT French Polynesia or New Caledonia.

### PNOA orthophoto (Spain, Balearics, Canaries)
- GUID `{094D4409-B58E-424F-ADD0-499AF513ED82}`
- Service: WMTS `https://www.ign.es/wmts/pnoa-ma`, layer `OI.OrthoimageCoverage`, matrix set `GoogleMapsCompatible`, JPEG.
- Terms: capabilities `AccessConstraints` "CC BY 4.0 scne.es", `Fees` "No se aplican condiciones".
- Attribution: PNOA cedido por © Instituto Geográfico Nacional de España, CC BY 4.0 scne.es. Max zoom 19 (z20 is upsampled).
- Coverage: Spain including the Balearics and the Canaries; PNOA 25-50 cm, Sentinel-2 below about 1:70 000.

### PDOK aerial photo (Netherlands)
- GUID `{DCF3A72B-CE26-40A1-9A9B-00D20A7219F1}`
- Service: WMTS REST `https://service.pdok.nl/hwh/luchtfotorgb/wmts/v1_0/Actueel_orthoHR/EPSG:3857/{z}/{x}/{y}.jpeg` (the capabilities'
  ResourceURL template), layer `Actueel_orthoHR` (the high-resolution current mosaic; `Actueel_ortho25` is the 25 cm one). The set
  writes matrix ids with two digits (`00`...`21`); the server also answers plain `8` (byte-identical to `08`), so `{z}` works.
- Terms: capabilities `Fees` / `AccessConstraints` "none"; PDOK article https://www.pdok.nl/introductie/-/article/pdok-luchtfoto-rgb-open-:
  "voor iedereen kosteloos en vrij beschikbaar voor alle toepassingen, onder een CC-BY licentie".
- Attribution: Luchtfoto: Beeldmateriaal Nederland / PDOK, CC BY 4.0. Max zoom 19.
- Coverage: the Netherlands including the Wadden and Zeeland, two flights a year.

### Satellite mosaic 2025 (Pacific islands, DEP)
- GUID `{1C427DB9-2798-4ADE-803D-3F0F865CA1BD}` (base chart)
- Service: WMTS `https://ows.prod.digitalearthpacific.io/wmts` (key-value GetTile), layer `dep_s2_geomad`, style `simple_rgb`, `TIME=2025-01-01`, matrix set `WholeWorld_WebMercator`, PNG
- Terms: DEP STAC `dep_s2_geomad` "CC-BY-4.0", ESA listed as producer/licensor; WMTS `Fees`/`AccessConstraints` none.
- Attribution: Digital Earth Pacific (SPC), Sentinel-2 GeoMAD, CC BY 4.0; contains modified Copernicus Sentinel data. Max zoom 14.
- Coverage: Pacific island countries of the SPC.

### Regione Toscana orthophoto 2023 (Tuscany, Elba)
- GUID `{AC9F22EE-9024-4561-824E-B039C94841AE}` (base chart)
- Service: WMS 1.3.0 `https://www502.regione.toscana.it/ows_ofc/com.rt.wms.RTmap/wms?map=owsofc_rt`, layer `rt_ofc.5k23.32bit`, EPSG:3857, JPEG
- Terms: metadata "Regione Toscana - Ortofoto anno 2023": "liberamente accessibile e usabile con licenza CC BY 4.0". Only the 2023 layer: the 2024/25 licence was not found.
- Attribution: Regione Toscana — Ortofoto 2023, CC BY 4.0. Max zoom 19.
- Coverage: Tuscany incl. Elba and the islands.

### theLIST orthophoto (Tasmania)
- GUID `{0B84A398-3EA3-420D-8322-839AF8D5E6BB}` (base chart)
- Service: ArcGIS tile cache `https://services.thelist.tas.gov.au/arcgis/rest/services/Basemaps/Orthophoto/MapServer/tile/{z}/{y}/{x}`, wkid 3857, JPEG, levels 0-19
- Terms: MapServer `copyrightText` "Creative Commons BY-NC-ND 3.0 AU © State of Tasmania"; `exportTilesAllowed` true.
- Attribution: Creative Commons BY-NC-ND 3.0 AU © State of Tasmania. Max zoom 19.
- Coverage: Tasmania.

### IDE Uruguay orthophoto 2017-18 (Uruguay coast)
- GUID `{497CB539-3E59-4BEC-AD24-3DE6DD52A76F}` (base chart)
- Service: WMTS `https://mapas.ide.uy/geoserver-raster/gwc/service/wmts` (key-value GetTile), layer `ortofotos:ortofoto_nacional`, style `raster`, matrix set `EPSG:3857` (ids `EPSG:3857:0`..`EPSG:3857:20`), JPEG
- Terms: national open-data catalogue, dataset "Ortoimagen y modelo digital del terreno de cobertura nacional", licence "Licencia de Datos Abiertos – Uruguay" (commercial and non-commercial reuse and redistribution; name the provider, the licence and the dataset; mention changes); WMTS `Fees`/`AccessConstraints` NONE.
- Attribution: IDE Uruguay — Ortoimágenes 2017–2018, Licencia de Datos Abiertos – Uruguay; imagen reproyectada y teselada. Max zoom 19.
- Coverage: the whole country at 32 cm, incl. the Río de la Plata and Atlantic shore.

### IGN Argentina aerial photo (Buenos Aires, Paraná, towns)
- GUID `{DAA88925-58F8-4B43-BEE3-A9434C1CA070}` (base chart)
- Service: WMTS `https://imagenes.ign.gob.ar/geoserver/gwc/service/wmts` (key-value GetTile), layer `mosaicos_vuelos`, empty style, matrix set `EPSG:3857` (ids `EPSG:3857:{z}`), PNG
- Terms: WMTS `AccessConstraints` "Se permite buscar, acceder, solicitar, recibir, copiar, analizar, reprocesar, reutilizar y redistribuir libremente la información publicada por este servicio según el Artículo 2 de la Ley 27.275 …"; IGN terms: cite "FUENTE: Instituto Geográfico Nacional de la República Argentina" and the data date, do not alter the data, share it free of charge.
- Attribution: FUENTE: Instituto Geográfico Nacional de la República Argentina — mosaicos de vuelos 2011–2022. Max zoom 19.
- Coverage: photogrammetric flights only: Buenos Aires and the Río de la Plata shore, the Paraná, Necochea, Río Gallegos, Río Grande; elsewhere an empty tile.

### GSI aerial photo (Japan)
- GUID `{17A4115B-0232-4DCD-9FDE-E33E6448CEE3}` (base chart)
- Service: XYZ `https://cyberjapandata.gsi.go.jp/xyz/seamlessphoto/{z}/{x}/{y}.jpg` (GSI Tiles, layer "seamless photo", 全国最新写真（シームレス）), JPEG
- Terms: GSI tile list (https://maps.gsi.go.jp/development/ichiran.html): use under the GSI Content Terms, credit "地理院タイル" or "国土地理院", link to the tile list, tiles with GRUS imagery credit "GRUS画像（© Axelspace）"; reading tiles in software needs no application. GSI Content Terms (https://www.gsi.go.jp/kikakuchousei/kikakuchousei40182.html): Public Data License 1.0 (compatible with CC BY 4.0: copy, transmit, adapt, commercial use, with the credit; an edited copy must say so and must not look like GSI's own). Photo tiles are not basic survey results, so no Survey Act application (https://www.gsi.go.jp/LAW/2930-index.html). Read 2026-11-06.
- Attribution: Source: GSI Tiles (地理院タイル), seamless photo (全国最新写真（シームレス）), Geospatial Information Authority of Japan, https://maps.gsi.go.jp/development/ichiran.html; GRUS画像（© Axelspace） in some tiles; PDL 1.0. Max zoom 18.
- Coverage: Japan; Okinawa seen, the outer islands not checked. Notes: the newest photo per area, dates not stated; some tiles are Landsat-8 or GRUS satellite imagery. GSI's standard and pale maps are not used (they are basic survey results).

### Satellite mosaic 2025 (Africa, DE Africa)
- GUID `{027DCBF5-2DF0-4F4A-92FA-361EF8150212}` (base chart)
- Service: WMTS `https://ows.digitalearth.africa/wmts` (key-value GetTile), layer `gm_s2_annual`, style `simple_rgb`, `TIME=2025-01-01`, matrix set `WholeWorld_WebMercator` (XYZ grid, 0..14), PNG
- Terms: DE Africa GeoMAD specification (https://docs.digitalearthafrica.org/en/latest/data_specs/GeoMAD_specs.html): "CC BY Attribution 4.0 International License", credit "Contains modified Copernicus Sentinel data …, processed by Digital Earth Africa."; WMTS `Fees`/`AccessConstraints` none. The external datasets on the same WMTS (WorldCover and others) are not used: they keep their own terms. Read 2026-11-06.
- Attribution: Contains modified Copernicus Sentinel data 2025, processed by Digital Earth Africa. CC BY 4.0. Max zoom 14.
- Coverage: Africa and its islands (layer extent -27.86 to 65.67 E, -48.31 to 39.00 N); not the Maldives, Chagos or Sri Lanka. Notes: annual 2025 median, 10 m; the twin of the Pacific DEP mosaic.

### LandsD aerial photo (Hong Kong)
- GUID `{6E6A0E91-A076-4651-A530-F7C579910F34}` (base chart)
- Service: XYZ `https://mapapi.hkmapservice.gov.hk/gs/api/v1.0.0/xyz/imagery/WGS84/{z}/{x}/{y}.png` (Lands Department Imagery Map API; `WGS84` in the path is the Web Mercator tile scheme), PNG, one connection
- Terms: CSDI Portal Terms and Conditions of Use (https://portal.csdi.gov.hk/csdi-webpage/doc/TNC): browse, download, distribute and reproduce the data for commercial and non-commercial purposes free of charge, naming the Government and the CSDI Portal as the source and acknowledging the Government's ownership. Imagery Map API page (https://portal.csdi.gov.hk/csdi-webpage/apidoc/ImageryMapAPI): copyright notice "Aerial Photograph from Lands Department" and, for the satellite images, "(Satellite Images from Landsat)"; no "large amount of requests within a short period". The page also asks for the Lands Department logo on the map face; OpenCPN draws no logo on chart tiles, so the credit is given in this line, the source information and the user guide. Read 2026-11-06.
- Attribution: Aerial Photograph from Lands Department; Guangdong-Hong Kong-Macao Greater Bay Area Satellite Image 2024 (Satellite Images from Landsat). © Government of the Hong Kong SAR; source: the Government and the CSDI Portal (https://portal.csdi.gov.hk). Max zoom 19.
- Coverage: Hong Kong; Landsat imagery of the region at low zooms. Notes: the service gives no photo date; the tiles show the Kai Tak stadium roof closed and the Cross Bay Link (opened December 2022) built, so 2023 or later. The old domain mapapi.geodata.gov.hk is not used (it ends on 15 March 2027).

### DROTe orthophoto 2018-19 (Madeira, Porto Santo)
- GUID `{CAF750A4-01B2-4EB9-A7BE-01E6E98535A7}` (base chart)
- Service: WMS 1.3.0 `https://geoportal-irig.madeira.gov.pt/mapproxy/base/service`, layer `drote_ortos2018_ortos_2018`, EPSG:3857, JPEG, one connection (the service's WMTS has no Web Mercator tile set)
- Terms: iRIG catalogue record "Ortofotomapas à escala 1:2000 da Região Autónoma da Madeira - 2018/2019" (id 4387e6ca-0962-4872-b558-6203ae24eace, https://geoportal-irig.madeira.gov.pt/geonetwork): "Acesso público sem restrições"; "Licença Creative Commons – Atribuição (CC-BY-4.0). Deverá atribuir créditos com inclusão de texto “Informação geográfica cedida pela Direção Regional do Ordenamento do Território” ou “© DROTe” ou “Fonte: DROTe”." WMS capabilities `Fees`/`AccessConstraints` "none". Read 2026-11-06. The 2023 layer of the same service is not used: no licence record was found for it.
- Attribution: © DROTe — Informação geográfica cedida pela Direção Regional do Ordenamento do Território; Ortofotomapas 1:2000 2018/2019, CC BY 4.0. Max zoom 19.
- Coverage: Madeira, Porto Santo, Desertas and Selvagens (record); Funchal and Porto Santo marinas seen. Notes: 10 cm, flown 3 October 2018 to 19 June 2019.

### Beeldmateriaal aerial photo 2024 (Bonaire)
- GUID `{69A6470E-6B6E-40AF-8366-D16C81079B60}` (base chart)
- Service: WMTS (KVP GetTile) `https://api.ellipsis-drive.com/v3/ogc/wmts/07a0f368-478f-4377-a753-c66d7b0c3f6f`, layer `f462790a-88ab-404c-9e6c-83fca88ccfb3_e0ed4080-c898-43e0-965c-a4310de2da31`, style `default`, matrix set `EPSG:3857` (ids `0`..`22`, the Web Mercator grid, a reprojection of the island's own `EPSG:10759` set), PNG, two connections
- Terms: Beeldmateriaal dataroom page (https://www.beeldmateriaal.nl/dataroom-caribisch-nederland): "De data is beschikbaar onder de Creative Commons Naamsvermelding 4.0 licentie" (CC BY 4.0). Hosted on Ellipsis Drive: no key; use is metered on the publisher's account, so the definition keeps two connections and max zoom 20. Read 2026-11-23.
- Attribution: Beeldmateriaal Nederland — Orthofotomozaïek Caribisch Nederland 2023–2024, CC BY 4.0. Max zoom 20 (the service goes to 22; 20 is about 15 cm per pixel).
- Coverage: Bonaire (-68.43 to -68.18 E, 12.02 to 12.32 N). Outside the island the tiles are transparent. Notes: flown autumn 2024 (the dataroom page).

### Beeldmateriaal aerial photo 2023-24 (Saba)
- GUID `{D7D1A3F3-8EE7-4091-9E97-A55EB20248AE}` (base chart)
- Service: WMTS (KVP GetTile) `https://api.ellipsis-drive.com/v3/ogc/wmts/37a9029d-7578-4471-826d-3b40d721b83b`, layer `007963d5-7a71-4d40-b6ce-b4440306244f_786eb116-19ff-4406-bd3e-3d03bf8b89af`, style `default`, matrix set `EPSG:3857` (ids `0`..`22`, the Web Mercator grid, a reprojection of the island's own `EPSG:10641` set), PNG, two connections
- Terms: Beeldmateriaal dataroom page (https://www.beeldmateriaal.nl/dataroom-caribisch-nederland): "De data is beschikbaar onder de Creative Commons Naamsvermelding 4.0 licentie" (CC BY 4.0). Hosted on Ellipsis Drive: no key; use is metered on the publisher's account, so the definition keeps two connections and max zoom 20. Read 2026-11-23.
- Attribution: Beeldmateriaal Nederland — Orthofotomozaïek Caribisch Nederland 2023–2024, CC BY 4.0. Max zoom 20 (the service goes to 22; 20 is about 15 cm per pixel).
- Coverage: Saba (-63.26 to -63.21 E, 17.61 to 17.65 N). Outside the island the tiles are transparent. Notes: flown around the turn of 2023-24.

### Beeldmateriaal aerial photo 2023-24 (Sint Eustatius)
- GUID `{8BC940C1-16FD-4739-B47D-E5A0425FD173}` (base chart)
- Service: WMTS (KVP GetTile) `https://api.ellipsis-drive.com/v3/ogc/wmts/cc50d13b-33cf-4d6e-ab41-9e5fb4651478`, layer `e4db515f-ac3e-4bee-aaae-bfa27f7d1dae_4d242928-ebbf-4ddd-9e65-77a4f1058aaf`, style `default`, matrix set `EPSG:3857` (ids `0`..`22`, the Web Mercator grid, a reprojection of the island's own `EPSG:10745` set), PNG, two connections
- Terms: Beeldmateriaal dataroom page (https://www.beeldmateriaal.nl/dataroom-caribisch-nederland): "De data is beschikbaar onder de Creative Commons Naamsvermelding 4.0 licentie" (CC BY 4.0). Hosted on Ellipsis Drive: no key; use is metered on the publisher's account, so the definition keeps two connections and max zoom 20. Read 2026-11-23.
- Attribution: Beeldmateriaal Nederland — Orthofotomozaïek Caribisch Nederland 2023–2024, CC BY 4.0. Max zoom 20 (the service goes to 22; 20 is about 15 cm per pixel).
- Coverage: Sint Eustatius (-63.01 to -62.94 E, 17.46 to 17.53 N). Outside the island the tiles are transparent. Notes: flown around the turn of 2023-24.

### LVermGeo SH orthophoto (Schleswig-Holstein)
- GUID `{B7950BF1-42A5-4DBD-9277-317E8173B40E}` (base chart)
- Service: WMS 1.3.0 `https://dienste.gdi-sh.de/WMS_SH_DOP20col_OpenGBD`, layer `sh_dop20_rgb`, EPSG:3857, JPEG, two connections
- Terms: capabilities `Fees`: "Für die Nutzung der Daten ist die Creative Commons (CC BY 4.0) – Namensnennung 4.0 International anzuwenden … Der Quellenvermerk lautet "© GeoBasis-DE/LVermGeo SH/CC BY 4.0""; `AccessConstraints` "NONE". Read 2026-11-23.
- Attribution: © GeoBasis-DE/LVermGeo SH/CC BY 4.0. Max zoom 19.
- Coverage: Schleswig-Holstein, Baltic and North Sea coast with the islands (7.82 to 11.42 E, 53.30 to 55.15 N). Notes: 20 cm; the capabilities give no flight year.

### LGLN orthophoto (Lower Saxony, East Frisian Islands)
- GUID `{C5486F37-67D7-4306-8412-9261B62EDF71}` (base chart)
- Service: WMS 1.3.0 `https://opendata.lgln.niedersachsen.de/doorman/noauth/dop_wms`, layer `ni_dop20`, EPSG:3857, JPEG, two connections
- Terms: capabilities `AccessConstraints`: CC BY 4.0; "Als Rechteinhaber und Bereitsteller ist "LGLN", sowie das Jahr des Datenbezugs in Klammern anzugeben". Read 2026-11-23; the year in the attribution is the year this definition was written.
- Attribution: LGLN (2026) Creative Commons Namensnennung – 4.0 International (CC BY 4.0). Max zoom 19.
- Coverage: Lower Saxony with the Wadden Sea and the East Frisian islands (6.51 to 11.75 E, 51.15 to 54.15 N). Notes: 20 cm.

### GeoInformation Bremen orthophoto 2025 (Bremen, Bremerhaven)
- GUID `{CE3EB174-5386-46B5-932B-B36FA0125CD7}` (base chart)
- Service: WMS 1.3.0 `https://geodienste.bremen.de/wms_dop_lb`, layers `dop10_2025_HB,dop10_2025_BHV` in one request, EPSG:3857, JPEG, two connections
- Terms: capabilities `Fees`: "Creative Commons Namensnennung (CC-BY), Quellenvermerk: Landesamt GeoInformation Bremen"; `AccessConstraints`: "Es gelten keine Zugriffsbeschränkungen". Read 2026-11-23.
- Attribution: Landesamt GeoInformation Bremen — DOP10 2025, CC BY. Max zoom 20.
- Coverage: Bremerhaven (8.46 to 8.68 E, 53.45 to 53.63 N) and Bremen (8.46 to 9.01 E, 53.01 to 53.23 N). Notes: 10 cm, 2025.

### LGV orthophoto 2026 (Hamburg, the Elbe)
- GUID `{82DF084C-7FCB-4451-9AFA-E5D71309D0AD}` (base chart)
- Service: WMS 1.3.0 `https://geodienste.hamburg.de/wms_dop_zeitreihe_unbelaubt`, layer `dop_zeitreihe_unbelaubt`, `TIME=2026`, EPSG:3857, JPEG, two connections
- Terms: capabilities `Fees`: "Datenlizenz Deutschland Namensnennung 2.0, Quellenvermerk: Freie und Hansestadt Hamburg, Landesbetrieb Geoinformation und Vermessung (LGV)"; the same field names a Maxar picture under a closed licence in the time series, so the definition always asks for the LGV year 2026. Read 2026-11-23.
- Attribution: Freie und Hansestadt Hamburg, Landesbetrieb Geoinformation und Vermessung (LGV) — DOP 2026, dl-de/by-2-0. Max zoom 19.
- Coverage: Hamburg: the port and the Elbe through the city (9.72 to 10.35 E, 53.39 to 53.75 N); not the Elbe mouth. Notes: 20 cm, spring flight 2026.

### Digitaal Vlaanderen orthophoto 2025 (Belgian coast, Flanders)
- GUID `{7FD25993-764A-4180-9A2B-B7DA68958C69}` (base chart)
- Service: WMTS (KVP GetTile) `https://geo.api.vlaanderen.be/OMW/wmts`, layer `omwrgb25vl`, empty style, matrix set `GoogleMapsVL` (ids `0`..`21`, the Web Mercator grid), PNG only, two connections. The path is upper case: the lower-case `/omw/` answers "layer not known".
- Terms: dataset record "Orthofotomozaïek, middenschalig, winteropnamen, kleur, 2025, Vlaanderen" (metadata.vlaanderen.be 088a9e65-83c7-4154-8078-ac2309bf41a2): "Geen beperkingen op de publieke toegang", "Modellicentie voor gratis hergebruik", "Bron: Luchtopnamen Digitaal Vlaanderen"; service: "De service kan door iedereen gebruikt worden." Read 2026-11-23.
- Attribution: Bron: Luchtopnamen Digitaal Vlaanderen — Orthofotomozaïek winteropnamen 2025, Modellicentie Gratis Hergebruik v1.0. Max zoom 19.
- Coverage: Flemish Region with the whole Belgian coast (2.53 to 5.92 E, 50.69 to 51.52 N). Notes: 15 cm winter flight 2025; PNG tiles of 100-200 KB; a new year is a new layer id.

### Maa- ja Ruumiamet orthophoto (Estonia)
- GUID `{255566CA-0E01-4439-B9CA-24592B36F8BC}` (base chart)
- Service: WMTS (REST) `https://tiles.maaamet.ee/tm/wmts/1.0.0/foto/default/GMC/{z}/{y}/{x}.jpg`, layer `foto`, matrix set `GMC` (EPSG:3857, ids `0`..`18`), JPEG, two connections
- Terms: "Maa- ja Ruumiameti avaandmete litsents, 01.01.2025": free use, derivatives and redistribution, commercial or not; credit the licensor, the dataset and the data age or extract date, and link the licence. Read 2026-11-23; the year in the attribution is the year this definition was written.
- Attribution: Ortofoto, Maa- ja Ruumiamet 2026; licence https://geoportaal.maaruum.ee/opendata-licence. Max zoom 18.
- Coverage: All Estonia with the islands ("Kogu Eestit kattev ajakohane ortofoto kiht"). Notes: the flight year varies by area.

### Göteborgs Stad orthophoto 2025 (Gothenburg)
- GUID `{5F1C343C-5AE6-4B14-9C56-285AA0542819}` (base chart)
- Service: WMS 1.3.0 `https://opengeodata.goteborg.se/services/ortofoto/wms/v1`, layer `orto_2025`, EPSG:3857, JPEG, two connections
- Terms: capabilities: `Fees` "Kostnadsfri", `AccessConstraints` "Inga begränsningar (CC0)". Read 2026-11-23.
- Attribution: Göteborgs Stad — Ortofoto 2025, CC0. Max zoom 18.
- Coverage: Gothenburg municipality (11.68 to 12.25 E, 57.53 to 57.87 N) with the southern archipelago; some outer skerries may be missing (the service says so). Notes: 25 cm, 2025.

### HSY orthophoto 2023 (Helsinki region)
- GUID `{360DC957-DFB1-421B-B01D-DC561EC7059A}` (base chart)
- Service: WMS 1.3.0 `https://kartta.hsy.fi/geoserver/taustakartat_ja_aluejaot/wms`, layer `Ortoilmakuva_2023`, EPSG:3857, JPEG, two connections
- Terms: HSY dataset "Helsingin seudun ortoilmakuvat" (hri.fi): CC BY 4.0; credit all procurement parties and the year: "Ortokuva (c) Alueen kunnat, HSY, HSL ja Puolustusvoimat 2023". Read 2026-11-23.
- Attribution: Ortokuva (c) Alueen kunnat, HSY, HSL ja Puolustusvoimat 2023, CC BY 4.0. Max zoom 18.
- Coverage: Helsinki, Espoo, Kauniainen, Vantaa, Kirkkonummi and inland towns (24.25 to 25.37 E, 59.90 to 60.61 N). Notes: open-data resolution 25 cm, flown June 2023.

### Turku region orthophoto 2023 (Turku, Naantali)
- GUID `{500EAC92-A190-489E-A856-69EFDC80E432}` (base chart)
- Service: WMS 1.3.0 `https://turku.asiointi.fi/TeklaOGCWeb/WMS.ashx`, layer `Ilmakuva 2023` (URL-encoded), EPSG:3857, PNG (the server answers PNG only), two connections
- Terms: avoindata.fi dataset "Turun seudun ilmakuva 2023" (City of Turku): CC BY 4.0; flown 7 August 2023, 15 cm. Read 2026-11-23. The 2025 layer of the same service is not used: no licence record was found for it.
- Attribution: © Turun kaupunki — Turun seudun ilmakuva 2023, CC BY 4.0. Max zoom 19.
- Coverage: Turku, Raisio, Naantali, Kaarina and neighbours; NOT the Parainen archipelago (white tiles at Nagu). Notes: PNG tiles of 50-120 KB.

### DGT OrtoSat satellite photo 2023 (mainland Portugal)
- GUID `{E70897C7-B9A6-4AA0-9E14-79102C5A66EA}` (base chart)
- Service: WMS 1.3.0 `https://ortos.dgterritorio.gov.pt/wms/ortosat2023`, layer `ortoSat2023-CorVerdadeira`, EPSG:3857, JPEG, two connections
- Terms: DGT "Dados abertos": "A informação geográfica descarregada do Centro de Dados está sujeita a uma licença de utilização CC-BY 4.0 … tendo apenas como obrigação a menção de que a entidade proprietária da informação é a Direção-Geral do Território"; the page lists "OrtoSat 30 cm - Portugal Continental – 2023". Read 2026-11-23.
- Attribution: © Direção-Geral do Território — OrtoSat2023, CC BY 4.0. Max zoom 19.
- Coverage: Mainland Portugal (-10.19 to -5.71 E, 36.76 to 42.28 N); not the Azores or Madeira. Notes: 30 cm satellite imagery, 2023.

### Regione Emilia-Romagna orthophoto 2023-24 (Rimini coast)
- GUID `{E2F995F8-02EB-4F3F-9AAA-7FFA0B7D1410}` (base chart)
- Service: WMS 1.3.0 `https://servizigis.regione.emilia-romagna.it/wms/rer2023_24_rgb`, layer `RER2023_24_RGB`, EPSG:3857, JPEG, two connections
- Terms: metadata "Ortofoto RER 2023-24 RGB (WMS)" (Regione Emilia-Romagna CSW, r_emiro:2024-02-14T095114): "CC BY 4.0 (Creative Commons - Attribuzione)", "Nessuna limitazione all'accesso pubblico". Read 2026-11-23.
- Attribution: Regione Emilia-Romagna — Ortofoto RER 2023–24, CC BY 4.0. Max zoom 19.
- Coverage: Parts of Emilia-Romagna flown in 2023-24, Rimini included; black where not flown (Porto Garibaldi is). Notes: 20 cm.

### Regione Emilia-Romagna orthophoto 2022-23 (Ferrara coast)
- GUID `{C29D7414-801D-40C9-8909-77B3A2CCAA84}` (base chart)
- Service: WMS 1.3.0 `https://servizigis.regione.emilia-romagna.it/wms/rer2022_rgb`, layer `RER2022_RGB`, EPSG:3857, JPEG, two connections
- Terms: metadata "Ortofoto RER 2022 RGB (WMS)" (r_emiro:2023-01-12T104521): "CC BY 4.0 (Creative Commons - Attribuzione)", "Nessuna limitazione all'accesso pubblico", classification "Non Classificato". Read 2026-11-23.
- Attribution: Regione Emilia-Romagna — Ortofoto RER 2022, CC BY 4.0. Max zoom 19.
- Coverage: The 2022 pilot areas, among them the Ferrara coast flown in January 2023 (Porto Garibaldi seen); black where not flown. Notes: 20 cm.

### Regione Abruzzo orthophoto 2018-19 (Abruzzo)
- GUID `{1378C96F-6D91-45F6-9A5F-6267C32CBE8E}` (base chart)
- Service: WMS 1.3.0 `http://geocatalogo.regione.abruzzo.it/erdas-iws/ogc/wms/` (plain HTTP; the server has no HTTPS), layer `Mosaici_UTM-WGS84_2018_2019_Regione_Abruzzo.ecw`, EPSG:3857, JPEG, two connections
- Terms: the region's "Licenza regionale": CC BY-NC 3.0 ("Non puoi utilizzare il materiale per scopi commerciali"). Read 2026-11-23. Non-commercial use only.
- Attribution: Regione Abruzzo — Ortofoto 2018–2019, CC BY-NC 3.0. Max zoom 19.
- Coverage: The whole region (12.97 to 14.79 E, 41.66 to 42.91 N), coast from Giulianova to San Salvo. Notes: 20 cm, flown 2018-2019.

### ASIG orthophoto 2015 (Albania)
- GUID `{B4088F26-9BD5-42DE-8F12-271097547A77}` (base chart)
- Service: WMS 1.3.0 `https://geoportal.asig.gov.al/service/orthophoto_2015/wms`, layer `OrthoImagery_20cm`, CRS `EPSG:900913` (the same Web Mercator; the service lists no EPSG:3857), JPEG, two connections
- Terms: ASIG terms of use: "The data is licensed under the Creative Commons Attribution 4.0 International (CC BY 4.0)". Read 2026-11-23.
- Attribution: ASIG — Ortofoto 2015, CC BY 4.0. Max zoom 19.
- Coverage: Albania (19.23 to 21.10 E, 39.63 to 42.73 N); the sea outside the mosaic is white. Notes: flown 2015; old imagery.

### Regione Siciliana orthophoto 2012-13 (Sicily and islands)
- GUID `{837FADA3-FFA3-4C60-B272-97A8D4E55AC0}` (base chart)
- Service: ArcGIS tile cache `https://map.sitr.regione.sicilia.it/gis/rest/services/ortofoto/ortofoto_2013_25cm_sicilia/MapServer/tile/{z}/{y}/{x}` (Web Mercator, levels to 19), JPEG or PNG (MIXED), two connections
- Terms: SITR "Licenza d'uso": portal data "non sottoposti a costi di cessione, sono rilasciati con licenza Attribuzione 4.0 Internazionale (CC BY 4.0)"; the service credit is "Regione Siciliana". Read 2026-11-23. Not the 2019 or 2022 layers of the same portal (AGEA imagery).
- Attribution: Regione Siciliana — S.I.T.R., Ortofoto ATA 2012–2013, CC BY 4.0. Max zoom 19.
- Coverage: Sicily and the minor islands (11.82 to 15.67 E, 35.47 to 38.82 N). Notes: 25 cm, regional flight A.T.A. 2012-2013; old imagery.

### Grey source: Esri World Imagery

This source carries a `termsOfUseWarning` key: the program prints a terms-of-use warning before downloading from it. The
provider's terms may not allow downloading tiles for offline use; reading the terms and complying is the user's responsibility.

- GUID `{7B743985-BC5F-4AB6-8915-AC5DBBB8F552}`
- Service: `https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}`
- Terms: https://www.arcgis.com/home/item.html?id=10df2279f9684e4a9f6a7f08febac2a9 - "This layer is not intended to be
  used to export tiles for offline. If you would like to export imagery for offline use in ArcGIS applications, you may use
  the World Imagery (for Export) layer"; Esri Master License Agreement.
- Attribution: Esri, Vantor, Earthstar Geographics, and the GIS User Community


## Coast and land

### IDEIB Posidonia and protected seabed (Balearics)
- GUID `{0C31D17C-5FAB-4A03-AC03-16BD45326219}` (overlay layer), `"navigationWarning": true`
- Service: WMS 1.3.0 `https://ideib.caib.es/geoserveis/services/public/GOIB_Posidonia_IB/MapServer/WMSServer`, layers `2,3,4` (Posidonia oceanica; Zones a Regular and Zones Alt Valor of Decret 25/2018), EPSG:3857, PNG, one connection
- Terms: capabilities `AccessConstraints` "Attribution-ShareAlike (by-sa)"; metadata `useLimitation` BY-SA. ShareAlike: a chart made from it is shared under the same licence.
- Attribution: © Govern de les Illes Balears — IDEIB, CC BY-SA. Informative, not for navigation. Max zoom 17.
- Coverage: Balearic Islands.

### EMODnet seagrass and Caribbean seabed
- GUID `{51385452-EFA1-4B30-8D8A-606F7F35DF54}` (overlay layer), `"navigationWarning": true`
- Service: WMS 1.3.0 `https://ows.emodnet-seabedhabitats.eu/geoserver/emodnet_view/wms`, layers `seagrass_eov_poly_2025,greek_seagrass_meadows_v0906,carib_eusm2025_subs_full`, EPSG:3857, PNG
- Terms: capabilities `Fees`/`AccessConstraints` "None"; ICES records: seagrass EOV 2025 European subset CC BY 4.0, Caribbean subset CC BY-NC 4.0; Greek seagrass and Caribbean substrate CC BY 4.0.
- Attribution: Licensed under CC-BY 4.0 from EMODnet Seabed Habitats; Caribbean seagrass subset CC BY-NC 4.0. Not for navigation. Max zoom 14.
- Coverage: European seas (seagrass), Greece, Caribbean (seagrass, substrate).

### EMODnet MPAs and Natura 2000 (European seas)
- GUID `{82A8F0F6-5881-4C5D-ACA5-8EB39DDD172F}` (overlay layer)
- Service: WMS 1.3.0 `https://ows.emodnet-humanactivities.eu/wms` (the host of the landed vessel-density sources), layers `marineprotectedareas,natura2000areas`, EPSG:3857, PNG
- Terms: capabilities `Fees`/`AccessConstraints` NONE; records "Creative Commons CC-BY 4.0", data originator Cogea Srl. The `wdpa*` layers of the same service are not used.
- Attribution: EMODnet Human Activities (data originator Cogea Srl), CC BY 4.0. Max zoom 12.
- Coverage: European seas.

### Queensland marine parks, moorings, reef markers
- GUID `{7ECFB249-0CBD-40FD-8E79-A9C6217BA0B6}` (overlay layer)
- Service: WMS 1.3.0 `https://spatial-gis.information.qld.gov.au/arcgis/services/Environment/ParksMarineProtectedAreas/MapServer/WMSServer`, layers `Great_Barrier_Reef_Coast_Marine_Park_zones30202,Moreton_Bay_Marine_Park_zones11630,Great_Sandy_Marine_Park_zones64240,QPWS_Reef_Protection_Markers__RPM_40119,QPWS_Moorings6676`, EPSG:3857, PNG, one connection
- Terms: capabilities `AccessConstraints` "This service is free to use and is licensed under a Creative Commons - Attribution 4.0 International licence.", `Fees` "Nil".
- Attribution: © State of Queensland (Department of Environment and Science), CC BY 4.0. Max zoom 16.
- Coverage: Queensland state waters.

### IGN Argentina protected areas
- GUID `{3654F1F2-468C-4A2D-84E7-83741E281B08}` (overlay layer)
- Service: WMS 1.3.0 `https://wms.ign.gob.ar/geoserver/ows`, layer `ign:area_protegida` (style `ign:polygon_ign`), EPSG:3857, transparent PNG, one connection
- Terms: WMS `AccessConstraints` (the Ley 27.275 clause, as for the IGN aerial photo); IGN terms as above.
- Attribution: FUENTE: Instituto Geográfico Nacional de la República Argentina — Área protegida (WMS, leído 2026-11-05). Max zoom 12.
- Coverage: national and provincial protected areas of Argentina, on land and at sea.

### NOAA NCCOS seabed habitat map (Saipan)
- GUID `{8098ECA8-1D3C-4448-8E5E-A77D18E1F5F9}` (overlay layer), `"navigationWarning": true`
- Service: ArcGIS MapServer export `https://gis.ngdc.noaa.gov/arcgis/rest/services/nccos/Saipan_ShallowWaterHabitat/MapServer/export` by tile box (`bbox={bbox}`, `bboxSR` and `imageSR` 3857, 256 × 256 transparent PNG), layer 37 "Benthic Habitat Map" only (the default layers draw nothing), one connection. The service has no tile cache and no WMS.
- Terms: service item information, `licenseInfo`: "These data and related items have no restriction on their use. However, these products are draft and have not been formally disseminated by NOAA. They do not represent any agency determination, view, or policy, and users assume all risk when using this data." (https://gis.ngdc.noaa.gov/arcgis/rest/services/nccos/Saipan_ShallowWaterHabitat/MapServer/info/iteminfo?f=pjson). Read 2026-11-06. The flag is set because the map is a draft.
- Attribution: NOAA National Centers for Coastal Ocean Science (NCCOS), Saipan shallow-water benthic habitat map (field data 2018-2022); no restriction on use; draft product, not for navigation. Max zoom 17.
- Coverage: Saipan's west lagoon and reefs and a patch west of them (145.59-145.80 E, 15.12-15.30 N). Colours (the service legend): pale yellow bare sand; olive sand with algae and seagrass; green and dark green seagrass on sand; orange coral reef (live and dead); blue pavement; brown coral rubble; red artificial. Notes: field data 2018-11 to 2022-04 (NCEI accession 0291792); 87 % overall accuracy (service text). The Guam service has no class map and is not used.

### Grey source: OpenTopoMap

This source carries a `termsOfUseWarning` key too (see above).

- GUID `{51C1079B-B55D-4DF7-A410-ADC118F53AB1}`
- Service: `https://a.tile.opentopomap.org/{z}/{x}/{y}.png` (one host, no subdomain rotation)
- Terms: https://opentopomap.org/about - "...sofern unser Server z.B. durch Massendownloads nicht zu stark beansprucht wird"
  (allowed as long as the server is not overloaded by mass downloads). Data CC-BY-SA.
- Attribution: Map data (c) OpenStreetMap contributors, SRTM; map style (c) OpenTopoMap (CC-BY-SA)


## Weather now

Snapshot sources: the program takes one frame ("now") and caches it as its own chart. Eighteen sources, current weather only.

### RainViewer radar
- GUID `{1A696D82-F258-41A4-BCD6-7648D9E08DE2}` (overlay layer), kind: snapshot (`json-last`, `radar.past`, field `path`)
- Service: `https://tilecache.rainviewer.com{ver}/256/{z}/{x}/{y}/2/1_1.png`; index: `https://api.rainviewer.com/public/weather-maps.json`
- Terms: https://www.rainviewer.com/api.html - "This API is available for personal and educational use only." and
  "You must display credit such as "Weather data by RainViewer" with a link back to rainviewer.com somewhere visible in
  your app or website. Attribution is mandatory under the free terms of use"; https://rainviewer.com/api/transition-faq.html -
  "Maximum zoom level set to 7", "Rate limiting set to 100 requests/IP/minute".
  api.html itself now says "There are no hard rate limits posted"; the FAQ's 100/min is kept as the safe reading.
- Attribution: Weather data by RainViewer (https://www.rainviewer.com). Max zoom 7.
- Rate: at most 85 requests per minute: one connection, 700 ms between requests; the service allows 100 per IP per minute.
- Use: personal and educational use only; radar only, no coverage over open sea.

### NASA VIIRS NOAA-20 true colour (GIBS)
- GUID `{8B9C244E-E978-4267-8FE4-D703534C0789}`, kind: snapshot (`wmts-domain`, daily)
- Service: `https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/VIIRS_NOAA20_CorrectedReflectance_TrueColor/default/{ver}/GoogleMapsCompatible_Level9/{z}/{y}/{x}.jpg` (row before column)
- Index: `https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/1.0.0/VIIRS_NOAA20_CorrectedReflectance_TrueColor/default/GoogleMapsCompatible_Level9/all/2026-01-01--2100-01-01.xml`
- Terms: https://nasa-gibs.github.io/gibs-api-docs/ - "NASA promotes full and open sharing of data"; acknowledgment asked
  for GIBS clients; https://www.earthdata.nasa.gov/engage/open-data-services-software-policies/data-use-guidance - "Unless
  the content is marked with a use restriction or license, data provided from a NASA-led mission are licensed as Creative
  Commons Zero (CC0)."
- Attribution: Imagery: NASA Global Imagery Browse Services (GIBS), part of the NASA Earth Science Data and Information System (ESDIS); VIIRS NOAA-20. Max zoom 9.

### NASA IMERG precipitation rate (GIBS)
- GUID `{92E40853-04A2-46B7-8A5B-3665FCDDFDBE}` (overlay layer), kind: snapshot (`wmts-domain`, 30 min)
- Service: `https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/IMERG_Precipitation_Rate_30min/default/{ver}/GoogleMapsCompatible_Level6/{z}/{y}/{x}.png`
- Index: `https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/1.0.0/IMERG_Precipitation_Rate_30min/default/GoogleMapsCompatible_Level6/all/2026-01-01--2100-01-01.xml`
- Terms: as VIIRS above (NASA ESDIS data use guidance, CC0 unless marked otherwise).
- Attribution: Precipitation: NASA GPM IMERG via NASA Global Imagery Browse Services (GIBS), part of the NASA Earth Science Data and Information System (ESDIS). Max zoom 6.

### EUMETSAT MTG GeoColour (EUMETView)
- GUID `{FE84A175-2C0F-420F-86DF-5900DE16A33C}` (base map, JPEG), kind: snapshot (`wms-default`, 10 min)
- Service: `https://view.eumetsat.int/geoserver/wms`, layer `mtg_fd:rgb_geocolour`, WMS 1.3.0, EPSG:3857, `TIME={ver}`
- Index: `https://view.eumetsat.int/geoserver/mtg_fd/rgb_geocolour/ows?service=WMS&version=1.3.0&request=GetCapabilities`
- Terms: EUMETSAT Data Policy (https://www-cdn.eumetsat.int/files/2025-02/45173%20-%20Data_Policy(1442019%20V1).pdf) -
  "Access to Core Data and Products is granted to all users world-wide on a Free and Unrestricted basis under a CC-BY-4.0
  licence" (Article 5.1); attribution "[Contains modified] EUMETSAT [Meteosat/Metop] [data/product] [Year of publication
  or distribution]" (Article 6.3).
- Attribution: Contains modified EUMETSAT Meteosat data {year}. Max zoom 8.
- Coverage: the Meteosat 0 degree disc (Europe, Africa, Atlantic); clouds day and night.

### EUMETSAT H SAF precipitation rate H60B (EUMETView)
- GUID `{BC2EA862-0F3D-4933-8F71-00AFA0BAD114}` (overlay layer), kind: snapshot (`wms-default`, 15 min)
- Service: `https://view.eumetsat.int/geoserver/wms`, layer `msg_fes:h60b`, WMS 1.3.0, EPSG:3857, PNG, `TIME={ver}`
- Index: `https://view.eumetsat.int/geoserver/msg_fes/h60b/ows?service=WMS&version=1.3.0&request=GetCapabilities`
- Terms: as GeoColour above; "All SAF products are categorised as "Core" products."
- Attribution: Contains modified EUMETSAT H SAF product {year}. Max zoom 7.
- Coverage: Europe, Africa, Atlantic.

### DWD weather radar (Germany)
- GUID `{25E4232E-08A5-47C5-8D97-E38B5F418A2D}` (overlay layer), kind: snapshot (`wms-default`, 5 min)
- Service: `https://maps.dwd.de/geoserver/dwd/wms`, layer `dwd:Radar_wn-analysis_1x1km_ger`, WMS 1.3.0, EPSG:3857, PNG, `TIME={ver}`
- Index: `https://maps.dwd.de/geoserver/dwd/Radar_wn-analysis_1x1km_ger/ows?service=WMS&version=1.3.0&request=GetCapabilities`
- Terms: https://www.dwd.de/copyright - "Alle frei zugänglichen Geodaten und Geodatendienste ... dürfen unter den
  Bedingungen der Lizenz Creative Commons BY 4.0 ( CC BY 4.0 ) unter Beigabe eines Quellenvermerks weiterverwendet werden."
- Attribution: Source: Deutscher Wetterdienst (DWD), CC BY 4.0. Max zoom 9.
- Coverage: Germany and the North Sea / western Baltic coasts.

### NOAA nowCOAST weather radar (contiguous US)
- GUID `{076D1F9A-DC94-4184-B785-77362CD01B5B}` (overlay layer), kind: snapshot (`wms-default`, 2-4 min)
- Service: `https://nowcoast.noaa.gov/geoserver/weather_radar/wms`, layer `conus_base_reflectivity_mosaic`, WMS 1.3.0, EPSG:3857, PNG, `TIME={ver}`
- Index: `https://nowcoast.noaa.gov/geoserver/weather_radar/conus_base_reflectivity_mosaic/ows?service=WMS&version=1.3.0&request=GetCapabilities`
- Terms: the capabilities state `Fees` "NONE" and `AccessConstraints` "NONE"; https://www.weather.gov/disclaimer -
  "The information on National Weather Service (NWS) Web pages are in the public domain, unless specifically noted otherwise".
- Attribution: NOAA nowCOAST, MRMS weather radar (US government, public domain). Max zoom 9.
- Coverage: US waters only (contiguous US; Alaska, Hawaii, Caribbean and Guam are not shipped).

### NOAA nowCOAST clouds, infrared (worldwide)
- GUID `{925EFE79-929A-40E4-BDF0-E38E95FAB64E}` (overlay layer), kind: snapshot (`wms-default`, hourly)
- Service: `https://nowcoast.noaa.gov/geoserver/satellite/wms`, layer `global_longwave_imagery_mosaic`, WMS 1.3.0, EPSG:3857, PNG, `TIME={ver}`
- Index: `https://nowcoast.noaa.gov/geoserver/satellite/global_longwave_imagery_mosaic/ows?service=WMS&version=1.3.0&request=GetCapabilities`
- Terms: the capabilities state `Fees` "NONE" and `AccessConstraints` "NONE"; NOAA data, public domain (https://www.weather.gov/disclaimer).
- Attribution: NOAA NESDIS GMGSI global longwave infrared mosaic via nowCOAST (US government, public domain). Max zoom 7 (about 3 km).
- Coverage: worldwide, geostationary mosaic (about 60 N to 60 S plus the polar gaps of the mosaic). The frame is two to three hours old.

### NOAA nowCOAST active tropical cyclones
- GUID `{91F4EDA9-11F5-44AE-84AB-BBD7FEF492AF}` (overlay layer), kind: snapshot (`none-refetch`)
- Service: `https://nowcoast.noaa.gov/geoserver/tropical_cyclones/wms`, layer `active_tropical_cyclones`, WMS 1.3.0, EPSG:3857, PNG, no `TIME` (the capabilities have no time dimension)
- Index: `https://nowcoast.noaa.gov/geoserver/tropical_cyclones/wms` (the service itself; every run fetches the area again)
- Terms: the capabilities state `Fees` "NONE" and `AccessConstraints` "NONE"; NOAA data, public domain (https://www.weather.gov/disclaimer).
- Attribution: NOAA nowCOAST, active tropical cyclones (US government, public domain). Max zoom 7.
- Coverage: the layer group draws cone, wind extent and radii, track, centre and watches/warnings of the storms active now.

### NOAA nowCOAST NWS watches and warnings (US)
- GUID `{901E0ED9-78CD-41C4-BA6A-0E93CF08C4F0}` (overlay layer), kind: snapshot (`none-refetch`)
- Service: `https://nowcoast.noaa.gov/geoserver/alerts/wms`, layer `watches_warnings_advisories`, WMS 1.3.0, EPSG:3857, PNG, no `TIME`
- Index: `https://nowcoast.noaa.gov/geoserver/alerts/wms` (the service itself; every run fetches the area again)
- Terms: the capabilities state `Fees` "NONE" and `AccessConstraints` "NONE"; NWS data, public domain (https://www.weather.gov/disclaimer).
- Attribution: NOAA National Weather Service watches, warnings and advisories via nowCOAST (US government, public domain). Max zoom 8.
- Coverage: US land and coastal marine zones.

### NASA GOES-East GeoColor (Americas, Atlantic)
- GUID `{30D88DB0-B695-412B-91ED-1656B598D49E}` (base chart), kind: snapshot (`wmts-domain`)
- Service: WMTS REST `https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/GOES-East_ABI_GeoColor/default/{ver}/GoogleMapsCompatible_Level7/{z}/{y}/{x}.png`; index `…/1.0.0/GOES-East_ABI_GeoColor/default/GoogleMapsCompatible_Level7/all/2026-11-01--2100-01-01.xml` (`wmts-domain`, PT10M; the start 2026-11-01 keeps the domain document small: from 2026-01-01 it is already 362 KB and grows with every data gap)
- Terms: Earthdata: NASA data CC0, non-NASA data under the sponsor's terms; GOES is NOAA data, NWS disclaimer "public domain … may be used without charge for any lawful purpose"; GIBS asks for the acknowledgement. The Himawari layer is not shipped (see "Not shipped").
- Attribution: Imagery: NASA GIBS (ESDIS); GOES ABI — NOAA. Max zoom 7.
- Coverage: the GOES East disk, about 60 N to 60 S; the newest frame lags ~50 min.

### NASA GOES-West GeoColor (Pacific, Americas)
- GUID `{A7EA159A-1A46-4B2D-95FD-5408E996A36E}` (base chart), kind: snapshot (`wmts-domain`)
- Service: WMTS REST `https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/GOES-West_ABI_GeoColor/default/{ver}/GoogleMapsCompatible_Level7/{z}/{y}/{x}.png`; index `…/1.0.0/GOES-West_ABI_GeoColor/default/GoogleMapsCompatible_Level7/all/2026-11-01--2100-01-01.xml` (`wmts-domain`, PT10M; the start 2026-11-01 keeps the domain document small: from 2026-01-01 it is already 362 KB and grows with every data gap)
- Terms: Earthdata: NASA data CC0, non-NASA data under the sponsor's terms; GOES is NOAA data, NWS disclaimer "public domain … may be used without charge for any lawful purpose"; GIBS asks for the acknowledgement. The Himawari layer is not shipped (see "Not shipped").
- Attribution: Imagery: NASA GIBS (ESDIS); GOES ABI — NOAA. Max zoom 7.
- Coverage: the GOES West disk, about 60 N to 60 S; the newest frame lags ~50 min.

### NASA GOES-East infrared (Americas, Atlantic)
- GUID `{957861F8-FA84-4192-8DD4-5C1E4E2BC399}` (base chart), kind: snapshot (`wmts-domain`)
- Service: WMTS REST `https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/GOES-East_ABI_Band13_Clean_Infrared/default/{ver}/GoogleMapsCompatible_Level6/{z}/{y}/{x}.png`; index `…/1.0.0/GOES-East_ABI_Band13_Clean_Infrared/default/GoogleMapsCompatible_Level6/all/2026-11-01--2100-01-01.xml` (`wmts-domain`, PT10M; the start 2026-11-01 keeps the domain document small: from 2026-01-01 it is already 362 KB and grows with every data gap)
- Terms: Earthdata: NASA data CC0, non-NASA data under the sponsor's terms; GOES is NOAA data, NWS disclaimer "public domain … may be used without charge for any lawful purpose"; GIBS asks for the acknowledgement. The Himawari layer is not shipped (see "Not shipped").
- Attribution: Imagery: NASA GIBS (ESDIS); GOES ABI — NOAA. Max zoom 6.
- Coverage: the GOES East disk, about 60 N to 60 S; the newest frame lags ~50 min.

### NASA GOES-West infrared (Pacific, Americas)
- GUID `{AE5863F3-1D34-4B66-8BD4-41630DF8B940}` (base chart), kind: snapshot (`wmts-domain`)
- Service: WMTS REST `https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/GOES-West_ABI_Band13_Clean_Infrared/default/{ver}/GoogleMapsCompatible_Level6/{z}/{y}/{x}.png`; index `…/1.0.0/GOES-West_ABI_Band13_Clean_Infrared/default/GoogleMapsCompatible_Level6/all/2026-11-01--2100-01-01.xml` (`wmts-domain`, PT10M; the start 2026-11-01 keeps the domain document small: from 2026-01-01 it is already 362 KB and grows with every data gap)
- Terms: Earthdata: NASA data CC0, non-NASA data under the sponsor's terms; GOES is NOAA data, NWS disclaimer "public domain … may be used without charge for any lawful purpose"; GIBS asks for the acknowledgement. The Himawari layer is not shipped (see "Not shipped").
- Attribution: Imagery: NASA GIBS (ESDIS); GOES ABI — NOAA. Max zoom 6.
- Coverage: the GOES West disk, about 60 N to 60 S; the newest frame lags ~50 min.

### NOAA STOFS-3D surge water level (US coasts)
- GUID `{984FD6D2-DB46-41AC-8E54-4F39E6A1239D}` (overlay layer), kind: snapshot (`wms-default`, `nearest-now`), `"navigationWarning": true`
- Service: WMS 1.3.0 `https://nowcoast.noaa.gov/geoserver/stofs3d/wms`, layers `stofs3d_atlc_water_disturbance,stofs3d_pac_water_disturbance` in one request, EPSG:3857, PNG, `TIME={ver}`; index `…/stofs3d/stofs3d_atlc_water_disturbance/ows?…GetCapabilities` (`wms-default`, `nearest-now`; hourly, default = last forecast hour)
- Terms: capabilities `Fees`/`AccessConstraints` NONE; NWS disclaimer "public domain … may be used without charge for any lawful purpose".
- Attribution: NOAA NOS STOFS via nowCOAST (public domain). Guidance only, not for navigation. Max zoom 10.

### NOAA STOFS-2D peak water level (global)
- GUID `{83FB6266-385F-46A4-BB45-0D2D3BEABE9A}` (overlay layer), kind: snapshot (`none-refetch`), `"navigationWarning": true`
- Service: WMS 1.3.0 `https://nowcoast.noaa.gov/geoserver/stofs2d/wms`, layer `stofs2d_cwl_max_contours`, EPSG:3857, PNG; no time dimension: `none-refetch` (fresh tiles every download)
- Terms: as STOFS-3D.
- Attribution: NOAA NOS STOFS via nowCOAST (public domain). Guidance only, not for navigation. Max zoom 10.

### FMI weather radar (Finland)
- GUID `{336DC3A7-381C-4D77-BA3A-E063C5392F3F}` (overlay layer), kind: snapshot (`wms-default`, `nearest-now`)
- Service: WMS 1.3.0 `https://openwms.fmi.fi/geoserver/wms`, layer `Radar:suomi_dbz_eureffin`, EPSG:3857 (inherited from the root layer), PNG, `TIME={ver}`; index `https://openwms.fmi.fi/geoserver/Radar/suomi_dbz_eureffin/ows?…GetCapabilities` (`wms-default`, `nearest-now`: the `time` default is the word `current`, the dimension `…/PT5M`)
- Terms: FMI open data licence "Creative Commons Attribution 4.0" (https://en.ilmatieteenlaitos.fi/open-data-licence); capabilities `Fees`/`AccessConstraints` NONE.
- Attribution: Source: Finnish Meteorological Institute, CC BY 4.0. Max zoom 9.

### KNMI weather radar (Netherlands)
- GUID `{C167DA1F-58E9-4261-809F-3B6D16AD56AA}` (overlay layer), kind: snapshot (`wms-default`)
- Service: WMS 1.3.0 `https://geoservices.knmi.nl/adaguc-server?dataset=RADAR`, layer `RAD_NL25_PCP_CM`, EPSG:3857, PNG, `TIME={ver}`; index the same URL with `&service=WMS&version=1.3.0&request=GetCapabilities` (`wms-default`; default = latest 5-minute frame)
- Terms: KNMI Data Platform "Creative Commons Attribution 4.0", "No use limitations"; capabilities `Fees` "no conditions apply", `AccessConstraints` "None".
- Attribution: Source: KNMI, CC BY 4.0. Max zoom 9.


## Ocean

Eight snapshot sources: two daily NASA GIBS products (`wmts-domain`), six model pictures (two NOAA RTOFS, the NOAA OFS currents and three Copernicus Marine fields; `wms-default` or `wmts-default`). The six SHOM layers after them are static (no snapshot).

### NASA sea surface temperature (GIBS, GHRSST MUR)
- GUID `{AF87E1F3-ECD7-431F-81ED-E0C7E95458D0}` (overlay layer), kind: snapshot (`wmts-domain`, daily)
- Service: `https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/GHRSST_L4_MUR_Sea_Surface_Temperature/default/{ver}/GoogleMapsCompatible_Level7/{z}/{y}/{x}.png`
- Index: `https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/1.0.0/GHRSST_L4_MUR_Sea_Surface_Temperature/default/GoogleMapsCompatible_Level7/all/2026-01-01--2100-01-01.xml`
- Terms: https://www.earthdata.nasa.gov/engage/open-data-services-software-policies/data-use-guidance - "Unless the content is
  marked with a use restriction or license, data provided from a NASA-led mission are licensed as Creative Commons Zero (CC0)."
- Attribution: Imagery: NASA Global Imagery Browse Services (GIBS), GHRSST MUR L4 sea surface temperature (JPL PO.DAAC). Max zoom 7.

### NASA sea ice concentration (GIBS, GHRSST MUR)
- GUID `{544D656E-F024-4E79-ADC9-4981D7AE601A}` (overlay layer), kind: snapshot (`wmts-domain`, daily)
- Service: `https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/GHRSST_L4_MUR_Sea_Ice_Concentration/default/{ver}/GoogleMapsCompatible_Level7/{z}/{y}/{x}.png`
- Index: `https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/1.0.0/GHRSST_L4_MUR_Sea_Ice_Concentration/default/GoogleMapsCompatible_Level7/all/2026-01-01--2100-01-01.xml`
- Terms: as sea surface temperature above (NASA data use guidance, CC0 unless marked otherwise).
- Attribution: Imagery: NASA Global Imagery Browse Services (GIBS), GHRSST MUR L4 sea ice concentration (JPL PO.DAAC). Max zoom 7.

### NOAA RTOFS surface currents (western Atlantic)
- GUID `{F022DF76-6F80-4294-9403-F1A6123EDD80}` (overlay layer), kind: snapshot (`wms-default`, `nearest-now`, 6-hourly)
- Service: `https://nowcoast.noaa.gov/geoserver/grtofs/wms`, layer `rtofs_east_sfc_currents`, WMS 1.3.0, EPSG:3857, PNG, `TIME={ver}`
- Index: `https://nowcoast.noaa.gov/geoserver/grtofs/rtofs_east_sfc_currents/ows?service=WMS&version=1.3.0&request=GetCapabilities`
- Terms: the capabilities state `Fees` "NONE" and `AccessConstraints` "NONE"; NOAA data, public domain (https://www.weather.gov/disclaimer).
- Attribution: NOAA RTOFS ocean model surface currents via nowCOAST (NOS and NWS/NCEP; US government, public domain). Max zoom 8.
- Coverage: US East coast waters and the western Atlantic, 99.8 W to 40.6 W, 0.1 N to 44.9 N (capabilities box).

### NOAA RTOFS surface currents (eastern Pacific)
- GUID `{D228EF7F-C01B-4A00-AD22-2C7B527A2CA5}` (overlay layer), kind: snapshot (`wms-default`, `nearest-now`, 6-hourly)
- Service: `https://nowcoast.noaa.gov/geoserver/grtofs/wms`, layer `rtofs_west_sfc_currents`, WMS 1.3.0, EPSG:3857, PNG, `TIME={ver}`
- Index: `https://nowcoast.noaa.gov/geoserver/grtofs/rtofs_west_sfc_currents/ows?service=WMS&version=1.3.0&request=GetCapabilities`
- Terms: the capabilities state `Fees` "NONE" and `AccessConstraints` "NONE"; NOAA data, public domain (https://www.weather.gov/disclaimer).
- Attribution: NOAA RTOFS ocean model surface currents via nowCOAST (NOS and NWS/NCEP; US government, public domain). Max zoom 8.
- Coverage: US West coast waters and the eastern Pacific, 179.9 W to 101.8 W, 14.4 N to 62.4 N (capabilities box).

### NOAA OFS surface currents (US bays and coasts)
- GUID `{B7B26ADE-22F8-4CD3-9389-3B289BB04D6F}` (overlay layer), kind: snapshot (`wms-default`, `nearest-now`), `"navigationWarning": true`
- Service: WMS 1.3.0 `https://nowcoast.noaa.gov/geoserver/ows`, layers `cbofs:cbofs_sfc_currents,dbofs:dbofs_sfc_currents,gomofs:gomofs_sfc_currents,ngofs:ngofs_sfc_currents,tbofs:tbofs_sfc_currents,sfbofs:sfbofs_sfc_currents,wcofs:wcofs_sfc_currents` in one request, EPSG:3857, PNG, `TIME={ver}`; index `…/cbofs/cbofs_sfc_currents/ows?…GetCapabilities` (`wms-default`, `nearest-now`, hourly)
- Terms: capabilities `Fees`/`AccessConstraints` NONE; NWS disclaimer, public domain.
- Attribution: NOAA NOS CO-OPS OFS via nowCOAST (public domain). Not for navigation. Max zoom 12.

### Copernicus currents with tide (global)
- GUID `{07ECC645-967B-4C6D-9AE7-E5E7DA075AAA}` (overlay layer), kind: snapshot (`wmts-default`)
- Service: WMTS `https://wmts.marine.copernicus.eu/teroWmts` (key-value GetTile), layer `GLOBAL_ANALYSISFORECAST_PHY_001_024/cmems_mod_glo_phy_anfc_merged-uv_PT1H-i_202211/sea_water_velocity`, style `cmap:speed,vectorStyle:solidAndVector`, matrix set `EPSG:3857` (ids `0`..`10`, XYZ grid), PNG, `TIME={ver}`; index = the dataset's capabilities (`wmts-default`; the default time is the hour nearest now; `--time` can pick a forecast frame)
- Terms: Copernicus Marine licence 2.1 "granted free of charge", 2.2(c) redistribution in original form, 2.4(b) credit "E.U. Copernicus Marine Service Information" (https://marine.copernicus.eu/user-corner/service-commitments-and-licence). GetTile is anonymous (fetch test 2026-11-05).
- Attribution: Generated using E.U. Copernicus Marine Service Information; GLOBAL_ANALYSISFORECAST_PHY_001_024. Max zoom 7.

### Copernicus currents (North-West European shelf)
- GUID `{A3A40A49-FF43-4FA5-A74C-036454BE8DEF}` (overlay layer), kind: snapshot (`wmts-default`)
- Service: WMTS `https://wmts.marine.copernicus.eu/teroWmts` (key-value GetTile), layer `NWSHELF_ANALYSISFORECAST_PHY_004_013/cmems_mod_nws_phy-cur_anfc_1.5km-2D_PT1H-i_202511/sea_water_velocity`, style `cmap:speed,vectorStyle:solidAndVector`, matrix set `EPSG:3857` (ids `0`..`10`, XYZ grid), PNG, `TIME={ver}`; index = the dataset's capabilities (`wmts-default`; the default time is the hour nearest now; `--time` can pick a forecast frame)
- Terms: Copernicus Marine licence 2.1 "granted free of charge", 2.2(c) redistribution in original form, 2.4(b) credit "E.U. Copernicus Marine Service Information" (https://marine.copernicus.eu/user-corner/service-commitments-and-licence). GetTile is anonymous (fetch test 2026-11-05).
- Attribution: Generated using E.U. Copernicus Marine Service Information; NWSHELF_ANALYSISFORECAST_PHY_004_013. Max zoom 9.

### Copernicus significant wave height (global)
- GUID `{07CA509E-7F76-436A-98EF-4248B72A5DE8}` (overlay layer), kind: snapshot (`wmts-default`)
- Service: WMTS `https://wmts.marine.copernicus.eu/teroWmts` (key-value GetTile), layer `GLOBAL_ANALYSISFORECAST_WAV_001_027/cmems_mod_glo_wav_anfc_0.083deg_PT3H-i_202411/VHM0`, style `cmap:amp`, matrix set `EPSG:3857` (ids `0`..`10`, XYZ grid), PNG, `TIME={ver}`; index = the dataset's capabilities (`wmts-default`; the default time is the hour nearest now; `--time` can pick a forecast frame)
- Terms: Copernicus Marine licence 2.1 "granted free of charge", 2.2(c) redistribution in original form, 2.4(b) credit "E.U. Copernicus Marine Service Information" (https://marine.copernicus.eu/user-corner/service-commitments-and-licence). GetTile is anonymous (fetch test 2026-11-05).
- Attribution: Generated using E.U. Copernicus Marine Service Information; GLOBAL_ANALYSISFORECAST_WAV_001_027. Max zoom 7.

### SHOM tidal range, spring tides (France)
- GUID `{D88BE862-97D3-4062-9332-470B2BB5B5BE}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://services.data.shom.fr/INSPIRE/wmts`, layer `MARNAGE_VE_3857_WMTS`, style `normal`, matrix set `3857` (ids `0`..`21`, XYZ grid), PNG
- Terms: datasets under Licence Ouverte 2.0; the `/INSPIRE/` capabilities say `Fees` "Soumis à abonnement" and the WMS/r "sauf pour les thématiques INSPIRE qui sont en accès libre"; the data.shom.fr CGU was not found.
- Attribution: © Shom — Licence Ouverte 2.0. Non certifié pour la navigation. Max zoom 13.
- Coverage: French coasts.

### SHOM tidal range, neap tides (France)
- GUID `{B2E953DA-3B93-4651-88F1-DD4EAFC8CD5A}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://services.data.shom.fr/INSPIRE/wmts`, layer `MARNAGE_ME_3857_WMTS`, style `normal`, matrix set `3857` (ids `0`..`21`, XYZ grid), PNG
- Terms: datasets under Licence Ouverte 2.0; the `/INSPIRE/` capabilities say `Fees` "Soumis à abonnement" and the WMS/r "sauf pour les thématiques INSPIRE qui sont en accès libre"; the data.shom.fr CGU was not found.
- Attribution: © Shom — Licence Ouverte 2.0. Non certifié pour la navigation. Max zoom 13.
- Coverage: French coasts.

### SHOM tidal stream, maximum speed (French coasts)
- GUID `{6CEDB50E-F401-4653-A69C-D9C402886D0B}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://services.data.shom.fr/INSPIRE/wmts`, layer `COURANTS2D_WMTS_VMAX_3857`, style `normal`, matrix set `3857` (ids `0`..`21`, XYZ grid), PNG
- Terms: datasets under Licence Ouverte 2.0; the `/INSPIRE/` capabilities say `Fees` "Soumis à abonnement" and the WMS/r "sauf pour les thématiques INSPIRE qui sont en accès libre"; the data.shom.fr CGU was not found.
- Attribution: © Shom — Licence Ouverte 2.0. Non certifié pour la navigation. Max zoom 13.
- Coverage: French coasts; levels 4-13.

### SHOM surface currents, 3D model (Channel)
- GUID `{9C9D5257-EC7A-4B63-B163-2537CBA2AA07}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://services.data.shom.fr/INSPIRE/wmts`, layer `COURANTS_3D_MANCHE_SURF_WMTS_3857`, style `normal`, matrix set `3857` (ids `0`..`21`, XYZ grid), PNG
- Terms: datasets under Licence Ouverte 2.0; the `/INSPIRE/` capabilities say `Fees` "Soumis à abonnement" and the WMS/r "sauf pour les thématiques INSPIRE qui sont en accès libre"; the data.shom.fr CGU was not found.
- Attribution: © Shom — Licence Ouverte 2.0. Non certifié pour la navigation. Max zoom 14.
- Coverage: English Channel (French model).

### SHOM surface currents, 3D model (Fromveur, Ushant)
- GUID `{7F6A0B57-C94F-4B1D-AA06-C8A2A797BF78}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://services.data.shom.fr/INSPIRE/wmts`, layer `COURANTS_3D_FROMVEUR_SURF_WMTS_3857`, style `normal`, matrix set `3857` (ids `0`..`21`, XYZ grid), PNG
- Terms: datasets under Licence Ouverte 2.0; the `/INSPIRE/` capabilities say `Fees` "Soumis à abonnement" and the WMS/r "sauf pour les thématiques INSPIRE qui sont en accès libre"; the data.shom.fr CGU was not found.
- Attribution: © Shom — Licence Ouverte 2.0. Non certifié pour la navigation. Max zoom 14.
- Coverage: Fromveur passage and Ushant.

### SHOM tide zones (France)
- GUID `{FA4270B3-BFD8-483A-8ED7-6D3C6DEE1080}` (overlay layer), `"navigationWarning": true`
- Service: WMTS `https://services.data.shom.fr/INSPIRE/wmts`, layer `ZONES_MAREE_PYR_PNG_3857_WMTS`, style `normal`, matrix set `3857` (ids `0`..`21`, XYZ grid), PNG
- Terms: datasets under Licence Ouverte 2.0; the `/INSPIRE/` capabilities say `Fees` "Soumis à abonnement" and the WMS/r "sauf pour les thématiques INSPIRE qui sont en accès libre"; the data.shom.fr CGU was not found.
- Attribution: © Shom — Licence Ouverte 2.0. Non certifié pour la navigation. Max zoom 10.
- Coverage: French coasts; thin lines, levels 0-10.


## Not shipped, with the reason

Checked on 2026-10-22 unless a date says otherwise. Sources that need a key or whose terms are unknown live in a folder
OUTSIDE the repository (see README.md, "Chart sources").

- Google (all), Bing (all), Apple: the terms forbid pre-fetching, storing or bulk download (Google Maps Platform terms; Bing direct tile access "not allowed"; Apple's terms).
- OpenStreetMap standard tiles: "Offline use is not permitted" (https://operations.osmfoundation.org/policies/tiles/).
- Esri family beyond World Imagery (Ocean Base, Clarity, Topo, Hybrid): the item licence says "not intended to be used to export tiles for offline".
- Keyed sources: Lantmateriet topographic web chart (Sweden, free key; terms not retrieved).
- Anygis.ru proxy entries: a third-party proxy that rehosts other providers' tiles.
- EOX Sentinel-2 Cloudless 2016: not needed (2025 is bundled).
- New Caledonia Géorep imagery (2026-11-05): no Web Mercator; the WMS offers only CRS:84, EPSG:4326 and EPSG:3163 (read that day).
- Queensland imagery, Latest State Program (2026-11-05): CC BY-SA, but the Planet fill is "all rights reserved"; a cache cannot leave it out.
- NSW Imagery (2026-11-05): licence version unclear on the provider pages (CC BY, BY-ND or not specified); `exportTilesAllowed` false.
- LINZ chart images (2026-11-05): needs a per-user LDS key.
- Allen Coral Atlas (2026-11-05): no tile service.
- NASA GIBS Himawari layer (2026-11-05): JMA terms for Himawari data not found on a JMA page; a mirror says commercial use "not permitted in principle".
- Caribbean Netherlands orthophotos 2023-24 (Bonaire, Saba, St. Eustatius; Beeldmateriaal, CC BY 4.0) (2026-11-05): hosted on Ellipsis Drive, whose terms are an agreement for account holders with no permission to download tiles for offline use; they say content "accessible through the Services … [is] protected by … intellectual property laws".
