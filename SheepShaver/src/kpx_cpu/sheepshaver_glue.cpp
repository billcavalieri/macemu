/*
 *  sheepshaver_glue.cpp - Glue Kheperix CPU to SheepShaver CPU engine interface
 *
 *  SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
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
#include "cpu_emulation.h"
#include "main.h"
#include "prefs.h"
#include "xlowmem.h"
#include "emul_op.h"
#include "rom_patches.h"
#include "macos_util.h"
#include "block-alloc.hpp"
#include "sigsegv.h"
#include "cpu/ppc/ppc-cpu.hpp"
#include "cpu/ppc/ppc-mmu.hpp"
#include "cpu/ppc/ppc-operations.hpp"
#include "cpu/ppc/ppc-instructions.hpp"
#include "thunks.h"
#include "shears.h"
#include "sheepforce.h"
#include "nw_boot_contract.h"
#include "nw_devices.h"
#include "nw_io.h"
#include "nw_script.h"
#include "nw_68k_jit.h"
#include "nw_68k_core.h"
#include "nw_jit.h"

// Used for NativeOp trampolines
#include "video.h"
#include "name_registry.h"
#include "serial.h"
#include "ether.h"
#include "timer.h"
#include "audio.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#ifdef USE_SDL_VIDEO
#include "my_sdl.h"
#endif

#if ENABLE_MON
#include "mon.h"
#include "mon_disass.h"
#endif

#define DEBUG 0
#include "debug.h"

extern "C" {
#include "dis-asm.h"
}

// Emulation time statistics
#ifndef EMUL_TIME_STATS
#define EMUL_TIME_STATS 0
#endif

#if EMUL_TIME_STATS
static clock_t emul_start_time;
static uint32 interrupt_count = 0, ppc_interrupt_count = 0;
static clock_t interrupt_time = 0;
static uint32 exec68k_count = 0;
static clock_t exec68k_time = 0;
static uint32 native_exec_count = 0;
static clock_t native_exec_time = 0;
static uint32 macos_exec_count = 0;
static clock_t macos_exec_time = 0;
#endif

static void enter_mon(void)
{
	// Start up mon in real-mode
#if ENABLE_MON
	const char *arg[4] = {"mon", "-m", "-r", NULL};
	mon(3, arg);
#endif
}

// From main_*.cpp
extern uintptr SignalStackBase();

// From rsrc_patches.cpp
extern "C" void check_load_invoc(uint32 type, int16 id, uint32 h);
extern "C" void named_check_load_invoc(uint32 type, uint32 name, uint32 h);

// PowerPC EmulOp to exit from emulation loop
const uint32 POWERPC_EXEC_RETURN = POWERPC_EMUL_OP | 1;

// Enable Execute68k() safety checks?
#define SAFE_EXEC_68K 1

// Save FP state in Execute68k()?
#define SAVE_FP_EXEC_68K 1

// Interrupts in EMUL_OP mode?
#define INTERRUPTS_IN_EMUL_OP_MODE 1

// Interrupts in native mode?
#define INTERRUPTS_IN_NATIVE_MODE 1

// Pointer to Kernel Data
static KernelData * kernel_data;

// SIGSEGV handler
sigsegv_return_t sigsegv_handler(sigsegv_address_t, sigsegv_address_t);

#if PPC_ENABLE_JIT && PPC_REENTRANT_JIT
// Special trampolines for EmulOp and NativeOp
static uint8 *emul_op_trampoline;
static uint8 *native_op_trampoline;
#endif


/**
 *		PowerPC emulator glue with special 'sheep' opcodes
 **/

enum {
	PPC_I(SHEEP) = PPC_I(MAX),
	PPC_I(SHEEP_MAX)
};

class sheepshaver_cpu
	: public powerpc_cpu
{
	void init_decoder();
	void execute_sheep(uint32 opcode);

public:

	// Read-only view of the guest PC for the NW_GUEST_PROF sampler thread (a racy read is fine for sampling).
	uint32 sample_pc() { return pc(); }
	uint32 sample_gpr(int n) { return gpr(n); }

	// Constructor
	sheepshaver_cpu();

	// CR & XER accessors
	uint32 get_cr() const		{ return cr().get(); }
	void set_cr(uint32 v)		{ cr().set(v); }
	uint32 get_xer() const		{ return xer().get(); }
	void set_xer(uint32 v)		{ xer().set(v); }

	// Execute NATIVE_OP routine
	void execute_native_op(uint32 native_op);
	static void call_execute_native_op(powerpc_cpu * cpu, uint32 native_op);

	// Execute EMUL_OP routine
	void execute_emul_op(uint32 emul_op);
	static void call_execute_emul_op(powerpc_cpu * cpu, uint32 emul_op);

	// Execute 68k routine
	void execute_68k(uint32 entry, M68kRegisters *r);

	// Execute ppc routine
	void execute_ppc(uint32 entry);
	virtual void nw_invoke_mm_ppc(uint32 entry);

	// Execute MacOS/PPC code
	uint32 execute_macos_code(uint32 tvect, int nargs, uint32 const *args);

#if PPC_ENABLE_JIT
	// Compile one instruction
	virtual int compile1(codegen_context_t & cg_context);
#endif
	// Resource manager thunk
	void get_resource(uint32 old_get_resource);
	static void call_get_resource(powerpc_cpu * cpu, uint32 old_get_resource);

	// Handle MacOS interrupt
	void interrupt(uint32 entry);

	// Make sure the SIGSEGV handler can access CPU registers
	friend sigsegv_return_t sigsegv_handler(sigsegv_info_t *sip);
};

sheepshaver_cpu::sheepshaver_cpu()
{
	init_decoder();

#if PPC_ENABLE_JIT
	if (PrefsFindBool("jit"))
		enable_jit();
#endif
}

void sheepshaver_cpu::init_decoder()
{
	static const instr_info_t sheep_ii_table[] = {
		{ "sheep",
		  (execute_pmf)&sheepshaver_cpu::execute_sheep,
		  PPC_I(SHEEP),
		  D_form, 6, 0, CFLOW_JUMP | CFLOW_TRAP
		}
	};

	const int ii_count = sizeof(sheep_ii_table)/sizeof(sheep_ii_table[0]);
	D(bug("SheepShaver extra decode table has %d entries\n", ii_count));

	for (int i = 0; i < ii_count; i++) {
		const instr_info_t * ii = &sheep_ii_table[i];
		init_decoder_entry(ii);
	}
}

/*		NativeOp instruction format:
		+------------+-------------------------+--+-----------+------------+
		|      6     |                         |FN|    OP     |      2     |
		+------------+-------------------------+--+-----------+------------+
		 0         5 |6                      18 19 20      25 26        31
*/

typedef bit_field< 19, 19 > FN_field;
typedef bit_field< 20, 25 > NATIVE_OP_field;
typedef bit_field< 26, 31 > EMUL_OP_field;

#if NW_BOOT_LOG
static thread_local uint32 nw_last_return_pc;
static thread_local int nw_last_return_depth;
#endif

void sheepshaver_cpu::call_execute_emul_op(powerpc_cpu * cpu, uint32 emul_op) {
	static_cast<sheepshaver_cpu *>(cpu)->execute_emul_op(emul_op);
}

// Execute EMUL_OP routine
void sheepshaver_cpu::execute_emul_op(uint32 emul_op)
{
	M68kRegisters r68;
	if (ROMType == ROMTYPE_NEWWORLD && nw_la_kdp_pa == 0) {
		/* First host entry with the NK's MMU up: learn where the NK put
		 * the kernel data page so host reads of KDP LAs reach it. */
		ppc32_xlate_result xr = ppc32_guest_mmu().translate(KERNEL_DATA_BASE, PPC32_XLATE_DR, 4, false);
		if (xr.ok) {
			nw_la_kdp_pa = xr.pa;
#if NW_BOOT_LOG
			char buf[96];
			snprintf(buf, sizeof(buf), "G1: first EMUL_OP %u at 68k pc %08x; KDP LA %08x -> PA %08x",
				 (unsigned)emul_op, gpr(24) - 2u, (unsigned)KERNEL_DATA_BASE, (unsigned)xr.pa);
			nw_boot_diag(buf);
#endif
		}
	}
	WriteMacInt32(XLM_68K_R25, gpr(25));
	WriteMacInt32(XLM_RUN_MODE, MODE_EMUL_OP);
	for (int i = 0; i < 8; i++)
		r68.d[i] = gpr(8 + i);
	for (int i = 0; i < 7; i++)
		r68.a[i] = gpr(16 + i);
	r68.a[7] = gpr(1);
	uint32 saved_cr = get_cr();
	/* In New World, the nanokernel sets CR2's wake/interrupt bits before
	 * returning to the 68k emulator. Its dispatch at 6806d140 tests CR bit
	 * 10 to handle that interrupt and eventually issue the OpenPIC EOI.
	 * The legacy mask clears bits 9 and 10: an interrupt delivered at a
	 * host callback (notably ExtFSHFS) would therefore never complete. */
	if (ROMType != ROMTYPE_NEWWORLD)
		saved_cr &= 0xff9fffff; // mask_operand::compute(11, 8)
	uint32 saved_xer = get_xer();
	EmulOp(&r68, gpr(24), emul_op);
#if NW_BOOT_LOG
	if (spcflags().test(SPCFLAG_CPU_EXEC_RETURN) && getenv("NW_TEST_NK_STATE_PATH")) {
		NW_DIAG("NW-BOOT CPU return pending after EmulOp selector=%u pc=%08x depth=%d last_return=%08x last_depth=%d r24=%08x r29=%08x\n",
		       emul_op, pc(), nw_68k_execute_depth(), nw_last_return_pc, nw_last_return_depth, gpr(24), gpr(29));
		fflush(stdout);
	}
#endif
	set_cr(saved_cr);
	set_xer(saved_xer);
	for (int i = 0; i < 8; i++)
		gpr(8 + i) = r68.d[i];
	for (int i = 0; i < 7; i++)
		gpr(16 + i) = r68.a[i];
	gpr(1) = r68.a[7];
	// Scripted timing runs borrow this valid NK entry frame. Preserve the
	// callback's complete return state around the nested diagnostic routine.
	if (nw_script_benchmark_pending()) {
		nw68_nk_state saved;
		nw_68k_snapshot(saved);
		nw_script_guest_benchmark();
		nw_68k_restore(saved);
	}
	WriteMacInt32(XLM_RUN_MODE, MODE_68K);
}

