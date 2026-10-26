"""Security regressions for Save As: destinations, cancellation, and encryption."""
import pathlib
import subprocess
import sys
import tempfile

from plugin_smoke import Terminal

MAGIC = b'TXX1\r\n\x1a\n'


def save_as(terminal, old_name, new_name):
    terminal.output = b''
    terminal.send('\x1bf')
    terminal.wait('Save As...')
    terminal.send('\x1b[B' * 3 + '\r')
    terminal.wait('Encrypted (.txx)')
    terminal.send('\x7f' * len(old_name) + new_name + '\r')


def main():
    executable = pathlib.Path(sys.argv[1]).resolve()
    cli = pathlib.Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix='etui-save-security-') as directory:
        root = pathlib.Path(directory)
        source = root / 'source.txt'
        expected = b'changed original\n'
        for scenario in ('cancel_first', 'cancel_confirmation', 'mismatch', 'write_failure'):
            source.write_bytes(b'original\n')
            destination = root / (scenario + '.txx')
            if scenario == 'write_failure':
                destination = root / 'missing' / 'failed.txx'
            terminal = Terminal(executable, source, root / 'config')
            try:
                terminal.send('changed ')
                save_as(terminal, source.name, str(destination))
                terminal.wait('New Password')
                if scenario == 'cancel_first':
                    terminal.send('\x1b')
                else:
                    terminal.send('audit-password\r')
                    terminal.wait('Confirm Password')
                    if scenario == 'cancel_confirmation':
                        terminal.send('\x1b')
                    else:
                        terminal.send(('wrong' if scenario == 'mismatch' else 'audit-password') + '\r')
                        terminal.wait('Passwords do not match.' if scenario == 'mismatch' else 'Write failed:')
                        terminal.output = b''
                        terminal.send('\r')
                        terminal.wait('INS')  # the nested alert has returned to the editor
                        terminal.pump(0.3)
                terminal.output = b''
                terminal.send('\x13')
                terminal.wait('Saved source.txt')
                assert not destination.exists(), scenario + ': failed Save As changed destination'
                assert source.read_bytes() == expected, scenario + ': original document was not saved'
            finally:
                terminal.close()

        source.write_bytes(b'original\n')
        encrypted = root / 'secret.TXX'
        exported = root / 'export.txt'
        terminal = Terminal(executable, source, root / 'config')
        try:
            save_as(terminal, source.name, encrypted.name)
            terminal.wait('New Password')
            terminal.send('audit-password\r')
            terminal.wait('Confirm Password')
            terminal.send('audit-password\r')
            terminal.wait('Saved secret.TXX')
            assert encrypted.read_bytes().startswith(MAGIC)
            terminal.send('changed ')
            terminal.send('\x13')
            assert encrypted.read_bytes().startswith(MAGIC)

            save_as(terminal, encrypted.name, exported.name)
            terminal.wait('Save an unencrypted copy')
            terminal.send('\r')  # default No
            terminal.send('\x13')
            assert not exported.exists()
            assert encrypted.read_bytes().startswith(MAGIC)
            decoded = root / 'decoded.txt'
            subprocess.run([str(cli), 'unpack', str(encrypted), str(decoded), '--password-stdin'],
                           input=b'audit-password\n', check=True)
            assert decoded.read_bytes() == expected

            save_as(terminal, encrypted.name, exported.name)
            terminal.wait('Save an unencrypted copy')
            terminal.send('\x1b[D\r')  # explicit Yes
            assert exported.read_bytes() == expected
        finally:
            terminal.close()
    print('Save security smoke checks passed')


if __name__ == '__main__':
    main()
