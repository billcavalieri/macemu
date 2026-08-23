/*
 *  ppc-mmu.cpp - Just-enough PowerPC 32-bit MMU (BAT + HTAB)
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

#include "cpu/ppc/ppc-mmu.hpp"

ppc32_mmu::ppc32_mmu()
{
	phys_ = 0;
	phys_size_ = 0;
	tlb_next_ = 0;
	reset();
}

void ppc32_mmu::reset()
{
	msr_ = 0;
	sdr1_ = 0;
	unsigned i;
	for (i = 0; i < NSR; i++)
		sr_[i] = 0;
	for (i = 0; i < NBAT; i++) {
		ibatu_[i] = 0;
		ibatl_[i] = 0;
		dbatu_[i] = 0;
		dbatl_[i] = 0;
	}
	tlbia();
}

void ppc32_mmu::set_physical_memory(uint8_t *base, uint32_t size)
{
	phys_ = base;
	phys_size_ = size;
}

void ppc32_mmu::set_msr(uint32_t value)
{
	msr_ = value;
}

void ppc32_mmu::set_sdr1(uint32_t value)
{
	sdr1_ = value;
}

void ppc32_mmu::set_sr(unsigned i, uint32_t value)
{
	if (i < NSR)
		sr_[i] = value;
}

uint32_t ppc32_mmu::sr(unsigned i) const
{
	return (i < NSR) ? sr_[i] : 0;
}

void ppc32_mmu::set_ibat(unsigned i, uint32_t upper, uint32_t lower)
{
	if (i < NBAT) {
		ibatu_[i] = upper;
		ibatl_[i] = lower;
	}
}

void ppc32_mmu::set_dbat(unsigned i, uint32_t upper, uint32_t lower)
{
	if (i < NBAT) {
		dbatu_[i] = upper;
		dbatl_[i] = lower;
	}
}

void ppc32_mmu::tlbie(uint32_t ea)
{
	const uint32_t page = ea & ~0xfffu;
	for (unsigned i = 0; i < NTLB; i++) {
		if (tlb_[i].valid && tlb_[i].ea_page == page)
			tlb_[i].valid = false;
	}
}

void ppc32_mmu::tlbia()
{
	for (unsigned i = 0; i < NTLB; i++)
		tlb_[i].valid = false;
	tlb_next_ = 0;
}

bool ppc32_mmu::relocation_on(ppc32_xlate_space space) const
{
	if (space == PPC32_XLATE_IR)
		return (msr_ & MSR_IR) != 0;
	return (msr_ & MSR_DR) != 0;
}

bool ppc32_mmu::phys_read32(uint32_t pa, uint32_t *value) const
{
	if (phys_ == 0 || (uint64_t)pa + 4 > phys_size_)
		return false;
	const uint8_t *p = phys_ + pa;
	*value = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
	return true;
}

bool ppc32_mmu::bat_hit(uint32_t ea, bool insn, uint32_t *pa) const
{
	const uint32_t *upper = insn ? ibatu_ : dbatu_;
	const uint32_t *lower = insn ? ibatl_ : dbatl_;
	const bool priv = (msr_ & MSR_PR) != 0;

	for (unsigned i = 0; i < NBAT; i++) {
		const uint32_t batu = upper[i];
		const uint32_t batl = lower[i];
		const bool vs = (batu & 2) != 0;
		const bool vp = (batu & 1) != 0;
		if (priv ? !vp : !vs)
			continue;

		const uint32_t bl = (batu >> 2) & 0x1fffu;
		const uint32_t block_mask = (bl << 17) | 0x1ffffu;
		const uint32_t bepi = batu & 0xfffe0000u;
		if (((ea ^ bepi) & ~block_mask) != 0)
			continue;

		const uint32_t brpn = batl & 0xfffe0000u;
		*pa = (brpn & ~block_mask) | (ea & block_mask);
		return true;
	}
	return false;
}

bool ppc32_mmu::htab_hit(uint32_t ea, uint32_t *pa)
{
	const uint32_t sr_val = sr_[(ea >> 28) & 0xfu];
	if (sr_val & 0x80000000u)
		return false; /* T=1 direct-store: not implemented */

	const uint32_t vsid = sr_val & 0x00ffffffu;
	const uint32_t page_index = (ea >> 12) & 0xffffu;
	const uint32_t api = (ea >> 22) & 0x3fu;
	const uint32_t hash0 = (vsid & 0x7ffffu) ^ page_index;
	const uint32_t htaborg = sdr1_ & 0xffff0000u;
	const uint32_t htabmask = ((sdr1_ & 0x1ffu) << 16) | 0xffffu;

	for (int hash_id = 0; hash_id < 2; hash_id++) {
		const uint32_t hash = hash_id ? (hash0 ^ 0x7ffffu) : hash0;
		const uint32_t pteg = htaborg | ((hash * 64u) & htabmask);

		for (unsigned slot = 0; slot < 8; slot++) {
			uint32_t w0, w1;
			if (!phys_read32(pteg + slot * 8u, &w0))
				return false;
			if (!phys_read32(pteg + slot * 8u + 4u, &w1))
				return false;

			const bool v = (w0 & 0x80000000u) != 0;
			const bool h = (w0 & 0x00000040u) != 0;
			const uint32_t pte_vsid = (w0 >> 7) & 0x00ffffffu;
			const uint32_t pte_api = w0 & 0x3fu;
			if (!v || h != (hash_id != 0) || pte_vsid != vsid || pte_api != api)
				continue;

			const uint32_t rpn = w1 >> 12;
			*pa = (rpn << 12) | (ea & 0xfffu);
			return true;
		}
	}
	return false;
}

void ppc32_mmu::tlb_insert(uint32_t ea, uint32_t pa, bool insn)
{
	tlb_entry &e = tlb_[tlb_next_];
	e.valid = true;
	e.insn = insn;
	e.ea_page = ea & ~0xfffu;
	e.pa_page = pa & ~0xfffu;
	tlb_next_ = (tlb_next_ + 1) % NTLB;
}

bool ppc32_mmu::tlb_lookup(uint32_t ea, bool insn, uint32_t *pa) const
{
	const uint32_t page = ea & ~0xfffu;
	for (unsigned i = 0; i < NTLB; i++) {
		const tlb_entry &e = tlb_[i];
		if (e.valid && e.insn == insn && e.ea_page == page) {
			*pa = e.pa_page | (ea & 0xfffu);
			return true;
		}
	}
	return false;
}

ppc32_xlate_result ppc32_mmu::translate(uint32_t ea, ppc32_xlate_space space, unsigned width)
{
	ppc32_xlate_result r;
	r.ok = false;
	r.pa = 0;

	if (width == 0)
		return r;

	if (!relocation_on(space)) {
		r.ok = true;
		r.pa = ea;
		return r;
	}

	const bool insn = (space == PPC32_XLATE_IR);
	uint32_t pa;

	if (tlb_lookup(ea, insn, &pa)) {
		r.ok = true;
		r.pa = pa;
		return r;
	}

	if (bat_hit(ea, insn, &pa)) {
		tlb_insert(ea, pa, insn);
		r.ok = true;
		r.pa = pa;
		return r;
	}

	if (htab_hit(ea, &pa)) {
		tlb_insert(ea, pa, insn);
		r.ok = true;
		r.pa = pa;
		return r;
	}

	return r;
}
