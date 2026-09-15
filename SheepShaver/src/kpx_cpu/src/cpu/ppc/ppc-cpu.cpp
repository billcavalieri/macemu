/*
 *  ppc-cpu.cpp - PowerPC CPU definition
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
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "vm_alloc.h"
#include "cpu/vm.hpp"
#include "cpu/ppc/ppc-cpu.hpp"
#ifndef SHEEPSHAVER
#include "basic-kernel.hpp"
#endif

#if PPC_ENABLE_JIT
#include "cpu/jit/dyngen-exec.h"
#endif

#if ENABLE_MON
#include "mon.h"
#include "mon_disass.h"
#endif

#ifdef SHEEPSHAVER
#include "nw_boot_contract.h"
#include "nw_io.h"
#include "nw_devices.h"
#include "nw_script.h"
#include "nw_jit.h"
#endif

#define DEBUG 0
#include "debug.h"

#if PPC_PROFILE_GENERIC_CALLS
uint32 powerpc_cpu::generic_calls_count[PPC_I(MAX)];
static int generic_calls_ids[PPC_I(MAX)];
const int generic_calls_top_ten = 20;

int generic_calls_compare(const void *e1, const void *e2)
{
	const int id1 = *(const int *)e1;
	const int id2 = *(const int *)e2;
	return powerpc_cpu::generic_calls_count[id2] - powerpc_cpu::generic_calls_count[id1];
}
#endif

#if PPC_PROFILE_REGS_USE
int register_info_compare(const void *e1, const void *e2)
{
	const powerpc_cpu::register_info *ri1 = (powerpc_cpu::register_info *)e1;
	const powerpc_cpu::register_info *ri2 = (powerpc_cpu::register_info *)e2;
	return ri2->count - ri1->count;
}
#endif

static int ppc_refcount = 0;

#ifdef DO_CONVENTION_CALL_STATICS
template<> bool nv_mem_fun1_t<void, powerpc_cpu, uint32>::do_convention_call_init_done = false;
template<> int nv_mem_fun1_t<void, powerpc_cpu, uint32>::do_convention_call_code_len = 0;
template<> int nv_mem_fun1_t<void, powerpc_cpu, uint32>::do_convention_call_pf_offset = 0;
#endif

void powerpc_cpu::set_register(int id, any_register const & value)
{
	if (id >= powerpc_registers::GPR(0) && id <= powerpc_registers::GPR(31)) {
		gpr(id - powerpc_registers::GPR_BASE) = value.i;
		return;
	}
	if (id >= powerpc_registers::FPR(0) && id <= powerpc_registers::FPR(31)) {
		fpr(id - powerpc_registers::FPR_BASE) = value.d;
		return;
	}
	switch (id) {
	case powerpc_registers::CR:			cr().set(value.i);		break;
	case powerpc_registers::FPSCR:		fpscr() = value.i;		break;
	case powerpc_registers::XER:		xer().set(value.i);		break;
	case powerpc_registers::LR:			lr() = value.i;			break;
	case powerpc_registers::CTR:		ctr() = value.i;		break;
	case basic_registers::PC:
	case powerpc_registers::PC:			pc() = value.i;			break;
	case basic_registers::SP:
	case powerpc_registers::SP:			gpr(1)= value.i;		break;
	default:							abort();				break;
	}
}

any_register powerpc_cpu::get_register(int id)
{
	any_register value;
	if (id >= powerpc_registers::GPR(0) && id <= powerpc_registers::GPR(31)) {
		value.i = gpr(id - powerpc_registers::GPR_BASE);
		return value;
	}
	if (id >= powerpc_registers::FPR(0) && id <= powerpc_registers::FPR(31)) {
		value.d = fpr(id - powerpc_registers::FPR_BASE);
		return value;
	}
	switch (id) {
	case powerpc_registers::CR:			value.i = cr().get();	break;
	case powerpc_registers::FPSCR:		value.i = fpscr();		break;
	case powerpc_registers::XER:		value.i = xer().get();	break;
	case powerpc_registers::LR:			value.i = lr();			break;
	case powerpc_registers::CTR:		value.i = ctr();		break;
	case basic_registers::PC:
	case powerpc_registers::PC:			value.i = pc();			break;
	case basic_registers::SP:
	case powerpc_registers::SP:			value.i = gpr(1);		break;
	default:							abort();				break;
	}
	return value;
}

#if KPX_MAX_CPUS != 1
uint32 powerpc_registers::reserve_valid = 0;
uint32 powerpc_registers::reserve_addr = 0;
uint32 powerpc_registers::reserve_data = 0;
#endif

void powerpc_cpu::init_registers()
{
	assert((((uintptr)&vr(0)) % 16) == 0);
	for (int i = 0; i < 32; i++) {
		gpr(i) = 0;
		fpr(i) = 0;
	}
	cr().set(0);
	fpscr() = 0;
	xer().set(0);
	lr() = 0;
	ctr() = 0;
	pc() = 0;
	srr0_ = 0;
	srr1_ = 0;
	dar_ = 0;
	dsisr_ = 0;
	for (int i = 0; i < 4; i++)
		sprg_[i] = 0;
}

void powerpc_cpu::init_flight_recorder()
{
#if PPC_FLIGHT_RECORDER
	log_ptr = 0;
	log_ptr_wrapped = false;
#endif
}

void powerpc_cpu::do_record_step(uint32 pc, uint32 opcode)
{
#if PPC_FLIGHT_RECORDER
	log[log_ptr].pc = pc;
	log[log_ptr].opcode = opcode;
#ifdef SHEEPSHAVER
	log[log_ptr].sp = gpr(1);
	log[log_ptr].r24 = gpr(24);
#endif
#if PPC_FLIGHT_RECORDER >= 2
	for (int i = 0; i < 32; i++) {
		log[log_ptr].r[i] = gpr(i);
		log[log_ptr].fr[i] = fpr(i);
	}
	log[log_ptr].lr = lr();
	log[log_ptr].ctr = ctr();
	log[log_ptr].cr = cr().get();
	log[log_ptr].xer = xer().get();
	log[log_ptr].fpscr = fpscr();
#endif
	log_ptr++;
	if (log_ptr == LOG_SIZE) {
		log_ptr = 0;
		log_ptr_wrapped = true;
	}
#endif
}

#if PPC_FLIGHT_RECORDER
void powerpc_cpu::start_log()
{
	logging = true;
	invalidate_cache();
}

void powerpc_cpu::stop_log()
{
	logging = false;
	invalidate_cache();
}

void powerpc_cpu::dump_log(const char *filename)
{
	if (filename == NULL)
		filename = "ppc.log";

	FILE *f = fopen(filename, "w");
	if (f == NULL)
		return;

	int start_ptr = 0;
	int log_size = log_ptr;
	if (log_ptr_wrapped) {
		start_ptr = log_ptr;
		log_size = LOG_SIZE;
	}

	for (int i = 0; i < log_size; i++) {
		int j = (i + start_ptr) % LOG_SIZE;
#if PPC_FLIGHT_RECORDER >= 2
		fprintf(f, " pc %08x  lr %08x ctr %08x  cr %08x xer %08x ", log[j].pc, log[j].lr, log[j].ctr, log[j].cr, log[j].xer);
		fprintf(f, " r0 %08x  r1 %08x  r2 %08x  r3 %08x ", log[j].r[0], log[j].r[1], log[j].r[2], log[j].r[3]);
		fprintf(f, " r4 %08x  r5 %08x  r6 %08x  r7 %08x ", log[j].r[4], log[j].r[5], log[j].r[6], log[j].r[7]);
		fprintf(f, " r8 %08x  r9 %08x r10 %08x r11 %08x ", log[j].r[8], log[j].r[9], log[j].r[10], log[j].r[11]);
		fprintf(f, "r12 %08x r13 %08x r14 %08x r15 %08x ", log[j].r[12], log[j].r[13], log[j].r[14], log[j].r[15]);
		fprintf(f, "r16 %08x r17 %08x r18 %08x r19 %08x ", log[j].r[16], log[j].r[17], log[j].r[18], log[j].r[19]);
		fprintf(f, "r20 %08x r21 %08x r22 %08x r23 %08x ", log[j].r[20], log[j].r[21], log[j].r[22], log[j].r[23]);
		fprintf(f, "r24 %08x r25 %08x r26 %08x r27 %08x ", log[j].r[24], log[j].r[25], log[j].r[26], log[j].r[27]);
		fprintf(f, "r28 %08x r29 %08x r30 %08x r31 %08x\n", log[j].r[28], log[j].r[29], log[j].r[30], log[j].r[31]);
		fprintf(f, "opcode %08x\n", log[j].opcode);
#else
		fprintf(f, " pc %08x opc %08x", log[j].pc, log[j].opcode);
#ifdef SHEEPSHAVER
		fprintf(f, " sp %08x r24 %08x", log[j].sp, log[j].r24);
#endif
		fprintf(f, "| ");
#if !ENABLE_MON
		fprintf(f, "\n");
#endif
#endif
#if ENABLE_MON
		disass_ppc(f, log[j].pc, log[j].opcode);
#endif
	}
	fclose(f);
}
#endif

#if ENABLE_MON
static uint32 mon_read_byte_ppc(uintptr addr)
{
	return *((uint8 *)addr);
}

static void mon_write_byte_ppc(uintptr addr, uint32 b)
{
	uint8 *m = (uint8 *)addr;
	*m = b;
}
#endif

void powerpc_cpu::initialize()
{
#ifdef SHEEPSHAVER
	printf("PowerPC CPU emulator by Gwenole Beauchesne\n");
#endif

#if PPC_PROFILE_REGS_USE
	reginfo = new register_info[32];
	for (int i = 0; i < 32; i++) {
		reginfo[i].id = i;
		reginfo[i].count = 0;
	}
#endif

	init_flight_recorder();
	init_decoder();
	init_registers();
	init_decode_cache();
	execute_depth = 0;
	srr0_ = srr1_ = dar_ = dsisr_ = 0;
	dec_ = 0xffffffffu;
	tb_offset_ = 0;
	dec_tb_base_ = tb_ticks();
	dec_pending_ = false;
#ifdef SHEEPSHAVER
	last_fetch_pa_ = 0;
#endif
	for (int i = 0; i < 4; i++)
		sprg_[i] = 0;
	for (int i = 0; i < SPR_IMPL_COUNT; i++)
		spr_impl_[i] = 0;

	// Initialize block lookup table
#if PPC_DECODE_CACHE || PPC_ENABLE_JIT
	my_block_cache.initialize();
#endif

	// Init cache range invalidate recorder
	cache_range.start = cache_range.end = 0;

	// Init syscalls handler
	execute_do_syscall = NULL;

	// Init field2mask
	for (int i = 0; i < 256; i++) {
		uint32 mask = 0;
		if (i & 0x01) mask |= 0x0000000f;
		if (i & 0x02) mask |= 0x000000f0;
		if (i & 0x04) mask |= 0x00000f00;
		if (i & 0x08) mask |= 0x0000f000;
		if (i & 0x10) mask |= 0x000f0000;
		if (i & 0x20) mask |= 0x00f00000;
		if (i & 0x40) mask |= 0x0f000000;
		if (i & 0x80) mask |= 0xf0000000;
		field2mask[i] = mask;
	}

#if ENABLE_MON
	mon_init();
	mon_read_byte = mon_read_byte_ppc;
	mon_write_byte = mon_write_byte_ppc;
#endif

#if PPC_PROFILE_COMPILE_TIME
	compile_count = 0;
	compile_time = 0;
	emul_start_time = clock();
#endif
}

#if PPC_ENABLE_JIT
void powerpc_cpu::enable_jit(uint32 cache_size)
{
	use_jit = true;
	if (cache_size)
		codegen.set_cache_size(cache_size);
	codegen.initialize();
}
#endif

void powerpc_cpu::enable_guest_mmu(bool on)
{
	ppc32_guest_mmu_enable(on);
#if PPC_ENABLE_JIT
	if (on)
		use_jit = false;
#endif
#ifdef SHEEPSHAVER
	if (on) {
		nw_jit_set_host_mem(powerpc_cpu::jit_host_lwz, powerpc_cpu::jit_host_stw);
		nw_jit_set_host_pa(powerpc_cpu::jit_host_lwz_pa, powerpc_cpu::jit_host_stw_pa);
		nw_jit_set_host_mfspr(powerpc_cpu::jit_host_mfspr);
		nw_jit_set_host_isync(powerpc_cpu::jit_host_isync);
		nw_jit_set_host_mtmsr(powerpc_cpu::jit_host_mtmsr);
		nw_jit_set_host_mtspr(powerpc_cpu::jit_host_mtspr);
		nw_jit_set_host_lvx(powerpc_cpu::jit_host_lvx);
		nw_jit_set_host_stvx(powerpc_cpu::jit_host_stvx);
		nw_jit_set_host_lfd(powerpc_cpu::jit_host_lfd);
		nw_jit_set_host_half(powerpc_cpu::jit_host_lh, powerpc_cpu::jit_host_sth);
		nw_jit_set_host_byte(powerpc_cpu::jit_host_lb, powerpc_cpu::jit_host_stb);
	}
#endif
}

#if defined(SHEEPSHAVER) && NW_BOOT_LOG
/*
 *  Debug-only PC trace. Records every taken control transfer (pc != prev+4)
 *  in a ring and dumps it once when the NK first enters its debug-print /
 *  panic region (ROM+0x325500..0x325fff). Observation only; names the call
 *  site of a silent spin that raises no exception.
 */
