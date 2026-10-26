# Changelog

## 1.3 — 2026-10-26

### Editor improvements

- Added GUI syntax highlighting with automatic file-type detection, Markdown
  highlighting, and support for the languages available in `etui`. Incremental
  rescanning keeps multiline comments and strings correct while editing.
- Added `etui` themes, JSON highlighting, horizontal scrolling for long lines,
  and support for editing text from standard input.
- Added an XDG desktop installer for Linux.

### Security and file handling

- Added **File → Lock Now** and optional inactivity locking in the GUI. Locking
  wipes the open document and reopens it after unlock.
- Improved cleanup of plaintext, password, clipboard, and stored undo/redo data.
  FLTK's internal undo buffers remain a documented residual. GUI document
  buffers and GUI/`etui` password buffers use best-effort memory locking, which
  platform limits can prevent.
- Added GUI clipboard controls, Windows clipboard sensitivity markers, and
  Windows screen-capture exclusion while a document is open.
- Hardened Windows process settings and executable search paths, disabled core
  dumps, and strengthened Linux binary hardening.
- Enforced the minimum Argon2 parameters when decoding and preserved existing
  file permission bits when saving.
- Restricted external URL launching to supported schemes.

### Build and maintenance

- Updated vendored dependencies and build instructions.
- Added tests for syntax rescanning, GUI buffer wiping, terminal themes, stdin
  editing, and save permissions.
