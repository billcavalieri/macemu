/*
 *  nw_boot_contract.h - New World / nanokernel v2 boot contract (G0–G2)
 *
 *  Host-checkable. No guest ROM is required to assert the layout.
 *  Locked G1: root compatible MacRISC2, Gestalt 406, /memory /cpus /chosen,
 *  Hnfo-or-mtsdr1 HTAB gate, BATRangeInit at KDP+0x2cc, saveKernelDataPtr
 *  immediately after saveReturnAddr. Do not require mfsdr1.
 *
 *  S4 (branch newworld-boot): this header carries no mill. No skip lists,
 *  no planted code, no host 68k dispatch, no A-trap stubs. The CPU raises
 *  exceptions architecturally and logs them in the golden event grammar
 *  (see research-score/golden/nwgolden.c) so newworldview `diff` can name
 *  the first divergence against QEMU mac99.
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

/* PowerPC MSR bits used by the boot log. */
enum {
	NW_MSR_EE = 0x00008000,
	NW_MSR_IR = 0x00000020,
	NW_MSR_DR = 0x00000010,
	NW_MSR_ME = 0x00001000,
	NW_MSR_IP = 0x00000040,
	NW_MSR_VEC = 0x02000000
};

/* Exception vectors (OEA). */
enum {
	NW_VEC_MACHINE_CHECK = 0x200,
	NW_VEC_DSI = 0x300,
	NW_VEC_ISI = 0x400,
	NW_VEC_EXTERNAL = 0x500,
	NW_VEC_ALIGNMENT = 0x600,
	NW_VEC_PROGRAM = 0x700,
	NW_VEC_FPU = 0x800,
	NW_VEC_DECREMENTER = 0x900,
	NW_VEC_SYSCALL = 0xc00,
	NW_VEC_TRACE = 0xd00,
	NW_VEC_VPU = 0xf20		/* AltiVec unavailable (MSR[VEC] = 0) */
};

/*
 * 68k emulator inside the ROM (PPC part). The four A-line handlers all
 * start with `lwz r5,0x28(r28)`; at entry op = (r29 >> 3) & 0xffff and
 * the A-line word is at r24 - 2. Same constants as nwgolden.c.
 */
enum {
	NW_EMU_ALINE_OS_FLAG = 0x3695e0,	/* OS trap, bit 8 set */
	NW_EMU_ALINE_OS = 0x369660,
	NW_EMU_ALINE_TOOL = 0x369720,
	NW_EMU_ALINE_TOOL_AUTOPOP = 0x369780,
	NW_EMU_ALINE_ENTRY_OP = 0x80bc0028
};
/* 0..3 handler index for a PC at ROM offset `off`, -1 if not an A-line entry. */
int nw_emu_aline_handler(uint32_t off);

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

/*
 * Host-side G0 decode. dest must be NW_ROM_SIZE (4 MiB).
 * Accepts a plain 4 MiB image, CHRP lzss (ROM 1.6), or CHRP parcels/prcl
 * (9.2.1 install-set tbxi). Returns 1 on success.
 */
int nw_decode_rom_image(const uint8_t *src, size_t src_size,
			uint8_t *dest, size_t dest_size);
int nw_g0_unpacked_ok(const uint8_t *rom, size_t size);

const struct nw_of_node_spec *nw_of_tree_spec(size_t *count);
int nw_of_tree_has_required_nodes(void);

void nw_fill_kdp_be(uint8_t *page, size_t page_len, const struct nw_kdp_params *p);

/*
 * G1 handoff: the Trampoline's job. On a real New World Mac the boot
 * script's Trampoline completes NKConfigurationInfo (page map, segment
 * maps, BAT ranges, relocated low memory) and NKSystemInfo (physical RAM
 * banks) before jumping to the NK. The ROM's own ConfigInfo has an empty
 * page map (PageMapInitSize 0), so an unfilled handoff makes NK v2 read
 * garbage PMDTs and panic at "Converting PMDTs to areas" (ROM+0x31e878).
 *
 * Layout the fill describes (SheepShaver: logical == physical for ROM):
 *   PA 0x0000..0x2fff   exception vector stubs (copy of ROM+0x300000)
 *   PA 0x3000..0x3fff   the filled ConfigInfo (copy of ROM+0x30d000 after
 *                       nw_fill_config_info_be), NW_CI_PA
 *   PA ram_base         RAM bank 0; Mac low memory (LA 0) is its first
 *                       pages (PA_RelocatedLowMemInit == ram_base), so
 *                       logical RAM is LA x -> PA ram_base + x
 *   LA rom_base         ROM area, identity, rom_area_size (5 MiB)
 *   LA 0x68000000       1 MiB -> PA rom_base+0x300000 (NK, emulator,
 *                       dispatch table in place; ROM LA_* defaults kept)
 *   LA_InfoRecord/LA_KernelData/LA_EmulatorData: ROM defaults; the NK
 *                       fills their PAs from its own top-of-RAM block.
 * Reference: QEMU mac99 golden dump of the Trampoline-built ConfigInfo.
 */
