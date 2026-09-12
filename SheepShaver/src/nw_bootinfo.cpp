/*
 *  nw_bootinfo.cpp - New World boot-info area: flattened device tree + parcels
 *
 *  See nw_bootinfo.h for the record format (decoded from the 68k importer
 *  at ROM 0x44420: node {sibling, child, props}, property {next, name[32],
 *  len, value}). The tree mirrors the shape the golden mac99 Trampoline
 *  produced, with SheepShaver's own frame buffer as the display device and
 *  without devices SheepShaver does not present (usb, ethernet).
 *
 *  Parcels ('prcl' in the Mac OS ROM file):
 *    node header (88): link, ostype, hdr size, flags, +20 child stride,
 *                      a[32] @24, b[32] @56
 *    child (60):       ostype, flags, compress ('lzss'), +12 unpacked len,
 *                      +20 packed len, +24 data offset, name[32] @28
 *    'node' parcels create a root child named by their 'name' child; the
 *    other children become its properties.
 *    'prop' parcels attach children as properties to matching nodes:
 *      flags bit 0: node name == a        bit 1: parent name == a
 *      flags bit 2: compatible contains a bit 3: device_type == b
 *    child flags: 0x10000 -> AAPL,CodeRegister, 0x20000 -> AAPL,CodePrepare
 *                 (libraries), 0x100 skip (debug-only), 0x20 only if the
 *                 property does not exist yet.
 *  Matching what the golden tree shows: keylargo-ata/ata got the ATA ndrv,
 *  via-pmu/rtc the RTC ndrv, nvram,flash/nvram the NVRAM ndrv, uni-north/pci
 *  the PCI cycles lib, via-pmu-99/power-mgt the PowerMgr plugin (PMULib went
 *  to CodeRegister), cofb/display the frame buffer ndrv, EtherPrintfLib
 *  (0x10194) was left out.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <vector>

#include "nw_bootinfo.h"
#include "nw_boot_contract.h"

namespace {

struct Prop {
	std::string name;
	std::vector<uint8_t> val;
};

struct Node {
	std::string name;
	std::string device_type;
	std::string compatible;		/* raw, NUL separated list */
	std::vector<Prop> props;
	std::vector<Node *> kids;
	Node *parent;
	uint32_t rec_off, props_off, props_len;

	explicit Node(const char *n) : name(n), parent(NULL), rec_off(0), props_off(0), props_len(0) {}
	~Node()
	{
		for (size_t i = 0; i < kids.size(); i++)
			delete kids[i];
	}
	Node *add(const char *n)
	{
		Node *k = new Node(n);
		k->parent = this;
		kids.push_back(k);
		return k;
	}
	Prop *find(const char *n)
	{
		for (size_t i = 0; i < props.size(); i++)
			if (props[i].name == n)
				return &props[i];
		return NULL;
	}
	void set(const char *n, const uint8_t *v, size_t len)
	{
		Prop *p = find(n);
		if (!p) {
			props.push_back(Prop());
			p = &props.back();
			p->name = n;
		}
		p->val.assign(v, v + len);
		if (strcmp(n, "device_type") == 0)
			device_type = std::string((const char *)v, strnlen((const char *)v, len));
		else if (strcmp(n, "compatible") == 0)
			compatible = std::string((const char *)v, len);
	}
	void str(const char *n, const char *s) { set(n, (const uint8_t *)s, strlen(s) + 1); }
	void u32(const char *n, uint32_t v)
	{
		uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
		set(n, b, 4);
	}
	void empty(const char *n) { set(n, NULL, 0); }
	/* NUL separated string list: "a\0b\0" */
	void strs(const char *n, const char *s, size_t len) { set(n, (const uint8_t *)s, len); }
	void hex(const char *n, const char *h)
	{
		std::vector<uint8_t> v;
		for (; h[0] && h[1]; h += 2) {
			unsigned x;
			if (sscanf(h, "%2x", &x) != 1)
				break;
			v.push_back((uint8_t)x);
		}
		set(n, v.empty() ? NULL : &v[0], v.size());
	}
	bool compatible_has(const char *a) const
	{
		size_t o = 0;
		while (o < compatible.size()) {
			size_t e = compatible.find('\0', o);
			if (e == std::string::npos)
				e = compatible.size();
			if (compatible.compare(o, e - o, a) == 0)
				return true;
			o = e + 1;
		}
		return false;
	}
};

void be32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

uint32_t rd32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

uint32_t prop_rec_len(const Prop &p) { return NW_BOOTINFO_PROP_HDR + (((uint32_t)p.val.size() + 3u) & ~3u); }

/* ---- machine description -------------------------------------------- */

