#include "nw_68k_core.h"
#include <limits.h>

bool nw68_page_translate(nw68_page_cache &cache, void *opaque, nw68_page_probe probe,
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
bool nw68_hash_table_overlap(uint32_t pa, unsigned width, uint32_t sdr1)
{
	const uint32_t base = sdr1 & 0xffff0000u;
	const uint32_t last = base | ((sdr1 & 0x1ffu) << 16) | 0xffffu;
	return width && (uint64_t)pa + width > base && pa <= last;
}

namespace {
uint32_t mask(unsigned w) { return w == 4 ? UINT32_MAX : (1u << (8 * w)) - 1u; }
uint32_t sign(unsigned w) { return 1u << (8 * w - 1); }
int64_t signed_value(uint32_t v, unsigned w) {
	return w == 1 ? (int8_t)v : w == 2 ? (int16_t)v : (int32_t)v;
}
uint32_t &reg(nw68_state &s, unsigned r) { return r < 8 ? s.d[r] : s.a[r - 8]; }
bool read(nw68_frame &f, uint32_t ea, unsigned w, uint32_t &v) {
	if ((uint64_t)ea + w > (uint64_t)UINT32_MAX + 1 || !f.bus.read || !f.bus.read(f.bus.opaque, ea, w, &v)) {
		f.exit = NW68_EXIT_MEMORY; return false;
	}
	/* Forward staged bytes, so overlapping accesses observe program order. */
	uint32_t key = ea;
	if (f.bus.resolve && !f.bus.resolve(f.bus.opaque, ea, w, false, &key)) { f.exit = NW68_EXIT_MEMORY; return false; }
	for (unsigned j = 0; j < w; ++j) {
		for (unsigned k = 0; k < f.nwrite; ++k) {
			const nw68_write &x = f.writes[k];
			if (key + j >= x.key && key + j - x.key < x.width) {
				const unsigned sh = (w - j - 1) * 8, xs = (x.width - (key + j - x.key) - 1) * 8;
				v = (v & ~(255u << sh)) | (((x.value >> xs) & 255u) << sh);
			}
		}
	}
	return true;
}
bool stage(nw68_frame &f, uint32_t ea, unsigned w, uint32_t v) {
	if (f.nwrite == 32 || (uint64_t)ea + w > (uint64_t)UINT32_MAX + 1 ||
	    !f.bus.probe_write || !f.bus.probe_write(f.bus.opaque, ea, w)) {
		f.exit = NW68_EXIT_MEMORY; return false;
	}
	uint32_t key = ea;
	if (f.bus.resolve && !f.bus.resolve(f.bus.opaque, ea, w, true, &key)) { f.exit = NW68_EXIT_MEMORY; return false; }
	nw68_write &x = f.writes[f.nwrite++]; x.ea = ea; x.key = key; x.width = w; x.value = v & mask(w);
	return true;
}
bool address(nw68_frame &f, const nw68_ea &e, unsigned w, uint32_t &ea) {
	const unsigned step = w == 1 && e.reg == 7 ? 2 : w;
	switch (e.mode) {
	case 2: ea = f.state.a[e.reg]; return true;
	case 3: ea = f.state.a[e.reg]; f.state.a[e.reg] += step; return true;
	case 4: f.state.a[e.reg] -= step; ea = f.state.a[e.reg]; return true;
	case 5: ea = f.state.a[e.reg] + (uint32_t)e.displacement; return true;
	case 6: case 7:
		if (e.mode == 7 && e.reg <= 1) { ea = e.absolute; return true; }
		if (e.mode == 7 && e.reg == 2) { ea = e.pc_base + (uint32_t)e.displacement; return true; }
		if (e.mode == 6 || (e.mode == 7 && e.reg == 3)) {
			uint32_t index = e.index_address ? f.state.a[e.index_reg] : f.state.d[e.index_reg];
			if (!e.index_long) index = (uint32_t)(int32_t)(int16_t)index;
			ea = (e.mode == 6 ? f.state.a[e.reg] : e.pc_base) + (uint32_t)e.displacement + (index << e.index_scale);
			return true;
		}
	}
	f.exit = NW68_EXIT_SERVICE; return false;
}
bool operand(nw68_frame &f, const nw68_ea &e, unsigned w, uint32_t &v, uint32_t *dest = 0) {
	if (e.mode == 0 || e.mode == 1) {
		v = (e.mode == 0 ? f.state.d[e.reg] : f.state.a[e.reg]) & mask(w);
		if (dest) *dest = e.reg; return true;
	}
	if (e.mode == 7 && e.reg == 4) { v = e.immediate & mask(w); return true; }
	uint32_t ea;
	if (!address(f, e, w, ea)) return false;
	if (dest) *dest = ea;
	return read(f, ea, w, v);
}
bool destination(nw68_frame &f, bool needs_read) {
	const nw68_instruction &i = *f.instruction;
	f.destination_memory = i.dst.mode > 1;
	if (!f.destination_memory) {
		f.destination = i.dst.reg;
		f.dst = i.dst.mode == 0 ? f.state.d[i.dst.reg] : f.state.a[i.dst.reg];
		return true;
	}
	if (!address(f, i.dst, i.width, f.destination)) return false;
	return !needs_read || read(f, f.destination, i.width, f.dst);
}
bool put(nw68_frame &f, unsigned width, uint32_t value) {
	const nw68_instruction &i = *f.instruction;
	if (f.destination_memory) return stage(f, f.destination, width, value);
	uint32_t &dst = i.dst.mode == 0 ? f.state.d[f.destination] : f.state.a[f.destination];
	dst = (dst & ~mask(width)) | (value & mask(width)); return true;
}
bool push(nw68_frame &f, uint32_t value) { f.state.a[7] -= 4; return stage(f, f.state.a[7], 4, value); }
void flags(nw68_frame &f, unsigned width, bool carry, bool overflow, bool cumulative) {
	const nw68_instruction &i = *f.instruction;
	if (i.operation == NW68_ADDX && width == 4) f.state.so = f.state.extend_so;
	if (i.operation == NW68_DIV && i.subop) f.state.so = false;
	if (overflow && (i.flags & NW68_V)) { f.state.so = true; f.so_raised = true; }
	if (i.flags & NW68_X) {
		const bool captured = i.operation == NW68_ADDX || i.operation == NW68_ADD;
		f.state.extend_so = captured ? f.state.so : carry;
	}
	if (!i.flags_live) return;
	const uint32_t r = f.result & mask(width);
	uint8_t next = (r & sign(width) ? NW68_N : 0) | (!r ? NW68_Z : 0) |
		(carry ? NW68_C : 0) | (overflow ? NW68_V : 0) | (carry ? NW68_X : 0);
	if (cumulative && !(f.state.ccr & NW68_Z)) next &= ~NW68_Z;
	f.state.ccr = (f.state.ccr & ~i.flags_live) | (next & i.flags_live);
}
bool code_finish(nw68_frame &f) {
	if ((f.state.pc & 1) || f.state.pc > UINT32_MAX - 3u) { f.exit = NW68_EXIT_CODE; return false; }
	/* A write overlapping any current/next instruction word goes to the
	 * NK before effects. It owns that instruction's prefetch/store ordering. */
	for (unsigned j = 0; j < f.nwrite; ++j) {
		const nw68_write &w = f.writes[j];
		const uint64_t end = (uint64_t)w.ea + w.width;
		if ((w.ea < (uint64_t)f.instruction->pc + f.instruction->length && end > f.instruction->pc) ||
		    (w.ea < (uint64_t)f.state.pc + 4 && end > f.state.pc)) {
			f.exit = NW68_EXIT_CODE; return false;
		}
	}
	uint32_t pa;
	auto fetch = f.refetch && f.bus.fetch ? f.bus.fetch : f.bus.code;
	if (!fetch || !fetch(f.bus.opaque, f.state.pc, &f.next_op, &pa) ||
	    !fetch(f.bus.opaque, f.state.pc + 2u, &f.next_prefetch, &pa)) {
		f.exit = NW68_EXIT_CODE; return false;
	}
	// These NK compound handlers omit a dead stack store/update. Keep the
	// real fused semantics until the corresponding compound native form is
	// implemented; reject here before any journaled effect can be committed.
	const uint16_t op = f.instruction->opcode;
	if (((op == 0x2f38 || op == 0x2e9f) && f.next_op == 0x4e75) ||
	    (f.instruction->operation == NW68_MOVEM && f.next_op == 0x4e5e)) {
		f.exit = NW68_EXIT_SERVICE; return false;
	}
	f.exit = NW68_EXIT_NATIVE; return true;
}
bool movem(nw68_frame &f) {
	const nw68_instruction &i = *f.instruction;
	uint32_t ea;
	if (i.dst.mode == 4) {
		/* NK model-specific base-in-list value is deliberately delegated. */
		if (i.reg_mask & (1u << (7 - i.dst.reg))) { f.exit = NW68_EXIT_SERVICE; return false; }
		ea = f.state.a[i.dst.reg];
	} else if (i.dst.mode == 3) ea = f.state.a[i.dst.reg];
	else if (!address(f, i.dst, i.width, ea)) return false;
	for (unsigned bit = 0; bit < 16; ++bit) {
		if (!(i.reg_mask & (1u << bit))) continue;
		const unsigned r = i.dst.mode == 4 ? 15 - bit : bit;
		if (i.dst.mode == 4) ea -= i.width;
		if (i.reverse) {
			uint32_t value; if (!read(f, ea, i.width, value)) return false;
			reg(f.state, r) = i.width == 2 ? (uint32_t)(int32_t)(int16_t)value : value;
		} else if (!stage(f, ea, i.width, reg(f.state, r))) return false;
		if (i.dst.mode != 4) ea += i.width;
	}
	if (i.dst.mode == 3 || i.dst.mode == 4) f.state.a[i.dst.reg] = ea;
	return true;
}
bool shift(nw68_frame &f) {
	const nw68_instruction &i = *f.instruction;
	const unsigned n = i.reverse ? f.state.d[i.condition] & 63 : i.immediate;
	const unsigned bits = i.width * 8; const uint32_t m = mask(i.width), s = sign(i.width);
	const bool old_extend_so = f.state.extend_so;
	uint32_t v = f.src & m; bool c = false, overflow = false, x = f.state.ccr & NW68_X;
	for (unsigned k = 0; k < n; ++k) {
		const bool left = i.subop & 1; const bool out = left ? (v & s) != 0 : (v & 1) != 0;
		const uint32_t before = v;
		if (left) v = (v << 1) & m; else v >>= 1;
		if (i.subop < 2 && !left) v |= before & s; // ASR
		else if (i.subop >= 4 && i.subop < 6) { // ROX
			if (x) v |= left ? 1u : s; x = out;
		} else if (i.subop >= 6) { if (out) v |= left ? 1u : s; }
		if (i.subop == 1 && ((before ^ v) & s)) overflow = true;
		c = out;
	}
	if (i.subop >= 4 && i.subop < 6 && !n) c = x;
	f.result = v; flags(f, i.width, c, overflow, false);
	const bool keeps_so = (!n && i.subop < 4) || i.subop == 3 ||
		(i.subop == 1 && !i.reverse && n == 1) || (i.subop >= 6 && (i.width == 4 || !n));
	if (!keeps_so) { f.state.so = false; f.so_raised = false; }
	if (i.subop < 6) {
		if (!n && i.subop < 4) f.state.extend_so = old_extend_so;
		else f.state.extend_so = keeps_so ? f.state.so : false;
	}
	if (!n && i.subop < 4) f.state.ccr = (f.state.ccr & ~NW68_X) | (x ? NW68_X : 0);
	if (i.subop >= 4 && i.subop < 6) f.state.ccr = (f.state.ccr & ~NW68_X) | (x ? NW68_X : 0);
	(void)bits; return true;
}
}

int nw68_prepare(nw68_frame *p, const nw68_instruction *instruction)
{
	nw68_frame &f = *p; const nw68_instruction &i = *instruction;
	f.instruction = instruction; f.exit = NW68_EXIT_SERVICE;
	f.refetch = false;
	f.state.pc = i.pc + i.length;
	uint32_t ea, value;
	switch (i.operation) {
	case NW68_SERVICE: return 0;
	case NW68_NOP: return 1;
	case NW68_BRANCH:
		if (i.condition == 1 && !push(f, f.state.pc)) return 0;
		if (i.condition <= 1 || nw68_condition(i.condition, f.state.ccr)) {
			f.state.pc = i.pc + 2u + (uint32_t)i.displacement; f.refetch = true;
		}
		return 1;
	case NW68_DBCC:
		if (!nw68_condition(i.condition, f.state.ccr)) {
			uint32_t &r = f.state.d[i.dst.reg]; const uint16_t n = (uint16_t)(r - 1u);
			r = (r & 0xffff0000u) | n;
			if (n != 0xffffu) { f.state.pc = i.pc + 2u + (uint32_t)i.displacement; f.refetch = true; }
		} return 1;
	case NW68_LEA: case NW68_PEA: case NW68_JSR: case NW68_JMP:
		if (!address(f, i.src, 4, ea)) return 0;
		if (i.operation == NW68_LEA) f.state.a[i.dst.reg] = ea;
		else if (i.operation == NW68_PEA) { if (!push(f, ea)) return 0; }
		else {
			if (i.operation == NW68_JSR && !push(f, f.state.pc)) return 0;
			f.state.pc = ea; f.refetch = true;
		} return 1;
	case NW68_RTS:
		if (!read(f, f.state.a[7], 4, ea)) return 0;
		f.state.a[7] += 4; f.state.pc = ea; f.refetch = true; return 1;
	case NW68_LINK:
		if (i.dst.reg == 7) return 0;
		if (!push(f, f.state.a[i.dst.reg])) return 0;
		f.state.a[i.dst.reg] = f.state.a[7]; f.state.a[7] += (uint32_t)i.displacement; return 1;
	case NW68_UNLK:
		if (i.dst.reg == 7) return 0;
		ea = f.state.a[i.dst.reg]; if (!read(f, ea, 4, value)) return 0;
		f.state.a[7] = ea + 4; f.state.a[i.dst.reg] = value; return 1;
	case NW68_MOVEM: return movem(f);
	case NW68_EXG:
		value = i.src.mode == 0 ? f.state.d[i.src.reg] : f.state.a[i.src.reg];
		if (i.src.mode == 0) f.state.d[i.src.reg] = i.dst.mode == 0 ? f.state.d[i.dst.reg] : f.state.a[i.dst.reg];
		else f.state.a[i.src.reg] = i.dst.mode == 0 ? f.state.d[i.dst.reg] : f.state.a[i.dst.reg];
		if (i.dst.mode == 0) f.state.d[i.dst.reg] = value; else f.state.a[i.dst.reg] = value;
		return 1;
	case NW68_CLR: case NW68_SCC:
		if (!destination(f, false)) return 0;
		f.result = i.operation == NW68_SCC && nw68_condition(i.condition, f.state.ccr) ? 255 : 0;
		return 1;
	case NW68_NEG: case NW68_NEGX: case NW68_NOT: case NW68_TST:
		if (!destination(f, true)) return 0;
		f.src = f.dst & mask(i.width); f.dst = 0; return 1;
	default:
		if (!operand(f, i.src, i.width, f.src)) return 0;
		if (!destination(f, i.operation != NW68_MOVE && i.operation != NW68_MOVEA)) return 0;
		if (i.operation == NW68_ADDA || i.operation == NW68_SUBA || i.operation == NW68_CMPA || i.operation == NW68_MOVEA)
			if (i.width == 2) f.src = (uint32_t)(int32_t)(int16_t)f.src;
		if (i.operation == NW68_BIT) f.src &= i.width == 4 ? 31 : 7;
		if (i.operation == NW68_SHIFT) return shift(f);
		if (i.operation == NW68_MUL) {
			f.result = i.subop ? (uint32_t)((int32_t)(int16_t)f.src * (int32_t)(int16_t)f.dst) :
				(uint32_t)(uint16_t)f.src * (uint16_t)f.dst;
		} else if (i.operation == NW68_DIV) {
			if (!(f.src & 0xffffu)) return 0;
			int64_t q, r;
			if (i.subop) {
				q = (int64_t)(int32_t)f.dst / (int16_t)f.src; r = (int64_t)(int32_t)f.dst % (int16_t)f.src;
				if (q < -32768 || q > 32767) return 0;
			} else {
				q = f.dst / (uint16_t)f.src; r = f.dst % (uint16_t)f.src;
				if (q > 65535) return 0;
			}
			f.result = ((uint32_t)(uint16_t)r << 16) | (uint16_t)q;
		}
		return 1;
	}
}

int nw68_finish(nw68_frame *p)
{
	nw68_frame &f = *p; const nw68_instruction &i = *f.instruction;
	unsigned w = i.width;
	if (i.operation == NW68_MOVEA || i.operation == NW68_ADDA || i.operation == NW68_SUBA || i.operation == NW68_CMPA || i.operation == NW68_MUL || i.operation == NW68_DIV) w = 4;
	const uint32_t m = mask(w), a = f.dst & m, b = f.src & m;
	f.result &= m;
	switch (i.operation) {
	case NW68_MOVE: case NW68_CLR: case NW68_NOT: case NW68_EXT: case NW68_SWAP: case NW68_TST:
		flags(f, w, false, false, false); break;
	case NW68_ADD: case NW68_ADDA: case NW68_ADDX: {
		const unsigned x = i.operation == NW68_ADDX && (f.state.ccr & NW68_X) ? 1 : 0;
		const uint64_t sum = (uint64_t)a + b + x;
		const int64_t signed_sum = signed_value(a, w) + signed_value(b, w) + x;
		flags(f, w, sum > m, signed_sum < -((int64_t)sign(w)) || signed_sum >= sign(w), i.operation == NW68_ADDX);
		break;
	}
	case NW68_SUB: case NW68_SUBA: case NW68_CMP: case NW68_CMPA: case NW68_SUBX: case NW68_NEG: case NW68_NEGX: {
		const bool extend = i.operation == NW68_SUBX || i.operation == NW68_NEGX;
		const unsigned x = extend && (f.state.ccr & NW68_X) ? 1 : 0;
		const int64_t difference = signed_value(a, w) - signed_value(b, w) - x;
		flags(f, w, (uint64_t)b + x > a, difference < -((int64_t)sign(w)) || difference >= sign(w), extend);
		break;
	}
	case NW68_AND: case NW68_OR: case NW68_EOR: case NW68_MUL:
		flags(f, w, false, false, false); break;
	case NW68_DIV:
		flags(f, 2, false, false, false); w = 4; break;
	case NW68_BIT:
		f.state.ccr = (f.state.ccr & ~NW68_Z) | ((f.dst & (1u << f.src)) ? 0 : NW68_Z); break;
	default: break;
	}
	switch (i.operation) {
	case NW68_MOVE: case NW68_MOVEA: case NW68_ADD: case NW68_ADDA: case NW68_SUB: case NW68_SUBA:
	case NW68_AND: case NW68_OR: case NW68_EOR: case NW68_CLR: case NW68_NOT: case NW68_NEG:
	case NW68_NEGX: case NW68_ADDX: case NW68_SUBX: case NW68_EXT: case NW68_SWAP:
	case NW68_SHIFT: case NW68_SCC: case NW68_MUL: case NW68_DIV:
		if (!put(f, w, f.result)) return 0; break;
	case NW68_BIT: if (i.subop && !put(f, w, f.result)) return 0; break;
	default: break;
	}
	return code_finish(f);
}

void nw68_commit(const nw68_frame &f)
{
	if (f.exit != NW68_EXIT_NATIVE || !f.bus.write) return;
	for (unsigned i = 0; i < f.nwrite; ++i) {
		const nw68_write &w = f.writes[i]; f.bus.write(f.bus.opaque, w.ea, w.width, w.value);
	}
}
