# Theseus unified build system.
#
# One Makefile, four output targets, all driven from the same source tree
# under ../theseus. Each target writes its artifacts to a self-contained
# directory under $(BUILDS_ROOT), which defaults to ~/builds/theseus.
#
# ------------------------------------------------------------------------
# Targets
# ------------------------------------------------------------------------
#   make                      Xbox debug XBE (alias for `make debug`)
#   make debug                Xbox debug XBE      -> xbox-debug/default.xbe
#   make retail               Xbox retail XBE     -> xbox-retail/default.xbe
#   make desktop              Native desktop      -> desktop/theseus
#                             (macOS / Linux / MSYS2 Windows; auto-detected)
#   make desktop-win64        Windows cross from  -> desktop-win64/theseus.exe
#                             macOS or Linux via mingw-w64
#   make desktop-linux-arm64  Linux ARM64 cross   -> desktop-linux-arm64/theseus
#                             from x86_64 Linux via aarch64-linux-gnu-g++
#
#   make clean                Remove Xbox + native desktop builds
#   make desktop-clean        Remove just the native desktop build
#   make desktop-win64-clean  Remove just the Windows cross build
#   make desktop-linux-arm64-clean
#                             Remove just the Linux ARM64 cross build
#
# ------------------------------------------------------------------------
# Requirements (per target)
# ------------------------------------------------------------------------
# Xbox (debug/retail):
#   - clang + lld-link (`brew install llvm` on macOS, `apt install clang lld`
#     on Linux). Apple clang from the Xcode CLT works on macOS too, as long
#     as lld is available via Homebrew llvm.
#   - OXDK cloned and built: https://github.com/MrMilenko/OXDK
#   - An Xbox SDK source tree (headers + libs the dashboard ABI needs at
#     link time). Path passed via XDK_BASE.
#
# Native desktop:
#   - C++17 compiler (clang or gcc).
#   - SDL2, SDL2_mixer, OpenGL headers.
#     macOS:           brew install sdl2 sdl2_mixer
#     Debian/Ubuntu:   sudo apt install build-essential libsdl2-dev \
#                                       libsdl2-mixer-dev libgl-dev
#     Arch:            sudo pacman -S sdl2 sdl2_mixer
#     MSYS2 (Windows): pacman -S mingw-w64-x86_64-{gcc,SDL2,SDL2_mixer}
#
# Windows cross (mingw-w64):
#   - macOS:    brew install mingw-w64
#     Linux:    sudo apt install mingw-w64
#   - Plus the SDL2 + SDL2_mixer mingw devel tarballs and a locally built
#     mingw GLEW. See the desktop-win64 section header below for the
#     one-time setup script.
#
# Linux ARM64 cross (aarch64-linux-gnu):
#   - Build host: x86_64 Debian/Ubuntu (or any Debian-family distro that
#     supports the arm64 multiarch repo). Cross-compiling aarch64-linux
#     from macOS is technically possible but not supported here.
#   - Setup:
#       sudo dpkg --add-architecture arm64
#       sudo apt update
#       sudo apt install -y g++-aarch64-linux-gnu \
#                           libsdl2-dev:arm64 \
#                           libsdl2-mixer-dev:arm64 \
#                           libgl-dev:arm64
#   - glibc caveat: built binaries link against the build host's glibc
#     version. If your target machine ships an older glibc (e.g. Ubuntu
#     22.04 / glibc 2.35), build on a Debian Bookworm host (glibc 2.36)
#     or build directly on the target.
#
# ------------------------------------------------------------------------
# Configuration (override on command line or via environment)
# ------------------------------------------------------------------------
#   XDK_BASE=/path/to/xbox          MS XDK source tree (Xbox builds)
#   OXDK_DIR=/path/to/OXDK          OXDK toolkit (Xbox builds)
#   BUILDS_ROOT=/path/to/output     Where build dirs live (default: ~/builds/theseus)
#   SDL2_MINGW_DIR=/path/to/dir     SDL2 mingw devel root (Windows cross)
#   GLEW_MINGW_DIR=/path/to/dir     GLEW source root (Windows cross)
#   DEBUG=1                         Desktop / cross builds with -g -O0
#   ASAN=1                          Native desktop build with AddressSanitizer

# ============================================================
# Paths. override these via environment or make command line
# ============================================================

THESEUS_SRC  = ../theseus

# Xbox Development Kit source tree (private + public headers/libs)
XDK_BASE    ?= $(error XDK_BASE not set. Point it at your Xbox SDK tree, e.g. XDK_BASE=/Volumes/MyDrive/xbox)

# OXDK toolkit (provides cxbe and xdk_compat.h)
OXDK_DIR    ?= $(HOME)/OXDK

XDK_PUB_INC  = $(THESEUS_SRC)/xdk/include
XDK_PUB_LIB  = $(THESEUS_SRC)/xdk/lib
XDK_PRV_INC  = $(XDK_BASE)/private/inc
XDK_PRV_LIB  = $(XDK_BASE)/private/lib/i386
NTOS_INC     = $(XDK_BASE)/private/ntos/inc
NTOS_XAPI    = $(XDK_BASE)/private/ntos/xapi/inc
SDK_INC      = $(XDK_BASE)/public/sdk/inc
DDK_INC      = $(XDK_BASE)/public/ddk/inc
CXBE         = $(OXDK_DIR)/tools/cxbe/cxbe

# ============================================================
# Build configuration (debug vs retail)
# ============================================================

# All build artifacts live under ~/builds/theseus/<platform>/ so each
# build directory is self-contained and can be copied/zipped/run from
# anywhere without dragging the source tree along.
BUILDS_ROOT ?= $(HOME)/builds/theseus

# Default to debug
CONFIG      ?= debug

ifeq ($(CONFIG),retail)
  OUTPUT_DIR     = $(BUILDS_ROOT)/xbox-retail
  OPT_FLAGS      = -O0
  XBE_MODE       = RETAIL
  # Release libs. no debug asserts, no XBDM dependency
  D3D_LIB        = d3d8.lib
  D3DX_LIB       = d3dx8.lib
  DSOUND_LIB     = dsound.lib
  XGRAPHICS_LIB  = xgraphics.lib
  XAPILIB        = xapilib.lib
  XNET_LIB       = xnet.lib
  XONLINE_LIB    = xonline.lib
  CRT_LIB        = libcmt.lib
  CRTCPP_LIB     = libcpmt.lib
  XBDM_LIB      =
else
  OUTPUT_DIR     = $(BUILDS_ROOT)/xbox-debug
  OPT_FLAGS      = -O0 -g
  XBE_MODE       = DEBUG
  # Debug libs, except dsound (debug dsound asserts without a debugger)
  D3D_LIB        = d3d8.lib
  D3DX_LIB       = d3dx8.lib
  DSOUND_LIB     = dsound.lib
  XGRAPHICS_LIB  = xgraphicsd.lib
  XAPILIB        = xapilibd.lib
  XNET_LIB       = xnetd.lib
  XONLINE_LIB    = xonlined.lib
  CRT_LIB        = libcmtd.lib
  CRTCPP_LIB     = libcpmtd.lib
  XBDM_LIB      = xbdm.lib
endif

TARGET_EXE   = $(OUTPUT_DIR)/theseus.exe
TARGET_XBE   = $(OUTPUT_DIR)/default.xbe

# ============================================================
# Toolchain
# ============================================================

CC  = clang
CXX = clang++
LD  = lld-link
# lld-link /lib invokes the same COFF archiver as llvm-lib but is reachable
# without keg-only PATH hacks on a stock `brew install llvm`.
AR  = lld-link /lib

# Target: original Xbox (Pentium III, MSVC ABI)
TARGET_FLAGS = \
	-target i386-pc-windows-msvc \
	-march=pentium3 \
	-fms-extensions \
	-fms-compatibility \
	-fms-compatibility-version=13.10 \
	-fdelayed-template-parsing

CFLAGS = \
	$(TARGET_FLAGS) \
	-c \
	-MMD -MP \
	$(OPT_FLAGS) \
	-D_XBOX \
	-D_UNICODE \
	-DUNICODE \
	-D_LIGHTS \
	-D_MAC_CROSS_COMPILE \
	-D_X86_ \
	-DWIN32_LEAN_AND_MEAN \
	-Wno-microsoft-include \
	-Wno-pragma-pack \
	-Wno-ignored-pragmas \
	-Wno-deprecated-declarations \
	-Wno-writable-strings \
	-Wno-microsoft-cast \
	-Wno-unknown-pragmas \
	-Wno-extra-tokens \
	-Wno-nonportable-include-path \
	-Wno-typedef-redefinition \
	-include xdk_compat.h \
	-I. \
	-iquote $(THESEUS_SRC)/xbox \
	-iquote $(THESEUS_SRC) \
	-iquote $(THESEUS_SRC)/engine \
	-iquote $(THESEUS_SRC)/shared \
	-iquote $(THESEUS_SRC)/render \
	-I$(THESEUS_SRC)/xdk/include \
	-I$(XDK_PUB_INC) \
	-I$(XDK_PRV_INC) \
	-I$(NTOS_INC) \
	-I$(NTOS_XAPI) \
	-I$(SDK_INC) \
	-I$(SDK_INC)/crt \
	-I$(DDK_INC) \
	-I$(XDK_PRV_INC)/crypto

# stdcall is the default calling convention for Xbox (MSVC /Gz).
# Only set for C++. C functions use explicit WINAPI/WSAAPI attributes.
CXXFLAGS = $(CFLAGS) -fno-rtti -Xclang -fdefault-calling-conv=stdcall

