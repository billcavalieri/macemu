/*
 *  bench_report.c - SheepBench's results and report file (see bench_report.h).
 *
 *  (C) 2026 Bill Cavalieri
 */

#include "bench_report.h"
#include <Memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

BenchReport gReport;

static const char *const kColumns[] = {
    "suite", "test", "unit", "higher_is_better", "value", "noise_pct", "reps", "native", "pct_native", "slowdown_x",
    "jit_off", "jit_speedup_x", "gpu_off", "gpu_speedup_x", "jit", "gpu", "hook", "hook_hits", "kind", "status"
};
#define NCOLUMNS ((int)(sizeof kColumns / sizeof kColumns[0]))

static void Copy(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n >= cap)
        n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

void ReportClear(BenchReport *r)
{
    r->count = 0;
    r->info[0] = 0;
}

void ReportInfo(BenchReport *r, const char *key, const char *value)
{
    size_t used = strlen(r->info), need = strlen(key) + strlen(value) + 2;
    if (used + need + 1 > sizeof r->info)
        return;
    sprintf(r->info + used, "%s\t%s\n", key, value);
}

const char *ReportInfoValue(const BenchReport *r, const char *key)
{
    static char values[8][160];     /* used in turn, so several results can be held at once */
    static int turn;
    char *value = values[turn = (turn + 1) & 7];
    size_t kl = strlen(key);
    const char *p = r->info;
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len > kl && strncmp(p, key, kl) == 0 && p[kl] == '\t') {
            size_t vl = len - kl - 1;
            if (vl >= sizeof values[0])
                vl = sizeof values[0] - 1;
            memcpy(value, p + kl + 1, vl);
            value[vl] = 0;
            return value;
        }
        if (!end)
            break;
        p = end + 1;
    }
    return NULL;
}

/* Names are compared as they are stored (cut to the field's size), so a long name finds its row again. */
BenchRow *ReportFind(const BenchReport *r, const char *suite, const char *test)
{
    int i;
    for (i = 0; i < r->count; i++)
        if (strncmp(r->rows[i].suite, suite, sizeof r->rows[i].suite - 1) == 0 &&
            strncmp(r->rows[i].test, test, sizeof r->rows[i].test - 1) == 0)
            return (BenchRow *)&r->rows[i];
    return NULL;
}

BenchRow *ReportRow(BenchReport *r, const char *suite, const char *test)
{
    BenchRow *row = ReportFind(r, suite, test);
    if (row)
        return row;
    if (r->count >= MAX_ROWS)
        return NULL;
    row = &r->rows[r->count++];
    memset(row, 0, sizeof *row);
    Copy(row->suite, sizeof row->suite, suite);
    Copy(row->test, sizeof row->test, test);
    row->value = row->native = row->jitOff = row->gpuOff = NO_VALUE;
    strcpy(row->jit, "unknown");
    strcpy(row->gpu, "n/a");
    row->status = rowSkipped;
    strcpy(row->why, "not run");
    return row;
}

void RowSkip(BenchRow *row, const char *why)
{
    if (!row)
        return;
    row->status = rowSkipped;
    Copy(row->why, sizeof row->why, why);
}

void RowFail(BenchRow *row, const char *why)
{
    if (!row)
        return;
    row->status = rowFailed;
    Copy(row->why, sizeof row->why, why);
}

/* Ratio of "how good": for a higher-is-better unit a / b, for a time b / a ... both say "times faster". */
static double Faster(const BenchRow *row, double a, double b)
{
    if (a <= 0 || b <= 0 || row->status == rowFailed || row->status == rowSkipped)
        return NO_VALUE;
    return row->higherIsBetter ? a / b : b / a;
}

double RowPctNative(const BenchRow *row)
{
    double f = Faster(row, row->value, row->native);
    return f < 0 ? NO_VALUE : f * 100.0;
}

double RowSlowdown(const BenchRow *row)
{
    double f = Faster(row, row->native, row->value);
    return f;
}

double RowJitSpeedup(const BenchRow *row)
{
    return Faster(row, row->value, row->jitOff);
}

double RowGpuSpeedup(const BenchRow *row)
{
    return Faster(row, row->value, row->gpuOff);
}

void FormatValue(char *out, double v)
{
    if (v < 0) {
        out[0] = 0;
        return;
    }
    if (v >= 1000000.0)
        sprintf(out, "%.0f", v);
    else if (v >= 100.0)
        sprintf(out, "%.1f", v);
    else if (v >= 1.0)
        sprintf(out, "%.3f", v);
    else
        sprintf(out, "%.5f", v);
}

static const char *StatusText(const BenchRow *row, char *buf)
{
    switch (row->status) {
    case rowOk: return "ok";
    case rowNoisy: return "noisy";
    case rowSkipped: sprintf(buf, "skipped: %s", row->why); return buf;
    default: sprintf(buf, "FAILED: %s", row->why); return buf;
    }
}