// Execute SheepShaver instruction
void sheepshaver_cpu::execute_sheep(uint32 opcode)
{
//	D(bug("Extended opcode %08x at %08x (68k pc %08x)\n", opcode, pc(), gpr(24)));
	assert((((opcode >> 26) & 0x3f) == 6) && OP_MAX <= 64 + 3);

	switch (opcode & 0x3f) {
	case 0:		// EMUL_RETURN
		QuitEmulator();
		break;

	case 1:		// EXEC_RETURN
		/* An external interrupt may return directly to this host trampoline.
		 * Ordinary NK handlers go through 66090/d114 before dispatching the
		 * next instruction; unwinding here bypassed that check and discarded
		 * the pending CR2 state with the nested frame's saved registers. Give
		 * the NK its normal slow dispatch, with this instruction as the retry
		 * target, before allowing the host frame to leave. The guest decides
		 * whether the interrupt is masked and issues its own IACK/EOI. */
		if (ROMType == ROMTYPE_NEWWORLD && ppc32_guest_mmu_enabled() &&
		    pc() == 0x680ff208u && gpr(29) == pc() &&
		    gpr(30) == 0x68060000u && CR_LT_field<2>::test(get_cr())) {
			lr() = pc();
			pc() = gpr(30) | 0xd114u;
			break;
		}
#if NW_BOOT_LOG
		nw_last_return_pc = pc();
		nw_last_return_depth = nw_68k_execute_depth();
		if (nw_last_return_depth <= 1 && getenv("NW_TEST_NK_STATE_PATH")) {
			NW_DIAG("NW-BOOT CPU outer return requested pc=%08x opcode=%08x r24=%08x r29=%08x cr=%08x\n",
			       pc(), opcode, gpr(24), gpr(29), get_cr());
			fflush(stdout);
		}
#endif
		spcflags().set(SPCFLAG_CPU_EXEC_RETURN);
		break;

	case 2:		// EXEC_NATIVE
		execute_native_op(NATIVE_OP_field::extract(opcode));
		if (FN_field::test(opcode))
			pc() = lr();
		else
			pc() += 4;
		break;

	default:	// EMUL_OP
		execute_emul_op(EMUL_OP_field::extract(opcode) - 3);
		pc() += 4;
		break;
	}
}

// Compile one instruction
#if PPC_ENABLE_JIT
int sheepshaver_cpu::compile1(codegen_context_t & cg_context)
{
	const instr_info_t *ii = cg_context.instr_info;
	if (ii->mnemo != PPC_I(SHEEP))
		return COMPILE_FAILURE;

	int status = COMPILE_FAILURE;
	powerpc_dyngen & dg = cg_context.codegen;
	uint32 opcode = cg_context.opcode;

	switch (opcode & 0x3f) {
	case 0:		// EMUL_RETURN
		dg.gen_invoke(QuitEmulator);
		status = COMPILE_CODE_OK;
		break;

	case 1:		// EXEC_RETURN
		dg.gen_spcflags_set(SPCFLAG_CPU_EXEC_RETURN);
		// Don't check for pending interrupts, we do know we have to
		// get out of this block ASAP
		dg.gen_exec_return();
		status = COMPILE_EPILOGUE_OK;
		break;

	case 2: {	// EXEC_NATIVE
		uint32 selector = NATIVE_OP_field::extract(opcode);
		switch (selector) {
#if !PPC_REENTRANT_JIT
		// Filter out functions that may invoke Execute68k() or
		// CallMacOS(), this would break reentrancy as they could
		// invalidate the translation cache and even overwrite
		// continuation code when we are done with them.
		case NATIVE_PATCH_NAME_REGISTRY:
			dg.gen_invoke(DoPatchNameRegistry);
			status = COMPILE_CODE_OK;
			break;
		case NATIVE_VIDEO_INSTALL_ACCEL:
			dg.gen_invoke(VideoInstallAccel);
			status = COMPILE_CODE_OK;
			break;
		case NATIVE_VIDEO_VBL:
			if (ROMType == ROMTYPE_NEWWORLD)
				break;	/* generic path: VideoDriverVBL + r3 = kIsrIsComplete (0) */
			dg.gen_invoke(VideoVBL);
			status = COMPILE_CODE_OK;
			break;
		case NATIVE_GET_RESOURCE:
		case NATIVE_GET_1_RESOURCE:
		case NATIVE_GET_IND_RESOURCE:
		case NATIVE_GET_1_IND_RESOURCE:
		case NATIVE_R_GET_RESOURCE: {
			static const uint32 get_resource_ptr[] = {
				XLM_GET_RESOURCE,
				XLM_GET_1_RESOURCE,
				XLM_GET_IND_RESOURCE,
				XLM_GET_1_IND_RESOURCE,
				XLM_R_GET_RESOURCE
			};
			uint32 old_get_resource = ReadMacInt32(get_resource_ptr[selector - NATIVE_GET_RESOURCE]);
			typedef void (*func_t)(dyngen_cpu_base, uint32);
			func_t func = &sheepshaver_cpu::call_get_resource;
			dg.gen_invoke_CPU_im(func, old_get_resource);
			status = COMPILE_CODE_OK;
			break;
		}
#endif
		case NATIVE_CHECK_LOAD_INVOC:
			dg.gen_load_T0_GPR(3);
			dg.gen_load_T1_GPR(4);
			dg.gen_se_16_32_T1();
			dg.gen_load_T2_GPR(5);
			dg.gen_invoke_T0_T1_T2((void (*)(uint32, uint32, uint32))check_load_invoc);
			status = COMPILE_CODE_OK;
			break;
		case NATIVE_NAMED_CHECK_LOAD_INVOC:
			dg.gen_load_T0_GPR(3);
			dg.gen_load_T1_GPR(4);
			dg.gen_load_T2_GPR(5);
			dg.gen_invoke_T0_T1_T2((void (*)(uint32, uint32, uint32))named_check_load_invoc);
			status = COMPILE_CODE_OK;
			break;
		case NATIVE_NQD_SYNC_HOOK:
			dg.gen_load_T0_GPR(3);
			dg.gen_invoke_T0_ret_T0((uint32 (*)(uint32))NQD_sync_hook);
			dg.gen_store_T0_GPR(3);
			status = COMPILE_CODE_OK;
			break;
		case NATIVE_NQD_BITBLT_HOOK:
			dg.gen_load_T0_GPR(3);
			dg.gen_invoke_T0_ret_T0((uint32 (*)(uint32))NQD_bitblt_hook);
			dg.gen_store_T0_GPR(3);
			status = COMPILE_CODE_OK;
			break;
		case NATIVE_NQD_FILLRECT_HOOK:
			dg.gen_load_T0_GPR(3);
			dg.gen_invoke_T0_ret_T0((uint32 (*)(uint32))NQD_fillrect_hook);
			dg.gen_store_T0_GPR(3);
			status = COMPILE_CODE_OK;
			break;
		case NATIVE_NQD_UNKNOWN_HOOK:
			dg.gen_load_T0_GPR(3);
			dg.gen_invoke_T0_ret_T0((uint32 (*)(uint32))NQD_unknown_hook);
			dg.gen_store_T0_GPR(3);
			status = COMPILE_CODE_OK;
			break;
		case NATIVE_NQD_LINES_HOOK:
			dg.gen_load_T0_GPR(3);
			dg.gen_invoke_T0_ret_T0((uint32 (*)(uint32))NQD_lines_hook);
			dg.gen_store_T0_GPR(3);
			status = COMPILE_CODE_OK;
			break;
		case NATIVE_NQD_FILLMASK_HOOK:
			dg.gen_load_T0_GPR(3);
			dg.gen_invoke_T0_ret_T0((uint32 (*)(uint32))NQD_fillmask_hook);
			dg.gen_store_T0_GPR(3);
			status = COMPILE_CODE_OK;
			break;
		case NATIVE_NQD_FILLMASK:
			dg.gen_load_T0_GPR(3);
			dg.gen_invoke_T0((void (*)(uint32))NQD_fillmask);
			status = COMPILE_CODE_OK;
			break;
		case NATIVE_NQD_BITBLT:
			dg.gen_load_T0_GPR(3);
			dg.gen_invoke_T0((void (*)(uint32))NQD_bitblt);
			status = COMPILE_CODE_OK;
			break;
		case NATIVE_NQD_INVRECT:
			dg.gen_load_T0_GPR(3);
			dg.gen_invoke_T0((void (*)(uint32))NQD_invrect);
			status = COMPILE_CODE_OK;
			break;
		case NATIVE_NQD_FILLRECT:
			dg.gen_load_T0_GPR(3);
			dg.gen_invoke_T0((void (*)(uint32))NQD_fillrect);
			status = COMPILE_CODE_OK;
			break;
		}
		// Could we fully translate this NativeOp?
		if (status == COMPILE_CODE_OK) {
			if (!FN_field::test(opcode))
				cg_context.done_compile = false;
			else {
				dg.gen_load_T0_LR_aligned();
				dg.gen_set_PC_T0();
				cg_context.done_compile = true;
			}
			break;
		}
#if PPC_REENTRANT_JIT
		// Try to execute NativeOp trampoline
		if (!FN_field::test(opcode))
			dg.gen_set_PC_im(cg_context.pc + 4);
		else {
			dg.gen_load_T0_LR_aligned();
			dg.gen_set_PC_T0();
		}
		dg.gen_mov_32_T0_im(selector);
		dg.gen_jmp(native_op_trampoline);
		cg_context.done_compile = true;
		status = COMPILE_EPILOGUE_OK;
		break;
#else
		// Invoke NativeOp handler
		if (!FN_field::test(opcode)) {
			typedef void (*func_t)(dyngen_cpu_base, uint32);
			func_t func = &sheepshaver_cpu::call_execute_native_op;
			dg.gen_invoke_CPU_im(func, selector);
			cg_context.done_compile = false;
			status = COMPILE_CODE_OK;
		}
		// Otherwise, let it generate a call to execute_sheep() which
		// will cause necessary updates to the program counter
		break;
#endif
	}

	default: {	// EMUL_OP
		uint32 emul_op = EMUL_OP_field::extract(opcode) - 3;
#if PPC_REENTRANT_JIT
		// Try to execute EmulOp trampoline
		dg.gen_set_PC_im(cg_context.pc + 4);
		dg.gen_mov_32_T0_im(emul_op);
		dg.gen_jmp(emul_op_trampoline);
		cg_context.done_compile = true;
		status = COMPILE_EPILOGUE_OK;
		break;
#else
		// Invoke EmulOp handler
		typedef void (*func_t)(dyngen_cpu_base, uint32);
		func_t func = &sheepshaver_cpu::call_execute_emul_op;
		dg.gen_invoke_CPU_im(func, emul_op);
		cg_context.done_compile = false;
		status = COMPILE_CODE_OK;
		break;
#endif
	}
	}
	return status;
}
#endif

