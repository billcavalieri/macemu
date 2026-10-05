/*
 *  sheepforce.h - SheepForce display: pages, Metal scanout, RAVE, QuickDraw hooks
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
#ifndef SHEEPFORCE_H
#define SHEEPFORCE_H

#include "sysdeps.h"

extern bool SheepForceEnabled(void);
/* Per-feature kill switches; both imply SheepForceEnabled(). */
extern bool SheepForceQDEnabled(void);
extern bool SheepForceRaveEnabled(void);
extern int SheepForcePageCount(void);
extern uint32 SheepForcePageBytes(void);
extern uint32 SheepForcePageMac(int page);
extern int SheepForceVisiblePage(void);
extern void SheepForceSetVisiblePage(int page);
extern void SheepForceSetGeometry(uint8 *host, uint32 mac_base, uint32 page_bytes,
				  int width, int height, int rowbytes, int depth_bits);
extern uint8 *SheepForcePageHost(int page);
extern int SheepForceWidth(void);
extern int SheepForceHeight(void);
extern int SheepForceRowBytes(void);
extern int SheepForceDepth(void);
extern bool SheepForceOwns(uint32 mac_addr);
extern bool SheepForceAdoptHostFB(uint8 *host, uint32 bytes);

extern void SheepForceStartup(void *sdl_window);
extern void SheepForceShutdown(void);
extern void SheepForceSync(void);
/* Wait for an in-flight GPU write that overlaps this CPU rectangle.
 * A null dest waits for whatever is pending. */
extern void SheepForceFlushCPU(uint8 *dest, int rowbytes, int width_bytes, int height);
extern void SheepForceLayoutDisplay(void);
extern void SheepForceMarkDirty(void);
extern void SheepForceLoadPalette(void);
extern bool SheepForcePresent(int x, int y, int w, int h);
/* Hash of the visible page the last present handed the GPU, plus the
 * palette. *have is 0 until a present has a page to hash. */
extern uint32 SheepForcePresentedHash(int *have);

extern bool SheepForceTryFill(uint8 *dest, int bpp, int rowbytes, int width_bytes, int height, uint32 color);
/* Fill clipped to per-row spans. Pixels are 32-bit; fore/back are the guest words as SheepForceTryFill takes
 * them. pat is an 8x8 one-bit pattern (bit 7 of pat[0] is the top-left tile pixel; set bits take fore,
 * clear bits take back, so all 0xff is a solid fore fill). (pat_ox, pat_oy) is the tile coordinate of
 * the rectangle's top-left pixel. row_start has height + 1 entries indexing pairs of x0, x1 in runs
 * (rectangle-relative, [x0, x1)). A false return means "not handled". */
struct SheepForceSpanFill {
	uint8 *dest;
	int rowbytes;
	int width, height;
	uint32 fore, back;
	uint8 pat[8];
	int pat_ox, pat_oy;
	const int32_t *row_start;
	const int32_t *runs;
};
extern bool SheepForceTryFillSpans(const SheepForceSpanFill *op);
/* The same clip, painted from a pixel-pattern tile of tile_w x tile_h 32-bit words (memory order, as fore/back above).
 * Rectangle pixel (x, y) takes tile[(y + tile_oy) % tile_h][(x + tile_ox) % tile_w]. */
struct SheepForceTileFill {
	uint8 *dest;
	int rowbytes;
	int width, height;
	const uint32 *tile;
	int tile_w, tile_h;
	int tile_ox, tile_oy;
	const int32_t *row_start;
	const int32_t *runs;
};
extern bool SheepForceTryFillTile(const SheepForceTileFill *op);
extern bool SheepForceTryInvert(uint8 *dest, int bpp, int rowbytes, int width_bytes, int height);
/* One QuickDraw blit. Pixel counts are in pixels, strides are positive byte counts,
 * bpp is bytes per pixel. mode is the QuickDraw transfer mode (0-7, 34, 36-39
 * are implemented). back_word is the background pen as a Mac 32-bit pixel word as it
 * lies in memory (byte 0 pad, then R, G, B) read as a host uint32; pal holds 256 such
 * words for 8-bit sources. A false return means "not handled": the caller runs
 * the CPU path. */
