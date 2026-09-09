#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

echo "=============================="
echo "  CaféBill — Build (Linux)"
echo "=============================="

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "→ Configuring with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_CXX_FLAGS="-march=x86-64 -mtune=generic" \
    -DCMAKE_EXE_LINKER_FLAGS="-march=x86-64 -mtune=generic"

echo "→ Building..."
make -j"$(nproc)"

echo ""
echo "→ Stripping x86-64-v2/v3 requirement from binaries..."
for BIN in "$BUILD_DIR/cafe-server" "$BUILD_DIR/cafe-client"; do
    if [ -f "$BIN" ]; then
        objcopy --remove-section=.note.gnu.property "$BIN" 2>/dev/null \
            || echo "  (skipped objcopy for $(basename "$BIN"))"
    fi
done

echo ""
echo "✓ Linux build complete!"
echo ""
echo "Binaries:"
echo "  $BUILD_DIR/cafe-server   — Admin server (run on admin machine)"
echo "  $BUILD_DIR/cafe-client   — Linux client (run on Linux workstations)"
echo ""

# ── Optional: cross-compile Windows client ────────────────────────────────
echo "Checking for MinGW-w64 cross-compiler (Windows client)..."
CXX_WIN=""
for c in x86_64-w64-mingw32-g++ i686-w64-mingw32-g++ x86_64-pc-mingw32-g++; do
    if command -v "$c" &>/dev/null; then CXX_WIN="$c"; break; fi
done

if [ -n "$CXX_WIN" ]; then
    echo "→ MinGW found: $CXX_WIN"
    echo "→ Cross-compiling Windows client..."
    mkdir -p "$SCRIPT_DIR/windows-client/build-win"
    $CXX_WIN -std=c++17 -O2 -march=x86-64 -mtune=generic -mwindows \
        -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0601 \
        -I"$SCRIPT_DIR/windows-client" \
        "$SCRIPT_DIR/windows-client/main.cpp" \
        "$SCRIPT_DIR/windows-client/WsClient.cpp" \
        -lws2_32 -lcomctl32 \
        -static \
        -o "$SCRIPT_DIR/windows-client/build-win/cafe-client.exe"
    echo "✓ Windows client built: windows-client/build-win/cafe-client.exe"
else
    echo "  (MinGW not found — skipping Windows cross-compilation)"
    echo "  → To build cafe-client.exe on Windows, see windows-client/build-native.bat"
    echo "  → To cross-compile from Linux, install: sudo apt install mingw-w64"
fi

echo ""
echo "Quick start:"
echo "  # Terminal 1 – Admin server"
echo "  $BUILD_DIR/cafe-server"
echo ""
echo "  # Terminal 2 – Linux client"
echo "  $BUILD_DIR/cafe-client"
echo ""
echo "  # Windows workstations: copy cafe-client.exe and run it"
