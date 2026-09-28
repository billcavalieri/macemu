/*
 *  nw_68k_jit.cpp - Phase 0 histogram and Phase 1 fallback-only skeleton
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 *
 *  Memory goes through the guest MMU. The cache key is
 *  (phys_page, 68k pc, SR.S). The body of every entry is one NK insn.
 */

#include "nw_68k_jit.h"
#include "nw_boot_contract.h"
#include "cpu/ppc/ppc-cpu.hpp"
#include "cpu_emulation.h"
#include "prefs.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#ifdef __APPLE__
#include <pthread.h>
#endif

enum { NW68_TABLE = 0x68080000u, NW68_TABLE_END = 0x68100000u };
enum { NW68_CACHE = 4096, NW68_TOP = 16 };

static int in_table(uint32_t pc)
{
	return pc >= NW68_TABLE && pc < NW68_TABLE_END;
}

void nw_log_mkdir(const char *dir)
{
	char buf[512];
	size_t n;
	if (!dir || !dir[0])
		return;
	n = strlen(dir);
	if (n >= sizeof(buf))
		return;
	memcpy(buf, dir, n + 1);
	for (size_t i = 1; i < n; i++) {
		if (buf[i] != '/')
			continue;
		buf[i] = 0;
		if (mkdir(buf, 0755) != 0 && errno != EEXIST)
			return;
		buf[i] = '/';
	}
	if (mkdir(buf, 0755) != 0 && errno != EEXIST)
		return;
}

static int hist_on(void)
{
	static int on = -1;
	if (on < 0) {
		const char *e = getenv("NW_JIT68K_HIST");
		if (e && (strcmp(e, "1") == 0 || strcmp(e, "on") == 0))
			on = 1;
		else if (e && (strcmp(e, "0") == 0 || strcmp(e, "off") == 0))
			on = 0;
		else
			on = PrefsFindBool("jit68k_host") ? 1 : 0;
	}
	return on;
}

static uint64_t g_op_n[65536];
static uint64_t g_op_tot;
static uint64_t g_fallback_n;
static uint64_t g_m68_n[65536];
static struct { uint32_t pc; uint64_t n; } g_pc[1024];
static int g_hist_atexit;

static void hist_write(void)
{
	char path[512];
	const char *env = getenv("NW_JIT68K_HIST_PATH");
	if (env && env[0])
		snprintf(path, sizeof(path), "%s", env);
	else {
		const char *home = getenv("HOME");
		if (!home || !home[0])
			return;
		snprintf(path, sizeof(path), "%s/Library/Logs/SheepShaver", home);
		nw_log_mkdir(path);
		snprintf(path, sizeof(path), "%s/Library/Logs/SheepShaver/jit68k-hist.txt", home);
	}
	FILE *f = fopen(path, "w");
	if (!f)
		return;
	fprintf(f, "hits %llu fallback %llu\n",
		(unsigned long long)g_op_tot,
		(unsigned long long)g_fallback_n);
	int top_i[NW68_TOP];
	int ntop = 0;
	for (int op = 0; op < 65536; op++) {
		if (!g_op_n[op])
			continue;
		int k = ntop;
		while (k > 0 && g_op_n[op] > g_op_n[top_i[k - 1]])
			k--;
		if (k >= NW68_TOP)
			continue;
		int n = ntop < NW68_TOP ? ntop : NW68_TOP - 1;
		for (int j = n; j > k; j--)
			top_i[j] = top_i[j - 1];
		top_i[k] = op;
		if (ntop < NW68_TOP)
			ntop++;
	}
	uint64_t top_sum = 0;
	for (int i = 0; i < ntop; i++)
		top_sum += g_op_n[top_i[i]];
	const unsigned pct = g_op_tot ? (unsigned)((top_sum * 100ull) / g_op_tot) : 0;
	fprintf(f, "top %d opcodes %u%% of hits\n", ntop, pct);
	for (int i = 0; i < ntop; i++) {
		const int op = top_i[i];
		const unsigned opct = g_op_tot ? (unsigned)((g_op_n[op] * 1000ull) / g_op_tot) : 0;
		fprintf(f, "op %04x n=%llu %u/1000\n", op,
			(unsigned long long)g_op_n[op], opct);
	}
	int pt[NW68_TOP];
	int npt = 0;
	for (int i = 0; i < 1024; i++) {
		if (!g_pc[i].n)
			continue;
		int k = npt;
		while (k > 0 && g_pc[i].n > g_pc[pt[k - 1]].n)
			k--;
		if (k >= NW68_TOP)
			continue;
		int n = npt < NW68_TOP ? npt : NW68_TOP - 1;
		for (int j = n; j > k; j--)
			pt[j] = pt[j - 1];
		pt[k] = i;
		if (npt < NW68_TOP)
			npt++;
	}
	uint64_t pc_sum = 0;
	for (int i = 0; i < npt; i++)
		pc_sum += g_pc[pt[i]].n;
	const unsigned pc_pct = g_op_tot ? (unsigned)((pc_sum * 100ull) / g_op_tot) : 0;
	fprintf(f, "top %d pcs %u%% of hits\n", npt, pc_pct);
	for (int i = 0; i < npt; i++)
		fprintf(f, "pc %08x n=%llu\n", g_pc[pt[i]].pc,
			(unsigned long long)g_pc[pt[i]].n);
	{
		int mt[NW68_TOP];
		int nmt = 0;
		for (int op = 0; op < 65536; op++) {
			if (!g_m68_n[op])
				continue;
			int k = nmt;
			while (k > 0 && g_m68_n[op] > g_m68_n[mt[k - 1]])
				k--;
			if (k >= NW68_TOP)
				continue;
			int n = nmt < NW68_TOP ? nmt : NW68_TOP - 1;
			for (int j = n; j > k; j--)
				mt[j] = mt[j - 1];
			mt[k] = op;
			if (nmt < NW68_TOP)
				nmt++;
		}
		fprintf(f, "m68 top %d\n", nmt);
		for (int i = 0; i < nmt; i++)
			fprintf(f, "m68 %04x n=%llu\n", mt[i],
				(unsigned long long)g_m68_n[mt[i]]);
	}
	fclose(f);
}

