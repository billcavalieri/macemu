/*
 *  bench_report.h - SheepBench's results: one row per test, the run info, and the tab-separated report file.
 *
 *  Report format (also read back by Compare and by the host's benchResult):
 *    # key<TAB>value            run info (host build, PVR, JIT modes, SheepForce, disk image types, ...)
 *    suite<TAB>test<TAB>...     column names (kColumns), then one line per test
 *  Values are plain numbers in the row's unit; an empty cell means "not measured". Columns ending in _x and
 *  pct_native are derived (ratios of two measurements); everything else in a row was measured.
 *
 *  (C) 2026 Bill Cavalieri
 */

#ifndef BENCH_REPORT_H
#define BENCH_REPORT_H

#include <MacTypes.h>
#include <stdint.h>

#define NO_VALUE (-1.0)

typedef enum { rowOk, rowNoisy, rowSkipped, rowFailed } RowStatus;

typedef struct {
    char suite[12];
    char test[72];
    char unit[12];
    Boolean higherIsBetter;     /* MB/s and per-second units: true; ns per instruction: false */
    double value;               /* measured with the modes the VM started with */
    double noise;               /* share, 0.01 = 1 % */
    int reps;
    double native;              /* the same kernel natively on the host, same unit, or NO_VALUE */
    double jitOff;              /* with the JIT off, or NO_VALUE */
    double gpuOff;              /* with SheepForce QuickDraw off, or NO_VALUE */
    char jit[8];                /* JIT state for `value`: "on", "off", "unknown" */
    char gpu[8];                /* SheepForce QuickDraw for `value`: "on", "off", "n/a" */
    char hook[8];               /* the SheepForce QuickDraw hook the test should hit ("0", "2,3", "rom", "") */
    char hookHits[24];          /* hits counted by the host during the test, when it reports them */
    RowStatus status;
    char why[112];              /* skipped / failed: the reason */
} BenchRow;

#define MAX_ROWS 400

typedef struct {
    int count;
    BenchRow rows[MAX_ROWS];
    char info[6144];            /* "key\tvalue\n" lines */
} BenchReport;

extern BenchReport gReport;

void ReportClear(BenchReport *r);
void ReportInfo(BenchReport *r, const char *key, const char *value);
const char *ReportInfoValue(const BenchReport *r, const char *key);
/* Finds the row of suite/test, or adds it (NULL when full). */
BenchRow *ReportRow(BenchReport *r, const char *suite, const char *test);
BenchRow *ReportFind(const BenchReport *r, const char *suite, const char *test);
void RowSkip(BenchRow *row, const char *why);
void RowFail(BenchRow *row, const char *why);

/* Derived figures; NO_VALUE when either side is missing. */
double RowPctNative(const BenchRow *row);        /* 100 = native speed */
double RowSlowdown(const BenchRow *row);         /* guest time / native time */
double RowJitSpeedup(const BenchRow *row);
double RowGpuSpeedup(const BenchRow *row);

/* The report as text (Mac line ends when `macLines`). Returns the length; the buffer is allocated with NewPtr and
 * owned by the caller (DisposePtr). */
long ReportText(const BenchReport *r, Boolean macLines, char **out);
/* Reads a report written by ReportText (either line end). False when it is not one. */
Boolean ReportParse(BenchReport *r, const char *text, long length);

/* Number formatting without printf's %g (small and the same everywhere): 3 significant digits. */
void FormatValue(char *out, double v);

#endif
