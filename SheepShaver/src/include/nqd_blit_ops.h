/*
 *  nqd_blit_ops.h - QuickDraw blit pixel operations (reference semantics)
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
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

#ifndef NQD_BLIT_OPS_H
#define NQD_BLIT_OPS_H

#include <stdint.h>

/*
 * A 32-bit Mac pixel is handled as a host word whose byte i is memory byte i:
 * byte 0 pad, bytes 1..3 R, G, B. These functions are the CPU twin of qd_fetch /
 * qd_op in MacOSX/SheepForce.metal. The CPU fallback in gfxaccel.cpp and the
 * test harness use them, so GPU and CPU paths are required to agree.
 */

static inline uint32_t nqd_word_load(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void nqd_word_store(uint8_t *p, uint32_t w)
{
	p[0] = (uint8_t)w;
	p[1] = (uint8_t)(w >> 8);
	p[2] = (uint8_t)(w >> 16);
	p[3] = (uint8_t)(w >> 24);
}

/* Mac word for an 8-bit-per-channel colour */
static inline uint32_t nqd_pack_rgb(uint8_t r, uint8_t g, uint8_t b)
{
	return ((uint32_t)r << 8) | ((uint32_t)g << 16) | ((uint32_t)b << 24);
}

/* 15/16-bit source pixel (big-endian 5-5-5), expanded with a shift of 3 */
static inline uint32_t nqd_expand555(uint16_t v)
{
	return nqd_pack_rgb((uint8_t)(((v >> 10) & 31) * 8), (uint8_t)(((v >> 5) & 31) * 8), (uint8_t)((v & 31) * 8));
}

/* Per-component arithmetic on bytes 1..3 (the pad byte comes out 0).
 * op 0 add (wraps), 1 subtract (wraps), 2 max, 3 min. */
static inline uint32_t nqd_bytes(uint32_t a, uint32_t b, int op)
{
	uint32_t r = 0;
	for (int i = 1; i < 4; i++) {
		int x = (int)((a >> (8 * i)) & 0xff), y = (int)((b >> (8 * i)) & 0xff), v;
		if (op == 0) v = (x + y) & 0xff;
		else if (op == 1) v = (x - y) & 0xff;
		else if (op == 2) v = x > y ? x : y;
		else v = x < y ? x : y;
		r |= (uint32_t)v << (8 * i);
	}
	return r;
}

/*
 * Transfer modes the accelerator reproduces exactly for a 32-bit destination:
 *   0-7  srcCopy srcOr srcXor srcBic notSrcCopy notSrcOr notSrcXor notSrcBic (bitwise)
 *   34   addOver, 38 subOver, 37 adMax, 39 adMin (per component)
 *   36   transparent (source pixels equal to the background are skipped)
 * Modes that need OpColor or the highlight colour (32 blend, 33 addPin, 35 subPin,
 * 50 hilite) are not implemented here and stay with the ROM.
 */
static inline bool nqd_blit_mode_ok(int mode, int dbpp, int sbpp)
{
	if (dbpp == 4) {
		if (sbpp != 1 && sbpp != 2 && sbpp != 4)
			return false;
		if (mode >= 0 && mode <= 7)
			return true;
		if (mode == 34 || mode == 37 || mode == 38 || mode == 39)
			return true;
		return mode == 36 && sbpp == 4;
	}
	return (dbpp == 1 || dbpp == 2) && sbpp == dbpp && mode == 0;
}

/* Combine source pixel s into destination pixel d. */
static inline uint32_t nqd_blit_mode(int mode, uint32_t s, uint32_t d)
{
	switch (mode) {
	case 0: return s;
	case 1: return d | s;
	case 2: return d ^ s;
	case 3: return d & ~s;
	case 4: return ~s;
	case 5: return d | ~s;
	case 6: return d ^ ~s;
	case 7: return d & s;
	case 34: return nqd_bytes(d, s, 0);
	case 37: return nqd_bytes(d, s, 2);
	case 38: return nqd_bytes(d, s, 1);
	case 39: return nqd_bytes(d, s, 3);
	}
	return s;
}

/* True when a transparent-mode source pixel must be left untouched. */
static inline bool nqd_transparent_skip(uint32_t s, uint32_t back)
{
	return ((s ^ back) & 0xffffff00u) == 0;
}

#endif
