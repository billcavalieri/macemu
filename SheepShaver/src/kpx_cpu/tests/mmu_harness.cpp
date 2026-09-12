/*
 *  mmu_harness.cpp - Host-side SheepShaver-MMUTests (G1 + G2)
 *
 *  SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 *
 *  G1: New World boot contract (tree nodes, Gestalt 406, KDP layout,
 *  Hnfo-or-mtsdr1, BATRangeInit, saveKernelDataPtr adjacency). No ROM.
 *  G2: BAT + synthetic HTAB; HotInts DSI accept (SRR0 lwz HIT).
 */

#include "cpu/ppc/ppc-mmu.hpp"
#include "nw_boot_contract.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <vector>

static int g_pass;
static int g_fail;

#define CHECK(cond) do { \
	if (cond) { \
		g_pass++; \
	} else { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		g_fail++; \
	} \
} while (0)

static void be32_store(uint8_t *mem, uint32_t pa, uint32_t value)
{
	mem[pa + 0] = (uint8_t)(value >> 24);
	mem[pa + 1] = (uint8_t)(value >> 16);
	mem[pa + 2] = (uint8_t)(value >> 8);
	mem[pa + 3] = (uint8_t)value;
}

static uint32_t pte_word0(uint32_t vsid, int h, uint32_t api)
{
	return 0x80000000u | ((vsid & 0x00ffffffu) << 7) |
	       ((uint32_t)h << 6) | (api & 0x3fu);
}

static uint32_t pte_word1(uint32_t rpn)
{
	return (rpn & 0xfffffu) << 12;
}

static uint32_t pteg_addr(uint32_t sdr1, uint32_t hash)
{
	const uint32_t htaborg = sdr1 & 0xffff0000u;
	const uint32_t htabmask = ((sdr1 & 0x1ffu) << 16) | 0xffffu;
	return htaborg | ((hash * 64u) & htabmask);
}

static void program_pte(uint8_t *ram, uint32_t sdr1, uint32_t vsid,
                        uint32_t ea, int hash_id, uint32_t rpn)
{
	const uint32_t page_index = (ea >> 12) & 0xffffu;
	const uint32_t api = (ea >> 22) & 0x3fu;
	const uint32_t hash0 = (vsid & 0x7ffffu) ^ page_index;
	const uint32_t hash = hash_id ? (hash0 ^ 0x7ffffu) : hash0;
	const uint32_t pteg = pteg_addr(sdr1, hash);
	be32_store(ram, pteg, pte_word0(vsid, hash_id, api));
	be32_store(ram, pteg + 4, pte_word1(rpn));
}

