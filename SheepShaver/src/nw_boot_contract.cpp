/*
 *  nw_boot_contract.cpp - New World / nanokernel v2 boot contract (G0–G2) + S4 event stream
 */

#include "nw_boot_contract.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>


/* Must match rom_patches.h ROMTYPE_NEWWORLD. */
enum { NW_ROMTYPE_NEWWORLD = 5 };

static const struct nw_of_node_spec kNewWorldTree[] = {
	/* Root properties live on device-tree itself (compatible MacRISC2). */
	{ "", "cpus", NULL, NULL, NULL },
	{ "cpus", "PowerPC,750@0", "cpu", NULL, NULL },
	{ "", "memory", "memory", NULL, NULL },
	{ "", "chosen", NULL, NULL, NULL },
	{ "", "uni-n", "uni-n", "uni-n", NULL },
	{ "uni-n", "pci", "pci", "uni-north", NULL },
	{ "uni-n/pci", "video", "display", NULL, "SheepShaver Video" },
	{ "uni-n/pci", "mac-io", "mac-io", "mac-io", NULL },
	{ "uni-n/pci/mac-io", "via-cuda", "via-cuda", "cuda", NULL },
	{ "uni-n/pci/mac-io/via-cuda", "adb", "adb", NULL, NULL },
	{ "uni-n/pci/mac-io/via-cuda", "nvram", "nvram", NULL, NULL },
};

static int spec_has_name(const char *name)
{
	const size_t n = sizeof(kNewWorldTree) / sizeof(kNewWorldTree[0]);
	for (size_t i = 0; i < n; i++) {
		if (strcmp(kNewWorldTree[i].name, name) == 0)
			return 1;
	}
	return 0;
}

void nw_be32_store(uint8_t *mem, uint32_t off, uint32_t value)
{
	mem[off + 0] = (uint8_t)(value >> 24);
	mem[off + 1] = (uint8_t)(value >> 16);
	mem[off + 2] = (uint8_t)(value >> 8);
	mem[off + 3] = (uint8_t)value;
}

uint32_t nw_be32_load(const uint8_t *mem, uint32_t off)
{
	return ((uint32_t)mem[off + 0] << 24) |
	       ((uint32_t)mem[off + 1] << 16) |
	       ((uint32_t)mem[off + 2] << 8) |
	       (uint32_t)mem[off + 3];
}

const char *nw_root_compatible(void)
{
	return "MacRISC2";
}

const char *nw_root_model(void)
{
	return "PowerMac3,1";
}

uint32_t nw_gestalt_machine_id(int is_newworld)
{
	return is_newworld ? (uint32_t)NW_GESTALT_MACHINE_ID : 0x3020u;
}

int nw_rom_type_is_newworld(int rom_type)
{
	return rom_type == NW_ROMTYPE_NEWWORLD;
}

enum nw_decoded_rom_kind nw_detect_decoded_rom(const uint8_t *rom, size_t size)
{
	if (rom == NULL || size < NW_NEWWORLD_SIG_OFFSET + 13)
		return NW_DECODED_UNKNOWN;
	const uint8_t *sig = rom + NW_NEWWORLD_SIG_OFFSET;
	if (memcmp(sig, "NewWorld", 8) == 0)
		return NW_DECODED_NEWWORLD;
	if (memcmp(sig, "Boot TNT", 8) == 0 ||
	    memcmp(sig, "Boot Alchemy", 12) == 0 ||
	    memcmp(sig, "Boot Zanzibar", 13) == 0 ||
	    memcmp(sig, "Boot Gazelle", 12) == 0 ||
	    memcmp(sig, "Boot Gossamer", 13) == 0)
		return NW_DECODED_OLDWORLD;
	return NW_DECODED_UNKNOWN;
}

enum {
	NW_FOURCC_PRCL = 0x7072636c,	/* 'prcl' */
	NW_FOURCC_ROM  = 0x726f6d20	/* 'rom ' */
};

static const uint8_t *nw_find_mem(const uint8_t *hay, size_t n, const char *needle)
{
	const size_t m = strlen(needle);
	if (m == 0 || m > n)
		return NULL;
	for (size_t i = 0; i + m <= n; i++) {
		if (memcmp(hay + i, needle, m) == 0)
			return hay + i;
	}
	return NULL;
}

static int nw_chrp_hex_constant(const uint8_t *src, size_t src_size,
				const char *name, uint32_t *out)
{
	char needle[80];
	if (snprintf(needle, sizeof(needle), "constant %s", name) >= (int)sizeof(needle))
		return 0;
	const uint8_t *p = nw_find_mem(src, src_size, needle);
	if (p == NULL || (size_t)(p - src) < 7)
		return 0;
	unsigned v = 0;
	if (sscanf((const char *)(p - 7), "%06x", &v) != 1)
		return 0;
	*out = (uint32_t)v;
	return 1;
}

