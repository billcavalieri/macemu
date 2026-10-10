#!/usr/bin/env python3
"""Turns a JIT profile (NW_JIT_PROFILE=<file>, see SheepShaver/src/nw_jit.cpp) into tables:

    tools/perf/jit_report.py PROFILE [--top N] [--markdown]

What it answers: which guest instructions run most, what the JIT does with each (code inline, or a call into a C helper,
and which helper), how long the blocks are, how they end, and which blocks are hottest. The profile counts every
execution of every compiled block (an increment in the block's own code), so the numbers are exact, not sampled.

"Helper call sites" are the call instructions in an op's emitted code, weighted by how often its block ran. An op with an
inline fast path and a helper slow path is listed as having a helper even if the slow path is rare: use the sampling profile
(tools/perf/sample_summary.py) for time, and this report for what to look at.
"""
import argparse, collections, re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from gen_jit_ops_md import parse_decode_table  # noqa: E402

NOXO = {"D_form", "I_form", "B_form", "SC_form", "M_form", "DS_form", "MD_form", "MDS_form", "INVALID_form"}


def build_decoder():
    text = (ROOT / "SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-decode.cpp").read_text()
    by_prim = collections.defaultdict(list)
    for e in parse_decode_table(text):
        by_prim[e["prim"]].append(e)

    def key(form, op):
        if form == "VX_form": return op & 0x7ff
        if form == "VXR_form": return op & 0x3ff
        if form == "VA_form": return op & 0x3f
        if form == "A_form": return (op >> 1) & 0x1f
        if form == "XO_form": return (op >> 1) & 0x1ff
        if form == "XS_form": return (op >> 2) & 0x1ff
        return (op >> 1) & 0x3ff

    cache = {}

    def name_of(op):
        prim = op >> 26
        sig = (prim, (op >> 1) & 0x3ff, op & 0x7ff)
        if sig in cache:
            return cache[sig]
        result = "?%d/%d" % (prim, (op >> 1) & 0x3ff)
        cands = by_prim.get(prim, [])
        order = sorted(cands, key=lambda e: {"VA_form": 0, "A_form": 0}.get(e["form"], 1))
        for e in order:
            f = e["form"]
            if f in NOXO:
                if e["name"] != "invalid":
                    result = e["name"]
                    break
                continue
            if f == "VA_form" and (op & 0x3f) < 32:
                continue
            if key(f, op) == e["xo"]:
                result = e["name"]
                break
        cache[sig] = result
        return result

    return name_of


def demangle(sym):
    m = re.match(r"_Z[LN]?(\d+)(.*)", sym)
    if m:
        n = int(m.group(1))
        rest = m.group(2)
        return rest[:n]
    return sym.lstrip("_")


