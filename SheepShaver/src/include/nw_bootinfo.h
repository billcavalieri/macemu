/*
 *  nw_bootinfo.h - New World boot-info area: flattened device tree + parcels
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 *
 *  The Trampoline leaves a 'PMR&' 'BGsT' 'ree\0' block at LA 0x64000000:
 *  a device tree in "BGsTree" form (consumed by the 68k StartInit /
 *  NameRegistry importer at ROM 0x44420..0x44580) with the ROM file's
 *  parcels merged in as properties (drivers, libraries).
 *
 *  Format (all offsets relative to the record they appear in):
 *    +0x0  'PMR&' 'BGsT' 'ree\0'
 *    +0xc  root node record
 *    node record (12 bytes): sibling, child, first-property
 *    property record: next (== record size, 0 for the last), name[32],
 *                     value length, value padded to 4 bytes
 *
 *  Data only: no ROM code is patched. The tree describes the machine
 *  SheepShaver presents (mac99-like: uni-north, Keylargo mac-io, via-pmu,
 *  OpenPIC, escc, ata-3, a display node backed by SheepShaver's frame
 *  buffer). Parcels come from the loaded Mac OS ROM file.
 */

#ifndef NW_BOOTINFO_H
#define NW_BOOTINFO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct nw_bootinfo_params {
	uint32_t ram_size;		/* logical RAM (/memory reg) */
	uint32_t fb_la;			/* frame buffer LA the guest sees (display "address") */
	uint32_t fb_width, fb_height, fb_depth, fb_linebytes;
	uint32_t pvr, cpu_hz, bus_hz, tb_hz;
	const uint8_t *parcels;		/* 'prcl' blob, may be NULL */
	size_t parcels_size;
	/*
	 * SheepShaver's video ndrv for the display node
	 * ("driver,AAPL,MacOS,PowerPC"), may be NULL. With it present the
	 * ROM's cofb parcel driver (add-if-absent) stays off the node.
	 */
	const uint8_t *display_driver;
	size_t display_driver_size;
	/*
	 * Boot device (StartLib's GetStartupDevice): it resolves /chosen
	 * "bootpath" to a node and, for every drive queue entry, looks for a
	 * child of that node whose "AAPL,boot-cookie" (4 bytes) equals the
	 * drive's driver refnum. The ROM's device drivers tag their nodes
	 * this way at probe time (and the keylargo-ata ndrv deletes ATA
	 * children it does not find). SheepShaver's host-backed DRVRs have
	 * fixed refnums, so the tree carries the tags on its own node:
	 * /host-drives/cdrom@1 = cd_refnum, /host-drives/disk@0 = disk_refnum
	 * (0 = no node). boot_from_cd picks which one "bootpath" names.
	 */
	int16_t cd_refnum, disk_refnum;
	int boot_from_cd;
};

enum {
	NW_BOOTINFO_MAGIC0 = 0x504d5226u,	/* 'PMR&' */
	NW_BOOTINFO_MAGIC1 = 0x42477354u,	/* 'BGsT' */
	NW_BOOTINFO_MAGIC2 = 0x72656500u,	/* 'ree\0' */
	NW_BOOTINFO_ROOT = 0xc,
	NW_BOOTINFO_PROP_HDR = 0x28,		/* next + name[32] + length */
	NW_PHANDLE_PIC = 0x0000f0a0u		/* interrupt-parent of every device */
};

/*
 * Build the tree into `area` (zeroed first, `size` bytes). Returns the end
 * offset of the tree data (> 0) or 0 on failure (area too small, bad
 * parcels). Node records first, property data after them, both in
 * pre-order.
 */
uint32_t nw_bootinfo_build_tree(uint8_t *area, uint32_t size,
				const struct nw_bootinfo_params *p);

/* Tree queries (for tests and logging). `path` is "/" separated node
 * names from the root ("/pci/mac-io/via-pmu"); "" or "/" is the root. */
int nw_bootinfo_find_node(const uint8_t *area, uint32_t size, const char *path,
			  uint32_t *node_off);
int nw_bootinfo_get_prop(const uint8_t *area, uint32_t size, uint32_t node_off,
			 const char *name, const uint8_t **val, uint32_t *len);
int nw_bootinfo_count_nodes(const uint8_t *area, uint32_t size);

/*
 * Parcel blob keeper. DecodeROM copies the 'prcl' region of the raw ROM
 * file here (the decoded 4 MiB image no longer contains it). Returns 1 if
 * a parcel blob was found and kept.
 */
int nw_parcels_keep(const uint8_t *romfile, size_t size);
const uint8_t *nw_parcels_get(size_t *size);

#ifdef __cplusplus
}
#endif

#endif /* NW_BOOTINFO_H */
