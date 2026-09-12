/*
 *  nw_boot_contract.cpp - New World / nanokernel v2 boot contract (G0–G2) + S4 event stream
 */

#include "nw_boot_contract.h"
#include "nw_devices.h"

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
	CI_WORD_380 = 0x380, CI_WORD_384 = 0x384,
	CI_LOWMEM_INIT_OFF = 0xb0,	/* offset of the MacLowMemInit table */
	CI_LOWMEM_INIT_TBL = 0x3a0,	/* Trampoline moves it here (ROM: 0xff4) */
	CI_VERSION_STR = 0x70, CI_LA_INTERRUPT_CTL = 0x94,
	/* Trampoline tail tables, reached from the hardware-info block */
	CI_TAIL_CFC = 0xcfc, CI_TAIL_D00 = 0xd00, CI_TAIL_F00 = 0xf00,
	CI_TAIL_F40 = 0xf40, CI_TAIL_F80 = 0xf80,
	/* PMDT attr low bits: M|PP=2 (RW), M|PP=3 (RO), M|PP=1 (KDP), I|M|G|PP=2
	 * (I/O, golden 0x8000003a / 0xf000003a), 'unmapped' terminator */
	PMDT_RW = 0x12, PMDT_RO = 0x13, PMDT_KDP = 0x11, PMDT_IO = 0x3a, PMDT_TERM = 0xa00,
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
	if ((l->ci_pa & 0xfffu) != 0 || (l->bootinfo_pa & 0xfffu) != 0)
		return -1;

	const uint32_t la_irp = nw_be32_load(ci, CI_LA_INFO_RECORD);	/* 0x5fffe000 */
	const uint32_t la_kdp = nw_be32_load(ci, CI_LA_KERNEL_DATA);	/* 0x68ffe000 */
	const uint32_t la_edp = nw_be32_load(ci, CI_LA_EMULATOR_DATA);	/* 0x68fff000 */
	const uint32_t la_emul = nw_be32_load(ci, CI_LA_EMULATOR_CODE);	/* 0x68060000 */
	const uint32_t la_nk = la_emul & 0xfff00000u;			/* 0x68000000 */
	if ((la_irp & 0xfffu) || (la_kdp & 0xfffu) || (la_edp & 0xfffu) || la_emul == 0)
		return -1;

	/* Trampoline clears the checksum/signature words. */
	for (uint32_t o = 0; o < 0x28; o += 4)
		nw_be32_store(ci, o, 0);

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
	/* ConfigInfo page, read-only, where the 68k and the hardware-info block
	 * expect it (golden: (8fef, 1, 0x3013)); boot-info area RW. */
	pmdt_add(r, &nr, NW_CI_LA, l->ci_pa, 0x1000u, PMDT_RO, 0);
	if (l->bootinfo_pa)
		pmdt_add(r, &nr, NW_BOOTINFO_LA, l->bootinfo_pa, NW_BOOTINFO_SIZE, PMDT_RW, 0);
	/* I/O segments 1:1, cache-inhibited/guarded, as golden mac99: mac-io
	 * (0x80000000, whole segment) and the high segment (uni-north; the 68k
	 * ROM window BAT sits in front of its upper 4 MiB). */
	pmdt_add(r, &nr, 0x80000000u, 0x80000000u, 0x10000000u, PMDT_IO, 0);
	pmdt_add(r, &nr, 0xf0000000u, 0xf0000000u, 0x10000000u, PMDT_IO, 0);
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
			if ((uint64_t)r[i].la < (uint64_t)r[j].la + r[j].size &&
			    (uint64_t)r[j].la < (uint64_t)r[i].la + r[i].size)
				return -1;

	int n = 0, irp_idx = -1, kdp_idx = -1, edp_idx = -1;
	uint32_t seg_off[16];
	for (int seg = 0; seg < 16; seg++) {
		seg_off[seg] = (uint32_t)n * 8u;
		/*
		 * RAM segments carry a spare 0xa00 entry ahead of the terminator
		 * (golden: two `0000ffff 00000a00` per segment 0..3, `0000fffd`
		 * for 4 and 5). NK 0x3123fc rewrites the *first* entry of each
		 * logical-RAM segment in place into a page-table-backed range
		 * ((table << 10) | 0xc00); with a single 8-byte entry per segment
		 * the rewritten seg-0 entry ran into seg 1's and LA 0 resolved
		 * through the wrong table (512 MiB: LA 0 -> PA 0x20000000 while
		 * MacLowMemInit sat at 0x10000000). Segment 5 is our ROM area, not
		 * RAM, so it keeps only the terminator (RAM <= 1 GiB, segs 0..3).
		 * Golden's 0xfffd for segs 4/5 leaves pages 0xfffe/0xffff to fall
		 * through into the next list (IRP mirror); we keep every list
		 * self-terminating with 0xffff.
		 */
		if (seg < 5) {
			struct pmdt_entry p;
			p.page = 0;
			p.count_m1 = 0xffffu;
			p.attr = PMDT_TERM;
			if (n >= NW_CI_PAGEMAP_MAX - 1)
				return -1;
			pmdt_put(ci, n++, &p);
		}
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
	 * 0xffc00000 window, never at the ROM area's host identity. This value
	 * is also the trim applied to bank 0 (see nw_fill_system_info_be). */
	nw_be32_store(ci, CI_PA_RELOC_LOWMEM, l->ram_base);

	/* Trampoline marks the record v1.01 (golden: 0101 0000 8100 0000) and
	 * clears the stale Old World words 0x364..0x374. */
	for (uint32_t o = 0x364; o <= 0x374; o += 4)
		nw_be32_store(ci, o, 0);
	nw_be32_store(ci, CI_VERSION16, 0x01010000u);
	nw_be32_store(ci, CI_FLAGS_37C, 0x81000000u);
	nw_be32_store(ci, CI_WORD_380, 0x03200258u);
	nw_be32_store(ci, CI_WORD_384, 0x0c800004u);
	nw_be32_store(ci, CI_VERSION_STR, 0x30202020u);		/* "0   " */
	nw_be32_store(ci, CI_LA_INTERRUPT_CTL, 0x80060000u);	/* golden mac99 */

	/* MacLowMemInit moves from 0xff4 to 0x3a0 (the tail is used below):
	 * (offset 4, 0xffc0002a), then a zero offset terminates. */
	nw_be32_store(ci, CI_LOWMEM_INIT_OFF, CI_LOWMEM_INIT_TBL);
	nw_be32_store(ci, CI_LOWMEM_INIT_TBL + 0, 4);
	nw_be32_store(ci, CI_LOWMEM_INIT_TBL + 4, NW_68K_ROM_LA + 0x2a);
	nw_be32_store(ci, CI_LOWMEM_INIT_TBL + 8, 0);

	/*
	 * Tail tables the Trampoline appends (golden mac99 values). The
	 * hardware-info block points at +0xcfc, +0xf40 and +0xf80; +0xf80 is a
	 * 0xffff-terminated list of 16-bit OpenPIC interrupt sources (0x2f VIA-PMU
	 * first), +0xd40/+0xf00 hold a 16-byte descriptor, the rest is 0xff fill.
	 */
	for (uint32_t o = CI_TAIL_D00; o < CI_TAIL_D00 + 0x40; o += 4)
		nw_be32_store(ci, o, 0xffffffffu);
	/* per-source priorities, one byte each in list order, zero padded */
	for (uint32_t i = 0; i < 16; i++) {
		const uint8_t p = i < NW_TRAMPOLINE_NIRQ ? nw_trampoline_irqs[i].prio : 0;
		ci[CI_TAIL_D00 + 0x40 + i] = p;
		ci[CI_TAIL_F00 + i] = p;
	}
	/* per-68k-level vector masks, +0xf40[level]: bit (31 - list position)
	 * for each source of that level. The ROM's level-N autovector handler
	 * ANDs the NK's pending-vector word with this before dispatching (and
	 * returns without acknowledging when nothing matches, which re-posts the
	 * interrupt forever), so this must follow the list. Golden: level 2 =
	 * 0x80540000 (positions 0, 9, 11, 13 of its 15-entry list). */
	for (uint32_t o = CI_TAIL_F40; o < CI_TAIL_F80; o += 4)
		nw_be32_store(ci, o, 0);
	for (uint32_t i = 0; i < NW_TRAMPOLINE_NIRQ && i < 32; i++) {
		const uint32_t o = CI_TAIL_F40 + 4u * (nw_trampoline_irqs[i].prio & 7);
		nw_be32_store(ci, o, nw_be32_load(ci, o) | (0x80000000u >> i));
	}
	/* 16-bit source list, 0xffff terminated */
	for (uint32_t o = CI_TAIL_F80; o < NW_CI_SIZE; o += 4)
		nw_be32_store(ci, o, 0xffffffffu);
	for (uint32_t i = 0; i < NW_TRAMPOLINE_NIRQ; i++) {
		ci[CI_TAIL_F80 + 2 * i] = 0;
		ci[CI_TAIL_F80 + 2 * i + 1] = nw_trampoline_irqs[i].src;
	}
	return n;
}

