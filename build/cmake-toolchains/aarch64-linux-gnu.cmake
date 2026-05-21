# Cross-compile toolchain for aarch64-linux-gnu (Linux ARM64) from an
# x86_64 Linux host. Used by the `projectm-deps-linux-arm64` Makefile
# target to build libprojectM 4.x into
# theseus/third-party/projectm/install-linux-arm64/.
#
# Pass to cmake with:
#   -DCMAKE_TOOLCHAIN_FILE=/path/to/aarch64-linux-gnu.cmake
#
# Override the compiler triple at configure time if your toolchain uses
# a different prefix:
#   -DAARCH64_TRIPLE=aarch64-linux-gnu
#
# CI runs Linux ARM64 on a native ARM runner, so this file is only used
# for local cross builds from x86_64 hosts.

set(CMAKE_SYSTEM_NAME       Linux)
set(CMAKE_SYSTEM_PROCESSOR  aarch64)

if(NOT DEFINED AARCH64_TRIPLE)
    set(AARCH64_TRIPLE aarch64-linux-gnu)
endif()

set(CMAKE_C_COMPILER        ${AARCH64_TRIPLE}-gcc)
set(CMAKE_CXX_COMPILER      ${AARCH64_TRIPLE}-g++)
set(CMAKE_AR                ${AARCH64_TRIPLE}-ar)
set(CMAKE_RANLIB            ${AARCH64_TRIPLE}-ranlib)

# Debian multiarch puts aarch64 sysroot libs under /usr/lib/aarch64-linux-gnu
# and shared headers at /usr/include. Point cmake at the multiarch lib dir
# so find_library() picks the arm64 .so files when both arches are present.
set(CMAKE_FIND_ROOT_PATH    /usr/${AARCH64_TRIPLE} /usr/lib/${AARCH64_TRIPLE})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
