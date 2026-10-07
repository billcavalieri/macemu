#include "nw_68k_core.h"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#ifdef __APPLE__
#include <pthread.h>
#endif

void nw_jit_dtlb_demote_pa(uint32_t pa);

namespace {
enum { CACHE = 16384, CODE_BYTES = 8 << 20, ENTRY_BYTES = 4096 };
typedef int (*native_fn)(nw68_frame *);
struct entry {
	nw68_instruction instruction;
	nw68_instruction following[NW68_BLOCK_MAX - 1];
	unsigned count;
	uint32_t context, pages[8];
	unsigned npages;
	native_fn fn;
	bool used;
};
entry entries[CACHE];
/* This is only an index into live compiled entries, whose page ownership
 * already handles writes, eviction, recycling and cross-thread invalidation. */
unsigned decoded_slots[4096];
std::unordered_map<uint32_t, std::vector<unsigned> > owners;
/* Separate from PPC ownership: unrelated stores never lock this cache. */
std::atomic<uint32_t> pagebits[32768];
void mark_page(uint32_t page, bool present) {
	const unsigned bit = page >> 12;
	const uint32_t mask = 1u << (bit & 31);
	if (present) {
		/* The first block of this page: the PPC JIT's store-proven data-TLB entries for it stop being that. */
		if (!(pagebits[bit >> 5].fetch_or(mask, std::memory_order_release) & mask)) nw_jit_dtlb_demote_pa(page);
	}
	else pagebits[bit >> 5].fetch_and(~mask, std::memory_order_release);
}
std::mutex mutex;
uint8_t *code;
size_t used;
nw68_cache_stats stats;
thread_local bool executing, deferred_invalidation;
std::atomic<uint64_t> code_generation(1);

void erase_entry(unsigned slot) {
	entry &e = entries[slot];
	if (!e.used) return;
	for (unsigned j = 0; j < e.npages; ++j) {
		auto p = owners.find(e.pages[j]);
		if (p == owners.end()) continue;
		std::vector<unsigned> &v = p->second;
		v.erase(std::remove(v.begin(), v.end(), slot), v.end());
		if (v.empty()) { mark_page(p->first, false); owners.erase(p); }
	}
	e.used = false; ++stats.invalidated;
}
void clear_entries() {
	for (unsigned j = 0; j < CACHE; ++j) {
		if (entries[j].used) ++stats.invalidated;
		entries[j].used = false;
	}
	for (auto p = owners.begin(); p != owners.end(); ++p) mark_page(p->first, false);
	owners.clear();
}
struct writer {
	bool ready;
	writer() : ready(true) {
#ifdef __APPLE__
		pthread_jit_write_protect_np(0);
#else
		ready = mprotect(code, CODE_BYTES, PROT_READ | PROT_WRITE) == 0;
#endif
	}
	~writer() {
#ifdef __APPLE__
		pthread_jit_write_protect_np(1);
#else
		if (ready) mprotect(code, CODE_BYTES, PROT_READ | PROT_EXEC);
#endif
	}
};
struct emitter {
	uint32_t *p, *end;
	bool ok;
	void word(uint32_t v) { if (p == end) ok = false; else if (ok) *p++ = v; }
	void imm(unsigned r, uintptr_t v) {
		word(0xd2800000u | (uint32_t)((v & 65535u) << 5) | r);
		for (unsigned sh = 16; sh < 64; sh += 16)
			word(0xf2800000u | ((sh / 16) << 21) | (uint32_t)(((v >> sh) & 65535u) << 5) | r);
	}
	void call(uintptr_t fn) { imm(9, fn); word(0xd63f0120u); }
	void load(unsigned r, size_t offset) { word(0xb9400000u | (uint32_t)((offset / 4) << 10) | (19 << 5) | r); }
	void store(unsigned r, size_t offset) { word(0xb9000000u | (uint32_t)((offset / 4) << 10) | (19 << 5) | r); }
	void alu(uint32_t base, unsigned rd, unsigned a, unsigned b) { word(base | (b << 16) | (a << 5) | rd); }
};
const nw68_instruction &instruction_at(const entry &e, unsigned n) { return n ? e.following[n - 1] : e.instruction; }
unsigned decoded_index(uint32_t pc, uint32_t context, uint32_t page) {
	return ((pc >> 1) ^ (pc >> 12) ^ context ^ (page >> 12)) & 4095;
}
void register_operand(emitter &a, unsigned target, const nw68_ea &ea) {
	if (ea.mode == 0) a.word(0x2a0003e0u | ((20 + ea.reg) << 16) | target);
	else if (ea.mode == 1) a.load(target, offsetof(nw68_frame, state.a) + ea.reg * 4);
	else a.imm(target, ea.immediate);
}
int block_progress(nw68_frame *f, const nw68_instruction *next) {
	++f->completed;
	return !next || (f->state.pc == next->pc && f->next_op == next->opcode &&
		(next->word_count < 2 || f->next_prefetch == next->words[1]));
}
bool register_block(emitter &a, entry &e) {
	// D0-D7 stay in callee-saved W20-W27 across flag/prefetch helpers.
	for (unsigned r = 20; r < 28; r += 2)
		a.word(0xa9bf0000u | ((r + 1) << 10) | (31 << 5) | r);
	for (unsigned r = 0; r < 8; ++r) a.load(20 + r, offsetof(nw68_frame, state.d) + r * 4);
	uint32_t *failures[NW68_BLOCK_MAX * 2]; unsigned nf = 0;
	for (unsigned n = 0; n < e.count; ++n) {
		const nw68_instruction &i = instruction_at(e, n);
		if (i.operation == NW68_BRANCH) {
			a.word(0xaa1303e0u); a.imm(1, (uintptr_t)&i); a.call((uintptr_t)nw68_prepare);
			failures[nf++] = a.p; a.word(0x34000000u);
			a.word(0xaa1303e0u); a.call((uintptr_t)nw68_finish);
			failures[nf++] = a.p; a.word(0x34000000u);
			a.imm(8, n + 1);
			a.word(0x39000000u | (offsetof(nw68_frame, completed) << 10) | (19 << 5) | 8);
			continue;
		}
		a.imm(8, (uintptr_t)&i);
		a.word(0xf9000000u | ((offsetof(nw68_frame, instruction) / 8) << 10) | (19 << 5) | 8);
		a.imm(8, i.pc + i.length); a.store(8, offsetof(nw68_frame, state.pc));
		register_operand(a, 8, i.src); register_operand(a, 9, i.dst);
		if ((i.operation == NW68_MOVEA || i.operation == NW68_ADDA || i.operation == NW68_SUBA || i.operation == NW68_CMPA) && i.width == 2)
			a.word(0x13003d08u); // sxth w8,w8
		if (i.operation == NW68_NEG || i.operation == NW68_NOT || i.operation == NW68_TST) a.word(0x52800009u);
		a.store(8, offsetof(nw68_frame, src)); a.store(9, offsetof(nw68_frame, dst));
		a.imm(11, i.dst.reg); a.store(11, offsetof(nw68_frame, destination));
		switch (i.operation) {
		case NW68_ADD: case NW68_ADDA: a.alu(0x0b000000u, 10, 9, 8); break;
		case NW68_SUB: case NW68_SUBA: case NW68_CMP: case NW68_CMPA: case NW68_NEG:
			a.alu(0x4b000000u, 10, 9, 8); break;
		case NW68_AND: a.alu(0x0a000000u, 10, 9, 8); break;
		case NW68_OR: a.alu(0x2a000000u, 10, 9, 8); break;
		case NW68_EOR: a.alu(0x4a000000u, 10, 9, 8); break;
		case NW68_CLR: a.word(0x5280000au); break;
		case NW68_NOT: a.word(0x2a2803eau); break;
		case NW68_EXT: a.word(0x13000000u | ((i.subop == 1 ? 15u : 7u) << 10) | (8 << 5) | 10); break;
		case NW68_SWAP: a.word(0x13800000u | (8 << 16) | (16 << 10) | (8 << 5) | 10); break;
		default: a.word(0x2a0803eau); break;
		}
		a.store(10, offsetof(nw68_frame, result));
		a.word(0xaa1303e0u); a.call((uintptr_t)nw68_finish);
		failures[nf++] = a.p; a.word(0x34000000u);
		a.imm(8, n + 1);
		a.word(0x39000000u | (offsetof(nw68_frame, completed) << 10) | (19 << 5) | 8);
		if (i.dst.mode == 0) a.load(20 + i.dst.reg, offsetof(nw68_frame, state.d) + i.dst.reg * 4);
	}
	uint32_t *done = a.p;
	for (int r = 26; r >= 20; r -= 2)
		a.word(0xa8c10000u | ((r + 1) << 10) | (31 << 5) | r);
	for (unsigned n = 0; n < nf; ++n)
		*failures[n] = 0x34000000u | ((uint32_t)(done - failures[n]) << 5);
	return a.ok;
}
native_fn compile(entry &e) {
#if !defined(__aarch64__) && !defined(__arm64__)
	return 0;
#else
	if (!code) {
		int protection = PROT_READ | PROT_WRITE;
		int flags = MAP_ANON | MAP_PRIVATE;
#ifdef MAP_JIT
		protection |= PROT_EXEC; flags |= MAP_JIT;
#endif
		void *p = mmap(0, CODE_BYTES, protection, flags, -1, 0);
		if (p == MAP_FAILED) return 0;
		code = (uint8_t *)p;
		atexit(nw68_shutdown);
	}
	writer w;
	if (!w.ready || used + ENTRY_BYTES > CODE_BYTES) return 0;
	uint32_t *start = (uint32_t *)(code + used);
	emitter a = { start, start + ENTRY_BYTES / 4, true };
	a.word(0xa9bf7bf3u); // stp x19,x30,[sp,#-16]!
	a.word(0xaa0003f3u); // mov x19,x0
	bool registers = true;
	for (unsigned n = 0; n < e.count; ++n) registers &= nw68_register_instruction(instruction_at(e,n)) ||
		(e.count > 1 && n + 1 == e.count && instruction_at(e,n).operation == NW68_BRANCH);
	if (registers) {
		if (!register_block(a, e)) return 0;
		a.word(0xa8c17bf3u); a.word(0xd65f03c0u);
		__builtin___clear_cache((char *)start, (char *)a.p);
		used = ((uint8_t *)a.p - code + 15u) & ~(size_t)15u;
		++stats.compiled;
		if (e.count > 1) ++stats.blocks;
		return (native_fn)start;
	}
	uint32_t *failures[NW68_BLOCK_MAX * 3]; unsigned nf = 0;
	for (unsigned n = 0; n < e.count; ++n) {
	const nw68_instruction &i = instruction_at(e, n);
	a.word(0xaa1303e0u);
	a.imm(1, (uintptr_t)&i);
	a.call((uintptr_t)nw68_prepare);
	failures[nf++] = a.p; a.word(0x34000000u); // cbz w0,return
	a.load(8, offsetof(nw68_frame, src)); a.load(9, offsetof(nw68_frame, dst));
	bool stores_result = true;
	switch (i.operation) {
	case NW68_MOVE: case NW68_MOVEA: case NW68_TST: a.word(0x2a0803eau); break;
	case NW68_ADD: case NW68_ADDA: case NW68_ADDX: a.alu(0x0b000000u, 10, 9, 8); break;
	case NW68_SUB: case NW68_SUBA: case NW68_CMP: case NW68_CMPA: case NW68_SUBX:
	case NW68_NEG: case NW68_NEGX: a.alu(0x4b000000u, 10, 9, 8); break;
	case NW68_AND: a.alu(0x0a000000u, 10, 9, 8); break;
	case NW68_OR: a.alu(0x2a000000u, 10, 9, 8); break;
	case NW68_EOR: a.alu(0x4a000000u, 10, 9, 8); break;
	case NW68_CLR: a.word(0x5280000au); break;
	case NW68_NOT: a.word(0x2a2803eau); break;
	case NW68_EXT:
		a.word(0x13000000u | ((i.subop == 1 ? 15u : 7u) << 10) | (8 << 5) | 10); break;
	case NW68_SWAP: a.word(0x13800000u | (8 << 16) | (16 << 10) | (8 << 5) | 10); break;
	case NW68_BIT:
		a.word(0x5280002bu); // mov w11,#1
		a.alu(0x1ac02000u, 11, 11, 8); // lslv w11,w11,w8
		if (i.subop == 1) a.alu(0x4a000000u, 10, 9, 11);
		else if (i.subop == 2) a.alu(0x0a200000u, 10, 9, 11);
		else if (i.subop == 3) a.alu(0x2a000000u, 10, 9, 11);
		else a.word(0x2a0903eau);
		break;
	default: stores_result = false; break;
	}
	if (i.operation == NW68_ADDX || i.operation == NW68_SUBX || i.operation == NW68_NEGX) {
		a.word(0x39400000u | (offsetof(nw68_frame, state.ccr) << 10) | (19 << 5) | 11);
		a.word(0x53000000u | (4 << 16) | (4 << 10) | (11 << 5) | 11);
		a.alu(i.operation == NW68_ADDX ? 0x0b000000u : 0x4b000000u, 10, 10, 11);
	}
	if (stores_result) a.store(10, offsetof(nw68_frame, result));
	a.word(0xaa1303e0u); a.call((uintptr_t)nw68_finish);
	failures[nf++] = a.p; a.word(0x34000000u);
	a.word(0xaa1303e0u);
	a.imm(1, n + 1 < e.count ? (uintptr_t)&instruction_at(e, n + 1) : 0);
	a.call((uintptr_t)block_progress);
	failures[nf++] = a.p; a.word(0x34000000u);
	}
	uint32_t *done = a.p;
	a.word(0xa8c17bf3u); a.word(0xd65f03c0u);
	if (!a.ok) return 0;
	for (unsigned n = 0; n < nf; ++n) *failures[n] = 0x34000000u | ((uint32_t)(done - failures[n]) << 5);
	__builtin___clear_cache((char *)start, (char *)a.p);
	used = ((uint8_t *)a.p - code + 15u) & ~(size_t)15u;
	++stats.compiled; if (e.count > 1) ++stats.blocks;
	return (native_fn)start;
#endif
}
}

