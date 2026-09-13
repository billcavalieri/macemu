/*
 *  nw_nvram.h - New World NVRAM: the two parameter blocks of the boot flash
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

#ifndef NW_NVRAM_H
#define NW_NVRAM_H

#include <stdint.h>

/*
 * On a Core99 machine the NVRAM is not a separate part: it is the two 8 KiB
 * parameter blocks (flash offsets 0x4000 and 0x6000) of the 1 MiB boot-block
 * flash that also holds Open Firmware. The device tree says so
 * (/nvram@fff04000, reg fff04000 4000, compatible nvram,flash) and the
 * ROM's nvram,flash ndrv addresses the chip directly, with a twist: it uses
 * 0xff004000/0xff006000, not the reg address. The bridge decodes the whole
 * 16 MiB ROM window 0xff000000..0xffffffff onto the 1 MiB chip, so the
 * flash appears 16 times and both addresses are the same bytes. The model
 * claims the 16 KiB at offset 0x4000 of every alias and nothing else of the
 * chip (the OF image is not part of what SheepShaver has).
 *
 * The chip speaks the Intel/Sharp "SM" command set the driver issues: 0x20
 * 0xd0 erase block, 0x40 <data> program a byte (bits can only be cleared),
 * 0x50 clear status, 0xff back to read-array; while in command mode a read
 * returns the status register, 0x80 = ready, bits 0x38 = errors. Erase and
 * program complete instantly here.
 *
 * Contents: each bank is an image {sig 0x5a, chrp checksum, len, name[12],
 * adler32 of [0x14..0x2000) at 0x10, generation at 0x14} followed by CHRP
 * partitions {sig, checksum, len/16, name[12]}; the driver copies the bank
 * with the higher valid generation into RAM at start-up and, when the
 * System commits (Restart/Shut Down), erases and programs the other bank
 * with generation + 1. With no image file the model presents what Open
 * Firmware and the Trampoline leave in a fresh machine: a 'common'
 * partition (OF variables, empty = defaults) and the 'APL,MacOS75'
 * partition the Trampoline creates for Mac OS at the canonical offset
 * 0x1400, then free space. The image (both banks) persists to a file.
 */
enum {
	NW_NVRAM_FLASH_BASE = 0xff000000u,	/* ROM window; the 1 MiB chip is decoded 16 times */
	NW_NVRAM_ALIAS_STRIDE = 0x00100000u,
	NW_NVRAM_ALIASES = 16,
	NW_NVRAM_FLASH_OFFSET = 0x4000u,	/* first parameter block */
	NW_NVRAM_BANK_SIZE = 0x2000u,
	NW_NVRAM_SIZE = 0x4000u,			/* both banks */
	NW_NVRAM_HEADER_SIZE = 0x20u,
	NW_NVRAM_MACOS75_OFFSET = 0x1400u,	/* data start of APL,MacOS75; the Trampoline's constant */

	NW_NVRAM_SIG_CORE99 = 0x5a,
	NW_NVRAM_SIG_COMMON = 0x70,
	NW_NVRAM_SIG_MACOS75 = 0xa0,
	NW_NVRAM_SIG_FREE = 0x7f,

	NW_FLASH_CMD_ERASE_SETUP = 0x20,
	NW_FLASH_CMD_ERASE_CONFIRM = 0xd0,
	NW_FLASH_CMD_PROGRAM = 0x40,
	NW_FLASH_CMD_CLEAR_STATUS = 0x50,
	NW_FLASH_CMD_READ_STATUS = 0x70,
	NW_FLASH_CMD_READ_ARRAY = 0xff,
	NW_FLASH_STATUS_READY = 0x80,
	NW_FLASH_STATUS_ERASE_ERR = 0x20,
	NW_FLASH_STATUS_PROGRAM_ERR = 0x10
};

/* Load the image from path (created on first commit if absent, formatted as
 * described above) and register the aliases with nw_io. path may be NULL:
 * volatile, for the harness. */
void nw_nvram_init(const char *path);
/* Write the image back if a commit changed it since the last write. */
void nw_nvram_flush(void);
void nw_nvram_exit(void);

/* Bank image helpers, also used by the harness. */
uint8_t nw_nvram_chrp_checksum(const uint8_t *hdr16);
uint32_t nw_nvram_adler32(const uint8_t *p, uint32_t len);
/* Format one bank as a fresh machine has it; generation as given. */
void nw_nvram_format_bank(uint8_t *bank, uint32_t generation);
/* 0 if the bank is not a valid image, else its generation. */
uint32_t nw_nvram_bank_generation(const uint8_t *bank);
const uint8_t *nw_nvram_image(void);

#endif