// Handle MacOS interrupt
void sheepshaver_cpu::interrupt(uint32 entry)
{
#if EMUL_TIME_STATS
	ppc_interrupt_count++;
	const clock_t interrupt_start = clock();
#endif

	// Save program counters and branch registers
	uint32 saved_pc = pc();
	uint32 saved_lr = lr();
	uint32 saved_ctr= ctr();
	uint32 saved_sp = gpr(1);

	// Initialize stack pointer to SheepShaver alternate stack base
	gpr(1) = (uint32)(SignalStackBase() - 64);

	// Build trampoline to return from interrupt
	SheepVar32 trampoline = POWERPC_EXEC_RETURN;

	// Prepare registers for nanokernel interrupt routine
	WriteMacInt32(KERNEL_DATA_BASE + 0x004, gpr(1));
	WriteMacInt32(KERNEL_DATA_BASE + 0x018, gpr(6));

	gpr(6) = ReadMacInt32(KERNEL_DATA_BASE + 0x65c);
	assert(gpr(6) != 0);
	WriteMacInt32(gpr(6) + 0x13c, gpr(7));
	WriteMacInt32(gpr(6) + 0x144, gpr(8));
	WriteMacInt32(gpr(6) + 0x14c, gpr(9));
	WriteMacInt32(gpr(6) + 0x154, gpr(10));
	WriteMacInt32(gpr(6) + 0x15c, gpr(11));
	WriteMacInt32(gpr(6) + 0x164, gpr(12));
	WriteMacInt32(gpr(6) + 0x16c, gpr(13));

	gpr(1)  = KernelDataAddr;
	gpr(7)  = ReadMacInt32(KERNEL_DATA_BASE + 0x660);
	gpr(8)  = 0;
	gpr(10) = trampoline.addr();
	gpr(12) = trampoline.addr();
	gpr(13) = get_cr();

	// rlwimi. r7,r7,8,0,0
	uint32 result = op_ppc_rlwimi::apply(gpr(7), 8, 0x80000000, gpr(7));
	record_cr0(result);
	gpr(7) = result;

	gpr(11) = 0xf072; // MSR (SRR1)
	cr().set((gpr(11) & 0x0fff0000) | (get_cr() & ~0x0fff0000));

	// Enter nanokernel
	execute(entry);

	// Restore program counters and branch registers
	pc() = saved_pc;
	lr() = saved_lr;
	ctr()= saved_ctr;
	gpr(1) = saved_sp;

#if EMUL_TIME_STATS
	interrupt_time += (clock() - interrupt_start);
#endif
}

// Execute 68k routine
void sheepshaver_cpu::execute_68k(uint32 entry, M68kRegisters *r)
{
#if EMUL_TIME_STATS
	exec68k_count++;
	const clock_t exec68k_start = clock();
#endif

#if SAFE_EXEC_68K
	if (ReadMacInt32(XLM_RUN_MODE) != MODE_EMUL_OP &&
	    !(ROMType == ROMTYPE_NEWWORLD &&
	      (ReadMacInt32(XLM_RUN_MODE) == MODE_NATIVE ||
	       ReadMacInt32(XLM_RUN_MODE) == MODE_68K)))
		printf("FATAL: Execute68k() not called from EMUL_OP mode\n");
#endif

	// Save program counters and branch registers
	uint32 saved_pc = pc();
	uint32 saved_lr = lr();
	uint32 saved_ctr= ctr();
	uint32 saved_cr = get_cr();

	// Create MacOS stack frame
	// FIXME: make sure MacOS doesn't expect PPC registers to live on top
	uint32 sp = gpr(1);
	gpr(1) -= 56;
	WriteMacInt32(gpr(1), sp);

	// Save PowerPC registers
	uint32 saved_GPRs[19];
	memcpy(&saved_GPRs[0], &gpr(13), sizeof(uint32)*(32-13));
#if SAVE_FP_EXEC_68K
	double saved_FPRs[18];
	memcpy(&saved_FPRs[0], &fpr(14), sizeof(double)*(32-14));
#endif

	// Setup registers for 68k emulator
	cr().set(CR_SO_field<2>::mask());			// Supervisor mode
	for (int i = 0; i < 8; i++)					// d[0]..d[7]
	  gpr(8 + i) = r->d[i];
	for (int i = 0; i < 7; i++)					// a[0]..a[6]
	  gpr(16 + i) = r->a[i];
	gpr(23) = 0;
	gpr(24) = entry;
	gpr(25) = ReadMacInt32(XLM_68K_R25);		// MSB of SR
	if (ROMType == ROMTYPE_NEWWORLD) {
		/*
		 * Nested in the emulator: r30/r31/r26/r28 are already the
		 * emulator's. A native caller (the Sound panel waits in PPC)
		 * does not have them. Load the NK v2 entry set then.
		 * r29 is the opcode table at 0x68080000. r30 is emulator code.
		 * r31 is Emulator Data.
		 */
		if (ReadMacInt32(XLM_RUN_MODE) != MODE_EMUL_OP &&
		    ReadMacInt32(XLM_RUN_MODE) != MODE_68K) {
			gpr(26) = 0;
			gpr(28) = 0;
			gpr(30) = 0x68060000;
			gpr(31) = KernelDataAddr + 0x1000;
			gpr(29) = 0x68080000;
		}
		gpr(29) &= 0xfff80000u;
	} else {
		gpr(26) = 0;
		gpr(28) = 0;								// VBR
		gpr(29) = ReadMacInt32(KERNEL_DATA_BASE + 0x1074);		// Pointer to opcode table
		gpr(30) = ReadMacInt32(KERNEL_DATA_BASE + 0x1078);		// Address of emulator
		gpr(31) = KernelDataAddr + 0x1000;
	}

	// Push return address (points to EXEC_RETURN opcode) on stack
	gpr(1) -= 4;
	WriteMacInt32(gpr(1), XLM_EXEC_RETURN_OPCODE);
	
	// Rentering 68k emulator
	WriteMacInt32(XLM_RUN_MODE, MODE_68K);

	// Set r0 to 0 for 68k emulator
	gpr(0) = 0;

	// Execute 68k opcode
	uint32 opcode = ReadMacInt16(gpr(24));
	gpr(27) = (int32)(int16)ReadMacInt16(gpr(24) += 2);
	gpr(29) += opcode * 8;
	if (PrefsFindBool("jit68k_host"))
		nw_68k_jit_execute(this, gpr(29));
	else
		execute(gpr(29));

	// Save r25 (contains current 68k interrupt level)
	WriteMacInt32(XLM_68K_R25, gpr(25));

	// Reentering EMUL_OP mode
	WriteMacInt32(XLM_RUN_MODE, MODE_EMUL_OP);

	// Save 68k registers
	for (int i = 0; i < 8; i++)					// d[0]..d[7]
	  r->d[i] = gpr(8 + i);
	for (int i = 0; i < 7; i++)					// a[0]..a[6]
	  r->a[i] = gpr(16 + i);

	// Restore PowerPC registers
	memcpy(&gpr(13), &saved_GPRs[0], sizeof(uint32)*(32-13));
#if SAVE_FP_EXEC_68K
	memcpy(&fpr(14), &saved_FPRs[0], sizeof(double)*(32-14));
#endif

	// Cleanup stack
	gpr(1) += 56;

	// Restore program counters and branch registers
	pc() = saved_pc;
	lr() = saved_lr;
	ctr()= saved_ctr;
	set_cr(saved_cr);

#if EMUL_TIME_STATS
	exec68k_time += (clock() - exec68k_start);
#endif
}

