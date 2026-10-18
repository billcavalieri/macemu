/*
 *  suite_common.c - Helpers shared by SheepBench's suites (see suites.h).
 *
 *  (C) 2026 Bill Cavalieri
 */

#include "suites.h"
#include "bench_host.h"
#include "kernels/sb_cases.h"
#include "sb_expected.h"
#include <Memory.h>
#include <stdio.h>
#include <string.h>

int gPass;
Boolean gCancelled;

extern void UiStatus(const char *text);     /* main.c */

void Say(const char *text)
{
    UiStatus(text);
    HostLog(text);
}

static const char *UnitName(Unit u)
{
    switch (u) {
    case unitNsPerInsn: return "ns/insn";
    case unitMPerSec: return "M/s";
    case unitMBPerSec: return "MB/s";
    case unitNs: return "ns";
    case unitPerSec: return "/s";
    default: return "us";
    }
}

static Boolean HigherIsBetter(Unit u)
{
    return u == unitMPerSec || u == unitMBPerSec || u == unitPerSec;
}

double UnitValue(Unit unit, double s, double work)
{
    if (s <= 0)
        return NO_VALUE;
    switch (unit) {
    case unitNsPerInsn: return s * 1e9 / work;
    case unitMPerSec: return work / s / 1e6;
    case unitMBPerSec: return work / s / 1e6;
    case unitNs: return s * 1e9 / work;
    case unitPerSec: return work / s;
    default: return s * 1e6 / work;
    }
}

static const char *PassName(void)
{
    return gPass == passJitOff ? " (JIT off)" : gPass == passGpuOff ? " (GPU off)" : "";
}

BenchRow *TestBegin(const char *suite, const char *test, Unit unit)
{
    char line[120];
    BenchRow *row = ReportRow(&gReport, suite, test);
    sprintf(line, "%s: %s%s", suite, test, PassName());
    Say(line);
    if (!row)
        return NULL;
    strcpy(row->unit, UnitName(unit));
    row->higherIsBetter = HigherIsBetter(unit);
    if (gPass == passMain) {
        const char *jit = HostInfo("jit"), *qd = HostInfo("sheepforce_qd"), *sf = HostInfo("sheepforce");
        snprintf(row->jit, sizeof row->jit, "%s", jit ? jit : "unknown");
        strcpy(row->gpu, !sf ? "unknown" : (sf[0] == '1' && qd && qd[0] == '1') ? "on" : "off");
        row->status = rowSkipped;
        strcpy(row->why, "not run");
    }
    return row;
}

void TestRecord(BenchRow *row, const Measurement *m, Unit unit, double work)
{
    char line[256], v[24];
    double value;
    if (!row)
        return;
    if (m->cancelled && m->reps < 1) {
        if (gPass == passMain)
            RowSkip(row, "stopped");
        return;
    }
    value = UnitValue(unit, m->secondsPerIter, work);
    if (gPass == passMain) {
        row->value = value;
        row->noise = m->noise;
        row->reps = m->reps;
        row->status = m->settled ? rowOk : rowNoisy;
        row->why[0] = 0;
    } else if (gPass == passJitOff) {
        row->jitOff = value;
    } else {
        row->gpuOff = value;
    }
    FormatValue(v, value);
    sprintf(line, "RESULT %s | %s%s | %s %s | noise %.1f%% | reps %d", row->suite, row->test, PassName(), v, row->unit,
            m->noise * 100.0, m->reps);
    HostLog(line);
}

void TestRecordNative(BenchRow *row, double nsPerIter, Unit unit, double work)
{
    if (row && nsPerIter > 0)
        row->native = UnitValue(unit, nsPerIter * 1e-9, work);
}

Boolean ExpectedChecksum(unsigned kernel, uint32_t param, uint32_t *iters, uint32_t *sum)
{
    int i;
    for (i = 0; i < SB_NCASES; i++)
        if (kSbCases[i].kernel == kernel && kSbCases[i].param == param) {
            *iters = kSbCases[i].iters;
            *sum = kSbExpected[i];
            return true;
        }
    return false;
}

void *BenchAlloc(size_t bytes, Handle *temp)
{
    OSErr err;
    *temp = NULL;
    if (bytes == 0)
        bytes = 16;
    if (bytes < 512 * 1024) {
        void *p = NewPtr(bytes);
        if (p)
            return p;
    }
    *temp = TempNewHandle(bytes, &err);
    if (*temp && err == noErr) {
        HLock(*temp);
        return **temp;
    }
    if (*temp)
        DisposeHandle(*temp);
    *temp = NULL;
    return NewPtr(bytes);
}

void BenchFree(void *p, Handle temp)
{
    if (temp)
        DisposeHandle(temp);
    else if (p)
        DisposePtr((Ptr)p);
}

typedef struct {
    unsigned kernel;
    uint32_t param;
    void *scratch;
} KernelCtx;

static uint32_t KernelBody(void *c, uint32_t iters)
{
    KernelCtx *k = (KernelCtx *)c;
    return sb_run(k->kernel, iters, k->param, k->scratch);
}

Boolean RunKernelTest(const char *suite, const char *test, unsigned kernel, uint32_t param, Unit unit, double work)
{
    BenchRow *row = TestBegin(suite, test, unit);
    uint32_t iters, want, got;
    Handle temp;
    KernelCtx k;
    Measurement m;
    char why[64];
    if (!row || gCancelled)
        return false;
    if (!ExpectedChecksum(kernel, param, &iters, &want)) {
        RowFail(row, "no expected checksum for this case");
        return false;
    }
    k.kernel = kernel;
    k.param = param;
    k.scratch = BenchAlloc(sb_scratch_bytes(kernel, param), &temp);
    if (!k.scratch) {
        if (gPass == passMain) {
            sprintf(why, "not enough memory (%lu KB)", (unsigned long)(sb_scratch_bytes(kernel, param) / 1024));
            RowSkip(row, why);
        }
        return false;
    }
    sb_prepare(kernel, param, k.scratch);
    got = sb_run(kernel, iters, param, k.scratch);
    if (got != want) {
        sprintf(why, "checksum %08lx, expected %08lx%s", (unsigned long)got, (unsigned long)want, PassName());
        RowFail(row, why);
        Say(why);
        BenchFree(k.scratch, temp);
        return false;
    }
    if (gPass == passMain && HostPresent()) {
        double ns;
        uint32_t native;
        if (HostNative(kernel, iters, param, &ns, &native)) {
            if (native != want) {
                sprintf(why, "host checksum %08lx, expected %08lx", (unsigned long)native, (unsigned long)want);
                RowFail(row, why);
                Say(why);
                BenchFree(k.scratch, temp);
                return false;
            }
            TestRecordNative(row, ns, unit, work);
        }
    }
    Measure(KernelBody, &k, 1, &m);
    if (m.cancelled)
        gCancelled = true;
    TestRecord(row, &m, unit, work);
    BenchFree(k.scratch, temp);
    return true;
}
