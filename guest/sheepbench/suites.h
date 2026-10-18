/*
 *  suites.h - SheepBench's test suites and the helpers they share.
 *
 *  A suite runs once per pass: the main pass with the modes the VM started with, then, when asked for and allowed by
 *  the host, a pass with the JIT off and a pass with SheepForce QuickDraw off. Each pass writes its own column of
 *  the same rows.
 *
 *  (C) 2026 Bill Cavalieri
 */

#ifndef SUITES_H
#define SUITES_H

#include "bench_report.h"
#include "bench_time.h"
#include "kernels/sb_kernels.h"

enum { passMain, passJitOff, passGpuOff };
extern int gPass;
extern Boolean gCancelled;

/* Units. The value written is computed from the measured seconds per iteration and `work` (bytes, instructions or
 * operations per iteration). */
typedef enum {
    unitNsPerInsn,      /* ns per guest instruction (work = instructions per iteration); lower is better */
    unitMPerSec,        /* million iterations per second (work = operations per iteration) */
    unitMBPerSec,       /* MB/s (work = bytes per iteration) */
    unitNs,             /* ns per iteration (latency); lower is better */
    unitPerSec,         /* iterations per second */
    unitUs              /* microseconds per iteration; lower is better */
} Unit;

/* Starts a test: shows it in the window, logs it, and returns its row (NULL when the report is full). */
BenchRow *TestBegin(const char *suite, const char *test, Unit unit);
/* Stores a measurement in the column of the current pass; work as above. */
void TestRecord(BenchRow *row, const Measurement *m, Unit unit, double work);
/* Stores the native time (ns per iteration) in the row's unit. */
void TestRecordNative(BenchRow *row, double nsPerIter, Unit unit, double work);
/* Converts seconds per iteration to the unit. */
double UnitValue(Unit unit, double secondsPerIter, double work);

/* The checksum gen_expected.c computed for kernel/param, or false when the case is not in sb_cases.h. */
Boolean ExpectedChecksum(unsigned kernel, uint32_t param, uint32_t *iters, uint32_t *sum);

/* Runs a shared kernel as a test: allocates its scratch memory (temporary memory for big blocks), checks its
 * checksum (against sb_expected.h, and against the host's native run in the main pass), measures it and asks the
 * host for the native time. False when it was skipped or failed (already recorded in the row). */
Boolean RunKernelTest(const char *suite, const char *test, unsigned kernel, uint32_t param, Unit unit, double work);

/* Memory for a test: NewPtr for small blocks, temporary memory (locked) for large ones. */
void *BenchAlloc(size_t bytes, Handle *temp);
void BenchFree(void *p, Handle temp);

/* A log line for the tests (HostLog) and the status line of the main window. */
void Say(const char *text);

void SuiteCPU(void);
void SuiteMemory(void);
void Suite68k(void);
void SuiteMMU(void);
void SuiteQuickDraw(void);
void Suite3D(void);
void SuiteDisk(void);
void SuiteQD3D(void);       /* part of the 3D suite, in suite_qd3d.c */

/* AltiVec, in sb_altivec.c (compiled with -maltivec): y = y * 0.5 + x over n floats (n a multiple of 4, both
 * 16-byte aligned), `iters` passes; half4 is four floats of 0.5, 16-byte aligned. Same result as SB_K_VEC. */
void AltivecVec(float *x, float *y, uint32_t n, uint32_t iters, const float *half4);
Boolean HasAltivec(void);

#endif
