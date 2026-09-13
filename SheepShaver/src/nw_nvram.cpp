/*
 *  nw_nvram.cpp - New World NVRAM: the two parameter blocks of the boot flash
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
#include "nw_nvram.h"

/*
 * What the ROM's nvram,flash ndrv does with the chip (disassembly of the
 * parcel, code offsets): validate a bank (+0x1f4/+0x20c: byte 0 == 0x5a,
 * CHRP checksum of the 16-byte header with byte 1 taken as 0, adler32 of
 * [0x14..0x2000) against +0x10, result = generation at +0x14); pick the
 * bank with the higher generation and copy it byte by byte (+0x2b8);
 * commit (+0x6e8): generation + 1, new adler, erase the other bank with
 * 0x20 0xd0 at its first byte, program every byte with 0x40 <data>, 0xff
 * after each, verify; the status is polled at 0xff004000 (+0x444) for up
 * to five seconds, 0x80 = done, 0x38 = error. It never reads the device
 * tree's reg: the addresses are 0xff000000 + 0x4000/0x6000.
 */

enum { BANKS = NW_NVRAM_SIZE / NW_NVRAM_BANK_SIZE };

static uint8_t g_image[NW_NVRAM_SIZE];
static char g_path[1024];
static int g_have_path;
static int g_dirty;

/* chip state: read-array or command mode, and the pending two-cycle command */
static int g_status_mode;
static uint8_t g_status = NW_FLASH_STATUS_READY;
static int g_pending;		/* 0, NW_FLASH_CMD_ERASE_SETUP or NW_FLASH_CMD_PROGRAM */
static uint32_t g_programmed;	/* bytes programmed since the last read-array command */
static int g_program_bank;	/* bank of the last programmed byte; the read-array command
				 * that ends a sequence always goes to 0xff004000 */

uint8_t nw_nvram_chrp_checksum(const uint8_t *hdr16)
{
	/* the ndrv's loop: 8-bit sum with end-around carry over all 16 bytes,
	 * the checksum byte itself counted as 0 */
	unsigned sum = 0;
	for (int i = 0; i < 16; i++) {
		unsigned b = (i == 1) ? 0 : hdr16[i];
		unsigned n = (sum + b) & 0xff;
		if (n < sum)
			n = (n + 1) & 0xff;
		sum = n;
	}
	return (uint8_t)sum;
}

uint32_t nw_nvram_adler32(const uint8_t *p, uint32_t len)
{
	uint32_t a = 1, b = 0;
	for (uint32_t i = 0; i < len; i++) {
		a = (a + p[i]) % 65521u;
		b = (b + a) % 65521u;
	}
	return (b << 16) | a;
}

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v >> 16); put16(p + 2, v); }
static uint32_t get16(const uint8_t *p) { return ((uint32_t)p[0] << 8) | p[1]; }
static uint32_t get32(const uint8_t *p) { return (get16(p) << 16) | get16(p + 2); }

static void write_partition_header(uint8_t *h, uint8_t sig, uint32_t len16, const char *name)
{
	memset(h, 0, 16);
	h[0] = sig;
	put16(h + 2, len16);
	if (name)
		strncpy((char *)h + 4, name, 12);
	h[1] = nw_nvram_chrp_checksum(h);
}

static void seal_bank(uint8_t *bank, uint32_t generation)
{
	put32(bank + 0x14, generation);
	put32(bank + 0x10, nw_nvram_adler32(bank + 0x14, NW_NVRAM_BANK_SIZE - 0x14));
	bank[1] = nw_nvram_chrp_checksum(bank);
}

void nw_nvram_format_bank(uint8_t *bank, uint32_t generation)
{
	memset(bank, 0, NW_NVRAM_BANK_SIZE);
	/* the image header is itself the first partition, two units long */
	write_partition_header(bank, NW_NVRAM_SIG_CORE99, NW_NVRAM_HEADER_SIZE / 16, "nvram");
	/* Open Firmware's variables up to the Trampoline's constant */
	const uint32_t common_end = NW_NVRAM_MACOS75_OFFSET - 16;
	write_partition_header(bank + NW_NVRAM_HEADER_SIZE, NW_NVRAM_SIG_COMMON,
	                       (common_end - NW_NVRAM_HEADER_SIZE) / 16, "common");
	/* 0x50 units: the size the Trampoline creates it with */
	const uint32_t macos75_len = 0x50;
	write_partition_header(bank + common_end, NW_NVRAM_SIG_MACOS75, macos75_len, "APL,MacOS75");
	const uint32_t free_start = common_end + macos75_len * 16;
	write_partition_header(bank + free_start, NW_NVRAM_SIG_FREE,
	                       (NW_NVRAM_BANK_SIZE - free_start) / 16, NULL);
	seal_bank(bank, generation);
}