static void m68_note(powerpc_cpu *cpu)
{
	const uint32_t pc = cpu->gpr(24);
	if (pc < 2)
		return;
	const uint32_t op = ReadMacInt16(pc - 2) & 0xffffu;
	g_m68_n[op]++;
	if (!g_hist_atexit) {
		g_hist_atexit = 1;
		atexit(hist_write);
	}
}

void nw_68k_op_summary(void)
{
	int mt[8];
	int nmt = 0;
	for (int op = 0; op < 65536; op++) {
		if (!g_m68_n[op])
			continue;
		int k = nmt;
		while (k > 0 && g_m68_n[op] > g_m68_n[mt[k - 1]])
			k--;
		if (k >= 8)
			continue;
		int n = nmt < 8 ? nmt : 7;
		for (int j = n; j > k; j--)
			mt[j] = mt[j - 1];
		mt[k] = op;
		if (nmt < 8)
			nmt++;
	}
	if (!nmt)
		return;
	printf("NW-BOOT G1: jit68k op");
	for (int i = 0; i < nmt; i++)
		printf(" %04x=%llu", mt[i], (unsigned long long)g_m68_n[mt[i]]);
	printf("\n");
	hist_write();
}

void nw_68k_hist_note(uint32_t ppc_pc, uint32_t pc68)
{
	if (!hist_on() || !in_table(ppc_pc))
		return;
	if (!g_hist_atexit) {
		g_hist_atexit = 1;
		atexit(hist_write);
	}
	const uint32_t op = (ppc_pc - NW68_TABLE) >> 3;
	if (op < 65536u) {
		g_op_n[op]++;
		g_op_tot++;
	}
	const unsigned h = (pc68 >> 1) & 1023u;
	for (int n = 0; n < 8; n++) {
		const unsigned i = (h + (unsigned)n) & 1023u;
		if (g_pc[i].n == 0 || g_pc[i].pc == pc68) {
			g_pc[i].pc = pc68;
			g_pc[i].n++;
			break;
		}
	}
}

static int g_armed, g_arm_depth, g_left, g_took, g_active;
static uint32_t g_stop_pc;

void nw_68k_arm_one(void)
{
	g_armed = 1;
	g_arm_depth = -1;
	g_left = 0;
	g_took = 0;
}

int nw_68k_stop_one(uint32_t ppc_pc, int depth)
{
	if (!g_armed)
		return 0;
	if (g_arm_depth < 0)
		g_arm_depth = depth;
	if (depth != g_arm_depth)
		return 0;
	if (!in_table(ppc_pc)) {
		g_left = 1;
		return 0;
	}
	if (!g_left)
		return 0;
	g_armed = 0;
	g_left = 0;
	g_took = 1;
	g_stop_pc = ppc_pc;
	return 1;
}

int nw_68k_took_one(void)
{
	const int t = g_took;
	g_took = 0;
	g_armed = 0;
	return t;
}

struct nw_68k_ent {
	uint32_t page, pc, sr_s;
	void (*fn)(powerpc_cpu *);
	uint8_t used;
};

static struct nw_68k_ent g_cache[NW68_CACHE];
static uint8_t *g_code;
static uint32_t g_entry;