void add_interrupts(Node *n, const char *interrupts, const char *vectors, const char *prios,
		    const char *ints, const char *index)
{
	n->hex("interrupts", interrupts);
	n->hex("AAPL,interrupt-vectors", vectors);
	n->hex("AAPL,interrupt-priorities", prios);
	n->hex("AAPL,interrupts", ints);
	n->hex("AAPL,interrupt-index", index);
}

void add_pci_ids(Node *n, uint32_t vendor, uint32_t device, uint32_t rev, uint32_t cls)
{
	n->u32("vendor-id", vendor);
	n->u32("device-id", device);
	n->u32("revision-id", rev);
	n->u32("class-code", cls);
	n->u32("min-grant", 0);
	n->u32("max-latency", 0);
	n->u32("devsel-speed", 0);
	n->u32("subsystem-vendor-id", 0x1af4);
	n->u32("subsystem-id", 0x1100);
}

void add_escc_channel(Node *ch, bool a, bool legacy)
{
	ch->str("device_type", "serial");
	if (!legacy) {
		ch->str("compatible", a ? "chrp,es2" : "chrp,es3");
		ch->hex("reg", a ? "00013020000000010001303000000001000130500000000100008400000001000000850000000100"
				 : "00013000000000010001301000000001000130400000000100008600000001000000870000000100");
		ch->hex("AAPL,address", a ? "8001302080013030800130508000840080008500" : "8001300080013010800130408000860080008700");
	} else {
		ch->str("compatible", a ? "chrp,es4" : "chrp,es5");
		ch->hex("reg", a ? "000120020000000100012006000000010001200a0000000100008400000001000000850000000100"
				 : "00012000000000010001200400000001000120080000000100008600000001000000870000000100");
		ch->hex("AAPL,address", a ? "80012002800120068001200a8000840080008500" : "8001200080012004800120088000860080008700");
	}
	if (a)
		add_interrupts(ch, "000000250000000100000004000000000000000500000000", "000000250000000400000005",
			       "000000040000000400000004", legacy ? "000000090000000a0000000b" : "000000030000000400000005",
			       "000000030000000400000005");
	else
		add_interrupts(ch, "000000240000000100000006000000000000000700000000", "000000240000000600000007",
			       "000000040000000400000004", legacy ? "0000000c0000000d0000000e" : "000000060000000700000008",
			       "000000060000000700000008");
	ch->u32("slot-names", 0);
	ch->u32("interrupt-parent", NW_PHANDLE_PIC);
}

void add_ata(Node *ata, int bus)
{
	ata->str("device_type", "ata");
	ata->u32("#address-cells", 1);
	ata->u32("#size-cells", 0);
	ata->str("compatible", "keylargo-ata");
	ata->str("model", "ata-3");
	ata->str("AAPL,connector", "ata");
	ata->hex("AAPL,pio-timing", "0000052600000085000000250000002500000025000000000000000000000000");
	if (bus == 0)
		add_interrupts(ata, "0000000d000000010000000200000000", "0000000d00000002", "0000000200000004",
			       "0000000f00000010", "000000090000000a");
	else
		add_interrupts(ata, "0000000e000000010000000300000000", "0000000e00000003", "0000000200000004",
			       "0000001100000012", "0000000b0000000c");
	ata->u32("#interrupt-cells", 2);
	ata->hex("reg", bus == 0 ? "000200000000100000008b0000000200" : "000210000000100000008d0000000200");
	ata->hex("AAPL,address", bus == 0 ? "8002000080008b00" : "8002100080008d00");
	ata->u32("AAPL,bus-id", (uint32_t)bus);
	ata->u32("interrupt-parent", NW_PHANDLE_PIC);
	/* Golden (QEMU mac99): cdrom@1 on bus 0, nothing on bus 1. The ROM's
	 * keylargo-ata ndrv probes the bus, finds nothing here, and deletes
	 * the child (observed: RegistryEntryDelete from the ndrv). The
	 * host-backed drives live under /host-drives instead. */
	if (bus == 0) {
		Node *cd = ata->add("cdrom");
		cd->u32("reg", 1);
		cd->str("device_type", "block");
	}
}

const char *cpu_node_name(uint32_t pvr)
{
	switch (pvr >> 16) {
	case 0x0008: return "PowerPC,750";
	case 0x7000: case 0x7002: return "PowerPC,750";
	case 0x000c: case 0x8000: case 0x8001: case 0x8002: case 0x8003: return "PowerPC,G4";
	default: return "PowerPC,G4";
	}
}

