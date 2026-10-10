"""The hive of windowscodecs' own registrations in the runtime is what its source gives.

dlls/ntdll/unix/horizon_wic_registry.h is written by tools/make-wic-registry.py from
dlls/windowscodecs/regsvr.c and is kept in the repository; this fails when one was
changed without the other, and when the PNG decoder or its pattern is not in it.
"""
import subprocess, sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
header = root / 'dlls/ntdll/unix/horizon_wic_registry.h'
subprocess.run([sys.executable, str(root / 'horizon-wine/tools/make-wic-registry.py'), str(root), '--check', str(header)], check=True)
# The header holds the hive as a C string; read it back as the text the runtime loads.
hive = ''
for line in header.read_text(encoding='utf-8').splitlines():
    line = line.strip()
    if line.startswith('"') and line.endswith('"'):
        hive += line[1:-1].encode('latin-1').decode('unicode_escape')
decoders, png = '{7ED96837-96F0-4812-B211-F13C24117ED3}', '{389EA17B-5078-4CDE-B6EF-25C15175C751}'
assert '[Software\\\\Classes\\\\CLSID\\\\%s\\\\Instance\\\\%s]' % (decoders, png) in hive, 'the PNG decoder is not listed among the decoders'
assert '"Pattern"=hex:89,50,4e,47,0d,0a,1a,0a' in hive, 'the PNG signature is not among the patterns'
assert '"Pattern"=hex:ff,d8' in hive, 'the JPEG signature is not among the patterns'
print("The imaging component's registrations match dlls/windowscodecs/regsvr.c.")