static void nw_decode_lzss(const uint8_t *src, uint8_t *dest, int size,
			   uint8_t *dest_end)
{
	char dict[0x1000];
	int run_mask = 0, dict_idx = 0xfee;
	for (;;) {
		if (run_mask < 0x100) {
			if (--size < 0)
				break;
			run_mask = *src++ | 0xff00;
		}
		bool bit = run_mask & 1;
		run_mask >>= 1;
		if (bit) {
			if (--size < 0)
				break;
			int c = *src++;
			dict[dict_idx++] = (char)c;
			if (dest < dest_end)
				*dest++ = (uint8_t)c;
			dict_idx &= 0xfff;
		} else {
			if (--size < 0)
				break;
			int idx = *src++;
			if (--size < 0)
				break;
			int cnt = *src++;
			idx |= (cnt << 4) & 0xf00;
			cnt = (cnt & 0x0f) + 3;
			while (cnt--) {
				char c = dict[idx++];
				dict[dict_idx++] = c;
				if (dest < dest_end)
					*dest++ = (uint8_t)c;
				idx &= 0xfff;
				dict_idx &= 0xfff;
			}
		}
	}
}

static int nw_decode_parcels(const uint8_t *src, size_t src_size, uint8_t *dest,
			     uint8_t *dest_end)
{
	uint32_t parcel_offset = 0x14;
	int decoded = 0;
	while (parcel_offset != 0 && parcel_offset + 12 <= src_size) {
		const uint32_t next_offset = nw_be32_load(src, parcel_offset);
		const uint32_t parcel_type = nw_be32_load(src, parcel_offset + 4);
		if (parcel_type == (uint32_t)NW_FOURCC_ROM) {
			const uint32_t lzss_offset = nw_be32_load(src, parcel_offset + 8);
			uint32_t parcel_end = next_offset ? next_offset : (uint32_t)src_size;
			if (parcel_end <= parcel_offset + lzss_offset)
				return 0;
			const uint32_t lzss_size = parcel_end - parcel_offset - lzss_offset;
			if ((size_t)parcel_offset + lzss_offset + lzss_size > src_size)
				return 0;
			nw_decode_lzss(src + parcel_offset + lzss_offset, dest,
				       (int)lzss_size, dest_end);
			decoded = 1;
		}
		if (next_offset == 0 || next_offset <= parcel_offset)
			break;
		parcel_offset = next_offset;
	}
	return decoded;
}

int nw_g0_unpacked_ok(const uint8_t *rom, size_t size)
{
	if (rom == NULL || size < (size_t)NW_ROM_SIZE)
		return 0;
	if (nw_detect_decoded_rom(rom, size) != NW_DECODED_NEWWORLD)
		return 0;
	/* Nanokernel v2 entry jump_to_rom uses. Must not be an empty page. */
	unsigned nz = 0;
	for (unsigned i = 0; i < 16; i++)
		nz += rom[NW_NK_V2_OFFSET + i] != 0;
	return nz != 0;
}

int nw_decode_rom_image(const uint8_t *src, size_t src_size,
			uint8_t *dest, size_t dest_size)
{
	if (src == NULL || dest == NULL || dest_size < (size_t)NW_ROM_SIZE)
		return 0;

	memset(dest, 0, dest_size);

	if (src_size == (size_t)NW_ROM_SIZE) {
		memcpy(dest, src, NW_ROM_SIZE);
		return 1;
	}

	if (src_size < 11 || memcmp(src, "<CHRP-BOOT>", 11) != 0)
		return 0;

	uint32_t image_offset = 0, image_size = 0;
	int decode_info_ok = 0;
	if (nw_chrp_hex_constant(src, src_size, "lzss-offset", &image_offset) &&
	    nw_chrp_hex_constant(src, src_size, "lzss-size", &image_size))
		decode_info_ok = 1;
	else if (nw_chrp_hex_constant(src, src_size, "parcels-offset", &image_offset) &&
		 nw_chrp_hex_constant(src, src_size, "parcels-size", &image_size))
		decode_info_ok = 1;
	if (!decode_info_ok)
		return 0;
	if (image_size == 0 || (size_t)image_offset + image_size > src_size)
		return 0;

	const uint32_t sig = nw_be32_load(src, image_offset);
	if (sig == (uint32_t)NW_FOURCC_PRCL) {
		if (!nw_decode_parcels(src + image_offset, image_size, dest,
				       dest + NW_ROM_SIZE))
			return 0;
	} else {
		nw_decode_lzss(src + image_offset, dest, (int)image_size,
			       dest + NW_ROM_SIZE);
	}
	return 1;
}