static void nw_trace_pc(powerpc_cpu &cpu, uint32 pc)
{
	extern uint32 ROMBase;
	enum { RING = 96 };
	static uint32 from[RING], to[RING];
	static unsigned head, count;
	static uint32 prev;
	static int dumped;

	if (prev != 0 && pc != prev + 4) {
		from[head] = prev;
		to[head] = pc;
		head = (head + 1) % RING;
		if (count < RING)
			count++;
	}
	prev = pc;
	const uint32 off = pc - ROMBase;
	/* NK panic / debugger entry (saves all GPRs to KDP+0x700). */
	if (off == 0x326420u || off == 0x326428u) {
		static int n;
		if (n < 4) {
			n++;
			char buf[160];
			snprintf(buf, sizeof(buf),
				 "NKPANIC entry=%08x lr=%08x r1=%08x r3=%08x r8=%08x r9=%08x sprg0=%08x msr=%08x",
				 (unsigned)pc, (unsigned)cpu.debug_lr(), (unsigned)cpu.gpr(1),
				 (unsigned)cpu.gpr(3), (unsigned)cpu.gpr(8), (unsigned)cpu.gpr(9),
				 (unsigned)cpu.sprg(0), (unsigned)ppc32_guest_mmu().msr());
			nw_boot_log(buf);
		}
	}
	if (dumped)
		return;
	if (off >= 0x325500u && off < 0x326000u) {
		dumped = 1;
		char buf[96];
		snprintf(buf, sizeof(buf), "PCTRACE enter NK debug region pc=%08x (%u transfers)",
			 (unsigned)pc, count);
		nw_boot_log(buf);
		unsigned i = (head + RING - count) % RING;
		for (unsigned n = 0; n < count; n++, i = (i + 1) % RING) {
			snprintf(buf, sizeof(buf), "PCTRACE %08x -> %08x",
				 (unsigned)from[i], (unsigned)to[i]);
			nw_boot_log(buf);
		}
	}
}
#endif

/*
 *  New World (nanokernel v2) exception model.
 *
 *  Architectural only. An exception saves SRR0/SRR1, clears the MSR
 *  interrupt bits, and continues at the vector (offset 0 or 0xfff00000
 *  by MSR[IP]). The guest owns the vector page: the NK copies its VecTbl
 *  there. Nothing here plants code, redirects to a handler, retries the
 *  faulting access, or resumes the 68k emulator. Every exception and
 *  every 68k A-line dispatch is logged in the golden event grammar
 *  (research-score/golden/nwgolden.c) for newworldview `diff`.
 */

uint32 powerpc_cpu::exception_vector(uint32 vec) const
{
	return (srr1_ & ppc32_mmu::MSR_IP) ? (0xfff00000u | vec) : vec;
}

void powerpc_cpu::take_exception(uint32 vec, uint32 srr0, uint32 srr1_extra, uint32 event_pc)
{
	ppc32_mmu &mmu = ppc32_guest_mmu();
	if (event_pc == 0xffffffffu)
		event_pc = srr0;
	srr0_ = srr0;
	/* OEA: SRR1[0,5-9,16-31] come from the MSR (this keeps MSR[VEC], bit 6,
	   which the NK reads to decide whether the interrupted context owns the
	   vector unit); SRR1[1-4,10-15] carry exception-specific information. */
	srr1_ = (mmu.msr() & ~0x783f0000u) | srr1_extra;
	mmu.set_msr(mmu.msr() & ~ppc32_mmu::MSR_EXC_CLEAR);
	pc() = exception_vector(vec);
#ifdef SHEEPSHAVER
	uint32 extra = 0;
	int extra_valid = 0;
	if (vec == NW_VEC_DSI || vec == NW_VEC_ALIGNMENT) {
		extra = dar_;
		extra_valid = 1;
	} else if (vec == NW_VEC_PROGRAM) {
		extra = srr1_;
		extra_valid = 1;
	}
	nw_event_exception(event_pc, vec, extra, extra_valid);
#endif
}

void powerpc_cpu::take_data_dsi(uint32 ea, bool is_store, uint32 fault)
{
	ppc32_hotints_dsi dsi;
	dsi.take_data_dsi(ppc32_guest_mmu(), pc(), ea, is_store, fault);
	srr0_ = dsi.srr0;
	srr1_ = dsi.srr1;
	dar_ = dsi.dar;
	dsisr_ = dsi.dsisr;
	pc() = dsi.vector;
#ifdef SHEEPSHAVER
#if NW_BOOT_LOG
	{
		ppc32_mmu &mmu = ppc32_guest_mmu();
		const uint32 saved_msr = mmu.msr();
		mmu.set_msr(saved_msr | ppc32_mmu::MSR_DR);
		const ppc32_xlate_result hit =
			mmu.translate(dsi.srr0, PPC32_XLATE_DR, 4);
		mmu.set_msr(saved_msr);
		nw_log_first_dsi(dsi.srr0, dsi.dar, hit.ok ? 1 : 0);
	}
#endif
	nw_event_exception(srr0_, NW_VEC_DSI, dar_, 1);
#endif
}

void powerpc_cpu::take_isi(uint32 fault)
{
	/* SRR1[1] no translation, [3] no-execute/guarded, [4] protection. */
	take_exception(NW_VEC_ISI, pc(), fault ? fault : (uint32)PPC32_FAULT_NOTRANS);
}

void powerpc_cpu::take_sc()
{
	/* SRR0 is the instruction after sc; the boot event names the sc itself
	 * (golden grammar: from = PC of the discontinuity). */
	const uint32 sc_pc = pc();
	take_exception(NW_VEC_SYSCALL, sc_pc + 4, 0, sc_pc);
}

void powerpc_cpu::take_dec()
{
	dec_pending_ = false;
	take_exception(NW_VEC_DECREMENTER, pc(), 0);
}

void powerpc_cpu::take_external()
{
	/* Level-sensitive: the line stays asserted until the handler's IACK. */
	take_exception(NW_VEC_EXTERNAL, pc(), 0);
}

/*
 *  Thermal assist unit (MPC750/7400 THRM1..3). Once THRM3[E] is set, a
 *  THRMn with [V] set completes its comparison: [TIV] = 1 and [TIN] =
 *  junction temperature above the threshold ([TID] = 0) or below it
 *  ([TID] = 1). The temperature is a constant; the point is that the
 *  comparison completes. Mac OS 9's Multiprocessing CPU plugin (Core99)
 *  scans the thresholds with interrupts off and spins on [TIV].
 */
uint32 powerpc_cpu::tau_read(int idx) const
{
	enum { TIN = 0x80000000u, TIV = 0x40000000u, TID = 0x4u, V = 0x1u, THRM3_E = 0x1u,
	       JUNCTION_C = 40 };
	const int thrm3 = spr_impl_index(powerpc_registers::SPR_THRM3);
	uint32 v = spr_impl_[idx];
	if (!(spr_impl_[thrm3] & THRM3_E) || !(v & V))
		return v & ~(TIN | TIV);
	const int threshold = (int)((v >> 23) & 0x7f);
	v &= ~TIN;
	v |= TIV;
	if ((v & TID) ? JUNCTION_C < threshold : JUNCTION_C > threshold)
		v |= TIN;
	return v;
}

void powerpc_cpu::take_program(uint32 srr1_bits)
{
	/* SRR1[46] trap = 0x00020000, [45] privileged = 0x00040000,
	 * [44] illegal = 0x00080000. */
	take_exception(NW_VEC_PROGRAM, pc(), srr1_bits);
}

void powerpc_cpu::execute_trap(uint32 opcode)
{
	/* tw (31/4) and twi (3): TO bits 6..10 test a (rA) against b. */
	const uint32 to = TO_field::extract(opcode);
	const int32 a = (int32)gpr(rA_field::extract(opcode));
	int32 b;
	if (OPCD_field::extract(opcode) == 3)
		b = (int32)(int16)(opcode & 0xffffu);
	else
		b = (int32)gpr(rB_field::extract(opcode));
	const bool trap =
		((to & 0x10) && a < b) ||
		((to & 0x08) && a > b) ||
		((to & 0x04) && a == b) ||
		((to & 0x02) && (uint32)a < (uint32)b) ||
		((to & 0x01) && (uint32)a > (uint32)b);
	if (trap && ppc32_guest_mmu_enabled()) {
		take_program(0x00020000u);
		return;
	}
	increment_pc(4);
}

uint64 powerpc_cpu::tb_host_ticks() const
{
#ifdef SHEEPSHAVER
	extern int64 TimebaseSpeed;
	const uint64 us = GetTicks_usec();
	return (us / 1000000u) * (uint64)TimebaseSpeed + ((us % 1000000u) * (uint64)TimebaseSpeed) / 1000000u;
#else
	return ((uint64)clock() * 25000000u) / CLOCKS_PER_SEC;
#endif
}

uint64 powerpc_cpu::tb_ticks() const
{
	return tb_host_ticks() + (uint64)tb_offset_;
}

/*
 *  Guest-mode SPR model: an MPC7400 as QEMU's `-cpu g4` presents it, so the
 *  NK's CPU-feature probes (mtspr/mfspr read-back, MQ, AltiVec) take the same
 *  path as in the golden trace:
 *    - invalid SPR: user mode -> privileged program exception; supervisor
 *      mode -> no-op (mfspr leaves rD), except SPR 0 (and 4/5/6 for mfspr)
 *      which raise the privileged exception;
 *    - SIAR is read-only (write -> privileged exception), L2CR writes are
 *      ignored, PIR keeps its low 4 bits;
 *    - user-mode read aliases (UMMCRx, UPMCx, USIAR, UBAMR) read spr+0x10.
 */

int powerpc_cpu::spr_impl_index(uint32 spr)
{
	switch (spr) {
	case powerpc_registers::SPR_HID0:	return 0;
	case powerpc_registers::SPR_HID1:	return 1;
	case powerpc_registers::SPR_IABR:	return 2;
	case powerpc_registers::SPR_DABR:	return 3;
	case powerpc_registers::SPR_MSSCR0:	return 4;
	case powerpc_registers::SPR_MSSCR1:	return 5;
	case powerpc_registers::SPR_L2CR:	return 6;
	case powerpc_registers::SPR_ICTC:	return 7;
	case powerpc_registers::SPR_THRM1:	return 8;
	case powerpc_registers::SPR_THRM2:	return 9;
	case powerpc_registers::SPR_THRM3:	return 10;
	case powerpc_registers::SPR_PIR:	return 11;
	case powerpc_registers::SPR_EAR:	return 12;
	case powerpc_registers::SPR_BAMR:	return 13;
	case powerpc_registers::SPR_MMCR0:	return 14;
	case powerpc_registers::SPR_MMCR1:	return 15;
	case powerpc_registers::SPR_MMCR2:	return 16;
	case powerpc_registers::SPR_PMC1:	return 17;
	case powerpc_registers::SPR_PMC2:	return 18;
	case powerpc_registers::SPR_PMC3:	return 19;
	case powerpc_registers::SPR_PMC4:	return 20;
	case powerpc_registers::SPR_SIAR:	return 21;
	default:							return -1;
	}
}

