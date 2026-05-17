#!/usr/bin/env bash
# Cross-compile the Windows client from Linux using MinGW-w64
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-win"

echo "======================================"
echo "  CaféBill Windows Client — Build"
echo "======================================"

# Detect MinGW cross-compiler
CXX=""
for candidate in \
    x86_64-w64-mingw32-g++ \
    i686-w64-mingw32-g++ \
    x86_64-pc-mingw32-g++
do
    if command -v "$candidate" &>/dev/null; then
        CXX="$candidate"
        break
    fi
done

if [ -z "$CXX" ]; then
    echo ""
    echo "ERROR: MinGW-w64 cross-compiler not found."
    echo ""
    echo "Install it with:"
    echo "  Ubuntu/Debian: sudo apt-get install mingw-w64"
    echo "  Arch:          sudo pacman -S mingw-w64-gcc"
    echo "  Fedora:        sudo dnf install mingw64-gcc-c++"
    echo ""
    echo "Or compile natively on Windows — see build-native.bat"
    exit 1
fi

echo "Using compiler: $CXX"
mkdir -p "$BUILD_DIR"

SRCS="$SCRIPT_DIR/main.cpp $SCRIPT_DIR/WsClient.cpp"
OUT="$BUILD_DIR/cafe-client.exe"

$CXX -std=c++17 -O2 -mwindows \
    -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0601 \
    -I"$SCRIPT_DIR" \
    $SRCS \
    -lws2_32 -lcomctl32 \
    -static-libgcc -static-libstdc++ \
    -o "$OUT"

echo ""
echo "✓ Build complete!"
echo "  Output: $OUT"
echo ""
echo "Copy cafe-client.exe to each Windows workstation and run it."
echo "Enter the server's IP address and port (default 12345) to connect."
