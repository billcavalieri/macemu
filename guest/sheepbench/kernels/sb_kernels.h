/*
 *  sb_kernels.h - SheepBench measurement kernels, shared by the guest app (PowerPC, Retro68) and the host
 *  (SheepShaver's benchNative command, ARM64), so "percent of native" compares the same C code.
 *
 *  Each kernel returns a checksum that depends only on its inputs, never on the machine (no byte order, no
 *  pointer values, floating point only through fma() or exact operations). The guest compares its checksum with
 *  the host's: a JIT bug fails the test instead of looking fast.
 *
 *  (C) 2026 Bill Cavalieri
 */

#ifndef SB_KERNELS_H
#define SB_KERNELS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Kernel numbers are part of the guest/host protocol (benchNative): append only. */
enum {
    SB_K_INT = 1,           /* integer mix (add, xor, shift, rotate, multiply); one iteration = one round */
    SB_K_BYTECOPY = 2,      /* byte-at-a-time copy of `param` bytes; one iteration = one pass */
    SB_K_COMPARE = 3,       /* MacBench-integer-style compare loop over a `param`-entry table; one iteration = one pass */
    SB_K_CALL = 4,          /* direct call of a small function; one iteration = one call */
    SB_K_INDIRECT = 5,      /* call through a function pointer; one iteration = one call */
    SB_K_ATOMIC = 6,        /* atomic increment (lwarx/stwcx. on the guest); one iteration = one increment */
    SB_K_FPD_CHAIN = 7,     /* dependent double fma chain; one iteration = one fma */
    SB_K_FPD_THRU = 8,      /* independent double fmas over two `param`-entry arrays; one iteration = one pass */
    SB_K_FPS_CHAIN = 9,     /* dependent single fma chain; one iteration = one fma */
    SB_K_VEC = 10,          /* y = y * 0.5 + x over `param` floats (AltiVec vmaddfp on the guest); one iteration = one pass */
    SB_K_COPY = 11,         /* memcpy of `param` bytes; one iteration = one pass */
    SB_K_FILL = 12,         /* memset of `param` bytes; one iteration = one pass */
    SB_K_CHASE = 13,        /* dependent loads around a shuffled ring of `param` entries; one iteration = one load */
    SB_K_WORDCOPY = 14,     /* copy of `param` bytes by 8-byte words (a hand-written BlockMove); one iteration = one pass */
    SB_K_COUNT
};

/* Bytes of scratch memory the kernel needs (0 when none). */
size_t sb_scratch_bytes(unsigned kernel, uint32_t param);
/* Fills the scratch memory (not timed). */
void sb_prepare(unsigned kernel, uint32_t param, void *scratch);
/* Runs `iters` iterations; returns the checksum. 0xFFFFFFFF for an unknown kernel. */
uint32_t sb_run(unsigned kernel, uint32_t iters, uint32_t param, void *scratch);
/* Whether the kernel number is known. */
int sb_known(unsigned kernel);
/* For another implementation of SB_K_VEC (AltiVec): its two arrays inside the scratch block (16-byte aligned), and
 * the checksum sb_run would return after the same passes. */
void sb_vec_arrays(void *scratch, uint32_t param, float **x, float **y);
uint32_t sb_vec_checksum(void *scratch, uint32_t param);

#ifdef __cplusplus
}
#endif

#endif
