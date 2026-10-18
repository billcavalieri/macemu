/*
 *  suite_68k.c - 68k CPU suite: integer, copy, byte copy, call and multiply/divide loops in 68k code
 *  (kernels68k.s), called through Mixed Mode so the Mac OS ROM's 68k emulator runs them, as it runs the 68k parts of
 *  Mac OS and old applications. SheepShaver's host 68k JIT (jit68k_host) speeds that emulator up. Reported in ns per
 *  68k instruction; each result is checked against the same arithmetic done in C.
 *
 *  When switching is allowed, each test also runs with only the host 68k JIT off (the PowerPC JIT stays on), as its
 *  own row, so its effect shows apart from the PowerPC JIT's.
 *
 *  (C) 2026 Bill Cavalieri
 */

#include "suites.h"
#include "bench_host.h"
#include <MixedMode.h>
#include <Memory.h>
#include <Events.h>
#include <stdio.h>
#include <string.h>
#include "k68_code.h"       /* k68_code[], from kernels68k.s */

static const char kSuite[] = "68k";

/* Mixed Mode procInfo fields (MixedMode.h): calling convention in bits 0-3, result size from bit 4, then two bits
 * per stack parameter from bit 6. */
#define SB_RESULT_SIZE(sz) ((ProcInfoType)(sz) << 4)
#define SB_STACK_PARAM(n, sz) ((ProcInfoType)(sz) << (6 + ((n) - 1) * 2))

enum { kInt, kCopy, kByteCopy, kCall, kMulDiv, kKernels };

static const struct {
    const char *name;
    int args;
    unsigned insns;     /* per inner iteration */
    uint32_t inner;     /* inner iterations per call */
} kDefs[kKernels] = {
    { "68k: add/eor/rol loop", 1, 5, 20000 },
    { "68k: move.l (a0)+,(a1)+ copy, 16 KB", 3, 3, 4096 },
    { "68k: move.b (a0)+,(a1)+ copy, 4 KB", 3, 3, 4096 },
    { "68k: bsr/rts", 1, 4, 10000 },
    { "68k: mulu.w/divu.w", 1, 9, 10000 },
};

typedef struct {
    UniversalProcPtr upp[kKernels];
    ProcInfoType info[kKernels];
    Ptr code;
    unsigned char *src, *dst;
    int k;
} K68;

static long Call(K68 *c, int k, long a, long b, long n)
{
    if (kDefs[k].args == 1)
        return CallUniversalProc(c->upp[k], c->info[k], a);
    return CallUniversalProc(c->upp[k], c->info[k], a, b, n);
}

static long Run(K68 *c, int k)
{
    switch (k) {
    case kCopy: return Call(c, k, (long)c->src, (long)c->dst, kDefs[k].inner);
    case kByteCopy: return Call(c, k, (long)c->src, (long)c->dst, kDefs[k].inner);
    default: return Call(c, k, kDefs[k].inner, 0, 0);
    }
}

static uint32_t Body(void *p, uint32_t iters)
{
    K68 *c = (K68 *)p;
    uint32_t i, r = 0;
    for (i = 0; i < iters; i++)
        r += (uint32_t)Run(c, c->k);
    return r;
}

/* What the 68k code must return, worked out in C. */
static uint32_t Expected(int k)
{
    uint32_t n = kDefs[k].inner, d0 = 0, d1;
    switch (k) {
    case kInt:
        for (d1 = n; d1; d1--) {
            d0 += d1;
            d0 ^= 0x12345678UL;
            d0 = d0 << 3 | d0 >> 29;
        }
        return d0;
    case kCall:
        return n;
    case kMulDiv:
        for (d1 = n; d1; d1--) {
            uint32_t v = (d1 & 0xFFFF) * 5;
            d0 = (d0 & 0xFFFF0000UL) | ((d0 + v / 7) & 0xFFFF);
            d0 = (d0 & 0xFFFF0000UL) | ((d0 + v % 7) & 0xFFFF);
        }
        return d0;
    default:
        return 0;       /* the copies are checked by comparing memory */
    }
}

