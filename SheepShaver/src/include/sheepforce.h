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

extern void SheepForceStartup(void *sdl_window);
extern void SheepForceShutdown(void);
extern void SheepForceSync(void);
extern bool SheepForcePresent(int x, int y, int w, int h);

extern bool SheepForceTryFill(uint8 *dest, int bpp, int rowbytes, int width_bytes, int height, uint32 color);
extern bool SheepForceTryInvert(uint8 *dest, int bpp, int rowbytes, int width_bytes, int height);
extern bool SheepForceTryBlit(uint8 *dest, const uint8 *src, int bpp, int dst_row, int src_row,
			      int width_bytes, int height);

/* Draw one Gouraud triangle into the caller's pixmap. Returns 0 on success. */
extern int SheepForceRaveTriangle(uint8 *pixmap, int width, int height, int rowbytes, int depth_bits,
				  float x0, float y0, float x1, float y1, float x2, float y2,
				  uint8_t r, uint8_t g, uint8_t b);
extern void SheepForceRaveSync(void);

struct SheepForceRaveCall {
	uint32 pixmap;
	int16 width, height;
	int16 rowbytes;
	int16 depth;
	int16 selector;		/* 1 draw, 2 sync, 3 state */
	float x0, y0, x1, y1, x2, y2;
	uint8 r, g, b, a;
	uint32 texture;
	int16 scissor_x, scissor_y, scissor_w, scissor_h;
	uint16 flags;		/* bit0 depth, bit1 blend, bit2 alpha, bit3 fog */
};

extern int32 SheepForceRaveDispatch(uint32 mac_params);

#endif
