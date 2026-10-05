#!/usr/bin/env python3
"""Run isolated New World 68k qualification; retain every artifact.

Requires macOS/APFS, user-supplied guest images and a diagnostic app.
A positional .sheepvm bundle isolates prefs, NVRAM and flash; HOME is unchanged.
The default workload samples boot and mouse delivery. Supply --script for
application/audio/file workloads and --seconds for active or idle soaks.
"""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time

def clone(source, destination):
    subprocess.run(['cp', '-c', str(source), str(destination)], check=True)

def changed_movie_pixels(work, region):
    """Compare the movie content, excluding player controls and the cursor."""
    if region is None:
        return None
    images = []
    for name in ('playing-1.ppm', 'playing-2.ppm'):
        path = work / name
        if not path.exists():
            return 0
        header = path.read_bytes().split(b'\n', 3)
        if len(header) != 4 or header[0] != b'P6' or header[2] != b'255':
            raise ValueError('Unexpected screenshot format: ' + str(path))
        width, height = map(int, header[1].split())
        x, y, w, h = region
        if x < 0 or y < 0 or w < 1 or h < 1 or x + w > width or y + h > height or len(header[3]) != width * height * 3:
            raise ValueError('Invalid movie region or screenshot: ' + str(path))
        images.append(b''.join(header[3][((y + row) * width + x) * 3:((y + row) * width + x + w) * 3] for row in range(h)))
    return sum(images[0][n:n+3] != images[1][n:n+3] for n in range(0, len(images[0]), 3))

