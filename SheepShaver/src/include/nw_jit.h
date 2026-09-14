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
 * goes through the same 0→1 / tb_base path as kpx mtspr_oea. 4c: the
 * cache is keyed by phys_page, so mtsr/BAT/SDR1/tlbie do not flush-all
 * (a remap misses). icbi, compiled stw/sth, interpreter pa_write, and
 * host writes into guest RAM drop that page. 4d: ON DSI from a helper
 * takes the exception with SRR0 = the faulting PC (same as kpx).
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
	uint32_t fault_ea;
	uint32_t fault_st;	/* 1 if the faulting access was a store */
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
/* Branches (bc/b/bclr). Stores do not end the block; a store into the
 * executing page sets fault SMC and the ON path commits pc = store+4. */
int nw_jit_op_ends_block(uint32_t op);

enum {
	NW_JIT_FAULT_DSI = 1,
	NW_JIT_FAULT_IO = 2,
	NW_JIT_FAULT_SMC = 3	/* store into the executing code page */
};

void nw_jit_verify_note(const uint32_t *ops, int n, int miss);
void nw_jit_verify_fail(void);
void nw_jit_verify_skip(int mem);
void nw_jit_note_skip_unsup(uint32_t op);
void nw_jit_verify_uncompared(int fault);	/* 1 = DSI probe, 2 = I/O skip */
void nw_jit_verify_dump(const char *why);
void nw_jit_pc_hot(uint32_t pc, uint32_t op);
void nw_jit_pc_hot_dump(const char *why);

typedef uint32_t (*nw_jit_host_lwz)(void *host, uint32_t ea, uint32_t pc, int *fault);
typedef void (*nw_jit_host_stw)(void *host, uint32_t ea, uint32_t val, uint32_t pc, int *fault);
typedef uint32_t (*nw_jit_host_lh)(void *host, uint32_t ea, uint32_t pc, int *fault);
typedef void (*nw_jit_host_sth16)(void *host, uint32_t ea, uint32_t val, uint32_t pc, int *fault);
typedef uint32_t (*nw_jit_host_lb)(void *host, uint32_t ea, uint32_t pc, int *fault);
typedef void (*nw_jit_host_stb8)(void *host, uint32_t ea, uint32_t val, uint32_t pc, int *fault);
void nw_jit_set_host_mem(nw_jit_host_lwz lwz, nw_jit_host_stw stw);
void nw_jit_set_host_half(nw_jit_host_lh lh, nw_jit_host_sth16 sth);
void nw_jit_set_host_byte(nw_jit_host_lb lb, nw_jit_host_stb8 stb);

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

/* Flush attribution for jit stats. */
enum {
	NW_JIT_FL_STORE = 0,	/* compiled stw/sth into a page with blocks */
	NW_JIT_FL_ICBI,		/* icbi range */
	NW_JIT_FL_TLB,		/* tlbie/tlbia -> invalidate_cache */
	NW_JIT_FL_SR,		/* mtsr/mtsrin */
	NW_JIT_FL_BAT,		/* mtibat/mtdbat */
	NW_JIT_FL_SDR1,		/* mtsdr1 */
	NW_JIT_FL_WRAP,		/* code buffer wrap */
	NW_JIT_FL_ISTORE,	/* interpreter pa_write into a code page */
	NW_JIT_FL_HOST,		/* Host2Mac / WriteMacInt into guest RAM */
	NW_JIT_FL_OTHER,
	NW_JIT_FL_N
};
void nw_jit_invalidate_page(uint32_t phys_page);
void nw_jit_invalidate_page_src(uint32_t phys_page, int src);
void nw_jit_invalidate_range_src(uint32_t pa, uint32_t nbytes, int src);
void nw_jit_invalidate_all(void);
void nw_jit_invalidate_all_src(int src);

/*
 * JIT data TLB: same EA→PA map as ppc32_mmu::translate (filled only after
 * a successful probe). Direct-mapped, 256 entries. Not a second translator.
 * Hit is inlined; miss calls the C helper, which walks and fills.
 * Flush on tlbie/tlbia/mtsr/BAT/SDR1. MSR[DR] off skips the cache.
 */