bool powerpc_cpu::spr_user_readable(uint32 spr) const
{
	switch (spr) {
	case powerpc_registers::SPR_XER:
	case powerpc_registers::SPR_LR:
	case powerpc_registers::SPR_CTR:
	case powerpc_registers::SPR_VRSAVE:
	case powerpc_registers::SPR_TBL_R:
	case powerpc_registers::SPR_TBU_R:
	case powerpc_registers::SPR_UMMCR2:
	case powerpc_registers::SPR_UBAMR:
	case powerpc_registers::SPR_UMMCR0:
	case powerpc_registers::SPR_UPMC1:
	case powerpc_registers::SPR_UPMC2:
	case powerpc_registers::SPR_USIAR:
	case powerpc_registers::SPR_UMMCR1:
	case powerpc_registers::SPR_UPMC3:
	case powerpc_registers::SPR_UPMC4:
		return true;
	default:
		return false;
	}
}

powerpc_cpu::spr_access_result powerpc_cpu::mfspr_guest(uint32 spr, uint32 *value)
{
	const bool user = (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_PR) != 0;
	if (user && !spr_user_readable(spr)) {
		take_program(0x00040000u);
		return SPR_ACCESS_EXC;
	}
	switch (spr) {
	case powerpc_registers::SPR_XER:	*value = xer().get();	return SPR_ACCESS_OK;
	case powerpc_registers::SPR_LR:		*value = lr();			return SPR_ACCESS_OK;
	case powerpc_registers::SPR_CTR:	*value = ctr();			return SPR_ACCESS_OK;
	case powerpc_registers::SPR_VRSAVE:	*value = vrsave();		return SPR_ACCESS_OK;
	case powerpc_registers::SPR_TBL_R:	*value = (uint32)tb_ticks();			return SPR_ACCESS_OK;
	case powerpc_registers::SPR_TBU_R:	*value = (uint32)(tb_ticks() >> 32);	return SPR_ACCESS_OK;
	case powerpc_registers::SPR_PVR: {
#ifdef SHEEPSHAVER
		extern uint32 PVR;
		*value = PVR;
#else
		*value = 0x000c0000u;
#endif
		return SPR_ACCESS_OK;
	}
	default:
		break;
	}
	if (mfspr_oea(spr, value))
		return SPR_ACCESS_OK;
	/* User-mode read aliases of the performance-monitor SPRs. */
	if (spr >= powerpc_registers::SPR_UMMCR2 && spr <= powerpc_registers::SPR_UPMC4) {
		const int idx = spr_impl_index(spr + 0x10);
		if (idx >= 0) {
			*value = spr_impl_[idx];
			return SPR_ACCESS_OK;
		}
	}
	const int idx = spr_impl_index(spr);
	if (idx >= 0) {
		if (spr == powerpc_registers::SPR_THRM1 || spr == powerpc_registers::SPR_THRM2)
			*value = tau_read(idx);
		else
			*value = spr_impl_[idx];
		return SPR_ACCESS_OK;
	}
	if (spr == 0 || spr == 4 || spr == 5 || spr == 6) {
		take_program(0x00040000u);
		return SPR_ACCESS_EXC;
	}
	return SPR_ACCESS_NOP;
}

powerpc_cpu::spr_access_result powerpc_cpu::mtspr_guest(uint32 spr, uint32 value)
{
	const bool user = (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_PR) != 0;
	switch (spr) {
	case powerpc_registers::SPR_XER:	xer().set(value);	return SPR_ACCESS_OK;
	case powerpc_registers::SPR_LR:		lr() = value;		return SPR_ACCESS_OK;
	case powerpc_registers::SPR_CTR:	ctr() = value;		return SPR_ACCESS_OK;
	case powerpc_registers::SPR_VRSAVE:	vrsave() = value;	return SPR_ACCESS_OK;
	default:
		break;
	}
	if (user) {
		take_program(0x00040000u);
		return SPR_ACCESS_EXC;
	}
	if (mtspr_oea(spr, value))
		return SPR_ACCESS_OK;
	switch (spr) {
	case powerpc_registers::SPR_TBL_W: {
		const uint64 now = tb_ticks();
		const uint64 want = (now & 0xffffffff00000000ull) | value;
		tb_offset_ += (int64)(want - now);
		return SPR_ACCESS_OK;
	}
	case powerpc_registers::SPR_TBU_W: {
		const uint64 now = tb_ticks();
		const uint64 want = ((uint64)value << 32) | (now & 0xffffffffu);
		tb_offset_ += (int64)(want - now);
		return SPR_ACCESS_OK;
	}
	case powerpc_registers::SPR_SIAR:		/* read-only */
	case powerpc_registers::SPR_PVR:
	case powerpc_registers::SPR_UMMCR2:
	case powerpc_registers::SPR_UBAMR:
	case powerpc_registers::SPR_UMMCR0:
	case powerpc_registers::SPR_UPMC1:
	case powerpc_registers::SPR_UPMC2:
	case powerpc_registers::SPR_USIAR:
	case powerpc_registers::SPR_UMMCR1:
	case powerpc_registers::SPR_UPMC3:
	case powerpc_registers::SPR_UPMC4:
		take_program(0x00040000u);
		return SPR_ACCESS_EXC;
	case powerpc_registers::SPR_L2CR:		/* write ignored */
		return SPR_ACCESS_OK;
	case powerpc_registers::SPR_PIR:
		spr_impl_[spr_impl_index(spr)] = value & 0xfu;
		return SPR_ACCESS_OK;
	default:
		break;
	}
	const int idx = spr_impl_index(spr);
	if (idx >= 0) {
		spr_impl_[idx] = value;
		return SPR_ACCESS_OK;
	}
	if (spr == 0) {
		take_program(0x00040000u);
		return SPR_ACCESS_EXC;
	}
	return SPR_ACCESS_NOP;
}

bool powerpc_cpu::is_altivec_insn(uint32 opcode)
{
	const uint32 opcd = opcode >> 26;
	if (opcd == 4)
		return true;
	if (opcd != 31)
		return false;
	switch ((opcode >> 1) & 0x3ffu) {
	case 6:		/* lvsl */
	case 38:	/* lvsr */
	case 7:		/* lvebx */
	case 39:	/* lvehx */
	case 71:	/* lvewx */
	case 103:	/* lvx */
	case 359:	/* lvxl */
	case 135:	/* stvebx */
	case 167:	/* stvehx */
	case 199:	/* stvewx */
	case 231:	/* stvx */
	case 487:	/* stvxl */
		return true;
	default:
		return false;
	}
}

void powerpc_cpu::take_vpu()
{
	take_exception(NW_VEC_VPU, pc(), 0);
}

/*
 *  Floating-point instructions: primary opcodes 48..55 (lfs/lfd/stfs/stfd
 *  and their update/indexed forms live at 48-55), 59 (single), 63 (double
 *  and FPSCR), plus the indexed FP loads/stores in opcode 31.
 */
bool powerpc_cpu::is_fp_insn(uint32 opcode)
{
	const uint32 opcd = opcode >> 26;
	if ((opcd >= 48 && opcd <= 55) || opcd == 59 || opcd == 63)
		return true;
	if (opcd != 31)
		return false;
	switch ((opcode >> 1) & 0x3ff) {
	case 535: case 567: case 599: case 631:	/* lfsx lfsux lfdx lfdux */
	case 663: case 695: case 727: case 759:	/* stfsx stfsux stfdx stfdux */
	case 983:				/* stfiwx */
		return true;
	}
	return false;
}

void powerpc_cpu::take_fpu()
{
	take_exception(NW_VEC_FPU, pc(), 0);
}

void powerpc_cpu::tick_decrementer()
{
	/* DEC decrements at the timebase rate (mftb and DEC share a clock).
	 * Sampled every 256 interpreted instructions; a 0->1 transition of the
	 * MSB raises the decrementer interrupt, once, as on hardware. The NK
	 * programs DEC from TB deltas, so a DEC that ran at instruction rate
	 * made every deadline look expired (DEC storm after the first fire). */
	static unsigned div;
	if (++div < 256u)
		return;
	div = 0;
	const uint64 now = tb_ticks();
	const uint32 elapsed = (uint32)(now - dec_tb_base_);
	dec_tb_base_ = now;
	const uint32 old_dec = dec_;
	dec_ = old_dec - elapsed;
	if ((old_dec & 0x80000000u) == 0 && ((dec_ & 0x80000000u) || elapsed > old_dec))
		dec_pending_ = true;
	/* Edge 0→1 is the OEA; after take_dec, pending is clear. JIT ON can
	 * leave DEC negative without a following mtdec (g6-js: 1×0x900 vs
	 * 8678×0x500 at 503191d8, A-traps stall, Happy Mac frozen). Keep the
	 * exception asserted while the MSB is 1 so nap still wakes. */
	if (dec_ & 0x80000000u)
		dec_pending_ = true;
#ifdef SHEEPSHAVER
	nw_devices_tick();
	nw_host_tick();
#if NW_BOOT_LOG
	nw_script_tick();
#endif
	nw_event_tick(pc(), ppc32_guest_mmu().msr());
#endif
}

bool powerpc_cpu::async_exception_pending() const
{
#ifdef SHEEPSHAVER
	if (nw_io_ext_irq)
		return true;
#endif
	return dec_pending_;
}

void powerpc_cpu::take_async_exception()
{
	/* External interrupt has priority over the decrementer (OEA 6.4.1). */
#ifdef SHEEPSHAVER
	if (nw_io_ext_irq) {
		take_external();
		return;
	}
#endif
	take_dec();
}

bool powerpc_cpu::guest_fetch(uint32 *opcode)
{
	if (!ppc32_guest_mmu_enabled()) {
#ifdef SHEEPSHAVER
		last_fetch_pa_ = pc();
#endif
		*opcode = vm_read_memory_4(pc());
		return true;
	}
	ppc32_xlate_result r = ppc32_guest_mmu().translate(pc(), PPC32_XLATE_IR, 4);
	if (!r.ok) {
		take_isi(r.fault);
		return false;
	}
#ifdef SHEEPSHAVER
	{
		last_fetch_pa_ = r.pa;
		/* Fetch outside guest RAM/ROM (and SheepShaver's thunk area, which
		 * the guest is handed by identity mapping) would read host memory.
		 * Report it as a machine check so the log names the PA; do not
		 * guess. */
		extern uint32 ROMBase, RAMBase, RAMSize;
		const uint32 pa = r.pa;
		const bool in_ram = pa >= RAMBase && pa < RAMBase + RAMSize;
		const bool in_rom = pa >= ROMBase && pa < ROMBase + 0x500000u;
		const bool in_low = pa < 0x4000u;
		const bool in_thunks = pa - nw_thunk_area_base < nw_thunk_area_size;
		if (!(in_ram || in_rom || in_low || in_thunks)) {
			dar_ = pa;
			take_exception(NW_VEC_MACHINE_CHECK, pc(), 0);
			return false;
		}
		/* 68k A-line dispatch inside the ROM's own emulator. Observation
		 * only: op = (r29 >> 3) & 0xffff, A-line word at r24 - 2. */
		if (pa >= ROMBase + NW_EMU_ALINE_OS_FLAG &&
		    pa <= ROMBase + NW_EMU_ALINE_TOOL_AUTOPOP) {
			const int h = nw_emu_aline_handler(pa - ROMBase);
			if (h >= 0 && vm_read_memory_4(pa) == NW_EMU_ALINE_ENTRY_OP)
				nw_event_aline((gpr(29) >> 3) & 0xffffu, gpr(24) - 2u, h);
		}
	}
#endif
	*opcode = vm_read_memory_4(r.pa);
	return true;
}

