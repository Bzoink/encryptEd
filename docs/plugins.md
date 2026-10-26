# Lua plugins

Both the GUI and `etui` terminal editor have a **Plugins** menu that runs `.lua`
files against the active document. Each command runs only when selected. Existing
Edit → Text commands remain available. Both editors share the same user plugin folder
and bundled defaults. On Linux, put both your own scripts and installed defaults
in `~/.local/share/encryptEd/plugins/` (or the configured XDG data location).

## Install and organize

Choose **Plugins → Open Plugins Folder** in the GUI, or **Plugins → Show Plugins Folder**
in `etui`, then place scripts in that folder:

| Platform | Directory |
| --- | --- |
| Linux | `$XDG_DATA_HOME/encryptEd/plugins`, falling back to `~/.local/share/encryptEd/plugins` |
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

Choose **Plugins → Install Default Plugins** to install the bundled collection in
one click. It includes twelve ready-made commands for HTML, coding, lines, and text, plus
Demo → Text Playground (interactive transformation and dialog examples).
Subfolders are preserved and the menu refreshes automatically.

Installation adds missing scripts only: existing files, including edited defaults,
are always kept. The result dialog lists kept files and any errors. Run installation
again to add newly shipped defaults, or delete an unwanted edited default before
reinstalling to restore its bundled version. Symlinks and directory conflicts are
rejected. Installation never runs scripts or changes the document.

Bundled defaults live separately in `default-plugins/` and are copied into your
active `plugins/` folder by Install Default Plugins. Defaults ship with the
application and alongside development build executables;
macOS app bundles contain their own copy. If the collection is missing, reinstall
the application with its plugin examples.

You can also copy the examples from `examples/plugins` in the source tree, or from
`share/encryptEd/default-plugins` in an installed package, into your plugin folder.
Examples are distributed separately and never overwrite your scripts.

## Local Linux installation

For the terminal ZIP bundle, install the executable and its separate default collection:

```sh
unzip etui-linux-plugins.zip -d /tmp/etui-install
mkdir -p ~/.local/bin ~/.local/share/encryptEd
install -m 755 /tmp/etui-install/etui ~/.local/bin/etui
cp -r /tmp/etui-install/default-plugins ~/.local/share/encryptEd/
```

Launch `etui` and choose **Plugins → Install Default Plugins**. Your active scripts
then live in `~/.local/share/encryptEd/plugins/`, where you can also add your own
`.lua` files and subfolders. Choose Reload Plugins after adding files.

Older versions used `~/.config/encryptEd/plugins/`. Copy any scripts you kept there
to the new active folder, keeping existing destination files. Settings still use
the configuration directory; scripts use the data directory.

## Default collection

Commands use selected text, or the whole document when nothing is selected.
Line commands expand selections to complete lines, excluding the next line when
selection ends at its start. They preserve a final newline without creating an
extra line. New commands select their result. Changes are one undo action;
canceling any dialog discards the run.

| Command | Behavior |
| --- | --- |
| Text → Add prefix and suffix | Choose each line or the exact block, then enter literal, single-line prefix and suffix strings. Blank lines are included; `%` is literal. |
| HTML → Compact HTML | Remove whitespace-only gaps between tags, including inline tags. Confirm first: `<span>Hello</span> <span>world</span>` becomes `<span>Hello</span><span>world</span>` and may render as joined words. |
| HTML → Escape HTML | Choose text or attribute-value escaping. Escape `&`, `<`, `>`; attribute mode also escapes both quote characters. Existing entities are escaped again (`&amp;` → `&amp;amp;`). |
| HTML → Wrap in tag | Enter a tag name, such as `div` or `my-tag`. Wrap the exact scope without added whitespace. Attributes and void tags such as `img` are rejected. |
| Lines → Sort lines | Ascending or descending, case-sensitive byte ordering. Includes blank lines and retains duplicates; this is not locale-aware sorting. |
| Lines → Remove duplicate lines | Keep the first occurrence of each exact line, including blank lines; report the number removed. |
| Code → Comment lines | Add or remove a literal prefix after indentation, default `//`. Add one separating space; removal strips at most one space. Whitespace-only lines stay unchanged. |
| Code → Convert identifier style | Convert one ASCII name per line to snake_case, camelCase, or PascalCase. Split acronym and camel-case boundaries, spaces, underscores, and hyphens: `HTTPServer` → `http_server`. Preserve indentation and blank lines; reject punctuation, non-ASCII names, and leading digits. This does not rename symbols throughout source code. |
| Text → Change case | ASCII uppercase/lowercase for the exact selection or document. |
| Lines → Prefix lines | Add a literal prefix to complete lines. |
| Whitespace → Trim trailing spaces | Remove trailing spaces and tabs in the chosen scope. |
| Demo → Text Playground | Interactive transformations, prefix/suffix input, confirmation, statistics, and cursor/selection control. |

