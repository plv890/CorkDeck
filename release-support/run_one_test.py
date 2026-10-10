"""Run host tests in the WSL mirror and show their whole output. Run inside WSL:

    python3 builds/run_one_test.py <test-file>[,<test-file>...] [--lines N] [source-file ...]

The tests directory and every listed source file (paths relative to source/)
are copied into the mirror first, so an edit can be tried without a full
comparison of the tree. Several tests run at the same time. The environment is
the one host_tests.py uses.
"""
import glob, os, shutil, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

SOURCE = Path('/mnt/d/My Personal Fork/source')
MIRROR = Path('/opt/fork-build')
TESTS = MIRROR / 'horizon-wine/tests'

args = sys.argv[1:]
lines = 60
if '--lines' in args:
    at = args.index('--lines')
    lines = int(args[at + 1])
    del args[at:at + 2]
names, extra = args[0].split(','), args[1:]

for src in (SOURCE / 'horizon-wine/tests').iterdir():
    if src.is_file():
        dst = TESTS / src.name
        if not dst.is_file() or dst.read_bytes() != src.read_bytes():
            shutil.copy2(src, dst)
for rel in extra:
    (MIRROR / rel).parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(SOURCE / rel, MIRROR / rel)

fonts = glob.glob('/usr/share/fonts/**/DejaVuSans.ttf', recursive=True) + \
        glob.glob('/usr/share/fonts/**/*.ttf', recursive=True)
env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
if fonts:
    env['LAUNCHER_FONT'] = fonts[0]


def one(name):
    command = ['setarch', 'x86_64', '-R'] + (['sh', name] if name.endswith('.sh') else ['python3', name])
    run = subprocess.run(command, cwd=TESTS, env=env, capture_output=True, text=True, errors='replace')
    text = [l for l in (run.stdout + run.stderr).splitlines()
            if not l.startswith(('Traceback', '  File ', '    ', '      |'))]
    shown = text[-lines:] if lines > 0 else text[:-lines]
    return '\n'.join(l[:240] for l in shown) + f'\n--- {name}: exit {run.returncode}, {len(text)} lines\n'


with ThreadPoolExecutor(8) as pool:
    for report in pool.map(one, names):
        print(report)
