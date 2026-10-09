#!/usr/bin/env python3
"""Run inside WSL; build only our minimal PE layer with pinned GBE ABI headers."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys
import argparse

# The fork's workspace and its own build mirror; the original's are left alone.
root=Path('/mnt/d/My Personal Fork')
repo=root/'source'
mirror=Path('/opt/fork-build')
reference=root/'references/gbe_fork'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,default=root/'artifacts/steamworks-minimal')
parser.add_argument('--source',type=Path,default=repo/'tools/steamworks',help='Folder of local.cpp, probe.cpp and generate.py to build')
parser.add_argument('--with-client-adapter',action='store_true',help='Build separate experimental explicit offline client role')
args=parser.parse_args()
out=args.output.resolve()
if args.with_client_adapter and out==root/'artifacts/steamworks-minimal':
    raise SystemExit('Client adapter requires a separate experimental --output directory')
work=mirror/('steamworks-client-experimental' if args.with_client_adapter else 'steamworks-local')
pin='7103add7ca6ef3ef8353279219880068f2494fc8'
actual=subprocess.check_output(['git','-C',str(reference),'rev-parse','HEAD'],text=True).strip()
if actual!=pin:raise SystemExit('GBE reference revision differs from build pin')
work.mkdir(parents=True,exist_ok=True);out.mkdir(parents=True,exist_ok=True)
source_names=['local.cpp','probe.cpp','generate.py','build.py']+(['client_probe.cpp'] if args.with_client_adapter else [])
source_hashes={name:hashlib.sha256(((repo/'tools/steamworks' if name=='build.py' else args.source)/name).read_bytes()).hexdigest() for name in source_names}
(out/'build.json').write_text(json.dumps({'state':'building','source_hashes':source_hashes})+'\n')
for filename in ('local.cpp','probe.cpp')+ (('client_probe.cpp',) if args.with_client_adapter else ()):shutil.copyfile(args.source/filename,work/filename)
image='autorun/switch-dev:2026.09.28.4-fixed'
base=['docker','run','--rm','-v',str(mirror)+':/work','-v',str(reference/'sdk')+':/sdk:ro',image]
bin='/work/horizon-wine/toolchains/llvm-mingw-20260505-ucrt-ubuntu-22.04-x86_64/bin/'
records=[]
for arch in ('x86_64','i686'):
    generated=work/('generated-'+arch);generated.mkdir(exist_ok=True)
    target='steam_api64.dll' if arch=='x86_64' else 'steam_api.dll'
    compiler=bin+arch+'-w64-mingw32-clang++'
    with (generated/'effective.h').open('w') as f:
        subprocess.run(base+[compiler,'-std=c++17','-E','-P','-DSTEAM_WIN32','-DSTEAM_API_EXPORTS','-DSTEAM_API_FUNCTIONS_IMPL','-I/sdk','/sdk/steam/steam_api.h'],stdout=f,check=True)
    subprocess.run([sys.executable,str(args.source/'generate.py'),str(reference),str(generated),'--preprocessed',str(generated/'effective.h')],check=True)
    flags=['-std=c++17','-O2','-static','-ferror-limit=8','-I/sdk','-I/work/'+work.name+'/'+generated.name]
    container_work='/work/'+work.name+'/'
    log=out/('build-'+arch+'.log')
    with log.open('w') as f:
        result=subprocess.run(base+[compiler,*flags,'-shared','-Wl,--strip-all,--icf=all',container_work+'local.cpp','-o',container_work+target],stdout=f,stderr=f)
    if result.returncode:print(log.read_text()[-5000:]);raise SystemExit(result.returncode)
    with log.open('a') as f:
        result=subprocess.run(base+[compiler,*flags,container_work+'probe.cpp','-o',container_work+'probe-'+arch+'.exe'],stdout=f,stderr=f)
    if result.returncode:print(log.read_text()[-5000:]);raise SystemExit(result.returncode)
    for name in (target,'probe-'+arch+'.exe'):shutil.copyfile(work/name,out/name)
    data=(out/target).read_bytes();records.append({'file':target,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'architecture':arch})
    print(target,len(data),flush=True)
    if args.with_client_adapter:
        adapter='steamclient-local64.dll' if arch=='x86_64' else 'steamclient-local.dll'
        with log.open('a') as f:
            result=subprocess.run(base+[compiler,*flags,'-DAUTORUN_LOCAL_CLIENT_ADAPTER','-shared','-Wl,--strip-all,--icf=all',container_work+'local.cpp','-o',container_work+adapter],stdout=f,stderr=f)
        if result.returncode:print(log.read_text()[-5000:]);raise SystemExit(result.returncode)
        with log.open('a') as f:
            result=subprocess.run(base+[compiler,*flags,container_work+'client_probe.cpp','-o',container_work+'client-probe-'+arch+'.exe'],stdout=f,stderr=f)
        if result.returncode:print(log.read_text()[-5000:]);raise SystemExit(result.returncode)
        for name in (adapter,'client-probe-'+arch+'.exe'):shutil.copyfile(work/name,out/name)
        data=(out/adapter).read_bytes();records.append({'file':adapter,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'architecture':arch,'role':'explicit offline client adapter; not Valve client'})
        print(adapter,len(data),flush=True)
(out/'build.json').write_text(json.dumps({'state':'complete','source_hashes':source_hashes,'gbe_revision':pin,'configuration':'local minimal; no networking, overlay or worker','files':records},indent=2)+'\n')
shutil.copyfile(reference/'LICENSE',out/'COPYING.LESSER')
shutil.copyfile(reference/'sdk/Readme.txt',out/'SDK-NOTICE.txt')
