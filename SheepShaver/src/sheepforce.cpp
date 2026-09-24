/*
 *  sheepforce.cpp - SheepForce page geometry. Metal lives in sheepforce_metal.mm.
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
#include "sysdeps.h"
#include "sheepforce.h"
#include "prefs.h"
#include "cpu_emulation.h"

static uint8 *g_host;
static uint32 g_mac;
static uint32 g_page_bytes;
static int g_w, g_h, g_row, g_depth;
static int g_visible;

bool SheepForceEnabled(void)
{
	return PrefsFindBool("sheepforce");
}

int SheepForcePageCount(void)
{
	return SheepForceEnabled() ? 2 : 1;
}

uint32 SheepForcePageBytes(void)
{
	if (g_page_bytes)
		return g_page_bytes;
	if (g_row > 0 && g_h > 0)
		return (uint32)g_row * (uint32)g_h;
	return 0;
}

void SheepForceSetGeometry(uint8 *host, uint32 mac_base, uint32 page_bytes,
			   int width, int height, int rowbytes, int depth_bits)
{
	g_host = host;
	g_mac = mac_base;
	g_page_bytes = page_bytes;
	g_w = width;
	g_h = height;
	g_row = rowbytes;
	g_depth = depth_bits;
	if (g_visible >= SheepForcePageCount())
		g_visible = 0;
}

uint32 SheepForcePageMac(int page)
{
	if (page < 0 || page >= SheepForcePageCount() || g_mac == 0)
		return g_mac;
	return g_mac + (uint32)page * SheepForcePageBytes();
}

uint8 *SheepForcePageHost(int page)
{
	if (!g_host || page < 0 || page >= SheepForcePageCount())
		return g_host;
	return g_host + (uint32)page * SheepForcePageBytes();
}

int SheepForceVisiblePage(void)
{
	return g_visible;
}

void SheepForceSetVisiblePage(int page)
{
	if (page < 0)
		page = 0;
	if (page >= SheepForcePageCount())
		page = SheepForcePageCount() - 1;
	g_visible = page;
}

int SheepForceWidth(void) { return g_w; }
int SheepForceHeight(void) { return g_h; }
int SheepForceRowBytes(void) { return g_row; }
int SheepForceDepth(void) { return g_depth; }

bool SheepForceOwns(uint32 mac_addr)
{
	if (!SheepForceEnabled() || g_mac == 0 || mac_addr < g_mac)
		return false;
	uint32 bytes = SheepForcePageBytes() * (uint32)SheepForcePageCount();
	return mac_addr < g_mac + bytes;
}

#if !defined(__APPLE__)
void SheepForceStartup(void *)
{
	if (SheepForceEnabled()) {
		printf("NW-BOOT SheepForce by Bill Cavalieri\n");
		fflush(stdout);
	}
}
void SheepForceShutdown(void) {}
void SheepForceSync(void) {}
bool SheepForcePresent(int, int, int, int) { return false; }
bool SheepForceTryFill(uint8 *, int, int, int, int, uint32) { return false; }
bool SheepForceTryInvert(uint8 *, int, int, int, int) { return false; }
bool SheepForceTryBlit(uint8 *, const uint8 *, int, int, int, int, int) { return false; }
int SheepForceRaveTriangle(uint8 *pixmap, int width, int height, int rowbytes, int depth_bits,
			   float x0, float y0, float x1, float y1, float x2, float y2,
			   uint8_t r, uint8_t g, uint8_t b)
{
	(void)pixmap; (void)width; (void)height; (void)rowbytes; (void)depth_bits;
	(void)x0; (void)y0; (void)x1; (void)y1; (void)x2; (void)y2;
	(void)r; (void)g; (void)b;
	return -1;
}
void SheepForceRaveSync(void) {}
int32 SheepForceRaveDispatch(uint32 mac_params)
{
	(void)mac_params;
	return -1;
}
#endif
