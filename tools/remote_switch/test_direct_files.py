import hashlib
from pathlib import Path
import tempfile
import unittest
from direct_files import DirectFiles


class Fake:
    def __init__(self, corrupt=False):
        self.content = bytearray()
        self.corrupt = corrupt
        self.commands = []

    def send(self, command, **kwargs):
        self.commands.append(command)
        parts = command.split()
        if parts[0] == 'arVersion': return b'OK 1'
        if parts[0] == 'arBegin': return b'OK'
        if parts[0] == 'arWrite':
            assert int(parts[2]) == len(self.content)
            self.content.extend(bytes.fromhex(parts[3]))
            return f'OK {len(self.content)}'.encode()
        if parts[0] == 'arRead':
            offset, count = map(int, parts[2:])
            value = bytes(self.content[offset:offset+count])
            if self.corrupt: value = b'x' * count
            return b'OK ' + value.hex().encode()
        raise AssertionError(command)


class DirectTests(unittest.TestCase):
    def test_live_reads_only_closed_snapshots_and_records_identity(self):
        commands=[]
        class Live:
            def send(self, command, **kwargs):
                commands.append(command)
                if command == 'arVersion': return b'OK 1'
                if command == 'arTail remote-snapshot-a.log':
                    payload=b'[STARTUP] stage=4 total_s=100 graphics_presents=0\n[REMOTE-SNAPSHOT] run=0123456789abcdef sequence=32 tick=1234 build=nx-amd64-fex-2640\n'
                    return b'OK 0 '+payload.hex().encode()
                if command == 'arTail remote-snapshot-b.log': return b'ERR open'
                raise AssertionError(command)
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as folder:
            result=DirectFiles(Live()).collect(Path(folder),live=True)
            self.assertEqual(result[0]['snapshot']['sequence'],32)
            self.assertEqual(result[0]['snapshot']['run'],'0123456789abcdef')
            self.assertEqual(result[1]['status'],'unavailable')
            self.assertNotIn('arLogs',commands)

    def nro(self, folder):
        path = Path(folder) / 'probe.nro'
        path.write_bytes(bytes(16) + b'NRO0' + bytes(range(256)) * 20)
        return path

    def test_multichunk_stage_readback_without_activation(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as folder:
            path = self.nro(folder)
            fake = Fake()
            result = DirectFiles(fake).stage(path)
            self.assertEqual(result['sha256'], hashlib.sha256(path.read_bytes()).hexdigest())
            self.assertFalse(result['activated'])
            self.assertEqual(bytes(fake.content), path.read_bytes())
            self.assertFalse(any(c.startswith('arCommit') for c in fake.commands))

    def test_corrupt_readback_stops(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as folder:
            fake = Fake(corrupt=True)
            with self.assertRaises(ValueError): DirectFiles(fake).stage(self.nro(folder))
            self.assertFalse(any(c.startswith('arCommit') for c in fake.commands))

    def test_injected_digest_never_sends(self):
        fake = Fake()
        with self.assertRaises(ValueError): DirectFiles(fake).activate('a'*64+'\nclick A')
        self.assertEqual(fake.commands, [])

    def test_directory_traversal_never_reads(self):
        class Listing:
            def send(self, command, **kwargs):
                return b'OK 1' if command == 'arVersion' else b'OK ../config.ini'
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as folder:
            with self.assertRaises(ValueError): DirectFiles(Listing()).collect(Path(folder))


if __name__ == '__main__': unittest.main()
