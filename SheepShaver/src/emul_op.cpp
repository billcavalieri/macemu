/*
 *  emul_op.cpp - 68k opcodes for ROM patches
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

#include <stdio.h>
#include <string.h>

#include "sysdeps.h"
#include "main.h"
#include "version.h"
#include "prefs.h"
#include "cpu_emulation.h"
#include "thunks.h"
#include "xlowmem.h"
#include "xpram.h"
#include "timer.h"
#include "adb.h"
#include "sony.h"
#include "disk.h"
#include "cdrom.h"
#include "scsi.h"
#include "video.h"
#include "audio.h"
#include "audio_defs.h"
#include "ether.h"
#include "serial.h"
#include "clip.h"
#include "extfs.h"
#include "macos_util.h"
#include "rom_patches.h"
#include "nw_io.h"
#include "rsrc_patches.h"
#include "name_registry.h"
#include "user_strings.h"
#include "emul_op.h"
#include "thunks.h"

#define DEBUG 0
#include "debug.h"

extern bool tick_inhibit;

void PlayStartupSound();

// TVector of MakeExecutable
static uint32 MakeExecutableTvec;


/*
 *  Execute EMUL_OP opcode (called by 68k emulator)
 */

/* Sound Manager sift -16569 entry. ResViewer:
 * link; movem.l d0-d2/a3-a4; movea.l 12(a6),a3; move.w 2(a3),d7.
 * The move.w is what makes this the dispatcher. Without it the same
 * link/movem prologue matches unrelated calls. */
static const uint8 nw_sift_prefix[16] = {
	0x4e, 0x56, 0x00, 0x00, 0x48, 0xe7, 0x17, 0x18,
	0x26, 0x6e, 0x00, 0x0c, 0x3e, 0x2b, 0x00, 0x02
};

static void nw_emit16(uint8 *b, int *n, uint16 v)
{
	b[(*n)++] = (uint8)(v >> 8);
	b[(*n)++] = (uint8)v;
}

static void nw_emit32(uint8 *b, int *n, uint32 v)
{
	nw_emit16(b, n, (uint16)(v >> 16));
	nw_emit16(b, n, (uint16)v);
}

/* The 9.2.1 ROM has no vCheckLoad pattern, so the AWACS sift is never
 * rewritten on the way in. The scan only reports copies. It must not
 * write them: a patched entry made Sound quit with error type 3.
 * FindNext/Capture are not used. */
static int nw_debug_arm;

void nw_audio_arm_debug(void)
{
	nw_debug_arm = 1;
}

static void nw_audio_debug_scan(void)
{
	static int pass;
	static const uint8 desc[8] = {
		0x73, 0x64, 0x65, 0x76, 0x61, 0x77, 0x61, 0x63 /* sdevawac */
	};
	if (pass >= 3 || RAMBaseHost == 0 || RAMSize < sizeof nw_sift_prefix)
		return;
	pass++;
	uint8 *base = RAMBaseHost;
	uint8 *end = base + RAMSize;
	uint8 *p = base;
	int sift = 0;
	while (p + sizeof nw_sift_prefix < end && sift < 12) {
		uint8 *hit = (uint8 *)memmem(p, (size_t)(end - p), nw_sift_prefix, sizeof nw_sift_prefix);
		if (!hit)
			break;
		sift++;
		p = hit + sizeof nw_sift_prefix;
		uint32 addr = RAMBase + (uint32)(hit - base);
		/* Do not write these bytes. Replacing link with an emul op
		 * made the Sound control panel execute an illegal instruction
		 * (error type 3) once SheepBlaster was the saved output. */
		const char *what = (addr & 1) ? "odd" : "code";
		printf("NW-BOOT SheepBlaster debug sift pass=%d ptr=%08x %s\n",
		       pass, (unsigned)addr, what);
		fflush(stdout);
	}
	p = base;
	int ndesc = 0;
	while (p + sizeof desc < end && ndesc < 12) {
		uint8 *hit = (uint8 *)memmem(p, (size_t)(end - p), desc, sizeof desc);
		if (!hit)
			break;
		ndesc++;
		p = hit + sizeof desc;
		uint32 addr = RAMBase + (uint32)(hit - base);
		printf("NW-BOOT SheepBlaster debug desc pass=%d ptr=%08x\n",
		       pass, (unsigned)addr);
		fflush(stdout);
	}
	printf("NW-BOOT SheepBlaster debug scan pass=%d sift=%d desc=%d\n",
	       pass, sift, ndesc);
	fflush(stdout);
}

