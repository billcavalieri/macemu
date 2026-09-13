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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#ifdef __APPLE__
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#endif

enum { NW_JIT_CODE_SIZE = 1 << 20, NW_JIT_CACHE = 256 };

struct nw_jit_entry {
	uint32_t phys_page, guest_pc, msr_ir, endian;
	nw_jit_fn fn;
};

static uint8_t *g_code;
static size_t g_code_used;
static struct nw_jit_entry g_cache[NW_JIT_CACHE];
static int g_ncache;
static uint64_t g_flush;

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
	g_ncache = 0;
	g_code_used = 0;
	g_flush = 0;
}

void nw_jit_invalidate_page(uint32_t phys_page)
{
	int w = 0;
	for (int i = 0; i < g_ncache; i++) {
		if (g_cache[i].phys_page != phys_page)
			g_cache[w++] = g_cache[i];
		else
			g_flush++;
	}
	g_ncache = w;
}

uint64_t nw_jit_flush_count(void)
{
	return g_flush;
}

uint32_t nw_ppc_addi(int rd, int ra, int simm)
{
	return (14u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) | ((uint32_t)simm & 0xffffu);
}

uint32_t nw_ppc_add(int rd, int ra, int rb, int rc)
{
	return 0x7c000214u | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) | ((uint32_t)rb << 11) | (rc ? 1u : 0);
}

uint32_t nw_ppc_rlwinm(int ra, int rs, int sh, int mb, int me)
{
	return (21u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)sh << 11) | ((uint32_t)mb << 6) | ((uint32_t)me << 1);
}

uint32_t nw_ppc_lwz(int rd, int ra, int d)
{
	return (32u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) | ((uint32_t)d & 0xffffu);
}

uint32_t nw_ppc_stw(int rs, int ra, int d)
{
	return (36u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) | ((uint32_t)d & 0xffffu);
}

uint32_t nw_ppc_cmp(int ra, int rb)
{
	return 0x7c000000u | ((uint32_t)ra << 16) | ((uint32_t)rb << 11);
}

