/*
 *  gfxaccel.cpp - Generic Native QuickDraw acceleration
 *
 *  SheepShaver (C) 1997-2008 Marc Hellwig and Christian Bauer
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

#include "sysdeps.h"
#include <time.h>

#include "prefs.h"
#include "video.h"
#include "video_defs.h"
#include "nw_io.h"
#include "main.h"
#include "cpu_emulation.h"
#include "emul_op.h"
#include "macos_util.h"
#include "sheepforce.h"
#include "thunks.h"
#include "nqd_blit_ops.h"
#include "nqd_region.h"
#include "nqd_fillmask.h"
#include <chrono>
#include <memory>

#define DEBUG 0
#include "debug.h"


/*
 *	Utility functions
 */

// Return bytes per pixel for requested depth
static inline int bytes_per_pixel(int depth)
{
	switch (depth) {
	case 8:
		return 1;
	case 15: case 16:
		return 2;
	case 24: case 32:
		return 4;
	default:
		return 0;	/* 1/2/4-bit packed; not a byte pixel */
	}
}

static void shadow_flush(void);
static inline void nqd_shadow_flush_hook(void) { shadow_flush(); }


/*
 *  Clip safety. The parameter block holds QuickDraw regions (clip, visible, mask) at
 *  offsets that depend on the caller, so instead of picking one we require that EVERY
 *  region reachable from the block (pointer, or handle to a region) that overlaps the
 *  operation's rectangle covers all of it. If any of them could clip a pixel, the
 *  operation stays with the ROM. False positives while scanning only cost acceleration.
 */
static inline bool nqd_guest_ptr_ok(uint32 a)
{
	return nqd_fm_ptr_ok(a);
}

/* Does the region at 'rgn' contain every pixel of [rl,rr) x [rt,rb)? */
static bool nqd_region_covers(uint32 rgn, int rt, int rl, int rb, int rr)
{
	const unsigned size = ReadMacInt16(rgn);
	const int t = (int16)ReadMacInt16(rgn + 2), l = (int16)ReadMacInt16(rgn + 4);
	const int b = (int16)ReadMacInt16(rgn + 6), r = (int16)ReadMacInt16(rgn + 8);
	if (t > rt || l > rl || b < rb || r < rr)
		return false;
	if (size <= 10)
		return true;			// plain rectangle region, covered by its bbox
	if (size > 8192)
		return false;
	int pts[64], np = 0;
	uint32 a = rgn + 10;
	const uint32 end = rgn + size;
	int cur_y = t;
	for (;;) {
		if (a + 2 > end)
			return false;
		int y = (int16)ReadMacInt16(a);
		a += 2;
		// Rows [cur_y, y) use the current inversion points.
		if (y > rt && cur_y < rb) {
			int from = cur_y > rt ? cur_y : rt;
			int to = y < rb ? y : rb;
			if (to > from) {
				bool cov = false;
				for (int i = 0; i + 1 < np; i += 2)
					if (pts[i] <= rl && pts[i + 1] >= rr)
						cov = true;
				if (!cov)
					return false;
			}
		}
		if (y == 0x7fff || y >= rb)
			return true;
		int line[64], nl = 0;
		for (;;) {
			if (a + 2 > end)
				return false;
			int x = (int16)ReadMacInt16(a);
			a += 2;
			if (x == 0x7fff)
				break;
			if (nl < 64)
				line[nl++] = x;
		}
		int merged[128], nm = 0, i = 0, j = 0;
		while (i < np || j < nl) {
			if (j >= nl || (i < np && pts[i] < line[j]))
				merged[nm++] = pts[i++];
			else if (i >= np || line[j] < pts[i])
				merged[nm++] = line[j++];
			else {
				i++;
				j++;
			}
		}
		np = nm > 64 ? 64 : nm;
		memcpy(pts, merged, (size_t)np * sizeof(int));
		cur_y = y;
	}
}

/* True when at least one region is reachable and none of the reachable regions can clip the destination rectangle. */
static uint16 g_gate_cap[2][40];	// probe: raw words of the first two regions the gate examined
static int g_gate_cap_n;

enum { GATE_DIRECT = 1, GATE_DEEP_HANDLE = 2, GATE_DEEP_DIRECT = 4 };
static bool nqd_clip_gate(uint32 p, int *nregions, int mode = 0)
{
	g_gate_cap_n = 0;
	const int rt = (int16)ReadMacInt16(p + acclDestRect + 0), rl = (int16)ReadMacInt16(p + acclDestRect + 2);
	const int rb = (int16)ReadMacInt16(p + acclDestRect + 4), rr = (int16)ReadMacInt16(p + acclDestRect + 6);
	int seen = 0;
	bool ok = true;
	/* A region may be reached directly (pointer), through a handle, and, one step further, from a
	 * structure the block points at (a port holds its clip and visible regions). */
	auto examine = [&](uint32 base) {
		if (!ok || !nqd_guest_ptr_ok(base))
			return;
		unsigned sz = ReadMacInt16(base);
		int t = (int16)ReadMacInt16(base + 2), l = (int16)ReadMacInt16(base + 4);
		int b = (int16)ReadMacInt16(base + 6), r = (int16)ReadMacInt16(base + 8);
		if (sz < 10 || sz > 8192 || b <= t || r <= l)
			return;				// not a region
		if (b <= rt || t >= rb || r <= rl || l >= rr)
			return;				// does not touch the rectangle
		seen++;
		if (g_gate_cap_n < 2) {
			for (int k = 0; k < 40 && k * 2 < (int)sz; k++)
				g_gate_cap[g_gate_cap_n][k] = (uint16)ReadMacInt16(base + k * 2);
			g_gate_cap_n++;
		}
		if (!nqd_region_covers(base, rt, rl, rb, rr))
			ok = false;
	};
	for (uint32 off = 0; off < 0x500 && ok; off += 4) {
		uint32 w = ReadMacInt32(p + off);
		if (!nqd_guest_ptr_ok(w))
			continue;
		if (mode & GATE_DIRECT)
			examine(w);			// a region pointer
		examine(ReadMacInt32(w));		// a handle
		if (mode & (GATE_DEEP_HANDLE | GATE_DEEP_DIRECT))
			for (uint32 k = 0; k < 0x80 && ok; k += 4) {	// one level deeper
				uint32 v = ReadMacInt32(w + k);
				if (!nqd_guest_ptr_ok(v))
					continue;
				if (mode & GATE_DEEP_DIRECT)
					examine(v);
				if (mode & GATE_DEEP_HANDLE)
					examine(ReadMacInt32(v));
			}
	}
	if (nregions)
		*nregions = seen;
	/* Seeing no region at all proves nothing: the probe found lines clipped to a rectangle
	 * the block does not reference (6 of 691 gate passes). Require a region we could check. */
	return ok && seen > 0;
}

// Pass-through dirty areas to redraw functions
static inline void NQD_set_dirty_area(uint32 p)
{
	int16 x = (int16)ReadMacInt16(p + acclDestRect + 2) - (int16)ReadMacInt16(p + acclDestBoundsRect + 2);
	int16 y = (int16)ReadMacInt16(p + acclDestRect + 0) - (int16)ReadMacInt16(p + acclDestBoundsRect + 0);
	int16 w  = (int16)ReadMacInt16(p + acclDestRect + 6) - (int16)ReadMacInt16(p + acclDestRect + 2);
	int16 h = (int16)ReadMacInt16(p + acclDestRect + 4) - (int16)ReadMacInt16(p + acclDestRect + 0);
	const uint32 dest = ReadMacInt32(p + acclDestBaseAddr);
	nw_fb_damage_pixmap(dest, x, y, w, h);
	if (dest == screen_base)
		video_set_dirty_area(x, y, w, h);
}

static void nqd_mark_written(uint8 *dest, int width_px, int height, int bpp)
{
	if (!screen_base || !dest || width_px <= 0 || height <= 0 || bpp <= 0)
		return;
	uint8 *fb = Mac2HostAddr(screen_base);
	if (!fb)
		return;
	const uint32 rowbytes = VModes[cur_mode].viRowBytes;
	const uint32 fb_bytes = rowbytes * VModes[cur_mode].viYsize;
	if (dest < fb || dest >= fb + fb_bytes)
		return;
	const uint32 off = (uint32)(dest - fb);
	const int x = (int)((off % rowbytes) / (uint32)bpp);
	const int y = (int)(off / rowbytes);
	nw_fb_damage_rect(x, y, width_px, height);
}


/*
 *	Rectangle inversion
 */

template< int bpp >
static inline void do_invrect(uint8 *dest, uint32 length)
{
#define INVERT_1(PTR, OFS) ((uint8  *)(PTR))[OFS] = ~((uint8  *)(PTR))[OFS]
#define INVERT_2(PTR, OFS) ((uint16 *)(PTR))[OFS] = ~((uint16 *)(PTR))[OFS]
#define INVERT_4(PTR, OFS) ((uint32 *)(PTR))[OFS] = ~((uint32 *)(PTR))[OFS]
#define INVERT_8(PTR, OFS) ((uint64 *)(PTR))[OFS] = ~((uint64 *)(PTR))[OFS]

#ifndef UNALIGNED_PROFITABLE
	// Align on 16-bit boundaries
	if (bpp < 16 && (((uintptr)dest) & 1)) {
		INVERT_1(dest, 0);
		dest += 1; length -= 1;
	}

	// Align on 32-bit boundaries
	if (bpp < 32 && (((uintptr)dest) & 2) && length >= 2) {
		INVERT_2(dest, 0);
		dest += 2; length -= 2;
	}
#endif

	// Invert 8-byte words
	if (length >= 8) {
		const int r = (length / 8) % 8;
		dest += r * 8;

		int n = ((length / 8) + 7) / 8;
		switch (r) {
		case 0: do {
				dest += 64;
				INVERT_8(dest, -8);
		case 7: INVERT_8(dest, -7);
		case 6: INVERT_8(dest, -6);
		case 5: INVERT_8(dest, -5);
		case 4: INVERT_8(dest, -4);
		case 3: INVERT_8(dest, -3);
		case 2: INVERT_8(dest, -2);
		case 1: INVERT_8(dest, -1);
				} while (--n > 0);
		}
	}

	// 32-bit cell to invert?
	if (length & 4) {
		INVERT_4(dest, 0);
		if (bpp <= 16)
			dest += 4;
	}

	// 16-bit cell to invert?
	if (bpp <= 16 && (length & 2)) {
		INVERT_2(dest, 0);
		if (bpp <= 8)
			dest += 2;
	}

	// 8-bit cell to invert?
	if (bpp <= 8 && (length & 1))
		INVERT_1(dest, 0);

#undef INVERT_1
#undef INVERT_2
#undef INVERT_4
#undef INVERT_8
}

void NQD_invrect(uint32 p)
{
	D(bug("accl_invrect %08x\n", p));

	// Get inversion parameters
	int16 dest_X = (int16)ReadMacInt16(p + acclDestRect + 2) - (int16)ReadMacInt16(p + acclDestBoundsRect + 2);
	int16 dest_Y = (int16)ReadMacInt16(p + acclDestRect + 0) - (int16)ReadMacInt16(p + acclDestBoundsRect + 0);
	int16 width  = (int16)ReadMacInt16(p + acclDestRect + 6) - (int16)ReadMacInt16(p + acclDestRect + 2);
	int16 height = (int16)ReadMacInt16(p + acclDestRect + 4) - (int16)ReadMacInt16(p + acclDestRect + 0);
	D(bug(" dest X %d, dest Y %d\n", dest_X, dest_Y));
	D(bug(" width %d, height %d, bytes_per_row %d\n", width, height, (int32)ReadMacInt32(p + acclDestRowBytes)));

	//!!?? pen_mode == 14

	// And perform the inversion
	const int bpp = bytes_per_pixel(ReadMacInt32(p + acclDestPixelSize));
	const int dest_row_bytes = (int32)ReadMacInt32(p + acclDestRowBytes);
	uint8 *dest = Mac2HostAddr(ReadMacInt32(p + acclDestBaseAddr) + (dest_Y * dest_row_bytes) + (dest_X * bpp));
	nqd_mark_written(dest, width, height, bpp);
	if (SheepForceQDEnabled() && SheepForceOwns(ReadMacInt32(p + acclDestBaseAddr)) &&
	    bpp >= 1 &&
	    SheepForceTryInvert(dest, bpp, dest_row_bytes, width * bpp, height))
		return;
	if (SheepForceEnabled())
		SheepForceFlushCPU(dest, dest_row_bytes, width * bpp, height);
	width *= bpp;
	switch (bpp) {
	case 1:
		for (int i = 0; i < height; i++) {
			do_invrect<8>(dest, width);
			dest += dest_row_bytes;
		}
		break;
	case 2:
		for (int i = 0; i < height; i++) {
			do_invrect<16>(dest, width);
			dest += dest_row_bytes;
		}
		break;
	case 4:
		for (int i = 0; i < height; i++) {
			do_invrect<32>(dest, width);
			dest += dest_row_bytes;
		}
		break;
	}
}


/*
 *	Rectangle filling
 */

template< int bpp >
static inline void do_fillrect(uint8 *dest, uint32 color, uint32 length)
{
#define FILL_1(PTR, OFS, VAL) ((uint8  *)(PTR))[OFS] = (VAL)
#define FILL_2(PTR, OFS, VAL) ((uint16 *)(PTR))[OFS] = (VAL)
#define FILL_4(PTR, OFS, VAL) ((uint32 *)(PTR))[OFS] = (VAL)
#define FILL_8(PTR, OFS, VAL) ((uint64 *)(PTR))[OFS] = (VAL)

#ifndef UNALIGNED_PROFITABLE
	// Align on 16-bit boundaries
	if (bpp < 16 && (((uintptr)dest) & 1)) {
		FILL_1(dest, 0, color);
		dest += 1; length -= 1;
	}

	// Align on 32-bit boundaries
	if (bpp < 32 && (((uintptr)dest) & 2) && length >= 2) {
		FILL_2(dest, 0, color);
		dest += 2; length -= 2;
	}
#endif

	// Fill 8-byte words
	if (length >= 8) {
		const uint64 c = (((uint64)color) << 32) | color;
		const int r = (length / 8) % 8;
		dest += r * 8;

		int n = ((length / 8) + 7) / 8;
		switch (r) {
		case 0: do {
				dest += 64;
				FILL_8(dest, -8, c);
		case 7: FILL_8(dest, -7, c);
		case 6: FILL_8(dest, -6, c);
		case 5: FILL_8(dest, -5, c);
		case 4: FILL_8(dest, -4, c);
		case 3: FILL_8(dest, -3, c);
		case 2: FILL_8(dest, -2, c);
		case 1: FILL_8(dest, -1, c);
				} while (--n > 0);
		}
	}

	// 32-bit cell to fill?
	if (length & 4) {
		FILL_4(dest, 0, color);
		if (bpp <= 16)
			dest += 4;
	}

	// 16-bit cell to fill?
	if (bpp <= 16 && (length & 2)) {
		FILL_2(dest, 0, color);
		if (bpp <= 8)
			dest += 2;
	}

	// 8-bit cell to fill?
	if (bpp <= 8 && (length & 1))
		FILL_1(dest, 0, color);

#undef FILL_1
#undef FILL_2
#undef FILL_4
#undef FILL_8
}

void NQD_fillrect(uint32 p)
{
	D(bug("accl_fillrect %08x\n", p));

	// Get filling parameters
	int16 dest_X = (int16)ReadMacInt16(p + acclDestRect + 2) - (int16)ReadMacInt16(p + acclDestBoundsRect + 2);
	int16 dest_Y = (int16)ReadMacInt16(p + acclDestRect + 0) - (int16)ReadMacInt16(p + acclDestBoundsRect + 0);
	int16 width  = (int16)ReadMacInt16(p + acclDestRect + 6) - (int16)ReadMacInt16(p + acclDestRect + 2);
	int16 height = (int16)ReadMacInt16(p + acclDestRect + 4) - (int16)ReadMacInt16(p + acclDestRect + 0);
	uint32 color = htonl(ReadMacInt32(p + acclPenMode) == 8 ? ReadMacInt32(p + acclForePen) : ReadMacInt32(p + acclBackPen));
	D(bug(" dest X %d, dest Y %d\n", dest_X, dest_Y));
	D(bug(" width %d, height %d\n", width, height));
	D(bug(" bytes_per_row %d color %08x\n", (int32)ReadMacInt32(p + acclDestRowBytes), color));

	// And perform the fill
	const int bpp = bytes_per_pixel(ReadMacInt32(p + acclDestPixelSize));
	const int dest_row_bytes = (int32)ReadMacInt32(p + acclDestRowBytes);
	uint8 *dest = Mac2HostAddr(ReadMacInt32(p + acclDestBaseAddr) + (dest_Y * dest_row_bytes) + (dest_X * bpp));
	nqd_mark_written(dest, width, height, bpp);
	if (SheepForceQDEnabled() && SheepForceOwns(ReadMacInt32(p + acclDestBaseAddr)) &&
	    bpp >= 1 &&
	    SheepForceTryFill(dest, bpp, dest_row_bytes, width * bpp, height, color))
		return;
	if (SheepForceEnabled())
		SheepForceFlushCPU(dest, dest_row_bytes, width * bpp, height);
	width *= bpp;
	switch (bpp) {
	case 1:
		for (int i = 0; i < height; i++) {
			memset(dest, color, width);
			dest += dest_row_bytes;
		}
		break;
	case 2:
		for (int i = 0; i < height; i++) {
			do_fillrect<16>(dest, color, width);
			dest += dest_row_bytes;
		}
		break;
	case 4:
		for (int i = 0; i < height; i++) {
			do_fillrect<32>(dest, color, width);
			dest += dest_row_bytes;
		}
		break;
	}
}

bool NQD_fillrect_hook(uint32 p)
{
	D(bug("accl_fillrect_hook %08x\n", p));
	nqd_shadow_flush_hook();
	NQD_set_dirty_area(p);

	// Check if we can accelerate this fillrect
	if (ReadMacInt32(p + 0x284) != 0 && ReadMacInt32(p + acclDestPixelSize) >= 8) {
		const int transfer_mode = ReadMacInt32(p + acclTransferMode);
		if (transfer_mode == 8) {
			// Fill
			WriteMacInt32(p + acclDrawProc, NativeTVECT(NATIVE_NQD_FILLRECT));
			return true;
		}
		else if (transfer_mode == 10) {
			// Invert
			WriteMacInt32(p + acclDrawProc, NativeTVECT(NATIVE_NQD_INVRECT));
			return true;
		}
	}
	return false;
}


/*
 *  "Lines" hook (code 5): the ROM draws frames, rules and bevels as solid pen-colour
 *  rectangles. Shadow verification against the ROM's own output (no mismatch in
 *  thousands of calls) showed the result is the pen colour wherever the clip lets it,
 *  so this takes over only when nothing can clip: a 32-bit framebuffer destination,
 *  pen mode patCopy, transfer mode 0, and no reachable region that could cut the rectangle.
 *  The ROM's fill draw proc then paints it.
 */
