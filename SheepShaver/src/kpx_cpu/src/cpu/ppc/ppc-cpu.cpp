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
#include "nw_log.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>
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
#include "cpu_emulation.h"
#include <sys/time.h>
#include "emul_op.h"
#include "nw_boot_contract.h"
#include "nw_io.h"
#include "nw_devices.h"
#include "nw_script.h"
#include "nw_jit.h"
#include "nw_jit_verify.h"
#include "nw_68k_jit.h"
#include "xlowmem.h"
#include "nw_68k_core.h"
#include "prefs.h"
#include "timer.h"
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
	fpu_retry_pc_ = 0;
	fpu_retry_on_ = 0;
	dec_ = 0xffffffffu;
	tb_offset_ = 0;
	dec_tb_base_ = tb_host_ticks();
	dec_pending_ = false;
	dec_tick_div_ = 0;
	jit_dec_div_ = 0;
	exception_serial_ = 0;
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
		nw_jit_set_host_msr(powerpc_cpu::jit_host_msr);
		nw_jit_set_host_pa(powerpc_cpu::jit_host_lwz_pa, powerpc_cpu::jit_host_stw_pa);
		nw_jit_set_host_mfspr(powerpc_cpu::jit_host_mfspr);
		nw_jit_set_host_dec(powerpc_cpu::jit_host_dec);
		nw_jit_set_host_system(powerpc_cpu::jit_host_system);
		nw_jit_set_host_isync(powerpc_cpu::jit_host_isync);
		nw_jit_set_host_mtmsr(powerpc_cpu::jit_host_mtmsr);
		nw_jit_set_host_mtsr(powerpc_cpu::jit_host_mtsr);
		nw_jit_set_host_mfsr(powerpc_cpu::jit_host_mfsr);
		nw_jit_set_host_trap(powerpc_cpu::jit_host_trap);
		nw_jit_set_host_fp_exception(powerpc_cpu::jit_host_fp_exception);
		nw_jit_set_host_sc(powerpc_cpu::jit_host_sc);
		nw_jit_set_host_mtspr(powerpc_cpu::jit_host_mtspr);
		nw_jit_set_host_lvx(powerpc_cpu::jit_host_lvx);
		nw_jit_set_host_stvx(powerpc_cpu::jit_host_stvx);
		nw_jit_set_host_vmx(powerpc_cpu::jit_host_vmx);
		nw_jit_set_host_rfi(powerpc_cpu::jit_host_rfi);
		nw_jit_set_host_chain(powerpc_cpu::jit_host_chain);
		nw_jit_set_host_icbi(powerpc_cpu::jit_host_icbi);
		nw_jit_set_host_tlbie(powerpc_cpu::jit_host_tlbie);
		nw_jit_set_host_tlbia(powerpc_cpu::jit_host_tlbia);
		nw_jit_set_host_lwarx(powerpc_cpu::jit_host_lwarx);
		nw_jit_set_host_stwcx(powerpc_cpu::jit_host_stwcx);
		nw_jit_set_host_lfd(powerpc_cpu::jit_host_lfd);
		nw_jit_set_host_stfd(powerpc_cpu::jit_host_stfd);
		nw_jit_set_host_half(powerpc_cpu::jit_host_lh, powerpc_cpu::jit_host_sth);
		nw_jit_set_host_byte(powerpc_cpu::jit_host_lb, powerpc_cpu::jit_host_stb);
	}
#endif
}

#ifdef SHEEPSHAVER
void nw_jit_host_memory_written(const void *buffer, uint32_t nbytes)
{
    if (!nbytes || !ppc32_guest_mmu_enabled()) return;
    const uintptr address = reinterpret_cast<uintptr>(buffer);
    // I/O also uses stack/heap buffers. Only the direct guest mapping owns
    // physical pages; do not truncate an unrelated host pointer into that map.
    if (address < VMBaseDiff || address - VMBaseDiff > UINT32_MAX) return;
    const uint32 pa = uint32(address - VMBaseDiff);
    if (uint64(pa) + nbytes > (UINT64_C(1) << 32)) return;
    nw_jit_invalidate_range_src(pa,nbytes,NW_JIT_FL_HOST);
}
#endif

#if defined(SHEEPSHAVER) && NW_BOOT_LOG
/*
 *  Debug-only PC trace. Records every taken control transfer (pc != prev+4)
 *  in a ring and dumps it once when the NK first enters its debug-print /
 *  panic region (ROM+0x325500..0x325fff). Observation only; names the call
 *  site of a silent spin that raises no exception.
 */
static bool nw_jit_peek(uint32 ea, uint32 *opcode);
static int g_glue_arm;
static uint64 g_glue_n, g_glue_sum, g_glue_trips;

static void nw_glue_add(int n)
{
	if (g_glue_arm && n > 0)
		g_glue_n += (uint64)n;
}

static void nw_glue_site(powerpc_cpu &cpu, uint32 pc)
{
	static int dumped_de10, dumped_e128, dumped_e8c4;
	static int nlog;
	int *dumped = 0;
	if (pc == 0x6806de10u) {
		g_glue_arm = 1;
		g_glue_n = 0;
		dumped = &dumped_de10;
	} else if (pc == 0x6806e128u) {
		dumped = &dumped_e128;
	} else if (pc == 0x6806e8c4u) {
		if (g_glue_arm) {
			g_glue_sum += g_glue_n;
			g_glue_trips++;
			g_glue_arm = 0;
		}
		dumped = &dumped_e8c4;
	} else
		return;
	if (nlog < 12) {
		nlog++;
		const uint32 trap = (cpu.gpr(29) >> 3) & 0xffffu;
		NW_DIAG("NW-BOOT G1: glue pc=%08x lr=%08x cr=%08x r24=%08x trap=%04x ppc_rd=%llu insns=%llu\n",
		       (unsigned)pc, (unsigned)cpu.debug_lr(), (unsigned)cpu.debug_cr(),
		       (unsigned)cpu.gpr(24), (unsigned)trap,
		       (unsigned long long)nw_mixedmode_ppc_rds(),
		       (unsigned long long)g_glue_n);
		fflush(stdout);
	}
	if (dumped && !*dumped) {
		*dumped = 1;
		for (int k = 0; k < 8; k++) {
			uint32 w = 0;
			const uint32 ea = pc + (uint32)k * 4u;
			const int ok = nw_jit_peek(ea, &w);
			NW_DIAG("NW-BOOT G1: glue word %08x %+d %s %08x\n",
			       (unsigned)pc, k, ok ? "ok" : "fail", (unsigned)w);
		}
		fflush(stdout);
	}
	if (g_glue_trips && (g_glue_trips % 10000ull) == 0 && pc == 0x6806e8c4u) {
		NW_DIAG("NW-BOOT G1: glue trips=%llu insns_to_twi=%llu avg=%llu\n",
		       (unsigned long long)g_glue_trips,
		       (unsigned long long)g_glue_sum,
		       (unsigned long long)(g_glue_sum / g_glue_trips));
		fflush(stdout);
	}
}