const struct nw_of_node_spec *nw_of_tree_spec(size_t *count)
{
	if (count)
		*count = sizeof(kNewWorldTree) / sizeof(kNewWorldTree[0]);
	return kNewWorldTree;
}

int nw_of_tree_has_required_nodes(void)
{
	/* G1 required: /memory (RAM banks), /cpus, /chosen.
	   9.2.1 also wants uni-n / PCI / video / ADB / CUDA-or-PMU / NVRAM. */
	return spec_has_name("memory") &&
	       spec_has_name("cpus") &&
	       spec_has_name("chosen") &&
	       spec_has_name("uni-n") &&
	       spec_has_name("pci") &&
	       spec_has_name("video") &&
	       spec_has_name("adb") &&
	       spec_has_name("via-cuda") &&
	       spec_has_name("nvram");
}

void nw_fill_kdp_be(uint8_t *page, size_t page_len, const struct nw_kdp_params *p)
{
	if (page == NULL || p == NULL || page_len < NW_KDP_PAGE_SIZE)
		return;

	uint32_t htaborg = p->htaborg ? p->htaborg : (uint32_t)NW_DEFAULT_HTABORG;
	uint32_t ptegmask = p->ptegmask ? p->ptegmask : (uint32_t)NW_DEFAULT_PTEGMASK;
	uint32_t sdr1 = p->sdr1 ? p->sdr1 : (htaborg | ((ptegmask >> 16) & 0x1ffu));

	/* saveKernelDataPtr must sit immediately after saveReturnAddr. */
	nw_be32_store(page, NW_KDP_SAVE_RETURN_ADDR, 0);
	nw_be32_store(page, NW_KDP_SAVE_KERNEL_DATA_PTR, p->kdp_ea);

	/* BATRangeInit: 32 longs at KDP+0x2cc. First pair records RAM banks. */
	for (int i = 0; i < NW_KDP_BAT_RANGE_INIT_LONGS; i++)
		nw_be32_store(page, NW_KDP_BAT_RANGE_INIT + (uint32_t)i * 4u, 0);
	nw_be32_store(page, NW_KDP_BAT_RANGE_INIT + 0, p->ram_base);
	nw_be32_store(page, NW_KDP_BAT_RANGE_INIT + 4, p->ram_size);

	nw_be32_store(page, NW_KDP_PTEGMASK, ptegmask);
	nw_be32_store(page, NW_KDP_HTABORG, htaborg);

	/* NKHWInfo.Signature == 'Hnfo' skips HardwareInit CPU probe / mfsdr1. */
	nw_be32_store(page, NW_KDP_HWINFO_BASE + 0, p->rom_base);
	nw_be32_store(page, NW_KDP_HNFO_SIGNATURE, (uint32_t)NW_HNFO_SIGNATURE);
	nw_be32_store(page, NW_KDP_HNFO_HTAB_SDR1, sdr1);
	/* 68k CMPI.L #'Hnfo', ([KDP+0xfd0], $70) */
	nw_be32_store(page, 0xfd0, p->kdp_ea + (uint32_t)NW_KDP_HWINFO_BASE);
}

/*
 * NKConfigurationInfo ("NewWorld v1.0" layout) field offsets. Only the
 * fields the Trampoline rewrites are listed; everything else stays as the
 * ROM shipped it (offsets are ConfigInfo-relative, so an in-place ROM is
 * self-consistent).
 */
enum {
	CI_EXC_TABLE_OFF = 0x3c, CI_EXC_TABLE_SIZE = 0x40,
	CI_HWINIT_OFF = 0x44, CI_HWINIT_SIZE = 0x48,
	CI_EMUL_CODE_OFF = 0x54, CI_EMUL_CODE_SIZE = 0x58,
	CI_OPCODE_TBL_OFF = 0x5c, CI_OPCODE_TBL_SIZE = 0x60,
	CI_LA_INFO_RECORD = 0x9c, CI_LA_KERNEL_DATA = 0xa0, CI_LA_EMULATOR_DATA = 0xa4,
	CI_LA_DISPATCH_TABLE = 0xa8, CI_LA_EMULATOR_CODE = 0xac,
	CI_PAGE_ATTR_INIT = 0xb4, CI_PAGEMAP_SIZE = 0xb8, CI_PAGEMAP_OFF = 0xbc,
	CI_PAGEMAP_IRP = 0xc0, CI_PAGEMAP_KDP = 0xc4, CI_PAGEMAP_EDP = 0xc8,
	CI_SEGMAP_SUP = 0xcc, CI_SEGMAP_USR = 0x14c, CI_SEGMAP_CPU = 0x1cc, CI_SEGMAP_OVL = 0x24c,
	CI_BAT_RANGE_INIT = 0x2cc,
	CI_BATMAP_SUP = 0x34c, CI_BATMAP_USR = 0x350, CI_BATMAP_CPU = 0x354, CI_BATMAP_OVL = 0x358,
	CI_PA_RELOC_LOWMEM = 0x360,
	CI_VERSION16 = 0x378, CI_FLAGS_37C = 0x37c,
	CI_LOWMEM_INIT_TBL = 0xff4,
	/* PMDT attr low bits: M|PP=2 (RW), M|PP=1 (KDP), 'unmapped' terminator */
	PMDT_RW = 0x12, PMDT_KDP = 0x11, PMDT_TERM = 0xa00,
	/* BAT map nibbles (IBAT0..3 low, DBAT0..3 high; f = unused): golden mac99
	 * uses slot 3 = range 1, slot 2 = range 3 (NK 1 MiB); we add slot 1 =
	 * range 2 (68k ROM window) since golden's range 1 covers 0xffc00000 and
	 * ours is the ROM area identity. CPU map: NK only, as golden. */
	BATMAP_SUP = 0x132f132fu, BATMAP_USR = 0x132f132fu,
	BATMAP_CPU = 0xf3fff3ffu, BATMAP_OVL = 0x132f132fu
};

