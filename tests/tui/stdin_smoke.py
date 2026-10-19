"""Documents read from standard input: `etui -` and `something | etui`.

Runs the editor with a pipe on stdin and a pty as the controlling terminal, so
FTXUI can take the keyboard from /dev/tty while stdin carries the document.
"""
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


class PipedTerminal:
    def __init__(self, executable, piped_text, config, arguments=(), rows=40, columns=120):
        self.master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', rows, columns, 0, 0))
        read_fd, write_fd = os.pipe()
        os.write(write_fd, piped_text)
        os.close(write_fd)

        def preexec():  # give the child the pty as its controlling terminal
            os.setsid()
            fcntl.ioctl(slave, termios.TIOCSCTTY, 0)

        env = dict(os.environ, TERM='xterm-256color',
                   XDG_CONFIG_HOME=str(config), XDG_DATA_HOME=str(config.parent / 'data'))
        self.process = subprocess.Popen(
            [str(executable), *arguments], stdin=read_fd, stdout=slave, stderr=slave,
            env=env, preexec_fn=preexec, close_fds=False)
        os.close(slave)
        os.close(read_fd)
        self.output = b''

    def pump(self, duration):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            if select.select([self.master], [], [], max(0, deadline - time.monotonic()))[0]:
                try:
                    data = os.read(self.master, 65536)
                except OSError:
                    return
                if not data:
                    return
                self.output += data

    def wait(self, text):
        target = text.encode()
        deadline = time.monotonic() + 5
        while target not in self.output:
            assert self.process.poll() is None, self.output[-2000:].decode(errors='replace')
            if time.monotonic() >= deadline:
                raise AssertionError('Missing ' + text + '\n' + self.output[-4000:].decode(errors='replace'))
            self.pump(0.1)

    def send(self, keys):
        os.write(self.master, keys.encode() if isinstance(keys, str) else keys)
        self.pump(0.2)

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=5)
        os.close(self.master)


def main():
    if not os.path.exists('/dev/tty'):
        print('Skipping stdin smoke checks: /dev/tty is unavailable')
        return
    executable = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='etui-stdin-') as temporary:
        root = pathlib.Path(temporary)
        document = root / 'from-file.txt'
        document.write_text('header line\nFILE CONTENT\n')

        terminal = PipedTerminal(executable, b'piped document text\nsecond line\n', root / 'config')
        try:
            terminal.wait('second line')      # piped document rendered
            terminal.send('\x1b[21~')         # F10: keyboard still comes from /dev/tty
            terminal.wait('Open...')
        finally:
            terminal.close()

        terminal = PipedTerminal(executable, b'ignored piped text\n',
                                 root / 'config', arguments=(str(document),))
        try:
            terminal.wait('FILE CONTENT')     # an explicit path wins over stdin
        finally:
            terminal.close()
    print('Stdin document smoke checks passed')


if __name__ == '__main__':
    main()
