#!/usr/bin/env bash
# Build encryptEd for macOS (native, 64-bit, Release).
# Outputs: build-macos/encryptEd.app  (macOS bundle)
#
# FLTK is resolved in this order:
#   1. Vendored:  external/fltk/1_5/fltk-macos/  (place a macOS static build there)
#   2. Homebrew:  $(brew --prefix fltk)/
#   3. System:    fltk-config in PATH

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-macos"
FLTK_VENDORED="$SCRIPT_DIR/external/fltk/1_5/fltk-macos"

# ── OS check ─────────────────────────────────────────────────────────────────

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "ERROR: This script must be run on macOS (detected: $(uname -s))." >&2
    exit 1
fi

# ── Dependency checks ────────────────────────────────────────────────────────

ok=1

if ! command -v cmake &>/dev/null; then
    echo "ERROR: cmake not found. Install via 'brew install cmake' or Xcode tools." >&2
    ok=0
fi

if ! command -v clang++ &>/dev/null && ! command -v g++ &>/dev/null; then
    echo "ERROR: No C++ compiler found. Run 'xcode-select --install'." >&2
    ok=0
fi

# Locate FLTK
FLTK_CMAKE_EXTRA_ARGS=()
if [[ -f "$FLTK_VENDORED/share/fltk/FLTKConfig.cmake" ]]; then
    echo "→ Using vendored FLTK from $FLTK_VENDORED"
    FLTK_CMAKE_EXTRA_ARGS+=(-DCMAKE_PREFIX_PATH="$FLTK_VENDORED")
elif command -v brew &>/dev/null && brew --prefix fltk &>/dev/null 2>&1; then
    BREW_FLTK="$(brew --prefix fltk)"
    if [[ -f "$BREW_FLTK/lib/cmake/fltk/FLTKConfig.cmake" || \
          -f "$BREW_FLTK/share/fltk/FLTKConfig.cmake" ]]; then
        echo "→ Using Homebrew FLTK from $BREW_FLTK"
        FLTK_CMAKE_EXTRA_ARGS+=(-DCMAKE_PREFIX_PATH="$BREW_FLTK")
    fi
elif ! command -v fltk-config &>/dev/null; then
    echo "ERROR: FLTK not found. Options:" >&2
    echo "  1. Place a macOS FLTK 1.5 build at external/fltk/1_5/fltk-macos/" >&2
    echo "  2. Install via Homebrew: brew install fltk" >&2
    ok=0
fi

if [[ $ok -eq 0 ]]; then
    echo "macOS build aborted due to missing dependencies." >&2
    exit 1
fi

# ── Configure ────────────────────────────────────────────────────────────────

if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    echo "→ Configuring macOS build in $BUILD_DIR …"
    cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DENCRYPTED_FLTK_VERSION=1_5 \
        "${FLTK_CMAKE_EXTRA_ARGS[@]}"
fi

# ── Build ────────────────────────────────────────────────────────────────────

echo "→ Building (target: encryptEd) …"
cmake --build "$BUILD_DIR" --target encryptEd

echo ""
echo "✓ macOS build complete: $BUILD_DIR/encryptEd.app"
