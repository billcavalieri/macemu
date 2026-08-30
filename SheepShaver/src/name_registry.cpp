/*
 *  name_registry.cpp - Name Registry handling
 *
 *  SheepShaver (C) Christian Bauer and Marc Hellwig
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
#include "name_registry.h"
#include "main.h"
#include "macos_util.h"
#include "user_strings.h"
#include "emul_op.h"
#include "thunks.h"
#include "rom_patches.h"
#include "cpu_emulation.h"
#include "nw_boot_contract.h"
#include "xpram.h"

#define DEBUG 0
#include "debug.h"


// Function pointers
typedef int16 (*rcec_ptr)(const RegEntryID *, const char *, RegEntryID *);
static uint32 rcec_tvect = 0;
static inline int16 RegistryCStrEntryCreate(uintptr arg1, const char *arg2, uint32 arg3)
{
	SheepString arg2str(arg2);
	return (int16)CallMacOS3(rcec_ptr, rcec_tvect, (const RegEntryID *)arg1, arg2str.addr(), arg3);
}
typedef int16 (*rpc_ptr)(const RegEntryID *, const char *, const void *, uint32);
static uint32 rpc_tvect = 0;
static inline int16 RegistryPropertyCreate(uintptr arg1, const char *arg2, uintptr arg3, uint32 arg4)
{
	SheepString arg2str(arg2);
	return (int16)CallMacOS4(rpc_ptr, rpc_tvect, (const RegEntryID *)arg1, arg2str.addr(), (const void *)arg3, arg4);
}
static inline int16 RegistryPropertyCreateStr(uintptr arg1, const char *arg2, const char *arg3)
{
	SheepString arg3str(arg3);
	return RegistryPropertyCreate(arg1, arg2, arg3str.addr(), strlen(arg3) + 1);
}

// Video driver stub
static const uint8 video_driver[] = {
#include "VideoDriverStub.i"
};

// Ethernet driver stub
static const uint8 ethernet_driver[] = {
#ifdef USE_ETHER_FULL_DRIVER
#include "EthernetDriverFull.i"
#else
#include "EthernetDriverStub.i"
#endif
};

// Helper for RegEntryID
typedef SheepArray<sizeof(RegEntryID)> SheepRegEntryID;

// Helper for a <uint32, uint32> pair
struct SheepPair : public SheepArray<8> {
	SheepPair(uint32 base, uint32 size) : SheepArray<8>()
		{ WriteMacInt32(addr(), base); WriteMacInt32(addr() + 4, size); }
};

static const char *cpu_node_name_for_pvr(void)
{
	switch (PVR >> 16) {
		case 1:		return "PowerPC,601";
		case 3:		return "PowerPC,603";
		case 4:		return "PowerPC,604";
		case 6:		return "PowerPC,603e";
		case 7:		return "PowerPC,603ev";
		case 8:		return "PowerPC,750";
		case 9:		return "PowerPC,604e";
		case 10:	return "PowerPC,604ev";
		case 50:	return "PowerPC,821";
		case 80:	return "PowerPC,860";
		case 12:
		case 0x800c:
		case 0x8000:
		case 0x8001:
		case 0x8002:
			return "PowerPC,G4";
		default:
			return "PowerPC,???";
	}
}

static void FillCPUProperties(uint32 cpu_entry)
{
	SheepVar32 u32;
	u32.set_value(CPUClockSpeed);
	RegistryPropertyCreate(cpu_entry, "clock-frequency", u32.addr(), 4);
	u32.set_value(BusClockSpeed);
	RegistryPropertyCreate(cpu_entry, "bus-frequency", u32.addr(), 4);
	u32.set_value(TimebaseSpeed);
	RegistryPropertyCreate(cpu_entry, "timebase-frequency", u32.addr(), 4);
	u32.set_value(PVR);
	RegistryPropertyCreate(cpu_entry, "cpu-version", u32.addr(), 4);
	RegistryPropertyCreateStr(cpu_entry, "device_type", "cpu");
	switch (PVR >> 16) {
		case 1:		// 601
			u32.set_value(64);
			RegistryPropertyCreate(cpu_entry, "d-cache-block-size", u32.addr(), 4);
			u32.set_value(128);
			RegistryPropertyCreate(cpu_entry, "d-cache-sets", u32.addr(), 4);
			u32.set_value(0x8000);
			RegistryPropertyCreate(cpu_entry, "d-cache-size", u32.addr(), 4);
			u32.set_value(64);
			RegistryPropertyCreate(cpu_entry, "i-cache-block-size", u32.addr(), 4);
			u32.set_value(128);
			RegistryPropertyCreate(cpu_entry, "i-cache-sets", u32.addr(), 4);
			u32.set_value(0x8000);
			RegistryPropertyCreate(cpu_entry, "i-cache-size", u32.addr(), 4);
			u32.set_value(128);
			RegistryPropertyCreate(cpu_entry, "tlb-sets", u32.addr(), 4);
			u32.set_value(256);
			RegistryPropertyCreate(cpu_entry, "tlb-size", u32.addr(), 4);
			break;
		case 3:		// 603
			u32.set_value(32);
			RegistryPropertyCreate(cpu_entry, "d-cache-block-size", u32.addr(), 4);
			u32.set_value(64);
			RegistryPropertyCreate(cpu_entry, "d-cache-sets", u32.addr(), 4);
			u32.set_value(0x2000);
			RegistryPropertyCreate(cpu_entry, "d-cache-size", u32.addr(), 4);
			u32.set_value(32);
			RegistryPropertyCreate(cpu_entry, "i-cache-block-size", u32.addr(), 4);
			u32.set_value(64);
			RegistryPropertyCreate(cpu_entry, "i-cache-sets", u32.addr(), 4);
			u32.set_value(0x2000);
			RegistryPropertyCreate(cpu_entry, "i-cache-size", u32.addr(), 4);
			u32.set_value(32);
			RegistryPropertyCreate(cpu_entry, "tlb-sets", u32.addr(), 4);
			u32.set_value(64);
			RegistryPropertyCreate(cpu_entry, "tlb-size", u32.addr(), 4);
			break;
		case 4:		// 604
			u32.set_value(32);
			RegistryPropertyCreate(cpu_entry, "d-cache-block-size", u32.addr(), 4);
			u32.set_value(128);
			RegistryPropertyCreate(cpu_entry, "d-cache-sets", u32.addr(), 4);
			u32.set_value(0x4000);
			RegistryPropertyCreate(cpu_entry, "d-cache-size", u32.addr(), 4);
			u32.set_value(32);
			RegistryPropertyCreate(cpu_entry, "i-cache-block-size", u32.addr(), 4);
			u32.set_value(128);
			RegistryPropertyCreate(cpu_entry, "i-cache-sets", u32.addr(), 4);
			u32.set_value(0x4000);
			RegistryPropertyCreate(cpu_entry, "i-cache-size", u32.addr(), 4);
			u32.set_value(64);
			RegistryPropertyCreate(cpu_entry, "tlb-sets", u32.addr(), 4);
			u32.set_value(128);
			RegistryPropertyCreate(cpu_entry, "tlb-size", u32.addr(), 4);
			break;
		case 6:		// 603e
		case 7:		// 603ev
			u32.set_value(32);
			RegistryPropertyCreate(cpu_entry, "d-cache-block-size", u32.addr(), 4);
			u32.set_value(128);
			RegistryPropertyCreate(cpu_entry, "d-cache-sets", u32.addr(), 4);
			u32.set_value(0x4000);
			RegistryPropertyCreate(cpu_entry, "d-cache-size", u32.addr(), 4);
			u32.set_value(32);
			RegistryPropertyCreate(cpu_entry, "i-cache-block-size", u32.addr(), 4);
			u32.set_value(128);
			RegistryPropertyCreate(cpu_entry, "i-cache-sets", u32.addr(), 4);
			u32.set_value(0x4000);
			RegistryPropertyCreate(cpu_entry, "i-cache-size", u32.addr(), 4);
			u32.set_value(32);
			RegistryPropertyCreate(cpu_entry, "tlb-sets", u32.addr(), 4);
			u32.set_value(64);
			RegistryPropertyCreate(cpu_entry, "tlb-size", u32.addr(), 4);
			break;
		case 8:		// 750, 750FX
		case 0x7000:
			u32.set_value(32);
			RegistryPropertyCreate(cpu_entry, "d-cache-block-size", u32.addr(), 4);
			u32.set_value(256);
			RegistryPropertyCreate(cpu_entry, "d-cache-sets", u32.addr(), 4);
			u32.set_value(0x8000);
			RegistryPropertyCreate(cpu_entry, "d-cache-size", u32.addr(), 4);
			u32.set_value(32);
			RegistryPropertyCreate(cpu_entry, "i-cache-block-size", u32.addr(), 4);
			u32.set_value(256);
			RegistryPropertyCreate(cpu_entry, "i-cache-sets", u32.addr(), 4);
			u32.set_value(0x8000);
			RegistryPropertyCreate(cpu_entry, "i-cache-size", u32.addr(), 4);
			u32.set_value(64);
			RegistryPropertyCreate(cpu_entry, "tlb-sets", u32.addr(), 4);
			u32.set_value(128);
			RegistryPropertyCreate(cpu_entry, "tlb-size", u32.addr(), 4);
			break;
		case 9:		// 604e
		case 10:	// 604ev5
			u32.set_value(32);
			RegistryPropertyCreate(cpu_entry, "d-cache-block-size", u32.addr(), 4);
			u32.set_value(256);
			RegistryPropertyCreate(cpu_entry, "d-cache-sets", u32.addr(), 4);
			u32.set_value(0x8000);
			RegistryPropertyCreate(cpu_entry, "d-cache-size", u32.addr(), 4);
			u32.set_value(32);
			RegistryPropertyCreate(cpu_entry, "i-cache-block-size", u32.addr(), 4);
			u32.set_value(256);
			RegistryPropertyCreate(cpu_entry, "i-cache-sets", u32.addr(), 4);
			u32.set_value(0x8000);
			RegistryPropertyCreate(cpu_entry, "i-cache-size", u32.addr(), 4);
			u32.set_value(64);
			RegistryPropertyCreate(cpu_entry, "tlb-sets", u32.addr(), 4);
			u32.set_value(128);
			RegistryPropertyCreate(cpu_entry, "tlb-size", u32.addr(), 4);
			break;
		case 12:	// 7400, 7410, 7450, 7455, 7457
		case 0x800c:
		case 0x8000:
		case 0x8001:
		case 0x8002:
			u32.set_value(32);
			RegistryPropertyCreate(cpu_entry, "d-cache-block-size", u32.addr(), 4);
			u32.set_value(128);
			RegistryPropertyCreate(cpu_entry, "d-cache-sets", u32.addr(), 4);
			u32.set_value(0x8000);
			RegistryPropertyCreate(cpu_entry, "d-cache-size", u32.addr(), 4);
			u32.set_value(32);
			RegistryPropertyCreate(cpu_entry, "i-cache-block-size", u32.addr(), 4);
			u32.set_value(128);
			RegistryPropertyCreate(cpu_entry, "i-cache-sets", u32.addr(), 4);
			u32.set_value(0x8000);
			RegistryPropertyCreate(cpu_entry, "i-cache-size", u32.addr(), 4);
			u32.set_value(64);
			RegistryPropertyCreate(cpu_entry, "tlb-sets", u32.addr(), 4);
			u32.set_value(128);
			RegistryPropertyCreate(cpu_entry, "tlb-size", u32.addr(), 4);
			break;
		case 0x39:	// 970
			u32.set_value(128);
			RegistryPropertyCreate(cpu_entry, "d-cache-block-size", u32.addr(), 4);
			u32.set_value(128);
			RegistryPropertyCreate(cpu_entry, "d-cache-sets", u32.addr(), 4);
			u32.set_value(0x8000);
			RegistryPropertyCreate(cpu_entry, "d-cache-size", u32.addr(), 4);
			u32.set_value(128);
			RegistryPropertyCreate(cpu_entry, "i-cache-block-size", u32.addr(), 4);
			u32.set_value(512);
			RegistryPropertyCreate(cpu_entry, "i-cache-sets", u32.addr(), 4);
			u32.set_value(0x10000);
			RegistryPropertyCreate(cpu_entry, "i-cache-size", u32.addr(), 4);
			u32.set_value(256);
			RegistryPropertyCreate(cpu_entry, "tlb-sets", u32.addr(), 4);
			u32.set_value(0x1000);
			RegistryPropertyCreate(cpu_entry, "tlb-size", u32.addr(), 4);
			break;
		default:
			break;
	}
	u32.set_value(32);
	RegistryPropertyCreate(cpu_entry, "reservation-granularity", u32.addr(), 4);
	SheepPair reg(0, 0);
	RegistryPropertyCreate(cpu_entry, "reg", reg.addr(), 8);
}

/*
 *  New World OF / Name Registry tree 9.2.1 will accept (G1).
 *  Nodes only — no chipset. Root compatible is MacRISC2; Gestalt is 406.
 */