bool NQD_lines_hook(uint32 p)
{
	nqd_shadow_flush_hook();
	NQD_set_dirty_area(p);
	if (!SheepForceQDEnabled())
		return false;
	if (ReadMacInt32(p + acclPenMode) != 8 || ReadMacInt32(p + acclTransferMode) != 0)
		return false;
	if (ReadMacInt32(p + acclDestPixelSize) != 32)
		return false;
	if ((int32)ReadMacInt32(p + acclDestRowBytes) <= 0)
		return false;
	if (!SheepForceOwns(ReadMacInt32(p + acclDestBaseAddr)))
		return false;
	const int w = (int16)ReadMacInt16(p + acclDestRect + 6) - (int16)ReadMacInt16(p + acclDestRect + 2);
	const int h = (int16)ReadMacInt16(p + acclDestRect + 4) - (int16)ReadMacInt16(p + acclDestRect + 0);
	if (w <= 0 || h <= 0)
		return false;
	/* Direct region pointers matter: probe runs showed a rounded clip reached only that way. */
	if (!nqd_clip_gate(p, NULL, GATE_DIRECT))
		return false;
	WriteMacInt32(p + acclDrawProc, NativeTVECT(NATIVE_NQD_FILLRECT));
	return true;
}

/*
 *  Fill-with-mask (hook code 3, ACCL_FILLMASK). The acceptance rule lives in include/nqd_fillmask.h, shared
 *  with the probe and the harness; this file supplies the guest accessor, the executors and the counters.
 *  The plan built in the hook is the one the draw proc executes, so the draw cannot disagree with the
 *  decision and cannot lose the operation if the guest changes in between.
 */
static_assert(NQD_FM_TRANSFER == acclTransferMode && NQD_FM_PEN == acclPenMode && NQD_FM_FORE == acclForePen &&
	      NQD_FM_BACK == acclBackPen && NQD_FM_DEST_BASE == acclDestBaseAddr && NQD_FM_DEST_ROW == acclDestRowBytes &&
	      NQD_FM_DEST_BOUNDS == acclDestBoundsRect && NQD_FM_DEST_PIXEL == acclDestPixelSize &&
	      NQD_FM_DEST_RECT == acclDestRect, "nqd_fillmask.h block offsets must match accl_params");

struct NqdGuestMem {
	uint32_t r8(uint32_t a) { return ReadMacInt8(a); }
	uint32_t r16(uint32_t a) { return ReadMacInt16(a); }
	uint32_t r32(uint32_t a) { return ReadMacInt32(a); }
	bool owns(uint32_t a) { return SheepForceOwns(a); }
	uint32_t fb_mac() { return SheepForcePageMac(0); }
	uint64_t fb_bytes() { return (uint64_t)SheepForcePageBytes() * (uint64_t)SheepForcePageCount(); }
};

struct NqdFillStats {
	uint64_t hooks, accepted, empty_plans, draws, gpu_ok, gpu_declined, cpu_fallbacks, cpu_failed, lost;
	uint64_t rejected[NQD_FM_REJ_COUNT];
	uint64_t rejected_px[NQD_FM_REJ_COUNT];	// rectangle area of rejected operations
	uint64_t rejected_clip_px[NQD_FM_REJ_COUNT];	// pixels the clip leaves, i.e. what the ROM will actually draw
	uint64_t tiled_draws, tiled_px;
	uint32_t mode_key[12][3];	// refused (pen, transfer, pixel size) combinations
	uint64_t mode_n[12], mode_px[12];
	unsigned mode_nk;
	uint64_t span_px, rect_px, gpu_px, cpu_px;
	uint64_t ns_build, ns_submit, ns_wait, ns_cpu;
	uint64_t last_report_ns;
};
static NqdFillPolicy nqd_fm_policy(void)
{
	static int tiles = -1;
	if (tiles < 0)
		tiles = PrefsFindBool("sheepforce_fillmask_tiles") ? 1 : 0;
	NqdFillPolicy p = nqd_fm_production_policy();
	p.allow_tiled = tiles == 1;
	return p;
}
static NqdFillStats g_fm_stats;
static NqdFillmaskPlan g_fm_pending;
static bool g_fm_have;

