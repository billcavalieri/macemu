/*
 *  ppc-mmu.hpp - Just-enough PowerPC 32-bit MMU (BAT + HTAB)
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

#ifndef PPC_MMU_H
#define PPC_MMU_H

#include <stdint.h>

/*
 * Guest-truth EA → PA translator. This is the only address map. Interpreter
 * and JIT must call ppc32_mmu::translate(); do not add a second map.
 *
 * First cut is just-enough PPC32 (MSR[IR]/MSR[DR], BAT, SDR1 HTAB,
 * primary then secondary hash, software TLB + tlbie). Not a chipset.
 */

enum ppc32_xlate_space {
	PPC32_XLATE_IR = 0,
	PPC32_XLATE_DR = 1
};

/*
 * fault: DSISR (data) / SRR1 (fetch) bits when !ok, as the OEA defines them
 * and QEMU's hash32 MMU sets them:
 *   0x40000000  no translation (no BAT, no PTE)
 *   0x08000000  protection violation (PP/key, BAT PP)
 *   0x10000000  fetch from a no-execute segment or guarded page
 * The store bit (0x02000000) is added by the caller for data faults.
 */
enum {
	PPC32_FAULT_NOTRANS = 0x40000000u,
	PPC32_FAULT_PROT = 0x08000000u,
	PPC32_FAULT_NOEXEC = 0x10000000u
};

struct ppc32_xlate_result {
	bool ok;
	uint32_t pa;
	uint32_t fault;
};

class ppc32_mmu
{
public:
	enum {
		MSR_RI = 0x00000002,
		MSR_DR = 0x00000010,
		MSR_IR = 0x00000020,
		MSR_IP = 0x00000040,
		MSR_PR = 0x00004000,
		MSR_EE = 0x00008000,
		/* Bits cleared on interrupt (EE, PR, FP, FE0, SE, BE, FE1, IR, DR, RI). */
		MSR_EXC_CLEAR = 0x0000ef32
	};

	typedef bool (*phys_read32_fn)(void *ctx, uint32_t pa, uint32_t *value);
	typedef bool (*phys_write32_fn)(void *ctx, uint32_t pa, uint32_t value);

	ppc32_mmu();

	void reset();

	void set_physical_memory(uint8_t *base, uint32_t size);
	void set_phys_read32(phys_read32_fn fn, void *ctx);
	/* Used to set the PTE R/C bits in the guest HTAB (QEMU does the same). */
	void set_phys_write32(phys_write32_fn fn, void *ctx);

	void set_msr(uint32_t value);
	uint32_t msr() const { return msr_; }

	void set_sdr1(uint32_t value);
	uint32_t sdr1() const { return sdr1_; }

	void set_sr(unsigned i, uint32_t value);
	uint32_t sr(unsigned i) const;

	void set_ibat(unsigned i, uint32_t upper, uint32_t lower);
	void set_dbat(unsigned i, uint32_t upper, uint32_t lower);

	void tlbie(uint32_t ea);
	void tlbia();

	/*
	 * space selects instruction vs data translation (MSR[IR] vs MSR[DR]).
	 * width is the access size in bytes (1/2/4/8), used as a non-zero
	 * access marker. is_store asks for write permission (data only) and
	 * sets the PTE C bit on success; fetches need execute permission.
	 * Protection follows the OEA: key = MSR[PR] ? Kp : Ks; PP 0..3 with
	 * key 0 -> RW RW RW RO, key 1 -> none RO RW RO; BAT PP 0 none, 2 RW,
	 * 1/3 RO. BAT translations are not cached; TLB entries are tagged with
	 * the segment register they were derived from.
	 */
	ppc32_xlate_result translate(uint32_t ea, ppc32_xlate_space space, unsigned width,
				     bool is_store = false);

