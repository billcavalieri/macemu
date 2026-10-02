#include "nw_68k_core.h"
#include <array>
#include <string.h>

namespace {
struct decoder {
	nw68_instruction &i;
	const nw68_bus &bus;
	bool word(uint16_t &v) {
		if (i.word_count == 16) return false;
		uint32_t pa;
		if (!bus.code || !bus.code(bus.opaque, i.pc + i.word_count * 2u, &v, &pa)) return false;
		i.words[i.word_count++] = v;
		return true;
	}
	bool immediate(unsigned width, uint32_t &v) {
		uint16_t hi, lo;
		if (!word(hi)) return false;
		v = width == 1 ? hi & 255u : hi;
		if (width == 4) { if (!word(lo)) return false; v = ((uint32_t)hi << 16) | lo; }
		return true;
	}
	bool ea(nw68_ea &e, unsigned bits, unsigned width) {
		e.mode = (bits >> 3) & 7; e.reg = bits & 7;
		e.pc_base = i.pc + i.word_count * 2u;
		uint16_t ext;
		if (e.mode <= 4) return !(e.mode == 1 && width == 1);
		if (e.mode == 5 || (e.mode == 7 && e.reg == 2)) {
			if (!word(ext)) return false;
			e.displacement = (int16_t)ext; return true;
		}
		if (e.mode == 6 || (e.mode == 7 && e.reg == 3)) {
			if (!word(ext) || (ext & 0x100u)) return false; // full extension: exact NK exit
			e.index_reg = (ext >> 12) & 7; e.index_address = (ext & 0x8000u) != 0;
			e.index_long = (ext & 0x800u) != 0; e.index_scale = (ext >> 9) & 3;
			e.displacement = (int8_t)ext; return true;
		}
		if (e.mode != 7) return false;
		if (e.reg == 0) { if (!word(ext)) return false; e.absolute = (uint32_t)(int32_t)(int16_t)ext; return true; }
		if (e.reg == 1) return immediate(4, e.absolute);
		if (e.reg == 4) return immediate(width, e.immediate);
		return false;
	}
};
static nw68_ea reg_ea(unsigned mode, unsigned reg) {
	nw68_ea e = {}; e.mode = mode; e.reg = reg; return e;
}
static nw68_ea imm_ea(uint32_t value) {
	nw68_ea e = {}; e.mode = 7; e.reg = 4; e.immediate = value; return e;
}
static bool alterable(const nw68_ea &e) {
	return e.mode < 7 || e.reg <= 1;
}
static bool data_alterable(const nw68_ea &e) { return e.mode != 1 && alterable(e); }
static bool control_ea(const nw68_ea &e) {
	return e.mode == 2 || e.mode == 5 || e.mode == 6 || (e.mode == 7 && e.reg <= 3);
}
static unsigned size_width(unsigned size) { return size < 3 ? 1u << size : 0; }
}