// Call MacOS PPC code
uint32 sheepshaver_cpu::execute_macos_code(uint32 tvect, int nargs, uint32 const *args)
{
#if EMUL_TIME_STATS
	macos_exec_count++;
	const clock_t macos_exec_start = clock();
#endif

	// Save program counters and branch registers
	uint32 saved_pc = pc();
	uint32 saved_lr = lr();
	uint32 saved_ctr= ctr();

	// Build trampoline with EXEC_RETURN
	SheepVar32 trampoline = POWERPC_EXEC_RETURN;
	lr() = trampoline.addr();

	gpr(1) -= 64;								// Create stack frame
	uint32 proc = ReadMacInt32(tvect);			// Get routine address
	uint32 toc = ReadMacInt32(tvect + 4);		// Get TOC pointer

	// Save PowerPC registers
	uint32 regs[8];
	regs[0] = gpr(2);
	for (int i = 0; i < nargs; i++)
		regs[i + 1] = gpr(i + 3);

	// Prepare and call MacOS routine
	gpr(2) = toc;
	for (int i = 0; i < nargs; i++)
		gpr(i + 3) = args[i];
	execute(proc);
	uint32 retval = gpr(3);

	// Restore PowerPC registers
	for (int i = 0; i <= nargs; i++)
		gpr(i + 2) = regs[i];

	// Cleanup stack
	gpr(1) += 64;

	// Restore program counters and branch registers
	pc() = saved_pc;
	lr() = saved_lr;
	ctr()= saved_ctr;

#if EMUL_TIME_STATS
	macos_exec_time += (clock() - macos_exec_start);
#endif

	return retval;
}

// Execute ppc routine
inline void sheepshaver_cpu::execute_ppc(uint32 entry)
{
	// Save branch registers
	uint32 saved_lr = lr();

	SheepVar32 trampoline = POWERPC_EXEC_RETURN;
	WriteMacInt32(trampoline.addr(), POWERPC_EXEC_RETURN);
	lr() = trampoline.addr();

	execute(entry);

	// Restore branch registers
	lr() = saved_lr;
}

void sheepshaver_cpu::nw_invoke_mm_ppc(uint32 entry)
{
	execute_ppc(entry);
	nw_mixedmode_leave();
}

void sheepshaver_cpu::call_get_resource(powerpc_cpu * cpu, uint32 old_get_resource) {
	static_cast<sheepshaver_cpu *>(cpu)->get_resource(old_get_resource);
}

// Resource Manager thunk
inline void sheepshaver_cpu::get_resource(uint32 old_get_resource)
{
	uint32 type = gpr(3);
	int16 id = gpr(4);

	// Create stack frame
	gpr(1) -= 56;

	// Call old routine
	execute_ppc(old_get_resource);

	// Call CheckLoad()
	uint32 handle = gpr(3);
	check_load_invoc(type, id, handle);
	gpr(3) = handle;

	// Cleanup stack
	gpr(1) += 56;
}


/**
 *		SheepShaver CPU engine interface
 **/

// PowerPC CPU emulator
static sheepshaver_cpu *ppc_cpu = NULL;

/*
 *  Guest PC sampler (diagnostic, NW_GUEST_PROF=1; costs nothing when off). A host thread reads the guest PC and the
 *  idle flag every millisecond, which attributes time to guest code that a host profile cannot see (the JIT's own code
 *  has no symbols) and shows how much of the time the guest sits in its idle loop. Every 8 s it prints the idle share,
 *  the share per large address region and the hottest 64-byte blocks, then starts again.
 */
extern volatile int nw_guest_idle_flag;
extern uint32_t nw_trap_hist[65536];
extern volatile int nw_trap_prof_on;
#include "nw_68k_jit.h"
#include "nw_68k_core.h"
extern uint64_t nw_trapsite_key[16384];
extern uint32_t nw_trapsite_cnt[16384];
#include <sys/time.h>
#include <map>
#include <chrono>
#include <functional>
#include <thread>
#include <atomic>
static void nw_guest_prof_main(bool dump_words)
{
	std::map<uint32, unsigned> blocks, regions, pc68;
	unsigned total = 0, idle = 0, ticks = 0;
	const char *pe = getenv("NW_GUEST_PROF_MS");	// report period, default 8000 ms
	const unsigned period = pe && atoi(pe) > 0 ? (unsigned)atoi(pe) : 8000u;
	const auto t_start = std::chrono::steady_clock::now();
	nw_trap_prof_on = 1;
	{ struct timeval tv0; gettimeofday(&tv0, NULL); printf("GPROFSTART epoch=%lld.%03d\n", (long long)tv0.tv_sec, (int)(tv0.tv_usec / 1000)); }
	static uint32_t trap_prev[65536];
	for (;;) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		if (!ppc_cpu)
			continue;
		total++;
		if (nw_guest_idle_flag) {
			idle++;
		} else {
			const uint32 pc = ppc_cpu->sample_pc();
			blocks[pc & ~63u]++;
			regions[pc >> 20]++;
			if ((pc >> 20) == 0x680)			// in the ROM's 68k emulator: r24 is the 68k PC
				pc68[ppc_cpu->sample_gpr(24) & ~63u]++;
		}
		if (++ticks < period)
			continue;
		ticks = 0;
		printf("GPROF t=%.1fs %u samples: idle %.1f%%; regions (MB-aligned, %% of non-idle):", std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count(), total, 100.0 * idle / (total ? total : 1));
		std::multimap<unsigned, uint32, std::greater<unsigned>> rs;
		for (auto &kv : regions) rs.insert({kv.second, kv.first});
		int n = 0;
		for (auto &kv : rs) { if (n++ >= 8) break; printf(" %03xMB:%.1f", (unsigned)kv.second, 100.0 * kv.first / (total - idle ? total - idle : 1)); }
		printf("\n");
		std::multimap<unsigned, uint32, std::greater<unsigned>> bs;
		for (auto &kv : blocks) bs.insert({kv.second, kv.first});
		n = 0;
		for (auto &kv : bs) {
			if (n++ >= 24) break;
			printf("GPROF   %08x %.2f%%", (unsigned)kv.second, 100.0 * kv.first / (total - idle ? total - idle : 1));
			if (dump_words && n <= 14) {	// NW_GUEST_PROF=2: the block's 16 instruction words, for classifying the hot code
				// Translate on a private snapshot of the MMU (never the live one: translate() fills the
				// TLB, which the emulation thread owns) and without recording accesses. Only physical
				// addresses inside RAM are read; anything else prints as dashes.
				extern uint32 RAMBase, RAMSize;
				static ppc32_mmu snap;
				snap = ppc32_guest_mmu();
				printf(" :");
				for (uint32 w = 0; w < 16; w++) {
					const ppc32_xlate_result xr = snap.translate(kv.second + w * 4u, PPC32_XLATE_IR, 4, false, false);
					const bool in_rom = xr.ok && xr.pa >= ROMBase && (uint64_t)xr.pa + 4 <= (uint64_t)ROMBase + ROM_AREA_SIZE;
					if (xr.ok && ((xr.pa >= RAMBase && (uint64_t)xr.pa + 4 <= (uint64_t)RAMBase + RAMSize) || in_rom))
						printf(" %08x", (unsigned)vm_read_memory_4(xr.pa));
					else
						printf(" --------");
				}
			}
			printf("\n");
		}
		{
			std::multimap<unsigned, uint32, std::greater<unsigned>> ps;
			for (auto &kv : pc68) ps.insert({kv.second, kv.first});
			printf("GPROF68 68k pc (r24) top:");
			n = 0;
			for (auto &kv : ps) { if (n++ >= 10) break; printf(" %08x:%.1f", (unsigned)kv.second, 100.0 * kv.first / (total - idle ? total - idle : 1)); }
			printf("\n");
			if (dump_words && getenv("NW_DUMP68K")) {	// NW_DUMP68K=<hex start>,<hex length>: one shot, first period after 12 s
				static bool done;
				if (!done && std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count() > 12.0) {
					done = true;
					unsigned long st = 0, len = 0;
					sscanf(getenv("NW_DUMP68K"), "%lx,%lx", &st, &len);
					const uint32 pa = nw_la_to_pa((uint32)st);
					if (len <= 0x40000 && (nw_pa_kind(pa) == NW_PA_RAM || nw_pa_kind(pa) == NW_PA_ROM)) {
						printf("GPROF68D %08lx %lx:", st, len);
						for (unsigned long b = 0; b < len; b++) printf("%02x", (unsigned)vm_read_memory_1(pa + b));
						printf("\n");
					}
				}
			}
			if (dump_words) {		// the 64 bytes of each of the 8 hottest 68k blocks, as hex, for offline disassembly
				n = 0;
				for (auto &kv : ps) {
					if (n++ >= 8) break;
					const uint32 pa = nw_la_to_pa(kv.second);
					const int kind = nw_pa_kind(pa);
					printf("GPROF68W %08x %.2f%%:", (unsigned)kv.second, 100.0 * kv.first / (total - idle ? total - idle : 1));
					if (kind == NW_PA_RAM || kind == NW_PA_ROM)
						for (uint32 b = 0; b < 64; b++) printf("%02x", (unsigned)vm_read_memory_1(pa + b));
					printf("\n");
				}
			}
		}
		{
			std::multimap<unsigned, unsigned, std::greater<unsigned>> ts;
			unsigned all = 0;
			for (unsigned t = 0; t < 65536; t++) {
				const uint32_t d = nw_trap_hist[t] - trap_prev[t];
				trap_prev[t] = nw_trap_hist[t];
				if (d) { ts.insert({d, t}); all += d; }
			}
			{	// the busiest (trap, caller pc) pairs of this period
				std::multimap<unsigned, uint64_t, std::greater<unsigned>> ss;
				for (unsigned i = 0; i < 16384; i++) {
					if (nw_trapsite_key[i] && nw_trapsite_cnt[i]) { ss.insert({nw_trapsite_cnt[i], nw_trapsite_key[i]}); }
					nw_trapsite_cnt[i] = 0;
				}
				printf("GPROFSITE:");
				int m = 0;
				for (auto &kv : ss) { if (m++ >= 8) break; printf(" %04x@%08x:%u", (unsigned)((kv.second >> 32) & 0xffff), (unsigned)(kv.second & 0xffffffffu), kv.first); }
				printf("\n");
			}
			printf("GPROFTRAP %u traps; top:", all);
			n = 0;
			for (auto &kv : ts) { if (n++ >= 8) break; printf(" %04x:%u", kv.second, kv.first); }
			printf("\n");
		}
		{
			static nw68_cache_stats prev;
			static uint64_t prev_native, prev_fb;
			const nw68_cache_stats cs = nw68_cache_statistics();
			const uint64_t nat = nw_68k_native_count(), fb = nw_68k_fallback_count();
			printf("GPROF68C dispatches native=%llu fallback=%llu | compiled=%llu hits=%llu invalidated=%llu blocks=%llu block_insn=%llu decoded_hits=%llu\n",
			       (unsigned long long)(nat - prev_native), (unsigned long long)(fb - prev_fb),
			       (unsigned long long)(cs.compiled - prev.compiled), (unsigned long long)(cs.hits - prev.hits),
			       (unsigned long long)(cs.invalidated - prev.invalidated), (unsigned long long)(cs.blocks - prev.blocks),
			       (unsigned long long)(cs.block_instructions - prev.block_instructions), (unsigned long long)(cs.decoded_hits - prev.decoded_hits));
			prev = cs; prev_native = nat; prev_fb = fb;
		}
		{
			static uint64_t pb, pi, ph, plf, plm, pcut[NW_JIT_CUT_N], phop[NW_JIT_HOP_N];
			const uint64_t b = nw_jit_exec_blocks(), i = nw_jit_exec_insns(), h = nw_jit_chain_hops();
			uint64_t lf = 0, lm = 0;
			nw_jit_link_stats(&lf, &lm);
			printf("GPROFJIT blocks=%llu insns=%llu (%.1f/block) chain_hops=%llu link_fast=%llu links_made=%llu ibtc_fills=%llu epochs[link,itlbflush,itlbdrop,mtsr]=%llu,%llu,%llu,%llu cuts:",
			       (unsigned long long)(b - pb), (unsigned long long)(i - pi), b > pb ? double(i - pi) / double(b - pb) : 0.0,
			       (unsigned long long)(h - ph), (unsigned long long)(lf - plf), (unsigned long long)(lm - plm), (unsigned long long)nw_jit_ibtc_fills(), (unsigned long long)nw_jit_ibtc_epoch_count(0), (unsigned long long)nw_jit_ibtc_epoch_count(1), (unsigned long long)nw_jit_ibtc_epoch_count(2), (unsigned long long)nw_jit_ibtc_epoch_count(3));
			plf = lf; plm = lm;
			static const char *const cn[] = {"ends", "page", "peek", "class", "unsup", "mem0", "mem2", "max", "io"};
			for (int r = 0; r < NW_JIT_CUT_N; r++) { const uint64_t c = nw_jit_cut_count(r); printf(" %s=%llu", cn[r], (unsigned long long)(c - pcut[r])); pcut[r] = c; }
			printf(" hopstop:");
			static const char *const hn[] = {"cap", "nochain", "pcmis", "itlb", "aline", "cmiss", "vec", "fp", "cnull"};
			for (int r = 0; r < NW_JIT_HOP_N; r++) { const uint64_t c = nw_jit_hop_stop_count(r); printf(" %s=%llu", hn[r], (unsigned long long)(c - phop[r])); phop[r] = c; }
			printf("\n");
			pb = b; pi = i; ph = h;
		}
		fflush(stdout);
		blocks.clear(); regions.clear(); pc68.clear(); total = idle = 0;
	}
}
static void nw_guest_prof_start()
{
	const char *e = getenv("NW_GUEST_PROF");
	if (e && (e[0] == '1' || e[0] == '2')) {
		static std::thread t(nw_guest_prof_main, e[0] == '2');
		t.detach();
	}
}