	void get_ibat(unsigned i, uint32_t *upper, uint32_t *lower) const;
	void get_dbat(unsigned i, uint32_t *upper, uint32_t *lower) const;

private:
	enum { NBAT = 4, NSR = 16, NTLB = 64 };
	enum { PROT_R = 1, PROT_W = 2, PROT_X = 4 };

	struct tlb_entry {
		bool valid;
		bool insn;
		bool guarded;
		bool c_set;
		bool r_set;
		uint8_t pp;
		uint32_t ea_page;
		uint32_t pa_page;
		uint32_t sr_val;
		uint32_t pte_pa;	/* address of PTE word 1 (R/C updates) */
	};

	bool relocation_on(ppc32_xlate_space space) const;
	bool bat_hit(uint32_t ea, bool insn, uint32_t *pa, unsigned *prot) const;
	bool htab_hit(uint32_t ea, tlb_entry *e);
	bool phys_read32(uint32_t pa, uint32_t *value) const;
	bool phys_write32(uint32_t pa, uint32_t value);
	tlb_entry *tlb_insert(const tlb_entry &e);
	tlb_entry *tlb_lookup(uint32_t ea, bool insn);
	unsigned pte_prot(uint8_t pp, uint32_t sr_val) const;
	void note_access(tlb_entry *e, bool is_store);

	uint8_t *phys_;
	uint32_t phys_size_;
	phys_read32_fn phys_read32_;
	void *phys_read32_ctx_;
	phys_write32_fn phys_write32_;
	void *phys_write32_ctx_;
	uint32_t msr_;
	uint32_t sdr1_;
	uint32_t sr_[NSR];
	uint32_t ibatu_[NBAT];
	uint32_t ibatl_[NBAT];
	uint32_t dbatu_[NBAT];
	uint32_t dbatl_[NBAT];
	tlb_entry tlb_[NTLB];
	unsigned tlb_next_;
};

/*
 * HotInts DataStorageInt (elliotnunn/NanoKernel HotInts.s).
 *
 * First DSI is identified by SRR0 (faulting insn) + a DR-on lwz of that
 * insn. DAR/DSISR are filled because the architecture writes them and
 * AlignmentInt reads them (mfdsisr/mfdar) — they are not how first DSI
 * finds the store/load.
 *
 * SPRG0=KDP, SPRG1=saved r1, SPRG2=LR, SPRG3=VecTbl (handler contract).
 */
struct ppc32_hotints_dsi {
	uint32_t srr0;
	uint32_t srr1;
	uint32_t dar;
	uint32_t dsisr;
	uint32_t vector;
	uint32_t sprg[4];

	void take_data_dsi(ppc32_mmu &mmu, uint32_t fault_pc, uint32_t fault_ea, bool is_store,
			   uint32_t fault = PPC32_FAULT_NOTRANS)
	{
		srr0 = fault_pc;
		srr1 = mmu.msr();
		dar = fault_ea;
		/* Bit 1 = no translation / bit 4 = protection; bit 6 = store.
		 * AlignmentInt and the NK's fault handler read these. */
		dsisr = (fault ? fault : PPC32_FAULT_NOTRANS) | (is_store ? 0x02000000u : 0);
		vector = (srr1 & ppc32_mmu::MSR_IP) ? 0xfff00300u : 0x300u;
		mmu.set_msr(mmu.msr() & ~ppc32_mmu::MSR_EXC_CLEAR);
	}

	/* Handler: MSR[DR] ON, lwz at SRR0. Must HIT — no second DSI. */
	ppc32_xlate_result lwz_faulting_insn(ppc32_mmu &mmu) const
	{
		mmu.set_msr(mmu.msr() | ppc32_mmu::MSR_DR);
		return mmu.translate(srr0, PPC32_XLATE_DR, 4);
	}
};

ppc32_mmu &ppc32_guest_mmu();
void ppc32_guest_mmu_enable(bool on);
bool ppc32_guest_mmu_enabled();

#endif /* PPC_MMU_H */
