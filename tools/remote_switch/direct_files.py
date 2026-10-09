"""Bounded sys-botbase extension client; no FTP or menu dependency."""
import hashlib
import json
import re
from ftp_logs import redact_tail

NAME = re.compile(r'[A-Za-z0-9_.-]{1,100}\Z')
CHUNK = 2048
MAX_FILE = 2 * 1024 * 1024


class DirectFiles:
    def __init__(self, transport):
        self.transport = transport

    def request(self, command, limit=8192):
        response = self.transport.send(command, reply=True, limit=limit)
        if response == b'OK':
            return ''
        if not response.startswith(b'OK '):
            raise ValueError('Direct file command rejected')
        return response[3:].decode('ascii')

    def version(self):
        if self.request('arVersion') != '1':
            raise ValueError('Unsupported extension version')

    def collect(self, folder, live=False):
        self.version()
        listing = '' if live else self.request('arLogs')
        names = ['remote-snapshot-a.log', 'remote-snapshot-b.log'] if live else (listing.split(',') if listing else [])
        if len(names) > 32 or any(not NAME.fullmatch(n) or '..' in n for n in names):
            raise ValueError('Invalid log listing')
        if not live:
            names = list(dict.fromkeys(['autorun_runtime.log'] + names))
        results = []
        for name in names:
            try:
                offset, _, encoded = self.request(f'arTail {name}').partition(' ')
                if not offset.isdecimal() or len(encoded) > CHUNK * 2:
                    raise ValueError('Invalid tail response')
                raw = bytes.fromhex(encoded)
                saved = redact_tail(raw, truncated=int(offset) > 0)
                (folder / name).write_bytes(saved)
                result = {'name': name, 'source_bytes': len(raw),
                          'saved_bytes': len(saved), 'status': 'received'}
                metadata = re.search(rb'\[REMOTE-SNAPSHOT\] run=([0-9a-f]{16}) sequence=(\d{1,20}) tick=(\d{1,20}) build=([A-Za-z0-9_.-]{1,64})\r?\n\Z', raw)
                if metadata:
                    result['snapshot'] = {'run': metadata[1].decode(), 'sequence': int(metadata[2]),
                                          'tick': int(metadata[3]), 'build': metadata[4].decode()}
                results.append(result)
            except ValueError:
                results.append({'name': name, 'status': 'unavailable'})
        (folder / 'collection.json').write_text(json.dumps(results, indent=2)+'\n', encoding='utf-8')
        return results

    def stage(self, nro):
        self.version()
        if not 20 <= nro.stat().st_size <= MAX_FILE:
            raise ValueError('Test NRO size outside bounds')
        content = nro.read_bytes()
        if not 20 <= len(content) <= MAX_FILE or content[16:20] != b'NRO0':
            raise ValueError('Invalid test NRO')
        digest = hashlib.sha256(content).hexdigest()
        self.request(f'arBegin {digest}')
        for offset in range(0, len(content), CHUNK):
            block = content[offset:offset+CHUNK]
            result = self.request(f'arWrite {digest} {offset} {block.hex()}')
            if result != str(offset + len(block)):
                raise ValueError('Upload offset mismatch; stop')
        checksum = hashlib.sha256()
        for offset in range(0, len(content), CHUNK):
            count = min(CHUNK, len(content)-offset)
            encoded = self.request(f'arRead {digest} {offset} {count}')
            if len(encoded) != count*2:
                raise ValueError('Read-back length mismatch')
            checksum.update(bytes.fromhex(encoded))
        if checksum.hexdigest() != digest:
            raise ValueError('Read-back digest mismatch')
        return {'sha256': digest, 'size': len(content), 'readback_verified': True,
                'activated': False}

    def activate(self, digest):
        if not re.fullmatch('[0-9a-f]{64}', digest):
            raise ValueError('Invalid digest')
        self.version()
        if self.request(f'arCommit {digest}') != digest:
            raise ValueError('Commit acknowledgement mismatch')
        return {'sha256': digest, 'activated': True, 'path': '/switch/autorun-remote-test/probe.nro'}