const struct nw_irq_source nw_trampoline_irqs[NW_TRAMPOLINE_NIRQ] = {
	{ 0x2f, 2, 1 },	/* extint-gpio1: PMU interrupt */
	{ 0x37, 7, 0 },	/* programmer-switch NMI */
	{ 0x19, 1, 1 },	/* via-pmu */
	/* (golden: escc ch-a 0x25/0x04/0x05 and ch-b 0x24/0x06/0x07 here; the
	 * tree has no escc, see nw_bootinfo.cpp) */
	{ 0x0d, 2, 1 },	/* ata-3 bus 0 */
	{ 0x02, 4, 0 },	/*   dma */
	{ 0x0e, 2, 1 },	/* ata-3 bus 1 */
	{ 0x03, 4, 0 },
	{ 0x1d, 2, 1 },	/* display VBL (pci slot e), SheepShaver's; level, 68k level 2 */
	{ 0x1c, 2, 1 },	/* usb (golden machine has one; harmless without) */
	{ 0x1e, 3, 1 },
};

void nw_trampoline_program_pic(void)
{
	for (int i = 0; i < NW_TRAMPOLINE_NIRQ; i++) {
		const struct nw_irq_source *s = &nw_trampoline_irqs[i];
		/* vector = list index: the NK maps IACK vectors to 68k levels
		 * through ConfigInfo+0xf00[vector] (PA 0x3f00 on the golden
		 * machine), the byte table written in list order above. */
		uint32_t ivpr = (uint32_t)NW_OPENPIC_IVPR_MASK | NW_OPENPIC_IVPR_POLARITY |
				((uint32_t)s->prio << 16) | (uint32_t)i;
		if (s->level)
			ivpr |= NW_OPENPIC_IVPR_SENSE;
		nw_openpic_write(NW_OPENPIC_SRC0 + (uint32_t)s->src * 0x20u, ivpr);
		nw_openpic_write(NW_OPENPIC_SRC0 + (uint32_t)s->src * 0x20u + 0x10u, 1);	/* IDR: CPU 0 */
	}
	nw_openpic_write(NW_OPENPIC_CPU0 + 0x80, 0);					/* CTPR */
}

