/*
 *  ppc-execute.cpp - PowerPC semantics
 *
 *  Kheperix (C) 2003-2005 Gwenole Beauchesne
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

#include "sysdeps.h"
#if defined(__clang__)
#pragma STDC FENV_ACCESS ON
#endif

#include <stdio.h>
#include <math.h>
#include <time.h>
#include <type_traits>
#ifdef __MINGW64__
#include <fenv.h>
#endif
#include "cpu/vm.hpp"
#include "cpu/ppc/ppc-cpu.hpp"
#include "cpu/ppc/ppc-bitfields.hpp"
#include "cpu/ppc/ppc-operands.hpp"
#include "cpu/ppc/ppc-operations.hpp"
#include "cpu/ppc/ppc-execute.hpp"
#include "cpu/ppc/ppc-fp-environment.hpp"

#ifndef SHEEPSHAVER
#include "basic-kernel.hpp"
#endif

#ifdef SHEEPSHAVER
#include "main.h"
#include "prefs.h"
#include "nw_boot_contract.h"
#include "nw_io.h"
#include "nw_jit.h"
#include "nw_jit_verify.h"
#endif

#if ENABLE_MON
#include "mon.h"
#include "mon_disass.h"
#endif

#define DEBUG 0
#include "debug.h"

/**
 *	Illegal & NOP instructions
 **/

void powerpc_cpu::execute_illegal(uint32 opcode)
{
#ifdef SHEEPSHAVER
	if (ppc32_guest_mmu_enabled()) {
		/* Architectural: program exception, SRR1 illegal-instruction bit. */
#if NW_BOOT_LOG
		static int n_ill;
		if (n_ill < 8) {
			n_ill++;
			printf("NW-BOOT illegal pc=%08x op=%08x -> 0x700\n", pc(), opcode);
			fflush(stdout);
		}
#endif
		take_program(0x00080000u);
		return;
	}
#endif
	fprintf(stderr, "Illegal instruction at %08x, opcode = %08x\n", pc(), opcode);

#ifdef SHEEPSHAVER
#if NW_BOOT_LOG
	static int n_ill;
	if (n_ill < 8) {
		n_ill++;
		printf("NW-BOOT illegal pc=%08x op=%08x\n", pc(), opcode);
		fflush(stdout);
	}
#endif
	if (PrefsFindBool("ignoreillegal")) {
		increment_pc(4);
		return;
	}
#endif

#if ENABLE_MON
	disass_ppc(stdout, pc(), opcode);

	// Start up mon in real-mode
	const char *arg[4] = {"mon", "-m", "-r", NULL};
	mon(3, arg);
#endif
	abort();
}

void powerpc_cpu::execute_nop(uint32 opcode)
{
	increment_pc(4);
}

/**
 *	Helper class to compute the overflow/carry condition
 *
 *		OP		Operation to perform
 */

template< class OP >
struct op_carry {
	static inline bool apply(uint32, uint32, uint32) {
		return false;
	}
};

template<>
struct op_carry<op_add> {
	static inline bool apply(uint32 a, uint32 b, uint32 c) {
		// TODO: use 32-bit arithmetic
		uint64 carry = (uint64)a + (uint64)b + (uint64)c;
		return (carry >> 32) != 0;
	}
};

template< class OP >
struct op_overflow {
	static inline bool apply(uint32, uint32, uint32) {
		return false;
	}
};

template<>
struct op_overflow<op_neg> {
	static inline bool apply(uint32 a, uint32, uint32) {
		return a == 0x80000000;
	};
};

template<>
struct op_overflow<op_add> {
	static inline bool apply(uint32 a, uint32 b, uint32 c) {
		// TODO: use 32-bit arithmetic
		int64 overflow = (int64)(int32)a + (int64)(int32)b + (int64)(int32)c;
		return (((uint64)overflow) >> 63) ^ (((uint32)overflow) >> 31);
	}
};

/**
 *	Perform an addition/subtraction
 *
 *		RA		Input operand register, possibly 0
 *		RB		Input operand either register or immediate
 *		RC		Input carry
 *		CA		Predicate to compute the carry out of the operation
 *		OE		Predicate to compute the overflow flag
 *		Rc		Predicate to record CR0
 **/

template< class RA, class RB, class RC, class CA, class OE, class Rc >
void powerpc_cpu::execute_addition(uint32 opcode)
{
	const uint32 a = RA::get(this, opcode);
	const uint32 b = RB::get(this, opcode);
	const uint32 c = RC::get(this, opcode);
	uint32 d = a + b + c;

	// Set XER (CA) if instruction affects carry bit
	if (CA::test(opcode))
		xer().set_ca(op_carry<op_add>::apply(a, b, c));

	// Set XER (OV, SO) if instruction has OE set
	if (OE::test(opcode))
		xer().set_ov(op_overflow<op_add>::apply(a, b, c));

	// Set CR0 (LT, GT, EQ, SO) if instruction has Rc set
	if (Rc::test(opcode))
		record_cr0((int32)d);

	// Commit result to output operand
	operand_RD::set(this, opcode, d);

	increment_pc(4);
}

/**
 *	Generic arithmetic instruction
 *
 *		OP		Operation to perform
 *		RD		Output register
 *		RA		Input operand register
 *		RB		Input operand register or immediate (optional: operand_NONE)
 *		RC		Input operand register or immediate (optional: operand_NONE)
 *		OE		Predicate to compute overflow flag
 *		Rc		Predicate to record CR0
 **/

template< class OP, class RD, class RA, class RB, class RC, class OE, class Rc >
void powerpc_cpu::execute_generic_arith(uint32 opcode)
{
	const uint32 a = RA::get(this, opcode);
	const uint32 b = RB::get(this, opcode);
	const uint32 c = RC::get(this, opcode);

	uint32 d = op_apply<uint32, OP, RA, RB, RC>::apply(a, b, c);

	// Set XER (OV, SO) if instruction has OE set
	if (OE::test(opcode))
		xer().set_ov(op_overflow<OP>::apply(a, b, c));

	// Set CR0 (LT, GT, EQ, SO) if instruction has Rc set
	if (Rc::test(opcode))
		record_cr0((int32)d);

	// commit result to output operand
	RD::set(this, opcode, d);

	increment_pc(4);
}

/**
 *	Rotate Left Word Immediate then Mask Insert
 *
 *		SH		Shift count
 *		MA		Mask value
 *		Rc		Predicate to record CR0
 **/

template< class SH, class MA, class Rc >
void powerpc_cpu::execute_rlwimi(uint32 opcode)
{
	const uint32 n = SH::get(this, opcode);
	const uint32 m = MA::get(this, opcode);
	const uint32 rs = operand_RS::get(this, opcode);
	const uint32 ra = operand_RA::get(this, opcode);
	uint32 d = op_ppc_rlwimi::apply(rs, n, m, ra);

	// Set CR0 (LT, GT, EQ, SO) if instruction has Rc set
	if (Rc::test(opcode))
		record_cr0((int32)d);

	// Commit result to output operand
	operand_RA::set(this, opcode, d);

	increment_pc(4);
}

/**
 *	Shift instructions
 *
 *		OP		Operation to perform
 *		RD		Output operand
 *		RA		Source operand
 *		SH		Shift count
 *		SO		Shift operation
 *		CA		Predicate to compute carry bit
 *		Rc		Predicate to record CR0
 **/

template< class OP >
struct invalid_shift {
	static inline uint32 value(uint32) {
		return 0;
	}
};

template<>
struct invalid_shift<op_shra> {
	static inline uint32 value(uint32 r) {
		return 0 - (r >> 31);
	}
};

template< class OP, class RD, class RA, class SH, class SO, class CA, class Rc >
void powerpc_cpu::execute_shift(uint32 opcode)
{
	const uint32 n = SO::apply(SH::get(this, opcode));
	const uint32 r = RA::get(this, opcode);
	uint32 d;

	// Shift operation is valid only if rB[26] = 0
	if (n & 0x20) {
		d = invalid_shift<OP>::value(r);
		if (CA::test(opcode))
			xer().set_ca(d >> 31);
	}
	else {
		d = OP::apply(r, n);
		if (CA::test(opcode)) {
			const uint32 ca = (r & 0x80000000) && (r & ~(0xffffffff << n));
			xer().set_ca(ca);
		}
	}

	// Set CR0 (LT, GT, EQ, SO) if instruction has Rc set
	if (Rc::test(opcode))
		record_cr0((int32)d);

	// Commit result to output operand
	RD::set(this, opcode, d);

	increment_pc(4);
}

/**
 *	Branch conditional instructions
 *
 *		PC		Input program counter (PC, LR, CTR)
 *		BO		BO operand
 *		DP		Displacement operand
 *		AA		Predicate for absolute address
 *		LK		Predicate to record NPC into link register
 **/

template< class PC, class BO, class DP, class AA, class LK >
void powerpc_cpu::execute_branch(uint32 opcode)
{
	const int bo = BO::get(this, opcode);
	bool ctr_ok = true;
	bool cond_ok = true;

	if (BO_CONDITIONAL_BRANCH(bo)) {
		cond_ok = cr().test(BI_field::extract(opcode));
		if (!BO_BRANCH_IF_TRUE(bo))
			cond_ok = !cond_ok;
	}

	if (BO_DECREMENT_CTR(bo)) {
		ctr_ok = (ctr() -= 1) == 0;
		if (!BO_BRANCH_IF_CTR_ZERO(bo))
			ctr_ok = !ctr_ok;
	}

	const uint32 npc = pc() + 4;
	if (ctr_ok && cond_ok)
		pc() = ((AA::test(opcode) ? 0 : PC::get(this, opcode)) + DP::get(this, opcode)) & -4;
	else
		pc() = npc;

	if (LK::test(opcode))
		lr() = npc;
}

/**
 *	Compare instructions
 *
 *		RB		Second operand (GPR, SIMM, UIMM)
 *		CT		Type of variables to be compared (uint32, int32)
 **/

template< class RB, typename CT >
void powerpc_cpu::execute_compare(uint32 opcode)
{
	const uint32 a = operand_RA::get(this, opcode);
	const uint32 b = RB::get(this, opcode);
	const uint32 crfd = crfD_field::extract(opcode);
	record_cr(crfd, (CT)a < (CT)b ? -1 : ((CT)a > (CT)b ? +1 : 0));
	increment_pc(4);
}

/**
 *	Operations on condition register
 *
 *		OP		Operation to perform
 **/

