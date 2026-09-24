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
#include <stdlib.h>
#include <string.h>
#include "nw_io.h"
#include "nw_jit.h"

enum { NW_IO_MAX_DEVICES = 32, NW_IO_LOG_MAX = 64, NW_IO_PAGES_MAX = 128,	/* 7 models + 16 flash aliases */
       NW_BANKS_MAX = 8 };

static struct nw_io_device g_devs[NW_IO_MAX_DEVICES];
static int g_ndevs;
static int g_log_count;
static uint32_t g_pages[NW_IO_PAGES_MAX];
static int g_npages;
int nw_io_ext_irq;
static uint32_t g_io_pc;

uint32_t nw_io_last_pc(void)
{
	return g_io_pc;
}

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
enum { NW_FB_RECTS = 48 };
static int g_fb_rx[NW_FB_RECTS], g_fb_ry[NW_FB_RECTS], g_fb_rw[NW_FB_RECTS], g_fb_rh[NW_FB_RECTS];
static int g_fb_nrects;
static uint64_t g_fb_marks;
static uint64_t g_fb_upload;
static uint32_t g_fps_hash;
static uint64_t g_fps_frames, g_fps_frames_sec;
static uint64_t g_fps_upload_mark;
static unsigned g_fps_flat, g_fps_flat_max;
static int g_fps_have;