uint32_t nw_ppc_cmpi(int ra, int simm)
{
	return (11u << 26) | ((uint32_t)ra << 16) | ((uint32_t)simm & 0xffffu);
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

static uint32_t ra_or_0(const struct nw_jit_cpu *cpu, int ra)
{
	return ra ? cpu->gpr[ra] : 0;
}

static int mem_ok(const struct nw_jit_cpu *cpu, uint32_t ea)
{
	return cpu->mem && ea >= cpu->mem_base && (ea - cpu->mem_base) + 4 <= cpu->mem_size;
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
	if (prim == 11) {
		record_cr0(cpu, (int32_t)cpu->gpr[ra] - simm);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 16) {
		const int bo = rd, bi = ra;
		const int32_t disp = (int16_t)(op & 0xfffcu);
		const int crbit = (int)((cpu->cr >> (31 - bi)) & 1);
		int take = 0;
		if (bo == NW_PPC_BO_TRUE)
			take = crbit;
		else if (bo == NW_PPC_BO_FALSE)
			take = !crbit;
		else
			return -1;
		cpu->pc = take ? (uint32_t)(pc + disp) : pc + 4;
		return take ? 1 : 0;
	}
	if (prim == 18) {
		const int32_t disp = (((int32_t)(op << 6)) >> 6) & ~3;
		if (op & 1)
			cpu->lr = pc + 4;
		cpu->pc = (uint32_t)(pc + disp);
		return 1;
	}
	if (prim == 19 && xo == 16 && rd == 20 && ra == 0) {
		cpu->pc = cpu->lr;
		return 1;
	}
	if (prim == 21) {
		const int sh = rb, mb = (int)((op >> 6) & 0x1f), me = (int)((op >> 1) & 0x1f);
		cpu->gpr[ra] = rotl32(cpu->gpr[rd], (uint32_t)sh) & ppc_mask((uint32_t)mb, (uint32_t)me);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 266) {
		cpu->gpr[rd] = cpu->gpr[ra] + cpu->gpr[rb];
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[rd]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 0) {
		record_cr0(cpu, (int32_t)cpu->gpr[ra] - (int32_t)cpu->gpr[rb]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 339 && spr_num(op) == NW_PPC_SPR_DEC) {
		cpu->gpr[rd] = cpu->dec;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 467 && spr_num(op) == NW_PPC_SPR_DEC) {
		cpu->dec = cpu->gpr[rd];
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 32) {
		const uint32_t ea = ra_or_0(cpu, ra) + (uint32_t)simm;
		if (!mem_ok(cpu, ea))
			return -1;
		cpu->gpr[rd] = mem_ld_be(cpu, ea);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 36) {
		const uint32_t ea = ra_or_0(cpu, ra) + (uint32_t)simm;
		if (!mem_ok(cpu, ea))
			return -1;
		mem_st_be(cpu, ea, cpu->gpr[rd]);
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
	}
	return last;
}

#if defined(__aarch64__)

enum { W8 = 8, W9 = 9, W10 = 10, X0 = 0, X11 = 11 };

struct emit {
	uint32_t *p;
	uint32_t *end;
};

static int emit_w(struct emit *e, uint32_t w)
{
	if (e->p >= e->end)
		return 0;
	*e->p++ = w;
	return 1;
}

static uint32_t a64_add_reg(int rd, int rn, int rm)
{
	return 0x0b000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_sub_reg(int rd, int rn, int rm)
{
	return 0x4b000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_and_reg(int rd, int rn, int rm)
{
	return 0x0a000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_orr_reg(int rd, int rn, int rm)
{
	return 0x2a000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
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

static uint32_t a64_str_w(int rt, int rn, uint32_t off)
{
	return 0xb9000000u | ((off >> 2) << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}

static uint32_t a64_ldr_x(int rt, int rn, uint32_t off)
{
	return 0xf9400000u | ((off >> 3) << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}

static uint32_t a64_add_uxtw(int xd, int xn, int wm)
{
	return 0x8b200000u | ((uint32_t)wm << 16) | (2u << 13) | ((uint32_t)xn << 5) | (uint32_t)xd;
}

static uint32_t a64_rev(int rd, int rn)
{
	return 0x5ac00800u | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_extr(int rd, int rn, int rm, int lsb)
{
	return 0x13800000u | ((uint32_t)rm << 16) | ((uint32_t)lsb << 10) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_lsr(int rd, int rn, int n)
{
	return 0x53007c00u | ((uint32_t)n << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
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

static uint32_t a64_cbz(int rt, int imm19)
{
	return 0x34000000u | (((uint32_t)imm19 & 0x7ffffu) << 5) | (uint32_t)rt;
}

static uint32_t a64_cbnz(int rt, int imm19)
{
	return 0x35000000u | (((uint32_t)imm19 & 0x7ffffu) << 5) | (uint32_t)rt;
}

static int emit_imm32(struct emit *e, int rd, uint32_t v)
{
	if (!emit_w(e, a64_movz(rd, v & 0xffffu, 0)))
		return 0;
	return emit_w(e, a64_movk(rd, v >> 16, 1));
}

static int emit_load_gpr(struct emit *e, int wt, int r)
{
	return emit_w(e, a64_ldr_w(wt, X0, (uint32_t)offsetof(struct nw_jit_cpu, gpr) + (uint32_t)r * 4u));
}

static int emit_store_gpr(struct emit *e, int wt, int r)
{
	return emit_w(e, a64_str_w(wt, X0, (uint32_t)offsetof(struct nw_jit_cpu, gpr) + (uint32_t)r * 4u));
}

static int emit_ra_or_0(struct emit *e, int wt, int ra)
{
	if (ra == 0)
		return emit_w(e, a64_movz(wt, 0, 0));
	return emit_load_gpr(e, wt, ra);
}

static int emit_set_pc(struct emit *e, uint32_t pc)
{
	if (!emit_imm32(e, W8, pc))
		return 0;
	return emit_w(e, a64_str_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, pc)));
}

static int emit_ret(struct emit *e)
{
	return emit_w(e, 0xd65f03c0u);
}

static int emit_cr0_from_w8(struct emit *e)
{
	/* w8 = signed result. CR0 in w9: LT=8, GT=4, EQ=2, SO from XER[31]. */
	if (!emit_w(e, a64_movz(W9, 2, 0)))
		return 0;
	if (!emit_w(e, 0x7100011fu))			/* SUBS WZR, W8, #0 */
		return 0;
	if (!emit_w(e, 0x54000084u))			/* B.MI +4 → LT */
		return 0;
	if (!emit_w(e, 0x54000080u))			/* B.EQ +4 → join */
		return 0;
	if (!emit_w(e, a64_movz(W9, 4, 0)))		/* GT */
		return 0;
	if (!emit_w(e, 0x14000002u))			/* B +2 → join */
		return 0;
	if (!emit_w(e, a64_movz(W9, 8, 0)))		/* LT */
		return 0;
	if (!emit_w(e, a64_ldr_w(W10, X0, (uint32_t)offsetof(struct nw_jit_cpu, xer))))
		return 0;
	if (!emit_w(e, a64_lsr(W10, W10, 31)))
		return 0;
	if (!emit_w(e, a64_orr_reg(W9, W9, W10)))
		return 0;
	if (!emit_w(e, a64_ldr_w(W10, X0, (uint32_t)offsetof(struct nw_jit_cpu, cr))))
		return 0;
	if (!emit_imm32(e, W8, 0x0fffffffu))
		return 0;
	if (!emit_w(e, a64_and_reg(W10, W10, W8)))
		return 0;
	if (!emit_w(e, a64_lsl(W9, W9, 28)))
		return 0;
	if (!emit_w(e, a64_orr_reg(W10, W10, W9)))
		return 0;
	return emit_w(e, a64_str_w(W10, X0, (uint32_t)offsetof(struct nw_jit_cpu, cr)));
}

static int emit_mem_ea(struct emit *e, int ra, int simm)
{
	if (!emit_ra_or_0(e, W8, ra))
		return 0;
	if (!emit_imm32(e, W9, (uint32_t)simm))
		return 0;
	if (!emit_w(e, a64_add_reg(W8, W8, W9)))
		return 0;
	if (!emit_w(e, a64_ldr_x(X11, X0, (uint32_t)offsetof(struct nw_jit_cpu, mem))))
		return 0;
	if (!emit_w(e, a64_ldr_w(W9, X0, (uint32_t)offsetof(struct nw_jit_cpu, mem_base))))
		return 0;
	if (!emit_w(e, a64_sub_reg(W8, W8, W9)))
		return 0;
	return emit_w(e, a64_add_uxtw(X11, X11, W8));
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
	if (prim == 11) {
		if (!emit_load_gpr(e, W8, ra))
			return 0;
		if (!emit_imm32(e, W9, (uint32_t)simm))
			return 0;
		if (!emit_w(e, a64_sub_reg(W8, W8, W9)))
			return 0;
		return emit_cr0_from_w8(e);
	}
	if (prim == 16) {
		const int bo = rd, bi = ra;
		const int32_t disp = (int16_t)(op & 0xfffcu);
		if (bo != NW_PPC_BO_TRUE && bo != NW_PPC_BO_FALSE)
			return 0;
		if (!emit_w(e, a64_ldr_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, cr))))
			return 0;
		if (!emit_w(e, a64_lsr(W8, W8, 31 - bi)))
			return 0;
		if (!emit_w(e, a64_and_imm1(W8, W8)))
			return 0;
		/* skip 4 insns (movz, movk, str pc, ret) if not taken */
		if (bo == NW_PPC_BO_TRUE) {
			if (!emit_w(e, a64_cbz(W8, 5)))
				return 0;
		} else {
			if (!emit_w(e, a64_cbnz(W8, 5)))
				return 0;
		}
		if (!emit_set_pc(e, (uint32_t)(pc + disp)))
			return 0;
		if (!emit_ret(e))
			return 0;
		if (is_last)
			return emit_set_pc(e, pc + 4);
		return 1;
	}
	if (prim == 18) {
		const int32_t disp = (((int32_t)(op << 6)) >> 6) & ~3;
		if (op & 1) {
			if (!emit_imm32(e, W8, pc + 4))
				return 0;
			if (!emit_w(e, a64_str_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, lr))))
				return 0;
		}
		if (!emit_set_pc(e, (uint32_t)(pc + disp)))
			return 0;
		return emit_ret(e);
	}
	if (prim == 19 && xo == 16 && rd == 20 && ra == 0) {
		if (!emit_w(e, a64_ldr_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, lr))))
			return 0;
		if (!emit_w(e, a64_str_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, pc))))
			return 0;
		return emit_ret(e);
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
		return emit_store_gpr(e, W8, ra);
	}
	if (prim == 31 && xo == 266) {
		if (!emit_load_gpr(e, W8, ra))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_add_reg(W8, W8, W9)))
			return 0;
		if (!emit_store_gpr(e, W8, rd))
			return 0;
		if (op & 1)
			return emit_cr0_from_w8(e);
		return 1;
	}
	if (prim == 31 && xo == 0) {
		if (!emit_load_gpr(e, W8, ra))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		if (!emit_w(e, a64_sub_reg(W8, W8, W9)))
			return 0;
		return emit_cr0_from_w8(e);
	}
	if (prim == 31 && xo == 339 && spr_num(op) == NW_PPC_SPR_DEC) {
		if (!emit_w(e, a64_ldr_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, dec))))
			return 0;
		return emit_store_gpr(e, W8, rd);
	}
	if (prim == 31 && xo == 467 && spr_num(op) == NW_PPC_SPR_DEC) {
		if (!emit_load_gpr(e, W8, rd))
			return 0;
		return emit_w(e, a64_str_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, dec)));
	}
	if (prim == 32) {
		if (!emit_mem_ea(e, ra, simm))
			return 0;
		if (!emit_w(e, a64_ldr_w(W8, X11, 0)))
			return 0;
		if (!emit_w(e, a64_rev(W8, W8)))
			return 0;
		return emit_store_gpr(e, W8, rd);
	}
	if (prim == 36) {
		if (!emit_mem_ea(e, ra, simm))
			return 0;
		if (!emit_load_gpr(e, W8, rd))
			return 0;
		if (!emit_w(e, a64_rev(W8, W8)))
			return 0;
		return emit_w(e, a64_str_w(W8, X11, 0));
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
	return prim == 18 || (prim == 19 && xo == 16 && rd == 20 && ra == 0);
}

