/*
 *  nw_boot_contract.h - New World / nanokernel v2 boot contract (G1)
 *
 *  Host-checkable. No guest ROM is required to assert the layout.
 *  Locked G1: root compatible MacRISC2, Gestalt 406, /memory /cpus /chosen,
 *  Hnfo-or-mtsdr1 HTAB gate, BATRangeInit at KDP+0x2cc, saveKernelDataPtr
 *  immediately after saveReturnAddr. Do not require mfsdr1.
 */

#ifndef NW_BOOT_CONTRACT_H
#define NW_BOOT_CONTRACT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	NW_GESTALT_MACHINE_ID = 406,
	NW_ROM_SIZE = 0x400000,
	NW_NEWWORLD_SIG_OFFSET = 0x30d064,
	NW_CONFIGINFO_OFFSET = 0x30d000,
	NW_NK_V2_OFFSET = 0x310000,
	NW_KDP_PAGE_SIZE = 0x1000,

	/* NKProcessorState trampoline (elliotnunn/NanoKernel). DO NOT reorder. */
	NW_KDP_SAVE_RETURN_ADDR = 0x074,
	NW_KDP_SAVE_KERNEL_DATA_PTR = 0x078,

	/* NKConfigurationInfo overlay when LA_InfoRecord == KDP. */
	NW_KDP_BAT_RANGE_INIT = 0x2cc,
	NW_KDP_BAT_RANGE_INIT_LONGS = 32,

	/* KDP HTAB (NKKernelDataPriv). */
	NW_KDP_PTEGMASK = 0x6a0,
	NW_KDP_HTABORG = 0x6a4,

	/* SheepShaver OF / NKHWInfo seed at KDP+0xb80. Signature is +0x070. */
	NW_KDP_HWINFO_BASE = 0xb80,
	NW_KDP_HNFO_SIGNATURE = 0xb80 + 0x070,	/* 0xbf0 */
	NW_KDP_HNFO_HTAB_SDR1 = 0xb80 + 0x09c,	/* HTAB in the Hnfo block */

	NW_HNFO_SIGNATURE = 0x486e666f,		/* 'Hnfo' */
	NW_DEFAULT_HTABORG = 0x00100000,
	NW_DEFAULT_PTEGMASK = 0x0000ffff,	/* 64 KiB HTAB */
	NW_DEFAULT_SDR1 = 0x00100000
};

enum nw_decoded_rom_kind {
	NW_DECODED_UNKNOWN = 0,
	NW_DECODED_OLDWORLD,
	NW_DECODED_NEWWORLD
};

struct nw_kdp_params {
	uint32_t kdp_ea;
	uint32_t ram_base;
	uint32_t ram_size;
	uint32_t rom_base;
	uint32_t htaborg;
	uint32_t ptegmask;
	uint32_t sdr1;
};

struct nw_htab_gate {
	int hnfo_valid_htab;	/* 'Hnfo' + HTAB already in the Hnfo/KDP block */
	int spr_log_mtsdr1;	/* mtsdr1 seen in the SPR log */
};

struct nw_of_node_spec {
	const char *parent;		/* empty string = device-tree root */
	const char *name;
	const char *device_type;	/* NULL if none */
	const char *compatible;		/* NULL if none */
	const char *model;		/* NULL if none */
};

/* Root compatible is MacRISC2. Gestalt is always 406; identity is model. */
const char *nw_root_compatible(void);
const char *nw_root_model(void);
uint32_t nw_gestalt_machine_id(int is_newworld);

enum nw_decoded_rom_kind nw_detect_decoded_rom(const uint8_t *rom, size_t size);
int nw_rom_type_is_newworld(int rom_type);	/* matches ROMTYPE_NEWWORLD */

const struct nw_of_node_spec *nw_of_tree_spec(size_t *count);
int nw_of_tree_has_required_nodes(void);

void nw_fill_kdp_be(uint8_t *page, size_t page_len, const struct nw_kdp_params *p);

int nw_kdp_save_ptrs_adjacent(const uint8_t *page);
int nw_kdp_bat_range_init_present(const uint8_t *page);
int nw_kdp_hnfo_valid_htab(const uint8_t *page);
int nw_htab_gate_pass(const struct nw_htab_gate *gate);

uint32_t nw_be32_load(const uint8_t *mem, uint32_t off);
void nw_be32_store(uint8_t *mem, uint32_t off, uint32_t value);

#ifdef __cplusplus
}
#endif

#endif