static void sync_pull(powerpc_cpu *cpu, uint32_t *d, uint32_t *a, uint32_t *pc, uint32_t *sr)
{
	for (int i = 0; i < 8; i++)
		d[i] = cpu->gpr(8 + i);
	for (int i = 0; i < 7; i++)
		a[i] = cpu->gpr(16 + i);
	*pc = cpu->gpr(24);
	*sr = cpu->gpr(25);
}

static void sync_push(powerpc_cpu *cpu, const uint32_t *d, const uint32_t *a, uint32_t pc, uint32_t sr)
{
	for (int i = 0; i < 8; i++)
		cpu->gpr(8 + i) = d[i];
	for (int i = 0; i < 7; i++)
		cpu->gpr(16 + i) = a[i];
	cpu->gpr(24) = pc;
	cpu->gpr(25) = sr;
}

static void nw_68k_one(powerpc_cpu *cpu)
{
	uint32_t d[8], a[7], pc, sr;
	sync_pull(cpu, d, a, &pc, &sr);
	sync_push(cpu, d, a, pc, sr);
	nw_68k_arm_one();
	cpu->execute(g_entry);
	sync_pull(cpu, d, a, &pc, &sr);
	sync_push(cpu, d, a, pc, sr);
	g_fallback_n++;
}

static int emit_w(uint32_t **p, uint32_t *end, uint32_t w)
{
	if (*p >= end)
		return 0;
	*(*p)++ = w;
	return 1;
}

static int emit_imm64(uint32_t **p, uint32_t *end, int rd, uint64_t v)
{
	if (!emit_w(p, end, 0xd2800000u | ((uint32_t)(v & 0xffffu) << 5) | (uint32_t)rd))
		return 0;
	if (!emit_w(p, end, 0xf2a00000u | ((uint32_t)((v >> 16) & 0xffffu) << 5) | (uint32_t)rd))
		return 0;
	if (!emit_w(p, end, 0xf2c00000u | ((uint32_t)((v >> 32) & 0xffffu) << 5) | (uint32_t)rd))
		return 0;
	return emit_w(p, end, 0xf2e00000u | ((uint32_t)((v >> 48) & 0xffffu) << 5) | (uint32_t)rd);
}

static void (*g_stub)(powerpc_cpu *);
static uint32_t *g_emit_p, *g_emit_end;

/* Top opcodes from one Debug boot (jit68k op line). Each gets an ARM
 * stub. The stub runs the already-native handler and stops when the
 * bclr lands back in the opcode table. It does not chain that bclr. */
static const uint16_t k_ranked[] = {
	0x3007, 0x7000, 0x2078, 0x6770, 0x5247, 0x4e56, 0x4219, 0x51c8,
	0x4e75, 0x0c42, 0x2f0a
};
enum { N_RANKED = 11 };
static void (*g_ranked_fn[N_RANKED])(powerpc_cpu *);

static int ranked_index(uint32_t op)
{
	for (int i = 0; i < N_RANKED; i++)
		if (k_ranked[i] == op)
			return i;
	return -1;
}

static int code_ready(void)
{
	if (g_code)
		return 1;
	void *m = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
		       MAP_ANON | MAP_PRIVATE
#ifdef MAP_JIT
		       | MAP_JIT
#endif
		       , -1, 0);
	if (m == MAP_FAILED)
		return 0;
	g_code = (uint8_t *)m;
	g_emit_p = (uint32_t *)g_code;
	g_emit_end = g_emit_p + 1024;
	return 1;
}

static void code_seal(uint32_t *start)
{
#ifdef __APPLE__
	pthread_jit_write_protect_np(1);
#endif
	__builtin___clear_cache((char *)start, (char *)g_emit_p);
}

static int make_stub(void)
{
	if (g_stub)
		return 1;
	if (!code_ready())
		return 0;
#ifdef __APPLE__
	pthread_jit_write_protect_np(0);
#endif
	uint32_t *start = g_emit_p;
	/* stp x29, x30, [sp, #-16]!; x0 is the cpu; blr to nw_68k_one; ret. */
	if (!emit_w(&g_emit_p, g_emit_end, 0xa9bf7bfdu))
		return 0;
	if (!emit_imm64(&g_emit_p, g_emit_end, 9, (uint64_t)(uintptr_t)nw_68k_one))
		return 0;
	if (!emit_w(&g_emit_p, g_emit_end, 0xd63f0120u))
		return 0;
	if (!emit_w(&g_emit_p, g_emit_end, 0xa8c17bfdu))
		return 0;
	if (!emit_w(&g_emit_p, g_emit_end, 0xd65f03c0u))
		return 0;
	code_seal(start);
	g_stub = (void (*)(powerpc_cpu *))start;
	return 1;
}

