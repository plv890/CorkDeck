"""Build the fork's NRO in its own WSL mirror. Run inside WSL:

    python3 "/mnt/d/My Personal Fork/builds/fork_build.py" <record-dir> [--sync] [--touch FILE] [--target T]

The mirror (/opt/fork-build) is a Linux copy of the source tree with the build
directories; the container mounts it as /work. Every Git-tracked file of the
fork is compared with the mirror first. Without --sync a difference stops the
build; with --sync the differing files are copied in, files Git no longer
tracks are removed, and both lists are recorded. Outputs and the record go to
builds/<record-dir>/ (build.json, build.log, runtime.nro, runtime.elf).
"""
import argparse, hashlib, json, shutil, subprocess, sys, time
from pathlib import Path

WORKSPACE = Path('/mnt/d/My Personal Fork')
SOURCE = WORKSPACE / 'source'
MIRROR = Path('/opt/fork-build')
IMAGE = 'autorun/switch-dev:cumulative-20261007'
BUILD_DIR = 'horizon-wine/build-switch-amd64'
# Not part of what the NRO is built from.
SKIP = ('horizon-dlls', 'FORK.md', '.git')

parser = argparse.ArgumentParser()
parser.add_argument('record')
parser.add_argument('--sync', action='store_true')
parser.add_argument('--touch', action='append', default=[])
parser.add_argument('--target', default='wine-nx-runtime-nro')
parser.add_argument('--compare-only', action='store_true')
args = parser.parse_args()

out = WORKSPACE / 'builds' / args.record
out.mkdir(parents=True, exist_ok=True)
digest = lambda data: hashlib.sha256(data).hexdigest()


def git(*a):
    return subprocess.run(['git', '-C', str(SOURCE), *a], check=True, capture_output=True, text=True).stdout


tracked = [n for n in git('ls-files', '-z').split('\0') if n and not n.startswith(SKIP)]
changed, missing = [], []
for name in tracked:
    src, dst = SOURCE / name, MIRROR / name
    if src.is_symlink() or not src.is_file():
        continue
    data = src.read_bytes()
    if not dst.is_file():
        missing.append(name)
    elif dst.read_bytes() != data:
        changed.append(name)
# What the mirror still has of files the fork deleted from Git.
mirror_tracked = subprocess.run(['git', '-C', str(MIRROR), 'ls-files', '-z'], capture_output=True, text=True).stdout
stale = [n for n in mirror_tracked.split('\0')
         if n and not n.startswith(SKIP) and n not in set(tracked) and (MIRROR / n).is_file()]

record = {
    'when': time.strftime('%Y-%m-%dT%H:%M:%S%z'),
    'commit': git('rev-parse', 'HEAD').strip(),
    'dirty': bool(git('status', '--porcelain').strip()),
    'image': IMAGE, 'mirror': str(MIRROR), 'target': args.target,
    'tracked_files_compared': len(tracked),
    'differed_from_mirror': changed, 'absent_in_mirror': missing, 'stale_in_mirror': stale,
    'synced': False, 'files': {},
}
print(f'{len(tracked)} tracked files: {len(changed)} differ, {len(missing)} absent in the mirror, {len(stale)} stale there')
for label, names in (('differ', changed), ('absent', missing), ('stale', stale)):
    for name in names[:40]:
        print(f'  {label}: {name}')

if (changed or missing or stale) and not args.sync:
    (out / 'build.json').write_text(json.dumps(record, indent=1) + '\n')
    sys.exit('the mirror does not match the source; rerun with --sync to copy the differences in')
if args.sync:
    for name in changed + missing:
        (MIRROR / name).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(SOURCE / name, MIRROR / name)
    for name in stale:
        (MIRROR / name).unlink()
    record['synced'] = True
record['sources'] = {n: digest((SOURCE / n).read_bytes()) for n in changed + missing}
if args.compare_only:
    (out / 'build.json').write_text(json.dumps(record, indent=1) + '\n')
    sys.exit(0)

for name in args.touch:
    (MIRROR / name).touch()
record['touched'] = args.touch
with (out / 'build.log').open('w') as log:
    result = subprocess.run(['docker', 'run', '--rm', '-v', f'{MIRROR}:/work', '-w', '/work', IMAGE,
                             'cmake', '--build', BUILD_DIR, '--target', args.target, '-j4'],
                            stdout=log, stderr=subprocess.STDOUT)
record['exit'] = result.returncode
for ext in ('nro', 'elf'):
    built = MIRROR / BUILD_DIR / f'wine-nx-runtime.{ext}'
    if result.returncode == 0 and built.is_file():
        shutil.copy2(built, out / f'runtime.{ext}')
        data = built.read_bytes()
        record['files'][ext] = {'size': len(data), 'sha256': digest(data)}
(out / 'build.json').write_text(json.dumps(record, indent=1) + '\n')
print(json.dumps(record['files'], indent=1))
sys.exit(result.returncode)