static void RunPass(K68 *c, const char *suffix)
{
    int k;
    for (k = 0; k < kKernels && !gCancelled; k++) {
        char name[96], why[80];
        BenchRow *row;
        Measurement m;
        long r;
        sprintf(name, "%s%s", kDefs[k].name, suffix);
        row = TestBegin(kSuite, name, unitNsPerInsn);
        if (!row)
            continue;
        if (!c->upp[k]) {
            RowSkip(row, "no routine descriptor");
            continue;
        }
        memset(c->dst, 0, 16384);
        r = Run(c, k);
        why[0] = 0;
        if ((k == kCopy && memcmp(c->src, c->dst, kDefs[k].inner * 4) != 0) ||
            (k == kByteCopy && memcmp(c->src, c->dst, kDefs[k].inner) != 0))
            strcpy(why, "copy differs from the source");
        else if (k != kCopy && k != kByteCopy && (uint32_t)r != Expected(k))
            sprintf(why, "returned %08lx, expected %08lx", (unsigned long)r, (unsigned long)Expected(k));
        if (why[0]) {
            RowFail(row, why);
            Say(why);
            continue;
        }
        c->k = k;
        Measure(Body, c, 1, &m);
        if (m.cancelled)
            gCancelled = true;
        TestRecord(row, &m, unitNsPerInsn, (double)kDefs[k].insns * kDefs[k].inner);
    }
}

void Suite68k(void)
{
    K68 c;
    const uint32_t *offsets = (const uint32_t *)k68_code;
    int k;
    memset(&c, 0, sizeof c);
    c.code = NewPtr(sizeof k68_code);
    c.src = (unsigned char *)NewPtr(16384);
    c.dst = (unsigned char *)NewPtr(16384);
    if (!c.code || !c.src || !c.dst) {
        RowSkip(TestBegin(kSuite, "68k loops", unitNsPerInsn), "not enough memory");
        goto done;
    }
    memcpy(c.code, k68_code, sizeof k68_code);
    for (k = 0; k < 16384; k++)
        c.src[k] = (unsigned char)(k * 13 + 7);
    for (k = 0; k < kKernels; k++) {
        c.info[k] = kCStackBased | SB_RESULT_SIZE(kFourByteCode) | SB_STACK_PARAM(1, kFourByteCode);
        if (kDefs[k].args == 3)
            c.info[k] |= SB_STACK_PARAM(2, kFourByteCode) | SB_STACK_PARAM(3, kFourByteCode);
        c.upp[k] = NewRoutineDescriptor((ProcPtr)(c.code + offsets[k]), c.info[k], kM68kISA);
    }
    RunPass(&c, "");
    /* The host 68k JIT alone: only in the main pass, only when the host lets SheepBench switch and it is on. */
    if (gPass == passMain && !gCancelled && HostPresent() && HostInfo("bench_switch_allowed") &&
        HostInfo("bench_switch_allowed")[0] == '1' && HostInfo("jit68k_host_at_start") &&
        !strcmp(HostInfo("jit68k_host_at_start"), "on")) {
        const char *ppc = HostInfo("jit"), *qd = HostInfo("sheepforce_qd"), *sf = HostInfo("sheepforce");
        Boolean ppcOn = ppc && !strcmp(ppc, "on"), qdOn = sf && sf[0] == '1' && qd && qd[0] == '1';
        long why;
        if (HostSetModes(ppcOn, false, qdOn, false, &why) == hostOk) {
            unsigned long until = TickCount() + 120;
            while (TickCount() < until && !(HostInfoRefresh() && HostInfo("switch_pending") &&
                                            HostInfo("switch_pending")[0] == '0'))
                ;
            RunPass(&c, " [host 68k JIT off]");
            HostSetModes(true, true, true, true, &why);
            until = TickCount() + 120;
            while (TickCount() < until && !(HostInfoRefresh() && HostInfo("switch_pending") &&
                                            HostInfo("switch_pending")[0] == '0'))
                ;
        }
    }
    for (k = 0; k < kKernels; k++)
        if (c.upp[k])
            DisposeRoutineDescriptor(c.upp[k]);
done:
    if (c.code) DisposePtr(c.code);
    if (c.src) DisposePtr((Ptr)c.src);
    if (c.dst) DisposePtr((Ptr)c.dst);
}
