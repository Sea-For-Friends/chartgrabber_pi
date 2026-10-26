# Build from source

Dependencies (Debian / Ubuntu package names): `cmake` (3.20 or newer), `ninja-build` (or make), `g++` with C++23
(GCC 13 or newer), `pkg-config`, `libwxgtk3.2-dev`, `libgtk-3-dev`, `libgl-dev`, `libgtest-dev`; for the window tests
also `xvfb` and `xauth`. On Arch: `wxwidgets-gtk3 gtk3 gtest cmake ninja xorg-server-xvfb`.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCG_TARGET=debian-x86_64 -DCG_TARGET_VERSION=n/a
cmake --build build
ctest --test-dir build --output-on-failure
```

`CG_TARGET`, `CG_TARGET_VERSION` and `CG_TARGET_ARCH` fill `metadata.xml`: they must equal the host tuple of the
OpenCPN you import the plugin into (for example `debian-x86_64` / `12`, `debian-arm64` / `n/a`, `ubuntu-x86_64` /
`24.04`; `CG_TARGET_ARCH` defaults to the build machine). `CG_TARGET=all` is refused: the plugin links wxWidgets, whose
ABI differs between distributions. Build the tarball on the same kind of system it is for, never cross-built.
Tests: `-DCG_BUILD_TESTS=OFF` skips them; the window tests run on a private Xvfb, never on your desktop.

Pack the Plugin Manager tarball (`metadata.xml`, `usr/lib/opencpn/libchartgrabber_pi.so`,
`usr/share/opencpn/plugins/chartgrabber_pi/data/...`):

```
tools/tarball.sh build [out-dir]
```

The tarball is byte-reproducible for one build directory; outside a git checkout set `SOURCE_DATE_EPOCH`.