void nw_audio_try(void)
{
	nw_audio_debug_scan();
}


static int nw_reg_arm;
static int nw_audio_live;

int nw_audio_service_ok(void)
{
	return nw_audio_live;
}

void nw_audio_arm_register(void)
{
	nw_reg_arm = 1;
}

/* Install a new sound-output component. Do not overwrite the AWACS
 * entry; that removed the desktop icons. */
static void nw_register_output(void)
{
	if (!nw_reg_arm)
		return;
	nw_reg_arm = 0;
	static const uint8 entry_glue[] = {
		0x4e, 0x56, 0x00, 0x00,
		0x48, 0xe7, 0x80, 0x18,
		0x26, 0x6e, 0x00, 0x0c,
		0x28, 0x6e, 0x00, 0x08,
		(uint8)(M68K_EMUL_OP_AUDIO_DISPATCH >> 8),
		(uint8)(M68K_EMUL_OP_AUDIO_DISPATCH & 0xff),
		0x2d, 0x40, 0x00, 0x10,
		0x4c, 0xdf, 0x18, 0x01,
		0x4e, 0x5e,
		0x4e, 0x74, 0x00, 0x08
	};
	uint32 entry = SheepProc(entry_glue, sizeof entry_glue);
	SheepVar cd(20);
	WriteMacInt32(cd.addr() + 0, 0x73646576); /* sdev */
	WriteMacInt32(cd.addr() + 4, 0x7368626c); /* shbl */
	WriteMacInt32(cd.addr() + 8, 0x53685368); /* ShSh */
	/* 8- and 16-bit, mono and stereo. No rate-convert bit:
	 * the card resamples to 44100 itself. System sounds are
	 * mostly 8-bit mono at 22050, and playing those at 44100
	 * made them run about twice as fast. */
	/* Output only. Input bits made the Sound panel treat this as a
	 * sound input and lock when the device was selected. */
	WriteMacInt32(cd.addr() + 12, 0x00000f00);
	WriteMacInt32(cd.addr() + 16, 0);
	/* The Sound control panel lists GetComponentInfo's name. A nil
	 * name is not shown. NewHandle is the Memory Manager, not the
	 * component-list walk that left the window erased. */
	M68kRegisters nr;
	memset(&nr, 0, sizeof nr);
	nr.d[0] = 16;
	Execute68kTrap(0xa122, &nr);
	uint32 name_h = nr.a[0];
	if (name_h != 0) {
		uint32 p = ReadMacInt32(name_h);
		const char *s = "SheepBlaster";
		WriteMacInt8(p, 12);
		for (int i = 0; i < 12; i++)
			WriteMacInt8(p + 1 + i, (uint8)s[i]);
	}
	printf("NW-BOOT G1: audio-reg name=%08x\n", (unsigned)name_h);
	fflush(stdout);
	/* RegisterComponent(cd, entry, global=1, name, nil, nil).
	 * D0 is the selector ($7001). A parameter block with D0=0 is not
	 * this call, and a 2-byte result slot smashes the guest stack. */
	uint8 stub[48];
	int n = 0;
	nw_emit16(stub, &n, 0x598f);				/* subq.l #4,sp */
	nw_emit16(stub, &n, 0x2f3c); nw_emit32(stub, &n, cd.addr());
	nw_emit16(stub, &n, 0x2f3c); nw_emit32(stub, &n, entry);
	nw_emit16(stub, &n, 0x3f3c); nw_emit16(stub, &n, 1);	/* global */
	nw_emit16(stub, &n, 0x2f3c); nw_emit32(stub, &n, name_h);
	nw_emit16(stub, &n, 0x2f3c); nw_emit32(stub, &n, 0);	/* info */
	nw_emit16(stub, &n, 0x2f3c); nw_emit32(stub, &n, 0);	/* icon */
	nw_emit16(stub, &n, 0x7001);				/* moveq #1,d0 */
	nw_emit16(stub, &n, 0xa82a);
	nw_emit16(stub, &n, 0x201f);				/* move.l (sp)+,d0 */
	nw_emit16(stub, &n, 0x4e75);
	uint32 stub_addr = SheepProc(stub, n);
	M68kRegisters rr;
	memset(&rr, 0, sizeof rr);
	/* Open/Register during RegisterComponent must hit this card, not
	 * the mixer path. That path calls 68k again and freezes the desktop. */
	nw_sheepblaster_set_ready(1);
	printf("NW-BOOT G1: audio-reg enter\n");
	fflush(stdout);
	Execute68k(stub_addr, &rr);
	uint32 component = rr.d[0];
	printf("NW-BOOT G1: audio-reg component=%08x\n", (unsigned)component);
	fflush(stdout);
	if (component == 0 || name_h == 0) {
		static int tries;
		nw_sheepblaster_set_ready(0);
		if (++tries < 5)
			nw_reg_arm = 1;
		printf("NW-BOOT G1: audio-reg retry component=%08x name=%08x\n",
		       (unsigned)component, (unsigned)name_h);
		fflush(stdout);
		return;
	}
	/* Do not call SetDefaultSoundOutput here. On the 1024 boot it
	 * never returned: the main thread stayed in that 68k call and
	 * the desktop never appeared. The Output list stays on Built-in
	 * until the panel is opened. FindNext/Capture does not return
	 * either. The stuck bytes are the component-list walk. */
	nw_audio_live = 1;
	nw_audio_try();
}