bool nw68_decode(uint32_t pc, const nw68_bus &bus, nw68_instruction &i)
{
	memset(&i, 0, sizeof i); i.pc = pc; i.operation = NW68_SERVICE;
	decoder d = { i, bus }; uint16_t op;
	if ((pc & 1) || !d.word(op)) return false;
	i.opcode = op; i.width = 4;
	const unsigned top = op >> 12, mode = (op >> 3) & 7, reg = op & 7;
	const unsigned dn = (op >> 9) & 7, sz = (op >> 6) & 3;
	const unsigned normal_flags = NW68_N | NW68_Z | NW68_V | NW68_C;
	bool ok = false;
	if (top >= 1 && top <= 3) {
		i.width = top == 1 ? 1 : top == 2 ? 4 : 2;
		ok = d.ea(i.src, op & 63, i.width) && d.ea(i.dst, ((op >> 3) & 0x38) | dn, i.width);
		i.operation = i.dst.mode == 1 ? NW68_MOVEA : NW68_MOVE;
		i.flags = i.operation == NW68_MOVE ? normal_flags : 0;
		ok = ok && alterable(i.dst);
	} else if (top == 7 && !(op & 0x100)) {
		i.operation = NW68_MOVE; i.src = imm_ea((uint32_t)(int32_t)(int8_t)op);
		i.dst = reg_ea(0, dn); i.flags = normal_flags; ok = true;
	} else if (top == 6) {
		i.operation = NW68_BRANCH; i.condition = (op >> 8) & 15; i.control = true;
		i.displacement = (int8_t)op; ok = true;
		if (!(op & 255)) { uint16_t v = 0; ok = d.word(v); i.displacement = (int16_t)v; }
		if ((op & 255) == 255) ok = false; // ROM model must certify long-branch form
	} else if (top == 5) {
		if (sz == 3) {
			i.condition = (op >> 8) & 15; i.width = 1;
			if (mode == 1) {
				i.operation = NW68_DBCC; i.dst = reg_ea(0, reg); i.control = true;
				uint16_t v = 0; ok = d.word(v); i.displacement = (int16_t)v;
			} else { i.operation = NW68_SCC; ok = d.ea(i.dst, op & 63, 1) && data_alterable(i.dst); }
		} else {
			i.operation = (op & 0x100) ? NW68_SUB : NW68_ADD;
			i.width = mode == 1 ? 4 : size_width(sz); i.src = imm_ea(dn ? dn : 8);
			ok = !(mode == 1 && sz == 0) && d.ea(i.dst, op & 63, i.width) && alterable(i.dst);
			i.flags = mode == 1 ? 0 : normal_flags | NW68_X;
		}
	} else if (top == 0) {
		const unsigned group = (op >> 8) & 15;
		if ((op & 0xf100) == 0x0100 || group == 8) {
			if (mode != 1) {
				i.operation = NW68_BIT; i.subop = (op >> 6) & 3;
				i.width = mode == 0 ? 4 : 1; i.flags = NW68_Z;
				ok = true;
				if (group == 8 && !(op & 0x100)) { uint32_t v = 0; ok = d.immediate(2, v); i.src = imm_ea(v); }
				else i.src = reg_ea(0, dn);
				ok = ok && d.ea(i.dst, op & 63, i.width) &&
					(i.subop ? data_alterable(i.dst) : i.dst.mode != 1 && !(i.dst.mode == 7 && i.dst.reg == 4));
			}
		} else if (sz < 3 && (group == 0 || group == 2 || group == 4 || group == 6 || group == 10 || group == 12)) {
			i.width = size_width(sz); uint32_t value = 0;
			ok = d.immediate(i.width, value); i.src = imm_ea(value);
			i.operation = group == 0 ? NW68_OR : group == 2 ? NW68_AND : group == 4 ? NW68_SUB :
				group == 6 ? NW68_ADD : group == 10 ? NW68_EOR : NW68_CMP;
			i.flags = normal_flags | ((group == 4 || group == 6) ? NW68_X : 0);
			ok = ok && d.ea(i.dst, op & 63, i.width) && data_alterable(i.dst);
		}
	} else if (top == 8 || top == 9 || top == 11 || top == 12 || top == 13) {
		const unsigned om = (op >> 6) & 7;
		if (top == 12 && ((op & 0xf1f8) == 0xc140 || (op & 0xf1f8) == 0xc148 || (op & 0xf1f8) == 0xc188)) {
			i.operation = NW68_EXG; i.src = reg_ea((op & 0xf1f8) == 0xc148 ? 1 : 0, dn);
			i.dst = reg_ea((op & 0xf1f8) == 0xc140 ? 0 : 1, reg); ok = true;
		} else if ((top == 9 || top == 13) && (op & 0x0130) == 0x0100 && sz < 3) {
			i.operation = top == 9 ? NW68_SUBX : NW68_ADDX; i.width = size_width(sz);
			i.src = reg_ea((op & 8) ? 4 : 0, reg); i.dst = reg_ea((op & 8) ? 4 : 0, dn);
			i.flags = normal_flags | NW68_X; ok = true;
		} else if (top == 11 && (op & 0x0138) == 0x0108 && sz < 3) {
			i.operation = NW68_CMP; i.width = size_width(sz); i.src = reg_ea(3, reg);
			i.dst = reg_ea(3, dn); i.flags = normal_flags; ok = true;
		} else if ((top == 8 || top == 12) && (om == 3 || om == 7)) {
			i.operation = top == 8 ? NW68_DIV : NW68_MUL; i.width = 2; i.subop = om == 7;
			i.dst = reg_ea(0, dn); i.flags = normal_flags;
			ok = d.ea(i.src, op & 63, 2) && i.src.mode != 1;
		} else if ((top == 9 || top == 11 || top == 13) && (om == 3 || om == 7)) {
			i.operation = top == 9 ? NW68_SUBA : top == 11 ? NW68_CMPA : NW68_ADDA;
			i.width = om == 3 ? 2 : 4; i.dst = reg_ea(1, dn);
			i.flags = top == 11 ? normal_flags : 0; ok = d.ea(i.src, op & 63, i.width);
		} else if (sz < 3 && !(top == 8 && (op & 0x01f0) == 0x0100) &&
			   !(top == 12 && (op & 0x01f0) == 0x0100)) {
			i.width = size_width(sz); i.reverse = (om & 4) != 0;
			i.operation = top == 8 ? NW68_OR : top == 9 ? NW68_SUB : top == 11 ? (i.reverse ? NW68_EOR : NW68_CMP) : top == 12 ? NW68_AND : NW68_ADD;
			i.flags = normal_flags | ((top == 9 || top == 13) ? NW68_X : 0);
			if (i.reverse) { i.src = reg_ea(0, dn); ok = d.ea(i.dst, op & 63, i.width) && data_alterable(i.dst) &&
				!((top == 8 || top == 12) && mode < 2); }
			else { i.dst = reg_ea(0, dn); ok = d.ea(i.src, op & 63, i.width) && i.src.mode != 1; }
		}
	} else if (top == 4) {
		if (op == 0x4e71) { i.operation = NW68_NOP; ok = true; }
		else if (op == 0x4e75) { i.operation = NW68_RTS; i.control = true; ok = true; }
		else if ((op & 0xfff8) == 0x4e50) {
			i.operation = NW68_LINK; i.dst = reg_ea(1, reg); uint16_t v = 0; ok = d.word(v); i.displacement = (int16_t)v;
		} else if ((op & 0xfff8) == 0x4e58) { i.operation = NW68_UNLK; i.dst = reg_ea(1, reg); ok = true; }
		else if ((op & 0xfff8) == 0x4840) { i.operation = NW68_SWAP; i.src = i.dst = reg_ea(0, reg); i.flags = normal_flags; ok = true; }
		else if ((op & 0xfff8) == 0x4880 || (op & 0xfff8) == 0x48c0 || (op & 0xfff8) == 0x49c0) {
			i.operation = NW68_EXT; i.src = i.dst = reg_ea(0, reg); i.subop = (op & 0xfff8) == 0x48c0 ? 1 : (op & 0xfff8) == 0x49c0 ? 2 : 0;
			i.width = i.subop == 0 ? 2 : 4; i.flags = normal_flags; ok = true;
		} else if ((op & 0xfb80) == 0x4880) {
			i.operation = NW68_MOVEM; i.width = (op & 64) ? 4 : 2; i.reverse = (op & 0x400) != 0;
			ok = d.word(i.reg_mask) && d.ea(i.dst, op & 63, i.width);
			ok = ok && (control_ea(i.dst) || (i.reverse ? mode == 3 : mode == 4)) && (i.reverse || alterable(i.dst));
		} else if ((op & 0xf1c0) == 0x41c0) {
			i.operation = NW68_LEA; i.dst = reg_ea(1, dn); ok = d.ea(i.src, op & 63, 4) && control_ea(i.src);
		} else if ((op & 0xffc0) == 0x4840) {
			i.operation = NW68_PEA; ok = d.ea(i.src, op & 63, 4) && control_ea(i.src);
		} else if ((op & 0xff80) == 0x4e80) {
			i.operation = (op & 64) ? NW68_JMP : NW68_JSR; i.control = true;
			ok = d.ea(i.src, op & 63, 4) && control_ea(i.src);
		} else {
			const unsigned group = (op >> 8) & 255;
			if (sz < 3 && (group == 0x40 || group == 0x42 || group == 0x44 || group == 0x46 || group == 0x4a)) {
				i.width = size_width(sz); i.operation = group == 0x40 ? NW68_NEGX : group == 0x42 ? NW68_CLR : group == 0x44 ? NW68_NEG : group == 0x46 ? NW68_NOT : NW68_TST;
				i.flags = normal_flags | ((group == 0x40 || group == 0x44) ? NW68_X : 0);
				ok = d.ea(i.dst, op & 63, i.width) && data_alterable(i.dst); i.src = i.dst;
			}
		}
	} else if (top == 14 && sz < 3) {
		i.operation = NW68_SHIFT; i.width = size_width(sz); i.dst = i.src = reg_ea(0, reg);
		i.subop = ((op >> 2) & 6) | ((op >> 8) & 1);
		i.immediate = dn ? dn : 8; i.reverse = (op & 32) != 0;
		i.condition = dn; i.flags = normal_flags | (i.subop < 6 ? NW68_X : 0); ok = true;
	}
	if (!ok) { i.operation = NW68_SERVICE; i.flags = 0; }
	i.flags_live = i.flags;
	i.length = i.word_count * 2;
	return ok;
}

