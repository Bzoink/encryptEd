#!/usr/bin/env bash
# Unified build script — run with --help for usage.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ── Colour helpers ─────────────────────────────────────────────────────────────

if [[ -t 1 ]]; then
    C_GREEN="\033[0;32m"; C_YELLOW="\033[1;33m"; C_RED="\033[0;31m"
    C_CYAN="\033[0;36m";  C_BOLD="\033[1m";      C_RESET="\033[0m"
else
    C_GREEN=""; C_YELLOW=""; C_RED=""; C_CYAN=""; C_BOLD=""; C_RESET=""
fi

info()   { echo -e "${C_CYAN}→${C_RESET} $*"; }
pass()   { echo -e "${C_GREEN}✓${C_RESET} $*"; }
warn()   { echo -e "${C_YELLOW}⚠${C_RESET} $*"; }
err()    { echo -e "${C_RED}✗${C_RESET} $*" >&2; }
header() { echo -e "\n${C_BOLD}━━━ $* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${C_RESET}"; }

# ── Argument parsing ───────────────────────────────────────────────────────────

do_linux=0; do_windows=0; do_macos=0
do_encrypted=0; do_etui=0
do_clean=0
explicit_platform=0; explicit_target=0

usage() {
    cat <<'EOF'
Build encryptEd (FLTK GUI) and/or etui (terminal UI) for Linux, Windows, or macOS.

Usage:
  ./build.sh [PLATFORM...] [TARGET...] [--all] [--help]

Platform (default: host platform):
  --linux      Build for Linux (native, 64-bit)
  --windows    Cross-compile for Windows 64-bit (requires mingw-w64)
  --macos      Build for macOS (must run on macOS)

Target (default: both):
  --encrypted  Build encryptEd FLTK GUI
  --etui       Build etui terminal UI

--all          Build all platforms and targets (skips unavailable ones)
--clean        Remove all build directories and exit

Build directories:
  build/           Linux encryptEd
  build-tui/       Linux etui
  build-win64/     Windows encryptEd
  build-tui-win64/ Windows etui
  build-macos/     macOS encryptEd
  build-tui-macos/ macOS etui

Examples:
  ./build.sh                    host platform, both targets
  ./build.sh --linux --etui     Linux terminal UI only
  ./build.sh --windows          Windows, both targets (requires mingw-w64)
  ./build.sh --all              everything (skips what's unavailable)
EOF
}

for arg in "$@"; do
    case "$arg" in
        --linux)     do_linux=1;     explicit_platform=1 ;;
        --windows)   do_windows=1;   explicit_platform=1 ;;
        --macos)     do_macos=1;     explicit_platform=1 ;;
        --encrypted) do_encrypted=1; explicit_target=1 ;;
        --etui)      do_etui=1;      explicit_target=1 ;;
        --all)
            do_linux=1; do_windows=1; do_macos=1
            do_encrypted=1; do_etui=1
            explicit_platform=1; explicit_target=1
            ;;
        --clean) do_clean=1 ;;
        --help|-h) usage; exit 0 ;;
        *) err "Unknown option: $arg"; echo "" >&2; usage >&2; exit 1 ;;
    esac
done

if [[ $explicit_platform -eq 0 ]]; then
    case "$(uname -s)" in
        Linux)  do_linux=1  ;;
        Darwin) do_macos=1  ;;
        *)      do_linux=1  ;;
    esac
fi

if [[ $explicit_target -eq 0 ]]; then
    do_encrypted=1; do_etui=1
fi

# ── Paths ──────────────────────────────────────────────────────────────────────

FLTK_LINUX="$SCRIPT_DIR/external/fltk/1_5/fltk-linux"
FLTK_WIN64="$SCRIPT_DIR/external/fltk/1_5/fltk-win64"
FLTK_MACOS="$SCRIPT_DIR/external/fltk/1_5/fltk-macos"
TOOLCHAIN_WIN64="$SCRIPT_DIR/lib/fltk/mingw64-toolchain.cmake"

BUILD_LINUX_ENC="$SCRIPT_DIR/build"
BUILD_LINUX_TUI="$SCRIPT_DIR/build-tui"
BUILD_WIN64_ENC="$SCRIPT_DIR/build-win64"
BUILD_WIN64_TUI="$SCRIPT_DIR/build-tui-win64"
BUILD_MACOS_ENC="$SCRIPT_DIR/build-macos"
BUILD_MACOS_TUI="$SCRIPT_DIR/build-tui-macos"

# ── Clean ─────────────────────────────────────────────────────────────────────