struct pmdt_entry { uint16_t page, count_m1; uint32_t attr; };

static void pmdt_put(uint8_t *ci, int idx, const struct pmdt_entry *e)
{
	uint32_t off = (uint32_t)NW_CI_PAGEMAP_OFF + (uint32_t)idx * 8u;
	ci[off + 0] = (uint8_t)(e->page >> 8);
	ci[off + 1] = (uint8_t)e->page;
	ci[off + 2] = (uint8_t)(e->count_m1 >> 8);
	ci[off + 3] = (uint8_t)e->count_m1;
	nw_be32_store(ci, off + 4, e->attr);
}


enum { PMDT_MAX_RANGES = 40 };
struct pmdt_range { uint32_t la, pa, size, attr; int role; };

static int pmdt_add(struct pmdt_range *r, int *nr, uint32_t la, uint32_t pa,
		    uint32_t size, uint32_t attr, int role)
{
	if (*nr >= PMDT_MAX_RANGES)
		return 0;
	r[*nr].la = la;
	r[*nr].pa = pa;
	r[*nr].size = size;
	r[*nr].attr = attr;
	r[*nr].role = role;
	(*nr)++;
	return 1;
}

int nw_fill_config_info_be(uint8_t *ci, const struct nw_config_info_layout *l)
{
	if (ci == NULL || l == NULL)
		return -1;
	if ((l->rom_base & 0xfffu) != 0 || l->rom_area_size == 0 || (l->rom_area_size & 0xfffu) ||
	    (l->rom_base & 0x0fffffffu) + l->rom_area_size > 0x10000000u)
		return -1;	/* ROM area: page aligned, inside one segment */
	if ((l->ram_base & 0xfffu) != 0 || l->ram_size <= (uint32_t)NW_NK_LOWMEM_ZEROED)
		return -1;

	const uint32_t la_irp = nw_be32_load(ci, CI_LA_INFO_RECORD);	/* 0x5fffe000 */
	const uint32_t la_kdp = nw_be32_load(ci, CI_LA_KERNEL_DATA);	/* 0x68ffe000 */
	const uint32_t la_edp = nw_be32_load(ci, CI_LA_EMULATOR_DATA);	/* 0x68fff000 */
	const uint32_t la_emul = nw_be32_load(ci, CI_LA_EMULATOR_CODE);	/* 0x68060000 */
	const uint32_t la_nk = la_emul & 0xfff00000u;			/* 0x68000000 */
	if ((la_irp & 0xfffu) || (la_kdp & 0xfffu) || (la_edp & 0xfffu) || la_emul == 0)
		return -1;

	/* Code stays in place (no NK-side copies), no Old World HWInit probe. */
	nw_be32_store(ci, CI_EXC_TABLE_OFF, 0);
	nw_be32_store(ci, CI_EXC_TABLE_SIZE, 0);
	nw_be32_store(ci, CI_HWINIT_OFF, 0);
	nw_be32_store(ci, CI_HWINIT_SIZE, 0);
	nw_be32_store(ci, CI_EMUL_CODE_OFF, 0);
	nw_be32_store(ci, CI_EMUL_CODE_SIZE, 0);
	nw_be32_store(ci, CI_OPCODE_TBL_OFF, 0);
	nw_be32_store(ci, CI_OPCODE_TBL_SIZE, 0);

	/*
	 * Collect mapped ranges (LA, PA, size, attr, role), split at segment
	 * boundaries, then emit per segment in ascending page order with one
	 * terminator each. role: 0 plain, 1 IRP, 2 KDP, 3 EDP (PA filled by NK).
	 */
	struct pmdt_range r[PMDT_MAX_RANGES];
	int nr = 0;
	pmdt_add(r, &nr, l->rom_base, l->rom_base, l->rom_area_size, PMDT_RW, 0);
	pmdt_add(r, &nr, la_nk, l->rom_base + (uint32_t)NW_NK_EXC_TABLE_ROM_OFF, 0x100000u, PMDT_RW, 0);
	pmdt_add(r, &nr, la_irp, 0, 0x1000u, PMDT_RW, 1);
	pmdt_add(r, &nr, la_kdp, 0, 0x1000u, PMDT_KDP, 2);
	pmdt_add(r, &nr, la_edp, 0, 0x1000u, PMDT_RW, 3);
	for (int i = 0; i < l->n_extra; i++) {
		const struct nw_pmdt_range *x = &l->extra[i];
		if (x->size == 0 || (x->la & 0xfffu) || (x->pa & 0xfffu) || (x->size & 0xfffu))
			return -1;
		uint32_t la = x->la, pa = x->pa, left = x->size;
		while (left) {	/* split at 256 MiB segment boundaries */
			uint32_t room = 0x10000000u - (la & 0x0fffffffu);
			uint32_t chunk = left < room ? left : room;
			if (!pmdt_add(r, &nr, la, pa, chunk, PMDT_RW, 0))
				return -1;
			la += chunk; pa += chunk; left -= chunk;
		}
	}
	/* Overlap check (ranges within the same segment must not intersect). */
	for (int i = 0; i < nr; i++)
		for (int j = i + 1; j < nr; j++)
			if (r[i].la < r[j].la + r[j].size && r[j].la < r[i].la + r[i].size)
				return -1;

	int n = 0, irp_idx = -1, kdp_idx = -1, edp_idx = -1;
	uint32_t seg_off[16];
	for (int seg = 0; seg < 16; seg++) {
		seg_off[seg] = (uint32_t)n * 8u;
		/* selection sort of this segment's ranges by LA */
		int used[PMDT_MAX_RANGES] = { 0 };
		for (;;) {
			int best = -1;
			for (int i = 0; i < nr; i++) {
				if (used[i] || (int)(r[i].la >> 28) != seg)
					continue;
				if (best < 0 || r[i].la < r[best].la)
					best = i;
			}
			if (best < 0)
				break;
			used[best] = 1;
			struct pmdt_entry e;
			e.page = (uint16_t)((r[best].la >> 12) & 0xffffu);
			e.count_m1 = (uint16_t)((r[best].size >> 12) - 1u);
			e.attr = (r[best].pa & 0xfffff000u) | r[best].attr;
			if (r[best].role == 1) irp_idx = n;
			if (r[best].role == 2) kdp_idx = n;
			if (r[best].role == 3) edp_idx = n;
			if (n >= NW_CI_PAGEMAP_MAX - 1)
				return -1;
			pmdt_put(ci, n++, &e);
		}
		struct pmdt_entry t;
		t.page = 0;
		t.count_m1 = 0xffff;
		t.attr = (seg >= 6) ? (((uint32_t)seg << 28) | PMDT_TERM | 1u) : PMDT_TERM;
		if (n >= NW_CI_PAGEMAP_MAX)
			return -1;
		pmdt_put(ci, n++, &t);
	}
	if (irp_idx < 0 || kdp_idx < 0 || edp_idx < 0)
		return -1;
	nw_be32_store(ci, CI_PAGE_ATTR_INIT, PMDT_RW);
	nw_be32_store(ci, CI_PAGEMAP_SIZE, (uint32_t)n * 8u);
	nw_be32_store(ci, CI_PAGEMAP_OFF, NW_CI_PAGEMAP_OFF);
	nw_be32_store(ci, CI_PAGEMAP_IRP, (uint32_t)irp_idx * 8u);
	nw_be32_store(ci, CI_PAGEMAP_KDP, (uint32_t)kdp_idx * 8u);
	nw_be32_store(ci, CI_PAGEMAP_EDP, (uint32_t)edp_idx * 8u);

	/* Segment maps: (page-map offset, SR value = seg << 20) x 16, x 4 maps. */
	const uint32_t segmaps[4] = { CI_SEGMAP_SUP, CI_SEGMAP_USR, CI_SEGMAP_CPU, CI_SEGMAP_OVL };
	for (int m = 0; m < 4; m++) {
		for (int seg = 0; seg < 16; seg++) {
			nw_be32_store(ci, segmaps[m] + (uint32_t)seg * 8u, seg_off[seg]);
			nw_be32_store(ci, segmaps[m] + (uint32_t)seg * 8u + 4u, (uint32_t)seg << 20);
		}
	}

	/* BAT ranges: [1] ROM area (8 MiB block, RW) at its host-side identity;
	 * [2] the 68k ROM window LA 0xffc00000 (4 MiB, read-only, write-through)
	 * -> ROM image, as the golden Trampoline does (its PA is the RAM copy of
	 * the ROM, ours is the ROM area itself); [3] NK 1 MiB at la_nk. */
	for (int i = 0; i < 32; i++)
		nw_be32_store(ci, CI_BAT_RANGE_INIT + (uint32_t)i * 4u, 0);
	nw_be32_store(ci, CI_BAT_RANGE_INIT + 8, l->rom_base | (0x3fu << 2) | 3u);
	nw_be32_store(ci, CI_BAT_RANGE_INIT + 12, l->rom_base | 0x02u);
	nw_be32_store(ci, CI_BAT_RANGE_INIT + 16, NW_68K_ROM_LA | (0x1fu << 2) | 3u);
	nw_be32_store(ci, CI_BAT_RANGE_INIT + 20, l->rom_base | 0x43u);
	nw_be32_store(ci, CI_BAT_RANGE_INIT + 24, la_nk | (0x07u << 2) | 3u);
	nw_be32_store(ci, CI_BAT_RANGE_INIT + 28, (l->rom_base + (uint32_t)NW_NK_EXC_TABLE_ROM_OFF) | 0x02u);
	nw_be32_store(ci, CI_BATMAP_SUP, BATMAP_SUP);
	nw_be32_store(ci, CI_BATMAP_USR, BATMAP_USR);
	nw_be32_store(ci, CI_BATMAP_CPU, BATMAP_CPU);
	nw_be32_store(ci, CI_BATMAP_OVL, BATMAP_OVL);

	/* Low memory lives at the start of the RAM bank; NK zeroes 0x2000 there
	 * and applies the ROM's own MacLowMemInit table, which sets the 68k
	 * reset PC (lowmem 4) to 0xffc0002a: the 68k ROM runs in the
	 * 0xffc00000 window, never at the ROM area's host identity. */
	nw_be32_store(ci, CI_PA_RELOC_LOWMEM, l->ram_base);

	/* Trampoline marks the record v1.01 (golden: 0101 0000 8100 0000). */
	nw_be32_store(ci, CI_VERSION16, 0x01010000u);
	nw_be32_store(ci, CI_FLAGS_37C, 0x81000000u);
	return n;
}

