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
 * msr_ir is a packed IR|DR<<1|PR<<2 mask (not EE/FP/VEC).
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
enum { NW_JIT_MAX_BLOCK = 32 };

struct nw_jit_cpu {
	uint32_t gpr[32];
	uint32_t vr[32][4];	/* AltiVec, big-endian word order */
	uint64_t fpr[32];	/* IEEE754 bits, PowerPC dw order */
	uint32_t cr;
	uint32_t xer;
	uint32_t fpscr;
	uint32_t vscr;
	uint32_t sr[16];	/* harness / no-host mtsrin path */
	uint32_t lr;
	uint32_t ctr;
	uint32_t pc;
	uint32_t dec;
	uint32_t msr;
	uint32_t fault;
	uint32_t fault_ea;
	uint32_t fault_st;	/* 1 if the faulting access was a store */
	uint32_t dec_wr;	/* 1 if this block executed mtspr DEC */
	uint32_t reserve_valid;
	uint32_t reserve_ea;
	uint8_t *mem;
	uint32_t mem_base;
	uint32_t mem_size;
	void *host;
	int nstore;
	uint32_t store_ea[NW_JIT_MAX_BLOCK];
	uint32_t store_val[NW_JIT_MAX_BLOCK];
	/* Filled by nw_jit_cpu_bind; compiled code loads these via x19. */
	void *jit_dtlb;
	uint64_t *jit_dtlb_hit;
	void *jit_lwz;
	void *jit_stw;
	void *jit_lwz_pa;
	void *jit_stw_pa;
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
void nw_jit_cpu_bind(struct nw_jit_cpu *c);

int nw_jit_mode(void);
void nw_jit_set_mode(int mode);
const char *nw_jit_mode_name(void);

int nw_jit_op_supported(uint32_t op);
/* Live path: supported ops whose EA is a writable/readable bank (NONE/IO
 * mem ops stay on kpx — 4b-2 fill at 50310490). */
int nw_jit_op_dispatch(uint32_t op);
/* Branches (bc/b/bclr) and isync (flush pending icbi, then leave the
 * block). Stores do not end the block; a store into the executing page
 * sets fault SMC and the ON path commits pc = store+4. */
int nw_jit_op_ends_block(uint32_t op);
/* GPRs the opcode reads or writes. Unrecognized ops return ~0u. */
uint32_t nw_jit_op_gpr_mask(uint32_t op);

enum {
	NW_JIT_FAULT_DSI = 1,
	NW_JIT_FAULT_IO = 2,
	NW_JIT_FAULT_SMC = 3,	/* store into the executing code page */
	NW_JIT_FAULT_EXC = 4	/* helper already took a program exception */
};

void nw_jit_verify_note(const uint32_t *ops, int n, int miss);
void nw_jit_verify_fail(void);
void nw_jit_verify_skip(int mem);
void nw_jit_note_skip_unsup(uint32_t op, unsigned packed, uint32_t pc = 0);
uint32_t nw_jit_skip_op(uint32_t op);
uint32_t nw_jit_skip_pc(uint32_t op);
/* One-shot raw skip word + pc for unnamed prim-4 buckets and prim 6. */
void nw_jit_skip_raw_once(uint32_t op, uint32_t pc);
int nw_jit_skip_raw_last(uint32_t *op, uint32_t *pc, char *name, size_t n);
void nw_jit_verify_uncompared(int fault);	/* 1 = DSI probe, 2 = I/O skip */
void nw_jit_note_skip_io(uint32_t ea, uint32_t pc);
void nw_jit_verify_dump(const char *why);
void nw_jit_pc_hot(uint32_t pc, uint32_t op);
void nw_jit_pc_hot_dump(const char *why);
void nw_jit_summary_print(const char *why);
uint64_t nw_jit_skip_n(uint32_t op);
uint64_t nw_jit_skip_lost(uint32_t op);
uint64_t nw_jit_codec_insns(void);
uint64_t nw_jit_other_insns(void);

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
			  uint32_t msr_ir, uint32_t endian, int *n_out,
			  int *uses_fpr = 0, int *uses_vr = 0,
			  uint32_t *chain_pc = 0, uint32_t *gpr_mask = 0,
			  int16_t *chain_disp = 0);