uint32_t nw_nvram_bank_generation(const uint8_t *bank)
{
	if (bank[0] != NW_NVRAM_SIG_CORE99)
		return 0;
	if (nw_nvram_chrp_checksum(bank) != bank[1])
		return 0;
	if (nw_nvram_adler32(bank + 0x14, NW_NVRAM_BANK_SIZE - 0x14) != get32(bank + 0x10))
		return 0;
	return get32(bank + 0x14);
}

/* Offset of the partition with this signature (and name, if given) in a
 * valid bank, walking the 16-byte headers; 0 if none. */
static uint32_t find_partition(const uint8_t *bank, uint8_t sig, const char *name)
{
	uint32_t off = 0;
	while (off + 16 <= NW_NVRAM_BANK_SIZE) {
		const uint32_t len = get16(bank + off + 2) * 16;
		if (len == 0)
			return 0;
		if (bank[off] == sig && (name == NULL || strncmp((const char *)bank + off + 4, name, 12) == 0))
			return off;
		off += len;
	}
	return 0;
}

/*
 * The Trampoline (Mac OS ROM file +0x6300..+0x6620) walks the partitions
 * of the bank Open Firmware hands it and, if there is no 0xa0
 * 'APL,MacOS75', carves one of 0x50 units out of the first 0x7f free
 * partition of at least 0x51 units, writing the new headers through OF.
 * Done here on the same conditions, in the current bank.
 */
static int ensure_macos75(uint8_t *bank)
{
	if (find_partition(bank, NW_NVRAM_SIG_MACOS75, "APL,MacOS75"))
		return 0;
	uint32_t off = 0;
	while (off + 16 <= NW_NVRAM_BANK_SIZE) {
		const uint32_t len16 = get16(bank + off + 2);
		if (len16 == 0)
			return -1;
		if (bank[off] == NW_NVRAM_SIG_FREE && len16 >= 0x51) {
			write_partition_header(bank + off, NW_NVRAM_SIG_MACOS75, 0x50, "APL,MacOS75");
			memset(bank + off + 16, 0, 0x50 * 16 - 16);
			write_partition_header(bank + off + 0x50 * 16, NW_NVRAM_SIG_FREE, len16 - 0x50, NULL);
			seal_bank(bank, get32(bank + 0x14));
			return 1;
		}
		off += len16 * 16;
	}
	return -1;
}

static int current_bank(void)
{
	const uint32_t ga = nw_nvram_bank_generation(g_image);
	const uint32_t gb = nw_nvram_bank_generation(g_image + NW_NVRAM_BANK_SIZE);
	return (ga >= gb) ? 0 : 1;	/* the ndrv's choice, ties to A */
}

void nw_nvram_flush(void)
{
	if (!g_dirty || !g_have_path)
		return;
	FILE *f = fopen(g_path, "wb");
	if (f == NULL) {
		printf("NW-BOOT G1: nvram flash: cannot write %s\n", g_path);
		return;
	}
	const size_t n = fwrite(g_image, 1, NW_NVRAM_SIZE, f);
	fclose(f);
	if (n == NW_NVRAM_SIZE)
		g_dirty = 0;
}

static void erase_block(uint32_t off)
{
	const uint32_t block = off & ~(NW_NVRAM_BANK_SIZE - 1);
	memset(g_image + block, 0xff, NW_NVRAM_BANK_SIZE);
	g_dirty = 1;
	printf("NW-BOOT G1: nvram flash erase bank %c\n", block ? 'B' : 'A');
}

static uint32_t flash_read(void *, uint32_t off, int size)
{
	uint32_t v = 0;
	for (int i = 0; i < size; i++) {
		const uint32_t o = (off + i) & (NW_NVRAM_SIZE - 1);
		v = (v << 8) | (g_status_mode ? g_status : g_image[o]);
	}
	return v;
}

