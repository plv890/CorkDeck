"""Run the host test suite in the fork's WSL mirror and record what passes.

    python3 "/mnt/d/My Personal Fork/builds/host_tests.py" <record-dir> [name-substring ...]

Each shell entry point (check-*-host.sh, check-horizon-dlls.sh, check-fshack-fsr.sh)
and each Python fixture (check_*.py) of horizon-wine/tests runs on its own with a
time limit. The result goes to builds/<record-dir>/host-tests.json with the last
lines of every failure, so two runs can be compared test by test.
"""
import json, os, subprocess, sys, time, glob
from pathlib import Path

MIRROR = Path('/opt/fork-build')
TESTS = MIRROR / 'horizon-wine/tests'
out = Path('/mnt/d/My Personal Fork/builds') / sys.argv[1]
out.mkdir(parents=True, exist_ok=True)
only = sys.argv[2:]

fonts = glob.glob('/usr/share/fonts/**/DejaVuSans.ttf', recursive=True) + \
        glob.glob('/usr/share/fonts/**/*.ttf', recursive=True)
env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
if fonts:
    env['LAUNCHER_FONT'] = fonts[0]

names = sorted(p.name for p in TESTS.glob('check-*.sh')) + sorted(p.name for p in TESTS.glob('check_*.py'))
if only:
    names = [n for n in names if any(o in n for o in only)]
results = {}
for name in names:
    # ASan binaries crash at start under this WSL kernel's 32-bit mmap
    # randomisation; the tests themselves do not depend on it.
    command = ['setarch', 'x86_64', '-R'] + (['sh', name] if name.endswith('.sh') else ['python3', name])
    start = time.time()
    try:
        run = subprocess.run(command, cwd=TESTS, env=env, capture_output=True, text=True, timeout=900, errors='replace')
        status, tail = ('pass' if run.returncode == 0 else 'fail'), (run.stdout + run.stderr)[-1500:]
    except subprocess.TimeoutExpired:
        status, tail = 'timeout', ''
    results[name] = {'status': status, 'seconds': round(time.time() - start, 1)}
    if status != 'pass':
        results[name]['tail'] = tail
    print(f'{status:8}{results[name]["seconds"]:7.1f}s  {name}', flush=True)

summary = {s: sum(1 for r in results.values() if r['status'] == s) for s in ('pass', 'fail', 'timeout')}
commit = subprocess.run(['git', '-C', '/mnt/d/My Personal Fork/source', 'rev-parse', 'HEAD'],
                        capture_output=True, text=True).stdout.strip()
(out / 'host-tests.json').write_text(json.dumps({'commit': commit, 'font': env.get('LAUNCHER_FONT'),
                                                 'summary': summary, 'tests': results}, indent=1) + '\n')
print(summary)