static void nw_trace_pc(powerpc_cpu &cpu, uint32 pc)
{
	extern uint32 ROMBase;
	if (pc == 0x6806de10u || pc == 0x6806e128u || pc == 0x6806e8c4u)
		nw_glue_site(cpu, pc);
	enum { RING = 256 };
	static uint32 from[RING], to[RING];
	static unsigned head, count;
	static uint32 prev;
	static int dumped, dumped_panic;

	if (prev != 0 && pc != prev + 4) {
		/* The NK's debug printer loops thousands of times per message;
		 * keeping it would flush the path that led there out of the
		 * ring. */
		const uint32 f = prev - ROMBase, t = pc - ROMBase;
		const bool print_loop = f >= 0x325500u && f < 0x328000u &&
					t >= 0x325500u && t < 0x328000u;
		if (!print_loop) {
			from[head] = prev;
			to[head] = pc;
			head = (head + 1) % RING;
			if (count < RING)
				count++;
		}
	}
	prev = pc;
	const uint32 off = pc - ROMBase;
	char buf[256];
	const char *why = 0;
	/* NK panic / debugger entry (saves all GPRs to KDP+0x700). */
	if (off == 0x326420u || off == 0x326428u) {
		static int n;
		if (n < 4) {
			n++;
			snprintf(buf, sizeof(buf),
				 "NKPANIC entry=%08x lr=%08x r1=%08x r3=%08x r8=%08x r9=%08x sprg0=%08x msr=%08x",
				 (unsigned)pc, (unsigned)cpu.debug_lr(), (unsigned)cpu.gpr(1),
				 (unsigned)cpu.gpr(3), (unsigned)cpu.gpr(8), (unsigned)cpu.gpr(9),
				 (unsigned)cpu.sprg(0), (unsigned)ppc32_guest_mmu().msr());
			nw_boot_diag(buf);
			/* The NK prints its panic text a character at a time out
			 * of ROM; r8 walks that string. */
			const uint32 msg = (cpu.gpr(8) - 0x140u) & ~0xfu;
			for (unsigned line = 0; line < 5u; line++) {
				const uint32 a = msg + line * 96u;
				char txt[100];
				unsigned k = 0;
				for (unsigned b = 0; b < 96u; b++) {
					const uint32 w = vm_read_memory_4((a + b) & ~3u);
					const unsigned c = (w >> (8u * (3u - ((a + b) & 3u)))) & 0xffu;
					txt[k++] = (c >= 0x20u && c < 0x7fu) ? (char)c : '.';
				}
				txt[k] = 0;
				snprintf(buf, sizeof(buf), "NKPANIC msg@%08x \"%s\"", (unsigned)a, txt);
				nw_boot_diag(buf);
			}
			if (!dumped_panic) {
				dumped_panic = 1;
				why = "NKPANIC";
			}
		}
	}
	if (!why && !dumped && off >= 0x325500u && off < 0x326000u) {
		dumped = 1;
		why = "NK debug region";
	}
	if (!why)
		return;
	snprintf(buf, sizeof(buf), "PCTRACE enter %s pc=%08x (%u transfers)",
		 why, (unsigned)pc, count);
	nw_boot_diag(buf);
	unsigned i = (head + RING - count) % RING;
	for (unsigned n = 0; n < count; n++, i = (i + 1) % RING) {
		snprintf(buf, sizeof(buf), "PCTRACE %08x -> %08x",
			 (unsigned)from[i], (unsigned)to[i]);
		nw_boot_diag(buf);
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
	++exception_serial_;
	ppc32_mmu &mmu = ppc32_guest_mmu();
	if (event_pc == 0xffffffffu)
		event_pc = srr0;
	srr0_ = srr0;
	/* OEA: SRR1[0,5-9,16-31] come from the MSR (this keeps MSR[VEC], bit 6,
	   which the NK reads to decide whether the interrupted context owns the
	   vector unit); SRR1[1-4,10-15] carry exception-specific information. */
	srr1_ = (mmu.msr() & ~0x783f0000u) | srr1_extra;
	const uint32 old_msr = mmu.msr();
	mmu.set_msr(old_msr & ~ppc32_mmu::MSR_EXC_CLEAR);
#ifdef SHEEPSHAVER
	nw_jit_itlb_note_msr(old_msr, mmu.msr());
#endif
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
	/* Replay must stop even when the handler vector equals the next PC. */
	++exception_serial_;
	ppc32_hotints_dsi dsi;
#ifdef SHEEPSHAVER
	const uint32 old_msr = ppc32_guest_mmu().msr();
#endif
	dsi.take_data_dsi(ppc32_guest_mmu(), pc(), ea, is_store, fault);
#ifdef SHEEPSHAVER
	nw_jit_itlb_note_msr(old_msr, ppc32_guest_mmu().msr());
#endif
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

#ifdef SHEEPSHAVER
static uint64_t g_vec500, g_vec900;
#endif

void powerpc_cpu::take_dec()
{
	dec_pending_ = false;
#ifdef SHEEPSHAVER
	g_vec900++;
#endif
	take_exception(NW_VEC_DECREMENTER, pc(), 0);
}

void powerpc_cpu::take_external()
{
	/* Level-sensitive: the line stays asserted until the handler's IACK. */
#ifdef SHEEPSHAVER
	g_vec500++;
#endif
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

#ifdef SHEEPSHAVER
/* 68k-emu twi table at 0x6806e8c0: per-n trap / palaver hit / ctx miss. */
static uint64 g_tw_emu[NW_EMU_KCALL_N];
static uint64 g_kcall_seen[NW_EMU_KCALL_N];
static uint64 g_kcall_hit[NW_EMU_KCALL_N];
static uint64 g_kcall_miss_ctx[NW_EMU_KCALL_N];

static unsigned emu_kcall_n(uint32 src)
{
	if (src < (uint32)NW_EMU_KCALL_BASE ||
	    src >= (uint32)NW_EMU_KCALL_BASE + (uint32)NW_EMU_KCALL_N * 4u ||
	    (src & 3u) != 0)
		return ~0u;
	return (src - (uint32)NW_EMU_KCALL_BASE) / 4u;
}

static void note_tw_emu(uint32 src)
{
	const unsigned n = emu_kcall_n(src);
	if (n < (unsigned)NW_EMU_KCALL_N)
		g_tw_emu[n]++;
}

static void kcall_hist_dump(const char *why)
{
	NW_DIAG("NW-BOOT G1: kcall %s", why ? why : "?");
	for (unsigned n = 0; n < (unsigned)NW_EMU_KCALL_N; n++) {
		if (!g_tw_emu[n] && !g_kcall_seen[n] && !g_kcall_hit[n] && !g_kcall_miss_ctx[n])
			continue;
		NW_DIAG(" n%u tw=%llu seen=%llu hit=%llu miss=%llu", n,
		       (unsigned long long)g_tw_emu[n],
		       (unsigned long long)g_kcall_seen[n],
		       (unsigned long long)g_kcall_hit[n],
		       (unsigned long long)g_kcall_miss_ctx[n]);
	}
	NW_DIAG(" mm enter=%llu leave=%llu ppc_rd=%llu fast=%llu\n",
	       (unsigned long long)nw_mixedmode_enters(),
	       (unsigned long long)nw_mixedmode_leaves(),
	       (unsigned long long)nw_mixedmode_ppc_rds(),
	       (unsigned long long)nw_jit_kcall_fast());
	fflush(stdout);
}

#if NW_BOOT_LOG
/*
 * NW_KDP_CONTEXT_PTR is an NK v1 offset (elliotnunn/NanoKernel master
 * Defines.s); the 9.2.1 ROM ships v2, where it reads 0 for the whole
 * Finder session (cs-p21-baseline/after). Let the ROM name its own
 * offset: the 0x700 vector's ProgramInt starts with the standard
 * palaver, so the first `lwz r6,imm(r1)` (0x80c1xxxx) is KDP.ContextPtr
 * in this NK. Also dump the KDP window so a CB-looking pointer can be
 * told apart from Flags.
 */
static void kdp_probe_dump(const char *why, uint32 kdp, uint32 vectbl)
{
	NW_DIAG("NW-BOOT G1: kdp probe %s base=%08x\n", why ? why : "?", (unsigned)kdp);
	/* NK v2 LoadInterruptRegisters: ContextPtr at -0x14, Flags at -0x10,
	 * handler base at -0x4. */
	NW_DIAG("NW-BOOT G1: kdp-20 %08x %08x %08x %08x  -10 %08x %08x %08x %08x\n",
	       (unsigned)vm_read_memory_4(kdp - 0x20u), (unsigned)vm_read_memory_4(kdp - 0x1cu),
	       (unsigned)vm_read_memory_4(kdp - 0x18u), (unsigned)vm_read_memory_4(kdp - 0x14u),
	       (unsigned)vm_read_memory_4(kdp - 0x10u), (unsigned)vm_read_memory_4(kdp - 0x0cu),
	       (unsigned)vm_read_memory_4(kdp - 0x08u), (unsigned)vm_read_memory_4(kdp - 0x04u));
	for (uint32 off = 0x5f0; off < 0x6b0; off += 16)
		NW_DIAG("NW-BOOT G1: kdp+%03x %08x %08x %08x %08x\n", (unsigned)off,
		       (unsigned)vm_read_memory_4(kdp + off),
		       (unsigned)vm_read_memory_4(kdp + off + 4),
		       (unsigned)vm_read_memory_4(kdp + off + 8),
		       (unsigned)vm_read_memory_4(kdp + off + 12));
	/* The 0x700 stub dispatches through VecTbl (SPRG3) + 0x1c; ProgramInt
	 * opens with `bl LoadInterruptRegisters`. */
	const uint32 target = vectbl ? vm_read_memory_4(vectbl + 0x1cu) : 0;
	const uint32 w0 = target ? vm_read_memory_4(target) : 0;
	uint32 loadint = 0;
	if ((w0 & 0xfc000003u) == 0x48000001u) {
		const int32 li = (int32)((w0 & 0x03fffffcu) << 6) >> 6;
		loadint = target + (uint32)li;
	}
	NW_DIAG("NW-BOOT G1: vectbl=%08x program=%08x loadint=%08x\n",
	       (unsigned)vectbl, (unsigned)target, (unsigned)loadint);
	fflush(stdout);
}
#endif
#endif

void powerpc_cpu::take_program(uint32 srr1_bits)
{
	/* SRR1[46] trap = 0x00020000, [45] privileged = 0x00040000,
	 * [44] illegal = 0x00080000. */
	take_exception(NW_VEC_PROGRAM, pc(), srr1_bits);
}
#ifdef SHEEPSHAVER
	{
		/* Diagnostic (NW_VERBOSE=1): the first occurrence of each distinct (pc, cause) outside the 68k emulator's own
		 * trap use, with the faulting opcode, so an unimplemented instruction is named instead of just misbehaving. */
		static struct { uint32 pc, bits; } seen[48];
		static unsigned nseen;
		const uint32 at = pc();
		if (!(at >= 0x68000000u && at < 0x68c00000u) && nseen < 48) {
			unsigned i = 0;
			while (i < nseen && !(seen[i].pc == at && seen[i].bits == srr1_bits)) i++;
			if (i == nseen) {
				seen[nseen].pc = at; seen[nseen].bits = srr1_bits; nseen++;
				uint32 op = 0;
				const bool ok = nw_jit_peek(at, &op);
				NW_DIAG("NW-BOOT program exception #%u pc=%08x srr1bits=%08x op=%s%08x\n", nseen, (unsigned)at,
				       (unsigned)srr1_bits, ok ? "" : "?", (unsigned)op);
			}
		}
	}
#endif

#ifdef SHEEPSHAVER
/*
 * Host ProgramInt for 68k-emu twi r31,n, built from the 9.2.1 ROM's own
 * NK v2 handler: LoadInterruptRegisters (ROM 0x313d60) then the KCall
 * fast path (ROM 0x314800):
 *   add r8,r8,r1 / NKCallCounts++ / lwz r10,KCallTbl(r8) / mtlr r10
 *   mr r10,r12 (resume at the emulator's LR) / rlwimi r7,r7,27,26,26 / blr
 * reached from 0x3147f0, which does `mtcrf 0x3f,r7` and only falls into
 * the fast path when Flags bit 8 (GlobalFlagSystem) is set.
 *
 * Measured unpaid tw: n=0 ~2.5 M but almost all fail GlobalFlagSystem
 * (remill-kcall-n0 hit=2, 19–48 s stall worse). n=3 VMDispatch 130 k
 * in the t+8–18 s frames=30 window. Widen n=3 with the same ProgramInt
 * decline. n=0/n=2/n=15 and n=5..9 stay on the 0x700 vector.
 *
 * Dead ends, all of them the v1 KDP offsets feeding the NK zeros:
 * tw700d r10=srr0 retrapped; tw700e / cs-p21-after-r10plus4 r10=twi+4
 * walked into Reset; cs-p21-after-r10lr and -fill used SysContextPtr as
 * the CB with Flags read from +0x660 (= 0), so the NK took illegalTrap
 * and ReturnFromInt-spun (mm enter=1, pc stuck 6806e444).
 */
int powerpc_cpu::programint_kcall_fast(void)
{
	if (!ppc32_guest_mmu_enabled())
		return 0;
	const uint32 src = srr0_;
	const unsigned n = emu_kcall_n(src);
	if (n >= (unsigned)NW_EMU_KCALL_N)
		return 0;
	g_kcall_seen[n]++;
#if NW_BOOT_LOG
	{
		static int probed_first_tw;
		if (!probed_first_tw && sprg(0)) {
			probed_first_tw = 1;
			kdp_probe_dump("first-tw", sprg(0), sprg(3));
		}
	}
#endif
	if (n != 1u && n != 3u && n != 4u)
		return 0;
	const uint32 kdp = sprg(0);
	if (!kdp)
		return 0;
	const uint32 ctx = vm_read_memory_4(kdp + (uint32)NW_KDP_V2_CONTEXT_PTR);
	const uint32 flags = vm_read_memory_4(kdp + (uint32)NW_KDP_V2_FLAGS);
	const uint32 base = vm_read_memory_4(kdp + (uint32)NW_KDP_V2_SELF);
#if NW_BOOT_LOG
	/* Differential: NW_KCALL_FAST=0 leaves the trap on the 0x700 vector,
	 * so the next log line shows what the ROM's own LoadInterruptRegisters
	 * wrote into the CB for the previous trap. */
	{
		static int enabled = -1;
		if (enabled < 0) {
			const char *e = getenv("NW_KCALL_FAST");
			enabled = (e && e[0] == '0') ? 0 : 1;
		}
		static unsigned ntrace;
		if (n == 1u && ntrace < 2u) {
			ntrace++;
			NW_DIAG("NW-BOOT G1: kcall_state #%u n=%u srr0=%08x srr1=%08x msr=%08x cr=%08x xer=%08x lr=%08x ctr=%08x\n",
			       ntrace, n, (unsigned)srr0_, (unsigned)srr1_,
			       (unsigned)ppc32_guest_mmu().msr(), (unsigned)cr().get(),
			       (unsigned)xer().get(), (unsigned)lr(), (unsigned)ctr());
			NW_DIAG("NW-BOOT G1: kcall_state #%u r0=%08x r1=%08x r3=%08x r4=%08x r5=%08x r6=%08x r7=%08x r8=%08x r9=%08x r10=%08x r11=%08x r12=%08x r13=%08x\n",
			       ntrace, (unsigned)gpr(0), (unsigned)gpr(1), (unsigned)gpr(3),
			       (unsigned)gpr(4), (unsigned)gpr(5), (unsigned)gpr(6),
			       (unsigned)gpr(7), (unsigned)gpr(8), (unsigned)gpr(9),
			       (unsigned)gpr(10), (unsigned)gpr(11), (unsigned)gpr(12),
			       (unsigned)gpr(13));
			if (ctx)
				NW_DIAG("NW-BOOT G1: kcall_state #%u cb r0=%08x r7=%08x r8=%08x r9=%08x r10=%08x r11=%08x r12=%08x r13=%08x kdp r1=%08x r6=%08x\n",
				       ntrace,
				       (unsigned)vm_read_memory_4(ctx + (uint32)NW_CB_R0),
				       (unsigned)vm_read_memory_4(ctx + (uint32)NW_CB_R7),
				       (unsigned)vm_read_memory_4(ctx + (uint32)NW_CB_R7 + 8u),
				       (unsigned)vm_read_memory_4(ctx + (uint32)NW_CB_R7 + 16u),
				       (unsigned)vm_read_memory_4(ctx + (uint32)NW_CB_R7 + 24u),
				       (unsigned)vm_read_memory_4(ctx + (uint32)NW_CB_R7 + 32u),
				       (unsigned)vm_read_memory_4(ctx + (uint32)NW_CB_R7 + 40u),
				       (unsigned)vm_read_memory_4(ctx + (uint32)NW_CB_R7 + 48u),
				       (unsigned)vm_read_memory_4(kdp + (uint32)NW_KDP_SAVED_R1),
				       (unsigned)vm_read_memory_4(kdp + (uint32)NW_KDP_SAVED_R6));
			fflush(stdout);
		}
		if (!enabled)
			return 0;
	}
#endif
	const uint32 kcall = base ? vm_read_memory_4(base + (uint32)NW_KDP_KCALLTBL + n * 4u) : 0;
	/* Refuse exactly what ProgramInt refuses, so a state the NK would
	 * have sent to illegalTrap still reaches the 0x700 vector. */
	const bool ok = ctx != 0 && kcall != 0 && base == kdp &&
			(flags & (uint32)NW_KDP_FLAG_SYSTEM) != 0 &&
			(flags & (uint32)NW_KDP_FLAG_NO_KCALL) == 0;
#if NW_BOOT_LOG
	{
		static unsigned nmiss;
		if (!ok) {
			g_kcall_miss_ctx[n]++;
			if (nmiss < 16u) {
				nmiss++;
				if (nmiss == 1u)
					kdp_probe_dump("first-miss", kdp, sprg(3));
				NW_DIAG("NW-BOOT G1: kcall_miss #%u n=%u src=%08x kdp=%08x ctx=%08x flags=%08x base=%08x kcall=%08x lr=%08x\n",
				       nmiss, n, (unsigned)src, (unsigned)kdp, (unsigned)ctx,
				       (unsigned)flags, (unsigned)base, (unsigned)kcall,
				       (unsigned)lr());
				fflush(stdout);
			}
			return 0;
		}
	}
#else
	if (!ok) {
		g_kcall_miss_ctx[n]++;
		return 0;
	}
#endif
	/* LoadInterruptRegisters. SPRG1/SPRG2 are what the 0x700 stub would
	 * have saved before the handler ran. */
	const uint32 r1_save = gpr(1);
	const uint32 lr_save = lr();
	const uint32 cr_save = cr().get();
	vm_write_memory_4(kdp + (uint32)NW_KDP_SAVED_R6, gpr(6));
	vm_write_memory_4(kdp + (uint32)NW_KDP_SAVED_R1, r1_save);
	vm_write_memory_4(ctx + (uint32)NW_CB_R0, gpr(0));
	for (int r = 7; r <= 13; r++)
		vm_write_memory_4(ctx + (uint32)NW_CB_R7 + (uint32)(r - 7) * 8u, gpr(r));
	sprg_[1] = r1_save;
	sprg_[2] = lr_save;
	gpr(0) = 0;
	gpr(6) = ctx;
	gpr(11) = srr1_;
	gpr(12) = lr_save;
	gpr(13) = cr_save;
	gpr(1) = base;
	/* KCall fast path: NanoKernelCallCounts[n]++, resume at the
	 * emulator's LR, r8 = &KCallTbl entry. */
	const uint32 r8 = base + n * 4u;
	const uint32 cnt_ea = r8 + (uint32)NW_KDP_NKCALL_COUNTS;
	vm_write_memory_4(cnt_ea, vm_read_memory_4(cnt_ea) + 1u);
	gpr(8) = r8;
	/* CR2..7 are `mtcrf 0x3f,r7` with the pre-rlwimi Flags; CR0 and CR1
	 * are the dispatch's own `cmplwi r8,0xc` and `cmplwi cr1,r8,0x40`. */
	const uint32 tw = n * 4u;
	uint32 cr0 = tw < 0xcu ? 0x8u : (tw == 0xcu ? 0x2u : 0x4u);
	uint32 cr1 = tw < 0x40u ? 0x8u : (tw == 0x40u ? 0x2u : 0x4u);
	if (xer().get() & 0x80000000u) {
		cr0 |= 1u;
		cr1 |= 1u;
	}
	cr().set((cr0 << 28) | (cr1 << 24) | (flags & 0x00ffffffu));
	/* rlwimi r7,r7,27,26,26: rotating left 27 puts bit 21 into bit 26. */
	gpr(7) = (flags & ~0x00000020u) | ((flags & 0x00000400u) >> 5);
	gpr(10) = lr_save;
	lr() = kcall;
	pc() = kcall;
	g_kcall_hit[n]++;
	nw_jit_note_kcall_fast();
#if NW_BOOT_LOG
	{
		static unsigned nlog;
		if (nlog < 8u) {
			nlog++;
			NW_DIAG("NW-BOOT G1: kcall_fast #%u n=%u src=%08x kdp=%08x ctx=%08x flags=%08x lr=%08x -> %08x\n",
			       nlog, n, (unsigned)src, (unsigned)kdp, (unsigned)ctx,
			       (unsigned)flags, (unsigned)lr_save, (unsigned)kcall);
			fflush(stdout);
		}
	}
#endif
	return 1;
}
#endif

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
#ifdef SHEEPSHAVER
		note_tw_emu(pc());
#endif
		const uint32 crv = cr().get();
		const uint32 ctrv = ctr();
		take_program(0x00020000u);
		/* 68k-emu tw (6806e8c0..) is program, not illegal. NK reads
		 * SRR0/SRR1; keep CR/CTR as the interrupted context. */
		cr().set(crv);
		ctr() = ctrv;
#ifdef SHEEPSHAVER
		if (!nw_verify_trace_) (void)programint_kcall_fast();
#endif
		return;
	}
	increment_pc(4);
}

uint64 powerpc_cpu::tb_host_ticks() const
{
#ifdef SHEEPSHAVER
	extern int64 TimebaseSpeed;
	const uint64 us = GetTicks_usec();
	return (us / 1000000u) * (uint64)25000000 + ((us % 1000000u) * (uint64)25000000) / 1000000u;
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
	if (spr == powerpc_registers::SPR_DEC) sample_decrementer();
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
	fpu_retry_pc_ = pc();
	fpu_retry_on_ = 1;
#if defined(SHEEPSHAVER) && NW_BOOT_LOG
	{
		static unsigned n;
		if (n < 8u) {
			n++;
			NW_DIAG("NW-BOOT G1: fpu #%u pc=%08x msr=%08x\n",
			       n, (unsigned)pc(),
			       (unsigned)ppc32_guest_mmu().msr());
			fflush(stdout);
		}
	}
#endif
	take_exception(NW_VEC_FPU, pc(), 0);
}

void powerpc_cpu::finish_fpu_rfi()
{
	if (!fpu_retry_on_ || srr0_ != fpu_retry_pc_)
		return;
	fpu_retry_on_ = 0;
	if (srr1_ & ppc32_mmu::MSR_FP)
		return;
	srr1_ |= ppc32_mmu::MSR_FP;
#if defined(SHEEPSHAVER) && NW_BOOT_LOG
	{
		static unsigned n;
		if (n < 8u) {
			n++;
			NW_DIAG("NW-BOOT G1: fpu-enable #%u pc=%08x srr1=%08x\n",
			       n, (unsigned)srr0_, (unsigned)srr1_);
			fflush(stdout);
		}
	}
#endif
}

void powerpc_cpu::tick_decrementer()
{
	/* DEC decrements at the timebase rate (mftb and DEC share a clock).
	 * Sampled every 256 interpreted instructions; a 0->1 transition of the
	 * MSB raises the decrementer interrupt, once, as on hardware. The NK
	 * programs DEC from TB deltas, so a DEC that ran at instruction rate
	 * made every deadline look expired (DEC storm after the first fire). */
	if (++dec_tick_div_ < 256u)
		return;
	dec_tick_div_ = 0;
	catch_up_timebase();
}

void powerpc_cpu::advance_decrementer(uint64 elapsed)
{
    const uint32 old = dec_;
    dec_ -= (uint32)elapsed;
    // The next 0 -> -1 edge is old+1 ticks away, even if old is negative.
    // Keep the full delta: a delayed sample can span one or more 32-bit wraps.
    if (elapsed > (uint64)old) dec_pending_ = true;
}

void powerpc_cpu::sample_decrementer()
{
    // DEC runs from the host clock; software TB writes must not tick DEC.
    const uint64 now = tb_host_ticks();
    advance_decrementer(now - dec_tb_base_);
    dec_tb_base_ = now;
}

void powerpc_cpu::catch_up_timebase()
{
    sample_decrementer();

#ifdef SHEEPSHAVER
	nw_devices_tick();
	nw_host_tick();
	nw_script_tick();
#if NW_BOOT_LOG
	/* Optional CPU-owned snapshots for diagnosing guest waits. Preview
	 * translation touches no device and does not set guest PTE R/C bits. */
	const uint64 now = tb_ticks();
	static const char *nk_state_path = getenv("NW_TEST_NK_STATE_PATH");
	static uint64 nk_state_last;
	extern int64 TimebaseSpeed;
	if (nk_state_path && now - nk_state_last >= (uint64)(TimebaseSpeed > 0 ? TimebaseSpeed : 25000000)) {
		nk_state_last = now;
		std::string temporary = std::string(nk_state_path) + ".tmp";
		FILE *f = fopen(temporary.c_str(), "w");
		if (f) {
			fprintf(f, "pc=%08x cr=%08x xer=%08x lr=%08x msr=%08x mode=%u depth=%d\n",
			        pc(), cr().get(), xer().get(), lr(), ppc32_guest_mmu().msr(), ReadMacInt32(XLM_RUN_MODE), execute_depth);
			for (unsigned r = 0; r < 32; ++r) fprintf(f, "r%u=%08x%c", r, gpr(r), r % 4 == 3 ? '\n' : ' ');
			const uint32 bases[] = {gpr(24) - 18u, gpr(20) - 16u, gpr(1), 0x910u};
			for (unsigned b = 0; b < 4; ++b) {
				uint32 actual_pa = 0;
				const bool mapped = guest_data_probe(bases[b], 1, false, &actual_pa, NULL, false);
				fprintf(f, "data %08x mapped=%u pa=%08x mac_pa=%08x:", bases[b], mapped, actual_pa, nw_la_to_pa(bases[b]));
				for (unsigned j = 0; j < 64; ++j) {
					uint32 pa;
					if (guest_data_probe(bases[b] + j, 1, false, &pa, NULL, false) &&
					    nw_pa_kind(pa) != NW_PA_IO && nw_pa_kind(pa) != NW_PA_NONE && nw_pa_kind(pa) != NW_PA_FB)
						fprintf(f, " %02x", vm_read_memory_1(pa));
					else fprintf(f, " --");
				}
				fprintf(f, "\n");
			}
			fclose(f);
			rename(temporary.c_str(), nk_state_path);
		}
	}
#endif
	if (nw_sheepblaster_tracing()) {
		static time_t sb_last;
		static int sb_n;
		const time_t sb_now = time(NULL);
		if (sb_now != sb_last && sb_n < 12) {
			sb_last = sb_now;
			sb_n++;
			NW_DIAG("NW-BOOT G1: sheepblaster stuck #%d pc=%08x r24=%08x r1=%08x",
			       sb_n, (unsigned)pc(), (unsigned)gpr(24), (unsigned)gpr(1));
			if (sb_n == 1) {
				NW_DIAG(" bytes");
				for (int i = 0; i < 16; i++)
					NW_DIAG(" %02x", ReadMacInt8(gpr(24) + i));
			}
			NW_DIAG("\n");
			fflush(stdout);
		}
	}
#if NW_BOOT_LOG
	{
		uint32 tm = 0;
		if (RAMBaseHost)
			tm = ReadMacInt32(0x16A);	/* Time Manager Ticks */
		nw_event_tick(pc(), ppc32_guest_mmu().msr(),
			      GetTicks_usec(), tb_ticks(), tm, lr(), gpr(1));
	}
#endif
#if defined(NW_BOOT_LOG) && NW_BOOT_LOG
	{
		static uint64_t last500;
		static time_t last;
		const time_t now = time(NULL);
		if (now != last) {
			last = now;
			static unsigned kdump;
			if ((++kdump % 10u) == 0)
				kcall_hist_dump("tick");
			if (g_vec500 == last500 && g_vec900)
				NW_DIAG("NW-BOOT G1: 900-only r24=%08x pc=%08x n500=%llu n900=%llu ext=%d\n",
				       (unsigned)gpr(24), (unsigned)pc(),
				       (unsigned long long)g_vec500, (unsigned long long)g_vec900,
				       nw_io_ext_irq);
			last500 = g_vec500;
		}
	}
#endif
#endif
}

#ifdef SHEEPSHAVER
volatile int nw_guest_idle_flag;	/* set while the guest sleeps in its idle loop (for NW_GUEST_PROF) */
/* New World never hits the SynchIdleTime EMUL_OP (the 68k pattern is not
 * in this ROM). The guest sits in this isync/b loop — measured at
 * 0x0027bae0 / 0x0027bae4 — and the JIT runs it at full rate. Sleep until
 * the next host interrupt when EE is on and nothing is already pending.
 * The opcode check refuses the address if a different guest lands there. */
int powerpc_cpu::guest_idle_wait()
{
	static int on = -1;
	if (on < 0)
		on = PrefsFindBool("idlewait") ? 1 : 0;
	if (!on || execute_depth != 1)
		return 0;
	const uint32 p = pc();
	if (p != 0x0027bae0u && p != 0x0027bae4u)
		return 0;
	if (nw_la_ram_size <= p + 4u)
		return 0;
	const uint32 op = ReadMacInt32(p);
	if (p == 0x0027bae0u && op != 0x4c00012cu)
		return 0;
	if (p == 0x0027bae4u && op != 0x4bffffe8u)
		return 0;
	if ((ppc32_guest_mmu().msr() & ppc32_mmu::MSR_EE) == 0)
		return 0;
	if (async_exception_pending())
		return 0;
	nw_guest_idle_flag = 1;
	idle_wait();
	nw_guest_idle_flag = 0;
#if NW_BOOT_LOG
	{
		static int logged;
		if (!logged) {
			logged = 1;
			NW_DIAG("NW-BOOT G1: idle sleep pc=%08x\n", (unsigned)p);
			fflush(stdout);
		}
	}
#endif
	return 1;
}
#endif

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

#ifdef SHEEPSHAVER
static int nw_aline_blockmove(powerpc_cpu *ppc)
{
	/* OS trap A22E: A0=src r16, A1=dst r17, D0=count r8. Mac LAs, not PPC DR. */
	const uint32 src = ppc->gpr(16);
	const uint32 dst = ppc->gpr(17);
	const int32 n = (int32)ppc->gpr(8);
	if (n <= 0 || n > 0x04000000)
		return 0;
	const uint32 spa = nw_la_to_pa(src);
	const uint32 dpa = nw_la_to_pa(dst);
	if (nw_pa_kind(spa) == NW_PA_IO || nw_pa_kind(dpa) == NW_PA_IO ||
	    nw_pa_kind(spa) == NW_PA_NONE || nw_pa_kind(dpa) == NW_PA_NONE)
		return 0;
	uint8 *hs = vm_do_get_real_address(spa);
	uint8 *hd = vm_do_get_real_address(dpa);
	if (!hs || !hd)
		return 0;
	memmove(hd, hs, (size_t)n);
	if (nw_pa_kind(dpa) == NW_PA_FB)
		nw_fb_damage_store(dpa, (unsigned)n);
	nw_jit_invalidate_range_src(dpa, (uint32)n, NW_JIT_FL_HOST);
	ppc->gpr(8) = 0;	/* 68k remaining count; handler copies 0 */
	return 1;
}

/* The PA range guest_fetch hands to nw_aline_fastpath. A chained block must
 * not start inside it: a hop skips guest_fetch, so the trap would be neither
 * counted nor accelerated. */
static bool nw_aline_dispatch_pa(uint32 pa)
{
	extern uint32 ROMBase;
	return pa >= ROMBase + NW_EMU_ALINE_OS_FLAG &&
	       pa <= ROMBase + NW_EMU_ALINE_TOOL_AUTOPOP;
}

#if NW_BOOT_LOG
/* CPU-thread-owned trap history: no guest writes or repeated callbacks.
 * Keep only a bounded ring and emit it on SysError, so a diagnostic run
 * records the File Manager calls preceding failure without log flooding. */
struct nw_trap_record { uint32 trap, pc, d0, d1, a0, a1, a2, a6, sp; };
static nw_trap_record nw_trap_history[128];
static uint64 nw_trap_sequence;
#endif

uint32_t nw_trap_hist[65536];		// A-line trap counts, read by the NW_GUEST_PROF sampler
volatile int nw_trap_prof_on;		// set by the sampler (NW_GUEST_PROF=1/2)
/* (trap, caller 68k pc) counts: open-addressed table, single writer (the emulation thread), read racily by the sampler. */
enum { NW_TRAPSITE_N = 16384 };
uint64_t nw_trapsite_key[NW_TRAPSITE_N];
uint32_t nw_trapsite_cnt[NW_TRAPSITE_N];
static inline void nw_trapsite_add(uint32 trap, uint32 pc68)
{
	const uint64_t k = ((uint64_t)trap << 32) | pc68 | 1ull << 63;
	unsigned h = (unsigned)((k * 0x9E3779B97F4A7C15ull) >> 50);
	for (unsigned i = 0; i < 64; i++, h = (h + 1) & (NW_TRAPSITE_N - 1)) {
		if (nw_trapsite_key[h] == k) { nw_trapsite_cnt[h]++; return; }
		if (nw_trapsite_key[h] == 0) { nw_trapsite_key[h] = k; nw_trapsite_cnt[h] = 1; return; }
	}
}

/* NW_GUEST_PROF: name the file an Open/OpenRF/HFSDispatch call names, to see which file a slow boot step is loading. */
static void nw_trap_prof_name(powerpc_cpu *ppc, uint32 trap)
{
	const uint32 base = trap & 0xf0ffu;
	if (base != 0xa000u && base != 0xa00au && base != 0xa060u)
		return;
	const uint32 pb = ppc->gpr(16);
	extern uint32 RAMSize;
	if (pb < 0x1000u || pb >= RAMSize)
		return;
	const uint32 np = ReadMacInt32(pb + 18);
	if (np < 0x1000u || np >= RAMSize)
		return;
	const unsigned len = ReadMacInt8(np);
	if (len == 0 || len > 63)
		return;
	char name[64];
	for (unsigned i = 0; i < len; i++) {
		const uint8 c = (uint8)ReadMacInt8(np + 1 + i);
		name[i] = c >= 32 && c < 127 ? (char)c : '?';
	}
	name[len] = 0;
	static uint64_t n;
	struct timeval tv; gettimeofday(&tv, NULL);
	printf("TRAPNAME T=%lld.%03d #%llu %04x pc68=%08x d0=%08x name=\"%s\" dirid=%08x\n", (long long)tv.tv_sec, (int)(tv.tv_usec / 1000), (unsigned long long)++n, (unsigned)trap,
	       (unsigned)(ppc->gpr(24) - 2u), (unsigned)ppc->gpr(8), name, (unsigned)ReadMacInt32(pb + 48));
}

static void nw_aline_fastpath(powerpc_cpu *ppc, uint32 pa)
{
	extern uint32 ROMBase;
	if (pa < ROMBase + NW_EMU_ALINE_OS_FLAG ||
	    pa > ROMBase + NW_EMU_ALINE_TOOL_AUTOPOP)
		return;
	const int h = nw_emu_aline_handler(pa - ROMBase);
	if (h < 0 || vm_read_memory_4(pa) != NW_EMU_ALINE_ENTRY_OP)
		return;
	const uint32 trap = (ppc->gpr(29) >> 3) & 0xffffu;
	nw_event_aline(trap, ppc->gpr(24) - 2u, h);
	if (nw_trap_prof_on) {
		nw_trap_hist[trap & 0xffffu]++;
		nw_trapsite_add(trap & 0xffffu, ppc->gpr(24) - 2u);
		nw_trap_prof_name(ppc, trap);
	}
#if NW_BOOT_LOG
	static const bool keep_traps = getenv("NW_TEST_NK_STATE_PATH") != NULL;
	if (keep_traps) {
		nw_trap_record &r = nw_trap_history[nw_trap_sequence++ % 128];
		r = {trap, ppc->gpr(24) - 2u, ppc->gpr(8), ppc->gpr(9),
			ppc->gpr(16), ppc->gpr(17), ppc->gpr(18), ppc->gpr(22), ppc->gpr(1)};
	}
#endif
	if (trap == 0xa9c9) {
		static int n_syserr;
		if (n_syserr < 16) {
			n_syserr++;
			uint32 pc68 = ppc->gpr(24) - 2u;
			uint16 op = 0;
			uint8 *hp = vm_do_get_real_address(nw_la_to_pa(pc68));
			if (hp)
				op = (uint16)((hp[0] << 8) | hp[1]);
			printf("NW-BOOT SysError #%d 68k_pc=%08x opcode=%04x\n",
			       (int16)ppc->gpr(8), (unsigned)pc68, (unsigned)op);
			/* Where was the guest? The 68k registers and the top of its stack (an exception frame holds the faulting
			 * PC) make a Release-build crash log usable without the diagnostic dump below. */
			printf("NW-BOOT SysError regs d0=%08x d1=%08x a0=%08x a1=%08x a2=%08x a6=%08x sp=%08x stack:",
			       (unsigned)ppc->gpr(8), (unsigned)ppc->gpr(9), (unsigned)ppc->gpr(16), (unsigned)ppc->gpr(17),
			       (unsigned)ppc->gpr(18), (unsigned)ppc->gpr(22), (unsigned)ppc->gpr(1));
			for (unsigned w = 0; w < 12; ++w) {
				uint32 physical;
				if (ppc->guest_data_probe(ppc->gpr(1) + w * 4u, 4, false, &physical, NULL, false) &&
				    nw_pa_kind(physical) != NW_PA_IO && nw_pa_kind(physical) != NW_PA_NONE && nw_pa_kind(physical) != NW_PA_FB)
					printf(" %08x", (unsigned)vm_read_memory_4(physical));
				else
					printf(" --------");
			}
			printf("\n");
#if NW_BOOT_LOG
			const char *path = getenv("NW_TEST_NK_STATE_PATH");
			if (path && (int16)ppc->gpr(8) > 0) {
				char dump_path[1024];
				snprintf(dump_path, sizeof dump_path, "%s.syserr-%d", path, n_syserr);
				FILE *f = fopen(dump_path, "w");
				if (f) {
					nw68_nk_state state; ppc->nw_68k_snapshot(state);
					fprintf(f, "SysError=%d pc68=%08x ppc=%08x cr=%08x xer=%08x lr=%08x ctr=%08x\n",
					        (int16)ppc->gpr(8), pc68, state.ppc_pc, state.cr, state.xer, state.lr, state.ctr);
					for (unsigned r = 0; r < 32; ++r)
						fprintf(f, "r%u=%08x%c", r, ppc->gpr(r), r % 4 == 3 ? '\n' : ' ');
					const uint64 first_trap = nw_trap_sequence > 128 ? nw_trap_sequence - 128 : 0;
					for (uint64 j = first_trap; j < nw_trap_sequence; ++j) {
						const nw_trap_record &r = nw_trap_history[j % 128];
						fprintf(f, "trap #%llu op=%04x pc=%08x d0=%08x d1=%08x a0=%08x a1=%08x a2=%08x a6=%08x sp=%08x\n",
							(unsigned long long)j, r.trap, r.pc, r.d0, r.d1, r.a0, r.a1, r.a2, r.a6, r.sp);
					}
					const uint32 roots[] = {pc68, pc68 - 4096u, 0u, ppc->gpr(1), ppc->gpr(1) + 4096u,
						ppc->gpr(16), ppc->gpr(17), ppc->gpr(18), ppc->gpr(19), ppc->gpr(20), ppc->gpr(21), ppc->gpr(22)};
					for (unsigned r = 0; r < sizeof roots / sizeof roots[0]; ++r) {
						const uint32 base = roots[r] & ~4095u;
						bool duplicate = false;
						for (unsigned j = 0; j < r; ++j) duplicate |= (roots[j] & ~4095u) == base;
						if (duplicate) continue;
						for (unsigned offset = 0; offset < 4096; offset += 4) {
							if (!(offset & 31)) fprintf(f, "\n%08x:", base + offset);
							uint32 physical;
							if (ppc->guest_data_probe(base + offset, 4, false, &physical, NULL, false) &&
							    nw_pa_kind(physical) != NW_PA_IO && nw_pa_kind(physical) != NW_PA_NONE && nw_pa_kind(physical) != NW_PA_FB)
								fprintf(f, " %08x", vm_read_memory_4(physical));
							else fprintf(f, " --------");
						}
					}
					fputc('\n', f); fclose(f);
				}
			}
#endif
			fflush(stdout);
		}
	}
	if (trap == 0xaafeu) {
		nw_mixedmode_enter();
		const uint32 rd_la = ppc->gpr(24) - 2u;
		const uint32 rd_pa = nw_la_to_pa(rd_la);
		uint8 rd[32];
		int ok = 1;
		for (unsigned i = 0; i < 32; i++) {
			uint8 *hp = vm_do_get_real_address(rd_pa + i);
			if (!hp) {
				ok = 0;
				break;
			}
			rd[i] = *hp;
		}
		uint32 proc = 0;
		if (ok && nw_mixedmode_rd_ppc(rd, 32, &proc) && proc != 0)
			nw_mixedmode_note_ppc_rd(rd_la, proc);
	}
	if (trap == 0xa22eu && h == 1)
		nw_aline_blockmove(ppc);
}
#endif

bool powerpc_cpu::guest_fetch(uint32 *opcode)
{
	if (!ppc32_guest_mmu_enabled()) {
#ifdef SHEEPSHAVER
		last_fetch_pa_ = nw_la_to_pa(pc());
#endif
		*opcode = vm_read_memory_4(pc());
		return true;
	}
#ifdef SHEEPSHAVER
	{
		uint32_t ipa;
		if (nw_jit_itlb_lookup(pc(), &ipa)) {
			last_fetch_pa_ = ipa;
			const uint32 pa = ipa;
			extern uint32 ROMBase, RAMBase, RAMSize;
			const bool in_ram = pa >= RAMBase && pa < RAMBase + RAMSize;
			const bool in_rom = pa >= ROMBase && pa < ROMBase + 0x500000u;
			const bool in_low = pa < 0x4000u;
			const bool in_thunks = pa - nw_thunk_area_base < nw_thunk_area_size;
			if (!(in_ram || in_rom || in_low || in_thunks)) {
				dar_ = pa;
				take_exception(NW_VEC_MACHINE_CHECK, pc(), 0);
				return false;
			}
			nw_aline_fastpath(this, pa);
			*opcode = vm_read_memory_4(pa);
			return true;
		}
	}
#endif
	ppc32_xlate_result r = ppc32_guest_mmu().translate(pc(), PPC32_XLATE_IR, 4);
	if (!r.ok) {
		take_isi(r.fault);
		return false;
	}
#ifdef SHEEPSHAVER
	{
		nw_jit_itlb_fill(pc(), r.pa);
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
		nw_aline_fastpath(this, pa);
	}
#endif
	*opcode = vm_read_memory_4(r.pa);
	return true;
}

bool powerpc_cpu::guest_data_probe(uint32 ea, unsigned width, bool is_store, uint32 *pa,
				  int *via_bat, bool record_access)
{
	if (!ppc32_guest_mmu_enabled()) {
		*pa = ea;
		if (via_bat)
			*via_bat = 0;
		return true;
	}
	ppc32_xlate_result r = ppc32_guest_mmu().translate(ea, PPC32_XLATE_DR, width, is_store, record_access);
	if (r.ok) {
		*pa = r.pa;
		if (via_bat)
			*via_bat = r.via_bat ? 1 : 0;
		return true;
	}
	return false;
}

bool powerpc_cpu::guest_data_xlate(uint32 ea, unsigned width, bool is_store, uint32 *pa, bool observe_translation)
{
#ifdef SHEEPSHAVER
	if (nw_verify_trace_) nw_verify_ea_ = ea;
#endif
	if (!ppc32_guest_mmu_enabled()) {
		*pa = ea;
#ifdef SHEEPSHAVER
		if (nw_verify_trace_ && observe_translation)
			nw_verify_trace_->record_translation(pc(), ea, width, is_store, *pa);
#endif
		return true;
	}
	ppc32_xlate_result r = ppc32_guest_mmu().translate(ea, PPC32_XLATE_DR, width, is_store);
	if (r.ok) {
		*pa = r.pa;
#ifdef SHEEPSHAVER
		if (nw_verify_trace_ && observe_translation)
			nw_verify_trace_->record_translation(pc(), ea, width, is_store, *pa);
#endif
		return true;
	}
#ifdef SHEEPSHAVER
	if (nw_verify_trace_) {
		if (observe_translation) nw_verify_trace_->record_translation(pc(), ea, width, is_store, 0, NW_JIT_FAULT_DSI);
		else nw_verify_trace_->record(pc(), ea, width, is_store, 0, NW_JIT_FAULT_DSI);
	}
#endif
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
		nw_jit_dtlb_flush_src(NW_JIT_DTLB_FL_SDR1);
		return true;
	case powerpc_registers::SPR_SRR0:	srr0_ = value; return true;
	case powerpc_registers::SPR_SRR1:	srr1_ = value; return true;
	case powerpc_registers::SPR_DEC:
		sample_decrementer();
		/* Writing DEC with the MSB going 0->1 also raises the interrupt. */
		if ((dec_ & 0x80000000u) == 0 && (value & 0x80000000u))
			dec_pending_ = true;
		dec_ = value;
		dec_tb_base_ = tb_host_ticks();
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
		const uint32 ou = u, ol = l;
		if (spr & 1)
			l = value;
		else
			u = value;
		if (u == ou && l == ol)
			return true;
		mmu.set_ibat(i, u, l);
		/* IBAT is instruction translation; drop the fetch ITLB. */
		nw_jit_itlb_flush();
		return true;
	}
	if (spr >= powerpc_registers::SPR_DBAT0U && spr <= powerpc_registers::SPR_DBAT3L) {
		unsigned i = (spr - powerpc_registers::SPR_DBAT0U) / 2;
		uint32 u = 0, l = 0;
		mmu.get_dbat(i, &u, &l);
		const uint32 ou = u, ol = l;
		if (spr & 1)
			l = value;
		else
			u = value;
		if (u == ou && l == ol)
			return true;
		mmu.set_dbat(i, u, l);
		nw_jit_dtlb_drop_bat(ou, NW_JIT_DTLB_FL_BAT);
		if (u != ou)
			nw_jit_dtlb_drop_bat(u, NW_JIT_DTLB_FL_BAT);
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
	while ((((uintptr)ptr) % ALIGN) != 0) {
		ofs++;
		ptr++;
	}

	// Insert signature and offset
	struct aligned_block_t {
		uint32 pad[(ALIGN - 8) / 4];
		uint32 signature;
		uint32 offset;
		uint8  data[sizeof(powerpc_cpu)];
	};
	aligned_block_t *blk = (aligned_block_t *)ptr;
	blk->signature = 0x53435055;		/* 'SCPU' */
	blk->offset = (uint32)(ofs + (&blk->data[0] - (uint8 *)blk));
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
#ifdef SHEEPSHAVER
	nw_jc_ = 0;
	nw_verify_trace_ = NULL;
	nw_verify_ea_ = 0;
	mm_ppc_pending_ = 0;
#endif
	initialize();
}

#ifdef SHEEPSHAVER
void powerpc_cpu::nw_invoke_mm_ppc(uint32 entry)
{
	(void)entry;
}
#endif

powerpc_cpu::~powerpc_cpu()
{
#ifdef SHEEPSHAVER
	kcall_hist_dump("exit");
	free(nw_jc_);
	nw_jc_ = 0;
#endif
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
#if defined(SHEEPSHAVER) && NW_BOOT_LOG
		if (execute_depth <= 1) {
			NW_DIAG("NW-BOOT CPU outer execution-return pc=%08x r24=%08x r29=%08x r1=%08x cr=%08x mode=%u\n",
			       pc(), gpr(24), gpr(29), gpr(1), cr().get(), ReadMacInt32(XLM_RUN_MODE));
			fflush(stdout);
		}
#endif
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
/* 68k emu polls VIA/PMU/SCC/PCI. skip_io used to punt those loads to kpx
 * (one insn/block). Do the access here so the JIT can keep the loop. */
static uint32 jit_io_load(uint32 pa, int size, uint32 pc, int *fault)
{
	*fault = 0;
	return nw_io_read(pa, size, pc);
}
static void jit_io_store(uint32 pa, int size, uint32 val, uint32 pc, int *fault)
{
	*fault = 0;
	nw_io_write(pa, size, val, pc);
}

uint32 powerpc_cpu::jit_host_msr(void *host)
{
	(void)host;
	return ppc32_guest_mmu().msr();
}

uint32 powerpc_cpu::jit_host_lwz(void *host, uint32 ea, uint32 pc, int *fault)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	uint32 pa;
	int via_bat = 0;
	/* Copy-out is gated. Shadow runs must not take a DSI, double-hit I/O,
	 * or vm_write an unmapped PA (no SIGSEGV recovery from JIT code). */
	if (!ppc->guest_data_probe(ea, 4, false, &pa, &via_bat)) {
		*fault = 1;
		return 0;
	}
	const int kind = nw_pa_kind(pa);
	if (kind == NW_PA_IO)
		return jit_io_load(pa, 4, pc, fault);
	if (kind == NW_PA_NONE) {
		*fault = 1;
		return 0;
	}
	if (kind != NW_PA_FB && ppc32_guest_mmu_enabled() &&
	    (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_DR)) {
		const int pr = (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_PR) != 0;
		nw_jit_dtlb_fill(ea, pa, nw_pa_writable(pa) && kind != NW_PA_ROM,
			(uint64_t)(uintptr_t)vm_do_get_real_address(pa & ~0xfffu),
			pr, via_bat);
	}
	return vm_read_memory_4(pa);
}

void powerpc_cpu::jit_host_stw(void *host, uint32 ea, uint32 val, uint32 pc, int *fault)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	uint32 pa;
	int via_bat = 0;
	if (!ppc->guest_data_probe(ea, 4, true, &pa, &via_bat)) {
		*fault = 1;
		return;
	}
	const int kind = nw_pa_kind(pa);
	if (kind == NW_PA_IO) {
		jit_io_store(pa, 4, val, pc, fault);
		return;
	}
	if (kind == NW_PA_ROM)
		return;
	if (!nw_pa_writable(pa)) {
		*fault = 1;
		return;
	}
	vm_write_memory_4(pa, val);
	if (kind == NW_PA_FB) {
		uint32_t nbytes = 0;
		const uint32_t base = nw_fb_phys(&nbytes);
		uint8 *host_base = base ? vm_do_get_real_address(base) : NULL;
		if (host_base && nbytes)
			nw_fb_bind_host(host_base, nbytes);
		nw_fb_note_host(vm_do_get_real_address(pa));
	}
	if (ppc32_guest_mmu_enabled() &&
	    (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_DR)) {
		uint8 *hostp = vm_do_get_real_address(pa & ~0xfffu);
		nw_jit_dtlb_fill(ea, pa, 1, (uint64_t)(uintptr_t)hostp,
			(ppc32_guest_mmu().msr() & ppc32_mmu::MSR_PR) != 0, via_bat);
	}
	if (kind != NW_PA_FB)
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
	if (kind == NW_PA_IO)
		return jit_io_load(pa, 4, pc, fault);
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
		jit_io_store(pa, 4, val, pc, fault);
		return;
	}
	if (kind == NW_PA_ROM)
		return;
	if (!nw_pa_writable(pa)) {
		*fault = 1;
		return;
	}
	vm_write_memory_4(pa, val);
	if (kind == NW_PA_FB)
		nw_fb_damage_store(pa, 4);
	if (kind != NW_PA_FB)
		nw_jit_invalidate_page_src(pa, NW_JIT_FL_STORE);
	if ((pa & ~0xfffu) == (ppc->last_fetch_pa_ & ~0xfffu))
		*fault = NW_JIT_FAULT_SMC;
}

