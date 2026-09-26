/*
 *  nw_devices.h - New World device models (mac99 pieces the NK and the
 *                 System's native code drive directly)
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

#ifndef NW_DEVICES_H
#define NW_DEVICES_H

#include <stdint.h>

/*
 * Small device models behind nw_io. Host-backed drivers do the I/O, these models exist only because
 * native code addresses the registers itself:
 *
 *   OpenPIC (Keylargo MPIC, little-endian, 0x80040000): the NK's interrupt
 *   path (source mask/priority, CTPR, IACK, EOI) and the Multiprocessing
 *   CPU plugin (IPI vectors, CTPR). 64 external sources, 4 IPIs, 4 timers.
 *   Its INT output is nw_io_ext_irq; device models raise sources with
 *   nw_openpic_set_irq().
 *
 *   Keylargo timer (0x80015000, little-endian): a free-running 18.432 MHz
 *   counter at +0x38 (low) / +0x3c (high). The CPU plugin's
 *   CalculateBusClock measures the decrementer against it and hands the
 *   result to NKSetPrInfoClockRates; derived from the emulated timebase so
 *   the measurement is exact.
 *
 *   uni-n (0xf8000000, big-endian): register file that reads back what was
 *   written; +0 is the version (0x7, as QEMU's mac99 reports).
 *
 *   uni-north PCI host bridge (pci@f2000000): CONFIG_ADDR at 0xf2800000
 *   latches what is written and reads it back (the CPU plugin's
 *   WaitForZeroPCI / CalculateBusClock write a marker there and spin on
 *   the readback to time the bus); CONFIG_DATA at 0xf2c00000 serves the
 *   configuration headers of the devices the boot-info tree declares
 *   (host bridge, mac-io, display), 0xffffffff for empty slots.
 *
 *   VIA-PMU (0x80016000, 6522 registers at stride 0x200, byte access):
 *   the ROM's native PMU driver and the RTC/NVRAM/ADB plugins run the
 *   PMU handshake themselves (port B TREQ/TACK, shift register, ACR
 *   direction, SR interrupt), so the 6522 and the PMU command protocol are
 *   modelled as QEMU's via-pmu does: a request/response state machine
 *   driven by port-B writes, with responses for the commands the boot
 *   issues (interrupt ack/mask, RTC read/set, version, power events,
 *   cover, download status, ADB packets; others answer zeros of the
 *   documented length). T1/T2 count at the PMU VIA rates; the VIA
 *   IRQ (IFR & IER) is OpenPIC source 0x19, the PMU's own interrupt
 *   (intbits & intmask: one-second tick, ADB data) is GPIO 1 -> OpenPIC
 *   0x2f.
 *
 *   ADB behind the PMU (the mac99 `via=pmu-adb` machine; the boot-info
 *   tree declares via-pmu/adb with keyboard and mouse children): the host
 *   keyboard and mouse are ADB devices at addresses 2 (keyboard, handler
 *   1..3) and 3 (mouse, handler 1..2) with register 3 = {address,
 *   handler}, register 0 = data, as QEMU's adb-kbd/adb-mouse. PMU command
 *   0x20 carries one ADB packet {command, flags, len, data...}; the
 *   special form {0, 0x86, mask.hi, mask.lo} sets the autopoll mask (0
 *   stops it); 0x21 stops autopoll. Replies and autopoll data arrive as
 *   PMU interrupts: INT_ACK answers {0x10 | 0x04 if autopoll, [0x01, len,]
 *   ADB command byte for autopoll, data...} (0x00 alone for a packet with
 *   no reply). Autopoll runs every 20 ms of timebase while no PMU command
 *   is in flight, one device per poll, the same device again while it
 *   has data. Key data: {code | 0x80 on release, 0xff}; mouse data:
 *   {dy | !button<<7, dx | 0x80}, deltas clipped to +-63.
 *
 *   Keylargo GPIO (0x80000050): 8 read-only level bytes then 36 pin
 *   registers (bit 0 out data, bit 1 in data, bit 2 output enable). Pin 1
 *   is the PMU interrupt (level low), pin 9 the NMI (edge), wired to
 *   OpenPIC 0x2f / 0x37.
 *
 *   Display VBL: the display node (pci slot e) has a vertical-blank
 *   interrupt, OpenPIC 0x1d (uni-north's line for that slot in the mac99
 *   layout), level-sensitive: asserted every 1/60 s of timebase while
 *   the device's VBL interrupt is enabled (nw_display_vbl_enable(), the
 *   driver's cscSetInterrupt), cleared by the driver's handler
 *   (nw_display_vbl_clear()). SheepShaver's video ndrv installs its handler
 *   through the Interrupt Manager (driver-ist, InstallInterruptFunctions,
 *   the member's enabler) like any PCI ndrv.
 *
 * All clocks derive from one timebase so guest measurements agree with
 * mftb/DEC.
 */
