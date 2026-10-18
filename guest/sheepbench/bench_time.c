/*
 *  bench_time.c - SheepBench's clock and measurement rule (see bench_time.h).
 *
 *  (C) 2026 Bill Cavalieri
 */

#include "bench_time.h"
#include <Events.h>
#include <stdlib.h>

MeasureRules gRules = { 0.020, 5, 25, 3.0, 1.0 };
Boolean (*gBenchYield)(void);

static double gHz;

uint64_t TbNow(void)
{
    uint32_t hi, lo, hi2;
    do {
        __asm__ volatile("mftbu %0" : "=r"(hi));
        __asm__ volatile("mftb %0" : "=r"(lo));
        __asm__ volatile("mftbu %0" : "=r"(hi2));
    } while (hi != hi2);
    return ((uint64_t)hi << 32) | lo;
}

void TbSetHz(double hz)
{
    if (hz > 1000.0)
        gHz = hz;
}

double TbHz(void)
{
    if (gHz <= 0) {
        /* No host: count time base ticks over 30 ticks of the 60.15 Hz tick counter. */
        unsigned long t0 = TickCount(), t1;
        uint64_t a, b;
        while ((t1 = TickCount()) == t0)
            ;
        a = TbNow();
        while (TickCount() < t1 + 30)
            ;
        b = TbNow();
        gHz = (double)(b - a) / (30.0 / 60.15);
    }
    return gHz;
}

double TbSeconds(uint64_t ticks)
{
    return (double)ticks / TbHz();
}

static int CompareDoubles(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static double Median(double *v, int n)
{
    qsort(v, n, sizeof *v, CompareDoubles);
    return n & 1 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

static volatile uint32_t gSink;

static double TimeOnce(BenchBody body, void *ctx, uint32_t iters)
{
    uint64_t t0 = TbNow();
    gSink = body(ctx, iters);
    return TbSeconds(TbNow() - t0);
}

void Measure(BenchBody body, void *ctx, uint32_t startIters, Measurement *out)
{
    double times[64], dev[64], t, med = 0, mad = 0, spent = 0;
    double minRep = gRules.minRepSeconds, budget = gRules.budgetSeconds * gRules.scale;
    uint32_t iters = startIters ? startIters : 1;
    int n = 0, maxReps = gRules.maxReps, i;

    out->cancelled = false;
    /* Long enough? Grow by the factor still missing (at least x2) until one repetition takes minRep. */
    for (;;) {
        t = TimeOnce(body, ctx, iters);
        spent += t;
        if (t >= minRep || iters >= 0x40000000UL)
            break;
        if (gBenchYield && gBenchYield()) {
            out->cancelled = true;
            break;
        }
        {
            double f = t > 0 ? minRep * 1.25 / t : 64;
            if (f < 2)
                f = 2;
            if (f > 64)
                f = 64;
            if ((double)iters * f >= 1073741824.0)
                iters = 0x40000000UL;
            else
                iters = (uint32_t)((double)iters * f);
        }
    }
    if (maxReps > 64)
        maxReps = 64;
    times[n++] = t;
    while (!out->cancelled) {
        if (n >= gRules.minReps) {
            for (i = 0; i < n; i++)
                dev[i] = times[i];
            med = Median(dev, n);
            for (i = 0; i < n; i++)
                dev[i] = times[i] > med ? times[i] - med : med - times[i];
            mad = Median(dev, n);
            if (med > 0 && mad / med < 0.02)
                break;
            if (n >= maxReps || spent >= budget)
                break;
        }
        if (gBenchYield && gBenchYield()) {
            out->cancelled = true;
            break;
        }
        t = TimeOnce(body, ctx, iters);
        spent += t;
        times[n++] = t;
    }
    if (n < gRules.minReps) {          /* cancelled early: still report what there is */
        for (i = 0; i < n; i++)
            dev[i] = times[i];
        med = Median(dev, n);
        mad = 0;
    }
    out->secondsPerIter = med / iters;
    out->noise = med > 0 ? mad / med : 0;
    out->reps = n;
    out->settled = med > 0 && out->noise < 0.02;
    out->iters = iters;
}