void powerpc_cpu::jit_host_system(void *host, nw_jit_cpu *c, uint32 op,
                                  uint32 a, uint32 b, nw_jit_system_result *r)
{
    powerpc_cpu *ppc = static_cast<powerpc_cpu *>(host);
    ppc->pc() = c->pc;
    const unsigned p = op >> 26, x = (op >> 1) & 1023;
    const bool user = ppc32_guest_mmu().msr() & ppc32_mmu::MSR_PR;
    *r = {0, 0, NW_SYS_OK};
    if (p == 31 && (x == 339 || x == 371 || x == 467)) {
        uint32 value = 0;
        const spr_access_result status = x == 467 ? ppc->mtspr_guest(a, b) : ppc->mfspr_guest(a, &value);
        r->status = status == SPR_ACCESS_EXC ? NW_SYS_PRIV : status == SPR_ACCESS_NOP ? NW_SYS_NOP : NW_SYS_OK;
        r->value = value; return;
    }
    if (user && ((p == 19 && x == 50) || (p == 31 &&
        (x == 83 || x == 146 || x == 210 || x == 242 || x == 595 || x == 659 ||
         x == 306 || x == 370 || x == 566 || x == 470)))) {
        ppc->take_program(0x40000); r->status = NW_SYS_PRIV; return;
    }
    if (p == 3 || (p == 31 && x == 4)) {
        const unsigned to = (op >> 21) & 31;
        const bool trap = ((to & 16) && int32(a) < int32(b)) || ((to & 8) && int32(a) > int32(b)) ||
                          ((to & 4) && a == b) || ((to & 2) && a < b) || ((to & 1) && a > b);
        if (trap) { jit_host_trap(host, c); r->status = NW_SYS_TRAP; } return;
    }
    if (p == 17) { ppc->take_sc(); r->status = NW_SYS_SC; return; }
    if (p == 19 && x == 50) {
        // Lazy FP retry is part of this CPU's system service, sampled once.
        jit_host_rfi(host, c); r->value = ppc->srr0_; r->aux = ppc->srr1_;
        c->pc &= ~3u; return;
    }
    if (p == 19 && x == 150) { ppc->execute_invalidate_cache_range(); return; }
    if (p != 31) return;
    switch (x) {
    case 83: r->value = ppc32_guest_mmu().msr(); return;
    case 146: jit_host_mtmsr(host, a); return;
    case 595: case 659: r->value = ppc32_guest_mmu().sr(a); return;
    case 210: case 242: jit_host_mtsr(host, a, b); return;
    case 306: jit_host_tlbie(host, a); return;
    case 370: jit_host_tlbia(host); return;
    case 982: {
        const uint32 base = a & ~31u;
        if (base == ppc->cache_range.end) ppc->cache_range.end += 32;
        else { ppc->execute_invalidate_cache_range(); ppc->cache_range.start = base; ppc->cache_range.end = base + 32; }
        return;
    }
    case 1014: {
        uint32 pa;
        if (!ppc->guest_data_probe(a, 32, true, &pa)) { r->status = NW_SYS_DSI; return; }
        if (nw_pa_kind(pa) != NW_PA_IO && nw_pa_kind(pa) != NW_PA_ROM) {
            const uint32 base = pa & ~31u;
            vm_memset(base, 0, 32); nw_fb_damage_store(base, 32);
            nw_jit_invalidate_page_src(base, NW_JIT_FL_STORE);
            ppc->invalidate_cache();
            if ((base & ~0xfffu) == (ppc->last_fetch_pa_ & ~0xfffu)) r->status = NW_SYS_SMC;
        }
        return;
    }
    default: return; // no data-cache emulation; sync ordering is native-owned
    }
}

