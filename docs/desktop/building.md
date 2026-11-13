# Building UIX Desktop

Everything builds from one Makefile in `build/`. The libraries each OS needs, and the versions they have to clear, are in [dependencies.md](dependencies.md). The CI workflows in `.github/workflows/` build every desktop target on every push, so when this page and the workflows disagree, trust the workflows.

| Target | Command (from `build/`) | Output |
|---|---|---|
| macOS / Linux, native | `make desktop` | `~/builds/theseus/desktop/theseus` |
| Windows x64, cross from macOS or Linux | `make desktop-win64` | `~/builds/theseus/desktop-win64/theseus.exe` |
| Linux ARM64, cross from x86_64 Debian/Ubuntu | `make desktop-linux-arm64` | `~/builds/theseus/desktop-linux-arm64/theseus` |

`BUILDS_ROOT=/some/dir` moves the output somewhere other than `~/builds/theseus`. `DEBUG=1` builds with `-g -O0`, and `ASAN=1` adds AddressSanitizer to a native build.

## 1. Clone with submodules

bgfx, bx, bimg, projectM and libdatachannel are submodules under `theseus/third-party/`.

```
git clone --recursive https://github.com/MrMilenko/Theseus
# or, in an existing clone:
git submodule update --init --recursive
```

## 2. Install the packages

See [dependencies.md](dependencies.md) for the per OS package lists. The build finds every library through `pkg-config`, so the package manager doesn't matter. If something sits off the default search path, point `PKG_CONFIG_PATH` at its `.pc` files. On macOS with Homebrew, curl is keg only:

```
export PKG_CONFIG_PATH="$(brew --prefix curl)/lib/pkgconfig:$PKG_CONFIG_PATH"
```

## 3. Build the bgfx runtime libraries (once per platform)

Only `bx`, `bimg` and `bgfx` are needed. bgfx's own top level targets also build `shaderc`, `geometryc` and a debug config, which takes far longer for nothing this build uses.

```
cd theseus/third-party/bgfx

# macOS (Apple Silicon)
../bx/tools/bin/darwin/genie --gcc=osx-arm64 gmake
make -C .build/projects/gmake-osx-arm64 -j$(sysctl -n hw.ncpu) bx bimg bgfx config=release

# Linux
../bx/tools/bin/linux/genie --gcc=linux-gcc gmake
make -C .build/projects/gmake-linux-gcc -j$(nproc) bx bimg bgfx config=release64 \
     CXXFLAGS=-DBGFX_CONFIG_RENDERER_OPENGL=33

# Windows, cross from Linux
../bx/tools/bin/linux/genie --gcc=mingw-gcc gmake
make -C .build/projects/gmake-mingw-gcc -j$(nproc) bx bimg bgfx config=release64 \
     CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++ \
     CXXFLAGS=-DBGFX_CONFIG_RENDERER_OPENGL=33
```

The OpenGL renderer needs `BGFX_CONFIG_RENDERER_OPENGL=33`. Without it bgfx targets OpenGL 2.1, compiles the shaders as GLSL 1.20, and strict drivers reject them, which leaves a black screen and then a crash.

Always give `-j` a number. A bare `-j` starts every compile at once, and on a small machine that runs it out of memory and can freeze the whole desktop. If a low RAM machine with no swap still struggles, use a smaller number than the core count. The same goes for every `make` and `cmake --build` below.

The `genie` that ships prebuilt in bx needs a recent glibc. On an older distro it fails with `GLIBC_2.38 not found`; build it from source and carry on:

```
git clone --depth 1 https://github.com/bkaradzic/GENie.git /tmp/genie
make -C /tmp/genie
cp /tmp/genie/bin/linux/genie theseus/third-party/bx/tools/bin/linux/genie
```

The compiled shaders are checked in under `Data/shaders/` for every backend (Metal, SPIR-V, Direct3D 11, GLSL, ESSL), so a normal build never runs `shaderc`. Only if you edit a `.sc` file in `theseus/desktop/shaders/` do you need the `shaders-bgfx*` targets, and those want a built `shaderc`.

## 4. Build libdatachannel (once per platform)

This is the WebRTC transport behind Xbox Cloud Gaming and remote play. It's easy to skip and the failure is quiet: the Makefile only defines `THESEUS_HAVE_WEBRTC` when it finds the static library, so without this step everything compiles and links and xCloud is simply missing. The folder name matters: `build-mac`, `build-linux` or `build-win64`, because that's where the Makefile looks.