template< class OP >
void powerpc_cpu::execute_cr_op(uint32 opcode)
{
	const uint32 crbA = crbA_field::extract(opcode);
	uint32 a = (cr().get() >> (31 - crbA)) & 1;
	const uint32 crbB = crbB_field::extract(opcode);
	uint32 b = (cr().get() >> (31 - crbB)) & 1;
	const uint32 crbD = crbD_field::extract(opcode);
	uint32 d = OP::apply(a, b) & 1;
	cr().set((cr().get() & ~(1 << (31 - crbD))) | (d << (31 - crbD)));
	increment_pc(4);
}

/**
 *	Divide instructions
 *
 *		SB		Signed division
 *		OE		Predicate to compute overflow
 *		Rc		Predicate to record CR0
 **/

template< bool SB, class OE, class Rc >
void powerpc_cpu::execute_divide(uint32 opcode)
{
	const uint32 a = operand_RA::get(this, opcode);
	const uint32 b = operand_RB::get(this, opcode);
	uint32 d;

	// Specialize divide semantic action
	if (OE::test(opcode))
		d = do_execute_divide<SB, true>(a, b);
	else
		d = do_execute_divide<SB, false>(a, b);

	// Set CR0 (LT, GT, EQ, SO) if instruction has Rc set
	if (Rc::test(opcode))
		record_cr0((int32)d);

	// Commit result to output operand
	operand_RD::set(this, opcode, d);

	increment_pc(4);
}

/**
 *	Multiply instructions
 *
 *		HI		Predicate for multiply high word
 *		SB		Predicate for signed operation
 *		OE		Predicate to compute overflow
 *		Rc		Predicate to record CR0
 **/

template< bool HI, bool SB, class OE, class Rc >
void powerpc_cpu::execute_multiply(uint32 opcode)
{
	const uint32 a = operand_RA::get(this, opcode);
	const uint32 b = operand_RB::get(this, opcode);
	uint64 d = SB ? (int64)(int32)a * (int64)(int32)b : (uint64)a * (uint64)b;

	// Overflow if the product cannot be represented in 32 bits
	if (OE::test(opcode)) {
		xer().set_ov((d & UVAL64(0xffffffff80000000)) != 0 &&
					 (d & UVAL64(0xffffffff80000000)) != UVAL64(0xffffffff80000000));
	}

	// Only keep high word if multiply high instruction
	if (HI)
		d >>= 32;

	// Set CR0 (LT, GT, EQ, SO) if instruction has Rc set
	if (Rc::test(opcode))
		record_cr0((uint32)d);

	// Commit result to output operand
	operand_RD::set(this, opcode, (uint32)d);

	increment_pc(4);
}

/**
 *  Record FPSCR
 *
 *		Update FP exception bits
 **/

void powerpc_cpu::record_fpscr(int exceptions)
{
    if (uint32(exceptions) & ~fpscr()) fpscr() |= 0x80000000u;
    fpscr() |= exceptions;
    fpscr() = ppc_fpscr_summaries(fpscr());
}

/**
 *	Floating-point arithmetic
 *
 *		FP		Floating Point type
 *		OP		Operation to perform
 *		RD		Output register
 *		RA		Input operand
 *		RB		Input operand (optional)
 *		RC		Input operand (optional)
 *		Rc		Predicate to record CR1
 *		FPSCR	Predicate to compute FPSCR bits
 **/

template< class FP, class OP, class RD, class RA, class RB, class RC, class Rc, bool FPSCR >
void powerpc_cpu::execute_fp_arith(uint32 opcode)
{
    const unsigned kind = (opcode >> 1) & 31u;
    if (FPSCR && ppc32_guest_mmu_enabled() && (kind == 18 || kind == 20 || kind == 21 || kind == 25)) {
        {
        const ppc_fp_environment fp_env(fpscr());
        const double a = RA::get(this, opcode), b = RB::get(this, opcode);
        const bool nan_a = is_NaN(a), nan_b = is_NaN(b), ai = isinf(a), bi = isinf(b);
        const bool negative_a = signbit(a), negative_b = signbit(b);
        uint32 causes = 0;
        any_register result;
        if (nan_a || nan_b) {
            any_register selected; selected.d = nan_a ? a : b;
            result.j = selected.j | UVAL64(0x0008000000000000);
            if (is_SNaN(a) || is_SNaN(b)) causes = 0x01000000u;
        } else if ((kind == 20 || kind == 21) && ai && bi &&
                   (negative_a != (negative_b != (kind == 20)))) causes = 0x00800000u;
        else if (kind == 25 && ((ai && b == 0) || (bi && a == 0))) causes = 0x00100000u;
        else if (kind == 18) {
            if (ai && bi) causes = 0x00400000u;
            else if (a == 0 && b == 0) causes = 0x00200000u;
            else if (!ai && a != 0 && b == 0) causes = 0x04000000u;
        }
        if (causes & 0x00f00000u) result.j = UVAL64(0x7ff8000000000000);
        const bool suppressed = ((causes & 0x01f00000u) && (fpscr() & 0x80u)) ||
                                ((causes & 0x04000000u) && (fpscr() & 0x10u));
        // Special arithmetic is exact or undefined; finite FR/FI and newly
        // raised OX/UX/XX remain a separate, unqualified arithmetic milestone.
        const bool exact_special = ai || bi || ((kind == 20 || kind == 21) ? a == 0 && b == 0 : a == 0 || b == 0);
        if (nan_a || nan_b || causes || exact_special) fpscr() &= ~0x60000u;
        record_fpscr(causes);
        if (!suppressed) {
            if (!(nan_a || nan_b || (causes & 0x00f00000u))) {
                const FP rounded = op_apply<double, OP, RA, RB, RC>::apply(a, b, 0);
                result.d = rounded;
                fp_classify(rounded);
            } else fpscr() = (fpscr() & ~0x1f000u) | 0x11000u;
            // Do not round propagated NaNs to single: arithmetic retains the
            // whole selected FPR payload in both precision forms.
            RD::set(this, opcode, result.d);
        }
        } // Program exception entry observes the caller's FP environment.
        if (Rc::test(opcode)) record_cr1();
        if ((fpscr() & 0x40000000u) && (ppc32_guest_mmu().msr() & 0x900u)) {
            take_program(0x00100000u); return;
        }
        increment_pc(4); return;
    }

	const ppc_fp_environment fp_env(fpscr());
	const double a = RA::get(this, opcode);
	const double b = RB::get(this, opcode);
	const double c = RC::get(this, opcode);

#if PPC_ENABLE_FPU_EXCEPTIONS
	int exceptions;
	if (FPSCR) {
		exceptions = op_apply<uint32, fp_exception_condition<OP>, RA, RB, RC>::apply(a, b, c);
		feclearexcept(FE_ALL_EXCEPT);
		febarrier();
	}
#endif

	FP d = op_apply<double, OP, RA, RB, RC>::apply(a, b, c);

	if (FPSCR) {

		// Update FPSCR exception bits
#if PPC_ENABLE_FPU_EXCEPTIONS
		febarrier();
		int raised = fetestexcept(FE_ALL_EXCEPT);
		if (raised & FE_INEXACT)
			exceptions |= FPSCR_XX_field::mask();
		if (raised & FE_DIVBYZERO)
			exceptions |= FPSCR_ZX_field::mask();
		if (raised & FE_UNDERFLOW)
			exceptions |= FPSCR_UX_field::mask();
		if (raised & FE_OVERFLOW)
			exceptions |= FPSCR_OX_field::mask();
		record_fpscr(exceptions);
#endif

		// FPSCR[FPRF] is set to the class and sign of the result
		if (!FPSCR_VE_field::test(fpscr()))
			fp_classify(d);
	}
	
	// Set CR1 (FX, FEX, VX, VOX) if instruction has Rc set
	if (Rc::test(opcode))
		record_cr1();

	// Commit result to output operand
	RD::set(this, opcode, d);
	increment_pc(4);
}

/**
 *	Load/store instructions
 *
 *		OP		Operation to perform on loaded value
 *		RA		Base operand
 *		RB		Displacement (GPR(RB), EXTS(d))
 *		LD		Load operation?
 *		SZ		Size of load/store operation
 *		UP		Update RA with EA
 *		RX		Reverse operand
 **/

#ifdef SHEEPSHAVER
#define PA_JIT_STORE(PA, N) do { \
	nw_jit_invalidate_range_src((PA), (N), NW_JIT_FL_ISTORE); \
	nw_fb_damage_store((PA), (unsigned)(N)); \
} while (0)
#else
#define PA_JIT_STORE(PA, N) ((void)0)
#endif

template< int SZ, bool RX >
struct memory_helper;

#define DEFINE_MEMORY_HELPER(SIZE)																\
template< bool RX >																				\
struct memory_helper<SIZE, RX>																	\
{																								\
	static inline uint32 load(uint32 ea) {														\
		return RX ? vm_read_memory_##SIZE##_reversed(ea) : vm_read_memory_##SIZE(ea);			\
	}																							\
	static inline void store(uint32 ea, uint32 value) {											\
		RX ? vm_write_memory_##SIZE##_reversed(ea, value) : vm_write_memory_##SIZE(ea, value);	\
		PA_JIT_STORE(ea, SIZE);																	\
	}																							\
}

DEFINE_MEMORY_HELPER(1);
DEFINE_MEMORY_HELPER(2);
DEFINE_MEMORY_HELPER(4);

/*
 *	Physical-address accessors for the less common load/store forms (string,
 *	floating-point, reservation, vector, dcbz). New World I/O segments must
 *	reach the device models from every path; a host dereference of a guest
 *	I/O physical address is a SIGSEGV (seen with a 68k BlockMove, lswx/stswx,
 *	from 0x809122c3).
 */
#ifdef SHEEPSHAVER
static inline bool pa_is_io(uint32 pa) { return ppc32_guest_mmu_enabled() && nw_pa_kind(pa) == NW_PA_IO; }
static inline bool pa_is_rom(uint32 pa) { return ppc32_guest_mmu_enabled() && nw_pa_kind(pa) == NW_PA_ROM; }
#define PA_IO_READ(PA, SZ, PC) nw_io_read((PA), (SZ), (PC))
#define PA_IO_WRITE(PA, SZ, V, PC) nw_io_write((PA), (SZ), (V), (PC))
#else
static inline bool pa_is_io(uint32) { return false; }
static inline bool pa_is_rom(uint32) { return false; }
#define PA_IO_READ(PA, SZ, PC) 0u
#define PA_IO_WRITE(PA, SZ, V, PC) ((void)0)
#endif

