<p align="center">
	<img src="docs/icon.svg" alt="EncryptEd icon" width="144" height="144">
</p>

# EncryptEd

EncryptEd is a small encrypted text editor with a FLTK GUI and a C17 core for UTF-8 text files stored in a compressed and encrypted `.txx` format.

## Build

Release builds use `-O2 -DNDEBUG` with GCC/Clang, including dependencies built
from source. MSVC uses its standard `/O2` Release optimization.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Linux Desktop Install

`install.sh` installs the built editors into the XDG/freedesktop locations that
every Linux desktop uses: `<prefix>/bin` for the binaries,
`<prefix>/share/applications` for the desktop entry, and
`<prefix>/share/icons/hicolor` for the icons. It refreshes the desktop-entry and
icon caches afterwards, so the menu entry appears without a re-login.

```sh
./install.sh                  # per-user install to ~/.local (no sudo)
./install.sh --system         # system-wide install to /usr/local (sudo)
./install.sh --stage          # stage the tree under build/install-root/ instead
./install.sh --uninstall      # remove a previous install
```

`etui` is installed when it has been built; add `--with-cli` to also install the
`encrypted_cli` prototype, and `--build` to build first if needed. Run
`./install.sh --help` for all options, including `--prefix DIR` and `--stage=DIR`.
A prior `./build.sh` run is required unless `--build` is given.

The same result without the helper script:

```sh
cmake --install build --prefix ~/.local        # per-user
sudo cmake --install build --prefix /usr/local # system-wide
```

The menu entry runs `encryptEd` from `PATH`, so `<prefix>/bin` must be on `PATH`
(system installs are; add `~/.local/bin` for per-user installs). Uninstalling
keeps `~/.local/share/encryptEd/plugins`, where your own Lua plugins live.

## Windows Installer

After configuring a Windows-target build directory that produces the `encryptEd` GUI target, build the NSIS installer with:

```sh
cmake --build <build-dir> --target package_windows_installer
```

Requirements:

- NSIS 3.x with `makensis` available on the packaging host.
- A Windows build where the `encryptEd` GUI target is enabled.

The installer stages the GUI, etui (when enabled), and bundled default plugins into `<build-dir>/windows-installer/` and emits `encryptEd-<version>-windows-setup.exe`. It defaults to a per-user install under `%LocalAppData%\Programs\EncryptEd`, offers an all-users option in the same setup, creates a Start Menu shortcut, and associates `.txx` files with EncryptEd.

Because the same installer can switch to an all-users install, Windows may request elevation when an administrator-capable account launches setup. The CLI prototype is not included in this installer.

## CLI Prototype

```sh
build/encrypted_cli pack input.txt output.txx
build/encrypted_cli unpack output.txx restored.txt

printf '%s\n' "$PASSPHRASE" | build/encrypted_cli pack input.txt output.txx --password-stdin
```

The CLI exists to exercise the core format alongside the desktop editor. By default it prompts for the passphrase without echoing it; use `--password-stdin` for scripts.

## Terminal Editor

`etui` opens standard input as a document whenever stdin is not a terminal, so it
works with pipes and here-documents:

```sh
cat notes.txt | etui
etui - < draft.md
git show HEAD:README.md | etui
```

The keyboard keeps coming from the terminal. The document has no path, so **Save**
opens the Save As dialog; a path argument takes precedence over stdin.

Long lines scroll horizontally instead of wrapping: the view follows the cursor,
**Home**/**End** jump to either end of the line, and horizontal wheel events shift
it a few columns. A position bar appears in the status line while a visible line is
wider than the window.

Colours come from a theme, chosen in **Settings → Theme**: terminal default,
Dracula, Nord, Gruvbox Dark and Light, Solarized Dark and Light, Tokyo Night,
Catppuccin Mocha and Latte, One Dark, and Kanagawa. Named themes paint the whole
interface — menu bar, status line, selections, cursor and screen background — in
24-bit colour, which FTXUI downgrades to the terminal's own palette when it does
not support truecolor. The choice is stored as `theme=<name>` in the config file;
**Terminal** keeps the terminal's colours and leaves the background alone.

## Text Plugins

The GUI and `etui` support restricted Lua plugins for editing the active document and asking
for input. Put `.lua` scripts in **Plugins → Open Plugins Folder**, then choose
**Reload Plugins**. Subdirectories become nested menus. Plugins have no file,
network, or process access, and each successful run is one undoable text change.
See [the plugin guide](docs/plugins.md) for the API and installation paths, and
[examples/plugins](examples/plugins) for example commands.

## Dependencies

Downloaded compilation dependencies belong in the gitignored `external/` directory.
Repository-owned Lua and modified microz sources remain in `lib/`; MinGW toolchain
files remain in `lib/fltk/`.

- `lib/lua`: Lua 5.5.1, committed with its MIT license and archive checksum.
  The plugin runtime statically links selected libraries and exposes a restricted API.
- `lib/microz`: stripped miniz-derived raw DEFLATE support, kept as the compression layer.
  Committed to this repo — it's a modified derivative (ported to C17, archive APIs
  stripped), not a redistributed vendor copy, released under microz's original MIT
  license.
- `external/sources/monocypher-4.0.3`: Monocypher 4.0.3 for Argon2id and
  XChaCha20-Poly1305. **Not** committed — download it from
  https://monocypher.org/download/ and extract it there; CMake uses its `src/`
  directory. CMake fails with instructions if it's missing.
- FLTK 1.5 (GUI only): **not** committed — build it as a static library and place it at
  `external/fltk/1_5/fltk-linux/` (or `fltk-win64/`/`fltk-win32/` for Windows cross-builds); see
  https://www.fltk.org/software.php. Without it, CMake skips the `encryptEd` GUI target but
  still builds the core library, CLI, and tests.

- `external/sources/ftxui-7.0.3`: FTXUI 7.0.3 for the terminal editor. CMake downloads
  it here when needed, or uses an existing source checkout. Each build keeps separate
  generated FTXUI artifacts in its build directory.

## Navigation

Use **Ctrl+F** to find text, **F3** for the next match, and **F2** for the previous
match. Searches wrap at either end of the document. **Ctrl+R** or Edit's
**Find / Replace** command opens search and replace with mouse-accessible controls.
In etui, searches are literal and case-sensitive; an empty replacement deletes
matches, and Replace All is one undo action (Alt+Z; redo Ctrl+Y/Alt+Y).

The status counter supports **Character count**, **Byte count**, and **Readable size**.
Choose the mode in Settings (GUI: Preferences). Readable size shows UTF-8 document
bytes as `123.5 KB`, using decimal units (1 KB = 1,000 bytes), rather than the
compressed or encrypted file size on disk. The preference is saved.

The vertical scrollbar appears when a document exceeds the editor height. Toggle it in
**Settings → Vertical Scrollbar** in etui or **Settings → Preferences → Vertical scrollbar**
in the GUI. The preference is saved; etui supports clicking or dragging the track.

Terminal dialogs support mouse clicks as well as keyboard navigation. The file picker
provides **Open/Save**, **Parent**, and **Cancel** buttons; Tab cycles through its controls.
Long About and plugin messages provide **Scroll Up/Down** buttons and mouse-wheel scrolling,
with Page Up/Page Down retained. Click text fields to place the cursor.

## License

GPLv3 — see [LICENSE](LICENSE). Vendored dependencies keep their own licenses (Monocypher:
BSD-2-clause/CC0 dual license; microz/miniz: MIT; FLTK: LGPL with the static-linking
exception).
