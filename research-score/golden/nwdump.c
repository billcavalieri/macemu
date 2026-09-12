/*
 * nwdump.c - QEMU TCG plugin: dump the New World NK handoff data (golden).
 *
 * Three triggers, matched by low 20 PC bits + instruction bytes (so the NK may
 * run from ROM 0x68310000 or its RAM copy 0x00F10000):
 *
 *   1. NK entry     (+0x10000, `b 0x31000c` 4800000c)
 *      r3 = NKConfigurationInfo, r4 = ProcessorInfo, r5 = SystemInfo,
 *      r6 = DiagInfo. Dumps regs + 0x4000 @r3, 0x400 @r4, 0x400 @r5, 0x400 @r6.
 *   2. "Converting PMDTs to areas" (+0x1e618, `mflr r16` 7e0802a6)
 *      r1 = KDP. Dumps regs + [KDP-0x4000, KDP+0x4000) so the NK-built segment
 *      map (KDP+0x80) and the PMDTs it points at are on disk.
 *   3. 68k hardware-info consumed (+0x61c14, emulator `stb` 7c92d9ae): the
 *      68k StartInit has read the 'Hnfo' record through KDP+0xfd0 and is
 *      writing its first VIA register. Dumps regs, the record and the
 *      records it points at (+0x8 DecoderInfo copy, +0x10/+0x14 tables,
 *      +0xa8), LA->PA translations (hwinfo-xlate.txt), low memory 0..0x3000,
 *      the ConfigInfo page, KDP and IRP as the 68k sees them.
 *
 * Files land in `dir=`: nkentry-regs.txt nkentry-r3.bin nkentry-r4.bin
 * nkentry-r5.bin nkentry-r6.bin nkentry-r9.bin (hardware-info block when
 * r7 == 'RTAS') pmdt-regs.txt pmdt-kdp.bin pmdt-pa0.bin
 * (physical 0..0xffff: exception vectors + relocated low memory)
 * hwinfo-*.bin hwinfo-xlate.txt. The plugin exits QEMU after the third dump
 * (`exit=0` to keep running).
 *
 * Build:
 *   cc -O2 -shared -fPIC -Wall -undefined dynamic_lookup \
 *      -I/opt/homebrew/include $(pkg-config --cflags --libs glib-2.0) \
 *      -o libnwdump.dylib nwdump.c
 */

#include <glib.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static char *dir;
static bool do_exit = true;
static GByteArray *buf;
static struct qemu_plugin_register *gpr[32];
static struct qemu_plugin_register *reg_msr, *reg_lr;
static bool done_entry, done_pmdt;

struct trigger {
    uint32_t low20;
    uint32_t word;
    void (*fn)(uint64_t pc);
};

static uint64_t read_reg(struct qemu_plugin_register *reg)
{
    if (!reg) {
        return 0;
    }
    g_byte_array_set_size(buf, 0);
    if (!qemu_plugin_read_register(reg, buf)) {
        return 0;
    }
    uint64_t v = 0;
    for (guint i = 0; i < buf->len; i++) {
        v = (v << 8) | buf->data[i];
    }
    return v;
}

static void dump_regs(const char *name, uint64_t pc)
{
    char *path = g_strdup_printf("%s/%s-regs.txt", dir, name);
    FILE *f = fopen(path, "w");
    g_free(path);
    if (!f) {
        return;
    }
    fprintf(f, "pc=%08" PRIx64 " lr=%08" PRIx64 " msr=%08" PRIx64 "\n",
            pc, read_reg(reg_lr), read_reg(reg_msr));
    for (int i = 0; i < 32; i++) {
        fprintf(f, "r%d=%08" PRIx64 "\n", i, read_reg(gpr[i]));
    }
    fclose(f);
}

static void dump_mem(const char *name, uint64_t addr, size_t len)
{
    g_byte_array_set_size(buf, 0);
    if (!qemu_plugin_read_memory_vaddr(addr, buf, len)) {
        fprintf(stderr, "nwdump: read %08" PRIx64 "+%zx failed (%s)\n", addr, len, name);
        return;
    }
    char *path = g_strdup_printf("%s/%s.bin", dir, name);
    FILE *f = fopen(path, "wb");
    g_free(path);
    if (!f) {
        return;
    }
    fwrite(buf->data, 1, buf->len, f);
    fclose(f);
}

