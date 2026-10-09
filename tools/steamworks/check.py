"""Run our synthetic PE probes on Windows, never the installed games."""
from pathlib import Path
import datetime
import hashlib
import json
import shutil
import subprocess
import argparse
from pe import inspect

default_root=Path(__file__).resolve().parents[3]/'artifacts/steamworks-minimal'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,default=default_root)
root=parser.parse_args().output.resolve()
manifest=json.loads((root/'build.json').read_text())
if manifest.get('state')!='complete':raise RuntimeError('No complete current build')
for name,digest in manifest['source_hashes'].items():
    if hashlib.sha256((Path(__file__).parent/name).read_bytes()).hexdigest()!=digest:raise RuntimeError('Build is stale: '+name)
for record in manifest['files']:
    if hashlib.sha256((root/record['file']).read_bytes()).hexdigest()!=record['sha256']:raise RuntimeError('Payload hash mismatch')
folder=root/('check-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ'))
folder.mkdir()
results=[]
for record in manifest['files']:
    arch,name=record['architecture'],record['file']
    adapter=record.get('role') is not None
    probe=('client-probe-' if adapter else 'probe-')+arch+'.exe'
    work=folder/(arch+('-client' if adapter else ''));work.mkdir()
    for f in (name,probe):shutil.copyfile(root/f,work/f)
    dll=inspect(work/name)
    forbidden=[x for x in dll['imports'] if any(k in x.lower() for k in ('ws2_32','wininet','winhttp','curl','opus','portaudio','sdl'))]
    if forbidden:raise RuntimeError('Network/voice dependencies: '+str(forbidden))
    if adapter:
        if not {'CreateInterface','AutorunLocalClientAdapterRole'}<=set(dll['exports']):raise RuntimeError('Missing explicit adapter role/factory')
    elif {'CreateInterface','AutorunLocalClientAdapterRole'}&set(dll['exports']):raise RuntimeError('Normal SDK exposes adapter role')
    if arch=='x86_64' and not adapter:
        needed=inspect(default_root/'reference/Balatro-luasteam.dll')['imports']['steam_api64.dll']
        missing=set(needed)-set(dll['exports'])
        if missing:raise RuntimeError('Missing SDK imports: '+str(sorted(missing)))
    process=subprocess.run([str(work/probe),str(work/name)],cwd=work,capture_output=True,text=True,timeout=30)
    item={'architecture':arch,'file':name,'code':process.returncode,'stdout':process.stdout,'stderr':process.stderr,'imports':sorted(dll['imports']),'exports':len(dll['exports'])}
    results.append(item);print(name,process.returncode,process.stdout.strip())
    (folder/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    if process.returncode:raise SystemExit(process.returncode)
print(folder)