uint32 powerpc_cpu::jit_host_mfspr(void *host, uint32 spr, uint32 guest_pc, int *status)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	if (status)
		*status = 0;
    ppc->pc() = guest_pc;
    uint32 value = 0;
    const spr_access_result result = ppc->mfspr_guest(spr, &value);
    if (status) *status = result == SPR_ACCESS_EXC ? 2 : result == SPR_ACCESS_NOP ? 1 : 0;
    return result == SPR_ACCESS_EXC ? ppc->pc() : value;
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
		const uint32 old = ppc32_guest_mmu().msr();
		ppc32_guest_mmu().set_msr(msr);
#ifdef SHEEPSHAVER
		nw_log_msr_dr(msr);
		nw_log_msr_write("mtmsr", ppc->pc(), msr);
#endif
		/* IR|DR only. Do not use flush_if_pr: rfi shares that and CHK'd QT. */
		if ((old ^ msr) & 0x00000030u)
			nw_jit_dtlb_flush_src(NW_JIT_DTLB_FL_MTMSR);
		nw_jit_itlb_note_msr(old, msr);
	}
	(void)ppc;
}

void powerpc_cpu::jit_host_mtsr(void *host, uint32 sr, uint32 val)
{
	(void)host;
	if (!ppc32_guest_mmu_enabled())
		return;
	ppc32_mmu &mmu = ppc32_guest_mmu();
	const unsigned i = sr & 0xfu;
	const uint32 old = mmu.sr(i);
	if (old == val)
		return;
	mmu.set_sr(i, val);
	nw_jit_mtsr_note(i, old, val);
}

uint32 powerpc_cpu::jit_host_mfsr(void *host, uint32 sr)
{
	(void)host;
	if (!ppc32_guest_mmu_enabled())
		return 0;
	return ppc32_guest_mmu().sr(sr & 0xfu);
}

void powerpc_cpu::jit_host_trap(void *host, struct nw_jit_cpu *cpu)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	if (!ppc || !cpu)
		return;
	ppc->pc() = cpu->pc;
	note_tw_emu(cpu->pc);
	const uint32 crv = cpu->cr;
	const uint32 ctrv = cpu->ctr;
	ppc->take_program(0x00020000u);
	ppc->cr().set(crv);
	ppc->ctr() = ctrv;
}

void powerpc_cpu::jit_host_fp_exception(void *host, struct nw_jit_cpu *cpu)
{
    powerpc_cpu *ppc = static_cast<powerpc_cpu *>(host);
    ppc->pc() = cpu->pc;
    ppc->fpscr() = cpu->fpscr;
    ppc->take_program(0x00100000u);
}

void powerpc_cpu::jit_host_sc(void *host, uint32 guest_pc)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	ppc->pc() = guest_pc;
	ppc->take_sc();
}

uint32 powerpc_cpu::jit_host_dec(void *host, uint32 val, uint32 guest_pc, int write, int *status)
{
    powerpc_cpu *ppc = (powerpc_cpu *)host;
    ppc->pc() = guest_pc;
    uint32 value = ppc->dec_;
    const spr_access_result result = write ? ppc->mtspr_guest(powerpc_registers::SPR_DEC, val)
                                         : ppc->mfspr_guest(powerpc_registers::SPR_DEC, &value);
    *status = result == SPR_ACCESS_EXC ? 2 : 0;
    return *status == 2 ? ppc->pc() : ppc->dec_;
}

// Both native tail calls and C successors stop at the same event boundary.
// The decrementer is derived from the host clock and this runs at every chain exit and hop, so
// reading the clock each time was 5.8% of the emulation thread in a host profile of an iTunes
// window raise. Sampling every kJitDecSamplePeriod calls only delays noticing the expiry edge by a
// few block exits (the interpreter samples every 256 instructions); sample_decrementer() adds the
// full elapsed delta, and every guest read of DEC samples it fresh, so values stay exact.
static const unsigned kJitDecSamplePeriod = 16;

bool powerpc_cpu::jit_events_pending()
{
    if ((nw_jit_legacy & NW_JIT_LEGACY_CLOCK) || (!dec_pending_ && ++jit_dec_div_ >= kJitDecSamplePeriod)) {
        jit_dec_div_ = 0;
        sample_decrementer();
    }
    return !nw_68k_can_run() || (async_exception_pending() &&
                               (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_EE));
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
	/* The aligned 16-byte access has been probed as a whole. Complete it
	 * before reporting SMC; four scalar callbacks could exit after word 0
	 * and leave the rest of the architectural vector store unwritten. */
	for (unsigned i = 0; i < 4; ++i)
		vm_write_memory_4(pa + i * 4u, w[i]);
	if (kind == NW_PA_FB)
		nw_fb_damage_store(pa, 16);
	else
		nw_jit_invalidate_page_src(pa, NW_JIT_FL_STORE);
	if ((pa & ~0xfffu) == (ppc->last_fetch_pa_ & ~0xfffu))
		*fault = NW_JIT_FAULT_SMC;
}

void powerpc_cpu::jit_host_vmx(void *host, uint32 op, struct nw_jit_cpu *cpu)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	if (!ppc || !cpu)
		return;
	for (int i = 0; i < 32; i++)
		ppc->gpr(i) = cpu->gpr[i];
	ppc->cr().set(cpu->cr);
	ppc->xer().set(cpu->xer);
	ppc->vscr().set(cpu->vscr);
	for (int i = 0; i < 32; i++) {
		ppc->vr(i).w[0] = cpu->vr[i][0];
		ppc->vr(i).w[1] = cpu->vr[i][1];
		ppc->vr(i).w[2] = cpu->vr[i][2];
		ppc->vr(i).w[3] = cpu->vr[i][3];
	}
	const uint32 saved = ppc->pc();
	const instr_info_t *ii = ppc->decode(op);
	if (ii)
		ii->execute(ppc, op);
	ppc->pc() = saved;
	cpu->cr = ppc->cr().get();
	cpu->xer = ppc->xer().get();
	cpu->vscr = ppc->vscr().get();
	for (int i = 0; i < 32; i++)
		cpu->gpr[i] = ppc->gpr(i);
	for (int i = 0; i < 32; i++) {
		cpu->vr[i][0] = ppc->vr(i).w[0];
		cpu->vr[i][1] = ppc->vr(i).w[1];
		cpu->vr[i][2] = ppc->vr(i).w[2];
		cpu->vr[i][3] = ppc->vr(i).w[3];
	}
}

void powerpc_cpu::jit_host_rfi(void *host, struct nw_jit_cpu *cpu)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	if (!ppc || !cpu)
		return;
	if (ppc32_guest_mmu_enabled()) {
		ppc->finish_fpu_rfi();
		const uint32 old = ppc32_guest_mmu().msr();
		ppc32_guest_mmu().set_msr(ppc->srr1_);
		nw_log_msr_dr(ppc->srr1_);
		nw_log_msr_write("rfi", ppc->srr0_, ppc->srr1_);
		nw_log_emu_rfi(ppc->srr0_, ppc->srr1_, cpu->cr, ppc->sprg(0));
#if NW_BOOT_LOG
		if (ppc->srr0_ == 0x680b07f0u || ppc->srr0_ == 0x680b07f4u) {
			static unsigned nh;
			if (nh < 6u) {
				nh++;
				NW_DIAG("NW-BOOT G1: halt-rfi pc=%08x cpu_pc=%08x lr=%08x r13=%08x r28=%08x r31=%08x cr=%08x\n",
				       (unsigned)ppc->pc(), (unsigned)cpu->pc, (unsigned)cpu->lr,
				       cpu->gpr[13], cpu->gpr[28], cpu->gpr[31], cpu->cr);
				fflush(stdout);
				if ((cpu->cr & 0x00800000u) == 0)
					nw_jit_dump_wake_ring();
			}
		}
#endif
		nw_jit_dtlb_flush_if_pr(old, ppc->srr1_, NW_JIT_DTLB_FL_RFI);
		nw_jit_itlb_note_msr(old, ppc->srr1_);
		cpu->pc = ppc->srr0_ & ~3u;
		cpu->msr = ppc->srr1_;
		return;
	}
	cpu->pc += 4;
}

