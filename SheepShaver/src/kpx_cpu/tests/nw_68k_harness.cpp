#include "nw_68k_core.h"
#include "nw_jit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <memory>
#include <limits.h>
#include <chrono>
#include <thread>

void idle_resume() {}
namespace {
unsigned passed, failed, rom_run, rom_skipped;
unsigned family_failed[64];
bool benchmark_requested;
const char *policy_path;
#define CHECK(x) do { if (x) ++passed; else { if (failed < 24) fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); ++failed; } } while (0)
struct fixture {
	uint8_t ram[0x10000];
	std::shared_ptr<std::vector<uint8_t> > rom;
	int fail_read, fail_write, fail_code, reads, probes, writes, codes, nested;
	bool alias;
	std::thread *invalidator = nullptr;
	uint32_t map(uint32_t ea) const { return alias && ea >= 0x9000 && ea < 0xa000 ? ea - 0x5000 : ea; }
	bool snapshot; uint16_t snapshot_op, snapshot_prefetch;
	fixture() : rom(new std::vector<uint8_t>), fail_read(-1), fail_write(-1), fail_code(-1), reads(0), probes(0), writes(0), codes(0), nested(0), alias(false), snapshot(false), snapshot_op(0), snapshot_prefetch(0) { memset(ram, 0, sizeof ram); }
	bool get(uint32_t ea, unsigned w, uint32_t &v) {
		ea = map(ea);
		const uint8_t *p = 0;
		if ((uint64_t)ea + w <= sizeof ram) p = ram + ea;
		else if (ea >= 0x68000000u && (uint64_t)(ea - 0x68000000u) + 0x300000u + w <= rom->size()) p = &(*rom)[ea - 0x68000000u + 0x300000u];
		if (!p) return false;
		v = 0; for (unsigned j = 0; j < w; ++j) v = (v << 8) | p[j]; return true;
	}
	void set(uint32_t ea, unsigned w, uint32_t v) {
		ea = map(ea);
		if ((uint64_t)ea + w > sizeof ram) abort();
		for (unsigned j = 0; j < w; ++j) ram[ea + j] = (uint8_t)(v >> ((w - j - 1) * 8));
	}
	static bool fetch(void *p, uint32_t ea, uint16_t *v, uint32_t *pa) {
		fixture &f = *(fixture *)p; uint32_t value;
		if (f.codes++ == f.fail_code) return false;
		if (!f.get(ea, 2, value)) return false; *v = value; *pa = f.map(ea); return true;
	}
	static bool code(void *p, uint32_t ea, uint16_t *v, uint32_t *pa) {
		if (!fetch(p, ea, v, pa)) return false;
		fixture &f = *(fixture *)p;
		if (f.snapshot && ea == 0x200) *v = f.snapshot_op;
		if (f.snapshot && ea == 0x202) *v = f.snapshot_prefetch;
		return true;
	}
	static bool read(void *p, uint32_t ea, unsigned w, uint32_t *v) {
		fixture &f = *(fixture *)p;
		if (f.nested) {
			const int event = f.nested; f.nested = 0;
			if (event == 1) {
				nw68_instruction i = {}; i.operation = NW68_NOP; i.pc = 0x200; i.length = 2; i.width = 4;
				nw68_state s = {}; s.pc = i.pc; nw68_frame nested_frame; uint32_t page = 0;
				CHECK(nw68_run(i, s, f.bus(), 0, &page, 1, nested_frame) == NW68_EXIT_REENTRY);
			} else if (event == 2) nw68_invalidate_page(0);
			else if (event == 3) nw68_context_changed();
			else {
				const uint64_t generation = nw68_code_generation();
				f.invalidator = new std::thread([] { nw68_invalidate_page(0); });
				// The invalidator announces the write before waiting for our
				// execution lock. Joining here would deadlock this regression.
				while (nw68_code_generation() == generation) std::this_thread::yield();
			}
		}
		if (f.reads++ == f.fail_read) return false;
		return f.get(ea, w, *v);
	}
	static bool probe(void *p, uint32_t ea, unsigned w) {
		fixture &f = *(fixture *)p;
		return f.probes++ != f.fail_write && (uint64_t)ea + w <= sizeof f.ram;
	}
	static void write(void *p, uint32_t ea, unsigned w, uint32_t v) { fixture &f = *(fixture *)p; ++f.writes; f.set(ea, w, v); }
	static bool resolve(void *p, uint32_t ea, unsigned w, bool, uint32_t *pa) {
		fixture &f = *(fixture *)p; *pa = f.map(ea);
		return f.map(ea + w - 1) == *pa + w - 1;
	}
	nw68_bus bus() { nw68_bus b = { this, code, read, probe, write, fetch, resolve }; return b; }
};
nw68_state initial()
{
	nw68_state s = {}; s.pc = 0x200; s.a[0] = 0x4000; s.a[1] = 0x5000; s.a[6] = 0x6000; s.a[7] = 0x8000;
	for (unsigned j = 0; j < 8; ++j) s.d[j] = 0xa5000000u | j;
	s.ccr = NW68_X | NW68_Z; return s;
}
nw68_exit run(fixture &f, nw68_state &s, const std::vector<uint16_t> &words, nw68_frame &out, bool commit = true)
{
	for (unsigned j = 0; j < words.size(); ++j) f.set(s.pc + j * 2, 2, words[j]);
	f.set(s.pc + words.size() * 2, 2, 0x4e71); f.set(s.pc + words.size() * 2 + 2, 2, 0x4e71);
	nw68_instruction i; nw68_bus bus = f.bus();
	if (!nw68_decode(s.pc, bus, i)) return NW68_EXIT_SERVICE;
	uint32_t pages[] = { s.pc & ~4095u, (s.pc + i.length - 1) & ~4095u };
	const nw68_exit e = nw68_run(i, s, bus, 0, pages, pages[0] == pages[1] ? 1 : 2, out);
	if (e == NW68_EXIT_NATIVE && commit) { nw68_commit(out); s = out.state; }
	return e;
}
uint32_t rng_state = 0x68c0ffee;
uint32_t random_word() { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5; return rng_state; }
struct page_fixture {
	unsigned calls = 0; bool writable = false;
	static bool probe(void *p, uint32_t ea, bool store, uint32_t *pa) {
		page_fixture &f = *(page_fixture *)p; ++f.calls;
		if (store && !f.writable) return false;
		*pa = ea < 4096 ? ea + 0x10000 : ea + 0x20000; return true;
	}
};
void page_tests()
{
	nw68_page_cache cache = {}; page_fixture f; uint32_t pa = 0;
	CHECK(nw68_page_translate(cache, &f, page_fixture::probe, 12, false, &pa) && pa == 0x1000c);
	CHECK(nw68_page_translate(cache, &f, page_fixture::probe, 4095, false, &pa) && pa == 0x10fff && f.calls == 1);
	CHECK(!nw68_page_translate(cache, &f, page_fixture::probe, 12, true, &pa) && f.calls == 2);
	f.writable = true;
	CHECK(nw68_page_translate(cache, &f, page_fixture::probe, 12, true, &pa) && f.calls == 3);
	CHECK(nw68_page_translate(cache, &f, page_fixture::probe, 16, true, &pa) && pa == 0x10010 && f.calls == 3);
	CHECK(nw68_page_translate(cache, &f, page_fixture::probe, 4096, false, &pa) && pa == 0x21000 && f.calls == 4);
	cache = {}; /* A new dispatch cannot reuse an old permission. */
	f.writable = false;
	CHECK(!nw68_page_translate(cache, &f, page_fixture::probe, 12, true, &pa) && f.calls == 5);
	for (unsigned page = 0; page < 20; ++page)
		CHECK(nw68_page_translate(cache, &f, page_fixture::probe, page * 4096, false, &pa));
	CHECK(cache.count == 16);
	CHECK(nw68_hash_table_overlap(0x100000, 4, 0x100001));
	CHECK(nw68_hash_table_overlap(0xffffe, 4, 0x100001));
	CHECK(nw68_hash_table_overlap(0x11ffff, 1, 0x100001));
	CHECK(!nw68_hash_table_overlap(0x120000, 4, 0x100001));
	CHECK(!nw68_hash_table_overlap(0xffffc, 4, 0x100001));
	CHECK(!nw68_hash_table_overlap(0x100000, 0, 0x100001));
	CHECK(nw68_hash_table_overlap(0xffffffff, 1, 0xffff0000));
}

void state_tests()
{
	for (unsigned bits = 0; bits < 128; ++bits) {
		nw68_nk_state nk = {}; nk.cr = 0x00f12345; nk.xer = 0x8000007f; nk.gpr[25] = 0x12345678;
		for (unsigned j = 0; j < 32; ++j) nk.gpr[j] = random_word();
		nk.gpr[29] = 0x68080000;
		nw68_state s = initial(); s.ccr = bits & 31; s.so = bits & 32; s.extend_so = bits & 64;
		nw68_export(nk, s, 31, 0x7001, 0xffff);
		nw68_state back; nw68_import(nk, back);
		CHECK(!memcmp(s.d, back.d, sizeof s.d)); CHECK(!memcmp(s.a, back.a, sizeof s.a));
		CHECK(back.pc == s.pc && back.ccr == s.ccr && back.so == s.so && back.extend_so == s.extend_so);
		CHECK((nk.cr & 0x0fffffffu) == 0x00f12345u);
		CHECK(nk.gpr[27] == UINT32_MAX && nk.gpr[29] == 0x680b8008 && nk.lr == nk.gpr[29]);
	}
}
void arithmetic_tests()
{
	const uint32_t edges[] = {0,1,0x7f,0x80,0xff,0x7fff,0x8000,0xffff,0x7fffffff,0x80000000,UINT32_MAX};
	for (unsigned width = 1; width <= 4; width *= 2) {
		const unsigned size = width == 1 ? 0 : width == 2 ? 1 : 2;
		const uint64_t mask = width == 4 ? UINT32_MAX : (1u << (width * 8)) - 1;
		const uint32_t sign = 1u << (width * 8 - 1);
		for (unsigned sub = 0; sub < 2; ++sub) for (unsigned x = 0; x < 2; ++x)
		for (uint32_t a : edges) for (uint32_t b : edges) {
			fixture f; nw68_state s = initial(); s.d[0] = a; s.d[1] = b;
			s.ccr = (x ? NW68_X : 0) | NW68_Z;
			nw68_frame out; const uint16_t op = (sub ? 0x9001 : 0xd001) | (size << 6);
			CHECK(run(f, s, {op}, out) == NW68_EXIT_NATIVE);
			const uint32_t aa = a & mask, bb = b & mask;
			const uint32_t r = (sub ? aa - bb : aa + bb) & mask;
			const bool carry = sub ? aa < bb : (uint64_t)aa + bb > mask;
			const bool ov = sub ? ((aa ^ bb) & (aa ^ r) & sign) : (~(aa ^ bb) & (aa ^ r) & sign);
			const unsigned flags = (r & sign ? NW68_N : 0) | (!r ? NW68_Z : 0) | (ov ? NW68_V : 0) | (carry ? NW68_C | NW68_X : 0);
			CHECK(s.d[0] == ((a & ~mask) | r)); CHECK(s.ccr == flags); CHECK(s.pc == 0x202);
		}
	}
	for (unsigned n = 0; n < 256; ++n) {
		fixture f; nw68_state s = initial(); nw68_frame out;
		CHECK(run(f, s, {(uint16_t)(0x7000 | n)}, out) == NW68_EXIT_NATIVE);
		CHECK(s.d[0] == (uint32_t)(int32_t)(int8_t)n);
		CHECK((s.ccr & NW68_X) != 0);
	}
}
void memory_control_tests()
{
	fixture f; nw68_state s = initial(); nw68_frame out;
	f.set(0x4000, 4, 0x12345678);
	CHECK(run(f, s, {0x22d8}, out) == NW68_EXIT_NATIVE); // move.l (a0)+,(a1)+
	uint32_t value; CHECK(f.get(0x5000,4,value) && value == 0x12345678);
	CHECK(s.a[0] == 0x4004 && s.a[1] == 0x5004 && f.writes == 1);
	s = initial(); f.set(0x8000,1,0x80);
	CHECK(run(f, s, {0x101f}, out) == NW68_EXIT_NATIVE); // move.b (a7)+,d0
	CHECK(s.a[7] == 0x8002 && s.d[0] == 0xa5000080 && (s.ccr & NW68_N));
	s = initial(); f.set(0x208,2,0x4e71); f.set(0x20a,2,0x4e71);
	CHECK(run(f,s,{0x6106},out) == NW68_EXIT_NATIVE); CHECK(s.pc == 0x208 && s.a[7] == 0x7ffc);
	CHECK(f.get(0x7ffc,4,value) && value == 0x202);
	CHECK(run(f,s,{0x4e75},out) == NW68_EXIT_NATIVE); CHECK(s.pc == 0x202 && s.a[7] == 0x8000);
	s = initial(); CHECK(run(f,s,{0x4e56,0xfff0},out) == NW68_EXIT_NATIVE);
	CHECK(s.a[6] == 0x7ffc && s.a[7] == 0x7fec);
	CHECK(run(f,s,{0x4e5e},out) == NW68_EXIT_NATIVE); CHECK(s.a[6] == 0x6000 && s.a[7] == 0x8000);
	s = initial(); s.d[0] = 0xaaaa0002; f.set(0x1fe,2,0x4e71); f.set(0x200,2,0x4e71);
	CHECK(run(f,s,{0x51c8,0xfffc},out) == NW68_EXIT_NATIVE); CHECK(s.pc == 0x1fe && s.d[0] == 0xaaaa0001);
	s = initial(); CHECK(run(f,s,{0x48e7,0xc000},out) == NW68_EXIT_NATIVE); // movem.l d0/d1,-(a7)
	CHECK(s.a[7] == 0x7ff8); CHECK(f.get(0x7ff8,4,value) && value == 0xa5000000);
	CHECK(f.get(0x7ffc,4,value) && value == 0xa5000001);
	CHECK(run(f,s,{0x4cdf,0x0003},out) == NW68_EXIT_NATIVE); CHECK(s.a[7] == 0x8000 && s.d[1] == 0xa5000001);
	s = initial(); f.set(0x4000, 4, 0x76543210);
	CHECK(run(f,s,{0x20d8},out) == NW68_EXIT_NATIVE); // (a0)+,(a0)+: source increment precedes destination
	CHECK(s.a[0] == 0x4008 && f.get(0x4004,4,value) && value == 0x76543210);
	s = initial(); f.set(0x7ffe, 1, 0x55);
	CHECK(run(f,s,{0x1f27},out) == NW68_EXIT_NATIVE); // -(a7),-(a7): each byte stack operand consumes two bytes
	CHECK(s.a[7] == 0x7ffc && f.get(0x7ffc,1,value) && value == 0x55);
}
void block_tests()
{
	const uint16_t words[] = {0x203c,0x7fff,0xffff,0x2200,0x5240,0xd280,0xb041,0x4601,0x4841,0x48c1,0x4e71,0x4e71};
	for (unsigned trial = 0; trial < 512; ++trial) {
		fixture f; nw68_state before = initial(); before.ccr = trial & 31;
		before.so = trial & 32; before.extend_so = trial & 64;
		for (unsigned r = 0; r < 8; ++r) before.d[r] = random_word();
		for (unsigned j = 0; j < sizeof words / sizeof words[0]; ++j) f.set(0x200 + j*2, 2, words[j]);
		nw68_instruction i[NW68_BLOCK_MAX]; nw68_bus bus = f.bus(); unsigned pc = 0x200;
		for (unsigned j = 0; j < NW68_BLOCK_MAX; ++j) { CHECK(nw68_decode(pc, bus, i[j])); CHECK(nw68_register_instruction(i[j])); pc += i[j].length; }
		nw68_state singles = before; nw68_frame one, block; uint32_t page = 0;
		bool so = false;
		for (unsigned j = 0; j < NW68_BLOCK_MAX; ++j) { CHECK(nw68_run(i[j], singles, bus, 0, &page, 1, one) == NW68_EXIT_NATIVE); singles = one.state; so |= one.so_raised; }
		CHECK(nw68_run_block(i, NW68_BLOCK_MAX, before, bus, 0, &page, 1, block) == NW68_EXIT_NATIVE);
		CHECK(singles.pc == block.state.pc && singles.ccr == block.state.ccr &&
			singles.so == block.state.so && singles.extend_so == block.state.extend_so &&
			!memcmp(singles.d, block.state.d, sizeof singles.d));
		CHECK(block.so_raised == so);
		CHECK(!block.nwrite && block.next_op == 0x4e71);
		// A late prefetch failure rejects the entire private register block.
		f.fail_code = f.codes + 7;
		CHECK(nw68_run_block(i, NW68_BLOCK_MAX, before, bus, 0, &page, 1, block) == NW68_EXIT_CODE);
		CHECK(!block.nwrite && !f.writes);
	}
	CHECK(nw68_cache_statistics().blocks > 0 && nw68_cache_statistics().block_instructions >= 512 * NW68_BLOCK_MAX);
}
void register_branch_tests()
{
	for (unsigned condition = 2; condition < 16; ++condition) for (unsigned trial = 0; trial < 128; ++trial) {
		fixture f; const uint16_t words[] = {0x5280,0x5381,(uint16_t)(0x60fa | condition << 8),0x4e71,0x4e71};
		for (unsigned n = 0; n < 5; ++n) f.set(0x200 + n*2,2,words[n]);
		nw68_state initial_state = initial(); initial_state.d[0] = random_word(); initial_state.d[1] = trial & 3;
		initial_state.ccr = trial & 31; initial_state.so = trial & 32; initial_state.extend_so = trial & 64;
		nw68_instruction instructions[3]; nw68_bus bus = f.bus();
		for (unsigned n = 0; n < 3; ++n) CHECK(nw68_decode(0x200 + n*2,bus,instructions[n]));
		nw68_state expected = initial_state; nw68_frame scalar, block; uint32_t page = 0;
		for (unsigned n = 0; n < 3; ++n) { CHECK(nw68_run(instructions[n],expected,bus,0,&page,1,scalar) == NW68_EXIT_NATIVE); expected = scalar.state; }
		CHECK(nw68_run_block(instructions,3,initial_state,bus,0,&page,1,block) == NW68_EXIT_NATIVE);
		CHECK(block.completed == 3 && !memcmp(expected.d,block.state.d,sizeof expected.d) && expected.pc == block.state.pc &&
			expected.ccr == block.state.ccr && expected.so == block.state.so && expected.extend_so == block.state.extend_so &&
			scalar.next_op == block.next_op && scalar.next_prefetch == block.next_prefetch);
	}
}

void mixed_block_tests()
{
	// Store/load forwarding across memory blocks, followed by a guarded
	// direct branch. Compare complete state and RAM with committed scalars.
	const uint16_t words[] = {0x2080,0x2210,0x5281,0x2081,0x2410,0x6002,0x7000,0x4e71,0x4e71};
	for (unsigned trial = 0; trial < 128; ++trial) {
		fixture scalar, combined; nw68_state before = initial();
		before.d[0] = random_word(); before.ccr = trial & 31;
		before.so = trial & 32; before.extend_so = trial & 64;
		for (unsigned j = 0; j < sizeof words / sizeof words[0]; ++j)
			scalar.set(0x200 + j * 2, 2, words[j]);
		combined = scalar;
		nw68_instruction instructions[7]; unsigned pc = before.pc;
		for (unsigned n = 0; n < 7; ++n) {
			CHECK(nw68_decode(pc, scalar.bus(), instructions[n]));
			pc = instructions[n].operation == NW68_BRANCH ? pc + 2 + instructions[n].displacement : pc + instructions[n].length;
		}
		uint32_t page = 0; nw68_state expected = before; nw68_frame step, block;
		for (unsigned n = 0; n < 7; ++n) {
			CHECK(nw68_run(instructions[n], expected, scalar.bus(), 0, &page, 1, step) == NW68_EXIT_NATIVE);
			nw68_commit(step); expected = step.state;
		}
		CHECK(nw68_run_block(instructions, 7, before, combined.bus(), 0, &page, 1, block) == NW68_EXIT_NATIVE);
		CHECK(block.completed == 7 && block.nwrite == 2);
		nw68_commit(block);
		CHECK(!memcmp(expected.d, block.state.d, sizeof expected.d) &&
			!memcmp(expected.a, block.state.a, sizeof expected.a) && expected.pc == block.state.pc &&
			expected.ccr == block.state.ccr && expected.so == block.state.so && expected.extend_so == block.state.extend_so);
		CHECK(!memcmp(scalar.ram, combined.ram, sizeof scalar.ram));
		// A failure after earlier staged stores must leave RAM untouched.
		fixture fault = scalar; fault.fail_read = fault.reads + 1; const fixture untouched = fault;
		CHECK(nw68_run_block(instructions, 7, before, fault.bus(), 0, &page, 1, block) == NW68_EXIT_MEMORY);
		CHECK(!memcmp(untouched.ram, fault.ram, sizeof fault.ram));
		// Target modifications cannot execute the already decoded successor.
		combined.set(instructions[6].pc, 2, 0x7007);
		CHECK(nw68_run_block(instructions, 7, before, combined.bus(), 0, &page, 1, block) == NW68_EXIT_NATIVE);
		CHECK(block.completed == 6 && block.next_op == 0x7007);
	}
}

void alias_block_tests()
{
	for (unsigned trial = 0; trial < 128; ++trial) {
		fixture scalar; scalar.alias = true;
		const uint16_t words[] = {0x2080,0x2211,0x5281,0x1281,0x2410,0x4e71,0x4e71};
		for (unsigned n = 0; n < 7; ++n) scalar.set(0x200+n*2,2,words[n]);
		fixture block_memory = scalar;
		nw68_state before = initial(); before.a[1] = 0x9000; before.d[0] = random_word();
		before.ccr = trial & 31; before.so = trial & 32; before.extend_so = trial & 64;
		nw68_instruction instructions[5];
		for (unsigned n = 0; n < 5; ++n) CHECK(nw68_decode(0x200+n*2,scalar.bus(),instructions[n]));
		uint32_t page = 0; nw68_state expected = before; nw68_frame one, block;
		for (unsigned n = 0; n < 5; ++n) {
			CHECK(nw68_run(instructions[n],expected,scalar.bus(),0,&page,1,one) == NW68_EXIT_NATIVE);
			nw68_commit(one); expected = one.state;
		}
		CHECK(nw68_run_block(instructions,5,before,block_memory.bus(),0,&page,1,block) == NW68_EXIT_NATIVE);
		CHECK(block.nwrite == 2 && block.writes[0].key == block.writes[1].key);
		nw68_commit(block);
		CHECK(!memcmp(expected.d,block.state.d,sizeof expected.d) && expected.ccr == block.state.ccr && expected.pc == block.state.pc);
		CHECK(!memcmp(scalar.ram,block_memory.ram,sizeof scalar.ram));
		fixture invalidated = scalar; invalidated.nested = 3;
		const fixture untouched = invalidated;
		CHECK(nw68_run_block(instructions,5,before,invalidated.bus(),0,&page,1,block) == NW68_EXIT_CODE);
		CHECK(!memcmp(invalidated.ram,untouched.ram,sizeof invalidated.ram));
	}
}

void fault_cache_tests()
{
	for (unsigned test = 0; test < 3; ++test) {
		fixture prefetched; prefetched.snapshot = true;
		prefetched.snapshot_op = test == 0 ? 0x60fe : test == 1 ? 0x7001 : 0x6600;
		prefetched.snapshot_prefetch = test == 2 ? 0 : 0x7002;
		prefetched.set(0x200,2,0x7012); prefetched.set(0x202,2,0x7023); prefetched.set(0x204,2,0x4e71);
		nw68_state s = initial(); s.ccr = 0; nw68_bus bus = prefetched.bus();
		nw68_instruction i; nw68_frame out; uint32_t page = 0;
		CHECK(nw68_decode(s.pc,bus,i));
		CHECK(nw68_run(i,s,bus,0,&page,1,out) == NW68_EXIT_NATIVE);
		CHECK(out.next_op == (test == 0 ? 0x7012 : test == 1 ? 0x7002 : 0x7023));
		CHECK(out.state.pc == (test == 0 ? 0x200u : 0x202u));
	}
	for (int fault = 0; fault < 2; ++fault) {
		fixture f; nw68_state s = initial(), before = s; nw68_frame out;
		f.fail_read = fault; f.set(0x4000,4,0x12345678);
		const nw68_exit e = run(f,s,{0x2298},out); // move.l (a0)+,(a1)
		if (!fault) { CHECK(e == NW68_EXIT_MEMORY); CHECK(!memcmp(&s,&before,sizeof s)); CHECK(f.writes == 0); }
		else CHECK(e == NW68_EXIT_NATIVE && f.writes == 1);
	}
	fixture f; nw68_state s = initial(), before = s; nw68_frame out;
	f.fail_write = 0;
	CHECK(run(f,s,{0x6102},out) == NW68_EXIT_MEMORY); CHECK(f.writes == 0 && !memcmp(&s,&before,sizeof s));
	unsigned direct = 0;
	for (unsigned op = 0; op < 65536; ++op) direct += nw68_opcode_policy(op) == NW68_DIRECT;
	CHECK(direct > 20000 && direct < 65536); CHECK(nw68_opcode_policy(0xa000) == NW68_NANOKERNEL);
	CHECK(nw68_opcode_policy(0x4e73) == NW68_NANOKERNEL); CHECK(nw68_opcode_policy(0xf200) == NW68_NANOKERNEL);
	CHECK(nw68_opcode_policy(0x5008) == NW68_NANOKERNEL); // ADDQ.B An reaches the NK illegal/service handler
	CHECK(nw68_opcode_policy(0x5108) == NW68_NANOKERNEL);
	CHECK(nw68_opcode_policy(0x5048) == NW68_DIRECT && nw68_opcode_policy(0x5088) == NW68_DIRECT);
	s = initial(); CHECK(run(f,s,{0x7001},out) == NW68_EXIT_NATIVE);
	const nw68_cache_stats first = nw68_cache_statistics();
	s = initial(); CHECK(run(f,s,{0x7001},out) == NW68_EXIT_NATIVE);
	CHECK(nw68_cache_statistics().hits > first.hits);
	nw68_invalidate_page(0); s = initial(); CHECK(run(f,s,{0x7002},out) == NW68_EXIT_NATIVE);
	CHECK(s.d[0] == 2 && nw68_cache_statistics().invalidated > first.invalidated);
	{
		nw68_instruction cached[NW68_BLOCK_MAX]; unsigned count = 0;
		CHECK(nw68_cached_block(0x200, 0, 0, 0x7002, 0x4e71, cached, &count));
		CHECK(count == 1 && cached[0].opcode == 0x7002);
		CHECK(!nw68_cached_block(0x200, 0, 0x1000, 0x7002, 0x4e71, cached, &count));
		CHECK(!nw68_cached_block(0x200, 1, 0, 0x7002, 0x4e71, cached, &count));
		CHECK(!nw68_cached_block(0x200, 0, 0, 0x7001, 0x4e71, cached, &count));
		nw68_invalidate_page(0);
		CHECK(!nw68_cached_block(0x200, 0, 0, 0x7002, 0x4e71, cached, &count));
		s = initial(); CHECK(run(f, s, {0x203c,0x1234,0x5678},out) == NW68_EXIT_NATIVE);
		CHECK(nw68_cached_block(0x200,0,0,0x203c,0x1234,cached,&count));
		CHECK(!nw68_cached_block(0x200,0,0,0x203c,0x5678,cached,&count));
		CHECK(cached[0].src.immediate == 0x12345678);
		nw68_invalidate_all();
		CHECK(!nw68_cached_block(0x200,0,0,0x203c,0x1234,cached,&count));
	}
	// Reusing a register block must retain the NK's already-prefetched
	// successor, and copied metadata must produce the same live flags/state.
	{
		fixture block_memory; nw68_state start = initial();
		block_memory.set(0x200,2,0x7002); block_memory.set(0x202,2,0x5280);
		block_memory.set(0x204,2,0x4e71); block_memory.set(0x206,2,0x4e71);
		nw68_instruction fresh[2], cached[NW68_BLOCK_MAX]; unsigned count = 0;
		nw68_bus bus = block_memory.bus(); uint32_t page = 0; nw68_frame original, reused;
		CHECK(nw68_decode(0x200,bus,fresh[0]) && nw68_decode(0x202,bus,fresh[1]));
		CHECK(nw68_run_block(fresh,2,start,bus,0,&page,1,original) == NW68_EXIT_NATIVE);
		CHECK(nw68_cached_block(0x200,0,0,0x7002,0x5280,cached,&count) && count == 2);
		CHECK(!nw68_cached_block(0x200,0,0,0x7002,0x5380,cached,&count));
		CHECK(nw68_run_block(cached,count,start,bus,0,&page,1,reused) == NW68_EXIT_NATIVE);
		CHECK(!memcmp(&original.state,&reused.state,sizeof original.state) &&
		      original.next_op == reused.next_op && original.next_prefetch == reused.next_prefetch);
		// The first block's pruned flags must be restored for scalar retry.
		CHECK(nw68_run(cached[0],start,bus,0,&page,1,reused) == NW68_EXIT_NATIVE);
		CHECK(reused.state.d[0] == 2 && !(reused.state.ccr & 15));
		CHECK(nw68_cached_block(0x200,0,0,0x7002,0x5280,cached,&count) && count == 2);
		nw68_invalidate_page(0);
		CHECK(!nw68_cached_block(0x200,0,0,0x7002,0x5280,cached,&count));
	}
	for (unsigned event = 1; event <= 4; ++event) {
		fixture nested; nested.nested = event; s = initial(); before = s;
		nested.set(0x4000, 4, 0x12345678);
		const nw68_exit e = run(nested, s, {0x2010}, out);
		if (nested.invalidator) { nested.invalidator->join(); delete nested.invalidator; }
		CHECK(e == (event == 1 ? NW68_EXIT_NATIVE : NW68_EXIT_CODE));
		if (event != 1) CHECK(!memcmp(&s, &before, sizeof s) && !nested.writes);
	}
	// A host invalidation between decoding and native entry must reject
	// the decoded old extension words before any state or journal changes.
	{
		fixture changed; s = initial();
		changed.set(s.pc, 2, 0x203c); changed.set(s.pc + 2, 4, 0x12345678);
		nw68_bus bus = changed.bus(); nw68_instruction decoded; uint32_t page = 0;
		const uint64_t generation = nw68_code_generation();
		CHECK(nw68_decode(s.pc, bus, decoded));
		nw68_invalidate_all(); changed.set(s.pc + 2, 4, 0x76543210);
		CHECK(nw68_run(decoded, s, bus, 0, &page, 1, out, generation) == NW68_EXIT_CODE);
		CHECK(!memcmp(&out.state, &s, sizeof s) && !out.nwrite && !changed.writes);
	}
	for (int fault = 0; fault < 16; ++fault) {
		fixture movem_fault; movem_fault.fail_write = fault; s = initial(); before = s;
		CHECK(run(movem_fault,s,{0x48d0,0xffff},out) == NW68_EXIT_MEMORY);
		CHECK(!memcmp(&s,&before,sizeof s) && !movem_fault.writes);
		fixture load_fault; load_fault.fail_read = fault; s = initial(); before = s;
		CHECK(run(load_fault,s,{0x4cd0,0xffff},out) == NW68_EXIT_MEMORY);
		CHECK(!memcmp(&s,&before,sizeof s) && !load_fault.writes);
	}
	fixture self_mod; s = initial(); before = s; s.a[0] = 0x202; before = s;
	CHECK(run(self_mod,s,{0x2080},out) == NW68_EXIT_CODE);
	CHECK(!memcmp(&s,&before,sizeof s) && !self_mod.writes);
	fixture full_ea; s = initial();
	CHECK(run(full_ea,s,{0x2030,0x0100},out) == NW68_EXIT_SERVICE);
	const std::vector<uint16_t> fused[] = {{0x2e9f,0x4e75}, {0x2f38,0x4000,0x4e75}, {0x48e7,0xc000,0x4e5e}};
	for (const auto &words : fused) {
		fixture compound; s = initial(); before = s;
		CHECK(run(compound,s,words,out) == NW68_EXIT_SERVICE);
		CHECK(!memcmp(&s,&before,sizeof s) && !compound.writes);
	}
	// Opcode/extension and both successor prefetch reads: never advance on failure.
	for (unsigned fault = 0; fault < 5; ++fault) {
		fixture prefetch; prefetch.fail_code = fault; s = initial(); before = s;
		CHECK(run(prefetch, s, {0x203c, 0x1234, 0x5678}, out) != NW68_EXIT_NATIVE);
		CHECK(!memcmp(&s, &before, sizeof s) && !prefetch.writes);
	}
	// Fill beyond executable arena capacity and execute again after recycling.
	const uint64_t recycles = nw68_cache_statistics().recycled;
	for (unsigned n = 0; n < 100000; ++n) {
		if (!(n & 255) && nw68_cache_statistics().recycled > recycles) break;
		fixture churn; s = initial(); s.pc = 0x200 + (n % 100) * 4;
		churn.set(s.pc, 2, 0x7001); churn.set(s.pc + 2, 2, 0x4e71); churn.set(s.pc + 4, 2, 0x4e71);
		nw68_instruction i; nw68_bus bus = churn.bus(); uint32_t page = 0;
		CHECK(nw68_decode(s.pc, bus, i)); CHECK(nw68_run(i, s, bus, n, &page, 1, out) == NW68_EXIT_NATIVE);
		CHECK(out.state.d[0] == 1 && out.state.pc == s.pc + 2);
	}
	CHECK(nw68_cache_statistics().recycled > recycles);
	{
		nw68_instruction cached[NW68_BLOCK_MAX]; unsigned count = 0;
		CHECK(!nw68_cached_block(0x200,0,0,0x7002,0x5280,cached,&count));
	}
	printf("68k opcode policy: %u direct candidates, %u exact NanoKernel exits\n",direct,65536-direct);
}
uint32_t ppc_load(void *h,uint32_t ea,uint32_t,int *fault) { uint32_t v; if (!((fixture *)h)->get(ea,4,v)) { *fault=1;return 0; } return v; }
uint32_t ppc_half(void *h,uint32_t ea,uint32_t,int *fault) { uint32_t v; if (!((fixture *)h)->get(ea,2,v)) { *fault=1;return 0; } return v; }
uint32_t ppc_byte(void *h,uint32_t ea,uint32_t,int *fault) { uint32_t v; if (!((fixture *)h)->get(ea,1,v)) { *fault=1;return 0; } return v; }
void ppc_store(void *h,uint32_t ea,uint32_t v,uint32_t,int *fault) { fixture &f=*(fixture *)h; if ((uint64_t)ea+4>sizeof f.ram) {*fault=1;return;}f.set(ea,4,v); }
void ppc_sthalf(void *h,uint32_t ea,uint32_t v,uint32_t,int *fault) { fixture &f=*(fixture *)h; if ((uint64_t)ea+2>sizeof f.ram) {*fault=1;return;}f.set(ea,2,v); }
void ppc_stbyte(void *h,uint32_t ea,uint32_t v,uint32_t,int *fault) { fixture &f=*(fixture *)h; if (ea>=sizeof f.ram) {*fault=1;return;}f.set(ea,1,v); }
bool reference_step(fixture &f, nw68_nk_state &nk)
{
	nw_jit_cpu cpu = {};
	memcpy(cpu.gpr, nk.gpr, sizeof nk.gpr);
	cpu.cr = nk.cr; cpu.xer = nk.xer; cpu.lr = nk.lr; cpu.ctr = nk.ctr; cpu.pc = nk.ppc_pc;
	cpu.host = &f; cpu.mem = f.ram; cpu.mem_size = sizeof f.ram;
	bool left = false;
	for (unsigned step = 0; step < 10000; ++step) {
		if (cpu.pc < 0x68080000u || cpu.pc >= 0x68100000u) left = true;
		if (left && cpu.pc >= 0x68080000u && cpu.pc < 0x68100000u && !(cpu.pc & 7)) {
			memcpy(nk.gpr, cpu.gpr, sizeof nk.gpr);
			nk.cr = cpu.cr; nk.xer = cpu.xer; nk.lr = cpu.lr; nk.ctr = cpu.ctr; nk.ppc_pc = cpu.pc;
			return true;
		}
		uint32_t word;
		if (!f.get(cpu.pc, 4, word) || nw_jit_interp_one(&cpu, word) < 0 || cpu.fault) break;
	}
	return false;
}
void mixed_tests(const fixture &base)
{
	// CCR services and BCD use the real NK between native operations. Compare
	// the entire stream with a second, entirely interpreted NK execution.
	const uint16_t words[] = {0xd001,0xd081,0xe348,0x9041,0xd580,0x42c2,0x44c3,
		0xc101,0x8101,0xe0b9,0x4640,0x023c,0x001f,0xd181,0x4e71,0x4e71};
	for (unsigned trial = 0; trial < 256; ++trial) {
		fixture hybrid = base;
		for (unsigned j = 0; j < sizeof words / sizeof words[0]; ++j) hybrid.set(0x200 + j*2, 2, words[j]);
		fixture interpreted = hybrid;
		nw68_state state = initial(); state.ccr = trial & 31;
		state.so = trial & 32; state.extend_so = trial & 64;
		for (unsigned r = 0; r < 8; ++r) state.d[r] = random_word();
		nw68_nk_state actual = {}; actual.gpr[29] = 0x68080000;
		nw68_export(actual, state, 31, words[0], words[1]);
		actual.gpr[30] = 0x68060000; actual.gpr[31] = 0x1000;
		nw68_nk_state expected = actual;
		for (unsigned step = 0; step < 14; ++step) {
			CHECK(reference_step(interpreted, expected));
			nw68_import(actual, state);
			nw68_instruction instruction; nw68_frame native; nw68_bus bus = hybrid.bus(); uint32_t page = 0;
			const bool direct = !(step & 1) && nw68_decode(state.pc, bus, instruction) &&
				nw68_run(instruction, state, bus, 0, &page, 1, native) == NW68_EXIT_NATIVE;
			if (direct) {
				nw68_commit(native);
				nw68_export(actual, native.state, instruction.flags, native.next_op, native.next_prefetch);
			} else CHECK(reference_step(hybrid, actual));
			nw68_state a, b; nw68_import(actual, a); nw68_import(expected, b);
			const bool match = a.pc == b.pc && a.ccr == b.ccr && a.so == b.so && a.extend_so == b.extend_so &&
				actual.ppc_pc == expected.ppc_pc && actual.gpr[27] == expected.gpr[27] &&
				!memcmp(a.d, b.d, sizeof a.d) && !memcmp(a.a, b.a, sizeof a.a) &&
				!memcmp(hybrid.ram, interpreted.ram, sizeof hybrid.ram);
			if (!match && failed < 24) fprintf(stderr, "mixed handoff trial=%u step=%u direct=%d pc=%x/%x ccr=%x/%x SO=%d/%d savedSO=%d/%d\n",
				trial, step, direct, a.pc, b.pc, a.ccr, b.ccr, a.so, b.so, a.extend_so, b.extend_so);
			CHECK(match);
			if (!match) break;
		}
	}
}
void benchmark(const fixture &base)
{
	fixture f = base;
	const uint16_t words[] = {0x203c,0x7fff,0xffff,0x2200,0x5240,0xd280,0xb041,0x4601,0x4841,0x48c1,0x4e71,0x4e71};
	for (unsigned j = 0; j < sizeof words / sizeof words[0]; ++j) f.set(0x200 + j*2, 2, words[j]);
	nw68_instruction instructions[NW68_BLOCK_MAX]; nw68_bus bus = f.bus(); uint32_t pc = 0x200, page = 0;
	for (unsigned j = 0; j < NW68_BLOCK_MAX; ++j) { CHECK(nw68_decode(pc, bus, instructions[j])); pc += instructions[j].length; }
	nw68_state native_state = initial(); nw68_frame native;
	nw68_nk_state nk = {}; nk.gpr[29] = 0x68080000;
	nw68_export(nk, native_state, 31, words[0], words[1]); nk.gpr[30] = 0x68060000;
	nw_jit_cpu cpu = {}; memcpy(cpu.gpr, nk.gpr, sizeof nk.gpr);
	cpu.cr = nk.cr; cpu.xer = nk.xer; cpu.lr = nk.lr; cpu.host = &f; cpu.mem = f.ram; cpu.mem_size = sizeof f.ram;
	nw_jit_cpu_bind(&cpu);
	const unsigned repeats = 2000000;
	double seconds[2] = {};
	for (unsigned engine = 0; engine < 2; ++engine) {
		const auto start = std::chrono::steady_clock::now();
		for (unsigned iteration = 0; iteration <= repeats; ++iteration) {
			if (!engine) {
				native_state.pc = 0x200;
				if (nw68_run_block(instructions, NW68_BLOCK_MAX, native_state, bus, 0, &page, 1, native) != NW68_EXIT_NATIVE) { CHECK(false); return; }
				native_state = native.state;
			} else {
				cpu.pc = 0x68080000 + ((uint32_t)words[0] << 3); cpu.gpr[29] = cpu.pc;
				cpu.gpr[24] = 0x202; cpu.gpr[27] = words[1]; cpu.lr = cpu.pc;
				unsigned budget = 1000;
				while (budget-- && !(cpu.pc >= 0x68080000 && cpu.pc < 0x68100000 && !(cpu.pc & 7) && cpu.gpr[24] == pc + 2)) {
					int n = 0;
					nw_jit_fn fn = nw_jit_cache_get(cpu.pc & ~4095u, cpu.pc, 0, 0, &n);
					if (!fn) {
						uint32_t ops[NW_JIT_MAX_BLOCK];
						while (n < NW_JIT_MAX_BLOCK && !((cpu.pc + n*4 ^ cpu.pc) & ~4095u) &&
						       f.get(cpu.pc + n*4, 4, ops[n]) && nw_jit_op_supported(ops[n])) {
							if (nw_jit_op_ends_block(ops[n++])) break;
						}
						if (n) fn = nw_jit_compile(ops, n, cpu.pc, cpu.pc & ~4095u, 0, 0);
					}
					if (fn && fn != NW_JIT_INTERPRET) fn(&cpu);
					else { uint32_t word; if (!f.get(cpu.pc,4,word) || nw_jit_interp_one(&cpu,word) < 0) { CHECK(false); return; } }
					if (cpu.fault) { CHECK(false); return; }
				}
				if (!(cpu.pc >= 0x68080000 && cpu.pc < 0x68100000 && !(cpu.pc & 7) && cpu.gpr[24] == pc + 2)) { CHECK(false); return; }
			}
		}
		seconds[engine] = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	}
	memcpy(nk.gpr, cpu.gpr, sizeof nk.gpr); nk.cr = cpu.cr; nk.xer = cpu.xer;
	nw68_state reference; nw68_import(nk, reference);
	CHECK(reference.pc == native_state.pc && reference.ccr == native_state.ccr && reference.so == native_state.so &&
		reference.extend_so == native_state.extend_so && !memcmp(reference.d, native_state.d, sizeof reference.d));
	printf("68k register workload: %u x 8 instructions; native %.6fs, PPC-JIT NanoKernel %.6fs, ratio %.3f (isolated harness, includes cache lookup; not app speed)\n",
		repeats + 1, seconds[0], seconds[1], seconds[1] / seconds[0]);
}
void rom_tests(const char *path)
{
	fixture base; FILE *f=fopen(path,"rb"); if (!f) { perror(path); ++failed; return; }
	base.rom->resize(4<<20); CHECK(fread(base.rom->data(),1,base.rom->size(),f)==base.rom->size()); fclose(f);
	nw_jit_set_host_mem(ppc_load,ppc_store); nw_jit_set_host_half(ppc_half,ppc_sthalf); nw_jit_set_host_byte(ppc_byte,ppc_stbyte);
	mixed_tests(base);
	rng_state = 0x68c0ffee; // ROM vectors stay stable as other regressions grow.
	std::vector<uint16_t> cases;
	for (unsigned op = 0; op < 65536; ++op) if (nw68_opcode_policy(op) == NW68_DIRECT) {
		uint32_t word;
		CHECK(base.get(0x68080004u + op*8,4,word));
		int32_t displacement = word & 0x03fffffcu;
		if (displacement & 0x02000000) displacement |= (int32_t)0xfc000000u;
		const uint32_t target = (word & 2) ? (uint32_t)displacement : 0x68080004u + op*8 + (uint32_t)displacement;
		// A candidate that actually dispatches the NK exception/service
		// trampoline is not validated by merely skipping a failed reference.
		CHECK((word >> 26) != 18 || target != 0x6806d750u);
		cases.push_back(op);
	}
	for(uint16_t op:cases) for(unsigned trial=0;trial<4;++trial) {
		fixture native=base; nw68_state s=initial();
		for(unsigned j=0;j<8;++j){s.d[j]=random_word();s.a[j]=0x4000+j*0x100;} s.ccr=random_word()&31;
		// These NK bookkeeping bits are independent of architectural CCR.V/X.
		s.so = trial & 1; s.extend_so = trial & 2;
		s.a[0]=trial == 3 ? 0x4ffe : 0x4000 + (trial >= 2 ? 2 : 0);
		s.a[1]=0x5000 + (trial >= 2 ? 2 : 0);s.a[6]=0x6000 + (trial >= 2 ? 2 : 0);s.a[7]=0x8000 + (trial >= 2 ? 2 : 0);
		for(unsigned ea=0x3ff0;ea<0x4010;ea+=4)native.set(ea,4,random_word());
		native.set(s.a[6],4,0x7000);native.set(s.a[7],4,0x208);
		const uint16_t ext=op==0x4e56 ? 0xfff0 : op==0x51c8 ? 6 : 0x0010;
		native.set(0x200,2,op);native.set(0x202,2,ext);
		for(unsigned ea=0x204;ea<0x220;ea+=2)native.set(ea,2,0x4e71);
		fixture reference=native; nw68_bus bus=native.bus(); nw68_instruction i;
		if(!nw68_decode(0x200,bus,i)) {++rom_skipped;continue;}
		nw68_frame predicted; uint32_t page=0;
		if(nw68_run(i,s,bus,0,&page,1,predicted)!=NW68_EXIT_NATIVE){++rom_skipped;continue;}
		nw68_nk_state nk={};nk.gpr[29]=0x68080000; nw68_export(nk,s,31,op,ext);nk.gpr[0]=0;nk.gpr[23]=0;nk.gpr[30]=0x68060000;nk.gpr[31]=0x1000;
		nw_jit_cpu cpu={};memcpy(cpu.gpr,nk.gpr,sizeof nk.gpr);cpu.cr=nk.cr;cpu.xer=nk.xer;cpu.lr=nk.lr;cpu.ctr=nk.ctr;cpu.pc=nk.ppc_pc;cpu.host=&reference;
		cpu.mem=reference.ram;cpu.mem_size=sizeof reference.ram;
		const bool initial_so = (cpu.xer & 0x80000000u) != 0;
		bool left=false,done=false; unsigned step=0;
		for(;step<10000;++step) {
			if(cpu.pc<0x68080000u||cpu.pc>=0x68100000u)left=true;
			if(left&&cpu.pc>=0x68080000u&&cpu.pc<0x68100000u&&!(cpu.pc&7)){done=true;break;}
			uint32_t word;if(!reference.get(cpu.pc,4,word)||nw_jit_interp_one(&cpu,word)<0||cpu.fault)break;
		}
		if(!done){if(rom_skipped<3)fprintf(stderr,"reference stop op=%04x pc=%08x steps=%u fault=%u\n",op,cpu.pc,step,cpu.fault);++rom_skipped;continue;}
		memcpy(nk.gpr,cpu.gpr,sizeof nk.gpr);nk.cr=cpu.cr;nk.xer=cpu.xer;nk.lr=cpu.lr;nk.ctr=cpu.ctr;nk.ppc_pc=cpu.pc;
		nw68_state actual;nw68_import(nk,actual);nw68_commit(predicted);
		const bool expected_so = predicted.state.so;
		const bool match=actual.pc==predicted.state.pc&&actual.ccr==predicted.state.ccr&&!memcmp(actual.d,predicted.state.d,sizeof actual.d)&&!memcmp(actual.a,predicted.state.a,sizeof actual.a)&&!memcmp(reference.ram,native.ram,sizeof native.ram) && expected_so == ((cpu.xer & 0x80000000u) != 0) && actual.extend_so == predicted.state.extend_so &&
			predicted.next_op == (uint16_t)((cpu.pc - 0x68080000u) >> 3) &&
			cpu.gpr[27] == (uint32_t)(int32_t)(int16_t)predicted.next_prefetch;
		if(!match) ++family_failed[i.operation];
		if(!match&&family_failed[i.operation]<9){fprintf(stderr,"ROM mismatch op=%04x family=%s trial=%u pc=%x/%x ccr=%x/%x SO=%d+%d/%d src=%x dst=%x\n",op,nw68_operation_name(i.operation),trial,actual.pc,predicted.state.pc,actual.ccr,predicted.state.ccr,initial_so,predicted.so_raised,(cpu.xer>>31)!=0,predicted.src,predicted.dst);for(unsigned r=0;r<8;++r)if(actual.d[r]!=predicted.state.d[r]||actual.a[r]!=predicted.state.a[r])fprintf(stderr," r%u D=%x/%x A=%x/%x\n",r,actual.d[r],predicted.state.d[r],actual.a[r],predicted.state.a[r]);}
		CHECK(match);++rom_run;
	}
	printf("NanoKernel ROM differential: %u compared, %u unsupported reference/unsafe cases skipped\n",rom_run,rom_skipped);
	for(unsigned j=0;j<64;++j)if(family_failed[j])fprintf(stderr,"family %s failures=%u\n",nw68_operation_name((nw68_operation)j),family_failed[j]);
	CHECK(rom_run>=1000);
	if (benchmark_requested) benchmark(base);
	nw_jit_set_host_mem(0,0);nw_jit_set_host_half(0,0);nw_jit_set_host_byte(0,0);
}
}
int main(int argc,char **argv)
{
	const char *rom = 0;
	for (int j = 1; j < argc; ++j) {
		if (!strcmp(argv[j], "--benchmark")) benchmark_requested = true;
		else if (!strcmp(argv[j], "--policy") && j + 1 < argc) policy_path = argv[++j];
		else rom = argv[j];
	}
	page_tests();state_tests();arithmetic_tests();memory_control_tests();block_tests();register_branch_tests();mixed_block_tests();alias_block_tests();fault_cache_tests();
	if (rom) rom_tests(rom);
	if (policy_path) {
		FILE *csv = fopen(policy_path, "w");
		if (!csv) { perror(policy_path); CHECK(false); }
		else {
			fputs("opcode,zero_extension_candidate,family,execution_policy\n", csv);
			fixture f; nw68_bus bus = f.bus();
			for (unsigned op = 0; op < 65536; ++op) {
				f.set(0x200, 2, op); nw68_instruction i;
				const bool direct = nw68_decode(0x200, bus, i);
				fprintf(csv, "%04x,%u,%s,%s\n", op, direct, direct ? nw68_operation_name(i.operation) : "service",
					direct ? "native_if_runtime_guards_pass_else_exact_NK" : "exact_NK");
			}
			CHECK(fclose(csv) == 0);
		}
	}
	printf("New World 68k tests: %u passed, %u failed\n",passed,failed);
	return failed ? 1 : 0;
}
