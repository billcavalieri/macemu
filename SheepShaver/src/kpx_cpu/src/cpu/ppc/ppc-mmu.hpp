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

struct ppc32_xlate_result {
	bool ok;
	uint32_t pa;
};

class ppc32_mmu
{
public:
	enum {
		MSR_DR = 0x00000010,
		MSR_IR = 0x00000020,
		MSR_PR = 0x00004000
	};

	ppc32_mmu();

	void reset();

	void set_physical_memory(uint8_t *base, uint32_t size);

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
	 * width is the access size in bytes (1/2/4/8); first cut uses it only
	 * as a non-zero access marker.
	 */
	ppc32_xlate_result translate(uint32_t ea, ppc32_xlate_space space, unsigned width);

private:
	enum { NBAT = 4, NSR = 16, NTLB = 64 };

	struct tlb_entry {
		bool valid;
		bool insn;
		uint32_t ea_page;
		uint32_t pa_page;
	};

	bool relocation_on(ppc32_xlate_space space) const;
	bool bat_hit(uint32_t ea, bool insn, uint32_t *pa) const;
	bool htab_hit(uint32_t ea, uint32_t *pa);
	bool phys_read32(uint32_t pa, uint32_t *value) const;
	void tlb_insert(uint32_t ea, uint32_t pa, bool insn);
	bool tlb_lookup(uint32_t ea, bool insn, uint32_t *pa) const;

	uint8_t *phys_;
	uint32_t phys_size_;
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

#endif /* PPC_MMU_H */
