/*
 *  nw_devices.cpp - New World device models: OpenPIC, Keylargo timer, uni-n,
 *                   PCI config, Keylargo GPIO, VIA-PMU with its ADB bus
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

#include <string.h>
#include <time.h>
#include <atomic>
#include "nw_io.h"
#include "nw_devices.h"


static struct nw_devices_clock g_tb;

static uint64_t tb_now(void)
{
	return g_tb.ticks ? g_tb.ticks(g_tb.ctx) : 0;
}

/* ticks of a `hz` clock elapsed during `tb` timebase ticks */
static uint64_t tb_to_hz(uint64_t tb, uint32_t hz)
{
	if (g_tb.hz == 0)
		return 0;
	return (tb / g_tb.hz) * hz + ((tb % g_tb.hz) * hz) / g_tb.hz;
}

static inline uint32_t bswap32(uint32_t v)
{
	return (v >> 24) | ((v >> 8) & 0xff00u) | ((v << 8) & 0xff0000u) | (v << 24);
}

/*
 *  OpenPIC (Keylargo MPIC). Register semantics follow the OpenPIC
 *  specification as the mac99 machine presents them: one CPU, sources
 *  0..63 external, 64..67 IPIs, 68..71 timers; IVPR resets to MASK|MODE,
 *  IDR to 0 (no destination until the guest programs it), CTPR to 15.
 */
struct pic_src {
	uint32_t ivpr, idr;
	int pending;			/* input asserted (level) or latched (edge) */
	int servicing;			/* acknowledged, EOI not yet written */
};

struct pic_timer {
	uint32_t tbcr;			/* base count, bit 31 = count inhibit */
	uint32_t count;			/* count at `start`, when running */
	uint64_t start;			/* timebase when `count` was loaded */
	uint32_t tog;			/* TCCR toggle bit */
	int running;
};

static struct {
	uint32_t gcr, pir, spve, tfrr, ctpr;
	struct pic_src src[NW_OPENPIC_NIRQ];
	struct pic_timer tmr[NW_OPENPIC_NTMR];
} pic;

static inline int prio(uint32_t ivpr)
{
	return (int)((ivpr & NW_OPENPIC_IVPR_PRIORITY) >> 16);
}

static inline int is_level(int n)
{
	/* IPIs and timers are edge-triggered; external sources per IVPR[S] */
	return n < NW_OPENPIC_NSRC && (pic.src[n].ivpr & NW_OPENPIC_IVPR_SENSE) != 0;
}

/* pending, not masked, and routed to the CPU */
static inline int raised(int n)
{
	const struct pic_src *s = &pic.src[n];
	return s->pending && !(s->ivpr & NW_OPENPIC_IVPR_MASK) && (s->idr & 1);
}

static int highest_raised(void)
{
	int best = -1, bp = -1;
	for (int n = 0; n < NW_OPENPIC_NIRQ; n++)
		if (raised(n) && prio(pic.src[n].ivpr) > bp) {
			best = n;
			bp = prio(pic.src[n].ivpr);
		}
	return best;
}

static int servicing_priority(int *which)
{
	int best = -1, bp = -1;
	for (int n = 0; n < NW_OPENPIC_NIRQ; n++)
		if (pic.src[n].servicing && prio(pic.src[n].ivpr) > bp) {
			best = n;
			bp = prio(pic.src[n].ivpr);
		}
	if (which)
		*which = best;
	return bp;
}

/* Recompute the ACTIVITY bits and the INT output from the register state. */
static void pic_update(void)
{
	for (int n = 0; n < NW_OPENPIC_NIRQ; n++) {
		if (raised(n))
			pic.src[n].ivpr |= NW_OPENPIC_IVPR_ACTIVITY;
		else
			pic.src[n].ivpr &= ~(uint32_t)NW_OPENPIC_IVPR_ACTIVITY;
	}
	const int r = highest_raised();
	const int sp = servicing_priority(NULL);
	nw_io_ext_irq = (r >= 0 && prio(pic.src[r].ivpr) > (int)pic.ctpr &&
			 prio(pic.src[r].ivpr) > sp) ? 1 : 0;
}

static void pic_reset(void)
{
	memset(&pic, 0, sizeof(pic));
	pic.gcr = 0;
	pic.spve = NW_OPENPIC_IVPR_VECTOR;
	pic.tfrr = NW_OPENPIC_TFRR_HZ;
	pic.ctpr = 15;
	for (int n = 0; n < NW_OPENPIC_NIRQ; n++)
		pic.src[n].ivpr = (uint32_t)NW_OPENPIC_IVPR_MASK | NW_OPENPIC_IVPR_MODE;
	for (int t = 0; t < NW_OPENPIC_NTMR; t++)
		pic.tmr[t].tbcr = (uint32_t)NW_OPENPIC_TBCR_CI;
	nw_io_ext_irq = 0;
}

static void pic_write_ivpr(int n, uint32_t v)
{
	const uint32_t writable = (uint32_t)NW_OPENPIC_IVPR_MASK | NW_OPENPIC_IVPR_PRIORITY |
				  NW_OPENPIC_IVPR_SENSE | NW_OPENPIC_IVPR_POLARITY | NW_OPENPIC_IVPR_VECTOR;
	pic.src[n].ivpr = (pic.src[n].ivpr & NW_OPENPIC_IVPR_ACTIVITY) | (v & writable);
	if (n >= NW_OPENPIC_NSRC)
		pic.src[n].ivpr &= ~(uint32_t)(NW_OPENPIC_IVPR_SENSE | NW_OPENPIC_IVPR_POLARITY);
	pic_update();
}

static void pic_write_idr(int n, uint32_t v)
{
	pic.src[n].idr = v & 1;		/* one CPU */
	pic_update();
}

static void pic_set_irq(int n, int level)
{
	if (is_level(n))
		pic.src[n].pending = level ? 1 : 0;
	else if (level)
		pic.src[n].pending = 1;
	pic_update();
}

void nw_openpic_set_irq(int n, int level)
{
	if (n >= 0 && n < NW_OPENPIC_NSRC)
		pic_set_irq(n, level);
}

static uint32_t pic_iack(void)
{
	nw_io_ext_irq = 0;
	const int r = highest_raised();
	if (r < 0)
		return pic.spve;
	struct pic_src *s = &pic.src[r];
	if (prio(s->ivpr) <= (int)pic.ctpr)
		return pic.spve;
	s->servicing = 1;
	if (!is_level(r))
		s->pending = 0;
	pic_update();
	nw_io_ext_irq = 0;		/* stays low until EOI or a CTPR change */
	return s->ivpr & NW_OPENPIC_IVPR_VECTOR;
}

static void pic_eoi(void)
{
	int which;
	if (servicing_priority(&which) >= 0)
		pic.src[which].servicing = 0;
	pic_update();
}

static uint32_t timer_tccr(int t)
{
	const struct pic_timer *tm = &pic.tmr[t];
	if (!tm->running)
		return tm->tog | (tm->count & ~(uint32_t)NW_OPENPIC_TCCR_TOG);
	const uint64_t el = tb_to_hz(tb_now() - tm->start, pic.tfrr);
	const uint32_t cnt = el >= tm->count ? 0 : tm->count - (uint32_t)el;
	return tm->tog | (cnt & ~(uint32_t)NW_OPENPIC_TCCR_TOG);
}