void nw_jit_cache_put(uint32_t phys_page, uint32_t guest_pc, uint32_t msr_ir,
		      uint32_t endian, nw_jit_fn fn, int n,
		      uint32_t first_opcode = 0, int uses_fpr = 0, int uses_vr = 0,
		      uint32_t chain_pc = 0, uint32_t gpr_mask = 0xffffffffu,
		      int16_t chain_disp = 0);
uint64_t nw_jit_chain_hops(void);
void nw_jit_note_chain(int hops);
void nw_jit_tail_begin(void);
int nw_jit_tail_n(void);
void nw_jit_tail_dsi(uint32_t *pc, int *n);
void nw_jit_tail_class(int *fpr, int *vr);

uint64_t nw_jit_exec_blocks(void);
uint64_t nw_jit_exec_insns(void);
void nw_jit_note_exec(int n);
void nw_jit_note_exec_at(int n, uint32_t pc, int uses_vr = 0);
void nw_jit_note_kcall_fast(void);
uint64_t nw_jit_kcall_fast(void);
uint64_t nw_jit_bat_total(void);
uint64_t nw_jit_bat_gen_bumps(void);

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

enum {
	NW_JIT_CUT_ENDS_BLOCK = 0,
	NW_JIT_CUT_PAGE_CROSS,
	NW_JIT_CUT_PEEK_FAIL,
	NW_JIT_CUT_CLASS_CHANGE,
	NW_JIT_CUT_UNSUP_NEXT,
	NW_JIT_CUT_MEM_OK0,
	NW_JIT_CUT_MEM_OK2,
	NW_JIT_CUT_MAX_BLOCK,
	NW_JIT_CUT_FIRST_OP_IO,
	NW_JIT_CUT_N
};
enum {
	NW_JIT_HOP_CAP = 0,
	NW_JIT_HOP_NO_CHAIN_PC,
	NW_JIT_HOP_PC_MISMATCH,
	NW_JIT_HOP_ITLB_MISS,
	NW_JIT_HOP_ALINE,
	NW_JIT_HOP_CACHE_MISS,
	NW_JIT_HOP_VEC_GATE,
	NW_JIT_HOP_FP_GATE,
	NW_JIT_HOP_COMPILE_NULL,
	NW_JIT_HOP_N
};
void nw_jit_note_cut(int reason);
void nw_jit_note_hop_stop(int reason);
uint64_t nw_jit_cut_count(int reason);
uint64_t nw_jit_hop_stop_count(int reason);
void nw_jit_invalidate_page(uint32_t phys_page);
void nw_jit_invalidate_page_src(uint32_t phys_page, int src);
void nw_jit_invalidate_range_src(uint32_t pa, uint32_t nbytes, int src);
void nw_jit_invalidate_all(void);
void nw_jit_invalidate_all_src(int src);
void nw_jit_set_code_pages(uint32_t ram_base, uint32_t ram_size,
			   uint32_t rom_base, uint32_t rom_size);
int nw_jit_stats_wanted(void);

/*
 * JIT data TLB: same EA→PA map as ppc32_mmu::translate (filled only after
 * a successful probe). 1024 sets, 2 ways. Index is still (ea>>12)&1023;
 * the second way holds the page the 68k emulator aliases onto that set.
 * Not a second translator.
 * Hit is inlined; miss calls the C helper, which walks and fills.
 * Flush on tlbia/SDR1; mtsr drops that SR's entries; DBAT drops its
 * EA range; tlbie drops one page. Entries are tagged with MSR[PR]; a
 * privilege change is a miss, not a flush. IBAT does not touch the DTLB.
 */
