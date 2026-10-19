"""Headless terminal plugin regression: python3 tests/tui/plugin_smoke.py build/etui."""
import errno
import fcntl
import os
import pathlib
import pty
import select
import struct
import subprocess
import sys
import tempfile
import termios
import time


class Terminal:
    def __init__(self, executable, document, config, rows=40, columns=120, relative_path=False):
        self.master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', rows, columns, 0, 0))
        env = dict(os.environ, TERM='xterm-256color', XDG_CONFIG_HOME=str(config), XDG_DATA_HOME=str(config.parent / "data"))
        self.process = subprocess.Popen([str(executable), document.name if relative_path else str(document)], stdin=slave,
                                        stdout=slave, stderr=slave, env=env, cwd=document.parent, start_new_session=True)
        os.close(slave)
        self.output = b''
        self.pump(0.5)

    def pump(self, duration):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            if select.select([self.master], [], [], max(0, deadline - time.monotonic()))[0]:
                try:
                    data = os.read(self.master, 65536)
                except OSError as error:
                    if error.errno == errno.EIO:
                        return
                    raise
                if not data:
                    return
                self.output += data
                if b'\x1b[6n' in data:
                    os.write(self.master, b'\x1b[1;1R')

    def send(self, keys):
        os.write(self.master, keys.encode() if isinstance(keys, str) else keys)
        self.pump(0.15)

    def wait(self, text):
        target = text.encode()
        deadline = time.monotonic() + 5
        while target not in self.output:
            assert self.process.poll() is None, self.output[-2000:].decode(errors='replace')
            if time.monotonic() >= deadline:
                raise AssertionError('Missing ' + text + '\n' + self.output[-4000:].decode(errors='replace'))
            self.pump(0.1)

    def plugin(self, downs, title=None, submenu=False):
        self.output = b''
        self.send('\x1bp')
        self.wait('Install Default Plugins')
        self.send('\x1b[B' * downs)
        if submenu:
            self.send('\x1b[C')
        self.send('\r')
        if title:
            self.wait(title)

    def save(self, document, expected):
        self.send(b'\x13')
        assert document.read_text() == expected, repr(document.read_text())

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=5)
        os.close(self.master)


def scrollbar_smoke(executable):
    with tempfile.TemporaryDirectory(prefix='etui-scrollbar-') as temporary:
        root = pathlib.Path(temporary)
        config = root / 'config'
        document = root / 'long.txt'
        original = '\n'.join(f'row {i}' for i in range(100))
        document.write_text(original)
        terminal = Terminal(executable, document, config)
        try:
            terminal.wait('░')
            terminal.wait('█')
            # Click the bottom of the track, then drag back to its top.
            terminal.send('\x1b[<0;120;39M\x1b[<0;120;39m')
            terminal.wait('row 99')
            terminal.output = b''
            terminal.send('\x1b[<0;120;2M\x1b[<32;120;2M\x1b[<0;120;2m')
            terminal.wait('row 0')
            terminal.save(document, original)
            terminal.send('\x1bs')
            terminal.wait('Vertical Scrollbar')
            terminal.send('\r')
            assert 'vertical_scrollbar=0' in (config / 'encryptEd/config').read_text()
        finally:
            terminal.close()
        terminal = Terminal(executable, document, config)
        try:
            assert '░'.encode() not in terminal.output
            terminal.send('\x1bs')
            terminal.send('\r')
            terminal.wait('░')
            assert 'vertical_scrollbar=1' in (config / 'encryptEd/config').read_text()
            terminal.output = b''
            fcntl.ioctl(terminal.master, termios.TIOCSWINSZ, struct.pack('HHHH', 120, 120, 0, 0))
            terminal.pump(0.5)
            assert '░'.encode() not in terminal.output
        finally:
            terminal.close()
        document.write_text('short')
        terminal = Terminal(executable, document, config)
        try:
            assert '░'.encode() not in terminal.output
        finally:
            terminal.close()