static void nw_68k_ranked(powerpc_cpu *cpu, uint32_t op)
{
	const uint32_t entry = g_entry;
	uint32_t landed = entry;
	(void)op;
	nw_68k_arm_one();
	if (!cpu->nw_68k_jit_step(entry, &landed) || !in_table(landed)) {
		cpu->execute(landed);
		g_fallback_n++;
		return;
	}
	g_armed = 0;
	g_left = 0;
	g_took = 1;
	g_stop_pc = landed;
}

static int emit_ranked(int index, uint16_t op)
{
	if (g_ranked_fn[index])
		return 1;
	if (!code_ready())
		return 0;
#ifdef __APPLE__
	pthread_jit_write_protect_np(0);
#endif
	uint32_t *start = g_emit_p;
	/* stp; mov w1, #op; blr x9 = nw_68k_ranked; ldp; ret. */
	if (!emit_w(&g_emit_p, g_emit_end, 0xa9bf7bfdu))
		return 0;
	if (!emit_w(&g_emit_p, g_emit_end, 0x52800000u | ((uint32_t)op << 5) | 1u))
		return 0;
	if (!emit_imm64(&g_emit_p, g_emit_end, 9, (uint64_t)(uintptr_t)nw_68k_ranked))
		return 0;
	if (!emit_w(&g_emit_p, g_emit_end, 0xd63f0120u))
		return 0;
	if (!emit_w(&g_emit_p, g_emit_end, 0xa8c17bfdu))
		return 0;
	if (!emit_w(&g_emit_p, g_emit_end, 0xd65f03c0u))
		return 0;
	code_seal(start);
	g_ranked_fn[index] = (void (*)(powerpc_cpu *))start;
	return 1;
}

static void (*body_for(powerpc_cpu *cpu))(powerpc_cpu *)
{
	const uint32_t pc68 = cpu->gpr(24);
	if (pc68 >= 2) {
		const int ri = ranked_index(ReadMacInt16(pc68 - 2) & 0xffffu);
		if (ri >= 0 && emit_ranked(ri, k_ranked[ri]))
			return g_ranked_fn[ri];
	}
	if (!make_stub())
		return NULL;
	return g_stub;
}

static uint32_t sr_s_bit(powerpc_cpu *cpu)
{
	return (cpu->gpr(25) >> 13) & 1u;
}

static void (*cache_fn(powerpc_cpu *cpu))(powerpc_cpu *)
{
	const uint32_t pc68 = cpu->gpr(24);
	const uint32_t page = nw_la_to_pa(pc68) & ~0xfffu;
	const uint32_t sr = sr_s_bit(cpu);
	const unsigned h = (page ^ (pc68 << 1) ^ sr) & (NW68_CACHE - 1);
	for (int n = 0; n < 8; n++) {
		struct nw_68k_ent *e = &g_cache[(h + (unsigned)n) & (NW68_CACHE - 1)];
		if (e->used && e->page == page && e->pc == pc68 && e->sr_s == sr)
			return e->fn;
		if (!e->used) {
			void (*fn)(powerpc_cpu *) = body_for(cpu);
			if (!fn)
				return NULL;
			e->used = 1;
			e->page = page;
			e->pc = pc68;
			e->sr_s = sr;
			e->fn = fn;
			return e->fn;
		}
	}
	void (*fn)(powerpc_cpu *) = body_for(cpu);
	if (!fn)
		return NULL;
	struct nw_68k_ent *e = &g_cache[h];
	e->used = 1;
	e->page = page;
	e->pc = pc68;
	e->sr_s = sr;
	e->fn = fn;
	return e->fn;
}

uint64_t nw_68k_fallback_count(void)
{
	return g_fallback_n;
}

void nw_68k_jit_execute(powerpc_cpu *cpu, uint32_t entry)
{
	static int announced;
	if (!announced) {
		announced = 1;
		printf("NW-BOOT G1: jit68k_host on\n");
		fflush(stdout);
	}
	/* EMUL_OP from inside a handler calls Execute68k again. That nested
	 * call stays on the plain NK path so it cannot clear the outer arm. */
	if (g_active) {
		cpu->execute(entry);
		return;
	}
	g_active = 1;
	const uint32_t saved = g_entry;
	g_entry = entry;
	for (;;) {
		m68_note(cpu);
		void (*fn)(powerpc_cpu *) = cache_fn(cpu);
		if (!fn) {
			cpu->execute(g_entry);
			break;
		}
		fn(cpu);
		if (!nw_68k_took_one())
			break;
		const uint32_t next = g_stop_pc;
		if (!in_table(next))
			break;
		g_entry = next;
	}
	g_entry = saved;
	g_active = 0;
}
