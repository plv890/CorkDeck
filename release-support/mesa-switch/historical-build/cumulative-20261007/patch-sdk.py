from pathlib import Path
import subprocess,hashlib,json
base=Path('/artifact')
obj=base/'disk_cache.c.o'
assert 'AArch64' in subprocess.check_output(['aarch64-none-elf-readelf','-h',str(obj)],text=True)
record=[]
for archive in Path('/opt/devkitpro/portlibs/switch/lib').glob('*.a'):
    members=subprocess.check_output(['aarch64-none-elf-ar','t',str(archive)],text=True).splitlines()
    count=members.count('disk_cache.c.o')
    if not count:continue
    assert count==1,(archive,count)
    before=hashlib.sha256(archive.read_bytes()).hexdigest()
    subprocess.run(['aarch64-none-elf-ar','r',str(archive),str(obj)],check=True)
    subprocess.run(['aarch64-none-elf-ranlib',str(archive)],check=True)
    assert subprocess.check_output(['aarch64-none-elf-ar','p',str(archive),'disk_cache.c.o'])==obj.read_bytes()
    record.append({'archive':archive.name,'before':before,
                   'after':hashlib.sha256(archive.read_bytes()).hexdigest()})
assert {'libmesa_util.a','libvulkan.a'} <= {row['archive'] for row in record}
(base/'sdk-patch.json').write_text(json.dumps(record,indent=2)+'\n')
print('SDK object verified in',', '.join(row['archive'] for row in record),flush=True)

