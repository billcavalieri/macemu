/*
 *  nw_68k_jit.h - opt-in host 68k histogram and fallback-only JIT
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 *
 *  Prefs key jit68k_host (default false) selects the skeleton.
 *  NW_JIT68K_HIST=1 writes ~/Library/Logs/SheepShaver/jit68k-hist.txt
 *  (NW_JIT68K_HIST_PATH overrides that path).
 *  jit68k remains the Apple DR flag and stays compiled out.
 */

#ifndef NW_68K_JIT_H
#define NW_68K_JIT_H

#include <stdint.h>

class powerpc_cpu;

/* Create dir and any missing parents. EEXIST is success. */
void nw_log_mkdir(const char *dir);

/* One branch when the env is off. Records opcode-table slots only. */
void nw_68k_hist_note(uint32_t ppc_pc, uint32_t pc68);

/* Armed by the fallback. Returns 1 when PPC pc re-enters the opcode
 * table after leaving it, which is the next 68k insn. Nested execute
 * depths are ignored. */
int nw_68k_stop_one(uint32_t ppc_pc, int depth);
void nw_68k_arm_one(void);
int nw_68k_took_one(void);

/* Runs the 68k routine already set up in NK GPRs. Every insn falls
 * back to one NanoKernel handler. entry is the first opcode-table slot. */
void nw_68k_jit_execute(powerpc_cpu *cpu, uint32_t entry);

/* NK handlers run by nw_68k_jit_execute. Zero when the pref is off. */
uint64_t nw_68k_fallback_count(void);

/* Top 68k opcodes counted at the fallback entry. Debug summary line. */
void nw_68k_op_summary(void);

#endif
