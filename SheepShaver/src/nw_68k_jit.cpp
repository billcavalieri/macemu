/* Direct New World 68k-to-ARM64 dispatch. The NK owns service/fault
 * continuations. Native preparation has no device or memory-write effects. */
#include "nw_68k_jit.h"
#include "nw_68k_core.h"
#include "nw_jit.h"
#include "nw_io.h"
#include "cpu/ppc/ppc-cpu.hpp"
#include "prefs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include <vector>

namespace {
bool table(uint32_t pc) { return pc >= 0x68080000u && pc < 0x68100000u && !(pc & 7); }
enum mode { OFF, ON, VERIFY };
mode selected_mode()
{
	static int selected = -1;
	if (selected < 0) {
		const char *e = getenv("NW_JIT68K_MODE");
		selected = e ? (!strcmp(e, "on") ? ON : !strcmp(e, "verify") ? VERIFY : OFF) :
			PrefsFindBool("jit68k_host") ? ON : OFF;
	}
	return (mode)selected;
}
struct verification_frame;
thread_local verification_frame *reference;
bool quarantined[65536];
unsigned mismatch_traces;
struct captured_reads {
	struct byte { uint32_t key; uint8_t value; };
	std::vector<byte> bytes;
	bool stable = true;
	void add(uint32_t pa, unsigned width, uint32_t value, bool overwrite = false) {
		for (unsigned j = 0; j < width; ++j) {
			const uint8_t v = value >> ((width - j - 1) * 8);
			bool found = false;
			for (byte &b : bytes) if (b.key == pa + j) {
				if (overwrite) b.value = v; else stable &= b.value == v;
				found = true; break;
			}
			if (!found) bytes.push_back({pa + j, v});
		}
	}
	bool get(uint32_t key, uint8_t &value) const {
		for (const byte &b : bytes) if (b.key == key) { value = b.value; return true; }
		return false;
	}
};
thread_local captured_reads *active_samples;
bool plain_memory(uint32_t pa, bool store)
{
	const int k = nw_pa_kind(pa);
	return k != NW_PA_IO && k != NW_PA_NONE && k != NW_PA_FB && (!store || nw_pa_writable(pa));
}
struct bus_context {
	nw68_page_cache translations;
	powerpc_cpu *cpu; uint32_t pc; uint16_t opcode, prefetched;
	uint32_t pages[8], write_pages[32]; unsigned npages, nwrites;
	uint32_t read_addresses[64]; uint8_t read_widths[64]; unsigned nreads;
	uint32_t resolved_ea, resolved_pa; unsigned resolved_width;
	bool resolved_store, resolved_valid;
};
bool preview_page(void *opaque, uint32_t ea, bool store, uint32_t *pa)
{
	return ((bus_context *)opaque)->cpu->guest_data_probe(ea, 1, store, pa, 0, false);
}
bool record_read(bus_context &c, uint32_t ea, unsigned width)
{
	for (unsigned j = 0; j < c.nreads; ++j)
		/* PTE referenced state belongs to the logical page. Translation and
		 * protection are checked within a fixed dispatch context; one successful
		 * access per page suffices when materializing R at commit. Crossing
		 * operands separately record the second page in probe(). */
		if ((c.read_addresses[j] >> 12) == (ea >> 12)) return true;
	if (c.nreads == 64) return false;
	c.read_addresses[c.nreads] = ea; c.read_widths[c.nreads++] = width;
	return true;
}
bool probe(bus_context &c, uint32_t ea, unsigned width, bool store, uint32_t *pa)
{
	c.resolved_valid = false;
	if ((uint64_t)ea + width > (uint64_t)UINT32_MAX + 1 || (width > 1 && (ea & 1))) return false;
	uint32_t first;
	if (!nw68_page_translate(c.translations, &c, preview_page, ea, store, &first) || !plain_memory(first, store)) return false;
	/* Word-aligned long operands are supported by the NK's alignment fixup.
	 * They may cross a page: re-probe bytes on the second page and require
	 * contiguous, permitted physical memory before reading or staging.
	 * Preparation has no callbacks, context transitions or committed stores.
	 * Its page previews therefore share a fixed SR/BAT/protection context,
	 * with read/write permissions kept distinct. Still check
	 * every physical byte against host memory-bank permissions. */
	for (unsigned j = 1; j < width; ++j) {
		if (((ea + j) ^ ea) & ~4095u) {
			uint32_t next;
			if (!nw68_page_translate(c.translations, &c, preview_page, ea + j, store, &next) || next != first + j) return false;
			if (!store && !record_read(c, ea + j, 1)) return false;
		}
		if (!plain_memory(first + j, store)) return false;
	}
	/* A journaled PTE store would change translation before later accesses
	 * in the real NK. Delegate it before effects rather than previewing the
	 * rest of a block against the old hash table. */
	if (store && nw68_hash_table_overlap(first, width, ppc32_guest_mmu().sdr1())) return false;
	*pa = first;
	c.resolved_ea = ea; c.resolved_pa = first; c.resolved_width = width;
	c.resolved_store = store; c.resolved_valid = true;
	return true;
}
bool read_code(void *opaque, uint32_t ea, uint16_t *value, uint32_t *physical, bool snapshot)
{
	bus_context &c = *(bus_context *)opaque;
	if (!probe(c, ea, 2, false, physical) || !record_read(c, ea, 2)) return false;
	const uint32_t page = *physical & ~4095u;
	for (unsigned j = 0; j < c.nwrites; ++j) if (c.write_pages[j] == page) return false;
	bool found = false; for (unsigned j = 0; j < c.npages; ++j) if (c.pages[j] == page) found = true;
	if (!found) { if (c.npages == 8) return false; c.pages[c.npages++] = page; }
	/* Preserve the NK's already-prefetched snapshot. */
	*value = snapshot && ea == c.pc ? c.opcode : snapshot && ea == c.pc + 2u ? c.prefetched : vm_read_memory_2(*physical);
	return true;
}
bool code_read(void *p, uint32_t ea, uint16_t *v, uint32_t *pa) { return read_code(p, ea, v, pa, true); }
bool code_fetch(void *p, uint32_t ea, uint16_t *v, uint32_t *pa) { return read_code(p, ea, v, pa, false); }
bool resolve_address(void *p, uint32_t ea, unsigned width, bool store, uint32_t *pa) {
	bus_context &c = *(bus_context *)p;
	/* The journal asks for the byte key immediately after a plain-memory
	 * access/probe, without callbacks or a context transition in between.
	 * Reuse just that resolution; this cache never crosses a dispatch. */
	if (c.resolved_valid && c.resolved_ea == ea && c.resolved_width == width && c.resolved_store == store) {
		*pa = c.resolved_pa; return true;
	}
	return probe(c, ea, width, store, pa);
}
bool data_read(void *opaque, uint32_t ea, unsigned width, uint32_t *value)
{
	bus_context &c = *(bus_context *)opaque; uint32_t pa;
	if (!probe(c, ea, width, false, &pa) || !record_read(c, ea, width)) return false;
	*value = width == 1 ? vm_read_memory_1(pa) : width == 2 ? vm_read_memory_2(pa) : vm_read_memory_4(pa);
	if (active_samples) active_samples->add(pa, width, *value);
	return true;
}
bool write_probe(void *opaque, uint32_t ea, unsigned width)
{
	bus_context &c = *(bus_context *)opaque; uint32_t pa;
	if (!probe(c, ea, width, true, &pa)) return false;
	for (unsigned byte = 0; byte < width; ++byte) {
		const uint32_t page = (pa + byte) & ~4095u;
		for (unsigned j = 0; j < c.npages; ++j) if (c.pages[j] == page) return false;
		bool found = false;
		for (unsigned j = 0; j < c.nwrites; ++j) if (c.write_pages[j] == page) found = true;
		if (!found) {
			if (c.nwrites == 32) return false;
			c.write_pages[c.nwrites++] = page;
		}
	}
	return true;
}
void data_write(void *opaque, uint32_t ea, unsigned width, uint32_t value)
{
	bus_context &c = *(bus_context *)opaque; uint32_t pa;
	/* No callbacks/context changes occur between prepare and commit. */
	if (!c.cpu->guest_data_probe(ea, width, true, &pa)) abort();
	for (unsigned j = 1; j < width; ++j) if (((ea + j) ^ ea) & ~4095u) {
		uint32_t next;
		if (!c.cpu->guest_data_probe(ea + j, 1, true, &next) || next != pa + j) abort();
	}
	if (width == 1) vm_write_memory_1(pa, value);
	else if (width == 2) vm_write_memory_2(pa, value);
	else vm_write_memory_4(pa, value);
	nw_jit_invalidate_range_src(pa, width, NW_JIT_FL_ISTORE);
}
bool compatible_nk()
{
	const ppc32_xlate_result a = ppc32_guest_mmu().translate(0x68066000u, PPC32_XLATE_IR, 4, false, false);
	const ppc32_xlate_result b = ppc32_guest_mmu().translate(0x6806c000u, PPC32_XLATE_IR, 4, false, false);
	if (!a.ok || !b.ok || !plain_memory(a.pa, false) || !plain_memory(b.pa, false)) return false;
	/* Check current words even if handlers are patched without icbi. The
	 * mapping is uniform within each page; only two translations are needed. */
	return vm_read_memory_4(a.pa + 0x84) == 0x537d1b78u && vm_read_memory_4(a.pa + 0x88) == 0x7fa803a6u &&
		vm_read_memory_4(a.pa + 0x8c) == 0xaf780002u && vm_read_memory_4(a.pa + 0x90) == 0x4ca80020u &&
		vm_read_memory_4(b.pa + 0xd84) == 0x7cc00026u && vm_read_memory_4(b.pa + 0xd98) == 0x50c41fbeu &&
		vm_read_memory_4(b.pa + 0xdf4) == 0x7cc80120u && vm_read_memory_4(b.pa + 0xdf8) == 0x7c8103a6u;
}
bool equal_state(const nw68_state &a, const nw68_state &b)
{
	return a.pc == b.pc && a.ccr == b.ccr && a.so == b.so && a.extend_so == b.extend_so &&
		!memcmp(a.d, b.d, sizeof a.d) && !memcmp(a.a, b.a, sizeof a.a);
}
struct shadow_bus {
	bus_context *real;
	const captured_reads *samples = nullptr;
	nw68_write writes[512]; unsigned nwrite;
	static bool code(void *p, uint32_t ea, uint16_t *v, uint32_t *pa) {
		shadow_bus &b = *(shadow_bus *)p;
		return b.samples ? code_read(b.real, ea, v, pa) : code_fetch(b.real, ea, v, pa);
	}
	static bool fetch(void *p, uint32_t ea, uint16_t *v, uint32_t *pa) {
		return code_fetch(((shadow_bus *)p)->real, ea, v, pa);
	}
	static bool read(void *p, uint32_t ea, unsigned width, uint32_t *v) {
		shadow_bus &b = *(shadow_bus *)p;
		if (!b.samples && !data_read(b.real, ea, width, v)) return false;
		uint32_t key;
		if (!resolve_address(b.real, ea, width, false, &key)) return false;
		if (b.samples) {
			*v = 0;
			for (unsigned j = 0; j < width; ++j) {
				uint8_t byte = 0; bool found = b.samples->get(key + j, byte);
				for (unsigned k = 0; k < b.nwrite; ++k) {
					const nw68_write &w = b.writes[k];
					if (key + j >= w.key && key + j - w.key < w.width) {
						byte = w.value >> ((w.width - (key + j - w.key) - 1) * 8); found = true;
					}
				}
				if (!found) return false;
				*v = (*v << 8) | byte;
			}
			return true;
		}
		for (unsigned j = 0; j < width; ++j) for (unsigned k = 0; k < b.nwrite; ++k) {
			const nw68_write &w = b.writes[k];
			if (key + j >= w.key && key + j - w.key < w.width) {
				const unsigned sh = (width - j - 1) * 8, ws = (w.width - (key + j - w.key) - 1) * 8;
				*v = (*v & ~(255u << sh)) | (((w.value >> ws) & 255u) << sh);
			}
		}
		return true;
	}
	static bool probe(void *p, uint32_t ea, unsigned width) {
		return write_probe(((shadow_bus *)p)->real, ea, width);
	}
	static bool resolve(void *p, uint32_t ea, unsigned width, bool store, uint32_t *pa) {
		return resolve_address(((shadow_bus *)p)->real, ea, width, store, pa);
	}
	bool append(const nw68_frame &f) {
		if (nwrite + f.nwrite > 512) return false;
		for (unsigned j = 0; j < f.nwrite; ++j) writes[nwrite++] = f.writes[j];
		return true;
	}
	bool effects_match(unsigned count) {
		for (unsigned k = 0; k < count; ++k) for (unsigned j = 0; j < writes[k].width; ++j) {
			const uint32_t ea = writes[k].ea + j; bool overwritten = false;
			for (unsigned later = k + 1; later < count; ++later)
				if (writes[k].key + j >= writes[later].key && writes[k].key + j - writes[later].key < writes[later].width) overwritten = true;
			if (overwritten) continue;
			uint32_t pa;
			if (!::probe(*real, ea, 1, false, &pa) ||
			    vm_read_memory_1(pa) != ((writes[k].value >> ((writes[k].width-j-1)*8)) & 255)) return false;
		}
		return true;
	}
	bool journal_matches(const nw68_frame &frame) const {
		captured_reads a, b;
		for (unsigned n = 0; n < nwrite; ++n) a.add(writes[n].key, writes[n].width, writes[n].value, true);
		for (unsigned n = 0; n < frame.nwrite; ++n) b.add(frame.writes[n].key, frame.writes[n].width, frame.writes[n].value, true);
		if (a.bytes.size() != b.bytes.size()) return false;
		for (const captured_reads::byte &entry : a.bytes) {
			uint8_t value;
			if (!b.get(entry.key, value) || value != entry.value) return false;
		}
		return true;
	}
};
struct checkpoint { nw68_state state; unsigned writes; };
struct verification_frame {
	powerpc_cpu *cpu; int depth; bool left; uint64_t steps, serial, generation;
	bus_context context; shadow_bus shadow;
	nw68_nk_state nk; nw68_state before;
	nw68_instruction instruction; nw68_frame predicted;
	checkpoint checkpoints[16]; unsigned count;
};
void failure_trace(const nw68_nk_state &nk, const nw68_state &before,
		   const nw68_frame &predicted, const nw68_state &actual)
{
	if (mismatch_traces++ >= 32) return;
	const char *path = getenv("NW_JIT68K_TRACE_PATH");
	FILE *f = path && *path ? fopen(path, "a") : stdout;
	if (!f) f = stdout;
	const nw68_instruction &i = *predicted.instruction;
	fprintf(f, "jit68k failure pc=%08x op=%04x family=%s msr=%08x depth=%d cr=%08x xer=%08x r25=%08x steps=%llu words=",
		before.pc, i.opcode, nw68_operation_name(i.operation), ppc32_guest_mmu().msr(),
		reference ? reference->depth : 0, nk.cr, nk.xer, nk.gpr[25],
		(unsigned long long)(reference ? reference->steps : 0));
	for (unsigned j = 0; j < i.word_count; ++j) fprintf(f, "%04x ", i.words[j]);
	fprintf(f, "\nCCR before=%02x native=%02x reference=%02x PC native=%08x reference=%08x SO before=%u native=%u reference=%u savedSO before=%u native=%u reference=%u\n",
		before.ccr, predicted.state.ccr, actual.ccr, predicted.state.pc, actual.pc,
		before.so, predicted.state.so, actual.so, before.extend_so, predicted.state.extend_so, actual.extend_so);
	for (unsigned j = 0; j < 8; ++j)
		fprintf(f, "r%u D before=%08x native=%08x reference=%08x A before=%08x native=%08x reference=%08x\n",
			j, before.d[j], predicted.state.d[j], actual.d[j], before.a[j], predicted.state.a[j], actual.a[j]);
	for (unsigned j = 0; j < predicted.nwrite; ++j)
		fprintf(f, "write ea=%08x width=%u value=%08x\n", predicted.writes[j].ea, predicted.writes[j].width, predicted.writes[j].value);
	fflush(f); if (f != stdout) fclose(f);
}
}

