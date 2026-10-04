<p align="center">
	<img src="docs/icon.svg" alt="EncryptEd icon" width="144" height="144">
</p>

# EncryptEd

EncryptEd is a small encrypted text editor with a FLTK GUI and a C17 core for UTF-8 text files stored in a compressed and encrypted `.txx` format.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Windows Installer

After configuring a Windows-target build directory that produces the `encryptEd` GUI target, build the NSIS installer with:

```sh
cmake --build <build-dir> --target package_windows_installer
```

Requirements:

- NSIS 3.x with `makensis` available on the packaging host.
- A Windows build where the `encryptEd` GUI target is enabled.

The installer stages only the GUI application into `<build-dir>/windows-installer/` and emits `encryptEd-<version>-windows-setup.exe`. It defaults to a per-user install under `%LocalAppData%\Programs\EncryptEd`, offers an all-users option in the same setup, creates a Start Menu shortcut, and associates `.txx` files with EncryptEd.

Because the same installer can switch to an all-users install, Windows may request elevation when an administrator-capable account launches setup. The CLI prototype is not included in this installer.

## CLI Prototype

```sh
build/encrypted_cli pack input.txt output.txx
build/encrypted_cli unpack output.txx restored.txt

printf '%s\n' "$PASSPHRASE" | build/encrypted_cli pack input.txt output.txx --password-stdin
```

The CLI exists to exercise the core format alongside the desktop editor. By default it prompts for the passphrase without echoing it; use `--password-stdin` for scripts.

## Text Plugins

The GUI supports restricted Lua plugins for editing the active document and asking
for input. Put `.lua` scripts in **Plugins → Open Plugins Folder**, then choose
**Reload Plugins**. Subdirectories become nested menus. Plugins have no file,
network, or process access, and each successful run is one undoable text change.
See [the plugin guide](docs/plugins.md) for the API and installation paths, and
[examples/plugins](examples/plugins) for example commands.

## Vendored Dependencies

- `lib/lua`: Lua 5.4.9, committed with its MIT license and archive checksum.
  The plugin runtime statically links selected libraries and exposes a restricted API.
- `lib/microz`: stripped miniz-derived raw DEFLATE support, kept as the compression layer.
  Committed to this repo — it's a modified derivative (ported to C17, archive APIs
  stripped), not a redistributed vendor copy, released under microz's original MIT
  license.
- `lib/monocypher`: Monocypher 4.0.2 for Argon2id and XChaCha20-Poly1305. **Not** committed
  — download it from https://monocypher.org/download/ and place `monocypher.c` and
  `monocypher.h` in `lib/monocypher/`. CMake fails with instructions if it's missing.
- FLTK 1.5 (GUI only): **not** committed — build it as a static library and place it at
  `lib/fltk/1_5/fltk-linux/` (or `fltk-win64/`/`fltk-win32/` for Windows cross-builds); see
  https://www.fltk.org/software.php. Without it, CMake skips the `encryptEd` GUI target but
  still builds the core library, CLI, and tests.

## License

GPLv3 — see [LICENSE](LICENSE). Vendored dependencies keep their own licenses (Monocypher:
BSD-2-clause/CC0 dual license; microz/miniz: MIT; FLTK: LGPL with the static-linking
exception).