def main():
    executable = pathlib.Path(sys.argv[1]).resolve()
    scrollbar_smoke(executable)
    with tempfile.TemporaryDirectory(prefix='etui-plugins-') as temporary:
        root = pathlib.Path(temporary)
        config = root / 'config'
        plugins = root / 'data/encryptEd/plugins'
        plugins.mkdir(parents=True)
        scripts = {
            'A Input.lua': "editor.insert(1, ui.input('Prefix:', ''))",
            'B Choice.lua': "local n=ui.choose('Choose a suffix:', {'one','two'}); editor.insert(#editor.text()+1,tostring(n))",
            'C Confirm.lua': "editor.insert(1,ui.confirm('Apply?') and 'YES' or 'NO')",
            'D Message.lua': "editor.insert(1,'M'); ui.message('Changes are still staged.')",
            'E Error.lua': "editor.set_text('discarded'); error('intentional failure')",
            'F Noop.lua': "editor.set_text(editor.text())",
            'Nested/Deep/G Unicode.lua': "assert(editor.selected_text()=='é'); editor.replace_selection('猫'); editor.set_cursor(1)",
        }
        for name, source in scripts.items():
            path = plugins / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source)
        document = root / 'document.txt'
        document.write_text('é original')
        terminal = Terminal(executable, document, config)
        try:
            terminal.send('\x1bh')
            terminal.send('\r')
            for detail in ['About etui', 'etui - EncryptEd terminal editor', 'Version: 1.2',
                           'Author: Tony Mattsson', 'GPLv3', 'FTXUI 7.0.3',
                           'Lua 5.5.1', 'Monocypher 4.0.3', 'microz (unversioned']:
                terminal.wait(detail)
            terminal.send('\r')
            terminal.save(document, 'é original')
            # Management entries: three, separator skipped, then Nested and six root scripts.
            terminal.plugin(4, 'Plugin: A Input')
            terminal.send('hello ')
            terminal.send('\r')
            terminal.wait('Plugin completed: A Input')
            terminal.save(document, 'hello é original')
            terminal.send('\x1bz')
            terminal.save(document, 'é original')
            terminal.send(b'\x19')
            terminal.save(document, 'hello é original')
            terminal.send('\x1bz')
            terminal.save(document, 'é original')

            terminal.plugin(4, 'Plugin: A Input')
            terminal.send('discarded')
            terminal.send('\x1b')
            terminal.wait('Plugin cancelled: A Input')
            terminal.save(document, 'é original')

            terminal.plugin(5, 'Plugin: B Choice')
            terminal.send('\x1b[B')
            terminal.send('\r')
            terminal.wait('Plugin completed: B Choice')
            terminal.save(document, 'é original2')
            terminal.send('\x1bz')
            terminal.save(document, 'é original')

            terminal.plugin(6, 'Plugin: C Confirm')
            terminal.send('\r')  # No is a normal false response, not cancellation.
            terminal.wait('Plugin completed: C Confirm')
            terminal.save(document, 'NOé original')
            terminal.send('\x1bz')
            terminal.save(document, 'é original')
            terminal.plugin(6, 'Plugin: C Confirm')
            terminal.send('\x1b')
            terminal.wait('Plugin cancelled: C Confirm')
            terminal.save(document, 'é original')
            terminal.plugin(6, 'Plugin: C Confirm')
            terminal.send('\x1b[D')
            terminal.send('\r')
            terminal.wait('Plugin completed: C Confirm')
            terminal.save(document, 'YESé original')
            terminal.send('\x1bz')
            terminal.save(document, 'é original')

            terminal.plugin(7, 'Changes are still staged.')
            terminal.send('\x1b')
            terminal.wait('Plugin cancelled: D Message')
            terminal.save(document, 'é original')
            terminal.plugin(7, 'Changes are still staged.')
            terminal.send('\r')
            terminal.wait('Plugin completed: D Message')
            terminal.save(document, 'Mé original')
            terminal.send('\x1bz')
            terminal.save(document, 'é original')

            terminal.plugin(8, 'intentional failure')
            terminal.send('\r')
            terminal.save(document, 'é original')
            terminal.plugin(9, 'Plugin completed: F Noop')
            # A no-op must not require a dirty-document confirmation on quit.

            # Select the first Unicode code point and navigate two folder levels.
            terminal.send(b'\x1b[H')  # Home
            terminal.send('\x1b[1;2C')  # Shift+Right
            terminal.plugin(3, 'Choose a plugin or folder:', submenu=True)
            terminal.send('\x1b[B')  # Back is first; choose G Unicode.
            terminal.send('\r')
            terminal.wait('Plugin completed: G Unicode')
            terminal.save(document, '猫 original')
            terminal.send('\x1bz')
            terminal.save(document, 'é original')

            # Existing source edits take effect without reload.
            (plugins / 'F Noop.lua').write_text("editor.set_text('edited')")
            terminal.plugin(9, 'Plugin completed: F Noop')
            terminal.save(document, 'edited')
            terminal.send('\x1bz')
            terminal.save(document, 'é original')
            # Reload discovers new scripts.
            (plugins / 'Z New.lua').write_text("editor.set_text('new')")
            terminal.plugin(1)
            terminal.plugin(10, 'Plugin completed: Z New')
            terminal.save(document, 'new')

            terminal.plugin(0, 'Installed 12 plugins')
            terminal.send('\r')
            assert len(list(plugins.rglob('*.lua'))) == 20
            modified = plugins / 'HTML/Compact HTML.lua'
            modified.write_text('-- edited default')
            terminal.plugin(0, 'Installed 0 plugins')
            terminal.send('\r')
            assert modified.read_text() == '-- edited default'
            terminal.save(document, 'new')
            terminal.send(b'\x11')
            assert terminal.process.wait(timeout=5) == 0
            print('Terminal plugins: input, choice, confirmation, messages, cancellation, error rollback, Unicode selection, nested folders, undo/redo, reload, and default installation passed.')
        finally:
            terminal.close()


if __name__ == '__main__':
    main()