bool powerpc_cpu::guest_data_probe(uint32 ea, unsigned width, bool is_store, uint32 *pa)
{
	if (!ppc32_guest_mmu_enabled()) {
		*pa = ea;
		return true;
	}
	ppc32_xlate_result r = ppc32_guest_mmu().translate(ea, PPC32_XLATE_DR, width, is_store);
	if (r.ok) {
		*pa = r.pa;
		return true;
	}
	return false;
}

bool powerpc_cpu::guest_data_xlate(uint32 ea, unsigned width, bool is_store, uint32 *pa)
{
	if (!ppc32_guest_mmu_enabled()) {
		*pa = ea;
		return true;
	}
	ppc32_xlate_result r = ppc32_guest_mmu().translate(ea, PPC32_XLATE_DR, width, is_store);
	if (r.ok) {
		*pa = r.pa;
		return true;
	}
	take_data_dsi(ea, is_store, r.fault);
	return false;
}

bool powerpc_cpu::mfspr_oea(uint32 spr, uint32 *value) const
{
	ppc32_mmu &mmu = ppc32_guest_mmu();
	switch (spr) {
	case powerpc_registers::SPR_DSISR:	*value = dsisr_; return true;
	case powerpc_registers::SPR_DAR:	*value = dar_; return true;
	case powerpc_registers::SPR_SDR1:	*value = mmu.sdr1(); return true;
	case powerpc_registers::SPR_DEC:	*value = dec_; return true;
	case powerpc_registers::SPR_SRR0:	*value = srr0_; return true;
	case powerpc_registers::SPR_SRR1:	*value = srr1_; return true;
	case powerpc_registers::SPR_SPRG0:	*value = sprg_[0]; return true;
	case powerpc_registers::SPR_SPRG1:	*value = sprg_[1]; return true;
	case powerpc_registers::SPR_SPRG2:	*value = sprg_[2]; return true;
	case powerpc_registers::SPR_SPRG3:	*value = sprg_[3]; return true;
	default:
		break;
	}
	if (spr >= powerpc_registers::SPR_IBAT0U && spr <= powerpc_registers::SPR_IBAT3L) {
		unsigned i = (spr - powerpc_registers::SPR_IBAT0U) / 2;
		uint32 u = 0, l = 0;
		mmu.get_ibat(i, &u, &l);
		*value = (spr & 1) ? l : u;
		return true;
	}
	if (spr >= powerpc_registers::SPR_DBAT0U && spr <= powerpc_registers::SPR_DBAT3L) {
		unsigned i = (spr - powerpc_registers::SPR_DBAT0U) / 2;
		uint32 u = 0, l = 0;
		mmu.get_dbat(i, &u, &l);
		*value = (spr & 1) ? l : u;
		return true;
	}
	return false;
}

bool powerpc_cpu::mtspr_oea(uint32 spr, uint32 value)
{
	ppc32_mmu &mmu = ppc32_guest_mmu();
	switch (spr) {
	case powerpc_registers::SPR_DSISR:	dsisr_ = value; return true;
	case powerpc_registers::SPR_DAR:	dar_ = value; return true;
	case powerpc_registers::SPR_SDR1:
#ifdef SHEEPSHAVER
		nw_note_mtsdr1();
		/* Cache key is phys_page; a new HTAB misses, no flush-all. */
#endif
		mmu.set_sdr1(value);
		nw_jit_dtlb_flush();
		return true;
	case powerpc_registers::SPR_SRR0:	srr0_ = value; return true;
	case powerpc_registers::SPR_SRR1:	srr1_ = value; return true;
	case powerpc_registers::SPR_DEC:
		/* Writing DEC with the MSB going 0->1 also raises the interrupt. */
		if ((dec_ & 0x80000000u) == 0 && (value & 0x80000000u))
			dec_pending_ = true;
		dec_ = value;
		dec_tb_base_ = tb_ticks();
		return true;
	case powerpc_registers::SPR_SPRG0:	sprg_[0] = value; return true;
	case powerpc_registers::SPR_SPRG1:	sprg_[1] = value; return true;
	case powerpc_registers::SPR_SPRG2:	sprg_[2] = value; return true;
	case powerpc_registers::SPR_SPRG3:	sprg_[3] = value; return true;
	default:
		break;
	}
	if (spr >= powerpc_registers::SPR_IBAT0U && spr <= powerpc_registers::SPR_IBAT3L) {
		unsigned i = (spr - powerpc_registers::SPR_IBAT0U) / 2;
		uint32 u = 0, l = 0;
		mmu.get_ibat(i, &u, &l);
		if (spr & 1)
			l = value;
		else
			u = value;
		mmu.set_ibat(i, u, l);
		nw_jit_dtlb_flush();
		return true;
	}
	if (spr >= powerpc_registers::SPR_DBAT0U && spr <= powerpc_registers::SPR_DBAT3L) {
		unsigned i = (spr - powerpc_registers::SPR_DBAT0U) / 2;
		uint32 u = 0, l = 0;
		mmu.get_dbat(i, &u, &l);
		if (spr & 1)
			l = value;
		else
			u = value;
		mmu.set_dbat(i, u, l);
		nw_jit_dtlb_flush();
		return true;
	}
	return false;
}

// Memory allocator returning powerpc_cpu objects aligned on 16-byte boundaries
// FORMAT: [ alignment ] magic identifier, offset to malloc'ed data, powerpc_cpu data
void *powerpc_cpu::operator new(size_t size)
{
	const int ALIGN = 16;

	// Allocate enough space for powerpc_cpu data + signature + align pad
	uint8 *ptr = (uint8 *)malloc(size + ALIGN * 2);
	if (ptr == NULL)
		throw std::bad_alloc();

	// Align memory
	int ofs = 0;
	while ((((uintptr)ptr) % ALIGN) != 0)
		ofs++, ptr++;

	// Insert signature and offset
	struct aligned_block_t {
		uint32 pad[(ALIGN - 8) / 4];
		uint32 signature;
		uint32 offset;
		uint8  data[sizeof(powerpc_cpu)];
	};
	aligned_block_t *blk = (aligned_block_t *)ptr;
	blk->signature = 0x53435055;		/* 'SCPU' */
	blk->offset = ofs + (&blk->data[0] - (uint8 *)blk);
	assert((((uintptr)&blk->data) % ALIGN) == 0);
	return &blk->data[0];
}

void powerpc_cpu::operator delete(void *p)
{
	uint32 *blk = (uint32 *)p;
	assert(blk[-2] == 0x53435055);		/* 'SCPU' */
	void *ptr = (void *)(((uintptr)p) - blk[-1]);
	free(ptr);
}

#ifdef SHEEPSHAVER
powerpc_cpu::powerpc_cpu()
#if PPC_ENABLE_JIT
	: codegen(this)
#endif
#else
powerpc_cpu::powerpc_cpu(task_struct *parent_task)
	: basic_cpu(parent_task)
#if PPC_ENABLE_JIT
	, codegen(this)
#endif
#endif
{
#if PPC_ENABLE_JIT
	use_jit = false;
#endif
	spcflags().init();
	++ppc_refcount;
	initialize();
}

powerpc_cpu::~powerpc_cpu()
{
	--ppc_refcount;
#if PPC_PROFILE_COMPILE_TIME
	clock_t emul_end_time = clock();

	const char *type = NULL;
#if PPC_ENABLE_JIT
	if (use_jit)
		type = "compile";
#endif
#if PPC_DECODE_CACHE
	if (!type)
		type = "predecode";
#endif
	if (type) {
		printf("### Statistics for block %s\n", type);
		printf("Total block %s count : %d\n", type, compile_count);
		uint32 emul_time = emul_end_time - emul_start_time;
		printf("Total emulation time : %.1f sec\n",
			   double(emul_time) / double(CLOCKS_PER_SEC));
		printf("Total %s time : %.1f sec (%.1f%%)\n", type,
			   double(compile_time) / double(CLOCKS_PER_SEC),
			   100.0 * double(compile_time) / double(emul_time));
		printf("\n");
	}
#endif

#if PPC_PROFILE_GENERIC_CALLS
	if (use_jit && ppc_refcount == 0) {
		uint64 total_generic_calls_count = 0;
		for (int i = 0; i < PPC_I(MAX); i++) {
			generic_calls_ids[i] = i;
			total_generic_calls_count += generic_calls_count[i];
		}
		qsort(generic_calls_ids, PPC_I(MAX), sizeof(int), generic_calls_compare);
		printf("Rank      Count Ratio Name\n");
		for (int i = 0; i < generic_calls_top_ten; i++) {
			uint32 mnemo = generic_calls_ids[i];
			uint32 count = generic_calls_count[mnemo];
			const instr_info_t *ii = powerpc_ii_table;
			while (ii->mnemo != mnemo)
				ii++;
			printf("%03d: %10lu %2.1f%% %s\n", i, count, 100.0*double(count)/double(total_generic_calls_count), ii->name);
		}
	}
#endif

#if PPC_PROFILE_REGS_USE
	printf("\n### Statistics for register usage\n");
	uint64 tot_reg_count = 0;
	for (int i = 0; i < 32; i++)
		tot_reg_count += reginfo[i].count;
	qsort(reginfo, 32, sizeof(register_info), register_info_compare);
	uint64 cum_reg_count = 0;
	for (int i = 0; i < 32; i++) {
		cum_reg_count += reginfo[i].count;
	    printf("r%-2d : %16llu %2.1f%% [%3.1f%%]\n",
			   reginfo[i].id, reginfo[i].count,
			   100.0*double(reginfo[i].count)/double(tot_reg_count),
			   100.0*double(cum_reg_count)/double(tot_reg_count));
	}
	delete[] reginfo;
#endif

	kill_decode_cache();

#if ENABLE_MON
	mon_exit();
#endif
}

void powerpc_cpu::dump_registers()
{
	fprintf(stderr, " r0 %08x   r1 %08x   r2 %08x   r3 %08x\n", gpr(0), gpr(1), gpr(2), gpr(3));
	fprintf(stderr, " r4 %08x   r5 %08x   r6 %08x   r7 %08x\n", gpr(4), gpr(5), gpr(6), gpr(7));
	fprintf(stderr, " r8 %08x   r9 %08x  r10 %08x  r11 %08x\n", gpr(8), gpr(9), gpr(10), gpr(11));
	fprintf(stderr, "r12 %08x  r13 %08x  r14 %08x  r15 %08x\n", gpr(12), gpr(13), gpr(14), gpr(15));
	fprintf(stderr, "r16 %08x  r17 %08x  r18 %08x  r19 %08x\n", gpr(16), gpr(17), gpr(18), gpr(19));
	fprintf(stderr, "r20 %08x  r21 %08x  r22 %08x  r23 %08x\n", gpr(20), gpr(21), gpr(22), gpr(23));
	fprintf(stderr, "r24 %08x  r25 %08x  r26 %08x  r27 %08x\n", gpr(24), gpr(25), gpr(26), gpr(27));
	fprintf(stderr, "r28 %08x  r29 %08x  r30 %08x  r31 %08x\n", gpr(28), gpr(29), gpr(30), gpr(31));
	fprintf(stderr, " f0 %02.5f   f1 %02.5f   f2 %02.5f   f3 %02.5f\n", fpr(0), fpr(1), fpr(2), fpr(3));
	fprintf(stderr, " f4 %02.5f   f5 %02.5f   f6 %02.5f   f7 %02.5f\n", fpr(4), fpr(5), fpr(6), fpr(7));
	fprintf(stderr, " f8 %02.5f   f9 %02.5f  f10 %02.5f  f11 %02.5f\n", fpr(8), fpr(9), fpr(10), fpr(11));
	fprintf(stderr, "f12 %02.5f  f13 %02.5f  f14 %02.5f  f15 %02.5f\n", fpr(12), fpr(13), fpr(14), fpr(15));
	fprintf(stderr, "f16 %02.5f  f17 %02.5f  f18 %02.5f  f19 %02.5f\n", fpr(16), fpr(17), fpr(18), fpr(19));
	fprintf(stderr, "f20 %02.5f  f21 %02.5f  f22 %02.5f  f23 %02.5f\n", fpr(20), fpr(21), fpr(22), fpr(23));
	fprintf(stderr, "f24 %02.5f  f25 %02.5f  f26 %02.5f  f27 %02.5f\n", fpr(24), fpr(25), fpr(26), fpr(27));
	fprintf(stderr, "f28 %02.5f  f29 %02.5f  f30 %02.5f  f31 %02.5f\n", fpr(28), fpr(29), fpr(30), fpr(31));
	fprintf(stderr, " lr %08x  ctr %08x   cr %08x  xer %08x\n", lr(), ctr(), cr().get(), xer().get());
	fprintf(stderr, " pc %08x fpscr %08x\n", pc(), fpscr());
	fflush(stderr);
}