long ReportText(const BenchReport *r, Boolean macLines, char **out)
{
    long cap = (long)strlen(r->info) * 2 + 512 + (long)r->count * 400, used = 0;
    char *t = NewPtr(cap), nl = macLines ? '\r' : '\n';
    const char *p;
    int i, c;
    *out = t;
    if (!t)
        return 0;
    /* run info */
    for (p = r->info; *p; ) {
        const char *end = strchr(p, '\n');
        long len = end ? (long)(end - p) : (long)strlen(p);
        t[used++] = '#';
        t[used++] = ' ';
        memcpy(t + used, p, len);
        used += len;
        t[used++] = nl;
        if (!end)
            break;
        p = end + 1;
    }
    for (c = 0; c < NCOLUMNS; c++)
        used += sprintf(t + used, "%s%s", c ? "\t" : "", kColumns[c]);
    t[used++] = nl;
    for (i = 0; i < r->count; i++) {
        const BenchRow *row = &r->rows[i];
        char v[11][24], st[140];
        FormatValue(v[0], row->value);
        FormatValue(v[1], row->value > 0 ? row->noise * 100.0 : NO_VALUE);
        FormatValue(v[2], row->native);
        FormatValue(v[3], RowPctNative(row));
        FormatValue(v[4], RowSlowdown(row));
        FormatValue(v[5], row->jitOff);
        FormatValue(v[6], RowJitSpeedup(row));
        FormatValue(v[7], row->gpuOff);
        FormatValue(v[8], RowGpuSpeedup(row));
        used += sprintf(t + used, "%s\t%s\t%s\t%d\t%s\t%s\t%d\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s",
                        row->suite, row->test, row->unit, row->higherIsBetter ? 1 : 0, v[0], v[1], row->reps, v[2],
                        v[3], v[4], v[5], v[6], v[7], v[8], row->jit, row->gpu, row->hook, row->hookHits,
                        "measured", StatusText(row, st));
        t[used++] = nl;
    }
    t[used] = 0;
    return used;
}

/* Splits one line in place at tabs. */
static int SplitTabs(char *line, char **cells, int max)
{
    int n = 0;
    cells[n++] = line;
    while (*line && n < max) {
        if (*line == '\t') {
            *line = 0;
            cells[n++] = line + 1;
        }
        line++;
    }
    return n;
}

static double Num(const char *s)
{
    return *s ? atof(s) : NO_VALUE;
}

Boolean ReportParse(BenchReport *r, const char *text, long length)
{
    char *copy = NewPtr(length + 1), *p, *line;
    int col[NCOLUMNS], ncol = 0, i, c;
    Boolean header = false;
    if (!copy)
        return false;
    memcpy(copy, text, length);
    copy[length] = 0;
    ReportClear(r);
    for (p = copy; *p; p++)
        if (*p == '\r')
            *p = '\n';
    for (line = copy; line && *line; ) {
        char *end = strchr(line, '\n');
        if (end)
            *end = 0;
        if (line[0] == '#') {
            char *k = line + 1, *tab;
            while (*k == ' ')
                k++;
            tab = strchr(k, '\t');
            if (tab) {
                *tab = 0;
                ReportInfo(r, k, tab + 1);
            }
        } else if (*line && !header) {
            char *cells[40];
            int n = SplitTabs(line, cells, 40);
            for (c = 0; c < NCOLUMNS; c++) {
                col[c] = -1;
                for (i = 0; i < n; i++)
                    if (strcmp(cells[i], kColumns[c]) == 0)
                        col[c] = i;
            }
            ncol = n;
            header = true;
            if (col[0] < 0 || col[1] < 0 || col[4] < 0)
                break;
        } else if (*line) {
            char *cells[40];
            int n = SplitTabs(line, cells, 40);
            BenchRow *row;
#define CELL(k) (col[k] >= 0 && col[k] < n ? cells[col[k]] : "")
            if (n < 2 || !(row = ReportRow(r, CELL(0), CELL(1))))
                goto next;
            Copy(row->unit, sizeof row->unit, CELL(2));
            row->higherIsBetter = atoi(CELL(3)) != 0;
            row->value = Num(CELL(4));
            row->noise = Num(CELL(5)) > 0 ? Num(CELL(5)) / 100.0 : 0;
            row->reps = atoi(CELL(6));
            row->native = Num(CELL(7));
            row->jitOff = Num(CELL(10));
            row->gpuOff = Num(CELL(12));
            Copy(row->jit, sizeof row->jit, CELL(14));
            Copy(row->gpu, sizeof row->gpu, CELL(15));
            Copy(row->hook, sizeof row->hook, CELL(16));
            Copy(row->hookHits, sizeof row->hookHits, CELL(17));
            {
                const char *st = CELL(19);
                if (strcmp(st, "ok") == 0)
                    row->status = rowOk;
                else if (strcmp(st, "noisy") == 0)
                    row->status = rowNoisy;
                else if (strncmp(st, "skipped: ", 9) == 0)
                    RowSkip(row, st + 9);
                else if (strncmp(st, "FAILED: ", 8) == 0)
                    RowFail(row, st + 8);
                else
                    RowSkip(row, st);
            }
#undef CELL
        }
next:
        line = end ? end + 1 : NULL;
    }
    (void)ncol;
    DisposePtr(copy);
    return header && r->count > 0;
}
