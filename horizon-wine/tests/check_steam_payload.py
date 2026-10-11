"""The local Steam library carried inside the application is the one the application expects.

assets/steamworks/steam_api.dll and steam_api64.dll are embedded in the runtime and put on the
card when the card's are not this build's; source/steam/steam_local.c names their size and
SHA-256. One changed without the other would leave every Steam game without its library.
"""
import hashlib, re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
text = (root / 'source/steam/steam_local.c').read_text(encoding='utf-8')
for name in ('steam_api.dll', 'steam_api64.dll'):
    line = next(l for l in text.splitlines() if l.strip().startswith('{"%s"' % name))
    expected = re.match(r'\s*\{"[^"]+","([0-9a-f]{64})"', line).group(1)
    size = int(re.search(r'\},\s*(\d+)\s*,', line).group(1))
    data = (root / 'assets/steamworks' / name).read_bytes()
    assert len(data) == size, '%s: %d bytes carried, %d expected' % (name, len(data), size)
    assert hashlib.sha256(data).hexdigest() == expected, name + ': the carried file is not the one steam_local.c names'
line = next(l for l in text.splitlines() if 'static const char client_hash[]' in l)
expected = re.search(r'"([0-9a-f]{64})"', line).group(1)
size = int(re.search(r'#define STEAM_CLIENT_LOCAL_SIZE (\d+)u', text).group(1))
data = (root / 'assets/steamworks/steamclient-local.dll').read_bytes()
assert len(data) == size, 'steamclient-local.dll: %d bytes carried, %d expected' % (len(data), size)
assert hashlib.sha256(data).hexdigest() == expected, 'steamclient-local.dll: the carried file is not the one steam_local.c names'
print('The carried local Steam library is the one the application expects.')