/* Commit and pull run at every chain exit. Only a few registers are usually live, so visit the set bits
 * instead of testing all 32 (the same loads and stores, in the same ascending order). */
static void nw_commit_gpr(struct nw_jit_cpu *jc, powerpc_cpu *ppc)
{
	const uint32_t mask = jc->gpr_live;
	if (mask == 0xffffffffu) {
		for (int i = 0; i < 32; i++)
			ppc->gpr(i) = jc->gpr[i];
		return;
	}
	if (nw_jit_legacy & NW_JIT_LEGACY_SYNC) {
		for (int i = 0; i < 32; i++)
			if (mask & (1u << i))
				ppc->gpr(i) = jc->gpr[i];
		return;
	}
	for (uint32_t m = mask; m; m &= m - 1)
		ppc->gpr(__builtin_ctz(m)) = jc->gpr[__builtin_ctz(m)];
}
static void nw_pull_gpr(struct nw_jit_cpu *jc, powerpc_cpu *ppc, uint32_t mask)
{
	if (mask == 0)
		return;
	if (jc->gpr_live == 0xffffffffu)
		return;
	if (nw_jit_legacy & NW_JIT_LEGACY_SYNC) {
		if (mask == 0xffffffffu) {
			for (int i = 0; i < 32; i++)
				if ((jc->gpr_live & (1u << i)) == 0)
					jc->gpr[i] = ppc->gpr(i);
			jc->gpr_live = 0xffffffffu;
			return;
		}
		for (int i = 0; i < 32; i++) {
			const uint32_t b = 1u << i;
			if ((mask & b) && (jc->gpr_live & b) == 0)
				jc->gpr[i] = ppc->gpr(i);
		}
		jc->gpr_live |= mask;
		return;
	}
	/* Registers the successor reads that are not already live in jc. */
	for (uint32_t m = mask & ~jc->gpr_live; m; m &= m - 1)
		jc->gpr[__builtin_ctz(m)] = ppc->gpr(__builtin_ctz(m));
	jc->gpr_live |= mask;
}

static void nw_commit_fpr(struct nw_jit_cpu *jc, powerpc_cpu *ppc, uint32_t &fpscr)
{
	const uint32_t mask = jc->fpr_live;
	if (mask == 0)
		return;
	if (mask == 0xffffffffu) {
		for (int i = 0; i < 32; i++)
			ppc->fpr_dw(i) = jc->fpr[i];
	} else if (nw_jit_legacy & NW_JIT_LEGACY_SYNC) {
		for (int i = 0; i < 32; i++)
			if (mask & (1u << i))
				ppc->fpr_dw(i) = jc->fpr[i];
	} else {
		for (uint32_t m = mask; m; m &= m - 1)
			ppc->fpr_dw(__builtin_ctz(m)) = jc->fpr[__builtin_ctz(m)];
	}
	fpscr = jc->fpscr;
}

static void nw_pull_fpr(struct nw_jit_cpu *jc, powerpc_cpu *ppc, uint32_t mask,
			uint32_t fpscr)
{
	if (mask == 0 || jc->fpr_live == 0xffffffffu)
		return;
	const int had = jc->fpr_live != 0;
	if (mask == 0xffffffffu) {
		for (int i = 0; i < 32; i++)
			if ((jc->fpr_live & (1u << i)) == 0)
				jc->fpr[i] = ppc->fpr_dw(i);
		jc->fpr_live = 0xffffffffu;
	} else if (nw_jit_legacy & NW_JIT_LEGACY_SYNC) {
		for (int i = 0; i < 32; i++) {
			const uint32_t b = 1u << i;
			if ((mask & b) && (jc->fpr_live & b) == 0)
				jc->fpr[i] = ppc->fpr_dw(i);
		}
		jc->fpr_live |= mask;
	} else {
		for (uint32_t m = mask & ~jc->fpr_live; m; m &= m - 1)
			jc->fpr[__builtin_ctz(m)] = ppc->fpr_dw(__builtin_ctz(m));
		jc->fpr_live |= mask;
	}
	if (!had)
		jc->fpscr = fpscr;
}

void *powerpc_cpu::jit_host_chain(void *host, struct nw_jit_cpu *cpu,
				  uint32 chain_pc, int *n2,
				  int *uses_fpr, int *uses_vr,
				  uint32 *dsi_pc, uint32 *chain2,
				  int cur_fpr, int cur_vr)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	if (!ppc || !cpu || !ppc32_guest_mmu_enabled())
		return NULL;
	if (cpu->pc != chain_pc || cpu->fault)
		return NULL;
	if (nw_68k_wants_boundary(cpu->pc)) return NULL;
	if (ppc->jit_events_pending())
		return NULL;
	const uint32 hmsr = ppc32_guest_mmu().msr();
	const uint32 hmsr_ir = ((hmsr & ppc32_mmu::MSR_IR) ? 1u : 0u) |
			       ((hmsr & ppc32_mmu::MSR_DR) ? 2u : 0u) |
			       ((hmsr & ppc32_mmu::MSR_PR) ? 4u : 0u);
	uint32_t npa = 0;
	if (!nw_jit_itlb_lookup(cpu->pc, &npa))
		return NULL;
	if (nw_aline_dispatch_pa(npa))
		return NULL;
	int nn = 0, f2 = 0, v2 = 0;
	uint32_t ch2 = 0, sg = 0, sf = 0;
	nw_jit_fn next = nw_jit_cache_get(npa & ~0xfffu, cpu->pc, hmsr_ir, 0,
					  &nn, &f2, &v2, &ch2, &sg, NULL, &sf);
	if (!next || next == NW_JIT_INTERPRET ||
	    nn <= 0 || nn > NW_JIT_MAX_BLOCK)
		return NULL;
	if (v2 && !(hmsr & NW_MSR_VEC))
		return NULL;
	if (f2 && !(hmsr & ppc32_mmu::MSR_FP))
		return NULL;
	nw_68k_reference_chain(cpu->pc,
		(cpu->gpr_live & (1u << 24) ? cpu->gpr[24] : ppc->gpr(24)) - 2u,
		cpu->gpr_live & (1u << 29) ? cpu->gpr[29] : ppc->gpr(29));
	nw_commit_gpr(cpu, ppc);
	nw_pull_gpr(cpu, ppc, sg);
	ppc->cr().set(cpu->cr);
	ppc->xer().set(cpu->xer);
	ppc->lr() = cpu->lr;
	ppc->ctr() = cpu->ctr;
	if (cpu->dec_wr) {
		if ((ppc->dec_ & 0x80000000u) == 0 && (cpu->dec & 0x80000000u))
			ppc->dec_pending_ = true;
		ppc->dec_ = cpu->dec;
		ppc->dec_tb_base_ = ppc->tb_host_ticks();
		cpu->dec_wr = 0;
		cpu->dec = ppc->dec_;
	}
	ppc->pc() = cpu->pc;
	/* Same marshalling as the C hop loop. Copy-out live jc FP/VR so a
	 * VMX/FP tail does not leave ppc stale; copy-in only when this
	 * block did not already have that class in jc. Copy-in on a live
	 * VMX block overwrote glyph VRs with pre-block ppc and smeared
	 * Finder/menu text. */
	if (cur_fpr)
		nw_commit_fpr(cpu, ppc, ppc->fpscr());
	if (cur_vr) {
		for (int i = 0; i < 32; i++) {
			ppc->vr(i).w[0] = cpu->vr[i][0];
			ppc->vr(i).w[1] = cpu->vr[i][1];
			ppc->vr(i).w[2] = cpu->vr[i][2];
			ppc->vr(i).w[3] = cpu->vr[i][3];
		}
		ppc->vscr().set(cpu->vscr);
	}
	if (f2)
		nw_pull_fpr(cpu, ppc, sf ? sf : 0xffffffffu, ppc->fpscr());
	if (v2 && !cur_vr) {
		for (int i = 0; i < 32; i++) {
			cpu->vr[i][0] = ppc->vr(i).w[0];
			cpu->vr[i][1] = ppc->vr(i).w[1];
			cpu->vr[i][2] = ppc->vr(i).w[2];
			cpu->vr[i][3] = ppc->vr(i).w[3];
		}
		cpu->vscr = ppc->vscr().get();
	}
	ppc->last_fetch_pa_ = npa;
	cpu->msr = hmsr;
	if (n2)
		*n2 = nn;
	if (uses_fpr)
		*uses_fpr = f2;
	if (uses_vr)
		*uses_vr = v2;
	if (dsi_pc)
		*dsi_pc = cpu->pc;
	if (chain2)
		*chain2 = ch2;
	return (void *)next;
}

void powerpc_cpu::jit_host_icbi(void *host, uint32 ea)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	if (!ppc)
		return;
	uint32 pa;
	if (ppc->guest_data_probe(ea, 4, false, &pa))
		nw_jit_invalidate_page_src(pa, NW_JIT_FL_ICBI);
}

void powerpc_cpu::jit_host_tlbie(void *host, uint32 ea)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	if (!ppc)
		return;
	if (ppc32_guest_mmu_enabled()) {
		ppc32_guest_mmu().tlbie(ea);
		nw_jit_dtlb_drop_page(ea, NW_JIT_DTLB_FL_TLB);
		ppc->invalidate_cache();
	}
}

void powerpc_cpu::jit_host_tlbia(void *host)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	if (!ppc)
		return;
	if (ppc32_guest_mmu_enabled()) {
		ppc32_guest_mmu().tlbia();
		ppc->invalidate_cache();
	}
}

uint32 powerpc_cpu::jit_host_lwarx(void *host, uint32 ea, uint32 pc, int *fault)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	uint32 pa;
	if (!ppc->guest_data_probe(ea, 4, false, &pa)) {
		*fault = NW_JIT_FAULT_DSI;
		return 0;
	}
	const int kind = nw_pa_kind(pa);
	if (kind == NW_PA_NONE) { *fault = NW_JIT_FAULT_DSI; return 0; }
	const uint32 value = kind == NW_PA_IO ? jit_io_load(pa, 4, pc, fault) : vm_read_memory_4(pa);
	if (*fault) return 0;
	ppc->regs().reserve_valid = 1;
	ppc->regs().reserve_addr = pa;
#if KPX_MAX_CPUS != 1
	ppc->regs().reserve_data = value;
#endif
	return value;
}

int powerpc_cpu::jit_host_stwcx(void *host, uint32 ea, uint32 val, uint32 pc, int *fault)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	uint32 pa;
	if (!ppc->guest_data_probe(ea, 4, true, &pa)) {
		*fault = NW_JIT_FAULT_DSI;
		return 0;
	}
	const int kind = nw_pa_kind(pa);
	if (kind == NW_PA_NONE || (kind != NW_PA_IO && kind != NW_PA_ROM && !nw_pa_writable(pa))) {
		*fault = NW_JIT_FAULT_DSI;
		return 0;
	}
	int eq = 0;
	if (ppc->regs().reserve_valid && ppc->regs().reserve_addr == pa
#if KPX_MAX_CPUS != 1
	    && ppc->regs().reserve_data == (kind == NW_PA_IO ? jit_io_load(pa, 4, pc, fault) : vm_read_memory_4(pa))
#endif
	    ) {
		if (kind == NW_PA_IO) jit_io_store(pa, 4, val, pc, fault);
		else if (kind != NW_PA_ROM) {
			vm_write_memory_4(pa, val);
			if (kind == NW_PA_FB) nw_fb_damage_store(pa, 4);
			else nw_jit_invalidate_page_src(pa, NW_JIT_FL_STORE);
			if ((pa & ~0xfffu) == (ppc->last_fetch_pa_ & ~0xfffu)) *fault = NW_JIT_FAULT_SMC;
		}
		// ROM writes are ignored, as for KPX's ordinary stores. A failed
		// transaction preserves the reservation and CR; completed SMC does not.
		if (*fault && *fault != NW_JIT_FAULT_SMC) return 0;
		eq = 1;
	}
	ppc->regs().reserve_valid = 0;
	return eq;
}

void powerpc_cpu::jit_host_lfd(void *host, uint32 fd, uint32 ea, uint32 pc, int *fault, uint64 *out)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	(void)pc;
	uint32 pa;
	int via_bat = 0;
	if (!ppc->guest_data_probe(ea, 8, false, &pa, &via_bat)) {
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
	if (kind != NW_PA_FB && ppc32_guest_mmu_enabled() &&
	    (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_DR))
		nw_jit_dtlb_fill(ea, pa, nw_pa_writable(pa) && kind != NW_PA_ROM,
			(uint64_t)(uintptr_t)vm_do_get_real_address(pa & ~0xfffu),
			(ppc32_guest_mmu().msr() & ppc32_mmu::MSR_PR) != 0, via_bat);
	const uint64 v = vm_read_memory_8(pa);
	ppc->fpr_dw((int)fd) = v;
	if (out)
		*out = v;
}

void powerpc_cpu::jit_host_stfd(void *host, uint32 ea, uint64 val, uint32 pc, int *fault)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	(void)pc;
	uint32 pa;
	int via_bat = 0;
	if (!ppc->guest_data_probe(ea, 8, true, &pa, &via_bat)) {
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
	vm_write_memory_8(pa, val);
	if (kind == NW_PA_FB)
		nw_fb_damage_store(pa, 8);
	else if (ppc32_guest_mmu_enabled() &&
	    (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_DR))
		nw_jit_dtlb_fill(ea, pa, 1,
			(uint64_t)(uintptr_t)vm_do_get_real_address(pa & ~0xfffu),
			(ppc32_guest_mmu().msr() & ppc32_mmu::MSR_PR) != 0, via_bat);
	if (kind != NW_PA_FB)
		nw_jit_invalidate_page_src(pa, NW_JIT_FL_STORE);
	if ((pa & ~0xfffu) == (ppc->last_fetch_pa_ & ~0xfffu))
		*fault = NW_JIT_FAULT_SMC;
}

uint32 powerpc_cpu::jit_host_lh(void *host, uint32 ea, uint32 pc, int *fault)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	uint32 pa;
	int via_bat = 0;
	(void)pc;
	if (!ppc->guest_data_probe(ea, 2, false, &pa, &via_bat)) {
		*fault = 1;
		return 0;
	}
	const int kind = nw_pa_kind(pa);
	if (kind == NW_PA_IO)
		return jit_io_load(pa, 2, pc, fault);
	if (kind == NW_PA_NONE) {
		*fault = 1;
		return 0;
	}
	/* Same refill as jit_host_lwz: later byte/halfword loads from this page hit the host-line table in
	 * nw_jit_helper_lh instead of translating again (translate was 13.7% of the emulation thread in a profile). */
	if (!(nw_jit_legacy & NW_JIT_LEGACY_LOAD) && kind != NW_PA_FB && ppc32_guest_mmu_enabled() &&
	    (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_DR)) {
		const int pr = (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_PR) != 0;
		/* Read-only: this access only proved the page readable. Marking it writable (as the word-load fill does)
		 * would let a later store skip the guest's write-protection check and the page's changed bit. */
		nw_jit_dtlb_fill(ea, pa, 0,
			(uint64_t)(uintptr_t)vm_do_get_real_address(pa & ~0xfffu),
			pr, via_bat);
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
		jit_io_store(pa, 2, val, pc, fault);
		return;
	}
	if (kind == NW_PA_ROM)
		return;
	if (!nw_pa_writable(pa)) {
		*fault = 1;
		return;
	}
	vm_write_memory_2(pa, val);
	if (kind == NW_PA_FB)
		nw_fb_damage_store(pa, 2);
	if (kind != NW_PA_FB)
		nw_jit_invalidate_page_src(pa, NW_JIT_FL_STORE);
	if ((pa & ~0xfffu) == (ppc->last_fetch_pa_ & ~0xfffu))
		*fault = NW_JIT_FAULT_SMC;
}

uint32 powerpc_cpu::jit_host_lb(void *host, uint32 ea, uint32 pc, int *fault)
{
	powerpc_cpu *ppc = (powerpc_cpu *)host;
	uint32 pa;
	int via_bat = 0;
	(void)pc;
	if (!ppc->guest_data_probe(ea, 1, false, &pa, &via_bat)) {
		*fault = 1;
		return 0;
	}
	const int kind = nw_pa_kind(pa);
	if (kind == NW_PA_IO)
		return jit_io_load(pa, 1, pc, fault);
	if (kind == NW_PA_NONE) {
		*fault = 1;
		return 0;
	}
	/* Same refill as jit_host_lwz: later byte/halfword loads from this page hit the host-line table in
	 * nw_jit_helper_lb instead of translating again (translate was 13.7% of the emulation thread in a profile). */
	if (!(nw_jit_legacy & NW_JIT_LEGACY_LOAD) && kind != NW_PA_FB && ppc32_guest_mmu_enabled() &&
	    (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_DR)) {
		const int pr = (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_PR) != 0;
		/* Read-only: this access only proved the page readable. Marking it writable (as the word-load fill does)
		 * would let a later store skip the guest's write-protection check and the page's changed bit. */
		nw_jit_dtlb_fill(ea, pa, 0,
			(uint64_t)(uintptr_t)vm_do_get_real_address(pa & ~0xfffu),
			pr, via_bat);
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
		jit_io_store(pa, 1, val, pc, fault);
		return;
	}
	if (kind == NW_PA_ROM)
		return;
	if (!nw_pa_writable(pa)) {
		*fault = 1;
		return;
	}
	vm_write_memory_1(pa, val);
	if (kind == NW_PA_FB)
		nw_fb_damage_store(pa, 1);
	if (kind != NW_PA_FB)
		nw_jit_invalidate_page_src(pa, NW_JIT_FL_STORE);
	if ((pa & ~0xfffu) == (ppc->last_fetch_pa_ & ~0xfffu))
		*fault = NW_JIT_FAULT_SMC;
}

