#!/bin/sh
# Build Theme Park World HD (wsock32.dll) with a 32-bit MinGW-w64 toolchain.
#   Windows: MSYS2 mingw32 (C:/msys64/mingw32/bin), or set CXX
#   Linux:   CXX=i686-w64-mingw32-g++ ./build.sh
set -e
cd "$(dirname "$0")"
CXX=${CXX:-/c/msys64/mingw32/bin/g++.exe}
mkdir -p build
"$CXX" -std=c++17 -O2 -Wall -Wextra -Wno-cast-function-type -shared -static -s \
    -o build/wsock32.dll src/main.cpp src/registry.cpp src/headless.cpp src/display.cpp src/framecap.cpp src/ddtrace.cpp src/compat.cpp src/prof.cpp src/input.cpp src/window.cpp src/proxy.cpp src/wsock32.def \
    -lole32 -luuid -lwinmm -lversion -luser32 -lkernel32
echo "built build/wsock32.dll"
