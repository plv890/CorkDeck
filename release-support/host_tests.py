"""Run the host test suite in the fork's WSL mirror and record what passes.

    python3 "/mnt/d/My Personal Fork/builds/host_tests.py" <record-dir> [--jobs N] [name-substring ...]

Each shell entry point (check-*-host.sh, check-horizon-dlls.sh, check-fshack-fsr.sh)
and each Python fixture (check_*.py) of horizon-wine/tests runs on its own with a
time limit. The result goes to builds/<record-dir>/host-tests.json with the last
lines of every failure, so two runs can be compared test by test. A test that
exits with 77 says it cannot run on this machine and is counted as skipped.
What the tests need beside the mirror is put in place by host_test_env.sh.

Several tests run at the same time (--jobs, 6 unless told). One that fails
beside the others is run once more alone before it is called a failure, so a
test that only lost a race for a port or for the processor does not count.
"""
import json, os, subprocess, sys, time, glob
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

MIRROR = Path('/opt/fork-build')
TESTS = MIRROR / 'horizon-wine/tests'
arguments = sys.argv[1:]
jobs = 6
if '--jobs' in arguments:
    at = arguments.index('--jobs')
    jobs = int(arguments[at + 1])
    del arguments[at:at + 2]
out = Path('/mnt/d/My Personal Fork/builds') / arguments[0]
out.mkdir(parents=True, exist_ok=True)
only = arguments[1:]

fonts = glob.glob('/usr/share/fonts/**/DejaVuSans.ttf', recursive=True) + \
        glob.glob('/usr/share/fonts/**/*.ttf', recursive=True)
env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
if fonts:
    env['LAUNCHER_FONT'] = fonts[0]

names = sorted(p.name for p in TESTS.glob('check-*.sh')) + sorted(p.name for p in TESTS.glob('check_*.py'))
if only:
    names = [n for n in names if any(o in n for o in only)]


def run_test(name):
    # ASan binaries crash at start under this WSL kernel's 32-bit mmap
    # randomisation; the tests themselves do not depend on it.
    command = ['setarch', 'x86_64', '-R'] + (['sh', name] if name.endswith('.sh') else ['python3', name])
    start = time.time()
    try:
        run = subprocess.run(command, cwd=TESTS, env=env, capture_output=True, text=True, timeout=900, errors='replace')
        # 77 is a test saying it cannot run here (tests/arm64_host.py): not a failure.
        status = {0: 'pass', 77: 'skip'}.get(run.returncode, 'fail')
        tail = (run.stdout + run.stderr)[-1500:]
    except subprocess.TimeoutExpired:
        status, tail = 'timeout', ''
    result = {'status': status, 'seconds': round(time.time() - start, 1)}
    if status != 'pass':
        result['tail'] = tail
    return result


def report(name, result, note=''):
    print(f'{result["status"]:8}{result["seconds"]:7.1f}s  {name}{note}', flush=True)


results = {}
with ThreadPoolExecutor(max(1, jobs)) as pool:
    for name, result in zip(names, pool.map(run_test, names)):
        results[name] = result
        if result['status'] in ('pass', 'skip') or jobs <= 1:
            report(name, result)
for name in names:
    if jobs > 1 and results[name]['status'] in ('fail', 'timeout'):
        beside = results[name]
        results[name] = run_test(name)
        if results[name]['status'] == 'pass':
            results[name]['failed_beside_others'] = beside.get('tail', '')[-600:]
        report(name, results[name], '  (alone; it failed beside the others)')

summary = {s: sum(1 for r in results.values() if r['status'] == s) for s in ('pass', 'skip', 'fail', 'timeout')}
commit = subprocess.run(['git', '-C', '/mnt/d/My Personal Fork/source', 'rev-parse', 'HEAD'],
                        capture_output=True, text=True).stdout.strip()
(out / 'host-tests.json').write_text(json.dumps({'commit': commit, 'font': env.get('LAUNCHER_FONT'), 'jobs': jobs,
                                                 'summary': summary, 'tests': results}, indent=1) + '\n')
print(summary)