Node *build_machine(const nw_bootinfo_params *p)
{
	Node *root = new Node("device-tree");
	root->u32("#address-cells", 1);
	root->u32("#size-cells", 1);
	root->u32("clock-frequency", p->bus_hz ? p->bus_hz : 100000000u);
	root->str("model", "PowerMac3,1");
	static const char compat[] = "PowerMac3,1\0MacRISC\0MacRISC2\0Power Macintosh";
	root->strs("compatible", compat, sizeof(compat));
	root->str("device_type", "bootrom");
	root->str("system-id", "0000000000000");
	root->str("copyright", "SheepShaver New World boot-info (device tree built at boot)");

	/* Parcel 'node' children (AAPL,CodePrepare / AAPL,CodeRegister) are
	 * inserted ahead of these by merge_parcels(). */
	Node *aliases = root->add("aliases");
	aliases->str("nvram", "/nvram@fff04000");
	aliases->str("via-pmu", "/pci@f2000000/mac-io@c/via-pmu");
	aliases->str("rtc", "/pci@f2000000/mac-io@c/via-pmu/rtc");
	aliases->str("scca", "/pci@f2000000/mac-io@c/escc/ch-a");
	aliases->str("sccb", "/pci@f2000000/mac-io@c/escc/ch-b");
	aliases->str("ide0", "/pci@f2000000/mac-io@c/ata-3@20000/cdrom@1");
	aliases->str("cd", "/pci@f2000000/mac-io@c/ata-3@20000/cdrom@1");
	aliases->str("cdrom", "/pci@f2000000/mac-io@c/ata-3@20000/cdrom@1");
	aliases->str("ide1", "/pci@f2000000/mac-io@c/ata-3@21000");
	aliases->str("mac-io", "/pci@f2000000/mac-io@c");
	aliases->str("screen", "/pci@f2000000/display@e");

	Node *openprom = root->add("openprom");
	openprom->str("device_type", "BootROM");
	openprom->str("model", "OpenFirmware 3");
	openprom->empty("relative-addressing");
	openprom->empty("supports-bootinfo");
	openprom->u32("boot-syntax", 1);
	openprom->add("client-services");

	Node *options = root->add("options");
	options->str("boot-args", "");
	options->str("boot-device", "cd:,\\\\:tbxi");
	options->str("use-generic?", "false");
	options->str("boot-script", "");
	options->str("boot-screen", "");
	options->str("vga-ndrv?", "true");
	options->str("virt-size", "-1");
	options->str("virt-base", "-1");
	options->str("load-base", "4000000");
	options->str("real-size", "-1");
	options->str("real-base", "-1");
	options->str("real-mode?", "false");
	options->str("little-endian?", "false");
	options->str("scroll-lock", "true");
	options->str("skip-netboot?", "false");
	options->str("default-mac-address", "false");
	options->str("pci-probe-mask", "-1");
	options->str("selftest-#megs", "0");
	options->str("screen-#rows", "75");
	options->str("screen-#columns", "100");
	options->str("output-device", "screen");
	options->str("input-device", "keyboard");
	options->str("use-nvramrc?", "false");
	options->str("oem-logo?", "false");
	options->str("oem-banner", "");
	options->str("oem-banner?", "false");
	options->str("nvramrc", "");
	options->str("fcode-debug?", "false");
	options->str("diag-switch?", "false");
	options->str("boot-file", "");
	options->str("boot-command", "boot");
	options->str("auto-boot?", "true");

	Node *chosen = root->add("chosen");
	chosen->u32("stdin", 0x00010001);
	chosen->u32("stdout", 0x00010002);
	chosen->u32("nvram", 0x00010003);
	chosen->u32("mmu", 0x00010004);
	chosen->u32("rtc", 0x00010005);
	chosen->u32("memory", 0x00010006);
	chosen->u32("display", 0x00010002);
	chosen->str("bootargs", "");
	/* No partition number: StartLib compares it with the one it derives
	 * from the drive's partition map, and the SheepShaver DRVRs present
	 * the HFS partition itself (no map). The unit address is checked
	 * against the SCSI-target field of the driver's 'boot' response:
	 * .AppleCD puts its drive number there (1: it is opened first, see
	 * nw_install_drivers), .Disk leaves it 0. */
	chosen->str("bootpath", p->boot_from_cd ? "/host-drives/cdrom@1:,\\\\:tbxi"
					       : "/host-drives/disk@0:,\\\\:tbxi");

	root->add("builtin")->add("console");
	Node *packages = root->add("packages");
	static const char *const pkgs[] = { "cmdline", "disk-label", "terminal-emulator", "deblocker",
		"hfsplus-files", "hfs-files", "iso9660-files", "mac-parts", "pc-parts", "xcoff-loader",
		"elf-loader", "bootinfo-loader" };
	for (size_t i = 0; i < sizeof(pkgs) / sizeof(pkgs[0]); i++) {
		Node *k = packages->add(pkgs[i]);
		if (strcmp(pkgs[i], "mac-parts") == 0)
			k->str("selected-partition-args", "9,\\\\:tbxi");
	}

	Node *cpus = root->add("cpus");
	cpus->u32("#address-cells", 1);
	cpus->u32("#size-cells", 0);
	Node *cpu = cpus->add(cpu_node_name(p->pvr));
	cpu->str("device_type", "cpu");
	cpu->u32("cpu-version", p->pvr);
	cpu->u32("d-cache-size", 0x8000);
	cpu->u32("i-cache-size", 0x8000);
	cpu->u32("d-cache-sets", 0x80);
	cpu->u32("i-cache-sets", 0x80);
	cpu->u32("d-cache-block-size", 0x20);
	cpu->u32("i-cache-block-size", 0x20);
	cpu->u32("tlb-sets", 0x40);
	cpu->u32("tlb-size", 0x80);
	cpu->u32("timebase-frequency", p->tb_hz);
	cpu->u32("clock-frequency", p->cpu_hz);
	cpu->u32("bus-frequency", p->bus_hz);
	cpu->str("state", "running");
	cpu->u32("reservation-granule-size", 0x20);
	cpu->u32("reg", 0);

	Node *memory = root->add("memory");
	memory->str("device_type", "memory");
	{
		uint8_t reg[8] = { 0, 0, 0, 0 };
		be32(reg + 4, p->ram_size);
		memory->set("reg", reg, 8);
		memory->set("available", reg, 8);
	}

	Node *rom = root->add("rom");
	rom->hex("reg", "ff80000000000000");
	rom->u32("#address-cells", 1);
	rom->hex("ranges", "ff80000000800000ff800000");
	rom->add("macos");	/* MacOSROMFile-version comes from the 'macos' parcel */

	Node *nvram = root->add("nvram");
	nvram->u32("#bytes", 0x2000);
	nvram->hex("reg", "fff0400000004000");
	nvram->str("device_type", "nvram");
	nvram->str("compatible", "nvram,flash");

	/*
	 * SheepShaver's host-backed drives (.AppleCD / .Disk DRVRs, installed
	 * by the EMUL_OP driver hook). Not part of the mac99 golden tree: no
	 * ROM driver claims this node, so nothing deletes it, and StartLib's
	 * GetStartupDevice can resolve "bootpath" to it and match the
	 * AAPL,boot-cookie tag against the drive queue (see nw_bootinfo.h).
	 * device_type "scsi": StartLib picks its lookup by the driver's
	 * DriverGestalt 'boot' response; the SheepShaver DRVRs answer with
	 * the classic (drive, refnum) form, which StartLib treats as a SCSI
	 * boot ID and then requires the cookie node to be device_type
	 * "scsi" with a unit address equal to the SCSI target field. The
	 * DRVRs are the Apple SCSI .AppleCD/.Disk personalities, so that is
	 * what they are. See NEWWORLD-BOOT-PLAN.md S4 step 5.
	 */
	Node *hd = root->add("host-drives");
	hd->u32("#address-cells", 1);
	hd->u32("#size-cells", 0);
	hd->str("device_type", "block-host");
	if (p->cd_refnum) {
		Node *cd = hd->add("cdrom");
		cd->u32("reg", 1);
		cd->str("device_type", "scsi");
		cd->u32("AAPL,boot-cookie", (uint32_t)(uint16_t)p->cd_refnum);
	}
	if (p->disk_refnum) {
		Node *dk = hd->add("disk");
		dk->u32("reg", 0);
		dk->str("device_type", "scsi");
		dk->u32("AAPL,boot-cookie", (uint32_t)(uint16_t)p->disk_refnum);
	}

	Node *pci = root->add("pci");
	add_pci_ids(pci, 0x106b, 0x1f, 0, 0x00060000);
	pci->u32("cache-line-size", 0x1000);
	pci->str("device_type", "pci");
	pci->str("model", "AAPL,UniNorth");
	pci->str("compatible", "uni-north");
	pci->u32("#address-cells", 3);
	pci->u32("#size-cells", 2);
	pci->u32("#interrupt-cells", 1);
	pci->hex("reg", "f200000002000000");
	pci->hex("ranges", "010000000000000000000000f20000000000000000800000020000000000000080000000800000000000000010000000");
	pci->hex("bus-range", "0000000000000000");
	pci->hex("available", "020000000000000082400000000000008dc0000001000000000000000000100000000000007ff000");
	pci->u32("interrupt-parent", NW_PHANDLE_PIC);
	pci->hex("interrupt-map-mask", "00fff800000000000000000000000007");

	Node *macio = pci->add("mac-io");
	add_pci_ids(macio, 0x106b, 0x22, 0, 0x00ff0000);
	macio->u32("cache-line-size", 0);
	macio->str("device_type", "mac-io");
	macio->str("model", "AAPL,Keylargo");
	macio->str("compatible", "Keylargo");
	macio->u32("#address-cells", 1);
	macio->u32("#size-cells", 1);
	macio->u32("#interrupt-cells", 1);
	macio->hex("assigned-addresses", "0200601000000000800000000000000000080000");
	macio->u32("AAPL,address", 0x80000000u);
	macio->hex("reg", "00006000000000000000000000000000000000000200601000000000000000000000000000080000");
	macio->hex("ranges", "0000000002006010000000008000000000080000");
	macio->u32("interrupt-parent", NW_PHANDLE_PIC);

	Node *gpio = macio->add("gpio");
	gpio->str("device_type", "gpio");
	gpio->u32("#address-cells", 1);
	gpio->u32("#size-cells", 0);
	gpio->str("compatible", "mac-io-gpio");
	gpio->hex("reg", "0000005000000030");
	gpio->u32("AAPL,address", 0x80000050u);
	Node *extint = gpio->add("extint-gpio1");
	add_interrupts(extint, "0000002f00000001", "0000002f", "00000002", "00000000", "00000000");
	extint->u32("reg", 9);
	static const char extint_compat[] = "keywest-gpio1\0gpio";
	extint->strs("compatible", extint_compat, sizeof(extint_compat));
	extint->u32("interrupt-parent", NW_PHANDLE_PIC);
	Node *pswitch = gpio->add("programmer-switch");
	pswitch->str("device_type", "programmer-switch");
	add_interrupts(pswitch, "0000003700000000", "00000037", "00000007", "00000001", "00000001");
	pswitch->u32("interrupt-parent", NW_PHANDLE_PIC);

	Node *pmu = macio->add("via-pmu");
	pmu->str("device_type", "via-pmu");
	pmu->str("compatible", "pmu");
	pmu->u32("#address-cells", 1);
	pmu->u32("#size-cells", 0);
	pmu->hex("reg", "0001600000002000");
	pmu->u32("AAPL,address", 0x80016000u);
	add_interrupts(pmu, "0000001900000001", "00000019", "00000001", "00000002", "00000002");
	pmu->u32("pmu-version", 0x00d0330c);
	pmu->u32("interrupt-parent", NW_PHANDLE_PIC);
	Node *rtc = pmu->add("rtc");
	rtc->str("device_type", "rtc");
	rtc->str("compatible", "rtc,via-pmu");
	Node *pmgt = pmu->add("power-mgt");
	pmgt->str("device_type", "power-mgt");
	pmgt->str("compatible", "via-pmu-99");
	pmgt->str("registry-name", "extint-gpio1");
	pmgt->hex("prim-info", "000000ff0000002c00030d400001e70500003400000000000000260d46000278783c00");

	Node *escc = macio->add("escc");
	escc->u32("#address-cells", 1);
	escc->hex("reg", "0001300000001000");
	escc->u32("AAPL,address", 0x80013000u);
	escc->str("device_type", "escc");
	static const char escc_compat[] = "escc\0CHRP,es0";
	escc->strs("compatible", escc_compat, sizeof(escc_compat));
	escc->empty("ranges");
	add_escc_channel(escc->add("ch-a"), true, false);
	add_escc_channel(escc->add("ch-b"), false, false);

	Node *esccl = macio->add("escc-legacy");
	esccl->u32("#address-cells", 1);
	esccl->hex("reg", "0001200000001000");
	esccl->u32("AAPL,address", 0x80012000u);
	esccl->str("device_type", "escc-legacy");
	esccl->str("compatible", "chrp,es1");
	esccl->empty("ranges");
	add_escc_channel(esccl->add("ch-a"), true, true);
	add_escc_channel(esccl->add("ch-b"), false, true);

	add_ata(macio->add("ata-3"), 0);
	add_ata(macio->add("ata-3"), 1);

	Node *pic = macio->add("interrupt-controller");
	pic->str("device_type", "open-pic");
	pic->str("compatible", "chrp,open-pic");
	pic->empty("built-in");
	pic->hex("reg", "0004000000040000");
	pic->u32("AAPL,address", 0x80040000u);
	pic->u32("#interrupt-cells", 2);
	pic->u32("#address-cells", 0);
	pic->empty("interrupt-controller");
	pic->u32("clock-frequency", 0x003f940a);

	/* Display: SheepShaver's frame buffer, presented as a PCI device at
	 * slot e with its aperture at fb_la. The ROM's cofb ndrv (parcel
	 * cofb/display) drives it through address/width/height/depth/linebytes. */
	Node *disp = pci->add("display");
	add_pci_ids(disp, 0x106b, 0x0010, 0, 0x00030000);
	disp->u32("cache-line-size", 0);
	disp->str("device_type", "display");
	disp->str("model", "SheepShaver Video");
	disp->str("compatible", "cofb");
	{
		uint32_t fb_size = (p->fb_linebytes * p->fb_height + 0xfffu) & ~0xfffu;
		uint8_t aa[20] = { 0x82, 0x00, 0x70, 0x10, 0, 0, 0, 0 };
		be32(aa + 8, p->fb_la);
		be32(aa + 12, 0);
		be32(aa + 16, fb_size);
		disp->set("assigned-addresses", aa, 20);
		disp->u32("AAPL,address", p->fb_la);
		uint8_t reg[40];
		memset(reg, 0, sizeof(reg));
		reg[0] = 0x00; reg[1] = 0x00; reg[2] = 0x70; reg[3] = 0x00;
		reg[20] = 0x82; reg[21] = 0x00; reg[22] = 0x70; reg[23] = 0x10;
		be32(reg + 36, fb_size);
		disp->set("reg", reg, 40);
	}
	disp->u32("width", p->fb_width);
	disp->u32("height", p->fb_height);
	disp->u32("depth", p->fb_depth);
	disp->u32("linebytes", p->fb_linebytes);
	disp->u32("address", p->fb_la);

	Node *unin = root->add("uni-n");
	unin->str("device_type", "memory-controller");
	unin->str("compatible", "uni-north");
	unin->u32("device-rev", 7);
	unin->hex("reg", "f800000001000000");

	return root;
}

