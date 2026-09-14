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

uint32_t nw_jit_helper_lwz(struct nw_jit_cpu *cpu, uint32_t ea);
void nw_jit_helper_stw(struct nw_jit_cpu *cpu, uint32_t ea, uint32_t val);
uint32_t nw_jit_helper_lwz_pa(struct nw_jit_cpu *cpu, uint32_t pa);
void nw_jit_helper_stw_pa(struct nw_jit_cpu *cpu, uint32_t pa, uint32_t val);
uint32_t nw_jit_helper_lh(struct nw_jit_cpu *cpu, uint32_t ea);
void nw_jit_helper_sth(struct nw_jit_cpu *cpu, uint32_t ea, uint32_t val);
uint32_t nw_jit_helper_lb(struct nw_jit_cpu *cpu, uint32_t ea);
void nw_jit_helper_stb(struct nw_jit_cpu *cpu, uint32_t ea, uint32_t val);

enum { NW_JIT_CODE_SIZE = 1 << 20, NW_JIT_CACHE = 4096, NW_JIT_PROBE = 8 };

struct nw_jit_entry {
	uint32_t phys_page, guest_pc, msr_ir, endian;
	nw_jit_fn fn;
	uint8_t used;
	uint8_t n;
};

static uint8_t *g_code;
static size_t g_code_used;
static struct nw_jit_entry g_cache[NW_JIT_CACHE];
static uint8_t g_pagebit[512];	/* 4096 bits: (phys>>12)&4095 may have compiled code */
static uint64_t g_flush;
static uint64_t g_flush_src[NW_JIT_FL_N];	/* entries dropped, by cause */
static uint64_t g_flush_calls[NW_JIT_FL_N];	/* invalidate calls, by cause */
static uint64_t g_compiles;
static uint64_t g_exec_blocks, g_exec_insns;
static int g_mode = -1;
static nw_jit_host_lwz g_host_lwz;
static nw_jit_host_stw g_host_stw;
static nw_jit_host_lwz_pa g_host_lwz_pa;
static nw_jit_host_stw_pa g_host_stw_pa;
static nw_jit_host_lh g_host_lh;
static nw_jit_host_sth16 g_host_sth16;
static nw_jit_host_lb g_host_lb;
static nw_jit_host_stb8 g_host_stb;

static struct nw_jit_dtlb_ent g_dtlb[NW_JIT_DTLB_N];
static_assert(sizeof(struct nw_jit_dtlb_ent) == 32, "dtlb entry is 32 bytes");
static uint64_t g_dtlb_hit, g_dtlb_miss;

struct nw_jit_hist {
	int prim;
	int xo;
	const char *name;
	uint64_t n, miss, insns;
};

static struct nw_jit_hist g_hist[] = {
	{14, -1, "addi", 0, 0, 0},
	{12, -1, "addic", 0, 0, 0},
	{13, -1, "addic.", 0, 0, 0},
	{31, 10, "addc", 0, 0, 0},
	{31, 522, "addco", 0, 0, 0},
	{31, 520, "subfco", 0, 0, 0},
	{11, -1, "cmpi", 0, 0, 0},
	{10, -1, "cmpli", 0, 0, 0},
	{28, -1, "andi.", 0, 0, 0},
	{31, 144, "mtcrf", 0, 0, 0},
	{21, -1, "rlwinm", 0, 0, 0},
	{20, -1, "rlwimi", 0, 0, 0},
	{16, -1, "bc", 0, 0, 0},
	{18, -1, "b", 0, 0, 0},
	{19, 16, "blr", 0, 0, 0},
	{19, 528, "bcctr", 0, 0, 0},
	{31, 266, "add", 0, 0, 0},
	{31, 444, "or", 0, 0, 0},
	{24, -1, "ori", 0, 0, 0},
	{31, 0, "cmp", 0, 0, 0},
	{31, 339, "mfspr", 0, 0, 0},
	{31, 467, "mtspr", 0, 0, 0},
	{32, -1, "lwz", 0, 0, 0},
	{33, -1, "lwzu", 0, 0, 0},
	{34, -1, "lbz", 0, 0, 0},
	{38, -1, "stb", 0, 0, 0},
	{36, -1, "stw", 0, 0, 0},
	{37, -1, "stwu", 0, 0, 0},
	{31, 23, "lwzx", 0, 0, 0},
	{40, -1, "lhz", 0, 0, 0},
	{42, -1, "lha", 0, 0, 0},
	{43, -1, "lhau", 0, 0, 0},
	{44, -1, "sth", 0, 0, 0},
};

