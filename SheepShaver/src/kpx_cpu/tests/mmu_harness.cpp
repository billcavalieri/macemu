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
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <sys/stat.h>

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

	/* G0: CHRP parse without a guest ROM in git. */
	{
		static const char chrp[] =
			"<CHRP-BOOT>\n"
			"h# 000040 constant parcels-offset\n"
			"h# 000010 constant parcels-size\n";
		std::vector<uint8_t> buf(0x50, 0);
		memcpy(&buf[0], chrp, sizeof(chrp) - 1);
		buf[0x40] = 'p'; buf[0x41] = 'r'; buf[0x42] = 'c'; buf[0x43] = 'l';
		std::vector<uint8_t> dest((size_t)NW_ROM_SIZE, 0);
		/* Payload is prcl but has no rom parcel — decode must fail, not crash. */
		CHECK(!nw_decode_rom_image(&buf[0], buf.size(), &dest[0], dest.size()));
		CHECK(!nw_g0_unpacked_ok(&dest[0], dest.size()));

		std::vector<uint8_t> plain((size_t)NW_ROM_SIZE, 0);
		memcpy(&plain[NW_NEWWORLD_SIG_OFFSET], "NewWorld", 8);
		plain[NW_NK_V2_OFFSET] = 0x48;
		CHECK(nw_decode_rom_image(&plain[0], plain.size(), &dest[0], dest.size()));
		CHECK(nw_g0_unpacked_ok(&dest[0], dest.size()));
	}

	/* G0 file check: local Mac OS ROM, never loaded from git. */
	{
		const char *path = getenv("SHEEP_OS921_ROM");
		char fallback[512];
		fallback[0] = 0;
		if (path == NULL || path[0] == 0) {
			const char *home = getenv("HOME");
			if (home) {
				snprintf(fallback, sizeof(fallback),
					 "%s/Downloads/Mac OS ROM", home);
				path = fallback;
			}
		}
		struct stat st;
		if (path && stat(path, &st) == 0 && st.st_size > 0) {
			std::vector<uint8_t> src((size_t)st.st_size);
			FILE *f = fopen(path, "rb");
			CHECK(f != NULL);
			if (f) {
				size_t n = fread(&src[0], 1, src.size(), f);
				fclose(f);
				CHECK(n == src.size());
				CHECK(n >= 11 && memcmp(&src[0], "<CHRP-BOOT>", 11) == 0);
				std::vector<uint8_t> dest((size_t)NW_ROM_SIZE, 0);
				CHECK(nw_decode_rom_image(&src[0], src.size(),
							  &dest[0], dest.size()));
				CHECK(nw_g0_unpacked_ok(&dest[0], dest.size()));
				CHECK(nw_detect_decoded_rom(&dest[0], dest.size())
				      == NW_DECODED_NEWWORLD);
				printf("G0: DecodeROM 4 MiB NewWorld from %s\n", path);
				printf("G0: unpacked +0x30d064 '%.8s' NK0 %02x%02x%02x%02x\n",
				       (char *)&dest[NW_NEWWORLD_SIG_OFFSET],
				       dest[NW_NK_V2_OFFSET], dest[NW_NK_V2_OFFSET+1],
				       dest[NW_NK_V2_OFFSET+2], dest[NW_NK_V2_OFFSET+3]);
				/* SheepShaver PatchROM needs these regions empty (0 or 'kckc'). */
				{
					const uint32_t spaces[] = {
						0x2fcf00, 0x2fcf80, 0x2fcfc0, 0x2fd100, 0x2fd118
					};
					for (unsigned s = 0; s < 5; s++) {
						uint32_t base = spaces[s];
						int empty = 1;
						for (unsigned i = 0; i < 0x40; i += 4) {
							uint32_t x = ((uint32_t)dest[base+i] << 24) |
							             ((uint32_t)dest[base+i+1] << 16) |
							             ((uint32_t)dest[base+i+2] << 8) |
							             (uint32_t)dest[base+i+3];
							if (x != 0 && x != 0x6b636b63u) {
								empty = 0;
								break;
							}
						}
						printf("G0: patch-space +0x%06x %s\n",
						       base, empty ? "free" : "occupied");
					}
				}
				{
					const uint8_t twi[] = {
						0x0f, 0xff, 0x00, 0x00, 0x0f, 0xff, 0x00, 0x01,
						0x0f, 0xff, 0x00, 0x02
					};
					int found = 0;
					for (uint32_t o = 0x36e000; o < 0x36f000; o++) {
						if (memcmp(&dest[o], twi, sizeof(twi)) == 0) {
							printf("G0: 68k-emul twi at +0x%06x\n", o);
							found = 1;
							break;
						}
					}
					if (!found)
						printf("G0: 68k-emul twi not in 0x36e000-0x36f000\n");
				}
			}
		} else {
			printf("G0 file check skipped (no local Mac OS ROM)\n");
		}
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
		CHECK(strcmp(nw_boot_line_g3_sdl2_window(),
			"G3: SDL2 bbox dirty VOSF off") == 0);
		CHECK(strcmp(nw_boot_line_g3_irq_nk(),
			"G3: HandleInterrupt NK native") == 0);
		CHECK(strcmp(nw_boot_line_g3_native_op(),
			"G3: native_op") == 0);
		CHECK(strcmp(nw_boot_line_g3_dec_arm(),
			"G3: DEC arm after G2") == 0);
		CHECK(strcmp(nw_boot_line_g3_walk_dec_ee(),
			"G3: 171-PC walk DEC blocked MSR[EE]=0") == 0);
		CHECK(strcmp(nw_boot_line_g3_dec_take(),
			"G3: DEC 0x900") == 0);
		CHECK(strcmp(nw_boot_line_g3_dec_left(),
			"G3: DEC handler left") == 0);
		CHECK(strcmp(nw_boot_line_g3_dec_leave_50326(),
			"G3: DEC leave 50326") == 0);
		CHECK(strcmp(nw_boot_line_g3_dec_leave_50326_cmp(),
			"G3: DEC leave 50326 cmp") == 0);
		CHECK(strcmp(nw_boot_line_g3_fb_guest(),
			"G3: FB guest dirty") == 0);
		CHECK(strcmp(nw_boot_line_g3_fb_none(),
			"G3: FB guest dirty none reason=EE=0 DEC never yielded walk") == 0);
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

	/* ---- ROM identity PTEs (G2 insn-side HIT seed) ---- */
	{
		mmu.reset();
		mmu.set_physical_memory(&ram[0], ram_size);
		memset(&ram[0], 0, ram_size);
		const uint32_t sdr1 = 0x00100000u;
		const uint32_t rom_base = 0x40800000u;
		nw_htab_program_rom_ptes(&ram[sdr1 & 0xffff0000u], 0x10000u, sdr1,
					 rom_base, 0x400000u,
					 (rom_base >> 8) & 0x00ffffffu);
		mmu.set_msr(ppc32_mmu::MSR_DR);
		mmu.set_sdr1(sdr1);
		mmu.set_sr(4, (rom_base >> 8) & 0x00ffffffu);
		const ppc32_xlate_result hit =
			mmu.translate(rom_base + 0x310000u, PPC32_XLATE_DR, 4);
		CHECK(hit.ok);
		CHECK(hit.pa == rom_base + 0x310000u);
		const ppc32_xlate_result miss =
			mmu.translate(0x12345000u, PPC32_XLATE_DR, 4);
		CHECK(!miss.ok);
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

	/*
	 * G2: data DSI, then DR-on lwz at SRR0 HITs; executing 0x300 is
	 * not zeros once NK has installed the DSI slot. IR/DR-off fetch
	 * of the vector is identity into that planted page.
	 */
	{
		mmu.reset();
		mmu.set_physical_memory(&ram[0], ram_size);
		memset(&ram[0], 0, ram_size);

		const uint32_t fault_pc = 0x00004000u;
		const uint32_t store_ea = 0x12345000u;
		const uint32_t handler = 0x00005000u;
		nw_fill_dsi_vector_be(&ram[0], ram_size, handler);
		be32_store(&ram[0], fault_pc, 0x90840000u);

		CHECK(nw_be32_load(&ram[0], NW_DSI_VECTOR_EA) != 0);
		CHECK(nw_be32_load(&ram[0], NW_DSI_VECTOR_EA) ==
		      (uint32_t)NW_DSI_VEC_MTSPRG1);

		mmu.set_dbat(0, 0x00000002u, 0x00000000u);
		mmu.set_ibat(0, 0x00000002u, 0x00000000u);
		mmu.set_msr(ppc32_mmu::MSR_IR | ppc32_mmu::MSR_DR);

		CHECK(!mmu.translate(store_ea, PPC32_XLATE_DR, 4).ok);

		ppc32_hotints_dsi dsi;
		dsi.take_data_dsi(mmu, fault_pc, store_ea, true);
		CHECK(dsi.srr0 == fault_pc);
		CHECK(dsi.dar == store_ea);
		CHECK(dsi.vector == (uint32_t)NW_DSI_VECTOR_EA);
		CHECK((mmu.msr() & (ppc32_mmu::MSR_IR | ppc32_mmu::MSR_DR)) == 0);
		CHECK(mmu.ivt_mapped());

		const ppc32_xlate_result vec =
			mmu.translate(NW_DSI_VECTOR_EA, PPC32_XLATE_IR, 4);
		CHECK(vec.ok);
		CHECK(vec.pa == (uint32_t)NW_DSI_VECTOR_EA);
		CHECK(nw_be32_load(&ram[0], vec.pa) != 0);
		CHECK(nw_be32_load(&ram[0], vec.pa) ==
		      (uint32_t)NW_DSI_VEC_MTSPRG1);

		const ppc32_xlate_result lwz = dsi.lwz_faulting_insn(mmu);
		CHECK(lwz.ok);
		CHECK(lwz.pa == fault_pc);
		CHECK(!mmu.translate(store_ea, PPC32_XLATE_DR, 4).ok);

		mmu.set_msr(ppc32_mmu::MSR_IR | ppc32_mmu::MSR_DR);
		const ppc32_xlate_result ir_vec =
			mmu.translate(NW_DSI_VECTOR_EA, PPC32_XLATE_IR, 4);
		CHECK(ir_vec.ok);
		CHECK(nw_be32_load(&ram[0], ir_vec.pa) != 0);
	}

	/* G2: 0x300 HITs via IVT with no BAT and no HTAB (IR/DR on). */
	{
		mmu.reset();
		mmu.set_physical_memory(&ram[0], ram_size);
		memset(&ram[0], 0, ram_size);
		nw_fill_dsi_vector_be(&ram[0], ram_size, 0x00004000u);
		mmu.set_ivt_mapped(true);
		mmu.set_msr(ppc32_mmu::MSR_IR | ppc32_mmu::MSR_DR);
		const ppc32_xlate_result hit =
			mmu.translate(NW_DSI_VECTOR_EA, PPC32_XLATE_IR, 4);
		CHECK(hit.ok);
		CHECK(hit.pa == (uint32_t)NW_DSI_VECTOR_EA);
		CHECK(nw_be32_load(&ram[0], hit.pa) != 0);
		const ppc32_xlate_result miss =
			mmu.translate(0x12345000u, PPC32_XLATE_DR, 4);
		CHECK(!miss.ok);
	}

	/* 9.0.4: IVT off, IR/DR off, 0x300 is identity (zeros allowed). */
	{
		mmu.reset();
		mmu.set_physical_memory(&ram[0], ram_size);
		memset(&ram[0], 0, ram_size);
		CHECK(!mmu.ivt_mapped());
		mmu.set_msr(0);
		const ppc32_xlate_result id =
			mmu.translate(NW_DSI_VECTOR_EA, PPC32_XLATE_IR, 4);
		CHECK(id.ok);
		CHECK(id.pa == (uint32_t)NW_DSI_VECTOR_EA);
		CHECK(nw_be32_load(&ram[0], id.pa) == 0);
		CHECK(!ppc32_guest_mmu_enabled());
	}

	/*
	 * Live 9.2.1 packet after PR #4: identity RAM BAT 10000fff/10000002
	 * (128 MiB at 0x10000000) covers KDP-1048 ea=17efdbe8. That is why
	 * xlatehow=miss and G2 HIT vanished — not the 4 KiB IVT, not ROM PTEs.
	 */
	{
		const uint32_t kdp_m1048 = 0x17efdbe8u;
		const uint32_t ram_batu = 0x10000fffu;
		const uint32_t ram_batl = 0x10000002u;

		mmu.reset();
		mmu.set_physical_memory(&ram[0], ram_size);
		memset(&ram[0], 0, ram_size);
		mmu.set_msr(ppc32_mmu::MSR_IR | ppc32_mmu::MSR_DR);
		mmu.set_ivt_mapped(true);
		mmu.set_sdr1(0x17f00000u);

		CHECK(mmu.translate(NW_DSI_VECTOR_EA, PPC32_XLATE_IR, 4).ok);
		CHECK(!mmu.translate(kdp_m1048, PPC32_XLATE_DR, 4).ok);

		mmu.set_dbat(0, ram_batu, ram_batl);
		mmu.tlbia();
		const ppc32_xlate_result swallowed =
			mmu.translate(kdp_m1048, PPC32_XLATE_DR, 4);
		CHECK(swallowed.ok);
		CHECK(swallowed.pa == kdp_m1048);
	}

	/*
	 * G2 live shape: data DSI at KDP-1048, insn page mapped, IVT 0x300
	 * planted, DR-on lwz at SRR0 HITs, original DAR still misses.
	 * Do not treat a post-DSI identity RAM BAT as the G2 HIT.
	 */
	{
		mmu.reset();
		mmu.set_physical_memory(&ram[0], ram_size);
		memset(&ram[0], 0, ram_size);

		const uint32_t fault_pc = 0x00004000u;
		const uint32_t store_ea = 0x17efdbe8u;
		const uint32_t handler = 0x00005000u;
		nw_fill_dsi_vector_be(&ram[0], ram_size, handler);
		be32_store(&ram[0], fault_pc, 0x90840000u);

		mmu.set_dbat(0, 0x00000002u, 0x00000000u);
		mmu.set_ibat(0, 0x00000002u, 0x00000000u);
		mmu.set_msr(ppc32_mmu::MSR_IR | ppc32_mmu::MSR_DR);
		mmu.set_ivt_mapped(true);

		const ppc32_xlate_result data_miss =
			mmu.translate(store_ea, PPC32_XLATE_DR, 4);
		CHECK(!data_miss.ok);

		ppc32_hotints_dsi dsi;
		dsi.take_data_dsi(mmu, fault_pc, store_ea, true);
		CHECK(dsi.srr0 == fault_pc);
		CHECK(dsi.dar == store_ea);
		CHECK(dsi.srr0 != dsi.dar);
		CHECK(dsi.vector == (uint32_t)NW_DSI_VECTOR_EA);
		CHECK(nw_be32_load(&ram[0], NW_DSI_VECTOR_EA) != 0);

		const ppc32_xlate_result vec =
			mmu.translate(NW_DSI_VECTOR_EA, PPC32_XLATE_IR, 4);
		CHECK(vec.ok);
		CHECK(nw_be32_load(&ram[0], vec.pa) != 0);

		const ppc32_xlate_result lwz = dsi.lwz_faulting_insn(mmu);
		CHECK(lwz.ok);
		CHECK(lwz.pa == fault_pc);
		CHECK(!mmu.translate(store_ea, PPC32_XLATE_DR, 4).ok);

		/* Identity RAM BAT after the miss is MemRetry, not G2. */
		mmu.set_dbat(1, 0x10000fffu, 0x10000002u);
		mmu.tlbia();
		const ppc32_xlate_result memretry =
			mmu.translate(store_ea, PPC32_XLATE_DR, 4);
		CHECK(memretry.ok);
		CHECK(memretry.pa == store_ea);
	}

	/*
	 * Without host identity BATs, KDP-1048 DR-misses. An NK/host RAM BAT
	 * 10000fff/10000002 is delayed until after the first data DSI, then
	 * DR-on lwz at SRR0 HITs, 0x300 is non-zero, original DAR still
	 * misses until the post-DSI RAM BAT is allowed.
	 */
	{
		const uint32_t kdp_m1048 = 0x17efdbe8u;
		const uint32_t ram_batu = 0x10000fffu;
		const uint32_t ram_batl = 0x10000002u;
		const uint32_t ram_base = 0x10000000u;
		const uint32_t ram_size = 0x08000000u;
		const uint32_t fault_pc = 0x00004000u;

		CHECK(ppc32_mmu::bat_overlaps(ram_batu, ram_batl, ram_base,
					      ram_base + ram_size));
		CHECK(!ppc32_mmu::bat_overlaps(0x68fe0003u, 0x68fe0002u,
						 ram_base, ram_base + ram_size));

		mmu.reset();
		mmu.set_physical_memory(&ram[0], (uint32_t)ram.size());
		memset(&ram[0], 0, ram.size());
		nw_fill_dsi_vector_be(&ram[0], (uint32_t)ram.size(), 0x00005000u);
		be32_store(&ram[0], fault_pc, 0x90840000u);

		mmu.delay_ram_bats(true, ram_base, ram_size);
		mmu.set_dbat(0, ram_batu, ram_batl);
		mmu.set_dbat(1, 0x00000002u, 0x00000000u); /* insn page */
		mmu.set_ibat(1, 0x00000002u, 0x00000000u);
		mmu.set_msr(ppc32_mmu::MSR_IR | ppc32_mmu::MSR_DR);
		mmu.set_ivt_mapped(true);
		mmu.set_sdr1(0x17f00000u);

		CHECK(mmu.ram_bats_delayed());
		const ppc32_xlate_result delayed =
			mmu.translate(kdp_m1048, PPC32_XLATE_DR, 4);
		CHECK(!delayed.ok);
		CHECK(delayed.how != NULL && strcmp(delayed.how, "miss") == 0);

		ppc32_hotints_dsi dsi;
		dsi.take_data_dsi(mmu, fault_pc, kdp_m1048, true);
		CHECK(dsi.srr0 == fault_pc);
		CHECK(dsi.dar == kdp_m1048);
		CHECK(dsi.vector == (uint32_t)NW_DSI_VECTOR_EA);
		CHECK(nw_be32_load(&ram[0], NW_DSI_VECTOR_EA) != 0);

		const ppc32_xlate_result lwz = dsi.lwz_faulting_insn(mmu);
		CHECK(lwz.ok);
		CHECK(lwz.pa == fault_pc);

		CHECK(!mmu.translate(kdp_m1048, PPC32_XLATE_DR, 4).ok);

		mmu.delay_ram_bats(false, ram_base, ram_size);
		const ppc32_xlate_result after =
			mmu.translate(kdp_m1048, PPC32_XLATE_DR, 4);
		CHECK(after.ok);
		CHECK(after.pa == kdp_m1048);
	}

	/*
	 * Live 87f42f4d: first miss ea=10010002 (lbz r30,2(r28), r28=PIC
	 * at RAMBase+0x10000), dbat3=10000003/1000003a (128 KiB at
	 * 0x10000000). Delay skips that RAM BAT so G2 can miss; after
	 * first DSI, MemRetry HITs. 0xFF at PIC+2 is the spin value.
	 */
	{
		const uint32_t ram_base = 0x10000000u;
		const uint32_t ram_size = 0x08000000u;
		const uint32_t pic_batu = 0x10000003u;
		const uint32_t pic_batl = 0x1000003au;
		const uint32_t pic = nw_nk_irq_pic_ea(ram_base);
		const uint32_t pic_status = pic + (uint32_t)NW_NK_IRQ_STATUS_OFF;
		const uint32_t fault_pc = 0x00004000u;

		CHECK(pic == 0x10010000u);
		CHECK(pic_status == 0x10010002u);
		CHECK(nw_nk_irq_status_idle() == 0);
		CHECK(nw_nk_irq_status_spins(0xff));
		CHECK(!nw_nk_irq_status_spins(nw_nk_irq_status_idle()));
		CHECK(nw_ppc_is_branch(0x4182fffcu)); /* beq *-4 */
		CHECK(nw_ppc_is_branch(0x4e800020u)); /* blr */
		CHECK(nw_ppc_is_branch((uint32_t)NW_NK_PICSPIN_BEQ_OP));
		CHECK(!nw_ppc_is_branch((uint32_t)NW_NK_PICSPIN_LBZ_OP));
		CHECK(NW_NK_PICSPIN_LBZ_OP == 0x8bdc0002u);
		CHECK(NW_NK_PICSPIN_BEQ_OP == 0x4182fc40u);
		CHECK(nw_nk_picspin_rom_off(NW_NK_PICSPIN_LBZ));
		CHECK(nw_nk_picspin_rom_off(NW_NK_PICSPIN_BEQ));
		CHECK(nw_nk_picspin_rom_off(NW_NK_PICSPIN_OLD_B));
		CHECK(!nw_nk_picspin_rom_off(0x366084u));
		CHECK(nw_nk_picspin_is_g2_dsi_off(NW_NK_PICSPIN_LBZ));
		CHECK(!nw_nk_picspin_is_g2_dsi_off(NW_NK_PICSPIN_BEQ));
		CHECK(nw_nk_picspin_skip_after_g2(NW_NK_PICSPIN_LBZ,
						  NW_NK_PICSPIN_LBZ_OP));
		CHECK(nw_nk_picspin_skip_after_g2(NW_NK_PICSPIN_BEQ,
						  NW_NK_PICSPIN_BEQ_OP));
		/* Live 855f81f6: OLD_B is not a branch and must still skip. */
		CHECK(nw_nk_picspin_skip_after_g2(NW_NK_PICSPIN_OLD_B,
						  0x60000000u));
		CHECK(nw_nk_picspin_skip_after_g2(NW_NK_PICSPIN_OLD_B,
						  NW_NK_PICSPIN_BEQ_OP));
		CHECK(nw_nk_picspin_skip_after_g2(NW_NK_PICSPIN_OLD_A,
						  0x60000000u));
		CHECK(nw_nk_picspin_skip_after_g2(NW_NK_PICSPIN_OLD_C,
						  0x60000000u));
		CHECK(!nw_nk_picspin_skip_after_g2(0x366084u,
						   NW_NK_PICSPIN_BEQ_OP));
		CHECK(!nw_nk_picspin_skip_after_g2(0x366084u,
						   0x60000000u));

		/* Live 902fbf32: skip at OLD_B logged but pc+4 stayed in
		 * the lbz loop. Leave must be the fallthrough of the first
		 * backward branch, not PC+0 / +4 / 325a14 / 325a20 / 325a9c. */
		{
			const uint32_t rom = 0x50000000u;
			const uint32_t old_b = rom + (uint32_t)NW_NK_PICSPIN_OLD_B;
			const uint32_t beq_pc = rom + (uint32_t)NW_NK_PICSPIN_BEQ;
			const uint32_t lbz_pc = rom + (uint32_t)NW_NK_PICSPIN_LBZ;
			uint32_t tgt = 0;
			uint32_t loop[3];
			uint32_t farw[3];
			uint32_t beqw[1];
			uint32_t npc;

			CHECK(nw_ppc_rel_branch_target(beq_pc,
						       (uint32_t)NW_NK_PICSPIN_BEQ_OP,
						       &tgt));
			CHECK(tgt == beq_pc - 960u);
			CHECK(tgt < beq_pc);

			beqw[0] = (uint32_t)NW_NK_PICSPIN_BEQ_OP;
			npc = nw_nk_picspin_leave_npc(beq_pc, rom, beqw, 1);
			CHECK(npc == beq_pc + 4u);
			CHECK(!nw_nk_picspin_npc_stays(npc, beq_pc, rom));

			loop[0] = (uint32_t)NW_NK_PICSPIN_LBZ_OP;
			loop[1] = 0x2c1e0000u; /* cmpwi r30,0 */
			loop[2] = 0x4182fff8u; /* beq *-8 */
			CHECK(nw_ppc_rel_branch_target(old_b + 8u, loop[2],
						       &tgt));
			CHECK(tgt == old_b);
			npc = nw_nk_picspin_leave_npc(old_b, rom, loop, 3);
			CHECK(npc == old_b + 12u);
			CHECK(npc != old_b);
			CHECK(npc != old_b + 4u);
			CHECK(npc != lbz_pc);
			CHECK(npc != beq_pc);
			CHECK(!nw_nk_picspin_npc_stays(npc, old_b, rom));
			CHECK(nw_nk_picspin_npc_stays(old_b, old_b, rom));
			/* pc+4 is not itself a listed spin off; leave_npc
			 * must still not use it when the back-edge is later. */
			CHECK(!nw_nk_picspin_npc_stays(old_b + 4u, old_b, rom));

			farw[0] = (uint32_t)NW_NK_PICSPIN_LBZ_OP;
			farw[1] = 0x60000000u;
			farw[2] = (uint32_t)NW_NK_PICSPIN_BEQ_OP;
			npc = nw_nk_picspin_leave_npc(old_b, rom, farw, 3);
			CHECK(npc == old_b + 12u);
			CHECK(!nw_nk_picspin_npc_stays(npc, old_b, rom));

			/* Live e0df3b4e: skip pc=50325a9c npc=50325aac. */
			{
				uint32_t live[4];
				live[0] = (uint32_t)NW_NK_PICSPIN_LBZ_OP;
				live[1] = 0x60000000u;
				live[2] = 0x2c1e0000u;
				live[3] = 0x4182fff4u; /* beq *-12 → old_b */
				CHECK(nw_ppc_rel_branch_target(old_b + 12u,
							       live[3], &tgt));
				CHECK(tgt == old_b);
				npc = nw_nk_picspin_leave_npc(old_b, rom,
							      live, 4);
				CHECK(npc == old_b + 16u);
				CHECK(npc == rom + 0x325aacu);
			}

			CHECK(!nw_nk_picspin_skip_after_g2(0x366084u,
							   loop[0]));
			CHECK(nw_nk_picspin_npc_stays(
				rom + 0x366084u, old_b, rom));
			CHECK(nw_nk_picspin_cycle_off(NW_NK_CYCLE_A));
			CHECK(nw_nk_picspin_cycle_off(NW_NK_CYCLE_F));
			CHECK(nw_nk_picspin_cycle_off(NW_NK_CYCLE_OLD_PAST));
			CHECK(!nw_nk_picspin_cycle_off(NW_NK_MILL_68K));
			CHECK(!nw_nk_picspin_rom_off(NW_NK_CYCLE_A));
			CHECK(nw_nk_picspin_skip_after_g2(NW_NK_CYCLE_A,
							  0x60000000u));

			CHECK(NW_NK_PICSPIN_PAST == 0x326000u);
			CHECK(nw_nk_picspin_mill_off(NW_NK_MILL_68K));
			CHECK(!nw_nk_picspin_mill_off(NW_NK_PICSPIN_OLD_B));
			CHECK(!nw_nk_picspin_skip_after_g2(NW_NK_MILL_68K,
							   0x60000000u));
			CHECK(nw_nk_picspin_npc_stays(
				rom + (uint32_t)NW_NK_PICSPIN_PAST, old_b,
				rom));
			CHECK(nw_nk_picspin_npc_stays(
				rom + (uint32_t)NW_NK_MILL_68K, old_b, rom));
			npc = nw_nk_picspin_past_npc(old_b, rom);
			CHECK(npc != rom + (uint32_t)NW_NK_PICSPIN_PAST);
			CHECK(npc != rom + (uint32_t)NW_NK_MILL_68K);
			CHECK(npc < rom + (uint32_t)NW_NK_PICSPIN_PAST);
			CHECK(npc != old_b);
			CHECK(npc != lbz_pc);
			CHECK(npc != beq_pc);
			CHECK(npc != rom + (uint32_t)NW_NK_CYCLE_A);
			CHECK(npc != rom + (uint32_t)NW_NK_CYCLE_OLD_PAST);
			CHECK(!nw_nk_picspin_npc_stays(npc, old_b, rom));
			CHECK(nw_nk_picspin_npc_stays(
				rom + (uint32_t)NW_NK_CYCLE_A, old_b, rom));
			npc = nw_nk_picspin_past_npc(beq_pc, rom);
			CHECK(npc != rom + (uint32_t)NW_NK_PICSPIN_PAST);
			CHECK(npc != rom + (uint32_t)NW_NK_MILL_68K);
			npc = nw_nk_picspin_past_npc(
				rom + (uint32_t)NW_NK_CYCLE_A, rom);
			CHECK(npc != rom + (uint32_t)NW_NK_PICSPIN_PAST);
			CHECK(npc != rom + (uint32_t)NW_NK_MILL_68K);
			CHECK(npc != rom + 0x325c84u);
			CHECK(npc != rom + (uint32_t)NW_NK_CLOUD_A);
			CHECK(!nw_nk_picspin_cycle_off(npc - rom));
			/* Live e0df3b4e: beq skip is +4, not PAST. */
			{
				uint32_t beqw2[1];
				beqw2[0] = (uint32_t)NW_NK_PICSPIN_BEQ_OP;
				npc = nw_nk_picspin_leave_npc(beq_pc, rom,
							      beqw2, 1);
				CHECK(npc == beq_pc + 4u);
				CHECK(npc != rom + (uint32_t)NW_NK_PICSPIN_PAST);
			}

			/* Cycle wait: leave like OLD_B, not to 50325c98
			 * or live 92beda4a 50325c84. */
			{
				const uint32_t cyc_pc =
					rom + (uint32_t)NW_NK_CYCLE_A;
				uint32_t cyc[2];
				uint32_t oldc[7];
				unsigned j;
				cyc[0] = (uint32_t)NW_NK_PICSPIN_LBZ_OP;
				cyc[1] = 0x4182fff8u; /* beq *-8 */
				npc = nw_nk_picspin_leave_npc(cyc_pc, rom,
							      cyc, 2);
				CHECK(npc != cyc_pc + 8u);
				CHECK(npc != rom + 0x325c84u);
				CHECK(!nw_nk_picspin_cycle_off(npc - rom));
				CHECK(npc != rom +
				      (uint32_t)NW_NK_CYCLE_OLD_PAST);
				for (j = 0; j < 6u; j++)
					oldc[j] = 0x60000000u;
				oldc[6] = (uint32_t)NW_NK_PICSPIN_BEQ_OP;
				npc = nw_nk_picspin_leave_npc(cyc_pc, rom,
							      oldc, 7);
				CHECK(npc != rom +
				      (uint32_t)NW_NK_CYCLE_OLD_PAST);
				CHECK(!nw_nk_picspin_cycle_off(npc - rom));
				CHECK(npc > cyc_pc);
			}

			CHECK(nw_nk_picspin_cycle_off(NW_NK_TAIL_A));
			CHECK(nw_nk_picspin_cycle_off(NW_NK_TAIL_B));
			CHECK(nw_nk_picspin_cycle_off(NW_NK_TAIL_C));
			CHECK(nw_nk_picspin_cycle_off(NW_NK_TAIL_A + 4u));
			CHECK(nw_nk_picspin_skip_after_g2(NW_NK_TAIL_A,
							  0x60000000u));
			CHECK(nw_nk_picspin_cycle_off(NW_NK_CLOUD_TAIL));
			CHECK(nw_nk_picspin_cycle_off(NW_NK_CLOUD_A));
			CHECK(nw_nk_picspin_cycle_off(NW_NK_CLOUD_B));
			CHECK(nw_nk_picspin_cycle_off(NW_NK_CLOUD_C));
			CHECK(nw_nk_picspin_cycle_off(NW_NK_CLOUD_MID));
			CHECK(nw_nk_picspin_cycle_off(NW_NK_STICK));
			CHECK(nw_nk_picspin_skip_after_g2(NW_NK_STICK,
							  0x60000000u));
			CHECK(nw_nk_picspin_skip_after_g2(NW_NK_CLOUD_A,
							  0x60000000u));
			CHECK(!nw_nk_picspin_skip_after_g2(NW_NK_MILL_68K,
							   0x60000000u));
			CHECK(!nw_nk_picspin_cycle_off(NW_NK_MILL_68K));
			{
				const uint32_t g = rom + (uint32_t)NW_NK_CYCLE_G;
				const uint32_t tail_a =
					rom + (uint32_t)NW_NK_TAIL_A;
				uint32_t nops[NW_NK_PICSPIN_LEAVE_INSNS];
				unsigned k;
				for (k = 0; k < (unsigned)NW_NK_PICSPIN_LEAVE_INSNS; k++)
					nops[k] = 0x60000000u;
				npc = nw_nk_picspin_leave_npc(
					g, rom, nops,
					(unsigned)NW_NK_PICSPIN_LEAVE_INSNS);
				CHECK(npc != rom + (uint32_t)NW_NK_TAIL_A);
				CHECK(npc != rom + (uint32_t)NW_NK_TAIL_B);
				CHECK(npc != rom + (uint32_t)NW_NK_TAIL_C);
				CHECK(npc != rom + (uint32_t)NW_NK_CLOUD_TAIL);
				CHECK(npc != rom + (uint32_t)NW_NK_CLOUD_A);
				CHECK(npc != rom + (uint32_t)NW_NK_CLOUD_B);
				CHECK(npc != rom + (uint32_t)NW_NK_CLOUD_C);
				CHECK(npc != rom + (uint32_t)NW_NK_STICK);
				CHECK(npc > rom + (uint32_t)NW_NK_CLOUD_TAIL);
				CHECK(!nw_nk_picspin_cycle_off(npc - rom));
				nops[0] = (uint32_t)NW_NK_PICSPIN_LBZ_OP;
				npc = nw_nk_picspin_leave_npc(
					tail_a, rom, nops,
					(unsigned)NW_NK_PICSPIN_LEAVE_INSNS);
				CHECK(npc != tail_a);
				CHECK(npc != rom + (uint32_t)NW_NK_CLOUD_TAIL);
				CHECK(npc > rom + (uint32_t)NW_NK_TAIL_HI);
				CHECK(!nw_nk_picspin_npc_stays(npc, tail_a,
							       rom));
			}

			/* Live 92beda4a listed ~50-PC cloud: skip, do
			 * not land leave_npc on any of them. */
			{
				static const uint32_t live_cloud[] = {
					0x325584u, 0x326438u, 0x3128bcu,
					0x326448u, 0x326444u, 0x326440u,
					0x32643cu, 0x326434u, 0x326430u,
					0x32642cu, 0x326424u, 0x326420u,
					0x325c84u, 0x325704u, 0x325700u,
					0x3256fcu, 0x3256f8u, 0x3256f4u,
					0x3256f0u, 0x3256ecu, 0x325660u,
					0x32558cu, 0x325588u, 0x325580u,
					0x32557cu, 0x32556cu, 0x325568u,
					0x325564u, 0x325560u, 0x32555cu,
					0x325558u, 0x325554u, 0x325520u,
					0x3128c0u, 0x3128a8u, 0x3128a4u,
					0x3128a0u, 0x31289cu, 0x3127ccu,
					0x312728u, 0x312724u, 0x312720u,
					0x312708u, 0x312704u, 0x312700u
				};
				const uint32_t g =
					rom + (uint32_t)NW_NK_CYCLE_G;
				uint32_t nops[NW_NK_PICSPIN_LEAVE_INSNS];
				unsigned k;
				unsigned ci;
				for (ci = 0; ci < sizeof(live_cloud) /
				     sizeof(live_cloud[0]); ci++) {
					CHECK(nw_nk_picspin_cycle_off(
						live_cloud[ci]));
					CHECK(nw_nk_picspin_skip_after_g2(
						live_cloud[ci], 0x60000000u));
				}
				for (k = 0; k < (unsigned)NW_NK_PICSPIN_LEAVE_INSNS; k++)
					nops[k] = 0x60000000u;
				npc = nw_nk_picspin_leave_npc(
					g, rom, nops,
					(unsigned)NW_NK_PICSPIN_LEAVE_INSNS);
				for (ci = 0; ci < sizeof(live_cloud) /
				     sizeof(live_cloud[0]); ci++)
					CHECK(npc != rom + live_cloud[ci]);
				CHECK(npc != rom + (uint32_t)NW_NK_PICSPIN_PAST);
				CHECK(npc != rom + (uint32_t)NW_NK_MILL_68K);
				CHECK(npc != rom + (uint32_t)NW_NK_STICK);
				nops[0] = (uint32_t)NW_NK_PICSPIN_LBZ_OP;
				npc = nw_nk_picspin_leave_npc(
					rom + (uint32_t)NW_NK_CLOUD_A, rom,
					nops,
					(unsigned)NW_NK_PICSPIN_LEAVE_INSNS);
				CHECK(npc != rom + (uint32_t)NW_NK_CLOUD_A);
				CHECK(npc != rom + (uint32_t)NW_NK_CLOUD_B);
				CHECK(npc != rom + (uint32_t)NW_NK_CLOUD_C);
				CHECK(!nw_nk_picspin_cycle_off(npc - rom));
				npc = nw_nk_picspin_leave_npc(
					rom + (uint32_t)NW_NK_CLOUD_B, rom,
					nops,
					(unsigned)NW_NK_PICSPIN_LEAVE_INSNS);
				CHECK(npc != rom + (uint32_t)NW_NK_CLOUD_B);
				CHECK(!nw_nk_picspin_cycle_off(npc - rom));
				npc = nw_nk_picspin_leave_npc(
					rom + (uint32_t)NW_NK_CLOUD_C, rom,
					nops,
					(unsigned)NW_NK_PICSPIN_LEAVE_INSNS);
				CHECK(npc != rom + (uint32_t)NW_NK_CLOUD_C);
				CHECK(npc != rom + (uint32_t)NW_NK_CLOUD_A);
				CHECK(npc != rom + (uint32_t)NW_NK_CLOUD_B);
				CHECK(!nw_nk_picspin_cycle_off(npc - rom));
			}

			/* Live 93eb1588: cloud left, stuck on 5032582c. */
			{
				const uint32_t stick =
					rom + (uint32_t)NW_NK_STICK;
				uint32_t nops[NW_NK_PICSPIN_LEAVE_INSNS];
				uint32_t spin[1];
				unsigned k;
				CHECK(NW_NK_STICK == 0x32582cu);
				CHECK(!nw_nk_picspin_rom_off(NW_NK_STICK));
				CHECK(nw_nk_picspin_cycle_off(NW_NK_STICK));
				CHECK(nw_nk_picspin_skip_after_g2(
					NW_NK_STICK, 0x60000000u));
				CHECK(!nw_nk_picspin_skip_after_g2(
					NW_NK_MILL_68K, 0x60000000u));
				spin[0] = 0x48000000u; /* b .+0 → self */
				npc = nw_nk_picspin_leave_npc(stick, rom,
							      spin, 1);
				CHECK(npc == stick + 4u);
				CHECK(npc != stick);
				CHECK(npc != lbz_pc);
				CHECK(npc != beq_pc);
				CHECK(npc != old_b);
				CHECK(npc != rom + (uint32_t)NW_NK_CLOUD_A);
				CHECK(npc != rom + (uint32_t)NW_NK_CLOUD_B);
				CHECK(npc != rom + (uint32_t)NW_NK_CLOUD_C);
				CHECK(npc != rom + (uint32_t)NW_NK_TAIL_A);
				CHECK(npc != rom + (uint32_t)NW_NK_PICSPIN_PAST);
				CHECK(npc != rom + (uint32_t)NW_NK_MILL_68K);
				for (k = 0; k < (unsigned)NW_NK_PICSPIN_LEAVE_INSNS; k++)
					nops[k] = 0x60000000u;
				npc = nw_nk_picspin_leave_npc(
					stick, rom, nops,
					(unsigned)NW_NK_PICSPIN_LEAVE_INSNS);
				CHECK(npc != stick);
				CHECK(npc != lbz_pc);
				CHECK(npc != beq_pc);
				CHECK(npc != old_b);
				CHECK(npc != rom + (uint32_t)NW_NK_CLOUD_A);
				CHECK(npc != rom + (uint32_t)NW_NK_CLOUD_B);
				CHECK(npc != rom + (uint32_t)NW_NK_CLOUD_C);
				CHECK(npc != rom + (uint32_t)NW_NK_CLOUD_TAIL);
				CHECK(npc != rom + (uint32_t)NW_NK_TAIL_A);
				CHECK(npc != rom + (uint32_t)NW_NK_PICSPIN_PAST);
				CHECK(npc != rom + (uint32_t)NW_NK_MILL_68K);
				CHECK(!nw_nk_picspin_cycle_off(npc - rom));
				/* Live skips that left the cloud. */
				nops[0] = (uint32_t)NW_NK_PICSPIN_LBZ_OP;
				npc = nw_nk_picspin_leave_npc(
					rom + 0x325660u, rom, nops,
					(unsigned)NW_NK_PICSPIN_LEAVE_INSNS);
				CHECK(npc != stick);
				npc = nw_nk_picspin_leave_npc(
					rom + 0x3256ecu, rom, nops,
					(unsigned)NW_NK_PICSPIN_LEAVE_INSNS);
				CHECK(npc != stick);
				npc = nw_nk_picspin_leave_npc(
					rom + 0x325c84u, rom, nops,
					(unsigned)NW_NK_PICSPIN_LEAVE_INSNS);
				CHECK(npc != stick);
			}

			{
				uint32_t nops[NW_NK_PICSPIN_LEAVE_INSNS];
				unsigned k;
				nops[0] = (uint32_t)NW_NK_PICSPIN_LBZ_OP;
				for (k = 1; k < (unsigned)NW_NK_PICSPIN_LEAVE_INSNS; k++)
					nops[k] = 0x60000000u;
				npc = nw_nk_picspin_leave_npc(
					old_b, rom, nops,
					(unsigned)NW_NK_PICSPIN_LEAVE_INSNS);
				CHECK(npc == old_b +
				      4u * (uint32_t)NW_NK_PICSPIN_LEAVE_INSNS);
				CHECK(npc != old_b + 4u);
				CHECK(npc != rom + (uint32_t)NW_NK_PICSPIN_PAST);
				CHECK(npc != rom + (uint32_t)NW_NK_MILL_68K);
			}
		}

		CHECK(ppc32_mmu::bat_overlaps(pic_batu, pic_batl, ram_base,
					      ram_base + ram_size));

		mmu.reset();
		mmu.set_physical_memory(&ram[0], (uint32_t)ram.size());
		memset(&ram[0], 0, ram.size());
		nw_fill_dsi_vector_be(&ram[0], (uint32_t)ram.size(), 0x00005000u);
		be32_store(&ram[0], fault_pc, 0x8bdc0002u);
		/* Host RAM image is 4 MiB; plant the PIC page at the same
		 * offset the guest uses (RAMBase+0x10000 → 0x10000). */
		nw_nk_irq_fill_pic_be(&ram[0], (uint32_t)ram.size(),
				      (uint32_t)NW_NK_IRQ_PIC_RAM_OFF);
		CHECK(!nw_nk_irq_status_spins(
			ram[NW_NK_IRQ_PIC_RAM_OFF + NW_NK_IRQ_STATUS_OFF]));
		ram[NW_NK_IRQ_PIC_RAM_OFF + NW_NK_IRQ_STATUS_OFF] = 0xff;
		CHECK(nw_nk_irq_status_spins(
			ram[NW_NK_IRQ_PIC_RAM_OFF + NW_NK_IRQ_STATUS_OFF]));
		nw_nk_irq_fill_pic_be(&ram[0], (uint32_t)ram.size(),
				      (uint32_t)NW_NK_IRQ_PIC_RAM_OFF);
		CHECK(!nw_nk_irq_status_spins(
			ram[NW_NK_IRQ_PIC_RAM_OFF + NW_NK_IRQ_STATUS_OFF]));

		mmu.delay_ram_bats(true, ram_base, ram_size);
		mmu.set_dbat(3, pic_batu, pic_batl);
		mmu.set_dbat(1, 0x00000002u, 0x00000000u);
		mmu.set_ibat(1, 0x00000002u, 0x00000000u);
		mmu.set_msr(ppc32_mmu::MSR_IR | ppc32_mmu::MSR_DR);
		mmu.set_ivt_mapped(true);
		mmu.set_sdr1(0x17f0000fu);

		CHECK(mmu.ram_bats_delayed());
		const ppc32_xlate_result delayed =
			mmu.translate(pic_status, PPC32_XLATE_DR, 1);
		CHECK(!delayed.ok);
		CHECK(delayed.how != NULL && strcmp(delayed.how, "miss") == 0);

		ppc32_hotints_dsi dsi;
		dsi.take_data_dsi(mmu, fault_pc, pic_status, false);
		CHECK(dsi.srr0 == fault_pc);
		CHECK(dsi.dar == pic_status);
		CHECK(dsi.vector == (uint32_t)NW_DSI_VECTOR_EA);
		CHECK(nw_be32_load(&ram[0], NW_DSI_VECTOR_EA) != 0);

		const ppc32_xlate_result lwz = dsi.lwz_faulting_insn(mmu);
		CHECK(lwz.ok);
		CHECK(lwz.pa == fault_pc);
		CHECK(!mmu.translate(pic_status, PPC32_XLATE_DR, 1).ok);

		mmu.delay_ram_bats(false, ram_base, ram_size);
		const ppc32_xlate_result memretry =
			mmu.translate(pic_status, PPC32_XLATE_DR, 1);
		CHECK(memretry.ok);
		CHECK(memretry.pa == pic_status);
	}

	/* 9.0.4 IR/DR-off still identity; RAM-BAT delay is off. */
	{
		mmu.reset();
		CHECK(!mmu.ram_bats_delayed());
		mmu.set_msr(0);
		const ppc32_xlate_result id =
			mmu.translate(0x17efdbe8u, PPC32_XLATE_DR, 4);
		CHECK(id.ok);
		CHECK(id.pa == 0x17efdbe8u);
		CHECK(id.how != NULL && strcmp(id.how, "ident") == 0);
		CHECK(!ppc32_guest_mmu_enabled());
	}

	/* Non-VOSF dirty bbox clip (SDL2 update_display_static_bbox). */
	{
		int x = -8, y = -4, w = 32, h = 16;
		CHECK(nw_video_clip_dirty(&x, &y, &w, &h, 640, 480));
		CHECK(x == 0);
		CHECK(y == 0);
		CHECK(w == 24);
		CHECK(h == 12);
		x = 630; y = 470; w = 20; h = 20;
		CHECK(nw_video_clip_dirty(&x, &y, &w, &h, 640, 480));
		CHECK(x == 630);
		CHECK(y == 470);
		CHECK(w == 10);
		CHECK(h == 10);
		x = 640; y = 0; w = 8; h = 8;
		CHECK(!nw_video_clip_dirty(&x, &y, &w, &h, 640, 480));
		x = 0; y = 0; w = 0; h = 8;
		CHECK(!nw_video_clip_dirty(&x, &y, &w, &h, 640, 480));
		x = -16; y = 10; w = 8; h = 8;
		CHECK(!nw_video_clip_dirty(&x, &y, &w, &h, 640, 480));
	}

	/* Live bedd28a3 171-PC walk. Dominant 50327b54. Not a skip. */
	{
		const uint32_t rom = 0x50000000u;
		const uint32_t walk = rom + (uint32_t)NW_NK_WALK_A;
		const uint32_t mill = rom + (uint32_t)NW_NK_MILL_68K;
		const uint32_t emul = rom + (uint32_t)NW_NK_68K_EMUL;
		const uint32_t kdp = 0x68ffe000u;

		CHECK(NW_NK_WALK_A == 0x327b54u);
		CHECK(NW_NK_IRQ_NATIVE == 0x312b1cu);
		CHECK(!nw_nk_picspin_rom_off(NW_NK_WALK_A));
		CHECK(!nw_nk_picspin_cycle_off(NW_NK_WALK_A));
		CHECK(!nw_nk_picspin_mill_off(NW_NK_WALK_A));
		CHECK(!nw_nk_picspin_skip_after_g2(NW_NK_WALK_A, 0x60000000u));
		CHECK(!nw_nk_picspin_skip_after_g2(NW_NK_WALK_A, 0x4e800020u));
		CHECK(!nw_nk_picspin_skip_after_g2(NW_NK_MILL_68K,
						     0x4e800020u));
		CHECK(nw_nk_picspin_mill_off(NW_NK_MILL_68K));

		CHECK(nw_ppc_pc_in_nk(walk, rom));
		CHECK(!nw_ppc_pc_in_nk(mill, rom));
		CHECK(!nw_ppc_pc_in_nk(emul, rom));
		CHECK(!nw_ppc_pc_in_nk(rom + (uint32_t)NW_NK_V2_OFFSET - 4u, rom));
		CHECK(nw_ppc_pc_in_nk(rom + (uint32_t)NW_NK_V2_OFFSET, rom));

		CHECK(!nw_handle_interrupt_use_native(0, walk, rom));
		CHECK(nw_handle_interrupt_use_native(1, walk, rom));
		CHECK(!nw_handle_interrupt_use_native(1, mill, rom));
		CHECK(!nw_handle_interrupt_use_native(1, emul, rom));

		CHECK(nw_handle_interrupt_skip_nested(0, kdp, kdp));
		CHECK(!nw_handle_interrupt_skip_nested(1, kdp, kdp));
		CHECK(!nw_handle_interrupt_skip_nested(1, 0x10010000u, kdp));

		CHECK(NW_NK_WALK_B == 0x327b50u);
		CHECK(NW_NK_WALK_C == 0x327b60u);
		CHECK(NW_NK_WALK_EE == 0x325600u);
		CHECK(!nw_nk_picspin_skip_after_g2(NW_NK_WALK_B, 0x60000000u));
		CHECK(!nw_nk_picspin_skip_after_g2(NW_NK_WALK_C, 0x60000000u));
		CHECK(!nw_nk_picspin_skip_after_g2(NW_NK_WALK_EE, 0x60000000u));
		CHECK(!nw_nk_picspin_skip_after_g2(NW_NK_WALK_EE, 0x4e800020u));
		CHECK(NW_NK_WALK_EE_N == 0x325604u);
		CHECK(!nw_nk_picspin_skip_after_g2(NW_NK_WALK_EE_N,
						     0x60000000u));
		CHECK(!nw_nk_picspin_skip_after_g2(NW_NK_WALK_A, 0x60000000u));
		CHECK(!nw_nk_picspin_skip_after_g2(NW_NK_MILL_68K,
						     0x4e800020u));
		/* Live 46577d78 0x900 = 50326420. Runtime does not skip
		 * while in the handler; skip-list itself is unchanged. */
		CHECK(NW_NK_CLOUD_LO_4 == 0x326420u);
		/* Live 58b12272: after DEC 0x900, sc 0x2e then twi
		 * 0fff0005 at 50324140. Do not mill that PC. */
		CHECK(!nw_nk_picspin_skip_after_g2(0x324140u, 0x0fff0005u));
		CHECK(!nw_nk_picspin_skip_after_g2(0x32412cu, 0x44000002u));
		CHECK(((0x0fff0005u >> 26) == 3) &&
		      (((0x0fff0005u >> 21) & 0x1fu) == 31u));
		CHECK(nw_ppc_pc_in_dec_handler(rom + (uint32_t)NW_NK_CLOUD_LO_4,
						 rom));
		CHECK(nw_ppc_pc_in_dec_handler(rom + 0x3264f0u, rom));
		CHECK(!nw_ppc_pc_in_dec_handler(walk, rom));
		CHECK(!nw_ppc_pc_in_dec_handler(rom + (uint32_t)NW_NK_WALK_EE,
						 rom));
		CHECK(!nw_nk_picspin_skip_after_g2(NW_NK_WALK_A, 0x60000000u));
		CHECK(NW_MSR_LIVE_EE_OFF == 0x00002000u);
		CHECK(NW_DEC_VECTOR_EA == 0x900u);
		CHECK(!nw_dec_can_yield((uint32_t)NW_MSR_LIVE_EE_OFF));
		CHECK(!nw_dec_can_yield((uint32_t)NW_MSR_EE)); /* EE, IR off */
		CHECK(nw_dec_can_yield((uint32_t)NW_MSR_EE |
					 (uint32_t)NW_MSR_IR));
		/* After G2 host takes 0x900 even if guest EE is off. */
		CHECK(nw_dec_host_take(1, (uint32_t)NW_MSR_LIVE_EE_OFF));
		CHECK(!nw_dec_ee_on((uint32_t)NW_MSR_LIVE_EE_OFF));
		CHECK(!nw_dec_host_take(0, (uint32_t)NW_MSR_LIVE_EE_OFF));
		CHECK(nw_dec_host_take(0, (uint32_t)NW_MSR_EE |
					  (uint32_t)NW_MSR_IR));
		CHECK(!nw_video_guest_paint_blocked(
			1, (uint32_t)NW_MSR_LIVE_EE_OFF, 0, 0));
		CHECK(!nw_video_guest_paint_blocked(
			1, (uint32_t)NW_MSR_EE | (uint32_t)NW_MSR_IR, 0, 0));
		CHECK(!nw_video_guest_paint_blocked(
			1, (uint32_t)NW_MSR_LIVE_EE_OFF, 1, 0));
		CHECK(!nw_video_guest_paint_blocked(
			1, (uint32_t)NW_MSR_LIVE_EE_OFF, 0, 1));
		CHECK(!nw_video_guest_paint_blocked(0, 0, 0, 0));
		CHECK(NW_GUEST_FB_RAM_OFF == 0x800000u);
		CHECK(nw_video_fb_in_ram(0x10000000u + (uint32_t)NW_GUEST_FB_RAM_OFF,
					 0x10000000u, 0x08000000u,
					 640u * 480u * 4u));
		CHECK(!nw_video_fb_in_ram(0x68fff000u, 0x10000000u,
					    0x08000000u, 0x1000u));
		CHECK(!nw_video_fb_in_ram(0x10000000u + 0x400000u,
					    0x10000000u, 0x08000000u,
					    0x08000000u)); /* size past RAM end */
		CHECK(nw_dec_arm_value() == 0x1000u);
		CHECK(!nw_dec_take_after_g2(0));
		CHECK(nw_dec_take_after_g2(1));
		CHECK(nw_dec_ee_on(0x00008000u));
		CHECK(!nw_dec_ee_on(0));
		CHECK(!nw_dec_ee_on(0x00000030u)); /* IR+DR, EE off */
		/* Live c3b5d982: 17efbb80 is r1, not SRR1. */
		CHECK(!nw_ppc_srr1_is_msr(0x17efbb80u));
		CHECK(!nw_ppc_srr1_is_msr(0x17efe360u)); /* SPRG3 */
		CHECK(nw_ppc_srr1_is_msr(0x00002000u));
		CHECK(nw_ppc_srr1_is_msr(0));
		CHECK(nw_ppc_srr1_use(0x17efbb80u) ==
		      (uint32_t)NW_MSR_LIVE_EE_OFF);
		CHECK(!nw_dec_ee_on(nw_ppc_srr1_use(0x17efbb80u)));
		CHECK(nw_ppc_srr1_use(0x00002000u) == 0x00002000u);
		CHECK(!nw_nk_picspin_skip_after_g2(NW_NK_WALK_EE,
						     0x60000000u));

		/* Live a4f0c6ce: 50326 still spun after r8 terminator.
		 * Do not skip-list those PCs. Do not mill. Do not or-in
		 * EE. 00001040 (ME+IP) after leave is a real MSR. */
		CHECK(!nw_nk_picspin_skip_after_g2(0x326674u, 0x2c08ffffu));
		CHECK(!nw_nk_picspin_skip_after_g2(0x326678u, 0x40820008u));
		CHECK(!nw_nk_picspin_skip_after_g2(0x326670u, 0x4bfff351u));
		CHECK(!nw_nk_picspin_skip_after_g2(0x3259e0u, 0x2f9c0000u));
		CHECK(!nw_nk_picspin_skip_after_g2(0x3256ccu, 0x48000b61u));
		CHECK(!nw_nk_picspin_mill_off(0x326674u));
		CHECK(!nw_nk_picspin_mill_off(0x326678u));
		CHECK(!nw_nk_picspin_mill_off(0x3259e0u));
		CHECK(nw_nk_picspin_mill_off(NW_NK_MILL_68K));
		CHECK(NW_MSR_ME == 0x00001000u);
		CHECK(NW_MSR_IP == 0x00000040u);
		CHECK(NW_MSR_DR == 0x00000010u);
		CHECK(nw_ppc_srr1_is_msr(0x00001040u));
		CHECK((0x00001040u & (uint32_t)NW_MSR_ME) != 0);
		CHECK((0x00001040u &
		       ((uint32_t)NW_MSR_IR | (uint32_t)NW_MSR_DR)) == 0);
		CHECK(!nw_dec_ee_on(0x00001040u));
		CHECK(!nw_dec_leave_pin_real(0x00001040u, 0x00007672u));
		/* 00000010 is DR, not a missing-IR+DR collapse. */
		CHECK(!nw_dec_leave_pin_real(0x00000010u, 0x00007672u));
		/* RI-only leftover still pins; collapse-to-0 still pins. */
		CHECK(nw_dec_leave_pin_real(0x00000002u, 0x00007672u));
		CHECK(nw_dec_leave_pin_real(0, 0x00007672u));
		CHECK(nw_dec_leave_pin_real(0x17efbb80u, 0x00007672u));
		CHECK(!nw_dec_leave_pin_real(0x00007672u, 0x00007672u));
		CHECK(!nw_dec_leave_pin_real(0x00003010u, 0x00007672u));
		CHECK(nw_nk_postleave_walk_off(0x326674u));
		CHECK(nw_nk_postleave_walk_off(0x3259e0u));
		CHECK(nw_nk_postleave_walk_off(0x325a9cu));
		CHECK(!nw_nk_postleave_walk_off(0x326420u));
		CHECK(!nw_nk_postleave_walk_off(0x366084u));
		CHECK(!nw_nk_picspin_mill_off(0x326674u));
		CHECK(!nw_nk_picspin_mill_off(0x325a9cu));
		CHECK(nw_ppc_is_cmp(0x2c08ffffu));
		CHECK(nw_ppc_is_cmp(0x2c1e0000u));
		CHECK(nw_ppc_is_cmp(0x7c032040u)); /* cmplw cr0,r3,r4 */
		CHECK(!nw_ppc_is_cmp(0x48000b61u));
		CHECK(nw_dec_leave_50326674_cmp(0x326674u, 0x2c08ffffu));
		CHECK(!nw_dec_leave_50326674_cmp(0x326670u, 0x4bfff351u));
		CHECK(!nw_dec_leave_50326674_cmp(0x326674u, 0x2c1e0000u));
		CHECK(nw_ppc_is_bc(0x40820008u));
		CHECK(!nw_ppc_is_bc(0x2c08ffffu));
		CHECK(nw_ppc_bc_fallthrough_cr_set(0x40820008u) == 1);
		CHECK(nw_ppc_bc_fallthrough_cr_set(0x41820008u) == 0);
		CHECK(nw_ppc_bc_fallthrough_cr_set(0x2c08ffffu) == -1);
		/* r8 bne +8: do not smash (live 3081e072 use=0). */
		CHECK(nw_dec_leave_50326_cmp_use(8, 0, 0xffffffffu,
						  0x40820008u) == 0);
		CHECK(nw_dec_leave_50326_cmp_use(8, 0x503266a7u, 0xffffffffu,
						  0x40820008u) == 0x503266a7u);
		/* r8 beq when already -1 → 0. */
		CHECK(nw_dec_leave_50326_cmp_use(8, 0xffffffffu, 0xffffffffu,
						  0x41820008u) == 0);
		/* Other GPR: make bne fall through (EQ). */
		CHECK(nw_dec_leave_50326_cmp_use(9, 1, 0, 0x40820008u) == 0);
		/* cmplw + blt +8: fall through with RA>=RB. */
		CHECK(nw_dec_leave_50326_cmp_use(3, 0, 100u,
						  0x41800008u) == 100u);

		/* Live 2d295270: 503256f4 cmp+li is not a wait.
		 * Completing 503264fc bne -16 hung silent at
		 * 50326480. Complete 50326480 like 50326674.
		 * Do not mill or skip-list. Do not pin 00003010. */
		CHECK(nw_ppc_is_li(0x3bc00000u)); /* li r30,0 */
		CHECK(!nw_ppc_is_li(0x40820008u));
		CHECK(!nw_ppc_is_li(0x2c9e0000u));
		CHECK(nw_ppc_bc_disp(0x40820008u) == 8);
		CHECK(nw_ppc_bc_disp(0x4082fff0u) == -16);
		CHECK(!nw_dec_leave_cmp_wait(0x3bc00000u));
		CHECK(!nw_dec_leave_cmp_wait(0x2c9e0000u));
		CHECK(!nw_dec_leave_cmp_wait(0x4082fff0u)); /* backward */
		CHECK(nw_dec_leave_cmp_wait(0x40820008u));
		CHECK(nw_dec_leave_cmp_wait(0x41820008u));
		CHECK(nw_dec_leave_503256f4_false(0x3256f4u, 0x2c9e0000u,
						   0x3bc00000u));
		CHECK(nw_dec_leave_503256f4_false(0x32663cu, 0x2c080000u,
						   0x3bc00000u));
		CHECK(!nw_dec_leave_503256f4_false(0x32663cu, 0x2c08ffffu,
						    0x40820008u));
		CHECK(nw_dec_leave_503256f4_false(0x3264f8u, 0x2c080000u,
						    0x4082fff0u));
		CHECK(nw_dec_leave_50326480_off(0x326480u));
		CHECK(nw_dec_leave_50326480_off(0x326484u));
		CHECK(nw_dec_leave_50326480_off(0x326478u));
		CHECK(!nw_dec_leave_50326480_off(0x3264fcu));
		CHECK(!nw_dec_leave_50326480_off(0x326674u));
		CHECK(nw_dec_leave_hb_wait_off(0x326480u));
		CHECK(nw_dec_leave_hb_wait_off(0x32663cu));
		CHECK(nw_dec_leave_hb_wait_off(0x3259dcu));
		CHECK(!nw_dec_leave_hb_wait_off(0x3264fcu));
		CHECK(!nw_dec_leave_hb_wait_off(0x326674u));
		CHECK(!nw_dec_leave_hb_wait_off(0x3256f4u));
		CHECK(nw_nk_postleave_walk_off(0x326480u));
		CHECK(nw_nk_postleave_walk_off(0x32663cu));
		CHECK(nw_nk_postleave_walk_off(0x3259dcu));
		CHECK(nw_nk_postleave_walk_off(0x3256f4u));
		CHECK(!nw_nk_picspin_skip_after_g2(0x326480u, 0x2c08ffffu));
		CHECK(!nw_nk_picspin_skip_after_g2(0x32663cu, 0x2c08ffffu));
		CHECK(!nw_nk_picspin_skip_after_g2(0x3259dcu, 0x2f9c0000u));
		CHECK(!nw_nk_picspin_skip_after_g2(0x3264fcu, 0x4082fff0u));
		CHECK(!nw_nk_picspin_mill_off(0x326480u));
		CHECK(!nw_nk_picspin_mill_off(0x32663cu));
		CHECK(!nw_nk_picspin_mill_off(0x3259dcu));
		CHECK(!nw_nk_picspin_mill_off(0x3264fcu));
		CHECK(!nw_nk_picspin_mill_off(0x3256f4u));
		CHECK(!nw_dec_ee_on(0x00003010u));
		CHECK(!nw_dec_leave_pin_real(0x00003010u, 0x00007672u));
		CHECK(nw_dec_leave_50326_cmp_use(8, 0, 0xffffffffu,
						  0x40820008u) == 0);

		/* Live 5dd8d481: 503264f8 cmpw r0,r3 nxt=4082fff0
		 * did not unstick 50326480. Do not complete that
		 * pair. Do not smash r0. */
		CHECK(nw_ppc_is_cmp(0x7c001800u));
		CHECK(nw_dec_leave_503264f8_false(0x3264f8u, 0x7c001800u,
						    0x4082fff0u));
		CHECK(nw_dec_leave_503264f8_false(0x3264fcu, 0x4082fff0u,
						    0));
		CHECK(!nw_dec_leave_503264f8_false(0x326480u, 0x2c08ffffu,
						     0x40820008u));
		CHECK(nw_dec_leave_50326_cmp_use(0, 0x00000c28u, 0,
						  0x4082fff0u) == 0x00000c28u);
		{
			uint32_t w[4];
			w[0] = 0x2c080000u;
			w[1] = 0x40820008u;
			w[2] = 0;
			w[3] = 0;
			CHECK(nw_dec_leave_50326480_arm_idx(0x326480u, w, 4u) == 1);
			w[1] = 0x4082fff0u;
			CHECK(nw_dec_leave_50326480_arm_idx(0x326480u, w, 4u) == -1);
			w[0] = 0x40820008u;
			CHECK(nw_dec_leave_50326480_arm_idx(0x326480u, w, 4u) == 0);
			w[0] = 0x7c001800u;
			w[1] = 0x4082fff0u;
			CHECK(nw_dec_leave_50326480_arm_idx(0x3264f8u, w, 4u) == -1);
		}
		CHECK(!nw_nk_picspin_skip_after_g2(0x3264f8u, 0x7c001800u));
		CHECK(!nw_nk_picspin_mill_off(0x3264f8u));

		/* Live 183fe016: 50326480 is rlwinm. 5400001d
		 * nxt=4082000c. Hang advanced to 50326564. */
		CHECK(nw_ppc_is_rlwinm_rc(0x5400001du));
		CHECK(!nw_ppc_is_rlwinm_rc(0x5400001cu)); /* Rc=0 */
		CHECK(!nw_ppc_is_cmp(0x5400001du));
		CHECK(nw_dec_leave_50326480_rlwinm(0x326480u, 0x5400001du,
						    0x4082000cu));
		CHECK(!nw_dec_leave_50326480_rlwinm(0x326564u, 0x5400001du,
						     0x4082000cu));
		CHECK(nw_dec_leave_cr_then_fwd_bc(0x5400001du, 0x4082000cu));
		CHECK(nw_dec_leave_cr_then_fwd_bc(0x2c08ffffu, 0x40820008u));
		CHECK(!nw_dec_leave_cr_then_fwd_bc(0x5400001du, 0x4082fff0u));
		CHECK(!nw_dec_leave_cr_then_fwd_bc(0x5400001du, 0x3bc00000u));
		{
			uint32_t w[4];
			w[0] = 0x5400001du;
			w[1] = 0x4082000cu;
			w[2] = 0;
			w[3] = 0;
			CHECK(nw_dec_leave_50326480_arm_idx(0x326564u, w, 4u) == 1);
			CHECK(nw_dec_leave_50326480_arm_idx(0x326480u, w, 4u) == 1);
		}
		CHECK(nw_ppc_bc_disp(0x4082000cu) == 12);
		CHECK(nw_dec_leave_cmp_wait(0x4082000cu));
		CHECK(nw_dec_leave_50326564_off(0x326564u));
		CHECK(nw_dec_leave_50326564_off(0x326568u));
		CHECK(nw_dec_leave_50326564_off(0x32655cu));
		CHECK(!nw_dec_leave_50326564_off(0x326480u));
		CHECK(!nw_dec_leave_50326564_off(0x3264fcu));
		CHECK(!nw_dec_leave_50326564_off(0x326674u));
		CHECK(nw_dec_leave_hb_wait_off(0x326564u));
		CHECK(nw_nk_postleave_walk_off(0x326564u));
		CHECK(!nw_nk_picspin_skip_after_g2(0x326564u, 0x4082000cu));
		CHECK(!nw_nk_picspin_mill_off(0x326564u));
		CHECK(!nw_dec_leave_cmp_wait(0x4082fff0u));
		{
			uint32_t w[4];
			w[0] = 0x2c080000u;
			w[1] = 0x4082000cu;
			w[2] = 0;
			w[3] = 0;
			CHECK(nw_dec_leave_50326480_arm_idx(0x326564u, w, 4u) == 1);
			w[0] = 0x4082000cu;
			CHECK(nw_dec_leave_50326480_arm_idx(0x326564u, w, 4u) == 0);
			w[0] = 0x2c080000u;
			w[1] = 0x4082fff0u;
			CHECK(nw_dec_leave_50326480_arm_idx(0x326564u, w, 4u) == -1);
		}

		{
			uint8_t a[16], b[16];
			memset(a, 0, sizeof(a));
			memset(b, 0, sizeof(b));
			CHECK(!nw_video_fb_guest_dirty(a, b, sizeof(a)));
			a[7] = 0x5a;
			CHECK(nw_video_fb_guest_dirty(a, b, sizeof(a)));
			CHECK(!nw_video_fb_guest_dirty(NULL, b, sizeof(a)));
			CHECK(!nw_video_fb_guest_dirty(a, b, 0));
		}
	}

	printf("SheepShaver-MMUTests: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