LDFLAGS = \
	/nologo \
	/subsystem:windows \
	/fixed:no \
	/base:0x00010000 \
	/stack:1048576 \
	/machine:x86 \
	/entry:mainCRTStartup \
	/nodefaultlib \
	/force:multiple \
	/safeseh:no \
	/merge:.edata=.edataxb \
	/libpath:$(XDK_PUB_LIB) \
	/libpath:$(XDK_PRV_LIB) \
	/errorlimit:0 \
	/map:$(OUTPUT_DIR)/theseus.map \
	/alternatename:__imp__HalReturnToFirmware=__imp__HalReturnToFirmware@4 \
	/alternatename:__imp__HalInitiateShutdown=__imp__HalInitiateShutdown@0 \
	/alternatename:__imp__HalReadSMCTrayState=__imp__HalReadSMCTrayState@8 \
	/alternatename:__imp__HalReadSMBusValue=__imp__HalReadSMBusValue@16 \
	/alternatename:__imp__HalWriteSMBusValue=__imp__HalWriteSMBusValue@16 \
	/alternatename:__imp__IoCreateSymbolicLink=__imp__IoCreateSymbolicLink@8 \
	/alternatename:__imp__IoDeleteSymbolicLink=__imp__IoDeleteSymbolicLink@4 \
	/alternatename:__imp__IoDismountVolumeByName=__imp__IoDismountVolumeByName@4 \
	/alternatename:__imp__MmFreeContiguousMemory=__imp__MmFreeContiguousMemory@4 \
	/alternatename:__imp__KeDelayExecutionThread=__imp__KeDelayExecutionThread@12 \
	/alternatename:_ExReadWriteRefurbInfo=_ExReadWriteRefurbInfo@12 \
	/alternatename:_MU_CreateDeviceObject=_MU_CreateDeviceObject@12 \
	/alternatename:_MU_CloseDeviceObject=_MU_CloseDeviceObject@8 \
	/alternatename:_XcHMAC=_XcHMAC@28 \
	/alternatename:___CxxFrameHandler3=___CxxFrameHandler \
	/alternatename:_XapiDeleteCachePartition=_XapiDeleteCachePartition@4 \
	/alternatename:?vswprintf@@YAHPA_WIPB_WPAD@Z=_vswprintf \
	/alternatename:_mp3dec_init@4=_mp3dec_init \
	/alternatename:_mp3dec_decode_frame@20=_mp3dec_decode_frame \
	/alternatename:_mp3_decode_full@20=_mp3_decode_full

LIBS = \
	xboxkrnl.lib \
	$(XAPILIB) \
	xapilib.lib \
	$(D3D_LIB) \
	$(D3DX_LIB) \
	$(XGRAPHICS_LIB) \
	$(DSOUND_LIB) \
	$(XNET_LIB) \
	$(XONLINE_LIB) \
	$(CRT_LIB) \
	$(CRTCPP_LIB) \
	$(XBDM_LIB)

# ============================================================
# Source files
# ============================================================

SRCS_ENGINE = \
	engine/array.cpp \
	engine/debug.cpp \
	engine/dotfield_node.cpp \
	engine/locale_node.cpp \
	engine/math_node.cpp \
	engine/settings_nodes.cpp \
	engine/xap_compile.cpp \
	engine/minimp3_decode.cpp

# Shared sources compiled for BOTH Xbox and Desktop
SRCS_SHARED = \
	shared/activefile.cpp \
	shared/date_node.cpp \
	shared/file_ops.cpp \
	shared/file_util.cpp \
	shared/keyboard_node.cpp \
	shared/music_collection.cpp \
	shared/ntiosvc.cpp \
	shared/overlay_alert.cpp \
	shared/recovery_node.cpp \
	shared/savegame_grid.cpp \
	shared/screen.cpp \
	shared/settingsfile.cpp \
	shared/string_util.cpp \
	shared/timezone.cpp \
	shared/title_collection.cpp \
	shared/util.cpp \
	shared/xap_vm.cpp \
	shared/xbe.cpp

# Xbox-only shared files. Desktop provides replacements or stubs.
SRCS_SHARED_XBOX_ONLY = \
	shared/audio_system.cpp \
	shared/discord.cpp \
	shared/harddrive.cpp

SRCS_RENDER = \
	render/animation_nodes.cpp \
	render/asset_loader.cpp \
	render/autosmooth.cpp \
	render/camera.cpp \
	render/hud_node.cpp \
	render/materials.cpp \
	render/node_system.cpp \
	render/scene_groups.cpp \
	render/shape_render.cpp \
	render/starfield.cpp \
	render/text.cpp \
	render/tmap_system.cpp \
	render/widget_draw.cpp \
	render/widget_layer.cpp

SRCS_XBOX = \
	xbox/config.cpp \
	xbox/disc_management.cpp \
	xbox/dsound_manager.cpp \
	xbox/fps_widget.cpp \
	xbox/ftp_widget.cpp \
	xbox/iso_widget.cpp \
	xbox/input.cpp \
	xbox/live_accounts.cpp \
	xbox/main.cpp \
	xbox/memory_monitor.cpp \
	xbox/memutil.cpp \
	xbox/modchip.cpp \
	xbox/network.cpp \
	xbox/overlay.cpp \
	xbox/panic_screen.cpp \
	xbox/perf_widget.cpp \
	xbox/theseus_launcher.cpp \
	xbox/title_scanner.cpp \
	xbox/widget_config.cpp \
	xbox/xbox_live.cpp \
	xbox/xip_archive.cpp

SRCS_TOOLBOX = \
	toolbox/drive.cpp \
	toolbox/driveManager.cpp \
	toolbox/fileSystem.cpp \
	toolbox/ftpServer.cpp \
	toolbox/socketUtility.cpp \
	toolbox/stringUtility.cpp \
	toolbox/utils.cpp

ALL_SRCS = $(SRCS_ENGINE) $(SRCS_SHARED) $(SRCS_SHARED_XBOX_ONLY) $(SRCS_RENDER) $(SRCS_XBOX) $(SRCS_TOOLBOX)

# theseuslib: shared C library consumed by both the Xbox build and the
# desktop port. Provides the private-API helper functions the dashboard
# requires at link time, plus xiso / xip parsers shared with the desktop
# tooling. Built as a static library so external consumers can drop it in.
THESEUSLIB_DIR  = ../theseuslib
THESEUSLIB_SRCS = \
	theseuslib.c \
	xiso.c \
	xip_archive.c
THESEUSLIB_OBJS = $(addprefix $(OUTPUT_DIR)/theseuslib/,$(THESEUSLIB_SRCS:.c=.obj))
THESEUSLIB_DEPS = $(THESEUSLIB_OBJS:.obj=.d)
THESEUSLIB_LIB  = $(OUTPUT_DIR)/theseuslib.lib

# Object files
OBJS = $(addprefix $(OUTPUT_DIR)/,$(ALL_SRCS:.cpp=.obj)) $(OUTPUT_DIR)/d3d_compat.obj
XBOX_DEPS = $(OBJS:.obj=.d)

# Pull in auto-generated header dependencies (from -MMD -MP). On a fresh
# tree these don't exist yet. the leading '-' tells make to ignore that.
-include $(XBOX_DEPS)
-include $(THESEUSLIB_DEPS)

# ============================================================
# Rules
# ============================================================

.PHONY: all debug retail clean

all: $(TARGET_XBE)
	@echo "=== $(CONFIG) build complete: $(TARGET_XBE) ==="

debug:
	$(MAKE) CONFIG=debug all

retail:
	$(MAKE) CONFIG=retail all

$(TARGET_XBE): $(TARGET_EXE)
	$(CXBE) -MODE:$(XBE_MODE) \
		-TITLE:"Theseus" \
		-OUT:$@ \
		$<

$(TARGET_EXE): $(OBJS) $(THESEUSLIB_LIB)
	$(LD) $(LDFLAGS) /out:$@ $(OBJS) $(THESEUSLIB_LIB) $(LIBS)

# minimp3 is compiled in isolation. no XDK headers, no force-include.
# Only needs CRT headers (stdlib.h, string.h, math.h).
$(OUTPUT_DIR)/engine/minimp3_decode.obj: $(THESEUS_SRC)/engine/minimp3_decode.cpp | $(OUTPUT_DIR)/engine
	$(CXX) $(TARGET_FLAGS) -c -MMD -MP $(OPT_FLAGS) -fno-rtti \
		-I$(THESEUS_SRC)/engine -I$(SDK_INC)/crt -I$(SDK_INC) \
		-Wno-typedef-redefinition \
		-o $@ $<

$(OUTPUT_DIR)/%.obj: $(THESEUS_SRC)/%.cpp | $(OUTPUT_DIR)/engine $(OUTPUT_DIR)/shared $(OUTPUT_DIR)/render $(OUTPUT_DIR)/xbox $(OUTPUT_DIR)/toolbox
	$(CXX) $(CXXFLAGS) -o $@ $<