static nw_jit_fn compile_block(const uint32_t *ops, int n, uint32_t guest_pc)
{
	if (!code_ready() || n <= 0)
		return NULL;
	if (g_code_used + 4096 > NW_JIT_CODE_SIZE) {
		g_code_used = 0;
		g_ncache = 0;
		g_flush++;
	}
#ifdef __APPLE__
	pthread_jit_write_protect_np(0);
#endif
	struct emit e;
	e.p = (uint32_t *)(g_code + g_code_used);
	e.end = (uint32_t *)(g_code + NW_JIT_CODE_SIZE);
	uint32_t *start = e.p;
	for (int i = 0; i < n; i++) {
		if (!emit_op(&e, ops[i], guest_pc + (uint32_t)i * 4, i == n - 1)) {
#ifdef __APPLE__
			pthread_jit_write_protect_np(1);
#endif
			return NULL;
		}
	}
	if (!is_term(ops[n - 1])) {
		if (!emit_set_pc(&e, guest_pc + (uint32_t)n * 4) || !emit_ret(&e)) {
#ifdef __APPLE__
			pthread_jit_write_protect_np(1);
#endif
			return NULL;
		}
	}
	size_t bytes = (size_t)((uint8_t *)e.p - (g_code + g_code_used));
	g_code_used += bytes;
#ifdef __APPLE__
	pthread_jit_write_protect_np(1);
	sys_icache_invalidate(start, bytes);
#endif
	__builtin___clear_cache((char *)start, (char *)e.p);
	return (nw_jit_fn)start;
}

nw_jit_fn nw_jit_compile(const uint32_t *ops, int n, uint32_t guest_pc,
			uint32_t phys_page, uint32_t msr_ir, uint32_t endian)
{
	if (endian != 0)
		return NULL;
	for (int i = 0; i < g_ncache; i++) {
		if (g_cache[i].phys_page == phys_page && g_cache[i].guest_pc == guest_pc &&
		    g_cache[i].msr_ir == msr_ir && g_cache[i].endian == endian)
			return g_cache[i].fn;
	}
	nw_jit_fn fn = compile_block(ops, n, guest_pc);
	if (!fn)
		return NULL;
	if (g_ncache < NW_JIT_CACHE) {
		g_cache[g_ncache].phys_page = phys_page;
		g_cache[g_ncache].guest_pc = guest_pc;
		g_cache[g_ncache].msr_ir = msr_ir;
		g_cache[g_ncache].endian = endian;
		g_cache[g_ncache].fn = fn;
		g_ncache++;
	}
	return fn;
}

#else

nw_jit_fn nw_jit_compile(const uint32_t *, int, uint32_t, uint32_t, uint32_t, uint32_t)
{
	return NULL;
}

#endif