/* ---- parcels -------------------------------------------------------- */

struct ParcelChild {
	std::string ostype, name;
	uint32_t flags, unpacked, packed, ptr;
	bool lzss;
};

struct Parcel {
	std::string ostype, a, b;
	uint32_t flags;
	std::vector<ParcelChild> kids;
};

std::string cstr(const uint8_t *p, size_t n)
{
	return std::string((const char *)p, strnlen((const char *)p, n));
}

bool parse_parcels(const uint8_t *d, size_t n, std::vector<Parcel> *out)
{
	if (d == NULL || n < 16 || memcmp(d, "prcl", 4) != 0)
		return false;
	uint32_t off = rd32(d + 12);
	int guard = 0;
	while (off != 0 && guard++ < 256) {
		if ((size_t)off + 88 > n)
			return false;
		Parcel p;
		const uint32_t link = rd32(d + off);
		p.ostype = std::string((const char *)d + off + 4, 4);
		const uint32_t hdr = rd32(d + off + 8);
		p.flags = rd32(d + off + 12);
		uint32_t stride = rd32(d + off + 20);
		if (stride < 60)
			stride = 60;
		p.a = cstr(d + off + 24, 32);
		p.b = cstr(d + off + 56, 32);
		if ((size_t)off + hdr > n)
			return false;
		for (uint32_t co = off + 88; co + 60 <= off + hdr; co += stride) {
			ParcelChild c;
			c.ostype = std::string((const char *)d + co, 4);
			c.flags = rd32(d + co + 4);
			c.lzss = memcmp(d + co + 8, "lzss", 4) == 0;
			c.unpacked = rd32(d + co + 12);
			c.packed = rd32(d + co + 20);
			c.ptr = rd32(d + co + 24);
			c.name = cstr(d + co + 28, 32);
			if ((size_t)c.ptr + c.packed > n)
				return false;
			p.kids.push_back(c);
		}
		out->push_back(p);
		if (link <= off)
			break;
		off = link;
	}
	return true;
}

