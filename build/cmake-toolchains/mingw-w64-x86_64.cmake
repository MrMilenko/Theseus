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

# Look for headers/libs in the mingw sysroot, not the host system. The
# sysroot path varies by distro; cmake walks the compiler's default
# search path so an explicit CMAKE_FIND_ROOT_PATH usually isn't needed,
# but locking the find-modes prevents accidental host-lib pickups.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
