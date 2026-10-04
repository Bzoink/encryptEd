# EncryptEd — Implementation Reference

Author: Tony Mattsson

EncryptEd is a small cross-platform text editor for UTF-8 text files. Files are saved in a compressed and encrypted `.txx` format. The project is designed to be easy to compile, easy to understand, and small enough to produce compact self-contained executables for Windows, Linux, and modern macOS.

## Goals

- Easy to compile from source with no system dependencies beyond a C/C++ toolchain.
- Small executable and minimal runtime dependencies.
- Portable desktop builds for Windows, Linux, and modern macOS.
- All dependencies vendored under `lib` where practical.
- MIT, BSD-style, ISC, zlib, public domain, or similarly permissive licenses throughout.
- C17 for the core library; C++17 for the thin FLTK GUI layer.
- Full UTF-8 editor with encrypted compressed `.txx` document format.

## Architecture

The codebase has two layers.

**Core** (`src/core/`, `src/platform/`) is pure C17 and owns all format, crypto, compression, and text handling. It has no GUI dependency.

**GUI** (`src/gui/main_fltk.cpp`) is a single C++17 file built on FLTK 1.5. It calls into the core through the public C headers and adds no application logic of its own beyond UI interaction.

### Core modules

| File | Responsibility |
|---|---|
| `txx_format.c` | Binary `.txx` header layout, field constants, magic bytes |
| `txx_codec.c` | `.txx` encode/decode: KDF, encryption, compression, framing |
| `txx_crypto.c` | XChaCha20-Poly1305 AEAD via Monocypher; Argon2id KDF |
| `txx_compress.c` | Raw DEFLATE compression/decompression via microz |
| `txx_buffer.c` | Growable byte buffer with secure-free support |
| `txx_status.c` | Status codes and string descriptions |
| `utf8.c` | UTF-8/UTF-16/Windows-1252/ISO-8859-1 detection and transcoding; newline analysis |
| `platform/random.c` | Platform CSPRNG (getrandom on Linux, BCryptGenRandom on Windows) |
| `platform/secure_mem.c` | Compiler-barrier secure memory zeroing |

### Vendored dependencies

| Library | Location | License | Use |
|---|---|---|---|
| FLTK 1.5 | `external/fltk/1_5/` | FLTK (LGPL + exceptions) | GUI |
| Monocypher 4.0.2 | `external/monocypher/` | BSD 2-Clause / CC0 | Crypto and KDF |
| microz | `lib/microz/` | MIT (miniz-derived) | DEFLATE compression |
| FTXUI 5.0.0 | `external/ftxui/` (downloaded if missing) | MIT | Terminal UI (`etui`) |

FLTK and Monocypher are pristine upstream code and are not committed to this repository —
see the top-level `README.md` for how to obtain them.

## TXX File Format

The `.txx` format is documented in full in `docs/txx-format.md`. In summary:

- 96-byte fixed header (magic, version, flags, algorithm IDs, KDF parameters, lengths).
- Header is authenticated as AEAD associated data — any tampering causes auth failure.
- 16-byte random salt, 24-byte random nonce, 16-byte Poly1305 authentication tag.
- Payload = DEFLATE-compressed UTF-8 text, encrypted with XChaCha20-Poly1305.
- Key derived from user password via Argon2id using the stored salt and KDF parameters.
- Fresh salt and nonce generated on every save.
- No password or password hash is stored in the file.

## Editor Features

### File menu

| Action | Notes |
|---|---|
| New | Clears the editor; warns about unsaved changes |
| Open… (`Ctrl+O`) | Opens `.txx` (prompts for password) or any plain-text file; auto-detects encoding |
| Open Containing Folder | Opens the file's directory in the system file manager; disabled when no file is open |
| Save (`Ctrl+S`) | Saves to the current path; prompts for password if saving `.txx` for the first time |
| Save As… | Saves to a new path; format determined by extension (`.txx` = encrypted, anything else = plain UTF-8) |
| Line Endings | Radio submenu: LF, CRLF, CR, Mixed (Preserve) |
| Quit (`Ctrl+Q`) | Warns about unsaved changes |

Open detects the source encoding automatically and transcodes to UTF-8. Supported input encodings: UTF-8 (with or without BOM), UTF-16 LE/BE, Windows-1252, ISO-8859-1. Mixed line endings are reported and can be preserved or normalized.

### Edit menu

| Action | Shortcut | Notes |
|---|---|---|
| Undo | `Ctrl+Z` | |
| Redo | `Ctrl+Y` | |
| Cut | `Ctrl+X` | Disabled when no selection |
| Copy | `Ctrl+C` | Disabled when no selection |
| Paste | `Ctrl+V` | Disabled when clipboard has no text |
| Select All | `Ctrl+A` | Disabled on empty document |
| Find… | `Ctrl+F` | Opens the Find & Replace panel |
| Go to Line… | `Ctrl+G` | Modal dialog; type a line number and press Enter or OK |
| Text / Convert Tabs to Spaces | | |
| Text / Convert Leading Spaces to Tabs | | |
| Text / Trim Trailing Spaces | | |
| Text / Remove Blank Lines | | |
| Text / Column Wrap… | | Wraps lines at a chosen column width |
| Text / Ensure Trailing Newline | | |
| Text / Duplicate Current Line | `Ctrl+D` | |
| Text / Remove Current Line | `Ctrl+Shift+K` | |

