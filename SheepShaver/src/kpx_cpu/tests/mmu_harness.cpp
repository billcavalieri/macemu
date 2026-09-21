/*
 *  mmu_harness.cpp - Host-side SheepShaver-MMUTests (G1 + G2)
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 *
 *  G1: New World boot contract (tree nodes, Gestalt 406, KDP layout,
 *  Hnfo-or-mtsdr1, BATRangeInit, saveKernelDataPtr adjacency). No ROM.
 *  G2: BAT + synthetic HTAB; HotInts DSI accept (SRR0 lwz HIT).
 */

#include "cpu/ppc/ppc-mmu.hpp"
#include "nw_boot_contract.h"
#include "nw_bootinfo.h"
#include "nw_io.h"
#include "nw_devices.h"
#include "nw_nvram.h"
#include "nw_jit.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>

static int g_pass;
static int g_fail;

static uint64_t g_fake_tb;
static uint64_t fake_tb_ticks(void *)
{
	return g_fake_tb;
}

/* PMU handshake as the ROM's driver runs it, through the byte bus. */
static uint32_t via_rd(uint32_t reg) { return nw_io_read(NW_IO_VIA_PMU_BASE + (reg << NW_VIA_REG_SHIFT), 1, 0); }
static void via_wr(uint32_t reg, uint32_t v) { nw_io_write(NW_IO_VIA_PMU_BASE + (reg << NW_VIA_REG_SHIFT), 1, v, 0); }
static int pmu_xfer_ok;
static void pmu_handshake(void)
{
	via_wr(NW_VIA_B, via_rd(NW_VIA_B) & ~NW_PMU_TREQ);		/* request */
	if (via_rd(NW_VIA_B) & NW_PMU_TACK) pmu_xfer_ok = 0;		/* PMU acknowledges at once */
	if (!(via_rd(NW_VIA_IFR) & NW_VIA_IFR_SR)) pmu_xfer_ok = 0;	/* byte shifted */
}
static void pmu_release(void)
{
	via_wr(NW_VIA_B, via_rd(NW_VIA_B) | NW_PMU_TREQ);
	if (!(via_rd(NW_VIA_B) & NW_PMU_TACK)) pmu_xfer_ok = 0;
}
static void pmu_send(uint8_t b)
{
	via_wr(NW_VIA_ACR, via_rd(NW_VIA_ACR) | NW_VIA_ACR_SR_OUT);
	via_wr(NW_VIA_SR, b);
	pmu_handshake();
	via_rd(NW_VIA_SR);							/* clears SR_INT */
	pmu_release();
}
static uint8_t pmu_recv(void)
{
	via_wr(NW_VIA_ACR, via_rd(NW_VIA_ACR) & ~NW_VIA_ACR_SR_OUT);
	via_rd(NW_VIA_SR);
	pmu_handshake();
	const uint8_t b = (uint8_t)via_rd(NW_VIA_SR);
	pmu_release();
	return b;
}

static uint8_t g_jit_hram[64];
static uint32_t harness_jit_lwz(void *, uint32_t ea, uint32_t, int *fault)
{
	if (ea + 4 > sizeof(g_jit_hram)) {
		*fault = 1;
		return 0;
	}
	return ((uint32_t)g_jit_hram[ea] << 24) | ((uint32_t)g_jit_hram[ea + 1] << 16) |
	       ((uint32_t)g_jit_hram[ea + 2] << 8) | g_jit_hram[ea + 3];
}
static void harness_jit_stw(void *, uint32_t ea, uint32_t val, uint32_t, int *fault)
{
	if (ea + 4 > sizeof(g_jit_hram)) {
		*fault = 1;
		return;
	}
	g_jit_hram[ea] = (uint8_t)(val >> 24);
	g_jit_hram[ea + 1] = (uint8_t)(val >> 16);
	g_jit_hram[ea + 2] = (uint8_t)(val >> 8);
	g_jit_hram[ea + 3] = (uint8_t)val;
}

static uint32_t harness_jit_lwz_pa(void *h, uint32_t pa, uint32_t pc, int *fault)
{
	return harness_jit_lwz(h, pa, pc, fault);
}
static void harness_jit_stw_pa(void *h, uint32_t pa, uint32_t val, uint32_t pc, int *fault)
{
	harness_jit_stw(h, pa, val, pc, fault);
}

struct harness_rfi_host {
	uint32_t srr0;
	uint32_t srr1;
	int ncall;
};
static void harness_rfi(void *host, struct nw_jit_cpu *cpu)
{
	struct harness_rfi_host *h = (struct harness_rfi_host *)host;
	h->ncall++;
	cpu->pc = h->srr0;
	cpu->msr = h->srr1;
}