void FlushCodeCache(uintptr start, uintptr end)
{
	D(bug("FlushCodeCache(%08x, %08x)\n", start, end));
	ppc_cpu->invalidate_cache_range(start, end);
}

// Dump PPC registers
static void dump_registers(void)
{
	ppc_cpu->dump_registers();
}

// Dump log
static void dump_log(void)
{
	ppc_cpu->dump_log();
}

static int read_mem(bfd_vma memaddr, bfd_byte *myaddr, int length, struct disassemble_info *info)
{
	Mac2Host_memcpy(myaddr, (uint32)memaddr, length);
	return 0;
}

static void dump_disassembly(const uint32 pc, const int prefix_count, const int suffix_count)
{
	struct disassemble_info info;
	INIT_DISASSEMBLE_INFO(info, stderr, fprintf);
	info.read_memory_func = read_mem;

	const int count = prefix_count + suffix_count + 1;
	const uint32 base_addr = pc - prefix_count * 4;
	for (int i = 0; i < count; i++) {
		const bfd_vma addr = base_addr + i * 4;
		fprintf(stderr, "%s0x%8llx:  ", addr == pc ? " >" : "  ", addr);
		print_insn_ppc(addr, &info);
		fprintf(stderr, "\n");
	}
}

sigsegv_return_t sigsegv_handler(sigsegv_info_t *sip)
{
#if ENABLE_VOSF
	// Handle screen fault
	extern bool Screen_fault_handler(sigsegv_info_t *sip);
	if (Screen_fault_handler(sip))
		return SIGSEGV_RETURN_SUCCESS;
#endif

	const uintptr addr = (uintptr)sigsegv_get_fault_address(sip);
#if HAVE_SIGSEGV_SKIP_INSTRUCTION
	// Ignore writes to ROM
	if ((addr - (uintptr)ROMBaseHost) < ROM_SIZE)
		return SIGSEGV_RETURN_SKIP_INSTRUCTION;

	// Get program counter of target CPU
	sheepshaver_cpu * const cpu = ppc_cpu;
	const uint32 pc = cpu->pc();
	
	// Fault in Mac ROM or RAM?
	bool mac_fault = (pc >= ROMBase && pc < (ROMBase + ROM_AREA_SIZE)) || (pc >= RAMBase && pc < (RAMBase + RAMSize)) || (pc >= DR_CACHE_BASE && pc < (DR_CACHE_BASE + DR_CACHE_SIZE));
	if (mac_fault) {

		// "VM settings" during MacOS 8 installation
		if (pc == ROMBase + 0x488160 && cpu->gpr(20) == 0xf8000000)
			return SIGSEGV_RETURN_SKIP_INSTRUCTION;
	
		// MacOS 8.5 installation
		else if (pc == ROMBase + 0x488140 && cpu->gpr(16) == 0xf8000000)
			return SIGSEGV_RETURN_SKIP_INSTRUCTION;
	
		// MacOS 8 serial drivers on startup
		else if (pc == ROMBase + 0x48e080 && (cpu->gpr(8) == 0xf3012002 || cpu->gpr(8) == 0xf3012000))
			return SIGSEGV_RETURN_SKIP_INSTRUCTION;
	
		// MacOS 8.1 serial drivers on startup
		else if (pc == ROMBase + 0x48c5e0 && (cpu->gpr(20) == 0xf3012002 || cpu->gpr(20) == 0xf3012000))
			return SIGSEGV_RETURN_SKIP_INSTRUCTION;
		else if (pc == ROMBase + 0x4a10a0 && (cpu->gpr(20) == 0xf3012002 || cpu->gpr(20) == 0xf3012000))
			return SIGSEGV_RETURN_SKIP_INSTRUCTION;
	
		// MacOS 8.6 serial drivers on startup (with DR Cache and OldWorld ROM)
		else if ((pc - DR_CACHE_BASE) < DR_CACHE_SIZE && (cpu->gpr(16) == 0xf3012002 || cpu->gpr(16) == 0xf3012000))
			return SIGSEGV_RETURN_SKIP_INSTRUCTION;
		else if ((pc - DR_CACHE_BASE) < DR_CACHE_SIZE && (cpu->gpr(20) == 0xf3012002 || cpu->gpr(20) == 0xf3012000))
			return SIGSEGV_RETURN_SKIP_INSTRUCTION;

		// Ignore writes to the zero page
		else if ((uint32)(addr - SheepMem::ZeroPage()) < (uint32)SheepMem::PageSize())
			return SIGSEGV_RETURN_SKIP_INSTRUCTION;

		// Ignore all other faults, if requested
		if (PrefsFindBool("ignoresegv"))
			return SIGSEGV_RETURN_SKIP_INSTRUCTION;
	}
#else
#error "FIXME: You don't have the capability to skip instruction within signal handlers"
#endif

	fprintf(stderr, "SIGSEGV\n");
	fprintf(stderr, "  pc %p\n", sigsegv_get_fault_instruction_address(sip));
	fprintf(stderr, "  ea %p\n", sigsegv_get_fault_address(sip));
	dump_registers();
	dump_log();
	dump_disassembly(pc, 8, 8);

	enter_mon();
	QuitEmulator();

	return SIGSEGV_RETURN_FAILURE;
}