void nw_fill_hwinfo_be(uint8_t *hw, const struct nw_config_info_layout *l)
{
	if (hw == NULL || l == NULL)
		return;
	memset(hw, 0, NW_HWINFO_SIZE);
	nw_be32_store(hw, 0x00, l->rom_base);			/* ROM image PA */
	nw_be32_store(hw, 0x04, NW_BOOTINFO_LA + 0x0c);		/* boot-info entry list */
	nw_be32_store(hw, 0x08, NW_BOOTINFO_LA + NW_BOOTINFO_HWREC_OFF);	/* ProductInfo record */
	nw_be32_store(hw, 0x0c, l->ci_pa);			/* ConfigInfo PA */
	nw_be32_store(hw, 0x10, NW_CI_LA + CI_TAIL_F80);	/* interrupt source list */
	nw_be32_store(hw, 0x14, NW_CI_LA + CI_TAIL_F40);
	nw_be32_store(hw, 0x18, 0x80040000u);			/* OpenPIC (mac99 mac-io + 0x40000) */
	nw_be32_store(hw, 0x3c, 0x00001400u);
	nw_be32_store(hw, 0x70, 0x486e666fu);			/* 'Hnfo' */
	nw_be32_store(hw, 0x74, 0x00403035u);			/* +0x76: machine id 0x3035, 68k reads it */
	nw_be32_store(hw, 0x78, 0x00250024u);
	nw_be32_store(hw, 0x7c, 0x08000800u);
	nw_be32_store(hw, 0x80, 0x00190002u);
	nw_be32_store(hw, 0x84, 0x08000037u);
	nw_be32_store(hw, 0x88, 0x00010000u);
	nw_be32_store(hw, 0x94, 0x00000004u);
	nw_be32_store(hw, 0x9c, 0x00410000u);
	nw_be32_store(hw, 0xa0, 0x63173569u);
	nw_be32_store(hw, 0xa8, NW_CI_LA + CI_TAIL_CFC);
}

