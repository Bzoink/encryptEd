"""Horizontal scrolling for long lines: End/Home, wheel, and the indicator."""
import pathlib
import sys
import tempfile
sys.dont_write_bytecode = True
from mouse_smoke import MouseTerminal


def main():
    executable = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='etui-hscroll-') as temporary:
        root = pathlib.Path(temporary)
        document = root / 'long.txt'
        document.write_text('START' + 'x' * 200 + 'END\nshort line\n')
        terminal = MouseTerminal(executable, document, root / 'config')
        try:
            def visible(text):
                return any(text in line for line in terminal.lines())

            terminal.locate('START')
            assert not visible('END'), 'long line should be clipped at first'
            assert visible('█') or visible('░'), 'horizontal indicator missing'

            terminal.send('\x1b[F')  # End: scroll to the end of the line
            terminal.locate('END')
            assert not visible('START'), 'view did not scroll right'

            terminal.send('\x1b[H')  # Home: scroll back to the start
            terminal.locate('START')
            assert not visible('END'), 'view did not scroll back'

            # Horizontal wheel scrolls the view by moving the cursor; enough
            # notches return the cursor (and the view) to the line start.
            terminal.send('\x1b[F')
            terminal.locate('END')
            terminal.send('\x1b[<66;10;2M' * 60)  # wheel left
            terminal.locate('START')

            terminal.send('\x1b[F')
            terminal.pump(0.3)
            assert not visible('short line'), 'short line should scroll out of view'
        finally:
            terminal.close()
    print('Horizontal scroll smoke checks passed')


if __name__ == '__main__':
    main()
