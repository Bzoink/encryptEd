"""Optional X11 GUI regression: xvfb-run -a python3 tests/plugin_gui_smoke.py build/encryptEd."""

import os
import pathlib
import subprocess
import sys
import tempfile
import time


def xd(*args):
    return subprocess.check_output(["xdotool", *map(str, args)], text=True).strip()


with tempfile.TemporaryDirectory(prefix="encrypted-plugin-gui-") as directory:
    root = pathlib.Path(directory)
    plugins = root / "data/encryptEd/plugins"
    (plugins / "Nested/Deep").mkdir(parents=True)
    (plugins / "Nested/Deep/Trim.lua").write_text(
        "editor.set_text(editor.text():gsub('[ \\t]+\\n', '\\n'):gsub('[ \\t]+$', ''))"
    )
    (plugins / "Prompt.lua").write_text("editor.insert(1, ui.input('Prefix:', 'hello '))")
    (plugins / "Cancel.lua").write_text("editor.set_text('discarded'); ui.input('Cancel this:')")
    document = root / "document.txt"
    document.write_text("original  \nsecond  ")
    env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"))
    app = subprocess.Popen([str(pathlib.Path(sys.argv[1]).resolve()), str(document)], env=env)
    try:
        time.sleep(1)
        assert app.poll() is None, "Editor exited during startup"
        window = xd("search", "--onlyvisible", "--pid", app.pid).splitlines()[0]
        xd("windowfocus", window)

        def key(*keys):
            xd("key", "--clearmodifiers", *keys)
            time.sleep(0.15)

        def menu(y):
            xd("mousemove", "--window", window, 115, 12, "click", 1)
            time.sleep(0.15)
            xd("mousemove", "--window", window, 120, y)
            time.sleep(0.2)

        def save(expected):
            key("ctrl+s")
            assert document.read_text() == expected, repr(document.read_text())

        menu(110)
        xd("mousemove", "--window", window, 265, 110)
        time.sleep(0.3)
        key("Right", "Return")
        assert "•" in xd("getwindowname", window)
        save("original\nsecond")
        # Running the same transform again must not dirty the document or add undo.
        menu(110)
        xd("mousemove", "--window", window, 265, 110)
        time.sleep(0.3)
        key("Right", "Return")
        assert "•" not in xd("getwindowname", window)
        key("ctrl+z")
        save("original  \nsecond  ")

        menu(134)
        xd("click", 1)
        time.sleep(0.2)
        assert xd("search", "--onlyvisible", "--name", "^Plugin: Cancel$")
        key("Escape")
        assert "•" not in xd("getwindowname", window)
        save("original  \nsecond  ")

        menu(158)
        xd("click", 1)
        time.sleep(0.2)
        assert xd("search", "--onlyvisible", "--name", "^Plugin: Prompt$")
        key("ctrl+a")
        xd("type", "--clearmodifiers", "prefix: ")
        key("Return")
        save("prefix: original  \nsecond  ")

        # Refresh the menu after adding a new command.
        (plugins / "New.lua").write_text("editor.set_text('reloaded')")
        menu(62)
        xd("click", 1)
        time.sleep(0.2)
        menu(158)
        xd("click", 1)
        save("reloaded")
        # Deleting all text followed by a plugin insertion previously coalesced
        # into a single FLTK action. Both edits must remain separately undoable.
        key("ctrl+a", "BackSpace")
        menu(182)
        xd("click", 1)
        time.sleep(0.2)
        key("ctrl+a")
        xd("type", "--clearmodifiers", "entry")
        key("Return")
        save("entry")
        key("ctrl+z")
        save("")
        key("ctrl+z")
        save("reloaded")
        key("ctrl+shift+z")
        save("")
        key("ctrl+shift+z")
        save("entry")
        # Install bundled defaults, preserve custom scripts, and repeat safely.
        prompt_before = (plugins / "Prompt.lua").read_text()
        menu(38)
        xd("click", 1)
        time.sleep(0.3)
        key("Return")
        defaults = ["Demo/Text Playground.lua", "Text/Change case.lua",
                    "Lines/Prefix lines.lua", "Whitespace/Trim trailing spaces.lua",
                    "Text/Add prefix and suffix.lua", "HTML/Compact HTML.lua",
                    "HTML/Escape HTML.lua", "HTML/Wrap in tag.lua",
                    "Lines/Sort lines.lua", "Lines/Remove duplicate lines.lua",
                    "Code/Comment lines.lua", "Code/Convert identifier style.lua"]
        for relative in defaults:
            assert (plugins / relative).is_file(), relative
        assert (plugins / "Prompt.lua").read_text() == prompt_before
        modified = plugins / "Text/Change case.lua"
        modified.write_text("-- user modification\nreturn")
        menu(38)
        xd("click", 1)
        time.sleep(0.3)
        key("Return")
        assert modified.read_text() == "-- user modification\nreturn"
        assert "•" not in xd("getwindowname", window)
        key("ctrl+q")
        assert app.wait(timeout=5) == 0
        print("GUI plugin tree, input, cancellation, reload, dirty state, and undo passed.")
    finally:
        if app.poll() is None:
            app.terminate()
        app.wait(timeout=5)