static int nw_jit_pa_ok(uint32 pa, int is_st)
{
	const int kind = nw_pa_kind(pa);
	if (kind == NW_PA_NONE)
		return 0;
	if (kind == NW_PA_IO)
		return 2;	/* pack this op, then end the block */
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
	else if (prim == 31 && (((op >> 1) & 0x3ff) == 23 ||
				 ((op >> 1) & 0x3ff) == 55 ||
				 ((op >> 1) & 0x3ff) == 20)) {
		const int ra = (int)((op >> 16) & 0x1f);
		const int rb = (int)((op >> 11) & 0x1f);
		const uint32 ea = (ra ? sg[ra] : 0) + sg[rb];
		uint32 pa;
		if (!ppc->guest_data_probe(ea, 4, false, &pa))
			return 0;
		return nw_jit_pa_ok(pa, 0);
	}
	else if (prim == 31 && (((op >> 1) & 0x3ff) == 87 ||
				 ((op >> 1) & 0x3ff) == 119)) {
		const int ra = (int)((op >> 16) & 0x1f);
		const int rb = (int)((op >> 11) & 0x1f);
		const uint32 ea = (ra ? sg[ra] : 0) + sg[rb];
		uint32 pa;
		if (!ppc->guest_data_probe(ea, 1, false, &pa))
			return 0;
		return nw_jit_pa_ok(pa, 0);
	}
	else if (prim == 31 && (((op >> 1) & 0x3ff) == 215 ||
				 ((op >> 1) & 0x3ff) == 247)) {
		const int ra = (int)((op >> 16) & 0x1f);
		const int rb = (int)((op >> 11) & 0x1f);
		const uint32 ea = (ra ? sg[ra] : 0) + sg[rb];
		uint32 pa;
		if (!ppc->guest_data_probe(ea, 1, true, &pa))
			return 0;
		return nw_jit_pa_ok(pa, 1);
	}
	else if (prim == 31 && (((op >> 1) & 0x3ff) == 151 ||
				 ((op >> 1) & 0x3ff) == 150 ||
				 ((op >> 1) & 0x3ff) == 183)) {
		const int ra = (int)((op >> 16) & 0x1f);
		const int rb = (int)((op >> 11) & 0x1f);
		const uint32 ea = (ra ? sg[ra] : 0) + sg[rb];
		uint32 pa;
		if (!ppc->guest_data_probe(ea, 4, true, &pa))
			return 0;
		return nw_jit_pa_ok(pa, 1);
	}
	else if (prim == 31 && (((op >> 1) & 0x3ff) == 407 ||
				 ((op >> 1) & 0x3ff) == 439)) {
		const int ra = (int)((op >> 16) & 0x1f);
		const int rb = (int)((op >> 11) & 0x1f);
		const uint32 ea = (ra ? sg[ra] : 0) + sg[rb];
		uint32 pa;
		if (!ppc->guest_data_probe(ea, 2, true, &pa))
			return 0;
		return nw_jit_pa_ok(pa, 1);
	}
	else if (prim == 31 && (((op >> 1) & 0x3ff) == 279 ||
				 ((op >> 1) & 0x3ff) == 311 ||
				 ((op >> 1) & 0x3ff) == 343 ||
				 ((op >> 1) & 0x3ff) == 375)) {
		const int ra = (int)((op >> 16) & 0x1f);
		const int rb = (int)((op >> 11) & 0x1f);
		const uint32 ea = (ra ? sg[ra] : 0) + sg[rb];
		uint32 pa;
		if (!ppc->guest_data_probe(ea, 2, false, &pa))
			return 0;
		return nw_jit_pa_ok(pa, 0);
	}
	else if (prim == 31 && ((op >> 1) & 0x3ff) == 599) {
		const int ra = (int)((op >> 16) & 0x1f);
		const int rb = (int)((op >> 11) & 0x1f);
		const uint32 ea = (ra ? sg[ra] : 0) + sg[rb];
		uint32 pa;
		if (!ppc->guest_data_probe(ea, 8, false, &pa))
			return 0;
		return nw_jit_pa_ok(pa, 0);
	}
	else if (prim == 31 && ((op >> 1) & 0x3ff) == 727) {
		const int ra = (int)((op >> 16) & 0x1f);
		const int rb = (int)((op >> 11) & 0x1f);
		const uint32 ea = (ra ? sg[ra] : 0) + sg[rb];
		uint32 pa;
		if (!ppc->guest_data_probe(ea, 8, true, &pa))
			return 0;
		return nw_jit_pa_ok(pa, 1);
	}
	else if (prim == 33)
		;
	else if (prim == 34)
		width = 1;
	else if (prim == 38 || prim == 39) {
		width = 1;
		is_st = 1;
	} else if (prim == 40 || prim == 41 || prim == 42 || prim == 43)
		width = 2;
	else if (prim == 44 || prim == 45) {
		width = 2;
		is_st = 1;
	} else if (prim == 49)
		width = 4;
	else if (prim == 50 || prim == 51)
		width = 8;
	else if (prim == 54 || prim == 55) {
		width = 8;
		is_st = 1;
	} else if (prim == 46 || prim == 47) {
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
	if (prim == 31 && (xo == 266 || xo == 778)) {
		sg[rd] = sg[ra] + sg[rb];
		return;
	}
	if (prim == 31 && xo == 183 && ra) {
		sg[ra] = sg[ra] + sg[rb];
		return;
	}
	if (prim == 8) {
		sg[rd] = (uint32)simm - sg[ra];
		return;
	}
	if (prim == 32 || prim == 33) {
		const uint32 ea = (ra ? sg[ra] : 0) + (uint32)simm;
		uint32 pa;
		if (ppc->guest_data_probe(ea, 4, false, &pa) && nw_jit_pa_ok(pa, 0) == 1)
			sg[rd] = vm_read_memory_4(pa);
		if (prim == 33 && ra)
			sg[ra] = ea;
		return;
	}
	if (prim == 34) {
		const uint32 ea = (ra ? sg[ra] : 0) + (uint32)simm;
		uint32 pa;
		if (ppc->guest_data_probe(ea, 1, false, &pa) && nw_jit_pa_ok(pa, 0) == 1)
			sg[rd] = vm_read_memory_1(pa);
		return;
	}
	if (prim == 31 && xo == 87) {
		const uint32 ea = (ra ? sg[ra] : 0) + sg[rb];
		uint32 pa;
		if (ppc->guest_data_probe(ea, 1, false, &pa) && nw_jit_pa_ok(pa, 0) == 1)
			sg[rd] = vm_read_memory_1(pa);
		return;
	}
	if (prim == 31 && xo == 119) {
		const uint32 ea = sg[ra] + sg[rb];
		uint32 pa;
		if (ppc->guest_data_probe(ea, 1, false, &pa) && nw_jit_pa_ok(pa, 0) == 1) {
			sg[rd] = vm_read_memory_1(pa);
			if (ra)
				sg[ra] = ea;
		}
		return;
	}
	if (prim == 31 && xo == 311) {
		const uint32 ea = sg[ra] + sg[rb];
		uint32 pa;
		if (ppc->guest_data_probe(ea, 2, false, &pa) && nw_jit_pa_ok(pa, 0) == 1) {
			sg[rd] = vm_read_memory_2(pa);
			if (ra)
				sg[ra] = ea;
		}
		return;
	}
	if ((prim == 37 || prim == 39 || prim == 45) && ra) {
		sg[ra] = (ra ? sg[ra] : 0) + (uint32)simm;
		return;
	}
	if ((prim == 41 || prim == 49 || prim == 51 || prim == 55) && ra) {
		const uint32 ea = sg[ra] + (uint32)simm;
		unsigned w = (prim == 41) ? 2u : (prim == 49) ? 4u : 8u;
		int st = (prim == 55);
		uint32 pa;
		if (ppc->guest_data_probe(ea, w, st != 0, &pa) && nw_jit_pa_ok(pa, st) == 1) {
			if (prim == 41)
				sg[rd] = vm_read_memory_2(pa);
			sg[ra] = ea;
		}
		return;
	}
	if (prim == 31 && xo == 55) {
		const uint32 ea = sg[ra] + sg[rb];
		uint32 pa;
		if (ppc->guest_data_probe(ea, 4, false, &pa) && nw_jit_pa_ok(pa, 0) == 1) {
			sg[rd] = vm_read_memory_4(pa);
			if (ra)
				sg[ra] = ea;
		}
		return;
	}
	if (prim == 31 && xo == 23) {
		const uint32 ea = (ra ? sg[ra] : 0) + sg[rb];
		uint32 pa;
		if (ppc->guest_data_probe(ea, 4, false, &pa) && nw_jit_pa_ok(pa, 0) == 1)
			sg[rd] = vm_read_memory_4(pa);
		return;
	}
	if (prim == 31 && (xo == 343 || xo == 375)) {
		const uint32 ea = (ra ? sg[ra] : 0) + sg[rb];
		uint32 pa;
		if (ppc->guest_data_probe(ea, 2, false, &pa) && nw_jit_pa_ok(pa, 0) == 1)
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

/* KPX is authoritative and advances the actual machine once. The native
 * shadow starts from the entry snapshot and consumes observations without
 * touching translation/PTEs, RAM, devices, reservations or CPU callbacks. */
int powerpc_cpu::nw_jit_verify_block(nw_jit_cpu &jc, nw_jit_fn fn, const uint32 *ops, int n, uint32 compiled_first)
{
    jc.reserve_valid = regs().reserve_valid;
    jc.reserve_ea = regs().reserve_addr;
    jc.srr0 = srr0_; jc.srr1 = srr1_;
    const nw_jit_cpu initial = jc;
    nw_jit_verify_trace trace;
    const uint64 serial = exception_serial_;
    const uint32 start = pc();
    struct recording_scope {
        nw_jit_verify_trace *&slot;
        recording_scope(nw_jit_verify_trace *&s, nw_jit_verify_trace &t) : slot(s) { assert(!slot); slot = &t; }
        ~recording_scope() { slot = NULL; }
    };
    {
        recording_scope recording(nw_verify_trace_, trace);
        for (int i = 0; i < n && pc() == start + uint32(i) * 4; ++i) {
            const uint32 op = ops[i], instruction_pc = pc();
            const unsigned p = op >> 26, x = (op >> 1) & 1023;
            uint32 a = 0, b = 0;
            const bool system = nw_jit_op_system(op);
            if (system) {
                // KPX operands are read independently from the architectural CPU.
                const unsigned d = (op >> 21) & 31, ra = (op >> 16) & 31, rb = (op >> 11) & 31;
                if (p == 3 || (p == 31 && x == 4)) { a = gpr(ra); b = p == 3 ? uint32(int32(int16(op))) : gpr(rb); }
                else if (p == 31) {
                    if (x == 339 || x == 371 || x == 467) { a = ra | (rb << 5); if (x == 467) b = gpr(d); }
                    else if (x == 146) a = gpr(d);
                    else if (x == 210 || x == 595) { a = ra & 15; if (x == 210) b = gpr(d); }
                    else if (x == 242 || x == 659) { a = gpr(rb) >> 28; if (x == 242) b = gpr(d); }
                    else if (x == 306) a = gpr(rb);
                    else if (x == 54 || x == 86 || x == 246 || x == 278 || x == 470 || x == 758 || x == 982 || x == 1014)
                        a = (ra ? gpr(ra) : 0) + gpr(rb);
                }
                if (p == 31 && x == 1014) a &= ~31u;
                nw_verify_system_status_ = NW_SYS_OK; nw_verify_system_store_pa_ = 0xffffffffu;
            }
            // dcbz has one typed whole-line service observation, including its
            // translation outcome, rather than eight replayed scalar stores.
            if (system && p == 31 && x == 1014) nw_verify_trace_ = NULL;
            decode(op)->execute(this, op);
            nw_verify_trace_ = &trace;
            if (system) {
                nw_jit_system_result r = {0, 0, nw_verify_system_status_};
                if (exception_serial_ != serial) {
                    if ((pc() & 0xfffu) == 0x300u) r.status = NW_SYS_DSI;
                    else if (p == 17) r.status = NW_SYS_SC;
                    else if (p == 3 || (p == 31 && x == 4)) r.status = NW_SYS_TRAP;
                    else r.status = NW_SYS_PRIV;
                } else {
                    if (p == 19 && x == 50) { r.value = srr0_; r.aux = srr1_; }
                    else if (p == 31 && (x == 339 || x == 371 || x == 83 || x == 595 || x == 659)) r.value = gpr((op >> 21) & 31);
                    if (p == 31 && x == 1014 && nw_verify_system_store_pa_ != 0xffffffffu &&
                        (nw_verify_system_store_pa_ & ~0xfffu) == (last_fetch_pa_ & ~0xfffu)) r.status = NW_SYS_SMC;
                }
                trace.record_system(instruction_pc, op, a, b, r);
            }
#if NW_BOOT_LOG
            nw_event_insn();
            nw_jit_pc_hot(start + uint32(i) * 4, ops[i]);
#endif
            if (exception_serial_ != serial || trace.smc || nw_jit_op_ends_block(ops[i])) break;
        }
    }
    if (trace.overflow) {
        nw_jit_verify_uncompared(NW_JIT_FAULT_VERIFY);
        return 1; // reference has already advanced; never repeat its effects
    }
    jc.host = NULL;
    jc.mem = NULL;
    jc.verify_context = &trace;
    jc.verify_mem = nw_jit_verify_trace::replay;
    jc.verify_xlate = nw_jit_verify_trace::replay_translation;
    jc.verify_system = nw_jit_verify_trace::replay_system;
    nw_jit_cpu_bind(&jc);
    nw_jit_tail_begin();
    fn(&jc);
    jc.verify_mem = NULL;
    jc.verify_system = NULL;
    jc.verify_xlate = NULL;
    jc.verify_context = NULL;
    jc.jit_dtlb = NULL; // next production entry binds the live cache afresh

    const bool system_last = trace.count && trace.accesses[trace.count - 1].kind == nw_jit_verify_trace::system;
    const bool dsi = trace.count && trace.accesses[trace.count - 1].fault == (system_last ? NW_SYS_DSI : NW_JIT_FAULT_DSI);
    const bool system_exception = exception_serial_ != serial && !dsi;
    const uint32 expected_pc = dsi ? trace.accesses[trace.count - 1].pc : pc();
    const uint32 result_pc = jc.fault == NW_JIT_FAULT_SMC ? jc.pc + 4 : jc.pc;
    unsigned bits = !trace.complete() ? 1u : 0;
    if (result_pc != expected_pc) bits |= 2u;
    if (jc.cr != cr().get()) bits |= 4u;
    if (jc.xer != xer().get()) bits |= 8u;
    if (jc.lr != lr()) bits |= 16u;
    if (jc.ctr != ctr()) bits |= 32u;
    if (jc.dec != dec_) bits |= 64u;
    for (int i = 0; i < 32; ++i) {
        if (jc.gpr[i] != gpr(i)) bits |= 128u;
        if (jc.fpr[i] != fpr_dw(i)) bits |= 256u;
        for (int w = 0; w < 4; ++w) if (jc.vr[i][w] != vr(i).w[w]) bits |= 512u;
    }
    if (jc.fpscr != fpscr()) bits |= 1024u;
    if (jc.msr != (dsi ? initial.msr : ppc32_guest_mmu().msr())) bits |= 2048u;
    if (jc.vscr != vscr().get()) bits |= 4096u;
    if (jc.reserve_valid != regs().reserve_valid || jc.reserve_ea != regs().reserve_addr) bits |= 16384u;
    const uint32 expected_fault = dsi ? NW_JIT_FAULT_DSI : trace.smc ? NW_JIT_FAULT_SMC : system_exception ? NW_JIT_FAULT_EXC : 0;
    const nw_jit_verify_trace::access *exit_access = NULL;
    for (unsigned i = 0; i < trace.count; ++i)
        if (trace.accesses[i].kind != nw_jit_verify_trace::system && trace.accesses[i].fault == expected_fault && expected_fault)
            exit_access = &trace.accesses[i];
    if (jc.fault != expected_fault) bits |= 8192u;
    if (exit_access) {
        const nw_jit_verify_trace::access &a = *exit_access;
        if (jc.fault_ea != a.ea || jc.fault_width != a.width || jc.fault_st != a.store) bits |= 8192u;
    }
    if (system_exception && (jc.srr0 != srr0_ || jc.srr1 != srr1_)) bits |= 32768u;
    if (system_last && (dsi || trace.smc)) {
        const auto &a = trace.accesses[trace.count - 1];
        if (jc.fault_ea != a.width || jc.fault_width != 32 || jc.fault_st != 1) bits |= 8192u;
    }
    nw_jit_verify_note(ops, n, bits != 0);
    static unsigned comparisons, misses, captures;
    static uint32 captured_pc[16];
    ++comparisons;
    if (bits) {
        ++misses;
        bool already_captured = false;
        for (unsigned i = 0; i < captures; ++i) already_captured |= captured_pc[i] == start;
        if (!already_captured && captures < 16) {
            captured_pc[captures++] = start;
            NW_DIAG("NW-BOOT JIT isolated verify miss #%u pc=%08x n=%d bits=%x accesses=%u/%u fault=%u expected=%u cached_op=%08x current_op=%08x\n",
                   misses, start, n, bits, trace.cursor, trace.count, jc.fault, expected_fault, compiled_first, ops[0]);
            // A bounded, deterministic replay record with no host pointers.
            // Captures all input/output registers, opcodes and access observations.
            char name[160];
            snprintf(name, sizeof name, "/tmp/nw-ppc-verify-%ld-%u.txt", (long)getpid(), captures);
            if (FILE *file = fopen(name, "w")) {
                fprintf(file, "NW-PPC-VERIFY 3\npc %08x bits %x n %d\nfault %u %08x %u %u\n", start, bits, n, expected_fault,
                        exit_access ? exit_access->ea : system_last && (dsi || trace.smc) ? trace.accesses[trace.count - 1].width : 0,
                        exit_access ? exit_access->width : system_last && (dsi || trace.smc) ? 32 : 0,
                        exit_access ? exit_access->store : system_last && (dsi || trace.smc) ? 1 : 0);
                fprintf(file, "exception %08x %08x %08x %08x\n", initial.srr0, initial.srr1, srr0_, srr1_);
                fprintf(file, "cached_first %08x\n", compiled_first);
                fprintf(file, "reservation %u %08x %u %08x %u %08x\n",
                        initial.reserve_valid, initial.reserve_ea, regs().reserve_valid, regs().reserve_addr,
                        jc.reserve_valid, jc.reserve_ea);
                for (int i = 0; i < n; ++i) fprintf(file, "op %08x\n", ops[i]);
                fprintf(file, "state cr xer fpscr lr ctr dec msr vscr\ninput %08x %08x %08x %08x %08x %08x %08x %08x\n",
                        initial.cr, initial.xer, initial.fpscr, initial.lr, initial.ctr, initial.dec, initial.msr, initial.vscr);
                fprintf(file, "reference %08x %08x %08x %08x %08x %08x %08x %08x pc %08x\n",
                        cr().get(), xer().get(), fpscr(), lr(), ctr(), dec_, dsi ? initial.msr : ppc32_guest_mmu().msr(), vscr().get(), expected_pc);
                fprintf(file, "native %08x %08x %08x %08x %08x %08x %08x %08x pc %08x\n",
                        jc.cr, jc.xer, jc.fpscr, jc.lr, jc.ctr, jc.dec, jc.msr, jc.vscr, result_pc);
                for (int i = 0; i < 32; ++i) {
                    fprintf(file, "gpr %d %08x %08x %08x\n", i, initial.gpr[i], gpr(i), jc.gpr[i]);
                    fprintf(file, "fpr %d %016llx %016llx %016llx\n", i,
                            (unsigned long long)initial.fpr[i], (unsigned long long)fpr_dw(i), (unsigned long long)jc.fpr[i]);
                    for (int w = 0; w < 4; ++w) fprintf(file, "vr %d %d %08x %08x %08x\n", i, w, initial.vr[i][w], vr(i).w[w], jc.vr[i][w]);
                }
                for (unsigned i = 0; i < trace.count; ++i) {
                    const nw_jit_verify_trace::access &a = trace.accesses[i];
                    fprintf(file, "%s %08x %08x %u %u %u %016llx\n", a.kind == nw_jit_verify_trace::system ? "system" : a.kind == nw_jit_verify_trace::translation ? "translation" : "access", a.pc, a.ea, a.width, a.store, a.fault, (unsigned long long)a.value);
                }
                fclose(file);
                NW_DIAG("NW-BOOT JIT verify capture %s\n", name);
            }
            fflush(stdout);
        }
    } else if (comparisons <= 16) {
        NW_DIAG("NW-BOOT JIT isolated verify ok #%u pc=%08x n=%d accesses=%u\n", comparisons, start, n, trace.count);
        fflush(stdout);
    }
    if (comparisons % 100000 == 0) nw_jit_verify_dump(comparisons % 1000000 == 0 ? "isolated" : "periodic");
    // Run the kernel-call shortcut once, after architectural exception replay.
    if (system_exception && system_last && trace.accesses[trace.count - 1].fault == NW_SYS_TRAP)
        (void)programint_kcall_fast();
    return 1;
}

int powerpc_cpu::nw_jit_try(uint32 first_opcode)
{
	const int mode = nw_jit_mode();
	if (mode != NW_JIT_ON && mode != NW_JIT_VERIFY)
		return 0;

	const uint32 guest_pc = pc();
	const uint32 phys_page = last_fetch_pa_ & ~0xfffu;
	const uint32 msr = ppc32_guest_mmu().msr();
	const uint32 msr_ir = ((msr & ppc32_mmu::MSR_IR) ? 1u : 0u) |
			      ((msr & ppc32_mmu::MSR_DR) ? 2u : 0u) |
			      ((msr & ppc32_mmu::MSR_PR) ? 4u : 0u);
	uint32 ops[NW_JIT_MAX_BLOCK];
	int n = 0;
	int hit = 0;
	int uses_fpr = 0, uses_vr = 0;
	uint32_t chain_pc = 0;
	uint32_t gpr_mask = 0xffffffffu;
	uint32_t fpr_mask = 0xffffffffu;
	int16_t chain_disp = 0;
	uint32_t compiled_first = first_opcode;
	nw_jit_fn fn = nw_jit_cache_get(phys_page, guest_pc, msr_ir, 0, &n,
					&uses_fpr, &uses_vr, &chain_pc, &gpr_mask,
					&chain_disp, &fpr_mask, &compiled_first);
	if (fn == NW_JIT_INTERPRET)
		return 0;
	if (fn && n > 0 && n <= NW_JIT_MAX_BLOCK) {
		hit = 1;
		/* ON hit: do not re-fetch ops[]. VERIFY still needs them for
		 * the kpx compare loop. DSI width uses a lazy load of one word. */
		if (mode == NW_JIT_VERIFY) {
			for (int i = 0; i < n; i++)
				ops[i] = vm_read_memory_4(last_fetch_pa_ + (uint32)i * 4u);
		}
#if NW_BOOT_LOG
		{
			static uint32 hit_pc_sample;
			if ((++hit_pc_sample & 255u) == 0)
				nw_jit_pc_hot(guest_pc, first_opcode);
			if (guest_pc == 0x00ee2f94u) {
				static int spin_dump;
				if (!spin_dump) {
					spin_dump = 1;
					for (int k = 0; k < 16; k++) {
						uint32 w = 0;
						const uint32 ea = guest_pc + (uint32)k * 4u;
						const int ok = nw_jit_peek(ea, &w);
						NW_DIAG("NW-BOOT G1: spinword %+d ea=%08x %s %08x\n",
						       k, (unsigned)ea, ok ? "ok" : "fail",
						       (unsigned)w);
					}
					fflush(stdout);
				}
			}
		}
#endif
	} else {
		fn = NULL;
		if (!nw_jit_op_supported(first_opcode)) {
			nw_jit_skip_raw_once(first_opcode, guest_pc);
			if ((first_opcode >> 26) == 6) {
				static int op6_id;
				if (!op6_id) {
					op6_id = 1;
					NW_DIAG("NW-BOOT G1: op6 id pc=%08x op=%08x msr=%08x IR=%u DR=%u PR=%u\n",
					       (unsigned)guest_pc, (unsigned)first_opcode,
					       (unsigned)msr,
					       (msr & ppc32_mmu::MSR_IR) ? 1u : 0u,
					       (msr & ppc32_mmu::MSR_DR) ? 1u : 0u,
					       (msr & ppc32_mmu::MSR_PR) ? 1u : 0u);
					for (int k = -4; k <= 4; k++) {
						uint32 w = 0;
						const uint32 ea = guest_pc + (uint32)k * 4u;
						const int ok = nw_jit_peek(ea, &w);
						NW_DIAG("NW-BOOT G1: op6 word %+d ea=%08x %s %08x\n",
						       k, (unsigned)ea, ok ? "ok" : "fail",
						       (unsigned)w);
					}
					fflush(stdout);
				}
			}
			nw_jit_note_skip_unsup(first_opcode, 1, guest_pc);
			return 0;
		}
		if (!nw_jit_op_dispatch(first_opcode)) {
			nw_jit_verify_skip(1);
			return 0;
		}

		uint32 sg[32];
		for (int i = 0; i < 32; i++)
			sg[i] = gpr(i);
		{
			const int mok0 = nw_jit_op_mem_ok(this, first_opcode, sg);
			if (mok0 == 0) {
				const int prim0 = (int)(first_opcode >> 26);
				const int ra = (int)((first_opcode >> 16) & 0x1f);
				const int simm = (int16_t)(first_opcode & 0xffffu);
				const uint32 ea = (ra ? sg[ra] : 0) + (uint32)simm;
				uint32 pa = 0;
				guest_data_probe(ea, 4, prim0 == 36, &pa);
				if (nw_pa_kind(pa) == NW_PA_IO) {
					nw_jit_verify_uncompared(2);
					nw_jit_note_skip_io(ea, guest_pc);
				} else
					nw_jit_verify_uncompared(1);
				return 0;
			}

			ops[0] = first_opcode;
			n = 1;
			nw_jit_sg_apply(this, sg, ops[0]);
			if (mok0 == 2) {
				nw_jit_note_cut(NW_JIT_CUT_FIRST_OP_IO);
			} else {
				int cut = -1;
				while (n < NW_JIT_MAX_BLOCK && !nw_jit_op_ends_block(ops[n - 1])) {
					const uint32 ea = guest_pc + (uint32)n * 4u;
					if ((ea & ~0xfffu) != (guest_pc & ~0xfffu)) {
						cut = NW_JIT_CUT_PAGE_CROSS;
						break;
					}
					uint32 op;
					if (!nw_jit_peek(ea, &op)) {
						cut = NW_JIT_CUT_PEEK_FAIL;
						break;
					}
					/* Integer, FP, and VMX stay in separate blocks.
					 * MSR[FP]/MSR[VEC] are only tested on the first op;
					 * an lfd…lvx block with VEC off would run VMX with
					 * no 0xf20, so the NK never saves VRs on switch. */
					if (is_altivec_insn(op) != is_altivec_insn(ops[0]) ||
					    is_fp_insn(op) != is_fp_insn(ops[0])) {
						cut = NW_JIT_CUT_CLASS_CHANGE;
						break;
					}
					if (mode == NW_JIT_VERIFY && !nw_jit_op_verify_safe(op)) break;
					if (!nw_jit_op_dispatch(op)) {
						nw_jit_skip_raw_once(op, guest_pc + (uint32)n * 4u);
						nw_jit_note_skip_unsup(op, (unsigned)n,
								       guest_pc + (uint32)n * 4u);
						cut = NW_JIT_CUT_UNSUP_NEXT;
						break;
					}
					const int mok = nw_jit_op_mem_ok(this, op, sg);
					if (mok == 0) {
						cut = NW_JIT_CUT_MEM_OK0;
						break;
					}
					ops[n++] = op;
					nw_jit_sg_apply(this, sg, op);
					if (mok == 2) {
						cut = NW_JIT_CUT_MEM_OK2;
						break;
					}
				}
				if (cut < 0)
					cut = (n >= NW_JIT_MAX_BLOCK) ? NW_JIT_CUT_MAX_BLOCK
								      : NW_JIT_CUT_ENDS_BLOCK;
				nw_jit_note_cut(cut);
				if (cut == NW_JIT_CUT_CLASS_CHANGE)
					nw_jit_itunes_note(guest_pc, 0, 2, 0);
			}
		}

		fn = nw_jit_compile(ops, n, guest_pc, phys_page, msr_ir, 0);
		(void)nw_jit_cache_get(phys_page, guest_pc, msr_ir, 0, &n,
				       &uses_fpr, &uses_vr, &chain_pc, &gpr_mask,
				       &chain_disp, &fpr_mask);
	}
	if (!fn) {
		nw_jit_note_hop_stop(NW_JIT_HOP_COMPILE_NULL);
		nw_jit_verify_fail();
		static unsigned nfail_log;
		if (nfail_log < 8u) {
			nfail_log++;
			NW_DIAG("NW-BOOT JIT verify compile-fail #%u pc=%08x n=%d op=%08x\n",
			       nfail_log, (unsigned)guest_pc, n, (unsigned)first_opcode);
			fflush(stdout);
		}
		return 0;
	}

	if (mode == NW_JIT_VERIFY) {
		for (int i = 0; i < n; ++i) {
			if (!nw_jit_op_verify_safe(ops[i])) {
				nw_jit_verify_uncompared(NW_JIT_FAULT_VERIFY);
				return 0;
			}
		}
	}

	if (!nw_jc_) {
		nw_jc_ = (struct nw_jit_cpu *)calloc(1, sizeof(*nw_jc_));
		if (!nw_jc_)
			return 0;
	}
	struct nw_jit_cpu &jc = *nw_jc_;
	const int full = (mode == NW_JIT_VERIFY);
	if (!hit) {
		uses_fpr = 0;
		for (int i = 0; i < n; i++) {
			if (is_fp_insn(ops[i]))
				uses_fpr = 1;
		}
		uses_vr = is_altivec_insn(first_opcode);
		gpr_mask = 0;
		fpr_mask = 0;
		for (int i = 0; i < n; i++) {
			gpr_mask |= nw_jit_op_gpr_mask(ops[i]);
			fpr_mask |= nw_jit_op_fpr_mask(ops[i]);
		}
	}
	if (full) {
		gpr_mask = 0xffffffffu;
		fpr_mask = 0xffffffffu;
	} else {
		uint32_t wpc = chain_pc;
		for (int t = 0; t < nw_jit_tail_max() && wpc && wpc != NW_JIT_CHAIN_DYNAMIC; t++) {
			uint32_t npa = 0;
			if (!nw_jit_itlb_lookup(wpc, &npa)) {
				gpr_mask = 0xffffffffu;
				fpr_mask = 0xffffffffu;
				break;
			}
			uint32_t sg = 0, sf = 0, ch = 0;
			int n2 = 0, f2 = 0, v2 = 0;
			nw_jit_fn nx = nw_jit_cache_get(npa & ~0xfffu, wpc, msr_ir, 0,
						       &n2, &f2, &v2, &ch, &sg, NULL, &sf);
			if (!nx || nx == NW_JIT_INTERPRET || n2 <= 0) {
				gpr_mask = 0xffffffffu;
				fpr_mask = 0xffffffffu;
				break;
			}
			gpr_mask |= sg;
			fpr_mask |= sf;
			wpc = ch;
		}
	}
	/* A mixed block can start with an integer op, so execute() did not
	 * see an FP opcode. MSR[FP] still has to be on or the NK never
	 * saves the FPRs across a switch. */
	if (mode == NW_JIT_ON && uses_fpr && !(ppc32_guest_mmu().msr() & ppc32_mmu::MSR_FP)) {
		take_fpu();
		return 1;
	}
	jc.fault = 0;
	jc.fault_ea = 0;
	jc.fault_st = 0;
	jc.fault_width = 0;
	jc.dec_wr = 0;
	jc.nstore = 0;
	jc.reserve_valid = 0;
	jc.reserve_ea = 0;
	jc.gpr_live = 0;
	jc.fpr_live = 0;
	if (gpr_mask == 0xffffffffu) {
		for (int i = 0; i < 32; i++)
			jc.gpr[i] = gpr(i);
		jc.gpr_live = 0xffffffffu;
	} else {
		for (int i = 0; i < 32; i++)
			if (gpr_mask & (1u << i))
				jc.gpr[i] = gpr(i);
		jc.gpr_live = gpr_mask;
	}
	if (full || uses_vr) {
		for (int i = 0; i < 32; i++) {
			jc.vr[i][0] = vr(i).w[0];
			jc.vr[i][1] = vr(i).w[1];
			jc.vr[i][2] = vr(i).w[2];
			jc.vr[i][3] = vr(i).w[3];
		}
		jc.vscr = vscr().get();
	}
	if (full || fpr_mask == 0xffffffffu) {
		if (full || uses_fpr || fpr_mask) {
			for (int i = 0; i < 32; i++)
				jc.fpr[i] = fpr_dw(i);
			jc.fpscr = fpscr();
			jc.fpr_live = 0xffffffffu;
		}
	} else if (fpr_mask) {
		for (int i = 0; i < 32; i++)
			if (fpr_mask & (1u << i))
				jc.fpr[i] = fpr_dw(i);
		jc.fpscr = fpscr();
		jc.fpr_live = fpr_mask;
	}
	jc.cr = cr().get();
	jc.xer = xer().get();
	jc.lr = lr();
	jc.ctr = ctr();
	jc.pc = pc();
	jc.dec = dec_;
	jc.msr = ppc32_guest_mmu().msr();
	jc.host = this;
	jc.verify_mem = NULL;
	jc.verify_system = NULL;
	jc.verify_xlate = NULL;
	jc.verify_context = NULL;
	nw_jit_cpu_bind(&jc);
	if (mode == NW_JIT_VERIFY) return nw_jit_verify_block(jc, fn, ops, n, compiled_first);

	auto commit = [&]() {
		nw_commit_gpr(&jc, this);
		if (full || uses_vr) {
			for (int i = 0; i < 32; i++) {
				vr(i).w[0] = jc.vr[i][0];
				vr(i).w[1] = jc.vr[i][1];
				vr(i).w[2] = jc.vr[i][2];
				vr(i).w[3] = jc.vr[i][3];
			}
			vscr().set(jc.vscr);
		}
		if (full && jc.fpr_live == 0) {
			for (int i = 0; i < 32; i++)
				fpr_dw(i) = jc.fpr[i];
			fpscr() = jc.fpscr;
		} else
			nw_commit_fpr(&jc, this, fpscr());
		cr().set(jc.cr);
		xer().set(jc.xer);
		lr() = jc.lr;
		ctr() = jc.ctr;
		if (jc.dec_wr) {
			if ((dec_ & 0x80000000u) == 0 && (jc.dec & 0x80000000u))
				dec_pending_ = true;
			dec_ = jc.dec;
			dec_tb_base_ = tb_host_ticks();
		}
	};

#if NW_BOOT_LOG
	if (guest_pc >= 0x1de00000u && guest_pc < 0x1e000000u) {
		static int dumped_a, dumped_b;
		auto dump_loop = [&](uint32 ea, int *done) {
			if (*done)
				return;
			*done = 1;
			NW_DIAG("NW-BOOT G1: itunes-loop %08x\n", (unsigned)ea);
			for (int i = 0; i < 20; i++) {
				const uint32 a = ea + (uint32)i * 4u;
				const ppc32_xlate_result xr = ppc32_guest_mmu().translate(
					a, PPC32_XLATE_DR, 4, false);
				if (!xr.ok) {
					NW_DIAG("  %08x translate-fail\n", (unsigned)a);
					continue;
				}
				NW_DIAG("  %08x %08x\n", (unsigned)a,
				       (unsigned)vm_read_memory_4(xr.pa));
			}
			fflush(stdout);
		};
		if (guest_pc >= 0x1dedfd00u && guest_pc < 0x1dedff00u)
			dump_loop(0x1dedfd38u, &dumped_a);
		if (guest_pc >= 0x1dfa2300u && guest_pc < 0x1dfa2500u)
			dump_loop(0x1dfa2354u, &dumped_b);
	}
#endif
	nw_jit_tail_begin();
#if NW_BOOT_LOG
	{
		const int it = guest_pc >= 0x1de00000u && guest_pc < 0x1e000000u;
		const uint64 t0 = it ? GetTicks_usec() : 0;
		fn(&jc);
		if (it)
			nw_jit_itunes_note(guest_pc, n, 0, GetTicks_usec() - t0);
	}
#else
	fn(&jc);
#endif
	int n_total = n;
	{
		const int extra = nw_jit_tail_n();
		if (extra > 0) {
			n_total += extra;
			int tf = 0, tv = 0;
			nw_jit_tail_class(&tf, &tv);
			if (tf)
				uses_fpr = 1;
			if (tv)
				uses_vr = 1;
		}
	}
	/* Run the successor block (fall-through or uncond b target) without
	 * returning to execute(). execute() tests MSR[VEC]/MSR[FP] against the
	 * class of every block's first opcode and takes 0xf20/0x800 there, so a
	 * hop must decline a successor whose class is disabled: running VMX with
	 * MSR[VEC]=0 skips the 0xf20 the NK uses to latch "this task uses
	 * vectors", and VRs are then not saved across a context switch. A block
	 * ends at a class change, so the fall-through chain_pc is usually exactly
	 * that first lvx/lfd — which is why ungated hops inverted QuickDraw. */
	if (mode == NW_JIT_ON && !jc.fault && nw_jit_tail_n() == 0) {
		int hops = 0;
        /* Dynamic branches chain only to their completed architectural PC.
         * A native tail already owns its final stop; do not reuse the initial
         * block's successor metadata after a tail traversed other blocks. */
		for (;;) {
			if (jit_events_pending()) break;
			if (hops >= 8) {
				nw_jit_note_hop_stop(NW_JIT_HOP_CAP);
				break;
			}
			if (!chain_pc) {
				nw_jit_note_hop_stop(NW_JIT_HOP_NO_CHAIN_PC);
				break;
			}
			if (chain_pc != NW_JIT_CHAIN_DYNAMIC && jc.pc != chain_pc) {
				nw_jit_note_hop_stop(NW_JIT_HOP_PC_MISMATCH);
				break;
			}
			if (nw_68k_wants_boundary(jc.pc)) break;
			/* Re-read MSR: hop 1 may have run mtmsr/rfi via a host
			 * helper, which changes both the class gate and the key. */
			const uint32 hmsr = ppc32_guest_mmu().msr();
			const uint32 hmsr_ir = ((hmsr & ppc32_mmu::MSR_IR) ? 1u : 0u) |
					       ((hmsr & ppc32_mmu::MSR_DR) ? 2u : 0u) |
					       ((hmsr & ppc32_mmu::MSR_PR) ? 4u : 0u);
			uint32_t npa = 0;
			if (!nw_jit_itlb_lookup(jc.pc, &npa)) {
				nw_jit_note_hop_stop(NW_JIT_HOP_ITLB_MISS);
				break;
			}
			if (nw_aline_dispatch_pa(npa)) {
				nw_jit_note_hop_stop(NW_JIT_HOP_ALINE);
				break;
			}
			int n2 = 0, f2 = 0, v2 = 0;
			uint32_t chain2 = 0, sg = 0, sf = 0;
			int16_t disp2 = 0;
			nw_jit_fn next = nw_jit_cache_get(npa & ~0xfffu, jc.pc, hmsr_ir, 0,
							 &n2, &f2, &v2, &chain2, &sg, &disp2, &sf);
			if (!next || next == NW_JIT_INTERPRET ||
			    n2 <= 0 || n2 > NW_JIT_MAX_BLOCK) {
				nw_jit_note_hop_stop(NW_JIT_HOP_CACHE_MISS);
				break;
			}
			if (v2 && !(hmsr & NW_MSR_VEC)) {
				nw_jit_note_hop_stop(NW_JIT_HOP_VEC_GATE);
				break;
			}
			if (f2 && !(hmsr & ppc32_mmu::MSR_FP)) {
				nw_jit_note_hop_stop(NW_JIT_HOP_FP_GATE);
				nw_jit_itunes_note(jc.pc, 0, 3, 0);
				break;
			}
			commit();
			/* commit() applied this block's mtspr DEC and rebased
			 * dec_tb_base_. Leaving dec_wr set would re-apply the same
			 * DEC on every later hop and pin the decrementer. */
			jc.dec_wr = 0;
			jc.dec = dec_;
			pc() = jc.pc;
			nw_68k_reference_chain(jc.pc, gpr(24) - 2u, gpr(29));
			nw_pull_gpr(&jc, this, sg);
			if (f2) {
				nw_pull_fpr(&jc, this, sf ? sf : 0xffffffffu, fpscr());
				uses_fpr = 1;
			}
			if (v2 && !uses_vr) {
				for (int i = 0; i < 32; i++) {
					jc.vr[i][0] = vr(i).w[0];
					jc.vr[i][1] = vr(i).w[1];
					jc.vr[i][2] = vr(i).w[2];
					jc.vr[i][3] = vr(i).w[3];
				}
				jc.vscr = vscr().get();
				uses_vr = 1;
			}
			jc.fault = 0;
			jc.fault_ea = 0;
			jc.fault_st = 0;
			jc.fault_width = 0;
			jc.nstore = 0;
			last_fetch_pa_ = npa;
			/* The successor's inline DTLB reads jc.msr, not hmsr.
			 * Leaving the entry MSR there rejected way 1 for the
			 * rest of the hop (page 0x00280000 refill storm). */
			jc.msr = hmsr;
#if NW_BOOT_LOG
			{
				const uint32 hop_pc = jc.pc;
				const int it = hop_pc >= 0x1de00000u && hop_pc < 0x1e000000u;
				const uint64 t0 = it ? GetTicks_usec() : 0;
				next(&jc);
				if (it)
					nw_jit_itunes_note(hop_pc, n2, 0, GetTicks_usec() - t0);
			}
#else
			next(&jc);
#endif
            const int tail = nw_jit_tail_n();
            if (tail) {
                int tf = 0, tv = 0; nw_jit_tail_class(&tf,&tv);
                uses_fpr |= tf; uses_vr |= tv; n_total += tail;
            }
			n_total += n2;
			chain_pc = chain2;
			chain_disp = disp2;
			hops++;
			if (nw_jit_tail_n() > 0) break;
			if (jc.fault)
				break;
		}
		if (hops)
			nw_jit_note_chain(hops);
		n = n_total;
	}

	if (jc.fault) {
		if (mode == NW_JIT_ON && jc.fault == NW_JIT_FAULT_EXC) {
			commit();
			(void)programint_kcall_fast();
			nw_jit_note_exec_at(n, guest_pc, uses_vr);
#if NW_BOOT_LOG
			nw_event_insns((unsigned)n);
#endif
			return 1;
		}
		if (mode == NW_JIT_ON && jc.fault == NW_JIT_FAULT_SMC) {
			commit();
			pc() = jc.pc + 4u;
			nw_jit_note_exec_at(n, guest_pc, uses_vr);
#if NW_BOOT_LOG
			nw_event_insns((unsigned)n);
#endif
			return 1;
		}
		if (mode == NW_JIT_ON && jc.fault == NW_JIT_FAULT_DSI &&
		    jc.fault_width && ppc32_guest_mmu_enabled()) {
			/* Width belongs to the actual failed subaccess. Reconstructing it
			 * from the first opcode misses indexed FP/vector, narrow stores,
			 * and partial string/multiple accesses, especially after chaining. */
			const ppc32_xlate_result xr = ppc32_guest_mmu().translate(
				jc.fault_ea, PPC32_XLATE_DR, jc.fault_width, jc.fault_st != 0);
			if (!xr.ok) {
				commit();
				pc() = jc.pc;
				static unsigned ndsi_log;
				if (ndsi_log < 8u) {
					ndsi_log++;
					NW_DIAG("NW-BOOT G1: jit dsi #%u pc=%08x ea=%08x st=%u\n",
					       ndsi_log, (unsigned)jc.pc,
					       (unsigned)jc.fault_ea, (unsigned)jc.fault_st);
					fflush(stdout);
				}
				take_data_dsi(jc.fault_ea, jc.fault_st != 0, xr.fault);
				nw_jit_note_exec_at(n, guest_pc, uses_vr);
				return 1;
			}
		}
		static unsigned nskip_log;
		nskip_log++;
		if (nskip_log <= 8u) {
			NW_DIAG("NW-BOOT JIT verify skip-%s #%u pc=%08x op=%08x n=%d\n",
			       jc.fault == 2 ? "io" : "dsi", nskip_log,
			       (unsigned)guest_pc, (unsigned)first_opcode, n);
			fflush(stdout);
		}
		/* Mid-block IO: keep stores already done, resume at the
		 * faulting insn (return 1 so execute() fetches that opcode).
		 * First-insn skip: pc unchanged, return 0, kpx does it. */
		if (mode == NW_JIT_ON && jc.pc != guest_pc) {
			commit();
			pc() = jc.pc;
			nw_jit_note_exec_at(n, guest_pc, uses_vr);
			return 1;
		}
		nw_jit_verify_uncompared(jc.fault);
		if (jc.fault == 2)
			nw_jit_note_skip_io(jc.fault_ea, jc.pc);
		nw_jit_itunes_note(guest_pc, 1, 1, 0);
		return 0;
	}

	if (mode == NW_JIT_ON) {
		commit();
		pc() = jc.pc;
		nw_jit_note_exec_at(n, guest_pc, uses_vr);
		{
			static unsigned nlog;
			if (nlog < 16u) {
				nlog++;
				NW_DIAG("NW-BOOT JIT on #%u n=%d pc=%08x op=%08x -> %08x\n",
				       nlog, n, (unsigned)guest_pc, (unsigned)first_opcode,
				       (unsigned)jc.pc);
				fflush(stdout);
			}
		}
#if NW_BOOT_LOG
		nw_glue_add(n);
		{
			nw_event_insns((unsigned)n);
			if (!hit) {
				for (int i = 0; i < n; i++)
					nw_jit_pc_hot(guest_pc + (uint32)i * 4u, ops[i]);
			}
		}
#endif
		return 1;
	}

	return 0;
}
#endif

void powerpc_cpu::nw_68k_snapshot(nw68_nk_state &s) const
{
	for (unsigned i = 0; i < 32; ++i) s.gpr[i] = gpr(i);
	s.cr = cr().get(); s.xer = xer().get(); s.lr = lr(); s.ctr = ctr(); s.ppc_pc = pc();
}

void powerpc_cpu::nw_68k_restore(const nw68_nk_state &s)
{
	for (unsigned i = 0; i < 32; ++i) gpr(i) = s.gpr[i];
	cr().set(s.cr); xer().set(s.xer); lr() = s.lr; ctr() = s.ctr; pc() = s.ppc_pc;
}

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
					const int blocklen = (int)(di - bi->di);
					memmove(decode_cache_p, bi->di, blocklen * sizeof(*di));
					bi->di = decode_cache_p;
					di = bi->di + blocklen;
				}
			} while ((ii->cflow & CFLOW_END_BLOCK) == 0);
			bi->end_pc = dpc;
			bi->min_pc = dpc;
			bi->max_pc = entry;
			bi->size = (uint32)(di - bi->di);
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
#ifdef SHEEPSHAVER
		nw_68k_observe(this, pc(), execute_depth);
#endif
		uint32 opcode;
		if (ppc32_guest_mmu_enabled()) {
			tick_decrementer();
			if (async_exception_pending() && (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_EE)) {
				take_async_exception();
				continue;
			}
#ifdef SHEEPSHAVER
			/* After the sleep, apply the TB delta the 256-instruction
			 * divider would have skipped so DEC and the OpenPIC keep
			 * wall time. */
			if (guest_idle_wait()) {
				catch_up_timebase();
				if (!spcflags().empty() && !check_spcflags())
					goto return_site;
				continue;
			}
#endif
		}
#ifdef SHEEPSHAVER
		/* Native 68k returns after at most eight register instructions.
		 * Normal guest execution and Execute68k share this event boundary. */
		if (nw_68k_dispatch(this)) {
			if (!spcflags().empty() && !check_spcflags()) goto return_site;
			continue;
		}
#endif
		if (!guest_fetch(&opcode)) {
			if (!spcflags().empty() && !check_spcflags())
				goto return_site;
			continue;
		}
#if defined(SHEEPSHAVER) && NW_BOOT_LOG
		/* Regression runner: deliver a real display interrupt immediately
		 * before a shared-folder host callback. Never force an EOI. Disabled
		 * unless explicitly requested in a diagnostic build's environment. */
		if (ppc32_guest_mmu_enabled() &&
		    (opcode == (POWERPC_EMUL_OP | (OP_EXTFS_HFS + 3u)) ||
		     (opcode == (POWERPC_EMUL_OP | 1u) && pc() == 0x680ff208u))) {
			static unsigned requested = []() -> unsigned {
				const char *s = getenv("NW_TEST_EXTFS_IRQS");
				return s ? (unsigned)strtoul(s, NULL, 10) : 0u;
			}();
			static unsigned injected;
			static unsigned return_requested = []() -> unsigned {
				const char *s = getenv("NW_TEST_RETURN_IRQS");
				return s ? (unsigned)strtoul(s, NULL, 10) : 0u;
			}();
			static unsigned return_injected;
			const bool at_return = opcode == (POWERPC_EMUL_OP | 1u);
			unsigned &delivered = at_return ? return_injected : injected;
			const unsigned limit = at_return ? return_requested : requested;
			static uint64_t last;
			const uint64_t now = limit ? GetTicks_usec() : 0;
			struct nw_pic_service service;
			if (delivered < limit && now - last >= 1000000 &&
			    (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_EE) &&
			    ReadMacInt8(0x910) == 6 && ReadMacInt32(0x911) == 0x46696e64u &&
			    !nw_pic_service_snapshot(&service) &&
			    !(nw_openpic_read(NW_OPENPIC_SRC0 + NW_VBL_IRQ * 0x20) & NW_OPENPIC_IVPR_MASK)) {
				nw_openpic_set_irq(NW_VBL_IRQ, 1);
				if (nw_io_ext_irq) {
					last = now;
					printf("NW-BOOT G1: irq-test %s injection=%u pc=%08x cr=%08x\n",
					       at_return ? "return" : "extfs", ++delivered,
					       (unsigned)pc(), (unsigned)cr().get());
					fflush(stdout);
					take_external();
					continue;
				}
			}
		}
		if (ppc32_guest_mmu_enabled())
			nw_trace_pc(*this, pc());
#endif
		if (ppc32_guest_mmu_enabled() &&
		    !(ppc32_guest_mmu().msr() & NW_MSR_VEC) && is_altivec_insn(opcode) &&
		    decode(opcode)->format != INVALID_form) {
			/* An illegal encoding is not an instruction accessing VRF/VSCR. */
			take_vpu();
			continue;
		}
		if (ppc32_guest_mmu_enabled() &&
		    !(ppc32_guest_mmu().msr() & ppc32_mmu::MSR_FP) && is_fp_insn(opcode)) {
			take_fpu();
			continue;
		}
#ifdef SHEEPSHAVER
		/* Verification needs exactly one NK handler. PPC block chaining can
		 * cross several 68k boundaries before the loop's stop hook runs. */
		if (!nw_68k_reference_active(this) && nw_jit_try(opcode)) {
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
		nw_glue_add(1);
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
#ifdef SHEEPSHAVER
	nw_68k_end_execution(this, execute_depth);
#endif
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

void powerpc_cpu::invalidate_cache_range(uintptr start, uintptr end, int source)
{
#ifdef SHEEPSHAVER
	/* FlushCodeCache/MakeExecutable publish guest code through this entry.
	 * Notify both New World engines as well as the legacy KPX cache. */
	if (ppc32_guest_mmu_enabled() && start < end && start <= UINT32_MAX) {
		const uint64 limit = std::min<uint64>((uint64)end, UINT64_C(1) << 32);
		for (uint64 ea = (uint64)start & ~UINT64_C(0xfff); ea < limit; ea += 0x1000) {
			const ppc32_xlate_result r = ppc32_guest_mmu().translate(
				(uint32)ea, PPC32_XLATE_IR, 4, false, false);
			if (r.ok) nw_jit_invalidate_page_src(r.pa, source < 0 ? NW_JIT_FL_HOST : source);
		}
	}
#endif
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
