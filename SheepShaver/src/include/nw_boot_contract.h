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
	/* 9.2.1 NK HotInts DataStorageInt: mfsprg r1,0; stmw r2,8(r1). */
	NW_NK_DATA_STORAGE_INT = 0x3132a0,
	NW_NK_DATA_STORAGE_INT_OP = 0x7c3042a6, /* mfsprg r1, SPRG0 */
	/* Exception vector page copied to EA 0 (SPRG3 = VecTbl). */
	NW_NK_VEC_TEMPLATE = 0x300000,
	NW_NK_VEC_TEMPLATE_SIZE = 0x1000,
	NW_DSI_VECTOR_EA = 0x300,
	NW_DSI_VECTOR_SLOT = 0x100,		/* 0x300..0x3ff */
	NW_DSI_VEC_MTSPRG1 = 0x7c3143a6,	/* mtsprg 1,r1 */
	NW_DSI_VEC_MTSPRG2 = 0x7c3243a6,	/* mtsprg 2,lr */
	NW_DSI_VEC_MTLR = 0x7c2803a6,		/* mtlr r1 */
	NW_DSI_VEC_BLR = 0x4e800020,
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

int nw_kdp_save_ptrs_adjacent(const uint8_t *page);
int nw_kdp_bat_range_init_present(const uint8_t *page);
int nw_kdp_hnfo_valid_htab(const uint8_t *page);
int nw_htab_gate_pass(const struct nw_htab_gate *gate);

uint32_t nw_be32_load(const uint8_t *mem, uint32_t off);
void nw_be32_store(uint8_t *mem, uint32_t off, uint32_t value);

/* Debug-only live boot log (NW_BOOT_LOG=1 on Xcode SheepShaver Debug). */
const char *nw_boot_line_g0_newworld(void);
const char *nw_boot_line_g1_tree(void);
const char *nw_boot_line_g1_kdp(void);
const char *nw_boot_line_g1_mtsdr1(void);
const char *nw_boot_line_g1_hwinit(void);
const char *nw_boot_line_g1_patch_skip(void);
const char *nw_boot_line_g2_first_dsi(void);
const char *nw_boot_line_g2_translator_off(void);
const char *nw_boot_line_g3_sdl2_window(void);
const char *nw_boot_line_g3_irq_nk(void);	/* NK native IRQ after G2; not mill */
const char *nw_boot_line_g3_native_op(void);
const char *nw_boot_line_g3_dec_arm(void);
const char *nw_boot_line_g3_walk_dec_ee(void);
const char *nw_boot_line_g3_fb_guest(void);
const char *nw_boot_line_g3_fb_none(void);	/* named reason: EE=0, not host n=1 */

/*
 * Clip a dirty rect to the screen. Returns 1 if the result is non-empty.
 * Host-testable helper for SDL2 update_display_static_bbox / video_set_dirty_area.
 */
int nw_video_clip_dirty(int *x, int *y, int *w, int *h, int sw, int sh);

/*
 * Guest framebuffer vs copy. 1 if any byte differs. Host present of an
 * empty union is not this. Do not mill 68k.
 */
int nw_video_fb_guest_dirty(const uint8_t *fb, const uint8_t *copy,
			    size_t nbytes);

/*
 * Guest FB must live in RAM so after G2 the RAM BAT lets NQD /
 * VideoDoDriverIO / QD stores HIT the_buffer. vm_acquire_reserved is
 * outside that BAT. Offset is past HTAB and mill's NuBus 4MiB alias.
 * Live c81f88bd 50325600 is not a skip.
 */
enum {
	NW_GUEST_FB_RAM_OFF = 0x800000
};
int nw_video_fb_in_ram(uint32_t fb_ea, uint32_t ram_base, uint32_t ram_size,
			uint32_t fb_bytes);

/*
 * NK v2 is ROM+0x310000 .. ROM+0x360000. Mill 68k (ROM+0x366084) is not
 * in NK and is not G3. Live 50327b54 (ROM+0x327b54) is in NK. Do not mill
 * that walk. After G2, HandleInterrupt uses native 0x312b1c while PC is
 * in NK even if XLM_RUN_MODE is still MODE_68K (New World patch skip).
 */
int nw_ppc_pc_in_nk(uint32_t pc, uint32_t rom_base);
int nw_handle_interrupt_use_native(int first_dsi, uint32_t pc,
				     uint32_t rom_base);
int nw_handle_interrupt_skip_nested(int use_native, uint32_t r1,
				      uint32_t kdp);

/*
 * After G2, arm a short DEC so the 171-PC walk can take 0x900.
 * Reset DEC is 0x7fffffff and ticks 1/256 insns — that never underflows
 * in a live hang. Do not skip_after_g2 0x327b5x. mill 68k is not G3.
 */