# Build-mac local compat files
$(OUTPUT_DIR)/d3d_compat.obj: d3d_compat.cpp | $(OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

# theseuslib build. Uses base CFLAGS (which has xdk_compat.h and the
# XDK headers theseuslib needs for xtl.h). The -iquote paths in CFLAGS
# only match quoted includes, so theseuslib's <xip_archive.h> (angle
# bracket) resolves to theseuslib/include/ not theseus/engine/.
$(OUTPUT_DIR)/theseuslib/%.obj: $(THESEUSLIB_DIR)/src/%.c | $(OUTPUT_DIR)/theseuslib
	$(CC) $(CFLAGS) -Xclang -fdefault-calling-conv=stdcall \
		-I$(THESEUSLIB_DIR)/include -o $@ $<

$(THESEUSLIB_LIB): $(THESEUSLIB_OBJS)
	$(AR) /nologo /out:$@ $(THESEUSLIB_OBJS)

$(OUTPUT_DIR)/engine $(OUTPUT_DIR)/shared $(OUTPUT_DIR)/render $(OUTPUT_DIR)/xbox $(OUTPUT_DIR)/toolbox $(OUTPUT_DIR)/theseuslib:
	mkdir -p $(OUTPUT_DIR)/engine $(OUTPUT_DIR)/shared $(OUTPUT_DIR)/render $(OUTPUT_DIR)/xbox $(OUTPUT_DIR)/toolbox $(OUTPUT_DIR)/theseuslib

clean:
	rm -rf $(BUILDS_ROOT)/xbox-debug $(BUILDS_ROOT)/xbox-retail $(BUILDS_ROOT)/desktop
	@# Sweep up the in-tree leftovers from the old layout if present.
	rm -rf bin bin-retail bin-desktop

# ============================================================
# Desktop build (macOS / Linux / Windows)
# ============================================================
# Uses native clang + SDL2 + OpenGL. No XDK dependency.
#
# Usage:
#   make desktop       Build desktop executable
#   make desktop-clean Remove desktop build artifacts
#
# Requirements:
#   - SDL2 (brew install sdl2 / apt install libsdl2-dev)
#   - SDL2_mixer (brew install sdl2_mixer / apt install libsdl2-mixer-dev)
#   - OpenGL headers (system-provided)

DESKTOP_DIR    ?= $(BUILDS_ROOT)/desktop
DESKTOP_TARGET = $(DESKTOP_DIR)/theseus

# Detect platform
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)
ifeq ($(UNAME_S),Darwin)
  DESKTOP_CXX  ?= clang++
  DESKTOP_CC   ?= clang
  SDL2_CFLAGS  := $(shell sdl2-config --cflags 2>/dev/null || echo -I/opt/homebrew/include/SDL2 -I/usr/local/include/SDL2)
  SDL2_LDFLAGS := $(shell sdl2-config --libs 2>/dev/null || echo -L/opt/homebrew/lib -lSDL2)
  DESKTOP_PLATFORM_LDFLAGS = -framework OpenGL -lSDL2_mixer
else ifeq ($(UNAME_S),Linux)
  # Default to system g++. Debian/Ubuntu ship gcc by default; users with
  # clang installed can still override with `make desktop DESKTOP_CXX=clang++`.
  DESKTOP_CXX  ?= g++
  DESKTOP_CC   ?= gcc
  SDL2_CFLAGS  := $(shell sdl2-config --cflags)
  SDL2_LDFLAGS := $(shell sdl2-config --libs)
  DESKTOP_PLATFORM_LDFLAGS = -lGL -lSDL2_mixer
else
  # Windows (MSYS2/MinGW). d3d8_sdl.h pulls <GL/glew.h> on _WIN32, so we
  # need -lglew32 at link time. -DGLEW_STATIC isn't set here because the
  # mingw-w64-x86_64-glew package ships a DLL, not a static lib.
  #
  # ws2_32: config.cpp's hostname / gethostbyname (network discovery).
  # dbghelp: sdl_main.cpp's StackWalk64-based panic crash handler.
  # Same set as the Win64 cross link line.
  DESKTOP_CXX  ?= g++
  DESKTOP_CC   ?= gcc
  SDL2_CFLAGS  := $(shell sdl2-config --cflags)
  SDL2_LDFLAGS := $(shell sdl2-config --libs)
  DESKTOP_PLATFORM_LDFLAGS = -lopengl32 -lglew32 -lSDL2_mixer -lws2_32 -ldbghelp
endif

# libmpv + libcurl: REQUIRED for the native desktop target.
#
#   libmpv. fullscreen video playback (media_player.cpp + media_ui.cpp)
#             and CD audio (cdaudio.cpp).
#   libcurl. in-process HTTPS for TMDB metadata (tmdb.cpp).
#
# The dashboard ships with these features always-on; there is no
# fallback / stub build. We probe via pkg-config and error out loudly
# with install hints if either library is missing.
#
# These checks gate ONLY the native `desktop` target. Cross-compile
# targets (desktop-win64, desktop-linux-arm64) wire in their own copies
# via target-scoped CFLAGS/LDFLAGS later in this file, so we skip the
# host-side pkg-config check when the user is only invoking a cross
# target. (Detected via MAKECMDGOALS.)
DESKTOP_NATIVE_GOALS := desktop desktop-clean
ifneq ($(filter $(DESKTOP_NATIVE_GOALS),$(or $(MAKECMDGOALS),desktop)),)
  ifeq ($(shell pkg-config --exists mpv 2>/dev/null && echo yes),yes)
    MPV_CFLAGS  := $(shell pkg-config --cflags mpv)
    MPV_LDFLAGS := $(shell pkg-config --libs mpv)
  else
    $(error libmpv (mpv.pc) not found via pkg-config. Install: macOS \
`brew install mpv`; Debian/Ubuntu `sudo apt install libmpv-dev`; \
MSYS2 `pacman -S mingw-w64-x86_64-mpv`)
  endif
  ifeq ($(shell pkg-config --exists libcurl 2>/dev/null && echo yes),yes)
    CURL_CFLAGS  := $(shell pkg-config --cflags libcurl)
    CURL_LDFLAGS := $(shell pkg-config --libs libcurl)
  else
    $(error libcurl (libcurl.pc) not found via pkg-config. Install: macOS \
`brew install curl`; Debian/Ubuntu `sudo apt install libcurl4-openssl-dev`; \
MSYS2 `pacman -S mingw-w64-x86_64-curl`)
  endif
  # libprojectM is vendored as a git submodule under
  # theseus/third-party/projectm and built into a local prefix by the
  # `projectm-deps` target below. projectM-4's .pc file declares an
  # `opengl` pkg-config dep that doesn't exist on macOS (GL is a system
  # framework), so we use direct -I/-L flags instead.
  #
  # No header-presence gate: `desktop` has a hard prereq on
  # `projectm-deps`, so the prefix is guaranteed populated by the time
  # the .cpp compile rule fires. (A previous version gated on a
  # parse-time wildcard, which caused first-time `make desktop` runs on
  # a clean checkout to silently ship a no-milkdrop binary.)
  PROJECTM_PREFIX := $(THESEUS_SRC)/third-party/projectm/install
  PROJECTM_CFLAGS  := -I$(PROJECTM_PREFIX)/include -DTHESEUS_HAS_MILKDROP=1
  PROJECTM_LDFLAGS := -L$(PROJECTM_PREFIX)/lib -lprojectM-4 -lprojectM-4-playlist \
                      -Wl,-rpath,$(PROJECTM_PREFIX)/lib
endif
CDAUDIO_CFLAGS  := $(MPV_CFLAGS)
CDAUDIO_LDFLAGS :=
CDAUDIO_DEFINES := -DUIX_CDAUDIO=1

DESKTOP_CFLAGS = \
	-std=c++17 \
	$(if $(DEBUG),-g -O0,-O2 -DNDEBUG) \
	-MMD -MP \
	-D_DESKTOP \
	-DTHESEUS_DESKTOP \
	-DUIX_DESKTOP=1 \
	-D_WINDOWS=1 \
	-D_THESEUS_STD_H=1 \
	-fpermissive \
	-fms-extensions \
	$(SDL2_CFLAGS) \
	-I$(THESEUS_SRC)/desktop \
	-I$(THESEUS_SRC)/desktop/imgui \
	-I$(THESEUS_SRC)/shared \
	-I$(THESEUS_SRC)/engine \
	-I$(THESEUS_SRC)/render \
	-Wno-writable-strings \
	-Wno-deprecated-declarations \
	-Wno-reorder-ctor \
	-Wno-unused-value \
	-Wno-switch \
	-Wno-pointer-to-int-cast \
	-Wno-int-to-pointer-cast \
	-Wno-void-pointer-to-int-cast \
	-Wno-tautological-compare \
	-Wno-parentheses \
	-Wno-narrowing \
	-Wno-dangling-else \
	-Wno-format \
	-Wno-microsoft-exception-spec \
	-Wno-macro-redefined \
	-Wno-microsoft \
	-Wno-null-conversion \
	-Wno-c++11-narrowing \
	$(MPV_CFLAGS) \
	$(CDAUDIO_DEFINES) \
	$(CDAUDIO_CFLAGS) \
	$(CURL_CFLAGS) \
	$(PROJECTM_CFLAGS)

DESKTOP_LDFLAGS = $(SDL2_LDFLAGS) $(DESKTOP_PLATFORM_LDFLAGS) $(MPV_LDFLAGS) $(CDAUDIO_LDFLAGS) $(CURL_LDFLAGS) $(PROJECTM_LDFLAGS) -lm

# Toggle: build the desktop with AddressSanitizer to catch UB / UAF
# at runtime. Use: `make desktop ASAN=1`.
ifdef ASAN
  DESKTOP_CFLAGS  += -fsanitize=address -fno-omit-frame-pointer
  DESKTOP_LDFLAGS += -fsanitize=address
endif

