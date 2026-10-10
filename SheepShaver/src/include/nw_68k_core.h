/* New World direct 68k translator. No dependency on the PPC CPU or host UI. */
#ifndef NW_68K_CORE_H
#define NW_68K_CORE_H
#include <stdint.h>
#include <stddef.h>

enum { NW68_C = 1, NW68_V = 2, NW68_Z = 4, NW68_N = 8, NW68_X = 16 };
struct nw68_state {
	uint32_t d[8], a[8], pc;
	uint8_t ccr;
	/* NK's PPC SO and saved-X SO are live across long ADDX handlers. */
	bool so, extend_so;
};
/* Raw NK boundary state. Scratch registers are retained on non-native exits. */
struct nw68_nk_state {
	uint32_t gpr[32], cr, xer, lr, ctr, ppc_pc;
};
void nw68_import(const nw68_nk_state &, nw68_state &);
void nw68_export(nw68_nk_state &, const nw68_state &, unsigned flag_mask,
		 uint16_t next_op, uint16_t next_prefetch);

enum nw68_operation {
	NW68_SERVICE, NW68_NOP, NW68_MOVE, NW68_MOVEA, NW68_ADD, NW68_SUB,
	NW68_CMP, NW68_ADDA, NW68_SUBA, NW68_CMPA, NW68_AND, NW68_OR,
	NW68_EOR, NW68_CLR, NW68_TST, NW68_NOT, NW68_NEG, NW68_EXT,
	NW68_SWAP, NW68_EXG, NW68_LEA, NW68_PEA, NW68_BRANCH, NW68_DBCC,
	NW68_SCC, NW68_JSR, NW68_JMP, NW68_RTS, NW68_LINK, NW68_UNLK,
	NW68_MOVEM, NW68_BIT, NW68_SHIFT, NW68_MUL, NW68_DIV,
	NW68_ADDX, NW68_SUBX, NW68_NEGX
};
enum nw68_policy { NW68_DIRECT, NW68_NANOKERNEL };
struct nw68_ea {
	uint8_t mode, reg, index_reg, index_scale;
	bool index_address, index_long;
	int32_t displacement;
	uint32_t absolute, immediate, pc_base;
};
struct nw68_instruction {
	nw68_operation operation;
	nw68_ea src, dst;
	uint32_t pc, immediate;
	int32_t displacement;
	uint16_t opcode, words[16], reg_mask;
	uint8_t width, length, word_count, condition, subop, flags, flags_live;
	bool reverse, control;
};
/* Code reads may return already-prefetched words. Data reads must not. */
struct nw68_bus {
	void *opaque;
	bool (*code)(void *, uint32_t, uint16_t *, uint32_t *physical);
	bool (*read)(void *, uint32_t, unsigned, uint32_t *);
	bool (*probe_write)(void *, uint32_t, unsigned);
	void (*write)(void *, uint32_t, unsigned, uint32_t);
	/* Taken control transfers refetch targets rather than reusing the
	 * initial NK opcode/prefetch snapshot. Null uses code() for fixtures. */
	bool (*fetch)(void *, uint32_t, uint16_t *, uint32_t *physical);
	/* Stable contiguous byte key for journal forwarding through aliases.
	 * Null uses the guest address for simple identity-mapped fixtures. */
	bool (*resolve)(void *, uint32_t, unsigned, bool store, uint32_t *physical);
};
/* Dispatch-local page translations. The owner guarantees a fixed MMU
 * context and no callbacks/committed stores during preparation. Read/write
 * permissions have separate entries; nothing survives the dispatch. */
struct nw68_page_cache {
	struct mapping { uint32_t ea, pa; bool store; } pages[16];
	unsigned count;
};
typedef bool (*nw68_page_probe)(void *, uint32_t, bool, uint32_t *);
bool nw68_page_translate(nw68_page_cache &, void *, nw68_page_probe,
			uint32_t ea, bool store, uint32_t *pa);
