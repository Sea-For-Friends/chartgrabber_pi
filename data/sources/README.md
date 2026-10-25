# Shipped chart-source definitions

Twenty-eight JSON source files (`<group>/<name>.source.json`, format 1 of the CLI's sources guide), written by us from each service's public documentation. Nothing is
copied from the SAS.Planet map catalogue; the GUID and `NameInCache` are reused only for the three services the
catalogue also has (OpenSeaMap seamarks, Esri World Imagery, OpenTopoMap), so an existing tile cache stays valid.
The folder name is the menu group (the `group` value of the definitions); seven groups, below. The definition
files are CC0-1.0 (see `LICENSE`). Terms of the first sixteen were checked on 2026-10-15, those of the eight added on
2026-10-22, those of the four weather sources added on 2026-10-23. Every source of the first 24 was downloaded once with the CLI 2026.10.8 on 2026-10-22 (one connection, a few tiles,
one or two zoom levels); the four added later were downloaded with the CLI 2026.10.9 on 2026-10-23. The folder `data/sources` is passed to the CLI first; a sailor's own
folder (Preferences) is passed after it.

The two groups `Nautical charts` and `Depth` carry `"navigationWarning": true`: they are tile pictures, not charts to navigate
by. Kartverket and Traficom say "not for navigation" themselves.

## Nautical charts

### Norway sea chart (Kartverket)
- GUID `{25DBA489-D484-4A13-B65D-ABA8967FF3DA}` (base chart), `"navigationWarning": true`
- Service: `https://cache.kartverket.no/v1/wmts/1.0.0/sjokartraster/default/webmercator/{z}/{y}/{x}.png` (WMTS, matrix set `webmercator`, levels 0-18)
- Terms: capabilities (https://cache.kartverket.no/v1/wmts/1.0.0/WMTSCapabilities.xml) `<ows:Fees>Ingen`, `<ows:AccessConstraints>Ingen`;
  CC BY 4.0 (https://data.norge.no/en/data-services/e3454491-066a-3276-a482-f63069728021, https://proxy.kartverket.no/en/api-and-data/terms-of-use);
  the service is marked "Ikke til navigasjon".
- Attribution: (c) Kartverket, CC BY 4.0 (not for navigation). Max zoom 18 (the last level of the capabilities).
- Coverage: the Norwegian coast. Kartverket ends the raster Kystkart download on 2026-08-28; tiles remain.

### Finland sea chart (Traficom)
- GUID `{8A9C3A53-0512-4401-A6D3-050E08934949}` (base chart), `"navigationWarning": true`
- Service: WMTS `https://julkinen.traficom.fi/rasteripalvelu/wmts`, layer `Traficom:Merikarttasarjat public`, matrix set `WGS84_Pseudo-Mercator`, matrix id `WGS84_Pseudo-Mercator:{z}` (key-value request, `TILEROW={y}&TILECOL={x}`, levels 5-15 for this layer)
- Terms: capabilities of the WMS (https://julkinen.traficom.fi/rasteripalvelu/wms?SERVICE=WMS&VERSION=1.3.0&REQUEST=GetCapabilities, 2026-10-22):
  `<Fees>NONE`; `AccessConstraints`: "This dataset produced by the Finnish Transport Agency is licensed under a Creative Commons
  Attribution 4.0 International License - http://creativecommons.org/licenses/by/4.0/ "Source: Finnish Transport Agency.
  Not for navigational use. Does not meet the requirements for official nautical charts.""
  (https://traficom.fi/en/hydrography/finnish-nautical-charts/distribution-channels-nautical-charts-and-hydrographic-data).
- Attribution: Source: Traficom, CC BY 4.0 (not for navigational use). Max zoom 15.
- Notes: ONE source for the whole coast. The preferred form, one WMS request drawing four layers (`Yleiskartat 250k public`, `...100k`,
  `Rannikkokartat public`, `Merikarttasarjat public`), does not work: the server is GeoWebCache, which answers HTTP 400 "Unknown
  layer" for a list of layers and only takes WMS 1.1.1 (`SRS=`, not `CRS=`). The WMTS layer `Merikarttasarjat public` (the
  chart series) is used; on 2026-10-22 its tile of z11 over Helsinki was byte-identical in size to the WMS 1.1.1 answer (13788 bytes).
  The coarser general-chart layers are not drawn, so offshore Finnish waters at low zoom may show less than the WMS fallback would.

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
  `AccessConstraints` are empty. Not re-read by beyond the capabilities.
- Attribution: NOAA Office of Coast Survey, NOAA ENC chart display (US government, public domain). Max zoom 16.
- Notes: the same ENC data is native in OpenCPN through the Chart Downloader (better for navigation).

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


## Depth

### EMODnet Bathymetry (world)
- GUID `{C44877A0-D455-4238-B5E2-C8BFF25FC0D4}`
- Service: `https://tiles.emodnet-bathymetry.eu/2020/baselayer/web_mercator/{z}/{x}/{y}.png`
- Terms: https://tiles.emodnet-bathymetry.eu/wmts/1.0.0/WMTSCapabilities.xml - `<ows:Fees>None`, `<ows:AccessConstraints>none`;
  https://emodnet.ec.europa.eu/en/emodnet-web-service-documentation - "As images are pre-rendered and can be cached (locally or remotely)".
  The EMODnet terms page was not reachable (HTTP 403); the credit line below is the cautious default.
- Attribution: EMODnet Bathymetry Consortium

### EMODnet Bathymetry (European seas, depth shading)
- GUID `{91D4F1B4-A7CD-4FC8-95F1-637609068FB2}`
- Service: `https://tiles.emodnet-bathymetry.eu/latest/mean_atlas_land/web_mercator/{z}/{x}/{y}.png`
- Terms: as above. Attribution: EMODnet Bathymetry Consortium

### GEBCO bathymetry (world)
- GUID `{87D82F0F-4841-434F-ADB5-1D63B1BFE5DA}` (base chart), `"navigationWarning": true`
- Service: WMS `https://wms.gebco.net/mapserv`, layer `GEBCO_LATEST`, WMS 1.3.0, EPSG:3857, PNG
- Terms: grid is public domain, attribution required;
  WMS `AccessConstraints` (2026-10-22): "Imagery from this WMS is not to be used for navigation or any purpose relating to safety at sea."
- Attribution: GEBCO Compilation Group, GEBCO_LATEST grid (public domain, attribution required). Max zoom 12 (the grid is about 450 m).
- Notes: overlaps EMODnet Bathymetry (world); GEBCO also covers the polar and Pacific waters.

### NOAA BlueTopo bathymetry (US coast)
- GUID `{D73ACBDA-4E01-4C81-A36A-A069F38CF90C}` (base chart), `"navigationWarning": true`
- Service: WMS `https://nowcoast.noaa.gov/geoserver/bluetopo/wms`, layer `bathymetry` (the workspace also has `hillshade`, not used), WMS 1.3.0, EPSG:3857, PNG
- Terms: the capabilities state `Fees` "NONE" and `AccessConstraints` "NONE"; NOAA data, public domain (https://www.weather.gov/disclaimer).
- Attribution: NOAA National Bathymetric Source, BlueTopo via nowCOAST (US government, public domain). Max zoom 16.
- Coverage: US nearshore waters where BlueTopo exists; elsewhere the tile is transparent.


## Satellite

Imagery of the first three below. Google, Bing and Esri high-resolution imagery cannot be shipped (see "Not shipped"):
Sentinel-2 at 10 m and USGS show reef edges, banks and pass entrances, not single coral heads.

### Sentinel-2 Cloudless 2025 (EOX)
- GUID `{92122135-6B15-4800-AA9E-3E48C556DA17}`
- Service: `https://tiles.maps.eox.at/wmts/1.0.0/s2cloudless-2025_3857/default/g/{z}/{y}/{x}.jpg` (row before column)
- Terms: https://cloudless.eox.at/documentation/license - "You may use EOxCloudless freely for non-commercial purposes
  under the terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License, including: ... Personal use".
  The server rate-limits (https://maps.eox.at/), so the definition allows one connection.
- Attribution: EOxCloudless https://cloudless.eox.at by EOX IT Services GmbH (Contains modified Copernicus Sentinel data 2025)

### USGS Imagery (USA)
- GUID `{E05EDD5E-C6C3-483C-8BA7-8DE1D9892ED8}`
- Service: `https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer/tile/{z}/{y}/{x}` (US coverage only;
  the server refuses a non-browser User-Agent)
- Terms: https://www.usgs.gov/information-policies-and-instructions/copyrights-and-credits - "USGS-authored or produced data
  and information are considered to be in the U.S. Public Domain".
- Attribution: U.S. Geological Survey, public domain

### Grey source: Esri World Imagery

This source carries a `termsOfUseWarning` key: the program prints a terms-of-use warning before downloading from it. The
provider's terms may not allow downloading tiles for offline use; reading the terms and complying is the user's responsibility.
Kept grey by default.

- GUID `{7B743985-BC5F-4AB6-8915-AC5DBBB8F552}`
- Service: `https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}`
- Terms: https://www.arcgis.com/home/item.html?id=10df2279f9684e4a9f6a7f08febac2a9 - "This layer is not intended to be
  used to export tiles for offline. If you would like to export imagery for offline use in ArcGIS applications, you may use
  the World Imagery (for Export) layer"; Esri Master License Agreement.
- Attribution: Esri, Maxar, Earthstar Geographics, and the GIS User Community


## Coast and land

### Grey source: OpenTopoMap

This source carries a `termsOfUseWarning` key too (see above).

- GUID `{51C1079B-B55D-4DF7-A410-ADC118F53AB1}`
- Service: `https://a.tile.opentopomap.org/{z}/{x}/{y}.png` (one host, no subdomain rotation)
- Terms: https://opentopomap.org/about - "...sofern unser Server z.B. durch Massendownloads nicht zu stark beansprucht wird"
  (allowed as long as the server is not overloaded by mass downloads). Data CC-BY-SA.
- Attribution: Map data (c) OpenStreetMap contributors, SRTM; map style (c) OpenTopoMap (CC-BY-SA)


## Weather now

Snapshot sources: the program takes one frame ("now") and caches it as its own chart. Ten sources, current weather only.

### RainViewer radar
- GUID `{1A696D82-F258-41A4-BCD6-7648D9E08DE2}` (overlay layer), kind: snapshot (`json-last`, `radar.past`, field `path`)
- Service: `https://tilecache.rainviewer.com{ver}/256/{z}/{x}/{y}/2/1_1.png`; index: `https://api.rainviewer.com/public/weather-maps.json`
- Terms: https://www.rainviewer.com/api.html - "This API is available for personal and educational use only." and
  "You must display credit such as "Weather data by RainViewer" with a link back to rainviewer.com somewhere visible in
  your app or website. Attribution is mandatory under the free terms of use"; https://rainviewer.com/api/transition-faq.html -
  "Maximum zoom level set to 7", "Rate limiting set to 100 requests/IP/minute".
- Attribution: Weather data by RainViewer (https://www.rainviewer.com). Max zoom 7.
- Rate: at most 85 requests per minute: one connection, 700 ms between requests; the service allows 100 per IP per minute.
- Notes: RainViewer is for personal and educational use; radar only, no coverage over
  open sea; a frame stays on the server about 2 hours (a `resume` later fails its tiles).

### NASA VIIRS NOAA-20 true colour (GIBS)
- GUID `{8B9C244E-E978-4267-8FE4-D703534C0789}`, kind: snapshot (`wmts-domain`, daily)
- Service: `https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/VIIRS_NOAA20_CorrectedReflectance_TrueColor/default/{ver}/GoogleMapsCompatible_Level9/{z}/{y}/{x}.jpg` (row before column)
- Index: `https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/1.0.0/VIIRS_NOAA20_CorrectedReflectance_TrueColor/default/GoogleMapsCompatible_Level9/all/2026-01-01--2100-01-01.xml`
- Terms: https://nasa-gibs.github.io/gibs-api-docs/ - "NASA promotes full and open sharing of data"; acknowledgment asked
  for GIBS clients; https://www.earthdata.nasa.gov/engage/open-data-services-software-policies/data-use-guidance - "Unless
  the content is marked with a use restriction or license, data provided from a NASA-led mission are licensed as Creative
  Commons Zero (CC0)."
- Attribution: Imagery: NASA Global Imagery Browse Services (GIBS), part of the NASA Earth Science Data and Information System (ESDIS); VIIRS NOAA-20. Max zoom 9.
- Notes: the latest day may not yet cover the area.

### NASA IMERG precipitation rate (GIBS)
- GUID `{92E40853-04A2-46B7-8A5B-3665FCDDFDBE}` (overlay layer), kind: snapshot (`wmts-domain`, 30 min)
- Service: `https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/IMERG_Precipitation_Rate_30min/default/{ver}/GoogleMapsCompatible_Level6/{z}/{y}/{x}.png`
- Index: `https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/1.0.0/IMERG_Precipitation_Rate_30min/default/GoogleMapsCompatible_Level6/all/2026-01-01--2100-01-01.xml`
- Terms: as VIIRS above (NASA ESDIS data use guidance, CC0 unless marked otherwise).
- Attribution: Precipitation: NASA GPM IMERG via NASA Global Imagery Browse Services (GIBS), part of the NASA Earth Science Data and Information System (ESDIS). Max zoom 6.
- Notes: about 6 hours latency.

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
- Notes: the credit says `{year}`, which the program replaces by the year of the snapshot; without `TIME` the
  service returns a different frame than the capabilities default, so `TIME` stays in the URL.

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
- Notes: DWD's Meteosat layer is not used (EUMETSAT data, taken from EUMETView instead).

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
- Notes: transparent when no storm is active.

### NOAA nowCOAST NWS watches and warnings (US)
- GUID `{901E0ED9-78CD-41C4-BA6A-0E93CF08C4F0}` (overlay layer), kind: snapshot (`none-refetch`)
- Service: `https://nowcoast.noaa.gov/geoserver/alerts/wms`, layer `watches_warnings_advisories`, WMS 1.3.0, EPSG:3857, PNG, no `TIME`
- Index: `https://nowcoast.noaa.gov/geoserver/alerts/wms` (the service itself; every run fetches the area again)
- Terms: the capabilities state `Fees` "NONE" and `AccessConstraints` "NONE"; NWS data, public domain (https://www.weather.gov/disclaimer).
- Attribution: NOAA National Weather Service watches, warnings and advisories via nowCOAST (US government, public domain). Max zoom 8.
- Coverage: US land and coastal marine zones.


## Ocean

Four snapshot sources: two daily NASA GIBS products (`wmts-domain`) and two NOAA RTOFS model pictures (`wms-default`, `nearest-now`).

### NASA sea surface temperature (GIBS, GHRSST MUR)
- GUID `{AF87E1F3-ECD7-431F-81ED-E0C7E95458D0}` (overlay layer), kind: snapshot (`wmts-domain`, daily)
- Service: `https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/GHRSST_L4_MUR_Sea_Surface_Temperature/default/{ver}/GoogleMapsCompatible_Level7/{z}/{y}/{x}.png`
- Index: `https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/1.0.0/GHRSST_L4_MUR_Sea_Surface_Temperature/default/GoogleMapsCompatible_Level7/all/2026-01-01--2100-01-01.xml`
- Terms: https://www.earthdata.nasa.gov/engage/open-data-services-software-policies/data-use-guidance - "Unless the content is
  marked with a use restriction or license, data provided from a NASA-led mission are licensed as Creative Commons Zero (CC0)."
- Attribution: Imagery: NASA Global Imagery Browse Services (GIBS), GHRSST MUR L4 sea surface temperature (JPL PO.DAAC). Max zoom 7.
- Notes: a Gulf Stream and eddy picture before a crossing; the newest day is one to two days old (default time 2026-10-20 on 2026-10-22).

### NASA sea ice concentration (GIBS, GHRSST MUR)
- GUID `{544D656E-F024-4E79-ADC9-4981D7AE601A}` (overlay layer), kind: snapshot (`wmts-domain`, daily)
- Service: `https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/GHRSST_L4_MUR_Sea_Ice_Concentration/default/{ver}/GoogleMapsCompatible_Level7/{z}/{y}/{x}.png`
- Index: `https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/1.0.0/GHRSST_L4_MUR_Sea_Ice_Concentration/default/GoogleMapsCompatible_Level7/all/2026-01-01--2100-01-01.xml`
- Terms: as sea surface temperature above (NASA data use guidance, CC0 unless marked otherwise).
- Attribution: Imagery: NASA Global Imagery Browse Services (GIBS), GHRSST MUR L4 sea ice concentration (JPL PO.DAAC). Max zoom 7.
- Notes: the first plan named `AMSRU2_Sea_Ice_Concentration_12km`; its time dimension ends on 2025-09-01 (GIBS capabilities, 2026-10-22), so a "now"
  snapshot would be a year old. The MUR product (default 2026-10-20) replaces it.

### NOAA RTOFS surface currents (western Atlantic)
- GUID `{F022DF76-6F80-4294-9403-F1A6123EDD80}` (overlay layer), kind: snapshot (`wms-default`, `nearest-now`, 6-hourly)
- Service: `https://nowcoast.noaa.gov/geoserver/grtofs/wms`, layer `rtofs_east_sfc_currents`, WMS 1.3.0, EPSG:3857, PNG, `TIME={ver}`
- Index: `https://nowcoast.noaa.gov/geoserver/grtofs/rtofs_east_sfc_currents/ows?service=WMS&version=1.3.0&request=GetCapabilities`
- Terms: the capabilities state `Fees` "NONE" and `AccessConstraints` "NONE"; NOAA data, public domain (https://www.weather.gov/disclaimer).
- Attribution: NOAA RTOFS ocean model surface currents via nowCOAST (NOS and NWS/NCEP; US government, public domain). Max zoom 8.
- Coverage: US East coast waters and the western Atlantic, 99.8 W to 40.6 W, 0.1 N to 44.9 N (capabilities box).
- Notes: a forecast model's frame nearest to the run, not an observation. The `time` default is the last forecast hour (days ahead,
  2026-10-27 on 2026-10-23), so the source picks the listed instant nearest now instead.

### NOAA RTOFS surface currents (eastern Pacific)
- GUID `{D228EF7F-C01B-4A00-AD22-2C7B527A2CA5}` (overlay layer), kind: snapshot (`wms-default`, `nearest-now`, 6-hourly)
- Service: `https://nowcoast.noaa.gov/geoserver/grtofs/wms`, layer `rtofs_west_sfc_currents`, WMS 1.3.0, EPSG:3857, PNG, `TIME={ver}`
- Index: `https://nowcoast.noaa.gov/geoserver/grtofs/rtofs_west_sfc_currents/ows?service=WMS&version=1.3.0&request=GetCapabilities`
- Terms: the capabilities state `Fees` "NONE" and `AccessConstraints` "NONE"; NOAA data, public domain (https://www.weather.gov/disclaimer).
- Attribution: NOAA RTOFS ocean model surface currents via nowCOAST (NOS and NWS/NCEP; US government, public domain). Max zoom 8.
- Coverage: US West coast waters and the eastern Pacific, 179.9 W to 101.8 W, 14.4 N to 62.4 N (capabilities box).
- Notes: a forecast model's frame nearest to the run, not an observation. The `time` default is the last forecast hour (days ahead,
  2026-10-27 on 2026-10-23), so the source picks the listed instant nearest now instead.


## Not shipped, with the reason

Checked on 2026-10-22 unless a date says otherwise. Sources that need a key or whose terms are unknown live in a folder
OUTSIDE the repository (see README.md, "Chart sources").

- Google (all), Bing (all), Apple: the terms forbid pre-fetching, storing or bulk download (Google Maps Platform terms; Bing direct tile access "not allowed"; Apple's terms).
- OpenStreetMap standard tiles: "Offline use is not permitted" (https://operations.osmfoundation.org/policies/tiles/).
- Esri family beyond World Imagery (Ocean Base, Clarity, Topo, Hybrid): the item licence says "not intended to be used to export tiles for offline".
- Navionics and SonarChart: proprietary Garmin service, an authentication script is needed, no grant for offline use found;
  CLI 2026.10.8 lists such a source as "downloads are disabled for this map".
- Keyed sources: Lantmateriet topographic web chart (Sweden, free key; terms not retrieved).
- Anygis.ru proxy entries: a third-party proxy that rehosts other providers' tiles.
- EOX Sentinel-2 Cloudless 2016: not needed (2025 is bundled).
- Copernicus Marine waves and currents: no key needed for tiles and the licence allows copies, so it is a base-set candidate
  that is undecided.