bool nw_68k_wants_boundary(uint32_t pc)
{
	if (!table(pc)) return false;
	const mode m = selected_mode();
	/* Verification observes every ROM continuation. Native selection needs
	 * only candidate boundaries; service-only handlers retain the existing
	 * bounded PPC chaining and normal event checks. */
	return m == VERIFY || (m == ON &&
		nw68_opcode_policy((pc - 0x68080000u) >> 3) == NW68_DIRECT);
}

void nw_68k_reference_chain(uint32_t pc, uint32_t opcode_pc, uint32_t dispatch)
{
	if (!nw_68k_hist_enabled() || !table(pc) || dispatch != pc) return;
	const mode m = selected_mode();
	if (m == VERIFY || (m == ON &&
		nw68_opcode_policy((pc - 0x68080000u) >> 3) == NW68_DIRECT)) return;
	nw_68k_hist_note(pc, opcode_pc);
	nw_68k_note_exit_at((pc - 0x68080000u) >> 3, NW68_EXIT_SERVICE, opcode_pc);
}

bool nw_68k_reference_active(const powerpc_cpu *cpu)
{
	return reference && reference->cpu == cpu;
}

void nw_68k_end_execution(powerpc_cpu *cpu, int depth)
{
	if (reference && reference->cpu == cpu && reference->depth == depth) {
		nw_68k_note_exit_at(reference->instruction.opcode, NW68_EXIT_VERIFY_SKIPPED, reference->before.pc);
		delete reference; reference = 0;
	}
}

