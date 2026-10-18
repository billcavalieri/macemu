/*
 *  sb_kernels.c - SheepBench measurement kernels (see sb_kernels.h). Plain C99 for both compilers: Retro68's GCC for
 *  the guest and clang for the host. Loops that a compiler would turn into a library call or delete are written
 *  so it cannot (volatile stores, results that feed the checksum).
 *
 *  (C) 2026 Bill Cavalieri
 */

#include "sb_kernels.h"
#include <math.h>
#include <string.h>

static uint32_t lcg(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s;
}

static uint32_t fold64(double d)
{
    uint64_t u;
    memcpy(&u, &d, 8);
    return (uint32_t)u ^ (uint32_t)(u >> 32);
}

static uint32_t fbits(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

/* Words summed every 64 bytes: enough to catch a copy that went wrong, cheap next to the copy itself. */
static uint32_t sample_sum(const unsigned char *p, uint32_t bytes)
{
    uint32_t s = 0, i, w;
    for (i = 0; i + 4 <= bytes; i += 64) {
        w = ((uint32_t)p[i] << 24) | ((uint32_t)p[i + 1] << 16) | ((uint32_t)p[i + 2] << 8) | p[i + 3];
        s = (s << 1 | s >> 31) ^ w;
    }
    if (bytes >= 4) {
        i = bytes - 4;
        w = ((uint32_t)p[i] << 24) | ((uint32_t)p[i + 1] << 16) | ((uint32_t)p[i + 2] << 8) | p[i + 3];
        s ^= w;
    }
    return s;
}

static void fill_pattern(unsigned char *p, uint32_t bytes, uint32_t seed)
{
    uint32_t i;
    for (i = 0; i < bytes; i++)
        p[i] = (unsigned char)(lcg(&seed) >> 24);
}

__attribute__((noinline)) static uint32_t leaf(uint32_t x)
{
    __asm__ volatile("" : "+r"(x));     /* keeps the call: the compiler cannot see through it */
    return x + 1;
}

static uint32_t (*volatile leaf_ptr)(uint32_t) = leaf;

/* 16-byte aligned start inside a scratch block (the guest's NewPtr gives 8). */
static unsigned char *align16(void *p)
{
    return (unsigned char *)(((uintptr_t)p + 15) & ~(uintptr_t)15);
}

static void atomic_inc(volatile uint32_t *p)
{
#if defined(__powerpc__) || defined(__ppc__)
    uint32_t t;
    __asm__ volatile("1: lwarx %0,0,%1\n\taddi %0,%0,1\n\tstwcx. %0,0,%1\n\tbne- 1b" : "=&b"(t) : "r"(p) : "cc", "memory");
#else
    __atomic_fetch_add(p, 1, __ATOMIC_SEQ_CST);
#endif
}

void sb_vec_arrays(void *scratch, uint32_t param, float **x, float **y)
{
    *x = (float *)align16(scratch);
    *y = *x + param;
}

uint32_t sb_vec_checksum(void *scratch, uint32_t param)
{
    float *x, *y;
    uint32_t i, sum = 0;
    sb_vec_arrays(scratch, param, &x, &y);
    for (i = 0; i < param; i++)
        sum = (sum << 1 | sum >> 31) ^ fbits(y[i]);
    return sum;
}

int sb_known(unsigned k)
{
    return k >= SB_K_INT && k < SB_K_COUNT;
}

size_t sb_scratch_bytes(unsigned k, uint32_t param)
{
    switch (k) {
    case SB_K_BYTECOPY: case SB_K_COPY: case SB_K_WORDCOPY:
        return (size_t)param * 2 + 16;
    case SB_K_FILL:
        return (size_t)param + 16;
    case SB_K_COMPARE:
        return (size_t)param * 2 + 256;
    case SB_K_ATOMIC:
        return 16;
    case SB_K_FPD_THRU:
        return (size_t)param * 16;
    case SB_K_VEC:
        return (size_t)param * 8 + 16;
    case SB_K_CHASE:
        return (size_t)param * 4;
    default:
        return 0;
    }
}

void sb_prepare(unsigned k, uint32_t param, void *scratch)
{
    uint32_t seed = 12345, i;
    unsigned char *a = align16(scratch);
    switch (k) {
    case SB_K_BYTECOPY: case SB_K_COPY: case SB_K_WORDCOPY:
        fill_pattern(a, param, 77);
        memset(a + param, 0, param);
        break;
    case SB_K_FILL:
        memset(a, 0, param);
        break;
    case SB_K_COMPARE: {
        int16_t *h = (int16_t *)scratch;
        signed char *t = (signed char *)scratch + (size_t)param * 2;
        for (i = 0; i < param; i++)
            h[i] = (int16_t)(lcg(&seed) >> 24);
        for (i = 0; i < 256; i++)
            t[i] = (signed char)(lcg(&seed) >> 29);
        break;
    }
    case SB_K_ATOMIC:
        memset(scratch, 0, 16);
        break;
    case SB_K_FPD_THRU: {
        double *x = (double *)scratch, *y = x + param;
        for (i = 0; i < param; i++) {
            x[i] = (double)(lcg(&seed) >> 16) / 65536.0;
            y[i] = (double)(lcg(&seed) >> 16) / 65536.0;
        }
        break;
    }
    case SB_K_VEC: {
        float *x = (float *)a, *y = x + param;
        for (i = 0; i < param; i++) {
            x[i] = (float)(lcg(&seed) >> 28);
            y[i] = 0.0f;
        }
        break;
    }
    case SB_K_CHASE: {
        uint32_t *next = (uint32_t *)scratch, j, t;
        for (i = 0; i < param; i++)
            next[i] = i;
        for (i = param - 1; i > 0; i--) {   /* Sattolo: one cycle through every entry */
            j = lcg(&seed) % i;
            t = next[i]; next[i] = next[j]; next[j] = t;
        }
        break;
    }
    default:
        break;
    }
}

uint32_t sb_run(unsigned k, uint32_t iters, uint32_t param, void *scratch)
{
    uint32_t it, i;
    unsigned char *a = align16(scratch);
    switch (k) {
    case SB_K_INT: {
        uint32_t x = 0x12345678u, y = 1;
        for (it = 0; it < iters; it++) {
            x = (x ^ (x << 5)) + y;
            y = y * 0x9E3779B1u + (x >> 7);
            x = (x << 3 | x >> 29) ^ (y & 0xFFFFu);
        }
        return x ^ y;
    }
    case SB_K_BYTECOPY: {
        const unsigned char *s = a;
        volatile unsigned char *d = a + param;
        for (it = 0; it < iters; it++)
            for (i = 0; i < param; i++)
                d[i] = s[i];
        return sample_sum(a + param, param);
    }
    case SB_K_COMPARE: {
        const int16_t *h = (const int16_t *)scratch;
        const signed char *t = (const signed char *)scratch + (size_t)param * 2;
        uint32_t n = 0;
        for (it = 0; it < iters; it++)
            for (i = 0; i < param; i++) {
                int16_t key = h[i];
                if (t[key & 255] == t[(i * 7) & 255])
                    n++;
                else
                    n ^= (uint32_t)key;
            }
        return n;
    }
    case SB_K_CALL: {
        uint32_t acc = 0;
        for (it = 0; it < iters; it++)
            acc = leaf(acc);
        return acc;
    }
    case SB_K_INDIRECT: {
        uint32_t acc = 0;
        for (it = 0; it < iters; it++)
            acc = leaf_ptr(acc);
        return acc;
    }
    case SB_K_ATOMIC: {
        volatile uint32_t *p = (volatile uint32_t *)align16(scratch);
        *p = 0;
        for (it = 0; it < iters; it++)
            atomic_inc(p);
        return *p;
    }
    case SB_K_FPD_CHAIN: {
        double x = 1.0;
        for (it = 0; it < iters; it++)
            x = fma(x, 0.999999, 0.001);
        return fold64(x);
    }
    case SB_K_FPD_THRU: {
        const double *x = (const double *)scratch, *y = x + param;
        double s0 = 0, s1 = 0, s2 = 0, s3 = 0;
        for (it = 0; it < iters; it++)
            for (i = 0; i + 4 <= param; i += 4) {
                s0 = fma(x[i], y[i], s0);
                s1 = fma(x[i + 1], y[i + 1], s1);
                s2 = fma(x[i + 2], y[i + 2], s2);
                s3 = fma(x[i + 3], y[i + 3], s3);
            }
        return fold64(s0) ^ fold64(s1) * 3u ^ fold64(s2) * 5u ^ fold64(s3) * 7u;
    }
    case SB_K_FPS_CHAIN: {
        float x = 1.0f;
        for (it = 0; it < iters; it++)
            x = fmaf(x, 0.9999f, 0.01f);
        return fbits(x);
    }
    case SB_K_VEC: {
        const float *x = (const float *)a;
        float *y = (float *)a + param;
        for (it = 0; it < iters; it++)
            for (i = 0; i < param; i++)
                y[i] = y[i] * 0.5f + x[i];      /* exact multiply, so fused or not gives the same result */
        return sb_vec_checksum(scratch, param);
    }
    case SB_K_COPY:
        for (it = 0; it < iters; it++)
            memcpy(a + param, a, param);
        return sample_sum(a + param, param);
    case SB_K_WORDCOPY: {
        const double *s = (const double *)a;
        volatile double *d = (volatile double *)(a + param);
        uint32_t n = param / 8;
        for (it = 0; it < iters; it++)
            for (i = 0; i + 2 <= n; i += 2) {
                double f0 = s[i], f1 = s[i + 1];
                d[i] = f0;
                d[i + 1] = f1;
            }
        return sample_sum(a + param, n * 8);
    }
    case SB_K_FILL:
        for (it = 0; it < iters; it++)
            memset(a, (int)(it & 0xFF) | 1, param);
        return sample_sum(a, param);
    case SB_K_CHASE: {
        const uint32_t *next = (const uint32_t *)scratch;
        uint32_t p = 0;
        for (it = 0; it < iters; it++)
            p = next[p];
        return p;
    }
    default:
        return 0xFFFFFFFFu;
    }
}