bool child_payload(const uint8_t *d, const ParcelChild &c, std::vector<uint8_t> *out)
{
	if (!c.lzss) {
		out->assign(d + c.ptr, d + c.ptr + c.packed);
		return true;
	}
	if (c.unpacked == 0 || c.unpacked > 0x400000u)
		return false;
	out->assign(c.unpacked, 0);
	nw_lzss_decode(d + c.ptr, c.packed, &(*out)[0], out->size());
	return true;
}

void collect(Node *n, std::vector<Node *> *all)
{
	all->push_back(n);
	for (size_t i = 0; i < n->kids.size(); i++)
		collect(n->kids[i], all);
}

bool parcel_matches(const Parcel &p, const Node *n)
{
	if (p.flags & 1) {
		if (n->name != p.a)
			return false;
	}
	if (p.flags & 2) {
		if (n->parent == NULL || n->parent->name != p.a)
			return false;
	}
	if (p.flags & 4) {
		if (!n->compatible_has(p.a.c_str()))
			return false;
	}
	if (p.flags & 8) {
		if (n->device_type != p.b)
			return false;
	}
	return (p.flags & 0xf) != 0;
}

bool merge_parcels(Node *root, const uint8_t *d, size_t n, int *n_props)
{
	std::vector<Parcel> parcels;
	if (!parse_parcels(d, n, &parcels))
		return false;
	Node *prepare = NULL, *reg = NULL;
	std::vector<Node *> created;
	/* 'node' parcels: new root children, inserted first (golden order). */
	for (size_t i = 0; i < parcels.size(); i++) {
		const Parcel &p = parcels[i];
		if (p.ostype != "node")
			continue;
		std::string name;
		for (size_t k = 0; k < p.kids.size(); k++)
			if (p.kids[k].name == "name") {
				std::vector<uint8_t> v;
				if (child_payload(d, p.kids[k], &v))
					name = cstr(&v[0], v.size());
			}
		if (name.empty())
			return false;
		Node *nn = new Node(name.c_str());
		nn->parent = root;
		created.push_back(nn);
		if (p.flags & 0x20000) prepare = nn;
		if (p.flags & 0x10000) reg = nn;
		for (size_t k = 0; k < p.kids.size(); k++) {
			const ParcelChild &c = p.kids[k];
			if (c.name == "name" || (c.flags & 0x100))
				continue;
			std::vector<uint8_t> v;
			if (!child_payload(d, c, &v))
				return false;
			nn->set(c.name.c_str(), v.empty() ? NULL : &v[0], v.size());
			(*n_props)++;
		}
	}
	root->kids.insert(root->kids.begin(), created.begin(), created.end());

	std::vector<Node *> all;
	collect(root, &all);
	for (size_t i = 0; i < parcels.size(); i++) {
		const Parcel &p = parcels[i];
		if (p.ostype != "prop")
			continue;
		for (size_t j = 0; j < all.size(); j++) {
			Node *tgt = all[j];
			if (!parcel_matches(p, tgt))
				continue;
			for (size_t k = 0; k < p.kids.size(); k++) {
				const ParcelChild &c = p.kids[k];
				if (c.flags & 0x100)
					continue;
				Node *dst = tgt;
				if (c.flags & 0x20000) dst = prepare;
				else if (c.flags & 0x10000) dst = reg;
				if (dst == NULL)
					continue;
				if ((c.flags & 0x20) && dst->find(c.name.c_str()))
					continue;
				if (dst != tgt && dst->find(c.name.c_str()))
					continue;	/* library already registered (PMULib x3) */
				std::vector<uint8_t> v;
				if (!child_payload(d, c, &v))
					return false;
				dst->set(c.name.c_str(), v.empty() ? NULL : &v[0], v.size());
				(*n_props)++;
			}
		}
	}
	return true;
}