bool nw68_register_instruction(const nw68_instruction &i)
{
	if (i.control || i.dst.mode > 1 || !(i.src.mode <= 1 || (i.src.mode == 7 && i.src.reg == 4))) return false;
	switch (i.operation) {
	case NW68_NOP: case NW68_MOVE: case NW68_MOVEA: case NW68_ADD: case NW68_SUB:
	case NW68_CMP: case NW68_ADDA: case NW68_SUBA: case NW68_CMPA: case NW68_AND:
	case NW68_OR: case NW68_EOR: case NW68_CLR: case NW68_TST: case NW68_NOT:
	case NW68_NEG: case NW68_EXT: case NW68_SWAP: return true;
	default: return false;
	}
}

nw68_exit nw68_run_block(const nw68_instruction *instructions, unsigned count, const nw68_state &state,
		  const nw68_bus &bus, uint32_t context, const uint32_t *pages,
		  unsigned npages, nw68_frame &f, uint64_t generation)
{
	memset(&f, 0, sizeof f); f.state = state; f.bus = bus; f.exit = NW68_EXIT_UNAVAILABLE;
	if (!generation) generation = nw68_code_generation();
	if (executing) return f.exit = NW68_EXIT_REENTRY;
	if (!instructions || !count || count > NW68_BLOCK_MAX) return f.exit = NW68_EXIT_SERVICE;
	const nw68_instruction &i = instructions[0];
	if (i.operation == NW68_SERVICE || !pages || !npages || npages > 8) return f.exit = NW68_EXIT_SERVICE;
	for (unsigned n = 0; n < count; ++n) {
		if (instructions[n].operation == NW68_SERVICE) return f.exit = NW68_EXIT_SERVICE;
		if (n && !instructions[n-1].control &&
		    instructions[n].pc != instructions[n-1].pc + instructions[n-1].length) return f.exit = NW68_EXIT_SERVICE;
	}
	std::lock_guard<std::mutex> lock(mutex);
	if (generation != nw68_code_generation()) return f.exit = NW68_EXIT_CODE;
	if (used + ENTRY_BYTES > CODE_BYTES) { clear_entries(); used = 0; ++stats.recycled; }
	uint32_t hash = i.pc ^ context;
	for (unsigned j = 0; j < npages; ++j) hash = hash * 33u ^ pages[j];
	for (unsigned n = 0; n < count; ++n) for (unsigned j = 0; j < instructions[n].word_count; ++j)
		hash = hash * 33u ^ instructions[n].words[j];
	/* Guest PCs/opcodes share alignment and low-bit patterns. Avalanche
	 * before choosing a set, rather than repeatedly evicting the same sets. */
	hash ^= hash >> 16; hash *= 0x7feb352du;
	hash ^= hash >> 15; hash *= 0x846ca68bu; hash ^= hash >> 16;
	unsigned slot = hash & (CACHE - 1), target = slot;
	entry *e = 0;
	for (unsigned n = 0; n < 8; ++n) {
		entry &candidate = entries[(slot + n) & (CACHE - 1)];
		if (!candidate.used) { target = (slot + n) & (CACHE - 1); break; }
		if (candidate.count == count && candidate.context == context && candidate.instruction.pc == i.pc &&
		    candidate.npages == npages && candidate.instruction.word_count == i.word_count &&
		    memcmp(candidate.pages, pages, npages * sizeof *pages) == 0 &&
		    memcmp(candidate.instruction.words, i.words, i.word_count * sizeof i.words[0]) == 0) {
			bool match = true;
			for (unsigned k = 1; k < count; ++k) {
				const nw68_instruction &cached = instruction_at(candidate, k);
				if (cached.pc != instructions[k].pc || cached.word_count != instructions[k].word_count ||
				    memcmp(cached.words, instructions[k].words, cached.word_count * sizeof cached.words[0])) match = false;
			}
			if (match) { e = &candidate; ++stats.hits; break; }
		}
	}
	if (!e) {
		erase_entry(target); e = &entries[target]; e->instruction = i;
		e->count = count;
		for (unsigned n = 1; n < count; ++n) e->following[n - 1] = instructions[n];
		/* Register blocks have no CCR consumers. Keep only flag producers
		 * needed at the synchronized exit; preserve sticky PPC SO separately. */
		bool registers = true;
		for (unsigned n = 0; n < count; ++n) registers &= nw68_register_instruction(instructions[n]) ||
			(count > 1 && n + 1 == count && instructions[n].operation == NW68_BRANCH);
		unsigned live = 31;
		for (unsigned n = count; n > 0; --n) {
			nw68_instruction &cached = n == 1 ? e->instruction : e->following[n - 2];
			cached.flags_live = registers ? cached.flags & live : cached.flags; live &= ~cached.flags;
		}
		e->context = context; e->npages = npages; memcpy(e->pages, pages, npages * sizeof *pages);
		e->fn = compile(*e);
		if (!e->fn) return f.exit;
		e->used = true;
		for (unsigned j = 0; j < npages; ++j) { owners[pages[j]].push_back(target); mark_page(pages[j], true); }
	}
	unsigned &hint = decoded_slots[decoded_index(i.pc, context, pages[0])];
	bool retain_block = false;
	if (count == 1 && hint) {
		const entry &previous = entries[hint - 1];
		retain_block = previous.used && previous.count > 1 && previous.npages == 1 &&
			previous.pages[0] == pages[0] && previous.context == context &&
			previous.instruction.pc == i.pc && previous.instruction.word_count == i.word_count &&
			!memcmp(previous.instruction.words, i.words, i.word_count * sizeof i.words[0]);
	}
	// A data-dependent scalar retry must not permanently replace a still
	// valid block's decode hint. The next run rechecks operands and prefetch.
	if (!retain_block) hint = (unsigned)(e - entries) + 1;
	executing = true;
	e->fn(&f);
	executing = false;
	/* Never hand out an instruction pointer into an evictable cache entry. */
	f.instruction = &i;
	if (deferred_invalidation) {
		clear_entries(); deferred_invalidation = false; f.exit = NW68_EXIT_CODE;
	}
	if (generation != nw68_code_generation()) f.exit = NW68_EXIT_CODE;
	if (count > 1 && f.exit == NW68_EXIT_NATIVE) stats.block_instructions += f.completed;
	return f.exit;
}

