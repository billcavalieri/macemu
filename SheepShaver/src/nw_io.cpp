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

enum { NW_IO_MAX_DEVICES = 32, NW_IO_LOG_MAX = 64, NW_IO_PAGES_MAX = 128 };	/* 7 models + 16 flash aliases */

static struct nw_io_device g_devs[NW_IO_MAX_DEVICES];
static int g_ndevs;
static int g_log_count;
static uint32_t g_pages[NW_IO_PAGES_MAX];
static int g_npages;
int nw_io_ext_irq;

void nw_io_reset(void)
{
	g_ndevs = 0;
	g_log_count = 0;
	g_npages = 0;
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
	const struct nw_io_device *d = find(pa);
	if (d && d->read)
		return d->read(d->ctx, pa - d->base, size);
	log_unclaimed('R', pa, size, 0, pc);
	return 0;
}

void nw_io_write(uint32_t pa, int size, uint32_t value, uint32_t pc)
{
	const struct nw_io_device *d = find(pa);
	if (d && d->write) {
		d->write(d->ctx, pa - d->base, size, value);
		return;
	}
	log_unclaimed('W', pa, size, value, pc);
}