enum {
	NW_CI_SIZE = 0x1000,
	NW_CI_PAGEMAP_OFF = 0x3ac,	/* where the page map is placed */
	NW_CI_PAGEMAP_MAX = 0x40,	/* entries (0x3ac + 0x40*8 < 0xff4) */
	NW_SI_SIZE = 0x140,		/* NKSystemInfo bytes the NK copies */
	NW_NK_EXC_TABLE_ROM_OFF = 0x300000,
	NW_68K_ROM_LA = 0xffc00000u,	/* 68k ROM window (ROM's own reset PC 0xffc0002a) */
	NW_NK_EXC_TABLE_COPY_LEN = 0x2800,	/* vectors 0x100..0x27ff; 0x2800.. is XLM */
	NW_NK_LOWMEM_ZEROED = 0x2000,	/* NK clears this much low memory */
	NW_CI_LA = 0x68fef000u,		/* ConfigInfo page as the 68k/NK see it (RO) */
	/* Where the Trampoline leaves the filled ConfigInfo: the page after
	 * the exception vectors (golden hardware-info +0xc == 0x3000). The NK
	 * hard-codes this PA when it turns an IACK vector into a 68k interrupt
	 * level (`lbz level, 0x3f00(vector)` in its external interrupt
	 * handler), so the page must really be there, not only mapped at
	 * NW_CI_LA. r3 at NK entry points here too. */
	NW_CI_PA = 0x3000,
	/* Trampoline boot-info area: LA 0x64000000, 384 pages. Holds the
	 * 'PMR&' header, the flattened device tree ('BGsTree'), the driver
	 * parcels (nw_bootinfo.h), and the ProductInfo/DecoderInfo record the
	 * 68k StartInit reaches through hardware-info +0x8 (golden: right after
	 * the tree at +0x51dd0; here at a fixed 1 MiB so the tree may grow). */
	NW_BOOTINFO_LA = 0x64000000u,
	NW_BOOTINFO_SIZE = 0x180000,
	NW_BOOTINFO_HWREC_OFF = 0x100000,
	NW_BOOTINFO_HWREC_PRE = 0x28,	/* bytes before the record the 68k indexes negatively */
	NW_BOOTINFO_HWREC_LEN = 0x1c0,
	NW_BOOTINFO_TREE_MAX = NW_BOOTINFO_HWREC_OFF - NW_BOOTINFO_HWREC_PRE,
	/* Hardware-info block: r9 at NK entry when r7 == 'RTAS'; the NK copies
	 * 0xc0 bytes to IRP+0xf00 and publishes it at KDP+0xfd0; the 68k checks
	 * 'Hnfo' at +0x70. */
	NW_HWINFO_SIZE = 0xc0,
	NW_HWINFO_MAGIC_R7 = 0x52544153u	/* 'RTAS' */
};

/* Extra page-mapped ranges (page aligned), e.g. host areas the guest is
 * handed pointers into: SheepMem thunks, DR cache, video frame buffer. */
struct nw_pmdt_range {
	uint32_t la;
	uint32_t pa;
	uint32_t size;
};

struct nw_config_info_layout {
	uint32_t rom_base;		/* ROM image LA == PA */
	uint32_t rom_area_size;	/* ROM_AREA_SIZE (5 MiB) */
	uint32_t ram_base;		/* PA of the RAM bank; also relocated low-memory PA */
	uint32_t ram_size;
	uint32_t ci_pa;			/* PA of the filled ConfigInfo page (NW_CI_PA); mapped RO at NW_CI_LA */
	uint32_t bootinfo_pa;	/* PA of the NW_BOOTINFO_SIZE boot-info area (0 = none) */
	const struct nw_pmdt_range *extra;
	int n_extra;
};

