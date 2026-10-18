/*
 *  sheepbench_host.cpp - Host side of SheepBench (see sheepbench_host.h and guest/sheepbench/)
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <atomic>
#include <algorithm>

#include "sysdeps.h"
#include "main.h"
#include "cpu_emulation.h"
#include "prefs.h"
#include "disk.h"
#include "cdrom.h"
#include "extfs.h"
#include "sheepforce.h"
#include "nw_jit.h"
#include "nw_68k_jit.h"
#include "sheepbench_host.h"
#include "../../guest/sheepbench/kernels/sb_kernels.h"

static int put(char *out, int cap, int used, const char *fmt, ...) __attribute__((format(printf, 4, 5)));

static int put(char *out, int cap, int used, const char *fmt, ...)
{
	if (used >= cap)
		return used;
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(out + used, cap - used, fmt, ap);
	va_end(ap);
	if (n < 0 || n >= cap - used)
		return used;		// a line that does not fit is left out whole
	return used + n;
}

/*
 *  Mode switching. The request is recorded here (on the emulation thread, inside the Shears call) and applied from
 *  HandleInterrupt, between blocks, so no generated code that is about to be thrown away is running.
 */

uint32_t sheepbench_hook_hits[8];

static std::atomic<int> g_pending(0);
static int g_req_ppc, g_req_68k, g_req_qd;
static int g_boot_ppc = -1, g_boot_68k = -1;
static bool g_override;

static void note_boot_modes(void)
{
	if (g_boot_ppc < 0) {
		g_boot_ppc = nw_jit_mode();
		g_boot_68k = nw_68k_host_mode();
	}
}

int SheepBenchRequestModes(int ppc, int m68k, int qd, int restore)
{
	note_boot_modes();
	if ((g_boot_ppc != NW_JIT_ON && g_boot_ppc != NW_JIT_OFF) || g_boot_68k > 1)
		return 1;
	if (restore) {
		ppc = g_boot_ppc == NW_JIT_ON;
		m68k = g_boot_68k == 1;
		qd = 1;
	} else if ((ppc && g_boot_ppc != NW_JIT_ON) || (m68k && g_boot_68k != 1) ||
	           (qd && !(SheepForceEnabled() && PrefsFindBool("sheepforce_qd")))) {
		return 2;	// only what was on at start-up can be switched off and back on
	}
	g_req_ppc = ppc ? 1 : 0;
	g_req_68k = m68k ? 1 : 0;
	g_req_qd = qd ? 1 : 0;
	g_pending.store(1);
	TriggerInterrupt();
	return 0;
}

void SheepBenchApplyPending(void)
{
	if (!g_pending.load(std::memory_order_relaxed) || !g_pending.exchange(0))
		return;
	nw_jit_set_mode(g_req_ppc ? NW_JIT_ON : NW_JIT_OFF);
	nw_68k_set_host_mode(g_req_68k);
	SheepForceSuspendQD(!g_req_qd);
	nw_jit_invalidate_all();
	g_override = (g_req_ppc != (g_boot_ppc == NW_JIT_ON)) || (g_req_68k != (g_boot_68k == 1)) || !g_req_qd;
	printf("SheepBench: jit %s, jit68k_host %s, SheepForce QuickDraw %s%s\n", g_req_ppc ? "on" : "off",
	       g_req_68k ? "on" : "off", g_req_qd ? "on" : "off", g_override ? "" : " (as started)");
	fflush(stdout);
}

int SheepBenchOverrideActive(void)
{
	return g_override ? 1 : 0;
}

/*
 *  Run info
 */