/* ---- serialisation -------------------------------------------------- */

void layout_nodes(Node *n, uint32_t *off)
{
	n->rec_off = *off;
	*off += 12;
	for (size_t i = 0; i < n->kids.size(); i++)
		layout_nodes(n->kids[i], off);
}

void layout_props(Node *n, uint32_t *off)
{
	/* "name" first, like the Trampoline. */
	n->props_off = *off;
	uint32_t len = NW_BOOTINFO_PROP_HDR + ((n->name.size() + 1 + 3) & ~3u);
	for (size_t i = 0; i < n->props.size(); i++)
		len += prop_rec_len(n->props[i]);
	n->props_len = len;
	*off += len;
	for (size_t i = 0; i < n->kids.size(); i++)
		layout_props(n->kids[i], off);
}

void put_prop(uint8_t *area, uint32_t *o, const char *name, const uint8_t *val, uint32_t len, bool last)
{
	const uint32_t rec = NW_BOOTINFO_PROP_HDR + ((len + 3u) & ~3u);
	be32(area + *o, last ? 0 : rec);
	memset(area + *o + 4, 0, 32);
	strncpy((char *)area + *o + 4, name, 31);
	be32(area + *o + 0x24, len);
	if (len)
		memcpy(area + *o + 0x28, val, len);
	*o += rec;
}