nw68_policy nw68_opcode_policy(uint16_t op)
{
	struct reader {
		static bool code(void *p, uint32_t ea, uint16_t *v, uint32_t *pa) {
			*v = ea == 0 ? *(uint16_t *)p : 0; *pa = ea; return true;
		}
	};
	/* Policy is independent of mappings and code lifetime. Real extensions
	 * and operands still receive the full live checks at native entry. */
	static const std::array<bool, 65536> candidates = []() {
		std::array<bool, 65536> result = {};
		for (unsigned n = 0; n < result.size(); ++n) {
			uint16_t word = n;
			nw68_bus bus = {}; bus.opaque = &word; bus.code = reader::code;
			nw68_instruction i;
			result[n] = nw68_decode(0, bus, i);
		}
		return result;
	}();
	return candidates[op] ? NW68_DIRECT : NW68_NANOKERNEL;
}

bool nw68_condition(unsigned c, uint8_t f)
{
	const bool n = f & NW68_N, z = f & NW68_Z, v = f & NW68_V, carry = f & NW68_C;
	switch (c & 15) {
	case 0: return true; case 1: return false;
	case 2: return !carry && !z; case 3: return carry || z;
	case 4: return !carry; case 5: return carry; case 6: return !z; case 7: return z;
	case 8: return !v; case 9: return v; case 10: return !n; case 11: return n;
	case 12: return n == v; case 13: return n != v;
	case 14: return !z && n == v; default: return z || n != v;
	}
}

const char *nw68_operation_name(nw68_operation op)
{
	static const char *names[] = { "service", "nop", "move", "movea", "add", "sub", "cmp", "adda", "suba", "cmpa", "and", "or", "eor", "clr", "tst", "not", "neg", "ext", "swap", "exg", "lea", "pea", "branch", "dbcc", "scc", "jsr", "jmp", "rts", "link", "unlk", "movem", "bit", "shift", "mul", "div", "addx", "subx", "negx" };
	return (unsigned)op < sizeof names / sizeof names[0] ? names[op] : "unknown";
}