static inline uint32 pa_read_1(uint32 pa, uint32 pc) { return pa_is_io(pa) ? PA_IO_READ(pa, 1, pc) : vm_read_memory_1(pa); }
static inline uint32 pa_read_2(uint32 pa, uint32 pc) { return pa_is_io(pa) ? PA_IO_READ(pa, 2, pc) : vm_read_memory_2(pa); }
static inline uint32 pa_read_4(uint32 pa, uint32 pc) { return pa_is_io(pa) ? PA_IO_READ(pa, 4, pc) : vm_read_memory_4(pa); }
static inline uint64 pa_read_8(uint32 pa, uint32 pc)
{
	if (!pa_is_io(pa))
		return vm_read_memory_8(pa);
	return ((uint64)PA_IO_READ(pa, 4, pc) << 32) | PA_IO_READ(pa + 4, 4, pc);
}
static inline void pa_write_1(uint32 pa, uint32 v, uint32 pc) { if (pa_is_io(pa)) PA_IO_WRITE(pa, 1, v & 0xffu, pc); else if (!pa_is_rom(pa)) { vm_write_memory_1(pa, v); PA_JIT_STORE(pa, 1); } }
static inline void pa_write_2(uint32 pa, uint32 v, uint32 pc) { if (pa_is_io(pa)) PA_IO_WRITE(pa, 2, v & 0xffffu, pc); else if (!pa_is_rom(pa)) { vm_write_memory_2(pa, v); PA_JIT_STORE(pa, 2); } }
static inline void pa_write_4(uint32 pa, uint32 v, uint32 pc) { if (pa_is_io(pa)) PA_IO_WRITE(pa, 4, v, pc); else if (!pa_is_rom(pa)) { vm_write_memory_4(pa, v); PA_JIT_STORE(pa, 4); } }
static inline void pa_write_8(uint32 pa, uint64 v, uint32 pc)
{
	if (pa_is_io(pa)) {
		PA_IO_WRITE(pa, 4, (uint32)(v >> 32), pc);
		PA_IO_WRITE(pa + 4, 4, (uint32)v, pc);
		return;
	}
	if (!pa_is_rom(pa)) {
		vm_write_memory_8(pa, v);
		PA_JIT_STORE(pa, 8);
	}
}

#ifdef SHEEPSHAVER
uint64 powerpc_cpu::nw_verify_read(uint32 pa, unsigned width)
{
    uint64 value = 0;
    switch (width) {
    case 1: value = pa_read_1(pa, pc()); break;
    case 2: value = pa_read_2(pa, pc()); break;
    case 4: value = pa_read_4(pa, pc()); break;
    case 8: value = pa_read_8(pa, pc()); break;
    default: abort();
    }
    if (nw_verify_trace_) nw_verify_trace_->record(pc(), nw_verify_ea_, width, false, value);
    return value;
}

void powerpc_cpu::nw_verify_write(uint32 pa, unsigned width, uint64 value)
{
    if (width < 8) value &= (UINT64_C(1) << (width * 8)) - 1;
    switch (width) {
    case 1: pa_write_1(pa, (uint32)value, pc()); break;
    case 2: pa_write_2(pa, (uint32)value, pc()); break;
    case 4: pa_write_4(pa, (uint32)value, pc()); break;
    case 8: pa_write_8(pa, value, pc()); break;
    default: abort();
    }
    if (nw_verify_trace_) {
        const uint32 fault = !pa_is_io(pa) && !pa_is_rom(pa) &&
            ((pa & ~0xfffu) == (last_fetch_pa_ & ~0xfffu)) ? NW_JIT_FAULT_SMC : 0;
        nw_verify_trace_->record(pc(), nw_verify_ea_, width, true, value, fault);
    }
}
#define VERIFY_PA_READ(PA, N) (nw_verify_trace_ ? nw_verify_read((PA), (N)) : pa_read_##N((PA), pc()))
#define VERIFY_PA_WRITE(PA, N, V) do { \
	if (nw_verify_trace_) nw_verify_write((PA), (N), (V)); \
	else pa_write_##N((PA), (V), pc()); \
} while (0)
#else
#define VERIFY_PA_READ(PA, N) pa_read_##N((PA), pc())
#define VERIFY_PA_WRITE(PA, N, V) pa_write_##N((PA), (V), pc())
#endif

template< class OP, class RA, class RB, bool LD, int SZ, bool UP, bool RX >
void powerpc_cpu::execute_loadstore(uint32 opcode)
{
	const uint32 a = RA::get(this, opcode);
	const uint32 b = RB::get(this, opcode);
	const uint32 ea = a + b;
	uint32 pa;

	if (!guest_data_xlate(ea, SZ, !LD, &pa))
		return;

#ifdef SHEEPSHAVER
	if (nw_verify_trace_) {
		if (LD) {
			uint32 value = (uint32)nw_verify_read(pa, SZ);
			if (RX) value = SZ == 2 ? bswap_16(value) : SZ == 4 ? bswap_32(value) : value;
			operand_RD::set(this, opcode, OP::apply(value));
		} else {
			uint32 value = operand_RS::get(this, opcode);
			if (RX) value = SZ == 2 ? bswap_16(value) : SZ == 4 ? bswap_32(value) : value;
			nw_verify_write(pa, SZ, value);
		}
		if (UP) RA::set(this, opcode, ea);
		increment_pc(4);
		return;
	}
	/* New World: physical decode (nw_pa_kind) — I/O to devices, ROM stores dropped. */
	if (pa_is_io(pa)) {
		if (LD) {
			uint32 v = nw_io_read(pa, SZ, pc());
			if (RX)
				v = (SZ == 2) ? bswap_16(v) : (SZ == 4) ? bswap_32(v) : v;
			operand_RD::set(this, opcode, OP::apply(v));
		} else {
			uint32 v = operand_RS::get(this, opcode);
			if (RX)
				v = (SZ == 2) ? bswap_16(v) : (SZ == 4) ? bswap_32(v) : v;
			nw_io_write(pa, SZ, v & (SZ == 4 ? 0xffffffffu : SZ == 2 ? 0xffffu : 0xffu), pc());
		}
		if (UP)
			RA::set(this, opcode, ea);
		increment_pc(4);
		return;
	}
	if (!LD && pa_is_rom(pa)) {
		if (UP)
			RA::set(this, opcode, ea);
		increment_pc(4);
		return;
	}
#endif

	if (LD)
		operand_RD::set(this, opcode, OP::apply(memory_helper<SZ, RX>::load(pa)));
	else
		memory_helper<SZ, RX>::store(pa, operand_RS::get(this, opcode));

	if (UP)
		RA::set(this, opcode, ea);

	increment_pc(4);
}

template< class RA, class DP, bool LD >
void powerpc_cpu::execute_loadstore_multiple(uint32 opcode)
{
	const uint32 a = RA::get(this, opcode);
	const uint32 d = DP::get(this, opcode);
	uint32 ea = a + d;
/*
	// FIXME: generate exception if ea is not word-aligned
	if ((ea & 3) != 0) {
#ifdef SHEEPSHAVER
		D(bug("unaligned load/store multiple to %08x\n", ea));
		increment_pc(4);
		return;
#else
		abort();
#endif
	}
*/
	int r = LD ? rD_field::extract(opcode) : rS_field::extract(opcode);
	while (r <= 31) {
		uint32 pa;
		if (!guest_data_xlate(ea, 4, !LD, &pa))
			return;
#ifdef SHEEPSHAVER
		if (nw_verify_trace_) {
			if (LD) gpr(r) = (uint32)nw_verify_read(pa, 4);
			else nw_verify_write(pa, 4, gpr(r));
			r++; ea += 4; continue;
		}
		if (pa_is_io(pa)) {
			if (LD)
				gpr(r) = nw_io_read(pa, 4, pc());
			else
				nw_io_write(pa, 4, gpr(r), pc());
			r++;
			ea += 4;
			continue;
		}
		if (!LD && pa_is_rom(pa)) {
			r++;
			ea += 4;
			continue;
		}
#endif
		if (LD)
			gpr(r) = vm_read_memory_4(pa);
		else {
			vm_write_memory_4(pa, gpr(r));
#ifdef SHEEPSHAVER
			nw_jit_invalidate_page_src(pa, NW_JIT_FL_ISTORE);
			nw_fb_damage_store(pa, 4);
#endif
		}
		r++;
		ea += 4;
	}

	increment_pc(4);
}

/**
 *	Floating-point load/store instructions
 *
 *		RA		Base operand
 *		RB		Displacement (GPR(RB), EXTS(d))
 *		LD		Load operation?
 *		DB		Predicate for double value
 *		UP		Predicate to update RA with EA
 **/

template< class RA, class RB, bool LD, bool DB, bool UP >
void powerpc_cpu::execute_fp_loadstore(uint32 opcode)
{
	const uint32 a = RA::get(this, opcode);
	const uint32 b = RB::get(this, opcode);
	const uint32 ea = a + b;
	uint64 v;

	if (LD) {
		uint32 pa;
		if (DB) {
			if (!guest_data_xlate(ea, 8, false, &pa))
				return;
			v = VERIFY_PA_READ(pa, 8);
		}
		else {
			if (!guest_data_xlate(ea, 4, false, &pa))
				return;
			v = fp_load_single_convert(VERIFY_PA_READ(pa, 4));
		}
		operand_fp_dw_RD::set(this, opcode, v);
	}
	else {
		v = operand_fp_dw_RS::get(this, opcode);
		uint32 pa;
		if (DB) {
			if (!guest_data_xlate(ea, 8, true, &pa))
				return;
			VERIFY_PA_WRITE(pa, 8, v);
		}
		else {
			if (!guest_data_xlate(ea, 4, true, &pa))
				return;
			VERIFY_PA_WRITE(pa, 4, fp_store_single_convert(v));
		}
	}

	if (UP)
		RA::set(this, opcode, ea);

	increment_pc(4);
}

/**
 *	Load/Store String Word instruction
 *
 *		RA		Input operand as base EA
 *		IM		lswi mode?
 *		NB		Number of bytes to transfer
 **/