def terminator(op):
    prim = op >> 26
    if prim == 18: return "b/bl"
    if prim == 16: return "bc"
    if prim == 19:
        xo = (op >> 1) & 0x3ff
        return {16: "bclr", 528: "bcctr", 50: "rfi", 150: "isync"}.get(xo, "other-19")
    if prim == 17: return "sc"
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("profile", type=Path)
    ap.add_argument("--top", type=int, default=25)
    ap.add_argument("--markdown", action="store_true")
    ap.add_argument("--title", default="")
    args = ap.parse_args()
    name_of = build_decoder()

    stats, helpers, interp, hdyn = {}, {}, collections.Counter(), {}
    sysn, spr = collections.Counter(), collections.Counter()
    blocks = []  # (pc, n, count, [(op, ncalls, words, [helper ids])])
    cur = None
    for line in args.profile.read_text().splitlines():
        f = line.split("\t")
        if f[0] == "S":
            stats[f[1]] = int(f[2])
        elif f[0] == "H":
            helpers[int(f[1])] = demangle(f[3])
        elif f[0] == "C":
            hdyn[int(f[1])] = int(f[2])
        elif f[0] == "Y":
            sysn[int(f[1])] = int(f[2])
        elif f[0] == "R":
            spr[("mtspr" if f[1] == "1" else "mfspr", int(f[2]))] = int(f[3])
        elif f[0] == "I":
            interp[(int(f[1]) << 10) | int(f[2])] = int(f[3])
        elif f[0] == "B":
            cur = [int(f[1], 16), int(f[2]), int(f[3]), []]
            blocks.append(cur)
        elif f[0] == "O" and cur is not None:
            cur[3].append((int(f[1], 16), int(f[2]), int(f[3]), [int(x) for x in f[4:]]))

    h = (lambda s: "\n## " + s + "\n") if args.markdown else (lambda s: "\n== " + s + " ==")
    out = []
    w = out.append

    total_block_runs = sum(b[2] for b in blocks)
    total_insns = sum(b[2] * len(b[3]) for b in blocks)
    interp_total = sum(interp.values())
    c_runs = stats.get("blocks_exec_c", 0)
    w(h("Run" + (": " + args.title if args.title else "")))
    w("blocks compiled %d (distinct records), recompiled/flushed %d, flush calls %d" % (len(blocks), stats.get("compiles", 0), stats.get("flush", 0)))
    w("guest instructions in JIT blocks   %14s" % format(total_insns, ","))
    w("guest instructions in interpreter  %14s  (%.4f%% of all)" % (format(interp_total, ","), 100.0 * interp_total / max(total_insns + interp_total, 1)))
    w("block executions                   %14s   average block length %.2f instructions" % (format(total_block_runs, ","), total_insns / max(total_block_runs, 1)))
    w("  entered through the C dispatcher %14s   (%.1f%%)   the rest were direct/native chain hops" % (format(c_runs, ","), 100.0 * c_runs / max(total_block_runs, 1)))
    w("  C chain-loop hops                %14s" % format(stats.get("chain_hops", 0), ","))
    d_hit, d_miss = stats.get("dtlb_hit", 0), stats.get("dtlb_miss", 0)
    i_hit, i_miss = stats.get("itlb_hit", 0), stats.get("itlb_miss", 0)
    w("data TLB  hit %s miss %s (%.2f%% miss)" % (format(d_hit, ","), format(d_miss, ","), 100.0 * d_miss / max(d_hit + d_miss, 1)))
    w("instr TLB hit %s miss %s (%.2f%% miss)" % (format(i_hit, ","), format(i_miss, ","), 100.0 * i_miss / max(i_hit + i_miss, 1)))

    if "compile_ns" in stats:
        w("JIT compile time        %8.2f s over %s compiles (%.1f us each), of which write-protect toggles %.2f s, icache invalidate %.2f s" % (
            stats["compile_ns"] / 1e9, format(stats.get("compiles", 0), ","), stats["compile_ns"] / 1e3 / max(stats.get("compiles", 1), 1),
            stats.get("wx_ns", 0) / 1e9, stats.get("icache_ns", 0) / 1e9))

    causes = [(k[len("flush_entries_"):], v, stats.get("flush_calls_" + k[len("flush_entries_"):], 0)) for k, v in stats.items() if k.startswith("flush_entries_") and v]
    if causes or stats.get("arena_compacts") is not None:
        w(h("Code-cache upkeep: why translations are thrown away"))
        for name, ent, calls in sorted(causes, key=lambda t: -t[1]):
            w("  dropped by %-8s %10s translations in %s invalidations" % (name, format(ent, ","), format(calls, ",")))
        if "ibtc_fills_cumulative" in stats:
            w("  indirect-target cache (cumulative since start): %s fills; epochs (every entry and cross-page link invalidated) by link/compaction %s, ITLB flush %s, tlbie %s, mtsr %s" % (
                format(stats["ibtc_fills_cumulative"], ","), format(stats["ibtc_epochs_link_cumulative"], ","), format(stats["ibtc_epochs_itlb_flush_cumulative"], ","),
                format(stats["ibtc_epochs_itlb_drop_page_cumulative"], ","), format(stats["ibtc_epochs_mtsr_cumulative"], ",")))
        if "itlb_clears_cumulative" in stats:
            w("  instruction-TLB flushes so far: %s" % format(stats["itlb_clears_cumulative"], ","))
        w("  chain-helper calls by cause: " + ", ".join("%s %s" % (k[len("chain_"):], format(v, ",")) for k, v in stats.items() if k.startswith("chain_")))
        if "idle_sleeps" in stats:
            w("  idle loop: %s sleeps (%s ended by the timeout, the rest by an event), %s wake-up requests from other threads" % (
                format(stats["idle_sleeps"], ","), format(stats.get("idle_timeouts", 0), ","), format(stats.get("idle_resumes", 0), ",")))
        w("  direct block links made: %s" % format(stats.get("links_made", 0), ","))
        w("  code arena: %d compactions (%.1f MB copied, %.2f s), %d bank wipes, %d cold evictions" % (
            stats.get("arena_compacts", 0), stats.get("arena_compact_bytes", 0) / 1e6, stats.get("arena_compact_ns", 0) / 1e9,
            stats.get("arena_bank_wipes", 0), stats.get("arena_evicts", 0)))

    # ---- instruction mix
    mix = collections.defaultdict(lambda: [0, 0, 0, 0, 0])  # execs, execs with calls, call sites run, words*execs, static count
    for pc, n, count, ops in blocks:
        for op, ncalls, words, hs in ops:
            m = mix[name_of(op)]
            m[0] += count
            if ncalls:
                m[1] += count
                m[2] += count * ncalls
            m[3] += count * words
            m[4] += 1
    w(h("Instruction mix: what runs, and what the JIT does with it"))
    w("%-12s %14s %7s %7s %12s %8s %7s" % ("op", "executed", "%", "cum%", "w/ helper", "calls/ex", "words"))
    cum = 0
    rows = sorted(mix.items(), key=lambda kv: -kv[1][0])
    for name, m in rows[:args.top * 2]:
        cum += m[0]
        w("%-12s %14s %6.2f%% %6.1f%% %12s %8.2f %7.1f" % (name, format(m[0], ","), 100.0 * m[0] / max(total_insns, 1), 100.0 * cum / max(total_insns, 1),
                                                       format(m[1], ","), m[2] / max(m[0], 1), m[3] / max(m[0], 1)))
    tot_help = sum(m[1] for m in mix.values())
    w("instructions whose emitted code contains a helper call: %s (%.1f%% of executed)" % (format(tot_help, ","), 100.0 * tot_help / max(total_insns, 1)))

    # ---- helpers
    callers = collections.defaultdict(collections.Counter)   # helper id -> op name -> static weight (block executions)
    for pc, n, count, ops in blocks:
        for op, ncalls, words, hs in ops:
            for hid in hs:
                callers[hid][name_of(op)] += count
    w(h("C helpers called from JIT code: exact call counts (profile mode counts every call at its call site)"))
    total_calls = sum(hdyn.values())
    w("total helper calls %s  (%.2f per guest instruction)" % (format(total_calls, ","), total_calls / max(total_insns, 1)))
    w("%-34s %14s %7s  %s" % ("helper", "calls", "%", "emitted by (op, weight)"))
    rows = []
    for hid, cnt in hdyn.items():
        nm = helpers.get(hid, "?" if hid else "(unnamed)")
        rows.append((cnt, nm, hid))
    for cnt, nm, hid in sorted(rows, reverse=True)[:args.top]:
        who = ", ".join("%s" % k for k, v in callers[hid].most_common(4))
        w("%-34s %14s %6.1f%%  %s" % (nm[:34], format(cnt, ","), 100.0 * cnt / max(total_calls, 1), who))

    if sysn or spr:
        w(h("nw_jit_helper_system calls by instruction (exact)"))
        rows = [(v, name_of(((k >> 10) << 26) | ((k & 1023) << 1))) for k, v in sysn.items()] + [(v, "%s spr %d" % k) for k, v in spr.items()]
        tot = sum(v for v, _ in rows)
        for v, n in sorted(rows, reverse=True)[:args.top]:
            w("%-24s %14s %6.1f%%" % (n, format(v, ","), 100.0 * v / max(tot, 1)))

    # ---- interpreter fallbacks
    w(h("Instructions the interpreter executed (not compiled)"))
    if not interp:
        w("none")
    else:
        w("%-12s %14s" % ("op", "executed"))
        agg = collections.Counter()
        for k, v in interp.items():
            prim, xo = k >> 10, k & 0x3ff
            # reconstruct an opcode whose fields give this name (extended opcode in bits 1..10)
            agg[name_of((prim << 26) | (xo << 1))] += v
        for name, v in agg.most_common(args.top):
            w("%-12s %14s" % (name, format(v, ",")))

    # ---- block shape
    w(h("Blocks: length and how they end (weighted by executions)"))
    ends = collections.Counter()
    ends_n = collections.defaultdict(int)
    length_hist = collections.Counter()
    for pc, n, count, ops in blocks:
        if not ops:
            continue
        last = ops[-1][0]
        kind = terminator(last) or ("cut at %d" % n if n >= 32 else "cut (page end or unsupported next op)")
        ends[kind] += count
        ends_n[kind] += count * n
        length_hist[min(n, 32)] += count
    w("%-40s %14s %7s %10s" % ("block ends with", "executions", "%", "avg length"))
    for kind, cnt in ends.most_common():
        w("%-40s %14s %6.1f%% %10.2f" % (kind, format(cnt, ","), 100.0 * cnt / max(total_block_runs, 1), ends_n[kind] / max(cnt, 1)))
    short = sum(c for l, c in length_hist.items() if l <= 4)
    w("blocks of 1-4 instructions: %.1f%% of executions; 5-8: %.1f%%; 9-16: %.1f%%; 17-32: %.1f%%" % (
        100.0 * short / max(total_block_runs, 1),
        100.0 * sum(c for l, c in length_hist.items() if 5 <= l <= 8) / max(total_block_runs, 1),
        100.0 * sum(c for l, c in length_hist.items() if 9 <= l <= 16) / max(total_block_runs, 1),
        100.0 * sum(c for l, c in length_hist.items() if l >= 17) / max(total_block_runs, 1)))

    # ---- hot blocks
    w(h("Hottest blocks (by instructions executed)"))
    w("%-10s %4s %14s %7s  %s" % ("pc", "len", "executions", "% insns", "ops"))
    for pc, n, count, ops in sorted(blocks, key=lambda b: -b[2] * len(b[3]))[:args.top]:
        names = [name_of(o[0]) for o in ops]
        w("%08x %4d %14s %6.2f%%  %s" % (pc, len(ops), format(count, ","), 100.0 * count * len(ops) / max(total_insns, 1), " ".join(names[:14]) + (" ..." if len(names) > 14 else "")))

    # ---- guest code regions
    w(h("Guest code regions (64 KB) by instructions executed"))
    region = collections.Counter()
    for pc, n, count, ops in blocks:
        region[pc >> 16] += count * len(ops)
    for r, cnt in region.most_common(12):
        w("%04x0000-%04xffff %14s %6.2f%%" % (r, r, format(cnt, ","), 100.0 * cnt / max(total_insns, 1)))

    print("\n".join(out))


if __name__ == "__main__":
    main()
