"""Exercise terminal navigation using SGR mouse events, without extra packages."""
import pathlib
import re
import sys
import tempfile
import time
import unicodedata
sys.dont_write_bytecode = True
from plugin_smoke import Terminal


class MouseTerminal(Terminal):
    def __init__(self, *args, rows=40):
        self.rows = rows
        self.history = b''
        super().__init__(*args, rows=rows, relative_path=True)

    def pump(self, duration):
        previous = len(self.output)
        super().pump(duration)
        self.history += self.output[previous:]

    def lines(self):
        # Minimal VT screen reconstruction for locating rendered controls.
        grid = [[' '] * 120 for _ in range(self.rows)]
        x = y = 0
        saved = (0, 0)
        for token in re.findall(r'\x1b\[[0-?]*[ -/]*[@-~]|\x1b[P\]_^][^\x1b\x07]*(?:\x1b\\|\x07)?|\x1b.|[^\x1b]', self.history.decode(errors='replace')):
            if token.startswith('\x1b['):
                final = token[-1]
                params = token[2:-1]
                numbers = [int(n or 0) for n in params.split(';')] if re.fullmatch(r'[0-9;]*', params) else []
                first = (numbers[0] or 1) if numbers else 1
                if final in 'Hf':
                    y = first - 1
                    x = ((numbers[1] or 1) if len(numbers) > 1 else 1) - 1
                elif final == 'A': y -= first
                elif final in 'Be': y += first
                elif final in 'Ca': x += first
                elif final == 'D': x -= first
                elif final == 'E': y += first; x = 0
                elif final == 'F': y -= first; x = 0
                elif final == 'G': x = first - 1
                elif final == 'd': y = first - 1
                elif final == 'J' and numbers and numbers[0] == 2:
                    grid = [[' '] * 120 for _ in range(self.rows)]
                elif final == 'K':
                    start = 0 if numbers and numbers[0] in (1, 2) else x
                    end = x + 1 if numbers and numbers[0] == 1 else 120
                    grid[y][start:end] = [' '] * (end - start)
                elif final == 's': saved = (x, y)
                elif final == 'u': x, y = saved
                x = max(0, min(119, x)); y = max(0, min(self.rows - 1, y))
            elif token.startswith('\x1b'):
                continue
            elif token == '\r': x = 0
            elif token == '\n': y = min(self.rows - 1, y + 1)
            elif ord(token) >= 32 and not unicodedata.combining(token):
                grid[y][x] = token
                width = 2 if unicodedata.east_asian_width(token) in ('W', 'F') else 1
                if width == 2 and x < 119: grid[y][x + 1] = ' '
                x = min(119, x + width)
        return [''.join(row) for row in grid]

    def locate(self, label, timeout=3.0):
        # Nested dialog sessions re-initialize the terminal and may wait for
        # capability replies before their first frame; poll until it appears.
        deadline = time.monotonic() + timeout
        while True:
            for y, line in enumerate(self.lines()):
                x = line.find(label)
                if x >= 0:
                    return x, y
            if time.monotonic() >= deadline:
                raise AssertionError('Missing clickable ' + label + '\n' + '\n'.join(self.lines()))
            self.pump(0.1)

    def click(self, label, offset=1):
        x, y = self.locate(label)
        self.send(f'\x1b[<0;{x + offset + 1};{y + 1}M\x1b[<0;{x + offset + 1};{y + 1}m')

    def cursor_visible(self):
        modes = re.findall(rb'\x1b\[\?25([hl])', self.history)
        return bool(modes and modes[-1] == b'h')

    def menu(self, title, item):
        self.click(title)
        self.click(item)