/*
 * ProductInfo/DecoderInfo record the Trampoline builds for a mac99-class
 * machine (captured from the golden run after the 68k StartInit had applied
 * its own ROM-table fixups, which are idempotent). Offsets are relative to
 * record - NW_BOOTINFO_HWREC_PRE. +0x28 (record +0) is the offset of the
 * DecoderInfo (0x98); DecoderInfo +0x8 is the VIA base, +0xc/+0x10 the SCC
 * bases, +0xf4 the OpenPIC; DecoderInfo -0x28 (record +0x70) holds the
 * hardware flags the 68k tests (bit 2: VIA1 present). The 0x9bbbxxxx words
 * are offsets the Trampoline relocated against its own view of the ROM
 * ProductInfo; the 68k never dereferences them before Welcome, and they are
 * reproduced as captured.
 *
 * Record +0x24 (here +0x4c, mirrored at +0xa4) is UnivROMFlags. Bits 1..3
 * describe the input path the Trampoline's HandleSpecialNode found in the
 * device tree: bit 2 for an `adb` node compatible "pmu-99" ("P99 ADB
 * detected"), bit 1 when there is none but USB ("Virtual (USB-emulated) ADB
 * detected!"). The 68k ADB Manager (ffc2b5f6) picks its bus routines by
 * `UnivROMFlags & 0xe`: 0xa keeps the ROM default (no bus, every command
 * completes without a device; input is expected from USB HID), 0xc installs
 * the PMU-99 ADB routines (ffc06bf0: PMgrOp 0x20 packets, PMU ADB
 * interrupt). Golden `-M mac99,via=pmu` shows c003bf1a, `via=pmu-adb`
 * c003bf1c; everything else in the record is identical. Our tree carries the
 * pmu-adb shape (nw_bootinfo.cpp), so the record says bit 2.
 */
static const struct { uint16_t off; uint32_t val; } hwrec[] = {
	{ 0x01c, 0x00000008u }, { 0x020, 0xf8000000u }, { 0x024, 0x01000000u },
	{ 0x028, 0x00000098u }, { 0x02c, 0x9bbbc350u }, { 0x030, 0x9bbbc360u },
	{ 0x034, 0x9bbbc362u }, { 0x038, 0x4c807f1au }, { 0x03c, 0x3fff0402u },
	{ 0x040, 0x0000001cu }, { 0x044, 0x60000000u }, { 0x04c, 0xc003bf1cu },
	{ 0x050, 0x058480efu }, { 0x060, 0x9bbbc488u }, { 0x068, 0x9bbb5a54u },
	{ 0x06c, 0x9bbb5714u }, { 0x070, 0x9bbb4e90u }, { 0x078, 0x9bbbd2d4u },
	{ 0x080, 0x30350000u }, { 0x084, 0x9bbbc372u }, { 0x088, 0x00000190u },
	{ 0x090, 0xffc0e000u }, { 0x098, 0x0000001cu }, { 0x09c, 0x60000000u },
	{ 0x0a4, 0xc003bf1cu }, { 0x0a8, 0x058480efu }, { 0x0b8, 0x1a010000u },
	{ 0x0c0, 0xffc00000u }, { 0x0c8, 0x80016000u }, { 0x0cc, 0x80012000u },
	{ 0x0d0, 0x80012000u }, { 0x1b4, 0x80040000u }, { 0x1b8, 0x00010100u },
};