static inline uint64_t nqd_fm_now_ns(void)
{
	return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void NQD_fillmask_report(void)
{
	const NqdFillStats &s = g_fm_stats;
	if (!s.hooks)
		return;
	printf("SheepForce: fill-mask: %llu hooks, %llu accepted (%llu empty), %llu drawn: %llu GPU, %llu declined by GPU, %llu CPU fallbacks, %llu CPU failures, %llu lost; "
	       "pixels: rect %llu, clipped %llu, GPU %llu, CPU %llu; time ms: build %.2f, GPU submit %.2f, wait %.2f, CPU draw %.2f; rejected:",
	       (unsigned long long)s.hooks, (unsigned long long)s.accepted, (unsigned long long)s.empty_plans, (unsigned long long)s.draws,
	       (unsigned long long)s.gpu_ok, (unsigned long long)s.gpu_declined, (unsigned long long)s.cpu_fallbacks,
	       (unsigned long long)s.cpu_failed, (unsigned long long)s.lost, (unsigned long long)s.rect_px, (unsigned long long)s.span_px,
	       (unsigned long long)s.gpu_px, (unsigned long long)s.cpu_px, s.ns_build / 1e6, s.ns_submit / 1e6, s.ns_wait / 1e6, s.ns_cpu / 1e6);
	for (int i = 1; i < NQD_FM_REJ_COUNT; i++)
		if (s.rejected[i])
			printf(" %s %llu (rect %llu kpx, clipped %llu kpx)", nqd_fm_reject_name(i), (unsigned long long)s.rejected[i], (unsigned long long)s.rejected_px[i] / 1000, (unsigned long long)s.rejected_clip_px[i] / 1000);
	printf("; tiled draws %llu (%llu kpx)", (unsigned long long)s.tiled_draws, (unsigned long long)s.tiled_px / 1000);
	if (s.mode_nk) {
		printf("; mode-rejected (pen,transfer,pixsize) n/kpx:");
		for (unsigned k = 0; k < s.mode_nk; k++)
			printf(" (%u,%u,%u) %llu/%llu", s.mode_key[k][0], s.mode_key[k][1], s.mode_key[k][2], (unsigned long long)s.mode_n[k], (unsigned long long)s.mode_px[k] / 1000);
	}
	printf("\n");
	fflush(stdout);
}

static void nqd_fm_maybe_report(uint64_t now)
{
	if (now - g_fm_stats.last_report_ns >= 2000000000ull) {
		g_fm_stats.last_report_ns = now;
		NQD_fillmask_report();
	}
}

bool NQD_fillmask_hook(uint32 p)
{
	if (!SheepForceQDEnabled())
		return false;
	NqdFillStats &s = g_fm_stats;
	const uint64_t t0 = nqd_fm_now_ns();
	s.hooks++;
	NqdGuestMem g;
	NqdFillmaskPlan pl;
	const int r = nqd_fillmask_build(g, p, &pl, nqd_fm_policy());
	const uint64_t t1 = nqd_fm_now_ns();
	s.ns_build += t1 - t0;
	if (r != NQD_FM_OK) {
		s.rejected[r]++;
		{
			const int w = (int16)ReadMacInt16(p + acclDestRect + 6) - (int16)ReadMacInt16(p + acclDestRect + 2);
			const int h = (int16)ReadMacInt16(p + acclDestRect + 4) - (int16)ReadMacInt16(p + acclDestRect + 0);
			if (w > 0 && h > 0)
				s.rejected_px[r] += (uint64_t)w * (uint64_t)h;
			if (r == NQD_FM_REJ_MODE || r == NQD_FM_REJ_PATTERN || r == NQD_FM_REJ_NOT_SOLID || r == NQD_FM_REJ_BLACK || r == NQD_FM_REJ_SMALL) {
				// What will the ROM really draw? Rebuild the clip alone (no mode/pattern checks) and count its pixels.
				NqdFillPolicy sv = nqd_fm_production_policy();
				sv.survey = true;
				NqdFillmaskPlan sp;
				if (nqd_fillmask_build(g, p, &sp, sv) == NQD_FM_OK)
					s.rejected_clip_px[r] += sp.spans.pixels();
			}
			if (r == NQD_FM_REJ_MODE) {
				const uint32_t key[3] = { ReadMacInt32(p + acclPenMode), ReadMacInt32(p + acclTransferMode), ReadMacInt32(p + acclDestPixelSize) };
				unsigned k = 0;
				while (k < s.mode_nk && memcmp(s.mode_key[k], key, sizeof key) != 0)
					k++;
				if (k == s.mode_nk && s.mode_nk < 12) {
					memcpy(s.mode_key[s.mode_nk], key, sizeof key);
					s.mode_nk++;
				}
				if (k < s.mode_nk && w > 0 && h > 0) {
					s.mode_n[k]++;
					s.mode_px[k] += (uint64_t)w * (uint64_t)h;
				}
			}
		}
		nqd_fm_maybe_report(t1);
		return false;
	}
	s.accepted++;
	g_fm_pending = pl;
	g_fm_have = true;
	WriteMacInt32(p + acclDrawProc, NativeTVECT(NATIVE_NQD_FILLMASK));
	nqd_fm_maybe_report(t1);
	return true;
}

void NQD_fillmask(uint32 p)
{
	NqdFillStats &s = g_fm_stats;
	NqdFillmaskPlan local;
	NqdFillmaskPlan *pl = &g_fm_pending;
	if (!(g_fm_have && g_fm_pending.block == p)) {
		NqdGuestMem g;
		const int r = nqd_fillmask_build(g, p, &local, nqd_fm_policy());
		if (r != NQD_FM_OK) {
			// The hook accepted this block and the ROM will not draw it, so this operation is lost; make that loud.
			s.lost++;
			if (s.lost <= 5)
				printf("SheepForce: fill-mask draw for block %08x has no plan (%s); the operation is lost\n", (unsigned)p, nqd_fm_reject_name(r));
			return;
		}
		pl = &local;
	}
	g_fm_have = false;
	s.draws++;
	s.rect_px += (uint64_t)pl->width * (uint64_t)pl->height;
	const uint64_t px = pl->spans.pixels();
	s.span_px += px;
	NqdGuestMem g;
	uint8 *fb = Mac2HostAddr(g.fb_mac());
	uint8 *dest = fb + pl->start;
	nqd_mark_written(dest, pl->width, pl->height, 4);
	if (pl->spans.runs.empty()) {
		s.empty_plans++;
		return;				// the clip leaves nothing to draw
	}
	const uint64_t t0 = nqd_fm_now_ns();
	bool submitted = false;
	if (SheepForceQDEnabled()) {
		if (pl->tiled) {
			SheepForceTileFill top;
			top.dest = dest;
			top.rowbytes = pl->row_bytes;
			top.width = pl->width;
			top.height = pl->height;
			top.tile = pl->tile.data();
			top.tile_w = pl->tile_w;
			top.tile_h = pl->tile_h;
			top.tile_ox = pl->dest_x;
			top.tile_oy = pl->dest_y;
			top.row_start = pl->spans.row_start.data();
			top.runs = pl->spans.runs.data();
			submitted = SheepForceTryFillTile(&top);
		} else {
			SheepForceSpanFill op;
			op.dest = dest;
			op.rowbytes = pl->row_bytes;
			op.width = pl->width;
			op.height = pl->height;
			op.fore = pl->colour;
			op.back = pl->colour;
			memset(op.pat, 0xff, sizeof op.pat);
			op.pat_ox = op.pat_oy = 0;
			op.row_start = pl->spans.row_start.data();
			op.runs = pl->spans.runs.data();
			submitted = SheepForceTryFillSpans(&op);
		}
	}
	if (submitted) {
		if (pl->tiled) {
			s.tiled_draws++;
			s.tiled_px += px;
		}
		const uint64_t t1 = nqd_fm_now_ns();
		s.gpu_ok++;
		s.gpu_px += px;
		s.ns_submit += t1 - t0;
		nqd_fm_maybe_report(t1);
		return;
	}
	s.gpu_declined++;
	s.cpu_fallbacks++;
	const uint64_t t1 = nqd_fm_now_ns();
	if (SheepForceEnabled())
		SheepForceFlushCPU(dest, pl->row_bytes, pl->width * 4, pl->height);
	const uint64_t t2 = nqd_fm_now_ns();
	s.ns_wait += t2 - t1;
	if (nqd_fillmask_cpu(*pl, fb, g.fb_bytes())) {
		s.cpu_px += px;
	} else {
		s.cpu_failed++;
		if (s.cpu_failed <= 5)
			printf("SheepForce: fill-mask CPU draw refused its plan (block %08x); the operation is lost\n", (unsigned)p);
	}
	const uint64_t t3 = nqd_fm_now_ns();
	s.ns_cpu += t3 - t2;
	nqd_fm_maybe_report(t3);
}

/*
 *	Isomorphic rectangle blitting
 */

static int nqd_scale_onto_screen(uint32 p, int *sw, int *sh, int *dw, int *dh,
				 int *sx, int *sy, int *dx, int *dy, int *src_stride)
{
	if (ReadMacInt32(p + acclTransferMode) != 0 &&
	    ReadMacInt32(p + acclTransferMode) != 64)
		return 0;
	if (ReadMacInt32(p + acclSrcPixelSize) != 32 || ReadMacInt32(p + acclDestPixelSize) != 32)
		return 0;
	*sw = (int)(int16)ReadMacInt16(p + acclSrcRect + 6) - (int)(int16)ReadMacInt16(p + acclSrcRect + 2);
	*sh = (int)(int16)ReadMacInt16(p + acclSrcRect + 4) - (int)(int16)ReadMacInt16(p + acclSrcRect + 0);
	*dw = (int)(int16)ReadMacInt16(p + acclDestRect + 6) - (int)(int16)ReadMacInt16(p + acclDestRect + 2);
	*dh = (int)(int16)ReadMacInt16(p + acclDestRect + 4) - (int)(int16)ReadMacInt16(p + acclDestRect + 0);
	if (*sw < 16 || *sh < 16 || *dw < *sw * 2 || *dh < *sh * 2)
		return 0;
	const uint32 dest = ReadMacInt32(p + acclDestBaseAddr);
	if (!screen_base || !dest)
		return 0;
	uint8 *fb = Mac2HostAddr(screen_base);
	uint8 *dp = Mac2HostAddr(dest);
	if (!fb || !dp || dp < fb)
		return 0;
	const uint32 row = VModes[cur_mode].viRowBytes;
	const uint32 fb_bytes = row * VModes[cur_mode].viYsize;
	if ((uint32)(dp - fb) >= fb_bytes || row == 0)
		return 0;
	const int ox = (int)(((dp - fb) % row) / 4);
	const int oy = (int)((dp - fb) / row);
	*sx = (int)(int16)ReadMacInt16(p + acclSrcRect + 2) - (int)(int16)ReadMacInt16(p + acclSrcBoundsRect + 2);
	*sy = (int)(int16)ReadMacInt16(p + acclSrcRect + 0) - (int)(int16)ReadMacInt16(p + acclSrcBoundsRect + 0);
	*dx = ox + (int)(int16)ReadMacInt16(p + acclDestRect + 2) - (int)(int16)ReadMacInt16(p + acclDestBoundsRect + 2);
	*dy = oy + (int)(int16)ReadMacInt16(p + acclDestRect + 0) - (int)(int16)ReadMacInt16(p + acclDestBoundsRect + 0);
	*src_stride = (int32)ReadMacInt32(p + acclSrcRowBytes);
	if (*src_stride < 0)
		*src_stride = -*src_stride;
	if (*src_stride < *sw * 4)
		return 0;
	return 1;
}

static int nqd_host_scale(uint32 p)
{
	int sw, sh, dw, dh, sx, sy, dx, dy, src_stride;
	if (!nqd_scale_onto_screen(p, &sw, &sh, &dw, &dh, &sx, &sy, &dx, &dy, &src_stride))
		return 0;
	const int src_row_signed = (int32)ReadMacInt32(p + acclSrcRowBytes);
	const int down = src_row_signed < 0;
	uint8 *src = Mac2HostAddr(ReadMacInt32(p + acclSrcBaseAddr) +
		(uint32)((down ? sy + sh - 1 : sy) * src_stride + sx * 4));
	if (!src)
		return 0;
	if (!nw_movie_scale_put(src, down ? -src_stride : src_stride, sw, sh, dx, dy, dw, dh))
		return 0;
	const int dst_row_signed = (int32)ReadMacInt32(p + acclDestRowBytes);
	const int dst_stride = dst_row_signed < 0 ? -dst_row_signed : dst_row_signed;
	uint8 *dst = Mac2HostAddr(ReadMacInt32(p + acclDestBaseAddr));
	if (dst && dst_stride >= dw * 4) {
		const int dest_x = (int)(int16)ReadMacInt16(p + acclDestRect + 2) - (int)(int16)ReadMacInt16(p + acclDestBoundsRect + 2);
		const int dest_y = (int)(int16)ReadMacInt16(p + acclDestRect + 0) - (int)(int16)ReadMacInt16(p + acclDestBoundsRect + 0);
		uint8 *dp = dst + (dest_y * dst_stride) + dest_x * 4;
		for (int y = 0; y < dh; y++) {
			const int sy = (int)((y * sh) / dh);
			const uint8 *srow = src + (down ? (sh - 1 - sy) : sy) * src_stride;
			uint8 *drow = dp + y * dst_stride;
			for (int x = 0; x < dw; x++) {
				const int sx = (int)((x * sw) / dw);
				const uint8 *sp = srow + sx * 4;
				drow[x * 4] = sp[0];
				drow[x * 4 + 1] = sp[1];
				drow[x * 4 + 2] = sp[2];
				drow[x * 4 + 3] = sp[3];
			}
		}
	}
	nw_fb_damage_rect(dx, dy, dw, dh);
	if (ReadMacInt32(p + acclDestBaseAddr) == screen_base || screen_base)
		video_set_dirty_area(dx, dy, dw, dh);
	return 1;
}

void NQD_bitblt(uint32 p)
{
	D(bug("accl_bitblt %08x\n", p));
	if (nqd_host_scale(p))
		return;

	// Get blitting parameters
	int16 src_X  = (int16)ReadMacInt16(p + acclSrcRect + 2) - (int16)ReadMacInt16(p + acclSrcBoundsRect + 2);
	int16 src_Y  = (int16)ReadMacInt16(p + acclSrcRect + 0) - (int16)ReadMacInt16(p + acclSrcBoundsRect + 0);
	int16 dest_X = (int16)ReadMacInt16(p + acclDestRect + 2) - (int16)ReadMacInt16(p + acclDestBoundsRect + 2);
	int16 dest_Y = (int16)ReadMacInt16(p + acclDestRect + 0) - (int16)ReadMacInt16(p + acclDestBoundsRect + 0);
	int16 width  = (int16)ReadMacInt16(p + acclDestRect + 6) - (int16)ReadMacInt16(p + acclDestRect + 2);
	int16 height = (int16)ReadMacInt16(p + acclDestRect + 4) - (int16)ReadMacInt16(p + acclDestRect + 0);
	D(bug(" src addr %08x, dest addr %08x\n", ReadMacInt32(p + acclSrcBaseAddr), ReadMacInt32(p + acclDestBaseAddr)));
	D(bug(" src X %d, src Y %d, dest X %d, dest Y %d\n", src_X, src_Y, dest_X, dest_Y));
	D(bug(" width %d, height %d\n", width, height));
	if (width <= 0 || height <= 0)
		return;

	const int src_bpp = bytes_per_pixel((int)ReadMacInt32(p + acclSrcPixelSize));
	const int dst_bpp = bytes_per_pixel((int)ReadMacInt32(p + acclDestPixelSize));
	if (src_bpp <= 0 || dst_bpp <= 0)
		return;
	const int src_row_signed = (int32)ReadMacInt32(p + acclSrcRowBytes);
	const int dst_row_signed = (int32)ReadMacInt32(p + acclDestRowBytes);
	const int src_row_bytes = src_row_signed < 0 ? -src_row_signed : src_row_signed;
	const int dst_row_bytes = dst_row_signed < 0 ? -dst_row_signed : dst_row_signed;
	const int down = src_row_signed < 0;
	uint8 *src = Mac2HostAddr(ReadMacInt32(p + acclSrcBaseAddr) +
		((down ? src_Y + height - 1 : src_Y) * src_row_bytes) + (src_X * src_bpp));
	uint8 *dst = Mac2HostAddr(ReadMacInt32(p + acclDestBaseAddr) +
		((down ? dest_Y + height - 1 : dest_Y) * dst_row_bytes) + (dest_X * dst_bpp));
	uint8 *dst0 = Mac2HostAddr(ReadMacInt32(p + acclDestBaseAddr) +
		(dest_Y * dst_row_bytes) + (dest_X * dst_bpp));
	nqd_mark_written(dst0, width, height, dst_bpp);
	const int mode = (int)ReadMacInt32(p + acclTransferMode);
	const uint32 back_pen = ReadMacInt32(p + acclBackPen);
	const uint32 back_word = nqd_pack_rgb((uint8)(back_pen >> 16), (uint8)(back_pen >> 8), (uint8)back_pen);
	uint32 pal[256];
	if (src_bpp == 1)
		for (int i = 0; i < 256; i++)
			pal[i] = nqd_pack_rgb(mac_pal[i].red, mac_pal[i].green, mac_pal[i].blue);
	if (!down && nqd_blit_mode_ok(mode, dst_bpp, src_bpp) && SheepForceQDEnabled() &&
	    SheepForceOwns(ReadMacInt32(p + acclDestBaseAddr))) {
		SheepForceBlitOp op;
		op.dest = dst;
		op.src = src;
		op.dbpp = dst_bpp;
		op.sbpp = src_bpp;
		op.dst_row = dst_row_bytes;
		op.src_row = src_row_bytes;
		op.width = width;
		op.height = height;
		op.mode = mode;
		op.back_word = back_word;
		op.pal = src_bpp == 1 ? pal : NULL;
		if (SheepForceTryBlit(&op))
			return;
	}
	if (SheepForceEnabled())
		SheepForceFlushCPU(dst0, dst_row_bytes, width * dst_bpp, height);
	const int sstep = down ? -src_row_bytes : src_row_bytes;
	const int dstep = down ? -dst_row_bytes : dst_row_bytes;
	if (src_bpp == dst_bpp && mode == 0) {
		const int span = width * src_bpp;
		for (int i = 0; i < height; i++) {
			memmove(dst, src, (size_t)span);
			src += sstep;
			dst += dstep;
		}
		return;
	}
	if (!nqd_blit_mode_ok(mode, dst_bpp, src_bpp))
		return;
	/* 32-bit destination, any implemented mode or depth pairing. If the source
	 * rows overlap the destination rows, read the source from a private copy. */
	const int swb = width * src_bpp;
	uint8 *srow0 = down ? src - (size_t)(height - 1) * (size_t)src_row_bytes : src;
	uint8 *drow0 = down ? dst - (size_t)(height - 1) * (size_t)dst_row_bytes : dst;
	const size_t sspan = (size_t)src_row_bytes * (size_t)(height - 1) + (size_t)swb;
	const size_t dspan = (size_t)dst_row_bytes * (size_t)(height - 1) + (size_t)width * 4;
	uint8 *tmp = NULL;
	if (srow0 < drow0 + dspan && drow0 < srow0 + sspan) {
		tmp = (uint8 *)malloc((size_t)swb * (size_t)height);
		if (!tmp)
			return;
		for (int i = 0; i < height; i++)
			memcpy(tmp + (size_t)i * (size_t)swb, srow0 + (size_t)i * (size_t)src_row_bytes, (size_t)swb);
	}
	for (int i = 0; i < height; i++) {
		const int row = down ? height - 1 - i : i;
		const uint8 *sp = tmp ? tmp + (size_t)row * (size_t)swb : srow0 + (size_t)row * (size_t)src_row_bytes;
		uint8 *dp = drow0 + (size_t)row * (size_t)dst_row_bytes;
		for (int x = 0; x < width; x++) {
			uint32 sw;
			if (src_bpp == 1)
				sw = pal[sp[x]];
			else if (src_bpp == 2)
				sw = nqd_expand555((uint16)((sp[x * 2] << 8) | sp[x * 2 + 1]));
			else
				sw = nqd_word_load(sp + x * 4);
			if (mode == 36 && nqd_transparent_skip(sw, back_word))
				continue;
			nqd_word_store(dp + x * 4, nqd_blit_mode(mode, sw, nqd_word_load(dp + x * 4)));
		}
	}
	free(tmp);
}

/*
  BitBlt transfer modes:
  0 : srcCopy
  1 : srcOr
  2 : srcXor
  3 : srcBic
  4 : notSrcCopy
  5 : notSrcOr
  6 : notSrcXor
  7 : notSrcBic
  32 : blend
  33 : addPin
  34 : addOver
  35 : subPin
  36 : transparent
  37 : adMax
  38 : subOver
  39 : adMin
  50 : hilite
*/

#if NW_BOOT_LOG
static void nqd_trace_bitblt(uint32 p, int native)
{
	static unsigned n;
	if (n >= 400u)
		return;
	n++;
	const uint32 dest = ReadMacInt32(p + acclDestBaseAddr);
	const uint32 src = ReadMacInt32(p + acclSrcBaseAddr);
	const int16 dest_X = (int16)ReadMacInt16(p + acclDestRect + 2) - (int16)ReadMacInt16(p + acclDestBoundsRect + 2);
	const int16 dest_Y = (int16)ReadMacInt16(p + acclDestRect + 0) - (int16)ReadMacInt16(p + acclDestBoundsRect + 0);
	const int16 width  = (int16)ReadMacInt16(p + acclDestRect + 6) - (int16)ReadMacInt16(p + acclDestRect + 2);
	const int16 height = (int16)ReadMacInt16(p + acclDestRect + 4) - (int16)ReadMacInt16(p + acclDestRect + 0);
	const int16 src_X  = (int16)ReadMacInt16(p + acclSrcRect + 2) - (int16)ReadMacInt16(p + acclSrcBoundsRect + 2);
	const int16 src_Y  = (int16)ReadMacInt16(p + acclSrcRect + 0) - (int16)ReadMacInt16(p + acclSrcBoundsRect + 0);
	const int16 db_t = (int16)ReadMacInt16(p + acclDestBoundsRect + 0);
	const int16 db_l = (int16)ReadMacInt16(p + acclDestBoundsRect + 2);
	const int16 db_b = (int16)ReadMacInt16(p + acclDestBoundsRect + 4);
	const int16 db_r = (int16)ReadMacInt16(p + acclDestBoundsRect + 6);
	const int16 sb_t = (int16)ReadMacInt16(p + acclSrcBoundsRect + 0);
	const int16 sb_l = (int16)ReadMacInt16(p + acclSrcBoundsRect + 2);
	const int16 sb_b = (int16)ReadMacInt16(p + acclSrcBoundsRect + 4);
	const int16 sb_r = (int16)ReadMacInt16(p + acclSrcBoundsRect + 6);
	printf("NW-BOOT G1: bitblt %s dest %08x xy %d,%d %dx%d drow %d dbounds %d,%d %dx%d src %08x xy %d,%d srow %d sbounds %d,%d %dx%d mode %d depth %d\n",
	       native ? "nq" : "cpu",
	       (unsigned)dest, dest_X, dest_Y, width, height,
	       (int)ReadMacInt32(p + acclDestRowBytes),
	       db_l, db_t, db_r - db_l, db_b - db_t,
	       (unsigned)src, src_X, src_Y,
	       (int)ReadMacInt32(p + acclSrcRowBytes),
	       sb_l, sb_t, sb_r - sb_l, sb_b - sb_t,
	       (int)ReadMacInt32(p + acclTransferMode),
	       (int)ReadMacInt32(p + acclSrcPixelSize));
}
#endif

bool NQD_bitblt_hook(uint32 p)
{
	D(bug("accl_draw_hook %08x\n", p));
	nqd_shadow_flush_hook();
	NQD_set_dirty_area(p);

	{
		int sw, sh, dw, dh, sx, sy, dx, dy, src_stride;
		if (nqd_scale_onto_screen(p, &sw, &sh, &dw, &dh, &sx, &sy, &dx, &dy, &src_stride)) {
			WriteMacInt32(p + acclDrawProc, NativeTVECT(NATIVE_NQD_BITBLT));
			return true;
		}
	}

	const uint32 src_ps = ReadMacInt32(p + acclSrcPixelSize);
	const uint32 dst_ps = ReadMacInt32(p + acclDestPixelSize);
	/* 8/16-bit Appearance chrome into a 32-bit FB, and same-depth
	 * 32-bit srcCopy when the blit misses CrsrRect (0x83C). A hit
	 * stays in the ROM so ShieldCursor still wraps it. */
	const int expand32 = dst_ps == 32 && (src_ps == 8 || src_ps == 15 || src_ps == 16);
	int hits_cursor = 0;
	if (dst_ps == 32 && src_ps == 32) {
		int bt = (int)(int16)ReadMacInt16(p + acclDestRect + 0);
		int bl = (int)(int16)ReadMacInt16(p + acclDestRect + 2);
		int bb = (int)(int16)ReadMacInt16(p + acclDestRect + 4);
		int br = (int)(int16)ReadMacInt16(p + acclDestRect + 6);
		int ct = (int)(int16)ReadMacInt16(0x83c);
		int cl = (int)(int16)ReadMacInt16(0x83e);
		int cb = (int)(int16)ReadMacInt16(0x840);
		int cr = (int)(int16)ReadMacInt16(0x842);
		if (cb > ct && cr > cl && bl < cr && cl < br && bt < cb && ct < bb)
			hits_cursor = 1;
	}
	const int same32 = dst_ps == 32 && src_ps == 32 && !hits_cursor;
	if (ReadMacInt32(p + 0x018) + ReadMacInt32(p + 0x128) == 0 &&
		ReadMacInt32(p + 0x130) == 0 &&
		(expand32 || same32) &&
		(int32)(ReadMacInt32(p + acclSrcRowBytes) ^ ReadMacInt32(p + acclDestRowBytes)) >= 0 &&
		nqd_blit_mode_ok((int)ReadMacInt32(p + acclTransferMode), 4, src_ps == 32 ? 4 : (src_ps == 8 ? 1 : 2)) &&
		(int32)ReadMacInt32(p + 0x15c) > 0) {

		// Yes, set function pointer
		WriteMacInt32(p + acclDrawProc, NativeTVECT(NATIVE_NQD_BITBLT));
#if NW_BOOT_LOG
		nqd_trace_bitblt(p, 1);
#endif
		return true;
	}
#if NW_BOOT_LOG
	nqd_trace_bitblt(p, 0);
#endif
	return false;
}

// Unknown hook
bool NQD_unknown_hook(uint32 arg)
{
	D(bug("accl_unknown_hook %08x\n", arg));
	{
		int sw, sh, dw, dh, sx, sy, dx, dy, src_stride;
		if (nqd_scale_onto_screen(arg, &sw, &sh, &dw, &dh, &sx, &sy, &dx, &dy, &src_stride)) {
			WriteMacInt32(arg + acclDrawProc, NativeTVECT(NATIVE_NQD_BITBLT));
			return true;
		}
	}
	NQD_set_dirty_area(arg);

	return false;
}

/*
 *  Hook-code probe (pref sheepforce_probe). The ROM calls the hook planted
 *  under a given code only for the operation that code stands for, so planting
 *  a logging hook under each candidate code shows which codes exist and what
 *  accl_params look like for them. Always declines, so the ROM keeps drawing.
 */
/*
 *  Shadow verification (pref sheepforce_probe). The hook declines, the ROM draws in
 *  software, and at the next hook call (any code, so the previous operation is
 *  finished) the result is compared with what a candidate decoder predicts from
 *  the parameter block. Only decoders that match the ROM are allowed to take an
 *  operation over; every other layout stays with the ROM.
 */
#define SHADOW_MAXW 1024
#define SHADOW_MAXH 768
struct nqd_shadow_op {
	bool valid;
	uint32 code, n;
	uint32 w[0x140];		// parameter block words (big-endian converted), 0x500 bytes
	uint8 *dest;			// top-left of the destination rectangle (host)
	int row, rw, rh, dbpp;		// destination row bytes, rectangle size, bytes per pixel
	int cw, ch;			// captured size (the rectangle, clamped)
	bool gate;			// nqd_clip_gate said no region can clip this rectangle
	int gate_regions;
	bool gate_v[4];			// gate variants (see GATE_*): 0 original, 1 +direct, 2 +deep handles, 3 +deep direct
	int cap_n;
	uint16 cap[2][40];		// the regions the gate examined, as read at gate time
	int mask_off;			// block offset of the first mask region candidate, or -1 for none
	int ncand;			// region candidates found in the block
	int over;			// further overlapping regions beyond the 8 kept
	std::shared_ptr<NqdFillmaskPlan> fm[4];	// code 3: plans built at hook entry under policies A (old rule), B (skip bad regions), C (production: clip offsets only), D (C plus pixel patterns)
	int fm_rej[4];			// each one's rejection reason, or -1 if not built
	int cand_off[16];
	bool cand_rect[16];
	bool has_pre;
	uint8 pre[SHADOW_MAXH * SHADOW_MAXW * 4];	// destination before the ROM drew
	uint8 mask[SHADOW_MAXH * SHADOW_MAXW];		// 1 = inside the first candidate region
	uint8 cmask[16][SHADOW_MAXH * SHADOW_MAXW];	// 1 = inside candidate k's region
};
static nqd_shadow_op shadow_ops[8];
static unsigned shadow_n;

static uint32 shadow_pen_word(uint32 pen)
{
	return nqd_pack_rgb((uint8)(pen >> 16), (uint8)(pen >> 8), (uint8)pen);
}

/* Prints a small rectangle as characters, one per distinct colour. */
static void shadow_grid(const nqd_shadow_op &q)
{
	uint32 seen[16];
	unsigned ns = 0;
	for (int y = 0; y < q.rh && y < 16; y++) {
		char line[80];
		int x;
		for (x = 0; x < q.rw && x < 40; x++) {
			uint32 v = nqd_word_load(q.dest + (size_t)y * q.row + (size_t)x * 4) >> 8;
			unsigned i = 0;
			while (i < ns && seen[i] != v)
				i++;
			if (i == ns && ns < 16)
				seen[ns++] = v;
			line[x] = i < 16 ? "0123456789abcdef"[i] : '?';
		}
		line[x] = 0;
		printf("SheepForce shadow: code %u call %u row %2d %s\n", q.code, q.n, y, line);
	}
	printf("SheepForce shadow: code %u call %u palette", q.code, q.n);
	for (unsigned i = 0; i < ns; i++)
		printf(" %x=%06x", i, (unsigned)seen[i]);
	printf("\n");
}

/*
 *  QuickDraw region: rgnSize, rgnBBox, then for each scan line its y followed by
 *  inversion x values and 0x7fff, ending with y = 0x7fff. A line's x values toggle
 *  the previous line's inversion points. Returns false if the data is malformed.
 *  mask[] is filled for the rows/columns of the rectangle (rt..rb, rl..rr) in region
 *  coordinates, clamped to SHADOW_MAXW x SHADOW_MAXH; its stride is SHADOW_MAXW.
 */
static bool region_decode(uint32 rgn, int rt, int rl, int rb, int rr, uint8 *mask, bool *rect_only)
{
	const unsigned size = ReadMacInt16(rgn);
	if (size < 10 || size > 8192)
		return false;
	*rect_only = size == 10;
	const int t = (int16)ReadMacInt16(rgn + 2), l = (int16)ReadMacInt16(rgn + 4);
	const int b = (int16)ReadMacInt16(rgn + 6), r = (int16)ReadMacInt16(rgn + 8);
	const int h = rb - rt > SHADOW_MAXH ? SHADOW_MAXH : rb - rt;
	const int w = rr - rl > SHADOW_MAXW ? SHADOW_MAXW : rr - rl;
	memset(mask, 0, (size_t)SHADOW_MAXW * SHADOW_MAXH);
	if (*rect_only) {
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++)
				mask[y * SHADOW_MAXW + x] = (rt + y >= t && rt + y < b && rl + x >= l && rl + x < r);
		return true;
	}
	int pts[64], np = 0;
	uint32 a = rgn + 10;
	const uint32 end = rgn + size;
	int cur_y = t;
	for (;;) {
		if (a + 2 > end)
			return false;
		int y = (int16)ReadMacInt16(a);
		a += 2;
		// Rows from the previous line up to (not including) y use the current points.
		for (; cur_y < y && cur_y < rb; cur_y++) {
			if (cur_y < rt)
				continue;
			for (int x = 0; x < w; x++) {
				int gx = rl + x, in = 0;
				for (int i = 0; i + 1 < np; i += 2)
					if (gx >= pts[i] && gx < pts[i + 1])
						in = 1;
				mask[(cur_y - rt) * SHADOW_MAXW + x] = (uint8)in;
			}
		}
		if (y == 0x7fff)
			return true;
		int line[64], nl = 0;
		for (;;) {
			if (a + 2 > end)
				return false;
			int x = (int16)ReadMacInt16(a);
			a += 2;
			if (x == 0x7fff)
				break;
			if (nl < 64)
				line[nl++] = x;
		}
		// Toggle: symmetric difference of the sorted inversion point sets.
		int merged[128], nm = 0, i = 0, j = 0;
		while (i < np || j < nl) {
			if (j >= nl || (i < np && pts[i] < line[j]))
				merged[nm++] = pts[i++];
			else if (i >= np || line[j] < pts[i])
				merged[nm++] = line[j++];
			else {
				i++;
				j++;
			}
		}
		np = nm > 64 ? 64 : nm;
		memcpy(pts, merged, (size_t)np * sizeof(int));
		cur_y = y;
	}
}