void powerpc_cpu::dump_instruction(uint32 opcode)
{
	fprintf(stderr, "[%08x]-> %08x\n", pc(), opcode);
}

void powerpc_cpu::fake_dump_registers(uint32)
{
	dump_registers();
}

void powerpc_registers::interrupt_copy(powerpc_registers &oregs, powerpc_registers const &iregs)
{
	for (int i = 0; i < 32; i++) {
		oregs.gpr[i] = iregs.gpr[i];
		oregs.fpr[i] = iregs.fpr[i];
	}
	oregs.cr	= iregs.cr;
	oregs.fpscr	= iregs.fpscr;
	oregs.xer	= iregs.xer;
	oregs.lr	= iregs.lr;
	oregs.ctr	= iregs.ctr;
	oregs.pc	= iregs.pc;

	uint32 vrsave = iregs.vrsave;
	oregs.vrsave  = vrsave;
	if (vrsave) {
		for (int i = 31; i >= 0; i--) {
			if (vrsave & 1)
				oregs.vr[i] = iregs.vr[i];
			vrsave >>= 1;
		}
	}
}

bool powerpc_cpu::check_spcflags()
{
	if (spcflags().test(SPCFLAG_CPU_EXEC_RETURN)) {
		spcflags().clear(SPCFLAG_CPU_EXEC_RETURN);
		return false;
	}
#ifdef SHEEPSHAVER
	if (spcflags().test(SPCFLAG_CPU_HANDLE_INTERRUPT)) {
		spcflags().clear(SPCFLAG_CPU_HANDLE_INTERRUPT);
		static bool processing_interrupt = false;
		if (!processing_interrupt) {
			processing_interrupt = true;
			powerpc_registers r;
			powerpc_registers::interrupt_copy(r, regs());
			HandleInterrupt(&r);
			powerpc_registers::interrupt_copy(regs(), r);
			processing_interrupt = false;
		}
	}
	if (spcflags().test(SPCFLAG_CPU_TRIGGER_INTERRUPT)) {
		spcflags().clear(SPCFLAG_CPU_TRIGGER_INTERRUPT);
		spcflags().set(SPCFLAG_CPU_HANDLE_INTERRUPT);
	}
#endif
	if (spcflags().test(SPCFLAG_CPU_ENTER_MON)) {
		spcflags().clear(SPCFLAG_CPU_ENTER_MON);
#if ENABLE_MON
		// Start up mon in real-mode
		const char *arg[] = {
			"mon",
#ifdef SHEEPSHAVER
			"-m",
#endif
			"-r",
			NULL
		};
		mon(sizeof(arg)/sizeof(arg[0]) - 1, arg);
#endif
	}
	return true;
}

#if DYNGEN_DIRECT_BLOCK_CHAINING
void * powerpc_cpu::call_compile_chain_block(powerpc_cpu * the_cpu, block_info *sbi)
{
	return the_cpu->compile_chain_block(sbi);
}

void * PF_CONVENTION powerpc_cpu::compile_chain_block(block_info *sbi)
{
	// Block index is stuffed into the source basic block pointer,
	// which is aligned at least on 4-byte boundaries
	const int n = ((uintptr)sbi) & 3;
	sbi = (block_info *)(((uintptr)sbi) & ~3L);

	const uint32 tpc = sbi->li[n].jmp_pc;
	block_info *tbi = my_block_cache.find(tpc);
	if (tbi == NULL)
		tbi = compile_block(tpc);
	assert(tbi && tbi->pc == tpc);

	dg_set_jmp_target(sbi->li[n].jmp_addr, tbi->entry_point);
	return tbi->entry_point;
}
#endif

#ifdef SHEEPSHAVER
uint32 powerpc_cpu::jit_host_lwz(void *host, uint32 ea, uint32 pc, int *fault)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	uint32 pa;
	/* Copy-out is gated. Shadow runs must not take a DSI, double-hit I/O,
	 * or vm_write an unmapped PA (no SIGSEGV recovery from JIT code). */
	if (!ppc->guest_data_probe(ea, 4, false, &pa)) {
		*fault = 1;
		return 0;
	}
	const int kind = nw_pa_kind(pa);
	if (kind == NW_PA_IO) {
		*fault = 2;
		return 0;
	}
	if (kind == NW_PA_NONE) {
		*fault = 1;
		return 0;
	}
	if (ppc32_guest_mmu_enabled() &&
	    (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_DR))
		nw_jit_dtlb_fill(ea, pa, nw_pa_writable(pa) && kind != NW_PA_ROM,
			(uint64_t)(uintptr_t)vm_do_get_real_address(pa & ~0xfffu));
	return vm_read_memory_4(pa);
}

void powerpc_cpu::jit_host_stw(void *host, uint32 ea, uint32 val, uint32 pc, int *fault)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	uint32 pa;
	(void)pc;
	if (!ppc->guest_data_probe(ea, 4, true, &pa)) {
		*fault = 1;
		return;
	}
	const int kind = nw_pa_kind(pa);
	if (kind == NW_PA_IO) {
		*fault = 2;
		return;
	}
	if (kind == NW_PA_ROM)
		return;
	if (!nw_pa_writable(pa)) {
		*fault = 1;
		return;
	}
	vm_write_memory_4(pa, val);
	if (ppc32_guest_mmu_enabled() &&
	    (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_DR))
		nw_jit_dtlb_fill(ea, pa, 1,
			(uint64_t)(uintptr_t)vm_do_get_real_address(pa & ~0xfffu));
	nw_jit_invalidate_page_src(pa, NW_JIT_FL_STORE);
	if ((pa & ~0xfffu) == (ppc->last_fetch_pa_ & ~0xfffu))
		*fault = NW_JIT_FAULT_SMC;
}

uint32 powerpc_cpu::jit_host_lwz_pa(void *host, uint32 pa, uint32 pc, int *fault)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	(void)pc;
	(void)ppc;
	const int kind = nw_pa_kind(pa);
	if (kind == NW_PA_IO) {
		*fault = 2;
		return 0;
	}
	if (kind == NW_PA_NONE) {
		*fault = 1;
		return 0;
	}
	return vm_read_memory_4(pa);
}

void powerpc_cpu::jit_host_stw_pa(void *host, uint32 pa, uint32 val, uint32 pc, int *fault)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	(void)pc;
	const int kind = nw_pa_kind(pa);
	if (kind == NW_PA_IO) {
		*fault = 2;
		return;
	}
	if (kind == NW_PA_ROM)
		return;
	if (!nw_pa_writable(pa)) {
		*fault = 1;
		return;
	}
	vm_write_memory_4(pa, val);
	nw_jit_invalidate_page_src(pa, NW_JIT_FL_STORE);
	if ((pa & ~0xfffu) == (ppc->last_fetch_pa_ & ~0xfffu))
		*fault = NW_JIT_FAULT_SMC;
}

uint32 powerpc_cpu::jit_host_mfspr(void *host, uint32 spr, uint32 guest_pc, int *status)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	if (status)
		*status = 0;
	switch (spr) {
	case powerpc_registers::SPR_TBL_R:
		return (uint32)ppc->tb_ticks();
	case powerpc_registers::SPR_TBU_R:
		return (uint32)(ppc->tb_ticks() >> 32);
	case powerpc_registers::SPR_PVR: {
		extern uint32 PVR;
		return PVR;
	}
	case powerpc_registers::SPR_VRSAVE:
		return ppc->vrsave();
	case powerpc_registers::SPR_SPRG0:
	case powerpc_registers::SPR_SPRG1:
	case powerpc_registers::SPR_SPRG2:
	case powerpc_registers::SPR_SPRG3:
		return ppc->sprg(spr - powerpc_registers::SPR_SPRG0);
	default: {
		ppc->pc() = guest_pc;
		uint32 d = 0;
		const spr_access_result r = ppc->mfspr_guest(spr, &d);
		if (r == SPR_ACCESS_EXC) {
			if (status)
				*status = 2;
			return ppc->pc();
		}
		if (r == SPR_ACCESS_NOP) {
			if (status)
				*status = 1;
			return 0;
		}
		return d;
	}
	}
}

void powerpc_cpu::jit_host_isync(void *host)
{
	/* Same as execute_isync without the PC bump: apply the pending
	 * icbi range (kpx decode cache + NW JIT pages). */
	((powerpc_cpu *)host)->execute_invalidate_cache_range();
}

void powerpc_cpu::jit_host_mtmsr(void *host, uint32 msr)
{
	/* Same as execute_mtmsr without the PC bump. */
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	if (ppc32_guest_mmu_enabled()) {
		ppc32_guest_mmu().set_msr(msr);
#ifdef SHEEPSHAVER
		nw_log_msr_dr(msr);
		nw_log_msr_write("mtmsr", ppc->pc(), msr);
#endif
	}
	nw_jit_dtlb_flush();
	(void)ppc;
}

void powerpc_cpu::jit_host_mtspr(void *host, uint32 spr, uint32 val)
{
	/* Same as execute_mtspr guest path without the PC bump. */
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	if (ppc32_guest_mmu_enabled())
		ppc->mtspr_guest(spr, val);
	(void)host;
}

void powerpc_cpu::jit_host_lvx(void *host, uint32 vd, uint32 ea, uint32 pc, int *fault, uint32 *out)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	(void)pc;
	ea &= ~15u;
	uint32 pa;
	if (!ppc->guest_data_probe(ea, 16, false, &pa)) {
		*fault = 1;
		return;
	}
	if (nw_pa_kind(pa) == NW_PA_NONE || nw_pa_kind(pa) == NW_PA_IO) {
		*fault = (nw_pa_kind(pa) == NW_PA_IO) ? 2 : 1;
		return;
	}
	powerpc_vr &v = ppc->vr((int)vd);
	v.w[0] = vm_read_memory_4(pa +  0);
	v.w[1] = vm_read_memory_4(pa +  4);
	v.w[2] = vm_read_memory_4(pa +  8);
	v.w[3] = vm_read_memory_4(pa + 12);
	if (out) {
		out[0] = v.w[0];
		out[1] = v.w[1];
		out[2] = v.w[2];
		out[3] = v.w[3];
	}
}

void powerpc_cpu::jit_host_stvx(void *host, uint32 ea, const uint32 *w, uint32 pc, int *fault)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	(void)pc;
	ea &= ~15u;
	uint32 pa;
	if (!ppc->guest_data_probe(ea, 16, true, &pa)) {
		*fault = 1;
		return;
	}
	const int kind = nw_pa_kind(pa);
	if (kind == NW_PA_IO) {
		*fault = 2;
		return;
	}
	if (kind == NW_PA_ROM)
		return;
	if (kind == NW_PA_NONE || !nw_pa_writable(pa)) {
		*fault = 1;
		return;
	}
	jit_host_stw(host, ea +  0, w[0], pc, fault);
	if (*fault)
		return;
	jit_host_stw(host, ea +  4, w[1], pc, fault);
	if (*fault)
		return;
	jit_host_stw(host, ea +  8, w[2], pc, fault);
	if (*fault)
		return;
	jit_host_stw(host, ea + 12, w[3], pc, fault);
}

