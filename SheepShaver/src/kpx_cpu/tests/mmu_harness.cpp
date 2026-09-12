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
#include "nw_bootinfo.h"

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
		/* 16 terminators + 5 RAM-segment spares (segs 0..4) + ROM, SheepMem,
		 * IRP, boot-info, NK 1 MiB, CI page, KDP, EDP, DR cache, I/O seg 8,
		 * I/O seg f */
		CHECK(n == 16 + 5 + 11);
		CHECK(nw_config_info_pagemap_ok(&ci[0]));
		CHECK(nw_be32_load(&ci[0], 0xb8) == (uint32_t)n * 8u);
		CHECK(nw_be32_load(&ci[0], 0xbc) == NW_CI_PAGEMAP_OFF);
		const uint8_t *pm = &ci[NW_CI_PAGEMAP_OFF];
		/* segs 0..4: two (0, 0xffff, 0xa00) entries each, 16 bytes apart,
		 * as golden (NK rewrites the first in place into a table range). */
		for (int s = 0; s < 5; s++) {
			uint32_t so = nw_be32_load(&ci[0], 0xcc + s * 8);
			CHECK(so == (uint32_t)s * 16u);
			CHECK(nw_be32_load(pm, so) == 0x0000ffffu && nw_be32_load(pm, so + 4) == 0xa00u);
			CHECK(nw_be32_load(pm, so + 8) == 0x0000ffffu && nw_be32_load(pm, so + 12) == 0xa00u);
		}
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
		/* I/O segments: whole segment, 1:1, I|M|G|PP2, then the a01 terminator */
		uint32_t s8 = nw_be32_load(&ci[0], 0xcc + 8 * 8);
		CHECK(((pm[s8] << 8) | pm[s8 + 1]) == 0 && ((pm[s8 + 2] << 8) | pm[s8 + 3]) == 0xffff);
		CHECK(nw_be32_load(pm, s8 + 4) == 0x8000003au && nw_be32_load(pm, s8 + 12) == 0x80000a01u);
		uint32_t sf = nw_be32_load(&ci[0], 0xcc + 15 * 8);
		CHECK(nw_be32_load(pm, sf + 4) == 0xf000003au && nw_be32_load(pm, sf + 12) == 0xf0000a01u);
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
		CHECK(nw_be32_load(hw, 0x08) == NW_BOOTINFO_LA + NW_BOOTINFO_HWREC_OFF && nw_be32_load(hw, 0x04) == 0x6400000cu);
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

		/*
		 * Boot-info device tree (BGsTree) with a synthetic parcel blob:
		 * a 'node' parcel (AAPL,CodePrepare + one stored library), a 'prop'
		 * parcel matched by parent name + device_type (via-pmu/rtc) and one
		 * matched by node name ('macos'). Read back through the same
		 * record walk the 68k importer (ROM 0x44420) performs.
		 */
		{
			std::vector<uint8_t> pb(0x400, 0);
			memcpy(&pb[0], "prcl", 4);
			be32_store(&pb[0], 4, 1);
			be32_store(&pb[0], 12, 0x14);
			uint32_t data = 0x300;	/* payload area */
			memcpy(&pb[data], "AAPL,CodePrepare", 17);
			memcpy(&pb[data + 0x20], "LIBRARY!", 8);
			memcpy(&pb[data + 0x40], "RTCDRV", 6);
			memcpy(&pb[data + 0x50], "9.2.1", 6);
			/* parcel 1: node, two children */
			uint32_t p1 = 0x14, p2 = p1 + 88 + 2 * 60, p3 = p2 + 88 + 60;
			be32_store(&pb[0], p1, p2);
			memcpy(&pb[p1 + 4], "node", 4);
			be32_store(&pb[0], p1 + 8, 88 + 2 * 60);
			be32_store(&pb[0], p1 + 12, 0x20000);
			be32_store(&pb[0], p1 + 20, 60);
			memcpy(&pb[p1 + 24], "CodePrepare Node Parcel", 23);
			uint32_t c = p1 + 88;
			memcpy(&pb[c], "cstr", 4); be32_store(&pb[0], c + 12, 17); be32_store(&pb[0], c + 20, 17);
			be32_store(&pb[0], c + 24, data); memcpy(&pb[c + 28], "name", 4);
			c += 60;
			memcpy(&pb[c], "nlib", 4); be32_store(&pb[0], c + 4, 0x20094); be32_store(&pb[0], c + 12, 8);
			be32_store(&pb[0], c + 20, 8); be32_store(&pb[0], c + 24, data + 0x20); memcpy(&pb[c + 28], "TestLib", 7);
			/* parcel 2: prop, parent 'via-pmu' && device_type 'rtc' */
			be32_store(&pb[0], p2, p3);
			memcpy(&pb[p2 + 4], "prop", 4);
			be32_store(&pb[0], p2 + 8, 88 + 60);
			be32_store(&pb[0], p2 + 12, 0xa);
			be32_store(&pb[0], p2 + 20, 60);
			memcpy(&pb[p2 + 24], "via-pmu", 7);
			memcpy(&pb[p2 + 56], "rtc", 3);
			c = p2 + 88;
			memcpy(&pb[c], "ndrv", 4); be32_store(&pb[0], c + 4, 4); be32_store(&pb[0], c + 12, 6);
			be32_store(&pb[0], c + 20, 6); be32_store(&pb[0], c + 24, data + 0x40);
			memcpy(&pb[c + 28], "driver,AAPL,MacOS,PowerPC", 25);
			/* parcel 3: prop, name == 'macos' (last: link 0) */
			be32_store(&pb[0], p3, 0);
			memcpy(&pb[p3 + 4], "prop", 4);
			be32_store(&pb[0], p3 + 8, 88 + 60);
			be32_store(&pb[0], p3 + 12, 0x1);
			be32_store(&pb[0], p3 + 20, 60);
			memcpy(&pb[p3 + 24], "macos", 5);
			c = p3 + 88;
			memcpy(&pb[c], "cstr", 4); be32_store(&pb[0], c + 12, 6); be32_store(&pb[0], c + 20, 6);
			be32_store(&pb[0], c + 24, data + 0x50); memcpy(&pb[c + 28], "MacOSROMFile-version", 20);

			nw_bootinfo_params bp;
			memset(&bp, 0, sizeof(bp));
			bp.ram_size = 0x08000000u;
			bp.fb_la = 0x81000000u;
			bp.fb_width = 640; bp.fb_height = 480; bp.fb_depth = 8; bp.fb_linebytes = 640;
			bp.pvr = 0x000c0000u; bp.cpu_hz = 400000000u; bp.bus_hz = 100000000u; bp.tb_hz = 25000000u;
			bp.parcels = &pb[0]; bp.parcels_size = pb.size();
			const uint32_t end = nw_bootinfo_build_tree(&bi[0], NW_BOOTINFO_TREE_MAX, &bp);
			CHECK(end > 0x1000 && end <= NW_BOOTINFO_TREE_MAX);
			CHECK(nw_be32_load(&bi[0], 0) == 0x504d5226u && nw_be32_load(&bi[0], 4) == 0x42477354u);
			CHECK(nw_be32_load(&bi[0], rec) == 0x98u);			/* hwrec untouched */
			const int nn = nw_bootinfo_count_nodes(&bi[0], NW_BOOTINFO_TREE_MAX);
			CHECK(nn > 40 && nw_be32_load(&bi[0], 0xc) == 0);		/* root: no sibling */
			CHECK(nw_be32_load(&bi[0], 0x10) == 12 * 1);			/* first child right after root */
			CHECK(nw_be32_load(&bi[0], 0x14) == 12u * (uint32_t)nn);	/* props follow the node records */
			const uint8_t *v; uint32_t vl, node;
			CHECK(nw_bootinfo_get_prop(&bi[0], NW_BOOTINFO_TREE_MAX, NW_BOOTINFO_ROOT, "name", &v, &vl) &&
			      vl == 12 && memcmp(v, "device-tree", 12) == 0);
			CHECK(nw_bootinfo_get_prop(&bi[0], NW_BOOTINFO_TREE_MAX, NW_BOOTINFO_ROOT, "model", &v, &vl) &&
			      memcmp(v, "PowerMac3,1", 12) == 0);
			/* parcel 'node' becomes the first root child, its library a property */
			CHECK(nw_bootinfo_find_node(&bi[0], NW_BOOTINFO_TREE_MAX, "/AAPL,CodePrepare", &node) &&
			      node == NW_BOOTINFO_ROOT + 12);
			CHECK(nw_bootinfo_get_prop(&bi[0], NW_BOOTINFO_TREE_MAX, node, "TestLib", &v, &vl) &&
			      vl == 8 && memcmp(v, "LIBRARY!", 8) == 0);
			CHECK(!nw_bootinfo_get_prop(&bi[0], NW_BOOTINFO_TREE_MAX, node, "name-not-here", &v, &vl));
			/* prop parcel matched by parent + device_type */
			CHECK(nw_bootinfo_find_node(&bi[0], NW_BOOTINFO_TREE_MAX, "/pci/mac-io/via-pmu/rtc", &node) &&
			      nw_bootinfo_get_prop(&bi[0], NW_BOOTINFO_TREE_MAX, node, "driver,AAPL,MacOS,PowerPC", &v, &vl) &&
			      vl == 6 && memcmp(v, "RTCDRV", 6) == 0);
			CHECK(nw_bootinfo_find_node(&bi[0], NW_BOOTINFO_TREE_MAX, "/pci/mac-io/via-pmu", &node) &&
			      !nw_bootinfo_get_prop(&bi[0], NW_BOOTINFO_TREE_MAX, node, "driver,AAPL,MacOS,PowerPC", &v, &vl));
			CHECK(nw_bootinfo_find_node(&bi[0], NW_BOOTINFO_TREE_MAX, "/rom/macos", &node) &&
			      nw_bootinfo_get_prop(&bi[0], NW_BOOTINFO_TREE_MAX, node, "MacOSROMFile-version", &v, &vl) &&
			      vl == 6 && memcmp(v, "9.2.1", 6) == 0);
			/* machine data */
			CHECK(nw_bootinfo_find_node(&bi[0], NW_BOOTINFO_TREE_MAX, "/memory", &node) &&
			      nw_bootinfo_get_prop(&bi[0], NW_BOOTINFO_TREE_MAX, node, "reg", &v, &vl) &&
			      vl == 8 && nw_be32_load(v, 0) == 0 && nw_be32_load(v, 4) == 0x08000000u);
			CHECK(nw_bootinfo_find_node(&bi[0], NW_BOOTINFO_TREE_MAX, "/pci/display", &node) &&
			      nw_bootinfo_get_prop(&bi[0], NW_BOOTINFO_TREE_MAX, node, "address", &v, &vl) &&
			      nw_be32_load(v, 0) == 0x81000000u &&
			      nw_bootinfo_get_prop(&bi[0], NW_BOOTINFO_TREE_MAX, node, "linebytes", &v, &vl) &&
			      nw_be32_load(v, 0) == 640);
			CHECK(nw_bootinfo_find_node(&bi[0], NW_BOOTINFO_TREE_MAX, "/cpus/PowerPC,G4", &node) &&
			      nw_bootinfo_get_prop(&bi[0], NW_BOOTINFO_TREE_MAX, node, "cpu-version", &v, &vl) &&
			      nw_be32_load(v, 0) == 0x000c0000u);
			CHECK(nw_bootinfo_find_node(&bi[0], NW_BOOTINFO_TREE_MAX, "/pci/mac-io/interrupt-controller", &node) &&
			      nw_bootinfo_get_prop(&bi[0], NW_BOOTINFO_TREE_MAX, node, "AAPL,address", &v, &vl) &&
			      nw_be32_load(v, 0) == 0x80040000u);
			CHECK(!nw_bootinfo_find_node(&bi[0], NW_BOOTINFO_TREE_MAX, "/pci/usb", &node));
			/* every property record: next == its own size, last has 0 */
			{
				uint32_t p = NW_BOOTINFO_ROOT + nw_be32_load(&bi[0], 0x14);
				int ok = 1, n = 0;
				for (;;) {
					const uint32_t next = nw_be32_load(&bi[0], p);
					const uint32_t sz = NW_BOOTINFO_PROP_HDR + ((nw_be32_load(&bi[0], p + 0x24) + 3u) & ~3u);
					if (next == 0) break;
					if (next != sz) { ok = 0; break; }
					p += next; n++;
				}
				CHECK(ok && n >= 7);
			}
			/* bad parcels fail the build; no parcels still yields a tree */
			pb[0] = 'x';
			CHECK(nw_bootinfo_build_tree(&bi[0], NW_BOOTINFO_TREE_MAX, &bp) == 0);
			bp.parcels = NULL; bp.parcels_size = 0;
			CHECK(nw_bootinfo_build_tree(&bi[0], NW_BOOTINFO_TREE_MAX, &bp) > 0 &&
			      !nw_bootinfo_find_node(&bi[0], NW_BOOTINFO_TREE_MAX, "/AAPL,CodePrepare", &node));
		}

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
		/* NK (0x310548) trims bank 0 by PA_RelocatedLowMem and subtracts it
		 * from the total: bank 0 is described from PA 0 so the trim lands
		 * the bank at ram_base and lowmem (LA 0) on its first page. */
		CHECK(nw_be32_load(si, 0) == 0x18000000u);
		CHECK(nw_be32_load(si, 0x30) == 0);
		CHECK(nw_be32_load(si, 0x34) == 0x18000000u);
		CHECK(nw_be32_load(si, 0x38) == 0);
		CHECK(nw_be32_load(ci.data(), 0x360) == 0x10000000u);
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
		/* DBAT0: EA 0x80000000, 128 KiB, Vs, PA 0x00020000, PP=2 (RW) */
		mmu.set_dbat(0, 0x80000002u, 0x00020002u);
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
		mmu.set_ibat(0, 0x90000002u, 0x00040002u);
		mmu.set_dbat(0, 0x90000002u, 0x00080002u);

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

		/* tlbie works by congruence class (EA[13..19]): a tlbie on
		 * 0x00001000 drops the cached 0x10001000 too (the NK flushes the
		 * whole TLB with tlbie over 0..0x7f000); a different class does not. */
		program_pte(&ram[0], sdr1, vsid, ea, 0, 0x00070u);
		mmu.tlbie(0x00002000u);
		CHECK(mmu.translate(ea, PPC32_XLATE_DR, 4).pa == 0x00060000u);
		mmu.tlbie(0x00001000u);
		CHECK(mmu.translate(ea, PPC32_XLATE_DR, 4).pa == 0x00070000u);
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
		mmu.set_dbat(0, 0x00000002u, 0x00000002u);
		mmu.set_ibat(0, 0x00000002u, 0x00000002u);
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

	/*
	 * Protection (OEA 32-bit, as QEMU hash32): PP/key, BAT PP, R/C bits,
	 * no-execute segments, guarded pages, SR-tagged TLB entries. Golden
	 * shows the NK re-faulting on an already mapped EDP page from the
	 * user-mode emulator; without these checks the fault never happens.
	 */
	{
		mmu.reset();
		mmu.set_physical_memory(&ram[0], ram_size);
		memset(&ram[0], 0, ram_size);
		const uint32_t sdr1 = 0x00100000u;
		const uint32_t ea = 0x20003000u;	/* segment 2 */
		const uint32_t rpn = 0x00070u;
		mmu.set_sdr1(sdr1);
		mmu.set_msr(ppc32_mmu::MSR_DR | ppc32_mmu::MSR_IR);

		/* PP=0, Ks=0 Kp=1: supervisor RW, user no access. */
		mmu.set_sr(2, 0x20000000u | 5u);
		program_pte(&ram[0], sdr1, 5, ea, 0, rpn);
		const uint32_t pte1_pa = pteg_addr(sdr1, ((5u & 0x7ffffu) ^ ((ea >> 12) & 0xffffu))) + 4;
		CHECK((nw_be32_load(&ram[0], pte1_pa) & 0x180u) == 0);
		ppc32_xlate_result sup = mmu.translate(ea, PPC32_XLATE_DR, 4, false);
		CHECK(sup.ok && sup.pa == (rpn << 12));
		CHECK((nw_be32_load(&ram[0], pte1_pa) & 0x180u) == 0x100u);	/* R set, C clear */
		sup = mmu.translate(ea, PPC32_XLATE_DR, 4, true);
		CHECK(sup.ok);
		CHECK((nw_be32_load(&ram[0], pte1_pa) & 0x180u) == 0x180u);	/* C set on store */
		mmu.set_msr(ppc32_mmu::MSR_DR | ppc32_mmu::MSR_IR | ppc32_mmu::MSR_PR);
		ppc32_xlate_result usr = mmu.translate(ea, PPC32_XLATE_DR, 4, false);
		CHECK(!usr.ok && usr.fault == PPC32_FAULT_PROT);
		ppc32_hotints_dsi dsi;
		dsi.take_data_dsi(mmu, 0x1000u, ea, true, usr.fault);
		CHECK(dsi.dsisr == (PPC32_FAULT_PROT | 0x02000000u));
		CHECK(dsi.dar == ea);
		mmu.set_msr(ppc32_mmu::MSR_DR | ppc32_mmu::MSR_IR | ppc32_mmu::MSR_PR);
		/* Kp=0: user RW through the same PTE (new SR value, no tlbie). */
		mmu.set_sr(2, 5u);
		usr = mmu.translate(ea, PPC32_XLATE_DR, 4, true);
		CHECK(usr.ok && usr.pa == (rpn << 12));
		/* PP=3: read-only for everyone. */
		nw_be32_store(&ram[0], pte1_pa, (rpn << 12) | 3u);
		mmu.tlbie(ea);
		mmu.set_msr(ppc32_mmu::MSR_DR | ppc32_mmu::MSR_IR);
		CHECK(mmu.translate(ea, PPC32_XLATE_DR, 4, false).ok);
		ppc32_xlate_result ro = mmu.translate(ea, PPC32_XLATE_DR, 4, true);
		CHECK(!ro.ok && ro.fault == PPC32_FAULT_PROT);
		/* PP=1 under key 1: read-only; key 0: RW. */
		nw_be32_store(&ram[0], pte1_pa, (rpn << 12) | 1u);
		mmu.tlbie(ea);
		mmu.set_sr(2, 0x40000000u | 5u);	/* Ks=1 */
		CHECK(mmu.translate(ea, PPC32_XLATE_DR, 4, false).ok);
		CHECK(mmu.translate(ea, PPC32_XLATE_DR, 4, true).fault == PPC32_FAULT_PROT);
		mmu.set_sr(2, 5u);
		CHECK(mmu.translate(ea, PPC32_XLATE_DR, 4, true).ok);
		/* Fetch: N segment, guarded page, otherwise executable. */
		mmu.set_sr(2, 0x10000000u | 5u);
		ppc32_xlate_result nx = mmu.translate(ea, PPC32_XLATE_IR, 4);
		CHECK(!nx.ok && nx.fault == PPC32_FAULT_NOEXEC);
		mmu.set_sr(2, 5u);
		CHECK(mmu.translate(ea, PPC32_XLATE_IR, 4).ok);
		nw_be32_store(&ram[0], pte1_pa, (rpn << 12) | 0x8u | 2u);	/* G */
		mmu.tlbie(ea);
		ppc32_xlate_result g = mmu.translate(ea, PPC32_XLATE_IR, 4);
		CHECK(!g.ok && g.fault == PPC32_FAULT_NOEXEC);
		CHECK(mmu.translate(ea, PPC32_XLATE_DR, 4).ok);	/* data on guarded is fine */
		/* A different VSID in the SR misses the cached entry: no PTE. */
		mmu.set_sr(2, 6u);
		ppc32_xlate_result other = mmu.translate(ea, PPC32_XLATE_DR, 4);
		CHECK(!other.ok && other.fault == PPC32_FAULT_NOTRANS);
		/* BAT PP: 0 no access, 1 read-only, 2 RW; not cached across mtbat. */
		mmu.set_dbat(1, 0x30000002u, 0x00200000u);
		ppc32_xlate_result b0 = mmu.translate(0x30000100u, PPC32_XLATE_DR, 4);
		CHECK(!b0.ok && b0.fault == PPC32_FAULT_PROT);
		mmu.set_dbat(1, 0x30000002u, 0x00200001u);
		CHECK(mmu.translate(0x30000100u, PPC32_XLATE_DR, 4).ok);
		CHECK(mmu.translate(0x30000100u, PPC32_XLATE_DR, 4, true).fault == PPC32_FAULT_PROT);
		mmu.set_dbat(1, 0x30000002u, 0x00200002u);
		CHECK(mmu.translate(0x30000100u, PPC32_XLATE_DR, 4, true).ok);
		mmu.set_dbat(1, 0x30000002u, 0x00300002u);	/* moved: no stale cache */
		CHECK(mmu.translate(0x30000100u, PPC32_XLATE_DR, 4).pa == 0x00300100u);
		/* Vs-only BAT is invisible in user mode. */
		mmu.set_msr(ppc32_mmu::MSR_DR | ppc32_mmu::MSR_IR | ppc32_mmu::MSR_PR);
		CHECK(mmu.translate(0x30000100u, PPC32_XLATE_DR, 4).fault == PPC32_FAULT_NOTRANS);
	}

	printf("SheepShaver-MMUTests: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
