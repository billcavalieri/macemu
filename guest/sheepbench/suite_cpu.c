/*
 *  suite_cpu.c - PowerPC CPU suite.
 *
 *  1. The seven loops of the emulator's own loop benchmark (kpx_cpu/tests/ppc_core_harness.cpp, --loop-bench), as the
 *     same instruction words: byte copy, the ROM's BlockMove loop, a compare loop shaped like MacBench's integer
 *     test, calls in the same page and to another page, bctrl + blr, and a conditional return. They are written into
 *     a buffer at run time, so the callee lands one page away from the caller as in the harness. Reported in ns per
 *     guest instruction; each one's result is checked (the copy, the loop count, the number of calls).
 *  2. The shared C kernels (kernels/sb_kernels.c): integer, compare, calls, atomic increment, double and single FP,
 *     and the same vector loop in C and in AltiVec. These are also run natively by the host, which gives the
 *     percent-of-native column.
 *
 *  (C) 2026 Bill Cavalieri
 */

#include "suites.h"
#include "codebuf.h"
#include "bench_host.h"
#include <Memory.h>
#include <stdio.h>
#include <string.h>

static const char kSuite[] = "cpu";

/* Instruction encoders (as in the harness). */
static uint32_t D(unsigned op, unsigned rt, unsigned ra, int d) { return (op << 26) | (rt << 21) | (ra << 16) | (uint16_t)d; }
static uint32_t X31(unsigned rt, unsigned ra, unsigned rb, unsigned xo) { return (31u << 26) | (rt << 21) | (ra << 16) | (rb << 11) | (xo << 1); }
static uint32_t BC(unsigned bo, unsigned bi, int disp) { return (16u << 26) | (bo << 21) | (bi << 16) | ((uint16_t)disp & 0xfffcu); }
static uint32_t BL(int disp) { return (18u << 26) | ((uint32_t)disp & 0x3fffffcu) | 1u; }
#define LBZU(rd, ra, d)  D(35, rd, ra, d)
#define STBU(rs, ra, d)  D(39, rs, ra, d)
#define LFD(fd, ra, d)   D(50, fd, ra, d)
#define STFD(fs, ra, d)  D(54, fs, ra, d)
#define ADDI(rd, ra, i)  D(14, rd, ra, i)
#define LI(rd, i)        D(14, rd, 0, i)
#define ADDIC_(rd, ra, i) D(13, rd, ra, i)
#define CMPWI(ra, i)     D(11, 0, ra, i)
#define MR(ra, rs)       X31(rs, ra, rs, 444)
#define MFLR(r)          X31(r, 8, 0, 339)
#define MTLR(r)          X31(r, 8, 0, 467)
#define MTCTR(r)         X31(r, 9, 0, 467)
#define BDNZ(disp)       BC(16, 0, disp)
#define BCTRL            ((19u << 26) | (20u << 21) | (528u << 1) | 1u)
#define BEQLR            ((19u << 26) | (12u << 21) | (2u << 16) | (16u << 1))
#define BLR              0x4e800020u
#define NOP              0x60000000u

typedef uint32_t (*Kernel3)(uint32_t, uint32_t, uint32_t);

typedef struct {
    const char *name;
    unsigned insnsPerIter;
    uint32_t count;         /* inner iterations per call (the harness's) */
    int leafFar;            /* the leaf goes in the page before the code */
    int which;              /* 0 copy, 1 blockmove, 2 compare, 3 call */
} LoopDef;

static const LoopDef kLoops[] = {
    { "loop: byte copy", 3, 3000, 0, 0 },
    { "loop: BlockMove", 7, 200, 0, 1 },
    { "loop: compare (MacBench integer shape)", 14, 2047, 0, 2 },
    { "loop: call, same page", 4, 1500, 0, 3 },
    { "loop: call, other page", 4, 1500, 1, 3 },
    { "loop: bctrl + blr", 6, 1500, 1, 4 },
    { "loop: conditional blr", 5, 1500, 1, 5 },
};

/* Writes the loop's words; returns how many. The callee (when far) goes at `leaf`, the code at `code`. The kernel
 * takes r3, r4 (data) and r5 (count), keeps LR in r12 (volatile, unlike the harness's r31) and returns r3. */