enum {
	NW_MSR_EE = 0x00008000,
	NW_MSR_IR = 0x00000020,
	NW_DEC_ARM_AFTER_G2 = 0x1000,
	NW_MSR_LIVE_EE_OFF = 0x00002000	/* live c81f88bd at 50325600 */
};
uint32_t nw_dec_arm_value(void);
int nw_dec_take_after_g2(int first_dsi);
int nw_dec_ee_on(uint32_t msr);
/* take_dec after G2 requires EE+IR. Live 00002000 has neither.
 * Do not mill-skip the walk. Do not flip EE. */
int nw_dec_can_yield(uint32_t msr);
/*
 * After G2, EE/IR off so DEC never yields: guest never reaches
 * VideoDoDriverIO / NQD / FB writes. 1 = the_buffer==copy for that reason.
 */
int nw_video_guest_paint_blocked(int first_dsi, uint32_t msr,
				   int nqd, int dirty);
/*
 * After G2, NQD bbox is a hint not a clip. QD stores that skip NQD
 * still HIT the_buffer. Do not require EE. Do not mill.
 */
int nw_video_full_scan(int first_dsi);
/* Blit when the SDL surface is not the guest FB (16-bit). 8/32-bit
 * CreateRGBSurfaceFrom shares pixels — blit would be a no-op. */
int nw_video_need_blit(const void *pixels, const void *fb);
/*
 * After G2, present leftover SDL union even if this scan found 0
 * new boxes (redraw thread queued, VideoVBL never ran, EE=0).
 * Host n=1 of an empty union is not this.
 */
int nw_video_present_pending(int first_dsi, unsigned boxes,
			      int union_empty);

void nw_boot_log(const char *line);
void nw_log_g0_decode(const uint8_t *rom, size_t size);
void nw_log_g1_tree(void);
void nw_log_g1_kdp(const uint8_t *page);
void nw_log_g1_hwinit(void);
void nw_log_g1_patch_skip(int is_newworld);
void nw_note_mtsdr1(void);
void nw_log_msr_dr(uint32_t msr);
void nw_log_first_dsi(uint32_t srr0, uint32_t dar, int dr_on_hit);
void nw_log_translator_off(void);
void nw_log_pc(uint32_t pc, uint32_t msr);
void nw_log_dr_xlate(uint32_t pc, uint32_t ea, int ok, uint32_t pa,
		     uint32_t dbat0u, uint32_t dbat0l);

/*
 * Identity-map ROM pages into a caller-owned HTAB (VSID 0, 1:1 RPN).
 * Used so a later DR-on lwz of the faulting insn at SRR0 can HIT (G2).
 */
void nw_htab_program_rom_ptes(uint8_t *htab, size_t htab_size, uint32_t sdr1,
			      uint32_t rom_base, uint32_t rom_size, uint32_t vsid);
void nw_guest_seed_rom_htab(uint32_t sdr1);
/*
 * NK polls *(KDP-2272) as a PIC pointer; 0x3104a8 never stores one.
 * Live 93eb1588: 50-PC cloud left, then single-PC stick
 * heartbeat 5032582c ×2208 (ROM+0x32582c). Not mill. After first
 * data DSI, leave_npc must not land on 0x32582c or prior waits.
 * OLD_B npc=50325aac. Do not skip +0x325a14 before that DSI.
 * Do not jump PAST 0x326000 or mill +0x366084. PIC idle 0.
 */