static void flash_write_byte(uint32_t off, uint8_t v)
{
	if (g_pending == NW_FLASH_CMD_PROGRAM) {
		g_image[off] &= v;		/* programming only clears bits */
		g_pending = 0;
		g_status_mode = 1;
		g_dirty = 1;
		g_programmed++;
		g_program_bank = (off >= NW_NVRAM_BANK_SIZE);
		return;
	}
	if (g_pending == NW_FLASH_CMD_ERASE_SETUP) {
		g_pending = 0;
		g_status_mode = 1;
		if (v == NW_FLASH_CMD_ERASE_CONFIRM)
			erase_block(off);
		else	/* command sequence error, as the chip reports it */
			g_status |= NW_FLASH_STATUS_ERASE_ERR | NW_FLASH_STATUS_PROGRAM_ERR;
		return;
	}
	switch (v) {
	case NW_FLASH_CMD_ERASE_SETUP:
	case NW_FLASH_CMD_PROGRAM:
		g_pending = v;
		g_status_mode = 1;
		break;
	case NW_FLASH_CMD_CLEAR_STATUS:
		g_status = NW_FLASH_STATUS_READY;
		g_status_mode = 1;
		break;
	case NW_FLASH_CMD_READ_STATUS:
		g_status_mode = 1;
		break;
	case NW_FLASH_CMD_READ_ARRAY:
		g_status_mode = 0;
		if (g_programmed) {
			/* the driver programs a whole bank then verifies it; the file
			 * follows each completed sequence, not each byte */
			printf("NW-BOOT G1: nvram flash bank %c programmed %u bytes, generation %u\n",
			       g_program_bank ? 'B' : 'A', (unsigned)g_programmed,
			       (unsigned)nw_nvram_bank_generation(g_image + g_program_bank * NW_NVRAM_BANK_SIZE));
			g_programmed = 0;
		}
		nw_nvram_flush();
		break;
	default:
		break;
	}
}

static void flash_write(void *, uint32_t off, int size, uint32_t value)
{
	for (int i = 0; i < size; i++)
		flash_write_byte((off + i) & (NW_NVRAM_SIZE - 1), (uint8_t)(value >> (8 * (size - 1 - i))));
}

void nw_nvram_init(const char *path)
{
	g_have_path = 0;
	g_dirty = 0;
	g_status_mode = 0;
	g_status = NW_FLASH_STATUS_READY;
	g_pending = 0;
	g_programmed = 0;
	memset(g_image, 0xff, NW_NVRAM_SIZE);

	int loaded = 0;
	if (path && strlen(path) < sizeof(g_path)) {
		strcpy(g_path, path);
		g_have_path = 1;
		FILE *f = fopen(g_path, "rb");
		if (f) {
			loaded = (fread(g_image, 1, NW_NVRAM_SIZE, f) == NW_NVRAM_SIZE);
			fclose(f);
			if (!loaded)
				memset(g_image, 0xff, NW_NVRAM_SIZE);
		}
	}
	if (!loaded) {
		/* a fresh machine: OF's image in bank A, generation 1, bank B erased */
		nw_nvram_format_bank(g_image, 1);
		g_dirty = 1;
	}

	const int cur = current_bank();
	uint8_t *bank = g_image + cur * NW_NVRAM_BANK_SIZE;
	int carved = 0;
	if (nw_nvram_bank_generation(bank)) {
		carved = ensure_macos75(bank);
		if (carved > 0)
			g_dirty = 1;
	}
	const uint32_t macos75 = find_partition(bank, NW_NVRAM_SIG_MACOS75, "APL,MacOS75");
	printf("NW-BOOT G1: nvram flash %s: bank A gen %u, bank B gen %u, APL,MacOS75 %s0x%x%s\n",
	       g_have_path ? g_path : "(volatile)",
	       (unsigned)nw_nvram_bank_generation(g_image),
	       (unsigned)nw_nvram_bank_generation(g_image + NW_NVRAM_BANK_SIZE),
	       macos75 ? "data at " : "", macos75 ? (unsigned)(macos75 + 16) : 0u,
	       carved > 0 ? " (created, as the Trampoline does)" : macos75 ? "" : " missing");

	for (int i = 0; i < NW_NVRAM_ALIASES; i++) {
		struct nw_io_device dev;
		dev.name = "nvram-flash";
		dev.base = NW_NVRAM_FLASH_BASE + i * NW_NVRAM_ALIAS_STRIDE + NW_NVRAM_FLASH_OFFSET;
		dev.size = NW_NVRAM_SIZE;
		dev.read = flash_read;
		dev.write = flash_write;
		dev.ctx = NULL;
		nw_io_register(&dev);
	}
	nw_nvram_flush();
}

void nw_nvram_exit(void)
{
	nw_nvram_flush();
}

const uint8_t *nw_nvram_image(void)
{
	return g_image;
}