template< class RA, bool IM, class NB >
void powerpc_cpu::execute_load_string(uint32 opcode)
{
	uint32 ea = RA::get(this, opcode);
	if (!IM)
		ea += operand_RB::get(this, opcode);

	int nb = NB::get(this, opcode);
	if (IM && nb == 0)
		nb = 32;

	int rd = rD_field::extract(opcode);
	for (int i = 0; i < nb; rd = (rd + 1) & 31) {
		const unsigned bytes = (nb - i >= 4) ? 4 : nb - i;
		uint32 value = 0;
		for (unsigned b = 0; b < bytes;) {
			const uint32 address = ea + i + b;
			const unsigned available = 4096 - (address & 4095);
			// Preserve ordinary word/halfword reads, but never translate only
			// the start of a read spanning a different page's permissions/PA.
			const unsigned width = bytes - b >= 4 && available >= 4 ? 4 :
			                       bytes - b >= 2 && available >= 2 ? 2 : 1;
			uint32 pa;
			if (!guest_data_xlate(address, width, false, &pa)) return;
			uint32 part;
			if (width == 4) part = VERIFY_PA_READ(pa, 4);
			else if (width == 2) part = VERIFY_PA_READ(pa, 2);
			else part = VERIFY_PA_READ(pa, 1);
			value |= part << (8 * (4 - b - width));
			b += width;
		}
		// Retain completed earlier registers; a fault within this register
		// does not expose an unfinished result.
		gpr(rd) = value;
		i += bytes;
	}

	increment_pc(4);
}

template< class RA, bool IM, class NB >
void powerpc_cpu::execute_store_string(uint32 opcode)
{
	uint32 ea = RA::get(this, opcode);
	if (!IM)
		ea += operand_RB::get(this, opcode);

	int nb = NB::get(this, opcode);
	if (IM && nb == 0)
		nb = 32;

	int rs = rS_field::extract(opcode);
	int sh = 24;
	for (int i = 0; i < nb; i++) {
		uint32 pa;
		if (!guest_data_xlate(ea + i, 1, true, &pa))
			return;
		VERIFY_PA_WRITE(pa, 1, gpr(rs) >> sh);
		sh -= 8;
		if (sh < 0) {
			sh = 24;
			rs = (rs + 1) & 0x1f;
		}
	}

	increment_pc(4);
}

/**
 *	Load Word and Reserve Indexed / Store Word Conditional Indexed
 *
 *		RA		Input operand as base EA
 **/

template< class RA >
void powerpc_cpu::execute_lwarx(uint32 opcode)
{
	const uint32 ea = RA::get(this, opcode) + operand_RB::get(this, opcode);
	uint32 pa;
	if (!guest_data_xlate(ea, 4, false, &pa, true))
		return;
	uint32 reserve_data = VERIFY_PA_READ(pa, 4);
	regs().reserve_valid = 1;
	regs().reserve_addr = pa;
#if KPX_MAX_CPUS != 1
	regs().reserve_data = reserve_data;
#endif
	operand_RD::set(this, opcode, reserve_data);
	increment_pc(4);
}

template< class RA >
void powerpc_cpu::execute_stwcx(uint32 opcode)
{
	const uint32 ea = RA::get(this, opcode) + operand_RB::get(this, opcode);
	uint32 pa;
	if (!guest_data_xlate(ea, 4, true, &pa, true))
		return;
	cr().clear(0);
	if (regs().reserve_valid) {
		if (regs().reserve_addr == pa
#if KPX_MAX_CPUS != 1
			&& regs().reserve_data == VERIFY_PA_READ(pa, 4)
#endif
			) {
			VERIFY_PA_WRITE(pa, 4, operand_RS::get(this, opcode));
			cr().set(0, standalone_CR_EQ_field::mask());
		}
		regs().reserve_valid = 0;
	}
	cr().set_so(0, xer().get_so());
	increment_pc(4);
}

/**
 *	Floating-point compare instruction
 *
 *		OC		Predicate for ordered compare
 **/

template< bool OC >
void powerpc_cpu::execute_fp_compare(uint32 opcode)
{
	{
	const ppc_fp_environment fp_env(fpscr());
	const double a = operand_fp_RA::get(this, opcode);
	const double b = operand_fp_RB::get(this, opcode);
	const int crfd = crfD_field::extract(opcode);
	int c;

	if (is_NaN(a) || is_NaN(b))
		c = 1;
	else if (isless(a, b))
		c = 8;
	else if (isgreater(a, b))
		c = 4;
	else
		c = 2;

	FPSCR_FPCC_field::insert(fpscr(), c);
	cr().set(crfd, c);
	// Update FPSCR exception bits
	int exceptions = 0;
	if (is_SNaN(a) || is_SNaN(b)) {
		exceptions |= FPSCR_VXSNAN_field::mask();
		if (OC && !FPSCR_VE_field::test(fpscr()))
			exceptions |= FPSCR_VXVC_field::mask();
	}
	else if (OC && (is_QNaN(a) || is_QNaN(b)))
		exceptions |= FPSCR_VXVC_field::mask();
	if (ppc32_guest_mmu_enabled() || PPC_ENABLE_FPU_EXCEPTIONS) record_fpscr(exceptions);
	}

    if (ppc32_guest_mmu_enabled() && (fpscr() & 0x40000000u) && (ppc32_guest_mmu().msr() & 0x900u)) {
        take_program(0x00100000u); return;
    }
	increment_pc(4);
}

/**
 *	Floating Convert to Integer Word instructions
 *
 *		RN		Rounding mode
 *		Rc		Predicate to record CR1
 **/

template< class RN, class Rc >
void powerpc_cpu::execute_fp_int_convert(uint32 opcode)
{
    {
    const uint32 rn = RN::get(this, opcode);
    const ppc_fp_environment fp_env(rn);
    const double input = operand_fp_RB::get(this, opcode);
    const bool nan = is_NaN(input), signaling = is_SNaN(input);
    const double rounded = nan || isinf(input) ? input : nearbyint(input);
    const bool invalid = nan || rounded < -2147483648.0 || rounded > 2147483647.0;
    const bool track = ppc32_guest_mmu_enabled() || PPC_ENABLE_FPU_EXCEPTIONS;
    int exceptions = 0;
    if (track) fpscr() &= ~(FPSCR_FR_field::mask() | FPSCR_FI_field::mask());
    if (invalid) {
        exceptions = FPSCR_VXCVI_field::mask();
        if (signaling) exceptions |= FPSCR_VXSNAN_field::mask();
    } else if (rounded != input) {
        if (track) fpscr() |= FPSCR_FI_field::mask();
        exceptions = FPSCR_XX_field::mask();
        if (track && fabs(rounded) > fabs(input)) fpscr() |= FPSCR_FR_field::mask();
    }
    if (track) record_fpscr(exceptions);
    if (!(track && invalid && FPSCR_VE_field::test(fpscr()))) {
        any_register output;
        output.j = invalid ? nan || input < 0 ? int32(0x80000000u) : int32(0x7fffffffu) : int64(rounded);
        operand_fp_RD::set(this, opcode, output.d);
    }
    }
    if (Rc::test(opcode)) record_cr1();
    if (ppc32_guest_mmu_enabled() && (fpscr() & 0x40000000u) && (ppc32_guest_mmu().msr() & 0x900u)) {
        take_program(0x00100000u); return;
    }
    increment_pc(4);
}

/**
 *	Floating-point Round to Single
 *
 *		Rc		Predicate to record CR1
 **/

#ifndef FPCLASSIFY_RETURN_T
#ifdef __MINGW32__
#define FPCLASSIFY_RETURN_T int
#else
#define FPCLASSIFY_RETURN_T uint8
#endif
#endif

template< class FP >
void powerpc_cpu::fp_classify(FP x)
{
	uint32 c = fpscr() & ~FPSCR_FPRF_field::mask();
	FPCLASSIFY_RETURN_T fc = fpclassify(x);
	switch (fc) {
	case FP_NAN:
		c |= FPSCR_FPRF_FU_field::mask() | FPSCR_FPRF_C_field::mask();
		break;
	case FP_ZERO:
		c |= FPSCR_FPRF_FE_field::mask();
		if (signbit(x))
			c |= FPSCR_FPRF_C_field::mask();
		break;
	case FP_INFINITE:
		c |= FPSCR_FPRF_FU_field::mask();
		goto FL_FG_field;
	case FP_SUBNORMAL:
		c |= FPSCR_FPRF_C_field::mask();
		// fall-through
	case FP_NORMAL:
	  FL_FG_field:
		if (x < 0)
			c |= FPSCR_FPRF_FL_field::mask();
		else
			c |= FPSCR_FPRF_FG_field::mask();
		break;
	}
	fpscr() = c;
}