```
cd theseus/third-party/libdatachannel

# macOS
cmake -S . -B build-mac -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
      -DNO_WEBSOCKET=1 -DNO_EXAMPLES=1 -DNO_TESTS=1 \
      -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)"
cmake --build build-mac -j$(sysctl -n hw.ncpu)

# Linux
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
      -DNO_WEBSOCKET=1 -DNO_EXAMPLES=1 -DNO_TESTS=1
cmake --build build-linux -j$(nproc)
```

The Windows cross build of it is in [Windows](#windows) below.

## 5. Build and run

```
cd build
make desktop
~/builds/theseus/desktop/theseus
```

The first `make desktop` also builds the vendored projectM into a local prefix and drops its shared library in `lib/` beside the binary. Each build copies `Configs/`, `Data/` and `Library/` next to the binary and stamps `Configs/version` with the build date and commit.

When the app runs, the copies beside the binary are only the starting point. Your settings and library live in the per user data folder (see the main README), seeded from those copies on first run.

## Windows

The release is cross compiled from Linux with mingw-w64 (`x86_64-w64-mingw32-g++`, posix thread model). The pieces mingw doesn't ship:

- **SDL2 and SDL2_mixer** mingw devel tarballs from libsdl.org, unpacked into `~/cross/sdl2-mingw`. The preamble of the `desktop-win64` section in `build/Makefile` has the exact commands.
- **libmpv** dev archive (shinchiro's mpv-winbuild-cmake releases) into `~/cross/mpv-mingw`, and the **curl** mingw zip from curl.se into `~/cross/curl-mingw`.
- **OpenSSL, opus and a trimmed FFmpeg**: `build/fetch-mingw-deps.sh` puts them in `~/cross/mingw64`. [dependencies.md](dependencies.md) explains why FFmpeg is built rather than fetched.
- **libdatachannel**, cross built against that prefix:

```
cmake -S theseus/third-party/libdatachannel \
      -B theseus/third-party/libdatachannel/build-win64 \
      -DCMAKE_TOOLCHAIN_FILE="$PWD/build/cmake-toolchains/mingw-w64-x86_64.cmake" \
      -DCMAKE_FIND_ROOT_PATH="$HOME/cross/mingw64" \
      -DOPENSSL_ROOT_DIR="$HOME/cross/mingw64" \
      -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
      -DNO_WEBSOCKET=1 -DNO_EXAMPLES=1 -DNO_TESTS=1 \
      -DENABLE_WARNINGS_AS_ERRORS=OFF
cmake --build theseus/third-party/libdatachannel/build-win64 -j$(nproc)
```

Then `make desktop-win64`. The output folder is a complete drop: the exe, every runtime DLL, and Configs/Data/Library. The `release-win64` job in `.github/workflows/release.yml` is the whole recipe end to end.

`make desktop` under an MSYS2 MinGW64 shell is also detected, but the cross build is the one CI tests.

## Linux ARM64

The release ARM64 build runs natively on an ARM64 runner with the same steps as x64, plus a GENie built from source. To cross compile from an x86_64 Debian or Ubuntu host instead, enable the arm64 multiarch packages listed at the top of `build/Makefile` and run `make desktop-linux-arm64`. The binary links against the build host's glibc, so build on a host whose glibc is no newer than the target's.

## Packaging

- **macOS app**: `build/mk-macapp.sh` wraps a desktop build into `~/builds/theseus/Theseus.app`. It copies the Homebrew dylibs into `Contents/Frameworks`, rewrites their install names to `@rpath` so the bundle runs without your Homebrew layout, builds the icon, and ad hoc signs the result. Ad hoc is enough to run it; handing it to other people properly needs a Developer ID and notarization.
- **Linux AppImage**: `build/mk-appimage.sh [build dir] [output]` bundles the binary with every library it pulls in, except the ones that have to come from the host (glibc, GPU drivers, X11, Wayland, audio servers), and needs `patchelf`.

## Before you send a change

- `make lint-platform`: only `theseus/desktop/host_*` may include OS headers or test OS macros. `_WIN32` is true on the Xbox too, so shared code goes through `host.h` instead.
- `make win64-import-guard`: compiles the Windows objects and fails if anything outside `host_win*` calls the Win32 file, clock or sleep functions directly.
- Build every desktop target you can, and the Xbox build if you touched `engine/`, `shared/` or `render/`.