static void fb_damage_reset(void)
{
	g_fb_base = 0;
	g_fb_rowbytes = 0;
	g_fb_w = 0;
	g_fb_h = 0;
	g_fb_bpp = 0;
	memset(g_fb_tiles, 0, sizeof(g_fb_tiles));
	g_fb_nrects = 0;
	g_fb_marks = 0;
	g_fb_upload = 0;
	g_fps_hash = 0;
	g_fps_frames = g_fps_frames_sec = 0;
	g_fps_upload_mark = 0;
	g_fps_flat = g_fps_flat_max = 0;
	g_fps_have = 0;
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
	g_io_pc = pc;
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
	g_io_pc = pc;
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
	g_fb_nrects = 0;
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
	const int rw = x1 - x + 1;
	const int rh = y1 - y + 1;
	/* CopyBits-sized rects stay exact so a movie blit does not pull
	 * neighbouring chrome into a 64-pixel tile. Pixel stores still
	 * use tiles. */
	if (rw >= 8 && rh >= 8 && g_fb_nrects < NW_FB_RECTS) {
		g_fb_rx[g_fb_nrects] = x;
		g_fb_ry[g_fb_nrects] = y;
		g_fb_rw[g_fb_nrects] = rw;
		g_fb_rh[g_fb_nrects] = rh;
		g_fb_nrects++;
		g_fb_marks++;
		return;
	}
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
	nw_movie_scale_note_store(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
	const int w = x1 - x0 + 1;
	/* Same-row: one rect. Cross-row stores have x1 < x0 so w <= 0
	 * and a single rect marks nothing; cover each row's span. */
	if (w > 0 && y1 == y0) {
		nw_fb_damage_rect(x0, y0, w, 1);
		return;
	}
	if (y1 > y0) {
		nw_fb_damage_rect(x0, y0, (int)g_fb_w - x0, 1);
		for (int y = y0 + 1; y < y1; y++)
			nw_fb_damage_rect(0, y, (int)g_fb_w, 1);
		nw_fb_damage_rect(0, y1, x1 + 1, 1);
	}
}

int nw_fb_damage_any(void)
{
	if (g_fb_nrects)
		return 1;
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
	if (g_fb_nrects > 0 && x && y && w && h && max > 0) {
		const int n = g_fb_nrects < max ? g_fb_nrects : max;
		for (int i = 0; i < n; i++) {
			x[i] = g_fb_rx[i];
			y[i] = g_fb_ry[i];
			w[i] = g_fb_rw[i];
			h[i] = g_fb_rh[i];
		}
		return n;
	}
	return fb_collect_from(g_fb_tiles, x, y, w, h, max);
}

int nw_fb_damage_take(int *x, int *y, int *w, int *h, int max)
{
	if (g_fb_nrects > 0 && x && y && w && h && max > 0) {
		const int n = g_fb_nrects < max ? g_fb_nrects : max;
		for (int i = 0; i < n; i++) {
			x[i] = g_fb_rx[i];
			y[i] = g_fb_ry[i];
			w[i] = g_fb_rw[i];
			h[i] = g_fb_rh[i];
		}
		g_fb_nrects = 0;
		memset(g_fb_tiles, 0, sizeof(g_fb_tiles));
		return n;
	}
	uint32_t snap[sizeof(g_fb_tiles) / sizeof(g_fb_tiles[0])];
	memcpy(snap, g_fb_tiles, sizeof(snap));
	memset(g_fb_tiles, 0, sizeof(g_fb_tiles));
	g_fb_nrects = 0;
	return fb_collect_from(snap, x, y, w, h, max);
}

void nw_fb_damage_clear(void)
{
	memset(g_fb_tiles, 0, sizeof(g_fb_tiles));
	g_fb_nrects = 0;
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

static int g_fb_arm;
static uint8_t *g_fb_host;
static uint32_t g_fb_host_bytes;
static uint32_t g_fb_pages[32];

void nw_fb_bind_host(uint8_t *base, uint32_t bytes)
{
	if (!base || bytes < 4)
		return;
	g_fb_host = base;
	g_fb_host_bytes = bytes;
}

uint32_t nw_fb_phys(uint32_t *bytes)
{
	if (bytes)
		*bytes = g_fb_rowbytes * g_fb_h;
	return g_fb_base;
}

void nw_fb_arm(void)
{
	g_fb_arm = 1;
}

void nw_fb_note_host(const uint8_t *p)
{
	if (!g_fb_host || p < g_fb_host)
		return;
	const uint32_t off = (uint32_t)(p - g_fb_host);
	if (off >= g_fb_host_bytes)
		return;
	const unsigned page = off >> 12;
	if (page < 1024u)
		g_fb_pages[page >> 5] |= 1u << (page & 31u);
	g_fb_arm = 1;
}

void nw_fb_commit(void)
{
	if (!g_fb_arm || !g_fb_w || !g_fb_rowbytes || !g_fb_bpp)
		return;
	g_fb_arm = 0;
	const int row_pages = (int)((g_fb_rowbytes + 4095u) >> 12);
	for (unsigned page = 0; page < 1024u; page++) {
		if ((g_fb_pages[page >> 5] & (1u << (page & 31u))) == 0)
			continue;
		g_fb_pages[page >> 5] &= ~(1u << (page & 31u));
		const uint32_t off = page << 12;
		const int y = (int)(off / g_fb_rowbytes);
		if (y >= (int)g_fb_h)
			continue;
		int h = row_pages > 0 ? (int)(4096u / g_fb_rowbytes) : 1;
		if (h < 1)
			h = 1;
		if (y + h > (int)g_fb_h)
			h = (int)g_fb_h - y;
		nw_fb_damage_rect(0, y, (int)g_fb_w, h);
	}
}

void nw_fb_fps_proxy_sample(const uint8_t *fb, uint32_t pitch, uint32_t w, uint32_t h)
{
	if (!fb || w < 32 || h < 32 || pitch == 0)
		return;
	const uint32_t x0 = w / 4u;
	const uint32_t y0 = h / 6u;
	const uint32_t rw = w / 2u;
	const uint32_t rh = h / 2u;
	const unsigned bpp = (w && pitch / w >= 1u) ? (unsigned)(pitch / w) : 4u;
	uint32_t hash = 2166136261u;
	for (uint32_t y = y0; y < y0 + rh && y < h; y += 4u) {
		const uint8_t *row = fb + (size_t)y * pitch + (size_t)x0 * bpp;
		for (uint32_t x = 0; x < rw && x0 + x < w; x += 4u) {
			const uint8_t *p = row + (size_t)x * bpp;
			hash ^= p[0]; hash *= 16777619u;
			if (bpp > 1) { hash ^= p[1]; hash *= 16777619u; }
			if (bpp > 2) { hash ^= p[2]; hash *= 16777619u; }
		}
	}
	if (g_fps_have && hash != g_fps_hash) {
		g_fps_frames++;
		g_fps_frames_sec++;
	}
	g_fps_hash = hash;
	g_fps_have = 1;
}

void nw_fb_fps_proxy_tick(void)
{
	const uint64_t up = g_fb_upload;
	const uint64_t dbytes = (up >= g_fps_upload_mark) ? (up - g_fps_upload_mark) : 0;
	g_fps_upload_mark = up;
	if (g_fps_frames_sec == 0)
		g_fps_flat++;
	else {
		if (g_fps_flat > g_fps_flat_max)
			g_fps_flat_max = g_fps_flat;
		g_fps_flat = 0;
	}
	if (nw_jit_stats_wanted()) {
		printf("NW-BOOT G1: qt_fps_proxy frames=%llu dbytes=%llu hash=%08x flat=%u\n",
		       (unsigned long long)g_fps_frames_sec,
		       (unsigned long long)dbytes,
		       (unsigned)g_fps_hash,
		       g_fps_flat);
		fflush(stdout);
	}
	g_fps_frames_sec = 0;
}

uint64_t nw_fb_fps_proxy_frames(void)
{
	return g_fps_frames;
}

unsigned nw_fb_fps_proxy_flat_max(void)
{
	return g_fps_flat_max;
}

void nw_fb_mac32_rgb(const uint8_t *px, uint8_t *r, uint8_t *g, uint8_t *b)
{
	if (!px)
		return;
	if (r)
		*r = px[1];
	if (g)
		*g = px[2];
	if (b)
		*b = px[3];
}

enum { NW_MOVIE_BANDS = 64, NW_MOVIE_BYTES = 2 * 1024 * 1024 };

struct nw_movie_band {
	int dx, dy, dw, dh, sw, sh, stride, off;
};

static uint8_t *g_movie;
static int g_movie_used, g_movie_n;
static struct nw_movie_band g_movie_b[NW_MOVIE_BANDS];

int nw_movie_scale_put(const uint8_t *src, int src_stride, int sw, int sh,
		       int dx, int dy, int dw, int dh)
{
	if (!src || sw < 1 || sh < 1 || dw < 1 || dh < 1 || sw > 2048 || sh > 2048)
		return 0;
	const int row = sw * 4;
	const int nbytes = row * sh;
	/* A band that starts above the previous one is the next frame. */
	if (g_movie_n > 0 && dy + 8 < g_movie_b[g_movie_n - 1].dy) {
		g_movie_n = 0;
		g_movie_used = 0;
	}
	if (g_movie_n >= NW_MOVIE_BANDS || g_movie_used > NW_MOVIE_BYTES - nbytes) {
		g_movie_n = 0;
		g_movie_used = 0;
	}
	if (!g_movie) {
		g_movie = (uint8_t *)malloc(NW_MOVIE_BYTES);
		if (!g_movie)
			return 0;
	}
	uint8_t *dst = g_movie + g_movie_used;
	const int step = src_stride < 0 ? -src_stride : src_stride;
	if (src_stride < 0)
		src += (size_t)(sh - 1) * (size_t)step;
	for (int y = 0; y < sh; y++) {
		memcpy(dst + (size_t)y * (size_t)row, src, (size_t)row);
		src += step;
	}
	g_movie_b[g_movie_n].dx = dx;
	g_movie_b[g_movie_n].dy = dy;
	g_movie_b[g_movie_n].dw = dw;
	g_movie_b[g_movie_n].dh = dh;
	g_movie_b[g_movie_n].sw = sw;
	g_movie_b[g_movie_n].sh = sh;
	g_movie_b[g_movie_n].stride = row;
	g_movie_b[g_movie_n].off = g_movie_used;
	g_movie_n++;
	g_movie_used += nbytes;
	return 1;
}

int nw_movie_scale_count(void)
{
	return g_movie_n;
}

int nw_movie_scale_band(int i, int *dx, int *dy, int *dw, int *dh,
			int *sw, int *sh, const uint8_t **px, int *stride)
{
	if (i < 0 || i >= g_movie_n || !g_movie)
		return 0;
	const struct nw_movie_band *b = &g_movie_b[i];
	*dx = b->dx;
	*dy = b->dy;
	*dw = b->dw;
	*dh = b->dh;
	*sw = b->sw;
	*sh = b->sh;
	*px = g_movie + b->off;
	*stride = b->stride;
	return 1;
}

void nw_movie_scale_note_store(int x, int y, int w, int h)
{
	if (g_movie_n <= 0 || w <= 0 || h <= 0)
		return;
	const int x1 = x + w;
	const int y1 = y + h;
	for (int i = 0; i < g_movie_n; i++) {
		const struct nw_movie_band *b = &g_movie_b[i];
		if (x < b->dx + b->dw && x1 > b->dx && y < b->dy + b->dh && y1 > b->dy) {
			g_movie_n = 0;
			g_movie_used = 0;
			return;
		}
	}
}

void nw_fb_pack_mac32(uint8_t *px, uint8_t r, uint8_t g, uint8_t b)
{
	if (!px)
		return;
	px[0] = 0;
	px[1] = r;
	px[2] = g;
	px[3] = b;
}

void nw_fb_expand_clut8_to_mac32(uint8_t *dst32, const uint8_t *src8, int npix,
				 const uint8_t pal_rgb[256 * 3])
{
	if (!dst32 || !src8 || !pal_rgb || npix <= 0)
		return;
	for (int i = 0; i < npix; i++) {
		const unsigned c = src8[i];
		nw_fb_pack_mac32(dst32 + (size_t)i * 4u,
				 pal_rgb[c * 3u], pal_rgb[c * 3u + 1u],
				 pal_rgb[c * 3u + 2u]);
	}
}
