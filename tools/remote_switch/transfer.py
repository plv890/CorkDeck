"""DBI FTP staging only. Does not replace or execute installed homebrew."""
from ftplib import FTP, error_perm
import hashlib
import io
import re
from pathlib import Path
from transport import lan_ip
from credentials import login

STAGING = '/switch/autorun-remote-test'
MAX_NRO = 64 * 1024 * 1024


def activate_test(host, digest, port=5000, timeout=5):
    """Only the separate test target. Caller must verify foreground DBI."""
    if not re.fullmatch(r'[0-9a-f]{64}', digest):
        raise ValueError('Invalid staged SHA-256')
    ftp = FTP(timeout=timeout)
    previous = None
    try:
        ftp.connect(lan_ip(host), port)
        login(ftp)
        ftp.cwd(STAGING)
        listing = []
        ftp.retrlines('LIST', listing.append)
        names = {line.rsplit(' ', 1)[-1] for line in listing}
        staged = 'test-' + digest + '.nro'
        if staged not in names:
            raise ValueError('Verified staging file missing')
        def remote_hash(name):
            hasher = hashlib.sha256()
            size = 0
            def receive(block):
                nonlocal size
                size += len(block)
                if size > MAX_NRO:
                    raise ValueError('Remote test file exceeds limit')
                hasher.update(block)
            ftp.retrbinary('RETR ' + name, receive)
            return hasher.hexdigest()
        if remote_hash(staged) != digest:
            raise ValueError('Staging digest changed; no activation')
        if 'probe.nro' in names:
            previous = 'previous-' + remote_hash('probe.nro') + '.nro'
            if previous in names:
                raise ValueError('Backup already exists; stop before overwrite')
            ftp.rename('probe.nro', previous)
        try:
            ftp.rename(staged, 'probe.nro')
        except Exception:
            if previous:
                ftp.rename(previous, 'probe.nro')
            raise
        return {'remote_path': STAGING + '/probe.nro', 'sha256': digest,
                'previous': previous, 'activated_test_only': True}
    finally:
        ftp.close()


def stage_nro(host, source, port=5000, timeout=5):
    source = Path(source)
    if source.suffix.lower() != '.nro' or not 0 < source.stat().st_size <= MAX_NRO:
        raise ValueError('Expected bounded test NRO')
    data = source.read_bytes()
    if len(data) < 20 or data[16:20] != b'NRO0':
        raise ValueError('Invalid NRO header')
    digest = hashlib.sha256(data).hexdigest()
    # Content-addressed, never a live forwarder target or user game path.
    filename = 'test-' + digest + '.nro'
    ftp = FTP(timeout=timeout)
    try:
        ftp.connect(lan_ip(host), port)
        login(ftp)
        try:
            ftp.cwd(STAGING)
        except error_perm:
            ftp.mkd(STAGING)
            ftp.cwd(STAGING)
        listing = []
        ftp.retrlines('LIST', listing.append)
        names = {line.rsplit(' ', 1)[-1] for line in listing}
        if filename in names or filename + '.part' in names:
            raise ValueError('Staged file already exists; do not overwrite')
        ftp.storbinary('STOR ' + filename + '.part', io.BytesIO(data))
        readback = hashlib.sha256()
        received = 0
        def receive(block):
            nonlocal received
            received += len(block)
            if received > len(data):
                raise ValueError('Remote readback exceeds expected size')
            readback.update(block)
        ftp.retrbinary('RETR ' + filename + '.part', receive)
        if received != len(data) or readback.hexdigest() != digest:
            raise ValueError('Readback mismatch; staged file NOT activated')
        ftp.rename(filename + '.part', filename)
        return {'remote_path': STAGING + '/' + filename,
                'sha256': digest, 'size': received, 'readback_verified': True,
                'installed_or_launched': False}
    finally:
        ftp.close()
