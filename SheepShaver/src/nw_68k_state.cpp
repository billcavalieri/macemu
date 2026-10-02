#include "nw_68k_core.h"

void nw68_import(const nw68_nk_state &nk, nw68_state &s)
{
	for (unsigned i = 0; i < 8; ++i) {
		s.d[i] = nk.gpr[8 + i];
		s.a[i] = nk.gpr[i == 7 ? 1 : 16 + i];
	}
	s.pc = nk.gpr[24] - 2u;
	s.so = (nk.xer & 0x80000000u) != 0;
	s.extend_so = (nk.gpr[26] & 0x80000000u) != 0;
	/* Verified against NK v2's MoveFromCCR at 6806cd80 and MoveToCCR
 * at 6806cdd8. X is r26.CA, N/Z are CR0, V/C are XER.OV/CA. */
	s.ccr = ((nk.gpr[26] >> 25) & NW68_X) |
		((nk.cr >> 28) & NW68_N) | ((nk.cr >> 27) & NW68_Z) |
		((nk.xer >> 29) & (NW68_V | NW68_C));
}

void nw68_export(nw68_nk_state &nk, const nw68_state &s, unsigned flags,
		 uint16_t next_op, uint16_t prefetch)
{
	nk.xer = (nk.xer & ~0x80000000u) | (s.so ? 0x80000000u : 0u);
	for (unsigned i = 0; i < 8; ++i) {
		nk.gpr[8 + i] = s.d[i];
		nk.gpr[i == 7 ? 1 : 16 + i] = s.a[i];
	}
	if (flags & (NW68_N | NW68_Z)) {
		uint32_t nz = ((s.ccr & NW68_N) ? 0x80000000u : 0u) |
			((s.ccr & NW68_Z) ? 0x20000000u : 0u);
		if (!nz) nz = 0x40000000u;
		nk.cr = (nk.cr & ~0xe0000000u) | nz;
	}
	if (flags & NW68_V) {
		nk.xer = (nk.xer & ~0x40000000u) |
			((s.ccr & NW68_V) ? 0x40000000u : 0u);
		/* Materialize the NK's summary state, including explicit replacements. */
		nk.cr = (nk.cr & ~0x10000000u) | ((nk.xer >> 3) & 0x10000000u);
	}
	if (flags & NW68_C)
		nk.xer = (nk.xer & ~0x20000000u) | ((s.ccr & NW68_C) ? 0x20000000u : 0u);
	if (flags & NW68_X)
		nk.gpr[26] = (nk.gpr[26] & ~0xa0000000u) | ((s.ccr & NW68_X) ? 0x20000000u : 0u) |
			(s.extend_so ? 0x80000000u : 0u);
	/* r24 addresses the next prefetched word, not the opcode itself.
 * Never replace CR2: its wake/IRQ/privilege state belongs to the NK. */
	nk.gpr[24] = s.pc + 2u;
	nk.gpr[27] = (uint32_t)(int32_t)(int16_t)prefetch;
	nk.gpr[29] = (nk.gpr[29] & ~0x0007fff8u) | ((uint32_t)next_op << 3);
	nk.lr = nk.gpr[29];
	nk.ppc_pc = nk.gpr[29];
}
