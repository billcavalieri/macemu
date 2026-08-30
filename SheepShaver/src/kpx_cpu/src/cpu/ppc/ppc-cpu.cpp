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
extern uint32 ROMBase, RAMBase, RAMSize;
static uint32 g3_rom0(uint32 a)
{
	/* 24-bit Mac tags. Keep SheepShaver RAM 0x10-0x17, ROM 0x50,
	 * kernel 0x68. NuBus slot FA/FB (Old World video) aliases the
	 * planted framebuffer. */
	{
		const uint32 hi = a >> 24;
		if (hi == 0xfau || hi == 0xfbu)
			return RAMBase + 0x400000u + (a & 0x000fffffu);
		if (!(hi == 0 ||
		      (hi >= 0x10u && hi <= 0x17u) ||
		      hi == 0x50u || hi == 0x68u))
			a &= 0x00ffffffu;
	}
	/* RAM-at-0 for 68k mem probes past the 128KiB lowmem BAT. */
	if (a >= 0x20000u && a < RAMSize)
		return RAMBase + a;
	if (a >= 0x3000u && a < 0x400000u)
		return ROMBase + a;
	return a;
}
static uint32 g3_pc0(uint32 pc)
{
	if (pc >= ROMBase && pc < ROMBase + 0x500000u)
		return pc - ROMBase;
	return pc;
}
static int g3_ea_data(uint32 a)
{
	a = g3_rom0(a);
	if (a < 0x20000u)
		return 1;
	if (a >= RAMBase && a < RAMBase + RAMSize)
		return 1;
	if (a >= 0x68fe0000u && a < 0x69000000u)
		return 1;
	return 0;
}
static int g3_r24_ok(uint32 r24)
{
	if (r24 & 1u)
		return 0;
	if (r24 >= ROMBase && r24 < ROMBase + 0x2au)
		return 0;
	if (r24 >= ROMBase && r24 < ROMBase + 0x500000u) {
		/* Overlay pointer table + zero pad, not 68k code. */
		const uint32 off = r24 - ROMBase;
		/* Extra 1MiB copy at ROM+4MiB is PPC/data, not 68k. */
		if (off >= 0x400000u)
			return 0;
		/* 68k emulator + jump table, not 68k PC. */
		if (off >= 0x350000u && off < 0x400000u)
			return 0;
		if (off >= 0xa9e0u && off < 0xaa7eu)
			return 0;
		/* SANE dispatcher from MOVE.W #$2600,A7. Keep 0xa940
		 * legal; 0xaf3e is the next RTS. */
		if (off >= 0xa942u && off < 0xaf3eu)
			return 0;
		if (off >= 0x2c1aau && off < 0x2c1c6u)
			return 0;
		if (off >= 0x40c82u && off < 0x40ce8u)
			return 0;
		if (off >= 0x41062u && off < 0x41100u)
			return 0;
		/* Debugger Nub + MixedMode Pascal names.
		 * Dest-RTS mill on KEEP 0xfeb0 RTS; include the
		 * MOVEQ #-14 epilogue. Dest 0x13000. */
		if (off >= 0x0fe9au && off < 0x13000u)
			return 0;
		/* Pascal GetStartupDeviceType/StartLib names after
		 * RTS 0x4248, then StartLib mill + dest-edge VBR
		 * DBF fill + JMP (A6). Keep 0x4248. Dest 0x48b0. */
		if (off >= 0x424au && off < 0x48b0u)
			return 0;
		/* Packed offset table after RTS 0x498e0. Keep 0x498e0.
		 * Dest 0x499d6 MOVEQ #0x2c. */
		if (off >= 0x498e2u && off < 0x499d6u)
			return 0;
		/* Zone/handle helper dest-RTS after InitGraf.
		 * Keep 0x49e62 RTS. Dest 0x4a0e0 next LINK. */
		if (off >= 0x49e64u && off < 0x4a0e0u)
			return 0;
		/* VIA offset words after JMP (A2) at 0x53a. */
		if (off >= 0x53cu && off < 0x550u)
			return 0;
		/* JMP (A5) SCSI loop + BRA 0xd084 back to 0xd052.
		 * Keep 0xd04e legal. 0xd094 is after RTS. */
		if (off >= 0xd050u && off < 0xd094u)
			return 0;
		if (off >= 0xcbdcu && off < 0xcbe4u)
			return 0;
		if (off >= 0xcdeau && off < 0xce24u)
			return 0;
		if (off >= 0xcfc8u && off < 0xd04eu)
			return 0;
		/* SCSI command map + chip glue. Dest 0xd430
		 * MOVEQ #-50 (scNoDevice) + RTS. */
		if (off >= 0xd0d8u && off < 0xd430u)
			return 0;
		/* SCSI bit-count table after RTS 0xc758. Keep 0xc758. */
		if (off >= 0xc75au && off < 0xc86cu)
			return 0;
		if (off >= 0xc91cu && off < 0xc92au)
			return 0;
		/* SCSI jump table after MOVEQ #-50 RTS. */
		if (off >= 0xd4b4u && off < 0xd570u)
			return 0;
		if (off >= 0xd592u && off < 0xd5f4u)
			return 0;
		/* Whole SCSI/VIA mill including tables. Keep 0xbf60. */
		if (off >= 0xbf62u && off < 0xd5f4u)
			return 0;
		/* Field thunks 2F30/2030/4E75 plus 2F38/RTS, then
		 * zero pad. 0x26006 is the next real routine. */
		if (off >= 0x25fc0u && off < 0x26006u)
			return 0;
		/* MixedMode 2f30 dest-RTS mill + BSR into skip.
		 * Keep 0x271ce RTS. Dest 0x27620 2d1f (not 4eba/4efa
		 * JMP 2f30 table mill at 0x27330). */
		if (off >= 0x271d0u && off < 0x27620u)
			return 0;
		/* 60ff pad + zeros + 2f30 after helper RTS 0x210e2.
		 * Dest 0x21130 MOVEA.L $0BB8. */
		if (off >= 0x210e4u && off < 0x21130u)
			return 0;
		/* MixedMode 2f30 + BSR.W 0x21184/0x21232/0x21260 back
		 * into wait, JSR (A1) garbage, JMP (A2) table.
		 * Keep 0x2116c RTS. Dest 0x214da 2f30. */
		if (off >= 0x2116eu && off < 0x214dau)
			return 0;
		/* MixedMode 2f30 + ADB init + dest-edge HideCursor BRA
		 * + QD copy dest-RTS. Keep 0x572c4 A8D9. Dest 0x58000. */
		if (off >= 0x572c6u && off < 0x58000u)
			return 0;
		/* MixedMode 2f30 thunk dest-RTS. Keep 0x570ea RTS.
		 * Dest 0x57100 MOVEA.L 8(A7),A1. */
		if (off >= 0x570ecu && off < 0x57100u)
			return 0;
		/* GetFCB dest-RTS + 2f30 thunks. Keep 0x5706c RTS.
		 * Dest 0x571d0 MOVEM. */
		if (off >= 0x5706eu && off < 0x571d0u)
			return 0;
		/* MOVEA.L A3,A0 mill A0=-1 + JSR 2f30 + BRA.W
		 * retry. Keep 0x1f8f6 MOVEQ. Dest 0x1f90c BTST. */
		if (off >= 0x1f8f8u && off < 0x1f90cu)
			return 0;
		/* MixedMode 2f30 dest of GetResource epilogue.
		 * Keep 0x1fe86 RTS. Dest 0x1fef6 MOVE.L (SP)+. */
		if (off >= 0x1fe88u && off < 0x1fef6u)
			return 0;
		/* MixedMode 2f30 dest-edge of 0x58000. Keep 0x58664 RTS.
		 * Dest 0x58670 MOVEA.L (A5),A0. */
		if (off >= 0x58666u && off < 0x58670u)
			return 0;
		/* MixedMode 2f30 dest of BRA.L 0x7690. Keep 0x46e88 RTS.
		 * Dest 0x46ea0 next LINK. */
		if (off >= 0x46e8au && off < 0x46ea0u)
			return 0;
		/* Dest-edge 2f30 after RTD 0x46f0e. Keep 0x46f0e.
		 * Dest 0x46f20 next LINK. */
		if (off >= 0x46f14u && off < 0x46f20u)
			return 0;
		/* Slot helper + dest-RTS 2f30 0x2699e + selector table.
		 * Keep 0x26612. Dest 0x26de0 MOVEA (not dest-RTS 0x26db4). */
		if (off >= 0x26614u && off < 0x26de0u)
			return 0;
		/* 'PACK' GetResource unpacker + dest-edge mill.
		 * Keep 0x2bdee. Dest 0x2bec0 MOVEM. */
		if (off >= 0x2bdf0u && off < 0x2bec0u)
			return 0;
		/* MixedMode 2f30 table + 60ff pad. Keep 0x24c50 RTS.
		 * Dest 0x255f6 LEA (not dest-RTS 0x24c50 / ADDQ 0x24e80). */
		if (off >= 0x24c52u && off < 0x24e80u)
			return 0;
		/* Slot selector offset table + JMP (A1). Keep 0x26c50.
		 * Dest 0x26de0 MOVEA. */
		if (off >= 0x26c52u && off < 0x26de0u)
			return 0;
		/* 2f30 dest-RTS before copy mill. Keep 0x1e90e BRA.W
		 * opcode (not ext 0be0 at 0x1e910). Dest 0x1e920. */
		if (off >= 0x1e910u && off < 0x1e920u)
			return 0;
		/* CLR.L DBF fill after MOVEQ #0x7f. Keep 0x1e8b0.
		 * Dest 0x1e8b8 MOVEA.L (SP)+,A1. */
		if (off >= 0x1e8b2u && off < 0x1e8b8u)
			return 0;
		/* Zeros + InitFCB TST $03F6 / DIVU.W $03F6 / A71E /
		 * SysError 25. Keep 0x25494 RTS. Dest 0x255f6 LEA. */
		if (off >= 0x25496u && off < 0x255f6u)
			return 0;
		/* A71E fail MOVEQ #25 SysError. Keep 0x28b10 RTS.
		 * Dest 0x28b66 2f30. */
		if (off >= 0x28b12u && off < 0x28b66u)
			return 0;
		/* 60ff pads before QD JSR. Keep 0x214f0 MOVEQ.
		 * Dest 0x21500 JSR. */
		if (off >= 0x214f2u && off < 0x21500u)
			return 0;
		/* BRA.S mill after BNE. Keep 0x21514. Dest 0x21518 JSR. */
		if (off >= 0x21516u && off < 0x21518u)
			return 0;
		/* CLR.W -(SP)/DBF alloca. Keep 0x2014c RTS. Dest 0x20160 MOVEA.
		 */
		if (off >= 0x2014eu && off < 0x20160u)
			return 0;
		/* Unconditional BRA.S back to 0x214f8 60ff mill.
		 * Keep 0x2153c BNE. Dest 0x21540 JSR. */
		if (off >= 0x2153eu && off < 0x21540u)
			return 0;
		/* BRA.W back to 0x214f2 60ff mill. Keep 0x21574 ADD.
		 * Dest 0x2157a JSR. */
		if (off >= 0x21576u && off < 0x2158au)
			return 0;
		/* BRA.W back to 0x214f2. Keep 0x21584 MOVE. Dest 0x2158a BSR. */
		if (off >= 0x2157eu && off < 0x2158au)
			return 0;
		/* 2f30 + copy 20 longs from $0BB8. Keep 0x1e9d0. */
		if (off >= 0x1e9d2u && off < 0x1ea48u)
			return 0;
		/* MOVE.B (A0)+,(A1)+ DBF dest-edge D0 garbage. Keep 0x1fa16 RTS.
		 * Dest 0x1fa34 2d1f. */
		if (off >= 0x1fa1eu && off < 0x1fa34u)
			return 0;
		/* Zeros after RTS 0x252a4. Dest 0x252b0 MOVEQ. */
		if (off >= 0x252a6u && off < 0x252b0u)
			return 0;
		/* Zeros after 2f30 RTS 0x1ebea. Keep 0x1ebe2 thunk.
		 * Dest 0x1ebf0 MOVEM. */
		if (off >= 0x1ebecu && off < 0x1ebf0u)
			return 0;
		/* 2f38 $0744 JMP blit after tail RTS. Dest 0x20ffe 2d1f. */
		if (off >= 0x20f38u && off < 0x20ffeu)
			return 0;
		/* PEA idx ext 080c after 0x19518. Dest 0x1951c JSR. */
		if (off >= 0x1951au && off < 0x1951cu)
			return 0;
		/* MixedMode BSR.L/ADDQ A7/JMP (A3) glue.
		 * Keep 0x75fa RTS. Dest 0x7640 2f38. */
		if (off >= 0x75fcu && off < 0x7640u)
			return 0;
		/* ADB 0xe080 BRA.L MOVE SR trampoline 0x4926 plus
		 * exception wait and MOVE.L D0,-(A1) fill. Keep
		 * 0x4920 MOVEM and 0x4926 MOVE SR. Dest 0x49dc RTS. */
		if (off >= 0x492au && off < 0x49dcu)
			return 0;
		/* GetResource prelude 2f30 + copy + A9C9 + JMP idx.
		 * Keep 0x1ee20 RTS. Dest 0x1f7de. */
		if (off >= 0x1ee22u && off < 0x1f7deu)
			return 0;
		/* 2f30 thunk + JMP idx selector. Keep 0x1dd38.
		 * Dest 0x1ddb2 BSR. */
		if (off >= 0x1dd3au && off < 0x1ddb2u)
			return 0;
		/* Lowmem queue walk + dest helper RTS mill 0x2238e.
		 * Keep 0x215c2 RTS. Dest 0x22394 ST $034B
		 * (not dest-RTS 0x22368 / 0x2238e). */
		if (off >= 0x215c4u && off < 0x22394u)
			return 0;
		/* BRA.W dest-edge mill after queue dest. Keep 0x24498.
		 * Dest 0x24c50 RTS (hole; do not swallow KEEP). */
		if (off >= 0x2449au && off < 0x24c50u)
			return 0;
		/* BRA.W dest into queue skip + $0372 walk mill.
		 * Keep 0x22f00 BRA.W. Dest 0x22ffe MOVEM. */
		if (off >= 0x22f02u && off < 0x22ffeu)
			return 0;
		/* BRA.W dest-edge 70dd/BRA.B * + slot mill.
		 * Keep 0x22cd0. Hole 0x24284-0x24290 'BD' CMPI/RTS
		 * (BSR.W dest from 0x224b0). Dest 0x24e80 ADDQ. */
		if (off >= 0x22cd2u && off < 0x24284u)
			return 0;
		/* Slot helper $034E walk + dest-edge thunks.
		 * Keep 0x224e0 CLR.W. Hole 0x24284 'BD' check.
		 * Dest 0x24e80 ADDQ. */
		if (off >= 0x224e2u && off < 0x24284u)
			return 0;
		if (off >= 0x24290u && off < 0x24c50u)
			return 0;
		if (off >= 0x24c52u && off < 0x24e80u)
			return 0;
		/* Offset table after 0x16eca RTS. Dest 0x16f8c CLR.B. */
		if (off >= 0x16ed0u && off < 0x16f8cu)
			return 0;
		if (off >= 0x7e308u && off < 0x7e35au)
			return 0;
		/* MixedMode 2f30 + JMP (A1) + VBL $014A mill.
		 * Keep 0x640c0. */
		if (off >= 0x640c2u && off < 0x64ef6u)
			return 0;
		/* MixedMode 2f30 thunk dest-RTS. Keep 0x67a82 RTS.
		 * Dest 0x67a90 MOVE.L A0,-(A7). */
		if (off >= 0x67a84u && off < 0x67a90u)
			return 0;
		/* A8B5 BitMap helper + dest-RTS 0x5c5fa + BRA/BSR
		 * mill through 0x5c66e JMP back into skip.
		 * Keep 0x5be10. Dest 0x5c820 2f30 (not LINK
		 * 0x5c7f0 / ADDQ 0x5c81c mill). */
		if (off >= 0x5be12u && off < 0x5c820u)
			return 0;
		/* MixedMode 2f30 + HideCursor A910 mill. Keep 0x5ce1c.
		 * Dest 0x5cfb0 next LINK. */
		if (off >= 0x5ce1eu && off < 0x5cfb0u)
			return 0;
		/* PRAM selector JMP (d8,A1,D1) + offset table.
		 * Keep 0x3fb0. 0x3fd8 is the first thunk. */
		if (off >= 0x3fb2u && off < 0x3fd8u)
			return 0;
		/* ExpandMem $02B6 Gestalt walker. Keep 0x4facc RTD.
		 * Dest 0x4ffe0 'scrn' LINK. */
		if (off >= 0x4fad0u && off < 0x4ffe0u)
			return 0;
		/* 'scrn' GetResource / grafport mill including
		 * 0x5000e UNLK prelude and dest-edge GetResource.
		 * Keep 0x4ffe0. Dest 0x50840 next LINK. */
		if (off >= 0x4ffe2u && off < 0x50840u)
			return 0;
		/* Token/char switch parser. Keep 0x160e0.
		 * Dest 0x16780 next LINK after RTS 0x1675a. */
		if (off >= 0x160e2u && off < 0x16780u)
			return 0;
		/* Empty A4 list walk after 2f30 thunks. Keep 0x5d350. */
		if (off >= 0x5d352u && off < 0x5d4c6u)
			return 0;
		/* '.ATALoad' Pascal string + 60ff after RTS 0x5d880.
		 * Keep 0x5d882 (BTST as legal skip-start). Dest 0x5d8b0 LINK. */
		if (off >= 0x5d884u && off < 0x5d8b0u)
			return 0;
		/* "ATAManager" after RTS 0x5d908. Dest 0x5d920 LINK. */
		if (off >= 0x5d90au && off < 0x5d920u)
			return 0;
		/* "device_type" after RTS 0x5da68. Dest 0x5da80 LINK. */
		if (off >= 0x5da6au && off < 0x5da80u)
			return 0;
		/* Offset table after RTS 0x5cc28. Dest 0x5cc40 LINK. */
		if (off >= 0x5cc2au && off < 0x5cc40u)
			return 0;
		/* $08A8 bit scan ASL/AND/BNE mill. Keep 0x5cc8e
		 * MOVEQ. Dest 0x5cce0 MOVE.W. */
		if (off >= 0x5cc90u && off < 0x5cce0u)
			return 0;
		/* Word constants. Dest 0x5dbe0 MOVE.W. */
		if (off >= 0x5dbd0u && off < 0x5dbe0u)
			return 0;
		/* Zero after RTE 0x498c. Keep MOVEM 0x497e, 2f38
		 * 0x4984, 3f38 0x4988, RTE 0x498c. Dest 0x4990 LEA. */
		if (off >= 0x498eu && off < 0x4990u)
			return 0;
		/* GetStartupDevice LINK 0x40f0 + AA5A + StartLib mill.
		 * Keep 0x40f0 LINK. Dest 0x4248 RTS. */
		if (off >= 0x40f2u && off < 0x4248u)
			return 0;
		/* BRA.L pads after zeros. Dest 0x52bc0 LINK. */
		if (off >= 0x52ba0u && off < 0x52bc0u)
			return 0;
		/* BSR.W dest-edge onto displacement 0x1df96. Dest 0x1df98. */
		if (off >= 0x1df96u && off < 0x1df98u)
			return 0;
		/* BSR.W dest-edge 0x1df82 163e. Dest 0x1df84. Keep 0x1df80. */
		if (off >= 0x1df82u && off < 0x1df84u)
			return 0;
		/* BSR.W dest-edge 0x1ddf2 17ce. Dest 0x1ddf4. Keep 0x1ddf0. */
		if (off >= 0x1ddf2u && off < 0x1ddf4u)
			return 0;
		/* CMPI.L dest-edge onto #$3F3F3F3F. Dest 0x146b4. Keep 0x146ae. */
		if (off >= 0x146b0u && off < 0x146b4u)
			return 0;
		/* BRA.W dest-edge 0x5c626 + zeros. Dest 0x5c630. Keep 0x5c624. */
		if (off >= 0x5c626u && off < 0x5c630u)
			return 0;
		/* MOVE.W 31ef dest-edge onto 0004/0a24 + BRA.W.
		 * Keep 0x5c6d0. Dest 0x5c6e0 MOVEA.L (SP)+. */
		if (off >= 0x5c6d2u && off < 0x5c6e0u)
			return 0;
		/* BSR.W dest-edge 0x1ffd8. Dest 0x1ffda BNE. Keep 0x1ffd6. */
		if (off >= 0x1ffd8u && off < 0x1ffdau)
			return 0;
		/* After slot dest 0x24e80: HFSDispatch wrapper,
		 * TST/BGT waits, trap CMPI, JMP mill. Keep 0x24e80
		 * ADDQ. Dest 0x25490 (not dest-RTS 0x2548a). */
		if (off >= 0x24e82u && off < 0x25490u)
			return 0;
		/* ".BCscreen" after RTS 0x7cb8. Dest 0x7cc4 MOVE.W. */
		if (off >= 0x7cbau && off < 0x7cc4u)
			return 0;
		/* Word offset table before MOVEQ at 0xdb16
		 * plus list-walk BNE before RTS 0xdb0a. */
		if (off >= 0xdadcu && off < 0xdb56u)
			return 0;
		/* Icon bitstream folded into unpacker skip 0x46e8. */
		/* BSR.S exception stubs + RTE, not sequential code. */
		if (off >= 0x48f0u && off < 0x4920u)
			return 0;
		/* GetOSEvent BRA.L mill. Keep 0x49de MOVE SR and
		 * 0x4d4a MOVEA A7,A0 + A031. Dest 0x4d4a then
		 * dest 0x4e30 MOVEA.L (A7)+,A2. */
		if (off >= 0x49e0u && off < 0x49e2u)
			return 0;
		if (off >= 0x49f0u && off < 0x4d4au)
			return 0;
		if (off >= 0x4d52u && off < 0x4e30u)
			return 0;
		/* Packed 8001 table after 0x49b4 RTS plus
		 * MOVE.L D0,-(A1) fill mill. Dest 0x49dc RTS. */
		if (off >= 0x49b6u && off < 0x49dcu)
			return 0;
		/* DebugStr/DBF banner + EraseRect JMP (A2) mill.
		 * Keep 0x4e30 MOVEA.L (SP)+,A2. Dest 0x4e86 SWAP. */
		if (off >= 0x4e32u && off < 0x4e86u)
			return 0;
		/* Token scanner BSR prelude. Dest 0x5078 RTS. */
		if (off >= 0x4f52u && off < 0x5078u)
			return 0;
		/* SetOrigin digit helper + BEQ.W wait mill.
		 * Keep 0x4e86 SWAP. Dest 0x4f50 RTS (not 0x4ec0
		 * MOVE.L D0,(A0)+ dest-edge). */
		if (off >= 0x4e88u && off < 0x4f50u)
			return 0;
		/* DBF extension after fill. Keep 0x20888 DBF.
		 * Other field thunks stay skipped; blit JSR dest
		 * 0x208f8–0x20902 (2f30+RTS) stays legal. */
		if (off >= 0x2088au && off < 0x208f8u)
			return 0;
		/* Hole 0x20948–0x20952 (2f30+RTS, JSR dest of 0x210da). */
		if (off >= 0x20902u && off < 0x20948u)
			return 0;
		if (off >= 0x20952u && off < 0x20b80u)
			return 0;
		/* 0x20986 2d1f mill: dest-edge of thunk table.
		 * Dest 0x20b80 MOVEQ (blit body, not 2d1f). */
		if (off >= 0x20986u && off < 0x20b80u)
			return 0;
		/* 2f38 $073C mill + dest-RTS 0x20c80/0x20c92 +
		 * 2f38 $0740 JMP blit. Keep 0x20bda RTS and blit
		 * 0x20b80. Dest 0x20c9a CopyBits 2d1f (not inner
		 * 0x20ca4 / 0x8e770). */
		if (off >= 0x20bdcu && off < 0x20c9au)
			return 0;
		/* 2f30 + 60ff pad + QD CMP mill. Keep 0x20220.
		 * Dest 0x203da 2f38 thunk. */
		if (off >= 0x20222u && off < 0x203dau)
			return 0;
		/* DIVU.W #8 loop before SWAP. Keep 0x1ffea.
		 * Dest 0x20000 SWAP. */
		if (off >= 0x1ffecu && off < 0x20000u)
			return 0;
		/* QD helper SWAP/JSR dest-RTS mill. Keep 0x20000 SWAP.
		 * Dest 0x2012c after thunk RTS. */
		if (off >= 0x20002u && off < 0x2012cu)
			return 0;
		/* Addr helper mill prelude through thunks before
		 * the blit JSR dest. Keep 0x2065a BSR.
		 * Hole 0x208f8–0x20902. Dest 0x20b80 MOVEQ. */
		/* CLR.L (A0)+ / DBF fill from $2A(A4). Keep 0x20558 RTS.
		 * Dest 0x20574 MOVEM. */
		if (off >= 0x2055au && off < 0x20574u)
			return 0;
		if (off >= 0x2065cu && off < 0x208f8u)
			return 0;
		/* QD helper dest-RTS epilogue + CopyBits blit mill.
		 * Keep 0x75778. Dest 0x77240 MOVEA. */
		if (off >= 0x7577au && off < 0x77240u)
			return 0;
		if (off >= 0x251e4u && off < 0x251eeu)
			return 0;
		if (off >= 0x25226u && off < 0x25230u)
			return 0;
		if (off >= 0x2c8a6u && off < 0x2c8aeu)
			return 0;
		/* $037C queue walk CMPA A3,A4 / BRA. Keep 0x2a166.
		 * Dest 0x2a204 RTS. */
		if (off >= 0x2a168u && off < 0x2a204u)
			return 0;
		if (off >= 0x2ca40u && off < 0x2cab0u)
			return 0;
		/* ROM vers CMPI after EDisk dest RTS. Keep 0x2ca16. */
		if (off >= 0x2ca18u && off < 0x2ca3eu)
			return 0;
		/* ExpandMem $02B6+$2fc callback walker. Keep 0x2cbb0. */
		if (off >= 0x2cbb2u && off < 0x2cbd8u)
			return 0;
		if (off >= 0x2cbe2u && off < 0x2cc38u)
			return 0;
		/* Handle-size A-line mill after dest RTS 0x2d47e. */
		if (off >= 0x2d482u && off < 0x2dd18u)
			return 0;
		/* Gestalt-poll 64-bit mul. Keep 0x4e050. */
		if (off >= 0x4e052u && off < 0x4e374u)
			return 0;
		/* 'scrn' GetResource loop after HFS dest. Keep 0x4c1e0. */
		if (off >= 0x4c1e2u && off < 0x4c3d4u)
			return 0;
		/* HFS $0DD5 BTST only. Keep 0x4b1d0. Dest 0x4b1e8 RTS.
		 * Was 0x4c1d8 swallowing File Mgr. */
		if (off >= 0x4b1d2u && off < 0x4b1e8u)
			return 0;
		/* UTable / DriverDescription / ndrv mill.
		 * Keep 0x5dcd0. Dest 0x61080 next LINK. */
		if (off >= 0x5dcd2u && off < 0x61080u)
			return 0;
		/* Packed ffff offsets after JMP (A2) 0xf012. */
		if (off >= 0xf01au && off < 0xf168u)
			return 0;
		/* LINE-F words after RTS at 0x2b19c, not 68k. */
		if (off >= 0x2b19eu && off < 0x2b1aeu)
			return 0;
		if (off >= 0x2cb36u && off < 0x2cb50u)
			return 0;
		/* ".Backlight" Pascal name + 60ff pad. */
		if (off >= 0x2cb86u && off < 0x2cbb0u)
			return 0;
		if (off >= 0xa80a6u && off < 0xa8130u)
			return 0;
		/* 'pwpc' Gestalt probe. Keep 0xf7c0. */
		if (off >= 0xf7c2u && off < 0xf868u)
			return 0;
		if (off >= 0xf94eu && off < 0xf960u)
			return 0;
		/* DBF CLR.L/MOVE.B fill. Keep 0x8190.
		 * Dest was 0x81e2 dest-edge onto 4eb0 ext. Dest 0x81f0 LINK. */
		if (off >= 0x8192u && off < 0x81f0u)
			return 0;
		/* "Mc" pad after SoundDispatch RTS 0x81e6.
		 * Keep 0x81e2/0x81e6 RTS. Dest 0x81f0 LINK. */
		if (off >= 0x81e8u && off < 0x81f0u)
			return 0;
		/* JMP (d16,PC) trampolines back to 0x8220.
		 * Dest 0x82c0 was MOVE.W D0,$C(A6) ext 000c dest-edge.
		 * Dest 0x83a2 after last 4efa. Keep 0x81f0 LINK. */
		if (off >= 0x81f2u && off < 0x83a2u)
			return 0;
		/* Word table after RTS 0x8582. Dest 0x8592 RTS. */
		if (off >= 0x8584u && off < 0x8592u)
			return 0;
		/* Copy-outer DBF D3 that calls 0x27af2 stub. Keep 0x27afe RTS.
		 * Dest 0x27ca0 LINK. Not 0x27aea-0x27afe copy. */
		if (off >= 0x27b00u && off < 0x27ca0u)
			return 0;
		/* Packed MOVE.B/DBF 0x8f6a + dest copy 0x9264.
		 * Keep 0x8eaa RTS. Dest 0x932a MOVEQ #0. */
		if (off >= 0x8eb0u && off < 0x932au)
			return 0;
		/* 2f38 $07c8 JMP planted RTS. Keep 0x2596c RTS. Dest 0x25974 2d1f. */
		if (off >= 0x2596eu && off < 0x25974u)
			return 0;
		/* 2f38 $0770 unplanted. Keep 0x25f72 RTS. Dest 0x25f7a. */
		if (off >= 0x25f74u && off < 0x25f7au)
			return 0;
		/* MOVE.B (A4)+,(A1)+ DBF memcpy + dest 2d1f mill.
		 * Keep 0x28632 2f30. Dest 0x28786 2d1f. */
		if (off >= 0x28640u && off < 0x28786u)
			return 0;
		/* DrawMenuBar A977 / InsertMenu A972 recursive BSR mill.
		 * Keep 0x57ff4 MOVEM. Dest 0x58090 CLR.W $0A44. */
		if (off >= 0x58000u && off < 0x58090u)
			return 0;
		/* ADDQ/CMP/DBF D2 before KEEP 0x27aaa. Dest 0x27aaa 2d1f. */
		if (off >= 0x27a72u && off < 0x27aaau)
			return 0;
		/* "could not allocate ram..." after SysError.
		 * Keep 0x98fe A9C9. Dest 0x99a8 LEA. */
		if (off >= 0x9900u && off < 0x99a8u)
			return 0;
		/* "0123456789ABCDEF" after RTS 0x2838a.
		 * Dest 0x283b0 LINK. */
		if (off >= 0x2838cu && off < 0x283b0u)
			return 0;
		/* CopyBits/MaskBits DBcc mill. Keep LINK 0x81572. */
		if (off >= 0x815b0u && off < 0x816aeu)
			return 0;
		/* CopyBits MOVE.B/W (An)+ / DBF mill + BRA.S
		 * dest-BSR. Keep 0x81a22 JMP (A0). Dest 0x81b78 2f30.
		 * Not 0x8e770. */
		if (off >= 0x81a3cu && off < 0x81b78u)
			return 0;
		/* CopyBits StdBits dest-RTS. Keep 0x81d00 LINK.
		 * Dest 0x81d80 MOVEQ #10. */
		if (off >= 0x81d02u && off < 0x81d80u)
			return 0;
		/* CopyBits inner MOVE.W (An)+ / DBF mill.
		 * Keep 0x85900. Dest 0x85968 MOVEA.L D5,A1. */
		if (off >= 0x85902u && off < 0x85968u)
			return 0;
		/* CopyBits/MaskBits shift+DBcc mill + BRA.W loopback.
		 * Keep 0x870f0 JMP (A2). Dest 0x8e770 LINK. Not skip 0x8e770. */
		if (off >= 0x870f2u && off < 0x8e770u)
			return 0;
		/* CopyBits A6-frame mill at 0x80000. Dest 0x806d0 CMP.
		 * Not 0x8e770. */
		if (off >= 0x80000u && off < 0x806d0u)
			return 0;
		/* Blit MOVE.L (A2)+ / BNE / DBF D2 0x7a40a. Keep 0x7a400 RTS.
		 * Dest 0x7a538 MOVEM. Not 0x8e770. */
		if (off >= 0x7a402u && off < 0x7a538u)
			return 0;
		/* CopyBits scanline + A6-frame LINK #-776 + MaskBits JMP (A4)
		 * DBF 0x88098. Keep 0x82818 RTS. Dest 0x8e770 LINK. */
		if (off >= 0x8281au && off < 0x8e770u)
			return 0;
		/* Alloca CLR.L -(SP)/DBF 0x74986 + AND.W DBF 0x74c8a
		 * + fill stubs 0x74dea. Keep 0x7465e JMP (A0).
		 * Dest 0x7509e MOVEQ #0. */
		if (off >= 0x74660u && off < 0x7509eu)
			return 0;
		/* AddResource mill after scrn dest through
		 * dest-edge LINK cluster. Keep 0x4c3e0.
		 * Dest 0x4f7c0 next LINK after RTS 0x4f7b6. */
		if (off >= 0x4c3e2u && off < 0x4f7c0u)
			return 0;
		/* Token scanner tail after dest RTS 0x4ffa. */
		if (off >= 0x4ffcu && off < 0x5078u)
			return 0;
		/* VIA/SCC BTST+NOP waits. Keep 0x9500.
		 * Dest was dest-RTS 0x97b8 into 2f30 thunks.
		 * Dest 0x9800 LEA $0D92. */
		if (off >= 0x9502u && off < 0x9800u)
			return 0;
		/* VIA $1a00 BTST waits + BRA mill. Keep 0x8fc4.
		 * Dest 0x90a8 MOVE.B. */
		if (off >= 0x8fc6u && off < 0x8fc8u)
			return 0;
		if (off >= 0x9010u && off < 0x9018u)
			return 0;
		if (off >= 0x9038u && off < 0x9044u)
			return 0;
		if (off >= 0x905eu && off < 0x9066u)
			return 0;
		if (off >= 0x9088u && off < 0x908au)
			return 0;
		if (off >= 0x909cu && off < 0x90a8u)
			return 0;
		/* Dest-edge 2f30 after RTS 0xa2f2. Keep 0xa2f2.
		 * Dest 0xa340 MOVE.L #imm. */
		if (off >= 0xa2f4u && off < 0xa340u)
			return 0;
		/* Offset table after VIA RTS 0x9886. Keep 0x9886.
		 * Dest 0x989c MOVEM. */
		if (off >= 0x9888u && off < 0x989cu)
			return 0;
		/* TST.B $A0(A0) / BEQ wait. Keep 0x1b930 LINK.
		 * Dest 0x1b94e MOVEQ #0. */
		if (off >= 0x1b944u && off < 0x1b94eu)
			return 0;
		/* MOVE.B D7 / BNE.S * wait after PC-idx 0x16d9c.
		 * Skip ADDQ.L #4,A7 (*4 mill). Dest 0x16dae MOVEA. */
		if (off >= 0x16daau && off < 0x16daeu)
			return 0;
		/* JSR (A0) / TST.W 6(A3) / BGT retry. Keep 0x1cc80
		 * LINK. Dest 0x1ccdc TST.L. */
		if (off >= 0x1ccc6u && off < 0x1ccdcu)
			return 0;
		/* Zeros after ADB dest-edge RTS 0xe072. Keep 0xe072
		 * and 0xe080 BRA.L trampoline to 0x4926 MOVE SR. */
		if (off >= 0xe074u && off < 0xe080u)
			return 0;
		if (off >= 0xe086u && off < 0xe270u)
			return 0;
		/* TimeDBRA $0D00 + ADB record DBF 0xe666/0xe7ae.
		 * Keep 0xe616 BRA. Dest 0xe99c MOVEQ #0. */
		if (off >= 0xe61au && off < 0xe99cu)
			return 0;
		/* Word after JMP (A0) 0xe3cc. Dest 0xe3d8 LINK. */
		if (off >= 0xe3ceu && off < 0xe3d8u)
			return 0;
		/* Shift/mask table after RTS 0x8b7c. Dest 0x8bae MOVEA. */
		if (off >= 0x8b7eu && off < 0x8baeu)
			return 0;
		/* Vector table after JMP (A0) 0xa026. Dest 0xa0c6 LINK. */
		if (off >= 0xa028u && off < 0xa0c6u)
			return 0;
		/* IRQ A1=-8 CMPI/MOVE.L $020C mill. Keep 0xa00a
		 * SUBQ. Dest 0xa01e MOVEA.L $01D4. */
		if (off >= 0xa00cu && off < 0xa01eu)
			return 0;
		/* Zeros after MixedMode 2f30 RTS 0x9fdd6. Keep 0x9fdce
		 * 2f30 and 0x9fdcc JMP (A0). Dest 0x9fde0 LINK. */
		if (off >= 0x9fdd8u && off < 0x9fde0u)
			return 0;
		/* Slot helper mid-body mill. 2f30 ([0x202C],$134) lands
		 * on 0x134ca AND.L with odd A6. Keep 0x133e0 LINK.
		 * Dest 0x13620 next LINK. */
		if (off >= 0x13400u && off < 0x13620u)
			return 0;
		/* Zero after RTE 0x498c. Keep 0x498c. Dest 0x4990 LEA. */
		if (off >= 0x498eu && off < 0x4990u)
			return 0;
		/* 60ff pads after sRsrc RTS 0x1568e. Keep 0x1568e.
		 * Dest 0x156d0 LINK. */
		if (off >= 0x15690u && off < 0x156d0u)
			return 0;
		/* Zeros after JMP (A0) 0xf1aa. Keep 0xf1aa. Dest 0xf1b0 LINK. */
		if (off >= 0xf1acu && off < 0xf1b0u)
			return 0;
		/* Zero after JMP (A0) 0xf18c. Keep 0xf18c. Dest 0xf190 LINK. */
		if (off >= 0xf18eu && off < 0xf190u)
			return 0;
		/* TST.B (A1) / BEQ * hardware wait. Keep 0x8dd0.
		 * Dest 0x8dd6 LEA. */
		if (off >= 0x8dd2u && off < 0x8dd6u)
			return 0;
		/* BSET #7,$0376 / BEQ * one-shot. Keep 0x2574a BRA.
		 * Dest 0x25754 MOVEM. */
		if (off >= 0x2574cu && off < 0x25754u)
			return 0;
		/* SWAP/LSR store mill A0=$03A4. Keep 0x256b2 CLR.W.
		 * Dest 0x256cc MOVE.L $0720,-(SP). Not dest-RTS. */
		if (off >= 0x256b4u && off < 0x256ccu)
			return 0;
		/* Duplicate SWAP + BRA.W dest-edge onto 0x256ce.
		 * Keep 0x257da CLR. Dest 0x257fa MOVE.L $0728,-(SP). */
		if (off >= 0x257dcu && off < 0x257fau)
			return 0;
		/* BNE.W * self mill. Keep 0x8a4e. Dest 0x8a52. */
		if (off >= 0x8a50u && off < 0x8a52u)
			return 0;
		/* ADB/MemTop/TimeDBRA/FCBSPtr mill through SCSI
		 * dest-edge stubs. Keep 0xafae. */
		if (off >= 0xafb0u && off < 0xdfeau)
			return 0;
		/* GetResource #'#' probe. Keep 0x40040. */
		if (off >= 0x40042u && off < 0x40106u)
			return 0;
		/* DrvQ/JSwap BRA * and NewPtr waits. Keep 0x25000.
		 * Dest was dest-RTS 0x251bc. Dest 0x251be MOVEA. */
		if (off >= 0x25002u && off < 0x251bcu)
			return 0;
		/* Zeros after DrvQ BRA 0x251ca. Keep 0x251ca.
		 * Dest 0x251d0 BCLR abs.W. */
		if (off >= 0x251ccu && off < 0x251d0u)
			return 0;
		/* FB size probe BSR 2f30. Keep 0x44fc0. */
		if (off >= 0x44fc2u && off < 0x44ff4u)
			return 0;
		if (off >= 0x8e764u && off < 0x8e770u)
			return 0;
		/* Dest-RTS UNLK 0x14318 + catalog mill LINK 0x14320
		 * through dest-RTS 0x14616. Keep 0x142e0 LINK.
		 * Dest 0x14620 LINK #-232. */
		if (off >= 0x14318u && off < 0x14620u)
			return 0;
		/* Queue walk ADDQ A2 / CMP.L (A2)+ / DBEQ 0x660f2.
		 * Keep 0x660d2 RTS. Dest 0x66158 MOVE.W. */
		if (off >= 0x660d4u && off < 0x66158u)
			return 0;
		/* JMP idx 0x156e8 table mill + dest-RTS UNLK 0x1579a.
		 * Keep 0x1568e RTS. Dest 0x157a0 LINK. */
		if (off >= 0x15690u && off < 0x157a0u)
			return 0;
		/* FCB/A4==0 returns -50 mill. Keep 0x15c1a RTS.
		 * Dest 0x15cb0 LINK #-8. */
		if (off >= 0x15c20u && off < 0x15cb0u)
			return 0;
		/* LSR.L mill A0=$8F000000. Keep 0x14d16 RTS.
		 * Dest 0x14d50 LINK. */
		if (off >= 0x14d20u && off < 0x14d50u)
			return 0;
		/* SlotManager A06e mill. Keep 0x1735a RTS.
		 * Dest 0x173da MOVEM restore. */
		if (off >= 0x1735cu && off < 0x173dau)
			return 0;
		/* 2f30 dest-RTS 0x17d66/0x17d6e/0x17d78. Keep 0x17d64 RTS.
		 * Dest 0x17d90 MOVEQ #0. */
		if (off >= 0x17d66u && off < 0x17d90u)
			return 0;
		/* Word offsets after RTS 0x803e. Dest 0x8052 MOVE.W. */
		if (off >= 0x8040u && off < 0x8052u)
			return 0;
		/* Pascal "InitItt" after RTS 0x18c28. Dest 0x18c50 LINK. */
		if (off >= 0x18c2au && off < 0x18c50u)
			return 0;
		/* SCSIAtomic/SCSIDispatch mill cluster. Keep 0x1914a LEA.
		 * Dest 0x1a7d0 LINK (memset KEEP, exclusive of dest-RTS 0x1a4ca). */
		if (off >= 0x1914cu && off < 0x1a7d0u)
			return 0;
		/* Word after RTD 0x5cbb0. Dest 0x5cbb8 MOVEA. */
		if (off >= 0x5cbb4u && off < 0x5cbb8u)
			return 0;
		/* ABEB DisplayDispatch helper after RTS 0x91940.
		 * Keep 0x91940 RTS. Dest 0x91980 LINK #-28. */
		if (off >= 0x91942u && off < 0x91980u)
			return 0;
		/* QD list walk CMP.B / BLT plus DBF D7 0x95ee0
		 * and LINK #-52 0x963f0 mill. Keep 0x95ed0 RTS.
		 * Dest 0x96788 RTS. Not 0x8e770. */
		if (off >= 0x95ee2u && off < 0x96788u)
			return 0;
		/* VIA/ADB BTST wait 0x94e6 + 60fe. Keep 0x94c4 JMP (A0).
		 * Dest 0x96a6 MOVE.L (SP)+,D4. */
		if (off >= 0x94c8u && off < 0x96a6u)
			return 0;
		if (off >= 0x2d210u && off < 0x2d250u)
			return 0;
		if (off >= 0x7b46u && off < 0x7bbcu)
			return 0;
		/* F-line fe0a + DBF D2 bit-scan. Keep 0x7bd0 MOVEM.
		 * Dest 0x7c06 MOVEQ #0 (not dest-RTS 0x7c0c). */
		if (off >= 0x7bd2u && off < 0x7c06u)
			return 0;
		/* 2d1f + MOVE.W (A0)+,(A1)+ DBF with A0=A1=$2A.
		 * Keep 0x27aaa 2d1f. Dest 0x27ad8 MOVEM restore. */
		if (off >= 0x27ab0u && off < 0x27ad8u)
			return 0;
		/* Dest-RTS + offset words + JMP (A6). Keep 0x77ce RTS.
		 * Dest 0x78d2 MOVEQ #1. */
		if (off >= 0x77d0u && off < 0x78d2u)
			return 0;
		/* 60ff/JMP (A6) 68k-emul mill 0x7546. Keep 0x74d4 BRA.L.
		 * Dest 0x76b0 LINK. */
		if (off >= 0x7516u && off < 0x76b0u)
			return 0;
		if (off >= 0x14fcau && off < 0x14fd0u)
			return 0;
		/* Slot/sResource helper UNLK with A6=0 + dest LINK mill.
		 * Keep 0x137b0. Dest 0x138b0 LINK. */
		if (off >= 0x137b2u && off < 0x138b0u)
			return 0;
		/* GetCatInfo wrapper: A260 sel 9 + BRA dest-edge
		 * onto 0x13f6c ext 0030. Keep nothing; dest 0x13f76 RTS. */
		if (off >= 0x13f00u && off < 0x13f76u)
			return 0;
		/* Catalog/sResource walker. Dest 0x1425a RTS. */
		if (off >= 0x13f80u && off < 0x1425au)
			return 0;
		/* Cat-err helper. Keep 0x14bee. Dest 0x14c60 RTS. */
		if (off >= 0x14bf0u && off < 0x14c60u)
			return 0;
		/* GetCCursor BEQ nil. Keep 0x5c870. Dest 0x5c87e
		 * MOVE.L A4,-(SP) before AA68 (was dest 0x5c896
		 * swallowing DialogDispatch). */
		if (off >= 0x5c872u && off < 0x5c87eu)
			return 0;
		/* A991 BNE retry KEEP 0x5c89e. Do not skip 0x5c8a0
		 * CMP/BNE — dest 0x5c8a8 was DisposeDialog. */
		/* Stretch/scaler mill with A0=$0C0C. Dest 0xa3a34
		 * RTS. Keep 0xa37a8. Not CopyBits 0x20ca4. */
		if (off >= 0xa37c0u && off < 0xa3a34u)
			return 0;
		/* Bit-scan F-line fe0a + DBF. Keep 0xa494 MOVE.L.
		 * Dest 0xa4a4 MOVEQ #0. */
		if (off >= 0xa496u && off < 0xa4a4u)
			return 0;
		/* PixMap NewPtr+$4A mill + dest-RTS 0x20ff6 +
		 * 2f38 $0748 JMP blit. Dest 0x20ffe 2d1f. */
		if (off >= 0x20f3eu && off < 0x20ffeu)
			return 0;
		/* Duplicate PixMap mill. Keep 0x1ea44 RTS.
		 * Dest 0x1ebe0 RTS. */
		if (off >= 0x1ea50u && off < 0x1ebe0u)
			return 0;
		/* Zeros + 60ff after PixMap size RTS 0x210e2.
		 * Dest 0x21100 MOVE SR. */
		if (off >= 0x210e4u && off < 0x21100u)
			return 0;
		/* EOR.W/B/L (A1) / DBNE mill + BRA.S back. Keep
		 * 0x1e69c RTS. Dest 0x1e6fc RTS. */
		if (off >= 0x1e69eu && off < 0x1e6fcu)
			return 0;
		/* PackBits fill MOVE.W #$FFFF,(A0)+ / DBF. Keep
		 * 0x1e6fc RTS. Dest 0x1e794 RTS. */
		if (off >= 0x1e76au && off < 0x1e794u)
			return 0;
		/* PackBits/error mill returns $FE60. Keep 0x1e794 RTS.
		 * Dest 0x1e874 RTS. */
		if (off >= 0x1e796u && off < 0x1e874u)
			return 0;
		/* FCB/QD walk after CLR DBF RTS 0x1e8bc. Dest 0x1e912
		 * 2f30. Keep 0x1e8bc and 0x1e9d0. */
		if (off >= 0x1e8beu && off < 0x1e912u)
			return 0;
		/* CopyBits helper MOVEA.L A3,A0 with A3=0 plus
		 * 2f38 $0740 JMP blit. Dest 0x20c9a. Not inner 0x20ca4. */
		if (off >= 0x20c82u && off < 0x20c9au)
			return 0;
		/* CopyBits tail + dest-RTS 0x20f36 + 2f38 $0744/$0748
		 * JMP blit. Keep inner 0x20ca4-0x20cc4 AND skip-start
		 * 0x20cc4. Dest 0x20ffe 2d1f. Not 0x8e770. */
		if (off >= 0x20cc6u && off < 0x20ffeu)
			return 0;
		/* CFM InitRoutineDescriptor + 'pwpc' mill. Keep 0xf300 RTS.
		 * Dest 0xf600 RTS. Strings at 0xf430 are data. */
		if (off >= 0xf310u && off < 0xf600u)
			return 0;
		/* 60ff pad after CFM RTS. Dest 0xf620 LINK. */
		if (off >= 0xf602u && off < 0xf620u)
			return 0;
		/* CFM JSR (A4) with A6=0. Keep 0xfc02 RTS. Dest 0xfc34 RTS. */
		if (off >= 0xfc10u && off < 0xfc34u)
			return 0;
		/* Zero pad after sRsrc wrap RTS at 0x150a2. */
		if (off >= 0x150a4u && off < 0x150b0u)
			return 0;
		/* Slot/sResource iterator through ExecMgr dest-edge
		 * stubs. Keep 0x1cf20. Dest 0x1dd38 A9C9. */
		if (off >= 0x1cf22u && off < 0x1dd38u)
			return 0;
		/* HFS catalog walk body. Keep 0x30e56 legal. */
		if (off >= 0x30e58u && off < 0x311e0u)
			return 0;
		/* HFS vol body + MOVEQ/BSR trampoline after RTS 0x305b6.
		 * Keep 0x304be LINK and 0x304bc MOVEA.L (SP)+,A0.
		 * Dest 0x305c0 LEA. */
		if (off >= 0x304c0u && off < 0x305c0u)
			return 0;
		/* CMP.L $0A06 / BNE wait plus dest-BSR BRA 0x289fe.
		 * Keep 0x289da RTS. Dest 0x28a04 2d1f. */
		if (off >= 0x289dcu && off < 0x28a04u)
			return 0;
		/* DBEQ D4 wait A0=A1 garbage. Keep 0x2891e RTS. Dest 0x28976 2d1f. */
		if (off >= 0x28922u && off < 0x28976u)
			return 0;
		/* SlotMgr/'gama' Gestalt body. Keep 0xe46 legal. */
		if (off >= 0xe48u && off < 0xf44u)
			return 0;
		if (off >= 0x1000u && off < 0x1040u)
			return 0;
		/* USB Family Expert Lib name + GetResource probe. */
		if (off >= 0x194cu && off < 0x1c80u)
			return 0;
		/* 2f30 dest-RTS mill 0x1888a/0x18892. Keep 0x18884 PEA.
		 * Dest 0x188da LEA (existing MM KEEP). */
		if (off >= 0x1888au && off < 0x188dau)
			return 0;
		/* 2f30 thunk mill + A001 Open walk. Keep 0x188da LEA
		 * opcode (not ext 0018 at 0x188dc). Dest 0x18b60. */
		if (off >= 0x188dcu && off < 0x18b60u)
			return 0;
		/* ROR/TST.B/BEQ wait with A1=0. Keep 0x186fc.
		 * Dest 0x18708 MOVE.L. */
		if (off >= 0x186feu && off < 0x18708u)
			return 0;
		/* CLR.B (A1)+ / DBF memset mill. Keep 0x1a7d0 LINK.
		 * Dest 0x1a7f0 LINK. */
		if (off >= 0x1a7d2u && off < 0x1a7f0u)
			return 0;
		/* Checksum MOVE.B (A0)+ / ADD / LSR / DBF. Keep
		 * 0x1a8d6 RTS. Dest 0x1a902 RTS. */
		if (off >= 0x1a8e0u && off < 0x1a902u)
			return 0;
		/* 60ff pad after RTS 0x129a. Dest 0x12c0 LINK. */
		if (off >= 0x129cu && off < 0x12c0u)
			return 0;
		/* Pascal "AAPL,prepare_ordername" + 60ff pad.
		 * Keep 0x1476 RTS. Dest 0x14c0 LINK. */
		if (off >= 0x1478u && off < 0x14c0u)
			return 0;
		/* Pascal "cOS,Name" + 60ff pad. Dest 0x1670 LINK. */
		if (off >= 0x1650u && off < 0x1670u)
			return 0;
		/* FCB mill + dest-RTS 0x5be02 + zeros.
		 * Keep 0x5ae76. Dest 0x5be10 MOVEA (not 0x5be02 RTS).
		 * Hole SectRect LINK 0x5b110 .. JMP (A0) 0x5b164. */
		if (off >= 0x5ae78u && off < 0x5be10u &&
		    !(off >= 0x5b110u && off < 0x5b166u))
			return 0;
		/* ndrv Gestalt/GetResource body nested in UTable mill.
		 * Keep 0x60f80. Dest 0x61080. */
		if (off >= 0x60f82u && off < 0x61080u)
			return 0;
		/* HFS GetFPos/SetFPos/HLock helper. Keep 0x63048. */
		if (off >= 0x6304au && off < 0x63100u)
			return 0;
		/* VMVectors $0CF0 + JSR (A0) self mill at 0xec18
		 * + MOVE.W SR mill + offset table + JMP (A0).
		 * Keep 0xeac6 RTS. Dest 0xf170 LINK. */
		if (off >= 0xeac8u && off < 0xf170u)
			return 0;
		/* Pascal "Unknown MPDispatch selector:" plus
		 * JMP (A0) error glue. Keep 0xde10 legal. */
		if (off >= 0xde12u && off < 0xded0u)
			return 0;
		/* MP/table walk + 2f30 dest-edge mill at 0x9fd9a
		 * through fill loop dest-edge. Keep 0x9fd98 A100.
		 * Dest 0xa08d0 next LINK after RTS 0xa08ce. */
		if (off >= 0x9fd9au && off < 0xa08d0u)
			return 0;
		if (off >= 0x2cf66u && off < 0x2cf70u)
			return 0;
		if (off >= 0x2cdfau && off < 0x2ce00u)
			return 0;
		/* RTD #10 ext 000a at 0x2cc3a plus zero pad. */
		if (off >= 0x2cc3au && off < 0x2cc40u)
			return 0;
		if (off >= 0x2cba0u && off < 0x2cbb0u)
			return 0;
		if (off >= 0x2cd36u && off < 0x2cd40u)
			return 0;
		/* EDisk CLR.L DBF dest was RTS 0x2c8e4. Dest 0x2ca0e CLR.W. */
		if (off >= 0x2c858u && off < 0x2ca0eu)
			return 0;
		/* EventQueue $0D66 walk + DBF. Keep 0x38b16 RTS.
		 * Dest 0x38b5e A9E6 InitWindows (0x38b58 ADDQ/BRA.B
		 * dest-BSR mill back to 0x38b18). */
		if (off >= 0x38b18u && off < 0x38b5eu)
			return 0;
		/* ROM reset MOVE SR/RESET + SANE MOVE.W #$2600,A7.
		 * Keep 0xb4. Dest 0x112 BSR.L. */
		if (off >= 0xb6u && off < 0x112u)
			return 0;
		/* JMP (d8,PC,Xn) switch tables: include the ext word so
		 * g3_fix cannot land on 0002/00f6 and spin. */
		if (off >= 0xb7802u && off < 0xb781au)
			return 0;
		/* SANE through last PACK/INF tables. Keep 0xb77b4
		 * legal; 68k resumes at 0x198154 after PPC libs. */
		if (off >= 0xb77b6u && off < 0xc0000u)
			return 0;
		/* ExecMgr/GetVolume C++ thunks plus DRVR I/O mill.
		 * Keep 0x198154. */
		if (off >= 0x198156u && off < 0x19b806u)
			return 0;
		/* TimeDBRA/VIA nested DBF + dest copy 0x9e6a.
		 * Keep 0x9c80. Dest 0xa008 RTS (0x9c8a MOVEM
		 * restore collapsed unique). */
		if (off >= 0x9c82u && off < 0xa008u)
			return 0;
		/* VIA $1a00(A1) BTST/BEQ wait. Keep 0x9bcc.
		 * Dest 0x9bd4 MOVEM. */
		if (off >= 0x9bceu && off < 0x9bd4u)
			return 0;
		/* VIA $1a00 BTST/BEQ after MOVE SR + NOP pad.
		 * Keep 0x9c54 RTS. Dest 0x9c80 TimeDBRA. */
		if (off >= 0x9c56u && off < 0x9c80u)
			return 0;
		/* c2pstr/uncompress_world/relocate_world 68k glue
		 * in the ExecMgr–PPC gap. Keep 0x19b806. */
		if (off >= 0x19b808u && off < 0x19cf62u)
			return 0;
		/* $0CF8 queue walk DBEQ. Dest 0x2b328 MOVEQ #0 RTS.
		 * Keep 0x2b308. Not 0x8e770 / CopyBits inner. */
		if (off >= 0x2b30au && off < 0x2b328u)
			return 0;
		/* $015D BTST wait body. Keep 0x2b32c. */
		if (off >= 0x2b32eu && off < 0x2b418u)
			return 0;
		/* Zone walk including prelude and dest RTS. Keep 0xa588.
		 * Dest 0xa928. */
		if (off >= 0xa590u && off < 0xa928u)
			return 0;
		/* VIA SR-level poll DBEQ. Keep 0xaf60 RTS. Dest 0xafac RTS. */
		if (off >= 0xaf62u && off < 0xafacu)
			return 0;
		/* $08A8 CurActivate wait + ABEB. Keep 0x511bc RTS.
		 * Dest 0x51220 RTS. */
		if (off >= 0x511c0u && off < 0x51220u)
			return 0;
		/* Offset table after VIA BRA.L 0x74d4 folded into
		 * VIA wait skip 0x70a6 dest 0x7510. */
		/* 60ff pad + zeros after JMP (A6) 0x7682. Keep 0x7684.
		 * Dest 0x76b0 LINK. */
		if (off >= 0x7686u && off < 0x76b0u)
			return 0;
		/* BRA.L pad after RTS 0x7aea. Keep 0x7aea. Dest 0x7af8 MOVEM. */
		if (off >= 0x7aecu && off < 0x7af8u)
			return 0;
		/* VIA $015E wait + Cuda packet + BRA.L table.
		 * Keep 0x70a4 BMI. Dest 0x7510 MOVEM. */
		if (off >= 0x70a6u && off < 0x7510u)
			return 0;
		/* JMP (A2) switch table. Keep 0x21318 JMP. Dest 0x214da 2f30. */
		if (off >= 0x2131au && off < 0x214dau)
			return 0;
		/* BRA.W dest-edges onto displacement. Keep 0x21524/0x21570/0x21596. */
		if (off >= 0x21526u && off < 0x21528u)
			return 0;
		if (off >= 0x21572u && off < 0x21574u)
			return 0;
		if (off >= 0x21598u && off < 0x2159au)
			return 0;
		/* BSR.W dest-edges. Keep 0x224ac/0x224d4. */
		if (off >= 0x224aeu && off < 0x224b0u)
			return 0;
		if (off >= 0x224d6u && off < 0x224d8u)
			return 0;
		/* BSR.W dest-edge 0xa54a. Dest 0xa54c. Keep 0xa548. */
		if (off >= 0xa54au && off < 0xa54cu)
			return 0;
		/* MOVEA.W #-1 wait folded into MM thunk skip 0x21180. */
		/* QD $034B poll; dest-edge onto BEQ.W disp 0x22460.
		 * Keep 0x2245e 6700. Dest 0x224a0 BLT. Not 0x8e770. */
		if (off >= 0x22460u && off < 0x224a0u)
			return 0;
		/* GetResource 'pref' mill through dest-edge LINK
		 * 0x437b0. Keep LINK 0x432c0. */
		if (off >= 0x432c2u && off < 0x4381eu)
			return 0;
		/* CMPI.W #6 helper BSR mill. Keep 0x16d30. */
		if (off >= 0x16d32u && off < 0x16d78u)
			return 0;
		if (off >= 0x2c2c0u && off < 0x2c2d2u)
			return 0;
		/* Packed 68k offset tables (0xFFC0xxxx), not code.
		 * Dense through 0x6c90; real 68k resumes at 0x6c94. */
		if (off >= 0x5080u && off < 0x6c94u)
			return 0;
		/* RTD #4 ext 0004 + zeros. Keep 0x50ace RTD.
		 * Dest 0x50ae0 LINK. */
		if (off >= 0x50ad0u && off < 0x50ae0u)
			return 0;
		/* Packed 00a6 table after RTS 0x50e08 plus 60ff dest-edge.
		 * Keep 0x50e08. Dest 0x51100 MOVE.L (A7)+. */
		if (off >= 0x50e0au && off < 0x51100u)
			return 0;
		/* Zeros after JMP (A0) 0x51128. Dest 0x51130 LINK. */
		if (off >= 0x5112au && off < 0x51130u)
			return 0;
		/* Offset table after JMP (A0) 0x5123e. Dest 0x512d0 LINK. */
		if (off >= 0x51240u && off < 0x512d0u)
			return 0;
		/* JMP (A1) mill via $204C/$7C. Keep 0x4b1ae RTS.
		 * Dest 0x4b1c0 MOVE.L A0,-(SP). */
		if (off >= 0x4b1b0u && off < 0x4b1c0u)
			return 0;
		/* 60ff pad after RTS 0x4211a. Dest 0x42130 LINK. */
		if (off >= 0x4211cu && off < 0x42130u)
			return 0;
		/* OF NameRegistry/AA5A property walk + packed strings.
		 * Dest 0x3f68 MOVEM (packed dest). */
		if (off >= 0x2760u && off < 0x3f68u)
			return 0;
		/* GetOSTrapAddress + CMPA.L $02AE mill. Keep 0x3f68 MOVEM.
		 * Dest 0x3f8c MOVEM restore (BCS-taken; not dest-RTS 0x3f90). */
		if (off >= 0x3f6eu && off < 0x3f8cu)
			return 0;
		/* DisplayDispatch ABE9 mill. Keep 0x48e30 LINK.
		 * Dest 0x49136 UNLK (not dest-RTS 0x49138 / OF name blob). */
		if (off >= 0x48e32u && off < 0x49136u)
			return 0;
		/* ExpandMem+'EMDN' +0x54 walk with A0=0. Keep 0x47ac0 LINK.
		 * Dest 0x47b26 UNLK. */
		if (off >= 0x47ac2u && off < 0x47b26u)
			return 0;
		/* KCHR/keymap/font tables; 0xb77b4 is the next LINK. */
		if (off >= 0xaec00u && off < 0xb77b4u)
			return 0;
		/* SANE EOR.B/DBF mill. Keep 0xa88c0. Dest 0xa88e2. */
		if (off >= 0xa88c2u && off < 0xa88e2u)
			return 0;
		/* SANE ADDQ/ADDA/BRA mill. Keep 0xa8816. Dest 0xa882e. */
		if (off >= 0xa8818u && off < 0xa882eu)
			return 0;
		/* SANE byte-copy SUBQ/BCC mill. Dest 0xa884c ADDI. */
		if (off >= 0xa8848u && off < 0xa884cu)
			return 0;
		/* "cptr" Pascal string after RTS 0xa8882. Dest 0xa8890. */
		if (off >= 0xa8884u && off < 0xa8890u)
			return 0;
		/* SANE memcpy MOVE.L (A1)+,(A0)+ unroll. Keep
		 * 0xa88ec RTS. Dest 0xa894a RTS. */
		if (off >= 0xa8900u && off < 0xa894au)
			return 0;
		/* "memcpy" after RTS 0xa894a. Keep 0xa8960. */
		if (off >= 0xa894cu && off < 0xa8960u)
			return 0;
		/* memset/strcat plus SANE FP helper. Keep 0xa8960. */
		if (off >= 0xa8962u && off < 0xaec00u)
			return 0;
		/* PPC libraries (dense blr/mfspr/fsub). Last 68k
		 * LINK before this is 0xba824; 68k resumes 0x198154.
		 * A second PPC hole runs 0x1a8000-0x1c8d50. */
		if (off >= 0xc0000u && off < 0x198154u)
			return 0;
		/* C++ symbols + PPC after last 68k RTS 0x19cf60
		 * through ROM blob. Dest 0x1d2000. */
		if (off >= 0x19cf62u && off < 0x1d2000u)
			return 0;
		/* New World ROM resource/string blob, then PPC
		 * (0x240000 beq/b) through the 68k emulator. */
		/* USB name strings + resource/PPC blob. */
		if (off >= 0x1d2000u && off < 0x400000u)
			return 0;
		/* ndrv name strings + TimeDBRA DBF mill.
		 * Keep 0x1d1b20. */
		if (off >= 0x1d1b22u && off < 0x1d2000u)
			return 0;
		/* Packed name/offset table after BRA.W at 0x4df0. */
		if (off >= 0x4df4u && off < 0x4e16u)
			return 0;
		return 1;
	}
	/* Empty lowmem is not 68k (was executing 0000/0440/1000).
	 * Planted ExpandMem stub at RAM+0x9000 is at 0x10009000. */
	return 0;
}
/* 2f30/2f38+RTS dest must look like a routine start.
 * Mid-body ALU (0x134ca AND.L) is g3_r24_ok but mills with a
 * garbage A6. Keep blit BCLR 0x20b96 (08xx). */
static int g3_68k_entry(uint32 p)
{
	uint32 op;
	if (!g3_r24_ok(p))
		return 0;
	/* Planted grafProc/FileMgr RTS. 2f30+RTS JMP here mills. */
	if (p == ROMBase + 0x20658u)
		return 0;
	if (p == ROMBase + 0x0feb0u)
		return 0;
	if (p == ROMBase + 0x18892u)
		return 0;
	if (p == ROMBase + 0x1579au)
		return 0;
	if (p == ROMBase + 0x2c24eu)
		return 0;
	if (p == ROMBase + 0x2c258u)
		return 0;
	if (p == ROMBase + 0x2c8e4u)
		return 0;
	/* MixedMode hole RTS. 2f30+RTS JMP here mills
	 * (same class as planted 0x20658). KEEP 0x20948 2f30. */
	if (p == ROMBase + 0x20950u) {
#if NW_BOOT_LOG
		static unsigned n20950;
		if (n20950 < 8) {
			n20950++;
			nw_boot_log("G3: 68k reject 2f30 RTS 0x20950");
		}
#endif
		return 0;
	}
	op = vm_read_memory_2(p);
	if (op == 0x4e56u || op == 0x4e75u || op == 0x4e74u ||
	    op == 0x4e73u || op == 0x4e71u ||
	    op == 0x48e7u)
		return 1;
	if ((op & 0xf000u) == 0xa000u)
		return 1;
	if ((op & 0xf000u) == 0x7000u)
		return 1;
	if ((op & 0xf000u) == 0x6000u)
		return 1;
	if ((op & 0xffc0u) == 0x4e80u || (op & 0xffc0u) == 0x4ec0u)
		return 1;
	if ((op & 0xf1c0u) == 0x41c0u)
		return 1;
	if ((op & 0xfff8u) == 0x4870u || op == 0x4878u || op == 0x487au)
		return 1;
	if ((op & 0xff00u) == 0x2f00u)
		return 1;
	if ((op & 0xff00u) == 0x0800u)
		return 1;
	if ((op & 0xf1c0u) == 0x2068u || (op & 0xf1c0u) == 0x206eu)
		return 1;
	if ((op & 0xff00u) == 0x4a00u || (op & 0xff00u) == 0x4200u)
		return 1;
	return 0;
}
static uint32 g3_fix_r24(uint32 r24)
{
	static uint32 last_rom;
	const uint32 in = r24;
	r24 = g3_rom0(r24);
	if (g3_r24_ok(r24)) {
		if (r24 >= ROMBase && r24 < ROMBase + 0x500000u)
			last_rom = r24;
		return r24;
	}
#if NW_BOOT_LOG
	{
		static unsigned nfix;
		if (nfix < 16) {
			nfix++;
			char buf[96];
			snprintf(buf, sizeof(buf),
				 "G3: 68k r24 reset %08x last_rom=%08x",
				 (unsigned)in, (unsigned)last_rom);
			nw_boot_log(buf);
		}
	}
#endif
	if (last_rom >= ROMBase && last_rom < ROMBase + 0x500000u) {
		/* Remap the requested dest if it is a skip interior.
		 * last_rom+2 rewrote BLE 0x20cb4 / BSR 0x2065a onto
		 * the next mill PC. */
		uint32 t = last_rom + 2u;
		if (r24 >= ROMBase && r24 < ROMBase + 0x500000u)
			t = r24;
		const uint32 tab0 = ROMBase + 0xa9e0u;
		const uint32 tab1 = ROMBase + 0xaa7eu;
		unsigned i;
		for (i = 0; i < 1024u; i++) {
			if (t >= tab0 && t < tab1)
				t = tab1;
			if (t >= ROMBase + 0xa942u && t < ROMBase + 0xaf3eu)
				t = ROMBase + 0xaf3eu;
			if (t >= ROMBase + 0x2c1aau && t < ROMBase + 0x2c1c6u)
				t = ROMBase + 0x2c1c6u;
			if (t >= ROMBase + 0x40c82u && t < ROMBase + 0x40ce8u)
				t = ROMBase + 0x40ce8u;
			if (t >= ROMBase + 0x41062u && t < ROMBase + 0x41100u)
				t = ROMBase + 0x41100u;
			if (t >= ROMBase + 0x2760u && t < ROMBase + 0x3f68u)
				t = ROMBase + 0x3f68u;
			if (t >= ROMBase + 0x3f6eu && t < ROMBase + 0x3f8cu)
				t = ROMBase + 0x3f8cu;
			if (t >= ROMBase + 0x48e32u && t < ROMBase + 0x49136u)
				t = ROMBase + 0x49136u;
			if (t >= ROMBase + 0x47ac2u && t < ROMBase + 0x47b26u)
				t = ROMBase + 0x47b26u;
			if (t >= ROMBase + 0x0fe9au && t < ROMBase + 0x13000u)
				t = ROMBase + 0x13000u;
			if (t >= ROMBase + 0x5080u && t < ROMBase + 0x6c94u)
				t = ROMBase + 0x6c94u;
			if (t >= ROMBase + 0x50ad0u && t < ROMBase + 0x50ae0u)
				t = ROMBase + 0x50ae0u;
			if (t >= ROMBase + 0x50e0au && t < ROMBase + 0x51100u)
				t = ROMBase + 0x51100u;
			if (t >= ROMBase + 0x5112au && t < ROMBase + 0x51130u)
				t = ROMBase + 0x51130u;
			if (t >= ROMBase + 0x51240u && t < ROMBase + 0x512d0u)
				t = ROMBase + 0x512d0u;
			if (t >= ROMBase + 0x4b1b0u && t < ROMBase + 0x4b1c0u)
				t = ROMBase + 0x4b1c0u;
			if (t >= ROMBase + 0x4211cu && t < ROMBase + 0x42130u)
				t = ROMBase + 0x42130u;
			if (t >= ROMBase + 0x2ca40u && t < ROMBase + 0x2cab0u)
				t = ROMBase + 0x2cab0u;
			if (t >= ROMBase + 0x2c858u && t < ROMBase + 0x2ca0eu)
				t = ROMBase + 0x2ca0eu;
			if (t >= ROMBase + 0x38b18u && t < ROMBase + 0x38b5eu)
				t = ROMBase + 0x38b5eu;
			if (t >= ROMBase + 0xb6u && t < ROMBase + 0x112u)
				t = ROMBase + 0x112u;
			if (t >= ROMBase + 0x2ca18u && t < ROMBase + 0x2ca3eu)
				t = ROMBase + 0x2ca3eu;
			if (t >= ROMBase + 0x2a168u && t < ROMBase + 0x2a204u)
				t = ROMBase + 0x2a204u;
			if (t >= ROMBase + 0x2cbb2u && t < ROMBase + 0x2cbd8u)
				t = ROMBase + 0x2cbd8u;
			if (t >= ROMBase + 0x2cbe2u && t < ROMBase + 0x2cc38u)
				t = ROMBase + 0x2cc38u;
			if (t >= ROMBase + 0x2d482u && t < ROMBase + 0x2dd18u)
				t = ROMBase + 0x2dd18u;
			if (t >= ROMBase + 0x4e052u && t < ROMBase + 0x4e374u)
				t = ROMBase + 0x4e374u;
			if (t >= ROMBase + 0x4c1e2u && t < ROMBase + 0x4c3d4u)
				t = ROMBase + 0x4c3d4u;
			if (t >= ROMBase + 0x4b1d2u && t < ROMBase + 0x4b1e8u)
				t = ROMBase + 0x4b1e8u;
			if (t >= ROMBase + 0x4c3e2u && t < ROMBase + 0x4f7c0u)
				t = ROMBase + 0x4f7c0u;
			if (t >= ROMBase + 0x85902u && t < ROMBase + 0x85968u)
				t = ROMBase + 0x85968u;
			if (t >= ROMBase + 0x870f2u && t < ROMBase + 0x8e770u)
				t = ROMBase + 0x8e770u;
			if (t >= ROMBase + 0x80000u && t < ROMBase + 0x806d0u)
				t = ROMBase + 0x806d0u;
			if (t >= ROMBase + 0x7a402u && t < ROMBase + 0x7a538u)
				t = ROMBase + 0x7a538u;
			if (t >= ROMBase + 0x8281au && t < ROMBase + 0x8e770u)
				t = ROMBase + 0x8e770u;
			if (t >= ROMBase + 0x74660u && t < ROMBase + 0x7509eu)
				t = ROMBase + 0x7509eu;
			if (t >= ROMBase + 0x4ffcu && t < ROMBase + 0x5078u)
				t = ROMBase + 0x5078u;
			if (t >= ROMBase + 0x5dcd2u && t < ROMBase + 0x61080u)
				t = ROMBase + 0x61080u;
			if (t >= ROMBase + 0x1dd3au && t < ROMBase + 0x1ddb2u)
				t = ROMBase + 0x1ddb2u;
			if (t >= ROMBase + 0xf01au && t < ROMBase + 0xf168u)
				t = ROMBase + 0xf168u;
			if (t >= ROMBase + 0x2088au && t < ROMBase + 0x208f8u)
				t = ROMBase + 0x20b80u;
			if (t >= ROMBase + 0x20902u && t < ROMBase + 0x20948u)
				t = ROMBase + 0x20b80u;
			if (t >= ROMBase + 0x20952u && t < ROMBase + 0x20b80u)
				t = ROMBase + 0x20b80u;
			if (t >= ROMBase + 0x20986u && t < ROMBase + 0x20b80u)
				t = ROMBase + 0x20b80u;
			if (t >= ROMBase + 0x20bdcu && t < ROMBase + 0x20c9au)
				t = ROMBase + 0x20c9au;
			if (t >= ROMBase + 0x2014eu && t < ROMBase + 0x20160u)
				t = ROMBase + 0x20160u;
			if (t >= ROMBase + 0x20222u && t < ROMBase + 0x203dau)
				t = ROMBase + 0x203dau;
			if (t >= ROMBase + 0x1ffecu && t < ROMBase + 0x20000u)
				t = ROMBase + 0x20000u;
			if (t >= ROMBase + 0x20002u && t < ROMBase + 0x2012cu)
				t = ROMBase + 0x2012cu;
			if (t >= ROMBase + 0x2055au && t < ROMBase + 0x20574u)
				t = ROMBase + 0x20574u;
			if (t >= ROMBase + 0x2065cu && t < ROMBase + 0x208f8u)
				t = ROMBase + 0x20b80u;
			if (t >= ROMBase + 0x7577au && t < ROMBase + 0x77240u)
				t = ROMBase + 0x77240u;
			if (t >= ROMBase + 0x25fc0u && t < ROMBase + 0x26006u)
				t = ROMBase + 0x26006u;
			if (t >= ROMBase + 0x271d0u && t < ROMBase + 0x27620u)
				t = ROMBase + 0x27620u;
			if (t >= ROMBase + 0x7e308u && t < ROMBase + 0x7e35au)
				t = ROMBase + 0x7e35au;
			if (t >= ROMBase + 0x640c2u && t < ROMBase + 0x64ef6u)
				t = ROMBase + 0x64ef6u;
			if (t >= ROMBase + 0x67a84u && t < ROMBase + 0x67a90u)
				t = ROMBase + 0x67a90u;
			if (t >= ROMBase + 0x5be12u && t < ROMBase + 0x5c820u)
				t = ROMBase + 0x5c820u;
			if (t >= ROMBase + 0x5ce1eu && t < ROMBase + 0x5cfb0u)
				t = ROMBase + 0x5cfb0u;
			if (t >= ROMBase + 0x3fb2u && t < ROMBase + 0x3fd8u)
				t = ROMBase + 0x3fd8u;
			if (t >= ROMBase + 0x49e0u && t < ROMBase + 0x49e2u)
				t = ROMBase + 0x49e2u;
			if (t >= ROMBase + 0x49f0u && t < ROMBase + 0x4d4au)
				t = ROMBase + 0x4d4au;
			if (t >= ROMBase + 0x4d52u && t < ROMBase + 0x4e30u)
				t = ROMBase + 0x4e30u;
			if (t >= ROMBase + 0x49b6u && t < ROMBase + 0x49dcu)
				t = ROMBase + 0x49dcu;
			if (t >= ROMBase + 0x4e32u && t < ROMBase + 0x4e86u)
				t = ROMBase + 0x4e86u;
			if (t >= ROMBase + 0x4f52u && t < ROMBase + 0x5078u)
				t = ROMBase + 0x5078u;
			if (t >= ROMBase + 0x4e88u && t < ROMBase + 0x4f50u)
				t = ROMBase + 0x4f50u;
			if (t >= ROMBase + 0x4fad0u && t < ROMBase + 0x4ffe0u)
				t = ROMBase + 0x4ffe0u;
			if (t >= ROMBase + 0x4ffe2u && t < ROMBase + 0x50840u)
				t = ROMBase + 0x50840u;
			if (t >= ROMBase + 0x160e2u && t < ROMBase + 0x16780u)
				t = ROMBase + 0x16780u;
			if (t >= ROMBase + 0x5d352u && t < ROMBase + 0x5d4c6u)
				t = ROMBase + 0x5d4d0u;
			if (t >= ROMBase + 0x210e4u && t < ROMBase + 0x21130u)
				t = ROMBase + 0x21130u;
			if (t >= ROMBase + 0x2116eu && t < ROMBase + 0x214dau)
				t = ROMBase + 0x214dau;
			if (t >= ROMBase + 0x572c6u && t < ROMBase + 0x58000u)
				t = ROMBase + 0x58000u;
			if (t >= ROMBase + 0x570ecu && t < ROMBase + 0x57100u)
				t = ROMBase + 0x57100u;
			if (t >= ROMBase + 0x5706eu && t < ROMBase + 0x571d0u)
				t = ROMBase + 0x571d0u;
			if (t >= ROMBase + 0x58666u && t < ROMBase + 0x58670u)
				t = ROMBase + 0x58670u;
			if (t >= ROMBase + 0x46e8au && t < ROMBase + 0x46ea0u)
				t = ROMBase + 0x46ea0u;
			if (t >= ROMBase + 0x46f14u && t < ROMBase + 0x46f20u)
				t = ROMBase + 0x46f20u;
			if (t >= ROMBase + 0xa88c2u && t < ROMBase + 0xa88e2u)
				t = ROMBase + 0xa88e2u;
			if (t >= ROMBase + 0xa8900u && t < ROMBase + 0xa894au)
				t = ROMBase + 0xa894au;
			if (t >= ROMBase + 0xa8818u && t < ROMBase + 0xa882eu)
				t = ROMBase + 0xa882eu;
			if (t >= ROMBase + 0xa8848u && t < ROMBase + 0xa884cu)
				t = ROMBase + 0xa884cu;
			if (t >= ROMBase + 0x26614u && t < ROMBase + 0x26de0u)
				t = ROMBase + 0x26de0u;
			if (t >= ROMBase + 0x2bdf0u && t < ROMBase + 0x2bec0u)
				t = ROMBase + 0x2bec0u;
			if (t >= ROMBase + 0x1e910u && t < ROMBase + 0x1e920u)
				t = ROMBase + 0x1e920u;
			if (t >= ROMBase + 0x1e8b2u && t < ROMBase + 0x1e8b8u)
				t = ROMBase + 0x1e8b8u;
			if (t >= ROMBase + 0x25496u && t < ROMBase + 0x255f6u)
				t = ROMBase + 0x255f6u;
			if (t >= ROMBase + 0x28b12u && t < ROMBase + 0x28b66u)
				t = ROMBase + 0x28b66u;
			if (t >= ROMBase + 0x214f2u && t < ROMBase + 0x21500u)
				t = ROMBase + 0x21500u;
			if (t >= ROMBase + 0x21516u && t < ROMBase + 0x21518u)
				t = ROMBase + 0x21518u;
			if (t >= ROMBase + 0x2153eu && t < ROMBase + 0x21540u)
				t = ROMBase + 0x21540u;
			if (t >= ROMBase + 0x21576u && t < ROMBase + 0x2158au)
				t = ROMBase + 0x2158au;
			if (t >= ROMBase + 0x21586u && t < ROMBase + 0x2158au)
				t = ROMBase + 0x2158au;
			if (t >= ROMBase + 0x2157eu && t < ROMBase + 0x2158au)
				t = ROMBase + 0x2158au;
			if (t >= ROMBase + 0x1e9d2u && t < ROMBase + 0x1ea48u)
				t = ROMBase + 0x1ea48u;
			if (t >= ROMBase + 0x1fa1eu && t < ROMBase + 0x1fa34u)
				t = ROMBase + 0x1fa34u;
			if (t >= ROMBase + 0x252a6u && t < ROMBase + 0x252b0u)
				t = ROMBase + 0x252b0u;
			if (t >= ROMBase + 0x1ebecu && t < ROMBase + 0x1ebf0u)
				t = ROMBase + 0x1ebf0u;
			if (t >= ROMBase + 0x20f38u && t < ROMBase + 0x20ffeu)
				t = ROMBase + 0x20ffeu;
			if (t >= ROMBase + 0x1951au && t < ROMBase + 0x1951cu)
				t = ROMBase + 0x1951cu;
			if (t >= ROMBase + 0x75fcu && t < ROMBase + 0x7640u)
				t = ROMBase + 0x7640u;
			if (t >= ROMBase + 0x492au && t < ROMBase + 0x49dcu)
				t = ROMBase + 0x49dcu;
			if (t >= ROMBase + 0x40f2u && t < ROMBase + 0x4248u)
				t = ROMBase + 0x4248u;
			if (t >= ROMBase + 0x1ee22u && t < ROMBase + 0x1f7deu)
				t = ROMBase + 0x1f7deu;
			if (t >= ROMBase + 0x1f8f8u && t < ROMBase + 0x1f90cu)
				t = ROMBase + 0x1f90cu;
			if (t >= ROMBase + 0x1fe88u && t < ROMBase + 0x1fef6u)
				t = ROMBase + 0x1fef6u;
			if (t >= ROMBase + 0x24c52u && t < ROMBase + 0x24e80u)
				t = ROMBase + 0x255f6u;
			if (t >= ROMBase + 0x26c52u && t < ROMBase + 0x26de0u)
				t = ROMBase + 0x26de0u;
			if (t >= ROMBase + 0x215c4u && t < ROMBase + 0x22394u)
				t = ROMBase + 0x22394u;
			if (t >= ROMBase + 0x2449au && t < ROMBase + 0x24c50u)
				t = ROMBase + 0x255f6u;
			if (t >= ROMBase + 0x22f02u && t < ROMBase + 0x22ffeu)
				t = ROMBase + 0x22ffeu;
			if (t >= ROMBase + 0x22cd2u && t < ROMBase + 0x24284u)
				t = ROMBase + 0x24e80u;
			if (t >= ROMBase + 0x224e2u && t < ROMBase + 0x24284u)
				t = ROMBase + 0x24e80u;
			if (t >= ROMBase + 0x24290u && t < ROMBase + 0x24c50u)
				t = ROMBase + 0x255f6u;
			if (t >= ROMBase + 0x24c52u && t < ROMBase + 0x24e80u)
				t = ROMBase + 0x255f6u;
			if (t >= ROMBase + 0x24e82u && t < ROMBase + 0x25490u)
				t = ROMBase + 0x25490u;
			if (t >= ROMBase + 0x16ed0u && t < ROMBase + 0x16f8cu)
				t = ROMBase + 0x16f8cu;
			if (t >= ROMBase + 0xd050u && t < ROMBase + 0xd094u)
				t = ROMBase + 0xd094u;
			if (t >= ROMBase + 0xcbdcu && t < ROMBase + 0xcbe4u)
				t = ROMBase + 0xcbe4u;
			if (t >= ROMBase + 0xcdeau && t < ROMBase + 0xce24u)
				t = ROMBase + 0xce24u;
			if (t >= ROMBase + 0xcfc8u && t < ROMBase + 0xd04eu)
				t = ROMBase + 0xd04eu;
			if (t >= ROMBase + 0xd0d8u && t < ROMBase + 0xd430u)
				t = ROMBase + 0xd430u;
			if (t >= ROMBase + 0xc75au && t < ROMBase + 0xc86cu)
				t = ROMBase + 0xc86cu;
			if (t >= ROMBase + 0xc91cu && t < ROMBase + 0xc92au)
				t = ROMBase + 0xc92au;
			if (t >= ROMBase + 0xd4b4u && t < ROMBase + 0xd570u)
				t = ROMBase + 0xd570u;
			if (t >= ROMBase + 0xd592u && t < ROMBase + 0xd5f4u)
				t = ROMBase + 0xd5f4u;
			if (t >= ROMBase + 0xbf62u && t < ROMBase + 0xd5f4u)
				t = ROMBase + 0xd5f4u;
			if (t >= ROMBase + 0xa8962u && t < ROMBase + 0xb77b4u)
				t = ROMBase + 0xb77b4u;
			if (t >= ROMBase + 0xc0000u && t < ROMBase + 0x198154u)
				t = ROMBase + 0x198154u;
			if (t >= ROMBase + 0x19cf62u && t < ROMBase + 0x1d2000u)
				t = ROMBase + 0x1dd38u;
			if (t >= ROMBase + 0x1d1b22u && t < ROMBase + 0x1d2000u)
				t = ROMBase + 0x1d2000u;
			if (t >= ROMBase + 0x1d2000u && t < ROMBase + 0x400000u)
				t = ROMBase + 0x1dd38u;
			if (t >= ROMBase + 0x2cc3au && t < ROMBase + 0x2cc40u)
				t = ROMBase + 0x2cc40u;
			if (t >= ROMBase + 0x1cf22u && t < ROMBase + 0x1dd38u)
				t = ROMBase + 0x1dd38u;
			if (t >= ROMBase + 0x30e58u && t < ROMBase + 0x311e0u)
				t = ROMBase + 0x311e0u;
			if (t >= ROMBase + 0x304c0u && t < ROMBase + 0x305c0u)
				t = ROMBase + 0x305c0u;
			if (t >= ROMBase + 0x289dcu && t < ROMBase + 0x28a04u)
				t = ROMBase + 0x28a04u;
			if (t >= ROMBase + 0x28922u && t < ROMBase + 0x28976u)
				t = ROMBase + 0x28976u;
			if (t >= ROMBase + 0xe48u && t < ROMBase + 0xf44u)
				t = ROMBase + 0xf44u;
			if (t >= ROMBase + 0x1000u && t < ROMBase + 0x1040u)
				t = ROMBase + 0x1040u;
			if (t >= ROMBase + 0x194cu && t < ROMBase + 0x1c80u)
				t = ROMBase + 0x1c80u;
			if (t >= ROMBase + 0x1888au && t < ROMBase + 0x188dau)
				t = ROMBase + 0x188dau;
			if (t >= ROMBase + 0x188dcu && t < ROMBase + 0x18b60u)
				t = ROMBase + 0x18b60u;
			if (t >= ROMBase + 0x186feu && t < ROMBase + 0x18708u)
				t = ROMBase + 0x18708u;
			if (t >= ROMBase + 0x1a7d2u && t < ROMBase + 0x1a7f0u)
				t = ROMBase + 0x1a7f0u;
			if (t >= ROMBase + 0x1a8e0u && t < ROMBase + 0x1a902u)
				t = ROMBase + 0x1a902u;
			if (t >= ROMBase + 0x18c2au && t < ROMBase + 0x18c50u)
				t = ROMBase + 0x18c50u;
			if (t >= ROMBase + 0x1914cu && t < ROMBase + 0x1a7d0u)
				t = ROMBase + 0x1a7d0u;
			if (t >= ROMBase + 0x5cbb4u && t < ROMBase + 0x5cbb8u)
				t = ROMBase + 0x5cbb8u;
			if (t >= ROMBase + 0x129cu && t < ROMBase + 0x12c0u)
				t = ROMBase + 0x12c0u;
			if (t >= ROMBase + 0x1478u && t < ROMBase + 0x14c0u)
				t = ROMBase + 0x14c0u;
			if (t >= ROMBase + 0x1650u && t < ROMBase + 0x1670u)
				t = ROMBase + 0x1670u;
			if (t >= ROMBase + 0x5ae78u && t < ROMBase + 0x5be10u &&
			    !(t >= ROMBase + 0x5b110u && t < ROMBase + 0x5b166u))
				t = ROMBase + 0x5be10u;
			if (t >= ROMBase + 0x498e2u && t < ROMBase + 0x499d6u)
				t = ROMBase + 0x499d6u;
			if (t >= ROMBase + 0x49e64u && t < ROMBase + 0x4a0e0u)
				t = ROMBase + 0x4a0e0u;
			if (t >= ROMBase + 0x60f82u && t < ROMBase + 0x61080u)
				t = ROMBase + 0x61080u;
			if (t >= ROMBase + 0x6304au && t < ROMBase + 0x63100u)
				t = ROMBase + 0x63100u;
			if (t >= ROMBase + 0xeac8u && t < ROMBase + 0xf170u)
				t = ROMBase + 0xf170u;
			if (t >= ROMBase + 0xde12u && t < ROMBase + 0xded0u)
				t = ROMBase + 0xded0u;
			if (t >= ROMBase + 0x9fd9au && t < ROMBase + 0xa08d0u)
				t = ROMBase + 0xa08d0u;
			if (t >= ROMBase + 0x400000u && t < ROMBase + 0x500000u)
				t = ROMBase + 0x2au;
			if (t >= ROMBase + 0x350000u && t < ROMBase + 0x400000u)
				t = ROMBase + 0x2au;
			if (t >= ROMBase + 0x424au && t < ROMBase + 0x48b0u)
				t = ROMBase + 0x48b0u;
			if (t >= ROMBase + 0x7b46u && t < ROMBase + 0x7bbcu)
				t = ROMBase + 0x7bbcu;
			if (t >= ROMBase + 0x7bd2u && t < ROMBase + 0x7c06u)
				t = ROMBase + 0x7c06u;
			if (t >= ROMBase + 0x27ab0u && t < ROMBase + 0x27ad8u)
				t = ROMBase + 0x27ad8u;
			if (t >= ROMBase + 0x14318u && t < ROMBase + 0x14620u)
				t = ROMBase + 0x14620u;
			if (t >= ROMBase + 0x660d4u && t < ROMBase + 0x66158u)
				t = ROMBase + 0x66158u;
			if (t >= ROMBase + 0x15690u && t < ROMBase + 0x157a0u)
				t = ROMBase + 0x157a0u;
			if (t >= ROMBase + 0x15c20u && t < ROMBase + 0x15cb0u)
				t = ROMBase + 0x15cb0u;
			if (t >= ROMBase + 0x14d20u && t < ROMBase + 0x14d50u)
				t = ROMBase + 0x14d50u;
			if (t >= ROMBase + 0x1735cu && t < ROMBase + 0x173dau)
				t = ROMBase + 0x173dau;
			if (t >= ROMBase + 0x17d66u && t < ROMBase + 0x17d90u)
				t = ROMBase + 0x17d90u;
			if (t >= ROMBase + 0x8040u && t < ROMBase + 0x8052u)
				t = ROMBase + 0x8052u;
			if (t >= ROMBase + 0x77d0u && t < ROMBase + 0x78d2u)
				t = ROMBase + 0x78d2u;
			if (t >= ROMBase + 0x7516u && t < ROMBase + 0x76b0u)
				t = ROMBase + 0x76b0u;
			if (t >= ROMBase + 0xa80a6u && t < ROMBase + 0xa8130u)
				t = ROMBase + 0xa8130u;
			if (t >= ROMBase + 0xf7c2u && t < ROMBase + 0xf868u)
				t = ROMBase + 0xf868u;
			if (t >= ROMBase + 0xf94eu && t < ROMBase + 0xf960u)
				t = ROMBase + 0xf960u;
			if (t >= ROMBase + 0x8192u && t < ROMBase + 0x81f0u)
				t = ROMBase + 0x81f0u;
			if (t >= ROMBase + 0x81e8u && t < ROMBase + 0x81f0u)
				t = ROMBase + 0x81f0u;
			if (t >= ROMBase + 0x81f2u && t < ROMBase + 0x83a2u)
				t = ROMBase + 0x83a2u;
			if (t >= ROMBase + 0x8584u && t < ROMBase + 0x8592u)
				t = ROMBase + 0x8592u;
			if (t >= ROMBase + 0x27b00u && t < ROMBase + 0x27ca0u)
				t = ROMBase + 0x27ca0u;
			if (t >= ROMBase + 0x8eb0u && t < ROMBase + 0x932au)
				t = ROMBase + 0x932au;
			if (t >= ROMBase + 0x2596eu && t < ROMBase + 0x25974u)
				t = ROMBase + 0x25974u;
			if (t >= ROMBase + 0x25f74u && t < ROMBase + 0x25f7au)
				t = ROMBase + 0x25f7au;
			if (t >= ROMBase + 0x28640u && t < ROMBase + 0x28786u)
				t = ROMBase + 0x28786u;
			if (t >= ROMBase + 0x58000u && t < ROMBase + 0x58090u)
				t = ROMBase + 0x58090u;
			if (t >= ROMBase + 0x27a72u && t < ROMBase + 0x27aaau)
				t = ROMBase + 0x27aaau;
			if (t >= ROMBase + 0x9900u && t < ROMBase + 0x99a8u)
				t = ROMBase + 0x99a8u;
			if (t >= ROMBase + 0x94c8u && t < ROMBase + 0x96a6u)
				t = ROMBase + 0x96a6u;
			if (t >= ROMBase + 0x91942u && t < ROMBase + 0x91980u)
				t = ROMBase + 0x91980u;
			if (t >= ROMBase + 0x95ee2u && t < ROMBase + 0x96788u)
				t = ROMBase + 0x96788u;
			if (t >= ROMBase + 0x2838cu && t < ROMBase + 0x283b0u)
				t = ROMBase + 0x283b0u;
			if (t >= ROMBase + 0x815b0u && t < ROMBase + 0x816aeu)
				t = ROMBase + 0x816aeu;
			if (t >= ROMBase + 0x81a3cu && t < ROMBase + 0x81b78u)
				t = ROMBase + 0x81b78u;
			if (t >= ROMBase + 0x81d02u && t < ROMBase + 0x81d80u)
				t = ROMBase + 0x81d80u;
			if (t >= ROMBase + 0xafb0u && t < ROMBase + 0xdfeau)
				t = ROMBase + 0xdfeau;
			if (t >= ROMBase + 0xdadcu && t < ROMBase + 0xdb56u)
				t = ROMBase + 0xdb56u;
			if (t >= ROMBase + 0x40042u && t < ROMBase + 0x40106u)
				t = ROMBase + 0x40106u;
			if (t >= ROMBase + 0x25002u && t < ROMBase + 0x251bcu)
				t = ROMBase + 0x251beu;
			if (t >= ROMBase + 0x251ccu && t < ROMBase + 0x251d0u)
				t = ROMBase + 0x251d0u;
			if (t >= ROMBase + 0x44fc2u && t < ROMBase + 0x44ff4u)
				t = ROMBase + 0x44ff4u;
			if (t >= ROMBase + 0x9502u && t < ROMBase + 0x9800u)
				t = ROMBase + 0x9800u;
			if (t >= ROMBase + 0x8fc6u && t < ROMBase + 0x8fc8u)
				t = ROMBase + 0x8fc8u;
			if (t >= ROMBase + 0x9010u && t < ROMBase + 0x9018u)
				t = ROMBase + 0x9018u;
			if (t >= ROMBase + 0x9038u && t < ROMBase + 0x9044u)
				t = ROMBase + 0x9044u;
			if (t >= ROMBase + 0x905eu && t < ROMBase + 0x9066u)
				t = ROMBase + 0x9066u;
			if (t >= ROMBase + 0x9088u && t < ROMBase + 0x908au)
				t = ROMBase + 0x908au;
			if (t >= ROMBase + 0x909cu && t < ROMBase + 0x90a8u)
				t = ROMBase + 0x90a8u;
			if (t >= ROMBase + 0xa2f4u && t < ROMBase + 0xa340u)
				t = ROMBase + 0xa340u;
			if (t >= ROMBase + 0x9888u && t < ROMBase + 0x989cu)
				t = ROMBase + 0x989cu;
			if (t >= ROMBase + 0x1b944u && t < ROMBase + 0x1b94eu)
				t = ROMBase + 0x1b94eu;
			if (t >= ROMBase + 0x16daau && t < ROMBase + 0x16daeu)
				t = ROMBase + 0x16daeu;
			if (t >= ROMBase + 0x1ccc6u && t < ROMBase + 0x1ccdcu)
				t = ROMBase + 0x1ccdcu;
			if (t >= ROMBase + 0xe074u && t < ROMBase + 0xe080u)
				t = ROMBase + 0xe080u;
			if (t >= ROMBase + 0xe086u && t < ROMBase + 0xe270u)
				t = ROMBase + 0xe270u;
			if (t >= ROMBase + 0xe61au && t < ROMBase + 0xe99cu)
				t = ROMBase + 0xe99cu;
			if (t >= ROMBase + 0xe3ceu && t < ROMBase + 0xe3d8u)
				t = ROMBase + 0xe3d8u;
			if (t >= ROMBase + 0x8b7eu && t < ROMBase + 0x8baeu)
				t = ROMBase + 0x8baeu;
			if (t >= ROMBase + 0xa028u && t < ROMBase + 0xa0c6u)
				t = ROMBase + 0xa0c6u;
			if (t >= ROMBase + 0xa00cu && t < ROMBase + 0xa01eu)
				t = ROMBase + 0xa01eu;
			if (t >= ROMBase + 0x9fdd8u && t < ROMBase + 0x9fde0u)
				t = ROMBase + 0x9fde0u;
			if (t >= ROMBase + 0x13400u && t < ROMBase + 0x13620u)
				t = ROMBase + 0x13620u;
			if (t >= ROMBase + 0x498eu && t < ROMBase + 0x4990u)
				t = ROMBase + 0x4990u;
			if (t >= ROMBase + 0x15690u && t < ROMBase + 0x156d0u)
				t = ROMBase + 0x156d0u;
			if (t >= ROMBase + 0xf1acu && t < ROMBase + 0xf1b0u)
				t = ROMBase + 0xf1b0u;
			if (t >= ROMBase + 0xf18eu && t < ROMBase + 0xf190u)
				t = ROMBase + 0xf190u;
			if (t >= ROMBase + 0x8dd2u && t < ROMBase + 0x8dd6u)
				t = ROMBase + 0x8dd6u;
			if (t >= ROMBase + 0x2574cu && t < ROMBase + 0x25754u)
				t = ROMBase + 0x25754u;
			if (t >= ROMBase + 0x256b4u && t < ROMBase + 0x256ccu)
				t = ROMBase + 0x256ccu;
			if (t >= ROMBase + 0x257dcu && t < ROMBase + 0x257fau)
				t = ROMBase + 0x257fau;
			if (t >= ROMBase + 0x8a50u && t < ROMBase + 0x8a52u)
				t = ROMBase + 0x8a52u;
			if (t >= ROMBase + 0x150a4u && t < ROMBase + 0x150b0u)
				t = ROMBase + 0x150b0u;
			if (t >= ROMBase + 0x137b2u && t < ROMBase + 0x138b0u)
				t = ROMBase + 0x138b0u;
			if (t >= ROMBase + 0x13f00u && t < ROMBase + 0x13f76u)
				t = ROMBase + 0x13f76u;
			if (t >= ROMBase + 0x13f80u && t < ROMBase + 0x1425au)
				t = ROMBase + 0x1425au;
			if (t >= ROMBase + 0x14bf0u && t < ROMBase + 0x14c60u)
				t = ROMBase + 0x14c60u;
			if (t >= ROMBase + 0x5c872u && t < ROMBase + 0x5c87eu)
				t = ROMBase + 0x5c87eu;

			if (t >= ROMBase + 0x5cc90u && t < ROMBase + 0x5cce0u)
				t = ROMBase + 0x5cce0u;
			if (t >= ROMBase + 0xa37c0u && t < ROMBase + 0xa3a34u)
				t = ROMBase + 0xa3a34u;
			if (t >= ROMBase + 0xa496u && t < ROMBase + 0xa4a4u)
				t = ROMBase + 0xa4a4u;
			if (t >= ROMBase + 0x20f3eu && t < ROMBase + 0x20ffeu)
				t = ROMBase + 0x20ffeu;
			if (t >= ROMBase + 0x1ea50u && t < ROMBase + 0x1ebe0u)
				t = ROMBase + 0x1ebe0u;
			if (t >= ROMBase + 0x210e4u && t < ROMBase + 0x21100u)
				t = ROMBase + 0x21100u;
			if (t >= ROMBase + 0x1e69eu && t < ROMBase + 0x1e6fcu)
				t = ROMBase + 0x1e6fcu;
			if (t >= ROMBase + 0x1e76au && t < ROMBase + 0x1e794u)
				t = ROMBase + 0x1e794u;
			if (t >= ROMBase + 0x1e796u && t < ROMBase + 0x1e874u)
				t = ROMBase + 0x1e874u;
			if (t >= ROMBase + 0x1e8beu && t < ROMBase + 0x1e912u)
				t = ROMBase + 0x1e912u;
			if (t >= ROMBase + 0x20c82u && t < ROMBase + 0x20c9au)
				t = ROMBase + 0x20c9au;
			if (t >= ROMBase + 0x20cc6u && t < ROMBase + 0x20ffeu)
				t = ROMBase + 0x20ffeu;
			if (t >= ROMBase + 0xf310u && t < ROMBase + 0xf600u)
				t = ROMBase + 0xf600u;
			if (t >= ROMBase + 0xf602u && t < ROMBase + 0xf620u)
				t = ROMBase + 0xf620u;
			if (t >= ROMBase + 0xfc10u && t < ROMBase + 0xfc34u)
				t = ROMBase + 0xfc34u;
			if (t >= ROMBase + 0x4df4u && t < ROMBase + 0x4e16u)
				t = ROMBase + 0x4e16u;
			if (t >= ROMBase + 0xb7802u && t < ROMBase + 0xb781au)
				t = ROMBase + 0xb781au;
			if (t >= ROMBase + 0xb77b6u && t < ROMBase + 0xc0000u)
				t = ROMBase + 0x198154u;
			if (t >= ROMBase + 0x198156u && t < ROMBase + 0x19b806u)
				t = ROMBase + 0x1da00u;
			if (t >= ROMBase + 0x9c82u && t < ROMBase + 0xa008u)
				t = ROMBase + 0xa008u;
			if (t >= ROMBase + 0x9bceu && t < ROMBase + 0x9bd4u)
				t = ROMBase + 0x9bd4u;
			if (t >= ROMBase + 0x9c56u && t < ROMBase + 0x9c80u)
				t = ROMBase + 0x9c80u;
			if (t >= ROMBase + 0x19b808u && t < ROMBase + 0x19cf62u)
				t = ROMBase + 0x19cf62u;
			if (t >= ROMBase + 0x2b30au && t < ROMBase + 0x2b328u)
				t = ROMBase + 0x2b328u;
			if (t >= ROMBase + 0x2b32eu && t < ROMBase + 0x2b418u)
				t = ROMBase + 0x2b376u;
			if (t >= ROMBase + 0xa590u && t < ROMBase + 0xa928u)
				t = ROMBase + 0xa928u;
			if (t >= ROMBase + 0xaf62u && t < ROMBase + 0xafacu)
				t = ROMBase + 0xafacu;
			if (t >= ROMBase + 0x511c0u && t < ROMBase + 0x51220u)
				t = ROMBase + 0x51220u;
			if (t >= ROMBase + 0x70a6u && t < ROMBase + 0x7510u)
				t = ROMBase + 0x7510u;
			if (t >= ROMBase + 0x2131au && t < ROMBase + 0x214dau)
				t = ROMBase + 0x214dau;
			if (t >= ROMBase + 0x7686u && t < ROMBase + 0x76b0u)
				t = ROMBase + 0x76b0u;
			if (t >= ROMBase + 0x7aecu && t < ROMBase + 0x7af8u)
				t = ROMBase + 0x7af8u;
			if (t >= ROMBase + 0x432c2u && t < ROMBase + 0x4381eu)
				t = ROMBase + 0x4381eu;
			if (t >= ROMBase + 0x16d32u && t < ROMBase + 0x16d78u)
				t = ROMBase + 0x16d78u;
			if (t >= ROMBase + 0x2c2c0u && t < ROMBase + 0x2c2d2u)
				t = ROMBase + 0x2c2d4u;
			if (g3_r24_ok(t) && vm_read_memory_2(t) != 0)
				return t;
			t += 2u;
			if (t >= ROMBase + 0x500000u)
				break;
		}
		return last_rom;
	}
	return ROMBase + 0x2au;
}
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
	dec_ = 0x7fffffffu;
	dec_pending_ = false;
	for (int i = 0; i < 4; i++)
		sprg_[i] = 0;
	regs().reserve_valid = 0;
	regs().reserve_addr = 0;
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
	dec_ = 0x7fffffffu;
	dec_pending_ = false;
	for (int i = 0; i < 4; i++)
		sprg_[i] = 0;

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
}

void powerpc_cpu::take_data_dsi(uint32 ea, bool is_store)
{
	ppc32_hotints_dsi dsi;
	dsi.take_data_dsi(ppc32_guest_mmu(), pc(), ea, is_store);
	srr0_ = dsi.srr0;
	srr1_ = dsi.srr1;
	dar_ = dsi.dar;
	dsisr_ = dsi.dsisr;
	pc() = dsi.vector;
#ifdef SHEEPSHAVER
	if (ppc32_guest_mmu_enabled()) {
		extern uint32 ROMBase;
		/*
		 * Hardware 0x300 is the IVT DSI slot. NK's copy is often
		 * still zeros; plant a trampoline to HotInts DataStorageInt
		 * so the guest does not execute opcode 0 at 0x300.
		 */
		nw_guest_plant_dsi_vector();
		sprg_[1] = gpr(1);
		sprg_[2] = lr();
		if (sprg_[0] == 0) {
			const uint32 htaborg =
				ppc32_guest_mmu().sdr1() & 0xffff0000u;
			if (htaborg >= 0x2000)
				sprg_[0] = htaborg - 0x2000u;
		}
		const uint32 handler = ROMBase + NW_NK_DATA_STORAGE_INT;
		/* Mill 68k resume and r1/ea retry swallow the first data DSI
		 * (pc stays off 0x300, HotInts never runs). After G2 they are
		 * G3 and may run. */
		if (nw_guest_first_data_dsi_seen()) {
		if (ea >= 0xfffff000u || gpr(1) < 0x1000u) {
			extern uint32 RAMBase, RAMSize;
			const uint32 stk = RAMBase + RAMSize - 0x10000u;
			gpr(1) = stk;
#if NW_BOOT_LOG
			static int stkfix;
			if (!stkfix) {
				stkfix = 1;
				char buf[80];
				snprintf(buf, sizeof(buf),
					 "G3: DSI r1/ea fix ea=%08x r1=%08x",
					 (unsigned)ea, (unsigned)stk);
				nw_boot_log(buf);
			}
#endif
			pc() = srr0_;
			ppc32_guest_mmu().set_msr(srr1_);
			return;
		}
		{
			extern uint32 RAMBase, RAMSize, ROMBase;
			const int guest_ea =
				(ea < 0x20000u) ||
				(ea >= RAMBase && ea < RAMBase + RAMSize) ||
				(ea >= ROMBase && ea < ROMBase + 0x500000u) ||
				(ea >= 0x68000000u && ea < 0x69000000u);
			if (!guest_ea) {
				uint32 r24 = g3_rom0(gpr(24));
				/* Table stub DSI (0x3d fill, PPC opcodes as
				 * EA): skip the 68k op instead of retrying. */
				if (g3_r24_ok(r24) && r24 >= ROMBase + 0x100u)
					r24 += 2;
				else
					r24 = g3_fix_r24(gpr(24));
				gpr(24) = r24;
				gpr(27) = 0xffffffffu;
				gpr(29) = ROMBase + 0x380000u;
				pc() = ROMBase + 0x366084u;
				ppc32_guest_mmu().set_msr(srr1_);
#if NW_BOOT_LOG
				static unsigned nbad;
				if (nbad < 8) {
					nbad++;
					char buf[96];
					snprintf(buf, sizeof(buf),
						 "G3: DSI 68k resume DAR=%08x SRR0=%08x r24=%08x",
						 (unsigned)ea, (unsigned)srr0_,
						 (unsigned)r24);
					nw_boot_log(buf);
				}
#endif
				return;
			}
		}
		}
		/*
		 * Identity RAM/ROM BATs are for HotInts MemRetry after this
		 * DSI has already missed. Planting them before the miss
		 * (or retrying the DAR once they cover it) swallows G2:
		 * HotInts never runs and there is no xlatehow=miss.
		 */
		nw_guest_note_first_data_dsi();
		nw_guest_map_ram_rom_identity();
		if (vm_read_memory_4(handler) == NW_NK_DATA_STORAGE_INT_OP)
			pc() = handler;
#if NW_BOOT_LOG
		{
			ppc32_mmu &mmu = ppc32_guest_mmu();
			nw_guest_seed_rom_htab(mmu.sdr1());
			const uint32 saved_msr = mmu.msr();
			mmu.set_msr(saved_msr | ppc32_mmu::MSR_DR);
			const ppc32_xlate_result hit =
				dsi.lwz_faulting_insn(mmu);
			mmu.set_msr(saved_msr);
			const int vec_ok = vm_read_memory_4(NW_DSI_VECTOR_EA) != 0;
			nw_log_first_dsi(dsi.srr0, dsi.dar,
					 (hit.ok && vec_ok) ? 1 : 0);
			static unsigned n_dsi;
			n_dsi++;
			if (n_dsi <= 8) {
				char buf[192];
				snprintf(buf, sizeof(buf),
					 "G3: DSI n=%u SRR0=%08x DAR=%08x to=%08x SPRG0=%08x op=%08x lr=%08x r1=%08x r10=%08x r22=%08x r26=%08x",
					 n_dsi, (unsigned)dsi.srr0, (unsigned)dsi.dar,
					 (unsigned)pc(), (unsigned)sprg_[0],
					 (unsigned)vm_read_memory_4(dsi.srr0),
					 (unsigned)lr(), (unsigned)gpr(1),
					 (unsigned)gpr(10), (unsigned)gpr(22),
					 (unsigned)gpr(26));
				nw_boot_log(buf);
				if (n_dsi == 1) {
					char ibuf[160];
					snprintf(ibuf, sizeof(ibuf),
						 "G3: DSIcode %08x %08x %08x %08x %08x %08x %08x %08x",
						 (unsigned)vm_read_memory_4(dsi.srr0 - 16),
						 (unsigned)vm_read_memory_4(dsi.srr0 - 12),
						 (unsigned)vm_read_memory_4(dsi.srr0 - 8),
						 (unsigned)vm_read_memory_4(dsi.srr0 - 4),
						 (unsigned)vm_read_memory_4(dsi.srr0),
						 (unsigned)vm_read_memory_4(dsi.srr0 + 4),
						 (unsigned)vm_read_memory_4(dsi.srr0 + 8),
						 (unsigned)vm_read_memory_4(dsi.srr0 + 12));
					nw_boot_log(ibuf);
				}
			}
		}
#endif
	}
#endif
}

void powerpc_cpu::take_isi()
{
	srr0_ = pc();
	srr1_ = ppc32_guest_mmu().msr();
#ifdef SHEEPSHAVER
	nw_guest_map_kernel_data();
	{
		extern uint32 ROMBase;
		if (srr0_ >= ROMBase + 0x360000u &&
		    srr0_ < ROMBase + 0x500000u)
			nw_guest_map_ram_rom_identity();
		ppc32_mmu &mmu = ppc32_guest_mmu();
		const uint32 saved = mmu.msr();
		mmu.set_msr(srr1_);
		const ppc32_xlate_result hit =
			mmu.translate(srr0_, PPC32_XLATE_IR, 4);
		if (hit.ok) {
			pc() = srr0_;
#if NW_BOOT_LOG
			static unsigned n_isi_retry;
			n_isi_retry++;
			if (n_isi_retry <= 8) {
				char buf[96];
				snprintf(buf, sizeof(buf),
					 "G3: ISI retry n=%u SRR0=%08x pa=%08x",
					 n_isi_retry, (unsigned)srr0_,
					 (unsigned)hit.pa);
				nw_boot_log(buf);
			}
#endif
			return;
		}
		if (nw_guest_first_data_dsi_seen()) {
		extern uint32 ROMBase, RAMBase, RAMSize;
		uint32 r24 = g3_fix_r24(gpr(24));
		pc() = ROMBase + 0x366084u;
		gpr(24) = r24;
		gpr(27) = 0xffffffffu;
		gpr(29) = ROMBase + 0x380000u;
#if NW_BOOT_LOG
		static unsigned n_isi_68k;
		n_isi_68k++;
		if (n_isi_68k <= 8) {
			char buf[96];
			snprintf(buf, sizeof(buf),
				 "G3: ISI 68k resume SRR0=%08x r24=%08x",
				 (unsigned)srr0_, (unsigned)r24);
			nw_boot_log(buf);
		}
#endif
		return;
		}
		mmu.set_msr(saved);
	}
#endif
	ppc32_guest_mmu().set_msr(srr1_ & ~ppc32_mmu::MSR_EXC_CLEAR);
	sprg_[1] = gpr(1);
	sprg_[2] = lr();
	pc() = hotints_vector(0x400);
#ifdef SHEEPSHAVER
#if NW_BOOT_LOG
	{
		static unsigned n;
		if (n < 8) {
			n++;
			char buf[96];
			snprintf(buf, sizeof(buf),
				 "G3: ISI n=%u SRR0=%08x to=%08x msr=%08x",
				 n, (unsigned)srr0_, (unsigned)pc(),
				 (unsigned)srr1_);
			nw_boot_log(buf);
		}
	}
#endif
#endif
}

uint32 powerpc_cpu::hotints_vector(uint32 vec) const
{
	uint32 handler = vec;
	if (sprg_[3]) {
		const uint32 from_tbl =
			vm_read_memory_4(sprg_[3] + (vec >> 8) * 4u);
		if (from_tbl)
			handler = from_tbl;
	}
	return handler;
}

void powerpc_cpu::take_sc()
{
	ppc32_mmu &mmu = ppc32_guest_mmu();
#ifdef SHEEPSHAVER
	/* NK debug/panic path: sc r0=0x2e after a NULL callback. */
	if (gpr(0) == 0x2eu) {
#if NW_BOOT_LOG
		static unsigned n2e;
		if (n2e < 4) {
			n2e++;
			char buf[80];
			snprintf(buf, sizeof(buf),
				 "G3: sc 0x2e nop pc=%08x", (unsigned)pc());
			nw_boot_log(buf);
		}
#endif
		increment_pc(4);
		return;
	}
#endif
	srr0_ = pc() + 4;
	srr1_ = mmu.msr();
	mmu.set_msr(srr1_ & ~ppc32_mmu::MSR_EXC_CLEAR);
	sprg_[1] = gpr(1);
	sprg_[2] = lr();
	pc() = hotints_vector(0xc00);
#ifdef SHEEPSHAVER
#if NW_BOOT_LOG
	{
		static unsigned n;
		if (n < 8) {
			n++;
			char buf[112];
			snprintf(buf, sizeof(buf),
				 "G3: sc n=%u r0=%08x SPRG3=%08x to=%08x",
				 n, (unsigned)gpr(0), (unsigned)sprg_[3],
				 (unsigned)pc());
			nw_boot_log(buf);
		}
	}
#endif
#endif
}

void powerpc_cpu::take_dec()
{
	ppc32_mmu &mmu = ppc32_guest_mmu();
	srr0_ = pc();
	srr1_ = mmu.msr();
	mmu.set_msr(srr1_ & ~ppc32_mmu::MSR_EXC_CLEAR);
	sprg_[1] = gpr(1);
	sprg_[2] = lr();
	pc() = hotints_vector(0x900);
	dec_pending_ = false;
#ifdef SHEEPSHAVER
#if NW_BOOT_LOG
	{
		static int n;
		if (!n) {
			n = 1;
			char buf[80];
			snprintf(buf, sizeof(buf),
				 "G3: DEC to=%08x SPRG3=%08x srr0=%08x srr1=%08x dec=%08x",
				 (unsigned)pc(), (unsigned)sprg_[3],
				 (unsigned)srr0_, (unsigned)srr1_,
				 (unsigned)dec_);
			nw_boot_log(buf);
		}
	}
#endif
#endif
}

bool powerpc_cpu::guest_fetch(uint32 *opcode)
{
	if (!ppc32_guest_mmu_enabled()) {
		*opcode = vm_read_memory_4(pc());
		return true;
	}
	ppc32_xlate_result r = ppc32_guest_mmu().translate(pc(), PPC32_XLATE_IR, 4);
	if (!r.ok) {
		take_isi();
		return false;
	}
#ifdef SHEEPSHAVER
	if (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_IR) {
		extern uint32 ROMBase, RAMBase, RAMSize;
		const uint32 pa = r.pa;
		if (ROMBase &&
		    !((pa >= RAMBase && pa < RAMBase + RAMSize) ||
		      (pa >= ROMBase && pa < ROMBase + 0x500000u) ||
		      pa < 0x20000u ||
		      (pa >= 0x68fe0000u && pa < 0x69000000u))) {
			take_isi();
			return false;
		}
	}
#endif
	*opcode = vm_read_memory_4(r.pa);
#ifdef SHEEPSHAVER
	/*
	 * After a data DSI the CPU fetches 0x300 with IR off (identity PA).
	 * NK's VecTbl copy is often still zeros there; plant the DSI slot
	 * so HotInts runs instead of opcode 0.
	 */
	if ((pc() & ~0xffu) == (uint32)NW_DSI_VECTOR_EA && *opcode == 0) {
		nw_guest_plant_dsi_vector();
		*opcode = vm_read_memory_4(r.pa);
	}
#endif
	return true;
}

bool powerpc_cpu::guest_data_xlate(uint32 ea, unsigned width, bool is_store, uint32 *pa)
{
	if (!ppc32_guest_mmu_enabled()) {
		*pa = ea;
		return true;
	}
	ppc32_xlate_result r = ppc32_guest_mmu().translate(ea, PPC32_XLATE_DR, width);
#ifdef SHEEPSHAVER
	if (ppc32_guest_mmu().msr() & ppc32_mmu::MSR_DR) {
		uint32 du = 0, dl = 0;
		ppc32_guest_mmu().get_dbat(0, &du, &dl);
		nw_log_dr_xlate(pc(), ea, r.ok ? 1 : 0, r.pa, du, dl);
	}
#endif
	if (r.ok) {
		*pa = r.pa;
		return true;
	}
	take_data_dsi(ea, is_store);
	return false;
}

bool powerpc_cpu::mfspr_oea(uint32 spr, uint32 *value) const
{
	ppc32_mmu &mmu = ppc32_guest_mmu();
	switch (spr) {
	case powerpc_registers::SPR_DSISR:	*value = dsisr_; return true;
	case powerpc_registers::SPR_DAR:	*value = dar_; return true;
	case powerpc_registers::SPR_DEC:	*value = dec_; return true;
	case powerpc_registers::SPR_SDR1:	*value = mmu.sdr1(); return true;
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
	case powerpc_registers::SPR_DEC:
		dec_ = value;
		if ((value & 0x80000000u) == 0)
			dec_pending_ = false;
		return true;
	case powerpc_registers::SPR_SDR1:
#ifdef SHEEPSHAVER
		nw_note_mtsdr1();
#endif
		mmu.set_sdr1(value);
		mmu.tlbia();
#ifdef SHEEPSHAVER
		nw_guest_seed_rom_htab(value);
#endif
		return true;
	case powerpc_registers::SPR_SRR0:	srr0_ = value; return true;
	case powerpc_registers::SPR_SRR1:	srr1_ = value; return true;
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
#ifdef SHEEPSHAVER
#if NW_BOOT_LOG
		{
			static unsigned n;
			if (n < 8) {
				n++;
				char buf[96];
				snprintf(buf, sizeof(buf),
					 "G2: mtspr IBAT%u %08x/%08x delayram=%d",
					 i, (unsigned)u, (unsigned)l,
					 (int)mmu.ram_bats_delayed());
				nw_boot_log(buf);
			}
		}
#endif
#endif
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
#ifdef SHEEPSHAVER
#if NW_BOOT_LOG
		{
			static unsigned n;
			if (n < 8) {
				n++;
				char buf[96];
				snprintf(buf, sizeof(buf),
					 "G2: mtspr DBAT%u %08x/%08x delayram=%d",
					 i, (unsigned)u, (unsigned)l,
					 (int)mmu.ram_bats_delayed());
				nw_boot_log(buf);
			}
		}
#endif
#endif
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
#ifdef SHEEPSHAVER
		if (ppc32_guest_mmu_enabled()) {
			static unsigned g3_post;
			static uint32 g3_ccr;
			{
				static unsigned dec_div;
				if (++dec_div >= 256u) {
					dec_div = 0;
					const uint32 old_dec = dec_;
					dec_ -= 1;
					if ((old_dec & 0x80000000u) == 0 &&
					    (dec_ & 0x80000000u))
						dec_pending_ = true;
				}
			}
			/* Per-insn DEC underflows the instant EE is set
			 * (reset DEC is 0). Real DecrementerIntSys rfi's
			 * after mtdec ClockRateHz; 0 rate storms. Wait
			 * until IR is on so NK has finished probe. */
			{
				const uint32 msr_now = ppc32_guest_mmu().msr();
#if NW_BOOT_LOG
				if ((msr_now & ppc32_mmu::MSR_IR) &&
				    (msr_now & ppc32_mmu::MSR_DR)) {
					static int ir_on;
					static unsigned ir_trace;
					if (!ir_on) {
						ir_on = 1;
						uint32 du = 0, dl = 0;
						ppc32_guest_mmu().get_dbat(0, &du, &dl);
						char buf[128];
						snprintf(buf, sizeof(buf),
							 "G3: IR+DR on pc=%08x msr=%08x delayram=%d dbat0=%08x/%08x mill=%d",
							 (unsigned)pc(),
							 (unsigned)msr_now,
							 (int)ppc32_guest_mmu().ram_bats_delayed(),
							 (unsigned)du, (unsigned)dl,
							 nw_guest_first_data_dsi_seen());
						nw_boot_log(buf);
						/* Do not identity-map RAM here.
						 * A 128 MiB DBAT at RAMBase
						 * (10000fff/10000002) covers
						 * KDP-1048 (17efdbe8) and the
						 * first data DSI never fires. */
					}
					if (ir_trace < 40) {
						ir_trace++;
						char buf[96];
						snprintf(buf, sizeof(buf),
							 "G3: IRpc n=%u pc=%08x msr=%08x op=%08x",
							 ir_trace, (unsigned)pc(),
							 (unsigned)msr_now,
							 (unsigned)vm_read_memory_4(pc()));
						nw_boot_log(buf);
					}
				}
#endif
				if (dec_pending_ &&
				    (msr_now & (ppc32_mmu::MSR_EE |
						ppc32_mmu::MSR_IR)) ==
				    (ppc32_mmu::MSR_EE | ppc32_mmu::MSR_IR)) {
					/* Boot spent so long with DEC=0 that
					 * the first IR+EE is an underflow,
					 * not a programmed tick. Swallow it. */
					static int dec_boot;
					if (!dec_boot) {
						dec_boot = 1;
						dec_pending_ = false;
						dec_ = 0x7fffffffu;
#if NW_BOOT_LOG
						char buf[80];
						snprintf(buf, sizeof(buf),
							 "G3: DEC swallow boot underflow pc=%08x",
							 (unsigned)pc());
						nw_boot_log(buf);
#endif
					} else {
						take_dec();
						if (dec_ < 0x1000u)
							dec_ = 0x00100000u;
						continue;
					}
				}
			}
			nw_log_pc(pc(), ppc32_guest_mmu().msr());
			extern uint32 ROMBase;
			extern uint32 RAMBase;
			extern uint32 RAMSize;
			if (g3_post && g3_post < 24) {
				g3_post++;
#if NW_BOOT_LOG
				char buf[96];
				snprintf(buf, sizeof(buf),
					 "G3: post n=%u pc=%08x op=%08x r22=%08x lr=%08x",
					 g3_post, (unsigned)pc(),
					 (unsigned)vm_read_memory_4(pc()),
					 (unsigned)gpr(22), (unsigned)lr());
				nw_boot_log(buf);
#endif
			}
			if (pc() == ROMBase + 0x3259c0u)
				nw_guest_plant_nk_irq(sprg_[0] ? sprg_[0]
					: (ppc32_guest_mmu().sdr1() & 0xffff0000u) - 0x2000u);
			if (pc() == ROMBase + 0x311ff4u) {
				/* stwbrx-zero HTAB: addic. r22,-4; stwbrx r23=0,r8,r22; bgt.
				 * Interpreter makes 1MiB of stores glacial. */
				const uint32 base = gpr(8);
				const uint32 n = gpr(22);
#if NW_BOOT_LOG
				{
					static int spin;
					if (!spin) {
						spin = 1;
						char buf[96];
						snprintf(buf, sizeof(buf),
							 "G3: HTAB zero r8=%08x r22=%08x r23=%08x",
							 (unsigned)base, (unsigned)n,
							 (unsigned)gpr(23));
						nw_boot_log(buf);
					}
				}
#endif
				if (n >= 0x1000u && n <= 0x200000u &&
				    base >= RAMBase &&
				    (uint64_t)base + n <=
					    (uint64_t)RAMBase + RAMSize) {
					for (uint32 o = 0; o < n; o += 4)
						vm_write_memory_4(base + o, 0);
					gpr(22) = 0;
					nw_guest_seed_rom_htab(
						ppc32_guest_mmu().sdr1());
					pc() = ROMBase + 0x312000u;
					g3_post = 1;
					continue;
				}
				gpr(22) = 4;
			}
			if (pc() == ROMBase + 0x3155c0u) {
				/* dcbf walk over RAM; emulator caches are not real. */
#if NW_BOOT_LOG
				static int dcb;
				if (!dcb) {
					dcb = 1;
					char buf[80];
					snprintf(buf, sizeof(buf),
						 "G3: skip dcbf r29=%08x lr=%08x",
						 (unsigned)gpr(29), (unsigned)lr());
					nw_boot_log(buf);
				}
#endif
				pc() = lr();
				continue;
			}
			if (pc() == ROMBase + 0x3121b8u && gpr(22) >= 0x2000u) {
				/* Page-list fill: cmplwi r22,4096; stwu r31,4(r29); r31+=4K. */
#if NW_BOOT_LOG
				{
					static int pl;
					if (!pl) {
						pl = 1;
						char buf[112];
						snprintf(buf, sizeof(buf),
							 "G3: page list r22=%08x r31=%08x r29=%08x r20=%08x r21=%08x",
							 (unsigned)gpr(22), (unsigned)gpr(31),
							 (unsigned)gpr(29), (unsigned)gpr(20),
							 (unsigned)gpr(21));
						nw_boot_log(buf);
					}
				}
#endif
				if (gpr(31) < RAMBase ||
				    gpr(31) >= RAMBase + RAMSize) {
					gpr(22) = 0;
					pc() = ROMBase + 0x3121e0u;
					continue;
				}
				if (gpr(22) > RAMSize)
					gpr(22) = RAMSize;
				while (gpr(22) >= 0x1000u) {
					if (gpr(31) != gpr(20) &&
					    gpr(29) + 4 >= RAMBase &&
					    gpr(29) + 8 <= RAMBase + RAMSize) {
						gpr(29) += 4;
						vm_write_memory_4(gpr(29), gpr(31));
					} else if (gpr(31) != gpr(20)) {
						gpr(29) += 4;
					}
					gpr(31) += 0x1000u;
					gpr(22) -= 0x1000u;
				}
				pc() = ROMBase + 0x3121a4u;
				continue;
			}
			if (pc() == ROMBase + 0x3123fcu && gpr(22) > 0xffffu) {
				/* PMDT pointer array is 16 banks; r22=0x3fffffff is junk. */
#if NW_BOOT_LOG
				{
					static int pa;
					if (!pa) {
						pa = 1;
						char buf[112];
						snprintf(buf, sizeof(buf),
							 "G3: PMDT ptrs r22=%08x r21=%08x r19=%08x r29=%08x r30=%08x",
							 (unsigned)gpr(22), (unsigned)gpr(21),
							 (unsigned)gpr(19), (unsigned)gpr(29),
							 (unsigned)gpr(30));
						nw_boot_log(buf);
					}
				}
#endif
				unsigned nslot = 0;
				for (;;) {
					const uint32 old = gpr(22);
					gpr(19) += 8;
					const uint32 slot = vm_read_memory_4(gpr(19));
					uint32 r21v = gpr(21);
					uint32 r31v = (r21v << 10) | (r21v >> 22);
					r31v |= 0xc00u;
					if (slot >= RAMBase &&
					    slot + 8 <= RAMBase + RAMSize) {
						vm_write_memory_4(slot, gpr(30));
						vm_write_memory_4(slot + 4, r31v);
					}
					if (gpr(29) + 4 >= RAMBase &&
					    gpr(29) + 8 <= RAMBase + RAMSize) {
						gpr(29) += 4;
						vm_write_memory_4(gpr(29), r21v);
					} else {
						gpr(29) += 4;
					}
					gpr(21) = r21v + 0x40000u;
					gpr(22) += 0xffff0000u;
					nslot++;
					if (old <= 0xffffu || nslot >= 16)
						break;
				}
				gpr(22) = 0;
				pc() = ROMBase + 0x312424u;
				continue;
			}
			if ((pc() == ROMBase + 0x312244u && (int32)gpr(17) > 64) ||
			    (pc() == ROMBase + 0x3122d8u && (int32)gpr(17) > 64) ||
			    (pc() == ROMBase + 0x3124ccu && (int32)gpr(18) > 64)) {
#if NW_BOOT_LOG
				static int pr;
				if (!pr) {
					pr = 1;
					char buf[80];
					snprintf(buf, sizeof(buf),
						 "G3: skip page prime pc=%08x r17=%08x r18=%08x",
						 (unsigned)pc(), (unsigned)gpr(17),
						 (unsigned)gpr(18));
					nw_boot_log(buf);
				}
#endif
				if (pc() == ROMBase + 0x312244u)
					pc() = ROMBase + 0x312260u;
				else if (pc() == ROMBase + 0x3122d8u)
					pc() = ROMBase + 0x3122f0u;
				else
					pc() = ROMBase + 0x3124e4u;
				continue;
			}
			if (pc() == ROMBase + 0x312014u && gpr(22) > 0x1000u) {
				uint32 n = gpr(22);
				const uint32 src = gpr(9);
				const uint32 dst = gpr(18);
				const uint32 rel = gpr(26);
#if NW_BOOT_LOG
				{
					static int cv;
					if (!cv) {
						cv = 1;
						char buf[112];
						snprintf(buf, sizeof(buf),
							 "G3: HTAB cvt n=%08x src=%08x dst=%08x r26=%08x",
							 (unsigned)n, (unsigned)src,
							 (unsigned)dst, (unsigned)rel);
						nw_boot_log(buf);
					}
				}
#endif
				while ((int32)n > 0) {
					n -= 4;
					uint32 w = vm_read_memory_4(src + n);
					if ((w & 0x0a00u) == 0x0200u)
						w = (w & ~0x0200u) + rel;
					vm_write_memory_1(dst + n, w);
					vm_write_memory_1(dst + n + 1, w >> 8);
					vm_write_memory_1(dst + n + 2, w >> 16);
					vm_write_memory_1(dst + n + 3, w >> 24);
					if ((int32)n <= 0)
						break;
					n -= 4;
					w = vm_read_memory_4(src + n);
					vm_write_memory_1(dst + n, w);
					vm_write_memory_1(dst + n + 1, w >> 8);
					vm_write_memory_1(dst + n + 2, w >> 16);
					vm_write_memory_1(dst + n + 3, w >> 24);
				}
				gpr(22) = n;
				nw_guest_seed_rom_htab(ppc32_guest_mmu().sdr1());
				pc() = ROMBase + 0x312044u;
				continue;
			}
			if (nw_guest_first_data_dsi_seen() &&
			    (pc() == ROMBase + 0x36e8c0u ||
			    pc() == ROMBase + 0x46e8c0u ||
			    pc() == ROMBase + 0x36f900u ||
			    pc() == ROMBase + 0x46f900u)) {
				nw_guest_map_kernel_data();
				/* r24 is 68k PC; NK never seeded it. */
				{
					const uint32 r24 = g3_fix_r24(gpr(24));
					if (r24 != gpr(24)) {
						gpr(24) = r24;
#if NW_BOOT_LOG
						nw_boot_log("G3: seed 68k PC r24=ROM+0x2a");
#endif
					}
				}
				{
					const uint32 stk = RAMBase + RAMSize - 0x10000u;
					int i;
					for (i = 8; i <= 15; i++)
						gpr(i) = 0;
					for (i = 16; i <= 22; i++)
						gpr(i) = stk;
					gpr(23) = 0;
					gpr(25) = 0;
					gpr(26) = 0;
					gpr(27) = 0xffffffffu;
					gpr(28) = 0;
					gpr(29) = ROMBase + 0x380000u;
					gpr(30) = ROMBase + 0x380000u;
					gpr(31) = 0x68ffe000u + 0x1000u;
				}
			}
			/* 68k opcode stub bclr/blr to unmapped LR → resume.
			 * After G2 only: before that, execute the real PPC so
			 * the first DR load can miss through translate(). */
			if (nw_guest_first_data_dsi_seen() &&
			    pc() >= ROMBase + 0x360000u &&
			    pc() < ROMBase + 0x500000u) {
				const uint32 opw = vm_read_memory_4(pc());
				if ((opw & 0xfc0007feu) == 0x4c000020u) {
					const uint32 t = lr();
					if (!((t >= RAMBase && t < RAMBase + RAMSize) ||
					      (t >= ROMBase && t < ROMBase + 0x500000u) ||
					      t < 0x20000u)) {
#if NW_BOOT_LOG
						static unsigned nb;
						if (nb < 8) {
							nb++;
							char buf[80];
							snprintf(buf, sizeof(buf),
								 "G3: 68k bclr lr=%08x -> inner",
								 (unsigned)t);
							nw_boot_log(buf);
						}
#endif
						if (t <= 0x7fff8u)
							pc() = ROMBase + 0x380000u + t;
						else
							pc() = ROMBase + 0x366084u;
						continue;
					}
				}
			}
			if (nw_guest_first_data_dsi_seen() &&
			    ((pc() >= ROMBase + 0x361300u &&
			     pc() < ROMBase + 0x3616a0u) ||
			    (pc() >= ROMBase + 0x461300u &&
			     pc() < ROMBase + 0x4616a0u) ||
			    (pc() >= ROMBase + 0x3a1500u &&
			     pc() < ROMBase + 0x3a1600u) ||
			    (pc() >= ROMBase + 0x367600u &&
			     pc() < ROMBase + 0x367700u) ||
			    (pc() >= ROMBase + 0x467600u &&
			     pc() < ROMBase + 0x467700u) ||
			    (pc() >= ROMBase + 0x3665a0u &&
			     pc() < ROMBase + 0x3665f0u) ||
			    (pc() >= ROMBase + 0x4665a0u &&
			     pc() < ROMBase + 0x4665f0u) ||
			    (pc() >= ROMBase + 0x36d750u &&
			     pc() < ROMBase + 0x36d800u) ||
			    (pc() >= ROMBase + 0x46d750u &&
			     pc() < ROMBase + 0x46d800u) ||
			    (pc() >= ROMBase + 0x3a6300u &&
			     pc() < ROMBase + 0x3a6400u) ||
			    (pc() >= ROMBase + 0x4a6300u &&
			     pc() < ROMBase + 0x4a6400u) ||
			    (pc() >= ROMBase + 0x389100u &&
			     pc() < ROMBase + 0x389180u) ||
			    (pc() >= ROMBase + 0x489100u &&
			     pc() < ROMBase + 0x489180u) ||
			    (pc() >= ROMBase + 0x360a00u &&
			     pc() < ROMBase + 0x360a20u) ||
			    (pc() >= ROMBase + 0x460a00u &&
			     pc() < ROMBase + 0x460a20u) ||
			    (pc() >= ROMBase + 0x360c00u &&
			     pc() < ROMBase + 0x360c40u) ||
			    (pc() >= ROMBase + 0x460c00u &&
			     pc() < ROMBase + 0x460c40u) ||
			    (pc() >= ROMBase + 0x380000u &&
			     pc() < ROMBase + 0x400000u))) {
#if NW_BOOT_LOG
				static int blrl;
				if (!blrl) {
					blrl = 1;
					nw_boot_log("G3: skip 68k blrl helper");
				}
#endif
				gpr(27) = 0xffffffffu;
				pc() = ROMBase + 0x366084u;
				continue;
			}
			if (nw_guest_first_data_dsi_seen() &&
			    (pc() == ROMBase + 0x36ce20u ||
			    pc() == ROMBase + 0x46ce20u)) {
#if NW_BOOT_LOG
				static int decd;
				if (!decd) {
					decd = 1;
					nw_boot_log("G3: skip 68k decode helper");
				}
#endif
				gpr(27) = 0xffffffffu;
				pc() = ROMBase + 0x366084u;
				continue;
			}
			if (nw_guest_first_data_dsi_seen() &&
			    (pc() == ROMBase + 0x36ca80u ||
			    pc() == ROMBase + 0x46ca80u)) {
				/* mtctr r5 / bctrl / b 0x36ca80. r5 from
				 * 0x80c(r31) is 0 or a blr, so this never
				 * sets CR to exit. */
#if NW_BOOT_LOG
				static int hlp;
				if (!hlp) {
					hlp = 1;
					char buf[80];
					snprintf(buf, sizeof(buf),
						 "G3: skip 68k helper r5=%08x r31=%08x",
						 (unsigned)gpr(5), (unsigned)gpr(31));
					nw_boot_log(buf);
				}
#endif
				pc() = ROMBase + 0x36ca94u;
				continue;
			}
			if (nw_guest_first_data_dsi_seen() &&
			    pc() >= ROMBase && pc() < ROMBase + 0x310000u) {
				/* 68k image executed as PPC. Never mill EA 0..0xFFF:
				 * that is the DSI vector (0x300) HotInts must run. */
#if NW_BOOT_LOG
				static unsigned nppc;
				if (nppc < 8) {
					nppc++;
					char buf[80];
					snprintf(buf, sizeof(buf),
						 "G3: 68k resume ppc pc=%08x r24=%08x",
						 (unsigned)pc(), (unsigned)gpr(24));
					nw_boot_log(buf);
				}
#endif
				gpr(24) = g3_fix_r24(gpr(24));
				gpr(27) = 0xffffffffu;
				gpr(29) = ROMBase + 0x380000u;
				pc() = ROMBase + 0x366084u;
				continue;
			}
			if (nw_guest_first_data_dsi_seen() &&
			    (pc() == ROMBase + 0x366084u ||
			    pc() == ROMBase + 0x367c64u ||
			    pc() == ROMBase + 0x367c6cu ||
			    pc() == ROMBase + 0x466084u ||
			    pc() == ROMBase + 0x467c64u ||
			    pc() == ROMBase + 0x467c6cu)) {
				{
					uint32 sp = g3_rom0(gpr(1));
					if (sp < RAMBase ||
					    sp >= RAMBase + RAMSize)
						sp = RAMBase + 0x7e00000u;
					gpr(1) = sp;
				}
				gpr(22) = g3_rom0(gpr(22));
				{
					static int em_plant;
					if (!em_plant) {
						em_plant = 1;
						const uint32 em = RAMBase + 0x9000u;
						const uint32 obj = em + 0x400u;
						const uint32 stub = obj + 0x40u;
						vm_write_memory_4(0x2b6u, em);
						vm_write_memory_4(em + 0x2fcu, obj);
						vm_write_memory_4(obj + 0x0cu, stub);
						vm_write_memory_4(obj + 0x14u, stub);
						vm_write_memory_2(stub, 0x4e75u);
						{
							const uint32 rec1e0 = em + 0x200u;
							vm_write_memory_4(em + 0x1e0u, rec1e0);
							vm_write_memory_4(em + 0x1e4u, rec1e0);
							vm_write_memory_4(rec1e0 + 0x12u,
									  rec1e0 + 0x20u);
							vm_write_memory_2(rec1e0 + 0x20u,
									  0x4e75u);
							vm_write_memory_2(rec1e0 + 0x6cu, 1);
							/* 0x911c2 AND.B $10(A3),#2
							 * BEQ.W skip init if 0. */
							vm_write_memory_1(rec1e0 + 0x10u, 2);
							/* 0x50850 MOVEA.L $01fc(A0),A3
							 * TST.B $b5(A3) / BNE skip
							 * DisplayDispatch body. */
							{
								const uint32 rec1fc =
									em + 0x300u;
								vm_write_memory_4(
									em + 0x1fcu, rec1fc);
								vm_write_memory_1(
									rec1fc + 0xb5u, 0);
							}
#if NW_BOOT_LOG
							nw_boot_log("G3: 68k plant ExpandMem+0x1e4");
							nw_boot_log("G3: 68k plant rec1e0+0x10");
							nw_boot_log("G3: 68k plant ExpandMem+0x1fc");
#endif
						}
						vm_write_memory_2(0x34au, 480);
						vm_write_memory_2(0x34cu, 640);
						vm_write_memory_4(0xcf8u, RAMBase + 0x9c00u);
						vm_write_memory_2(0x360u, 0);
						vm_write_memory_4(0x362u, 0);
						vm_write_memory_4(0x366u, 0);
						vm_write_memory_1(0xbffu, 0);
#if NW_BOOT_LOG
						nw_boot_log("G3: 68k plant ScrVRes 480");
#endif
						vm_write_memory_4(0x68ffefd0u,
								  RAMBase + 0x9800u);
						{
							const uint32 a5 = RAMBase + 0xa000u;
							const uint32 zone = RAMBase + 0xb000u;
							vm_write_memory_4(0x2a6u, a5);
							vm_write_memory_4(0x2aau, a5);
							vm_write_memory_4(0x118u, zone);
							vm_write_memory_4(0x120u, zone);
							vm_write_memory_4(0x162u, 0);
							vm_write_memory_4(0x378u, 0);
							vm_write_memory_4(0x358u, 0);
							vm_write_memory_4(0x35cu, 0);
							vm_write_memory_2(0x360u, 0);
							vm_write_memory_4(0x1ff4u, zone);
							vm_write_memory_4(zone, 0);
							vm_write_memory_4(zone + 4u, zone + 0x100u);
							vm_write_memory_4(zone + 8u, zone + 0x100u);
							{
								const uint32 ut =
									RAMBase + 0x70000u;
								unsigned ti;
								for (ti = 0; ti < 0x400u; ti++)
									vm_write_memory_4(
										ut + ti * 4u, stub);
								vm_write_memory_4(0x11cu, ut);
							}
							vm_write_memory_2(0x1d2u, 0x200);
							{
								const uint32 fb =
									RAMBase + 0x400000u;
								vm_write_memory_4(0x824u, fb);
								vm_write_memory_4(0xdacu, fb);
								/* 0x8e770 loads $0808 as grafProc.
								 * 0x8e7a0 is the bits body (no
								 * $0808 reread). Not 0x8e770. */
								vm_write_memory_4(
									0x808u,
									ROMBase + 0x8e7a0u);
							}
							{
								/* Handle object for 0x5b11a
								 * MOVE.L $08A8 host only. Do
								 * not write $08A8 (SysError mill). */
								const uint32 h =
									RAMBase + 0xd800u;
								const uint32 blk =
									RAMBase + 0xd900u;
								vm_write_memory_4(h, blk);
								vm_write_memory_2(blk + 0x14u,
										  0x8000);
								vm_write_memory_4(blk + 0x1eu, 0);
							}
							{
								const uint32 ds =
									RAMBase + 0xc000u;
								const uint32 memtop =
									RAMBase + RAMSize -
									0x200000u;
								const uint32 stk =
									RAMBase + 0x7e00000u;
								vm_write_memory_4(0x2bau, ds);
								vm_write_memory_4(0x108u, memtop);
								vm_write_memory_4(0x10cu, memtop);
								vm_write_memory_4(0x110u,
										  stk - 0x10000u);
								vm_write_memory_4(0x114u,
										  RAMBase +
										  0x100000u);
								vm_write_memory_4(0x130u,
										  memtop);
								vm_write_memory_4(0x2aeu, ROMBase);
								vm_write_memory_4(0x2b2u, RAMBase);
								vm_write_memory_4(0x31eu, 0x2000u);
								vm_write_memory_4(0x322u, 0x8000u);
								/* 2f30/2f38 jump-through must land on
								 * a g3_r24_ok ROM RTS, not RAM stub. */
								/* $0730-$0748 2f38 jump-throughs.
								 * 0x20658 RTS made grafProc a no-op.
								 * 0x20b80 is the blit MOVEQ body. */
								vm_write_memory_4(0x730u, ROMBase + 0x20b80u);
								vm_write_memory_4(0x734u, ROMBase + 0x20b80u);
								vm_write_memory_4(0x738u, ROMBase + 0x20b80u);
								vm_write_memory_4(0x73cu, ROMBase + 0x20b80u);
								vm_write_memory_4(0x740u, ROMBase + 0x20b80u);
								vm_write_memory_4(0x744u, ROMBase + 0x20b80u);
								vm_write_memory_4(0x748u, ROMBase + 0x20b80u);
								vm_write_memory_4(0x794u, ROMBase + 0x20658u);
								vm_write_memory_4(0x7c8u, ROMBase + 0x20658u);
#if NW_BOOT_LOG
								{
									static unsigned nplbl;
									if (nplbl < 4) {
										nplbl++;
										nw_boot_log("G3: 68k plant $0740 blit 0x20b80");
									}
								}
#endif
								/* FileMgr JMP (A0)/JMP (A1) through
								 * $0698/$069C. */
								vm_write_memory_4(0x698u, ROMBase + 0x20658u);
								vm_write_memory_4(0x69cu, ROMBase + 0x20658u);
								{
									const uint32 rec =
										RAMBase + 0xa200u;
									vm_write_memory_4(0x2030u, rec);
									vm_write_memory_4(rec + 0x40u,
											  ROMBase + 0x20658u);
									vm_write_memory_4(rec + 0x28u,
											  ROMBase + 0x20658u);
									vm_write_memory_1(RAMBase + 0xa100u, 0x80);
#if NW_BOOT_LOG
									nw_boot_log("G3: 68k plant 2f30 RTS 0x20658");
#endif
								}
								vm_write_memory_1(0x15du, 0x24);
							/* ROM InitFCB writes MOVE.W #$5E,$03F6.
							 * Zero here is DIVU.W abs.W mill. */
							vm_write_memory_2(0x3f6u, 0x005e);
							vm_write_memory_4(0xcf0u, 0x80000000u);
							vm_write_memory_1(0x172u, 1);
							vm_write_memory_1(0x3f8u, 0);
							vm_write_memory_2(0xaf0u, 0x000d);
								vm_write_memory_2(0x308u, 0);
								vm_write_memory_4(0x30au, 0);
								vm_write_memory_4(0x30eu, 0);
								{
									const uint32 dummy =
										RAMBase + 0x60000u;
									unsigned di;
									for (di = 0; di < 0x200u;
									     di += 4)
										vm_write_memory_4(
											dummy + di,
											stub);
									vm_write_memory_4(
										0x16cu, stub);
									{
										const uint32 h =
											RAMBase + 0xd000u;
										const uint32 blk =
											RAMBase + 0xd100u;
										vm_write_memory_4(h, blk);
										vm_write_memory_4(blk, stub);
										vm_write_memory_4(0xa1cu, h);
									}
									/* $0192 is MOVEA.L/JMP (A0)
									 * (0x94be, 0xa022, 0xc700), not
									 * a dispatch table. dummy there
									 * executes pointer words as 68k.
									 * $01B2 is LEA'd as a table. */
									vm_write_memory_4(0x192u, stub);
									/* $019A is MOVEA.L/JSR (A0)
									 * (0x90bc, 0x9c84) like $0192. */
									vm_write_memory_4(0x19au, stub);
									vm_write_memory_4(0x1b2u, dummy);
#if NW_BOOT_LOG
									{
										static unsigned n192;
										if (n192 < 4) {
											n192++;
											nw_boot_log("G3: 68k plant $0192 $019A RTS stub");
										}
									}
#endif
									{
										const uint32 fcb =
											RAMBase + 0xe000u;
										const uint32 obj =
											RAMBase + 0xf000u;
										unsigned fi;
										vm_write_memory_2(fcb, 0x17au);
										for (fi = 0; fi < 16u; fi++)
											vm_write_memory_4(
												fcb + 0x14u + fi * 4u,
												obj);
										for (fi = 0; fi < 128u; fi += 2)
									vm_write_memory_2(obj + fi, 1);
										vm_write_memory_2(obj + 0x48u, 1);
										vm_write_memory_2(obj + 0x4cu, 0);
										vm_write_memory_4(0x34eu, fcb);
									}
								}
							}
						}
					}
				}
				uint32 r24 = gpr(24);
				{
					r24 = g3_rom0(r24);
					if (gpr(16) < 0x1000u &&
					    r24 >= ROMBase + 0x1f000u &&
					    r24 < ROMBase + 0x20000u)
						gpr(16) = RAMBase + 0x60000u;
					if (!g3_r24_ok(r24)) {
#if NW_BOOT_LOG
						static unsigned nbadpc;
						if (nbadpc < 12) {
							nbadpc++;
							char buf[80];
							snprintf(buf, sizeof(buf),
								 "G3: 68k pc fix %08x",
								 (unsigned)gpr(24));
							nw_boot_log(buf);
						}
#endif
						r24 = g3_fix_r24(gpr(24));
					} else
						(void)g3_fix_r24(r24);
					gpr(24) = r24;
				}
				uint32 op68 = vm_read_memory_2(r24);
				r24 += 2;
#if NW_BOOT_LOG
				if (r24 - 2u == ROMBase + 0x8e770u ||
				    r24 - 2u == ROMBase + 0x8e7a0u) {
					static unsigned nbits;
					if (nbits < 16) {
						nbits++;
						char buf[80];
						snprintf(buf, sizeof(buf),
							 "G3: 68k bits proc pc=%08x",
							 (unsigned)(r24 - 2u));
						nw_boot_log(buf);
					}
				}
				{
					static unsigned nspin;
					if ((++nspin & 0xffffu) == 0) {
						char buf[112];
						snprintf(buf, sizeof(buf),
							 "G3: 68k spin r24=%08x op=%04x ccr=%x d0=%08x a0=%08x a1=%08x a2=%08x a6=%08x",
							 (unsigned)(r24 - 2), (unsigned)op68,
							 (unsigned)g3_ccr, (unsigned)gpr(8),
							 (unsigned)gpr(16), (unsigned)gpr(17),
							 (unsigned)gpr(18), (unsigned)gpr(22));
						nw_boot_log(buf);
					}
				}
#endif
				/* Packed offset/60ff tables. Must run
				 * before any dest that lands here. */
				if (r24 - 2u >= ROMBase + 0x5080u &&
				    r24 - 2u < ROMBase + 0x6c94u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x6c94u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned npk0;
						if (npk0 < 8) {
							npk0++;
							nw_boot_log("G3: 68k skip packed 0x5080 early");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x49e64u &&
				    r24 - 2u < ROMBase + 0x4a0e0u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x49e64u &&
					     dest < ROMBase + 0x4a0e0u))
						dest = ROMBase + 0x4a0e0u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nzoneh;
						if (nzoneh < 8) {
							nzoneh++;
							nw_boot_log("G3: 68k skip zone helper 0x49e70");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x498e2u &&
				    r24 - 2u < ROMBase + 0x499d6u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x499d6u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned npktbl;
						if (npktbl < 8) {
							npktbl++;
							nw_boot_log("G3: 68k skip packed tbl 0x498e2");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1ffecu &&
				    r24 - 2u < ROMBase + 0x20000u) {
					gpr(24) = ROMBase + 0x20000u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ndivu;
						if (ndivu < 8) {
							ndivu++;
							nw_boot_log("G3: 68k skip DIVU mill 0x20000");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x20002u &&
				    r24 - 2u < ROMBase + 0x2012cu) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x20002u &&
					     dest < ROMBase + 0x2012cu))
						dest = ROMBase + 0x2012cu;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nqd20;
						if (nqd20 < 8) {
							nqd20++;
							nw_boot_log("G3: 68k skip QD helper 0x20000");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x2014eu &&
				    r24 - 2u < ROMBase + 0x20160u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x20160u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nallocw;
						if (nallocw < 8) {
							nallocw++;
							nw_boot_log("G3: 68k skip CLR.W alloca 0x20160");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x20220u &&
				    r24 - 2u < ROMBase + 0x203dau) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x203dau;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n60ff20;
						if (n60ff20 < 8) {
							n60ff20++;
							nw_boot_log("G3: 68k skip 60ff mill 0x203da");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x75778u &&
				    r24 - 2u < ROMBase + 0x77240u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x75778u &&
					     dest < ROMBase + 0x77240u))
						dest = ROMBase + 0x77240u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nqdepi;
						if (nqdepi < 8) {
							nqdepi++;
							nw_boot_log("G3: 68k skip QD blit 0x77240");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x2055au &&
				    r24 - 2u < ROMBase + 0x20574u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x20574u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nclrdf;
						if (nclrdf < 8) {
							nclrdf++;
							nw_boot_log("G3: 68k skip CLR.L DBF 0x20574");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x2065au &&
				    r24 - 2u < ROMBase + 0x208f8u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x2065au &&
					     dest < ROMBase + 0x208f8u) ||
					    (dest >= ROMBase + 0x20902u &&
					     dest < ROMBase + 0x20948u) ||
					    (dest >= ROMBase + 0x20952u &&
					     dest < ROMBase + 0x20b80u) ||
					    (dest >= ROMBase + 0x20986u &&
					     dest < ROMBase + 0x20b80u))
						dest = ROMBase + 0x20b80u;
					gpr(24) = dest;
					gpr(8) = 0;
					gpr(11) = 0;
					gpr(17) = RAMBase + 0xe000u;
					gpr(18) = RAMBase + 0xf000u;
					gpr(21) = RAMBase + 0xa100u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nleahel;
						if (nleahel < 8) {
							nleahel++;
							nw_boot_log("G3: 68k skip LEA helper 0x20b80");
						}
					}
#endif
					continue;
				}
				if ((r24 - 2u >= ROMBase + 0x20902u &&
				     r24 - 2u < ROMBase + 0x20948u) ||
				    (r24 - 2u >= ROMBase + 0x20952u &&
				     r24 - 2u < ROMBase + 0x20b80u)) {
					gpr(24) = ROMBase + 0x20b80u;
					gpr(8) = 0;
					gpr(11) = 0;
					gpr(17) = RAMBase + 0xe000u;
					gpr(18) = RAMBase + 0xf000u;
					gpr(21) = RAMBase + 0xa100u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ntzpad;
						if (ntzpad < 4) {
							ntzpad++;
							nw_boot_log("G3: 68k hole 2f30 0x20948");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x20bdcu &&
				    r24 - 2u < ROMBase + 0x20c9au) {
					gpr(24) = ROMBase + 0x20c9au;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nbe2;
						if (nbe2 < 8) {
							nbe2++;
							nw_boot_log("G3: 68k skip 2f38 blit JMP 0x20c9a");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x50ad0u &&
				    r24 - 2u < ROMBase + 0x50ae0u) {
					gpr(24) = ROMBase + 0x50ae0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nrtdp;
						if (nrtdp < 8) {
							nrtdp++;
							nw_boot_log("G3: 68k skip RTD pad 0x50ae0");
						}
					}
#endif
					continue;
				}
				/* Packed 00a6 + 60ff dest-edge after RTS
				 * 0x50e08. Dest 0x51100. Keep 0x50e08. */
				if (r24 - 2u >= ROMBase + 0x50e0au &&
				    r24 - 2u < ROMBase + 0x51100u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x50e0au &&
					     dest < ROMBase + 0x51100u))
						dest = ROMBase + 0x51100u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n60ff;
						if (n60ff < 8) {
							n60ff++;
							nw_boot_log("G3: 68k skip packed 0x51100");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x5112au &&
				    r24 - 2u < ROMBase + 0x51130u) {
					gpr(24) = ROMBase + 0x51130u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x51240u &&
				    r24 - 2u < ROMBase + 0x512d0u) {
					gpr(24) = ROMBase + 0x512d0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n512;
						if (n512 < 8) {
							n512++;
							nw_boot_log("G3: 68k skip jmp tbl 0x51240");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x4b1b0u &&
				    r24 - 2u < ROMBase + 0x4b1c0u) {
					gpr(24) = ROMBase + 0x4b1c0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x4211cu &&
				    r24 - 2u < ROMBase + 0x42130u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x4211cu &&
					     dest < ROMBase + 0x42130u))
						dest = ROMBase + 0x42130u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n421;
						if (n421 < 8) {
							n421++;
							nw_boot_log("G3: 68k skip 60ff pad 0x42130");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1f51cu &&
				    r24 - 2u < ROMBase + 0x1f538u) {
					gpr(24) = ROMBase + 0x1f538u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nio;
						if (nio < 4) {
							nio++;
							nw_boot_log("G3: 68k skip I/O JMP idx 0x1f51c");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x2ca40u &&
				    r24 - 2u < ROMBase + 0x2cab0u) {
					gpr(24) = ROMBase + 0x2cab0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ntbx;
						if (ntbx < 4) {
							ntbx++;
							nw_boot_log("G3: 68k skip 0x2ca40 thunks");
						}
					}
#endif
					continue;
				}
				/* Recurse on poison A1 (e7e7e7e7) until
				 * CMPA D2. Dest 0x2c774 is the RTS. */
				if (r24 - 2u >= ROMBase + 0x2c722u &&
				    r24 - 2u < ROMBase + 0x2c774u) {
					gpr(24) = ROMBase + 0x2c774u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nrec;
						if (nrec < 4) {
							nrec++;
							nw_boot_log("G3: 68k skip A1 recurse 0x2c722");
						}
					}
#endif
					continue;
				}
				/* UTable walk: UnitNtryCnt $01D2 was 0x200
				 * so 512 empty stub slots. Dest 0x2c240 is
				 * MOVEM restore after the save at 0x2c1e4. */
				if (r24 - 2u >= ROMBase + 0x2c1e8u &&
				    r24 - 2u < ROMBase + 0x2c240u) {
					gpr(24) = ROMBase + 0x2c240u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nut;
						if (nut < 8) {
							nut++;
							nw_boot_log("G3: 68k skip UTable walk 0x2c1e8");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x38b18u &&
				    r24 - 2u < ROMBase + 0x38b5eu) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x38b5eu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nevq;
						if (nevq < 8) {
							nevq++;
							nw_boot_log("G3: 68k skip EventQueue DBF 0x38b5e");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1a8e0u &&
				    r24 - 2u < ROMBase + 0x1a902u) {
					gpr(8) = 1;
					g3_ccr = 0;
					gpr(24) = ROMBase + 0x1a902u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ncksm;
						if (ncksm < 8) {
							ncksm++;
							nw_boot_log("G3: 68k skip checksum DBF 0x1a902");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0xb6u &&
				    r24 - 2u < ROMBase + 0x112u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x112u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nrsane;
						if (nrsane < 8) {
							nrsane++;
							nw_boot_log("G3: 68k skip ROM reset SANE 0x112");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x2c858u &&
				    r24 - 2u < ROMBase + 0x2ca0eu) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x2ca0eu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nedsk;
						if (nedsk < 4) {
							nedsk++;
							nw_boot_log("G3: 68k skip EDisk dest 0x2ca0e");
						}
					}
#endif
					continue;
				}
				/* BSR 0x2c858 then DrvQ walk at $0358.
				 * Dest 0x2ca0e is CLR.W D0 + MOVEA.L (A7)+,A2
				 * + RTS (same as the function's empty-queue
				 * path). Do not include 0x2ca0e in the range. */
				if (r24 - 2u >= ROMBase + 0x2c8e6u &&
				    r24 - 2u < ROMBase + 0x2ca0eu) {
					gpr(24) = ROMBase + 0x2ca0eu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ndrvq;
						if (ndrvq < 4) {
							ndrvq++;
							nw_boot_log("G3: 68k skip EDisk DrvQ 0x2c8e6");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x4418u &&
				    r24 - 2u < ROMBase + 0x446eu) {
					gpr(24) = ROMBase + 0x446eu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned npack;
						if (npack < 4) {
							npack++;
							nw_boot_log("G3: 68k skip pack/DrvQ walk");
						}
					}
#endif
					continue;
				}
				/* $0CF8 queue walk. Dest found. */
				if (r24 - 2u >= ROMBase + 0x2b30au &&
				    r24 - 2u < ROMBase + 0x2b328u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x2b328u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ncf8w;
						if (ncf8w < 8) {
							ncf8w++;
							nw_boot_log("G3: 68k skip $0CF8 walk 0x2b328");
						}
					}
#endif
					continue;
				}
				/* $015D BTST wait. Synthetic RTS; dest
				 * 0x2b376 was dest-edge bounce. */
				if (r24 - 2u >= ROMBase + 0x2b32cu &&
				    r24 - 2u < ROMBase + 0x2b418u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x2b32cu &&
					     dest < ROMBase + 0x2b418u))
						dest = g3_fix_r24(r24);
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nbtst2;
						if (nbtst2 < 8) {
							nbtst2++;
							nw_boot_log("G3: 68k skip BTST wait synth RTS");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1ce5au &&
				    r24 - 2u < ROMBase + 0x1ce66u) {
					gpr(24) = ROMBase + 0x1ce66u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nbgt;
						if (nbgt < 4) {
							nbgt++;
							nw_boot_log("G3: 68k skip BGT wait 0x1ce64");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x0fe9au &&
				    r24 - 2u < ROMBase + 0x13000u) {
					const uint32 sp = gpr(1);
					const uint32 t = vm_read_memory_4(sp);
					gpr(1) = sp + 4;
					{
						const uint32 dest = g3_rom0(t);
						if (g3_r24_ok(dest) && dest != r24 - 2u)
							gpr(24) = dest;
						else
							gpr(24) = g3_fix_r24(r24);
					}
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nnub;
						if (nnub < 4) {
							nnub++;
							nw_boot_log("G3: 68k skip Nub dest-RTS 0x13000");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x2c8d8u &&
				    r24 - 2u < ROMBase + 0x2ca0eu) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x2ca0eu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nclr;
						if (nclr < 4) {
							nclr++;
							nw_boot_log("G3: 68k skip CLR.L DBF fill");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1f77au &&
				    r24 - 2u < ROMBase + 0x1f7b0u) {
					gpr(24) = ROMBase + 0x1f7b0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nthunk;
						if (nthunk < 4) {
							nthunk++;
							nw_boot_log("G3: 68k skip 0x1f77a thunk wrap");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x848eu &&
				    r24 - 2u < ROMBase + 0x852cu) {
					gpr(24) = ROMBase + 0x852cu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				/* GetResource type '#' + three MOVE.L #0.
				 * Dest RTD #6. */
				if (r24 - 2u >= ROMBase + 0x40040u &&
				    r24 - 2u < ROMBase + 0x40106u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x40106u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ngr;
						if (ngr < 8) {
							ngr++;
							nw_boot_log("G3: 68k skip GetRes 0x40040");
						}
					}
#endif
					continue;
				}
				/* GetResource 'pref' / Gestalt mill.
				 * Dest 0x43412 RTS. Keep LINK 0x432c0.
				 * Not 0x8e770. */
				if (r24 - 2u >= ROMBase + 0x432c0u &&
				    r24 - 2u < ROMBase + 0x4381eu) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x4381eu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned npref;
						if (npref < 8) {
							npref++;
							nw_boot_log("G3: 68k skip pref rsrc 0x432c0-0x4381e");
						}
					}
#endif
					continue;
				}
				/* FB #$400 probe (A0=ScrnBase). Dest RTS.
				 * Not 0x8e770. */
				if (r24 - 2u >= ROMBase + 0x44fc0u &&
				    r24 - 2u < ROMBase + 0x44ff4u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x44ff4u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nfb;
						if (nfb < 8) {
							nfb++;
							nw_boot_log("G3: 68k skip FB probe 0x44fc0");
						}
					}
#endif
					continue;
				}
				/* DrvQ $036A JMP + BRA * 0x2508c.
				 * Dest 0x251be MOVEA (not dest-RTS 0x251bc). */
				if (r24 - 2u >= ROMBase + 0x25000u &&
				    r24 - 2u < ROMBase + 0x251bcu) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x251beu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ndrv2;
						if (ndrv2 < 8) {
							ndrv2++;
							nw_boot_log("G3: 68k skip DrvQ BRA 0x251be");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x251ccu &&
				    r24 - 2u < ROMBase + 0x251d0u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x251d0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				/* ADB/MemTop/TimeDBRA/FCBSPtr mill
				 * including TimeDBRA/$014A prelude and
				 * dest-edge DBF at 0xbd28. Dest 0xd5f4.
				 * Keep 0xafae. */
				if (r24 - 2u >= ROMBase + 0xafaeu &&
				    r24 - 2u < ROMBase + 0xdfeau) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0xdfeau;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nadb;
						if (nadb < 8) {
							nadb++;
							nw_boot_log("G3: 68k skip ADB mill 0xafae-0xdfea");
						}
					}
#endif
					continue;
				}
				/* ADB reloc MOVE.L (A2)+ / ADD A3 / (A1)+
				 * / DBF. Keep the copy; finish remaining
				 * iters in one host. Do not skip. */
				{
					const uint32 relpc = r24 - 2u;
					int reloc = 0;
					uint32 reldest = 0;
					if (relpc >= ROMBase + 0xe01cu &&
					    relpc < ROMBase + 0xe02cu) {
						reloc = 1;
						reldest = ROMBase + 0xe02cu;
					} else if (relpc >= ROMBase + 0xe034u &&
						   relpc < ROMBase + 0xe044u) {
						reloc = 1;
						reldest = ROMBase + 0xe044u;
					}
					if (reloc) {
						uint32 a1 = gpr(17);
						uint32 a2 = gpr(18);
						const uint32 a3 = gpr(19);
						const uint32 a4 = gpr(20);
						uint32 d0w = gpr(8) & 0xffffu;
						int have_d1 = 0;
						uint32 d1 = gpr(9);
						if (relpc == ROMBase + 0xe026u ||
						    relpc == ROMBase + 0xe03eu)
							have_d1 = 1;
						else if (relpc == ROMBase + 0xe028u ||
							 relpc == ROMBase + 0xe040u) {
							if (d0w == 0)
								goto reloc_done;
							d0w--;
						}
						for (;;) {
							if (!have_d1) {
								d1 = 0;
								if (g3_ea_data(a2))
									d1 = vm_read_memory_4(
										g3_rom0(a2));
								a2 += 4;
								if (d1 == 0)
									d1 = a4;
								else
									d1 += a3;
							}
							have_d1 = 0;
							if (g3_ea_data(a1))
								vm_write_memory_4(
									g3_rom0(a1), d1);
							a1 += 4;
							if (d0w == 0)
								break;
							d0w--;
						}
					reloc_done:
						gpr(17) = a1;
						gpr(18) = a2;
						gpr(8) = (gpr(8) & 0xffff0000u) |
							 0xffffu;
						gpr(9) = d1;
						gpr(24) = reldest;
						gpr(27) = 0xffffffffu;
						gpr(29) = ROMBase + 0x380000u;
						pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
						{
							static unsigned nreloc;
							if (nreloc < 8) {
								nreloc++;
								nw_boot_log(
									reldest == ROMBase + 0xe044u
									? "G3: 68k ADB reloc DBF 0xe044"
									: "G3: 68k ADB reloc DBF 0xe02c");
							}
						}
#endif
						continue;
					}
				}
				if (r24 - 2u >= ROMBase + 0x9b94u &&
				    r24 - 2u < ROMBase + 0x9bd4u) {
					/* BTST $0BFE / BEQ 0x9bcc was hottest
					 * after $0192 RTS plant. Dest KEEP
					 * 0x9bd4 MOVEM. Keep 0x9b8c MOVEM. */
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x9bd4u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nvia1a;
						if (nvia1a < 8) {
							nvia1a++;
							nw_boot_log("G3: 68k skip VIA BTST 0x9b94");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x9c56u &&
				    r24 - 2u < ROMBase + 0x9c80u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x9c80u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nvia1b;
						if (nvia1b < 8) {
							nvia1b++;
							nw_boot_log("G3: 68k skip VIA BTST 0x9c80");
						}
					}
#endif
					continue;
				}
				/* VIA CMP/DBEQ poll after ORI SR. Dest RTS. */
				if (r24 - 2u >= ROMBase + 0xaf62u &&
				    r24 - 2u < ROMBase + 0xafacu) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0xafacu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nviap;
						if (nviap < 8) {
							nviap++;
							nw_boot_log("G3: 68k skip VIA poll 0xafac");
						}
					}
#endif
					continue;
				}
				/* $08A8 wait / ABEB loop. Dest RTS. */
				if (r24 - 2u >= ROMBase + 0x511c0u &&
				    r24 - 2u < ROMBase + 0x51220u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x51220u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n8a8;
						if (n8a8 < 8) {
							n8a8++;
							nw_boot_log("G3: 68k skip $08A8 wait 0x51220");
						}
					}
#endif
					continue;
				}
				/* TimeDBRA/VIA nested DBF delay + dest copy 0x9e6a.
				 * Dest 0xa008 RTS. Keep 0x9c80. Dest 0x9c8a
				 * MOVEM restore collapsed unique. */
				if (r24 - 2u >= ROMBase + 0x9c82u &&
				    r24 - 2u < ROMBase + 0xa008u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0xa008u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ntdb;
						if (ntdb < 8) {
							ntdb++;
							nw_boot_log("G3: 68k skip TimeDBRA dest 0xa008");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x16daau &&
				    r24 - 2u < ROMBase + 0x16daeu) {
					g3_ccr |= 4;
					gpr(24) = ROMBase + 0x16daeu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nd7w;
						if (nd7w < 8) {
							nd7w++;
							nw_boot_log("G3: 68k skip D7 wait 0x16dae");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x8a4eu &&
				    r24 - 2u < ROMBase + 0x8a52u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x8a52u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nbne8;
						if (nbne8 < 8) {
							nbne8++;
							nw_boot_log("G3: 68k skip BNE.W * 0x8a4e");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x909cu &&
				    r24 - 2u < ROMBase + 0x90a8u) {
					gpr(24) = ROMBase + 0x90a8u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nvia1a;
						if (nvia1a < 8) {
							nvia1a++;
							nw_boot_log("G3: 68k skip VIA 1a00 0x90a8");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x8fc6u &&
				    r24 - 2u < ROMBase + 0x8fc8u) {
					gpr(24) = ROMBase + 0x8fc8u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x9010u &&
				    r24 - 2u < ROMBase + 0x9018u) {
					gpr(24) = ROMBase + 0x9018u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x9038u &&
				    r24 - 2u < ROMBase + 0x9044u) {
					gpr(24) = ROMBase + 0x9044u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x905eu &&
				    r24 - 2u < ROMBase + 0x9066u) {
					gpr(24) = ROMBase + 0x9066u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x9088u &&
				    r24 - 2u < ROMBase + 0x908au) {
					gpr(24) = ROMBase + 0x908au;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x8dd2u &&
				    r24 - 2u < ROMBase + 0x8dd6u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x8dd6u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ntst8;
						if (ntst8 < 8) {
							ntst8++;
							nw_boot_log("G3: 68k skip TST wait 0x8dd2");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x2574cu &&
				    r24 - 2u < ROMBase + 0x25754u) {
					gpr(24) = ROMBase + 0x25754u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nbsetw;
						if (nbsetw < 8) {
							nbsetw++;
							nw_boot_log("G3: 68k skip BSET wait 0x25754");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x256b4u &&
				    r24 - 2u < ROMBase + 0x256ccu) {
					gpr(24) = ROMBase + 0x256ccu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nsw03;
						if (nsw03 < 8) {
							nsw03++;
							nw_boot_log("G3: 68k skip SWAP mill 0x256cc");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x257dcu &&
				    r24 - 2u < ROMBase + 0x257fau) {
					gpr(24) = ROMBase + 0x257fau;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nswbr;
						if (nswbr < 8) {
							nswbr++;
							nw_boot_log("G3: 68k skip SWAP BRA 0x257fa");
						}
					}
#endif
					continue;
				}
				/* VIA $1a00(A1) BTST + NOP/BRA.L waits.
				 * Dest 0x9800 LEA (not dest-RTS 0x97b8). */
				if (r24 - 2u >= ROMBase + 0x9500u &&
				    r24 - 2u < ROMBase + 0x9800u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x9800u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nvia3;
						if (nvia3 < 8) {
							nvia3++;
							nw_boot_log("G3: 68k skip VIA NOP 0x9800");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x9888u &&
				    r24 - 2u < ROMBase + 0x989cu) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x989cu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nviat2;
						if (nviat2 < 8) {
							nviat2++;
							nw_boot_log("G3: 68k skip VIA table 0x989c");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1b944u &&
				    r24 - 2u < ROMBase + 0x1b94eu) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x1b94eu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ntsa0;
						if (ntsa0 < 8) {
							ntsa0++;
							nw_boot_log("G3: 68k skip TST.B $A0 wait 0x1b94e");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1ccc6u &&
				    r24 - 2u < ROMBase + 0x1ccdcu) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x1ccdcu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ntwgt;
						if (ntwgt < 8) {
							ntwgt++;
							nw_boot_log("G3: 68k skip TST.W BGT wait 0x1ccdc");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0xe074u &&
				    r24 - 2u < ROMBase + 0xe080u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0xe080u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n60ffe0;
						if (n60ffe0 < 8) {
							n60ffe0++;
							nw_boot_log("G3: 68k skip ADB zeros 0xe080");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0xe086u &&
				    r24 - 2u < ROMBase + 0xe270u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0xe270u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nadbpk;
						if (nadbpk < 8) {
							nadbpk++;
							nw_boot_log("G3: 68k skip ADB packed 0xe270");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0xe61au &&
				    r24 - 2u < ROMBase + 0xe99cu) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0xe99cu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nadbdf;
						if (nadbdf < 8) {
							nadbdf++;
							nw_boot_log("G3: 68k skip ADB DBF 0xe99c");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0xe3ceu &&
				    r24 - 2u < ROMBase + 0xe3d8u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0xe3d8u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ne3d8;
						if (ne3d8 < 8) {
							ne3d8++;
							nw_boot_log("G3: 68k skip JMP pad 0xe3d8");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x8b7eu &&
				    r24 - 2u < ROMBase + 0x8baeu) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x8baeu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n8bae;
						if (n8bae < 8) {
							n8bae++;
							nw_boot_log("G3: 68k skip mask tbl 0x8bae");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0xa028u &&
				    r24 - 2u < ROMBase + 0xa0c6u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0xa0c6u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned na0c6;
						if (na0c6 < 8) {
							na0c6++;
							nw_boot_log("G3: 68k skip vec tbl 0xa0c6");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0xa00cu &&
				    r24 - 2u < ROMBase + 0xa01eu) {
					gpr(24) = ROMBase + 0xa01eu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned na01e;
						if (na01e < 8) {
							na01e++;
							nw_boot_log("G3: 68k skip IRQ A1 0xa01e");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x9fdd8u &&
				    r24 - 2u < ROMBase + 0x9fde0u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x9fde0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmm9f;
						if (nmm9f < 8) {
							nmm9f++;
							nw_boot_log("G3: 68k skip MM 2f30 0x9fde0");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x13400u &&
				    r24 - 2u < ROMBase + 0x13620u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x13620u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nslotm;
						if (nslotm < 8) {
							nslotm++;
							nw_boot_log("G3: 68k skip slot mid 0x13620");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x498eu &&
				    r24 - 2u < ROMBase + 0x4990u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x4990u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x15690u &&
				    r24 - 2u < ROMBase + 0x156d0u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x156d0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n60ff15;
						if (n60ff15 < 8) {
							n60ff15++;
							nw_boot_log("G3: 68k skip 60ff pad 0x156d0");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0xf18eu &&
				    r24 - 2u < ROMBase + 0xf190u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0xf190u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0xf1acu &&
				    r24 - 2u < ROMBase + 0xf1b0u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0xf1b0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned njmpz;
						if (njmpz < 8) {
							njmpz++;
							nw_boot_log("G3: 68k skip JMP zeros 0xf1b0");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0xa2f4u &&
				    r24 - 2u < ROMBase + 0xa340u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0xa2f4u &&
					     dest < ROMBase + 0xa340u))
						dest = ROMBase + 0xa340u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmm2f;
						if (nmm2f < 8) {
							nmm2f++;
							nw_boot_log("G3: 68k skip MM 2f30 0xa340");
						}
					}
#endif
					continue;
				}
				/* CopyBits/MaskBits inner DBcc mill.
				 * Dest 0x816ae MOVEA.L -6(A6),A7 then
				 * MOVEM/UNLK/RTD. Keep LINK 0x81572. */
				if (r24 - 2u >= ROMBase + 0x815aeu &&
				    r24 - 2u < ROMBase + 0x816aeu) {
					gpr(24) = ROMBase + 0x816aeu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ncbm;
						if (ncbm < 8) {
							ncbm++;
							nw_boot_log("G3: 68k skip CopyBits mill 0x815ae");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x81d00u &&
				    r24 - 2u < ROMBase + 0x81d80u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x81d00u &&
					     dest < ROMBase + 0x81d80u))
						dest = ROMBase + 0x81d80u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ncb81;
						if (ncb81 < 8) {
							ncb81++;
							nw_boot_log("G3: 68k skip CopyBits 0x81d00");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x85900u &&
				    r24 - 2u < ROMBase + 0x85968u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x85900u &&
					     dest < ROMBase + 0x85968u))
						dest = ROMBase + 0x85968u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ncbm2;
						if (ncbm2 < 8) {
							ncbm2++;
							nw_boot_log("G3: 68k skip CopyBits DBF 0x85900");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x81a3cu &&
				    r24 - 2u < ROMBase + 0x81b78u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x81b78u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ncbcp;
						if (ncbcp < 8) {
							ncbcp++;
							nw_boot_log("G3: 68k skip CopyBits copy DBF 0x81b78");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x8281au &&
				    r24 - 2u < ROMBase + 0x8e770u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    dest == ROMBase + 0x8e770u ||
					    (dest >= ROMBase + 0x8281au &&
					     dest < ROMBase + 0x8e770u))
						dest = ROMBase + 0x8e770u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nble82;
						if (nble82 < 8) {
							nble82++;
							nw_boot_log("G3: 68k skip MaskBits dest 0x8e770");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x74660u &&
				    r24 - 2u < ROMBase + 0x7509eu) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x74660u &&
					     dest < ROMBase + 0x7509eu))
						dest = ROMBase + 0x7509eu;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nalloca;
						if (nalloca < 8) {
							nalloca++;
							nw_boot_log("G3: 68k skip alloca DBF 0x7509e");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x870f2u &&
				    r24 - 2u < ROMBase + 0x8e770u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x870f2u &&
					     dest < ROMBase + 0x8e770u))
						dest = ROMBase + 0x8e770u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ncb871;
						if (ncb871 < 8) {
							ncb871++;
							nw_boot_log("G3: 68k skip MaskBits dest 0x8e770");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x7a402u &&
				    r24 - 2u < ROMBase + 0x7a538u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x7a538u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nblit7a;
						if (nblit7a < 8) {
							nblit7a++;
							nw_boot_log("G3: 68k skip blit DBF 0x7a538");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x80000u &&
				    r24 - 2u < ROMBase + 0x806d0u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x80000u &&
					     dest < ROMBase + 0x806d0u))
						dest = ROMBase + 0x806d0u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ncb800;
						if (ncb800 < 8) {
							ncb800++;
							nw_boot_log("G3: 68k skip CopyBits 0x806d0");
						}
					}
#endif
					continue;
				}
				/* DBF D3 CLR.L / MOVE.B fill waits. */
				if (r24 - 2u >= ROMBase + 0x8190u &&
				    r24 - 2u < ROMBase + 0x81f0u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x81f0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ndbf;
						if (ndbf < 8) {
							ndbf++;
							nw_boot_log("G3: 68k skip DBF fill 0x81f0");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x81e8u &&
				    r24 - 2u < ROMBase + 0x81f0u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x81f0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmc;
						if (nmc < 8) {
							nmc++;
							nw_boot_log("G3: 68k skip Mc pad 0x81f0");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x8584u &&
				    r24 - 2u < ROMBase + 0x8592u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x8592u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n8592;
						if (n8592 < 8) {
							n8592++;
							nw_boot_log("G3: 68k skip offset tbl 0x8592");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x27b00u &&
				    r24 - 2u < ROMBase + 0x27ca0u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x27ca0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n27ca0;
						if (n27ca0 < 8) {
							n27ca0++;
							nw_boot_log("G3: 68k skip copy-outer DBF 0x27ca0");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x8eb0u &&
				    r24 - 2u < ROMBase + 0x932au) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x932au;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned npkcp;
						if (npkcp < 8) {
							npkcp++;
							nw_boot_log("G3: 68k skip packed copy 0x932a");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x2596eu &&
				    r24 - 2u < ROMBase + 0x25974u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x25974u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n2f07c8;
						if (n2f07c8 < 8) {
							n2f07c8++;
							nw_boot_log("G3: 68k skip 2f38 $07c8 0x25974");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x25f74u &&
				    r24 - 2u < ROMBase + 0x25f7au) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x25f7au;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n2f0770;
						if (n2f0770 < 8) {
							n2f0770++;
							nw_boot_log("G3: 68k skip 2f38 $0770 0x25f7a");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x28640u &&
				    r24 - 2u < ROMBase + 0x28786u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x28786u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmbcp;
						if (nmbcp < 8) {
							nmbcp++;
							nw_boot_log("G3: 68k skip memcpy DBF 0x28786");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x58000u &&
				    r24 - 2u < ROMBase + 0x58090u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x58090u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmenubar;
						if (nmenubar < 8) {
							nmenubar++;
							nw_boot_log("G3: 68k skip DrawMenuBar 0x58090");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x27a72u &&
				    r24 - 2u < ROMBase + 0x27aaau) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x27aaau;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n27a9a;
						if (n27a9a < 8) {
							n27a9a++;
							nw_boot_log("G3: 68k skip DBF D2 0x27aaa");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x94c8u &&
				    r24 - 2u < ROMBase + 0x96a6u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x96a6u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nvia94;
						if (nvia94 < 8) {
							nvia94++;
							nw_boot_log("G3: 68k skip VIA BTST 0x96a6");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x95ee2u &&
				    r24 - 2u < ROMBase + 0x96788u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x96788u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nqdlw;
						if (nqdlw < 8) {
							nqdlw++;
							nw_boot_log("G3: 68k skip QD list walk 0x96788");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x91942u &&
				    r24 - 2u < ROMBase + 0x91980u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x91980u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ndd91;
						if (ndd91 < 8) {
							ndd91++;
							nw_boot_log("G3: 68k skip DisplayDispatch 0x91980");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x9900u &&
				    r24 - 2u < ROMBase + 0x99a8u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x99a8u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ncould;
						if (ncould < 8) {
							ncould++;
							nw_boot_log("G3: 68k skip SysError str 0x99a8");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x2838cu &&
				    r24 - 2u < ROMBase + 0x283b0u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x283b0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nhex;
						if (nhex < 8) {
							nhex++;
							nw_boot_log("G3: 68k skip hex digits 0x283b0");
						}
					}
#endif
					continue;
				}
				/* JMP (d16,PC) trampolines to 0x8220.
				 * Dest 0x83a2 (not 0x82c0 dest-edge).
				 * Keep 0x81f0 LINK. */
				if (r24 - 2u >= ROMBase + 0x81f2u &&
				    r24 - 2u < ROMBase + 0x83a2u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x81f2u &&
					     dest < ROMBase + 0x83a2u))
						dest = ROMBase + 0x83a2u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned njmpa0;
						if (njmpa0 < 8) {
							njmpa0++;
							nw_boot_log("G3: 68k skip JMP PC mill 0x83a2");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x400000u &&
				    r24 - 2u < ROMBase + 0x500000u) {
					gpr(24) = g3_fix_r24(r24 - 2u);
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n4m;
						if (n4m < 4) {
							n4m++;
							nw_boot_log("G3: 68k skip ROM+4MiB copy");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0xa590u &&
				    r24 - 2u < ROMBase + 0xa928u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0xa928u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nzone;
						if (nzone < 4) {
							nzone++;
							nw_boot_log("G3: 68k skip zone walk 0xa5a0");
						}
					}
#endif
					continue;
				}
				/* VIA $015E wait + Cuda packet + BRA.L.
				 * Dest 0x7510 MOVEM. Keep 0x70a4 BMI. */
				if (r24 - 2u >= ROMBase + 0x70a6u &&
				    r24 - 2u < ROMBase + 0x7510u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x70a6u &&
					     dest < ROMBase + 0x7510u))
						dest = ROMBase + 0x7510u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nvia4;
						if (nvia4 < 8) {
							nvia4++;
							nw_boot_log("G3: 68k skip VIA wait 0x70a6");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x2131au &&
				    r24 - 2u < ROMBase + 0x214dau) {
					gpr(24) = ROMBase + 0x214dau;
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((r24 - 2u >= ROMBase + 0x21526u &&
				     r24 - 2u < ROMBase + 0x21528u) ||
				    (r24 - 2u >= ROMBase + 0x21572u &&
				     r24 - 2u < ROMBase + 0x21574u) ||
				    (r24 - 2u >= ROMBase + 0x21598u &&
				     r24 - 2u < ROMBase + 0x2159au) ||
				    (r24 - 2u >= ROMBase + 0x224aeu &&
				     r24 - 2u < ROMBase + 0x224b0u) ||
				    (r24 - 2u >= ROMBase + 0x224d6u &&
				     r24 - 2u < ROMBase + 0x224d8u)) {
					uint32 dest = r24;
					if (r24 - 2u < ROMBase + 0x21528u)
						dest = ROMBase + 0x21528u;
					else if (r24 - 2u < ROMBase + 0x21574u)
						dest = ROMBase + 0x21574u;
					else if (r24 - 2u < ROMBase + 0x2159au)
						dest = ROMBase + 0x2159au;
					else if (r24 - 2u < ROMBase + 0x224b0u)
						dest = ROMBase + 0x224b0u;
					else
						dest = ROMBase + 0x224d8u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nbrde;
						if (nbrde < 8) {
							nbrde++;
							nw_boot_log("G3: 68k skip BRA dest 0x2159a");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0xa54au &&
				    r24 - 2u < ROMBase + 0xa54cu) {
					gpr(24) = ROMBase + 0xa54cu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x2116eu &&
				    r24 - 2u < ROMBase + 0x214dau) {
					gpr(24) = ROMBase + 0x214dau;
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x22460u &&
				    r24 - 2u < ROMBase + 0x224a0u) {
					gpr(24) = ROMBase + 0x224a0u;
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nqdp;
						if (nqdp < 8) {
							nqdp++;
							nw_boot_log("G3: 68k skip QD poll 0x224a0");
						}
					}
#endif
					continue;
				}
				/* GetCatInfo wrapper A260 sel 9. Dest RTS. */
				if (r24 - 2u >= ROMBase + 0x13f00u &&
				    r24 - 2u < ROMBase + 0x13f76u) {
					gpr(8) = 0xffffffd5u;
					g3_ccr = 8;
					gpr(24) = ROMBase + 0x13f76u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ngci;
						if (ngci < 8) {
							ngci++;
							nw_boot_log("G3: 68k skip GetCatInfo 0x13f76");
						}
					}
#endif
					continue;
				}
				/* Catalog/sResource walker. Dest RTS. */
				if (r24 - 2u >= ROMBase + 0x13f80u &&
				    r24 - 2u < ROMBase + 0x1425au) {
					gpr(8) = 0xffffffd5u;
					g3_ccr = 8;
					gpr(24) = ROMBase + 0x1425au;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ncatw;
						if (ncatw < 8) {
							ncatw++;
							nw_boot_log("G3: 68k skip cat walk 0x1425a");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x14bf0u &&
				    r24 - 2u < ROMBase + 0x14c60u) {
					gpr(8) = 0xfffff4feu;
					g3_ccr = 8;
					gpr(24) = ROMBase + 0x14c60u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ncaterr;
						if (ncaterr < 8) {
							ncaterr++;
							nw_boot_log("G3: 68k skip cat err 0x14c60");
						}
					}
#endif
					continue;
				}
				/* Stretch/scaler. Dest RTS. Not 0x20ca4. */
				if (r24 - 2u >= ROMBase + 0xa37c0u &&
				    r24 - 2u < ROMBase + 0xa3a34u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0xa3a34u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nstr;
						if (nstr < 8) {
							nstr++;
							nw_boot_log("G3: 68k skip stretch 0xa3a34");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0xa496u &&
				    r24 - 2u < ROMBase + 0xa4a4u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0xa4a4u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nbit;
						if (nbit < 8) {
							nbit++;
							nw_boot_log("G3: 68k skip bit scan 0xa4a4");
						}
					}
#endif
					continue;
				}
				/* PixMap NewPtr mill + 2f38 $0748 JMP blit.
				 * Dest 0x20ffe. Not 0x20ca4. */
				if (r24 - 2u >= ROMBase + 0x20f3eu &&
				    r24 - 2u < ROMBase + 0x20ffeu) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x20ffeu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned npix;
						if (npix < 8) {
							npix++;
							nw_boot_log("G3: 68k skip pixmap 0x20ffe");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1ea50u &&
				    r24 - 2u < ROMBase + 0x1ebe0u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x1ebe0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned npix2;
						if (npix2 < 8) {
							npix2++;
							nw_boot_log("G3: 68k skip pixmap 0x1ebe0");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x210e4u &&
				    r24 - 2u < ROMBase + 0x21100u) {
					gpr(24) = ROMBase + 0x21100u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n60ff;
						if (n60ff < 4) {
							n60ff++;
							nw_boot_log("G3: 68k skip 60ff 0x21100");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1e69eu &&
				    r24 - 2u < ROMBase + 0x1e6fcu) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x1e6fcu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned neor;
						if (neor < 8) {
							neor++;
							nw_boot_log("G3: 68k skip EOR DBNE 0x1e6fc");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1e76au &&
				    r24 - 2u < ROMBase + 0x1e794u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x1e794u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nffff;
						if (nffff < 8) {
							nffff++;
							nw_boot_log("G3: 68k skip fill FFFF DBF 0x1e794");
						}
					}
#endif
					continue;
				}
				/* PackBits mill. Dest RTS with $FE60. */
				if (r24 - 2u >= ROMBase + 0x1e796u &&
				    r24 - 2u < ROMBase + 0x1e874u) {
					gpr(8) = 0xfffffe60u;
					g3_ccr = 8;
					gpr(24) = ROMBase + 0x1e874u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned npk;
						if (npk < 8) {
							npk++;
							nw_boot_log("G3: 68k skip PackBits 0x1e874");
						}
					}
#endif
					continue;
				}
				/* FCB/QD walk. Dest 2f30 thunk. Keep 0x1e9d0. */
				if (r24 - 2u >= ROMBase + 0x1e8beu &&
				    r24 - 2u < ROMBase + 0x1e912u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x1e912u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nfcbw;
						if (nfcbw < 8) {
							nfcbw++;
							nw_boot_log("G3: 68k skip FCB walk 0x1e912");
						}
					}
#endif
					continue;
				}
				/* CopyBits A3=0 helper + 2f38 $0740 JMP blit.
				 * Dest 0x20c9a. Not 0x20ca4. */
				if (r24 - 2u >= ROMBase + 0x20c82u &&
				    r24 - 2u < ROMBase + 0x20c9au) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x20c9au;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ncb0;
						if (ncb0 < 8) {
							ncb0++;
							nw_boot_log("G3: 68k skip CopyBits A3=0 0x20c9a");
						}
					}
#endif
					continue;
				}
				/* CopyBits tail + 2f38 $0744/$0748 JMP blit.
				 * Dest 0x20ffe. Keep 0x20cc4. */
				if (r24 - 2u >= ROMBase + 0x20cc6u &&
				    r24 - 2u < ROMBase + 0x20ffeu) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x20ffeu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ncbt;
						if (ncbt < 8) {
							ncbt++;
							nw_boot_log("G3: 68k skip CopyBits tail 0x20ffe");
						}
					}
#endif
					continue;
				}
				/* CFM InitRoutineDescriptor / 'pwpc'. */
				if (r24 - 2u >= ROMBase + 0xf310u &&
				    r24 - 2u < ROMBase + 0xf600u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0xf600u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ncfmrd;
						if (ncfmrd < 8) {
							ncfmrd++;
							nw_boot_log("G3: 68k skip CFM pwpc 0xf600");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0xf602u &&
				    r24 - 2u < ROMBase + 0xf620u) {
					gpr(24) = ROMBase + 0xf620u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0xfc10u &&
				    r24 - 2u < ROMBase + 0xfc34u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0xfc34u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ncfja4;
						if (ncfja4 < 8) {
							ncfja4++;
							nw_boot_log("G3: 68k skip CFM jsrA4 0xfc34");
						}
					}
#endif
					continue;
				}
				/* FileMgr glue: pop A1/A0, A021/A025, JMP (A1). */
				if (r24 - 2u == ROMBase + 0xa8060u ||
				    r24 - 2u == ROMBase + 0xa8070u) {
					const uint32 sp = gpr(1);
					uint32 a1 = g3_rom0(vm_read_memory_4(sp));
					uint32 a0 = g3_rom0(vm_read_memory_4(sp + 4u));
					gpr(1) = sp + 8u;
					gpr(17) = a1;
					gpr(16) = a0;
					gpr(8) = 0;
					g3_ccr = 4;
					if (g3_ea_data(a0) && a0 >= 0x20000u)
						vm_write_memory_4(a0, 0);
					if (!g3_r24_ok(a1) || a1 == r24 - 2u)
						a1 = ROMBase + 0xa8080u;
					gpr(24) = a1;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ngl;
						if (ngl < 8) {
							ngl++;
							nw_boot_log("G3: 68k glue A025");
						}
					}
#endif
					continue;
				}
				/* GetCCursor BEQ nil. Dest 0x5c87e before
				 * AA68. Old dest 0x5c896 swallowed it. */
				if (r24 - 2u >= ROMBase + 0x5c872u &&
				    r24 - 2u < ROMBase + 0x5c87eu) {
					g3_ccr &= ~4u;
					gpr(24) = ROMBase + 0x5c87eu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ngcbeq;
						if (ngcbeq < 8) {
							ngcbeq++;
							nw_boot_log("G3: 68k skip GetCCursor to AA68 0x5c87e");
						}
					}
#endif
					continue;
				}
				/* A991 CMP/BNE retry KEEP 0x5c89e. */
				if (r24 - 2u >= ROMBase + 0x7684u &&
				    r24 - 2u < ROMBase + 0x76b0u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x76b0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n60ff76;
						if (n60ff76 < 8) {
							n60ff76++;
							nw_boot_log("G3: 68k skip 60ff pad 0x76b0");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x7aecu &&
				    r24 - 2u < ROMBase + 0x7af8u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x7af8u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n60ff7a;
						if (n60ff7a < 8) {
							n60ff7a++;
							nw_boot_log("G3: 68k skip 60ff pad 0x7af8");
						}
					}
#endif
					continue;
				}
				/* CMPI.W #6,4(A0) helper + MOVEQ #-2 mill.
				 * Dest 0x16d78 RTS. */
				if (r24 - 2u >= ROMBase + 0x16d30u &&
				    r24 - 2u < ROMBase + 0x16d78u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x16d78u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ncmp6;
						if (ncmp6 < 8) {
							ncmp6++;
							nw_boot_log("G3: 68k skip CMPI #6 mill 0x16d30");
						}
					}
#endif
					continue;
				}
				/* FCBSPtr $0162 zone/FCB list walk. Empty
				 * table is A1=0 BEQ RTS at 0x2bffa. */
				if (r24 - 2u >= ROMBase + 0x2bfe2u &&
				    r24 - 2u < ROMBase + 0x2bffau) {
					gpr(24) = ROMBase + 0x2bffau;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nfcbs;
						if (nfcbs < 4) {
							nfcbs++;
							nw_boot_log("G3: 68k skip FCBSPtr walk 0x2bfe2");
						}
					}
#endif
					continue;
				}
				/* SlotManager sResource walk + bitstream.
				 * A06E stub used to return noErr so the
				 * search never ended. Dest 0x1735a RTS. */
				if (r24 - 2u >= ROMBase + 0x170f0u &&
				    r24 - 2u < ROMBase + 0x1735au) {
					gpr(24) = ROMBase + 0x1735au;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nck2;
						if (nck2 < 4) {
							nck2++;
							nw_boot_log("G3: 68k skip SlotMgr walk 0x170f0");
						}
					}
#endif
					continue;
				}
				/* Reloc compare-wait at 0x650 BEQ *. Dest 0x672 RTS. */
				if (r24 - 2u >= ROMBase + 0x648u &&
				    r24 - 2u < ROMBase + 0x672u) {
					gpr(24) = ROMBase + 0x672u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nrel;
						if (nrel < 4) {
							nrel++;
							nw_boot_log("G3: 68k skip reloc wait 0x648");
						}
					}
#endif
					continue;
				}
				/* VIA/SCC DBF waits + JMP (A2) + the 0x5f2
				 * fill that old dest 0x5e0 fell into with
				 * A1='hng'. Dest 0x5f8 is the fill RTS. */
				if (r24 - 2u >= ROMBase + 0x530u &&
				    r24 - 2u < ROMBase + 0x5f8u) {
					uint32 t = g3_rom0(gpr(18));
					if (!g3_r24_ok(t) ||
					    vm_read_memory_2(t) == 0 ||
					    (t >= ROMBase + 0x530u &&
					     t < ROMBase + 0x5f8u)) {
						const uint32 sp = gpr(1);
						uint32 dest =
							g3_rom0(vm_read_memory_4(sp));
						gpr(1) = sp + 4;
						if (g3_r24_ok(dest) &&
						    dest != r24 - 2u &&
						    !(dest >= ROMBase + 0x530u &&
						      dest < ROMBase + 0x5f8u))
							t = dest;
						else
							t = ROMBase + 0x5f8u;
					}
					gpr(24) = t;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nvia;
						if (nvia < 4) {
							nvia++;
							nw_boot_log("G3: 68k skip VIA wait 0x530");
						}
					}
#endif
					continue;
				}
				/* FCB/'thng' walker: A260 GetFCBInfo stub
				 * returns noErr so TST.W D0 never exits.
				 * Dest 0x4183a is the function RTS. */
				if (r24 - 2u >= ROMBase + 0x41790u &&
				    r24 - 2u < ROMBase + 0x4183au) {
					gpr(24) = ROMBase + 0x4183au;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nfcb;
						if (nfcb < 4) {
							nfcb++;
							nw_boot_log("G3: 68k skip FCB thng walk 0x41790");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x21850u &&
				    r24 - 2u < ROMBase + 0x218d4u) {
					gpr(24) = ROMBase + 0x218d4u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ntrapw;
						if (ntrapw < 4) {
							ntrapw++;
							nw_boot_log("G3: 68k skip GetTrapAddress poll");
						}
					}
#endif
					continue;
				}
				/* Number/token scanner at 0x4f5c. Empty
				 * A4 makes CMPI/BRA spin (0x4f72/0x4f8e).
				 * Dest 0x5078 RTS past 0x5004 abs.W tail. */
				if (r24 - 2u >= ROMBase + 0x4f52u &&
				    r24 - 2u < ROMBase + 0x5078u) {
					gpr(8) = gpr(12);
					gpr(9) = gpr(14);
					gpr(24) = ROMBase + 0x5078u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nscan;
						if (nscan < 4) {
							nscan++;
							nw_boot_log("G3: 68k skip token scanner 0x4f5c");
						}
					}
#endif
					continue;
				}
				/* SetOrigin digit helper; 6102 trampolines
				 * + A883 + BEQ.W wait. Keep 0x4e86 SWAP.
				 * Dest 0x4f50 RTS (not 0x4ec0 dest-edge). */
				if (r24 - 2u >= ROMBase + 0x4e88u &&
				    r24 - 2u < ROMBase + 0x4f50u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x4e88u &&
					     dest < ROMBase + 0x4f50u))
						dest = ROMBase + 0x4f50u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nso;
						if (nso < 4) {
							nso++;
							nw_boot_log("G3: 68k skip SetOrigin mill 0x4f50");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x4ec8u &&
				    r24 - 2u < ROMBase + 0x4ee0u) {
					gpr(24) = ROMBase + 0x4ee0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nfill;
						if (nfill < 4) {
							nfill++;
							nw_boot_log("G3: 68k skip byte fill 0x4ed2");
						}
					}
#endif
					continue;
				}
				/* 0x4e16 BSR scanner + 0x4e24 LEA 20(A7)
				 * then BRA 0x4ab8 epilogue. Dest 0x4e24
				 * spun on 4fef (LEA A7 wrote gpr(23)).
				 * Dealloc 0x14+0x15a, pop $0A5A, RTS. */
				if (r24 - 2u >= ROMBase + 0x4e16u &&
				    r24 - 2u < ROMBase + 0x4e30u) {
					gpr(1) += 0x14u + 0x15au;
					if (g3_ea_data(gpr(1))) {
						uint16 v = vm_read_memory_2(gpr(1));
						gpr(1) += 2;
						vm_write_memory_2(0xa5au, v);
					}
					{
						const uint32 sp = gpr(1);
						uint32 dest = g3_rom0(vm_read_memory_4(sp));
						gpr(1) = sp + 4;
						if (!g3_r24_ok(dest) ||
						    dest == r24 - 2u ||
						    (dest >= ROMBase + 0x4e16u &&
						     dest < ROMBase + 0x4e30u))
							dest = g3_fix_r24(r24);
						gpr(24) = dest;
					}
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nscbsr;
						if (nscbsr < 8) {
							nscbsr++;
							nw_boot_log("G3: 68k skip scanner BSR 0x4e16");
						}
					}
#endif
					continue;
				}
				/* 0x4e230 LINK: 64-bit mul then Gestalt
				 * poll BNE.W * (ABEB returns nonzero).
				 * Dest 0x4e374 RTS. */
				if (r24 - 2u >= ROMBase + 0x4e050u &&
				    r24 - 2u < ROMBase + 0x4e374u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x4e374u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ngpol;
						if (ngpol < 8) {
							ngpol++;
							nw_boot_log("G3: 68k skip Gestalt poll 0x4e050");
						}
					}
#endif
					continue;
				}
				/* InvertRect trampoline + DrawChar banner.
				 * Dest of the old 0x4e44 skip was DBF D6/D4
				 * around A893; DBcc wait cap resets on BSR.
				 * Dest 0x4e86 SWAP (not dest-RTS 0x4e84). */
				if (r24 - 2u >= ROMBase + 0x4e32u &&
				    r24 - 2u < ROMBase + 0x4e86u) {
					gpr(24) = ROMBase + 0x4e86u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ninv;
						if (ninv < 8) {
							ninv++;
							nw_boot_log("G3: 68k skip EraseRect JMP 0x4e86");
						}
					}
#endif
					continue;
				}
				/* Banner DrawChar/SetPort/GetTrapAddress
				 * loop. BRA.W 0x4d1c at 0x4df0 plus skip
				 * dest 0x4d12 from post-InitGraf. Dest
				 * 0x4e16 is BSR scanner after packed names.
				 * Hole dest 0x4d4a milled bits 8e7a0. */
				if (r24 - 2u >= ROMBase + 0x4d12u &&
				    r24 - 2u < ROMBase + 0x4e16u) {
					gpr(24) = ROMBase + 0x4e374u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nban;
						if (nban < 8) {
							nban++;
							nw_boot_log("G3: 68k skip banner loop 0x4d12");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x4d2eu &&
				    r24 - 2u < ROMBase + 0x4d3au) {
					gpr(24) = ROMBase + 0x4d3au;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x4dcau &&
				    r24 - 2u < ROMBase + 0x4e16u) {
					gpr(24) = ROMBase + 0x4e16u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x4d4au &&
				    r24 - 2u < ROMBase + 0x4dbeu) {
					gpr(24) = ROMBase + 0x4dbeu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ntrap2;
						if (ntrap2 < 4) {
							ntrap2++;
							nw_boot_log("G3: 68k skip GetTrapAddress poll 0x4d4a");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x4a24u &&
				    r24 - 2u < ROMBase + 0x4e16u) {
					gpr(24) = ROMBase + 0x4e374u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned npostg;
						if (npostg < 4) {
							npostg++;
							nw_boot_log("G3: 68k skip post-InitGraf 0x4a24");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x49eeu &&
				    r24 - 2u < ROMBase + 0x49f6u) {
					gpr(24) = ROMBase + 0x49f6u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nhalt2;
						if (nhalt2 < 4) {
							nhalt2++;
							nw_boot_log("G3: 68k skip BRA * 0x49f0");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x4b2cu &&
				    r24 - 2u < ROMBase + 0x4b54u) {
					gpr(24) = ROMBase + 0x4b54u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nbsrck;
						if (nbsrck < 4) {
							nbsrck++;
							nw_boot_log("G3: 68k skip BSR checksum 0x4b2e");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x4b54u &&
				    r24 - 2u < ROMBase + 0x4ce4u) {
					gpr(24) = ROMBase + 0x4ce4u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nbeq;
						if (nbeq < 4) {
							nbeq++;
							nw_boot_log("G3: 68k skip QD 0x4b54");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x4c9eu &&
				    r24 - 2u < ROMBase + 0x4ca4u) {
					gpr(24) = ROMBase + 0x4ca4u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n172;
						if (n172 < 4) {
							n172++;
							nw_boot_log("G3: 68k skip $0172 wait");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x4c5eu &&
				    r24 - 2u < ROMBase + 0x4c88u) {
					gpr(24) = ROMBase + 0x4c88u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nrect;
						if (nrect < 4) {
							nrect++;
							nw_boot_log("G3: 68k skip D3 rect walk 0x4c86");
						}
					}
#endif
					continue;
				}
				/* Pascal names after RTS 0x4248 plus
				 * StartLib mill + dest-edge VBR DBF.
				 * Dest 0x48b0. Keep 0x4248. */
				if (r24 - 2u >= ROMBase + 0x424au &&
				    r24 - 2u < ROMBase + 0x48b0u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x424au &&
					     dest < ROMBase + 0x48b0u))
						dest = ROMBase + 0x48b0u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nupk;
						if (nupk < 8) {
							nupk++;
							nw_boot_log("G3: 68k skip StartLib dest 0x48b0");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x4ac8u &&
				    r24 - 2u < ROMBase + 0x4b1au) {
					gpr(8) = 0;
					gpr(24) = ROMBase + 0x4b1au;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nck2;
						if (nck2 < 4) {
							nck2++;
							nw_boot_log("G3: 68k skip checksum 0x4ac8");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1cdc8u &&
				    r24 - 2u < ROMBase + 0x1cf1au) {
					gpr(24) = ROMBase + 0x1cf1au;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nslot;
						if (nslot < 4) {
							nslot++;
							nw_boot_log("G3: 68k skip slot walk 0x1ce00");
						}
					}
#endif
					continue;
				}
				/* Slot/sResource iterator: JSR (A1)=0 then
				 * TST/BGT 0x1cfd2 never ends. Dest next
				 * LINK 0x1d050. */
				/* SlotMgr + Gestalt('gama') BRA 0xf32.
				 * Dest 0xf44 after the self-BRA. */
				if (r24 - 2u >= ROMBase + 0xe46u &&
				    r24 - 2u < ROMBase + 0xf44u) {
					gpr(24) = ROMBase + 0xf44u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ngama;
						if (ngama < 8) {
							ngama++;
							nw_boot_log("G3: 68k skip gama/slot 0xe46");
						}
					}
#endif
					continue;
				}
				/* MP/table walk + 2f30 dest-edge mill.
				 * Dest 0xa08d0 after fill-loop RTS. */
				if (r24 - 2u >= ROMBase + 0x9fd9au &&
				    r24 - 2u < ROMBase + 0xa08d0u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x9fd9au &&
					     dest < ROMBase + 0xa08d0u))
						dest = ROMBase + 0xa08d0u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmpw;
						if (nmpw < 8) {
							nmpw++;
							nw_boot_log("G3: 68k skip A6 loop 0xa08d0");
						}
					}
#endif
					continue;
				}
				/* Pascal "Unknown MPDispatch selector:"
				 * executed as 68k + JMP (A0) glue.
				 * Dest next real MOVE.L 0xded0. */
				if (r24 - 2u >= ROMBase + 0xde10u &&
				    r24 - 2u < ROMBase + 0xded0u) {
					gpr(24) = ROMBase + 0xded0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmpd;
						if (nmpd < 8) {
							nmpd++;
							nw_boot_log("G3: 68k skip MPDispatch str 0xde10");
						}
					}
#endif
					continue;
				}
				/* HFS GetFPos/SetFPos inner A-lines.
				 * Dest 0x63100 RTS. */
				if (r24 - 2u >= ROMBase + 0x63048u &&
				    r24 - 2u < ROMBase + 0x63100u) {
					gpr(24) = ROMBase + 0x63100u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nhfsp;
						if (nhfsp < 8) {
							nhfsp++;
							nw_boot_log("G3: 68k skip HFS pos 0x63048");
						}
					}
#endif
					continue;
				}
				/* VMVectors $0CF0 + JSR (A0) self mill
				 * at 0xec18 + MOVE.W SR mill + offset
				 * table + JMP (A0). Dest 0xf170. */
				if (r24 - 2u >= ROMBase + 0xeac8u &&
				    r24 - 2u < ROMBase + 0xf170u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0xeac8u &&
					     dest < ROMBase + 0xf170u))
						dest = ROMBase + 0xf170u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nvmv;
						if (nvmv < 8) {
							nvmv++;
							nw_boot_log("G3: 68k skip VMVectors 0xeac8");
						}
					}
#endif
					continue;
				}
				/* USB Family Expert Lib GetResource
				 * probe: BEQ 0x1bb8 -> 0x1986. Dest RTS. */
				if (r24 - 2u >= ROMBase + 0x194cu &&
				    r24 - 2u < ROMBase + 0x1c80u) {
					gpr(24) = ROMBase + 0x1c80u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nusb;
						if (nusb < 8) {
							nusb++;
							nw_boot_log("G3: 68k skip USBExpert 0x1960");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1888au &&
				    r24 - 2u < ROMBase + 0x188dau) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x188dau;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n2frts;
						if (n2frts < 8) {
							n2frts++;
							nw_boot_log("G3: 68k skip 2f30 dest-RTS 0x188da");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x188dau &&
				    r24 - 2u < ROMBase + 0x18b60u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x188dau &&
					     dest < ROMBase + 0x18b60u))
						dest = ROMBase + 0x18b60u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n2f18;
						if (n2f18 < 8) {
							n2f18++;
							nw_boot_log("G3: 68k skip MM 2f30 0x188da");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x186feu &&
				    r24 - 2u < ROMBase + 0x18708u) {
					gpr(24) = ROMBase + 0x18708u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nrorw;
						if (nrorw < 8) {
							nrorw++;
							nw_boot_log("G3: 68k skip ROR/BEQ wait 0x18708");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1a7d2u &&
				    r24 - 2u < ROMBase + 0x1a7f0u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x1a7f0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nclrbf;
						if (nclrbf < 8) {
							nclrbf++;
							nw_boot_log("G3: 68k skip CLR.B DBF 0x1a7f0");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x129cu &&
				    r24 - 2u < ROMBase + 0x12c0u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x129cu &&
					     dest < ROMBase + 0x12c0u))
						dest = ROMBase + 0x12c0u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n60a;
						if (n60a < 8) {
							n60a++;
							nw_boot_log("G3: 68k skip 60ff pad 0x129c");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1478u &&
				    r24 - 2u < ROMBase + 0x14c0u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x1478u &&
					     dest < ROMBase + 0x14c0u))
						dest = ROMBase + 0x14c0u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n60b;
						if (n60b < 8) {
							n60b++;
							nw_boot_log("G3: 68k skip 60ff pad 0x1478");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1650u &&
				    r24 - 2u < ROMBase + 0x1670u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x1650u &&
					     dest < ROMBase + 0x1670u))
						dest = ROMBase + 0x1670u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n60c;
						if (n60c < 8) {
							n60c++;
							nw_boot_log("G3: 68k skip 60ff pad 0x1650");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0xffeu &&
				    r24 - 2u < ROMBase + 0x1040u) {
					gpr(24) = ROMBase + 0x1040u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nlist;
						if (nlist < 8) {
							nlist++;
							nw_boot_log("G3: 68k skip list walk 0xffe");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x28922u &&
				    r24 - 2u < ROMBase + 0x28976u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x28976u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ndbeq;
						if (ndbeq < 8) {
							ndbeq++;
							nw_boot_log("G3: 68k skip DBEQ wait 0x28976");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x289dcu &&
				    r24 - 2u < ROMBase + 0x28a04u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x28a04u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ncmpa;
						if (ncmpa < 8) {
							ncmpa++;
							nw_boot_log("G3: 68k skip CMP $0A06 0x28a04");
						}
					}
#endif
					continue;
				}
				/* HFS GetVolInfo body + GetPC trampoline.
				 * Keep 0x304be LINK. Dest 0x305c0 LEA. */
				if (r24 - 2u >= ROMBase + 0x304c0u &&
				    r24 - 2u < ROMBase + 0x305c0u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x305c0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nhfs2;
						if (nhfs2 < 8) {
							nhfs2++;
							nw_boot_log("G3: 68k skip HFS vol 0x305c0");
						}
					}
#endif
					continue;
				}
				/* HFS catalog walk: CMP.W (A0)+ / BHI and
				 * _Open A9E0 / BCS 0x31066. Dest next
				 * LINK 0x311e0. */
				if (r24 - 2u >= ROMBase + 0x30e56u &&
				    r24 - 2u < ROMBase + 0x311e0u) {
					gpr(24) = ROMBase + 0x311e0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nhfs;
						if (nhfs < 8) {
							nhfs++;
							nw_boot_log("G3: 68k skip HFS walk 0x30e56");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1cf20u &&
				    r24 - 2u < ROMBase + 0x1dd38u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x1cf20u &&
					     dest < ROMBase + 0x1dd38u))
						dest = ROMBase + 0x1dd38u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nslot2;
						if (nslot2 < 8) {
							nslot2++;
							nw_boot_log("G3: 68k skip slot iter dest 0x1dd38");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0xa15eu &&
				    r24 - 2u < ROMBase + 0xa1a6u) {
					gpr(24) = ROMBase + 0xa1a6u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nready;
						if (nready < 4) {
							nready++;
							nw_boot_log("G3: 68k skip $015D ready wait");
						}
					}
#endif
					continue;
				}
				/* FCB/map getter: MOVEA.L $0A1C then
				 * TST nil / _HOpenResFile / BSR self.
				 * Dest synthetic RTS. */
				/* 'ndrv' Gestalt + GetResource. BRA 0x61004
				 * retries. Dest 0x61080 next LINK. */
				if (r24 - 2u >= ROMBase + 0x60f80u &&
				    r24 - 2u < ROMBase + 0x61080u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x60f80u &&
					     dest < ROMBase + 0x61080u))
						dest = ROMBase + 0x61080u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nndrv;
						if (nndrv < 8) {
							nndrv++;
							nw_boot_log("G3: 68k skip ndrv 0x61080");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x5ae76u &&
				    r24 - 2u < ROMBase + 0x5be10u &&
				    !(r24 - 2u >= ROMBase + 0x5b110u &&
				      r24 - 2u < ROMBase + 0x5b166u)) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(16) = RAMBase + 0xd100u;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x5ae76u &&
					     dest < ROMBase + 0x5be10u &&
					     !(dest >= ROMBase + 0x5b110u &&
					       dest < ROMBase + 0x5b166u)))
						dest = ROMBase + 0x5be10u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nfcb;
						if (nfcb < 8) {
							nfcb++;
							nw_boot_log("G3: 68k skip FCB ptr 0x5be10");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x19cf62u &&
				    r24 - 2u < ROMBase + 0x1d2000u) {
					gpr(24) = ROMBase + 0x1da00u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nppc2;
						if (nppc2 < 8) {
							nppc2++;
							nw_boot_log("G3: 68k skip PPC lib dest 0x1da00");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0xc0000u &&
				    r24 - 2u < ROMBase + 0x198154u) {
					gpr(24) = ROMBase + 0x198154u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nppc;
						if (nppc < 8) {
							nppc++;
							nw_boot_log("G3: 68k skip PPC lib 0xc0000");
						}
					}
#endif
					continue;
				}
				/* ndrv 'ndrv'/media-bay names + TimeDBRA
				 * DBF mill. Dest 0x1d2000 ROM blob skip.
				 * Keep 0x1d1b20. */
				if (r24 - 2u >= ROMBase + 0x1d1b20u &&
				    r24 - 2u < ROMBase + 0x1d2000u) {
					gpr(24) = ROMBase + 0x1d2000u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ndbf2;
						if (ndbf2 < 8) {
							ndbf2++;
							nw_boot_log("G3: 68k skip ndrv mill 0x1d1b20");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1d2000u &&
				    r24 - 2u < ROMBase + 0x400000u) {
					gpr(24) = ROMBase + 0x1da00u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nres;
						if (nres < 4) {
							nres++;
							nw_boot_log("G3: 68k skip ROM resource blob");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0xa8c66u &&
				    r24 - 2u < ROMBase + 0xb77b4u) {
					gpr(24) = ROMBase + 0xb77b4u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nkchr;
						if (nkchr < 4) {
							nkchr++;
							nw_boot_log("G3: 68k skip KCHR/keymap 0xaec00");
						}
					}
#endif
					continue;
				}
				/* 64-bit restoring divide at 0xb7c32.
				 * D0 bits of ADDX/CMP/SUB; dest 0xb7c4e RTS. */
				if (r24 - 2u >= ROMBase + 0xb7c32u &&
				    r24 - 2u < ROMBase + 0xb7c4eu) {
					uint32 d0 = gpr(8) & 0xffffu;
					uint32 d1 = gpr(9);
					uint32 d2 = gpr(10);
					uint32 d3 = gpr(11);
					uint32 d4 = gpr(12);
					uint32 d5 = gpr(13);
					uint32 a2 = gpr(18);
					if (d0 > 64u)
						d0 = 64u;
					while (d0) {
						uint32 x = d5 >> 31;
						d5 <<= 1;
						{
							uint32 x2 = d4 >> 31;
							d4 = (d4 << 1) | x;
							x = d2 >> 31;
							d2 = (d2 << 1) | x2;
						}
						{
							uint32 x3 = d1 >> 31;
							d1 = (d1 << 1) | x;
							int ge;
							if (x3)
								ge = 1;
							else if (d2 != d3)
								ge = !(d2 < d3);
							else
								ge = !(d4 < a2);
							if (ge) {
								d5 += 1u;
								d4 -= a2;
								d2 -= d3;
							}
						}
						d0--;
					}
					gpr(8) = d0;
					gpr(9) = d1;
					gpr(10) = d2;
					gpr(11) = d3;
					gpr(12) = d4;
					gpr(13) = d5;
					gpr(24) = ROMBase + 0xb7c4eu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ndiv;
						if (ndiv < 8) {
							ndiv++;
							nw_boot_log("G3: 68k host 64b div 0xb7c32");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0xb7802u &&
				    r24 - 2u < ROMBase + 0xb781au) {
					gpr(24) = ROMBase + 0xb781au;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				/* JMP (d8,PC,Xn) switch. Dest 0x2c2d2 RTS
				 * was inside the ext/table reject; synthetic
				 * RTS instead. */
				if (r24 - 2u >= ROMBase + 0x2c2beu &&
				    r24 - 2u < ROMBase + 0x2c2d4u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x2c2beu &&
					     dest < ROMBase + 0x2c2d4u))
						dest = g3_fix_r24(r24);
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned njt;
						if (njt < 4) {
							njt++;
							nw_boot_log("G3: 68k skip JMP table 0x2c2be");
						}
					}
#endif
					continue;
				}
				/* SANE dispatcher from 0xa940. Dest 0xaf3e
				 * RTS; A6 was 0xa96c inside the body. */
				if (r24 - 2u >= ROMBase + 0xa940u &&
				    r24 - 2u < ROMBase + 0xaf3eu) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0xa940u &&
					     dest < ROMBase + 0xaf3eu))
						dest = ROMBase + 0xaf3eu;
					g3_ccr = 4;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nsaned;
						if (nsaned < 8) {
							nsaned++;
							nw_boot_log("G3: 68k skip SANE disp 0xa98c");
						}
					}
#endif
					continue;
				}
				/* MixedMode/CFM UNLK+RTD glue. Dest next
				 * LINK 0x41100. */
				if (r24 - 2u >= ROMBase + 0x41060u &&
				    r24 - 2u < ROMBase + 0x41100u) {
					gpr(24) = ROMBase + 0x41100u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmm;
						if (nmm < 8) {
							nmm++;
							nw_boot_log("G3: 68k skip MM glue 0x41060");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x40c80u &&
				    r24 - 2u < ROMBase + 0x40ce8u) {
					gpr(24) = ROMBase + 0x40ce8u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmm2;
						if (nmm2 < 8) {
							nmm2++;
							nw_boot_log("G3: 68k skip MM glue 0x40c80");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x2c1a8u &&
				    r24 - 2u < ROMBase + 0x2c1c6u) {
					gpr(24) = ROMBase + 0x2c1c6u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nclr;
						if (nclr < 8) {
							nclr++;
							nw_boot_log("G3: 68k skip MOVEQ block 0x2c1a8");
						}
					}
#endif
					continue;
				}
				/* JMP (A5) SCSI/selector loop: 4EBB then
				 * 4ED5 with A5=0. Dest after BNE 0xd07a. */
				if (r24 - 2u >= ROMBase + 0xd0d8u &&
				    r24 - 2u < ROMBase + 0xd430u) {
					gpr(8) = 0xffffffceu;
					gpr(24) = ROMBase + 0xd430u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nscsi;
						if (nscsi < 8) {
							nscsi++;
							nw_boot_log("G3: 68k skip SCSI map 0xd0d8");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0xd04eu &&
				    r24 - 2u < ROMBase + 0xd094u) {
					gpr(24) = ROMBase + 0xd094u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nj5;
						if (nj5 < 8) {
							nj5++;
							nw_boot_log("G3: 68k skip JMP (A5) 0xd04e");
						}
					}
#endif
					continue;
				}
				/* Field thunks at 0x25fc0. Dest 0x26006. */
				if (r24 - 2u >= ROMBase + 0x25fc0u &&
				    r24 - 2u < ROMBase + 0x26006u) {
					gpr(24) = ROMBase + 0x26006u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nthunk;
						if (nthunk < 8) {
							nthunk++;
							nw_boot_log("G3: 68k skip thunks 0x25fc0");
						}
					}
#endif
					continue;
				}
				/* c2pstr / uncompress_world / relocate_world
				 * 68k glue after ExecMgr dest. Dest 0x19cf62
				 * PPC-lib skip. */
				if (r24 - 2u >= ROMBase + 0x19b806u &&
				    r24 - 2u < ROMBase + 0x19cf62u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x19cf62u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nunw;
						if (nunw < 8) {
							nunw++;
							nw_boot_log("G3: 68k skip uncompress_world 0x19b806");
						}
					}
#endif
					continue;
				}
				/* ExecMgr/GetVolume/FindEmptyRefNum plus
				 * DRVR InitiateIO/SetupIO mill. Dest
				 * 0x1d7aa RTS after slot dest-edge. */
				if (r24 - 2u >= ROMBase + 0x198154u &&
				    r24 - 2u < ROMBase + 0x19b806u) {
					gpr(24) = ROMBase + 0x1dd38u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nexec;
						if (nexec < 8) {
							nexec++;
							nw_boot_log("G3: 68k skip ExecMgr dest 0x1dd38");
						}
					}
#endif
					continue;
				}
				/* SANE helpers through PACK/INF. Dest
				 * 0x198154 after PPC libraries. */
				if (r24 - 2u >= ROMBase + 0xb77b4u &&
				    r24 - 2u < ROMBase + 0xc0000u) {
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x198154u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nsane;
						if (nsane < 8) {
							nsane++;
							nw_boot_log("G3: 68k skip SANE helper 0xb77b4");
						}
					}
#endif
					continue;
				}
				/* SANE normalize + mul (now covered by
				 * 0xb7d16 skip). Keep for dest 0x198154. */
				if (r24 - 2u >= ROMBase + 0xb8254u &&
				    r24 - 2u < ROMBase + 0xc0000u) {
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x198154u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nnorm;
						if (nnorm < 8) {
							nnorm++;
							nw_boot_log("G3: 68k skip SANE norm 0xb8254");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x5080u &&
				    r24 - 2u < ROMBase + 0x6c94u) {
					gpr(24) = ROMBase + 0x6c94u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned noff;
						if (noff < 4) {
							noff++;
							nw_boot_log("G3: 68k skip offset tables 0x5080");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x4fa0u &&
				    r24 - 2u < ROMBase + 0x5080u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u)
						dest = g3_fix_r24(ROMBase + 0x2d47eu);
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nck;
						if (nck < 4) {
							nck++;
							nw_boot_log("G3: 68k skip checksum");
						}
					}
#endif
					continue;
				}
				/* 24-bit / NuBus decode at 0xdf4c; caller
				 * BSR 0xdf92 loops. Dest 0xdfa8 RTS. */
				if (r24 - 2u >= ROMBase + 0xdf4cu &&
				    r24 - 2u < ROMBase + 0xdfa8u) {
					gpr(24) = ROMBase + 0xdfa8u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n24;
						if (n24 < 4) {
							n24++;
							nw_boot_log("G3: 68k skip 24bit decode 0xdf4c");
						}
					}
#endif
					continue;
				}
				/* CFM LOAD/rmve walker. Pascal RTD #4. */
				if (r24 - 2u >= ROMBase + 0x19c1a4u &&
				    r24 - 2u < ROMBase + 0x19c2a2u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 8;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x19c1a4u &&
					     dest < ROMBase + 0x19c2a2u))
						dest = g3_fix_r24(ROMBase + 0x2d47eu);
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ncfm;
						if (ncfm < 4) {
							ncfm++;
							nw_boot_log("G3: 68k skip CFM LOAD 0x19c1a4");
						}
					}
#endif
					continue;
				}
				/* TST.B (A0); DBF * at UnitNtryCnt. Dest 0x19814e. */
				if (r24 - 2u >= ROMBase + 0x198140u &&
				    r24 - 2u < ROMBase + 0x19814eu) {
					gpr(24) = ROMBase + 0x19814eu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				/* ExpandMem method-list walk: JSR (A1)
				 * with poison A4. Dest 0x9a94 RTS. */
				if (r24 - 2u >= ROMBase + 0x9a5eu &&
				    r24 - 2u < ROMBase + 0x9ab4u) {
					gpr(24) = ROMBase + 0x9ab4u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nem;
						if (nem < 4) {
							nem++;
							nw_boot_log("G3: 68k skip EM list 0x9a5e");
						}
					}
#endif
					continue;
				}
				/* SCC/VIA BTST+DBF * waits. Completion is
				 * JMP (A5); else dest 0x9800 LEA. */
				if (r24 - 2u >= ROMBase + 0x96d0u &&
				    r24 - 2u < ROMBase + 0x9800u) {
					uint32 t = g3_rom0(gpr(21));
					if (!g3_r24_ok(t) ||
					    vm_read_memory_2(t) == 0 ||
					    (t >= ROMBase + 0x96d0u &&
					     t < ROMBase + 0x9800u))
						t = ROMBase + 0x9800u;
					gpr(24) = t;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nscc;
						if (nscc < 4) {
							nscc++;
							nw_boot_log("G3: 68k skip SCC wait 0x96d0");
						}
					}
#endif
					continue;
				}
				/* GetTrapAddress glue JMP (A1) with
				 * A1 garbage. Synthetic RTS. */
				if (r24 - 2u >= ROMBase + 0xa8060u &&
				    r24 - 2u < ROMBase + 0xa8130u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0xa8060u &&
					     dest < ROMBase + 0xa8130u))
						dest = g3_fix_r24(r24);
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ngt;
						if (ngt < 8) {
							ngt++;
							nw_boot_log("G3: 68k skip GetTrap JMP 0xa80f0");
						}
					}
#endif
					continue;
				}
				/* 'pwpc' Gestalt/CFM probe. Dest RTS. */
				if (r24 - 2u >= ROMBase + 0xf7c0u &&
				    r24 - 2u < ROMBase + 0xf868u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0xf868u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned npwpc;
						if (npwpc < 8) {
							npwpc++;
							nw_boot_log("G3: 68k skip pwpc gestalt 0xf7c0");
						}
					}
#endif
					continue;
				}
				/* _Read backlight helper. Dest 0x2cb84 RTS. */
				if (r24 - 2u >= ROMBase + 0x2cb50u &&
				    r24 - 2u < ROMBase + 0x2cb84u) {
					gpr(24) = ROMBase + 0x2cb84u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nrd;
						if (nrd < 4) {
							nrd++;
							nw_boot_log("G3: 68k skip Read helper 0x2cb50");
						}
					}
#endif
					continue;
				}
				/* UTable GetTrapAddress thunk. JMP (A0)
				 * if A0 ok, else synthetic RTS. */
				if (r24 - 2u >= ROMBase + 0xa8170u &&
				    r24 - 2u < ROMBase + 0xa818au) {
					uint32 t = g3_rom0(gpr(16));
					if (!g3_r24_ok(t) ||
					    vm_read_memory_2(t) == 0) {
						const uint32 sp = gpr(1);
						uint32 dest =
							g3_rom0(vm_read_memory_4(sp));
						gpr(1) = sp + 4;
						if (g3_r24_ok(dest) &&
						    dest != r24 - 2u)
							t = dest;
						else
							t = g3_fix_r24(r24);
					}
					gpr(24) = t;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nut;
						if (nut < 4) {
							nut++;
							nw_boot_log("G3: 68k skip UTable thunk 0xa8170");
						}
					}
#endif
					continue;
				}
				/* 'scrn' GetResource + ADDQ D7 BGT.
				 * Dest 0x4c3d4 RTS. */
				if (r24 - 2u >= ROMBase + 0x4c1e0u &&
				    r24 - 2u < ROMBase + 0x4c3d4u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x4c3d4u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nscrn;
						if (nscrn < 8) {
							nscrn++;
							nw_boot_log("G3: 68k skip scrn rsrc 0x4c1e0");
						}
					}
#endif
					continue;
				}
				/* AddResource mill after scrn dest RTS
				 * through dest-edge LINK cluster. Dest
				 * 0x4f7c0. Keep 0x4c3e0. */
				if (r24 - 2u >= ROMBase + 0x4c3e0u &&
				    r24 - 2u < ROMBase + 0x4f7c0u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x4c3e0u &&
					     dest < ROMBase + 0x4f7c0u))
						dest = ROMBase + 0x4f7c0u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned naddres;
						if (naddres < 8) {
							naddres++;
							nw_boot_log("G3: 68k skip AddRes mill 0x4f7c0");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x4fad0u &&
				    r24 - 2u < ROMBase + 0x4ffe0u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x4ffe0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nemg;
						if (nemg < 8) {
							nemg++;
							nw_boot_log("G3: 68k skip ExpandMem Gestalt 0x4ffe0");
						}
					}
#endif
					continue;
				}
				/* HFS $0DD5 BTST. Dest 0x4b1e8 RTS. Not
				 * 0x4c1d8 File Mgr body. */
				if (r24 - 2u >= ROMBase + 0x4b1d2u &&
				    r24 - 2u < ROMBase + 0x4b1e8u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x4b1e8u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nhfs3;
						if (nhfs3 < 8) {
							nhfs3++;
							nw_boot_log("G3: 68k skip HFS IO 0x4b1e8");
						}
					}
#endif
					continue;
				}
				/* Packed ffff slot offsets executed as
				 * F-line. Dest RTS 0xf168. */
				if (r24 - 2u >= ROMBase + 0xf018u &&
				    r24 - 2u < ROMBase + 0xf168u) {
					gpr(24) = ROMBase + 0xf168u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned npk2;
						if (npk2 < 8) {
							npk2++;
							nw_boot_log("G3: 68k skip packed off 0xf018");
						}
					}
#endif
					continue;
				}
				/* a069/a024 handle mill plus 64-bit
				 * shift/DBF and packed FP constants.
				 * Dest 0x2dd18 RTS past dest-edge JMP (A0). */
				if (r24 - 2u >= ROMBase + 0x2d480u &&
				    r24 - 2u < ROMBase + 0x2dd18u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x2dd18u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nhdl;
						if (nhdl < 8) {
							nhdl++;
							nw_boot_log("G3: 68k skip handle mill 0x2dd18");
						}
					}
#endif
					continue;
				}
				/* UTable $011C walk + DriverDescription
				 * mill through ndrv dest-RTS. Dest 0x61080. */
				if (r24 - 2u >= ROMBase + 0x5dcd0u &&
				    r24 - 2u < ROMBase + 0x61080u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x5dcd0u &&
					     dest < ROMBase + 0x61080u))
						dest = ROMBase + 0x61080u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nutw;
						if (nutw < 8) {
							nutw++;
							nw_boot_log("G3: 68k skip UTable ndrv 0x5dcd0");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1dd38u &&
				    r24 - 2u < ROMBase + 0x1ddb2u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x1dd38u &&
					     dest < ROMBase + 0x1ddb2u))
						dest = ROMBase + 0x1ddb2u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n1dd;
						if (n1dd < 8) {
							n1dd++;
							nw_boot_log("G3: 68k skip JMP idx 0x1dd38");
						}
					}
#endif
					continue;
				}
				/* ROM vers CMPI #$077d after EDisk RTS.
				 * Dest 0x2ca3e MOVEQ #0 + RTS. */
				if (r24 - 2u >= ROMBase + 0x2ca16u &&
				    r24 - 2u < ROMBase + 0x2ca3eu) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x2ca3eu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nromv;
						if (nromv < 8) {
							nromv++;
							nw_boot_log("G3: 68k skip ROM vers 0x2ca16");
						}
					}
#endif
					continue;
				}
				/* ExpandMem callback walker JSR (A0)
				 * plus aa2c helpers. Dest RTD 0x2cc38. */
				if (r24 - 2u >= ROMBase + 0x2cbb0u &&
				    r24 - 2u < ROMBase + 0x2cc38u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x2cc38u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nemb;
						if (nemb < 8) {
							nemb++;
							nw_boot_log("G3: 68k skip ExpandMem cb 0x2cbb0");
						}
					}
#endif
					continue;
				}
				/* SCSI/ejec helper after thunk table.
				 * Dest 0x2cb34 is the RTS. */
				if (r24 - 2u >= ROMBase + 0x2cab0u &&
				    r24 - 2u < ROMBase + 0x2cb34u) {
					gpr(24) = ROMBase + 0x2cb34u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nejec;
						if (nejec < 4) {
							nejec++;
							nw_boot_log("G3: 68k skip ejec helper 0x2cab0");
						}
					}
#endif
					continue;
				}
				/* ShutDown BRA * then FNDR/MACS OSDispatch
				 * wait. Dest 0xdc70 is the function RTS. */
				if (r24 - 2u >= ROMBase + 0xdbf0u &&
				    r24 - 2u < ROMBase + 0xdc70u) {
					gpr(24) = ROMBase + 0xdc70u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nfndr;
						if (nfndr < 4) {
							nfndr++;
							nw_boot_log("G3: 68k skip FNDR wait 0xdbf0");
						}
					}
#endif
					continue;
				}
				/* List-walk BNE + offset table 0xdb0c
				 * + JMP (d8,PC,Xn). Dest 0xdb56 RTS. */
				if (r24 - 2u >= ROMBase + 0xdadau &&
				    r24 - 2u < ROMBase + 0xdb56u) {
					gpr(24) = ROMBase + 0xdb56u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ndb;
						if (ndb < 4) {
							ndb++;
							nw_boot_log("G3: 68k skip JMP idx 0xdada");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x570ecu &&
				    r24 - 2u < ROMBase + 0x57100u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x570ecu &&
					     dest < ROMBase + 0x57100u))
						dest = ROMBase + 0x57100u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmm570;
						if (nmm570 < 8) {
							nmm570++;
							nw_boot_log("G3: 68k skip MM 2f30 0x570ec");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x5706eu &&
				    r24 - 2u < ROMBase + 0x571d0u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x5706eu &&
					     dest < ROMBase + 0x571d0u))
						dest = ROMBase + 0x571d0u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmm570b;
						if (nmm570b < 8) {
							nmm570b++;
							nw_boot_log("G3: 68k skip MM 2f30 0x5706e");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x58666u &&
				    r24 - 2u < ROMBase + 0x58670u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x58666u &&
					     dest < ROMBase + 0x58670u))
						dest = ROMBase + 0x58670u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmm586;
						if (nmm586 < 8) {
							nmm586++;
							nw_boot_log("G3: 68k skip MM 2f30 0x58666");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x46e8au &&
				    r24 - 2u < ROMBase + 0x46ea0u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x46e8au &&
					     dest < ROMBase + 0x46ea0u))
						dest = ROMBase + 0x46ea0u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmm46e;
						if (nmm46e < 8) {
							nmm46e++;
							nw_boot_log("G3: 68k skip MM 2f30 0x46e8a");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x46f12u &&
				    r24 - 2u < ROMBase + 0x46f20u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x46f12u &&
					     dest < ROMBase + 0x46f20u))
						dest = ROMBase + 0x46f20u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmm46f;
						if (nmm46f < 8) {
							nmm46f++;
							nw_boot_log("G3: 68k skip MM 2f30 0x46f12");
						}
					}
#endif
					continue;
				}
				/* MixedMode 2f30 + ADB init + dest-edge
				 * HideCursor BRA mill. Dest 0x57f10.
				 * Keep 0x57360. */
				if (r24 - 2u >= ROMBase + 0x572c6u &&
				    r24 - 2u < ROMBase + 0x58000u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x572c6u &&
					     dest < ROMBase + 0x58000u))
						dest = ROMBase + 0x58000u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmm57;
						if (nmm57 < 8) {
							nmm57++;
							nw_boot_log("G3: 68k skip MM ADB 0x58000");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x210e4u &&
				    r24 - 2u < ROMBase + 0x21130u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x210e4u &&
					     dest < ROMBase + 0x21130u))
						dest = ROMBase + 0x21130u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n60b;
						if (n60b < 8) {
							n60b++;
							nw_boot_log("G3: 68k skip 60ff pad 0x21100");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x21174u &&
				    r24 - 2u < ROMBase + 0x214dau) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x21174u &&
					     dest < ROMBase + 0x214dau))
						dest = ROMBase + 0x214dau;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmm4;
						if (nmm4 < 8) {
							nmm4++;
							nw_boot_log("G3: 68k skip MM thunk 0x214da");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x26612u &&
				    r24 - 2u < ROMBase + 0x26de0u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x26612u &&
					     dest < ROMBase + 0x26de0u))
						dest = ROMBase + 0x26de0u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nslot3;
						if (nslot3 < 8) {
							nslot3++;
							nw_boot_log("G3: 68k skip slot helper 0x26de0");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x2bdf0u &&
				    r24 - 2u < ROMBase + 0x2bec0u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x2bdf0u &&
					     dest < ROMBase + 0x2bec0u))
						dest = ROMBase + 0x2bec0u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned npack2b;
						if (npack2b < 8) {
							npack2b++;
							nw_boot_log("G3: 68k skip PACK unpack 0x2bec0");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x2760u &&
				    r24 - 2u < ROMBase + 0x3f68u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x2760u &&
					     dest < ROMBase + 0x3f68u))
						dest = ROMBase + 0x3f68u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nofreg;
						if (nofreg < 8) {
							nofreg++;
							nw_boot_log("G3: 68k skip OF NameReg 0x3f68");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x3f6eu &&
				    r24 - 2u < ROMBase + 0x3f8cu) {
					gpr(24) = ROMBase + 0x3f8cu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ncmpa;
						if (ncmpa < 8) {
							ncmpa++;
							nw_boot_log("G3: 68k skip GetOSTrap CMPA 0x3f8c");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x48e32u &&
				    r24 - 2u < ROMBase + 0x49136u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x49136u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ndisp;
						if (ndisp < 8) {
							ndisp++;
							nw_boot_log("G3: 68k skip DisplayDispatch mill 0x49136");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x47ac2u &&
				    r24 - 2u < ROMBase + 0x47b26u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x47b26u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nemdn;
						if (nemdn < 8) {
							nemdn++;
							nw_boot_log("G3: 68k skip EMDN walk 0x47b26");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x214f2u &&
				    r24 - 2u < ROMBase + 0x21500u) {
					gpr(24) = ROMBase + 0x21500u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n60ffqd;
						if (n60ffqd < 8) {
							n60ffqd++;
							nw_boot_log("G3: 68k skip 60ff 0x21500");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x21516u &&
				    r24 - 2u < ROMBase + 0x21518u) {
					gpr(24) = ROMBase + 0x21518u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x2153eu &&
				    r24 - 2u < ROMBase + 0x21540u) {
					gpr(24) = ROMBase + 0x21540u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nbra215;
						if (nbra215 < 8) {
							nbra215++;
							nw_boot_log("G3: 68k skip BRA mill 0x2153e");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x21576u &&
				    r24 - 2u < ROMBase + 0x2158au) {
					gpr(24) = ROMBase + 0x2158au;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nbra576;
						if (nbra576 < 8) {
							nbra576++;
							nw_boot_log("G3: 68k skip JSR mill 0x2157a");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x2157eu &&
				    r24 - 2u < ROMBase + 0x2158au) {
					gpr(24) = ROMBase + 0x2158au;
#if NW_BOOT_LOG
					{
						static unsigned n2157e;
						if (n2157e < 8) {
							n2157e++;
							nw_boot_log("G3: 68k skip dest-edge 0x2157e");
						}
					}
#endif
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1e8b2u &&
				    r24 - 2u < ROMBase + 0x1e8b8u) {
					gpr(24) = ROMBase + 0x1e8b8u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nclrf;
						if (nclrf < 8) {
							nclrf++;
							nw_boot_log("G3: 68k skip CLR DBF 0x1e8b8");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x25496u &&
				    r24 - 2u < ROMBase + 0x255f6u) {
					gpr(24) = ROMBase + 0x255f6u;
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nsyze;
						if (nsyze < 8) {
							nsyze++;
							nw_boot_log("G3: 68k skip SysError 25 0x255f6");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x28b12u &&
				    r24 - 2u < ROMBase + 0x28b66u) {
					gpr(24) = ROMBase + 0x28b66u;
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nsy25b;
						if (nsy25b < 8) {
							nsy25b++;
							nw_boot_log("G3: 68k skip SysError 25 0x28b66");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1e90eu &&
				    r24 - 2u < ROMBase + 0x1e920u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x1e90eu &&
					     dest < ROMBase + 0x1e920u))
						dest = ROMBase + 0x1e920u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n2f1e9;
						if (n2f1e9 < 8) {
							n2f1e9++;
							nw_boot_log("G3: 68k skip MM 2f30 0x1e90e");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x20f38u &&
				    r24 - 2u < ROMBase + 0x20ffeu) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x20f38u &&
					     dest < ROMBase + 0x20ffeu))
						dest = ROMBase + 0x20ffeu;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n2f38;
						if (n2f38 < 8) {
							n2f38++;
							nw_boot_log("G3: 68k skip 2f38 0x20ffe");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x492au &&
				    r24 - 2u < ROMBase + 0x49dcu) {
					gpr(24) = ROMBase + 0x49dcu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nbeq49;
						if (nbeq49 < 8) {
							nbeq49++;
							nw_boot_log("G3: 68k skip IRQ trampoline 0x49dc");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x75fcu &&
				    r24 - 2u < ROMBase + 0x7640u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x75fcu &&
					     dest < ROMBase + 0x7640u))
						dest = ROMBase + 0x7640u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmmgl;
						if (nmmgl < 8) {
							nmmgl++;
							nw_boot_log("G3: 68k skip MM glue 0x7640");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1951au &&
				    r24 - 2u < ROMBase + 0x1951cu) {
					gpr(24) = ROMBase + 0x1951cu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1ebecu &&
				    r24 - 2u < ROMBase + 0x1ebf0u) {
					gpr(24) = ROMBase + 0x1ebf0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nz1eb;
						if (nz1eb < 4) {
							nz1eb++;
							nw_boot_log("G3: 68k skip zeros 0x1ebf0");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x252a6u &&
				    r24 - 2u < ROMBase + 0x252b0u) {
					gpr(24) = ROMBase + 0x252b0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nz252;
						if (nz252 < 8) {
							nz252++;
							nw_boot_log("G3: 68k skip zeros 0x252b0");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1fa1eu &&
				    r24 - 2u < ROMBase + 0x1fa34u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x1fa34u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n1fa;
						if (n1fa < 8) {
							n1fa++;
							nw_boot_log("G3: 68k skip copy dest-edge 0x1fa34");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1e9d2u &&
				    r24 - 2u < ROMBase + 0x1ea48u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x1e9d2u &&
					     dest < ROMBase + 0x1ea48u))
						dest = ROMBase + 0x1ea48u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nbb8;
						if (nbb8 < 8) {
							nbb8++;
							nw_boot_log("G3: 68k skip 2f30 copy 0x1ea48");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1ee20u &&
				    r24 - 2u < ROMBase + 0x1f7deu) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x1ee20u &&
					     dest < ROMBase + 0x1f7deu))
						dest = ROMBase + 0x1f7deu;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ngrsrc;
						if (ngrsrc < 8) {
							ngrsrc++;
							nw_boot_log("G3: 68k skip GetResource 0x1ee20");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1f8f8u &&
				    r24 - 2u < ROMBase + 0x1f90cu) {
					gpr(24) = ROMBase + 0x1f90cu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n1f8f8;
						if (n1f8f8 < 8) {
							n1f8f8++;
							nw_boot_log("G3: 68k skip MOVEA mill 0x1f90c");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1fe88u &&
				    r24 - 2u < ROMBase + 0x1fef6u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x1fe88u &&
					     dest < ROMBase + 0x1fef6u))
						dest = ROMBase + 0x1fef6u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmm1fe;
						if (nmm1fe < 8) {
							nmm1fe++;
							nw_boot_log("G3: 68k skip MM 2f30 0x1fef6");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x24c52u &&
				    r24 - 2u < ROMBase + 0x24e80u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x24c52u &&
					     dest < ROMBase + 0x24e80u))
						dest = ROMBase + 0x255f6u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmm24;
						if (nmm24 < 8) {
							nmm24++;
							nw_boot_log("G3: 68k skip MM 2f30 dest 0x255f6");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x26c50u &&
				    r24 - 2u < ROMBase + 0x26de0u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x26c50u &&
					     dest < ROMBase + 0x26de0u))
						dest = ROMBase + 0x26de0u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nslotsel;
						if (nslotsel < 8) {
							nslotsel++;
							nw_boot_log("G3: 68k skip slot sel 0x26c50");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x640c0u &&
				    r24 - 2u < ROMBase + 0x64ef6u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x64ef6u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmm64;
						if (nmm64 < 8) {
							nmm64++;
							nw_boot_log("G3: 68k skip MM mill 0x640c0-0x64ef6");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x67a84u &&
				    r24 - 2u < ROMBase + 0x67a90u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x67a84u &&
					     dest < ROMBase + 0x67a90u))
						dest = ROMBase + 0x67a90u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmm67;
						if (nmm67 < 8) {
							nmm67++;
							nw_boot_log("G3: 68k skip MM 2f30 0x67a84");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x5d882u &&
				    r24 - 2u < ROMBase + 0x5d8b0u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x5d882u &&
					     dest < ROMBase + 0x5d8b0u))
						dest = ROMBase + 0x5d8b0u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nataload;
						if (nataload < 8) {
							nataload++;
							nw_boot_log("G3: 68k skip ATALoad 0x5d8b0");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x5d90au &&
				    r24 - 2u < ROMBase + 0x5d920u) {
					gpr(24) = ROMBase + 0x5d920u;
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned natamgr;
						if (natamgr < 8) {
							natamgr++;
							nw_boot_log("G3: 68k skip ATA str 0x5d920");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x5da6au &&
				    r24 - 2u < ROMBase + 0x5da80u) {
					gpr(24) = ROMBase + 0x5da80u;
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ndevty;
						if (ndevty < 8) {
							ndevty++;
							nw_boot_log("G3: 68k skip device_type 0x5da80");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x5cc2au &&
				    r24 - 2u < ROMBase + 0x5cc40u) {
					gpr(24) = ROMBase + 0x5cc40u;
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x5cc90u &&
				    r24 - 2u < ROMBase + 0x5cce0u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x5cce0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nbit8;
						if (nbit8 < 8) {
							nbit8++;
							nw_boot_log("G3: 68k skip $08A8 bit scan 0x5cce0");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x5dbd0u &&
				    r24 - 2u < ROMBase + 0x5dbe0u) {
					gpr(24) = ROMBase + 0x5dbe0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x498eu &&
				    r24 - 2u < ROMBase + 0x4990u) {
					gpr(24) = ROMBase + 0x4990u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0xa8884u &&
				    r24 - 2u < ROMBase + 0xa8890u) {
					gpr(24) = ROMBase + 0xa8890u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0xa8900u &&
				    r24 - 2u < ROMBase + 0xa894au) {
					gpr(8) = gpr(16);
					g3_ccr = 4;
					gpr(24) = ROMBase + 0xa894au;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmcpy;
						if (nmcpy < 8) {
							nmcpy++;
							nw_boot_log("G3: 68k skip SANE memcpy 0xa894a");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0xa894cu &&
				    r24 - 2u < ROMBase + 0xa8960u) {
					gpr(24) = ROMBase + 0xa8960u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x14318u &&
				    r24 - 2u < ROMBase + 0x14620u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x14318u &&
					     dest < ROMBase + 0x14620u))
						dest = ROMBase + 0x14620u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nunlk;
						if (nunlk < 8) {
							nunlk++;
							nw_boot_log("G3: 68k skip dest-RTS UNLK 0x14620");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x660d4u &&
				    r24 - 2u < ROMBase + 0x66158u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x66158u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nqdb;
						if (nqdb < 8) {
							nqdb++;
							nw_boot_log("G3: 68k skip queue DBEQ 0x66158");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x15690u &&
				    r24 - 2u < ROMBase + 0x157a0u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x15690u &&
					     dest < ROMBase + 0x157a0u))
						dest = ROMBase + 0x157a0u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n157;
						if (n157 < 8) {
							n157++;
							nw_boot_log("G3: 68k skip JMP idx 0x157a0");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x15c20u &&
				    r24 - 2u < ROMBase + 0x15cb0u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x15cb0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nfcbb;
						if (nfcbb < 8) {
							nfcbb++;
							nw_boot_log("G3: 68k skip FCB bound 0x15cb0");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x14d20u &&
				    r24 - 2u < ROMBase + 0x14d50u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x14d50u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nlsr;
						if (nlsr < 8) {
							nlsr++;
							nw_boot_log("G3: 68k skip LSR mill 0x14d50");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1735cu &&
				    r24 - 2u < ROMBase + 0x173dau) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x173dau;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nslotm;
						if (nslotm < 8) {
							nslotm++;
							nw_boot_log("G3: 68k skip SlotManager mill 0x173da");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x17d66u &&
				    r24 - 2u < ROMBase + 0x17d90u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x17d90u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n2f17d;
						if (n2f17d < 8) {
							n2f17d++;
							nw_boot_log("G3: 68k skip 2f30 dest-RTS 0x17d90");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x8040u &&
				    r24 - 2u < ROMBase + 0x8052u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x8052u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n8040;
						if (n8040 < 8) {
							n8040++;
							nw_boot_log("G3: 68k skip offset tbl 0x8052");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x18c2au &&
				    r24 - 2u < ROMBase + 0x18c50u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x18c50u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nitt;
						if (nitt < 8) {
							nitt++;
							nw_boot_log("G3: 68k skip InitItt 0x18c50");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1914cu &&
				    r24 - 2u < ROMBase + 0x1a7d0u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x1a7d0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nscsi;
						if (nscsi < 8) {
							nscsi++;
							nw_boot_log("G3: 68k skip SCSIAtomic mill 0x1a7d0");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x5cbb4u &&
				    r24 - 2u < ROMBase + 0x5cbb8u) {
					gpr(24) = ROMBase + 0x5cbb8u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nrtdp;
						if (nrtdp < 8) {
							nrtdp++;
							nw_boot_log("G3: 68k skip RTD pad 0x5cbb8");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x40f2u &&
				    r24 - 2u < ROMBase + 0x4248u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x4248u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ngetst;
						if (ngetst < 8) {
							ngetst++;
							nw_boot_log("G3: 68k skip GetStartupDevice 0x4248");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x52ba0u &&
				    r24 - 2u < ROMBase + 0x52bc0u) {
					gpr(24) = ROMBase + 0x52bc0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1df96u &&
				    r24 - 2u < ROMBase + 0x1df98u) {
					gpr(24) = ROMBase + 0x1df98u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1df82u &&
				    r24 - 2u < ROMBase + 0x1df84u) {
					gpr(24) = ROMBase + 0x1df84u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nbsrde;
						if (nbsrde < 8) {
							nbsrde++;
							nw_boot_log("G3: 68k skip BSR dest 0x1df84");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1ddf2u &&
				    r24 - 2u < ROMBase + 0x1ddf4u) {
					gpr(24) = ROMBase + 0x1ddf4u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x146b0u &&
				    r24 - 2u < ROMBase + 0x146b4u) {
					gpr(24) = ROMBase + 0x146b4u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x5c626u &&
				    r24 - 2u < ROMBase + 0x5c820u) {
					gpr(24) = ROMBase + 0x5c820u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x5c6d2u &&
				    r24 - 2u < ROMBase + 0x5c6e0u) {
					gpr(24) = ROMBase + 0x5c6e0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n31ef;
						if (n31ef < 8) {
							n31ef++;
							nw_boot_log("G3: 68k skip 31ef dest 0x5c6e0");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x1ffd8u &&
				    r24 - 2u < ROMBase + 0x1ffdau) {
					gpr(24) = ROMBase + 0x1ffdau;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x24e82u &&
				    r24 - 2u < ROMBase + 0x25490u) {
					gpr(24) = ROMBase + 0x25490u;
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ntst8;
						if (ntst8 < 8) {
							ntst8++;
							nw_boot_log("G3: 68k skip slot dest 0x25490");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x24edeu &&
				    r24 - 2u < ROMBase + 0x25490u) {
					gpr(24) = ROMBase + 0x25490u;
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nbgtw;
						if (nbgtw < 8) {
							nbgtw++;
							nw_boot_log("G3: 68k skip BGT wait 0x24ee6");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x7cbau &&
				    r24 - 2u < ROMBase + 0x7cc4u) {
					gpr(24) = ROMBase + 0x7cc4u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x5d350u &&
				    r24 - 2u < ROMBase + 0x5d4c6u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x5d350u &&
					     dest < ROMBase + 0x5d4c6u))
						dest = ROMBase + 0x5d4d0u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nlst;
						if (nlst < 8) {
							nlst++;
							nw_boot_log("G3: 68k skip A4 list 0x5d350");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x3fb0u &&
				    r24 - 2u < ROMBase + 0x3fd8u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x3fb0u &&
					     dest < ROMBase + 0x3fd8u))
						dest = ROMBase + 0x40f0u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned npram;
						if (npram < 8) {
							npram++;
							nw_boot_log("G3: 68k skip PRAM jmp 0x3fb0");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x49f0u &&
				    r24 - 2u < ROMBase + 0x4d4au) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x4d4au;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ngeva;
						if (ngeva < 8) {
							ngeva++;
							nw_boot_log("G3: 68k skip GetOSEvent BRA.L 0x4d4a");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x4d52u &&
				    r24 - 2u < ROMBase + 0x4e16u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x4e30u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ngev;
						if (ngev < 8) {
							ngev++;
							nw_boot_log("G3: 68k skip GetOSEvent 0x4e30");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x2449au &&
				    r24 - 2u < ROMBase + 0x24c50u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x2449au &&
					     dest < ROMBase + 0x24c50u))
						dest = ROMBase + 0x255f6u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nbra244;
						if (nbra244 < 8) {
							nbra244++;
							nw_boot_log("G3: 68k skip BRA.W mill 0x24498");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x22f02u &&
				    r24 - 2u < ROMBase + 0x22ffeu) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x22f02u &&
					     dest < ROMBase + 0x22ffeu))
						dest = ROMBase + 0x22ffeu;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nbra22f;
						if (nbra22f < 8) {
							nbra22f++;
							nw_boot_log("G3: 68k skip BRA.W mill 0x22f00");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x22cd2u &&
				    r24 - 2u < ROMBase + 0x24e80u &&
				    !(r24 - 2u >= ROMBase + 0x24284u &&
				      r24 - 2u < ROMBase + 0x24290u) &&
				    r24 - 2u != ROMBase + 0x24c50u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x22cd2u &&
					     dest < ROMBase + 0x24e80u &&
					     dest != ROMBase + 0x24c50u &&
					     !(dest >= ROMBase + 0x24284u &&
					       dest < ROMBase + 0x24290u)))
						dest = ROMBase + 0x255f6u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nbra22c;
						if (nbra22c < 8) {
							nbra22c++;
							nw_boot_log("G3: 68k skip slot mill hole 0x24288");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x224e2u &&
				    r24 - 2u < ROMBase + 0x24e80u &&
				    !(r24 - 2u >= ROMBase + 0x24284u &&
				      r24 - 2u < ROMBase + 0x24290u) &&
				    r24 - 2u != ROMBase + 0x24c50u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x224e2u &&
					     dest < ROMBase + 0x24e80u &&
					     dest != ROMBase + 0x24c50u &&
					     !(dest >= ROMBase + 0x24284u &&
					       dest < ROMBase + 0x24290u)))
						dest = ROMBase + 0x255f6u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nslot226;
						if (nslot226 < 8) {
							nslot226++;
							nw_boot_log("G3: 68k skip slot walk 0x24e80");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x16ed0u &&
				    r24 - 2u < ROMBase + 0x16f8cu) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x16f8cu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned npk16e;
						if (npk16e < 8) {
							npk16e++;
							nw_boot_log("G3: 68k skip packed 0x16ed0");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x215c4u &&
				    r24 - 2u < ROMBase + 0x22394u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0xffffffddu;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x215c4u &&
					     dest < ROMBase + 0x22394u))
						dest = ROMBase + 0x22394u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nq356;
						if (nq356 < 8) {
							nq356++;
							nw_boot_log("G3: 68k skip queue walk 0x22394");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x5be12u &&
				    r24 - 2u < ROMBase + 0x5c820u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x5be12u &&
					     dest < ROMBase + 0x5c820u))
						dest = ROMBase + 0x5c820u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned na8b5;
						if (na8b5 < 8) {
							na8b5++;
							nw_boot_log("G3: 68k skip A8B5 mill 0x5c820");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x5ce1cu &&
				    r24 - 2u < ROMBase + 0x5cfb0u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x5ce1cu &&
					     dest < ROMBase + 0x5cfb0u))
						dest = ROMBase + 0x5cfb0u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nhidc;
						if (nhidc < 8) {
							nhidc++;
							nw_boot_log("G3: 68k skip HideCursor mill 0x5ce1c");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x4ffe0u &&
				    r24 - 2u < ROMBase + 0x50840u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x4ffe0u &&
					     dest < ROMBase + 0x50840u))
						dest = ROMBase + 0x50840u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nscrn;
						if (nscrn < 8) {
							nscrn++;
							nw_boot_log("G3: 68k skip scrn mill 0x4ffe0");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x160e0u &&
				    r24 - 2u < ROMBase + 0x16780u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x160e0u &&
					     dest < ROMBase + 0x16780u))
						dest = ROMBase + 0x16780u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned npar;
						if (npar < 8) {
							npar++;
							nw_boot_log("G3: 68k skip parser mill 0x160e0");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x7e306u &&
				    r24 - 2u < ROMBase + 0x7e35au) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x7e35au;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmm7;
						if (nmm7 < 8) {
							nmm7++;
							nw_boot_log("G3: 68k skip MM thunk 0x7e306");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x271d0u &&
				    r24 - 2u < ROMBase + 0x27620u) {
					gpr(8) = 0xffffffceu;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x27620u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmm3;
						if (nmm3 < 8) {
							nmm3++;
							nw_boot_log("G3: 68k skip MM thunk 0x27620");
						}
					}
#endif
					continue;
				}
				/* SCSI/VIA mill: tables + $0D18 dispatch.
				 * Dest 0xd5f4 RTS. */
				if (r24 - 2u >= ROMBase + 0xbf60u &&
				    r24 - 2u < ROMBase + 0xd5f4u) {
					gpr(8) = 0xffffffceu;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0xd5f4u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nscsim;
						if (nscsim < 8) {
							nscsim++;
							nw_boot_log("G3: 68k skip SCSI mill 0xbf60");
						}
					}
#endif
					continue;
				}
				/* SCSI popcount table executed as 68k.
				 * Dest next MOVEA.L $0D18. */
				if (r24 - 2u >= ROMBase + 0xc75au &&
				    r24 - 2u < ROMBase + 0xc86cu) {
					gpr(24) = ROMBase + 0xc86cu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nbits;
						if (nbits < 8) {
							nbits++;
							nw_boot_log("G3: 68k skip SCSI bits 0xc75e");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0xc91cu &&
				    r24 - 2u < ROMBase + 0xc92au) {
					gpr(24) = ROMBase + 0xc92au;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				/* SCSI selector jump table. Dest 0xd570. */
				if (r24 - 2u >= ROMBase + 0xd4b4u &&
				    r24 - 2u < ROMBase + 0xd570u) {
					gpr(24) = ROMBase + 0xd570u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned njt;
						if (njt < 8) {
							njt++;
							nw_boot_log("G3: 68k skip SCSI jtab 0xd4b4");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0xd590u &&
				    r24 - 2u < ROMBase + 0xd5f4u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0xd5f4u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nbral;
						if (nbral < 8) {
							nbral++;
							nw_boot_log("G3: 68k skip SCSI BRA.L 0xd592");
						}
					}
#endif
					continue;
				}
				/* SCSI BTST $026F(A0) / BNE wait. Dest
				 * MOVE.W #$FF00,D0. */
				if (r24 - 2u >= ROMBase + 0xcbdau &&
				    r24 - 2u < ROMBase + 0xcbe4u) {
					gpr(8) = 0xffffff00u;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0xcbe4u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nscsib;
						if (nscsib < 8) {
							nscsib++;
							nw_boot_log("G3: 68k skip SCSI BTST 0xcbda");
						}
					}
#endif
					continue;
				}
				/* SCSI BHI vtable waits. Dest 0xce24 RTS. */
				if (r24 - 2u >= ROMBase + 0xcde8u &&
				    r24 - 2u < ROMBase + 0xce24u) {
					gpr(8) = 0xffffffceu;
					gpr(24) = ROMBase + 0xce24u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nscsib2;
						if (nscsib2 < 8) {
							nscsib2++;
							nw_boot_log("G3: 68k skip SCSI BHI 0xcde8");
						}
					}
#endif
					continue;
				}
				/* SCSI cd37 BRA back into VIA poll. */
				if (r24 - 2u >= ROMBase + 0xcfc6u &&
				    r24 - 2u < ROMBase + 0xd04eu) {
					gpr(24) = ROMBase + 0xd04eu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nscsib3;
						if (nscsib3 < 8) {
							nscsib3++;
							nw_boot_log("G3: 68k skip SCSI BRA 0xcfc6");
						}
					}
#endif
					continue;
				}
				/* VIA/SCC poll + DBF * delay. Dest 0xcfb0
				 * is LEA/MOVEM/RTS restore. */
				if (r24 - 2u >= ROMBase + 0xce26u &&
				    r24 - 2u < ROMBase + 0xcfb0u) {
					gpr(24) = ROMBase + 0xcfb0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nvia2;
						if (nvia2 < 4) {
							nvia2++;
							nw_boot_log("G3: 68k skip VIA poll 0xce26");
						}
					}
#endif
					continue;
				}
				/* BlockMove/GetPtrSize walker. BEQ 0x2d3f4
				 * never ends if D3!=D4. Dest 0x2d402 RTS. */
				/* JHideCursor vector: JMP (A1) landed on
				 * 0x25002 immediate. Dest JMP (A1) if
				 * A1 ok and outside this range. */
				if (r24 - 2u >= ROMBase + 0x24ffcu &&
				    r24 - 2u < ROMBase + 0x2501cu) {
					uint32 t = g3_rom0(gpr(17));
					if (!g3_r24_ok(t) ||
					    vm_read_memory_2(t) == 0 ||
					    (t >= ROMBase + 0x24ffcu &&
					     t < ROMBase + 0x2501cu)) {
						const uint32 sp = gpr(1);
						uint32 dest =
							g3_rom0(vm_read_memory_4(sp));
						gpr(1) = sp + 4;
						if (g3_r24_ok(dest) &&
						    dest != r24 - 2u &&
						    !(dest >= ROMBase + 0x24ffcu &&
						      dest < ROMBase + 0x2501cu))
							t = dest;
						else
							t = g3_fix_r24(r24);
					}
					gpr(24) = t;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned njh;
						if (njh < 4) {
							njh++;
							nw_boot_log("G3: 68k skip JHideCursor 0x24ffc");
						}
					}
#endif
					continue;
				}
				/* Gestalt 0x0ad5 iterator. BHI 0x2cc7a.
				 * Dest 0x2ccbc RTS. D0 was 0 so caller
				 * 0x2ccc0 BEQ skipped its body. */
				if (r24 - 2u >= ROMBase + 0x2cc40u &&
				    r24 - 2u < ROMBase + 0x2ccbcu) {
					gpr(8) = RAMBase + 0x50000u;
					g3_ccr = 0;
					gpr(24) = ROMBase + 0x2ccbcu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ngst3;
						if (ngst3 < 4) {
							ngst3++;
							nw_boot_log("G3: 68k skip Gestalt 0xad5 0x2cc40");
						}
					}
#endif
					continue;
				}
				/* Calls skipped 0x2cc40 then ABEB. Dest
				 * 0x2cd34 RTS; LINK never ran. */
				if (r24 - 2u >= ROMBase + 0x2ccc0u &&
				    r24 - 2u < ROMBase + 0x2cd34u) {
					gpr(8) = 1;
					g3_ccr = 0;
					gpr(24) = ROMBase + 0x2cd34u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ngw;
						if (ngw < 4) {
							ngw++;
							nw_boot_log("G3: 68k skip Gestalt wrap 0x2ccc0");
						}
					}
#endif
					continue;
				}
				/* Status+Gestalt+Control wrapper. BEQ.W
				 * 0x2d204 is MOVEM/UNLK; never LINKed so
				 * synthetic RTS. */
				if (r24 - 2u >= ROMBase + 0x2cd40u &&
				    r24 - 2u < ROMBase + 0x2cdf8u) {
					gpr(8) = 1;
					g3_ccr = 0;
					gpr(24) = ROMBase + 0x2cdf8u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nst;
						if (nst < 4) {
							nst++;
							nw_boot_log("G3: 68k skip Status 0x2cd40");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x2cf70u &&
				    r24 - 2u < ROMBase + 0x2d20eu) {
					/* Dest 0x2d20e RTS; do not pop here
					 * (entry was JSR, RTS pops caller). */
					gpr(8) = 1;
					gpr(14) = 1;
					g3_ccr = 0;
					gpr(24) = ROMBase + 0x2d20eu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nsc;
						if (nsc < 4) {
							nsc++;
							nw_boot_log("G3: 68k skip Status/Control 0x2cf70");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x2ce00u &&
				    r24 - 2u < ROMBase + 0x2ce7cu) {
					gpr(24) = ROMBase + 0x2ce7cu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				/* Gestalt 0x0ad6/0x07ef helper. Dest
				 * 0x2cf64 RTS. */
				if (r24 - 2u >= ROMBase + 0x2ce80u &&
				    r24 - 2u < ROMBase + 0x2cf64u) {
					gpr(24) = ROMBase + 0x2cf64u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ngst2;
						if (ngst2 < 4) {
							ngst2++;
							nw_boot_log("G3: 68k skip Gestalt helper 0x2ce80");
						}
					}
#endif
					continue;
				}
				/* sRsrc helper: BGT 0x137de then UNLK
				 * A6=0. Dest was 0x13840 LINK mill. Dest 0x138b0. */
				if (r24 - 2u >= ROMBase + 0x137b0u &&
				    r24 - 2u < ROMBase + 0x138b0u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x138b0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nsrh;
						if (nsrh < 8) {
							nsrh++;
							nw_boot_log("G3: 68k skip sRsrc 0x138b0");
						}
					}
#endif
					continue;
				}
				/* Slot/sResource walker: CMPI #2 / BNE
				 * 0x14ff4. Dest 0x1508a RTS. */
				if (r24 - 2u >= ROMBase + 0x14fd0u &&
				    r24 - 2u < ROMBase + 0x1508au) {
					gpr(24) = ROMBase + 0x1508au;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nsrsrc;
						if (nsrsrc < 4) {
							nsrsrc++;
							nw_boot_log("G3: 68k skip sRsrc walk 0x14fd0");
						}
					}
#endif
					continue;
				}
				/* sRsrc wrapper LINK/JSR/UNLK/RTS. Dest
				 * 0x150a2 RTS self-looped; next LINK
				 * 0x150b0. */
				if (r24 - 2u >= ROMBase + 0x15090u &&
				    r24 - 2u < ROMBase + 0x150a4u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x15090u &&
					     dest < ROMBase + 0x150b0u))
						dest = ROMBase + 0x150b0u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nsrw;
						if (nsrw < 8) {
							nsrw++;
							nw_boot_log("G3: 68k skip sRsrc wrap 0x15090");
						}
					}
#endif
					continue;
				}
				/* Lowmem-to-ROM copy until D2==-1. Dest
				 * 0x7b26 is the RTS. */
				if (r24 - 2u >= ROMBase + 0x7af8u &&
				    r24 - 2u < ROMBase + 0x7b26u) {
					gpr(24) = ROMBase + 0x7b26u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ncpy;
						if (ncpy < 4) {
							ncpy++;
							nw_boot_log("G3: 68k skip lowmem copy 0x7af8");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x7516u &&
				    r24 - 2u < ROMBase + 0x76b0u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x76b0u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n60ff75;
						if (n60ff75 < 8) {
							n60ff75++;
							nw_boot_log("G3: 68k skip 60ff mill 0x76b0");
						}
					}
#endif
					continue;
				}
				/* Dest-RTS + 00000042 offset table + JMP (A6).
				 * Dest 0x78d2 MOVEQ #1. Keep 0x77ce. */
				if (r24 - 2u >= ROMBase + 0x77d0u &&
				    r24 - 2u < ROMBase + 0x78d2u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x77d0u &&
					     dest < ROMBase + 0x78d2u))
						dest = ROMBase + 0x78d2u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nofft;
						if (nofft < 8) {
							nofft++;
							nw_boot_log("G3: 68k skip offset tbl 0x77d0");
						}
					}
#endif
					continue;
				}
				/* Dest 0x7bbc RTS after F-line helper. */
				if (r24 - 2u >= ROMBase + 0x7b40u &&
				    r24 - 2u < ROMBase + 0x7bbcu) {
					gpr(24) = ROMBase + 0x7bbcu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned njt7;
						if (njt7 < 8) {
							njt7++;
							nw_boot_log("G3: 68k skip JMP table 0x7bbc");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x7bd2u &&
				    r24 - 2u < ROMBase + 0x7c06u) {
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = ROMBase + 0x7c06u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nfe0a;
						if (nfe0a < 8) {
							nfe0a++;
							nw_boot_log("G3: 68k skip F-line DBF 0x7c06");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x27ab0u &&
				    r24 - 2u < ROMBase + 0x27ad8u) {
					gpr(24) = ROMBase + 0x27ad8u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ncpy2a;
						if (ncpy2a < 8) {
							ncpy2a++;
							nw_boot_log("G3: 68k skip $2A copy DBF 0x27ad8");
						}
					}
#endif
					continue;
				}
				/* SANE EOR.B / DBEQ mill. Dest 0xa88e2
				 * MOVEQ #0,D1. Keep 0xa88c0. */
				if (r24 - 2u >= ROMBase + 0xa8818u &&
				    r24 - 2u < ROMBase + 0xa882eu) {
					gpr(24) = ROMBase + 0xa882eu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nsaneadd;
						if (nsaneadd < 8) {
							nsaneadd++;
							nw_boot_log("G3: 68k skip SANE add 0xa882e");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0xa8848u &&
				    r24 - 2u < ROMBase + 0xa884cu) {
					gpr(24) = ROMBase + 0xa884cu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0xa88c2u &&
				    r24 - 2u < ROMBase + 0xa88e2u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0xa88c0u &&
					     dest < ROMBase + 0xa88e2u))
						dest = ROMBase + 0xa88e2u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nsanedbf;
						if (nsanedbf < 8) {
							nsanedbf++;
							nw_boot_log("G3: 68k skip SANE DBF 0xa88c0");
						}
					}
#endif
					continue;
				}
				/* SANE FP helper: MOVEA.L A6,A2 / BRA.L
				 * dispatcher / JMP (A2). A2/A6 are 0 so
				 * JMP (A2) g3_fix-loops the body. A6
				 * linkage if legal, else RTS. */
				if (r24 - 2u >= ROMBase + 0xa8960u &&
				    r24 - 2u < ROMBase + 0xa8c66u) {
					uint32 dest = g3_rom0(gpr(22));
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0xa8960u &&
					     dest < ROMBase + 0xa8c66u)) {
						const uint32 sp = gpr(1);
						dest = g3_rom0(vm_read_memory_4(sp));
						gpr(1) = sp + 4;
						if (!g3_r24_ok(dest) || dest == r24 - 2u ||
						    (dest >= ROMBase + 0xa8960u &&
						     dest < ROMBase + 0xa8c66u))
							dest = ROMBase + 0x198154u;
					}
					gpr(8) = 0;
					g3_ccr = 4;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nsanefp;
						if (nsanefp < 8) {
							nsanefp++;
							nw_boot_log("G3: 68k skip SANE FP 0xa8960");
						}
					}
#endif
					continue;
				}
				/* Gestalt 0x0ad6/0x0ad5 iterator. BCS
				 * 0x2d28c never ends. Dest RTD #2. */
				if (r24 - 2u >= ROMBase + 0x2d250u &&
				    r24 - 2u < ROMBase + 0x2d2c8u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 6;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x2d250u &&
					     dest < ROMBase + 0x2d2c8u))
						dest = g3_fix_r24(ROMBase + 0x2d47eu);
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ngst;
						if (ngst < 4) {
							ngst++;
							nw_boot_log("G3: 68k skip Gestalt iter 0x2d250");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x2d480u &&
				    r24 - 2u < ROMBase + 0x2d4b4u) {
					gpr(24) = ROMBase + 0x2d47eu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x2d2feu &&
				    r24 - 2u < ROMBase + 0x2d47eu) {
					gpr(24) = ROMBase + 0x2d47eu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nbm;
						if (nbm < 4) {
							nbm++;
							nw_boot_log("G3: 68k skip BlockMove walk 0x2d2fe");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x2088au &&
				    r24 - 2u < ROMBase + 0x208f8u) {
					gpr(24) = ROMBase + 0x20b80u;
					gpr(8) = 0;
					gpr(11) = 0;
					gpr(17) = RAMBase + 0xe000u;
					gpr(18) = RAMBase + 0xf000u;
					gpr(21) = RAMBase + 0xa100u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ntk;
						if (ntk < 4) {
							ntk++;
							nw_boot_log("G3: 68k skip field thunks 0x20b80");
						}
					}
#endif
					continue;
				}
				/* JInitProc/$0394 walker: JMP (A1) with
				 * A1=0. Dest 0x2b132 RTS. */
				if (r24 - 2u >= ROMBase + 0x2b0fau &&
				    r24 - 2u < ROMBase + 0x2b132u) {
					gpr(24) = ROMBase + 0x2b132u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned njinit;
						if (njinit < 4) {
							njinit++;
							nw_boot_log("G3: 68k skip JInitProc 0x2b0fa");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x2b134u &&
				    r24 - 2u < ROMBase + 0x2b152u) {
					gpr(24) = ROMBase + 0x2b152u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				/* Drive-queue node init: DBF D2 BSR 0x2ac3c.
				 * Dest was JMP (d16,PC) 0x2b284 into F-line
				 * data. Synthetic RTS instead. */
				if (r24 - 2u >= ROMBase + 0x2b1dcu &&
				    r24 - 2u < ROMBase + 0x2b286u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x2b1dcu &&
					     dest < ROMBase + 0x2b286u))
						dest = g3_fix_r24(ROMBase + 0x2d47eu);
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nqi;
						if (nqi < 4) {
							nqi++;
							nw_boot_log("G3: 68k skip queue init 0x2b1dc");
						}
					}
#endif
					continue;
				}
				/* CLR.L -(A1) DBF * fill. Dest 0x2b1da RTS. */
				if (r24 - 2u >= ROMBase + 0x2b1b0u &&
				    r24 - 2u < ROMBase + 0x2b1dau) {
					gpr(24) = ROMBase + 0x2b1dau;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				/* Drive/queue walk at $0378: CMPA A3,A4
				 * then BRA back. Empty list dest 0x2ad98 RTS. */
				if (r24 - 2u >= ROMBase + 0x2ad58u &&
				    r24 - 2u < ROMBase + 0x2ad98u) {
					gpr(24) = ROMBase + 0x2ad98u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nq;
						if (nq < 4) {
							nq++;
							nw_boot_log("G3: 68k skip queue walk 0x2ad58");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x2a166u &&
				    r24 - 2u < ROMBase + 0x2a204u) {
					const uint32 sp = gpr(1);
					uint32 dest = g3_rom0(vm_read_memory_4(sp));
					gpr(1) = sp + 4;
					gpr(8) = 0;
					g3_ccr = 4;
					if (!g3_r24_ok(dest) || dest == r24 - 2u ||
					    (dest >= ROMBase + 0x2a166u &&
					     dest < ROMBase + 0x2a204u))
						dest = ROMBase + 0x2a204u;
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nq37c;
						if (nq37c < 8) {
							nq37c++;
							nw_boot_log("G3: 68k skip queue walk 0x2a166");
						}
					}
#endif
					continue;
				}
				/* DrvQ walk: MOVEA.L (A4),A4 until CMPA
				 * A3. Poison A4=bc610000. Dest 0x2a8be
				 * clears A4. */
				if (r24 - 2u >= ROMBase + 0x2a8a2u &&
				    r24 - 2u < ROMBase + 0x2a8beu) {
					gpr(24) = ROMBase + 0x2a8beu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ndrvq;
						if (ndrvq < 4) {
							ndrvq++;
							nw_boot_log("G3: 68k skip DrvQ walk 0x2a8a2");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x2aa0eu &&
				    r24 - 2u < ROMBase + 0x2aa60u) {
					gpr(24) = ROMBase + 0x2aa60u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x2ac50u &&
				    r24 - 2u < ROMBase + 0x2acb8u) {
					gpr(24) = ROMBase + 0x2acb8u;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (r24 - 2u >= ROMBase + 0x2ab7au &&
				    r24 - 2u < ROMBase + 0x2ac4eu) {
					gpr(24) = ROMBase + 0x2ac4eu;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nvol;
						if (nvol < 4) {
							nvol++;
							nw_boot_log("G3: 68k skip SysVol wait");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf138u) == 0x0008u) {
					gpr(24) = g3_fix_r24(r24);
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xff00u) == 0 &&
				    ((op68 >> 6) & 3u) <= 2u &&
				    ((op68 >> 3) & 7u) == 0u &&
				    op68 != 0) {
					const unsigned sz = (op68 >> 6) & 3u;
					const int dn = (int)(op68 & 7u);
					uint32 imm, mask, v;
					if (sz == 2u) {
						imm = vm_read_memory_4(r24);
						r24 += 4;
						mask = 0xffffffffu;
					} else if (sz == 1u) {
						imm = vm_read_memory_2(r24);
						r24 += 2;
						mask = 0xffffu;
					} else {
						imm = vm_read_memory_2(r24) & 0xffu;
						r24 += 2;
						mask = 0xffu;
					}
					v = (gpr(8 + dn) | imm) & mask;
					gpr(8 + dn) = (gpr(8 + dn) & ~mask) | v;
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((sz == 2u && (int32)v < 0) ||
					    (sz == 1u && (int16)v < 0) ||
					    (sz == 0u && (int8)v < 0))
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (op68 == 0x0000u) {
					uint32 p = r24 + 2;
					unsigned z;
					for (z = 0; z < 64u; z++) {
						if (vm_read_memory_2(p) != 0)
							break;
						p += 2;
					}
					if (p >= ROMBase + 0xa9e0u && p < ROMBase + 0xaa7eu)
						p = ROMBase + 0xaa7eu;
					if (p >= ROMBase + 0x2760u && p < ROMBase + 0x3f68u)
						p = ROMBase + 0x3f68u;
					if (p >= ROMBase + 0x3f6eu && p < ROMBase + 0x3f8cu)
						p = ROMBase + 0x3f8cu;
					if (p >= ROMBase + 0x48e32u && p < ROMBase + 0x49136u)
						p = ROMBase + 0x49136u;
					if (p >= ROMBase + 0x47ac2u && p < ROMBase + 0x47b26u)
						p = ROMBase + 0x47b26u;
					if (p >= ROMBase + 0x7bd2u && p < ROMBase + 0x7c06u)
						p = ROMBase + 0x7c06u;
					if (p >= ROMBase + 0x27ab0u && p < ROMBase + 0x27ad8u)
						p = ROMBase + 0x27ad8u;
					if (p >= ROMBase + 0x27b00u && p < ROMBase + 0x27ca0u)
						p = ROMBase + 0x27ca0u;
					if (p >= ROMBase + 0x8eb0u && p < ROMBase + 0x932au)
						p = ROMBase + 0x932au;
					if (p >= ROMBase + 0x2596eu && p < ROMBase + 0x25974u)
						p = ROMBase + 0x25974u;
					if (p >= ROMBase + 0x25f74u && p < ROMBase + 0x25f7au)
						p = ROMBase + 0x25f7au;
					if (p >= ROMBase + 0x28640u && p < ROMBase + 0x28786u)
						p = ROMBase + 0x28786u;
					if (p >= ROMBase + 0x58000u && p < ROMBase + 0x58090u)
						p = ROMBase + 0x58090u;
					if (p >= ROMBase + 0x27a72u && p < ROMBase + 0x27aaau)
						p = ROMBase + 0x27aaau;
					if (p >= ROMBase + 0x15c20u && p < ROMBase + 0x15cb0u)
						p = ROMBase + 0x15cb0u;
					if (p >= ROMBase + 0x14d20u && p < ROMBase + 0x14d50u)
						p = ROMBase + 0x14d50u;
					if (p >= ROMBase + 0x1735cu && p < ROMBase + 0x173dau)
						p = ROMBase + 0x173dau;
					if (p >= ROMBase + 0x17d66u && p < ROMBase + 0x17d90u)
						p = ROMBase + 0x17d90u;
					if (p >= ROMBase + 0x3fb2u && p < ROMBase + 0x3fd8u)
						p = ROMBase + 0x3fd8u;
					if (p >= ROMBase + 0x5be12u && p < ROMBase + 0x5c820u)
						p = ROMBase + 0x5c820u;
					if (p >= ROMBase + 0x5ce1eu && p < ROMBase + 0x5cfb0u)
						p = ROMBase + 0x5cfb0u;
					if (p >= ROMBase + 0x4fad0u && p < ROMBase + 0x4ffe0u)
						p = ROMBase + 0x4ffe0u;
					if (p >= ROMBase + 0x4ffe2u && p < ROMBase + 0x50840u)
						p = ROMBase + 0x50840u;
					if (p >= ROMBase + 0x4c3e2u && p < ROMBase + 0x4f7c0u)
						p = ROMBase + 0x4f7c0u;
					if (p >= ROMBase + 0x85902u && p < ROMBase + 0x85968u)
						p = ROMBase + 0x85968u;
					if (p >= ROMBase + 0x870f2u && p < ROMBase + 0x8e770u)
						p = ROMBase + 0x8e770u;
					if (p >= ROMBase + 0x80000u && p < ROMBase + 0x806d0u)
						p = ROMBase + 0x806d0u;
					if (p >= ROMBase + 0x7a402u && p < ROMBase + 0x7a538u)
						p = ROMBase + 0x7a538u;
					if (p >= ROMBase + 0x8281au && p < ROMBase + 0x8e770u)
						p = ROMBase + 0x8e770u;
					if (p >= ROMBase + 0x74660u && p < ROMBase + 0x7509eu)
						p = ROMBase + 0x7509eu;
					if (p >= ROMBase + 0x2a168u && p < ROMBase + 0x2a204u)
						p = ROMBase + 0x2a204u;
					if (p >= ROMBase + 0x8dd2u && p < ROMBase + 0x8dd6u)
						p = ROMBase + 0x8dd6u;
					if (p >= ROMBase + 0x2574cu && p < ROMBase + 0x25754u)
						p = ROMBase + 0x25754u;
					if (p >= ROMBase + 0x256b4u && p < ROMBase + 0x256ccu)
						p = ROMBase + 0x256ccu;
					if (p >= ROMBase + 0x257dcu && p < ROMBase + 0x257fau)
						p = ROMBase + 0x257fau;
					if (p >= ROMBase + 0x1b944u && p < ROMBase + 0x1b94eu)
						p = ROMBase + 0x1b94eu;
					if (p >= ROMBase + 0x16daau && p < ROMBase + 0x16daeu)
						p = ROMBase + 0x16daeu;
					if (p >= ROMBase + 0x1ccc6u && p < ROMBase + 0x1ccdcu)
						p = ROMBase + 0x1ccdcu;
					if (p >= ROMBase + 0xe074u && p < ROMBase + 0xe080u)
						p = ROMBase + 0xe080u;
					if (p >= ROMBase + 0xe086u && p < ROMBase + 0xe270u)
						p = ROMBase + 0xe270u;
					if (p >= ROMBase + 0xe61au && p < ROMBase + 0xe99cu)
						p = ROMBase + 0xe99cu;
					if (p >= ROMBase + 0xe3ceu && p < ROMBase + 0xe3d8u)
						p = ROMBase + 0xe3d8u;
					if (p >= ROMBase + 0x8b7eu && p < ROMBase + 0x8baeu)
						p = ROMBase + 0x8baeu;
					if (p >= ROMBase + 0xa028u && p < ROMBase + 0xa0c6u)
						p = ROMBase + 0xa0c6u;
					if (p >= ROMBase + 0xa00cu && p < ROMBase + 0xa01eu)
						p = ROMBase + 0xa01eu;
					if (p >= ROMBase + 0x9fdd8u && p < ROMBase + 0x9fde0u)
						p = ROMBase + 0x9fde0u;
					if (p >= ROMBase + 0x13400u && p < ROMBase + 0x13620u)
						p = ROMBase + 0x13620u;
					if (p >= ROMBase + 0x13f00u && p < ROMBase + 0x13f76u)
						p = ROMBase + 0x13f76u;
					if (p >= ROMBase + 0x13f80u && p < ROMBase + 0x1425au)
						p = ROMBase + 0x1425au;
					if (p >= ROMBase + 0x14bf0u && p < ROMBase + 0x14c60u)
						p = ROMBase + 0x14c60u;
					if (p >= ROMBase + 0x5c872u && p < ROMBase + 0x5c87eu)
						p = ROMBase + 0x5c87eu;

					if (p >= ROMBase + 0x5cc90u && p < ROMBase + 0x5cce0u)
						p = ROMBase + 0x5cce0u;
					if (p >= ROMBase + 0xa37c0u && p < ROMBase + 0xa3a34u)
						p = ROMBase + 0xa3a34u;
					if (p >= ROMBase + 0xa496u && p < ROMBase + 0xa4a4u)
						p = ROMBase + 0xa4a4u;
					if (p >= ROMBase + 0x20f3eu && p < ROMBase + 0x20ffeu)
						p = ROMBase + 0x20ffeu;
					if (p >= ROMBase + 0x1ea50u && p < ROMBase + 0x1ebe0u)
						p = ROMBase + 0x1ebe0u;
					if (p >= ROMBase + 0x210e4u && p < ROMBase + 0x21100u)
						p = ROMBase + 0x21100u;
					if (p >= ROMBase + 0x1e69eu && p < ROMBase + 0x1e6fcu)
						p = ROMBase + 0x1e6fcu;
					if (p >= ROMBase + 0x1e76au && p < ROMBase + 0x1e794u)
						p = ROMBase + 0x1e794u;
					if (p >= ROMBase + 0x1e796u && p < ROMBase + 0x1e874u)
						p = ROMBase + 0x1e874u;
					if (p >= ROMBase + 0x1e8beu && p < ROMBase + 0x1e912u)
						p = ROMBase + 0x1e912u;
					if (p >= ROMBase + 0x20c82u && p < ROMBase + 0x20c9au)
						p = ROMBase + 0x20c9au;
					if (p >= ROMBase + 0x20cc6u && p < ROMBase + 0x20ffeu)
						p = ROMBase + 0x20ffeu;
					if (p >= ROMBase + 0xf310u && p < ROMBase + 0xf600u)
						p = ROMBase + 0xf600u;
					if (p >= ROMBase + 0xf602u && p < ROMBase + 0xf620u)
						p = ROMBase + 0xf620u;
					if (p >= ROMBase + 0xfc10u && p < ROMBase + 0xfc34u)
						p = ROMBase + 0xfc34u;
					if (p >= ROMBase + 0x304c0u && p < ROMBase + 0x305c0u)
						p = ROMBase + 0x305c0u;
					if (p >= ROMBase + 0x289dcu && p < ROMBase + 0x28a04u)
						p = ROMBase + 0x28a04u;
					if (p >= ROMBase + 0x8fc6u && p < ROMBase + 0x8fc8u)
						p = ROMBase + 0x8fc8u;
					if (p >= ROMBase + 0x9010u && p < ROMBase + 0x9018u)
						p = ROMBase + 0x9018u;
					if (p >= ROMBase + 0x9038u && p < ROMBase + 0x9044u)
						p = ROMBase + 0x9044u;
					if (p >= ROMBase + 0x905eu && p < ROMBase + 0x9066u)
						p = ROMBase + 0x9066u;
					if (p >= ROMBase + 0x9088u && p < ROMBase + 0x908au)
						p = ROMBase + 0x908au;
					if (p >= ROMBase + 0x909cu && p < ROMBase + 0x90a8u)
						p = ROMBase + 0x90a8u;
					if (p >= ROMBase + 0x8a50u && p < ROMBase + 0x8a52u)
						p = ROMBase + 0x8a52u;
					if (p >= ROMBase + 0x70a6u && p < ROMBase + 0x7510u)
						p = ROMBase + 0x7510u;
					if (p >= ROMBase + 0x50ad0u && p < ROMBase + 0x50ae0u)
						p = ROMBase + 0x50ae0u;
					if (p >= ROMBase + 0x50e0au && p < ROMBase + 0x51100u)
						p = ROMBase + 0x51100u;
					if (p >= ROMBase + 0x5112au && p < ROMBase + 0x51130u)
						p = ROMBase + 0x51130u;
					if (p >= ROMBase + 0x51240u && p < ROMBase + 0x512d0u)
						p = ROMBase + 0x512d0u;
					if (p >= ROMBase + 0x4b1b0u && p < ROMBase + 0x4b1c0u)
						p = ROMBase + 0x4b1c0u;
					if (p >= ROMBase + 0x4211cu && p < ROMBase + 0x42130u)
						p = ROMBase + 0x42130u;
					if (p >= ROMBase + 0x81a3cu && p < ROMBase + 0x81b78u)
						p = ROMBase + 0x81b78u;
					if (p >= ROMBase + 0x81d02u && p < ROMBase + 0x81d80u)
						p = ROMBase + 0x81d80u;
					if (p >= ROMBase + 0x8192u && p < ROMBase + 0x81f0u)
						p = ROMBase + 0x81f0u;
					if (p >= ROMBase + 0x94c8u && p < ROMBase + 0x96a6u)
						p = ROMBase + 0x96a6u;
					if (p >= ROMBase + 0x91942u && p < ROMBase + 0x91980u)
						p = ROMBase + 0x91980u;
					if (p >= ROMBase + 0x95ee2u && p < ROMBase + 0x96788u)
						p = ROMBase + 0x96788u;
					if (p >= ROMBase + 0x14318u && p < ROMBase + 0x14620u)
						p = ROMBase + 0x14620u;
					if (p >= ROMBase + 0x15690u && p < ROMBase + 0x157a0u)
						p = ROMBase + 0x157a0u;
					if (p >= ROMBase + 0x137b2u && p < ROMBase + 0x138b0u)
						p = ROMBase + 0x138b0u;
					if (p >= ROMBase + 0x660d4u && p < ROMBase + 0x66158u)
						p = ROMBase + 0x66158u;
					if (p >= ROMBase + 0x81e8u && p < ROMBase + 0x81f0u)
						p = ROMBase + 0x81f0u;
					if (p >= ROMBase + 0x81f2u && p < ROMBase + 0x83a2u)
						p = ROMBase + 0x83a2u;
					if (p >= ROMBase + 0x8584u && p < ROMBase + 0x8592u)
						p = ROMBase + 0x8592u;
					if (p >= ROMBase + 0x49e0u && p < ROMBase + 0x49e2u)
						p = ROMBase + 0x49e2u;
					if (p >= ROMBase + 0x49f0u && p < ROMBase + 0x4d4au)
						p = ROMBase + 0x4d4au;
					if (p >= ROMBase + 0x4d52u && p < ROMBase + 0x4e30u)
						p = ROMBase + 0x4e30u;
					if (p >= ROMBase + 0x49b6u && p < ROMBase + 0x49dcu)
						p = ROMBase + 0x49dcu;
					if (p >= ROMBase + 0x4e32u && p < ROMBase + 0x4e86u)
						p = ROMBase + 0x4e86u;
					if (p >= ROMBase + 0x4f52u && p < ROMBase + 0x5078u)
						p = ROMBase + 0x5078u;
					if (p >= ROMBase + 0x4e88u && p < ROMBase + 0x4f50u)
						p = ROMBase + 0x4f50u;
					if (p >= ROMBase + 0x424au && p < ROMBase + 0x48b0u)
						p = ROMBase + 0x48b0u;
					if (p >= ROMBase + 0x498e2u && p < ROMBase + 0x499d6u)
						p = ROMBase + 0x499d6u;
					if (p >= ROMBase + 0x49e64u && p < ROMBase + 0x4a0e0u)
						p = ROMBase + 0x4a0e0u;
					if (p >= ROMBase + 0x67a84u && p < ROMBase + 0x67a90u)
						p = ROMBase + 0x67a90u;
					if (p >= ROMBase + 0x77d0u && p < ROMBase + 0x78d2u)
						p = ROMBase + 0x78d2u;
					if (p >= ROMBase + 0x7516u && p < ROMBase + 0x76b0u)
						p = ROMBase + 0x76b0u;
					if (p >= ROMBase + 0x1fa1eu && p < ROMBase + 0x1fa34u)
						p = ROMBase + 0x1fa34u;
					if (p >= ROMBase + 0x28922u && p < ROMBase + 0x28976u)
						p = ROMBase + 0x28976u;
					if (p >= ROMBase + 0x2014eu && p < ROMBase + 0x20160u)
						p = ROMBase + 0x20160u;
					if (p >= ROMBase + 0x9c56u && p < ROMBase + 0x9c80u)
						p = ROMBase + 0x9c80u;
					if (p >= ROMBase + 0x9c82u && p < ROMBase + 0xa008u)
						p = ROMBase + 0xa008u;
					if (p >= ROMBase + 0x20222u && p < ROMBase + 0x203dau)
						p = ROMBase + 0x203dau;
					if (p >= ROMBase + 0x1ffecu && p < ROMBase + 0x20000u)
						p = ROMBase + 0x20000u;
					if (p >= ROMBase + 0x20002u && p < ROMBase + 0x2012cu)
						p = ROMBase + 0x2012cu;
					if (p >= ROMBase + 0x2055au && p < ROMBase + 0x20574u)
						p = ROMBase + 0x20574u;
					if (p >= ROMBase + 0x2065cu && p < ROMBase + 0x208f8u)
						p = ROMBase + 0x20b80u;
					if (p >= ROMBase + 0x20902u && p < ROMBase + 0x20948u)
						p = ROMBase + 0x20b80u;
					if (p >= ROMBase + 0x20952u && p < ROMBase + 0x20b80u)
						p = ROMBase + 0x20b80u;
					if (p >= ROMBase + 0x20986u && p < ROMBase + 0x20b80u)
						p = ROMBase + 0x20b80u;
					if (p >= ROMBase + 0x20bdcu && p < ROMBase + 0x20c9au)
						p = ROMBase + 0x20c9au;
					if (p >= ROMBase + 0x9fd9au && p < ROMBase + 0xa08d0u)
						p = ROMBase + 0xa08d0u;
					if (p >= ROMBase + 0x1888au && p < ROMBase + 0x188dau)
						p = ROMBase + 0x188dau;
					if (p >= ROMBase + 0x188dcu && p < ROMBase + 0x18b60u)
						p = ROMBase + 0x18b60u;
					if (p >= ROMBase + 0x186feu && p < ROMBase + 0x18708u)
						p = ROMBase + 0x18708u;
					if (p >= ROMBase + 0x1a7d2u && p < ROMBase + 0x1a7f0u)
						p = ROMBase + 0x1a7f0u;
					if (p >= ROMBase + 0x129cu && p < ROMBase + 0x12c0u)
						p = ROMBase + 0x12c0u;
					if (p >= ROMBase + 0x1478u && p < ROMBase + 0x14c0u)
						p = ROMBase + 0x14c0u;
					if (p >= ROMBase + 0x1650u && p < ROMBase + 0x1670u)
						p = ROMBase + 0x1670u;
					if (p >= ROMBase + 0x7577au && p < ROMBase + 0x77240u)
						p = ROMBase + 0x77240u;
					if (p >= ROMBase + 0x2088au && p < ROMBase + 0x208f8u)
						p = ROMBase + 0x20b80u;
					if (p >= ROMBase + 0x5ae78u && p < ROMBase + 0x5be10u &&
					    !(p >= ROMBase + 0x5b110u && p < ROMBase + 0x5b166u))
						p = ROMBase + 0x5be10u;
					if (p >= ROMBase + 0x215c4u && p < ROMBase + 0x22394u)
						p = ROMBase + 0x22394u;
					if (p >= ROMBase + 0x2449au && p < ROMBase + 0x24c50u)
						p = ROMBase + 0x255f6u;
					if (p >= ROMBase + 0x22f02u && p < ROMBase + 0x22ffeu)
						p = ROMBase + 0x22ffeu;
					if (p >= ROMBase + 0x22cd2u && p < ROMBase + 0x24284u)
						p = ROMBase + 0x24e80u;
					if (p >= ROMBase + 0x224e2u && p < ROMBase + 0x24284u)
						p = ROMBase + 0x24e80u;
					if (p >= ROMBase + 0x24290u && p < ROMBase + 0x24c50u)
						p = ROMBase + 0x255f6u;
					if (p >= ROMBase + 0x24c52u && p < ROMBase + 0x24e80u)
						p = ROMBase + 0x255f6u;
					if (p >= ROMBase + 0x24e82u && p < ROMBase + 0x25490u)
						p = ROMBase + 0x25490u;
					if (p >= ROMBase + 0x16ed0u && p < ROMBase + 0x16f8cu)
						p = ROMBase + 0x16f8cu;
					if (p >= ROMBase + 0x210e4u && p < ROMBase + 0x21130u)
						p = ROMBase + 0x21130u;
					if (p >= ROMBase + 0x2116eu && p < ROMBase + 0x214dau)
						p = ROMBase + 0x214dau;
					if (p >= ROMBase + 0x572c6u && p < ROMBase + 0x58000u)
						p = ROMBase + 0x58000u;
					if (p >= ROMBase + 0x570ecu && p < ROMBase + 0x57100u)
						p = ROMBase + 0x57100u;
					if (p >= ROMBase + 0x5706eu && p < ROMBase + 0x571d0u)
						p = ROMBase + 0x571d0u;
					if (p >= ROMBase + 0x58666u && p < ROMBase + 0x58670u)
						p = ROMBase + 0x58670u;
					if (p >= ROMBase + 0x46e8au && p < ROMBase + 0x46ea0u)
						p = ROMBase + 0x46ea0u;
					if (p >= ROMBase + 0xa8818u && p < ROMBase + 0xa882eu)
						p = ROMBase + 0xa882eu;
					if (p >= ROMBase + 0xa8848u && p < ROMBase + 0xa884cu)
						p = ROMBase + 0xa884cu;
					if (p >= ROMBase + 0xa88c2u && p < ROMBase + 0xa88e2u)
						p = ROMBase + 0xa88e2u;
					if (p >= ROMBase + 0xa8900u && p < ROMBase + 0xa894au)
						p = ROMBase + 0xa894au;
					if (p >= ROMBase + 0x26614u && p < ROMBase + 0x26de0u)
						p = ROMBase + 0x26de0u;
					if (p >= ROMBase + 0x271d0u && p < ROMBase + 0x27620u)
						p = ROMBase + 0x27620u;
					if (p >= ROMBase + 0x2c858u && p < ROMBase + 0x2ca0eu)
						p = ROMBase + 0x2ca0eu;
					if (p >= ROMBase + 0x38b18u && p < ROMBase + 0x38b5eu)
						p = ROMBase + 0x38b5eu;
					if (p >= ROMBase + 0x1a8e0u && p < ROMBase + 0x1a902u)
						p = ROMBase + 0x1a902u;
					if (p >= ROMBase + 0xb6u && p < ROMBase + 0x112u)
						p = ROMBase + 0x112u;
					if (p >= ROMBase + 0x2b30au && p < ROMBase + 0x2b328u)
						p = ROMBase + 0x2b328u;
					if (p >= ROMBase + 0x2b32eu && p < ROMBase + 0x2b418u)
						p = ROMBase + 0x2b376u;
					if (p >= ROMBase + 0x2bdf0u && p < ROMBase + 0x2bec0u)
						p = ROMBase + 0x2bec0u;
					if (p >= ROMBase + 0x1e910u && p < ROMBase + 0x1e920u)
						p = ROMBase + 0x1e920u;
					if (p >= ROMBase + 0x1e8b2u && p < ROMBase + 0x1e8b8u)
						p = ROMBase + 0x1e8b8u;
					if (p >= ROMBase + 0x25496u && p < ROMBase + 0x255f6u)
						p = ROMBase + 0x255f6u;
					if (p >= ROMBase + 0x28b12u && p < ROMBase + 0x28b66u)
						p = ROMBase + 0x28b66u;
					if (p >= ROMBase + 0x214f2u && p < ROMBase + 0x21500u)
						p = ROMBase + 0x21500u;
					if (p >= ROMBase + 0x21516u && p < ROMBase + 0x21518u)
						p = ROMBase + 0x21518u;
					if (p >= ROMBase + 0x2153eu && p < ROMBase + 0x21540u)
						p = ROMBase + 0x21540u;
					if (p >= ROMBase + 0x21576u && p < ROMBase + 0x2158au)
						p = ROMBase + 0x2158au;
					if (p >= ROMBase + 0x21586u && p < ROMBase + 0x2158au)
						p = ROMBase + 0x2158au;
					if (p >= ROMBase + 0x2157eu && p < ROMBase + 0x2158au)
						p = ROMBase + 0x2158au;
					if (p >= ROMBase + 0x1e9d2u && p < ROMBase + 0x1ea48u)
						p = ROMBase + 0x1ea48u;
					if (p >= ROMBase + 0x252a6u && p < ROMBase + 0x252b0u)
						p = ROMBase + 0x252b0u;
					if (p >= ROMBase + 0x1ebecu && p < ROMBase + 0x1ebf0u)
						p = ROMBase + 0x1ebf0u;
					if (p >= ROMBase + 0x20f38u && p < ROMBase + 0x20ffeu)
						p = ROMBase + 0x20ffeu;
					if (p >= ROMBase + 0x1951au && p < ROMBase + 0x1951cu)
						p = ROMBase + 0x1951cu;
					if (p >= ROMBase + 0x75fcu && p < ROMBase + 0x7640u)
						p = ROMBase + 0x7640u;
					if (p >= ROMBase + 0x492au && p < ROMBase + 0x49dcu)
						p = ROMBase + 0x49dcu;
					if (p >= ROMBase + 0x40f2u && p < ROMBase + 0x4248u)
						p = ROMBase + 0x4248u;
					if (p >= ROMBase + 0x1ee22u && p < ROMBase + 0x1f7deu)
						p = ROMBase + 0x1f7deu;
					if (p >= ROMBase + 0x1f8f8u && p < ROMBase + 0x1f90cu)
						p = ROMBase + 0x1f90cu;
					if (p >= ROMBase + 0x5dcd2u && p < ROMBase + 0x61080u)
						p = ROMBase + 0x61080u;
					if (p >= ROMBase + 0x1dd3au && p < ROMBase + 0x1ddb2u)
						p = ROMBase + 0x1ddb2u;
					if (p >= ROMBase + 0x24c52u && p < ROMBase + 0x24e80u)
						p = ROMBase + 0x255f6u;
					if (p >= ROMBase + 0x26c52u && p < ROMBase + 0x26de0u)
						p = ROMBase + 0x26de0u;
					if (p >= ROMBase + 0xeac8u && p < ROMBase + 0xf170u)
						p = ROMBase + 0xf170u;
					if (p >= ROMBase + 0x160e2u && p < ROMBase + 0x16780u)
						p = ROMBase + 0x16780u;
					if (p >= ROMBase + 0x0fe9au && p < ROMBase + 0x13000u)
						p = ROMBase + 0x13000u;
					if (p >= ROMBase + 0x5080u && p < ROMBase + 0x6c94u)
						p = ROMBase + 0x6c94u;
					if (p >= ROMBase + 0xaec00u && p < ROMBase + 0xb77b4u)
						p = ROMBase + 0xb77b4u;
					if (p >= ROMBase + 0xc0000u && p < ROMBase + 0x198154u)
						p = ROMBase + 0x198154u;
					if (p >= ROMBase + 0x19cf62u && p < ROMBase + 0x1d2000u)
						p = ROMBase + 0x1dd38u;
					if (p >= ROMBase + 0x1d2000u && p < ROMBase + 0x400000u)
						p = ROMBase + 0x1dd38u;
					if (p >= ROMBase + 0x1cf22u && p < ROMBase + 0x1dd38u)
						p = ROMBase + 0x1dd38u;
					if (p >= ROMBase + 0x350000u && p < ROMBase + 0x400000u)
						p = ROMBase + 0x2au;
					if (!g3_r24_ok(p))
						p = g3_fix_r24(p);
					gpr(24) = p;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (op68 == 0x4e71u) {
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (op68 == 0x4efau) {
					const int32 d = (int16)vm_read_memory_2(r24);
					uint32 t = r24 + d;
					/* 2f30+RTS MixedMode table: JMP-through to
					 * planted RTS is RTS, not dest-fix onto
					 * ADDQ/LEA plants. */
					if (t >= ROMBase + 0x24c52u &&
					    t < ROMBase + 0x24e80u) {
						const uint32 sp = gpr(1);
						uint32 ret = g3_rom0(vm_read_memory_4(sp));
						gpr(1) = sp + 4;
						if (!g3_r24_ok(ret) ||
						    (ret >= ROMBase + 0x24c52u &&
						     ret < ROMBase + 0x24e80u))
							ret = ROMBase + 0x27620u;
						t = ret;
#if NW_BOOT_LOG
						{
							static unsigned njmm;
							if (njmm < 8) {
								njmm++;
								nw_boot_log("G3: 68k JMP MM table as RTS");
							}
						}
#endif
					} else if (!g3_r24_ok(t))
						t = g3_fix_r24(t);
					gpr(24) = t;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned njmpd;
						if (njmpd < 8) {
							njmpd++;
							char buf[80];
							snprintf(buf, sizeof(buf),
								 "G3: 68k JMP d16 pc=%08x",
								 (unsigned)gpr(24));
							nw_boot_log(buf);
						}
					}
#endif
					continue;
				}
				if (op68 == 0x4ef9u) {
					uint32 t = g3_rom0(vm_read_memory_4(r24));
					if (!g3_r24_ok(t))
						t = r24 + 4;
					gpr(24) = t;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (op68 == 0x4ebau) {
					const int32 d = (int16)vm_read_memory_2(r24);
					const uint32 ret = r24 + 2;
					uint32 t = r24 + d;
					if (!g3_r24_ok(t))
						t = ret;
					else {
						gpr(1) -= 4;
						vm_write_memory_4(gpr(1), ret);
					}
					gpr(24) = t;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned njsrd;
						if (njsrd < 8) {
							njsrd++;
							char buf[80];
							snprintf(buf, sizeof(buf),
								 "G3: 68k JSR d16 pc=%08x ret=%08x",
								 (unsigned)gpr(24), (unsigned)ret);
							nw_boot_log(buf);
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xfff8u) == 0x4ea8u) {
					const int an = (int)(op68 & 7u);
					const int32 d = (int16)vm_read_memory_2(r24);
					const uint32 ret = r24 + 2;
					uint32 t = g3_rom0(((an == 7) ? gpr(1)
								    : gpr(16 + an)) + d);
					if (!g3_r24_ok(t) || vm_read_memory_2(t) == 0)
						gpr(24) = ret;
					else {
						gpr(1) -= 4;
						vm_write_memory_4(gpr(1), ret);
						gpr(24) = t;
					}
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (op68 == 0x4eb9u) {
					const uint32 tgt = g3_rom0(vm_read_memory_4(r24));
					const uint32 ret = r24 + 4;
					if (!g3_r24_ok(tgt))
						gpr(24) = ret;
					else {
						gpr(1) -= 4;
						vm_write_memory_4(gpr(1), ret);
						gpr(24) = tgt;
					}
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x4e90u) {
					const int an = (int)(op68 & 7u);
					uint32 t = g3_rom0((an == 7) ? gpr(1)
							     : gpr(16 + an));
					const uint32 self = r24 - 2u;
					int self_jsr = 0;
					if (!g3_r24_ok(t) ||
					    vm_read_memory_2(t) == 0 ||
					    t == self) {
						self_jsr = (t == self);
						gpr(24) = r24;
					} else {
						gpr(1) -= 4;
						vm_write_memory_4(gpr(1), r24);
						gpr(24) = t;
					}
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					if (self_jsr) {
						static unsigned njsrs;
						if (njsrs < 8) {
							njsrs++;
							nw_boot_log("G3: 68k JSR self skip");
						}
					} else {
						static unsigned njsra;
						if (njsra < 8) {
							njsra++;
							char buf[80];
							snprintf(buf, sizeof(buf),
								 "G3: 68k JSR (A%d)=%08x",
								 an, (unsigned)gpr(24));
							nw_boot_log(buf);
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xffc0u) == 0x46c0u) {
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					if (sm == 3u) {
						uint32 a = (sr == 7u) ? gpr(1)
								     : gpr(16 + (int)sr);
						a += 2;
						if (sr == 7u)
							gpr(1) = a;
						else
							gpr(16 + (int)sr) = a;
					} else if (sm == 7u && sr == 4u)
						r24 += 2;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nsr2;
						if (nsr2 < 8) {
							nsr2++;
							nw_boot_log("G3: 68k MOVE ea,SR");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xfff8u) == 0x40c0u) {
					const int dn = (int)(op68 & 7u);
					gpr(8 + dn) = (gpr(8 + dn) & 0xffff0000u) |
						      0x2000u;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmsrd;
						if (nmsrd < 8) {
							nmsrd++;
							nw_boot_log("G3: 68k MOVE SR,Dn");
						}
					}
#endif
					continue;
				}
				if (op68 == 0x40e7u) {
					gpr(1) -= 2;
					if (g3_ea_data(gpr(1)))
						vm_write_memory_2(gpr(1), 0x2000u);
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmsrsp;
						if (nmsrsp < 8) {
							nmsrsp++;
							nw_boot_log("G3: 68k MOVE SR,-(SP)");
						}
					}
#endif
					continue;
				}
				/* MOVE (An)+,SR. VIA poll 0x9b6a 46df. */
				if ((op68 & 0xfff8u) == 0x46d8u) {
					const int an = (int)(op68 & 7u);
					uint32 a = (an == 7) ? gpr(1)
						     : gpr(16 + an);
					if (an == 7)
						gpr(1) = a + 2u;
					else
						gpr(16 + an) = a + 2u;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmsrr;
						if (nmsrr < 8) {
							nmsrr++;
							nw_boot_log("G3: 68k MOVE (An)+,SR");
						}
					}
#endif
					continue;
				}
				if (op68 == 0x40f8u) {
					const int32 a =
						(int16)vm_read_memory_2(r24);
					r24 += 2;
					if (g3_ea_data((uint32)a))
						vm_write_memory_2(
							g3_rom0((uint32)a), 0x2000u);
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmsraw;
						if (nmsraw < 8) {
							nmsraw++;
							nw_boot_log("G3: 68k MOVE SR,abs.W");
						}
					}
#endif
					continue;
				}
				if (op68 == 0x48f8u) {
					const uint32 mask =
						vm_read_memory_2(r24);
					r24 += 2;
					const int32 a =
						(int16)vm_read_memory_2(r24);
					r24 += 2;
					uint32 addr = g3_rom0((uint32)a);
					if (g3_ea_data(addr)) {
						unsigned i;
						for (i = 0; i < 16u; i++) {
							if ((mask & (1u << i)) == 0)
								continue;
							uint32 v;
							if (i < 8u)
								v = gpr(8 + (int)i);
							else if (i == 15u)
								v = gpr(1);
							else
								v = gpr(8 + (int)i);
							vm_write_memory_2(addr,
								(uint16)v);
							addr += 2;
						}
					}
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmovemw;
						if (nmovemw < 8) {
							nmovemw++;
							nw_boot_log("G3: 68k MOVEM.W abs.W");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xfff8u) == 0x0040u) {
					const int dn = (int)(op68 & 7u);
					const uint32 imm = vm_read_memory_2(r24);
					r24 += 2;
					uint32 v = (gpr(8 + dn) | imm) & 0xffffu;
					gpr(8 + dn) = (gpr(8 + dn) & 0xffff0000u) | v;
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (op68 == 0x007cu) {
					r24 += 2;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned norisr;
						if (norisr < 8) {
							norisr++;
							nw_boot_log("G3: 68k ORI SR");
						}
					}
#endif
					continue;
				}
				if (op68 == 0x46fcu || op68 == 0x44fcu) {
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nsr;
						if (nsr < 4) {
							nsr++;
							nw_boot_log("G3: 68k MOVE SR skip");
						}
					}
#endif
					continue;
				}
				if (op68 == 0x4e70u) {
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nrst;
						if (nrst < 4) {
							nrst++;
							nw_boot_log("G3: 68k RESET nop");
						}
					}
#endif
					continue;
				}
				if (op68 == 0x4e7bu) {
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmovec;
						if (nmovec < 4) {
							nmovec++;
							nw_boot_log("G3: 68k MOVEC nop");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf000u) == 0x1000u) {
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					const unsigned dm = (op68 >> 6) & 7u;
					const unsigned dr = (op68 >> 9) & 7u;
					uint32 srca = (sr == 7u) ? gpr(1)
						: gpr(16 + (int)sr);
					uint32 dsta = (dr == 7u) ? gpr(1)
						: gpr(16 + (int)dr);
					int hb = 0;
					uint32 v = 0;
					if (sm == 0u) {
						v = gpr(8 + (int)sr);
						hb = 1;
					} else if (sm == 1u) {
						v = srca;
						hb = 1;
					} else if (sm == 2u) {
						v = vm_read_memory_1(g3_rom0(srca));
						hb = 1;
					} else if (sm == 3u) {
						v = vm_read_memory_1(g3_rom0(srca));
						srca += 1;
						if (sr == 7u)
							gpr(1) = srca;
						else
							gpr(16 + (int)sr) = srca;
						hb = 1;
					} else if (sm == 4u) {
						srca -= 1;
						if (sr == 7u)
							gpr(1) = srca;
						else
							gpr(16 + (int)sr) = srca;
						v = vm_read_memory_1(g3_rom0(srca));
						hb = 1;
					} else if (sm == 5u) {
						const int32 d =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						v = vm_read_memory_1(
							g3_rom0(srca + d));
						hb = 1;
					} else if (sm == 7u && sr == 4u) {
						v = vm_read_memory_2(r24) & 0xffu;
						r24 += 2;
						hb = 1;
					} else if (sm == 7u && sr == 0u) {
						const int32 a =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						v = vm_read_memory_1(
							g3_rom0((uint32)a));
						hb = 1;
					}
					if (hb) {
						if (dm == 0u) {
							gpr(8 + (int)dr) =
								(gpr(8 + (int)dr) & 0xffffff00u) |
								(v & 0xffu);
						} else if (dm == 2u) {
							if (g3_ea_data(dsta))
								vm_write_memory_1(
									g3_rom0(dsta),
									v & 0xffu);
						} else if (dm == 3u) {
							if (g3_ea_data(dsta))
								vm_write_memory_1(
									g3_rom0(dsta),
									v & 0xffu);
							dsta += 1;
							if (dr == 7u)
								gpr(1) = dsta;
							else
								gpr(16 + (int)dr) = dsta;
						} else if (dm == 5u) {
							const int32 d =
								(int16)vm_read_memory_2(r24);
							r24 += 2;
							if (g3_ea_data(dsta + d))
								vm_write_memory_1(
									g3_rom0(dsta + d),
									v & 0xffu);
						} else if (dm == 7u && dr == 0u) {
							const int32 a =
								(int16)vm_read_memory_2(r24);
							r24 += 2;
							if (g3_ea_data((uint32)a))
								vm_write_memory_1(
									g3_rom0((uint32)a),
									v & 0xffu);
						} else if (dm == 4u) {
							dsta -= 1;
							if (dr == 7u)
								gpr(1) = dsta;
							else
								gpr(16 + (int)dr) = dsta;
							if (g3_ea_data(dsta))
								vm_write_memory_1(
									g3_rom0(dsta),
									v & 0xffu);
						} else if (dm == 6u) {
							const uint32 ext =
								vm_read_memory_2(r24);
							r24 += 2;
							const int da = (int)((ext >> 15) & 1u);
							const int xr = (int)((ext >> 12) & 7u);
							const int wl = (int)((ext >> 11) & 1u);
							const int sc = (int)((ext >> 9) & 3u);
							uint32 xn = da ? ((xr == 7) ? gpr(1)
									       : gpr(16 + xr))
								      : gpr(8 + xr);
							if (!wl)
								xn = (uint32)(int32)(int16)xn;
							xn <<= sc;
							uint32 a = dsta;
							if ((ext & 0x100u) == 0) {
								a = dsta +
								    (int32)(int8)(ext & 0xffu) + xn;
							} else {
								const int bs = (int)((ext >> 7) & 1u);
								const int isup = (int)((ext >> 6) & 1u);
								const int bdsz = (int)((ext >> 4) & 3u);
								const int iis = (int)(ext & 7u);
								uint32 bd = 0;
								if (bdsz == 2) {
									bd = (uint32)(int32)(int16)
										vm_read_memory_2(r24);
									r24 += 2;
								} else if (bdsz == 3) {
									bd = vm_read_memory_4(r24);
									r24 += 4;
								}
								uint32 od = 0;
								if (iis == 2 || iis == 3 ||
								    iis == 6 || iis == 7) {
									od = (uint32)(int32)(int16)
										vm_read_memory_2(r24);
									r24 += 2;
								} else if (iis == 4) {
									od = vm_read_memory_4(r24);
									r24 += 4;
								}
								uint32 inner = (bs ? 0 : dsta) + bd;
								if (!isup && iis < 6)
									inner += xn;
								if (iis >= 2)
									inner = vm_read_memory_4(inner);
								if (!isup && iis >= 6)
									inner += xn;
								a = inner + od;
							}
							if (g3_ea_data(a))
								vm_write_memory_1(
									g3_rom0(a),
									v & 0xffu);
#if NW_BOOT_LOG
							{
								static unsigned nmbxf;
								if (nmbxf < 8) {
									nmbxf++;
									nw_boot_log("G3: 68k MOVE.B idx full");
								}
							}
#endif
						} else
							hb = 0;
					}
					if (hb) {
						g3_ccr = 0;
						if ((v & 0xffu) == 0)
							g3_ccr |= 4;
						if ((int8)v < 0)
							g3_ccr |= 8;
						gpr(24) = r24;
						gpr(27) = 0xffffffffu;
						gpr(29) = ROMBase + 0x380000u;
						pc() = ROMBase + 0x366084u;
						continue;
					}
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 3u &&
				    ((op68 >> 6) & 7u) == 7u &&
				    ((op68 >> 9) & 7u) <= 1u) {
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					const unsigned dr = (op68 >> 9) & 7u;
					uint32 v;
					if (sm == 0u)
						v = gpr(8 + (int)sr);
					else if (sm == 1u)
						v = (sr == 7u) ? gpr(1)
							       : gpr(16 + (int)sr);
					else
						v = 0;
					v &= 0xffffu;
					uint32 a;
					if (dr == 0u) {
						a = (uint32)(int32)(int16)
							vm_read_memory_2(r24);
						r24 += 2;
					} else {
						a = vm_read_memory_4(r24);
						r24 += 4;
					}
					if (g3_ea_data(a))
						vm_write_memory_2(g3_rom0(a), v);
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 3u &&
				    ((op68 >> 6) & 7u) == 0u &&
				    ((op68 >> 3) & 7u) <= 3u) {
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					const unsigned dr = (op68 >> 9) & 7u;
					uint32 addr = (sr == 7u) ? gpr(1)
						: gpr(16 + (int)sr);
					uint32 v;
					if (sm <= 1u)
						v = (sm == 0u) ? gpr(8 + (int)sr)
							       : addr;
					else {
						v = vm_read_memory_2(g3_rom0(addr));
						if (sm == 3u) {
							if (sr == 7u)
								gpr(1) = addr + 2;
							else
								gpr(16 + (int)sr) = addr + 2;
						}
					}
					v &= 0xffffu;
					gpr(8 + (int)dr) =
						(gpr(8 + (int)dr) & 0xffff0000u) | v;
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 3u &&
				    ((op68 >> 3) & 7u) == 3u &&
				    (((op68 >> 6) & 7u) == 2u ||
				     ((op68 >> 6) & 7u) == 3u)) {
					const unsigned sr = op68 & 7u;
					const unsigned dr = (op68 >> 9) & 7u;
					const unsigned dm = (op68 >> 6) & 7u;
					uint32 sa = (sr == 7u) ? gpr(1)
							 : gpr(16 + (int)sr);
					uint32 v = 0;
					if (g3_ea_data(sa))
						v = vm_read_memory_2(g3_rom0(sa));
					sa += 2;
					if (sr == 7u)
						gpr(1) = sa;
					else
						gpr(16 + (int)sr) = sa;
					uint32 da = (dr == 7u) ? gpr(1)
							 : gpr(16 + (int)dr);
					if (g3_ea_data(da))
						vm_write_memory_2(g3_rom0(da), v);
					if (dm == 3u) {
						da += 2;
						if (dr == 7u)
							gpr(1) = da;
						else
							gpr(16 + (int)dr) = da;
					}
					g3_ccr = 0;
					if ((v & 0xffffu) == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmwpp;
						if (nmwpp < 8) {
							nmwpp++;
							nw_boot_log(dm == 3u
								    ? "G3: 68k MOVE.W (An)+,(An)+"
								    : "G3: 68k MOVE.W (An)+,(An)");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 2u &&
				    ((op68 >> 3) & 7u) == 3u &&
				    ((op68 >> 6) & 7u) == 3u) {
					const unsigned sr = op68 & 7u;
					const unsigned dr = (op68 >> 9) & 7u;
					uint32 sa = (sr == 7u) ? gpr(1)
							 : gpr(16 + (int)sr);
					uint32 v = 0;
					if (g3_ea_data(sa))
						v = vm_read_memory_4(g3_rom0(sa));
					sa += 4;
					if (sr == 7u)
						gpr(1) = sa;
					else
						gpr(16 + (int)sr) = sa;
					uint32 da = (dr == 7u) ? gpr(1)
							 : gpr(16 + (int)dr);
					if (g3_ea_data(da))
						vm_write_memory_4(g3_rom0(da), v);
					da += 4;
					if (dr == 7u)
						gpr(1) = da;
					else
						gpr(16 + (int)dr) = da;
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int32)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmlpp;
						if (nmlpp < 8) {
							nmlpp++;
							nw_boot_log("G3: 68k MOVE.L (An)+,(An)+");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 2u &&
				    ((op68 >> 3) & 7u) == 3u &&
				    ((op68 >> 6) & 7u) == 0u) {
					const unsigned sr = op68 & 7u;
					const unsigned dr = (op68 >> 9) & 7u;
					uint32 sa = (sr == 7u) ? gpr(1)
							 : gpr(16 + (int)sr);
					uint32 v = 0;
					if (g3_ea_data(sa))
						v = vm_read_memory_4(g3_rom0(sa));
					sa += 4;
					if (sr == 7u)
						gpr(1) = sa;
					else
						gpr(16 + (int)sr) = sa;
					gpr(8 + (int)dr) = v;
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int32)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 2u &&
				    ((op68 >> 3) & 7u) == 0u &&
				    ((op68 >> 6) & 7u) == 3u) {
					const unsigned sr = op68 & 7u;
					const unsigned dr = (op68 >> 9) & 7u;
					uint32 v = gpr(8 + (int)sr);
					uint32 da = (dr == 7u) ? gpr(1)
							 : gpr(16 + (int)dr);
					if (g3_ea_data(da))
						vm_write_memory_4(g3_rom0(da), v);
					da += 4;
					if (dr == 7u)
						gpr(1) = da;
					else
						gpr(16 + (int)dr) = da;
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int32)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 3u &&
				    ((op68 >> 3) & 7u) <= 1u &&
				    ((op68 >> 6) & 7u) <= 1u) {
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					const unsigned dm = (op68 >> 6) & 7u;
					const unsigned dr = (op68 >> 9) & 7u;
					uint32 v;
					if (sm == 0u)
						v = gpr(8 + (int)sr);
					else
						v = (sr == 7u) ? gpr(1)
							       : gpr(16 + (int)sr);
					v &= 0xffffu;
					if (dm == 0u)
						gpr(8 + (int)dr) =
							(gpr(8 + (int)dr) & 0xffff0000u) | v;
					else if (dr == 7u)
						gpr(1) = (uint32)(int32)(int16)v;
					else
						gpr(16 + (int)dr) =
							(uint32)(int32)(int16)v;
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 3u &&
				    ((op68 >> 6) & 7u) == 0u &&
				    ((op68 >> 3) & 7u) == 5u) {
					const unsigned sr = op68 & 7u;
					const unsigned dr = (op68 >> 9) & 7u;
					const int32 d =
						(int16)vm_read_memory_2(r24);
					r24 += 2;
					const uint32 a = ((sr == 7u) ? gpr(1)
							     : gpr(16 + (int)sr)) + d;
					const uint32 v = (uint32)(int32)(int16)
						vm_read_memory_2(g3_rom0(a));
					gpr(8 + (int)dr) = v;
					g3_ccr = 0;
					if ((v & 0xffffu) == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf1ffu) == 0x303bu) {
					const int dn = (int)((op68 >> 9) & 7u);
					const uint32 ext = vm_read_memory_2(r24);
					const int da = (int)((ext >> 15) & 1u);
					const int xr = (int)((ext >> 12) & 7u);
					const int wl = (int)((ext >> 11) & 1u);
					const int32 disp = (int8)(ext & 0xffu);
					uint32 xn = da ? ((xr == 7) ? gpr(1)
							       : gpr(16 + xr))
						      : gpr(8 + xr);
					if (!wl)
						xn = (uint32)(int32)(int16)xn;
					xn <<= (int)((ext >> 9) & 3u);
					{
						const uint32 raw = g3_pc0(r24) + disp + xn;
						uint32 addr = g3_rom0(raw);
						if (raw >= 0x3000u && raw < 0x400000u &&
						    (!g3_ea_data(addr) ||
						     vm_read_memory_2(addr) == 0))
							addr = ROMBase + raw;
						const uint32 v = (uint32)(int32)(int16)
							vm_read_memory_2(addr);
						gpr(8 + dn) = v;
						g3_ccr = 0;
						if ((v & 0xffffu) == 0)
							g3_ccr |= 4;
						if ((int16)v < 0)
							g3_ccr |= 8;
					}
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 3u &&
				    ((op68 >> 6) & 7u) == 3u &&
				    ((op68 >> 3) & 7u) == 7u &&
				    (op68 & 7u) == 0u) {
					const unsigned dr = (op68 >> 9) & 7u;
					const int32 a =
						(int16)vm_read_memory_2(r24);
					r24 += 2;
					const uint32 v = vm_read_memory_2(
						g3_rom0((uint32)a));
					uint32 dsta = (dr == 7u) ? gpr(1)
							: gpr(16 + (int)dr);
					if (g3_ea_data(dsta))
						vm_write_memory_2(g3_rom0(dsta), v);
					if (dr == 7u)
						gpr(1) = dsta + 2;
					else
						gpr(16 + (int)dr) = dsta + 2;
					g3_ccr = 0;
					if ((v & 0xffffu) == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf1c0u) == 0x1000u &&
				    ((op68 >> 3) & 7u) == 0u) {
					/* MOVE.B Dn,Dd. 0x1c4be 1801
					 * dest-edged as unhosted. */
					const int dn = (int)((op68 >> 9) & 7u);
					const int sn = (int)(op68 & 7u);
					const uint32 v = gpr(8 + sn) & 0xffu;
					gpr(8 + dn) = (gpr(8 + dn) & 0xffffff00u) | v;
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int8)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmbdn;
						if (nmbdn < 8) {
							nmbdn++;
							nw_boot_log("G3: 68k MOVE.B Dn,Dn");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1ffu) == 0x103bu) {
					/* MOVE.B d8(PC,Xn),Dn. 0x16d9c 103b
					 * dest-edged onto 0044 (unhosted 30). */
					const int dn = (int)((op68 >> 9) & 7u);
					const uint32 ext = vm_read_memory_2(r24);
					const uint32 pca = r24;
					r24 += 2;
					const int da = (int)((ext >> 15) & 1u);
					const int xr = (int)((ext >> 12) & 7u);
					const int wl = (int)((ext >> 11) & 1u);
					uint32 xn = da ? ((xr == 7) ? gpr(1)
							       : gpr(16 + xr))
						      : gpr(8 + xr);
					if (!wl)
						xn = (uint32)(int32)(int16)xn;
					const uint32 a = pca +
						(int32)(int8)(ext & 0xffu) + xn;
					uint32 v = 0;
					if (g3_ea_data(a))
						v = vm_read_memory_1(g3_rom0(a));
					gpr(8 + dn) = (gpr(8 + dn) & 0xffffff00u) |
						      (v & 0xffu);
					g3_ccr = 0;
					if ((v & 0xffu) == 0)
						g3_ccr |= 4;
					if ((int8)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmbpc;
						if (nmbpc < 8) {
							nmbpc++;
							nw_boot_log("G3: 68k MOVE.B d8(PC,Xn),Dn");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1ffu) == 0x1038u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const int32 a =
						(int16)vm_read_memory_2(r24);
					const uint32 v = vm_read_memory_1(g3_rom0((uint32)a));
					gpr(8 + dn) = (gpr(8 + dn) & 0xffffff00u) | (v & 0xffu);
					g3_ccr = 0;
					if ((v & 0xffu) == 0)
						g3_ccr |= 4;
					if ((int8)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf1ffu) == 0x2038u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const int32 a =
						(int16)vm_read_memory_2(r24);
					uint32 v;
					if (r24 - 2u == ROMBase + 0x5b11au &&
					    (uint16)a == 0x08a8u)
						v = RAMBase + 0xd800u;
					else
						v = vm_read_memory_4(
							g3_rom0((uint32)a));
					gpr(8 + dn) = v;
					g3_ccr = 0;
					if (gpr(8 + dn) == 0)
						g3_ccr |= 4;
					if ((int32)gpr(8 + dn) < 0)
						g3_ccr |= 8;
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					if (r24 - 2u == ROMBase + 0x5b11au) {
						static unsigned n8a8m;
						if (n8a8m < 8) {
							n8a8m++;
							nw_boot_log("G3: 68k $08A8 at 0x5b11a");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1ffu) == 0x20bcu) {
					const int an = (int)((op68 >> 9) & 7u);
					const uint32 imm = vm_read_memory_4(r24);
					r24 += 4;
					uint32 a = (an == 7) ? gpr(1)
							     : gpr(16 + an);
					a -= 4u;
					if (g3_ea_data(a))
						vm_write_memory_4(g3_rom0(a),
								  imm);
					if (an == 7)
						gpr(1) = a;
					else
						gpr(16 + an) = a;
					g3_ccr = 0;
					if (imm == 0)
						g3_ccr |= 4;
					if ((int32)imm < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmlimm;
						if (nmlimm < 8) {
							nmlimm++;
							nw_boot_log("G3: 68k MOVE.L #imm,-(An)");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1ffu) == 0x20b8u) {
					/* MOVE.L abs.W,(An). 0x54186 2eb8
					 * $02A6 dest-edged onto 02a6. */
					const int an = (int)((op68 >> 9) & 7u);
					const int32 a16 =
						(int16)vm_read_memory_2(r24);
					r24 += 2;
					uint32 v = 0;
					if (g3_ea_data((uint32)a16))
						v = vm_read_memory_4(
							g3_rom0((uint32)a16));
					uint32 da = (an == 7) ? gpr(1)
							      : gpr(16 + an);
					if (g3_ea_data(da))
						vm_write_memory_4(g3_rom0(da),
								  v);
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int32)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmlabs;
						if (nmlabs < 8) {
							nmlabs++;
							nw_boot_log("G3: 68k MOVE.L abs.W,(An)");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1ffu) == 0x30b8u) {
					/* MOVE.W abs.W,(An). 0x523fe 3eb8
					 * $0220 dest-edged after NewRgn. */
					const int an = (int)((op68 >> 9) & 7u);
					const int32 a16 =
						(int16)vm_read_memory_2(r24);
					r24 += 2;
					uint32 v = 0;
					if (g3_ea_data((uint32)a16))
						v = vm_read_memory_2(
							g3_rom0((uint32)a16));
					uint32 da = (an == 7) ? gpr(1)
							      : gpr(16 + an);
					if (g3_ea_data(da))
						vm_write_memory_2(g3_rom0(da),
								  (uint16)v);
					g3_ccr = 0;
					if ((v & 0xffffu) == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmwabs;
						if (nmwabs < 8) {
							nmwabs++;
							nw_boot_log("G3: 68k MOVE.W abs.W,(An)");
						}
					}
#endif
					continue;
				}
				/* MOVE.L A6,$000c(A4) at 0x1fe6a. Dest-edges
				 * onto 000c if unhosted. Do not skip 0x1fe6a
				 * (tried with KEEP 21df; bits 0). Do not host
				 * global MOVE.L ea,d16(An) store (GetCatInfo
				 * mill). PC-only. Then BCLR (A4) runs. */
				if (r24 - 2u == ROMBase + 0x1fe6au &&
				    op68 == 0x294eu) {
					const int32 d =
						(int16)vm_read_memory_2(r24);
					r24 += 2;
					uint32 da = gpr(20) + d;
					const uint32 v = gpr(22);
					if (g3_ea_data(da))
						vm_write_memory_4(g3_rom0(da),
								  v);
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int32)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nml1fe;
						if (nml1fe < 8) {
							nml1fe++;
							nw_boot_log("G3: 68k MOVE.L A6,d16(A4) 0x1fe6a");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1c0u) == 0x2000u &&
				    ((op68 >> 3) & 7u) == 5u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const int an = (int)(op68 & 7u);
					const int32 d =
						(int16)vm_read_memory_2(r24);
					uint32 a = ((an == 7) ? gpr(1)
							 : gpr(16 + an)) + d;
					uint32 v = 0;
					if (g3_ea_data(a))
						v = vm_read_memory_4(g3_rom0(a));
					gpr(8 + dn) = v;
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int32)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmld16;
						if (nmld16 < 8) {
							nmld16++;
							nw_boot_log("G3: 68k MOVE.L d16(An),Dn");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1c0u) == 0x3000u &&
				    ((op68 >> 6) & 7u) == 0u &&
				    ((op68 >> 3) & 7u) == 5u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const int an = (int)(op68 & 7u);
					const int32 d =
						(int16)vm_read_memory_2(r24);
					uint32 a = ((an == 7) ? gpr(1)
							     : gpr(16 + an)) + d;
					uint32 v = 0;
					if (g3_ea_data(a))
						v = (uint32)(int32)(int16)
							vm_read_memory_2(g3_rom0(a));
					gpr(8 + dn) = v;
					g3_ccr = 0;
					if ((v & 0xffffu) == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmwd16;
						if (nmwd16 < 8) {
							nmwd16++;
							nw_boot_log("G3: 68k MOVE.W d16(An),Dn");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1ffu) == 0x3038u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const int32 a =
						(int16)vm_read_memory_2(r24);
					const uint32 v = (uint32)(int32)(int16)
						vm_read_memory_2(g3_rom0((uint32)a));
					gpr(8 + dn) = v;
					g3_ccr = 0;
					if ((v & 0xffffu) == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf1ffu) == 0x313cu) {
					const unsigned dr = (op68 >> 9) & 7u;
					const uint32 v = vm_read_memory_2(r24);
					r24 += 2;
					uint32 da = ((dr == 7u) ? gpr(1)
							     : gpr(16 + (int)dr)) - 2u;
					if (dr == 7u)
						gpr(1) = da;
					else
						gpr(16 + (int)dr) = da;
					if (g3_ea_data(da))
						vm_write_memory_2(g3_rom0(da), v);
					g3_ccr = 0;
					if ((v & 0xffffu) == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf1ffu) == 0x303cu) {
					const int dn = (int)((op68 >> 9) & 7u);
					const uint32 v = (uint32)(int32)(int16)
						vm_read_memory_2(r24);
					gpr(8 + dn) = v;
					g3_ccr = 0;
					if ((v & 0xffffu) == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf1ffu) == 0x307cu) {
					const int an = (int)((op68 >> 9) & 7u);
					const int32 imm = (int16)vm_read_memory_2(r24);
					if (an == 7)
						gpr(1) = (uint32)imm;
					else
						gpr(16 + an) = (uint32)imm;
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmwa;
						if (nmwa < 8) {
							nmwa++;
							char buf[80];
							snprintf(buf, sizeof(buf),
								 "G3: 68k MOVE.W #A%d=%08x",
								 an, (unsigned)(an == 7 ? gpr(1)
										       : gpr(16 + an)));
							nw_boot_log(buf);
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1ffu) == 0x207cu) {
					const int an = (int)((op68 >> 9) & 7u);
					const uint32 imm = vm_read_memory_4(r24);
					if (an == 7)
						gpr(1) = imm;
					else
						gpr(16 + an) = imm;
					gpr(24) = r24 + 4;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 3u &&
				    ((op68 >> 6) & 7u) == 5u &&
				    ((op68 >> 3) & 7u) == 5u) {
					const int32 ds =
						(int16)vm_read_memory_2(r24);
					const int32 dd =
						(int16)vm_read_memory_2(r24 + 2);
					const unsigned sr = op68 & 7u;
					const unsigned dr = (op68 >> 9) & 7u;
					uint32 sa = ((sr == 7u) ? gpr(1)
							       : gpr(16 + (int)sr)) + ds;
					uint32 da = ((dr == 7u) ? gpr(1)
							       : gpr(16 + (int)dr)) + dd;
					uint32 v = 0;
					if (g3_ea_data(sa))
						v = vm_read_memory_2(g3_rom0(sa));
					if (g3_ea_data(da))
						vm_write_memory_2(g3_rom0(da), v);
					g3_ccr = 0;
					if ((v & 0xffffu) == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24 + 4;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 3u &&
				    ((op68 >> 6) & 7u) == 4u &&
				    ((op68 >> 3) & 7u) == 7u &&
				    (op68 & 7u) == 0u) {
					const int32 abs =
						(int16)vm_read_memory_2(r24);
					r24 += 2;
					const unsigned dr = (op68 >> 9) & 7u;
					uint32 da = ((dr == 7u) ? gpr(1)
							     : gpr(16 + (int)dr)) - 2u;
					if (dr == 7u)
						gpr(1) = da;
					else
						gpr(16 + (int)dr) = da;
					const uint32 v =
						vm_read_memory_2(g3_rom0((uint32)abs));
					if (g3_ea_data(da))
						vm_write_memory_2(g3_rom0(da), v);
					g3_ccr = 0;
					if ((v & 0xffffu) == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 3u &&
				    ((op68 >> 6) & 7u) == 4u &&
				    ((op68 >> 3) & 7u) == 4u) {
					const unsigned sr = op68 & 7u;
					const unsigned dr = (op68 >> 9) & 7u;
					uint32 sa = ((sr == 7u) ? gpr(1)
							       : gpr(16 + (int)sr)) - 2u;
					if (sr == 7u)
						gpr(1) = sa;
					else
						gpr(16 + (int)sr) = sa;
					uint32 da = ((dr == 7u) ? gpr(1)
							     : gpr(16 + (int)dr)) - 2u;
					if (dr == 7u)
						gpr(1) = da;
					else
						gpr(16 + (int)dr) = da;
					uint32 v = 0;
					if (g3_ea_data(sa))
						v = vm_read_memory_2(g3_rom0(sa));
					if (g3_ea_data(da))
						vm_write_memory_2(g3_rom0(da), v);
					g3_ccr = 0;
					if ((v & 0xffffu) == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 2u &&
				    ((op68 >> 6) & 7u) == 4u &&
				    ((op68 >> 3) & 7u) == 4u) {
					const unsigned sr = op68 & 7u;
					const unsigned dr = (op68 >> 9) & 7u;
					uint32 sa = ((sr == 7u) ? gpr(1)
							       : gpr(16 + (int)sr)) - 4u;
					if (sr == 7u)
						gpr(1) = sa;
					else
						gpr(16 + (int)sr) = sa;
					uint32 da = ((dr == 7u) ? gpr(1)
							     : gpr(16 + (int)dr)) - 4u;
					if (dr == 7u)
						gpr(1) = da;
					else
						gpr(16 + (int)dr) = da;
					uint32 v = 0;
					if (g3_ea_data(sa))
						v = vm_read_memory_4(g3_rom0(sa));
					if (g3_ea_data(da))
						vm_write_memory_4(g3_rom0(da), v);
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int32)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 3u &&
				    ((op68 >> 6) & 7u) == 3u &&
				    ((op68 >> 3) & 7u) == 4u) {
					const unsigned sr = op68 & 7u;
					const unsigned dr = (op68 >> 9) & 7u;
					uint32 sa = ((sr == 7u) ? gpr(1)
							       : gpr(16 + (int)sr)) - 2u;
					if (sr == 7u)
						gpr(1) = sa;
					else
						gpr(16 + (int)sr) = sa;
					uint32 da = (dr == 7u) ? gpr(1)
							      : gpr(16 + (int)dr);
					uint32 v = 0;
					if (g3_ea_data(sa))
						v = vm_read_memory_2(g3_rom0(sa));
					if (g3_ea_data(da))
						vm_write_memory_2(g3_rom0(da), v);
					da += 2;
					if (dr == 7u)
						gpr(1) = da;
					else
						gpr(16 + (int)dr) = da;
					g3_ccr = 0;
					if ((v & 0xffffu) == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 3u &&
				    ((op68 >> 3) & 7u) == 7u &&
				    (op68 & 7u) == 0u &&
				    ((op68 >> 6) & 7u) == 5u) {
					const int32 abs =
						(int16)vm_read_memory_2(r24);
					const int32 d =
						(int16)vm_read_memory_2(r24 + 2);
					r24 += 4;
					const unsigned dr = (op68 >> 9) & 7u;
					uint32 da = ((dr == 7u) ? gpr(1)
							     : gpr(16 + (int)dr)) + d;
					const uint32 v =
						vm_read_memory_2(g3_rom0((uint32)abs));
					if (g3_ea_data(da))
						vm_write_memory_2(g3_rom0(da), v);
					g3_ccr = 0;
					if ((v & 0xffffu) == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmwad;
						if (nmwad < 8) {
							nmwad++;
							nw_boot_log("G3: 68k MOVE.W abs,d16");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 3u &&
				    ((op68 >> 3) & 7u) == 5u &&
				    ((op68 >> 6) & 7u) == 7u &&
				    ((op68 >> 9) & 7u) == 0u) {
					const unsigned sr = op68 & 7u;
					const int32 d =
						(int16)vm_read_memory_2(r24);
					const int32 abs =
						(int16)vm_read_memory_2(r24 + 2);
					r24 += 4;
					const uint32 sa = ((sr == 7u) ? gpr(1)
							     : gpr(16 + (int)sr)) + d;
					uint32 v = 0;
					if (g3_ea_data(sa))
						v = vm_read_memory_2(g3_rom0(sa));
					if (g3_ea_data((uint32)abs))
						vm_write_memory_2(g3_rom0((uint32)abs), v);
					g3_ccr = 0;
					if ((v & 0xffffu) == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmwda;
						if (nmwda < 8) {
							nmwda++;
							nw_boot_log("G3: 68k MOVE.W d16,abs.W");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 3u &&
				    ((op68 >> 3) & 7u) == 0u &&
				    ((op68 >> 6) & 7u) == 5u) {
					const unsigned sr = op68 & 7u;
					const unsigned dr = (op68 >> 9) & 7u;
					const int32 d =
						(int16)vm_read_memory_2(r24);
					r24 += 2;
					uint32 v = gpr(8 + (int)sr) & 0xffffu;
					uint32 da = ((dr == 7u) ? gpr(1)
							     : gpr(16 + (int)dr)) + d;
					if (g3_ea_data(da))
						vm_write_memory_2(g3_rom0(da), v);
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmwd16;
						if (nmwd16 < 8) {
							nmwd16++;
							nw_boot_log("G3: 68k MOVE.W Dn,d16(An)");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 3u &&
				    ((op68 >> 3) & 7u) == 7u &&
				    (op68 & 7u) == 0u &&
				    ((op68 >> 6) & 7u) == 2u) {
					const int32 abs =
						(int16)vm_read_memory_2(r24);
					r24 += 2;
					const unsigned dr = (op68 >> 9) & 7u;
					uint32 dsta = (dr == 7u) ? gpr(1)
						: gpr(16 + (int)dr);
					const uint32 v =
						vm_read_memory_2(g3_rom0((uint32)abs));
					if (g3_ea_data(dsta))
						vm_write_memory_2(g3_rom0(dsta), v);
					g3_ccr = 0;
					if ((v & 0xffffu) == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 1u &&
				    ((op68 >> 3) & 7u) == 3u &&
				    ((op68 >> 6) & 7u) == 3u) {
					const unsigned sr = op68 & 7u;
					const unsigned dr = (op68 >> 9) & 7u;
					uint32 sa = (sr == 7u) ? gpr(1)
							  : gpr(16 + (int)sr);
					uint8 v = 0;
					if (g3_ea_data(sa))
						v = (uint8)vm_read_memory_1(g3_rom0(sa));
					sa += (sr == 7u) ? 2u : 1u;
					if (sr == 7u)
						gpr(1) = sa;
					else
						gpr(16 + (int)sr) = sa;
					uint32 da = (dr == 7u) ? gpr(1)
							  : gpr(16 + (int)dr);
					if (g3_ea_data(da))
						vm_write_memory_1(g3_rom0(da), v);
					da += (dr == 7u) ? 2u : 1u;
					if (dr == 7u)
						gpr(1) = da;
					else
						gpr(16 + (int)dr) = da;
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int8)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmovb;
						if (nmovb < 8) {
							nmovb++;
							nw_boot_log("G3: 68k MOVE.B (An)+,(An)+");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    (((op68 >> 12) & 3u) == 1u ||
				     ((op68 >> 12) & 3u) == 3u) &&
				    ((op68 >> 3) & 7u) == 7u &&
				    (op68 & 7u) == 4u &&
				    ((op68 >> 6) & 7u) == 2u) {
					const int szw =
						((op68 >> 12) & 3u) == 3u;
					uint32 v = vm_read_memory_2(r24);
					r24 += 2;
					if (!szw)
						v &= 0xffu;
					const unsigned dr = (op68 >> 9) & 7u;
					uint32 dsta = (dr == 7u) ? gpr(1)
						: gpr(16 + (int)dr);
					if (g3_ea_data(dsta)) {
						if (szw)
							vm_write_memory_2(
								g3_rom0(dsta), v);
						else
							vm_write_memory_1(
								g3_rom0(dsta),
								(uint8)v);
					}
					g3_ccr = 0;
					if (szw) {
						if ((v & 0xffffu) == 0)
							g3_ccr |= 4;
						if ((int16)v < 0)
							g3_ccr |= 8;
					} else {
						if ((v & 0xffu) == 0)
							g3_ccr |= 4;
						if ((int8)v < 0)
							g3_ccr |= 8;
					}
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmimm2;
						if (nmimm2 < 8) {
							nmimm2++;
							nw_boot_log("G3: 68k MOVE.W # (An)");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 3u &&
				    ((op68 >> 3) & 7u) == 7u &&
				    (op68 & 7u) == 4u &&
				    ((op68 >> 6) & 7u) == 3u) {
					const uint32 v = vm_read_memory_2(r24);
					r24 += 2;
					const unsigned dr = (op68 >> 9) & 7u;
					uint32 dsta = (dr == 7u) ? gpr(1)
						: gpr(16 + (int)dr);
					if (g3_ea_data(dsta))
						vm_write_memory_2(g3_rom0(dsta), v);
					if (dr == 7u)
						gpr(1) = dsta + 2;
					else
						gpr(16 + (int)dr) = dsta + 2;
					g3_ccr = 0;
					if ((v & 0xffffu) == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 3u &&
				    ((op68 >> 3) & 7u) == 0u &&
				    ((op68 >> 6) & 7u) == 2u) {
					const unsigned sr = op68 & 7u;
					const unsigned dr = (op68 >> 9) & 7u;
					const uint32 dsta = (dr == 7u) ? gpr(1)
						: gpr(16 + (int)dr);
					vm_write_memory_2(dsta,
						gpr(8 + (int)sr));
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x21d8u) {
					const int an = (int)(op68 & 7u);
					const int32 a =
						(int16)vm_read_memory_2(r24);
					uint32 sa = (an == 7) ? gpr(1)
						: gpr(16 + an);
					uint32 v = 0;
					if (g3_ea_data(g3_rom0(sa)))
						v = vm_read_memory_4(g3_rom0(sa));
					sa += 4;
					if (an == 7)
						gpr(1) = sa;
					else
						gpr(16 + an) = sa;
					if (g3_ea_data((uint32)a))
						vm_write_memory_4(
							g3_rom0((uint32)a), v);
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmlaw;
						if (nmlaw < 8) {
							nmlaw++;
							nw_boot_log("G3: 68k MOVE.L (An)+,abs.W");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xfff8u) == 0x21c8u) {
					const int an = (int)(op68 & 7u);
					const int32 a =
						(int16)vm_read_memory_2(r24);
					const uint32 v = (an == 7) ? gpr(1)
						: gpr(16 + an);
					if (g3_ea_data((uint32)a))
						vm_write_memory_4(
							g3_rom0((uint32)a), v);
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmanw;
						if (nmanw < 8) {
							nmanw++;
							nw_boot_log("G3: 68k MOVE.L An,abs.W");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 2u) {
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					const unsigned dm = (op68 >> 6) & 7u;
					const unsigned dr = (op68 >> 9) & 7u;
					int handled_move = 0;
					uint32 v = 0;
					const uint32 srca = (sr == 7u) ? gpr(1)
						: gpr(16 + (int)sr);
					const uint32 dsta = (dr == 7u) ? gpr(1)
						: gpr(16 + (int)dr);
					if (sm <= 1u && dm <= 1u) {
						if (sm == 0)
							v = gpr(8 + (int)sr);
						else
							v = srca;
						handled_move = 1;
					} else if (sm == 2u) {
						v = vm_read_memory_4(g3_rom0(srca));
						handled_move = 1;
					} else if (sm == 3u) {
						v = vm_read_memory_4(g3_rom0(srca));
						if (sr == 7u)
							gpr(1) = srca + 4;
						else
							gpr(16 + (int)sr) = srca + 4;
						handled_move = 1;
					} else if (sm == 4u) {
						uint32 a = srca - 4;
						if (sr == 7u)
							gpr(1) = a;
						else
							gpr(16 + (int)sr) = a;
						v = vm_read_memory_4(g3_rom0(a));
						handled_move = 1;
					} else if (sm == 5u) {
						const int32 d =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						v = vm_read_memory_4(g3_rom0(srca + d));
						handled_move = 1;
					} else if (sm == 7u && sr == 0u) {
						const int32 a =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						v = vm_read_memory_4((uint32)a);
						handled_move = 1;
					} else if (sm == 7u && sr == 1u) {
						const uint32 a = vm_read_memory_4(r24);
						r24 += 4;
						v = vm_read_memory_4(g3_rom0(a));
						handled_move = 1;
					} else if (sm == 7u && sr == 4u) {
						v = vm_read_memory_4(r24);
						r24 += 4;
						if (dm <= 1u)
							handled_move = 1;
						else if (dm == 2u) {
							if (g3_ea_data(dsta))
								vm_write_memory_4(
									g3_rom0(dsta), v);
							handled_move = 2;
						} else if (dm == 3u) {
							if (g3_ea_data(dsta))
								vm_write_memory_4(
									g3_rom0(dsta), v);
							if (dr == 7u)
								gpr(1) = dsta + 4;
							else
								gpr(16 + (int)dr) = dsta + 4;
							handled_move = 2;
						} else if (dm == 4u) {
							uint32 a = dsta - 4;
							if (dr == 7u)
								gpr(1) = a;
							else
								gpr(16 + (int)dr) = a;
							if (g3_ea_data(a))
								vm_write_memory_4(
									g3_rom0(a), v);
							handled_move = 2;
						} else if (dm == 5u) {
							const int32 d =
								(int16)vm_read_memory_2(r24);
							r24 += 2;
							if (g3_ea_data(dsta + d))
								vm_write_memory_4(
									g3_rom0(dsta + d), v);
							handled_move = 2;
						} else if (dm == 7u && dr == 0u) {
							const int32 a =
								(int16)vm_read_memory_2(r24);
							r24 += 2;
							if (g3_ea_data((uint32)a))
								vm_write_memory_4(
									g3_rom0((uint32)a), v);
							handled_move = 2;
#if NW_BOOT_LOG
							{
								static unsigned nmlia;
								if (nmlia < 8) {
									nmlia++;
									nw_boot_log("G3: 68k MOVE.L #,abs.W");
								}
							}
#endif
						}
					} else if ((sm == 0u || sm == 1u) &&
						   dm == 7u && dr == 0u) {
						const int32 a =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						v = (sm == 0u) ? gpr(8 + (int)sr)
							       : srca;
						if (g3_ea_data((uint32)a))
							vm_write_memory_4(
								g3_rom0((uint32)a), v);
						handled_move = 2;
					} else if (sm == 6u) {
						const uint32 ext =
							vm_read_memory_2(r24);
						r24 += 2;
						const int da = (int)((ext >> 15) & 1u);
						const int xr = (int)((ext >> 12) & 7u);
						const int wl = (int)((ext >> 11) & 1u);
						const int sc = (int)((ext >> 9) & 3u);
						uint32 xn = da ? ((xr == 7) ? gpr(1)
								       : gpr(16 + xr))
							      : gpr(8 + xr);
						if (!wl)
							xn = (uint32)(int32)(int16)xn;
						xn <<= sc;
						uint32 addr = srca;
						if ((ext & 0x100u) == 0) {
							addr = srca +
							       (int32)(int8)(ext & 0xffu) + xn;
						} else {
							const int bs = (int)((ext >> 7) & 1u);
							const int isup = (int)((ext >> 6) & 1u);
							const int bdsz = (int)((ext >> 4) & 3u);
							const int iis = (int)(ext & 7u);
							uint32 bd = 0;
							if (bdsz == 2) {
								bd = (uint32)(int32)(int16)
									vm_read_memory_2(r24);
								r24 += 2;
							} else if (bdsz == 3) {
								bd = vm_read_memory_4(r24);
								r24 += 4;
							}
							uint32 od = 0;
							if (iis == 2 || iis == 3 ||
							    iis == 6 || iis == 7) {
								od = (uint32)(int32)(int16)
									vm_read_memory_2(r24);
								r24 += 2;
							} else if (iis == 4) {
								od = vm_read_memory_4(r24);
								r24 += 4;
							}
							uint32 inner = (bs ? 0 : srca) + bd;
							if (!isup && iis < 6)
								inner += xn;
							if (iis >= 2 && g3_ea_data(inner))
								inner = vm_read_memory_4(
									g3_rom0(inner));
							if (!isup && iis >= 6)
								inner += xn;
							addr = inner + od;
						}
						if (g3_ea_data(g3_rom0(addr)))
							v = vm_read_memory_4(g3_rom0(addr));
						handled_move = 1;
					} else if ((sm == 0u || sm == 1u) &&
						   dm == 2u) {
						v = (sm == 0u) ? gpr(8 + (int)sr)
							       : srca;
						if (g3_ea_data(dsta))
							vm_write_memory_4(g3_rom0(dsta), v);
						handled_move = 2;
					} else if ((sm == 0u || sm == 1u) &&
						   dm == 3u) {
						v = (sm == 0u) ? gpr(8 + (int)sr)
							       : srca;
						if (g3_ea_data(dsta))
							vm_write_memory_4(g3_rom0(dsta), v);
						if (dr == 7u)
							gpr(1) = dsta + 4;
						else
							gpr(16 + (int)dr) = dsta + 4;
						handled_move = 2;
					} else if ((sm == 0u || sm == 1u) &&
						   dm == 4u) {
						uint32 a = dsta - 4;
						if (dr == 7u)
							gpr(1) = a;
						else
							gpr(16 + (int)dr) = a;
						v = (sm == 0u) ? gpr(8 + (int)sr)
							       : srca;
						vm_write_memory_4(a, v);
						handled_move = 2;
					} else if ((sm == 0u || sm == 1u) &&
						   dm == 5u) {
						const int32 d =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						v = (sm == 0u) ? gpr(8 + (int)sr)
							       : srca;
						if (g3_ea_data(dsta + d))
							vm_write_memory_4(g3_rom0(dsta + d), v);
						handled_move = 2;
					} else if ((sm == 0u || sm == 1u) &&
						   dm == 6u) {
						const uint32 ext =
							vm_read_memory_2(r24);
						r24 += 2;
						const int da = (int)((ext >> 15) & 1u);
						const int xr = (int)((ext >> 12) & 7u);
						const int wl = (int)((ext >> 11) & 1u);
						const int sc = (int)((ext >> 9) & 3u);
						uint32 xn = da ? ((xr == 7) ? gpr(1)
								       : gpr(16 + xr))
							      : gpr(8 + xr);
						if (!wl)
							xn = (uint32)(int32)(int16)xn;
						xn <<= sc;
						uint32 addr;
						if ((ext & 0x100u) == 0)
							addr = dsta +
								(int32)(int8)(ext & 0xffu) +
								xn;
						else {
							/* Full-format. Brief-only
							 * dest-edged 0x98b8 2188/81e2
							 * onto 02b6 (DeviceDispatch
							 * globals at ExpandMem+$0248). */
							const int bs = (int)((ext >> 7) & 1u);
							const int isup = (int)((ext >> 6) & 1u);
							const int bdsz = (int)((ext >> 4) & 3u);
							const int iis = (int)(ext & 7u);
							uint32 bd = 0;
							if (bdsz == 2) {
								bd = (uint32)(int32)(int16)
									vm_read_memory_2(r24);
								r24 += 2;
							} else if (bdsz == 3) {
								bd = vm_read_memory_4(r24);
								r24 += 4;
							}
							uint32 od = 0;
							if (iis == 2 || iis == 3 ||
							    iis == 6 || iis == 7) {
								od = (uint32)(int32)(int16)
									vm_read_memory_2(r24);
								r24 += 2;
							} else if (iis == 4) {
								od = vm_read_memory_4(r24);
								r24 += 4;
							}
							uint32 inner = (bs ? 0 : dsta) + bd;
							if (!isup && iis < 6)
								inner += xn;
							if (iis >= 2 && g3_ea_data(inner))
								inner = vm_read_memory_4(
									g3_rom0(inner));
							if (!isup && iis >= 6)
								inner += xn;
							addr = inner + od;
						}
						v = (sm == 0u) ? gpr(8 + (int)sr)
							       : srca;
						if (g3_ea_data(addr))
							vm_write_memory_4(
								g3_rom0(addr), v);
						handled_move = 2;
#if NW_BOOT_LOG
						if (ext & 0x100u) {
							static unsigned nmlif;
							if (nmlif < 8) {
								nmlif++;
								nw_boot_log("G3: 68k MOVE.L An,idx full");
							}
						}
#endif
					}
					if (handled_move) {
						if (handled_move == 1 && dm >= 2u) {
							if (dm == 2u) {
								if (g3_ea_data(dsta))
									vm_write_memory_4(
										g3_rom0(dsta), v);
							} else if (dm == 3u) {
								if (g3_ea_data(dsta))
									vm_write_memory_4(
										g3_rom0(dsta), v);
								if (dr == 7u)
									gpr(1) = dsta + 4;
								else
									gpr(16 + (int)dr) =
										dsta + 4;
							} else if (dm == 4u) {
								const uint32 pv = g3_rom0(v);
								const uint32 nxt =
									vm_read_memory_2(r24);
								/* 2f30/2f38+RTS is jump-through.
								 * Illegal or mid-routine dest must
								 * not consume the JSR return
								 * (blit 0x20b96 BCLR stays legal). */
								if (nxt == 0x4e75u &&
								    !g3_68k_entry(pv)) {
#if NW_BOOT_LOG
									static unsigned n2fthr;
									if (n2fthr < 16) {
										n2fthr++;
										char buf[80];
										snprintf(buf, sizeof(buf),
											 "G3: 68k 2f30 skip mid pc=%08x",
											 (unsigned)(r24 - 4u));
										nw_boot_log(buf);
									}
#endif
								} else {
									uint32 a = dsta - 4;
									if (dr == 7u)
										gpr(1) = a;
									else
										gpr(16 + (int)dr) = a;
									if (g3_ea_data(a))
										vm_write_memory_4(
											g3_rom0(a),
											g3_r24_ok(pv) ? pv : v);
								}
							} else if (dm == 5u) {
								const int32 d =
									(int16)vm_read_memory_2(r24);
								r24 += 2;
								if (g3_ea_data(dsta + d))
									vm_write_memory_4(
										g3_rom0(dsta + d), v);
							} else if (dm == 7u && dr == 0u) {
								const int32 a =
									(int16)vm_read_memory_2(r24);
								r24 += 2;
								if (g3_ea_data((uint32)a))
									vm_write_memory_4(
										g3_rom0((uint32)a), v);
#if NW_BOOT_LOG
								{
									static unsigned nmabs;
									if (nmabs < 8) {
										nmabs++;
										nw_boot_log("G3: 68k MOVE.L abs,abs");
									}
								}
#endif
							} else if (dm == 7u && dr == 1u) {
								const uint32 a =
									vm_read_memory_4(r24);
								r24 += 4;
								if (g3_ea_data(a))
									vm_write_memory_4(
										g3_rom0(a), v);
							}
							handled_move = 2;
						}
						if (handled_move == 1) {
							if (dm == 0)
								gpr(8 + (int)dr) = v;
							else if (dr == 7)
								gpr(1) = v;
							else
								gpr(16 + (int)dr) = v;
						}
						if (dm == 0 || handled_move == 2) {
							g3_ccr = 0;
							if (v == 0)
								g3_ccr |= 4;
							if ((int32)v < 0)
								g3_ccr |= 8;
						}
						gpr(24) = r24;
						gpr(27) = 0xffffffffu;
						gpr(29) = ROMBase + 0x380000u;
						pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
						static unsigned nmov;
						if (nmov < 24) {
							nmov++;
							char buf[80];
							snprintf(buf, sizeof(buf),
								 "G3: 68k MOVE.L %04x pc=%08x",
								 (unsigned)op68,
								 (unsigned)r24);
							nw_boot_log(buf);
						}
#endif
						continue;
					}
				}
				if ((op68 & 0xf100u) == 0x7000u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const uint32 v = (uint32)(int32)(int8)(op68 & 0xffu);
					gpr(8 + dn) = v;
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int32)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				/* FileMgr Pascal glue ADDQ.L #n,A7.
				 * Generic ADDQ.L An uses *4 (KEEP).
				 * 0xa8252 #8 SetFPos; 0xa821e #6 GetEOF.
				 * Do not change global An *4. */
				if (r24 - 2u == ROMBase + 0x52064u &&
				    op68 == 0x558fu) {
					/* SUBQ.L #4,A7. Generic *4 pops 16. */
					gpr(1) -= 4u;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nsubq4;
						if (nsubq4 < 8) {
							nsubq4++;
							nw_boot_log("G3: 68k SUBQ.L #4,A7 0x52064");
						}
					}
#endif
					continue;
				}
				if (r24 - 2u == ROMBase + 0x28bf4u &&
				    op68 == 0x554eu) {
					/* SUBQ.L #2,A6. Generic *4 would
					 * subtract 8. */
					gpr(22) -= 2u;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nsubq6;
						if (nsubq6 < 8) {
							nsubq6++;
							nw_boot_log("G3: 68k SUBQ.L #2,A6 0x28bf4");
						}
					}
#endif
					continue;
				}
				if ((r24 - 2u == ROMBase + 0xa8252u &&
				     op68 == 0x508fu) ||
				    (r24 - 2u == ROMBase + 0xa821eu &&
				     op68 == 0x5c8fu)) {
					const uint32 n =
						(op68 == 0x508fu) ? 8u : 6u;
					gpr(1) += n;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned naddq8;
						if (naddq8 < 8) {
							naddq8++;
							nw_boot_log("G3: 68k ADDQ.L A7 FileMgr glue");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf000u) == 0x5000u &&
				    ((op68 >> 6) & 3u) < 3u &&
				    (((op68 >> 3) & 7u) <= 3u ||
				     ((op68 >> 3) & 7u) == 5u)) {
					const int an = (int)(op68 & 7u);
					int q = (int)((op68 >> 9) & 7u);
					if (q == 0)
						q = 8;
					const unsigned sz = (op68 >> 6) & 3u;
					const unsigned sm = (op68 >> 3) & 7u;
					if (sm == 0u) {
						uint32 v = gpr(8 + an);
						if (op68 & 0x0100u)
							v -= (uint32)q;
						else
							v += (uint32)q;
						gpr(8 + an) = v;
						g3_ccr = 0;
						if (v == 0)
							g3_ccr |= 4;
						if ((int32)v < 0)
							g3_ccr |= 8;
					} else if (sm == 2u || sm == 3u) {
						uint32 a = (an == 7) ? gpr(1)
								      : gpr(16 + an);
						if (g3_ea_data(a)) {
							uint32 v, mask;
							if (sz == 2u) {
								v = vm_read_memory_4(g3_rom0(a));
								mask = 0xffffffffu;
							} else if (sz == 1u) {
								v = vm_read_memory_2(g3_rom0(a));
								mask = 0xffffu;
							} else {
								v = vm_read_memory_1(g3_rom0(a));
								mask = 0xffu;
							}
							if (op68 & 0x0100u)
								v = (v - (uint32)q) & mask;
							else
								v = (v + (uint32)q) & mask;
							if (sz == 2u)
								vm_write_memory_4(g3_rom0(a), v);
							else if (sz == 1u)
								vm_write_memory_2(g3_rom0(a),
										  (uint16)v);
							else
								vm_write_memory_1(g3_rom0(a),
										  (uint8)v);
							g3_ccr = 0;
							if (v == 0)
								g3_ccr |= 4;
							if ((sz == 2u && (int32)v < 0) ||
							    (sz == 1u && (int16)v < 0) ||
							    (sz == 0u && (int8)v < 0))
								g3_ccr |= 8;
						}
						if (sm == 3u) {
							const uint32 inc =
								(sz == 2u) ? 4u
								: ((sz == 1u) ? 2u : 1u);
							a += inc;
							if (an == 7)
								gpr(1) = a;
							else
								gpr(16 + an) = a;
#if NW_BOOT_LOG
							{
								static unsigned naddq3;
								if (naddq3 < 8) {
									naddq3++;
									nw_boot_log("G3: 68k ADDQ (An)+");
								}
							}
#endif
						}
					} else if (sm == 5u) {
						const int32 d =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						uint32 a = ((an == 7) ? gpr(1)
								      : gpr(16 + an)) + d;
						if (g3_ea_data(a)) {
							uint32 v, mask;
							if (sz == 2u) {
								v = vm_read_memory_4(g3_rom0(a));
								mask = 0xffffffffu;
							} else if (sz == 1u) {
								v = vm_read_memory_2(g3_rom0(a));
								mask = 0xffffu;
							} else {
								v = vm_read_memory_1(g3_rom0(a));
								mask = 0xffu;
							}
							if (op68 & 0x0100u)
								v = (v - (uint32)q) & mask;
							else
								v = (v + (uint32)q) & mask;
							if (sz == 2u)
								vm_write_memory_4(g3_rom0(a), v);
							else if (sz == 1u)
								vm_write_memory_2(g3_rom0(a),
										  (uint16)v);
							else
								vm_write_memory_1(g3_rom0(a),
										  (uint8)v);
							g3_ccr = 0;
							if (v == 0)
								g3_ccr |= 4;
							if ((sz == 2u && (int32)v < 0) ||
							    (sz == 1u && (int16)v < 0) ||
							    (sz == 0u && (int8)v < 0))
								g3_ccr |= 8;
						}
#if NW_BOOT_LOG
						{
							static unsigned naddq;
							if (naddq < 8) {
								naddq++;
								nw_boot_log("G3: 68k ADDQ d16");
							}
						}
#endif
					} else {
						uint32 v = (an == 7) ? gpr(1)
								     : gpr(16 + an);
						if (op68 & 0x0100u)
							v -= (sz == 2u) ? (uint32)q * 4u
									: (uint32)q;
						else
							v += (sz == 2u) ? (uint32)q * 4u
									: (uint32)q;
						if (an == 7)
							gpr(1) = v;
						else
							gpr(16 + an) = v;
					}
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf1c0u) == 0xb040u &&
				    (((op68 >> 3) & 7u) == 2u ||
				     ((op68 >> 3) & 7u) == 3u)) {
					const unsigned sr = op68 & 7u;
					const unsigned dn = (op68 >> 9) & 7u;
					const unsigned sm = (op68 >> 3) & 7u;
					uint32 a = (sr == 7u) ? gpr(1)
							  : gpr(16 + (int)sr);
					uint32 m = 0;
					if (g3_ea_data(g3_rom0(a)))
						m = vm_read_memory_2(g3_rom0(a)) &
						    0xffffu;
					if (sm == 3u) {
						a += 2;
						if (sr == 7u)
							gpr(1) = a;
						else
							gpr(16 + (int)sr) = a;
					}
					const uint32 d = gpr(8 + (int)dn) & 0xffffu;
					const uint32 r = (uint16)(d - m);
					g3_ccr = 0;
					if (r == 0)
						g3_ccr |= 4;
					if ((int16)r < 0)
						g3_ccr |= 8;
					if (d < m)
						g3_ccr |= 1;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ncmpan;
						if (ncmpan < 8) {
							ncmpan++;
							nw_boot_log("G3: 68k CMP.W (An)");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1c0u) == 0xb040u &&
				    ((op68 >> 3) & 7u) <= 1u) {
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					const unsigned dn = (op68 >> 9) & 7u;
					uint32 m = (sm == 0u) ? gpr(8 + (int)sr)
						: ((sr == 7u) ? gpr(1) : gpr(16 + (int)sr));
					m &= 0xffffu;
					const uint32 d = gpr(8 + (int)dn) & 0xffffu;
					const uint32 r = (uint16)(d - m);
					g3_ccr = 0;
					if (r == 0)
						g3_ccr |= 4;
					if ((int16)r < 0)
						g3_ccr |= 8;
					if (d < m)
						g3_ccr |= 1;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf1c0u) == 0xb040u &&
				    ((op68 >> 3) & 7u) == 5u) {
					const int32 d =
						(int16)vm_read_memory_2(r24);
					r24 += 2;
					const unsigned sr = op68 & 7u;
					const unsigned dn = (op68 >> 9) & 7u;
					const uint32 a = ((sr == 7u) ? gpr(1)
							     : gpr(16 + (int)sr)) + d;
					const uint32 m = vm_read_memory_2(g3_rom0(a)) &
							 0xffffu;
					const uint32 dv = gpr(8 + (int)dn) & 0xffffu;
					const uint32 r = (uint16)(dv - m);
					g3_ccr = 0;
					if (r == 0)
						g3_ccr |= 4;
					if ((int16)r < 0)
						g3_ccr |= 8;
					if (dv < m)
						g3_ccr |= 1;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf1ffu) == 0xb078u) {
					const int32 a =
						(int16)vm_read_memory_2(r24);
					r24 += 2;
					const unsigned dn = (op68 >> 9) & 7u;
					const uint32 m =
						vm_read_memory_2(g3_rom0((uint32)a)) &
						0xffffu;
					const uint32 dv = gpr(8 + (int)dn) & 0xffffu;
					const uint32 r = (uint16)(dv - m);
					g3_ccr = 0;
					if (r == 0)
						g3_ccr |= 4;
					if ((int16)r < 0)
						g3_ccr |= 8;
					if (dv < m)
						g3_ccr |= 1;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    (((op68 >> 12) & 3u) == 1u ||
				     ((op68 >> 12) & 3u) == 2u ||
				     ((op68 >> 12) & 3u) == 3u) &&
				    ((op68 >> 3) & 7u) == 6u) {
					const unsigned sz =
						(op68 >> 12) & 3u;
					const int szw = sz == 3u;
					const int szl = sz == 2u;
					const unsigned sr = op68 & 7u;
					const unsigned dr = (op68 >> 9) & 7u;
					const unsigned dm = (op68 >> 6) & 7u;
					const uint32 srcb = (sr == 7u) ? gpr(1)
						: gpr(16 + (int)sr);
					uint32 sext = vm_read_memory_2(r24);
					r24 += 2;
					{
						const int da = (int)((sext >> 15) & 1u);
						const int xr = (int)((sext >> 12) & 7u);
						const int wl = (int)((sext >> 11) & 1u);
						const int sc = (int)((sext >> 9) & 3u);
						uint32 xn = da ? ((xr == 7) ? gpr(1)
								       : gpr(16 + xr))
							      : gpr(8 + xr);
						if (!wl)
							xn = (uint32)(int32)(int16)xn;
						xn <<= sc;
						uint32 sa = srcb;
						if ((sext & 0x100u) == 0)
							sa = srcb +
							     (int32)(int8)(sext & 0xffu) +
							     xn;
						else {
							const int bs = (int)((sext >> 7) & 1u);
							const int isup = (int)((sext >> 6) & 1u);
							const int bdsz = (int)((sext >> 4) & 3u);
							const int iis = (int)(sext & 7u);
							uint32 bd = 0;
							if (bdsz == 2) {
								bd = (uint32)(int32)(int16)
									vm_read_memory_2(r24);
								r24 += 2;
							} else if (bdsz == 3) {
								bd = vm_read_memory_4(r24);
								r24 += 4;
							}
							uint32 od = 0;
							if (iis == 2 || iis == 3 ||
							    iis == 6 || iis == 7) {
								od = (uint32)(int32)(int16)
									vm_read_memory_2(r24);
								r24 += 2;
							} else if (iis == 4) {
								od = vm_read_memory_4(r24);
								r24 += 4;
							}
							uint32 inner = (bs ? 0 : srcb) + bd;
							if (!isup && iis < 6)
								inner += xn;
							if (iis >= 2 && g3_ea_data(inner))
								inner = vm_read_memory_4(
									g3_rom0(inner));
							if (!isup && iis >= 6)
								inner += xn;
							sa = inner + od;
						}
						sext = sa;
					}
					uint32 v = 0;
					if (g3_ea_data(sext)) {
						if (szl)
							v = vm_read_memory_4(
								g3_rom0(sext));
						else if (szw)
							v = vm_read_memory_2(
								g3_rom0(sext));
						else
							v = vm_read_memory_1(
								g3_rom0(sext));
					}
					int hb = 1;
					uint32 dsta = (dr == 7u) ? gpr(1)
						: gpr(16 + (int)dr);
					if (dm == 0u) {
						if (szw)
							gpr(8 + (int)dr) =
								(gpr(8 + (int)dr) &
								 0xffff0000u) |
								(v & 0xffffu);
						else
							gpr(8 + (int)dr) =
								(gpr(8 + (int)dr) &
								 0xffffff00u) |
								(v & 0xffu);
					} else if (dm == 2u || dm == 3u) {
						if (g3_ea_data(dsta)) {
							if (szw)
								vm_write_memory_2(
									g3_rom0(dsta), v);
							else
								vm_write_memory_1(
									g3_rom0(dsta),
									(uint8)v);
						}
						if (dm == 3u) {
							dsta += szw ? 2u : 1u;
							if (dr == 7u)
								gpr(1) = dsta;
							else
								gpr(16 + (int)dr) = dsta;
						}
					} else if (dm == 4u) {
						dsta -= szw ? 2u : 1u;
						if (dr == 7u)
							gpr(1) = dsta;
						else
							gpr(16 + (int)dr) = dsta;
						if (g3_ea_data(dsta)) {
							if (szw)
								vm_write_memory_2(
									g3_rom0(dsta), v);
							else
								vm_write_memory_1(
									g3_rom0(dsta),
									(uint8)v);
						}
					} else if (dm == 5u) {
						const int32 d =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						if (g3_ea_data(dsta + d)) {
							if (szw)
								vm_write_memory_2(
									g3_rom0(dsta + d),
									v);
							else
								vm_write_memory_1(
									g3_rom0(dsta + d),
									(uint8)v);
						}
					} else if (dm == 6u) {
						uint32 dext = vm_read_memory_2(r24);
						r24 += 2;
						const int da = (int)((dext >> 15) & 1u);
						const int xr = (int)((dext >> 12) & 7u);
						const int wl = (int)((dext >> 11) & 1u);
						const int sc = (int)((dext >> 9) & 3u);
						uint32 xn = da ? ((xr == 7) ? gpr(1)
								       : gpr(16 + xr))
							      : gpr(8 + xr);
						if (!wl)
							xn = (uint32)(int32)(int16)xn;
						xn <<= sc;
						uint32 daaddr = dsta;
						if ((dext & 0x100u) == 0)
							daaddr = dsta +
								 (int32)(int8)(dext & 0xffu) +
								 xn;
						else {
							const int bs = (int)((dext >> 7) & 1u);
							const int isup = (int)((dext >> 6) & 1u);
							const int bdsz = (int)((dext >> 4) & 3u);
							const int iis = (int)(dext & 7u);
							uint32 bd = 0;
							if (bdsz == 2) {
								bd = (uint32)(int32)(int16)
									vm_read_memory_2(r24);
								r24 += 2;
							} else if (bdsz == 3) {
								bd = vm_read_memory_4(r24);
								r24 += 4;
							}
							uint32 od = 0;
							if (iis == 2 || iis == 3 ||
							    iis == 6 || iis == 7) {
								od = (uint32)(int32)(int16)
									vm_read_memory_2(r24);
								r24 += 2;
							} else if (iis == 4) {
								od = vm_read_memory_4(r24);
								r24 += 4;
							}
							uint32 inner = (bs ? 0 : dsta) + bd;
							if (!isup && iis < 6)
								inner += xn;
							if (iis >= 2 && g3_ea_data(inner))
								inner = vm_read_memory_4(
									g3_rom0(inner));
							if (!isup && iis >= 6)
								inner += xn;
							daaddr = inner + od;
						}
						if (g3_ea_data(daaddr)) {
							if (szl)
								vm_write_memory_4(
									g3_rom0(daaddr),
									v);
							else if (szw)
								vm_write_memory_2(
									g3_rom0(daaddr),
									v);
							else
								vm_write_memory_1(
									g3_rom0(daaddr),
									(uint8)v);
						}
					} else if (dm == 7u && dr == 0u) {
						const int32 a =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						if (g3_ea_data((uint32)a)) {
							if (szl)
								vm_write_memory_4(
									g3_rom0((uint32)a),
									v);
							else if (szw)
								vm_write_memory_2(
									g3_rom0((uint32)a),
									v);
							else
								vm_write_memory_1(
									g3_rom0((uint32)a),
									(uint8)v);
						}
					} else
						hb = 0;
					if (hb) {
						g3_ccr = 0;
						if (szw) {
							if ((v & 0xffffu) == 0)
								g3_ccr |= 4;
							if ((int16)v < 0)
								g3_ccr |= 8;
						} else {
							if ((v & 0xffu) == 0)
								g3_ccr |= 4;
							if ((int8)v < 0)
								g3_ccr |= 8;
						}
						gpr(24) = r24;
						gpr(27) = 0xffffffffu;
						gpr(29) = ROMBase + 0x380000u;
						pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
						{
							static unsigned nmwii;
							if (nmwii < 8) {
								nmwii++;
								nw_boot_log("G3: 68k MOVE.W idx,idx");
							}
						}
#endif
						continue;
					}
				}
				if ((op68 & 0xf1c0u) == 0xc000u &&
				    ((op68 >> 6) & 7u) == 0u &&
				    ((op68 >> 3) & 7u) == 5u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const int an = (int)(op68 & 7u);
					const int32 d =
						(int16)vm_read_memory_2(r24);
					r24 += 2;
					uint32 a = ((an == 7) ? gpr(1)
							     : gpr(16 + an)) + d;
					uint8 m = 0;
					if (g3_ea_data(a))
						m = (uint8)vm_read_memory_1(g3_rom0(a));
					uint32 v = (gpr(8 + dn) & 0xffu) & m;
					gpr(8 + dn) = (gpr(8 + dn) & 0xffffff00u) | v;
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int8)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nandb;
						if (nandb < 8) {
							nandb++;
							nw_boot_log("G3: 68k AND.B d16(An),Dn");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1c0u) == 0xc080u &&
				    ((op68 >> 3) & 7u) == 7u &&
				    (op68 & 7u) == 0u) {
					const unsigned dr = (op68 >> 9) & 7u;
					const int32 a =
						(int16)vm_read_memory_2(r24);
					r24 += 2;
					uint32 m = 0;
					if (g3_ea_data((uint32)a))
						m = vm_read_memory_4(g3_rom0((uint32)a));
					const uint32 v = gpr(8 + (int)dr) & m;
					gpr(8 + (int)dr) = v;
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int32)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nandl;
						if (nandl < 8) {
							nandl++;
							nw_boot_log("G3: 68k AND.L abs.W");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1c0u) == 0x8180u &&
				    ((op68 >> 3) & 7u) == 6u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const int an = (int)(op68 & 7u);
					const uint32 ext = vm_read_memory_2(r24);
					const uint32 basea = (an == 7) ? gpr(1)
								      : gpr(16 + an);
					const int da = (int)((ext >> 15) & 1u);
					const int xr = (int)((ext >> 12) & 7u);
					const int wl = (int)((ext >> 11) & 1u);
					const int32 disp = (int8)(ext & 0xffu);
					uint32 xn = da ? ((xr == 7) ? gpr(1)
							       : gpr(16 + xr))
						      : gpr(8 + xr);
					if (!wl)
						xn = (uint32)(int32)(int16)xn;
					uint32 addr = basea + disp + xn;
					uint32 v = 0;
					if (g3_ea_data(addr))
						v = vm_read_memory_4(g3_rom0(addr));
					v |= gpr(8 + dn);
					if (g3_ea_data(addr))
						vm_write_memory_4(g3_rom0(addr), v);
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int32)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned noridx;
						if (noridx < 8) {
							noridx++;
							nw_boot_log("G3: 68k OR.L Dn,idx");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1c0u) == 0x8100u &&
				    ((op68 >> 3) & 7u) == 6u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const int an = (int)(op68 & 7u);
					const uint32 ext = vm_read_memory_2(r24);
					const uint32 basea = (an == 7) ? gpr(1)
								      : gpr(16 + an);
					const int da = (int)((ext >> 15) & 1u);
					const int xr = (int)((ext >> 12) & 7u);
					const int wl = (int)((ext >> 11) & 1u);
					const int32 disp = (int8)(ext & 0xffu);
					uint32 xn = da ? ((xr == 7) ? gpr(1)
							       : gpr(16 + xr))
						      : gpr(8 + xr);
					if (!wl)
						xn = (uint32)(int32)(int16)xn;
					uint32 addr = basea + disp + xn;
					uint32 v = 0;
					if (g3_ea_data(addr))
						v = vm_read_memory_1(g3_rom0(addr));
					v = (v | (gpr(8 + dn) & 0xffu)) & 0xffu;
					if (g3_ea_data(addr))
						vm_write_memory_1(g3_rom0(addr), (uint8)v);
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int8)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned norbidx;
						if (norbidx < 8) {
							norbidx++;
							nw_boot_log("G3: 68k OR.B Dn,idx");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xffc0u) == 0x00c0u &&
				    ((op68 >> 3) & 7u) == 6u) {
					const int an = (int)(op68 & 7u);
					const uint32 ext = vm_read_memory_2(r24);
					r24 += 2;
					const uint32 rn_w = vm_read_memory_2(r24);
					r24 += 2;
					const uint32 basea = (an == 7) ? gpr(1)
								      : gpr(16 + an);
					const int da = (int)((ext >> 15) & 1u);
					const int xr = (int)((ext >> 12) & 7u);
					const int wl = (int)((ext >> 11) & 1u);
					const int32 disp = (int8)(ext & 0xffu);
					uint32 xn = da ? ((xr == 7) ? gpr(1)
							       : gpr(16 + xr))
						      : gpr(8 + xr);
					if (!wl)
						xn = (uint32)(int32)(int16)xn;
					uint32 addr = basea + disp + xn;
					const int rn = (int)((rn_w >> 12) & 7u);
					const int rna = (int)((rn_w >> 15) & 1u);
					uint32 rv = rna ? ((rn == 7) ? gpr(1)
								    : gpr(16 + rn))
						       : gpr(8 + rn);
					int8 lo = 0, hi = 0;
					if (g3_ea_data(addr)) {
						lo = (int8)vm_read_memory_1(
							g3_rom0(addr));
						hi = (int8)vm_read_memory_1(
							g3_rom0(addr + 1u));
					}
					const int8 r = (int8)rv;
					g3_ccr = 0;
					if (r == lo || r == hi)
						g3_ccr |= 4;
					if (r < lo || r > hi)
						g3_ccr |= 1;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ncmp2;
						if (ncmp2 < 8) {
							ncmp2++;
							nw_boot_log("G3: 68k CMP2.B idx");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf0c0u) == 0xb080u) {
					const uint32 cmp_opc = r24 - 2u;
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					uint32 m = 0;
					int got = 0;
					if (sm == 0u) {
						m = gpr(8 + (int)sr);
						got = 1;
					} else if (sm == 1u) {
						m = (sr == 7u) ? gpr(1)
							       : gpr(16 + (int)sr);
						got = 1;
					} else {
						uint32 addr = (sr == 7u) ? gpr(1)
									 : gpr(16 + (int)sr);
						if (sm == 4u) {
							addr -= 4;
							if (sr == 7u)
								gpr(1) = addr;
							else
								gpr(16 + (int)sr) = addr;
							got = 2;
						} else if (sm == 3u) {
							if (sr == 7u)
								gpr(1) = addr + 4;
							else
								gpr(16 + (int)sr) = addr + 4;
							got = 2;
						} else if (sm == 2u)
							got = 2;
						else if (sm == 5u) {
							const int32 d =
								(int16)vm_read_memory_2(r24);
							r24 += 2;
							addr += d;
							got = 2;
						} else if (sm == 7u && sr == 0u) {
							addr = (uint32)(int32)(int16)
								vm_read_memory_2(r24);
							r24 += 2;
							got = 2;
						} else if (sm == 6u) {
							const uint32 ext =
								vm_read_memory_2(r24);
							r24 += 2;
							const int da = (int)((ext >> 15) & 1u);
							const int xr = (int)((ext >> 12) & 7u);
							const int wl = (int)((ext >> 11) & 1u);
							const int sc = (int)((ext >> 9) & 3u);
							const int32 disp = (int8)(ext & 0xffu);
							uint32 xn = da ? ((xr == 7) ? gpr(1)
									       : gpr(16 + xr))
								      : gpr(8 + xr);
							if (!wl)
								xn = (uint32)(int32)(int16)xn;
							xn <<= sc;
							addr = addr + disp + xn;
							got = 2;
#if NW_BOOT_LOG
							{
								static unsigned ncmplix;
								if (ncmplix < 8) {
									ncmplix++;
									nw_boot_log("G3: 68k CMP.L idx");
								}
							}
#endif
						}
						if (got == 2)
							m = vm_read_memory_4(g3_rom0(addr));
					}
					if (got) {
						const int dn = (int)((op68 >> 9) & 7u);
						uint32 d = gpr(8 + dn);
						/* CopyBits scan bound at 0x32(A4) is 0
						 * on a NewPtr-zero record, so BGT
						 * 0x20cc2/0x1fb08 never exits.
						 * Force D3==bound so BLE falls out.
						 * Do not skip 0x20ca4-0x20cc4. */
						if (cmp_opc == ROMBase + 0x20cb0u ||
						    cmp_opc == ROMBase + 0x20cbeu ||
						    cmp_opc == ROMBase + 0x1faf6u ||
						    cmp_opc == ROMBase + 0x1fb04u) {
							gpr(8 + dn) = m;
							d = m;
#if NW_BOOT_LOG
							{
								static unsigned ncbnd;
								if (ncbnd < 8) {
									ncbnd++;
									nw_boot_log("G3: 68k CopyBits bound cap");
								}
							}
#endif
						}
						const uint32 r = d - m;
						g3_ccr = 0;
						if (r == 0)
							g3_ccr |= 4;
						if ((int32)r < 0)
							g3_ccr |= 8;
						if (d < m)
							g3_ccr |= 1;
						gpr(24) = r24;
						gpr(27) = 0xffffffffu;
						gpr(29) = ROMBase + 0x380000u;
						pc() = ROMBase + 0x366084u;
						continue;
					}
				}
				if ((op68 & 0xff00u) == 0x0c00u &&
				    ((op68 >> 6) & 3u) != 3u) {
					const unsigned sz = (op68 >> 6) & 3u;
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					uint32 imm;
					if (sz == 2u) {
						imm = vm_read_memory_4(r24);
						r24 += 4;
					} else {
						imm = vm_read_memory_2(r24);
						r24 += 2;
						if (sz == 0u)
							imm &= 0xffu;
					}
					uint32 addr = 0;
					uint32 val = 0;
					int got = 0;
					if (sm == 0u) {
						val = gpr(8 + (int)sr);
						got = 1;
					} else if (sm == 1u) {
						val = (sr == 7u) ? gpr(1)
								 : gpr(16 + (int)sr);
						got = 1;
					} else if (sm == 2u) {
						addr = (sr == 7u) ? gpr(1)
								  : gpr(16 + (int)sr);
						got = 2;
					} else if (sm == 3u) {
						addr = (sr == 7u) ? gpr(1)
								  : gpr(16 + (int)sr);
						{
							const uint32 inc =
								(sz == 2u) ? 4u
								: ((sz == 1u) ? 2u : 1u);
							if (sr == 7u)
								gpr(1) = addr + inc;
							else
								gpr(16 + (int)sr) =
									addr + inc;
						}
						got = 2;
#if NW_BOOT_LOG
						{
							static unsigned ncmpip;
							if (ncmpip < 8) {
								ncmpip++;
								nw_boot_log("G3: 68k CMPI (An)+");
							}
						}
#endif
					} else if (sm == 5u) {
						const int32 d =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						addr = ((sr == 7u) ? gpr(1)
								   : gpr(16 + (int)sr)) + d;
						got = 2;
					} else if (sm == 6u) {
						const uint32 ext =
							vm_read_memory_2(r24);
						r24 += 2;
						const uint32 srca = (sr == 7u)
							? gpr(1)
							: gpr(16 + (int)sr);
						const int da = (int)((ext >> 15) & 1u);
						const int xr = (int)((ext >> 12) & 7u);
						const int wl = (int)((ext >> 11) & 1u);
						const int sc = (int)((ext >> 9) & 3u);
						uint32 xn = da ? ((xr == 7) ? gpr(1)
								       : gpr(16 + xr))
							      : gpr(8 + xr);
						if (!wl)
							xn = (uint32)(int32)(int16)xn;
						xn <<= sc;
						if ((ext & 0x100u) == 0) {
							addr = srca +
							       (int32)(int8)(ext & 0xffu) + xn;
						} else {
							const int bs = (int)((ext >> 7) & 1u);
							const int isup = (int)((ext >> 6) & 1u);
							const int bdsz = (int)((ext >> 4) & 3u);
							const int iis = (int)(ext & 7u);
							uint32 bd = 0;
							if (bdsz == 2) {
								bd = (uint32)(int32)(int16)
									vm_read_memory_2(r24);
								r24 += 2;
							} else if (bdsz == 3) {
								bd = vm_read_memory_4(r24);
								r24 += 4;
							}
							uint32 od = 0;
							if (iis == 2 || iis == 3 ||
							    iis == 6 || iis == 7) {
								od = (uint32)(int32)(int16)
									vm_read_memory_2(r24);
								r24 += 2;
							} else if (iis == 4) {
								od = vm_read_memory_4(r24);
								r24 += 4;
							}
							uint32 inner = (bs ? 0 : srca) + bd;
							if (!isup && iis < 6)
								inner += xn;
							if (iis >= 2)
								inner = vm_read_memory_4(inner);
							if (!isup && iis >= 6)
								inner += xn;
							addr = inner + od;
						}
						got = 2;
					} else if (sm == 7u && sr == 0u) {
						addr = (uint32)(int32)(int16)
							vm_read_memory_2(r24);
						r24 += 2;
						got = 2;
					} else if (sm == 7u && sr == 1u) {
						addr = vm_read_memory_4(r24);
						r24 += 4;
						got = 2;
					} else if (sm == 7u && sr == 2u) {
						const int32 d =
							(int16)vm_read_memory_2(r24);
						addr = r24 + d;
						r24 += 2;
						got = 2;
					}
					if (got) {
						if (got == 2) {
							addr = g3_rom0(addr);
							if (sz == 2u)
								val = vm_read_memory_4(addr);
							else if (sz == 1u)
								val = vm_read_memory_2(addr);
							else
								val = vm_read_memory_1(addr);
						}
						if (sz == 1u)
							val &= 0xffffu;
						else if (sz == 0u)
							val &= 0xffu;
						const uint32 r = val - imm;
						g3_ccr = 0;
						if (sz == 2u) {
							if (r == 0)
								g3_ccr |= 4;
							if ((int32)r < 0)
								g3_ccr |= 8;
							if (val < imm)
								g3_ccr |= 1;
						} else if (sz == 1u) {
							const uint16 rr = (uint16)r;
							if (rr == 0)
								g3_ccr |= 4;
							if ((int16)rr < 0)
								g3_ccr |= 8;
							if ((uint16)val < (uint16)imm)
								g3_ccr |= 1;
						} else {
							const uint8 rr = (uint8)r;
							if (rr == 0)
								g3_ccr |= 4;
							if ((int8)rr < 0)
								g3_ccr |= 8;
							if ((uint8)val < (uint8)imm)
								g3_ccr |= 1;
						}
						gpr(24) = r24;
						gpr(27) = 0xffffffffu;
						gpr(29) = ROMBase + 0x380000u;
						pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
						static unsigned ncmpi;
						if (ncmpi < 8) {
							ncmpi++;
							char buf[96];
							snprintf(buf, sizeof(buf),
								 "G3: 68k CMPI %04x val=%08x imm=%08x z=%u",
								 (unsigned)op68, (unsigned)val,
								 (unsigned)imm,
								 (unsigned)((g3_ccr & 4) != 0));
							nw_boot_log(buf);
						}
#endif
						continue;
					}
					if (sz == 2u)
						r24 -= 4;
					else
						r24 -= 2;
				}
				if ((op68 & 0xf1f8u) == 0xc140u ||
				    (op68 & 0xf1f8u) == 0xc148u ||
				    (op68 & 0xf1f8u) == 0xc188u) {
					const int rx = (int)((op68 >> 9) & 7u);
					const int ry = (int)(op68 & 7u);
					uint32 vx, vy;
					if ((op68 & 0xf1f8u) == 0xc140u) {
						vx = gpr(8 + rx);
						vy = gpr(8 + ry);
						gpr(8 + rx) = vy;
						gpr(8 + ry) = vx;
					} else if ((op68 & 0xf1f8u) == 0xc148u) {
						vx = (rx == 7) ? gpr(1)
							       : gpr(16 + rx);
						vy = (ry == 7) ? gpr(1)
							       : gpr(16 + ry);
						if (rx == 7)
							gpr(1) = vy;
						else
							gpr(16 + rx) = vy;
						if (ry == 7)
							gpr(1) = vx;
						else
							gpr(16 + ry) = vx;
					} else {
						vx = gpr(8 + rx);
						vy = (ry == 7) ? gpr(1)
							       : gpr(16 + ry);
						gpr(8 + rx) = vy;
						if (ry == 7)
							gpr(1) = vx;
						else
							gpr(16 + ry) = vx;
					}
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					static unsigned nexg;
					if (nexg < 8) {
						nexg++;
						char buf[80];
						snprintf(buf, sizeof(buf),
							 "G3: 68k EXG %04x pc=%08x",
							 (unsigned)op68,
							 (unsigned)r24);
						nw_boot_log(buf);
					}
#endif
					continue;
				}
				if ((op68 & 0xfff8u) == 0x4200u) {
					gpr(8 + (int)(op68 & 7u)) &= 0xffffff00u;
					g3_ccr = 4;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (((op68 & 0xf000u) == 0x8000u ||
				     (op68 & 0xf000u) == 0xc000u) &&
				    ((op68 >> 6) & 3u) <= 2u &&
				    ((op68 >> 8) & 1u) == 0u &&
				    ((op68 >> 3) & 7u) == 5u) {
					const unsigned sz = (op68 >> 6) & 3u;
					const int dn = (int)((op68 >> 9) & 7u);
					const unsigned sr = op68 & 7u;
					const int32 disp =
						(int16)vm_read_memory_2(r24);
					r24 += 2;
					const uint32 addr =
						((sr == 7u) ? gpr(1)
							    : gpr(16 + (int)sr)) + disp;
					uint32 b, a, r, mask;
					if (sz == 2u) {
						b = vm_read_memory_4(g3_rom0(addr));
						mask = 0xffffffffu;
					} else if (sz == 1u) {
						b = vm_read_memory_2(g3_rom0(addr));
						mask = 0xffffu;
					} else {
						b = vm_read_memory_1(g3_rom0(addr));
						mask = 0xffu;
					}
					a = gpr(8 + dn) & mask;
					b &= mask;
					if ((op68 & 0xf000u) == 0x8000u)
						r = a | b;
					else
						r = a & b;
					r &= mask;
					if (sz == 2u)
						gpr(8 + dn) = r;
					else if (sz == 1u)
						gpr(8 + dn) = (gpr(8 + dn) & 0xffff0000u) | r;
					else
						gpr(8 + dn) = (gpr(8 + dn) & 0xffffff00u) | r;
					g3_ccr = 0;
					if (r == 0)
						g3_ccr |= 4;
					if (sz == 2u) {
						if ((int32)r < 0)
							g3_ccr |= 8;
					} else if (sz == 1u) {
						if ((int16)r < 0)
							g3_ccr |= 8;
					} else if ((int8)r < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (((op68 & 0xf000u) == 0x8000u ||
				     (op68 & 0xf000u) == 0xc000u) &&
				    ((op68 >> 6) & 3u) <= 2u &&
				    ((op68 >> 8) & 1u) == 0u &&
				    ((op68 >> 3) & 7u) == 0u) {
					const unsigned sz = (op68 >> 6) & 3u;
					const int dn = (int)((op68 >> 9) & 7u);
					const int sn = (int)(op68 & 7u);
					uint32 a = gpr(8 + dn);
					uint32 b = gpr(8 + sn);
					uint32 r;
					unsigned bits = sz == 0u ? 8u
						: (sz == 1u ? 16u : 32u);
					const uint32 mask = bits == 8u ? 0xffu
						: (bits == 16u ? 0xffffu
							       : 0xffffffffu);
					a &= mask;
					b &= mask;
					if ((op68 & 0xf000u) == 0x8000u)
						r = a | b;
					else
						r = a & b;
					r &= mask;
					if (sz == 2u)
						gpr(8 + dn) = r;
					else if (sz == 1u)
						gpr(8 + dn) = (gpr(8 + dn) & 0xffff0000u) | r;
					else
						gpr(8 + dn) = (gpr(8 + dn) & 0xffffff00u) | r;
					g3_ccr = 0;
					if (r == 0)
						g3_ccr |= 4;
					if (sz == 2u) {
						if ((int32)r < 0)
							g3_ccr |= 8;
					} else if (sz == 1u) {
						if ((int16)r < 0)
							g3_ccr |= 8;
					} else if ((int8)r < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf1c0u) == 0xc1c0u ||
				    (op68 & 0xf1c0u) == 0xc0c0u) {
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					const int dn = (int)((op68 >> 9) & 7u);
					uint32 m = 0;
					int got = 0;
					if (sm == 0u) {
						m = gpr(8 + (int)sr);
						got = 1;
					} else if (sm == 7u && sr == 4u) {
						m = vm_read_memory_2(r24);
						r24 += 2;
						got = 1;
					}
					if (got) {
						int32 r;
						if ((op68 & 0xf1c0u) == 0xc1c0u)
							r = (int32)(int16)gpr(8 + dn) *
							    (int32)(int16)m;
						else
							r = (int32)(uint16)gpr(8 + dn) *
							    (int32)(uint16)m;
						gpr(8 + dn) = (uint32)r;
						g3_ccr = 0;
						if (r == 0)
							g3_ccr |= 4;
						if (r < 0)
							g3_ccr |= 8;
						gpr(24) = r24;
						gpr(27) = 0xffffffffu;
						gpr(29) = ROMBase + 0x380000u;
						pc() = ROMBase + 0x366084u;
						continue;
					}
				}
				if (((op68 & 0xf1c0u) == 0xb0c0u ||
				     (op68 & 0xf1c0u) == 0xb1c0u) &&
				    ((op68 >> 3) & 7u) == 5u) {
					const int an = (int)((op68 >> 9) & 7u);
					const int am = (int)(op68 & 7u);
					const int32 d =
						(int16)vm_read_memory_2(r24);
					r24 += 2;
					const uint32 a = (an == 7) ? gpr(1)
							      : gpr(16 + an);
					uint32 baddr = ((am == 7) ? gpr(1)
							       : gpr(16 + am)) + d;
					uint32 b = 0;
					const int word =
						((op68 & 0xf1c0u) == 0xb0c0u);
					if (g3_ea_data(baddr)) {
						if (word)
							b = vm_read_memory_2(g3_rom0(baddr));
						else
							b = vm_read_memory_4(g3_rom0(baddr));
					}
					g3_ccr = 0;
					if (word) {
						uint32 av = a & 0xffffu;
						uint32 bv = b & 0xffffu;
						uint32 r = (uint16)(av - bv);
						if (r == 0)
							g3_ccr |= 4;
						if ((int16)r < 0)
							g3_ccr |= 8;
						if (av < bv)
							g3_ccr |= 1;
					} else {
						uint32 r = a - b;
						if (r == 0)
							g3_ccr |= 4;
						if ((int32)r < 0)
							g3_ccr |= 8;
						if (a < b)
							g3_ccr |= 1;
					}
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				/* CMPA.W/L #imm. 0x51918 b3fc nil check. */
				if (((op68 & 0xf1c0u) == 0xb0c0u ||
				     (op68 & 0xf1c0u) == 0xb1c0u) &&
				    ((op68 >> 3) & 7u) == 7u &&
				    (op68 & 7u) == 4u) {
					const int an = (int)((op68 >> 9) & 7u);
					const int word =
						((op68 & 0xf1c0u) == 0xb0c0u);
					uint32 b;
					if (word) {
						b = (uint32)(int32)(int16)
							vm_read_memory_2(r24);
						r24 += 2;
					} else {
						b = vm_read_memory_4(r24);
						r24 += 4;
					}
					const uint32 a = (an == 7) ? gpr(1)
							      : gpr(16 + an);
					const uint32 r = a - b;
					g3_ccr = 0;
					if (r == 0)
						g3_ccr |= 4;
					if ((int32)r < 0)
						g3_ccr |= 8;
					if (a < b)
						g3_ccr |= 1;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ncmpai;
						if (ncmpai < 8) {
							ncmpai++;
							nw_boot_log("G3: 68k CMPA #");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1f8u) == 0xb0c8u ||
				    (op68 & 0xf1f8u) == 0xb1c8u) {
					const int an = (int)((op68 >> 9) & 7u);
					const int am = (int)(op68 & 7u);
					const uint32 a = (an == 7) ? gpr(1)
							      : gpr(16 + an);
					const uint32 b = (am == 7) ? gpr(1)
							      : gpr(16 + am);
					const int word =
						((op68 & 0xf1f8u) == 0xb0c8u);
					g3_ccr = 0;
					if (word) {
						uint32 av = a & 0xffffu;
						uint32 bv = b & 0xffffu;
						uint32 r = (uint16)(av - bv);
						if (r == 0)
							g3_ccr |= 4;
						if ((int16)r < 0)
							g3_ccr |= 8;
						if (av < bv)
							g3_ccr |= 1;
					} else {
						uint32 r = a - b;
						if (r == 0)
							g3_ccr |= 4;
						if ((int32)r < 0)
							g3_ccr |= 8;
						if (a < b)
							g3_ccr |= 1;
					}
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf1f8u) == 0x9048u ||
				    (op68 & 0xf1f8u) == 0x9088u ||
				    (op68 & 0xf1f8u) == 0xd048u ||
				    (op68 & 0xf1f8u) == 0xd088u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const int an = (int)(op68 & 7u);
					const uint32 src = (an == 7) ? gpr(1)
							       : gpr(16 + an);
					const int word =
						((op68 & 0xf1f8u) == 0x9048u ||
						 (op68 & 0xf1f8u) == 0xd048u);
					const int sub =
						((op68 & 0xf000u) == 0x9000u);
					uint32 a = gpr(8 + dn);
					uint32 r;
					if (word) {
						uint32 av = a & 0xffffu;
						uint32 bv = src & 0xffffu;
						r = sub ? (av - bv) : (av + bv);
						r &= 0xffffu;
						gpr(8 + dn) = (a & 0xffff0000u) | r;
						g3_ccr = 0;
						if (r == 0)
							g3_ccr |= 4;
						if ((int16)r < 0)
							g3_ccr |= 8;
					} else {
						r = sub ? (a - src) : (a + src);
						gpr(8 + dn) = r;
						g3_ccr = 0;
						if (r == 0)
							g3_ccr |= 4;
						if ((int32)r < 0)
							g3_ccr |= 8;
					}
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nsuba;
						if (nsuba < 8) {
							nsuba++;
							nw_boot_log("G3: 68k SUB/ADD An,Dn");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1c0u) == 0x90c0u &&
				    (((op68 >> 3) & 7u) <= 1u ||
				     (((op68 >> 3) & 7u) == 7u &&
				      (op68 & 7u) == 4u))) {
					const int dn = (int)((op68 >> 9) & 7u);
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					uint32 src;
					if (sm == 7u && sr == 4u) {
						src = vm_read_memory_2(r24);
						r24 += 2;
					} else if (sm == 0u)
						src = gpr(8 + (int)sr);
					else
						src = (sr == 7u) ? gpr(1)
							      : gpr(16 + (int)sr);
					uint32 dst = (dn == 7) ? gpr(1)
							       : gpr(16 + dn);
					const uint32 adj = (uint32)(int32)(int16)src;
					dst -= adj;
					if (dn == 7)
						gpr(1) = dst;
					else
						gpr(16 + dn) = dst;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf1c0u) == 0xd0c0u &&
				    (((op68 >> 3) & 7u) <= 1u ||
				     (((op68 >> 3) & 7u) == 7u &&
				      ((op68 & 7u) == 4u ||
				       (op68 & 7u) == 0u)))) {
					const int dn = (int)((op68 >> 9) & 7u);
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					uint32 src;
					if (sm == 7u && sr == 4u) {
						src = vm_read_memory_2(r24);
						r24 += 2;
					} else if (sm == 7u && sr == 0u) {
						const int32 a =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						src = vm_read_memory_2(
							g3_rom0((uint32)a));
					} else if (sm == 0u)
						src = gpr(8 + (int)sr);
					else
						src = (sr == 7u) ? gpr(1)
							      : gpr(16 + (int)sr);
					uint32 dst = (dn == 7) ? gpr(1)
							       : gpr(16 + dn);
					dst += (uint32)(int32)(int16)src;
					if (dn == 7)
						gpr(1) = dst;
					else
						gpr(16 + dn) = dst;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf1c0u) == 0xc180u &&
				    ((op68 >> 3) & 7u) == 2u) {
					/* AND.L Dn,(An). 0x16dec c390.
					 * Mask 0xc1c0 never matches (opmode 110). */
					const int dn = (int)((op68 >> 9) & 7u);
					const int an = (int)(op68 & 7u);
					uint32 a = (an == 7) ? gpr(1)
							     : gpr(16 + an);
					uint32 v = 0;
					if (g3_ea_data(a))
						v = vm_read_memory_4(g3_rom0(a));
					v &= gpr(8 + dn);
					if (g3_ea_data(a))
						vm_write_memory_4(g3_rom0(a), v);
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int32)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nandla;
						if (nandla < 8) {
							nandla++;
							nw_boot_log("G3: 68k AND.L Dn,(An) c180");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1ffu) == 0xc0bcu) {
					/* AND.L #imm,Dn. 0x1b72a c0bc. */
					const int dn = (int)((op68 >> 9) & 7u);
					const uint32 imm = vm_read_memory_4(r24);
					r24 += 4;
					const uint32 v = gpr(8 + dn) & imm;
					gpr(8 + dn) = v;
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int32)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nandi;
						if (nandi < 8) {
							nandi++;
							nw_boot_log("G3: 68k AND.L #imm,Dn");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1c0u) == 0xc080u &&
				    ((op68 >> 3) & 7u) == 0u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const int sn = (int)(op68 & 7u);
					const uint32 v = gpr(8 + dn) & gpr(8 + sn);
					gpr(8 + dn) = v;
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int32)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (((op68 & 0xf1c0u) == 0xc080u ||
				     (op68 & 0xf1c0u) == 0x8080u) &&
				    (((op68 >> 3) & 7u) == 2u ||
				     ((op68 >> 3) & 7u) == 3u ||
				     ((op68 >> 3) & 7u) == 5u)) {
					const int dn = (int)((op68 >> 9) & 7u);
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					uint32 a = (sr == 7u) ? gpr(1)
							 : gpr(16 + (int)sr);
					if (sm == 5u) {
						const int32 d =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						a += d;
					}
					uint32 src = 0;
					if (g3_ea_data(a))
						src = vm_read_memory_4(g3_rom0(a));
					if (sm == 3u) {
						a += 4u;
						if (sr == 7u)
							gpr(1) = a;
						else
							gpr(16 + (int)sr) = a;
					}
					uint32 v = gpr(8 + dn);
					if ((op68 & 0xf000u) == 0x8000u)
						v |= src;
					else
						v &= src;
					gpr(8 + dn) = v;
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int32)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nandl;
						if (nandl < 8) {
							nandl++;
							nw_boot_log("G3: 68k AND.L (An)+");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1c0u) == 0xc040u &&
				    ((op68 >> 3) & 7u) == 0u) {
					/* AND.W Dm,Dn. 0x90092 c240 after
					 * MOVE.W #$8000,D1; unhosted dest-edges
					 * onto 8000. Do not host AB1D. */
					const int dn = (int)((op68 >> 9) & 7u);
					const int sm = (int)(op68 & 7u);
					const uint32 v = (gpr(8 + dn) &
							  gpr(8 + sm)) & 0xffffu;
					gpr(8 + dn) = (gpr(8 + dn) & 0xffff0000u) | v;
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nandwd;
						if (nandwd < 8) {
							nandwd++;
							nw_boot_log("G3: 68k AND.W Dn,Dn");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1c0u) == 0xc040u &&
				    ((op68 >> 3) & 7u) == 3u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const unsigned sr = op68 & 7u;
					uint32 a = (sr == 7u) ? gpr(1)
							 : gpr(16 + (int)sr);
					uint32 src = 0;
					if (g3_ea_data(a))
						src = vm_read_memory_2(g3_rom0(a));
					a += 2u;
					if (sr == 7u)
						gpr(1) = a;
					else
						gpr(16 + (int)sr) = a;
					const uint32 v = (gpr(8 + dn) & src) & 0xffffu;
					gpr(8 + dn) = (gpr(8 + dn) & 0xffff0000u) | v;
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nandw;
						if (nandw < 8) {
							nandw++;
							nw_boot_log("G3: 68k AND.W (An)+");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1ffu) == 0x51b8u) {
					/* SUBQ.L #n,abs.W. Mask 0x53b8 never
					 * matched (quick field); dest-edge
					 * fetched $020C at 0x94b8 / 0xa01c. */
					const unsigned q = (op68 >> 9) & 7u;
					const uint32 n = q ? q : 8u;
					const int32 a =
						(int16)vm_read_memory_2(r24);
					r24 += 2;
					uint32 v = 0;
					if (g3_ea_data((uint32)a))
						v = vm_read_memory_4(g3_rom0((uint32)a));
					v -= n;
					if (g3_ea_data((uint32)a))
						vm_write_memory_4(g3_rom0((uint32)a), v);
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int32)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nsubqa;
						if (nsubqa < 8) {
							nsubqa++;
							char buf[80];
							snprintf(buf, sizeof(buf),
								 "G3: 68k SUBQ.L abs.W n=%u",
								 (unsigned)n);
							nw_boot_log(buf);
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf100u) == 0x5000u &&
				    ((op68 >> 3) & 7u) == 6u) {
					const unsigned imm = (op68 >> 9) & 7u;
					const unsigned sz = (op68 >> 6) & 3u;
					const unsigned sr = op68 & 7u;
					const uint32 n = (imm == 0u) ? 8u : imm;
					const uint32 basea = (sr == 7u) ? gpr(1)
								       : gpr(16 + (int)sr);
					uint32 ext = vm_read_memory_2(r24);
					r24 += 2;
					const int da = (int)((ext >> 15) & 1u);
					const int xr = (int)((ext >> 12) & 7u);
					const int wl = (int)((ext >> 11) & 1u);
					const int sc = (int)((ext >> 9) & 3u);
					uint32 xn = da ? ((xr == 7) ? gpr(1)
							       : gpr(16 + xr))
						      : gpr(8 + xr);
					if (!wl)
						xn = (uint32)(int32)(int16)xn;
					xn <<= sc;
					uint32 addr = basea;
					if ((ext & 0x100u) == 0)
						addr = basea +
						       (int32)(int8)(ext & 0xffu) +
						       xn;
					else {
						const int bs = (int)((ext >> 7) & 1u);
						const int isup = (int)((ext >> 6) & 1u);
						const int bdsz = (int)((ext >> 4) & 3u);
						const int iis = (int)(ext & 7u);
						uint32 bd = 0;
						if (bdsz == 2) {
							bd = (uint32)(int32)(int16)
								vm_read_memory_2(r24);
							r24 += 2;
						} else if (bdsz == 3) {
							bd = vm_read_memory_4(r24);
							r24 += 4;
						}
						uint32 od = 0;
						if (iis == 2 || iis == 3 ||
						    iis == 6 || iis == 7) {
							od = (uint32)(int32)(int16)
								vm_read_memory_2(r24);
							r24 += 2;
						} else if (iis == 4) {
							od = vm_read_memory_4(r24);
							r24 += 4;
						}
						uint32 inner = (bs ? 0 : basea) + bd;
						if (!isup && iis < 6)
							inner += xn;
						if (iis >= 2 && g3_ea_data(inner))
							inner = vm_read_memory_4(
								g3_rom0(inner));
						if (!isup && iis >= 6)
							inner += xn;
						addr = inner + od;
					}
					uint32 v = 0;
					if (g3_ea_data(addr)) {
						if (sz == 2u)
							v = vm_read_memory_4(
								g3_rom0(addr));
						else if (sz == 1u)
							v = vm_read_memory_2(
								g3_rom0(addr));
						else
							v = vm_read_memory_1(
								g3_rom0(addr));
					}
					v += n;
					if (g3_ea_data(addr)) {
						if (sz == 2u)
							vm_write_memory_4(
								g3_rom0(addr), v);
						else if (sz == 1u)
							vm_write_memory_2(
								g3_rom0(addr),
								(uint16)v);
						else
							vm_write_memory_1(
								g3_rom0(addr),
								(uint8)v);
					}
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned naddq;
						if (naddq < 8) {
							naddq++;
							nw_boot_log("G3: 68k ADDQ idx");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1c0u) == 0xd000u &&
				    ((op68 >> 3) & 7u) <= 5u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					uint32 b = 0;
					int got = 0;
					if (sm == 0u) {
						b = gpr(8 + (int)sr);
						got = 1;
					} else if (sm == 1u) {
						b = (sr == 7u) ? gpr(1)
							       : gpr(16 + (int)sr);
						got = 1;
					} else if (sm == 2u) {
						const uint32 a = (sr == 7u) ? gpr(1)
									    : gpr(16 + (int)sr);
						b = vm_read_memory_1(g3_rom0(a));
						got = 1;
					} else if (sm == 5u) {
						const int32 d =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						const uint32 a = ((sr == 7u) ? gpr(1)
									     : gpr(16 + (int)sr)) + d;
						b = vm_read_memory_1(g3_rom0(a));
						got = 1;
					}
					if (got) {
						const uint32 r =
							((gpr(8 + dn) + b) & 0xffu);
						gpr(8 + dn) =
							(gpr(8 + dn) & 0xffffff00u) | r;
						g3_ccr = 0;
						if (r == 0)
							g3_ccr |= 4;
						if ((int8)r < 0)
							g3_ccr |= 8;
						gpr(24) = r24;
						gpr(27) = 0xffffffffu;
						gpr(29) = ROMBase + 0x380000u;
						pc() = ROMBase + 0x366084u;
						continue;
					}
				}
				if (((op68 & 0xf1c0u) == 0xd040u ||
				     (op68 & 0xf1c0u) == 0x9040u) &&
				    (((op68 >> 3) & 7u) <= 5u ||
				     (((op68 >> 3) & 7u) == 7u &&
				      ((op68 & 7u) == 0u ||
				       (op68 & 7u) == 4u)))) {
					const int dn = (int)((op68 >> 9) & 7u);
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					uint32 b = 0;
					int got = 0;
					if (sm == 0u) {
						b = gpr(8 + (int)sr);
						got = 1;
					} else if (sm == 1u) {
						b = (sr == 7u) ? gpr(1)
							       : gpr(16 + (int)sr);
						got = 1;
					} else if (sm == 2u) {
						const uint32 a = (sr == 7u) ? gpr(1)
									    : gpr(16 + (int)sr);
						b = vm_read_memory_2(g3_rom0(a));
						got = 1;
					} else if (sm == 5u) {
						const int32 d =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						{
							const uint32 a =
								((sr == 7u) ? gpr(1)
									    : gpr(16 + (int)sr)) + d;
							b = vm_read_memory_2(g3_rom0(a));
						}
						got = 1;
					} else if (sm == 7u && sr == 0u) {
						const int32 a =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						b = vm_read_memory_2(
							g3_rom0((uint32)a));
						got = 1;
					} else if (sm == 7u && sr == 4u) {
						b = vm_read_memory_2(r24);
						r24 += 2;
						got = 1;
					}
					if (got) {
						const int sub =
							((op68 & 0xf000u) == 0x9000u);
						uint32 av = gpr(8 + dn) & 0xffffu;
						uint32 bv = b & 0xffffu;
						uint32 r = sub ? (av - bv)
							       : (av + bv);
						r &= 0xffffu;
						gpr(8 + dn) =
							(gpr(8 + dn) & 0xffff0000u) | r;
						g3_ccr = 0;
						if (r == 0)
							g3_ccr |= 4;
						if ((int16)r < 0)
							g3_ccr |= 8;
						gpr(24) = r24;
						gpr(27) = 0xffffffffu;
						gpr(29) = ROMBase + 0x380000u;
						pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
						{
							static unsigned naddw;
							if (naddw < 8) {
								naddw++;
								nw_boot_log("G3: 68k ADD.W d16");
							}
						}
#endif
						continue;
					}
				}
				if ((op68 & 0xf1c0u) == 0xb1c0u ||
				    (op68 & 0xf1c0u) == 0xb0c0u) {
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					const int an = (int)((op68 >> 9) & 7u);
					uint32 m = 0;
					int got = 0;
					if (sm == 0u) {
						m = gpr(8 + (int)sr);
						got = 1;
					} else if (sm == 1u) {
						m = g3_rom0((sr == 7u) ? gpr(1)
								      : gpr(16 + (int)sr));
						got = 1;
					} else if (sm == 2u || sm == 3u) {
						uint32 a = (sr == 7u) ? gpr(1)
								      : gpr(16 + (int)sr);
						m = vm_read_memory_4(g3_rom0(a));
						if (sm == 3u) {
							if (sr == 7u)
								gpr(1) = a + 4;
							else
								gpr(16 + (int)sr) = a + 4;
						}
						got = 1;
					} else if (sm == 5u) {
						const int32 d16 =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						{
							const uint32 a =
								((sr == 7u) ? gpr(1)
									    : gpr(16 + (int)sr)) +
								d16;
							if ((op68 & 0xf1c0u) == 0xb0c0u)
								m = vm_read_memory_2(
									g3_rom0(a));
							else
								m = vm_read_memory_4(
									g3_rom0(a));
						}
						got = 1;
					} else if (sm == 7u && sr == 0u) {
						const int32 a =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						if ((op68 & 0xf1c0u) == 0xb0c0u)
							m = vm_read_memory_2(
								g3_rom0((uint32)a));
						else
							m = vm_read_memory_4(
								g3_rom0((uint32)a));
						got = 1;
					} else if (sm == 7u && sr == 4u) {
						if ((op68 & 0xf1c0u) == 0xb0c0u) {
							m = (uint32)(int32)(int16)
								vm_read_memory_2(r24);
							r24 += 2;
						} else {
							m = vm_read_memory_4(r24);
							r24 += 4;
						}
						got = 1;
					} else if (sm == 6u) {
						const uint32 ext =
							vm_read_memory_2(r24);
						r24 += 2;
						const uint32 basea = (sr == 7u)
							? gpr(1)
							: gpr(16 + (int)sr);
						const int da = (int)((ext >> 15) & 1u);
						const int xr = (int)((ext >> 12) & 7u);
						const int wl = (int)((ext >> 11) & 1u);
						const int sc = (int)((ext >> 9) & 3u);
						const int32 disp = (int8)(ext & 0xffu);
						uint32 xn = da ? ((xr == 7) ? gpr(1)
								       : gpr(16 + xr))
							      : gpr(8 + xr);
						if (!wl)
							xn = (uint32)(int32)(int16)xn;
						xn <<= sc;
						{
							const uint32 a = basea + disp + xn;
							if ((op68 & 0xf1c0u) == 0xb0c0u)
								m = vm_read_memory_2(
									g3_rom0(a));
							else
								m = vm_read_memory_4(
									g3_rom0(a));
						}
						got = 1;
					}
					if (got) {
						uint32 d = (an == 7) ? gpr(1)
								     : gpr(16 + an);
						if (!(sm == 7u && sr == 4u))
							d = g3_rom0(d);
						if ((op68 & 0xf1c0u) == 0xb0c0u) {
							m = (uint32)(int32)(int16)m;
							d = (uint32)(int32)(int16)d;
						}
						const uint32 r = d - m;
						g3_ccr = 0;
						if (r == 0)
							g3_ccr |= 4;
						if ((int32)r < 0)
							g3_ccr |= 8;
						if (d < m)
							g3_ccr |= 1;
						gpr(24) = r24;
						gpr(27) = 0xffffffffu;
						gpr(29) = ROMBase + 0x380000u;
						pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
						{
							static unsigned ncmpa;
							if (ncmpa < 8) {
								ncmpa++;
								nw_boot_log("G3: 68k CMPA imm");
							}
						}
#endif
						continue;
					}
				}
				if ((op68 & 0xf0c0u) == 0xb000u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					uint8 b = 0;
					int got = 0;
					if (sm == 0u) {
						b = (uint8)gpr(8 + (int)sr);
						got = 1;
					} else if (sm == 1u) {
						b = (uint8)((sr == 7u) ? gpr(1)
							: gpr(16 + (int)sr));
						got = 1;
					} else if (sm == 2u) {
						const uint32 a = (sr == 7u) ? gpr(1)
									    : gpr(16 + (int)sr);
						b = (uint8)vm_read_memory_1(g3_rom0(a));
						got = 1;
					} else if (sm == 5u) {
						const int32 d16 =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						{
							const uint32 a =
								((sr == 7u) ? gpr(1)
									    : gpr(16 + (int)sr)) +
								d16;
							b = (uint8)vm_read_memory_1(
								g3_rom0(a));
						}
						got = 1;
					} else if (sm == 7u && sr == 0u) {
						const int32 a =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						b = (uint8)vm_read_memory_1(
							g3_rom0((uint32)a));
						got = 1;
					} else if (sm == 6u) {
						const uint32 ext = vm_read_memory_2(r24);
						r24 += 2;
						const uint32 basea = (sr == 7u)
							? gpr(1)
							: gpr(16 + (int)sr);
						const int da = (int)((ext >> 15) & 1u);
						const int xr = (int)((ext >> 12) & 7u);
						const int wl = (int)((ext >> 11) & 1u);
						const int sc = (int)((ext >> 9) & 3u);
						const int32 disp = (int8)(ext & 0xffu);
						uint32 xn = da ? ((xr == 7) ? gpr(1)
								       : gpr(16 + xr))
							      : gpr(8 + xr);
						if (!wl)
							xn = (uint32)(int32)(int16)xn;
						xn <<= sc;
						b = (uint8)vm_read_memory_1(
							g3_rom0(basea + disp + xn));
						got = 1;
					}
					if (got) {
						const uint8 a = (uint8)gpr(8 + dn);
						const uint8 r = (uint8)(a - b);
						g3_ccr = 0;
						if (r == 0)
							g3_ccr |= 4;
						if ((int8)r < 0)
							g3_ccr |= 8;
						if (a < b)
							g3_ccr |= 1;
						gpr(24) = r24;
						gpr(27) = 0xffffffffu;
						gpr(29) = ROMBase + 0x380000u;
						pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
						{
							static unsigned ncmpbd;
							if (ncmpbd < 8) {
								ncmpbd++;
								nw_boot_log("G3: 68k CMP.B d16");
							}
						}
#endif
						continue;
					}
				}
				if ((op68 & 0xf000u) == 0xe000u &&
				    ((op68 >> 6) & 3u) < 3u) {
					const unsigned sz = (op68 >> 6) & 3u;
					const unsigned left = (op68 >> 8) & 1u;
					const unsigned ir = (op68 >> 5) & 1u;
					const unsigned typ = (op68 >> 3) & 3u;
					const unsigned cr = (op68 >> 9) & 7u;
					const int dn = (int)(op68 & 7u);
					unsigned cnt = ir ? (gpr(8 + (int)cr) & 63u)
							 : (cr ? cr : 8u);
					const unsigned bits = sz == 0u ? 8u
						: (sz == 1u ? 16u : 32u);
					const uint32 mask = (bits == 32u)
						? 0xffffffffu
						: ((1u << bits) - 1u);
					uint32 v = gpr(8 + dn) & mask;
					if (cnt >= bits && (typ == 1u))
						v = 0;
					else {
						cnt %= bits;
						if (typ == 3u && cnt) {
							if (left)
								v = ((v << cnt) |
								     (v >> (bits - cnt))) & mask;
							else
								v = ((v >> cnt) |
								     (v << (bits - cnt))) & mask;
						} else if (typ == 1u && cnt) {
							v = left ? ((v << cnt) & mask)
								 : (v >> cnt);
						} else if (typ == 0u && cnt) {
							if (left)
								v = (v << cnt) & mask;
							else {
								const uint32 s =
									v & (1u << (bits - 1u));
								v >>= cnt;
								if (s) {
									const uint32 smask =
										(cnt >= bits) ? mask
										: (((1u << cnt) - 1u)
										   << (bits - cnt));
									v |= smask;
								}
							}
						}
					}
					if (sz == 0u)
						gpr(8 + dn) = (gpr(8 + dn) & 0xffffff00u) | v;
					else if (sz == 1u)
						gpr(8 + dn) = (gpr(8 + dn) & 0xffff0000u) | v;
					else
						gpr(8 + dn) = v;
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if (v & (1u << (bits - 1u)))
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf1c0u) == 0xd080u &&
				    ((op68 >> 3) & 7u) == 1u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const int sn = (int)(op68 & 7u);
					gpr(8 + dn) += (sn == 7) ? gpr(1)
								: gpr(16 + sn);
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf1c0u) == 0xd080u &&
				    ((op68 >> 3) & 7u) == 2u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const int sn = (int)(op68 & 7u);
					uint32 a = (sn == 7) ? gpr(1) : gpr(16 + sn);
					if (g3_ea_data(a))
						gpr(8 + dn) += vm_read_memory_4(g3_rom0(a));
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf1c0u) == 0xd080u &&
				    ((op68 >> 3) & 7u) == 3u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const unsigned sr = op68 & 7u;
					uint32 addr = (sr == 7u) ? gpr(1)
								 : gpr(16 + (int)sr);
					gpr(8 + dn) += vm_read_memory_4(g3_rom0(addr));
					if (sr == 7u)
						gpr(1) = addr + 4;
					else
						gpr(16 + (int)sr) = addr + 4;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf1c0u) == 0xd080u &&
				    ((op68 >> 3) & 7u) == 0u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const int sn = (int)(op68 & 7u);
					gpr(8 + dn) += gpr(8 + sn);
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf1c0u) == 0xd080u &&
				    ((op68 >> 3) & 7u) == 5u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const unsigned sr = op68 & 7u;
					const int32 d =
						(int16)vm_read_memory_2(r24);
					r24 += 2;
					const uint32 a = ((sr == 7u) ? gpr(1)
							     : gpr(16 + (int)sr)) + d;
					gpr(8 + dn) += vm_read_memory_4(g3_rom0(a));
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xffc0u) == 0x4c00u ||
				    (op68 & 0xffc0u) == 0x4c40u) {
					const uint32 ext = vm_read_memory_2(r24);
					r24 += 2;
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					uint32 src = 0;
					int got = 0;
					if (sm == 0u) {
						src = gpr(8 + (int)sr);
						got = 1;
					} else if (sm == 2u) {
						const uint32 a = (sr == 7u) ? gpr(1)
									    : gpr(16 + (int)sr);
						src = vm_read_memory_4(g3_rom0(a));
						got = 1;
					} else if (sm == 5u) {
						const int32 d =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						{
							const uint32 a =
								((sr == 7u) ? gpr(1)
									    : gpr(16 + (int)sr)) + d;
							src = vm_read_memory_4(g3_rom0(a));
						}
						got = 1;
					} else if (sm == 7u && sr == 4u) {
						src = vm_read_memory_4(r24);
						r24 += 4;
						got = 1;
					} else if (sm == 7u && sr == 0u) {
						const int32 a =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						src = vm_read_memory_4(
							g3_rom0((uint32)a));
						got = 1;
					} else if (sm == 6u) {
						r24 += 2;
					} else if (sm == 7u && sr == 1u) {
						r24 += 4;
					}
					const int is_div =
						((op68 & 0xffc0u) == 0x4c40u);
					const int is_s = (int)((ext >> 15) & 1u);
					const int dl = (int)((ext >> 12) & 7u);
					const int sz64 = (int)((ext >> 10) & 1u);
					const int dh = (int)(ext & 7u);
					g3_ccr = 0;
					if (got && !is_div) {
						if (is_s) {
							long long p =
								(long long)(int32)gpr(8 + dl) *
								(long long)(int32)src;
							gpr(8 + dl) = (uint32)p;
							if (sz64)
								gpr(8 + dh) = (uint32)(
									(unsigned long long)p >> 32);
							if (p == 0)
								g3_ccr |= 4;
							if (p < 0)
								g3_ccr |= 8;
							if (!sz64 &&
							    (p < -(long long)0x80000000LL ||
							     p > 0x7fffffffLL))
								g3_ccr |= 2;
						} else {
							unsigned long long p =
								(unsigned long long)gpr(8 + dl) *
								(unsigned long long)src;
							gpr(8 + dl) = (uint32)p;
							if (sz64)
								gpr(8 + dh) = (uint32)(p >> 32);
							if (p == 0)
								g3_ccr |= 4;
							if (!sz64 && (p >> 32))
								g3_ccr |= 2;
						}
					} else if (got && is_div) {
						if (src == 0)
							g3_ccr |= 1;
						else if (!sz64) {
							if (is_s) {
								int32 num = (int32)gpr(8 + dl);
								int32 den = (int32)src;
								int32 q = num / den;
								int32 r = num % den;
								gpr(8 + dh) = (uint32)r;
								gpr(8 + dl) = (uint32)q;
								if (q == 0)
									g3_ccr |= 4;
								if (q < 0)
									g3_ccr |= 8;
							} else {
								uint32 num = gpr(8 + dl);
								uint32 q = num / src;
								uint32 r = num % src;
								gpr(8 + dh) = r;
								gpr(8 + dl) = q;
								if (q == 0)
									g3_ccr |= 4;
							}
						} else {
							unsigned long long num =
								((unsigned long long)gpr(8 + dh) << 32) |
								gpr(8 + dl);
							if (is_s) {
								long long n = (long long)num;
								long long den = (int32)src;
								long long q = n / den;
								long long r = n % den;
								if (q > 0x7fffffffLL ||
								    q < -(long long)0x80000000LL)
									g3_ccr |= 2;
								else {
									gpr(8 + dh) = (uint32)r;
									gpr(8 + dl) = (uint32)q;
									if (q == 0)
										g3_ccr |= 4;
									if (q < 0)
										g3_ccr |= 8;
								}
							} else {
								unsigned long long q = num / src;
								unsigned long long r = num % src;
								if (q > 0xffffffffull)
									g3_ccr |= 2;
								else {
									gpr(8 + dh) = (uint32)r;
									gpr(8 + dl) = (uint32)q;
									if (q == 0)
										g3_ccr |= 4;
								}
							}
						}
					}
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmull;
						if (nmull < 8) {
							nmull++;
							nw_boot_log("G3: 68k MUL.L/DIV.L");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1c0u) == 0x0100u) {
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					const unsigned dn = (op68 >> 9) & 7u;
					const unsigned bit = gpr(8 + (int)dn);
					uint32 val = 0;
					unsigned width = 7u;
					if (sm == 0u) {
						val = gpr(8 + (int)sr);
						width = 31u;
					} else if (sm == 2u) {
						const uint32 a = (sr == 7u) ? gpr(1)
									    : gpr(16 + (int)sr);
						val = vm_read_memory_1(g3_rom0(a));
					} else if (sm == 5u) {
						const int32 d =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						const uint32 a = ((sr == 7u) ? gpr(1)
									     : gpr(16 + (int)sr)) + d;
						val = vm_read_memory_1(g3_rom0(a));
					} else {
						width = 0;
					}
					if (width) {
						g3_ccr &= ~4u;
						if ((val & (1u << (bit & width))) == 0)
							g3_ccr |= 4;
						gpr(24) = r24;
						gpr(27) = 0xffffffffu;
						gpr(29) = ROMBase + 0x380000u;
						pc() = ROMBase + 0x366084u;
						continue;
					}
				}
				if ((op68 & 0xf0c0u) == 0xe0c0u &&
				    ((op68 >> 3) & 7u) == 0u) {
					const unsigned left = (op68 >> 8) & 1u;
					const unsigned typ = (op68 >> 9) & 3u;
					const int dn = (int)(op68 & 7u);
					uint32 v = gpr(8 + dn);
					if (typ == 0u || typ == 1u) {
						if (left)
							v <<= 1;
						else
							v >>= 1;
					} else {
						if (left)
							v = (v << 1) | (v >> 31);
						else
							v = (v >> 1) | (v << 31);
					}
					gpr(8 + dn) = v;
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int32)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xffc0u) == 0x0800u) {
					const uint32 bit = vm_read_memory_2(r24);
					r24 += 2;
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					uint32 val = 0;
					unsigned width = 31u;
					if (sm == 0u)
						val = gpr(8 + (int)sr);
					else if (sm == 1u)
						val = (sr == 7u) ? gpr(1)
								 : gpr(16 + (int)sr);
					else if (sm == 2u) {
						const uint32 a = (sr == 7u) ? gpr(1)
									    : gpr(16 + (int)sr);
						val = vm_read_memory_1(a);
						width = 7u;
					} else if (sm == 5u) {
						const int32 d =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						const uint32 a = ((sr == 7u) ? gpr(1)
									     : gpr(16 + (int)sr)) + d;
						val = vm_read_memory_1(a);
						width = 7u;
					} else if (sm == 7u && sr == 0u) {
						const uint32 a = (uint32)(int32)(int16)
							vm_read_memory_2(r24);
						r24 += 2;
						val = vm_read_memory_1(a);
						width = 7u;
					} else if (sm == 7u && sr == 1u) {
						const uint32 a = vm_read_memory_4(r24);
						r24 += 4;
						val = vm_read_memory_1(a);
						width = 7u;
					}
					g3_ccr &= ~4u;
					if ((val & (1u << (bit & width))) == 0)
						g3_ccr |= 4;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xff00u) == 0x0800u &&
				    ((op68 >> 6) & 3u) <= 3u) {
					const uint32 bitw = vm_read_memory_2(r24);
					r24 += 2;
					const unsigned typ = (op68 >> 6) & 3u;
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					uint32 addr = 0;
					uint32 val = 0;
					unsigned width = 7u;
					int mem = 0;
					int got = 0;
					if (sm == 0u) {
						val = gpr(8 + (int)sr);
						width = 31u;
						got = 1;
					} else if (sm == 2u) {
						addr = (sr == 7u) ? gpr(1)
								  : gpr(16 + (int)sr);
						mem = 1;
						got = 1;
					} else if (sm == 5u) {
						const int32 d =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						addr = ((sr == 7u) ? gpr(1)
								   : gpr(16 + (int)sr)) + d;
						mem = 1;
						got = 1;
					} else if (sm == 7u && sr == 0u) {
						addr = (uint32)(int32)(int16)
							vm_read_memory_2(r24);
						r24 += 2;
						mem = 1;
						got = 1;
					} else if (sm == 7u && sr == 1u) {
						r24 += 4;
					} else if (sm == 6u) {
						r24 += 2;
					}
					if (got && mem && g3_ea_data(addr))
						val = vm_read_memory_1(g3_rom0(addr));
					if (got) {
						const unsigned b = (unsigned)bitw & width;
						g3_ccr &= ~4u;
						if ((val & (1u << b)) == 0)
							g3_ccr |= 4;
						if (typ == 1u)
							val ^= (1u << b);
						else if (typ == 2u)
							val &= ~(1u << b);
						else if (typ == 3u)
							val |= (1u << b);
						if (typ != 0u) {
							if (sm == 0u)
								gpr(8 + (int)sr) = val;
							else if (mem && g3_ea_data(addr))
								vm_write_memory_1(
									g3_rom0(addr),
									(uint8)val);
						}
					}
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nbclr;
						if (nbclr < 8) {
							nbclr++;
							nw_boot_log("G3: 68k BCLR (An)");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1f8u) == 0x8100u) {
					const int dn = (int)((op68 >> 9) & 7u);
					gpr(8 + dn) &= 0xffffff00u;
					g3_ccr = 4;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf100u) == 0x8100u &&
				    ((op68 >> 3) & 7u) == 5u) {
					r24 += 2;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				/* MOVE.W/B #imm, d16(An): imm + disp. The
				 * d16 catch-all below only ate one word, so
				 * 3d7c 0020 ffb8 ran ffb8 as 68k. */
				if ((op68 & 0xc000u) == 0 &&
				    (((op68 >> 12) & 3u) == 1u ||
				     ((op68 >> 12) & 3u) == 3u) &&
				    ((op68 >> 6) & 7u) == 5u &&
				    ((op68 >> 3) & 7u) == 7u &&
				    (op68 & 7u) == 4u) {
					const int szw =
						((op68 >> 12) & 3u) == 3u;
					uint32 v = vm_read_memory_2(r24);
					r24 += 2;
					if (!szw)
						v &= 0xffu;
					const int32 d =
						(int16)vm_read_memory_2(r24);
					r24 += 2;
					const unsigned dr = (op68 >> 9) & 7u;
					uint32 da = ((dr == 7u) ? gpr(1)
							     : gpr(16 + (int)dr)) + d;
					if (g3_ea_data(da)) {
						if (szw)
							vm_write_memory_2(
								g3_rom0(da), v);
						else
							vm_write_memory_1(
								g3_rom0(da), v);
					}
					g3_ccr = 0;
					if (szw) {
						if ((v & 0xffffu) == 0)
							g3_ccr |= 4;
						if ((int16)v < 0)
							g3_ccr |= 8;
					} else {
						if ((v & 0xffu) == 0)
							g3_ccr |= 4;
						if ((int8)v < 0)
							g3_ccr |= 8;
					}
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmimm;
						if (nmimm < 8) {
							nmimm++;
							char buf[80];
							snprintf(buf, sizeof(buf),
								 "G3: 68k MOVE.W # d16 pc=%08x",
								 (unsigned)r24);
							nw_boot_log(buf);
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    (((op68 >> 12) & 3u) == 1u ||
				     ((op68 >> 12) & 3u) == 3u) &&
				    (((op68 >> 6) & 7u) == 5u ||
				     ((op68 >> 3) & 7u) == 5u)) {
					r24 += 2;
					if (((op68 >> 6) & 7u) == 5u &&
					    ((op68 >> 3) & 7u) == 7u &&
					    (op68 & 7u) == 4u)
						r24 += 2;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (((op68 & 0xff00u) == 0 ||
				     (op68 & 0xff00u) == 0x0200u ||
				     (op68 & 0xff00u) == 0x0400u ||
				     (op68 & 0xff00u) == 0x0600u ||
				     (op68 & 0xff00u) == 0x0a00u) &&
				    op68 != 0 &&
				    ((op68 >> 6) & 3u) < 3u &&
				    ((op68 >> 3) & 7u) == 5u) {
					const unsigned sz = (op68 >> 6) & 3u;
					const int an = (int)(op68 & 7u);
					uint32 imm;
					if (sz == 2u) {
						imm = vm_read_memory_4(r24);
						r24 += 4;
					} else {
						imm = vm_read_memory_2(r24);
						r24 += 2;
						if (sz == 0u)
							imm &= 0xffu;
					}
					const int32 d =
						(int16)vm_read_memory_2(r24);
					r24 += 2;
					uint32 a = ((an == 7) ? gpr(1)
							 : gpr(16 + an)) + d;
					uint32 v = 0;
					if (g3_ea_data(a)) {
						if (sz == 2u)
							v = vm_read_memory_4(g3_rom0(a));
						else if (sz == 1u)
							v = vm_read_memory_2(g3_rom0(a));
						else
							v = vm_read_memory_1(g3_rom0(a));
					}
					const uint32 hi = op68 & 0xff00u;
					if (hi == 0x0600u)
						v += imm;
					else if (hi == 0x0400u)
						v -= imm;
					else if (hi == 0x0a00u)
						v ^= imm;
					else if (hi == 0x0200u)
						v &= imm;
					else
						v |= imm;
					if (g3_ea_data(a)) {
						if (sz == 2u)
							vm_write_memory_4(g3_rom0(a), v);
						else if (sz == 1u)
							vm_write_memory_2(
								g3_rom0(a), v);
						else
							vm_write_memory_1(
								g3_rom0(a), v);
					}
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (((op68 & 0xff00u) == 0 ||
				     (op68 & 0xff00u) == 0x0200u ||
				     (op68 & 0xff00u) == 0x0400u ||
				     (op68 & 0xff00u) == 0x0600u ||
				     (op68 & 0xff00u) == 0x0a00u) &&
				    op68 != 0 &&
				    ((op68 >> 6) & 3u) < 3u &&
				    (((op68 >> 3) & 7u) == 2u ||
				     ((op68 >> 3) & 7u) == 3u)) {
					const unsigned sz = (op68 >> 6) & 3u;
					const unsigned sm = (op68 >> 3) & 7u;
					const int an = (int)(op68 & 7u);
					uint32 imm;
					if (sz == 2u) {
						imm = vm_read_memory_4(r24);
						r24 += 4;
					} else {
						imm = vm_read_memory_2(r24);
						r24 += 2;
						if (sz == 0u)
							imm &= 0xffu;
					}
					uint32 a = (an == 7) ? gpr(1)
							 : gpr(16 + an);
					if (sm == 3u) {
						unsigned inc = sz == 2u ? 4u
							: (sz == 1u ? 2u : 1u);
						if (an == 7 && inc < 2u)
							inc = 2u;
						if (an == 7)
							gpr(1) = a + inc;
						else
							gpr(16 + an) = a + inc;
					}
					uint32 v = 0;
					if (g3_ea_data(a)) {
						if (sz == 2u)
							v = vm_read_memory_4(g3_rom0(a));
						else if (sz == 1u)
							v = vm_read_memory_2(g3_rom0(a));
						else
							v = vm_read_memory_1(g3_rom0(a));
					}
					const uint32 hi = op68 & 0xff00u;
					if (hi == 0x0600u)
						v += imm;
					else if (hi == 0x0400u)
						v -= imm;
					else if (hi == 0x0a00u)
						v ^= imm;
					else if (hi == 0x0200u)
						v &= imm;
					else
						v |= imm;
					if (g3_ea_data(a)) {
						if (sz == 2u)
							vm_write_memory_4(g3_rom0(a), v);
						else if (sz == 1u)
							vm_write_memory_2(
								g3_rom0(a), v);
						else
							vm_write_memory_1(
								g3_rom0(a), v);
					}
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nandi;
						if (nandi < 8) {
							nandi++;
							nw_boot_log("G3: 68k ANDI (An)");
						}
					}
#endif
					continue;
				}
				if (((op68 & 0xff00u) == 0x0200u ||
				     (op68 & 0xff00u) == 0x0400u ||
				     (op68 & 0xff00u) == 0x0600u ||
				     (op68 & 0xff00u) == 0x0a00u) &&
				    ((op68 >> 6) & 3u) < 3u &&
				    ((op68 >> 3) & 7u) == 0u) {
					const unsigned sz = (op68 >> 6) & 3u;
					const int dn = (int)(op68 & 7u);
					uint32 imm;
					if (sz == 2u) {
						imm = vm_read_memory_4(r24);
						r24 += 4;
					} else {
						imm = vm_read_memory_2(r24);
						r24 += 2;
						if (sz == 0u)
							imm &= 0xffu;
						else
							imm &= 0xffffu;
					}
					uint32 v = gpr(8 + dn);
					if ((op68 & 0xff00u) == 0x0600u) {
						if (sz == 2u)
							v += imm;
						else if (sz == 1u)
							v = (v & 0xffff0000u) |
							    ((v + imm) & 0xffffu);
						else
							v = (v & 0xffffff00u) |
							    ((v + imm) & 0xffu);
					} else if ((op68 & 0xff00u) == 0x0400u) {
						if (sz == 2u)
							v -= imm;
						else if (sz == 1u)
							v = (v & 0xffff0000u) |
							    ((v - imm) & 0xffffu);
						else
							v = (v & 0xffffff00u) |
							    ((v - imm) & 0xffu);
					} else if ((op68 & 0xff00u) == 0x0a00u) {
						if (sz == 2u)
							v ^= imm;
						else if (sz == 1u)
							v = (v & 0xffff0000u) |
							    ((v ^ imm) & 0xffffu);
						else
							v = (v & 0xffffff00u) |
							    ((v ^ imm) & 0xffu);
					} else if (sz == 2u)
						v &= imm;
					else if (sz == 1u)
						v = (v & 0xffff0000u) | ((v & imm) & 0xffffu);
					else
						v = (v & 0xffffff00u) | ((v & imm) & 0xffu);
					gpr(8 + dn) = v;
					g3_ccr = 0;
					if (sz == 2u) {
						if (v == 0)
							g3_ccr |= 4;
						if ((int32)v < 0)
							g3_ccr |= 8;
					} else if (sz == 1u) {
						if ((v & 0xffffu) == 0)
							g3_ccr |= 4;
						if ((int16)v < 0)
							g3_ccr |= 8;
					} else {
						if ((v & 0xffu) == 0)
							g3_ccr |= 4;
						if ((int8)v < 0)
							g3_ccr |= 8;
					}
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x4240u ||
				    (op68 & 0xfff8u) == 0x4280u) {
					gpr(8 + (int)(op68 & 7u)) = 0;
					g3_ccr = 4;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				/* CLR.W -(An). Exception epilogue 0x492e
				 * 4267 CLR.W -(SP). */
				if ((op68 & 0xfff8u) == 0x4260u) {
					const int an = (int)(op68 & 7u);
					uint32 a = (an == 7) ? gpr(1)
						     : gpr(16 + an);
					a -= 2u;
					if (g3_ea_data(a))
						vm_write_memory_2(g3_rom0(a), 0);
					if (an == 7)
						gpr(1) = a;
					else
						gpr(16 + an) = a;
					g3_ccr = 4;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				/* CLR.L -(An). 0x8872 42a7 CLR.L -(SP). */
				if ((op68 & 0xfff8u) == 0x42a0u) {
					const int an = (int)(op68 & 7u);
					uint32 a = (an == 7) ? gpr(1)
						     : gpr(16 + an);
					a -= 4u;
					if (g3_ea_data(a))
						vm_write_memory_4(g3_rom0(a), 0);
					if (an == 7)
						gpr(1) = a;
					else
						gpr(16 + an) = a;
					g3_ccr = 4;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nclrlpre;
						if (nclrlpre < 8) {
							nclrlpre++;
							nw_boot_log("G3: 68k CLR.L -(An)");
						}
					}
#endif
					continue;
				}
				if (op68 == 0x4278u) {
					const int32 a =
						(int16)vm_read_memory_2(r24);
					if (g3_ea_data((uint32)a))
						vm_write_memory_2(g3_rom0((uint32)a), 0);
					g3_ccr = 4;
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				/* CLR.B/W/L d16(An). FileMgr 0x65f24
				 * CLR.W d16(A6). */
				if ((op68 & 0xfff8u) == 0x4228u ||
				    (op68 & 0xfff8u) == 0x4268u ||
				    (op68 & 0xfff8u) == 0x42a8u) {
					const int an = (int)(op68 & 7u);
					const int32 d =
						(int16)vm_read_memory_2(r24);
					uint32 a = ((an == 7) ? gpr(1)
						    : gpr(16 + an)) + d;
					if (g3_ea_data(a)) {
						a = g3_rom0(a);
						if ((op68 & 0xfff8u) == 0x42a8u)
							vm_write_memory_4(a, 0);
						else if ((op68 & 0xfff8u) ==
							 0x4268u)
							vm_write_memory_2(a, 0);
						else
							vm_write_memory_1(a, 0);
					}
					g3_ccr = 4;
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nclrd;
						if (nclrd < 8) {
							nclrd++;
							nw_boot_log("G3: 68k CLR d16(An)");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xffc0u) == 0x4440u &&
				    ((op68 >> 3) & 7u) == 5u) {
					const int an = (int)(op68 & 7u);
					const int32 d =
						(int16)vm_read_memory_2(r24);
					uint32 a = ((an == 7) ? gpr(1)
						    : gpr(16 + an)) + d;
					uint32 v = 0;
					if (g3_ea_data(a))
						v = vm_read_memory_2(g3_rom0(a)) & 0xffffu;
					v = (uint16)(-(int16)v);
					if (g3_ea_data(a))
						vm_write_memory_2(g3_rom0(a), (uint16)v);
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nnegw;
						if (nnegw < 8) {
							nnegw++;
							nw_boot_log("G3: 68k NEG.W d16");
						}
					}
#endif
					continue;
				}
				if (op68 == 0x4238u) {
					const int32 a =
						(int16)vm_read_memory_2(r24);
					if (g3_ea_data((uint32)a))
						vm_write_memory_1(g3_rom0((uint32)a), 0);
					g3_ccr = 4;
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x4210u) {
					const int an = (int)(op68 & 7u);
					uint32 a = (an == 7) ? gpr(1) : gpr(16 + an);
					if (g3_ea_data(a))
						vm_write_memory_1(g3_rom0(a), 0);
					g3_ccr = 4;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x4218u) {
					const int an = (int)(op68 & 7u);
					uint32 a = (an == 7) ? gpr(1) : gpr(16 + an);
					if (g3_ea_data(a))
						vm_write_memory_1(g3_rom0(a), 0);
					a += 1;
					if (an == 7)
						gpr(1) = a;
					else
						gpr(16 + an) = a;
					g3_ccr = 4;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x4290u) {
					const int an = (int)(op68 & 7u);
					uint32 a = (an == 7) ? gpr(1) : gpr(16 + an);
					if (g3_ea_data(a))
						vm_write_memory_4(g3_rom0(a), 0);
					g3_ccr = 4;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x4298u) {
					const int an = (int)(op68 & 7u);
					uint32 a = (an == 7) ? gpr(1) : gpr(16 + an);
					if (g3_ea_data(a))
						vm_write_memory_4(g3_rom0(a), 0);
					a += 4;
					if (an == 7)
						gpr(1) = a;
					else
						gpr(16 + an) = a;
					g3_ccr = 4;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (op68 == 0x42b8u) {
					const int32 a =
						(int16)vm_read_memory_2(r24);
					if (g3_ea_data((uint32)a))
						vm_write_memory_4(g3_rom0((uint32)a), 0);
					g3_ccr = 4;
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x4220u) {
					const int an = (int)(op68 & 7u);
					uint32 a = ((an == 7) ? gpr(1)
							    : gpr(16 + an)) - 1u;
					if (an == 7)
						gpr(1) = a;
					else
						gpr(16 + an) = a;
					if (g3_ea_data(a))
						vm_write_memory_1(g3_rom0(a), 0);
					g3_ccr = 4;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x4250u) {
					const int an = (int)(op68 & 7u);
					uint32 a = (an == 7) ? gpr(1) : gpr(16 + an);
					if (g3_ea_data(a))
						vm_write_memory_2(g3_rom0(a), 0);
					g3_ccr = 4;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x4258u) {
					const int an = (int)(op68 & 7u);
					uint32 a = (an == 7) ? gpr(1) : gpr(16 + an);
					if (g3_ea_data(a))
						vm_write_memory_2(g3_rom0(a), 0);
					a += 2;
					if (an == 7)
						gpr(1) = a;
					else
						gpr(16 + an) = a;
					g3_ccr = 4;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x4260u) {
					const int an = (int)(op68 & 7u);
					uint32 a = ((an == 7) ? gpr(1)
							    : gpr(16 + an)) - 2u;
					if (an == 7)
						gpr(1) = a;
					else
						gpr(16 + an) = a;
					if (g3_ea_data(a))
						vm_write_memory_2(g3_rom0(a), 0);
					g3_ccr = 4;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x4230u) {
					const int an = (int)(op68 & 7u);
					const uint32 ext = vm_read_memory_2(r24);
					const uint32 basea = (an == 7) ? gpr(1)
								      : gpr(16 + an);
					const int da = (int)((ext >> 15) & 1u);
					const int xr = (int)((ext >> 12) & 7u);
					const int wl = (int)((ext >> 11) & 1u);
					const int32 disp = (int8)(ext & 0xffu);
					uint32 xn = da ? ((xr == 7) ? gpr(1)
							       : gpr(16 + xr))
						      : gpr(8 + xr);
					if (!wl)
						xn = (uint32)(int32)(int16)xn;
					xn <<= (int)((ext >> 9) & 3u);
					{
						uint32 a = basea + disp + xn;
						if (g3_ea_data(a))
							vm_write_memory_1(g3_rom0(a), 0);
					}
					g3_ccr = 4;
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x4228u) {
					const int an = (int)(op68 & 7u);
					const int32 d =
						(int16)vm_read_memory_2(r24);
					uint32 a = ((an == 7) ? gpr(1)
							    : gpr(16 + an)) + d;
					if (g3_ea_data(a))
						vm_write_memory_1(g3_rom0(a), 0);
					g3_ccr = 4;
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x4268u) {
					const int an = (int)(op68 & 7u);
					const int32 d =
						(int16)vm_read_memory_2(r24);
					uint32 a = ((an == 7) ? gpr(1)
							    : gpr(16 + an)) + d;
					if (g3_ea_data(a))
						vm_write_memory_2(g3_rom0(a), 0);
					g3_ccr = 4;
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x42a8u) {
					const int an = (int)(op68 & 7u);
					const int32 d =
						(int16)vm_read_memory_2(r24);
					uint32 a = ((an == 7) ? gpr(1)
							    : gpr(16 + an)) + d;
					if (g3_ea_data(a))
						vm_write_memory_4(g3_rom0(a), 0);
					g3_ccr = 4;
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x4290u) {
					const int an = (int)(op68 & 7u);
					uint32 a = (an == 7) ? gpr(1) : gpr(16 + an);
					if (g3_ea_data(a))
						vm_write_memory_4(g3_rom0(a), 0);
					g3_ccr = 4;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (op68 == 0x42b8u) {
					const int32 a =
						(int16)vm_read_memory_2(r24);
					if (g3_ea_data((uint32)a))
						vm_write_memory_4(g3_rom0((uint32)a), 0);
					g3_ccr = 4;
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x42a0u) {
					const int an = (int)(op68 & 7u);
					uint32 a = ((an == 7) ? gpr(1)
							    : gpr(16 + an)) - 4u;
					if (an == 7)
						gpr(1) = a;
					else
						gpr(16 + an) = a;
					if (g3_ea_data(a))
						vm_write_memory_4(g3_rom0(a), 0);
					g3_ccr = 4;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x42b0u) {
					const int an = (int)(op68 & 7u);
					const uint32 ext = vm_read_memory_2(r24);
					const uint32 basea = (an == 7) ? gpr(1)
								      : gpr(16 + an);
					const int da = (int)((ext >> 15) & 1u);
					const int xr = (int)((ext >> 12) & 7u);
					const int wl = (int)((ext >> 11) & 1u);
					const int32 disp = (int8)(ext & 0xffu);
					uint32 xn = da ? ((xr == 7) ? gpr(1)
							       : gpr(16 + xr))
						      : gpr(8 + xr);
					if (!wl)
						xn = (uint32)(int32)(int16)xn;
					xn <<= (int)((ext >> 9) & 3u);
					{
						uint32 a = basea + disp + xn;
						if (g3_ea_data(a))
							vm_write_memory_4(g3_rom0(a), 0);
					}
					g3_ccr = 4;
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x4298u) {
					const int an = (int)(op68 & 7u);
					uint32 a = (an == 7) ? gpr(1) : gpr(16 + an);
					if (g3_ea_data(a))
						vm_write_memory_4(g3_rom0(a), 0);
					a += 4;
					if (an == 7)
						gpr(1) = a;
					else
						gpr(16 + an) = a;
					g3_ccr = 4;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x4640u) {
					const int dn = (int)(op68 & 7u);
					uint32 v = gpr(8 + dn) ^ 0xffffu;
					gpr(8 + dn) = (gpr(8 + dn) & 0xffff0000u) |
						      (v & 0xffffu);
					g3_ccr = 0;
					if ((v & 0xffffu) == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x0880u) {
					const uint32 bit =
						vm_read_memory_2(r24) & 31u;
					r24 += 2;
					const int dn = (int)(op68 & 7u);
					uint32 v = gpr(8 + dn);
					const uint32 mask = 1u << bit;
					g3_ccr = (g3_ccr & ~4u);
					if ((v & mask) == 0)
						g3_ccr |= 4;
					gpr(8 + dn) = v & ~mask;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x4840u) {
					const int dn = (int)(op68 & 7u);
					uint32 v = gpr(8 + dn);
					v = (v << 16) | (v >> 16);
					gpr(8 + dn) = v;
					g3_ccr = 0;
					if ((v & 0xffffu) == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x48c0u) {
					/* Boot-dependent: real 16→32 sign-extend
					   milled bits/unique (pid 29428). Leave as
					   no-op; later 4880/49c0 EXT.W/EXTB.L still
					   run. */
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xffc0u) == 0x4840u &&
				    ((op68 >> 3) & 7u) == 5u) {
					const int an = (int)(op68 & 7u);
					const uint32 basea = (an == 7) ? gpr(1)
								      : gpr(16 + an);
					const int32 d =
						(int16)vm_read_memory_2(r24);
					gpr(1) -= 4;
					vm_write_memory_4(gpr(1), basea + d);
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xff00u) == 0x4400u &&
				    ((op68 >> 6) & 3u) < 3u &&
				    ((op68 >> 3) & 7u) == 0u) {
					const unsigned sz = (op68 >> 6) & 3u;
					const int dn = (int)(op68 & 7u);
					if (sz == 0u) {
						const uint8 r = (uint8)(-(int8)gpr(8 + dn));
						gpr(8 + dn) = (gpr(8 + dn) & 0xffffff00u) | r;
						g3_ccr = 0;
						if (r == 0)
							g3_ccr |= 4;
						if ((int8)r < 0)
							g3_ccr |= 8;
					} else if (sz == 1u) {
						const uint16 r = (uint16)(-(int16)gpr(8 + dn));
						gpr(8 + dn) = (gpr(8 + dn) & 0xffff0000u) | r;
						g3_ccr = 0;
						if (r == 0)
							g3_ccr |= 4;
						if ((int16)r < 0)
							g3_ccr |= 8;
					} else {
						gpr(8 + dn) = -gpr(8 + dn);
						g3_ccr = 0;
						if (gpr(8 + dn) == 0)
							g3_ccr |= 4;
						if ((int32)gpr(8 + dn) < 0)
							g3_ccr |= 8;
					}
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf1ffu) == 0x10bcu) {
					const int an = (int)((op68 >> 9) & 7u);
					const uint32 v = vm_read_memory_2(r24) & 0xffu;
					const uint32 dsta = (an == 7) ? gpr(1)
								      : gpr(16 + an);
					if (g3_ea_data(dsta))
						vm_write_memory_1(g3_rom0(dsta), v);
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int8)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf0f8u) == 0x50c8u) {
					const int32 d =
						(int16)vm_read_memory_2(r24);
					const uint32 base = r24;
					r24 += 2;
					const int dn = (int)(op68 & 7u);
					uint32 v = gpr(8 + dn);
					const uint16 ctr = (uint16)(v - 1u);
					gpr(8 + dn) = (v & 0xffff0000u) | ctr;
					const unsigned cc =
						(op68 >> 8) & 0xfu;
					const int nset = (g3_ccr & 8) != 0;
					const int vset = (g3_ccr & 2) != 0;
					int cc_true = 0;
					if (cc == 0u)
						cc_true = 1;
					else if (cc == 1u)
						cc_true = 0;
					else if (cc == 2u)
						cc_true = ((g3_ccr & 5) == 0);
					else if (cc == 3u)
						cc_true = ((g3_ccr & 5) != 0);
					else if (cc == 4u)
						cc_true = ((g3_ccr & 1) == 0);
					else if (cc == 5u)
						cc_true = ((g3_ccr & 1) != 0);
					else if (cc == 6u)
						cc_true = ((g3_ccr & 4) == 0);
					else if (cc == 7u)
						cc_true = ((g3_ccr & 4) != 0);
					else if (cc == 8u)
						cc_true = ((g3_ccr & 2) == 0);
					else if (cc == 9u)
						cc_true = ((g3_ccr & 2) != 0);
					else if (cc == 10u)
						cc_true = !nset;
					else if (cc == 11u)
						cc_true = nset;
					else if (cc == 12u)
						cc_true = (nset == vset);
					else if (cc == 13u)
						cc_true = (nset != vset);
					else if (cc == 14u)
						cc_true = ((g3_ccr & 4) == 0) && (nset == vset);
					else
						cc_true = ((g3_ccr & 4) != 0) || (nset != vset);
					if (!cc_true && ctr != 0xffffu &&
					    g3_r24_ok(base + d)) {
						const uint32 dest = base + d;
						/* Cap empty/self DBF only (d>=-2).
						 * Copy loops are MOVE +(An)+; DBF d=-4. */
						if (d >= -2) {
							static uint32 dbf_dest[4];
							static unsigned dbf_n[4];
							static unsigned dbf_wr;
							unsigned i, slot = 4;
							for (i = 0; i < 4u; i++) {
								if (dbf_dest[i] == dest) {
									slot = i;
									break;
								}
							}
							if (slot == 4u) {
								slot = dbf_wr++ & 3u;
								dbf_dest[slot] = dest;
								dbf_n[slot] = 0;
							}
							if (++dbf_n[slot] > 16u) {
								gpr(24) = r24;
#if NW_BOOT_LOG
								{
									static unsigned ndbfcap;
									if (ndbfcap < 8) {
										ndbfcap++;
										nw_boot_log("G3: 68k DBF wait cap");
									}
								}
#endif
							} else
								gpr(24) = dest;
						} else
							gpr(24) = dest;
					} else
						gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf0f8u) == 0x50c0u) {
					const unsigned cc =
						(op68 >> 8) & 0xfu;
					const int nset = (g3_ccr & 8) != 0;
					const int vset = (g3_ccr & 2) != 0;
					int cc_true = 0;
					if (cc == 0u)
						cc_true = 1;
					else if (cc == 1u)
						cc_true = 0;
					else if (cc == 2u)
						cc_true = ((g3_ccr & 5) == 0);
					else if (cc == 3u)
						cc_true = ((g3_ccr & 5) != 0);
					else if (cc == 4u)
						cc_true = ((g3_ccr & 1) == 0);
					else if (cc == 5u)
						cc_true = ((g3_ccr & 1) != 0);
					else if (cc == 6u)
						cc_true = ((g3_ccr & 4) == 0);
					else if (cc == 7u)
						cc_true = ((g3_ccr & 4) != 0);
					else if (cc == 8u)
						cc_true = ((g3_ccr & 2) == 0);
					else if (cc == 9u)
						cc_true = ((g3_ccr & 2) != 0);
					else if (cc == 10u)
						cc_true = !nset;
					else if (cc == 11u)
						cc_true = nset;
					else if (cc == 12u)
						cc_true = (nset == vset);
					else if (cc == 13u)
						cc_true = (nset != vset);
					else if (cc == 14u)
						cc_true = ((g3_ccr & 4) == 0) && (nset == vset);
					else
						cc_true = ((g3_ccr & 4) != 0) || (nset != vset);
					const int dn = (int)(op68 & 7u);
					gpr(8 + dn) = (gpr(8 + dn) & 0xffffff00u) |
						      (cc_true ? 0xffu : 0u);
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf0ffu) == 0x50f8u) {
					const int32 a =
						(int16)vm_read_memory_2(r24);
					r24 += 2;
					if (g3_ea_data((uint32)a))
						vm_write_memory_1(g3_rom0((uint32)a), 0xff);
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf0c0u) == 0x50c0u) {
					const unsigned sm = (op68 >> 3) & 7u;
					if (sm == 7u && (op68 & 7u) == 0u)
						r24 += 2;
					else if (sm == 5u)
						r24 += 2;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf1c0u) == 0x0180u ||
				    (op68 & 0xf1c0u) == 0x01c0u) {
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					const unsigned bit = gpr(8 + (int)((op68 >> 9) & 7u));
					const int is_set = ((op68 & 0xf1c0u) == 0x01c0u);
					if (sm == 0u) {
						uint32 val = gpr(8 + (int)sr);
						const unsigned b = bit & 31u;
						g3_ccr &= ~4u;
						if ((val & (1u << b)) == 0)
							g3_ccr |= 4;
						if (is_set)
							val |= (1u << b);
						else
							val &= ~(1u << b);
						gpr(8 + (int)sr) = val;
					} else if (sm == 2u || sm == 3u ||
						   sm == 4u) {
						uint32 a = (sr == 7u) ? gpr(1)
								      : gpr(16 + (int)sr);
						if (sm == 4u)
							a -= 1;
						uint8 val = 0;
						if (g3_ea_data(a))
							val = (uint8)vm_read_memory_1(
								g3_rom0(a));
						const unsigned b = bit & 7u;
						g3_ccr &= ~4u;
						if ((val & (1u << b)) == 0)
							g3_ccr |= 4;
						if (is_set)
							val = (uint8)(val | (1u << b));
						else
							val = (uint8)(val & ~(1u << b));
						if (g3_ea_data(a))
							vm_write_memory_1(g3_rom0(a), val);
						if (sm == 3u)
							a += 1;
						if (sm == 3u || sm == 4u) {
							if (sr == 7u)
								gpr(1) = a;
							else
								gpr(16 + (int)sr) = a;
						}
					}
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xff00u) == 0x0800u) {
					const uint32 bit = vm_read_memory_2(r24);
					r24 += 2;
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					const unsigned kind = (op68 >> 6) & 3u;
					uint32 addr = 0;
					int mem = 0;
					if (sm == 0u) {
						uint32 val = gpr(8 + (int)sr);
						const unsigned b = (unsigned)(bit & 31u);
						g3_ccr &= ~4u;
						if ((val & (1u << b)) == 0)
							g3_ccr |= 4;
						if (kind == 2u)
							val &= ~(1u << b);
						else if (kind == 3u)
							val |= (1u << b);
						else if (kind == 1u)
							val ^= (1u << b);
						gpr(8 + (int)sr) = val;
					} else {
						if (sm == 2u || sm == 3u)
							addr = (sr == 7u) ? gpr(1)
									  : gpr(16 + (int)sr);
						else if (sm == 5u) {
							const int32 d =
								(int16)vm_read_memory_2(r24);
							r24 += 2;
							addr = ((sr == 7u) ? gpr(1)
									   : gpr(16 + (int)sr)) + d;
						}
						else if (sm == 7u && sr == 0u) {
							addr = (uint32)(int32)(int16)
								vm_read_memory_2(r24);
							r24 += 2;
						} else
							mem = -1;
						if (mem == 0) {
							uint8 val = 0;
							if (g3_ea_data(addr))
								val = (uint8)vm_read_memory_1(
									g3_rom0(addr));
							const unsigned b =
								(unsigned)(bit & 7u);
							g3_ccr &= ~4u;
							if ((val & (1u << b)) == 0)
								g3_ccr |= 4;
							if (kind == 2u)
								val = (uint8)(val & ~(1u << b));
							else if (kind == 3u)
								val = (uint8)(val | (1u << b));
							else if (kind == 1u)
								val = (uint8)(val ^ (1u << b));
							if (kind != 0u && g3_ea_data(addr))
								vm_write_memory_1(
									g3_rom0(addr), val);
							if (sm == 3u) {
								if (sr == 7u)
									gpr(1) = addr + 1;
								else
									gpr(16 + (int)sr) =
										addr + 1;
							}
						}
					}
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf1c0u) == 0x9080u &&
				    ((op68 >> 3) & 7u) == 3u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const unsigned sr = op68 & 7u;
					uint32 addr = (sr == 7u) ? gpr(1)
								 : gpr(16 + (int)sr);
					gpr(8 + dn) -= vm_read_memory_4(g3_rom0(addr));
					if (sr == 7u)
						gpr(1) = addr + 4;
					else
						gpr(16 + (int)sr) = addr + 4;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (((op68 & 0xf1c0u) == 0x9080u ||
				     (op68 & 0xf1c0u) == 0xd080u) &&
				    ((op68 >> 3) & 7u) == 5u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const unsigned sr = op68 & 7u;
					const int32 d =
						(int16)vm_read_memory_2(r24);
					r24 += 2;
					const uint32 a = ((sr == 7u) ? gpr(1)
							     : gpr(16 + (int)sr)) + d;
					const uint32 src = vm_read_memory_4(g3_rom0(a));
					if ((op68 & 0xf000u) == 0x9000u)
						gpr(8 + dn) -= src;
					else
						gpr(8 + dn) += src;
					g3_ccr = 0;
					if (gpr(8 + dn) == 0)
						g3_ccr |= 4;
					if ((int32)gpr(8 + dn) < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (((op68 & 0xf1c0u) == 0x9080u ||
				     (op68 & 0xf1c0u) == 0xd080u) &&
				    ((op68 >> 3) & 7u) == 6u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const unsigned sr = op68 & 7u;
					const uint32 basea = (sr == 7u) ? gpr(1)
								      : gpr(16 + (int)sr);
					const uint32 ext = vm_read_memory_2(r24);
					r24 += 2;
					const int da = (int)((ext >> 15) & 1u);
					const int xr = (int)((ext >> 12) & 7u);
					const int wl = (int)((ext >> 11) & 1u);
					const int32 disp = (int8)(ext & 0xffu);
					uint32 xn = da ? ((xr == 7) ? gpr(1)
							       : gpr(16 + xr))
						      : gpr(8 + xr);
					if (!wl)
						xn = (uint32)(int32)(int16)xn;
					uint32 a = basea + disp + xn;
					uint32 src = 0;
					if (g3_ea_data(a))
						src = vm_read_memory_4(g3_rom0(a));
					if ((op68 & 0xf000u) == 0x9000u)
						gpr(8 + dn) -= src;
					else
						gpr(8 + dn) += src;
					g3_ccr = 0;
					if (gpr(8 + dn) == 0)
						g3_ccr |= 4;
					if ((int32)gpr(8 + dn) < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nsubidx;
						if (nsubidx < 8) {
							nsubidx++;
							nw_boot_log("G3: 68k SUB.L idx,Dn");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1ffu) == 0xd1b8u ||
				    (op68 & 0xf1ffu) == 0x91b8u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const int32 a =
						(int16)vm_read_memory_2(r24);
					r24 += 2;
					uint32 v = 0;
					if (g3_ea_data((uint32)a))
						v = vm_read_memory_4(g3_rom0((uint32)a));
					if ((op68 & 0xf000u) == 0x9000u)
						v -= gpr(8 + dn);
					else
						v += gpr(8 + dn);
					if (g3_ea_data((uint32)a))
						vm_write_memory_4(g3_rom0((uint32)a), v);
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int32)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned naddabs;
						if (naddabs < 8) {
							naddabs++;
							nw_boot_log("G3: 68k ADD.L Dn,abs.W");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1c0u) == 0xc000u &&
				    (((op68 >> 3) & 7u) == 2u ||
				     ((op68 >> 3) & 7u) == 3u)) {
					const int dn = (int)((op68 >> 9) & 7u);
					const unsigned sr = op68 & 7u;
					const unsigned sm = (op68 >> 3) & 7u;
					uint32 a = (sr == 7u) ? gpr(1)
							 : gpr(16 + (int)sr);
					uint32 v = 0;
					if (g3_ea_data(a))
						v = vm_read_memory_1(g3_rom0(a));
					if (sm == 3u) {
						a += 1u;
						if (sr == 7u)
							gpr(1) = a;
						else
							gpr(16 + (int)sr) = a;
					}
					v = (v & (gpr(8 + dn) & 0xffu)) & 0xffu;
					gpr(8 + dn) = (gpr(8 + dn) & 0xffffff00u) | v;
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int8)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nandban2;
						if (nandban2 < 8) {
							nandban2++;
							nw_boot_log("G3: 68k AND.B (An),Dn");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1c0u) == 0xc100u &&
				    (((op68 >> 3) & 7u) == 2u ||
				     ((op68 >> 3) & 7u) == 6u)) {
					const int dn = (int)((op68 >> 9) & 7u);
					const unsigned sr = op68 & 7u;
					const unsigned sm = (op68 >> 3) & 7u;
					uint32 a = (sr == 7u) ? gpr(1)
							 : gpr(16 + (int)sr);
					if (sm == 6u) {
						const uint32 ext =
							vm_read_memory_2(r24);
						r24 += 2;
						const int da = (int)((ext >> 15) & 1u);
						const int xr = (int)((ext >> 12) & 7u);
						const int wl = (int)((ext >> 11) & 1u);
						const int32 disp = (int8)(ext & 0xffu);
						uint32 xn = da ? ((xr == 7) ? gpr(1)
								       : gpr(16 + xr))
							      : gpr(8 + xr);
						if (!wl)
							xn = (uint32)(int32)(int16)xn;
						a = a + disp + xn;
					}
					uint32 v = 0;
					if (g3_ea_data(a))
						v = vm_read_memory_1(g3_rom0(a));
					v = (v & gpr(8 + dn)) & 0xffu;
					if (g3_ea_data(a))
						vm_write_memory_1(g3_rom0(a), (uint8)v);
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int8)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nandban;
						if (nandban < 8) {
							nandban++;
							nw_boot_log("G3: 68k AND.B Dn,(An)");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1c0u) == 0xd100u &&
				    ((op68 >> 3) & 7u) == 2u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const unsigned sr = op68 & 7u;
					uint32 a = (sr == 7u) ? gpr(1)
							 : gpr(16 + (int)sr);
					uint32 v = 0;
					if (g3_ea_data(a))
						v = vm_read_memory_1(g3_rom0(a));
					v = (v + (gpr(8 + dn) & 0xffu)) & 0xffu;
					if (g3_ea_data(a))
						vm_write_memory_1(g3_rom0(a), (uint8)v);
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int8)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned naddban;
						if (naddban < 8) {
							naddban++;
							nw_boot_log("G3: 68k ADD.B Dn,(An)");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 3u &&
				    ((op68 >> 6) & 7u) == 3u &&
				    ((op68 >> 3) & 7u) == 0u) {
					const unsigned dr = (op68 >> 9) & 7u;
					const unsigned sr = op68 & 7u;
					uint32 v = gpr(8 + (int)sr) & 0xffffu;
					uint32 da = (dr == 7u) ? gpr(1)
							 : gpr(16 + (int)dr);
					if (g3_ea_data(da))
						vm_write_memory_2(g3_rom0(da), v);
					da += 2u;
					if (dr == 7u)
						gpr(1) = da;
					else
						gpr(16 + (int)dr) = da;
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmwapo;
						if (nmwapo < 8) {
							nmwapo++;
							nw_boot_log("G3: 68k MOVE.W Dn,(An)+");
						}
					}
#endif
					continue;
				}
				if (((op68 & 0xf1c0u) == 0xd180u ||
				     (op68 & 0xf1c0u) == 0x9180u) &&
				    (((op68 >> 3) & 7u) == 2u ||
				     ((op68 >> 3) & 7u) == 5u)) {
					const int dn = (int)((op68 >> 9) & 7u);
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					uint32 a = (sr == 7u) ? gpr(1)
							 : gpr(16 + (int)sr);
					if (sm == 5u) {
						const int32 d =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						a += d;
					}
					uint32 v = 0;
					if (g3_ea_data(a))
						v = vm_read_memory_4(g3_rom0(a));
					if ((op68 & 0xf000u) == 0x9000u)
						v -= gpr(8 + dn);
					else
						v += gpr(8 + dn);
					if (g3_ea_data(a))
						vm_write_memory_4(g3_rom0(a), v);
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int32)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned naddld;
						if (naddld < 8) {
							naddld++;
							nw_boot_log("G3: 68k ADD.L Dn,d16");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1ffu) == 0xd0bcu ||
				    (op68 & 0xf1ffu) == 0x90bcu) {
					const int dn = (int)((op68 >> 9) & 7u);
					const uint32 imm = vm_read_memory_4(r24);
					r24 += 4;
					if ((op68 & 0xf000u) == 0x9000u)
						gpr(8 + dn) -= imm;
					else
						gpr(8 + dn) += imm;
					g3_ccr = 0;
					if (gpr(8 + dn) == 0)
						g3_ccr |= 4;
					if ((int32)gpr(8 + dn) < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned naddli;
						if (naddli < 8) {
							naddli++;
							nw_boot_log("G3: 68k ADD.L #imm,Dn");
						}
					}
#endif
					continue;
				}
				if (((op68 & 0xf1c0u) == 0xd140u ||
				     (op68 & 0xf1c0u) == 0x9140u) &&
				    (((op68 >> 3) & 7u) == 2u ||
				     ((op68 >> 3) & 7u) == 5u)) {
					const int dn = (int)((op68 >> 9) & 7u);
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					uint32 a = (sr == 7u) ? gpr(1)
							 : gpr(16 + (int)sr);
					if (sm == 5u) {
						const int32 d =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						a += d;
					}
					uint32 v = 0;
					if (g3_ea_data(a))
						v = vm_read_memory_2(g3_rom0(a)) & 0xffffu;
					if ((op68 & 0xf000u) == 0x9000u)
						v = (v - (gpr(8 + dn) & 0xffffu)) & 0xffffu;
					else
						v = (v + (gpr(8 + dn) & 0xffffu)) & 0xffffu;
					if (g3_ea_data(a))
						vm_write_memory_2(g3_rom0(a), (uint16)v);
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned naddwa;
						if (naddwa < 8) {
							naddwa++;
							nw_boot_log("G3: 68k ADD.W Dn,d16(An)");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1c0u) == 0x9080u &&
				    ((op68 >> 3) & 7u) <= 1u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					uint32 m = (sm == 0u) ? gpr(8 + (int)sr)
						: ((sr == 7u) ? gpr(1) : gpr(16 + (int)sr));
					gpr(8 + dn) -= m;
					g3_ccr = 0;
					if (gpr(8 + dn) == 0)
						g3_ccr |= 4;
					if ((int32)gpr(8 + dn) < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf1c0u) == 0x80c0u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const unsigned sm = (op68 >> 3) & 7u;
					const int sn = (int)(op68 & 7u);
					uint32 div = 0;
					int ok = 0;
					if (sm == 0u) {
						div = gpr(8 + sn) & 0xffffu;
						ok = 1;
					} else if (sm == 5u) {
						const int32 d =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						{
							const uint32 a =
								((sn == 7) ? gpr(1)
									   : gpr(16 + sn)) + d;
							div = vm_read_memory_2(g3_rom0(a));
						}
						ok = 1;
					} else if (sm == 7u && sn == 4) {
						div = vm_read_memory_2(r24);
						r24 += 2;
						ok = 1;
					} else if (sm == 7u && sn == 0) {
						const int32 a =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						div = vm_read_memory_2(
							g3_rom0((uint32)a));
						ok = 1;
#if NW_BOOT_LOG
						{
							static unsigned ndivuaw;
							if (ndivuaw < 8) {
								ndivuaw++;
								nw_boot_log("G3: 68k DIVU.W abs.W");
							}
						}
#endif
					}
					if (ok) {
						uint32 d = gpr(8 + dn);
						g3_ccr = 0;
						if (div == 0) {
							g3_ccr |= 1;
						} else {
							uint32 q = d / div;
							uint32 rem = d % div;
							if (q > 0xffffu)
								g3_ccr |= 2;
							else {
								gpr(8 + dn) = (rem << 16) |
									      (q & 0xffffu);
								if (q == 0)
									g3_ccr |= 4;
							}
						}
						gpr(24) = r24;
						gpr(27) = 0xffffffffu;
						gpr(29) = ROMBase + 0x380000u;
						pc() = ROMBase + 0x366084u;
						continue;
					}
				}
				if (((op68 & 0xf1c0u) == 0xc040u ||
				     (op68 & 0xf1c0u) == 0xc080u) &&
				    ((op68 >> 3) & 7u) <= 1u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const unsigned sm = (op68 >> 3) & 7u;
					const int sn = (int)(op68 & 7u);
					uint32 src = (sm == 0u) ? gpr(8 + sn)
						: ((sn == 7) ? gpr(1)
							     : gpr(16 + sn));
					uint32 dst = gpr(8 + dn);
					if ((op68 & 0xf1c0u) == 0xc040u) {
						dst = (dst & 0xffff0000u) |
						      ((dst & src) & 0xffffu);
					} else
						dst &= src;
					gpr(8 + dn) = dst;
					g3_ccr = 0;
					if (((op68 & 0xf1c0u) == 0xc040u)
					    ? ((dst & 0xffffu) == 0)
					    : (dst == 0))
						g3_ccr |= 4;
					if ((op68 & 0xf1c0u) == 0xc040u) {
						if ((int16)dst < 0)
							g3_ccr |= 8;
					} else if ((int32)dst < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf1c0u) == 0x91c0u ||
				    (op68 & 0xf1c0u) == 0xd1c0u) {
					const int an = (int)((op68 >> 9) & 7u);
					const unsigned sm = (op68 >> 3) & 7u;
					const int sn = (int)(op68 & 7u);
					uint32 src = 0;
					int ok = 0;
					if (sm == 0u) {
						src = gpr(8 + sn);
						ok = 1;
					} else if (sm == 1u) {
						src = (sn == 7) ? gpr(1)
								: gpr(16 + sn);
						ok = 1;
					} else if (sm == 2u) {
						const uint32 a = (sn == 7) ? gpr(1)
									  : gpr(16 + sn);
						if (g3_ea_data(a))
							src = vm_read_memory_4(g3_rom0(a));
						ok = 1;
					} else if (sm == 7u && sn == 4) {
						/* ADDA.L #imm. Dest-edge was
						 * 0x7064 0000 (low word of
						 * #$00010000 after d1fc). */
						src = vm_read_memory_4(r24);
						r24 += 4;
						ok = 1;
					}
					if (ok) {
						uint32 dst = (an == 7) ? gpr(1)
								       : gpr(16 + an);
						if ((op68 & 0xf000u) == 0x9000u)
							dst -= src;
						else
							dst += src;
						if (an == 7)
							gpr(1) = dst;
						else
							gpr(16 + an) = dst;
						gpr(24) = r24;
						gpr(27) = 0xffffffffu;
						gpr(29) = ROMBase + 0x380000u;
						pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
						{
							static unsigned nsubal;
							if (nsubal < 8) {
								nsubal++;
								nw_boot_log(
									(sm == 7u && sn == 4)
									? "G3: 68k ADDA.L #imm"
									: "G3: 68k SUBA/ADDA.L");
							}
						}
#endif
						continue;
					}
				}
				if ((op68 & 0xf1ffu) == 0xd0fcu ||
				    (op68 & 0xf1ffu) == 0x90fcu) {
					const int an = (int)((op68 >> 9) & 7u);
					const int32 imm = (int16)vm_read_memory_2(r24);
					r24 += 2;
					uint32 dst = (an == 7) ? gpr(1) : gpr(16 + an);
					if ((op68 & 0xf000u) == 0x9000u)
						dst -= (uint32)imm;
					else
						dst += (uint32)imm;
					if (an == 7)
						gpr(1) = dst;
					else
						gpr(16 + an) = dst;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned naddaw;
						if (naddaw < 8) {
							naddaw++;
							nw_boot_log("G3: 68k ADDA.W #imm");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1c0u) == 0x90c0u ||
				    (op68 & 0xf1c0u) == 0xd0c0u) {
					const int an = (int)((op68 >> 9) & 7u);
					const unsigned sm = (op68 >> 3) & 7u;
					const int sn = (int)(op68 & 7u);
					uint32 src = 0;
					int ok = 0;
					if (sm == 0u) {
						src = gpr(8 + sn);
						ok = 1;
					} else if (sm == 1u) {
						src = (sn == 7) ? gpr(1) : gpr(16 + sn);
						ok = 1;
					} else if (sm == 2u) {
						const uint32 a = (sn == 7) ? gpr(1)
									  : gpr(16 + sn);
						src = vm_read_memory_2(g3_rom0(a));
						ok = 1;
					} else if (sm == 3u) {
						uint32 a = (sn == 7) ? gpr(1)
								     : gpr(16 + sn);
						src = vm_read_memory_2(g3_rom0(a));
						a += 2u;
						if (sn == 7)
							gpr(1) = a;
						else
							gpr(16 + sn) = a;
						ok = 1;
					} else if (sm == 7u && sn == 0) {
						const int32 a =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						src = vm_read_memory_2(
							g3_rom0((uint32)a));
						ok = 1;
					} else if (sm == 6u) {
						const uint32 basea = (sn == 7) ? gpr(1)
									      : gpr(16 + sn);
						const uint32 ext = vm_read_memory_2(r24);
						r24 += 2;
						const int da = (int)((ext >> 15) & 1u);
						const int xr = (int)((ext >> 12) & 7u);
						const int wl = (int)((ext >> 11) & 1u);
						const int32 disp = (int8)(ext & 0xffu);
						uint32 xn = da ? ((xr == 7) ? gpr(1)
								       : gpr(16 + xr))
							      : gpr(8 + xr);
						if (!wl)
							xn = (uint32)(int32)(int16)xn;
						uint32 a = basea + disp + xn;
						if (g3_ea_data(a))
							src = vm_read_memory_2(
								g3_rom0(a));
						ok = 1;
					}
					if (ok) {
						src = (uint32)(int32)(int16)src;
						uint32 dst = (an == 7) ? gpr(1)
								       : gpr(16 + an);
						if ((op68 & 0xf000u) == 0x9000u)
							dst -= src;
						else
							dst += src;
						if (an == 7)
							gpr(1) = dst;
						else
							gpr(16 + an) = dst;
						gpr(24) = r24;
						gpr(27) = 0xffffffffu;
						gpr(29) = ROMBase + 0x380000u;
						pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
						{
							static unsigned nsubaw;
							if (nsubaw < 8) {
								nsubaw++;
								nw_boot_log("G3: 68k SUBA/ADDA.W");
							}
						}
#endif
						continue;
					}
				}
				if ((op68 & 0xf1ffu) == 0x90b8u ||
				    (op68 & 0xf1ffu) == 0xd0b8u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const int32 a =
						(int16)vm_read_memory_2(r24);
					r24 += 2;
					const uint32 src = vm_read_memory_4(
						g3_rom0((uint32)a));
					uint32 dst = gpr(8 + dn);
					if ((op68 & 0xf000u) == 0x9000u)
						dst -= src;
					else
						dst += src;
					gpr(8 + dn) = dst;
					g3_ccr = 0;
					if (dst == 0)
						g3_ccr |= 4;
					if ((int32)dst < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nsublw;
						if (nsublw < 8) {
							nsublw++;
							nw_boot_log("G3: 68k SUB.L abs.W");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1ffu) == 0xb0b8u ||
				    (op68 & 0xf1ffu) == 0xb1b8u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const int32 a =
						(int16)vm_read_memory_2(r24);
					r24 += 2;
					const int word =
						((op68 & 0xf1ffu) == 0xb0b8u);
					g3_ccr = 0;
					if (word) {
						const uint32 src = vm_read_memory_2(
							g3_rom0((uint32)a));
						const uint32 dst = gpr(8 + dn) & 0xffffu;
						const uint32 r = (uint16)(dst - src);
						if (r == 0)
							g3_ccr |= 4;
						if ((int16)r < 0)
							g3_ccr |= 8;
						if (dst < src)
							g3_ccr |= 1;
					} else {
						const uint32 src = vm_read_memory_4(
							g3_rom0((uint32)a));
						const uint32 dst = gpr(8 + dn);
						const uint32 r = dst - src;
						if (r == 0)
							g3_ccr |= 4;
						if ((int32)r < 0)
							g3_ccr |= 8;
						if (dst < src)
							g3_ccr |= 1;
					}
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ncmpw;
						if (ncmpw < 8) {
							ncmpw++;
							nw_boot_log("G3: 68k CMP abs.W");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1c0u) == 0x91c0u &&
				    ((op68 >> 3) & 7u) <= 1u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const int sn = (int)(op68 & 7u);
					const unsigned sm = (op68 >> 3) & 7u;
					const uint32 src = (sm == 0u)
						? gpr(8 + sn)
						: ((sn == 7) ? gpr(1)
							     : gpr(16 + sn));
					uint32 dst = (dn == 7) ? gpr(1)
							       : gpr(16 + dn);
					dst -= src;
					if (dn == 7)
						gpr(1) = dst;
					else
						gpr(16 + dn) = dst;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf1c0u) == 0xd1c0u &&
				    (((op68 >> 3) & 7u) <= 5u ||
				     (((op68 >> 3) & 7u) == 7u &&
				      (op68 & 7u) == 4u))) {
					const int dn = (int)((op68 >> 9) & 7u);
					const int sn = (int)(op68 & 7u);
					const unsigned sm = (op68 >> 3) & 7u;
					uint32 src;
					if (sm == 0u)
						src = gpr(8 + sn);
					else if (sm == 2u) {
						const uint32 a = (sn == 7) ? gpr(1)
									  : gpr(16 + sn);
						src = vm_read_memory_4(g3_rom0(a));
					} else if (sm == 5u) {
						const int32 d =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						const uint32 a = ((sn == 7) ? gpr(1)
									    : gpr(16 + sn)) + d;
						src = vm_read_memory_4(g3_rom0(a));
					} else if (sm == 7u && sn == 4) {
						src = vm_read_memory_4(r24);
						r24 += 4;
					} else
						src = (sn == 7) ? gpr(1) : gpr(16 + sn);
					uint32 dst = (dn == 7) ? gpr(1)
							       : gpr(16 + dn);
					dst += src;
					if (dn == 7)
						gpr(1) = dst;
					else
						gpr(16 + dn) = dst;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xff00u) == 0x4a00u) {
					const unsigned sz = (op68 >> 6) & 3u;
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					if (sz < 3u) {
						uint32 addr = 0;
						uint32 val = 0;
						int got = 0;
						if (sm == 0u) {
							val = gpr(8 + (int)sr);
							got = 1;
						} else if (sm == 1u) {
							val = (sr == 7u) ? gpr(1)
									 : gpr(16 + (int)sr);
							got = 1;
						} else if (sm == 2u) {
							addr = (sr == 7u) ? gpr(1)
									  : gpr(16 + (int)sr);
							got = 2;
						} else if (sm == 3u) {
							addr = (sr == 7u) ? gpr(1)
									  : gpr(16 + (int)sr);
							{
								uint32 inc = (sz == 2u) ? 4u
									: (sz == 1u ? 2u : 1u);
								if (sr == 7u && sz == 0u)
									inc = 2u;
								if (sr == 7u)
									gpr(1) = addr + inc;
								else
									gpr(16 + (int)sr) =
										addr + inc;
							}
							got = 2;
						} else if (sm == 7u && sr == 0u) {
							addr = (uint32)(int32)(int16)
								vm_read_memory_2(r24);
							r24 += 2;
							got = 2;
						} else if (sm == 5u) {
							const int32 d =
								(int16)vm_read_memory_2(r24);
							r24 += 2;
							addr = ((sr == 7u) ? gpr(1)
									   : gpr(16 + (int)sr)) + d;
							got = 2;
						} else if (sm == 6u) {
							const uint32 ext =
								vm_read_memory_2(r24);
							r24 += 2;
							const uint32 basea = (sr == 7u)
								? gpr(1)
								: gpr(16 + (int)sr);
							const int da = (int)((ext >> 15) & 1u);
							const int xr = (int)((ext >> 12) & 7u);
							const int wl = (int)((ext >> 11) & 1u);
							const int sc = (int)((ext >> 9) & 3u);
							const int32 disp = (int8)(ext & 0xffu);
							uint32 xn = da ? ((xr == 7) ? gpr(1)
									       : gpr(16 + xr))
								      : gpr(8 + xr);
							if (!wl)
								xn = (uint32)(int32)(int16)xn;
							xn <<= sc;
							addr = basea + disp + xn;
							got = 2;
						}
						if (got) {
							if (got == 2) {
								addr = g3_rom0(addr);
								if (sz == 2u)
									val = vm_read_memory_4(addr);
								else if (sz == 1u)
									val = vm_read_memory_2(addr);
								else
									val = vm_read_memory_1(addr);
							}
							if (sz == 0u)
								val &= 0xffu;
							else if (sz == 1u)
								val &= 0xffffu;
							g3_ccr = 0;
							if (val == 0)
								g3_ccr |= 4;
							if (sz == 2u) {
								if ((int32)val < 0)
									g3_ccr |= 8;
							} else if (sz == 1u) {
								if ((int16)val < 0)
									g3_ccr |= 8;
							} else if ((int8)val < 0)
								g3_ccr |= 8;
							gpr(24) = r24;
							gpr(27) = 0xffffffffu;
							gpr(29) = ROMBase + 0x380000u;
							pc() = ROMBase + 0x366084u;
							continue;
						}
					}
				}
				if ((op68 & 0xf1ffu) == 0x41f8u) {
					const int an = (int)((op68 >> 9) & 7u);
					{
						const uint32 t = (uint32)(int32)(int16)
							vm_read_memory_2(r24);
						if (an == 7)
							gpr(1) = t;
						else
							gpr(16 + an) = t;
					}
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf1ffu) == 0x41f9u) {
					const int an = (int)((op68 >> 9) & 7u);
					{
						const uint32 t = vm_read_memory_4(r24);
						if (an == 7)
							gpr(1) = t;
						else
							gpr(16 + an) = t;
					}
					gpr(24) = r24 + 4;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					static unsigned nleal;
					if (nleal < 16) {
						nleal++;
						char buf[80];
						snprintf(buf, sizeof(buf),
							 "G3: 68k LEA.L A%d=%08x",
							 an, (unsigned)gpr(16 + an));
						nw_boot_log(buf);
					}
#endif
					continue;
				}
				if ((op68 & 0xf1c0u) == 0x2040u) {
					const int an = (int)((op68 >> 9) & 7u);
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					uint32 v = 0;
					int got = 0;
					if (sm <= 1u) {
						v = (sm == 0u) ? gpr(8 + (int)sr)
						    : ((sr == 7u) ? gpr(1)
								  : gpr(16 + (int)sr));
						got = 1;
					} else if (sm == 2u || sm == 3u ||
						   sm == 4u) {
						uint32 a = (sr == 7u) ? gpr(1)
								     : gpr(16 + (int)sr);
						if (sm == 4u)
							a -= 4u;
						if (g3_ea_data(g3_rom0(a)))
							v = vm_read_memory_4(g3_rom0(a));
						if (sm == 3u)
							a += 4u;
						if (sm == 3u || sm == 4u) {
							if (sr == 7u)
								gpr(1) = a;
							else
								gpr(16 + (int)sr) = a;
						}
						got = 1;
					} else if (sm == 5u) {
						const int32 d =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						uint32 a = ((sr == 7u) ? gpr(1)
								       : gpr(16 + (int)sr)) + d;
						if (g3_ea_data(g3_rom0(a)))
							v = vm_read_memory_4(g3_rom0(a));
						got = 1;
					} else if (sm == 7u && sr == 0u) {
						uint32 a = (uint32)(int32)(int16)
							vm_read_memory_2(r24);
						r24 += 2;
						if (g3_ea_data(g3_rom0(a)))
							v = vm_read_memory_4(g3_rom0(a));
						got = 1;
					} else if (sm == 6u) {
						const uint32 ext =
							vm_read_memory_2(r24);
						r24 += 2;
						const uint32 basea = (sr == 7u)
							? gpr(1)
							: gpr(16 + (int)sr);
						const int da = (int)((ext >> 15) & 1u);
						const int xr = (int)((ext >> 12) & 7u);
						const int wl = (int)((ext >> 11) & 1u);
						const int sc = (int)((ext >> 9) & 3u);
						uint32 xn = da ? ((xr == 7) ? gpr(1)
								       : gpr(16 + xr))
							      : gpr(8 + xr);
						if (!wl)
							xn = (uint32)(int32)(int16)xn;
						xn <<= sc;
						uint32 addr = basea;
						if ((ext & 0x100u) == 0) {
							addr = basea +
							       (int32)(int8)(ext & 0xffu) +
							       xn;
						} else {
							const int bs = (int)((ext >> 7) & 1u);
							const int isup = (int)((ext >> 6) & 1u);
							const int bdsz = (int)((ext >> 4) & 3u);
							const int iis = (int)(ext & 7u);
							uint32 bd = 0;
							if (bdsz == 2) {
								bd = (uint32)(int32)(int16)
									vm_read_memory_2(r24);
								r24 += 2;
							} else if (bdsz == 3) {
								bd = vm_read_memory_4(r24);
								r24 += 4;
							}
							uint32 od = 0;
							if (iis == 2 || iis == 3 ||
							    iis == 6 || iis == 7) {
								od = (uint32)(int32)(int16)
									vm_read_memory_2(r24);
								r24 += 2;
							} else if (iis == 4) {
								od = vm_read_memory_4(r24);
								r24 += 4;
							}
							uint32 inner = (bs ? 0 : basea) + bd;
							if (!isup && iis < 6)
								inner += xn;
							if (iis >= 2 && g3_ea_data(inner))
								inner = vm_read_memory_4(
									g3_rom0(inner));
							if (!isup && iis >= 6)
								inner += xn;
							addr = inner + od;
#if NW_BOOT_LOG
							{
								static unsigned nmaif;
								if (nmaif < 8) {
									nmaif++;
									nw_boot_log("G3: 68k MOVEA.L full");
								}
							}
#endif
						}
						if (g3_ea_data(g3_rom0(addr)))
							v = vm_read_memory_4(
								g3_rom0(addr));
						got = 1;
#if NW_BOOT_LOG
						{
							static unsigned nmai;
							if (nmai < 8) {
								nmai++;
								nw_boot_log("G3: 68k MOVEA.L idx");
							}
						}
#endif
					}
					if (got) {
						if (an == 7)
							gpr(1) = v;
						else
							gpr(16 + an) = v;
						gpr(24) = r24;
						gpr(27) = 0xffffffffu;
						gpr(29) = ROMBase + 0x380000u;
						pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
						{
							static unsigned nmae;
							if (nmae < 8) {
								nmae++;
								char buf[80];
								snprintf(buf, sizeof(buf),
									 "G3: 68k MOVEA.L ea A%d=%08x",
									 an, (unsigned)v);
								nw_boot_log(buf);
							}
						}
#endif
						continue;
					}
				}
				if ((op68 & 0xf1ffu) == 0x2079u) {
					const int an = (int)((op68 >> 9) & 7u);
					const uint32 a = vm_read_memory_4(r24);
					uint32 v = 0;
					if (g3_ea_data(g3_rom0(a)))
						v = vm_read_memory_4(g3_rom0(a));
					if (an == 7)
						gpr(1) = v;
					else
						gpr(16 + an) = v;
					gpr(24) = r24 + 4;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmal;
						if (nmal < 8) {
							nmal++;
							char buf[80];
							snprintf(buf, sizeof(buf),
								 "G3: 68k MOVEA.L abs A%d=%08x",
								 an, (unsigned)v);
							nw_boot_log(buf);
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1ffu) == 0x41fau) {
					const int an = (int)((op68 >> 9) & 7u);
					const int32 d = (int16)vm_read_memory_2(r24);
					{
						const uint32 t = r24 + d;
						if (an == 7)
							gpr(1) = t;
						else
							gpr(16 + an) = t;
					}
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					static unsigned nlea;
					if (nlea < 8) {
						nlea++;
						char buf[80];
						snprintf(buf, sizeof(buf),
							 "G3: 68k LEA A%d=%08x pc=%08x",
							 an, (unsigned)gpr(16 + an),
							 (unsigned)gpr(24));
						nw_boot_log(buf);
					}
#endif
					continue;
				}
				if ((op68 & 0xf1ffu) == 0x41fbu) {
					const int an = (int)((op68 >> 9) & 7u);
					const uint32 ext_pc = r24;
					const uint32 ext = vm_read_memory_2(r24);
					r24 += 2;
					const int da = (int)((ext >> 15) & 1u);
					const int xr = (int)((ext >> 12) & 7u);
					const int wl = (int)((ext >> 11) & 1u);
					const int32 disp = (int8)(ext & 0xffu);
					uint32 xn = da ? ((xr == 7) ? gpr(1)
							       : gpr(16 + xr))
						      : gpr(8 + xr);
					if (!wl)
						xn = (uint32)(int32)(int16)xn;
					xn <<= (int)((ext >> 9) & 3u);
					uint32 t;
					if ((ext & 0x100u) == 0) {
						/* PC-relative: use ROM-at-0 PC so
						 * LEA -8(PC,A2) with A2=$FFFF568E
						 * yields 0, not ROMBase. */
						t = g3_pc0(ext_pc) + disp + xn;
					} else {
						const int bs = (int)((ext >> 7) & 1u);
						const int isup = (int)((ext >> 6) & 1u);
						const int bdsz = (int)((ext >> 4) & 3u);
						uint32 bd = 0;
						if (bdsz == 2) {
							bd = (uint32)(int32)(int16)
								vm_read_memory_2(r24);
							r24 += 2;
						} else if (bdsz == 3) {
							bd = vm_read_memory_4(r24);
							r24 += 4;
						}
						t = (bs ? 0 : g3_pc0(ext_pc)) + bd;
						if (!isup)
							t += xn;
					}
					if (an == 7)
						gpr(1) = t;
					else
						gpr(16 + an) = t;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					static unsigned nleai;
					if (nleai < 16) {
						nleai++;
						char buf[80];
						snprintf(buf, sizeof(buf),
							 "G3: 68k LEA idx A%d=%08x",
							 an, (unsigned)gpr(16 + an));
						nw_boot_log(buf);
					}
#endif
					continue;
				}
				if ((op68 & 0xf1c0u) == 0x41c0u &&
				    ((op68 >> 3) & 7u) == 5u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const int an = (int)(op68 & 7u);
					const uint32 basea = (an == 7) ? gpr(1)
								      : gpr(16 + an);
					const int32 d =
						(int16)vm_read_memory_2(r24);
					{
						uint32 t = basea + d;
						if (dn == 7)
							gpr(1) = t;
						else
							gpr(16 + dn) = t;
					}
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf1c0u) == 0x41c0u &&
				    ((op68 >> 3) & 7u) == 2u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const int an = (int)(op68 & 7u);
					const uint32 t = (an == 7) ? gpr(1)
							      : gpr(16 + an);
					if (dn == 7)
						gpr(1) = t;
					else
						gpr(16 + dn) = t;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nleaan;
						if (nleaan < 8) {
							nleaan++;
							nw_boot_log("G3: 68k LEA (An)");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xfff8u) == 0x4850u) {
					const int an = (int)(op68 & 7u);
					const uint32 a = (an == 7) ? gpr(1)
							      : gpr(16 + an);
					gpr(1) -= 4;
					if (g3_ea_data(gpr(1)))
						vm_write_memory_4(gpr(1), a);
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned npeaan;
						if (npeaan < 8) {
							npeaan++;
							nw_boot_log("G3: 68k PEA (An)");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xf1c0u) == 0x41c0u &&
				    ((op68 >> 3) & 7u) == 6u) {
					const int dn = (int)((op68 >> 9) & 7u);
					const int an = (int)(op68 & 7u);
					const uint32 basea = (an == 7) ? gpr(1)
								      : gpr(16 + an);
					const uint32 ext = vm_read_memory_2(r24);
					const int da = (int)((ext >> 15) & 1u);
					const int xr = (int)((ext >> 12) & 7u);
					const int wl = (int)((ext >> 11) & 1u);
					const int32 disp = (int8)(ext & 0xffu);
					uint32 xn = da ? ((xr == 7) ? gpr(1)
							       : gpr(16 + xr))
						      : gpr(8 + xr);
					if (!wl)
						xn = (uint32)(int32)(int16)xn;
					{
						uint32 t = basea + disp + xn;
						if (dn == 7)
							gpr(1) = t;
						else
							gpr(16 + dn) = t;
					}
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xffffu) == 0x4efbu ||
				    (op68 & 0xffffu) == 0x4ebbu) {
					const uint32 ext = vm_read_memory_2(r24);
					const int da = (int)((ext >> 15) & 1u);
					const int xr = (int)((ext >> 12) & 7u);
					const int wl = (int)((ext >> 11) & 1u);
					const int32 disp = (int8)(ext & 0xffu);
					uint32 xn = da ? ((xr == 7) ? gpr(1)
							       : gpr(16 + xr))
						      : gpr(8 + xr);
					if (!wl)
						xn = (uint32)(int32)(int16)xn;
					xn <<= (int)((ext >> 9) & 3u);
					const uint32 ret = r24 + 2;
					{
						uint32 t = g3_rom0(g3_pc0(r24) + disp + xn);
						if (!g3_r24_ok(t) || vm_read_memory_2(t) == 0)
							t = ret;
						else if (op68 == 0x4ebbu) {
							gpr(1) -= 4;
							vm_write_memory_4(gpr(1), ret);
						}
						gpr(24) = t;
					}
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					if (op68 == 0x4ebbu) {
						static unsigned njsri;
						if (njsri < 8) {
							njsri++;
							char buf[80];
							snprintf(buf, sizeof(buf),
								 "G3: 68k JSR idx pc=%08x",
								 (unsigned)gpr(24));
							nw_boot_log(buf);
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xfff8u) == 0x4ef0u ||
				    (op68 & 0xfff8u) == 0x4eb0u) {
					const int jsr = (op68 & 0xfff8u) == 0x4eb0u;
					const int an = (int)(op68 & 7u);
					const uint32 ext = vm_read_memory_2(r24);
					r24 += 2;
					const uint32 basea = (an == 7) ? gpr(1)
								    : gpr(16 + an);
					const int da = (int)((ext >> 15) & 1u);
					const int xr = (int)((ext >> 12) & 7u);
					const int wl = (int)((ext >> 11) & 1u);
					const int sc = (int)((ext >> 9) & 3u);
					uint32 xn = da ? ((xr == 7) ? gpr(1)
							       : gpr(16 + xr))
						      : gpr(8 + xr);
					if (!wl)
						xn = (uint32)(int32)(int16)xn;
					xn <<= sc;
					uint32 addr = basea;
					if ((ext & 0x100u) == 0) {
						addr = basea +
						       (int32)(int8)(ext & 0xffu) + xn;
					} else {
						const int bs = (int)((ext >> 7) & 1u);
						const int isup = (int)((ext >> 6) & 1u);
						const int bdsz = (int)((ext >> 4) & 3u);
						const int iis = (int)(ext & 7u);
						uint32 bd = 0;
						if (bdsz == 2) {
							bd = (uint32)(int32)(int16)
								vm_read_memory_2(r24);
							r24 += 2;
						} else if (bdsz == 3) {
							bd = vm_read_memory_4(r24);
							r24 += 4;
						}
						uint32 od = 0;
						if (iis == 2 || iis == 3 ||
						    iis == 6 || iis == 7) {
							od = (uint32)(int32)(int16)
								vm_read_memory_2(r24);
							r24 += 2;
						} else if (iis == 4) {
							od = vm_read_memory_4(r24);
							r24 += 4;
						}
						uint32 inner = (bs ? 0 : basea) + bd;
						if (!isup && iis < 6)
							inner += xn;
						if (iis >= 2 && g3_ea_data(inner))
							inner = vm_read_memory_4(
								g3_rom0(inner));
						if (!isup && iis >= 6)
							inner += xn;
						addr = inner + od;
					}
					{
						uint32 t = g3_rom0(addr);
						const uint32 ret = r24;
						const uint32 self = ret - 2u;
						if (!g3_r24_ok(t) ||
						    vm_read_memory_2(t) == 0 ||
						    t == self)
							t = ret;
						else if (jsr) {
							gpr(1) -= 4;
							vm_write_memory_4(gpr(1), ret);
						}
						gpr(24) = t;
					}
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned njmpi;
						if (njmpi < 8) {
							njmpi++;
							char buf[80];
							snprintf(buf, sizeof(buf),
								 "G3: 68k JMP idx pc=%08x",
								 (unsigned)gpr(24));
							nw_boot_log(buf);
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xff00u) == 0x6000u ||
				    (op68 & 0xf000u) == 0x6000u) {
					const uint32 base = r24;
					int32 d;
					if ((op68 & 0xffu) == 0xffu) {
						d = (int32)vm_read_memory_4(r24);
						r24 += 4;
					} else if ((op68 & 0xffu) == 0) {
						d = (int16)vm_read_memory_2(r24);
						r24 += 2;
					} else {
						d = (int8)(op68 & 0xffu);
					}
					const uint32 hi = op68 & 0xff00u;
					const int nset = (g3_ccr & 8) != 0;
					const int vset = (g3_ccr & 2) != 0;
					int take = 0;
					if (hi == 0x6000u)
						take = 1;
					else if (hi == 0x6100u) {
						gpr(1) -= 4;
						vm_write_memory_4(gpr(1), r24);
						take = 1;
					} else if (hi == 0x6700u)
						take = (g3_ccr & 4) != 0;
					else if (hi == 0x6600u)
						take = (g3_ccr & 4) == 0;
					else if (hi == 0x6a00u)
						take = (g3_ccr & 8) == 0;
					else if (hi == 0x6b00u)
						take = (g3_ccr & 8) != 0;
					else if (hi == 0x6400u)
						take = (g3_ccr & 1) == 0;
					else if (hi == 0x6500u)
						take = (g3_ccr & 1) != 0;
					else if (hi == 0x6800u)
						take = (g3_ccr & 2) == 0;
					else if (hi == 0x6900u)
						take = (g3_ccr & 2) != 0;
					else if (hi == 0x6c00u)
						take = nset == vset;
					else if (hi == 0x6d00u)
						take = nset != vset;
					else if (hi == 0x6e00u)
						take = ((g3_ccr & 4) == 0) && (nset == vset);
					else if (hi == 0x6f00u)
						take = ((g3_ccr & 4) != 0) || (nset != vset);
					{
						const uint32 bcc_opc = base - 2u;
						/* CopyBits scan: BLE dest 0x20cc4 was
						 * g3_r24_ok-rejected so dest-fix landed
						 * on 0x20cb6 BSR. Force exit. Keep
						 * inner 0x20ca4-0x20cc4. */
						if (bcc_opc == ROMBase + 0x20cb4u ||
						    bcc_opc == ROMBase + 0x1fafau) {
							take = 1;
#if NW_BOOT_LOG
							{
								static unsigned nblex;
								if (nblex < 8) {
									nblex++;
									nw_boot_log("G3: 68k CopyBits BLE exit 0x20cc4");
								}
							}
#endif
						} else if (bcc_opc == ROMBase + 0x20cc2u ||
							   bcc_opc == ROMBase + 0x1fb08u)
							take = 0;
						const uint32 dest = base + d;
						const int dest_ok = g3_r24_ok(dest);
						if (take && d < 0 && d >= -512 &&
						    (hi == 0x6600u ||
						     hi == 0x6700u ||
						     hi == 0x6000u ||
						     hi == 0x6200u ||
						     hi == 0x6300u ||
						     hi == 0x6400u ||
						     hi == 0x6500u ||
						     hi == 0x6a00u ||
						     hi == 0x6b00u ||
						     hi == 0x6800u ||
						     hi == 0x6900u ||
						     hi == 0x6c00u ||
						     hi == 0x6d00u ||
						     hi == 0x6e00u ||
						     hi == 0x6f00u)) {
							static uint32 wait_dest[4];
							static unsigned wait_n[4];
							static unsigned wait_wr;
							unsigned i, slot = 4;
							for (i = 0; i < 4u; i++) {
								if (wait_dest[i] == dest) {
									slot = i;
									break;
								}
							}
							if (slot == 4u) {
								slot = wait_wr++ & 3u;
								wait_dest[slot] = dest;
								wait_n[slot] = 0;
							}
							if (++wait_n[slot] > 16u)
								take = 0;
						}
						if (take && dest != base - 2u) {
							uint32 d2 = dest;
							if (!dest_ok)
								d2 = g3_fix_r24(dest);
							if (g3_r24_ok(d2) &&
							    d2 != base - 2u) {
								gpr(24) = d2;
#if NW_BOOT_LOG
								if (!dest_ok) {
									static unsigned nbrfx;
									if (nbrfx < 8) {
										nbrfx++;
										nw_boot_log("G3: 68k BRA dest-fix");
									}
								}
#endif
							} else
								gpr(24) = r24;
						} else {
							gpr(24) = r24;
#if NW_BOOT_LOG
							if (take && dest == base - 2u) {
								static unsigned nhalt;
								if (nhalt < 4) {
									nhalt++;
									nw_boot_log("G3: 68k skip BRA *");
								}
							}
#endif
						}
					}
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					static unsigned nbra;
					if (nbra < 24) {
						nbra++;
						char buf[96];
						snprintf(buf, sizeof(buf),
							 "G3: 68k Bxx %04x pc=%08x d=%08x",
							 (unsigned)op68, (unsigned)gpr(24),
							 (unsigned)d);
						nw_boot_log(buf);
					}
#endif
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 2u &&
				    ((op68 >> 6) & 7u) == 6u &&
				    ((op68 >> 3) & 7u) == 1u) {
					const unsigned dr = (op68 >> 9) & 7u;
					const unsigned sr = op68 & 7u;
					uint32 v = (sr == 7u) ? gpr(1)
							 : gpr(16 + (int)sr);
					const uint32 dsta = (dr == 7u) ? gpr(1)
								      : gpr(16 + (int)dr);
					uint32 ext = vm_read_memory_2(r24);
					r24 += 2;
					const int da = (int)((ext >> 15) & 1u);
					const int xr = (int)((ext >> 12) & 7u);
					const int wl = (int)((ext >> 11) & 1u);
					const int sc = (int)((ext >> 9) & 3u);
					uint32 xn = da ? ((xr == 7) ? gpr(1)
							       : gpr(16 + xr))
						      : gpr(8 + xr);
					if (!wl)
						xn = (uint32)(int32)(int16)xn;
					xn <<= sc;
					uint32 addr = dsta;
					if ((ext & 0x100u) == 0)
						addr = dsta +
						       (int32)(int8)(ext & 0xffu) +
						       xn;
					else {
						const int bs = (int)((ext >> 7) & 1u);
						const int isup = (int)((ext >> 6) & 1u);
						const int bdsz = (int)((ext >> 4) & 3u);
						const int iis = (int)(ext & 7u);
						uint32 bd = 0;
						if (bdsz == 2) {
							bd = (uint32)(int32)(int16)
								vm_read_memory_2(r24);
							r24 += 2;
						} else if (bdsz == 3) {
							bd = vm_read_memory_4(r24);
							r24 += 4;
						}
						uint32 od = 0;
						if (iis == 2 || iis == 3 ||
						    iis == 6 || iis == 7) {
							od = (uint32)(int32)(int16)
								vm_read_memory_2(r24);
							r24 += 2;
						} else if (iis == 4) {
							od = vm_read_memory_4(r24);
							r24 += 4;
						}
						uint32 inner = (bs ? 0 : dsta) + bd;
						if (!isup && iis < 6)
							inner += xn;
						if (iis >= 2 && g3_ea_data(inner))
							inner = vm_read_memory_4(
								g3_rom0(inner));
						if (!isup && iis >= 6)
							inner += xn;
						addr = inner + od;
					}
					if (g3_ea_data(addr))
						vm_write_memory_4(g3_rom0(addr), v);
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmlai;
						if (nmlai < 8) {
							nmlai++;
							nw_boot_log("G3: 68k MOVE.L An,idx");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 2u &&
				    ((op68 >> 6) & 7u) == 6u &&
				    ((op68 >> 3) & 7u) == 6u) {
					const unsigned sr = op68 & 7u;
					const unsigned dr = (op68 >> 9) & 7u;
					const uint32 sext = vm_read_memory_2(r24);
					const uint32 dext = vm_read_memory_2(r24 + 2);
					if ((sext & 0x100u) == 0 &&
					    (dext & 0x100u) == 0) {
						r24 += 4;
						uint32 sbase = (sr == 7u) ? gpr(1)
								 : gpr(16 + (int)sr);
						uint32 dbase = (dr == 7u) ? gpr(1)
								 : gpr(16 + (int)dr);
						int sda = (int)((sext >> 15) & 1u);
						int sxr = (int)((sext >> 12) & 7u);
						int swl = (int)((sext >> 11) & 1u);
						int ssc = (int)((sext >> 9) & 3u);
						uint32 sxn = sda ? ((sxr == 7) ? gpr(1)
									: gpr(16 + sxr))
								 : gpr(8 + sxr);
						if (!swl)
							sxn = (uint32)(int32)(int16)sxn;
						sxn <<= ssc;
						uint32 sa = sbase +
							(int32)(int8)(sext & 0xffu) +
							sxn;
						int dda = (int)((dext >> 15) & 1u);
						int dxr = (int)((dext >> 12) & 7u);
						int dwl = (int)((dext >> 11) & 1u);
						int dsc = (int)((dext >> 9) & 3u);
						uint32 dxn = dda ? ((dxr == 7) ? gpr(1)
									: gpr(16 + dxr))
								 : gpr(8 + dxr);
						if (!dwl)
							dxn = (uint32)(int32)(int16)dxn;
						dxn <<= dsc;
						uint32 da = dbase +
							(int32)(int8)(dext & 0xffu) +
							dxn;
						uint32 v = 0;
						if (g3_ea_data(sa))
							v = vm_read_memory_4(
								g3_rom0(sa));
						if (g3_ea_data(da))
							vm_write_memory_4(
								g3_rom0(da), v);
						g3_ccr = 0;
						if (v == 0)
							g3_ccr |= 4;
						if ((int32)v < 0)
							g3_ccr |= 8;
						gpr(24) = r24;
						gpr(27) = 0xffffffffu;
						gpr(29) = ROMBase + 0x380000u;
						pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
						{
							static unsigned nmlii;
							if (nmlii < 8) {
								nmlii++;
								nw_boot_log("G3: 68k MOVE.L idx,idx");
							}
						}
#endif
						continue;
					}
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 2u &&
				    ((op68 >> 6) & 7u) == 5u &&
				    ((op68 >> 3) & 7u) == 6u) {
					const unsigned sr = op68 & 7u;
					const unsigned dr = (op68 >> 9) & 7u;
					const uint32 sext = vm_read_memory_2(r24);
					if ((sext & 0x100u) == 0) {
						const int32 dd =
							(int16)vm_read_memory_2(r24 + 2);
						r24 += 4;
						uint32 sbase = (sr == 7u) ? gpr(1)
								 : gpr(16 + (int)sr);
						int sda = (int)((sext >> 15) & 1u);
						int sxr = (int)((sext >> 12) & 7u);
						int swl = (int)((sext >> 11) & 1u);
						int ssc = (int)((sext >> 9) & 3u);
						uint32 sxn = sda ? ((sxr == 7) ? gpr(1)
									: gpr(16 + sxr))
								 : gpr(8 + sxr);
						if (!swl)
							sxn = (uint32)(int32)(int16)sxn;
						sxn <<= ssc;
						uint32 sa = sbase +
							(int32)(int8)(sext & 0xffu) +
							sxn;
						uint32 da = ((dr == 7u) ? gpr(1)
							     : gpr(16 + (int)dr)) + dd;
						uint32 v = 0;
						if (g3_ea_data(sa))
							v = vm_read_memory_4(
								g3_rom0(sa));
						if (g3_ea_data(da))
							vm_write_memory_4(
								g3_rom0(da), v);
						g3_ccr = 0;
						if (v == 0)
							g3_ccr |= 4;
						if ((int32)v < 0)
							g3_ccr |= 8;
						gpr(24) = r24;
						gpr(27) = 0xffffffffu;
						gpr(29) = ROMBase + 0x380000u;
						pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
						{
							static unsigned nmlid;
							if (nmlid < 8) {
								nmlid++;
								nw_boot_log("G3: 68k MOVE.L idx,d16");
							}
						}
#endif
						continue;
					}
				}
				if ((op68 & 0xfff8u) == 0x4a70u) {
					const int an = (int)(op68 & 7u);
					const uint32 basea = (an == 7) ? gpr(1)
								      : gpr(16 + an);
					uint32 ext = vm_read_memory_2(r24);
					r24 += 2;
					const int da = (int)((ext >> 15) & 1u);
					const int xr = (int)((ext >> 12) & 7u);
					const int wl = (int)((ext >> 11) & 1u);
					const int sc = (int)((ext >> 9) & 3u);
					uint32 xn = da ? ((xr == 7) ? gpr(1)
							       : gpr(16 + xr))
						      : gpr(8 + xr);
					if (!wl)
						xn = (uint32)(int32)(int16)xn;
					xn <<= sc;
					uint32 addr = basea;
					if ((ext & 0x100u) == 0)
						addr = basea +
						       (int32)(int8)(ext & 0xffu) +
						       xn;
					else {
						const int bs = (int)((ext >> 7) & 1u);
						const int isup = (int)((ext >> 6) & 1u);
						const int bdsz = (int)((ext >> 4) & 3u);
						const int iis = (int)(ext & 7u);
						uint32 bd = 0;
						if (bdsz == 2) {
							bd = (uint32)(int32)(int16)
								vm_read_memory_2(r24);
							r24 += 2;
						} else if (bdsz == 3) {
							bd = vm_read_memory_4(r24);
							r24 += 4;
						}
						uint32 od = 0;
						if (iis == 2 || iis == 3 ||
						    iis == 6 || iis == 7) {
							od = (uint32)(int32)(int16)
								vm_read_memory_2(r24);
							r24 += 2;
						} else if (iis == 4) {
							od = vm_read_memory_4(r24);
							r24 += 4;
						}
						uint32 inner = (bs ? 0 : basea) + bd;
						if (!isup && iis < 6)
							inner += xn;
						if (iis >= 2 && g3_ea_data(inner))
							inner = vm_read_memory_4(
								g3_rom0(inner));
						if (!isup && iis >= 6)
							inner += xn;
						addr = inner + od;
					}
					uint32 v = 0;
					if (g3_ea_data(addr))
						v = vm_read_memory_2(g3_rom0(addr));
					g3_ccr = 0;
					if ((v & 0xffffu) == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ntstiw;
						if (ntstiw < 8) {
							ntstiw++;
							nw_boot_log("G3: 68k TST.W idx");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xfff8u) == 0x2f30u) {
					const int an = (int)(op68 & 7u);
					const uint32 ext = vm_read_memory_2(r24);
					r24 += 2;
					const uint32 basea = (an == 7) ? gpr(1)
								      : gpr(16 + an);
					const int da = (int)((ext >> 15) & 1u);
					const int xr = (int)((ext >> 12) & 7u);
					const int wl = (int)((ext >> 11) & 1u);
					const int sc = (int)((ext >> 9) & 3u);
					uint32 xn = da ? ((xr == 7) ? gpr(1)
							       : gpr(16 + xr))
						      : gpr(8 + xr);
					if (!wl)
						xn = (uint32)(int32)(int16)xn;
					xn <<= sc;
					uint32 addr = basea;
					if ((ext & 0x100u) == 0) {
						addr = basea +
						       (int32)(int8)(ext & 0xffu) + xn;
					} else {
						const int bs = (int)((ext >> 7) & 1u);
						const int isup = (int)((ext >> 6) & 1u);
						const int bdsz = (int)((ext >> 4) & 3u);
						const int iis = (int)(ext & 7u);
						uint32 bd = 0;
						if (bdsz == 2) {
							bd = (uint32)(int32)(int16)
								vm_read_memory_2(r24);
							r24 += 2;
						} else if (bdsz == 3) {
							bd = vm_read_memory_4(r24);
							r24 += 4;
						}
						uint32 od = 0;
						if (iis == 2 || iis == 3 ||
						    iis == 6 || iis == 7) {
							od = (uint32)(int32)(int16)
								vm_read_memory_2(r24);
							r24 += 2;
						} else if (iis == 4) {
							od = vm_read_memory_4(r24);
							r24 += 4;
						}
						uint32 inner = (bs ? 0 : basea) + bd;
						if (!isup && iis < 6)
							inner += xn;
						if (iis >= 2 && g3_ea_data(inner))
							inner = vm_read_memory_4(
								g3_rom0(inner));
						if (!isup && iis >= 6)
							inner += xn;
						addr = inner + od;
					}
					{
						uint32 a = g3_rom0(addr);
						uint32 v = 0;
						if (g3_ea_data(a))
							v = g3_rom0(vm_read_memory_4(a));
						/* Jump-through: only push a routine
						 * entry. Mid-body 0x134ca AND.L mills.
						 * Blit BCLR 0x20b96 stays legal (08xx). */
						if (g3_68k_entry(v)) {
							gpr(1) -= 4;
							if (g3_ea_data(gpr(1)))
								vm_write_memory_4(gpr(1), v);
						}
					}
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned n2f30;
						if (n2f30 < 16) {
							n2f30++;
							nw_boot_log("G3: 68k 2f30 jsr");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xfff8u) == 0x4ed0u) {
					uint32 t = g3_rom0(gpr(16 + (int)(op68 & 7u)));
					const uint32 self = r24 - 2u;
					if (!g3_r24_ok(t) ||
					    vm_read_memory_2(t) == 0 ||
					    t == self) {
						const uint32 sp = gpr(1);
						uint32 dest = g3_rom0(vm_read_memory_4(sp));
						gpr(1) = sp + 4;
						if (g3_r24_ok(dest) && dest != self)
							t = dest;
						else
							t = g3_fix_r24(r24);
					}
					gpr(24) = t;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					static unsigned njmpa;
					if (njmpa < 8) {
						njmpa++;
						char buf[80];
						snprintf(buf, sizeof(buf),
							 "G3: 68k JMP (A%d)=%08x",
							 (int)(op68 & 7u),
							 (unsigned)gpr(24));
						nw_boot_log(buf);
					}
#endif
					continue;
				}
				if (op68 == 0x4e56u) {
					const int32 d = (int16)vm_read_memory_2(r24);
					gpr(1) -= 4;
					vm_write_memory_4(gpr(1), gpr(22));
					gpr(22) = gpr(1);
					gpr(1) += d;
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x48e0u) {
					const uint32 mask =
						vm_read_memory_2(r24);
					r24 += 2;
					const int an = (int)(op68 & 7u);
					uint32 snap[16];
					unsigned i;
					for (i = 0; i < 8u; i++)
						snap[i] = gpr(8 + (int)i);
					for (i = 0; i < 7u; i++)
						snap[8 + i] = gpr(16 + (int)i);
					snap[15] = gpr(1);
					uint32 a = (an == 7) ? gpr(1)
							 : gpr(16 + an);
					for (i = 0; i < 16u; i++) {
						if ((mask & (1u << i)) == 0)
							continue;
						a -= 4u;
						if (g3_ea_data(a))
							vm_write_memory_4(a,
								snap[15u - i]);
					}
					if (an == 7)
						gpr(1) = a;
					else
						gpr(16 + an) = a;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmmpre;
						if (nmmpre < 8) {
							nmmpre++;
							nw_boot_log("G3: 68k MOVEM.L -(An)");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xfff8u) == 0x4cd8u) {
					const uint32 mask =
						vm_read_memory_2(r24);
					r24 += 2;
					const int an = (int)(op68 & 7u);
					uint32 a = (an == 7) ? gpr(1)
							 : gpr(16 + an);
					unsigned i;
					int listed = 0;
					for (i = 0; i < 16u; i++) {
						if ((mask & (1u << i)) == 0)
							continue;
						uint32 v = 0;
						if (g3_ea_data(a))
							v = vm_read_memory_4(
								g3_rom0(a));
						a += 4u;
						if (i < 8u)
							gpr(8 + (int)i) = v;
						else if (i == 15u)
							gpr(1) = v;
						else
							gpr(16 + (int)(i - 8u)) = v;
						if (i == (unsigned)(8 + an))
							listed = 1;
					}
					if (!listed) {
						if (an == 7)
							gpr(1) = a;
						else
							gpr(16 + an) = a;
					}
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmmpst;
						if (nmmpst < 8) {
							nmmpst++;
							nw_boot_log("G3: 68k MOVEM.L (An)+");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xfff8u) == 0x4ce8u) {
					const uint32 mask =
						vm_read_memory_2(r24);
					r24 += 2;
					const int an = (int)(op68 & 7u);
					const int32 d =
						(int16)vm_read_memory_2(r24);
					r24 += 2;
					uint32 a = ((an == 7) ? gpr(1)
							 : gpr(16 + an)) + d;
					unsigned i;
					for (i = 0; i < 16u; i++) {
						if ((mask & (1u << i)) == 0)
							continue;
						uint32 v = 0;
						if (g3_ea_data(a))
							v = vm_read_memory_4(
								g3_rom0(a));
						a += 4u;
						if (i < 8u)
							gpr(8 + (int)i) = v;
						else if (i == 15u)
							gpr(1) = v;
						else
							gpr(16 + (int)(i - 8u)) = v;
					}
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmmd16;
						if (nmmd16 < 8) {
							nmmd16++;
							nw_boot_log("G3: 68k MOVEM.L d16(An)");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xfff8u) == 0x4a68u) {
					const int an = (int)(op68 & 7u);
					const int32 d =
						(int16)vm_read_memory_2(r24);
					uint32 a = ((an == 7) ? gpr(1)
							 : gpr(16 + an)) + d;
					uint32 v = 0;
					if (g3_ea_data(a))
						v = vm_read_memory_2(g3_rom0(a));
					g3_ccr = 0;
					if ((v & 0xffffu) == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned ntstw;
						if (ntstw < 8) {
							ntstw++;
							nw_boot_log("G3: 68k TST.W d16(An)");
						}
					}
#endif
					continue;
				}
				if (op68 == 0x4e5eu) {
					const uint32 fp = gpr(22);
					if (fp != 0 && g3_ea_data(fp)) {
						gpr(1) = fp;
						gpr(22) = vm_read_memory_4(fp);
						gpr(1) += 4;
					}
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x4880u ||
				    (op68 & 0xfff8u) == 0x48c0u ||
				    (op68 & 0xfff8u) == 0x49c0u) {
					const int dn = (int)(op68 & 7u);
					uint32 v = gpr(8 + dn);
					if ((op68 & 0xfff8u) == 0x49c0u)
						v = (uint32)(int32)(int8)v;
					else if ((op68 & 0xfff8u) == 0x4880u)
						v = (uint32)(int32)(int16)v;
					gpr(8 + dn) = v;
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int32)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (((op68 & 0xff80u) == 0x4880u ||
				     (op68 & 0xff80u) == 0x4c80u) &&
				    ((op68 >> 3) & 7u) >= 2u) {
					const uint32 mask = vm_read_memory_2(r24);
					r24 += 2;
					const unsigned sm = (op68 >> 3) & 7u;
					const unsigned sr = op68 & 7u;
					const int from = (op68 & 0x0400u) != 0;
					const uint32 sz = (op68 & 0x40u) ? 4u : 2u;
					uint32 addr = (sr == 7u) ? gpr(1)
						: gpr(16 + (int)sr);
					if (sm == 5u) {
						const int32 d =
							(int16)vm_read_memory_2(r24);
						r24 += 2;
						addr += d;
					} else if (sm == 6u)
						r24 += 2;
					else if (sm == 7u && sr == 0u) {
						addr = (uint32)(int32)(int16)
							vm_read_memory_2(r24);
						r24 += 2;
					} else if (sm == 7u && sr == 1u) {
						addr = vm_read_memory_4(r24);
						r24 += 4;
					}
					if (!from && sm == 4u) {
						unsigned i;
						for (i = 0; i < 16u; i++) {
							if ((mask & (1u << i)) == 0)
								continue;
							addr -= sz;
							const unsigned reg = 15u - i;
							uint32 val;
							if (reg < 8u)
								val = gpr(8 + (int)reg);
							else if (reg == 15u)
								val = gpr(1);
							else
								val = gpr(16 + (int)(reg - 8u));
							if (g3_ea_data(addr)) {
								if (sz == 4u)
									vm_write_memory_4(
										g3_rom0(addr), val);
								else
									vm_write_memory_2(
										g3_rom0(addr), val);
							}
						}
						if (sr == 7u)
							gpr(1) = addr;
						else
							gpr(16 + (int)sr) = addr;
					} else {
						unsigned i;
						for (i = 0; i < 16u; i++) {
							if ((mask & (1u << i)) == 0)
								continue;
							if (from) {
								uint32 val = 0;
								if (g3_ea_data(addr)) {
									if (sz == 4u)
										val = vm_read_memory_4(
											g3_rom0(addr));
									else
										val = (uint32)(int32)(int16)
											vm_read_memory_2(
												g3_rom0(addr));
								}
								if (i < 8u)
									gpr(8 + (int)i) = val;
								else if (i == 15u)
									gpr(1) = val;
								else
									gpr(16 + (int)(i - 8u)) = val;
							} else {
								uint32 val;
								if (i < 8u)
									val = gpr(8 + (int)i);
								else if (i == 15u)
									val = gpr(1);
								else
									val = gpr(16 + (int)(i - 8u));
								if (g3_ea_data(addr)) {
									if (sz == 4u)
										vm_write_memory_4(
											g3_rom0(addr), val);
									else
										vm_write_memory_2(
											g3_rom0(addr), val);
								}
							}
							addr += sz;
						}
						if (from && sm == 3u) {
							if (sr == 7u)
								gpr(1) = addr;
							else
								gpr(16 + (int)sr) = addr;
						}
					}
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 3u &&
				    ((op68 >> 6) & 7u) == 1u &&
				    ((op68 >> 3) & 7u) == 3u) {
					const unsigned sr = op68 & 7u;
					const unsigned dr = (op68 >> 9) & 7u;
					uint32 addr = (sr == 7u) ? gpr(1)
						: gpr(16 + (int)sr);
					const uint32 v = (uint32)(int32)(int16)
						vm_read_memory_2(g3_rom0(addr));
					if (sr == 7u)
						gpr(1) = addr + 2;
					else
						gpr(16 + (int)sr) = addr + 2;
					if (dr == 7u)
						gpr(1) = v;
					else
						gpr(16 + (int)dr) = v;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x1f00u) {
					gpr(1) -= 2;
					if (g3_ea_data(gpr(1)))
						vm_write_memory_1(gpr(1),
							gpr(8 + (int)(op68 & 7u)));
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x2f10u) {
					const int an = (int)(op68 & 7u);
					uint32 a = (an == 7) ? gpr(1) : gpr(16 + an);
					uint32 v = 0;
					if (g3_ea_data(g3_rom0(a)))
						v = vm_read_memory_4(g3_rom0(a));
					gpr(1) -= 4;
					if (g3_ea_data(gpr(1)))
						vm_write_memory_4(gpr(1), v);
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x2f28u) {
					const int an = (int)(op68 & 7u);
					const int32 d =
						(int16)vm_read_memory_2(r24);
					uint32 a = ((an == 7) ? gpr(1)
							    : gpr(16 + an)) + d;
					uint32 v = 0;
					if (g3_ea_data(g3_rom0(a)))
						v = vm_read_memory_4(g3_rom0(a));
					gpr(1) -= 4;
					if (g3_ea_data(gpr(1)))
						vm_write_memory_4(gpr(1), v);
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (op68 == 0x2f38u) {
					const uint32 a = (uint32)(int32)(int16)
						vm_read_memory_2(r24);
					uint32 v = 0;
					if (g3_ea_data(a))
						v = vm_read_memory_4(g3_rom0(a));
					gpr(1) -= 4u;
					if (g3_ea_data(gpr(1)))
						vm_write_memory_4(gpr(1), v);
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff0u) == 0x2f00u) {
					const unsigned r = op68 & 0xfu;
					uint32 v = (r < 8u) ? gpr(8 + (int)r)
						: ((r == 15u) ? gpr(1)
							      : gpr(16 + (int)(r - 8u)));
					gpr(1) -= 4;
					if (g3_ea_data(gpr(1)))
						vm_write_memory_4(gpr(1), v);
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x4868u) {
					const int an = (int)(op68 & 7u);
					const int32 d =
						(int16)vm_read_memory_2(r24);
					uint32 a = ((an == 7) ? gpr(1)
							    : gpr(16 + an)) + d;
					gpr(1) -= 4;
					if (g3_ea_data(gpr(1)))
						vm_write_memory_4(gpr(1), a);
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xfff8u) == 0x4870u) {
					const int an = (int)(op68 & 7u);
					const uint32 ext = vm_read_memory_2(r24);
					r24 += 2;
					const uint32 basea = (an == 7) ? gpr(1)
								    : gpr(16 + an);
					const int da = (int)((ext >> 15) & 1u);
					const int xr = (int)((ext >> 12) & 7u);
					const int wl = (int)((ext >> 11) & 1u);
					const int sc = (int)((ext >> 9) & 3u);
					uint32 xn = da ? ((xr == 7) ? gpr(1)
							       : gpr(16 + xr))
						      : gpr(8 + xr);
					if (!wl)
						xn = (uint32)(int32)(int16)xn;
					xn <<= sc;
					uint32 addr = basea;
					if ((ext & 0x100u) == 0) {
						addr = basea +
						       (int32)(int8)(ext & 0xffu) + xn;
					} else {
						const int bs = (int)((ext >> 7) & 1u);
						const int isup = (int)((ext >> 6) & 1u);
						const int bdsz = (int)((ext >> 4) & 3u);
						const int iis = (int)(ext & 7u);
						uint32 bd = 0;
						if (bdsz == 2) {
							bd = (uint32)(int32)(int16)
								vm_read_memory_2(r24);
							r24 += 2;
						} else if (bdsz == 3) {
							bd = vm_read_memory_4(r24);
							r24 += 4;
						}
						uint32 od = 0;
						if (iis == 2 || iis == 3 ||
						    iis == 6 || iis == 7) {
							od = (uint32)(int32)(int16)
								vm_read_memory_2(r24);
							r24 += 2;
						} else if (iis == 4) {
							od = vm_read_memory_4(r24);
							r24 += 4;
						}
						uint32 inner = (bs ? 0 : basea) + bd;
						if (!isup && iis < 6)
							inner += xn;
						if (iis >= 2 && g3_ea_data(inner))
							inner = vm_read_memory_4(
								g3_rom0(inner));
						if (!isup && iis >= 6)
							inner += xn;
						addr = inner + od;
					}
					gpr(1) -= 4;
					if (g3_ea_data(gpr(1)))
						vm_write_memory_4(gpr(1), addr);
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned npeai;
						if (npeai < 8) {
							npeai++;
							nw_boot_log("G3: 68k PEA idx");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xfff0u) == 0x3f00u) {
					const unsigned r = op68 & 0xfu;
					uint32 v = (r < 8u) ? gpr(8 + (int)r)
						: ((r == 15u) ? gpr(1)
							      : gpr(16 + (int)(r - 8u)));
					gpr(1) -= 2;
					if (g3_ea_data(gpr(1)))
						vm_write_memory_2(gpr(1), v);
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 2u &&
				    ((op68 >> 3) & 7u) == 0u &&
				    ((op68 >> 6) & 7u) == 4u) {
					const unsigned sr = op68 & 7u;
					const unsigned dr = (op68 >> 9) & 7u;
					uint32 v = gpr(8 + (int)sr);
					uint32 da = (dr == 7u) ? gpr(1)
							 : gpr(16 + (int)dr);
					da -= 4;
					if (dr == 7u)
						gpr(1) = da;
					else
						gpr(16 + (int)dr) = da;
					if (g3_ea_data(g3_rom0(da)))
						vm_write_memory_4(g3_rom0(da), v);
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int32)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmlpre;
						if (nmlpre < 8) {
							nmlpre++;
							nw_boot_log("G3: 68k MOVE.L Dn,-(An)");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xc000u) == 0 &&
				    ((op68 >> 12) & 3u) == 3u &&
				    ((op68 >> 3) & 7u) == 0u &&
				    ((op68 >> 6) & 7u) == 4u) {
					const unsigned sr = op68 & 7u;
					const unsigned dr = (op68 >> 9) & 7u;
					uint32 v = gpr(8 + (int)sr) & 0xffffu;
					uint32 da = (dr == 7u) ? gpr(1)
							 : gpr(16 + (int)dr);
					da -= 2;
					if (dr == 7u)
						gpr(1) = da;
					else
						gpr(16 + (int)dr) = da;
					if (g3_ea_data(da))
						vm_write_memory_2(g3_rom0(da), v);
					g3_ccr = 0;
					if (v == 0)
						g3_ccr |= 4;
					if ((int16)v < 0)
						g3_ccr |= 8;
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmwpre;
						if (nmwpre < 8) {
							nmwpre++;
							nw_boot_log("G3: 68k MOVE.W Dn,-(An)");
						}
					}
#endif
					continue;
				}
				/* MOVE.W (An)+,-(SP) 3f18. Open 0x5adc2. */
				if ((op68 & 0xfff8u) == 0x3f18u) {
					const int an = (int)(op68 & 7u);
					uint32 sa = (an == 7) ? gpr(1)
							      : gpr(16 + an);
					uint32 v = 0;
					if (g3_ea_data(sa))
						v = vm_read_memory_2(g3_rom0(sa));
					sa += 2;
					if (an == 7)
						gpr(1) = sa;
					else
						gpr(16 + an) = sa;
					gpr(1) -= 2;
					if (g3_ea_data(gpr(1)))
						vm_write_memory_2(gpr(1), (uint16)v);
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmwpsp;
						if (nmwpsp < 8) {
							nmwpsp++;
							nw_boot_log("G3: 68k MOVE.W (An)+,-(SP)");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xfff8u) == 0x3f10u) {
					const int an = (int)(op68 & 7u);
					uint32 sa = (an == 7) ? gpr(1)
							      : gpr(16 + an);
					uint32 v = 0;
					if (g3_ea_data(sa))
						v = vm_read_memory_2(g3_rom0(sa));
					gpr(1) -= 2;
					if (g3_ea_data(gpr(1)))
						vm_write_memory_2(gpr(1), (uint16)v);
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmwsp;
						if (nmwsp < 8) {
							nmwsp++;
							nw_boot_log("G3: 68k MOVE.W (An),-(SP)");
						}
					}
#endif
					continue;
				}
				if ((op68 & 0xfff8u) == 0x3f28u) {
					const int an = (int)(op68 & 7u);
					const int32 d =
						(int16)vm_read_memory_2(r24);
					uint32 sa = ((an == 7) ? gpr(1)
							      : gpr(16 + an)) + d;
					uint32 v = 0;
					if (g3_ea_data(sa))
						v = vm_read_memory_2(g3_rom0(sa));
					gpr(1) -= 2;
					if (g3_ea_data(gpr(1)))
						vm_write_memory_2(gpr(1), (uint16)v);
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nmwdsp;
						if (nmwdsp < 8) {
							nmwdsp++;
							nw_boot_log("G3: 68k MOVE.W d16(An),-(SP)");
						}
					}
#endif
					continue;
				}
				if (op68 == 0x3f38u) {
					const uint32 a = (uint32)(int32)(int16)
						vm_read_memory_2(r24);
					uint32 v = 0;
					if (g3_ea_data(a))
						v = vm_read_memory_2(g3_rom0(a));
					gpr(1) -= 2u;
					if (g3_ea_data(gpr(1)))
						vm_write_memory_2(gpr(1), (uint16)v);
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (op68 == 0x3f3cu) {
					const uint32 v = vm_read_memory_2(r24);
					gpr(1) -= 2;
					vm_write_memory_2(gpr(1), v);
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (op68 == 0x4878u) {
					const int32 a =
						(int16)vm_read_memory_2(r24);
					gpr(1) -= 4;
					vm_write_memory_4(gpr(1), (uint32)a);
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (op68 == 0x487au) {
					const int32 d =
						(int16)vm_read_memory_2(r24);
					gpr(1) -= 4;
					vm_write_memory_4(gpr(1), r24 + d);
					gpr(24) = r24 + 2;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if ((op68 & 0xf000u) == 0xa000u) {
					if (op68 == 0xa746u || op68 == 0xa346u ||
					    op68 == 0xa146u) {
						const uint32 trap = gpr(8) & 0x0fffu;
						gpr(16) = ROMBase + 0x2000u + trap * 4u;
						gpr(8) = 0;
					} else if (op68 == 0xa816u) {
						/* Pack8: sel 0x0204 is ptr+result
						 * (pop 6). sel 0x0610 leaves a
						 * word for MOVE.W (SP)+. */
						const uint32 sel = gpr(8) & 0xffffu;
						uint32 sp = gpr(1);
						if (sel == 0x0204u) {
							if (g3_ea_data(sp + 4u))
								vm_write_memory_2(
									sp + 4u, 0);
							if (g3_ea_data(sp))
								gpr(1) = sp + 6u;
						} else {
							gpr(1) -= 2u;
							if (g3_ea_data(gpr(1)))
								vm_write_memory_2(
									gpr(1), 1);
						}
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned na816;
							if (na816 < 8) {
								na816++;
								char buf[80];
								snprintf(buf, sizeof(buf),
									 "G3: 68k Pack8 A816 sel=%u",
									 (unsigned)sel);
								nw_boot_log(buf);
							}
						}
#endif
					} else if (op68 == 0xa88fu) {
						uint16 sel = 0;
						if (g3_ea_data(gpr(1))) {
							sel = vm_read_memory_2(gpr(1));
							vm_write_memory_2(gpr(1), 0);
						}
						if (sel == 0x3au &&
						    g3_ea_data(gpr(1) + 2u)) {
							uint32 p = g3_rom0(
								vm_read_memory_4(gpr(1) + 2u));
							if (g3_ea_data(p + 0x14u)) {
								vm_write_memory_4(
									p + 0x10u, 0x464e4452u);
								vm_write_memory_4(
									p + 0x14u, 0x4d414353u);
							}
						}
						gpr(8) = 0;
					} else if (op68 == 0xa895u) {
						gpr(1) += 2;
						gpr(8) = 0;
					} else if (op68 == 0xa81au ||
						   op68 == 0xa81bu) {
						gpr(1) += 10;
						gpr(1) -= 2;
						if (g3_ea_data(gpr(1)))
							vm_write_memory_2(gpr(1), 0xffffu);
						gpr(8) = 0xffffffffu;
					} else if (op68 == 0xa9a0u ||
						   op68 == 0xa9abu) {
						/* Return planted handle so
						 * 'nsrd'/driver GetResource
						 * does not retry on NULL. */
						gpr(1) += 6;
						gpr(1) -= 4;
						{
							const uint32 h =
								RAMBase + 0xd000u;
							if (g3_ea_data(gpr(1)))
								vm_write_memory_4(
									gpr(1), h);
						}
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned ngrstub;
							if (ngrstub < 8) {
								ngrstub++;
								nw_boot_log("G3: 68k GetRes A9A0 stub");
							}
						}
#endif
					} else if (op68 == 0xa1adu) {
						/* Gestalt: D0=selector, A0=response
						 * out. 'dply' bit0 / 'dplv'<10. */
						{
							const uint32 sel = gpr(8);
							uint32 resp = 0;
							uint32 err = 0xffffea51u;
							if (sel == 0x64706c79u) {
								resp = 1;
								err = 0;
							} else if (sel == 0x64706c76u) {
								resp = 2;
								err = 0;
							} else if (sel == 0x706f7772u) {
								/* 'powr' gestaltPowerMgrAttr. */
								resp = 0;
								err = 0;
							}
							gpr(16) = resp;
							gpr(8) = err;
#if NW_BOOT_LOG
							{
								static unsigned ngest;
								if (ngest < 8) {
									ngest++;
									char buf[96];
									snprintf(buf, sizeof(buf),
										 "G3: 68k Gestalt A1AD sel=%08x err=%04x",
										 (unsigned)sel,
										 (unsigned)(err & 0xffffu));
									nw_boot_log(buf);
								}
							}
#endif
						}
					} else if (op68 == 0xabebu) {
						/* DisplayDispatch: selector in D0,
						 * OSErr on TOS. Not Gestalt. */
						{
							const unsigned sel =
								(unsigned)(gpr(8) & 0xffffu);
							if (g3_ea_data(gpr(1)))
								vm_write_memory_2(gpr(1), 0);
							gpr(8) = 0;
#if NW_BOOT_LOG
							{
								static unsigned ndisp;
								if (ndisp < 8) {
									ndisp++;
									char buf[80];
									snprintf(buf, sizeof(buf),
										 "G3: 68k DisplayDispatch ABEB sel=%u",
										 sel);
									nw_boot_log(buf);
								}
							}
#endif
						}
					} else if (op68 == 0xa089u) {
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned nscsia;
							if (nscsia < 8) {
								nscsia++;
								nw_boot_log("G3: 68k SCSIAtomic A089");
							}
						}
#endif
					} else if (op68 == 0xa092u) {
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned negret;
							if (negret < 8) {
								negret++;
								nw_boot_log("G3: 68k EgretDispatch A092");
							}
						}
#endif
					} else if (op68 == 0xa823u) {
						if (g3_ea_data(gpr(1)))
							vm_write_memory_2(gpr(1), 0);
						gpr(8) = 0;
					} else if (op68 == 0xa71eu ||
						   op68 == 0xa11eu ||
						   op68 == 0xa11au ||
						   op68 == 0xa51eu ||
						   op68 == 0xa01eu) {
						static uint32 heap;
						if (!heap)
							heap = RAMBase + 0x50000u;
						uint32 n = gpr(8);
						if ((int32)n <= 0 || n > 0x100000u)
							n = 16;
						n = (n + 15u) & ~15u;
						if (n == 0)
							n = 16;
						if (heap + n >= RAMBase + 0x7c00000u)
							heap = RAMBase + 0x50000u;
						if (n < 0x100000u &&
						    heap + n < RAMBase + 0x7c00000u) {
							uint32 p = heap;
							unsigned i;
							heap += n;
							if (op68 == 0xa71eu ||
							    op68 == 0xa51eu ||
							    op68 == 0xa11au) {
								for (i = 0; i < n; i += 4)
									vm_write_memory_4(p + i, 0);
							}
							gpr(16) = p;
							gpr(8) = 0;
						} else {
							gpr(16) = 0;
							gpr(8) = 0xffffffd4u;
						}
#if NW_BOOT_LOG
						if (op68 == 0xa11au) {
							static unsigned nnp11a;
							if (nnp11a < 8) {
								nnp11a++;
								nw_boot_log("G3: 68k NewPtr A11A");
							}
						} else if (op68 == 0xa71eu) {
							static unsigned nnp71e;
							if (nnp71e < 8) {
								nnp71e++;
								nw_boot_log("G3: 68k NewPtrSysClear wrap");
							}
						}
#endif
					} else if (op68 == 0xa122u ||
						   op68 == 0xa322u ||
						   op68 == 0xa522u ||
						   op68 == 0xa722u) {
						/* NewHandle / Clear / Sys / SysClear.
						 * A0 = handle (ptr to master ptr). */
						static uint32 hheap;
						if (!hheap)
							hheap = RAMBase + 0x200000u;
						uint32 n = gpr(8);
						if ((int32)n <= 0 || n > 0x100000u)
							n = 16;
						n = (n + 15u) & ~15u;
						if (n == 0)
							n = 16;
						if (hheap + n + 8u >= RAMBase + 0x7c00000u)
							hheap = RAMBase + 0x200000u;
						if (n < 0x100000u &&
						    hheap + n + 8u < RAMBase + 0x7c00000u) {
							uint32 mp = hheap;
							hheap += 4;
							uint32 blk = hheap;
							hheap += n;
							if (op68 == 0xa322u ||
							    op68 == 0xa722u) {
								unsigned i;
								for (i = 0; i < n; i += 4)
									vm_write_memory_4(blk + i, 0);
							}
							vm_write_memory_4(mp, blk);
							gpr(16) = mp;
							gpr(8) = 0;
						} else {
							gpr(16) = 0;
							gpr(8) = 0xffffffd4u;
						}
#if NW_BOOT_LOG
						{
							static unsigned nnh;
							if (nnh < 8) {
								nnh++;
								nw_boot_log("G3: 68k NewHandle A122");
							}
						}
#endif
					} else if (op68 == 0xa148u) {
						gpr(16) = RAMBase + 0x40000u;
						gpr(8) = 0;
					} else if (op68 == 0xa99cu ||
						   op68 == 0xa99du) {
						/* CountResources / GetIndResource:
						 * pop-4 + D0=0 keeps bits; fake
						 * handle collapsed unique. */
						if (g3_ea_data(gpr(1)))
							gpr(1) += 4;
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned ncnt1;
							if (ncnt1 < 8) {
								ncnt1++;
								nw_boot_log("G3: 68k Count1Res A99D");
							}
						}
#endif
					} else if (op68 == 0xa9c9u ||
						   op68 == 0xa9ffu) {
#if NW_BOOT_LOG
						{
							static unsigned nserr;
							if (nserr < 16) {
								nserr++;
								char buf[80];
								snprintf(buf, sizeof(buf),
									 "G3: 68k SysError A9C9 d0=%08x pc=%08x",
									 (unsigned)gpr(8),
									 (unsigned)r24);
								nw_boot_log(buf);
							}
						}
#endif
						gpr(8) = 0;
					} else if (op68 == 0xa997u) {
						gpr(1) += 4;
						gpr(1) -= 2;
						if (g3_ea_data(gpr(1)))
							vm_write_memory_2(gpr(1), 0xffffu);
						gpr(8) = 0xffffffffu;
					} else if (op68 == 0xa02cu) {
						gpr(8) = 0;
					} else if (op68 == 0xa9e6u) {
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned niniw;
							if (niniw < 8) {
								niniw++;
								nw_boot_log("G3: 68k InitWindows A9E6");
							}
						}
#endif
					} else if (op68 == 0xa97cu) {
						/* GetCCursor: plant a dummy
						 * handle on TOS so A4 is not
						 * nil/-1 and ModalDialog runs. */
						const uint32 h = RAMBase + 0xd400u;
						const uint32 blk = RAMBase + 0xd500u;
						const uint32 sp = g3_rom0(gpr(1));
						vm_write_memory_4(h, blk);
						vm_write_memory_4(sp, h);
						gpr(16) = h;
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned ngcc;
							if (ngcc < 8) {
								ngcc++;
								char buf[80];
								snprintf(buf, sizeof(buf),
									 "G3: 68k GetCCursor A97C pc=%08x",
									 (unsigned)r24);
								nw_boot_log(buf);
							}
						}
#endif
					} else if (op68 == 0xa996u) {
						/* _InitGraf: Pascal pop globalPtr,
						 * plant QD globals + grafPort at FB. */
						uint32 gp = 0;
						if (g3_ea_data(gpr(1))) {
							gp = g3_rom0(
								vm_read_memory_4(gpr(1)));
							gpr(1) += 4;
						}
						if (!g3_ea_data(gp) ||
						    (gp >= ROMBase &&
						     gp < ROMBase + 0x500000u))
							gp = RAMBase + 0xa000u;
						{
							const uint32 port =
								RAMBase + 0xa100u;
							const uint32 fb =
								RAMBase + 0x400000u;
							unsigned i;
							for (i = 0; i < 128u; i += 4)
								vm_write_memory_4(gp + i, 0);
							for (i = 0; i < 108u; i += 4)
								vm_write_memory_4(port + i, 0);
							vm_write_memory_4(gp, port);
							vm_write_memory_4(0x2a6u, gp);
							vm_write_memory_4(0x2aau, port);
							/* $034E is FCBSPtr, not thePort. */
							{
								const uint32 fcb =
									RAMBase + 0xe000u;
								const uint32 obj =
									RAMBase + 0xf000u;
								unsigned fi;
								vm_write_memory_2(fcb, 0x17au);
								for (fi = 0; fi < 16u; fi++)
									vm_write_memory_4(
										fcb + 0x14u + fi * 4u,
										obj);
								for (fi = 0; fi < 128u; fi += 2)
									vm_write_memory_2(obj + fi, 1);
								vm_write_memory_2(obj + 0x48u, 1);
								vm_write_memory_2(obj + 0x4cu, 0);
								vm_write_memory_4(0x34eu, fcb);
#if NW_BOOT_LOG
								{
									static unsigned nfcbg;
									if (nfcbg < 8) {
										nfcbg++;
										nw_boot_log("G3: 68k plant FCB +0x4c=0");
									}
								}
#endif
							}
							vm_write_memory_1(port, 0x80);
							vm_write_memory_4(port + 2u, fb);
							vm_write_memory_2(port + 6u, 80);
							vm_write_memory_2(port + 12u, 480);
							vm_write_memory_2(port + 14u, 640);
							vm_write_memory_2(port + 38u, 1);
							vm_write_memory_2(port + 40u, 1);
							vm_write_memory_2(port + 48u, 1);
							vm_write_memory_2(port + 50u, 1);
							vm_write_memory_4(0x824u, fb);
							vm_write_memory_4(
								0x808u,
								ROMBase + 0x8e7a0u);
							{
								unsigned y, x;
								for (y = 0; y < 16u; y++)
									for (x = 0; x < 80u; x++)
										vm_write_memory_1(
											fb + y * 80u + x,
											0xffu);
							}
						}
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned nig;
							if (nig < 8) {
								nig++;
								nw_boot_log("G3: 68k InitGraf A996 $0808=0x8e7a0");
							}
						}
#endif
					} else if (op68 == 0xa019u) {
						/* InitZone (not InitPort A86D, not
						 * GetEOF A011). Keep $02AA plant. */
						uint32 p = g3_rom0(gpr(16));
						if (g3_ea_data(p))
							vm_write_memory_4(0x2aau, p);
						gpr(8) = 0;
					} else if (op68 == 0xa01bu) {
						/* SetZone(hz). Pascal TOS zone.
						 * 0x5418c after MOVE.L $02A6,(A7). */
						uint32 sp = gpr(1);
						uint32 z = 0;
						if (g3_ea_data(sp)) {
							z = g3_rom0(
								vm_read_memory_4(sp));
							gpr(1) = sp + 4u;
						}
						if (g3_ea_data(z) &&
						    z >= 0x20000u)
							vm_write_memory_4(0x2a6u, z);
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned nszone;
							if (nszone < 8) {
								nszone++;
								nw_boot_log("G3: 68k SetZone A01B");
							}
						}
#endif
					} else if (op68 == 0xa883u ||
						   op68 == 0xa884u ||
						   op68 == 0xa893u) {
						/* DrawChar / DrawString / DebugStr:
						 * stamp 8x8 blocks into planted FB. */
						uint32 port = g3_rom0(
							vm_read_memory_4(0x2aau));
						if (!g3_ea_data(port))
							port = RAMBase + 0xa100u;
						uint32 fb = g3_rom0(
							vm_read_memory_4(port + 2u));
						int32 rb = (int16)vm_read_memory_2(
							port + 6u);
						if (rb < 0)
							rb = -rb;
						if (rb < 80)
							rb = 80;
						if (!g3_ea_data(fb))
							fb = RAMBase + 0x400000u;
						int32 px = (int16)vm_read_memory_2(
							port + 48u);
						int32 py = (int16)vm_read_memory_2(
							port + 50u);
						unsigned nch = 1;
						if (op68 != 0xa883u) {
							uint32 sp = gpr(1);
							uint32 str = 0;
							if (g3_ea_data(sp)) {
								str = g3_rom0(
									vm_read_memory_4(sp));
								gpr(1) = sp + 4;
							}
							if (g3_ea_data(str))
								nch = vm_read_memory_1(str);
							if (nch > 80u)
								nch = 80u;
						} else if (g3_ea_data(gpr(1)))
							gpr(1) += 2;
						{
							unsigned ci, y, x;
							for (ci = 0; ci < nch; ci++) {
								for (y = 0; y < 8u; y++) {
									uint32 row = fb +
										(uint32)(py + (int32)y) *
										(uint32)rb;
									for (x = 0; x < 8u; x++) {
										int32 col = px + (int32)x;
										uint32 a = row +
											(uint32)(col / 8);
										if (!g3_ea_data(a))
											continue;
										uint32 bit = 0x80u >>
											(unsigned)(col & 7);
										vm_write_memory_1(
											a,
											vm_read_memory_1(a) |
											(uint8)bit);
									}
								}
								px += 8;
							}
							if (g3_ea_data(port + 48u))
								vm_write_memory_2(
									port + 48u, (uint16)px);
						}
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned ndr;
							if (ndr < 8) {
								ndr++;
								nw_boot_log("G3: 68k DrawChar/String");
							}
						}
#endif
					} else if (op68 == 0xa260u ||
						   op68 == 0xa060u) {
						/* HFSDispatch: sel 26 GetVolParms
						 * noErr; other sels stay fnfErr.
						 * Full fake volume collapsed unique. */
						{
							const unsigned sel =
								(unsigned)(gpr(8) & 0xffffu);
							uint32 pb = g3_rom0(gpr(16));
							if (sel == 26u) {
								uint32 buf = 0;
								if (g3_ea_data(pb + 32u))
									buf = g3_rom0(
										vm_read_memory_4(
											pb + 32u));
								if (g3_ea_data(buf) &&
								    buf >= 0x20000u) {
									vm_write_memory_2(buf, 2);
									/* bHasDesktopMgr. Full fake
									 * volume collapsed unique. */
									vm_write_memory_4(buf + 2u,
											  0x00000001u);
								}
								if (g3_ea_data(pb + 16u))
									vm_write_memory_2(
										pb + 16u, 0);
								gpr(8) = 0;
#if NW_BOOT_LOG
								{
									static unsigned nvp;
									if (nvp < 8) {
										nvp++;
										nw_boot_log("G3: 68k HFS GetVolParms attrib");
									}
								}
#endif
							} else if (sel == 9u) {
								/* GetCatInfo: idx 1 / empty name /
								 * nlen==8 ("Mac OS 9"). Other
								 * by-name stays fnfErr. */
								int16 idx = 0;
								unsigned nlen = 0xffu;
								char nbuf[32];
								uint32 np = 0;
								nbuf[0] = 0;
								if (g3_ea_data(pb + 28u))
									idx = (int16)vm_read_memory_2(
										pb + 28u);
								if (idx == 0 &&
								    g3_ea_data(pb + 18u)) {
									np = g3_rom0(
										vm_read_memory_4(
											pb + 18u));
									/* NULL / lowmem vector is not a
									 * Str255 (np=0 read nlen=8). */
									if (g3_ea_data(np) &&
									    np >= 0x20000u) {
										nlen = vm_read_memory_1(np);
										if (nlen > 31u)
											nlen = 31u;
										{
											unsigned ni;
											for (ni = 0; ni < nlen; ni++)
												nbuf[ni] = (char)vm_read_memory_1(
													np + 1u + ni);
											nbuf[nlen] = 0;
										}
									} else {
										/* NULL / $18: plant
										 * volume name so catalog
										 * is not empty Str255. */
										const uint32 nn =
											RAMBase + 0xdc00u;
										static const char vol[] =
											"Mac OS 9";
										unsigned ni;
										nlen = 8u;
										vm_write_memory_1(nn, 8);
										for (ni = 0; ni < 8u; ni++) {
											vm_write_memory_1(
												nn + 1u + ni,
												(uint8)vol[ni]);
											nbuf[ni] = vol[ni];
										}
										nbuf[8] = 0;
										vm_write_memory_4(
											pb + 18u, nn);
										np = nn;
									}
								}
								if (idx == 1 ||
								    (idx == 0 && nlen == 0) ||
								    (idx == 0 && nlen == 8u &&
								     (unsigned char)nbuf[0] >= 0x20u)) {
									if (g3_ea_data(pb + 16u))
										vm_write_memory_2(
											pb + 16u, 0);
									if (g3_ea_data(pb + 30u))
										vm_write_memory_1(
											pb + 30u, 0x10);
									if (g3_ea_data(pb + 48u))
										vm_write_memory_4(
											pb + 48u, 2);
									if (g3_ea_data(pb + 52u))
										vm_write_memory_2(
											pb + 52u, 1);
									if (g3_ea_data(pb + 100u))
										vm_write_memory_4(
											pb + 100u, 1);
									gpr(8) = 0;
								} else {
									if (g3_ea_data(pb + 16u))
										vm_write_memory_2(
											pb + 16u, 0xffd5u);
									gpr(8) = 0xffffffd5u;
								}
#if NW_BOOT_LOG
								{
									static unsigned ngci;
									if (ngci < 8) {
										ngci++;
										char buf[96];
										snprintf(buf, sizeof(buf),
											 "G3: 68k HFS GetCatInfo idx=%d nlen=%u np=%08x nm=%s",
											 (int)idx, nlen, (unsigned)np, nbuf);
										nw_boot_log(buf);
									}
								}
#endif
							} else {
								if (g3_ea_data(pb + 16u))
									vm_write_memory_2(
										pb + 16u, 0xffd5u);
								gpr(8) = 0xffffffd5u;
							}
#if NW_BOOT_LOG
							{
								static unsigned nhfsd;
								if (nhfsd < 8) {
									nhfsd++;
									char buf[80];
									snprintf(buf, sizeof(buf),
										 "G3: 68k HFSDispatch A260 sel=%u",
										 sel);
									nw_boot_log(buf);
								}
							}
#endif
						}
					} else if (op68 == 0xa20cu ||
						   op68 == 0xa00cu) {
						/* HGetFileInfo / GetFileInfo:
						 * default D0=0 was false noErr. */
						uint32 pb = g3_rom0(gpr(16));
						if (g3_ea_data(pb + 16u))
							vm_write_memory_2(
								pb + 16u, 0xffd5u);
						gpr(8) = 0xffffffd5u;
#if NW_BOOT_LOG
						{
							static unsigned ngfi;
							if (ngfi < 8) {
								ngfi++;
								nw_boot_log("G3: 68k HGetFileInfo A20C");
							}
						}
#endif
					} else if (op68 == 0xa991u ||
						   op68 == 0xa981u) {
						/* ModalDialog / DrawDialog: stamp a
						 * 1-bit frame on the planted FB. */
						{
							const uint32 fb =
								RAMBase + 0x400000u;
							unsigned y, x;
							for (y = 120u; y < 360u; y++) {
								const uint32 row =
									fb + y * 80u;
								for (x = 10u; x < 70u; x++) {
									uint8 v = 0;
									if (y == 120u ||
									    y == 359u ||
									    x == 10u ||
									    x == 69u)
										v = 0xffu;
									if (g3_ea_data(row + x))
										vm_write_memory_1(
											row + x, v);
								}
							}
						}
						if (op68 == 0xa991u) {
							uint32 sp = gpr(1);
							uint32 ptr = 0;
							if (g3_ea_data(sp + 4u))
								ptr = g3_rom0(
									vm_read_memory_4(sp + 4u));
							/* itemHit=0 keeps ModalDialog BNE
							 * retry. Writing 1 was DisposeDialog. */
							if (g3_ea_data(ptr) &&
							    ptr >= 0x20000u)
								vm_write_memory_2(ptr, 0);
							if (g3_ea_data(sp))
								gpr(1) = sp + 8u;
						} else if (g3_ea_data(gpr(1)))
							gpr(1) += 4u;
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned na991;
							if (na991 < 8) {
								na991++;
								nw_boot_log(
									op68 == 0xa991u
									? "G3: 68k A991 itemHit=0"
									: "G3: 68k DrawDialog A981");
							}
						}
#endif
					} else if (op68 == 0xa983u) {
						/* DisposeDialog(theDialog). Pascal
						 * pop 4. Do not set itemHit=1. */
						if (g3_ea_data(gpr(1)))
							gpr(1) += 4u;
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned nddlg;
							if (nddlg < 8) {
								nddlg++;
								nw_boot_log("G3: 68k DisposeDialog A983");
							}
						}
#endif
					} else if (op68 == 0xa98bu) {
						/* ParamText(s0..s3). Pascal pop 16.
						 * Site 0x3e1b4 then A991. */
						if (g3_ea_data(gpr(1)))
							gpr(1) += 16u;
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned nptxt;
							if (nptxt < 8) {
								nptxt++;
								nw_boot_log("G3: 68k ParamText A98B");
							}
						}
#endif
					} else if (op68 == 0xa982u) {
						/* CloseDialog(theDialog). Pascal
						 * pop 4. Site 0x6314e. */
						if (g3_ea_data(gpr(1)))
							gpr(1) += 4u;
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned ncdlg;
							if (ncdlg < 8) {
								ncdlg++;
								nw_boot_log("G3: 68k CloseDialog A982");
							}
						}
#endif
					} else if (op68 == 0xa91bu) {
						/* SetWTitle(window, title). Pascal
						 * pop 8. Site 0x51f08 near Pack8. */
						if (g3_ea_data(gpr(1)))
							gpr(1) += 8u;
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned nswt;
							if (nswt < 8) {
								nswt++;
								nw_boot_log("G3: 68k SetWTitle A91B");
							}
						}
#endif
					} else if (op68 == 0xa91fu) {
						/* ShowWindow(theWindow). Pascal pop 4. */
						if (g3_ea_data(gpr(1)))
							gpr(1) += 4u;
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned nshw;
							if (nshw < 8) {
								nshw++;
								nw_boot_log("G3: 68k ShowWindow A91F");
							}
						}
#endif
					} else if (op68 == 0xa98fu) {
						/* SetDialogItemText(item, text).
						 * Pascal pop 8. Site 0x107dc. */
						if (g3_ea_data(gpr(1)))
							gpr(1) += 8u;
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned nsit;
							if (nsit < 8) {
								nsit++;
								nw_boot_log("G3: 68k SetDialogItemText A98F");
							}
						}
#endif
					} else if (op68 == 0xa98du) {
						/* GetDialogItem: TOS box*, item*,
						 * itemType*, itemNo.W, dialog*. Pop 18. */
						uint32 sp = gpr(1);
						uint32 boxp = 0, itemp = 0, typep = 0;
						if (g3_ea_data(sp))
							boxp = g3_rom0(
								vm_read_memory_4(sp));
						if (g3_ea_data(sp + 4u))
							itemp = g3_rom0(
								vm_read_memory_4(sp + 4u));
						if (g3_ea_data(sp + 8u))
							typep = g3_rom0(
								vm_read_memory_4(sp + 8u));
						if (g3_ea_data(typep) &&
						    typep >= 0x20000u)
							vm_write_memory_2(typep, 4);
						if (g3_ea_data(itemp) &&
						    itemp >= 0x20000u)
							vm_write_memory_4(
								itemp, RAMBase + 0xd400u);
						if (g3_ea_data(boxp) &&
						    boxp >= 0x20000u) {
							vm_write_memory_2(boxp, 40);
							vm_write_memory_2(boxp + 2u, 40);
							vm_write_memory_2(boxp + 4u, 80);
							vm_write_memory_2(boxp + 6u, 200);
						}
						if (g3_ea_data(sp))
							gpr(1) = sp + 18u;
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned ngdi;
							if (ngdi < 8) {
								ngdi++;
								nw_boot_log("G3: 68k GetDialogItem A98D");
							}
						}
#endif
					} else if (op68 == 0xa98eu) {
						/* SetDialogItem(dialog, itemNo,
						 * itemType, item, box). Pascal
						 * pop 16. */
						if (g3_ea_data(gpr(1)))
							gpr(1) += 16u;
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned nsdi;
							if (nsdi < 8) {
								nsdi++;
								nw_boot_log("G3: 68k SetDialogItem A98E");
							}
						}
#endif
					} else if (op68 == 0xaa68u) {
						/* DialogDispatch. 0x0304/0x0305:
						 * ptr.L + word = 6. */
						{
							const unsigned sel =
								(unsigned)(gpr(8) & 0xffffu);
							unsigned pop = 4u;
							if (sel == 0x0304u ||
							    sel == 0x0305u)
								pop = 6u;
							else if (sel == 0x07fdu)
								pop = 10u;
							else if (sel == 0x0402u)
								pop = 8u;
							if (g3_ea_data(gpr(1)))
								gpr(1) += pop;
							if (sel == 0x0402u ||
							    sel == 0x0203u) {
								gpr(1) -= 2u;
								if (g3_ea_data(gpr(1)))
									vm_write_memory_2(
										gpr(1), 0);
							}
							if (sel == 0x0304u) {
								const uint32 fb =
									RAMBase + 0x400000u;
								unsigned y, x;
								for (y = 128u; y < 352u; y++) {
									const uint32 row =
										fb + y * 80u;
									for (x = 12u; x < 68u; x++)
										if (g3_ea_data(row + x))
											vm_write_memory_1(
												row + x, 0xffu);
								}
							}
							gpr(8) = 0;
#if NW_BOOT_LOG
							{
								static unsigned ndd;
								if (ndd < 8) {
									ndd++;
									char buf[80];
									snprintf(buf, sizeof(buf),
										 "G3: 68k DialogDispatch AA68 sel=%04x fill",
										 sel);
									nw_boot_log(buf);
								}
							}
#endif
						}
					} else if (op68 == 0xa8d6u ||
						   op68 == 0xa8d8u) {
						/* NewRgn A8D8 (A8D6 alias). Return
						 * dummy RgnHandle. 0x523f8 265f pop. */
						const uint32 h = RAMBase + 0xd600u;
						const uint32 blk = RAMBase + 0xd700u;
						vm_write_memory_4(h, blk);
						vm_write_memory_2(blk, 10);
						gpr(1) -= 4u;
						if (g3_ea_data(gpr(1)))
							vm_write_memory_4(gpr(1), h);
						gpr(16) = h;
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned nnrgn;
							if (nnrgn < 8) {
								nnrgn++;
								nw_boot_log(
									op68 == 0xa8d8u
									? "G3: 68k NewRgn A8D8"
									: "G3: 68k NewRgn A8D6");
							}
						}
#endif
					} else if (op68 == 0xa8d9u) {
						/* CloseRgn(dstRgn). */
						if (g3_ea_data(gpr(1)))
							gpr(1) += 4u;
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned ncrgn;
							if (ncrgn < 8) {
								ncrgn++;
								nw_boot_log("G3: 68k CloseRgn A8D9");
							}
						}
#endif
					} else if (op68 == 0xa97du) {
						/* GetNewDialog: Pascal pop
						 * behind.L dStorage.L id.W,
						 * return planted grafPort. */
						if (g3_ea_data(gpr(1)))
							gpr(1) += 10u;
						gpr(1) -= 4u;
						{
							const uint32 dlg =
								RAMBase + 0xa100u;
							if (g3_ea_data(gpr(1)))
								vm_write_memory_4(
									gpr(1), dlg);
							gpr(16) = dlg;
						}
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned ngnd;
							if (ngnd < 8) {
								ngnd++;
								nw_boot_log("G3: 68k GetNewDialog A97D");
							}
						}
#endif
					} else if (op68 == 0xa06eu) {
						/* SlotManager: no more sResources. */
						gpr(8) = 0xfffffea8u;
					} else if (op68 == 0xa86eu ||
						   op68 == 0xa86du ||
						   op68 == 0xabe8u ||
						   op68 == 0xabe9u) {
						uint32 p = g3_rom0(gpr(16));
						int lowmem = 0;
						/* A0=0 is g3_ea_data-true lowmem;
						 * writing a grafPort there smashes
						 * vectors / ScrnBase. */
						if (!g3_ea_data(p) || p < 0x20000u ||
						    (p >= ROMBase &&
						     p < ROMBase + 0x500000u)) {
							p = RAMBase + 0xa100u;
							lowmem = 1;
						}
						gpr(16) = p;
						{
							unsigned i;
							for (i = 0; i < 128u; i += 4)
								vm_write_memory_4(p + i, 0);
						}
						if (op68 == 0xa86eu ||
						    op68 == 0xabe8u ||
						    op68 == 0xabe9u) {
							vm_write_memory_4(p + 2u,
									  RAMBase + 0x400000u);
							vm_write_memory_2(p + 6u, 80);
							vm_write_memory_2(p + 8u, 0);
							vm_write_memory_2(p + 10u, 0);
							vm_write_memory_2(p + 12u, 480);
							vm_write_memory_2(p + 14u, 640);
							vm_write_memory_2(p + 38u, 1);
							vm_write_memory_2(p + 40u, 1);
							vm_write_memory_2(p + 48u, 1);
							vm_write_memory_2(p + 50u, 1);
							vm_write_memory_4(0x986u, p);
							vm_write_memory_4(0x2aau, p);
							vm_write_memory_4(0x824u,
									  RAMBase + 0x400000u);
							vm_write_memory_4(
								0x808u,
								ROMBase + 0x8e7a0u);
							{
								const uint32 fcb =
									RAMBase + 0xe000u;
								const uint32 obj =
									RAMBase + 0xf000u;
								unsigned fi;
								vm_write_memory_2(fcb, 0x17au);
								for (fi = 0; fi < 16u; fi++)
									vm_write_memory_4(
										fcb + 0x14u + fi * 4u,
										obj);
								for (fi = 0; fi < 128u; fi += 2)
									vm_write_memory_2(obj + fi, 1);
								vm_write_memory_2(obj + 0x48u, 1);
								vm_write_memory_2(obj + 0x4cu, 0);
								vm_write_memory_4(0x34eu, fcb);
#if NW_BOOT_LOG
								{
									static unsigned nfcbp;
									if (nfcbp < 8) {
										nfcbp++;
										nw_boot_log("G3: 68k plant FCB +0x4c=0");
									}
								}
#endif
							}
						}
						gpr(8) = 0;
#if NW_BOOT_LOG
						if (lowmem) {
							static unsigned nlowp;
							if (nlowp < 8) {
								nlowp++;
								nw_boot_log("G3: 68k OpenPort lowmem");
							}
						} else if (op68 == 0xabe8u ||
							   op68 == 0xabe9u) {
							static unsigned ncport;
							if (ncport < 8) {
								ncport++;
								nw_boot_log("G3: 68k InitCPort ABE9");
							}
						}
#endif
					} else if (op68 == 0xaa5au) {
						uint16 sel = 0;
						uint32 sp = gpr(1);
						if (g3_ea_data(sp))
							sel = vm_read_memory_2(sp);
						/* Pascal: result word under args+selector.
						 * Sel 1 GetSharedLibrary 26 bytes above result.
						 * Sel 5 FindSymbol 18 bytes above result. */
						unsigned pop = 10u;
						if (sel == 1u)
							pop = 26u;
						else if (sel == 5u)
							pop = 18u;
						else if (sel == 2u || sel == 3u)
							pop = 22u;
						else if (sel == 0xfffcu)
							pop = 2u;
						if (sel == 1u &&
						    g3_ea_data(sp + 10u)) {
							uint32 cidp = g3_rom0(
								vm_read_memory_4(sp + 10u));
							if (g3_ea_data(cidp) &&
							    cidp >= 0x20000u)
								vm_write_memory_4(cidp, 1);
						}
						sp += pop;
						gpr(1) = sp;
						if (g3_ea_data(sp))
							vm_write_memory_2(sp, 0);
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned ncfm;
							if (ncfm < 12) {
								ncfm++;
								char buf[80];
								snprintf(buf, sizeof(buf),
									 "G3: 68k CFM AA5A sel=%u",
									 (unsigned)sel);
								nw_boot_log(buf);
							}
						}
#endif
					} else if (op68 == 0xa96fu) {
						uint32 elem = g3_rom0(gpr(16));
						uint32 qh = g3_rom0(gpr(17));
						if (g3_ea_data(elem) &&
						    g3_ea_data(qh) &&
						    (elem & 1u) == 0 &&
						    (qh & 1u) == 0 &&
						    elem >= 0x20000u) {
							vm_write_memory_4(elem, 0);
							uint32 tail =
								vm_read_memory_4(qh + 6u);
							if (tail == 0 ||
							    !g3_ea_data(tail))
								vm_write_memory_4(qh + 2u,
										  elem);
							else
								vm_write_memory_4(
									g3_rom0(tail),
									elem);
							vm_write_memory_4(qh + 6u, elem);
						}
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned nenq;
							if (nenq < 8) {
								nenq++;
								nw_boot_log("G3: 68k Enqueue A96F");
							}
						}
#endif
					} else if (op68 == 0xa8a7u) {
						/* SetRect(r,l,t,rgt,b). Pascal
						 * LTR: TOS bottom.. r at +8. */
						uint32 sp = gpr(1);
						uint32 rp = 0;
						if (g3_ea_data(sp + 8u))
							rp = g3_rom0(
								vm_read_memory_4(sp + 8u));
						if (g3_ea_data(rp) &&
						    rp >= 0x20000u &&
						    g3_ea_data(sp)) {
							vm_write_memory_2(
								rp,
								vm_read_memory_2(sp + 4u));
							vm_write_memory_2(
								rp + 2u,
								vm_read_memory_2(sp + 6u));
							vm_write_memory_2(
								rp + 4u,
								vm_read_memory_2(sp));
							vm_write_memory_2(
								rp + 6u,
								vm_read_memory_2(sp + 2u));
						}
						if (g3_ea_data(sp))
							gpr(1) = sp + 12u;
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned nsrect;
							if (nsrect < 8) {
								nsrect++;
								nw_boot_log("G3: 68k SetRect A8A7");
							}
						}
#endif
					} else if (op68 == 0xa8a8u) {
						/* OffsetRect(r, dh, dv). Pascal
						 * LTR: TOS dv, dh, r at +4. */
						uint32 sp = gpr(1);
						uint32 rp = 0;
						int16 dh = 0, dv = 0;
						if (g3_ea_data(sp)) {
							dv = (int16)vm_read_memory_2(sp);
							dh = (int16)vm_read_memory_2(sp + 2u);
						}
						if (g3_ea_data(sp + 4u))
							rp = g3_rom0(
								vm_read_memory_4(sp + 4u));
						if (g3_ea_data(rp) &&
						    rp >= 0x20000u) {
							vm_write_memory_2(
								rp,
								(uint16)((int16)vm_read_memory_2(rp) + dv));
							vm_write_memory_2(
								rp + 2u,
								(uint16)((int16)vm_read_memory_2(rp + 2u) + dh));
							vm_write_memory_2(
								rp + 4u,
								(uint16)((int16)vm_read_memory_2(rp + 4u) + dv));
							vm_write_memory_2(
								rp + 6u,
								(uint16)((int16)vm_read_memory_2(rp + 6u) + dh));
						}
						if (g3_ea_data(sp))
							gpr(1) = sp + 8u;
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned noffr;
							if (noffr < 8) {
								noffr++;
								nw_boot_log("G3: 68k OffsetRect A8A8");
							}
						}
#endif
					} else if (op68 == 0xa8a9u) {
						/* InsetRect(r, dh, dv). Same frame
						 * as OffsetRect. Not a rgn trap. */
						uint32 sp = gpr(1);
						uint32 rp = 0;
						int16 dh = 0, dv = 0;
						if (g3_ea_data(sp)) {
							dv = (int16)vm_read_memory_2(sp);
							dh = (int16)vm_read_memory_2(sp + 2u);
						}
						if (g3_ea_data(sp + 4u))
							rp = g3_rom0(
								vm_read_memory_4(sp + 4u));
						if (g3_ea_data(rp) &&
						    rp >= 0x20000u) {
							vm_write_memory_2(
								rp,
								(uint16)((int16)vm_read_memory_2(rp) + dv));
							vm_write_memory_2(
								rp + 2u,
								(uint16)((int16)vm_read_memory_2(rp + 2u) + dh));
							vm_write_memory_2(
								rp + 4u,
								(uint16)((int16)vm_read_memory_2(rp + 4u) - dv));
							vm_write_memory_2(
								rp + 6u,
								(uint16)((int16)vm_read_memory_2(rp + 6u) - dh));
						}
						if (g3_ea_data(sp))
							gpr(1) = sp + 8u;
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned ninsr;
							if (ninsr < 8) {
								ninsr++;
								nw_boot_log("G3: 68k InsetRect A8A9");
							}
						}
#endif
					} else if (op68 == 0xa8dfu) {
						/* RectRgn(rgn, r). Pascal TOS
						 * r, rgn handle at +4. Rectangular
						 * rgnSize=10. Do not host A8E4. */
						uint32 sp = gpr(1);
						uint32 rp = 0, rh = 0, p = 0;
						if (g3_ea_data(sp))
							rp = g3_rom0(
								vm_read_memory_4(sp));
						if (g3_ea_data(sp + 4u))
							rh = g3_rom0(
								vm_read_memory_4(sp + 4u));
						if (g3_ea_data(rh) &&
						    rh >= 0x20000u)
							p = g3_rom0(
								vm_read_memory_4(rh));
						if (g3_ea_data(p) &&
						    p >= 0x20000u &&
						    g3_ea_data(rp) &&
						    rp >= 0x20000u) {
							vm_write_memory_2(p, 10);
							vm_write_memory_2(
								p + 2u,
								vm_read_memory_2(rp));
							vm_write_memory_2(
								p + 4u,
								vm_read_memory_2(rp + 2u));
							vm_write_memory_2(
								p + 6u,
								vm_read_memory_2(rp + 4u));
							vm_write_memory_2(
								p + 8u,
								vm_read_memory_2(rp + 6u));
						}
						if (g3_ea_data(sp))
							gpr(1) = sp + 8u;
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned nrrgn;
							if (nrrgn < 8) {
								nrrgn++;
								nw_boot_log("G3: 68k RectRgn A8DF");
							}
						}
#endif
					} else if (op68 == 0xa8deu) {
						/* SetRectRgn(rgn,l,t,r,b). Pascal
						 * TOS b,r,t,l, rgn at +8. Pop 12. */
						uint32 sp = gpr(1);
						uint32 rh = 0, p = 0;
						if (g3_ea_data(sp + 8u))
							rh = g3_rom0(
								vm_read_memory_4(sp + 8u));
						if (g3_ea_data(rh) &&
						    rh >= 0x20000u)
							p = g3_rom0(
								vm_read_memory_4(rh));
						if (g3_ea_data(p) &&
						    p >= 0x20000u &&
						    g3_ea_data(sp)) {
							vm_write_memory_2(p, 10);
							vm_write_memory_2(
								p + 2u,
								vm_read_memory_2(sp + 4u));
							vm_write_memory_2(
								p + 4u,
								vm_read_memory_2(sp + 6u));
							vm_write_memory_2(
								p + 6u,
								vm_read_memory_2(sp));
							vm_write_memory_2(
								p + 8u,
								vm_read_memory_2(sp + 2u));
						}
						if (g3_ea_data(sp))
							gpr(1) = sp + 12u;
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned nsrrgn;
							if (nsrrgn < 8) {
								nsrrgn++;
								nw_boot_log("G3: 68k SetRectRgn A8DE");
							}
						}
#endif
					} else if (op68 == 0xa910u) {
						/* GetPicture: sites PEA dest
						 * then CMP vs -1 / MOVEA result.
						 * Pop 4, write dummy pict ptr. */
						uint32 sp = gpr(1);
						uint32 rp = 0;
						if (g3_ea_data(sp)) {
							rp = g3_rom0(
								vm_read_memory_4(sp));
							gpr(1) = sp + 4u;
						}
						{
							const uint32 pict =
								RAMBase + 0xdb00u;
							unsigned i;
							for (i = 0; i < 32u; i += 4u)
								vm_write_memory_4(
									pict + i, 0);
							vm_write_memory_2(pict, 10);
							if (g3_ea_data(rp) &&
							    rp >= 0x20000u)
								vm_write_memory_4(rp, pict);
							gpr(8) = pict;
						}
#if NW_BOOT_LOG
						{
							static unsigned npict;
							if (npict < 8) {
								npict++;
								nw_boot_log("G3: 68k GetPicture A910");
							}
						}
#endif
					} else if (op68 == 0xa8aau) {
						/* SectRect: pop 12, Boolean true as
						 * word for MOVE.B (SP)+ (0x5b13e). */
						if (g3_ea_data(gpr(1)))
							gpr(1) += 12u;
						gpr(1) -= 2u;
						if (g3_ea_data(gpr(1)))
							vm_write_memory_2(gpr(1), 1);
						gpr(8) = 1;
#if NW_BOOT_LOG
						{
							static unsigned nsect;
							if (nsect < 8) {
								nsect++;
								nw_boot_log("G3: 68k SectRect A8AA");
							}
						}
#endif
					} else if (op68 == 0xaa2au) {
						/* GetMainDevice: result GDHandle
						 * already slotted (SUBQ #4). */
						const uint32 h = RAMBase + 0xd000u;
						if (g3_ea_data(gpr(1)))
							vm_write_memory_4(gpr(1), h);
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned ngmd;
							if (ngmd < 8) {
								ngmd++;
								nw_boot_log("G3: 68k GetMainDevice AA2A");
							}
						}
#endif
					} else if (op68 == 0xaa31u) {
						/* SetGDevice(gd). Pascal pop 4. */
						if (g3_ea_data(gpr(1)))
							gpr(1) += 4u;
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned nsgd;
							if (nsgd < 8) {
								nsgd++;
								nw_boot_log("G3: 68k SetGDevice AA31");
							}
						}
#endif
					} else if (op68 == 0xaa06u) {
						/* SetPortPix(pm). Pascal pop 4. */
						if (g3_ea_data(gpr(1)))
							gpr(1) += 4u;
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned nspx;
							if (nspx < 8) {
								nspx++;
								nw_boot_log("G3: 68k SetPortPix AA06");
							}
						}
#endif
					} else if (op68 == 0xa873u) {
						/* SetPort(gp). Pascal pop 4. */
						uint32 p = 0;
						if (g3_ea_data(gpr(1))) {
							p = g3_rom0(
								vm_read_memory_4(gpr(1)));
							gpr(1) += 4u;
						}
						if (g3_ea_data(p) && p >= 0x20000u &&
						    !(p >= ROMBase &&
						      p < ROMBase + 0x500000u))
							vm_write_memory_4(0x2aau, p);
						gpr(16) = p;
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned nsetp;
							if (nsetp < 8) {
								nsetp++;
								nw_boot_log("G3: 68k SetPort A873");
							}
						}
#endif
					} else if (op68 == 0xa879u) {
						/* SetClip(rgn). Pascal pop 4. */
						if (g3_ea_data(gpr(1)))
							gpr(1) += 4u;
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned nsetc;
							if (nsetc < 8) {
								nsetc++;
								nw_boot_log("G3: 68k SetClip A879");
							}
						}
#endif
					} else if (op68 == 0xa8a1u) {
						/* FrameRect(r). Pascal pop 4. */
						if (g3_ea_data(gpr(1)))
							gpr(1) += 4u;
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned nfrr;
							if (nfrr < 8) {
								nfrr++;
								nw_boot_log("G3: 68k FrameRect A8A1");
							}
						}
#endif
					} else if (op68 == 0xa8a3u) {
						/* EraseRect(r). Pascal pop 4. */
						if (g3_ea_data(gpr(1)))
							gpr(1) += 4u;
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned nerr;
							if (nerr < 8) {
								nerr++;
								nw_boot_log("G3: 68k EraseRect A8A3");
							}
						}
#endif
					} else if (op68 == 0xa8a2u) {
						/* PaintRect(r). Pascal pop 4. */
						if (g3_ea_data(gpr(1)))
							gpr(1) += 4u;
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned nptr;
							if (nptr < 8) {
								nptr++;
								nw_boot_log("G3: 68k PaintRect A8A2");
							}
						}
#endif
					} else if (op68 == 0xa002u ||
						   op68 == 0xa202u ||
						   op68 == 0xa450u) {
						/* Read / HFS glue A450. noErr +
						 * 0 bytes retried with SetFPos.
						 * eofErr so the loop can exit.
						 * Keep Write. */
						uint32 pb = g3_rom0(gpr(16));
						if (g3_ea_data(pb + 16u))
							vm_write_memory_2(pb + 16u,
									  0xffd9u);
						if (g3_ea_data(pb + 40u))
							vm_write_memory_4(pb + 40u, 0);
						gpr(8) = 0xffffffd9u;
#if NW_BOOT_LOG
						{
							static unsigned nrd;
							if (nrd < 8) {
								nrd++;
								nw_boot_log("G3: 68k Read A002/A450 eofErr");
							}
						}
#endif
					} else if (op68 == 0xa852u ||
						   op68 == 0xa8d6u ||
						   op68 == 0xa89eu ||
						   op68 == 0xa89bu ||
						   op68 == 0xa877u ||
						   op68 == 0xa871u ||
						   op68 == 0xa89au ||
						   op68 == 0xa88du ||
						   op68 == 0xa894u ||
						   op68 == 0xa9e5u ||
						   op68 == 0xa9ebu ||
						   op68 == 0xa9ecu ||
						   op68 == 0xa198u ||
						   op68 == 0xa010u ||
						   op68 == 0xa000u ||
						   op68 == 0xa001u ||
						   op68 == 0xa003u ||
						   op68 == 0xa012u ||
						   op68 == 0xa023u ||
						   op68 == 0xa025u ||
						   op68 == 0xa029u ||
						   op68 == 0xa02au ||
						   op68 == 0xa069u ||
						   op68 == 0xa874u ||
						   op68 == 0xa87au ||
						   op68 == 0xa87bu ||
						   op68 == 0xa8e4u ||
						   op68 == 0xa8a4u ||
						   op68 == 0xaa19u ||
						   op68 == 0xa045u ||
						   op68 == 0xa9e0u ||
						   op68 == 0xa200u) {
						gpr(8) = 0;
					} else if (op68 == 0xa011u) {
						/* GetEOF: ioMisc at +28 is LEOF.
						 * Unhosted; empty file so SetFPos
						 * retry can exit. */
						uint32 pb = g3_rom0(gpr(16));
						if (g3_ea_data(pb + 16u))
							vm_write_memory_2(pb + 16u, 0);
						if (g3_ea_data(pb + 28u))
							vm_write_memory_4(pb + 28u, 0);
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned ngeof;
							if (ngeof < 8) {
								ngeof++;
								nw_boot_log("G3: 68k GetEOF A011");
							}
						}
#endif
					} else if (op68 == 0xa044u) {
						/* SetFPos(PB). noErr retried at
						 * 0xa8248. eofErr (-39) so the
						 * read loop can exit. */
						uint32 pb = g3_rom0(gpr(16));
						if (g3_ea_data(pb + 16u))
							vm_write_memory_2(pb + 16u,
									  0xffd9u);
						gpr(8) = 0xffffffd9u;
#if NW_BOOT_LOG
						{
							static unsigned nsfp;
							if (nsfp < 8) {
								nsfp++;
								nw_boot_log("G3: 68k SetFPos A044 eofErr");
							}
						}
#endif
					} else if (op68 == 0xa085u) {
						/* PMgrOp after 0x6d40 DBF fill.
						 * Do not skip the fill. OS trap. */
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned npmgr;
							if (npmgr < 8) {
								npmgr++;
								nw_boot_log("G3: 68k PMgrOp A085");
							}
						}
#endif
					} else if (op68 == 0xa08eu ||
						   op68 == 0xa48eu) {
						/* BTreeDispatch. fnfErr not noErr
						 * (HFS noErr all sels milled). */
						gpr(8) = 0xffffffd5u;
#if NW_BOOT_LOG
						{
							static unsigned nbt;
							if (nbt < 8) {
								nbt++;
								nw_boot_log("G3: 68k BTreeDispatch A08E fnfErr");
							}
						}
#endif
					} else if (op68 == 0xa004u ||
						   op68 == 0xa204u) {
						uint32 pb = g3_rom0(gpr(16));
						uint16 cscode = 0;
						if (g3_ea_data(pb + 16u))
							vm_write_memory_2(pb + 16u, 0);
						if (g3_ea_data(pb + 0x1au))
							cscode = vm_read_memory_2(
								pb + 0x1au);
						/* Status fills csParam so AND #7
						 * == 7. Control at 0x80ba writes
						 * $1c then traps. */
						if (g3_ea_data(pb + 0x1cu))
							vm_write_memory_4(
								pb + 0x1cu, 7);
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned nctl;
							if (nctl < 8) {
								nctl++;
								char buf[80];
								snprintf(buf, sizeof(buf),
									 "G3: 68k Control A004 csCode=%u",
									 (unsigned)cscode);
								nw_boot_log(buf);
							}
						}
#endif
					} else if (op68 == 0xa005u ||
						   op68 == 0xa205u) {
						/* _Status: noErr, csCode $20
						 * result bit0-2 so AND #7 == 7. */
						uint32 pb = g3_rom0(gpr(16));
						if (g3_ea_data(pb + 16u))
							vm_write_memory_2(pb + 16u, 0);
						if (g3_ea_data(pb + 0x1cu))
							vm_write_memory_4(pb + 0x1cu, 7);
						if (g3_ea_data(pb + 0x42u))
							vm_write_memory_4(pb + 0x42u, 7);
						gpr(8) = 0;
					} else if (op68 == 0xa02eu ||
						   op68 == 0xa22eu) {
						uint32 n = gpr(8);
						uint32 s = g3_rom0(gpr(16));
						uint32 d = g3_rom0(gpr(17));
						if (n > 0x100000u)
							n = 0x100000u;
						if (!g3_ea_data(d) || d < 0x20000u)
							d = RAMBase + 0x400000u;
						if (g3_ea_data(s) && g3_ea_data(d)) {
							unsigned i;
							for (i = 0; i + 4 <= n; i += 4)
								vm_write_memory_4(
									d + i,
									vm_read_memory_4(s + i));
							for (; i < n; i++)
								vm_write_memory_1(
									d + i,
									vm_read_memory_1(s + i));
						}
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned nblkm;
							if (nblkm < 8) {
								nblkm++;
								nw_boot_log("G3: 68k BlockMove A22E");
							}
						}
#endif
					} else
						gpr(8) = 0;
					if (op68 == 0xa746u) {
						gpr(16) = RAMBase + 0x9000u;
						gpr(8) = 0;
					} else if (op68 == 0xa031u) {
						/* GetOSEvent: A0=EventRecord, D0=mask.
						 * Do not clobber A0 with ExpandMem. */
						uint32 ev = g3_rom0(gpr(16));
						if (g3_ea_data(ev)) {
							vm_write_memory_2(ev, 0);
							vm_write_memory_4(ev + 2u, 0);
							vm_write_memory_4(ev + 6u, 0);
							vm_write_memory_4(ev + 10u, 0);
							vm_write_memory_2(ev + 14u, 0);
						}
						gpr(8) = 0;
#if NW_BOOT_LOG
						{
							static unsigned ngev;
							if (ngev < 8) {
								ngev++;
								nw_boot_log("G3: 68k GetOSEvent A031");
							}
						}
#endif
					}
					{
						const uint32 d0 = gpr(8);
						g3_ccr = 0;
						if (d0 == 0)
							g3_ccr |= 4;
						if ((int32)d0 < 0)
							g3_ccr |= 8;
					}
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					static unsigned naline;
					if (naline < 24) {
						naline++;
						char buf[96];
						snprintf(buf, sizeof(buf),
							 "G3: 68k A-line %04x pc=%08x d0=%08x a0=%08x",
							 (unsigned)op68,
							 (unsigned)r24,
							 (unsigned)gpr(8),
							 (unsigned)gpr(16));
						nw_boot_log(buf);
					}
#endif
					continue;
				}
				if (op68 == 0x4e73u) {
					const uint32 sp = gpr(1);
					uint32 dest = 0;
					if (g3_ea_data(sp + 2u))
						dest = g3_rom0(vm_read_memory_4(sp + 2u));
					gpr(1) = sp + 6u;
					if (!g3_r24_ok(dest) || dest == r24 - 2u)
						dest = g3_fix_r24(dest);
					gpr(24) = dest;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nrte;
						if (nrte < 16) {
							nrte++;
							char buf[80];
							snprintf(buf, sizeof(buf),
								 "G3: 68k RTE dest=%08x",
								 (unsigned)dest);
							nw_boot_log(buf);
						}
					}
#endif
					continue;
				}
				if (op68 == 0x4e74u) {
					const int32 d =
						(int16)vm_read_memory_2(r24);
					const uint32 sp = gpr(1);
					const uint32 t = vm_read_memory_4(sp);
					gpr(1) = sp + 4 + d;
					{
						uint32 dest = g3_rom0(t);
						if (!g3_r24_ok(dest)) {
							uint32 p = r24 + 2;
							unsigned z;
							for (z = 0; z < 16u; z++) {
								if (vm_read_memory_2(p) != 0)
									break;
								p += 2;
							}
							if (!g3_r24_ok(p))
								p = g3_fix_r24(p);
							dest = p;
#if NW_BOOT_LOG
							{
								static unsigned nrtdf;
								if (nrtdf < 8) {
									nrtdf++;
									nw_boot_log("G3: 68k RTD dest-fix");
								}
							}
#endif
						}
						gpr(24) = dest;
					}
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
					continue;
				}
				if (op68 == 0x4e75u) {
					const uint32 sp = gpr(1);
					const uint32 t = vm_read_memory_4(sp);
					gpr(1) = sp + 4;
					{
						const uint32 dest = g3_rom0(t);
						const uint32 self = r24 - 2u;
						if (g3_r24_ok(dest) && dest != self &&
						    dest != r24)
							gpr(24) = dest;
						else {
							uint32 p = r24;
							unsigned z;
							for (z = 0; z < 64u; z++) {
								if (vm_read_memory_2(p) != 0)
									break;
								p += 2;
							}
							if (!g3_r24_ok(p) || p == self)
								p = g3_fix_r24(p);
							gpr(24) = p;
						}
					}
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					static unsigned nrts;
					if (nrts < 4) {
						nrts++;
						char buf[80];
						snprintf(buf, sizeof(buf),
							 "G3: 68k RTS pc=%08x",
							 (unsigned)gpr(24));
						nw_boot_log(buf);
					}
#endif
					continue;
				}
				if ((op68 & 0xf000u) == 0xf000u) {
					gpr(24) = r24;
					gpr(27) = 0xffffffffu;
					gpr(29) = ROMBase + 0x380000u;
					pc() = ROMBase + 0x366084u;
#if NW_BOOT_LOG
					{
						static unsigned nfl;
						if (nfl < 8) {
							nfl++;
							char buf[80];
							snprintf(buf, sizeof(buf),
								 "G3: 68k F-line %04x pc=%08x",
								 (unsigned)op68,
								 (unsigned)(r24 - 2));
							nw_boot_log(buf);
						}
					}
#endif
					continue;
				}
				/* No prefetch: immediates stay at r24 for the
				 * handler. Handlers that b 0x366080 will lhzu. */
				gpr(24) = r24;
				gpr(27) = op68;
				const uint32 tgt = ROMBase + 0x380000u + op68 * 8u;
				if (tgt >= ROMBase + 0x380000u &&
				    tgt + 8u <= ROMBase + 0x400000u) {
					gpr(29) = tgt;
					pc() = tgt;
#if NW_BOOT_LOG
					static unsigned nopf;
					if (nopf < 24) {
						nopf++;
						char buf[96];
						snprintf(buf, sizeof(buf),
							 "G3: 68k op=%04x to=%08x pc=%08x",
							 (unsigned)op68, (unsigned)tgt,
							 (unsigned)r24);
						nw_boot_log(buf);
					}
#endif
					continue;
				}
				uint32 r27 = op68;
				uint32 r29 = gpr(29);
				uint32 npc = pc();
				if (nw_guest_68k_dispatch(&npc, &r24, &r27, &r29)) {
					gpr(24) = r24;
					gpr(27) = r27;
					gpr(29) = r29;
					pc() = npc;
					continue;
				}
			}
			{
				static unsigned pmdt_walk;
				static unsigned pmdt_lists;
				if (pc() == ROMBase + 0x31e664u) {
					pmdt_lists = 0;
					/*
					 * OF never filled the 16-bank PMDT pointer array at
					 * KDP+0x80. Guest walked 0xffff / KDP as tables.
					 * Plant RAM + ROM + terminator-only lists.
					 */
					static int pmdt_planted;
					if (!pmdt_planted) {
						pmdt_planted = 1;
						const uint32 kdp = gpr(1);
						const uint32 tab = RAMBase + 0x8000u;
						const uint32 ram_n = (RAMSize >> 12) - 1u;
						vm_write_memory_2(tab, 0);
						vm_write_memory_2(tab + 2, ram_n);
						vm_write_memory_4(tab + 4, RAMBase);
						vm_write_memory_2(tab + 8, 0);
						vm_write_memory_2(tab + 10, 0xffff);
						/* Terminator flags must not be 0 or 0xc00
						 * or the walk treats them as convert entries. */
						vm_write_memory_4(tab + 12, 0xe00);
						vm_write_memory_2(tab + 0x10, 0);
						vm_write_memory_2(tab + 0x12, 0x4ff); /* 5 MiB ROM area */
						vm_write_memory_4(tab + 0x14, ROMBase);
						vm_write_memory_2(tab + 0x18, 0);
						vm_write_memory_2(tab + 0x1a, 0xffff);
						vm_write_memory_4(tab + 0x1c, 0xe00);
						vm_write_memory_2(tab + 0x20, 0);
						vm_write_memory_2(tab + 0x22, 0xffff);
						vm_write_memory_4(tab + 0x24, 0xe00);
						for (unsigned b = 0; b < 16; b++) {
							uint32 p = tab + 0x20;
							if (b == (RAMBase >> 28))
								p = tab;
							else if (b == (ROMBase >> 28))
								p = tab + 0x10;
							vm_write_memory_4(kdp + 0x80u + b * 8u, p);
						}
						/* Convert allocs 160-byte area structs from the
						 * NK free list at KDP-2704. That list is empty
						 * this early, so plant one 4KiB free page. */
						const uint32 head = kdp - 2704u;
						uint32 prev = head;
						uint32 first_blk = 0;
						for (unsigned i = 0; i < 4; i++) {
							const uint32 page = RAMBase + 0x9000u + i * 0x1000u;
							const uint32 blk = page + 8u;
							if (!first_blk)
								first_blk = blk;
							vm_write_memory_4(page, 4072);
							vm_write_memory_4(page + 4, 0x8742474eu);
							vm_write_memory_4(blk, 4064);
							vm_write_memory_4(blk + 4, 0x66726565u);
							vm_write_memory_4(blk + 12, prev);
							if (prev == head)
								vm_write_memory_4(head + 8, blk);
							else
								vm_write_memory_4(prev + 8, blk);
							vm_write_memory_4(page + 4072, 0);
							vm_write_memory_4(page + 4076, 0x87454e44u);
							prev = blk;
						}
						vm_write_memory_4(prev + 8, head);
						vm_write_memory_4(head + 12, prev);
#if NW_BOOT_LOG
						char buf[128];
						snprintf(buf, sizeof(buf),
							 "G3: planted PMDT kdp=%08x tab=%08x ram_n=%u pool=%08x",
							 (unsigned)kdp, (unsigned)tab,
							 (unsigned)ram_n, (unsigned)first_blk);
						nw_boot_log(buf);
#endif
					}
				}
				if (pc() == ROMBase + 0x31e66cu) {
					pmdt_walk = 0;
					pmdt_lists++;
					/* r26 += 0x10000000 wraps after 16 lists; skip-dump
					 * at 0x31e5e0 re-enters the walk. */
					if (pmdt_lists > 16) {
#if NW_BOOT_LOG
						static int listed;
						if (!listed) {
							listed = 1;
							nw_boot_log("G3: PMDT lists capped, finish convert");
						}
#endif
						pc() = ROMBase + 0x31e6b4u;
						continue;
					}
				}
				if (pc() == ROMBase + 0x31e67cu) {
					pmdt_walk++;
#if NW_BOOT_LOG
					if (pmdt_walk <= 4 || (pmdt_walk % 16) == 0) {
						char buf[128];
						snprintf(buf, sizeof(buf),
							 "G3: PMDTwalk n=%u r25=%08x r15=%04x r16=%04x r26=%08x",
							 pmdt_walk, (unsigned)gpr(25),
							 (unsigned)(gpr(15) & 0xffffu),
							 (unsigned)(gpr(16) & 0xffffu),
							 (unsigned)gpr(26));
						nw_boot_log(buf);
					}
#endif
					/* Unterminated, non-RAM, or terminator. */
					bool pmdt_done = pmdt_walk > 8 ||
					    gpr(25) < RAMBase ||
					    gpr(25) >= RAMBase + RAMSize;
					if (!pmdt_done &&
					    gpr(25) + 4u < RAMBase + RAMSize &&
					    vm_read_memory_2(gpr(25)) == 0 &&
					    vm_read_memory_2(gpr(25) + 2) == 0xffff)
						pmdt_done = true;
					if (pmdt_done) {
						pc() = ROMBase + 0x31e6a8u;
						continue;
					}
				}
				/* Alloc failed after the walk: dump was patched
				 * to 0x31e674, which re-enters the array. */
				if ((pc() == ROMBase + 0x31e6c0u ||
				     pc() == ROMBase + 0x31e724u) &&
				    gpr(8) == 0) {
#if NW_BOOT_LOG
					static int pmdt_ep;
					if (!pmdt_ep) {
						pmdt_ep = 1;
						nw_boot_log("G3: PMDT alloc-fail -> convert epilogue");
					}
#endif
					pc() = ROMBase + 0x31e77cu;
					continue;
				}
			}
			if (pc() == ROMBase + 0x31e790u) {
#if NW_BOOT_LOG
				static int pmdt_cv;
				if (pmdt_cv < 6) {
					pmdt_cv++;
					char buf[128];
					snprintf(buf, sizeof(buf),
						 "G3: PMDTcvt r25=%08x r15=%04x r16=%04x r17=%08x r26=%08x",
						 (unsigned)gpr(25),
						 (unsigned)(gpr(15) & 0xffffu),
						 (unsigned)(gpr(16) & 0xffffu),
						 (unsigned)gpr(17), (unsigned)gpr(26));
					nw_boot_log(buf);
				}
#endif
			}
			if (pc() == ROMBase + 0x31e7bcu) {
#if NW_BOOT_LOG
				static int pmdt_al;
				if (pmdt_al < 6) {
					pmdt_al++;
					char buf[80];
					snprintf(buf, sizeof(buf),
						 "G3: PMDTalloc r8=%08x r31=%08x",
						 (unsigned)gpr(8), (unsigned)gpr(31));
					nw_boot_log(buf);
				}
#endif
			}
			if (pc() == ROMBase + 0x31e868u) {
#if NW_BOOT_LOG
				static int pmdt;
				if (pmdt < 8) {
					pmdt++;
					char buf[128];
					snprintf(buf, sizeof(buf),
						 "G2: PMDT r15=%08x r16=%08x r24=%08x r31=%08x",
						 (unsigned)gpr(15), (unsigned)gpr(16),
						 (unsigned)gpr(24), (unsigned)gpr(31));
					nw_boot_log(buf);
				}
#endif
			}
			if (pc() == ROMBase + 0x321c1cu) {
				uint32 r8v = gpr(8);
				const uint32 pg = (r8v >= 16) ? r8v - 16u : 0;
				if (pg >= 0x1000 && (pg & 0xfffu) == 0 &&
				    pg < RAMBase) {
					const uint32 np = RAMBase + r8v;
					if ((uint64_t)RAMBase + pg + 4096u <=
					    (uint64_t)RAMBase + RAMSize) {
						gpr(8) = np;
						const uint32 page = np - 16u;
						if (vm_read_memory_2(page + 12) !=
						    0x876c) {
							vm_write_memory_4(page, 4072);
							vm_write_memory_4(page + 4,
									  0x8742474eu);
							vm_write_memory_4(page + 8, 4064);
							vm_write_memory_4(page + 12,
									  0x876c6f63u);
							vm_write_memory_4(page + 4072, 0);
							vm_write_memory_4(page + 4076,
									  0x87454e44u);
						}
#if NW_BOOT_LOG
						static int ins;
						if (ins < 4) {
							ins++;
							char buf[80];
							snprintf(buf, sizeof(buf),
								 "G3: poolins %08x -> %08x",
								 (unsigned)r8v,
								 (unsigned)np);
							nw_boot_log(buf);
						}
#endif
					}
				}
			}
			if (pc() == ROMBase + 0x321c2cu) {
#if NW_BOOT_LOG
				static int pool;
				if (pool < 8) {
					pool++;
					char buf[128];
					snprintf(buf, sizeof(buf),
						 "G3: pool r8=%08x r15=%08x r16=%08x hw=%04x",
						 (unsigned)gpr(8), (unsigned)gpr(15),
						 (unsigned)gpr(16),
						 (unsigned)vm_read_memory_2(gpr(15) + 4));
					nw_boot_log(buf);
				}
#endif
			}
			if (pc() == ROMBase + 0x321d34u) {
				const uint32 page = gpr(17);
				if (page && page < RAMBase &&
				    (uint64_t)RAMBase + page + 4096u <=
					    (uint64_t)RAMBase + RAMSize) {
					gpr(17) = RAMBase + page;
#if NW_BOOT_LOG
					static int reloc;
					if (reloc < 4) {
						reloc++;
						char buf[80];
						snprintf(buf, sizeof(buf),
							 "G3: poolpage %08x -> %08x",
							 (unsigned)page,
							 (unsigned)gpr(17));
						nw_boot_log(buf);
					}
#endif
				}
			}
			if (pc() == ROMBase + 0x3219a0u) {
#if NW_BOOT_LOG
				static int pd;
				if (pd < 4) {
					pd++;
					char buf[144];
					snprintf(buf, sizeof(buf),
						 "G3: pooldump lr=%08x r8=%08x r15=%08x r16=%08x r18=%08x r19=%08x",
						 (unsigned)lr(), (unsigned)gpr(8),
						 (unsigned)gpr(15), (unsigned)gpr(16),
						 (unsigned)gpr(18), (unsigned)gpr(19));
					nw_boot_log(buf);
				}
#endif
			}
			if (pc() == ROMBase + 0x321c68u) {
#if NW_BOOT_LOG
				static int pchk;
				if (pchk < 6) {
					pchk++;
					char buf[144];
					snprintf(buf, sizeof(buf),
						 "G3: poolchk r15=%08x sz=%08x end=%08x tag=%08x want=%08x",
						 (unsigned)gpr(15), (unsigned)gpr(16),
						 (unsigned)gpr(18), (unsigned)gpr(19),
						 (unsigned)gpr(20));
					nw_boot_log(buf);
				}
#endif
			}
			if (pc() == ROMBase + 0x326420u) {
#if NW_BOOT_LOG
				static int dumped;
				if (dumped < 4) {
					dumped++;
					char buf[96];
					snprintf(buf, sizeof(buf),
						 "G2: NKdump lr=%08x r0=%08x r3=%08x r8=%08x r9=%08x srr0=%08x",
						 (unsigned)lr(), (unsigned)gpr(0),
						 (unsigned)gpr(3), (unsigned)gpr(8),
						 (unsigned)gpr(9), (unsigned)srr0_);
					nw_boot_log(buf);
				}
#endif
			}
			if (pc() == ROMBase + 0x325c94u ||
			    pc() == ROMBase + 0x325a9c ||
			    pc() == ROMBase + 0x325998u) {
				const uint32 pic = gpr(28);
				const uint32 opw = vm_read_memory_4(pc());
#if NW_BOOT_LOG
				static int spun;
				if (!spun) {
					spun = 1;
					char buf[112];
					snprintf(buf, sizeof(buf),
						 "G2: picspin r28=%08x pc=%08x op=%08x mill=%d",
						 (unsigned)pic, (unsigned)pc(),
						 (unsigned)opw,
						 nw_guest_first_data_dsi_seen());
					nw_boot_log(buf);
				}
#endif
				/*
				 * After G2 only. Live wrote 0xFFFFFFFF here every
				 * iteration (byte+2=0xFF, bit 2 set) and stayed in
				 * picspin with DSI n=1. That value is the spin
				 * condition. Idle is 0. If this PC is a branch
				 * on r30, skip it once the idle byte is visible.
				 * Do not mill 68k. Do not run before first DSI.
				 */
				if (nw_guest_first_data_dsi_seen() &&
				    pic >= RAMBase &&
				    (uint64_t)pic + 8 <= (uint64_t)RAMBase + RAMSize) {
					vm_write_memory_4(pic, 0);
					vm_write_memory_4(pic + 4, 0);
					vm_write_memory_1(pic + (uint32)NW_NK_IRQ_STATUS_OFF,
							  nw_nk_irq_status_idle());
					gpr(30) = nw_nk_irq_status_idle();
					if (nw_ppc_is_branch(opw)) {
#if NW_BOOT_LOG
						static int left;
						if (!left) {
							left = 1;
							char buf[96];
							snprintf(buf, sizeof(buf),
								 "G2: picspin idle r28=%08x r30=%08x skipbr op=%08x",
								 (unsigned)pic,
								 (unsigned)gpr(30),
								 (unsigned)opw);
							nw_boot_log(buf);
						}
#endif
						pc() += 4;
						continue;
					}
				}
			}
		}
#endif
		uint32 opcode;
		if (!guest_fetch(&opcode)) {
			if (!spcflags().empty() && !check_spcflags())
				goto return_site;
			continue;
		}
		const instr_info_t *ii = decode(opcode);
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
