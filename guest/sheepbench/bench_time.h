/*
 *  bench_time.h - SheepBench's clock and its measurement rule.
 *
 *  The clock is the PowerPC time base (mftb). Its rate comes from the host (benchInfo tb_hz); without a host it is
 *  measured against the tick count once.
 *
 *  The rule, the same for every test: one repetition is made long enough to take at least 20 ms (fewer in a quick
 *  run), then at least five repetitions are timed and the median taken. Noise is the median absolute deviation as a
 *  share of the median; repetitions are added until it is under 2 %, or until 25 repetitions or the test's time
 *  budget. A result that never gets there is reported as noisy.
 *
 *  (C) 2026 Bill Cavalieri
 */

#ifndef BENCH_TIME_H
#define BENCH_TIME_H

#include <MacTypes.h>
#include <stdint.h>

uint64_t TbNow(void);
/* Time base ticks per second (calibrated on first use when the host did not say). */
double TbHz(void);
void TbSetHz(double hz);
double TbSeconds(uint64_t ticks);

/* A test body: runs `iters` iterations of the work and returns a value that depends on it (kept so the compiler
 * cannot drop the work). */
typedef uint32_t (*BenchBody)(void *ctx, uint32_t iters);

typedef struct {
    double secondsPerIter;      /* median */
    double noise;               /* median absolute deviation / median */
    int reps;
    Boolean settled;            /* noise < 2 % */
    Boolean cancelled;
    uint32_t iters;             /* per repetition */
} Measurement;

/* Settings for one run (set by the main window). */
typedef struct {
    double minRepSeconds;       /* 0.020, quick 0.010 */
    int minReps;                /* 5 */
    int maxReps;                /* 25, quick 9 */
    double budgetSeconds;       /* per test: 3.0, quick 1.0; multiplied by `scale` (JIT off runs use less) */
    double scale;
} MeasureRules;

extern MeasureRules gRules;

/* Called between repetitions (never inside one): keeps the app responsive. Returns true when the user stopped
 * the run. Set by the main program. */
extern Boolean (*gBenchYield)(void);

/* Measures `body`. startIters is the first guess; it is doubled (or scaled) until one repetition is long enough. */
void Measure(BenchBody body, void *ctx, uint32_t startIters, Measurement *out);

#endif