static void timer_write_tbcr(int t, uint32_t v)
{
	struct pic_timer *tm = &pic.tmr[t];
	const int was = (tm->tbcr & NW_OPENPIC_TBCR_CI) == 0;
	const int now = (v & NW_OPENPIC_TBCR_CI) == 0;
	tm->tbcr = v;
	if (now && !was) {
		tm->count = v & ~(uint32_t)NW_OPENPIC_TBCR_CI;
		tm->start = tb_now();
		/* a base count of 0 would expire every clock; leave it stopped */
		tm->running = tm->count != 0;
	} else if (!now && was) {
		tm->count = timer_tccr(t) & ~(uint32_t)NW_OPENPIC_TCCR_TOG;
		tm->running = 0;
	}
}

static void via_tick(void);

/* Display vertical blank: asserted (level) every 1/NW_VBL_HZ s of timebase
 * while the device's VBL interrupt is enabled (the driver's cscSetInterrupt);
 * the driver's handler clears it (nw_display_vbl_clear). */
static uint64_t vbl_next;
static int vbl_enabled;

void nw_display_vbl_enable(int on)
{
	vbl_enabled = on != 0;
	if (!vbl_enabled)
		pic_set_irq(NW_VBL_IRQ, 0);
}

void nw_display_vbl_clear(void)
{
	pic_set_irq(NW_VBL_IRQ, 0);
}

static void vbl_tick(void)
{
	if (g_tb.hz == 0)
		return;
	const uint64_t period = g_tb.hz / NW_VBL_HZ;
	const uint64_t now = tb_now();
	if (vbl_next != 0 && now < vbl_next)
		return;
	if (vbl_next == 0 || now - vbl_next > 4 * period) {	/* first call or fell behind: re-arm */
		vbl_next = now + period;
		return;
	}
	vbl_next += period;
	if (vbl_enabled)
		pic_set_irq(NW_VBL_IRQ, 1);
}

void nw_devices_tick(void)
{
	via_tick();
	vbl_tick();
	for (int t = 0; t < NW_OPENPIC_NTMR; t++) {
		struct pic_timer *tm = &pic.tmr[t];
		if (!tm->running)
			continue;
		const uint64_t now = tb_now();
		if (tb_to_hz(now - tm->start, pic.tfrr) < tm->count)
			continue;
		tm->tog ^= (uint32_t)NW_OPENPIC_TCCR_TOG;
		tm->count = tm->tbcr & ~(uint32_t)NW_OPENPIC_TBCR_CI;
		tm->start = now;
		tm->running = tm->count != 0;
		pic_set_irq(NW_OPENPIC_TMR0 + t, 1);
	}
}

static uint32_t pic_cpu_read(uint32_t off)
{
	switch (off & 0xff0) {
	case 0x80: return pic.ctpr;
	case 0x90: return 0;				/* WHOAMI */
	case 0xa0: return pic_iack();
	case 0xb0: return 0;				/* EOI */
	default: return 0xffffffffu;
	}
}

static void pic_cpu_write(uint32_t off, uint32_t v)
{
	switch (off & 0xff0) {
	case 0x40: case 0x50: case 0x60: case 0x70:	/* IPI dispatch to CPU set v */
		if (v & 1)
			pic_set_irq(NW_OPENPIC_IPI0 + (int)((off & 0xf0) - 0x40) / 0x10, 1);
		break;
	case 0x80:
		pic.ctpr = v & 0xf;
		pic_update();
		break;
	case 0xb0:
		pic_eoi();
		break;
	default:
		break;
	}
}

uint32_t nw_openpic_read(uint32_t off)
{
	if (off & 0xf)
		return 0xffffffffu;
	if (off >= NW_OPENPIC_CPU0)
		return (off & 0xfff) < 0x100 ? pic_cpu_read(off & 0xfff) : 0xffffffffu;
	if (off >= NW_OPENPIC_SRC0) {
		const uint32_t n = (off - NW_OPENPIC_SRC0) >> 5;
		if (n >= NW_OPENPIC_NSRC)
			return 0xffffffffu;
		return (off & 0x10) ? pic.src[n].idr : pic.src[n].ivpr;
	}
	if (off >= NW_OPENPIC_TIMER0 && off < NW_OPENPIC_TIMER0 + 0x40 * NW_OPENPIC_NTMR) {
		const int t = (int)(off - NW_OPENPIC_TIMER0) >> 6;
		switch (off & 0x30) {
		case 0x00: return timer_tccr(t);
		case 0x10: return pic.tmr[t].tbcr;
		case 0x20: return pic.src[NW_OPENPIC_TMR0 + t].ivpr;
		default: return pic.src[NW_OPENPIC_TMR0 + t].idr;
		}
	}
	switch (off) {
	case 0x0: return 0xffffffffu;			/* BRR1 */
	case NW_OPENPIC_FRR: return NW_OPENPIC_FRR_VALUE;
	case NW_OPENPIC_GCR: return pic.gcr;
	case NW_OPENPIC_VIR: return 0;
	case NW_OPENPIC_PIR: return 0;
	case NW_OPENPIC_IPIVPR0: case NW_OPENPIC_IPIVPR0 + 0x10:
	case NW_OPENPIC_IPIVPR0 + 0x20: case NW_OPENPIC_IPIVPR0 + 0x30:
		return pic.src[NW_OPENPIC_IPI0 + (int)(off - NW_OPENPIC_IPIVPR0) / 0x10].ivpr;
	case NW_OPENPIC_SPVE: return pic.spve;
	case NW_OPENPIC_TFRR: return pic.tfrr;
	case 0x40: case 0x50: case 0x60: case 0x70: case 0x80: case 0x90: case 0xa0: case 0xb0:
		return pic_cpu_read(off);		/* current-CPU alias */
	default:
		return 0xffffffffu;
	}
}

void nw_openpic_write(uint32_t off, uint32_t v)
{
	if (off & 0xf)
		return;
	if (off >= NW_OPENPIC_CPU0) {
		if ((off & 0xfff) < 0x100)
			pic_cpu_write(off & 0xfff, v);
		return;
	}
	if (off >= NW_OPENPIC_SRC0) {
		const uint32_t n = (off - NW_OPENPIC_SRC0) >> 5;
		if (n >= NW_OPENPIC_NSRC)
			return;
		if (off & 0x10)
			pic_write_idr((int)n, v);
		else
			pic_write_ivpr((int)n, v);
		return;
	}
	if (off >= NW_OPENPIC_TIMER0 && off < NW_OPENPIC_TIMER0 + 0x40 * NW_OPENPIC_NTMR) {
		const int t = (int)(off - NW_OPENPIC_TIMER0) >> 6;
		switch (off & 0x30) {
		case 0x00: break;			/* TCCR read-only */
		case 0x10: timer_write_tbcr(t, v); break;
		case 0x20: pic_write_ivpr(NW_OPENPIC_TMR0 + t, v); break;
		default: pic_write_idr(NW_OPENPIC_TMR0 + t, v); break;
		}
		return;
	}
	switch (off) {
	case NW_OPENPIC_GCR:
		if (v & NW_OPENPIC_GCR_RESET) {
			pic_reset();
			return;
		}
		pic.gcr = v & NW_OPENPIC_GCR_MODE;
		break;
	case NW_OPENPIC_PIR:
		pic.pir = v;				/* one CPU: no reset output */
		break;
	case NW_OPENPIC_IPIVPR0: case NW_OPENPIC_IPIVPR0 + 0x10:
	case NW_OPENPIC_IPIVPR0 + 0x20: case NW_OPENPIC_IPIVPR0 + 0x30:
		pic_write_ivpr(NW_OPENPIC_IPI0 + (int)(off - NW_OPENPIC_IPIVPR0) / 0x10, v);
		break;
	case NW_OPENPIC_SPVE:
		pic.spve = v & NW_OPENPIC_IVPR_VECTOR;
		break;
	case NW_OPENPIC_TFRR:
		pic.tfrr = v;
		break;
	case 0x40: case 0x50: case 0x60: case 0x70: case 0x80: case 0x90: case 0xa0: case 0xb0:
		pic_cpu_write(off, v);
		break;
	default:
		break;
	}
}

