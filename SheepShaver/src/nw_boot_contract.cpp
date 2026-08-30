/*
 *  nw_boot_contract.cpp - New World / nanokernel v2 boot contract (G1)
 */

#include "nw_boot_contract.h"

#include <stdio.h>
#include <string.h>

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

const char *nw_boot_line_g3_sdl2_window(void)
{
	return "G3: SDL2 bbox dirty VOSF off";
}

const char *nw_boot_line_g3_irq_nk(void)
{
	return "G3: HandleInterrupt NK native";
}

const char *nw_boot_line_g3_native_op(void)
{
	return "G3: native_op";
}

const char *nw_boot_line_g3_dec_arm(void)
{
	return "G3: DEC arm after G2";
}

const char *nw_boot_line_g3_walk_dec_ee(void)
{
	return "G3: 171-PC walk DEC blocked MSR[EE]=0";
}

const char *nw_boot_line_g3_dec_take(void)
{
	return "G3: DEC 0x900";
}

const char *nw_boot_line_g3_dec_left(void)
{
	return "G3: DEC handler left";
}

const char *nw_boot_line_g3_dec_leave_50326(void)
{
	return "G3: DEC leave 50326";
}

const char *nw_boot_line_g3_fb_guest(void)
{
	return "G3: FB guest dirty";
}

const char *nw_boot_line_g3_fb_none(void)
{
	return "G3: FB guest dirty none reason=EE=0 DEC never yielded walk";
}

int nw_video_fb_in_ram(uint32_t fb_ea, uint32_t ram_base, uint32_t ram_size,
			uint32_t fb_bytes)
{
	uint32_t ram_end, fb_end;

	if (fb_bytes == 0 || ram_size < fb_bytes)
		return 0;
	if (fb_ea < ram_base)
		return 0;
	ram_end = ram_base + ram_size;
	if (ram_end < ram_base)
		return 0;
	fb_end = fb_ea + fb_bytes;
	if (fb_end < fb_ea)
		return 0;
	if (fb_end > ram_end)
		return 0;
	return 1;
}

int nw_video_fb_guest_dirty(const uint8_t *fb, const uint8_t *copy,
			    size_t nbytes)
{
	if (fb == NULL || copy == NULL || nbytes == 0)
		return 0;
	return memcmp(fb, copy, nbytes) != 0 ? 1 : 0;
}

uint32_t nw_dec_arm_value(void)
{
	return (uint32_t)NW_DEC_ARM_AFTER_G2;
}

int nw_dec_take_after_g2(int first_dsi)
{
	return first_dsi ? 1 : 0;
}

int nw_dec_ee_on(uint32_t msr)
{
	return (msr & (uint32_t)NW_MSR_EE) != 0;
}

int nw_ppc_srr1_is_msr(uint32_t srr1)
{
	if (srr1 & 0xffff0000u)
		return 0;
	return 1;
}

uint32_t nw_ppc_srr1_use(uint32_t srr1)
{
	if (nw_ppc_srr1_is_msr(srr1))
		return srr1;
	return (uint32_t)NW_MSR_LIVE_EE_OFF;
}

int nw_dec_leave_pin_real(uint32_t live, uint32_t last_real)
{
	const uint32_t ir_dr =
		(uint32_t)NW_MSR_IR | (uint32_t)NW_MSR_DR;

	if (!nw_ppc_srr1_is_msr(live) || live == 0)
		return 1;
	/* Live a4f0c6ce pin was=00001040 use=00007672. Guest
	 * ME+IP real-mode after leave is a real MSR. Do not
	 * force IR+DR back. RI-only leftover (no ME) still
	 * pins. Collapse-to-0 and !is_msr still pin. */
	if ((last_real & ir_dr) != 0 && (live & ir_dr) == 0) {
		if (live & (uint32_t)NW_MSR_ME)
			return 0;
		return 1;
	}
	return 0;
}

int nw_ppc_is_cmp(uint32_t op)
{
	const uint32_t prim = op >> 26;

	if (prim == 10u || prim == 11u)
		return 1;
	if ((op & 0xfc0007feu) == 0x7c000000u)
		return 1;
	if ((op & 0xfc0007feu) == 0x7c000040u)
		return 1;
	return 0;
}