void nw_fill_system_info_be(uint8_t *si, const struct nw_config_info_layout *l)
{
	if (si == NULL || l == NULL)
		return;
	memset(si, 0, NW_SI_SIZE);
	/* PhysicalMemorySize, UsableMemorySize; bank list at +0x30 (start,size)
	 * pairs, up to 26. Low memory (LA 0) is the first pages of bank 0: the
	 * NK writes MacLowMemInit at PA_RelocatedLowMemInit (== ram_base) and
	 * maps logical page 0 to the first free page, so both must be the same
	 * page (golden: bank 0 trimmed to start at 0x4000 == PA_RelocatedLowMem).
	 * Our vectors live at PA 0 outside the bank, so no trim is needed and
	 * LA x -> PA ram_base + x for the whole bank. */
	nw_be32_store(si, 0x00, l->ram_size);
	nw_be32_store(si, 0x04, l->ram_size);
	nw_be32_store(si, 0x30, l->ram_base);
	nw_be32_store(si, 0x34, l->ram_size);
	/* Golden mac99 values for fields the NK reads but we have not decoded. */
	nw_be32_store(si, 0x100, 0x80040000u);
	nw_be32_store(si, 0x128, 0x00000035u);
	nw_be32_store(si, 0x12c, 0xe0000000u);
}

