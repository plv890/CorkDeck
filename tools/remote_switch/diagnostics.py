"""Read-only adapter for existing Autorun LAN diagnostics; tokens stay in env."""
import http.client
import json
import re
from transport import lan_ip

NAME = re.compile(r'[A-Za-z0-9_-][A-Za-z0-9_.-]{0,159}\Z')


class DiagnosticsHTTPError(ConnectionError):
    def __init__(self, status):
        self.status = status
        super().__init__(f'Diagnostics HTTP {status}')


class Diagnostics:
    def __init__(self, host, key, timeout=5):
        self.host = lan_ip(host)
        if not re.fullmatch(r'[0-9a-fA-F]{32}', key):
            raise ValueError('Set AUTORUN_LAN_KEY to the temporary 32-hex key')
        self.key, self.timeout = key, timeout

    def get(self, route):
        conn = http.client.HTTPConnection(self.host, 8181, timeout=self.timeout)
        try:
            conn.request('GET', route, headers={'X-Autorun-Key': self.key})
            response = conn.getresponse()
            if response.status != 200:
                raise DiagnosticsHTTPError(response.status)
            data = response.read(20481)
            if len(data) > 20480:
                raise ValueError('Diagnostics response exceeds limit')
            return data
        finally:
            conn.close()

    def collect(self, folder):
        (folder / 'status.txt').write_bytes(self.get('/status'))
        names = self.get('/files').decode('ascii').splitlines()
        if len(names) > 64:
            raise ValueError('Too many log files')
        results = []
        for name in names:
            if not NAME.fullmatch(name) or '..' in name or not (
                    name.endswith('.log') or name in ('stdout.txt', 'stderr.txt')):
                raise ValueError('Invalid log name')
            try:
                data = self.get('/tail/' + name)
                (folder / name).write_bytes(data)
                results.append({'name': name, 'bytes': len(data), 'status': 'received'})
            except DiagnosticsHTTPError as exc:
                results.append({'name': name, 'http_status': exc.status, 'status': 'unavailable'})
            (folder / 'collection.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
        return results

    def collect_live(self, folder):
        from ftp_logs import redact_tail
        # The authenticated server filters secrets and reads a bounded memory
        # buffer, so the current game does not need to be closed.
        data = self.get('/live')
        (folder / 'live-runtime.log').write_bytes(redact_tail(data, truncated=False))
        item = {'name': 'live-runtime.log', 'source_bytes': len(data), 'status': 'received'}
        metadata = re.search(rb'\[REMOTE-SNAPSHOT\] run=([0-9a-f]{16}) sequence=(\d{1,20}) tick=(\d{1,20}) build=([A-Za-z0-9_.-]{1,64})\r?\n\Z', data)
        if metadata:
            item['snapshot'] = {'run': metadata[1].decode(), 'sequence': int(metadata[2]),
                                'tick': int(metadata[3]), 'build': metadata[4].decode()}
        (folder / 'collection.json').write_text(json.dumps([item], indent=2)+'\n', encoding='utf-8')
        return [item]