template< class Rc >
void powerpc_cpu::execute_fp_round(uint32 opcode)
{
    if (ppc32_guest_mmu_enabled()) {
        {
        const ppc_fp_environment fp_env(fpscr());
        const double input = operand_fp_RB::get(this,opcode);
        any_register source; source.d = input;
        const bool negative = signbit(input), nan = is_NaN(input), signaling = is_SNaN(input);
        uint32 causes = 0, classification = 0;
        any_register result; result.d = input;
        bool suppressed = false;
        if (nan) {
            fpscr() &= ~0x60000u;
            if (signaling) { causes = 0x01000000u; suppressed = (fpscr() & 0x80u) != 0; }
            result.j = (source.j | UVAL64(0x0008000000000000)) & ~UVAL64(0x1fffffff);
            classification = 17;
        } else if (isinf(input)) classification = negative ? 9 : 5;
        else if (input == 0) { fpscr() &= ~0x60000u; classification = negative ? 18 : 2; }
        else {
            // Numeric rounding is independent of the JIT's integer-significand
            // kernel. Scaling remains within double's exponent range even for
            // the smallest subnormal and the enabled +/-192 adjustments.
            int exponent;
            const double magnitude = fabs(input);
            const double fraction = frexp(input,&exponent);
            const bool tiny = magnitude < 0x1p-126, enabled_underflow = tiny && (fpscr() & 0x20u);
            const int scale = tiny && !enabled_underflow ? 149 : 24-exponent;
            const double scaled = tiny && !enabled_underflow ? ldexp(input,149) : ldexp(fraction,24);
            const double rounded = nearbyint(scaled);
            const bool inexact = rounded != scaled, increment = fabs(rounded) > fabs(scaled);
            fpscr() &= ~0x60000u;
            if (inexact) { fpscr() |= 0x20000u; causes |= 0x02000000u; }
            if (increment) fpscr() |= 0x40000u;
            if (tiny && (enabled_underflow || inexact)) causes |= 0x08000000u;
            // Compare before rescaling so rounding DBL_MAX cannot overflow
            // the host and destroy the enabled-overflow adjusted significand.
            const bool overflow = !tiny && (exponent > 128 || (exponent == 128 && fabs(rounded) == 0x1p24));
            if (overflow) causes |= 0x10000000u;
            if (overflow && !(fpscr() & 0x40u)) {
                const unsigned rn = fpscr() & 3u;
                const bool infinity = rn == 0 || (rn == 2 && !negative) || (rn == 3 && negative);
                result.d = copysign(infinity ? INFINITY : 0x1.fffffep127,input);
                fpscr() = (fpscr() & ~0x40000u) | 0x20000u; // undefined FR profile: zero
                causes |= 0x02000000u;
                classification = infinity ? negative ? 9 : 5 : negative ? 8 : 4;
            } else {
                result.d = ldexp(rounded,-scale + (enabled_underflow ? 192 : overflow ? -192 : 0));
                classification = result.d == 0 ? negative ? 18 : 2 :
                    tiny && !enabled_underflow && fabs(result.d) < 0x1p-126 ? negative ? 24 : 20 : negative ? 8 : 4;
            }
        }
        record_fpscr(causes);
        if (!suppressed) {
            operand_fp_RD::set(this,opcode,result.d);
            fpscr() = (fpscr() & ~0x1f000u) | (classification << 12);
        }
        } // Restore host rounding and flags before publishing the exception.
        if (Rc::test(opcode)) record_cr1();
        if ((fpscr() & 0x40000000u) && (ppc32_guest_mmu().msr() & 0x900u)) {
            take_program(0x00100000u); return;
        }
        increment_pc(4); return;
    }

	const ppc_fp_environment fp_env(fpscr());
	const double b = operand_fp_RB::get(this, opcode);

#if PPC_ENABLE_FPU_EXCEPTIONS
	int exceptions =
		fp_invalid_operation_condition<double>::
		apply(FPSCR_VXSNAN_field::mask(), b);

	feclearexcept(FE_ALL_EXCEPT);
	febarrier();
#endif

	float d = (float)b;
	// Update FPSCR exception bits
#if PPC_ENABLE_FPU_EXCEPTIONS
	febarrier();
	int raised = fetestexcept(FE_ALL_EXCEPT);
	if (raised & FE_UNDERFLOW)
		exceptions |= FPSCR_UX_field::mask();
	if (raised & FE_OVERFLOW)
		exceptions |= FPSCR_OX_field::mask();
	if (raised & FE_INEXACT)
		exceptions |= FPSCR_XX_field::mask();
	record_fpscr(exceptions);
#endif

	// FPSCR[FPRF] is set to the class and sign of the result
	if (!FPSCR_VE_field::test(fpscr()))
		fp_classify(d);
	// Set CR1 (FX, FEX, VX, VOX) if instruction has Rc set
	if (Rc::test(opcode))
		record_cr1();

	// Commit result to output operand
	operand_fp_RD::set(this, opcode, (double)d);
	increment_pc(4);

}

/**
 *		System Call instruction
 **/

void powerpc_cpu::execute_syscall(uint32 opcode)
{
	(void)opcode;
#ifdef SHEEPSHAVER
	if (ppc32_guest_mmu_enabled()) {
		take_sc();
		return;
	}
#endif
	cr().set_so(0, execute_do_syscall && !execute_do_syscall(this));
	increment_pc(4);
}

/**
 *		Instructions dealing with system registers
 **/

void powerpc_cpu::execute_mcrf(uint32 opcode)
{
	const int crfS = crfS_field::extract(opcode);
	const int crfD = crfD_field::extract(opcode);
	cr().set(crfD, cr().get(crfS));
	increment_pc(4);
}

void powerpc_cpu::execute_mcrfs(uint32 opcode)
{
	const int crfS = crfS_field::extract(opcode);
	const int crfD = crfD_field::extract(opcode);

	// The contents of FPSCR field crfS are copied to CR field crfD
	const uint32 m = 0xf << (28 - 4 * crfS);
	cr().set(crfD, (fpscr() & m) >> (28 - 4 * crfS));

	// All exception bits copied (except FEX and VX) are cleared in the FPSCR
	fpscr() &= ~(m & (FPSCR_FX_field::mask() | FPSCR_OX_field::mask() |
					  FPSCR_UX_field::mask() | FPSCR_ZX_field::mask() |
					  FPSCR_XX_field::mask() | FPSCR_VXSNAN_field::mask() |
					  FPSCR_VXISI_field::mask() | FPSCR_VXIDI_field::mask() |
					  FPSCR_VXZDZ_field::mask() | FPSCR_VXIMZ_field::mask() |
					  FPSCR_VXVC_field::mask() | FPSCR_VXSOFT_field::mask() |
					  FPSCR_VXSQRT_field::mask() | FPSCR_VXCVI_field::mask()));

	record_fpscr(0);
	increment_pc(4);
}

void powerpc_cpu::execute_mcrxr(uint32 opcode)
{
	const int crfD = crfD_field::extract(opcode);
	const uint32 x = xer().get();
	cr().set(crfD, x >> 28);
	xer().set(x & 0x0fffffff);
	increment_pc(4);
}

void powerpc_cpu::execute_mtcrf(uint32 opcode)
{
	uint32 mask = field2mask[CRM_field::extract(opcode)];
	cr().set((operand_RS::get(this, opcode) & mask) | (cr().get() & ~mask));
	increment_pc(4);
}

template< class FM, class RB, class Rc >
void powerpc_cpu::execute_mtfsf(uint32 opcode)
{
	const uint64 fsf = RB::get(this, opcode);
	const uint32 f = FM::get(this, opcode);
	uint32 m = field2mask[f];

	// FPSCR[FX] is altered only if FM[0] = 1
	if ((f & 0x80) == 0)
		m &= ~FPSCR_FX_field::mask();

	// The mtfsf instruction cannot alter FPSCR[FEX] nor FPSCR[VX] explicitly
	int exceptions = fsf & m;
	exceptions &= ~(FPSCR_FEX_field::mask() | FPSCR_VX_field::mask());

	// Move frB bits to FPSCR according to field mask
	fpscr() = (fpscr() & ~m) | exceptions;
	// Update FPSCR exception bits (don't implicitly update FX)
	record_fpscr(0);

	// Set CR1 (FX, FEX, VX, VOX) if instruction has Rc set
	if (Rc::test(opcode))
		record_cr1();

	increment_pc(4);
}

template< class RB, class Rc >
void powerpc_cpu::execute_mtfsfi(uint32 opcode)
{
	const uint32 crfD = crfD_field::extract(opcode);
	uint32 m = 0xf << (4 * (7 - crfD));

	// FPSCR[FX] is altered only if crfD = 0
	if (crfD != 0)
		m &= ~FPSCR_FX_field::mask();

	// The mtfsfi instruction cannot alter FPSCR[FEX] nor FPSCR[VX] explicitly
	int exceptions = (RB::get(this, opcode) << (4 * (7 - crfD))) & m;
	exceptions &= ~(FPSCR_FEX_field::mask() | FPSCR_VX_field::mask());

	// Move immediate to FPSCR according to field crfD
	fpscr() = (fpscr() & ~m) | exceptions;

	// Update FPSCR exception bits (don't implicitly update FX)
	record_fpscr(0);
	// Set CR1 (FX, FEX, VX, VOX) if instruction has Rc set
	if (Rc::test(opcode))
		record_cr1();

	increment_pc(4);
}

template< class RB, class Rc >
void powerpc_cpu::execute_mtfsb(uint32 opcode)
{
	const bool set_bit = RB::get(this, opcode);

	// The mtfsb0 and mtfsb1 instructions cannot alter FPSCR[FEX] nor FPSCR[VX] explicitly
	uint32 m = 1 << (31 - crbD_field::extract(opcode));
	m &= ~(FPSCR_FEX_field::mask() | FPSCR_VX_field::mask());

	// Bit crbD of the FPSCR is set or clear
	const uint32 old = fpscr();
	fpscr() &= ~m;
	// Update FPSCR exception bits
	if (set_bit) fpscr() |= m;
	if (set_bit && !(old & m) && (m & 0x1ff80700u)) fpscr() |= 0x80000000u;
	record_fpscr(0);

	// Set CR1 (FX, FEX, VX, VOX) if instruction has Rc set
	if (Rc::test(opcode))
		record_cr1();

	increment_pc(4);
}

template< class Rc >
void powerpc_cpu::execute_mffs(uint32 opcode)
{
	// Move FPSCR to FPR(FRD)
	operand_fp_dw_RD::set(this, opcode, fpscr());
	// Set CR1 (FX, FEX, VX, VOX) if instruction has Rc set
	if (Rc::test(opcode))
		record_cr1();

	increment_pc(4);
}

void powerpc_cpu::execute_mfmsr(uint32 opcode)
{
	if (ppc32_guest_mmu_enabled() && (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_PR)) {
		take_program(0x00040000u); return;
	}
	uint32 msr = 0xf072;
	if (ppc32_guest_mmu_enabled())
		msr = ppc32_guest_mmu().msr();
	operand_RD::set(this, opcode, msr);
	increment_pc(4);
}

void powerpc_cpu::execute_mtmsr(uint32 opcode)
{
	if (ppc32_guest_mmu_enabled() && (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_PR)) {
		take_program(0x00040000u); return;
	}
	if (ppc32_guest_mmu_enabled()) {
		const uint32 msr = operand_RS::get(this, opcode);
		const uint32 old = ppc32_guest_mmu().msr();
		ppc32_guest_mmu().set_msr(msr);
#ifdef SHEEPSHAVER
		nw_log_msr_dr(msr);
		nw_log_msr_write("mtmsr", pc(), msr);
		nw_jit_itlb_note_msr(old, msr);
#endif
		if ((old ^ msr) & 0x00000030u)
			nw_jit_dtlb_flush_src(NW_JIT_DTLB_FL_MTMSR);
	}
	increment_pc(4);
}

void powerpc_cpu::execute_mfsr(uint32 opcode)
{
	if (ppc32_guest_mmu_enabled() && (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_PR)) {
		take_program(0x00040000u); return;
	}
	uint32 d = 0;
	if (ppc32_guest_mmu_enabled())
		d = ppc32_guest_mmu().sr(rA_field::extract(opcode) & 0xfu);
	operand_RD::set(this, opcode, d);
	increment_pc(4);
}

