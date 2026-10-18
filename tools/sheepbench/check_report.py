#!/usr/bin/env python3
"""Checks a SheepBench report (the .tsv the host wrote, or the guest's "SheepBench Report" with Mac line ends).

  check_report.py REPORT [--expect-host] [--expect-no-host] [--expect-jit-off] [--expect-no-jit-off]
                         [--expect-failures] [--suites cpu,memory]

Always: the report parses, it has rows, every skipped row says why. Unless --expect-failures: no FAILED row.
--expect-host: host info present and the percent-of-native column filled for every C kernel row that ran.
--expect-no-host: host is "unknown" and no native column.
--expect-jit-off: JIT-off values for the rows that ran, and the CPU suite's kernels faster with the JIT (median).
--expect-failures: at least one FAILED row and every kernel row that has a native column... (the corrupt hook)."""
import statistics, sys

args = sys.argv[1:]
if not args:
    sys.exit(__doc__)
path, flags = args[0], set(a for a in args[1:] if a.startswith('--'))
suites = None
if '--suites' in args:
    suites = args[args.index('--suites') + 1].split(',')
text = open(path, 'rb').read().decode('latin-1').replace('\r', '\n')
info, columns, rows = {}, None, []
for line in text.split('\n'):
    if not line:
        continue
    if line.startswith('#'):
        k, _, v = line[1:].strip().partition('\t')
        info[k] = v
    elif columns is None:
        columns = line.split('\t')
    else:
        cells = line.split('\t')
        rows.append(dict(zip(columns, cells + [''] * (len(columns) - len(cells)))))

problems = []
def need(cond, what):
    if not cond:
        problems.append(what)

need(columns and 'status' in columns, 'no column header')
need(rows, 'no rows')
ran = [r for r in rows if r['status'] in ('ok', 'noisy')]
failed = [r for r in rows if r['status'].startswith('FAILED')]
skipped = [r for r in rows if r['status'].startswith('skipped')]
for r in skipped:
    need(len(r['status']) > len('skipped: '), f"skipped without a reason: {r['suite']} / {r['test']}")
if suites:
    for s in suites:
        need(any(r['suite'] == s for r in rows), f'no rows for suite {s}')
if '--expect-failures' in flags:
    need(failed, 'expected FAILED rows (corrupted native checksums), found none')
else:
    for r in failed:
        problems.append(f"FAILED: {r['suite']} / {r['test']}: {r['status']}")

kernel_rows = [r for r in ran if not r['test'].startswith('loop:') and r['unit'] != 'ns/insn']
if '--expect-host' in flags:
    need(info.get('host', '').startswith('SheepShaver'), f"host should be SheepShaver, is {info.get('host')!r}")
    need(info.get('build', 'unknown') != 'unknown', 'no build commit in the run info')
    missing = [r['test'] for r in kernel_rows if r['suite'] in ('cpu', 'memory') and not r['pct_native']]
    need(not missing, f'percent of native missing for: {missing[:5]}')
if '--expect-no-host' in flags:
    need(info.get('host') == 'unknown', f"host should be unknown, is {info.get('host')!r}")
    need(not any(r['pct_native'] for r in rows), 'percent of native present without a host')
if '--expect-jit-off' in flags:
    missing = [r['test'] for r in ran if not r['jit_off'] and '[host 68k JIT off]' not in r['test']]
    need(not missing, f'JIT-off value missing for: {missing[:5]}')
    sp = [float(r['jit_speedup_x']) for r in ran if r['suite'] == 'cpu' and r['jit_speedup_x']]
    if any(r['suite'] == 'cpu' for r in ran):
        need(sp and statistics.median(sp) > 1.0, f'CPU JIT speedup median should be above 1: {sp}')
if '--expect-jit-off' in flags and any(r['suite'] == 'quickdraw' for r in ran) and info.get('sheepforce') == '1':
    missing = [r['test'] for r in ran if r['suite'] == 'quickdraw' and not r['gpu_off']]
    need(not missing, f'GPU-off value missing for: {missing[:5]}')
if '--expect-no-jit-off' in flags:
    need(not any(r['jit_off'] for r in rows), 'JIT-off values present although switching was not allowed')

print(f"{path}: {len(rows)} rows, {len(ran)} ran, {len(skipped)} skipped, {len(failed)} failed")
for r in rows[:200]:
    print(f"  {r['suite']:<10} {r['test']:<44} {r['value']:>10} {r['unit']:<7} native% {r['pct_native']:>7} "
          f"jit_off {r['jit_off']:>9} x{r['jit_speedup_x']:<6} gpu x{r['gpu_speedup_x']:<6} {r['hook']}:{r['hook_hits']} "
          f"{r['status']}")
if problems:
    print('PROBLEMS:')
    for p in problems:
        print('  ' + p)
    sys.exit(1)
print('report checks passed')