enum {
	NW_NK_IRQ_KDP_OFF = 2272,
	NW_NK_IRQ_PIC_RAM_OFF = 0x10000,
	NW_NK_IRQ_STATUS_OFF = 2,
	NW_NK_IRQ_SPIN_BIT = 2,
	NW_NK_PICSPIN_LBZ = 0x325a14,
	NW_NK_PICSPIN_BEQ = 0x325a20,
	NW_NK_PICSPIN_LBZ_OP = 0x8bdc0002,	/* lbz r30,2(r28) */
	NW_NK_PICSPIN_BEQ_OP = 0x4182fc40,	/* beq */
	NW_NK_PICSPIN_OLD_A = 0x325998,
	NW_NK_PICSPIN_OLD_B = 0x325a9c,
	NW_NK_PICSPIN_OLD_C = 0x325c94,
	NW_NK_PICSPIN_PAST = 0x326000,	/* live b62e7717: went to mill; stay */
	NW_NK_PICSPIN_LEAVE_INSNS = 32,
	NW_NK_68K_EMUL = 0x360000,	/* Old World 68k mill; mill 68k is not G3 */
	NW_NK_IRQ_NATIVE = 0x312b1c,	/* New World NK interrupt entry */
	NW_NK_MILL_68K = 0x366084,	/* mill inner loop; not G3 / not WINDOW */
	NW_NK_CYCLE_A = 0x325c7c,	/* live e0df3b4e dominant */
	NW_NK_CYCLE_B = 0x325c44,
	NW_NK_CYCLE_C = 0x32570c,
	NW_NK_CYCLE_D = 0x325690,
	NW_NK_CYCLE_E = 0x325670,
	NW_NK_CYCLE_F = 0x325520,	/* NK debug print */
	NW_NK_CYCLE_G = 0x312728,
	NW_NK_CYCLE_H = 0x312708,
	NW_NK_PRINT_A = 0x32572c,
	NW_NK_PRINT_B = 0x325850,
	NW_NK_PRINT_C = 0x325874,
	NW_NK_CYCLE_OLD_PAST = 0x325c98,	/* 108235a0 PAST, in cluster */
	NW_NK_TAIL_A = 0x3127a8,	/* live b19886d6 3-PC loop */
	NW_NK_TAIL_B = 0x3127b8,
	NW_NK_TAIL_C = 0x3127c8,
	NW_NK_TAIL_LO = 0x3127a8,
	NW_NK_TAIL_HI = 0x3127c8,
	/* Live 92beda4a ~50-PC cloud after npc=503127cc. */
	NW_NK_CLOUD_A = 0x325584,	/* dominant */
	NW_NK_CLOUD_B = 0x326438,
	NW_NK_CLOUD_C = 0x3128bc,
	NW_NK_CLOUD_TAIL = 0x3127cc,	/* skip from 0x312728 landed here */
	NW_NK_CLOUD_MID = 0x325660,
	NW_NK_CLOUD_LO_0 = 0x312700,
	NW_NK_CLOUD_HI_0 = 0x312728,
	NW_NK_CLOUD_LO_1 = 0x31289c,
	NW_NK_CLOUD_HI_1 = 0x3128c0,
	NW_NK_CLOUD_LO_2 = 0x325554,
	NW_NK_CLOUD_HI_2 = 0x32558c,
	NW_NK_CLOUD_LO_3 = 0x3256ec,
	NW_NK_CLOUD_HI_3 = 0x32570c,	/* through CYCLE_C */
	NW_NK_CLOUD_LO_4 = 0x326420,
	NW_NK_CLOUD_HI_4 = 0x326448,
	NW_NK_STICK = 0x32582c,	/* live 93eb1588 heartbeat ×2208 */
	/*
	 * Live bedd28a3: 171 unique NK PCs after leaving 5032582c.
	 * Dominant 50327b54 (ROM+0x327b54)×33. Walk, not a stick.
	 * Do not skip_after_g2 this off. mill +0x366084 is not a skip.
	 */
	NW_NK_WALK_A = 0x327b54,
	NW_NK_WALK_B = 0x327b50,	/* live dee26adb ×38; not skip */
	NW_NK_WALK_C = 0x327b60,	/* live dee26adb ×37; not skip */
	NW_NK_WALK_EE = 0x325600,	/* live c81f88bd; do not skip */
	NW_NK_WALK_EE_N = 0x325604	/* live heartbeat +4; not skip */
};
uint32_t nw_nk_irq_pic_ea(uint32_t ram_base);
uint8_t nw_nk_irq_status_idle(void);
int nw_nk_irq_status_spins(uint8_t v);
int nw_ppc_is_branch(uint32_t op);
int nw_ppc_rel_branch_target(uint32_t pc, uint32_t op, uint32_t *target);
int nw_nk_picspin_rom_off(uint32_t off);
int nw_nk_picspin_cycle_off(uint32_t off);
int nw_nk_picspin_mill_off(uint32_t off);
int nw_nk_picspin_is_g2_dsi_off(uint32_t off);
int nw_nk_picspin_skip_after_g2(uint32_t off, uint32_t op);
int nw_nk_picspin_npc_stays(uint32_t npc, uint32_t from_pc, uint32_t rom_base);
uint32_t nw_nk_picspin_leave_npc(uint32_t pc, uint32_t rom_base,
				 const uint32_t *insns, unsigned n);
uint32_t nw_nk_picspin_past_npc(uint32_t pc, uint32_t rom_base);
void nw_nk_irq_fill_pic_be(uint8_t *mem, size_t mem_size, uint32_t pic_ea);
void nw_guest_plant_nk_irq(uint32_t kdp);
/* After first DSI: 1:1 BAT RAM+ROM so HotInts MemRetry can HIT under DR.
 * No-op until nw_guest_note_first_data_dsi() — planting at first IR+DR
 * covers KDP-1048 and swallows G2. */
void nw_guest_note_first_data_dsi(void);
int nw_guest_first_data_dsi_seen(void);
void nw_guest_map_ram_rom_identity(void);
void nw_guest_map_kernel_data(void);
void nw_log_xlatehow(const char *how, uint32_t ea, uint32_t msr, uint32_t sdr1,
		     uint32_t sr, uint32_t dbat3u, uint32_t dbat3l);
/* Write the DSI slot at EA 0x300 (host RAM image, no guest ROM). */
void nw_fill_dsi_vector_be(uint8_t *mem, size_t mem_size, uint32_t handler);
/* Guest: copy NK VecTbl DSI slot to PA 0x300; synthesize if template is empty. */
void nw_guest_plant_dsi_vector(void);
/* 68k inner loop at ROM+0x366084: table is ROM+0x380000 + opcode*8. */
int nw_guest_68k_dispatch(uint32_t *pc, uint32_t *r24, uint32_t *r27,
			  uint32_t *r29);

#ifdef __cplusplus
}
#endif

#endif
