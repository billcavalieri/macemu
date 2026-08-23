/*
 *  nw_boot_contract.cpp - New World / nanokernel v2 boot contract (G1)
 */

#include "nw_boot_contract.h"

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