/* Collect every region reachable from the block (pointer-like word -> handle -> region)
 * whose bounding box overlaps the destination rectangle. Which field is the mask is
 * decided by the statistics in shadow_check, not guessed here. */
static int region_collect(nqd_shadow_op &q, uint32 p, int rt, int rl, int rb, int rr)
{
	q.ncand = 0;
	q.over = 0;
	for (uint32 off = 0; off < 0x500; off += 4) {
		uint32 w = ReadMacInt32(p + off);
		if (w < 0x1000 || w >= 0x1ff00000u)
			continue;
		uint32 base = ReadMacInt32(w);
		if (base < 0x1000 || base >= 0x1ff00000u)
			continue;
		unsigned sz = ReadMacInt16(base);
		int t = (int16)ReadMacInt16(base + 2), l = (int16)ReadMacInt16(base + 4);
		int b = (int16)ReadMacInt16(base + 6), r = (int16)ReadMacInt16(base + 8);
		if (sz < 10 || sz > 8192 || b <= t || r <= l)
			continue;
		if (b <= rt || t >= rb || r <= rl || l >= rr)
			continue;	// does not overlap the rectangle
		if (q.ncand >= 16) {
			q.over++;
			continue;
		}
		bool rect_only = false;
		if (!region_decode(base, rt, rl, rb, rr, q.cmask[q.ncand], &rect_only))
			continue;
		q.cand_off[q.ncand] = (int)off;
		q.cand_rect[q.ncand] = rect_only;
		q.ncand++;
	}
	return q.ncand;
}

/* Discovery aid: find where the pixels the ROM just drew came from. Takes four
 * consecutive changed pixels of the first changed row and searches the memory
 * behind every pointer-like word (and its handle target) for the same bytes. */
static void locate_pixels(const nqd_shadow_op &q)
{
	int fx = -1, fy = -1;
	for (int y = 0; y < q.ch && fy < 0; y++)
		for (int x = 0; x + 4 <= q.cw; x++) {
			bool all = true;
			for (int k = 0; k < 4; k++) {
				uint32 a = nqd_word_load(q.dest + (size_t)y * q.row + (size_t)(x + k) * 4);
				uint32 b = nqd_word_load(q.pre + ((size_t)y * SHADOW_MAXW + x + k) * 4);
				if (!((a ^ b) & 0xffffff00u))
					all = false;
			}
			if (all && nqd_word_load(q.dest + (size_t)y * q.row + (size_t)x * 4) !=
			    nqd_word_load(q.dest + (size_t)y * q.row + (size_t)(x + 3) * 4)) {
				fx = x;
				fy = y;
				break;
			}
		}
	if (fy < 0)
		return;
	uint8 key[12];
	for (int k = 0; k < 3; k++) {
		uint32 a = nqd_word_load(q.dest + (size_t)fy * q.row + (size_t)(fx + k) * 4);
		key[k * 4] = (uint8)a; key[k * 4 + 1] = (uint8)(a >> 8); key[k * 4 + 2] = (uint8)(a >> 16); key[k * 4 + 3] = (uint8)(a >> 24);
	}
	unsigned hits = 0;
	for (uint32 off = 0; off < 0x500 && hits < 6; off += 4) {
		uint32 w = q.w[off / 4];
		if (w < 0x1000 || w >= 0x1ff00000u)
			continue;
		for (int level = 0; level < 2; level++) {
			uint32 base = w;
			if (level == 1) {
				base = ReadMacInt32(w);
				if (base < 0x1000 || base >= 0x1ff00000u)
					continue;
			}
			uint8 *h = Mac2HostAddr(base);
			for (uint32 d = 0; d < 0x8000 && hits < 6; d += 4)
				if (memcmp(h + d, key, sizeof key) == 0) {
					hits++;
					printf("SheepForce locate: code %u call %u pixels (first changed %d,%d) found at word@%03x=%08x level %d +%u\n",
					       q.code, q.n, fx, fy, (unsigned)off, (unsigned)w, level, (unsigned)d);
				}
		}
	}
	if (!hits)
		printf("SheepForce locate: code %u call %u pixels (first changed %d,%d) not found behind any pointer\n", q.code, q.n, fx, fy);
	if (q.code == 3 || q.code == 6) {
		// Patterns hang off several handle levels (PixPat -> patXData -> pixels): follow
		// pointers up to four steps from the block and report the path to the pixels.
		struct node { uint32 addr; int parent; uint32 via; };
		static node nodes[4000];
		int n = 0, found = 0;
		for (uint32 off = 0; off < 0x500; off += 4) {
			uint32 w = q.w[off / 4];
			if (w >= 0x1000 && w < 0x1ff00000u && (w & 1) == 0 && n < 4000)
				nodes[n++] = { w, -1, off };
		}
		for (int i = 0; i < n && found < 4; i++) {
			const uint32 a = nodes[i].addr;
			const uint8 *h = Mac2HostAddr(a);
			{	// does a PixPat start here? type 1/2, patMap handle -> PixMap with a sane depth and bounds
				const unsigned ptype = ReadMacInt16(a);
				const uint32 hm = ReadMacInt32(a + 2);
				if ((ptype == 1 || ptype == 2) && hm >= 0x1000 && hm < 0x1ff00000u && (hm & 1) == 0) {
					const uint32 pm = ReadMacInt32(hm);
					if (pm >= 0x1000 && pm < 0x1ff00000u && (pm & 1) == 0) {
						const unsigned rb = ReadMacInt16(pm + 4), psz = ReadMacInt16(pm + 32);
						const int bt = (int16)ReadMacInt16(pm + 6), bl = (int16)ReadMacInt16(pm + 8);
						const int bb = (int16)ReadMacInt16(pm + 10), br = (int16)ReadMacInt16(pm + 12);
						if ((rb & 0x8000) && (psz == 1 || psz == 2 || psz == 4 || psz == 8 || psz == 16 || psz == 32) &&
						    bt == 0 && bl == 0 && bb > 0 && bb <= 256 && br > 0 && br <= 256) {
							printf("SheepForce locate: code %u call %u PIXPAT at %08x type %u map %08x pm %08x rowBytes %u pixelSize %u bounds %dx%d "
							       "patData %08x patXData %08x patXValid %d patXMap %08x pmTable %08x; path:",
							       q.code, q.n, (unsigned)a, ptype, (unsigned)hm, (unsigned)pm, rb & 0x3fff, psz, br, bb,
							       (unsigned)ReadMacInt32(a + 6), (unsigned)ReadMacInt32(a + 10), (int)(int16)ReadMacInt16(a + 14),
							       (unsigned)ReadMacInt32(a + 16), (unsigned)ReadMacInt32(pm + 42));
							int depth = 0;
							for (int k = i; k >= 0 && depth < 6; k = nodes[k].parent, depth++)
								printf(" [%x%s]", (unsigned)nodes[k].via, nodes[k].parent < 0 ? "@blk" : "");
							printf("\n");
							found++;
						}
					}
				}
			}
			for (uint32 d = 0; d < 0x6000 && found < 4; d += 4)
				if (memcmp(h + d, key, sizeof key) == 0) {
					found++;
					printf("SheepForce locate: code %u call %u DEEP pixels at %08x +%u, path:", q.code, q.n, (unsigned)a, (unsigned)d);
					int depth = 0;
					for (int k = i; k >= 0 && depth < 6; k = nodes[k].parent, depth++)
						printf(" [%x%s]", (unsigned)nodes[k].via, nodes[k].parent < 0 ? "@blk" : "");
					printf("\n");
				}
			int depth = 0;
			for (int k = i; nodes[k].parent >= 0; k = nodes[k].parent)
				depth++;
			if (depth >= 3)
				continue;
			for (uint32 off = 0; off < 0x80 && n < 4000; off += 2) {	// 68k structures: even offsets
				uint32 v = ReadMacInt32(a + off);
				if (v >= 0x1000 && v < 0x1ff00000u && (v & 1) == 0) {
					const int vi = n;
					nodes[n++] = { v, i, off };
					if (n < 4000) {
						uint32 v2 = ReadMacInt32(v);	// as a handle
						if (v2 >= 0x1000 && v2 < 0x1ff00000u && (v2 & 1) == 0)
							nodes[n++] = { v2, vi, 0x10000 };
					}
				}
			}
		}
		if (!found)
			printf("SheepForce locate: code %u call %u DEEP search (%d nodes) found nothing\n", q.code, q.n, n);
	}
}

/*
 *  Pattern fills (hook code 3). The pattern is a classic PixPat: type, patMap, patData,
 *  patXData (the pattern expanded to the device depth), patXValid (that depth), patXMap.
 *  The block reaches it through a pointer to a structure that holds a PixPat handle.
 */
struct nqd_pixpat {
	uint32 addr, xdata;		// PixPat address, expanded pixel data (host-visible guest address)
	int xw, xh, xrow;		// expanded tile size and row bytes
	int xvalid;			// depth the expansion is valid for
};

static bool nqd_pixpat_at(uint32 a, nqd_pixpat *pp)
{
	if (!nqd_guest_ptr_ok(a))
		return false;
	const unsigned ptype = ReadMacInt16(a);
	const uint32 hm = ReadMacInt32(a + 2);
	if ((ptype != 1 && ptype != 2) || !nqd_guest_ptr_ok(hm))
		return false;
	const uint32 pm = ReadMacInt32(hm);
	if (!nqd_guest_ptr_ok(pm))
		return false;
	const unsigned rb = ReadMacInt16(pm + 4), psz = ReadMacInt16(pm + 32);
	const int bt = (int16)ReadMacInt16(pm + 6), bl = (int16)ReadMacInt16(pm + 8);
	const int bb = (int16)ReadMacInt16(pm + 10), br = (int16)ReadMacInt16(pm + 12);
	if (!(rb & 0x8000) || bt != 0 || bl != 0 || bb <= 0 || bb > 256 || br <= 0 || br > 256 ||
	    (psz != 1 && psz != 2 && psz != 4 && psz != 8 && psz != 16 && psz != 32))
		return false;
	pp->addr = a;
	pp->xvalid = (int16)ReadMacInt16(a + 14);
	pp->xdata = 0;
	pp->xw = pp->xh = pp->xrow = 0;
	// patXData is a handle to the tile expanded to the device depth; its size follows patMap's bounds.
	const uint32 hx = ReadMacInt32(a + 10);
	if (nqd_guest_ptr_ok(hx)) {
		const uint32 xd = ReadMacInt32(hx);
		if (nqd_guest_ptr_ok(xd)) {
			pp->xdata = xd;
			pp->xw = br;
			pp->xh = bb;
			pp->xrow = br * 4;
		}
	}
	return true;
}

/* The block holds a pointer to a structure that holds a PixPat handle (seen at +0x14 and +0x3e). */
static bool nqd_pixpat_find(const uint32 *w, nqd_pixpat *pp, uint32 *via)
{
	for (uint32 off = 0; off < 0x500; off += 4) {
		const uint32 s = w[off / 4];
		if (!nqd_guest_ptr_ok(s))
			continue;
		for (uint32 k = 0; k < 0x80; k += 2) {
			const uint32 h = ReadMacInt32(s + k);
			if (!nqd_guest_ptr_ok(h))
				continue;
			if (nqd_pixpat_at(ReadMacInt32(h), pp)) {
				*via = off << 16 | k;
				return true;
			}
		}
	}
	return false;
}

/* What do code 3 operations paint? Tally by area: nothing changed, one colour (fore, back or another),
 * two colours (a 1-bit pattern), or many (a pixel pattern or picture). */
static void code3_stats(const nqd_shadow_op &q)
{
	struct cat { unsigned n; unsigned long long px; };
	static cat solid_fore, solid_back, solid_other, two, many, none, big_many_nopat;
	static unsigned total;
	uint32 colours[3] = { 0, 0, 0 };
	int nc = 0;
	bool more = false;
	unsigned changed = 0;
	for (int y = 0; y < q.ch; y++)
		for (int x = 0; x < q.cw; x++) {
			uint32 got = nqd_word_load(q.dest + (size_t)y * q.row + (size_t)x * 4) & 0xffffff00u;
			uint32 pre = nqd_word_load(q.pre + ((size_t)y * SHADOW_MAXW + x) * 4) & 0xffffff00u;
			if (got == pre)
				continue;
			changed++;
			int k = 0;
			while (k < nc && colours[k] != got)
				k++;
			if (k == nc) {
				if (nc < 3)
					colours[nc++] = got;
				else
					more = true;
			}
		}
	const unsigned long long area = (unsigned long long)q.rw * (unsigned long long)q.rh;
	const uint32 fore = shadow_pen_word(q.w[0x1c / 4]) & 0xffffff00u, back = shadow_pen_word(q.w[0x20 / 4]) & 0xffffff00u;
	cat *c;
	if (!changed)
		c = &none;
	else if (more || nc == 3)
		c = &many;
	else if (nc == 2)
		c = &two;
	else
		c = colours[0] == fore ? &solid_fore : colours[0] == back ? &solid_back : &solid_other;
	{
		// Dump the first operations of each class so the block field that marks a solid pattern can be found.
		static unsigned dumped[8];
		const int ci = c == &none ? 0 : c == &many ? 1 : c == &two ? 2 : c == &solid_fore ? 3 : c == &solid_back ? 4 : 5;
		if (dumped[ci]++ < 4 && area > 2000) {
			printf("SheepForce shadow: code 3 class %d dump call %u pen %u:", ci, q.n, (unsigned)q.w[0x10 / 4]);
			for (int i = 0; i < 0x60 / 4 + 8; i++)
				printf(" %03x=%08x", i * 4, (unsigned)q.w[i]);
			printf("\n");
			// Look for a colour GrafPort (portVersion 0xC000 at +6) one or two pointer steps from the block.
			for (uint32 off = 0; off < 0x500; off += 4) {
				const uint32 w = q.w[off / 4];
				if (!nqd_guest_ptr_ok(w))
					continue;
				for (int lvl = 0; lvl < 2; lvl++) {
					uint32 port = lvl ? ReadMacInt32(w) : w;
					if (!nqd_guest_ptr_ok(port) || (ReadMacInt16(port + 6) & 0xc000) != 0xc000)
						continue;
					const uint32 hv = ReadMacInt32(port + 24), hc = ReadMacInt32(port + 28);
					if (!nqd_guest_ptr_ok(hv) || !nqd_guest_ptr_ok(hc))
						continue;
					printf("    PORT via +%03x%s -> %08x pnMode %u:", (unsigned)off, lvl ? " (handle)" : "", (unsigned)port, ReadMacInt16(port + 56));
					static const struct { const char *n; uint32 o; } pats[] = { { "bk", 0x20 }, { "pn", 0x3a }, { "fill", 0x3e } };
					for (auto &pt : pats) {
						const uint32 hp = ReadMacInt32(port + pt.o);
						nqd_pixpat pp;
						if (nqd_guest_ptr_ok(hp) && nqd_pixpat_at(ReadMacInt32(hp), &pp))
							printf(" %s=PixPat@%08x type %u pat1 %08x%08x xvalid %d", pt.n, (unsigned)pp.addr, ReadMacInt16(pp.addr),
							       (unsigned)ReadMacInt32(pp.addr + 20), (unsigned)ReadMacInt32(pp.addr + 24), pp.xvalid);
						else if (nqd_guest_ptr_ok(hp) && nqd_guest_ptr_ok(ReadMacInt32(hp)))
							printf(" %s=h%08x type %u pat1 %08x%08x", pt.n, (unsigned)hp, ReadMacInt16(ReadMacInt32(hp)),
							       (unsigned)ReadMacInt32(ReadMacInt32(hp) + 20), (unsigned)ReadMacInt32(ReadMacInt32(hp) + 24));
						else
							printf(" %s=%08x", pt.n, (unsigned)hp);
					}
					printf("\n");
				}
			}
			for (uint32 off = 0; off < 0x100; off += 4) {
				const uint32 w = q.w[off / 4];
				if (nqd_guest_ptr_ok(w) && w != q.w[0x64 / 4] && w != q.w[0x30 / 4]) {
					printf("    @%03x -> %08x:", (unsigned)off, (unsigned)w);
					for (int k = 0; k < 8; k++)
						printf(" %08x", (unsigned)ReadMacInt32(w + k * 4));
					printf("\n");
				}
			}
		}
	}
	c->n++;
	c->px += area;
	if (c == &many && area > 20000)
		big_many_nopat.n++;
	if (q.n <= 3 || (q.n % 40) == 0 || (area > 100000 && c != &solid_fore && c != &solid_back && c->n <= 12))
		printf("SheepForce shadow: code 3 call %u classify: %s, %d colours, area %llu, pen %u fore %08x back %08x first %08x\n", q.n,
		       c == &none ? "unchanged" : c == &many ? "many" : c == &two ? "two" : c == &solid_fore ? "solid fore" :
		       c == &solid_back ? "solid back" : "solid other", nc, area, (unsigned)q.w[0x10 / 4],
		       (unsigned)fore, (unsigned)back, (unsigned)colours[0]);
	if ((++total % 40) == 0)
		printf("SheepForce shadow: code 3 classes (ops/kpixels after %u ops): solid-fore %u/%llu solid-back %u/%llu solid-other %u/%llu two-colour %u/%llu many %u/%llu unchanged %u/%llu\n",
		       total, solid_fore.n, solid_fore.px / 1000, solid_back.n, solid_back.px / 1000, solid_other.n, solid_other.px / 1000,
		       two.n, two.px / 1000, many.n, many.px / 1000, none.n, none.px / 1000);
}

/* Which of the port's three patterns (bk, pn, fill) explains a code 3 operation, and with what origin? */
struct nqd_pat {
	bool valid;
	int type;			// 0: 1-bit pattern, 1: expanded pixel pattern
	uint8 bits[8];
	nqd_pixpat pp;
};

static bool nqd_pat_load(uint32 port, uint32 off, nqd_pat *out)
{
	out->valid = false;
	const uint32 hp = ReadMacInt32(port + off);
	if (!nqd_guest_ptr_ok(hp))
		return false;
	const uint32 pat = ReadMacInt32(hp);
	if (!nqd_guest_ptr_ok(pat))
		return false;
	if (nqd_pixpat_at(pat, &out->pp)) {
		if (ReadMacInt16(pat) == 0)
			return false;
		if (!out->pp.xdata || out->pp.xvalid != 32)
			return false;
		out->type = 1;
		out->valid = true;
		return true;
	}
	if (ReadMacInt16(pat) == 0) {
		out->type = 0;
		for (int i = 0; i < 8; i++)
			out->bits[i] = ReadMacInt8(pat + 20 + i);
		out->valid = true;
		return true;
	}
	return false;
}

