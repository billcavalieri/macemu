/*
 *  nw_jit.cpp - New World ARM64 JIT: integer subset + C oracle
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

#include "nw_jit.h"
#include "nw_io.h"
#include "nw_boot_contract.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/mman.h>
#ifdef __APPLE__
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#include <time.h>
#endif

uint32_t nw_jit_helper_lwz(struct nw_jit_cpu *cpu, uint32_t ea);
void nw_jit_helper_stw(struct nw_jit_cpu *cpu, uint32_t ea, uint32_t val);
uint32_t nw_jit_helper_lwz_pa(struct nw_jit_cpu *cpu, uint32_t pa);
void nw_jit_helper_stw_pa(struct nw_jit_cpu *cpu, uint32_t pa, uint32_t val);
void nw_jit_helper_mfspr(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t spr);
uint32_t nw_jit_helper_lh(struct nw_jit_cpu *cpu, uint32_t ea);
void nw_jit_helper_sth(struct nw_jit_cpu *cpu, uint32_t ea, uint32_t val);
uint32_t nw_jit_helper_lb(struct nw_jit_cpu *cpu, uint32_t ea);
void nw_jit_helper_stb(struct nw_jit_cpu *cpu, uint32_t ea, uint32_t val);
uint32_t nw_jit_helper_sraw(struct nw_jit_cpu *cpu, uint32_t rs, uint32_t rb);
void nw_jit_helper_lmw(struct nw_jit_cpu *cpu, uint32_t ea, uint32_t rd);
void nw_jit_helper_stmw(struct nw_jit_cpu *cpu, uint32_t ea, uint32_t rs);
void nw_jit_helper_isync(struct nw_jit_cpu *cpu);
void nw_jit_helper_mtmsr(struct nw_jit_cpu *cpu, uint32_t msr);
void nw_jit_helper_mtsr(struct nw_jit_cpu *cpu, uint32_t sr, uint32_t val);
void nw_jit_helper_dcbz(struct nw_jit_cpu *cpu, uint32_t ea);
int nw_jit_helper_twi(struct nw_jit_cpu *cpu, uint32_t to, uint32_t a, uint32_t simm);
void nw_jit_helper_bc(struct nw_jit_cpu *cpu, uint32_t op, uint32_t pc);
void nw_jit_helper_mtspr(struct nw_jit_cpu *cpu, uint32_t spr, uint32_t val);
void nw_jit_helper_lvx(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t ra, uint32_t rb);
void nw_jit_helper_stvx(struct nw_jit_cpu *cpu, uint32_t vs, uint32_t ra, uint32_t rb);
void nw_jit_helper_lfd(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t ra, uint32_t simm);
void nw_jit_helper_stfd(struct nw_jit_cpu *cpu, uint32_t fs, uint32_t ra, uint32_t simm);
void nw_jit_helper_lfs(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t ea);
void nw_jit_helper_stfs(struct nw_jit_cpu *cpu, uint32_t fs, uint32_t ea);
void nw_jit_helper_fadds(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fb);
void nw_jit_helper_fsubs(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fb);
void nw_jit_helper_fdivs(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fb);
void nw_jit_helper_fmuls(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fc);
void nw_jit_helper_fmadds(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fc, uint32_t fb);
void nw_jit_helper_fmsubs(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fc, uint32_t fb);
void nw_jit_helper_fnmsubs(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fc, uint32_t fb);
void nw_jit_helper_fmadd(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fc, uint32_t fb);
void nw_jit_helper_fcmpo(struct nw_jit_cpu *cpu, uint32_t crfd, uint32_t fa, uint32_t fb);
void nw_jit_helper_vadduwm(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb);
void nw_jit_helper_vaddubm(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb);
void nw_jit_helper_vsraw(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb);
void nw_jit_helper_vsrw(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb);
void nw_jit_helper_vspltisw(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t uimm);
void nw_jit_helper_vpkswss(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb);
void nw_jit_helper_mcrf(struct nw_jit_cpu *cpu, uint32_t crfd, uint32_t crfs);
void nw_jit_helper_fneg(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fb);
void nw_jit_helper_fmr(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fb);
void nw_jit_helper_frsp(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fb);
void nw_jit_helper_mffs(struct nw_jit_cpu *cpu, uint32_t fd);
void nw_jit_helper_fnmsub(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fc, uint32_t fb);
void nw_jit_helper_mtfsf(struct nw_jit_cpu *cpu, uint32_t fm, uint32_t fb);
void nw_jit_helper_fsub(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fb);
void nw_jit_helper_fadd(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fb);
void nw_jit_helper_fmul(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fc);
void nw_jit_helper_fdiv(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fb);
void nw_jit_helper_fctiwz(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fb);
void nw_jit_helper_lvsl(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t ea, int sl);
void nw_jit_helper_vor(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb);
void nw_jit_helper_vand(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb);
void nw_jit_helper_vandc(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb);
void nw_jit_helper_vxor(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb);
void nw_jit_helper_vsububm(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb);
void nw_jit_helper_vslh(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb);
void nw_jit_helper_vsrb(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb);
void nw_jit_helper_vslb(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb);
void nw_jit_helper_fabs(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fb);
void nw_jit_helper_adde(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t ra, uint32_t rb, uint32_t rc);
void nw_jit_helper_addeo(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t ra, uint32_t rb, uint32_t rc);
void nw_jit_helper_vsel(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb, uint32_t vc);
void nw_jit_helper_vperm(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb, uint32_t vc);
void nw_jit_helper_vsldoi(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb, uint32_t shb);
void nw_jit_helper_vspltish(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t uimm);
void nw_jit_helper_vspltw(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t uimm, uint32_t vb);
void nw_jit_helper_vspltb(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t uimm, uint32_t vb);
void nw_jit_helper_vmrghw(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb);
void nw_jit_helper_vmrghb(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb);
void nw_jit_helper_vmrglb(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb);
void nw_jit_helper_vmrglw(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb);
void nw_jit_helper_vsumsws(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb);
void nw_jit_helper_vmx(struct nw_jit_cpu *cpu, uint32_t op);
void nw_jit_helper_rfi(struct nw_jit_cpu *cpu);
void nw_jit_helper_icbi(struct nw_jit_cpu *cpu, uint32_t ea);
void nw_jit_helper_tlbie(struct nw_jit_cpu *cpu, uint32_t ea);
void nw_jit_helper_lwarx(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t ea);
void nw_jit_helper_stwcx(struct nw_jit_cpu *cpu, uint32_t rs, uint32_t ea);
void nw_jit_helper_lswx(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t ea, uint32_t nb);
void nw_jit_helper_stswx(struct nw_jit_cpu *cpu, uint32_t rs, uint32_t ea, uint32_t nb);
void nw_jit_helper_vmsumshm(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb, uint32_t vc, int sat);
void nw_jit_helper_vmladduhm(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb, uint32_t vc);
void nw_jit_helper_vsubshs(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb);
void nw_jit_helper_addze(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t ra, uint32_t rc);
void nw_jit_helper_subfze(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t ra, uint32_t rc);
void nw_jit_helper_mullwo(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t ra, uint32_t rb, uint32_t rc);
void nw_jit_helper_divw(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t ra, uint32_t rb, uint32_t rc);
void nw_jit_helper_divwo(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t ra, uint32_t rb, uint32_t rc);
void nw_jit_helper_divwuo(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t ra, uint32_t rb, uint32_t rc);
void nw_jit_helper_vcmpequw(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb, uint32_t rc);
void nw_jit_helper_vcmpequb(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb, uint32_t rc);
void nw_jit_helper_vminsb(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb);
void nw_jit_helper_vsr(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb);
void nw_jit_helper_vsl(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb);
void nw_jit_helper_vsro(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb);
void nw_jit_helper_vslo(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb);
void nw_jit_helper_vspltisb(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t uimm);
void nw_jit_helper_mtvscr(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb);
void nw_jit_helper_mfvscr(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb);
void nw_jit_helper_mtsrin(struct nw_jit_cpu *cpu, uint32_t rs, uint32_t rb);
void nw_jit_helper_mfsrin(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t rb);
void nw_jit_helper_lwbrx(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t ea);
void nw_jit_helper_sc(struct nw_jit_cpu *cpu);
void nw_jit_helper_addme(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t ra, uint32_t rc);
void nw_jit_helper_subfme(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t ra, uint32_t rc);
void nw_jit_helper_orc(struct nw_jit_cpu *cpu, uint32_t ra, uint32_t rs, uint32_t rb, uint32_t rc);
void nw_jit_helper_mcrxr(struct nw_jit_cpu *cpu, uint32_t crfd);
void nw_jit_helper_mfsr(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t sr);
int nw_jit_helper_tw(struct nw_jit_cpu *cpu, uint32_t to, uint32_t a, uint32_t b);
void nw_jit_helper_tlbia(struct nw_jit_cpu *cpu);
void nw_jit_helper_lve(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t ra, uint32_t rb, uint32_t sz);
void nw_jit_helper_stve(struct nw_jit_cpu *cpu, uint32_t vs, uint32_t ra, uint32_t rb, uint32_t sz);
void nw_jit_helper_lhbrx(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t ea);
void nw_jit_helper_sthbrx(struct nw_jit_cpu *cpu, uint32_t rs, uint32_t ea);
void nw_jit_helper_stwbrx(struct nw_jit_cpu *cpu, uint32_t rs, uint32_t ea);
void nw_jit_helper_fnabs(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fb);
void nw_jit_helper_fsel(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fc, uint32_t fb);
void nw_jit_helper_fctiw(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fb);
void nw_jit_helper_cr1(struct nw_jit_cpu *cpu);
void nw_jit_helper_bclr(struct nw_jit_cpu *cpu, uint32_t op, uint32_t pc);
void nw_jit_helper_fmsub(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fc, uint32_t fb);
void nw_jit_helper_fnmadd(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fc, uint32_t fb);
void nw_jit_helper_fnmadds(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fc, uint32_t fb);
void nw_jit_helper_fres(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fb);
void nw_jit_helper_frsqrte(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fb);
void nw_jit_helper_mcrfs(struct nw_jit_cpu *cpu, uint32_t crfd, uint32_t crfs);
void nw_jit_helper_mtfsb(struct nw_jit_cpu *cpu, uint32_t crbd, uint32_t setbit);
void nw_jit_helper_mtfsfi(struct nw_jit_cpu *cpu, uint32_t crfd, uint32_t imm);
void *nw_jit_helper_chain(struct nw_jit_cpu *cpu, uint32_t chain_pc, uint32_t cur_class);

enum { NW_JIT_CODE_SIZE = 64 << 20, NW_JIT_CACHE = 262144, NW_JIT_PROBE = 16 };
enum { NW_JIT_USED_EMPTY = 0, NW_JIT_USED_LIVE = 1, NW_JIT_USED_TOMB = 2 };
enum { NW_JIT_BANKS = 16, NW_JIT_BANK_SIZE = NW_JIT_CODE_SIZE / NW_JIT_BANKS };
static_assert((NW_JIT_CACHE & (NW_JIT_CACHE - 1)) == 0, "cache size power of two");
static_assert(NW_JIT_CODE_SIZE % NW_JIT_BANKS == 0, "even banks");
static_assert(NW_JIT_BANK_SIZE == (4 << 20), "4 MB banks");
enum { NW_JIT_HITS_AGE = 4096 };
enum { NW_JIT_RAM_PAGES = 131072, NW_JIT_ROM_PAGES = 2048 };

struct nw_jit_entry {
	uint32_t phys_page, guest_pc, msr_ir, endian;
	nw_jit_fn fn;
	uint32_t first_opcode;
	uint32_t chain_pc;	/* fall-through or uncond b target; 0 = no chain */
	uint8_t used;		/* 0 empty (stop), 1 live, 2 tombstone (skip) */
	uint8_t n;
	uint8_t uses_fpr;
	uint8_t uses_vr;
	uint16_t hits;
	int16_t chain_disp;	/* bc taken displacement; 0 if last is not bc */
	uint32_t gpr_mask;	/* GPRs this block reads or writes; 0xffffffff = all */
	uint32_t code_bytes;	/* host bytes at fn; 0 if this entry is not movable */
};
static_assert(sizeof(struct nw_jit_entry) == 48, "nw_jit_entry stays 48 bytes");

static uint8_t *g_code;
static uint8_t *g_code_spare;
static size_t g_code_used;
static struct nw_jit_entry g_cache[NW_JIT_CACHE];
static uint8_t g_pagebit_ram[(NW_JIT_RAM_PAGES + 7) / 8];
static uint8_t g_pagebit_rom[(NW_JIT_ROM_PAGES + 7) / 8];
static uint32_t g_ram_base, g_ram_size, g_rom_base, g_rom_size;
static uint64_t g_flush;
static uint64_t g_flush_src[NW_JIT_FL_N];	/* entries dropped, by cause */
static uint64_t g_flush_calls[NW_JIT_FL_N];	/* invalidate calls, by cause */
static uint64_t g_compiles;
static uint64_t g_evict, g_recompile_n;
static uint64_t g_exec_blocks, g_exec_insns;
static uint64_t g_chain_hops;
static int g_tail_hops, g_tail_n, g_tail_dsi_n, g_tail_fpr, g_tail_vr;
static uint32_t g_tail_dsi_pc;
enum { NW_JIT_TAIL_MAX = 1 };
static uint64_t g_code_emitted, g_compiles_at_wrap, g_wraps;
#ifdef __APPLE__
static uint64_t g_wx_ns, g_icache_ns, g_wx_n;

static uint64_t nw_nsnow(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
#endif
static int g_occ_max;
static uint64_t g_dtlb_fl[NW_JIT_DTLB_FL_N];
static int g_mode = -1;
static nw_jit_host_lwz g_host_lwz;
static nw_jit_host_stw g_host_stw;
static nw_jit_host_msr g_host_msr;
static nw_jit_host_lwz_pa g_host_lwz_pa;
static nw_jit_host_stw_pa g_host_stw_pa;
static nw_jit_host_mfspr g_host_mfspr;
static nw_jit_host_isync g_host_isync;
static nw_jit_host_mtmsr g_host_mtmsr;
static nw_jit_host_mtsr g_host_mtsr;
static nw_jit_host_mfsr g_host_mfsr;
static uint32_t g_skip_raw_op, g_skip_raw_pc;
static char g_skip_raw_nm[24];
static unsigned g_skip_raw_seen;
static nw_jit_host_trap g_host_trap;
static nw_jit_host_sc g_host_sc;
static nw_jit_host_mtspr g_host_mtspr;
static nw_jit_host_lvx g_host_lvx;
static nw_jit_host_stvx g_host_stvx;
static nw_jit_host_vmx g_host_vmx;
static nw_jit_host_rfi g_host_rfi;
static nw_jit_host_chain g_host_chain;
static nw_jit_host_icbi g_host_icbi;
static nw_jit_host_tlbie g_host_tlbie;
static nw_jit_host_tlbia g_host_tlbia;
static nw_jit_host_lwarx g_host_lwarx;
static nw_jit_host_stwcx g_host_stwcx;
static nw_jit_host_lfd g_host_lfd;
static nw_jit_host_stfd g_host_stfd;
static nw_jit_host_lh g_host_lh;
static nw_jit_host_sth16 g_host_sth16;
static nw_jit_host_lb g_host_lb;
static nw_jit_host_stb8 g_host_stb;

enum { NW_JIT_DTLB_WAYS = 2 };
static struct nw_jit_dtlb_ent g_dtlb[NW_JIT_DTLB_N][NW_JIT_DTLB_WAYS];
static_assert(sizeof(struct nw_jit_dtlb_ent) == 32, "dtlb entry is 32 bytes");
static uint64_t g_dtlb_hit, g_dtlb_miss;
enum { DTLB_WHY_SR = 0, DTLB_WHY_BAT, DTLB_WHY_CONFLICT, DTLB_WHY_PR, DTLB_WHY_OTHER, DTLB_WHY_N };
static uint64_t g_dtlb_why[DTLB_WHY_N];
/* VSID|Ks|Kp|N: translation-relevant SR bits. T and reserved noise does not drop. */
enum { NW_JIT_SR_XLATE = 0x70ffffffu };
static uint32_t g_sr_gen[16];
static uint64_t g_mtsr_total, g_mtsr_vsid;
static uint32_t g_bat_gen;
static uint64_t g_bat_total, g_bat_bumps;

static struct {
	uint32_t ea_page;
	uint32_t pa_page;
	uint32_t flags;
	uint32_t sr_gen;
} g_itlb[NW_JIT_ITLB_N];
static uint32_t g_itlb_sticky_ea, g_itlb_sticky_pa;
static int g_itlb_sticky;
static uint64_t g_itlb_hit, g_itlb_miss;

struct nw_jit_hist {
	int prim;
	int xo;
	const char *name;
	uint64_t n, miss, insns;
};

static struct nw_jit_hist g_hist[] = {
	{14, -1, "addi", 0, 0, 0},
	{15, -1, "addis", 0, 0, 0},
	{7, -1, "mulli", 0, 0, 0},
	{12, -1, "addic", 0, 0, 0},
	{13, -1, "addic.", 0, 0, 0},
	{31, 10, "addc", 0, 0, 0},
	{31, 522, "addco", 0, 0, 0},
	{31, 520, "subfco", 0, 0, 0},
	{31, 136, "subfe", 0, 0, 0},
	{31, 648, "subfeo", 0, 0, 0},
	{31, 40, "subf", 0, 0, 0},
	{31, 552, "subfo", 0, 0, 0},
	{11, -1, "cmpi", 0, 0, 0},
	{10, -1, "cmpli", 0, 0, 0},
	{28, -1, "andi.", 0, 0, 0},
	{31, 144, "mtcrf", 0, 0, 0},
	{31, 19, "mfcr", 0, 0, 0},
	{19, 33, "crnor", 0, 0, 0},
	{19, 193, "crxor", 0, 0, 0},
	{19, 289, "creqv", 0, 0, 0},
	{19, 449, "cror", 0, 0, 0},
	{19, 417, "crorc", 0, 0, 0},
	{19, 257, "crand", 0, 0, 0},
	{19, 129, "crandc", 0, 0, 0},
	{19, 225, "crnand", 0, 0, 0},
	{31, 922, "extsh", 0, 0, 0},
	{31, 954, "extsb", 0, 0, 0},
	{31, 24, "slw", 0, 0, 0},
	{31, 536, "srw", 0, 0, 0},
	{31, 792, "sraw", 0, 0, 0},
	{31, 824, "srawi", 0, 0, 0},
	{31, 598, "sync", 0, 0, 0},
	{31, 822, "dss", 0, 0, 0},
	{31, 75, "mulhw", 0, 0, 0},
	{31, 279, "lhzx", 0, 0, 0},
	{31, 534, "lwbrx", 0, 0, 0},
	{31, 242, "mtsrin", 0, 0, 0},
	{31, 659, "mfsrin", 0, 0, 0},
	{31, 342, "dst", 0, 0, 0},
	{31, 374, "dstst", 0, 0, 0},
	{31, 278, "dcbt", 0, 0, 0},
	{31, 246, "dcbtst", 0, 0, 0},
	{31, 86, "dcbf", 0, 0, 0},
	{31, 854, "eieio", 0, 0, 0},
	{31, 1014, "dcbz", 0, 0, 0},
	{31, 210, "mtsr", 0, 0, 0},
	{3, -1, "twi", 0, 0, 0},
	{31, 146, "mtmsr", 0, 0, 0},
	{19, 50, "rfi", 0, 0, 0},
	{31, 103, "lvx", 0, 0, 0},
	{31, 231, "stvx", 0, 0, 0},
	{19, 150, "isync", 0, 0, 0},
	{21, -1, "rlwinm", 0, 0, 0},
	{23, -1, "rlwnm", 0, 0, 0},
	{46, -1, "lmw", 0, 0, 0},
	{47, -1, "stmw", 0, 0, 0},
	{20, -1, "rlwimi", 0, 0, 0},
	{16, -1, "bc", 0, 0, 0},
	{18, -1, "b", 0, 0, 0},
	{19, 16, "blr", 0, 0, 0},
	{19, 528, "bcctr", 0, 0, 0},
	{31, 266, "add", 0, 0, 0},
	{31, 444, "or", 0, 0, 0},
	{31, 316, "xor", 0, 0, 0},
	{31, 28, "and", 0, 0, 0},
	{31, 26, "cntlzw", 0, 0, 0},
	{31, 104, "neg", 0, 0, 0},
	{24, -1, "ori", 0, 0, 0},
	{31, 0, "cmp", 0, 0, 0},
	{31, 32, "cmpl", 0, 0, 0},
	{31, 339, "mfspr", 0, 0, 0},
	{31, 467, "mtspr", 0, 0, 0},
	{32, -1, "lwz", 0, 0, 0},
	{33, -1, "lwzu", 0, 0, 0},
	{34, -1, "lbz", 0, 0, 0},
	{31, 87, "lbzx", 0, 0, 0},
	{38, -1, "stb", 0, 0, 0},
	{39, -1, "stbu", 0, 0, 0},
	{36, -1, "stw", 0, 0, 0},
	{37, -1, "stwu", 0, 0, 0},
	{31, 23, "lwzx", 0, 0, 0},
	{31, 151, "stwx", 0, 0, 0},
	{31, 183, "stwux", 0, 0, 0},
	{31, 55, "lwzux", 0, 0, 0},
	{31, 119, "lbzux", 0, 0, 0},
	{31, 407, "sthx", 0, 0, 0},
	{31, 343, "lhax", 0, 0, 0},
	{31, 375, "lhaux", 0, 0, 0},
	{40, -1, "lhz", 0, 0, 0},
	{42, -1, "lha", 0, 0, 0},
	{43, -1, "lhau", 0, 0, 0},
	{44, -1, "sth", 0, 0, 0},
	{45, -1, "sthu", 0, 0, 0},
	{50, -1, "lfd", 0, 0, 0},
	{54, -1, "stfd", 0, 0, 0},
	{48, -1, "lfs", 0, 0, 0},
	{52, -1, "stfs", 0, 0, 0},
	{31, 535, "lfsx", 0, 0, 0},
	{31, 663, "stfsx", 0, 0, 0},
	{31, 599, "lfdx", 0, 0, 0},
	{31, 727, "stfdx", 0, 0, 0},
	{59, 18, "fdivs", 0, 0, 0},
	{59, 20, "fsubs", 0, 0, 0},
	{59, 21, "fadds", 0, 0, 0},
	{59, 25, "fmuls", 0, 0, 0},
	{59, 28, "fmsubs", 0, 0, 0},
	{59, 29, "fmadds", 0, 0, 0},
	{63, 40, "fneg", 0, 0, 0},
	{63, 72, "fmr", 0, 0, 0},
	{63, 711, "mtfsf", 0, 0, 0},
	{31, 235, "mullw", 0, 0, 0},
	{31, 491, "divw", 0, 0, 0},
	{31, 1003, "divwo", 0, 0, 0},
	{31, 11, "mulhwu", 0, 0, 0},
	{31, 60, "andc", 0, 0, 0},
	{31, 8, "subfc", 0, 0, 0},
	{25, -1, "oris", 0, 0, 0},
	{26, -1, "xori", 0, 0, 0},
	{27, -1, "xoris", 0, 0, 0},
	{29, -1, "andis.", 0, 0, 0},
};

static uint64_t g_v_cmp, g_v_miss, g_v_fail, g_v_skip_unsup, g_v_skip_mem;
static uint64_t g_v_skip_dsi, g_v_skip_io, g_v_other, g_v_other_miss;

enum { NW_JIT_SKIPN = 256, NW_JIT_SKIPTOP = 12 };
static struct {
	int prim, xo;
	uint64_t n, lost;
	uint32_t op, pc;
} g_skip[NW_JIT_SKIPN];
static uint64_t g_cut[NW_JIT_CUT_N];
static uint64_t g_hop_stop[NW_JIT_HOP_N];
static int g_pull_on;
static uint64_t g_it_jit, g_it_interp, g_it_us, g_it_class, g_it_fp, g_it_skip;
static uint64_t g_pull_jit, g_pull_vr, g_pull_skip, g_pull_vmx, g_pull_op6, g_pull_class;
static uint64_t g_pull_hop[NW_JIT_HOP_N];
struct nw_pull_pc { uint32_t pc; uint64_t n; int vr; };
static nw_pull_pc g_pull_pc[4];
struct nw_pull_sk { int prim; int xo; uint32_t pc; uint64_t n; };
static nw_pull_sk g_pull_sk[4];
static uint64_t g_codec_insns, g_other_insns;
static uint64_t g_codec_insns_tick, g_other_insns_tick;
static uint64_t g_kcall_fast;
enum { NW_JIT_DTLBH = 16, NW_JIT_IOH = 16 };
static struct {
	uint32_t page;
	uint64_t n;
} g_dtlb_h[NW_JIT_DTLBH];
static struct {
	uint32_t page, pc;
	uint64_t n;
} g_io_h[NW_JIT_IOH];

enum { NW_JIT_PCHOT = 1024, NW_JIT_PCPROBE = 8, NW_JIT_PCTOP = 12 };
static struct {
	uint32_t pc, op;
	uint64_t n;
} g_pchot[NW_JIT_PCHOT];
static uint64_t g_v_blocks[NW_JIT_MAX_BLOCK + 1];

static uint32_t spr_num(uint32_t op);

static int cache_slot(uint32_t phys_page, uint32_t guest_pc, uint32_t msr_ir, uint32_t endian)
{
	/* Mix then fold. Index is log2(NW_JIT_CACHE) bits (power of two).
	 * A low-bit mask of guest_pc * K aliases every 32 KiB (ROM is 4 MiB). */
	uint32_t h = (guest_pc >> 2) * 0x9e3779b1u;
	h ^= (phys_page >> 12) * 0x85ebca6bu;
	h ^= msr_ir * 0x27d4eb2fu;
	h ^= endian;
	h ^= h >> 15;
	return (int)(h & (NW_JIT_CACHE - 1));
}

void nw_jit_helper_isync(struct nw_jit_cpu *cpu)
{
	if (g_host_isync && cpu->host)
		g_host_isync(cpu->host);
}

void nw_jit_helper_mtmsr(struct nw_jit_cpu *cpu, uint32_t msr)
{
	const uint32_t old = cpu->msr;
	cpu->msr = msr;
	if (g_host_mtmsr && cpu->host)
		g_host_mtmsr(cpu->host, msr);
	else if ((old ^ msr) & 0x00000030u)	/* IR|DR only; rfi still uses no-op flush_if_pr */
		nw_jit_dtlb_flush_src(NW_JIT_DTLB_FL_MTMSR);
	nw_jit_itlb_note_msr(old, msr);
}

void nw_jit_mtsr_note(unsigned sr, uint32_t old_val, uint32_t new_val)
{
	g_mtsr_total++;
	if (((old_val ^ new_val) & (uint32_t)NW_JIT_SR_XLATE) == 0)
		return;
	g_mtsr_vsid++;
	g_sr_gen[sr & 0xfu]++;
	g_itlb_sticky = 0;
}

void nw_jit_helper_mtsr(struct nw_jit_cpu *cpu, uint32_t sr, uint32_t val)
{
	const unsigned i = sr & 0xfu;
	if (g_host_mtsr && cpu->host) {
		g_host_mtsr(cpu->host, i, val);
		cpu->sr[i] = val;
		return;
	}
	const uint32_t old = cpu->sr[i];
	if (old == val)
		return;
	cpu->sr[i] = val;
	nw_jit_mtsr_note(i, old, val);
}

void nw_jit_helper_mtsrin(struct nw_jit_cpu *cpu, uint32_t rs, uint32_t rb)
{
	nw_jit_helper_mtsr(cpu, (rb >> 28) & 0xfu, rs);
}

void nw_jit_helper_mfsrin(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t rb)
{
	const unsigned i = (rb >> 28) & 0xfu;
	uint32_t v = cpu->sr[i];
	if (g_host_mfsr && cpu->host)
		v = g_host_mfsr(cpu->host, i);
	cpu->gpr[rd & 31u] = v;
}

void nw_jit_helper_dcbz(struct nw_jit_cpu *cpu, uint32_t ea)
{
	const uint32_t base = ea & ~31u;
	for (int i = 0; i < 8; i++) {
		nw_jit_helper_stw(cpu, base + (uint32_t)i * 4u, 0);
		if (cpu->fault)
			return;
	}
}

int nw_jit_helper_twi(struct nw_jit_cpu *cpu, uint32_t to, uint32_t a, uint32_t simm)
{
	const int32_t sa = (int32_t)a;
	const int32_t sb = (int32_t)(int16_t)(uint16_t)simm;
	const int trap =
		((to & 0x10u) && sa < sb) ||
		((to & 0x08u) && sa > sb) ||
		((to & 0x04u) && sa == sb) ||
		((to & 0x02u) && a < (uint32_t)sb) ||
		((to & 0x01u) && a > (uint32_t)sb);
	if (!trap)
		return 0;
	if (g_host_trap && cpu->host)
		g_host_trap(cpu->host, cpu);
	cpu->fault = NW_JIT_FAULT_EXC;
	return 1;
}

void nw_jit_helper_sc(struct nw_jit_cpu *cpu)
{
	if (g_host_sc && cpu->host)
		g_host_sc(cpu->host, cpu->pc);
	cpu->fault = NW_JIT_FAULT_EXC;
}

void nw_jit_helper_mtspr(struct nw_jit_cpu *cpu, uint32_t spr, uint32_t val)
{
	if (g_host_mtspr && cpu->host)
		g_host_mtspr(cpu->host, spr, val);
}

void nw_jit_helper_lvx(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t ra, uint32_t rb)
{
	uint32_t ea = (ra ? cpu->gpr[ra] : 0u) + cpu->gpr[rb];
	ea &= ~15u;
	vd &= 31u;
	if (g_host_lvx && cpu->host) {
		int fault = 0;
		g_host_lvx(cpu->host, vd, ea, cpu->pc, &fault, cpu->vr[vd]);
		cpu->fault = (uint32_t)fault;
		return;
	}
	if (!cpu->mem || ea < cpu->mem_base ||
	    (ea - cpu->mem_base) + 16u > cpu->mem_size) {
		cpu->fault = 1;
		cpu->fault_ea = ea;
		return;
	}
	const uint8_t *p = cpu->mem + (ea - cpu->mem_base);
	for (int i = 0; i < 4; i++) {
		cpu->vr[vd][i] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
				 ((uint32_t)p[2] << 8) | (uint32_t)p[3];
		p += 4;
	}
}

void nw_jit_helper_stvx(struct nw_jit_cpu *cpu, uint32_t vs, uint32_t ra, uint32_t rb)
{
	uint32_t ea = (ra ? cpu->gpr[ra] : 0u) + cpu->gpr[rb];
	ea &= ~15u;
	vs &= 31u;
	if (g_host_stvx && cpu->host) {
		int fault = 0;
		g_host_stvx(cpu->host, ea, cpu->vr[vs], cpu->pc, &fault);
		cpu->fault = (uint32_t)fault;
		return;
	}
	if (!cpu->mem || ea < cpu->mem_base ||
	    (ea - cpu->mem_base) + 16u > cpu->mem_size) {
		cpu->fault = 1;
		cpu->fault_ea = ea;
		cpu->fault_st = 1;
		return;
	}
	uint8_t *p = cpu->mem + (ea - cpu->mem_base);
	for (int i = 0; i < 4; i++) {
		const uint32_t w = cpu->vr[vs][i];
		p[0] = (uint8_t)(w >> 24);
		p[1] = (uint8_t)(w >> 16);
		p[2] = (uint8_t)(w >> 8);
		p[3] = (uint8_t)w;
		p += 4;
	}
}

void nw_jit_helper_lfd(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t ra, uint32_t simm)
{
	const uint32_t ea = (ra ? cpu->gpr[ra & 31u] : 0u) + simm;
	fd &= 31u;
	if (g_host_lfd && cpu->host) {
		int fault = 0;
		g_host_lfd(cpu->host, fd, ea, cpu->pc, &fault, &cpu->fpr[fd]);
		cpu->fault = (uint32_t)fault;
		return;
	}
	if (!cpu->mem || ea < cpu->mem_base ||
	    (ea - cpu->mem_base) + 8u > cpu->mem_size) {
		cpu->fault = 1;
		cpu->fault_ea = ea;
		return;
	}
	const uint8_t *p = cpu->mem + (ea - cpu->mem_base);
	cpu->fpr[fd] =
		((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) |
		((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) |
		((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
		((uint64_t)p[6] << 8) | (uint64_t)p[7];
}

void nw_jit_helper_stfd(struct nw_jit_cpu *cpu, uint32_t fs, uint32_t ra, uint32_t simm)
{
	const uint32_t ea = (ra ? cpu->gpr[ra & 31u] : 0u) + simm;
	const uint64_t v = cpu->fpr[fs & 31u];
	if (g_host_stfd && cpu->host) {
		int fault = 0;
		g_host_stfd(cpu->host, ea, v, cpu->pc, &fault);
		cpu->fault = (uint32_t)fault;
		return;
	}
	if (!cpu->mem || ea < cpu->mem_base ||
	    (ea - cpu->mem_base) + 8u > cpu->mem_size) {
		cpu->fault = 1;
		cpu->fault_ea = ea;
		cpu->fault_st = 1;
		return;
	}
	uint8_t *p = cpu->mem + (ea - cpu->mem_base);
	p[0] = (uint8_t)(v >> 56);
	p[1] = (uint8_t)(v >> 48);
	p[2] = (uint8_t)(v >> 40);
	p[3] = (uint8_t)(v >> 32);
	p[4] = (uint8_t)(v >> 24);
	p[5] = (uint8_t)(v >> 16);
	p[6] = (uint8_t)(v >> 8);
	p[7] = (uint8_t)v;
}

static uint64_t f64_from_f32_bits(uint32_t i)
{
	float f;
	double d;
	uint64_t j;
	memcpy(&f, &i, 4);
	d = (double)f;
	memcpy(&j, &d, 8);
	return j;
}

static uint32_t f32_bits_from_f64(uint64_t j)
{
	const int exp = (int)((j >> 52) & 0x7ff);
	if (exp < 874 || exp > 896)
		return (uint32_t)(((j >> 32) & 0xc0000000u) | ((j >> 29) & 0x3fffffffu));
	double d;
	float f;
	uint32_t i;
	memcpy(&d, &j, 8);
	f = (float)d;
	memcpy(&i, &f, 4);
	return i;
}

static float f32_from_fpr(uint64_t j)
{
	double d;
	memcpy(&d, &j, 8);
	return (float)d;
}

static uint64_t fpr_from_f32(float f)
{
	double d = (double)f;
	uint64_t j;
	memcpy(&j, &d, 8);
	return j;
}

/* kpx fp_classify: FPSCR FPRF bits 15–19 (mask 0x1f000). */
static void nw_jit_fpscr_fprf(struct nw_jit_cpu *cpu, float x)
{
	if (cpu->fpscr & 0x80u)	/* VE: kpx skips classify */
		return;
	uint32_t c = cpu->fpscr & ~0x1f000u;
	if (x != x) {
		c |= 0x11000u;	/* C|FU */
	} else if (x == 0.f) {
		c |= 0x2000u;	/* FE */
		if (signbit(x))
			c |= 0x10000u;	/* C */
	} else if (isinf((double)x)) {
		c |= 0x1000u;	/* FU */
		c |= (x < 0.f) ? 0x8000u : 0x4000u;
	} else {
		if (fpclassify(x) == FP_SUBNORMAL)
			c |= 0x10000u;
		c |= (x < 0.f) ? 0x8000u : 0x4000u;
	}
	cpu->fpscr = c;
}

static void nw_jit_fpscr_fprf_d(struct nw_jit_cpu *cpu, double x)
{
	if (cpu->fpscr & 0x80u)
		return;
	uint32_t c = cpu->fpscr & ~0x1f000u;
	if (x != x) {
		c |= 0x11000u;
	} else if (x == 0.0) {
		c |= 0x2000u;
		if (signbit(x))
			c |= 0x10000u;
	} else if (isinf(x)) {
		c |= 0x1000u;
		c |= (x < 0.0) ? 0x8000u : 0x4000u;
	} else {
		if (fpclassify(x) == FP_SUBNORMAL)
			c |= 0x10000u;
		c |= (x < 0.0) ? 0x8000u : 0x4000u;
	}
	cpu->fpscr = c;
}

void nw_jit_helper_lfs(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t ea)
{
	fd &= 31u;
	if (g_host_lwz && cpu->host) {
		int fault = 0;
		const uint32_t w = g_host_lwz(cpu->host, ea, cpu->pc, &fault);
		cpu->fault = (uint32_t)fault;
		if (!fault)
			cpu->fpr[fd] = f64_from_f32_bits(w);
		return;
	}
	if (!cpu->mem || ea < cpu->mem_base ||
	    (ea - cpu->mem_base) + 4u > cpu->mem_size) {
		cpu->fault = 1;
		cpu->fault_ea = ea;
		return;
	}
	const uint8_t *p = cpu->mem + (ea - cpu->mem_base);
	const uint32_t w = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
			   ((uint32_t)p[2] << 8) | (uint32_t)p[3];
	cpu->fpr[fd] = f64_from_f32_bits(w);
}

void nw_jit_helper_stfs(struct nw_jit_cpu *cpu, uint32_t fs, uint32_t ea)
{
	const uint32_t w = f32_bits_from_f64(cpu->fpr[fs & 31u]);
	if (g_host_stw && cpu->host) {
		int fault = 0;
		g_host_stw(cpu->host, ea, w, cpu->pc, &fault);
		cpu->fault = (uint32_t)fault;
		return;
	}
	if (!cpu->mem || ea < cpu->mem_base ||
	    (ea - cpu->mem_base) + 4u > cpu->mem_size) {
		cpu->fault = 1;
		cpu->fault_ea = ea;
		cpu->fault_st = 1;
		return;
	}
	uint8_t *p = cpu->mem + (ea - cpu->mem_base);
	p[0] = (uint8_t)(w >> 24);
	p[1] = (uint8_t)(w >> 16);
	p[2] = (uint8_t)(w >> 8);
	p[3] = (uint8_t)w;
}

void nw_jit_helper_fadds(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fb)
{
	double a, b;
	memcpy(&a, &cpu->fpr[fa & 31u], 8);
	memcpy(&b, &cpu->fpr[fb & 31u], 8);
	const float r = (float)(a + b);
	cpu->fpr[fd & 31u] = fpr_from_f32(r);
	nw_jit_fpscr_fprf(cpu, r);
}

void nw_jit_helper_fsubs(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fb)
{
	double a, b;
	memcpy(&a, &cpu->fpr[fa & 31u], 8);
	memcpy(&b, &cpu->fpr[fb & 31u], 8);
	const float r = (float)(a - b);
	cpu->fpr[fd & 31u] = fpr_from_f32(r);
	nw_jit_fpscr_fprf(cpu, r);
}

void nw_jit_helper_fdivs(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fb)
{
	double a, b;
	memcpy(&a, &cpu->fpr[fa & 31u], 8);
	memcpy(&b, &cpu->fpr[fb & 31u], 8);
	const float r = (float)(a / b);
	cpu->fpr[fd & 31u] = fpr_from_f32(r);
	nw_jit_fpscr_fprf(cpu, r);
}

void nw_jit_helper_fmuls(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fc)
{
	double a, c;
	memcpy(&a, &cpu->fpr[fa & 31u], 8);
	memcpy(&c, &cpu->fpr[fc & 31u], 8);
	const float r = (float)(a * c);
	cpu->fpr[fd & 31u] = fpr_from_f32(r);
	nw_jit_fpscr_fprf(cpu, r);
}

void nw_jit_helper_fmadds(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fc, uint32_t fb)
{
	double a, c, b;
	memcpy(&a, &cpu->fpr[fa & 31u], 8);
	memcpy(&c, &cpu->fpr[fc & 31u], 8);
	memcpy(&b, &cpu->fpr[fb & 31u], 8);
	const float r = (float)(a * c + b);
	cpu->fpr[fd & 31u] = fpr_from_f32(r);
	nw_jit_fpscr_fprf(cpu, r);
}

void nw_jit_helper_fmsubs(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fc, uint32_t fb)
{
	double a, c, b;
	memcpy(&a, &cpu->fpr[fa & 31u], 8);
	memcpy(&c, &cpu->fpr[fc & 31u], 8);
	memcpy(&b, &cpu->fpr[fb & 31u], 8);
	const float r = (float)(a * c - b);
	cpu->fpr[fd & 31u] = fpr_from_f32(r);
	nw_jit_fpscr_fprf(cpu, r);
}

void nw_jit_helper_fnmsubs(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fc, uint32_t fb)
{
	double a, c, b;
	memcpy(&a, &cpu->fpr[fa & 31u], 8);
	memcpy(&c, &cpu->fpr[fc & 31u], 8);
	memcpy(&b, &cpu->fpr[fb & 31u], 8);
	const float r = (float)(-(a * c - b));
	cpu->fpr[fd & 31u] = fpr_from_f32(r);
	nw_jit_fpscr_fprf(cpu, r);
}

void nw_jit_helper_mcrf(struct nw_jit_cpu *cpu, uint32_t crfd, uint32_t crfs)
{
	const int shs = 28 - 4 * (int)(crfs & 7u);
	const int shd = 28 - 4 * (int)(crfd & 7u);
	const uint32_t f = (cpu->cr >> shs) & 0xfu;
	cpu->cr = (cpu->cr & ~(0xfu << shd)) | (f << shd);
}

void nw_jit_helper_vadduwm(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb)
{
	vd &= 31u; va &= 31u; vb &= 31u;
	for (int w = 0; w < 4; w++)
		cpu->vr[vd][w] = cpu->vr[va][w] + cpu->vr[vb][w];
}

void nw_jit_helper_vaddubm(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb)
{
	vd &= 31u; va &= 31u; vb &= 31u;
	for (int w = 0; w < 4; w++) {
		const uint32_t a = cpu->vr[va][w];
		const uint32_t b = cpu->vr[vb][w];
		uint32_t r = 0;
		for (int i = 0; i < 4; i++) {
			const unsigned s = 24u - 8u * (unsigned)i;
			const uint8_t t = (uint8_t)(((a >> s) & 0xffu) + ((b >> s) & 0xffu));
			r |= (uint32_t)t << s;
		}
		cpu->vr[vd][w] = r;
	}
}

void nw_jit_helper_vsraw(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb)
{
	vd &= 31u; va &= 31u; vb &= 31u;
	for (int w = 0; w < 4; w++) {
		const int sh = (int)(cpu->vr[vb][w] & 31u);
		cpu->vr[vd][w] = (uint32_t)((int32_t)cpu->vr[va][w] >> sh);
	}
}

void nw_jit_helper_vsrw(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb)
{
	vd &= 31u; va &= 31u; vb &= 31u;
	for (int w = 0; w < 4; w++) {
		const unsigned sh = cpu->vr[vb][w] & 31u;
		cpu->vr[vd][w] = cpu->vr[va][w] >> sh;
	}
}

void nw_jit_helper_vspltisw(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t uimm)
{
	const int32_t s = (int32_t)(uimm & 31u) << 27 >> 27;
	vd &= 31u;
	cpu->vr[vd][0] = cpu->vr[vd][1] = cpu->vr[vd][2] = cpu->vr[vd][3] = (uint32_t)s;
}

void nw_jit_helper_vpkswss(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb)
{
	vd &= 31u; va &= 31u; vb &= 31u;
	uint16_t h[8];
	for (int i = 0; i < 4; i++) {
		int32_t x = (int32_t)cpu->vr[va][i];
		if (x > 32767)
			x = 32767;
		else if (x < -32768)
			x = -32768;
		h[i] = (uint16_t)(int16_t)x;
	}
	for (int i = 0; i < 4; i++) {
		int32_t x = (int32_t)cpu->vr[vb][i];
		if (x > 32767)
			x = 32767;
		else if (x < -32768)
			x = -32768;
		h[i + 4] = (uint16_t)(int16_t)x;
	}
	for (int w = 0; w < 4; w++)
		cpu->vr[vd][w] = ((uint32_t)h[2 * w] << 16) | h[2 * w + 1];
}

void nw_jit_helper_fneg(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fb)
{
	cpu->fpr[fd & 31u] = cpu->fpr[fb & 31u] ^ 0x8000000000000000ull;
}

void nw_jit_helper_fmr(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fb)
{
	cpu->fpr[fd & 31u] = cpu->fpr[fb & 31u];
}

void nw_jit_helper_mtfsf(struct nw_jit_cpu *cpu, uint32_t fm, uint32_t fb)
{
	uint32_t m = 0;
	if (fm & 0x01u) m |= 0x0000000fu;
	if (fm & 0x02u) m |= 0x000000f0u;
	if (fm & 0x04u) m |= 0x00000f00u;
	if (fm & 0x08u) m |= 0x0000f000u;
	if (fm & 0x10u) m |= 0x000f0000u;
	if (fm & 0x20u) m |= 0x00f00000u;
	if (fm & 0x40u) m |= 0x0f000000u;
	if (fm & 0x80u) m |= 0xf0000000u;
	if ((fm & 0x80u) == 0)
		m &= ~0x80000000u;	/* FX only if FM[0] */
	uint32_t w = (uint32_t)cpu->fpr[fb & 31u] & m;
	w &= ~0x60000000u;	/* FEX and VX not written */
	cpu->fpscr = (cpu->fpscr & ~m) | w;
}

static uint64_t bits_from_f64(double d)
{
	uint64_t j;
	memcpy(&j, &d, 8);
	return j;
}

static double f64_from_fpr(uint64_t j)
{
	double d;
	memcpy(&d, &j, 8);
	return d;
}

void nw_jit_helper_fcmpo(struct nw_jit_cpu *cpu, uint32_t crfd, uint32_t fa, uint32_t fb)
{
	const double a = f64_from_fpr(cpu->fpr[fa & 31u]);
	const double b = f64_from_fpr(cpu->fpr[fb & 31u]);
	uint32_t f;
	if (a != a || b != b)
		f = 1;
	else if (a < b)
		f = 8;
	else if (a > b)
		f = 4;
	else
		f = 2;
	const int sh = 28 - 4 * (int)(crfd & 7u);
	cpu->cr = (cpu->cr & ~(0xfu << sh)) | (f << sh);
	cpu->fpscr = (cpu->fpscr & ~0xf000u) | ((f & 0xfu) << 12);	/* FPCC */
}

void nw_jit_helper_fsub(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fb)
{
	const double r = f64_from_fpr(cpu->fpr[fa & 31u]) - f64_from_fpr(cpu->fpr[fb & 31u]);
	cpu->fpr[fd & 31u] = bits_from_f64(r);
	nw_jit_fpscr_fprf_d(cpu, r);
}

void nw_jit_helper_fadd(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fb)
{
	const double r = f64_from_fpr(cpu->fpr[fa & 31u]) + f64_from_fpr(cpu->fpr[fb & 31u]);
	cpu->fpr[fd & 31u] = bits_from_f64(r);
	nw_jit_fpscr_fprf_d(cpu, r);
}

void nw_jit_helper_fmul(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fc)
{
	const double r = f64_from_fpr(cpu->fpr[fa & 31u]) * f64_from_fpr(cpu->fpr[fc & 31u]);
	cpu->fpr[fd & 31u] = bits_from_f64(r);
	nw_jit_fpscr_fprf_d(cpu, r);
}

void nw_jit_helper_fmadd(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fc, uint32_t fb)
{
	const double r = f64_from_fpr(cpu->fpr[fa & 31u]) * f64_from_fpr(cpu->fpr[fc & 31u]) +
			 f64_from_fpr(cpu->fpr[fb & 31u]);
	cpu->fpr[fd & 31u] = bits_from_f64(r);
	nw_jit_fpscr_fprf_d(cpu, r);
}

void nw_jit_helper_fdiv(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fb)
{
	const double r = f64_from_fpr(cpu->fpr[fa & 31u]) / f64_from_fpr(cpu->fpr[fb & 31u]);
	cpu->fpr[fd & 31u] = bits_from_f64(r);
	nw_jit_fpscr_fprf_d(cpu, r);
}

void nw_jit_helper_frsp(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fb)
{
	const double b = f64_from_fpr(cpu->fpr[fb & 31u]);
	const float r = (float)b;
	cpu->fpr[fd & 31u] = fpr_from_f32(r);
	nw_jit_fpscr_fprf(cpu, r);
}

void nw_jit_helper_mffs(struct nw_jit_cpu *cpu, uint32_t fd)
{
	cpu->fpr[fd & 31u] = (uint64_t)cpu->fpscr;
}

void nw_jit_helper_fnmsub(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fc, uint32_t fb)
{
	const double r = -(f64_from_fpr(cpu->fpr[fa & 31u]) *
			   f64_from_fpr(cpu->fpr[fc & 31u]) -
			   f64_from_fpr(cpu->fpr[fb & 31u]));
	cpu->fpr[fd & 31u] = bits_from_f64(r);
	nw_jit_fpscr_fprf_d(cpu, r);
}

void nw_jit_helper_fctiwz(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fb)
{
	const double d = f64_from_fpr(cpu->fpr[fb & 31u]);
	int32_t i;
	if (d >= 2147483647.0)
		i = 0x7fffffff;
	else if (d <= -2147483648.0)
		i = (int32_t)0x80000000;
	else
		i = (int32_t)d;
	cpu->fpr[fd & 31u] = 0xfff8000000000000ull | (uint32_t)i;
}

void nw_jit_helper_lvsl(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t ea, int sl)
{
	uint8_t b[16];
	int j = sl ? (int)(ea & 15u) : (16 - (int)(ea & 15u));
	for (int i = 0; i < 16; i++)
		b[i] = (uint8_t)(j + i);
	vd &= 31u;
	for (int w = 0; w < 4; w++)
		cpu->vr[vd][w] = ((uint32_t)b[w * 4] << 24) | ((uint32_t)b[w * 4 + 1] << 16) |
				 ((uint32_t)b[w * 4 + 2] << 8) | (uint32_t)b[w * 4 + 3];
}

void nw_jit_helper_vor(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb)
{
	vd &= 31u; va &= 31u; vb &= 31u;
	for (int i = 0; i < 4; i++)
		cpu->vr[vd][i] = cpu->vr[va][i] | cpu->vr[vb][i];
}

void nw_jit_helper_vand(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb)
{
	vd &= 31u; va &= 31u; vb &= 31u;
	for (int i = 0; i < 4; i++)
		cpu->vr[vd][i] = cpu->vr[va][i] & cpu->vr[vb][i];
}

void nw_jit_helper_vandc(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb)
{
	vd &= 31u; va &= 31u; vb &= 31u;
	for (int i = 0; i < 4; i++)
		cpu->vr[vd][i] = cpu->vr[va][i] & ~cpu->vr[vb][i];
}

void nw_jit_helper_vxor(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb)
{
	vd &= 31u; va &= 31u; vb &= 31u;
	for (int i = 0; i < 4; i++)
		cpu->vr[vd][i] = cpu->vr[va][i] ^ cpu->vr[vb][i];
}

void nw_jit_helper_vsububm(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb)
{
	vd &= 31u; va &= 31u; vb &= 31u;
	for (int w = 0; w < 4; w++) {
		const uint32_t a = cpu->vr[va][w];
		const uint32_t b = cpu->vr[vb][w];
		uint32_t r = 0;
		for (int i = 0; i < 4; i++) {
			const unsigned s = 24u - 8u * (unsigned)i;
			const uint8_t t = (uint8_t)(((a >> s) & 0xffu) - ((b >> s) & 0xffu));
			r |= (uint32_t)t << s;
		}
		cpu->vr[vd][w] = r;
	}
}

void nw_jit_helper_vslh(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb)
{
	vd &= 31u; va &= 31u; vb &= 31u;
	for (int w = 0; w < 4; w++) {
		const uint32_t a = cpu->vr[va][w];
		const uint32_t b = cpu->vr[vb][w];
		uint32_t r = 0;
		for (int i = 0; i < 2; i++) {
			const unsigned s = 16u - 16u * (unsigned)i;
			const uint16_t sh = (uint16_t)((b >> s) & 15u);
			const uint16_t t = (uint16_t)(((a >> s) & 0xffffu) << sh);
			r |= (uint32_t)t << s;
		}
		cpu->vr[vd][w] = r;
	}
}

static void record_cr6_cmp(struct nw_jit_cpu *cpu, int all1, int all0, uint32_t rc)
{
	if (!rc)
		return;
	const uint32_t f = all1 ? 8u : (all0 ? 2u : 0u);
	cpu->cr = (cpu->cr & ~0xf0u) | (f << 4);
}

/* vcmpequw / vcmpequw.: 4 word EQ → 0xffffffff/0. Rc writes CR6:
 * all-true=8 (LT), all-false=2 (EQ), mixed=0. kpx C1=1. */
void nw_jit_helper_vcmpequw(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb, uint32_t rc)
{
	vd &= 31u; va &= 31u; vb &= 31u;
	int all1 = 1, all0 = 1;
	for (int i = 0; i < 4; i++) {
		const uint32_t d = (cpu->vr[va][i] == cpu->vr[vb][i]) ? 0xffffffffu : 0;
		cpu->vr[vd][i] = d;
		if (d != 0xffffffffu)
			all1 = 0;
		if (d != 0)
			all0 = 0;
	}
	record_cr6_cmp(cpu, all1, all0, rc);
}

void nw_jit_helper_vcmpequb(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb, uint32_t rc)
{
	vd &= 31u; va &= 31u; vb &= 31u;
	int all1 = 1, all0 = 1;
	for (int w = 0; w < 4; w++) {
		const uint32_t a = cpu->vr[va][w], b = cpu->vr[vb][w];
		uint32_t r = 0;
		for (int i = 0; i < 4; i++) {
			const unsigned s = 24u - 8u * (unsigned)i;
			const uint8_t ea = (uint8_t)(a >> s);
			const uint8_t eb = (uint8_t)(b >> s);
			const uint8_t d = (ea == eb) ? 0xffu : 0;
			r |= (uint32_t)d << s;
			if (d != 0xffu)
				all1 = 0;
			if (d != 0)
				all0 = 0;
		}
		cpu->vr[vd][w] = r;
	}
	record_cr6_cmp(cpu, all1, all0, rc);
}

void nw_jit_helper_vminsb(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb)
{
	vd &= 31u; va &= 31u; vb &= 31u;
	for (int w = 0; w < 4; w++) {
		const uint32_t a = cpu->vr[va][w], b = cpu->vr[vb][w];
		uint32_t r = 0;
		for (int i = 0; i < 4; i++) {
			const unsigned s = 24u - 8u * (unsigned)i;
			const int8_t ea = (int8_t)(a >> s);
			const int8_t eb = (int8_t)(b >> s);
			const int8_t d = ea < eb ? ea : eb;
			r |= (uint32_t)(uint8_t)d << s;
		}
		cpu->vr[vd][w] = r;
	}
}

void nw_jit_helper_vsro(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb)
{
	uint8_t a[16], d[16];
	vd &= 31u; va &= 31u; vb &= 31u;
	const unsigned sh = (cpu->vr[vb][3] >> 3) & 15u;
	for (int w = 0; w < 4; w++) {
		const uint32_t aw = cpu->vr[va][w];
		a[w * 4] = (uint8_t)(aw >> 24);
		a[w * 4 + 1] = (uint8_t)(aw >> 16);
		a[w * 4 + 2] = (uint8_t)(aw >> 8);
		a[w * 4 + 3] = (uint8_t)aw;
	}
	for (int i = 0; i < 16; i++)
		d[i] = (i < (int)sh) ? 0 : a[i - (int)sh];
	for (int w = 0; w < 4; w++)
		cpu->vr[vd][w] = ((uint32_t)d[w * 4] << 24) | ((uint32_t)d[w * 4 + 1] << 16) |
				 ((uint32_t)d[w * 4 + 2] << 8) | (uint32_t)d[w * 4 + 3];
}

void nw_jit_helper_vslo(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb)
{
	uint8_t a[16], d[16];
	vd &= 31u; va &= 31u; vb &= 31u;
	const unsigned sh = (cpu->vr[vb][3] >> 3) & 15u;
	for (int w = 0; w < 4; w++) {
		const uint32_t aw = cpu->vr[va][w];
		a[w * 4] = (uint8_t)(aw >> 24);
		a[w * 4 + 1] = (uint8_t)(aw >> 16);
		a[w * 4 + 2] = (uint8_t)(aw >> 8);
		a[w * 4 + 3] = (uint8_t)aw;
	}
	for (int i = 0; i < 16; i++)
		d[i] = ((i + (int)sh) < 16) ? a[i + (int)sh] : 0;
	for (int w = 0; w < 4; w++)
		cpu->vr[vd][w] = ((uint32_t)d[w * 4] << 24) | ((uint32_t)d[w * 4 + 1] << 16) |
				 ((uint32_t)d[w * 4 + 2] << 8) | (uint32_t)d[w * 4 + 3];
}

void nw_jit_helper_vsr(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb)
{
	vd &= 31u; va &= 31u; vb &= 31u;
	const int sh = (int)(cpu->vr[vb][3] & 7u);
	if (sh == 0) {
		for (int i = 0; i < 4; i++)
			cpu->vr[vd][i] = cpu->vr[va][i];
		return;
	}
	uint32_t prev = 0;
	for (int i = 0; i < 4; i++) {
		const uint32_t w = cpu->vr[va][i];
		const uint32_t next = w << (32 - sh);
		cpu->vr[vd][i] = (w >> sh) | prev;
		prev = next;
	}
}

void nw_jit_helper_vsl(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb)
{
	vd &= 31u; va &= 31u; vb &= 31u;
	const int sh = (int)(cpu->vr[vb][3] & 7u);
	if (sh == 0) {
		for (int i = 0; i < 4; i++)
			cpu->vr[vd][i] = cpu->vr[va][i];
		return;
	}
	uint32_t prev = 0;
	for (int i = 3; i >= 0; i--) {
		const uint32_t w = cpu->vr[va][i];
		const uint32_t next = w >> (32 - sh);
		cpu->vr[vd][i] = (w << sh) | prev;
		prev = next;
	}
}

void nw_jit_helper_vspltisb(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t uimm)
{
	uint32_t v = uimm & 31u;
	if (v & 0x10u)
		v -= 0x20u;
	const uint32_t b = v & 0xffu;
	const uint32_t w = b * 0x01010101u;
	vd &= 31u;
	cpu->vr[vd][0] = cpu->vr[vd][1] = cpu->vr[vd][2] = cpu->vr[vd][3] = w;
}

void nw_jit_helper_mtvscr(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb)
{
	(void)vd;
	(void)va;
	cpu->vscr = cpu->vr[vb & 31u][3];
}

void nw_jit_helper_mfvscr(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb)
{
	(void)va;
	(void)vb;
	vd &= 31u;
	cpu->vr[vd][0] = 0;
	cpu->vr[vd][1] = 0;
	cpu->vr[vd][2] = 0;
	cpu->vr[vd][3] = cpu->vscr;
}

void nw_jit_helper_lwbrx(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t ea)
{
	const uint32_t v = nw_jit_helper_lwz(cpu, ea);
	if (cpu->fault)
		return;
	cpu->gpr[rd & 31u] = (v << 24) | ((v << 8) & 0xff0000u) |
			     ((v >> 8) & 0xff00u) | (v >> 24);
}

void nw_jit_helper_vsrb(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb)
{
	vd &= 31u; va &= 31u; vb &= 31u;
	for (int w = 0; w < 4; w++) {
		const uint32_t a = cpu->vr[va][w];
		const uint32_t b = cpu->vr[vb][w];
		uint32_t r = 0;
		for (int i = 0; i < 4; i++) {
			const unsigned s = 24u - 8u * (unsigned)i;
			const uint8_t sh = (uint8_t)((b >> s) & 7u);
			const uint8_t t = (uint8_t)(((a >> s) & 0xffu) >> sh);
			r |= (uint32_t)t << s;
		}
		cpu->vr[vd][w] = r;
	}
}

void nw_jit_helper_vslb(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb)
{
	vd &= 31u; va &= 31u; vb &= 31u;
	for (int w = 0; w < 4; w++) {
		const uint32_t a = cpu->vr[va][w];
		const uint32_t b = cpu->vr[vb][w];
		uint32_t r = 0;
		for (int i = 0; i < 4; i++) {
			const unsigned s = 24u - 8u * (unsigned)i;
			const uint8_t sh = (uint8_t)((b >> s) & 7u);
			const uint8_t t = (uint8_t)(((a >> s) & 0xffu) << sh);
			r |= (uint32_t)t << s;
		}
		cpu->vr[vd][w] = r;
	}
}

void nw_jit_helper_fabs(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fb)
{
	cpu->fpr[fd & 31u] = cpu->fpr[fb & 31u] & 0x7fffffffffffffffull;
}

/* Classify the single-precision value already stored in fpr[fd]. */
void nw_jit_fprf_fd(struct nw_jit_cpu *cpu, uint32_t fd)
{
	nw_jit_fpscr_fprf(cpu, f32_from_fpr(cpu->fpr[fd & 31u]));
}

void nw_jit_helper_vsel(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb, uint32_t vc)
{
	vd &= 31u; va &= 31u; vb &= 31u; vc &= 31u;
	for (int i = 0; i < 4; i++)
		cpu->vr[vd][i] = (cpu->vr[va][i] & ~cpu->vr[vc][i]) |
				 (cpu->vr[vb][i] & cpu->vr[vc][i]);
}

void nw_jit_helper_vperm(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb, uint32_t vc)
{
	uint8_t src[32], c[16], d[16];
	vd &= 31u; va &= 31u; vb &= 31u; vc &= 31u;
	for (int w = 0; w < 4; w++) {
		const uint32_t a = cpu->vr[va][w], b = cpu->vr[vb][w];
		src[w * 4] = (uint8_t)(a >> 24);
		src[w * 4 + 1] = (uint8_t)(a >> 16);
		src[w * 4 + 2] = (uint8_t)(a >> 8);
		src[w * 4 + 3] = (uint8_t)a;
		src[16 + w * 4] = (uint8_t)(b >> 24);
		src[16 + w * 4 + 1] = (uint8_t)(b >> 16);
		src[16 + w * 4 + 2] = (uint8_t)(b >> 8);
		src[16 + w * 4 + 3] = (uint8_t)b;
		const uint32_t cv = cpu->vr[vc][w];
		c[w * 4] = (uint8_t)(cv >> 24);
		c[w * 4 + 1] = (uint8_t)(cv >> 16);
		c[w * 4 + 2] = (uint8_t)(cv >> 8);
		c[w * 4 + 3] = (uint8_t)cv;
	}
	for (int i = 0; i < 16; i++)
		d[i] = src[c[i] & 31];
	for (int w = 0; w < 4; w++)
		cpu->vr[vd][w] = ((uint32_t)d[w * 4] << 24) | ((uint32_t)d[w * 4 + 1] << 16) |
				 ((uint32_t)d[w * 4 + 2] << 8) | (uint32_t)d[w * 4 + 3];
}

void nw_jit_helper_vsldoi(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb, uint32_t shb)
{
	uint8_t src[32], d[16];
	vd &= 31u; va &= 31u; vb &= 31u;
	shb &= 15u;
	for (int w = 0; w < 4; w++) {
		const uint32_t a = cpu->vr[va][w], b = cpu->vr[vb][w];
		src[w * 4] = (uint8_t)(a >> 24);
		src[w * 4 + 1] = (uint8_t)(a >> 16);
		src[w * 4 + 2] = (uint8_t)(a >> 8);
		src[w * 4 + 3] = (uint8_t)a;
		src[16 + w * 4] = (uint8_t)(b >> 24);
		src[16 + w * 4 + 1] = (uint8_t)(b >> 16);
		src[16 + w * 4 + 2] = (uint8_t)(b >> 8);
		src[16 + w * 4 + 3] = (uint8_t)b;
	}
	for (int i = 0; i < 16; i++)
		d[i] = src[i + (int)shb];
	for (int w = 0; w < 4; w++)
		cpu->vr[vd][w] = ((uint32_t)d[w * 4] << 24) | ((uint32_t)d[w * 4 + 1] << 16) |
				 ((uint32_t)d[w * 4 + 2] << 8) | (uint32_t)d[w * 4 + 3];
}

void nw_jit_helper_vspltish(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t uimm)
{
	const int16_t s = (int16_t)((int32_t)(uimm & 31u) << 27 >> 27);
	const uint32_t hw = (uint16_t)s;
	const uint32_t w = (hw << 16) | hw;
	vd &= 31u;
	cpu->vr[vd][0] = cpu->vr[vd][1] = cpu->vr[vd][2] = cpu->vr[vd][3] = w;
}

void nw_jit_helper_vspltw(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t uimm, uint32_t vb)
{
	vd &= 31u;
	vb &= 31u;
	const uint32_t w = cpu->vr[vb][uimm & 3u];
	cpu->vr[vd][0] = cpu->vr[vd][1] = cpu->vr[vd][2] = cpu->vr[vd][3] = w;
}

void nw_jit_helper_vspltb(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t uimm, uint32_t vb)
{
	vd &= 31u;
	vb &= 31u;
	const unsigned idx = uimm & 15u;
	const unsigned w = idx >> 2;
	const unsigned b = idx & 3u;
	const uint8_t t = (uint8_t)(cpu->vr[vb][w] >> (24u - 8u * b));
	const uint32_t word = (uint32_t)t * 0x01010101u;
	cpu->vr[vd][0] = cpu->vr[vd][1] = cpu->vr[vd][2] = cpu->vr[vd][3] = word;
}

void nw_jit_helper_vmrghb(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb)
{
	uint8_t a[16], b[16], d[16];
	vd &= 31u; va &= 31u; vb &= 31u;
	for (int w = 0; w < 4; w++) {
		const uint32_t aw = cpu->vr[va][w], bw = cpu->vr[vb][w];
		a[w * 4] = (uint8_t)(aw >> 24);
		a[w * 4 + 1] = (uint8_t)(aw >> 16);
		a[w * 4 + 2] = (uint8_t)(aw >> 8);
		a[w * 4 + 3] = (uint8_t)aw;
		b[w * 4] = (uint8_t)(bw >> 24);
		b[w * 4 + 1] = (uint8_t)(bw >> 16);
		b[w * 4 + 2] = (uint8_t)(bw >> 8);
		b[w * 4 + 3] = (uint8_t)bw;
	}
	for (int i = 0; i < 8; i++) {
		d[2 * i] = a[i];
		d[2 * i + 1] = b[i];
	}
	for (int w = 0; w < 4; w++)
		cpu->vr[vd][w] = ((uint32_t)d[w * 4] << 24) | ((uint32_t)d[w * 4 + 1] << 16) |
				 ((uint32_t)d[w * 4 + 2] << 8) | (uint32_t)d[w * 4 + 3];
}

void nw_jit_helper_vmrglb(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb)
{
	uint8_t a[16], b[16], d[16];
	vd &= 31u; va &= 31u; vb &= 31u;
	for (int w = 0; w < 4; w++) {
		const uint32_t aw = cpu->vr[va][w], bw = cpu->vr[vb][w];
		a[w * 4] = (uint8_t)(aw >> 24);
		a[w * 4 + 1] = (uint8_t)(aw >> 16);
		a[w * 4 + 2] = (uint8_t)(aw >> 8);
		a[w * 4 + 3] = (uint8_t)aw;
		b[w * 4] = (uint8_t)(bw >> 24);
		b[w * 4 + 1] = (uint8_t)(bw >> 16);
		b[w * 4 + 2] = (uint8_t)(bw >> 8);
		b[w * 4 + 3] = (uint8_t)bw;
	}
	for (int i = 0; i < 8; i++) {
		d[2 * i] = a[i + 8];
		d[2 * i + 1] = b[i + 8];
	}
	for (int w = 0; w < 4; w++)
		cpu->vr[vd][w] = ((uint32_t)d[w * 4] << 24) | ((uint32_t)d[w * 4 + 1] << 16) |
				 ((uint32_t)d[w * 4 + 2] << 8) | (uint32_t)d[w * 4 + 3];
}

void nw_jit_helper_vmrghw(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb)
{
	vd &= 31u; va &= 31u; vb &= 31u;
	const uint32_t a0 = cpu->vr[va][0], a1 = cpu->vr[va][1];
	const uint32_t b0 = cpu->vr[vb][0], b1 = cpu->vr[vb][1];
	cpu->vr[vd][0] = a0;
	cpu->vr[vd][1] = b0;
	cpu->vr[vd][2] = a1;
	cpu->vr[vd][3] = b1;
}

void nw_jit_helper_vmrglw(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb)
{
	vd &= 31u; va &= 31u; vb &= 31u;
	const uint32_t a2 = cpu->vr[va][2], a3 = cpu->vr[va][3];
	const uint32_t b2 = cpu->vr[vb][2], b3 = cpu->vr[vb][3];
	cpu->vr[vd][0] = a2;
	cpu->vr[vd][1] = b2;
	cpu->vr[vd][2] = a3;
	cpu->vr[vd][3] = b3;
}

void nw_jit_helper_vsumsws(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb)
{
	vd &= 31u; va &= 31u; vb &= 31u;
	int64_t s = (int32_t)cpu->vr[vb][3];
	for (int i = 0; i < 4; i++)
		s += (int32_t)cpu->vr[va][i];
	if (s > 2147483647ll)
		s = 2147483647ll;
	else if (s < -2147483648ll)
		s = -2147483648ll;
	cpu->vr[vd][0] = cpu->vr[vd][1] = cpu->vr[vd][2] = 0;
	cpu->vr[vd][3] = (uint32_t)(int32_t)s;
}

void nw_jit_helper_vmx(struct nw_jit_cpu *cpu, uint32_t op)
{
	if (g_host_vmx && cpu->host)
		g_host_vmx(cpu->host, op, cpu);
}

void nw_jit_helper_rfi(struct nw_jit_cpu *cpu)
{
	if (g_host_rfi && cpu->host)
		g_host_rfi(cpu->host, cpu);
	else
		cpu->pc += 4;
}

void nw_jit_helper_icbi(struct nw_jit_cpu *cpu, uint32_t ea)
{
	if (g_host_icbi && cpu->host)
		g_host_icbi(cpu->host, ea);
}

void nw_jit_helper_tlbie(struct nw_jit_cpu *cpu, uint32_t ea)
{
	if (g_host_tlbie && cpu->host)
		g_host_tlbie(cpu->host, ea);
	else
		nw_jit_dtlb_drop_page(ea, NW_JIT_DTLB_FL_TLB);
}

void nw_jit_helper_lwarx(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t ea)
{
	rd &= 31u;
	if (g_host_lwarx && cpu->host) {
		int fault = 0;
		const uint32_t v = g_host_lwarx(cpu->host, ea, cpu->pc, &fault);
		cpu->fault = (uint32_t)fault;
		if (!fault) {
			cpu->gpr[rd] = v;
			cpu->reserve_valid = 1;
			cpu->reserve_ea = ea;
		}
		return;
	}
	if (g_host_lwz && cpu->host) {
		int fault = 0;
		const uint32_t v = g_host_lwz(cpu->host, ea, cpu->pc, &fault);
		cpu->fault = (uint32_t)fault;
		if (!fault) {
			cpu->gpr[rd] = v;
			cpu->reserve_valid = 1;
			cpu->reserve_ea = ea;
		}
		return;
	}
	if (!cpu->mem || ea < cpu->mem_base ||
	    (ea - cpu->mem_base) + 4u > cpu->mem_size) {
		cpu->fault = 1;
		cpu->fault_ea = ea;
		return;
	}
	const uint8_t *p = cpu->mem + (ea - cpu->mem_base);
	cpu->gpr[rd] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
		       ((uint32_t)p[2] << 8) | (uint32_t)p[3];
	cpu->reserve_valid = 1;
	cpu->reserve_ea = ea;
}

void nw_jit_helper_stwcx(struct nw_jit_cpu *cpu, uint32_t rs, uint32_t ea)
{
	uint32_t cr0 = (cpu->xer >> 31) & 1u;	/* SO */
	if (g_host_stwcx && cpu->host) {
		int fault = 0;
		const int eq = g_host_stwcx(cpu->host, ea, cpu->gpr[rs & 31u], cpu->pc, &fault);
		cpu->fault = (uint32_t)fault;
		if (!fault && eq)
			cr0 |= 2u;
		cpu->reserve_valid = 0;
		cpu->cr = (cpu->cr & 0x0fffffffu) | (cr0 << 28);
		return;
	}
	if (cpu->reserve_valid && cpu->reserve_ea == ea) {
		if (g_host_stw && cpu->host) {
			int fault = 0;
			g_host_stw(cpu->host, ea, cpu->gpr[rs & 31u], cpu->pc, &fault);
			cpu->fault = (uint32_t)fault;
			if (!fault)
				cr0 |= 2u;	/* EQ */
		} else if (cpu->mem && ea >= cpu->mem_base &&
			   (ea - cpu->mem_base) + 4u <= cpu->mem_size) {
			uint8_t *p = cpu->mem + (ea - cpu->mem_base);
			const uint32_t v = cpu->gpr[rs & 31u];
			p[0] = (uint8_t)(v >> 24);
			p[1] = (uint8_t)(v >> 16);
			p[2] = (uint8_t)(v >> 8);
			p[3] = (uint8_t)v;
			cr0 |= 2u;
		} else {
			cpu->fault = 1;
			cpu->fault_ea = ea;
			cpu->fault_st = 1;
		}
	}
	cpu->reserve_valid = 0;
	cpu->cr = (cpu->cr & 0x0fffffffu) | (cr0 << 28);
}

void nw_jit_helper_lswx(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t ea, uint32_t nb)
{
	int r = (int)(rd & 31u);
	unsigned n = nb & 0x7fu;
	unsigned i = 0;
	while (n - i >= 4u) {
		uint32_t v = 0;
		for (int b = 0; b < 4; b++) {
			v = (v << 8) | (nw_jit_helper_lb(cpu, ea + i + (unsigned)b) & 0xffu);
			if (cpu->fault)
				return;
		}
		cpu->gpr[r] = v;
		i += 4;
		r = (r + 1) & 31;
	}
	if (n > i) {
		uint32_t v = 0;
		const unsigned left = n - i;
		for (unsigned b = 0; b < left; b++) {
			v |= (nw_jit_helper_lb(cpu, ea + i + b) & 0xffu) << (24 - 8 * (int)b);
			if (cpu->fault)
				return;
		}
		cpu->gpr[r] = v;
	}
}

void nw_jit_helper_stswx(struct nw_jit_cpu *cpu, uint32_t rs, uint32_t ea, uint32_t nb)
{
	int r = (int)(rs & 31u);
	int sh = 24;
	const unsigned n = nb & 0x7fu;
	for (unsigned i = 0; i < n; i++) {
		nw_jit_helper_stb(cpu, ea + i, cpu->gpr[r] >> sh);
		if (cpu->fault)
			return;
		sh -= 8;
		if (sh < 0) {
			sh = 24;
			r = (r + 1) & 31;
		}
	}
}

void nw_jit_helper_vmsumshm(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb, uint32_t vc, int sat)
{
	vd &= 31u; va &= 31u; vb &= 31u; vc &= 31u;
	for (int w = 0; w < 4; w++) {
		const int16_t a0 = (int16_t)(cpu->vr[va][w] >> 16);
		const int16_t a1 = (int16_t)cpu->vr[va][w];
		const int16_t b0 = (int16_t)(cpu->vr[vb][w] >> 16);
		const int16_t b1 = (int16_t)cpu->vr[vb][w];
		int64_t s = (int64_t)a0 * (int64_t)b0 + (int64_t)a1 * (int64_t)b1 +
			    (int32_t)cpu->vr[vc][w];
		if (sat) {
			if (s > 2147483647ll)
				s = 2147483647ll;
			else if (s < -2147483648ll)
				s = -2147483648ll;
		}
		cpu->vr[vd][w] = (uint32_t)(int32_t)s;
	}
}

void nw_jit_helper_vmladduhm(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb, uint32_t vc)
{
	vd &= 31u; va &= 31u; vb &= 31u; vc &= 31u;
	for (int w = 0; w < 4; w++) {
		const uint32_t a = cpu->vr[va][w];
		const uint32_t b = cpu->vr[vb][w];
		const uint32_t c = cpu->vr[vc][w];
		const uint32_t d0 = ((a >> 16) * (b >> 16) + (c >> 16)) & 0xffffu;
		const uint32_t d1 = ((a & 0xffffu) * (b & 0xffffu) + (c & 0xffffu)) & 0xffffu;
		cpu->vr[vd][w] = (d0 << 16) | d1;
	}
}

void nw_jit_helper_vsubshs(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t va, uint32_t vb)
{
	vd &= 31u; va &= 31u; vb &= 31u;
	int sat = 0;
	for (int w = 0; w < 4; w++) {
		const uint32_t a = cpu->vr[va][w];
		const uint32_t b = cpu->vr[vb][w];
		int32_t d0 = (int32_t)(int16_t)(a >> 16) - (int32_t)(int16_t)(b >> 16);
		int32_t d1 = (int32_t)(int16_t)a - (int32_t)(int16_t)b;
		if (d0 > 32767) { d0 = 32767; sat = 1; }
		else if (d0 < -32768) { d0 = -32768; sat = 1; }
		if (d1 > 32767) { d1 = 32767; sat = 1; }
		else if (d1 < -32768) { d1 = -32768; sat = 1; }
		cpu->vr[vd][w] = ((uint32_t)(uint16_t)d0 << 16) | (uint16_t)d1;
	}
	if (sat)
		cpu->vscr |= 1u;
}

void nw_jit_helper_bc(struct nw_jit_cpu *cpu, uint32_t op, uint32_t pc)
{
	const int bo = (int)((op >> 21) & 0x1f);
	const int bi = (int)((op >> 16) & 0x1f);
	const int32_t disp = (int16_t)(op & 0xfffcu);
	const int aa = (int)((op >> 1) & 1);
	const int lk = (int)(op & 1);
	int cond_ok = 1, ctr_ok = 1;
	if ((bo & 0x10) == 0) {
		const int crbit = (int)((cpu->cr >> (31 - bi)) & 1);
		cond_ok = (bo & 0x08) ? crbit : !crbit;
	}
	if ((bo & 0x04) == 0) {
		cpu->ctr -= 1u;
		ctr_ok = (cpu->ctr == 0);
		if ((bo & 0x02) == 0)
			ctr_ok = !ctr_ok;
	}
	if (lk)
		cpu->lr = pc + 4;
	if (cond_ok && ctr_ok)
		cpu->pc = ((aa ? 0u : pc) + (uint32_t)disp) & ~3u;
	else
		cpu->pc = pc + 4;
}

static int page_bit_index(uint32_t phys_page, uint8_t **bits, unsigned *idx)
{
	phys_page &= ~0xfffu;
	if (g_ram_size != 0 && phys_page >= g_ram_base &&
	    phys_page - g_ram_base < g_ram_size) {
		const unsigned i = (phys_page - g_ram_base) >> 12;
		if (i < (unsigned)NW_JIT_RAM_PAGES) {
			*bits = g_pagebit_ram;
			*idx = i;
			return 1;
		}
		return 0;
	}
	if (g_rom_size != 0 && phys_page >= g_rom_base &&
	    phys_page - g_rom_base < g_rom_size) {
		const unsigned i = (phys_page - g_rom_base) >> 12;
		if (i < (unsigned)NW_JIT_ROM_PAGES) {
			*bits = g_pagebit_rom;
			*idx = i;
			return 1;
		}
		return 0;
	}
	return 0;
}

static void pagebit_set(uint32_t phys_page)
{
	uint8_t *bits;
	unsigned i;
	if (!page_bit_index(phys_page, &bits, &i))
		return;
	bits[i >> 3] |= (uint8_t)(1u << (i & 7u));
}

static void pagebit_clear(uint32_t phys_page)
{
	uint8_t *bits;
	unsigned i;
	if (!page_bit_index(phys_page, &bits, &i))
		return;
	bits[i >> 3] &= (uint8_t)~(1u << (i & 7u));
}

static int page_may_have_code(uint32_t phys_page)
{
	switch (nw_pa_kind(phys_page)) {
	case NW_PA_FB:
	case NW_PA_IO:
		return 0;
	default:
		break;
	}
	uint8_t *bits;
	unsigned i;
	if (!page_bit_index(phys_page, &bits, &i))
		return g_ram_size == 0 && g_rom_size == 0;
	return bits[i >> 3] & (uint8_t)(1u << (i & 7u));
}

void nw_jit_set_code_pages(uint32_t ram_base, uint32_t ram_size,
			   uint32_t rom_base, uint32_t rom_size)
{
	g_ram_base = ram_base & ~0xfffu;
	g_ram_size = ram_size;
	g_rom_base = rom_base & ~0xfffu;
	g_rom_size = rom_size;
	memset(g_pagebit_ram, 0, sizeof(g_pagebit_ram));
	memset(g_pagebit_rom, 0, sizeof(g_pagebit_rom));
}

static int code_ready(void)
{
	if (g_code)
		return 1;
	g_code = (uint8_t *)mmap(NULL, NW_JIT_CODE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
				 MAP_ANON | MAP_PRIVATE | MAP_JIT, -1, 0);
	if (g_code == MAP_FAILED) {
		g_code = NULL;
		return 0;
	}
	g_code_used = 0;
	return 1;
}

void nw_jit_reset(void)
{
	memset(g_cache, 0, sizeof(g_cache));
	memset(g_pagebit_ram, 0, sizeof(g_pagebit_ram));
	memset(g_pagebit_rom, 0, sizeof(g_pagebit_rom));
	g_code_used = 0;
	g_flush = 0;
	g_compiles = 0;
	g_evict = 0;
	g_recompile_n = 0;
	g_code_emitted = 0;
	g_compiles_at_wrap = 0;
	g_wraps = 0;
#ifdef __APPLE__
	g_wx_ns = 0;
	g_icache_ns = 0;
	g_wx_n = 0;
#endif
	g_occ_max = 0;
	memset(g_flush_src, 0, sizeof(g_flush_src));
	memset(g_flush_calls, 0, sizeof(g_flush_calls));
	g_exec_blocks = 0;
	g_exec_insns = 0;
	g_v_cmp = g_v_miss = g_v_fail = g_v_skip_unsup = g_v_skip_mem = 0;
	g_v_skip_dsi = g_v_skip_io = g_v_other = g_v_other_miss = 0;
	memset(g_v_blocks, 0, sizeof(g_v_blocks));
	for (size_t i = 0; i < sizeof(g_hist) / sizeof(g_hist[0]); i++)
		g_hist[i].n = g_hist[i].miss = g_hist[i].insns = 0;
	memset(g_pchot, 0, sizeof(g_pchot));
	memset(g_skip, 0, sizeof(g_skip));
	memset(g_cut, 0, sizeof(g_cut));
	memset(g_hop_stop, 0, sizeof(g_hop_stop));
	g_skip_raw_seen = 0;
	g_skip_raw_op = 0;
	g_skip_raw_pc = 0;
	g_skip_raw_nm[0] = 0;
	g_codec_insns = g_other_insns = 0;
	g_kcall_fast = 0;
	g_chain_hops = 0;
	g_tail_hops = g_tail_n = g_tail_dsi_n = g_tail_fpr = g_tail_vr = 0;
	g_tail_dsi_pc = 0;
	g_codec_insns_tick = g_other_insns_tick = 0;
	memset(g_dtlb_h, 0, sizeof(g_dtlb_h));
	memset(g_io_h, 0, sizeof(g_io_h));
	memset(g_sr_gen, 0, sizeof(g_sr_gen));
	g_mtsr_total = g_mtsr_vsid = 0;
	g_bat_gen = 0;
	g_bat_total = g_bat_bumps = 0;
	nw_jit_dtlb_flush_src(NW_JIT_DTLB_FL_RESET);
	memset(g_dtlb_fl, 0, sizeof(g_dtlb_fl));
	g_dtlb_hit = g_dtlb_miss = 0;
	memset(g_dtlb_why, 0, sizeof(g_dtlb_why));
	g_itlb_hit = g_itlb_miss = 0;
}

void nw_jit_invalidate_page_src(uint32_t phys_page, int src)
{
	phys_page &= ~0xfffu;
	if (src < 0 || src >= NW_JIT_FL_N)
		src = NW_JIT_FL_OTHER;
	if (!page_may_have_code(phys_page))
		return;
	g_flush_calls[src]++;
	for (int i = 0; i < NW_JIT_CACHE; i++) {
		if (g_cache[i].used == NW_JIT_USED_LIVE && g_cache[i].phys_page == phys_page) {
			g_cache[i].used = NW_JIT_USED_TOMB;
			g_flush++;
			g_flush_src[src]++;
		}
	}
	pagebit_clear(phys_page);
}

void nw_jit_invalidate_page(uint32_t phys_page)
{
	nw_jit_invalidate_page_src(phys_page, NW_JIT_FL_OTHER);
}

void nw_jit_invalidate_range_src(uint32_t pa, uint32_t nbytes, int src)
{
	if (nbytes == 0)
		return;
	uint32_t a = pa & ~0xfffu;
	const uint32_t last = (pa + nbytes - 1u) & ~0xfffu;
	for (;;) {
		nw_jit_invalidate_page_src(a, src);
		if (a == last)
			break;
		a += 0x1000u;
	}
}

void nw_jit_invalidate_all_src(int src)
{
	if (src < 0 || src >= NW_JIT_FL_N)
		src = NW_JIT_FL_OTHER;
	g_flush_calls[src]++;
	for (int i = 0; i < NW_JIT_CACHE; i++) {
		if (g_cache[i].used == NW_JIT_USED_LIVE) {
			g_cache[i].used = 0;
			g_flush++;
			g_flush_src[src]++;
		}
	}
	memset(g_pagebit_ram, 0, sizeof(g_pagebit_ram));
	memset(g_pagebit_rom, 0, sizeof(g_pagebit_rom));
}

void nw_jit_invalidate_all(void)
{
	nw_jit_invalidate_all_src(NW_JIT_FL_OTHER);
}

static int code_bank(nw_jit_fn fn)
{
	if (!g_code || !fn || fn == NW_JIT_INTERPRET)
		return -1;
	ptrdiff_t off = (uint8_t *)fn - g_code;
	if (off < 0 || (size_t)off >= NW_JIT_CODE_SIZE)
		return -1;
	return (int)((size_t)off / NW_JIT_BANK_SIZE);
}

static void pagebit_rebuild(void)
{
	memset(g_pagebit_ram, 0, sizeof(g_pagebit_ram));
	memset(g_pagebit_rom, 0, sizeof(g_pagebit_rom));
	for (int i = 0; i < NW_JIT_CACHE; i++) {
		if (g_cache[i].used == NW_JIT_USED_LIVE)
			pagebit_set(g_cache[i].phys_page);
	}
}

static void invalidate_bank(int bank)
{
	g_flush_calls[NW_JIT_FL_WRAP]++;
	for (int i = 0; i < NW_JIT_CACHE; i++) {
		if (g_cache[i].used != NW_JIT_USED_LIVE)
			continue;
		if (code_bank(g_cache[i].fn) != bank)
			continue;
		g_cache[i].used = NW_JIT_USED_TOMB;
		g_flush++;
		g_flush_src[NW_JIT_FL_WRAP]++;
	}
	pagebit_rebuild();
}

static void wrap_note_occupancy(void)
{
	int live = 0;
	for (int i = 0; i < NW_JIT_CACHE; i++) {
		if (g_cache[i].used == NW_JIT_USED_LIVE)
			live++;
	}
	if (live > g_occ_max)
		g_occ_max = live;
	const uint64_t since = g_compiles - g_compiles_at_wrap;
	if (nw_jit_stats_wanted()) {
		printf("NW-BOOT G1: jit wrap live %d occ_max %d used %zu compiles %llu b/block %llu\n",
		       live, g_occ_max, g_code_used,
		       (unsigned long long)since,
		       (unsigned long long)(since ? g_code_used / since : 0));
		fflush(stdout);
	}
	g_wraps++;
	g_compiles_at_wrap = g_compiles;
}

uint64_t nw_jit_flush_count(void)
{
	return g_flush;
}

uint64_t nw_jit_compile_count(void)
{
	return g_compiles;
}

uint64_t nw_jit_wrap_count(void)
{
	return g_wraps;
}

size_t nw_jit_code_used(void)
{
	return g_code_used;
}

uint64_t nw_jit_evict_count(void)
{
	return g_evict;
}

int nw_jit_stats_wanted(void)
{
#if NW_BOOT_LOG
	return 1;
#else
	return 0;
#endif
}

int nw_jit_mode(void)
{
	if (g_mode < 0) {
		const char *e = getenv("NW_JIT");
		if (e && (strcmp(e, "on") == 0 || strcmp(e, "1") == 0))
			g_mode = NW_JIT_ON;
		else if (e && strcmp(e, "verify") == 0)
			g_mode = NW_JIT_VERIFY;
		else if (e && strcmp(e, "fallback") == 0)
			g_mode = NW_JIT_FALLBACK;
		else
			g_mode = NW_JIT_OFF;
	}
	return g_mode;
}

void nw_jit_set_mode(int mode)
{
	g_mode = mode;
}

const char *nw_jit_mode_name(void)
{
	switch (nw_jit_mode()) {
	case NW_JIT_ON:		return "on";
	case NW_JIT_VERIFY:	return "verify";
	case NW_JIT_FALLBACK:	return "fallback";
	default:		return "off";
	}
}

static void nw_jit_summary_write_file(void);

void nw_jit_stats_print(const char *why)
{
#if !NW_BOOT_LOG
	(void)why;
	return;
#endif
	if (!g_exec_blocks && nw_jit_mode() == NW_JIT_OFF)
		return;
	static const char *const src_name[NW_JIT_FL_N] = {
		"store", "icbi", "tlb", "sr", "bat", "sdr1", "wrap",
		"istore", "host", "other"
	};
	printf("NW-BOOT G1: jit stats %s mode %s blocks %llu insns %llu flush %llu compiles %llu evict %llu recompile_n %llu dtlb hit %llu miss %llu wrap %llu occ_max %d b/block %llu\n",
	       why, nw_jit_mode_name(),
	       (unsigned long long)g_exec_blocks,
	       (unsigned long long)g_exec_insns,
	       (unsigned long long)g_flush,
	       (unsigned long long)g_compiles,
	       (unsigned long long)g_evict,
	       (unsigned long long)g_recompile_n,
	       (unsigned long long)g_dtlb_hit,
	       (unsigned long long)g_dtlb_miss,
	       (unsigned long long)g_wraps,
	       g_occ_max,
	       (unsigned long long)(g_compiles ? g_code_emitted / g_compiles : 0));
#ifdef __APPLE__
	printf("NW-BOOT G1: jit wx ns %llu icache ns %llu n %llu\n",
	       (unsigned long long)g_wx_ns,
	       (unsigned long long)g_icache_ns,
	       (unsigned long long)g_wx_n);
#endif
	printf("NW-BOOT G1: jit itlb hit %llu miss %llu mtsr vsid_chg=%llu total=%llu\n",
	       (unsigned long long)g_itlb_hit, (unsigned long long)g_itlb_miss,
	       (unsigned long long)g_mtsr_vsid, (unsigned long long)g_mtsr_total);
	for (int i = 0; i < NW_JIT_FL_N; i++) {
		if (g_flush_calls[i] || g_flush_src[i])
			printf("NW-BOOT G1: jit flush %s calls %llu entries %llu\n",
			       src_name[i],
			       (unsigned long long)g_flush_calls[i],
			       (unsigned long long)g_flush_src[i]);
	}
	{
		static const char *const dtlb_fl_name[NW_JIT_DTLB_FL_N] = {
			"mtmsr", "rfi", "mtsr", "tlb", "bat", "sdr1", "reset", "other"
		};
		for (int i = 0; i < NW_JIT_DTLB_FL_N; i++) {
			if (g_dtlb_fl[i])
				printf("NW-BOOT G1: jit dtlb-flush %s %llu\n",
				       dtlb_fl_name[i],
				       (unsigned long long)g_dtlb_fl[i]);
		}
	}
	{
		int top[NW_JIT_SKIPTOP];
		int ntop = 0;
		for (int i = 0; i < NW_JIT_SKIPN; i++) {
			if (!g_skip[i].n)
				continue;
			int k = ntop;
			while (k > 0 && (g_skip[i].lost > g_skip[top[k - 1]].lost ||
					 (g_skip[i].lost == g_skip[top[k - 1]].lost &&
					  g_skip[i].n > g_skip[top[k - 1]].n)))
				k--;
			if (k >= NW_JIT_SKIPTOP)
				continue;
			int n = ntop < NW_JIT_SKIPTOP ? ntop : NW_JIT_SKIPTOP - 1;
			for (int j = n; j > k; j--)
				top[j] = top[j - 1];
			top[k] = i;
			if (ntop < NW_JIT_SKIPTOP)
				ntop++;
		}
		for (int i = 0; i < ntop; i++) {
			const int j = top[i];
			const char *nm = NULL;
			const int p = g_skip[j].prim, x = g_skip[j].xo;
			if (p == 19 && x == 528)
				nm = "bcctr";
			else if (p == 19 && x == 16)
				nm = "bclr";
			else if (p == 19 && x == 50)
				nm = "rfi";
			else if (p == 37)
				nm = "stwu";
			else if (p == 39)
				nm = "stbu";
			else if (p == 45)
				nm = "sthu";
			else if (p == 13)
				nm = "addic.";
			else if (p == 12)
				nm = "addic";
			else if (p == 31 && x == 151)
				nm = "stwx";
			else if (p == 31 && x == 407)
				nm = "sthx";
			else if (p == 31 && x == 23)
				nm = "lwzx";
			else if (p == 31 && x == 10)
				nm = "addc";
			else if (p == 31 && x == 522)
				nm = "addco";
			else if (p == 31 && x == 8)
				nm = "subfc";
			else if (p == 31 && x == 520)
				nm = "subfco";
			else if (p == 31 && x == 136)
				nm = "subfe";
			else if (p == 31 && x == 648)
				nm = "subfeo";
			else if (p == 31 && x == 40)
				nm = "subf";
			else if (p == 31 && x == 552)
				nm = "subfo";
			else if (p == 31 && x == 792)
				nm = "sraw";
			else if (p == 31 && x == 954)
				nm = "extsb";
			else if (p == 31 && x == 598)
				nm = "sync";
			else if (p == 31 && x == 342)
				nm = "dst";
			else if (p == 31 && x == 374)
				nm = "dstst";
			else if (p == 31 && x == 86)
				nm = "dcbf";
			else if (p == 31 && x == 535)
				nm = "lfsx";
			else if (p == 31 && x == 663)
				nm = "stfsx";
			else if (p == 31 && x == 599)
				nm = "lfdx";
			else if (p == 31 && x == 727)
				nm = "stfdx";
			else if (p == 48)
				nm = "lfs";
			else if (p == 49)
				nm = "lfsu";
			else if (p == 51)
				nm = "lfdu";
			else if (p == 52)
				nm = "stfs";
			else if (p == 55)
				nm = "stfdu";
			else if (p == 41)
				nm = "lhzu";
			else if (p == 4 && x == 0)
				nm = "vaddubm";
			else if (p == 59 && (x & 31) == 18)
				nm = "fdivs";
			else if (p == 59 && (x & 31) == 20)
				nm = "fsubs";
			else if (p == 59 && (x & 31) == 21)
				nm = "fadds";
			else if (p == 59 && (x & 31) == 25)
				nm = "fmuls";
			else if (p == 59 && (x & 31) == 28)
				nm = "fmsubs";
			else if (p == 59 && (x & 31) == 29)
				nm = "fmadds";
			else if (p == 59 && (x & 31) == 30)
				nm = "fnmsubs";
			else if (p == 63 && x == 711)
				nm = "mtfsf";
			else if (p == 63 && x == 583)
				nm = "mffs";
			else if (p == 63 && x == 12)
				nm = "frsp";
			else if (p == 63 && x == 32)
				nm = "fcmpo";
			else if (p == 63 && x == 0)
				nm = "fcmpu";
			else if (p == 63 && x == 264)
				nm = "fabs";
			else if (p == 4 && x == 514)
				nm = "vand";
			else if (p == 4 && x == 258)
				nm = "vsrb";
			else if (p == 4 && x == 67)
				nm = "vcmpequw";
			else if (p == 4 && x == 579)
				nm = "vcmpequw.";
			else if (p == 4 && x == 3)
				nm = "vcmpequb";
			else if (p == 4 && x == 515)
				nm = "vcmpequb.";
			else if (p == 4 && x == 385)
				nm = "vminsb";
			else if (p == 4 && x == 354)
				nm = "vsr";
			else if (p == 4 && x == 322)
				nm = "vsrw";
			else if (p == 4 && x == 454)
				nm = "vspltisw";
			else if (p == 4 && x == 390)
				nm = "vspltisb";
			else if (p == 4 && (x & 31) == 22)
				nm = "vsldoi";
			else if (p == 4 && x == 6)
				nm = "vmrghb";
			else if (p == 4 && x == 134)
				nm = "vmrglb";
			else if (p == 4 && x == 610)
				nm = "vxor";
			else if (p == 4 && x == 546)
				nm = "vandc";
			else if (p == 4 && x == 512)
				nm = "vsububm";
			else if (p == 4 && x == 162)
				nm = "vslh";
			else if (p == 4 && x == 226)
				nm = "vsl";
			else if (p == 4 && x == 518)
				nm = "vslo";
			else if (p == 4 && x == 262)
				nm = "vspltb";
			else if (p == 4 && x == 550)
				nm = "vsro";
			else if (p == 4 && x == 130)
				nm = "vslb";
			else if (p == 4 && x == 770)
				nm = "mfvscr";
			else if (p == 4 && x == 326)
				nm = "vspltw";
			else if (p == 4 && (x & 31) == 17)
				nm = "vmladduhm";
			else if (p == 4 && x == 928)
				nm = "vsubshs";
			else if (p == 31 && x == 487)
				nm = "stvxl";
			else if (p == 31 && x == 359)
				nm = "lvxl";
			else if (p == 31 && x == 247)
				nm = "stbux";
			else if (p == 31 && x == 439)
				nm = "sthux";
			else if (p == 31 && x == 200)
				nm = "subfze";
			else if (p == 31 && x == 566)
				nm = "tlbsync";
			else if (p == 31 && x == 311)
				nm = "lhzux";
			else if (p == 31 && x == 650)
				nm = "addeo";
			else if (p == 31 && x == 306)
				nm = "tlbie";
			else if (p == 4 && x == 802)
				nm = "mtvscr";
			else if (p == 31 && x == 75)
				nm = "mulhw";
			else if (p == 31 && x == 279)
				nm = "lhzx";
			else if (p == 31 && x == 534)
				nm = "lwbrx";
			else if (p == 31 && x == 242)
				nm = "mtsrin";
			else if (p == 31 && x == 659)
				nm = "mfsrin";
			else if (p == 6)
				nm = "op6";
			else if (p == 31 && x == 138)
				nm = "adde";
			else if (p == 63 && x == 40)
				nm = "fneg";
			else if (p == 63 && x == 72)
				nm = "fmr";
			else if (p == 63 && (x & 31) == 29)
				nm = "fmadd";
			else if (p == 63 && (x & 31) == 30)
				nm = "fnmsub";
			else if (p == 19 && x == 0)
				nm = "mcrf";
			else if (p == 4 && x == 64)
				nm = "vadduwm";
			else if (p == 4 && x == 450)
				nm = "vsraw";
			else if (p == 4 && x == 231)
				nm = "vpkswss";
			else if (p == 31 && x == 144)
				nm = "mtcrf";
			else if (p == 31 && x == 19)
				nm = "mfcr";
			else if (p == 19 && x == 33)
				nm = "crnor";
			else if (p == 19 && x == 193)
				nm = "crxor";
			else if (p == 19 && x == 289)
				nm = "creqv";
			else if (p == 31 && x == 87)
				nm = "lbzx";
			else if (p == 31 && x == 215)
				nm = "stbx";
			else if (p == 31 && x == 183)
				nm = "stwux";
			else if (p == 31 && x == 55)
				nm = "lwzux";
			else if (p == 31 && x == 119)
				nm = "lbzux";
			else if (p == 31 && x == 778)
				nm = "addo";
			else if (p == 31 && x == 747)
				nm = "mullwo";
			else if (p == 31 && x == 459)
				nm = "divwu";
			else if (p == 31 && x == 971)
				nm = "divwuo";
			else if (p == 31 && x == 491)
				nm = "divw";
			else if (p == 31 && x == 1003)
				nm = "divwo";
			else if (p == 31 && x == 597)
				nm = "lswi";
			else if (p == 31 && x == 725)
				nm = "stswi";
			else if (p == 31 && x == 83)
				nm = "mfmsr";
			else if (p == 31 && x == 371)
				nm = "mftb";
			else if (p == 17)
				nm = "sc";
			else if (p == 31 && x == 790)
				nm = "lhax";
			else if (p == 10)
				nm = "cmpli";
			else if (p == 31 && x == 32)
				nm = "cmpl";
			else if (p == 15)
				nm = "addis";
			else if (p == 23)
				nm = "rlwnm";
			else if (p == 46)
				nm = "lmw";
			else if (p == 47)
				nm = "stmw";
			else if (p == 24)
				nm = "ori";
			else if (p == 25)
				nm = "oris";
			else if (p == 26)
				nm = "xori";
			else if (p == 28)
				nm = "andi.";
			else if (p == 8)
				nm = "subfic";
			else if (p == 7)
				nm = "mulli";
			else if (p == 34)
				nm = "lbz";
			else if (p == 38)
				nm = "stb";
			else if (p == 33)
				nm = "lwzu";
			else if (p == 31 && x == 40)
				nm = "subf";
			else if (p == 31 && x == 104)
				nm = "neg";
			else if (p == 31 && x == 444)
				nm = "or";
			else if (p == 31 && x == 316)
				nm = "xor";
			else if (p == 31 && x == 284)
				nm = "eqv";
			else if (p == 31 && x == 124)
				nm = "nor";
			else if (p == 31 && x == 476)
				nm = "nand";
			else if (p == 63 && x == 0)
				nm = "fcmpu";
			else if (p == 31 && x == 26)
				nm = "cntlzw";
			else if (p == 31 && x == 104)
				nm = "neg";
			else if (p == 31 && x == 316)
				nm = "xor";
			else if (p == 31 && x == 284)
				nm = "eqv";
			else if (p == 31 && x == 124)
				nm = "nor";
			else if (p == 31 && x == 476)
				nm = "nand";
			else if (p == 63 && x == 0)
				nm = "fcmpu";
			else if (p == 31 && x == 28)
				nm = "and";
			else if (p == 31 && x == 24)
				nm = "slw";
			else if (p == 31 && x == 536)
				nm = "srw";
			else if (p == 31 && x == 792)
				nm = "sraw";
			else if (p == 31 && x == 824)
				nm = "srawi";
			printf("NW-BOOT G1: jit skip_unsup %s prim=%d xo=%d n=%llu lost=%llu op=%08x pc=%08x ra=%d rd=%d\n",
			       nm ? nm : "?", p, x,
			       (unsigned long long)g_skip[j].n,
			       (unsigned long long)g_skip[j].lost,
			       (unsigned)g_skip[j].op,
			       (unsigned)g_skip[j].pc,
			       (int)((g_skip[j].op >> 16) & 0x1f),
			       (int)((g_skip[j].op >> 21) & 0x1f));
		}
	}
	{
		const uint64_t dc = g_codec_insns - g_codec_insns_tick;
		const uint64_t do_ = g_other_insns - g_other_insns_tick;
		const uint64_t tot = dc + do_;
		const unsigned ratio = tot ? (unsigned)((dc * 1000ull) / tot) : 0;
		printf("NW-BOOT G1: jit codec %s insns %llu other %llu ratio %u/1000\n",
		       why, (unsigned long long)dc, (unsigned long long)do_, ratio);
		g_codec_insns_tick = g_codec_insns;
		g_other_insns_tick = g_other_insns;
	}
	nw_jit_summary_print(why);
	nw_jit_summary_write_file();
	fflush(stdout);
}

void nw_jit_summary_print(const char *why)
{
	const uint64_t tot = g_dtlb_hit + g_dtlb_miss;
	const unsigned miss_pct = tot ? (unsigned)((g_dtlb_miss * 1000ull) / tot) : 0;
	printf("NW-BOOT G1: jit summary %s skip_unsup %llu skip_io %llu dtlb_miss %u/1000 wrap %llu occ_max %d chain %llu\n",
	       why ? why : "?",
	       (unsigned long long)g_v_skip_unsup,
	       (unsigned long long)g_v_skip_io,
	       miss_pct,
	       (unsigned long long)g_wraps,
	       g_occ_max,
	       (unsigned long long)g_chain_hops);
	printf("NW-BOOT G1: dtlb-why sr=%llu bat=%llu conflict=%llu pr=%llu other=%llu\n",
	       (unsigned long long)g_dtlb_why[DTLB_WHY_SR],
	       (unsigned long long)g_dtlb_why[DTLB_WHY_BAT],
	       (unsigned long long)g_dtlb_why[DTLB_WHY_CONFLICT],
	       (unsigned long long)g_dtlb_why[DTLB_WHY_PR],
	       (unsigned long long)g_dtlb_why[DTLB_WHY_OTHER]);
	{
		static const char *const cut_name[NW_JIT_CUT_N] = {
			"ends_block", "page_cross", "peek_fail", "class_change",
			"unsup_next", "mem_ok0", "mem_ok2", "max_block", "first_op_io"
		};
		printf("NW-BOOT G1: jit summary %s cut", why ? why : "?");
		for (int i = 0; i < NW_JIT_CUT_N; i++)
			printf(" %s=%llu", cut_name[i], (unsigned long long)g_cut[i]);
		printf("\n");
	}
	{
		static const char *const hop_name[NW_JIT_HOP_N] = {
			"cap", "no_chain_pc", "pc_mismatch", "itlb_miss", "aline",
			"cache_miss", "vec_gate", "fp_gate", "compile_null"
		};
		printf("NW-BOOT G1: jit summary %s hop_stop", why ? why : "?");
		for (int i = 0; i < NW_JIT_HOP_N; i++)
			printf(" %s=%llu", hop_name[i], (unsigned long long)g_hop_stop[i]);
		printf("\n");
	}
	{
		const uint64_t it = g_itlb_hit + g_itlb_miss;
		const unsigned im = it ? (unsigned)((g_itlb_miss * 1000ull) / it) : 0;
		printf("NW-BOOT G1: jit summary %s itlb_miss %u/1000 mtsr vsid_chg=%llu total=%llu bat_gen=%llu bat_total=%llu\n",
		       why ? why : "?", im,
		       (unsigned long long)g_mtsr_vsid, (unsigned long long)g_mtsr_total,
		       (unsigned long long)g_bat_bumps, (unsigned long long)g_bat_total);
	}
	printf("NW-BOOT G1: jit summary %s codec %llu other %llu kcall_fast %llu qt_fps_proxy frames=%llu flat_max=%u upload=%llu\n",
	       why ? why : "?",
	       (unsigned long long)g_codec_insns,
	       (unsigned long long)g_other_insns,
	       (unsigned long long)g_kcall_fast,
	       (unsigned long long)nw_fb_fps_proxy_frames(),
	       nw_fb_fps_proxy_flat_max(),
	       (unsigned long long)nw_fb_damage_upload_bytes());
	{
		int top[8];
		int ntop = 0;
		for (int i = 0; i < NW_JIT_DTLBH; i++) {
			if (!g_dtlb_h[i].n)
				continue;
			int k = ntop;
			while (k > 0 && g_dtlb_h[i].n > g_dtlb_h[top[k - 1]].n)
				k--;
			if (k >= 8)
				continue;
			int n = ntop < 8 ? ntop : 7;
			for (int j = n; j > k; j--)
				top[j] = top[j - 1];
			top[k] = i;
			if (ntop < 8)
				ntop++;
		}
		for (int i = 0; i < ntop; i++) {
			const int j = top[i];
			printf("NW-BOOT G1: jit dtlb-page ea=%08x n=%llu\n",
			       (unsigned)g_dtlb_h[j].page,
			       (unsigned long long)g_dtlb_h[j].n);
		}
	}
	{
		int top[8];
		int ntop = 0;
		for (int i = 0; i < NW_JIT_IOH; i++) {
			if (!g_io_h[i].n)
				continue;
			int k = ntop;
			while (k > 0 && g_io_h[i].n > g_io_h[top[k - 1]].n)
				k--;
			if (k >= 8)
				continue;
			int n = ntop < 8 ? ntop : 7;
			for (int j = n; j > k; j--)
				top[j] = top[j - 1];
			top[k] = i;
			if (ntop < 8)
				ntop++;
		}
		for (int i = 0; i < ntop; i++) {
			const int j = top[i];
			printf("NW-BOOT G1: jit skip_io ea=%08x pc=%08x n=%llu\n",
			       (unsigned)g_io_h[j].page,
			       (unsigned)g_io_h[j].pc,
			       (unsigned long long)g_io_h[j].n);
		}
	}
	fflush(stdout);
}

static void nw_jit_summary_write_file(void)
{
	char path[512];
	const char *env = getenv("NW_JIT_SUMMARY");
	if (env && env[0]) {
		snprintf(path, sizeof(path), "%s", env);
	} else {
		const char *home = getenv("HOME");
		if (!home || !home[0])
			return;
		snprintf(path, sizeof(path), "%s/Library/Logs/SheepShaver", home);
		(void)mkdir(path, 0755);
		snprintf(path, sizeof(path), "%s/Library/Logs/SheepShaver/jit-summary.txt", home);
	}
	FILE *f = fopen(path, "w");
	if (!f)
		return;
	const uint64_t tot = g_dtlb_hit + g_dtlb_miss;
	const unsigned miss_pct = tot ? (unsigned)((g_dtlb_miss * 1000ull) / tot) : 0;
	fprintf(f, "skip_unsup %llu skip_io %llu dtlb_miss %u/1000 wrap %llu occ_max %d codec %llu other %llu\n",
		(unsigned long long)g_v_skip_unsup,
		(unsigned long long)g_v_skip_io,
		miss_pct,
		(unsigned long long)g_wraps,
		g_occ_max,
		(unsigned long long)g_codec_insns,
		(unsigned long long)g_other_insns);
	fclose(f);
}

static void nw_jit_atexit_stats(void)
{
	nw_jit_stats_print("exit");
	nw_jit_verify_dump("exit");
	nw_atrap_hist_dump("exit");
}

void nw_jit_set_host_half(nw_jit_host_lh lh, nw_jit_host_sth16 sth)
{
	g_host_lh = lh;
	g_host_sth16 = sth;
}

void nw_jit_set_host_byte(nw_jit_host_lb lb, nw_jit_host_stb8 stb)
{
	g_host_lb = lb;
	g_host_stb = stb;
}

void nw_jit_set_host_msr(nw_jit_host_msr fn)
{
	g_host_msr = fn;
}

void nw_jit_set_host_mem(nw_jit_host_lwz lwz, nw_jit_host_stw stw)
{
	static int once;
	g_host_lwz = lwz;
	g_host_stw = stw;
	if (!once) {
		once = 1;
		atexit(nw_jit_atexit_stats);
	}
}

void nw_jit_set_host_pa(nw_jit_host_lwz_pa lwz, nw_jit_host_stw_pa stw)
{
	g_host_lwz_pa = lwz;
	g_host_stw_pa = stw;
}

void nw_jit_set_host_mfspr(nw_jit_host_mfspr fn)
{
	g_host_mfspr = fn;
}

void nw_jit_set_host_isync(nw_jit_host_isync fn)
{
	g_host_isync = fn;
}

void nw_jit_set_host_mtmsr(nw_jit_host_mtmsr fn)
{
	g_host_mtmsr = fn;
}

void nw_jit_set_host_mtsr(nw_jit_host_mtsr fn)
{
	g_host_mtsr = fn;
}

void nw_jit_set_host_mfsr(nw_jit_host_mfsr fn)
{
	g_host_mfsr = fn;
}

void nw_jit_set_host_trap(nw_jit_host_trap fn)
{
	g_host_trap = fn;
}

void nw_jit_set_host_sc(nw_jit_host_sc fn)
{
	g_host_sc = fn;
}

void nw_jit_set_host_mtspr(nw_jit_host_mtspr fn)
{
	g_host_mtspr = fn;
}

void nw_jit_set_host_lvx(nw_jit_host_lvx fn)
{
	g_host_lvx = fn;
}

void nw_jit_set_host_stvx(nw_jit_host_stvx fn)
{
	g_host_stvx = fn;
}

void nw_jit_set_host_vmx(nw_jit_host_vmx fn)
{
	g_host_vmx = fn;
}

void nw_jit_set_host_rfi(nw_jit_host_rfi fn)
{
	g_host_rfi = fn;
}

void nw_jit_set_host_chain(nw_jit_host_chain fn)
{
	g_host_chain = fn;
}

void nw_jit_tail_begin(void)
{
	g_tail_hops = g_tail_n = g_tail_dsi_n = g_tail_fpr = g_tail_vr = 0;
	g_tail_dsi_pc = 0;
}

int nw_jit_tail_n(void)
{
	return g_tail_n;
}

void nw_jit_tail_dsi(uint32_t *pc, int *n)
{
	if (pc)
		*pc = g_tail_dsi_pc;
	if (n)
		*n = g_tail_dsi_n;
}

void nw_jit_tail_class(int *fpr, int *vr)
{
	if (fpr)
		*fpr = g_tail_fpr;
	if (vr)
		*vr = g_tail_vr;
}

void *nw_jit_helper_chain(struct nw_jit_cpu *cpu, uint32_t chain_pc, uint32_t cur_class)
{
	if (g_mode != NW_JIT_ON || !cpu || cpu->fault)
		return NULL;
	if (!chain_pc || cpu->pc != chain_pc)
		return NULL;
	if (g_tail_hops >= NW_JIT_TAIL_MAX)
		return NULL;
	if (!g_host_chain || !cpu->host)
		return NULL;
	const int cur_fpr = (cur_class & 1u) ? 1 : 0;
	const int cur_vr = (cur_class & 2u) ? 1 : 0;
	int n2 = 0, f2 = 0, v2 = 0;
	uint32_t dsi_pc = 0, chain2 = 0;
	void *next = g_host_chain(cpu->host, cpu, chain_pc, &n2, &f2, &v2,
				  &dsi_pc, &chain2, cur_fpr, cur_vr);
	if (!next || next == (void *)NW_JIT_INTERPRET || n2 <= 0)
		return NULL;
	g_tail_hops++;
	g_tail_n += n2;
	g_tail_dsi_pc = dsi_pc;
	g_tail_dsi_n = n2;
	if (f2 || cur_fpr)
		g_tail_fpr = 1;
	if (v2 || cur_vr)
		g_tail_vr = 1;
	nw_jit_note_chain(1);
	(void)chain2;
	return next;
}

void nw_jit_set_host_icbi(nw_jit_host_icbi fn)
{
	g_host_icbi = fn;
}

void nw_jit_set_host_tlbie(nw_jit_host_tlbie fn)
{
	g_host_tlbie = fn;
}

void nw_jit_set_host_tlbia(nw_jit_host_tlbia fn)
{
	g_host_tlbia = fn;
}

void nw_jit_set_host_lwarx(nw_jit_host_lwarx fn)
{
	g_host_lwarx = fn;
}

void nw_jit_set_host_stwcx(nw_jit_host_stwcx fn)
{
	g_host_stwcx = fn;
}

void nw_jit_set_host_lfd(nw_jit_host_lfd fn)
{
	g_host_lfd = fn;
}

void nw_jit_set_host_stfd(nw_jit_host_stfd fn)
{
	g_host_stfd = fn;
}

void nw_jit_dtlb_flush_src(int src)
{
	if (src < 0 || src >= NW_JIT_DTLB_FL_N)
		src = NW_JIT_DTLB_FL_OTHER;
	g_dtlb_fl[src]++;
	memset(g_dtlb, 0, sizeof(g_dtlb));
	nw_jit_itlb_flush();
}

void nw_jit_dtlb_flush(void)
{
	nw_jit_dtlb_flush_src(NW_JIT_DTLB_FL_OTHER);
}

void nw_jit_dtlb_drop_sr(unsigned sr, int src)
{
	if (src < 0 || src >= NW_JIT_DTLB_FL_N)
		src = NW_JIT_DTLB_FL_OTHER;
	g_dtlb_fl[src]++;
	const uint32_t seg = (sr & 0xfu) << 28;
	for (int i = 0; i < NW_JIT_DTLB_N; i++) {
		for (int w = 0; w < NW_JIT_DTLB_WAYS; w++) {
			if ((g_dtlb[i][w].flags & NW_JIT_DTLB_VALID) &&
			    (g_dtlb[i][w].ea_page & 0xf0000000u) == seg)
				g_dtlb[i][w].flags = 0;
		}
	}
}

void nw_jit_dtlb_drop_bat(uint32_t upper, int src)
{
	if (src < 0 || src >= NW_JIT_DTLB_FL_N)
		src = NW_JIT_DTLB_FL_OTHER;
	g_dtlb_fl[src]++;
	g_bat_total++;
	(void)upper;
	g_bat_gen++;
	g_bat_bumps++;
}

void nw_jit_dtlb_drop_page(uint32_t ea, int src)
{
	if (src < 0 || src >= NW_JIT_DTLB_FL_N)
		src = NW_JIT_DTLB_FL_OTHER;
	g_dtlb_fl[src]++;
	const unsigned i = (ea >> 12) & (NW_JIT_DTLB_N - 1u);
	const uint32_t page = ea & ~0xfffu;
	for (int w = 0; w < NW_JIT_DTLB_WAYS; w++) {
		if ((g_dtlb[i][w].flags & NW_JIT_DTLB_VALID) &&
		    g_dtlb[i][w].ea_page == page)
			g_dtlb[i][w].flags = 0;
	}
	nw_jit_itlb_drop_page(ea);
}

void nw_jit_dtlb_flush_if_pr(uint32_t old_msr, uint32_t new_msr, int src)
{
	/* Privilege is in the DTLB flags; a PR/IR/DR change is a miss, not a
	 * flush. Flushing here on IR/DR also hit kpx rfi and bombed QT (CHK). */
	(void)old_msr;
	(void)new_msr;
	(void)src;
}

void nw_jit_cpu_bind(struct nw_jit_cpu *c)
{
	c->jit_dtlb = g_dtlb;
	c->jit_dtlb_hit = &g_dtlb_hit;
	c->jit_sr_gen = g_sr_gen;
	c->jit_lwz = (void *)nw_jit_helper_lwz;
	c->jit_stw = (void *)nw_jit_helper_stw;
	c->jit_lwz_pa = (void *)nw_jit_helper_lwz_pa;
	c->jit_stw_pa = (void *)nw_jit_helper_stw_pa;
}

void nw_jit_dtlb_fill(uint32_t ea, uint32_t pa, int writable, uint64_t host, int pr, int via_bat)
{
	{
		const uint32_t page = ea & ~0xfffu;
		int i = (int)((page >> 12) & (NW_JIT_DTLBH - 1u));
		int cold = i;
		uint64_t cold_n = ~(uint64_t)0;
		for (int p = 0; p < 4; p++) {
			int j = (i + p) & (NW_JIT_DTLBH - 1);
			if (g_dtlb_h[j].n == 0 || g_dtlb_h[j].page == page) {
				g_dtlb_h[j].page = page;
				g_dtlb_h[j].n++;
				goto dtlb_h_done;
			}
			if (g_dtlb_h[j].n < cold_n) {
				cold_n = g_dtlb_h[j].n;
				cold = j;
			}
		}
		g_dtlb_h[cold].page = page;
		g_dtlb_h[cold].n = 1;
	}
dtlb_h_done:
	const unsigned i = (ea >> 12) & (NW_JIT_DTLB_N - 1u);
	const uint32_t page = ea & ~0xfffu;
	int slot = NW_JIT_DTLB_WAYS - 1;
	for (int w = 0; w < NW_JIT_DTLB_WAYS; w++) {
		if (!(g_dtlb[i][w].flags & NW_JIT_DTLB_VALID) ||
		    g_dtlb[i][w].ea_page == page) {
			slot = w;
			break;
		}
	}
	g_dtlb[i][slot].ea_page = page;
	g_dtlb[i][slot].pa_page = pa & ~0xfffu;
	g_dtlb[i][slot].host = host;
	g_dtlb[i][slot].sr_gen = g_sr_gen[(ea >> 28) & 0xfu];
	g_dtlb[i][slot].bat_gen = via_bat ? g_bat_gen : 0;
	uint32_t flags = NW_JIT_DTLB_VALID;
	if (writable)
		flags |= NW_JIT_DTLB_WRITE;
	if (host)
		flags |= NW_JIT_DTLB_HOST;
	if (pr)
		flags |= NW_JIT_DTLB_PR;
	if (via_bat)
		flags |= NW_JIT_DTLB_BAT;
	g_dtlb[i][slot].flags = flags;
}

int nw_jit_dtlb_lookup_pr(uint32_t ea, int is_store, uint32_t *pa, int pr)
{
	const unsigned i = (ea >> 12) & (NW_JIT_DTLB_N - 1u);
	const uint32_t page = ea & ~0xfffu;
	const uint32_t sr = g_sr_gen[(ea >> 28) & 0xfu];
	for (int w = 0; w < NW_JIT_DTLB_WAYS; w++) {
		const struct nw_jit_dtlb_ent *e = &g_dtlb[i][w];
		if (!(e->flags & NW_JIT_DTLB_VALID) || e->ea_page != page)
			continue;
		if (e->sr_gen != sr)
			return 0;
		if ((e->flags & NW_JIT_DTLB_BAT) && e->bat_gen != g_bat_gen)
			return 0;
		if (((e->flags & NW_JIT_DTLB_PR) != 0) != (pr != 0))
			return 0;
		if (is_store && !(e->flags & NW_JIT_DTLB_WRITE))
			return 0;
		if (pa)
			*pa = e->pa_page | (ea & 0xfffu);
		return 1;
	}
	return 0;
}

int nw_jit_dtlb_lookup(uint32_t ea, int is_store, uint32_t *pa)
{
	return nw_jit_dtlb_lookup_pr(ea, is_store, pa, 0);
}

/* Classify a miss from the set as it stands, before the refill. */
static int dtlb_why(uint32_t ea, int is_store, int pr)
{
	const unsigned i = (ea >> 12) & (NW_JIT_DTLB_N - 1u);
	const uint32_t page = ea & ~0xfffu;
	const uint32_t sr = g_sr_gen[(ea >> 28) & 0xfu];
	int valid = 0;
	for (int w = 0; w < NW_JIT_DTLB_WAYS; w++) {
		const struct nw_jit_dtlb_ent *e = &g_dtlb[i][w];
		if (!(e->flags & NW_JIT_DTLB_VALID))
			continue;
		valid++;
		if (e->ea_page != page)
			continue;
		if (e->sr_gen != sr)
			return DTLB_WHY_SR;
		if ((e->flags & NW_JIT_DTLB_BAT) && e->bat_gen != g_bat_gen)
			return DTLB_WHY_BAT;
		if (((e->flags & NW_JIT_DTLB_PR) != 0) != (pr != 0) ||
		    (is_store && !(e->flags & NW_JIT_DTLB_WRITE)))
			return DTLB_WHY_PR;
	}
	if (valid >= NW_JIT_DTLB_WAYS)
		return DTLB_WHY_CONFLICT;
	return DTLB_WHY_OTHER;
}

static void dtlb_note_miss(uint32_t ea, int is_store, int pr)
{
	g_dtlb_miss++;
	g_dtlb_why[dtlb_why(ea, is_store, pr)]++;
}

/* Copy the translator MSR into the JIT struct. A hop or tail does not
 * pass through execute(), which is the only other writer of cpu->msr. */
static void dtlb_sync_msr(struct nw_jit_cpu *cpu)
{
	if (!g_host_msr || !cpu->host)
		return;
	const uint32_t live = g_host_msr(cpu->host);
	if (live == cpu->msr)
		return;
	static unsigned n;
	if (n < 8u) {
		n++;
		printf("NW-BOOT G1: dtlb-msr #%u cpu=%08x live=%08x pc=%08x\n",
		       n, (unsigned)cpu->msr, (unsigned)live, (unsigned)cpu->pc);
		fflush(stdout);
	}
	cpu->msr = live;
}

/* Hit already in the table: use the host pointer. Do not translate or refill.
 * Returns 0 when the helper must take the real miss. */
static int dtlb_host_line(uint32_t ea, int is_store, int pr, uint64_t *host_out)
{
	const unsigned i = (ea >> 12) & (NW_JIT_DTLB_N - 1u);
	const uint32_t page = ea & ~0xfffu;
	const uint32_t sr = g_sr_gen[(ea >> 28) & 0xfu];
	for (int w = 0; w < NW_JIT_DTLB_WAYS; w++) {
		const struct nw_jit_dtlb_ent *e = &g_dtlb[i][w];
		if (!(e->flags & NW_JIT_DTLB_VALID) || e->ea_page != page)
			continue;
		if (e->sr_gen != sr)
			return 0;
		if ((e->flags & NW_JIT_DTLB_BAT) && e->bat_gen != g_bat_gen)
			return 0;
		if (((e->flags & NW_JIT_DTLB_PR) != 0) != (pr != 0))
			return 0;
		if (is_store && !(e->flags & NW_JIT_DTLB_WRITE))
			return 0;
		if (!e->host)
			return 0;
		*host_out = e->host;
		return 1;
	}
	return 0;
}

uint64_t nw_jit_dtlb_hits(void)
{
	return g_dtlb_hit;
}

uint64_t nw_jit_mtsr_total(void)
{
	return g_mtsr_total;
}

uint64_t nw_jit_mtsr_vsid(void)
{
	return g_mtsr_vsid;
}

int nw_jit_cache_slots(void)
{
	return NW_JIT_CACHE;
}

void nw_jit_itlb_flush(void)
{
	memset(g_itlb, 0, sizeof(g_itlb));
	g_itlb_sticky = 0;
}

void nw_jit_itlb_fill(uint32_t ea, uint32_t pa)
{
	const unsigned i = (ea >> 12) & (NW_JIT_ITLB_N - 1u);
	g_itlb[i].ea_page = ea & ~0xfffu;
	g_itlb[i].pa_page = pa & ~0xfffu;
	g_itlb[i].flags = 1u;
	g_itlb[i].sr_gen = g_sr_gen[(ea >> 28) & 0xfu];
	g_itlb_sticky_ea = ea & ~0xfffu;
	g_itlb_sticky_pa = pa & ~0xfffu;
	g_itlb_sticky = 1;
}

int nw_jit_itlb_lookup(uint32_t ea, uint32_t *pa)
{
	if (g_itlb_sticky && g_itlb_sticky_ea == (ea & ~0xfffu)) {
		g_itlb_hit++;
		if (pa)
			*pa = g_itlb_sticky_pa | (ea & 0xfffu);
		return 1;
	}
	const unsigned i = (ea >> 12) & (NW_JIT_ITLB_N - 1u);
	if (!g_itlb[i].flags || g_itlb[i].ea_page != (ea & ~0xfffu) ||
	    g_itlb[i].sr_gen != g_sr_gen[(ea >> 28) & 0xfu]) {
		g_itlb_miss++;
		return 0;
	}
	g_itlb_hit++;
	g_itlb_sticky_ea = g_itlb[i].ea_page;
	g_itlb_sticky_pa = g_itlb[i].pa_page;
	g_itlb_sticky = 1;
	if (pa)
		*pa = g_itlb[i].pa_page | (ea & 0xfffu);
	return 1;
}

void nw_jit_itlb_drop_page(uint32_t ea)
{
	g_itlb_sticky = 0;
	const unsigned i = (ea >> 12) & (NW_JIT_ITLB_N - 1u);
	if (g_itlb[i].flags && g_itlb[i].ea_page == (ea & ~0xfffu))
		g_itlb[i].flags = 0;
}

void nw_jit_itlb_note_msr(uint32_t old_msr, uint32_t new_msr)
{
	if ((old_msr ^ new_msr) & 0x00000020u)	/* IR */
		nw_jit_itlb_flush();
}

uint64_t nw_jit_itlb_hits(void)
{
	return g_itlb_hit;
}

uint64_t nw_jit_itlb_misses(void)
{
	return g_itlb_miss;
}

uint64_t nw_jit_dtlb_misses(void)
{
	return g_dtlb_miss;
}

uint64_t nw_jit_exec_blocks(void)
{
	return g_exec_blocks;
}

uint64_t nw_jit_exec_insns(void)
{
	return g_exec_insns;
}

uint64_t nw_jit_chain_hops(void)
{
	return g_chain_hops;
}

void nw_jit_note_chain(int hops)
{
	if (hops > 0)
		g_chain_hops += (uint64_t)hops;
}

void nw_jit_note_exec_at(int n, uint32_t pc, int uses_vr)
{
	(void)pc;
	if (n <= 0)
		return;
	g_exec_blocks++;
	g_exec_insns += (uint64_t)n;
	/* Codec = AltiVec-class blocks only. Heap bands 0x01/0x0039/0x003d
	 * move between soaks and must not be the compared number. */
	if (uses_vr)
		g_codec_insns += (uint64_t)n;
	else
		g_other_insns += (uint64_t)n;
	if (g_pull_on && pc) {
		g_pull_jit += (uint64_t)n;
		if (uses_vr)
			g_pull_vr += (uint64_t)n;
		int slot = -1;
		int small = 0;
		for (int i = 0; i < 4; i++) {
			if (g_pull_pc[i].pc == pc) {
				g_pull_pc[i].n += (uint64_t)n;
				g_pull_pc[i].vr = uses_vr;
				slot = -2;
				break;
			}
			if (g_pull_pc[i].pc == 0) {
				slot = i;
				break;
			}
			if (g_pull_pc[i].n < g_pull_pc[small].n)
				small = i;
		}
		if (slot == -1 && (uint64_t)n > g_pull_pc[small].n)
			slot = small;
		if (slot >= 0) {
			g_pull_pc[slot].pc = pc;
			g_pull_pc[slot].n = (uint64_t)n;
			g_pull_pc[slot].vr = uses_vr;
		}
	}
}

void nw_jit_note_kcall_fast(void)
{
	g_kcall_fast++;
}

uint64_t nw_jit_kcall_fast(void)
{
	return g_kcall_fast;
}

uint64_t nw_jit_bat_total(void)
{
	return g_bat_total;
}

uint64_t nw_jit_bat_gen_bumps(void)
{
	return g_bat_bumps;
}

void nw_jit_note_exec(int n)
{
	nw_jit_note_exec_at(n, 0);
}

uint64_t nw_jit_codec_insns(void)
{
	return g_codec_insns;
}

uint64_t nw_jit_other_insns(void)
{
	return g_other_insns;
}

static int bo_is_cr(int bo)
{
	const int b = bo & ~1;	/* ignore likely bit */
	return b == NW_PPC_BO_TRUE || b == NW_PPC_BO_FALSE;
}

static int spr_is_user(uint32_t spr)
{
	return spr == NW_PPC_SPR_DEC || spr == NW_PPC_SPR_LR ||
	       spr == NW_PPC_SPR_CTR || spr == NW_PPC_SPR_XER;
}

static int spr_is_mfspr_ext(uint32_t spr)
{
	return spr == NW_PPC_SPR_TBL || spr == NW_PPC_SPR_TBU ||
	       spr == NW_PPC_SPR_PVR || spr == NW_PPC_SPR_VRSAVE ||
	       (spr >= NW_PPC_SPR_SPRG0 && spr <= NW_PPC_SPR_SPRG3);
}

int nw_jit_op_supported(uint32_t op)
{
	const int prim = (int)(op >> 26);
	const int rd = (int)((op >> 21) & 0x1f);
	const int xo = (int)((op >> 1) & 0x3ff);
	if (prim == 14 || prim == 12 || prim == 13)
		return 1;	/* addi / addic / addic. */
	if (prim == 15)
		return 1;	/* addis */
	if (prim == 7)
		return 1;	/* mulli */
	if (prim == 31 && (xo == 10 || xo == 522))
		return 1;	/* addc / addco */
	if (prim == 31 && (xo == 138 || xo == 650))
		return 1;	/* adde / addeo */
	if (prim == 31 && xo == 520)
		return 1;	/* subfco */
	if (prim == 31 && xo == 8)
		return 1;	/* subfc */
	if (prim == 31 && xo == 136)
		return 1;	/* subfe */
	if (prim == 31 && xo == 648)
		return 1;	/* subfeo: skip 648 is OE|subfe, not subfo */
	if (prim == 31 && xo == 40)
		return 1;	/* subf */
	if (prim == 31 && xo == 552)
		return 1;	/* subfo */
	if (prim == 28)
		return 1;	/* andi. */
	if (prim == 10)
		return (rd & 3) == 0;	/* cmpli L=0, any crfD */
	if (prim == 31 && xo == 144)
		return 1;	/* mtcrf */
	if (prim == 31 && xo == 19)
		return 1;	/* mfcr */
	if (prim == 31 && xo == 922)
		return 1;	/* extsh */
	if (prim == 31 && xo == 954)
		return 1;	/* extsb */
	if (prim == 31 && xo == 24)
		return 1;	/* slw */
	if (prim == 31 && xo == 536)
		return 1;	/* srw */
	if (prim == 31 && xo == 792)
		return 1;	/* sraw */
	if (prim == 31 && xo == 824)
		return 1;	/* srawi */
	if (prim == 31 && xo == 598)
		return 1;	/* sync */
	if (prim == 31 && xo == 566)
		return 1;	/* tlbsync */
	if (prim == 31 && xo == 822)
		return 1;	/* dss (kpx nop) */
	if (prim == 31 && (xo == 342 || xo == 374))
		return 1;	/* dst / dstst (kpx nop; T-bit dstt/dststt same XO) */
	if (prim == 31 && (xo == 278 || xo == 246 || xo == 86 || xo == 54 || xo == 470 || xo == 758))
		return 1;	/* dcbt / dcbtst / dcbf / dcbst / dcbi / dcba (kpx nop) */
	if (prim == 31 && xo == 854)
		return 1;	/* eieio */
	if (prim == 31 && xo == 1014)
		return 1;	/* dcbz */
	if (prim == 31 && xo == 210)
		return 1;	/* mtsr */
	if (prim == 3)
		return 1;	/* twi */
	if (prim == 31 && xo == 146)
		return 1;	/* mtmsr */
	if (prim == 19 && xo == 50)
		return 1;	/* rfi: block-end helper + ret */
	if (prim == 19 && xo == 150)
		return 1;	/* isync */
	if (prim == 11)
		return (rd & 3) == 0;	/* cmpi L=0, any crfD */
	if (prim == 20 || prim == 21)
		return 1;	/* rlwimi / rlwinm */
	if (prim == 23)
		return 1;	/* rlwnm */
	if (prim == 46 || prim == 47)
		return 1;	/* lmw / stmw */
	if (prim == 16)
		return 1;	/* bc: CR, CTR, LK, AA */
	if (prim == 18)
		return 1;	/* b / ba / bl / bla */
	if (prim == 19 && xo == 0)
		return 1;	/* mcrf */
	if (prim == 19 && xo == 33)
		return 1;	/* crnor */
	if (prim == 19 && xo == 193)
		return 1;	/* crxor */
	if (prim == 19 && xo == 289)
		return 1;	/* creqv */
	if (prim == 19 && xo == 449)
		return 1;	/* cror */
	if (prim == 19 && xo == 417)
		return 1;	/* crorc */
	if (prim == 19 && xo == 257)
		return 1;	/* crand */
	if (prim == 19 && xo == 129)
		return 1;	/* crandc */
	if (prim == 19 && xo == 225)
		return 1;	/* crnand */
	if (prim == 19 && (xo == 16 || xo == 528))
		return 1;	/* bclr / bcctr: all BO */
	if (prim == 31 && (xo == 266 || xo == 778))
		return 1;	/* add / addo */
	if (prim == 31 && xo == 444)
		return 1;	/* or / mr */
	if (prim == 31 && xo == 316)
		return 1;	/* xor */
	if (prim == 31 && xo == 284)
		return 1;	/* eqv */
	if (prim == 31 && xo == 476)
		return 1;	/* nand */
	if (prim == 31 && xo == 124)
		return 1;	/* nor */
	if (prim == 31 && xo == 28)
		return 1;	/* and */
	if (prim == 31 && xo == 26)
		return 1;	/* cntlzw */
	if (prim == 31 && xo == 104)
		return 1;	/* neg */
	if (prim == 24)
		return 1;	/* ori */
	if (prim == 25)
		return 1;	/* oris */
	if (prim == 31 && xo == 0)
		return (rd & 3) == 0;	/* cmp L=0, any crfD */
	if (prim == 31 && xo == 32)
		return (rd & 3) == 0;	/* cmpl L=0, any crfD */
	if (prim == 31 && xo == 339)
		return 1;	/* mfspr: user inline, else kpx mfspr_guest */
	if (prim == 31 && xo == 467)
		return 1;	/* mtspr: user inline, else kpx mtspr_guest */
	if (prim == 32 || prim == 33 || prim == 36 || prim == 37)
		return 1;	/* lwz / lwzu / stw / stwu */
	if (prim == 31 && (xo == 20 || xo == 150))
		return 1;	/* lwarx / stwcx. */
	if (prim == 31 && (xo == 533 || xo == 661))
		return 1;	/* lswx / stswx */
	if (prim == 31 && (xo == 597 || xo == 725))
		return 1;	/* lswi / stswi: EA=RA_or_0, NB=0→32 */
	if (prim == 31 && xo == 982)
		return 1;	/* icbi */
	if (prim == 31 && xo == 306)
		return 1;	/* tlbie */
	if (prim == 31 && xo == 202)
		return 1;	/* addze */
	if (prim == 31 && xo == 200)
		return 1;	/* subfze */
	if (prim == 31 && xo == 234)
		return 1;	/* addme */
	if (prim == 31 && xo == 232)
		return 1;	/* subfme */
	if (prim == 31 && xo == 412)
		return 1;	/* orc */
	if (prim == 31 && xo == 512)
		return 1;	/* mcrxr */
	if (prim == 31 && xo == 595)
		return 1;	/* mfsr */
	if (prim == 31 && xo == 4)
		return 1;	/* tw */
	if (prim == 31 && xo == 370)
		return 1;	/* tlbia */
	if (prim == 34 || prim == 35 || prim == 38)
		return 1;	/* lbz / lbzu / stb */
	if (prim == 39)
		return 1;	/* stbu */
	if (prim == 31 && xo == 87)
		return 1;	/* lbzx */
	if (prim == 31 && xo == 215)
		return 1;	/* stbx */
	if (prim == 31 && xo == 247)
		return ((op >> 16) & 0x1f) != 0;	/* stbux; RA≠0 */
	if (prim == 31 && (xo == 103 || xo == 359))
		return 1;	/* lvx / lvxl (hint ignored) */
	if (prim == 31 && (xo == 231 || xo == 487))
		return 1;	/* stvx / stvxl (hint ignored) */
	if (prim == 31 && xo == 23)
		return 1;	/* lwzx */
	if (prim == 31 && xo == 151)
		return 1;	/* stwx */
	if (prim == 31 && xo == 183)
		return ((op >> 16) & 0x1f) != 0;	/* stwux; RA≠0 */
	if (prim == 31 && xo == 55)
		return ((op >> 16) & 0x1f) != 0;	/* lwzux; RA≠0 */
	if (prim == 31 && xo == 119)
		return ((op >> 16) & 0x1f) != 0;	/* lbzux; RA≠0 */
	if (prim == 31 && xo == 407)
		return 1;	/* sthx */
	if (prim == 31 && xo == 439)
		return ((op >> 16) & 0x1f) != 0;	/* sthux; RA≠0 */
	if (prim == 31 && (xo == 343 || xo == 375))
		return 1;	/* lhax / lhaux */
	if (prim == 40 || prim == 42 || prim == 43 || prim == 44)
		return 1;	/* lhz / lha / lhau / sth */
	if (prim == 41)
		return ((op >> 16) & 0x1f) != 0;	/* lhzu; RA≠0 */
	if (prim == 45)
		return 1;	/* sthu */
	if (prim == 50)
		return 1;	/* lfd */
	if (prim == 54)
		return 1;	/* stfd */
	if (prim == 48)
		return 1;	/* lfs */
	if (prim == 52)
		return 1;	/* stfs */
	if (prim == 49 || prim == 51 || prim == 53 || prim == 55)
		return ((op >> 16) & 0x1f) != 0;	/* lfsu / lfdu / stfsu / stfdu; RA≠0 */
	if (prim == 31 && (xo == 535 || xo == 663))
		return 1;	/* lfsx / stfsx */
	if (prim == 31 && (xo == 599 || xo == 727))
		return 1;	/* lfdx / stfdx: codec inner loop */
	if (prim == 31 && (xo == 567 || xo == 631 || xo == 695 || xo == 759))
		return ((op >> 16) & 0x1f) != 0;	/* lfsux / lfdux / stfsux / stfdux; RA≠0 */
	if (prim == 59) {
		const int axo = (int)((op >> 1) & 0x1f);
		if (axo == 18 || axo == 20 || axo == 21 || axo == 24 || axo == 25 || axo == 28 || axo == 29 || axo == 30 || axo == 31)
			return 1;	/* +fres +fnmadds; Rc=0/1 */
	}
	if (prim == 63 && xo == 40)
		return 1;	/* fneg */
	if (prim == 63 && xo == 72)
		return 1;	/* fmr */
	if (prim == 63 && xo == 12)
		return 1;	/* frsp */
	if (prim == 63 && xo == 583)
		return 1;	/* mffs */
	if (prim == 63 && xo == 15)
		return 1;	/* fctiwz */
	if (prim == 63 && xo == 14)
		return 1;	/* fctiw */
	if (prim == 63 && xo == 32)
		return 1;	/* fcmpo */
	if (prim == 63 && xo == 0)
		return 1;	/* fcmpu */
	if (prim == 63 && xo == 264)
		return 1;	/* fabs */
	if (prim == 63 && xo == 136)
		return 1;	/* fnabs */
	if (prim == 63 && xo == 711)
		return 1;	/* mtfsf */
	if (prim == 63 && xo == 70)
		return 1;	/* mtfsb0 */
	if (prim == 63 && xo == 38)
		return 1;	/* mtfsb1 */
	if (prim == 63 && xo == 134)
		return 1;	/* mtfsfi */
	if (prim == 63 && xo == 64)
		return 1;	/* mcrfs */
	if (prim == 63) {
		const int axo = (int)((op >> 1) & 0x1f);
		if (axo == 18 || axo == 20 || axo == 21 || axo == 23 || axo == 25 || axo == 26 || axo == 28 || axo == 29 || axo == 30 || axo == 31)
			return 1;	/* +fsel +frsqrte +fmsub +fnmadd; Rc=0/1 */
	}
	if (prim == 31 && (xo == 6 || xo == 38))
		return 1;	/* lvsl / lvsr */
	if (prim == 4)
		return 1;	/* AltiVec: named helpers or kpx helper_vmx */
	if (prim == 31 && xo == 235)
		return 1;	/* mullw */
	if (prim == 31 && xo == 747)
		return 1;	/* mullwo */
	if (prim == 31 && xo == 459)
		return 1;	/* divwu */
	if (prim == 31 && xo == 971)
		return 1;	/* divwuo: unsigned /0 → 0, OV+SO */
	if (prim == 31 && xo == 491)
		return 1;	/* divw */
	if (prim == 31 && xo == 1003)
		return 1;	/* divwo */
	if (prim == 8)
		return 1;	/* subfic */
	if (prim == 31 && xo == 83)
		return 1;	/* mfmsr */
	if (prim == 31 && xo == 371) {
		const uint32_t tbr = spr_num(op);
		return tbr == NW_PPC_SPR_TBL || tbr == NW_PPC_SPR_TBU;
	}
	if (prim == 17)
		return 1;	/* sc */
	if (prim == 31 && xo == 11)
		return 1;	/* mulhwu */
	if (prim == 31 && xo == 75)
		return 1;	/* mulhw / mulhw. */
	if (prim == 31 && xo == 279)
		return 1;	/* lhzx */
	if (prim == 31 && xo == 311)
		return ((op >> 16) & 0x1f) != 0;	/* lhzux; RA≠0 */
	if (prim == 31 && xo == 534)
		return 1;	/* lwbrx */
	if (prim == 31 && xo == 790)
		return 1;	/* lhbrx */
	if (prim == 31 && xo == 918)
		return 1;	/* sthbrx */
	if (prim == 31 && xo == 662)
		return 1;	/* stwbrx */
	if (prim == 31 && (xo == 7 || xo == 39 || xo == 71))
		return 1;	/* lvebx / lvehx / lvewx */
	if (prim == 31 && (xo == 135 || xo == 167 || xo == 199))
		return 1;	/* stvebx / stvehx / stvewx */
	if (prim == 31 && xo == 242)
		return 1;	/* mtsrin */
	if (prim == 31 && xo == 659)
		return 1;	/* mfsrin */
	if (prim == 31 && xo == 60)
		return 1;	/* andc */
	if (prim == 26)
		return 1;	/* xori */
	if (prim == 27)
		return 1;	/* xoris */
	if (prim == 29)
		return 1;	/* andis. */
	return 0;
}

int nw_jit_op_dispatch(uint32_t op)
{
	return nw_jit_op_supported(op);
}

static struct nw_jit_hist *hist_slot(uint32_t op)
{
	const int prim = (int)(op >> 26);
	const int xo = (int)((op >> 1) & 0x3ff);
	for (size_t i = 0; i < sizeof(g_hist) / sizeof(g_hist[0]); i++) {
		if (g_hist[i].prim != prim)
			continue;
		if (g_hist[i].xo >= 0 && g_hist[i].xo != xo)
			continue;
		return &g_hist[i];
	}
	return NULL;
}

void nw_jit_verify_note(const uint32_t *ops, int n, int miss)
{
	g_v_cmp++;
	if (n >= 1 && n <= NW_JIT_MAX_BLOCK)
		g_v_blocks[n]++;
	if (miss)
		g_v_miss++;
	for (int i = 0; i < n; i++) {
		struct nw_jit_hist *h = hist_slot(ops[i]);
		if (h) {
			h->n++;
			h->insns++;
			if (miss && i == 0)
				h->miss++;
		} else {
			g_v_other++;
			if (miss && i == 0)
				g_v_other_miss++;
		}
	}
}

void nw_jit_verify_fail(void)
{
	g_v_fail++;
}

void nw_jit_verify_skip(int mem)
{
	if (mem)
		g_v_skip_mem++;
	else
		g_v_skip_unsup++;
}

void nw_jit_note_skip_unsup(uint32_t op, unsigned packed, uint32_t pc)
{
	g_v_skip_unsup++;
	const int prim = (int)(op >> 26);
	if (g_pull_on) {
		const uint64_t add = packed ? (uint64_t)packed : 1ull;
		g_pull_skip += add;
		if (prim == 4)
			g_pull_vmx += add;
		if (prim == 6)
			g_pull_op6 += add;
		if (pc >= 0x1de00000u && pc < 0x1e000000u)
			g_it_skip += add;
		const int xo = (prim == 4 || prim == 19 || prim == 31 || prim == 59 || prim == 63)
				       ? (int)((op >> 1) & 0x3ff) : -1;
		int slot = -1;
		int small = 0;
		for (int i = 0; i < 4; i++) {
			if (g_pull_sk[i].n && g_pull_sk[i].prim == prim && g_pull_sk[i].xo == xo) {
				g_pull_sk[i].n += add;
				slot = -2;
				break;
			}
			if (g_pull_sk[i].n == 0) {
				slot = i;
				break;
			}
			if (g_pull_sk[i].n < g_pull_sk[small].n)
				small = i;
		}
		if (slot == -1 && add > g_pull_sk[small].n)
			slot = small;
		if (slot >= 0) {
			g_pull_sk[slot].prim = prim;
			g_pull_sk[slot].xo = xo;
			g_pull_sk[slot].pc = pc;
			g_pull_sk[slot].n = add;
		}
	}
	const int xo = (prim == 4 || prim == 19 || prim == 31 || prim == 59 || prim == 63)
			       ? (int)((op >> 1) & 0x3ff) : -1;
	const uint64_t add = packed ? (uint64_t)packed : 1ull;
	const uint32_t h = (uint32_t)prim * 0x9e3779b1u ^ (uint32_t)(xo + 1) * 0x85ebca6bu;
	int i = (int)(h & (NW_JIT_SKIPN - 1));
	for (int n = 0; n < 8; n++) {
		int j = (i + n) & (NW_JIT_SKIPN - 1);
		if (g_skip[j].n == 0 ||
		    (g_skip[j].prim == prim && g_skip[j].xo == xo)) {
			if (g_skip[j].n == 0) {
				g_skip[j].op = op;
				g_skip[j].pc = pc;
			}
			g_skip[j].prim = prim;
			g_skip[j].xo = xo;
			g_skip[j].n++;
			g_skip[j].lost += add;
			return;
		}
	}
	g_skip[i].prim = prim;
	g_skip[i].xo = xo;
	g_skip[i].n = 1;
	g_skip[i].lost = add;
	g_skip[i].op = op;
	g_skip[i].pc = pc;
}

static const char *skip_raw_name(uint32_t op)
{
	const int prim = (int)(op >> 26);
	if (prim == 6)
		return "op6";
	if (prim != 4)
		return "?";
	const int vaxo = (int)(op & 0x3f);
	if (vaxo == 44)
		return "vsldoi";
	if (vaxo == 34)
		return "vmladduhm";
	const int vxo = (int)(op & 0x7ff);
	switch (vxo) {
	case 6: return "vcmpequb";
	case 1030: return "vcmpequb.";
	case 12: return "vmrghb";
	case 268: return "vmrglb";
	case 134: return "vcmpequw";
	case 1024: return "vsububm";
	case 1092: return "vandc";
	case 1220: return "vxor";
	case 324: return "vslh";
	case 452: return "vsl";
	case 1036: return "vslo";
	case 524: return "vspltb";
	case 1158: return "vcmpequw.";
	case 258: return "vmaxsb";
	case 260: return "vslb";
	case 652: return "vspltw";
	case 708: return "vsr";
	case 644: return "vsrw";
	case 908: return "vspltisw";
	case 770: return "vminsb";
	case 780: return "vspltisb";
	case 1100: return "vsro";
	case 1540: return "mfvscr";
	case 1604: return "mtvscr";
	case 1856: return "vsubshs";
	default: return "?";
	}
}

void nw_jit_skip_raw_once(uint32_t op, uint32_t pc)
{
	const int prim = (int)(op >> 26);
	const int xo = (prim == 4 || prim == 19 || prim == 31 || prim == 59 || prim == 63)
			       ? (int)((op >> 1) & 0x3ff) : -1;
	unsigned bit = 0;
	if (prim == 4 && xo == 354)
		bit = 1u;
	else if (prim == 4 && xo == 390)
		bit = 2u;
	else if (prim == 4 && xo == 406)
		bit = 4u;
	else if (prim == 4 && xo == 802)
		bit = 8u;
	else if (prim == 6)
		bit = 16u;
	else
		return;
	if (g_skip_raw_seen & bit)
		return;
	g_skip_raw_seen |= bit;
	g_skip_raw_op = op;
	g_skip_raw_pc = pc;
	const char *nm = skip_raw_name(op);
	snprintf(g_skip_raw_nm, sizeof(g_skip_raw_nm), "%s", nm);
	printf("NW-BOOT G1: jit skip_raw prim=%d xo=%d op=%08x pc=%08x %s\n",
	       prim, xo, (unsigned)op, (unsigned)pc, nm);
	fflush(stdout);
}

int nw_jit_skip_raw_last(uint32_t *op, uint32_t *pc, char *name, size_t n)
{
	if (!g_skip_raw_seen)
		return 0;
	if (op)
		*op = g_skip_raw_op;
	if (pc)
		*pc = g_skip_raw_pc;
	if (name && n) {
		snprintf(name, n, "%s", g_skip_raw_nm);
	}
	return 1;
}

uint64_t nw_jit_skip_n(uint32_t op)
{
	const int prim = (int)(op >> 26);
	const int xo = (prim == 4 || prim == 19 || prim == 31 || prim == 59 || prim == 63)
			       ? (int)((op >> 1) & 0x3ff) : -1;
	for (int i = 0; i < NW_JIT_SKIPN; i++) {
		if (g_skip[i].n && g_skip[i].prim == prim && g_skip[i].xo == xo)
			return g_skip[i].n;
	}
	return 0;
}

uint64_t nw_jit_skip_lost(uint32_t op)
{
	const int prim = (int)(op >> 26);
	const int xo = (prim == 4 || prim == 19 || prim == 31 || prim == 59 || prim == 63)
			       ? (int)((op >> 1) & 0x3ff) : -1;
	for (int i = 0; i < NW_JIT_SKIPN; i++) {
		if (g_skip[i].n && g_skip[i].prim == prim && g_skip[i].xo == xo)
			return g_skip[i].lost;
	}
	return 0;
}

uint32_t nw_jit_skip_op(uint32_t op)
{
	const int prim = (int)(op >> 26);
	const int xo = (prim == 4 || prim == 19 || prim == 31 || prim == 59 || prim == 63)
			       ? (int)((op >> 1) & 0x3ff) : -1;
	for (int i = 0; i < NW_JIT_SKIPN; i++) {
		if (g_skip[i].n && g_skip[i].prim == prim && g_skip[i].xo == xo)
			return g_skip[i].op;
	}
	return 0;
}

uint32_t nw_jit_skip_pc(uint32_t op)
{
	const int prim = (int)(op >> 26);
	const int xo = (prim == 4 || prim == 19 || prim == 31 || prim == 59 || prim == 63)
			       ? (int)((op >> 1) & 0x3ff) : -1;
	for (int i = 0; i < NW_JIT_SKIPN; i++) {
		if (g_skip[i].n && g_skip[i].prim == prim && g_skip[i].xo == xo)
			return g_skip[i].pc;
	}
	return 0;
}

void nw_jit_note_cut(int reason)
{
	if (reason >= 0 && reason < NW_JIT_CUT_N)
		g_cut[reason]++;
	if (g_pull_on && reason == NW_JIT_CUT_CLASS_CHANGE)
		g_pull_class++;
}

void nw_jit_note_hop_stop(int reason)
{
	if (reason >= 0 && reason < NW_JIT_HOP_N) {
		g_hop_stop[reason]++;
		if (g_pull_on)
			g_pull_hop[reason]++;
	}
}

uint64_t nw_jit_cut_count(int reason)
{
	if (reason < 0 || reason >= NW_JIT_CUT_N)
		return 0;
	return g_cut[reason];
}

uint64_t nw_jit_hop_stop_count(int reason)
{
	if (reason < 0 || reason >= NW_JIT_HOP_N)
		return 0;
	return g_hop_stop[reason];
}

void nw_jit_pull_set(int on)
{
	g_pull_on = on ? 1 : 0;
}

static int itunes_pc(uint32_t pc)
{
	return pc >= 0x1de00000u && pc < 0x1e000000u;
}

void nw_jit_itunes_note(uint32_t pc, int n, int kind, uint64_t host_us)
{
	if (!itunes_pc(pc))
		return;
	if (kind == 0 && n > 0)
		g_it_jit += (uint64_t)n;
	else if (kind == 1)
		g_it_interp++;
	else if (kind == 2)
		g_it_class++;
	else if (kind == 3)
		g_it_fp++;
	g_it_us += host_us;
}

void nw_jit_itunes_log(uint64_t frames)
{
	const double audio_s = (double)frames / 44100.0;
	const double host_s = (double)g_it_us / 1000000.0;
	const double ratio = audio_s > 0.001 ? host_s / audio_s : 0.0;
	printf("NW-BOOT G1: itunes-cost jit=%llu interp=%llu host_us=%llu frames=%llu host_per_audio=%.3f class_change=%llu fp_gate=%llu skip=%llu\n",
	       (unsigned long long)g_it_jit, (unsigned long long)g_it_interp,
	       (unsigned long long)g_it_us, (unsigned long long)frames, ratio,
	       (unsigned long long)g_it_class, (unsigned long long)g_it_fp,
	       (unsigned long long)g_it_skip);
	g_it_jit = g_it_interp = g_it_us = g_it_class = g_it_fp = g_it_skip = 0;
}

void nw_jit_pull_log(void)
{
	static const char *const hop_name[NW_JIT_HOP_N] = {
		"cap", "no_chain_pc", "pc_mismatch", "itlb_miss", "aline",
		"cache_miss", "vec_gate", "fp_gate", "compile_null"
	};
	printf("NW-BOOT G1: sb-pull jit=%llu vr=%llu skip=%llu vmx=%llu op6=%llu class_change=%llu",
	       (unsigned long long)g_pull_jit, (unsigned long long)g_pull_vr,
	       (unsigned long long)g_pull_skip, (unsigned long long)g_pull_vmx,
	       (unsigned long long)g_pull_op6, (unsigned long long)g_pull_class);
	for (int i = 0; i < NW_JIT_HOP_N; i++) {
		if (g_pull_hop[i])
			printf(" %s=%llu", hop_name[i], (unsigned long long)g_pull_hop[i]);
	}
	printf("\n");
	printf("NW-BOOT G1: sb-pull pc");
	for (int i = 0; i < 4; i++) {
		if (g_pull_pc[i].pc)
			printf(" %08x:%llu%s", g_pull_pc[i].pc,
			       (unsigned long long)g_pull_pc[i].n,
			       g_pull_pc[i].vr ? ":vr" : "");
	}
	printf("\n");
	printf("NW-BOOT G1: sb-pull skip");
	for (int i = 0; i < 4; i++) {
		if (g_pull_sk[i].n)
			printf(" prim=%d xo=%d pc=%08x n=%llu",
			       g_pull_sk[i].prim, g_pull_sk[i].xo, g_pull_sk[i].pc,
			       (unsigned long long)g_pull_sk[i].n);
	}
	printf("\n");
	g_pull_jit = g_pull_vr = g_pull_skip = g_pull_vmx = g_pull_op6 = g_pull_class = 0;
	memset(g_pull_hop, 0, sizeof g_pull_hop);
	memset(g_pull_pc, 0, sizeof g_pull_pc);
	memset(g_pull_sk, 0, sizeof g_pull_sk);
}

void nw_jit_note_skip_io(uint32_t ea, uint32_t pc)
{
	const uint32_t page = ea & ~0xfffu;
	int i = (int)(((page >> 12) ^ (pc >> 4)) & (NW_JIT_IOH - 1u));
	int cold = i;
	uint64_t cold_n = ~(uint64_t)0;
	for (int p = 0; p < 4; p++) {
		int j = (i + p) & (NW_JIT_IOH - 1);
		if (g_io_h[j].n == 0 ||
		    (g_io_h[j].page == page && g_io_h[j].pc == pc)) {
			g_io_h[j].page = page;
			g_io_h[j].pc = pc;
			g_io_h[j].n++;
			return;
		}
		if (g_io_h[j].n < cold_n) {
			cold_n = g_io_h[j].n;
			cold = j;
		}
	}
	g_io_h[cold].page = page;
	g_io_h[cold].pc = pc;
	g_io_h[cold].n = 1;
}

void nw_jit_verify_uncompared(int fault)
{
	if (fault == 2)
		g_v_skip_io++;
	else
		g_v_skip_dsi++;
}

void nw_jit_verify_dump(const char *why)
{
	if (!g_v_cmp && !g_v_skip_unsup && !g_v_skip_mem && !g_v_fail &&
	    !g_v_skip_dsi && !g_v_skip_io) {
		nw_jit_pc_hot_dump(why);
		return;
	}
	uint64_t n_blr = 0, n_mfspr = 0, n_mtspr = 0, n_lwz = 0, n_stw = 0;
	for (size_t i = 0; i < sizeof(g_hist) / sizeof(g_hist[0]); i++) {
		if (g_hist[i].prim == 19)
			n_blr = g_hist[i].n;
		if (g_hist[i].prim == 31 && g_hist[i].xo == 339)
			n_mfspr = g_hist[i].n;
		if (g_hist[i].prim == 31 && g_hist[i].xo == 467)
			n_mtspr = g_hist[i].n;
		if (g_hist[i].prim == 32)
			n_lwz = g_hist[i].n;
		if (g_hist[i].prim == 36)
			n_stw = g_hist[i].n;
	}
	printf("NW-BOOT G1: jit verify %s cmp %llu miss %llu fail %llu skip_unsup %llu skip_mem %llu skip_dsi %llu skip_io %llu blr %llu mfspr %llu mtspr %llu lwz %llu stw %llu\n",
	       why ? why : "?",
	       (unsigned long long)g_v_cmp, (unsigned long long)g_v_miss,
	       (unsigned long long)g_v_fail,
	       (unsigned long long)g_v_skip_unsup, (unsigned long long)g_v_skip_mem,
	       (unsigned long long)g_v_skip_dsi, (unsigned long long)g_v_skip_io,
	       (unsigned long long)n_blr, (unsigned long long)n_mfspr,
	       (unsigned long long)n_mtspr,
	       (unsigned long long)n_lwz, (unsigned long long)n_stw);
	if (why && strcmp(why, "periodic") == 0) {
		fflush(stdout);
		return;
	}
	for (size_t i = 0; i < sizeof(g_hist) / sizeof(g_hist[0]); i++)
		printf("NW-BOOT G1: jit verify %s n=%llu miss=%llu\n",
		       g_hist[i].name,
		       (unsigned long long)g_hist[i].n,
		       (unsigned long long)g_hist[i].miss);
	if (g_v_other)
		printf("NW-BOOT G1: jit verify other n=%llu miss=%llu\n",
		       (unsigned long long)g_v_other, (unsigned long long)g_v_other_miss);
	for (int i = 1; i <= NW_JIT_MAX_BLOCK; i++) {
		if (g_v_blocks[i])
			printf("NW-BOOT G1: jit verify blocklen %d n=%llu\n",
			       i, (unsigned long long)g_v_blocks[i]);
	}
	nw_jit_pc_hot_dump(why);
	fflush(stdout);
}

void nw_jit_pc_hot(uint32_t pc, uint32_t op)
{
	int i = (int)((pc >> 2) & (NW_JIT_PCHOT - 1));
	int cold = i;
	uint64_t cold_n = ~(uint64_t)0;
	for (int p = 0; p < NW_JIT_PCPROBE; p++) {
		int j = (i + p) & (NW_JIT_PCHOT - 1);
		if (g_pchot[j].n == 0 || g_pchot[j].pc == pc) {
			g_pchot[j].pc = pc;
			if (!g_pchot[j].op)
				g_pchot[j].op = op;
			g_pchot[j].n++;
			return;
		}
		if (g_pchot[j].n < cold_n) {
			cold_n = g_pchot[j].n;
			cold = j;
		}
	}
	g_pchot[cold].pc = pc;
	g_pchot[cold].op = op;
	g_pchot[cold].n = 1;
}

void nw_jit_pc_hot_dump(const char *why)
{
	int top[NW_JIT_PCTOP];
	int ntop = 0;
	for (int i = 0; i < NW_JIT_PCHOT; i++) {
		if (!g_pchot[i].n)
			continue;
		int k = ntop;
		while (k > 0 && g_pchot[i].n > g_pchot[top[k - 1]].n)
			k--;
		if (k >= NW_JIT_PCTOP)
			continue;
		int n = ntop < NW_JIT_PCTOP ? ntop : NW_JIT_PCTOP - 1;
		for (int j = n; j > k; j--)
			top[j] = top[j - 1];
		top[k] = i;
		if (ntop < NW_JIT_PCTOP)
			ntop++;
	}
	for (int i = 0; i < ntop; i++) {
		int j = top[i];
		printf("NW-BOOT G1: jit pc-hot %s pc=%08x op=%08x n=%llu\n",
		       why ? why : "?",
		       (unsigned)g_pchot[j].pc, (unsigned)g_pchot[j].op,
		       (unsigned long long)g_pchot[j].n);
	}
	if (ntop)
		fflush(stdout);
}

int nw_jit_op_ends_block(uint32_t op)
{
	const int prim = (int)(op >> 26);
	const int xo = (int)((op >> 1) & 0x3ff);
	const int vxo = (int)(op & 0x7ff);
	return prim == 16 || prim == 18 || prim == 17 ||
	       (prim == 19 && (xo == 16 || xo == 528 || xo == 150 || xo == 50)) ||
	       (prim == 31 && xo == 146) ||
	       (prim == 31 && xo == 982) ||
	       (prim == 31 && xo == 306) ||
	       (prim == 31 && xo == 370) ||
	       (prim == 31 && xo == 210) ||
	       (prim == 31 && xo == 242) ||
	       (prim == 31 && xo == 339 &&
		!spr_is_user(spr_num(op)) && !spr_is_mfspr_ext(spr_num(op))) ||
	       (prim == 4 && (vxo == 1036 || vxo == 1100)); /* vslo / vsro: keep one-op; A/B Starting Up lock */
}

uint32_t nw_jit_op_gpr_mask(uint32_t op)
{
	const int prim = (int)(op >> 26);
	const int rd = (int)((op >> 21) & 0x1f);
	const int ra = (int)((op >> 16) & 0x1f);
	const int rb = (int)((op >> 11) & 0x1f);
	const int xo = (int)((op >> 1) & 0x3ff);
	const uint32_t rd_b = 1u << rd;
	const uint32_t ra_b = ra ? (1u << ra) : 0;
	const uint32_t rb_b = 1u << rb;
	switch (prim) {
	case 16:
	case 17:
	case 18:
	case 19:
		return 0;
	case 7: case 8: case 12: case 13: case 14: case 15:
		return rd_b | ra_b;
	case 10: case 11:
		return ra_b;
	case 20: case 21:
		return (1u << ra) | rd_b;
	case 23:
		return (1u << ra) | rd_b | rb_b;
	case 24: case 25: case 26: case 27: case 28: case 29:
		return (1u << ra) | rd_b;
	case 32: case 33: case 34: case 35: case 40: case 41: case 42: case 43:
		return rd_b | ra_b;
	case 36: case 37: case 38: case 39: case 44: case 45:
		return rd_b | ra_b;
	case 46: {
		uint32_t m = ra_b;
		for (int i = rd; i < 32; i++)
			m |= 1u << i;
		return m;
	}
	case 47: {
		uint32_t m = ra_b;
		for (int i = rd; i < 32; i++)
			m |= 1u << i;
		return m;
	}
	case 48: case 49: case 50: case 51: case 52: case 53: case 54: case 55:
		return ra_b;
	case 59: case 63:
		return 0;
	case 4:
		if (xo == 7 || xo == 39 || xo == 71 || xo == 103 || xo == 359 ||
		    xo == 135 || xo == 167 || xo == 199 || xo == 231 || xo == 487 ||
		    xo == 6 || xo == 38)
			return ra_b | rb_b;
		return 0;
	case 31: {
		if (xo == 597 || xo == 725) {
			int nb = rb & 0x1f;
			if (nb == 0)
				nb = 32;
			int nreg = (nb + 3) / 4;
			uint32_t m = ra_b;
			for (int i = 0; i < nreg; i++)
				m |= 1u << ((rd + i) & 31);
			return m;
		}
		if (xo == 339 || xo == 467 || xo == 83)
			return rd_b;
		if (xo == 19)
			return rd_b;
		if (xo == 144)
			return rd_b;
		if (xo == 0 || xo == 32)
			return ra_b | rb_b;
		return 0xffffffffu;
	}
	default:
		return 0xffffffffu;
	}
}

nw_jit_fn nw_jit_cache_get(uint32_t phys_page, uint32_t guest_pc,
			  uint32_t msr_ir, uint32_t endian, int *n_out,
			  int *uses_fpr, int *uses_vr, uint32_t *chain_pc,
			  uint32_t *gpr_mask, int16_t *chain_disp)
{
	int i = cache_slot(phys_page, guest_pc, msr_ir, endian);
	static uint32_t hit_sample;
	for (int n = 0; n < NW_JIT_PROBE; n++) {
		int j = (i + n) & (NW_JIT_CACHE - 1);
		if (g_cache[j].used == NW_JIT_USED_EMPTY)
			break;
		if (g_cache[j].used == NW_JIT_USED_TOMB)
			continue;
		if (g_cache[j].phys_page == phys_page && g_cache[j].guest_pc == guest_pc &&
		    g_cache[j].msr_ir == msr_ir && g_cache[j].endian == endian) {
			if ((++hit_sample & 255u) == 0 && g_cache[j].hits < 0xffffu)
				g_cache[j].hits++;
			if (n_out)
				*n_out = g_cache[j].n;
			if (uses_fpr)
				*uses_fpr = g_cache[j].uses_fpr;
			if (uses_vr)
				*uses_vr = g_cache[j].uses_vr;
			if (chain_pc)
				*chain_pc = g_cache[j].chain_pc;
			if (gpr_mask)
				*gpr_mask = g_cache[j].gpr_mask;
			if (chain_disp)
				*chain_disp = g_cache[j].chain_disp;
			return g_cache[j].fn;
		}
	}
	return NULL;
}

void nw_jit_cache_put(uint32_t phys_page, uint32_t guest_pc, uint32_t msr_ir,
		      uint32_t endian, nw_jit_fn fn, int n,
		      uint32_t first_opcode, int uses_fpr, int uses_vr,
		      uint32_t chain_pc, uint32_t gpr_mask, int16_t chain_disp,
		      uint32_t code_bytes)
{
	int i = cache_slot(phys_page, guest_pc, msr_ir, endian);
	int slot = -1, reuse = -1;
	for (int p = 0; p < NW_JIT_PROBE; p++) {
		int j = (i + p) & (NW_JIT_CACHE - 1);
		if (g_cache[j].used == NW_JIT_USED_EMPTY) {
			if (reuse < 0)
				reuse = j;
			break;
		}
		if (g_cache[j].used == NW_JIT_USED_TOMB) {
			if (reuse < 0)
				reuse = j;
			continue;
		}
		if (g_cache[j].phys_page == phys_page && g_cache[j].guest_pc == guest_pc &&
		    g_cache[j].msr_ir == msr_ir && g_cache[j].endian == endian) {
			slot = j;
			break;
		}
	}
	if (slot < 0) {
		if (reuse >= 0)
			slot = reuse;
		else {
			int best = i;
			uint16_t best_h = 0xffffu;
			for (int p = 0; p < NW_JIT_PROBE; p++) {
				int j = (i + p) & (NW_JIT_CACHE - 1);
				if (g_cache[j].hits < best_h) {
					best_h = g_cache[j].hits;
					best = j;
				}
			}
			slot = best;
		}
	}
	const int same = g_cache[slot].used == NW_JIT_USED_LIVE &&
		g_cache[slot].phys_page == phys_page &&
		g_cache[slot].guest_pc == guest_pc &&
		g_cache[slot].msr_ir == msr_ir &&
		g_cache[slot].endian == endian;
	if (g_cache[slot].used == NW_JIT_USED_LIVE && !same)
		g_evict++;
	g_cache[slot].phys_page = phys_page;
	g_cache[slot].guest_pc = guest_pc;
	g_cache[slot].msr_ir = msr_ir;
	g_cache[slot].endian = endian;
	g_cache[slot].fn = fn;
	g_cache[slot].first_opcode = first_opcode;
	g_cache[slot].uses_fpr = uses_fpr ? 1 : 0;
	g_cache[slot].uses_vr = uses_vr ? 1 : 0;
	g_cache[slot].chain_pc = chain_pc;
	g_cache[slot].chain_disp = chain_disp;
	g_cache[slot].gpr_mask = gpr_mask;
	g_cache[slot].code_bytes = code_bytes;
	g_cache[slot].used = NW_JIT_USED_LIVE;
	g_cache[slot].n = (uint8_t)(n < 0 ? 0 : n > 255 ? 255 : n);
	if (!same) {
		g_cache[slot].hits = (guest_pc >= 0x68000000u && guest_pc < 0x68c00000u)
			? 64 : 1;
	} else if (g_cache[slot].hits < 0xffffu)
		g_cache[slot].hits++;
	pagebit_set(phys_page);
}

uint32_t nw_ppc_addi(int rd, int ra, int simm)
{
	return (14u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) | ((uint32_t)simm & 0xffffu);
}

uint32_t nw_ppc_addis(int rd, int ra, int simm)
{
	return (15u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) | ((uint32_t)simm & 0xffffu);
}

uint32_t nw_ppc_mulli(int rd, int ra, int simm)
{
	return (7u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)simm & 0xffffu);
}

uint32_t nw_ppc_addic(int rd, int ra, int simm, int rc)
{
	return ((rc ? 13u : 12u) << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)simm & 0xffffu);
}

uint32_t nw_ppc_addc(int rd, int ra, int rb, int rc)
{
	return (31u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (10u << 1) | (rc ? 1u : 0);
}

uint32_t nw_ppc_addco(int rd, int ra, int rb, int rc)
{
	return nw_ppc_addc(rd, ra, rb, rc) | (1u << 10);
}

uint32_t nw_ppc_andi_dot(int ra, int rs, unsigned uimm)
{
	return (28u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) | (uimm & 0xffffu);
}

uint32_t nw_ppc_andis_dot(int ra, int rs, unsigned uimm)
{
	return (29u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) | (uimm & 0xffffu);
}

uint32_t nw_ppc_subfco(int rd, int ra, int rb, int rc)
{
	return (31u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (1u << 10) | (8u << 1) | (rc ? 1u : 0);
}

uint32_t nw_ppc_subfe(int rd, int ra, int rb, int rc)
{
	return (31u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (136u << 1) | (rc ? 1u : 0);
}

uint32_t nw_ppc_subfeo(int rd, int ra, int rb, int rc)
{
	return nw_ppc_subfe(rd, ra, rb, rc) | (1u << 10);
}

uint32_t nw_ppc_subf(int rd, int ra, int rb, int rc)
{
	return (31u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (40u << 1) | (rc ? 1u : 0);
}

uint32_t nw_ppc_subfo(int rd, int ra, int rb, int rc)
{
	return nw_ppc_subf(rd, ra, rb, rc) | (1u << 10);
}

uint32_t nw_ppc_subfc(int rd, int ra, int rb, int rc)
{
	return (31u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (8u << 1) | (rc ? 1u : 0);
}

uint32_t nw_ppc_cmpli(int crfd, int ra, unsigned uimm)
{
	return (10u << 26) | ((uint32_t)(crfd & 7) << 23) | ((uint32_t)ra << 16) |
	       (uimm & 0xffffu);
}

uint32_t nw_ppc_cmpl(int crfd, int ra, int rb)
{
	return (31u << 26) | ((uint32_t)(crfd & 7) << 23) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (32u << 1);
}

uint32_t nw_ppc_mtcrf(int crm, int rs)
{
	return (31u << 26) | ((uint32_t)rs << 21) | (((uint32_t)crm & 0xffu) << 12) |
	       (144u << 1);
}

uint32_t nw_ppc_mfcr(int rd)
{
	return (31u << 26) | ((uint32_t)rd << 21) | (19u << 1);
}

uint32_t nw_ppc_mcrf(int crfd, int crfs)
{
	return (19u << 26) | (((uint32_t)crfd & 7u) << 23) |
	       (((uint32_t)crfs & 7u) << 18);
}

uint32_t nw_ppc_crnor(int crbd, int crba, int crbb)
{
	return (19u << 26) | ((uint32_t)crbd << 21) | ((uint32_t)crba << 16) |
	       ((uint32_t)crbb << 11) | (33u << 1);
}

uint32_t nw_ppc_crxor(int crbd, int crba, int crbb)
{
	return (19u << 26) | ((uint32_t)crbd << 21) | ((uint32_t)crba << 16) |
	       ((uint32_t)crbb << 11) | (193u << 1);
}

uint32_t nw_ppc_creqv(int crbd, int crba, int crbb)
{
	return (19u << 26) | ((uint32_t)crbd << 21) | ((uint32_t)crba << 16) |
	       ((uint32_t)crbb << 11) | (289u << 1);
}

uint32_t nw_ppc_cror(int crbd, int crba, int crbb)
{
	return (19u << 26) | ((uint32_t)crbd << 21) | ((uint32_t)crba << 16) |
	       ((uint32_t)crbb << 11) | (449u << 1);
}

uint32_t nw_ppc_crorc(int crbd, int crba, int crbb)
{
	return (19u << 26) | ((uint32_t)crbd << 21) | ((uint32_t)crba << 16) |
	       ((uint32_t)crbb << 11) | (417u << 1);
}

uint32_t nw_ppc_crand(int crbd, int crba, int crbb)
{
	return (19u << 26) | ((uint32_t)crbd << 21) | ((uint32_t)crba << 16) |
	       ((uint32_t)crbb << 11) | (257u << 1);
}

uint32_t nw_ppc_crandc(int crbd, int crba, int crbb)
{
	return (19u << 26) | ((uint32_t)crbd << 21) | ((uint32_t)crba << 16) |
	       ((uint32_t)crbb << 11) | (129u << 1);
}

uint32_t nw_ppc_crnand(int crbd, int crba, int crbb)
{
	return (19u << 26) | ((uint32_t)crbd << 21) | ((uint32_t)crba << 16) |
	       ((uint32_t)crbb << 11) | (225u << 1);
}

uint32_t nw_ppc_extsh(int ra, int rs, int rc)
{
	return (31u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       (922u << 1) | (rc ? 1u : 0);
}

uint32_t nw_ppc_extsb(int ra, int rs, int rc)
{
	return (31u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       (954u << 1) | (rc ? 1u : 0);
}

uint32_t nw_ppc_slw(int ra, int rs, int rb, int rc)
{
	return (31u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (24u << 1) | (rc ? 1u : 0);
}

uint32_t nw_ppc_srw(int ra, int rs, int rb, int rc)
{
	return (31u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (536u << 1) | (rc ? 1u : 0);
}

uint32_t nw_ppc_sraw(int ra, int rs, int rb, int rc)
{
	return (31u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (792u << 1) | (rc ? 1u : 0);
}

uint32_t nw_ppc_srawi(int ra, int rs, int sh, int rc)
{
	return (31u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)(sh & 31) << 11) | (824u << 1) | (rc ? 1u : 0);
}

uint32_t nw_ppc_sync(void)
{
	return (31u << 26) | (598u << 1);
}

uint32_t nw_ppc_tlbsync(void)
{
	return (31u << 26) | (566u << 1);
}

uint32_t nw_ppc_dss(void)
{
	return (31u << 26) | (16u << 21) | (822u << 1);
}

uint32_t nw_ppc_dst(int ra, int rb, int strm)
{
	return (31u << 26) | ((uint32_t)(strm & 3) << 21) |
	       ((uint32_t)ra << 16) | ((uint32_t)rb << 11) | (342u << 1);
}

uint32_t nw_ppc_dstst(int ra, int rb, int strm)
{
	return (31u << 26) | ((uint32_t)(strm & 3) << 21) |
	       ((uint32_t)ra << 16) | ((uint32_t)rb << 11) | (374u << 1);
}

uint32_t nw_ppc_dcbt(int ra, int rb)
{
	return (31u << 26) | ((uint32_t)ra << 16) | ((uint32_t)rb << 11) | (278u << 1);
}

uint32_t nw_ppc_dcbtst(int ra, int rb)
{
	return (31u << 26) | ((uint32_t)ra << 16) | ((uint32_t)rb << 11) | (246u << 1);
}

uint32_t nw_ppc_dcbf(int ra, int rb)
{
	return (31u << 26) | ((uint32_t)ra << 16) | ((uint32_t)rb << 11) | (86u << 1);
}

uint32_t nw_ppc_eieio(void)
{
	return (31u << 26) | (854u << 1);
}

uint32_t nw_ppc_dcbz(int ra, int rb)
{
	return (31u << 26) | ((uint32_t)ra << 16) | ((uint32_t)rb << 11) | (1014u << 1);
}

uint32_t nw_ppc_mtsr(int sr, int rs)
{
	return (31u << 26) | ((uint32_t)rs << 21) | ((uint32_t)(sr & 15) << 16) | (210u << 1);
}

uint32_t nw_ppc_mtsrin(int rs, int rb)
{
	return (31u << 26) | ((uint32_t)rs << 21) | ((uint32_t)rb << 11) | (242u << 1);
}

uint32_t nw_ppc_mfsrin(int rd, int rb)
{
	return (31u << 26) | ((uint32_t)rd << 21) | ((uint32_t)rb << 11) | (659u << 1);
}

uint32_t nw_ppc_twi(int to, int ra, int simm)
{
	return (3u << 26) | ((uint32_t)(to & 31) << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)simm & 0xffffu);
}

uint32_t nw_ppc_mtmsr(int rs)
{
	return (31u << 26) | ((uint32_t)rs << 21) | (146u << 1);
}

uint32_t nw_ppc_isync(void)
{
	return (19u << 26) | (150u << 1);
}

uint32_t nw_ppc_tlbie(int rb)
{
	return (31u << 26) | ((uint32_t)rb << 11) | (306u << 1);
}

uint32_t nw_ppc_add(int rd, int ra, int rb, int rc)
{
	return 0x7c000214u | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) | ((uint32_t)rb << 11) | (rc ? 1u : 0);
}

uint32_t nw_ppc_subfze(int rd, int ra, int rc)
{
	return (31u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       (200u << 1) | (rc ? 1u : 0);
}

uint32_t nw_ppc_rlwinm(int ra, int rs, int sh, int mb, int me)
{
	return (21u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)sh << 11) | ((uint32_t)mb << 6) | ((uint32_t)me << 1);
}

uint32_t nw_ppc_rlwnm(int ra, int rs, int rb, int mb, int me)
{
	return (23u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | ((uint32_t)mb << 6) | ((uint32_t)me << 1);
}

uint32_t nw_ppc_lmw(int rd, int ra, int d)
{
	return (46u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) | ((uint32_t)d & 0xffffu);
}

uint32_t nw_ppc_stmw(int rs, int ra, int d)
{
	return (47u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) | ((uint32_t)d & 0xffffu);
}

uint32_t nw_ppc_rlwimi(int ra, int rs, int sh, int mb, int me)
{
	return (20u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)sh << 11) | ((uint32_t)mb << 6) | ((uint32_t)me << 1);
}

uint32_t nw_ppc_lfd(int frd, int ra, int d)
{
	return (50u << 26) | ((uint32_t)frd << 21) | ((uint32_t)ra << 16) | ((uint32_t)d & 0xffffu);
}

uint32_t nw_ppc_stfd(int frs, int ra, int d)
{
	return (54u << 26) | ((uint32_t)frs << 21) | ((uint32_t)ra << 16) | ((uint32_t)d & 0xffffu);
}

uint32_t nw_ppc_lfs(int frd, int ra, int d)
{
	return (48u << 26) | ((uint32_t)frd << 21) | ((uint32_t)ra << 16) | ((uint32_t)d & 0xffffu);
}

uint32_t nw_ppc_stfs(int frs, int ra, int d)
{
	return (52u << 26) | ((uint32_t)frs << 21) | ((uint32_t)ra << 16) | ((uint32_t)d & 0xffffu);
}

uint32_t nw_ppc_lfsu(int frd, int ra, int d)
{
	return (49u << 26) | ((uint32_t)frd << 21) | ((uint32_t)ra << 16) | ((uint32_t)d & 0xffffu);
}

uint32_t nw_ppc_lfdu(int frd, int ra, int d)
{
	return (51u << 26) | ((uint32_t)frd << 21) | ((uint32_t)ra << 16) | ((uint32_t)d & 0xffffu);
}

uint32_t nw_ppc_stfdu(int frs, int ra, int d)
{
	return (55u << 26) | ((uint32_t)frs << 21) | ((uint32_t)ra << 16) | ((uint32_t)d & 0xffffu);
}

uint32_t nw_ppc_lhzu(int rd, int ra, int d)
{
	return (41u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) | ((uint32_t)d & 0xffffu);
}

uint32_t nw_ppc_vaddubm(int vd, int va, int vb)
{
	return (4u << 26) | ((uint32_t)vd << 21) | ((uint32_t)va << 16) |
	       ((uint32_t)vb << 11);
}

uint32_t nw_ppc_lfsx(int frd, int ra, int rb)
{
	return (31u << 26) | ((uint32_t)frd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (535u << 1);
}

uint32_t nw_ppc_stfsx(int frs, int ra, int rb)
{
	return (31u << 26) | ((uint32_t)frs << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (663u << 1);
}

uint32_t nw_ppc_fdivs(int frd, int fra, int frb)
{
	return (59u << 26) | ((uint32_t)frd << 21) | ((uint32_t)fra << 16) |
	       ((uint32_t)frb << 11) | (18u << 1);
}

uint32_t nw_ppc_fsubs(int frd, int fra, int frb)
{
	return (59u << 26) | ((uint32_t)frd << 21) | ((uint32_t)fra << 16) |
	       ((uint32_t)frb << 11) | (20u << 1);
}

uint32_t nw_ppc_fadds(int frd, int fra, int frb)
{
	return (59u << 26) | ((uint32_t)frd << 21) | ((uint32_t)fra << 16) |
	       ((uint32_t)frb << 11) | (21u << 1);
}

uint32_t nw_ppc_fmuls(int frd, int fra, int frc)
{
	return (59u << 26) | ((uint32_t)frd << 21) | ((uint32_t)fra << 16) |
	       ((uint32_t)frc << 6) | (25u << 1);
}

uint32_t nw_ppc_fmadds(int frd, int fra, int frc, int frb)
{
	return (59u << 26) | ((uint32_t)frd << 21) | ((uint32_t)fra << 16) |
	       ((uint32_t)frb << 11) | ((uint32_t)frc << 6) | (29u << 1);
}

uint32_t nw_ppc_fnmsubs(int frd, int fra, int frc, int frb)
{
	return (59u << 26) | ((uint32_t)frd << 21) | ((uint32_t)fra << 16) |
	       ((uint32_t)frb << 11) | ((uint32_t)frc << 6) | (30u << 1);
}

uint32_t nw_ppc_fmsubs(int frd, int fra, int frc, int frb)
{
	return (59u << 26) | ((uint32_t)frd << 21) | ((uint32_t)fra << 16) |
	       ((uint32_t)frb << 11) | ((uint32_t)frc << 6) | (28u << 1);
}

uint32_t nw_ppc_lfdx(int frd, int ra, int rb)
{
	return (31u << 26) | ((uint32_t)frd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (599u << 1);
}

uint32_t nw_ppc_stfdx(int frs, int ra, int rb)
{
	return (31u << 26) | ((uint32_t)frs << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (727u << 1);
}

uint32_t nw_ppc_fmadd(int frd, int fra, int frc, int frb)
{
	return (63u << 26) | ((uint32_t)frd << 21) | ((uint32_t)fra << 16) |
	       ((uint32_t)frb << 11) | ((uint32_t)frc << 6) | (29u << 1);
}

uint32_t nw_ppc_fcmpo(int crfd, int fra, int frb)
{
	return (63u << 26) | (((uint32_t)crfd & 7u) << 23) |
	       ((uint32_t)fra << 16) | ((uint32_t)frb << 11) | (32u << 1);
}

uint32_t nw_ppc_fcmpu(int crfd, int fra, int frb)
{
	return (63u << 26) | (((uint32_t)crfd & 7u) << 23) |
	       ((uint32_t)fra << 16) | ((uint32_t)frb << 11);
}

uint32_t nw_ppc_fabs(int frd, int frb)
{
	return (63u << 26) | ((uint32_t)frd << 21) | ((uint32_t)frb << 11) | (264u << 1);
}

uint32_t nw_ppc_vand(int vd, int va, int vb)
{
	return (4u << 26) | ((uint32_t)vd << 21) | ((uint32_t)va << 16) |
	       ((uint32_t)vb << 11) | 1028u;
}

uint32_t nw_ppc_vandc(int vd, int va, int vb)
{
	return (4u << 26) | ((uint32_t)vd << 21) | ((uint32_t)va << 16) |
	       ((uint32_t)vb << 11) | 1092u;
}

uint32_t nw_ppc_vxor(int vd, int va, int vb)
{
	return (4u << 26) | ((uint32_t)vd << 21) | ((uint32_t)va << 16) |
	       ((uint32_t)vb << 11) | 1220u;
}

uint32_t nw_ppc_vsububm(int vd, int va, int vb)
{
	return (4u << 26) | ((uint32_t)vd << 21) | ((uint32_t)va << 16) |
	       ((uint32_t)vb << 11) | 1024u;
}

uint32_t nw_ppc_vslh(int vd, int va, int vb)
{
	return (4u << 26) | ((uint32_t)vd << 21) | ((uint32_t)va << 16) |
	       ((uint32_t)vb << 11) | 324u;
}

uint32_t nw_ppc_vcmpequw(int vd, int va, int vb, int rc)
{
	return (4u << 26) | ((uint32_t)vd << 21) | ((uint32_t)va << 16) |
	       ((uint32_t)vb << 11) | 134u | (rc ? (1u << 10) : 0);
}

uint32_t nw_ppc_vcmpequb(int vd, int va, int vb, int rc)
{
	return (4u << 26) | ((uint32_t)vd << 21) | ((uint32_t)va << 16) |
	       ((uint32_t)vb << 11) | 6u | (rc ? (1u << 10) : 0);
}

uint32_t nw_ppc_vminsb(int vd, int va, int vb)
{
	return (4u << 26) | ((uint32_t)vd << 21) | ((uint32_t)va << 16) |
	       ((uint32_t)vb << 11) | 770u;
}

uint32_t nw_ppc_vsr(int vd, int va, int vb)
{
	return (4u << 26) | ((uint32_t)vd << 21) | ((uint32_t)va << 16) |
	       ((uint32_t)vb << 11) | 708u;
}

uint32_t nw_ppc_vsrw(int vd, int va, int vb)
{
	return (4u << 26) | ((uint32_t)vd << 21) | ((uint32_t)va << 16) |
	       ((uint32_t)vb << 11) | 644u;
}

uint32_t nw_ppc_vspltisw(int vd, int simm)
{
	return (4u << 26) | ((uint32_t)vd << 21) | ((uint32_t)(simm & 31) << 16) | 908u;
}

uint32_t nw_ppc_vsl(int vd, int va, int vb)
{
	return (4u << 26) | ((uint32_t)vd << 21) | ((uint32_t)va << 16) |
	       ((uint32_t)vb << 11) | 452u;
}

uint32_t nw_ppc_vsro(int vd, int va, int vb)
{
	return (4u << 26) | ((uint32_t)vd << 21) | ((uint32_t)va << 16) |
	       ((uint32_t)vb << 11) | 1100u;
}

uint32_t nw_ppc_vslo(int vd, int va, int vb)
{
	return (4u << 26) | ((uint32_t)vd << 21) | ((uint32_t)va << 16) |
	       ((uint32_t)vb << 11) | 1036u;
}

uint32_t nw_ppc_vspltisb(int vd, int simm)
{
	return (4u << 26) | ((uint32_t)vd << 21) | ((uint32_t)(simm & 31) << 16) | 780u;
}

uint32_t nw_ppc_vspltw(int vd, int uimm, int vb)
{
	return (4u << 26) | ((uint32_t)vd << 21) | ((uint32_t)(uimm & 31) << 16) |
	       ((uint32_t)vb << 11) | 652u;
}

uint32_t nw_ppc_vspltb(int vd, int uimm, int vb)
{
	return (4u << 26) | ((uint32_t)vd << 21) | ((uint32_t)(uimm & 31) << 16) |
	       ((uint32_t)vb << 11) | 524u;
}

uint32_t nw_ppc_mtvscr(int vb)
{
	return (4u << 26) | ((uint32_t)vb << 11) | 1604u;
}

uint32_t nw_ppc_mfvscr(int vd)
{
	return (4u << 26) | ((uint32_t)vd << 21) | 1540u;
}

uint32_t nw_ppc_vsldoi(int vd, int va, int vb, int shb)
{
	return (4u << 26) | ((uint32_t)vd << 21) | ((uint32_t)va << 16) |
	       ((uint32_t)vb << 11) | ((uint32_t)(shb & 15) << 6) | 44u;
}

uint32_t nw_ppc_vmladduhm(int vd, int va, int vb, int vc)
{
	return (4u << 26) | ((uint32_t)vd << 21) | ((uint32_t)va << 16) |
	       ((uint32_t)vb << 11) | ((uint32_t)vc << 6) | 34u;
}

uint32_t nw_ppc_vsubshs(int vd, int va, int vb)
{
	return (4u << 26) | ((uint32_t)vd << 21) | ((uint32_t)va << 16) |
	       ((uint32_t)vb << 11) | 1856u;
}

uint32_t nw_ppc_vmrghb(int vd, int va, int vb)
{
	return (4u << 26) | ((uint32_t)vd << 21) | ((uint32_t)va << 16) |
	       ((uint32_t)vb << 11) | 12u;
}

uint32_t nw_ppc_vmrglb(int vd, int va, int vb)
{
	return (4u << 26) | ((uint32_t)vd << 21) | ((uint32_t)va << 16) |
	       ((uint32_t)vb << 11) | 268u;
}

uint32_t nw_ppc_vsrb(int vd, int va, int vb)
{
	return (4u << 26) | ((uint32_t)vd << 21) | ((uint32_t)va << 16) |
	       ((uint32_t)vb << 11) | 516u;
}

uint32_t nw_ppc_vslb(int vd, int va, int vb)
{
	return (4u << 26) | ((uint32_t)vd << 21) | ((uint32_t)va << 16) |
	       ((uint32_t)vb << 11) | 260u;
}

uint32_t nw_ppc_adde(int rd, int ra, int rb, int rc)
{
	return (31u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (138u << 1) | (rc ? 1u : 0);
}

uint32_t nw_ppc_addeo(int rd, int ra, int rb, int rc)
{
	return (31u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (1u << 10) | (138u << 1) | (rc ? 1u : 0);
}

uint32_t nw_ppc_eqv(int ra, int rs, int rb)
{
	return (31u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (284u << 1);
}

uint32_t nw_ppc_nand(int ra, int rs, int rb)
{
	return (31u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (476u << 1);
}

uint32_t nw_ppc_vadduwm(int vd, int va, int vb)
{
	return (4u << 26) | ((uint32_t)vd << 21) | ((uint32_t)va << 16) |
	       ((uint32_t)vb << 11) | 128u;
}

uint32_t nw_ppc_vsraw(int vd, int va, int vb)
{
	return (4u << 26) | ((uint32_t)vd << 21) | ((uint32_t)va << 16) |
	       ((uint32_t)vb << 11) | 900u;
}

uint32_t nw_ppc_vpkswss(int vd, int va, int vb)
{
	return (4u << 26) | ((uint32_t)vd << 21) | ((uint32_t)va << 16) |
	       ((uint32_t)vb << 11) | 462u;
}

uint32_t nw_ppc_fneg(int frd, int frb)
{
	return (63u << 26) | ((uint32_t)frd << 21) | ((uint32_t)frb << 11) | (40u << 1);
}

uint32_t nw_ppc_fmr(int frd, int frb)
{
	return (63u << 26) | ((uint32_t)frd << 21) | ((uint32_t)frb << 11) | (72u << 1);
}

uint32_t nw_ppc_mtfsf(int fm, int frb)
{
	return (63u << 26) | (((uint32_t)fm & 0xffu) << 17) |
	       ((uint32_t)frb << 11) | (711u << 1);
}

uint32_t nw_ppc_frsp(int frd, int frb)
{
	return (63u << 26) | ((uint32_t)frd << 21) | ((uint32_t)frb << 11) | (12u << 1);
}

uint32_t nw_ppc_mffs(int frd)
{
	return (63u << 26) | ((uint32_t)frd << 21) | (583u << 1);
}

uint32_t nw_ppc_fnmsub(int frd, int fra, int frc, int frb)
{
	return (63u << 26) | ((uint32_t)frd << 21) | ((uint32_t)fra << 16) |
	       ((uint32_t)frb << 11) | ((uint32_t)frc << 6) | (30u << 1);
}

uint32_t nw_ppc_mullw(int rd, int ra, int rb, int rc)
{
	return (31u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (235u << 1) | (rc ? 1u : 0);
}

uint32_t nw_ppc_mulhwu(int rd, int ra, int rb, int rc)
{
	return (31u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (11u << 1) | (rc ? 1u : 0);
}

uint32_t nw_ppc_mulhw(int rd, int ra, int rb, int rc)
{
	return (31u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (75u << 1) | (rc ? 1u : 0);
}

uint32_t nw_ppc_lhzx(int rd, int ra, int rb)
{
	return (31u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (279u << 1);
}

uint32_t nw_ppc_lhzux(int rd, int ra, int rb)
{
	return (31u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (311u << 1);
}

uint32_t nw_ppc_lwbrx(int rd, int ra, int rb)
{
	return (31u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (534u << 1);
}

uint32_t nw_ppc_subfic(int rd, int ra, int simm)
{
	return (8u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)simm & 0xffffu);
}

uint32_t nw_ppc_stbx(int rs, int ra, int rb)
{
	return (31u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (215u << 1);
}

uint32_t nw_ppc_stbux(int rs, int ra, int rb)
{
	return (31u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (247u << 1);
}

uint32_t nw_ppc_stwux(int rs, int ra, int rb)
{
	return (31u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (183u << 1);
}

uint32_t nw_ppc_lwzux(int rd, int ra, int rb)
{
	return (31u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (55u << 1);
}

uint32_t nw_ppc_lbzux(int rd, int ra, int rb)
{
	return (31u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (119u << 1);
}

uint32_t nw_ppc_divwu(int rd, int ra, int rb, int rc)
{
	return (31u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (459u << 1) | (rc ? 1u : 0);
}

uint32_t nw_ppc_divwuo(int rd, int ra, int rb, int rc)
{
	return nw_ppc_divwu(rd, ra, rb, rc) | (1u << 10);
}

uint32_t nw_ppc_divw(int rd, int ra, int rb, int rc)
{
	return (31u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (491u << 1) | (rc ? 1u : 0);
}

uint32_t nw_ppc_divwo(int rd, int ra, int rb, int rc)
{
	return nw_ppc_divw(rd, ra, rb, rc) | (1u << 10);
}

uint32_t nw_ppc_lswi(int rd, int ra, int nb)
{
	return (31u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       (((uint32_t)nb & 31u) << 11) | (597u << 1);
}

uint32_t nw_ppc_stswi(int rs, int ra, int nb)
{
	return (31u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       (((uint32_t)nb & 31u) << 11) | (725u << 1);
}

uint32_t nw_ppc_mfmsr(int rd)
{
	return (31u << 26) | ((uint32_t)rd << 21) | (83u << 1);
}

uint32_t nw_ppc_mftb(int rd, int tbr)
{
	uint32_t fld = ((uint32_t)(tbr & 0x1f) << 5) | ((uint32_t)(tbr >> 5) & 0x1f);
	return 0x7c000000u | ((uint32_t)rd << 21) | (fld << 11) | (371u << 1);
}

uint32_t nw_ppc_sc(void)
{
	return (17u << 26) | 2u;
}

uint32_t nw_ppc_lwz(int rd, int ra, int d)
{
	return (32u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) | ((uint32_t)d & 0xffffu);
}

uint32_t nw_ppc_lwzu(int rd, int ra, int d)
{
	return (33u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) | ((uint32_t)d & 0xffffu);
}

uint32_t nw_ppc_lbz(int rd, int ra, int d)
{
	return (34u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) | ((uint32_t)d & 0xffffu);
}

uint32_t nw_ppc_lbzu(int rd, int ra, int d)
{
	return (35u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) | ((uint32_t)d & 0xffffu);
}

uint32_t nw_ppc_lbzx(int rd, int ra, int rb)
{
	return (31u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (87u << 1);
}

uint32_t nw_ppc_lvx(int vd, int ra, int rb)
{
	return (31u << 26) | ((uint32_t)vd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (103u << 1);
}

uint32_t nw_ppc_lvxl(int vd, int ra, int rb)
{
	return (31u << 26) | ((uint32_t)vd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (359u << 1);
}

uint32_t nw_ppc_stvx(int vs, int ra, int rb)
{
	return (31u << 26) | ((uint32_t)vs << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (231u << 1);
}

uint32_t nw_ppc_stvxl(int vs, int ra, int rb)
{
	return (31u << 26) | ((uint32_t)vs << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (487u << 1);
}

uint32_t nw_ppc_stb(int rs, int ra, int d)
{
	return (38u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) | ((uint32_t)d & 0xffffu);
}

uint32_t nw_ppc_stbu(int rs, int ra, int d)
{
	return (39u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) | ((uint32_t)d & 0xffffu);
}

uint32_t nw_ppc_stw(int rs, int ra, int d)
{
	return (36u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) | ((uint32_t)d & 0xffffu);
}

uint32_t nw_ppc_stwu(int rs, int ra, int d)
{
	return (37u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) | ((uint32_t)d & 0xffffu);
}

uint32_t nw_ppc_stwx(int rs, int ra, int rb)
{
	return (31u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (151u << 1);
}

uint32_t nw_ppc_sthx(int rs, int ra, int rb)
{
	return (31u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (407u << 1);
}

uint32_t nw_ppc_sthux(int rs, int ra, int rb)
{
	return (31u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (439u << 1);
}

uint32_t nw_ppc_lwzx(int rd, int ra, int rb)
{
	return (31u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (23u << 1);
}

uint32_t nw_ppc_lhax(int rd, int ra, int rb)
{
	return (31u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (343u << 1);
}

uint32_t nw_ppc_lhaux(int rd, int ra, int rb)
{
	return (31u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (375u << 1);
}

uint32_t nw_ppc_lha(int rd, int ra, int d)
{
	return (42u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) | ((uint32_t)d & 0xffffu);
}

uint32_t nw_ppc_sth(int rs, int ra, int d)
{
	return (44u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) | ((uint32_t)d & 0xffffu);
}

uint32_t nw_ppc_sthu(int rs, int ra, int d)
{
	return (45u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) | ((uint32_t)d & 0xffffu);
}

uint32_t nw_ppc_cmp(int ra, int rb)
{
	return 0x7c000000u | ((uint32_t)ra << 16) | ((uint32_t)rb << 11);
}

uint32_t nw_ppc_cmp_cr(int crfd, int ra, int rb)
{
	return nw_ppc_cmp(ra, rb) | ((uint32_t)(crfd & 7) << 23);
}

uint32_t nw_ppc_cmpi(int ra, int simm)
{
	return (11u << 26) | ((uint32_t)ra << 16) | ((uint32_t)simm & 0xffffu);
}

uint32_t nw_ppc_cmpi_cr(int crfd, int ra, int simm)
{
	return nw_ppc_cmpi(ra, simm) | ((uint32_t)(crfd & 7) << 23);
}

uint32_t nw_ppc_b(int disp, int lk)
{
	return (18u << 26) | (((uint32_t)disp) & 0x03fffffcu) | (lk ? 1u : 0);
}

uint32_t nw_ppc_bc(int bo, int bi, int disp)
{
	return (16u << 26) | ((uint32_t)bo << 21) | ((uint32_t)bi << 16) | (((uint32_t)disp) & 0xfffcu);
}

uint32_t nw_ppc_blr(void)
{
	return 0x4e800020u;
}

uint32_t nw_ppc_bclr(int bo, int bi)
{
	return (19u << 26) | ((uint32_t)bo << 21) | ((uint32_t)bi << 16) | (16u << 1);
}

uint32_t nw_ppc_bcctr(int bo, int bi)
{
	return (19u << 26) | ((uint32_t)bo << 21) | ((uint32_t)bi << 16) | (528u << 1);
}

uint32_t nw_ppc_or(int ra, int rs, int rb)
{
	return (31u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (444u << 1);
}

uint32_t nw_ppc_xor(int ra, int rs, int rb)
{
	return (31u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (316u << 1);
}

uint32_t nw_ppc_and(int ra, int rs, int rb, int rc)
{
	return (31u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (28u << 1) | (rc ? 1u : 0);
}

uint32_t nw_ppc_andc(int ra, int rs, int rb, int rc)
{
	return (31u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (60u << 1) | (rc ? 1u : 0);
}

uint32_t nw_ppc_cntlzw(int ra, int rs, int rc)
{
	return (31u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       (26u << 1) | (rc ? 1u : 0);
}

uint32_t nw_ppc_neg(int rd, int ra, int rc)
{
	return (31u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       (104u << 1) | (rc ? 1u : 0);
}

uint32_t nw_ppc_ori(int ra, int rs, unsigned uimm)
{
	return (24u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       (uimm & 0xffffu);
}

uint32_t nw_ppc_oris(int ra, int rs, unsigned uimm)
{
	return (25u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       (uimm & 0xffffu);
}

uint32_t nw_ppc_xori(int ra, int rs, unsigned uimm)
{
	return (26u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       (uimm & 0xffffu);
}

uint32_t nw_ppc_xoris(int ra, int rs, unsigned uimm)
{
	return (27u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       (uimm & 0xffffu);
}

uint32_t nw_ppc_mfspr(int rd, int spr)
{
	uint32_t fld = ((uint32_t)(spr & 0x1f) << 5) | ((uint32_t)(spr >> 5) & 0x1f);
	return 0x7c000000u | ((uint32_t)rd << 21) | (fld << 11) | (339u << 1);
}

uint32_t nw_ppc_mtspr(int spr, int rs)
{
	uint32_t fld = ((uint32_t)(spr & 0x1f) << 5) | ((uint32_t)(spr >> 5) & 0x1f);
	return 0x7c000000u | ((uint32_t)rs << 21) | (fld << 11) | (467u << 1);
}

static uint32_t spr_num(uint32_t op)
{
	uint32_t spr = (op >> 11) & 0x3ffu;
	return ((spr & 0x1f) << 5) | ((spr >> 5) & 0x1f);
}

static uint32_t ppc_mask(uint32_t mb, uint32_t me)
{
	return (mb > me) ?
		~(((uint32_t)-1 >> mb) ^ ((me >= 31) ? 0 : (uint32_t)-1 >> (me + 1))) :
		(((uint32_t)-1 >> mb) ^ ((me >= 31) ? 0 : (uint32_t)-1 >> (me + 1)));
}

static uint32_t rotl32(uint32_t x, uint32_t n)
{
	n &= 31;
	return n ? ((x << n) | (x >> (32 - n))) : x;
}

static void record_ca(struct nw_jit_cpu *cpu, uint32_t a, uint32_t b)
{
	if (((uint64_t)a + (uint64_t)b) >> 32)
		cpu->xer |= 0x20000000u;
	else
		cpu->xer &= ~0x20000000u;
}

/* Signed overflow of a+b as 32-bit. OV is replaced; SO is sticky. */
static void record_ov(struct nw_jit_cpu *cpu, uint32_t a, uint32_t b)
{
	const int64_t s = (int64_t)(int32_t)a + (int64_t)(int32_t)b;
	const int ov = (int)((((uint64_t)s) >> 63) ^ (((uint32_t)s) >> 31));
	cpu->xer &= ~0x40000000u;
	if (ov)
		cpu->xer |= 0xc0000000u;
}

static void record_ca_sub(struct nw_jit_cpu *cpu, uint32_t a, uint32_t b)
{
	if (b >= a)
		cpu->xer |= 0x20000000u;
	else
		cpu->xer &= ~0x20000000u;
}

static void record_ov_sub(struct nw_jit_cpu *cpu, uint32_t a, uint32_t b)
{
	const int64_t s = (int64_t)(int32_t)b - (int64_t)(int32_t)a;
	const int ov = (int)((((uint64_t)s) >> 63) ^ (((uint32_t)s) >> 31));
	cpu->xer &= ~0x40000000u;
	if (ov)
		cpu->xer |= 0xc0000000u;
}

static uint32_t mtcrf_mask(uint32_t op)
{
	const uint32_t crm = (op >> 12) & 0xffu;
	uint32_t m = 0;
	if (crm & 0x80u) m |= 0xf0000000u;
	if (crm & 0x40u) m |= 0x0f000000u;
	if (crm & 0x20u) m |= 0x00f00000u;
	if (crm & 0x10u) m |= 0x000f0000u;
	if (crm & 0x08u) m |= 0x0000f000u;
	if (crm & 0x04u) m |= 0x00000f00u;
	if (crm & 0x02u) m |= 0x000000f0u;
	if (crm & 0x01u) m |= 0x0000000fu;
	return m;
}

static void record_cr_u(struct nw_jit_cpu *cpu, int crfd, uint32_t a, uint32_t b)
{
	uint32_t f = 2;
	if (a < b)
		f = 8;
	else if (a > b)
		f = 4;
	if (cpu->xer & 0x80000000u)
		f |= 1;
	const int sh = 28 - 4 * crfd;
	const uint32_t mask = 0xfu << sh;
	cpu->cr = (cpu->cr & ~mask) | (f << sh);
}

static void record_cr0(struct nw_jit_cpu *cpu, int32_t v)
{
	uint32_t cr0 = 0;
	if (v < 0)
		cr0 = 8;
	else if (v > 0)
		cr0 = 4;
	else
		cr0 = 2;
	if (cpu->xer & 0x80000000u)
		cr0 |= 1;
	cpu->cr = (cpu->cr & 0x0fffffffu) | (cr0 << 28);
}

void nw_jit_helper_addze(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t ra, uint32_t rc)
{
	const uint32_t a = cpu->gpr[ra & 31u];
	const uint32_t ca = (cpu->xer >> 29) & 1u;
	record_ca(cpu, a, ca);
	cpu->gpr[rd & 31u] = a + ca;
	if (rc)
		record_cr0(cpu, (int32_t)cpu->gpr[rd & 31u]);
}

void nw_jit_helper_subfze(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t ra, uint32_t rc)
{
	const uint32_t a = ~cpu->gpr[ra & 31u];
	const uint32_t ca = (cpu->xer >> 29) & 1u;
	record_ca(cpu, a, ca);
	cpu->gpr[rd & 31u] = a + ca;
	if (rc)
		record_cr0(cpu, (int32_t)cpu->gpr[rd & 31u]);
}

void nw_jit_helper_addme(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t ra, uint32_t rc)
{
	const uint32_t a = cpu->gpr[ra & 31u];
	const uint32_t ca = (cpu->xer >> 29) & 1u;
	const uint64_t s = (uint64_t)a + 0xffffffffull + ca;
	if (s >> 32)
		cpu->xer |= 0x20000000u;
	else
		cpu->xer &= ~0x20000000u;
	cpu->gpr[rd & 31u] = (uint32_t)s;
	if (rc)
		record_cr0(cpu, (int32_t)cpu->gpr[rd & 31u]);
}

void nw_jit_helper_subfme(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t ra, uint32_t rc)
{
	const uint32_t a = ~cpu->gpr[ra & 31u];
	const uint32_t ca = (cpu->xer >> 29) & 1u;
	const uint64_t s = (uint64_t)a + 0xffffffffull + ca;
	if (s >> 32)
		cpu->xer |= 0x20000000u;
	else
		cpu->xer &= ~0x20000000u;
	cpu->gpr[rd & 31u] = (uint32_t)s;
	if (rc)
		record_cr0(cpu, (int32_t)cpu->gpr[rd & 31u]);
}

void nw_jit_helper_orc(struct nw_jit_cpu *cpu, uint32_t ra, uint32_t rs, uint32_t rb, uint32_t rc)
{
	cpu->gpr[ra & 31u] = cpu->gpr[rs & 31u] | ~cpu->gpr[rb & 31u];
	if (rc)
		record_cr0(cpu, (int32_t)cpu->gpr[ra & 31u]);
}

void nw_jit_helper_mcrxr(struct nw_jit_cpu *cpu, uint32_t crfd)
{
	const uint32_t f = (cpu->xer >> 28) & 0xfu;
	const int sh = 28 - 4 * (int)(crfd & 7u);
	cpu->cr = (cpu->cr & ~(0xfu << sh)) | (f << sh);
	cpu->xer &= ~0xf0000000u;
}

void nw_jit_helper_mfsr(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t sr)
{
	const unsigned i = sr & 0xfu;
	uint32_t v = cpu->sr[i];
	if (g_host_mfsr && cpu->host)
		v = g_host_mfsr(cpu->host, i);
	cpu->gpr[rd & 31u] = v;
}

int nw_jit_helper_tw(struct nw_jit_cpu *cpu, uint32_t to, uint32_t a, uint32_t b)
{
	const int32_t sa = (int32_t)a;
	const int32_t sb = (int32_t)b;
	const int trap =
		((to & 0x10u) && sa < sb) ||
		((to & 0x08u) && sa > sb) ||
		((to & 0x04u) && sa == sb) ||
		((to & 0x02u) && a < b) ||
		((to & 0x01u) && a > b);
	if (!trap)
		return 0;
	if (g_host_trap && cpu->host)
		g_host_trap(cpu->host, cpu);
	cpu->fault = NW_JIT_FAULT_EXC;
	return 1;
}

void nw_jit_helper_tlbia(struct nw_jit_cpu *cpu)
{
	nw_jit_dtlb_flush_src(NW_JIT_DTLB_FL_TLB);
	if (g_host_tlbia && cpu->host)
		g_host_tlbia(cpu->host);
}

void nw_jit_helper_lve(struct nw_jit_cpu *cpu, uint32_t vd, uint32_t ra, uint32_t rb, uint32_t sz)
{
	uint32_t ea = (ra ? cpu->gpr[ra & 31u] : 0u) + cpu->gpr[rb & 31u];
	vd &= 31u;
	if (sz == 2)
		ea &= ~1u;
	else if (sz == 4)
		ea &= ~3u;
	uint32_t v;
	if (sz == 1)
		v = nw_jit_helper_lb(cpu, ea);
	else if (sz == 2)
		v = nw_jit_helper_lh(cpu, ea);
	else
		v = nw_jit_helper_lwz(cpu, ea);
	if (cpu->fault)
		return;
	const unsigned i = ea & 15u;
	if (sz == 1) {
		const unsigned w = i / 4u;
		const unsigned s = 24u - 8u * (i % 4u);
		cpu->vr[vd][w] = (cpu->vr[vd][w] & ~(0xffu << s)) | ((v & 0xffu) << s);
	} else if (sz == 2) {
		const unsigned w = i / 4u;
		const unsigned s = (i & 2u) ? 0u : 16u;
		cpu->vr[vd][w] = (cpu->vr[vd][w] & ~(0xffffu << s)) | ((v & 0xffffu) << s);
	} else {
		cpu->vr[vd][i / 4u] = v;
	}
}

void nw_jit_helper_stve(struct nw_jit_cpu *cpu, uint32_t vs, uint32_t ra, uint32_t rb, uint32_t sz)
{
	uint32_t ea = (ra ? cpu->gpr[ra & 31u] : 0u) + cpu->gpr[rb & 31u];
	vs &= 31u;
	if (sz == 2)
		ea &= ~1u;
	else if (sz == 4)
		ea &= ~3u;
	const unsigned i = ea & 15u;
	uint32_t v;
	if (sz == 1) {
		const unsigned w = i / 4u;
		const unsigned s = 24u - 8u * (i % 4u);
		v = (cpu->vr[vs][w] >> s) & 0xffu;
		nw_jit_helper_stb(cpu, ea, v);
	} else if (sz == 2) {
		const unsigned w = i / 4u;
		const unsigned s = (i & 2u) ? 0u : 16u;
		v = (cpu->vr[vs][w] >> s) & 0xffffu;
		nw_jit_helper_sth(cpu, ea, v);
	} else {
		nw_jit_helper_stw(cpu, ea, cpu->vr[vs][i / 4u]);
	}
}

void nw_jit_helper_lhbrx(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t ea)
{
	const uint32_t v = nw_jit_helper_lh(cpu, ea);
	if (cpu->fault)
		return;
	cpu->gpr[rd & 31u] = ((v & 0xffu) << 8) | (v >> 8);
}

void nw_jit_helper_sthbrx(struct nw_jit_cpu *cpu, uint32_t rs, uint32_t ea)
{
	const uint32_t v = cpu->gpr[rs & 31u];
	nw_jit_helper_sth(cpu, ea, ((v & 0xffu) << 8) | ((v >> 8) & 0xffu));
}

void nw_jit_helper_stwbrx(struct nw_jit_cpu *cpu, uint32_t rs, uint32_t ea)
{
	const uint32_t v = cpu->gpr[rs & 31u];
	nw_jit_helper_stw(cpu, ea, (v << 24) | ((v << 8) & 0xff0000u) |
			  ((v >> 8) & 0xff00u) | (v >> 24));
}

void nw_jit_helper_fnabs(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fb)
{
	cpu->fpr[fd & 31u] = cpu->fpr[fb & 31u] | 0x8000000000000000ull;
}

void nw_jit_helper_fsel(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fc, uint32_t fb)
{
	const double a = f64_from_fpr(cpu->fpr[fa & 31u]);
	cpu->fpr[fd & 31u] = (a >= 0.0) ? cpu->fpr[fc & 31u] : cpu->fpr[fb & 31u];
}

void nw_jit_helper_fctiw(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fb)
{
	const double d = f64_from_fpr(cpu->fpr[fb & 31u]);
	int32_t i;
	if (d >= 2147483647.0)
		i = 0x7fffffff;
	else if (d <= -2147483648.0)
		i = (int32_t)0x80000000;
	else
		i = (int32_t)nearbyint(d);
	cpu->fpr[fd & 31u] = 0xfff8000000000000ull | (uint32_t)i;
}

void nw_jit_helper_cr1(struct nw_jit_cpu *cpu)
{
	const uint32_t cr1 = (cpu->fpscr >> 28) & 0xfu;
	cpu->cr = (cpu->cr & 0xf0ffffffu) | (cr1 << 24);
}

void nw_jit_helper_bclr(struct nw_jit_cpu *cpu, uint32_t op, uint32_t pc)
{
	const int bo = (int)((op >> 21) & 0x1f);
	const int bi = (int)((op >> 16) & 0x1f);
	const int xo = (int)((op >> 1) & 0x3ff);
	int cond_ok = 1, ctr_ok = 1;
	if ((bo & 0x10) == 0) {
		const int crbit = (int)((cpu->cr >> (31 - bi)) & 1);
		cond_ok = (bo & 0x08) ? crbit : !crbit;
	}
	if ((bo & 0x04) == 0) {
		cpu->ctr -= 1u;
		ctr_ok = (cpu->ctr == 0);
		if ((bo & 0x02) == 0)
			ctr_ok = !ctr_ok;
	}
	const uint32_t t = (xo == 16) ? cpu->lr : cpu->ctr;
	if (op & 1)
		cpu->lr = pc + 4;
	if (cond_ok && ctr_ok)
		cpu->pc = t & ~3u;
	else
		cpu->pc = pc + 4;
}

void nw_jit_helper_fmsub(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fc, uint32_t fb)
{
	const double r = f64_from_fpr(cpu->fpr[fa & 31u]) * f64_from_fpr(cpu->fpr[fc & 31u]) -
			 f64_from_fpr(cpu->fpr[fb & 31u]);
	cpu->fpr[fd & 31u] = bits_from_f64(r);
	nw_jit_fpscr_fprf_d(cpu, r);
}

void nw_jit_helper_fnmadd(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fc, uint32_t fb)
{
	const double r = -(f64_from_fpr(cpu->fpr[fa & 31u]) * f64_from_fpr(cpu->fpr[fc & 31u]) +
			   f64_from_fpr(cpu->fpr[fb & 31u]));
	cpu->fpr[fd & 31u] = bits_from_f64(r);
	nw_jit_fpscr_fprf_d(cpu, r);
}

void nw_jit_helper_fnmadds(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fa, uint32_t fc, uint32_t fb)
{
	double a, c, b;
	memcpy(&a, &cpu->fpr[fa & 31u], 8);
	memcpy(&c, &cpu->fpr[fc & 31u], 8);
	memcpy(&b, &cpu->fpr[fb & 31u], 8);
	const float r = (float)(-(a * c + b));
	cpu->fpr[fd & 31u] = fpr_from_f32(r);
	nw_jit_fpscr_fprf(cpu, r);
}

void nw_jit_helper_fres(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fb)
{
	const float b = (float)f64_from_fpr(cpu->fpr[fb & 31u]);
	const float r = 1.0f / b;
	cpu->fpr[fd & 31u] = fpr_from_f32(r);
	nw_jit_fpscr_fprf(cpu, r);
}

void nw_jit_helper_frsqrte(struct nw_jit_cpu *cpu, uint32_t fd, uint32_t fb)
{
	const double b = f64_from_fpr(cpu->fpr[fb & 31u]);
	const double r = 1.0 / sqrt(b);
	cpu->fpr[fd & 31u] = bits_from_f64(r);
	nw_jit_fpscr_fprf_d(cpu, r);
}

void nw_jit_helper_mcrfs(struct nw_jit_cpu *cpu, uint32_t crfd, uint32_t crfs)
{
	const int ssh = 28 - 4 * (int)(crfs & 7u);
	const int dsh = 28 - 4 * (int)(crfd & 7u);
	const uint32_t m = 0xfu << ssh;
	const uint32_t f = (cpu->fpscr >> ssh) & 0xfu;
	cpu->cr = (cpu->cr & ~(0xfu << dsh)) | (f << dsh);
	cpu->fpscr &= ~(m & 0x9ff80700u);
}

void nw_jit_helper_mtfsb(struct nw_jit_cpu *cpu, uint32_t crbd, uint32_t setbit)
{
	const uint32_t bit = 1u << (31u - (crbd & 31u));
	if (setbit)
		cpu->fpscr |= bit;
	else
		cpu->fpscr &= ~bit;
}

void nw_jit_helper_mtfsfi(struct nw_jit_cpu *cpu, uint32_t crfd, uint32_t imm)
{
	const int sh = 28 - 4 * (int)(crfd & 7u);
	cpu->fpscr = (cpu->fpscr & ~(0xfu << sh)) | ((imm & 0xfu) << sh);
}

void nw_jit_helper_adde(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t ra, uint32_t rb, uint32_t rc)
{
	const uint32_t a = cpu->gpr[ra & 31u];
	const uint32_t b = cpu->gpr[rb & 31u];
	const uint32_t ca = (cpu->xer >> 29) & 1u;
	const uint64_t s = (uint64_t)a + (uint64_t)b + ca;
	if (s >> 32)
		cpu->xer |= 0x20000000u;
	else
		cpu->xer &= ~0x20000000u;
	cpu->gpr[rd & 31u] = (uint32_t)s;
	if (rc)
		record_cr0(cpu, (int32_t)cpu->gpr[rd & 31u]);
}

void nw_jit_helper_addeo(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t ra, uint32_t rb, uint32_t rc)
{
	const uint32_t a = cpu->gpr[ra & 31u];
	const uint32_t b = cpu->gpr[rb & 31u];
	const uint32_t ca = (cpu->xer >> 29) & 1u;
	const uint64_t s = (uint64_t)a + (uint64_t)b + ca;
	if (s >> 32)
		cpu->xer |= 0x20000000u;
	else
		cpu->xer &= ~0x20000000u;
	const int64_t ss = (int64_t)(int32_t)a + (int64_t)(int32_t)b + (int64_t)ca;
	cpu->xer &= ~0x40000000u;
	if (ss != (int64_t)(int32_t)ss)
		cpu->xer |= 0xc0000000u;
	cpu->gpr[rd & 31u] = (uint32_t)s;
	if (rc)
		record_cr0(cpu, (int32_t)cpu->gpr[rd & 31u]);
}

void nw_jit_helper_mullwo(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t ra, uint32_t rb, uint32_t rc)
{
	const int64_t p = (int64_t)(int32_t)cpu->gpr[ra & 31u] *
			  (int64_t)(int32_t)cpu->gpr[rb & 31u];
	const uint32_t d = (uint32_t)p;
	cpu->xer &= ~0x40000000u;
	if (p != (int64_t)(int32_t)d)
		cpu->xer |= 0xc0000000u;
	cpu->gpr[rd & 31u] = d;
	if (rc)
		record_cr0(cpu, (int32_t)d);
}

void nw_jit_helper_divw(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t ra, uint32_t rb, uint32_t rc)
{
	const int32_t a = (int32_t)cpu->gpr[ra & 31u];
	const int32_t b = (int32_t)cpu->gpr[rb & 31u];
	uint32_t d;
	if (b == 0 || (a == (int32_t)0x80000000 && b == -1))
		d = (uint32_t)(a >> 31);	/* kpx: MSB of dividend */
	else
		d = (uint32_t)(a / b);
	cpu->gpr[rd & 31u] = d;
	if (rc)
		record_cr0(cpu, (int32_t)d);
}

void nw_jit_helper_divwuo(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t ra, uint32_t rb, uint32_t rc)
{
	const uint32_t a = cpu->gpr[ra & 31u];
	const uint32_t b = cpu->gpr[rb & 31u];
	uint32_t d;
	if (b == 0) {
		d = 0;	/* kpx unsigned /0 */
		cpu->xer |= 0xc0000000u;	/* OV=1, SO sticky */
	} else {
		d = a / b;
		cpu->xer &= ~0x40000000u;	/* OV=0, SO unchanged */
	}
	cpu->gpr[rd & 31u] = d;
	if (rc)
		record_cr0(cpu, (int32_t)d);
}

void nw_jit_helper_divwo(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t ra, uint32_t rb, uint32_t rc)
{
	const int32_t a = (int32_t)cpu->gpr[ra & 31u];
	const int32_t b = (int32_t)cpu->gpr[rb & 31u];
	uint32_t d;
	if (b == 0 || (a == (int32_t)0x80000000 && b == -1)) {
		d = (uint32_t)(a >> 31);	/* kpx: MSB of dividend */
		cpu->xer |= 0xc0000000u;
	} else {
		d = (uint32_t)(a / b);
		cpu->xer &= ~0x40000000u;
	}
	cpu->gpr[rd & 31u] = d;
	if (rc)
		record_cr0(cpu, (int32_t)d);
}

/* Signed compare, not wrapped subtract. vs-kpx 7c13a000: INT_MIN vs 1
 * is LT; a-b wraps to positive and would record GT. */
static void record_cr_s(struct nw_jit_cpu *cpu, int crfd, int32_t a, int32_t b)
{
	uint32_t f;
	if (a < b)
		f = 8;
	else if (a > b)
		f = 4;
	else
		f = 2;
	if (cpu->xer & 0x80000000u)
		f |= 1;
	const int sh = 28 - 4 * crfd;
	const uint32_t mask = 0xfu << sh;
	cpu->cr = (cpu->cr & ~mask) | (f << sh);
}

static uint32_t ra_or_0(const struct nw_jit_cpu *cpu, int ra)
{
	return ra ? cpu->gpr[ra] : 0;
}

static int mem_ok_n(const struct nw_jit_cpu *cpu, uint32_t ea, uint32_t n)
{
	return cpu->mem && ea >= cpu->mem_base && (ea - cpu->mem_base) + n <= cpu->mem_size;
}

static int mem_ok(const struct nw_jit_cpu *cpu, uint32_t ea)
{
	return mem_ok_n(cpu, ea, 4);
}

static uint32_t mem_ld_be(const struct nw_jit_cpu *cpu, uint32_t ea)
{
	const uint8_t *p = cpu->mem + (ea - cpu->mem_base);
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void mem_st_be(struct nw_jit_cpu *cpu, uint32_t ea, uint32_t v)
{
	uint8_t *p = cpu->mem + (ea - cpu->mem_base);
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

uint32_t nw_jit_helper_lwz(struct nw_jit_cpu *cpu, uint32_t ea)
{
	if (cpu->mem) {
		if (!mem_ok(cpu, ea)) {
			cpu->fault = 1;
			cpu->fault_ea = ea;
			return 0;
		}
		dtlb_note_miss(ea, 0, (int)((cpu->msr >> 14) & 1u));
		nw_jit_dtlb_fill(ea, ea, 1,
			(uint64_t)(uintptr_t)(cpu->mem + ((ea - cpu->mem_base) & ~0xfffu)),
			(int)((cpu->msr >> 14) & 1u));
		return mem_ld_be(cpu, ea);
	}
	dtlb_sync_msr(cpu);
	{
		uint64_t hostp = 0;
		if (dtlb_host_line(ea, 0, (int)((cpu->msr >> 14) & 1u), &hostp)) {
			const uint8_t *p = (const uint8_t *)(uintptr_t)hostp + (ea & 0xfffu);
			g_dtlb_hit++;
			return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
			       ((uint32_t)p[2] << 8) | p[3];
		}
	}
	if (g_host_lwz && cpu->host) {
		const int why = dtlb_why(ea, 0, (int)((cpu->msr >> 14) & 1u));
		int f = 0;
		uint32_t v = g_host_lwz(cpu->host, ea, cpu->pc, &f);
		if (f) {
			cpu->fault = f;
			cpu->fault_ea = ea;
		} else {
			g_dtlb_miss++;
			g_dtlb_why[why]++;
		}
		return v;
	}
	cpu->fault = 1;
	cpu->fault_ea = ea;
	return 0;
}

uint32_t nw_jit_helper_lh(struct nw_jit_cpu *cpu, uint32_t ea)
{
	if (cpu->mem) {
		if (!mem_ok_n(cpu, ea, 2)) {
			cpu->fault = 1;
			cpu->fault_ea = ea;
			return 0;
		}
		const uint8_t *p = cpu->mem + (ea - cpu->mem_base);
		return ((uint32_t)p[0] << 8) | p[1];
	}
	if (g_host_lh && cpu->host) {
		int f = 0;
		uint32_t v = g_host_lh(cpu->host, ea, cpu->pc, &f);
		if (f) {
			cpu->fault = f;
			cpu->fault_ea = ea;
		}
		return v;
	}
	cpu->fault = 1;
	cpu->fault_ea = ea;
	return 0;
}

uint32_t nw_jit_helper_lb(struct nw_jit_cpu *cpu, uint32_t ea)
{
	if (cpu->mem) {
		if (!mem_ok_n(cpu, ea, 1)) {
			cpu->fault = 1;
			cpu->fault_ea = ea;
			return 0;
		}
		return cpu->mem[ea - cpu->mem_base];
	}
	if (g_host_lb && cpu->host) {
		int f = 0;
		uint32_t v = g_host_lb(cpu->host, ea, cpu->pc, &f);
		if (f) {
			cpu->fault = f;
			cpu->fault_ea = ea;
		}
		return v;
	}
	cpu->fault = 1;
	cpu->fault_ea = ea;
	return 0;
}

uint32_t nw_jit_helper_sraw(struct nw_jit_cpu *cpu, uint32_t rs, uint32_t rb)
{
	const uint32_t n = rb & 31u;
	const uint32_t hi = (rb >> 5) & 1u;
	const int32_t s = (int32_t)rs;
	uint32_t res, ca;
	if (hi) {
		res = (uint32_t)(s >> 31);
		ca = rs >> 31;
	} else {
		res = (uint32_t)(s >> (int)n);
		ca = (n && s < 0 && (rs & ((1u << n) - 1u))) ? 1u : 0;
	}
	if (ca)
		cpu->xer |= 0x20000000u;
	else
		cpu->xer &= ~0x20000000u;
	return res;
}

void nw_jit_helper_lmw(struct nw_jit_cpu *cpu, uint32_t ea, uint32_t rd)
{
	for (uint32_t r = rd; r < 32; r++, ea += 4) {
		cpu->gpr[r] = nw_jit_helper_lwz(cpu, ea);
		if (cpu->fault)
			return;
	}
}

void nw_jit_helper_stmw(struct nw_jit_cpu *cpu, uint32_t ea, uint32_t rs)
{
	for (uint32_t r = rs; r < 32; r++, ea += 4) {
		nw_jit_helper_stw(cpu, ea, cpu->gpr[r]);
		if (cpu->fault)
			return;
	}
}

void nw_jit_helper_stb(struct nw_jit_cpu *cpu, uint32_t ea, uint32_t val)
{
	if (cpu->nstore < NW_JIT_MAX_BLOCK) {
		cpu->store_ea[cpu->nstore] = ea;
		cpu->store_val[cpu->nstore] = val & 0xffu;
		cpu->nstore++;
	}
	if (cpu->mem) {
		if (!mem_ok_n(cpu, ea, 1)) {
			cpu->fault = 1;
			cpu->fault_ea = ea;
			cpu->fault_st = 1;
			return;
		}
		cpu->mem[ea - cpu->mem_base] = (uint8_t)val;
		if ((ea & ~0xfffu) == (cpu->pc & ~0xfffu))
			cpu->fault = NW_JIT_FAULT_SMC;
		return;
	}
	if (nw_jit_mode() == NW_JIT_VERIFY)
		return;
	if (g_host_stb && cpu->host) {
		int f = 0;
		g_host_stb(cpu->host, ea, val & 0xffu, cpu->pc, &f);
		if (f) {
			cpu->fault = f;
			cpu->fault_ea = ea;
			cpu->fault_st = 1;
		}
		return;
	}
	cpu->fault = 1;
	cpu->fault_ea = ea;
	cpu->fault_st = 1;
}

void nw_jit_helper_sth(struct nw_jit_cpu *cpu, uint32_t ea, uint32_t val)
{
	if (cpu->nstore < NW_JIT_MAX_BLOCK) {
		cpu->store_ea[cpu->nstore] = ea;
		cpu->store_val[cpu->nstore] = val & 0xffffu;
		cpu->nstore++;
	}
	if (cpu->mem) {
		if (!mem_ok_n(cpu, ea, 2)) {
			cpu->fault = 1;
			cpu->fault_ea = ea;
			cpu->fault_st = 1;
			return;
		}
		uint8_t *p = cpu->mem + (ea - cpu->mem_base);
		p[0] = (uint8_t)(val >> 8);
		p[1] = (uint8_t)val;
		if ((ea & ~0xfffu) == (cpu->pc & ~0xfffu))
			cpu->fault = NW_JIT_FAULT_SMC;
		return;
	}
	if (nw_jit_mode() == NW_JIT_VERIFY)
		return;
	if (g_host_sth16 && cpu->host) {
		int f = 0;
		g_host_sth16(cpu->host, ea, val & 0xffffu, cpu->pc, &f);
		if (f) {
			cpu->fault = f;
			cpu->fault_ea = ea;
			cpu->fault_st = 1;
		}
		return;
	}
	cpu->fault = 1;
	cpu->fault_ea = ea;
	cpu->fault_st = 1;
}

void nw_jit_helper_stw(struct nw_jit_cpu *cpu, uint32_t ea, uint32_t val)
{
	if (cpu->nstore < NW_JIT_MAX_BLOCK) {
		cpu->store_ea[cpu->nstore] = ea;
		cpu->store_val[cpu->nstore] = val;
		cpu->nstore++;
	}
	if (cpu->mem) {
		if (!mem_ok(cpu, ea)) {
			cpu->fault = 1;
			cpu->fault_ea = ea;
			cpu->fault_st = 1;
			return;
		}
		mem_st_be(cpu, ea, val);
		dtlb_note_miss(ea, 1, (int)((cpu->msr >> 14) & 1u));
		nw_jit_dtlb_fill(ea, ea, 1,
			(uint64_t)(uintptr_t)(cpu->mem + ((ea - cpu->mem_base) & ~0xfffu)),
			(int)((cpu->msr >> 14) & 1u));
		if ((ea & ~0xfffu) == (cpu->pc & ~0xfffu))
			cpu->fault = NW_JIT_FAULT_SMC;
		return;
	}
	/* Shadow: record only. A live write before kpx replay makes
	 * lwz/add/stw in one block double-apply (4b2-stw gpr11 10000000
	 * vs 20000000 at 50310574). Copy-out will call the host store. */
	if (nw_jit_mode() == NW_JIT_VERIFY)
		return;
	if (g_host_stw && cpu->host) {
		dtlb_sync_msr(cpu);
		{
			uint64_t hostp = 0;
			if (dtlb_host_line(ea, 1, (int)((cpu->msr >> 14) & 1u), &hostp)) {
				uint8_t *p = (uint8_t *)(uintptr_t)hostp + (ea & 0xfffu);
				p[0] = (uint8_t)(val >> 24);
				p[1] = (uint8_t)(val >> 16);
				p[2] = (uint8_t)(val >> 8);
				p[3] = (uint8_t)val;
				g_dtlb_hit++;
				nw_fb_note_host(p);
				if ((ea & ~0xfffu) == (cpu->pc & ~0xfffu))
					cpu->fault = NW_JIT_FAULT_SMC;
				return;
			}
		}
		const int why = dtlb_why(ea, 1, (int)((cpu->msr >> 14) & 1u));
		int f = 0;
		g_host_stw(cpu->host, ea, val, cpu->pc, &f);
		if (f) {
			cpu->fault = f;
			cpu->fault_ea = ea;
			cpu->fault_st = 1;
		} else {
			g_dtlb_miss++;
			g_dtlb_why[why]++;
		}
		return;
	}
	cpu->fault = 1;
	cpu->fault_ea = ea;
	cpu->fault_st = 1;
}

uint32_t nw_jit_helper_lwz_pa(struct nw_jit_cpu *cpu, uint32_t pa)
{
	g_dtlb_hit++;
	if (cpu->mem) {
		if (!mem_ok(cpu, pa)) {
			cpu->fault = 1;
			cpu->fault_ea = pa;
			return 0;
		}
		return mem_ld_be(cpu, pa);
	}
	if (g_host_lwz_pa && cpu->host) {
		int f = 0;
		uint32_t v = g_host_lwz_pa(cpu->host, pa, cpu->pc, &f);
		if (f) {
			cpu->fault = f;
			cpu->fault_ea = pa;
		}
		return v;
	}
	cpu->fault = 1;
	cpu->fault_ea = pa;
	return 0;
}

void nw_jit_helper_stw_pa(struct nw_jit_cpu *cpu, uint32_t pa, uint32_t val)
{
	g_dtlb_hit++;
	if (cpu->nstore < NW_JIT_MAX_BLOCK) {
		cpu->store_ea[cpu->nstore] = pa;
		cpu->store_val[cpu->nstore] = val;
		cpu->nstore++;
	}
	if (cpu->mem) {
		if (!mem_ok(cpu, pa)) {
			cpu->fault = 1;
			cpu->fault_ea = pa;
			cpu->fault_st = 1;
			return;
		}
		mem_st_be(cpu, pa, val);
		if ((pa & ~0xfffu) == (cpu->pc & ~0xfffu))
			cpu->fault = NW_JIT_FAULT_SMC;
		return;
	}
	if (nw_jit_mode() == NW_JIT_VERIFY)
		return;
	if (g_host_stw_pa && cpu->host) {
		int f = 0;
		g_host_stw_pa(cpu->host, pa, val, cpu->pc, &f);
		if (f) {
			cpu->fault = f;
			cpu->fault_ea = pa;
			cpu->fault_st = 1;
		}
		return;
	}
	cpu->fault = 1;
	cpu->fault_ea = pa;
	cpu->fault_st = 1;
}

void nw_jit_helper_mfspr(struct nw_jit_cpu *cpu, uint32_t rd, uint32_t spr)
{
	int st = 0;
	uint32_t v = 0;
	if (g_host_mfspr && cpu->host)
		v = g_host_mfspr(cpu->host, spr, cpu->pc, &st);
	if (st == 2) {
		cpu->pc = v;
		cpu->fault = NW_JIT_FAULT_EXC;
		return;
	}
	if (st == 0)
		cpu->gpr[rd & 31u] = v;
}

int nw_jit_interp_one(struct nw_jit_cpu *cpu, uint32_t op)
{
	const uint32_t pc = cpu->pc;
	const int prim = (int)(op >> 26);
	const int rd = (int)((op >> 21) & 0x1f);
	const int ra = (int)((op >> 16) & 0x1f);
	const int rb = (int)((op >> 11) & 0x1f);
	const int xo = (int)((op >> 1) & 0x3ff);
	const int simm = (int16_t)(op & 0xffffu);

	if (prim == 14) {
		cpu->gpr[rd] = ra_or_0(cpu, ra) + (uint32_t)simm;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 15) {
		cpu->gpr[rd] = ra_or_0(cpu, ra) + ((uint32_t)simm << 16);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 7) {
		cpu->gpr[rd] = (uint32_t)((int64_t)(int32_t)cpu->gpr[ra] * (int64_t)simm);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 235) {
		cpu->gpr[rd] = cpu->gpr[ra] * cpu->gpr[rb];
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[rd]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 747) {
		nw_jit_helper_mullwo(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb, op & 1u);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 491) {
		nw_jit_helper_divw(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb, op & 1u);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 1003) {
		nw_jit_helper_divwo(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb, op & 1u);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 459) {
		const uint32_t b = cpu->gpr[rb];
		cpu->gpr[rd] = b ? (cpu->gpr[ra] / b) : 0;
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[rd]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 971) {
		nw_jit_helper_divwuo(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb, op & 1u);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 11) {
		cpu->gpr[rd] = (uint32_t)(((uint64_t)cpu->gpr[ra] * (uint64_t)cpu->gpr[rb]) >> 32);
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[rd]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 75) {
		cpu->gpr[rd] = (uint32_t)((uint64_t)((int64_t)(int32_t)cpu->gpr[ra] *
						     (int64_t)(int32_t)cpu->gpr[rb]) >> 32);
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[rd]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 12 || prim == 13) {
		const uint32_t a = cpu->gpr[ra], b = (uint32_t)simm;
		record_ca(cpu, a, b);
		cpu->gpr[rd] = a + b;
		if (prim == 13)
			record_cr0(cpu, (int32_t)cpu->gpr[rd]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 8) {
		const uint32_t a = cpu->gpr[ra], b = (uint32_t)simm;
		record_ca_sub(cpu, a, b);
		cpu->gpr[rd] = b - a;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 11) {
		if (rd & 3)
			return -1;
		record_cr_s(cpu, rd >> 2, (int32_t)cpu->gpr[ra], simm);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 10) {
		if (rd & 3)
			return -1;
		record_cr_u(cpu, rd >> 2, cpu->gpr[ra], op & 0xffffu);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 28) {
		cpu->gpr[ra] = cpu->gpr[rd] & (op & 0xffffu);
		record_cr0(cpu, (int32_t)cpu->gpr[ra]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 29) {
		cpu->gpr[ra] = cpu->gpr[rd] & ((op & 0xffffu) << 16);
		record_cr0(cpu, (int32_t)cpu->gpr[ra]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 520) {
		const uint32_t a = cpu->gpr[ra], b = cpu->gpr[rb];
		record_ca_sub(cpu, a, b);
		record_ov_sub(cpu, a, b);
		cpu->gpr[rd] = b - a;
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[rd]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 8) {
		const uint32_t a = cpu->gpr[ra], b = cpu->gpr[rb];
		record_ca_sub(cpu, a, b);
		cpu->gpr[rd] = b - a;
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[rd]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && (xo == 136 || xo == 648)) {
		const uint32_t a = cpu->gpr[ra], b = cpu->gpr[rb];
		const uint32_t ca = (cpu->xer >> 29) & 1u;
		const uint64_t s = (uint64_t)(~a) + (uint64_t)b + ca;
		if (s >> 32)
			cpu->xer |= 0x20000000u;
		else
			cpu->xer &= ~0x20000000u;
		if (xo == 648) {
			const int64_t ss = (int64_t)(int32_t)(~a) + (int64_t)(int32_t)b + (int64_t)ca;
			cpu->xer &= ~0x40000000u;
			if (ss != (int64_t)(int32_t)ss)
				cpu->xer |= 0xc0000000u;
		}
		cpu->gpr[rd] = (uint32_t)s;
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[rd]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 144) {
		const uint32_t m = mtcrf_mask(op);
		cpu->cr = (cpu->gpr[rd] & m) | (cpu->cr & ~m);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 19) {
		cpu->gpr[rd] = cpu->cr;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 922) {
		cpu->gpr[ra] = (uint32_t)(int32_t)(int16_t)(uint16_t)cpu->gpr[rd];
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[ra]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 954) {
		cpu->gpr[ra] = (uint32_t)(int32_t)(int8_t)(uint8_t)cpu->gpr[rd];
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[ra]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 24) {
		const uint32_t sh = cpu->gpr[rb] & 0x3fu;
		cpu->gpr[ra] = (sh >= 32u) ? 0 : (cpu->gpr[rd] << sh);
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[ra]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 536) {
		const uint32_t sh = cpu->gpr[rb] & 0x3fu;
		cpu->gpr[ra] = (sh >= 32u) ? 0 : (cpu->gpr[rd] >> sh);
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[ra]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 792) {
		cpu->gpr[ra] = nw_jit_helper_sraw(cpu, cpu->gpr[rd], cpu->gpr[rb]);
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[ra]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 824) {
		cpu->gpr[ra] = nw_jit_helper_sraw(cpu, cpu->gpr[rd], (uint32_t)rb);
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[ra]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 598) {
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 566) {
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 822) {
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && (xo == 342 || xo == 374)) {
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && (xo == 278 || xo == 246 || xo == 86 || xo == 54 || xo == 470 || xo == 758)) {
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 854) {
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 1014) {
		const uint32_t ea = ra_or_0(cpu, ra) + cpu->gpr[rb];
		nw_jit_helper_dcbz(cpu, ea);
		if (cpu->fault)
			return 0;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 210) {
		nw_jit_helper_mtsr(cpu, (uint32_t)ra & 0xfu, cpu->gpr[rd]);
		cpu->pc = pc + 4;
		return 1;
	}
	if (prim == 31 && xo == 242) {
		nw_jit_helper_mtsrin(cpu, cpu->gpr[rd], cpu->gpr[rb]);
		cpu->pc = pc + 4;
		return 1;
	}
	if (prim == 31 && xo == 659) {
		nw_jit_helper_mfsrin(cpu, (uint32_t)rd, cpu->gpr[rb]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 595) {
		nw_jit_helper_mfsr(cpu, (uint32_t)rd, (uint32_t)ra);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 4) {
		if (nw_jit_helper_tw(cpu, (uint32_t)rd, cpu->gpr[ra], cpu->gpr[rb]))
			return 0;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 3) {
		if (nw_jit_helper_twi(cpu, (uint32_t)rd, cpu->gpr[ra], (uint32_t)simm))
			return 0;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 17) {
		nw_jit_helper_sc(cpu);
		return 0;
	}
	if (prim == 31 && xo == 146) {
		nw_jit_helper_mtmsr(cpu, cpu->gpr[rd]);
		cpu->pc = pc + 4;
		return 1;
	}
	if (prim == 31 && xo == 83) {
		cpu->gpr[rd] = cpu->msr;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 371) {
		nw_jit_helper_mfspr(cpu, (uint32_t)rd, spr_num(op));
		if (cpu->fault)
			return 0;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 19 && xo == 150) {
		nw_jit_helper_isync(cpu);
		cpu->pc = pc + 4;
		return 1;	/* end block: icbi range may have dropped later code */
	}
	if (prim == 16) {
		const uint32_t next = pc + 4;
		nw_jit_helper_bc(cpu, op, pc);
		return (cpu->pc != next) ? 1 : 0;
	}
	if (prim == 18) {
		const int32_t disp = (((int32_t)(op << 6)) >> 6) & ~3;
		if (op & 1)
			cpu->lr = pc + 4;
		cpu->pc = (op & 2) ? (uint32_t)disp : (uint32_t)(pc + disp);
		return 1;
	}
	if (prim == 19 && xo == 0) {
		nw_jit_helper_mcrf(cpu, (uint32_t)((op >> 23) & 7u),
				   (uint32_t)((op >> 18) & 7u));
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 19 && xo == 33) {
		const uint32_t a = (cpu->cr >> (31 - ra)) & 1u;
		const uint32_t b = (cpu->cr >> (31 - rb)) & 1u;
		const uint32_t bit = 31u - (uint32_t)rd;
		const uint32_t r = (a | b) ^ 1u;
		cpu->cr = (cpu->cr & ~(1u << bit)) | (r << bit);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 19 && xo == 193) {
		const uint32_t a = (cpu->cr >> (31 - ra)) & 1u;
		const uint32_t b = (cpu->cr >> (31 - rb)) & 1u;
		const uint32_t bit = 31u - (uint32_t)rd;
		const uint32_t r = a ^ b;
		cpu->cr = (cpu->cr & ~(1u << bit)) | (r << bit);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 19 && xo == 289) {
		const uint32_t a = (cpu->cr >> (31 - ra)) & 1u;
		const uint32_t b = (cpu->cr >> (31 - rb)) & 1u;
		const uint32_t bit = 31u - (uint32_t)rd;
		const uint32_t r = (a ^ b) ^ 1u;
		cpu->cr = (cpu->cr & ~(1u << bit)) | (r << bit);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 19 && xo == 449) {
		const uint32_t a = (cpu->cr >> (31 - ra)) & 1u;
		const uint32_t b = (cpu->cr >> (31 - rb)) & 1u;
		const uint32_t bit = 31u - (uint32_t)rd;
		const uint32_t r = a | b;
		cpu->cr = (cpu->cr & ~(1u << bit)) | (r << bit);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 19 && xo == 417) {
		const uint32_t a = (cpu->cr >> (31 - ra)) & 1u;
		const uint32_t b = (cpu->cr >> (31 - rb)) & 1u;
		const uint32_t bit = 31u - (uint32_t)rd;
		const uint32_t r = a | (b ^ 1u);
		cpu->cr = (cpu->cr & ~(1u << bit)) | (r << bit);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 19 && xo == 257) {
		const uint32_t a = (cpu->cr >> (31 - ra)) & 1u;
		const uint32_t b = (cpu->cr >> (31 - rb)) & 1u;
		const uint32_t bit = 31u - (uint32_t)rd;
		const uint32_t r = a & b;
		cpu->cr = (cpu->cr & ~(1u << bit)) | (r << bit);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 19 && xo == 129) {
		const uint32_t a = (cpu->cr >> (31 - ra)) & 1u;
		const uint32_t b = (cpu->cr >> (31 - rb)) & 1u;
		const uint32_t bit = 31u - (uint32_t)rd;
		const uint32_t r = a & (b ^ 1u);
		cpu->cr = (cpu->cr & ~(1u << bit)) | (r << bit);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 19 && xo == 225) {
		const uint32_t a = (cpu->cr >> (31 - ra)) & 1u;
		const uint32_t b = (cpu->cr >> (31 - rb)) & 1u;
		const uint32_t bit = 31u - (uint32_t)rd;
		const uint32_t r = (a & b) ^ 1u;
		cpu->cr = (cpu->cr & ~(1u << bit)) | (r << bit);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 19 && (xo == 16 || xo == 528)) {
		nw_jit_helper_bclr(cpu, op, pc);
		return (cpu->pc != pc + 4) ? 1 : 0;
	}
	if (prim == 24) {
		cpu->gpr[ra] = cpu->gpr[rd] | (op & 0xffffu);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 25) {
		cpu->gpr[ra] = cpu->gpr[rd] | ((op & 0xffffu) << 16);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 26) {
		cpu->gpr[ra] = cpu->gpr[rd] ^ (op & 0xffffu);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 27) {
		cpu->gpr[ra] = cpu->gpr[rd] ^ ((op & 0xffffu) << 16);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 20) {
		const int sh = rb, mb = (int)((op >> 6) & 0x1f), me = (int)((op >> 1) & 0x1f);
		const uint32_t m = ppc_mask((uint32_t)mb, (uint32_t)me);
		cpu->gpr[ra] = (rotl32(cpu->gpr[rd], (uint32_t)sh) & m) | (cpu->gpr[ra] & ~m);
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[ra]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 21) {
		const int sh = rb, mb = (int)((op >> 6) & 0x1f), me = (int)((op >> 1) & 0x1f);
		cpu->gpr[ra] = rotl32(cpu->gpr[rd], (uint32_t)sh) & ppc_mask((uint32_t)mb, (uint32_t)me);
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[ra]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 23) {
		const int mb = (int)((op >> 6) & 0x1f), me = (int)((op >> 1) & 0x1f);
		cpu->gpr[ra] = rotl32(cpu->gpr[rd], cpu->gpr[rb] & 31u) &
			       ppc_mask((uint32_t)mb, (uint32_t)me);
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[ra]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && (xo == 266 || xo == 778)) {
		const uint32_t a = cpu->gpr[ra], b = cpu->gpr[rb];
		if (xo == 778)
			record_ov(cpu, a, b);
		cpu->gpr[rd] = a + b;
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[rd]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && (xo == 40 || xo == 552)) {
		const uint32_t a = cpu->gpr[ra], b = cpu->gpr[rb];
		if (xo == 552)
			record_ov_sub(cpu, a, b);
		cpu->gpr[rd] = b - a;
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[rd]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && (xo == 10 || xo == 522)) {
		const uint32_t a = cpu->gpr[ra], b = cpu->gpr[rb];
		record_ca(cpu, a, b);
		if (xo == 522)
			record_ov(cpu, a, b);
		cpu->gpr[rd] = a + b;
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[rd]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 138) {
		nw_jit_helper_adde(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb, op & 1u);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 650) {
		nw_jit_helper_addeo(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb, op & 1u);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 444) {
		cpu->gpr[ra] = cpu->gpr[rd] | cpu->gpr[rb];
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[ra]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 316) {
		cpu->gpr[ra] = cpu->gpr[rd] ^ cpu->gpr[rb];
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[ra]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 284) {
		cpu->gpr[ra] = ~(cpu->gpr[rd] ^ cpu->gpr[rb]);
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[ra]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 476) {
		cpu->gpr[ra] = ~(cpu->gpr[rd] & cpu->gpr[rb]);
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[ra]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 124) {
		cpu->gpr[ra] = ~(cpu->gpr[rd] | cpu->gpr[rb]);
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[ra]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 28) {
		cpu->gpr[ra] = cpu->gpr[rd] & cpu->gpr[rb];
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[ra]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 60) {
		cpu->gpr[ra] = cpu->gpr[rd] & ~cpu->gpr[rb];
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[ra]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 412) {
		nw_jit_helper_orc(cpu, (uint32_t)ra, (uint32_t)rd, (uint32_t)rb, op & 1u);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 512) {
		nw_jit_helper_mcrxr(cpu, (op >> 23) & 7u);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 26) {
		const uint32_t v = cpu->gpr[rd];
		cpu->gpr[ra] = v ? (uint32_t)__builtin_clz(v) : 32u;
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[ra]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 104) {
		cpu->gpr[rd] = 0u - cpu->gpr[ra];
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[rd]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 0) {
		if (rd & 3)
			return -1;
		record_cr_s(cpu, rd >> 2, (int32_t)cpu->gpr[ra], (int32_t)cpu->gpr[rb]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 32) {
		if (rd & 3)
			return -1;
		record_cr_u(cpu, rd >> 2, cpu->gpr[ra], cpu->gpr[rb]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 339) {
		const uint32_t spr = spr_num(op);
		if (spr == NW_PPC_SPR_DEC)
			cpu->gpr[rd] = cpu->dec;
		else if (spr == NW_PPC_SPR_LR)
			cpu->gpr[rd] = cpu->lr;
		else if (spr == NW_PPC_SPR_CTR)
			cpu->gpr[rd] = cpu->ctr;
		else if (spr == NW_PPC_SPR_XER)
			cpu->gpr[rd] = cpu->xer;
		else {
			nw_jit_helper_mfspr(cpu, (uint32_t)rd, spr);
			if (cpu->fault)
				return 0;
		}
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 467) {
		const uint32_t spr = spr_num(op);
		if (spr == NW_PPC_SPR_DEC) {
			cpu->dec = cpu->gpr[rd];
			cpu->dec_wr = 1;
		}
		else if (spr == NW_PPC_SPR_LR)
			cpu->lr = cpu->gpr[rd];
		else if (spr == NW_PPC_SPR_CTR)
			cpu->ctr = cpu->gpr[rd];
		else if (spr == NW_PPC_SPR_XER)
			cpu->xer = cpu->gpr[rd];
		else {
			nw_jit_helper_mtspr(cpu, spr, cpu->gpr[rd]);
			cpu->pc = pc + 4;
			return 1;
		}
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 32 || prim == 33) {
		const uint32_t ea = ra_or_0(cpu, ra) + (uint32_t)simm;
		if (!mem_ok(cpu, ea))
			return -1;
		cpu->gpr[rd] = mem_ld_be(cpu, ea);
		if (prim == 33 && ra)
			cpu->gpr[ra] = ea;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 34 || prim == 35) {
		const uint32_t ea = ra_or_0(cpu, ra) + (uint32_t)simm;
		if (!mem_ok_n(cpu, ea, 1))
			return -1;
		cpu->gpr[rd] = cpu->mem[ea - cpu->mem_base];
		if (prim == 35 && ra)
			cpu->gpr[ra] = ea;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 87) {
		const uint32_t ea = ra_or_0(cpu, ra) + cpu->gpr[rb];
		if (!mem_ok_n(cpu, ea, 1))
			return -1;
		cpu->gpr[rd] = cpu->mem[ea - cpu->mem_base];
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 119) {
		const uint32_t ea = cpu->gpr[ra] + cpu->gpr[rb];
		if (cpu->mem) {
			if (!mem_ok_n(cpu, ea, 1))
				return -1;
			cpu->gpr[rd] = cpu->mem[ea - cpu->mem_base];
			if (ra)
				cpu->gpr[ra] = ea;
		} else {
			int f = 0;
			uint32_t v = nw_jit_helper_lb(cpu, ea);
			f = (int)cpu->fault;
			if (f && f != 3)
				return 0;
			cpu->gpr[rd] = v;
			if (ra)
				cpu->gpr[ra] = ea;
		}
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 50) {
		cpu->pc = pc;
		nw_jit_helper_lfd(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)simm);
		if (cpu->fault)
			return -1;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 54) {
		cpu->pc = pc;
		nw_jit_helper_stfd(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)simm);
		if (cpu->fault)
			return -1;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 48) {
		cpu->pc = pc;
		nw_jit_helper_lfs(cpu, (uint32_t)rd, ra_or_0(cpu, ra) + (uint32_t)simm);
		if (cpu->fault)
			return -1;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 49) {
		if (!ra)
			return -1;
		const uint32_t ea = cpu->gpr[ra] + (uint32_t)simm;
		cpu->pc = pc;
		nw_jit_helper_lfs(cpu, (uint32_t)rd, ea);
		if (cpu->fault && cpu->fault != 3u)
			return cpu->mem ? -1 : 0;
		cpu->gpr[ra] = ea;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 51) {
		if (!ra)
			return -1;
		const uint32_t ea = cpu->gpr[ra] + (uint32_t)simm;
		cpu->pc = pc;
		nw_jit_helper_lfd(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)simm);
		if (cpu->fault && cpu->fault != 3u)
			return cpu->mem ? -1 : 0;
		cpu->gpr[ra] = ea;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 55) {
		if (!ra)
			return -1;
		const uint32_t ea = cpu->gpr[ra] + (uint32_t)simm;
		cpu->pc = pc;
		nw_jit_helper_stfd(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)simm);
		if (cpu->fault && cpu->fault != 3u)
			return cpu->mem ? -1 : 0;
		cpu->gpr[ra] = ea;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 52) {
		cpu->pc = pc;
		nw_jit_helper_stfs(cpu, (uint32_t)rd, ra_or_0(cpu, ra) + (uint32_t)simm);
		if (cpu->fault)
			return -1;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 535) {
		cpu->pc = pc;
		nw_jit_helper_lfs(cpu, (uint32_t)rd, ra_or_0(cpu, ra) + cpu->gpr[rb]);
		if (cpu->fault)
			return -1;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 663) {
		cpu->pc = pc;
		nw_jit_helper_stfs(cpu, (uint32_t)rd, ra_or_0(cpu, ra) + cpu->gpr[rb]);
		if (cpu->fault)
			return -1;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 599) {
		cpu->pc = pc;
		nw_jit_helper_lfd(cpu, (uint32_t)rd, 0, ra_or_0(cpu, ra) + cpu->gpr[rb]);
		if (cpu->fault)
			return -1;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 727) {
		cpu->pc = pc;
		nw_jit_helper_stfd(cpu, (uint32_t)rd, 0, ra_or_0(cpu, ra) + cpu->gpr[rb]);
		if (cpu->fault)
			return -1;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && (xo == 567 || xo == 631 || xo == 695 || xo == 759)) {
		if (!ra)
			return -1;
		const uint32_t ea = cpu->gpr[ra] + cpu->gpr[rb];
		cpu->pc = pc;
		if (xo == 567)
			nw_jit_helper_lfs(cpu, (uint32_t)rd, ea);
		else if (xo == 631)
			nw_jit_helper_lfd(cpu, (uint32_t)rd, 0, ea);
		else if (xo == 695)
			nw_jit_helper_stfs(cpu, (uint32_t)rd, ea);
		else
			nw_jit_helper_stfd(cpu, (uint32_t)rd, 0, ea);
		if (cpu->fault && cpu->fault != 3u)
			return cpu->mem ? -1 : 0;
		cpu->gpr[ra] = ea;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 53) {
		if (!ra)
			return -1;
		const uint32_t ea = cpu->gpr[ra] + (uint32_t)simm;
		cpu->pc = pc;
		nw_jit_helper_stfs(cpu, (uint32_t)rd, ea);
		if (cpu->fault && cpu->fault != 3u)
			return cpu->mem ? -1 : 0;
		cpu->gpr[ra] = ea;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 59) {
		const int axo = (int)((op >> 1) & 0x1f);
		const int fc = (int)((op >> 6) & 0x1f);
		if (axo == 18)
			nw_jit_helper_fdivs(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (axo == 20)
			nw_jit_helper_fsubs(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (axo == 21)
			nw_jit_helper_fadds(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (axo == 24)
			nw_jit_helper_fres(cpu, (uint32_t)rd, (uint32_t)rb);
		else if (axo == 25)
			nw_jit_helper_fmuls(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)fc);
		else if (axo == 28)
			nw_jit_helper_fmsubs(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)fc, (uint32_t)rb);
		else if (axo == 29)
			nw_jit_helper_fmadds(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)fc, (uint32_t)rb);
		else if (axo == 30)
			nw_jit_helper_fnmsubs(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)fc, (uint32_t)rb);
		else if (axo == 31)
			nw_jit_helper_fnmadds(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)fc, (uint32_t)rb);
		else
			return -1;
		if (op & 1)
			nw_jit_helper_cr1(cpu);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 63 && (xo == 32 || xo == 0)) {
		nw_jit_helper_fcmpo(cpu, (uint32_t)((op >> 23) & 7u),
				    (uint32_t)ra, (uint32_t)rb);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 63 && xo == 264) {
		nw_jit_helper_fabs(cpu, (uint32_t)rd, (uint32_t)rb);
		if (op & 1)
			nw_jit_helper_cr1(cpu);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 63 && xo == 136) {
		nw_jit_helper_fnabs(cpu, (uint32_t)rd, (uint32_t)rb);
		if (op & 1)
			nw_jit_helper_cr1(cpu);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 63 && xo == 711) {
		nw_jit_helper_mtfsf(cpu, (op >> 17) & 0xffu, (uint32_t)rb);
		if (op & 1)
			nw_jit_helper_cr1(cpu);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 63 && xo == 70) {
		nw_jit_helper_mtfsb(cpu, (uint32_t)rd, 0);
		if (op & 1)
			nw_jit_helper_cr1(cpu);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 63 && xo == 38) {
		nw_jit_helper_mtfsb(cpu, (uint32_t)rd, 1);
		if (op & 1)
			nw_jit_helper_cr1(cpu);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 63 && xo == 134) {
		nw_jit_helper_mtfsfi(cpu, (op >> 23) & 7u, (op >> 12) & 0xfu);
		if (op & 1)
			nw_jit_helper_cr1(cpu);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 63 && xo == 64) {
		nw_jit_helper_mcrfs(cpu, (op >> 23) & 7u, (op >> 18) & 7u);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 63 && xo == 40) {
		nw_jit_helper_fneg(cpu, (uint32_t)rd, (uint32_t)rb);
		if (op & 1)
			nw_jit_helper_cr1(cpu);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 63 && xo == 72) {
		nw_jit_helper_fmr(cpu, (uint32_t)rd, (uint32_t)rb);
		if (op & 1)
			nw_jit_helper_cr1(cpu);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 63 && xo == 12) {
		nw_jit_helper_frsp(cpu, (uint32_t)rd, (uint32_t)rb);
		if (op & 1)
			nw_jit_helper_cr1(cpu);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 63 && xo == 583) {
		nw_jit_helper_mffs(cpu, (uint32_t)rd);
		if (op & 1)
			nw_jit_helper_cr1(cpu);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 63 && xo == 15) {
		nw_jit_helper_fctiwz(cpu, (uint32_t)rd, (uint32_t)rb);
		if (op & 1)
			nw_jit_helper_cr1(cpu);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 63 && xo == 14) {
		nw_jit_helper_fctiw(cpu, (uint32_t)rd, (uint32_t)rb);
		if (op & 1)
			nw_jit_helper_cr1(cpu);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 63) {
		const int axo = (int)((op >> 1) & 0x1f);
		const int fc = (int)((op >> 6) & 0x1f);
		if (axo == 20)
			nw_jit_helper_fsub(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (axo == 21)
			nw_jit_helper_fadd(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (axo == 18)
			nw_jit_helper_fdiv(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (axo == 23)
			nw_jit_helper_fsel(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)fc, (uint32_t)rb);
		else if (axo == 25)
			nw_jit_helper_fmul(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)fc);
		else if (axo == 26)
			nw_jit_helper_frsqrte(cpu, (uint32_t)rd, (uint32_t)rb);
		else if (axo == 28)
			nw_jit_helper_fmsub(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)fc, (uint32_t)rb);
		else if (axo == 29)
			nw_jit_helper_fmadd(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)fc, (uint32_t)rb);
		else if (axo == 30)
			nw_jit_helper_fnmsub(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)fc, (uint32_t)rb);
		else if (axo == 31)
			nw_jit_helper_fnmadd(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)fc, (uint32_t)rb);
		else
			return -1;
		if (op & 1)
			nw_jit_helper_cr1(cpu);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && (xo == 6 || xo == 38)) {
		const uint32_t ea = ra_or_0(cpu, ra) + cpu->gpr[rb];
		nw_jit_helper_lvsl(cpu, (uint32_t)rd, ea, xo == 6);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 4) {
		const int vxo = (int)(op & 0x7ff);
		const int vaxo = (int)(op & 0x3f);
		const int vc = (int)((op >> 6) & 0x1f);
		if (vxo == 0)
			nw_jit_helper_vaddubm(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (vxo == 1156)
			nw_jit_helper_vor(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (vxo == 1028)
			nw_jit_helper_vand(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (vxo == 1092)
			nw_jit_helper_vandc(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (vxo == 1220)
			nw_jit_helper_vxor(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (vxo == 1024)
			nw_jit_helper_vsububm(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (vxo == 324)
			nw_jit_helper_vslh(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (vxo == 134 || vxo == 1158)
			nw_jit_helper_vcmpequw(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb,
					       (vxo >> 10) & 1u);
		else if (vxo == 6 || vxo == 1030)
			nw_jit_helper_vcmpequb(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb,
					       (vxo >> 10) & 1u);
		else if (vxo == 770)
			nw_jit_helper_vminsb(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (vxo == 708)
			nw_jit_helper_vsr(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (vxo == 644)
			nw_jit_helper_vsrw(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (vxo == 908)
			nw_jit_helper_vspltisw(cpu, (uint32_t)rd, (uint32_t)ra);
		else if (vxo == 452)
			nw_jit_helper_vsl(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (vxo == 1036)
			nw_jit_helper_vslo(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (vxo == 1100)
			nw_jit_helper_vsro(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (vxo == 780)
			nw_jit_helper_vspltisb(cpu, (uint32_t)rd, (uint32_t)ra);
		else if (vxo == 1604)
			nw_jit_helper_mtvscr(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (vxo == 1540)
			nw_jit_helper_mfvscr(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (vxo == 516)
			nw_jit_helper_vsrb(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (vxo == 260)
			nw_jit_helper_vslb(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (vxo == 844)
			nw_jit_helper_vspltish(cpu, (uint32_t)rd, (uint32_t)ra);
		else if (vxo == 652)
			nw_jit_helper_vspltw(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (vxo == 524)
			nw_jit_helper_vspltb(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (vxo == 12)
			nw_jit_helper_vmrghb(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (vxo == 268)
			nw_jit_helper_vmrglb(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (vxo == 140)
			nw_jit_helper_vmrghw(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (vxo == 396)
			nw_jit_helper_vmrglw(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (vxo == 1928)
			nw_jit_helper_vsumsws(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (vaxo == 42)
			nw_jit_helper_vsel(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb, (uint32_t)vc);
		else if (vaxo == 43)
			nw_jit_helper_vperm(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb, (uint32_t)vc);
		else if (vaxo == 40 || vaxo == 41)
			nw_jit_helper_vmsumshm(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb, (uint32_t)vc, vaxo == 41);
		else if (vaxo == 44)
			nw_jit_helper_vsldoi(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb, (uint32_t)vc);
		else if (vaxo == 34)
			nw_jit_helper_vmladduhm(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb, (uint32_t)vc);
		else if (vxo == 128)
			nw_jit_helper_vadduwm(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (vxo == 900)
			nw_jit_helper_vsraw(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (vxo == 462)
			nw_jit_helper_vpkswss(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else if (vxo == 1856)
			nw_jit_helper_vsubshs(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		else
			nw_jit_helper_vmx(cpu, op);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && (xo == 103 || xo == 359)) {
		cpu->pc = pc;
		nw_jit_helper_lvx(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		if (cpu->fault)
			return -1;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && (xo == 231 || xo == 487)) {
		cpu->pc = pc;
		nw_jit_helper_stvx(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb);
		if (cpu->fault)
			return -1;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 38 || prim == 39) {
		const uint32_t ea = ra_or_0(cpu, ra) + (uint32_t)simm;
		if (!mem_ok_n(cpu, ea, 1))
			return -1;
		cpu->mem[ea - cpu->mem_base] = (uint8_t)cpu->gpr[rd];
		if (prim == 39 && ra)
			cpu->gpr[ra] = ea;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 215) {
		const uint32_t ea = ra_or_0(cpu, ra) + cpu->gpr[rb];
		if (!mem_ok_n(cpu, ea, 1))
			return -1;
		cpu->mem[ea - cpu->mem_base] = (uint8_t)cpu->gpr[rd];
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 247) {
		if (!ra)
			return -1;
		const uint32_t ea = cpu->gpr[ra] + cpu->gpr[rb];
		if (!mem_ok_n(cpu, ea, 1))
			return -1;
		cpu->mem[ea - cpu->mem_base] = (uint8_t)cpu->gpr[rd];
		cpu->gpr[ra] = ea;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 36 || prim == 37) {
		const uint32_t ea = ra_or_0(cpu, ra) + (uint32_t)simm;
		if (!mem_ok(cpu, ea))
			return -1;
		mem_st_be(cpu, ea, cpu->gpr[rd]);
		if (prim == 37 && ra)
			cpu->gpr[ra] = ea;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 23) {
		const uint32_t ea = ra_or_0(cpu, ra) + cpu->gpr[rb];
		if (!mem_ok(cpu, ea))
			return -1;
		cpu->gpr[rd] = mem_ld_be(cpu, ea);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 279) {
		const uint32_t ea = ra_or_0(cpu, ra) + cpu->gpr[rb];
		if (cpu->mem) {
			if (!mem_ok_n(cpu, ea, 2))
				return -1;
			const uint8_t *p = cpu->mem + (ea - cpu->mem_base);
			cpu->gpr[rd] = ((uint32_t)p[0] << 8) | p[1];
		} else {
			cpu->pc = pc;
			cpu->gpr[rd] = nw_jit_helper_lh(cpu, ea);
			if (cpu->fault && cpu->fault != 3u)
				return 0;
		}
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 311) {
		if (!ra)
			return -1;
		const uint32_t ea = cpu->gpr[ra] + cpu->gpr[rb];
		if (cpu->mem) {
			if (!mem_ok_n(cpu, ea, 2))
				return -1;
			const uint8_t *p = cpu->mem + (ea - cpu->mem_base);
			cpu->gpr[rd] = ((uint32_t)p[0] << 8) | p[1];
			cpu->gpr[ra] = ea;
		} else {
			cpu->pc = pc;
			const uint32_t v = nw_jit_helper_lh(cpu, ea);
			if (cpu->fault && cpu->fault != 3u)
				return 0;
			cpu->gpr[rd] = v;
			cpu->gpr[ra] = ea;
		}
		cpu->pc = pc + 4;
		return 0;
	}

	if (prim == 31 && xo == 534) {
		const uint32_t ea = ra_or_0(cpu, ra) + cpu->gpr[rb];
		cpu->pc = pc;
		nw_jit_helper_lwbrx(cpu, (uint32_t)rd, ea);
		if (cpu->fault)
			return 0;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 790) {
		const uint32_t ea = ra_or_0(cpu, ra) + cpu->gpr[rb];
		cpu->pc = pc;
		nw_jit_helper_lhbrx(cpu, (uint32_t)rd, ea);
		if (cpu->fault)
			return 0;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 918) {
		const uint32_t ea = ra_or_0(cpu, ra) + cpu->gpr[rb];
		cpu->pc = pc;
		nw_jit_helper_sthbrx(cpu, (uint32_t)rd, ea);
		if (cpu->fault)
			return 0;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 662) {
		const uint32_t ea = ra_or_0(cpu, ra) + cpu->gpr[rb];
		cpu->pc = pc;
		nw_jit_helper_stwbrx(cpu, (uint32_t)rd, ea);
		if (cpu->fault)
			return 0;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && (xo == 7 || xo == 39 || xo == 71)) {
		const uint32_t sz = (xo == 7) ? 1u : (xo == 39) ? 2u : 4u;
		cpu->pc = pc;
		nw_jit_helper_lve(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb, sz);
		if (cpu->fault)
			return 0;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && (xo == 135 || xo == 167 || xo == 199)) {
		const uint32_t sz = (xo == 135) ? 1u : (xo == 167) ? 2u : 4u;
		cpu->pc = pc;
		nw_jit_helper_stve(cpu, (uint32_t)rd, (uint32_t)ra, (uint32_t)rb, sz);
		if (cpu->fault)
			return 0;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 151) {
		const uint32_t ea = ra_or_0(cpu, ra) + cpu->gpr[rb];
		if (!mem_ok(cpu, ea))
			return -1;
		mem_st_be(cpu, ea, cpu->gpr[rd]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 55) {
		const uint32_t ea = cpu->gpr[ra] + cpu->gpr[rb];
		if (cpu->mem) {
			if (!mem_ok(cpu, ea))
				return -1;
			cpu->gpr[rd] = mem_ld_be(cpu, ea);
			if (ra)
				cpu->gpr[ra] = ea;
		} else {
			int f = 0;
			uint32_t v = g_host_lwz ? g_host_lwz(cpu->host, ea, pc, &f) : 0;
			cpu->fault = (uint32_t)f;
			if (f && f != 3)
				return 0;
			cpu->gpr[rd] = v;
			if (ra)
				cpu->gpr[ra] = ea;
		}
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 183) {
		const uint32_t ea = cpu->gpr[ra] + cpu->gpr[rb];
		if (cpu->mem) {
			if (!mem_ok(cpu, ea))
				return -1;
			mem_st_be(cpu, ea, cpu->gpr[rd]);
			if (ra)
				cpu->gpr[ra] = ea;
		} else {
			int f = 0;
			if (g_host_stw)
				g_host_stw(cpu->host, ea, cpu->gpr[rd], pc, &f);
			cpu->fault = (uint32_t)f;
			if (f && f != 3)
				return 0;
			if (ra)
				cpu->gpr[ra] = ea;
		}
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 20) {
		nw_jit_helper_lwarx(cpu, (uint32_t)rd, ra_or_0(cpu, ra) + cpu->gpr[rb]);
		if (cpu->fault)
			return 0;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 150) {
		nw_jit_helper_stwcx(cpu, (uint32_t)rd, ra_or_0(cpu, ra) + cpu->gpr[rb]);
		if (cpu->fault)
			return 0;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 533) {
		nw_jit_helper_lswx(cpu, (uint32_t)rd, ra_or_0(cpu, ra) + cpu->gpr[rb], cpu->xer);
		if (cpu->fault)
			return 0;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 661) {
		nw_jit_helper_stswx(cpu, (uint32_t)rd, ra_or_0(cpu, ra) + cpu->gpr[rb], cpu->xer);
		if (cpu->fault)
			return 0;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && (xo == 597 || xo == 725)) {
		unsigned nb = (unsigned)((op >> 11) & 31u);
		if (nb == 0)
			nb = 32;
		if (xo == 597)
			nw_jit_helper_lswx(cpu, (uint32_t)rd, ra_or_0(cpu, ra), nb);
		else
			nw_jit_helper_stswx(cpu, (uint32_t)rd, ra_or_0(cpu, ra), nb);
		if (cpu->fault)
			return 0;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 982) {
		nw_jit_helper_icbi(cpu, ra_or_0(cpu, ra) + cpu->gpr[rb]);
		cpu->pc = pc + 4;
		return 1;
	}
	if (prim == 31 && xo == 306) {
		nw_jit_helper_tlbie(cpu, cpu->gpr[rb]);
		cpu->pc = pc + 4;
		return 1;
	}
	if (prim == 31 && xo == 202) {
		const uint32_t a = cpu->gpr[ra];
		const uint32_t ca = (cpu->xer >> 29) & 1u;
		record_ca(cpu, a, ca);
		cpu->gpr[rd] = a + ca;
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[rd]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 200) {
		const uint32_t a = ~cpu->gpr[ra];
		const uint32_t ca = (cpu->xer >> 29) & 1u;
		record_ca(cpu, a, ca);
		cpu->gpr[rd] = a + ca;
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[rd]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 234) {
		nw_jit_helper_addme(cpu, (uint32_t)rd, (uint32_t)ra, op & 1u);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 232) {
		nw_jit_helper_subfme(cpu, (uint32_t)rd, (uint32_t)ra, op & 1u);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 370) {
		nw_jit_helper_tlbia(cpu);
		cpu->pc = pc + 4;
		return 1;
	}
	if (prim == 19 && xo == 50) {
		nw_jit_helper_rfi(cpu);
		return 1;
	}
	if (prim == 31 && xo == 407) {
		const uint32_t ea = ra_or_0(cpu, ra) + cpu->gpr[rb];
		if (!mem_ok_n(cpu, ea, 2))
			return -1;
		uint8_t *p = cpu->mem + (ea - cpu->mem_base);
		p[0] = (uint8_t)(cpu->gpr[rd] >> 8);
		p[1] = (uint8_t)cpu->gpr[rd];
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 439) {
		if (!ra)
			return -1;
		const uint32_t ea = cpu->gpr[ra] + cpu->gpr[rb];
		if (!mem_ok_n(cpu, ea, 2))
			return -1;
		uint8_t *p = cpu->mem + (ea - cpu->mem_base);
		p[0] = (uint8_t)(cpu->gpr[rd] >> 8);
		p[1] = (uint8_t)cpu->gpr[rd];
		cpu->gpr[ra] = ea;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && (xo == 343 || xo == 375)) {
		const uint32_t ea = (xo == 375 ? cpu->gpr[ra] : ra_or_0(cpu, ra)) + cpu->gpr[rb];
		if (!mem_ok_n(cpu, ea, 2))
			return -1;
		const uint8_t *p = cpu->mem + (ea - cpu->mem_base);
		const uint16_t h = (uint16_t)(((uint32_t)p[0] << 8) | p[1]);
		cpu->gpr[rd] = (uint32_t)(int16_t)h;
		if (xo == 375 && ra)
			cpu->gpr[ra] = ea;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 40 || prim == 41 || prim == 42 || prim == 43) {
		const uint32_t ea = (prim == 41) ? (cpu->gpr[ra] + (uint32_t)simm)
						 : (ra_or_0(cpu, ra) + (uint32_t)simm);
		if (prim == 41 && !ra)
			return -1;
		uint16_t h;
		if (cpu->mem) {
			if (!mem_ok_n(cpu, ea, 2))
				return -1;
			const uint8_t *p = cpu->mem + (ea - cpu->mem_base);
			h = (uint16_t)(((uint32_t)p[0] << 8) | p[1]);
		} else if (prim == 41) {
			cpu->pc = pc;
			uint32_t v = nw_jit_helper_lh(cpu, ea);
			if (cpu->fault && cpu->fault != 3u)
				return 0;
			cpu->gpr[rd] = v & 0xffffu;
			cpu->gpr[ra] = ea;
			cpu->pc = pc + 4;
			return 0;
		} else
			return -1;
		uint32_t v = (prim == 40 || prim == 41) ? h : (uint32_t)(int16_t)h;
		cpu->gpr[rd] = v;
		if ((prim == 41 || prim == 43) && ra)
			cpu->gpr[ra] = ea;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 44 || prim == 45) {
		const uint32_t ea = ra_or_0(cpu, ra) + (uint32_t)simm;
		if (!mem_ok_n(cpu, ea, 2))
			return -1;
		uint8_t *p = cpu->mem + (ea - cpu->mem_base);
		p[0] = (uint8_t)(cpu->gpr[rd] >> 8);
		p[1] = (uint8_t)cpu->gpr[rd];
		if (prim == 45 && ra)
			cpu->gpr[ra] = ea;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 46) {
		uint32_t ea = ra_or_0(cpu, ra) + (uint32_t)simm;
		const uint32_t n = 4u * (32u - (uint32_t)rd);
		if (!mem_ok_n(cpu, ea, n))
			return -1;
		nw_jit_helper_lmw(cpu, ea, (uint32_t)rd);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 47) {
		uint32_t ea = ra_or_0(cpu, ra) + (uint32_t)simm;
		const uint32_t n = 4u * (32u - (uint32_t)rd);
		if (!mem_ok_n(cpu, ea, n))
			return -1;
		nw_jit_helper_stmw(cpu, ea, (uint32_t)rd);
		cpu->pc = pc + 4;
		return 0;
	}
	return -1;
}

int nw_jit_interp_n(struct nw_jit_cpu *cpu, const uint32_t *ops, int n, uint32_t start_pc)
{
	cpu->pc = start_pc;
	int last = 0;
	for (int i = 0; i < n; i++) {
		last = nw_jit_interp_one(cpu, ops[i]);
		if (last != 0)
			return last;
		if (cpu->fault)
			return 0;
	}
	return last;
}

#if defined(__aarch64__)

enum {
	W0 = 0, W1 = 1, W2 = 2, W3 = 3, W4 = 4, W5 = 5, W8 = 8, W9 = 9, W10 = 10, W11 = 11, W12 = 12, W13 = 13, W14 = 14,
	X0 = 0, X1 = 1, X9 = 9, X10 = 10, X11 = 11, X12 = 12, X13 = 13, X19 = 19
};

struct emit {
	uint32_t *p;
	uint32_t *end;
	uint32_t *fault_br[NW_JIT_MAX_BLOCK];
	int nfault;
	int uses_fpr;
	int uses_vr;
	uint32_t gpr_mask;
	uint32_t last_pc;
	int just_set_pc;
	int last_st_r;
	int last_st_wt;
	/* Guest GPR cached in x21–x24 for this block. Memory stays current
	 * on every store; a BLR drops the cache because helpers write gpr[]. */
	int pin_gpr[4];
	int pin_next;
};

static int emit_imm32(struct emit *e, int rd, uint32_t v);
static int emit_imm64(struct emit *e, int xd, uint64_t v);

static int emit_w(struct emit *e, uint32_t w)
{
	e->just_set_pc = 0;
	e->last_st_r = -1;
	if (e->p >= e->end)
		return 0;
	*e->p++ = w;
	/* BLR Xn. Helpers read and write cpu->gpr; callee-saved pins would
	 * be stale after the call. */
	if ((w & 0xfffffc1fu) == 0xd63f0000u) {
		for (int i = 0; i < 4; i++)
			e->pin_gpr[i] = -1;
	}
	return 1;
}

static uint32_t a64_add_reg(int rd, int rn, int rm)
{
	return 0x0b000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_mul_w(int rd, int rn, int rm)
{
	return 0x1b007c00u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_udiv_w(int rd, int rn, int rm)
{
	return 0x1ac00800u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_umull_x(int rd, int rn, int rm)
{
	return 0x9ba07c00u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_smull_x(int rd, int rn, int rm)
{
	return 0x9b207c00u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_lsr_x32(int rd, int rn)
{
	return 0xd360fc00u | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_adds_reg(int rd, int rn, int rm)
{
	return 0x2b000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_subs_reg(int rd, int rn, int rm)
{
	return 0x6b000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_sbcs_reg(int rd, int rn, int rm)
{
	return 0x7a000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_cmp_imm1(int rn)
{
	return 0x7100041fu | ((uint32_t)rn << 5);
}

static uint32_t a64_sub_reg(int rd, int rn, int rm)
{
	return 0x4b000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_and_reg(int rd, int rn, int rm)
{
	return 0x0a000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_bic(int rd, int rn, int rm)
{
	return 0x0a200000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_sxth(int rd, int rn)
{
	return 0x13003c00u | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_sxtb(int rd, int rn)
{
	return 0x13001c00u | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_lslv(int rd, int rn, int rm)
{
	return 0x1ac02000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_lsrv(int rd, int rn, int rm)
{
	return 0x1ac02400u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_rorv(int rd, int rn, int rm)
{
	return 0x1ac02c00u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_csel(int rd, int rn, int rm, int cond)
{
	return 0x1a800000u | ((uint32_t)cond << 12) | ((uint32_t)rm << 16) |
	       ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_orr_reg(int rd, int rn, int rm)
{
	return 0x2a000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_clz(int rd, int rn)
{
	return 0x5ac01000u | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_movz(int rd, uint32_t imm16, int hw)
{
	return 0x52800000u | ((uint32_t)hw << 21) | (imm16 << 5) | (uint32_t)rd;
}

static uint32_t a64_movk(int rd, uint32_t imm16, int hw)
{
	return 0x72800000u | ((uint32_t)hw << 21) | (imm16 << 5) | (uint32_t)rd;
}

static uint32_t a64_ldr_w(int rt, int rn, uint32_t off)
{
	return 0xb9400000u | ((off >> 2) << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}

static uint32_t a64_ldr_x(int rt, int rn, uint32_t off)
{
	return 0xf9400000u | ((off >> 3) << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}

static uint32_t a64_str_w(int rt, int rn, uint32_t off)
{
	return 0xb9000000u | ((off >> 2) << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}

static uint32_t a64_extr(int rd, int rn, int rm, int lsb)
{
	return 0x13800000u | ((uint32_t)rm << 16) | ((uint32_t)lsb << 10) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_lsr(int rd, int rn, int n)
{
	return 0x53007c00u | ((uint32_t)n << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_ubfx(int rd, int rn, int lsb, int width)
{
	const uint32_t immr = (uint32_t)lsb;
	const uint32_t imms = (uint32_t)(lsb + width - 1);
	return 0x53000000u | (immr << 16) | (imms << 10) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_lsl(int rd, int rn, int n)
{
	uint32_t immr = (uint32_t)(-n) & 31u;
	uint32_t imms = (uint32_t)(31 - n);
	return 0x53000000u | (immr << 16) | (imms << 10) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_and_imm1(int rd, int rn)
{
	return 0x12000000u | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_eor_imm1(int rd, int rn)
{
	return 0x52000000u | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_eor_reg(int rd, int rn, int rm)
{
	return 0x4a000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_cbz(int rt, int imm19)
{
	return 0x34000000u | (((uint32_t)imm19 & 0x7ffffu) << 5) | (uint32_t)rt;
}

static uint32_t a64_cbnz(int rt, int imm19)
{
	return 0x35000000u | (((uint32_t)imm19 & 0x7ffffu) << 5) | (uint32_t)rt;
}

static uint32_t a64_cbnz64(int rt, int imm19)
{
	return 0xb5000000u | (((uint32_t)imm19 & 0x7ffffu) << 5) | (uint32_t)rt;
}

static uint32_t a64_cbz64(int rt, int imm19)
{
	return 0xb4000000u | (((uint32_t)imm19 & 0x7ffffu) << 5) | (uint32_t)rt;
}

static uint32_t a64_br(int xn)
{
	return 0xd61f0000u | ((uint32_t)xn << 5);
}

static uint32_t a64_b_cond(int cond, int imm19)
{
	return 0x54000000u | (((uint32_t)imm19 & 0x7ffffu) << 5) | (uint32_t)(cond & 15);
}

static uint32_t a64_b(int imm26)
{
	return 0x14000000u | ((uint32_t)imm26 & 0x3ffffffu);
}

static uint32_t a64_tbz(int rt, int bit, int imm14)
{
	const uint32_t b5 = ((uint32_t)bit >> 5) & 1u;
	const uint32_t b40 = (uint32_t)bit & 31u;
	return (b5 << 31) | 0x36000000u | (b40 << 19) |
	       (((uint32_t)imm14 & 0x3fffu) << 5) | (uint32_t)rt;
}

static uint32_t a64_cmp_w(int rn, int rm)
{
	return 0x6b00001fu | ((uint32_t)rm << 16) | ((uint32_t)rn << 5);
}

static uint32_t a64_add_x_lsl(int rd, int rn, int rm, int sh)
{
	return 0x8b000000u | ((uint32_t)rm << 16) | ((uint32_t)sh << 10) |
	       ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_and_dtlb_idx(int rd, int rn)
{
	static_assert(NW_JIT_DTLB_N == 1024, "AND #0x3ff");
	return 0x12002400u | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_and_imm_off12(int rd, int rn)
{
	/* AND Wd, Wn, #0xfff */
	return 0x12002c00u | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_and_imm_page(int rd, int rn)
{
	/* AND Wd, Wn, #0xfffff000 */
	return 0x12144c00u | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static int emit_blr_x19(struct emit *e, uint32_t off)
{
	if (!emit_w(e, a64_ldr_x(X9, X19, off)))
		return 0;
	return emit_w(e, 0xd63f0120u);			/* BLR X9 */
}

/* W8 = EA. Hit: helper_pa(cpu, pa). Miss: helper_ea(cpu, ea). W2 preserved. */
static int emit_dtlb_and_helpers(struct emit *e, int is_store)
{
	static_assert((offsetof(struct nw_jit_cpu, jit_dtlb) & 7) == 0, "jit_dtlb 8-aligned");

	static_assert((offsetof(struct nw_jit_cpu, jit_dtlb_hit) & 7) == 0, "jit_dtlb_hit 8-aligned");
	static_assert((offsetof(struct nw_jit_cpu, jit_sr_gen) & 7) == 0, "jit_sr_gen 8-aligned");
	static_assert((offsetof(struct nw_jit_cpu, jit_lwz) & 7) == 0, "jit_lwz 8-aligned");
	static_assert((offsetof(struct nw_jit_cpu, jit_stw) & 7) == 0, "jit_stw 8-aligned");
	static_assert((offsetof(struct nw_jit_cpu, jit_lwz_pa) & 7) == 0, "jit_lwz_pa 8-aligned");
	static_assert((offsetof(struct nw_jit_cpu, jit_stw_pa) & 7) == 0, "jit_stw_pa 8-aligned");
	const uint32_t off_miss = is_store
		? (uint32_t)offsetof(struct nw_jit_cpu, jit_stw)
		: (uint32_t)offsetof(struct nw_jit_cpu, jit_lwz);
	const uint32_t off_hit = is_store
		? (uint32_t)offsetof(struct nw_jit_cpu, jit_stw_pa)
		: (uint32_t)offsetof(struct nw_jit_cpu, jit_lwz_pa);
	if (!emit_w(e, a64_ldr_w(W12, X19, (uint32_t)offsetof(struct nw_jit_cpu, msr))))
		return 0;
	if (!emit_w(e, a64_ubfx(W14, W12, 14, 1)))	/* W14 = MSR[PR] */
		return 0;
	uint32_t *dr_off = e->p;
	if (!emit_w(e, a64_tbz(W12, 4, 0)))
		return 0;
	if (!emit_w(e, a64_ldr_x(X10, X19, (uint32_t)offsetof(struct nw_jit_cpu, jit_dtlb))))
		return 0;
	if (!emit_w(e, a64_lsr(W9, W8, 12)))
		return 0;
	if (!emit_w(e, a64_and_dtlb_idx(W9, W9)))
		return 0;
	/* Set stride is 2×32. Way 1 is +32; tag miss on way 0 tries way 1. */
	if (!emit_w(e, a64_add_x_lsl(X11, X10, 9, 6)))
		return 0;
	if (!emit_w(e, a64_ldr_w(W12, X11, 0)))
		return 0;
	if (!emit_w(e, a64_and_imm_page(W13, W8)))
		return 0;
	if (!emit_w(e, a64_cmp_w(W12, W13)))
		return 0;
	uint32_t *tag0_eq = e->p;
	if (!emit_w(e, a64_b_cond(0, 0)))		/* B.EQ checks */
		return 0;
	if (!emit_w(e, 0x9100816bu))			/* ADD X11, X11, #32 */
		return 0;
	if (!emit_w(e, a64_ldr_w(W12, X11, 0)))
		return 0;
	if (!emit_w(e, a64_cmp_w(W12, W13)))
		return 0;
	uint32_t *tag_ne = e->p;
	if (!emit_w(e, a64_b_cond(1, 0)))
		return 0;
	uint32_t *checks = e->p;
	*tag0_eq = a64_b_cond(0, (int)(checks - tag0_eq));
	if (!emit_w(e, a64_ldr_w(W12, X11, 8)))
		return 0;
	uint32_t *nv = e->p;
	if (!emit_w(e, a64_tbz(W12, 0, 0)))
		return 0;
	if (!emit_w(e, a64_ubfx(W13, W12, 3, 1)))	/* fill PR */
		return 0;
	if (!emit_w(e, a64_cmp_w(W13, W14)))
		return 0;
	uint32_t *pr_ne = e->p;
	if (!emit_w(e, a64_b_cond(1, 0)))
		return 0;
	uint32_t *nw = NULL;
	uint32_t *sr_ne = NULL;
	if (is_store) {
		nw = e->p;
		if (!emit_w(e, a64_tbz(W12, 1, 0)))
			return 0;
	}
	/* sr_gen at +12. Match stays on this path. A segment change misses
	 * and the helper refills. */
	if (!emit_w(e, a64_ldr_w(15, 11, 12)))
		return 0;
	if (!emit_w(e, a64_lsr(16, 8, 28)))
		return 0;
	if (!emit_w(e, a64_ldr_x(17, 19, (uint32_t)offsetof(struct nw_jit_cpu, jit_sr_gen))))
		return 0;
	/* LDR W16, [X17, W16, UXTW #2] */
	if (!emit_w(e, 0xb8600800u | (16u << 16) | (2u << 13) | (1u << 12) | (17u << 5) | 16u))
		return 0;
	if (!emit_w(e, a64_cmp_w(15, 16)))
		return 0;
	sr_ne = e->p;
	if (!emit_w(e, a64_b_cond(1, 0)))
		return 0;
	if (!emit_w(e, a64_ldr_w(W12, X11, 4)))
		return 0;
	if (!emit_w(e, a64_and_imm_off12(W1, W8)))
		return 0;
	if (!emit_w(e, a64_orr_reg(W1, W1, W12)))
		return 0;
	uint32_t *to_inline = e->p;
	if (!emit_w(e, a64_b(0)))
		return 0;
	/* MSR[DR] off: PA = EA, no translated cache. */
	uint32_t *ident_p = e->p;
	if (!emit_w(e, a64_orr_reg(W1, 31, W8)))
		return 0;
	uint32_t *ident_to_pa = e->p;
	if (!emit_w(e, a64_b(0)))
		return 0;
	uint32_t *miss_p = e->p;
	if (!emit_w(e, a64_orr_reg(W1, 31, W8)))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!emit_blr_x19(e, off_miss))
		return 0;
	uint32_t *to_join = e->p;
	if (!emit_w(e, a64_b(0)))
		return 0;
	uint32_t *inline_p = e->p;
	if (!emit_w(e, 0xf940096cu))			/* LDR X12, [X11, #16] host */
		return 0;
	uint32_t *no_host = e->p;
	if (!emit_w(e, 0xb400000cu))			/* CBZ X12, helper_pa */
		return 0;
	if (!emit_w(e, a64_and_imm_off12(W13, W8)))
		return 0;
	if (!emit_w(e, 0x8b2d418cu))			/* ADD X12, X12, W13, UXTW */
		return 0;
	if (is_store) {
		if (!emit_w(e, 0x5ac00840u))		/* REV W0, W2 */
			return 0;
		if (!emit_w(e, a64_str_w(W0, X12, 0)))
			return 0;
	} else {
		if (!emit_w(e, a64_ldr_w(W0, X12, 0)))
			return 0;
		if (!emit_w(e, 0x5ac00800u))		/* REV W0, W0 */
			return 0;
	}
	if (!emit_w(e, a64_ldr_x(X10, X19, (uint32_t)offsetof(struct nw_jit_cpu, jit_dtlb_hit))))
		return 0;
	if (!emit_w(e, 0xf940014du))			/* LDR X13, [X10] */
		return 0;
	if (!emit_w(e, 0x910005adu))			/* ADD X13, X13, #1 */
		return 0;
	if (!emit_w(e, 0xf900014du))			/* STR X13, [X10] */
		return 0;
	uint32_t *inline_join = e->p;
	if (!emit_w(e, a64_b(0)))
		return 0;
	uint32_t *hit_p = e->p;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!emit_blr_x19(e, off_hit))
		return 0;
	uint32_t *join = e->p;
	*dr_off = a64_tbz(W12, 4, (int)(ident_p - dr_off));
	*tag_ne = a64_b_cond(1, (int)(miss_p - tag_ne));
	*nv = a64_tbz(W12, 0, (int)(miss_p - nv));
	*pr_ne = a64_b_cond(1, (int)(miss_p - pr_ne));
	if (nw)
		*nw = a64_tbz(W12, 1, (int)(miss_p - nw));
	if (sr_ne)
		*sr_ne = a64_b_cond(1, (int)(miss_p - sr_ne));
	*to_inline = a64_b((int)(inline_p - to_inline));
	*ident_to_pa = a64_b((int)(hit_p - ident_to_pa));
	*to_join = a64_b((int)(join - to_join));
	*no_host = 0xb4000000u | (((uint32_t)(hit_p - no_host) & 0x7ffffu) << 5) | 12u;
	*inline_join = a64_b((int)(join - inline_join));
	(void)join;
	return 1;
}

static int emit_imm32(struct emit *e, int rd, uint32_t v)
{
	if (!emit_w(e, a64_movz(rd, v & 0xffffu, 0)))
		return 0;
	return emit_w(e, a64_movk(rd, v >> 16, 1));
}

static int pin_find(struct emit *e, int r)
{
	for (int i = 0; i < 4; i++) {
		if (e->pin_gpr[i] == r)
			return i;
	}
	return -1;
}

static int pin_hold(struct emit *e, int wt, int r)
{
	int s = pin_find(e, r);
	if (s < 0) {
		s = e->pin_next & 3;
		e->pin_next++;
	}
	if (!emit_w(e, a64_orr_reg(21 + s, 31, wt)))
		return 0;
	e->pin_gpr[s] = r;
	return 1;
}

static int emit_load_gpr(struct emit *e, int wt, int r)
{
	if (e->last_st_r == r && e->last_st_wt == wt)
		return 1;
	const int s = pin_find(e, r);
	if (s >= 0)
		return emit_w(e, a64_orr_reg(wt, 31, 21 + s));
	if (!emit_w(e, a64_ldr_w(wt, X0, (uint32_t)offsetof(struct nw_jit_cpu, gpr) + (uint32_t)r * 4u)))
		return 0;
	return pin_hold(e, wt, r);
}

static int emit_store_gpr(struct emit *e, int wt, int r)
{
	if (!emit_w(e, a64_str_w(wt, X0, (uint32_t)offsetof(struct nw_jit_cpu, gpr) + (uint32_t)r * 4u)))
		return 0;
	if (!pin_hold(e, wt, r))
		return 0;
	e->last_st_r = r;
	e->last_st_wt = wt;
	return 1;
}

static int emit_ra_or_0(struct emit *e, int wt, int ra)
{
	if (ra == 0)
		return emit_w(e, a64_movz(wt, 0, 0));
	return emit_load_gpr(e, wt, ra);
}

static int emit_set_pc(struct emit *e, uint32_t pc)
{
	if (e->just_set_pc && e->last_pc == pc)
		return 1;
	if (!emit_imm32(e, W8, pc))
		return 0;
	if (!emit_w(e, a64_str_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, pc))))
		return 0;
	e->last_pc = pc;
	e->just_set_pc = 1;
	return 1;
}

static uint32_t a64_movz64(int rd, uint32_t imm16, int hw)
{
	return 0xd2800000u | ((uint32_t)hw << 21) | (imm16 << 5) | (uint32_t)rd;
}

static uint32_t a64_movk64(int rd, uint32_t imm16, int hw)
{
	return 0xf2800000u | ((uint32_t)hw << 21) | (imm16 << 5) | (uint32_t)rd;
}

static int emit_imm64(struct emit *e, int xd, uint64_t v)
{
	if (!emit_w(e, a64_movz64(xd, (uint32_t)(v & 0xffffu), 0)))
		return 0;
	if (!emit_w(e, a64_movk64(xd, (uint32_t)((v >> 16) & 0xffffu), 1)))
		return 0;
	if (!emit_w(e, a64_movk64(xd, (uint32_t)((v >> 32) & 0xffffu), 2)))
		return 0;
	return emit_w(e, a64_movk64(xd, (uint32_t)((v >> 48) & 0xffffu), 3));
}

static int emit_prologue(struct emit *e)
{
	/* 64-byte frame: x29/x30, x19, and pin regs x21–x24. */
	if (!emit_w(e, 0xa9bc7bfdu))		/* stp x29, x30, [sp, #-64]! */
		return 0;
	if (!emit_w(e, 0xf9000bf3u))		/* str x19, [sp, #16] */
		return 0;
	if (!emit_w(e, 0xa9025bf5u))		/* stp x21, x22, [sp, #32] */
		return 0;
	if (!emit_w(e, 0xa90363f7u))		/* stp x23, x24, [sp, #48] */
		return 0;
	if (!emit_w(e, 0xaa0003f3u))		/* mov x19, x0 */
		return 0;
	/* Live path binds from C. Harness memset leaves jit_dtlb NULL. */
	if (!emit_w(e, a64_ldr_x(X9, X19, (uint32_t)offsetof(struct nw_jit_cpu, jit_dtlb))))
		return 0;
	uint32_t *bound = e->p;
	if (!emit_w(e, a64_cbnz64(X9, 0)))
		return 0;
	if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_cpu_bind))
		return 0;
	if (!emit_w(e, 0xd63f0120u))		/* blr x9 */
		return 0;
	if (!emit_w(e, 0xaa1303e0u))		/* mov x0, x19 */
		return 0;
	*bound = a64_cbnz64(X9, (int)(e->p - bound));
	return 1;
}

static int emit_pop_frame(struct emit *e)
{
	if (!emit_w(e, 0xa9425bf5u))		/* ldp x21, x22, [sp, #32] */
		return 0;
	if (!emit_w(e, 0xa94363f7u))		/* ldp x23, x24, [sp, #48] */
		return 0;
	if (!emit_w(e, 0xf9400bf3u))		/* ldr x19, [sp, #16] */
		return 0;
	return emit_w(e, 0xa8c47bfdu);		/* ldp x29, x30, [sp], #64 */
}

static int emit_ret(struct emit *e)
{
	if (!emit_pop_frame(e))
		return 0;
	return emit_w(e, 0xd65f03c0u);		/* ret */
}

/*
 * Pop this frame, x0=cpu, br to the successor prologue. JIT br/blr
 * without this pop nested frames and blacked or died in NK. Cap is
 * NW_JIT_TAIL_MAX (start at 1).
 */
static int emit_chain_epilogue(struct emit *e, uint32_t chain_pc)
{
	if (!chain_pc)
		return emit_ret(e);
	uint32_t cur_class = 0;
	if (e->uses_fpr)
		cur_class |= 1u;
	if (e->uses_vr)
		cur_class |= 2u;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!emit_imm32(e, W1, chain_pc))
		return 0;
	if (!emit_imm32(e, W2, cur_class))
		return 0;
	if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_chain))
		return 0;
	if (!emit_w(e, 0xd63f0120u))
		return 0;
	uint32_t *cbz = e->p;
	if (!emit_w(e, a64_cbz64(X0, 0)))
		return 0;
	if (!emit_w(e, 0xaa0003e1u))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!emit_pop_frame(e))
		return 0;
	if (!emit_w(e, a64_br(X1)))
		return 0;
	*cbz = a64_cbz64(X0, (int)(e->p - cbz));
	return emit_ret(e);
}

static int emit_fault_check(struct emit *e)
{
	if (!emit_w(e, a64_ldr_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, fault))))
		return 0;
	if (e->nfault >= NW_JIT_MAX_BLOCK)
		return 0;
	e->fault_br[e->nfault++] = e->p;
	return emit_w(e, a64_cbnz(W8, 0));
}

static int emit_helper_ea(struct emit *e, int ra, int simm)
{
	if (!emit_ra_or_0(e, W8, ra))
		return 0;
	if (!emit_imm32(e, W9, (uint32_t)simm))
		return 0;
	return emit_w(e, a64_add_reg(W8, W8, W9));
}

static int emit_call_lwz(struct emit *e, uint32_t pc, int rd, int ra, int simm, int upd)
{
	if (!emit_set_pc(e, pc))
		return 0;
	if (!emit_helper_ea(e, ra, simm))
		return 0;
	if (!emit_dtlb_and_helpers(e, 0))
		return 0;
	if (!emit_w(e, a64_orr_reg(W8, 31, W0)))	/* mov w8, w0 */
		return 0;
	if (!emit_w(e, 0xaa1303e0u))			/* mov x0, x19 */
		return 0;
	if (!emit_store_gpr(e, W8, rd))
		return 0;
	if (!emit_fault_check(e))
		return 0;
	if (upd && ra) {
		if (!emit_helper_ea(e, ra, simm))
			return 0;
		if (!emit_store_gpr(e, W8, ra))
			return 0;
	}
	return 1;
}

static int emit_call_lb(struct emit *e, uint32_t pc, int rd, int ra, int simm, int upd)
{
	if (!emit_set_pc(e, pc))
		return 0;
	if (!emit_helper_ea(e, ra, simm))
		return 0;
	if (upd && ra) {
		if (!emit_w(e, 0xb9001be8u))	/* str w8, [sp, #24]  saved EA */
			return 0;
	}
	if (!emit_w(e, a64_orr_reg(W1, 31, W8)))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_lb))
		return 0;
	if (!emit_w(e, 0xd63f0120u))
		return 0;
	if (!emit_w(e, a64_orr_reg(W8, 31, W0)))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!emit_store_gpr(e, W8, rd))
		return 0;
	if (!upd || !ra)
		return emit_fault_check(e);
	if (!emit_w(e, a64_ldr_w(W9, X0, (uint32_t)offsetof(struct nw_jit_cpu, fault))))
		return 0;
	uint32_t *cbnz_p = e->p;
	if (!emit_w(e, a64_cbnz(W9, 0)))
		return 0;
	if (!emit_w(e, 0xb9401beau))	/* ldr w10, [sp, #24] saved EA */
		return 0;
	if (!emit_store_gpr(e, W10, ra))
		return 0;
	uint32_t *after_p = e->p;
	*cbnz_p = a64_cbnz(W9, (int)(after_p - cbnz_p));
	return emit_fault_check(e);
}

static int emit_call_stb(struct emit *e, uint32_t pc, int rs, int ra, int simm, int upd)
{
	if (!emit_set_pc(e, pc))
		return 0;
	if (!emit_helper_ea(e, ra, simm))
		return 0;
	if (!emit_load_gpr(e, W2, rs))
		return 0;
	if (!emit_w(e, a64_orr_reg(W1, 31, W8)))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_stb))
		return 0;
	if (!emit_w(e, 0xd63f0120u))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!upd || !ra)
		return emit_fault_check(e);
	if (!emit_w(e, a64_ldr_w(W10, X0, (uint32_t)offsetof(struct nw_jit_cpu, fault))))
		return 0;
	uint32_t *cbz_p = e->p;
	if (!emit_w(e, a64_cbz(W10, 0)))
		return 0;
	if (!emit_w(e, 0x71000d1fu))
		return 0;
	uint32_t *bne_p = e->p;
	if (!emit_w(e, a64_b_cond(1, 0)))
		return 0;
	uint32_t *upd_p = e->p;
	if (!emit_helper_ea(e, ra, simm))
		return 0;
	if (!emit_store_gpr(e, W8, ra))
		return 0;
	uint32_t *after_p = e->p;
	*cbz_p = a64_cbz(W10, (int)(upd_p - cbz_p));
	*bne_p = a64_b_cond(1, (int)(after_p - bne_p));
	if (e->nfault >= NW_JIT_MAX_BLOCK)
		return 0;
	e->fault_br[e->nfault++] = e->p;
	return emit_w(e, a64_cbnz(W10, 0));
}

static int emit_helper_ea_idx(struct emit *e, int ra, int rb)
{
	if (!emit_ra_or_0(e, W8, ra))
		return 0;
	if (!emit_load_gpr(e, W9, rb))
		return 0;
	return emit_w(e, a64_add_reg(W8, W8, W9));
}

static int emit_call_lbx(struct emit *e, uint32_t pc, int rd, int ra, int rb, int upd)
{
	if (!emit_set_pc(e, pc))
		return 0;
	if (!emit_helper_ea_idx(e, ra, rb))
		return 0;
	if (upd && ra) {
		if (!emit_w(e, 0xb9001be8u))	/* str w8, [sp, #24] saved EA */
			return 0;
	}
	if (!emit_w(e, a64_orr_reg(W1, 31, W8)))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_lb))
		return 0;
	if (!emit_w(e, 0xd63f0120u))
		return 0;
	if (!emit_w(e, a64_orr_reg(W9, 31, W0)))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!upd || !ra) {
		if (!emit_fault_check(e))
			return 0;
		return emit_store_gpr(e, W9, rd);
	}
	if (!emit_store_gpr(e, W9, rd))
		return 0;
	if (!emit_w(e, a64_ldr_w(W9, X0, (uint32_t)offsetof(struct nw_jit_cpu, fault))))
		return 0;
	uint32_t *cbnz_p = e->p;
	if (!emit_w(e, a64_cbnz(W9, 0)))
		return 0;
	if (!emit_w(e, 0xb9401beau))	/* ldr w10, [sp, #24] saved EA */
		return 0;
	if (!emit_store_gpr(e, W10, ra))
		return 0;
	uint32_t *after_p = e->p;
	*cbnz_p = a64_cbnz(W9, (int)(after_p - cbnz_p));
	return emit_fault_check(e);
}

static int emit_call_stw(struct emit *e, uint32_t pc, int rs, int ra, int simm, int upd)
{
	if (!emit_set_pc(e, pc))
		return 0;
	if (!emit_helper_ea(e, ra, simm))
		return 0;
	if (!emit_load_gpr(e, W2, rs))			/* w2 = value */
		return 0;
	if (!emit_dtlb_and_helpers(e, 1))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))			/* mov x0, x19 */
		return 0;
	if (!upd || !ra)
		return emit_fault_check(e);
	/*
	 * stwu writes before RA update. DSI/IO (fault 1/2) must not
	 * update RA. SMC (3) is a successful store into the executing
	 * page: RA still updates, then the block stops. Skipping RA on
	 * SMC left r1 stale after a stack push (splash type 10).
	 */
	if (!emit_w(e, a64_ldr_w(W10, X0, (uint32_t)offsetof(struct nw_jit_cpu, fault))))
		return 0;
	uint32_t *cbz_p = e->p;
	if (!emit_w(e, a64_cbz(W10, 0)))
		return 0;
	if (!emit_w(e, 0x71000d1fu))			/* CMP W10, #3 */
		return 0;
	uint32_t *bne_p = e->p;
	if (!emit_w(e, a64_b_cond(1, 0)))		/* B.NE skip_upd */
		return 0;
	uint32_t *upd_p = e->p;
	if (!emit_helper_ea(e, ra, simm))
		return 0;
	if (!emit_store_gpr(e, W8, ra))
		return 0;
	uint32_t *after_p = e->p;
	*cbz_p = a64_cbz(W10, (int)(upd_p - cbz_p));
	*bne_p = a64_b_cond(1, (int)(after_p - bne_p));
	if (e->nfault >= NW_JIT_MAX_BLOCK)
		return 0;
	e->fault_br[e->nfault++] = e->p;
	return emit_w(e, a64_cbnz(W10, 0));
}

static int emit_call_lwzux(struct emit *e, uint32_t pc, int rd, int ra, int rb)
{
	if (!ra)
		return 0;
	if (!emit_set_pc(e, pc))
		return 0;
	if (!emit_helper_ea_idx(e, ra, rb))
		return 0;
	if (!emit_w(e, 0xb9001be8u))	/* str w8, [sp, #24] saved EA */
		return 0;
	if (!emit_dtlb_and_helpers(e, 0))
		return 0;
	if (!emit_w(e, a64_orr_reg(W9, 31, W0)))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!emit_w(e, a64_ldr_w(W10, X0, (uint32_t)offsetof(struct nw_jit_cpu, fault))))
		return 0;
	uint32_t *cbz_p = e->p;
	if (!emit_w(e, a64_cbz(W10, 0)))
		return 0;
	if (!emit_w(e, 0x71000d1fu))	/* CMP W10, #3 */
		return 0;
	uint32_t *bne_p = e->p;
	if (!emit_w(e, a64_b_cond(1, 0)))
		return 0;
	uint32_t *upd_p = e->p;
	if (!emit_store_gpr(e, W9, rd))
		return 0;
	if (!emit_w(e, 0xb9401be8u))	/* ldr w8, [sp, #24] */
		return 0;
	if (!emit_store_gpr(e, W8, ra))
		return 0;
	uint32_t *after_p = e->p;
	*cbz_p = a64_cbz(W10, (int)(upd_p - cbz_p));
	*bne_p = a64_b_cond(1, (int)(after_p - bne_p));
	if (e->nfault >= NW_JIT_MAX_BLOCK)
		return 0;
	e->fault_br[e->nfault++] = e->p;
	return emit_w(e, a64_cbnz(W10, 0));
}

static int emit_upd_ra_on_ok(struct emit *e, int ra)
{
	if (!emit_w(e, a64_ldr_w(W10, X0, (uint32_t)offsetof(struct nw_jit_cpu, fault))))
		return 0;
	uint32_t *cbz_p = e->p;
	if (!emit_w(e, a64_cbz(W10, 0)))
		return 0;
	if (!emit_w(e, 0x71000d1fu))	/* CMP W10, #3 */
		return 0;
	uint32_t *bne_p = e->p;
	if (!emit_w(e, a64_b_cond(1, 0)))
		return 0;
	uint32_t *upd_p = e->p;
	if (!emit_w(e, 0xb9401be8u))	/* ldr w8, [sp, #24] saved EA; W10 stays fault */
		return 0;
	if (!emit_store_gpr(e, W8, ra))
		return 0;
	uint32_t *after_p = e->p;
	*cbz_p = a64_cbz(W10, (int)(upd_p - cbz_p));
	*bne_p = a64_b_cond(1, (int)(after_p - bne_p));
	if (e->nfault >= NW_JIT_MAX_BLOCK)
		return 0;
	e->fault_br[e->nfault++] = e->p;
	return emit_w(e, a64_cbnz(W10, 0));
}

static int emit_call_fp_d_upd(struct emit *e, uint32_t pc, int fr, int ra, int simm, int kind)
{
	if (!ra)
		return 0;
	if (!emit_set_pc(e, pc))
		return 0;
	if (!emit_helper_ea(e, ra, simm))
		return 0;
	if (!emit_w(e, 0xb9001be8u))	/* str w8, [sp, #24] saved EA */
		return 0;
	if (kind == 0) {
		if (!emit_imm32(e, W1, (uint32_t)fr))
			return 0;
		if (!emit_w(e, a64_orr_reg(W2, 31, W8)))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_lfs))
			return 0;
	} else if (kind == 3) {
		if (!emit_imm32(e, W1, (uint32_t)fr))
			return 0;
		if (!emit_w(e, a64_orr_reg(W2, 31, W8)))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_stfs))
			return 0;
	} else {
		if (!emit_imm32(e, W1, (uint32_t)fr))
			return 0;
		if (!emit_imm32(e, W2, (uint32_t)ra))
			return 0;
		if (!emit_imm32(e, W3, (uint32_t)simm))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)(kind == 1 ? nw_jit_helper_lfd : nw_jit_helper_stfd)))
			return 0;
	}
	if (!emit_w(e, 0xd63f0120u))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	return emit_upd_ra_on_ok(e, ra);
}

static int emit_call_lhzu(struct emit *e, uint32_t pc, int rd, int ra, int simm)
{
	if (!ra)
		return 0;
	if (!emit_set_pc(e, pc))
		return 0;
	if (!emit_helper_ea(e, ra, simm))
		return 0;
	if (!emit_w(e, 0xb9001be8u))
		return 0;
	if (!emit_w(e, a64_orr_reg(W1, 31, W8)))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_lh))
		return 0;
	if (!emit_w(e, 0xd63f0120u))
		return 0;
	if (!emit_w(e, a64_orr_reg(W9, 31, W0)))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!emit_w(e, a64_ldr_w(W10, X0, (uint32_t)offsetof(struct nw_jit_cpu, fault))))
		return 0;
	uint32_t *cbz_p = e->p;
	if (!emit_w(e, a64_cbz(W10, 0)))
		return 0;
	if (!emit_w(e, 0x71000d1fu))
		return 0;
	uint32_t *bne_p = e->p;
	if (!emit_w(e, a64_b_cond(1, 0)))
		return 0;
	uint32_t *upd_p = e->p;
	if (!emit_store_gpr(e, W9, rd))
		return 0;
	if (!emit_w(e, 0xb9401be8u))	/* ldr w8, [sp, #24]; W10 stays fault */
		return 0;
	if (!emit_store_gpr(e, W8, ra))
		return 0;
	uint32_t *after_p = e->p;
	*cbz_p = a64_cbz(W10, (int)(upd_p - cbz_p));
	*bne_p = a64_b_cond(1, (int)(after_p - bne_p));
	if (e->nfault >= NW_JIT_MAX_BLOCK)
		return 0;
	e->fault_br[e->nfault++] = e->p;
	return emit_w(e, a64_cbnz(W10, 0));
}

static int emit_call_lbzux(struct emit *e, uint32_t pc, int rd, int ra, int rb)
{
	if (!ra)
		return 0;
	if (!emit_set_pc(e, pc))
		return 0;
	if (!emit_helper_ea_idx(e, ra, rb))
		return 0;
	if (!emit_w(e, 0xb9001be8u))	/* str w8, [sp, #24] saved EA */
		return 0;
	if (!emit_w(e, a64_orr_reg(W1, 31, W8)))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_lb))
		return 0;
	if (!emit_w(e, 0xd63f0120u))
		return 0;
	if (!emit_w(e, a64_orr_reg(W9, 31, W0)))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!emit_w(e, a64_ldr_w(W10, X0, (uint32_t)offsetof(struct nw_jit_cpu, fault))))
		return 0;
	uint32_t *cbz_p = e->p;
	if (!emit_w(e, a64_cbz(W10, 0)))
		return 0;
	if (!emit_w(e, 0x71000d1fu))
		return 0;
	uint32_t *bne_p = e->p;
	if (!emit_w(e, a64_b_cond(1, 0)))
		return 0;
	uint32_t *upd_p = e->p;
	if (!emit_store_gpr(e, W9, rd))
		return 0;
	if (!emit_w(e, 0xb9401be8u))	/* ldr w8, [sp, #24] saved EA; W10 stays fault */
		return 0;
	if (!emit_store_gpr(e, W8, ra))
		return 0;
	uint32_t *after_p = e->p;
	*cbz_p = a64_cbz(W10, (int)(upd_p - cbz_p));
	*bne_p = a64_b_cond(1, (int)(after_p - bne_p));
	if (e->nfault >= NW_JIT_MAX_BLOCK)
		return 0;
	e->fault_br[e->nfault++] = e->p;
	return emit_w(e, a64_cbnz(W10, 0));
}

static int emit_call_stbx(struct emit *e, uint32_t pc, int rs, int ra, int rb, int upd)
{
	if (!emit_set_pc(e, pc))
		return 0;
	if (!emit_helper_ea_idx(e, ra, rb))
		return 0;
	if (!emit_load_gpr(e, W2, rs))
		return 0;
	if (!emit_w(e, a64_orr_reg(W1, 31, W8)))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_stb))
		return 0;
	if (!emit_w(e, 0xd63f0120u))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!upd || !ra)
		return emit_fault_check(e);
	if (!emit_w(e, a64_ldr_w(W10, X0, (uint32_t)offsetof(struct nw_jit_cpu, fault))))
		return 0;
	uint32_t *cbz_p = e->p;
	if (!emit_w(e, a64_cbz(W10, 0)))
		return 0;
	if (!emit_w(e, 0x71000d1fu))
		return 0;
	uint32_t *bne_p = e->p;
	if (!emit_w(e, a64_b_cond(1, 0)))
		return 0;
	uint32_t *upd_p = e->p;
	if (!emit_helper_ea_idx(e, ra, rb))
		return 0;
	if (!emit_store_gpr(e, W8, ra))
		return 0;
	uint32_t *after_p = e->p;
	*cbz_p = a64_cbz(W10, (int)(upd_p - cbz_p));
	*bne_p = a64_b_cond(1, (int)(after_p - bne_p));
	if (e->nfault >= NW_JIT_MAX_BLOCK)
		return 0;
	e->fault_br[e->nfault++] = e->p;
	return emit_w(e, a64_cbnz(W10, 0));
}

static int emit_call_stwx(struct emit *e, uint32_t pc, int rs, int ra, int rb, int upd)
{
	if (!emit_set_pc(e, pc))
		return 0;
	if (!emit_helper_ea_idx(e, ra, rb))
		return 0;
	if (!emit_load_gpr(e, W2, rs))
		return 0;
	if (!emit_dtlb_and_helpers(e, 1))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!upd || !ra)
		return emit_fault_check(e);
	if (!emit_w(e, a64_ldr_w(W10, X0, (uint32_t)offsetof(struct nw_jit_cpu, fault))))
		return 0;
	uint32_t *cbz_p = e->p;
	if (!emit_w(e, a64_cbz(W10, 0)))
		return 0;
	if (!emit_w(e, 0x71000d1fu))
		return 0;
	uint32_t *bne_p = e->p;
	if (!emit_w(e, a64_b_cond(1, 0)))
		return 0;
	uint32_t *upd_p = e->p;
	if (!emit_helper_ea_idx(e, ra, rb))
		return 0;
	if (!emit_store_gpr(e, W8, ra))
		return 0;
	uint32_t *after_p = e->p;
	*cbz_p = a64_cbz(W10, (int)(upd_p - cbz_p));
	*bne_p = a64_b_cond(1, (int)(after_p - bne_p));
	if (e->nfault >= NW_JIT_MAX_BLOCK)
		return 0;
	e->fault_br[e->nfault++] = e->p;
	return emit_w(e, a64_cbnz(W10, 0));
}

static int emit_call_sthx(struct emit *e, uint32_t pc, int rs, int ra, int rb, int upd)
{
	if (!emit_set_pc(e, pc))
		return 0;
	if (!emit_helper_ea_idx(e, ra, rb))
		return 0;
	if (!emit_load_gpr(e, W2, rs))
		return 0;
	if (!emit_w(e, a64_orr_reg(W1, 31, W8)))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_sth))
		return 0;
	if (!emit_w(e, 0xd63f0120u))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!upd || !ra)
		return emit_fault_check(e);
	if (!emit_w(e, a64_ldr_w(W10, X0, (uint32_t)offsetof(struct nw_jit_cpu, fault))))
		return 0;
	uint32_t *cbz_p = e->p;
	if (!emit_w(e, a64_cbz(W10, 0)))
		return 0;
	if (!emit_w(e, 0x71000d1fu))
		return 0;
	uint32_t *bne_p = e->p;
	if (!emit_w(e, a64_b_cond(1, 0)))
		return 0;
	uint32_t *upd_p = e->p;
	if (!emit_helper_ea_idx(e, ra, rb))
		return 0;
	if (!emit_store_gpr(e, W8, ra))
		return 0;
	uint32_t *after_p = e->p;
	*cbz_p = a64_cbz(W10, (int)(upd_p - cbz_p));
	*bne_p = a64_b_cond(1, (int)(after_p - bne_p));
	if (e->nfault >= NW_JIT_MAX_BLOCK)
		return 0;
	e->fault_br[e->nfault++] = e->p;
	return emit_w(e, a64_cbnz(W10, 0));
}

static int emit_call_lwzx(struct emit *e, uint32_t pc, int rd, int ra, int rb, int upd)
{
	if (!emit_set_pc(e, pc))
		return 0;
	if (!emit_helper_ea_idx(e, ra, rb))
		return 0;
	if (!emit_dtlb_and_helpers(e, 0))
		return 0;
	if (!emit_w(e, a64_orr_reg(W9, 31, W0)))	/* value; fault_check uses W8 */
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!emit_fault_check(e))
		return 0;
	if (!emit_store_gpr(e, W9, rd))
		return 0;
	if (upd && ra) {
		if (!emit_helper_ea_idx(e, ra, rb))
			return 0;
		if (!emit_store_gpr(e, W8, ra))
			return 0;
	}
	return 1;
}

static int emit_call_lhx(struct emit *e, uint32_t pc, int rd, int ra, int rb, int upd)
{
	if (!emit_set_pc(e, pc))
		return 0;
	if (!emit_helper_ea_idx(e, ra, rb))
		return 0;
	if (!emit_w(e, a64_orr_reg(W1, 31, W8)))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_lh))
		return 0;
	if (!emit_w(e, 0xd63f0120u))
		return 0;
	if (!emit_w(e, a64_orr_reg(W9, 31, W0)))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!emit_fault_check(e))
		return 0;
	if (!emit_w(e, a64_sxth(W9, W9)))
		return 0;
	if (upd && ra) {
		/* rA = original EA, even if rD = rA (Mac uses that form). */
		if (!emit_w(e, a64_orr_reg(W10, 31, W9)))
			return 0;
		if (!emit_helper_ea_idx(e, ra, rb))
			return 0;
		if (!emit_store_gpr(e, W10, rd))
			return 0;
		if (!emit_store_gpr(e, W8, ra))
			return 0;
		return 1;
	}
	return emit_store_gpr(e, W9, rd);
}

static int emit_call_lhzux(struct emit *e, uint32_t pc, int rd, int ra, int rb)
{
	if (!ra)
		return 0;
	if (!emit_set_pc(e, pc))
		return 0;
	if (!emit_helper_ea_idx(e, ra, rb))
		return 0;
	if (!emit_w(e, 0xb9001be8u))	/* str w8, [sp, #24] saved EA */
		return 0;
	if (!emit_w(e, a64_orr_reg(W1, 31, W8)))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_lh))
		return 0;
	if (!emit_w(e, 0xd63f0120u))
		return 0;
	if (!emit_w(e, a64_orr_reg(W9, 31, W0)))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!emit_w(e, a64_ldr_w(W10, X0, (uint32_t)offsetof(struct nw_jit_cpu, fault))))
		return 0;
	uint32_t *cbz_p = e->p;
	if (!emit_w(e, a64_cbz(W10, 0)))
		return 0;
	if (!emit_w(e, 0x71000d1fu))	/* CMP W10, #3 */
		return 0;
	uint32_t *bne_p = e->p;
	if (!emit_w(e, a64_b_cond(1, 0)))
		return 0;
	uint32_t *upd_p = e->p;
	if (!emit_store_gpr(e, W9, rd))
		return 0;
	if (!emit_w(e, 0xb9401be8u))	/* ldr w8, [sp, #24]; W10 stays fault */
		return 0;
	if (!emit_store_gpr(e, W8, ra))
		return 0;
	uint32_t *after_p = e->p;
	*cbz_p = a64_cbz(W10, (int)(upd_p - cbz_p));
	*bne_p = a64_b_cond(1, (int)(after_p - bne_p));
	if (e->nfault >= NW_JIT_MAX_BLOCK)
		return 0;
	e->fault_br[e->nfault++] = e->p;
	return emit_w(e, a64_cbnz(W10, 0));
}

static int emit_call_lh(struct emit *e, uint32_t pc, int rd, int ra, int simm, int sext, int upd)
{
	if (!emit_set_pc(e, pc))
		return 0;
	if (!emit_helper_ea(e, ra, simm))
		return 0;
	if (!emit_w(e, a64_orr_reg(W1, 31, W8)))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_lh))
		return 0;
	if (!emit_w(e, 0xd63f0120u))
		return 0;
	if (!emit_w(e, a64_orr_reg(W8, 31, W0)))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (sext && !emit_w(e, a64_sxth(W8, W8)))
		return 0;
	if (!emit_store_gpr(e, W8, rd))
		return 0;
	if (upd && ra) {
		if (!emit_helper_ea(e, ra, simm))
			return 0;
		if (!emit_store_gpr(e, W8, ra))
			return 0;
	}
	return emit_fault_check(e);
}

static int emit_call_sth(struct emit *e, uint32_t pc, int rs, int ra, int simm, int upd)
{
	if (!emit_set_pc(e, pc))
		return 0;
	if (!emit_helper_ea(e, ra, simm))
		return 0;
	if (!emit_load_gpr(e, W2, rs))
		return 0;
	if (!emit_w(e, a64_orr_reg(W1, 31, W8)))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_sth))
		return 0;
	if (!emit_w(e, 0xd63f0120u))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!upd || !ra)
		return emit_fault_check(e);
	if (!emit_w(e, a64_ldr_w(W10, X0, (uint32_t)offsetof(struct nw_jit_cpu, fault))))
		return 0;
	uint32_t *cbz_p = e->p;
	if (!emit_w(e, a64_cbz(W10, 0)))
		return 0;
	if (!emit_w(e, 0x71000d1fu))
		return 0;
	uint32_t *bne_p = e->p;
	if (!emit_w(e, a64_b_cond(1, 0)))
		return 0;
	uint32_t *upd_p = e->p;
	if (!emit_helper_ea(e, ra, simm))
		return 0;
	if (!emit_store_gpr(e, W8, ra))
		return 0;
	uint32_t *after_p = e->p;
	*cbz_p = a64_cbz(W10, (int)(upd_p - cbz_p));
	*bne_p = a64_b_cond(1, (int)(after_p - bne_p));
	if (e->nfault >= NW_JIT_MAX_BLOCK)
		return 0;
	e->fault_br[e->nfault++] = e->p;
	return emit_w(e, a64_cbnz(W10, 0));
}

static int emit_cr_field_from_flags(struct emit *e, int crfd, uint32_t b_lt)
{
	const int sh = 28 - 4 * crfd;
	const uint32_t keep = ~(0xfu << sh);
	if (!emit_w(e, a64_movz(W9, 2, 0)))
		return 0;
	if (!emit_w(e, b_lt))
		return 0;
	if (!emit_w(e, 0x54000080u))
		return 0;
	if (!emit_w(e, a64_movz(W9, 4, 0)))
		return 0;
	if (!emit_w(e, 0x14000002u))
		return 0;
	if (!emit_w(e, a64_movz(W9, 8, 0)))
		return 0;
	if (!emit_w(e, a64_ldr_w(W10, X0, (uint32_t)offsetof(struct nw_jit_cpu, xer))))
		return 0;
	if (!emit_w(e, a64_lsr(W10, W10, 31)))
		return 0;
	if (!emit_w(e, a64_orr_reg(W9, W9, W10)))
		return 0;
	if (!emit_w(e, a64_ldr_w(W10, X0, (uint32_t)offsetof(struct nw_jit_cpu, cr))))
		return 0;
	if (!emit_imm32(e, W8, keep))
		return 0;
	if (!emit_w(e, a64_and_reg(W10, W10, W8)))
		return 0;
	if (sh && !emit_w(e, a64_lsl(W9, W9, sh)))
		return 0;
	if (!emit_w(e, a64_orr_reg(W10, W10, W9)))
		return 0;
	return emit_w(e, a64_str_w(W10, X0, (uint32_t)offsetof(struct nw_jit_cpu, cr)));
}

static int emit_cr0_from_flags(struct emit *e, uint32_t b_lt)
{
	return emit_cr_field_from_flags(e, 0, b_lt);
}

static int emit_xer_ca_from_cs(struct emit *e)
{
	/* CS from ADDS. Leaves W8 (sum) alone. NZCV unchanged. */
	if (!emit_w(e, a64_ldr_w(W9, X0, (uint32_t)offsetof(struct nw_jit_cpu, xer))))
		return 0;
	if (!emit_imm32(e, W10, ~0x20000000u))
		return 0;
	if (!emit_w(e, a64_and_reg(W9, W9, W10)))
		return 0;
	if (!emit_w(e, 0x1a9f37eau))			/* CSET W10, CS */
		return 0;
	if (!emit_w(e, a64_lsl(W10, W10, 29)))
		return 0;
	if (!emit_w(e, a64_orr_reg(W9, W9, W10)))
		return 0;
	return emit_w(e, a64_str_w(W9, X0, (uint32_t)offsetof(struct nw_jit_cpu, xer)));
}

static int emit_xer_ov_from_vs(struct emit *e)
{
	/* VS from ADDS (still live after CA). OV replaced, SO sticky. */
	if (!emit_w(e, a64_ldr_w(W9, X0, (uint32_t)offsetof(struct nw_jit_cpu, xer))))
		return 0;
	if (!emit_imm32(e, W10, ~0x40000000u))
		return 0;
	if (!emit_w(e, a64_and_reg(W9, W9, W10)))
		return 0;
	if (!emit_w(e, 0x1a9f77eau))			/* CSET W10, VS */
		return 0;
	if (!emit_w(e, a64_lsl(W10, W10, 30)))
		return 0;
	if (!emit_w(e, a64_orr_reg(W9, W9, W10)))
		return 0;
	if (!emit_w(e, a64_lsl(W10, W10, 1)))
		return 0;
	if (!emit_w(e, a64_orr_reg(W9, W9, W10)))
		return 0;
	return emit_w(e, a64_str_w(W9, X0, (uint32_t)offsetof(struct nw_jit_cpu, xer)));
}

static int emit_cr0_from_w8(struct emit *e)
{
	/* w8 = signed result. B.MI tests N of the result, not a compare. */
	if (!emit_w(e, 0x7100011fu))			/* SUBS WZR, W8, #0 */
		return 0;
	return emit_cr0_from_flags(e, 0x54000084u);	/* B.MI +4 */
}

static int emit_maybe_cr1(struct emit *e, uint32_t op)
{
	if (!(op & 1u))
		return 1;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_cr1))
		return 0;
	if (!emit_w(e, 0xd63f0120u))
		return 0;
	return emit_w(e, 0xaa1303e0u);
}

static int emit_call_fp_x_upd(struct emit *e, uint32_t pc, int fr, int ra, int rb, int kind)
{
	if (!ra)
		return 0;
	if (!emit_set_pc(e, pc))
		return 0;
	if (!emit_helper_ea_idx(e, ra, rb))
		return 0;
	if (!emit_w(e, 0xb9001be8u))	/* str w8, [sp, #24] saved EA */
		return 0;
	if (!emit_imm32(e, W1, (uint32_t)fr))
		return 0;
	if (kind == 0 || kind == 2) {
		if (!emit_w(e, a64_orr_reg(W2, 31, W8)))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)(kind == 0 ? nw_jit_helper_lfs : nw_jit_helper_stfs)))
			return 0;
	} else {
		if (!emit_imm32(e, W2, 0))
			return 0;
		if (!emit_w(e, a64_orr_reg(W3, 31, W8)))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)(kind == 1 ? nw_jit_helper_lfd : nw_jit_helper_stfd)))
			return 0;
	}
	if (!emit_w(e, 0xd63f0120u))
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	return emit_upd_ra_on_ok(e, ra);
}

static uint32_t a64_ldr_d(int rt, int rn, uint32_t off)
{
	return 0xfd400000u | ((off >> 3) << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}

static uint32_t a64_str_d(int rt, int rn, uint32_t off)
{
	return 0xfd000000u | ((off >> 3) << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}

static uint32_t fpr_off(int r)
{
	return (uint32_t)offsetof(struct nw_jit_cpu, fpr) + (uint32_t)(r & 31) * 8u;
}

/* Single-precision ops as ARM instructions. If FPSCR exception enables
 * are set, the existing helper runs instead. */
static int emit_fp_inline(struct emit *e, int kind, int rd, int ra, int rb, int fc, uint32_t op)
{
	const int unary = (kind >= 101 && kind <= 103);
	const int is_cmp = (kind == 104);
	const int src0 = (is_cmp || !(unary || kind == 100)) ? ra : rb;
	/* ARM inline of these ops, mixed integer/FP blocks, and the
	 * taken-branch hop each shipped once and iTunes lost its text
	 * and its audio. Every op stays on the helper. */
	const int fast = 0;
	(void)is_cmp;
	uint32_t *slow = NULL;
	if (fast && !unary) {
		if (!emit_w(e, a64_ldr_w(W8, X19, (uint32_t)offsetof(struct nw_jit_cpu, fpscr))))
			return 0;
		if (!emit_w(e, a64_movz(W9, 0xf8u, 0)))
			return 0;
		if (!emit_w(e, 0x0a090108u))	/* and w8, w8, w9 */
			return 0;
		slow = e->p;
		if (!emit_w(e, a64_cbnz(W8, 0)))
			return 0;
	}
	if (fast && !emit_w(e, a64_ldr_d(0, X19, fpr_off(src0))))
		return 0;
	if (fast && kind == 103) {
		if (!emit_w(e, a64_str_d(0, X19, fpr_off(rd))))
			return 0;
	} else if (fast && kind == 102) {
		if (!emit_w(e, 0x1e614000u))	/* fneg d0, d0 */
			return 0;
		if (!emit_w(e, a64_str_d(0, X19, fpr_off(rd))))
			return 0;
	} else if (fast && kind == 101) {
		if (!emit_w(e, 0x1e60c000u))	/* fabs d0, d0 */
			return 0;
		if (!emit_w(e, a64_str_d(0, X19, fpr_off(rd))))
			return 0;
	} else if (fast && is_cmp) {
		if (!emit_w(e, a64_ldr_d(1, X19, fpr_off(rb))))
			return 0;
		if (!emit_w(e, 0x1e612000u))	/* fcmp d0, d1 */
			return 0;
		if (!emit_w(e, 0x52800028u))	/* mov w8, #1  unordered */
			return 0;
		uint32_t *vs = e->p;
		if (!emit_w(e, a64_b_cond(6, 0)))	/* b.vs */
			return 0;
		if (!emit_w(e, 0x1a9fa7e8u))	/* cset w8, lt */
			return 0;
		if (!emit_w(e, a64_lsl(W8, W8, 3)))
			return 0;
		if (!emit_w(e, 0x1a9fd7e9u))	/* cset w9, gt */
			return 0;
		if (!emit_w(e, a64_lsl(W9, W9, 2)))
			return 0;
		if (!emit_w(e, 0x2a090108u))	/* orr w8, w8, w9 */
			return 0;
		if (!emit_w(e, 0x1a9f17e9u))	/* cset w9, eq */
			return 0;
		if (!emit_w(e, a64_lsl(W9, W9, 1)))
			return 0;
		if (!emit_w(e, 0x2a090108u))
			return 0;
		*vs = a64_b_cond(6, (int)(e->p - vs));
		/* FPCC in FPSCR[15:12], then the CR field. */
		if (!emit_w(e, a64_ldr_w(W9, X19, (uint32_t)offsetof(struct nw_jit_cpu, fpscr))))
			return 0;
		if (!emit_w(e, a64_movz(W10, 0xf000u, 0)))
			return 0;
		if (!emit_w(e, 0x0a2a0129u))	/* bic w9, w9, w10 */
			return 0;
		if (!emit_w(e, 0x2a083129u))	/* orr w9, w9, w8, lsl #12 */
			return 0;
		if (!emit_w(e, a64_str_w(W9, X19, (uint32_t)offsetof(struct nw_jit_cpu, fpscr))))
			return 0;
		const int sh = 28 - 4 * (int)((op >> 23) & 7u);
		if (sh) {
			if (!emit_w(e, a64_lsl(W8, W8, sh)))
				return 0;
		}
		if (!emit_w(e, a64_ldr_w(W9, X19, (uint32_t)offsetof(struct nw_jit_cpu, cr))))
			return 0;
		if (!emit_w(e, a64_movz(W10, 0xfu, 0)))
			return 0;
		if (sh && !emit_w(e, a64_lsl(W10, W10, sh)))
			return 0;
		if (!emit_w(e, 0x0a2a0129u))	/* bic w9, w9, w10 */
			return 0;
		if (!emit_w(e, 0x2a080129u))	/* orr w9, w9, w8 */
			return 0;
		if (!emit_w(e, a64_str_w(W9, X19, (uint32_t)offsetof(struct nw_jit_cpu, cr))))
			return 0;
	} else if (fast) {
		/* fmuls is fra*frc. fmadds is fra*frc+frb. The rest use fra, frb. */
		const int two = (kind == 18 || kind == 20 || kind == 21 || kind == 25 || kind == 100);
		const int breg = (kind == 25 || (kind >= 28 && kind <= 31)) ? fc : rb;
		if (kind != 100) {
			if (!emit_w(e, a64_ldr_d(1, X19, fpr_off(breg))))
				return 0;
		}
		if (!two && kind != 100) {
			if (!emit_w(e, a64_ldr_d(2, X19, fpr_off(rb))))
				return 0;
		}
		if (!emit_w(e, 0x1e624000u))	/* fcvt s0, d0 */
			return 0;
		if (kind != 100 && !emit_w(e, 0x1e624021u))	/* fcvt s1, d1 */
			return 0;
		if (!two && kind != 100 && !emit_w(e, 0x1e624042u))	/* fcvt s2, d2 */
			return 0;
		uint32_t opc = 0;
		if (kind == 21)
			opc = 0x1e212800u;		/* fadd s0, s0, s1 */
		else if (kind == 20)
			opc = 0x1e213800u;		/* fsub */
		else if (kind == 18)
			opc = 0x1e211800u;		/* fdiv */
		else if (kind == 25)
			opc = 0x1e210800u;		/* fmul s0, s0, s1  fra*frc */
		else if (kind == 29)
			opc = 0x1f010800u;		/* fmadd s0, s0, s1, s2 */
		else if (kind == 28)
			opc = 0x1f218800u;		/* fnmsub: Sn*Sm - Sa */
		else if (kind == 30)
			opc = 0x1f018800u;		/* fmsub: Sa - Sn*Sm */
		else if (kind == 31)
			opc = 0x1f210800u;		/* fnmadd */
		if (kind != 100 && !opc)
			return 0;
		if (opc && !emit_w(e, opc))
			return 0;
		if (!emit_w(e, 0x1e22c000u))	/* fcvt d0, s0 */
			return 0;
		if (!emit_w(e, a64_str_d(0, X19, fpr_off(rd))))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_fprf_fd))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
	}
	uint32_t *over = NULL;
	if (!fast || slow) {
		if (slow) {
			over = e->p;
			if (!emit_w(e, a64_b(0)))
				return 0;
			*slow = a64_cbnz(W8, (int)(e->p - slow));
		}
		void *fn = NULL;
		int narg = 2;
		if (kind == 18) { fn = (void *)nw_jit_helper_fdivs; narg = 3; }
		else if (kind == 20) { fn = (void *)nw_jit_helper_fsubs; narg = 3; }
		else if (kind == 21) { fn = (void *)nw_jit_helper_fadds; narg = 3; }
		else if (kind == 25) { fn = (void *)nw_jit_helper_fmuls; narg = 3; }
		else if (kind == 28) { fn = (void *)nw_jit_helper_fmsubs; narg = 4; }
		else if (kind == 29) { fn = (void *)nw_jit_helper_fmadds; narg = 4; }
		else if (kind == 30) { fn = (void *)nw_jit_helper_fnmsubs; narg = 4; }
		else if (kind == 31) { fn = (void *)nw_jit_helper_fnmadds; narg = 4; }
		else if (kind == 100) { fn = (void *)nw_jit_helper_frsp; narg = 2; }
		else if (kind == 101) { fn = (void *)nw_jit_helper_fabs; narg = 2; }
		else if (kind == 102) { fn = (void *)nw_jit_helper_fneg; narg = 2; }
		else if (kind == 103) { fn = (void *)nw_jit_helper_fmr; narg = 2; }
		else if (kind == 104) { fn = (void *)nw_jit_helper_fcmpo; narg = 3; }
		if (!fn)
			return 0;
		if (!emit_imm32(e, W1, is_cmp ? (uint32_t)((op >> 23) & 7u) : (uint32_t)rd))
			return 0;
		if (is_cmp) {
			if (!emit_imm32(e, W2, (uint32_t)ra))
				return 0;
			if (!emit_imm32(e, W3, (uint32_t)rb))
				return 0;
		} else if (narg == 2) {
			if (!emit_imm32(e, W2, (uint32_t)rb))
				return 0;
		} else if (kind == 25) {
			if (!emit_imm32(e, W2, (uint32_t)ra))
				return 0;
			if (!emit_imm32(e, W3, (uint32_t)fc))
				return 0;
		} else if (narg == 3) {
			if (!emit_imm32(e, W2, (uint32_t)ra))
				return 0;
			if (!emit_imm32(e, W3, (uint32_t)rb))
				return 0;
		} else {
			if (!emit_imm32(e, W2, (uint32_t)ra))
				return 0;
			if (!emit_imm32(e, W3, (uint32_t)fc))
				return 0;
			if (!emit_imm32(e, W4, (uint32_t)rb))
				return 0;
		}
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)fn))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (over)
			*over = a64_b((int)(e->p - over));
	}
	return emit_maybe_cr1(e, op);
}

static int emit_op(struct emit *e, uint32_t op, uint32_t pc, int is_last)
{
	const int prim = (int)(op >> 26);
	const int rd = (int)((op >> 21) & 0x1f);
	const int ra = (int)((op >> 16) & 0x1f);
	const int rb = (int)((op >> 11) & 0x1f);
	const int xo = (int)((op >> 1) & 0x3ff);
	const int simm = (int16_t)(op & 0xffffu);

	if (prim == 14) {
		if (!emit_ra_or_0(e, W8, ra))
			return 0;
		if (!emit_imm32(e, W9, (uint32_t)simm))
			return 0;
		if (!emit_w(e, a64_add_reg(W8, W8, W9)))
			return 0;
		return emit_store_gpr(e, W8, rd);
	}
	if (prim == 15) {
		if (!emit_ra_or_0(e, W8, ra))
			return 0;
		if (!emit_imm32(e, W9, (uint32_t)simm << 16))
			return 0;
		if (!emit_w(e, a64_add_reg(W8, W8, W9)))
			return 0;
		return emit_store_gpr(e, W8, rd);
	}
	if (prim == 7) {
		if (!emit_load_gpr(e, W8, ra))
			return 0;
		if (!emit_imm32(e, W9, (uint32_t)simm))
			return 0;
		if (!emit_w(e, a64_mul_w(W8, W8, W9)))
			return 0;
		return emit_store_gpr(e, W8, rd);
	}
	if (prim == 31 && xo == 235) {
		if (!emit_load_gpr(e, W8, ra))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_mul_w(W8, W8, W9)))
			return 0;
		if (!emit_store_gpr(e, W8, rd))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 31 && xo == 747) {
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_imm32(e, W2, (uint32_t)ra))
			return 0;
		if (!emit_imm32(e, W3, (uint32_t)rb))
			return 0;
		if (!emit_imm32(e, W4, op & 1u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_mullwo))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return 1;
	}
	if (prim == 31 && (xo == 491 || xo == 1003)) {
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_imm32(e, W2, (uint32_t)ra))
			return 0;
		if (!emit_imm32(e, W3, (uint32_t)rb))
			return 0;
		if (!emit_imm32(e, W4, op & 1u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)(xo == 1003 ?
				nw_jit_helper_divwo : nw_jit_helper_divw)))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return 1;
	}
	if (prim == 31 && xo == 459) {
		if (!emit_load_gpr(e, W8, ra))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_udiv_w(W8, W8, W9)))
			return 0;
		if (!emit_store_gpr(e, W8, rd))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 31 && xo == 971) {
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_imm32(e, W2, (uint32_t)ra))
			return 0;
		if (!emit_imm32(e, W3, (uint32_t)rb))
			return 0;
		if (!emit_imm32(e, W4, op & 1u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_divwuo))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return 1;
	}
	if (prim == 31 && xo == 11) {
		if (!emit_load_gpr(e, W8, ra))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_umull_x(W8, W8, W9)))
			return 0;
		if (!emit_w(e, a64_lsr_x32(W8, W8)))
			return 0;
		if (!emit_store_gpr(e, W8, rd))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 31 && xo == 75) {
		if (!emit_load_gpr(e, W8, ra))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_smull_x(W8, W8, W9)))
			return 0;
		if (!emit_w(e, a64_lsr_x32(W8, W8)))
			return 0;
		if (!emit_store_gpr(e, W8, rd))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 12 || prim == 13) {
		if (!emit_load_gpr(e, W8, ra))
			return 0;
		if (!emit_imm32(e, W9, (uint32_t)simm))
			return 0;
		if (!emit_w(e, a64_adds_reg(W8, W8, W9)))
			return 0;
		if (!emit_xer_ca_from_cs(e))
			return 0;
		if (!emit_store_gpr(e, W8, rd))
			return 0;
		if (prim == 13)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 8) {
		if (!emit_load_gpr(e, W8, ra))
			return 0;
		if (!emit_imm32(e, W9, (uint32_t)simm))
			return 0;
		if (!emit_w(e, a64_subs_reg(W8, W9, W8)))	/* SIMM - rA */
			return 0;
		if (!emit_xer_ca_from_cs(e))
			return 0;
		return emit_store_gpr(e, W8, rd);
	}
	if (prim == 24) {
		if (!emit_load_gpr(e, W8, rd))
			return 0;
		if (!emit_imm32(e, W9, op & 0xffffu))
			return 0;
		if (!emit_w(e, a64_orr_reg(W8, W8, W9)))
			return 0;
		return emit_store_gpr(e, W8, ra);
	}
	if (prim == 25) {
		if (!emit_load_gpr(e, W8, rd))
			return 0;
		if (!emit_imm32(e, W9, (op & 0xffffu) << 16))
			return 0;
		if (!emit_w(e, a64_orr_reg(W8, W8, W9)))
			return 0;
		return emit_store_gpr(e, W8, ra);
	}
	if (prim == 26) {
		if (!emit_load_gpr(e, W8, rd))
			return 0;
		if (!emit_imm32(e, W9, op & 0xffffu))
			return 0;
		if (!emit_w(e, a64_eor_reg(W8, W8, W9)))
			return 0;
		return emit_store_gpr(e, W8, ra);
	}
	if (prim == 27) {
		if (!emit_load_gpr(e, W8, rd))
			return 0;
		if (!emit_imm32(e, W9, (op & 0xffffu) << 16))
			return 0;
		if (!emit_w(e, a64_eor_reg(W8, W8, W9)))
			return 0;
		return emit_store_gpr(e, W8, ra);
	}
	if (prim == 28) {
		if (!emit_load_gpr(e, W8, rd))
			return 0;
		if (!emit_imm32(e, W9, op & 0xffffu))
			return 0;
		if (!emit_w(e, a64_and_reg(W8, W8, W9)))
			return 0;
		if (!emit_store_gpr(e, W8, ra))
			return 0;
		return emit_cr0_from_w8(e);
	}
	if (prim == 29) {
		if (!emit_load_gpr(e, W8, rd))
			return 0;
		if (!emit_imm32(e, W9, (op & 0xffffu) << 16))
			return 0;
		if (!emit_w(e, a64_and_reg(W8, W8, W9)))
			return 0;
		if (!emit_store_gpr(e, W8, ra))
			return 0;
		return emit_cr0_from_w8(e);
	}
	if (prim == 10) {
		if (rd & 3)
			return 0;
		if (!emit_load_gpr(e, W8, ra))
			return 0;
		if (!emit_imm32(e, W9, op & 0xffffu))
			return 0;
		if (!emit_w(e, a64_cmp_w(W8, W9)))
			return 0;
		return emit_cr_field_from_flags(e, rd >> 2, 0x54000083u); /* B.CC +4 */
	}
	if (prim == 11) {
		if (rd & 3)
			return 0;
		if (!emit_load_gpr(e, W8, ra))
			return 0;
		if (!emit_imm32(e, W9, (uint32_t)simm))
			return 0;
		if (!emit_w(e, a64_cmp_w(W8, W9)))
			return 0;
		return emit_cr_field_from_flags(e, rd >> 2, 0x5400008bu); /* B.LT +4 */
	}
	if (prim == 16) {
		const int bo = rd, bi = ra;
		const int32_t disp = (int16_t)(op & 0xfffcu);
		const int aa = (int)((op >> 1) & 1);
		const int lk = (int)(op & 1);
		const int dec_ctr = ((bo & 0x04) == 0);
		if (!dec_ctr && !lk && !aa && bo_is_cr(bo)) {
			if (!emit_w(e, a64_ldr_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, cr))))
				return 0;
			if (!emit_w(e, a64_lsr(W8, W8, 31 - bi)))
				return 0;
			if (!emit_w(e, a64_and_imm1(W8, W8)))
				return 0;
			uint32_t *cb = e->p;
			if (!emit_w(e, a64_cbz(W8, 0)))
				return 0;
			if (!emit_set_pc(e, (uint32_t)(pc + disp)))
				return 0;
			if (!emit_ret(e))
				return 0;
			int32_t off = (int32_t)(e->p - cb);
			*cb = ((bo & ~1) == NW_PPC_BO_TRUE) ? a64_cbz(W8, off) : a64_cbnz(W8, off);
			if (is_last)
				return emit_set_pc(e, pc + 4);
			return 1;
		}
		/* CTR / LK / AA: same as kpx execute_branch; helper sets PC. */
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm32(e, W1, op))
			return 0;
		if (!emit_imm32(e, W2, pc))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_bc))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		return emit_ret(e);
	}
	if (prim == 18) {
		const int32_t disp = (((int32_t)(op << 6)) >> 6) & ~3;
		const uint32_t target = (op & 2) ? (uint32_t)disp : (uint32_t)(pc + disp);
		if (op & 1) {
			if (!emit_imm32(e, W8, pc + 4))
				return 0;
			if (!emit_w(e, a64_str_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, lr))))
				return 0;
		}
		if (!emit_set_pc(e, target))
			return 0;
		if ((op & 1) == 0)
			return emit_chain_epilogue(e, target);
		return emit_ret(e);
	}
	if (prim == 19 && xo == 0) {
		if (!emit_imm32(e, W1, (op >> 23) & 7u))
			return 0;
		if (!emit_imm32(e, W2, (op >> 18) & 7u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_mcrf))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return 1;
	}
	if (prim == 19 && xo == 33) {
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_w(e, a64_ldr_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, cr))))
			return 0;
		if (!emit_w(e, a64_lsr(W9, W8, 31 - ra)))
			return 0;
		if (!emit_w(e, a64_and_imm1(W9, W9)))
			return 0;
		if (!emit_w(e, a64_lsr(W10, W8, 31 - rb)))
			return 0;
		if (!emit_w(e, a64_and_imm1(W10, W10)))
			return 0;
		if (!emit_w(e, a64_orr_reg(W9, W9, W10)))
			return 0;
		if (!emit_w(e, a64_eor_imm1(W9, W9)))
			return 0;
		if (!emit_imm32(e, W10, 1u << (31 - rd)))
			return 0;
		if (!emit_w(e, a64_bic(W8, W8, W10)))
			return 0;
		if (!emit_w(e, a64_cmp_imm1(W9)))
			return 0;
		if (!emit_w(e, a64_csel(W12, W10, 31, 0)))	/* EQ → mask */
			return 0;
		if (!emit_w(e, a64_orr_reg(W8, W8, W12)))
			return 0;
		return emit_w(e, a64_str_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, cr)));
	}
	if (prim == 19 && xo == 193) {
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_w(e, a64_ldr_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, cr))))
			return 0;
		if (!emit_w(e, a64_lsr(W9, W8, 31 - ra)))
			return 0;
		if (!emit_w(e, a64_and_imm1(W9, W9)))
			return 0;
		if (!emit_w(e, a64_lsr(W10, W8, 31 - rb)))
			return 0;
		if (!emit_w(e, a64_and_imm1(W10, W10)))
			return 0;
		if (!emit_w(e, a64_eor_reg(W9, W9, W10)))
			return 0;
		if (!emit_imm32(e, W10, 1u << (31 - rd)))
			return 0;
		if (!emit_w(e, a64_bic(W8, W8, W10)))
			return 0;
		if (!emit_w(e, a64_cmp_imm1(W9)))
			return 0;
		if (!emit_w(e, a64_csel(W12, W10, 31, 0)))
			return 0;
		if (!emit_w(e, a64_orr_reg(W8, W8, W12)))
			return 0;
		return emit_w(e, a64_str_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, cr)));
	}
	if (prim == 19 && xo == 289) {
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_w(e, a64_ldr_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, cr))))
			return 0;
		if (!emit_w(e, a64_lsr(W9, W8, 31 - ra)))
			return 0;
		if (!emit_w(e, a64_and_imm1(W9, W9)))
			return 0;
		if (!emit_w(e, a64_lsr(W10, W8, 31 - rb)))
			return 0;
		if (!emit_w(e, a64_and_imm1(W10, W10)))
			return 0;
		if (!emit_w(e, a64_eor_reg(W9, W9, W10)))
			return 0;
		if (!emit_w(e, a64_eor_imm1(W9, W9)))
			return 0;
		if (!emit_imm32(e, W10, 1u << (31 - rd)))
			return 0;
		if (!emit_w(e, a64_bic(W8, W8, W10)))
			return 0;
		if (!emit_w(e, a64_cmp_imm1(W9)))
			return 0;
		if (!emit_w(e, a64_csel(W12, W10, 31, 0)))
			return 0;
		if (!emit_w(e, a64_orr_reg(W8, W8, W12)))
			return 0;
		return emit_w(e, a64_str_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, cr)));
	}
	if (prim == 19 && xo == 449) {
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_w(e, a64_ldr_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, cr))))
			return 0;
		if (!emit_w(e, a64_lsr(W9, W8, 31 - ra)))
			return 0;
		if (!emit_w(e, a64_and_imm1(W9, W9)))
			return 0;
		if (!emit_w(e, a64_lsr(W10, W8, 31 - rb)))
			return 0;
		if (!emit_w(e, a64_and_imm1(W10, W10)))
			return 0;
		if (!emit_w(e, a64_orr_reg(W9, W9, W10)))
			return 0;
		if (!emit_imm32(e, W10, 1u << (31 - rd)))
			return 0;
		if (!emit_w(e, a64_bic(W8, W8, W10)))
			return 0;
		if (!emit_w(e, a64_cmp_imm1(W9)))
			return 0;
		if (!emit_w(e, a64_csel(W12, W10, 31, 0)))
			return 0;
		if (!emit_w(e, a64_orr_reg(W8, W8, W12)))
			return 0;
		return emit_w(e, a64_str_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, cr)));
	}
	if (prim == 19 && xo == 417) {
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_w(e, a64_ldr_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, cr))))
			return 0;
		if (!emit_w(e, a64_lsr(W9, W8, 31 - ra)))
			return 0;
		if (!emit_w(e, a64_and_imm1(W9, W9)))
			return 0;
		if (!emit_w(e, a64_lsr(W10, W8, 31 - rb)))
			return 0;
		if (!emit_w(e, a64_and_imm1(W10, W10)))
			return 0;
		if (!emit_w(e, a64_eor_imm1(W10, W10)))
			return 0;
		if (!emit_w(e, a64_orr_reg(W9, W9, W10)))
			return 0;
		if (!emit_imm32(e, W10, 1u << (31 - rd)))
			return 0;
		if (!emit_w(e, a64_bic(W8, W8, W10)))
			return 0;
		if (!emit_w(e, a64_cmp_imm1(W9)))
			return 0;
		if (!emit_w(e, a64_csel(W12, W10, 31, 0)))
			return 0;
		if (!emit_w(e, a64_orr_reg(W8, W8, W12)))
			return 0;
		return emit_w(e, a64_str_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, cr)));
	}
	if (prim == 19 && xo == 257) {
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_w(e, a64_ldr_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, cr))))
			return 0;
		if (!emit_w(e, a64_lsr(W9, W8, 31 - ra)))
			return 0;
		if (!emit_w(e, a64_and_imm1(W9, W9)))
			return 0;
		if (!emit_w(e, a64_lsr(W10, W8, 31 - rb)))
			return 0;
		if (!emit_w(e, a64_and_imm1(W10, W10)))
			return 0;
		if (!emit_w(e, a64_and_reg(W9, W9, W10)))
			return 0;
		if (!emit_imm32(e, W10, 1u << (31 - rd)))
			return 0;
		if (!emit_w(e, a64_bic(W8, W8, W10)))
			return 0;
		if (!emit_w(e, a64_cmp_imm1(W9)))
			return 0;
		if (!emit_w(e, a64_csel(W12, W10, 31, 0)))
			return 0;
		if (!emit_w(e, a64_orr_reg(W8, W8, W12)))
			return 0;
		return emit_w(e, a64_str_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, cr)));
	}
	if (prim == 19 && xo == 129) {
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_w(e, a64_ldr_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, cr))))
			return 0;
		if (!emit_w(e, a64_lsr(W9, W8, 31 - ra)))
			return 0;
		if (!emit_w(e, a64_and_imm1(W9, W9)))
			return 0;
		if (!emit_w(e, a64_lsr(W10, W8, 31 - rb)))
			return 0;
		if (!emit_w(e, a64_and_imm1(W10, W10)))
			return 0;
		if (!emit_w(e, a64_eor_imm1(W10, W10)))
			return 0;
		if (!emit_w(e, a64_and_reg(W9, W9, W10)))
			return 0;
		if (!emit_imm32(e, W10, 1u << (31 - rd)))
			return 0;
		if (!emit_w(e, a64_bic(W8, W8, W10)))
			return 0;
		if (!emit_w(e, a64_cmp_imm1(W9)))
			return 0;
		if (!emit_w(e, a64_csel(W12, W10, 31, 0)))
			return 0;
		if (!emit_w(e, a64_orr_reg(W8, W8, W12)))
			return 0;
		return emit_w(e, a64_str_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, cr)));
	}
	if (prim == 19 && xo == 225) {
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_w(e, a64_ldr_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, cr))))
			return 0;
		if (!emit_w(e, a64_lsr(W9, W8, 31 - ra)))
			return 0;
		if (!emit_w(e, a64_and_imm1(W9, W9)))
			return 0;
		if (!emit_w(e, a64_lsr(W10, W8, 31 - rb)))
			return 0;
		if (!emit_w(e, a64_and_imm1(W10, W10)))
			return 0;
		if (!emit_w(e, a64_and_reg(W9, W9, W10)))
			return 0;
		if (!emit_w(e, a64_eor_imm1(W9, W9)))
			return 0;
		if (!emit_imm32(e, W10, 1u << (31 - rd)))
			return 0;
		if (!emit_w(e, a64_bic(W8, W8, W10)))
			return 0;
		if (!emit_w(e, a64_cmp_imm1(W9)))
			return 0;
		if (!emit_w(e, a64_csel(W12, W10, 31, 0)))
			return 0;
		if (!emit_w(e, a64_orr_reg(W8, W8, W12)))
			return 0;
		return emit_w(e, a64_str_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, cr)));
	}
	if (prim == 19 && (xo == 16 || xo == 528)) {
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm32(e, W1, op))
			return 0;
		if (!emit_imm32(e, W2, pc))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_bclr))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (is_last)
			return emit_ret(e);
		if (!emit_w(e, a64_ldr_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, pc))))
			return 0;
		if (!emit_imm32(e, W9, pc + 4))
			return 0;
		if (!emit_w(e, a64_cmp_w(W8, W9)))
			return 0;
		uint32_t *cb = e->p;
		if (!emit_w(e, a64_b_cond(0, 0)))	/* EQ: not taken, skip ret */
			return 0;
		if (!emit_ret(e))
			return 0;
		*cb = a64_b_cond(0, (int)(e->p - cb));
		return 1;
	}
	if (prim == 20) {
		const int sh = rb, mb = (int)((op >> 6) & 0x1f), me = (int)((op >> 1) & 0x1f);
		if (!emit_load_gpr(e, W8, rd))
			return 0;
		if (sh)
			if (!emit_w(e, a64_extr(W8, W8, W8, 32 - sh)))
				return 0;
		if (!emit_imm32(e, W9, ppc_mask((uint32_t)mb, (uint32_t)me)))
			return 0;
		if (!emit_w(e, a64_and_reg(W8, W8, W9)))
			return 0;
		if (!emit_load_gpr(e, W10, ra))
			return 0;
		if (!emit_w(e, a64_bic(W10, W10, W9)))
			return 0;
		if (!emit_w(e, a64_orr_reg(W8, W8, W10)))
			return 0;
		if (!emit_store_gpr(e, W8, ra))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 21) {
		const int sh = rb, mb = (int)((op >> 6) & 0x1f), me = (int)((op >> 1) & 0x1f);
		if (!emit_load_gpr(e, W8, rd))
			return 0;
		if (sh)
			if (!emit_w(e, a64_extr(W8, W8, W8, 32 - sh)))
				return 0;
		if (!emit_imm32(e, W9, ppc_mask((uint32_t)mb, (uint32_t)me)))
			return 0;
		if (!emit_w(e, a64_and_reg(W8, W8, W9)))
			return 0;
		if (!emit_store_gpr(e, W8, ra))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 23) {
		const int mb = (int)((op >> 6) & 0x1f), me = (int)((op >> 1) & 0x1f);
		if (!emit_load_gpr(e, W8, rd))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_sub_reg(W9, 31, W9)))	/* -n → ror amount */
			return 0;
		if (!emit_w(e, a64_rorv(W8, W8, W9)))
			return 0;
		if (!emit_imm32(e, W9, ppc_mask((uint32_t)mb, (uint32_t)me)))
			return 0;
		if (!emit_w(e, a64_and_reg(W8, W8, W9)))
			return 0;
		if (!emit_store_gpr(e, W8, ra))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 31 && (xo == 266 || xo == 778)) {
		if (!emit_load_gpr(e, W8, ra))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (xo == 778) {
			if (!emit_w(e, a64_adds_reg(W8, W8, W9)))
				return 0;
			if (!emit_xer_ov_from_vs(e))
				return 0;
		} else if (!emit_w(e, a64_add_reg(W8, W8, W9)))
			return 0;
		if (!emit_store_gpr(e, W8, rd))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 31 && (xo == 40 || xo == 552)) {
		if (!emit_load_gpr(e, W8, ra))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (xo == 552) {
			if (!emit_w(e, a64_subs_reg(W8, W9, W8)))	/* rB - rA */
				return 0;
			if (!emit_xer_ov_from_vs(e))
				return 0;
		} else if (!emit_w(e, a64_sub_reg(W8, W9, W8)))
			return 0;
		if (!emit_store_gpr(e, W8, rd))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 31 && (xo == 10 || xo == 522)) {
		if (!emit_load_gpr(e, W8, ra))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_adds_reg(W8, W8, W9)))
			return 0;
		if (!emit_xer_ca_from_cs(e))
			return 0;
		if (xo == 522 && !emit_xer_ov_from_vs(e))
			return 0;
		if (!emit_store_gpr(e, W8, rd))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 31 && (xo == 138 || xo == 650)) {
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_imm32(e, W2, (uint32_t)ra))
			return 0;
		if (!emit_imm32(e, W3, (uint32_t)rb))
			return 0;
		if (!emit_imm32(e, W4, op & 1u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)(xo == 650 ? nw_jit_helper_addeo : nw_jit_helper_adde)))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return 1;
	}
	if (prim == 31 && xo == 520) {
		if (!emit_load_gpr(e, W8, ra))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_subs_reg(W8, W9, W8)))	/* rB - rA */
			return 0;
		if (!emit_xer_ca_from_cs(e))
			return 0;
		if (!emit_xer_ov_from_vs(e))
			return 0;
		if (!emit_store_gpr(e, W8, rd))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 31 && xo == 8) {
		if (!emit_load_gpr(e, W8, ra))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_subs_reg(W8, W9, W8)))	/* rB - rA */
			return 0;
		if (!emit_xer_ca_from_cs(e))
			return 0;
		if (!emit_store_gpr(e, W8, rd))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 31 && (xo == 136 || xo == 648)) {
		if (!emit_load_gpr(e, W8, ra))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_ldr_w(W10, X0, (uint32_t)offsetof(struct nw_jit_cpu, xer))))
			return 0;
		if (!emit_w(e, a64_lsr(W10, W10, 29)))
			return 0;
		if (!emit_w(e, a64_and_imm1(W10, W10)))
			return 0;
		if (!emit_w(e, a64_cmp_imm1(W10)))	/* C = XER[CA] */
			return 0;
		if (!emit_w(e, a64_sbcs_reg(W8, W9, W8)))	/* rB + ~rA + CA */
			return 0;
		if (!emit_xer_ca_from_cs(e))
			return 0;
		if (xo == 648 && !emit_xer_ov_from_vs(e))
			return 0;
		if (!emit_store_gpr(e, W8, rd))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 31 && xo == 922) {
		if (!emit_load_gpr(e, W8, rd))
			return 0;
		if (!emit_w(e, a64_sxth(W8, W8)))
			return 0;
		if (!emit_store_gpr(e, W8, ra))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 31 && xo == 954) {
		if (!emit_load_gpr(e, W8, rd))
			return 0;
		if (!emit_w(e, a64_sxtb(W8, W8)))
			return 0;
		if (!emit_store_gpr(e, W8, ra))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 31 && xo == 24) {
		if (!emit_load_gpr(e, W8, rd))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_lsr(W10, W9, 5)))
			return 0;
		if (!emit_w(e, a64_and_imm1(W10, W10)))
			return 0;
		if (!emit_w(e, a64_lslv(W8, W8, W9)))
			return 0;
		if (!emit_w(e, a64_cmp_imm1(W10)))
			return 0;
		if (!emit_w(e, a64_csel(W8, 31, W8, 0)))	/* EQ → 0 if rB bit5 */
			return 0;
		if (!emit_store_gpr(e, W8, ra))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 31 && xo == 536) {
		if (!emit_load_gpr(e, W8, rd))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_lsr(W10, W9, 5)))
			return 0;
		if (!emit_w(e, a64_and_imm1(W10, W10)))
			return 0;
		if (!emit_w(e, a64_lsrv(W8, W8, W9)))
			return 0;
		if (!emit_w(e, a64_cmp_imm1(W10)))
			return 0;
		if (!emit_w(e, a64_csel(W8, 31, W8, 0)))
			return 0;
		if (!emit_store_gpr(e, W8, ra))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 31 && xo == 792) {
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_load_gpr(e, W1, rd))
			return 0;
		if (!emit_load_gpr(e, W2, rb))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_sraw))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, a64_orr_reg(W8, 31, W0)))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_store_gpr(e, W8, ra))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 31 && xo == 824) {
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_load_gpr(e, W1, rd))
			return 0;
		if (!emit_imm32(e, W2, (uint32_t)rb))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_sraw))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, a64_orr_reg(W8, 31, W0)))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_store_gpr(e, W8, ra))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 19 && xo == 50) {
		/* Translation-context barrier: host rfi, no pc+4, leave. */
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_rfi))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_ret(e);
	}
	if (prim == 19 && xo == 150) {
		/* Same as kpx execute_isync: host flushes pending icbi range
		 * (NW JIT pages included), then ISB; leave the block. */
		if (!emit_w(e, 0xaa1303e0u))		/* mov x0, x19 */
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_isync))
			return 0;
		if (!emit_w(e, 0xd63f0120u))		/* blr x9 */
			return 0;
		if (!emit_w(e, 0xaa1303e0u))		/* mov x0, x19 */
			return 0;
		if (!emit_w(e, 0xd5033fdfu))		/* ISB */
			return 0;
		if (!emit_set_pc(e, pc + 4))
			return 0;
		return emit_ret(e);
	}
	if (prim == 31 && xo == 598)
		return emit_w(e, 0xd5033f9fu);	/* DMB SY */
	if (prim == 31 && xo == 566)
		return emit_w(e, 0xd5033f9fu);	/* DMB SY; tlbsync */
	if (prim == 31 && xo == 822)
		return emit_w(e, 0xd503201fu);	/* NOP; kpx dss is a no-op */
	if (prim == 31 && (xo == 342 || xo == 374))
		return emit_w(e, 0xd503201fu);	/* NOP; kpx dst/dstst */
	if (prim == 31 && (xo == 278 || xo == 246 || xo == 86 || xo == 54 || xo == 470 || xo == 758))
		return emit_w(e, 0xd503201fu);	/* NOP; kpx dcbt/dcbtst/dcbf/dcbst/dcbi/dcba */
	if (prim == 31 && xo == 854)
		return emit_w(e, 0xd5033f9fu);	/* DMB SY; eieio */
	if (prim == 31 && xo == 1014) {
		if (!emit_set_pc(e, pc))
			return 0;
		if (!emit_ra_or_0(e, W8, ra))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_add_reg(W1, W8, W9)))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_dcbz))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_fault_check(e);
	}
	if (prim == 31 && xo == 210) {
		/* SR change can remap this PC; end the block like mtmsr. */
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm32(e, W1, (uint32_t)ra & 0xfu))
			return 0;
		if (!emit_load_gpr(e, W2, rd))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_mtsr))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_set_pc(e, pc + 4))
			return 0;
		return emit_ret(e);
	}
	if (prim == 31 && xo == 242) {
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_load_gpr(e, W1, rd))
			return 0;
		if (!emit_load_gpr(e, W2, rb))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_mtsrin))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_set_pc(e, pc + 4))
			return 0;
		return emit_ret(e);
	}
	if (prim == 31 && xo == 659) {
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_load_gpr(e, W2, rb))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_mfsrin))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return 1;
	}
	if (prim == 31 && xo == 595) {
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_imm32(e, W2, (uint32_t)ra))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_mfsr))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return 1;
	}
	if (prim == 31 && xo == 4) {
		if (!emit_set_pc(e, pc))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_load_gpr(e, W2, ra))
			return 0;
		if (!emit_load_gpr(e, W3, rb))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_tw))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_fault_check(e);
	}
	if (prim == 31 && xo == 370) {
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_tlbia))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_set_pc(e, pc + 4))
			return 0;
		return emit_ret(e);
	}
	if (prim == 3) {
		if (!emit_set_pc(e, pc))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_load_gpr(e, W2, ra))
			return 0;
		if (!emit_imm32(e, W3, (uint32_t)(uint16_t)simm))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_twi))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_fault_check(e);
	}
	if (prim == 17) {
		if (!emit_set_pc(e, pc))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_sc))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_fault_check(e);
	}
	if (prim == 31 && xo == 146) {
		/* Same as kpx execute_mtmsr: set_msr(rS), flush DTLB if
		 * IR|DR changes, leave the block. rfi still uses no-op
		 * flush_if_pr. */
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_load_gpr(e, W1, rd))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_mtmsr))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_set_pc(e, pc + 4))
			return 0;
		return emit_ret(e);
	}
	if (prim == 31 && xo == 19) {
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_w(e, a64_ldr_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, cr))))
			return 0;
		return emit_store_gpr(e, W8, rd);
	}
	if (prim == 31 && xo == 83) {
		if (!emit_w(e, a64_ldr_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, msr))))
			return 0;
		return emit_store_gpr(e, W8, rd);
	}
	if (prim == 31 && xo == 371) {
		if (!emit_set_pc(e, pc))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_imm32(e, W2, spr_num(op)))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_mfspr))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_fault_check(e);
	}
	if (prim == 31 && xo == 144) {
		const uint32_t m = mtcrf_mask(op);
		if (!emit_load_gpr(e, W8, rd))
			return 0;
		if (!emit_imm32(e, W9, m))
			return 0;
		if (!emit_w(e, a64_and_reg(W8, W8, W9)))
			return 0;
		if (!emit_w(e, a64_ldr_w(W10, X0, (uint32_t)offsetof(struct nw_jit_cpu, cr))))
			return 0;
		if (!emit_w(e, a64_bic(W10, W10, W9)))
			return 0;
		if (!emit_w(e, a64_orr_reg(W10, W10, W8)))
			return 0;
		return emit_w(e, a64_str_w(W10, X0, (uint32_t)offsetof(struct nw_jit_cpu, cr)));
	}
	if (prim == 31 && xo == 444) {
		if (!emit_load_gpr(e, W8, rd))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_orr_reg(W8, W8, W9)))
			return 0;
		if (!emit_store_gpr(e, W8, ra))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 31 && xo == 316) {
		if (!emit_load_gpr(e, W8, rd))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_eor_reg(W8, W8, W9)))
			return 0;
		if (!emit_store_gpr(e, W8, ra))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 31 && xo == 284) {
		if (!emit_load_gpr(e, W8, rd))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_eor_reg(W8, W8, W9)))
			return 0;
		if (!emit_w(e, 0x2a2803e8u))	/* ORN W8, WZR, W8 */
			return 0;
		if (!emit_store_gpr(e, W8, ra))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 31 && xo == 476) {
		if (!emit_load_gpr(e, W8, rd))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_and_reg(W8, W8, W9)))
			return 0;
		if (!emit_w(e, 0x2a2803e8u))
			return 0;
		if (!emit_store_gpr(e, W8, ra))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 31 && xo == 124) {
		if (!emit_load_gpr(e, W8, rd))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_orr_reg(W8, W8, W9)))
			return 0;
		if (!emit_w(e, 0x2a2803e8u))
			return 0;
		if (!emit_store_gpr(e, W8, ra))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 31 && xo == 28) {
		if (!emit_load_gpr(e, W8, rd))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_and_reg(W8, W8, W9)))
			return 0;
		if (!emit_store_gpr(e, W8, ra))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 31 && xo == 60) {
		if (!emit_load_gpr(e, W8, rd))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_bic(W8, W8, W9)))
			return 0;
		if (!emit_store_gpr(e, W8, ra))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 31 && xo == 412) {
		if (!emit_imm32(e, W1, (uint32_t)ra))
			return 0;
		if (!emit_imm32(e, W2, (uint32_t)rd))
			return 0;
		if (!emit_imm32(e, W3, (uint32_t)rb))
			return 0;
		if (!emit_imm32(e, W4, op & 1u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_orc))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return 1;
	}
	if (prim == 31 && xo == 512) {
		if (!emit_imm32(e, W1, (op >> 23) & 7u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_mcrxr))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return 1;
	}
	if (prim == 31 && xo == 26) {
		if (!emit_load_gpr(e, W8, rd))
			return 0;
		if (!emit_w(e, a64_clz(W8, W8)))
			return 0;
		if (!emit_store_gpr(e, W8, ra))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 31 && xo == 104) {
		if (!emit_load_gpr(e, W8, ra))
			return 0;
		if (!emit_w(e, a64_sub_reg(W8, 31, W8)))	/* 0 - rA */
			return 0;
		if (!emit_store_gpr(e, W8, rd))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 31 && xo == 0) {
		if (rd & 3)
			return 0;
		if (!emit_load_gpr(e, W8, ra))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_cmp_w(W8, W9)))
			return 0;
		return emit_cr_field_from_flags(e, rd >> 2, 0x5400008bu); /* B.LT +4 */
	}
	if (prim == 31 && xo == 32) {
		if (rd & 3)
			return 0;
		if (!emit_load_gpr(e, W8, ra))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_cmp_w(W8, W9)))
			return 0;
		return emit_cr_field_from_flags(e, rd >> 2, 0x54000083u); /* B.CC +4 */
	}
	if (prim == 31 && (xo == 339 || xo == 467)) {
		const uint32_t spr = spr_num(op);
		uint32_t off;
		if (spr == NW_PPC_SPR_DEC)
			off = (uint32_t)offsetof(struct nw_jit_cpu, dec);
		else if (spr == NW_PPC_SPR_LR)
			off = (uint32_t)offsetof(struct nw_jit_cpu, lr);
		else if (spr == NW_PPC_SPR_CTR)
			off = (uint32_t)offsetof(struct nw_jit_cpu, ctr);
		else if (spr == NW_PPC_SPR_XER)
			off = (uint32_t)offsetof(struct nw_jit_cpu, xer);
		else if (xo == 339) {
			if (!emit_set_pc(e, pc))
				return 0;
			if (!emit_w(e, 0xaa1303e0u))
				return 0;
			if (!emit_imm32(e, W1, (uint32_t)rd))
				return 0;
			if (!emit_imm32(e, W2, spr))
				return 0;
			if (!emit_w(e, 0xaa1303e0u))
				return 0;
			if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_mfspr))
				return 0;
			if (!emit_w(e, 0xd63f0120u))
				return 0;
			if (!emit_w(e, 0xaa1303e0u))
				return 0;
			return emit_fault_check(e);
		} else if (xo == 467) {
			if (!emit_w(e, 0xaa1303e0u))
				return 0;
			if (!emit_imm32(e, W1, spr))
				return 0;
			if (!emit_load_gpr(e, W2, rd))
				return 0;
			if (!emit_w(e, 0xaa1303e0u))
				return 0;
			if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_mtspr))
				return 0;
			if (!emit_w(e, 0xd63f0120u))
				return 0;
			if (!emit_w(e, 0xaa1303e0u))
				return 0;
			if (!emit_set_pc(e, pc + 4))
				return 0;
			return emit_ret(e);
		} else
			return 0;
		if (xo == 339) {
			if (!emit_w(e, a64_ldr_w(W8, X0, off)))
				return 0;
			return emit_store_gpr(e, W8, rd);
		}
		if (!emit_load_gpr(e, W8, rd))
			return 0;
		if (!emit_w(e, a64_str_w(W8, X0, off)))
			return 0;
		if (spr == NW_PPC_SPR_DEC) {
			if (!emit_imm32(e, W8, 1))
				return 0;
			if (!emit_w(e, a64_str_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, dec_wr))))
				return 0;
		}
		return 1;
	}
	if (prim == 32 || prim == 33) {
		return emit_call_lwz(e, pc, rd, ra, simm, prim == 33);
	}
	if (prim == 34 || prim == 35) {
		return emit_call_lb(e, pc, rd, ra, simm, prim == 35);
	}
	if (prim == 31 && xo == 119) {
		if (!ra)
			return 0;
		return emit_call_lbzux(e, pc, rd, ra, rb);
	}
	if (prim == 31 && xo == 87) {
		return emit_call_lbx(e, pc, rd, ra, rb, 0);
	}
	if (prim == 50) {
		if (!emit_set_pc(e, pc))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_imm32(e, W2, (uint32_t)ra))
			return 0;
		if (!emit_imm32(e, W3, (uint32_t)simm))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_lfd))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_fault_check(e);
	}
	if (prim == 54) {
		if (!emit_set_pc(e, pc))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_imm32(e, W2, (uint32_t)ra))
			return 0;
		if (!emit_imm32(e, W3, (uint32_t)simm))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_stfd))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_fault_check(e);
	}
	if (prim == 48 || prim == 52) {
		if (!emit_set_pc(e, pc))
			return 0;
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_ra_or_0(e, W8, ra))
			return 0;
		if (!emit_imm32(e, W9, (uint32_t)simm))
			return 0;
		if (!emit_w(e, a64_add_reg(W2, W8, W9)))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)(prim == 48 ? nw_jit_helper_lfs : nw_jit_helper_stfs)))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_fault_check(e);
	}
	if (prim == 49) {
		if (!ra)
			return 0;
		return emit_call_fp_d_upd(e, pc, rd, ra, simm, 0);
	}
	if (prim == 51) {
		if (!ra)
			return 0;
		return emit_call_fp_d_upd(e, pc, rd, ra, simm, 1);
	}
	if (prim == 55) {
		if (!ra)
			return 0;
		return emit_call_fp_d_upd(e, pc, rd, ra, simm, 2);
	}
	if (prim == 53) {
		if (!ra)
			return 0;
		return emit_call_fp_d_upd(e, pc, rd, ra, simm, 3);
	}
	if (prim == 31 && (xo == 535 || xo == 663)) {
		if (!emit_set_pc(e, pc))
			return 0;
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_ra_or_0(e, W8, ra))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_add_reg(W2, W8, W9)))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)(xo == 535 ? nw_jit_helper_lfs : nw_jit_helper_stfs)))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_fault_check(e);
	}
	if (prim == 31 && (xo == 599 || xo == 727)) {
		if (!emit_set_pc(e, pc))
			return 0;
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_imm32(e, W2, 0))
			return 0;
		if (!emit_ra_or_0(e, W8, ra))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_add_reg(W3, W8, W9)))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)(xo == 599 ? nw_jit_helper_lfd : nw_jit_helper_stfd)))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_fault_check(e);
	}
	if (prim == 31 && (xo == 567 || xo == 631 || xo == 695 || xo == 759)) {
		const int kind = (xo == 567) ? 0 : (xo == 631) ? 1 : (xo == 695) ? 2 : 3;
		return emit_call_fp_x_upd(e, pc, rd, ra, rb, kind);
	}
	if (prim == 59) {
		const int axo = (int)((op >> 1) & 0x1f);
		const int fc = (int)((op >> 6) & 0x1f);
		if (axo == 18 || axo == 20 || axo == 21 || axo == 25 ||
		    axo == 28 || axo == 29 || axo == 30 || axo == 31)
			return emit_fp_inline(e, axo, rd, ra, rb, fc, op);
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (axo == 24) {
			if (!emit_imm32(e, W2, (uint32_t)rb))
				return 0;
		} else {
			if (!emit_imm32(e, W2, (uint32_t)ra))
				return 0;
			if (axo == 25) {
				if (!emit_imm32(e, W3, (uint32_t)fc))
					return 0;
			} else if (axo == 28 || axo == 29 || axo == 30 || axo == 31) {
				if (!emit_imm32(e, W3, (uint32_t)fc))
					return 0;
				if (!emit_imm32(e, W4, (uint32_t)rb))
					return 0;
			} else if (axo == 18 || axo == 20 || axo == 21) {
				if (!emit_imm32(e, W3, (uint32_t)rb))
					return 0;
			} else if (axo != 24)
				return 0;
		}
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		{
			void *fn = NULL;
			if (axo == 18)
				fn = (void *)nw_jit_helper_fdivs;
			else if (axo == 20)
				fn = (void *)nw_jit_helper_fsubs;
			else if (axo == 21)
				fn = (void *)nw_jit_helper_fadds;
			else if (axo == 24)
				fn = (void *)nw_jit_helper_fres;
			else if (axo == 25)
				fn = (void *)nw_jit_helper_fmuls;
			else if (axo == 28)
				fn = (void *)nw_jit_helper_fmsubs;
			else if (axo == 30)
				fn = (void *)nw_jit_helper_fnmsubs;
			else if (axo == 31)
				fn = (void *)nw_jit_helper_fnmadds;
			else
				fn = (void *)nw_jit_helper_fmadds;
			if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)fn))
				return 0;
		}
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_maybe_cr1(e, op);
	}
	if (prim == 63 && (xo == 32 || xo == 0))
		return emit_fp_inline(e, 104, rd, ra, rb, 0, op);
	if (prim == 63 && xo == 711) {
		if (!emit_imm32(e, W1, (op >> 17) & 0xffu))
			return 0;
		if (!emit_imm32(e, W2, (uint32_t)rb))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_mtfsf))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_maybe_cr1(e, op);
	}
	if (prim == 63 && xo == 264)
		return emit_fp_inline(e, 101, rd, ra, rb, 0, op);
	if (prim == 63 && xo == 40)
		return emit_fp_inline(e, 102, rd, ra, rb, 0, op);
	if (prim == 63 && xo == 72)
		return emit_fp_inline(e, 103, rd, ra, rb, 0, op);
	if (prim == 63 && xo == 12)
		return emit_fp_inline(e, 100, rd, ra, rb, 0, op);
	if (prim == 63 && xo == 583) {
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_mffs))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_maybe_cr1(e, op);
	}
	if (prim == 63 && xo == 15) {
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_imm32(e, W2, (uint32_t)rb))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_fctiwz))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_maybe_cr1(e, op);
	}
	if (prim == 63 && xo == 14) {
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_imm32(e, W2, (uint32_t)rb))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_fctiw))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_maybe_cr1(e, op);
	}
	if (prim == 63 && xo == 136) {
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_imm32(e, W2, (uint32_t)rb))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_fnabs))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_maybe_cr1(e, op);
	}
	if (prim == 63 && xo == 70) {
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_imm32(e, W2, 0))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_mtfsb))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_maybe_cr1(e, op);
	}
	if (prim == 63 && xo == 38) {
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_imm32(e, W2, 1))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_mtfsb))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_maybe_cr1(e, op);
	}
	if (prim == 63 && xo == 134) {
		if (!emit_imm32(e, W1, (op >> 23) & 7u))
			return 0;
		if (!emit_imm32(e, W2, (op >> 12) & 0xfu))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_mtfsfi))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_maybe_cr1(e, op);
	}
	if (prim == 63 && xo == 64) {
		if (!emit_imm32(e, W1, (op >> 23) & 7u))
			return 0;
		if (!emit_imm32(e, W2, (op >> 18) & 7u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_mcrfs))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return 1;
	}
	if (prim == 63) {
		const int axo = (int)((op >> 1) & 0x1f);
		const int fc = (int)((op >> 6) & 0x1f);
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (axo == 26) {
			if (!emit_imm32(e, W2, (uint32_t)rb))
				return 0;
		} else {
			if (!emit_imm32(e, W2, (uint32_t)ra))
				return 0;
			if (axo == 23) {
				if (!emit_imm32(e, W3, (uint32_t)fc))
					return 0;
				if (!emit_imm32(e, W4, (uint32_t)rb))
					return 0;
			} else if (axo == 25) {
				if (!emit_imm32(e, W3, (uint32_t)fc))
					return 0;
			} else if (axo == 28 || axo == 29 || axo == 30 || axo == 31) {
				if (!emit_imm32(e, W3, (uint32_t)fc))
					return 0;
				if (!emit_imm32(e, W4, (uint32_t)rb))
					return 0;
			} else if (axo == 18 || axo == 20 || axo == 21) {
				if (!emit_imm32(e, W3, (uint32_t)rb))
					return 0;
			} else
				return 0;
		}
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		{
			void *fn = NULL;
			if (axo == 20)
				fn = (void *)nw_jit_helper_fsub;
			else if (axo == 21)
				fn = (void *)nw_jit_helper_fadd;
			else if (axo == 18)
				fn = (void *)nw_jit_helper_fdiv;
			else if (axo == 23)
				fn = (void *)nw_jit_helper_fsel;
			else if (axo == 26)
				fn = (void *)nw_jit_helper_frsqrte;
			else if (axo == 28)
				fn = (void *)nw_jit_helper_fmsub;
			else if (axo == 29)
				fn = (void *)nw_jit_helper_fmadd;
			else if (axo == 30)
				fn = (void *)nw_jit_helper_fnmsub;
			else if (axo == 31)
				fn = (void *)nw_jit_helper_fnmadd;
			else
				fn = (void *)nw_jit_helper_fmul;
			if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)fn))
				return 0;
		}
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_maybe_cr1(e, op);
	}
	if (prim == 31 && (xo == 6 || xo == 38)) {
		if (!emit_ra_or_0(e, W8, ra))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_add_reg(W2, W8, W9)))
			return 0;
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_imm32(e, W3, xo == 6 ? 1u : 0u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_lvsl))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return 1;
	}
	if (prim == 4) {
		const int vxo = (int)(op & 0x7ff);
		const int vaxo = (int)(op & 0x3f);
		const int vc = (int)((op >> 6) & 0x1f);
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_imm32(e, W2, (uint32_t)ra))
			return 0;
		if (!emit_imm32(e, W3, (uint32_t)rb))
			return 0;
		if (vaxo == 42 || vaxo == 43 || vaxo == 40 || vaxo == 41 || vaxo == 44 ||
		    vaxo == 34) {
			if (!emit_imm32(e, W4, (uint32_t)vc))
				return 0;
			if (vaxo == 40 || vaxo == 41) {
				if (!emit_imm32(e, W5, vaxo == 41 ? 1u : 0u))
					return 0;
			}
		} else if (vxo == 134 || vxo == 1158 || vxo == 6 || vxo == 1030) {
			if (!emit_imm32(e, W4, (vxo >> 10) & 1u))
				return 0;
		}
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		{
			void *fn = NULL;
			if (vxo == 1156)
				fn = (void *)nw_jit_helper_vor;
			else if (vxo == 0)
				fn = (void *)nw_jit_helper_vaddubm;
			else if (vxo == 1028)
				fn = (void *)nw_jit_helper_vand;
			else if (vxo == 1092)
				fn = (void *)nw_jit_helper_vandc;
			else if (vxo == 1220)
				fn = (void *)nw_jit_helper_vxor;
			else if (vxo == 1024)
				fn = (void *)nw_jit_helper_vsububm;
			else if (vxo == 324)
				fn = (void *)nw_jit_helper_vslh;
			else if (vxo == 134 || vxo == 1158)
				fn = (void *)nw_jit_helper_vcmpequw;
			else if (vxo == 6 || vxo == 1030)
				fn = (void *)nw_jit_helper_vcmpequb;
			else if (vxo == 770)
				fn = (void *)nw_jit_helper_vminsb;
			else if (vxo == 708)
				fn = (void *)nw_jit_helper_vsr;
			else if (vxo == 644)
				fn = (void *)nw_jit_helper_vsrw;
			else if (vxo == 908)
				fn = (void *)nw_jit_helper_vspltisw;
			else if (vxo == 452)
				fn = (void *)nw_jit_helper_vsl;
			else if (vxo == 1036)
				fn = (void *)nw_jit_helper_vslo;
			else if (vxo == 1100)
				fn = (void *)nw_jit_helper_vsro;
			else if (vxo == 780)
				fn = (void *)nw_jit_helper_vspltisb;
			else if (vxo == 1604)
				fn = (void *)nw_jit_helper_mtvscr;
			else if (vxo == 1540)
				fn = (void *)nw_jit_helper_mfvscr;
			else if (vxo == 516)
				fn = (void *)nw_jit_helper_vsrb;
			else if (vxo == 260)
				fn = (void *)nw_jit_helper_vslb;
			else if (vxo == 844)
				fn = (void *)nw_jit_helper_vspltish;
			else if (vxo == 652)
				fn = (void *)nw_jit_helper_vspltw;
			else if (vxo == 524)
				fn = (void *)nw_jit_helper_vspltb;
			else if (vxo == 12)
				fn = (void *)nw_jit_helper_vmrghb;
			else if (vxo == 268)
				fn = (void *)nw_jit_helper_vmrglb;
			else if (vxo == 140)
				fn = (void *)nw_jit_helper_vmrghw;
			else if (vxo == 396)
				fn = (void *)nw_jit_helper_vmrglw;
			else if (vxo == 1928)
				fn = (void *)nw_jit_helper_vsumsws;
			else if (vaxo == 42)
				fn = (void *)nw_jit_helper_vsel;
			else if (vaxo == 43)
				fn = (void *)nw_jit_helper_vperm;
			else if (vaxo == 40 || vaxo == 41)
				fn = (void *)nw_jit_helper_vmsumshm;
			else if (vaxo == 44)
				fn = (void *)nw_jit_helper_vsldoi;
			else if (vaxo == 34)
				fn = (void *)nw_jit_helper_vmladduhm;
			else if (vxo == 128)
				fn = (void *)nw_jit_helper_vadduwm;
			else if (vxo == 900)
				fn = (void *)nw_jit_helper_vsraw;
			else if (vxo == 462)
				fn = (void *)nw_jit_helper_vpkswss;
			else if (vxo == 1856)
				fn = (void *)nw_jit_helper_vsubshs;
			if (!fn) {
				if (!emit_imm32(e, W1, op))
					return 0;
				if (!emit_w(e, 0xaa1303e0u))
					return 0;
				if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_vmx))
					return 0;
			} else if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)fn))
				return 0;
		}
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return 1;
	}
	if (prim == 31 && (xo == 103 || xo == 359)) {
		if (!emit_set_pc(e, pc))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_imm32(e, W2, (uint32_t)ra))
			return 0;
		if (!emit_imm32(e, W3, (uint32_t)rb))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_lvx))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_fault_check(e);
	}
	if (prim == 31 && (xo == 231 || xo == 487)) {
		if (!emit_set_pc(e, pc))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_imm32(e, W2, (uint32_t)ra))
			return 0;
		if (!emit_imm32(e, W3, (uint32_t)rb))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_stvx))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_fault_check(e);
	}
	if (prim == 38 || prim == 39) {
		return emit_call_stb(e, pc, rd, ra, simm, prim == 39);
	}
	if (prim == 31 && xo == 215) {
		return emit_call_stbx(e, pc, rd, ra, rb, 0);
	}
	if (prim == 31 && xo == 247) {
		if (!ra)
			return 0;
		return emit_call_stbx(e, pc, rd, ra, rb, 1);
	}
	if (prim == 36 || prim == 37) {
		return emit_call_stw(e, pc, rd, ra, simm, prim == 37);
	}
	if (prim == 31 && xo == 23) {
		return emit_call_lwzx(e, pc, rd, ra, rb, 0);
	}
	if (prim == 31 && xo == 55) {
		if (!ra)
			return 0;
		return emit_call_lwzux(e, pc, rd, ra, rb);
	}
	if (prim == 31 && xo == 151) {
		return emit_call_stwx(e, pc, rd, ra, rb, 0);
	}
	if (prim == 31 && xo == 183) {
		if (!ra)
			return 0;
		return emit_call_stwx(e, pc, rd, ra, rb, 1);
	}
	if (prim == 31 && xo == 20) {
		if (!emit_set_pc(e, pc))
			return 0;
		if (!emit_ra_or_0(e, W8, ra))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_add_reg(W2, W8, W9)))
			return 0;
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_lwarx))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_fault_check(e);
	}
	if (prim == 31 && xo == 150) {
		if (!emit_set_pc(e, pc))
			return 0;
		if (!emit_ra_or_0(e, W8, ra))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_add_reg(W2, W8, W9)))
			return 0;
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_stwcx))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_fault_check(e);
	}
	if (prim == 31 && (xo == 533 || xo == 661)) {
		if (!emit_set_pc(e, pc))
			return 0;
		if (!emit_ra_or_0(e, W8, ra))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_add_reg(W2, W8, W9)))
			return 0;
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_w(e, a64_ldr_w(W3, X19, (uint32_t)offsetof(struct nw_jit_cpu, xer))))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)(xo == 533 ? nw_jit_helper_lswx : nw_jit_helper_stswx)))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_fault_check(e);
	}
	if (prim == 31 && (xo == 597 || xo == 725)) {
		unsigned nb = (unsigned)((op >> 11) & 31u);
		if (nb == 0)
			nb = 32;
		if (!emit_set_pc(e, pc))
			return 0;
		if (!emit_ra_or_0(e, W2, ra))
			return 0;
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_imm32(e, W3, nb))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)(xo == 597 ? nw_jit_helper_lswx : nw_jit_helper_stswx)))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_fault_check(e);
	}
	if (prim == 31 && xo == 306) {
		if (!emit_load_gpr(e, W1, rb))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_tlbie))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_set_pc(e, pc + 4))
			return 0;
		return emit_ret(e);
	}
	if (prim == 31 && xo == 982) {
		if (!emit_ra_or_0(e, W8, ra))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_add_reg(W1, W8, W9)))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_icbi))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return 1;
	}
	if (prim == 31 && xo == 202) {
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_imm32(e, W2, (uint32_t)ra))
			return 0;
		if (!emit_imm32(e, W3, op & 1u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_addze))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return 1;
	}
	if (prim == 31 && xo == 200) {
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_imm32(e, W2, (uint32_t)ra))
			return 0;
		if (!emit_imm32(e, W3, op & 1u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_subfze))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return 1;
	}
	if (prim == 31 && xo == 234) {
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_imm32(e, W2, (uint32_t)ra))
			return 0;
		if (!emit_imm32(e, W3, op & 1u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_addme))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return 1;
	}
	if (prim == 31 && xo == 232) {
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_imm32(e, W2, (uint32_t)ra))
			return 0;
		if (!emit_imm32(e, W3, op & 1u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_subfme))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return 1;
	}
	if (prim == 31 && xo == 407) {
		return emit_call_sthx(e, pc, rd, ra, rb, 0);
	}
	if (prim == 31 && xo == 439) {
		if (!ra)
			return 0;
		return emit_call_sthx(e, pc, rd, ra, rb, 1);
	}
	if (prim == 31 && (xo == 343 || xo == 375)) {
		return emit_call_lhx(e, pc, rd, ra, rb, xo == 375);
	}
	if (prim == 31 && xo == 279) {
		if (!emit_set_pc(e, pc))
			return 0;
		if (!emit_helper_ea_idx(e, ra, rb))
			return 0;
		if (!emit_w(e, a64_orr_reg(W1, 31, W8)))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_lh))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, a64_orr_reg(W9, 31, W0)))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_fault_check(e))
			return 0;
		return emit_store_gpr(e, W9, rd);
	}
	if (prim == 31 && xo == 311) {
		if (!ra)
			return 0;
		return emit_call_lhzux(e, pc, rd, ra, rb);
	}
	if (prim == 31 && xo == 534) {
		if (!emit_set_pc(e, pc))
			return 0;
		if (!emit_helper_ea_idx(e, ra, rb))
			return 0;
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_w(e, a64_orr_reg(W2, 31, W8)))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_lwbrx))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_fault_check(e);
	}
	if (prim == 31 && xo == 790) {
		if (!emit_set_pc(e, pc))
			return 0;
		if (!emit_helper_ea_idx(e, ra, rb))
			return 0;
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_w(e, a64_orr_reg(W2, 31, W8)))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_lhbrx))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_fault_check(e);
	}
	if (prim == 31 && xo == 918) {
		if (!emit_set_pc(e, pc))
			return 0;
		if (!emit_helper_ea_idx(e, ra, rb))
			return 0;
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_w(e, a64_orr_reg(W2, 31, W8)))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_sthbrx))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_fault_check(e);
	}
	if (prim == 31 && xo == 662) {
		if (!emit_set_pc(e, pc))
			return 0;
		if (!emit_helper_ea_idx(e, ra, rb))
			return 0;
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_w(e, a64_orr_reg(W2, 31, W8)))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)nw_jit_helper_stwbrx))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_fault_check(e);
	}
	if (prim == 31 && (xo == 7 || xo == 39 || xo == 71 || xo == 135 || xo == 167 || xo == 199)) {
		const uint32_t sz = (xo == 7 || xo == 135) ? 1u : (xo == 39 || xo == 167) ? 2u : 4u;
		if (!emit_set_pc(e, pc))
			return 0;
		if (!emit_imm32(e, W1, (uint32_t)rd))
			return 0;
		if (!emit_imm32(e, W2, (uint32_t)ra))
			return 0;
		if (!emit_imm32(e, W3, (uint32_t)rb))
			return 0;
		if (!emit_imm32(e, W4, sz))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)((xo == 7 || xo == 39 || xo == 71)
			? nw_jit_helper_lve : nw_jit_helper_stve)))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_fault_check(e);
	}
	if (prim == 40 || prim == 42 || prim == 43)
		return emit_call_lh(e, pc, rd, ra, simm, prim != 40, prim == 43);
	if (prim == 41) {
		if (!ra)
			return 0;
		return emit_call_lhzu(e, pc, rd, ra, simm);
	}
	if (prim == 44 || prim == 45) {
		return emit_call_sth(e, pc, rd, ra, simm, prim == 45);
	}
	if (prim == 46 || prim == 47) {
		if (!emit_set_pc(e, pc))
			return 0;
		if (!emit_helper_ea(e, ra, simm))
			return 0;
		if (!emit_w(e, a64_orr_reg(W1, 31, W8)))
			return 0;
		if (!emit_imm32(e, W2, (uint32_t)rd))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)(prim == 46 ? nw_jit_helper_lmw : nw_jit_helper_stmw)))
			return 0;
		if (!emit_w(e, 0xd63f0120u))
			return 0;
		if (!emit_w(e, 0xaa1303e0u))
			return 0;
		return emit_fault_check(e);
	}
	(void)is_last;
	return 0;
}

static int is_term(uint32_t op)
{
	const int prim = (int)(op >> 26);
	const int xo = (int)((op >> 1) & 0x3ff);
	const int rd = (int)((op >> 21) & 0x1f);
	const int ra = (int)((op >> 16) & 0x1f);
	(void)ra;
	/* Only unconditional transfers always emit ret. Conditional bclr
	 * must fall through to the epilogue ret (ON SIGILL at 4b2-on2). */
	return prim == 18 || (prim == 19 && (xo == 16 || xo == 528) && rd == 20);
}

static uint32_t block_chain_pc(const uint32_t *ops, int n, uint32_t guest_pc)
{
	if (n <= 0 || !ops)
		return 0;
	const uint32_t last = ops[n - 1];
	const uint32_t last_pc = guest_pc + (uint32_t)(n - 1) * 4u;
	const int prim = (int)(last >> 26);
	if (prim == 18 && (last & 1u) == 0) {
		const int32_t li = ((int32_t)(last << 6)) >> 6; /* 26-bit; low 2 = AA/LK */
		const uint32_t disp = (uint32_t)li & ~3u;
		if (last & 2u)
			return disp;
		return last_pc + disp;
	}
	if (is_term(last) || nw_jit_op_ends_block(last))
		return 0;
	return guest_pc + (uint32_t)n * 4u;
}

static int16_t block_chain_disp(const uint32_t *ops, int n)
{
	if (n <= 0 || !ops)
		return 0;
	const uint32_t last = ops[n - 1];
	const int prim = (int)(last >> 26);
	const int xo = (int)((last >> 1) & 0x3ff);
	if (prim == 16)
		return (int16_t)(last & 0xfffcu);	/* 4-aligned taken disp */
	if (prim == 19 && xo == 16)
		return 1;	/* bclr: hop to LR, not any pc==lr */
	if (prim == 19 && xo == 528)
		return 3;	/* bcctr: hop to CTR */
	return 0;
}

/* Pack live translations to the front of a spare buffer and swap.
 * A bank wipe throws away every translation in the next 4 MB. The movie
 * log showed ~8000 of them still live at each wipe (live stayed ~20k,
 * occ_max 54858 of 262144 slots) while store/icbi had already tombstoned
 * the holes in between. Compaction drops the holes and keeps the code
 * the decoder is still running. Returns 0 if the spare buffer cannot
 * be mapped; the caller then falls back to the bank wipe. */
static int compact_code(void)
{
	if (!g_code)
		return 0;
	if (!g_code_spare) {
		void *m = mmap(NULL, NW_JIT_CODE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
			       MAP_ANON | MAP_PRIVATE | MAP_JIT, -1, 0);
		if (m == MAP_FAILED)
			return 0;
		g_code_spare = (uint8_t *)m;
	}
	const size_t before = g_code_used;
	size_t used = 0;
	int live = 0;
	int dropped = 0;
#ifdef __APPLE__
	pthread_jit_write_protect_np(0);
#endif
	for (int i = 0; i < NW_JIT_CACHE; i++) {
		if (g_cache[i].used != NW_JIT_USED_LIVE)
			continue;
		if (g_cache[i].fn == NW_JIT_INTERPRET)
			continue;
		const uint32_t nb = g_cache[i].code_bytes;
		uint8_t *src = (uint8_t *)g_cache[i].fn;
		const int movable = nb >= 4 && (nb & 3u) == 0 &&
			src >= g_code && src + nb <= g_code + NW_JIT_CODE_SIZE;
		if (!movable) {
			g_cache[i].used = NW_JIT_USED_TOMB;
			g_flush++;
			g_flush_src[NW_JIT_FL_WRAP]++;
			dropped++;
			continue;
		}
		const size_t next = (used + nb + 15u) & ~(size_t)15u;
		if (next > NW_JIT_CODE_SIZE) {
			g_cache[i].used = NW_JIT_USED_TOMB;
			g_flush++;
			g_flush_src[NW_JIT_FL_WRAP]++;
			dropped++;
			continue;
		}
		memcpy(g_code_spare + used, src, nb);
		g_cache[i].fn = (nw_jit_fn)(g_code_spare + used);
		used = next;
		live++;
	}
#ifdef __APPLE__
	sys_icache_invalidate(g_code_spare, used ? used : 4);
	pthread_jit_write_protect_np(1);
#endif
	__builtin___clear_cache((char *)g_code_spare, (char *)g_code_spare + (used ? used : 4));
	uint8_t *old = g_code;
	g_code = g_code_spare;
	g_code_spare = old;
	g_code_used = used;
	if (dropped)
		pagebit_rebuild();
	if (nw_jit_stats_wanted()) {
		printf("NW-BOOT G1: jit compact live %d kept %zu was %zu dropped %d\n",
		       live, used, before, dropped);
		fflush(stdout);
	}
	return 1;
}

static size_t live_movable_bytes(void)
{
	size_t n = 0;
	if (!g_code)
		return 0;
	for (int i = 0; i < NW_JIT_CACHE; i++) {
		if (g_cache[i].used != NW_JIT_USED_LIVE)
			continue;
		if (g_cache[i].fn == NW_JIT_INTERPRET)
			continue;
		const uint32_t nb = g_cache[i].code_bytes;
		uint8_t *src = (uint8_t *)g_cache[i].fn;
		if (nb < 4 || (nb & 3u) || src < g_code || src + nb > g_code + NW_JIT_CODE_SIZE)
			continue;
		n += (size_t)nb + 15u & ~(size_t)15u;
	}
	return n;
}

static int movable_block(int i)
{
	if (g_cache[i].used != NW_JIT_USED_LIVE)
		return 0;
	if (g_cache[i].fn == NW_JIT_INTERPRET)
		return 0;
	const uint32_t nb = g_cache[i].code_bytes;
	uint8_t *src = (uint8_t *)g_cache[i].fn;
	return nb >= 4 && (nb & 3u) == 0 &&
		src >= g_code && src + nb <= g_code + NW_JIT_CODE_SIZE;
}

/* Tombstone the least-used live code until `want` bytes are free.
 * QuickTime started with the 64 MB buffer already full and about 80 KB
 * of padding. Recopying all of it on every new block stalled the guest. */
static size_t evict_cold(size_t want)
{
	static size_t bucket[65536];
	if (!g_code || want == 0)
		return 0;
	memset(bucket, 0, sizeof bucket);
	for (int i = 0; i < NW_JIT_CACHE; i++) {
		if (!movable_block(i))
			continue;
		bucket[g_cache[i].hits] += g_cache[i].code_bytes;
	}
	size_t acc = 0;
	unsigned cut = 0;
	for (; cut < 65536u; cut++) {
		acc += bucket[cut];
		if (acc >= want)
			break;
	}
	size_t freed = 0;
	int n = 0;
	for (int i = 0; i < NW_JIT_CACHE && freed < want; i++) {
		if (!movable_block(i) || g_cache[i].hits > cut)
			continue;
		freed += g_cache[i].code_bytes;
		g_cache[i].used = NW_JIT_USED_TOMB;
		g_flush++;
		g_flush_src[NW_JIT_FL_WRAP]++;
		n++;
	}
	if (n) {
		g_flush_calls[NW_JIT_FL_WRAP]++;
		pagebit_rebuild();
		if (nw_jit_stats_wanted()) {
			printf("NW-BOOT G1: jit evict freed %zu blocks %d hits<=%u\n",
			       freed, n, cut);
			fflush(stdout);
		}
	}
	return freed;
}

static int g_jit_no_room;

static int ensure_code_room(size_t need)
{
	if (g_code_used + need <= NW_JIT_CODE_SIZE) {
		const size_t cur = g_code_used / NW_JIT_BANK_SIZE;
		const size_t nxt = (g_code_used + need) / NW_JIT_BANK_SIZE;
		if (nxt != cur && nxt < (size_t)NW_JIT_BANKS) {
			/* The 64 KB reserve crosses into the next bank before the
			 * cursor does. Pack first when the holes are large enough
			 * to pull the cursor back. Boot sat in the last 64 KB of a
			 * bank with nothing to drop and recopied 25 MB on every
			 * new block (701 compacts). That stalled the VBL handler.
			 * When packing cannot escape this bank, drop the next bank
			 * once and keep writing. */
			const size_t live = live_movable_bytes();
			const size_t bank_end = (cur + 1) * NW_JIT_BANK_SIZE;
			const size_t holes = g_code_used > live ? g_code_used - live : 0;
			/* Opening the movie recopied 50 MB to reclaim 12 KB of
			 * alignment so the reserve would fit, and a present took
			 * 18 ms. Pack only when the holes are at least the reserve. */
			if (holes >= need &&
			    live + need <= bank_end && live + need <= NW_JIT_CODE_SIZE &&
			    compact_code()) {
				const size_t cur2 = g_code_used / NW_JIT_BANK_SIZE;
				const size_t nxt2 = (g_code_used + need) / NW_JIT_BANK_SIZE;
				if (nxt2 == cur2 || nxt2 >= (size_t)NW_JIT_BANKS)
					return 1;
			}
			wrap_note_occupancy();
			invalidate_bank((int)nxt);
			g_code_used = nxt * NW_JIT_BANK_SIZE;
		}
		return 1;
	}
	/* The buffer is full. Holes of a few dozen KB are alignment, not
	 * garbage: compacting 64 MB to reclaim them stalled QuickTime.
	 * Drop the least-used bank's worth, pack once, and keep compiling.
	 * Wiping bank 0 after a compact faulted stwux as the desktop
	 * appeared, so cold blocks are tombstoned before the pack. */
	const size_t live = live_movable_bytes();
	const size_t holes = g_code_used > live ? g_code_used - live : 0;
	const size_t want = (size_t)NW_JIT_BANK_SIZE;
	if (holes < want && evict_cold(want - holes) == 0 && holes < need) {
		g_jit_no_room = 1;
		return 0;
	}
	if (compact_code() && g_code_used + need <= NW_JIT_CODE_SIZE)
		return 1;
	if (live <= NW_JIT_CODE_SIZE) {
		g_jit_no_room = 1;
		return 0;
	}
	if (nw_jit_stats_wanted()) {
		printf("NW-BOOT G1: jit compact skip live_bytes %zu need %zu\n",
		       live, need);
		fflush(stdout);
	}
	wrap_note_occupancy();
	invalidate_bank(0);
	g_code_used = 0;
	return g_code_used + need <= NW_JIT_CODE_SIZE;
}

static nw_jit_fn compile_block(const uint32_t *ops, int n, uint32_t guest_pc,
			       size_t *code_bytes)
{
	if (code_bytes)
		*code_bytes = 0;
	g_jit_no_room = 0;
	if (!code_ready() || n <= 0)
		return NULL;
	{
		const size_t need = 65536;
		if (!ensure_code_room(need))
			return NULL;
	}
	g_compiles++;
	if ((g_compiles & (NW_JIT_HITS_AGE - 1u)) == 0) {
		for (int i = 0; i < NW_JIT_CACHE; i++)
			g_cache[i].hits >>= 1;
	}
#ifdef __APPLE__
	{
		const uint64_t t0 = nw_nsnow();
		pthread_jit_write_protect_np(0);
		g_wx_ns += nw_nsnow() - t0;
		g_wx_n++;
	}
#endif
	struct emit e;
	e.p = (uint32_t *)(g_code + g_code_used);
	e.end = (uint32_t *)(g_code + NW_JIT_CODE_SIZE);
	e.nfault = 0;
	e.uses_fpr = 0;
	e.uses_vr = 0;
	e.gpr_mask = 0;
	e.last_pc = 0;
	e.just_set_pc = 0;
	e.last_st_r = -1;
	e.last_st_wt = -1;
	e.pin_gpr[0] = e.pin_gpr[1] = e.pin_gpr[2] = e.pin_gpr[3] = -1;
	e.pin_next = 0;
	if (n > 0) {
		const uint32_t op0 = ops[0];
		const int prim = (int)(op0 >> 26);
		const int xo = (int)((op0 >> 1) & 0x3ff);
		e.uses_vr = (prim == 4);
		if (prim == 31 && (xo == 6 || xo == 38 || xo == 7 || xo == 39 || xo == 71 ||
				   xo == 103 || xo == 359 || xo == 135 || xo == 167 || xo == 199 ||
				   xo == 231 || xo == 487))
			e.uses_vr = 1;
		e.uses_fpr = (prim >= 48 && prim <= 55) || prim == 59 || prim == 63;
		if (prim == 31 && (xo == 535 || xo == 567 || xo == 599 || xo == 631 ||
				   xo == 663 || xo == 695 || xo == 727 || xo == 759 || xo == 983))
			e.uses_fpr = 1;
	}
	if (!e.uses_fpr) {
		for (int i = 1; i < n; i++) {
			const uint32_t opi = ops[i];
			const int p = (int)(opi >> 26);
			const int x = (int)((opi >> 1) & 0x3ff);
			if ((p >= 48 && p <= 55) || p == 59 || p == 63 ||
			    (p == 31 && (x == 535 || x == 567 || x == 599 || x == 631 ||
					 x == 663 || x == 695 || x == 727 || x == 759 || x == 983))) {
				e.uses_fpr = 1;
				break;
			}
		}
	}
	for (int i = 0; i < n; i++)
		e.gpr_mask |= nw_jit_op_gpr_mask(ops[i]);
	uint32_t *start = e.p;
	if (!emit_prologue(&e)) {
#ifdef __APPLE__
		pthread_jit_write_protect_np(1);
#endif
		return NULL;
	}
	for (int i = 0; i < n; i++) {
		if (!emit_op(&e, ops[i], guest_pc + (uint32_t)i * 4, i == n - 1)) {
#ifdef __APPLE__
			pthread_jit_write_protect_np(1);
#endif
			return NULL;
		}
	}
	if (!is_term(ops[n - 1])) {
		if (!emit_set_pc(&e, guest_pc + (uint32_t)n * 4)) {
#ifdef __APPLE__
			pthread_jit_write_protect_np(1);
#endif
			return NULL;
		}
		uint32_t *epilogue = e.p;
		if (!emit_chain_epilogue(&e, block_chain_pc(ops, n, guest_pc))) {
#ifdef __APPLE__
			pthread_jit_write_protect_np(1);
#endif
			return NULL;
		}
		for (int i = 0; i < e.nfault; i++) {
			int32_t delta = (int32_t)(epilogue - e.fault_br[i]);
			const int rt = (int)(*e.fault_br[i] & 31u);
			*e.fault_br[i] = a64_cbnz(rt, delta);
		}
	} else if (e.nfault) {
		/* Terminator already emitted ret. Faults must not fall into it
		 * (cbnz 0 is a hang) and must not execute the branch. */
		uint32_t *fault_ep = e.p;
		if (!emit_ret(&e)) {
#ifdef __APPLE__
			pthread_jit_write_protect_np(1);
#endif
			return NULL;
		}
		for (int i = 0; i < e.nfault; i++) {
			int32_t delta = (int32_t)(fault_ep - e.fault_br[i]);
			const int rt = (int)(*e.fault_br[i] & 31u);
			*e.fault_br[i] = a64_cbnz(rt, delta);
		}
	}
	size_t bytes = (size_t)((uint8_t *)e.p - (g_code + g_code_used));
	g_code_used += bytes;
	g_code_emitted += bytes;
	if (code_bytes)
		*code_bytes = bytes;
#ifdef __APPLE__
	{
		uint64_t t0 = nw_nsnow();
		pthread_jit_write_protect_np(1);
		g_wx_ns += nw_nsnow() - t0;
		t0 = nw_nsnow();
		sys_icache_invalidate(start, bytes);
		g_icache_ns += nw_nsnow() - t0;
	}
#endif
	__builtin___clear_cache((char *)start, (char *)e.p);
	return (nw_jit_fn)start;
}

nw_jit_fn nw_jit_compile(const uint32_t *ops, int n, uint32_t guest_pc,
			uint32_t phys_page, uint32_t msr_ir, uint32_t endian)
{
	if (endian != 0)
		return NULL;
	int cached_n = 0;
	nw_jit_fn hit = nw_jit_cache_get(phys_page, guest_pc, msr_ir, endian, &cached_n);
	if (hit && hit != NW_JIT_INTERPRET && cached_n == n)
		return hit;
	if (hit && hit != NW_JIT_INTERPRET && cached_n != n)
		g_recompile_n++;
	size_t code_bytes = 0;
	nw_jit_fn fn = compile_block(ops, n, guest_pc, &code_bytes);
	if (!fn) {
		/* Remember a full buffer so the next execution interprets
		 * instead of scanning the cache and recopying it. */
		if (g_jit_no_room) {
			const uint32_t op0 = (n > 0) ? ops[0] : 0;
			nw_jit_cache_put(phys_page, guest_pc, msr_ir, endian,
					 NW_JIT_INTERPRET, n, op0, 0, 0, 0, 0, 0, 0);
		}
		return NULL;
	}
	const uint32_t op0 = (n > 0) ? ops[0] : 0;
	const int prim = (int)(op0 >> 26);
	const int xo = (int)((op0 >> 1) & 0x3ff);
	int uses_vr = (prim == 4);
	if (prim == 31 && (xo == 6 || xo == 38 || xo == 7 || xo == 39 || xo == 71 ||
			   xo == 103 || xo == 359 || xo == 135 || xo == 167 || xo == 199 ||
			   xo == 231 || xo == 487))
		uses_vr = 1;
	int uses_fpr = (prim >= 48 && prim <= 55) || prim == 59 || prim == 63;
	if (prim == 31 && (xo == 535 || xo == 567 || xo == 599 || xo == 631 ||
			   xo == 663 || xo == 695 || xo == 727 || xo == 759 || xo == 983))
		uses_fpr = 1;
	if (!uses_fpr) {
		for (int i = 1; i < n; i++) {
			const uint32_t opi = ops[i];
			const int p = (int)(opi >> 26);
			const int x = (int)((opi >> 1) & 0x3ff);
			if ((p >= 48 && p <= 55) || p == 59 || p == 63 ||
			    (p == 31 && (x == 535 || x == 567 || x == 599 || x == 631 ||
					 x == 663 || x == 695 || x == 727 || x == 759 || x == 983))) {
				uses_fpr = 1;
				break;
			}
		}
	}
	uint32_t gpr_mask = 0;
	for (int i = 0; i < n; i++)
		gpr_mask |= nw_jit_op_gpr_mask(ops[i]);
	nw_jit_cache_put(phys_page, guest_pc, msr_ir, endian, fn, n, op0, uses_fpr, uses_vr,
			 block_chain_pc(ops, n, guest_pc), gpr_mask,
			 block_chain_disp(ops, n), (uint32_t)code_bytes);
	return fn;
}

#else

nw_jit_fn nw_jit_compile(const uint32_t *, int, uint32_t, uint32_t, uint32_t, uint32_t)
{
	return NULL;
}

#endif