void nw_68k_observe(powerpc_cpu *cpu, uint32_t pc, int depth)
{
	if (!reference || reference->cpu != cpu || reference->depth != depth) return;
	if (++reference->steps > 1000000) { nw_68k_end_execution(cpu, depth); return; }
	if (!table(pc)) reference->left = true;
}

static void complete_reference(powerpc_cpu *cpu)
{
	verification_frame &v = *reference;
	nw68_nk_state nk; cpu->nw_68k_snapshot(nk);
	nw68_state actual; nw68_import(nk, actual);
	bool match = false, covered = false;
	for (unsigned j = 0; j < v.count; ++j) if (actual.pc == v.checkpoints[j].state.pc) {
		covered = true;
		if (equal_state(actual, v.checkpoints[j].state) && v.shadow.effects_match(v.checkpoints[j].writes)) { match = true; break; }
	}
	const bool comparable = v.serial == cpu->nw_68k_exception_serial() &&
		v.generation == nw68_code_generation() && covered;
	if (!match && comparable) {
		quarantined[v.instruction.opcode] = true;
		printf("NW-BOOT G1: jit68k verify mismatch pc=%08x op=%04x family=%s expected_pc=%08x actual_pc=%08x expected_ccr=%02x actual_ccr=%02x\n",
			v.before.pc, v.instruction.opcode, nw68_operation_name(v.instruction.operation), v.predicted.state.pc, actual.pc, v.predicted.state.ccr, actual.ccr);
		fflush(stdout); failure_trace(v.nk, v.before, v.predicted, actual);
	}
	nw_68k_note_exit_at(v.instruction.opcode, match && comparable ? NW68_EXIT_VERIFIED : comparable ? NW68_EXIT_SERVICE : NW68_EXIT_VERIFY_SKIPPED, v.before.pc);
	delete reference; reference = 0;
}