/*
 *  Scores the production decision against the ROM. The plan was built at hook entry by nqd_fillmask_build (the very
 *  function the hook runs); here the ROM's pixels are compared with that plan's exact prediction: the plan's
 *  colour inside its spans, the pre-image outside. Captures that do not cover the whole rectangle are
 *  inconclusive and say so. Every mismatch prints the evidence needed to reproduce it.
 */
static void code3_prod_verify(const nqd_shadow_op &q)
{
	static const char *names[4] = { "A old-rule", "B skip-bad-regions", "C production", "D production+pixel-patterns" };
	static unsigned rej_n[4][NQD_FM_REJ_COUNT];
	static unsigned long long rej_px[4][NQD_FM_REJ_COUNT];
	static unsigned ex_n[4], bad_n[4], inc_n[4], shown[4];
	static unsigned long long ex_px[4], bad_px[4], inc_px[4];
	static unsigned tick, shown_bad_region;
	static unsigned mode_key[16][2], mode_n[16], mode_nk;
	static unsigned long long mode_px[16];
	if (q.fm_rej[0] < 0 || !q.has_pre)
		return;
	const unsigned long long area = (unsigned long long)q.rw * (unsigned long long)q.rh;
	NqdGuestMem gm;
	for (int pi = 0; pi < 4; pi++) {
		const int rej = q.fm_rej[pi];
		if (rej != NQD_FM_OK) {
			rej_n[pi][rej]++;
			rej_px[pi][rej] += area;
			if (rej == NQD_FM_REJ_MODE && pi == 0) {
				// which pen/transfer modes are being refused, and how much area they carry
				const unsigned pen = q.w[0x10 / 4], xf = q.w[0xc / 4];
				unsigned k = 0;
				while (k < mode_nk && !(mode_key[k][0] == pen && mode_key[k][1] == xf))
					k++;
				if (k == mode_nk && mode_nk < 16) {
					mode_key[mode_nk][0] = pen;
					mode_key[mode_nk][1] = xf;
					mode_nk++;
				}
				if (k < 16) {
					mode_n[k]++;
					mode_px[k] += area;
				}
			}
			if (rej == NQD_FM_REJ_REGION_BAD && pi == 0 && q.fm[pi] && shown_bad_region++ < 12) {
				const uint32 base = q.fm[pi]->bad_region;
				printf("SheepForce shadow: call %u rejected for an undecodable region at %08x size %u bbox %d,%d,%d,%d words:", q.n, (unsigned)base,
				       ReadMacInt16(base), (int16)ReadMacInt16(base + 2), (int16)ReadMacInt16(base + 4), (int16)ReadMacInt16(base + 6), (int16)ReadMacInt16(base + 8));
				for (unsigned i = 5; i < ReadMacInt16(base) / 2 && i < 60; i++)
					printf(" %04x", ReadMacInt16(base + i * 2));
				printf("\n");
			}
			continue;
		}
		const NqdFillmaskPlan &pl = *q.fm[pi];
		bool conclusive = q.rw <= SHADOW_MAXW && q.rh <= SHADOW_MAXH && q.cw == q.rw && q.ch == q.rh && q.dbpp == 4 &&
				  pl.width == q.rw && pl.height == q.rh && Mac2HostAddr(gm.fb_mac()) + pl.start == q.dest;
		if (!conclusive) {
			inc_n[pi]++;
			inc_px[pi] += area;
			continue;
		}
		unsigned wrong = 0;
		struct bad_px_t { int x, y; uint32 pre, got, want; } list[20];
		for (int y = 0; y < q.rh; y++)
			for (int x = 0; x < q.rw; x++) {
				const uint32 got = nqd_word_load(q.dest + (size_t)y * q.row + (size_t)x * 4);
				const uint32 pre = nqd_word_load(q.pre + ((size_t)y * SHADOW_MAXW + x) * 4);
				const uint32 want = pl.spans.contains(x, y) ? nqd_fm_pixel(pl, x, y) : pre;
				if ((got ^ want) & 0xffffff00u) {
					if (wrong < 20)
						list[wrong] = { x, y, pre, got, want };
					wrong++;
				}
			}
		if (!wrong) {
			ex_n[pi]++;
			ex_px[pi] += area;
			continue;
		}
		bad_n[pi]++;
		bad_px[pi] += area;
		if (pi >= 2)
			printf("SheepForce shadow: %c-MISMATCH-LINE call %u rect %dx%d at %d,%d wrong %u of %d tiled %d pen %u regions %d spans %zu px\n", "ABCD"[pi], q.n, q.rw, q.rh,
			       (int16)(q.w[0xd4 / 4] & 0xffff), (int16)(q.w[0xd4 / 4] >> 16), wrong, q.rw * q.rh, (int)pl.tiled, (unsigned)pl.pen, pl.regions, pl.spans.pixels());
		if (shown[pi]++ < (pi >= 2 ? 12u : 4u)) {
			printf("SheepForce shadow: PRODUCTION RULE %s MISMATCH call %u: %u of %d pixels differ; rect %dx%d pen %u pattern %02x colour %08x regions %d (+%d skipped) spans %zu px\n",
			       names[pi], q.n, wrong, q.rw * q.rh, q.rw, q.rh, (unsigned)pl.pen, pl.pattern, (unsigned)pl.colour, pl.regions, pl.skipped_regions, pl.spans.pixels());
			for (unsigned i = 0; i < wrong && i < 6; i++)
				printf("    pixel %d,%d pre %08x got %08x want %08x\n", list[i].x, list[i].y, (unsigned)list[i].pre, (unsigned)list[i].got, (unsigned)list[i].want);
			if (pi == 0) {
				printf("    block:");
				for (int i = 0; i < 0x140; i++)
					printf(" %03x=%08x", i * 4, (unsigned)q.w[i]);
				printf("\n");
				for (uint32 off = 0; off < NQD_FM_BLOCK_SIZE; off += 4) {
					const uint32 w = q.w[off / 4];
					if (!nqd_guest_ptr_ok(w))
						continue;
					const uint32 base = ReadMacInt32(w);
					if (!nqd_guest_ptr_ok(base))
						continue;
					const unsigned sz = ReadMacInt16(base);
					const int t = (int16)ReadMacInt16(base + 2), l = (int16)ReadMacInt16(base + 4), b = (int16)ReadMacInt16(base + 6), r = (int16)ReadMacInt16(base + 8);
					if (sz < 10 || sz > 8192 || b <= t || r <= l)
						continue;
					printf("    region via +%03x at %08x size %u words:", (unsigned)off, (unsigned)base, sz);
					for (unsigned i = 0; i < sz / 2 && i < 60; i++)
						printf(" %04x", ReadMacInt16(base + i * 2));
					printf("\n");
				}
			}
		}
	}
	++tick;
	if (tick == 5 || tick == 10 || tick == 20 || (tick % 40) == 0)
		for (int pi = 0; pi < 4; pi++) {
			printf("SheepForce shadow: PRODUCTION RULE %s accepted: exact %u/%llu kpx, MISMATCH %u/%llu kpx, inconclusive %u/%llu kpx; rejected:",
			       names[pi], ex_n[pi], ex_px[pi] / 1000, bad_n[pi], bad_px[pi] / 1000, inc_n[pi], inc_px[pi] / 1000);
			for (int i = 1; i < NQD_FM_REJ_COUNT; i++)
				if (rej_n[pi][i])
					printf(" %s %u/%llu", nqd_fm_reject_name(i), rej_n[pi][i], rej_px[pi][i] / 1000);
			printf("\n");
		}
	if (tick == 5 || tick == 10 || tick == 20 || (tick % 40) == 0) {
		printf("SheepForce shadow: mode-rejected ops by (pen,transfer) n/kpx:");
		for (unsigned k = 0; k < mode_nk; k++)
			printf(" (%u,%u) %u/%llu", mode_key[k][0], mode_key[k][1], mode_n[k], mode_px[k] / 1000);
		printf("\n");
	}
}

/* The hypothesis the hook would rest on: the op paints the port's fillPixPat (+0x3e), clipped to the
 * intersection of every region in the block, with fore/back from the block. Solid 1-bit patterns need
 * no origin, so they can be verified even when the ROM changed nothing. */
static void code3_fill_verify(const nqd_shadow_op &q)
{
	const uint32 penm = q.w[0x10 / 4];
	if ((penm != 8 && penm != 12) || q.w[0xc / 4] != 8 || !q.has_pre || q.dbpp != 4)
		return;
	const uint32 port = q.w[1];
	if (!nqd_guest_ptr_ok(port) || (ReadMacInt16(port + 6) & 0xc000) != 0xc000)
		return;
	static unsigned long long px_exact, px_bad, px_skipped, px_nonsolid;
	static unsigned n_exact, n_bad, n_skipped, n_nonsolid, total, shown_bad, shown_ns;
	const unsigned long long area = (unsigned long long)q.rw * (unsigned long long)q.rh;
	nqd_pat pt;
	if (!nqd_pat_load(port, 0x3e, &pt)) {
		n_skipped++; px_skipped += area;
		return;
	}
	const uint32 fore = shadow_pen_word(q.w[0x1c / 4]), back = shadow_pen_word(q.w[0x20 / 4]);
	bool solid = pt.type == 0;
	for (int i = 1; i < 8 && solid; i++)
		solid = pt.bits[i] == pt.bits[0];
	solid = solid && (pt.bits[0] == 0x00 || pt.bits[0] == 0xff);
	if (!solid) {
		n_nonsolid++; px_nonsolid += area;
		if (pt.type == 0 && shown_ns < 24 && q.ncand >= 0) {
			// log the geometry so the origin rule can be derived
			shown_ns++;
			const int rl = (int16)(q.w[0xd4 / 4] & 0xffff), rt = (int16)(q.w[0xd4 / 4] >> 16);
			const int bl = (int16)(q.w[0x6c / 4] & 0xffff), bt = (int16)(q.w[0x6c / 4] >> 16);
			printf("SheepForce shadow: code 3 call %u NONSOLID fill bits %02x%02x%02x%02x%02x%02x%02x%02x type %d rect %d,%d bounds %d,%d size %dx%d fore %08x back %08x; origins:",
			       q.n, pt.bits[0], pt.bits[1], pt.bits[2], pt.bits[3], pt.bits[4], pt.bits[5], pt.bits[6], pt.bits[7], pt.type, rl, rt, bl, bt, q.rw, q.rh,
			       (unsigned)fore, (unsigned)back);
			for (int oy = 0; oy < 8; oy++)
				for (int ox = 0; ox < 8; ox++) {
					bool ok = true;
					for (int y = 0; y < q.rh && y < q.ch && ok; y++)
						for (int x = 0; x < q.rw && x < q.cw && ok; x++) {
							bool in = true;
							for (int k = 0; k < q.ncand && in; k++)
								in = q.cmask[k][y * SHADOW_MAXW + x];
							if (!in)
								continue;
							const uint32 got = nqd_word_load(q.dest + (size_t)y * q.row + (size_t)x * 4);
							const int X = rl - bl + x + ox, Y = rt - bt + y + oy;
							const uint32 want = (pt.bits[Y & 7] >> (7 - (X & 7))) & 1 ? fore : back;
							ok = !((got ^ want) & 0xffffff00u);
						}
					if (ok)
						printf(" (%d,%d)", ox, oy);
				}
			printf("\n");
		}
		return;
	}
	const uint32 want_c = ((penm == 8) == (pt.bits[0] == 0xff)) ? fore : back;
	unsigned in_wrong = 0, out_changed = 0, wrong_unchanged = 0;
	for (int y = 0; y < q.rh && y < q.ch; y++)
		for (int x = 0; x < q.rw && x < q.cw; x++) {
			bool in = true;
			for (int k = 0; k < q.ncand && in; k++)
				in = q.cmask[k][y * SHADOW_MAXW + x];
			const uint32 got = nqd_word_load(q.dest + (size_t)y * q.row + (size_t)x * 4);
			const uint32 pre = nqd_word_load(q.pre + ((size_t)y * SHADOW_MAXW + x) * 4);
			if (in) {
				if ((got ^ want_c) & 0xffffff00u)
				{
					in_wrong++;
					if (!((got ^ pre) & 0xffffff00u))
						wrong_unchanged++;
				}
			} else if ((got ^ pre) & 0xffffff00u) {
				out_changed++;
			}
		}
	{
		// Which block words separate verified fills from the ones that were not pattern fills at all?
		struct vs { uint32 v[5]; int n; bool over; };
		static vs ex[0x140], bd[0x140];
		auto add = [](vs &x, uint32 v) {
			for (int i = 0; i < x.n; i++)
				if (x.v[i] == v)
					return;
			if (x.n < 5)
				x.v[x.n++] = v;
			else
				x.over = true;
		};
		for (int i = 0; i < 0x140; i++)
			add((!in_wrong && !out_changed) ? ex[i] : bd[i], q.w[i]);
		static unsigned seen_ops;
		if ((++seen_ops % 80) == 0) {
			printf("SheepForce shadow: code 3 fields that separate exact fills from the rest:");
			for (int i = 0; i < 0x140; i++)
				if (ex[i].n && bd[i].n && !ex[i].over && !bd[i].over) {
					bool disjoint = true;
					for (int a2 = 0; a2 < ex[i].n && disjoint; a2++)
						for (int b2 = 0; b2 < bd[i].n; b2++)
							if (ex[i].v[a2] == bd[i].v[b2])
								disjoint = false;
					if (disjoint) {
						printf(" %03x[ex", i * 4);
						for (int a2 = 0; a2 < ex[i].n; a2++)
							printf(" %x", (unsigned)ex[i].v[a2]);
						printf(" | bad");
						for (int b2 = 0; b2 < bd[i].n; b2++)
							printf(" %x", (unsigned)bd[i].v[b2]);
						printf("]");
					}
				}
			printf("\n");
		}
	}
	{
		// Ops where the ROM drew nothing at all inside a rectangle we would have filled.
		static unsigned n_noop; static unsigned long long px_noop;
		static bool announced;
		if (in_wrong && !out_changed && wrong_unchanged == in_wrong && in_wrong >= (unsigned)(q.cw * q.ch) / 2) {
			n_noop++;
			px_noop += area;
			if (!announced && n_noop >= 20) {
				announced = true;
				printf("SheepForce shadow: code 3 ROM drew nothing in %u fills we would have painted (%llu kpx)\n", n_noop, px_noop / 1000);
			}
		}
	}
	if (in_wrong && !out_changed && area >= 20000) {
		// Draw what the ROM did. '#' painted our colour, '.' left untouched where we would paint, 'o' something else, ' ' outside the clip.
		static unsigned grids;
		if (grids < 4) {
			grids++;
			int fy = 0;
			for (int y = 0; y < q.rh && y < q.ch; y++) {
				bool any = false;
				for (int x = 0; x < q.rw && x < q.cw; x++) {
					bool in = true;
					for (int k = 0; k < q.ncand && in; k++)
						in = q.cmask[k][y * SHADOW_MAXW + x];
					const uint32 got = nqd_word_load(q.dest + (size_t)y * q.row + (size_t)x * 4);
					const uint32 pre = nqd_word_load(q.pre + ((size_t)y * SHADOW_MAXW + x) * 4);
					if (in && !((got ^ pre) & 0xffffff00u) && ((got ^ want_c) & 0xffffff00u))
						any = true;
				}
				if (any) { fy = y; break; }
			}
			printf("SheepForce shadow: code 3 call %u GRID rows %d..%d of rect %dx%d (# painted, . untouched, o other, ' ' outside clip); fill pattern %02x, pn/bk/fill bytes:",
			       q.n, fy, fy + 13, q.rw, q.rh, pt.bits[0]);
			{
				static const uint32 offs[3] = { 0x3a, 0x20, 0x3e };
				const uint32 port = q.w[1];
				for (int si = 0; si < 3; si++) {
					const uint32 hp = ReadMacInt32(port + offs[si]);
					const uint32 pt2 = nqd_guest_ptr_ok(hp) ? ReadMacInt32(hp) : 0;
					printf(" [");
					if (nqd_guest_ptr_ok(pt2))
						for (int i = 0; i < 8; i++)
							printf("%02x", (unsigned)ReadMacInt8(pt2 + 20 + i));
					printf("]");
				}
			}
			printf("\n");
			for (int y = fy; y < fy + 14 && y < q.rh && y < q.ch; y++) {
				char line[100];
				int n = 0;
				for (int x = 0; x < 90 && x < q.rw && x < q.cw; x++) {
					bool in = true;
					for (int k = 0; k < q.ncand && in; k++)
						in = q.cmask[k][y * SHADOW_MAXW + x];
					const uint32 got = nqd_word_load(q.dest + (size_t)y * q.row + (size_t)x * 4);
					const uint32 pre = nqd_word_load(q.pre + ((size_t)y * SHADOW_MAXW + x) * 4);
					char c;
					if (!in) c = ' ';
					else if (!((got ^ want_c) & 0xffffff00u)) c = '#';
					else if (!((got ^ pre) & 0xffffff00u)) c = '.';
					else c = 'o';
					line[n++] = c;
				}
				line[n] = 0;
				printf("    |%s|\n", line);
			}
		}
	}
	if (in_wrong && !out_changed && wrong_unchanged == in_wrong && area >= 4000) {
		// The ROM's clip is smaller than ours. Look for a region anywhere near the block that makes the prediction exact.
		static unsigned discovered;
		if (discovered < 14) {
			discovered++;
			const int rt = (int16)(q.w[0xd4 / 4] >> 16), rl = (int16)(q.w[0xd4 / 4] & 0xffff);
			const int rb = (int16)(q.w[0xd8 / 4] >> 16), rr = (int16)(q.w[0xd8 / 4] & 0xffff);
			static uint8 tmp[SHADOW_MAXH * SHADOW_MAXW], allm[SHADOW_MAXH * SHADOW_MAXW];
			for (int y = 0; y < q.rh && y < q.ch; y++)
				for (int x = 0; x < q.rw && x < q.cw; x++) {
					bool in = true;
					for (int k = 0; k < q.ncand && in; k++)
						in = q.cmask[k][y * SHADOW_MAXW + x];
					allm[y * SHADOW_MAXW + x] = in;
				}
			struct node { uint32 addr; int parent; uint32 via; };
			static node nodes[5000];
			int n = 0, tested = 0, explained = 0;
			for (uint32 off = 0; off < 0x500; off += 4) {
				const uint32 w = q.w[off / 4];
				if (nqd_guest_ptr_ok(w) && n < 5000)
					nodes[n++] = { w, -1, off };
			}
			for (int i = 0; i < n && explained < 4; i++) {
				const uint32 base_addrs[2] = { nodes[i].addr, ReadMacInt32(nodes[i].addr) };
				for (int bi = 0; bi < 2; bi++) {
					const uint32 base = base_addrs[bi];
					if (!nqd_guest_ptr_ok(base))
						continue;
					const unsigned sz = ReadMacInt16(base);
					const int t = (int16)ReadMacInt16(base + 2), l = (int16)ReadMacInt16(base + 4);
					const int b = (int16)ReadMacInt16(base + 6), r = (int16)ReadMacInt16(base + 8);
					if (sz < 10 || sz > 8192 || b <= t || r <= l || b <= rt || t >= rb || r <= rl || l >= rr)
						continue;
					if (t < -4096 || l < -4096 || b > 8192 || r > 8192 || b - t > 4096 || r - l > 4096)
						continue;	// not a plausible screen region
					bool rect_only = false;
					if (!region_decode(base, rt, rl, rb, rr, tmp, &rect_only))
						continue;
					tested++;
					unsigned mism = 0, tmpcover = 0;
					for (int y = 0; y < q.rh && y < q.ch && mism < 8; y++)
						for (int x = 0; x < q.rw && x < q.cw; x++) {
							const bool in = allm[y * SHADOW_MAXW + x] && tmp[y * SHADOW_MAXW + x];
							tmpcover += in;
							const uint32 got = nqd_word_load(q.dest + (size_t)y * q.row + (size_t)x * 4);
							const uint32 pre = nqd_word_load(q.pre + ((size_t)y * SHADOW_MAXW + x) * 4);
							const uint32 want = in ? want_c : pre;
							if ((got ^ want) & 0xffffff00u)
								mism++;
						}
					if (!mism && tmpcover) {
						explained++;
						printf("SheepForce shadow: code 3 call %u DISCOVERED missing clip: region %08x%s size %u bbox %d,%d,%d,%d; path:", q.n, (unsigned)base,
						       bi ? " (via handle)" : "", sz, t, l, b, r);
						int depth = 0;
						for (int k = i; k >= 0 && depth < 6; k = nodes[k].parent, depth++)
							printf(" [%x%s]", (unsigned)nodes[k].via, nodes[k].parent < 0 ? "@blk" : "");
						printf("\n");
					}
				}
				int depth = 0;
				for (int k = i; nodes[k].parent >= 0; k = nodes[k].parent)
					depth++;
				if (depth >= 2)
					continue;
				for (uint32 off = 0; off < 0xc0 && n < 5000; off += 4) {
					const uint32 v = ReadMacInt32(nodes[i].addr + off);
					if (nqd_guest_ptr_ok(v))
						nodes[n++] = { v, i, off };
				}
			}
			if (!explained)
				printf("SheepForce shadow: code 3 call %u clip discovery found nothing (%d nodes, %d regions tested)\n", q.n, n, tested);
		}
	}
	if (!in_wrong && !out_changed) {
		n_exact++; px_exact += area;
		if (n_exact < 40 || (n_exact % 10) == 0)
			printf("SheepForce shadow: code 3 call %u exact solid fill: pattern %02x -> %08x rect %dx%d at %d,%d, %d regions, src30 %08x src34 %08x src48 %08x\n",
			       q.n, pt.bits[0], (unsigned)want_c, q.rw, q.rh, (int16)(q.w[0xd4 / 4] & 0xffff), (int16)(q.w[0xd4 / 4] >> 16), q.ncand,
			       (unsigned)q.w[0x30 / 4], (unsigned)q.w[0x34 / 4], (unsigned)q.w[0x48 / 4]);
	} else {
		n_bad++; px_bad += area;
		if (shown_bad++ < 60)
			printf("SheepForce shadow: code 3 call %u SOLID FILL MISMATCH: pattern %02x -> %08x, %u inside wrong (%u of them untouched by the ROM), %u outside changed, %d regions (+%d over cap), rect %dx%d at %d,%d, pen %u, src30 %08x src34 %08x src48 %08x fore %08x\n",
			       q.n, pt.bits[0], (unsigned)want_c, in_wrong, wrong_unchanged, out_changed, q.ncand, q.over, q.rw, q.rh, (int16)(q.w[0xd4 / 4] & 0xffff), (int16)(q.w[0xd4 / 4] >> 16),
			       (unsigned)q.w[0x10 / 4], (unsigned)q.w[0x30 / 4], (unsigned)q.w[0x34 / 4], (unsigned)q.w[0x48 / 4], (unsigned)fore);
	}
	if ((++total % 40) == 0)
		printf("SheepForce shadow: code 3 fillPixPat solid verify (ops/kpixels): exact %u/%llu BAD %u/%llu; nonsolid %u/%llu, no pattern %u/%llu\n",
		       n_exact, px_exact / 1000, n_bad, px_bad / 1000, n_nonsolid, px_nonsolid / 1000, n_skipped, px_skipped / 1000);
}

