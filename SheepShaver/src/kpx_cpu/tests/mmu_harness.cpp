/*
 *  mmu_harness.cpp - Host-side SheepShaver-MMUTests (WP2)
 *
 *  SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 *
 *  Programs BAT + a synthetic HTAB and asserts BAT hit, HTAB hit, fault,
 *  IR-only vs DR-only, tlbie dropping a cached translation, and the G2
 *  HotInts accept: after a data DSI, MSR[DR]-on lwz of the faulting insn
 *  at SRR0 must HIT (BAT or HTAB) and must not take a second DSI.
 *  First DSI is not DSISR-only; AlignmentInt is still mfdsisr/mfdar.
 */

#include "cpu/ppc/ppc-mmu.hpp"

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