uint32_t nw_dec_leave_50326_cmp_use(uint32_t ra, uint32_t was,
				    uint32_t rb, uint32_t nxt)
{
	const uint32_t bo = (nxt >> 21) & 0x1fu;
	const uint32_t bi = (nxt >> 16) & 0x1fu;
	const uint32_t crb = bi & 3u;

	if ((nxt >> 26) != 16u)
		return was;
	/* r8: beq-only when already -1. Do not smash on bne. */
	if (ra == 8u) {
		if (bo == 12u && crb == 2u && was == 0xffffffffu)
			return 0;
		return was;
	}
	if (bo == 4u) {
		if (crb == 2u)
			return rb;
		if (crb == 0u)
			return rb - 1u;
		if (crb == 1u)
			return rb + 1u;
		return was;
	}
	if (bo == 12u) {
		if (crb == 2u)
			return rb ^ 1u;
		if (crb == 0u || crb == 1u)
			return rb;
		return was;
	}
	return was;
}

int nw_dec_can_yield(uint32_t msr)
{
	const uint32_t need =
		(uint32_t)NW_MSR_EE | (uint32_t)NW_MSR_IR;
	return ((msr & need) == need) ? 1 : 0;
}

int nw_dec_host_take(int first_dsi, uint32_t msr)
{
	/* After G2, live 00002000 never wrote EE. Take pending DEC
	 * anyway. Do not or-in MSR[EE]. Pre-G2 still needs EE+IR. */
	if (first_dsi)
		return 1;
	return nw_dec_can_yield(msr);
}

int nw_video_guest_paint_blocked(int first_dsi, uint32_t msr,
				   int nqd, int dirty)
{
	if (!first_dsi)
		return 0;
	if (nqd || dirty)
		return 0;
	/* Host takes 0x900 after G2 even if guest EE is off. */
	return nw_dec_host_take(first_dsi, msr) ? 0 : 1;
}

int nw_ppc_pc_in_nk(uint32_t pc, uint32_t rom_base)
{
	uint32_t off;

	if (pc < rom_base)
		return 0;
	off = pc - rom_base;
	if (off < (uint32_t)NW_NK_V2_OFFSET)
		return 0;
	/* mill 68k is not G3 */
	if (off >= (uint32_t)NW_NK_MILL_68K)
		return 0;
	if (off >= (uint32_t)NW_NK_68K_EMUL)
		return 0;
	return 1;
}

int nw_ppc_pc_in_dec_handler(uint32_t pc, uint32_t rom_base)
{
	uint32_t off;

	if (pc < rom_base)
		return 0;
	off = pc - rom_base;
	/* Live 46577d78: 0x900 VecTbl = ROM+0x326420 (CLOUD_LO_4).
	 * Heartbeats 503264xx. Not a skip-list change. */
	if (off < (uint32_t)NW_NK_CLOUD_LO_4)
		return 0;
	if (off >= (uint32_t)NW_NK_CLOUD_LO_4 + 0x400u)
		return 0;
	return 1;
}

int nw_handle_interrupt_use_native(int first_dsi, uint32_t pc,
				    uint32_t rom_base)
{
	if (!first_dsi)
		return 0;
	return nw_ppc_pc_in_nk(pc, rom_base);
}

int nw_handle_interrupt_skip_nested(int use_native, uint32_t r1,
				     uint32_t kdp)
{
	/* After G2, PC in NK: do not skip r1==KDP. That skip is for later
	 * MODE_NATIVE nested in NK. Boot walk uses KDP as r1. */
	if (use_native)
		return 0;
	return r1 == kdp;
}

