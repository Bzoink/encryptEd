#!/usr/bin/env bash
# Build encryptEd for Linux (native, 64-bit, Release).
# Outputs: build/encryptEd

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
FLTK_LIB="$SCRIPT_DIR/lib/fltk/1_5/fltk-linux"

# ── Dependency checks ────────────────────────────────────────────────────────

ok=1

if ! command -v cmake &>/dev/null; then
    echo "ERROR: cmake not found in PATH." >&2
    ok=0
fi

if ! command -v g++ &>/dev/null && ! command -v c++ &>/dev/null; then
    echo "ERROR: No C++ compiler (g++ / c++) found in PATH." >&2
    ok=0
fi

if [[ ! -f "$FLTK_LIB/share/fltk/FLTKConfig.cmake" ]]; then
    echo "ERROR: Vendored FLTK not found at $FLTK_LIB" >&2
    echo "       Expected: lib/fltk/1_5/fltk-linux/share/fltk/FLTKConfig.cmake" >&2
    ok=0
fi

if [[ $ok -eq 0 ]]; then
    echo "Linux build aborted due to missing dependencies." >&2
    exit 1
fi

# ── Configure ────────────────────────────────────────────────────────────────

if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    echo "→ Configuring Linux build in $BUILD_DIR …"
    cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DENCRYPTED_FLTK_VERSION=1_5
fi

# ── Build ────────────────────────────────────────────────────────────────────

echo "→ Building (target: encryptEd) …"
cmake --build "$BUILD_DIR" --target encryptEd

echo ""
echo "✓ Linux build complete: $BUILD_DIR/encryptEd"
