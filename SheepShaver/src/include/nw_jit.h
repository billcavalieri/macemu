/*
 *  nw_jit.h - New World ARM64 JIT (WP3): integer subset + equivalence oracle
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef NW_JIT_H
#define NW_JIT_H

#include <stdint.h>
#include <stddef.h>

/*
 * Translation cache key: (phys_page, guest_pc, msr_ir, endian).
 * endian 0 = guest big-endian (the only mode 4a/4b emits).
 * The C interpreter is the oracle; the ARM64 emitter must match it
 * on GPR/CR/XER/LR/CTR/PC/DEC. dyngen is not used.
 *
 * 4b: powerpc_cpu::execute consults the cache. NW_JIT_FALLBACK never
 * runs compiled code. NW_JIT_VERIFY compiles the same N-insn block the
 * live path would, runs it on a shadow CPU, then kpx interprets; the
 * guest follows kpx. NW_JIT_ON copy-out commits the shadow; mtspr DEC
 * goes through the same 0→1 / tb_base path as kpx mtspr_oea.
 */
enum { NW_JIT_MAX_BLOCK = 16 };

struct nw_jit_cpu {
	uint32_t gpr[32];
	uint32_t cr;
	uint32_t xer;
	uint32_t lr;
	uint32_t ctr;
	uint32_t pc;
	uint32_t dec;
	uint32_t msr;
	uint32_t fault;
	uint32_t dec_wr;	/* 1 if this block executed mtspr DEC */
	uint8_t *mem;
	uint32_t mem_base;
	uint32_t mem_size;
	void *host;
	int nstore;
	uint32_t store_ea[NW_JIT_MAX_BLOCK];
	uint32_t store_val[NW_JIT_MAX_BLOCK];
};

typedef void (*nw_jit_fn)(struct nw_jit_cpu *cpu);

/* Cache miss is NULL. A stored "run the interpreter" sentinel is this
 * pointer; it is never called. */
#define NW_JIT_INTERPRET ((nw_jit_fn)(uintptr_t)1)

enum {
	NW_JIT_OFF = 0,
	NW_JIT_FALLBACK = 1,
	NW_JIT_ON = 2,
	NW_JIT_VERIFY = 3
};

void nw_jit_reset(void);

int nw_jit_mode(void);
void nw_jit_set_mode(int mode);
const char *nw_jit_mode_name(void);

int nw_jit_op_supported(uint32_t op);
/* Live path: supported ops whose EA is a writable/readable bank (NONE/IO
 * mem ops stay on kpx — 4b-2 fill at 50310490). */
int nw_jit_op_dispatch(uint32_t op);
int nw_jit_op_ends_block(uint32_t op);

void nw_jit_verify_note(const uint32_t *ops, int n, int miss);
void nw_jit_verify_fail(void);
void nw_jit_verify_skip(int mem);
void nw_jit_verify_uncompared(int fault);	/* 1 = DSI probe, 2 = I/O skip */
void nw_jit_verify_dump(const char *why);
void nw_jit_pc_hot(uint32_t pc, uint32_t op);
void nw_jit_pc_hot_dump(const char *why);

typedef uint32_t (*nw_jit_host_lwz)(void *host, uint32_t ea, uint32_t pc, int *fault);
typedef void (*nw_jit_host_stw)(void *host, uint32_t ea, uint32_t val, uint32_t pc, int *fault);
typedef uint32_t (*nw_jit_host_lh)(void *host, uint32_t ea, uint32_t pc, int *fault);
typedef void (*nw_jit_host_sth16)(void *host, uint32_t ea, uint32_t val, uint32_t pc, int *fault);
void nw_jit_set_host_mem(nw_jit_host_lwz lwz, nw_jit_host_stw stw);
void nw_jit_set_host_half(nw_jit_host_lh lh, nw_jit_host_sth16 sth);

nw_jit_fn nw_jit_cache_get(uint32_t phys_page, uint32_t guest_pc,
			  uint32_t msr_ir, uint32_t endian, int *n_out);
void nw_jit_cache_put(uint32_t phys_page, uint32_t guest_pc, uint32_t msr_ir,
		      uint32_t endian, nw_jit_fn fn, int n);

uint64_t nw_jit_exec_blocks(void);
uint64_t nw_jit_exec_insns(void);
void nw_jit_note_exec(int n);

/* C oracle: execute one opcode at cpu->pc. 0 = pc advanced, 1 = block
 * ended (b/blr), -1 = not in the 4a subset. */
int nw_jit_interp_one(struct nw_jit_cpu *cpu, uint32_t opcode);
/* Run n opcodes or until a terminator. Returns the last interp_one code. */
int nw_jit_interp_n(struct nw_jit_cpu *cpu, const uint32_t *ops, int n, uint32_t start_pc);

/* Compile ops[0..n) as a block at guest_pc. phys_page is guest_pc & ~0xfff
 * unless the caller has a translated page. NULL if an opcode is unsupported
 * or the host cannot emit. */
nw_jit_fn nw_jit_compile(const uint32_t *ops, int n, uint32_t guest_pc,
			uint32_t phys_page, uint32_t msr_ir, uint32_t endian);

void nw_jit_invalidate_page(uint32_t phys_page);
uint64_t nw_jit_flush_count(void);

/* PPC instruction constructors for the harness. */
uint32_t nw_ppc_addi(int rd, int ra, int simm);
uint32_t nw_ppc_add(int rd, int ra, int rb, int rc);
uint32_t nw_ppc_rlwinm(int ra, int rs, int sh, int mb, int me);
uint32_t nw_ppc_rlwimi(int ra, int rs, int sh, int mb, int me);
uint32_t nw_ppc_lwz(int rd, int ra, int d);
uint32_t nw_ppc_stw(int rs, int ra, int d);
uint32_t nw_ppc_lha(int rd, int ra, int d);
uint32_t nw_ppc_sth(int rs, int ra, int d);
uint32_t nw_ppc_bclr(int bo, int bi);
uint32_t nw_ppc_cmp(int ra, int rb);
uint32_t nw_ppc_cmpi(int ra, int simm);
uint32_t nw_ppc_b(int disp, int lk);
uint32_t nw_ppc_bc(int bo, int bi, int disp);
uint32_t nw_ppc_blr(void);
uint32_t nw_ppc_mfspr(int rd, int spr);
uint32_t nw_ppc_mtspr(int spr, int rs);

enum {
	NW_PPC_SPR_XER = 1,
	NW_PPC_SPR_LR = 8,
	NW_PPC_SPR_CTR = 9,
	NW_PPC_SPR_DEC = 22,
	NW_PPC_BO_TRUE = 12,
	NW_PPC_BO_FALSE = 4
};

#endif