static void PatchNewWorldNameRegistry(uint32 device_tree)
{
	SheepVar32 u32;
	u32.set_value(1);
	RegistryPropertyCreate(device_tree, "#address-cells", u32.addr(), 4);
	u32.set_value(1);
	RegistryPropertyCreate(device_tree, "#size-cells", u32.addr(), 4);
	u32.set_value(BusClockSpeed);
	RegistryPropertyCreate(device_tree, "clock-frequency", u32.addr(), 4);
	RegistryPropertyCreateStr(device_tree, "model", nw_root_model());
	RegistryPropertyCreateStr(device_tree, "compatible", nw_root_compatible());

	SheepRegEntryID cpus;
	if (!RegistryCStrEntryCreate(device_tree, "cpus", cpus.addr())) {
		u32.set_value(1);
		RegistryPropertyCreate(cpus.addr(), "#address-cells", u32.addr(), 4);
		u32.set_value(0);
		RegistryPropertyCreate(cpus.addr(), "#size-cells", u32.addr(), 4);
		char cpu_name[40];
		snprintf(cpu_name, sizeof(cpu_name), "%s@0", cpu_node_name_for_pvr());
		SheepRegEntryID cpu;
		if (!RegistryCStrEntryCreate(cpus.addr(), cpu_name, cpu.addr()))
			FillCPUProperties(cpu.addr());
	}

	SheepRegEntryID memory;
	if (!RegistryCStrEntryCreate(device_tree, "memory", memory.addr())) {
		SheepPair reg(RAMBase, RAMSize);
		RegistryPropertyCreateStr(memory.addr(), "device_type", "memory");
		RegistryPropertyCreate(memory.addr(), "reg", reg.addr(), 8);
	}

	SheepRegEntryID chosen;
	if (!RegistryCStrEntryCreate(device_tree, "chosen", chosen.addr())) {
		RegistryPropertyCreateStr(chosen.addr(), "bootargs", "");
	}

	SheepRegEntryID uni_n;
	if (!RegistryCStrEntryCreate(device_tree, "uni-n", uni_n.addr())) {
		RegistryPropertyCreateStr(uni_n.addr(), "device_type", "uni-n");
		RegistryPropertyCreateStr(uni_n.addr(), "compatible", "uni-n");
		SheepPair reg(0xf8000000, 0x01000000);
		RegistryPropertyCreate(uni_n.addr(), "reg", reg.addr(), 8);

		SheepRegEntryID pci;
		if (!RegistryCStrEntryCreate(uni_n.addr(), "pci", pci.addr())) {
			RegistryPropertyCreateStr(pci.addr(), "device_type", "pci");
			RegistryPropertyCreateStr(pci.addr(), "compatible", "uni-north");

			SheepRegEntryID video;
			if (!RegistryCStrEntryCreate(pci.addr(), "video", video.addr())) {
				RegistryPropertyCreateStr(video.addr(), "AAPL,connector", "monitor");
				RegistryPropertyCreateStr(video.addr(), "device_type", "display");
				SheepArray<sizeof(video_driver)> the_video_driver;
				Host2Mac_memcpy(the_video_driver.addr(), video_driver, sizeof(video_driver));
				RegistryPropertyCreate(video.addr(), "driver,AAPL,MacOS,PowerPC",
				                       the_video_driver.addr(), sizeof(video_driver));
				RegistryPropertyCreateStr(video.addr(), "model", "SheepShaver Video");
			}

			SheepRegEntryID mac_io;
			if (!RegistryCStrEntryCreate(pci.addr(), "mac-io", mac_io.addr())) {
				RegistryPropertyCreateStr(mac_io.addr(), "device_type", "mac-io");
				RegistryPropertyCreateStr(mac_io.addr(), "compatible", "mac-io");

				SheepRegEntryID via_cuda;
				if (!RegistryCStrEntryCreate(mac_io.addr(), "via-cuda", via_cuda.addr())) {
					RegistryPropertyCreateStr(via_cuda.addr(), "device_type", "via-cuda");
					RegistryPropertyCreateStr(via_cuda.addr(), "compatible", "cuda");

					SheepRegEntryID adb;
					if (!RegistryCStrEntryCreate(via_cuda.addr(), "adb", adb.addr()))
						RegistryPropertyCreateStr(adb.addr(), "device_type", "adb");

					SheepRegEntryID nvram;
					if (!RegistryCStrEntryCreate(via_cuda.addr(), "nvram", nvram.addr())) {
						RegistryPropertyCreateStr(nvram.addr(), "device_type", "nvram");
						u32.set_value(XPRAM_SIZE);
						RegistryPropertyCreate(nvram.addr(), "#bytes", u32.addr(), 4);
					}
				}
			}
		}
	}
}

