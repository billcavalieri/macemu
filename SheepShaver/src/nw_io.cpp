/*
 *  nw_io.cpp - New World guest I/O space dispatch
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

#include <stdio.h>
#include <string.h>
#include "nw_io.h"

enum { NW_IO_MAX_DEVICES = 32, NW_IO_LOG_MAX = 64, NW_IO_PAGES_MAX = 128,	/* 7 models + 16 flash aliases */
       NW_BANKS_MAX = 8 };

static struct nw_io_device g_devs[NW_IO_MAX_DEVICES];
static int g_ndevs;
static int g_log_count;
static uint32_t g_pages[NW_IO_PAGES_MAX];
static int g_npages;
int nw_io_ext_irq;

struct nw_bank {
	int kind;
	uint32_t base;
	uint32_t size;
};
static struct nw_bank g_banks[NW_BANKS_MAX];
static int g_nbanks;

static uint32_t g_fb_base;
static uint32_t g_fb_rowbytes;
static uint32_t g_fb_w;
static uint32_t g_fb_h;
static uint32_t g_fb_bpp;
static uint32_t g_fb_tiles[(NW_FB_TILES_X * NW_FB_TILES_Y + 31) / 32];
static uint64_t g_fb_marks;
static uint64_t g_fb_upload;

static void fb_damage_reset(void)
{
	g_fb_base = 0;
	g_fb_rowbytes = 0;
	g_fb_w = 0;
	g_fb_h = 0;
	g_fb_bpp = 0;
	memset(g_fb_tiles, 0, sizeof(g_fb_tiles));
	g_fb_marks = 0;
	g_fb_upload = 0;
}

void nw_io_reset(void)
{
	g_ndevs = 0;
	g_log_count = 0;
	g_npages = 0;
	g_nbanks = 0;
	fb_damage_reset();
}

void nw_banks_set(int kind, uint32_t base, uint32_t size)
{
	if (kind <= NW_PA_NONE || kind == NW_PA_IO || size == 0)
		return;
	for (int i = 0; i < g_nbanks; i++) {
		if (g_banks[i].kind == kind) {
			g_banks[i].base = base;
			g_banks[i].size = size;
			return;
		}
	}
	if (g_nbanks >= NW_BANKS_MAX)
		return;
	g_banks[g_nbanks].kind = kind;
	g_banks[g_nbanks].base = base;
	g_banks[g_nbanks].size = size;
	g_nbanks++;
}

int nw_pa_kind(uint32_t pa)
{
	if (nw_io_range(pa))
		return NW_PA_IO;
	for (int i = 0; i < g_nbanks; i++) {
		if (pa - g_banks[i].base < g_banks[i].size)
			return g_banks[i].kind;
	}
	return NW_PA_NONE;
}

int nw_pa_writable(uint32_t pa)
{
	switch (nw_pa_kind(pa)) {
	case NW_PA_RAM:
	case NW_PA_SHEEP:
	case NW_PA_FB:
	case NW_PA_LOWMEM:
	case NW_PA_KDP:
	case NW_PA_BOOTINFO:
		return 1;
	default:
		return 0;
	}
}

static const char *bank_name(int kind)
{
	switch (kind) {
	case NW_PA_RAM: return "ram";
	case NW_PA_ROM: return "rom";
	case NW_PA_SHEEP: return "sheep";
	case NW_PA_FB: return "fb";
	case NW_PA_LOWMEM: return "lowmem";
	case NW_PA_KDP: return "kdp";
	case NW_PA_BOOTINFO: return "bootinfo";
	default: return "?";
	}
}

void nw_io_log_banks(void)
{
	for (int i = 0; i < g_nbanks; i++)
		printf("NW-BOOT G1: banks %s %08x+%x\n", bank_name(g_banks[i].kind),
		       (unsigned)g_banks[i].base, (unsigned)g_banks[i].size);
	int i = 0;
	while (i < g_ndevs) {
		const struct nw_io_device *d = &g_devs[i];
		int n = 1;
		uint32_t stride = 0;
		while (i + n < g_ndevs) {
			const struct nw_io_device *nx = &g_devs[i + n];
			if (d->name != nx->name &&
			    (!d->name || !nx->name || strcmp(d->name, nx->name) != 0))
				break;
			if (nx->size != d->size)
				break;
			uint32_t s = nx->base - g_devs[i + n - 1].base;
			if (n == 1)
				stride = s;
			else if (s != stride)
				break;
			n++;
		}
		if (n > 1)
			printf("NW-BOOT G1: banks io %s %08x+%x x%d stride %x\n",
			       d->name ? d->name : "?", (unsigned)d->base, (unsigned)d->size,
			       n, (unsigned)stride);
		else
			printf("NW-BOOT G1: banks io %s %08x+%x\n",
			       d->name ? d->name : "?", (unsigned)d->base, (unsigned)d->size);
		i += n;
	}
}

