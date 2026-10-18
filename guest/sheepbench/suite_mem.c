/*
 *  suite_mem.c - Memory suite: copy (memcpy, a hand-written 8-byte loop, BlockMove and BlockMoveData) and fill at
 *  4 KB, 64 KB, 1 MB and 16 MB, and dependent-load latency around a shuffled ring of the same sizes. Blocks of 1 MB
 *  and more come from temporary memory; a size that cannot be had is reported as skipped.
 *
 *  (C) 2026 Bill Cavalieri
 */

#include "suites.h"
#include "bench_host.h"
#include <Memory.h>
#include <stdio.h>
#include <string.h>

static const char kSuite[] = "memory";
static const uint32_t kSizes[] = { 4096, 65536, 1048576, 16777216 };

static const char *SizeName(uint32_t bytes)
{
    static char s[16];
    if (bytes >= 1048576)
        sprintf(s, "%lu MB", (unsigned long)(bytes >> 20));
    else
        sprintf(s, "%lu KB", (unsigned long)(bytes >> 10));
    return s;
}

typedef struct {
    unsigned char *src, *dst;
    uint32_t bytes;
    Boolean data;       /* BlockMoveData rather than BlockMove */
} MoveCtx;

static uint32_t MoveBody(void *c, uint32_t iters)
{
    MoveCtx *m = (MoveCtx *)c;
    uint32_t i;
    for (i = 0; i < iters; i++) {
        if (m->data)
            BlockMoveData(m->src, m->dst, m->bytes);
        else
            BlockMove(m->src, m->dst, m->bytes);
    }
    return m->dst[0];
}

/* BlockMove(Data): the same data and checksum as SB_K_COPY (sb_run with no iterations sums the destination). */
static void RunBlockMove(uint32_t bytes, Boolean data)
{
    char name[48];
    BenchRow *row;
    uint32_t iters, want, got;
    Handle temp;
    void *s;
    MoveCtx ctx;
    Measurement m;
    char why[64];
    sprintf(name, "%s, %s", data ? "BlockMoveData" : "BlockMove", SizeName(bytes));
    row = TestBegin(kSuite, name, unitMBPerSec);
    if (!row || gCancelled)
        return;
    ExpectedChecksum(SB_K_COPY, bytes, &iters, &want);
    s = BenchAlloc(sb_scratch_bytes(SB_K_COPY, bytes), &temp);
    if (!s) {
        if (gPass == passMain) {
            sprintf(why, "not enough memory (%lu KB)", (unsigned long)(sb_scratch_bytes(SB_K_COPY, bytes) / 1024));
            RowSkip(row, why);
        }
        return;
    }
    sb_prepare(SB_K_COPY, bytes, s);
    ctx.src = (unsigned char *)(((uint32_t)s + 15) & ~15UL);
    ctx.dst = ctx.src + bytes;
    ctx.bytes = bytes;
    ctx.data = data;
    MoveBody(&ctx, 1);
    got = sb_run(SB_K_COPY, 0, bytes, s);
    if (got != want) {
        sprintf(why, "checksum %08lx, expected %08lx", (unsigned long)got, (unsigned long)want);
        RowFail(row, why);
        Say(why);
    } else {
        double ns;
        uint32_t native;
        if (gPass == passMain && HostNative(SB_K_COPY, iters, bytes, &ns, &native) && native == want)
            TestRecordNative(row, ns, unitMBPerSec, bytes);
        Measure(MoveBody, &ctx, 1, &m);
        if (m.cancelled)
            gCancelled = true;
        TestRecord(row, &m, unitMBPerSec, bytes);
    }
    BenchFree(s, temp);
}

void SuiteMemory(void)
{
    int i;
    char name[48];
    for (i = 0; i < 4 && !gCancelled; i++) {
        uint32_t b = kSizes[i];
        sprintf(name, "copy (memcpy), %s", SizeName(b));
        RunKernelTest(kSuite, name, SB_K_COPY, b, unitMBPerSec, b);
        sprintf(name, "copy (8-byte loop), %s", SizeName(b));
        RunKernelTest(kSuite, name, SB_K_WORDCOPY, b, unitMBPerSec, b);
        RunBlockMove(b, false);
        RunBlockMove(b, true);
        sprintf(name, "fill (memset), %s", SizeName(b));
        RunKernelTest(kSuite, name, SB_K_FILL, b, unitMBPerSec, b);
        sprintf(name, "dependent load latency, %s ring", SizeName(b));
        RunKernelTest(kSuite, name, SB_K_CHASE, b / 4, unitNs, 1);
    }
}