/* For solid-pattern ops: which of the port's slots (bk 0x20, pn 0x3a, fill 0x3e) predicts the ROM's pixels exactly,
 * the clip being the intersection of the block's regions? Tallied by area, and the block words that tell the
 * cases apart are reported. */
static void code3_slot_verify(const nqd_shadow_op &q)
{
	const uint32 penm = q.w[0x10 / 4];
	if ((penm != 8 && penm != 12) || q.w[0xc / 4] != 8 || !q.has_pre || q.dbpp != 4)
		return;
	const uint32 port = q.w[1];
	if (!nqd_guest_ptr_ok(port) || (ReadMacInt16(port + 6) & 0xc000) != 0xc000)
		return;
	static const uint32 offs[3] = { 0x20, 0x3a, 0x3e };
	const uint32 fore = shadow_pen_word(q.w[0x1c / 4]), back = shadow_pen_word(q.w[0x20 / 4]);
	int st[3];			// 0 not a solid 1-bit pattern, 1 exact, 2 mismatch
	for (int si = 0; si < 3; si++) {
		st[si] = 0;
		nqd_pat pt;
		if (!nqd_pat_load(port, offs[si], &pt) || pt.type != 0)
			continue;
		bool solid = true;
		for (int i = 1; i < 8 && solid; i++)
			solid = pt.bits[i] == pt.bits[0];
		if (!solid || (pt.bits[0] != 0x00 && pt.bits[0] != 0xff))
			continue;
		const uint32 want_c = ((penm == 8) == (pt.bits[0] == 0xff)) ? fore : back;
		unsigned bad = 0;
		for (int y = 0; y < q.rh && y < q.ch && !bad; y++)
			for (int x = 0; x < q.rw && x < q.cw; x++) {
				bool in = true;
				for (int k = 0; k < q.ncand && in; k++)
					in = q.cmask[k][y * SHADOW_MAXW + x];
				const uint32 got = nqd_word_load(q.dest + (size_t)y * q.row + (size_t)x * 4);
				const uint32 pre = nqd_word_load(q.pre + ((size_t)y * SHADOW_MAXW + x) * 4);
				if ((got ^ (in ? want_c : pre)) & 0xffffff00u) {
					bad++;
					break;
				}
			}
		st[si] = bad ? 2 : 1;
	}
	static unsigned n[27];
	static unsigned long long px[27];
	const int key = st[0] * 9 + st[1] * 3 + st[2];
	const unsigned long long area = (unsigned long long)q.rw * (unsigned long long)q.rh;
	n[key]++;
	px[key] += area;
	// separators: ops where fill is exact (class A) vs ops where fill is wrong but bk is exact (class B)
	struct vs { uint32 v[5]; int n; bool over; };
	static vs ca[0x140], cb[0x140];
	auto add = [](vs &x, uint32 v) {
		for (int i = 0; i < x.n; i++)
			if (x.v[i] == v)
				return;
		if (x.n < 5) x.v[x.n++] = v; else x.over = true;
	};
	if (st[2] == 1 && st[0] != 1)
		for (int i = 0; i < 0x140; i++) add(ca[i], q.w[i]);
	if (st[2] == 2 && st[0] == 1)
		for (int i = 0; i < 0x140; i++) add(cb[i], q.w[i]);
	static unsigned tick;
	if ((++tick % 50) == 0) {
		printf("SheepForce shadow: code 3 slot match (bk,pn,fill: 0 n/a, 1 exact, 2 wrong) ops/kpx:");
		for (int k = 0; k < 27; k++)
			if (n[k])
				printf(" [%d%d%d]%u/%llu", k / 9, (k / 3) % 3, k % 3, n[k], px[k] / 1000);
		printf("\n");
		printf("SheepForce shadow: code 3 fields separating fill-correct ops from bk-correct ops:");
		for (int i = 0; i < 0x140; i++)
			if (ca[i].n && cb[i].n && !ca[i].over && !cb[i].over) {
				bool disjoint = true;
				for (int a2 = 0; a2 < ca[i].n && disjoint; a2++)
					for (int b2 = 0; b2 < cb[i].n; b2++)
						if (ca[i].v[a2] == cb[i].v[b2])
							disjoint = false;
				if (disjoint) {
					printf(" %03x[fill", i * 4);
					for (int a2 = 0; a2 < ca[i].n; a2++) printf(" %x", (unsigned)ca[i].v[a2]);
					printf(" | bk");
					for (int b2 = 0; b2 < cb[i].n; b2++) printf(" %x", (unsigned)cb[i].v[b2]);
					printf("]");
				}
			}
		printf("\n");
	}
}

static void code3_port_check(const nqd_shadow_op &q)
{
	if (q.w[0x10 / 4] != 8 || q.w[0xc / 4] != 8 || !q.has_pre || q.dbpp != 4)
		return;
	const uint32 port = q.w[1];
	if (!nqd_guest_ptr_ok(port) || (ReadMacInt16(port + 6) & 0xc000) != 0xc000)
		return;
	static const struct { const char *n; uint32 o; } slots[3] = { { "bk", 0x20 }, { "pn", 0x3a }, { "fill", 0x3e } };
	static unsigned long long match_px[8];	// by bitmask of the patterns that match exactly
	static unsigned match_n[8], total;
	const int rl = (int16)(q.w[0xd4 / 4] & 0xffff), rt = (int16)(q.w[0xd4 / 4] >> 16);
	const int bl = (int16)(q.w[0x6c / 4] & 0xffff), bt = (int16)(q.w[0x6c / 4] >> 16);
	const int X0 = rl - bl, Y0 = rt - bt;
	const uint32 fore = shadow_pen_word(q.w[0x1c / 4]), back = shadow_pen_word(q.w[0x20 / 4]);
	// the effective clip is the intersection of every region candidate in the block
	auto inmask = [&](int x, int y) {
		for (int k = 0; k < q.ncand; k++)
			if (!q.cmask[k][y * SHADOW_MAXW + x])
				return false;
		return true;
	};
	int px[24], py[24], np = 0;
	uint32 pv[24];
	for (int y = 0; y < q.ch && np < 24; y++)
		for (int x = 0; x < q.cw && np < 24; x += 3) {
			uint32 got = nqd_word_load(q.dest + (size_t)y * q.row + (size_t)x * 4);
			uint32 pre = nqd_word_load(q.pre + ((size_t)y * SHADOW_MAXW + x) * 4);
			if (((got ^ pre) & 0xffffff00u) && inmask(x, y)) {
				px[np] = x; py[np] = y; pv[np] = got; np++;
			}
		}
	unsigned mask = 0;
	int found_ox[3] = { -1, -1, -1 }, found_oy[3] = { -1, -1, -1 };
	char desc[160];
	desc[0] = 0;
	for (int si = 0; si < 3; si++) {
		nqd_pat pt;
		if (!nqd_pat_load(port, slots[si].o, &pt)) {
			strcat(desc, " ");
			strcat(desc, slots[si].n);
			strcat(desc, "=none");
			continue;
		}
		const int tw = pt.type ? pt.pp.xw : 8, th = pt.type ? pt.pp.xh : 8;
		auto pix = [&](int tx, int ty) -> uint32 {
			if (pt.type)
				return nqd_word_load(Mac2HostAddr(pt.pp.xdata) + (size_t)ty * pt.pp.xrow + (size_t)tx * 4);
			return (pt.bits[ty & 7] >> (7 - (tx & 7))) & 1 ? fore : back;
		};
		int sols = 0, bx = -1, by = -1;
		unsigned best_mism = 0xffffffffu, best_in = 0, best_out = 0;
		if (np >= 3)
			for (int oy = 0; oy < th; oy++)
				for (int ox = 0; ox < tw; ox++) {
					bool all = true;
					for (int i = 0; i < np && all; i++)
						all = !((pix((X0 + px[i] + ox) % tw, (Y0 + py[i] + oy) % th) ^ pv[i]) & 0xffffff00u);
					if (!all)
						continue;
					// full window check against the mask: wrong inside, changed outside
					unsigned mism = 0, in_wrong = 0, out_changed = 0;
					for (int y = 0; y < q.rh && y < q.ch; y++)
						for (int x = 0; x < q.rw && x < q.cw; x++) {
							uint32 got = nqd_word_load(q.dest + (size_t)y * q.row + (size_t)x * 4);
							uint32 pre = nqd_word_load(q.pre + ((size_t)y * SHADOW_MAXW + x) * 4);
							const bool in = inmask(x, y);
							if (in) {
								if ((got ^ pix((X0 + x + ox) % tw, (Y0 + y + oy) % th)) & 0xffffff00u)
									in_wrong++;
							} else if ((got ^ pre) & 0xffffff00u) {
								out_changed++;
							}
						}
					mism = in_wrong + out_changed;
					if (best_mism == 0xffffffffu || mism < best_mism) {
						best_mism = mism; best_in = in_wrong; best_out = out_changed;
					}
					if (!mism) {
						if (!sols) { bx = ox; by = oy; }
						sols++;
					}
				}
		char one[48];
		if (best_mism == 0xffffffffu)
			snprintf(one, sizeof one, " %s=%s:nosample", slots[si].n, pt.type ? "pix" : "bit");
		else
			snprintf(one, sizeof one, " %s=%s:%s(in%u,out%u)", slots[si].n, pt.type ? "pix" : "bit", sols ? "MATCH" : "x", best_in, best_out);
		strcat(desc, one);
		if (sols) {
			mask |= 1u << si;
			found_ox[si] = bx;
			found_oy[si] = by;
		}
	}
	{
		// ops that only one slot explains: dump the block words so the field that picks the slot can be found
		static unsigned shown[8];
		if ((mask == 1 || mask == 2 || mask == 4) && shown[mask]++ < 6) {
			printf("SheepForce shadow: code 3 call %u ONLY %s explains it (pen %u transfer %u rect %dx%d):", q.n,
			       mask == 1 ? "bk" : mask == 2 ? "pn" : "fill", (unsigned)q.w[0x10 / 4], (unsigned)q.w[0xc / 4], q.rw, q.rh);
			for (int i = 0; i < 0xb0 / 4; i++)
				printf(" %03x=%08x", i * 4, (unsigned)q.w[i]);
			printf("\n");
		}
	}
	match_n[mask]++;
	match_px[mask] += (unsigned long long)q.rw * (unsigned long long)q.rh;
	if (np >= 3 && (q.n <= 12 || (q.n % 40) == 0))
		printf("SheepForce shadow: code 3 call %u port %08x patterns:%s | origin bk %d,%d pn %d,%d fill %d,%d | rect@dest %d,%d bounds %d,%d fore %08x back %08x\n",
		       q.n, (unsigned)port, desc, found_ox[0], found_oy[0], found_ox[1], found_oy[1], found_ox[2], found_oy[2], X0, Y0, bl, bt,
		       (unsigned)fore, (unsigned)back);
	if ((++total % 40) == 0) {
		printf("SheepForce shadow: code 3 pattern match by set (bk=1 pn=2 fill=4), ops/kpixels:");
		for (int m = 0; m < 8; m++)
			if (match_n[m])
				printf(" set%d %u/%llu", m, match_n[m], match_px[m] / 1000);
		printf("\n");
	}
}