static int BuildLoop(const LoopDef *l, uint32_t *code, uint32_t *leaf)
{
    int n = 0;
    switch (l->which) {
    case 0:     /* r3 = src - 1, r4 = dst - 1 */
        code[n++] = MTCTR(5);
        code[n++] = LBZU(6, 3, 1);
        code[n++] = STBU(6, 4, 1);
        code[n++] = BDNZ(-8);
        code[n++] = BLR;
        break;
    case 1:     /* r3 = src, r4 = dst */
        code[n++] = MTCTR(5);
        code[n++] = LFD(0, 3, 0);
        code[n++] = LFD(1, 3, 8);
        code[n++] = ADDI(3, 3, 16);
        code[n++] = STFD(0, 4, 0);
        code[n++] = STFD(1, 4, 8);
        code[n++] = ADDI(4, 4, 16);
        code[n++] = BDNZ(-24);
        code[n++] = BLR;
        break;
    case 2: {   /* r3 = the halfword 0x07ff (loop bound), r4 = a zeroed table; returns the loop count */
        code[n++] = MR(8, 3);
        code[n++] = MR(7, 4);
        code[n++] = LI(6, 1);
        code[n++] = LI(9, 0);
        code[n++] = LI(10, 0);
        code[n++] = LI(4, 0);
        /* top: */
        code[n++] = ADDI(10, 10, 1);
        code[n++] = X31(9, 4, 0, 922);          /* extsh r4,r9 */
        code[n++] = X31(0, 4, 6, 0);            /* cmpw r4,r6 */
        code[n++] = BC(4, 0, 0x2c);             /* bge exit */
        code[n++] = D(42, 0, 8, 0);             /* lha r0,0(r8) */
        code[n++] = X31(10, 5, 0, 922);         /* extsh r5,r10 */
        code[n++] = X31(0, 5, 0, 0);            /* cmpw r5,r0 */
        code[n++] = BC(4, 0, 0x1c);             /* bge exit */
        code[n++] = X31(4, 7, 4, 87);           /* lbzx r4,r7,r4 */
        code[n++] = X31(0, 7, 5, 87);           /* lbzx r0,r7,r5 */
        code[n++] = X31(4, 4, 0, 954);          /* extsb r4,r4 */
        code[n++] = X31(0, 0, 0, 954);          /* extsb r0,r0 */
        code[n++] = X31(0, 4, 0, 0);            /* cmpw r4,r0 */
        code[n++] = BC(12, 2, -0x34);           /* beq top */
        /* exit: */
        code[n++] = MR(3, 10);
        code[n++] = BLR;
        break;
    }
    case 3:     /* bl leaf, count times; returns the count */
        code[n++] = MFLR(12);
        code[n++] = MTCTR(5);
        code[n++] = LI(3, 0);
        if (l->leafFar) {
            code[n] = BL((int)((char *)leaf - (char *)&code[n]));
            n++;
            code[n++] = BDNZ(-4);
            code[n++] = MTLR(12);
            code[n++] = BLR;
            leaf[0] = ADDI(3, 3, 1);
            leaf[1] = BLR;
        } else {
            code[n++] = BL(20);                 /* to the leaf 5 words on */
            code[n++] = BDNZ(-4);
            code[n++] = MTLR(12);
            code[n++] = BLR;
            code[n++] = NOP;
            code[n++] = ADDI(3, 3, 1);
            code[n++] = BLR;
        }
        break;
    case 4:     /* mtctr r6 (the leaf); bctrl; addic. r7,r7,-1; bne */
        code[n++] = MFLR(12);
        code[n++] = MR(7, 5);
        code[n++] = LI(3, 0);
        code[n++] = LI(6, 0);                   /* patched below: the leaf's address */
        code[n++] = 0;
        code[n++] = MTCTR(6);
        code[n++] = BCTRL;
        code[n++] = ADDIC_(7, 7, -1);
        code[n++] = BC(4, 2, -12);
        code[n++] = MTLR(12);
        code[n++] = BLR;
        code[3] = D(15, 6, 0, (int)((uint32_t)leaf >> 16));                 /* lis r6,hi */
        code[4] = D(24, 6, 6, (int)((uint32_t)leaf & 0xffff));              /* ori r6,r6,lo */
        leaf[0] = ADDI(3, 3, 1);
        leaf[1] = BLR;
        break;
    case 5:     /* bl to a leaf with beqlr (not taken: cr0 says "not equal") then blr */
        code[n++] = MFLR(12);
        code[n++] = MTCTR(5);
        code[n++] = LI(3, 0);
        code[n++] = CMPWI(5, 0);                /* count > 0: cr0 eq clear */
        code[n] = BL((int)((char *)leaf - (char *)&code[n]));
        n++;
        code[n++] = BDNZ(-4);
        code[n++] = MTLR(12);
        code[n++] = BLR;
        leaf[0] = ADDI(3, 3, 1);
        leaf[1] = BEQLR;
        leaf[2] = BLR;
        break;
    }
    return n;
}