nw68_exit nw68_run(const nw68_instruction &i, const nw68_state &state,
		  const nw68_bus &bus, uint32_t context, const uint32_t *pages,
		  unsigned npages, nw68_frame &f, uint64_t generation)
{
	return nw68_run_block(&i, 1, state, bus, context, pages, npages, f, generation);
}

uint64_t nw68_code_generation() { return code_generation.load(std::memory_order_acquire); }

bool nw68_cached_block(uint32_t pc, uint32_t context, uint32_t page,
		 uint16_t opcode, uint16_t prefetch, nw68_instruction *out, unsigned *count)
{
	if (executing || !out || !count) return false;
	std::lock_guard<std::mutex> lock(mutex);
	const unsigned slot = decoded_slots[decoded_index(pc, context, page)];
	if (!slot) return false;
	const entry &e = entries[slot - 1];
	if (!e.used || e.context != context || e.npages != 1 || e.pages[0] != page ||
	    e.instruction.pc != pc || e.instruction.opcode != opcode ||
	    ((pc ^ (pc + e.instruction.length - 1u)) & ~4095u)) return false;
	if (e.instruction.word_count > 1 && e.instruction.words[1] != prefetch) return false;
	// A one-word first instruction consumes the already-prefetched opcode
	// of its successor. A register block must match that snapshot too.
	if (e.count > 1 && e.instruction.length == 2 && !e.instruction.control &&
	    e.following[0].opcode != prefetch) return false;
	for (unsigned n = 0; n < e.count; ++n) out[n] = instruction_at(e, n);
	*count = e.count; ++stats.decoded_hits;
	return true;
}