/* The same lookup, inlined for the dispatcher (the call was most of its cost: a short scan of at most sixteen entries). */
static inline bool nw68_page_translate_fast(nw68_page_cache &cache, void *opaque, nw68_page_probe probe,
			uint32_t ea, bool store, uint32_t *pa)
{
	const uint32_t page = ea & ~4095u;
	for (unsigned j = 0; j < cache.count; ++j) {
		const nw68_page_cache::mapping &m = cache.pages[j];
		if (m.ea == page && m.store == store) { *pa = m.pa | (ea & 4095u); return true; }
	}
	if (!probe || !probe(opaque, ea, store, pa)) return false;
	if (cache.count < 16) cache.pages[cache.count++] = {page, *pa & ~4095u, store};
	return true;
}
bool nw68_hash_table_overlap(uint32_t pa, unsigned width, uint32_t sdr1);
bool nw68_decode(uint32_t pc, const nw68_bus &, nw68_instruction &);
nw68_policy nw68_opcode_policy(uint16_t opcode);
bool nw68_condition(unsigned condition, uint8_t ccr);
const char *nw68_operation_name(nw68_operation);

enum nw68_exit {
	NW68_EXIT_NATIVE, NW68_EXIT_SERVICE, NW68_EXIT_MEMORY,
	NW68_EXIT_CODE, NW68_EXIT_REENTRY, NW68_EXIT_UNAVAILABLE,
	NW68_EXIT_VERIFIED, NW68_EXIT_VERIFY_SKIPPED
};
struct nw68_write { uint32_t ea, value, key; uint8_t width; };
struct nw68_frame {
	nw68_state state;
	nw68_bus bus;
	const nw68_instruction *instruction;
	uint32_t src, dst, result, destination;
	uint16_t next_op, next_prefetch;
	nw68_write writes[32];
	uint8_t nwrite;
	uint8_t completed;
	bool destination_memory;
	bool so_raised;
	bool refetch;
	nw68_exit exit;
};
/* Generated arithmetic runs between these two ordered, side-effect-free
 * preparation/finish helpers. Writes remain journaled until commit(). */
int nw68_prepare(nw68_frame *, const nw68_instruction *);
int nw68_finish(nw68_frame *);
void nw68_commit(const nw68_frame &);
nw68_exit nw68_run(const nw68_instruction &, const nw68_state &,
		  const nw68_bus &, uint32_t context, const uint32_t *pages,
		  unsigned npages, nw68_frame &, uint64_t generation = 0);
enum { NW68_BLOCK_MAX = 8 };
bool nw68_register_instruction(const nw68_instruction &);
nw68_exit nw68_run_block(const nw68_instruction *, unsigned count,
		 const nw68_state &, const nw68_bus &, uint32_t context,
		 const uint32_t *pages, unsigned npages, nw68_frame &, uint64_t generation = 0);
/* Capture before decoding. An invalidation on any thread cancels a private
 * result prepared against older code, including while the cache lock is held. */
uint64_t nw68_code_generation();
/* Copy decoded metadata from a live, page-owned compiled entry. No pointer
 * into the arena/cache escapes. Callers must probe the current code mapping
 * and pass the generation captured before this lookup to run_block(). */
bool nw68_cached_block(uint32_t pc, uint32_t context, uint32_t physical_page,
		 uint16_t opcode, uint16_t prefetch, nw68_instruction *, unsigned *count);
/* What a cached run reports about the block it ran. */
struct nw68_cached_run {
	unsigned count, flag_mask;
	uint16_t opcodes[NW68_BLOCK_MAX];
	uint32_t pcs[NW68_BLOCK_MAX];
};
/* nw68_cached_block + flag union + nw68_run_block in one call: one lock, no copy of the decoded block, and no rehash of
 * its words (the entry was just validated by its decode-slot lookup, and no entry can change between the two steps:
 * only this thread adds or replaces entries, and any invalidation on another thread bumps the generation, which is
 * checked under the lock). Returns false, having run nothing, when there is no matching entry (or the generation moved,
 * or a run is already in progress); the caller then takes the decode path. When it returns true, f.exit says how the
 * block ended, and f.instruction is null. */
bool nw68_run_cached(uint32_t pc, uint32_t context, uint32_t physical_page, uint16_t opcode, uint16_t prefetch,
		 const nw68_state &, const nw68_bus &, nw68_frame &, uint64_t generation, nw68_cached_run *);
void nw68_invalidate_page(uint32_t physical_page);
int nw68_page_has_code(uint32_t physical_page);	/* a translated 68k block was built from this page */
void nw68_invalidate_all();
/* Mapping changes invalidate an in-flight preparation. Dispatch-local
 * previews never survive a context transition or expose a host pointer. */
void nw68_context_changed();
void nw68_shutdown();
struct nw68_cache_stats { uint64_t compiled, hits, invalidated, recycled, blocks, block_instructions, decoded_hits; };
nw68_cache_stats nw68_cache_statistics();
#endif