/* nw_io glue: little-endian bus, 32-bit registers only */
static uint32_t openpic_io_read(void *, uint32_t off, int size)
{
	const uint32_t r = size == 4 ? bswap32(nw_openpic_read(off)) : 0xffffffffu;
	return r;
}

static void openpic_io_write(void *, uint32_t off, int size, uint32_t v)
{
	if (size == 4)
		nw_openpic_write(off, bswap32(v));
}

/*
 *  Keylargo timer: 64-bit free-running counter at 18.432 MHz, low word at
 *  +0x38, high word at +0x3c, little-endian. Writes are ignored.
 */
uint32_t nw_keylargo_timer_read(uint32_t off)
{
	const uint64_t kl = tb_to_hz(tb_now(), NW_KEYLARGO_TIMER_HZ);
	switch (off) {
	case 0x38: return (uint32_t)kl;
	case 0x3c: return (uint32_t)(kl >> 32);
	default: return 0;
	}
}

static uint32_t kltimer_io_read(void *, uint32_t off, int size)
{
	return size == 4 ? bswap32(nw_keylargo_timer_read(off)) : 0;
}

static void kltimer_io_write(void *, uint32_t, int, uint32_t)
{
}

/*
 *  uni-n: big-endian register file. +0 reads the version; the CPU plugin
 *  (FindUniNorth) reads it and read-modify-writes +0x40 (arbitration).
 */
static uint8_t unin_regs[NW_IO_UNIN_SIZE];

uint32_t nw_unin_read(uint32_t off, int size)
{
	uint32_t v = 0;
	for (int i = 0; i < size; i++) {
		const uint32_t o = off + (uint32_t)i;
		uint8_t b = o < NW_IO_UNIN_SIZE ? unin_regs[o] : 0;
		if (o < 4)
			b = (uint8_t)(NW_UNIN_VERSION >> (8 * (3 - o)));
		v = (v << 8) | b;
	}
	return v;
}

void nw_unin_write(uint32_t off, int size, uint32_t value)
{
	for (int i = 0; i < size; i++) {
		const uint32_t o = off + (uint32_t)i;
		if (o < NW_IO_UNIN_SIZE)
			unin_regs[o] = (uint8_t)(value >> (8 * (size - 1 - i)));
	}
}

static uint32_t unin_io_read(void *, uint32_t off, int size)
{
	return nw_unin_read(off, size);
}

static void unin_io_write(void *, uint32_t off, int size, uint32_t v)
{
	nw_unin_write(off, size, v);
}

/*
 *  uni-north PCI host bridge. CONFIG_ADDR is a 32-bit little-endian latch;
 *  CONFIG_DATA is an 8-byte window onto the configuration space selected by
 *  it. Address formats as the bridge decodes them (and as QEMU's uni-north
 *  does): bit 0 set = CFA1 {bus[23:16], devfn[15:8], reg[7:2]}; bit 0 clear
 *  = CFA0 {one-hot slot in bits 31:11, function[10:8], reg[7:2]} on bus 0.
 *
 *  Configuration headers are those of the devices the boot-info tree
 *  declares (nw_bootinfo.cpp): the bridge itself at slot 0xb, mac-io at
 *  slot 0xc with its 512 KiB register aperture at 0x80000000, the display
 *  at slot 0xe. Vendor/device/class match the tree's `vendor-id`,
 *  `device-id`, `class-code`. Type-0 headers, no capabilities. Slots
 *  without a device return all ones (master abort).
 */
static uint8_t pci_addr[4];	/* CONFIG_ADDR bytes as written (bus order) */

struct pci_dev_hdr {
	uint32_t devfn;
	uint16_t vendor, device;
	uint32_t class_rev;	/* class[31:8] | revision[7:0] */
	uint8_t header_type;
	uint32_t bar0;		/* 0 = none */
	uint32_t subsys;	/* subsystem vendor[15:0] | id[31:16] */
	uint32_t irq_line;
};

static const struct pci_dev_hdr pci_devs[] = {
	{ 0xb << 3, 0x106b, 0x001f, 0x06000000u, 0x00, 0, 0x1100 << 16 | 0x1af4, 0 },
	{ 0xc << 3, 0x106b, 0x0022, 0xff000000u, 0x00, 0x80000000u, 0x1100 << 16 | 0x1af4, 0 },
	{ 0xe << 3, 0x106b, 0x0010, 0x03000000u, 0x00, 0, 0x1100 << 16 | 0x1af4, 0 },
};

static const struct pci_dev_hdr *pci_find(uint32_t bus, uint32_t devfn)
{
	if (bus != 0)
		return NULL;
	for (size_t i = 0; i < sizeof(pci_devs) / sizeof(pci_devs[0]); i++)
		if (pci_devs[i].devfn == devfn)
			return &pci_devs[i];
	return NULL;
}

/* one little-endian dword of a type-0 header */
static uint32_t pci_dword(const struct pci_dev_hdr *d, uint32_t reg)
{
	switch (reg & 0xfc) {
	case 0x00: return (uint32_t)d->device << 16 | d->vendor;
	case 0x04: return 0x02000000u | (d->bar0 ? 0x0002u : 0);	/* status: medium DEVSEL; command: memory enable */
	case 0x08: return d->class_rev;
	case 0x0c: return (uint32_t)d->header_type << 16;
	case 0x10: return d->bar0;
	case 0x2c: return d->subsys;
	case 0x3c: return d->irq_line;
	default: return 0;
	}
}

uint32_t nw_pci_config_read(uint32_t bus, uint32_t devfn, uint32_t reg, int size)
{
	const struct pci_dev_hdr *d = pci_find(bus, devfn);
	uint32_t v = 0;
	for (int i = size - 1; i >= 0; i--) {
		const uint32_t r = (reg + (uint32_t)i) & 0xff;
		const uint8_t b = d ? (uint8_t)(pci_dword(d, r) >> (8 * (r & 3))) : 0xff;
		v = (v << 8) | b;
	}
	return v;	/* little-endian value: byte at reg is the low byte */
}

uint32_t nw_pci_config_addr(void)
{
	return (uint32_t)pci_addr[0] | (uint32_t)pci_addr[1] << 8 | (uint32_t)pci_addr[2] << 16 | (uint32_t)pci_addr[3] << 24;
}

/* selected {bus, devfn, reg} from CONFIG_ADDR plus the CONFIG_DATA byte offset */
static void pci_decode(uint32_t off, uint32_t *bus, uint32_t *devfn, uint32_t *reg)
{
	const uint32_t a = nw_pci_config_addr();
	if (a & 1) {
		*bus = (a >> 16) & 0xff;
		*devfn = (a >> 8) & 0xff;
	} else {
		uint32_t slot = 32;
		for (uint32_t s = 11; s < 32; s++)
			if (a & (1u << s)) { slot = s; break; }
		*bus = 0;
		*devfn = slot == 32 ? 0xffu : ((slot & 0x1f) << 3) | ((a >> 8) & 7);
	}
	*reg = (a & 0xfc) | (off & 7);
}