int nw_io_n_devices(void)
{
	return g_ndevs;
}

const struct nw_io_device *nw_io_device(int i)
{
	if (i < 0 || i >= g_ndevs)
		return NULL;
	return &g_devs[i];
}

int nw_io_register(const struct nw_io_device *dev)
{
	if (dev == NULL || dev->size == 0 || g_ndevs >= NW_IO_MAX_DEVICES)
		return -1;
	for (int i = 0; i < g_ndevs; i++) {
		const struct nw_io_device *d = &g_devs[i];
		if (dev->base < d->base + d->size && d->base < dev->base + dev->size)
			return -1;
	}
	g_devs[g_ndevs++] = *dev;
	return 0;
}

static const struct nw_io_device *find(uint32_t pa)
{
	for (int i = 0; i < g_ndevs; i++) {
		const struct nw_io_device *d = &g_devs[i];
		if (pa - d->base < d->size)
			return d;
	}
	return NULL;
}

/* First unclaimed touch of each 4 KiB page is always reported: this is the
 * inventory of device registers the guest expects (the gate list). */
static void log_page(char rw, uint32_t pa, int size, uint32_t value, uint32_t pc)
{
	const uint32_t page = pa & ~0xfffu;
	for (int i = 0; i < g_npages; i++)
		if (g_pages[i] == page)
			return;
	if (g_npages < NW_IO_PAGES_MAX)
		g_pages[g_npages++] = page;
	printf("NW-BOOT IO page %08x first %c%d %08x %08x %08x\n", (unsigned)page, rw, size,
	       (unsigned)pa, (unsigned)value, (unsigned)pc);
}

static void log_unclaimed(char rw, uint32_t pa, int size, uint32_t value, uint32_t pc)
{
	log_page(rw, pa, size, value, pc);
	if (g_log_count >= NW_IO_LOG_MAX)
		return;
	g_log_count++;
	/* Grammar: NW-BOOT IO <R|W><size> <pa> <value> <pc>; same line shape as
	 * the X E events so the diff tools can align on it. */
	printf("NW-BOOT IO %c%d %08x %08x %08x unclaimed\n", rw, size, (unsigned)pa,
	       (unsigned)value, (unsigned)pc);
	if (g_log_count == NW_IO_LOG_MAX)
		printf("NW-BOOT IO (further unclaimed accesses not logged)\n");
}


uint32_t nw_io_read(uint32_t pa, int size, uint32_t pc)
{
	if (nw_pa_kind(pa) != NW_PA_IO)
		return 0;
	const struct nw_io_device *d = find(pa);
	if (d && d->read)
		return d->read(d->ctx, pa - d->base, size);
	log_unclaimed('R', pa, size, 0, pc);
	return 0;
}

void nw_io_write(uint32_t pa, int size, uint32_t value, uint32_t pc)
{
	if (nw_pa_kind(pa) != NW_PA_IO)
		return;
	const struct nw_io_device *d = find(pa);
	if (d && d->write) {
		d->write(d->ctx, pa - d->base, size, value);
		return;
	}
	log_unclaimed('W', pa, size, value, pc);
}

static void fb_set_tile(unsigned tx, unsigned ty)
{
	if (tx >= NW_FB_TILES_X || ty >= NW_FB_TILES_Y)
		return;
	const unsigned i = ty * NW_FB_TILES_X + tx;
	g_fb_tiles[i >> 5] |= 1u << (i & 31u);
}

void nw_fb_damage_layout(uint32_t base, uint32_t rowbytes, uint32_t width,
			 uint32_t height, uint32_t bpp)
{
	const int had = g_fb_w != 0;
	g_fb_base = base;
	g_fb_rowbytes = rowbytes;
	g_fb_w = width;
	g_fb_h = height;
	g_fb_bpp = bpp ? bpp : 4;
	memset(g_fb_tiles, 0, sizeof(g_fb_tiles));
	/* Mode change: the new buffer must be uploaded. First layout
	 * leaves tiles clear so we do not present a black frame. */
	if (had && width && height)
		nw_fb_damage_rect(0, 0, (int)width, (int)height);
}

