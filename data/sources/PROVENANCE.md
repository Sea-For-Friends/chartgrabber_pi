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