static void dump_mem_pa(const char *name, uint64_t pa, size_t len)
{
    g_byte_array_set_size(buf, 0);
    enum qemu_plugin_hwaddr_operation_result r = qemu_plugin_read_memory_hwaddr(pa, buf, len);
    if (r != QEMU_PLUGIN_HWADDR_OPERATION_OK) {
        fprintf(stderr, "nwdump: read PA %08" PRIx64 "+%zx failed (%s, %d)\n", pa, len, name, (int)r);
        return;
    }
    char *path = g_strdup_printf("%s/%s.bin", dir, name);
    FILE *f = fopen(path, "wb");
    g_free(path);
    if (!f) {
        return;
    }
    fwrite(buf->data, 1, buf->len, f);
    fclose(f);
}

static void on_nk_entry(uint64_t pc)
{
    if (done_entry) {
        return;
    }
    done_entry = true;
    fprintf(stderr, "nwdump: NK entry at %08" PRIx64 "\n", pc);
    dump_regs("nkentry", pc);
    dump_mem("nkentry-r3", read_reg(gpr[3]), 0x4000);
    dump_mem("nkentry-r4", read_reg(gpr[4]), 0x400);
    dump_mem("nkentry-r5", read_reg(gpr[5]), 0x400);
    if (read_reg(gpr[6])) {
        dump_mem("nkentry-r6", read_reg(gpr[6]), 0x400);
    }
    /* r7 == 'RTAS': r9 -> 0xc0-byte hardware-info block ('Hnfo' at +0x70),
     * copied by the NK to IRP+0xf00 and handed to the 68k via KDP+0xfd0. */
    if (read_reg(gpr[7]) == 0x52544153u && read_reg(gpr[9])) {
        dump_mem("nkentry-r9", read_reg(gpr[9]), 0x400);
    }
}

static void on_pmdt(uint64_t pc)
{
    if (done_pmdt) {
        return;
    }
    done_pmdt = true;
    fprintf(stderr, "nwdump: PMDT convert at %08" PRIx64 "\n", pc);
    dump_regs("pmdt", pc);
    uint64_t kdp = read_reg(gpr[1]);
    dump_mem("pmdt-kdp", kdp - 0x4000, 0x8000);
    dump_mem("pmdt-pa0", 0, 0x10000);
}

static uint32_t rd32(uint64_t addr)
{
    g_byte_array_set_size(buf, 0);
    if (!qemu_plugin_read_memory_vaddr(addr, buf, 4) || buf->len < 4) {
        return 0;
    }
    return ((uint32_t)buf->data[0] << 24) | ((uint32_t)buf->data[1] << 16) |
           ((uint32_t)buf->data[2] << 8) | buf->data[3];
}

static void xlate_note(FILE *f, const char *what, uint64_t la)
{
    uint64_t pa = 0;
    bool ok = qemu_plugin_translate_vaddr(la, &pa);
    fprintf(f, "%s la=%08" PRIx64 " pa=%s%08" PRIx64 "\n", what, la, ok ? "" : "(unmapped) ", pa);
}

/* Third dump: the 68k has consumed the hardware-info record and touches its
 * first VIA register (emulator stb at ROM+0x361c14; golden DAR 0x80017e00).
 * By now the NK has mapped the record's pointees, so they can be read by LA. */