/*
 * OpenPIC sources the Trampoline collects from the device tree
 * (AAPL,interrupt-vectors / -priorities), in the order it lists them in the
 * ConfigInfo tail (+0xf80 source list, +0xd40/+0xf00 priorities) and
 * programs them into the controller: IVPR = masked | priority | vector ==
 * position in that list (the tree's AAPL,interrupt-index), level sense per
 * the interrupt specifier, IDR = CPU 0, CTPR 0. The NK's external
 * interrupt handler turns the IACK vector into a 68k interrupt level with
 * `lbz level, 0xf00(vector)` on the ConfigInfo page, i.e. the priority
 * byte table in list order, and only levels 1..7 are signalled to the
 * emulator; a vector equal to the source number reads level 0 and the
 * interrupt is queued forever. The 68k StartInit then only toggles the mask
 * bits (bset/bclr #7 on the low-address byte of the little-endian
 * register), so without this programming every source keeps priority 0 and
 * nothing is ever delivered. Golden mac99 order and priorities, minus the
 * sources of nodes our tree does not carry (escc); the tree's
 * AAPL,interrupt-index values are positions in this list.
 */
struct nw_irq_source {
	uint8_t src;
	uint8_t prio;
	uint8_t level;
};
enum { NW_TRAMPOLINE_NIRQ = 10 };
extern const struct nw_irq_source nw_trampoline_irqs[NW_TRAMPOLINE_NIRQ];
/* Writes the table into the OpenPIC model (nw_devices.h); call after
 * nw_devices_init(), before the guest runs. */
void nw_trampoline_program_pic(void);

/* ci: the 4 KiB ROM ConfigInfo (ROM+0x30d000), big-endian, patched in place.
 * Returns the number of page-map entries written, or -1 on bad layout. */
int nw_fill_config_info_be(uint8_t *ci, const struct nw_config_info_layout *l);
/* hw: NW_HWINFO_SIZE bytes (r9). Pointers into the ConfigInfo page and the
 * boot-info area use their guest LAs (NW_CI_LA, NW_BOOTINFO_LA). */
void nw_fill_hwinfo_be(uint8_t *hw, const struct nw_config_info_layout *l);
/* area: NW_BOOTINFO_SIZE bytes, zeroed; writes the 'PMR&' header and the
 * ProductInfo/DecoderInfo record (I/O bases as on mac99: VIA 0x80016000,
 * SCC 0x80012000, OpenPIC 0x80040000). The device tree and parcels are
 * added by nw_bootinfo_build_tree() (nw_bootinfo.h) afterwards. */
void nw_fill_bootinfo_be(uint8_t *area, uint32_t size, const struct nw_config_info_layout *l);
/* Apple ROM-file LZSS (parcel payloads, ROM image). Stops at src_size input
 * bytes or dst_size output bytes. */
void nw_lzss_decode(const uint8_t *src, size_t src_size, uint8_t *dst, size_t dst_size);

/*
 * Host-side "Mac addresses" under New World.
 *
 * Old World: the guest's logical addresses are host addresses (RAM at
 * RAMBase, low memory at 0). New World: the NK owns LA -> PA and the 68k
 * sees RAM at LA 0 (PA RAMBase + LA), the ROM at LA 0xffc00000, the kernel
 * data page at LA 0x68ffe000 (PA chosen by the NK). Host code that handles
 * guest data (EMUL_OP drivers, Execute68k, the XLM globals at 0x2800) speaks
 * LAs; cpu_emulation.h routes every Mac accessor through nw_la_to_pa().
 * Host-owned areas the guest reaches by identity mapping (SheepMem, the
 * frame buffer, the boot-info area, the ROM image at ROMBase) translate to
 * themselves. Inactive (identity everywhere) until nw_la_enable().
 */
extern uint32_t nw_la_ram_base, nw_la_ram_size, nw_la_rom_base, nw_la_kdp_pa;
void nw_la_enable(uint32_t ram_base, uint32_t ram_size, uint32_t rom_base);
/* Host area the guest executes from besides RAM and ROM: SheepMem, where
 * SheepShaver's guest-callable thunks (NativeOp TVECTs, the CallMacOS
 * return trampoline, 68k procedures) live. Identity mapped for the NK
 * (rom_patches.cpp) and accepted by the CPU's fetch guard. */
extern uint32_t nw_thunk_area_base, nw_thunk_area_size;

static inline uint32_t nw_la_to_pa(uint32_t la)
{
	if (nw_la_ram_size == 0)
		return la;
	if (la < nw_la_ram_size)
		return la + nw_la_ram_base;
	if (la >= 0xffc00000u)
		return nw_la_rom_base + (la - 0xffc00000u);
	if (nw_la_kdp_pa != 0 && la - 0x68ffe000u < 0x2000u)
		return nw_la_kdp_pa + (la - 0x68ffe000u);
	return la;
}