def prepare(prefs, work, mode, script, seconds, workload, nvram_source=None, movie=None, dismiss_repair=False):
    lines = []
    disks = 0
    media = {'disk': 0, 'cdrom': 0}
    for line in prefs.read_text().splitlines():
        key, _, value = line.partition(' ')
        if key in ('disk', 'cdrom'):
            readonly = value.startswith('*')
            source = Path(value.lstrip('*')).expanduser()
            if not source.is_absolute():
                source = prefs.parent / source
            source = source.resolve(strict=True)
            destination = work / ('%s-%d.img' % (key, media[key]))
            clone(source, destination)
            media[key] += 1
            line = key + ' ' + ('*' if readonly else '') + str(destination)
            disks += key == 'disk'
        if key in ('extfs', 'jit68k_host'):
            continue
        lines.append(line)
    if not disks:
        raise ValueError('An installed guest disk is required')
    share = work / 'share'
    share.mkdir()
    for n in range(200 if workload == 'extfs' else 0 if workload == 'quicktime' else 20):
        (share / ('fixture-%02d.txt' % n)).write_text('68k mixed mode fixture.\n')
    if movie:
        shutil.copy2(movie, share / '000-qualification.mov')
    lines += ['extfs ' + str(share), 'jit68k_host ' + ('false' if mode == 'off' else 'true')]
    # Only a positional .sheepvm bundle selects private NVRAM on macOS.
    # --config changes preferences but still uses HOME/.sheepshaver_nvram.
    if nvram_source is None:
        nvram_source = prefs.parent / 'nvram' if prefs.parent.suffix == '.sheepvm' else Path.home() / '.sheepshaver_nvram'
    for suffix in ('', '.flash'):
        source = Path(str(nvram_source) + suffix)
        if source.is_file():
            shutil.copy2(source, work / ('nvram' + suffix))
    config = work / 'prefs'
    config.write_text('\n'.join(lines) + '\n')
    inputs = work / 'input.script'
    ready = ['every %d shot ' % (300 if seconds > 1800 else 60) + str(work / 'state-%d.ppm')]
    if dismiss_repair:
        ready += ['at 65 key return', 'at 67 key esc']
    # CurApName can name Finder before its desktop volumes are drawn,
    # especially under verification. Wait for the Unix disk's white stripe.
    ready += ['waitfinder', 'waitpixel 976 116 ffffff', 'phase', 'at 2 click 100 600']
    if script:
        inputs.write_text(script.read_text().replace('{work}', str(work)))
    elif workload == 'extfs':
        commands = ['at 5 dump 910 8 ' + str(work / 'finder.bin'),
                    'at 10 click 976 120', 'at 22 key cmd+o',
                    'at 35 shot ' + str(work / 'shared-folder.ppm')]
        points = [(150,150), (800,600), (400,200), (650,500), (100,600), (900,100)]
        commands += ['at %d mouse %d %d' % (20 + n, x, y) for n, (x, y) in enumerate(points)]
        commands += ['at 60 key cmd+n', 'at 62 text regression-created', 'at 64 key return',
                     'at 75 shot ' + str(work / 'shared-write.ppm')]
        # Reopen the host directory to exercise repeated Mixed Mode callbacks.
        for second in range(90, seconds - 10, 30):
            commands += ['at %d key cmd+w' % second, 'at %d click 976 120' % (second + 2), 'at %d key cmd+o' % (second + 4)]
        # CurApName is sampled in whichever classic process owns execution at
        # that instant. Preserve several late samples rather than overwrite
        # the only evidence with a background process's name.
        commands += ['at %d dump 910 32 ' % (seconds - 10 + 2*n) + str(work / ('finder-final-%d.bin' % n)) for n in range(5)]
        commands += ['at %d dump 910 8 ' % (seconds - 5) + str(work / 'finder.bin'),
                     'at %d mouse 300 600' % (seconds - 30),
                     'at %d log qualification workload completed' % (seconds - 2)]
        commands.sort(key=lambda line: float(line.split()[1]))
        inputs.write_text('\n'.join(ready + commands) + '\n')
    elif workload == 'quicktime':
        inputs.write_text('\n'.join(ready + [
            'at 5 dump 910 8 ' + str(work / 'finder.bin'),
            'at 10 click 976 120', 'at 22 key cmd+o',
            'at 30 shot ' + str(work / 'movie-folder.ppm'),
            'at 35 dblclick 63 92',
            # A cold QuickTime launch can spend tens of seconds loading
            # its resources. Give the player time before sending Play.
            'at 70 shot ' + str(work / 'player.ppm'),
            'at 80 key space',
            'at 90 shot ' + str(work / 'playing-1.ppm'),
            'at 95 shot ' + str(work / 'playing-2.ppm'),
            'at 110 mouse 800 600', 'at 111 mouse 300 150',
            'at 150 key space', 'at 155 key cmd+q',
            'at 180 shot ' + str(work / 'desktop.ppm'),
            'at %d dump 910 8 ' % (seconds - 5) + str(work / 'finder.bin'),
            'at %d log qualification workload completed' % (seconds - 2),
        ]) + '\n')
    elif workload == 'lifecycle':
        inputs.write_text('\n'.join(ready + [
            'at 10 shot ' + str(work / 'desktop.ppm'),
            'at 30 click 239 10',
            'at 35 log restart-requested',
            'at 40 click 270 120',
        ]) + '\n')
    elif workload == 'idle':
        commands = ['at 10 shot ' + str(work / 'desktop.ppm'),
                    'at 60 dump 910 8 ' + str(work / 'finder.bin')]
        commands += ['at %d log idle heartbeat' % t for t in range(300, seconds - 60, 300)]
        commands += ['at %d dump 910 8 ' % (seconds - 5) + str(work / 'finder.bin'),
                     'at %d mouse 800 600' % (seconds - 50),
                     'at %d mouse 150 150' % (seconds - 35),
                     'at %d log qualification workload completed' % (seconds - 2)]
        commands.sort(key=lambda line: float(line.split()[1]))
        inputs.write_text('\n'.join(ready + commands) + '\n')
    else:
        inputs.write_text('\n'.join(ready + [
            'at 10 shot ' + str(work / 'boot.ppm'),
            'at 30 mouse 150 150', 'at 31 mouse 800 600',
            'at 32 mouse 400 200', 'at 33 mouse 650 500',
            'at 55 shot ' + str(work / 'desktop.ppm'),
            'at 60 dump 910 8 ' + str(work / 'finder.bin'),
            'at %d dump 910 8 ' % (seconds - 5) + str(work / 'finder.bin'),
            'at %d log qualification workload completed' % (seconds - 2),
        ]) + '\n')
    return config, inputs

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--app', type=Path, required=True)
    p.add_argument('--prefs', type=Path, required=True)
    p.add_argument('--script', type=Path)
    p.add_argument('--dismiss-repair', action='store_true', help='Acknowledge the known completed Disk First Aid dialog in a dirty seed at 65 seconds')
    p.add_argument('--movie', type=Path, help='QuickTime-compatible movie copied to the private host share')
    p.add_argument('--movie-region', nargs=4, type=int, metavar=('X', 'Y', 'W', 'H'), help='Require changing pixels inside the movie content in paired playback screenshots')
    p.add_argument('--nvram', type=Path, help='Seed PRAM file; its .flash companion is copied too')
    p.add_argument('--work-dir', type=Path)
    p.add_argument('--modes', default='off,on,verify')
    p.add_argument('--ppc', choices=['on', 'off', 'verify'], default='on')
    p.add_argument('--hist', choices=['on', 'off'], default='on', help='Disable boundary profiling for Release timings')
    p.add_argument('--seconds', type=int, default=180)
    p.add_argument('--boot-timeout', type=int, default=600)
    p.add_argument('--expect-exit', type=int, choices=[0], help='Require a normal guest shutdown instead of terminating the VM')
    p.add_argument('--repeat', type=int, default=1)
    p.add_argument('--workload', choices=['boot', 'extfs', 'quicktime', 'idle', 'lifecycle'], default='boot')
    p.add_argument('--interrupts', type=int, default=0, help='VBL injections at real ExtFS callbacks')
    p.add_argument('--return-interrupts', type=int, default=0, help='VBL injections immediately before nested 68k execution returns')
    args = p.parse_args()
    if args.workload == 'quicktime' and not args.movie:
        p.error('--workload quicktime requires --movie')
    if args.workload == 'lifecycle':
        args.expect_exit = 0
    modes = args.modes.split(',')
    if args.seconds < 35 or args.boot_timeout < 1 or args.repeat < 1 or any(m not in ('off', 'on', 'verify') for m in modes):
        p.error('Use positive repeats, at least 35 seconds, and modes off,on,verify')
    if min(args.interrupts, args.return_interrupts) < 0 or (args.workload in ('extfs', 'quicktime') and args.seconds < 300):
        p.error('ExtFS needs at least 300 seconds; interrupt count must be nonnegative')
    if not args.script and args.workload in ('boot', 'idle') and args.seconds < 180:
        p.error('The built-in desktop workload requires at least 180 seconds')
    root = args.work_dir.resolve() if args.work_dir else Path(tempfile.mkdtemp(prefix='nw68-qualification-'))
    if args.work_dir:
        root.mkdir(parents=True, exist_ok=False)
    if ' ' in str(root):
        p.error('NW_SCRIPT requires artifact paths without spaces')
    exe = args.app.resolve(strict=True)
    if exe.is_dir():
        exe /= 'Contents/MacOS/SheepShaver'
    if args.interrupts and b'irq-test extfs injection=' not in exe.read_bytes():
        if b'NW_TEST_EXTFS_IRQS' not in exe.read_bytes():
            p.error('--interrupts requires an NW_BOOT_LOG=1 diagnostic app')
    if args.return_interrupts and b'NW_TEST_RETURN_IRQS' not in exe.read_bytes():
        p.error('--return-interrupts requires an NW_BOOT_LOG=1 diagnostic app with return injection support')
    results = []
    # Snapshot once: all modes/repeats start with exactly the same media,
    # even if the user's separately running VM modifies its original disk.
    seed = root / 'seed.sheepvm'
    seed.mkdir()
    seed_config, _ = prepare(args.prefs.resolve(), seed, 'off', None, args.seconds, 'boot', args.nvram.resolve() if args.nvram else None)
    manifest = dict(app=str(exe), app_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                    source_prefs=str(args.prefs.resolve()), seed_prefs=seed_config.read_text(),
                    seconds=args.seconds, boot_timeout=args.boot_timeout, modes=modes, ppc=args.ppc, hist=args.hist,
                    workload=args.workload, repeats=args.repeat, dismiss_repair=args.dismiss_repair)
    manifest['movie_region'] = args.movie_region
    manifest['nvram_sha256'] = {f.name: hashlib.sha256(f.read_bytes()).hexdigest() for f in seed.glob('nvram*')}
    (root / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Artifacts:', root, flush=True)
    for repeat in range(args.repeat):
        for mode in modes:
            work = root / ('%s-%d.sheepvm' % (mode, repeat))
            work.mkdir()
            config, inputs = prepare(seed_config, work, mode, args.script, args.seconds, args.workload, movie=args.movie, dismiss_repair=args.dismiss_repair)
            env = os.environ.copy()
            env.update(NW_JIT=args.ppc, NW_JIT68K_MODE=mode, NW_JIT68K_HIST='1' if args.hist == 'on' else '0',
                       NW_JIT68K_HIST_PATH=str(work / 'hist.txt'), NW_SCRIPT=str(inputs),
                       NW_JIT68K_TRACE_PATH=str(work / 'verify-failures.txt'),
                       NW_TEST_NK_STATE_PATH=str(work / 'nk-state.txt'))
            env.pop('NW_SCRIPT_RESTART', None)
            if args.workload == 'lifecycle':
                restart_script = work / 'restart.script'
                restart_script.write_text('\n'.join(['every 60 shot ' + str(work / 'restart-state-%d.ppm'),
                    'waitfinder', 'waitpixel 976 116 ffffff', 'phase',
                    'at 2 click 100 600',
                    'at 10 dump 910 8 ' + str(work / 'finder.bin'),
                    'at 20 click 239 10',
                    'at 23 dump 910 8 ' + str(work / 'finder.bin'),
                    'at 25 log qualification workload completed',
                    'at 30 click 270 135']) + '\n')
                env['NW_SCRIPT_RESTART'] = str(restart_script)
            # An unrelated diagnostic injection must not alter this matrix.
            env.pop('NW_TEST_EXTFS_IRQS', None)
            env.pop('NW_TEST_RETURN_IRQS', None)
            if args.interrupts:
                env['NW_TEST_EXTFS_IRQS'] = str(args.interrupts)
            if args.return_interrupts:
                env['NW_TEST_RETURN_IRQS'] = str(args.return_interrupts)
            log_path = work / 'guest.log'
            start = time.monotonic()
            phase_start = None
            phase_required = 'phase' in inputs.read_text().splitlines()
            with log_path.open('wb') as output:
                child = subprocess.Popen([str(exe), str(work)], env=env,
                                         stdout=output, stderr=subprocess.STDOUT)
                print(mode, 'PID', child.pid, flush=True)
                try:
                    last = 0
                    fatal_seen = None
                    with log_path.open('r', errors='replace') as progress:
                        while child.poll() is None:
                            chunk = progress.read()
                            if phase_required and phase_start is None and 'NW-BOOT SCRIPT workload-start' in chunk:
                                phase_start = time.monotonic()
                                print(mode, 'Finder ready; workload started', flush=True)
                            # Fatal guest errors cannot qualify as progress.
                            # Allow the CPU's synchronous diagnostic dump to
                            # finish even if stdout is line-buffered.
                            now = time.monotonic()
                            if fatal_seen is None and re.search(r'NW-BOOT SysError #([1-9]\d*)\b', chunk):
                                fatal_seen = now
                            if fatal_seen is not None and now - fatal_seen >= 2:
                                print(mode, 'fatal guest SysError; preserving failure artifacts', flush=True)
                                break
                            if phase_required and phase_start is None:
                                if now - start >= args.boot_timeout: break
                            elif now - (phase_start or start) >= args.seconds:
                                break
                            age = int(now - start)
                            if age - last >= 20:
                                last = age
                                print(mode, str(age) + 's', flush=True)
                            time.sleep(1)
                    exited = child.poll()
                finally:
                    if child.poll() is None:
                        child.terminate()
                        try:
                            child.wait(timeout=10)
                        except subprocess.TimeoutExpired:
                            child.kill()
                            child.wait()
            log = log_path.read_text(errors='replace')
            flash_paths = re.findall(r'nvram flash ([^:\n]+): bank A gen', log)
            nvram_isolated = bool(flash_paths) and all(Path(p).resolve() == work / 'nvram.flash' for p in flash_paths)
            hist = (work / 'hist.txt').read_text() if (work / 'hist.txt').exists() else ''
            native = re.search(r'\bnative (\d+)', hist)
            verified = re.search(r'\bverified_dispatches (\d+)', hist)
            finder = work / 'finder.bin'
            final_names = [f.read_bytes() for f in sorted(work.glob('finder-final-*.bin'))]
            desktop = any(name[:7] == b'\x06Finder' for name in final_names) if final_names else finder.exists() and finder.read_bytes()[:7] == b'\x06Finder'

            benchmark = work / 'benchmark.csv'
            samples = list(csv.DictReader(benchmark.open())) if benchmark.exists() else []
            benchmark_required = bool(args.script and 'bench68 ' in args.script.read_text())
            waiting = acknowledged = False
            completed = irq_order_errors = injections = 0
            delayed = set()
            max_vbl_latency = 0
            for line in log.splitlines():
                stall = re.search(r'irq-stall src=(\d+).*ack=(\d+)', line)
                if stall: delayed.add((int(stall[1]), int(stall[2])))
                eoi = re.search(r'pic_eoi .*src=(\d+).*ack=(\d+) age_us=(\d+)', line)
                if eoi:
                    delayed.discard((int(eoi[1]), int(eoi[2])))
                    if int(eoi[1]) == 29: max_vbl_latency = max(max_vbl_latency, int(eoi[3]))
                if re.search(r'irq-test (extfs|return) injection=', line):
                    injections += 1
                    irq_order_errors += waiting
                    waiting, acknowledged = True, False
                elif waiting and re.search(r'pic_iack .*src=29\b', line):
                    acknowledged = True
                elif waiting and acknowledged and re.search(r'pic_eoi .*src=29\b', line):
                    completed += 1
                    waiting = False
            result = dict(nvram_isolated=nvram_isolated, mode=mode, ppc=args.ppc, repeat=repeat, elapsed=round(time.monotonic()-start,2),
                          native=int(native.group(1)) if native else 0,
                          verified=int(verified.group(1)) if verified else 0,
                          unexpected_exit=exited if args.expect_exit is None else (None if exited == args.expect_exit else exited),
                          exit_status=exited, exit_requirement_met=exited == args.expect_exit,
                          guest_restarts=log.count('PMU reset (0xd0)'), guest_shutdowns=log.count('PMU shutdown'),
                          verify_mismatches=log.count('jit68k verify mismatch'),
                          block_mismatches=log.count('jit68k block mismatch'),
                          script_errors=len(re.findall(r'NW-BOOT SCRIPT (?:line \d+:|cannot open)', log)),
                          ppc_verify_misses=max([int(n) for n in re.findall(r'jit verify .*?cmp \d+ miss (\d+)', log)] + [log.count('JIT verify miss #')]),
                          ppc_verified=max([int(n) for n in re.findall(r'jit verify .*?cmp (\d+) miss', log)] or [0]),
                          guest_system_errors=[int(n) for n in re.findall(r'NW-BOOT SysError #([1-9]\d*)\b', log)],
                          unexpected_cpu_returns=log.count('CPU outer execution-return'),
                          irq_stalls=log.count('irq-stall '), mouse_failures=log.count('mouse gave up'),
                          unresolved_irq_stalls=len(delayed), max_vbl_latency_us=max_vbl_latency,
                          workload_started=not phase_required or phase_start is not None,
                          boot_seconds=round(phase_start - start, 2) if phase_start else None,
                          mouse_targets=len(re.findall(r'SCRIPT .* mouse at ', log)),
                          heartbeat='qualification workload completed' in log,
                          injected_irqs_completed=completed, interrupts_requested=args.interrupts + args.return_interrupts,
                          injected_irqs=injections, irq_order_errors=irq_order_errors,
                          shared_write=(work / 'share/regression-created').is_dir(),
                          desktop_confirmed=desktop,
                          final_process_name_samples=[name[1:1+name[0]].decode('mac_roman', errors='replace') if name else '' for name in final_names],
                          benchmark_samples=samples, benchmark_required=benchmark_required,
                          audio_input_frames=max([int(n) for n in re.findall(r'\bsb_in=(\d+)', log)] or [0]),
                          audio_output_frames=max([int(n) for n in re.findall(r'\bsb_out=(\d+)', log)] or [0]),
                          movie_changed_pixels=changed_movie_pixels(work, args.movie_region),
                          histogram=hist.splitlines()[:8], artifacts=str(work))
            results.append(result)
            (root / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
            print(json.dumps(result), flush=True)
    if any(r['verify_mismatches'] or r['block_mismatches'] or r['script_errors'] or r['ppc_verify_misses'] or r['guest_system_errors'] or
           r['unexpected_cpu_returns'] or r['unresolved_irq_stalls'] or r['mouse_failures'] or
           not r['workload_started'] or not r['nvram_isolated'] or
           not r['exit_requirement_met'] or not r['heartbeat'] or
           r['injected_irqs_completed'] < r['interrupts_requested'] or r['irq_order_errors'] or
           (args.workload == 'lifecycle' and (r['guest_restarts'] < 1 or r['guest_shutdowns'] < 1)) or
           (args.workload == 'extfs' and not r['shared_write']) or
           (args.workload == 'quicktime' and (not r['audio_input_frames'] or not r['audio_output_frames'])) or
           (args.movie_region is not None and not r['movie_changed_pixels']) or
           (not args.script and not r['desktop_confirmed']) or
           (r['benchmark_required'] and (len(r['benchmark_samples']) != 5 or
            any(s['correct'] != '1' for s in r['benchmark_samples']))) or
           (args.hist == 'on' and r['mode'] == 'on' and not r['native']) or
           (args.ppc == 'verify' and not r['ppc_verified']) or
           (args.hist == 'on' and r['mode'] == 'verify' and not r['verified']) for r in results):
        raise SystemExit(1)

if __name__ == '__main__':
    main()
