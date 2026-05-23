# Cross-compile toolchain for x86_64-w64-mingw32 (Windows x64) from a
# POSIX host (macOS Homebrew mingw-w64, or Linux apt mingw-w64). Used
# by the `projectm-deps-win64` Makefile target to build libprojectM 4.x
# into theseus/third-party/projectm/install-win64/.
#
# Pass to cmake with:
#   -DCMAKE_TOOLCHAIN_FILE=/path/to/mingw-w64-x86_64.cmake
#
# Override the compiler prefix at configure time if your toolchain uses
# a different triple:
#   -DMINGW_TRIPLE=x86_64-w64-mingw32

set(CMAKE_SYSTEM_NAME       Windows)
set(CMAKE_SYSTEM_PROCESSOR  x86_64)

if(NOT DEFINED MINGW_TRIPLE)
    set(MINGW_TRIPLE x86_64-w64-mingw32)
endif()

set(CMAKE_C_COMPILER        ${MINGW_TRIPLE}-gcc)
set(CMAKE_CXX_COMPILER      ${MINGW_TRIPLE}-g++)
set(CMAKE_RC_COMPILER       ${MINGW_TRIPLE}-windres)
set(CMAKE_AR                ${MINGW_TRIPLE}-ar)
set(CMAKE_RANLIB            ${MINGW_TRIPLE}-ranlib)

# Static-link the gcc + stdc++ runtime into every output shared lib so
# libprojectM-4.dll and libprojectM-4-playlist.dll have no load-time
# dependency on libgcc_s_seh-1.dll / libstdc++-6.dll. Eliminates the
# class of ABI mismatch that hits when Windows resolves a different
# libstdc++-6.dll from PATH than the one the DLL was built against.
set(CMAKE_C_FLAGS_INIT             "-static-libgcc")
set(CMAKE_CXX_FLAGS_INIT           "-static-libgcc -static-libstdc++")
# -Wl,--exclude-libs=ALL keeps the statically-linked runtime symbols
# (_Unwind_Resume, __cxa_throw, etc.) internal to the DLL. Without it
# the import lib re-exports them and any consumer that also -static-
# linkages the same runtime hits multiple-definition errors.
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++ -Wl,--exclude-libs=libgcc.a:libgcc_eh.a:libstdc++.a")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-static-libgcc -static-libstdc++")

# Look for headers/libs in the mingw sysroot, not the host system. The
# sysroot path varies by distro; cmake walks the compiler's default
# search path so an explicit CMAKE_FIND_ROOT_PATH usually isn't needed,
# but locking the find-modes prevents accidental host-lib pickups.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