static inline uint32_t nw_pa_to_la(uint32_t pa)
{
	if (nw_la_ram_size == 0)
		return pa;
	if (pa - nw_la_ram_base < nw_la_ram_size)
		return pa - nw_la_ram_base;
	if (nw_la_kdp_pa != 0 && pa - nw_la_kdp_pa < 0x2000u)
		return 0x68ffe000u + (pa - nw_la_kdp_pa);
	return pa;
}
/* si: NW_SI_SIZE bytes, zeroed and filled. */
void nw_fill_system_info_be(uint8_t *si, const struct nw_config_info_layout *l);
/*
 * NKProcessorInfo (r4, NW_PI_SIZE bytes, copied by the NK to KDP+0xf20):
 * +0 PVR, +4 CPU Hz, +8 bus Hz, +0xc timebase Hz, +0x10 page size, then
 * L1 cache geometry (golden mac99 G4 values). The NK derives its timeslice
 * quantum from the timebase field; a zero record made every quantum expire
 * at once (DEC storm: SetDEC saw deadline == now).
 */
enum { NW_PI_SIZE = 0xa0 };
void nw_fill_processor_info_be(uint8_t *pi, uint32_t pvr, uint32_t cpu_hz,
			       uint32_t bus_hz, uint32_t tb_hz);
/* Page-map sanity: per segment ascending page indices, exactly one
 * terminator per segment, IRP/KDP/EDP offsets point at 1-page entries. */
int nw_config_info_pagemap_ok(const uint8_t *ci);

int nw_kdp_save_ptrs_adjacent(const uint8_t *page);
int nw_kdp_bat_range_init_present(const uint8_t *page);
int nw_kdp_hnfo_valid_htab(const uint8_t *page);
int nw_htab_gate_pass(const struct nw_htab_gate *gate);

uint32_t nw_be32_load(const uint8_t *mem, uint32_t off);
void nw_be32_store(uint8_t *mem, uint32_t off, uint32_t value);

/* Debug-only live boot log (NW_BOOT_LOG=1 on Xcode SheepShaver Debug). */
const char *nw_boot_line_credits(void);
const char *nw_boot_line_g0_newworld(void);
const char *nw_boot_line_g1_tree(void);
const char *nw_boot_line_g1_kdp(void);
const char *nw_boot_line_g1_mtsdr1(void);
const char *nw_boot_line_g1_hwinit(void);
const char *nw_boot_line_g1_patch_skip(void);
const char *nw_boot_line_g2_first_dsi(void);
const char *nw_boot_line_g2_translator_off(void);

void nw_boot_log(const char *line);
/* decoded = 4 MiB after DecodeROM; file = the ROM prefs bytes (CHRP or 4 MiB). */
void nw_log_g0_decode(const uint8_t *decoded, size_t decoded_size,
		      const uint8_t *file, size_t file_size);
void nw_format_g0_rom_line(char *buf, size_t bufn,
			   const uint8_t *decoded, size_t decoded_size,
			   const uint8_t *file, size_t file_size);
void nw_log_g1_tree(void);
void nw_log_g1_kdp(const uint8_t *page);
void nw_log_g1_hwinit(void);
void nw_log_g1_patch_skip(int is_newworld);
void nw_note_mtsdr1(void);
void nw_log_msr_dr(uint32_t msr);
void nw_log_msr_write(const char *how, uint32_t pc, uint32_t msr);
void nw_log_first_dsi(uint32_t srr0, uint32_t dar, int dr_on_hit);
void nw_log_translator_off(void);

/*
 * S4 event stream, same grammar as the QEMU golden plugin:
 *   NW-BOOT X E <srr0> <vector> [<dar>|<srr1>]   (DAR for 0x300/0x600, SRR1 for 0x700)
 *   NW-BOOT A <op> <68k-pc> <handler>
 * Every event is logged (no caps); newworldview `diff` consumes it.
 * `extra_valid` selects whether the trailing field is printed.
 */
void nw_event_exception(uint32_t srr0, uint32_t vector, uint32_t extra, int extra_valid);
void nw_event_aline(uint32_t op, uint32_t pc68k, int handler);
/* Periodic `T <epoch_ms> <nX> <nA> <pc> <msr> <nI> <nF>` tick, at most
 * once per second. pc/msr are extra fields (the golden importer ignores
 * them) so a silent spin still names where the CPU is. nI is interpreter
 * ops, nF is VideoHostPresent calls (Debug / NW_BOOT_LOG only). */
void nw_event_tick(uint32_t pc, uint32_t msr);
#if NW_BOOT_LOG
void nw_event_insn(void);
void nw_event_frame(void);
#endif

/* Host-side periodic work on the CPU thread for New World (window
 * presentation at 60 Hz; the classic path hangs this off the video driver's
 * VBL, which needs the Old World interrupt injection). sheepshaver_glue.cpp;
 * cheap to call from the coarse CPU tick, does nothing on Old World. */
void nw_host_tick(void);

#ifdef __cplusplus
}
#endif

#endif