static uint64_t g_v_cmp, g_v_miss, g_v_fail, g_v_skip_unsup, g_v_skip_mem;
static uint64_t g_v_skip_dsi, g_v_skip_io, g_v_other, g_v_other_miss;

enum { NW_JIT_SKIPN = 256, NW_JIT_SKIPTOP = 12 };
static struct {
	int prim, xo;
	uint64_t n;
} g_skip[NW_JIT_SKIPN];

enum { NW_JIT_PCHOT = 1024, NW_JIT_PCPROBE = 8, NW_JIT_PCTOP = 12 };
static struct {
	uint32_t pc, op;
	uint64_t n;
} g_pchot[NW_JIT_PCHOT];
static uint64_t g_v_blocks[NW_JIT_MAX_BLOCK + 1];

static uint32_t spr_num(uint32_t op);

static int cache_slot(uint32_t phys_page, uint32_t guest_pc, uint32_t msr_ir, uint32_t endian)
{
	uint32_t h = phys_page ^ (guest_pc * 0x9e3779b1u) ^ (msr_ir << 16) ^ endian;
	return (int)(h & (NW_JIT_CACHE - 1));
}

static void pagebit_set(uint32_t phys_page)
{
	const unsigned i = (phys_page >> 12) & 4095u;
	g_pagebit[i >> 3] |= (uint8_t)(1u << (i & 7u));
}

static int pagebit_get(uint32_t phys_page)
{
	const unsigned i = (phys_page >> 12) & 4095u;
	return g_pagebit[i >> 3] & (uint8_t)(1u << (i & 7u));
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
	memset(g_pagebit, 0, sizeof(g_pagebit));
	g_code_used = 0;
	g_flush = 0;
	g_compiles = 0;
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
	nw_jit_dtlb_flush();
	g_dtlb_hit = g_dtlb_miss = 0;
}

void nw_jit_invalidate_page_src(uint32_t phys_page, int src)
{
	phys_page &= ~0xfffu;
	if (src < 0 || src >= NW_JIT_FL_N)
		src = NW_JIT_FL_OTHER;
	if (!pagebit_get(phys_page))
		return;
	g_flush_calls[src]++;
	for (int i = 0; i < NW_JIT_CACHE; i++) {
		if (g_cache[i].used && g_cache[i].phys_page == phys_page) {
			g_cache[i].used = 0;
			g_flush++;
			g_flush_src[src]++;
		}
	}
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
		if (g_cache[i].used) {
			g_cache[i].used = 0;
			g_flush++;
			g_flush_src[src]++;
		}
	}
	memset(g_pagebit, 0, sizeof(g_pagebit));
}

void nw_jit_invalidate_all(void)
{
	nw_jit_invalidate_all_src(NW_JIT_FL_OTHER);
}

uint64_t nw_jit_flush_count(void)
{
	return g_flush;
}

