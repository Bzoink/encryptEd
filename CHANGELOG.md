# Changelog

## 1.4 — 2026-11-11

### Search

- Added **regular-expression** find and replace to both `encryptEd` and `etui`
  (TRE 0.9.0, unmodified, linear-time matching) behind a "Regular expression"
  checkbox; plain-text search stays the default. Replacements support `\1`–`\9`,
  `\0`, `\n`, `\t`. The GUI Find / Replace window gained a status line and `etui` gained a
  "Match case" option; Replace All is a single undo step in both editors. Both editors now share one
  search implementation (`src/search/`).

### Fixes and hardening

- Fixed a heap overflow in the GUI cursor-position status update that could crash
  the editor when undoing past the start of the history.
- Added a crash reporter that writes a small, private report (version, platform,
  signal or exception, module+offset stack trace) and records no document
  material; it is shown once on the next start.
- Plugin runs now report unexpected host-side failures as plugin errors and added
  hostile-script regression tests.
- The GUI Undo and Redo menu items are grayed out when unavailable.
- **HTML → Compact HTML** now collapses whitespace between tags to one space
  instead of removing it.

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