void powerpc_cpu::execute_mtsr(uint32 opcode)
{
	if (ppc32_guest_mmu_enabled() && (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_PR)) {
		take_program(0x00040000u); return;
	}
	if (ppc32_guest_mmu_enabled()) {
		const unsigned i = rA_field::extract(opcode) & 0xfu;
		const uint32 val = operand_RS::get(this, opcode);
		ppc32_mmu &mmu = ppc32_guest_mmu();
		const uint32 old = mmu.sr(i);
		if (old != val) {
			mmu.set_sr(i, val);
			nw_jit_mtsr_note(i, old, val);
		}
	}
	increment_pc(4);
}

void powerpc_cpu::execute_mfsrin(uint32 opcode)
{
	if (ppc32_guest_mmu_enabled() && (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_PR)) {
		take_program(0x00040000u); return;
	}
	uint32 d = 0;
	if (ppc32_guest_mmu_enabled()) {
		const uint32 ea = operand_RB::get(this, opcode);
		d = ppc32_guest_mmu().sr((ea >> 28) & 0xfu);
	}
	operand_RD::set(this, opcode, d);
	increment_pc(4);
}

void powerpc_cpu::execute_mtsrin(uint32 opcode)
{
	if (ppc32_guest_mmu_enabled() && (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_PR)) {
		take_program(0x00040000u); return;
	}
	if (ppc32_guest_mmu_enabled()) {
		const uint32 ea = operand_RB::get(this, opcode);
		const unsigned i = (ea >> 28) & 0xfu;
		const uint32 val = operand_RS::get(this, opcode);
		ppc32_mmu &mmu = ppc32_guest_mmu();
		const uint32 old = mmu.sr(i);
		if (old != val) {
			mmu.set_sr(i, val);
			nw_jit_mtsr_note(i, old, val);
		}
	}
	increment_pc(4);
}

void powerpc_cpu::execute_rfi(uint32 opcode)
{
	if (ppc32_guest_mmu_enabled() && (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_PR)) {
		take_program(0x00040000u); return;
	}
	(void)opcode;
	if (ppc32_guest_mmu_enabled()) {
		finish_fpu_rfi();
		const uint32 old = ppc32_guest_mmu().msr();
		ppc32_guest_mmu().set_msr(srr1_);
#ifdef SHEEPSHAVER
		nw_log_msr_dr(srr1_);
		nw_log_msr_write("rfi", srr0_, srr1_);
		nw_jit_itlb_note_msr(old, srr1_);
		nw_log_emu_rfi(srr0_, srr1_, cr().get(), sprg(0));
#endif
		nw_jit_dtlb_flush_if_pr(old, srr1_, NW_JIT_DTLB_FL_RFI);
		pc() = srr0_ & ~3u;
		return;
	}
	increment_pc(4);
}

void powerpc_cpu::execute_tlbie(uint32 opcode)
{
	if (ppc32_guest_mmu_enabled() && (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_PR)) {
		take_program(0x00040000u); return;
	}
	if (ppc32_guest_mmu_enabled()) {
		const uint32 ea = operand_RB::get(this, opcode);
		ppc32_guest_mmu().tlbie(ea);
		nw_jit_dtlb_drop_page(ea, NW_JIT_DTLB_FL_TLB);
		invalidate_cache();
	}
	increment_pc(4);
}

void powerpc_cpu::execute_tlbia(uint32 opcode)
{
	if (ppc32_guest_mmu_enabled() && (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_PR)) {
		take_program(0x00040000u); return;
	}
	(void)opcode;
	if (ppc32_guest_mmu_enabled()) {
		ppc32_guest_mmu().tlbia();
		nw_jit_dtlb_flush_src(NW_JIT_DTLB_FL_TLB);
		invalidate_cache();
	}
	increment_pc(4);
}

void powerpc_cpu::execute_tlbsync(uint32 opcode)
{
	if (ppc32_guest_mmu_enabled() && (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_PR)) {
		take_program(0x00040000u); return;
	}
	(void)opcode;
	increment_pc(4);
}

template< class SPR >
void powerpc_cpu::execute_mfspr(uint32 opcode)
{
	const uint32 spr = SPR::get(this, opcode);
	uint32 d;
	if (ppc32_guest_mmu_enabled()) {
		const spr_access_result result = mfspr_guest(spr, &d);
#ifdef SHEEPSHAVER
		nw_verify_system_status_ = result == SPR_ACCESS_NOP ? NW_SYS_NOP : result == SPR_ACCESS_EXC ? NW_SYS_PRIV : NW_SYS_OK;
#endif
		switch (result) {
		case SPR_ACCESS_OK:
			operand_RD::set(this, opcode, d);
			/* fall through */
		case SPR_ACCESS_NOP:
			increment_pc(4);
			return;
		case SPR_ACCESS_EXC:
			return;
		}
	}
	switch (spr) {
	case powerpc_registers::SPR_XER:	d = xer().get();break;
	case powerpc_registers::SPR_LR:		d = lr();		break;
	case powerpc_registers::SPR_CTR:	d = ctr();		break;
	case powerpc_registers::SPR_VRSAVE:	d = vrsave();	break;
#ifdef SHEEPSHAVER
	case powerpc_registers::SPR_SDR1:	d = 0xdead001f;	break;
	case powerpc_registers::SPR_PVR: {
		extern uint32 PVR;
		d = PVR;
		break;
	}
	default: d = 0;
#else
	default: execute_illegal(opcode);
#endif
	}
	operand_RD::set(this, opcode, d);
	increment_pc(4);
}

template< class SPR >
void powerpc_cpu::execute_mtspr(uint32 opcode)
{
	const uint32 spr = SPR::get(this, opcode);
	const uint32 s = operand_RS::get(this, opcode);

	if (ppc32_guest_mmu_enabled()) {
		const spr_access_result result = mtspr_guest(spr, s);
#ifdef SHEEPSHAVER
		nw_verify_system_status_ = result == SPR_ACCESS_NOP ? NW_SYS_NOP : result == SPR_ACCESS_EXC ? NW_SYS_PRIV : NW_SYS_OK;
#endif
		if (result != SPR_ACCESS_EXC)
			increment_pc(4);
		return;
	}

	switch (spr) {
	case powerpc_registers::SPR_XER:	xer().set(s);	break;
	case powerpc_registers::SPR_LR:		lr() = s;		break;
	case powerpc_registers::SPR_CTR:	ctr() = s;		break;
	case powerpc_registers::SPR_VRSAVE:	vrsave() = s;	break;
#ifndef SHEEPSHAVER
	default: execute_illegal(opcode);
#endif
	}

	increment_pc(4);
}

// Compute with 96 bit intermediate result: (a * b) / c
static uint64 muldiv64(uint64 a, uint32 b, uint32 c)
{
	union {
		uint64 ll;
		struct {
#ifdef WORDS_BIGENDIAN
			uint32 high, low;
#else
			uint32 low, high;
#endif
		} l;
	} u, res;

	u.ll = a;
	uint64 rl = (uint64)u.l.low * (uint64)b;
	uint64 rh = (uint64)u.l.high * (uint64)b;
	rh += (rl >> 32);
	res.l.high = (uint32)(rh / c);
	res.l.low = (uint32)((((rh % c) << 32) + (rl & 0xffffffff)) / c);
	return res.ll;
}

static inline uint64 get_tb_ticks(void)
{
	uint64 ticks;
#ifdef SHEEPSHAVER
	const uint32 TBFreq = (uint32)TimebaseSpeed;
	ticks = muldiv64(GetTicks_usec(), TBFreq, 1000000);
#else
	const uint32 TBFreq = 25 * 1000 * 1000; // 25 MHz
	ticks = muldiv64((uint64)clock(), TBFreq, CLOCKS_PER_SEC);
#endif
	return ticks;
}

template< class TBR >
void powerpc_cpu::execute_mftbr(uint32 opcode)
{
	uint32 tbr = TBR::get(this, opcode);
	uint32 d = 0;
	/* Under the guest MMU the timebase honours mtspr TBL/TBU (tb_offset_). */
	const uint64 tb = ppc32_guest_mmu_enabled() ? tb_ticks() : get_tb_ticks();
	switch (tbr) {
	case 268: d = (uint32)tb; break;
	case 269: d = (uint32)(tb >> 32); break;
	default: execute_illegal(opcode); return;
	}
	operand_RD::set(this, opcode, d);
	increment_pc(4);
}

/**
 *		Instruction cache management
 **/

void powerpc_cpu::execute_invalidate_cache_range()
{
	if (cache_range.start != cache_range.end) {
#ifdef SHEEPSHAVER
		invalidate_cache_range(cache_range.start, cache_range.end, NW_JIT_FL_ICBI);
#else
		invalidate_cache_range(cache_range.start, cache_range.end);
#endif
		cache_range.start = cache_range.end = 0;
	}
}

template< class RA, class RB >
void powerpc_cpu::execute_icbi(uint32 opcode)
{
	const uint32 ea = RA::get(this, opcode) + RB::get(this, opcode);
	const uint32 block_start = ea - (ea % 32);

	if (block_start == cache_range.end) {
		// Extend region to invalidate
		cache_range.end += 32;
	}
	else {
		// New region to invalidate
		execute_invalidate_cache_range();
		cache_range.start = block_start;
		cache_range.end = cache_range.start + 32;
	}

	increment_pc(4);
}

void powerpc_cpu::execute_dcbi(uint32 opcode)
{
	if (ppc32_guest_mmu_enabled() && (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_PR)) {
		take_program(0x00040000u); return;
	}
	increment_pc(4); // no data-cache model
}

void powerpc_cpu::execute_isync(uint32 opcode)
{
	execute_invalidate_cache_range();
	increment_pc(4);
}

/**
 *		(Fake) data cache management
 **/

template< class RA, class RB >
void powerpc_cpu::execute_dcbz(uint32 opcode)
{
	uint32 ea = (RA::get(this, opcode) + RB::get(this, opcode)) & ~31u;
	uint32 pa;
	if (!guest_data_xlate(ea, 32, true, &pa))
		return;
	if (!pa_is_io(pa) && !pa_is_rom(pa)) {
		const uint32 base = pa - (pa % 32);
		vm_memset(base, 0, 32);
#ifdef SHEEPSHAVER
		nw_verify_system_store_pa_ = base;
		nw_fb_damage_store(base, 32);
		nw_jit_invalidate_page_src(base, NW_JIT_FL_STORE);
		invalidate_cache();
#endif
	}
	increment_pc(4);
}