int SheepBenchInfoText(char *out, int cap)
{
	note_boot_modes();
	int u = 0;
	const char *jit_env = getenv("NW_JIT"), *legacy = getenv("NW_JIT_LEGACY"), *m68_env = getenv("NW_JIT68K_MODE");
	static const char *const m68_names[] = { "off", "on", "verify" };
	const int m68 = nw_68k_host_mode(), m68_boot = g_boot_68k;
	u = put(out, cap, u, "pvr=%08x\n", (unsigned)PVR);
	u = put(out, cap, u, "cpu_hz=%lld\nbus_hz=%lld\ntb_hz=%lld\n", (long long)CPUClockSpeed, (long long)BusClockSpeed,
	        (long long)TimebaseSpeed);
	u = put(out, cap, u, "ram_mb=%u\n", (unsigned)(RAMSize >> 20));
	u = put(out, cap, u, "jit=%s\njit_source=%s\n", nw_jit_mode_name(), jit_env ? "NW_JIT" : "pref");
	u = put(out, cap, u, "jit_at_start=%s\n", g_boot_ppc == NW_JIT_ON ? "on" : g_boot_ppc == NW_JIT_OFF ? "off" :
	        g_boot_ppc == NW_JIT_VERIFY ? "verify" : "fallback");
	u = put(out, cap, u, "jit68k_host=%s\njit68k_host_source=%s\njit68k_host_at_start=%s\n", m68_names[(unsigned)m68 < 3 ? m68 : 0],
	        m68_env ? "NW_JIT68K_MODE" : "pref", m68_names[(unsigned)m68_boot < 3 ? m68_boot : 0]);
	u = put(out, cap, u, "nw_jit_legacy=%s\n", legacy && *legacy ? legacy : "none");
	static const char *const sf[] = { "sheepforce", "sheepforce_qd", "sheepforce_rave", "sheepforce_lines",
	                                  "sheepforce_fillmask", "sheepforce_fillmask_tiles", "gfxaccel", "qtcodec" };
	for (const char *k : sf)
		u = put(out, cap, u, "%s=%d\n", k, PrefsFindBool(k) ? 1 : 0);
	u = put(out, cap, u, "sheepforce_qd_suspended=%d\n", SheepForceQDSuspended() ? 1 : 0);
	u = put(out, cap, u, "rave=%s\n", SheepForceRaveState());
	u = put(out, cap, u, "bench_switch=%d\n", PrefsFindBool("bench_switch") ? 1 : 0);
	u = put(out, cap, u, "switch_pending=%d\noverride=%d\n", g_pending.load() ? 1 : 0, g_override ? 1 : 0);
	u = put(out, cap, u, "qd_hits=0:%u,2:%u,3:%u,5:%u\n", (unsigned)sheepbench_hook_hits[0], (unsigned)sheepbench_hook_hits[2],
	        (unsigned)sheepbench_hook_hits[3], (unsigned)sheepbench_hook_hits[5]);
	u = put(out, cap, u, "host_cache=on\n");	// disk images are read through the host's page cache (no F_NOCACHE)
	if (u < cap)
		u += DiskDescribe(out + u, cap - u);
	if (u < cap)
		u += CDROMDescribe(out + u, cap - u);
	if (ExtFSDriveNumber())
		u = put(out, cap, u, "extfs drive=%d kind=extfs\n", ExtFSDriveNumber());
	return u;
}

/*
 *  Native kernels
 */

static uint64_t now_ns(void)
{
	return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}

static void *g_scratch;
static size_t g_scratch_size;
static volatile uint32_t g_sink;

int SheepBenchNative(uint32_t kernel, uint32_t iters, uint32_t param, uint64_t *ps_per_iter, uint32_t *checksum)
{
	if (!sb_known(kernel) || iters == 0)
		return -1;
	size_t need = sb_scratch_bytes(kernel, param);
	if (need > (size_t)64 << 20)
		return -1;
	if (need > g_scratch_size) {
		free(g_scratch);
		g_scratch = malloc(need);
		g_scratch_size = g_scratch ? need : 0;
		if (!g_scratch)
			return -1;
	}
	sb_prepare(kernel, param, g_scratch);
	*checksum = sb_run(kernel, iters, param, g_scratch);

	// Scale until one repetition takes 2 ms, then take the median of five; stop early past 50 ms in all.
	const uint64_t start = now_ns();
	uint64_t n = iters, t = 0;
	for (;;) {
		uint64_t t0 = now_ns();
		g_sink = sb_run(kernel, (uint32_t)n, param, g_scratch);
		t = now_ns() - t0;
		if (t >= 2000000 || n >= 0x40000000u || now_ns() - start > 25000000)
			break;
		n *= 2;
	}
	uint64_t times[5];
	int reps = 0;
	times[reps++] = t;
	while (reps < 5 && now_ns() - start < 50000000) {
		uint64_t t0 = now_ns();
		g_sink = sb_run(kernel, (uint32_t)n, param, g_scratch);
		times[reps++] = now_ns() - t0;
	}
	std::sort(times, times + reps);
	*ps_per_iter = times[reps / 2] * 1000 / n;
	if (getenv("NW_BENCH_CORRUPT"))	// test hook (tools/sheepbench/test.sh): the guest must report a failure
		*checksum ^= 1;
	return 0;
}