enum { NW_JIT_DTLB_N = 256 };
enum {
	NW_JIT_DTLB_VALID = 1u,
	NW_JIT_DTLB_WRITE = 2u,
	NW_JIT_DTLB_HOST = 4u	/* host page pointer is live; ARM ldr/str */
};
struct nw_jit_dtlb_ent {
	uint32_t ea_page;
	uint32_t pa_page;
	uint32_t flags;
	uint32_t pad;
	uint64_t host;		/* host pointer to the page, 0 if not inlineable */
	uint64_t pad2;		/* 32-byte entry, index << 5 */
};
void nw_jit_dtlb_flush(void);
void nw_jit_dtlb_fill(uint32_t ea, uint32_t pa, int writable, uint64_t host);
int nw_jit_dtlb_lookup(uint32_t ea, int is_store, uint32_t *pa);
uint64_t nw_jit_dtlb_hits(void);
uint64_t nw_jit_dtlb_misses(void);

typedef uint32_t (*nw_jit_host_lwz_pa)(void *host, uint32_t pa, uint32_t pc, int *fault);
typedef void (*nw_jit_host_stw_pa)(void *host, uint32_t pa, uint32_t val, uint32_t pc, int *fault);
void nw_jit_set_host_pa(nw_jit_host_lwz_pa lwz, nw_jit_host_stw_pa stw);
typedef uint32_t (*nw_jit_host_mfspr)(void *host, uint32_t spr);
void nw_jit_set_host_mfspr(nw_jit_host_mfspr fn);
uint64_t nw_jit_flush_count(void);
uint64_t nw_jit_compile_count(void);
void nw_jit_stats_print(const char *why);

/* PPC instruction constructors for the harness. */
uint32_t nw_ppc_addi(int rd, int ra, int simm);
uint32_t nw_ppc_mulli(int rd, int ra, int simm);
uint32_t nw_ppc_addic(int rd, int ra, int simm, int rc);
uint32_t nw_ppc_add(int rd, int ra, int rb, int rc);
uint32_t nw_ppc_addc(int rd, int ra, int rb, int rc);
uint32_t nw_ppc_addco(int rd, int ra, int rb, int rc);
uint32_t nw_ppc_subfe(int rd, int ra, int rb, int rc);
uint32_t nw_ppc_rlwinm(int ra, int rs, int sh, int mb, int me);
uint32_t nw_ppc_rlwimi(int ra, int rs, int sh, int mb, int me);
uint32_t nw_ppc_lwz(int rd, int ra, int d);
uint32_t nw_ppc_lwzu(int rd, int ra, int d);
uint32_t nw_ppc_lbz(int rd, int ra, int d);
uint32_t nw_ppc_lbzx(int rd, int ra, int rb);
uint32_t nw_ppc_stb(int rs, int ra, int d);
uint32_t nw_ppc_stw(int rs, int ra, int d);
uint32_t nw_ppc_stwu(int rs, int ra, int d);
uint32_t nw_ppc_stwx(int rs, int ra, int rb);
uint32_t nw_ppc_lwzx(int rd, int ra, int rb);
uint32_t nw_ppc_lhax(int rd, int ra, int rb);
uint32_t nw_ppc_lhaux(int rd, int ra, int rb);
uint32_t nw_ppc_lha(int rd, int ra, int d);
uint32_t nw_ppc_sth(int rs, int ra, int d);
uint32_t nw_ppc_bclr(int bo, int bi);
uint32_t nw_ppc_bcctr(int bo, int bi);
uint32_t nw_ppc_or(int ra, int rs, int rb);
uint32_t nw_ppc_ori(int ra, int rs, unsigned uimm);
uint32_t nw_ppc_cmp(int ra, int rb);
uint32_t nw_ppc_cmpi(int ra, int simm);
uint32_t nw_ppc_cmpi_cr(int crfd, int ra, int simm);
uint32_t nw_ppc_andi_dot(int ra, int rs, unsigned uimm);
uint32_t nw_ppc_subfco(int rd, int ra, int rb, int rc);
uint32_t nw_ppc_cmpli(int crfd, int ra, unsigned uimm);
uint32_t nw_ppc_cmpl(int crfd, int ra, int rb);
uint32_t nw_ppc_mtcrf(int crm, int rs);
uint32_t nw_ppc_extsh(int ra, int rs, int rc);
uint32_t nw_ppc_extsb(int ra, int rs, int rc);
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
	NW_PPC_SPR_VRSAVE = 256,
	NW_PPC_SPR_TBL = 268,
	NW_PPC_SPR_TBU = 269,
	NW_PPC_SPR_SPRG0 = 272,
	NW_PPC_SPR_SPRG1 = 273,
	NW_PPC_SPR_SPRG2 = 274,
	NW_PPC_SPR_SPRG3 = 275,
	NW_PPC_SPR_PVR = 287,
	NW_PPC_BO_TRUE = 12,
	NW_PPC_BO_FALSE = 4
};

#endif
