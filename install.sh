#!/usr/bin/env bash
# Install EncryptEd (FLTK GUI) and etui (terminal UI) with icons and a desktop
# entry, using the XDG/freedesktop layout shared by all Linux desktops.
# Run with --help for usage.

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
header() { echo -e "\n${C_BOLD}━━━ $* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${C_RESET}"; }

# ── Paths ──────────────────────────────────────────────────────────────────────

BUILD_LINUX_ENC="$SCRIPT_DIR/build"
BUILD_LINUX_TUI="$SCRIPT_DIR/build-tui"

# Must match ENCRYPTED_LINUX_ICON_SIZES in CMakeLists.txt.
ICON_SIZES=(16 24 32 48 64 128 256 512)

# ── Argument parsing ───────────────────────────────────────────────────────────

use_system=0
prefix=""
do_stage=0; stage_dir=""
do_build=0
with_cli=0; with_etui=1
do_uninstall=0

usage() {
    cat <<'EOF'
Install encryptEd (FLTK GUI) and etui (terminal UI) with icons and a desktop
entry, using the XDG/freedesktop layout shared by all Linux desktops.

Usage:
  ./install.sh [OPTIONS]

Scope (default: --user):
  --user           Install under ~/.local (no sudo)
  --system         Install under /usr/local (uses sudo)
  --prefix DIR     Install under DIR instead (no sudo unless --system)

Options:
  --build          Build first with ./build.sh if needed
  --with-cli       Also install the encrypted_cli prototype
  --no-etui        Skip etui even when it has been built

Staging and removal:
  --stage[=DIR]    Stage the install tree under DIR (default: build/install-root)
                   instead of touching the system; nothing outside DIR changes
  --stage-dir DIR  Same as --stage=DIR
  --uninstall      Remove a previous install from the chosen prefix

  --help, -h       Show this help

Examples:
  ./install.sh                  per-user install to ~/.local
  ./install.sh --system         system-wide install to /usr/local
  ./install.sh --stage          inspect the tree under build/install-root/
  ./install.sh --uninstall      remove the per-user install
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --user)      use_system=0; shift ;;
        --system)    use_system=1; shift ;;
        --prefix)
            [[ $# -ge 2 ]] || { err "--prefix requires a directory"; exit 1; }
            prefix="$2"; shift 2 ;;
        --prefix=*)  prefix="${1#--prefix=}"; shift ;;
        --build)     do_build=1; shift ;;
        --with-cli)  with_cli=1; shift ;;
        --no-etui)   with_etui=0; shift ;;
        --stage)     do_stage=1; stage_dir="$BUILD_LINUX_ENC/install-root"; shift ;;
        --stage=*)   do_stage=1; stage_dir="${1#--stage=}"; shift ;;
        --stage-dir)
            [[ $# -ge 2 ]] || { err "--stage-dir requires a directory"; exit 1; }
            do_stage=1; stage_dir="$2"; shift 2 ;;
        --uninstall) do_uninstall=1; shift ;;
        --help|-h)   usage; exit 0 ;;
        *)           err "Unknown option: $1"; echo "" >&2; usage >&2; exit 1 ;;
    esac
done

# ── Resolve prefix and privilege handling ──────────────────────────────────────

if [[ -z "$prefix" ]]; then
    if [[ $use_system -eq 1 ]]; then
        prefix="/usr/local"
    else
        prefix="$HOME/.local"
    fi
fi

if [[ $do_stage -eq 1 && $do_uninstall -eq 1 ]]; then
    err "--stage cannot be combined with --uninstall."
    exit 1
fi

if [[ "$(uname -s)" != "Linux" ]]; then
    err "install.sh installs Linux desktop integration (XDG layout) only."
    exit 1
fi

SUDO=()
if [[ $use_system -eq 1 && $do_stage -eq 0 && $EUID -ne 0 ]]; then
    if command -v sudo &>/dev/null; then
        SUDO=(sudo)
    else
        err "--system requires sudo (or run this script as root)."
        exit 1
    fi
fi

if [[ $do_uninstall -eq 0 ]]; then
    command -v cmake &>/dev/null || { err "cmake not found. Install it, or use a distro package."; exit 1; }
fi

