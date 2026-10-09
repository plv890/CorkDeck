from pathlib import Path
import subprocess,shutil,json,hashlib
base=Path('/mnt/d/autorun fork/artifacts/cumulative-20261007')
root=base.parents[1]/'autorun';mirror=Path('/opt/autorun-build')
for name in ['horizon-wine/source/thread_profile.c','horizon-wine/source/runtime.c',
             'horizon-wine/source/steam/steam_diagnostics.h','dlls/ntdll/unix/horizon.c',
             'horizon-wine/source/shader_ram_cache.c','horizon-wine/source/shader_ram_cache.h',
             'horizon-wine/CMakeLists.txt']:
    shutil.copy2(root/name,mirror/name)
test=base/'cache-test'
subprocess.run(['cc','-pthread','-fsanitize=address,undefined','-g','-o',str(test),
                str(root/'horizon-wine/tests/shader_ram_cache.c'),
                str(root/'horizon-wine/source/shader_ram_cache.c')],check=True)
subprocess.run([str(test)],check=True)
shutil.copy2(base.parents[1]/'references/mesa-switch/src/util/disk_cache.c',base/'disk_cache.c')
compile_command='''set -e
cp /artifact/disk_cache.c /tmp/mesa-switch/src/util/disk_cache.c
ninja -C /tmp/mesa-build src/util/libmesa_util.a.p/disk_cache.c.o
cp /tmp/mesa-build/src/util/libmesa_util.a.p/disk_cache.c.o /artifact/disk_cache.c.o
'''
with (base/'mesa-build.log').open('w') as log:
    subprocess.run(['docker','run','--rm','-v',str(base)+':/artifact',
                    'autorun/switch-dev:four-core-build-checkpoint','bash','-c',compile_command],
                    stdout=log,stderr=subprocess.STDOUT,check=True)
container='autorun-cumulative-20261007';image='autorun/switch-dev:cumulative-20261007'
subprocess.run(['docker','create','--name',container,'-v',str(base)+':/artifact',
               '-v','/opt/autorun-build:/work','-w','/work',
               'autorun/switch-dev:four-core-20261007','bash','-c',
               'python3 /artifact/patch-sdk.py && cmake --build horizon-wine/build-switch-amd64 --target wine-nx-runtime-nro -j4'],check=True)
with (base/'build.log').open('w') as log:
    subprocess.run(['docker','start','-a',container],stdout=log,stderr=subprocess.STDOUT,check=True)
state=json.loads(subprocess.check_output(['docker','inspect',container]))[0]['State']
if state['ExitCode']:
    print('\n'.join((base/'build.log').read_text().splitlines()[-35:]));raise SystemExit(state['ExitCode'])
for ext in ['nro','elf']:
    shutil.copy2(mirror/f'horizon-wine/build-switch-amd64/wine-nx-runtime.{ext}',base/f'runtime-cumulative.{ext}')
data=(base/'runtime-cumulative.nro').read_bytes()
record={'revision':'2026.10.07-7','sha256':hashlib.sha256(data).hexdigest(),'size':len(data),
        'sdk_image':image,'host_cache_tests':'ASan/UBSan passed'}
(base/'build.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record),flush=True)
subprocess.run(['docker','commit',container,image],check=True)
