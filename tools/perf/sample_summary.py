#!/usr/bin/env python3
"""Summarises the output of /usr/bin/sample for a SheepShaver VM process:

    tools/perf/sample_summary.py SAMPLE_FILE [--top N] [--thread emul]

For the emulation thread (the one running powerpc_cpu::execute) it prints the *self* time of every function (samples where
that function was the leaf), grouped into JIT-generated code (no symbol), the C helpers the JIT calls, the dispatcher and
chain code, the 68k layer, the MMU and the rest, plus the percentage of the whole sample. Other threads are summed up on one
line each. Self time is exact for the sampled interval; it says what the CPU was executing, not why.
"""
import argparse, collections, re, sys
from pathlib import Path

WAIT = r"__psynch|__semwait|mach_msg|kevent|nanosleep|semaphore_wait|__workq|clock_sleep"
LINE = re.compile(r"^(?P<prefix>[\s+!:|]*?)(?P<count>\d+)\s+(?P<rest>.*)$")

GROUPS = [
    ("JIT code (generated)", re.compile(r"^\?\?\?|<unknown>|^jit code$")),
    ("JIT helpers (loads/stores/system/...)", re.compile(r"nw_jit_helper_|jit_host_(lw|lh|lb|st|lf|ld)|nw_jit_log_wake|nw_vxo_note|jit_host_mem")),
    ("JIT chain / dispatch", re.compile(r"jit_host_chain|nw_jit_try|nw_jit_cache_get|nw_jit_itlb|nw_jit_helper_chain|nw_pull_gpr|nw_commit_gpr|nw_jit_ibtc|dtlb_sync_msr|nw_jit_cpu_bind|cache_get")),
    ("JIT compile / invalidate", re.compile(r"compile_block|nw_jit_compile|emit_|nw_jit_invalidate|sys_icache_invalidate|pthread_jit_write_protect|ensure_code_room|nw_jit_flush|invalidate_page|nw_jit_op_")),
    ("68k layer (nw68)", re.compile(r"nw68_|nw_68k_|nw68|anonymous namespace\)::(probe|read_code|data_read|data_write|write_probe|block_progress|compatible_nk|page_translate)")),
    ("MMU / translate", re.compile(r"ppc32_mmu|nw_mmu|translate|nw_pa_kind|bat_hit")),
    ("guest events / timers / idle", re.compile(r"tick_decrementer|take_|nw_openpic|nw_via|pmu|adb|__psynch|nanosleep|semaphore|mach_msg|__semwait|kevent|__workq")),
    ("video (present, hash, flush)", re.compile(r"SheepForce|VideoHost|nw_fb_|PresentedHash|MTL|IOGPU|iokit_user_client")),
    ("memory ops (memcpy/memset/memcmp)", re.compile(r"_platform_mem|__bzero|memmove|memcpy|memset")),
]


def group_of(name):
    for label, rx in GROUPS:
        if rx.search(name):
            return label
    return "other"


def clean(rest):
    name = rest.split("  (in ")[0].strip()
    name = re.sub(r"\s*\+\s*\d+\s*\[0x[0-9a-f]+\].*$", "", name)
    name = re.sub(r"\(.*$", "", name).strip() if name.startswith("(anonymous") is False else name
    return name or "?"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sample", type=Path)
    ap.add_argument("--top", type=int, default=30)
    args = ap.parse_args()
    lines = args.sample.read_text(errors="replace").splitlines()
    try:
        start = next(i for i, l in enumerate(lines) if l.startswith("Call graph:"))
        end = next(i for i, l in enumerate(lines) if l.startswith("Total number in stack") and i > start)
    except StopIteration:
        sys.exit("not a sample file")
    # split thread blocks
    blocks, cur = [], None
    for l in lines[start + 1:end]:
        m = re.match(r"^    (\d+) Thread_\S+(.*)$", l)
        if m:
            cur = [int(m.group(1)), m.group(2).strip(), []]
            blocks.append(cur)
        elif cur is not None and l.strip():
            cur[2].append(l)

    total_all = sum(b[0] for b in blocks)
    emul = None
    for b in blocks:
        if any("powerpc_cpu::execute" in l or "emul_thread_main" in l for l in b[2][:400]):
            emul = b
            break
    print("sample %s: %d thread-samples in %d threads" % (args.sample.name, total_all, len(blocks)))
    waiting = [b for b in blocks if b is not emul and b[0] >= 0.9 * (emul[0] if emul else 0)]
    for b in blocks:
        if b is emul or b in waiting:
            continue
        if b[0] >= 0.01 * (emul[0] if emul else 1):
            print("  other thread %-40s %6d samples (%.1f%% of a core)" % (b[1][:40] or "(unnamed)", b[0], 100.0 * b[0] / max(emul[0] if emul else 1, 1)))
    print("  (%d other threads were blocked the whole time or ran under 1%%)" % (len(waiting) + sum(1 for b in blocks if b is not emul and b not in waiting and b[0] < 0.01 * emul[0])))
    if not emul:
        print("no emulation thread found")
        return
    # self time per function: node count minus its children's counts
    nodes = []  # (depth, count, name)
    for l in emul[2]:
        m = LINE.match(l)
        if not m:
            continue
        depth = len(m.group("prefix"))
        nodes.append((depth, int(m.group("count")), clean(m.group("rest"))))
    selfc = collections.Counter()
    for i, (d, c, nm) in enumerate(nodes):
        child_sum = 0
        for j in range(i + 1, len(nodes)):
            if nodes[j][0] <= d:
                break
            if nodes[j][0] == d + 2 or (nodes[j][0] > d and all(k[0] > d for k in nodes[i + 1:j]) and nodes[j][0] - d <= 4 and nodes[j][0] != nodes[i + 1][0] and False):
                pass
        # direct children are the following nodes with the smallest depth greater than d
        j = i + 1
        child_depth = None
        while j < len(nodes) and nodes[j][0] > d:
            if child_depth is None:
                child_depth = nodes[j][0]
            if nodes[j][0] == child_depth:
                child_sum += nodes[j][1]
            j += 1
        selfc[nm] += max(c - child_sum, 0)
    emul_total = emul[0]
    idle = sum(v for k, v in selfc.items() if re.search(WAIT, k))
    busy = emul_total - idle
    print("\nemulation thread: %d samples, %d idle (%.1f%%), %d busy" % (emul_total, idle, 100.0 * idle / max(emul_total, 1), busy))
    groups = collections.Counter()
    for k, v in selfc.items():
        if re.search(WAIT, k):
            continue
        groups[group_of(k)] += v
    print("\nBusy time by area (self time, % of busy):")
    for g, v in groups.most_common():
        print("  %-42s %6d %6.1f%%" % (g, v, 100.0 * v / max(busy, 1)))
    print("\nTop functions by self time (% of busy):")
    for k, v in selfc.most_common(args.top + 8):
        if re.search(WAIT, k):
            continue
        print("  %-60s %6d %6.2f%%" % (k[:60], v, 100.0 * v / max(busy, 1)))


if __name__ == "__main__":
    main()