# Toggle: build the desktop with the bgfx backend instead of the
# OpenGL backend of d3d8_sdl.h. See project_bgfx_swap.md for context.
# `make desktop BGFX=1`. Submodules live at theseus/third-party/{bx,bimg,bgfx}.
# Build them first with `make -C theseus/third-party/bgfx osx-arm64-release`
# (or the right target for your platform) to get libbgfxRelease.a etc.
ifdef BGFX
  BGFX_ROOT       := $(THESEUS_SRC)/third-party/bgfx
  BX_ROOT         := $(THESEUS_SRC)/third-party/bx
  BIMG_ROOT       := $(THESEUS_SRC)/third-party/bimg
  DESKTOP_CFLAGS  += -DTHESEUS_USE_BGFX=1 \
                     -I$(BGFX_ROOT)/include -I$(BX_ROOT)/include -I$(BIMG_ROOT)/include
  ifeq ($(UNAME_S),Darwin)
    BGFX_BUILD_DIR  := $(BGFX_ROOT)/.build/osx-arm64/bin
    DESKTOP_CFLAGS  += -I$(BX_ROOT)/include/compat/osx
    DESKTOP_LDFLAGS += $(BGFX_BUILD_DIR)/libbgfxRelease.a \
                       $(BGFX_BUILD_DIR)/libbxRelease.a \
                       $(BGFX_BUILD_DIR)/libbimgRelease.a \
                       -framework Cocoa -framework Metal -framework MetalKit \
                       -framework QuartzCore -framework IOKit -framework CoreFoundation
  else ifeq ($(UNAME_S),Linux)
    # Build libs locally via:
    #   make -C theseus/third-party/bgfx linux-gcc-release64
    # bgfx on Linux uses Vulkan by default (sdl_main.cpp's auto-pick
    # lands there since Vulkan caps trump GL). Runtime needs vulkan-loader
    # + X11 system libs; -ldl/-lpthread are dl/thread for bgfx's RTLD use.
    BGFX_BUILD_DIR  := $(BGFX_ROOT)/.build/linux64_gcc/bin
    DESKTOP_CFLAGS  += -I$(BX_ROOT)/include/compat/linux
    DESKTOP_LDFLAGS += $(BGFX_BUILD_DIR)/libbgfxRelease.a \
                       $(BGFX_BUILD_DIR)/libbxRelease.a \
                       $(BGFX_BUILD_DIR)/libbimgRelease.a \
                       -lvulkan -lX11 -ldl -lpthread
  endif
endif

# Desktop sources: ALL from theseus/desktop/ (UIX Desktop tree imported in full).
# This is the "in entirety" import. after we have a working build, we'll
# refactor by moving files into theseus/engine/, shared/, render/ etc.
DESKTOP_SRCS_ENGINE = \
	$(THESEUS_SRC)/shared/xap_vm.cpp \
	$(THESEUS_SRC)/engine/xap_compile.cpp \
	$(THESEUS_SRC)/render/node_system.cpp \
	$(THESEUS_SRC)/shared/util.cpp \
	$(THESEUS_SRC)/engine/array.cpp \
	$(THESEUS_SRC)/shared/string_util.cpp \
	$(THESEUS_SRC)/engine/math_node.cpp \
	$(THESEUS_SRC)/shared/date_node.cpp

DESKTOP_SRCS_FILE = \
	$(THESEUS_SRC)/desktop/xip.cpp \
	$(THESEUS_SRC)/shared/file_util.cpp \
	$(THESEUS_SRC)/desktop/files.cpp \
	$(THESEUS_SRC)/shared/activefile.cpp \
	$(THESEUS_SRC)/shared/settingsfile.cpp

DESKTOP_SRCS_RENDER = \
	$(THESEUS_SRC)/shared/screen.cpp \
	$(THESEUS_SRC)/desktop/image.cpp \
	$(THESEUS_SRC)/desktop/texture.cpp \
	$(THESEUS_SRC)/render/tmap_system.cpp \
	$(THESEUS_SRC)/desktop/materials_desktop.cpp \
	$(THESEUS_SRC)/desktop/mesh.cpp \
	$(THESEUS_SRC)/render/shape_render.cpp \
	$(THESEUS_SRC)/desktop/text.cpp \
	$(THESEUS_SRC)/render/starfield.cpp \
	$(THESEUS_SRC)/engine/dotfield_node.cpp

DESKTOP_SRCS_UI = \
	$(THESEUS_SRC)/render/scene_groups.cpp \
	$(THESEUS_SRC)/render/hud_node.cpp \
	$(THESEUS_SRC)/render/camera.cpp \
	$(THESEUS_SRC)/render/animation_nodes.cpp \
	$(THESEUS_SRC)/shared/overlay_alert.cpp \
	$(THESEUS_SRC)/shared/recovery_node.cpp \
	$(THESEUS_SRC)/engine/settings_nodes.cpp

DESKTOP_SRCS_SYSTEM = \
	$(THESEUS_SRC)/desktop/dashapp.cpp \
	$(THESEUS_SRC)/desktop/dashinit.cpp \
	$(THESEUS_SRC)/desktop/config.cpp \
	$(THESEUS_SRC)/desktop/joystick.cpp \
	$(THESEUS_SRC)/desktop/keyboard.cpp \
	$(THESEUS_SRC)/desktop/locale.cpp \
	$(THESEUS_SRC)/shared/timezone.cpp \
	$(THESEUS_SRC)/desktop/debug.cpp \
	$(THESEUS_SRC)/desktop/memutil.cpp \
	$(THESEUS_SRC)/render/autosmooth.cpp

# Desktop-only platform sources (SDL window, ImGui, audio, tools)
DESKTOP_SRCS_PLATFORM = \
	$(THESEUS_SRC)/desktop/sdl_main.cpp \
	$(THESEUS_SRC)/desktop/launch.cpp \
	$(THESEUS_SRC)/desktop/path_template.cpp \
	$(THESEUS_SRC)/desktop/launchers/registry.cpp \
	$(THESEUS_SRC)/desktop/launchers/shell.cpp \
	$(THESEUS_SRC)/desktop/launchers/url.cpp \
	$(THESEUS_SRC)/desktop/launchers/xemu.cpp \
	$(THESEUS_SRC)/desktop/launchers/steam.cpp \
	$(THESEUS_SRC)/desktop/launchers/retroarch.cpp \
	$(THESEUS_SRC)/desktop/title_sanitize.cpp \
	$(THESEUS_SRC)/desktop/udata_synth.cpp \
	$(THESEUS_SRC)/xbox/modchip.cpp \
	$(THESEUS_SRC)/xbox/network.cpp \
	$(THESEUS_SRC)/desktop/desktop_stubs.cpp \
	$(THESEUS_SRC)/desktop/desktop_nodes.cpp \
	$(THESEUS_SRC)/desktop/audio_sdl.cpp \
	$(THESEUS_SRC)/desktop/cdaudio.cpp \
	$(THESEUS_SRC)/desktop/inspector.cpp \
	$(THESEUS_SRC)/desktop/menu_bar.cpp \
	$(THESEUS_SRC)/desktop/xap_editor.cpp \
	$(THESEUS_SRC)/desktop/skin_editor.cpp \
	$(THESEUS_SRC)/desktop/TextEditor.cpp \
	$(THESEUS_SRC)/desktop/hdd_browser.cpp \
	$(THESEUS_SRC)/desktop/xbox_hdd.cpp \
	$(THESEUS_SRC)/desktop/title_maker.cpp \
	$(THESEUS_SRC)/desktop/playlist.cpp \
	$(THESEUS_SRC)/desktop/playlist_maker.cpp \
	$(THESEUS_SRC)/desktop/preloader.cpp \
	$(THESEUS_SRC)/desktop/media_player.cpp \
	$(THESEUS_SRC)/desktop/boot_anim.cpp \
	$(THESEUS_SRC)/desktop/media_ui.cpp \
	$(THESEUS_SRC)/desktop/milkdrop_window.cpp \
	$(THESEUS_SRC)/desktop/tmdb.cpp \
	$(THESEUS_SRC)/desktop/http_util.cpp \
	$(THESEUS_SRC)/desktop/plex_client.cpp \
	$(THESEUS_SRC)/desktop/jellyfin_client.cpp \
	$(THESEUS_SRC)/desktop/xiso.cpp

# ImGui sources. imgui_impl_opengl3 is the GL renderer backend used by
# the default build; imgui_impl_bgfx (chunk 5d-3) is its bgfx
# counterpart, swapped in when THESEUS_USE_BGFX is set.
DESKTOP_SRCS_IMGUI = \
	$(THESEUS_SRC)/desktop/imgui/imgui.cpp \
	$(THESEUS_SRC)/desktop/imgui/imgui_draw.cpp \
	$(THESEUS_SRC)/desktop/imgui/imgui_tables.cpp \
	$(THESEUS_SRC)/desktop/imgui/imgui_widgets.cpp \
	$(THESEUS_SRC)/desktop/imgui/imgui_impl_sdl2.cpp
ifdef BGFX
  DESKTOP_SRCS_IMGUI += $(THESEUS_SRC)/desktop/imgui_impl_bgfx.cpp
else
  DESKTOP_SRCS_IMGUI += $(THESEUS_SRC)/desktop/imgui/imgui_impl_opengl3.cpp
endif

DESKTOP_ALL_SRCS = \
	$(DESKTOP_SRCS_ENGINE) \
	$(DESKTOP_SRCS_FILE) \
	$(DESKTOP_SRCS_RENDER) \
	$(DESKTOP_SRCS_UI) \
	$(DESKTOP_SRCS_SYSTEM) \
	$(DESKTOP_SRCS_PLATFORM) \
	$(DESKTOP_SRCS_IMGUI)

DESKTOP_OBJS = $(patsubst $(THESEUS_SRC)/%.cpp,$(DESKTOP_DIR)/%.o,$(DESKTOP_ALL_SRCS))
DESKTOP_DEPS = $(DESKTOP_OBJS:.o=.d)
-include $(DESKTOP_DEPS)

.PHONY: desktop desktop-clean shaders-bgfx shaders-bgfx-spirv shaders-bgfx-dx11 shaders-bgfx-glsl shaders-bgfx-essl

# bgfx shader compilation (Phase 2 of project_d3d_call_audit.md).
# Cross-compiles each .sc file in theseus/desktop/shaders/ to a .bin
# for every backend we care about: macOS Metal (shaders-bgfx) and
# Windows D3D11 (shaders-bgfx-dx11). Vulkan / GL fallback profiles
# slot in here when those platform builds land.
#
# Run manually with `make -C build shaders-bgfx[-dx11]`; not yet
# wired into the desktop rebuild flow since shader edits are
# infrequent and require a separate run anyway.
ifeq ($(UNAME_S),Darwin)
  ifeq ($(UNAME_M),arm64)
    SHADERC_BUILD_DIR := osx-arm64
  else
    SHADERC_BUILD_DIR := osx-x64
  endif