enum { NW_JIT_DTLB_N = 1024 };
enum {
	NW_JIT_DTLB_FL_MTMSR = 0,
	NW_JIT_DTLB_FL_RFI,
	NW_JIT_DTLB_FL_MTSR,
	NW_JIT_DTLB_FL_TLB,
	NW_JIT_DTLB_FL_BAT,
	NW_JIT_DTLB_FL_SDR1,
	NW_JIT_DTLB_FL_RESET,
	NW_JIT_DTLB_FL_OTHER,
	NW_JIT_DTLB_FL_N
};
enum {
	NW_JIT_DTLB_VALID = 1u,
	NW_JIT_DTLB_WRITE = 2u,
	NW_JIT_DTLB_HOST = 4u,	/* host page pointer is live; ARM ldr/str */
	NW_JIT_DTLB_PR = 8u,	/* filled with MSR[PR]=1; miss if current PR differs */
	NW_JIT_DTLB_BAT = 16u	/* filled from a BAT; miss if bat_gen changed */
};
struct nw_jit_dtlb_ent {
	uint32_t ea_page;
	uint32_t pa_page;
	uint32_t flags;
	uint32_t sr_gen;	/* SR generation at fill; miss if VSID/Ks/Kp changed */
	uint64_t host;		/* host pointer to the page, 0 if not inlineable */
	uint32_t bat_gen;	/* DBAT generation at fill; miss if any DBAT changed */
	uint32_t pad2;
};
void nw_jit_dtlb_flush(void);
void nw_jit_dtlb_flush_src(int src);
void nw_jit_dtlb_flush_if_pr(uint32_t old_msr, uint32_t new_msr, int src);
void nw_jit_dtlb_drop_sr(unsigned sr, int src);
void nw_jit_dtlb_drop_bat(uint32_t upper, int src);
void nw_jit_dtlb_drop_page(uint32_t ea, int src);
void nw_jit_dtlb_fill(uint32_t ea, uint32_t pa, int writable, uint64_t host, int pr = 0,
		     int via_bat = 0);
int nw_jit_dtlb_lookup(uint32_t ea, int is_store, uint32_t *pa);
int nw_jit_dtlb_lookup_pr(uint32_t ea, int is_store, uint32_t *pa, int pr);
uint64_t nw_jit_dtlb_hits(void);
uint64_t nw_jit_dtlb_misses(void);
uint64_t nw_jit_mtsr_total(void);
uint64_t nw_jit_mtsr_vsid(void);
void nw_jit_mtsr_note(unsigned sr, uint32_t old_val, uint32_t new_val);
int nw_jit_cache_slots(void);

/* Fetch ITLB: direct-mapped page-tag + PA. Not a second translator.
 * guest_fetch hits here before ppc32_mmu::translate. Drop on tlbie,
 * SR VSID/Ks/Kp, IBAT, and IR change. Size is not the JIT code cache. */
enum { NW_JIT_ITLB_N = 256 };
void nw_jit_itlb_flush(void);
void nw_jit_itlb_fill(uint32_t ea, uint32_t pa);
int nw_jit_itlb_lookup(uint32_t ea, uint32_t *pa);
void nw_jit_itlb_drop_page(uint32_t ea);
void nw_jit_itlb_note_msr(uint32_t old_msr, uint32_t new_msr);
uint64_t nw_jit_itlb_hits(void);
uint64_t nw_jit_itlb_misses(void);

typedef uint32_t (*nw_jit_host_lwz_pa)(void *host, uint32_t pa, uint32_t pc, int *fault);
typedef void (*nw_jit_host_stw_pa)(void *host, uint32_t pa, uint32_t val, uint32_t pc, int *fault);
void nw_jit_set_host_pa(nw_jit_host_lwz_pa lwz, nw_jit_host_stw_pa stw);
/* status: 0 = OK (*value in return), 1 = NOP (leave rD), 2 = EXC (return is new pc). */
typedef uint32_t (*nw_jit_host_mfspr)(void *host, uint32_t spr, uint32_t guest_pc, int *status);
void nw_jit_set_host_mfspr(nw_jit_host_mfspr fn);
/* Same work as kpx execute_isync: flush the pending icbi range (and NW
 * JIT pages), then the emitter does ISB. No PC bump; the JIT owns PC. */