int nw_68k_dispatch(powerpc_cpu *cpu)
{
	const uint32_t entry = cpu->nw_68k_ppc_pc();
	if (!table(entry) || cpu->gpr(29) != entry) return 0;
	if (reference) {
		if (reference->cpu != cpu || reference->depth != cpu->nw_68k_execute_depth() || !reference->left) return 0;
		complete_reference(cpu);
	}
	nw_68k_hist_note(entry, cpu->gpr(24) - 2u);
	const mode m = selected_mode();
	if (m == OFF) { nw_68k_note_exit_at((entry - 0x68080000u) >> 3, NW68_EXIT_SERVICE, cpu->gpr(24) - 2u); return 0; }
	const uint16_t op = (entry - 0x68080000u) >> 3;
	if (m == ON && nw68_opcode_policy(op) == NW68_NANOKERNEL) {
		nw_68k_note_exit_at(op, NW68_EXIT_SERVICE, cpu->gpr(24) - 2u); return 0;
	}
	nw68_nk_state nk; cpu->nw_68k_snapshot(nk);
	if (quarantined[op]) { nw_68k_note_exit_at(op, NW68_EXIT_SERVICE, nk.gpr[24] - 2u); return 0; }
	if (!cpu->guest_mmu_enabled() || nk.gpr[0] || nk.gpr[30] != 0x68060000u ||
	    (nk.gpr[24] & 1) || (nk.cr & 0x04800000u) || nk.gpr[23] || !cpu->nw_68k_can_run() || !compatible_nk()) {
		nw_68k_note_exit_at(op, NW68_EXIT_UNAVAILABLE, nk.gpr[24] - 2u); return 0;
	}
	nw68_state state; nw68_import(nk, state);
	const uint64_t generation = nw68_code_generation();
	const uint32_t translation_context = ppc32_guest_mmu().msr() &
		(ppc32_mmu::MSR_IR | ppc32_mmu::MSR_DR | ppc32_mmu::MSR_PR);
	bus_context context = {}; context.cpu = cpu; context.pc = state.pc;
	context.opcode = op; context.prefetched = (uint16_t)nk.gpr[27];
	nw68_bus bus = { &context, code_read, data_read, write_probe, data_write, code_fetch, resolve_address };
	static const bool blocks_enabled = []() -> bool {
		const char *e = getenv("NW_JIT68K_BLOCKS"); return !e || strcmp(e, "0");
	}();
	static const bool decoded_enabled = []() -> bool {
		const char *e = getenv("NW_JIT68K_DECODED"); return !e || strcmp(e, "0");
	}();
	nw68_instruction block[NW68_BLOCK_MAX];
	unsigned count = 1;
	uint16_t current; uint32_t physical;
	const bool cached = m == ON && blocks_enabled && decoded_enabled &&
		code_read(&context, state.pc, &current, &physical) &&
		nw68_cached_block(state.pc, translation_context, physical & ~4095u,
			op, (uint16_t)nk.gpr[27], block, &count);
	if (!cached && !nw68_decode(state.pc, bus, block[0])) {
		nw_68k_note_exit_at(op, NW68_EXIT_SERVICE, nk.gpr[24] - 2u); return 0;
	}
	const nw68_instruction instruction = block[0];
	const bus_context single_context = context;
	if (!cached && m == ON && blocks_enabled && context.npages == 1) {
		while (count < NW68_BLOCK_MAX) {
			const nw68_instruction &previous = block[count-1];
			// Only a direct BRA has a statically known successor. Other
			// transfers end the block; no translated pointer escapes the cache.
			if (previous.control && (previous.operation != NW68_BRANCH || previous.condition != 0)) break;
			const uint32_t next = previous.control ? previous.pc + 2u + (uint32_t)previous.displacement : previous.pc + previous.length;
			if ((next ^ state.pc) & ~4095u) break;
			nw68_instruction candidate; const bus_context saved = context;
			if (!nw68_decode(next, bus, candidate) ||
			    context.npages != 1 || ((next + candidate.length - 1u) ^ state.pc) & ~4095u) { context = saved; break; }
			block[count++] = candidate;
		}
	}
	unsigned flag_mask = 0;
	for (unsigned n = 0; n < count; ++n) flag_mask |= block[n].flags;
	nw68_frame result;
	static const bool check_blocks = []() -> bool {
		const char *e = getenv("NW_JIT68K_CHECK_BLOCKS"); return e && !strcmp(e, "1");
	}();
	captured_reads samples;
	if (check_blocks && m == ON && count > 1) active_samples = &samples;
	nw68_exit exit = nw68_run_block(block, count, state, bus, translation_context, context.pages, context.npages, result, generation);
	active_samples = nullptr;
	if (exit != NW68_EXIT_NATIVE && count > 1) {
		/* A later unsafe operand must not deny native execution to the
		 * current safe instruction. Preparation has no guest side effects,
		 * so retry only that instruction with its original dependency set. */
		context = single_context; count = 1; flag_mask = instruction.flags;
		exit = nw68_run(instruction, state, bus, translation_context, context.pages, context.npages, result, generation);
	}
	if (exit != NW68_EXIT_NATIVE) { nw_68k_note_exit_at(op, exit, nk.gpr[24] - 2u); return 0; }
	static uint64_t blocks_compared, blocks_inconclusive;
	if (check_blocks && m == ON && count > 1 && samples.stable) {
		nw68_state scalar = state;
		nw68_frame one;
		bus_context comparison = context;
		shadow_bus shadow; shadow.real = &comparison; shadow.nwrite = 0; shadow.samples = &samples;
		nw68_bus replay = { &shadow, shadow_bus::code, shadow_bus::read, shadow_bus::probe,
			0, shadow_bus::fetch, shadow_bus::resolve };
		bool match = true;
		for (unsigned n = 0; n < result.completed; ++n) {
			nw68_instruction single = block[n]; single.flags_live = single.flags;
			if (n && one.next_op != single.opcode) { match = false; break; }
			comparison.pc = scalar.pc; comparison.opcode = single.opcode;
			comparison.prefetched = n ? one.next_prefetch : (uint16_t)nk.gpr[27];
			if (scalar.pc != single.pc ||
			    nw68_run(single, scalar, replay, translation_context, context.pages,
				context.npages, one, generation) != NW68_EXIT_NATIVE ||
			    !shadow.append(one)) { match = false; break; }
			scalar = one.state;
		}
		match = match && equal_state(scalar, result.state) &&
			one.next_op == result.next_op && one.next_prefetch == result.next_prefetch &&
			shadow.journal_matches(result);
		if (!match && generation == nw68_code_generation()) {
			printf("NW-BOOT G1: jit68k block mismatch pc=%08x op=%04x count=%u cached=%u\n",
				state.pc, op, count, cached);
			failure_trace(nk, state, result, scalar); fflush(stdout); abort();
		}
		if (generation == nw68_code_generation()) ++blocks_compared;
		else ++blocks_inconclusive;
	} else if (check_blocks && m == ON && count > 1) ++blocks_inconclusive;
	static uint64_t block_report;
	const uint64_t report = (blocks_compared + blocks_inconclusive) >> 20;
	if (check_blocks && report > block_report) {
		block_report = report;
		printf("NW-BOOT G1: jit68k block-check compared=%llu inconclusive=%llu\n",
			(unsigned long long)blocks_compared, (unsigned long long)blocks_inconclusive);
	}
	if (m == VERIFY) {
		/* Observe the ordinary loop: no synthetic execute() frame, no repeated
		 * host/device effects, and no changes to exception/interrupt nesting. */
		reference = new (std::nothrow) verification_frame();
		if (!reference) { nw_68k_note_exit_at(op, NW68_EXIT_UNAVAILABLE, nk.gpr[24] - 2u); return 0; }
		verification_frame &v = *reference;
		v.cpu = cpu; v.depth = cpu->nw_68k_execute_depth(); v.left = false; v.steps = 0;
		v.generation = generation;
		v.context = context; v.shadow.real = &v.context; v.shadow.nwrite = 0; v.shadow.append(result);
		v.nk = nk; v.before = state; v.instruction = instruction; v.predicted = result;
		v.predicted.instruction = &v.instruction;
		v.count = 1; v.checkpoints[0] = { result.state, v.shadow.nwrite };
		nw68_bus speculative = { &v.shadow, shadow_bus::code, shadow_bus::read, shadow_bus::probe, 0, shadow_bus::code, shadow_bus::resolve };
		/* Most ROM fusion is short. Four checkpoints retain those compares
		 * without predicting fifteen extra instructions at every dispatch.
		 * Longer continuations are explicitly inconclusive; diagnostics can
		 * request the full bounded window when investigating a fused handler. */
		static const unsigned verify_window = []() -> unsigned {
			const char *e = getenv("NW_JIT68K_VERIFY_WINDOW");
			const unsigned n = e ? (unsigned)strtoul(e, 0, 10) : 4;
			return n >= 1 && n <= 16 ? n : 4;
		}();
		while (v.count < verify_window) {
			nw68_instruction next; nw68_frame frame;
			if (!nw68_decode(v.checkpoints[v.count-1].state.pc, speculative, next) ||
			    nw68_run(next, v.checkpoints[v.count-1].state, speculative, translation_context, v.context.pages, v.context.npages, frame, generation) != NW68_EXIT_NATIVE ||
				!v.shadow.append(frame)) break;
			v.checkpoints[v.count++] = { frame.state, v.shadow.nwrite };
		}
		v.serial = cpu->nw_68k_exception_serial();
		return 0;
	}
	// Commit PTE read references only after the complete instruction/block
	// and successor prefetch are safe. Store C bits are set by data_write.
	for (unsigned j = 0; j < context.nreads; ++j) {
		uint32_t pa;
		if (!cpu->guest_data_probe(context.read_addresses[j], context.read_widths[j], false, &pa)) abort();
	}
	nw68_commit(result);
	nw68_export(nk, result.state, flag_mask, result.next_op, result.next_prefetch);
	cpu->nw_68k_restore(nk); nw_68k_note_exit_at(op, NW68_EXIT_NATIVE, state.pc);
	for (unsigned j = 1; j < result.completed; ++j) {
		nw_68k_hist_note(0x68080000u + ((uint32_t)block[j].opcode << 3), block[j].pc);
		nw_68k_note_exit_at(block[j].opcode, NW68_EXIT_NATIVE, block[j].pc);
	}
	return 1;
}

void nw_68k_jit_execute(powerpc_cpu *cpu, uint32_t entry)
{
	/* The ordinary loop owns dispatch, events, nesting and execution returns. */
	cpu->execute(entry);
}