else ifeq ($(UNAME_S),Linux)
  SHADERC_BUILD_DIR := linux64_gcc
else
  SHADERC_BUILD_DIR := win64_mingw-gcc
endif
SHADERC ?= $(THESEUS_SRC)/third-party/bgfx/.build/$(SHADERC_BUILD_DIR)/bin/shadercRelease
SHADERS_SRC_DIR := $(THESEUS_SRC)/desktop/shaders
SHADERS_OUT_DIR := $(THESEUS_SRC)/../Data/shaders
BGFX_SHADER_INC := $(THESEUS_SRC)/third-party/bgfx/src

shaders-bgfx:
	@mkdir -p $(SHADERS_OUT_DIR)/metal
	@for SC in $(SHADERS_SRC_DIR)/vs_*.sc; do \
		NAME=$$(basename $$SC .sc); \
		echo "shaderc [metal] $$NAME"; \
		$(SHADERC) -f $$SC -o $(SHADERS_OUT_DIR)/metal/$$NAME.bin \
			--type v --platform osx --profile metal -i $(BGFX_SHADER_INC) \
			|| exit 1; \
	done
	@for SC in $(SHADERS_SRC_DIR)/fs_*.sc; do \
		NAME=$$(basename $$SC .sc); \
		echo "shaderc [metal] $$NAME"; \
		$(SHADERC) -f $$SC -o $(SHADERS_OUT_DIR)/metal/$$NAME.bin \
			--type f --platform osx --profile metal -i $(BGFX_SHADER_INC) \
			|| exit 1; \
	done
	@echo "=== bgfx shaders compiled to $(SHADERS_OUT_DIR)/metal/ ==="

# SPIR-V for the Vulkan renderer (Windows / Linux). bgfx's HLSL
# backend invokes Microsoft fxc.exe which isn't available on macOS,
# so we can't emit DXBC from the macOS cross-compile host. Vulkan
# delivers the same per-call CPU-cost win as D3D11 on Windows (the
# Phase 2 audit goal). AMD / NVIDIA / Intel all ship first-class
# Vulkan drivers there. The runtime loader resolves Data/shaders/
# spirv/ when bgfx::getRendererType() == RendererType::Vulkan.
#
# Run shaders-bgfx + shaders-bgfx-spirv before bundling a Windows
# BGFX build, otherwise theseus_bgfx_load_shader returns null and
# the FF shader program never links.
shaders-bgfx-spirv:
	@mkdir -p $(SHADERS_OUT_DIR)/spirv
	@for SC in $(SHADERS_SRC_DIR)/vs_*.sc; do \
		NAME=$$(basename $$SC .sc); \
		echo "shaderc [spirv] $$NAME"; \
		$(SHADERC) -f $$SC -o $(SHADERS_OUT_DIR)/spirv/$$NAME.bin \
			--type v --platform windows --profile spirv -i $(BGFX_SHADER_INC) \
			|| exit 1; \
	done
	@for SC in $(SHADERS_SRC_DIR)/fs_*.sc; do \
		NAME=$$(basename $$SC .sc); \
		echo "shaderc [spirv] $$NAME"; \
		$(SHADERC) -f $$SC -o $(SHADERS_OUT_DIR)/spirv/$$NAME.bin \
			--type f --platform windows --profile spirv -i $(BGFX_SHADER_INC) \
			|| exit 1; \
	done
	@echo "=== bgfx shaders compiled to $(SHADERS_OUT_DIR)/spirv/ ==="

# GLSL 1.50 (core, OpenGL 3.2+). Cross-emits cleanly from any host
# since glslang doesn't need a vendor toolchain. Runtime loader picks
# Data/shaders/glsl/ when bgfx::getRendererType() == RendererType::OpenGL.
shaders-bgfx-glsl:
	@mkdir -p $(SHADERS_OUT_DIR)/glsl
	@for SC in $(SHADERS_SRC_DIR)/vs_*.sc; do \
		NAME=$$(basename $$SC .sc); \
		echo "shaderc [glsl] $$NAME"; \
		$(SHADERC) -f $$SC -o $(SHADERS_OUT_DIR)/glsl/$$NAME.bin \
			--type v --platform linux --profile 150 -i $(BGFX_SHADER_INC) \
			|| exit 1; \
	done
	@for SC in $(SHADERS_SRC_DIR)/fs_*.sc; do \
		NAME=$$(basename $$SC .sc); \
		echo "shaderc [glsl] $$NAME"; \
		$(SHADERC) -f $$SC -o $(SHADERS_OUT_DIR)/glsl/$$NAME.bin \
			--type f --platform linux --profile 150 -i $(BGFX_SHADER_INC) \
			|| exit 1; \
	done
	@echo "=== bgfx shaders compiled to $(SHADERS_OUT_DIR)/glsl/ ==="

# GLES 3.00 (OpenGL ES 3.0). Steam Deck Gaming Mode + embedded Linux.
# Cross-emits same as glsl. Runtime loader picks Data/shaders/essl/.
shaders-bgfx-essl:
	@mkdir -p $(SHADERS_OUT_DIR)/essl
	@for SC in $(SHADERS_SRC_DIR)/vs_*.sc; do \
		NAME=$$(basename $$SC .sc); \
		echo "shaderc [essl] $$NAME"; \
		$(SHADERC) -f $$SC -o $(SHADERS_OUT_DIR)/essl/$$NAME.bin \
			--type v --platform android --profile 300_es -i $(BGFX_SHADER_INC) \
			|| exit 1; \
	done
	@for SC in $(SHADERS_SRC_DIR)/fs_*.sc; do \
		NAME=$$(basename $$SC .sc); \
		echo "shaderc [essl] $$NAME"; \
		$(SHADERC) -f $$SC -o $(SHADERS_OUT_DIR)/essl/$$NAME.bin \
			--type f --platform android --profile 300_es -i $(BGFX_SHADER_INC) \
			|| exit 1; \
	done
	@echo "=== bgfx shaders compiled to $(SHADERS_OUT_DIR)/essl/ ==="

# DXBC for the D3D11 renderer. Has to run on Windows because shaderc
# delegates HLSL -> DXBC compilation to fxc.exe, which only ships in
# the Windows SDK. Vertex shaders go through vs_5_0, fragment shaders
# through ps_5_0. The runtime loader picks Data/shaders/dx11/ when
# bgfx::getRendererType() == RendererType::Direct3D11.
shaders-bgfx-dx11:
	@mkdir -p $(SHADERS_OUT_DIR)/dx11
	@for SC in $(SHADERS_SRC_DIR)/vs_*.sc; do \
		NAME=$$(basename $$SC .sc); \
		echo "shaderc [dx11] $$NAME"; \
		$(SHADERC) -f $$SC -o $(SHADERS_OUT_DIR)/dx11/$$NAME.bin \
			--type v --platform windows --profile s_5_0 -i $(BGFX_SHADER_INC) \
			|| exit 1; \
	done
	@for SC in $(SHADERS_SRC_DIR)/fs_*.sc; do \
		NAME=$$(basename $$SC .sc); \
		echo "shaderc [dx11] $$NAME"; \
		$(SHADERC) -f $$SC -o $(SHADERS_OUT_DIR)/dx11/$$NAME.bin \
			--type f --platform windows --profile s_5_0 -i $(BGFX_SHADER_INC) \
			|| exit 1; \
	done
	@echo "=== bgfx shaders compiled to $(SHADERS_OUT_DIR)/dx11/ ==="


desktop: projectm-deps $(DESKTOP_TARGET)
	@echo "=== desktop build complete: $(DESKTOP_TARGET) ==="

# Build the vendored libprojectM (4.x) into a local prefix the first time
# it's needed. Absolute path so cmake stores it correctly in its cache and
# `cmake --install` resolves the prefix from the project root, not the
# build subdir.
PROJECTM_SRC    := $(abspath $(THESEUS_SRC)/third-party/projectm)
PROJECTM_MARKER := $(PROJECTM_SRC)/install/include/projectM-4/projectM.h

.PHONY: projectm-deps projectm-clean
projectm-deps: $(PROJECTM_MARKER)

$(PROJECTM_MARKER):
	@if [ ! -f $(PROJECTM_SRC)/CMakeLists.txt ]; then \
		echo "projectM submodule not initialized; running git submodule update..." ; \
		(cd $(THESEUS_SRC)/.. && git submodule update --init --recursive --depth 1 $(PROJECTM_SRC)) ; \
	fi
	@echo "=== Building libprojectM 4.x (one-time) ==="
	cd $(PROJECTM_SRC) && cmake -S . -B build \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX=$(PROJECTM_SRC)/install \
		-DENABLE_PLAYLIST=ON \
		-DENABLE_SDL_UI=OFF \
		-DENABLE_TESTING=OFF
	cd $(PROJECTM_SRC) && cmake --build build -j$$(sysctl -n hw.ncpu 2>/dev/null || nproc)
	cd $(PROJECTM_SRC) && cmake --install build

projectm-clean:
	rm -rf $(PROJECTM_SRC)/build $(PROJECTM_SRC)/install