/* Shadow check for hook code 3 with a patCopy pen: predict the ROM's pixels from the expanded pattern. */
static void code3_check(const nqd_shadow_op &q)
{
	if (q.has_pre && q.dbpp == 4) {
		code3_stats(q);
		code3_prod_verify(q);
		code3_fill_verify(q);
		code3_slot_verify(q);
		code3_port_check(q);
	}
	static unsigned n_seen, n_nopat, n_noxdata, n_noorigin, n_pred;
	static unsigned cand_seen[0x140], cand_exact[0x140];
	auto R = [&](const char *m, int v = 0) { if (q.n <= 40) printf("SheepForce shadow: code 3 call %u result: %s %d\n", q.n, m, v); };
	if (q.n <= 12)
		printf("SheepForce shadow: code 3 call %u entry: pen %u transfer %u has_pre %d dbpp %d rect %dx%d\n", q.n, (unsigned)q.w[0x10 / 4], (unsigned)q.w[0xc / 4], (int)q.has_pre, q.dbpp, q.rw, q.rh);
	if (q.w[0x10 / 4] != 8 || q.w[0xc / 4] != 8 || !q.has_pre || q.dbpp != 4)
		return;
	n_seen++;
	nqd_pixpat pp;
	uint32 via = 0;
	if (!nqd_pixpat_find(q.w, &pp, &via)) {
		n_nopat++;
		R("no PixPat");
		if (n_nopat <= 5)
			printf("SheepForce shadow: code 3 call %u: no PixPat reachable (pen %u transfer %u)\n", q.n, (unsigned)q.w[0x10 / 4], (unsigned)q.w[0xc / 4]);
		return;
	}
	if (!pp.xdata || pp.xvalid != 32 || pp.xw <= 0 || pp.xh <= 0 || pp.xrow < pp.xw * 4) {
		n_noxdata++;
		R("no expansion");
		if (n_noxdata <= 6) {
			const uint32 hx = ReadMacInt32(pp.addr + 10), hxm = ReadMacInt32(pp.addr + 16), hd = ReadMacInt32(pp.addr + 6);
			printf("SheepForce shadow: code 3 call %u pattern has no usable 32-bit expansion (xvalid %d xdata %08x %dx%d rowbytes %d); "
			       "patData %08x->%08x patXData %08x->%08x patXMap %08x->%08x\n",
			       q.n, pp.xvalid, (unsigned)pp.xdata, pp.xw, pp.xh, pp.xrow,
			       (unsigned)hd, (unsigned)ReadMacInt32(hd), (unsigned)hx, (unsigned)ReadMacInt32(hx), (unsigned)hxm, (unsigned)ReadMacInt32(hxm));
			const uint32 xpm = ReadMacInt32(hxm);
			if (nqd_guest_ptr_ok(xpm))
				printf("    patXMap pixmap %08x: base %08x rowBytes %04x bounds %d,%d,%d,%d pixelSize %u\n", (unsigned)xpm,
				       (unsigned)ReadMacInt32(xpm), ReadMacInt16(xpm + 4), (int16)ReadMacInt16(xpm + 6), (int16)ReadMacInt16(xpm + 8),
				       (int16)ReadMacInt16(xpm + 10), (int16)ReadMacInt16(xpm + 12), ReadMacInt16(xpm + 32));
		}
		return;
	}
	const uint8 *xd = Mac2HostAddr(pp.xdata);
	auto pat = [&](int x, int y) {		// tile pixel (x, y), already reduced
		return nqd_word_load(xd + (size_t)y * pp.xrow + (size_t)x * 4);
	};
	// Origin: find (ox, oy) such that every changed pixel of the first rows equals the tile there.
	const int rl = (int16)(q.w[0xd4 / 4] & 0xffff), rt = (int16)(q.w[0xd4 / 4] >> 16);
	const int bl = (int16)(q.w[0x6c / 4] & 0xffff), bt = (int16)(q.w[0x6c / 4] >> 16);
	// q.w is converted to host order from guest big-endian: a rect word holds top in the high half.
	int px[24], py[24];
	uint32 pv[24];
	int np = 0;
	for (int y = 0; y < q.ch && np < 24; y++)
		for (int x = 0; x < q.cw && np < 24; x += 5) {
			uint32 got = nqd_word_load(q.dest + (size_t)y * q.row + (size_t)x * 4);
			uint32 pre = nqd_word_load(q.pre + ((size_t)y * SHADOW_MAXW + x) * 4);
			if (got != pre) {
				px[np] = x; py[np] = y; pv[np] = got; np++;
			}
		}
	if (np < 6) {
		R("too few changed pixels", np);
		static unsigned few;
		if (few++ < 5)
			printf("SheepForce shadow: code 3 call %u: only %d changed pixels sampled, cannot pin the origin\n", q.n, np);
		return;				// too little changed to pin the origin
	}
	const int X0 = rl - bl, Y0 = rt - bt;	// rectangle origin in destination pixel coordinates
	int sols = 0, ox = -1, oy = -1;
	for (int oyy = 0; oyy < pp.xh; oyy++)
		for (int oxx = 0; oxx < pp.xw; oxx++) {
			bool all = true;
			for (int i = 0; i < np && all; i++)
				all = !((pat((X0 + px[i] + oxx) % pp.xw, (Y0 + py[i] + oyy) % pp.xh) ^ pv[i]) & 0xffffff00u);
			if (all) {
				if (!sols) { ox = oxx; oy = oyy; }
				sols++;
			}
		}
	if (!sols) {
		n_noorigin++;
		R("no origin", np);
		if (n_noorigin <= 5)
			printf("SheepForce shadow: code 3 call %u no origin makes the tile match (%d changed pixels, rect %d,%d tile %dx%d)\n",
			       q.n, np, rl, rt, pp.xw, pp.xh);
		return;
	}
	n_pred++;
	R("origin found", sols);
	if (n_pred <= 8 || (n_pred % 25) == 0)
		printf("SheepForce shadow: code 3 call %u pattern %08x via %04x.%02x tile %dx%d origin (%d,%d) solutions %d rect@dest %d,%d bounds %d,%d\n",
		       q.n, (unsigned)pp.addr, (unsigned)(via >> 16), (unsigned)(via & 0xffff), pp.xw, pp.xh, ox, oy, sols, X0, Y0, bl, bt);
	// Exactness per candidate mask: inside the region the tile, outside the pre-image.
	for (int k = 0; k < q.ncand; k++) {
		unsigned mism = 0;
		for (int y = 0; y < q.rh && y < q.ch; y++)
			for (int x = 0; x < q.rw && x < q.cw; x++) {
				uint32 got = nqd_word_load(q.dest + (size_t)y * q.row + (size_t)x * 4);
				uint32 pre = nqd_word_load(q.pre + ((size_t)y * SHADOW_MAXW + x) * 4);
				uint32 want = q.cmask[k][y * SHADOW_MAXW + x] ?
					pat((X0 + x + ox) % pp.xw, (Y0 + y + oy) % pp.xh) : pre;
				mism += ((got ^ want) & 0xffffff00u) != 0;
			}
		const unsigned f = (unsigned)q.cand_off[k] / 4;
		cand_seen[f]++;
		cand_exact[f] += mism == 0;
	}
	if ((n_pred % 25) == 0) {
		printf("SheepForce shadow: code 3 summary: %u ops, %u no pattern, %u no expansion, %u no origin, %u predicted; mask fields (exact/seen):",
		       n_seen, n_nopat, n_noxdata, n_noorigin, n_pred);
		for (unsigned f = 0; f < 0x140; f++)
			if (cand_seen[f] >= 3)
				printf(" %03x:%u/%u", f * 4, cand_exact[f], cand_seen[f]);
		printf("\n");
	}
}

/* Compare one finished operation with its decoder's prediction. */
static void shadow_check(const nqd_shadow_op &q)
{
	if (q.code == 3)
		code3_check(q);
	static unsigned ok[8], bad[8], skipped[8];
	static unsigned shown_bad[8];
	const unsigned c = q.code & 7;
	if (q.has_pre && q.ncand > 0) {
		// Which block field is the mask? A field is consistent with an operation when
		// every pixel the ROM changed lies inside that field's region.
		static unsigned total[8][0x140], contained[8][0x140], ops[8], noop[8];
		unsigned changed_total = 0;
		bool contained_by[16] = {false};
		for (int k = 0; k < q.ncand; k++) {
			unsigned out_changed = 0;
			changed_total = 0;
			for (int y = 0; y < q.ch; y++)
				for (int x = 0; x < q.cw; x++) {
					bool ch = ((nqd_word_load(q.dest + (size_t)y * q.row + (size_t)x * 4) ^
						    nqd_word_load(q.pre + ((size_t)y * SHADOW_MAXW + x) * 4)) & 0xffffff00u) != 0;
					if (!ch)
						continue;
					changed_total++;
					if (!q.cmask[k][y * SHADOW_MAXW + x])
						out_changed++;
				}
			contained_by[k] = out_changed == 0;
		}
		ops[c]++;
		if (changed_total == 0) {
			noop[c]++;	// nothing to learn: the ROM redrew identical pixels
		} else {
			for (int k = 0; k < q.ncand; k++) {
				total[c][q.cand_off[k] / 4]++;
				contained[c][q.cand_off[k] / 4] += contained_by[k];
			}
		}
		if ((ops[c] % 100) == 0) {
			printf("SheepForce shadow: code %u mask fields after %u ops (%u changed nothing):", q.code, ops[c], noop[c]);
			for (unsigned f = 0; f < 0x140; f++)
				if (total[c][f] >= 5)
					printf(" %03x:%u/%u", f * 4, contained[c][f], total[c][f]);
			printf("\n");
		}
	}
	if (q.code == 4 && q.dbpp == 4 && q.has_pre && q.w[3] == 0 &&
	    (q.w[0x48 / 4] == 2 || q.w[0x48 / 4] == 4 || q.w[0x48 / 4] == 8)) {
		// Indexed source (2, 4 or 8 bits per pixel) copied to a 32-bit destination through the source
		// pixmap's colour table, srcCopy: the candidate decoder for the "code 4" blits.
		static unsigned d4_ok[9], d4_bad[9], d4_clipped[9], d4_shown;
		const unsigned bits = q.w[0x48 / 4];
		const uint32 sbase = q.w[0x30 / 4];
		const int32 srow = (int32)q.w[0x34 / 4];
		const int sb_top = (int16)(q.w[0x38 / 4] >> 16), sb_left = (int16)q.w[0x38 / 4];
		const int sr_top = (int16)(q.w[0xcc / 4] >> 16), sr_left = (int16)q.w[0xcc / 4];
		const int sx = sr_left - sb_left, sy = sr_top - sb_top;
		const uint32 hnd = q.w[0x54 / 4];
		const uint32 ct = hnd >= 0x1000 && hnd < 0x1ff00000u ? ReadMacInt32(hnd) : 0;
		unsigned bad_here = 0, clipped_here = 0, informative = 0;
		int first_x = -1, first_y = -1;
		uint32 first_expect = 0, first_got = 0, first_pre = 0;
		unsigned first_idx = 0;
		if (sbase >= 0x1000 && srow > 0 && ct >= 0x1000 && ct < 0x1ff00000u) {
			const int n = (int)ReadMacInt16(ct + 6) + 1;
			for (int y = 0; y < q.ch && y < q.rh; y++)
				for (int x = 0; x < q.cw && x < q.rw; x++) {
					const uint32 off = sbase + (uint32)((sy + y) * srow) + (uint32)(((sx + x) * (int)bits) / 8);
					unsigned idx = ReadMacInt8(off);
					if (bits == 4) idx = ((sx + x) & 1) ? (idx & 15u) : (idx >> 4);
					else if (bits == 2) idx = (idx >> (6 - 2 * ((sx + x) & 3))) & 3u;
					uint32 expect = 0;
					if ((int)idx < n) {
						const uint32 e = ct + 8 + idx * 8u;
						expect = nqd_pack_rgb((uint8)(ReadMacInt16(e + 2) >> 8), (uint8)(ReadMacInt16(e + 4) >> 8), (uint8)(ReadMacInt16(e + 6) >> 8));
					}
					const uint32 got = nqd_word_load(q.dest + (size_t)y * q.row + (size_t)x * 4);
					const uint32 pre = nqd_word_load(q.pre + ((size_t)y * SHADOW_MAXW + x) * 4);
					if (!((pre ^ expect) & 0xffffff00u)) continue;
					informative++;
					if (!((got ^ expect) & 0xffffff00u)) continue;
					if (!((got ^ pre) & 0xffffff00u)) { clipped_here++; continue; }
					if (!bad_here) { first_x = x; first_y = y; first_expect = expect; first_got = got; first_pre = pre; first_idx = idx; }
					bad_here++;
				}
			if (bad_here) {
				d4_bad[bits]++;
				if (d4_shown++ < 8)
					printf("SheepForce shadow: code 4 call %u %u-bit MISMATCH %u px, first at %d,%d idx %u expect %08x got %08x pre %08x (ctable %08x n %d, src %08x row %d sx %d sy %d)\n",
					       q.n, bits, bad_here, first_x, first_y, first_idx, (unsigned)first_expect, (unsigned)first_got, (unsigned)first_pre,
					       (unsigned)ct, n, (unsigned)sbase, (int)srow, sx, sy);
			} else {
				d4_ok[bits]++;
				if (clipped_here) d4_clipped[bits]++;
			}
			if (((d4_ok[bits] + d4_bad[bits]) % 25) == 0)
				printf("SheepForce shadow: code 4 indexed->32 %u-bit: ok %u (of which clipped %u) mismatch %u\n", bits, d4_ok[bits], d4_clipped[bits], d4_bad[bits]);
		}
		(void)informative;
	}
	if (q.code == 5 && q.w[4] == 8 && q.dbpp == 4) {
		// "lines": the pen colour fills the rectangle wherever the clip lets it.
		// Pixels that stay as they were are clipped, which tells us the clip region's shape.
		const uint32 expect = shadow_pen_word(q.w[7]);
		static unsigned clip_ops, clip_exact[0x140], clip_cands[0x140];
		unsigned unclipped_wrong = 0, clipped = 0, informative = 0;
		static bool wrong[SHADOW_MAXH * SHADOW_MAXW];
		for (int y = 0; y < q.rh && y < q.ch; y++)
			for (int x = 0; x < q.rw && x < q.cw; x++) {
				uint32 got = nqd_word_load(q.dest + (size_t)y * q.row + (size_t)x * 4);
				uint32 pre = nqd_word_load(q.pre + ((size_t)y * SHADOW_MAXW + x) * 4);
				bool is_pen = !((got ^ expect) & 0xffffff00u);
				bool pre_pen = !((pre ^ expect) & 0xffffff00u);
				wrong[y * SHADOW_MAXW + x] = !pre_pen;		// pixel carries information
				if (!pre_pen) {
					informative++;
					if (!is_pen) {
						if (got != pre)
							unclipped_wrong++;	// changed, but not to the pen colour
						else
							clipped++;		// left alone: clipped
					}
				}
			}
		if (unclipped_wrong) {
			bad[c]++;
			if (shown_bad[c]++ < 6)
				printf("SheepForce shadow: code 5 call %u MISMATCH: %u pixels changed to something other than the pen %08x\n",
				       q.n, unclipped_wrong, (unsigned)q.w[7]);
			return;
		}
		ok[c]++;
		{
			static unsigned g_pass_clean, g_pass_clipped, g_fail_clean, g_fail_clipped, g_total;
			const bool was_clipped = clipped != 0;
			if (q.gate) { if (was_clipped) g_pass_clipped++; else g_pass_clean++; }
			else { if (was_clipped) g_fail_clipped++; else g_fail_clean++; }
			if (q.gate && was_clipped && g_pass_clipped <= 5) {
				printf("SheepForce shadow: GATE WRONG: code 5 call %u accelerated-but-clipped (%d regions seen), %u pixels clipped; rect %d x %d, pen %08x\n",
				       q.n, q.gate_regions, clipped, q.rw, q.rh, (unsigned)q.w[7]);
				for (int y = 0; y < q.rh && y < q.ch; y++)
					for (int x = 0; x < q.rw && x < q.cw; x++) {
						uint32 got = nqd_word_load(q.dest + (size_t)y * q.row + (size_t)x * 4);
						uint32 pre = nqd_word_load(q.pre + ((size_t)y * SHADOW_MAXW + x) * 4);
						if (wrong[y * SHADOW_MAXW + x] && ((got ^ expect) & 0xffffff00u) && got == pre)
							printf("    pixel (%d,%d) still %08x (pen %08x)\n", x, y, (unsigned)got, (unsigned)expect);
					}
				for (int c = 0; c < q.cap_n; c++) {
					printf("    gate region %d:", c);
					for (int k = 0; k < 40; k++)
						printf(" %d", (int)(int16)q.cap[c][k]);
					printf("\n");
				}
				printf("    dest base %08x row %d bounds %08x,%08x rect %08x,%08x pix %d\n", (unsigned)q.w[0x64 / 4], (int)q.w[0x68 / 4],
				       (unsigned)q.w[0x6c / 4], (unsigned)q.w[0x70 / 4], (unsigned)q.w[0xd4 / 4], (unsigned)q.w[0xd8 / 4], (int)q.w[0x7c / 4]);
				for (uint32 off = 0; off < 0x500; off += 4) {
					uint32 w0 = q.w[off / 4];
					if (!nqd_guest_ptr_ok(w0))
						continue;
					uint32 base = ReadMacInt32(w0);
					if (!nqd_guest_ptr_ok(base))
						continue;
					unsigned sz = ReadMacInt16(base);
					int rt = (int16)ReadMacInt16(base + 2), rl = (int16)ReadMacInt16(base + 4);
					int rb = (int16)ReadMacInt16(base + 6), rr = (int16)ReadMacInt16(base + 8);
					if (sz < 10 || sz > 8192 || rb <= rt || rr <= rl)
						continue;
					printf("    region via +%03x: size %u bbox t%d l%d b%d r%d\n", off, sz, rt, rl, rb, rr);
				}
			}
			static unsigned v_clean[4], v_clipped[4];
			for (int v = 0; v < 4; v++)
				if (q.gate_v[v]) { if (was_clipped) v_clipped[v]++; else v_clean[v]++; }
			if ((g_total + 1) % 250 == 0)
				printf("SheepForce shadow: gate variants (accelerated clean / CLIPPED): original %u/%u, +direct %u/%u, +deep handles %u/%u, +deep direct %u/%u\n",
				       v_clean[0], v_clipped[0], v_clean[1], v_clipped[1], v_clean[2], v_clipped[2], v_clean[3], v_clipped[3]);
			if ((++g_total % 250) == 0)
				printf("SheepForce shadow: clip gate: pass+clean %u (accelerated), pass+CLIPPED %u (must be 0), fail+clipped %u (correctly kept), fail+clean %u (opportunity lost)\n",
				       g_pass_clean, g_pass_clipped, g_fail_clipped, g_fail_clean);
		}
		if (clipped && q.ncand > 0) {
			clip_ops++;
			for (int k = 0; k < q.ncand; k++) {
				unsigned mism = 0;
				for (int y = 0; y < q.rh && y < q.ch; y++)
					for (int x = 0; x < q.rw && x < q.cw; x++) {
						if (!wrong[y * SHADOW_MAXW + x])
							continue;
						uint32 got = nqd_word_load(q.dest + (size_t)y * q.row + (size_t)x * 4);
						bool painted = !((got ^ expect) & 0xffffff00u);
						mism += (q.cmask[k][y * SHADOW_MAXW + x] != 0) != painted;
					}
				unsigned f = (unsigned)q.cand_off[k] / 4;
				clip_cands[f]++;
				clip_exact[f] += mism == 0;
			}
			if (clip_ops <= 3 || (clip_ops % 20) == 0) {
				printf("SheepForce shadow: code 5 clip fields after %u clipped lines (exact/seen):", clip_ops);
				for (unsigned f = 0; f < 0x140; f++)
					if (clip_cands[f] >= 3)
						printf(" %03x:%u/%u", f * 4, clip_exact[f], clip_cands[f]);
				printf("\n");
			}
		}
		(void)informative;
	} else {
		skipped[c]++;
	}
	if (((ok[c] + bad[c] + skipped[c]) % 200) == 0)
		printf("SheepForce shadow: code %u totals ok %u mismatch %u undecoded %u\n", q.code, ok[c], bad[c], skipped[c]);
}

static void shadow_flush(void)
{
	if (shadow_n == 0)
		return;
	if (SheepForceEnabled())
		SheepForceSync();
	for (unsigned k = 0; k < shadow_n; k++) {
		nqd_shadow_op &q = shadow_ops[k];
		if (!q.valid)
			continue;
		q.valid = false;
		if (q.n <= 12 && q.rw <= 40 && q.rh <= 16 && q.dbpp == 4)
			shadow_grid(q);
		if (q.n <= 12 && q.has_pre && q.code != 5)
			locate_pixels(q);
		shadow_check(q);
	}
	shadow_n = 0;
	fflush(stdout);
}

/* Discovery aid: look for QuickDraw regions (rgnSize, rgnBBox, scan data) at every
 * pointer-like word of the block and one handle level below it, and print them.
 * A region whose bounding box falls inside the operation's destination rectangle
 * (in local or global coordinates) is a candidate mask. */
static bool locate_ok_addr(uint32 a)
{
	return a >= 0x1000 && a < 0x1ff00000u;
}

