/* Tests execute the real KPX decoder and instruction methods. Linked against
 * an isolated app build, with a private entry point and deterministic TB clock;
 * no GUI, ROM, disk or guest device callback is initialized. */
#include "sysdeps.h"
#include "cpu/ppc/ppc-cpu.hpp"
#include "nw_jit.h"
#include "nw_jit_verify.h"
#include "nw_io.h"
#include "nw_boot_contract.h"
#include <fenv.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

extern uint32_t nw_jit_helper_lwz_pa(nw_jit_cpu *, uint32_t, uint32_t);
extern void nw_jit_helper_stw_pa(nw_jit_cpu *, uint32_t, uint32_t, uint32_t);
extern uint32_t nw_jit_helper_lb(nw_jit_cpu *, uint32_t);
extern uint32_t nw_jit_helper_lh(nw_jit_cpu *, uint32_t);
extern void nw_jit_helper_stb(nw_jit_cpu *, uint32_t, uint32_t);
extern void nw_jit_helper_sth(nw_jit_cpu *, uint32_t, uint32_t);
extern void nw_jit_helper_lfd(nw_jit_cpu *, uint32_t, uint32_t, uint32_t);
extern void nw_jit_helper_stfd(nw_jit_cpu *, uint32_t, uint32_t, uint32_t);
static unsigned passed, failed;
#define CHECK(c) do { if (c) ++passed; else { ++failed; fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); } } while (0)
static uint64 fake_us;
uint64 ppc_test_ticks_usec() { return fake_us; }

struct verify_device { unsigned reads = 0, writes = 0; uint32 value = 10; };
static uint32 device_read(void *context, uint32, int) {
    verify_device &d = *static_cast<verify_device *>(context);
    ++d.reads; return d.value++;
}
static void device_write(void *context, uint32, int, uint32 value) {
    verify_device &d = *static_cast<verify_device *>(context);
    ++d.writes; d.value = value;
}