static uint32_t pci_addr_io_read(void *, uint32_t off, int size)
{
	uint32_t v = 0;
	for (int i = 0; i < size; i++) {
		const uint32_t o = off + (uint32_t)i;
		v = (v << 8) | (o < 4 ? pci_addr[o] : 0);
	}
	return v;
}

static void pci_addr_io_write(void *, uint32_t off, int size, uint32_t value)
{
	for (int i = 0; i < size; i++) {
		const uint32_t o = off + (uint32_t)i;
		if (o < 4)
			pci_addr[o] = (uint8_t)(value >> (8 * (size - 1 - i)));
	}
}

static uint32_t pci_data_io_read(void *, uint32_t off, int size)
{
	if (off >= 8)
		return 0xffffffffu;
	uint32_t bus, devfn, reg;
	pci_decode(off, &bus, &devfn, &reg);
	/* the bus delivers bytes in PCI order; present them as a big-endian load sees them */
	const uint32_t le = nw_pci_config_read(bus, devfn, reg, size);
	uint32_t v = 0;
	for (int i = 0; i < size; i++)
		v = (v << 8) | ((le >> (8 * i)) & 0xffu);
	return v;
}

static void pci_data_io_write(void *, uint32_t, int, uint32_t)
{
	/* headers are read-only in this model (no BAR relocation, no command bits) */
}

/*
 *  Keylargo GPIO. Pin registers: bit 0 output data, bit 1 input level,
 *  bit 2 output enable. Pin 1 carries the PMU interrupt (asserted low),
 *  pin 9 the programmer's switch NMI (asserted high); their ext-int lines
 *  are OpenPIC sources 0x2f and 0x37, as in QEMU's mac99.
 */
static struct {
	uint8_t regs[NW_GPIO_NPINS];
} gpio;

static void gpio_reset(void)
{
	memset(gpio.regs, 0, sizeof(gpio.regs));
	gpio.regs[1] = NW_GPIO_IN_DATA;		/* PMU interrupt line idle (high) */
}

void nw_gpio_set(uint32_t n, int state)
{
	if (n >= NW_GPIO_NPINS)
		return;
	const uint8_t v = (uint8_t)((gpio.regs[n] & ~NW_GPIO_IN_DATA) | (state ? NW_GPIO_IN_DATA : 0));
	if (v == gpio.regs[n])
		return;
	gpio.regs[n] = v;
	if (n == 1)
		nw_openpic_set_irq(NW_GPIO1_IRQ, !state);
	else if (n == 9)
		nw_openpic_set_irq(NW_GPIO9_IRQ, state);
}

uint32_t nw_gpio_read(uint32_t n)
{
	return n < NW_GPIO_NPINS ? gpio.regs[n] : 0;
}

static uint32_t gpio_byte_read(uint32_t off)
{
	if (off < 8)
		return 0;				/* level registers: nothing driven */
	return nw_gpio_read(off - 8);
}

static void gpio_byte_write(uint32_t off, uint8_t value)
{
	if (off < 8)
		return;					/* levels are read-only */
	const uint32_t n = off - 8;
	if (n >= NW_GPIO_NPINS)
		return;
	value &= (uint8_t)~NW_GPIO_IN_DATA;
	const uint8_t in = (value & NW_GPIO_OUT_ENABLE)
		? (uint8_t)((value & NW_GPIO_OUT_DATA) << 1)
		: (uint8_t)(gpio.regs[n] & NW_GPIO_IN_DATA);
	gpio.regs[n] = value | in;
}

static uint32_t gpio_io_read(void *, uint32_t off, int size)
{
	uint32_t v = 0;
	for (int i = 0; i < size; i++)
		v = (v << 8) | gpio_byte_read(off + (uint32_t)i);
	return v;
}

static void gpio_io_write(void *, uint32_t off, int size, uint32_t value)
{
	for (int i = 0; i < size; i++)
		gpio_byte_write(off + (uint32_t)i, (uint8_t)(value >> (8 * (size - 1 - i))));
}

/*
 *  VIA-PMU: a 6522 whose port B and shift register carry the PMU protocol.
 *  Register semantics and timer arithmetic follow QEMU's mos6522.c, the
 *  handshake and command set its pmu.c (hw/misc/macio), which is the model
 *  the golden trace was taken against.
 *
 *  Handshake (port B): idle is TREQ|TACK. The CPU lowers TREQ to send or
 *  receive a byte through the shift register (ACR SR_OUT set = sending);
 *  the PMU lowers TACK, transfers the byte, and raises the SR interrupt.
 *  The CPU raises TREQ, the PMU raises TACK. The first byte is the command;
 *  its argument count and response length come from the command table
 *  (-1 = a length byte precedes the data). After the last argument the
 *  command executes and the following transfers return the response.
 */
struct via_timer {
	uint16_t latch;
	uint16_t counter_value;		/* value loaded at `load` */
	uint64_t load;			/* timebase at load */
	uint64_t next_irq;		/* timebase of the next zero crossing */
	uint32_t hz;
	int index;
};

enum pmu_cmd_state { pmu_idle = 0, pmu_cmd, pmu_rsp };

static struct {
	uint8_t b, a, dirb, dira, sr, acr, pcr, ifr, ier;
	struct via_timer t[2];
	/* PMU */
	uint8_t last_b;
	enum pmu_cmd_state state;
	uint8_t cmd;
	int cmdlen, rsplen;
	uint8_t cmd_buf[128];
	int cmd_pos;
	uint8_t rsp[128];
	int rsp_sz, rsp_pos;
	uint8_t intbits, intmask;
	uint32_t tick_offset;		/* RTC seconds since 1904 at timebase 0 */
	uint64_t next_sec;		/* timebase of the next one-second tick */
} via;

/* Argument and response byte counts per command (-1 = length byte first),
 * from QEMU's pmu.h, itself from the Linux via-pmu driver. */
