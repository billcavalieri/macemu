/*
 *  sb_cases.h - The kernel runs SheepBench checks: kernel, iterations and parameter. tools/sheepbench/build.sh runs
 *  every case natively at build time (gen_expected.c) and writes the checksums into sb_expected.h, so the guest can
 *  check its results even without a host that answers benchNative.
 *
 *  (C) 2026 Bill Cavalieri
 */

#ifndef SB_CASES_H
#define SB_CASES_H

#include "sb_kernels.h"

typedef struct {
    uint16_t kernel;
    uint32_t iters;     /* for the checksum run */
    uint32_t param;
} SbCase;

#define SB_KB 1024u
#define SB_MB (1024u * 1024u)

static const SbCase kSbCases[] = {
    { SB_K_INT, 100000, 0 },
    { SB_K_BYTECOPY, 4, 4096 },
    { SB_K_COMPARE, 4, 2048 },
    { SB_K_CALL, 100000, 0 },
    { SB_K_INDIRECT, 100000, 0 },
    { SB_K_ATOMIC, 100000, 0 },
    { SB_K_FPD_CHAIN, 100000, 0 },
    { SB_K_FPD_THRU, 4, 1024 },
    { SB_K_FPS_CHAIN, 100000, 0 },
    { SB_K_VEC, 16, 1024 },
    { SB_K_COPY, 1, 4 * SB_KB }, { SB_K_COPY, 1, 64 * SB_KB }, { SB_K_COPY, 1, SB_MB }, { SB_K_COPY, 1, 16 * SB_MB },
    { SB_K_WORDCOPY, 1, 4 * SB_KB }, { SB_K_WORDCOPY, 1, 64 * SB_KB }, { SB_K_WORDCOPY, 1, SB_MB }, { SB_K_WORDCOPY, 1, 16 * SB_MB },
    { SB_K_FILL, 3, 4 * SB_KB }, { SB_K_FILL, 3, 64 * SB_KB }, { SB_K_FILL, 3, SB_MB }, { SB_K_FILL, 3, 16 * SB_MB },
    { SB_K_CHASE, 100000, 1 * SB_KB }, { SB_K_CHASE, 100000, 16 * SB_KB }, { SB_K_CHASE, 100000, 256 * SB_KB },
    { SB_K_CHASE, 100000, 4 * SB_MB },
};

#define SB_NCASES ((int)(sizeof kSbCases / sizeof kSbCases[0]))

#endif
