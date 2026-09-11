/*
 * nwgolden.c - QEMU TCG plugin: golden New World boot event stream.
 *
 * Emits one line per event to `out=` (default stderr):
 *
 *   # nwgolden 1 target=<arch>
 *   X <t> <from> <to> [<dar>|<srr1>]   PPC exception / interrupt.
 *                                        t: E exception, I interrupt, H hostcall.
 *                                        from: faulting / interrupted PC.
 *                                        to: vector the CPU was steered to.
 *                                        DSI/alignment (0x300/0x600) append DAR;
 *                                        program (0x700) appends SRR1.
 *   A <op> <pc> <h>                       68k A-line dispatch inside the ROM
 *                                        emulator. op: 68k opcode word,
 *                                        pc: 68k address of that word,
 *                                        h: handler 0=OS 1=OS(no-A0) 2=Tool 3=Tool(autopop).
 *   T <epoch_ms> <nX> <nA>                Wall-clock tick, at most 1/s, for
 *                                        aligning QMP screenshots with the stream.
 *
 * The A-line handlers are found by instruction bytes, not by address, so no
 * ROM base needs to be configured: every entry in the emulator dispatch table
 * for opcodes A000-AFFF branches to one of four handlers that all start with
 * `lwz r5,0x28(r28)` (80bc0028) at emulator-relative offsets 0x695e0, 0x69660,
 * 0x69720, 0x69780 (MacROM+0x300000 + offset). At handler entry the emulator
 * inner loop has already done
 *     rlwimi r29,r27,3,13,28   ; r29 = table + opcode*8
 *     lhau   r27,2(r24)        ; r24 -> next 68k insn, r27 = prefetched word
 * so opcode = (r29 >> 3) & 0xffff and the A-line word is at r24 - 2.
 *
 * Build (macOS, Homebrew qemu):
 *   cc -O2 -shared -fPIC -Wall -undefined dynamic_lookup \
 *      -I/opt/homebrew/include $(pkg-config --cflags --libs glib-2.0) \
 *      -o libnwgolden.dylib nwgolden.c
 * Run:
 *   qemu-system-ppc ... -plugin ./libnwgolden.dylib,out=/path/events.txt[,int=0]
 */

#include <glib.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static FILE *out;
static bool log_interrupts = true;
static bool debug_regs = false;

static struct qemu_plugin_register *reg_r24;
static struct qemu_plugin_register *reg_r29;
static struct qemu_plugin_register *reg_dar;
static struct qemu_plugin_register *reg_srr1;
static GByteArray *regbuf;

static uint64_t n_x;
static uint64_t n_a;
static gint64 last_tick_us;

static const uint32_t aline_entry_word = 0x80bc0028u; /* lwz r5,0x28(r28) */
static const uint32_t aline_handler_off[4] = { 0x69660u, 0x695e0u, 0x69720u, 0x69780u };

static uint64_t read_reg(struct qemu_plugin_register *reg)
{
    if (!reg) {
        return 0;
    }
    g_byte_array_set_size(regbuf, 0);
    if (!qemu_plugin_read_register(reg, regbuf)) {
        return 0;
    }
    uint64_t value = 0;
    for (guint i = 0; i < regbuf->len; i++) {
        value = (value << 8) | regbuf->data[i]; /* target (big-endian) order */
    }
    return value;
}

static void maybe_tick(void)
{
    gint64 now = g_get_real_time();
    if (now - last_tick_us < G_USEC_PER_SEC) {
        return;
    }
    last_tick_us = now;
    fprintf(out, "T %" PRId64 " %" PRIu64 " %" PRIu64 "\n", now / 1000, n_x, n_a);
}

static void on_discon(unsigned int vcpu_index, enum qemu_plugin_discon_type type,
                      uint64_t from_pc, uint64_t to_pc, void *userdata)
{
    char kind;
    switch (type) {
    case QEMU_PLUGIN_DISCON_EXCEPTION: kind = 'E'; break;
    case QEMU_PLUGIN_DISCON_INTERRUPT:
        if (!log_interrupts) {
            return;
        }
        kind = 'I';
        break;
    case QEMU_PLUGIN_DISCON_HOSTCALL: kind = 'H'; break;
    default: kind = '?'; break;
    }
    n_x++;
    uint32_t vec = (uint32_t)(to_pc & 0xffffu);
    if (vec == 0x300 || vec == 0x600) {
        fprintf(out, "X %c %08" PRIx64 " %08" PRIx64 " %08" PRIx64 "\n",
                kind, from_pc, to_pc, read_reg(reg_dar));
    } else if (vec == 0x700) {
        fprintf(out, "X %c %08" PRIx64 " %08" PRIx64 " %08" PRIx64 "\n",
                kind, from_pc, to_pc, read_reg(reg_srr1));
    } else {
        fprintf(out, "X %c %08" PRIx64 " %08" PRIx64 "\n", kind, from_pc, to_pc);
    }
    if ((n_x & 0xfff) == 0) {
        maybe_tick();
    }
}