typedef struct {
    Kernel3 fn;
    uint32_t r3, r4, count;
    uint32_t last;
} LoopCtx;

static uint32_t LoopBody(void *c, uint32_t iters)
{
    LoopCtx *l = (LoopCtx *)c;
    uint32_t i, r = 0;
    for (i = 0; i < iters; i++)
        r += l->fn(l->r3, l->r4, l->count);
    l->last = r;
    return r;
}

static void RunLoops(void)
{
    CodeBuf cb;
    unsigned char *data;
    Handle temp;
    int i;
    if (!CodeBufOpen(&cb, 3)) {
        for (i = 0; i < (int)(sizeof kLoops / sizeof kLoops[0]); i++)
            RowSkip(TestBegin(kSuite, kLoops[i].name, unitNsPerInsn), "no memory for code");
        return;
    }
    data = (unsigned char *)BenchAlloc(0x4000, &temp);
    for (i = 0; i < (int)(sizeof kLoops / sizeof kLoops[0]) && !gCancelled; i++) {
        const LoopDef *l = &kLoops[i];
        BenchRow *row = TestBegin(kSuite, l->name, unitNsPerInsn);
        uint32_t *leaf = (uint32_t *)CodeBufPage(&cb, 0), *code = (uint32_t *)CodeBufPage(&cb, 1);
        LoopCtx ctx;
        Measurement m;
        uint32_t r, j;
        char why[64];
        if (!row)
            continue;
        if (!data) {
            RowSkip(row, "no memory");
            continue;
        }
        memset(leaf, 0, 64);
        CodeBufCommit(&cb, BuildLoop(l, code, leaf));
        ctx.fn = (Kernel3)CodeBufEntry(&cb, code);
        ctx.count = l->count;
        memset(data, 0, 0x4000);
        for (j = 0; j < 4096; j++)
            data[j] = (unsigned char)(j * 7);
        data[0x2000] = 0x07;
        data[0x2001] = 0xff;
        switch (l->which) {
        case 0: ctx.r3 = (uint32_t)data - 1; ctx.r4 = (uint32_t)data + 0x1000 - 1; break;
        case 1: ctx.r3 = (uint32_t)data; ctx.r4 = (uint32_t)data + 0x1000; break;
        case 2: ctx.r3 = (uint32_t)data + 0x2000; ctx.r4 = (uint32_t)data + 0x3000; break;
        default: ctx.r3 = ctx.r4 = 0; break;
        }
        /* Check one call before timing. */
        r = ctx.fn(ctx.r3, ctx.r4, ctx.count);
        why[0] = 0;
        if (l->which == 0 && memcmp(data, data + 0x1000, 3000) != 0)
            strcpy(why, "copy differs from source");
        else if (l->which == 1 && memcmp(data, data + 0x1000, 3200) != 0)
            strcpy(why, "copy differs from source");
        else if (l->which == 2 && r != 2047)
            sprintf(why, "loop ran %lu times, expected 2047", (unsigned long)r);
        else if (l->which >= 3 && r != l->count)
            sprintf(why, "%lu calls returned, expected %lu", (unsigned long)r, (unsigned long)l->count);
        if (why[0]) {
            RowFail(row, why);
            Say(why);
            continue;
        }
        Measure(LoopBody, &ctx, 4, &m);
        if (m.cancelled)
            gCancelled = true;
        TestRecord(row, &m, unitNsPerInsn, (double)l->insnsPerIter * l->count);
        /* No native column for these: they are instruction sequences, not C; the C kernels below have one. */
    }
    BenchFree(data, temp);
    CodeBufClose(&cb);
}

typedef struct {
    float *x, *y;
    uint32_t n;
    float *half4;
} VecCtx;

static uint32_t AltivecBody(void *c, uint32_t iters)
{
    VecCtx *v = (VecCtx *)c;
    AltivecVec(v->x, v->y, v->n, iters, v->half4);
    return (uint32_t)v->y[0];
}