int nw_video_clip_dirty(int *x, int *y, int *w, int *h, int sw, int sh)
{
	if (x == NULL || y == NULL || w == NULL || h == NULL)
		return 0;
	if (sw <= 0 || sh <= 0 || *w <= 0 || *h <= 0)
		return 0;
	if (*x < 0) {
		*w += *x;
		*x = 0;
	}
	if (*y < 0) {
		*h += *y;
		*y = 0;
	}
	if (*w <= 0 || *h <= 0)
		return 0;
	if (*x >= sw || *y >= sh)
		return 0;
	if (*x + *w > sw)
		*w = sw - *x;
	if (*y + *h > sh)
		*h = sh - *y;
	return (*w > 0 && *h > 0) ? 1 : 0;
}

void nw_boot_log(const char *line)
{
#if NW_BOOT_LOG
	if (line) {
		printf("NW-BOOT %s\n", line);
		FILE *f = fopen("/tmp/ss-g2-run.log", "a");
		if (f) {
			fprintf(f, "NW-BOOT %s\n", line);
			fflush(f);
			fclose(f);
		}
	}
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
	if ((msr & 0x10u) == 0)
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
	if (nw_dec_ee_on(msr) && !ee_on) {
		ee_on = 1;
		snprintf(buf, sizeof(buf),
			 "G3: %s EE on pc=%08x msr=%08x",
			 how, (unsigned)pc, (unsigned)msr);
		nw_boot_log(buf);
	}
	if (n < 8) {
		n++;
		snprintf(buf, sizeof(buf),
			 "G3: %s n=%u pc=%08x msr=%08x ee=%d",
			 how, n, (unsigned)pc, (unsigned)msr,
			 nw_dec_ee_on(msr));
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

void nw_log_dr_xlate(uint32_t pc, uint32_t ea, int ok, uint32_t pa,
		     uint32_t dbat0u, uint32_t dbat0l)
{
#if NW_BOOT_LOG
	static unsigned n;
	char buf[128];
	if (n >= 8)
		return;
	n++;
	snprintf(buf, sizeof(buf),
		 "G2: DRxlate pc=%08x ea=%08x ok=%d pa=%08x dbat0=%08x/%08x",
		 (unsigned)pc, (unsigned)ea, ok, (unsigned)pa,
		 (unsigned)dbat0u, (unsigned)dbat0l);
	nw_boot_log(buf);
#else
	(void)pc;
	(void)ea;
	(void)ok;
	(void)pa;
	(void)dbat0u;
	(void)dbat0l;
#endif
}

void nw_log_pc(uint32_t pc, uint32_t msr)
{
#if NW_BOOT_LOG
	static unsigned n;
	static unsigned ticks;
	char buf[80];
	if (n < 16) {
		n++;
		snprintf(buf, sizeof(buf), "pc=%08x msr=%08x",
			 (unsigned)pc, (unsigned)msr);
		nw_boot_log(buf);
		return;
	}
	if ((++ticks & 0x0000ffffu) == 0) {
		static uint32_t last;
		static unsigned same;
		if (pc == last)
			same++;
		else
			same = 0;
		last = pc;
		snprintf(buf, sizeof(buf), "heartbeat pc=%08x msr=%08x same=%u",
			 (unsigned)pc, (unsigned)msr, same);
		nw_boot_log(buf);
	}
#else
	(void)pc;
	(void)msr;
#endif
}

uint32_t nw_nk_irq_pic_ea(uint32_t ram_base)
{
	return ram_base + (uint32_t)NW_NK_IRQ_PIC_RAM_OFF;
}

uint8_t nw_nk_irq_status_idle(void)
{
	/* Live wrote 0xFF (bit 2 set) and spun; that is the spin value. */
	return 0;
}

int nw_nk_irq_status_spins(uint8_t v)
{
	return (v & (uint8_t)(1u << NW_NK_IRQ_SPIN_BIT)) != 0;
}

int nw_ppc_is_branch(uint32_t op)
{
	const uint32_t prim = op >> 26;
	if (prim == 16u || prim == 18u)
		return 1;
	if ((op & 0xfc0007feu) == 0x4c000020u)
		return 1;
	return 0;
}

int nw_ppc_rel_branch_target(uint32_t pc, uint32_t op, uint32_t *target)
{
	const uint32_t prim = op >> 26;
	uint32_t t;

	if (target == NULL)
		return 0;
	if (prim == 18u) {
		t = op & 0x03fffffcu;
		if (t & 0x02000000u)
			t |= 0xfc000000u;
		if ((op & 2u) == 0)
			t += pc;
		*target = t;
		return 1;
	}
	if (prim == 16u) {
		t = (uint32_t)(int32_t)(int16_t)(op & 0xfffcu);
		if ((op & 2u) == 0)
			t += pc;
		*target = t;
		return 1;
	}
	return 0;
}

int nw_nk_picspin_rom_off(uint32_t off)
{
	switch (off) {
	case NW_NK_PICSPIN_LBZ:
	case NW_NK_PICSPIN_BEQ:
	case NW_NK_PICSPIN_OLD_A:
	case NW_NK_PICSPIN_OLD_B:
	case NW_NK_PICSPIN_OLD_C:
		return 1;
	default:
		return 0;
	}
}

static int nw_nk_off_span(uint32_t off, uint32_t lo, uint32_t hi)
{
	return (off & 3u) == 0 && off >= lo && off <= hi;
}

int nw_nk_picspin_cycle_off(uint32_t off)
{
	switch (off) {
	case NW_NK_CYCLE_A:
	case NW_NK_CYCLE_B:
	case NW_NK_CYCLE_C:
	case NW_NK_CYCLE_D:
	case NW_NK_CYCLE_E:
	case NW_NK_CYCLE_F:
	case NW_NK_CYCLE_G:
	case NW_NK_CYCLE_H:
	case NW_NK_PRINT_A:
	case NW_NK_PRINT_B:
	case NW_NK_PRINT_C:
	case NW_NK_CYCLE_OLD_PAST:
	case NW_NK_TAIL_A:
	case NW_NK_TAIL_B:
	case NW_NK_TAIL_C:
	case NW_NK_CLOUD_A:
	case NW_NK_CLOUD_B:
	case NW_NK_CLOUD_C:
	case NW_NK_CLOUD_TAIL:
	case NW_NK_CLOUD_MID:
	case NW_NK_STICK:
		return 1;
	default:
		break;
	}
	/* Live b19886d6: 503127a8/b8/c8. Live 92beda4a landed on
	 * 503127cc (TAIL_HI+4); stay through that insn. */
	if (nw_nk_off_span(off, (uint32_t)NW_NK_TAIL_LO,
			   (uint32_t)NW_NK_CLOUD_TAIL))
		return 1;
	/* Live 92beda4a ~50-PC NK wait. Aligned spans so leave_npc
	 * cannot land in a wait body. Mill and PAST stay out. */
	if (nw_nk_off_span(off, (uint32_t)NW_NK_CLOUD_LO_0,
			   (uint32_t)NW_NK_CLOUD_HI_0))
		return 1;
	if (nw_nk_off_span(off, (uint32_t)NW_NK_CLOUD_LO_1,
			   (uint32_t)NW_NK_CLOUD_HI_1))
		return 1;
	if (nw_nk_off_span(off, (uint32_t)NW_NK_CLOUD_LO_2,
			   (uint32_t)NW_NK_CLOUD_HI_2))
		return 1;
	if (nw_nk_off_span(off, (uint32_t)NW_NK_CLOUD_LO_3,
			   (uint32_t)NW_NK_CLOUD_HI_3))
		return 1;
	if (nw_nk_off_span(off, (uint32_t)NW_NK_CLOUD_LO_4,
			   (uint32_t)NW_NK_CLOUD_HI_4))
		return 1;
	if (nw_nk_off_span(off, (uint32_t)NW_NK_CYCLE_A,
			   (uint32_t)NW_NK_CYCLE_OLD_PAST))
		return 1;
	return 0;
}

int nw_nk_picspin_mill_off(uint32_t off)
{
	switch (off) {
	case NW_NK_MILL_68K:
	case 0x466084u: /* ROM+4MiB mirror */
		return 1;
	default:
		return 0;
	}
}

int nw_nk_picspin_is_g2_dsi_off(uint32_t off)
{
	return off == (uint32_t)NW_NK_PICSPIN_LBZ;
}

int nw_nk_picspin_skip_after_g2(uint32_t off, uint32_t op)
{
	(void)op;
	/*
	 * After G2, skip listed picspin offs, the 27-PC cycle, the
	 * 503127a8/b8/c8 loop, the live 92beda4a ~50-PC cloud, and
	 * live 93eb1588 stick 5032582c. leave_npc must not land on
	 * 5032582c / 50325584 / 50326438 / 503128bc / 503127cc.
	 * OLD_B npc=50325aac. Never PAST/mill.
	 */
	return nw_nk_picspin_rom_off(off) || nw_nk_picspin_cycle_off(off);
}

int nw_nk_picspin_npc_stays(uint32_t npc, uint32_t from_pc, uint32_t rom_base)
{
	uint32_t off;

	if (npc == from_pc)
		return 1;
	if (rom_base != 0 && npc >= rom_base)
		off = npc - rom_base;
	else
		off = npc;
	if (nw_nk_picspin_rom_off(off) || nw_nk_picspin_cycle_off(off) ||
	    nw_nk_picspin_mill_off(off) ||
	    off == (uint32_t)NW_NK_PICSPIN_PAST)
		return 1;
	return 0;
}

uint32_t nw_nk_picspin_leave_npc(uint32_t pc, uint32_t rom_base,
				 const uint32_t *insns, unsigned n)
{
	unsigned i;
	uint32_t past;

	if (insns == NULL || n == 0)
		return pc + 4u;
	for (i = 0; i < n; i++) {
		uint32_t cur = pc + 4u * i;
		uint32_t tgt = 0;
		if (nw_ppc_rel_branch_target(cur, insns[i], &tgt) &&
		    tgt <= cur) {
			uint32_t ft = cur + 4u;
			if (!nw_nk_picspin_npc_stays(ft, pc, rom_base))
				return ft;
		}
	}
	/* No backward branch, or fallthrough was a stay PC: walk past
	 * the window until the npc is not a listed wait. */
	past = pc + 4u * (uint32_t)n;
	while (nw_nk_picspin_npc_stays(past, pc, rom_base))
		past += 4u;
	return past;
}

uint32_t nw_nk_picspin_past_npc(uint32_t pc, uint32_t rom_base)
{
	uint32_t npc;
	unsigned i;

	/*
	 * Live b62e7717: ROM+0x326000 then mill +0x366084 flood.
	 * Walk forward from pc. Never PAST, never mill.
	 */
	npc = pc + 4u;
	for (i = 0; i < 256u; i++) {
		if (!nw_nk_picspin_npc_stays(npc, pc, rom_base) &&
		    npc > pc)
			return npc;
		npc += 4u;
	}
	return pc + 4u;
}

void nw_nk_irq_fill_pic_be(uint8_t *mem, size_t mem_size, uint32_t pic_ea)
{
	if (mem == NULL)
		return;
	if ((uint64_t)pic_ea + 8u > mem_size)
		return;
	nw_be32_store(mem, pic_ea, 0);
	nw_be32_store(mem, pic_ea + 4u, 0);
}

void nw_log_xlatehow(const char *how, uint32_t ea, uint32_t msr, uint32_t sdr1,
		     uint32_t sr, uint32_t dbat3u, uint32_t dbat3l)
{
	char buf[160];
	if (how == NULL)
		how = "miss";
	snprintf(buf, sizeof(buf),
		 "G2: xlatehow=%s ea=%08x msr=%08x sdr1=%08x sr=%08x dbat3=%08x/%08x",
		 how, (unsigned)ea, (unsigned)msr, (unsigned)sdr1,
		 (unsigned)sr, (unsigned)dbat3u, (unsigned)dbat3l);
	nw_boot_log(buf);
#if !NW_BOOT_LOG
	(void)how;
	(void)ea;
	(void)msr;
	(void)sdr1;
	(void)sr;
	(void)dbat3u;
	(void)dbat3l;
#endif
}

static void nw_htab_store32(uint8_t *htab, uint32_t off, uint32_t value)
{
	htab[off + 0] = (uint8_t)(value >> 24);
	htab[off + 1] = (uint8_t)(value >> 16);
	htab[off + 2] = (uint8_t)(value >> 8);
	htab[off + 3] = (uint8_t)value;
}

static uint32_t nw_htab_load32(const uint8_t *htab, uint32_t off)
{
	return ((uint32_t)htab[off + 0] << 24) |
	       ((uint32_t)htab[off + 1] << 16) |
	       ((uint32_t)htab[off + 2] << 8) |
	       (uint32_t)htab[off + 3];
}

static int nw_htab_insert_pte(uint8_t *htab, size_t htab_size, uint32_t sdr1,
			      uint32_t vsid, uint32_t ea, uint32_t rpn)
{
	const uint32_t page_index = (ea >> 12) & 0xffffu;
	const uint32_t api = (ea >> 22) & 0x3fu;
	const uint32_t hash0 = (vsid & 0x7ffffu) ^ page_index;
	const uint32_t htaborg = sdr1 & 0xffff0000u;
	const uint32_t htabmask = ((sdr1 & 0x1ffu) << 16) | 0xffffu;
	const uint32_t w0 = 0x80000000u | ((vsid & 0x00ffffffu) << 7) | api;
	const uint32_t w1 = rpn << 12;

	for (int hash_id = 0; hash_id < 2; hash_id++) {
		const uint32_t hash = hash_id ? (hash0 ^ 0x7ffffu) : hash0;
		const uint32_t pteg = htaborg | ((hash * 64u) & htabmask);
		if (pteg < htaborg || (uint32_t)(pteg - htaborg) + 64u > htab_size)
			continue;
		for (unsigned slot = 0; slot < 8; slot++) {
			const uint32_t off = (pteg - htaborg) + slot * 8u;
			const uint32_t cur = nw_htab_load32(htab, off);
			if (cur & 0x80000000u)
				continue;
			nw_htab_store32(htab, off, w0 | ((uint32_t)hash_id << 6));
			nw_htab_store32(htab, off + 4, w1);
			return 1;
		}
	}
	return 0;
}

void nw_htab_program_rom_ptes(uint8_t *htab, size_t htab_size, uint32_t sdr1,
			      uint32_t rom_base, uint32_t rom_size, uint32_t vsid)
{
	if (htab == NULL || htab_size < 64 || sdr1 == 0 || rom_size < 0x1000u)
		return;
	if (vsid == 0)
		vsid = (rom_base >> 8) & 0x00ffffffu;
	memset(htab, 0, htab_size);
	const uint32_t last = rom_base + rom_size;
	for (uint32_t ea = rom_base; ea + 0x1000u <= last && ea >= rom_base; ea += 0x1000u)
		nw_htab_insert_pte(htab, htab_size, sdr1, vsid, ea, ea >> 12);
}

void nw_fill_dsi_vector_be(uint8_t *mem, size_t mem_size, uint32_t handler)
{
	if (mem == NULL || mem_size < (size_t)NW_DSI_VECTOR_EA + 0x18u)
		return;
	/* mtsprg 1,r1; mtsprg 2,lr; lis/ori r1,handler; mtlr r1; blr */
	nw_be32_store(mem, NW_DSI_VECTOR_EA + 0x00, (uint32_t)NW_DSI_VEC_MTSPRG1);
	nw_be32_store(mem, NW_DSI_VECTOR_EA + 0x04, (uint32_t)NW_DSI_VEC_MTSPRG2);
	nw_be32_store(mem, NW_DSI_VECTOR_EA + 0x08, 0x3c200000u | (handler >> 16));
	nw_be32_store(mem, NW_DSI_VECTOR_EA + 0x0c, 0x60210000u | (handler & 0xffffu));
	nw_be32_store(mem, NW_DSI_VECTOR_EA + 0x10, (uint32_t)NW_DSI_VEC_MTLR);
	nw_be32_store(mem, NW_DSI_VECTOR_EA + 0x14, (uint32_t)NW_DSI_VEC_BLR);
}