static void on_aline(unsigned int vcpu_index, void *userdata)
{
    n_a++;
    int handler = (int)(uintptr_t)userdata;
    uint64_t r29 = read_reg(reg_r29);
    uint64_t r24 = read_reg(reg_r24);
    fprintf(out, "A %04" PRIx64 " %08" PRIx64 " %d\n",
            (r29 >> 3) & 0xffffu, (r24 - 2) & 0xffffffffu, handler);
    if ((n_a & 0x3ff) == 0) {
        maybe_tick();
    }
}

static void on_tb_trans(struct qemu_plugin_tb *tb, void *userdata)
{
    size_t n = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t vaddr = qemu_plugin_insn_vaddr(insn);
        uint32_t low = (uint32_t)(vaddr & 0xfffffu);
        int handler = -1;
        for (int h = 0; h < 4; h++) {
            if (low == aline_handler_off[h]) {
                handler = h;
                break;
            }
        }
        if (handler < 0) {
            continue;
        }
        uint8_t bytes[4];
        if (qemu_plugin_insn_data(insn, bytes, sizeof bytes) != sizeof bytes) {
            continue;
        }
        uint32_t word = ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16)
                      | ((uint32_t)bytes[2] << 8) | bytes[3];
        if (word != aline_entry_word) {
            continue;
        }
        qemu_plugin_register_vcpu_insn_exec_cb(insn, on_aline, QEMU_PLUGIN_CB_R_REGS,
                                               (void *)(uintptr_t)handler);
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
    if (debug_regs) {
        for (guint i = 0; i < regs->len; i++) {
            qemu_plugin_reg_descriptor *d = &g_array_index(regs, qemu_plugin_reg_descriptor, i);
            fprintf(stderr, "nwgolden: reg %s (%s)\n", d->name, d->feature ? d->feature : "-");
        }
    }
    reg_r24 = find_reg(regs, "r24");
    reg_r29 = find_reg(regs, "r29");
    reg_dar = find_reg(regs, "dar");
    reg_srr1 = find_reg(regs, "srr1");
    if (!reg_r24 || !reg_r29) {
        fprintf(stderr, "nwgolden: r24/r29 not readable; A-line events will be zero\n");
    }
    if (!reg_dar || !reg_srr1) {
        fprintf(stderr, "nwgolden: dar/srr1 not exposed; DSI DAR / program SRR1 will be zero\n");
    }
    g_array_free(regs, TRUE);
}

static void on_exit(void *userdata)
{
    fprintf(out, "T %" PRId64 " %" PRIu64 " %" PRIu64 "\n",
            g_get_real_time() / 1000, n_x, n_a);
    fprintf(out, "# end nX=%" PRIu64 " nA=%" PRIu64 "\n", n_x, n_a);
    fflush(out);
    if (out != stderr) {
        fclose(out);
    }
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                                           int argc, char **argv)
{
    out = stderr;
    for (int i = 0; i < argc; i++) {
        char **kv = g_strsplit(argv[i], "=", 2);
        if (kv[0] && kv[1]) {
            if (g_strcmp0(kv[0], "out") == 0) {
                out = fopen(kv[1], "w");
                if (!out) {
                    fprintf(stderr, "nwgolden: cannot open %s\n", kv[1]);
                    g_strfreev(kv);
                    return -1;
                }
                setvbuf(out, NULL, _IOFBF, 1 << 20);
            } else if (g_strcmp0(kv[0], "int") == 0) {
                log_interrupts = g_strcmp0(kv[1], "0") != 0 && g_strcmp0(kv[1], "off") != 0;
            } else if (g_strcmp0(kv[0], "debug") == 0) {
                debug_regs = true;
            }
        }
        g_strfreev(kv);
    }
    regbuf = g_byte_array_new();
    fprintf(out, "# nwgolden 1 target=%s\n", info->target_name);

    qemu_plugin_register_vcpu_init_cb(id, on_vcpu_init, NULL);
    qemu_plugin_register_vcpu_tb_trans_cb(id, on_tb_trans, NULL);
    qemu_plugin_register_vcpu_discon_cb(id, QEMU_PLUGIN_DISCON_ALL, on_discon, NULL);
    qemu_plugin_register_atexit_cb(id, on_exit, NULL);
    return 0;
}
