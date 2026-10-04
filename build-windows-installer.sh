#!/usr/bin/env bash
# Cross-compile encryptEd for Windows 64-bit and package the NSIS installer.
# Outputs: build-win64/windows-installer/encryptEd-*-windows-setup.exe

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-win64"
TOOLCHAIN="$SCRIPT_DIR/lib/fltk/mingw64-toolchain.cmake"
FLTK_LIB="$SCRIPT_DIR/external/fltk/1_5/fltk-win64"

# ── Dependency checks ────────────────────────────────────────────────────────

ok=1

if ! command -v cmake &>/dev/null; then
    echo "ERROR: cmake not found in PATH." >&2
    ok=0
fi

for tool in x86_64-w64-mingw32-gcc x86_64-w64-mingw32-g++ x86_64-w64-mingw32-windres; do
    if ! command -v "$tool" &>/dev/null; then
        echo "ERROR: $tool not found. Install mingw-w64 (e.g. apt install mingw-w64)." >&2
        ok=0
    fi
done

if ! command -v makensis &>/dev/null; then
    echo "ERROR: makensis not found. Install NSIS (e.g. apt install nsis)." >&2
    ok=0
fi

if [[ ! -f "$FLTK_LIB/CMake/FLTKConfig.cmake" ]]; then
    echo "ERROR: Vendored FLTK for Windows not found at $FLTK_LIB" >&2
    echo "       Expected: external/fltk/1_5/fltk-win64/CMake/FLTKConfig.cmake" >&2
    ok=0
fi

if [[ $ok -eq 0 ]]; then
    echo "Windows installer build aborted due to missing dependencies." >&2
    exit 1
fi

# ── Configure ────────────────────────────────────────────────────────────────

if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    echo "→ Configuring Windows cross-compile build in $BUILD_DIR …"
    cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
        -DCMAKE_BUILD_TYPE=Release \
        -DENCRYPTED_FLTK_VERSION=1_5
fi

# ── Build + package ──────────────────────────────────────────────────────────

echo "→ Building Windows installer …"
cmake --build "$BUILD_DIR" --target package_windows_installer

INSTALLER=$(ls "$BUILD_DIR/windows-installer/"*-setup.exe 2>/dev/null | head -1)
echo ""
echo "✓ Windows installer complete: $INSTALLER"