void nw_fill_processor_info_be(uint8_t *pi, uint32_t pvr, uint32_t cpu_hz,
			       uint32_t bus_hz, uint32_t tb_hz)
{
	if (pi == NULL)
		return;
	memset(pi, 0, NW_PI_SIZE);
	nw_be32_store(pi, 0x00, pvr);
	nw_be32_store(pi, 0x04, cpu_hz);
	nw_be32_store(pi, 0x08, bus_hz);
	nw_be32_store(pi, 0x0c, tb_hz);
	nw_be32_store(pi, 0x10, 0x1000u);		/* page size */
	nw_be32_store(pi, 0x14, 0x8000u);		/* L1 data cache size */
	nw_be32_store(pi, 0x18, 0x8000u);		/* L1 instruction cache size */
	nw_be32_store(pi, 0x1c, 0x00200020u);	/* cache block sizes */
	nw_be32_store(pi, 0x20, 0x00000020u);
	nw_be32_store(pi, 0x24, 0x00200020u);
	nw_be32_store(pi, 0x28, 0x00200020u);
	nw_be32_store(pi, 0x2c, 0x00080008u);	/* associativity */
	nw_be32_store(pi, 0x30, 0x00800002u);
}

int nw_config_info_pagemap_ok(const uint8_t *ci)
{
	if (ci == NULL)
		return 0;
	const uint32_t off = nw_be32_load(ci, CI_PAGEMAP_OFF);
	const uint32_t size = nw_be32_load(ci, CI_PAGEMAP_SIZE);
	if (off == 0 || size == 0 || (size & 7u) || off + size > (uint32_t)NW_CI_SIZE)
		return 0;
	const uint32_t irp = nw_be32_load(ci, CI_PAGEMAP_IRP);
	const uint32_t kdp = nw_be32_load(ci, CI_PAGEMAP_KDP);
	const uint32_t edp = nw_be32_load(ci, CI_PAGEMAP_EDP);
	for (int seg = 0; seg < 16; seg++) {
		uint32_t p = nw_be32_load(ci, CI_SEGMAP_SUP + (uint32_t)seg * 8u);
		if (p >= size)
			return 0;
		int prev = -1;
		for (;;) {
			if (p >= size)
				return 0;
			uint32_t page = ((uint32_t)ci[off + p] << 8) | ci[off + p + 1];
			uint32_t cnt = ((uint32_t)ci[off + p + 2] << 8) | ci[off + p + 3];
			uint32_t attr = nw_be32_load(ci, off + p + 4);
			if (page == 0 && cnt == 0xffffu)
				break;
			if ((attr & 0xe00u) == 0) {
				if ((int)page <= prev)
					return 0;
				if ((p == irp || p == kdp || p == edp) && cnt != 0)
					return 0;
				prev = (int)(page + cnt);
			}
			p += 8;
		}
	}
	return 1;
}