uint64_t nw_jit_compile_count(void)
{
	return g_compiles;
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

void nw_jit_stats_print(const char *why)
{
	if (!g_exec_blocks && nw_jit_mode() == NW_JIT_OFF)
		return;
	static const char *const src_name[NW_JIT_FL_N] = {
		"store", "icbi", "tlb", "sr", "bat", "sdr1", "wrap",
		"istore", "host", "other"
	};
	printf("NW-BOOT G1: jit stats %s mode %s blocks %llu insns %llu flush %llu compiles %llu dtlb hit %llu miss %llu\n",
	       why, nw_jit_mode_name(),
	       (unsigned long long)g_exec_blocks,
	       (unsigned long long)g_exec_insns,
	       (unsigned long long)g_flush,
	       (unsigned long long)g_compiles,
	       (unsigned long long)g_dtlb_hit,
	       (unsigned long long)g_dtlb_miss);
	for (int i = 0; i < NW_JIT_FL_N; i++) {
		if (g_flush_calls[i] || g_flush_src[i])
			printf("NW-BOOT G1: jit flush %s calls %llu entries %llu\n",
			       src_name[i],
			       (unsigned long long)g_flush_calls[i],
			       (unsigned long long)g_flush_src[i]);
	}
	{
		int top[NW_JIT_SKIPTOP];
		int ntop = 0;
		for (int i = 0; i < NW_JIT_SKIPN; i++) {
			if (!g_skip[i].n)
				continue;
			int k = ntop;
			while (k > 0 && g_skip[i].n > g_skip[top[k - 1]].n)
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
			else if (p == 37)
				nm = "stwu";
			else if (p == 13)
				nm = "addic.";
			else if (p == 12)
				nm = "addic";
			else if (p == 31 && x == 151)
				nm = "stwx";
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
			else if (p == 31 && x == 144)
				nm = "mtcrf";
			else if (p == 31 && x == 87)
				nm = "lbzx";
			else if (p == 31 && x == 215)
				nm = "stbx";
			else if (p == 31 && x == 790)
				nm = "lhax";
			else if (p == 10)
				nm = "cmpli";
			else if (p == 15)
				nm = "addis";
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
			printf("NW-BOOT G1: jit skip_unsup %s prim=%d xo=%d n=%llu\n",
			       nm ? nm : "?", p, x,
			       (unsigned long long)g_skip[j].n);
		}
	}
	fflush(stdout);
}

static void nw_jit_atexit_stats(void)
{
	nw_jit_stats_print("exit");
	nw_jit_verify_dump("exit");
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

void nw_jit_dtlb_flush(void)
{
	memset(g_dtlb, 0, sizeof(g_dtlb));
}

void nw_jit_dtlb_fill(uint32_t ea, uint32_t pa, int writable, uint64_t host)
{
	const unsigned i = (ea >> 12) & (NW_JIT_DTLB_N - 1u);
	g_dtlb[i].ea_page = ea & ~0xfffu;
	g_dtlb[i].pa_page = pa & ~0xfffu;
	g_dtlb[i].host = host;
	uint32_t flags = NW_JIT_DTLB_VALID;
	if (writable)
		flags |= NW_JIT_DTLB_WRITE;
	if (host)
		flags |= NW_JIT_DTLB_HOST;
	g_dtlb[i].flags = flags;
}

int nw_jit_dtlb_lookup(uint32_t ea, int is_store, uint32_t *pa)
{
	const unsigned i = (ea >> 12) & (NW_JIT_DTLB_N - 1u);
	const struct nw_jit_dtlb_ent *e = &g_dtlb[i];
	if (!(e->flags & NW_JIT_DTLB_VALID) || e->ea_page != (ea & ~0xfffu))
		return 0;
	if (is_store && !(e->flags & NW_JIT_DTLB_WRITE))
		return 0;
	if (pa)
		*pa = e->pa_page | (ea & 0xfffu);
	return 1;
}

uint64_t nw_jit_dtlb_hits(void)
{
	return g_dtlb_hit;
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

void nw_jit_note_exec(int n)
{
	if (n <= 0)
		return;
	g_exec_blocks++;
	g_exec_insns += (uint64_t)n;
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

int nw_jit_op_supported(uint32_t op)
{
	const int prim = (int)(op >> 26);
	const int rd = (int)((op >> 21) & 0x1f);
	const int xo = (int)((op >> 1) & 0x3ff);
	if (prim == 14 || prim == 12 || prim == 13)
		return 1;	/* addi / addic / addic. */
	if (prim == 31 && (xo == 10 || xo == 522))
		return 1;	/* addc / addco */
	if (prim == 31 && xo == 520)
		return 1;	/* subfco */
	if (prim == 28)
		return 1;	/* andi. */
	if (prim == 10)
		return (rd & 3) == 0;	/* cmpli L=0, any crfD */
	if (prim == 31 && xo == 144)
		return 1;	/* mtcrf */
	if (prim == 11)
		return (rd & 3) == 0;	/* cmpi L=0, any crfD */
	if (prim == 20 || prim == 21)
		return 1;	/* rlwimi / rlwinm */
	if (prim == 16)
		return bo_is_cr(rd) && (op & 3) == 0;
	if (prim == 18)
		return (op & 2) == 0;
	if (prim == 19 && (xo == 16 || xo == 528) && (rd == 20 || bo_is_cr(rd)))
		return 1;	/* blr / bclr / bcctr (CR true/false, likely ignored) */
	if (prim == 31 && xo == 266)
		return 1;
	if (prim == 31 && xo == 444)
		return 1;	/* or / mr */
	if (prim == 24)
		return 1;	/* ori */
	if (prim == 31 && xo == 0)
		return rd == 0;	/* cmp cr0 */
	if (prim == 31 && (xo == 339 || xo == 467) && spr_is_user(spr_num(op)))
		return 1;
	if (prim == 32 || prim == 33 || prim == 36 || prim == 37)
		return 1;	/* lwz / lwzu / stw / stwu */
	if (prim == 34 || prim == 38)
		return 1;	/* lbz / stb */
	if (prim == 31 && xo == 23)
		return 1;	/* lwzx */
	if (prim == 40 || prim == 42 || prim == 43 || prim == 44)
		return 1;	/* lhz / lha / lhau / sth */
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

void nw_jit_note_skip_unsup(uint32_t op)
{
	g_v_skip_unsup++;
	const int prim = (int)(op >> 26);
	const int xo = (prim == 19 || prim == 31 || prim == 59 || prim == 63)
			       ? (int)((op >> 1) & 0x3ff) : -1;
	const uint32_t h = (uint32_t)prim * 0x9e3779b1u ^ (uint32_t)(xo + 1) * 0x85ebca6bu;
	int i = (int)(h & (NW_JIT_SKIPN - 1));
	for (int n = 0; n < 8; n++) {
		int j = (i + n) & (NW_JIT_SKIPN - 1);
		if (g_skip[j].n == 0 ||
		    (g_skip[j].prim == prim && g_skip[j].xo == xo)) {
			g_skip[j].prim = prim;
			g_skip[j].xo = xo;
			g_skip[j].n++;
			return;
		}
	}
	g_skip[i].prim = prim;
	g_skip[i].xo = xo;
	g_skip[i].n = 1;
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
	return prim == 16 || prim == 18 || (prim == 19 && (xo == 16 || xo == 528));
}

nw_jit_fn nw_jit_cache_get(uint32_t phys_page, uint32_t guest_pc,
			  uint32_t msr_ir, uint32_t endian, int *n_out)
{
	int i = cache_slot(phys_page, guest_pc, msr_ir, endian);
	for (int n = 0; n < NW_JIT_PROBE; n++) {
		int j = (i + n) & (NW_JIT_CACHE - 1);
		if (!g_cache[j].used)
			continue;
		if (g_cache[j].phys_page == phys_page && g_cache[j].guest_pc == guest_pc &&
		    g_cache[j].msr_ir == msr_ir && g_cache[j].endian == endian) {
			if (n_out)
				*n_out = g_cache[j].n;
			return g_cache[j].fn;
		}
	}
	return NULL;
}

void nw_jit_cache_put(uint32_t phys_page, uint32_t guest_pc, uint32_t msr_ir,
		      uint32_t endian, nw_jit_fn fn, int n)
{
	int i = cache_slot(phys_page, guest_pc, msr_ir, endian);
	int slot = i;
	for (int p = 0; p < NW_JIT_PROBE; p++) {
		int j = (i + p) & (NW_JIT_CACHE - 1);
		if (!g_cache[j].used ||
		    (g_cache[j].phys_page == phys_page && g_cache[j].guest_pc == guest_pc &&
		     g_cache[j].msr_ir == msr_ir && g_cache[j].endian == endian)) {
			slot = j;
			break;
		}
	}
	g_cache[slot].phys_page = phys_page;
	g_cache[slot].guest_pc = guest_pc;
	g_cache[slot].msr_ir = msr_ir;
	g_cache[slot].endian = endian;
	g_cache[slot].fn = fn;
	g_cache[slot].used = 1;
	g_cache[slot].n = (uint8_t)(n < 0 ? 0 : n > 255 ? 255 : n);
	pagebit_set(phys_page);
}

uint32_t nw_ppc_addi(int rd, int ra, int simm)
{
	return (14u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) | ((uint32_t)simm & 0xffffu);
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

uint32_t nw_ppc_subfco(int rd, int ra, int rb, int rc)
{
	return (31u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (1u << 10) | (8u << 1) | (rc ? 1u : 0);
}

uint32_t nw_ppc_cmpli(int crfd, int ra, unsigned uimm)
{
	return (10u << 26) | ((uint32_t)(crfd & 7) << 23) | ((uint32_t)ra << 16) |
	       (uimm & 0xffffu);
}

uint32_t nw_ppc_mtcrf(int crm, int rs)
{
	return (31u << 26) | ((uint32_t)rs << 21) | (((uint32_t)crm & 0xffu) << 12) |
	       (144u << 1);
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

uint32_t nw_ppc_rlwimi(int ra, int rs, int sh, int mb, int me)
{
	return (20u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)sh << 11) | ((uint32_t)mb << 6) | ((uint32_t)me << 1);
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

uint32_t nw_ppc_stb(int rs, int ra, int d)
{
	return (38u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) | ((uint32_t)d & 0xffffu);
}

uint32_t nw_ppc_stw(int rs, int ra, int d)
{
	return (36u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) | ((uint32_t)d & 0xffffu);
}

uint32_t nw_ppc_stwu(int rs, int ra, int d)
{
	return (37u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) | ((uint32_t)d & 0xffffu);
}

uint32_t nw_ppc_lwzx(int rd, int ra, int rb)
{
	return (31u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) |
	       ((uint32_t)rb << 11) | (23u << 1);
}

uint32_t nw_ppc_lha(int rd, int ra, int d)
{
	return (42u << 26) | ((uint32_t)rd << 21) | ((uint32_t)ra << 16) | ((uint32_t)d & 0xffffu);
}

uint32_t nw_ppc_sth(int rs, int ra, int d)
{
	return (44u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) | ((uint32_t)d & 0xffffu);
}

uint32_t nw_ppc_cmp(int ra, int rb)
{
	return 0x7c000000u | ((uint32_t)ra << 16) | ((uint32_t)rb << 11);
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

uint32_t nw_ppc_ori(int ra, int rs, unsigned uimm)
{
	return (24u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
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

static void record_cr0_cmp(struct nw_jit_cpu *cpu, int32_t a, int32_t b)
{
	record_cr_s(cpu, 0, a, b);
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
		nw_jit_dtlb_fill(ea, ea, 1,
			(uint64_t)(uintptr_t)(cpu->mem + ((ea - cpu->mem_base) & ~0xfffu)));
		g_dtlb_miss++;
		return mem_ld_be(cpu, ea);
	}
	if (g_host_lwz && cpu->host) {
		int f = 0;
		uint32_t v = g_host_lwz(cpu->host, ea, cpu->pc, &f);
		if (f) {
			cpu->fault = f;
			cpu->fault_ea = ea;
		} else
			g_dtlb_miss++;
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
		nw_jit_dtlb_fill(ea, ea, 1,
			(uint64_t)(uintptr_t)(cpu->mem + ((ea - cpu->mem_base) & ~0xfffu)));
		g_dtlb_miss++;
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
		int f = 0;
		g_host_stw(cpu->host, ea, val, cpu->pc, &f);
		if (f) {
			cpu->fault = f;
			cpu->fault_ea = ea;
			cpu->fault_st = 1;
		} else
			g_dtlb_miss++;
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
	if (prim == 12 || prim == 13) {
		const uint32_t a = cpu->gpr[ra], b = (uint32_t)simm;
		record_ca(cpu, a, b);
		cpu->gpr[rd] = a + b;
		if (prim == 13)
			record_cr0(cpu, (int32_t)cpu->gpr[rd]);
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
	if (prim == 31 && xo == 144) {
		const uint32_t m = mtcrf_mask(op);
		cpu->cr = (cpu->gpr[rd] & m) | (cpu->cr & ~m);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 16) {
		const int bo = rd, bi = ra;
		const int32_t disp = (int16_t)(op & 0xfffcu);
		const int crbit = (int)((cpu->cr >> (31 - bi)) & 1);
		int take = 0;
		if (!bo_is_cr(bo))
			return -1;
		take = ((bo & ~1) == NW_PPC_BO_TRUE) ? crbit : !crbit;
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
	if (prim == 19 && (xo == 16 || xo == 528)) {
		int take = 1;
		if (rd != 20) {
			if (!bo_is_cr(rd))
				return -1;
			const int crbit = (int)((cpu->cr >> (31 - ra)) & 1);
			take = ((rd & ~1) == NW_PPC_BO_TRUE) ? crbit : !crbit;
		}
		const uint32_t t = (xo == 16) ? cpu->lr : cpu->ctr;
		if (op & 1)
			cpu->lr = pc + 4;
		if (take) {
			cpu->pc = t & ~3u;
			return 1;
		}
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 24) {
		cpu->gpr[ra] = cpu->gpr[rd] | (op & 0xffffu);
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
	if (prim == 31 && xo == 266) {
		cpu->gpr[rd] = cpu->gpr[ra] + cpu->gpr[rb];
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
	if (prim == 31 && xo == 444) {
		cpu->gpr[ra] = cpu->gpr[rd] | cpu->gpr[rb];
		if (op & 1)
			record_cr0(cpu, (int32_t)cpu->gpr[ra]);
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 31 && xo == 0) {
		if (rd != 0)
			return -1;
		record_cr0_cmp(cpu, (int32_t)cpu->gpr[ra], (int32_t)cpu->gpr[rb]);
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
		else
			return -1;
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
		else
			return -1;
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
	if (prim == 34) {
		const uint32_t ea = ra_or_0(cpu, ra) + (uint32_t)simm;
		if (!mem_ok_n(cpu, ea, 1))
			return -1;
		cpu->gpr[rd] = cpu->mem[ea - cpu->mem_base];
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 38) {
		const uint32_t ea = ra_or_0(cpu, ra) + (uint32_t)simm;
		if (!mem_ok_n(cpu, ea, 1))
			return -1;
		cpu->mem[ea - cpu->mem_base] = (uint8_t)cpu->gpr[rd];
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
	if (prim == 40 || prim == 42 || prim == 43) {
		const uint32_t ea = ra_or_0(cpu, ra) + (uint32_t)simm;
		uint16_t h;
		if (cpu->mem) {
			if (!mem_ok_n(cpu, ea, 2))
				return -1;
			const uint8_t *p = cpu->mem + (ea - cpu->mem_base);
			h = (uint16_t)(((uint32_t)p[0] << 8) | p[1]);
		} else
			return -1;
		uint32_t v = (prim == 40) ? h : (uint32_t)(int16_t)h;
		cpu->gpr[rd] = v;
		if (prim == 43 && ra)
			cpu->gpr[ra] = ea;
		cpu->pc = pc + 4;
		return 0;
	}
	if (prim == 44) {
		const uint32_t ea = ra_or_0(cpu, ra) + (uint32_t)simm;
		if (!mem_ok_n(cpu, ea, 2))
			return -1;
		uint8_t *p = cpu->mem + (ea - cpu->mem_base);
		p[0] = (uint8_t)(cpu->gpr[rd] >> 8);
		p[1] = (uint8_t)cpu->gpr[rd];
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

enum {
	W0 = 0, W1 = 1, W2 = 2, W8 = 8, W9 = 9, W10 = 10, W12 = 12, W13 = 13,
	X0 = 0, X9 = 9, X10 = 10, X11 = 11, X12 = 12, X13 = 13, X19 = 19
};

struct emit {
	uint32_t *p;
	uint32_t *end;
	uint32_t *fault_br[NW_JIT_MAX_BLOCK];
	int nfault;
};

static int emit_imm32(struct emit *e, int rd, uint32_t v);
static int emit_imm64(struct emit *e, int xd, uint64_t v);

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

static uint32_t a64_adds_reg(int rd, int rn, int rm)
{
	return 0x2b000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_subs_reg(int rd, int rn, int rm)
{
	return 0x6b000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
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

static uint32_t a64_and_imm8(int rd, int rn)
{
	return 0x12001c00u | ((uint32_t)rn << 5) | (uint32_t)rd;
}

/* W8 = EA. Hit: helper_pa(cpu, pa). Miss: helper_ea(cpu, ea). W2 preserved. */
static int emit_dtlb_and_helpers(struct emit *e, int is_store,
				 void *miss_fn, void *hit_fn)
{
	if (!emit_w(e, a64_ldr_w(W12, X19, (uint32_t)offsetof(struct nw_jit_cpu, msr))))
		return 0;
	uint32_t *dr_off = e->p;
	if (!emit_w(e, a64_tbz(W12, 4, 0)))
		return 0;
	if (!emit_imm64(e, X10, (uint64_t)(uintptr_t)g_dtlb))
		return 0;
	if (!emit_w(e, a64_lsr(W9, W8, 12)))
		return 0;
	if (!emit_w(e, a64_and_imm8(W9, W9)))
		return 0;
	if (!emit_w(e, a64_add_x_lsl(X11, X10, 9, 5)))
		return 0;
	if (!emit_w(e, a64_ldr_w(W12, X11, 0)))
		return 0;
	if (!emit_imm32(e, W13, ~0xfffu))
		return 0;
	if (!emit_w(e, a64_and_reg(W13, W8, W13)))
		return 0;
	if (!emit_w(e, a64_cmp_w(W12, W13)))
		return 0;
	uint32_t *tag_ne = e->p;
	if (!emit_w(e, a64_b_cond(1, 0)))
		return 0;
	if (!emit_w(e, a64_ldr_w(W12, X11, 8)))
		return 0;
	uint32_t *nv = e->p;
	if (!emit_w(e, a64_tbz(W12, 0, 0)))
		return 0;
	uint32_t *nw = NULL;
	if (is_store) {
		nw = e->p;
		if (!emit_w(e, a64_tbz(W12, 1, 0)))
			return 0;
	}
	if (!emit_w(e, a64_ldr_w(W12, X11, 4)))
		return 0;
	if (!emit_imm32(e, W1, 0xfffu))
		return 0;
	if (!emit_w(e, a64_and_reg(W1, W8, W1)))
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
	if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)miss_fn))
		return 0;
	if (!emit_w(e, 0xd63f0120u))
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
	if (!emit_imm32(e, W13, 0xfffu))
		return 0;
	if (!emit_w(e, a64_and_reg(W13, W8, W13)))
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
	if (!emit_imm64(e, X10, (uint64_t)(uintptr_t)&g_dtlb_hit))
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
	if (!emit_imm64(e, X9, (uint64_t)(uintptr_t)hit_fn))
		return 0;
	if (!emit_w(e, 0xd63f0120u))
		return 0;
	uint32_t *join = e->p;
	*dr_off = a64_tbz(W12, 4, (int)(ident_p - dr_off));
	*tag_ne = a64_b_cond(1, (int)(miss_p - tag_ne));
	*nv = a64_tbz(W12, 0, (int)(miss_p - nv));
	if (nw)
		*nw = a64_tbz(W12, 1, (int)(miss_p - nw));
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
	if (!emit_w(e, 0xa9bf7bfdu))		/* stp x29, x30, [sp, #-32]! */
		return 0;
	if (!emit_w(e, 0xf9000bf3u))		/* str x19, [sp, #16] */
		return 0;
	return emit_w(e, 0xaa0003f3u);		/* mov x19, x0 */
}

static int emit_ret(struct emit *e)
{
	if (!emit_w(e, 0xf9400bf3u))		/* ldr x19, [sp, #16] */
		return 0;
	if (!emit_w(e, 0xa8c17bfdu))		/* ldp x29, x30, [sp], #32 */
		return 0;
	return emit_w(e, 0xd65f03c0u);		/* ret */
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
	if (!emit_dtlb_and_helpers(e, 0, (void *)nw_jit_helper_lwz,
				   (void *)nw_jit_helper_lwz_pa))
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

static int emit_call_lb(struct emit *e, uint32_t pc, int rd, int ra, int simm)
{
	if (!emit_set_pc(e, pc))
		return 0;
	if (!emit_helper_ea(e, ra, simm))
		return 0;
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
	return emit_fault_check(e);
}

static int emit_call_stb(struct emit *e, uint32_t pc, int rs, int ra, int simm)
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
	return emit_fault_check(e);
}

static int emit_helper_ea_idx(struct emit *e, int ra, int rb)
{
	if (!emit_ra_or_0(e, W8, ra))
		return 0;
	if (!emit_load_gpr(e, W9, rb))
		return 0;
	return emit_w(e, a64_add_reg(W8, W8, W9));
}

static int emit_call_stw(struct emit *e, uint32_t pc, int rs, int ra, int simm, int upd)
{
	if (!emit_set_pc(e, pc))
		return 0;
	if (!emit_helper_ea(e, ra, simm))
		return 0;
	if (!emit_load_gpr(e, W2, rs))			/* w2 = value */
		return 0;
	if (!emit_dtlb_and_helpers(e, 1, (void *)nw_jit_helper_stw,
				   (void *)nw_jit_helper_stw_pa))
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

static int emit_call_lwzx(struct emit *e, uint32_t pc, int rd, int ra, int rb)
{
	if (!emit_set_pc(e, pc))
		return 0;
	if (!emit_helper_ea_idx(e, ra, rb))
		return 0;
	if (!emit_dtlb_and_helpers(e, 0, (void *)nw_jit_helper_lwz,
				   (void *)nw_jit_helper_lwz_pa))
		return 0;
	if (!emit_w(e, a64_orr_reg(W9, 31, W0)))	/* value; fault_check uses W8 */
		return 0;
	if (!emit_w(e, 0xaa1303e0u))
		return 0;
	if (!emit_fault_check(e))
		return 0;
	return emit_store_gpr(e, W9, rd);
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

static int emit_call_sth(struct emit *e, uint32_t pc, int rs, int ra, int simm)
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
	return emit_fault_check(e);
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

/* Signed compare of W8 vs W9. B.LT is N!=V, not N (B.MI). vs-kpx 7c13a000. */
static int emit_cr0_from_cmp_w8_w9(struct emit *e)
{
	if (!emit_w(e, 0x6b09011fu))			/* SUBS WZR, W8, W9 */
		return 0;
	return emit_cr0_from_flags(e, 0x5400008bu);	/* B.LT +4 */
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
	if (prim == 24) {
		if (!emit_load_gpr(e, W8, rd))
			return 0;
		if (!emit_imm32(e, W9, op & 0xffffu))
			return 0;
		if (!emit_w(e, a64_orr_reg(W8, W8, W9)))
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
		if (!bo_is_cr(bo))
			return 0;
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
	if (prim == 19 && (xo == 16 || xo == 528)) {
		int always = (rd == 20);
		const uint32_t spr_off = (xo == 16)
			? (uint32_t)offsetof(struct nw_jit_cpu, lr)
			: (uint32_t)offsetof(struct nw_jit_cpu, ctr);
		if (!always && !bo_is_cr(rd))
			return 0;
		if (!always) {
			if (!emit_w(e, a64_ldr_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, cr))))
				return 0;
			if (!emit_w(e, a64_lsr(W8, W8, 31 - ra)))
				return 0;
			if (!emit_w(e, a64_and_imm1(W8, W8)))
				return 0;
			uint32_t *cb = e->p;
			if (!emit_w(e, a64_cbz(W8, 0)))
				return 0;
			if (!emit_w(e, a64_ldr_w(W8, X0, spr_off)))
				return 0;
			if (op & 1) {
				if (!emit_imm32(e, W9, pc + 4))
					return 0;
				if (!emit_w(e, a64_str_w(W9, X0, (uint32_t)offsetof(struct nw_jit_cpu, lr))))
					return 0;
			}
			if (!emit_imm32(e, W9, ~3u))
				return 0;
			if (!emit_w(e, a64_and_reg(W8, W8, W9)))
				return 0;
			if (!emit_w(e, a64_str_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, pc))))
				return 0;
			if (!emit_ret(e))
				return 0;
			int32_t off = (int32_t)(e->p - cb);
			*cb = ((rd & ~1) == NW_PPC_BO_TRUE) ? a64_cbz(W8, off) : a64_cbnz(W8, off);
			if (is_last)
				return emit_set_pc(e, pc + 4);
			return 1;
		}
		if (!emit_w(e, a64_ldr_w(W8, X0, spr_off)))
			return 0;
		if (op & 1) {
			if (!emit_imm32(e, W9, pc + 4))
				return 0;
			if (!emit_w(e, a64_str_w(W9, X0, (uint32_t)offsetof(struct nw_jit_cpu, lr))))
				return 0;
		}
		if (!emit_imm32(e, W9, ~3u))
			return 0;
		if (!emit_w(e, a64_and_reg(W8, W8, W9)))
			return 0;
		if (!emit_w(e, a64_str_w(W8, X0, (uint32_t)offsetof(struct nw_jit_cpu, pc))))
			return 0;
		if (!emit_ret(e))
			return 0;
		if (!always && is_last)
			return emit_set_pc(e, pc + 4);
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
	if (prim == 31 && xo == 0) {
		if (rd != 0)
			return 0;
		if (!emit_load_gpr(e, W8, ra))
			return 0;
		if (!emit_load_gpr(e, W9, rb))
			return 0;
		return emit_cr0_from_cmp_w8_w9(e);
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
		else
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
	if (prim == 34) {
		return emit_call_lb(e, pc, rd, ra, simm);
	}
	if (prim == 38) {
		return emit_call_stb(e, pc, rd, ra, simm);
	}
	if (prim == 36 || prim == 37) {
		return emit_call_stw(e, pc, rd, ra, simm, prim == 37);
	}
	if (prim == 31 && xo == 23) {
		return emit_call_lwzx(e, pc, rd, ra, rb);
	}
	if (prim == 40 || prim == 42 || prim == 43) {
		return emit_call_lh(e, pc, rd, ra, simm, prim != 40, prim == 43);
	}
	if (prim == 44) {
		return emit_call_sth(e, pc, rd, ra, simm);
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

static nw_jit_fn compile_block(const uint32_t *ops, int n, uint32_t guest_pc)
{
	if (!code_ready() || n <= 0)
		return NULL;
	if (g_code_used + 8192 > NW_JIT_CODE_SIZE) {
		g_code_used = 0;
		nw_jit_invalidate_all_src(NW_JIT_FL_WRAP);
	}
	g_compiles++;
#ifdef __APPLE__
	pthread_jit_write_protect_np(0);
#endif
	struct emit e;
	e.p = (uint32_t *)(g_code + g_code_used);
	e.end = (uint32_t *)(g_code + NW_JIT_CODE_SIZE);
	e.nfault = 0;
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
		if (!emit_ret(&e)) {
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
	int cached_n = 0;
	nw_jit_fn hit = nw_jit_cache_get(phys_page, guest_pc, msr_ir, endian, &cached_n);
	if (hit && hit != NW_JIT_INTERPRET && cached_n == n)
		return hit;
	nw_jit_fn fn = compile_block(ops, n, guest_pc);
	if (!fn)
		return NULL;
	nw_jit_cache_put(phys_page, guest_pc, msr_ir, endian, fn, n);
	return fn;
}

#else

nw_jit_fn nw_jit_compile(const uint32_t *, int, uint32_t, uint32_t, uint32_t, uint32_t)
{
	return NULL;
}

#endif