static void RunAltivec(void)
{
    BenchRow *row = TestBegin(kSuite, "AltiVec: vmaddfp over 1024 floats", unitMPerSec);
    uint32_t iters, want, got;
    Handle temp;
    void *s;
    VecCtx v;
    Measurement m;
    char why[64];
    Ptr halfSpace = NULL;
    if (!row || gCancelled)
        return;
    if (!HasAltivec()) {
        if (gPass == passMain)
            RowSkip(row, "no AltiVec (Gestalt)");
        return;
    }
    ExpectedChecksum(SB_K_VEC, 1024, &iters, &want);
    s = BenchAlloc(sb_scratch_bytes(SB_K_VEC, 1024), &temp);
    if (!s || !(halfSpace = NewPtr(32))) {
        RowSkip(row, "no memory");
        BenchFree(s, temp);
        return;
    }
    v.half4 = (float *)(((uint32_t)halfSpace + 15) & ~15UL);
    v.half4[0] = v.half4[1] = v.half4[2] = v.half4[3] = 0.5f;
    sb_prepare(SB_K_VEC, 1024, s);
    sb_vec_arrays(s, 1024, &v.x, &v.y);
    v.n = 1024;
    AltivecVec(v.x, v.y, 1024, iters, v.half4);
    got = sb_vec_checksum(s, 1024);
    if (got != want) {
        /* Where it went wrong: the first element that differs from the C kernel's, as bit patterns. */
        Handle t2;
        void *ref = BenchAlloc(sb_scratch_bytes(SB_K_VEC, 1024), &t2);
        sprintf(why, "checksum %08lx, expected %08lx%s", (unsigned long)got, (unsigned long)want,
                gPass == passJitOff ? " (JIT off)" : "");
        if (ref) {
            float *rx, *ry;
            uint32_t i;
            sb_prepare(SB_K_VEC, 1024, ref);
            sb_run(SB_K_VEC, iters, 1024, ref);
            sb_vec_arrays(ref, 1024, &rx, &ry);
            for (i = 0; i < 1024; i++)
                if (*(uint32_t *)&ry[i] != *(uint32_t *)&v.y[i]) {
                    char line[160];
                    sprintf(line, "AltiVec differs at %lu: x %08lx, y %08lx, C %08lx", (unsigned long)i,
                            (unsigned long)*(uint32_t *)&v.x[i], (unsigned long)*(uint32_t *)&v.y[i],
                            (unsigned long)*(uint32_t *)&ry[i]);
                    HostLog(line);
                    break;
                }
            BenchFree(ref, t2);
        }
        RowFail(row, why);
        Say(why);
    } else {
        double ns;
        uint32_t native;
        if (gPass == passMain && HostNative(SB_K_VEC, iters, 1024, &ns, &native) && native == want)
            TestRecordNative(row, ns, unitMPerSec, 1024);
        Measure(AltivecBody, &v, 1, &m);
        if (m.cancelled)
            gCancelled = true;
        TestRecord(row, &m, unitMPerSec, 1024);
    }
    BenchFree(s, temp);
    DisposePtr(halfSpace);
}

void SuiteCPU(void)
{
    RunLoops();
    RunKernelTest(kSuite, "integer mix", SB_K_INT, 0, unitMPerSec, 1);
    RunKernelTest(kSuite, "byte copy (C), 4 KB", SB_K_BYTECOPY, 4096, unitMBPerSec, 4096);
    RunKernelTest(kSuite, "compare (C), 2048 entries", SB_K_COMPARE, 2048, unitMPerSec, 2048);
    RunKernelTest(kSuite, "call (C)", SB_K_CALL, 0, unitMPerSec, 1);
    RunKernelTest(kSuite, "call through a pointer (C)", SB_K_INDIRECT, 0, unitMPerSec, 1);
    RunKernelTest(kSuite, "atomic increment (lwarx/stwcx.)", SB_K_ATOMIC, 0, unitMPerSec, 1);
    RunKernelTest(kSuite, "double fma, dependent", SB_K_FPD_CHAIN, 0, unitMPerSec, 1);
    RunKernelTest(kSuite, "double fma, 4 independent", SB_K_FPD_THRU, 1024, unitMPerSec, 1024);
    RunKernelTest(kSuite, "single fma, dependent", SB_K_FPS_CHAIN, 0, unitMPerSec, 1);
    RunKernelTest(kSuite, "vector loop in C, 1024 floats", SB_K_VEC, 1024, unitMPerSec, 1024);
    RunAltivec();
}