$(DESKTOP_TARGET): $(DESKTOP_OBJS)
	@mkdir -p $(DESKTOP_DIR)
	$(DESKTOP_CXX) -o $@ $(DESKTOP_OBJS) $(DESKTOP_LDFLAGS)
	@# Configs/, Data/, Library/ live next to the binary at runtime; the
	@# binary CWDs into its own directory and resolves Xbox C:/Q:/E:
	@# paths through them. Prefer rsync for fast incrementals; fall back
	@# to cp -R on barebones systems (some Debian installs ship without rsync).
	@for D in Configs Data Library; do \
		if command -v rsync >/dev/null 2>&1; then \
			rsync -a --delete $(THESEUS_SRC)/../$$D/ $(DESKTOP_DIR)/$$D/; \
		else \
			rm -rf $(DESKTOP_DIR)/$$D && mkdir -p $(DESKTOP_DIR)/$$D && \
			cp -R $(THESEUS_SRC)/../$$D/. $(DESKTOP_DIR)/$$D/; \
		fi; \
	done
	@# Stamp Configs/version. Canon version lives in the source-tree
	@# Configs/version (`version=X.Y.YYMMDD`) and is hand-bumped on
	@# release. The build appends `-YYMMDD-<short-sha>` so artifact
	@# versions are traceable back to a build date + commit while still
	@# carrying the canonical release tag they were built from.
	@VFILE="$(DESKTOP_DIR)/Configs/version"; \
	 mkdir -p "$$(dirname $$VFILE)"; \
	 CANON=$$(grep -E '^version=' "$$VFILE" 2>/dev/null | head -1 | cut -d= -f2- | tr -d '\r' || true); \
	 [ -n "$$CANON" ] || CANON="0.0.000000"; \
	 BASE=$$(echo "$$CANON" | cut -d- -f1); \
	 STAMP="$$BASE-$$(date -u '+%y%m%d')-$$(git -C $(THESEUS_SRC)/.. rev-parse --short HEAD 2>/dev/null || echo unknown)"; \
	 printf '[default]\nversion=%s\n' "$$STAMP" > "$$VFILE"
	@echo "    (synced Configs/Data/Library + stamped version)"

$(DESKTOP_DIR)/%.o: $(THESEUS_SRC)/%.cpp
	@mkdir -p $(dir $@)
	$(DESKTOP_CXX) $(DESKTOP_CFLAGS) -c -o $@ $<

desktop-clean:
	rm -rf $(DESKTOP_DIR)

# ============================================================
# Windows cross-compile (mingw-w64 from macOS/Linux)
# ============================================================
# Uses x86_64-w64-mingw32-g++ + the SDL2 mingw devel libs from
# https://www.libsdl.org/. Output is a self-contained drop with the
# .exe, SDL2.dll, SDL2_mixer.dll, and Configs/Data/Library. runnable on Windows
# directly or under Wine/Whiskey on macOS.
#
# Setup (one-time):
#   brew install mingw-w64
#   mkdir -p ~/cross/sdl2-mingw && cd ~/cross/sdl2-mingw
#   curl -LO https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-devel-2.32.10-mingw.tar.gz
#   curl -LO https://github.com/libsdl-org/SDL_mixer/releases/download/release-2.8.1/SDL2_mixer-devel-2.8.1-mingw.tar.gz
#   tar xzf SDL2-devel-2.32.10-mingw.tar.gz
#   tar xzf SDL2_mixer-devel-2.8.1-mingw.tar.gz
#
# Override SDL2_MINGW_DIR if your layout differs.

WIN64_DIR     = $(BUILDS_ROOT)/desktop-win64
WIN64_TARGET  = $(WIN64_DIR)/theseus.exe
WIN64_CXX     = x86_64-w64-mingw32-g++

# libwinpthread-1.dll: on macOS/Homebrew it lives in x86_64-w64-mingw32/bin/;
# on Linux/APT it lives in x86_64-w64-mingw32/lib/. Try bin/ first, fall back
# to lib/ so both layouts work without manual configuration.
WIN64_PTHREAD_DLL := $(shell \
  BASE=$$(dirname $$(dirname $$(realpath $$(which x86_64-w64-mingw32-gcc 2>/dev/null)))) ; \
  BIN=$$BASE/x86_64-w64-mingw32/bin/libwinpthread-1.dll ; \
  LIB=$$BASE/x86_64-w64-mingw32/lib/libwinpthread-1.dll ; \
  if [ -f "$$BIN" ]; then echo $$BIN ; elif [ -f "$$LIB" ]; then echo $$LIB ; fi)

SDL2_MINGW_DIR    ?= $(HOME)/cross/sdl2-mingw
SDL2_MINGW_INC    = $(SDL2_MINGW_DIR)/SDL2-2.32.10/x86_64-w64-mingw32/include
SDL2_MINGW_LIB    = $(SDL2_MINGW_DIR)/SDL2-2.32.10/x86_64-w64-mingw32/lib
SDL2_MINGW_BIN    = $(SDL2_MINGW_DIR)/SDL2-2.32.10/x86_64-w64-mingw32/bin
MIXER_MINGW_INC   = $(SDL2_MINGW_DIR)/SDL2_mixer-2.8.1/x86_64-w64-mingw32/include
MIXER_MINGW_LIB   = $(SDL2_MINGW_DIR)/SDL2_mixer-2.8.1/x86_64-w64-mingw32/lib
MIXER_MINGW_BIN   = $(SDL2_MINGW_DIR)/SDL2_mixer-2.8.1/x86_64-w64-mingw32/bin

# GLEW static lib built locally from glew-2.2.0 source against the mingw
# toolchain (the prebuilt glew-2.2.0-win32 only ships MSVC .lib files).
# Build steps:
#   cd ~/cross/glew-src/glew-2.2.0
#   mkdir -p build-mingw
#   x86_64-w64-mingw32-gcc -DGLEW_STATIC -Iinclude -c src/glew.c -o build-mingw/glew.o
#   x86_64-w64-mingw32-ar rcs build-mingw/libglew32.a build-mingw/glew.o
GLEW_MINGW_DIR    ?= $(HOME)/cross/glew-src/glew-2.2.0
GLEW_MINGW_INC    = $(GLEW_MINGW_DIR)/include
GLEW_MINGW_LIB    = $(GLEW_MINGW_DIR)/build-mingw

# libmpv + libcurl mingw devel archives. shinchiro publishes mpv-mingw
# build outputs at https://github.com/shinchiro/mpv-winbuild-cmake/releases
# (look for `mpv-dev-x86_64-*.7z`). For libcurl, use the official mingw
# devel package from https://curl.se/windows/. Unpack each so that
# `include/mpv/client.h` and `include/curl/curl.h` resolve, then point
# the env vars at the unpacked roots.
MPV_MINGW_DIR    ?= $(HOME)/cross/mpv-mingw
CURL_MINGW_DIR   ?= $(HOME)/cross/curl-mingw
MPV_MINGW_INC    = $(MPV_MINGW_DIR)/include
MPV_MINGW_LIB    = $(MPV_MINGW_DIR)
CURL_MINGW_INC   = $(CURL_MINGW_DIR)/include
CURL_MINGW_LIB   = $(CURL_MINGW_DIR)/lib

# libprojectM 4.x for Windows cross. Built by the `projectm-deps-win64`
# target below into a separate install-win64/ prefix (kept apart from
# the native install/ prefix so both can coexist). Unlike the native
# desktop branch, we don't gate on header presence: desktop-win64 has
# a hard prereq on projectm-deps-win64, so the prefix is guaranteed
# populated by the time .cpp compilation fires. -I is silently ignored
# for missing dirs at parse time, so this is safe even on clean checkout.
PROJECTM_WIN64_PREFIX := $(abspath $(THESEUS_SRC)/third-party/projectm/install-win64)
PROJECTM_WIN64_CFLAGS  := -I$(PROJECTM_WIN64_PREFIX)/include -DTHESEUS_HAS_MILKDROP=1
PROJECTM_WIN64_LDFLAGS := -L$(PROJECTM_WIN64_PREFIX)/lib \
                          -lprojectM-4 -lprojectM-4-playlist

# DESKTOP_CFLAGS but with the mingw SDL2 / mpv / curl paths instead of
# the macOS ones, and SDL_MAIN_HANDLED so SDL2 doesn't try to substitute
# its own main().
WIN64_CFLAGS = \
	-std=c++17 \
	$(if $(DEBUG),-g -O0,-O2 -DNDEBUG) \
	-MMD -MP \
	-D_DESKTOP \
	-DTHESEUS_DESKTOP \
	-DUIX_DESKTOP=1 \
	-D_WINDOWS=1 \
	-D_THESEUS_STD_H=1 \
	-DSDL_MAIN_HANDLED \
	$(if $(BGFX),,-DGLEW_STATIC) \
	-DUIX_CDAUDIO=1 \
	-fpermissive \
	-I$(SDL2_MINGW_INC)/SDL2 \
	-I$(MIXER_MINGW_INC)/SDL2 \
	$(if $(BGFX),,-I$(GLEW_MINGW_INC)) \
	-I$(MPV_MINGW_INC) \
	-I$(CURL_MINGW_INC) \
	-I$(THESEUS_SRC)/desktop \
	-I$(THESEUS_SRC)/desktop/imgui \
	-I$(THESEUS_SRC)/shared \
	-I$(THESEUS_SRC)/engine \
	-I$(THESEUS_SRC)/render \
	$(PROJECTM_WIN64_CFLAGS) \
	-Wno-write-strings \
	-Wno-deprecated-declarations \
	-Wno-reorder \
	-Wno-unused-value \
	-Wno-switch \
	-Wno-pointer-to-int-cast \
	-Wno-int-to-pointer-cast \
	-Wno-tautological-compare \
	-Wno-parentheses \
	-Wno-narrowing \
	-Wno-dangling-else \
	-Wno-format

