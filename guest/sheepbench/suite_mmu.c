/*
 *  suite_mmu.c - MMU suite. A program cannot see the BATs or the page table, so these are indirect:
 *    - the page size and virtual memory state (Gestalt), in the run info;
 *    - loads one per page (4 KB stride) over 16, 256 and 4096 pages, against loads that stay in one page: the
 *      difference is what a translation miss costs (in the emulator's TLB and in the host's);
 *    - the first write to each page of a freshly allocated block;
 *    - self-modifying code: a routine is rewritten, made executable (MakeDataExecutable) and called, again and again;
 *      that measures how fast the JIT notices and recompiles changed code;
 *    - MakeDataExecutable alone on a 4 KB page.
 *  Results are checked: the loads sum to a known value, the rewritten routine returns the value just written into it.
 *
 *  (C) 2026 Bill Cavalieri
 */

#include "suites.h"
#include "codebuf.h"
#include <Gestalt.h>
#include <Memory.h>
#include <stdio.h>
#include <string.h>

static const char kSuite[] = "mmu";

typedef struct {
    volatile uint32_t *base;
    uint32_t pages;
    uint32_t stride;        /* in words */
} StrideCtx;

static uint32_t StrideBody(void *p, uint32_t iters)
{
    StrideCtx *c = (StrideCtx *)p;
    uint32_t it, i, sum = 0;
    for (it = 0; it < iters; it++)
        for (i = 0; i < c->pages; i++)
            sum += c->base[i * c->stride];
    return sum;
}

static void RunStride(void)
{
    static const uint32_t counts[] = { 16, 256, 4096 };
    Handle temp;
    void *raw = BenchAlloc(4096 * 4096 + 4096, &temp);
    uint32_t *base;
    int k;
    if (!raw) {
        RowSkip(TestBegin(kSuite, "loads one per page", unitNs), "not enough memory (16 MB)");
        return;
    }
    base = (uint32_t *)(((uint32_t)raw + 4095) & ~4095UL);
    for (k = 0; k < 4096; k++)
        base[k * 1024] = (uint32_t)k;
    for (k = 0; k < 3 && !gCancelled; k++) {
        char name[64];
        StrideCtx c;
        Measurement m;
        BenchRow *row;
        uint32_t want = counts[k] * (counts[k] - 1) / 2, got;
        int same;
        for (same = 0; same < 2 && !gCancelled; same++) {
            if (same)
                sprintf(name, "%lu loads in one page (reference)", (unsigned long)counts[k]);
            else
                sprintf(name, "loads one per page, %lu pages", (unsigned long)counts[k]);
            row = TestBegin(kSuite, name, unitNs);
            if (!row)
                continue;
            c.base = base;
            c.pages = counts[k];
            c.stride = same ? 0 : 1024;
            got = StrideBody(&c, 1);
            if (!same && got != want) {
                char why[64];
                sprintf(why, "sum %lu, expected %lu", (unsigned long)got, (unsigned long)want);
                RowFail(row, why);
                continue;
            }
            Measure(StrideBody, &c, 1, &m);
            if (m.cancelled)
                gCancelled = true;
            TestRecord(row, &m, unitNs, (double)counts[k]);
        }
    }
    BenchFree(raw, temp);
}

typedef struct {
    uint32_t bytes;
    long bad;
} TouchCtx;

static uint32_t TouchBody(void *p, uint32_t iters)
{
    TouchCtx *c = (TouchCtx *)p;
    uint32_t it, i;
    for (it = 0; it < iters; it++) {
        Handle temp;
        unsigned char *b = (unsigned char *)BenchAlloc(c->bytes, &temp);
        if (!b) {
            c->bad++;
            continue;
        }
        for (i = 0; i < c->bytes; i += 4096)
            b[i] = (unsigned char)i;
        BenchFree(b, temp);
    }
    return iters;
}

typedef struct {
    CodeBuf cb;
    uint32_t *code;
    uint32_t (*fn)(void);
    uint32_t value;
    long bad, calls;
    long firstBad;          /* which call (counting from 1) first ran old code */
    uint32_t firstGot, firstWant;
} SmcCtx;

static uint32_t SmcBody(void *p, uint32_t iters)
{
    SmcCtx *c = (SmcCtx *)p;
    uint32_t i;
    for (i = 0; i < iters; i++) {
        c->value = (c->value + 1) & 0x7FFF;
        c->code[0] = 0x38600000UL | c->value;        /* li r3,value */
        c->code[1] = 0x4E800020UL;                   /* blr */
        MakeDataExecutable(c->code, 8);
        c->calls++;
        {
            uint32_t got = c->fn();
            if (got != c->value && c->bad++ == 0) {
                c->firstBad = c->calls;
                c->firstGot = got;
                c->firstWant = c->value;
            }
        }
    }
    return iters;
}

static uint32_t FlushBody(void *p, uint32_t iters)
{
    SmcCtx *c = (SmcCtx *)p;
    uint32_t i;
    for (i = 0; i < iters; i++)
        MakeDataExecutable(c->code, 4096);
    return iters;
}

void SuiteMMU(void)
{
    long g;
    char v[32];
    if (gPass == passMain) {
        if (Gestalt('pgsz', &g) == noErr) {
            sprintf(v, "%ld", g);
            ReportInfo(&gReport, "mmu_page_size", v);
        }
        if (Gestalt('vm  ', &g) == noErr)
            ReportInfo(&gReport, "mmu_virtual_memory", (g & 1) ? "on" : "off");
    }
    RunStride();
    if (!gCancelled) {
        BenchRow *row = TestBegin(kSuite, "first write to each page of a new 4 MB block", unitNs);
        TouchCtx c = { 4 * 1024 * 1024, 0 };
        Measurement m;
        if (row) {
            Measure(TouchBody, &c, 1, &m);
            if (m.cancelled)
                gCancelled = true;
            if (c.bad)
                RowSkip(row, "could not allocate 4 MB");
            else
                TestRecord(row, &m, unitNs, (double)(c.bytes / 4096));
        }
    }
    if (!gCancelled) {
        SmcCtx c;
        memset(&c, 0, sizeof c);
        if (CodeBufOpen(&c.cb, 1)) {
            BenchRow *row = TestBegin(kSuite, "rewrite, flush and call a routine (self-modifying code)", unitUs);
            Measurement m;
            c.code = (uint32_t *)CodeBufPage(&c.cb, 0);
            c.fn = (uint32_t (*)(void))CodeBufEntry(&c.cb, c.code);
            if (row) {
                Measure(SmcBody, &c, 1, &m);
                if (m.cancelled)
                    gCancelled = true;
                if (c.bad) {
                    char why[128];
                    sprintf(why, "%ld of %ld calls ran old code (first: call %ld returned %lu, not %lu)", c.bad,
                            c.calls, c.firstBad, (unsigned long)c.firstGot, (unsigned long)c.firstWant);
                    RowFail(row, why);
                    Say(why);
                } else {
                    TestRecord(row, &m, unitUs, 1);
                }
            }
            row = TestBegin(kSuite, "MakeDataExecutable, 4 KB", unitUs);
            if (row && !gCancelled) {
                Measure(FlushBody, &c, 1, &m);
                if (m.cancelled)
                    gCancelled = true;
                TestRecord(row, &m, unitUs, 1);
            }
            CodeBufClose(&c.cb);
        } else {
            RowSkip(TestBegin(kSuite, "self-modifying code", unitUs), "not enough memory");
        }
    }
}