void nw_fill_bootinfo_be(uint8_t *area, uint32_t size, const struct nw_config_info_layout *l)
{
	if (area == NULL || l == NULL || size < (uint32_t)NW_BOOTINFO_SIZE)
		return;
	memset(area, 0, NW_BOOTINFO_SIZE);
	/* 'PMR&' 'BGsT' 'ree\0': boot-globals header. The device tree that
	 * follows at +0xc is written by nw_bootinfo_build_tree() into the
	 * NW_BOOTINFO_TREE_MAX bytes below the hardware record. */
	nw_be32_store(area, 0x0, 0x504d5226u);
	nw_be32_store(area, 0x4, 0x42477354u);
	nw_be32_store(area, 0x8, 0x72656500u);
	const uint32_t base = NW_BOOTINFO_HWREC_OFF - NW_BOOTINFO_HWREC_PRE;
	for (size_t i = 0; i < sizeof(hwrec) / sizeof(hwrec[0]); i++)
		nw_be32_store(area, base + hwrec[i].off, hwrec[i].val);
}

void nw_lzss_decode(const uint8_t *src, size_t src_size, uint8_t *dst, size_t dst_size)
{
	if (src == NULL || dst == NULL)
		return;
	nw_decode_lzss(src, dst, (int)src_size, dst + dst_size);
}

uint32_t nw_la_ram_base, nw_la_ram_size, nw_la_rom_base, nw_la_kdp_pa;
uint32_t nw_thunk_area_base, nw_thunk_area_size;

void nw_la_enable(uint32_t ram_base, uint32_t ram_size, uint32_t rom_base)
{
	nw_la_ram_base = ram_base;
	nw_la_rom_base = rom_base;
	nw_la_kdp_pa = 0;
	nw_la_ram_size = ram_size;	/* last: activates the translation */
}

void nw_fill_system_info_be(uint8_t *si, const struct nw_config_info_layout *l)
{
	if (si == NULL || l == NULL)
		return;
	memset(si, 0, NW_SI_SIZE);
	/* PhysicalMemorySize, UsableMemorySize; bank list at +0x30 (start,size)
	 * pairs, up to 26. NK 0x310548: r12 = ConfigInfo PA_RelocatedLowMem;
	 * the first non-empty bank gets base += r12, size -= r12 (r12 clamped
	 * to 0 if size < r12) and the total loses r12 too; lowmem is zeroed and
	 * MacLowMemInit applied at PA r12, and LA 0 is later mapped to bank 0's
	 * first page. Golden: bank (0, 0x20000000), r12 = 0x4000 (vectors).
	 * So the bank must be described from PA 0 with r12 == ram_base: after
	 * the trim it is (ram_base, ram_size) and lowmem sits on its first page.
	 * Describing (ram_base, ram_size) directly only worked while
	 * ram_size < ram_base (clamp); at 512 MiB the NK trimmed 256 MiB off
	 * and mapped LA 0 to ram_base + 0x10000000. */
	nw_be32_store(si, 0x00, l->ram_base + l->ram_size);
	nw_be32_store(si, 0x04, l->ram_base + l->ram_size);
	nw_be32_store(si, 0x30, 0);
	nw_be32_store(si, 0x34, l->ram_base + l->ram_size);
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

const char *nw_boot_line_credits(void)
{
	return "NewWorld boot by Bill Cavalieri";
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
	return "G1: NewWorld patch skip";
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
	if (nw_detect_decoded_rom(rom, size) == NW_DECODED_NEWWORLD) {
		nw_boot_log(nw_boot_line_credits());
		nw_boot_log(nw_boot_line_g0_newworld());
	}
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