static void on_68k_hwinfo(uint64_t pc)
{
    static bool done;
    if (done) {
        return;
    }
    done = true;
    fprintf(stderr, "nwdump: 68k hardware-info consumed at %08" PRIx64 "\n", pc);
    dump_regs("hwinfo", pc);
    /* KDP = SPRG0 is not exposed; the NK keeps KDP+0xfd0 = LA_InfoRecord+0xf00. */
    const uint64_t kdp_la = 0x68ffe000u, hnfo_la = rd32(kdp_la + 0xfd0);
    char *path = g_strdup_printf("%s/hwinfo-xlate.txt", dir);
    FILE *f = fopen(path, "w");
    g_free(path);
    if (!f) {
        return;
    }
    fprintf(f, "hnfo=%08" PRIx64 "\n", hnfo_la);
    dump_mem("hwinfo-hnfo", hnfo_la, 0x100);
    static const struct { const char *name; uint32_t off; uint32_t len; } ptrs[] = {
        { "hwinfo-p08", 0x08, 0x400 },
        { "hwinfo-p10", 0x10, 0x100 },
        { "hwinfo-p14", 0x14, 0x100 },
        { "hwinfo-pa8", 0xa8, 0x400 },
    };
    for (size_t i = 0; i < G_N_ELEMENTS(ptrs); i++) {
        uint64_t la = rd32(hnfo_la + ptrs[i].off);
        xlate_note(f, ptrs[i].name, la);
        if (la) {
            dump_mem(ptrs[i].name, la & ~0xffull, ptrs[i].len);
        }
    }
    xlate_note(f, "lowmem0", 0);
    xlate_note(f, "lowmem2000", 0x2000);
    xlate_note(f, "kdp", kdp_la);
    xlate_note(f, "edp", 0x68fff000u);
    xlate_note(f, "irp", 0x5fffe000u);
    xlate_note(f, "configinfo", 0x68fef000u);
    xlate_note(f, "via", 0x80016000u);
    xlate_note(f, "rom68k", 0xffc00000u);
    fclose(f);
    dump_mem("hwinfo-kdp", kdp_la, 0x1000);
    dump_mem("hwinfo-irp", 0x5fffe000u, 0x1000);
    /* By physical address (the vaddr translate only sees the current TLB):
     * relocated low memory (ConfigInfo+0x360 = PA 0x4000 on mac99), the
     * ConfigInfo page, and the Trampoline's boot-info area behind the
     * record's pointers (ConfigInfo PMDT seg 6: LA 0x64000000, 384 pages ->
     * PA 0x15600000 on mac99; derived here from the +0x8 translation). */
    dump_mem_pa("hwinfo-lowmem-pa", 0x4000, 0x3000);
    dump_mem_pa("hwinfo-configinfo-pa", 0x3000, 0x1000);
    {
        uint64_t la = rd32(hnfo_la + 0x08), pa = 0;
        if (la && qemu_plugin_translate_vaddr(la, &pa)) {
            const uint64_t base = pa - (la - 0x64000000u);
            fprintf(stderr, "nwdump: boot-info area PA %08" PRIx64 "\n", base);
            dump_mem_pa("hwinfo-bootinfo-pa", base, 0x180000);
        }
    }
    if (do_exit) {
        fflush(stderr);
        exit(0);
    }
}

static const struct trigger triggers[] = {
    { 0x10000u, 0x4800000cu, on_nk_entry },
    { 0x1e618u, 0x7e0802a6u, on_pmdt },
    { 0x61c14u, 0x7c92d9aeu, on_68k_hwinfo },
};

static void on_exec(unsigned int vcpu_index, void *userdata)
{
    const struct trigger *t = userdata;
    t->fn(t->low20);
}

static void on_tb_trans(struct qemu_plugin_tb *tb, void *userdata)
{
    size_t n = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t vaddr = qemu_plugin_insn_vaddr(insn);
        uint32_t low = (uint32_t)(vaddr & 0xfffffu);
        for (size_t k = 0; k < G_N_ELEMENTS(triggers); k++) {
            if (low != triggers[k].low20) {
                continue;
            }
            uint8_t b[4];
            if (qemu_plugin_insn_data(insn, b, 4) != 4) {
                continue;
            }
            uint32_t w = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
            if (w != triggers[k].word) {
                continue;
            }
            qemu_plugin_register_vcpu_insn_exec_cb(insn, on_exec, QEMU_PLUGIN_CB_R_REGS,
                                                   (void *)&triggers[k]);
        }
    }
}

static struct qemu_plugin_register *find_reg(GArray *regs, const char *name)
{
    for (guint i = 0; i < regs->len; i++) {
        qemu_plugin_reg_descriptor *d = &g_array_index(regs, qemu_plugin_reg_descriptor, i);
        if (g_strcmp0(d->name, name) == 0) {
            return d->handle;
        }
    }
    return NULL;
}

static void on_vcpu_init(unsigned int vcpu_index, void *userdata)
{
    if (vcpu_index != 0) {
        return;
    }
    GArray *regs = qemu_plugin_get_registers();
    for (int i = 0; i < 32; i++) {
        char name[8];
        snprintf(name, sizeof name, "r%d", i);
        gpr[i] = find_reg(regs, name);
    }
    reg_msr = find_reg(regs, "msr");
    reg_lr = find_reg(regs, "lr");
    g_array_free(regs, TRUE);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                                           int argc, char **argv)
{
    dir = g_strdup(".");
    for (int i = 0; i < argc; i++) {
        char **kv = g_strsplit(argv[i], "=", 2);
        if (kv[0] && kv[1]) {
            if (g_strcmp0(kv[0], "dir") == 0) {
                g_free(dir);
                dir = g_strdup(kv[1]);
            } else if (g_strcmp0(kv[0], "exit") == 0) {
                do_exit = g_strcmp0(kv[1], "0") != 0;
            }
        }
        g_strfreev(kv);
    }
    buf = g_byte_array_new();
    qemu_plugin_register_vcpu_init_cb(id, on_vcpu_init, NULL);
    qemu_plugin_register_vcpu_tb_trans_cb(id, on_tb_trans, NULL);
    return 0;
}