Menu items that are not applicable in the current context are shown greyed out.

### Settings / Preferences

| Setting | Options |
|---|---|
| Font | System monospace fonts; selected font shown with a checkmark |
| Size | 8–36 pt |
| Tab key | Tab character or spaces |
| Indent width | 2–8 (active only in spaces mode) |
| Dark mode | Toggle |
| Status bar | Character count or file size |

Settings are persisted per user via `Fl_Preferences`.

### Status bar

The status bar at the bottom of the window shows three segments from left to right:

- **Counter** — either character count (`8932 characters`, `11.32 thousand characters`) or file size (`892 B`, `1.32 KB`), depending on the preference. Also shows `[encrypted]` and `[unsaved changes]` flags.
- **Cursor position** — column and row (`C:4  R:12/340`).
- **Insert mode** — `INS` (insert) or `OVR` (overwrite), toggled with the Insert key.

### Drag and drop

Files can be dragged onto the editor window to open them. On Windows, Explorer drag via `WM_DROPFILES` is also handled.

## Project Layout

```text
docs/
  README.md          — this document
  txx-format.md      — binary format specification
external/             — downloaded compilation dependencies (gitignored)
  fltk/1_5/          — FLTK 1.5 sources and platform installations
  monocypher/        — crypto library
  ftxui/             — terminal UI library
lib/
  fltk/              — MinGW toolchain files
  lua/               — committed Lua runtime sources
  microz/            — modified DEFLATE library
src/
  core/              — C17 core library
  gui/
    main_fltk.cpp    — FLTK GUI (single file)
  tui/               — etui terminal UI (FTXUI)
  platform/          — OS-specific CSPRNG and secure memory
tests/
  core_tests.c       — core round-trip and error-path tests
  tui/
    edit_buffer_tests.cpp — etui document-model unit tests
packaging/
  linux/             — .desktop file
  windows/           — NSIS installer template and .rc template
assets/
  icons/             — PNG, .ico, .icns application icons
CMakeLists.txt
README.md            — build instructions
```

## Building

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

See the top-level `README.md` for cross-compilation and Windows installer instructions.

## Test Coverage

The `encrypted_core_tests` binary covers:

- Empty document round trip.
- Small uncompressed document round trip.
- Larger compressed document round trip.
- Non-ASCII UTF-8 round trip.
- Invalid UTF-8 rejection.
- Wrong password failure.
- Corrupted header failure.
- Corrupted ciphertext or tag failure.
- Truncated file failure.
- Unsupported future version failure.
- Compression threshold behavior.
- Secure random and secure-zero platform helper behavior where testable.

Manual GUI verification should cover:

- New file.
- Open encrypted file.
- Save.
- Save As.
- Import plaintext.
- Export plaintext.
- Dirty-state prompts.
- Password confirmation mismatch.
- Find and replace.
- Reopen saved file.

Build verification should include:

- Linux debug build with tests.
- Linux release build with stripped executable size check.
- Windows build with MSVC or MinGW.
- macOS build with Clang/CMake.
- Sanitizer build for core tests where supported.

## Terminal UI (etui)

`etui` is a terminal front-end built on [FTXUI](https://github.com/ArthurSonzogni/FTXUI)
(C++17, MIT), reaching feature parity with the FLTK GUI minus inherently graphical
settings (font face/size). It reuses `encrypted_core`, `text_transform.cpp`, and `utf8.c`
unchanged, shares the restricted Lua plugin runtime and user plugin folder, ships
in the same installer/bundle as the GUI, and lives under `src/tui/`:

| File | Responsibility |
|---|---|
| `main_tui.cpp` | App entry, `ScreenInteractive` loop, menu/command dispatch, dialogs, theming |
| `edit_buffer.h/.cpp` | Document model: storage, cursor, selection, undo/redo — no FTXUI dependency |
| `md_parse.h/.cpp`, `syn_parse.h/.cpp` | Markdown/syntax parsing, emitting styled runs for terminal rendering |
| `plugin_buffer.h`, `plugin_dialog.h` | Lua document adapter and modal terminal dialogs |
| `clipboard.h/.cpp` | OSC 52 + OS clipboard bridges |
| `config.h/.cpp` | Load/save preferences |

`EditBuffer` is UI-agnostic and unit-tested independently of FTXUI (`tests/tui/edit_buffer_tests.cpp`), mirroring how `text_transform` is tested without FLTK.

## Security Model

The basic version is intended to protect files at rest with password-based authenticated encryption.

Protected:

- Saved `.txx` file contents.
- Accidental file inspection without the password.
- Tampering with encrypted data, detected through AEAD authentication.

Not protected in the basic version:

- Live editor buffer in process memory.
- Clipboard contents.
- Swap files.
- Crash dumps.
- Screen capture.
- Keyloggers.
- A compromised operating system.

This security model is acceptable for the first version as long as it is documented clearly.
