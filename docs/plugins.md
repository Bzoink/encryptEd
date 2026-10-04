# Lua plugins

The GUI's **Plugins** menu runs `.lua` files against the active document.
Each command runs only when selected. Existing Edit → Text commands remain available.
The terminal editor does not load plugins yet.

## Install and organize

Choose **Plugins → Open Plugins Folder**, then place scripts in that folder:

| Platform | Directory |
| --- | --- |
| Linux | `$XDG_CONFIG_HOME/encryptEd/plugins`, falling back to `~/.config/encryptEd/plugins` |
| Windows | `%APPDATA%\encryptEd\plugins` |
| macOS | `~/Library/Application Support/encryptEd/plugins` |

Choose **Reload Plugins** after adding, deleting, or moving files. Edits to existing
scripts take effect on the next invocation. Restarting also refreshes the menu.
Folders become nested submenus, sorted by name with folders before commands.
Filenames without `.lua` become command labels. Discovery does not execute scripts.
Symlinks are skipped. Discovery errors can be viewed from the Plugins menu.

For example, `Whitespace/Trim trailing spaces.lua` appears as
**Plugins → Whitespace → Trim trailing spaces**. One script is one command;
there is no manifest or registration function.

Copy the examples from `examples/plugins` in the source tree, or from
`share/encryptEd/plugin-examples` in an installed package, into your plugin folder.
Examples are distributed separately and never overwrite your scripts.

## First plugin

Save this as `Insert greeting.lua`:

```lua
local name = ui.input('Name:', 'world')
editor.replace_selection('Hello, ' .. name .. '!')
```

Selecting the command asks for a name and replaces the selection, or inserts at
the cursor when nothing is selected. Canceling the dialog discards the whole run.

## Text and positions

Scripts use Lua 5.4 and `editor.api_version == 1`. Call API functions with a dot,
as in `editor.text()`, not a colon. There is no persistent state between runs.

All editing positions are **1-based UTF-8 byte positions**, including the cursor.
A range includes its start and excludes its end. `#editor.text() + 1` is the
position after the final byte. Thus `editor.range(1, 4)` reads three ASCII bytes.
Positions must lie at Unicode character boundaries: they cannot split UTF-8 sequences.
Use the conversion functions below when working in line/character coordinates.
Columns count Unicode code points, not screen cells or grapheme clusters; a tab is
one code point. Line numbers and columns are 1-based.

Inserted text must be valid UTF-8 without NUL bytes. CRLF and CR are normalized to
LF in the buffer. The editor retains its existing line-ending preferences for saving.
Lua strings and patterns remain byte-oriented; `string.upper` and `string.lower`
are not Unicode case conversion. The `utf8` library is available for code-point work.

### Editing API

| Function | Behavior |
| --- | --- |
| `editor.text()` | Return the complete working document. |
| `editor.set_text(text)` | Replace the complete document. |
| `editor.range(first, last)` | Read a range, with an exclusive end. |
| `editor.replace(first, last, text)` | Replace a range. |
| `editor.insert(position, text)` | Insert text at a position. |
| `editor.delete(first, last)` | Delete a range. |
| `editor.line_count()` | Return line count; an empty document has one line. A final LF adds an empty last line. |
| `editor.line(number)` | Return a line without its LF. |
| `editor.position(line, column)` | Convert a line/code-point column to a byte position. The column after the last character is valid. |
| `editor.line_column(position)` | Return line and code-point column as two values. |
| `editor.cursor()` | Return cursor byte position. |
| `editor.set_cursor(position)` | Set cursor; does not change selection. |
| `editor.selection()` | Return `first, last`, or `nil` if nothing is selected. |
| `editor.set_selection(first, last)` | Set selection independently of cursor. Pass `nil` to clear it. |
| `editor.selected_text()` | Return selected text, or an empty string. |
| `editor.replace_selection(text)` | Replace selection, or insert at cursor if none exists. |
| `editor.indentation()` | Return width and a boolean indicating spaces rather than tabs. |
| `editor.cancel()` | Abort this run and discard edits. |

Reads see earlier edits made during the same run. Every replacement shifts cursor
and selection positions after the edited range by the byte-length difference.
Positions within the range, including either endpoint, move to the replacement's
end. Set the cursor or selection explicitly afterward when a different result is
desired. Whole-document replacement therefore moves these positions to the end.

Edits are staged privately. Successful text changes commit as one undo action.
A no-op does not mark the document modified or add an undo action. A successful
cursor-only or selection-only command updates those positions without changing text.
Failure or cancellation leaves the original text, cursor, selection, and undo
history untouched. Undo/redo restores text using the editor's normal behavior;
selection changes are not separate undo actions.
Use **Edit → Undo** / **Edit → Redo** (Ctrl+Z / Ctrl+Shift+Z) to move through
plugin changes and ordinary edits. Plugin operations remain separate from
typing immediately before or after them.

### Dialog API

| Function | Result |
| --- | --- |
| `ui.input(prompt, initial)` | Entered string; `initial` is optional. An empty accepted string is valid. |
| `ui.choose(prompt, choices)` | 1-based index into a nonempty array of strings. |
| `ui.confirm(prompt)` | `true` for Yes, `false` for No. |
| `ui.message(text)` | Show a message; no return value. |

Each dialog is titled with the plugin name. Cancel, Escape, or closing a dialog
aborts the entire run, including previously staged edits. “No” is a normal
confirmation response. The document and file actions are disabled during execution.
Dialogs already shown cannot be undone.

## Restricted runtime

The global environment contains `editor`, `ui`, `_G`, `_VERSION`, and these basic
functions: `assert`, `error`, `ipairs`, `pairs`, `next`, `select`, `tonumber`,
`tostring`, `type`, and `rawequal`. It also contains `string` (without `dump`),
`table`, `math`, and `utf8`.

There is no filesystem, network, process, clipboard, password, native-module, or
application-internal API. `io`, `os`, `package`, `require`, `debug`, `load`,
`loadfile`, `dofile`, console output, metatable access, coroutine functions,
`pcall`, and `xpcall` are unavailable. Only text source is loaded, never bytecode.
The host reads plugin files; scripts cannot read or write files themselves.

Limits per run:

- 64 MiB of Lua allocations, including script data and Lua string copies.
- 16 MiB for the input and resulting document, checked separately from Lua memory.
- 1 MiB of script source.
- 10 million Lua instructions, checked in batches of 1,000.
- 100 dialogs; 64 KiB per prompt, initial value, or input response; at most 256
  choices totaling 64 KiB.

Exceeding a limit fails the run without committing edits. The instruction counter
does not measure time spent in native string operations or dialogs, so it is not a
hard wall-clock timeout. This is an embedded language sandbox, not OS process
isolation. Runtime-owned allocations and document copies are wiped when released;
FLTK widget storage can only be cleared on a best-effort basis.

## Development

The runtime and discovery library has no GUI dependency. `DialogHost` permits
mock dialogs in tests and a future terminal adapter. Add a script, choose Reload
Plugins, and invoke it; errors identify the plugin and include Lua's source line
where available. Errors are displayed in the editor and are not written to a log.

Run the automated checks with:

```sh
cmake -S . -B build -DENCRYPTED_BUILD_TUI=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

`plugin_tests` checks execution, restrictions, failure rollback, discovery, and
the shipped examples. When FLTK is available, `plugin_buffer_tests` checks actual
buffer undo/redo and no-op behavior without requiring a display.

For the optional Linux GUI regression (requires Xvfb and xdotool):

```sh
xvfb-run -a python3 tests/plugin_gui_smoke.py build/encryptEd
```
