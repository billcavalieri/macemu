#!/usr/bin/env python3
"""Run the remaining New World 68k rollout gates sequentially on private VMs.

Requires diagnostic/Release ARM64 apps, an installed 1024x768 English Mac OS 9
seed and a QuickTime-compatible movie. No original media is modified. Fail fast
on any stage; retain logs, executable hashes, manifests and private disk state.
This runner never changes the app's default preference or claims physical mouse
capture qualification. On macOS, use caffeinate -i to keep the host awake for
active/idle soaks while allowing the display to sleep.
"""
import argparse
import json
import os
import signal
import statistics
from pathlib import Path
import subprocess
import sys
import time


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--debug-app', type=Path, required=True)
    p.add_argument('--release-app', type=Path, required=True)
    p.add_argument('--prefs', type=Path, required=True)
    p.add_argument('--movie', type=Path, required=True)
    p.add_argument('--movie-region', type=int, nargs=4, required=True)
    p.add_argument('--work-dir', type=Path, required=True)
    args = p.parse_args()
    for path in (args.debug_app, args.release_app, args.prefs, args.movie):
        path.resolve(strict=True)
    root = args.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=False)
    if ' ' in str(root):
        p.error('NW_SCRIPT requires artifact paths without spaces')
    timing = root / 'timing.script'
    timing.write_text('\n'.join([
        'waitfinder', 'waitpixel 976 116 ffffff', 'phase',
        'at 2 click 100 600', 'at 5 dump 910 8 {work}/finder.bin',
        'at 12 bench68 500000 {work}/benchmark.csv',
        'at 30 mouse 800 600', 'at 50 mouse 300 150',
        'at 80 dump 910 8 {work}/finder.bin',
        'at 88 log qualification workload completed',
    ]) + '\n')
    stages = [
        ('release-timing', args.release_app, ['--modes', 'off,on', '--hist', 'off',
            '--script', str(timing), '--seconds', '90', '--repeat', '3'], False),
        ('events', args.debug_app, ['--modes', 'off,on,verify', '--workload', 'extfs',
            '--seconds', '360', '--interrupts', '10', '--return-interrupts', '10'], True),
        ('lifecycle', args.debug_app, ['--modes', 'off,on', '--workload', 'lifecycle',
            '--seconds', '900'], False),
        ('movie', args.debug_app, ['--modes', 'off,on', '--workload', 'quicktime',
            '--seconds', '300', '--movie', str(args.movie.resolve()),
            '--movie-region'] + list(map(str, args.movie_region)), False),
        ('active-2h', args.debug_app, ['--modes', 'on', '--workload', 'extfs',
            '--seconds', '7200'], False),
        ('idle-8h', args.debug_app, ['--modes', 'on', '--workload', 'idle',
            '--seconds', '28800'], False),
    ]
    runner = Path(__file__).with_name('qualify_nw68.py').resolve()
    status = {'started': time.time(), 'stages': [], 'complete': False}
    def save():
        temporary = root / 'status.json.tmp'
        temporary.write_text(json.dumps(status, indent=2) + '\n')
        temporary.replace(root / 'status.json')
    save()
    for name, app, extra, compare_blocks in stages:
        entry = {'name': name, 'started': time.time(), 'state': 'running',
                 'artifacts': str(root / name), 'log': str(root / (name + '.out'))}
        status['stages'].append(entry)
        save()
        print('Starting:', name, flush=True)
        env = os.environ.copy()
        # Ambient diagnostic overrides must not silently change qualification.
        for key in ('NW_JIT68K_BLOCKS', 'NW_JIT68K_DECODED', 'NW_JIT68K_VERIFY_WINDOW',
                    'NW_JIT68K_CHECK_BLOCKS', 'NW_SCRIPT', 'NW_SCRIPT_RESTART',
                    'NW_TEST_EXTFS_IRQS', 'NW_TEST_RETURN_IRQS'):
            env.pop(key, None)
        if compare_blocks:
            env['NW_JIT68K_CHECK_BLOCKS'] = '1'
        command = [sys.executable, str(runner), '--app', str(app.resolve()),
                   '--prefs', str(args.prefs.resolve()), '--ppc', 'on',
                   '--boot-timeout', '900', '--work-dir', str(root / name)] + extra
        entry['command'] = command
        save()
        with (root / (name + '.out')).open('w') as output:
            try:
                child = subprocess.Popen(command, env=env, stdout=output,
                                         stderr=subprocess.STDOUT, start_new_session=True)
                entry['pid'] = child.pid
                save()
                try:
                    code = child.wait()
                except BaseException:
                    # This group contains only this stage and its private VMs.
                    # Give the runner's finally block time to close its guest.
                    os.killpg(child.pid, signal.SIGINT)
                    try:
                        child.wait(timeout=20)
                    except subprocess.TimeoutExpired:
                        os.killpg(child.pid, signal.SIGKILL)
                        child.wait()
                    raise
            except BaseException:
                entry.update(state='interrupted', finished=time.time())
                save()
                raise
        if code == 0 and name == 'release-timing':
            results = json.loads((root / name / 'results.json').read_text())
            off = [r for r in results if r['mode'] == 'off']
            on = [r for r in results if r['mode'] == 'on']
            medians = lambda runs: [statistics.median(int(s['microseconds']) for s in r['benchmark_samples']) for r in runs]
            reference, native = medians(off), medians(on)
            ratios = [a / b for a, b in zip(reference, native)]
            coverage = min(int(s['native']) / int(s['instructions']) for r in on for s in r['benchmark_samples'])
            off_boot = statistics.median(r['boot_seconds'] for r in off)
            on_boot = statistics.median(r['boot_seconds'] for r in on)
            entry['performance'] = dict(arithmetic_speedups=ratios,
                minimum_loop_native_coverage=coverage,
                reference_boot_median=off_boot, native_boot_median=on_boot,
                # Allow small readiness noise, reject a material regression.
                boot_regression_allowance=max(2.0, off_boot * .05))
            if len(ratios) != 3 or min(ratios) <= 1.0 or coverage < .9 or on_boot > off_boot + max(2.0, off_boot * .05):
                code = 1
                entry['failure'] = 'Release speedup, 90% loop coverage, or mixed-boot regression gate failed'
        entry.update(state='passed' if code == 0 else 'failed', exit_status=code, finished=time.time())
        save()
        print(name, entry['state'], flush=True)
        if code:
            raise SystemExit(code)
    status.update(complete=True, finished=time.time())
    save()
    print('Automated gates passed. Review artifacts and physical mouse qualification before rollout.', flush=True)


if __name__ == '__main__':
    main()