void powerpc_cpu::jit_host_lfd(void *host, uint32 fd, uint32 ea, uint32 pc, int *fault, uint64 *out)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	(void)pc;
	uint32 pa;
	if (!ppc->guest_data_probe(ea, 8, false, &pa)) {
		*fault = 1;
		return;
	}
	const int kind = nw_pa_kind(pa);
	if (kind == NW_PA_IO) {
		*fault = 2;
		return;
	}
	if (kind == NW_PA_NONE) {
		*fault = 1;
		return;
	}
	if (ppc32_guest_mmu_enabled() &&
	    (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_DR))
		nw_jit_dtlb_fill(ea, pa, nw_pa_writable(pa) && kind != NW_PA_ROM,
			(uint64_t)(uintptr_t)vm_do_get_real_address(pa & ~0xfffu));
	const uint64 v = vm_read_memory_8(pa);
	ppc->fpr_dw((int)fd) = v;
	if (out)
		*out = v;
}

uint32 powerpc_cpu::jit_host_lh(void *host, uint32 ea, uint32 pc, int *fault)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	uint32 pa;
	(void)pc;
	if (!ppc->guest_data_probe(ea, 2, false, &pa)) {
		*fault = 1;
		return 0;
	}
	const int kind = nw_pa_kind(pa);
	if (kind == NW_PA_IO) {
		*fault = 2;
		return 0;
	}
	if (kind == NW_PA_NONE) {
		*fault = 1;
		return 0;
	}
	return vm_read_memory_2(pa);
}

void powerpc_cpu::jit_host_sth(void *host, uint32 ea, uint32 val, uint32 pc, int *fault)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	uint32 pa;
	(void)pc;
	if (!ppc->guest_data_probe(ea, 2, true, &pa)) {
		*fault = 1;
		return;
	}
	const int kind = nw_pa_kind(pa);
	if (kind == NW_PA_IO) {
		*fault = 2;
		return;
	}
	if (kind == NW_PA_ROM)
		return;
	if (!nw_pa_writable(pa)) {
		*fault = 1;
		return;
	}
	vm_write_memory_2(pa, val);
	nw_jit_invalidate_page_src(pa, NW_JIT_FL_STORE);
	if ((pa & ~0xfffu) == (ppc->last_fetch_pa_ & ~0xfffu))
		*fault = NW_JIT_FAULT_SMC;
}

uint32 powerpc_cpu::jit_host_lb(void *host, uint32 ea, uint32 pc, int *fault)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	uint32 pa;
	(void)pc;
	if (!ppc->guest_data_probe(ea, 1, false, &pa)) {
		*fault = 1;
		return 0;
	}
	const int kind = nw_pa_kind(pa);
	if (kind == NW_PA_IO) {
		*fault = 2;
		return 0;
	}
	if (kind == NW_PA_NONE) {
		*fault = 1;
		return 0;
	}
	return vm_read_memory_1(pa);
}

void powerpc_cpu::jit_host_stb(void *host, uint32 ea, uint32 val, uint32 pc, int *fault)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	uint32 pa;
	(void)pc;
	if (!ppc->guest_data_probe(ea, 1, true, &pa)) {
		*fault = 1;
		return;
	}
	const int kind = nw_pa_kind(pa);
	if (kind == NW_PA_IO) {
		*fault = 2;
		return;
	}
	if (kind == NW_PA_ROM)
		return;
	if (!nw_pa_writable(pa)) {
		*fault = 1;
		return;
	}
	vm_write_memory_1(pa, val);
	nw_jit_invalidate_page_src(pa, NW_JIT_FL_STORE);
	if ((pa & ~0xfffu) == (ppc->last_fetch_pa_ & ~0xfffu))
		*fault = NW_JIT_FAULT_SMC;
}

static int nw_jit_pa_ok(uint32 pa, int is_st)
{
	const int kind = nw_pa_kind(pa);
	if (kind == NW_PA_IO || kind == NW_PA_NONE)
		return 0;
	if (is_st && !nw_pa_writable(pa))
		return 0;
	return 1;
}

static int nw_jit_op_mem_ok(powerpc_cpu *ppc, uint32 op, const uint32 *sg)
{
	const int prim = (int)(op >> 26);
	int width = 4, is_st = 0;
	if (prim == 32)
		;
	else if (prim == 36 || prim == 37)
		is_st = 1;
	else if (prim == 31 && ((op >> 1) & 0x3ff) == 23) {
		const int ra = (int)((op >> 16) & 0x1f);
		const int rb = (int)((op >> 11) & 0x1f);
		const uint32 ea = (ra ? sg[ra] : 0) + sg[rb];
		uint32 pa;
		if (!ppc->guest_data_probe(ea, 4, false, &pa))
			return 0;
		return nw_jit_pa_ok(pa, 0);
	}
	else if (prim == 31 && ((op >> 1) & 0x3ff) == 87) {
		const int ra = (int)((op >> 16) & 0x1f);
		const int rb = (int)((op >> 11) & 0x1f);
		const uint32 ea = (ra ? sg[ra] : 0) + sg[rb];
		uint32 pa;
		if (!ppc->guest_data_probe(ea, 1, false, &pa))
			return 0;
		return nw_jit_pa_ok(pa, 0);
	}
	else if (prim == 31 && ((op >> 1) & 0x3ff) == 151) {
		const int ra = (int)((op >> 16) & 0x1f);
		const int rb = (int)((op >> 11) & 0x1f);
		const uint32 ea = (ra ? sg[ra] : 0) + sg[rb];
		uint32 pa;
		if (!ppc->guest_data_probe(ea, 4, true, &pa))
			return 0;
		return nw_jit_pa_ok(pa, 1);
	}
	else if (prim == 31 && ((op >> 1) & 0x3ff) == 407) {
		const int ra = (int)((op >> 16) & 0x1f);
		const int rb = (int)((op >> 11) & 0x1f);
		const uint32 ea = (ra ? sg[ra] : 0) + sg[rb];
		uint32 pa;
		if (!ppc->guest_data_probe(ea, 2, true, &pa))
			return 0;
		return nw_jit_pa_ok(pa, 1);
	}
	else if (prim == 31 && (((op >> 1) & 0x3ff) == 343 ||
				 ((op >> 1) & 0x3ff) == 375)) {
		const int ra = (int)((op >> 16) & 0x1f);
		const int rb = (int)((op >> 11) & 0x1f);
		const uint32 ea = (ra ? sg[ra] : 0) + sg[rb];
		uint32 pa;
		if (!ppc->guest_data_probe(ea, 2, false, &pa))
			return 0;
		return nw_jit_pa_ok(pa, 0);
	}
	else if (prim == 33)
		;
	else if (prim == 34)
		width = 1;
	else if (prim == 38 || prim == 39) {
		width = 1;
		is_st = 1;
	} else if (prim == 40 || prim == 42 || prim == 43)
		width = 2;
	else if (prim == 44 || prim == 45) {
		width = 2;
		is_st = 1;
	} else if (prim == 50)
		width = 8;
	else if (prim == 46 || prim == 47) {
		const int rd = (int)((op >> 21) & 0x1f);
		width = 4 * (32 - rd);
		is_st = (prim == 47);
	} else
		return 1;
	const int ra = (int)((op >> 16) & 0x1f);
	const int simm = (int16_t)(op & 0xffffu);
	const uint32 ea = (ra ? sg[ra] : 0) + (uint32)simm;
	uint32 pa;
	if (!ppc->guest_data_probe(ea, (unsigned)width, is_st, &pa))
		return 0;
	return nw_jit_pa_ok(pa, is_st);
}

static uint32 nw_jit_rotl32(uint32 x, uint32 n)
{
	n &= 31;
	return n ? ((x << n) | (x >> (32 - n))) : x;
}

static uint32 nw_jit_mask(uint32 mb, uint32 me)
{
	return (mb > me) ?
		~(((uint32)-1 >> mb) ^ ((me >= 31) ? 0 : (uint32)-1 >> (me + 1))) :
		(((uint32)-1 >> mb) ^ ((me >= 31) ? 0 : (uint32)-1 >> (me + 1)));
}

static void nw_jit_sg_apply(powerpc_cpu *ppc, uint32 *sg, uint32 op)
{
	const int prim = (int)(op >> 26);
	const int rd = (int)((op >> 21) & 0x1f);
	const int ra = (int)((op >> 16) & 0x1f);
	const int rb = (int)((op >> 11) & 0x1f);
	const int xo = (int)((op >> 1) & 0x3ff);
	const int simm = (int16_t)(op & 0xffffu);
	if (prim == 14) {
		sg[rd] = (ra ? sg[ra] : 0) + (uint32)simm;
		return;
	}
	if (prim == 15) {
		sg[rd] = (ra ? sg[ra] : 0) + ((uint32)simm << 16);
		return;
	}
	if (prim == 31 && xo == 19)
		return;
	if (prim == 7) {
		sg[rd] = (uint32)((int64)(int32)sg[ra] * (int64)simm);
		return;
	}
	if (prim == 12 || prim == 13) {
		sg[rd] = sg[ra] + (uint32)simm;
		return;
	}
	if (prim == 31 && (xo == 10 || xo == 522)) {
		sg[rd] = sg[ra] + sg[rb];
		return;
	}
	if (prim == 31 && xo == 520) {
		sg[rd] = sg[rb] - sg[ra];
		return;
	}
	if (prim == 31 && xo == 136) {
		sg[rd] = ~sg[ra] + sg[rb];
		return;
	}
	if (prim == 31 && xo == 40) {
		sg[rd] = sg[rb] - sg[ra];
		return;
	}
	if (prim == 28) {
		sg[ra] = sg[rd] & (op & 0xffffu);
		return;
	}
	if (prim == 31 && xo == 922) {
		sg[ra] = (uint32)(int32)(int16)(uint16)sg[rd];
		return;
	}
	if (prim == 31 && xo == 954) {
		sg[ra] = (uint32)(int32)(int8)(uint8)sg[rd];
		return;
	}
	if (prim == 31 && xo == 24) {
		const uint32 sh = sg[rb] & 0x3fu;
		sg[ra] = (sh >= 32u) ? 0 : (sg[rd] << sh);
		return;
	}
	if (prim == 31 && xo == 536) {
		const uint32 sh = sg[rb] & 0x3fu;
		sg[ra] = (sh >= 32u) ? 0 : (sg[rd] >> sh);
		return;
	}
	if (prim == 31 && xo == 792) {
		sg[ra] = (uint32)((int32)sg[rd] >> (int)(sg[rb] & 31u));
		return;
	}
	if (prim == 24) {
		sg[ra] = sg[rd] | (op & 0xffffu);
		return;
	}
	if (prim == 31 && xo == 444) {
		sg[ra] = sg[rd] | sg[rb];
		return;
	}
	if (prim == 20) {
		const int sh = rb, mb = (int)((op >> 6) & 0x1f), me = (int)((op >> 1) & 0x1f);
		const uint32 m = nw_jit_mask((uint32)mb, (uint32)me);
		sg[ra] = (nw_jit_rotl32(sg[rd], (uint32)sh) & m) | (sg[ra] & ~m);
		return;
	}
	if (prim == 21) {
		const int sh = rb, mb = (int)((op >> 6) & 0x1f), me = (int)((op >> 1) & 0x1f);
		sg[ra] = nw_jit_rotl32(sg[rd], (uint32)sh) & nw_jit_mask((uint32)mb, (uint32)me);
		return;
	}
	if (prim == 31 && xo == 266) {
		sg[rd] = sg[ra] + sg[rb];
		return;
	}
	if (prim == 32 || prim == 33) {
		const uint32 ea = (ra ? sg[ra] : 0) + (uint32)simm;
		uint32 pa;
		if (ppc->guest_data_probe(ea, 4, false, &pa) && nw_jit_pa_ok(pa, 0))
			sg[rd] = vm_read_memory_4(pa);
		if (prim == 33 && ra)
			sg[ra] = ea;
		return;
	}
	if (prim == 34) {
		const uint32 ea = (ra ? sg[ra] : 0) + (uint32)simm;
		uint32 pa;
		if (ppc->guest_data_probe(ea, 1, false, &pa) && nw_jit_pa_ok(pa, 0))
			sg[rd] = vm_read_memory_1(pa);
		return;
	}
	if (prim == 31 && xo == 87) {
		const uint32 ea = (ra ? sg[ra] : 0) + sg[rb];
		uint32 pa;
		if (ppc->guest_data_probe(ea, 1, false, &pa) && nw_jit_pa_ok(pa, 0))
			sg[rd] = vm_read_memory_1(pa);
		return;
	}
	if ((prim == 37 || prim == 39 || prim == 45) && ra) {
		sg[ra] = (ra ? sg[ra] : 0) + (uint32)simm;
		return;
	}
	if (prim == 31 && xo == 23) {
		const uint32 ea = (ra ? sg[ra] : 0) + sg[rb];
		uint32 pa;
		if (ppc->guest_data_probe(ea, 4, false, &pa) && nw_jit_pa_ok(pa, 0))
			sg[rd] = vm_read_memory_4(pa);
		return;
	}
	if (prim == 31 && (xo == 343 || xo == 375)) {
		const uint32 ea = (ra ? sg[ra] : 0) + sg[rb];
		uint32 pa;
		if (ppc->guest_data_probe(ea, 2, false, &pa) && nw_jit_pa_ok(pa, 0))
			sg[rd] = (uint32)(int32)(int16)vm_read_memory_2(pa);
		if (xo == 375 && ra)
			sg[ra] = ea;
		return;
	}
}

