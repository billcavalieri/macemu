/*
 *  nw_io.h - New World guest I/O space (mac99-style physical layout)
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

#ifndef NW_IO_H
#define NW_IO_H

#include <stdint.h>

/*
 * Under the New World NanoKernel the guest's physical address space is the
 * mac99 one: RAM, the ROM copy, and two I/O segments that the ConfigInfo
 * PMDT maps 1:1 as cache-inhibited/guarded (attr 0x3a): 0x80000000..
 * 0x8fffffff (mac-io: VIA-PMU 0x80016000, SCC 0x80012000/13000, OpenPIC
 * 0x80040000, ...) and 0xf0000000..0xffffffff (uni-north, ROM window).
 * SheepShaver identity-maps guest memory onto host addresses, so those PAs
 * are not memory: loads and stores that translate into them are routed here
 * to device models instead of being dereferenced.
 *
 * WP4: nw_pa_kind() is the one physical decode. RAM/ROM/SheepMem/FB/KDP
 * hits are pointer math (vm_*); I/O hits the trap table below; everything
 * else is the unclaimed default. ROM is not writable (a store is dropped).
 * NW_PA_FB is the bank WP5 marks for 64-pixel damage tiles.
 */
enum {
	NW_PA_NONE = 0,
	NW_PA_RAM,
	NW_PA_ROM,
	NW_PA_SHEEP,
	NW_PA_FB,
	NW_PA_LOWMEM,
	NW_PA_KDP,
	NW_PA_BOOTINFO,
	NW_PA_IO
};

enum {
	NW_IO_MACIO_BASE = 0x80000000u,
	NW_IO_MACIO_SIZE = 0x10000000u,
	NW_IO_HIGH_BASE = 0xf0000000u,
	NW_IO_MACIO_GPIO_BASE = 0x80000050u,		/* Keylargo GPIO: 8 level bytes, 36 pin registers */
	NW_IO_MACIO_GPIO_SIZE = 0x30u,
	NW_IO_VIA_PMU_BASE = 0x80016000u,		/* 6522 registers at stride 0x200 */
	NW_IO_VIA_PMU_SIZE = 0x2000u,
	NW_IO_SCC_LEGACY_BASE = 0x80012000u,
	NW_IO_SCC_BASE = 0x80013000u,
	NW_IO_SCC_SIZE = 0x2000u,			/* legacy 0x12000 and escc 0x13000; tree has no escc */
	NW_IO_ATA0_BASE = 0x80020000u,			/* mac-io ata-3 bus 0 (AAPL,address); ndrv finds nothing */
	NW_IO_ATA1_BASE = 0x80021000u,
	NW_IO_ATA_SIZE = 0x1000u,
	NW_IO_KEYLARGO_FCR_BASE = 0x80000038u,		/* FCR0..FCR4; abuts GPIO at 0x50 */
	NW_IO_KEYLARGO_FCR_SIZE = 0x18u,
	NW_IO_KEYLARGO_TIMER_BASE = 0x80015000u,	/* free-running 18.432 MHz counter at +0x38/+0x3c */
	NW_IO_KEYLARGO_TIMER_SIZE = 0x1000u,
	NW_IO_OPENPIC_BASE = 0x80040000u,
	NW_IO_OPENPIC_SIZE = 0x40000u,			/* global 0x1000, sources 0x10000, per-CPU 0x20000 */
	NW_IO_UNIN_BASE = 0xf8000000u,
	NW_IO_UNIN_SIZE = 0x1000u,
	NW_IO_PCI_CONFIG_ADDR = 0xf2800000u,		/* uni-north PCI host bridge (pci@f2000000) */
	NW_IO_PCI_CONFIG_DATA = 0xf2c00000u,
	NW_IO_PCI_CONFIG_SIZE = 0x1000u
};

/*
 * The OpenPIC's INT output = the CPU's external interrupt line (level). The
 * CPU samples it together with the decrementer and takes vector 0x500 when
 * MSR[EE] is set. Written only by the OpenPIC model (nw_devices.cpp).
 */
extern int nw_io_ext_irq;

struct nw_io_device {
	const char *name;
	uint32_t base;
	uint32_t size;
	uint32_t (*read)(void *ctx, uint32_t off, int size);
	void (*write)(void *ctx, uint32_t off, int size, uint32_t value);
	void *ctx;
};

static inline int nw_io_range(uint32_t pa)
{
	return (pa - NW_IO_MACIO_BASE) < NW_IO_MACIO_SIZE || pa >= NW_IO_HIGH_BASE;
}

void nw_banks_set(int kind, uint32_t base, uint32_t size);
int nw_pa_kind(uint32_t pa);
int nw_pa_writable(uint32_t pa);	/* 1 for RAM-like banks; 0 for ROM, I/O, none */
void nw_io_log_banks(void);
int nw_io_n_devices(void);
const struct nw_io_device *nw_io_device(int i);

/* Register a device; returns 0 on success, -1 if the table is full or the
 * range overlaps. size in bytes; off passed to handlers is pa - base. */
int nw_io_register(const struct nw_io_device *dev);
void nw_io_reset(void);

/* Dispatch; unclaimed accesses read as 0, writes are dropped, and the first
 * occurrences are logged (NW-BOOT IO lines) so the next gate is named. */
uint32_t nw_io_read(uint32_t pa, int size, uint32_t pc);
void nw_io_write(uint32_t pa, int size, uint32_t value, uint32_t pc);
uint32_t nw_io_last_pc(void);	/* PC of the last dispatched I/O access */

/* WP5: 64-pixel tiles covering the New World frame buffer. Stores that
 * land in the layout mark tiles; present consumes them. */
enum { NW_FB_TILE = 64, NW_FB_TILES_X = 32, NW_FB_TILES_Y = 32 };
void nw_fb_damage_layout(uint32_t base, uint32_t rowbytes, uint32_t width,
			 uint32_t height, uint32_t bpp);
void nw_fb_damage_store(uint32_t pa, unsigned nbytes);
void nw_fb_damage_rect(int x, int y, int w, int h);
void nw_fb_damage_pixmap(uint32_t dest_base, int x, int y, int w, int h);
int nw_fb_damage_any(void);
int nw_fb_damage_collect(int *x, int *y, int *w, int *h, int max);
int nw_fb_damage_take(int *x, int *y, int *w, int *h, int max);
void nw_fb_damage_clear(void);
void nw_fb_damage_note_upload(uint64_t bytes);
uint64_t nw_fb_damage_upload_bytes(void);
uint64_t nw_fb_damage_marks(void);
void nw_fb_fps_proxy_sample(const uint8_t *fb, uint32_t pitch, uint32_t w, uint32_t h);
void nw_fb_fps_proxy_tick(void);
uint64_t nw_fb_fps_proxy_frames(void);
unsigned nw_fb_fps_proxy_flat_max(void);

/* Mac 32-bit FB pixel is XRGB in memory (byte0 unused, 1=R, 2=G, 3=B),
 * the same layout take_shot dumps and an SDL ARGB8888 texture expects. */
void nw_fb_mac32_rgb(const uint8_t *px, uint8_t *r, uint8_t *g, uint8_t *b);
void nw_fb_pack_mac32(uint8_t *px, uint8_t r, uint8_t g, uint8_t b);
/* Expand 8-bit indices through a 256-entry RGB CLUT into Mac 32-bit pixels. */
void nw_fb_expand_clut8_to_mac32(uint8_t *dst32, const uint8_t *src8, int npix,
				 const uint8_t pal_rgb[256 * 3]);

#endif
