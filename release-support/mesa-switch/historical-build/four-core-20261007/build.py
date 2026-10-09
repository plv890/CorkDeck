from pathlib import Path
import subprocess, shutil, hashlib, json

base=Path('/mnt/d/autorun fork/artifacts/four-core-20261007')
root=base.parents[1]/'autorun'
mirror=Path('/opt/autorun-build')
files=['horizon-wine/source/thread_profile.c','horizon-wine/source/thread_profile.h',
       'horizon-wine/source/runtime.c','horizon-wine/source/steam/steam_diagnostics.h',
       'dlls/ntdll/unix/horizon.c']
for name in files: shutil.copy2(root/name,mirror/name)
test=base/'thread-profile-test'
subprocess.run(['cc','-fsanitize=address,undefined','-g','-o',str(test),
                str(root/'horizon-wine/tests/thread_profile.c')],check=True)
subprocess.run([str(test)],check=True)
container='autorun-four-core-20261007-v2'
image='autorun/switch-dev:four-core-20261007'
command='''set -euo pipefail
mkdir -p /tmp/mesa-switch
tar -C /source --exclude=./.git -cf - . | tar -C /tmp/mesa-switch -xf -
cd /tmp/mesa-switch
export MESA_SWITCH_RUST_TARGET=aarch64-unknown-linux-gnu
bash /usr/local/share/switch-dev/mesa-build.sh
cd /work
cmake --build horizon-wine/build-switch-amd64 --target wine-nx-runtime-nro -j4
'''
subprocess.run(['docker','create','--name',container,
    '-v',str(base.parents[1]/'references/mesa-switch')+':/source:ro',
    '-v','/opt/autorun-build:/work','-w','/work',
    'autorun/switch-dev:2026.09.28.4-fixed','bash','-c',command],check=True)
with (base/'build.log').open('w') as log:
    result=subprocess.run(['docker','start','-a',container],stdout=log,stderr=subprocess.STDOUT)
state=json.loads(subprocess.check_output(['docker','inspect',container]))[0]['State']
if result.returncode or state['ExitCode']:
    print('\n'.join((base/'build.log').read_text().splitlines()[-45:]))
    raise SystemExit(state['ExitCode'] or result.returncode)
subprocess.run(['docker','commit',container,image],check=True)
for ext in ['nro','elf']:
    shutil.copy2(mirror/f'horizon-wine/build-switch-amd64/wine-nx-runtime.{ext}',
                 base/f'runtime-four-core.{ext}')
data=(base/'runtime-four-core.nro').read_bytes()
record={'revision':'2026.10.07-6','size':len(data),'sha256':hashlib.sha256(data).hexdigest(),
        'sdk_image':image,'policy':4,'host_test':'ASan/UBSan passed'}
(base/'build.json').write_text(json.dumps(record,indent=2)+'\n')
print(json.dumps(record))
