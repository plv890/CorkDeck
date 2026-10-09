"""Host fixtures only: never represent these as Switch validation."""
import unittest
import tempfile
from pathlib import Path
from unittest.mock import patch
from controller import Controller
from diagnostics import Diagnostics, DiagnosticsHTTPError
from ftp_logs import redact_tail, collect as collect_ftp
from transport import BotTransport, lan_ip
from transfer import stage_nro, activate_test
from credentials import http_key


class FakeTransport:
    def __init__(self, answer=b''):
        self.answer, self.commands = answer, []

    def send(self, command, **kwargs):
        self.commands.append(command)
        return self.answer


class RemoteTests(unittest.TestCase):
    def test_http_key_decrypts_dpapi_path_and_rejects_invalid_value(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as directory:
            path=Path(directory)/'key.dpapi'
            path.write_bytes(b'encrypted fixture')
            with patch('credentials._crypt',return_value=b'0'*32) as decrypt:
                self.assertEqual(http_key(path),'0'*32)
                decrypt.assert_called_once_with(b'encrypted fixture',decrypt=True)
            with patch('credentials._crypt',return_value=b'not-a-key'),self.assertRaises(ValueError):
                http_key(path)
    def test_memory_collection_uses_only_live_route_and_filters_secrets(self):
        diagnostics=Diagnostics('192.168.1.3','0'*32)
        payload=(b'password=fixture_secret\n[REMOTE-STATE] fatal_kind=1\n'
                 b'[REMOTE-SNAPSHOT] run=0123456789abcdef sequence=5 tick=100 build=test\n')
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as directory:
            folder=Path(directory)
            with patch.object(diagnostics,'get',return_value=payload) as get:
                result=diagnostics.collect_live(folder)
            get.assert_called_once_with('/live')
            self.assertEqual(result[0]['snapshot']['sequence'],5)
            self.assertNotIn(b'fixture_secret',(folder/'live-runtime.log').read_bytes())
    def test_whole_logs_require_bounded_explicit_names_before_connect(self):
        with patch('ftp_logs.FTP') as ftp:
            for names in (None, [], ['../settings.log'], ['a.log']*5, ['credentials.txt']):
                with self.assertRaises(ValueError):
                    collect_ftp('192.168.1.3', Path('.'), selected=names, whole=True)
            ftp.assert_not_called()

    def test_ready_press_releases_and_detaches_after_failure(self):
        class Failing(FakeTransport):
            def send(self, command, **kwargs):
                self.commands.append(command)
                if command == 'press B': raise OSError('fixture disconnect')
        fake = Failing()
        with patch('controller.time.sleep'), self.assertRaises(OSError):
            Controller(fake).click_ready('B')
        self.assertEqual(fake.commands, ['click UNUSED', 'press B', 'release B', 'detachController'])

    def test_ftp_native_log_uses_runtime_directory_without_duplicate(self):
        for listed in (False, True):
            paths = []
            class FTPFixture:
                def __init__(self, **kwargs): pass
                def connect(self, *args): pass
                def login(self): pass
                def cwd(self, path): pass
                def retrlines(self, command, callback):
                    if listed: callback('-rw-r--r-- 1 owner group 7 Jan 1 00:00 autorun_runtime.log')
                def voidcmd(self, command): pass
                def size(self, path): return 7
                def retrbinary(self, command, callback, rest=None):
                    paths.append(command)
                    callback(b'[LOG]x\n')
                def close(self): pass
            with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as folder, \
                    patch('ftp_logs.FTP', FTPFixture), patch('ftp_logs.login', lambda ftp: None):
                result = collect_ftp('192.168.1.3', Path(folder), port=5000)
                self.assertEqual(paths, ['RETR /switch/wine/logs/autorun_runtime.log'])
                self.assertEqual(result[0]['status'], 'received')

    def test_home_prime_keeps_one_device_then_detaches(self):
        fake = FakeTransport()
        with patch('controller.time.sleep'):
            Controller(fake).home_primed()
        self.assertEqual(fake.commands, ['click UNUSED', 'click HOME', 'detachController'])

    def test_launch_chord_releases_on_input_failure(self):
        class Failing(FakeTransport):
            def send(self, command, **kwargs):
                self.commands.append(command)
                if command == 'click A': raise OSError('fixture disconnect')
        fake = Failing()
        with patch('controller.time.sleep'), self.assertRaises(OSError):
            Controller(fake).album_homebrew()
        self.assertEqual(fake.commands, ['press R', 'click A', 'release R', 'detachController'])

    def test_lan_policy(self):
        for value in ('8.8.8.8', '127.0.0.1', 'example.org', '::1', '169.254.1.1'):
            with self.assertRaises(ValueError):
                lan_ip(value)
        self.assertEqual(lan_ip('192.168.1.3'), '192.168.1.3')

    def test_injection_blocked(self):
        fake = FakeTransport()
        for command in ('A\r\npoke 0 0', 'poke', 'press A'):
            with self.assertRaises(ValueError):
                Controller(fake).click(command)
        self.assertEqual(fake.commands, [])

    def test_capture_empty_or_invalid(self):
        for answer in (b'', b'0', b'0011', b'ZZ'):
            with self.assertRaises(ValueError):
                Controller(FakeTransport(answer)).screenshot()

    def test_hex_capture(self):
        self.assertEqual(Controller(FakeTransport(b'FFD8FFD9')).screenshot(), b'\xff\xd8\xff\xd9')

    def test_title_parsing(self):
        self.assertIsNone(Controller(FakeTransport(b'')).title())
        self.assertEqual(Controller(FakeTransport(b'0100000000001000')).title(), '0100000000001000')
        with self.assertRaises(ValueError):
            Controller(FakeTransport(b'F'*18)).title()

    def test_key_not_accepted_as_url(self):
        with self.assertRaises(ValueError):
            Diagnostics('192.168.1.3', 'http://192.168.1.3:8181/#secret')

    def exchange(self, fragments, limit=128):
        commands = []
        class Connection:
            def __enter__(self):
                return self
            def __exit__(self, *args):
                pass
            def sendall(self, data):
                commands.append(data)
            def settimeout(self, timeout):
                assert 0 < timeout <= 5
            def recv(self, size):
                if not fragments:
                    return b''
                data = fragments.pop(0)
                if len(data) > size:
                    fragments.insert(0, data[size:])
                return data[:size]
        with patch('transport.socket.create_connection', return_value=Connection()):
            try:
                return BotTransport('192.168.1.3').send('getVersion', reply=True, limit=limit)
            finally:
                self.assertEqual(commands, [b'getVersion\r\n'])

    def test_fragmented_response(self):
        self.assertEqual(self.exchange([b'2.', b'5\r', b'\n']), b'2.5')

    def test_disconnect_preserves_failure(self):
        with self.assertRaises(ConnectionError):
            self.exchange([b'2.5'])

    def test_oversized_reply(self):
        with self.assertRaises(ValueError):
            self.exchange([b'123456789\n'], limit=5)

    def test_stalled_reply_deadline(self):
        with patch('transport.socket.create_connection') as connect:
            connect.return_value.__enter__.return_value.recv.side_effect = TimeoutError
            with self.assertRaises(TimeoutError):
                BotTransport('192.168.1.3', timeout=0.05).send('getVersion', reply=True)

    def transfer_fixture(self, bad_readback=False, existing=False):
        from unittest.mock import MagicMock
        ftp = MagicMock()
        data = b'\0'*16 + b'NRO0' + b'test fixture'
        import hashlib
        filename = 'test-' + hashlib.sha256(data).hexdigest() + '.nro'
        if existing:
            ftp.retrlines.side_effect = lambda command, callback: callback('-rw-rw-rw- 1 user user 32 Jan 1 1970 ' + filename)
        ftp.retrbinary.side_effect = lambda command, callback: callback(b'bad' if bad_readback else data)
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parent) as directory:
            path = Path(directory) / 'probe.nro'
            path.write_bytes(data)
            with patch('transfer.FTP', return_value=ftp):
                if bad_readback or existing:
                    with self.assertRaises(ValueError):
                        stage_nro('192.168.1.3', path)
                    ftp.rename.assert_not_called()
                    if existing:
                        ftp.storbinary.assert_not_called()
                else:
                    result = stage_nro('192.168.1.3', path)
                    self.assertTrue(result['readback_verified'])
                    self.assertFalse(result['installed_or_launched'])
                    ftp.rename.assert_called_once_with(filename + '.part', filename)
        ftp.close.assert_called_once()

    def test_transfer_readback_before_commit(self):
        self.transfer_fixture()

    def test_transfer_mismatch_never_commits(self):
        self.transfer_fixture(bad_readback=True)

    def test_transfer_never_overwrites(self):
        self.transfer_fixture(existing=True)

    def test_transfer_rejects_non_nro(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parent) as directory:
            path = Path(directory) / 'fake.nro'
            path.write_bytes(b'not a valid NRO')
            with patch('transfer.FTP') as ftp:
                with self.assertRaises(ValueError):
                    stage_nro('192.168.1.3', path)
                ftp.assert_not_called()

    def test_redaction_drops_fragments_and_credentials(self):
        self.assertEqual(redact_tail(b'cut-secret\nok=1\npassword=abc\nunfinished', True),
                         b'ok=1\n[REDACTED sensitive line]\n')

    def test_http_file_failure_preserves_other_logs(self):
        diagnostics = Diagnostics('192.168.1.3', 'a'*32)
        answers = {'/status': b'active', '/files': b'locked.log\ngood.log\n',
                   '/tail/good.log': b'ok\n'}
        def get(route):
            if route == '/tail/locked.log':
                raise DiagnosticsHTTPError(409)
            return answers[route]
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parent) as directory:
            with patch.object(diagnostics, 'get', side_effect=get):
                results = diagnostics.collect(Path(directory))
            self.assertEqual(results[0]['http_status'], 409)
            self.assertEqual((Path(directory)/'good.log').read_bytes(), b'ok\n')

    def test_activation_rejects_invalid_hash_before_network(self):
        with patch('transfer.FTP') as ftp:
            with self.assertRaises(ValueError):
                activate_test('192.168.1.3', '../probe')
            ftp.assert_not_called()

    def test_activation_digest_mismatch_never_renames(self):
        from unittest.mock import MagicMock
        ftp = MagicMock()
        digest = 'a'*64
        ftp.retrlines.side_effect = lambda command, callback: callback('-rw-rw-rw- 1 user user 3 Jan 1 1970 test-'+digest+'.nro')
        ftp.retrbinary.side_effect = lambda command, callback: callback(b'bad')
        with patch('transfer.FTP', return_value=ftp):
            with self.assertRaises(ValueError):
                activate_test('192.168.1.3', digest)
        ftp.rename.assert_not_called()
        ftp.close.assert_called_once()


if __name__ == '__main__':
    unittest.main()