typedef void (*nw_jit_host_isync)(void *host);
void nw_jit_set_host_isync(nw_jit_host_isync fn);
/* Same work as kpx execute_mtmsr: set_msr(rS), no PC bump. */
typedef void (*nw_jit_host_mtmsr)(void *host, uint32_t msr);
void nw_jit_set_host_mtmsr(nw_jit_host_mtmsr fn);
typedef void (*nw_jit_host_mtsr)(void *host, uint32_t sr, uint32_t val);
void nw_jit_set_host_mtsr(nw_jit_host_mtsr fn);
typedef uint32_t (*nw_jit_host_mfsr)(void *host, uint32_t sr);
void nw_jit_set_host_mfsr(nw_jit_host_mfsr fn);
typedef void (*nw_jit_host_trap)(void *host, struct nw_jit_cpu *cpu);
void nw_jit_set_host_trap(nw_jit_host_trap fn);
typedef void (*nw_jit_host_sc)(void *host, uint32_t guest_pc);
void nw_jit_set_host_sc(nw_jit_host_sc fn);
typedef void (*nw_jit_host_mtspr)(void *host, uint32_t spr, uint32_t val);
void nw_jit_set_host_mtspr(nw_jit_host_mtspr fn);
typedef void (*nw_jit_host_lvx)(void *host, uint32_t vd, uint32_t ea, uint32_t pc, int *fault, uint32_t *out);
void nw_jit_set_host_lvx(nw_jit_host_lvx fn);
typedef void (*nw_jit_host_stvx)(void *host, uint32_t ea, const uint32_t *w, uint32_t pc, int *fault);
void nw_jit_set_host_stvx(nw_jit_host_stvx fn);
typedef void (*nw_jit_host_vmx)(void *host, uint32_t op, struct nw_jit_cpu *cpu);
void nw_jit_set_host_vmx(nw_jit_host_vmx fn);
typedef void (*nw_jit_host_rfi)(void *host, struct nw_jit_cpu *cpu);
void nw_jit_set_host_rfi(nw_jit_host_rfi fn);
typedef void *(*nw_jit_host_chain)(void *host, struct nw_jit_cpu *cpu,
				   uint32_t chain_pc, int *n2,
				   int *uses_fpr, int *uses_vr,
				   uint32_t *dsi_pc, uint32_t *chain2,
				   int cur_fpr, int cur_vr);
void nw_jit_set_host_chain(nw_jit_host_chain fn);
typedef void (*nw_jit_host_icbi)(void *host, uint32_t ea);
void nw_jit_set_host_icbi(nw_jit_host_icbi fn);
/* Same work as kpx execute_tlbie: mmu.tlbie + DTLB drop_page, no PC bump. */
typedef void (*nw_jit_host_tlbie)(void *host, uint32_t ea);
void nw_jit_set_host_tlbie(nw_jit_host_tlbie fn);
typedef void (*nw_jit_host_tlbia)(void *host);
void nw_jit_set_host_tlbia(nw_jit_host_tlbia fn);
typedef uint32_t (*nw_jit_host_lwarx)(void *host, uint32_t ea, uint32_t pc, int *fault);
void nw_jit_set_host_lwarx(nw_jit_host_lwarx fn);
typedef int (*nw_jit_host_stwcx)(void *host, uint32_t ea, uint32_t val, uint32_t pc, int *fault);
void nw_jit_set_host_stwcx(nw_jit_host_stwcx fn);
typedef void (*nw_jit_host_lfd)(void *host, uint32_t fd, uint32_t ea, uint32_t pc, int *fault, uint64_t *out);
void nw_jit_set_host_lfd(nw_jit_host_lfd fn);
typedef void (*nw_jit_host_stfd)(void *host, uint32_t ea, uint64_t val, uint32_t pc, int *fault);
void nw_jit_set_host_stfd(nw_jit_host_stfd fn);
uint64_t nw_jit_flush_count(void);
uint64_t nw_jit_evict_count(void);
uint64_t nw_jit_compile_count(void);
uint64_t nw_jit_wrap_count(void);
size_t nw_jit_code_used(void);
void nw_jit_stats_print(const char *why);