/*
 *  Initialize CPU emulation
 */

static bool guest_phys_read32(void *, uint32_t pa, uint32_t *value)
{
	*value = vm_read_memory_4(pa);
	return true;
}

/* PTE R/C updates land in the guest HTAB (RAM). */
static bool guest_phys_write32(void *, uint32_t pa, uint32_t value)
{
	extern uint32 RAMBase, RAMSize;
	if (pa < RAMBase || pa + 4 > RAMBase + RAMSize)
		return false;
	vm_write_memory_4(pa, value);
	return true;
}

static uint64_t glue_tb_ticks(void *)
{
	return ppc_cpu->tb_host_ticks();
}

void nw_host_tick(void)
{
	if (ROMType != ROMTYPE_NEWWORLD)
		return;
	static uint64 next_present_us;
	const uint64 now = GetTicks_usec();
	if (now < next_present_us)
		return;
	next_present_us = now + 1000000 / 60;
	VideoHostPresent();
}

void init_emul_ppc(void)
{
	// Get pointer to KernelData in host address space
	kernel_data = (KernelData *)Mac2HostAddr(KERNEL_DATA_BASE);

	// Initialize main CPU emulator
	ppc_cpu = new sheepshaver_cpu();
	ppc_cpu->set_register(powerpc_registers::GPR(3), any_register((uint32)ROMBase + 0x30d000));
	ppc_cpu->set_register(powerpc_registers::GPR(4), any_register(KernelDataAddr + 0x1000));
	WriteMacInt32(XLM_RUN_MODE, MODE_68K);

	if (ROMType == ROMTYPE_NEWWORLD) {
		ppc32_guest_mmu().set_phys_read32(guest_phys_read32, NULL);
		ppc32_guest_mmu().set_phys_write32(guest_phys_write32, NULL);
		ppc_cpu->enable_guest_mmu(true);
		/* Device models (OpenPIC, Keylargo timer, uni-n, VIA-PMU) clocked at
		 * the timebase rate so guest measurements against them are exact,
		 * from the host side of it so a guest mttb does not move them. */
		struct nw_devices_clock clk;
		clk.ticks = glue_tb_ticks;
		clk.ctx = NULL;
		clk.hz = (uint32)TimebaseSpeed;
		nw_devices_init(&clk);
		nw_io_log_banks();
		{
			const char *e = getenv("NW_JIT");
			if (e && (strcmp(e, "on") == 0 || strcmp(e, "1") == 0))
				nw_jit_set_mode(NW_JIT_ON);
			else if (e && strcmp(e, "verify") == 0)
				nw_jit_set_mode(NW_JIT_VERIFY);
			else if (e && strcmp(e, "off") == 0)
				nw_jit_set_mode(NW_JIT_OFF);
			else if (e && strcmp(e, "fallback") == 0)
				nw_jit_set_mode(NW_JIT_FALLBACK);
			else if (PrefsFindBool("jit"))
				nw_jit_set_mode(NW_JIT_ON);
			else
				nw_jit_set_mode(NW_JIT_OFF);
			printf("NW-BOOT G1: jit %s\n", nw_jit_mode_name());
			nw_jit_set_code_pages(RAMBase, RAMSize, ROMBase, 0x500000u);
		}
		/* The Trampoline programs the OpenPIC sources (priority, vector,
		 * sense, destination) before the NK runs; the 68k StartInit only
		 * toggles their mask bits afterwards. */
		nw_trampoline_program_pic();
		nw_script_init();	/* no-op unless NW_SCRIPT is set */
		/*
		 * G1 handoff: NK v2 (0x3104a8) walks NKSystemInfo in r5 for the
		 * physical RAM banks (bank size at +52). The Trampoline fills this
		 * on real hardware; SheepShaver has no Trampoline, so seed the
		 * record here. Data only. The NK itself is not modified.
		 */
		extern uint32 RAMBase, RAMSize;
		nw_config_info_layout layout;
		layout.rom_base = ROMBase;
		layout.rom_area_size = ROM_AREA_SIZE;
		layout.ram_base = RAMBase;
		layout.ram_size = RAMSize;
		layout.ci_pa = NW_CI_PA;
		layout.bootinfo_pa = NW_BOOTINFO_LA;
		layout.extra = NULL;
		layout.n_extra = 0;
		uint8 si[NW_SI_SIZE];
		nw_fill_system_info_be(si, &layout);
		const uint32 sysinfo = SheepMem::Reserve(NW_SI_SIZE);
		Host2Mac_memcpy(sysinfo, si, NW_SI_SIZE);
		ppc_cpu->set_register(powerpc_registers::GPR(5), any_register(sysinfo));
		uint8 pi[NW_PI_SIZE];
		nw_fill_processor_info_be(pi, PVR, (uint32)CPUClockSpeed, (uint32)BusClockSpeed, (uint32)TimebaseSpeed);
		const uint32 procinfo = SheepMem::Reserve(NW_PI_SIZE);
		Host2Mac_memcpy(procinfo, pi, NW_PI_SIZE);
		ppc_cpu->set_register(powerpc_registers::GPR(4), any_register(procinfo));
		ppc_cpu->set_register(powerpc_registers::GPR(6), any_register((uint32)0));	/* no DiagInfo */
		/*
		 * S4 step 4b: hardware-info block. Golden: r7 = 'RTAS', r8 = RTAS
		 * entry (0), r9 -> 0xc0-byte block ('Hnfo' at +0x70). The NK copies
		 * it to IRP+0xf00 and publishes it at KDP+0xfd0; the 68k StartInit
		 * takes its ProductInfo record (boot-info +0x51dd0) and I/O bases
		 * from it. With r7 = 0 the block was all zero: VIA base 0.
		 */
		uint8 hw[NW_HWINFO_SIZE];
		nw_fill_hwinfo_be(hw, &layout);
		const uint32 hwinfo = SheepMem::Reserve(NW_HWINFO_SIZE);
		Host2Mac_memcpy(hwinfo, hw, NW_HWINFO_SIZE);
		ppc_cpu->set_register(powerpc_registers::GPR(7), any_register((uint32)NW_HWINFO_MAGIC_R7));
		ppc_cpu->set_register(powerpc_registers::GPR(8), any_register((uint32)0));
		ppc_cpu->set_register(powerpc_registers::GPR(9), any_register(hwinfo));
		ppc_cpu->set_register(powerpc_registers::GPR(23), any_register((uint32)0));	/* no debug SCC */
		/*
		 * Trampoline's other job: the exception vector stubs live at PA 0
		 * (MSR[IP]=0); on hardware the Trampoline copies them from the ROM
		 * exception table (ROM+0x300000) before the NK runs (golden: PA
		 * 0..0x2fff == ROM+0x300000). Only the first 0x2800 bytes are real
		 * vectors. This is a physical address (the guest's LA 0 is RAM at
		 * PA RAMBase, see nw_la_to_pa), so bypass the Mac accessors.
		 * The filled ConfigInfo goes in the page after them (PA 0x3000,
		 * golden hardware-info +0xc): the NK's external interrupt handler
		 * reads its vector -> 68k level table there by absolute address.
		 * r3 (ConfigInfo) points at that copy.
		 */
		memcpy(vm_do_get_real_address(0), ROMBaseHost + NW_NK_EXC_TABLE_ROM_OFF, NW_NK_EXC_TABLE_COPY_LEN);
		memcpy(vm_do_get_real_address(NW_CI_PA), ROMBaseHost + NW_CONFIGINFO_OFFSET, NW_CI_SIZE);
		ppc_cpu->set_register(powerpc_registers::GPR(3), any_register((uint32)NW_CI_PA));
		nw_log_g1_hwinit();
		{
			char buf[128];
			snprintf(buf, sizeof(buf), "G1: NKSystemInfo r5=%08x bank=%08x+%08x vectors@0 from ROM+%06x r9=%08x",
				 (unsigned)sysinfo, (unsigned)RAMBase, (unsigned)RAMSize, (unsigned)NW_NK_EXC_TABLE_ROM_OFF,
				 (unsigned)hwinfo);
			nw_boot_diag(buf);
		}
	} else {
		nw_log_translator_off();
	}

	nw_guest_prof_start();

#if ENABLE_MON
	// Install "regs" command in cxmon
	mon_add_command("regs", dump_registers, "regs                     Dump PowerPC registers\n");
	mon_add_command("log", dump_log, "log                      Dump PowerPC emulation log\n");
#endif

#if EMUL_TIME_STATS
	emul_start_time = clock();
#endif
}

