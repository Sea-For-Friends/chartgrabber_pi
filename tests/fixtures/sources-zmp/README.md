# Frozen .zmp sources

Two sailor-style source folders, kept byte for byte (byte order mark and CRLF line ends included):

- `OpenTopoMap.zmp/params.txt`
- `Norway-Sea-Chart-Kartverket.zmp/params.txt`

They are the files the plugin shipped as `data/sources/Coast and land/OpenTopoMap.zmp/params.txt` and
`data/sources/Nautical charts/Norway-Sea-Chart-Kartverket.zmp/params.txt` until the bundled set became `*.source.json`
(2026-10-23). The tests and the fixture capture use them as a sailor's own `.zmp` folder.
License: CC0-1.0, as `data/sources/LICENSE`.