int nw_kdp_save_ptrs_adjacent(const uint8_t *page)
{
	if (page == NULL)
		return 0;
	if (NW_KDP_SAVE_KERNEL_DATA_PTR != NW_KDP_SAVE_RETURN_ADDR + 4)
		return 0;
	return nw_be32_load(page, NW_KDP_SAVE_KERNEL_DATA_PTR) != 0;
}

int nw_kdp_bat_range_init_present(const uint8_t *page)
{
	if (page == NULL)
		return 0;
	/* 32 longs occupy [0x2cc, 0x34c). First bank is non-zero size. */
	return nw_be32_load(page, NW_KDP_BAT_RANGE_INIT + 4) != 0;
}

int nw_kdp_hnfo_valid_htab(const uint8_t *page)
{
	if (page == NULL)
		return 0;
	if (nw_be32_load(page, NW_KDP_HNFO_SIGNATURE) != (uint32_t)NW_HNFO_SIGNATURE)
		return 0;
	const uint32_t sdr1 = nw_be32_load(page, NW_KDP_HNFO_HTAB_SDR1);
	const uint32_t htaborg = nw_be32_load(page, NW_KDP_HTABORG);
	return sdr1 != 0 || htaborg != 0;
}

int nw_htab_gate_pass(const struct nw_htab_gate *gate)
{
	if (gate == NULL)
		return 0;
	return gate->hnfo_valid_htab || gate->spr_log_mtsdr1;
}

const char *nw_boot_line_g0_newworld(void)
{
	return "G0: DecodeROM 4 MiB NewWorld +0x30d064 NK +0x310000";
}

const char *nw_boot_line_g1_tree(void)
{
	return "G1: tree root compatible MacRISC2 Gestalt 406 /memory /cpus /chosen";
}

const char *nw_boot_line_g1_kdp(void)
{
	return "G1: Hnfo vs mtsdr1: Hnfo BATRangeInit saveKernelDataPtr adjacent";
}

const char *nw_boot_line_g1_mtsdr1(void)
{
	return "G1: Hnfo vs mtsdr1: mtsdr1";
}

const char *nw_boot_line_g1_hwinit(void)
{
	return "G1: HardwareInit handoff NK +0x310000 ConfigInfo +0x30d000";
}

const char *nw_boot_line_g1_patch_skip(void)
{
	return "G1: New World patch skip";
}

const char *nw_boot_line_g2_first_dsi(void)
{
	return "G2: first DSI SRR0=PC DR on HIT no second DSI";
}

const char *nw_boot_line_g2_translator_off(void)
{
	return "G2: translator off (Old World)";
}


/*
 *  Boot log. NW_BOOT_LOG=1 on the Xcode SheepShaver Debug configuration.
 *  Everything goes to stdout prefixed "NW-BOOT " so a hang-capped run's
 *  stdout is the log newworldview reads.
 */

void nw_boot_log(const char *line)
{
#if NW_BOOT_LOG
	if (line)
		printf("NW-BOOT %s\n", line);
	fflush(stdout);
#else
	(void)line;
#endif
}