/*
 *  Patch Name Registry during startup
 */

void DoPatchNameRegistry(void)
{
	SheepVar32 u32;
	D(bug("Patching Name Registry..."));
#if NW_BOOT_LOG
	nw_boot_log("G3: DoPatchNameRegistry");
#endif

	// Create "device-tree"
	SheepRegEntryID device_tree;
	if (!RegistryCStrEntryCreate(0, "Devices:device-tree", device_tree.addr())) {
		if (ROMType == ROMTYPE_NEWWORLD) {
			PatchNewWorldNameRegistry(device_tree.addr());
			nw_log_g1_tree();
			D(bug("done (New World MacRISC2).\n"));
			return;
		}

		u32.set_value(BusClockSpeed);
		RegistryPropertyCreate(device_tree.addr(), "clock-frequency", u32.addr(), 4);
		RegistryPropertyCreateStr(device_tree.addr(), "model", "Power Macintosh");

		// Create "AAPL,ROM"
		SheepRegEntryID aapl_rom;
		if (!RegistryCStrEntryCreate(device_tree.addr(), "AAPL,ROM", aapl_rom.addr())) {
			RegistryPropertyCreateStr(aapl_rom.addr(), "device_type", "rom");
			SheepPair reg(ROMBase, ROM_SIZE);
			RegistryPropertyCreate(aapl_rom.addr(), "reg", reg.addr(), 8);
		}

		// Create "PowerPC,60x"
		SheepRegEntryID power_pc;
		const char *str = cpu_node_name_for_pvr();
		if (!RegistryCStrEntryCreate(device_tree.addr(), str, power_pc.addr()))
			FillCPUProperties(power_pc.addr());

		// Create "memory"
		SheepRegEntryID memory;
		if (!RegistryCStrEntryCreate(device_tree.addr(), "memory", memory.addr())) {
			SheepPair reg(RAMBase, RAMSize);
			RegistryPropertyCreateStr(memory.addr(), "device_type", "memory");
			RegistryPropertyCreate(memory.addr(), "reg", reg.addr(), 8);
		}

		// Create "video"
		SheepRegEntryID video;
		if (!RegistryCStrEntryCreate(device_tree.addr(), "video", video.addr())) {
			RegistryPropertyCreateStr(video.addr(), "AAPL,connector", "monitor");
			RegistryPropertyCreateStr(video.addr(), "device_type", "display");
			SheepArray<sizeof(video_driver)> the_video_driver;
			Host2Mac_memcpy(the_video_driver.addr(), video_driver, sizeof(video_driver));
			RegistryPropertyCreate(video.addr(), "driver,AAPL,MacOS,PowerPC", the_video_driver.addr(), sizeof(video_driver));
			RegistryPropertyCreateStr(video.addr(), "model", "SheepShaver Video");
		}

		// Create "ethernet"
		SheepRegEntryID ethernet;
		if (!RegistryCStrEntryCreate(device_tree.addr(), "ethernet", ethernet.addr())) {
			RegistryPropertyCreateStr(ethernet.addr(), "AAPL,connector", "ethernet");
			RegistryPropertyCreateStr(ethernet.addr(), "device_type", "network");
			SheepArray<sizeof(ethernet_driver)> the_ethernet_driver;
			Host2Mac_memcpy(the_ethernet_driver.addr(), ethernet_driver, sizeof(ethernet_driver));
			RegistryPropertyCreate(ethernet.addr(), "driver,AAPL,MacOS,PowerPC", the_ethernet_driver.addr(), sizeof(ethernet_driver));
			// local-mac-address
			// max-frame-size 2048
		}
	}
	D(bug("done.\n"));
}

void PatchNameRegistry(void)
{
	// Find RegistryCStrEntryCreate() and RegistryPropertyCreate() TVECTs
	rcec_tvect = FindLibSymbol("\017NameRegistryLib", "\027RegistryCStrEntryCreate");
	D(bug("RegistryCStrEntryCreate TVECT at %08x\n", rcec_tvect));
	rpc_tvect = FindLibSymbol("\017NameRegistryLib", "\026RegistryPropertyCreate");
	D(bug("RegistryPropertyCreate TVECT at %08x\n", rpc_tvect));
	if (rcec_tvect == 0 || rpc_tvect == 0) {
		ErrorAlert(GetString(STR_NO_NAME_REGISTRY_ERR));
		QuitEmulator();
	}

	// Main routine must be executed in PPC mode
	ExecuteNative(NATIVE_PATCH_NAME_REGISTRY);
}