int main()
{
	/* ---- G1 New World boot contract (no guest ROM) ---- */
	{
		CHECK(strcmp(nw_root_compatible(), "MacRISC2") == 0);
		CHECK(nw_gestalt_machine_id(1) == 406);
		CHECK(nw_gestalt_machine_id(0) == 0x3020u);
		CHECK(nw_of_tree_has_required_nodes());
		CHECK(NW_KDP_SAVE_KERNEL_DATA_PTR == NW_KDP_SAVE_RETURN_ADDR + 4);
		CHECK(NW_KDP_BAT_RANGE_INIT_LONGS == 32);
		CHECK(NW_NK_V2_OFFSET == 0x310000);
		CHECK(nw_rom_type_is_newworld(5));
		CHECK(!nw_rom_type_is_newworld(0));

		std::vector<uint8_t> rom(NW_ROM_SIZE, 0);
		memcpy(&rom[NW_NEWWORLD_SIG_OFFSET], "NewWorld", 8);
		CHECK(nw_detect_decoded_rom(&rom[0], rom.size()) == NW_DECODED_NEWWORLD);
		memcpy(&rom[NW_NEWWORLD_SIG_OFFSET], "Boot TNT", 8);
		CHECK(nw_detect_decoded_rom(&rom[0], rom.size()) == NW_DECODED_OLDWORLD);

		std::vector<uint8_t> kdp(NW_KDP_PAGE_SIZE, 0);
		nw_kdp_params p;
		memset(&p, 0, sizeof(p));
		p.kdp_ea = 0x68ffe000u;
		p.ram_base = 0;
		p.ram_size = 64u * 1024u * 1024u;
		p.rom_base = 0x40800000u;
		p.htaborg = NW_DEFAULT_HTABORG;
		p.ptegmask = NW_DEFAULT_PTEGMASK;
		p.sdr1 = NW_DEFAULT_SDR1;
		nw_fill_kdp_be(&kdp[0], kdp.size(), &p);
		CHECK(nw_kdp_save_ptrs_adjacent(&kdp[0]));
		CHECK(nw_be32_load(&kdp[0], NW_KDP_SAVE_KERNEL_DATA_PTR) == p.kdp_ea);
		CHECK(nw_kdp_bat_range_init_present(&kdp[0]));
		CHECK(nw_kdp_hnfo_valid_htab(&kdp[0]));
		CHECK(nw_be32_load(&kdp[0], NW_KDP_HNFO_SIGNATURE) == (uint32_t)NW_HNFO_SIGNATURE);

		nw_htab_gate gate;
		gate.hnfo_valid_htab = nw_kdp_hnfo_valid_htab(&kdp[0]);
		gate.spr_log_mtsdr1 = 0;
		CHECK(nw_htab_gate_pass(&gate));

		/* HTAB half of G1: mtsdr1 in the SPR log is enough without Hnfo HTAB. */
		std::vector<uint8_t> empty(NW_KDP_PAGE_SIZE, 0);
		gate.hnfo_valid_htab = nw_kdp_hnfo_valid_htab(&empty[0]);
		gate.spr_log_mtsdr1 = 1;
		CHECK(!gate.hnfo_valid_htab);
		CHECK(nw_htab_gate_pass(&gate));
		gate.spr_log_mtsdr1 = 0;
		CHECK(!nw_htab_gate_pass(&gate));
	}

	/* G1 ConfigInfo page map (Trampoline job): 9.2.1 ROM LA_* defaults. */
	{
		std::vector<uint8_t> ci(NW_CI_SIZE, 0);
		nw_be32_store(&ci[0], 0x9c, 0x5fffe000u);	/* LA_InfoRecord */
		nw_be32_store(&ci[0], 0xa0, 0x68ffe000u);	/* LA_KernelData */
		nw_be32_store(&ci[0], 0xa4, 0x68fff000u);	/* LA_EmulatorData */
		nw_be32_store(&ci[0], 0xa8, 0x68080000u);	/* LA_DispatchTable */
		nw_be32_store(&ci[0], 0xac, 0x68060000u);	/* LA_EmulatorCode */
		nw_be32_store(&ci[0], 0xff4, 4);
		nw_be32_store(&ci[0], 0xff8, 0xffc0002au);
		nw_pmdt_range extra[2];
		extra[0].la = extra[0].pa = 0x50510000u; extra[0].size = 0x80000u;	/* SheepMem */
		extra[1].la = extra[1].pa = 0x69000000u; extra[1].size = 0x80000u;	/* DR cache */
		nw_config_info_layout l;
		l.rom_base = 0x50000000u;
		l.rom_area_size = 0x500000u;
		l.ram_base = 0x10000000u;
		l.ram_size = 0x08000000u;
		l.ci_pa = 0x5030d000u;
		l.bootinfo_pa = NW_BOOTINFO_LA;
		l.extra = extra;
		l.n_extra = 2;
		int n = nw_fill_config_info_be(&ci[0], &l);
		/* 16 terminators + ROM, SheepMem, IRP, boot-info, NK 1 MiB, CI page,
		 * KDP, EDP, DR cache */
		CHECK(n == 16 + 9);
		CHECK(nw_config_info_pagemap_ok(&ci[0]));
		CHECK(nw_be32_load(&ci[0], 0xb8) == (uint32_t)n * 8u);
		CHECK(nw_be32_load(&ci[0], 0xbc) == NW_CI_PAGEMAP_OFF);
		const uint8_t *pm = &ci[NW_CI_PAGEMAP_OFF];
		/* seg 5: ROM (0..0x4ff), SheepMem (0x510..), IRP (0xfffe), term */
		uint32_t s5 = nw_be32_load(&ci[0], 0xcc + 5 * 8);
		CHECK(((pm[s5] << 8) | pm[s5 + 1]) == 0 && ((pm[s5 + 2] << 8) | pm[s5 + 3]) == 0x4ff);
		CHECK(nw_be32_load(pm, s5 + 4) == 0x50000012u);
		CHECK(((pm[s5 + 8] << 8) | pm[s5 + 9]) == 0x510);
		CHECK(((pm[s5 + 16] << 8) | pm[s5 + 17]) == 0xfffe);
		CHECK(nw_be32_load(&ci[0], 0xc0) == s5 + 16);	/* IRP offset */
		CHECK(nw_be32_load(pm, s5 + 28) == 0xa00u);	/* seg 5 terminator */
		/* seg 6: boot-info (0x4000, 384 pages), NK 1 MiB -> ROM+0x300000,
		 * ConfigInfo page RO, KDP, EDP, DR cache, term (a01) */
		uint32_t s6 = nw_be32_load(&ci[0], 0xcc + 6 * 8);
		CHECK(((pm[s6] << 8) | pm[s6 + 1]) == 0x4000 && ((pm[s6 + 2] << 8) | pm[s6 + 3]) == 0x17f);
		CHECK(nw_be32_load(pm, s6 + 4) == 0x64000012u);
		CHECK(((pm[s6 + 8] << 8) | pm[s6 + 9]) == 0x8000 && ((pm[s6 + 10] << 8) | pm[s6 + 11]) == 0xff);
		CHECK(nw_be32_load(pm, s6 + 12) == 0x50300012u);
		CHECK(((pm[s6 + 16] << 8) | pm[s6 + 17]) == 0x8fef && nw_be32_load(pm, s6 + 20) == 0x5030d013u);
		CHECK(nw_be32_load(&ci[0], 0xc4) == s6 + 24);	/* KDP */
		CHECK(nw_be32_load(pm, s6 + 28) == 0x11u);
		CHECK(nw_be32_load(&ci[0], 0xc8) == s6 + 32);	/* EDP */
		CHECK(((pm[s6 + 40] << 8) | pm[s6 + 41]) == 0x9000);
		CHECK(nw_be32_load(pm, s6 + 52) == 0x60000a01u);
		/* SR values, BAT ranges, low memory, version */
		CHECK(nw_be32_load(&ci[0], 0xcc + 6 * 8 + 4) == 0x00600000u);
		CHECK(nw_be32_load(&ci[0], 0x24c + 15 * 8 + 4) == 0x00f00000u);
		CHECK(nw_be32_load(&ci[0], 0x2cc + 8) == 0x500000ffu);
		CHECK(nw_be32_load(&ci[0], 0x2cc + 12) == 0x50000002u);
		/* 68k ROM window: LA 0xffc00000, 4 MiB, RO write-through -> ROM area */
		CHECK(nw_be32_load(&ci[0], 0x2cc + 16) == 0xffc0007fu);
		CHECK(nw_be32_load(&ci[0], 0x2cc + 20) == 0x50000043u);
		CHECK(nw_be32_load(&ci[0], 0x2cc + 24) == 0x6800001fu);
		CHECK(nw_be32_load(&ci[0], 0x2cc + 28) == 0x50300002u);
		CHECK(nw_be32_load(&ci[0], 0x34c) == 0x132f132fu);
		CHECK(nw_be32_load(&ci[0], 0x354) == 0xf3fff3ffu);
		CHECK(nw_be32_load(&ci[0], 0x360) == 0x10000000u);
		/* MacLowMemInit (4 -> 0xffc0002a) moved to 0x3a0; tail tables as golden */
		CHECK(nw_be32_load(&ci[0], 0xb0) == 0x3a0u);
		CHECK(nw_be32_load(&ci[0], 0x3a0) == 4 && nw_be32_load(&ci[0], 0x3a4) == 0xffc0002au &&
		      nw_be32_load(&ci[0], 0x3a8) == 0);
		CHECK(nw_be32_load(&ci[0], 0xff4) == 0xffffffffu);
		CHECK(nw_be32_load(&ci[0], 0xf80) == 0x002f0037u && nw_be32_load(&ci[0], 0xf9c) == 0x001effffu);
		CHECK(nw_be32_load(&ci[0], 0xd00) == 0xffffffffu && nw_be32_load(&ci[0], 0xd40) == 0x02070104u);
		CHECK(nw_be32_load(&ci[0], 0xf00) == 0x02070104u && nw_be32_load(&ci[0], 0xf48) == 0x80540000u);
		CHECK(nw_be32_load(&ci[0], 0) == 0 && nw_be32_load(&ci[0], 0x70) == 0x30202020u);
		CHECK(nw_be32_load(&ci[0], 0x378) == 0x01010000u);
		CHECK(nw_be32_load(&ci[0], 0x54) == 0 && nw_be32_load(&ci[0], 0x44) == 0);

		/* Hardware-info block (r9) and the boot-info record it points at. */
		uint8_t hw[NW_HWINFO_SIZE];
		nw_fill_hwinfo_be(hw, &l);
		CHECK(nw_be32_load(hw, 0x70) == 0x486e666fu);		/* 'Hnfo' */
		CHECK(nw_be32_load(hw, 0x00) == 0x50000000u && nw_be32_load(hw, 0x0c) == 0x5030d000u);
		CHECK(nw_be32_load(hw, 0x08) == 0x64051dd0u && nw_be32_load(hw, 0x04) == 0x6400000cu);
		CHECK(nw_be32_load(hw, 0x10) == 0x68feff80u && nw_be32_load(hw, 0x14) == 0x68feff40u &&
		      nw_be32_load(hw, 0xa8) == 0x68fefcfcu);
		CHECK(((hw[0x76] << 8) | hw[0x77]) == 0x3035);	/* 68k: machine id */
		std::vector<uint8_t> bi(NW_BOOTINFO_SIZE, 0xee);
		nw_fill_bootinfo_be(&bi[0], NW_BOOTINFO_SIZE, &l);
		CHECK(nw_be32_load(&bi[0], 0) == 0x504d5226u && nw_be32_load(&bi[0], 0xc) == 0);
		const uint32_t rec = NW_BOOTINFO_HWREC_OFF;
		CHECK(nw_be32_load(&bi[0], rec) == 0x98u);			/* -> DecoderInfo */
		CHECK(nw_be32_load(&bi[0], rec + 0x98 - 0x28) == 0x1cu);	/* flags: VIA1, no VIA2 */
		CHECK(nw_be32_load(&bi[0], rec + 0x98 + 0x8) == 0x80016000u);	/* VIA base */
		CHECK(nw_be32_load(&bi[0], rec + 0x98 + 0x2c) == 0);		/* VIA2 absent */
		CHECK(nw_be32_load(&bi[0], rec + 0x98 + 0xf4) == 0x80040000u);	/* OpenPIC */
		CHECK(nw_be32_load(&bi[0], rec - 0x28 + 0x1c) == 8);

		/* Overlapping extra range is rejected; a bad table fails the check. */
		nw_pmdt_range bad;
		bad.la = bad.pa = 0x50100000u; bad.size = 0x1000u;
		l.extra = &bad; l.n_extra = 1;
		std::vector<uint8_t> ci2(ci);
		CHECK(nw_fill_config_info_be(&ci2[0], &l) == -1);
		std::vector<uint8_t> ci3(ci);
		ci3[NW_CI_PAGEMAP_OFF + s5 + 8] = 0; ci3[NW_CI_PAGEMAP_OFF + s5 + 9] = 0x10;	/* SheepMem page below ROM end */
		CHECK(!nw_config_info_pagemap_ok(&ci3[0]));

		uint8_t si[NW_SI_SIZE];
		nw_fill_system_info_be(si, &l);
		CHECK(nw_be32_load(si, 0) == 0x08000000u);
		CHECK(nw_be32_load(si, 0x30) == 0x10000000u);
		CHECK(nw_be32_load(si, 0x34) == 0x08000000u);
		CHECK(nw_be32_load(si, 0x38) == 0);
	}

	/* Debug log needles Grok Build greps (NW-BOOT prefix on SheepShaver Debug). */
	{
		CHECK(strcmp(nw_boot_line_g0_newworld(),
			"G0: DecodeROM 4 MiB NewWorld +0x30d064 NK +0x310000") == 0);
		CHECK(strcmp(nw_boot_line_g1_tree(),
			"G1: tree root compatible MacRISC2 Gestalt 406 /memory /cpus /chosen") == 0);
		CHECK(strcmp(nw_boot_line_g1_kdp(),
			"G1: Hnfo vs mtsdr1: Hnfo BATRangeInit saveKernelDataPtr adjacent") == 0);
		CHECK(strcmp(nw_boot_line_g1_mtsdr1(),
			"G1: Hnfo vs mtsdr1: mtsdr1") == 0);
		CHECK(strcmp(nw_boot_line_g1_hwinit(),
			"G1: HardwareInit handoff NK +0x310000 ConfigInfo +0x30d000") == 0);
		CHECK(strcmp(nw_boot_line_g1_patch_skip(),
			"G1: New World patch skip") == 0);
		CHECK(strcmp(nw_boot_line_g2_first_dsi(),
			"G2: first DSI SRR0=PC DR on HIT no second DSI") == 0);
		CHECK(strcmp(nw_boot_line_g2_translator_off(),
			"G2: translator off (Old World)") == 0);
	}

	const uint32_t ram_size = 4u * 1024u * 1024u;
	std::vector<uint8_t> ram(ram_size, 0);

	ppc32_mmu mmu;
	mmu.set_physical_memory(&ram[0], ram_size);

	/* ---- BAT hit (data), before HTAB ---- */
	{
		mmu.reset();
		mmu.set_physical_memory(&ram[0], ram_size);
		mmu.set_msr(ppc32_mmu::MSR_DR | ppc32_mmu::MSR_IR);
		/* DBAT0: EA 0x80000000, 128 KiB, Vs, PA 0x00020000 */
		mmu.set_dbat(0, 0x80000002u, 0x00020000u);
		const ppc32_xlate_result hit =
			mmu.translate(0x80000010u, PPC32_XLATE_DR, 4);
		CHECK(hit.ok);
		CHECK(hit.pa == 0x00020010u);

		/* Same EA via HTAB would map elsewhere; BAT wins on a cold TLB. */
		mmu.set_sr(8, 0x00000001u);
		mmu.set_sdr1(0x00100000u);
		program_pte(&ram[0], 0x00100000u, 1, 0x80000000u, 0, 0x00040u);
		mmu.tlbia();
		const ppc32_xlate_result bat_first =
			mmu.translate(0x80000010u, PPC32_XLATE_DR, 4);
		CHECK(bat_first.ok);
		CHECK(bat_first.pa == 0x00020010u);
	}

	/* ---- HTAB primary hash ---- */
	{
		mmu.reset();
		mmu.set_physical_memory(&ram[0], ram_size);
		memset(&ram[0], 0, ram_size);
		const uint32_t sdr1 = 0x00100000u; /* HTABORG=0x00100000, 64 KiB */
		const uint32_t vsid = 1;
		const uint32_t ea = 0x00004000u;
		const uint32_t rpn = 0x00020u;
		mmu.set_msr(ppc32_mmu::MSR_DR);
		mmu.set_sdr1(sdr1);
		mmu.set_sr(0, vsid);
		program_pte(&ram[0], sdr1, vsid, ea, 0, rpn);
		const ppc32_xlate_result hit =
			mmu.translate(ea | 0x20u, PPC32_XLATE_DR, 4);
		CHECK(hit.ok);
		CHECK(hit.pa == ((rpn << 12) | 0x20u));
	}

	/* ---- HTAB secondary hash (primary empty) ---- */
	{
		mmu.reset();
		mmu.set_physical_memory(&ram[0], ram_size);
		memset(&ram[0], 0, ram_size);
		const uint32_t sdr1 = 0x00100000u;
		const uint32_t vsid = 1;
		const uint32_t ea = 0x00004000u;
		const uint32_t rpn = 0x00030u;
		mmu.set_msr(ppc32_mmu::MSR_DR);
		mmu.set_sdr1(sdr1);
		mmu.set_sr(0, vsid);
		program_pte(&ram[0], sdr1, vsid, ea, 1, rpn);
		const ppc32_xlate_result hit =
			mmu.translate(ea, PPC32_XLATE_DR, 4);
		CHECK(hit.ok);
		CHECK(hit.pa == (rpn << 12));
	}

	/* ---- Fault: no BAT, no PTE ---- */
	{
		mmu.reset();
		mmu.set_physical_memory(&ram[0], ram_size);
		memset(&ram[0], 0, ram_size);
		mmu.set_msr(ppc32_mmu::MSR_DR);
		mmu.set_sdr1(0x00100000u);
		mmu.set_sr(0, 1);
		const ppc32_xlate_result miss =
			mmu.translate(0x00008000u, PPC32_XLATE_DR, 4);
		CHECK(!miss.ok);
	}

	/* ---- IR-only vs DR-only (independent MSR bits) ---- */
	{
		mmu.reset();
		mmu.set_physical_memory(&ram[0], ram_size);
		mmu.set_msr(ppc32_mmu::MSR_IR); /* IR on, DR off */
		mmu.set_ibat(0, 0x90000002u, 0x00040000u);
		mmu.set_dbat(0, 0x90000002u, 0x00080000u);

		const ppc32_xlate_result ir =
			mmu.translate(0x90000020u, PPC32_XLATE_IR, 4);
		CHECK(ir.ok);
		CHECK(ir.pa == 0x00040020u);

		const ppc32_xlate_result dr_ident =
			mmu.translate(0x90000020u, PPC32_XLATE_DR, 4);
		CHECK(dr_ident.ok);
		CHECK(dr_ident.pa == 0x90000020u); /* DR off: identity */

		mmu.set_msr(ppc32_mmu::MSR_DR); /* IR off, DR on */
		mmu.tlbia();
		const ppc32_xlate_result ir_ident =
			mmu.translate(0x90000020u, PPC32_XLATE_IR, 4);
		CHECK(ir_ident.ok);
		CHECK(ir_ident.pa == 0x90000020u);

		const ppc32_xlate_result dr =
			mmu.translate(0x90000020u, PPC32_XLATE_DR, 4);
		CHECK(dr.ok);
		CHECK(dr.pa == 0x00080020u);
	}

	/* ---- tlbie drops the cached translation ---- */
	{
		mmu.reset();
		mmu.set_physical_memory(&ram[0], ram_size);
		memset(&ram[0], 0, ram_size);
		const uint32_t sdr1 = 0x00100000u;
		const uint32_t vsid = 2;
		const uint32_t ea = 0x10001000u;
		mmu.set_msr(ppc32_mmu::MSR_DR);
		mmu.set_sdr1(sdr1);
		mmu.set_sr(1, vsid);
		program_pte(&ram[0], sdr1, vsid, ea, 0, 0x00050u);

		const ppc32_xlate_result first =
			mmu.translate(ea, PPC32_XLATE_DR, 4);
		CHECK(first.ok);
		CHECK(first.pa == 0x00050000u);

		/* Rewrite PTE to a new RPN without tlbie: TLB must still hit. */
		program_pte(&ram[0], sdr1, vsid, ea, 0, 0x00060u);
		const ppc32_xlate_result cached =
			mmu.translate(ea, PPC32_XLATE_DR, 4);
		CHECK(cached.ok);
		CHECK(cached.pa == 0x00050000u);

		mmu.tlbie(ea);
		const ppc32_xlate_result after =
			mmu.translate(ea, PPC32_XLATE_DR, 4);
		CHECK(after.ok);
		CHECK(after.pa == 0x00060000u);
	}

	/* width 0 is not a valid access */
	{
		mmu.reset();
		mmu.set_msr(0);
		const ppc32_xlate_result z =
			mmu.translate(0, PPC32_XLATE_DR, 0);
		CHECK(!z.ok);
	}

	/* IR/DR off: no forced translate (Old World / identity). */
	{
		mmu.reset();
		mmu.set_msr(0);
		const ppc32_xlate_result id =
			mmu.translate(0x12345000u, PPC32_XLATE_DR, 4);
		CHECK(id.ok);
		CHECK(id.pa == 0x12345000u);
		CHECK(!ppc32_guest_mmu_enabled());
	}

	/*
	 * G2 accept (HotInts DataStorageInt): data DSI, then DR-on lwz of
	 * the faulting insn at SRR0 must HIT. Early NK keeps the insn side
	 * mapped. Must not take a second DSI. Not a DSISR-only model.
	 */
	{
		mmu.reset();
		mmu.set_physical_memory(&ram[0], ram_size);
		memset(&ram[0], 0, ram_size);

		const uint32_t fault_pc = 0x00004000u;
		const uint32_t store_ea = 0x12345000u;
		const uint32_t stw_r3_0_r4 = 0x90840000u; /* stw r4,0(r4) placeholder */
		be32_store(&ram[0], fault_pc, stw_r3_0_r4);

		/* Code page identity-mapped for data (early NK insn side). */
		mmu.set_dbat(0, 0x00000002u, 0x00000000u);
		mmu.set_ibat(0, 0x00000002u, 0x00000000u);
		mmu.set_msr(ppc32_mmu::MSR_IR | ppc32_mmu::MSR_DR);

		const ppc32_xlate_result data_miss =
			mmu.translate(store_ea, PPC32_XLATE_DR, 4);
		CHECK(!data_miss.ok);

		ppc32_hotints_dsi dsi;
		dsi.sprg[0] = 0x68ffe000u; /* KDP */
		dsi.sprg[1] = 0x11111111u; /* saved r1 */
		dsi.sprg[2] = 0x22222222u; /* LR */
		dsi.sprg[3] = 0x33333333u; /* VecTbl */
		dsi.take_data_dsi(mmu, fault_pc, store_ea, true);
		CHECK(dsi.srr0 == fault_pc);
		CHECK(dsi.dar == store_ea);
		CHECK(dsi.srr0 != dsi.dar); /* not a DAR-as-PC / DSISR-only model */
		CHECK((dsi.dsisr & 0x40000000u) != 0); /* no translation */
		CHECK((dsi.dsisr & 0x02000000u) != 0); /* store; AlignmentInt mfdsisr */
		CHECK(dsi.vector == 0x300);
		CHECK((mmu.msr() & (ppc32_mmu::MSR_IR | ppc32_mmu::MSR_DR)) == 0);

		const ppc32_xlate_result lwz = dsi.lwz_faulting_insn(mmu);
		CHECK(lwz.ok);
		CHECK(lwz.pa == fault_pc);
		uint32_t insn = ((uint32_t)ram[lwz.pa] << 24) |
		                ((uint32_t)ram[lwz.pa + 1] << 16) |
		                ((uint32_t)ram[lwz.pa + 2] << 8) |
		                (uint32_t)ram[lwz.pa + 3];
		CHECK(insn == stw_r3_0_r4);

		/* Still a miss on the original data EA — no second DSI on the lwz. */
		const ppc32_xlate_result still_miss =
			mmu.translate(store_ea, PPC32_XLATE_DR, 4);
		CHECK(!still_miss.ok);
	}

	/* G2 accept via HTAB (insn page mapped, data EA not). */
	{
		mmu.reset();
		mmu.set_physical_memory(&ram[0], ram_size);
		memset(&ram[0], 0, ram_size);
		const uint32_t sdr1 = 0x00100000u;
		const uint32_t vsid = 1;
		const uint32_t fault_pc = 0x00004000u;
		const uint32_t store_ea = 0x12345000u;
		const uint32_t rpn = 0x00004u;
		be32_store(&ram[0], rpn << 12, 0x90000000u);
		mmu.set_sdr1(sdr1);
		mmu.set_sr(0, vsid);
		program_pte(&ram[0], sdr1, vsid, fault_pc, 0, rpn);
		mmu.set_msr(ppc32_mmu::MSR_IR | ppc32_mmu::MSR_DR);

		CHECK(!mmu.translate(store_ea, PPC32_XLATE_DR, 4).ok);

		ppc32_hotints_dsi dsi;
		dsi.take_data_dsi(mmu, fault_pc, store_ea, true);
		const ppc32_xlate_result lwz = dsi.lwz_faulting_insn(mmu);
		CHECK(lwz.ok);
		CHECK(lwz.pa == (rpn << 12));
	}

	printf("SheepShaver-MMUTests: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