static const int8_t pmu_data_len[256][2] = {
/*  0        1        2        3        4        5        6        7  */
	{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
	{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},
	{ 1,  0},{ 1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
	{ 0,  1},{ 0,  1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{ 0,  0},
	{-1,  0},{ 0,  0},{ 2,  0},{ 1,  0},{ 1,  0},{-1,  0},{-1,  0},{-1,  0},
	{ 0, -1},{ 0, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{ 0, -1},
	{ 4,  0},{20,  0},{-1,  0},{ 3,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
	{ 0,  4},{ 0, 20},{ 2, -1},{ 2,  1},{ 3, -1},{-1, -1},{-1, -1},{ 4,  0},
	{ 1,  0},{ 1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
	{ 0,  1},{ 0,  1},{-1, -1},{ 1,  0},{ 1,  0},{-1, -1},{-1, -1},{-1, -1},
	{ 1,  0},{ 0,  0},{ 2,  0},{ 2,  0},{-1,  0},{ 1,  0},{ 3,  0},{ 1,  0},
	{ 0,  1},{ 1,  0},{ 0,  2},{ 0,  2},{ 0, -1},{-1, -1},{-1, -1},{-1, -1},
	{ 2,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
	{ 0,  3},{ 0,  3},{ 0,  2},{ 0,  8},{ 0, -1},{ 0, -1},{-1, -1},{-1, -1},
	{ 1,  0},{ 1,  0},{ 1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
	{ 0, -1},{ 0, -1},{-1, -1},{-1, -1},{-1, -1},{ 5,  1},{ 4,  1},{ 4,  1},
	{ 4,  0},{-1,  0},{ 0,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
	{ 0,  5},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},
	{ 1,  0},{ 2,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
	{ 0,  1},{ 0,  1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},
	{ 2,  0},{ 2,  0},{ 2,  0},{ 4,  0},{-1,  0},{ 0,  0},{-1,  0},{-1,  0},
	{ 1,  1},{ 1,  0},{ 3,  0},{ 2,  0},{-1, -1},{-1, -1},{-1, -1},{-1, -1},
	{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
	{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},
	{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
	{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},
	{ 0,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
	{ 1,  1},{ 1,  1},{-1, -1},{-1, -1},{ 0,  1},{ 0, -1},{-1, -1},{-1, -1},
	{-1,  0},{ 4,  0},{ 0,  1},{-1,  0},{-1,  0},{ 4,  0},{-1,  0},{-1,  0},
	{ 3, -1},{-1, -1},{ 0,  1},{-1, -1},{ 0, -1},{-1, -1},{-1, -1},{ 0,  0},
	{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
	{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},
};

static void via_update_irq(void)
{
	nw_openpic_set_irq(NW_PMU_IRQ, (via.ifr & via.ier) != 0);
}

/* timebase ticks for `n` ticks of a `hz` clock */
static uint64_t hz_to_tb(uint64_t n, uint32_t hz)
{
	if (hz == 0)
		return 0;
	return (n / hz) * g_tb.hz + ((n % hz) * g_tb.hz) / hz;
}

static uint64_t via_timer_elapsed(const struct via_timer *t, uint64_t now)
{
	return tb_to_hz(now - t->load, t->hz);
}

static unsigned via_timer_counter(const struct via_timer *t, uint64_t now)
{
	const uint64_t d = via_timer_elapsed(t, now);
	unsigned counter;
	if (t->index == 0) {
		/* counts down from latch to -1: period latch + 2 */
		if (d <= (uint64_t)t->counter_value + 1) {
			counter = (t->counter_value - (unsigned)d) & 0xffff;
		} else {
			counter = (unsigned)((d - (t->counter_value + 1)) % ((uint64_t)t->latch + 2));
			counter = (t->latch - counter) & 0xffff;
		}
	} else {
		counter = (t->counter_value - (unsigned)d) & 0xffff;
	}
	return counter;
}

static uint64_t via_timer_next_irq(const struct via_timer *t, uint64_t now)
{
	const uint64_t d = via_timer_elapsed(t, now);
	unsigned counter;
	if (d <= (uint64_t)t->counter_value + 1) {
		counter = (t->counter_value - (unsigned)d) & 0xffff;
	} else {
		counter = (unsigned)((d - (t->counter_value + 1)) % ((uint64_t)t->latch + 2));
		counter = (t->latch - counter) & 0xffff;
	}
	uint64_t next;
	if (counter == 0xffff)
		next = d + t->latch + 1;
	else if (counter == 0)
		next = d + t->latch + 2;
	else
		next = d + counter;
	next = t->load + hz_to_tb(next, t->hz);
	return next <= now ? now + 1 : next;
}

static int via_timer_armed(int i)
{
	if (i == 0)
		return (via.ier & NW_VIA_IFR_T1) && (via.acr & NW_VIA_ACR_T1MODE);
	return (via.ier & NW_VIA_IFR_T2) != 0;
}

static void via_timer_update(int i, uint64_t now)
{
	via.t[i].next_irq = via_timer_next_irq(&via.t[i], now);
}

static void via_set_counter(int i, unsigned val, uint64_t now)
{
	via.t[i].load = now;
	via.t[i].counter_value = (uint16_t)val;
	via_timer_update(i, now);
}

/* expired timers latch their IFR bit; `armed_only` mirrors the scheduled
 * callback (register accesses check regardless, as the 6522 counts anyway) */
static void via_check_timers(uint64_t now, int armed_only)
{
	for (int i = 0; i < 2; i++) {
		if (now < via.t[i].next_irq || (armed_only && !via_timer_armed(i)))
			continue;
		via_timer_update(i, now);
		via.ifr |= i == 0 ? NW_VIA_IFR_T1 : NW_VIA_IFR_T2;
	}
	via_update_irq();
}

static void pmu_update_extirq(void)
{
	nw_gpio_set(1, (via.intbits & via.intmask) == 0);
}

static void pmu_set_sr_int(void)
{
	via.ifr |= NW_VIA_IFR_SR;
	via_update_irq();
}

static uint32_t pmu_rtc_now(void)
{
	return via.tick_offset + (uint32_t)(g_tb.hz ? tb_now() / g_tb.hz : 0);
}

/*
 *  ADB bus behind the PMU: a keyboard and a mouse, as QEMU's adb-kbd and
 *  adb-mouse. Host input lands in a key ring and mouse accumulators from
 *  the UI thread; the CPU thread drains them through register 0 reads.
 */
enum { ADB_KEY_RING = 256 };

static struct {
	/* keyboard */
	uint8_t kbd_addr, kbd_handler, kbd_flags;
	uint8_t keys[ADB_KEY_RING];
	std::atomic<unsigned> key_wr, key_rd;
	/* mouse */
	uint8_t mouse_addr, mouse_handler, mouse_flags;
	std::atomic<int> dx, dy;
	std::atomic<unsigned> buttons;		/* bit 0 primary, bit 1 secondary */
	unsigned last_buttons;
	/* bus */
	int autopoll;				/* enabled by the guest */
	uint16_t autopoll_mask;
	int poll_index;				/* device to poll next: 0 keyboard, 1 mouse */
	uint64_t next_poll;			/* timebase */
	uint8_t reply[32];			/* pending reply / autopoll data for INT_ACK */
	int reply_sz;
} adb;

/* Register 3 high byte as a real device answers it: bit 6 set (no
 * exceptional event), bit 5 service-request enable, low nibble the
 * address (devices may return anything there). Listen R3 with handler
 * 0x00 rewrites the enable bit; Mac OS 9's cursor device driver writes
 * 0x63 and reads it back, and drops the mouse when the flags stay clear. */
enum { ADB_R3_FLAGS = 0x60 };

static void adb_reset(void)
{
	adb.kbd_addr = NW_ADB_KBD_ADDR;
	adb.kbd_handler = 1;
	adb.kbd_flags = ADB_R3_FLAGS;
	adb.mouse_addr = NW_ADB_MOUSE_ADDR;
	adb.mouse_handler = 2;			/* as QEMU's adb-mouse after reset */
	adb.mouse_flags = ADB_R3_FLAGS;
	adb.last_buttons = adb.buttons.load();
	adb.dx.store(0);
	adb.dy.store(0);
}

static inline uint8_t adb_r3_hi(uint8_t flags, uint8_t addr)
{
	return (uint8_t)((flags & 0xf0) | (addr & 0x0f));
}

void nw_adb_key(uint8_t code, int down)
{
	const unsigned wr = adb.key_wr.load(std::memory_order_relaxed);
	if (wr - adb.key_rd.load(std::memory_order_acquire) >= ADB_KEY_RING)
		return;					/* full: drop */
	adb.keys[wr % ADB_KEY_RING] = (uint8_t)((code & 0x7f) | (down ? 0 : 0x80));
	adb.key_wr.store(wr + 1, std::memory_order_release);
}

void nw_adb_mouse_move(int dx, int dy)
{
	adb.dx.fetch_add(dx);
	adb.dy.fetch_add(dy);
}

void nw_adb_mouse_button(int button, int down)
{
	if (button < 0 || button > 1)
		return;
	if (down)
		adb.buttons.fetch_or(1u << button);
	else
		adb.buttons.fetch_and(~(1u << button));
}

static int adb_kbd_poll(uint8_t *obuf)
{
	const unsigned rd = adb.key_rd.load(std::memory_order_relaxed);
	if (rd == adb.key_wr.load(std::memory_order_acquire))
		return 0;
	obuf[0] = adb.keys[rd % ADB_KEY_RING];
	obuf[1] = 0xff;					/* one key per packet */
	adb.key_rd.store(rd + 1, std::memory_order_release);
	return 2;
}

static int adb_kbd_request(uint8_t *obuf, const uint8_t *buf, int len)
{
	const int cmd = buf[0] & 0xc, reg = buf[0] & 0x3;
	if ((buf[0] & 0xf) == NW_ADB_FLUSH) {
		adb.key_rd.store(adb.key_wr.load());
		return 0;
	}
	if (cmd == NW_ADB_WRITEREG) {
		if (reg == 3 && len >= 3) {
			switch (buf[2]) {
			case 0xff:				/* self test */
				break;
			case 0xfe: case 0xfd:	/* change address (no collision / activator) */
				adb.kbd_addr = buf[1] & 0xf;
				break;
			case 0x00:				/* change address and SRQ enable */
				adb.kbd_addr = buf[1] & 0xf;
				adb.kbd_flags = (uint8_t)((adb.kbd_flags & 0xd0) | (buf[1] & 0x20));
				break;
			default:				/* new address and handler */
				adb.kbd_addr = buf[1] & 0xf;
				if (buf[2] >= 1 && buf[2] <= 3)
					adb.kbd_handler = buf[2];
				break;
			}
		}
		return 0;
	}
	if (cmd != NW_ADB_READREG)
		return 0;
	switch (reg) {
	case 0:
		return adb_kbd_poll(obuf);
	case 2:
		obuf[0] = 0x00;
		obuf[1] = 0x07;					/* LEDs off */
		return 2;
	case 3:
		obuf[0] = adb_r3_hi(adb.kbd_flags, adb.kbd_addr);
		obuf[1] = adb.kbd_handler;
		return 2;
	default:
		return 0;
	}
}

static int adb_mouse_poll(uint8_t *obuf)
{
	const unsigned buttons = adb.buttons.load();
	int dx = adb.dx.load(), dy = adb.dy.load();
	if (buttons == adb.last_buttons && dx == 0 && dy == 0)
		return 0;
	if (dx < -63) dx = -63; else if (dx > 63) dx = 63;
	if (dy < -63) dy = -63; else if (dy > 63) dy = 63;
	adb.dx.fetch_sub(dx);
	adb.dy.fetch_sub(dy);
	adb.last_buttons = buttons;
	obuf[0] = (uint8_t)((dy & 0x7f) | ((buttons & 1) ? 0 : 0x80));
	obuf[1] = (uint8_t)((dx & 0x7f) | ((buttons & 2) ? 0 : 0x80));
	return 2;
}

static int adb_mouse_request(uint8_t *obuf, const uint8_t *buf, int len)
{
	const int cmd = buf[0] & 0xc, reg = buf[0] & 0x3;
	if ((buf[0] & 0xf) == NW_ADB_FLUSH) {
		adb.last_buttons = adb.buttons.load();
		adb.dx.store(0);
		adb.dy.store(0);
		return 0;
	}
	if (cmd == NW_ADB_WRITEREG) {
		/* Mac OS 9 follows its register-3 setup with a write of another
		 * length to register 3; QEMU ignores it too, or the mouse would
		 * take the keyboard's address. */
		if (reg == 3 && len == 3) {
			switch (buf[2]) {
			case 0xff:
				break;
			case 0xfe: case 0xfd:
				adb.mouse_addr = buf[1] & 0xf;
				break;
			case 0x00:
				adb.mouse_addr = buf[1] & 0xf;
				adb.mouse_flags = (uint8_t)((adb.mouse_flags & 0xd0) | (buf[1] & 0x20));
				break;
			default:
				adb.mouse_addr = buf[1] & 0xf;
				if (buf[2] == 1 || buf[2] == 2)
					adb.mouse_handler = buf[2];
				break;
			}
		}
		return 0;
	}
	if (cmd != NW_ADB_READREG)
		return 0;
	switch (reg) {
	case 0:
		return adb_mouse_poll(obuf);
	case 3:
		obuf[0] = adb_r3_hi(adb.mouse_flags, adb.mouse_addr);
		obuf[1] = adb.mouse_handler;
		return 2;
	default:
		return 0;
	}
}

/* One ADB command on the bus; returns the reply length (0: none / timeout). */
static int adb_request(uint8_t *obuf, const uint8_t *buf, int len)
{
	if ((buf[0] & 0xf) == NW_ADB_BUSRESET) {
		adb_reset();
		return 0;
	}
	const int devaddr = buf[0] >> 4;
	if (devaddr == adb.kbd_addr)
		return adb_kbd_request(obuf, buf, len);
	if (devaddr == adb.mouse_addr)
		return adb_mouse_request(obuf, buf, len);
	return 0;
}

/* Autopoll pass: Talk R0 to the devices in the mask, starting after the
 * one polled last; the first with data wins and is polled again next time. */
static int adb_poll(uint8_t *obuf, uint16_t mask)
{
	for (int i = 0; i < 2; i++) {
		const int dev = adb.poll_index & 1;
		const uint8_t addr = dev == 0 ? adb.kbd_addr : adb.mouse_addr;
		if ((1u << addr) & mask) {
			const uint8_t cmd = (uint8_t)(NW_ADB_READREG | (addr << 4));
			const int olen = dev == 0 ? adb_kbd_poll(obuf + 1) : adb_mouse_poll(obuf + 1);
			if (olen > 0) {
				obuf[0] = cmd;
				return olen + 1;
			}
		}
		adb.poll_index = (adb.poll_index + 1) & 1;
	}
	return 0;
}

/* PMU command 0x20: one ADB packet {command, flags, len, data...}. */
static void pmu_cmd_adb(const uint8_t *in, int in_len)
{
	if (in_len < 2)
		return;
	if (in[0] == 0 && in[1] == 0x86) {		/* set autopoll mask */
		if (in_len != 4)
			return;
		const uint16_t mask = (uint16_t)((in[2] << 8) | in[3]);
		adb.autopoll = mask != 0;
		if (mask) {
			adb.autopoll_mask = mask;
			adb.next_poll = tb_now() + (uint64_t)g_tb.hz * NW_ADB_POLL_MS / 1000;
		}
		return;
	}
	int len;
	const int adblen = in[2];
	if (in_len < 3 || adblen > in_len - 3 || adblen > 252) {
		len = -1;
	} else {
		uint8_t cmd[128];
		cmd[0] = in[0];
		memcpy(&cmd[1], &in[3], (size_t)(in_len - 3));
		len = adb_request(adb.reply + 2, cmd, in_len - 2);
	}
	if (len > 0) {
		adb.reply_sz = len + 2;
		adb.reply[0] = 0x01;
		adb.reply[1] = (uint8_t)len;
	} else {
		adb.reply_sz = 1;
		adb.reply[0] = 0x00;
	}
	via.intbits |= NW_PMU_INT_ADB;
	pmu_update_extirq();
}

/* Autopoll from the device tick: not while a PMU command is in flight
 * (QEMU blocks the timer between the command byte and the last reply
 * byte) and not while a previous ADB interrupt is unacknowledged. */
static void pmu_adb_poll(uint64_t now)
{
	if (!adb.autopoll || via.state != pmu_idle || now < adb.next_poll)
		return;
	adb.next_poll = now + (uint64_t)g_tb.hz * NW_ADB_POLL_MS / 1000;
	if (via.intbits & NW_PMU_INT_ADB)
		return;
	const int olen = adb_poll(adb.reply, adb.autopoll_mask);
	if (olen > 0) {
		adb.reply_sz = olen;
		via.intbits |= NW_PMU_INT_ADB | NW_PMU_INT_ADB_AUTO;
		pmu_update_extirq();
	}
}

/* Executes via.cmd on cmd_buf[0..cmd_pos); fills rsp/rsp_sz. */
static void pmu_dispatch(void)
{
	const uint8_t *in = via.cmd_buf;
	const int in_len = via.cmd_pos;
	uint8_t *out = via.rsp;
	via.rsp_sz = 0;
	switch (via.cmd) {
	case NW_PMU_INT_ACK:
		if (in_len != 0)
			return;
		if (via.intbits & NW_PMU_INT_ADB) {
			/* the ADB data first; other bits wait for the next ack */
			out[0] = via.intbits & (NW_PMU_INT_ADB | NW_PMU_INT_ADB_AUTO);
			memcpy(out + 1, adb.reply, (size_t)adb.reply_sz);
			via.rsp_sz = adb.reply_sz + 1;
			via.intbits &= (uint8_t)~(NW_PMU_INT_ADB | NW_PMU_INT_ADB_AUTO);
			adb.reply_sz = 0;
		} else {
			out[0] = via.intbits;
			via.intbits = 0;
			via.rsp_sz = 1;
		}
		pmu_update_extirq();
		return;
	case NW_PMU_SET_INTR_MASK:
		if (in_len != 1)
			return;
		via.intmask = in[0];
		pmu_update_extirq();
		return;
	case NW_PMU_ADB_CMD:
		pmu_cmd_adb(in, in_len);
		return;
	case NW_PMU_ADB_POLL_OFF:
		if (in_len == 0)
			adb.autopoll = 0;
		return;
	case NW_PMU_RESET:
	case NW_PMU_SYSTEM_READY:
		return;
	case NW_PMU_SHUTDOWN:
		if (in_len != 4)
			return;
		out[0] = 0;
		via.rsp_sz = 1;
		return;
	case NW_PMU_READ_RTC: {
		if (in_len != 0)
			return;
		const uint32_t ti = pmu_rtc_now();
		out[0] = (uint8_t)(ti >> 24);
		out[1] = (uint8_t)(ti >> 16);
		out[2] = (uint8_t)(ti >> 8);
		out[3] = (uint8_t)ti;
		via.rsp_sz = 4;
		return;
	}
	case NW_PMU_SET_RTC: {
		if (in_len != 4)
			return;
		const uint32_t ti = ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) | ((uint32_t)in[2] << 8) | in[3];
		via.tick_offset = ti - (uint32_t)(g_tb.hz ? tb_now() / g_tb.hz : 0);
		return;
	}
	case NW_PMU_GET_VERSION:
		out[0] = 1;
		via.rsp_sz = 1;
		return;
	case NW_PMU_POWER_EVENTS:
		if (in_len < 1)
			return;
		switch (in[0]) {
		case 0x00:		/* get power-up events */
		case 0x03:		/* get wake-up events */
			out[0] = out[1] = 0;
			via.rsp_sz = 2;
			break;
		default:		/* set/clear: no state kept */
			break;
		}
		return;
	case NW_PMU_GET_COVER:
		out[0] = 0;
		via.rsp_sz = 1;
		return;
	case NW_PMU_DOWNLOAD_STATUS:
		out[0] = 0x62;		/* what OpenPMU expects */
		via.rsp_sz = 1;
		return;
	case NW_PMU_READ_PMU_RAM:
		return;
	default:
		/* unknown: zeros of the documented length */
		if (via.rsplen > 0) {
			via.rsp_sz = via.rsplen;
			memset(out, 0, (size_t)via.rsplen);
		}
		return;
	}
}

/* Port B changed: run the handshake. */
static void pmu_update(void)
{
	if (via.b == via.last_b)
		return;
	via.last_b = via.b;
	switch (via.b & (NW_PMU_TREQ | NW_PMU_TACK)) {
	case NW_PMU_TREQ:
		/* ack release */
		via.b |= NW_PMU_TACK;
		via.last_b = via.b;
		return;
	case NW_PMU_TACK:
		break;			/* request */
	default:
		return;			/* idle, or both low: nothing to do */
	}
	via.b &= (uint8_t)~NW_PMU_TACK;
	via.last_b = via.b;
	switch (via.state) {
	case pmu_idle:
		if (!(via.acr & NW_VIA_ACR_SR_OUT))
			break;		/* protocol error: reading while idle */
		via.cmd = via.sr;
		pmu_set_sr_int();
		via.cmdlen = pmu_data_len[via.cmd][0];
		via.rsplen = pmu_data_len[via.cmd][1];
		via.cmd_pos = 0;
		via.rsp_pos = 0;
		via.state = pmu_cmd;
		break;
	case pmu_cmd:
		if (!(via.acr & NW_VIA_ACR_SR_OUT))
			break;
		if (via.cmdlen == -1)
			via.cmdlen = via.sr;
		else if (via.cmd_pos < (int)sizeof(via.cmd_buf))
			via.cmd_buf[via.cmd_pos++] = via.sr;
		pmu_set_sr_int();
		break;
	case pmu_rsp:
		if (via.acr & NW_VIA_ACR_SR_OUT)
			break;		/* protocol error: writing while responding */
		if (via.rsplen == -1) {
			via.sr = (uint8_t)via.rsp_sz;
			via.rsplen = via.rsp_sz;
		} else if (via.rsp_pos < via.rsp_sz) {
			via.sr = via.rsp[via.rsp_pos++];
		}
		pmu_set_sr_int();
		break;
	}
	if (via.state == pmu_cmd && via.cmdlen == via.cmd_pos) {
		pmu_dispatch();
		via.state = pmu_rsp;
	}
	if (via.state == pmu_rsp && via.rsplen == via.rsp_pos)
		via.state = pmu_idle;
}

int nw_pmu_state(void)
{
	return (int)via.state;
}

static void via_reset(void)
{
	const uint64_t now = tb_now();
	memset(&via, 0, sizeof(via));
	via.dirb = 0xff;
	via.t[0].index = 0;
	via.t[0].hz = NW_VIA_T1_HZ;
	via.t[0].latch = 0xffff;
	via_set_counter(0, 0xffff, now);
	via.t[1].index = 1;
	via.t[1].hz = NW_VIA_T2_HZ;
	via.t[1].latch = 0xffff;
	via_set_counter(1, 0xffff, now);
	via.b = via.last_b = NW_PMU_TACK | NW_PMU_TREQ;
	/* PMU: Mac OS 9 expects ADB and tick interrupts enabled after reset */
	via.intmask = NW_PMU_INT_ADB | NW_PMU_INT_TICK;
	via.state = pmu_idle;
	via.tick_offset = (uint32_t)((uint64_t)time(NULL) + (uint32_t)NW_PMU_RTC_OFFSET)
		- (uint32_t)(g_tb.hz ? now / g_tb.hz : 0);
	via.next_sec = now + g_tb.hz;
	/* ADB: devices at their default addresses, autopoll off, pending
	 * host input kept (the ring and accumulators are the UI thread's) */
	adb_reset();
	adb.autopoll = 0;
	adb.autopoll_mask = 0;
	adb.poll_index = 0;
	adb.next_poll = now;
	adb.reply_sz = 0;
}

uint32_t nw_via_read(uint32_t reg)
{
	const uint64_t now = tb_now();
	via_check_timers(now, 0);
	uint32_t v = 0;
	switch (reg & 0xf) {
	case NW_VIA_B:
		v = via.b;
		via.ifr &= (uint8_t)~0x18;		/* CB1, CB2 (independent-interrupt mode not modelled) */
		via_update_irq();
		break;
	case NW_VIA_A:
	case NW_VIA_ANH:
		v = via.a;
		via.ifr &= (uint8_t)~0x03;		/* CA1, CA2 */
		via_update_irq();
		break;
	case NW_VIA_DIRB: v = via.dirb; break;
	case NW_VIA_DIRA: v = via.dira; break;
	case NW_VIA_T1CL:
		v = via_timer_counter(&via.t[0], now) & 0xff;
		via.ifr &= (uint8_t)~NW_VIA_IFR_T1;
		via_update_irq();
		break;
	case NW_VIA_T1CH: v = via_timer_counter(&via.t[0], now) >> 8; break;
	case NW_VIA_T1LL: v = via.t[0].latch & 0xff; break;
	case NW_VIA_T1LH: v = (via.t[0].latch >> 8) & 0xff; break;
	case NW_VIA_T2CL:
		v = via_timer_counter(&via.t[1], now) & 0xff;
		via.ifr &= (uint8_t)~NW_VIA_IFR_T2;
		via_update_irq();
		break;
	case NW_VIA_T2CH: v = via_timer_counter(&via.t[1], now) >> 8; break;
	case NW_VIA_SR:
		v = via.sr;
		via.ifr &= (uint8_t)~NW_VIA_IFR_SR;
		via_update_irq();
		break;
	case NW_VIA_ACR: v = via.acr; break;
	case NW_VIA_PCR: v = via.pcr; break;
	case NW_VIA_IFR:
		v = via.ifr;
		if (via.ifr & via.ier)
			v |= 0x80;
		break;
	case NW_VIA_IER: v = via.ier | 0x80u; break;
	}
	return v;
}

void nw_via_write(uint32_t reg, uint32_t value)
{
	const uint64_t now = tb_now();
	const uint8_t val = (uint8_t)value;
	switch (reg & 0xf) {
	case NW_VIA_B:
		via.b = (uint8_t)((via.b & ~via.dirb) | (val & via.dirb));
		pmu_update();
		via.ifr &= (uint8_t)~0x18;
		via_update_irq();
		break;
	case NW_VIA_A:
	case NW_VIA_ANH:
		via.a = (uint8_t)((via.a & ~via.dira) | (val & via.dira));
		via.ifr &= (uint8_t)~0x03;
		via_update_irq();
		break;
	case NW_VIA_DIRB: via.dirb = val; break;
	case NW_VIA_DIRA: via.dira = val; break;
	case NW_VIA_T1CL:
	case NW_VIA_T1LL:
		via.t[0].latch = (uint16_t)((via.t[0].latch & 0xff00) | val);
		via_timer_update(0, now);
		break;
	case NW_VIA_T1CH:
		via.t[0].latch = (uint16_t)((via.t[0].latch & 0xff) | (val << 8));
		via.ifr &= (uint8_t)~NW_VIA_IFR_T1;
		via_set_counter(0, via.t[0].latch, now);
		via_update_irq();
		break;
	case NW_VIA_T1LH:
		via.t[0].latch = (uint16_t)((via.t[0].latch & 0xff) | (val << 8));
		via.ifr &= (uint8_t)~NW_VIA_IFR_T1;
		via_timer_update(0, now);
		via_update_irq();
		break;
	case NW_VIA_T2CL:
		via.t[1].latch = (uint16_t)((via.t[1].latch & 0xff00) | val);
		break;
	case NW_VIA_T2CH:
		/* the high write loads the counter from the latch */
		via.t[1].latch = (uint16_t)((via.t[1].latch & 0xff) | (val << 8));
		via.ifr &= (uint8_t)~NW_VIA_IFR_T2;
		via_set_counter(1, via.t[1].latch, now);
		via_update_irq();
		break;
	case NW_VIA_SR: via.sr = val; break;
	case NW_VIA_ACR:
		via.acr = val;
		via_timer_update(0, now);
		break;
	case NW_VIA_PCR: via.pcr = val; break;
	case NW_VIA_IFR:
		via.ifr &= (uint8_t)~val;
		via_update_irq();
		break;
	case NW_VIA_IER:
		if (val & NW_VIA_IER_SET)
			via.ier |= val & 0x7f;
		else
			via.ier &= (uint8_t)~val;
		via_update_irq();
		via_timer_update(0, now);
		via_timer_update(1, now);
		break;
	}
}

/* The bus carries bytes; a wider access sees the addressed register in
 * its first byte and nothing in the rest. */
static uint32_t via_io_read(void *, uint32_t off, int size)
{
	const uint32_t v = nw_via_read(off >> NW_VIA_REG_SHIFT);
	return v << (8 * (size - 1));
}

static void via_io_write(void *, uint32_t off, int size, uint32_t value)
{
	nw_via_write(off >> NW_VIA_REG_SHIFT, value >> (8 * (size - 1)));
}

static void via_tick(void)
{
	const uint64_t now = tb_now();
	via_check_timers(now, 1);
	pmu_adb_poll(now);
	if (g_tb.hz && now >= via.next_sec) {
		via.intbits |= NW_PMU_INT_TICK;
		pmu_update_extirq();
		/* one tick per second of wall time, however long the host paused */
		via.next_sec += ((now - via.next_sec) / g_tb.hz + 1) * g_tb.hz;
	}
}

void nw_devices_init(const struct nw_devices_clock *tb)
{
	g_tb = *tb;
	vbl_next = 0;
	vbl_enabled = 0;
	pic_reset();
	memset(unin_regs, 0, sizeof(unin_regs));
	memset(pci_addr, 0, sizeof(pci_addr));
	gpio_reset();
	via_reset();
	static const struct nw_io_device devs[] = {
		{ "openpic", NW_IO_OPENPIC_BASE, NW_IO_OPENPIC_SIZE, openpic_io_read, openpic_io_write, NULL },
		{ "keylargo-timer", NW_IO_KEYLARGO_TIMER_BASE, NW_IO_KEYLARGO_TIMER_SIZE, kltimer_io_read, kltimer_io_write, NULL },
		{ "uni-n", NW_IO_UNIN_BASE, NW_IO_UNIN_SIZE, unin_io_read, unin_io_write, NULL },
		{ "pci-config-addr", NW_IO_PCI_CONFIG_ADDR, NW_IO_PCI_CONFIG_SIZE, pci_addr_io_read, pci_addr_io_write, NULL },
		{ "pci-config-data", NW_IO_PCI_CONFIG_DATA, NW_IO_PCI_CONFIG_SIZE, pci_data_io_read, pci_data_io_write, NULL },
		{ "gpio", NW_IO_MACIO_GPIO_BASE, NW_IO_MACIO_GPIO_SIZE, gpio_io_read, gpio_io_write, NULL },
		{ "via-pmu", NW_IO_VIA_PMU_BASE, NW_IO_VIA_PMU_SIZE, via_io_read, via_io_write, NULL },
	};
	for (size_t i = 0; i < sizeof(devs) / sizeof(devs[0]); i++)
		nw_io_register(&devs[i]);
}
