/*
 *  sheepbench_host.h - Host side of SheepBench, the benchmark app in guest/sheepbench/.
 *
 *  The guest reaches these through the Sheep Shears mailbox (commands benchInfo, benchNative, benchSetJIT and
 *  benchResult, handled in SheepApp/Shears/BenchHost.swift). Everything here runs on the emulation thread while the
 *  guest waits for the reply.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

#ifndef SHEEPBENCH_HOST_H
#define SHEEPBENCH_HOST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The emulator's side of the run report: "key=value" lines (PVR, clocks, JIT and SheepForce state, one line per
 * drive). Returns the number of bytes written (no terminating zero counted). */
int SheepBenchInfoText(char *out, int cap);

/* Runs kernel `kernel` (sb_kernels.h) natively: `iters` iterations for the checksum, then enough repetitions to
 * time it (median of five, at most about 50 ms in all). Returns 0, or -1 for an unknown kernel or no memory. */
int SheepBenchNative(uint32_t kernel, uint32_t iters, uint32_t param, uint64_t *ps_per_iter, uint32_t *checksum);

/* Asks for the PowerPC JIT, the host 68k JIT and SheepForce QuickDraw to be on (1) or off (0); `restore` puts back
 * what the emulator started with and ignores the rest. The switch happens at the next safe point (between
 * blocks), not before this returns. Returns 0, 1 when the emulator started in a mode that cannot be switched
 * (verify, fallback), 2 when "on" is asked for something that was off at start-up (only what started on can be
 * switched off and back on). */
int SheepBenchRequestModes(int ppc, int m68k, int qd, int restore);

/* Called on the emulation thread between blocks (from HandleInterrupt): applies a pending request. */
void SheepBenchApplyPending(void);

/* 1 while the emulator runs with modes other than those it started with (for the VM's status). */
int SheepBenchOverrideActive(void);

/* QuickDraw accelerator hooks that took a call, by hook code (0 bitblt, 2 fillrect, 3 fillmask, 5 lines); counted in
 * the native-op dispatch and reported by benchInfo as qd_hits, so a test can tell whether it reached them. */
extern uint32_t sheepbench_hook_hits[8];

#ifdef __cplusplus
}
#endif

#endif