struct SheepForceBlitOp {
	uint8 *dest;
	const uint8 *src;
	int dbpp, sbpp;
	int dst_row, src_row;
	int width, height;
	int mode;
	uint32 back_word;
	const uint32 *pal;
};
extern bool SheepForceTryBlit(const SheepForceBlitOp *op);

/* RAVE draw-context rendering. Vertices are in pixel space as RAVE defines them. */
struct RaveVertex {
	float x, y, z, invW, r, g, b, a, uow, vow, kd[3], ks[3];
	float uow2, vow2, invW2;	/* the second texture layer */
	float pad;
};
struct RaveBatch {
	uint32 first, count;	/* vertex range, three vertices per triangle */
	uint8 zfunc;		/* kQAZFunction_xxx; kQAZFunction_None disables the depth test */
	uint8 zwrite;		/* write the depth buffer */
	uint8 blend;		/* kQABlend_xxx */
	uint8 texop;		/* kQATextureOp_xxx mask */
	uint8 filter;		/* kQATextureFilter_xxx */
	uint8 clamp;		/* clamp texture coordinates instead of wrapping */
	uint8 fogmode;		/* kQAFogMode_xxx */
	uint8 pad;
	void *tex;		/* texture from SheepForceRaveTexNew, or NULL */
	void *tex2;		/* second layer, or NULL */
	uint8 mtop, mtfilter, mtclamp, mtpad;	/* kQAMultiTexture_xxx, filter, clamp */
	float mtfactor;		/* kQAMultiTexture_Fixed blend factor */
	float fog[8];		/* colour a, r, g, b (as the tags), then start, end, density, max depth */
};
/* A texture: levels of 8-bit RGBA, level 0 first, each w[i] x h[i] with row stride 4 * w[i]. */
extern void *SheepForceRaveTexNew(int levels, const int *w, const int *h, const uint8 *const *rgba);
extern void SheepForceRaveTexDelete(void *tex);
extern void *SheepForceRaveCtxNew(int width, int height, bool depth);
extern void SheepForceRaveCtxDelete(void *ctx);
/* Clear colour (rgba[0] = alpha, then r, g, b, as RAVE's tags) and depth at the start of the next frame. */
extern void SheepForceRaveCtxClear(void *ctx, const float rgba[4], bool clear_depth, float depth, int antialias);
extern void SheepForceRaveCtxDraw(void *ctx, const RaveBatch *batch, const RaveVertex *vertices);
/* Render everything queued, wait, and write [x0,x1) x [y0,y1) into guest pixels
 * (pixel_type is a kQAPixel_xxx value) at dst. mask (context-sized, may be NULL) limits which
 * pixels are written. Returns false if Metal is unavailable. */
extern bool SheepForceRaveCtxResolve(void *ctx, uint8 *dst, int row_bytes, int pixel_type,
				     int x0, int y0, int x1, int y1, const uint8 *mask);
/* One guest call to a RAVE callback, in PowerPC ABI order. */
struct RaveGuestCall {
	uint32 r[8];		/* r3..r10 */
	double f[13];		/* f1..f13 */
	uint32 sp;		/* r1: further integer arguments are in the caller's frame */
};
struct RaveGuestResult {
	uint32 r3;
	double f1;
	bool is_float;
};
extern void SheepForceRaveMethod(uint32 slot, const RaveGuestCall *call, RaveGuestResult *res);
extern bool SheepForceRaveSelfTest(void);
extern void SheepForceRaveDisableEngine(void);
extern void SheepForceRaveRegisterEngine(uint32 register_engine_tvect, uint32 register_draw_method_tvect);
/* Runs as a native op (ExecuteNative(NATIVE_RAVE_REGISTER)): talks to the guest's RAVE manager. */
extern void SheepForceRaveRegisterNative(void);
/* Debug (sheepforce_probe): look for the RAVE manager's export names in guest RAM. */
extern bool SheepForceRaveScanRAM(void);
extern int SheepForceRaveProbeTick(void);



#endif