# A non-sudo install into a directory we cannot write fails late with cmake
# errors; catch it early with a clear message instead.
if [[ $do_stage -eq 0 && ${#SUDO[@]} -eq 0 ]]; then
    probe="$prefix"
    while [[ ! -e "$probe" && "$probe" != "/" && "$probe" != "." ]]; do
        probe="$(dirname "$probe")"
    done
    if [[ -e "$probe" && ! -w "$probe" ]]; then
        err "No write permission for $prefix (nearest existing directory: $probe)."
        err "  Use --system for a sudo install, or choose --prefix under your home."
        exit 1
    fi
fi

# ── Build check ────────────────────────────────────────────────────────────────

ensure_build() {
    if [[ -f "$BUILD_LINUX_ENC/CMakeCache.txt" && -x "$BUILD_LINUX_ENC/encryptEd" ]]; then
        return 0
    fi
    if [[ $do_build -eq 0 ]]; then
        err "No built GUI binary at $BUILD_LINUX_ENC/encryptEd."
        err "  Run ./build.sh first, or re-run this script with --build."
        return 1
    fi
    header "Build"
    if ! "$SCRIPT_DIR/build.sh" --linux; then
        err "Build failed."
        return 1
    fi
    if [[ ! -x "$BUILD_LINUX_ENC/encryptEd" ]]; then
        err "Build did not produce $BUILD_LINUX_ENC/encryptEd."
        return 1
    fi
    return 0
}

# ── Cache refresh and reporting ────────────────────────────────────────────────

refresh_caches() {
    if [[ -d "$prefix/share/applications" ]] \
        && command -v update-desktop-database &>/dev/null; then
        if "${SUDO[@]}" update-desktop-database "$prefix/share/applications" &>/dev/null; then
            pass "Refreshed the desktop entry database."
        else
            warn "update-desktop-database reported an error (harmless on some desktops)."
        fi
    else
        warn "update-desktop-database not found — the menu entry appears after the next login."
    fi

    if [[ -d "$prefix/share/icons/hicolor" ]] \
        && command -v gtk-update-icon-cache &>/dev/null; then
        if ! "${SUDO[@]}" gtk-update-icon-cache -q -t -f "$prefix/share/icons/hicolor" &>/dev/null; then
            warn "gtk-update-icon-cache reported an error (icons still work, just uncached)."
        fi
    else
        warn "gtk-update-icon-cache not found — icons still work, just uncached."
    fi

    if command -v desktop-file-validate &>/dev/null \
        && [[ -f "$prefix/share/applications/encryptEd.desktop" ]]; then
        if desktop-file-validate "$prefix/share/applications/encryptEd.desktop" &>/dev/null; then
            pass "Desktop entry validates."
        else
            warn "desktop-file-validate reported issues in the installed desktop entry."
        fi
    fi
}

report_install() {
    header "Summary"
    pass "Installed under $prefix:"
    echo "    $prefix/bin/encryptEd"
    if [[ $with_etui -eq 1 && -x "$prefix/bin/etui" ]]; then
        echo "    $prefix/bin/etui"
    fi
    if [[ $with_cli -eq 1 && -x "$prefix/bin/encrypted_cli" ]]; then
        echo "    $prefix/bin/encrypted_cli"
    fi
    echo "    $prefix/share/applications/encryptEd.desktop"
    echo "    $prefix/share/icons/hicolor/<size>/apps/encryptEd.png"
    local plugins_dir="$prefix/share/encryptEd/default-plugins"
    [[ -d "$plugins_dir" ]] || plugins_dir="$prefix/share/encryptEd/plugin-examples"
    echo "    $plugins_dir"
    echo ""
    if [[ $use_system -eq 0 ]]; then
        case ":$PATH:" in
            *":$prefix/bin:"*) ;;
            *)
                warn "$prefix/bin is not on PATH — the menu entry (Exec=encryptEd) will not launch."
                warn "  Add it to your shell profile:  export PATH=\"$prefix/bin:\$PATH\""
                warn "  Or install system-wide:         ./install.sh --system"
                ;;
        esac
    fi
    info "Open EncryptEd from your application menu, or run encryptEd."
}

# ── Install ────────────────────────────────────────────────────────────────────

install_tree() {
    local dest_root=""
    [[ $do_stage -eq 1 ]] && dest_root="$stage_dir"

    header "Install"
    info "Installing encryptEd into $prefix …"
    if [[ $do_stage -eq 1 ]]; then
        DESTDIR="$stage_dir" cmake --install "$BUILD_LINUX_ENC" \
            --prefix "$prefix" --component gui_runtime || return 1
    else
        "${SUDO[@]}" cmake --install "$BUILD_LINUX_ENC" \
            --prefix "$prefix" --component gui_runtime || return 1
    fi

    if [[ $with_cli -eq 1 ]]; then
        info "Installing encrypted_cli into $prefix/bin …"
        if [[ $do_stage -eq 1 ]]; then
            DESTDIR="$stage_dir" cmake --install "$BUILD_LINUX_ENC" \
                --prefix "$prefix" --component cli || return 1
        else
            "${SUDO[@]}" cmake --install "$BUILD_LINUX_ENC" \
                --prefix "$prefix" --component cli || return 1
        fi
    fi

    if [[ $with_etui -eq 1 ]]; then
        if [[ -x "$BUILD_LINUX_TUI/etui" ]]; then
            info "Installing etui into $prefix/bin …"
            "${SUDO[@]}" install -Dm755 "$BUILD_LINUX_TUI/etui" \
                "${dest_root}${prefix}/bin/etui" || return 1
        else
            warn "etui not built at $BUILD_LINUX_TUI/etui — skipping (./build.sh --linux --etui builds it)."
        fi
    fi

    if [[ $do_stage -eq 1 ]]; then
        pass "Staged install tree under ${stage_dir}${prefix}"
        info "Copy it into place with:  sudo cp -a \"${stage_dir}${prefix}/.\" \"${prefix}/\""
        return 0
    fi

    refresh_caches
    report_install
    return 0
}

# ── Uninstall ──────────────────────────────────────────────────────────────────

uninstall_tree() {
    header "Uninstall"
    info "Removing EncryptEd from $prefix …"

    local paths=("$prefix/bin/encryptEd")
    [[ $with_etui -eq 1 ]] && paths+=("$prefix/bin/etui")
    [[ $with_cli -eq 1 ]] && paths+=("$prefix/bin/encrypted_cli")
    paths+=("$prefix/share/applications/encryptEd.desktop")
    paths+=("$prefix/share/icons/hicolor/scalable/apps/encryptEd.svg")
    local size
    for size in "${ICON_SIZES[@]}"; do
        paths+=("$prefix/share/icons/hicolor/${size}x${size}/apps/encryptEd.png")
    done

    local removed=0 failed=0 p
    for p in "${paths[@]}"; do
        if [[ -e "$p" || -L "$p" ]]; then
            if "${SUDO[@]}" rm -f "$p"; then
                pass "Removed $p"
                removed=1
            else
                err "Could not remove $p"
                failed=1
            fi
        fi
    done

    # Remove only installed data, never $prefix/share/encryptEd/plugins,
    # which holds the user's own Lua plugins.
    local data_paths=(
        "$prefix/share/encryptEd/default-plugins"
        "$prefix/share/encryptEd/plugin-examples"
        "$prefix/share/encryptEd/plugins.md"
        "$prefix/share/encryptEd/licenses"
    )
    for p in "${data_paths[@]}"; do
        if [[ -e "$p" || -L "$p" ]]; then
            if "${SUDO[@]}" rm -rf "$p"; then
                pass "Removed $p"
                removed=1
            else
                err "Could not remove $p"
                failed=1
            fi
        fi
    done

    # Drop share/encryptEd itself only when nothing else (user plugins) remains.
    if [[ -d "$prefix/share/encryptEd" ]]; then
        if "${SUDO[@]}" rmdir "$prefix/share/encryptEd" 2>/dev/null; then
            pass "Removed $prefix/share/encryptEd"
        elif [[ -d "$prefix/share/encryptEd/plugins" ]]; then
            info "Kept $prefix/share/encryptEd/plugins (your plugin directory)."
        fi
    fi

    if [[ $removed -eq 0 ]]; then
        warn "Nothing found to remove under $prefix."
    fi

    refresh_caches

    if [[ $failed -eq 1 ]]; then
        err "Some files could not be removed — try running with --system."
        return 1
    fi
    return 0
}

# ── Dispatch ───────────────────────────────────────────────────────────────────

if [[ $do_uninstall -eq 1 ]]; then
    uninstall_tree || exit 1
else
    ensure_build || exit 1
    install_tree || exit 1
fi

exit 0