int nw68_page_has_code(uint32_t page)
{
	const unsigned bit = page >> 12;
	return (pagebits[bit >> 5].load(std::memory_order_acquire) & (1u << (bit & 31))) != 0;
}

void nw68_invalidate_page(uint32_t page)
{
	const unsigned bit = page >> 12;
	if (!(pagebits[bit >> 5].load(std::memory_order_acquire) & (1u << (bit & 31)))) return;
	code_generation.fetch_add(1, std::memory_order_acq_rel);
	if (executing) { deferred_invalidation = true; return; }
	std::lock_guard<std::mutex> lock(mutex);
	auto p = owners.find(page & ~4095u);
	if (p == owners.end()) return;
	const std::vector<unsigned> slots = p->second;
	for (unsigned j = 0; j < slots.size(); ++j) erase_entry(slots[j]);
}
void nw68_invalidate_all()
{
	code_generation.fetch_add(1, std::memory_order_acq_rel);
	if (executing) { deferred_invalidation = true; return; }
	std::lock_guard<std::mutex> lock(mutex); clear_entries();
}
void nw68_context_changed()
{
	code_generation.fetch_add(1, std::memory_order_acq_rel);
	if (executing) deferred_invalidation = true;
}
void nw68_shutdown()
{
	code_generation.fetch_add(1, std::memory_order_acq_rel);
	if (executing) { deferred_invalidation = true; return; }
	std::lock_guard<std::mutex> lock(mutex); clear_entries();
	if (code) munmap(code, CODE_BYTES); code = 0; used = 0;
}
nw68_cache_stats nw68_cache_statistics()
{
	std::lock_guard<std::mutex> lock(mutex); return stats;
}