struct nw_devices_clock {
	uint64_t (*ticks)(void *ctx);	/* timebase ticks */
	void *ctx;
	uint32_t hz;			/* timebase frequency */
};

/* Registers the models with nw_io. Call once, before the guest runs. */
void nw_devices_init(const struct nw_devices_clock *tb);
/* Periodic work (OpenPIC timers); call from the CPU's coarse tick. */
void nw_devices_note_pc(uint32_t pc);
void nw_devices_tick(void);

enum nw_pmu_power_event {
	NW_PMU_POWER_RESTART = 0,
	NW_PMU_POWER_OFF = 1
};

/* Invoked from nw_devices_tick() after the PMU command handshake completes. */
void nw_pmu_set_power_hook(void (*hook)(int event, void *ctx), void *ctx);

/* Display VBL interrupt enable (the video driver's cscSetInterrupt) and
 * status clear (its interrupt handler). */
void nw_display_vbl_enable(int on);
void nw_display_vbl_clear(void);

/* OpenPIC external source n (0..63): level 1 asserts, 0 deasserts. Edge
 * sources (IVPR sense = 0) latch on the rising edge. */
void nw_openpic_set_irq(int n, int level);

/* Register access with native-endian values (the nw_io handlers byte-swap
 * for the little-endian bus). Exposed for the harness. */
uint32_t nw_openpic_read(uint32_t off);
void nw_openpic_write(uint32_t off, uint32_t value);
uint32_t nw_keylargo_timer_read(uint32_t off);
uint32_t nw_unin_read(uint32_t off, int size);
/* PCI configuration space: `devfn` = slot << 3 | function on bus 0, `reg`
 * the byte offset; bytes in PCI (little-endian) order. */
uint32_t nw_pci_config_read(uint32_t bus, uint32_t devfn, uint32_t reg, int size);
/* The latched CONFIG_ADDR value, little-endian interpretation. */
uint32_t nw_pci_config_addr(void);
void nw_unin_write(uint32_t off, int size, uint32_t value);
/* 6522 register `reg` (0..15) of the PMU VIA, byte values. */
uint32_t nw_via_read(uint32_t reg);
void nw_via_write(uint32_t reg, uint32_t value);
/* PMU protocol state for the harness: 0 idle, 1 receiving command, 2 sending response. */
int nw_pmu_state(void);
/* Host input into the ADB devices. Safe to call from the UI thread while
 * the CPU thread polls: one producer, one consumer. `code` is the Mac
 * (= ADB raw) key code 0..0x7f; `button` 0 is the primary button. */
void nw_adb_key(uint8_t code, int down);
void nw_adb_mouse_move(int dx, int dy);
void nw_adb_mouse_button(int button, int down);
void nw_adb_mouse_clear_delta(void);
/* Keylargo GPIO pin register n (0..35) and pin input drive. */
uint32_t nw_gpio_read(uint32_t n);
void nw_gpio_set(uint32_t n, int state);