if [[ $do_clean -eq 1 ]]; then
    BUILD_DIRS=(
        "$SCRIPT_DIR/build"
        "$SCRIPT_DIR/build-tui"
        "$SCRIPT_DIR/build-win64"
        "$SCRIPT_DIR/build-tui-win64"
        "$SCRIPT_DIR/build-macos"
        "$SCRIPT_DIR/build-tui-macos"
    )
    header "Clean"
    any_removed=0
    for d in "${BUILD_DIRS[@]}"; do
        if [[ -d "$d" ]]; then
            info "Removing $d …"
            rm -rf "$d"
            pass "Removed $d"
            any_removed=1
        fi
    done
    [[ $any_removed -eq 0 ]] && info "Nothing to clean — no build directories found."
    echo ""
    exit 0
fi

# ── Probe available tools ──────────────────────────────────────────────────────

have_cmake=0;      command -v cmake &>/dev/null                         && have_cmake=1
have_cxx=0;        { command -v g++ || command -v clang++ || command -v c++; } &>/dev/null && have_cxx=1
have_mingw=0
have_git=0;        command -v git &>/dev/null                           && have_git=1
is_darwin=0;       [[ "$(uname -s)" == "Darwin" ]]                      && is_darwin=1

if command -v x86_64-w64-mingw32-gcc    &>/dev/null && \
   command -v x86_64-w64-mingw32-g++    &>/dev/null && \
   command -v x86_64-w64-mingw32-windres &>/dev/null; then
    have_mingw=1
fi

have_fltk_linux=0
[[ -f "$FLTK_LINUX/share/fltk/FLTKConfig.cmake" ]] && have_fltk_linux=1

have_fltk_win64=0
[[ -f "$FLTK_WIN64/CMake/FLTKConfig.cmake" ]] && have_fltk_win64=1

have_fltk_macos=0
if [[ $is_darwin -eq 1 ]]; then
    if [[ -f "$FLTK_MACOS/share/fltk/FLTKConfig.cmake" ]]; then
        have_fltk_macos=1
    elif command -v brew &>/dev/null; then
        _bp="$(brew --prefix fltk 2>/dev/null)" || true
        if [[ -n "${_bp:-}" ]] && \
           [[ -f "$_bp/lib/cmake/fltk/FLTKConfig.cmake" || -f "$_bp/share/fltk/FLTKConfig.cmake" ]]; then
            have_fltk_macos=1
        fi
    fi
    [[ $have_fltk_macos -eq 0 ]] && command -v fltk-config &>/dev/null && have_fltk_macos=1
fi

# ── Results tracking ───────────────────────────────────────────────────────────

results=()

record() {
    local label="$1" status="$2"
    results+=("$label:$status")
    case "$status" in
        OK)      pass "$label" ;;
        SKIPPED) warn "$label (skipped)" ;;
        FAILED)  err  "$label FAILED" ;;
    esac
}

# ── CMake helpers ──────────────────────────────────────────────────────────────