def main():
    executable = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='etui-mouse-') as temporary:
        root = pathlib.Path(temporary)
        document = root / 'document.txt'
        document.write_text('original')
        plugins = root / 'data/encryptEd/plugins'
        plugins.mkdir(parents=True)
        scripts = {
            'A Input.lua': "editor.set_text(ui.input('Enter text:', 'abc'))",
            'B Choose.lua': "editor.set_text(tostring(ui.choose('Choose:', {'one','two'})))",
            'C Confirm.lua': "editor.set_text(ui.confirm('Apply?') and 'YES' or 'NO')",
            'D Message.lua': "ui.message('" + '\\n'.join(f'message row {i}' for i in range(60)) + "')",
        }
        for name, source in scripts.items(): (plugins / name).write_text(source)
        terminal = MouseTerminal(executable, document, root / 'config')
        try:
            for arrow in ['\x1b[C', '\x1b[D']:
                start = len(terminal.history)
                terminal.send(arrow)
                frame = terminal.history[start:]
                hidden = frame.find(b'\x1b[?25l')
                movement = re.search(rb'\x1b\[[0-9;]*[ABCDHf]', frame)
                assert hidden >= 0 and movement and hidden < movement.start(), 'cursor visible during repaint'
                assert terminal.cursor_visible(), 'editing cursor not restored after repaint'
            for title, item in [('File', 'Open...'), ('Edit', 'Find...'),
                                ('Plugins', 'Install Default Plugins'),
                                ('Settings', 'Syntax Highlighting'), ('Help', 'About')]:
                terminal.click(title)
                terminal.locate(item)
                assert not terminal.cursor_visible(), 'cursor visible over menu'
                terminal.click(title)
                assert terminal.cursor_visible(), 'editing cursor not restored'
                assert not any(item in line for line in terminal.lines()), title + ' reopened on release'
            terminal.send('\x1bf')
            terminal.click('File')
            assert not any('Open...' in line for line in terminal.lines())
            terminal.click('File')
            terminal.click('Edit')
            terminal.locate('Find...')
            terminal.click('Edit')
            assert not any('Find...' in line for line in terminal.lines())
            terminal.menu('File', 'Open...')
            terminal.locate('│Open│')
            terminal.click('│Cancel│')
            terminal.menu('File', 'Save As...')
            terminal.click('│Parent│')
            terminal.click('│Cancel│')
            terminal.menu('File', 'Save As...')
            terminal.click('Name: document.txt', offset=6)
            terminal.send('copy-')
            terminal.click('│Save│')
            assert (root / 'copy-document.txt').read_text() == 'original'
            terminal.menu('File', 'Open...')
            terminal.click('  document.txt', offset=3)
            terminal.click('│Open│')
            terminal.menu('Help', 'About')
            terminal.click('OK')
            terminal.menu('Plugins', 'A Input')
            terminal.click('Cancel')
            terminal.menu('Plugins', 'A Input')
            terminal.click('abc', offset=1)
            terminal.send('X')
            terminal.click('OK')
            terminal.menu('File', 'Save')
            assert document.read_text() == 'aXbc', (document.read_text(), '\n'.join(terminal.lines()))
            terminal.menu('Edit', 'Find...')
            terminal.send('X')
            terminal.click('│OK│')
            terminal.menu('Edit', 'Find...')
            terminal.click('│Cancel│')
            terminal.menu('Edit', 'Go to Line...')
            terminal.send('999')
            terminal.send('\t\t\r')  # keyboard still reaches Cancel
            terminal.menu('Plugins', 'B Choose')
            terminal.click('two')
            terminal.click('OK')
            terminal.menu('File', 'Save')
            assert document.read_text() == '2'
            terminal.menu('Plugins', 'C Confirm')
            terminal.click('No')
            terminal.menu('File', 'Save')
            assert document.read_text() == 'NO'
            terminal.menu('Plugins', 'C Confirm')
            terminal.click('Yes')
            terminal.menu('File', 'Save')
            assert document.read_text() == 'YES'
            terminal.menu('Plugins', 'D Message')
            for _ in range(5): terminal.click('Scroll Down')
            terminal.locate('message row 59')
            for _ in range(5): terminal.click('Scroll Up')
            terminal.locate('message row 0')
            terminal.click('OK')
            terminal.send('dirty')
            terminal.menu('File', 'New')
            terminal.click('│No│')
            terminal.menu('File', 'New')
            terminal.click('│Yes│')
            terminal.locate('New document')
            terminal.menu('File', 'Save As...')
            terminal.click('Name:', offset=6)
            terminal.send('encrypted.txt')
            terminal.click('Encrypted (.txx)')
            terminal.click('│Save│')
            terminal.locate('New Password')
            terminal.send('password')
            terminal.click('│OK│')
            terminal.locate('Confirm Password')
            terminal.send('wrong')
            terminal.click('│OK│')
            terminal.locate('Passwords do not match.')
            terminal.click('OK')
            terminal.menu('File', 'Quit')
            terminal.process.wait(timeout=5)
        finally:
            terminal.close()
        terminal = MouseTerminal(executable, document, root / 'config', rows=24)
        try:
            terminal.menu('Plugins', 'D Message')
            terminal.locate('OK')
            for _ in range(5): terminal.click('Scroll Down')
            terminal.locate('message row 59')
            x, y = terminal.locate('message row 59')
            for _ in range(5): terminal.send(f'\x1b[<64;{x + 1};{y + 1}M')
            terminal.locate('message row 0')
            terminal.click('OK')
            terminal.menu('Plugins', 'B Choose')
            terminal.click('two')
            terminal.click('OK')
        finally:
            terminal.close()
        for content, expected in [('', '0 B'), ('x' * 999, '999 B'),
                                  ('x' * 1000, '1.0 KB'), ('é' * 61750, '123.5 KB')]:
            document.write_text(content)
            terminal = MouseTerminal(executable, document, root / 'config')
            try:
                terminal.menu('Settings', 'Status: Readable Size')
                terminal.locate(expected)
                assert 'status_count=2' in (root / 'config/encryptEd/config').read_text()
            finally:
                terminal.close()
        terminal = MouseTerminal(executable, document, root / 'config')
        try:
            terminal.locate('123.5 KB')  # readable size survives restart
            terminal.menu('Settings', 'Status: Byte Count')
            terminal.locate('123500 bytes')
            terminal.menu('Settings', 'Status: Char Count')
            terminal.locate('61750 chars')  # UTF-8 bytes and characters differ
        finally:
            terminal.close()
    print('Mouse navigation smoke checks passed')


if __name__ == '__main__': main()
