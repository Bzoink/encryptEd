"""Colour themes: switching through Settings, palettes, and persistence.

COLORTERM is forced to truecolor so the expected RGB values can be asserted
exactly instead of the terminal-palette approximation FTXUI would otherwise use.
"""
import os
import pathlib
import re
import sys
import tempfile
sys.dont_write_bytecode = True
os.environ['COLORTERM'] = 'truecolor'

from mouse_smoke import MouseTerminal

BACKGROUND = re.compile(rb'\x1b\[48;2;(\d+);(\d+);(\d+)m')

# Config name -> screen background (RGB) declared in src/tui/theme.h.
PALETTES = {
    'dracula': (40, 42, 54),
    'nord': (46, 52, 64),
    'gruvbox-dark': (40, 40, 40),
    'gruvbox-light': (251, 241, 199),
    'solarized-dark': (0, 43, 54),
    'solarized-light': (253, 246, 227),
    'tokyo-night': (26, 27, 38),
    'catppuccin-mocha': (30, 30, 46),
    'catppuccin-latte': (239, 241, 245),
    'one-dark': (40, 44, 52),
    'kanagawa': (31, 31, 40),
}


def backgrounds(raw):
    return {tuple(int(v) for v in match) for match in BACKGROUND.findall(raw)}


def main():
    executable = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='etui-theme-') as temporary:
        root = pathlib.Path(temporary)
        document = root / 'data.json'
        document.write_text('{"name": "etui", "ok": true}\n')
        config = root / 'config'
        settings = config / 'encryptEd/config'

        # The terminal default paints no background of its own.
        terminal = MouseTerminal(executable, document, config)
        try:
            terminal.pump(1.0)
            assert not backgrounds(terminal.history), 'terminal theme must not paint a background'

            terminal.menu('Settings', 'Theme')
            terminal.click('Dracula')
            terminal.pump(0.5)
            assert 'theme=dracula' in settings.read_text(), 'theme choice was not saved'
            assert PALETTES['dracula'] in backgrounds(terminal.history[-6000:]), 'Dracula background missing'
        finally:
            terminal.close()

        # The choice survives a restart.
        terminal = MouseTerminal(executable, document, config)
        try:
            terminal.pump(1.0)
            assert PALETTES['dracula'] in backgrounds(terminal.history), 'theme did not persist across restarts'

            terminal.menu('Settings', 'Theme')
            terminal.click('Terminal')
            terminal.pump(0.5)
            assert 'theme=terminal' in settings.read_text(), 'switch back was not saved'
        finally:
            terminal.close()

        # Every named palette loads and paints its documented background.
        for name, expected in PALETTES.items():
            settings.write_text(f'theme={name}\n')
            terminal = MouseTerminal(executable, document, config)
            try:
                terminal.pump(0.8)
                assert expected in backgrounds(terminal.history), f'{name} background {expected} missing'
            finally:
                terminal.close()

        # ... and switching back to the terminal default really is colour-free.
        settings.write_text('theme=terminal\n')
        terminal = MouseTerminal(executable, document, config)
        try:
            terminal.pump(1.0)
            assert not backgrounds(terminal.history), 'terminal theme must not paint a background'
        finally:
            terminal.close()
    print('Theme smoke checks passed')


if __name__ == '__main__':
    main()
