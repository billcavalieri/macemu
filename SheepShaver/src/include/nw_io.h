/*
 *  nw_io.h - New World guest I/O space (mac99-style physical layout)
 *
 *  SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
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
 */
enum {
	NW_IO_MACIO_BASE = 0x80000000u,
	NW_IO_MACIO_SIZE = 0x10000000u,
	NW_IO_HIGH_BASE = 0xf0000000u,
	NW_IO_VIA_PMU_BASE = 0x80016000u,
	NW_IO_SCC_LEGACY_BASE = 0x80012000u,
	NW_IO_SCC_BASE = 0x80013000u,
	NW_IO_OPENPIC_BASE = 0x80040000u
};

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

/* Register a device; returns 0 on success, -1 if the table is full or the
 * range overlaps. size in bytes; off passed to handlers is pa - base. */
int nw_io_register(const struct nw_io_device *dev);
void nw_io_reset(void);

/* Dispatch; unclaimed accesses read as 0, writes are dropped, and the first
 * occurrences are logged (NW-BOOT IO lines) so the next gate is named. */
uint32_t nw_io_read(uint32_t pa, int size, uint32_t pc);
void nw_io_write(uint32_t pa, int size, uint32_t value, uint32_t pc);

#endif