struct ppc_core_test_access {
    static int run();
    static void instruction(powerpc_cpu *cpu, uint32 op) { cpu->decode(op)->execute(cpu, op); }
};
static uint64 bits(double d) { uint64 b; memcpy(&b, &d, 8); return b; }
int ppc_core_test_access::run()
{
    powerpc_cpu *cpu = new powerpc_cpu;
    fenv_t caller; fegetenv(&caller);
    const int modes[] = {FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD};
    for (unsigned host = 0; host < 4; ++host)
    for (unsigned rn = 0; rn < 4; ++rn)
    for (unsigned neg = 0; neg < 2; ++neg)
    for (unsigned operation = 0; operation < 11; ++operation)
    for (unsigned writer = 0; writer < 3; ++writer) {
        const uint64 sign = neg ? UINT64_C(0x8000000000000000) : 0;
        const bool away = neg ? rn == 3 : rn == 2;
        uint32 op; uint64 expected;
        cpu->fpscr() = 0; cpu->cr().set(0); cpu->pc() = 0x1000;
        if (operation < 3) {
            cpu->fpr_dw(1) = bits(operation == 1 ? 9007199254740992.0 : 16777216.0) ^ sign;
            cpu->fpr_dw(2) = bits(1.0) ^ sign;
            op = ((operation == 0 ? 59u : 63u) << 26) | (3u<<21) | (1u<<16) | (2u<<11) | (21u<<1);
            expected = bits(operation == 1 ? (away ? 9007199254740994.0 : 9007199254740992.0)
                                           : (away ? 16777218.0 : 16777216.0)) ^ sign;
            if (operation == 2) { cpu->fpr_dw(1) = bits(16777217.0) ^ sign; op = nw_ppc_frsp(3,1); }
        } else if (operation < 5) {
            cpu->fpr_dw(1) = bits(1.5) ^ sign;
            op = (63u<<26)|(3u<<21)|(1u<<11)|((operation == 3 ? 14u : 15u)<<1);
            int value = operation == 4 ? 1 : (rn == 0 || away ? 2 : 1);
            if (neg) value = -value;
            expected = (uint64)(int64)value;
        } else if (operation < 7) {
            cpu->fpr_dw(1) = bits(1.0) ^ sign; cpu->fpr_dw(2) = bits(10.0);
            op = ((operation == 5 ? 63u : 59u)<<26)|(3u<<21)|(1u<<16)|(2u<<11)|(18u<<1);
            const bool high = rn == 0 || away;
            expected = (operation == 5 ? (high ? UINT64_C(0x3fb999999999999a) : UINT64_C(0x3fb9999999999999))
                                      : (high ? UINT64_C(0x3fb99999a0000000) : UINT64_C(0x3fb9999980000000))) ^ sign;
        }
        if (operation >= 7) {
            cpu->fpr_dw(1) = UINT64_C(0x3ff0000000000001) ^ sign;
            cpu->fpr_dw(2) = UINT64_C(0x3feffffffffffffe);
            const unsigned xo = 28 + operation - 7;
            cpu->fpr_dw(5) = bits(xo & 1 ? -1.0 : 1.0) ^ sign;
            op = (59u<<26)|(3u<<21)|(1u<<16)|(5u<<11)|(2u<<6)|(xo<<1);
            expected = (xo < 30 ? UINT64_C(0xb970000000000000) : UINT64_C(0x3970000000000000)) ^ sign;
        }
        fesetround(modes[host]); feclearexcept(FE_ALL_EXCEPT); feraiseexcept(FE_DIVBYZERO);
        const int flags = fetestexcept(FE_ALL_EXCEPT);
        if (writer == 0) ppc_core_test_access::instruction(cpu, (63u<<26)|(7u<<23)|(rn<<12)|(134u<<1));
        else if (writer == 1) { cpu->fpr_dw(4) = rn; ppc_core_test_access::instruction(cpu, nw_ppc_mtfsf(1,4)); }
        else {
            ppc_core_test_access::instruction(cpu, (63u<<26)|(30u<<21)|((rn&2 ? 38u : 70u)<<1));
            ppc_core_test_access::instruction(cpu, (63u<<26)|(31u<<21)|((rn&1 ? 38u : 70u)<<1));
        }
        CHECK(fegetround() == modes[host]); CHECK(fetestexcept(FE_ALL_EXCEPT) == flags);
        ppc_core_test_access::instruction(cpu, op);
        // Upper conversion word is unspecified by the ISA; both engines use KPX sign extension.
        CHECK(operation == 3 || operation == 4 ? (uint32)cpu->fpr_dw(3) == (uint32)expected : cpu->fpr_dw(3) == expected);
        CHECK((cpu->fpscr() & 3u) == rn);
        CHECK(fegetround() == modes[host]); CHECK(fetestexcept(FE_ALL_EXCEPT) == flags);
    }
    struct summary_case { uint32 initial, op, expected, cr; };
    const summary_case cases[] = {
        {0, (63u<<26)|(9u<<12)|(134u<<1)|1u, 0x90000000u, 0x09000000u},
        {0x01000080u, (63u<<26)|(134u<<1)|1u, 0x61000080u, 0x06000000u},
        {0, (63u<<26)|(1u<<21)|(38u<<1)|1u, 0, 0},
        {0, (63u<<26)|(2u<<21)|(38u<<1)|1u, 0, 0},
        {0, (63u<<26)|(7u<<21)|(38u<<1)|1u, 0xa1000000u, 0x0a000000u},
        {0xe1000080u, (63u<<26)|(7u<<21)|(70u<<1)|1u, 0x80000080u, 0x08000000u},
        {0x61000080u, (63u<<26)|(2u<<23)|(1u<<18)|(64u<<1), 0x80u, 0x00100000u},
        {0x50000040u, (63u<<26)|(2u<<23)|(64u<<1), 0x40u, 0x00500000u}
    };
    for (const summary_case &c : cases) {
        cpu->fpscr() = c.initial; cpu->cr().set(0);
        ppc_core_test_access::instruction(cpu, c.op);
        if (cpu->fpscr() != c.expected || cpu->cr().get() != c.cr) fprintf(stderr, "op=%08x fpscr=%08x expected=%08x cr=%08x expected=%08x\n", c.op, cpu->fpscr(), c.expected, cpu->cr().get(), c.cr);
        CHECK(cpu->fpscr() == c.expected); CHECK(cpu->cr().get() == c.cr);
    }
#if defined(__aarch64__)
    uint64 control, status;
    __asm__ volatile("mrs %0, fpcr\n\tmrs %1, fpsr" : "=r"(control), "=r"(status));
    const uint64 flushed = control | UINT64_C(0x3000000);
    __asm__ volatile("msr fpcr, %0" :: "r"(flushed) : "memory");
    cpu->fpscr() = 0; cpu->fpr_dw(1) = UINT64_C(0x3810000000000000); cpu->fpr_dw(2) = UINT64_C(0x3800000000000000);
    instruction(cpu, (59u<<26)|(3u<<21)|(1u<<16)|(2u<<11)|(20u<<1));
    CHECK(cpu->fpr_dw(3) == UINT64_C(0x3800000000000000));
    CHECK((cpu->fpscr() & 0x1f000u) == 0x14000u);
    cpu->fpr_dw(1) = UINT64_C(0x7ff0000000000001); cpu->fpr_dw(2) = UINT64_C(0x3ff0000000000000);
    instruction(cpu, (63u<<26)|(3u<<23)|(1u<<16)|(2u<<11)|(32u<<1));
    uint64 restored_control, restored_status;
    __asm__ volatile("mrs %0, fpcr\n\tmrs %1, fpsr" : "=r"(restored_control), "=r"(restored_status));
    if (restored_control != flushed || restored_status != status) fprintf(stderr,"control=%llx expected=%llx status=%llx expected=%llx\n", restored_control, flushed, restored_status, status);
    CHECK(restored_control == flushed && restored_status == status);
    CHECK((cpu->cr().get() & 0x000f0000u) == 0x00010000u);
    __asm__ volatile("msr fpcr, %0\n\tmsr fpsr, %1" :: "r"(control), "r"(status) : "memory");
#endif
    // Exercise the actual CPU timer, privilege rules, exception delivery and
    // JIT DEC callback. The replacement clock is private to this executable.
    cpu->enable_guest_mmu(true);
    ppc32_mmu &mmu = ppc32_guest_mmu();
    cpu->dec_ = 24; cpu->dec_pending_ = false; cpu->dec_tb_base_ = 0;
    fake_us = 1; cpu->sample_decrementer();
    CHECK(cpu->dec_ == 0xffffffffu && cpu->dec_pending_);
    cpu->pc() = 0x1000; mmu.set_msr(ppc32_mmu::MSR_EE);
    cpu->take_dec(); CHECK(!cpu->dec_pending_ && cpu->pc() == 0x900u);
    fake_us = 2; cpu->sample_decrementer();
    CHECK(cpu->dec_ == 0xffffffe6u && !cpu->dec_pending_); // negative is not a new edge
    cpu->advance_decrementer(UINT64_C(0x100000000));
    CHECK(cpu->dec_ == 0xffffffe6u && cpu->dec_pending_); // full wrap is a new edge
    cpu->dec_pending_ = false; cpu->dec_ = 0x80000000u;
    cpu->advance_decrementer(UINT64_C(0x80000000));
    CHECK(cpu->dec_ == 0 && !cpu->dec_pending_);
    cpu->advance_decrementer(1); CHECK(cpu->dec_ == 0xffffffffu && cpu->dec_pending_);
    cpu->dec_pending_ = false; cpu->dec_ = 1000; cpu->dec_tb_base_ = cpu->tb_host_ticks();
    mmu.set_msr(0); CHECK(cpu->mtspr_oea(powerpc_registers::SPR_DEC, 0x80000000u));
    CHECK(cpu->dec_pending_); CHECK(!cpu->jit_events_pending()); // EE masks delivery
    CHECK(cpu->mtspr_oea(powerpc_registers::SPR_DEC, 1000));
    CHECK(cpu->dec_pending_); // a positive write does not cancel an existing request
    mmu.set_msr(ppc32_mmu::MSR_EE); CHECK(cpu->jit_events_pending());
    cpu->pc() = 0x1200; nw_io_ext_irq = 1; cpu->take_async_exception();
    CHECK(cpu->pc() == 0x500u && cpu->dec_pending_ && cpu->srr0_ == 0x1200);
    nw_io_ext_irq = 0; mmu.set_msr(ppc32_mmu::MSR_EE); cpu->take_async_exception();
    CHECK(cpu->pc() == 0x900u && !cpu->dec_pending_);
    cpu->spcflags().set(SPCFLAG_CPU_HANDLE_INTERRUPT); CHECK(cpu->jit_events_pending());
    cpu->spcflags().clear(SPCFLAG_CPU_HANDLE_INTERRUPT);
    cpu->spcflags().set(SPCFLAG_JIT_EXEC_RETURN); CHECK(!cpu->jit_events_pending());
    cpu->spcflags().clear(SPCFLAG_JIT_EXEC_RETURN);
    // Guest time-base writes cannot advance or rewind the DEC clock.
    cpu->dec_ = 1000; cpu->dec_pending_ = false; cpu->dec_tb_base_ = cpu->tb_host_ticks();
    cpu->mtspr_guest(powerpc_registers::SPR_TBL_W, 0x90000000u);
    cpu->sample_decrementer(); CHECK(cpu->dec_ == 1000 && !cpu->dec_pending_);
    fake_us += 1; cpu->sample_decrementer(); CHECK(cpu->dec_ == 975 && !cpu->dec_pending_);
    // Both standalone generated DEC instructions and the KPX decoder use
    // the CPU-owned callback; privileged faults suppress destinations/suffixes.
    nw_jit_set_mode(NW_JIT_ON);
    for (unsigned native = 0; native < 2; ++native)
    for (unsigned write = 0; write < 2; ++write)
    for (unsigned user = 0; user < 2; ++user) {
        cpu->dec_ = 1000; cpu->dec_pending_ = false; cpu->dec_tb_base_ = cpu->tb_host_ticks();
        const uint32 start = 0x2000u + 0x40u * (native * 4 + write * 2 + user);
        cpu->gpr(3) = 0x80000000u; cpu->pc() = start; mmu.set_msr(user ? ppc32_mmu::MSR_PR : 0);
        const uint32 spr = powerpc_registers::SPR_DEC;
        const uint32 op = (31u<<26)|(3u<<21)|((spr&31u)<<16)|((spr>>5)<<11)|((write ? 467u : 339u)<<1);
        fake_us += 1;
        if (native) {
            nw_jit_cpu shadow = {}; shadow.host = cpu; shadow.gpr[3] = cpu->gpr(3);
            uint32 ops[] = {op, nw_ppc_addi(5,0,7), nw_ppc_blr()};
            nw_jit_fn fn = nw_jit_compile(ops, 3, start, start & ~0xfffu, 0, 0);
            CHECK(fn != NULL); if (fn) fn(&shadow);
            CHECK(shadow.gpr[5] == 0);
            CHECK(shadow.pc == (user ? 0x700u : start + 4));
            CHECK(shadow.fault == (user ? NW_JIT_FAULT_EXC : 0));
            CHECK(shadow.gpr[3] == (user || write ? 0x80000000u : 975u));
        } else {
            ppc_core_test_access::instruction(cpu, op);
            CHECK(cpu->pc() == (user ? 0x700u : start + 4));
            CHECK(cpu->gpr(3) == (user || write ? 0x80000000u : 975u));
        }
        CHECK(cpu->dec_ == (user ? 1000u : (write ? 0x80000000u : 975u)));
        CHECK(cpu->dec_pending_ == (!user && write));
    }

    // Independent live verification: destructive I/O executes only in KPX.
    // Poison the live DTLB with a usable RAM pointer: replay must not read it.
    nw_jit_set_mode(NW_JIT_VERIFY);
    verify_device device;
    nw_io_reset();
    struct nw_io_device dev = {"verify-fixture", NW_IO_VIA_PMU_BASE, 0x100, device_read, device_write, &device};
    CHECK(nw_io_register(&dev) == 0);
    for (unsigned translated = 0; translated < 2; ++translated) {
        // DR on with a direct BAT exercises the private invalid-DTLB miss path.
        mmu.reset(); mmu.set_msr(translated ? ppc32_mmu::MSR_DR : 0);
        if (translated) {
            mmu.set_dbat(0, (NW_IO_VIA_PMU_BASE & 0xfffe0000u) | 2u, (NW_IO_VIA_PMU_BASE & 0xfffe0000u) | 2u);
        }
        const uint32 start = 0x6000 + translated * 0x100;
        for (int i = 0; i < 32; ++i) { cpu->gpr(i) = 0; cpu->fpr_dw(i) = 0; memset(cpu->vr(i).w, 0, 16); }
        cpu->cr().set(0); cpu->xer().set(0); cpu->fpscr() = 0; cpu->vscr().set(0);
        cpu->pc() = start; cpu->lr() = 0; cpu->ctr() = 0; cpu->last_fetch_pa_ = start;
        cpu->gpr(4) = NW_IO_VIA_PMU_BASE; cpu->dec_ = 1000;
        nw_jit_cpu shadow = {};
        shadow.pc = start; shadow.gpr[4] = cpu->gpr(4); shadow.dec = cpu->dec_; shadow.msr = mmu.msr();
        uint32 ops[] = {nw_ppc_lwz(3,4,0), nw_ppc_addi(3,3,7), nw_ppc_stw(3,4,0), nw_ppc_lwz(5,4,0)};
        nw_jit_fn fn = nw_jit_compile(ops, 4, start, start & ~0xfffu, translated ? 2 : 0, 0);
        CHECK(fn != NULL);
        unsigned reads = device.reads, writes = device.writes;
        const uint32 before = device.value;
        const uint64 misses = nw_jit_verify_misses();
        uint8 poison[4096] = {}; poison[0] = 0x7f;
        nw_jit_dtlb_fill(cpu->gpr(4), cpu->gpr(4), 1, (uint64)(uintptr)poison, 0, 1);
        if (fn) CHECK(cpu->nw_jit_verify_block(shadow, fn, ops, 4) == 1);
        CHECK(nw_jit_verify_misses() == misses);
        CHECK(device.reads == reads + 2 && device.writes == writes + 1);
        CHECK(device.value == before + 8);
        CHECK(cpu->gpr(3) == before + 7 && cpu->gpr(5) == before + 7);
        CHECK(shadow.gpr[3] == cpu->gpr(3) && shadow.gpr[5] == cpu->gpr(5));
        CHECK(poison[0] == 0x7f && poison[1] == 0 && poison[2] == 0 && poison[3] == 0);
        CHECK(cpu->nw_verify_trace_ == NULL && shadow.verify_mem == NULL && shadow.host == NULL);
    }

    // Real RAM store/load replay, translated aliases, update/reversed/narrow
    // forms and FP memory. Only KPX owns the mapped memory; replay sees tape.
    const uint32 ram_base = 0x10000000u;
    void *const wanted = (void *)(VMBaseDiff + ram_base);
    void *const ram = mmap(wanted, 0x20000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    CHECK(ram == wanted);
    if (ram == wanted) {
        nw_banks_set(NW_PA_RAM, ram_base, 0x20000);
        struct memory_pair { unsigned store, load, width; bool indexed, fp; } pairs[] = {
            {36,32,4,false,false}, {37,33,4,false,false}, {38,34,1,false,false},
            {39,35,1,false,false}, {44,40,2,false,false}, {45,41,2,false,false},
            {44,42,2,false,false}, {45,43,2,false,false},
            {151,23,4,true,false}, {183,55,4,true,false}, {215,87,1,true,false},
            {247,119,1,true,false}, {407,279,2,true,false}, {439,311,2,true,false},
            {407,343,2,true,false}, {439,375,2,true,false},
            {662,534,4,true,false}, {918,790,2,true,false},
            {54,50,8,false,true}, {55,51,8,false,true}, {52,48,4,false,true}, {53,49,4,false,true},
            {727,599,8,true,true}, {759,631,8,true,true}, {663,535,4,true,true}, {695,567,4,true,true}
        };
        unsigned memory_case = 0;
        for (unsigned translated = 0; translated < 2; ++translated)
        for (const memory_pair &pair : pairs) {
            memset(ram, 0, 0x20000);
            mmu.reset(); mmu.set_msr(ppc32_mmu::MSR_FP | (translated ? ppc32_mmu::MSR_DR : 0));
            const uint32 logical = translated ? 0x20000000u : ram_base;
            if (translated) mmu.set_dbat(0, logical | 2u, ram_base | 2u);
            for (int i = 0; i < 32; ++i) { cpu->gpr(i) = 0; cpu->fpr_dw(i) = 0; memset(cpu->vr(i).w, 0, 16); }
            cpu->gpr(3) = 0x81234567u; cpu->gpr(4) = logical; cpu->gpr(6) = 8;
            cpu->fpr_dw(3) = bits(1.5); cpu->cr().set(0); cpu->xer().set(0); cpu->fpscr() = 0;
            cpu->pc() = 0x10000 + 64 * memory_case++; cpu->last_fetch_pa_ = cpu->pc(); cpu->lr() = 0; cpu->ctr() = 0;
            nw_jit_cpu shadow = {}; shadow.pc = cpu->pc(); shadow.dec = cpu->dec_; shadow.msr = mmu.msr();
            for (int i = 0; i < 32; ++i) { shadow.gpr[i] = cpu->gpr(i); shadow.fpr[i] = cpu->fpr_dw(i); }
            const uint32 store = pair.indexed ? (31u<<26)|(3u<<21)|(4u<<16)|(6u<<11)|(pair.store<<1)
                                              : (pair.store<<26)|(3u<<21)|(4u<<16)|8u;
            const uint32 load = pair.indexed ? (31u<<26)|(5u<<21)|(4u<<16)|(6u<<11)|(pair.load<<1)
                                             : (pair.load<<26)|(5u<<21)|(4u<<16)|8u;
            const uint32 ops[] = {store, load};
            nw_jit_fn fn = nw_jit_compile(ops, 2, cpu->pc(), cpu->pc() & ~0xfffu, translated ? 2 : 0, 0);
            CHECK(fn != NULL);
            const uint64 misses = nw_jit_verify_misses();
            if (fn) CHECK(cpu->nw_jit_verify_block(shadow, fn, ops, 2) == 1);
            CHECK(nw_jit_verify_misses() == misses);
            CHECK(shadow.pc == cpu->pc() && shadow.gpr[4] == cpu->gpr(4));
            CHECK(pair.fp ? shadow.fpr[5] == cpu->fpr_dw(5) : shadow.gpr[5] == cpu->gpr(5));
            CHECK(vm_read_memory_1(ram_base + 8) != 0); // expected actual reference store
        }
        // Successful prefix followed by a protected store must not execute the
        // suffix. Logical EA/width/direction are checked against the record.
        mmu.reset(); mmu.set_msr(ppc32_mmu::MSR_DR);
        mmu.set_dbat(0, 0x20000002u, ram_base | 1u); // read-only
        for (int i = 0; i < 32; ++i) { cpu->gpr(i) = 0; cpu->fpr_dw(i) = 0; }
        cpu->gpr(4) = 0x20000000; cpu->cr().set(0); cpu->xer().set(0); cpu->fpscr() = 0;
        cpu->pc() = 0x7400; cpu->last_fetch_pa_ = 0x7400;
        nw_jit_cpu shadow = {}; shadow.gpr[4] = cpu->gpr(4); shadow.pc = cpu->pc();
        shadow.msr = mmu.msr(); shadow.dec = cpu->dec_;
        uint32 ops[] = {nw_ppc_addi(3,0,77), nw_ppc_stw(3,4,8), nw_ppc_addi(5,0,99)};
        nw_jit_fn fn = nw_jit_compile(ops, 3, 0x7400, 0x7000, 2, 0); CHECK(fn != NULL);
        const uint64 misses = nw_jit_verify_misses();
        if (fn) CHECK(cpu->nw_jit_verify_block(shadow, fn, ops, 3) == 1);
        CHECK(nw_jit_verify_misses() == misses);
        CHECK(shadow.fault == NW_JIT_FAULT_DSI && shadow.pc == 0x7404 && shadow.gpr[3] == 77 && shadow.gpr[5] == 0);
        CHECK(cpu->dar_ == 0x20000008 && cpu->srr0_ == 0x7404);

        // Completed SMC instructions must commit every substore, but suppress
        // the next instruction. Test single and multiple stores independently.
        for (unsigned multiple = 0; multiple < 2; ++multiple) {
            mmu.reset(); mmu.set_msr(0);
            for (int i = 0; i < 32; ++i) { cpu->gpr(i) = 0; cpu->fpr_dw(i) = 0; }
            cpu->pc() = ram_base + 0x100 + multiple * 0x40;
            cpu->last_fetch_pa_ = cpu->pc(); cpu->gpr(4) = ram_base;
            cpu->gpr(30) = 0x11223344; cpu->gpr(31) = 0x55667788;
            cpu->cr().set(0); cpu->xer().set(0); cpu->fpscr() = 0;
            nw_jit_cpu shadow = {}; shadow.pc = cpu->pc(); shadow.dec = cpu->dec_;
            for (int i = 0; i < 32; ++i) shadow.gpr[i] = cpu->gpr(i);
            uint32 ops[] = {(multiple ? 47u : 36u)<<26 | (30u<<21) | (4u<<16) | 8u, nw_ppc_addi(5,0,99)};
            nw_jit_fn fn = nw_jit_compile(ops, 2, cpu->pc(), ram_base, 0, 0); CHECK(fn != NULL);
            const uint64 misses = nw_jit_verify_misses();
            if (fn) CHECK(cpu->nw_jit_verify_block(shadow, fn, ops, 2) == 1);
            CHECK(nw_jit_verify_misses() == misses);
            CHECK(shadow.fault == NW_JIT_FAULT_SMC && shadow.pc + 4 == cpu->pc() && shadow.gpr[5] == 0);
            CHECK(vm_read_memory_4(ram_base + 8) == 0x11223344u);
            if (multiple) CHECK(vm_read_memory_4(ram_base + 12) == 0x55667788u);
        }
        // Both engines retain the first successful access when the second
        // lmw/stmw word crosses into a BAT with no read/write permission.
        for (unsigned store = 0; store < 2; ++store) {
            mmu.reset(); mmu.set_msr(ppc32_mmu::MSR_DR);
            mmu.set_dbat(0, 0x20000002u, ram_base | 2u);
            mmu.set_dbat(1, 0x20020002u, (ram_base + 0x20000u));
            for (int i = 0; i < 32; ++i) { cpu->gpr(i) = 0; cpu->fpr_dw(i) = 0; }
            vm_write_memory_4(ram_base + 0x1fffc, 42);
            cpu->gpr(4) = 0x2001fffc; cpu->gpr(30) = 77; cpu->gpr(31) = 88;
            cpu->pc() = 0x7800 + store * 0x40; cpu->last_fetch_pa_ = cpu->pc();
            cpu->cr().set(0); cpu->xer().set(0); cpu->fpscr() = 0;
            nw_jit_cpu shadow = {}; shadow.pc = cpu->pc(); shadow.dec = cpu->dec_; shadow.msr = mmu.msr();
            for (int i = 0; i < 32; ++i) shadow.gpr[i] = cpu->gpr(i);
            uint32 ops[] = {((store ? 47u : 46u)<<26)|(30u<<21)|(4u<<16), nw_ppc_addi(5,0,99)};
            nw_jit_fn fn = nw_jit_compile(ops, 2, cpu->pc(), 0x7000, 2, 0); CHECK(fn != NULL);
            const uint64 misses = nw_jit_verify_misses();
            if (fn) CHECK(cpu->nw_jit_verify_block(shadow, fn, ops, 2) == 1);
            CHECK(nw_jit_verify_misses() == misses);
            CHECK(shadow.fault == NW_JIT_FAULT_DSI && shadow.gpr[30] == (store ? 77u : 42u) && shadow.gpr[31] == 88);
            CHECK(shadow.fault_ea == 0x20020000 && shadow.fault_width == 4 && shadow.fault_st == store);
            CHECK(vm_read_memory_4(ram_base + 0x1fffc) == (store ? 77u : 42u));
            CHECK(cpu->dar_ == 0x20020000 && shadow.pc == cpu->srr0_ && shadow.gpr[5] == 0);
        }

        // Host code publication previously cleared only legacy KPX blocks.
        // A changed instruction can stay in the native PPC cache indefinitely.
        for (unsigned translated = 0; translated < 2; ++translated) {
            mmu.reset();
            const uint32 logical = translated ? 0x20000000u : ram_base;
            const uint32 key = translated ? 3u : 0u;
            mmu.set_msr(translated ? ppc32_mmu::MSR_IR | ppc32_mmu::MSR_DR : 0);
            if (translated) { mmu.set_ibat(0, logical | 2u, ram_base | 2u); mmu.set_dbat(0, logical | 2u, ram_base | 2u); }
            uint32 ops[] = {nw_ppc_addi(3,0,1)};
            nw_jit_fn old = nw_jit_compile(ops, 1, logical + 0x500, ram_base, key, 0); CHECK(old != NULL);
            CHECK(nw_jit_cache_get(ram_base, logical + 0x500, key, 0, NULL) == old);
            vm_write_memory_4(ram_base + 0x500, nw_ppc_addi(3,0,2));
            cpu->invalidate_cache_range(logical + 0x500, logical + 0x504);
            CHECK(nw_jit_cache_get(ram_base, logical + 0x500, key, 0, NULL) == NULL);
            // Empty publication is a no-op, and adjacent pages keep their code.
            old = nw_jit_compile(ops, 1, logical + 0x1000, ram_base + 0x1000, key, 0);
            cpu->invalidate_cache_range(logical + 0x500, logical + 0x500);
            CHECK(nw_jit_cache_get(ram_base + 0x1000, logical + 0x1000, key, 0, NULL) == old);
            cpu->invalidate_cache_range(logical + 0x500, logical + 0x504);
            CHECK(nw_jit_cache_get(ram_base + 0x1000, logical + 0x1000, key, 0, NULL) == old);
        }
        munmap(ram, 0x20000);
    } else if (ram != MAP_FAILED) munmap(ram, 0x20000);

    // A stale native block may contain forbidden operations even when its
    // current reference instructions are safe. Helpers must reject replay
    // before touching live MMU generations, DTLBs or CPU/device callbacks.
    const uint32 unsafe[] = {
        (31u<<26)|(3u<<21)|(146u<<1), // mtmsr
        (31u<<26)|(3u<<21)|(2u<<16)|(210u<<1), // mtsr
        (31u<<26)|(3u<<21)|(306u<<1), // tlbie
        (31u<<26)|(370u<<1), // tlbia
        0x4c00012cu, // isync
        0x4c000064u, // rfi
        (31u<<26)|(3u<<21)|(982u<<1), // icbi
        (31u<<26)|(3u<<21)|(20u<<1), // lwarx
        (31u<<26)|(3u<<21)|(150u<<1)|1u, // stwcx.
        (31u<<26)|(3u<<21)|(4u<<16)|(1014u<<1), // dcbz
        (31u<<26)|(3u<<21)|(597u<<1), // lswi
        (31u<<26)|(3u<<21)|(725u<<1), // stswi
        (31u<<26)|(3u<<21)|(22u<<16)|(339u<<1), // mfspr DEC
        (31u<<26)|(3u<<21)|(22u<<16)|(467u<<1) // mtspr DEC
    };
    unsigned unsafe_case = 0;
    for (uint32 op : unsafe) {
        nw_jit_verify_trace trace;
        nw_jit_cpu shadow = {}; shadow.pc = 0x30000 + 64 * unsafe_case++;
        shadow.verify_mem = nw_jit_verify_trace::replay; shadow.verify_context = &trace;
        shadow.gpr[3] = 0x30; shadow.gpr[4] = 0x8000;
        uint32 ops[] = {op};
        nw_jit_fn fn = nw_jit_compile(ops, 1, shadow.pc, shadow.pc & ~0xfffu, 0, 0);
        CHECK(fn != NULL);
        nw_jit_dtlb_fill(0x8000, 0x9000, 1, 0, 0);
        uint32 pa = 0; CHECK(nw_jit_dtlb_lookup(0x8000, 0, &pa));
        const uint32 live_msr = mmu.msr(), live_sr = mmu.sr(2);
        if (fn) fn(&shadow);
        CHECK(shadow.fault == NW_JIT_FAULT_VERIFY);
        CHECK(nw_jit_dtlb_lookup(0x8000, 0, &pa) && pa == 0x9000);
        CHECK(mmu.msr() == live_msr && mmu.sr(2) == live_sr);
        CHECK(trace.count == 0 && trace.cursor == 0);
    }

    // Explicit integer-conversion bounds/NaNs, including the full chosen
    // KPX-compatible representation. No undefined host float-to-int cast.
    const uint64 conversion_inputs[] = {UINT64_C(0x7ff8000000000000), UINT64_C(0x7ff0000000000001),
        UINT64_C(0x7ff0000000000000), UINT64_C(0xfff0000000000000), bits(2147483648.0), bits(-2147483649.0)};
    unsigned conversion_case = 0;
    for (unsigned xo : {14u, 15u}) for (uint64 value : conversion_inputs) {
        for (int i = 0; i < 32; ++i) { cpu->gpr(i) = 0; cpu->fpr_dw(i) = 0; }
        cpu->fpr_dw(1) = value; cpu->pc() = 0x40000 + 64 * conversion_case++;
        cpu->cr().set(0); cpu->xer().set(0); cpu->fpscr() = 0; cpu->lr() = 0; cpu->ctr() = 0;
        mmu.reset(); mmu.set_msr(ppc32_mmu::MSR_FP);
        nw_jit_cpu shadow = {}; shadow.pc = cpu->pc(); shadow.dec = cpu->dec_; shadow.msr = mmu.msr();
        shadow.fpr[1] = value;
        uint32 ops[] = {(63u<<26)|(3u<<21)|(1u<<11)|(xo<<1)};
        nw_jit_fn fn = nw_jit_compile(ops, 1, cpu->pc(), cpu->pc() & ~0xfffu, 0, 0); CHECK(fn != NULL);
        const uint64 misses = nw_jit_verify_misses();
        if (fn) CHECK(cpu->nw_jit_verify_block(shadow, fn, ops, 1) == 1);
        const uint64 expected = (value == UINT64_C(0x7ff0000000000000) || value == bits(2147483648.0))
            ? UINT64_C(0x000000007fffffff) : UINT64_C(0xffffffff80000000);
        CHECK(shadow.fpr[3] == expected && cpu->fpr_dw(3) == expected);
        CHECK(nw_jit_verify_misses() == misses);
    }
    // Mutation checks: wrong address/value/order and missing stores must fail.
    for (unsigned mutation = 0; mutation < 5; ++mutation) {
        nw_jit_verify_trace trace;
        trace.record(0x1000, 0x8000, 4, true, 7);
        uint64 value = mutation == 0 ? 8 : 7;
        if (mutation != 4)
            nw_jit_verify_trace::replay(&trace, mutation == 1 ? 0x1004 : 0x1000,
                mutation == 2 ? 0x8004 : 0x8000, 4, mutation != 3, &value);
        CHECK(!trace.complete());
    }
    // Every supported narrow/FP memory helper consumes private observations,
    // including faulted loads with poisoned outputs and logical PA aliases.
    for (unsigned width : {1u, 2u, 4u, 8u}) {
        nw_jit_verify_trace trace;
        trace.record(0x1000, 0x8000, width, false, 0x3ff0000000000000ull);
        trace.record(0x1000, 0x8000, width, true, width == 8 ? 0x3ff0000000000000ull : 0);
        nw_jit_cpu shadow = {}; shadow.pc = 0x1000;
        shadow.verify_context = &trace; shadow.verify_mem = nw_jit_verify_trace::replay;
        if (width == 1) { CHECK(nw_jit_helper_lb(&shadow, 0x8000) == 0); nw_jit_helper_stb(&shadow, 0x8000, 0); }
        if (width == 2) { CHECK(nw_jit_helper_lh(&shadow, 0x8000) == 0); nw_jit_helper_sth(&shadow, 0x8000, 0); }
        if (width == 4) { CHECK(nw_jit_helper_lwz_pa(&shadow, 0xdead0000, 0x8000) == 0); nw_jit_helper_stw_pa(&shadow, 0xdead0000, 0, 0x8000); }
        if (width == 8) { nw_jit_helper_lfd(&shadow, 3, 0, 0x8000); nw_jit_helper_stfd(&shadow, 3, 0, 0x8000); }
        CHECK(trace.complete() && !shadow.fault);
    }
    CHECK(!nw_jit_op_verify_safe(0x4c000064u)); // rfi
    CHECK(!nw_jit_op_verify_safe((31u<<26)|(20u<<1))); // lwarx
    CHECK(!nw_jit_op_verify_safe(4u<<26)); // delegated VMX
    nw_jit_set_mode(NW_JIT_ON);
    mmu.reset();
    // Re-enabling EE via rfi exposes the retained DEC request immediately.
    cpu->dec_pending_ = true; cpu->dec_ = 1000; cpu->dec_tb_base_ = cpu->tb_host_ticks();
    cpu->srr0_ = 0x3000; cpu->srr1_ = ppc32_mmu::MSR_EE; mmu.set_msr(0);
    ppc_core_test_access::instruction(cpu, 0x4c000064u);
    CHECK(cpu->pc() == 0x3000 && cpu->jit_events_pending());
    cpu->take_async_exception(); CHECK(cpu->pc() == 0x900 && !cpu->dec_pending_);
    fesetenv(&caller);
    delete cpu;
    printf("PPC core tests: %u passed, %u failed\n", passed, failed);
    return failed ? 1 : 0;
}

/* Captures have a versioned textual format and no process addresses. Replays
 * reproduce a discrepancy without booting a VM or invoking a live callback. */
static int replay_capture(const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file) { perror(path); return 2; }
    char line[512];
    if (!fgets(line, sizeof line, file) || strcmp(line, "NW-PPC-VERIFY 1\n")) { fclose(file); return 2; }
    nw_jit_cpu input = {}, expected = {};
    nw_jit_verify_trace trace;
    uint32 ops[NW_JIT_MAX_BLOCK] = {};
    unsigned n = 0, declared = 0, bits = 0, index, word;
    unsigned long long a, b, c;
    while (fgets(line, sizeof line, file)) {
        if (sscanf(line, "pc %x bits %x n %u", &input.pc, &bits, &declared) == 3) continue;
        if (!strncmp(line, "op ", 3)) {
            if (n == NW_JIT_MAX_BLOCK || sscanf(line, "op %x", &ops[n]) != 1 || !nw_jit_op_verify_safe(ops[n])) { fclose(file); return 2; }
            ++n; continue;
        }
        if (sscanf(line, "fault %u %x %u %u", &expected.fault, &expected.fault_ea, &expected.fault_width, &expected.fault_st) == 4) continue;
        if (sscanf(line, "input %x %x %x %x %x %x %x %x", &input.cr, &input.xer, &input.fpscr, &input.lr, &input.ctr, &input.dec, &input.msr, &input.vscr) == 8) continue;
        if (sscanf(line, "reference %x %x %x %x %x %x %x %x pc %x", &expected.cr, &expected.xer, &expected.fpscr, &expected.lr, &expected.ctr, &expected.dec, &expected.msr, &expected.vscr, &expected.pc) == 9) continue;
        unsigned gi, gr, gj;
        if (sscanf(line, "gpr %u %x %x %x", &index, &gi, &gr, &gj) == 4) {
            if (index >= 32) { fclose(file); return 2; }
            input.gpr[index] = gi; expected.gpr[index] = gr; continue;
        }
        if (sscanf(line, "fpr %u %llx %llx %llx", &index, &a, &b, &c) == 4) {
            if (index >= 32) { fclose(file); return 2; }
            input.fpr[index] = a; expected.fpr[index] = b; continue;
        }
        if (sscanf(line, "vr %u %u %x %x %x", &index, &word, &gi, &gr, &gj) == 5) {
            if (index >= 32 || word >= 4) { fclose(file); return 2; }
            input.vr[index][word] = gi; expected.vr[index][word] = gr; continue;
        }
        unsigned pc, ea, width, store, fault;
        if (sscanf(line, "access %x %x %u %u %u %llx", &pc, &ea, &width, &store, &fault, &a) == 6) {
            if ((width != 1 && width != 2 && width != 4 && width != 8) || store > 1 || trace.count == trace.capacity) { fclose(file); return 2; }
            trace.record(pc, ea, width, store != 0, a, fault);
        }
    }
    fclose(file);
    if (!n || n != declared) return 2;
    nw_jit_set_mode(NW_JIT_VERIFY);
    nw_jit_fn fn = nw_jit_compile(ops, n, input.pc, input.pc & ~0xfffu,
        ((input.msr & ppc32_mmu::MSR_IR) ? 1u : 0u) | ((input.msr & ppc32_mmu::MSR_DR) ? 2u : 0u) | ((input.msr & ppc32_mmu::MSR_PR) ? 4u : 0u), 0);
    if (!fn) return 2;
    input.verify_mem = nw_jit_verify_trace::replay; input.verify_context = &trace;
    nw_jit_cpu_bind(&input); nw_jit_tail_begin(); fn(&input);
    CHECK(trace.complete());
    CHECK((input.fault == NW_JIT_FAULT_SMC ? input.pc + 4 : input.pc) == expected.pc);
    CHECK(input.cr == expected.cr); CHECK(input.xer == expected.xer);
    CHECK(input.fpscr == expected.fpscr); CHECK(input.lr == expected.lr);
    CHECK(input.ctr == expected.ctr); CHECK(input.dec == expected.dec);
    CHECK(input.msr == expected.msr); CHECK(input.vscr == expected.vscr);
    CHECK(input.fault == expected.fault);
    if (expected.fault) { CHECK(input.fault_ea == expected.fault_ea); CHECK(input.fault_width == expected.fault_width); CHECK(input.fault_st == expected.fault_st); }
    for (unsigned i = 0; i < 32; ++i) {
        CHECK(input.gpr[i] == expected.gpr[i]); CHECK(input.fpr[i] == expected.fpr[i]);
        for (unsigned w = 0; w < 4; ++w) CHECK(input.vr[i][w] == expected.vr[i][w]);
    }
    printf("PPC isolated replay: %u passed, %u failed (original bits=%x)\n", passed, failed, bits);
    return failed ? 1 : 0;
}

extern "C" int ppc_test_main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "--replay")) return replay_capture(argv[2]);
    return ppc_core_test_access::run();
}