# mingw link order matters: -lmingw32 -lSDL2main -lSDL2 first.
# -Wl,--subsystem,windows + -Wl,-emainCRTStartup: mark the PE as windows
#   subsystem (no auto-console window from explorer launches) but keep
#   our existing `int main()` as the entry point. AttachConsole(ATTACH_
#   PARENT_PROCESS) at the top of main() reconnects stdout/stderr if the
#   binary was launched from a shell.
WIN64_LDFLAGS = \
	-L$(SDL2_MINGW_LIB) \
	-L$(MIXER_MINGW_LIB) \
	$(if $(BGFX),,-L$(GLEW_MINGW_LIB)) \
	-L$(MPV_MINGW_LIB) \
	-L$(CURL_MINGW_LIB) \
	-static-libgcc -static-libstdc++ \
	-Wl,--subsystem,windows -Wl,-emainCRTStartup \
	-Wl,--export-all-symbols \
	-lmingw32 -lSDL2main -lSDL2 -lSDL2_mixer \
	$(if $(BGFX),,-lglew32) \
	-lmpv -lcurl \
	$(PROJECTM_WIN64_LDFLAGS) \
	-lopengl32 -lgdi32 -limm32 -lole32 -loleaut32 -lsetupapi -lversion -lwinmm -luuid \
	-lws2_32 -ldbghelp -lavrt -ldwmapi -lcomdlg32

# BGFX=1 on Windows: link Vulkan via bgfx, drop the OpenGL backend.
# bgfx libs were built from the macOS host with:
#   cd theseus/third-party/bgfx
#   MINGW=/opt/homebrew/Cellar/mingw-w64/14.0.0/toolchain-x86_64 \
#   make -C .build/projects/gmake-mingw-gcc bx bimg bgfx \
#     config=release64 AR=x86_64-w64-mingw32-ar
# Outputs at .build/win64_mingw-gcc/bin/lib{bx,bimg,bgfx}Release.a.
# Shaders ship as SPIR-V (see shaders-bgfx-spirv); the runtime
# pins bgfx::RendererType::Vulkan on Windows in sdl_main.cpp.
# Windows system libs bgfx pulls in: gdi32 + psapi (renderer_d3d*
# stubs even though we use Vulkan).
ifdef BGFX
  BGFX_WIN64_BUILD_DIR := $(THESEUS_SRC)/third-party/bgfx/.build/win64_mingw-gcc/bin
  WIN64_CFLAGS  += -DTHESEUS_USE_BGFX=1 \
                   -I$(THESEUS_SRC)/third-party/bgfx/include \
                   -I$(THESEUS_SRC)/third-party/bx/include \
                   -I$(THESEUS_SRC)/third-party/bimg/include \
                   -I$(THESEUS_SRC)/third-party/bx/include/compat/mingw
  WIN64_LDFLAGS += $(BGFX_WIN64_BUILD_DIR)/libbgfxRelease.a \
                   $(BGFX_WIN64_BUILD_DIR)/libbxRelease.a \
                   $(BGFX_WIN64_BUILD_DIR)/libbimgRelease.a \
                   -lgdi32 -lpsapi
endif

# Source list mirrors desktop's BGFX branching (imgui_impl_bgfx
# instead of imgui_impl_opengl3 when BGFX=1).
WIN64_SRCS = $(filter-out \
  $(THESEUS_SRC)/desktop/imgui/imgui_impl_opengl3.cpp \
  $(THESEUS_SRC)/desktop/imgui_impl_bgfx.cpp, $(DESKTOP_ALL_SRCS))
ifdef BGFX
  WIN64_SRCS += $(THESEUS_SRC)/desktop/imgui_impl_bgfx.cpp
else
  WIN64_SRCS += $(THESEUS_SRC)/desktop/imgui/imgui_impl_opengl3.cpp
endif

# Same source list as desktop. the compiler reads the same files.
WIN64_OBJS = $(patsubst $(THESEUS_SRC)/%.cpp,$(WIN64_DIR)/%.o,$(WIN64_SRCS))
WIN64_DEPS = $(WIN64_OBJS:.o=.d)
-include $(WIN64_DEPS)

.PHONY: desktop-win64 desktop-win64-clean projectm-deps-win64 projectm-win64-clean

desktop-win64: projectm-deps-win64 $(WIN64_TARGET)
	@echo "=== windows cross-compile complete: $(WIN64_TARGET) ==="

# Cross-build libprojectM 4.x for Windows via mingw-w64. Uses the cmake
# toolchain file in build/cmake-toolchains/ and installs into a
# Windows-specific prefix so it doesn't collide with the native install
# (which the host's `desktop` target reads).
PROJECTM_WIN64_MARKER := $(PROJECTM_WIN64_PREFIX)/include/projectM-4/projectM.h
PROJECTM_WIN64_TOOLCHAIN := $(abspath $(THESEUS_SRC)/../build/cmake-toolchains/mingw-w64-x86_64.cmake)

projectm-deps-win64: $(PROJECTM_WIN64_MARKER)

$(PROJECTM_WIN64_MARKER):
	@if [ ! -f $(PROJECTM_SRC)/CMakeLists.txt ]; then \
		echo "projectM submodule not initialized; running git submodule update..." ; \
		(cd $(THESEUS_SRC)/.. && git submodule update --init --recursive --depth 1 $(PROJECTM_SRC)) ; \
	fi
	@echo "=== Cross-building libprojectM 4.x for Windows x64 (one-time) ==="
	cd $(PROJECTM_SRC) && cmake -S . -B build-win64 \
		-DCMAKE_TOOLCHAIN_FILE=$(PROJECTM_WIN64_TOOLCHAIN) \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX=$(PROJECTM_WIN64_PREFIX) \
		-DBUILD_SHARED_LIBS=ON \
		-DENABLE_PLAYLIST=ON \
		-DENABLE_SDL_UI=OFF \
		-DENABLE_TESTING=OFF
	cd $(PROJECTM_SRC) && cmake --build build-win64 \
		-j$$(sysctl -n hw.ncpu 2>/dev/null || nproc)
	cd $(PROJECTM_SRC) && cmake --install build-win64

projectm-win64-clean:
	rm -rf $(PROJECTM_SRC)/build-win64 $(PROJECTM_WIN64_PREFIX)

$(WIN64_TARGET): $(WIN64_OBJS)
	@mkdir -p $(WIN64_DIR)
	$(WIN64_CXX) -o $@ $(WIN64_OBJS) $(WIN64_LDFLAGS)
	@# Bundle every DLL the exe needs so the build directory is a self-
	@# contained Windows drop. -static-libgcc -static-libstdc++ folds the
	@# core runtime in; SDL2 / SDL2_mixer / libmpv / libcurl / libwinpthread
	@# / libprojectM are dynamic and have to ship alongside.
	@cp $(SDL2_MINGW_BIN)/SDL2.dll $(WIN64_DIR)/
	@cp $(MIXER_MINGW_BIN)/SDL2_mixer.dll $(WIN64_DIR)/
	@cp $(WIN64_PTHREAD_DLL) $(WIN64_DIR)/
	@if [ -f $(MPV_MINGW_DIR)/libmpv-2.dll ]; then cp $(MPV_MINGW_DIR)/libmpv-2.dll $(WIN64_DIR)/; fi
	@for f in $(CURL_MINGW_DIR)/bin/libcurl*.dll $(CURL_MINGW_DIR)/libcurl*.dll; do \
		[ -f "$$f" ] && cp "$$f" $(WIN64_DIR)/ || true ; \
	done; true
	@# projectM cmake puts shared libs in install/bin on mingw and in
	@# install/lib on UNIX-style sysroots; copy from whichever exists.
	@for D in $(PROJECTM_WIN64_PREFIX)/bin $(PROJECTM_WIN64_PREFIX)/lib; do \
		[ -d "$$D" ] || continue ; \
		for f in $$D/libprojectM-4*.dll $$D/projectM-4*.dll; do \
			[ -f "$$f" ] && cp "$$f" $(WIN64_DIR)/ || true ; \
		done ; \
	done; true
	@for D in Configs Data Library; do \
		if command -v rsync >/dev/null 2>&1; then \
			rsync -a --delete $(THESEUS_SRC)/../$$D/ $(WIN64_DIR)/$$D/; \
		else \
			rm -rf $(WIN64_DIR)/$$D && mkdir -p $(WIN64_DIR)/$$D && \
			cp -R $(THESEUS_SRC)/../$$D/. $(WIN64_DIR)/$$D/; \
		fi; \
	done
	@VFILE="$(WIN64_DIR)/Configs/version"; \
	 mkdir -p "$$(dirname $$VFILE)"; \
	 CANON=$$(grep -E '^version=' "$$VFILE" 2>/dev/null | head -1 | cut -d= -f2- | tr -d '\r' || true); \
	 [ -n "$$CANON" ] || CANON="0.0.000000"; \
	 BASE=$$(echo "$$CANON" | cut -d- -f1); \
	 STAMP="$$BASE-$$(date -u '+%y%m%d')-$$(git -C $(THESEUS_SRC)/.. rev-parse --short HEAD 2>/dev/null || echo unknown)"; \
	 printf '[default]\nversion=%s\n' "$$STAMP" > "$$VFILE"
	@echo "    (bundled DLLs + Configs/Data/Library + stamped version -> $(WIN64_DIR))"

$(WIN64_DIR)/%.o: $(THESEUS_SRC)/%.cpp
	@mkdir -p $(dir $@)
	$(WIN64_CXX) $(WIN64_CFLAGS) -c -o $@ $<

desktop-win64-clean: projectm-win64-clean
	rm -rf $(WIN64_DIR)

# ============================================================
# Linux ARM64 cross-compile (aarch64-linux-gnu from x86_64 Linux)
# ============================================================
# Builds an aarch64 Linux ELF for testers running ARM64 Linux on devices
# like jailbroken Switches, Pi 4/5 boxes, or ARM cloud VMs. Output is a
# self-contained drop with the binary + Configs/Data/Library/. Runtime deps on the
# target are libsdl2-2.0-0 + libsdl2-mixer-2.0-0 + libgl1 (or distro
# equivalents).
#
# Setup (one-time, on an x86_64 Debian/Ubuntu host):
#   sudo dpkg --add-architecture arm64
#   sudo apt update
#   sudo apt install -y g++-aarch64-linux-gnu \
#                       libsdl2-dev:arm64 \
#                       libsdl2-mixer-dev:arm64 \
#                       libgl-dev:arm64
#
# glibc compatibility: this binary will dynamically link against the
# build host's glibc. If the target machine has an older glibc, the
# binary will fail with "GLIBC_2.x not found". Build on a host whose
# glibc is <= the target's glibc, or build directly on the target.

