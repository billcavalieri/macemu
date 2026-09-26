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
	if (SheepForceEnabled() && SheepForceOwns(ReadMacInt32(p + acclDestBaseAddr)) &&
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
	if (SheepForceEnabled() && SheepForceOwns(ReadMacInt32(p + acclDestBaseAddr)) &&
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
	if (!down && src_bpp == dst_bpp && SheepForceEnabled() &&
	    SheepForceOwns(ReadMacInt32(p + acclDestBaseAddr)) &&
	    SheepForceTryBlit(dst, src, src_bpp, dst_row_bytes, src_row_bytes, width * src_bpp, height))
		return;
	if (SheepForceEnabled())
		SheepForceFlushCPU(dst0, dst_row_bytes, width * dst_bpp, height);
	const int sstep = down ? -src_row_bytes : src_row_bytes;
	const int dstep = down ? -dst_row_bytes : dst_row_bytes;
	if (src_bpp == dst_bpp) {
		const int span = width * src_bpp;
		for (int i = 0; i < height; i++) {
			memmove(dst, src, (size_t)span);
			src += sstep;
			dst += dstep;
		}
		return;
	}
	if (dst_bpp != 4 || (src_bpp != 1 && src_bpp != 2))
		return;
	for (int i = 0; i < height; i++) {
		if (src_bpp == 1) {
			for (int x = 0; x < width; x++)
				nw_fb_pack_mac32(dst + x * 4, mac_pal[src[x]].red,
						 mac_pal[src[x]].green, mac_pal[src[x]].blue);
		} else {
			for (int x = 0; x < width; x++) {
				const uint16 v = (uint16)((src[x * 2] << 8) | src[x * 2 + 1]);
				const uint8 r = (uint8)(((v >> 10) & 31) * 8);
				const uint8 g = (uint8)(((v >> 5) & 31) * 8);
				const uint8 b = (uint8)((v & 31) * 8);
				nw_fb_pack_mac32(dst + x * 4, r, g, b);
			}
		}
		src += sstep;
		dst += dstep;
	}
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
		ReadMacInt32(p + acclTransferMode) == 0 &&
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
	printf("SheepForce: Native QuickDraw hooks installed\n");
}