static bool nw_jit_peek(uint32 ea, uint32 *opcode)
{
	if (!ppc32_guest_mmu_enabled()) {
		*opcode = vm_read_memory_4(ea);
		return true;
	}
	ppc32_xlate_result r = ppc32_guest_mmu().translate(ea, PPC32_XLATE_IR, 4);
	if (!r.ok)
		return false;
	*opcode = vm_read_memory_4(r.pa);
	return true;
}

int powerpc_cpu::nw_jit_try(uint32 first_opcode)
{
	const int mode = nw_jit_mode();
	if (mode != NW_JIT_ON && mode != NW_JIT_VERIFY)
		return 0;

	const uint32 guest_pc = pc();
	const uint32 phys_page = last_fetch_pa_ & ~0xfffu;
	const uint32 msr_ir = (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_IR) ? 1u : 0u;
	uint32 ops[NW_JIT_MAX_BLOCK];
	int n = 0;
	int hit = 0;
	nw_jit_fn fn = nw_jit_cache_get(phys_page, guest_pc, msr_ir, 0, &n);
	if (fn && n > 0 && n <= NW_JIT_MAX_BLOCK) {
		hit = 1;
		/* Hit: skip peek/mem_ok/sg_apply. Helpers take DSI (4d) or
		 * return 0 for IO. ops[] from the fetched physical page. */
		for (int i = 0; i < n; i++)
			ops[i] = vm_read_memory_4(last_fetch_pa_ + (uint32)i * 4u);
	} else {
		fn = NULL;
		if (!nw_jit_op_supported(first_opcode)) {
			nw_jit_note_skip_unsup(first_opcode);
			return 0;
		}
		if (!nw_jit_op_dispatch(first_opcode)) {
			nw_jit_verify_skip(1);
			return 0;
		}

		uint32 sg[32];
		for (int i = 0; i < 32; i++)
			sg[i] = gpr(i);
		if (!nw_jit_op_mem_ok(this, first_opcode, sg)) {
			const int prim0 = (int)(first_opcode >> 26);
			const int ra = (int)((first_opcode >> 16) & 0x1f);
			const int simm = (int16_t)(first_opcode & 0xffffu);
			const uint32 ea = (ra ? sg[ra] : 0) + (uint32)simm;
			uint32 pa = 0;
			guest_data_probe(ea, 4, prim0 == 36, &pa);
			nw_jit_verify_uncompared(nw_pa_kind(pa) == NW_PA_IO ? 2 : 1);
			return 0;
		}

		ops[0] = first_opcode;
		n = 1;
		nw_jit_sg_apply(this, sg, ops[0]);
		while (n < NW_JIT_MAX_BLOCK && !nw_jit_op_ends_block(ops[n - 1])) {
			const uint32 ea = guest_pc + (uint32)n * 4u;
			if ((ea & ~0xfffu) != (guest_pc & ~0xfffu))
				break;
			uint32 op;
			if (!nw_jit_peek(ea, &op))
				break;
			if (is_altivec_insn(op) || is_fp_insn(op))
				break;
			if (!nw_jit_op_dispatch(op))
				break;
			if (!nw_jit_op_mem_ok(this, op, sg))
				break;
			ops[n++] = op;
			nw_jit_sg_apply(this, sg, op);
		}

		fn = nw_jit_compile(ops, n, guest_pc, phys_page, msr_ir, 0);
	}
	if (!fn) {
		nw_jit_verify_fail();
		static unsigned nfail_log;
		if (nfail_log < 8u) {
			nfail_log++;
			printf("NW-BOOT JIT verify compile-fail #%u pc=%08x n=%d op=%08x\n",
			       nfail_log, (unsigned)guest_pc, n, (unsigned)first_opcode);
			fflush(stdout);
		}
		return 0;
	}

	nw_jit_cpu jc;
	memset(&jc, 0, sizeof(jc));
	for (int i = 0; i < 32; i++)
		jc.gpr[i] = gpr(i);
	for (int i = 0; i < 32; i++) {
		jc.vr[i][0] = vr(i).w[0];
		jc.vr[i][1] = vr(i).w[1];
		jc.vr[i][2] = vr(i).w[2];
		jc.vr[i][3] = vr(i).w[3];
	}
	for (int i = 0; i < 32; i++)
		jc.fpr[i] = fpr_dw(i);
	jc.cr = cr().get();
	jc.xer = xer().get();
	jc.lr = lr();
	jc.ctr = ctr();
	jc.pc = pc();
	jc.dec = dec_;
	jc.msr = ppc32_guest_mmu().msr();
	jc.host = this;
	fn(&jc);

	if (jc.fault) {
		if (mode == NW_JIT_ON && jc.fault == NW_JIT_FAULT_EXC) {
			for (int i = 0; i < 32; i++)
				gpr(i) = jc.gpr[i];
			cr().set(jc.cr);
			xer().set(jc.xer);
			lr() = jc.lr;
			ctr() = jc.ctr;
			if (jc.dec_wr) {
				if ((dec_ & 0x80000000u) == 0 && (jc.dec & 0x80000000u))
					dec_pending_ = true;
				dec_ = jc.dec;
				dec_tb_base_ = tb_ticks();
			}
			nw_jit_note_exec(n);
#if NW_BOOT_LOG
			for (int i = 0; i < n; i++)
				nw_event_insn();
#endif
			return 1;
		}
		if (mode == NW_JIT_ON && jc.fault == NW_JIT_FAULT_SMC) {
			for (int i = 0; i < 32; i++)
				gpr(i) = jc.gpr[i];
			cr().set(jc.cr);
			xer().set(jc.xer);
			lr() = jc.lr;
			ctr() = jc.ctr;
			pc() = jc.pc + 4u;
			if (jc.dec_wr) {
				if ((dec_ & 0x80000000u) == 0 && (jc.dec & 0x80000000u))
					dec_pending_ = true;
				dec_ = jc.dec;
				dec_tb_base_ = tb_ticks();
			}
			nw_jit_note_exec(n);
#if NW_BOOT_LOG
			for (int i = 0; i < n; i++)
				nw_event_insn();
#endif
			return 1;
		}
		if (mode == NW_JIT_ON && jc.fault == 1 && ppc32_guest_mmu_enabled()) {
			const ppc32_xlate_result xr = ppc32_guest_mmu().translate(
				jc.fault_ea, PPC32_XLATE_DR, 4, jc.fault_st != 0);
			if (!xr.ok) {
				for (int i = 0; i < 32; i++)
					gpr(i) = jc.gpr[i];
				cr().set(jc.cr);
				xer().set(jc.xer);
				lr() = jc.lr;
				ctr() = jc.ctr;
				pc() = jc.pc;
				if (jc.dec_wr) {
					if ((dec_ & 0x80000000u) == 0 && (jc.dec & 0x80000000u))
						dec_pending_ = true;
					dec_ = jc.dec;
					dec_tb_base_ = tb_ticks();
				}
				static unsigned ndsi_log;
				if (ndsi_log < 8u) {
					ndsi_log++;
					printf("NW-BOOT G1: jit dsi #%u pc=%08x ea=%08x st=%u\n",
					       ndsi_log, (unsigned)jc.pc,
					       (unsigned)jc.fault_ea, (unsigned)jc.fault_st);
					fflush(stdout);
				}
				take_data_dsi(jc.fault_ea, jc.fault_st != 0, xr.fault);
				nw_jit_note_exec(n);
				return 1;
			}
		}
		static unsigned nskip_log;
		nskip_log++;
		if (nskip_log <= 8u) {
			printf("NW-BOOT JIT verify skip-%s #%u pc=%08x op=%08x n=%d\n",
			       jc.fault == 2 ? "io" : "dsi", nskip_log,
			       (unsigned)guest_pc, (unsigned)first_opcode, n);
			fflush(stdout);
		}
		nw_jit_verify_uncompared(jc.fault);
		/* VERIFY / IO / unmapped PA: guest follows kpx so SIGSEGV
		 * recovery stays on the interpreter path. */
		return 0;
	}

	if (mode == NW_JIT_ON) {
		for (int i = 0; i < 32; i++)
			gpr(i) = jc.gpr[i];
		for (int i = 0; i < 32; i++)
			fpr_dw(i) = jc.fpr[i];
		cr().set(jc.cr);
		xer().set(jc.xer);
		lr() = jc.lr;
		ctr() = jc.ctr;
		pc() = jc.pc;
		if (jc.dec_wr) {
			/* Same edge as mtspr_oea: 0→1 MSB latches DEC, tb_base = now. */
			if ((dec_ & 0x80000000u) == 0 && (jc.dec & 0x80000000u))
				dec_pending_ = true;
			dec_ = jc.dec;
			dec_tb_base_ = tb_ticks();
		}
		nw_jit_note_exec(n);
#if NW_BOOT_LOG
		{
			static unsigned nlog;
			if (nlog < 16u) {
				nlog++;
				printf("NW-BOOT JIT on #%u n=%d pc=%08x op=%08x -> %08x\n",
				       nlog, n, (unsigned)guest_pc, (unsigned)first_opcode,
				       (unsigned)jc.pc);
				fflush(stdout);
			}
			for (int i = 0; i < n; i++) {
				nw_event_insn();
				if (!hit)
					nw_jit_pc_hot(guest_pc + (uint32)i * 4u, ops[i]);
			}
		}
#endif
		return 1;
	}

	for (int i = 0; i < n; i++) {
		if (pc() != guest_pc + (uint32)i * 4u)
			break;
		const instr_info_t *ii = decode(ops[i]);
		ii->execute(this, ops[i]);
#if NW_BOOT_LOG
		nw_event_insn();
		nw_jit_pc_hot(guest_pc + (uint32)i * 4u, ops[i]);
#endif
		if (nw_jit_op_ends_block(ops[i]))
			break;
	}

	unsigned bits = 0;
	if (jc.pc != pc())
		bits |= 2u;
	if (jc.cr != cr().get())
		bits |= 4u;
	if (jc.xer != xer().get())
		bits |= 8u;
	if (jc.lr != lr())
		bits |= 16u;
	if (jc.ctr != ctr())
		bits |= 32u;
	if (jc.dec != dec_)
		bits |= 64u;
	int dg = -1;
	for (int i = 0; i < 32; i++) {
		if (jc.gpr[i] != gpr(i)) {
			bits |= 128u;
			if (dg < 0)
				dg = i;
		}
	}
	nw_jit_verify_note(ops, n, bits != 0);

	struct uniq_miss { uint32 op; unsigned n, bits; uint32 pc0; };
	static uniq_miss uniq[48];
	static unsigned nuniq, nmiss, ncmp;
	ncmp++;

	if (bits) {
		nmiss++;
		int slot = -1;
		for (unsigned i = 0; i < nuniq; i++) {
			if (uniq[i].op == first_opcode) {
				slot = (int)i;
				break;
			}
		}
		if (slot < 0 && nuniq < 48u) {
			slot = (int)nuniq++;
			uniq[slot].op = first_opcode;
			uniq[slot].n = 0;
			uniq[slot].bits = 0;
			uniq[slot].pc0 = guest_pc;
		}
		if (slot >= 0) {
			uniq[slot].n++;
			uniq[slot].bits |= bits;
		}
		const int first_of_op = (slot >= 0 && uniq[slot].n == 1);
		if (first_of_op || nmiss <= 8u) {
			printf("NW-BOOT JIT verify miss #%u uniq=%d n=%u ninsns=%d pc=%08x op=%08x bits=%x\n",
			       nmiss, slot + 1, slot >= 0 ? uniq[slot].n : 0u, n,
			       (unsigned)guest_pc, (unsigned)first_opcode, bits);
			printf("NW-BOOT JIT verify jit pc=%08x cr=%08x xer=%08x lr=%08x ctr=%08x dec=%08x\n",
			       (unsigned)jc.pc, (unsigned)jc.cr, (unsigned)jc.xer,
			       (unsigned)jc.lr, (unsigned)jc.ctr, (unsigned)jc.dec);
			printf("NW-BOOT JIT verify kpx pc=%08x cr=%08x xer=%08x lr=%08x ctr=%08x dec=%08x\n",
			       (unsigned)pc(), (unsigned)cr().get(), (unsigned)xer().get(),
			       (unsigned)lr(), (unsigned)ctr(), (unsigned)dec_);
			if (dg >= 0)
				printf("NW-BOOT JIT verify gpr%d jit=%08x kpx=%08x\n",
				       dg, (unsigned)jc.gpr[dg], (unsigned)gpr(dg));
			if (bits & 4u) {
				const int ra = (int)((first_opcode >> 16) & 0x1f);
				const int rb = (int)((first_opcode >> 11) & 0x1f);
				printf("NW-BOOT JIT verify cr-ops ra=%d %08x rb=%d %08x\n",
				       ra, (unsigned)jc.gpr[ra], rb, (unsigned)jc.gpr[rb]);
			}
			fflush(stdout);
		}
	} else if (ncmp <= 16u) {
		printf("NW-BOOT JIT verify ok #%u ninsns=%d pc=%08x op=%08x -> pc=%08x\n",
		       ncmp, n, (unsigned)guest_pc, (unsigned)first_opcode, (unsigned)pc());
		fflush(stdout);
	}
	if ((ncmp % 100000u) == 0)
		nw_jit_verify_dump((ncmp % 1000000u) == 0 ? "hist" : "periodic");
	return 1;
}
#endif