LINUX_ARM64_DIR     = $(BUILDS_ROOT)/desktop-linux-arm64
LINUX_ARM64_TARGET  = $(LINUX_ARM64_DIR)/theseus
LINUX_ARM64_CXX     = aarch64-linux-gnu-g++

# Multiarch SDL2 paths. Debian's arm64 multiarch puts the headers under
# /usr/include/SDL2 (shared with the host arch) and the libs under
# /usr/lib/aarch64-linux-gnu/. sdl2-config returns x86_64 paths under
# multiarch, so don't use it here. override directly.
LINUX_ARM64_SDL2_INC = /usr/include/SDL2
LINUX_ARM64_SDL2_LIB = /usr/lib/aarch64-linux-gnu

# libmpv + libcurl for ARM64 cross. apt installs libmpv-dev:arm64 and
# libcurl4-openssl-dev:arm64 with pkgconfig under
# /usr/lib/aarch64-linux-gnu/pkgconfig; caller is expected to set
# PKG_CONFIG_LIBDIR to that path before invoking make. Falls back to
# system pkg-config if the user has already set it up correctly.
LINUX_ARM64_PKG_CONFIG := PKG_CONFIG_LIBDIR=$${PKG_CONFIG_LIBDIR:-/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig} pkg-config
LINUX_ARM64_MPV_CFLAGS  := $(shell $(LINUX_ARM64_PKG_CONFIG) --cflags mpv 2>/dev/null)
LINUX_ARM64_MPV_LDFLAGS := $(shell $(LINUX_ARM64_PKG_CONFIG) --libs mpv 2>/dev/null)
LINUX_ARM64_CURL_CFLAGS := $(shell $(LINUX_ARM64_PKG_CONFIG) --cflags libcurl 2>/dev/null)
LINUX_ARM64_CURL_LDFLAGS:= $(shell $(LINUX_ARM64_PKG_CONFIG) --libs libcurl 2>/dev/null)

# libprojectM 4.x for the Linux ARM64 cross. Built by the
# `projectm-deps-linux-arm64` target below into install-linux-arm64/.
# CI runs Linux ARM64 on a native ARM runner via the regular `desktop`
# target, so this prefix is only ever populated when someone is
# cross-compiling locally from an x86_64 Linux host.
PROJECTM_LINUX_ARM64_PREFIX := $(abspath $(THESEUS_SRC)/third-party/projectm/install-linux-arm64)
PROJECTM_LINUX_ARM64_CFLAGS  := -I$(PROJECTM_LINUX_ARM64_PREFIX)/include -DTHESEUS_HAS_MILKDROP=1
PROJECTM_LINUX_ARM64_LDFLAGS := -L$(PROJECTM_LINUX_ARM64_PREFIX)/lib \
                                -lprojectM-4 -lprojectM-4-playlist \
                                '-Wl,-rpath,$$ORIGIN/lib'

LINUX_ARM64_CFLAGS = \
	-std=c++17 \
	$(if $(DEBUG),-g -O0,-O2 -DNDEBUG) \
	-MMD -MP \
	-D_DESKTOP \
	-DTHESEUS_DESKTOP \
	-DUIX_DESKTOP=1 \
	-D_WINDOWS=1 \
	-D_THESEUS_STD_H=1 \
	-DUIX_CDAUDIO=1 \
	-fpermissive \
	-I$(LINUX_ARM64_SDL2_INC) \
	-I$(THESEUS_SRC)/desktop \
	-I$(THESEUS_SRC)/desktop/imgui \
	-I$(THESEUS_SRC)/shared \
	-I$(THESEUS_SRC)/engine \
	-I$(THESEUS_SRC)/render \
	$(LINUX_ARM64_MPV_CFLAGS) \
	$(LINUX_ARM64_CURL_CFLAGS) \
	$(PROJECTM_LINUX_ARM64_CFLAGS) \
	-Wno-write-strings \
	-Wno-deprecated-declarations \
	-Wno-reorder \
	-Wno-unused-value \
	-Wno-switch \
	-Wno-pointer-to-int-cast \
	-Wno-int-to-pointer-cast \
	-Wno-tautological-compare \
	-Wno-parentheses \
	-Wno-narrowing \
	-Wno-dangling-else \
	-Wno-format

LINUX_ARM64_LDFLAGS = \
	-L$(LINUX_ARM64_SDL2_LIB) \
	-lSDL2 -lSDL2_mixer \
	-lGL \
	$(LINUX_ARM64_MPV_LDFLAGS) \
	$(LINUX_ARM64_CURL_LDFLAGS) \
	$(PROJECTM_LINUX_ARM64_LDFLAGS) \
	-lm -ldl -lpthread

# Same source list as desktop. the compiler reads the same files.
LINUX_ARM64_OBJS = $(patsubst $(THESEUS_SRC)/%.cpp,$(LINUX_ARM64_DIR)/%.o,$(DESKTOP_ALL_SRCS))
LINUX_ARM64_DEPS = $(LINUX_ARM64_OBJS:.o=.d)
-include $(LINUX_ARM64_DEPS)

.PHONY: desktop-linux-arm64 desktop-linux-arm64-clean projectm-deps-linux-arm64 projectm-linux-arm64-clean

desktop-linux-arm64: projectm-deps-linux-arm64 $(LINUX_ARM64_TARGET)
	@echo "=== linux arm64 cross-compile complete: $(LINUX_ARM64_TARGET) ==="

# Cross-build libprojectM 4.x for Linux ARM64 via aarch64-linux-gnu-{gcc,g++}.
# Same shape as the Win64 path; install prefix kept separate so x86_64
# host and aarch64 target sysroots don't collide.
PROJECTM_LINUX_ARM64_MARKER    := $(PROJECTM_LINUX_ARM64_PREFIX)/include/projectM-4/projectM.h
PROJECTM_LINUX_ARM64_TOOLCHAIN := $(abspath $(THESEUS_SRC)/../build/cmake-toolchains/aarch64-linux-gnu.cmake)

projectm-deps-linux-arm64: $(PROJECTM_LINUX_ARM64_MARKER)

$(PROJECTM_LINUX_ARM64_MARKER):
	@if [ ! -f $(PROJECTM_SRC)/CMakeLists.txt ]; then \
		echo "projectM submodule not initialized; running git submodule update..." ; \
		(cd $(THESEUS_SRC)/.. && git submodule update --init --recursive --depth 1 $(PROJECTM_SRC)) ; \
	fi
	@echo "=== Cross-building libprojectM 4.x for Linux ARM64 (one-time) ==="
	cd $(PROJECTM_SRC) && cmake -S . -B build-linux-arm64 \
		-DCMAKE_TOOLCHAIN_FILE=$(PROJECTM_LINUX_ARM64_TOOLCHAIN) \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX=$(PROJECTM_LINUX_ARM64_PREFIX) \
		-DBUILD_SHARED_LIBS=ON \
		-DENABLE_PLAYLIST=ON \
		-DENABLE_SDL_UI=OFF \
		-DENABLE_TESTING=OFF
	cd $(PROJECTM_SRC) && cmake --build build-linux-arm64 \
		-j$$(sysctl -n hw.ncpu 2>/dev/null || nproc)
	cd $(PROJECTM_SRC) && cmake --install build-linux-arm64

projectm-linux-arm64-clean:
	rm -rf $(PROJECTM_SRC)/build-linux-arm64 $(PROJECTM_LINUX_ARM64_PREFIX)

$(LINUX_ARM64_TARGET): $(LINUX_ARM64_OBJS)
	@mkdir -p $(LINUX_ARM64_DIR)
	$(LINUX_ARM64_CXX) -o $@ $(LINUX_ARM64_OBJS) $(LINUX_ARM64_LDFLAGS)
	@# Configs/Data/Library next to the binary, same as desktop target.
	@for D in Configs Data Library; do \
		if command -v rsync >/dev/null 2>&1; then \
			rsync -a --delete $(THESEUS_SRC)/../$$D/ $(LINUX_ARM64_DIR)/$$D/; \
		else \
			rm -rf $(LINUX_ARM64_DIR)/$$D && mkdir -p $(LINUX_ARM64_DIR)/$$D && \
			cp -R $(THESEUS_SRC)/../$$D/. $(LINUX_ARM64_DIR)/$$D/; \
		fi; \
	done
	@# Bundle projectM .so files next to the binary under lib/ so the
	@# $ORIGIN/lib rpath in PROJECTM_LINUX_ARM64_LDFLAGS resolves at runtime.
	@mkdir -p $(LINUX_ARM64_DIR)/lib
	@for f in $(PROJECTM_LINUX_ARM64_PREFIX)/lib/libprojectM-4*.so* ; do \
		[ -e "$$f" ] && cp -a "$$f" $(LINUX_ARM64_DIR)/lib/ || true ; \
	done; true
	@echo "    (synced Configs/Data/Library + projectM .so -> $(LINUX_ARM64_DIR))"

$(LINUX_ARM64_DIR)/%.o: $(THESEUS_SRC)/%.cpp
	@mkdir -p $(dir $@)
	$(LINUX_ARM64_CXX) $(LINUX_ARM64_CFLAGS) -c -o $@ $<

desktop-linux-arm64-clean: projectm-linux-arm64-clean
	rm -rf $(LINUX_ARM64_DIR)