static int g_test_pmu_power_ev = -1;
static void test_pmu_power_hook(int ev, void *ctx)
{
	(void)ctx;
	g_test_pmu_power_ev = ev;
}

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
		/* ... 0x03 0x1d (display VBL) | 0x1c 0x1e | end */
		CHECK(nw_be32_load(&ci[0], 0xf80) == 0x002f0037u && nw_be32_load(&ci[0], 0xf8c) == 0x0003001du &&
		      nw_be32_load(&ci[0], 0xf90) == 0x001c001eu && nw_be32_load(&ci[0], 0xf94) == 0xffffffffu);
		CHECK(nw_be32_load(&ci[0], 0xd00) == 0xffffffffu && nw_be32_load(&ci[0], 0xd40) == 0x02070102u);
		CHECK(nw_be32_load(&ci[0], 0xf00) == 0x02070102u);
		/* level masks follow the list: level 1 = via-pmu (2); level 2 = gpio1, ata, display, usb 0x1c
		 * (0, 3, 5, 7, 8); level 3 = 0x1e (9); level 4 = ata dma (4, 6); level 7 = pswitch (1) */
		CHECK(nw_be32_load(&ci[0], 0xf44) == 0x20000000u && nw_be32_load(&ci[0], 0xf48) == 0x95800000u);
		CHECK(nw_be32_load(&ci[0], 0xf4c) == 0x00400000u && nw_be32_load(&ci[0], 0xf50) == 0x0a000000u);
		CHECK(nw_be32_load(&ci[0], 0xf5c) == 0x40000000u && nw_be32_load(&ci[0], 0xf40) == 0 && nw_be32_load(&ci[0], 0xf60) == 0);
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
		/* UnivROMFlags: bit 2 = ADB behind the PMU (68k table select
		 * `& 0xe` == 0xc -> PMU-99 ADB routines), not bit 1 (USB-only). */
		CHECK(nw_be32_load(&bi[0], rec + 0x24) == 0xc003bf1cu);
		CHECK(nw_be32_load(&bi[0], rec + 0x7c) == 0xc003bf1cu);
		CHECK((nw_be32_load(&bi[0], rec + 0x24) & 0xe) == 0xc);

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
			/* ADB behind the PMU, as OpenBIOS declares it for via=pmu-adb */
			CHECK(nw_bootinfo_find_node(&bi[0], NW_BOOTINFO_TREE_MAX, "/pci/mac-io/via-pmu/adb", &node) &&
			      nw_bootinfo_get_prop(&bi[0], NW_BOOTINFO_TREE_MAX, node, "compatible", &v, &vl) &&
			      vl == 7 && memcmp(v, "pmu-99", 7) == 0);
			CHECK(nw_bootinfo_find_node(&bi[0], NW_BOOTINFO_TREE_MAX, "/pci/mac-io/via-pmu/adb/keyboard", &node) &&
			      nw_bootinfo_get_prop(&bi[0], NW_BOOTINFO_TREE_MAX, node, "reg", &v, &vl) &&
			      vl == 4 && nw_be32_load(v, 0) == 8);
			CHECK(nw_bootinfo_find_node(&bi[0], NW_BOOTINFO_TREE_MAX, "/pci/mac-io/via-pmu/adb/mouse", &node) &&
			      nw_bootinfo_get_prop(&bi[0], NW_BOOTINFO_TREE_MAX, node, "#buttons", &v, &vl) &&
			      vl == 4 && nw_be32_load(v, 0) == 3);
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
		CHECK(strcmp(nw_boot_line_credits(), "NewWorld Boot by Bill Cavalieri") == 0);
		{
			std::vector<uint8_t> img(NW_ROM_SIZE, 0);
			img[8] = 0x07;
			img[9] = 0x7d;
			memcpy(&img[NW_NEWWORLD_SIG_OFFSET], "NewWorld v1.0", 14);
			char line[128];
			nw_format_g0_rom_line(line, sizeof(line), img.data(), img.size(),
					     img.data(), img.size());
			CHECK(strstr(line, "4 MiB unpacked") != NULL);
			CHECK(strstr(line, "077d") != NULL);
			CHECK(strstr(line, "NewWorld v1.0") != NULL);
			const char lzss[] =
				"<CHRP-BOOT>\nh# 000100 constant lzss-offset\nh# 000200 constant lzss-size\n";
			nw_format_g0_rom_line(line, sizeof(line), img.data(), img.size(),
					     (const uint8_t *)lzss, sizeof(lzss) - 1);
			CHECK(strstr(line, "lzss") != NULL);
			CHECK(strstr(line, "1.6") != NULL);
			const char prcl[] =
				"<CHRP-BOOT>\nh# 000100 constant parcels-offset\nh# 000200 constant parcels-size\n";
			nw_format_g0_rom_line(line, sizeof(line), img.data(), img.size(),
					     (const uint8_t *)prcl, sizeof(prcl) - 1);
			CHECK(strstr(line, "parcels") != NULL);
			CHECK(strstr(line, "9.2.1 tbxi") != NULL);
		}
		CHECK(strcmp(nw_boot_line_g0_newworld(),
			"G0: DecodeROM 4 MiB NewWorld +0x30d064 NK +0x310000") == 0);
		{
			/* Contiguous CHRP tbxi inside a fake volume image. */
			std::vector<uint8_t> img(512, 0xaa);
			const char hdr[] =
				"<CHRP-BOOT>\rh# 000080 constant parcels-offset\r"
				"h# 000010 constant parcels-size\r";
			const size_t at = 64;
			memcpy(&img[at], hdr, sizeof(hdr) - 1);
			img[at + 0x80] = 'p';
			img[at + 0x81] = 'r';
			img[at + 0x82] = 'c';
			img[at + 0x83] = 'l';
			size_t off = 0, span = 0;
			CHECK(nw_chrp_rom_span(img.data(), img.size(), &off, &span));
			CHECK(off == at);
			CHECK(span == 0x90u);
			uint32_t poff = 0, psz = 0;
			CHECK(nw_chrp_payload_range(&img[at], img.size() - at, &poff, &psz));
			CHECK(poff == 0x80u && psz == 0x10u);
		}
		{
			const char *vol = getenv("SHEEP_OS921_CDROM");
			char home_vol[512];
			if (vol == NULL || vol[0] == 0) {
				const char *home = getenv("HOME");
				if (home) {
					snprintf(home_vol, sizeof(home_vol),
						 "%s/Downloads/Mac OS 9.2.1.toast", home);
					vol = home_vol;
				}
			}
			if (vol && vol[0]) {
				FILE *tf = fopen(vol, "rb");
				if (tf) {
					fclose(tf);
					uint8_t *chrp = NULL;
					size_t n = 0;
					CHECK(nw_rom_bytes_from_volume_file(vol, &chrp, &n));
					CHECK(chrp != NULL && n > 11 &&
					      memcmp(chrp, "<CHRP-BOOT>", 11) == 0);
					std::vector<uint8_t> unpacked(NW_ROM_SIZE, 0);
					CHECK(nw_decode_rom_image(chrp, n, unpacked.data(),
								  unpacked.size()));
					CHECK(nw_g0_unpacked_ok(unpacked.data(), unpacked.size()));
					free(chrp);
				}
			}
		}
		CHECK(strcmp(nw_boot_line_g1_tree(),
			"G1: tree root compatible MacRISC2 Gestalt 406 /memory /cpus /chosen") == 0);
		CHECK(strcmp(nw_boot_line_g1_kdp(),
			"G1: Hnfo vs mtsdr1: Hnfo BATRangeInit saveKernelDataPtr adjacent") == 0);
		CHECK(strcmp(nw_boot_line_g1_mtsdr1(),
			"G1: Hnfo vs mtsdr1: mtsdr1") == 0);
		CHECK(strcmp(nw_boot_line_g1_hwinit(),
			"G1: HardwareInit handoff NK +0x310000 ConfigInfo +0x30d000") == 0);
		CHECK(strcmp(nw_boot_line_g1_patch_skip(),
			"G1: NewWorld patched (68k EMUL_OP + drivers); skip Old World 68k boot") == 0);
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

	/* S4 step 6: device models behind nw_io (OpenPIC, Keylargo timer, uni-n). */
	{
		struct nw_devices_clock clk;
		clk.ticks = fake_tb_ticks;
		clk.ctx = NULL;
		clk.hz = 25000000u;
		g_fake_tb = 0;
		nw_io_reset();
		nw_devices_init(&clk);
		const uint32_t P = NW_IO_OPENPIC_BASE;
		/* little-endian bus: a plain lwz of FRR sees the bytes reversed */
		CHECK(nw_io_read(P + NW_OPENPIC_FRR, 4, 0) == 0x02003f00u);
		CHECK(nw_openpic_read(NW_OPENPIC_FRR) == (uint32_t)NW_OPENPIC_FRR_VALUE);
		CHECK(nw_openpic_read(NW_OPENPIC_SRC0 + 5 * 0x20) == 0xa0000000u);	/* reset: masked */
		CHECK(nw_openpic_read(NW_OPENPIC_CPU0 + 0x80) == 15);			/* CTPR */
		CHECK(nw_openpic_read(NW_OPENPIC_CPU0 + 0x90) == 0);			/* WHOAMI */
		CHECK(nw_openpic_read(NW_OPENPIC_CPU0 + 0xa0) == 0xff);			/* IACK idle: spurious */
		/* Program source 5: level, priority 8, vector 0x25, routed to CPU 0. */
		nw_openpic_write(NW_OPENPIC_SRC0 + 5 * 0x20, 0x00480025u | NW_OPENPIC_IVPR_SENSE);
		nw_openpic_write(NW_OPENPIC_SRC0 + 5 * 0x20 + 0x10, 1);
		nw_openpic_write(NW_OPENPIC_CPU0 + 0x80, 0);
		CHECK(nw_io_ext_irq == 0);
		nw_openpic_set_irq(5, 1);
		CHECK(nw_io_ext_irq == 1);
		CHECK(nw_openpic_read(NW_OPENPIC_SRC0 + 5 * 0x20) & NW_OPENPIC_IVPR_ACTIVITY);
		/* CTPR at or above the priority hides it; lowering it re-raises. */
		nw_openpic_write(NW_OPENPIC_CPU0 + 0x80, 8);
		CHECK(nw_io_ext_irq == 0);
		nw_openpic_write(NW_OPENPIC_CPU0 + 0x80, 0);
		CHECK(nw_io_ext_irq == 1);
		/* IACK: vector, line drops; same-priority stays hidden until EOI. */
		CHECK(nw_openpic_read(NW_OPENPIC_CPU0 + 0xa0) == 0x25);
		CHECK(nw_io_ext_irq == 0);
		nw_openpic_set_irq(5, 0);					/* handler clears the device */
		nw_openpic_write(NW_OPENPIC_SRC0 + 6 * 0x20, 0x00480026u | NW_OPENPIC_IVPR_SENSE);
		nw_openpic_write(NW_OPENPIC_SRC0 + 6 * 0x20 + 0x10, 1);
		nw_openpic_set_irq(6, 1);
		CHECK(nw_io_ext_irq == 0);
		/* a higher priority source interrupts the one in service */
		nw_openpic_write(NW_OPENPIC_SRC0 + 7 * 0x20, 0x00090027u);	/* edge, priority 9 */
		nw_openpic_write(NW_OPENPIC_SRC0 + 7 * 0x20 + 0x10, 1);
		nw_openpic_set_irq(7, 1);
		nw_openpic_set_irq(7, 0);
		CHECK(nw_io_ext_irq == 1);
		CHECK(nw_openpic_read(NW_OPENPIC_CPU0 + 0xa0) == 0x27);
		CHECK(nw_io_ext_irq == 0);
		nw_openpic_write(NW_OPENPIC_CPU0 + 0xb0, 0);			/* EOI 7 */
		CHECK(nw_io_ext_irq == 0);					/* 5 still in service, 6 same prio */
		nw_openpic_write(NW_OPENPIC_CPU0 + 0xb0, 0);			/* EOI 5 */
		CHECK(nw_io_ext_irq == 1);					/* 6 pending */
		CHECK(nw_openpic_read(NW_OPENPIC_CPU0 + 0xa0) == 0x26);
		nw_openpic_set_irq(6, 0);
		nw_openpic_write(NW_OPENPIC_CPU0 + 0xb0, 0);
		CHECK(nw_io_ext_irq == 0);
		CHECK(nw_openpic_read(NW_OPENPIC_CPU0 + 0xa0) == 0xff);
		/* masked source never raises the line */
		nw_openpic_write(NW_OPENPIC_SRC0 + 5 * 0x20, 0x80480025u | NW_OPENPIC_IVPR_SENSE);
		nw_openpic_set_irq(5, 1);
		CHECK(nw_io_ext_irq == 0);
		nw_openpic_set_irq(5, 0);
		/* IPI dispatch to self, edge */
		nw_openpic_write(NW_OPENPIC_IPIVPR0, 0x000a0040u);
		nw_openpic_write(NW_OPENPIC_CPU0 + 0x40, 1);
		CHECK(nw_io_ext_irq == 0);					/* IDR 0: no destination */
		/* timer 0: 1000 ticks of the 4.16 MHz clock; expires after 1000/4.16e6 s = 6009 TB ticks */
		nw_openpic_write(NW_OPENPIC_TIMER0 + 0x20, 0x000b0070u);
		nw_openpic_write(NW_OPENPIC_TIMER0 + 0x30, 1);
		nw_openpic_write(NW_OPENPIC_TIMER0 + 0x10, 1000);
		CHECK(nw_openpic_read(NW_OPENPIC_TIMER0) == 1000);
		g_fake_tb += 3000;
		CHECK(nw_openpic_read(NW_OPENPIC_TIMER0) == 1000 - 499);
		nw_devices_tick();
		CHECK(nw_io_ext_irq == 0);
		g_fake_tb += 3100;
		nw_devices_tick();
		CHECK(nw_io_ext_irq == 1);
		CHECK(nw_openpic_read(NW_OPENPIC_CPU0 + 0xa0) == 0x70);
		CHECK(nw_openpic_read(NW_OPENPIC_TIMER0) & NW_OPENPIC_TCCR_TOG);	/* toggled, reloaded */
		nw_openpic_write(NW_OPENPIC_CPU0 + 0xb0, 0);
		nw_openpic_write(NW_OPENPIC_TIMER0 + 0x10, 1000 | NW_OPENPIC_TBCR_CI);	/* inhibit */
		/* display VBL: source NW_VBL_IRQ asserted every 1/60 s once enabled and unmasked (level, prio 3, vector 0x60) */
		nw_openpic_write(NW_OPENPIC_SRC0 + NW_VBL_IRQ * 0x20, 0x00c30060u);
		nw_openpic_write(NW_OPENPIC_SRC0 + NW_VBL_IRQ * 0x20 + 0x10, 1);
		nw_openpic_write(NW_OPENPIC_CPU0 + 0x80, 0);
		g_fake_tb += 25000000u / 60u;
		nw_devices_tick();						/* not enabled by the driver: no VBL */
		CHECK(nw_io_ext_irq == 0);
		nw_display_vbl_enable(1);
		nw_devices_tick();
		CHECK(nw_io_ext_irq == 0);
		g_fake_tb += 25000000u / 60u / 2u;
		nw_devices_tick();
		CHECK(nw_io_ext_irq == 0);					/* half a frame: nothing */
		g_fake_tb += 25000000u / 60u / 2u;
		nw_devices_tick();
		CHECK(nw_io_ext_irq == 1);					/* one frame: VBL */
		CHECK(nw_openpic_read(NW_OPENPIC_CPU0 + 0xa0) == 0x60);
		CHECK(nw_io_ext_irq == 0);					/* in service */
		nw_openpic_write(NW_OPENPIC_CPU0 + 0xb0, 0);
		CHECK(nw_io_ext_irq == 1);					/* level: still asserted until the handler clears it */
		nw_display_vbl_clear();
		CHECK(nw_io_ext_irq == 0);
		g_fake_tb += 25000000u;						/* a second without ticks: no burst, re-arms */
		nw_devices_tick();
		CHECK(nw_io_ext_irq == 0);
		g_fake_tb += 25000000u / 60u;
		nw_devices_tick();
		CHECK(nw_io_ext_irq == 1);
		nw_openpic_read(NW_OPENPIC_CPU0 + 0xa0);
		nw_display_vbl_clear();
		nw_openpic_write(NW_OPENPIC_CPU0 + 0xb0, 0);
		nw_display_vbl_enable(0);
		/* GCR reset returns everything to the reset state */
		nw_openpic_write(NW_OPENPIC_GCR, NW_OPENPIC_GCR_RESET);
		CHECK(nw_openpic_read(NW_OPENPIC_CPU0 + 0x80) == 15);
		CHECK(nw_openpic_read(NW_OPENPIC_SRC0 + 5 * 0x20) == 0xa0000000u);
		CHECK(nw_io_ext_irq == 0);
		/* Keylargo timer: 18.432 MHz from the 25 MHz timebase, LE at +0x38/+0x3c */
		g_fake_tb = 25000000u * 3u;				/* 3 s */
		CHECK(nw_keylargo_timer_read(0x38) == 18432000u * 3u);
		CHECK(nw_keylargo_timer_read(0x3c) == 0);
		CHECK(nw_io_read(NW_IO_KEYLARGO_TIMER_BASE + 0x38, 4, 0) == 0x00c04b03u);	/* 0x034bc000 reversed */
		g_fake_tb = 0x100000000ull * 25u / 18u;			/* past 2^32 counter ticks */
		CHECK(nw_keylargo_timer_read(0x3c) == 1);
		/* uni-n: version at +0, other registers read back */
		CHECK(nw_io_read(NW_IO_UNIN_BASE, 4, 0) == (uint32_t)NW_UNIN_VERSION);
		nw_io_write(NW_IO_UNIN_BASE + 0x40, 4, 0x12345678u, 0);
		CHECK(nw_io_read(NW_IO_UNIN_BASE + 0x40, 4, 0) == 0x12345678u);
		CHECK(nw_io_read(NW_IO_UNIN_BASE + 0x41, 1, 0) == 0x34u);
		nw_io_write(NW_IO_UNIN_BASE, 4, 0xffffffffu, 0);
		CHECK(nw_io_read(NW_IO_UNIN_BASE, 4, 0) == (uint32_t)NW_UNIN_VERSION);
		/* uni-north PCI: CONFIG_ADDR latches and reads back (WaitForZeroPCI spins on this) */
		CHECK(nw_io_read(NW_IO_PCI_CONFIG_ADDR, 4, 0) == 0);
		nw_io_write(NW_IO_PCI_CONFIG_ADDR, 4, 0x56656761u, 0);		/* 'Vega', a plain stw */
		CHECK(nw_io_read(NW_IO_PCI_CONFIG_ADDR, 4, 0) == 0x56656761u);
		CHECK(nw_pci_config_addr() == 0x61676556u);			/* little-endian view */
		/* CFA0: IDSEL = AD[slot]. A stwbrx puts the LSB first, so the value
		 * a plain 4-byte write carries is the little-endian word reversed. */
		nw_io_write(NW_IO_PCI_CONFIG_ADDR, 4, 0x00080000u, 0);		/* LE 0x00000800: slot 0xb (host bridge), reg 0 */
		CHECK(nw_pci_config_addr() == 0x00000800u);
		CHECK(nw_pci_config_read(0, 0xb << 3, 0, 4) == 0x001f106bu);
		CHECK(nw_io_read(NW_IO_PCI_CONFIG_DATA, 4, 0) == 0x6b101f00u);	/* bytes 6b 10 1f 00 as lwz sees them */
		CHECK(nw_io_read(NW_IO_PCI_CONFIG_DATA, 2, 0) == 0x6b10u);
		CHECK(nw_io_read(NW_IO_PCI_CONFIG_DATA + 2, 2, 0) == 0x1f00u);
		nw_io_write(NW_IO_PCI_CONFIG_ADDR, 4, 0x00100000u, 0);		/* LE 0x00001000: slot 0xc (mac-io) */
		CHECK(nw_pci_config_read(0, 0xc << 3, 0, 4) == 0x0022106bu);
		CHECK(nw_pci_config_read(0, 0xc << 3, 8, 4) == 0xff000000u);	/* class */
		CHECK(nw_pci_config_read(0, 0xc << 3, 0x10, 4) == 0x80000000u);	/* BAR0: register aperture */
		nw_io_write(NW_IO_PCI_CONFIG_ADDR, 4, 0x10100000u, 0);		/* LE 0x00001010: reg 0x10 */
		CHECK(nw_io_read(NW_IO_PCI_CONFIG_DATA, 4, 0) == 0x00000080u);	/* 00 00 00 80 -> lwz 0x00000080 */
		nw_io_write(NW_IO_PCI_CONFIG_ADDR, 4, 0x00000100u, 0);		/* LE 0x00010000: slot 16, empty */
		CHECK(nw_io_read(NW_IO_PCI_CONFIG_DATA, 4, 0) == 0xffffffffu);
		CHECK(nw_io_read(NW_IO_PCI_CONFIG_DATA, 1, 0) == 0xffu);
		CHECK(nw_pci_config_read(0, 0xe << 3, 0, 4) == 0x0010106bu);	/* display */
		/* CFA1: bus 0, devfn 0x60, reg 8 */
		nw_io_write(NW_IO_PCI_CONFIG_ADDR, 4, 0x09600000u, 0);		/* LE 0x00006009 */
		CHECK(nw_io_read(NW_IO_PCI_CONFIG_DATA, 4, 0) == 0x000000ffu);	/* class ff000000 LE: 00 00 00 ff */
		nw_io_write(NW_IO_PCI_CONFIG_ADDR, 4, 0x09600100u, 0);		/* LE 0x00016009: bus 1, nothing there */
		CHECK(nw_io_read(NW_IO_PCI_CONFIG_DATA, 4, 0) == 0xffffffffu);
	}

	/* S4 step 6: VIA-PMU and Keylargo GPIO. */
	{
		struct nw_devices_clock clk;
		clk.ticks = fake_tb_ticks;
		clk.ctx = NULL;
		clk.hz = 25000000u;
		g_fake_tb = 0;
		const time_t t0 = time(NULL);
		nw_io_reset();
		nw_devices_init(&clk);
		/* 6522 reset state */
		CHECK(via_rd(NW_VIA_B) == (NW_PMU_TACK | NW_PMU_TREQ));
		CHECK(via_rd(NW_VIA_DIRB) == 0xff);
		CHECK(via_rd(NW_VIA_DIRA) == 0);
		CHECK(via_rd(NW_VIA_IER) == 0x80);
		CHECK(via_rd(NW_VIA_IFR) == 0);
		CHECK(via_rd(NW_VIA_T1CH) == 0xff);
		CHECK(nw_io_read(NW_IO_VIA_PMU_BASE + (NW_VIA_DIRB << NW_VIA_REG_SHIFT), 4, 0) == 0xff000000u);
		/* IER: bit 7 selects set/clear */
		via_wr(NW_VIA_IER, 0x84);
		CHECK(via_rd(NW_VIA_IER) == 0x84);
		via_wr(NW_VIA_IER, 0x04);
		CHECK(via_rd(NW_VIA_IER) == 0x80);
		/* the ROM driver's setup sequence */
		via_wr(NW_VIA_IER, 0x7f);
		via_wr(NW_VIA_DIRB, 0x30);
		via_wr(NW_VIA_ACR, 0x1c);
		via_wr(NW_VIA_PCR, 0);
		via_wr(NW_VIA_B, 0x38);
		CHECK(via_rd(NW_VIA_B) == 0x38);
		CHECK(nw_pmu_state() == 0);
		/* GET_VERSION: no arguments, one response byte */
		pmu_xfer_ok = 1;
		pmu_send(NW_PMU_GET_VERSION);
		CHECK(nw_pmu_state() == 2);
		CHECK(pmu_recv() == 1);
		CHECK(nw_pmu_state() == 0);
		CHECK(pmu_xfer_ok);
		CHECK(via_rd(NW_VIA_IFR) == 0);				/* SR reads cleared SR_INT */
		/* READ_RTC: seconds since 1904 from the host clock */
		pmu_send(NW_PMU_READ_RTC);
		uint32_t rtc = 0;
		for (int i = 0; i < 4; i++)
			rtc = (rtc << 8) | pmu_recv();
		CHECK(nw_pmu_state() == 0);
		const uint32_t want = (uint32_t)t0 + (uint32_t)NW_PMU_RTC_OFFSET;
		CHECK(rtc - want <= 2);
		/* SET_RTC then READ_RTC a minute later */
		pmu_send(NW_PMU_SET_RTC);
		pmu_send(0x12); pmu_send(0x34); pmu_send(0x56); pmu_send(0x78);
		CHECK(nw_pmu_state() == 0);
		g_fake_tb = 60ull * 25000000u;
		pmu_send(NW_PMU_READ_RTC);
		rtc = 0;
		for (int i = 0; i < 4; i++)
			rtc = (rtc << 8) | pmu_recv();
		CHECK(rtc == 0x12345678u + 60);
		/* variable-length command with a length byte: a malformed autopoll
		 * packet (2 bytes, needs 4) is dropped without a reply or interrupt */
		pmu_send(NW_PMU_ADB_CMD);
		pmu_send(2); pmu_send(0x00); pmu_send(0x86);
		CHECK(nw_pmu_state() == 0);
		CHECK(nw_io_read(NW_IO_MACIO_GPIO_BASE + 8 + 1, 1, 0) == NW_GPIO_IN_DATA);
		/* unknown command with a fixed response length answers zeros */
		pmu_send(0x68);						/* {0, 3} */
		CHECK(pmu_recv() == 0);
		CHECK(pmu_recv() == 0);
		CHECK(pmu_recv() == 0);
		CHECK(nw_pmu_state() == 0);
		/* POWER_EVENTS get-wakeup: variable length both ways, two bytes back */
		pmu_send(NW_PMU_POWER_EVENTS);
		pmu_send(1); pmu_send(0x03);
		CHECK(nw_pmu_state() == 2);
		CHECK(pmu_recv() == 2);
		CHECK(pmu_recv() == 0);
		CHECK(pmu_recv() == 0);
		CHECK(nw_pmu_state() == 0);
		CHECK(pmu_xfer_ok);
		/* VIA IRQ -> OpenPIC 0x19 when an enabled IFR bit is set */
		nw_openpic_write(NW_OPENPIC_SRC0 + NW_PMU_IRQ * 0x20, 0x00480019u | NW_OPENPIC_IVPR_SENSE);
		nw_openpic_write(NW_OPENPIC_SRC0 + NW_PMU_IRQ * 0x20 + 0x10, 1);
		nw_openpic_write(NW_OPENPIC_CPU0 + 0x80, 0);
		via_wr(NW_VIA_IER, 0x84);					/* enable SR */
		via_wr(NW_VIA_ACR, 0x1c);
		via_wr(NW_VIA_SR, NW_PMU_GET_COVER);
		pmu_handshake();						/* SR_INT raised, not yet read */
		/* (T1/T2 have been free-running since reset; their IFR bits are set too) */
		CHECK((via_rd(NW_VIA_IFR) & 0x84) == 0x84);
		CHECK(nw_io_ext_irq == 1);
		CHECK(nw_openpic_read(NW_OPENPIC_CPU0 + 0xa0) == 0x19);
		via_rd(NW_VIA_SR);
		CHECK(nw_io_ext_irq == 0);
		nw_openpic_write(NW_OPENPIC_CPU0 + 0xb0, 0);
		pmu_release();
		CHECK(pmu_recv() == 0);						/* GET_COVER: one byte, lid open */
		CHECK(nw_pmu_state() == 0);
		via_wr(NW_VIA_IER, 0x04);
		/* one-second tick -> PMU interrupt on GPIO 1 (low) -> OpenPIC 0x2f.
		 * The timebase sits at 60 s and no tick has run yet: one tick, not sixty. */
		nw_openpic_write(NW_OPENPIC_SRC0 + NW_GPIO1_IRQ * 0x20, 0x0048002fu | NW_OPENPIC_IVPR_SENSE);
		nw_openpic_write(NW_OPENPIC_SRC0 + NW_GPIO1_IRQ * 0x20 + 0x10, 1);
		CHECK(nw_io_read(NW_IO_MACIO_GPIO_BASE + 8 + 1, 1, 0) == NW_GPIO_IN_DATA);
		nw_devices_tick();
		CHECK(nw_io_read(NW_IO_MACIO_GPIO_BASE + 8 + 1, 1, 0) == 0);
		CHECK(nw_io_ext_irq == 1);
		CHECK(nw_openpic_read(NW_OPENPIC_CPU0 + 0xa0) == 0x2f);
		pmu_send(NW_PMU_INT_ACK);					/* variable-length reply: count, then the bits */
		CHECK(pmu_recv() == 1);
		CHECK(pmu_recv() == NW_PMU_INT_TICK);
		CHECK(nw_pmu_state() == 0);
		CHECK(nw_io_read(NW_IO_MACIO_GPIO_BASE + 8 + 1, 1, 0) == NW_GPIO_IN_DATA);
		nw_openpic_write(NW_OPENPIC_CPU0 + 0xb0, 0);
		CHECK(nw_io_ext_irq == 0);
		g_fake_tb += 25000000u / 2;
		nw_devices_tick();
		CHECK(nw_io_read(NW_IO_MACIO_GPIO_BASE + 8 + 1, 1, 0) == NW_GPIO_IN_DATA);	/* not yet */
		/* masking the tick keeps the line idle */
		pmu_send(NW_PMU_SET_INTR_MASK);
		pmu_send(NW_PMU_INT_ADB);
		g_fake_tb += 25000000u;
		nw_devices_tick();
		CHECK(nw_io_read(NW_IO_MACIO_GPIO_BASE + 8 + 1, 1, 0) == NW_GPIO_IN_DATA);
		CHECK(nw_io_ext_irq == 0);
		CHECK(pmu_xfer_ok);
		/* T2: one-shot from the latch, fires at zero at 1276 Hz */
		via_wr(NW_VIA_T2CL, 0x0c);
		via_wr(NW_VIA_T2CH, 0x03);
		CHECK(via_rd(NW_VIA_T2CH) == 0x03);
		CHECK(via_rd(NW_VIA_T2CL) == 0x0c);
		via_wr(NW_VIA_IER, 0x80 | NW_VIA_IFR_T2);
		g_fake_tb += (uint64_t)0x100 * 25000000u / NW_VIA_T2_HZ;
		nw_devices_tick();
		CHECK((via_rd(NW_VIA_IFR) & (0x80 | NW_VIA_IFR_T2)) == 0);
		CHECK(via_rd(NW_VIA_T2CH) == 0x02);
		g_fake_tb += (uint64_t)0x210 * 25000000u / NW_VIA_T2_HZ;
		nw_devices_tick();
		CHECK((via_rd(NW_VIA_IFR) & ~NW_VIA_IFR_T1) == (0x80 | NW_VIA_IFR_T2));
		CHECK(nw_io_ext_irq == 1);
		via_rd(NW_VIA_T2CL);						/* clears T2 */
		CHECK((via_rd(NW_VIA_IFR) & ~NW_VIA_IFR_T1) == 0);
		CHECK(nw_io_ext_irq == 0);
		via_wr(NW_VIA_IER, NW_VIA_IFR_T2);
		/* GPIO pin registers: output enable mirrors out data into the input bit; levels read-only */
		nw_io_write(NW_IO_MACIO_GPIO_BASE + 8 + 5, 1, NW_GPIO_OUT_ENABLE | NW_GPIO_OUT_DATA, 0);
		CHECK(nw_io_read(NW_IO_MACIO_GPIO_BASE + 8 + 5, 1, 0) == 0x07);
		nw_io_write(NW_IO_MACIO_GPIO_BASE + 8 + 5, 1, NW_GPIO_IN_DATA, 0);	/* input again: level keeps, in bit not writable */
		CHECK(nw_io_read(NW_IO_MACIO_GPIO_BASE + 8 + 5, 1, 0) == NW_GPIO_IN_DATA);
		nw_gpio_set(5, 0);
		CHECK(nw_io_read(NW_IO_MACIO_GPIO_BASE + 8 + 5, 1, 0) == 0);
		nw_io_write(NW_IO_MACIO_GPIO_BASE + 2, 1, 0xff, 0);
		CHECK(nw_io_read(NW_IO_MACIO_GPIO_BASE + 2, 1, 0) == 0);
		/* Absent SCC / ATA / FCR: same as the unclaimed default (0, writes dropped). */
		CHECK(nw_io_read(NW_IO_SCC_LEGACY_BASE, 1, 0) == 0);
		nw_io_write(NW_IO_SCC_LEGACY_BASE, 1, 0xa0, 0);
		CHECK(nw_io_read(NW_IO_SCC_LEGACY_BASE, 1, 0) == 0);
		CHECK(nw_io_read(NW_IO_ATA0_BASE + 0x60, 1, 0) == 0);
		nw_io_write(NW_IO_ATA0_BASE + 0x60, 1, 0xa0, 0);
		CHECK(nw_io_read(NW_IO_ATA0_BASE + 0x60, 1, 0) == 0);
		CHECK(nw_io_read(NW_IO_ATA1_BASE + 0x60, 1, 0) == 0);
		nw_io_write(NW_IO_KEYLARGO_FCR_BASE, 4, 0x0000cc10u, 0);
		CHECK(nw_io_read(NW_IO_KEYLARGO_FCR_BASE, 4, 0) == 0);

		g_test_pmu_power_ev = -1;
		nw_pmu_set_power_hook(test_pmu_power_hook, NULL);
		pmu_send(NW_PMU_RESET);
		CHECK(nw_pmu_state() == 0);
		CHECK(g_test_pmu_power_ev == -1);
		nw_devices_tick();
		CHECK(g_test_pmu_power_ev == NW_PMU_POWER_RESTART);
		g_test_pmu_power_ev = -1;
		pmu_send(NW_PMU_SHUTDOWN);
		pmu_send('M');
		pmu_send('A');
		pmu_send('T');
		pmu_send('T');
		CHECK(pmu_recv() == 0);
		CHECK(g_test_pmu_power_ev == -1);
		nw_devices_tick();
		CHECK(g_test_pmu_power_ev == NW_PMU_POWER_OFF);
		nw_pmu_set_power_hook(NULL, NULL);

		g_test_pmu_power_ev = -1;
		pmu_send(NW_PMU_RESET);
		CHECK(nw_pmu_state() == 0);
		nw_devices_tick();
		CHECK(g_test_pmu_power_ev == -1);
		nw_pmu_set_power_hook(test_pmu_power_hook, NULL);
		nw_devices_tick();
		CHECK(g_test_pmu_power_ev == -1);
		nw_pmu_set_power_hook(NULL, NULL);

		/* ADB behind the PMU (interrupt mask is ADB only here). Packets are
		 * {cmd, flags, len, data...}; every packet answers through the PMU
		 * interrupt: INT_ACK -> {0x10, 0x01, len, data...} or {0x10, 0x00}. */
		pmu_send(NW_PMU_ADB_CMD); pmu_send(3); pmu_send(0x00); pmu_send(0x00); pmu_send(0);	/* bus reset */
		CHECK(nw_pmu_state() == 0);
		CHECK(nw_io_read(NW_IO_MACIO_GPIO_BASE + 8 + 1, 1, 0) == 0);			/* reply pending */
		pmu_send(NW_PMU_INT_ACK);
		CHECK(pmu_recv() == 2);
		CHECK(pmu_recv() == NW_PMU_INT_ADB);
		CHECK(pmu_recv() == 0x00);							/* no data */
		CHECK(nw_pmu_state() == 0);
		CHECK(nw_io_read(NW_IO_MACIO_GPIO_BASE + 8 + 1, 1, 0) == NW_GPIO_IN_DATA);
		/* Talk R3 at 2: the keyboard, handler 1 */
		pmu_send(NW_PMU_ADB_CMD); pmu_send(3); pmu_send(0x2f); pmu_send(0x00); pmu_send(0);
		pmu_send(NW_PMU_INT_ACK);
		CHECK(pmu_recv() == 5);
		CHECK(pmu_recv() == NW_PMU_INT_ADB);
		CHECK(pmu_recv() == 0x01);
		CHECK(pmu_recv() == 2);
		CHECK(pmu_recv() == (0x60 | NW_ADB_KBD_ADDR));		/* flags: no exceptional event, SRQ enabled */
		CHECK(pmu_recv() == 1);
		/* Listen R3 at 3: move the mouse to address 7, handler 2 */
		pmu_send(NW_PMU_ADB_CMD); pmu_send(5); pmu_send(0x3b); pmu_send(0x00); pmu_send(2); pmu_send(0x07); pmu_send(0x02);
		pmu_send(NW_PMU_INT_ACK);
		CHECK(pmu_recv() == 2);
		CHECK(pmu_recv() == NW_PMU_INT_ADB);
		CHECK(pmu_recv() == 0x00);
		pmu_send(NW_PMU_ADB_CMD); pmu_send(3); pmu_send(0x7f); pmu_send(0x00); pmu_send(0);
		pmu_send(NW_PMU_INT_ACK);
		CHECK(pmu_recv() == 5);
		CHECK(pmu_recv() == NW_PMU_INT_ADB);
		CHECK(pmu_recv() == 0x01);
		CHECK(pmu_recv() == 2);
		CHECK(pmu_recv() == (0x60 | 7));
		CHECK(pmu_recv() == 2);
		pmu_send(NW_PMU_ADB_CMD); pmu_send(3); pmu_send(0x3f); pmu_send(0x00); pmu_send(0);	/* 3 is empty now */
		pmu_send(NW_PMU_INT_ACK);
		CHECK(pmu_recv() == 2);
		CHECK(pmu_recv() == NW_PMU_INT_ADB);
		CHECK(pmu_recv() == 0x00);
		/* autopoll 2 and 7; nothing queued: quiet after a period */
		pmu_send(NW_PMU_ADB_CMD); pmu_send(4); pmu_send(0x00); pmu_send(0x86); pmu_send(0x00); pmu_send(0x84);
		CHECK(nw_pmu_state() == 0);
		g_fake_tb += 25000000u / 40;
		nw_devices_tick();
		CHECK(nw_io_read(NW_IO_MACIO_GPIO_BASE + 8 + 1, 1, 0) == NW_GPIO_IN_DATA);
		/* host key press and release: one per poll, the keyboard polled again while it has data */
		nw_adb_key(0x1f, 1);
		nw_adb_key(0x1f, 0);
		nw_devices_tick();
		CHECK(nw_io_read(NW_IO_MACIO_GPIO_BASE + 8 + 1, 1, 0) == NW_GPIO_IN_DATA);	/* not before the period */
		g_fake_tb += 25000000u / 40;
		nw_devices_tick();
		CHECK(nw_io_read(NW_IO_MACIO_GPIO_BASE + 8 + 1, 1, 0) == 0);
		pmu_send(NW_PMU_INT_ACK);
		CHECK(pmu_recv() == 4);
		CHECK(pmu_recv() == (NW_PMU_INT_ADB | NW_PMU_INT_ADB_AUTO));
		CHECK(pmu_recv() == 0x2c);							/* Talk R0, keyboard */
		CHECK(pmu_recv() == 0x1f);
		CHECK(pmu_recv() == 0xff);
		CHECK(nw_io_read(NW_IO_MACIO_GPIO_BASE + 8 + 1, 1, 0) == NW_GPIO_IN_DATA);
		g_fake_tb += 25000000u / 40;
		nw_devices_tick();
		pmu_send(NW_PMU_INT_ACK);
		CHECK(pmu_recv() == 4);
		CHECK(pmu_recv() == (NW_PMU_INT_ADB | NW_PMU_INT_ADB_AUTO));
		CHECK(pmu_recv() == 0x2c);
		CHECK(pmu_recv() == 0x9f);
		CHECK(pmu_recv() == 0xff);
		/* mouse: a move with the primary button down */
		nw_adb_mouse_move(5, -3);
		nw_adb_mouse_button(0, 1);
		g_fake_tb += 25000000u / 40;
		nw_devices_tick();
		pmu_send(NW_PMU_INT_ACK);
		CHECK(pmu_recv() == 4);
		CHECK(pmu_recv() == (NW_PMU_INT_ADB | NW_PMU_INT_ADB_AUTO));
		CHECK(pmu_recv() == 0x7c);							/* Talk R0, mouse at 7 */
		CHECK(pmu_recv() == 0x7d);							/* dy -3, button down */
		CHECK(pmu_recv() == 0x85);							/* dx 5, secondary up */
		/* poll off: queued input waits for an explicit Talk R0 */
		pmu_send(NW_PMU_ADB_POLL_OFF);
		nw_adb_key(0x00, 1);
		g_fake_tb += 25000000u / 40;
		nw_devices_tick();
		CHECK(nw_io_read(NW_IO_MACIO_GPIO_BASE + 8 + 1, 1, 0) == NW_GPIO_IN_DATA);
		pmu_send(NW_PMU_ADB_CMD); pmu_send(3); pmu_send(0x2c); pmu_send(0x00); pmu_send(0);
		pmu_send(NW_PMU_INT_ACK);
		CHECK(pmu_recv() == 5);
		CHECK(pmu_recv() == NW_PMU_INT_ADB);
		CHECK(pmu_recv() == 0x01);
		CHECK(pmu_recv() == 2);
		CHECK(pmu_recv() == 0x00);
		CHECK(pmu_recv() == 0xff);
		CHECK(nw_pmu_state() == 0);
		CHECK(pmu_xfer_ok);
	}

	/* S4 step 6: the Trampoline's OpenPIC programming, then the 68k
	 * StartInit's mask/unmask (bset/bclr #7 on the first byte of the LE
	 * register through a big-endian move.l), then a VIA interrupt. */
	{
		struct nw_devices_clock clk;
		clk.ticks = fake_tb_ticks;
		clk.ctx = NULL;
		clk.hz = 25000000u;
		g_fake_tb = 0;
		nw_io_reset();
		nw_devices_init(&clk);
		nw_trampoline_program_pic();
		const uint32_t via_ivpr = NW_IO_OPENPIC_BASE + NW_OPENPIC_SRC0 + NW_PMU_IRQ * 0x20;
		/* vector = position in the ConfigInfo source list (AAPL,interrupt-index),
		 * which the NK uses to index the +0xf00 level table: 0x2f -> 0, 0x37 -> 1, 0x19 -> 2 */
		CHECK(nw_openpic_read(NW_OPENPIC_SRC0 + NW_PMU_IRQ * 0x20) == 0x80c10002u);	/* masked, level, prio 1, vector 2 */
		CHECK(nw_openpic_read(NW_OPENPIC_SRC0 + NW_PMU_IRQ * 0x20 + 0x10) == 1);
		CHECK(nw_openpic_read(NW_OPENPIC_SRC0 + NW_GPIO9_IRQ * 0x20) == 0x80870001u);	/* edge, prio 7 */
		CHECK(nw_openpic_read(NW_OPENPIC_SRC0 + 0x2f * 0x20) == 0x80c20000u);
		CHECK(nw_openpic_read(NW_OPENPIC_SRC0 + 0x0d * 0x20) == 0x80c20003u);	/* ata-3 bus 0: list position 3 */
		CHECK(nw_openpic_read(NW_OPENPIC_SRC0 + NW_VBL_IRQ * 0x20) == 0x80c20007u);	/* display VBL: level, prio 2, position 7 */
		CHECK(nw_openpic_read(NW_OPENPIC_SRC0 + 0x08 * 0x20) == 0xa0000000u);		/* not in the list: reset value */
		CHECK(nw_openpic_read(NW_OPENPIC_CPU0 + 0x80) == 0);
		/* ConfigInfo tail agrees with the programmed table: +0xf80[i] == src, +0xf00[i] == prio */
		CHECK(nw_trampoline_irqs[0].src == 0x2f && nw_trampoline_irqs[2].src == 0x19 && nw_trampoline_irqs[2].prio == 1);
		{
			uint8_t ci[NW_CI_SIZE];
			struct nw_config_info_layout lay;
			memset(&lay, 0, sizeof(lay));
			lay.rom_base = 0x50000000u; lay.rom_area_size = 0x500000u;
			lay.ram_base = 0x10000000u; lay.ram_size = 0x10000000u;
			lay.ci_pa = 0x5030d000u;
			memset(ci, 0, sizeof(ci));
			nw_be32_store(ci, 0x9c, 0x5fffe000u);	/* LA_InfoRecord */
			nw_be32_store(ci, 0xa0, 0x68ffe000u);	/* LA_KernelData */
			nw_be32_store(ci, 0xa4, 0x68fff000u);	/* LA_EmulatorData */
			nw_be32_store(ci, 0xa8, 0x68080000u);	/* LA_DispatchTable */
			nw_be32_store(ci, 0xac, 0x68060000u);	/* LA_EmulatorCode */
			CHECK(nw_fill_config_info_be(ci, &lay) > 0);
			int ok = 1;
			for (int i = 0; i < NW_TRAMPOLINE_NIRQ; i++) {
				const uint32_t ivpr = nw_openpic_read(NW_OPENPIC_SRC0 + nw_trampoline_irqs[i].src * 0x20);
				if ((ivpr & 0xff) != (uint32_t)i || ci[0xf80 + 2 * i + 1] != nw_trampoline_irqs[i].src ||
				    ci[0xf00 + (ivpr & 0xff)] != nw_trampoline_irqs[i].prio || ci[0xf00 + i] == 0)
					ok = 0;
			}
			CHECK(ok);
			CHECK(ci[0xf00 + NW_TRAMPOLINE_NIRQ] == 0 && ci[0xf80 + 2 * NW_TRAMPOLINE_NIRQ] == 0xff);
		}
		/* 68k: move.l (a0,d1.w),d2 ; bset.b #7,d2 ; move.l d2,(a0,d1.w) */
		uint32_t v = nw_io_read(via_ivpr, 4, 0);
		CHECK(v == 0x0200c180u);
		nw_io_write(via_ivpr, 4, v | 0x80, 0);
		CHECK(nw_openpic_read(NW_OPENPIC_SRC0 + NW_PMU_IRQ * 0x20) == 0x80c10002u);
		nw_io_write(via_ivpr, 4, v & ~0x80u, 0);						/* bclr.b #7 */
		CHECK(nw_openpic_read(NW_OPENPIC_SRC0 + NW_PMU_IRQ * 0x20) == 0x00c10002u);
		/* VIA T2 expiry now reaches the CPU with vector 2 */
		via_wr(NW_VIA_T2CL, 0x10);
		via_wr(NW_VIA_T2CH, 0x00);
		via_wr(NW_VIA_IER, 0x80 | NW_VIA_IFR_T2);
		CHECK(nw_io_ext_irq == 0);
		g_fake_tb += (uint64_t)0x20 * 25000000u / NW_VIA_T2_HZ;
		nw_devices_tick();
		CHECK(nw_io_ext_irq == 1);
		CHECK(nw_openpic_read(NW_OPENPIC_CPU0 + 0xa0) == 2);
		via_rd(NW_VIA_T2CL);
		nw_openpic_write(NW_OPENPIC_CPU0 + 0xb0, 0);
		CHECK(nw_io_ext_irq == 0);
		CHECK(nw_openpic_read(NW_OPENPIC_CPU0 + 0xa0) == 0xff);
	}

	/* S4 step 9: NVRAM flash (nw_nvram) as the ROM's nvram,flash ndrv drives it. */
	{
		static const uint8_t wiki[] = "Wikipedia";
		CHECK(nw_nvram_adler32(wiki, 9) == 0x11e60398u);

		nw_io_reset();
		nw_nvram_init(NULL);
		const uint8_t *img = nw_nvram_image();
		/* a fresh machine: OF's image in bank A, bank B erased */
		CHECK(nw_nvram_bank_generation(img) == 1);
		CHECK(nw_nvram_bank_generation(img + NW_NVRAM_BANK_SIZE) == 0);
		CHECK(img[NW_NVRAM_BANK_SIZE] == 0xff && img[NW_NVRAM_SIZE - 1] == 0xff);
		CHECK(img[0] == NW_NVRAM_SIG_CORE99 && img[1] == nw_nvram_chrp_checksum(img));
		CHECK(img[0x20] == NW_NVRAM_SIG_COMMON && memcmp(img + 0x24, "common", 7) == 0);
		CHECK(img[0x13f0] == NW_NVRAM_SIG_MACOS75 && memcmp(img + 0x13f4, "APL,MacOS75", 12) == 0);
		CHECK(img[0x13f0 + 2] == 0x00 && img[0x13f0 + 3] == 0x50);		/* the Trampoline's 0x50 units */
		CHECK(img[0x18f0] == NW_NVRAM_SIG_FREE && img[0x18f0 + 2] == 0x00 && img[0x18f0 + 3] == 0x71);
		/* the whole 16 MiB ROM window aliases the chip: reg and the driver's address agree */
		CHECK(nw_io_read(0xff004000u, 1, 0) == NW_NVRAM_SIG_CORE99);
		CHECK(nw_io_read(0xfff04000u, 1, 0) == NW_NVRAM_SIG_CORE99);
		CHECK(nw_io_read(0xff104000u, 4, 0) == 0x5a000000u + (img[1] << 16) + 0x0002u);
		CHECK(nw_io_read(0xff003fffu, 1, 0) == 0);						/* rest of the chip: not modelled */
		CHECK(nw_io_read(0xff006000u, 1, 0) == 0xff);
		/* erase bank B as the driver does: 0x20 0xd0 at its first byte, poll at 0xff004000, 0xff */
		nw_io_write(0xff006000u, 1, NW_FLASH_CMD_ERASE_SETUP, 0);
		nw_io_write(0xff006000u, 1, NW_FLASH_CMD_ERASE_CONFIRM, 0);
		CHECK(nw_io_read(0xff004000u, 1, 0) == NW_FLASH_STATUS_READY);
		nw_io_write(0xff004000u, 1, NW_FLASH_CMD_READ_ARRAY, 0);
		CHECK(nw_io_read(0xff004000u, 1, 0) == NW_NVRAM_SIG_CORE99);	/* bank A untouched */
		CHECK(nw_io_read(0xff006000u, 1, 0) == 0xff && nw_io_read(0xff007fffu, 1, 0) == 0xff);
		/* program: 0x40 <data>, bits only clear */
		nw_io_write(0xff006001u, 1, NW_FLASH_CMD_PROGRAM, 0);
		nw_io_write(0xff006001u, 1, 0xf0, 0);
		CHECK(nw_io_read(0xff006001u, 1, 0) == NW_FLASH_STATUS_READY);
		nw_io_write(0xff006001u, 1, NW_FLASH_CMD_PROGRAM, 0);
		nw_io_write(0xff006001u, 1, 0x0f, 0);
		nw_io_write(0xff004000u, 1, NW_FLASH_CMD_READ_ARRAY, 0);
		CHECK(nw_io_read(0xff006001u, 1, 0) == 0x00);
		/* a broken sequence reports, clear status recovers */
		nw_io_write(0xff006000u, 1, NW_FLASH_CMD_ERASE_SETUP, 0);
		nw_io_write(0xff006000u, 1, 0x55, 0);
		CHECK(nw_io_read(0xff004000u, 1, 0) == (NW_FLASH_STATUS_READY | NW_FLASH_STATUS_ERASE_ERR | NW_FLASH_STATUS_PROGRAM_ERR));
		nw_io_write(0xff004000u, 1, NW_FLASH_CMD_CLEAR_STATUS, 0);
		CHECK(nw_io_read(0xff004000u, 1, 0) == NW_FLASH_STATUS_READY);
		nw_io_write(0xff004000u, 1, NW_FLASH_CMD_READ_ARRAY, 0);

		/* a commit: erase bank B, program a generation-5 image, persist, come back */
		static const char *path = "/tmp/nw-nvram-harness.flash";
		remove(path);
		nw_io_reset();
		nw_nvram_init(path);
		img = nw_nvram_image();
		uint8_t gen5[NW_NVRAM_BANK_SIZE];
		nw_nvram_format_bank(gen5, 5);
		gen5[0x1400] = 0x03;	/* an XPRAM byte */
		{
			uint8_t hdr[16];
			memcpy(hdr, gen5, 16);
			CHECK(nw_nvram_chrp_checksum(hdr) == gen5[1]);
			CHECK(nw_nvram_bank_generation(gen5) == 0);	/* adler stale after the poke */
			uint32_t adler = nw_nvram_adler32(gen5 + 0x14, NW_NVRAM_BANK_SIZE - 0x14);
			gen5[0x10] = adler >> 24; gen5[0x11] = adler >> 16; gen5[0x12] = adler >> 8; gen5[0x13] = adler;
			CHECK(nw_nvram_bank_generation(gen5) == 5);
		}
		nw_io_write(0xff006000u, 1, NW_FLASH_CMD_ERASE_SETUP, 0);
		nw_io_write(0xff006000u, 1, NW_FLASH_CMD_ERASE_CONFIRM, 0);
		nw_io_write(0xff004000u, 1, NW_FLASH_CMD_READ_ARRAY, 0);
		for (uint32_t i = 0; i < NW_NVRAM_BANK_SIZE; i++) {
			nw_io_write(0xff006000u + i, 1, NW_FLASH_CMD_PROGRAM, 0);
			nw_io_write(0xff006000u + i, 1, gen5[i], 0);
		}
		nw_io_write(0xff004000u, 1, NW_FLASH_CMD_READ_ARRAY, 0);
		CHECK(memcmp(img + NW_NVRAM_BANK_SIZE, gen5, NW_NVRAM_BANK_SIZE) == 0);
		CHECK(nw_nvram_bank_generation(img + NW_NVRAM_BANK_SIZE) == 5);
		nw_io_reset();
		nw_nvram_init(path);		/* next start: the file has both banks */
		img = nw_nvram_image();
		CHECK(nw_nvram_bank_generation(img) == 1 && nw_nvram_bank_generation(img + NW_NVRAM_BANK_SIZE) == 5);
		CHECK(nw_io_read(0xff007400u, 1, 0) == 0x03);

		/* an image without APL,MacOS75 (OF alone): carved out of the free partition as the Trampoline does */
		{
			uint8_t bank[NW_NVRAM_BANK_SIZE];
			nw_nvram_format_bank(bank, 7);
			/* fold APL,MacOS75 back into the free partition: 0x50 + 0x71 units */
			memset(bank + 0x13f0, 0, 16);
			bank[0x13f0] = NW_NVRAM_SIG_FREE; bank[0x13f0 + 2] = 0x00; bank[0x13f0 + 3] = 0xc1;
			bank[0x13f0 + 1] = nw_nvram_chrp_checksum(bank + 0x13f0);
			memset(bank + 0x1400, 0, NW_NVRAM_BANK_SIZE - 0x1400);
			uint32_t adler = nw_nvram_adler32(bank + 0x14, NW_NVRAM_BANK_SIZE - 0x14);
			bank[0x10] = adler >> 24; bank[0x11] = adler >> 16; bank[0x12] = adler >> 8; bank[0x13] = adler;
			CHECK(nw_nvram_bank_generation(bank) == 7);
			FILE *f = fopen(path, "wb");
			CHECK(f != NULL);
			if (f) {
				fwrite(bank, 1, NW_NVRAM_BANK_SIZE, f);
				memset(bank, 0xff, NW_NVRAM_BANK_SIZE);
				fwrite(bank, 1, NW_NVRAM_BANK_SIZE, f);
				fclose(f);
			}
			nw_io_reset();
			nw_nvram_init(path);
			img = nw_nvram_image();
			CHECK(nw_nvram_bank_generation(img) == 7);
			CHECK(img[0x13f0] == NW_NVRAM_SIG_MACOS75 && memcmp(img + 0x13f4, "APL,MacOS75", 12) == 0);
			CHECK(img[0x13f0 + 3] == 0x50 && img[0x18f0] == NW_NVRAM_SIG_FREE && img[0x18f0 + 3] == 0x71);
		}
		nw_nvram_exit();
		remove(path);
	}

	/* WP4: one physical decode (nw_pa_kind) for RAM/ROM/Sheep/FB/KDP/I/O. */
	{
		struct nw_devices_clock clk;
		clk.ticks = fake_tb_ticks;
		clk.ctx = NULL;
		clk.hz = 25000000u;
		nw_io_reset();
		nw_banks_set(NW_PA_RAM, 0x10000000u, 0x20000000u);
		nw_banks_set(NW_PA_ROM, 0x50000000u, 0x500000u);
		nw_banks_set(NW_PA_SHEEP, 0x50510000u, 0x80000u);
		nw_banks_set(NW_PA_FB, 0x50590000u, 0x12c000u);
		nw_banks_set(NW_PA_LOWMEM, 0, 0x4000u);
		nw_banks_set(NW_PA_KDP, 0x68ffe000u, 0x2000u);
		nw_banks_set(NW_PA_BOOTINFO, 0x64000000u, 0x180000u);
		nw_devices_init(&clk);
		nw_nvram_init(NULL);
		CHECK(nw_pa_kind(0x10000000u) == NW_PA_RAM);
		CHECK(nw_pa_kind(0x2fffffffu) == NW_PA_RAM);
		CHECK(nw_pa_kind(0x30000000u) == NW_PA_NONE);
		CHECK(nw_pa_kind(0x50000000u) == NW_PA_ROM);
		CHECK(nw_pa_kind(0x504fffffu) == NW_PA_ROM);
		CHECK(nw_pa_kind(0x50500000u) == NW_PA_NONE);
		CHECK(nw_pa_kind(0x50510000u) == NW_PA_SHEEP);
		CHECK(nw_pa_kind(0x5058ffffu) == NW_PA_SHEEP);
		CHECK(nw_pa_kind(0x50590000u) == NW_PA_FB);
		CHECK(nw_pa_kind(0x506bbfffu) == NW_PA_FB);
		CHECK(nw_pa_kind(0) == NW_PA_LOWMEM);
		CHECK(nw_pa_kind(0x3fffu) == NW_PA_LOWMEM);
		CHECK(nw_pa_kind(0x4000u) == NW_PA_NONE);
		CHECK(nw_pa_kind(0x68ffe000u) == NW_PA_KDP);
		CHECK(nw_pa_kind(0x64000000u) == NW_PA_BOOTINFO);
		CHECK(nw_pa_kind(NW_IO_VIA_PMU_BASE) == NW_PA_IO);
		CHECK(nw_pa_kind(NW_IO_OPENPIC_BASE) == NW_PA_IO);
		CHECK(nw_pa_kind(NW_IO_UNIN_BASE) == NW_PA_IO);
		CHECK(nw_pa_kind(NW_IO_SCC_LEGACY_BASE) == NW_PA_IO);
		CHECK(nw_pa_kind(NW_IO_ATA0_BASE) == NW_PA_IO);
		CHECK(nw_pa_writable(0x10000000u));
		CHECK(nw_pa_writable(0x50590000u));

		/* WP5: a store in the FB bank marks the 64-pixel tile; RAM does not. */
		nw_fb_damage_layout(0x50590000u, 2560u, 640u, 480u, 4u);
		nw_fb_damage_clear();
		CHECK(nw_fb_damage_any() == 0);
		nw_fb_damage_store(0x10000000u, 4);
		CHECK(nw_fb_damage_any() == 0);
		nw_fb_damage_store(0x50590000u, 4);
		CHECK(nw_fb_damage_any());
		{
			int x[4], y[4], w[4], h[4];
			CHECK(nw_fb_damage_collect(x, y, w, h, 4) == 1);
			CHECK(x[0] == 0 && y[0] == 0 && w[0] == 64 && h[0] == 64);
		}
		nw_fb_damage_clear();
		CHECK(nw_fb_damage_any() == 0);
		nw_fb_damage_store(0x50590000u + 63u * 4u, 8);
		{
			int x[4], y[4], w[4], h[4];
			CHECK(nw_fb_damage_collect(x, y, w, h, 4) == 2);
			CHECK(x[0] == 0 && y[0] == 0);
			CHECK(x[1] == 64 && y[1] == 0);
		}
		/* Cross-row: last pixel of row 63 onto first pixel of row 64.
		 * Same-row stores miss this (x1 < x0, w <= 0, mark nothing). */
		nw_fb_damage_clear();
		CHECK(nw_fb_damage_any() == 0);
		nw_fb_damage_store(0x50590000u + 63u * 2560u + 639u * 4u, 8);
		{
			int x[4], y[4], w[4], h[4];
			CHECK(nw_fb_damage_collect(x, y, w, h, 4) == 2);
			CHECK(x[0] == 576 && y[0] == 0);
			CHECK(x[1] == 0 && y[1] == 64);
		}
		nw_fb_damage_rect(200, 100, 10, 10);
		{
			int x[4], y[4], w[4], h[4];
			CHECK(nw_fb_damage_collect(x, y, w, h, 4) == 1);
			CHECK(x[0] == 200 && y[0] == 100 && w[0] == 10 && h[0] == 10);
		}
		/* Pixmap base inside the FB (window / QT dest != screen_base). */
		nw_fb_damage_clear();
		nw_fb_damage_pixmap(0x50590000u + 64u * 4u, 0, 0, 8, 8);
		{
			int x[4], y[4], w[4], h[4];
			CHECK(nw_fb_damage_take(x, y, w, h, 4) == 1);
			CHECK(x[0] == 64 && y[0] == 0 && w[0] == 8 && h[0] == 8);
			CHECK(nw_fb_damage_any() == 0);
		}
		nw_fb_damage_pixmap(0x10000000u, 0, 0, 8, 8);
		CHECK(nw_fb_damage_any() == 0);
		{
			uint8_t px[4];
			uint8_t r, g, b;
			nw_fb_pack_mac32(px, 0x11, 0x22, 0x33);
			CHECK(px[0] == 0 && px[1] == 0x11 && px[2] == 0x22 && px[3] == 0x33);
			nw_fb_mac32_rgb(px, &r, &g, &b);
			CHECK(r == 0x11 && g == 0x22 && b == 0x33);
			uint8_t pal[256 * 3];
			memset(pal, 0, sizeof(pal));
			pal[7 * 3] = 0xaa; pal[7 * 3 + 1] = 0xbb; pal[7 * 3 + 2] = 0xcc;
			uint8_t src[2] = {7, 0};
			uint8_t dst[8];
			memset(dst, 0xff, sizeof(dst));
			nw_fb_expand_clut8_to_mac32(dst, src, 2, pal);
			CHECK(dst[0] == 0 && dst[1] == 0xaa && dst[2] == 0xbb && dst[3] == 0xcc);
			CHECK(dst[4] == 0 && dst[5] == 0 && dst[6] == 0 && dst[7] == 0);
		}
		CHECK(!nw_pa_writable(0x50000000u));
		CHECK(!nw_pa_writable(NW_IO_VIA_PMU_BASE));
		CHECK(!nw_pa_writable(0x30000000u));
		int nflash = 0;
		for (int i = 0; i < NW_NVRAM_ALIASES; i++) {
			uint32_t pa = NW_NVRAM_FLASH_BASE + (uint32_t)i * NW_NVRAM_ALIAS_STRIDE + NW_NVRAM_FLASH_OFFSET;
			CHECK(nw_pa_kind(pa) == NW_PA_IO);
			CHECK(nw_pa_kind(pa + NW_NVRAM_SIZE - 1) == NW_PA_IO);
			nflash++;
		}
		CHECK(nflash == 16);
		int ndev_flash = 0;
		for (int i = 0; i < nw_io_n_devices(); i++) {
			const struct nw_io_device *d = nw_io_device(i);
			if (d && d->name && strcmp(d->name, "nvram-flash") == 0)
				ndev_flash++;
		}
		CHECK(ndev_flash == 16);
		CHECK(nw_io_n_devices() == 11 + 16);	/* 11 mac-io models + 16 flash aliases */
		/* a store to ROM must not reach host memory: the interpreter
		 * drops when !nw_pa_writable, which is 0 for the ROM bank. */
		uint8_t rom_sent[4] = { 0xaa, 0xbb, 0xcc, 0xdd };
		CHECK(!nw_pa_writable(0x50000000u));
		CHECK(rom_sent[0] == 0xaa);
	}

	/* WP3 4a: ARM64 JIT matches the C oracle on the NK idle integer subset. */
	{
		nw_jit_reset();
		struct nw_jit_cpu a, b;
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		uint32_t ops[8];
		ops[0] = nw_ppc_addi(3, 0, 42);
		ops[1] = nw_ppc_add(4, 3, 3, 0);
		ops[2] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 3, 0x1000u) == 1);
		CHECK(a.gpr[3] == 42 && a.gpr[4] == 84 && a.pc == 0x2000u);
		nw_jit_fn fn = nw_jit_compile(ops, 3, 0x1000u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(b.gpr[3] == a.gpr[3] && b.gpr[4] == a.gpr[4] && b.pc == a.pc && b.lr == a.lr);

		/* cntlzw: 0x00ff0000 → 8; 0 → 32 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0x00ff0000u;
		ops[0] = nw_ppc_cntlzw(4, 3, 0);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1690u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1690u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 8 && b.gpr[4] == 8);
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		ops[0] = nw_ppc_cntlzw(4, 3, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x16a0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x16a0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 32 && (a.cr >> 28) == 4 &&
		      b.gpr[4] == a.gpr[4] && b.cr == a.cr);

		/* neg r4, r3: 5 → -5 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 5;
		ops[0] = nw_ppc_neg(4, 3, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x16b0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x16b0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == (uint32_t)-5 && (a.cr >> 28) == 8 &&
		      b.gpr[4] == a.gpr[4] && b.cr == a.cr);

		/* xor r4, r3, r5 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0xaa;
		a.gpr[5] = 0xff;
		ops[0] = nw_ppc_xor(4, 3, 5);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x16c0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x16c0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0x55 && b.gpr[4] == 0x55);

		/* xori r4, r3, 0xff */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0xaa;
		ops[0] = nw_ppc_xori(4, 3, 0xff);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1850u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1850u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0x55 && b.gpr[4] == 0x55);

		/* xoris r4, r3, 0x00ff */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0xaa0000u;
		ops[0] = nw_ppc_xoris(4, 3, 0xff);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1860u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1860u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0x00550000u && b.gpr[4] == a.gpr[4]);

		/* oris r4, r3, 0x00ff */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0x11000000u;
		ops[0] = nw_ppc_oris(4, 3, 0xff);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1864u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1864u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0x11ff0000u && b.gpr[4] == a.gpr[4]);

		/* andis. r4, r3, 0x0ff0 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0xff000000u;
		ops[0] = nw_ppc_andis_dot(4, 3, 0x0ff0);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1870u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1870u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0x0f000000u && (a.cr >> 28) == 4 &&
		      b.gpr[4] == a.gpr[4] && b.cr == a.cr);

		/* and. r4, r3, r5 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0xff00;
		a.gpr[5] = 0x0ff0;
		ops[0] = nw_ppc_and(4, 3, 5, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x16e0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x16e0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0x0f00 && (a.cr >> 28) == 4 &&
		      b.gpr[4] == a.gpr[4] && b.cr == a.cr);

		/* andc r4, r3, r5 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0xff00;
		a.gpr[5] = 0x0ff0;
		ops[0] = nw_ppc_andc(4, 3, 5, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1840u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1840u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0xf000 && (a.cr >> 28) == 4 &&
		      b.gpr[4] == a.gpr[4] && b.cr == a.cr);

		/* subf r4, r3, r5: r5 - r3 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 3;
		a.gpr[5] = 10;
		ops[0] = nw_ppc_subf(4, 3, 5, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1610u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1610u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 7 && (a.cr >> 28) == 4 &&
		      b.gpr[4] == a.gpr[4] && b.cr == a.cr);

		/* addis r4, r3, 1 → r3 + 0x10000; addis r4, r0, -1 → 0xffff0000 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 5;
		ops[0] = nw_ppc_addis(4, 3, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x15c0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x15c0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0x10005u && b.gpr[4] == 0x10005u);
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		ops[0] = nw_ppc_addis(4, 0, -1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x15d0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x15d0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0xffff0000u && b.gpr[4] == 0xffff0000u);

		/* mulli r4, r3, 6 and a negative SIMM */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 7;
		ops[0] = nw_ppc_mulli(4, 3, 6);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1500u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1500u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 42 && b.gpr[4] == 42);
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 5;
		ops[0] = nw_ppc_mulli(4, 3, -3);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1510u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1510u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == (uint32_t)-15 && b.gpr[4] == a.gpr[4]);

		/* mullw r4, r3, r5 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 7;
		a.gpr[5] = 6;
		ops[0] = nw_ppc_mullw(4, 3, 5, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1820u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1820u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 42 && (a.cr >> 28) == 4 &&
		      b.gpr[4] == a.gpr[4] && b.cr == a.cr);

		/* mulhwu r4, r3, r5: high 32 of unsigned product */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0x80000000u;
		a.gpr[5] = 0x80000000u;
		ops[0] = nw_ppc_mulhwu(4, 3, 5, 0);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1830u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1830u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0x40000000u && b.gpr[4] == a.gpr[4]);

		/* add. records CR0 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		ops[0] = nw_ppc_addi(3, 0, -1);
		ops[1] = nw_ppc_add(4, 3, 3, 1);
		ops[2] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 3, 0x1000u) == 1);
		fn = nw_jit_compile(ops, 3, 0x1100u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == (uint32_t)-2 && (a.cr >> 28) == 8);
		CHECK(b.gpr[4] == a.gpr[4] && b.cr == a.cr);

		/* or r4, r3, r5 (skip_unsup 26.7 M) */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0x00ff00ffu;
		a.gpr[5] = 0xff00ff00u;
		ops[0] = nw_ppc_or(4, 3, 5);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1300u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1300u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0xffffffffu && b.gpr[4] == a.gpr[4]);

		/* ori r4, r3, 0x00ff */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0x11000000u;
		ops[0] = nw_ppc_ori(4, 3, 0x00ffu);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1310u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1310u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0x110000ffu && b.gpr[4] == a.gpr[4]);

		/* or. records CR0 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		ops[0] = nw_ppc_or(4, 3, 5) | 1u;
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1320u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1320u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK((a.cr >> 28) == 2 && b.cr == a.cr);

		/* bcctr always: NIA = CTR & ~3 */
		memset(&a, 0, sizeof(a));
		a.ctr = 0x3002u;
		a.lr = 0x2000u;
		ops[0] = nw_ppc_bcctr(20, 0);
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 1, 0x1330u) == 1);
		fn = nw_jit_compile(ops, 1, 0x1330u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.pc == 0x3000u && b.pc == a.pc && a.ctr == 0x3002u);

		/* bcctr not taken (CR bit 8 set, BO=5) falls through */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.ctr = 0x3000u;
		a.cr = 0x00800000u;
		ops[0] = nw_ppc_bcctr(5, 8);
		ops[1] = nw_ppc_addi(4, 0, 7);
		ops[2] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 3, 0x1340u) == 1);
		fn = nw_jit_compile(ops, 3, 0x1340u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 7 && b.gpr[4] == 7 && a.pc == 0x2000u && b.pc == a.pc);

		/* stwu r3, -4(r1): store and update r1 */
		{
			uint8_t ram[64];
			memset(ram, 0, sizeof(ram));
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = 64;
			a.gpr[1] = 16;
			a.gpr[3] = 0x11223344u;
			ops[0] = nw_ppc_stwu(3, 1, -4);
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x1350u) == 1);
			fn = nw_jit_compile(ops, 2, 0x1350u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[1] == 12 && b.gpr[1] == 12);
			CHECK(ram[12] == 0x11 && ram[15] == 0x44);
		}

		/* stwu then addi in one block: RA update must be visible. */
		{
			uint8_t ram[64];
			memset(ram, 0, sizeof(ram));
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = 64;
			a.gpr[1] = 16;
			a.gpr[3] = 0x11223344u;
			ops[0] = nw_ppc_stwu(3, 1, -4);
			ops[1] = nw_ppc_addi(4, 1, 0);
			ops[2] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 3, 0x1358u) == 1);
			fn = nw_jit_compile(ops, 3, 0x1358u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[1] == 12 && a.gpr[4] == 12);
			CHECK(b.gpr[1] == 12 && b.gpr[4] == 12);
		}

		/* stwu into the executing page: SMC, RA still updates. */
		{
			uint8_t ram[0x2000];
			memset(ram, 0, sizeof(ram));
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = 0x2000;
			a.gpr[1] = 0x1010u;
			a.gpr[3] = 0xa5a5a5a5u;
			ops[0] = nw_ppc_stwu(3, 1, -4);
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x1000u) == 1);
			fn = nw_jit_compile(ops, 2, 0x1000u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[1] == 0x100cu && b.gpr[1] == 0x100cu);
			CHECK(b.fault == NW_JIT_FAULT_SMC);
		}

		/* lwzx r3, r1, r2 */
		{
			uint8_t ram[64];
			memset(ram, 0, sizeof(ram));
			ram[12] = 0xaa; ram[13] = 0xbb; ram[14] = 0xcc; ram[15] = 0xdd;
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = 64;
			a.gpr[1] = 8;
			a.gpr[2] = 4;
			ops[0] = nw_ppc_lwzx(3, 1, 2);
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x1360u) == 1);
			fn = nw_jit_compile(ops, 2, 0x1360u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[3] == 0xaabbccddu && b.gpr[3] == a.gpr[3]);
		}

		/* lhax r3, r1, r2 */
		{
			uint8_t ram[64];
			memset(ram, 0, sizeof(ram));
			ram[12] = 0x80; ram[13] = 0x00;
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = 64;
			a.gpr[1] = 8;
			a.gpr[2] = 4;
			ops[0] = nw_ppc_lhax(3, 1, 2);
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x1490u) == 1);
			fn = nw_jit_compile(ops, 2, 0x1490u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[3] == 0xffff8000u && b.gpr[3] == a.gpr[3]);
		}

		/* lhaux r3, r1, r2: load and r1 = EA */
		{
			uint8_t ram[64];
			memset(ram, 0, sizeof(ram));
			ram[12] = 0x80; ram[13] = 0x00;
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = 64;
			a.gpr[1] = 8;
			a.gpr[2] = 4;
			ops[0] = nw_ppc_lhaux(3, 1, 2);
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x14a0u) == 1);
			fn = nw_jit_compile(ops, 2, 0x14a0u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[3] == 0xffff8000u && a.gpr[1] == 12 &&
			      b.gpr[3] == a.gpr[3] && b.gpr[1] == 12);
		}

		/* lhaux r3, r3, r2: rD=rA ends as EA, not the loaded halfword */
		{
			uint8_t ram[64];
			memset(ram, 0, sizeof(ram));
			ram[12] = 0x80; ram[13] = 0x00;
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = 64;
			a.gpr[3] = 8;
			a.gpr[2] = 4;
			ops[0] = nw_ppc_lhaux(3, 3, 2);
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x14d0u) == 1);
			fn = nw_jit_compile(ops, 2, 0x14d0u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[3] == 12 && b.gpr[3] == 12);
		}

		/* dtlb: C lookup, fill, store needs WRITE, flush */
		{
			uint32_t pa = 0;
			nw_jit_dtlb_flush();
			CHECK(nw_jit_dtlb_lookup(0x1004u, 0, &pa) == 0);
			nw_jit_dtlb_fill(0x1000u, 0x2000u, 0, 0);
			CHECK(nw_jit_dtlb_lookup(0x1004u, 0, &pa) == 1);
			CHECK(pa == 0x2004u);
			CHECK(nw_jit_dtlb_lookup(0x1004u, 1, &pa) == 0);
			nw_jit_dtlb_fill(0x1000u, 0x2000u, 1, 0);
			CHECK(nw_jit_dtlb_lookup(0x1000u, 1, &pa) == 1);
			CHECK(pa == 0x2000u);
			nw_jit_dtlb_flush();
			CHECK(nw_jit_dtlb_lookup(0x1000u, 0, &pa) == 0);

			/* drop_sr keeps other segments (distinct DTLB slots) */
			nw_jit_dtlb_fill(0x1000u, 0x2000u, 1, 0);
			nw_jit_dtlb_fill(0x10000000u, 0x3000u, 1, 0);
			nw_jit_dtlb_drop_sr(0, NW_JIT_DTLB_FL_MTSR);
			CHECK(nw_jit_dtlb_lookup(0x1000u, 0, &pa) == 0);
			CHECK(nw_jit_dtlb_lookup(0x10000000u, 0, &pa) == 1);
			CHECK(pa == 0x3000u);

			/* drop_bat 128 KiB at 0; SR1 page survives */
			nw_jit_dtlb_fill(0x1000u, 0x2000u, 1, 0);
			nw_jit_dtlb_drop_bat(0x2u, NW_JIT_DTLB_FL_BAT); /* Vs, BL=0 */
			CHECK(nw_jit_dtlb_lookup(0x1000u, 0, &pa) == 0);
			CHECK(nw_jit_dtlb_lookup(0x10000000u, 0, &pa) == 0); /* gen miss */
			CHECK(nw_jit_bat_gen_bumps() >= 1);

			/* drop_page hits one slot */
			nw_jit_dtlb_fill(0x10000000u, 0x3000u, 1, 0);
			nw_jit_dtlb_drop_page(0x10000004u, NW_JIT_DTLB_FL_TLB);
			CHECK(nw_jit_dtlb_lookup(0x10000000u, 0, &pa) == 0);

			/* tlbie r4: drop that page only; other slot stays. */
			CHECK(nw_ppc_tlbie(0) == 0x7c000264u);
			CHECK(nw_jit_op_supported(nw_ppc_tlbie(4)));
			CHECK(nw_jit_op_ends_block(nw_ppc_tlbie(4)));
			nw_jit_dtlb_fill(0x1000u, 0x2000u, 1, 0);
			nw_jit_dtlb_fill(0x10000000u, 0x3000u, 1, 0);
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.gpr[4] = 0x1004u;
			ops[0] = nw_ppc_tlbie(4);
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 1, 0x25b0u) == 1);
			CHECK(nw_jit_dtlb_lookup(0x1000u, 0, &pa) == 0);
			CHECK(nw_jit_dtlb_lookup(0x10000000u, 0, &pa) == 1);
			nw_jit_dtlb_fill(0x1000u, 0x2000u, 1, 0);
			fn = nw_jit_compile(ops, 1, 0x25b0u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(nw_jit_dtlb_lookup(0x1000u, 0, &pa) == 0);
			CHECK(nw_jit_dtlb_lookup(0x10000000u, 0, &pa) == 1);
			CHECK(pa == 0x3000u);

			/* PR tag: kernel fill misses in user, hits in kernel */
			nw_jit_dtlb_fill(0x1000u, 0x2000u, 1, 0, 0);
			CHECK(nw_jit_dtlb_lookup_pr(0x1000u, 0, &pa, 0) == 1);
			CHECK(nw_jit_dtlb_lookup_pr(0x1000u, 0, &pa, 1) == 0);
			nw_jit_dtlb_fill(0x1000u, 0x2000u, 1, 0, 1);
			CHECK(nw_jit_dtlb_lookup_pr(0x1000u, 0, &pa, 1) == 1);
			CHECK(nw_jit_dtlb_lookup_pr(0x1000u, 0, &pa, 0) == 0);

			/* ITLB: hit, miss, tlbie drop, SR VSID miss */
			{
				uint32_t ipa = 0;
				nw_jit_itlb_flush();
				CHECK(nw_jit_itlb_lookup(0x1004u, &ipa) == 0);
				nw_jit_itlb_fill(0x1000u, 0x2000u);
				CHECK(nw_jit_itlb_lookup(0x1004u, &ipa) == 1);
				CHECK(ipa == 0x2004u);
				CHECK(nw_jit_itlb_lookup(0x1004u, &ipa) == 1); /* sticky */
				nw_jit_itlb_drop_page(0x1000u);
				CHECK(nw_jit_itlb_lookup(0x1000u, &ipa) == 0);
				nw_jit_itlb_fill(0x1000u, 0x2000u);
				nw_jit_mtsr_note(0, 0x00000100u, 0x00000100u); /* same VSID */
				CHECK(nw_jit_itlb_lookup(0x1000u, &ipa) == 1);
				nw_jit_mtsr_note(0, 0x00000100u, 0x00000200u); /* VSID change */
				CHECK(nw_jit_itlb_lookup(0x1000u, &ipa) == 0);
			}

			/* DTLB SR v2: T-bit noise does not miss; VSID change does */
			nw_jit_dtlb_flush();
			nw_jit_dtlb_fill(0x1000u, 0x2000u, 1, 0);
			nw_jit_mtsr_note(0, 0x00000100u, 0x80000100u); /* T only */
			CHECK(nw_jit_dtlb_lookup(0x1000u, 0, &pa) == 1);
			nw_jit_mtsr_note(0, 0x00000100u, 0x00000200u); /* VSID */
			CHECK(nw_jit_dtlb_lookup(0x1000u, 0, &pa) == 0);
			CHECK(nw_jit_mtsr_total() >= 2);
			CHECK(nw_jit_mtsr_vsid() >= 1);
			CHECK(nw_jit_cache_slots() == 262144);
			CHECK(NW_JIT_ITLB_N == 256);
			CHECK(NW_JIT_DTLB_N == 1024);

			/* interp mtsrin: T-bit noise keeps DTLB; VSID change misses */
			{
				uint32_t pa2 = 0;
				nw_jit_dtlb_flush();
				nw_jit_dtlb_fill(0x1000u, 0x2000u, 1, 0);
				memset(&a, 0, sizeof(a));
				a.lr = 0x2000u;
				a.sr[0] = 0x00000100u;
				a.gpr[3] = 0x80000100u; /* T only */
				a.gpr[4] = 0;
				ops[0] = nw_ppc_mtsrin(3, 4);
				CHECK(nw_jit_interp_n(&a, ops, 1, 0x2690u) == 1);
				CHECK(nw_jit_dtlb_lookup(0x1000u, 0, &pa2) == 1);
				a.gpr[3] = 0x00000200u; /* VSID */
				CHECK(nw_jit_interp_n(&a, ops, 1, 0x26a0u) == 1);
				CHECK(nw_jit_dtlb_lookup(0x1000u, 0, &pa2) == 0);
			}
		}

		/* two lwz same EA: first miss fills, second hits */
		{
			uint8_t ram[64];
			memset(ram, 0, sizeof(ram));
			ram[8] = 0x11; ram[9] = 0x22; ram[10] = 0x33; ram[11] = 0x44;
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = 64;
			a.gpr[1] = 8;
			a.msr = 0x10u;	/* MSR[DR] so the inlined dtlb runs */
			ops[0] = nw_ppc_lwz(3, 1, 0);
			ops[1] = nw_ppc_lwz(4, 1, 0);
			ops[2] = nw_ppc_blr();
			nw_jit_dtlb_flush();
			{
				const uint64_t mh = nw_jit_dtlb_hits(), mm = nw_jit_dtlb_misses();
				fn = nw_jit_compile(ops, 3, 0x1370u, 0x1000u, 0, 0);
				CHECK(fn != NULL);
				fn(&a);
				CHECK(a.gpr[3] == 0x11223344u && a.gpr[4] == a.gpr[3]);
				CHECK(nw_jit_dtlb_misses() > mm);
				CHECK(nw_jit_dtlb_hits() > mh);
			}
		}

		/* lwzx DSI must not clobber rD (kpx xlate fail leaves rD). */
		{
			uint8_t ram[64];
			memset(ram, 0, sizeof(ram));
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = 64;
			a.gpr[1] = 0x100;
			a.gpr[2] = 0x100;
			a.gpr[3] = 0xdeadbeefu;
			ops[0] = nw_ppc_lwzx(3, 1, 2);
			ops[1] = nw_ppc_blr();
			b = a;
			fn = nw_jit_compile(ops, 2, 0x1368u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(b.fault == 1 && b.gpr[3] == 0xdeadbeefu);
		}

		/* addc sets CA on 0xffffffff+1 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0xffffffffu;
		a.gpr[5] = 1;
		ops[0] = nw_ppc_addc(4, 3, 5, 0);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1370u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1370u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0 && (a.xer & 0x20000000u) && b.gpr[4] == 0 &&
		      (b.xer & 0x20000000u));

		/* addc no carry */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.xer = 0x20000000u;
		a.gpr[3] = 1;
		a.gpr[5] = 1;
		ops[0] = nw_ppc_addc(4, 3, 5, 0);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1380u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1380u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 2 && !(a.xer & 0x20000000u) &&
		      b.gpr[4] == 2 && !(b.xer & 0x20000000u));

		/* addic. r4, r3, -1 records CR0 and CA */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0;
		ops[0] = nw_ppc_addic(4, 3, -1, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1390u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1390u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0xffffffffu && (a.cr >> 28) == 8 &&
		      b.gpr[4] == a.gpr[4] && b.cr == a.cr &&
		      !(a.xer & 0x20000000u) && !(b.xer & 0x20000000u));

		/* addco: 0x7fffffff+1 sets OV+SO, not CA */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0x7fffffffu;
		a.gpr[5] = 1;
		ops[0] = nw_ppc_addco(4, 3, 5, 0);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x13d0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x13d0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0x80000000u && (a.xer & 0xc0000000u) == 0xc0000000u &&
		      !(a.xer & 0x20000000u) && b.gpr[4] == a.gpr[4] && b.xer == a.xer);

		/* addco no overflow: OV cleared, SO sticky */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.xer = 0xc0000000u;
		a.gpr[3] = 1;
		a.gpr[5] = 1;
		ops[0] = nw_ppc_addco(4, 3, 5, 0);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x13e0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x13e0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 2 && (a.xer & 0xc0000000u) == 0x80000000u &&
		      b.gpr[4] == 2 && b.xer == a.xer);

		/* addco. records CR0 SO after overflow */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0x7fffffffu;
		a.gpr[5] = 1;
		ops[0] = nw_ppc_addco(4, 3, 5, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x13f0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x13f0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0x80000000u && (a.cr >> 28) == 9 &&
		      b.gpr[4] == a.gpr[4] && b.cr == a.cr);

		/* andi. */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[4] = 0x1234u;
		ops[0] = nw_ppc_andi_dot(3, 4, 0xff);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1400u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1400u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[3] == 0x34u && (a.cr >> 28) == 4 &&
		      b.gpr[3] == a.gpr[3] && b.cr == a.cr);

		/* subfco: 0-1, CA clear, OV clear */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 1;
		a.gpr[5] = 0;
		ops[0] = nw_ppc_subfco(4, 3, 5, 0);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1410u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1410u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0xffffffffu && !(a.xer & 0x20000000u) &&
		      !(a.xer & 0x40000000u) && b.gpr[4] == a.gpr[4] && b.xer == a.xer);

		/* subfc: 20-5 = 15, CA set, OV unchanged */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 5;
		a.gpr[5] = 20;
		ops[0] = nw_ppc_subfc(4, 3, 5, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1880u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1880u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 15 && (a.xer & 0x20000000u) &&
		      !(a.xer & 0x40000000u) && (a.cr >> 28) == 4 &&
		      b.gpr[4] == a.gpr[4] && b.xer == a.xer && b.cr == a.cr);

		/* subfco 0x80000000-1 sets OV */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 1;
		a.gpr[5] = 0x80000000u;
		ops[0] = nw_ppc_subfco(4, 3, 5, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1420u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1420u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0x7fffffffu && (a.xer & 0xc0000000u) == 0xc0000000u &&
		      (a.cr >> 28) == 5 && b.gpr[4] == a.gpr[4] && b.xer == a.xer &&
		      b.cr == a.cr);

		/* subfe CA=0: ~0 + 0 + 0 = -1, CA clear */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		ops[0] = nw_ppc_subfe(4, 3, 5, 0);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1520u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1520u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0xffffffffu && !(a.xer & 0x20000000u) &&
		      b.gpr[4] == a.gpr[4] && b.xer == a.xer);

		/* subfe CA=1: ~0 + 0 + 1 = 0, CA set */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.xer = 0x20000000u;
		ops[0] = nw_ppc_subfe(4, 3, 5, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1530u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1530u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0 && (a.xer & 0x20000000u) && (a.cr >> 28) == 2 &&
		      b.gpr[4] == a.gpr[4] && b.xer == a.xer && b.cr == a.cr);

		/* cmpli unsigned: 0xffffffff vs 1 is GT, not LT */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0xffffffffu;
		ops[0] = nw_ppc_cmpli(0, 3, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1430u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1430u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK((a.cr >> 28) == 4 && b.cr == a.cr);

		/* cmpli cr3 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0;
		ops[0] = nw_ppc_cmpli(3, 3, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1440u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1440u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(((a.cr >> 16) & 0xf) == 8 && b.cr == a.cr);

		/* cmpl unsigned: 0xffffffff vs 1 is GT, not LT */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0xffffffffu;
		a.gpr[4] = 1;
		ops[0] = nw_ppc_cmpl(0, 3, 4);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x14b0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x14b0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK((a.cr >> 28) == 4 && b.cr == a.cr);

		/* cmpl cr3 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0;
		a.gpr[4] = 1;
		ops[0] = nw_ppc_cmpl(3, 3, 4);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x14c0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x14c0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(((a.cr >> 16) & 0xf) == 8 && b.cr == a.cr);

		/* cmp cr3 signed: 0 vs 1 is LT */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0;
		a.gpr[4] = 1;
		ops[0] = nw_ppc_cmp_cr(3, 3, 4);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1630u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1630u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(((a.cr >> 16) & 0xf) == 8 && b.cr == a.cr);

		/* cmpi cr7: INT_MIN vs 1 is LT, not wrapped GT */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0x80000000u;
		ops[0] = nw_ppc_cmpi_cr(7, 3, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1460u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1460u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK((a.cr & 0xf) == 8 && b.cr == a.cr);

		/* mtcrf CR0 from rS */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.cr = 0x12345678u;
		a.gpr[4] = 0xa0000000u;
		ops[0] = nw_ppc_mtcrf(0x80, 4);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1450u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1450u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.cr == 0xa2345678u && b.cr == a.cr);

		/* mfcr copies CR into rD */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.cr = 0xa2345678u;
		ops[0] = nw_ppc_mfcr(3);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x15e0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x15e0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[3] == 0xa2345678u && b.gpr[3] == a.gpr[3]);

		/* crnor crb0, crb0, crb0: ~ (0|0) = 1 → CR bit 0 set */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		ops[0] = nw_ppc_crnor(0, 0, 0);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1600u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1600u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.cr == 0x80000000u && b.cr == a.cr);

		/* crxor crb31, crb0, crb1: 1 xor 0 = 1 → CR 0x80000001 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.cr = 0x80000000u;
		ops[0] = nw_ppc_crxor(31, 0, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1620u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1620u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.cr == 0x80000001u && b.cr == a.cr);

		/* creqv crb31, crb0, crb0: ~(1 xor 1) = 1 → CR 0x80000001 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.cr = 0x80000000u;
		ops[0] = nw_ppc_creqv(31, 0, 0);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1660u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1660u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.cr == 0x80000001u && b.cr == a.cr);

		/* cror crb31, crb0, crb1: 1 | 0 = 1 → CR 0x80000001 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.cr = 0x80000000u;
		ops[0] = nw_ppc_cror(31, 0, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1720u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1720u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.cr == 0x80000001u && b.cr == a.cr);

		/* crorc crb31, crb0, crb1: 1 | ~0 = 1 → CR 0x80000001 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.cr = 0x80000000u;
		ops[0] = nw_ppc_crorc(31, 0, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1730u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1730u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.cr == 0x80000001u && b.cr == a.cr);

		/* crand crb31, crb0, crb1: 1 & 1 = 1 → CR 0xC0000001 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.cr = 0xC0000000u;
		ops[0] = nw_ppc_crand(31, 0, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1740u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1740u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.cr == 0xC0000001u && b.cr == a.cr);

		/* crandc crb31, crb0, crb1: 1 & ~0 = 1 → CR 0x80000001 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.cr = 0x80000000u;
		ops[0] = nw_ppc_crandc(31, 0, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1750u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1750u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.cr == 0x80000001u && b.cr == a.cr);

		/* crnand crb31, crb0, crb1: ~(1 & 0) = 1 → CR 0x80000001 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.cr = 0x80000000u;
		ops[0] = nw_ppc_crnand(31, 0, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1760u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1760u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.cr == 0x80000001u && b.cr == a.cr);

		/* extsh 0x8000 → 0xffff8000, CR0 LT */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[8] = 0x18000u;
		ops[0] = nw_ppc_extsh(4, 8, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1480u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1480u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0xffff8000u && (a.cr >> 28) == 8 &&
		      b.gpr[4] == a.gpr[4] && b.cr == a.cr);

		/* extsb 0x80 → 0xffffff80, CR0 LT */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[8] = 0x180u;
		ops[0] = nw_ppc_extsb(4, 8, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1540u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1540u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0xffffff80u && (a.cr >> 28) == 8 &&
		      b.gpr[4] == a.gpr[4] && b.cr == a.cr);

		/* slw: 1<<4 = 16; shift count with bit5 set is 0 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[8] = 1;
		a.gpr[9] = 4;
		ops[0] = nw_ppc_slw(4, 8, 9, 0);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1560u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1560u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 16 && b.gpr[4] == 16);
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[8] = 1;
		a.gpr[9] = 32;
		ops[0] = nw_ppc_slw(4, 8, 9, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1570u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1570u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0 && (a.cr >> 28) == 2 &&
		      b.gpr[4] == a.gpr[4] && b.cr == a.cr);

		/* srw: 0x80 >> 4 = 8; bit5 set is 0 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[8] = 0x80;
		a.gpr[9] = 4;
		ops[0] = nw_ppc_srw(4, 8, 9, 0);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1580u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1580u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 8 && b.gpr[4] == 8);
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[8] = 0x80;
		a.gpr[9] = 32;
		ops[0] = nw_ppc_srw(4, 8, 9, 0);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1590u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1590u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0 && b.gpr[4] == 0);

		/* sraw: -1 >> 1 = -1, CA set (1 shifted out) */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[8] = (uint32_t)-1;
		a.gpr[9] = 1;
		ops[0] = nw_ppc_sraw(4, 8, 9, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1650u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1650u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == (uint32_t)-1 && (a.xer & 0x20000000u) &&
		      b.gpr[4] == a.gpr[4] && b.xer == a.xer && b.cr == a.cr);

		/* srawi: -1 >> 1 = -1, CA set */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[8] = (uint32_t)-1;
		ops[0] = nw_ppc_srawi(4, 8, 1, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x16f0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x16f0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == (uint32_t)-1 && (a.xer & 0x20000000u) &&
		      b.gpr[4] == a.gpr[4] && b.xer == a.xer && b.cr == a.cr);

		/* sync is a barrier; GPRs unchanged */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0x1111u;
		ops[0] = nw_ppc_sync();
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x15a0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x15a0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[3] == 0x1111u && b.gpr[3] == 0x1111u);

		/* dss is a nop; GPRs unchanged; does not end the block */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0x3333u;
		ops[0] = nw_ppc_dss();
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1770u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1770u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[3] == 0x3333u && b.gpr[3] == 0x3333u);
		CHECK(nw_jit_op_ends_block(nw_ppc_dss()) == 0);

		/* dst / dstst: kpx prefetch nops; GPRs unchanged; do not end the block */
		CHECK(nw_ppc_dst(3, 4, 1) == 0x7c2322acu);
		CHECK(nw_ppc_dstst(3, 4, 1) == 0x7c2322ecu);
		CHECK(nw_jit_op_supported(nw_ppc_dst(3, 4, 1)));
		CHECK(nw_jit_op_supported(nw_ppc_dstst(3, 4, 1)));
		CHECK(nw_jit_op_supported(nw_ppc_dst(3, 4, 1) | (1u << 25))); /* dstt T=1 */
		CHECK(nw_jit_op_supported(nw_ppc_dstst(3, 4, 1) | (1u << 25)));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0xabcdu;
		a.gpr[4] = 0x1111u;
		ops[0] = nw_ppc_dst(3, 4, 1);
		ops[1] = nw_ppc_dstst(3, 4, 2);
		ops[2] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 3, 0x2200u) == 1);
		fn = nw_jit_compile(ops, 3, 0x2200u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[3] == 0xabcdu && b.gpr[3] == 0xabcdu);
		CHECK(a.gpr[4] == 0x1111u && b.gpr[4] == 0x1111u);
		CHECK(nw_jit_op_ends_block(nw_ppc_dst(3, 4, 1)) == 0);
		CHECK(nw_jit_op_ends_block(nw_ppc_dstst(3, 4, 1)) == 0);

		/* dcbt / eieio: nop / barrier; GPRs unchanged; do not end the block */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0x4444u;
		ops[0] = nw_ppc_dcbt(3, 4);
		ops[1] = nw_ppc_eieio();
		ops[2] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 3, 0x1774u) == 1);
		fn = nw_jit_compile(ops, 3, 0x1774u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[3] == 0x4444u && b.gpr[3] == 0x4444u);
		CHECK(nw_jit_op_ends_block(nw_ppc_dcbt(3, 4)) == 0);
		CHECK(nw_jit_op_ends_block(nw_ppc_eieio()) == 0);

		/* dcbf: kpx nop; GPRs unchanged; do not end the block */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0x5555u;
		ops[0] = nw_ppc_dcbf(3, 4);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1776u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1776u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[3] == 0x5555u && b.gpr[3] == 0x5555u);
		CHECK(nw_jit_op_ends_block(nw_ppc_dcbf(3, 4)) == 0);

		/* dcbz zeros 32 B at (rA|0)+rB through the store path */
		{
			uint8_t ram[128];
			memset(ram, 0xaa, sizeof(ram));
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = sizeof(ram);
			a.gpr[3] = 0x20u;
			a.gpr[4] = 0x10u;
			ops[0] = nw_ppc_dcbz(3, 4);
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x1778u) == 1);
			fn = nw_jit_compile(ops, 2, 0x1778u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			int i;
			for (i = 0x20; i < 0x40; i++)
				CHECK(ram[i] == 0);
			CHECK(ram[0x1f] == 0xaa && ram[0x40] == 0xaa);
		}

		/* twi: no-trap continues; TO=4 (eq) with unequal is a fall-through */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 5;
		ops[0] = nw_ppc_twi(4, 3, 9);
		ops[1] = nw_ppc_addi(5, 0, 1);
		ops[2] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 3, 0x177cu) == 1);
		fn = nw_jit_compile(ops, 3, 0x177cu, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[5] == 1 && b.gpr[5] == 1 && b.fault == 0);

		/* twi trap (TO=4 eq) leaves fault EXC at the twi */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 9;
		ops[0] = nw_ppc_twi(4, 3, 9);
		ops[1] = nw_ppc_addi(5, 0, 1);
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1788u) == 0);
		CHECK(a.fault == NW_JIT_FAULT_EXC && a.pc == 0x1788u);
		fn = nw_jit_compile(ops, 2, 0x1788u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(b.fault == NW_JIT_FAULT_EXC && b.gpr[5] == 0);

		/* mtsr is supported and does not end the block */
		CHECK(nw_jit_op_supported(nw_ppc_mtsr(1, 4)));
		CHECK(nw_jit_op_ends_block(nw_ppc_mtsr(1, 4)));

		/* mtmsr r4: writes MSR from rS; ends the block (IR/DR may change) */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[4] = 0x0002d032u;
		a.msr = 0;
		ops[0] = nw_ppc_mtmsr(4);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1780u) == 1);
		CHECK(a.msr == 0x0002d032u && a.pc == 0x1784u);
		fn = nw_jit_compile(ops, 2, 0x1780u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(b.msr == 0x0002d032u && b.pc == 0x1784u);
		CHECK(nw_jit_op_ends_block(nw_ppc_mtmsr(4)));

		/* isync: host flushes pending icbi; GPRs unchanged; ends block */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0x2222u;
		ops[0] = nw_ppc_isync();
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1710u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1710u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[3] == 0x2222u && b.gpr[3] == 0x2222u);
		CHECK(a.pc == 0x1714u && b.pc == 0x1714u);

		/* mfspr TBL via host helper */
		{
			static uint32_t tbl_val;
			tbl_val = 0x11223344u;
			auto hmfspr = [](void *, uint32_t spr, uint32_t, int *status) -> uint32_t {
				if (status)
					*status = 0;
				return spr == NW_PPC_SPR_TBL ? tbl_val : 0;
			};
			nw_jit_set_host_mfspr(+hmfspr);
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.host = (void *)1;
			ops[0] = nw_ppc_mfspr(3, NW_PPC_SPR_TBL);
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x1470u) == 1);
			fn = nw_jit_compile(ops, 2, 0x1470u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[3] == 0x11223344u && b.gpr[3] == a.gpr[3]);
			nw_jit_set_host_mfspr(NULL);
		}

		/* leftover mfspr NOP must leave rD; EXC must not write rD */
		{
			auto hnop = [](void *, uint32_t, uint32_t, int *status) -> uint32_t {
				if (status)
					*status = 1;
				return 0xdeadu;
			};
			nw_jit_set_host_mfspr(+hnop);
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.host = (void *)1;
			a.gpr[3] = 0xa5a5a5a5u;
			ops[0] = nw_ppc_mfspr(3, 18);
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x1480u) == 1);
			fn = nw_jit_compile(ops, 2, 0x1480u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[3] == 0xa5a5a5a5u && b.gpr[3] == 0xa5a5a5a5u);
			CHECK(a.fault == 0 && b.fault == 0);
			auto hexc = [](void *, uint32_t, uint32_t, int *status) -> uint32_t {
				if (status)
					*status = 2;
				return 0x700u;
			};
			nw_jit_set_host_mfspr(+hexc);
			memset(&a, 0, sizeof(a));
			a.host = (void *)1;
			a.gpr[3] = 0x11u;
			ops[0] = nw_ppc_mfspr(3, 0);
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 1, 0x1490u) == 0);
			fn = nw_jit_compile(ops, 1, 0x1490u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[3] == 0x11u && b.gpr[3] == 0x11u);
			CHECK(a.fault == NW_JIT_FAULT_EXC && b.fault == NW_JIT_FAULT_EXC);
			CHECK(a.pc == 0x700u && b.pc == 0x700u);
			nw_jit_set_host_mfspr(NULL);
		}

		/* lbz / stb */
		{
			uint8_t ram[64];
			memset(ram, 0, sizeof(ram));
			ram[8] = 0xab;
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = 64;
			a.gpr[1] = 8;
			ops[0] = nw_ppc_lbz(3, 1, 0);
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x13a0u) == 1);
			fn = nw_jit_compile(ops, 2, 0x13a0u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[3] == 0xabu && b.gpr[3] == 0xabu);
			a.gpr[4] = 0xcd;
			b = a;
			ops[0] = nw_ppc_stb(4, 1, 1);
			ops[1] = nw_ppc_blr();
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x13b0u) == 1);
			fn = nw_jit_compile(ops, 2, 0x13b0u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(ram[9] == 0xcd);
			ram[12] = 0xef;
			a.gpr[1] = 8;
			a.fault = 0;
			ops[0] = nw_ppc_lbzu(3, 1, 4);
			ops[1] = nw_ppc_blr();
			b = a;
			b.mem = ram;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x17c0u) == 1);
			CHECK(a.gpr[3] == 0xefu);
			CHECK(a.gpr[1] == 12u);
			fn = nw_jit_compile(ops, 2, 0x17c0u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(b.gpr[3] == 0xefu);
			CHECK(b.gpr[1] == 12u);
		}

		/* lvx v3, r1, r2: 16-byte aligned load */
		{
			uint8_t ram[64];
			memset(ram, 0, sizeof(ram));
			for (int i = 0; i < 16; i++)
				ram[16 + i] = (uint8_t)(0xa0 + i);
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = 64;
			a.gpr[1] = 16;
			a.gpr[2] = 0;
			ops[0] = nw_ppc_lvx(3, 1, 2);
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x17d0u) == 1);
			fn = nw_jit_compile(ops, 2, 0x17d0u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.vr[3][0] == 0xa0a1a2a3u && b.vr[3][0] == a.vr[3][0]);
			CHECK(a.vr[3][3] == 0xacadaeafu && b.vr[3][3] == a.vr[3][3]);
			ops[0] = nw_ppc_lvxl(4, 1, 2);
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x2510u) == 1);
			fn = nw_jit_compile(ops, 2, 0x2510u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.vr[4][0] == 0xa0a1a2a3u && b.vr[4][0] == a.vr[4][0]);
			a.gpr[1] = 32;
			ops[0] = nw_ppc_stvx(3, 1, 2);
			ops[1] = nw_ppc_blr();
			b = a;
			b.mem = ram;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x17e0u) == 1);
			fn = nw_jit_compile(ops, 2, 0x17e0u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(ram[32] == 0xa0 && ram[47] == 0xaf);
			a.gpr[1] = 48;
			ops[0] = nw_ppc_stvxl(3, 1, 2);
			ops[1] = nw_ppc_blr();
			b = a;
			b.mem = ram;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x2520u) == 1);
			fn = nw_jit_compile(ops, 2, 0x2520u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(ram[48] == 0xa0 && ram[63] == 0xaf);
		}

		/* lfd f3, 8(r1) */
		{
			uint8_t ram[64];
			memset(ram, 0, sizeof(ram));
			ram[8] = 0x40; ram[9] = 0x09; ram[10] = 0x21; ram[11] = 0xfb;
			ram[12] = 0x54; ram[13] = 0x44; ram[14] = 0x2d; ram[15] = 0x18;
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = 64;
			a.gpr[1] = 0;
			ops[0] = nw_ppc_lfd(3, 1, 8);
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x17f0u) == 1);
			fn = nw_jit_compile(ops, 2, 0x17f0u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.fpr[3] == 0x400921fb54442d18ull &&
			      b.fpr[3] == a.fpr[3]);
			memset(ram, 0, sizeof(ram));
			a.fpr[3] = 0x400921fb54442d18ull;
			a.gpr[1] = 0;
			ops[0] = nw_ppc_stfd(3, 1, 16);
			ops[1] = nw_ppc_blr();
			b = a;
			b.mem = ram;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x1800u) == 1);
			fn = nw_jit_compile(ops, 2, 0x1800u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(ram[16] == 0x40 && ram[23] == 0x18);
			CHECK(b.mem[16] == 0x40 && b.mem[23] == 0x18);

			memset(ram, 0, sizeof(ram));
			ram[8] = 0x40; ram[9] = 0x09; ram[10] = 0x21; ram[11] = 0xfb;
			ram[12] = 0x54; ram[13] = 0x44; ram[14] = 0x2d; ram[15] = 0x18;
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = 64;
			a.gpr[1] = 0;
			a.gpr[2] = 8;
			ops[0] = nw_ppc_lfdx(3, 1, 2);
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x1810u) == 1);
			fn = nw_jit_compile(ops, 2, 0x1810u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.fpr[3] == 0x400921fb54442d18ull &&
			      b.fpr[3] == a.fpr[3]);
			memset(ram, 0, sizeof(ram));
			a.fpr[3] = 0x400921fb54442d18ull;
			a.gpr[1] = 8;
			a.gpr[2] = 8;
			ops[0] = nw_ppc_stfdx(3, 1, 2);
			ops[1] = nw_ppc_blr();
			b = a;
			b.mem = ram;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x1820u) == 1);
			fn = nw_jit_compile(ops, 2, 0x1820u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(ram[16] == 0x40 && ram[23] == 0x18);
			CHECK(b.mem[16] == 0x40 && b.mem[23] == 0x18);
		}

		/* lfs / fsubs / stfs: 3.0f - 1.0f = 2.0f */
		{
			uint8_t ram[64];
			memset(ram, 0, sizeof(ram));
			ram[0] = 0x40; ram[1] = 0x40; ram[2] = 0x00; ram[3] = 0x00; /* 3.0f */
			ram[4] = 0x3f; ram[5] = 0x80; ram[6] = 0x00; ram[7] = 0x00; /* 1.0f */
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = 64;
			ops[0] = nw_ppc_lfs(1, 0, 0);
			ops[1] = nw_ppc_lfs(2, 0, 4);
			ops[2] = nw_ppc_fsubs(3, 1, 2);
			ops[3] = nw_ppc_stfs(3, 0, 8);
			ops[4] = nw_ppc_blr();
			b = a;
			b.mem = ram;
			CHECK(nw_jit_op_supported(nw_ppc_lfs(1, 0, 0)));
			CHECK(nw_ppc_fdivs(0, 0, 0) == 0xec000024u);
			CHECK(nw_ppc_fsubs(0, 0, 0) == 0xec000028u);
			CHECK(nw_ppc_fadds(0, 0, 0) == 0xec00002au);
			CHECK(nw_jit_op_supported(nw_ppc_fsubs(3, 1, 2)));
			CHECK(nw_jit_op_supported(nw_ppc_fadds(3, 1, 2)));
			CHECK(nw_jit_op_supported(nw_ppc_stfs(3, 0, 8)));
			CHECK(nw_jit_op_supported(nw_ppc_lfsx(1, 0, 3)));
			CHECK(nw_jit_op_supported(nw_ppc_stfsx(3, 0, 3)));
			CHECK(nw_jit_op_supported(nw_ppc_fdivs(3, 1, 2)));
			CHECK(nw_jit_op_supported(nw_ppc_fmuls(3, 1, 2)));
			CHECK(nw_jit_op_supported(nw_ppc_fmadds(3, 1, 2, 2)));
			CHECK(nw_jit_op_supported(nw_ppc_fneg(3, 1)));
			CHECK(nw_jit_interp_n(&a, ops, 5, 0x1900u) == 1);
			fn = nw_jit_compile(ops, 5, 0x1900u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(ram[8] == 0x40 && ram[9] == 0x00 && ram[10] == 0x00 && ram[11] == 0x00);
			CHECK(b.mem[8] == 0x40 && b.mem[9] == 0x00);

			/* fadds: 3.0f + 1.0f = 4.0f */
			memset(ram, 0, sizeof(ram));
			ram[0] = 0x40; ram[1] = 0x40; ram[2] = 0x00; ram[3] = 0x00;
			ram[4] = 0x3f; ram[5] = 0x80; ram[6] = 0x00; ram[7] = 0x00;
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = 64;
			ops[0] = nw_ppc_lfs(1, 0, 0);
			ops[1] = nw_ppc_lfs(2, 0, 4);
			ops[2] = nw_ppc_fadds(3, 1, 2);
			ops[3] = nw_ppc_stfs(3, 0, 8);
			ops[4] = nw_ppc_blr();
			b = a;
			b.mem = ram;
			CHECK(nw_jit_interp_n(&a, ops, 5, 0x1910u) == 1);
			fn = nw_jit_compile(ops, 5, 0x1910u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(ram[8] == 0x40 && ram[9] == 0x80 && ram[10] == 0x00 && ram[11] == 0x00);
			CHECK(b.mem[8] == 0x40 && b.mem[9] == 0x80);
		}

		/* stbu r4, 4(r1): store then r1 = EA */
		{
			uint8_t ram[64];
			memset(ram, 0, sizeof(ram));
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = 64;
			a.gpr[1] = 8;
			a.gpr[4] = 0xab;
			ops[0] = nw_ppc_stbu(4, 1, 4);
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x15b0u) == 1);
			fn = nw_jit_compile(ops, 2, 0x15b0u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(ram[12] == 0xab && a.gpr[1] == 12 && b.gpr[1] == 12);
		}

		/* sthu r4, 4(r1): store halfword then r1 = EA */
		{
			uint8_t ram[64];
			memset(ram, 0, sizeof(ram));
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = 64;
			a.gpr[1] = 8;
			a.gpr[4] = 0xabcd;
			ops[0] = nw_ppc_sthu(4, 1, 4);
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x1640u) == 1);
			fn = nw_jit_compile(ops, 2, 0x1640u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(ram[12] == 0xab && ram[13] == 0xcd &&
			      a.gpr[1] == 12 && b.gpr[1] == 12);
		}

		/* lmw r30, 0(r1); stmw r30, 8(r1) */
		{
			uint8_t ram[64];
			memset(ram, 0, sizeof(ram));
			ram[0] = 0x11; ram[1] = 0x22; ram[2] = 0x33; ram[3] = 0x44;
			ram[4] = 0x55; ram[5] = 0x66; ram[6] = 0x77; ram[7] = 0x88;
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = 64;
			ops[0] = nw_ppc_lmw(30, 0, 0);
			ops[1] = nw_ppc_stmw(30, 0, 8);
			ops[2] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 3, 0x1680u) == 1);
			fn = nw_jit_compile(ops, 3, 0x1680u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[30] == 0x11223344u && a.gpr[31] == 0x55667788u);
			CHECK(b.gpr[30] == a.gpr[30] && b.gpr[31] == a.gpr[31]);
			CHECK(ram[8] == 0x11 && ram[15] == 0x88);
		}

		/* lbzx r3, r1, r2 */
		{
			uint8_t ram[64];
			memset(ram, 0, sizeof(ram));
			ram[12] = 0xfe;
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = 64;
			a.gpr[1] = 8;
			a.gpr[2] = 4;
			ops[0] = nw_ppc_lbzx(3, 1, 2);
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x14f0u) == 1);
			fn = nw_jit_compile(ops, 2, 0x14f0u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[3] == 0xfeu && b.gpr[3] == 0xfeu);
		}

		/* lwzu r3, 4(r1) */
		{
			uint8_t ram[64];
			memset(ram, 0, sizeof(ram));
			ram[12] = 0x11; ram[13] = 0x22; ram[14] = 0x33; ram[15] = 0x44;
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = 64;
			a.gpr[1] = 8;
			ops[0] = nw_ppc_lwzu(3, 1, 4);
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x13c0u) == 1);
			fn = nw_jit_compile(ops, 2, 0x13c0u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[1] == 12 && a.gpr[3] == 0x11223344u &&
			      b.gpr[1] == 12 && b.gpr[3] == a.gpr[3]);
		}

		/* rlwinm r4, r3, 8, 0, 23  (shift left 8) */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0x00000011u;
		ops[0] = nw_ppc_rlwinm(4, 3, 8, 0, 23);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1000u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1200u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0x00001100u && b.gpr[4] == a.gpr[4]);

		/* rlwnm r4, r3, r5, 0, 23 with r5=8 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0x00000011u;
		a.gpr[5] = 8;
		ops[0] = nw_ppc_rlwnm(4, 3, 5, 0, 23);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1670u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1670u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0x00001100u && b.gpr[4] == a.gpr[4]);

		/* rlwinm. records CR0 (vs-kpx miss: 540006f7) */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0;
		ops[0] = nw_ppc_rlwinm(4, 3, 0, 0, 31) | 1u;
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1000u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1210u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK((a.cr >> 28) == 2 && b.cr == a.cr && b.gpr[4] == 0);

		/* blrl: LR = pc+4, PC = old LR (vs-kpx miss: 4e800021) */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		ops[0] = nw_ppc_blr() | 1u;
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 1, 0x1220u) == 1);
		fn = nw_jit_compile(ops, 1, 0x1220u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.pc == 0x2000u && a.lr == 0x1224u);
		CHECK(b.pc == a.pc && b.lr == a.lr);
		CHECK(nw_jit_op_supported(nw_ppc_cmpi_cr(7, 3, 0))); /* cmpwi cr7 */

		/* vs-kpx 4e800020: NIA = LR & ~3 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2002u;
		ops[0] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 1, 0x1230u) == 1);
		fn = nw_jit_compile(ops, 1, 0x1230u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.pc == 0x2000u && b.pc == a.pc && b.lr == 0x2002u);

		/* vs-kpx 7c13a000: cmp is signed; wrapped sub of INT_MIN-1 is GT */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0x80000000u;
		a.gpr[4] = 1;
		ops[0] = nw_ppc_cmp(3, 4);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1240u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1240u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK((a.cr >> 28) == 8 && b.cr == a.cr);

		/* stw / lwz through a BE buffer */
		uint8_t ram[64];
		memset(ram, 0, sizeof(ram));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.mem = ram;
		a.mem_base = 0;
		a.mem_size = sizeof(ram);
		a.gpr[3] = 0x11223344u;
		a.gpr[1] = 16;
		ops[0] = nw_ppc_stw(3, 1, 0);
		ops[1] = nw_ppc_lwz(5, 1, 0);
		ops[2] = nw_ppc_blr();
		b = a;
		uint8_t ram2[64];
		memset(ram2, 0, sizeof(ram2));
		b.mem = ram2;
		CHECK(nw_jit_interp_n(&a, ops, 3, 0x1000u) == 1);
		fn = nw_jit_compile(ops, 3, 0x1300u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[5] == 0x11223344u && b.gpr[5] == a.gpr[5]);
		CHECK(ram[16] == 0x11 && ram[19] == 0x44);
		CHECK(ram2[16] == 0x11 && ram2[19] == 0x44);

		/* stwx r3, r1, r2: EA = r1+r2 */
		memset(ram, 0, sizeof(ram));
		memset(ram2, 0, sizeof(ram2));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.mem = ram;
		a.mem_base = 0;
		a.mem_size = sizeof(ram);
		a.gpr[3] = 0xaabbccddu;
		a.gpr[1] = 8;
		a.gpr[2] = 4;
		ops[0] = nw_ppc_stwx(3, 1, 2);
		ops[1] = nw_ppc_lwz(5, 1, 4);
		ops[2] = nw_ppc_blr();
		b = a;
		b.mem = ram2;
		CHECK(nw_jit_interp_n(&a, ops, 3, 0x14e0u) == 1);
		fn = nw_jit_compile(ops, 3, 0x14e0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[5] == 0xaabbccddu && b.gpr[5] == a.gpr[5]);
		CHECK(ram[12] == 0xaa && ram[15] == 0xdd);
		CHECK(ram2[12] == 0xaa && ram2[15] == 0xdd);

		/* sthx r3, r1, r2 */
		memset(ram, 0, sizeof(ram));
		memset(ram2, 0, sizeof(ram2));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.mem = ram;
		a.mem_base = 0;
		a.mem_size = sizeof(ram);
		a.gpr[3] = 0xaabb;
		a.gpr[1] = 8;
		a.gpr[2] = 4;
		ops[0] = nw_ppc_sthx(3, 1, 2);
		ops[1] = nw_ppc_blr();
		b = a;
		b.mem = ram2;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x16d0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x16d0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(ram[12] == 0xaa && ram[13] == 0xbb);
		CHECK(ram2[12] == 0xaa && ram2[13] == 0xbb);

		/* cmp / bc not taken: blt does not fire, both addi run */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		ops[0] = nw_ppc_addi(3, 0, 2);
		ops[1] = nw_ppc_addi(4, 0, 1);
		ops[2] = nw_ppc_cmp(3, 4);
		ops[3] = nw_ppc_bc(NW_PPC_BO_TRUE, 0, 8);	/* blt .+8, not taken */
		ops[4] = nw_ppc_addi(5, 0, 99);
		ops[5] = nw_ppc_addi(5, 0, 1);
		ops[6] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 7, 0x1000u) == 1);
		fn = nw_jit_compile(ops, 7, 0x1400u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[5] == 1 && b.gpr[5] == 1 && b.pc == a.pc && b.cr == a.cr);

		/* bc taken: addi -1, cmpwi, blt skips the 99 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		ops[0] = nw_ppc_addi(3, 0, -1);
		ops[1] = nw_ppc_cmpi(3, 0);
		ops[2] = nw_ppc_bc(NW_PPC_BO_TRUE, 0, 8);
		ops[3] = nw_ppc_addi(5, 0, 99);
		ops[4] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 5, 0x1c00u) == 1);
		fn = nw_jit_compile(ops, 5, 0x1c00u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[5] == 0 && b.gpr[5] == 0 && a.pc == 0x1c10u && b.pc == a.pc);

		/* bdnz: CTR=1 decs to 0, not taken (interp continues; compile ends block) */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.ctr = 1;
		ops[0] = nw_ppc_bc(NW_PPC_BO_BDNZ, 0, 8);
		ops[1] = nw_ppc_addi(5, 0, 99);
		ops[2] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 3, 0x1790u) == 1);
		CHECK(a.ctr == 0 && a.gpr[5] == 99);
		fn = nw_jit_compile(ops, 1, 0x1790u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(b.ctr == 0 && b.pc == 0x1794u);

		/* bdnz: CTR=2 decs to 1, taken, skips addi 99 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.ctr = 2;
		ops[0] = nw_ppc_bc(NW_PPC_BO_BDNZ, 0, 8);
		ops[1] = nw_ppc_addi(5, 0, 99);
		ops[2] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 3, 0x17a0u) == 1);
		fn = nw_jit_compile(ops, 3, 0x17a0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.ctr == 1 && b.ctr == 1);
		CHECK(a.gpr[5] == 0 && b.gpr[5] == 0);
		CHECK(a.pc == 0x17a8u && b.pc == a.pc);

		/* bcl: LK sets LR even when not taken (bdnz CTR=1) */
		memset(&a, 0, sizeof(a));
		a.lr = 0;
		a.ctr = 1;
		ops[0] = nw_ppc_bc(NW_PPC_BO_BDNZ, 0, 8) | 1u;
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x17b0u) == 1);
		CHECK(a.lr == 0x17b4u);
		fn = nw_jit_compile(ops, 1, 0x17b0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(b.lr == 0x17b4u && b.pc == 0x17b4u);

		/* bclr BO=5 not taken (CR bit 8 set) falls through */
		memset(&a, 0, sizeof(a));
		a.lr = 0x3000u;
		a.cr = 0x00800000u;	/* bit 8 */
		ops[0] = nw_ppc_addi(3, 0, 1);
		ops[1] = nw_ppc_bclr(5, 8);
		ops[2] = nw_ppc_addi(4, 0, 7);
		ops[3] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 4, 0x1d00u) == 1);
		fn = nw_jit_compile(ops, 4, 0x1d00u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(b.gpr[3] == 1 && b.gpr[4] == 7 && b.pc == 0x3000u);

		/* mtspr / mfspr DEC */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		ops[0] = nw_ppc_addi(3, 0, 0x55);
		ops[1] = nw_ppc_mtspr(NW_PPC_SPR_DEC, 3);
		ops[2] = nw_ppc_mfspr(4, NW_PPC_SPR_DEC);
		ops[3] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 4, 0x1000u) == 1);
		fn = nw_jit_compile(ops, 4, 0x1500u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.dec == 0x55 && a.gpr[4] == 0x55 && a.dec_wr == 1);
		CHECK(b.dec == a.dec && b.gpr[4] == a.gpr[4] && b.dec_wr == 1);

		/* cache key: same (page, pc, ir, endian) hits; IR/DR/PR distinguish */
		nw_jit_fn fn2 = nw_jit_compile(ops, 4, 0x1500u, 0x1000u, 0, 0);
		CHECK(fn2 == fn);
		nw_jit_fn fn3 = nw_jit_compile(ops, 4, 0x1500u, 0x1000u, 1, 0);
		CHECK(fn3 != NULL && fn3 != fn);
		CHECK(nw_jit_cache_get(0x1000u, 0x1500u, 1, 0, NULL) == fn3);
		CHECK(nw_jit_cache_get(0x1000u, 0x1500u, 1u | 2u, 0, NULL) == NULL);
		CHECK(nw_jit_cache_get(0x1000u, 0x1500u, 1u | 4u, 0, NULL) == NULL);
		nw_jit_fn fn_dr = nw_jit_compile(ops, 4, 0x1500u, 0x1000u, 1u | 2u, 0);
		CHECK(fn_dr != NULL && fn_dr != fn3);
		CHECK(nw_jit_cache_get(0x1000u, 0x1500u, 1, 0, NULL) == fn3);
		nw_jit_fn fn_pr = nw_jit_compile(ops, 4, 0x1500u, 0x1000u, 1u | 4u, 0);
		CHECK(fn_pr != NULL && fn_pr != fn3 && fn_pr != fn_dr);
		uint64_t fl = nw_jit_flush_count();
		nw_jit_invalidate_page(0x1000u);
		CHECK(nw_jit_flush_count() > fl);
		CHECK(nw_jit_compile(ops, 4, 0x1500u, 0x1000u, 0, 0) != fn);

		/* 4c: invalidate_page drops only that phys page; all drops both */
		nw_jit_reset();
		ops[0] = nw_ppc_addi(3, 0, 1);
		ops[1] = nw_ppc_blr();
		nw_jit_fn pa = nw_jit_compile(ops, 2, 0x1000u, 0x1000u, 0, 0);
		nw_jit_fn pb = nw_jit_compile(ops, 2, 0x2000u, 0x2000u, 0, 0);
		CHECK(pa != NULL && pb != NULL && pa != pb);
		CHECK(nw_jit_cache_get(0x1000u, 0x1000u, 0, 0, NULL) == pa);
		CHECK(nw_jit_cache_get(0x2000u, 0x2000u, 0, 0, NULL) == pb);
		nw_jit_invalidate_page(0x1000u);
		CHECK(nw_jit_cache_get(0x1000u, 0x1000u, 0, 0, NULL) == NULL);
		CHECK(nw_jit_cache_get(0x2000u, 0x2000u, 0, 0, NULL) == pb);
		fl = nw_jit_flush_count();
		nw_jit_invalidate_all();
		CHECK(nw_jit_flush_count() > fl);
		CHECK(nw_jit_cache_get(0x2000u, 0x2000u, 0, 0, NULL) == NULL);

		/* interpreter/host store src drops only that page */
		nw_jit_reset();
		pa = nw_jit_compile(ops, 2, 0x1000u, 0x1000u, 0, 0);
		pb = nw_jit_compile(ops, 2, 0x2000u, 0x2000u, 0, 0);
		nw_jit_invalidate_page_src(0x1000u, NW_JIT_FL_ISTORE);
		CHECK(nw_jit_cache_get(0x1000u, 0x1000u, 0, 0, NULL) == NULL);
		CHECK(nw_jit_cache_get(0x2000u, 0x2000u, 0, 0, NULL) == pb);
		nw_jit_invalidate_range_src(0x2000u, 4, NW_JIT_FL_HOST);
		CHECK(nw_jit_cache_get(0x2000u, 0x2000u, 0, 0, NULL) == NULL);

		/* FB is never a code page: a store there must not walk/drop RAM translations. */
		nw_jit_reset();
		nw_banks_set(NW_PA_FB, 0x50590000u, 0x12c000u);
		pa = nw_jit_compile(ops, 2, 0x1000u, 0x1000u, 0, 0);
		CHECK(pa != NULL);
		fl = nw_jit_flush_count();
		nw_jit_invalidate_page_src(0x50590000u, NW_JIT_FL_STORE);
		CHECK(nw_jit_flush_count() == fl);
		CHECK(nw_jit_cache_get(0x1000u, 0x1000u, 0, 0, NULL) == pa);

		/* coldest-of-probe: a hot block survives a flood of new PCs */
		nw_jit_reset();
		nw_jit_set_code_pages(0, 0x1000000u, 0x68000000u, 0x400000u);
		ops[0] = nw_ppc_addi(3, 0, 1);
		ops[1] = nw_ppc_blr();
		nw_jit_fn hot = nw_jit_compile(ops, 2, 0x1000u, 0x1000u, 0, 0);
		CHECK(hot != NULL);
		for (int i = 0; i < 64; i++)
			CHECK(nw_jit_cache_get(0x1000u, 0x1000u, 0, 0, NULL) == hot);
		for (uint32_t pc = 0x1100u; pc < 0x1100u + 64u * 4u; pc += 4u)
			(void)nw_jit_compile(ops, 2, pc, 0x1000u, 0, 0);
		CHECK(nw_jit_cache_get(0x1000u, 0x1000u, 0, 0, NULL) == hot);
		CHECK(nw_jit_evict_count() > 0 ||
		      nw_jit_cache_get(0x1000u, 0x1000u, 0, 0, NULL) == hot);

		/* 68k emulator window is still cached after the flood */
		nw_jit_fn emu = nw_jit_compile(ops, 2, 0x68066084u, 0x68066000u, 0, 0);
		CHECK(emu != NULL);
		CHECK(nw_jit_cache_get(0x68066000u, 0x68066084u, 0, 0, NULL) == emu);
		CHECK(NW_JIT_DTLB_N == 1024);

		/* 32 KiB PC aliases must not share a home slot (old hash evicted). */
		nw_jit_reset();
		for (int i = 0; i < 16384; i++) {
			const uint32_t pc = 0x1000u + (uint32_t)i * 0x8000u;
			nw_jit_cache_put(0x1000u, pc, 0, 0, NW_JIT_INTERPRET, 1);
		}
		CHECK(nw_jit_evict_count() == 0);
		CHECK(nw_jit_cache_get(0x1000u, 0x1000u, 0, 0, NULL) == NW_JIT_INTERPRET);
		CHECK(nw_jit_cache_get(0x1000u, 0x1000u + 16383u * 0x8000u, 0, 0, NULL) ==
		      NW_JIT_INTERPRET);

		/* Tombstone: invalidate one page; a later live in the probe still hits. */
		nw_jit_reset();
		nw_jit_cache_put(0x1000u, 0x1000u, 0, 0, NW_JIT_INTERPRET, 1);
		nw_jit_cache_put(0x2000u, 0x2000u, 0, 0, NW_JIT_INTERPRET, 1);
		CHECK(nw_jit_cache_get(0x1000u, 0x1000u, 0, 0, NULL) == NW_JIT_INTERPRET);
		nw_jit_invalidate_page(0x1000u);
		CHECK(nw_jit_cache_get(0x1000u, 0x1000u, 0, 0, NULL) == NULL);
		CHECK(nw_jit_cache_get(0x2000u, 0x2000u, 0, 0, NULL) == NW_JIT_INTERPRET);

		/* Fall-through chain_pc: two addi, no terminator. Uncond b. */
		{
			uint32_t cops[2];
			cops[0] = nw_ppc_addi(3, 0, 1);
			cops[1] = nw_ppc_addi(3, 3, 1);
			nw_jit_reset();
			nw_jit_fn cfn = nw_jit_compile(cops, 2, 0x2700u, 0x2000u, 0, 0);
			CHECK(cfn != NULL);
			uint32_t ch = 0;
			int cn = 0;
			CHECK(nw_jit_cache_get(0x2000u, 0x2700u, 0, 0, &cn, NULL, NULL, &ch) == cfn);
			CHECK(cn == 2);
			CHECK(ch == 0x2708u);
			cops[0] = nw_ppc_b(16, 0);
			cfn = nw_jit_compile(cops, 1, 0x2800u, 0x2000u, 0, 0);
			CHECK(cfn != NULL);
			ch = 0;
			CHECK(nw_jit_cache_get(0x2000u, 0x2800u, 0, 0, &cn, NULL, NULL, &ch) == cfn);
			CHECK(ch == 0x2810u);
			CHECK(nw_jit_chain_hops() == 0);
		}

		/* Banked wrap: a block in bank 1 survives recycle of bank 0. */
		{
			nw_jit_reset();
			uint32_t wops[2];
			wops[0] = nw_ppc_addi(3, 0, 1);
			wops[1] = nw_ppc_blr();
			int i = 0;
			while (nw_jit_wrap_count() == 0 && i < 800000) {
				const uint32_t pc = 0x1000u + (uint32_t)i * 4u;
				(void)nw_jit_compile(wops, 2, pc, 0x1000u, 0, 0);
				i++;
			}
			CHECK(nw_jit_wrap_count() >= 1);
			nw_jit_fn keep = nw_jit_compile(wops, 2, 0x2000u, 0x2000u, 0, 0);
			CHECK(keep != NULL);
			CHECK(nw_jit_cache_get(0x2000u, 0x2000u, 0, 0, NULL) == keep);
			const uint64_t w0 = nw_jit_wrap_count();
			while (nw_jit_wrap_count() == w0 && i < 1600000) {
				const uint32_t pc = 0x1000u + (uint32_t)i * 4u;
				(void)nw_jit_compile(wops, 2, pc, 0x1000u, 0, 0);
				if ((i & 1023) == 0)
					(void)nw_jit_cache_get(0x2000u, 0x2000u, 0, 0, NULL);
				i++;
			}
			CHECK(nw_jit_wrap_count() > w0);
			CHECK(nw_jit_cache_get(0x2000u, 0x2000u, 0, 0, NULL) == keep);
			struct nw_jit_cpu kc;
			memset(&kc, 0, sizeof(kc));
			kc.lr = 0x3000u;
			keep(&kc);
			CHECK(kc.gpr[3] == 1 && kc.pc == 0x3000u);
			nw_jit_invalidate_page(0x2000u);
			CHECK(nw_jit_cache_get(0x2000u, 0x2000u, 0, 0, NULL) == NULL);
		}

		/* addze r4, r3: +CA, wrap CA, addze. CR0 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 5;
		a.xer = 0x20000000u;
		ops[0] = 0x7c830194u;
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1a00u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1a00u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 6 && !(a.xer & 0x20000000u));
		CHECK(b.gpr[4] == a.gpr[4] && b.xer == a.xer && b.pc == a.pc);

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0xffffffffu;
		a.xer = 0x20000000u;
		ops[0] = 0x7c830194u;
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1a10u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1a10u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0 && (a.xer & 0x20000000u));
		CHECK(b.gpr[4] == 0 && b.xer == a.xer);

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		ops[0] = 0x7c830195u;	/* addze. r4, r3 */
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1a20u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1a20u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0 && (a.cr >> 28) == 2 && b.cr == a.cr);

		/* lwarx / stwcx. on RAM: match stores and sets EQ; mismatch does not */
		{
			uint8_t ram[64];
			memset(ram, 0, sizeof(ram));
			ram[16] = 0x11; ram[17] = 0x22; ram[18] = 0x33; ram[19] = 0x44;
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = sizeof(ram);
			a.gpr[1] = 16;
			ops[0] = 0x7c611028u;	/* lwarx r3, r1, r2 */
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x1a30u) == 1);
			fn = nw_jit_compile(ops, 2, 0x1a30u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[3] == 0x11223344u && b.gpr[3] == a.gpr[3]);
			CHECK(a.reserve_valid == 1 && a.reserve_ea == 16);
			CHECK(b.reserve_valid == 1 && b.reserve_ea == 16);
			a.gpr[3] = 0xaabbccddu;
			b.gpr[3] = 0xaabbccddu;
			ops[0] = 0x7c61112du;	/* stwcx. r3, r1, r2 */
			ops[1] = nw_ppc_blr();
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x1a40u) == 1);
			fn = nw_jit_compile(ops, 2, 0x1a40u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(ram[16] == 0xaa && ram[19] == 0xdd);
			CHECK((a.cr >> 28) == 2 && b.cr == a.cr);
			CHECK(a.reserve_valid == 0 && b.reserve_valid == 0);
			ram[16] = 0x11; ram[17] = 0x22; ram[18] = 0x33; ram[19] = 0x44;
			a.gpr[3] = 0xdeadbeefu;
			a.reserve_valid = 0;
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x1a50u) == 1);
			fn = nw_jit_compile(ops, 2, 0x1a50u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(ram[16] == 0x11 && ram[19] == 0x44);
			CHECK(((a.cr >> 28) & 2) == 0 && ((b.cr >> 28) & 2) == 0);
		}

		/* lswx 6 bytes; stswx 5 bytes */
		{
			uint8_t ram[64];
			memset(ram, 0, sizeof(ram));
			ram[0] = 0x11; ram[1] = 0x22; ram[2] = 0x33;
			ram[3] = 0x44; ram[4] = 0x55; ram[5] = 0x66;
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = sizeof(ram);
			a.xer = 6;
			ops[0] = 0x7c61142au;	/* lswx r3, r1, r2 */
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x1a60u) == 1);
			fn = nw_jit_compile(ops, 2, 0x1a60u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[3] == 0x11223344u && a.gpr[4] == 0x55660000u);
			CHECK(b.gpr[3] == a.gpr[3] && b.gpr[4] == a.gpr[4]);
			memset(ram, 0, sizeof(ram));
			a.gpr[3] = 0xaabbccddu;
			a.gpr[4] = 0xee000000u;
			a.xer = 5;
			ops[0] = 0x7c61152au;	/* stswx r3, r1, r2 */
			ops[1] = nw_ppc_blr();
			b = a;
			b.mem = ram;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x1a70u) == 1);
			fn = nw_jit_compile(ops, 2, 0x1a70u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(ram[0] == 0xaa && ram[1] == 0xbb && ram[2] == 0xcc &&
			      ram[3] == 0xdd && ram[4] == 0xee && ram[5] == 0);
		}

		/* vmsumshm / saturating vmsumshs */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 0x00020003u;
		a.vr[2][0] = 0x00040005u;
		a.vr[4][0] = 10;
		ops[0] = 0x10611128u;	/* vmsumshm v3, v1, v2, v4 */
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1a80u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1a80u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 33 && b.vr[3][0] == 33);

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 0x7fff0000u;
		a.vr[2][0] = 0x7fff0000u;
		a.vr[4][0] = 0x7fffffffu;
		ops[0] = 0x10611129u;	/* vmsumshs v3, v1, v2, v4 */
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1a90u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1a90u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0x7fffffffu && b.vr[3][0] == a.vr[3][0]);

		/* mcrf cr7, cr0 copies field 0 into field 7 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.cr = 0x12345678u;
		ops[0] = nw_ppc_mcrf(7, 0);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1c00u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1c00u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK((a.cr & 0xfu) == 1 && (a.cr & ~0xfu) == (0x12345678u & ~0xfu));
		CHECK(b.cr == a.cr);

		/* fcmpo cr0: 1.0 vs 2.0 is LT */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.fpr[1] = 0x3ff0000000000000ull;
		a.fpr[2] = 0x4000000000000000ull;
		ops[0] = nw_ppc_fcmpo(0, 1, 2);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1c10u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1c10u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK((a.cr >> 28) == 8 && b.cr == a.cr);

		/* fnmsubs: -(3*1 - 1) = -2.0f ; fmadd 3*1+1 = 4.0 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.fpr[1] = 0x4008000000000000ull;	/* 3.0 */
		a.fpr[2] = 0x3ff0000000000000ull;	/* 1.0 */
		a.fpr[4] = 0x3ff0000000000000ull;
		ops[0] = nw_ppc_fnmsubs(3, 1, 4, 2);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1c20u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1c20u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.fpr[3] == 0xc000000000000000ull && b.fpr[3] == a.fpr[3]);

		/* fmsubs: 3*1 - 1 = 2.0f */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.fpr[1] = 0x4008000000000000ull;
		a.fpr[2] = 0x3ff0000000000000ull;
		a.fpr[4] = 0x3ff0000000000000ull;
		ops[0] = nw_ppc_fmsubs(3, 1, 4, 2);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1c24u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1c24u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.fpr[3] == 0x4000000000000000ull && b.fpr[3] == a.fpr[3]);

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.fpr[1] = 0x4008000000000000ull;
		a.fpr[2] = 0x3ff0000000000000ull;
		a.fpr[4] = 0x3ff0000000000000ull;
		ops[0] = nw_ppc_fmadd(3, 1, 4, 2);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1c30u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1c30u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.fpr[3] == 0x4010000000000000ull && b.fpr[3] == a.fpr[3]);

		/* fmr: bit copy, no FPRF */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.fpr[1] = 0x3ff0000000000000ull;
		a.fpr[3] = 0xdeadbeefcafebabeull;
		ops[0] = nw_ppc_fmr(3, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_ppc_fmr(0, 0) == 0xfc000090u);
		CHECK(nw_jit_op_supported(nw_ppc_fmr(3, 1)));
		CHECK(!nw_jit_op_supported(nw_ppc_fmr(3, 1) | 1u));
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1c38u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1c38u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.fpr[3] == 0x3ff0000000000000ull && b.fpr[3] == a.fpr[3]);
		CHECK(a.fpscr == 0 && b.fpscr == 0);

		/* fabs: clear sign, no FPRF */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.fpr[1] = 0xbff0000000000000ull;
		ops[0] = nw_ppc_fabs(3, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_ppc_fabs(0, 0) == 0xfc000210u);
		CHECK(nw_jit_op_supported(nw_ppc_fabs(3, 1)));
		CHECK(!nw_jit_op_supported(nw_ppc_fabs(3, 1) | 1u));
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1f00u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1f00u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.fpr[3] == 0x3ff0000000000000ull && b.fpr[3] == a.fpr[3]);
		CHECK(a.fpscr == 0 && b.fpscr == 0);

		/* fcmpu: same CR/FPCC as fcmpo when FPU exceptions off */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.fpr[1] = 0x3ff0000000000000ull;
		a.fpr[2] = 0x4000000000000000ull;
		ops[0] = nw_ppc_fcmpu(0, 1, 2);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_ppc_fcmpu(0, 0, 0) == 0xfc000000u);
		CHECK(nw_jit_op_supported(nw_ppc_fcmpu(0, 1, 2)));
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1f10u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1f10u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(((a.cr >> 28) & 0xfu) == 8 && b.cr == a.cr);
		CHECK(((a.fpscr >> 12) & 0xfu) == 8 && b.fpscr == a.fpscr);

		/* vand / vsrb */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 0xff00ff00u;
		a.vr[2][0] = 0x0f0f0f0fu;
		ops[0] = nw_ppc_vand(3, 1, 2);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_ppc_vand(0, 0, 0) == 0x10000404u);
		CHECK(nw_jit_op_supported(nw_ppc_vand(3, 1, 2)));
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1f20u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1f20u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0x0f000f00u && b.vr[3][0] == a.vr[3][0]);

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 0x80f00f01u;
		a.vr[2][0] = 0x01020307u;
		ops[0] = nw_ppc_vsrb(3, 1, 2);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_ppc_vsrb(0, 0, 0) == 0x10000204u);
		CHECK(nw_jit_op_supported(nw_ppc_vsrb(3, 1, 2)));
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1f30u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1f30u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0x403c0100u && b.vr[3][0] == a.vr[3][0]);

		/* adde / eqv / nand */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0xffffffffu;
		a.gpr[5] = 1;
		a.xer = 0x20000000u;
		ops[0] = nw_ppc_adde(4, 3, 5, 0);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_ppc_adde(0, 0, 0, 0) == 0x7c000114u);
		CHECK(nw_jit_op_supported(nw_ppc_adde(4, 3, 5, 0)));
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1f40u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1f40u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 1 && (a.xer & 0x20000000u) && b.gpr[4] == a.gpr[4] && b.xer == a.xer);

		CHECK(nw_ppc_addeo(0, 0, 0, 0) == 0x7c000514u);
		CHECK(((nw_ppc_addeo(0, 0, 0, 0) >> 1) & 0x3ffu) == 650u);
		CHECK(nw_jit_op_supported(nw_ppc_addeo(4, 3, 5, 0)));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0xffffffffu;
		a.gpr[5] = 1;
		a.xer = 0x20000000u;
		ops[0] = nw_ppc_addeo(4, 3, 5, 0);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x2590u) == 1);
		fn = nw_jit_compile(ops, 2, 0x2590u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 1 && b.gpr[4] == 1);
		CHECK((a.xer & 0x60000000u) == 0x20000000u && b.xer == a.xer);
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0x7fffffffu;
		a.gpr[5] = 1;
		a.xer = 0;
		ops[0] = nw_ppc_addeo(4, 3, 5, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x25a0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x25a0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0x80000000u && b.gpr[4] == a.gpr[4]);
		CHECK((a.xer & 0xc0000000u) == 0xc0000000u && b.xer == a.xer);
		CHECK((a.cr >> 28) == 9 && b.cr == a.cr); /* LT|SO */

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0xf0f0f0f0u;
		a.gpr[5] = 0xff00ff00u;
		ops[0] = nw_ppc_eqv(4, 3, 5);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_op_supported(nw_ppc_eqv(4, 3, 5)));
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1f50u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1f50u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0xf00ff00fu && b.gpr[4] == a.gpr[4]);

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0xf0f0f0f0u;
		a.gpr[5] = 0xff00ff00u;
		ops[0] = nw_ppc_nand(4, 3, 5);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_op_supported(nw_ppc_nand(4, 3, 5)));
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1f60u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1f60u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0x0fff0fffu && b.gpr[4] == a.gpr[4]);

		/* mtfsf: FM=0xff copies low word; FEX/VX stay 0 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.fpr[3] = 0xffffffffu;
		ops[0] = nw_ppc_mtfsf(0xff, 3);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_ppc_mtfsf(0xff, 3) == 0xfdfe1d8eu);
		CHECK(nw_jit_op_supported(nw_ppc_mtfsf(0xff, 3)));
		CHECK(!nw_jit_op_supported(nw_ppc_mtfsf(0xff, 3) | 1u));
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1c3cu) == 1);
		fn = nw_jit_compile(ops, 2, 0x1c3cu, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.fpscr == 0x9fffffffu && b.fpscr == a.fpscr);

		/* frsp: kpx (float) then store as double; FPRF on the single */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.fpr[1] = 0x3ff0000000000000ull;	/* 1.0 */
		ops[0] = nw_ppc_frsp(3, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_ppc_frsp(0, 0) == 0xfc000018u);
		CHECK(nw_jit_op_supported(nw_ppc_frsp(3, 1)));
		CHECK(!nw_jit_op_supported(nw_ppc_frsp(3, 1) | 1u));
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1d00u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1d00u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.fpr[3] == 0x3ff0000000000000ull && b.fpr[3] == a.fpr[3]);
		CHECK((a.fpscr & 0x1f000u) == 0x4000u && b.fpscr == a.fpscr);

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.fpr[1] = 0x4170000010000000ull;	/* 2^24+1 -> 2^24 as float */
		ops[0] = nw_ppc_frsp(3, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1d10u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1d10u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.fpr[3] == 0x4170000000000000ull && b.fpr[3] == a.fpr[3]);

		/* mffs: FPSCR zero-extended into FPR; Rc stays kpx */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.fpscr = 0x12345678u;
		a.fpr[3] = 0xffffffffffffffffull;
		ops[0] = nw_ppc_mffs(3);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_ppc_mffs(0) == 0xfc00048eu);
		CHECK(nw_jit_op_supported(nw_ppc_mffs(3)));
		CHECK(!nw_jit_op_supported(nw_ppc_mffs(3) | 1u));
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1d20u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1d20u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.fpr[3] == 0x12345678ull && b.fpr[3] == a.fpr[3]);
		CHECK(a.fpscr == 0x12345678u && b.fpscr == a.fpscr);

		/* fnmsub: -(3*1 - 1) = -2.0 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.fpr[1] = 0x4008000000000000ull;
		a.fpr[2] = 0x3ff0000000000000ull;
		a.fpr[4] = 0x3ff0000000000000ull;
		ops[0] = nw_ppc_fnmsub(3, 1, 4, 2);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_ppc_fnmsub(0, 0, 0, 0) == 0xfc00003cu);
		CHECK(nw_jit_op_supported(nw_ppc_fnmsub(3, 1, 4, 2)));
		CHECK(!nw_jit_op_supported(nw_ppc_fnmsub(3, 1, 4, 2) | 1u));
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1d30u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1d30u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.fpr[3] == 0xc000000000000000ull && b.fpr[3] == a.fpr[3]);
		CHECK((a.fpscr & 0x1f000u) == 0x8000u && b.fpscr == a.fpscr);

		/* stfdu / lfsu / lfdu / lhzu: RA≠0, fault before RA write */
		{
			uint8_t ram[64];
			memset(ram, 0, sizeof(ram));
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = sizeof(ram);
			a.gpr[1] = 8;
			a.fpr[3] = 0x3ff0000000000000ull;
			ops[0] = nw_ppc_stfdu(3, 1, 8);
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_ppc_stfdu(0, 0, 0) == 0xdc000000u);
			CHECK(nw_jit_op_supported(nw_ppc_stfdu(3, 1, 8)));
			CHECK(!nw_jit_op_supported(nw_ppc_stfdu(3, 0, 8)));
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x1e00u) == 1);
			fn = nw_jit_compile(ops, 2, 0x1e00u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[1] == 16 && b.gpr[1] == 16);
			CHECK(ram[16] == 0x3f && ram[17] == 0xf0 && ram[23] == 0);

			ram[16] = 0x3f; ram[17] = 0x80; ram[18] = 0; ram[19] = 0;
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = sizeof(ram);
			a.gpr[1] = 8;
			ops[0] = nw_ppc_lfsu(3, 1, 8);
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_ppc_lfsu(0, 0, 0) == 0xc4000000u);
			CHECK(nw_jit_op_supported(nw_ppc_lfsu(3, 1, 8)));
			CHECK(!nw_jit_op_supported(nw_ppc_lfsu(3, 0, 8)));
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x1e10u) == 1);
			fn = nw_jit_compile(ops, 2, 0x1e10u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[1] == 16 && b.gpr[1] == 16);
			CHECK(a.fpr[3] == 0x3ff0000000000000ull && b.fpr[3] == a.fpr[3]);
			CHECK(b.pc == 0x2000u);

			ops[0] = nw_ppc_addi(1, 1, 0);
			ops[1] = nw_ppc_lfsu(3, 1, 8);
			ops[2] = nw_ppc_addi(5, 1, 0);
			ops[3] = nw_ppc_blr();
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = sizeof(ram);
			a.gpr[1] = 8;
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 4, 0x1e14u) == 1);
			fn = nw_jit_compile(ops, 4, 0x1e14u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[1] == 16 && a.gpr[5] == 16);
			CHECK(b.gpr[1] == 16 && b.gpr[5] == 16 && b.pc == 0x2000u);

			ram[16] = 0x3f; ram[17] = 0xf0;
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = sizeof(ram);
			a.gpr[1] = 8;
			ops[0] = nw_ppc_lfdu(3, 1, 8);
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_ppc_lfdu(0, 0, 0) == 0xcc000000u);
			CHECK(nw_jit_op_supported(nw_ppc_lfdu(3, 1, 8)));
			CHECK(!nw_jit_op_supported(nw_ppc_lfdu(3, 0, 8)));
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x1e20u) == 1);
			fn = nw_jit_compile(ops, 2, 0x1e20u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[1] == 16 && b.gpr[1] == 16);
			CHECK(a.fpr[3] == 0x3ff0000000000000ull && b.fpr[3] == a.fpr[3]);

			ram[16] = 0xab; ram[17] = 0xcd;
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = sizeof(ram);
			a.gpr[1] = 8;
			ops[0] = nw_ppc_lhzu(4, 1, 8);
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_ppc_lhzu(0, 0, 0) == 0xa4000000u);
			CHECK(nw_jit_op_supported(nw_ppc_lhzu(4, 1, 8)));
			CHECK(!nw_jit_op_supported(nw_ppc_lhzu(4, 0, 8)));
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x1e30u) == 1);
			fn = nw_jit_compile(ops, 2, 0x1e30u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[4] == 0xabcdu && a.gpr[1] == 16);
			CHECK(b.gpr[4] == a.gpr[4] && b.gpr[1] == a.gpr[1]);
			CHECK(b.pc == 0x2000u);

			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = sizeof(ram);
			a.gpr[1] = 8;
			ops[0] = nw_ppc_lhzu(1, 1, 8); /* RA==RD: final RD is EA */
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x1e38u) == 1);
			fn = nw_jit_compile(ops, 2, 0x1e38u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[1] == 16 && b.gpr[1] == 16);

			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = sizeof(ram);
			a.gpr[1] = 100;
			a.gpr[4] = 0xdeadbeefu;
			ops[0] = nw_ppc_lhzu(4, 1, 8);
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 1, 0x1e40u) != 1);
			fn = nw_jit_compile(ops, 1, 0x1e40u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(b.fault == 1);
			CHECK(b.gpr[4] == 0xdeadbeefu && b.gpr[1] == 100);
			CHECK(b.pc == 0x1e40u);

			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = sizeof(ram);
			a.gpr[1] = 100;
			a.fpr[3] = 0x1111ull;
			ops[0] = nw_ppc_lfsu(3, 1, 8);
			ops[1] = nw_ppc_blr();
			b = a;
			fn = nw_jit_compile(ops, 1, 0x1e44u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(b.fault == 1);
			CHECK(b.gpr[1] == 100 && b.fpr[3] == 0x1111ull);
			CHECK(b.pc == 0x1e44u);
		}

		/* vaddubm: byte add, wrap */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 0x0102fffeu;
		a.vr[2][0] = 0x05060103u;
		ops[0] = nw_ppc_vaddubm(3, 1, 2);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_ppc_vaddubm(0, 0, 0) == 0x10000000u);
		CHECK(nw_jit_op_supported(nw_ppc_vaddubm(3, 1, 2)));
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1e50u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1e50u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0x06080001u && b.vr[3][0] == a.vr[3][0]);

		/* vadduwm / vsraw / vpkswss */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 1; a.vr[1][1] = 2;
		a.vr[2][0] = 3; a.vr[2][1] = 4;
		ops[0] = nw_ppc_vadduwm(3, 1, 2);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1c40u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1c40u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 4 && a.vr[3][1] == 6 && b.vr[3][0] == 4);

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 0x80000000u;
		a.vr[2][0] = 1;
		ops[0] = nw_ppc_vsraw(3, 1, 2);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1c50u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1c50u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0xc0000000u && b.vr[3][0] == a.vr[3][0]);

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 5; a.vr[1][1] = 40000;
		a.vr[2][0] = 6;
		ops[0] = nw_ppc_vpkswss(3, 1, 2);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1c60u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1c60u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0x00057fffu && a.vr[3][2] == 0x00060000u);
		CHECK(b.vr[3][0] == a.vr[3][0] && b.vr[3][2] == a.vr[3][2]);

		/* subfic r4, r3, 1: 1 - 5 = -4, CA clear */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 5;
		ops[0] = nw_ppc_subfic(4, 3, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1b00u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1b00u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == (uint32_t)-4 && !(a.xer & 0x20000000u));
		CHECK(b.gpr[4] == a.gpr[4] && b.xer == a.xer);

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0;
		ops[0] = nw_ppc_subfic(4, 3, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1b10u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1b10u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 1 && (a.xer & 0x20000000u) && b.xer == a.xer);

		/* addo overflow; addo no overflow clears OV, SO sticky */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0x7fffffffu;
		a.gpr[5] = 1;
		ops[0] = nw_ppc_add(4, 3, 5, 0) | (1u << 10);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1b20u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1b20u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0x80000000u && (a.xer & 0xc0000000u) == 0xc0000000u);
		CHECK(b.gpr[4] == a.gpr[4] && b.xer == a.xer);

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.xer = 0xc0000000u;
		a.gpr[3] = 1;
		a.gpr[5] = 1;
		ops[0] = nw_ppc_add(4, 3, 5, 0) | (1u << 10);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1b30u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1b30u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 2 && (a.xer & 0xc0000000u) == 0x80000000u);
		CHECK(b.xer == a.xer);

		/* mullwo overflow; divwu and divwu by 0 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0x40000000u;
		a.gpr[5] = 4;
		ops[0] = nw_ppc_mullw(4, 3, 5, 0) | (1u << 10);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1b40u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1b40u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0 && (a.xer & 0xc0000000u) == 0xc0000000u);
		CHECK(b.gpr[4] == 0 && b.xer == a.xer);

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 100;
		a.gpr[5] = 7;
		ops[0] = nw_ppc_divwu(4, 3, 5, 0);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1b50u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1b50u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 14 && b.gpr[4] == 14);

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 100;
		a.gpr[5] = 0;
		ops[0] = nw_ppc_divwu(4, 3, 5, 0);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1b60u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1b60u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0 && b.gpr[4] == 0);

		/* divw: signed, kpx /0 and INT_MIN/-1 */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = (uint32_t)-100;
		a.gpr[5] = 7;
		ops[0] = nw_ppc_divw(4, 3, 5, 0);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_ppc_divw(0, 0, 0, 0) == 0x7c0003d6u);
		CHECK(nw_jit_op_supported(nw_ppc_divw(4, 3, 5, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_divw(4, 3, 5, 0) | (1u << 10)));
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1b64u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1b64u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == (uint32_t)-14 && b.gpr[4] == a.gpr[4]);

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = (uint32_t)-100;
		a.gpr[5] = 0;
		ops[0] = nw_ppc_divw(4, 3, 5, 0);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1b68u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1b68u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0xffffffffu && b.gpr[4] == a.gpr[4]);

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0x80000000u;
		a.gpr[5] = (uint32_t)-1;
		ops[0] = nw_ppc_divw(4, 3, 5, 0);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1b6cu) == 1);
		fn = nw_jit_compile(ops, 2, 0x1b6cu, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0xffffffffu && b.gpr[4] == a.gpr[4]);

		/* divwuo: unsigned OE. 0x7ce62797 is r7,r6,r4 Rc=1 */
		CHECK(nw_ppc_divwuo(7, 6, 4, 1) == 0x7ce62797u);
		CHECK(nw_jit_op_supported(nw_ppc_divwuo(7, 6, 4, 1)));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 100;
		a.gpr[5] = 7;
		a.xer = 0xc0000000u;
		ops[0] = nw_ppc_divwuo(4, 3, 5, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x2100u) == 1);
		fn = nw_jit_compile(ops, 2, 0x2100u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 14 && b.gpr[4] == 14);
		CHECK((a.xer & 0xc0000000u) == 0x80000000u && b.xer == a.xer);
		CHECK((a.cr >> 28) == 5 && b.cr == a.cr); /* GT|SO */

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 100;
		a.gpr[5] = 0;
		ops[0] = nw_ppc_divwuo(4, 3, 5, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x2110u) == 1);
		fn = nw_jit_compile(ops, 2, 0x2110u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0 && b.gpr[4] == 0);
		CHECK((a.xer & 0xc0000000u) == 0xc0000000u && b.xer == a.xer);
		CHECK((a.cr >> 28) == 3 && b.cr == a.cr); /* EQ|SO */

		/* divwo: signed OE. skip xo 1003 = 491|OE */
		CHECK(nw_ppc_divwo(0, 0, 0, 0) == (nw_ppc_divw(0, 0, 0, 0) | (1u << 10)));
		CHECK(nw_jit_op_supported(nw_ppc_divwo(4, 3, 5, 1)));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 100;
		a.gpr[5] = 7;
		a.xer = 0xc0000000u;
		ops[0] = nw_ppc_divwo(4, 3, 5, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x2150u) == 1);
		fn = nw_jit_compile(ops, 2, 0x2150u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 14 && b.gpr[4] == 14);
		CHECK((a.xer & 0xc0000000u) == 0x80000000u && b.xer == a.xer);
		CHECK((a.cr >> 28) == 5 && b.cr == a.cr); /* GT|SO */

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = (uint32_t)-100;
		a.gpr[5] = 0;
		ops[0] = nw_ppc_divwo(4, 3, 5, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x2160u) == 1);
		fn = nw_jit_compile(ops, 2, 0x2160u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0xffffffffu && b.gpr[4] == a.gpr[4]);
		CHECK((a.xer & 0xc0000000u) == 0xc0000000u && b.xer == a.xer);

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0x80000000u;
		a.gpr[5] = (uint32_t)-1;
		ops[0] = nw_ppc_divwo(4, 3, 5, 0);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x2170u) == 1);
		fn = nw_jit_compile(ops, 2, 0x2170u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0xffffffffu && b.gpr[4] == a.gpr[4]);
		CHECK((a.xer & 0xc0000000u) == 0xc0000000u && b.xer == a.xer);

		/* subfo skip 552 = 40|OE; soak 31/648 is subfeo not subfo */
		CHECK(((nw_ppc_subfo(0, 0, 0, 0) >> 1) & 0x3ffu) == 552u);
		CHECK(((nw_ppc_subfeo(0, 0, 0, 0) >> 1) & 0x3ffu) == 648u);
		CHECK(nw_jit_op_supported(nw_ppc_subfo(4, 3, 5, 1)));
		CHECK(nw_jit_op_supported(nw_ppc_subfeo(4, 3, 5, 1)));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 1;
		a.gpr[5] = 0x80000000u;
		ops[0] = nw_ppc_subfo(4, 3, 5, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x2180u) == 1);
		fn = nw_jit_compile(ops, 2, 0x2180u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0x7fffffffu && b.gpr[4] == a.gpr[4]);
		CHECK((a.xer & 0xc0000000u) == 0xc0000000u && b.xer == a.xer);
		CHECK((a.cr >> 28) == 5 && b.cr == a.cr); /* GT|SO */

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 3;
		a.gpr[5] = 5;
		a.xer = 0xc0000000u;
		ops[0] = nw_ppc_subfo(4, 3, 5, 0);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x2190u) == 1);
		fn = nw_jit_compile(ops, 2, 0x2190u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 2 && b.gpr[4] == 2);
		CHECK((a.xer & 0xc0000000u) == 0x80000000u && b.xer == a.xer);

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 3;
		a.gpr[5] = 5;
		a.xer = 0x20000000u; /* CA=1 → subfeo == subf */
		ops[0] = nw_ppc_subfeo(4, 3, 5, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x21a0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x21a0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 2 && b.gpr[4] == 2);
		CHECK((a.xer & 0x20000000u) == 0x20000000u && b.xer == a.xer);
		CHECK((a.xer & 0x40000000u) == 0 && b.xer == a.xer);

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 3;
		a.gpr[5] = 5;
		ops[0] = nw_ppc_subfeo(4, 3, 5, 0); /* CA=0 → rb+~ra */
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x21b0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x21b0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 1 && b.gpr[4] == 1);

		/* vcmpequw.: 0x10c10486 = v6,v1,v0 Rc=1. skip xo 579 = VXR 134|Rc */
		CHECK(nw_ppc_vcmpequw(6, 1, 0, 1) == 0x10c10486u);
		CHECK(nw_ppc_vcmpequw(0, 0, 0, 0) == 0x10000086u);
		CHECK(nw_jit_op_supported(0x10c10486u));
		CHECK(nw_jit_op_supported(nw_ppc_vcmpequw(3, 1, 2, 0)));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 1; a.vr[1][1] = 2; a.vr[1][2] = 3; a.vr[1][3] = 4;
		a.vr[2][0] = 1; a.vr[2][1] = 2; a.vr[2][2] = 3; a.vr[2][3] = 4;
		a.cr = 0x12345678u;
		ops[0] = nw_ppc_vcmpequw(3, 1, 2, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x21c0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x21c0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0xffffffffu && a.vr[3][1] == 0xffffffffu &&
		      a.vr[3][2] == 0xffffffffu && a.vr[3][3] == 0xffffffffu);
		CHECK(b.vr[3][0] == a.vr[3][0] && b.vr[3][3] == a.vr[3][3]);
		CHECK(((a.cr >> 4) & 0xfu) == 8u && b.cr == a.cr);

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 1; a.vr[1][1] = 2; a.vr[1][2] = 3; a.vr[1][3] = 4;
		a.vr[2][0] = 0; a.vr[2][1] = 0; a.vr[2][2] = 0; a.vr[2][3] = 0;
		ops[0] = nw_ppc_vcmpequw(3, 1, 2, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x21d0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x21d0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0 && a.vr[3][1] == 0 && a.vr[3][2] == 0 && a.vr[3][3] == 0);
		CHECK(b.vr[3][0] == 0 && b.vr[3][3] == 0);
		CHECK(((a.cr >> 4) & 0xfu) == 2u && b.cr == a.cr);

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 1; a.vr[1][1] = 2; a.vr[1][2] = 3; a.vr[1][3] = 4;
		a.vr[2][0] = 1; a.vr[2][1] = 0; a.vr[2][2] = 3; a.vr[2][3] = 0;
		a.cr = 0x12345678u;
		ops[0] = nw_ppc_vcmpequw(3, 1, 2, 0);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x21e0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x21e0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0xffffffffu && a.vr[3][1] == 0 &&
		      a.vr[3][2] == 0xffffffffu && a.vr[3][3] == 0);
		CHECK(b.vr[3][0] == a.vr[3][0] && b.vr[3][1] == 0);
		CHECK(a.cr == 0x12345678u && b.cr == a.cr);

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 1; a.vr[1][1] = 2; a.vr[1][2] = 3; a.vr[1][3] = 4;
		a.vr[2][0] = 1; a.vr[2][1] = 0; a.vr[2][2] = 3; a.vr[2][3] = 0;
		a.cr = 0x12345678u;
		ops[0] = nw_ppc_vcmpequw(6, 1, 2, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x21f0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x21f0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(((a.cr >> 4) & 0xfu) == 0 && b.cr == a.cr);

		/* Last mill batch pulled after QT CHK / boot lock. Encoders stay. */
		CHECK(nw_ppc_vcmpequb(0, 0, 0, 0) == 0x10000006u);
		CHECK(nw_ppc_vcmpequb(0, 0, 0, 1) == 0x10000406u);
		CHECK(nw_jit_op_supported(nw_ppc_vcmpequb(3, 1, 2, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_vcmpequb(3, 1, 2, 1)));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 0x01020304u; a.vr[1][1] = 0x05060708u;
		a.vr[1][2] = 0x090a0b0cu; a.vr[1][3] = 0x0d0e0f10u;
		a.vr[2][0] = 0x01020304u; a.vr[2][1] = 0x05060708u;
		a.vr[2][2] = 0x090a0b0cu; a.vr[2][3] = 0x0d0e0f10u;
		a.cr = 0x12345678u;
		ops[0] = nw_ppc_vcmpequb(3, 1, 2, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x2420u) == 1);
		fn = nw_jit_compile(ops, 2, 0x2420u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0xffffffffu && b.vr[3][0] == 0xffffffffu);
		CHECK(((a.cr >> 4) & 0xfu) == 8u && b.cr == a.cr);
		CHECK(nw_ppc_vminsb(0, 0, 0) == 0x10000302u);
		CHECK(nw_jit_op_supported(nw_ppc_vminsb(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_vsr(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_vspltisb(3, 1)));
		CHECK(nw_jit_op_supported(nw_ppc_mtvscr(2)));
		CHECK(nw_jit_op_supported(nw_ppc_mfvscr(3)));
		CHECK(nw_ppc_vsldoi(12, 2, 0, 12) == 0x1182032cu);
		CHECK(nw_ppc_vsldoi(0, 0, 0, 12) == ((4u << 26) | 812u));
		CHECK(nw_jit_op_supported(nw_ppc_vsldoi(3, 1, 2, 12)));
		CHECK(nw_jit_op_supported((4u << 26) | 812u));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 0x00010203u; a.vr[1][1] = 0x04050607u;
		a.vr[1][2] = 0x08090a0bu; a.vr[1][3] = 0x0c0d0e0fu;
		a.vr[2][0] = 0x10111213u; a.vr[2][1] = 0x14151617u;
		a.vr[2][2] = 0x18191a1bu; a.vr[2][3] = 0x1c1d1e1fu;
		ops[0] = nw_ppc_vsldoi(3, 1, 2, 12);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x24c0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x24c0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0x0c0d0e0fu && b.vr[3][0] == a.vr[3][0]);
		CHECK(a.vr[3][1] == 0x10111213u && b.vr[3][1] == a.vr[3][1]);
		CHECK(a.vr[3][2] == 0x14151617u && b.vr[3][2] == a.vr[3][2]);
		CHECK(a.vr[3][3] == 0x18191a1bu && b.vr[3][3] == a.vr[3][3]);
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 0x00010203u; a.vr[1][1] = 0x04050607u;
		a.vr[1][2] = 0x08090a0bu; a.vr[1][3] = 0x0c0d0e0fu;
		a.vr[2][0] = 0xaabbccddu;
		ops[0] = nw_ppc_vsldoi(3, 1, 2, 0);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x24d0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x24d0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0x00010203u && b.vr[3][0] == a.vr[3][0]);
		CHECK(a.vr[3][3] == 0x0c0d0e0fu && b.vr[3][3] == a.vr[3][3]);
		CHECK(nw_ppc_vmrghb(0, 0, 0) == 0x1000000cu);
		CHECK(nw_jit_op_supported(nw_ppc_vmrghb(3, 1, 2)));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 0x00010203u; a.vr[1][1] = 0x04050607u;
		a.vr[1][2] = 0x08090a0bu; a.vr[1][3] = 0x0c0d0e0fu;
		a.vr[2][0] = 0x10111213u; a.vr[2][1] = 0x14151617u;
		a.vr[2][2] = 0x18191a1bu; a.vr[2][3] = 0x1c1d1e1fu;
		ops[0] = nw_ppc_vmrghb(3, 1, 2);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x24e0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x24e0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0x00100111u && b.vr[3][0] == a.vr[3][0]);
		CHECK(a.vr[3][1] == 0x02120313u && b.vr[3][1] == a.vr[3][1]);
		CHECK(a.vr[3][2] == 0x04140515u && b.vr[3][2] == a.vr[3][2]);
		CHECK(a.vr[3][3] == 0x06160717u && b.vr[3][3] == a.vr[3][3]);
		CHECK(nw_ppc_vmrglb(0, 0, 0) == 0x1000010cu);
		CHECK(nw_jit_op_supported(nw_ppc_vmrglb(3, 1, 2)));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 0x00010203u; a.vr[1][1] = 0x04050607u;
		a.vr[1][2] = 0x08090a0bu; a.vr[1][3] = 0x0c0d0e0fu;
		a.vr[2][0] = 0x10111213u; a.vr[2][1] = 0x14151617u;
		a.vr[2][2] = 0x18191a1bu; a.vr[2][3] = 0x1c1d1e1fu;
		ops[0] = nw_ppc_vmrglb(3, 1, 2);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x25c0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x25c0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0x08180919u && b.vr[3][0] == a.vr[3][0]);
		CHECK(a.vr[3][1] == 0x0a1a0b1bu && b.vr[3][1] == a.vr[3][1]);
		CHECK(a.vr[3][2] == 0x0c1c0d1du && b.vr[3][2] == a.vr[3][2]);
		CHECK(a.vr[3][3] == 0x0e1e0f1fu && b.vr[3][3] == a.vr[3][3]);
		CHECK(nw_ppc_vandc(0, 0, 0) == 0x10000444u);
		CHECK(nw_jit_op_supported(nw_ppc_vandc(3, 1, 2)));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 0xffffffffu;
		a.vr[2][0] = 0x0f0f0f0fu;
		ops[0] = nw_ppc_vandc(3, 1, 2);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x25d0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x25d0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0xf0f0f0f0u && b.vr[3][0] == a.vr[3][0]);
		CHECK(nw_ppc_vxor(0, 0, 0) == 0x100004c4u);
		CHECK(nw_jit_op_supported(nw_ppc_vxor(3, 1, 2)));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 0xa5a5a5a5u;
		a.vr[2][0] = 0xff00ff00u;
		ops[0] = nw_ppc_vxor(3, 1, 2);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x25e0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x25e0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0x5aa55aa5u && b.vr[3][0] == a.vr[3][0]);
		CHECK(nw_ppc_vsububm(0, 0, 0) == 0x10000400u);
		CHECK(nw_jit_op_supported(nw_ppc_vsububm(3, 1, 2)));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 0x00010203u;
		a.vr[2][0] = 0x01010101u;
		ops[0] = nw_ppc_vsububm(3, 1, 2);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x25f0u) == 1);
		fn = nw_jit_compile(ops, 2, 0x25f0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0xff000102u && b.vr[3][0] == a.vr[3][0]);
		CHECK(nw_ppc_vslh(0, 0, 0) == 0x10000144u);
		CHECK(nw_jit_op_supported(nw_ppc_vslh(3, 1, 2)));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 0x00010002u;
		a.vr[2][0] = 0x00040001u;
		ops[0] = nw_ppc_vslh(3, 1, 2);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x2600u) == 1);
		fn = nw_jit_compile(ops, 2, 0x2600u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0x00100004u && b.vr[3][0] == a.vr[3][0]);
		CHECK(nw_ppc_vspltb(0, 0, 0) == 0x1000020cu);
		CHECK(nw_jit_op_supported(nw_ppc_vspltb(3, 5, 1)));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 0x00010203u; a.vr[1][1] = 0x04050607u;
		a.vr[1][2] = 0x08090a0bu; a.vr[1][3] = 0x0c0d0e0fu;
		ops[0] = nw_ppc_vspltb(3, 5, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x2610u) == 1);
		fn = nw_jit_compile(ops, 2, 0x2610u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0x05050505u && b.vr[3][0] == a.vr[3][0]);
		CHECK(a.vr[3][3] == 0x05050505u && b.vr[3][3] == a.vr[3][3]);
		CHECK(nw_ppc_vsl(0, 0, 0) == 0x100001c4u);
		CHECK(nw_jit_op_supported(nw_ppc_vsl(3, 1, 2)));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][3] = 0xf0000001u;
		a.vr[2][3] = 4;
		ops[0] = nw_ppc_vsl(3, 1, 2);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x2620u) == 1);
		fn = nw_jit_compile(ops, 2, 0x2620u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][2] == 0x0000000fu && b.vr[3][2] == a.vr[3][2]);
		CHECK(a.vr[3][3] == 0x00000010u && b.vr[3][3] == a.vr[3][3]);
		CHECK(nw_ppc_vslo(0, 0, 0) == 0x1000040cu);
		CHECK(nw_jit_op_supported(nw_ppc_vslo(3, 1, 2)));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 0x00010203u; a.vr[1][1] = 0x04050607u;
		a.vr[1][2] = 0x08090a0bu; a.vr[1][3] = 0x0c0d0e0fu;
		a.vr[2][3] = 32; /* sh = (32>>3)&15 = 4 */
		ops[0] = nw_ppc_vslo(3, 1, 2);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x2630u) == 1);
		fn = nw_jit_compile(ops, 2, 0x2630u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0x04050607u && b.vr[3][0] == a.vr[3][0]);
		CHECK(a.vr[3][1] == 0x08090a0bu && b.vr[3][1] == a.vr[3][1]);
		CHECK(a.vr[3][2] == 0x0c0d0e0fu && b.vr[3][2] == a.vr[3][2]);
		CHECK(a.vr[3][3] == 0 && b.vr[3][3] == a.vr[3][3]);
		CHECK(nw_ppc_vsro(0, 0, 0) == 0x1000044cu);
		CHECK(nw_jit_op_supported(nw_ppc_vsro(3, 1, 2)));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 0x00010203u; a.vr[1][1] = 0x04050607u;
		a.vr[1][2] = 0x08090a0bu; a.vr[1][3] = 0x0c0d0e0fu;
		a.vr[2][3] = 32; /* sh = 4 */
		ops[0] = nw_ppc_vsro(3, 1, 2);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x2650u) == 1);
		fn = nw_jit_compile(ops, 2, 0x2650u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0 && b.vr[3][0] == a.vr[3][0]);
		CHECK(a.vr[3][1] == 0x00010203u && b.vr[3][1] == a.vr[3][1]);
		CHECK(a.vr[3][2] == 0x04050607u && b.vr[3][2] == a.vr[3][2]);
		CHECK(a.vr[3][3] == 0x08090a0bu && b.vr[3][3] == a.vr[3][3]);
		CHECK(nw_ppc_vslb(0, 0, 0) == 0x10000104u);
		CHECK(nw_jit_op_supported(nw_ppc_vslb(3, 1, 2)));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 0x01020304u;
		a.vr[2][0] = 0x01020304u; /* shifts 1,2,3,4 */
		ops[0] = nw_ppc_vslb(3, 1, 2);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x2500u) == 1);
		fn = nw_jit_compile(ops, 2, 0x2500u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0x02081840u && b.vr[3][0] == a.vr[3][0]);
		CHECK(nw_ppc_vsrw(0, 0, 0) == 0x10000284u);
		CHECK(nw_jit_op_supported(nw_ppc_vsrw(3, 1, 2)));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 0x80000000u;
		a.vr[2][0] = 1;
		ops[0] = nw_ppc_vsrw(3, 1, 2);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x2670u) == 1);
		fn = nw_jit_compile(ops, 2, 0x2670u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0x40000000u && b.vr[3][0] == a.vr[3][0]);
		CHECK(nw_ppc_vspltisw(0, 0) == 0x1000038cu);
		CHECK(nw_jit_op_supported(nw_ppc_vspltisw(3, 1)));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		ops[0] = nw_ppc_vspltisw(3, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x2680u) == 1);
		fn = nw_jit_compile(ops, 2, 0x2680u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 1 && b.vr[3][0] == a.vr[3][0]);
		CHECK(a.vr[3][3] == 1 && b.vr[3][3] == 1);
		{
			uint8_t ram[32];
			memset(ram, 0, sizeof(ram));
			memcpy(ram + 4, "Hello", 5);
			CHECK(nw_blockmove_copy(ram, 0, sizeof(ram), 16, 4, 5) == 1);
			CHECK(memcmp(ram + 16, "Hello", 5) == 0);
			CHECK(nw_blockmove_copy(ram, 0, sizeof(ram), 100, 4, 5) == 0);
			/* A22E register contract: D0=n, A0=src, A1=dst; skip sets D0=0 */
			{
				uint8_t r2[32];
				uint32_t d0 = 0xdeadbeefu;
				memset(r2, 0, sizeof(r2));
				memcpy(r2 + 2, "ABCD", 4);
				CHECK(nw_blockmove_regs(4, 2, 10, r2, 0, sizeof(r2), &d0) == 1);
				CHECK(d0 == 0);
				CHECK(memcmp(r2 + 10, "ABCD", 4) == 0);
				d0 = 0xffffffffu; /* reverse; do not skip */
				CHECK(nw_blockmove_regs(d0, 2, 10, r2, 0, sizeof(r2), &d0) == 0);
				CHECK(d0 == 0xffffffffu);
				d0 = 4;
				CHECK(nw_blockmove_regs(4, 2, 100, r2, 0, sizeof(r2), &d0) == 0);
				CHECK(d0 == 4);
				memset(r2, 0, sizeof(r2));
				memcpy(r2 + 8, "WXYZ", 4);
				d0 = 4;
				CHECK(nw_blockmove_regs(4, 8, 9, r2, 0, sizeof(r2), &d0) == 1); /* overlap */
				CHECK(d0 == 0);
				CHECK(memcmp(r2 + 9, "WXYZ", 4) == 0);
			}
			const uint64_t e0 = nw_mixedmode_enters();
			nw_mixedmode_enter();
			nw_mixedmode_leave();
			CHECK(nw_mixedmode_enters() == e0 + 1);
			CHECK(nw_mixedmode_leaves() >= 1);
		}
		CHECK(nw_jit_op_supported(nw_ppc_mulhw(4, 3, 5, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_mulhw(4, 3, 5, 1)));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0x80000000u;
		a.gpr[5] = 2;
		ops[0] = nw_ppc_mulhw(4, 3, 5, 0);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x2450u) == 1);
		fn = nw_jit_compile(ops, 2, 0x2450u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0xffffffffu && b.gpr[4] == a.gpr[4]);
		CHECK(nw_jit_op_supported(nw_ppc_lhzx(3, 1, 2)));
		CHECK(!nw_jit_op_supported(nw_ppc_lhzux(4, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_lwbrx(3, 1, 2)));

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		ops[0] = nw_ppc_vspltisb(3, 0x1f); /* -1 splat */
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x2400u) == 1);
		fn = nw_jit_compile(ops, 2, 0x2400u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0xffffffffu && b.vr[3][0] == 0xffffffffu);
		CHECK(a.vr[3][3] == 0xffffffffu && b.vr[3][3] == 0xffffffffu);

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[2][3] = 0x00010001u;
		ops[0] = nw_ppc_mtvscr(2);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x2460u) == 1);
		fn = nw_jit_compile(ops, 2, 0x2460u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vscr == 0x00010001u && b.vscr == a.vscr);

		CHECK(nw_ppc_mfvscr(0) == 0x10000604u);
		CHECK(nw_jit_op_supported(nw_ppc_mfvscr(3)));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vscr = 0x00010001u;
		a.vr[3][0] = 0xffffffffu;
		ops[0] = nw_ppc_mfvscr(3);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x2530u) == 1);
		fn = nw_jit_compile(ops, 2, 0x2530u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0 && b.vr[3][0] == 0);
		CHECK(a.vr[3][1] == 0 && b.vr[3][1] == 0);
		CHECK(a.vr[3][2] == 0 && b.vr[3][2] == 0);
		CHECK(a.vr[3][3] == 0x00010001u && b.vr[3][3] == a.vr[3][3]);

		CHECK(nw_ppc_vspltw(0, 0, 0) == 0x1000028cu);
		CHECK(nw_jit_op_supported(nw_ppc_vspltw(3, 2, 1)));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 0x11111111u; a.vr[1][1] = 0x22222222u;
		a.vr[1][2] = 0x33333333u; a.vr[1][3] = 0x44444444u;
		ops[0] = nw_ppc_vspltw(3, 2, 1);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x2540u) == 1);
		fn = nw_jit_compile(ops, 2, 0x2540u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0x33333333u && b.vr[3][0] == a.vr[3][0]);
		CHECK(a.vr[3][3] == 0x33333333u && b.vr[3][3] == a.vr[3][3]);

		CHECK(nw_ppc_vmladduhm(0, 0, 0, 0) == 0x10000022u);
		CHECK(nw_jit_op_supported(nw_ppc_vmladduhm(3, 1, 2, 4)));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 0x00020003u;
		a.vr[2][0] = 0x00040005u;
		a.vr[4][0] = 0x000a000b;
		ops[0] = nw_ppc_vmladduhm(3, 1, 2, 4);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x2550u) == 1);
		fn = nw_jit_compile(ops, 2, 0x2550u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0x0012001au && b.vr[3][0] == a.vr[3][0]);

		CHECK(nw_ppc_vsubshs(0, 0, 0) == 0x10000740u);
		CHECK(nw_jit_op_supported(nw_ppc_vsubshs(3, 1, 2)));
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 0x00020007u;
		a.vr[2][0] = 0x00030001u;
		ops[0] = nw_ppc_vsubshs(3, 1, 2);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x2560u) == 1);
		fn = nw_jit_compile(ops, 2, 0x2560u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0xffff0006u && b.vr[3][0] == a.vr[3][0]);
		CHECK(a.vscr == 0 && b.vscr == 0);
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 0x7fff8000u;
		a.vr[2][0] = 0x80000001u;
		ops[0] = nw_ppc_vsubshs(3, 1, 2);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x2570u) == 1);
		fn = nw_jit_compile(ops, 2, 0x2570u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0x7fff8000u && b.vr[3][0] == a.vr[3][0]);
		CHECK((a.vscr & 1u) && (b.vscr & 1u));

		{
			uint8_t ram[16];
			memset(ram, 0, sizeof(ram));
			ram[8] = 0x12; ram[9] = 0x34; ram[10] = 0x56; ram[11] = 0x78;
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = sizeof(ram);
			a.gpr[1] = 8;
			a.gpr[2] = 0;
			ops[0] = nw_ppc_lwbrx(3, 1, 2);
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x2470u) == 1);
			fn = nw_jit_compile(ops, 2, 0x2470u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[3] == 0x78563412u && b.gpr[3] == a.gpr[3]);
		}

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.sr[1] = 0xabcdu;
		a.gpr[4] = 0x10000000u;
		ops[0] = nw_ppc_mfsrin(3, 4);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x2480u) == 1);
		fn = nw_jit_compile(ops, 2, 0x2480u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[3] == 0xabcdu && b.gpr[3] == 0xabcdu);
		CHECK(!nw_jit_op_ends_block(nw_ppc_mfsrin(3, 4)));
		{
			uint32_t pa = 0;
			nw_jit_dtlb_flush();
			nw_jit_dtlb_fill(0x1000u, 0x2000u, 1, 0);
			nw_jit_dtlb_fill(0x10000000u, 0x3000u, 1, 0);
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.sr[0] = 1;
			a.gpr[3] = 2;
			a.gpr[4] = 0x00001000u;
			ops[0] = nw_ppc_mtsrin(3, 4);
			b = a;
			CHECK(nw_jit_op_ends_block(nw_ppc_mtsrin(3, 4)));
			CHECK(nw_jit_interp_n(&a, ops, 1, 0x2490u) == 1);
			fn = nw_jit_compile(ops, 1, 0x2490u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(nw_jit_dtlb_lookup(0x1000u, 0, &pa) == 0);
			CHECK(nw_jit_dtlb_lookup(0x10000000u, 0, &pa) == 1);
		}

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.vr[1][0] = 0x7f80ff01u; a.vr[2][0] = 0x0080fe02u;
		a.vr[1][1] = a.vr[1][2] = a.vr[1][3] = 0;
		a.vr[2][1] = a.vr[2][2] = a.vr[2][3] = 0;
		ops[0] = nw_ppc_vminsb(3, 1, 2);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x2410u) == 1);
		fn = nw_jit_compile(ops, 2, 0x2410u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.vr[3][0] == 0x0080fe01u && b.vr[3][0] == a.vr[3][0]);

		/* skip_raw one-shot for 354/390/406/802 and prim 6 */
		{
			char nm[24];
			uint32_t rop = 0, rpc = 0;
			nw_jit_reset();
			nw_jit_skip_raw_once(nw_ppc_vspltisb(3, 1), 0x0100u);
			CHECK(nw_jit_skip_raw_last(&rop, &rpc, nm, sizeof(nm)));
			CHECK(rop == nw_ppc_vspltisb(3, 1) && rpc == 0x0100u);
			CHECK(strcmp(nm, "vspltisb") == 0);
			nw_jit_skip_raw_once((4u << 26) | 812u, 0x0200u);
			CHECK(nw_jit_skip_raw_last(&rop, &rpc, nm, sizeof(nm)));
			CHECK(rop == ((4u << 26) | 812u) && rpc == 0x0200u);
			CHECK(strcmp(nm, "vsldoi") == 0);
			CHECK(nw_jit_op_supported((4u << 26) | 812u));
			nw_jit_skip_raw_once((6u << 26) | 0x00abcdeeu, 0x0300u);
			CHECK(nw_jit_skip_raw_last(&rop, &rpc, nm, sizeof(nm)));
			CHECK(((rop >> 26) == 6) && rpc == 0x0300u);
			CHECK(strcmp(nm, "op6") == 0);
			CHECK(!nw_jit_op_supported(6u << 26));
			nw_jit_skip_raw_once(nw_ppc_mtvscr(2), 0x0400u);
			CHECK(nw_jit_skip_raw_last(&rop, &rpc, nm, sizeof(nm)));
			CHECK(strcmp(nm, "mtvscr") == 0);
			nw_jit_skip_raw_once(nw_ppc_vsr(3, 1, 2), 0x0500u);
			CHECK(nw_jit_skip_raw_last(&rop, &rpc, nm, sizeof(nm)));
			CHECK(strcmp(nm, "vsr") == 0);
		}

		/* mtsrin/mfsrin pulled off JIT after QT CHK. drop_sr still scoped. */
		CHECK(nw_jit_op_supported(nw_ppc_mtsrin(3, 4)));
		CHECK(nw_jit_op_supported(nw_ppc_mfsrin(3, 4)));
		{
			uint32_t pa = 0;
			nw_jit_dtlb_flush();
			nw_jit_dtlb_fill(0x1000u, 0x2000u, 1, 0);
			nw_jit_dtlb_fill(0x10000000u, 0x3000u, 1, 0);
			nw_jit_dtlb_drop_sr(0, NW_JIT_DTLB_FL_MTSR);
			CHECK(nw_jit_dtlb_lookup(0x1000u, 0, &pa) == 0);
			CHECK(nw_jit_dtlb_lookup(0x10000000u, 0, &pa) == 1);
			CHECK(pa == 0x3000u);
		}

		/* mtmsr IR|DR change flushes DTLB. PR-only does not. rfi still
		 * uses no-op flush_if_pr (checked below). */
		{
			uint32_t pa = 0;
			nw_jit_dtlb_flush();
			nw_jit_dtlb_fill(0x1000u, 0x2000u, 1, 0);
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.msr = 0x30u; /* IR|DR */
			a.gpr[3] = 0x20u; /* drop DR */
			ops[0] = nw_ppc_mtmsr(3);
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 1, 0x24a0u) == 1);
			CHECK(a.msr == 0x20u);
			CHECK(nw_jit_dtlb_lookup(0x1000u, 0, &pa) == 0);
			nw_jit_dtlb_fill(0x1000u, 0x2000u, 1, 0);
			fn = nw_jit_compile(ops, 1, 0x24a0u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(b.msr == 0x20u);
			CHECK(nw_jit_dtlb_lookup(0x1000u, 0, &pa) == 0);

			nw_jit_dtlb_fill(0x1000u, 0x2000u, 1, 0);
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.msr = 0x30u;
			a.gpr[3] = 0x30u | ppc32_mmu::MSR_PR; /* PR only */
			ops[0] = nw_ppc_mtmsr(3);
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 1, 0x24b0u) == 1);
			CHECK(nw_jit_dtlb_lookup(0x1000u, 0, &pa) == 1);
			CHECK(pa == 0x2000u);
			fn = nw_jit_compile(ops, 1, 0x24b0u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(nw_jit_dtlb_lookup(0x1000u, 0, &pa) == 1);
			CHECK(pa == 0x2000u);
		}

		/* A-trap histogram: boot vs later windows */
		nw_atrap_hist_reset();
		nw_atrap_hist_set_elapsed(0);
		nw_event_aline(0xaafe, 0x1000u, 0);
		nw_event_aline(0xa22e, 0x1004u, 1);
		nw_event_aline(0xaafe, 0x1008u, 0);
		CHECK(nw_atrap_count(0xaafe, 0) == 2);
		CHECK(nw_atrap_count(0xa22e, 0) == 1);
		CHECK(nw_atrap_count(0xaafe, 1) == 0);
		nw_atrap_hist_set_elapsed(60);
		nw_event_aline(0xaafe, 0x2000u, 0);
		nw_event_aline(0xa22e, 0x2004u, 1);
		CHECK(nw_atrap_count(0xaafe, 0) == 2);
		CHECK(nw_atrap_count(0xaafe, 1) == 1);
		CHECK(nw_atrap_count(0xa22e, 1) == 1);
		{
			uint8_t rd[32];
			memset(rd, 0, sizeof(rd));
			rd[0] = 0xaa; rd[1] = 0xfe;
			rd[2] = 7;
			rd[17] = 1;
			rd[20] = 0x00; rd[21] = 0x12; rd[22] = 0x34; rd[23] = 0x56;
			uint32_t proc = 0;
			CHECK(nw_mixedmode_rd_ppc(rd, 32, &proc) == 1);
			CHECK(proc == 0x00123456u);
			rd[17] = 0; /* 68k ISA */
			CHECK(nw_mixedmode_rd_ppc(rd, 32, &proc) == 0);
			rd[17] = 1;
			rd[0] = 0xa0;
			CHECK(nw_mixedmode_rd_ppc(rd, 32, &proc) == 0);
			CHECK(nw_mixedmode_rd_ppc(rd, 16, &proc) == 0);
		}
		{
			uint64_t last = 0;
			CHECK(nw_clock_sample_due(50, &last, 100) == 1);
			CHECK(last == 50);
			CHECK(nw_clock_sample_due(149, &last, 100) == 0);
			CHECK(last == 50);
			CHECK(nw_clock_sample_due(150, &last, 100) == 1);
			CHECK(last == 150);
			CHECK(nw_clock_sample_due(150, NULL, 100) == 0);
			CHECK(nw_clock_sample_due(200, &last, 0) == 0);
		}

		/* lswi / stswi: EA=RA_or_0, NB=0→32; RA not in dest */
		{
			uint8_t ram[64];
			memset(ram, 0, sizeof(ram));
			ram[0] = 0x11; ram[1] = 0x22; ram[2] = 0x33;
			ram[3] = 0x44; ram[4] = 0x55; ram[5] = 0x66;
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = sizeof(ram);
			a.gpr[1] = 0; /* EA=0, RA=r1 not in r3.. */
			ops[0] = nw_ppc_lswi(3, 1, 6);
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_ppc_lswi(0, 0, 0) == 0x7c0004aau);
			CHECK(nw_ppc_stswi(0, 0, 0) == 0x7c0005aau);
			CHECK(nw_jit_op_supported(nw_ppc_lswi(3, 1, 6)));
			CHECK(nw_jit_op_supported(nw_ppc_stswi(3, 1, 5)));
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x2120u) == 1);
			fn = nw_jit_compile(ops, 2, 0x2120u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[3] == 0x11223344u && a.gpr[4] == 0x55660000u);
			CHECK(b.gpr[3] == a.gpr[3] && b.gpr[4] == a.gpr[4]);

			for (int i = 0; i < 32; i++)
				ram[i] = (uint8_t)(0x10 + i);
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = sizeof(ram);
			ops[0] = nw_ppc_lswi(3, 0, 0); /* NB=0 → 32; RA=0 EA=0 */
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x2130u) == 1);
			fn = nw_jit_compile(ops, 2, 0x2130u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[3] == 0x10111213u && a.gpr[10] == 0x2c2d2e2fu);
			CHECK(b.gpr[3] == a.gpr[3] && b.gpr[10] == a.gpr[10]);

			memset(ram, 0, sizeof(ram));
			a.gpr[3] = 0xaabbccddu;
			a.gpr[4] = 0xee000000u;
			ops[0] = nw_ppc_stswi(3, 1, 5);
			ops[1] = nw_ppc_blr();
			a.gpr[1] = 0;
			b = a;
			b.mem = ram;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x2140u) == 1);
			CHECK(ram[0] == 0xaa && ram[1] == 0xbb && ram[2] == 0xcc &&
			      ram[3] == 0xdd && ram[4] == 0xee && ram[5] == 0);
			memset(ram, 0, sizeof(ram));
			fn = nw_jit_compile(ops, 2, 0x2140u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(ram[0] == 0xaa && ram[1] == 0xbb && ram[2] == 0xcc &&
			      ram[3] == 0xdd && ram[4] == 0xee && ram[5] == 0);
		}

		/* stbx / lbzux */
		{
			uint8_t ram[64];
			memset(ram, 0, sizeof(ram));
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = sizeof(ram);
			a.gpr[1] = 8;
			a.gpr[2] = 4;
			a.gpr[3] = 0xab;
			ops[0] = nw_ppc_stbx(3, 1, 2);
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x1b70u) == 1);
			fn = nw_jit_compile(ops, 2, 0x1b70u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(ram[12] == 0xab);
			ops[0] = nw_ppc_lbzux(4, 1, 2);
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x1b80u) == 1);
			CHECK(a.gpr[4] == 0xabu && a.gpr[1] == 12);
			CHECK(nw_jit_op_supported(nw_ppc_lbzux(4, 1, 2)));
			CHECK(!nw_jit_op_supported(nw_ppc_lbzux(4, 0, 2)));
			fn = nw_jit_compile(ops, 2, 0x1b80u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			b = a;
			b.gpr[1] = 8;
			b.gpr[4] = 0;
			b.pc = 0x1b80u;
			fn(&b);
			CHECK(b.gpr[4] == 0xabu && b.gpr[1] == 12);
			CHECK(b.pc == 0x2000u); /* blr must run; W10 must stay fault */
			CHECK(nw_ppc_stbux(0, 0, 0) == 0x7c0001eeu);
			CHECK(!nw_jit_op_supported(nw_ppc_stbux(3, 0, 2)));
			CHECK(!nw_jit_op_supported(nw_ppc_stbux(3, 1, 2)));

			ops[0] = nw_ppc_addi(1, 1, 0);
			ops[1] = nw_ppc_lbzux(4, 1, 2);
			ops[2] = nw_ppc_addi(5, 1, 0);
			ops[3] = nw_ppc_blr();
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = sizeof(ram);
			a.gpr[1] = 8;
			a.gpr[2] = 4;
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 4, 0x1c80u) == 1);
			fn = nw_jit_compile(ops, 4, 0x1c80u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[4] == 0xabu && a.gpr[1] == 12 && a.gpr[5] == 12);
			CHECK(b.gpr[4] == a.gpr[4] && b.gpr[1] == a.gpr[1] && b.gpr[5] == a.gpr[5]);
			CHECK(b.pc == 0x2000u);

			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = sizeof(ram);
			a.gpr[1] = 8;
			a.gpr[2] = 4;
			ops[0] = nw_ppc_lbzux(1, 1, 2); /* RA==RD: final RD is EA */
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x1c88u) == 1);
			fn = nw_jit_compile(ops, 2, 0x1c88u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[1] == 12 && b.gpr[1] == 12);
			CHECK(b.pc == 0x2000u);

			ram[16] = 0x5a;
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = sizeof(ram);
			a.gpr[1] = 8;
			ops[0] = nw_ppc_lbzux(4, 1, 1); /* RA==RB: EA uses old r1 */
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x1c8cu) == 1);
			fn = nw_jit_compile(ops, 2, 0x1c8cu, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[1] == 16 && a.gpr[4] == 0x5au);
			CHECK(b.gpr[1] == a.gpr[1] && b.gpr[4] == a.gpr[4]);
			CHECK(b.pc == 0x2000u);

			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = 8; /* EA 8+4=12 is DSI */
			a.gpr[1] = 8;
			a.gpr[2] = 4;
			a.gpr[4] = 0xdeadbeefu;
			ops[0] = nw_ppc_lbzux(4, 1, 2);
			b = a;
			CHECK(nw_jit_interp_one(&a, ops[0]) == -1);
			CHECK(a.gpr[4] == 0xdeadbeefu && a.gpr[1] == 8);
			fn = nw_jit_compile(ops, 1, 0x1c90u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(b.fault == 1);
			CHECK(b.gpr[4] == 0xdeadbeefu && b.gpr[1] == 8);
			CHECK(b.pc == 0x1c90u); /* faulting pc, RA not replay-updated */

			/* lwzux normal / RA==RD / RA==RB / DSI / addi sandwich */
			ram[16] = 0x11; ram[17] = 0x22; ram[18] = 0x33; ram[19] = 0x44;
			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = sizeof(ram);
			a.gpr[1] = 12;
			a.gpr[2] = 4;
			ops[0] = nw_ppc_lwzux(4, 1, 2);
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_ppc_lwzux(0, 0, 0) == 0x7c00006eu);
			CHECK(nw_jit_op_supported(nw_ppc_lwzux(4, 1, 2)));
			CHECK(!nw_jit_op_supported(0x7c00006eu));
			CHECK(nw_jit_op_supported(nw_ppc_lwzux(4, 4, 2))); /* RA==RD is ISA */
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x1b84u) == 1);
			fn = nw_jit_compile(ops, 2, 0x1b84u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[4] == 0x11223344u && a.gpr[1] == 16);
			CHECK(b.gpr[4] == a.gpr[4] && b.gpr[1] == a.gpr[1]);

			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = sizeof(ram);
			a.gpr[4] = 12;
			a.gpr[2] = 4;
			ops[0] = nw_ppc_lwzux(4, 4, 2); /* RA==RD: final RD is EA */
			ops[1] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x1b88u) == 1);
			fn = nw_jit_compile(ops, 2, 0x1b88u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[4] == 16 && b.gpr[4] == 16);

			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = sizeof(ram);
			a.gpr[1] = 12;
			a.gpr[1] = 12;
			a.gpr[2] = 4;
			ops[0] = nw_ppc_lwzux(4, 1, 1); /* RA==RB: EA uses old RB */
			ops[1] = nw_ppc_blr();
			a.gpr[1] = 8;
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 2, 0x1b8cu) == 1);
			fn = nw_jit_compile(ops, 2, 0x1b8cu, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[1] == 16 && a.gpr[4] == 0x11223344u);
			CHECK(b.gpr[1] == a.gpr[1] && b.gpr[4] == a.gpr[4]);

			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = 8; /* EA 12+4=16 is DSI */
			a.gpr[1] = 12;
			a.gpr[2] = 4;
			a.gpr[4] = 0xdeadbeefu;
			ops[0] = nw_ppc_lwzux(4, 1, 2);
			b = a;
			CHECK(nw_jit_interp_one(&a, ops[0]) == -1);
			CHECK(a.gpr[4] == 0xdeadbeefu && a.gpr[1] == 12);
			fn = nw_jit_compile(ops, 1, 0x1bd0u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(b.fault == 1);
			CHECK(b.gpr[4] == 0xdeadbeefu && b.gpr[1] == 12);

			memset(&a, 0, sizeof(a));
			a.lr = 0x2000u;
			a.mem = ram;
			a.mem_base = 0;
			a.mem_size = sizeof(ram);
			a.gpr[1] = 12;
			a.gpr[2] = 4;
			ops[0] = nw_ppc_addi(1, 1, 0);
			ops[1] = nw_ppc_lwzux(4, 1, 2);
			ops[2] = nw_ppc_addi(5, 1, 0);
			ops[3] = nw_ppc_blr();
			b = a;
			CHECK(nw_jit_interp_n(&a, ops, 4, 0x1b94u) == 1);
			fn = nw_jit_compile(ops, 4, 0x1b94u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&b);
			CHECK(a.gpr[4] == 0x11223344u && a.gpr[1] == 16 && a.gpr[5] == 16);
			CHECK(b.gpr[4] == a.gpr[4] && b.gpr[1] == a.gpr[1] && b.gpr[5] == a.gpr[5]);
		}

		/* mfmsr copies cpu->msr */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.msr = 0x0000b032u;
		ops[0] = nw_ppc_mfmsr(3);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1b90u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1b90u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[3] == 0x0000b032u && b.gpr[3] == a.gpr[3]);

		/* sc takes EXC */
		memset(&a, 0, sizeof(a));
		ops[0] = nw_ppc_sc();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 1, 0x1ba0u) == 0);
		fn = nw_jit_compile(ops, 1, 0x1ba0u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.fault == NW_JIT_FAULT_EXC && b.fault == NW_JIT_FAULT_EXC);
		CHECK(a.pc == 0x1ba0u && b.pc == a.pc);

		/* A1: rfi helper. No host → pc+=4. Host stub → pc=SRR0, msr=SRR1. */
		{
			const uint32_t rfi = 0x4c000064u;
			memset(&a, 0, sizeof(a));
			a.pc = 0x1bb0u;
			a.msr = 0x0000b032u;
			CHECK(nw_jit_interp_one(&a, rfi) == 1);
			CHECK(a.pc == 0x1bb4u);
			CHECK(a.msr == 0x0000b032u);

			struct harness_rfi_host rh;
			const uint32_t combos[] = {
				0,
				ppc32_mmu::MSR_IR,
				ppc32_mmu::MSR_DR,
				ppc32_mmu::MSR_PR,
				ppc32_mmu::MSR_EE,
				0x00002000u,	/* FP */
				0x02000000u,	/* VEC */
				ppc32_mmu::MSR_IR | ppc32_mmu::MSR_DR | ppc32_mmu::MSR_PR |
					ppc32_mmu::MSR_EE | 0x00002000u | 0x02000000u,
			};
			nw_jit_set_host_rfi(harness_rfi);
			for (unsigned i = 0; i < sizeof(combos) / sizeof(combos[0]); i++) {
				memset(&a, 0, sizeof(a));
				memset(&rh, 0, sizeof(rh));
				rh.srr0 = 0x6806e8c0u;
				rh.srr1 = combos[i];
				a.host = &rh;
				a.pc = 0x503113ccu;
				a.msr = 0x00040000u;
				CHECK(nw_jit_interp_one(&a, rfi) == 1);
				CHECK(rh.ncall == 1);
				CHECK(a.pc == rh.srr0);
				CHECK(a.msr == rh.srr1);
			}
			nw_jit_set_host_rfi(NULL);

			nw_jit_set_host_rfi(harness_rfi);
			memset(&a, 0, sizeof(a));
			memset(&rh, 0, sizeof(rh));
			rh.srr0 = 0x6806e8c0u;
			rh.srr1 = 0x0000d032u;
			a.host = &rh;
			a.pc = 0x503113ccu;
			a.msr = 0x00040000u;
			ops[0] = rfi;
			fn = nw_jit_compile(ops, 1, 0x1bc0u, 0x1000u, 0, 0);
			CHECK(fn != NULL);
			fn(&a);
			CHECK(rh.ncall == 1);
			CHECK(a.pc == rh.srr0);
			CHECK(a.msr == rh.srr1);
			nw_jit_set_host_rfi(NULL);

			nw_jit_dtlb_flush();
			nw_jit_dtlb_fill(0x1000u, 0x2000u, 1, 0, 0);
			uint32_t pa = 0;
			CHECK(nw_jit_dtlb_lookup(0x1000u, 0, &pa) == 1);
			CHECK(pa == 0x2000u);
			const uint64_t miss0 = nw_jit_dtlb_misses();
			nw_jit_dtlb_flush_if_pr(0, ppc32_mmu::MSR_PR, 0);
			CHECK(nw_jit_dtlb_lookup(0x1000u, 0, &pa) == 1);
			CHECK(pa == 0x2000u);
			CHECK(nw_jit_dtlb_misses() == miss0);
		}
	}

	/* WP3 4b: dispatcher cache sentinels, mode, op filter. */
	{
		nw_jit_reset();
		nw_jit_set_mode(NW_JIT_FALLBACK);
		CHECK(nw_jit_mode() == NW_JIT_FALLBACK);
		CHECK(strcmp(nw_jit_mode_name(), "fallback") == 0);
		CHECK(nw_jit_op_supported(nw_ppc_addi(3, 0, 1)));
		CHECK(nw_jit_op_supported(nw_ppc_addis(4, 3, 1)));
		CHECK(nw_jit_op_supported(nw_ppc_mulli(4, 3, 6)));
		CHECK(nw_jit_op_supported(nw_ppc_lwz(3, 1, 0)));
		CHECK(nw_jit_op_dispatch(nw_ppc_addi(3, 0, 1)));
		CHECK(nw_jit_op_dispatch(nw_ppc_lwz(3, 1, 0)));
		CHECK(nw_jit_op_dispatch(nw_ppc_stw(3, 1, 0)));
		CHECK(nw_jit_op_ends_block(nw_ppc_blr()));
		CHECK(nw_jit_op_ends_block(nw_ppc_bcctr(20, 0)));
		CHECK(!nw_jit_op_ends_block(nw_ppc_stw(3, 1, 0)));
		CHECK(nw_jit_op_ends_block(nw_ppc_bc(NW_PPC_BO_TRUE, 0, 8)));
		CHECK(nw_jit_op_ends_block(0x4c000064u));
		CHECK(nw_jit_op_supported(0x4c000064u));
		CHECK(nw_jit_op_supported(nw_ppc_or(4, 3, 5)));
		CHECK(nw_jit_op_supported(nw_ppc_xor(4, 3, 5)));
		CHECK(nw_jit_op_supported(nw_ppc_and(4, 3, 5, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_cntlzw(4, 3, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_neg(4, 3, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_ori(4, 3, 1)));
		CHECK(nw_jit_op_supported(nw_ppc_rlwnm(4, 3, 5, 0, 23)));
		CHECK(nw_jit_op_supported(nw_ppc_lmw(30, 0, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_stmw(30, 0, 8)));
		CHECK(nw_jit_op_supported(nw_ppc_bcctr(20, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_bc(NW_PPC_BO_BDNZ, 0, -8)));
		CHECK(nw_jit_op_supported(nw_ppc_bc(NW_PPC_BO_BDNZ, 0, 8) | 1u));
		CHECK(nw_jit_op_supported(nw_ppc_stwu(3, 1, -4)));
		CHECK(nw_jit_op_supported(nw_ppc_lwzx(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_stwx(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_sthx(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_addc(4, 3, 5, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_addic(4, 3, -1, 1)));
		CHECK(nw_jit_op_supported(nw_ppc_lbz(3, 1, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_lbzu(3, 1, 4)));
		CHECK(nw_jit_op_supported(nw_ppc_lvx(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_lvxl(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_stvx(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_stvxl(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_lfd(3, 1, 8)));
		CHECK(nw_jit_op_supported(nw_ppc_stfd(3, 1, 16)));
		CHECK(nw_jit_op_supported(nw_ppc_mullw(4, 3, 5, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_mulhwu(4, 3, 5, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_andc(4, 3, 5, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_xori(4, 3, 0xff)));
		CHECK(nw_jit_op_supported(nw_ppc_xoris(4, 3, 0xff)));
		CHECK(nw_jit_op_supported(nw_ppc_oris(4, 3, 0xff)));
		CHECK(nw_jit_op_supported(nw_ppc_andis_dot(4, 3, 0x0ff0)));
		CHECK(nw_jit_op_supported(nw_ppc_lbzx(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_stb(3, 1, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_stbu(4, 1, 4)));
		CHECK(nw_jit_op_supported(nw_ppc_sthu(4, 1, 4)));
		CHECK(nw_jit_op_supported(nw_ppc_lwzu(3, 1, 4)));
		CHECK(nw_jit_op_supported(nw_ppc_addco(4, 3, 5, 0)));
		CHECK(nw_jit_op_supported(0x7c000414u));	/* addco r0,r0,r0 */
		{
			uint32_t blk[4];
			blk[0] = nw_ppc_addi(3, 0, 1);
			blk[1] = nw_ppc_stw(3, 1, 0);
			blk[2] = nw_ppc_addi(4, 0, 2);
			blk[3] = nw_ppc_blr();
			nw_jit_reset();
			nw_jit_fn bf = nw_jit_compile(blk, 4, 0x1000u, 0x1000u, 0, 0);
			int bn = 0;
			CHECK(bf != NULL);
			CHECK(nw_jit_cache_get(0x1000u, 0x1000u, 0, 0, &bn) == bf);
			CHECK(bn == 4);
		}
		CHECK(nw_jit_op_supported(nw_ppc_andi_dot(3, 4, 0xff)));
		CHECK(nw_jit_op_supported(nw_ppc_subfco(4, 3, 5, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_subfc(4, 3, 5, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_subfe(4, 3, 5, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_subf(4, 3, 5, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_cmpli(0, 3, 1)));
		CHECK(nw_jit_op_supported(nw_ppc_mtcrf(0x80, 4)));
		CHECK(nw_jit_op_supported(nw_ppc_mfcr(3)));
		CHECK(nw_jit_op_supported(nw_ppc_crnor(0, 0, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_crxor(31, 0, 1)));
		CHECK(nw_jit_op_supported(nw_ppc_creqv(31, 0, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_cror(31, 0, 1)));
		CHECK(nw_jit_op_supported(nw_ppc_crorc(31, 1, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_crand(31, 0, 1)));
		CHECK(nw_jit_op_supported(nw_ppc_crandc(31, 0, 1)));
		CHECK(nw_jit_op_supported(nw_ppc_crnand(31, 0, 1)));
		CHECK(nw_jit_op_supported(nw_ppc_mfspr(3, NW_PPC_SPR_TBL)));
		CHECK(nw_jit_op_supported(nw_ppc_extsh(4, 8, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_extsb(4, 8, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_slw(4, 8, 9, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_srw(4, 8, 9, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_sraw(4, 8, 9, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_srawi(4, 8, 1, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_sync()));
		CHECK(nw_jit_op_supported(nw_ppc_dss()));
		CHECK(nw_jit_op_supported(nw_ppc_dst(3, 4, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_dstst(3, 4, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_dcbt(3, 4)));
		CHECK(nw_jit_op_supported(nw_ppc_dcbtst(3, 4)));
		CHECK(nw_jit_op_supported(nw_ppc_dcbf(3, 4)));
		CHECK(nw_jit_op_supported(nw_ppc_eieio()));
		CHECK(nw_jit_op_supported(nw_ppc_dcbz(3, 4)));
		CHECK(nw_jit_op_supported(nw_ppc_mtsr(2, 5)));
		CHECK(nw_jit_op_supported(nw_ppc_twi(4, 3, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_mtmsr(10)));
		CHECK(nw_jit_op_supported(nw_ppc_mtspr(NW_PPC_SPR_SPRG0, 3)));
		CHECK(nw_jit_op_supported(nw_ppc_mfspr(3, 287)));	/* PVR already; any SPR now */
		CHECK(nw_jit_op_supported(nw_ppc_isync()));
		CHECK(nw_jit_op_supported(nw_ppc_lhax(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_lhaux(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_cmpl(0, 3, 4)));
		CHECK(nw_jit_op_supported(nw_ppc_cmpl(7, 3, 4)));
		CHECK(nw_jit_op_supported(nw_ppc_cmp_cr(7, 3, 4)));
		CHECK(!nw_jit_op_supported(nw_ppc_cmp_cr(0, 3, 4) | (1u << 21))); /* L=1 */
		CHECK(!nw_jit_op_supported(nw_ppc_cmpl(0, 3, 4) | (1u << 21))); /* L=1 */
		CHECK(nw_jit_op_supported(nw_ppc_mfspr(3, NW_PPC_SPR_TBU)));
		CHECK(nw_jit_op_supported(nw_ppc_mfspr(3, 18)));	/* DSISR via mfspr_guest */
		CHECK(nw_jit_op_supported(0x7c000028u));	/* lwarx */
		CHECK(nw_jit_op_supported(0x7c00012du));	/* stwcx. */
		CHECK(nw_jit_op_supported(0x4c000064u));	/* rfi helper+ret */
		CHECK(nw_jit_op_ends_block(0x4c000064u));
		CHECK(!nw_jit_op_supported(0x7c00006eu));	/* lwzux RA=0 */
		CHECK(!nw_jit_op_supported(0x7c00016eu));	/* stwux r0,r0,r0: RA=0 */
		CHECK(nw_jit_op_supported(nw_ppc_stwux(3, 1, 2)));
		CHECK(nw_jit_op_supported(0x7c0007acu));	/* icbi r0,r0 */
		CHECK(nw_jit_op_ends_block(0x7c0007acu));
		CHECK(nw_jit_op_supported(nw_ppc_tlbie(4)));
		CHECK(nw_jit_op_ends_block(nw_ppc_tlbie(4)));
		CHECK(nw_jit_op_supported(0x7c000194u));	/* addze */
		CHECK(nw_jit_op_supported(0x7c00042au));	/* lswx */
		CHECK(nw_jit_op_supported(0x7c00052au));	/* stswx */
		CHECK(nw_jit_op_supported(0x10000028u));	/* vmsumshm */
		CHECK(nw_jit_op_supported(0x10000029u));	/* vmsumshs */
		CHECK(nw_jit_op_supported(nw_ppc_subfic(4, 3, 1)));
		CHECK(nw_jit_op_supported(nw_ppc_stbx(3, 1, 2)));
		CHECK(!nw_jit_op_supported(nw_ppc_stbux(3, 1, 2)));
		CHECK(!nw_jit_op_supported(0x7c0001eeu));
		CHECK(nw_jit_op_supported(nw_ppc_lbzux(3, 1, 2)));
		CHECK(!nw_jit_op_supported(nw_ppc_lbzux(3, 0, 2)));
		CHECK(!nw_jit_op_supported(nw_ppc_lhzux(4, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_lwzux(4, 1, 2)));
		CHECK(!nw_jit_op_supported(0x7c00006eu));
		CHECK(nw_jit_op_supported(nw_ppc_add(4, 3, 5, 0) | (1u << 10)));
		CHECK(nw_jit_op_supported(nw_ppc_mullw(4, 3, 5, 0) | (1u << 10)));
		CHECK(nw_jit_op_supported(nw_ppc_divwu(4, 3, 5, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_divwuo(7, 6, 4, 1)));
		CHECK(nw_ppc_divwuo(7, 6, 4, 1) == 0x7ce62797u);
		CHECK(nw_jit_op_supported(nw_ppc_divw(4, 3, 5, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_divwo(4, 3, 5, 1)));
		CHECK(nw_jit_op_supported(nw_ppc_subfo(4, 3, 5, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_subfeo(4, 3, 5, 0)));
		CHECK(nw_ppc_vcmpequw(6, 1, 0, 1) == 0x10c10486u);
		CHECK(nw_jit_op_supported(0x10c10486u));
		CHECK(nw_jit_op_supported(nw_ppc_vcmpequw(3, 1, 2, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_vcmpequb(3, 1, 2, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_vcmpequb(3, 1, 2, 1)));
		CHECK(nw_jit_op_supported(nw_ppc_vminsb(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_vsr(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_vspltisb(3, 1)));
		CHECK(nw_jit_op_supported(nw_ppc_mtvscr(2)));
		CHECK(nw_jit_op_supported(nw_ppc_mfvscr(3)));
		CHECK(nw_jit_op_supported(nw_ppc_vspltw(3, 2, 1)));
		CHECK(nw_jit_op_supported(nw_ppc_vmladduhm(3, 1, 2, 4)));
		CHECK(nw_jit_op_supported(nw_ppc_vsubshs(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_vsldoi(3, 1, 2, 12)));
		CHECK(nw_jit_op_supported((4u << 26) | 812u));
		CHECK(nw_jit_op_supported(nw_ppc_vmrghb(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_vmrglb(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_vandc(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_vxor(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_vsububm(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_vslh(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_vspltb(3, 5, 1)));
		CHECK(nw_jit_op_supported(nw_ppc_vsl(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_vslo(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_vsro(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_vsrw(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_vspltisw(3, 1)));
		CHECK(!nw_jit_op_supported(nw_ppc_lhzux(4, 1, 2)));
		CHECK(!nw_jit_op_supported(nw_ppc_stbux(3, 1, 2)));
		CHECK(nw_jit_op_ends_block(nw_ppc_vslo(3, 1, 2)));
		CHECK(nw_jit_op_ends_block(nw_ppc_vsro(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_vslb(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_mulhw(4, 3, 5, 1)));
		CHECK(nw_jit_op_supported(nw_ppc_lhzx(3, 1, 2)));
		CHECK(!nw_jit_op_supported(nw_ppc_lhzux(4, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_lwbrx(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_mtsrin(3, 4)));
		CHECK(nw_jit_op_supported(nw_ppc_mfsrin(3, 4)));
		CHECK(nw_jit_op_supported(nw_ppc_lswi(3, 1, 6)));
		CHECK(nw_jit_op_supported(nw_ppc_stswi(3, 1, 5)));
		CHECK(nw_ppc_lswi(0, 0, 0) == 0x7c0004aau);
		CHECK(nw_ppc_stswi(0, 0, 0) == 0x7c0005aau);
		CHECK(nw_jit_op_supported(nw_ppc_mfmsr(3)));
		CHECK(nw_jit_op_supported(nw_ppc_mftb(3, NW_PPC_SPR_TBL)));
		CHECK(nw_jit_op_supported(nw_ppc_mftb(3, NW_PPC_SPR_TBU)));
		CHECK(!nw_jit_op_supported(nw_ppc_mftb(3, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_sc()));
		CHECK(nw_jit_op_ends_block(nw_ppc_sc()));
		CHECK(nw_jit_op_supported(nw_ppc_mcrf(7, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_fcmpo(0, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_fnmsubs(3, 1, 4, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_fmsubs(3, 1, 4, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_fadds(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_lfdx(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_stfdx(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_fmadd(3, 1, 4, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_fmr(3, 1)));
		CHECK(nw_jit_op_supported(nw_ppc_frsp(3, 1)));
		CHECK(nw_jit_op_supported(nw_ppc_mffs(3)));
		CHECK(nw_jit_op_supported(nw_ppc_fnmsub(3, 1, 4, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_mtfsf(0xff, 3)));
		CHECK(nw_jit_op_supported(nw_ppc_stfdu(3, 1, 8)));
		CHECK(!nw_jit_op_supported(nw_ppc_stfdu(3, 0, 8)));
		CHECK(nw_jit_op_supported(nw_ppc_lfsu(3, 1, 8)));
		CHECK(nw_jit_op_supported(nw_ppc_lfdu(3, 1, 8)));
		CHECK(nw_jit_op_supported(nw_ppc_lhzu(4, 1, 8)));
		CHECK(!nw_jit_op_supported(nw_ppc_lhzu(4, 0, 8)));
		CHECK(nw_jit_op_supported(nw_ppc_vaddubm(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_vadduwm(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_vand(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_vsrb(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_fabs(3, 1)));
		CHECK(nw_jit_op_supported(nw_ppc_fcmpu(0, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_adde(4, 3, 5, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_addeo(4, 3, 5, 0)));
		CHECK(nw_jit_op_supported(nw_ppc_eqv(4, 3, 5)));
		CHECK(nw_jit_op_supported(nw_ppc_nand(4, 3, 5)));
		{
			const uint32_t skipop = 0x7c000238u; /* eqv r0,r0,r0 */
			nw_jit_reset();
			CHECK(nw_jit_skip_n(skipop) == 0);
			nw_jit_note_skip_unsup(skipop, 4);
			nw_jit_note_skip_unsup(skipop, 12);
			CHECK(nw_jit_skip_n(skipop) == 2);
			CHECK(nw_jit_skip_lost(skipop) == 16);
			nw_jit_note_exec_at(10, 0x01060000u, 1);
			nw_jit_note_exec_at(5, 0x68000000u, 0);
			CHECK(nw_jit_codec_insns() == 10);
			CHECK(nw_jit_other_insns() == 5);
			CHECK(nw_jit_kcall_fast() == 0);
			nw_jit_note_kcall_fast();
			CHECK(nw_jit_kcall_fast() == 1);
			uint8_t fb[64 * 64 * 4];
			memset(fb, 0x40, sizeof(fb));
			nw_fb_fps_proxy_sample(fb, 64u * 4u, 64, 64);
			nw_fb_fps_proxy_sample(fb, 64u * 4u, 64, 64);
			CHECK(nw_fb_fps_proxy_frames() == 0);
			fb[(10u * 256u) + (16u * 4u)] ^= 1u;
			nw_fb_fps_proxy_sample(fb, 64u * 4u, 64, 64);
			CHECK(nw_fb_fps_proxy_frames() == 1);
		}
		CHECK(nw_jit_op_supported(nw_ppc_vsraw(3, 1, 2)));
		CHECK(nw_jit_op_supported(nw_ppc_vpkswss(3, 1, 2)));
		CHECK(nw_jit_op_supported(0x4c000064u));
		CHECK(!nw_jit_op_supported(0x7c00006eu));
		CHECK(!nw_jit_op_supported(0x7c00016eu));
		CHECK(nw_jit_cache_get(0x2000u, 0x2000u, 0, 0, NULL) == NULL);
		nw_jit_cache_put(0x2000u, 0x2000u, 0, 0, NW_JIT_INTERPRET, 0);
		CHECK(nw_jit_cache_get(0x2000u, 0x2000u, 0, 0, NULL) == NW_JIT_INTERPRET);
		nw_jit_set_mode(NW_JIT_VERIFY);
		CHECK(nw_jit_mode() == NW_JIT_VERIFY);
		CHECK(strcmp(nw_jit_mode_name(), "verify") == 0);
		nw_jit_set_mode(NW_JIT_OFF);
		CHECK(nw_jit_mode() == NW_JIT_OFF);
	}

	/* WP3 4b-2: lwz/stw helpers vs C oracle, including host path and
	 * a faulting lwz before blr (must ret, not hang on cbnz 0). */
	{
		nw_jit_reset();
		struct nw_jit_cpu a, b;
		uint32_t ops[4];

		/* RA=0: EA is d, not gpr[0]+d */
		uint8_t ram[64];
		memset(ram, 0, sizeof(ram));
		ram[16] = 0xaa; ram[17] = 0xbb; ram[18] = 0xcc; ram[19] = 0xdd;
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.mem = ram;
		a.mem_base = 0;
		a.mem_size = sizeof(ram);
		a.gpr[0] = 0xdead0000u;
		ops[0] = nw_ppc_lwz(3, 0, 16);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1000u) == 1);
		nw_jit_fn fn = nw_jit_compile(ops, 2, 0x1000u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[3] == 0xaabbccdd && b.gpr[3] == a.gpr[3] && b.pc == a.pc);

		memset(g_jit_hram, 0, sizeof(g_jit_hram));
		nw_jit_set_host_mem(harness_jit_lwz, harness_jit_stw);
		nw_jit_set_host_pa(harness_jit_lwz_pa, harness_jit_stw_pa);

		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.host = (void *)1;
		a.gpr[3] = 0x11223344u;
		a.gpr[1] = 16;
		ops[0] = nw_ppc_stw(3, 1, 0);
		ops[1] = nw_ppc_lwz(5, 1, 0);
		ops[2] = nw_ppc_blr();
		b = a;
		fn = nw_jit_compile(ops, 3, 0x1700u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(b.gpr[5] == 0x11223344u && b.pc == 0x2000u);
		CHECK(g_jit_hram[16] == 0x11 && g_jit_hram[19] == 0x44);

		/* faulting lwz then blr: pc stays at lwz, LR untouched */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.host = (void *)1;
		a.gpr[1] = 0x1000u;
		ops[0] = nw_ppc_lwz(3, 1, 0);
		ops[1] = nw_ppc_blr();
		fn = nw_jit_compile(ops, 2, 0x1800u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&a);
		CHECK(a.fault == 1 && a.pc == 0x1800u && a.lr == 0x2000u);
		CHECK(a.fault_ea == 0x1000u && a.fault_st == 0);

		/* rlwimi r4, r3, 8, 0, 7  insert top byte; mtlr; bclr not taken */
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.gpr[3] = 0x12u;
		a.gpr[4] = 0xaabbccddu;
		ops[0] = nw_ppc_rlwimi(4, 3, 24, 0, 7);
		ops[1] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1900u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1900u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[4] == 0x12bbccddu && b.gpr[4] == a.gpr[4] && b.pc == a.pc);

		memset(&a, 0, sizeof(a));
		a.gpr[29] = 0x3000u;
		a.cr = 0;	/* CR bit 8 = 0 → bclr BO=5 (false) taken */
		ops[0] = nw_ppc_mtspr(NW_PPC_SPR_LR, 29);
		ops[1] = nw_ppc_bclr(5, 8);
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 2, 0x1a00u) == 1);
		fn = nw_jit_compile(ops, 2, 0x1a00u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.pc == 0x3000u && b.pc == a.pc && b.lr == 0x3000u);

		uint8_t half[8];
		memset(half, 0, sizeof(half));
		half[2] = 0xfe; half[3] = 0xed;
		memset(&a, 0, sizeof(a));
		a.lr = 0x2000u;
		a.mem = half;
		a.mem_base = 0;
		a.mem_size = sizeof(half);
		a.gpr[1] = 2;
		ops[0] = nw_ppc_lha(3, 1, 0);
		ops[1] = nw_ppc_sth(3, 1, 4);
		ops[2] = nw_ppc_blr();
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 3, 0x1b00u) == 1);
		fn = nw_jit_compile(ops, 3, 0x1b00u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.gpr[3] == 0xfffffeed && b.gpr[3] == a.gpr[3]);
		CHECK(half[6] == 0xfe && half[7] == 0xed);
	}

	/* WP3 4b: fall-through PC after a 4-insn ALU block with no terminator. */
	{
		nw_jit_reset();
		struct nw_jit_cpu a, b;
		uint32_t ops[4];
		memset(&a, 0, sizeof(a));
		ops[0] = nw_ppc_addi(3, 0, 1);
		ops[1] = nw_ppc_addi(3, 3, 1);
		ops[2] = nw_ppc_addi(3, 3, 1);
		ops[3] = nw_ppc_addi(3, 3, 1);
		b = a;
		CHECK(nw_jit_interp_n(&a, ops, 4, 0x1600u) == 0);
		nw_jit_fn fn = nw_jit_compile(ops, 4, 0x1600u, 0x1000u, 0, 0);
		CHECK(fn != NULL);
		fn(&b);
		CHECK(a.pc == 0x1610u && b.pc == a.pc && a.gpr[3] == 4 && b.gpr[3] == 4);
	}

	printf("SheepShaver-MMUTests: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
