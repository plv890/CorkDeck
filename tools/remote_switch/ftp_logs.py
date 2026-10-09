"""Direct bounded saved-log reads, independent of Autorun lifetime."""
from ftplib import FTP, error_perm
import json
import re
from credentials import login
from diagnostics import NAME
from transport import lan_ip

TAIL = 16384
SENSITIVE = re.compile(r'password|passwd|token|cookie|authorization|steam.?guard|'
                       r'secret|credential|bearer|account_name|ticket|steamid|https?://', re.I)


def redact_tail(data, truncated=False):
    if truncated:
        data = data.partition(b'\n')[2]
    output = []
    # Keep complete lines only: incomplete fragments can conceal a secret prefix.
    for line in data.splitlines(keepends=True):
        if not line.endswith(b'\n'):
            break
        text = line.decode('utf-8', errors='replace')
        output.append('[REDACTED sensitive line]\n' if SENSITIVE.search(text) else text)
    return ''.join(output).encode('utf-8')


def collect(host, folder, port=5001, timeout=5, selected=None, whole=False):
    if selected is not None:
        if not 1 <= len(selected) <= 4 or any(
                not NAME.fullmatch(n) or '..' in n or not n.endswith('.log') for n in selected):
            raise ValueError('Invalid selected log names')
    if whole and selected is None:
        raise ValueError('Whole reads require explicitly selected game logs')
    bound = 512 * 1024 if whole else TAIL
    ftp = FTP(timeout=timeout)
    results = []
    try:
        ftp.connect(lan_ip(host), port)
        login(ftp)
        ftp.cwd('/switch/wine/logs')
        listing = []
        def add(line):
            if len(listing) >= 128:
                raise ValueError('Log directory exceeds listing limit')
            listing.append(line)
        if selected is None:
            ftp.retrlines('LIST', add)
        names = list(dict.fromkeys(selected)) if selected is not None else [
            line.rsplit(' ', 1)[-1] for line in listing if line.startswith('-')]
        names = [name for name in names if NAME.fullmatch(name) and '..' not in name
                 and (name.endswith('.log') or name in ('stdout.txt', 'stderr.txt'))]
        if len(names) > 32:
            raise ValueError('Too many log files')
        candidates = [('/switch/wine/logs/' + name, name) for name in names]
        # Runtime writes this beside per-game logs, not in the wine root.
        # It may be absent from LIST while the writer is still open.
        if selected is None and 'autorun_runtime.log' not in names:
            candidates.append(('/switch/wine/logs/autorun_runtime.log', 'autorun_runtime.log'))
        for remote, local in candidates:
            try:
                ftp.voidcmd('TYPE I')
                size = ftp.size(remote)
                if size is None or size < 0:
                    raise ValueError('Unknown log size')
                if whole and size > bound:
                    results.append({'name': local, 'status': 'too-large', 'source_size': size})
                    continue
                offset = 0 if whole else max(0, size - bound)
                data = bytearray()
                def receive(block):
                    if len(data) + len(block) > bound:
                        raise ValueError('Log grew beyond bounded tail; stop collection')
                    data.extend(block)
                ftp.retrbinary('RETR ' + remote, receive, rest=offset if offset else None)
                filtered = redact_tail(bytes(data), truncated=bool(offset))
                (folder / local).write_bytes(filtered)
                results.append({'name': local, 'source_bytes': len(data),
                                'saved_bytes': len(filtered), 'status': 'received'})
            except error_perm as exc:
                results.append({'name': local, 'ftp_status': str(exc)[:3], 'status': 'unavailable'})
            (folder / 'collection.json').write_text(json.dumps(results, indent=2)+'\n', encoding='utf-8')
        return results
    finally:
        ftp.close()