enum {
	NW_OPENPIC_FRR = 0x1000,
	NW_OPENPIC_GCR = 0x1020,
	NW_OPENPIC_VIR = 0x1080,
	NW_OPENPIC_PIR = 0x1090,
	NW_OPENPIC_IPIVPR0 = 0x10a0,
	NW_OPENPIC_SPVE = 0x10e0,
	NW_OPENPIC_TFRR = 0x10f0,
	NW_OPENPIC_TIMER0 = 0x1100,		/* TCCR +0, TBCR +0x10, TVPR +0x20, TDR +0x30; stride 0x40 */
	NW_OPENPIC_SRC0 = 0x10000,		/* IVPR +0, IDR +0x10; stride 0x20 */
	NW_OPENPIC_CPU0 = 0x20000,		/* IPIDR0 +0x40.., CTPR +0x80, WHOAMI +0x90, IACK +0xa0, EOI +0xb0 */
	NW_OPENPIC_NSRC = 64,
	NW_OPENPIC_NIPI = 4,
	NW_OPENPIC_NTMR = 4,
	NW_OPENPIC_IPI0 = 64,
	NW_OPENPIC_TMR0 = 68,
	NW_OPENPIC_NIRQ = 72,
	NW_OPENPIC_FRR_VALUE = 0x003f0002,	/* 63 = last source, 1 CPU, VID 2 */
	NW_OPENPIC_TFRR_HZ = 4160000,
	NW_OPENPIC_IVPR_MASK = (int)0x80000000,
	NW_OPENPIC_IVPR_ACTIVITY = 0x40000000,
	NW_OPENPIC_IVPR_MODE = 0x20000000,
	NW_OPENPIC_IVPR_POLARITY = 0x00800000,
	NW_OPENPIC_IVPR_SENSE = 0x00400000,
	NW_OPENPIC_IVPR_PRIORITY = 0x000f0000,
	NW_OPENPIC_IVPR_VECTOR = 0x000000ff,
	NW_OPENPIC_GCR_RESET = (int)0x80000000,
	NW_OPENPIC_GCR_MODE = 0x20000000,
	NW_OPENPIC_TBCR_CI = (int)0x80000000,
	NW_OPENPIC_TCCR_TOG = (int)0x80000000,
	NW_KEYLARGO_TIMER_HZ = 18432000,
	NW_UNIN_VERSION = 0x7,
	/* 6522 */
	NW_VIA_B = 0, NW_VIA_A = 1, NW_VIA_DIRB = 2, NW_VIA_DIRA = 3,
	NW_VIA_T1CL = 4, NW_VIA_T1CH = 5, NW_VIA_T1LL = 6, NW_VIA_T1LH = 7,
	NW_VIA_T2CL = 8, NW_VIA_T2CH = 9, NW_VIA_SR = 10, NW_VIA_ACR = 11,
	NW_VIA_PCR = 12, NW_VIA_IFR = 13, NW_VIA_IER = 14, NW_VIA_ANH = 15,
	NW_VIA_REG_SHIFT = 9,			/* register index = (offset >> 9) & 0xf */
	NW_VIA_IFR_SR = 0x04, NW_VIA_IFR_T2 = 0x20, NW_VIA_IFR_T1 = 0x40,
	NW_VIA_IER_SET = 0x80,
	NW_VIA_ACR_SR_OUT = 0x10, NW_VIA_ACR_T1MODE = 0x40,
	NW_VIA_T1_HZ = 4700000 / 6,
	NW_VIA_T2_HZ = 6000000 / 4700,		/* as QEMU's via-pmu clocks T2 */
	/* PMU */
	NW_PMU_TACK = 0x08,			/* port B, PMU -> CPU */
	NW_PMU_TREQ = 0x10,			/* port B, CPU -> PMU */
	NW_PMU_INT_ADB = 0x10, NW_PMU_INT_TICK = 0x80,
	NW_PMU_INT_ADB_AUTO = 0x04,		/* with INT_ADB: autopoll data, not a reply */
	/* ADB */
	NW_ADB_BUSRESET = 0x0, NW_ADB_FLUSH = 0x1, NW_ADB_WRITEREG = 0x8, NW_ADB_READREG = 0xc,
	NW_ADB_KBD_ADDR = 2, NW_ADB_MOUSE_ADDR = 3,
	NW_ADB_POLL_MS = 20,
	NW_PMU_INT_ACK = 0x78, NW_PMU_SET_INTR_MASK = 0x70, NW_PMU_ADB_CMD = 0x20,
	NW_PMU_ADB_POLL_OFF = 0x21, NW_PMU_RESET = 0xd0, NW_PMU_SHUTDOWN = 0x7e,
	NW_PMU_READ_RTC = 0x38, NW_PMU_SET_RTC = 0x30, NW_PMU_SYSTEM_READY = 0xdf,
	NW_PMU_GET_VERSION = 0xea, NW_PMU_POWER_EVENTS = 0x8f, NW_PMU_GET_COVER = 0xdc,
	NW_PMU_DOWNLOAD_STATUS = 0xe2, NW_PMU_READ_PMU_RAM = 0xe8,
	NW_PMU_RTC_OFFSET = (int)2082844800u,	/* 1904 -> 1970 */
	NW_PMU_IRQ = 0x19,			/* OpenPIC sources */
	NW_GPIO1_IRQ = 0x2f, NW_GPIO9_IRQ = 0x37,
	NW_VBL_IRQ = 0x1d, NW_VBL_HZ = 60,
	NW_GPIO_OUT_DATA = 1, NW_GPIO_IN_DATA = 2, NW_GPIO_OUT_ENABLE = 4,
	NW_GPIO_NPINS = 36
};

#endif
