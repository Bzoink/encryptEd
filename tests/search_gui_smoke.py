"""Optional X11 GUI regression: xvfb-run -a python3 tests/search_gui_smoke.py build/encryptEd

Drives the Find / Replace window: plain text by default, then the
"Regular expression" checkbox with capture groups, and error reporting."""

import os
import pathlib
import subprocess
import sys
import tempfile
import time


def xd(*args):
    return subprocess.check_output(["xdotool", *map(str, args)], text=True).strip()


with tempfile.TemporaryDirectory(prefix="encrypted-search-gui-") as directory:
    root = pathlib.Path(directory)
    document = root / "document.txt"
    original = "John Smith\nJane Doe\nabab ab"
    document.write_text(original)
    env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
               XDG_STATE_HOME=str(root / "state"))
    app = subprocess.Popen([str(pathlib.Path(sys.argv[1]).resolve()), str(document)], env=env)
    try:
        time.sleep(1.5)
        assert app.poll() is None, "Editor exited during startup"
        main = xd("search", "--onlyvisible", "--pid", app.pid).splitlines()[0]
        xd("windowfocus", main)

        def key(*keys):
            xd("key", "--clearmodifiers", *keys)
            time.sleep(0.2)

        def save(expected):
            xd("windowfocus", main)
            key("ctrl+s")
            assert document.read_text() == expected, repr(document.read_text())

        key("ctrl+r")
        time.sleep(0.5)
        find = xd("search", "--onlyvisible", "--name", "^Find / Replace$").splitlines()[0]

        def click(x, y):
            xd("mousemove", "--window", find, x, y, "click", 1)
            time.sleep(0.2)

        # Shifted punctuation is sent as the physical key combination: neither
        # `xdotool type` nor keysym names reach FLTK for these on a bare Xvfb.
        symbols = {"(": "shift+9", ")": "shift+0", "^": "shift+6", ">": "shift+period",
                   " ": "space", ",": "comma", "\\": "backslash", "+": "shift+equal"}

        def type_into(x, y, text):
            click(x, y)
            xd("key", "--clearmodifiers", "ctrl+a")
            for ch in text:
                xd("key", "--clearmodifiers", symbols.get(ch, ch))
            time.sleep(0.2)

        FIND, REPLACE, REGEX, REPLACE_ALL = (200, 22), (200, 54), (160, 86), (245, 143)

        type_into(*FIND, "(\\w+) (\\w+)")
        type_into(*REPLACE, "\\2, \\1")
        click(*REPLACE_ALL)                                  # plain text: nothing matches
        save(original)

        click(*REGEX)                                        # switch to regular expressions
        click(*REPLACE_ALL)
        replaced = "Smith, John\nDoe, Jane\nab, abab"
        save(replaced)
        key("ctrl+z")
        save(original)                                       # Replace All is one undo step

        type_into(*FIND, "^")
        type_into(*REPLACE, "> ")
        click(*REPLACE_ALL)
        save("> John Smith\n> Jane Doe\n> abab ab")

        type_into(*FIND, "(")                                # invalid pattern: reported, text untouched
        click(*REPLACE_ALL)
        save("> John Smith\n> Jane Doe\n> abab ab")
        assert app.poll() is None
        print("GUI regular-expression find/replace passed.")
    finally:
        if app.poll() is None:
            app.kill()