void emit(uint8_t *area, Node *n)
{
	uint8_t *r = area + n->rec_off;
	Node *sib = NULL;
	if (n->parent) {
		std::vector<Node *> &ks = n->parent->kids;
		for (size_t i = 0; i + 1 < ks.size(); i++)
			if (ks[i] == n)
				sib = ks[i + 1];
	}
	be32(r, sib ? sib->rec_off - n->rec_off : 0);
	be32(r + 4, n->kids.empty() ? 0 : n->kids[0]->rec_off - n->rec_off);
	be32(r + 8, n->props_off - n->rec_off);
	uint32_t o = n->props_off;
	put_prop(area, &o, "name", (const uint8_t *)n->name.c_str(), (uint32_t)n->name.size() + 1, n->props.empty());
	for (size_t i = 0; i < n->props.size(); i++)
		put_prop(area, &o, n->props[i].name.c_str(), n->props[i].val.empty() ? NULL : &n->props[i].val[0],
			 (uint32_t)n->props[i].val.size(), i + 1 == n->props.size());
	for (size_t i = 0; i < n->kids.size(); i++)
		emit(area, n->kids[i]);
}

/* ---- kept parcel blob ------------------------------------------------ */

uint8_t *g_parcels = NULL;
size_t g_parcels_size = 0;

} // namespace