void nw_fb_damage_rect(int x, int y, int w, int h)
{
	if (!g_fb_w || !g_fb_h || w <= 0 || h <= 0)
		return;
	int x1 = x + w - 1;
	int y1 = y + h - 1;
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (x1 >= (int)g_fb_w)
		x1 = (int)g_fb_w - 1;
	if (y1 >= (int)g_fb_h)
		y1 = (int)g_fb_h - 1;
	if (x1 < x || y1 < y)
		return;
	const int tx0 = x / NW_FB_TILE;
	const int ty0 = y / NW_FB_TILE;
	const int tx1 = x1 / NW_FB_TILE;
	const int ty1 = y1 / NW_FB_TILE;
	for (int ty = ty0; ty <= ty1; ty++)
		for (int tx = tx0; tx <= tx1; tx++)
			fb_set_tile((unsigned)tx, (unsigned)ty);
	g_fb_marks++;
}

void nw_fb_damage_pixmap(uint32_t dest_base, int x, int y, int w, int h)
{
	if (!g_fb_rowbytes || !g_fb_bpp || !g_fb_w || w <= 0 || h <= 0)
		return;
	const uint32_t fb_bytes = g_fb_rowbytes * g_fb_h;
	if (dest_base < g_fb_base || dest_base - g_fb_base >= fb_bytes)
		return;
	const uint32_t off = dest_base - g_fb_base;
	const int ox = (int)((off % g_fb_rowbytes) / g_fb_bpp);
	const int oy = (int)(off / g_fb_rowbytes);
	nw_fb_damage_rect(ox + x, oy + y, w, h);
}

void nw_fb_damage_store(uint32_t pa, unsigned nbytes)
{
	if (!g_fb_rowbytes || !g_fb_bpp || !g_fb_w)
		return;
	if (pa < g_fb_base)
		return;
	const uint32_t off = pa - g_fb_base;
	const uint32_t fb_bytes = g_fb_rowbytes * g_fb_h;
	if (off >= fb_bytes)
		return;
	uint32_t last = off;
	if (nbytes)
		last = off + nbytes - 1;
	if (last >= fb_bytes)
		last = fb_bytes - 1;
	const int y0 = (int)(off / g_fb_rowbytes);
	const int x0 = (int)((off % g_fb_rowbytes) / g_fb_bpp);
	const int y1 = (int)(last / g_fb_rowbytes);
	const int x1 = (int)((last % g_fb_rowbytes) / g_fb_bpp);
	nw_fb_damage_rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
}

int nw_fb_damage_any(void)
{
	for (unsigned i = 0; i < sizeof(g_fb_tiles) / sizeof(g_fb_tiles[0]); i++) {
		if (g_fb_tiles[i])
			return 1;
	}
	return 0;
}

static int fb_collect_from(const uint32_t *bits, int *x, int *y, int *w, int *h, int max)
{
	if (!x || !y || !w || !h || max <= 0 || !g_fb_w || !bits)
		return 0;
	int n = 0;
	const unsigned ntx = (g_fb_w + NW_FB_TILE - 1) / NW_FB_TILE;
	const unsigned nty = (g_fb_h + NW_FB_TILE - 1) / NW_FB_TILE;
	for (unsigned ty = 0; ty < nty && n < max; ty++) {
		for (unsigned tx = 0; tx < ntx && n < max; tx++) {
			const unsigned i = ty * NW_FB_TILES_X + tx;
			if (!(bits[i >> 5] & (1u << (i & 31u))))
				continue;
			int tw = (int)NW_FB_TILE;
			int th = (int)NW_FB_TILE;
			if ((unsigned)(tx * NW_FB_TILE + tw) > g_fb_w)
				tw = (int)g_fb_w - (int)(tx * NW_FB_TILE);
			if ((unsigned)(ty * NW_FB_TILE + th) > g_fb_h)
				th = (int)g_fb_h - (int)(ty * NW_FB_TILE);
			x[n] = (int)(tx * NW_FB_TILE);
			y[n] = (int)(ty * NW_FB_TILE);
			w[n] = tw;
			h[n] = th;
			n++;
		}
	}
	return n;
}

int nw_fb_damage_collect(int *x, int *y, int *w, int *h, int max)
{
	return fb_collect_from(g_fb_tiles, x, y, w, h, max);
}

int nw_fb_damage_take(int *x, int *y, int *w, int *h, int max)
{
	uint32_t snap[sizeof(g_fb_tiles) / sizeof(g_fb_tiles[0])];
	memcpy(snap, g_fb_tiles, sizeof(snap));
	memset(g_fb_tiles, 0, sizeof(g_fb_tiles));
	return fb_collect_from(snap, x, y, w, h, max);
}

void nw_fb_damage_clear(void)
{
	memset(g_fb_tiles, 0, sizeof(g_fb_tiles));
}

void nw_fb_damage_note_upload(uint64_t bytes)
{
	g_fb_upload += bytes;
}

uint64_t nw_fb_damage_upload_bytes(void)
{
	return g_fb_upload;
}

uint64_t nw_fb_damage_marks(void)
{
	return g_fb_marks;
}
