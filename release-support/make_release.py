"""Puts a build together as a package for somebody else's card:

    release/<name>/switch/wine/...      the clean tree with this build's NRO
    release/<name>/README-INSTALL.txt   how to install it
    release/<name>.zip                  the same, to hand over

Usage: make_release.py BUILD_FOLDER NAME README
The clean tree is the one builds/2026-10-09-corkdeck-0.1.0/make_card.sh staged
(the base files Wine reads beside the runtime; nobody's settings, games,
sessions or logs). The local Steamworks replacement this project builds goes in
too; files that are somebody's own or of unknown licence do not."""
import hashlib, shutil, sys, zipfile
from pathlib import Path

root = Path(__file__).resolve().parent.parent
build, name, readme = root / 'builds' / sys.argv[1], sys.argv[2], Path(sys.argv[3])
tree = root / 'builds/2026-10-09-corkdeck-0.1.0/sd-card/switch/wine'
steam = root / 'builds/console/steamworks-local'
out = root / 'release' / name
if out.exists():
    shutil.rmtree(out)
wine = out / 'switch/wine'
shutil.copytree(tree, wine)
shutil.copyfile(build / 'runtime.nro', wine / 'wine-nx-runtime.nro')
# Built by this project from open sources; their notices travel with them.
# The Steam API libraries are inside the NRO since 0.2f and are written to the
# card by the application; only the old client's stand-in still travels here.
for item in ('COPYING.LESSER', 'SDK-NOTICE.txt', 'legacy-client/steamclient-local.dll'):
    target = wine / 'steamworks-local' / item
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(steam / item, target)
shutil.copyfile(readme, out / 'README-INSTALL.txt')
digest = hashlib.sha256((wine / 'wine-nx-runtime.nro').read_bytes()).hexdigest()
files = sorted(p for p in out.rglob('*') if p.is_file())
archive = root / 'release' / (name + '.zip')
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
    for p in files:
        z.write(p, p.relative_to(out).as_posix())
print('NRO', digest)
print(len(files), 'files,', sum(p.stat().st_size for p in files) >> 20, 'MB;', archive.name, archive.stat().st_size >> 20, 'MB')
for bad in ('settings.json', 'steam-session.vault', 'launcher-library-v2.ini', 'autorun-steam.ini', 'target.txt'):
    assert not list(out.rglob(bad)), bad