int32 nw_sheepblaster_delegate(uint32 params, uint32 target)
{
	static int busy;
	static uint32 stub;
	if (busy || params == 0 || target == 0)
		return badComponentSelector;
	if (stub == 0) {
		uint8 b[16];
		int n = 0;
		nw_emit16(b, &n, 0x598f);
		nw_emit16(b, &n, 0x2f09);			/* params */
		nw_emit16(b, &n, 0x2f08);			/* target */
		nw_emit16(b, &n, 0x7024);			/* DelegateComponentCall */
		nw_emit16(b, &n, 0xa82a);
		nw_emit16(b, &n, 0x201f);
		nw_emit16(b, &n, 0x4e75);
		stub = SheepProc(b, n);
	}
	busy = 1;
	M68kRegisters rr;
	memset(&rr, 0, sizeof rr);
	rr.a[0] = target;
	rr.a[1] = params;
	Execute68k(stub, &rr);
	busy = 0;
	return (int32)rr.d[0];
}

void EmulOp(M68kRegisters *r, uint32 pc, int selector)
{
	/* Not from the Time Manager task: registration allocates memory,
	 * which is not allowed at interrupt time. */
	bool sb_op = selector == OP_AUDIO_DISPATCH || selector == OP_SHEEPBLASTER ||
		selector == OP_SHEEPBLASTER_TICK;
	if (!sb_op)
		nw_register_output();
	if (nw_debug_arm && !sb_op) {
		nw_debug_arm = 0;
		nw_audio_debug_scan();
	}
	D(bug("EmulOp %04x at %08x\n", selector, pc));
	switch (selector) {
		case OP_BREAK:				// Breakpoint
			printf("*** Breakpoint\n");
			Dump68kRegs(r);
			break;

		case OP_XPRAM1: {			// Read/write from/to XPRam
			uint32 len = r->d[3];
			uint8 *adr = Mac2HostAddr(r->a[3]);
			D(bug("XPRAMReadWrite d3: %08lx, a3: %p\n", len, adr));
			int ofs = len & 0xffff;
			len >>= 16;
			if (len & 0x8000) {
				len &= 0x7fff;
				for (uint32 i=0; i<len; i++)
					XPRAM[((ofs + i) & 0xff) + 0x1300] = *adr++;
			} else {
				for (uint32 i=0; i<len; i++)
					*adr++ = XPRAM[((ofs + i) & 0xff) + 0x1300];
			}
			break;
		}

		case OP_XPRAM2:				// Read from XPRam
			r->d[1] = XPRAM[(r->d[1] & 0xff) + 0x1300];
			break;

		case OP_XPRAM3:				// Write to XPRam
			XPRAM[(r->d[1] & 0xff) + 0x1300] = r->d[2];
			break;

		case OP_NVRAM1: {			// Read from NVRAM
			int ofs = r->d[0];
			r->d[0] = XPRAM[ofs & 0x1fff];
			bool localtalk = !(XPRAM[0x13e0] || XPRAM[0x13e1]);	// LocalTalk enabled?
			switch (ofs) {
				case 0x13e0:			// Disable LocalTalk (use EtherTalk instead)
					if (localtalk)
						r->d[0] = 0x00;
					break;
				case 0x13e1:
					if (localtalk)
						r->d[0] = 0x01;
					break;
				case 0x13e2:
					if (localtalk)
						r->d[0] = 0x00;
					break;
				case 0x13e3:
					if (localtalk)
						r->d[0] = 0x0a;
					break;
			}
			break;
		}

		case OP_NVRAM2:				// Write to NVRAM
			XPRAM[r->d[0] & 0x1fff] = r->d[1];
			break;

		case OP_NVRAM3:				// Read/write from/to NVRAM
			if (r->d[3]) {
				r->d[0] = XPRAM[(r->d[4] + 0x1300) & 0x1fff];
			} else {
				XPRAM[(r->d[4] + 0x1300) & 0x1fff] = r->d[5];
				r->d[0] = 0;
			}
			break;

		case OP_FIX_MEMTOP:			// Fixes MemTop in BootGlobs during startup
			D(bug("Fix MemTop\n"));
			WriteMacInt32(BootGlobsAddr - 20, RAMBase + RAMSize);	// MemTop
			r->a[6] = RAMBase + RAMSize;
			break;

		case OP_FIX_MEMSIZE: {		// Fixes physical/logical RAM size during startup
			D(bug("Fix MemSize\n"));
			uint32 diff = ReadMacInt32(0x1ef8) - ReadMacInt32(0x1ef4);
			WriteMacInt32(0x1ef8, RAMSize);			// Physical RAM size
			WriteMacInt32(0x1ef4, RAMSize - diff);	// Logical RAM size
			break;
		}

		case OP_FIX_BOOTSTACK:		// Fixes boot stack pointer in boot 3 resource
			D(bug("Fix BootStack\n"));
			r->a[1] = r->a[7] = RAMBase + RAMSize * 3 / 4;
			break;

		case OP_SONY_OPEN:			// Floppy driver functions
			r->d[0] = SonyOpen(r->a[0], r->a[1]);
			break;
		case OP_SONY_PRIME:
			r->d[0] = SonyPrime(r->a[0], r->a[1]);
			break;
		case OP_SONY_CONTROL:
			r->d[0] = SonyControl(r->a[0], r->a[1]);
			break;
		case OP_SONY_STATUS:
			r->d[0] = SonyStatus(r->a[0], r->a[1]);
			break;

		case OP_DISK_OPEN:			// Disk driver functions
			r->d[0] = DiskOpen(r->a[0], r->a[1]);
			break;
		case OP_DISK_PRIME:
			r->d[0] = DiskPrime(r->a[0], r->a[1]);
			break;
		case OP_DISK_CONTROL:
			r->d[0] = DiskControl(r->a[0], r->a[1]);
			break;
		case OP_DISK_STATUS:
			r->d[0] = DiskStatus(r->a[0], r->a[1]);
			break;

		case OP_CDROM_OPEN:			// CD-ROM driver functions
			r->d[0] = CDROMOpen(r->a[0], r->a[1]);
			break;
		case OP_CDROM_PRIME:
			r->d[0] = CDROMPrime(r->a[0], r->a[1]);
			break;
		case OP_CDROM_CONTROL:
			r->d[0] = CDROMControl(r->a[0], r->a[1]);
			break;
		case OP_CDROM_STATUS:
			r->d[0] = CDROMStatus(r->a[0], r->a[1]);
			break;

		case OP_AUDIO_DISPATCH:		// Audio component functions
			r->d[0] = AudioDispatch(r->a[3], r->a[4]);
			break;

		case OP_SHEEPBLASTER_TICK:	// SheepBlaster Time Manager task
			r->d[0] = AudioSheepBlasterTick(&r->a[0]);
			break;

		case OP_SHEEPBLASTER: {		// AWACS `link a6,#0`, then the original body
			uint32 sp = r->a[7];
			if (sp >= 0x1000 && sp + 12 < RAMSize) {
				uint32 params = ReadMacInt32(sp + 8);
				if (params >= 0x1000 && params + 32 < RAMSize) {
					int16 sel = (int16)ReadMacInt16(params + 2);
					static int n_sniff;
					if (n_sniff < 24) {
						n_sniff++;
						printf("NW-BOOT SheepBlaster debug sniff #%d pc=%08x sp=%08x sel=%d s0=%08x s4=%08x s8=%08x s12=%08x\n",
						       n_sniff, (unsigned)pc, (unsigned)sp, (int)sel,
						       (unsigned)ReadMacInt32(sp),
						       (unsigned)ReadMacInt32(sp + 4),
						       (unsigned)ReadMacInt32(sp + 8),
						       (unsigned)ReadMacInt32(sp + 12));
						fflush(stdout);
					}
					if (sel == kSoundComponentPlaySourceBufferSelect) {
						uint32 pb = ReadMacInt32(params + 8);
						if (pb == 0)
							pb = ReadMacInt32(params + 4);
						if (pb >= 0x1000 && pb + 32 < RAMSize) {
							uint32 rec = ReadMacInt32(pb);
							uint32 frames, buf;
							if (rec >= 32 && rec < 512) {
								frames = ReadMacInt32(pb + 4 + scd_sampleCount);
								buf = ReadMacInt32(pb + 4 + scd_buffer);
							} else {
								frames = ReadMacInt32(pb + scd_sampleCount);
								buf = ReadMacInt32(pb + scd_buffer);
							}
							if (buf >= 0x1000 && frames != 0 && frames <= 16384 &&
							    buf + frames * 4 < RAMSize) {
								nw_sheepblaster_enable(1);
								nw_sheepblaster_submit(Mac2HostAddr(buf), frames);
							}
						}
					}
				}
			}
			sp -= 4;
			WriteMacInt32(sp, r->a[6]);
			r->a[6] = sp;
			r->a[7] = sp;
			break;
		}

		case OP_SOUNDIN_OPEN:		// Sound input driver functions
			r->d[0] = SoundInOpen(r->a[0], r->a[1]);
			break;
		case OP_SOUNDIN_PRIME:
			r->d[0] = SoundInPrime(r->a[0], r->a[1]);
			break;
		case OP_SOUNDIN_CONTROL:
			r->d[0] = SoundInControl(r->a[0], r->a[1]);
			break;
		case OP_SOUNDIN_STATUS:
			r->d[0] = SoundInStatus(r->a[0], r->a[1]);
			break;
		case OP_SOUNDIN_CLOSE:
			r->d[0] = SoundInClose(r->a[0], r->a[1]);
			break;

		case OP_ADBOP:				// ADBOp() replacement
			ADBOp(r->d[0], Mac2HostAddr(ReadMacInt32(r->a[0])));
			break;

		case OP_INSTIME:			// InsTime() replacement
			r->d[0] = InsTime(r->a[0], r->d[1]);
			break;
		case OP_RMVTIME:			// RmvTime() replacement
			r->d[0] = RmvTime(r->a[0]);
			break;
		case OP_PRIMETIME:			// PrimeTime() replacement
			r->d[0] = PrimeTime(r->a[0], r->d[0]);
			break;

		case OP_MICROSECONDS:		// Microseconds() replacement
			Microseconds(r->a[0], r->d[0]);
			break;

		case OP_ZERO_SCRAP:			// ZeroScrap() patch
			ZeroScrap();
			break;

		case OP_PUT_SCRAP:			// PutScrap() patch
			PutScrap(ReadMacInt32(r->a[7] + 8), Mac2HostAddr(ReadMacInt32(r->a[7] + 4)), ReadMacInt32(r->a[7] + 12));
			break;

		case OP_GET_SCRAP:			// GetScrap() patch
			GetScrap((void **)Mac2HostAddr(ReadMacInt32(r->a[7] + 4)), ReadMacInt32(r->a[7] + 8), ReadMacInt32(r->a[7] + 12));
			break;

		case OP_DEBUG_STR:			// DebugStr() shows warning message
			if (PrefsFindBool("nogui")) {
				uint8 *pstr = Mac2HostAddr(ReadMacInt32(r->a[7] + 4));
				char str[256];
				int i;
				for (i=0; i<pstr[0]; i++)
					str[i] = pstr[i+1];
				str[i] = 0;
				WarningAlert(str);
			}
			break;

		case OP_INSTALL_DRIVERS: {	// Patch to install our own drivers during startup
			if (ROMType == ROMTYPE_NEWWORLD) {
				/* Hybrid: the opcode replaced `lea -$32(a7),a7` at the
				 * start of the ROM's own driver-install routine, which
				 * continues after us (nw_patch_68k_drivers). */
				nw_install_drivers();
				/* CFM is up here (the ROM has been through CFMDispatch
				 * before the unit table). Resolve the InterfaceLib TVECTs
				 * host-side calls into the guest use from native mode
				 * (GetSharedLibrary/FindSymbol/NewPtrSys/CallUniversalProc):
				 * the video ndrv's DoDriverIO needs them. */
				InitCallUniversalProc();
				r->a[7] -= 0x32;
				break;
			}
			// Install drivers
			InstallDrivers();

			// Patch MakeExecutable()
			MakeExecutableTvec = FindLibSymbol("\023PrivateInterfaceLib", "\016MakeExecutable");
			D(bug("MakeExecutable TVECT at %08x\n", MakeExecutableTvec));
			WriteMacInt32(MakeExecutableTvec, NativeFunction(NATIVE_MAKE_EXECUTABLE));
#if !EMULATED_PPC
			WriteMacInt32(MakeExecutableTvec + 4, (uint32)TOC);
#endif

			// Patch DebugStr()
			static const uint8 proc_template[] = {
				M68K_EMUL_OP_DEBUG_STR >> 8, M68K_EMUL_OP_DEBUG_STR & 0xFF,
				0x4e, 0x74,			// rtd	#4
				0x00, 0x04
			};
			BUILD_SHEEPSHAVER_PROCEDURE(proc);
			WriteMacInt32(0x1dfc, proc);
			break;
		}

		case OP_NAME_REGISTRY:		// Patch Name Registry and initialize CallUniversalProc
			r->d[0] = (uint32)-1;
			PatchNameRegistry();
			InitCallUniversalProc();
			break;

		case OP_RESET:				// Early in MacOS reset
			D(bug("*** RESET ***\n"));
			tick_inhibit = true;
			CDROMRemount(); // for System 7.x
			TimerReset();
			MacOSUtilReset();
			EtherResetCachedAllocation();
			ether_reset();
			AudioReset();
#ifdef USE_SDL_AUDIO
			PlayStartupSound();
#endif
			// Enable DR emulator (disabled for now)
			if (PrefsFindBool("jit68k") && 0) {
				D(bug("DR activated\n"));
				WriteMacInt32(KernelDataAddr + 0x17a0, 3);		// Prepare for DR emulator activation
				WriteMacInt32(KernelDataAddr + 0x17c0, DR_CACHE_BASE);
				WriteMacInt32(KernelDataAddr + 0x17c4, DR_CACHE_SIZE);
				WriteMacInt32(KernelDataAddr + 0x1b04, DR_CACHE_BASE);
				WriteMacInt32(KernelDataAddr + 0x1b00, DR_EMULATOR_BASE);
				memcpy((void *)DR_EMULATOR_BASE, (void *)(ROMBase + 0x370000), DR_EMULATOR_SIZE);
				MakeExecutable(0, DR_EMULATOR_BASE, DR_EMULATOR_SIZE);
			}
			tick_inhibit = false;
			break;

		case OP_IRQ:			// Level 1 interrupt
			WriteMacInt16(ReadMacInt32(KernelDataAddr + 0x67c), 0);	// Clear interrupt
			r->d[0] = 0;
			if (HasMacStarted()) {
				if (InterruptFlags & INTFLAG_VIA) {
					ClearInterruptFlag(INTFLAG_VIA);
#if !PRECISE_TIMING
					TimerInterrupt();
#endif
					ExecuteNative(NATIVE_VIDEO_VBL);

					static int tick_counter = 0;
					if (++tick_counter >= 60) {
						tick_counter = 0;
						SonyInterrupt();
						DiskInterrupt();
						CDROMInterrupt();
					}

					r->d[0] = 1;		// Flag: 68k interrupt routine executes VBLTasks etc.
				}
				if (InterruptFlags & INTFLAG_SERIAL) {
					ClearInterruptFlag(INTFLAG_SERIAL);
					SerialInterrupt();
				}
				if (InterruptFlags & INTFLAG_ETHER) {
					ClearInterruptFlag(INTFLAG_ETHER);
					ExecuteNative(NATIVE_ETHER_IRQ);
				}
				if (InterruptFlags & INTFLAG_TIMER) {
					ClearInterruptFlag(INTFLAG_TIMER);
					TimerInterrupt();
				}
				if (InterruptFlags & INTFLAG_AUDIO) {
					ClearInterruptFlag(INTFLAG_AUDIO);
					AudioInterrupt();
				}
				if (InterruptFlags & INTFLAG_ADB) {
					ClearInterruptFlag(INTFLAG_ADB);
					ADBInterrupt();
				}
			} else
				r->d[0] = 1;
			break;

		case OP_SCSI_DISPATCH: {	// SCSIDispatch() replacement
			uint32 ret = ReadMacInt32(r->a[7]);
			uint16 sel = ReadMacInt16(r->a[7] + 4);
			r->a[7] += 6;
//			D(bug("SCSIDispatch(%d)\n", sel));
			int stack;
			switch (sel) {
				case 0:		// SCSIReset
					WriteMacInt16(r->a[7], SCSIReset());
					stack = 0;
					break;
				case 1:		// SCSIGet
					WriteMacInt16(r->a[7], SCSIGet());
					stack = 0;
					break;
				case 2:		// SCSISelect
				case 11:	// SCSISelAtn
					WriteMacInt16(r->a[7] + 2, SCSISelect(ReadMacInt8(r->a[7] + 1)));
					stack = 2;
					break;
				case 3:		// SCSICmd
					WriteMacInt16(r->a[7] + 6, SCSICmd(ReadMacInt16(r->a[7]), Mac2HostAddr(ReadMacInt32(r->a[7] + 2))));
					stack = 6;
					break;
				case 4:		// SCSIComplete
					WriteMacInt16(r->a[7] + 12, SCSIComplete(ReadMacInt32(r->a[7]), ReadMacInt32(r->a[7] + 4), ReadMacInt32(r->a[7] + 8)));
					stack = 12;
					break;
				case 5:		// SCSIRead
				case 8:		// SCSIRBlind
					WriteMacInt16(r->a[7] + 4, SCSIRead(ReadMacInt32(r->a[7])));
					stack = 4;
					break;
				case 6:		// SCSIWrite
				case 9:		// SCSIWBlind
					WriteMacInt16(r->a[7] + 4, SCSIWrite(ReadMacInt32(r->a[7])));
					stack = 4;
					break;
				case 10:	// SCSIStat
					WriteMacInt16(r->a[7], SCSIStat());
					stack = 0;
					break;
				case 12:	// SCSIMsgIn
					WriteMacInt16(r->a[7] + 4, 0);
					stack = 4;
					break;
				case 13:	// SCSIMsgOut
					WriteMacInt16(r->a[7] + 2, 0);
					stack = 2;
					break;
				case 14:	// SCSIMgrBusy
					WriteMacInt16(r->a[7], SCSIMgrBusy());
					stack = 0;
					break;
				default:
					printf("FATAL: SCSIDispatch: illegal selector\n");
					stack = 0;
					//!! SysError(12)
			}
			r->a[0] = ret;
			r->a[7] += stack;
			break;
		}

		case OP_SCSI_ATOMIC:		// SCSIAtomic() replacement
			D(bug("SCSIAtomic\n"));
			r->d[0] = (uint32)-7887;
			break;

		case OP_CHECK_SYSV: {		// Check we are not using MacOS < 8.1 with a NewWorld ROM
			r->a[1] = r->d[1];
			r->a[0] = ReadMacInt32(r->d[1]);
			uint32 sysv = ReadMacInt16(r->a[0]);
			D(bug("Detected MacOS version %d.%d.%d\n", (sysv >> 8) & 0xf, (sysv >> 4) & 0xf, sysv & 0xf));
			if (ROMType == ROMTYPE_NEWWORLD && sysv < 0x0801)
				r->d[1] = 0;
			break;
		}

		case OP_NTRB_17_PATCH:
			r->a[2] = ReadMacInt32(r->a[7]);
			r->a[7] += 4;
			if (ReadMacInt16(r->a[2] + 6) == 17)
				PatchNativeResourceManager();
			break;

		case OP_NTRB_17_PATCH2:
			r->a[7] += 8;
			PatchNativeResourceManager();
			break;

		case OP_NTRB_17_PATCH3:
			r->a[2] = ReadMacInt32(r->a[7]);
			r->a[7] += 4;
		 	D(bug("%d %d\n", ReadMacInt16(r->a[2]), ReadMacInt16(r->a[2] + 6)));
			if (ReadMacInt16(r->a[2]) == 11 && ReadMacInt16(r->a[2] + 6) == 17)
				PatchNativeResourceManager();
			break;

		case OP_NTRB_17_PATCH4:
			r->d[0] = ReadMacInt16(r->a[7]);
			r->a[7] += 2;
		 	D(bug("%d %d\n", ReadMacInt16(r->a[2]), ReadMacInt16(r->a[2] + 6)));
			if (ReadMacInt16(r->a[2]) == 11 && ReadMacInt16(r->a[2] + 6) == 17)
				PatchNativeResourceManager();
			break;

		case OP_CHECKLOAD: {		// vCheckLoad() patch
			uint32 type = ReadMacInt32(r->a[7]);
			r->a[7] += 4;
			int16 id = ReadMacInt16(r->a[2]);
			if (r->a[0] == 0)
				break;
			uint32 adr = ReadMacInt32(r->a[0]);
			if (adr == 0)
				break;
			uint16 *p = (uint16 *)Mac2HostAddr(adr);
			uint32 size = ReadMacInt32(adr - 8) & 0xffffff;
			CheckLoad(type, id, p, size);
			break;
		}

		case OP_EXTFS_COMM:			// External file system routines
			WriteMacInt16(r->a[7] + 14, ExtFSComm(ReadMacInt16(r->a[7] + 12), ReadMacInt32(r->a[7] + 8), ReadMacInt32(r->a[7] + 4)));
			break;

		case OP_EXTFS_HFS:
			WriteMacInt16(r->a[7] + 20, ExtFSHFS(ReadMacInt32(r->a[7] + 16), ReadMacInt16(r->a[7] + 14), ReadMacInt32(r->a[7] + 10), ReadMacInt32(r->a[7] + 6), ReadMacInt16(r->a[7] + 4)));
			break;

		case OP_IDLE_TIME:
			// Sleep if no events pending
			if (ReadMacInt32(0x14c) == 0)
				idle_wait();
			r->a[0] = ReadMacInt32(0x2b6);
			break;

		case OP_IDLE_TIME_2:
			// Sleep if no events pending
			if (ReadMacInt32(0x14c) == 0)
				idle_wait();
			r->d[0] = (uint32)-2;
			break;

		case OP_COPYBITS_EXPAND:
			r->d[0] = (uint32)NQD_copybits_expand(
				ReadMacInt32(r->a[7] + 4),
				ReadMacInt32(r->a[7] + 8),
				ReadMacInt32(r->a[7] + 12),
				ReadMacInt32(r->a[7] + 16),
				(int16)ReadMacInt16(r->a[7] + 20),
				ReadMacInt32(r->a[7] + 22));
			break;

		default:
			printf("FATAL: EMUL_OP called with bogus selector %08x\n", selector);
			QuitEmulator();
			break;
	}
}
