"""Search navigation and replacement through a real terminal, including undo."""
import pathlib
import sys
import tempfile
sys.dont_write_bytecode = True
from mouse_smoke import MouseTerminal


def main():
    executable = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='etui-search-') as temporary:
        root = pathlib.Path(temporary)
        document = root / 'search.txt'
        original = 'é猫\n猫猫\nend 猫'
        document.write_text(original)
        terminal = MouseTerminal(executable, document, root / 'config')
        try:
            terminal.send(b'\x06')
            terminal.send('猫')
            terminal.click('│OK│')
            terminal.locate('Ln 1, Col 2')
            for position in ['Ln 2, Col 1', 'Ln 2, Col 2', 'Ln 3, Col 5', 'Ln 1, Col 2']:
                terminal.send('\x1bOR')  # F3
                terminal.locate(position)
            terminal.locate('(wrapped)')
            terminal.send('\x1bOQ')  # F2 wraps backwards
            terminal.locate('Ln 3, Col 5')
            terminal.send('\x1b[12~')  # alternate F2 sequence
            terminal.locate('Ln 2, Col 2')
            terminal.send('\x1b[13~')  # alternate F3 sequence
            terminal.locate('Ln 3, Col 5')
            terminal.save(document, original)

            terminal.menu('Edit', 'Find / Replace...')
            terminal.click('Replace with:', offset=14)
            terminal.send('猫X')
            terminal.click('│Replace│')
            terminal.click('│Close│')
            terminal.save(document, 'é猫\n猫猫\nend 猫X')
            terminal.send('\x1bz')
            terminal.save(document, original)
            terminal.send(b'\x19')
            terminal.save(document, 'é猫\n猫猫\nend 猫X')
            terminal.send('\x1bz')

            terminal.send(b'\x12')  # Ctrl+R
            terminal.locate('Find / Replace')
            terminal.click('│Replace All│')
            terminal.locate('Replaced 4 occurrences')
            terminal.click('│Close│')
            replaced = 'é猫X\n猫X猫X\nend 猫X'
            terminal.save(document, replaced)
            terminal.send('\x1bz')
            terminal.save(document, original)  # one undo for all occurrences
            terminal.send(b'\x19')
            terminal.save(document, replaced)

            terminal.send(b'\x12')
            terminal.click('Replace with:', offset=14)
            terminal.send('\x1b[F\x7f\x7f')
            terminal.click('│Replace All│')
            terminal.click('│Close│')
            deleted = 'éX\nXX\nend X'
            terminal.save(document, deleted)

            terminal.send(b'\x12')
            terminal.click('Find:', offset=14)
            terminal.send('\x1b[F\x7fmissing')
            terminal.click('│Replace All│')
            terminal.locate('Not found: missing')
            terminal.click('Find:', offset=14)
            terminal.send('\x1b[F' + '\x7f' * 7)
            terminal.click('│Replace All│')
            terminal.locate('Enter search text')
            terminal.click('│Close│')
            terminal.save(document, deleted)
            terminal.send('\x1bz')
            terminal.save(document, replaced)  # no-op searches did not add undo actions
            terminal.send(b'\x19')

            terminal.send(b'\x01')  # unrelated selection must not be replaced
            terminal.send(b'\x12')
            terminal.send('X')
            terminal.click('│Replace│')  # select only, no text change
            terminal.locate('Found: X')
            terminal.click('│Close│')
            terminal.save(document, deleted)
            terminal.send(b'\x12')
            terminal.click('│Replace│')
            terminal.click('│Close│')
            terminal.save(document, 'é\nXX\nend X')
        finally:
            terminal.close()
        terminal = MouseTerminal(executable, document, root / 'config', rows=24)
        try:
            terminal.send(b'\x12')
            terminal.send('X')
            terminal.click('│Find Next│')
            terminal.locate('Ln 2, Col 1')
            terminal.send('\x1bOR')
            terminal.locate('Ln 2, Col 2')
            terminal.send('\x1bOQ')
            terminal.locate('Ln 2, Col 1')
            terminal.send('\x1b')
            terminal.save(document, 'é\nXX\nend X')
        finally:
            terminal.close()
    print('Search navigation and replacement smoke checks passed')


if __name__ == '__main__': main()