/**
 *		Vector load/store instructions
 **/

template< bool SL >
void powerpc_cpu::execute_vector_load_for_shift(uint32 opcode)
{
	const uint32 ra = operand_RA_or_0::get(this, opcode);
	const uint32 rb = operand_RB::get(this, opcode);
	const uint32 ea = ra + rb;
	powerpc_vr & vD = vr(vD_field::extract(opcode));
	int j = SL ? (ea & 0xf) : (0x10 - (ea & 0xf));
	for (int i = 0; i < 16; i++)
		vD.b[ev_mixed::byte_element(i)] = j++;
	increment_pc(4);
}

template< class VD, class RA, class RB >
void powerpc_cpu::execute_vector_load(uint32 opcode)
{
	uint32 ea = RA::get(this, opcode) + RB::get(this, opcode);
	typename VD::type & vD = VD::ref(this, opcode);
	uint32 pa;
	switch (VD::element_size) {
	case 1:
		if (!guest_data_xlate(ea, 1, false, &pa))
			return;
		VD::set_element(vD, (ea & 0x0f), VERIFY_PA_READ(pa, 1));
		break;
	case 2:
		if (!guest_data_xlate(ea & ~1, 2, false, &pa))
			return;
		VD::set_element(vD, ((ea >> 1) & 0x07), VERIFY_PA_READ(pa, 2));
		break;
	case 4:
		if (!guest_data_xlate(ea & ~3, 4, false, &pa))
			return;
		VD::set_element(vD, ((ea >> 2) & 0x03), VERIFY_PA_READ(pa, 4));
		break;
	case 8:
		ea &= ~15;
		if (!guest_data_xlate(ea, 16, false, &pa, true))
			return;
		for (unsigned i = 0; i < 4; ++i) {
#ifdef SHEEPSHAVER
			if (nw_verify_trace_) nw_verify_ea_ = ea + i * 4;
#endif
			vD.w[i] = VERIFY_PA_READ(pa + i * 4, 4);
		}
		break;
	}
	increment_pc(4);
}

template< class VS, class RA, class RB >
void powerpc_cpu::execute_vector_store(uint32 opcode)
{
	uint32 ea = RA::get(this, opcode) + RB::get(this, opcode);
	typename VS::type & vS = VS::ref(this, opcode);
	uint32 pa;
	switch (VS::element_size) {
	case 1:
		if (!guest_data_xlate(ea, 1, true, &pa))
			return;
		VERIFY_PA_WRITE(pa, 1, VS::get_element(vS, (ea & 0x0f)));
		break;
	case 2:
		if (!guest_data_xlate(ea & ~1, 2, true, &pa))
			return;
		VERIFY_PA_WRITE(pa, 2, VS::get_element(vS, ((ea >> 1) & 0x07)));
		break;
	case 4:
		if (!guest_data_xlate(ea & ~3, 4, true, &pa))
			return;
		VERIFY_PA_WRITE(pa, 4, VS::get_element(vS, ((ea >> 2) & 0x03)));
		break;
	case 8:
		ea &= ~15;
		if (!guest_data_xlate(ea, 16, true, &pa, true))
			return;
		for (unsigned i = 0; i < 4; ++i) {
#ifdef SHEEPSHAVER
			if (nw_verify_trace_) nw_verify_ea_ = ea + i * 4;
#endif
			VERIFY_PA_WRITE(pa + i * 4, 4, vS.w[i]);
		}
		break;
	}
	increment_pc(4);
}

/**
 *	Vector arithmetic
 *
 *		OP		Operation to perform on element
 *		VD		Output operand vector
 *		VA		Input operand vector
 *		VB		Input operand vector (optional: operand_NONE)
 *		VC		Input operand vector (optional: operand_NONE)
 *		Rc		Predicate to record CR6
 *		C1		If recording CR6, do we check for '1' bits in vD?
 **/

template<class T> static T vector_fp_input(T x, bool) { return x; }
static float vector_fp_input(float x, bool nj) {
	uint32 bits; memcpy(&bits, &x, 4);
	if (nj && (bits & 0x7f800000u) == 0) bits &= 0x80000000u;
	memcpy(&x, &bits, 4); return x;
}
template<class T> static uint32 vector_fp_nan_bits(T) { return 0; }
template<class T> static bool vector_fp_negative(T) { return false; }
static bool vector_fp_negative(float x) { return x < 0; }
static uint32 vector_fp_nan_bits(float x) {
	uint32 bits; memcpy(&bits, &x, 4);
	return (bits & 0x7fffffffu) > 0x7f800000u ? bits : 0;
}
template<class T, class A, class B, class C>
static T vector_fp_result(T x, A, B, C, bool) { return x; }
template<class A, class B, class C>
static float vector_fp_result(float x, A a, B b, C c, bool nj) {
	// AltiVec selects A, then B, then C, even when a later NaN signals.
	const uint32 an = vector_fp_nan_bits(a), bn = vector_fp_nan_bits(b), cn = vector_fp_nan_bits(c);
	uint32 bits; memcpy(&bits, &x, 4);
	if (an || bn || cn) bits = (an ? an : bn ? bn : cn) | 0x00400000u;
	else if ((bits & 0x7fffffffu) > 0x7f800000u) bits = 0x7fc00000u;
	if (nj && !(bits & 0x7f800000u)) bits &= 0x80000000u;
	memcpy(&x, &bits, 4); return x;
}

template< class OP, class VD, class VA, class VB, class VC, class Rc, int C1 >
void powerpc_cpu::execute_vector_arith(uint32 opcode)
{
	const bool fp = std::is_same<typename VA::element_type, float>::value ||
		std::is_same<typename VB::element_type, float>::value ||
		std::is_same<typename VC::element_type, float>::value ||
		std::is_same<typename VD::element_type, float>::value;
	const ppc_fp_environment fp_env(0, fp);
	const bool nj = fp && (vscr().get() & 0x10000u) && (opcode & 2047u) != 522;
	typename VA::type const & vA = VA::const_ref(this, opcode);
	typename VB::type const & vB = VB::const_ref(this, opcode);
	typename VC::type const & vC = VC::const_ref(this, opcode);
	typename VD::type & vD = VD::ref(this, opcode);
	const int n_elements = 16 / VD::element_size;

	for (int i = 0; i < n_elements; i++) {
		const typename VA::element_type a = vector_fp_input(VA::get_element(vA, i), nj);
		const typename VB::element_type b = vector_fp_input(VB::get_element(vB, i), nj);
		const typename VC::element_type c = vector_fp_input(VC::get_element(vC, i), nj);
		typename VD::element_type d = op_apply<typename VD::element_type, OP, VA, VB, VC>::apply(a, b, c);
		// Preserve saturation for negative unsigned-conversion fractions
		// that would otherwise truncate to zero before VD sees the value.
		if (fp && (opcode & 2047u) == 906 && vector_fp_negative(b)) d = -1;
		d = vector_fp_result(d, a, b, c, nj);
		if (VD::saturate(d))
			vscr().set_sat(1);
		VD::set_element(vD, i, d);
	}

	// Propagate all conditions to CR6
	if (Rc::test(opcode))
		record_cr6(vD, C1);

	increment_pc(4);
}

/**
 *	Vector mixed arithmetic
 *
 *		OP		Operation to perform on element
 *		VD		Output operand vector
 *		VA		Input operand vector
 *		VB		Input operand vector (optional: operand_NONE)
 *		VC		Input operand vector (optional: operand_NONE)
 **/

template< class OP, class VD, class VA, class VB, class VC >
void powerpc_cpu::execute_vector_arith_mixed(uint32 opcode)
{
	typename VA::type const & vA = VA::const_ref(this, opcode);
	typename VB::type const & vB = VB::const_ref(this, opcode);
	typename VC::type const & vC = VC::const_ref(this, opcode);
	typename VD::type & vD = VD::ref(this, opcode);
	const int n_elements = 16 / VD::element_size;
	const int n_sub_elements = 4 / VA::element_size;

	for (int i = 0; i < n_elements; i++) {
		const typename VC::element_type c = VC::get_element(vC, i);
		typename VD::element_type d = c;
		for (int j = 0; j < n_sub_elements; j++) {
			const typename VA::element_type a = VA::get_element(vA, i * n_sub_elements + j);
			const typename VB::element_type b = VB::get_element(vB, i * n_sub_elements + j);
			d += op_apply<typename VD::element_type, OP, VA, VB, null_vector_operand>::apply(a, b, c);
		}
		if (VD::saturate(d))
			vscr().set_sat(1);
		VD::set_element(vD, i, d);
	}

	increment_pc(4);
}

/**
 *	Vector odd/even arithmetic
 *
 *		ODD		Flag: are we computing every odd element?
 *		OP		Operation to perform on element
 *		VD		Output operand vector
 *		VA		Input operand vector
 *		VB		Input operand vector (optional: operand_NONE)
 *		VC		Input operand vector (optional: operand_NONE)
 **/

template< int ODD, class OP, class VD, class VA, class VB, class VC >
void powerpc_cpu::execute_vector_arith_odd(uint32 opcode)
{
	typename VA::type const & vA = VA::const_ref(this, opcode);
	typename VB::type const & vB = VB::const_ref(this, opcode);
	typename VC::type const & vC = VC::const_ref(this, opcode);
	typename VD::type & vD = VD::ref(this, opcode);
	const int n_elements = 16 / VD::element_size;

	for (int i = 0; i < n_elements; i++) {
		const typename VA::element_type a = VA::get_element(vA, (i * 2) + ODD);
		const typename VB::element_type b = VB::get_element(vB, (i * 2) + ODD);
		const typename VC::element_type c = VC::get_element(vC, (i * 2) + ODD);
		typename VD::element_type d = op_apply<typename VD::element_type, OP, VA, VB, VC>::apply(a, b, c);
		if (VD::saturate(d))
			vscr().set_sat(1);
		VD::set_element(vD, i, d);
	}

	increment_pc(4);
}

/**
 *	Vector merge instructions
 *
 *		OP		Operation to perform on element
 *		VD		Output operand vector
 *		VA		Input operand vector
 *		VB		Input operand vector (optional: operand_NONE)
 *		VC		Input operand vector (optional: operand_NONE)
 *		LO		Flag: use lower part of element
 *
 *	This and the other element-moving instructions below (pack, unpack,
 *	shift by octets, permute, sum) take their inputs by value: vD may be
 *	one of the inputs (e.g. vmrghb v17,v0,v17 in Mac OS 9.2's QuickDraw
 *	text blend) and elements are read after earlier ones were written.
 **/