void powerpc_cpu::execute(uint32 entry)
{
	bool invalidated_cache = false;
	pc() = entry;
#if PPC_EXECUTE_DUMP_STATE
	const bool dump_state = true;
#endif
	execute_depth++;
#if PPC_DECODE_CACHE || PPC_ENABLE_JIT
	if (!ppc32_guest_mmu_enabled() && (execute_depth == 1 || (PPC_ENABLE_JIT && PPC_REENTRANT_JIT))) {
#if PPC_ENABLE_JIT
		if (use_jit) {
			block_info *bi = my_block_cache.find(pc());
			if (bi == NULL)
				bi = compile_block(pc());
			for (;;) {
				// Execute all cached blocks
				for (;;) {
					codegen.execute(bi->entry_point);

					if (!spcflags().empty()) {
						if (!check_spcflags())
							goto return_site;

						// Force redecoding if cache was invalidated
						if (spcflags().test(SPCFLAG_JIT_EXEC_RETURN)) {
							spcflags().clear(SPCFLAG_JIT_EXEC_RETURN);
							invalidated_cache = true;
							break;
						}
					}

					// Don't check for backward branches here as this
					// is now done by generated code. Besides, we will
					// get here if the fast cache lookup failed too.
					if ((bi = my_block_cache.find(pc())) == NULL)
						break;
				}

				// Compile new block
				bi = compile_block(pc());
			}
		}
#endif
#if PPC_DECODE_CACHE
		block_info *bi = my_block_cache.find(pc());
		if (bi != NULL)
			goto pdi_execute;
		for (;;) {
#if PPC_PROFILE_COMPILE_TIME
			compile_count++;
			clock_t start_time;
			start_time = clock();
#endif
			bi = my_block_cache.new_blockinfo();
			bi->init(pc());

			// Predecode a new block
			block_info::decode_info *di;
			const instr_info_t *ii;
			uint32 dpc;
			di = bi->di = decode_cache_p;
			dpc = pc() - 4;
			do {
				uint32 opcode = vm_read_memory_4(dpc += 4);
				ii = decode(opcode);
#if PPC_EXECUTE_DUMP_STATE
				if (dump_state) {
					di->opcode = opcode;
					di->execute = nv_mem_fun(&powerpc_cpu::dump_instruction);
					di++;
				}
#endif
#if PPC_FLIGHT_RECORDER
				if (is_logging()) {
					di->opcode = opcode;
					di->execute = nv_mem_fun(&powerpc_cpu::record_step);
					di++;
				}
#endif
				di->opcode = opcode;
				di->execute = ii->execute;
				di++;
#if PPC_EXECUTE_DUMP_STATE
				if (dump_state) {
					di->opcode = 0;
					di->execute = nv_mem_fun(&powerpc_cpu::fake_dump_registers);
					di++;
				}
#endif
				if (di >= decode_cache_end_p) {
					// Invalidate cache and move current code to start
					invalidate_cache();
					const int blocklen = di - bi->di;
					memmove(decode_cache_p, bi->di, blocklen * sizeof(*di));
					bi->di = decode_cache_p;
					di = bi->di + blocklen;
				}
			} while ((ii->cflow & CFLOW_END_BLOCK) == 0);
			bi->end_pc = dpc;
			bi->min_pc = dpc;
			bi->max_pc = entry;
			bi->size = di - bi->di;
			my_block_cache.add_to_cl_list(bi);
			my_block_cache.add_to_active_list(bi);
			decode_cache_p += bi->size;
#if PPC_PROFILE_COMPILE_TIME
			compile_time += (clock() - start_time);
#endif

			// Execute all cached blocks
		  pdi_execute:
			for (;;) {
				const int r = bi->size % 4;
				di = bi->di + r;
				int n = (bi->size + 3) / 4;
				switch (r) {
				case 0: do {
						di += 4;
						di[-4].execute(this, di[-4].opcode);
				case 3: di[-3].execute(this, di[-3].opcode);
				case 2: di[-2].execute(this, di[-2].opcode);
				case 1: di[-1].execute(this, di[-1].opcode);
					} while (--n > 0);
				}

				if (!spcflags().empty()) {
					if (!check_spcflags())
						goto return_site;

					// Force redecoding if cache was invalidated
					if (spcflags().test(SPCFLAG_JIT_EXEC_RETURN)) {
						spcflags().clear(SPCFLAG_JIT_EXEC_RETURN);
						invalidated_cache = true;
						break;
					}
				}

				if ((bi->pc != pc()) && ((bi = my_block_cache.find(pc())) == NULL))
					break;
			}
		}
#else
		goto do_interpret;
#endif
	}
#endif
  do_interpret:
	for (;;) {
		uint32 opcode;
		if (ppc32_guest_mmu_enabled()) {
			tick_decrementer();
			if (async_exception_pending() && (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_EE)) {
				take_async_exception();
				continue;
			}
		}
		if (!guest_fetch(&opcode)) {
			if (!spcflags().empty() && !check_spcflags())
				goto return_site;
			continue;
		}
#if defined(SHEEPSHAVER) && NW_BOOT_LOG
		if (ppc32_guest_mmu_enabled())
			nw_trace_pc(*this, pc());
#endif
		if (ppc32_guest_mmu_enabled() &&
		    !(ppc32_guest_mmu().msr() & NW_MSR_VEC) && is_altivec_insn(opcode)) {
			take_vpu();
			continue;
		}
		if (ppc32_guest_mmu_enabled() &&
		    !(ppc32_guest_mmu().msr() & ppc32_mmu::MSR_FP) && is_fp_insn(opcode)) {
			take_fpu();
			continue;
		}
#ifdef SHEEPSHAVER
		if (nw_jit_try(opcode)) {
			if (!spcflags().empty() && !check_spcflags())
				goto return_site;
			continue;
		}
#endif
		const instr_info_t *ii = decode(opcode);
		const uint32 insn_pc = pc();
#if PPC_EXECUTE_DUMP_STATE
		if (dump_state)
			dump_instruction(opcode);
#endif
#if PPC_FLIGHT_RECORDER
		if (is_logging())
			record_step(opcode);
#endif
#ifdef __MINGW32__
		assert(ii->execute.default_call_conv_ptr() != 0);
#else
		assert(ii->execute.ptr() != 0);
#endif
		ii->execute(this, opcode);
#if defined(SHEEPSHAVER) && NW_BOOT_LOG
		nw_event_insn();
		nw_jit_pc_hot(insn_pc, opcode);
#endif
#if PPC_EXECUTE_DUMP_STATE
		if (dump_state)
			dump_registers();
#endif
		if (!spcflags().empty() && !check_spcflags())
			goto return_site;
	}
  return_site:
	// Tell upper level we invalidated cache?
	if (invalidated_cache)
		spcflags().set(SPCFLAG_JIT_EXEC_RETURN);
	--execute_depth;
}

void powerpc_cpu::execute()
{
	execute(pc());
}

void powerpc_cpu::init_decode_cache()
{
#if PPC_DECODE_CACHE
	decode_cache = (block_info::decode_info *)vm_acquire(DECODE_CACHE_SIZE);
	if (decode_cache == VM_MAP_FAILED) {
		fprintf(stderr, "powerpc_cpu: Could not allocate decode cache\n");
		abort();
	}

	D(bug("powerpc_cpu: Allocated decode cache: %d KB at %p\n", DECODE_CACHE_SIZE / 1024, decode_cache));
	decode_cache_p = decode_cache;
	decode_cache_end_p = decode_cache + DECODE_CACHE_MAX_ENTRIES;
#if FLIGHT_RECORDER
	// Leave enough room to last call to record_step()
	decode_cache_end_p -= 2;
#endif
#if PPC_EXECUTE_DUMP_STATE
	// Leave enough room to last calls to dump state functions
	decode_cache_end_p -= 2;
#endif
#endif
}

void powerpc_cpu::kill_decode_cache()
{
#if PPC_DECODE_CACHE
	vm_release(decode_cache, DECODE_CACHE_SIZE);
#endif
}

void powerpc_cpu::invalidate_cache()
{
	D(bug("Invalidate all cache blocks\n"));
#if PPC_DECODE_CACHE || PPC_ENABLE_JIT
	my_block_cache.clear();
	my_block_cache.initialize();
	spcflags().set(SPCFLAG_JIT_EXEC_RETURN);
#endif
#if PPC_ENABLE_JIT
	codegen.invalidate_cache();
#endif
#if PPC_DECODE_CACHE
	decode_cache_p = decode_cache;
#endif
	/* tlbie/tlbia drop TLB entries only. NW JIT is keyed by phys_page. */
}

void powerpc_block_info::invalidate()
{
#if PPC_DECODE_CACHE
	// Don't do anything if this is a predecoded block
	if (di)
		return;
#endif
#if DYNGEN_DIRECT_BLOCK_CHAINING
	for (int i = 0; i < MAX_TARGETS; i++) {
		link_info * const tli = &li[i];
		uint32 tpc = tli->jmp_pc;
		// For any jump within page boundaries, reset the jump address
		// to the target block resolver (trampoline)
		if (tpc != INVALID_PC && ((tpc ^ pc) >> 12) == 0)
			dg_set_jmp_target(tli->jmp_addr, tli->jmp_resolve_addr);
	}
#endif
}

void powerpc_cpu::invalidate_cache_range(uintptr start, uintptr end)
{
	D(bug("Invalidate cache block [%08x - %08x]\n", start, end));
#if PPC_DECODE_CACHE || PPC_ENABLE_JIT
#if DYNGEN_DIRECT_BLOCK_CHAINING
	if (use_jit) {
		// Invalidate on page boundaries
		start &= -4096;
		end = (end + 4095) & -4096;
		D(bug("    at page boundaries [%08x - %08x]\n", start, end));
	}
#endif
	spcflags().set(SPCFLAG_JIT_EXEC_RETURN);
	my_block_cache.clear_range(start, end);
#endif
}