/* PPC instruction constructors for the harness. */
uint32_t nw_ppc_addi(int rd, int ra, int simm);
uint32_t nw_ppc_addis(int rd, int ra, int simm);
uint32_t nw_ppc_mulli(int rd, int ra, int simm);
uint32_t nw_ppc_addic(int rd, int ra, int simm, int rc);
uint32_t nw_ppc_add(int rd, int ra, int rb, int rc);
uint32_t nw_ppc_addc(int rd, int ra, int rb, int rc);
uint32_t nw_ppc_addco(int rd, int ra, int rb, int rc);
uint32_t nw_ppc_subfe(int rd, int ra, int rb, int rc);
uint32_t nw_ppc_subfeo(int rd, int ra, int rb, int rc);
uint32_t nw_ppc_subf(int rd, int ra, int rb, int rc);
uint32_t nw_ppc_subfo(int rd, int ra, int rb, int rc);
uint32_t nw_ppc_subfc(int rd, int ra, int rb, int rc);
uint32_t nw_ppc_subfze(int rd, int ra, int rc);
uint32_t nw_ppc_rlwinm(int ra, int rs, int sh, int mb, int me);
uint32_t nw_ppc_rlwnm(int ra, int rs, int rb, int mb, int me);
uint32_t nw_ppc_lmw(int rd, int ra, int d);
uint32_t nw_ppc_stmw(int rs, int ra, int d);
uint32_t nw_ppc_rlwimi(int ra, int rs, int sh, int mb, int me);
uint32_t nw_ppc_lwz(int rd, int ra, int d);
uint32_t nw_ppc_lwzu(int rd, int ra, int d);
uint32_t nw_ppc_lbz(int rd, int ra, int d);
uint32_t nw_ppc_lbzu(int rd, int ra, int d);
uint32_t nw_ppc_lbzx(int rd, int ra, int rb);
uint32_t nw_ppc_lvx(int vd, int ra, int rb);
uint32_t nw_ppc_lvxl(int vd, int ra, int rb);
uint32_t nw_ppc_stvx(int vs, int ra, int rb);
uint32_t nw_ppc_stvxl(int vs, int ra, int rb);
uint32_t nw_ppc_stb(int rs, int ra, int d);
uint32_t nw_ppc_stbu(int rs, int ra, int d);
uint32_t nw_ppc_stw(int rs, int ra, int d);
uint32_t nw_ppc_stwu(int rs, int ra, int d);
uint32_t nw_ppc_stwx(int rs, int ra, int rb);
uint32_t nw_ppc_sthx(int rs, int ra, int rb);
uint32_t nw_ppc_sthux(int rs, int ra, int rb);
uint32_t nw_ppc_lwzx(int rd, int ra, int rb);
uint32_t nw_ppc_lhax(int rd, int ra, int rb);
uint32_t nw_ppc_lhaux(int rd, int ra, int rb);
uint32_t nw_ppc_lha(int rd, int ra, int d);
uint32_t nw_ppc_sth(int rs, int ra, int d);
uint32_t nw_ppc_sthu(int rs, int ra, int d);
uint32_t nw_ppc_bclr(int bo, int bi);
uint32_t nw_ppc_bcctr(int bo, int bi);
uint32_t nw_ppc_or(int ra, int rs, int rb);
uint32_t nw_ppc_xor(int ra, int rs, int rb);
uint32_t nw_ppc_and(int ra, int rs, int rb, int rc);
uint32_t nw_ppc_andc(int ra, int rs, int rb, int rc);
uint32_t nw_ppc_cntlzw(int ra, int rs, int rc);
uint32_t nw_ppc_neg(int rd, int ra, int rc);
uint32_t nw_ppc_ori(int ra, int rs, unsigned uimm);
uint32_t nw_ppc_oris(int ra, int rs, unsigned uimm);
uint32_t nw_ppc_xori(int ra, int rs, unsigned uimm);
uint32_t nw_ppc_xoris(int ra, int rs, unsigned uimm);
uint32_t nw_ppc_cmp(int ra, int rb);
uint32_t nw_ppc_cmp_cr(int crfd, int ra, int rb);
uint32_t nw_ppc_cmpi(int ra, int simm);
uint32_t nw_ppc_cmpi_cr(int crfd, int ra, int simm);
uint32_t nw_ppc_andi_dot(int ra, int rs, unsigned uimm);
uint32_t nw_ppc_andis_dot(int ra, int rs, unsigned uimm);
uint32_t nw_ppc_subfco(int rd, int ra, int rb, int rc);
uint32_t nw_ppc_cmpli(int crfd, int ra, unsigned uimm);
uint32_t nw_ppc_cmpl(int crfd, int ra, int rb);
uint32_t nw_ppc_mtcrf(int crm, int rs);
uint32_t nw_ppc_mfcr(int rd);
uint32_t nw_ppc_mcrf(int crfd, int crfs);
uint32_t nw_ppc_crnor(int crbd, int crba, int crbb);
uint32_t nw_ppc_crxor(int crbd, int crba, int crbb);
uint32_t nw_ppc_creqv(int crbd, int crba, int crbb);
uint32_t nw_ppc_cror(int crbd, int crba, int crbb);
uint32_t nw_ppc_crorc(int crbd, int crba, int crbb);
uint32_t nw_ppc_crand(int crbd, int crba, int crbb);
uint32_t nw_ppc_crandc(int crbd, int crba, int crbb);
uint32_t nw_ppc_crnand(int crbd, int crba, int crbb);
uint32_t nw_ppc_extsh(int ra, int rs, int rc);
uint32_t nw_ppc_extsb(int ra, int rs, int rc);
uint32_t nw_ppc_slw(int ra, int rs, int rb, int rc);
uint32_t nw_ppc_srw(int ra, int rs, int rb, int rc);
uint32_t nw_ppc_sraw(int ra, int rs, int rb, int rc);
uint32_t nw_ppc_srawi(int ra, int rs, int sh, int rc);
uint32_t nw_ppc_sync(void);
uint32_t nw_ppc_tlbsync(void);
uint32_t nw_ppc_dss(void);
uint32_t nw_ppc_dst(int ra, int rb, int strm);
uint32_t nw_ppc_dstst(int ra, int rb, int strm);
uint32_t nw_ppc_dcbt(int ra, int rb);
uint32_t nw_ppc_dcbtst(int ra, int rb);
uint32_t nw_ppc_dcbf(int ra, int rb);
uint32_t nw_ppc_eieio(void);
uint32_t nw_ppc_dcbz(int ra, int rb);
uint32_t nw_ppc_mtsr(int sr, int rs);
uint32_t nw_ppc_mtsrin(int rs, int rb);
uint32_t nw_ppc_mfsrin(int rd, int rb);
uint32_t nw_ppc_twi(int to, int ra, int simm);
uint32_t nw_ppc_mtmsr(int rs);
uint32_t nw_ppc_isync(void);
uint32_t nw_ppc_tlbie(int rb);
uint32_t nw_ppc_b(int disp, int lk);
uint32_t nw_ppc_bc(int bo, int bi, int disp);
uint32_t nw_ppc_blr(void);
uint32_t nw_ppc_mfspr(int rd, int spr);
uint32_t nw_ppc_mtspr(int spr, int rs);
uint32_t nw_ppc_lfd(int frd, int ra, int d);
uint32_t nw_ppc_stfd(int frs, int ra, int d);
uint32_t nw_ppc_lfs(int frd, int ra, int d);
uint32_t nw_ppc_stfs(int frs, int ra, int d);
uint32_t nw_ppc_lfsu(int frd, int ra, int d);
uint32_t nw_ppc_lfdu(int frd, int ra, int d);
uint32_t nw_ppc_stfdu(int frs, int ra, int d);
uint32_t nw_ppc_lhzu(int rd, int ra, int d);
uint32_t nw_ppc_vaddubm(int vd, int va, int vb);
uint32_t nw_ppc_lfsx(int frd, int ra, int rb);
uint32_t nw_ppc_stfsx(int frs, int ra, int rb);
uint32_t nw_ppc_fdivs(int frd, int fra, int frb);
uint32_t nw_ppc_fsubs(int frd, int fra, int frb);
uint32_t nw_ppc_fadds(int frd, int fra, int frb);
uint32_t nw_ppc_fmuls(int frd, int fra, int frc);
uint32_t nw_ppc_fmadds(int frd, int fra, int frc, int frb);
uint32_t nw_ppc_fmsubs(int frd, int fra, int frc, int frb);
uint32_t nw_ppc_fnmsubs(int frd, int fra, int frc, int frb);
uint32_t nw_ppc_lfdx(int frd, int ra, int rb);
uint32_t nw_ppc_stfdx(int frs, int ra, int rb);
uint32_t nw_ppc_fmadd(int frd, int fra, int frc, int frb);
uint32_t nw_ppc_fcmpo(int crfd, int fra, int frb);
uint32_t nw_ppc_fcmpu(int crfd, int fra, int frb);
uint32_t nw_ppc_fabs(int frd, int frb);
uint32_t nw_ppc_vand(int vd, int va, int vb);
uint32_t nw_ppc_vandc(int vd, int va, int vb);
uint32_t nw_ppc_vxor(int vd, int va, int vb);
uint32_t nw_ppc_vsububm(int vd, int va, int vb);
uint32_t nw_ppc_vslh(int vd, int va, int vb);
uint32_t nw_ppc_vcmpequw(int vd, int va, int vb, int rc);
uint32_t nw_ppc_vcmpequb(int vd, int va, int vb, int rc);
uint32_t nw_ppc_vminsb(int vd, int va, int vb);
uint32_t nw_ppc_vsr(int vd, int va, int vb);
uint32_t nw_ppc_vsrw(int vd, int va, int vb);
uint32_t nw_ppc_vspltisw(int vd, int simm);
uint32_t nw_ppc_vsro(int vd, int va, int vb);
uint32_t nw_ppc_vslo(int vd, int va, int vb);
uint32_t nw_ppc_vspltisb(int vd, int simm);
uint32_t nw_ppc_vspltw(int vd, int uimm, int vb);
uint32_t nw_ppc_vspltb(int vd, int uimm, int vb);
uint32_t nw_ppc_vsl(int vd, int va, int vb);
uint32_t nw_ppc_mtvscr(int vb);
uint32_t nw_ppc_mfvscr(int vd);
uint32_t nw_ppc_vsldoi(int vd, int va, int vb, int shb);
uint32_t nw_ppc_vmladduhm(int vd, int va, int vb, int vc);
uint32_t nw_ppc_vsubshs(int vd, int va, int vb);
uint32_t nw_ppc_vmrghb(int vd, int va, int vb);
uint32_t nw_ppc_vmrglb(int vd, int va, int vb);
uint32_t nw_ppc_vsrb(int vd, int va, int vb);
uint32_t nw_ppc_vslb(int vd, int va, int vb);
uint32_t nw_ppc_adde(int rd, int ra, int rb, int rc);
uint32_t nw_ppc_addeo(int rd, int ra, int rb, int rc);
uint32_t nw_ppc_eqv(int ra, int rs, int rb);
uint32_t nw_ppc_nand(int ra, int rs, int rb);
uint32_t nw_ppc_fneg(int frd, int frb);
uint32_t nw_ppc_fmr(int frd, int frb);
uint32_t nw_ppc_frsp(int frd, int frb);
uint32_t nw_ppc_mffs(int frd);
uint32_t nw_ppc_fnmsub(int frd, int fra, int frc, int frb);
uint32_t nw_ppc_mtfsf(int fm, int frb);
uint32_t nw_ppc_vadduwm(int vd, int va, int vb);
uint32_t nw_ppc_vsraw(int vd, int va, int vb);
uint32_t nw_ppc_vpkswss(int vd, int va, int vb);
uint32_t nw_ppc_mullw(int rd, int ra, int rb, int rc);
uint32_t nw_ppc_mulhwu(int rd, int ra, int rb, int rc);
uint32_t nw_ppc_mulhw(int rd, int ra, int rb, int rc);
uint32_t nw_ppc_lhzx(int rd, int ra, int rb);
uint32_t nw_ppc_lhzux(int rd, int ra, int rb);
uint32_t nw_ppc_lwbrx(int rd, int ra, int rb);
uint32_t nw_ppc_subfic(int rd, int ra, int simm);
uint32_t nw_ppc_stbx(int rs, int ra, int rb);
uint32_t nw_ppc_stbux(int rs, int ra, int rb);
uint32_t nw_ppc_stwux(int rs, int ra, int rb);
uint32_t nw_ppc_lwzux(int rd, int ra, int rb);
uint32_t nw_ppc_lbzux(int rd, int ra, int rb);
uint32_t nw_ppc_divwu(int rd, int ra, int rb, int rc);
uint32_t nw_ppc_divwuo(int rd, int ra, int rb, int rc);
uint32_t nw_ppc_divw(int rd, int ra, int rb, int rc);
uint32_t nw_ppc_divwo(int rd, int ra, int rb, int rc);
uint32_t nw_ppc_lswi(int rd, int ra, int nb);
uint32_t nw_ppc_stswi(int rs, int ra, int nb);
uint32_t nw_ppc_mfmsr(int rd);
uint32_t nw_ppc_mftb(int rd, int tbr);
uint32_t nw_ppc_sc(void);

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
	NW_PPC_BO_FALSE = 4,
	NW_PPC_BO_BDNZ = 16,	/* dec CTR, branch if CTR != 0 */
	NW_PPC_BO_BDZ = 18,	/* dec CTR, branch if CTR == 0 */
	NW_PPC_BO_ALWAYS = 20
};

#endif