cmake_configure() {
    local build_dir="$1"; shift
    if [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
        info "Configuring $build_dir …"
        cmake -S "$SCRIPT_DIR" -B "$build_dir" "$@"
        return $?
    fi
    return 0
}

cmake_build() {
    local build_dir="$1" target="$2"
    info "Building $target …"
    cmake --build "$build_dir" --target "$target"
    return $?
}

# ── Linux / encryptEd ──────────────────────────────────────────────────────────

build_linux_encrypted() {
    local label="Linux / encryptEd"
    if [[ $have_cmake -eq 0 ]]; then
        err "cmake not found. Install: apt install cmake"
        record "$label" SKIPPED; return
    fi
    if [[ $have_cxx -eq 0 ]]; then
        err "No C++ compiler found (g++ / clang++). Install: apt install build-essential"
        record "$label" SKIPPED; return
    fi
    if [[ $have_fltk_linux -eq 0 ]]; then
        warn "Vendored FLTK not found — encryptEd GUI cannot be built."
        warn "  Expected: external/fltk/1_5/fltk-linux/share/fltk/FLTKConfig.cmake"
        record "$label" SKIPPED; return
    fi

    if cmake_configure "$BUILD_LINUX_ENC" \
        -DCMAKE_BUILD_TYPE=Release \
        -DENCRYPTED_FLTK_VERSION=1_5 \
        -DENCRYPTED_BUILD_TUI=OFF; then
        if cmake_build "$BUILD_LINUX_ENC" encryptEd; then
            pass "Output: $BUILD_LINUX_ENC/encryptEd"
            record "$label" OK
        else
            record "$label" FAILED
        fi
    else
        record "$label" FAILED
    fi
}

# ── Linux / etui ───────────────────────────────────────────────────────────────

build_linux_etui() {
    local label="Linux / etui"
    if [[ $have_cmake -eq 0 ]]; then
        err "cmake not found. Install: apt install cmake"
        record "$label" SKIPPED; return
    fi
    if [[ $have_cxx -eq 0 ]]; then
        err "No C++ compiler found. Install: apt install build-essential"
        record "$label" SKIPPED; return
    fi
    if [[ ! -f "$BUILD_LINUX_TUI/CMakeCache.txt" ]] && [[ $have_git -eq 0 ]]; then
        err "git not found — required to download FTXUI on first configure."
        err "  Install: apt install git"
        record "$label" SKIPPED; return
    fi

    if cmake_configure "$BUILD_LINUX_TUI" \
        -DCMAKE_BUILD_TYPE=Release \
        -DENCRYPTED_BUILD_TUI=ON; then
        if cmake_build "$BUILD_LINUX_TUI" etui; then
            pass "Output: $BUILD_LINUX_TUI/etui"
            record "$label" OK
        else
            record "$label" FAILED
        fi
    else
        record "$label" FAILED
    fi
}

# ── Windows / encryptEd ────────────────────────────────────────────────────────

build_windows_encrypted() {
    local label="Windows / encryptEd"
    if [[ $have_cmake -eq 0 ]]; then
        err "cmake not found. Install: apt install cmake"
        record "$label" SKIPPED; return
    fi
    if [[ $have_mingw -eq 0 ]]; then
        err "mingw-w64 cross-compiler not found."
        err "  Install: apt install mingw-w64"
        err "  Required: x86_64-w64-mingw32-gcc, x86_64-w64-mingw32-g++, x86_64-w64-mingw32-windres"
        record "$label" SKIPPED; return
    fi
    if [[ ! -f "$TOOLCHAIN_WIN64" ]]; then
        err "MinGW toolchain file not found: $TOOLCHAIN_WIN64"
        record "$label" SKIPPED; return
    fi
    if [[ $have_fltk_win64 -eq 0 ]]; then
        warn "Vendored FLTK for Windows not found — encryptEd GUI cannot be built."
        warn "  Expected: external/fltk/1_5/fltk-win64/CMake/FLTKConfig.cmake"
        record "$label" SKIPPED; return
    fi

    if cmake_configure "$BUILD_WIN64_ENC" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_WIN64" \
        -DCMAKE_BUILD_TYPE=Release \
        -DENCRYPTED_FLTK_VERSION=1_5 \
        -DENCRYPTED_BUILD_TUI=OFF; then
        if cmake_build "$BUILD_WIN64_ENC" encryptEd; then
            pass "Output: $BUILD_WIN64_ENC/encryptEd.exe"
            record "$label" OK
        else
            record "$label" FAILED
        fi
    else
        record "$label" FAILED
    fi
}

# ── Windows / etui ─────────────────────────────────────────────────────────────

build_windows_etui() {
    local label="Windows / etui"
    if [[ $have_cmake -eq 0 ]]; then
        err "cmake not found. Install: apt install cmake"
        record "$label" SKIPPED; return
    fi
    if [[ $have_mingw -eq 0 ]]; then
        err "mingw-w64 cross-compiler not found."
        err "  Install: apt install mingw-w64"
        record "$label" SKIPPED; return
    fi
    if [[ ! -f "$TOOLCHAIN_WIN64" ]]; then
        err "MinGW toolchain file not found: $TOOLCHAIN_WIN64"
        record "$label" SKIPPED; return
    fi
    if [[ ! -f "$BUILD_WIN64_TUI/CMakeCache.txt" ]] && [[ $have_git -eq 0 ]]; then
        err "git not found — required to download FTXUI on first configure."
        err "  Install: apt install git"
        record "$label" SKIPPED; return
    fi

    if cmake_configure "$BUILD_WIN64_TUI" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_WIN64" \
        -DCMAKE_BUILD_TYPE=Release \
        -DENCRYPTED_BUILD_TUI=ON; then
        if cmake_build "$BUILD_WIN64_TUI" etui; then
            pass "Output: $BUILD_WIN64_TUI/etui.exe"
            record "$label" OK
        else
            record "$label" FAILED
        fi
    else
        record "$label" FAILED
    fi
}

# ── macOS / encryptEd ──────────────────────────────────────────────────────────

build_macos_encrypted() {
    local label="macOS / encryptEd"
    if [[ $is_darwin -eq 0 ]]; then
        warn "macOS build requires running on macOS (current OS: $(uname -s))."
        record "$label" SKIPPED; return
    fi
    if [[ $have_cmake -eq 0 ]]; then
        err "cmake not found. Install: brew install cmake"
        record "$label" SKIPPED; return
    fi
    if [[ $have_cxx -eq 0 ]]; then
        err "No C++ compiler found. Run: xcode-select --install"
        record "$label" SKIPPED; return
    fi
    if [[ $have_fltk_macos -eq 0 ]]; then
        warn "FLTK not found — encryptEd GUI cannot be built."
        warn "  Option 1: place a macOS FLTK 1.5 build at external/fltk/1_5/fltk-macos/"
        warn "  Option 2: brew install fltk"
        record "$label" SKIPPED; return
    fi

    local cmake_extra=()
    if [[ -f "$FLTK_MACOS/share/fltk/FLTKConfig.cmake" ]]; then
        cmake_extra+=(-DCMAKE_PREFIX_PATH="$FLTK_MACOS")
    elif command -v brew &>/dev/null; then
        local bp
        bp="$(brew --prefix fltk 2>/dev/null)" || true
        [[ -n "${bp:-}" ]] && cmake_extra+=(-DCMAKE_PREFIX_PATH="$bp")
    fi

    if cmake_configure "$BUILD_MACOS_ENC" \
        -DCMAKE_BUILD_TYPE=Release \
        -DENCRYPTED_FLTK_VERSION=1_5 \
        -DENCRYPTED_BUILD_TUI=OFF \
        "${cmake_extra[@]}"; then
        if cmake_build "$BUILD_MACOS_ENC" encryptEd; then
            pass "Output: $BUILD_MACOS_ENC/encryptEd.app"
            record "$label" OK
        else
            record "$label" FAILED
        fi
    else
        record "$label" FAILED
    fi
}

# ── macOS / etui ───────────────────────────────────────────────────────────────

build_macos_etui() {
    local label="macOS / etui"
    if [[ $is_darwin -eq 0 ]]; then
        warn "macOS build requires running on macOS (current OS: $(uname -s))."
        record "$label" SKIPPED; return
    fi
    if [[ $have_cmake -eq 0 ]]; then
        err "cmake not found. Install: brew install cmake"
        record "$label" SKIPPED; return
    fi
    if [[ $have_cxx -eq 0 ]]; then
        err "No C++ compiler found. Run: xcode-select --install"
        record "$label" SKIPPED; return
    fi
    if [[ ! -f "$BUILD_MACOS_TUI/CMakeCache.txt" ]] && [[ $have_git -eq 0 ]]; then
        err "git not found — required to download FTXUI on first configure."
        err "  Install: brew install git  (or install Xcode)"
        record "$label" SKIPPED; return
    fi

    if cmake_configure "$BUILD_MACOS_TUI" \
        -DCMAKE_BUILD_TYPE=Release \
        -DENCRYPTED_BUILD_TUI=ON; then
        if cmake_build "$BUILD_MACOS_TUI" etui; then
            pass "Output: $BUILD_MACOS_TUI/etui"
            record "$label" OK
        else
            record "$label" FAILED
        fi
    else
        record "$label" FAILED
    fi
}

# ── Dispatch ───────────────────────────────────────────────────────────────────

if [[ $do_linux -eq 1 ]]; then
    header "Linux"
    [[ $do_encrypted -eq 1 ]] && build_linux_encrypted
    [[ $do_etui      -eq 1 ]] && build_linux_etui
fi

if [[ $do_windows -eq 1 ]]; then
    header "Windows (cross-compile)"
    [[ $do_encrypted -eq 1 ]] && build_windows_encrypted
    [[ $do_etui      -eq 1 ]] && build_windows_etui
fi

if [[ $do_macos -eq 1 ]]; then
    header "macOS"
    [[ $do_encrypted -eq 1 ]] && build_macos_encrypted
    [[ $do_etui      -eq 1 ]] && build_macos_etui
fi

# ── Summary ────────────────────────────────────────────────────────────────────

echo ""
echo -e "${C_BOLD}━━━ Summary ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${C_RESET}"
any_failed=0
for r in "${results[@]}"; do
    label="${r%%:*}"; status="${r##*:}"
    case "$status" in
        OK)      pass "$label" ;;
        SKIPPED) warn "$label (skipped — see above for details)" ;;
        FAILED)  err  "$label"; any_failed=1 ;;
    esac
done
echo ""

exit $any_failed