static void locate_regions(uint32 p, uint32 code, uint32 n)
{
	const int bt = (int16)ReadMacInt16(p + acclDestBoundsRect + 0), bl = (int16)ReadMacInt16(p + acclDestBoundsRect + 2);
	const int rt = (int16)ReadMacInt16(p + acclDestRect + 0), rl = (int16)ReadMacInt16(p + acclDestRect + 2);
	const int rb = (int16)ReadMacInt16(p + acclDestRect + 4), rr = (int16)ReadMacInt16(p + acclDestRect + 6);
	uint32 done[64];
	unsigned nd = 0;
	for (uint32 off = 0; off < 0x500; off += 4) {
		uint32 w = ReadMacInt32(p + off);
		if (!locate_ok_addr(w))
			continue;
		for (int level = 0; level < 2; level++) {
			uint32 base = w;
			if (level == 1) {
				base = ReadMacInt32(w);
				if (!locate_ok_addr(base))
					continue;
			}
			unsigned sz = ReadMacInt16(base);
			int t = (int16)ReadMacInt16(base + 2), l = (int16)ReadMacInt16(base + 4);
			int b = (int16)ReadMacInt16(base + 6), r = (int16)ReadMacInt16(base + 8);
			if (sz < 10 || sz > 2000 || b <= t || r <= l)
				continue;
			bool inside = (t >= rt && l >= rl && b <= rb && r <= rr) ||
				      (t >= rt + bt && l >= rl + bl && b <= rb + bt && r <= rr + bl);
			bool nonrect = sz > 10;
			if (!inside || !nonrect)
				continue;
			bool dup = false;
			for (unsigned i = 0; i < nd; i++)
				dup |= done[i] == base;
			if (dup || nd >= 64)
				continue;
			done[nd++] = base;
			printf("SheepForce locate: code %u call %u region word@%03x=%08x level %d size %u bbox %d,%d,%d,%d data",
			       code, n, (unsigned)off, (unsigned)w, level, sz, t, l, b, r);
			for (unsigned i = 10; i < sz && i < 120; i += 2)
				printf(" %04x", ReadMacInt16(base + i));
			printf("\n");
		}
	}
}

/* Histogram of the operations the ROM asks for, printed every 2000 calls. */
struct probe_key { uint32 code; int mode, pen, spix, dpix; bool masked, screen; unsigned count; unsigned long long pixels; };
static probe_key probe_keys[96];
static unsigned probe_nkeys, probe_total;

static void probe_count(uint32 p, uint32 code, bool masked, bool screen)
{
	probe_key k = { code, (int)ReadMacInt32(p + acclTransferMode), (int)ReadMacInt32(p + acclPenMode),
			(int)ReadMacInt32(p + acclSrcPixelSize), (int)ReadMacInt32(p + acclDestPixelSize), masked, screen, 0, 0 };
	if (k.spix < 0 || k.spix > 32)
		k.spix = -1;		// no source: the word is stale
	unsigned i = 0;
	while (i < probe_nkeys && !(probe_keys[i].code == k.code && probe_keys[i].mode == k.mode &&
	       probe_keys[i].pen == k.pen && probe_keys[i].spix == k.spix && probe_keys[i].dpix == k.dpix &&
	       probe_keys[i].masked == k.masked && probe_keys[i].screen == k.screen))
		i++;
	if (i == probe_nkeys) {
		if (probe_nkeys >= 96)
			return;
		probe_keys[probe_nkeys++] = k;
	}
	probe_keys[i].count++;
	{
		const int aw = (int16)ReadMacInt16(p + acclDestRect + 6) - (int16)ReadMacInt16(p + acclDestRect + 2);
		const int ah = (int16)ReadMacInt16(p + acclDestRect + 4) - (int16)ReadMacInt16(p + acclDestRect + 0);
		if (aw > 0 && ah > 0)
			probe_keys[i].pixels += (unsigned long long)aw * (unsigned long long)ah;
	}
	if ((++probe_total % 2000) == 0) {
		printf("SheepForce stats after %u hook calls:\n", probe_total);
		for (unsigned j = 0; j < probe_nkeys; j++)
			if (probe_keys[j].count >= 20)
				printf("SheepForce stats:   code %u mode %d pen %d src %dbpp dst %dbpp %s %s: %u calls, %llu kpixels\n",
				       probe_keys[j].code, probe_keys[j].mode, probe_keys[j].pen, probe_keys[j].spix,
				       probe_keys[j].dpix, probe_keys[j].masked ? "masked" : "rect",
				       probe_keys[j].screen ? "screen" : "offscreen", probe_keys[j].count, probe_keys[j].pixels / 1000);
		fflush(stdout);
	}
}

bool NQD_probe_hook(uint32 p, uint32 code)
{
	static unsigned calls[16];
	shadow_flush();

	if (code >= 16)
		return false;
	const unsigned n = ++calls[code];
	if (n <= 12 || (n % 500) == 0) {
		printf("SheepForce probe: code %u call %u params %08x mode %d pen %d fore %08x back %08x"
		       " dest %08x drow %d dpix %u rect %d,%d,%d,%d src %08x srow %d spix %u\n",
		       code, n, (unsigned)p, (int)ReadMacInt32(p + acclTransferMode), (int)ReadMacInt32(p + acclPenMode),
		       (unsigned)ReadMacInt32(p + acclForePen), (unsigned)ReadMacInt32(p + acclBackPen),
		       (unsigned)ReadMacInt32(p + acclDestBaseAddr), (int)ReadMacInt32(p + acclDestRowBytes),
		       (unsigned)ReadMacInt32(p + acclDestPixelSize),
		       (int16)ReadMacInt16(p + acclDestRect + 0), (int16)ReadMacInt16(p + acclDestRect + 2),
		       (int16)ReadMacInt16(p + acclDestRect + 4), (int16)ReadMacInt16(p + acclDestRect + 6),
		       (unsigned)ReadMacInt32(p + acclSrcBaseAddr), (int)ReadMacInt32(p + acclSrcRowBytes),
		       (unsigned)ReadMacInt32(p + acclSrcPixelSize));
		if (n <= 6) {
			printf("SheepForce probe: code %u call %u dump", code, n);
			for (uint32 off = 0; off < 0x500; off += 4) {
				uint32 v = ReadMacInt32(p + off);
				if (v)
					printf(" %03x=%08x", (unsigned)off, (unsigned)v);
			}
			printf("\n");
		}
		if (n <= 6 && code != 5)
			locate_regions(p, code, n);
		fflush(stdout);
	}
	if (shadow_n < 8 && ReadMacInt32(p + acclDestPixelSize) == 32) {
		const int row = (int32)ReadMacInt32(p + acclDestRowBytes);
		const int dx = (int16)ReadMacInt16(p + acclDestRect + 2) - (int16)ReadMacInt16(p + acclDestBoundsRect + 2);
		const int dy = (int16)ReadMacInt16(p + acclDestRect + 0) - (int16)ReadMacInt16(p + acclDestBoundsRect + 0);
		const int w = (int16)ReadMacInt16(p + acclDestRect + 6) - (int16)ReadMacInt16(p + acclDestRect + 2);
		const int h = (int16)ReadMacInt16(p + acclDestRect + 4) - (int16)ReadMacInt16(p + acclDestRect + 0);
		uint8 *d = Mac2HostAddr(ReadMacInt32(p + acclDestBaseAddr));
		if (row > 0 && w > 0 && h > 0 && d) {
			nqd_shadow_op &q = shadow_ops[shadow_n++];
			q.valid = true;
			q.code = code;
			q.n = n;
			for (int i = 0; i < 0x140; i++)
				q.w[i] = ReadMacInt32(p + i * 4);
			q.dest = d + (size_t)dy * row + (size_t)dx * 4;
			q.row = row;
			q.rw = w;
			q.rh = h;
			q.dbpp = 4;
			q.cw = w > SHADOW_MAXW ? SHADOW_MAXW : w;
			q.ch = h > SHADOW_MAXH ? SHADOW_MAXH : h;
			q.mask_off = -1;
			q.has_pre = false;
			if (SheepForceEnabled())
				SheepForceSync();
			for (int y = 0; y < q.ch; y++)
				memcpy(q.pre + (size_t)y * SHADOW_MAXW * 4, q.dest + (size_t)y * row, (size_t)q.cw * 4);
			q.has_pre = true;
			for (int pi = 0; pi < 4; pi++) {
				q.fm[pi].reset();
				q.fm_rej[pi] = -1;
			}
			if (code == 3) {
				// The production decision from the same builder the hook uses, before the ROM draws; B and C are candidate rules.
				static const NqdFillPolicy pols[4] = { { false, false, false, false }, { true, false, false, false }, nqd_fm_production_policy(), { true, true, true, false } };
				NqdGuestMem gm;
				for (int pi = 0; pi < 4; pi++) {
					auto plan = std::make_shared<NqdFillmaskPlan>();
					q.fm_rej[pi] = nqd_fillmask_build(gm, p, plan.get(), pols[pi]);
					if (q.fm_rej[pi] == NQD_FM_OK)
						q.fm[pi] = plan;
					else if (plan->bad_region)
						q.fm[pi] = plan;	// kept only for its bad_region diagnostics
				}
			}
			q.gate = nqd_clip_gate(p, &q.gate_regions);
			q.gate_v[0] = q.gate;
			q.cap_n = g_gate_cap_n;
			memcpy(q.cap, g_gate_cap, sizeof q.cap);
			q.gate_v[1] = nqd_clip_gate(p, NULL, GATE_DIRECT);
			q.gate_v[2] = nqd_clip_gate(p, NULL, GATE_DIRECT | GATE_DEEP_HANDLE);
			q.gate_v[3] = nqd_clip_gate(p, NULL, GATE_DIRECT | GATE_DEEP_HANDLE | GATE_DEEP_DIRECT);

			q.ncand = 0;
			{
				const int rt = (int16)ReadMacInt16(p + acclDestRect + 0), rl = (int16)ReadMacInt16(p + acclDestRect + 2);
				const int rb = (int16)ReadMacInt16(p + acclDestRect + 4), rr = (int16)ReadMacInt16(p + acclDestRect + 6);
				if (region_collect(q, p, rt, rl, rb, rr)) {
					q.mask_off = q.cand_off[0];
					memcpy(q.mask, q.cmask[0], sizeof q.mask);
				}
			}
		}
	}
	{
		const bool scr = ReadMacInt32(p + acclDestBaseAddr) == screen_base;
		bool masked = false;
		if (shadow_n > 0) {
			const nqd_shadow_op &q = shadow_ops[shadow_n - 1];
			masked = q.valid && q.n == n && q.code == code && q.mask_off >= 0;
		}
		probe_count(p, code, masked, scr);
	}
	return false;
}

// Wait for graphics operation to finish
bool NQD_sync_hook(uint32 arg)
{
	D(bug("accl_sync_hook %08x\n", arg));
	if (SheepForceEnabled())
		SheepForceSync();
	return true;
}


/*
 *	Install Native QuickDraw acceleration hooks
 */

static void pixmap_rgb8(uint32 pm, int idx, uint8 *r, uint8 *g, uint8 *b)
{
	idx &= 255;
	const uint32 h = ReadMacInt32(pm + 42);
	if (h) {
		const uint32 ct = ReadMacInt32(h);
		if (ct) {
			const int n = (int)ReadMacInt16(ct + 6) + 1;
			if (idx < n) {
				const uint32 e = ct + 8 + (uint32)idx * 8u;
				*r = (uint8)(ReadMacInt16(e + 2) >> 8);
				*g = (uint8)(ReadMacInt16(e + 4) >> 8);
				*b = (uint8)(ReadMacInt16(e + 6) >> 8);
				return;
			}
		}
	}
	*r = mac_pal[idx].red;
	*g = mac_pal[idx].green;
	*b = mac_pal[idx].blue;
}

int NQD_copybits_expand(uint32 srcBits, uint32 dstBits, uint32 srcRect,
			uint32 dstRect, int16 mode, uint32 maskRgn)
{
	if (!srcBits || !dstBits || !srcRect || !dstRect || mode != 0 || maskRgn)
		return 0;
	const int16 srb = (int16)ReadMacInt16(srcBits + 4);
	const int16 drb = (int16)ReadMacInt16(dstBits + 4);
	if (srb >= 0 || drb >= 0)
		return 0;
	const int src_ps = (int)ReadMacInt16(srcBits + 32);
	const int dst_ps = (int)ReadMacInt16(dstBits + 32);
	const int src_bpp = bytes_per_pixel(src_ps);
	const int dst_bpp = bytes_per_pixel(dst_ps);
	if (dst_bpp != 4 || (src_bpp != 1 && src_bpp != 2))
		return 0;
	const int16 st = (int16)ReadMacInt16(srcRect + 0);
	const int16 sl = (int16)ReadMacInt16(srcRect + 2);
	const int16 sb = (int16)ReadMacInt16(srcRect + 4);
	const int16 sr = (int16)ReadMacInt16(srcRect + 6);
	const int16 dt = (int16)ReadMacInt16(dstRect + 0);
	const int16 dl = (int16)ReadMacInt16(dstRect + 2);
	const int16 db = (int16)ReadMacInt16(dstRect + 4);
	const int16 dr = (int16)ReadMacInt16(dstRect + 6);
	const int width = (int)sr - (int)sl;
	const int height = (int)sb - (int)st;
	if (width <= 0 || height <= 0 || width != (int)dr - (int)dl ||
	    height != (int)db - (int)dt)
		return 0;
	const int16 sbt = (int16)ReadMacInt16(srcBits + 6);
	const int16 sbl = (int16)ReadMacInt16(srcBits + 8);
	const int16 dbt = (int16)ReadMacInt16(dstBits + 6);
	const int16 dbl = (int16)ReadMacInt16(dstBits + 8);
	const int src_X = (int)sl - (int)sbl;
	const int src_Y = (int)st - (int)sbt;
	const int dest_X = (int)dl - (int)dbl;
	const int dest_Y = (int)dt - (int)dbt;
	const int src_row = (int)srb & 0x3fff;
	const int dst_row = (int)drb & 0x3fff;
	if (src_row < width * src_bpp || dst_row < width * dst_bpp)
		return 0;
	uint8 *src = Mac2HostAddr(ReadMacInt32(srcBits) +
				  (uint32)(src_Y * src_row + src_X * src_bpp));
	uint8 *dst = Mac2HostAddr(ReadMacInt32(dstBits) +
				  (uint32)(dest_Y * dst_row + dest_X * dst_bpp));
	if (!src || !dst)
		return 0;
	if (SheepForceEnabled())
		SheepForceFlushCPU(dst, dst_row, width * dst_bpp, height);
	nqd_mark_written(dst, width, height, dst_bpp);
	for (int y = 0; y < height; y++) {
		if (src_bpp == 1) {
			for (int x = 0; x < width; x++) {
				uint8 r, g, b;
				pixmap_rgb8(srcBits, src[x], &r, &g, &b);
				nw_fb_pack_mac32(dst + x * 4, r, g, b);
			}
		} else {
			for (int x = 0; x < width; x++) {
				const uint16 v = (uint16)((src[x * 2] << 8) | src[x * 2 + 1]);
				nw_fb_pack_mac32(dst + x * 4,
						 (uint8)(((v >> 10) & 31) * 8),
						 (uint8)(((v >> 5) & 31) * 8),
						 (uint8)((v & 31) * 8));
			}
		}
		src += src_row;
		dst += dst_row;
	}
	if (ReadMacInt32(dstBits) == screen_base)
		video_set_dirty_area(dest_X, dest_Y, width, height);
#if NW_BOOT_LOG
	static unsigned nlog;
	if (nlog < 40u) {
		nlog++;
		printf("NW-BOOT G1: copybits expand %d→32 %dx%d dest %08x\n",
		       src_ps, width, height, (unsigned)ReadMacInt32(dstBits));
		fflush(stdout);
	}
#endif
	return 1;
}

void VideoInstallAccel(void)
{
	if (!PrefsFindBool("gfxaccel") && !PrefsFindBool("sheepforce"))
		return;
	{
		extern uint32 RAMSize;
		nqd_fm_set_ram_size(RAMSize);
	}
	/*
	 * Plant the Native QuickDraw draw procs. Do not Execute68kTrap from
	 * here: PatchAfterStartup is already inside a 68k accRun.
	 */
	uint32 info = Mac_sysalloc(16);
	if (info == 0)
		return;
	WriteMacInt32(info + 0, NativeTVECT(NATIVE_NQD_BITBLT_HOOK));
	WriteMacInt32(info + 4, NativeTVECT(NATIVE_NQD_SYNC_HOOK));
	WriteMacInt32(info + 8, ACCL_BITBLT);
	NQDMisc(6, info);
	WriteMacInt32(info + 0, NativeTVECT(NATIVE_NQD_FILLRECT_HOOK));
	WriteMacInt32(info + 4, NativeTVECT(NATIVE_NQD_SYNC_HOOK));
	WriteMacInt32(info + 8, ACCL_FILLRECT);
	NQDMisc(6, info);
	if (PrefsFindBool("sheepforce_probe")) {
		// Candidate codes beyond the planted 0 and 2: the enum's masked blit (1)
		// and masked fill (3), and the stale comment's 4, 5 and 6, plus 7 and 8.
		static const uint32 codes[] = {1, 3, 4, 5, 6, 7, 8};
		for (unsigned i = 0; i < sizeof codes / sizeof codes[0]; i++) {
			uint32 tv = NativeSlotTVECT(NATIVE_NQD_PROBE_HOOK, (int)codes[i]);
			if (tv == 0)
				continue;
			WriteMacInt32(info + 0, tv);
			WriteMacInt32(info + 4, NativeTVECT(NATIVE_NQD_SYNC_HOOK));
			WriteMacInt32(info + 8, codes[i]);
			NQDMisc(6, info);
		}
		printf("SheepForce: probe hooks planted for codes 1 3 4 5 6 7 8\n");
	}
	if (SheepForceQDEnabled() && PrefsFindBool("sheepforce_lines") && !PrefsFindBool("sheepforce_probe")) {
		WriteMacInt32(info + 0, NativeTVECT(NATIVE_NQD_LINES_HOOK));
		WriteMacInt32(info + 4, NativeTVECT(NATIVE_NQD_SYNC_HOOK));
		WriteMacInt32(info + 8, ACCL_LINES);
		NQDMisc(6, info);
		printf("SheepForce: lines hook (code %d) planted\n", (int)ACCL_LINES);
	}
	if (SheepForceQDEnabled() && PrefsFindBool("sheepforce_fillmask") && !PrefsFindBool("sheepforce_probe")) {
		WriteMacInt32(info + 0, NativeTVECT(NATIVE_NQD_FILLMASK_HOOK));
		WriteMacInt32(info + 4, NativeTVECT(NATIVE_NQD_SYNC_HOOK));
		WriteMacInt32(info + 8, ACCL_FILLMASK);
		NQDMisc(6, info);
		atexit(NQD_fillmask_report);
		printf("SheepForce: fill-mask hook (code %d) planted\n", (int)ACCL_FILLMASK);
	}
	printf("SheepForce: Native QuickDraw hooks installed\n");
}