template< class VD, class VA, class VB, int LO >
void powerpc_cpu::execute_vector_merge(uint32 opcode)
{
	typename VA::type const vA = VA::const_ref(this, opcode);
	typename VB::type const vB = VB::const_ref(this, opcode);
	typename VD::type & vD = VD::ref(this, opcode);
	const int n_elements = 16 / VD::element_size;

	for (int i = 0; i < n_elements; i += 2) {
		VD::set_element(vD, i    , VA::get_element(vA, (i / 2) + LO * (n_elements / 2)));
		VD::set_element(vD, i + 1, VB::get_element(vB, (i / 2) + LO * (n_elements / 2)));
	}

	increment_pc(4);
}

/**
 *	Vector pack/unpack instructions
 *
 *		OP		Operation to perform on element
 *		VD		Output operand vector
 *		VA		Input operand vector
 *		VB		Input operand vector (optional: operand_NONE)
 *		VC		Input operand vector (optional: operand_NONE)
 *		LO		Flag: use lower part of element
 **/

template< class VD, class VA, class VB >
void powerpc_cpu::execute_vector_pack(uint32 opcode)
{
	typename VA::type const vA = VA::const_ref(this, opcode);
	typename VB::type const vB = VB::const_ref(this, opcode);
	typename VD::type & vD = VD::ref(this, opcode);
	const int n_elements = 16 / VD::element_size;
	const int n_pivot = n_elements / 2;

	for (int i = 0; i < n_elements; i++) {
		typename VD::element_type d;
		if (i < n_pivot)
			d = VA::get_element(vA, i);
		else
			d = VB::get_element(vB, i - n_pivot);
		if (VD::saturate(d))
			vscr().set_sat(1);
		VD::set_element(vD, i, d);
	}

	increment_pc(4);
}

template< int LO, class VD, class VA >
void powerpc_cpu::execute_vector_unpack(uint32 opcode)
{
	typename VA::type const vA = VA::const_ref(this, opcode);
	typename VD::type & vD = VD::ref(this, opcode);
	const int n_elements = 16 / VD::element_size;

	for (int i = 0; i < n_elements; i++)
		VD::set_element(vD, i, VA::get_element(vA, i + LO * n_elements));

	increment_pc(4);
}

void powerpc_cpu::execute_vector_pack_pixel(uint32 opcode)
{
	powerpc_vr const vA = vr(vA_field::extract(opcode));
	powerpc_vr const vB = vr(vB_field::extract(opcode));
	powerpc_vr & vD = vr(vD_field::extract(opcode));

	for (int i = 0; i < 4; i++) {
		const uint32 a = vA.w[i];
		vD.h[ev_mixed::half_element(i)] = ((a >> 9) & 0xfc00) | ((a >> 6) & 0x03e0) | ((a >> 3) & 0x001f);
		const uint32 b = vB.w[i];
		vD.h[ev_mixed::half_element(i + 4)] = ((b >> 9) & 0xfc00) | ((b >> 6) & 0x03e0) | ((b >> 3) & 0x001f);
	}

	increment_pc(4);
}

template< int LO >
void powerpc_cpu::execute_vector_unpack_pixel(uint32 opcode)
{
	powerpc_vr const vB = vr(vB_field::extract(opcode));
	powerpc_vr & vD = vr(vD_field::extract(opcode));

	for (int i = 0; i < 4; i++) {
		const uint32 h = vB.h[ev_mixed::half_element(i + LO * 4)];
		vD.w[i] = (((h & 0x8000) ? 0xff000000 : 0) |
				   ((h & 0x7c00) << 6) |
				   ((h & 0x03e0) << 3) |
				   (h & 0x001f));
	}

	increment_pc(4);
}

/**
 *	Vector shift instructions
 *
 *		SD		Shift direction: left (-1), right (+1)
 *		OP		Operation to perform on element
 *		VD		Output operand vector
 *		VA		Input operand vector
 *		VB		Input operand vector (optional: operand_NONE)
 *		VC		Input operand vector (optional: operand_NONE)
 *		SH		Shift count operand
 **/

template< int SD >
void powerpc_cpu::execute_vector_shift(uint32 opcode)
{
	powerpc_vr const vA = vr(vA_field::extract(opcode));
	powerpc_vr const vB = vr(vB_field::extract(opcode));
	powerpc_vr & vD = vr(vD_field::extract(opcode));

	// The contents of the low-order three bits of all byte
	// elements in vB must be identical to vB[125-127]; otherwise
	// the value placed into vD is undefined.
	const int sh = vB.b[ev_mixed::byte_element(15)] & 7;
	if (sh == 0) {
		for (int i = 0; i < 4; i++)
			vD.w[i] = vA.w[i];
	}
	else {
		uint32 prev_bits = 0;
		if (SD < 0) {
			for (int i = 3; i >= 0; i--) {
				uint32 next_bits = vA.w[i] >> (32 - sh);
				vD.w[i] = ((vA.w[i] << sh) | prev_bits);
				prev_bits = next_bits;
			}
		}
		else if (SD > 0) {
			for (int i = 0; i < 4; i++) {
				uint32 next_bits = vA.w[i] << (32 - sh);
				vD.w[i] = ((vA.w[i] >> sh) | prev_bits);
				prev_bits = next_bits;
			}
		}
	}

	increment_pc(4);
}

template< int SD, class VD, class VA, class VB, class SH >
void powerpc_cpu::execute_vector_shift_octet(uint32 opcode)
{
	typename VA::type const vA = VA::const_ref(this, opcode);
	typename VB::type const vB = VB::const_ref(this, opcode);
	typename VD::type & vD = VD::ref(this, opcode);

	const int sh = SH::get(this, opcode);
	if (SD < 0) {
		for (int i = 0; i < 16; i++) {
			if (i + sh < 16)
				VD::set_element(vD, i, VA::get_element(vA, i + sh));
			else
				VD::set_element(vD, i, VB::get_element(vB, i - (16 - sh)));
		}
	}
	else if (SD > 0) {
		for (int i = 0; i < 16; i++) {
			if (i < sh)
				VD::set_element(vD, i, VB::get_element(vB, 16 - (i - sh)));
			else
				VD::set_element(vD, i, VA::get_element(vA, i - sh));
		}
	}

	increment_pc(4);
}

/**
 *	Vector splat instructions
 *
 *		OP		Operation to perform on element
 *		VD		Output operand vector
 *		VA		Input operand vector
 *		VB		Input operand vector (optional: operand_NONE)
 *		IM		Immediate value to replicate
 **/

template< class OP, class VD, class VB, bool IM >
void powerpc_cpu::execute_vector_splat(uint32 opcode)
{
	typename VD::type & vD = VD::ref(this, opcode);
	const int n_elements = 16 / VD::element_size;

	uint32 value;
	if (IM)
		value = OP::apply(vUIMM_field::extract(opcode));
	else {
		typename VB::type const & vB = VB::const_ref(this, opcode);
		const int n = vUIMM_field::extract(opcode) & (n_elements - 1);
		value = OP::apply(VB::get_element(vB, n));
	}

	for (int i = 0; i < n_elements; i++)
		VD::set_element(vD, i, value);

	increment_pc(4);
}

/**
 *	Vector sum instructions
 *
 *		SZ		Size of destination vector elements
 *		VD		Output operand vector
 *		VA		Input operand vector
 *		VB		Input operand vector (optional: operand_NONE)
 **/

template< int SZ, class VD, class VA, class VB >
void powerpc_cpu::execute_vector_sum(uint32 opcode)
{
	typename VA::type const vA = VA::const_ref(this, opcode);
	typename VB::type const vB = VB::const_ref(this, opcode);
	typename VD::type & vD = VD::ref(this, opcode);
	typename VD::element_type d;
	
	switch (SZ) {
	case 1: // vsum
		d = VB::get_element(vB, 3);
		for (int j = 0; j < 4; j++)
			d += VA::get_element(vA, j);
		if (VD::saturate(d))
			vscr().set_sat(1);
		VD::set_element(vD, 0, 0);
		VD::set_element(vD, 1, 0);
		VD::set_element(vD, 2, 0);
		VD::set_element(vD, 3, d);
		break;

	case 2: // vsum2
		for (int i = 0; i < 4; i += 2) {
			d = VB::get_element(vB, i + 1);
			for (int j = 0; j < 2; j++)
				d += VA::get_element(vA, i + j);
			if (VD::saturate(d))
				vscr().set_sat(1);
			VD::set_element(vD, i + 0, 0);
			VD::set_element(vD, i + 1, d);
		}
		break;

	case 4: // vsum4
		for (int i = 0; i < 4; i += 1) {
			d = VB::get_element(vB, i);
			const int n_elements = 4 / VA::element_size;
			for (int j = 0; j < n_elements; j++)
				d += VA::get_element(vA, i * n_elements + j);
			if (VD::saturate(d))
				vscr().set_sat(1);
			VD::set_element(vD, i, d);
		}
		break;
	}

	increment_pc(4);
}

/**
 *		Misc vector instructions
 **/

void powerpc_cpu::execute_vector_permute(uint32 opcode)
{
	powerpc_vr const vA = vr(vA_field::extract(opcode));
	powerpc_vr const vB = vr(vB_field::extract(opcode));
	powerpc_vr const vC = vr(vC_field::extract(opcode));
	powerpc_vr & vD = vr(vD_field::extract(opcode));

	for (int i = 0; i < 16; i++) {
		const int ei = ev_mixed::byte_element(i);
		const int n  = vC.b[ei] & 0x1f;
		const int en = ev_mixed::byte_element(n & 0xf);
		vD.b[ei] = (n & 0x10) ? vB.b[en] : vA.b[en];
	}

	increment_pc(4);
}

void powerpc_cpu::execute_mfvscr(uint32 opcode)
{
	const int vD = vD_field::extract(opcode);
	vr(vD).w[0] = 0;
	vr(vD).w[1] = 0;
	vr(vD).w[2] = 0;
	vr(vD).w[3] = vscr().get();
	increment_pc(4);
}

void powerpc_cpu::execute_mtvscr(uint32 opcode)
{
	const int vB = vB_field::extract(opcode);
	vscr().set(vr(vB).w[3]);
	increment_pc(4);
}

/**
 *		Explicit template instantiations
 **/

#include "ppc-execute-impl.cpp"