/*
 *  Deinitialize emulation
 */

void exit_emul_ppc(void)
{
#if EMUL_TIME_STATS
	clock_t emul_end_time = clock();

	printf("### Statistics for SheepShaver emulation parts\n");
	const clock_t emul_time = emul_end_time - emul_start_time;
	printf("Total emulation time : %.1f sec\n", double(emul_time) / double(CLOCKS_PER_SEC));
	printf("Total interrupt count: %d (%2.1f Hz)\n", interrupt_count,
		   (double(interrupt_count) * CLOCKS_PER_SEC) / double(emul_time));
	printf("Total ppc interrupt count: %d (%2.1f %%)\n", ppc_interrupt_count,
		   (double(ppc_interrupt_count) * 100.0) / double(interrupt_count));

#define PRINT_STATS(LABEL, VAR_PREFIX) do {								\
		printf("Total " LABEL " count : %d\n", VAR_PREFIX##_count);		\
		printf("Total " LABEL " time  : %.1f sec (%.1f%%)\n",			\
			   double(VAR_PREFIX##_time) / double(CLOCKS_PER_SEC),		\
			   100.0 * double(VAR_PREFIX##_time) / double(emul_time));	\
	} while (0)

	PRINT_STATS("Execute68k[Trap] execution", exec68k);
	PRINT_STATS("NativeOp execution", native_exec);
	PRINT_STATS("MacOS routine execution", macos_exec);

#undef PRINT_STATS
	printf("\n");
#endif

	delete ppc_cpu;
	ppc_cpu = NULL;
}

#if PPC_ENABLE_JIT && PPC_REENTRANT_JIT
// Initialize EmulOp trampolines
void init_emul_op_trampolines(basic_dyngen & dg)
{
	typedef void (*func_t)(dyngen_cpu_base, uint32);
	func_t func;

	// EmulOp
	emul_op_trampoline = dg.gen_start();
	func = &sheepshaver_cpu::call_execute_emul_op;
	dg.gen_invoke_CPU_T0(func);
	dg.gen_exec_return();
	dg.gen_end();

	// NativeOp
	native_op_trampoline = dg.gen_start();
	func = &sheepshaver_cpu::call_execute_native_op;
	dg.gen_invoke_CPU_T0(func);	
	dg.gen_exec_return();
	dg.gen_end();

	D(bug("EmulOp trampoline:   %p\n", emul_op_trampoline));
	D(bug("NativeOp trampoline: %p\n", native_op_trampoline));
}
#endif

/*
 *  Emulation loop
 */

void emul_ppc(uint32 entry)
{
#if 0
	ppc_cpu->start_log();
#endif
	// start emulation loop and enable code translation or caching
	ppc_cpu->execute(entry);
}

/*
 *  Handle PowerPC interrupt
 */

void TriggerInterrupt(void)
{
	idle_resume();
#if 0
  WriteMacInt32(0x16a, ReadMacInt32(0x16a) + 1);
#else
  // Trigger interrupt to main cpu only
  if (ppc_cpu)
	  ppc_cpu->trigger_interrupt();
#endif
}

void HandleInterrupt(powerpc_registers *r)
{
#ifdef USE_SDL_VIDEO
	// We must fill in the events queue in the same thread that did call SDL_SetVideoMode()
	SDL_PumpEvents();
#endif

	/*
	 * New World: NK v2 owns interrupt delivery (decrementer, OpenPIC).
	 * The Old World injection below pokes the classic KDP layout
	 * (+0x674/+0x67c) and runs the 68k interrupt routine by hand; with the
	 * KDP now reachable at its real PA that corrupts the NK's page. Host
	 * events reach the guest through the device models instead.
	 */
	if (ROMType == ROMTYPE_NEWWORLD) {
		/* The card hardware is in the device tree before the guest
		 * runs. The Sound control panel lists a component, and the
		 * Component Manager throws away registrations made before it
		 * finishes loading extensions. CurApName becomes "Finder"
		 * as Finder starts. EmulOp registers one component per trap. */
		if (HasMacStarted()) {
			static int audio_tick;
			static int audio_reg;
			audio_tick++;
			if (!audio_reg && ReadMacInt8(0x910) == 6 &&
			    ReadMacInt8(0x911) == 'F' && ReadMacInt8(0x912) == 'i' &&
			    ReadMacInt8(0x913) == 'n' && ReadMacInt8(0x914) == 'd' &&
			    ReadMacInt8(0x915) == 'e' && ReadMacInt8(0x916) == 'r') {
				audio_reg = 1;
				nw_audio_arm_register();
				SheepForceComponentsArm();
			}
			/* Do not scan guest RAM from this tick. The scan stalls
			 * the desktop while the Sound panel is in use. */
		}
		/* The SDL callback is blocked on the audio thread until
		 * AudioInterrupt posts. Do that here. Do not fall through:
		 * the rest of this function writes the nanokernel page. */
		if ((InterruptFlags & INTFLAG_AUDIO) && int32(ReadMacInt32(XLM_IRQ_NEST)) <= 0) {
			ClearInterruptFlag(INTFLAG_AUDIO);
			AudioInterrupt();
		}
		return;
	}

	// Do nothing if interrupts are disabled
	if (int32(ReadMacInt32(XLM_IRQ_NEST)) > 0)
		return;

	// Update interrupt count
#if EMUL_TIME_STATS
	interrupt_count++;
#endif

	// Interrupt action depends on current run mode
	switch (ReadMacInt32(XLM_RUN_MODE)) {
	case MODE_68K:
		// 68k emulator active, trigger 68k interrupt level 1
		WriteMacInt16(ReadMacInt32(KERNEL_DATA_BASE + 0x67c), 1);
		r->cr.set(r->cr.get() | ReadMacInt32(KERNEL_DATA_BASE + 0x674));
		break;
    
#if INTERRUPTS_IN_NATIVE_MODE
	case MODE_NATIVE:
		// 68k emulator inactive, in nanokernel?
		if (r->gpr[1] != KernelDataAddr) {

			// Prepare for 68k interrupt level 1
			WriteMacInt16(ReadMacInt32(KERNEL_DATA_BASE + 0x67c), 1);
			WriteMacInt32(ReadMacInt32(KERNEL_DATA_BASE + 0x658) + 0xdc,
						  ReadMacInt32(ReadMacInt32(KERNEL_DATA_BASE + 0x658) + 0xdc)
						  | ReadMacInt32(KERNEL_DATA_BASE + 0x674));
      
			// Execute nanokernel interrupt routine (this will activate the 68k emulator)
			DisableInterrupt();
			if (ROMType == ROMTYPE_NEWWORLD)
				ppc_cpu->interrupt(ROMBase + 0x312b1c);
			else
				ppc_cpu->interrupt(ROMBase + 0x312a3c);
		}
		break;
#endif
    
#if INTERRUPTS_IN_EMUL_OP_MODE
	case MODE_EMUL_OP:
		// 68k emulator active, within EMUL_OP routine, execute 68k interrupt routine directly when interrupt level is 0
		if ((ReadMacInt32(XLM_68K_R25) & 7) == 0) {
#if EMUL_TIME_STATS
			const clock_t interrupt_start = clock();
#endif
#if 1
			// Execute full 68k interrupt routine
			M68kRegisters r;
			uint32 old_r25 = ReadMacInt32(XLM_68K_R25);	// Save interrupt level
			WriteMacInt32(XLM_68K_R25, 0x21);			// Execute with interrupt level 1
			static const uint8 proc_template[] = {
				0x3f, 0x3c, 0x00, 0x00,			// move.w	#$0000,-(sp)	(fake format word)
				0x48, 0x7a, 0x00, 0x0a,			// pea		@1(pc)			(return address)
				0x40, 0xe7,						// move		sr,-(sp)		(saved SR)
				0x20, 0x78, 0x00, 0x064,		// move.l	$64,a0
				0x4e, 0xd0,						// jmp		(a0)
				M68K_RTS >> 8, M68K_RTS & 0xff	// @1
			};
			BUILD_SHEEPSHAVER_PROCEDURE(proc);
			Execute68k(proc, &r);
			WriteMacInt32(XLM_68K_R25, old_r25);		// Restore interrupt level
#else
			// Only update cursor
			if (HasMacStarted()) {
				if (InterruptFlags & INTFLAG_VIA) {
					ClearInterruptFlag(INTFLAG_VIA);
					ADBInterrupt();
					ExecuteNative(NATIVE_VIDEO_VBL);
				}
			}
#endif
#if EMUL_TIME_STATS
			interrupt_time += (clock() - interrupt_start);
#endif
		}
		break;
#endif
	}
}

void sheepshaver_cpu::call_execute_native_op(powerpc_cpu * cpu, uint32 selector) {
	static_cast<sheepshaver_cpu *>(cpu)->execute_native_op(selector);
}

// Execute NATIVE_OP routine
void sheepshaver_cpu::execute_native_op(uint32 selector)
{
#if EMUL_TIME_STATS
	native_exec_count++;
	const clock_t native_exec_start = clock();
#endif

	switch (selector) {
	case NATIVE_PATCH_NAME_REGISTRY:
		DoPatchNameRegistry();
		break;
	case NATIVE_VIDEO_INSTALL_ACCEL:
		VideoInstallAccel();
		break;
	case NATIVE_RAVE_REGISTER:
		SheepForceRaveRegisterNative();
		break;
	case NATIVE_RAVE_METHOD: {
		/* A RAVE callback called by the guest; the slot is in r0 (see NativeSlotTVECT). */
		RaveGuestCall call;
		for (int i = 0; i < 8; i++)
			call.r[i] = gpr(3 + i);
		for (int i = 0; i < 13; i++)
			call.f[i] = fpr(1 + i);
		call.sp = gpr(1);
		RaveGuestResult res;
		SheepForceRaveMethod(gpr(0), &call, &res);
		gpr(3) = res.r3;
		if (res.is_float)
			fpr(1) = res.f1;
		break;
	}
	case NATIVE_VIDEO_VBL:
		if (ROMType == ROMTYPE_NEWWORLD) {
			/* the display node's interrupt handler: returns kIsrIsComplete
			 * (0; a positive value names a child set member, -1 is not
			 * complete) */
			VideoDriverVBL();
			gpr(3) = 0;
		} else
			VideoVBL();
		break;
	case NATIVE_VIDEO_DO_DRIVER_IO:
		gpr(3) = (int32)(int16)VideoDoDriverIO(gpr(3), gpr(4), gpr(5), gpr(6), gpr(7));
		break;
	case NATIVE_ETHER_AO_GET_HWADDR:
		AO_get_ethernet_address(gpr(3));
		break;
	case NATIVE_ETHER_AO_ADD_MULTI:
		AO_enable_multicast(gpr(3));
		break;
	case NATIVE_ETHER_AO_DEL_MULTI:
		AO_disable_multicast(gpr(3));
		break;
	case NATIVE_ETHER_AO_SEND_PACKET:
		AO_transmit_packet(gpr(3));
		break;
	case NATIVE_ETHER_IRQ:
		EtherIRQ();
		break;
	case NATIVE_ETHER_INIT:
		gpr(3) = InitStreamModule((void *)(uintptr)gpr(3));
		break;
	case NATIVE_ETHER_TERM:
		TerminateStreamModule();
		break;
	case NATIVE_ETHER_OPEN:
		gpr(3) = ether_open((queue_t *)Mac2HostAddr(gpr(3)), (void *)(uintptr)gpr(4), gpr(5), gpr(6), (void *)(uintptr)gpr(7));
		break;
	case NATIVE_ETHER_CLOSE:
		gpr(3) = ether_close((queue_t *)Mac2HostAddr(gpr(3)), gpr(4), (void *)(uintptr)gpr(5));
		break;
	case NATIVE_ETHER_WPUT:
		gpr(3) = ether_wput((queue_t *)Mac2HostAddr(gpr(3)), (mblk_t *)Mac2HostAddr(gpr(4)));
		break;
	case NATIVE_ETHER_RSRV:
		gpr(3) = ether_rsrv((queue_t *)Mac2HostAddr(gpr(3)));
		break;
	case NATIVE_NQD_SYNC_HOOK:
		gpr(3) = NQD_sync_hook(gpr(3));
		break;
	case NATIVE_NQD_UNKNOWN_HOOK:
		gpr(3) = NQD_unknown_hook(gpr(3));
		break;
	case NATIVE_NQD_LINES_HOOK:
		gpr(3) = NQD_lines_hook(gpr(3));
		break;
	case NATIVE_NQD_FILLMASK_HOOK:
		gpr(3) = NQD_fillmask_hook(gpr(3));
		break;
	case NATIVE_NQD_FILLMASK:
		NQD_fillmask(gpr(3));
		break;
	case NATIVE_NQD_PROBE_HOOK:
		gpr(3) = NQD_probe_hook(gpr(3), gpr(0));
		break;
	case NATIVE_NQD_BITBLT_HOOK:
		gpr(3) = NQD_bitblt_hook(gpr(3));
		break;
	case NATIVE_NQD_BITBLT:
		NQD_bitblt(gpr(3));
		break;
	case NATIVE_NQD_FILLRECT_HOOK:
		gpr(3) = NQD_fillrect_hook(gpr(3));
		break;
	case NATIVE_NQD_INVRECT:
		NQD_invrect(gpr(3));
		break;
	case NATIVE_NQD_FILLRECT:
		NQD_fillrect(gpr(3));
		break;
	case NATIVE_SERIAL_NOTHING:
	case NATIVE_SERIAL_OPEN:
	case NATIVE_SERIAL_PRIME_IN:
	case NATIVE_SERIAL_PRIME_OUT:
	case NATIVE_SERIAL_CONTROL:
	case NATIVE_SERIAL_STATUS:
	case NATIVE_SERIAL_CLOSE: {
		typedef int16 (*SerialCallback)(uint32, uint32);
		static const SerialCallback serial_callbacks[] = {
			SerialNothing,
			SerialOpen,
			SerialPrimeIn,
			SerialPrimeOut,
			SerialControl,
			SerialStatus,
			SerialClose
		};
		gpr(3) = serial_callbacks[selector - NATIVE_SERIAL_NOTHING](gpr(3), gpr(4));
		break;
	}
	case NATIVE_GET_RESOURCE:
		get_resource(ReadMacInt32(XLM_GET_RESOURCE));
		break;
	case NATIVE_GET_1_RESOURCE:
		get_resource(ReadMacInt32(XLM_GET_1_RESOURCE));
		break;
	case NATIVE_SHEARS:
		gpr(3) = ShearsCall(gpr(3));
		break;
	case NATIVE_GET_IND_RESOURCE:
		get_resource(ReadMacInt32(XLM_GET_IND_RESOURCE));
		break;
	case NATIVE_GET_1_IND_RESOURCE:
		get_resource(ReadMacInt32(XLM_GET_1_IND_RESOURCE));
		break;
	case NATIVE_R_GET_RESOURCE:
		get_resource(ReadMacInt32(XLM_R_GET_RESOURCE));
		break;
	case NATIVE_MAKE_EXECUTABLE:
		MakeExecutable(0, gpr(4), gpr(5));
		break;
	case NATIVE_CHECK_LOAD_INVOC:
		check_load_invoc(gpr(3), gpr(4), gpr(5));
		break;
	case NATIVE_NAMED_CHECK_LOAD_INVOC:
		named_check_load_invoc(gpr(3), gpr(4), gpr(5));
		break;
	default:
		printf("FATAL: NATIVE_OP called with bogus selector %d\n", selector);
		QuitEmulator();
		break;
	}

#if EMUL_TIME_STATS
	native_exec_time += (clock() - native_exec_start);
#endif
}

/*
 *  Execute 68k subroutine (must be ended with EXEC_RETURN)
 *  This must only be called by the emul_thread when in EMUL_OP mode
 *  r->a[7] is unused, the routine runs on the caller's stack
 */

void Execute68k(uint32 pc, M68kRegisters *r)
{
	ppc_cpu->execute_68k(pc, r);
}

/*
 *  Execute 68k A-Trap from EMUL_OP routine
 *  r->a[7] is unused, the routine runs on the caller's stack
 */

void Execute68kTrap(uint16 trap, M68kRegisters *r)
{
	SheepVar proc_var(4);
	uint32 proc = proc_var.addr();
	WriteMacInt16(proc, trap);
	WriteMacInt16(proc + 2, M68K_RTS);
	Execute68k(proc, r);
}

/*
 *  Call MacOS PPC code
 */

uint32 call_macos(uint32 tvect)
{
	return ppc_cpu->execute_macos_code(tvect, 0, NULL);
}

uint32 call_macos1(uint32 tvect, uint32 arg1)
{
	const uint32 args[] = { arg1 };
	return ppc_cpu->execute_macos_code(tvect, sizeof(args)/sizeof(args[0]), args);
}

uint32 call_macos2(uint32 tvect, uint32 arg1, uint32 arg2)
{
	const uint32 args[] = { arg1, arg2 };
	return ppc_cpu->execute_macos_code(tvect, sizeof(args)/sizeof(args[0]), args);
}

uint32 call_macos3(uint32 tvect, uint32 arg1, uint32 arg2, uint32 arg3)
{
	const uint32 args[] = { arg1, arg2, arg3 };
	return ppc_cpu->execute_macos_code(tvect, sizeof(args)/sizeof(args[0]), args);
}

uint32 call_macos4(uint32 tvect, uint32 arg1, uint32 arg2, uint32 arg3, uint32 arg4)
{
	const uint32 args[] = { arg1, arg2, arg3, arg4 };
	return ppc_cpu->execute_macos_code(tvect, sizeof(args)/sizeof(args[0]), args);
}

uint32 call_macos5(uint32 tvect, uint32 arg1, uint32 arg2, uint32 arg3, uint32 arg4, uint32 arg5)
{
	const uint32 args[] = { arg1, arg2, arg3, arg4, arg5 };
	return ppc_cpu->execute_macos_code(tvect, sizeof(args)/sizeof(args[0]), args);
}

uint32 call_macos6(uint32 tvect, uint32 arg1, uint32 arg2, uint32 arg3, uint32 arg4, uint32 arg5, uint32 arg6)
{
	const uint32 args[] = { arg1, arg2, arg3, arg4, arg5, arg6 };
	return ppc_cpu->execute_macos_code(tvect, sizeof(args)/sizeof(args[0]), args);
}

uint32 call_macos7(uint32 tvect, uint32 arg1, uint32 arg2, uint32 arg3, uint32 arg4, uint32 arg5, uint32 arg6, uint32 arg7)
{
	const uint32 args[] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7 };
	return ppc_cpu->execute_macos_code(tvect, sizeof(args)/sizeof(args[0]), args);
}