Compact HTML uses a quote-aware scan of the complete document, so selections
inside `pre`, `textarea`, `script`, and `style` remain protected. Comments, attribute
values, and ordinary text are preserved. Only eligible whitespace gaps entirely
inside the selected scope are removed. Unterminated tags, quotes, comments, or
protected elements cause an error without edits. This is whitespace processing,
not a complete HTML parser or minifier.

## First plugin

Save this as `Insert greeting.lua`:

```lua
local name = ui.input('Name:', 'world')
editor.replace_selection('Hello, ' .. name .. '!')
```

Selecting the command asks for a name and replaces the selection, or inserts at
the cursor when nothing is selected. Canceling the dialog discards the whole run.

## Terminal controls

In `etui`, press **Alt+P** to open Plugins, or open the menu bar with **F9** and
navigate with the arrow keys. Folder submenus list commands; folders deeper than
one submenu level open a choice dialog with **.. (Back)** navigation.

Plugin dialogs support keyboard and mouse input. Use arrows to select a choice,
**Tab** to move between fields and buttons, and **Enter** to accept. Use
**Page Up/Page Down** to scroll long prompts or messages. Inputs are
single-line and support normal cursor editing. Confirmation dialogs initially
focus **No**; accepting No returns `false`, while **Escape** cancels the whole run.
Escape also cancels message dialogs. While dialogs are open, the editor is suspended.
**Alt+Z** or **Edit → Undo** undoes a plugin change; **Ctrl+Y** or **Alt+Y** redoes it.
In etui, Ctrl+Z is also an undo shortcut. Position-only commands and no-op
transformations do not mark the document modified or add undo steps.

## Text and positions

Scripts use Lua 5.5 and `editor.api_version == 1`. Call API functions with a dot,
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
In the GUI, use **Edit → Undo** / **Edit → Redo** (Ctrl+Z / Ctrl+Shift+Z) to move through
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
- 10 million native work units, covering pattern matching, literal searches,
  string transformations, table operations, and UTF-8 scans. Work is charged
  for processed bytes/elements and matching attempts, including backtracking.
- Five seconds of execution time, checked at Lua instruction, native work,
  and editor API boundaries. Time spent waiting for plugin dialogs is excluded.
- 100 dialogs; 64 KiB per prompt, initial value, or input response; at most 256
  choices totaling 64 KiB.

Exceeding a limit fails the run without committing edits. Native work checks make
expensive pattern searches and large table moves interruptible, even when no Lua
instructions execute inside them. The time limit is cooperative: a bounded
operation already in progress can finish before the next check. The runtime uses
an embedded language sandbox without OS process isolation. Runtime-owned
allocations and document copies are wiped when released;
FLTK widget storage can only be cleared on a best-effort basis.

## Development

The runtime and discovery library has no GUI dependency. `DialogHost` permits
mock dialogs in tests and the terminal adapter. Add a script, choose Reload
Plugins, and invoke it; errors identify the plugin and include Lua's source line
where available. Errors are displayed in the editor and are not written to a log.

Run the automated checks with:

```sh
cmake -S . -B build -DENCRYPTED_BUILD_TUI=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

`plugin_tests` checks execution, restrictions, failure rollback, discovery, and
the shipped examples, including native-operation exhaustion and dialog timing.
The bundled Lua contains local work-budget hooks; see `lib/lua/ENCRYPTED.md`
before updating it. When FLTK is available, `plugin_buffer_tests` checks actual
buffer undo/redo and no-op behavior without requiring a display.

For the optional Linux GUI regression (requires Xvfb and xdotool):

```sh
xvfb-run -a python3 tests/plugin_gui_smoke.py build/encryptEd
```