extern "C" {

uint32_t nw_bootinfo_build_tree(uint8_t *area, uint32_t size, const struct nw_bootinfo_params *p)
{
	if (area == NULL || p == NULL || size < 0x1000)
		return 0;
	Node *root = build_machine(p);
	int n_parcel_props = 0;
	if (p->parcels && p->parcels_size) {
		if (!merge_parcels(root, p->parcels, p->parcels_size, &n_parcel_props)) {
			delete root;
			return 0;
		}
	}
	uint32_t off = NW_BOOTINFO_ROOT;
	layout_nodes(root, &off);
	layout_props(root, &off);
	if (off > size) {
		delete root;
		return 0;
	}
	memset(area, 0, size);
	be32(area + 0, NW_BOOTINFO_MAGIC0);
	be32(area + 4, NW_BOOTINFO_MAGIC1);
	be32(area + 8, NW_BOOTINFO_MAGIC2);
	emit(area, root);
	delete root;
	return off;
}

int nw_bootinfo_get_prop(const uint8_t *area, uint32_t size, uint32_t node_off, const char *name,
			 const uint8_t **val, uint32_t *len)
{
	if (area == NULL || node_off + 12 > size)
		return 0;
	uint32_t po = rd32(area + node_off + 8);
	if (po == 0)
		return 0;
	uint32_t p = node_off + po;
	for (int guard = 0; guard < 4096; guard++) {
		if (p + NW_BOOTINFO_PROP_HDR > size)
			return 0;
		const uint32_t next = rd32(area + p);
		const uint32_t vl = rd32(area + p + 0x24);
		if (p + NW_BOOTINFO_PROP_HDR + vl > size)
			return 0;
		if (strncmp((const char *)area + p + 4, name, 32) == 0) {
			if (val) *val = area + p + 0x28;
			if (len) *len = vl;
			return 1;
		}
		if (next == 0)
			return 0;
		p += next;
	}
	return 0;
}

static int node_name_is(const uint8_t *area, uint32_t size, uint32_t node, const char *name, size_t n)
{
	const uint8_t *v;
	uint32_t vl;
	if (!nw_bootinfo_get_prop(area, size, node, "name", &v, &vl))
		return 0;
	return strnlen((const char *)v, vl) == n && memcmp(v, name, n) == 0;
}

int nw_bootinfo_find_node(const uint8_t *area, uint32_t size, const char *path, uint32_t *node_off)
{
	if (area == NULL || size < 0x18 || rd32(area) != NW_BOOTINFO_MAGIC0)
		return 0;
	uint32_t node = NW_BOOTINFO_ROOT;
	const char *s = path ? path : "";
	while (*s == '/')
		s++;
	while (*s) {
		const char *e = strchr(s, '/');
		size_t n = e ? (size_t)(e - s) : strlen(s);
		uint32_t child = rd32(area + node + 4);
		if (child == 0)
			return 0;
		uint32_t c = node + child;
		int found = 0;
		for (int guard = 0; guard < 4096; guard++) {
			if (c + 12 > size)
				return 0;
			if (node_name_is(area, size, c, s, n)) {
				found = 1;
				break;
			}
			const uint32_t sib = rd32(area + c);
			if (sib == 0)
				break;
			c += sib;
		}
		if (!found)
			return 0;
		node = c;
		s += n;
		while (*s == '/')
			s++;
	}
	if (node_off)
		*node_off = node;
	return 1;
}

static int count_nodes(const uint8_t *area, uint32_t size, uint32_t node, int depth)
{
	if (depth > 64 || node + 12 > size)
		return 0;
	int n = 1;
	const uint32_t child = rd32(area + node + 4);
	if (child)
		n += count_nodes(area, size, node + child, depth + 1);
	const uint32_t sib = rd32(area + node);
	if (sib)
		n += count_nodes(area, size, node + sib, depth);
	return n;
}

int nw_bootinfo_count_nodes(const uint8_t *area, uint32_t size)
{
	if (area == NULL || size < 0x18 || rd32(area) != NW_BOOTINFO_MAGIC0)
		return 0;
	return count_nodes(area, size, NW_BOOTINFO_ROOT, 0);
}

int nw_parcels_keep(const uint8_t *romfile, size_t size)
{
	free(g_parcels);
	g_parcels = NULL;
	g_parcels_size = 0;
	if (romfile == NULL || size < 16)
		return 0;
	/* The CHRP boot script names the offset; fall back to a scan. */
	size_t off = size;
	for (size_t i = 0; i + 4 <= size; i++) {
		if (memcmp(romfile + i, "prcl", 4) == 0 && i + 16 <= size && rd32(romfile + i + 12) == 0x14u) {
			off = i;
			break;
		}
	}
	if (off >= size)
		return 0;
	g_parcels = (uint8_t *)malloc(size - off);
	if (g_parcels == NULL)
		return 0;
	memcpy(g_parcels, romfile + off, size - off);
	g_parcels_size = size - off;
	return 1;
}

const uint8_t *nw_parcels_get(size_t *size)
{
	if (size)
		*size = g_parcels_size;
	return g_parcels;
}

} // extern "C"