void nw_log_g0_decode(const uint8_t *rom, size_t size)
{
	if (nw_detect_decoded_rom(rom, size) == NW_DECODED_NEWWORLD)
		nw_boot_log(nw_boot_line_g0_newworld());
}

void nw_log_g1_tree(void)
{
	nw_boot_log(nw_boot_line_g1_tree());
}

void nw_log_g1_kdp(const uint8_t *page)
{
	if (nw_kdp_hnfo_valid_htab(page) && nw_kdp_bat_range_init_present(page) &&
	    nw_kdp_save_ptrs_adjacent(page))
		nw_boot_log(nw_boot_line_g1_kdp());
}

void nw_note_mtsdr1(void)
{
	static int logged;
	if (logged)
		return;
	logged = 1;
	nw_boot_log(nw_boot_line_g1_mtsdr1());
}

void nw_log_msr_dr(uint32_t msr)
{
	static int logged;
	if (logged)
		return;
	if ((msr & NW_MSR_DR) == 0)
		return;
	logged = 1;
	nw_boot_log("G2: MSR[DR] on");
}

void nw_log_msr_write(const char *how, uint32_t pc, uint32_t msr)
{
#if NW_BOOT_LOG
	static unsigned n;
	static int ee_on;
	char buf[96];

	if (how == NULL)
		how = "msr";
	if ((msr & NW_MSR_EE) && !ee_on) {
		ee_on = 1;
		snprintf(buf, sizeof(buf), "G2: %s EE on pc=%08x msr=%08x",
			 how, (unsigned)pc, (unsigned)msr);
		nw_boot_log(buf);
	}
	if (n < 8) {
		n++;
		snprintf(buf, sizeof(buf), "G2: %s n=%u pc=%08x msr=%08x",
			 how, n, (unsigned)pc, (unsigned)msr);
		nw_boot_log(buf);
	}
#else
	(void)how;
	(void)pc;
	(void)msr;
#endif
}

void nw_log_g1_hwinit(void)
{
	nw_boot_log(nw_boot_line_g1_hwinit());
}

void nw_log_g1_patch_skip(int is_newworld)
{
	if (is_newworld)
		nw_boot_log(nw_boot_line_g1_patch_skip());
}

void nw_log_first_dsi(uint32_t srr0, uint32_t dar, int dr_on_hit)
{
	static int logged;
	char buf[128];
	if (logged)
		return;
	logged = 1;
	snprintf(buf, sizeof(buf), "G2: first DSI SRR0=%08x DAR=%08x DRhit=%d",
		 (unsigned)srr0, (unsigned)dar, dr_on_hit);
	nw_boot_log(buf);
	if (srr0 != dar && dr_on_hit)
		nw_boot_log(nw_boot_line_g2_first_dsi());
}

void nw_log_translator_off(void)
{
	nw_boot_log(nw_boot_line_g2_translator_off());
}

/*
 *  S4 event stream (golden grammar).
 */

int nw_emu_aline_handler(uint32_t off)
{
	switch (off) {
	case NW_EMU_ALINE_OS_FLAG:		return 0;
	case NW_EMU_ALINE_OS:			return 1;
	case NW_EMU_ALINE_TOOL:			return 2;
	case NW_EMU_ALINE_TOOL_AUTOPOP:		return 3;
	default:				return -1;
	}
}

#if NW_BOOT_LOG
static unsigned long nw_event_nx;
static unsigned long nw_event_na;
#endif

void nw_event_exception(uint32_t srr0, uint32_t vector, uint32_t extra, int extra_valid)
{
#if NW_BOOT_LOG
	nw_event_nx++;
	if (extra_valid)
		printf("NW-BOOT X E %08x %08x %08x\n", (unsigned)srr0,
		       (unsigned)vector, (unsigned)extra);
	else
		printf("NW-BOOT X E %08x %08x\n", (unsigned)srr0, (unsigned)vector);
#else
	(void)srr0;
	(void)vector;
	(void)extra;
	(void)extra_valid;
#endif
}

void nw_event_aline(uint32_t op, uint32_t pc68k, int handler)
{
#if NW_BOOT_LOG
	nw_event_na++;
	printf("NW-BOOT A %04x %08x %d\n", (unsigned)(op & 0xffffu),
	       (unsigned)pc68k, handler);
#else
	(void)op;
	(void)pc68k;
	(void)handler;
#endif
}

void nw_event_tick(uint32_t pc, uint32_t msr)
{
#if NW_BOOT_LOG
	static time_t last;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	if (tv.tv_sec == last)
		return;
	last = tv.tv_sec;
	printf("NW-BOOT T %lld %lu %lu %08x %08x\n",
	       (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000,
	       nw_event_nx, nw_event_na, (unsigned)pc, (unsigned)msr);
	fflush(stdout);
#else
	(void)pc;
	(void)msr;
#endif
}
