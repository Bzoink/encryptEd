"""Regular-expression find/replace through a real terminal."""
import pathlib
import sys
import tempfile
sys.dont_write_bytecode = True
from mouse_smoke import MouseTerminal


def main():
    executable = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='etui-regex-') as temporary:
        root = pathlib.Path(temporary)
        document = root / 'regex.txt'
        original = 'John Smith\nJane Doe\nabab ab'
        document.write_text(original)
        terminal = MouseTerminal(executable, document, root / 'config')
        try:
            terminal.send(b'\x12')  # Ctrl+R opens Find / Replace
            terminal.locate('Find / Replace')
            terminal.locate('Literal text search')           # plain text is the default
            terminal.send('(\\w+) (\\w+)')
            terminal.click('Replace with:', offset=14)
            terminal.send('\\2, \\1')
            terminal.click('│Replace All│')
            terminal.locate('Not found')                     # literal: no such text
            terminal.click('Regular expression')
            terminal.locate('Regex:')
            terminal.click('│Replace All│')
            terminal.locate('Replaced 3 occurrences')
            terminal.click('│Close│')
            replaced = 'Smith, John\nDoe, Jane\nab, abab'
            terminal.save(document, replaced)
            terminal.send('\x1bz')
            terminal.save(document, original)                # a single undo step
            terminal.send(b'\x19')

            # Line anchors apply the empty match on every line.
            terminal.send(b'\x12')
            terminal.click('Find:', offset=14)
            terminal.send('\x1b[F' + '\x7f' * 12 + '^')
            terminal.click('Replace with:', offset=14)
            terminal.send('\x1b[F' + '\x7f' * 12 + '> ')
            terminal.click('│Replace All│')
            terminal.locate('Replaced 3 occurrences')
            terminal.click('│Close│')
            terminal.save(document, '> ' + replaced.replace('\n', '\n> '))

            # An invalid pattern is reported without changing the text or echoing it.
            terminal.send(b'\x12')
            terminal.click('Find:', offset=14)
            terminal.send('\x1b[F\x7f(')
            terminal.click('│Replace All│')
            terminal.locate("Missing ')'")
            terminal.click('│Close│')
            terminal.save(document, '> ' + replaced.replace('\n', '\n> '))
        finally:
            terminal.close()
    print('Regular-expression search smoke checks passed')


if __name__ == '__main__': main()
