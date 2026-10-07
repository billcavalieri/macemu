#include "nw_68k_jit.h"
#include "nw_log.h"
#include "nw_68k_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>

void nw_log_mkdir(const char *dir)
{
	if (!dir || strlen(dir) >= 512) return;
	char path[512]; strcpy(path, dir);
	for (size_t i = 1; path[i]; ++i) if (path[i] == '/') {
		path[i] = 0; if (mkdir(path, 0755) && errno != EEXIST) return; path[i] = '/';
	}
	mkdir(path, 0755);
}
namespace {
uint64_t op_count[65536], op_native[65536], op_exit[65536], total, native_count, fallback_count, dropped, verified;
uint64_t reasons[16];
struct pc_sample { uint32_t pc; uint64_t n; } pcs[4096];
bool registered;
uint32_t measured_first, measured_end;
uint64_t measured_native;
int enabled()
{
	static int on = -1;
	if (on < 0) {
		const char *e = getenv("NW_JIT68K_HIST");
		on = e ? strcmp(e, "1") == 0 || strcmp(e, "on") == 0 : 0;
	}
	return on;
}
void write_hist()
{
	if (!enabled()) return;
	char path[512], tmp[528];
	const char *e = getenv("NW_JIT68K_HIST_PATH");
	if (e && *e) snprintf(path, sizeof path, "%s", e);
	else {
		const char *home = getenv("HOME"); if (!home) return;
		snprintf(path, sizeof path, "%s/Library/Logs/SheepShaver", home); nw_log_mkdir(path);
		snprintf(path, sizeof path, "%s/Library/Logs/SheepShaver/jit68k-hist.txt", home);
	}
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	FILE *f = fopen(tmp, "w"); if (!f) return;
	nw68_cache_stats cache = nw68_cache_statistics();
	fprintf(f, "canonical_dispatches %llu native %llu nanokernel %llu pc_samples_dropped %llu\n",
		(unsigned long long)total, (unsigned long long)native_count,
		(unsigned long long)fallback_count, (unsigned long long)dropped);
	fprintf(f, "verified_dispatches %llu (NanoKernel fused sequences may span multiple instructions)\n", (unsigned long long)verified);
	fprintf(f, "compiled %llu cache_hits %llu invalidated %llu arena_recycles %llu\n",
		(unsigned long long)cache.compiled, (unsigned long long)cache.hits,
		(unsigned long long)cache.invalidated, (unsigned long long)cache.recycled);
	fprintf(f, "compiled_blocks %llu native_block_instructions %llu\n",
		(unsigned long long)cache.blocks, (unsigned long long)cache.block_instructions);
	fprintf(f, "decoded_block_hits %llu\n", (unsigned long long)cache.decoded_hits);
	for (unsigned i = 0; i < 16; ++i) if (reasons[i]) fprintf(f, "exit %u count %llu\n", i, (unsigned long long)reasons[i]);
	unsigned top[16] = {}, n = 0;
	for (unsigned op = 0; op < 65536; ++op) {
		if (!op_count[op]) continue;
		unsigned pos = n; while (pos && op_count[op] > op_count[top[pos - 1]]) --pos;
		if (pos >= 16) continue;
		for (unsigned j = n < 16 ? n : 15; j > pos; --j) top[j] = top[j - 1];
		top[pos] = op; if (n < 16) ++n;
	}
	for (unsigned j = 0; j < n; ++j) fprintf(f, "op %04x n=%llu\n", top[j], (unsigned long long)op_count[top[j]]);
	for (unsigned op = 0; op < 65536; ++op) if (op_count[op])
		fprintf(f, "coverage op=%04x dispatches=%llu native=%llu nanokernel=%llu\n", op,
			(unsigned long long)op_count[op], (unsigned long long)op_native[op], (unsigned long long)op_exit[op]);
	for (unsigned j = 0; j < 4096; ++j) if (pcs[j].n) fprintf(f, "pc %08x n=%llu\n", pcs[j].pc, (unsigned long long)pcs[j].n);
	if (fclose(f) == 0) rename(tmp, path);
}
}
bool nw_68k_hist_enabled() { return enabled() != 0; }
void nw_68k_hist_note(uint32_t ppc_pc, uint32_t opcode_pc)
{
	if (!enabled() || ppc_pc < 0x68080000u || ppc_pc >= 0x68100000u || (ppc_pc & 7)) return;
	if (!registered) { registered = true; atexit(write_hist); }
	++op_count[(ppc_pc - 0x68080000u) >> 3]; ++total;
	const unsigned h = (opcode_pc >> 1) & 4095;
	bool recorded = false;
	for (unsigned n = 0; n < 16; ++n) {
		pc_sample &s = pcs[(h + n) & 4095];
		if (!s.n || s.pc == opcode_pc) { s.pc = opcode_pc; ++s.n; recorded = true; break; }
	}
	if (!recorded) ++dropped;
	if ((total & 0xfffffu) == 0) {
		static time_t last; const time_t now = time(0);
		if (now - last >= 5) { last = now; write_hist(); }
	}
}
void nw_68k_note_exit(uint16_t op, unsigned reason)
{
	if (reason == NW68_EXIT_NATIVE) { ++native_count; ++op_native[op]; }
	else { ++fallback_count; ++op_exit[op]; }
	if (reason == NW68_EXIT_VERIFIED) ++verified;
	if (reason < 16) ++reasons[reason];
}
uint64_t nw_68k_fallback_count() { return fallback_count; }
void nw_68k_note_exit_at(uint16_t op, unsigned reason, uint32_t pc)
{
	nw_68k_note_exit(op, reason);
	if (reason == NW68_EXIT_NATIVE && pc >= measured_first && pc < measured_end) ++measured_native;
}
void nw_68k_measure_begin(uint32_t first, uint32_t end)
{
	measured_first = first; measured_end = end; measured_native = 0;
}
uint64_t nw_68k_measure_end()
{
	measured_first = measured_end = 0;
	return measured_native;
}
uint64_t nw_68k_native_count() { return native_count; }
void nw_68k_op_summary()
{
	if (native_count || fallback_count)
		NW_DIAG("NW-BOOT G1: jit68k native=%llu nanokernel=%llu\n", (unsigned long long)native_count, (unsigned long long)fallback_count);
	write_hist();
}
