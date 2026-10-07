/* Tests execute the real KPX decoder and instruction methods. Linked against
 * an isolated app build, with a private entry point and deterministic TB clock;
 * no GUI, ROM, disk or guest device callback is initialized. */
#include <functional>
#include "sysdeps.h"
#include "cpu/ppc/ppc-cpu.hpp"
#include "nw_jit.h"
#include "nw_jit_verify.h"
#include "nw_68k_core.h"
#include "nw_io.h"
#include "nw_boot_contract.h"
#include "sys.h"
#include <unistd.h>
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
extern void nw_jit_helper_lvx(nw_jit_cpu *, uint32_t, uint32_t, uint32_t);
extern void nw_jit_helper_stvx(nw_jit_cpu *, uint32_t, uint32_t, uint32_t);
extern void nw_jit_helper_vmx(nw_jit_cpu *, uint32_t);
static unsigned delegated_vmx_calls;
static void forbidden_vmx_callback(void *, uint32_t, nw_jit_cpu *) { ++delegated_vmx_calls; }
static unsigned passed, failed;
#define CHECK(c) do { if (c) ++passed; else { ++failed; fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); } } while (0)
static int fp_callback_round, fp_callback_flags;
static uint64 fake_us;
uint64 ppc_test_ticks_usec() { return fake_us; }

struct verify_device { unsigned reads = 0, writes = 0; uint32 value = 10; void (*on_write)(void *) = NULL; void *on_write_context = NULL; };
static uint32 device_read(void *context, uint32, int) {
    verify_device &d = *static_cast<verify_device *>(context);
    ++d.reads; return d.value++;
}
static void device_write(void *context, uint32, int, uint32 value) {
    verify_device &d = *static_cast<verify_device *>(context);
    ++d.writes; d.value = value; if (d.on_write) d.on_write(d.on_write_context);
}

struct ppc_core_test_access {
    static void checked_fp_exception(void *host, nw_jit_cpu *c) {
        CHECK(fegetround() == fp_callback_round);
        CHECK(fetestexcept(FE_ALL_EXCEPT) == fp_callback_flags);
        powerpc_cpu::jit_host_fp_exception(host,c);
    }
    static void *stale_vector_tail(void *host, nw_jit_cpu *c, uint32 pc, int *n,
        int *f, int *v, uint32 *dsi, uint32 *chain, int cf, int cv, nw_jit_chain_info *info) {
        powerpc_cpu *ppc = static_cast<powerpc_cpu *>(host);
        uint32 stale[32][4];
        for (unsigned r = 0; r < 32; ++r) for (unsigned w = 0; w < 4; ++w) stale[r][w] = ppc->vr(r).w[w];
        void *next = powerpc_cpu::jit_host_chain(host,c,pc,n,f,v,dsi,chain,cf,cv,info);
        if (next && *v && cv) memcpy(c->vr,stale,sizeof stale);
        return next;
    }
    static int run();
    static int scalar_p6(powerpc_cpu *);
    static int frsp_p6(powerpc_cpu *);
    static int basic_special_p6(powerpc_cpu *);
    static int multiply_p6(powerpc_cpu *);
    static int fp_fast_sweep(powerpc_cpu *);
    static int sub_store_sweep(powerpc_cpu *);
    static int loop_bench(powerpc_cpu *);
    static int ibtc_remap(powerpc_cpu *);
    static int link_sweep(powerpc_cpu *);
    static int io_publication(powerpc_cpu *);
    static void stop_on_device(void *context) { static_cast<powerpc_cpu *>(context)->spcflags().set(SPCFLAG_CPU_EXEC_RETURN); }
    static void instruction(powerpc_cpu *cpu, uint32 op) { cpu->decode(op)->execute(cpu, op); }
};
static uint64 bits(double d) { uint64 b; memcpy(&b, &d, 8); return b; }
int ppc_core_test_access::io_publication(powerpc_cpu *cpu)
{
    cpu->enable_guest_mmu(true);
    nw_jit_set_mode(NW_JIT_VERIFY); // initialize the optional inline check before emitting code
    ppc32_mmu &mmu = ppc32_guest_mmu();
    const uint32 base = 0x10000000u;
    void *const wanted = (void *)(VMBaseDiff + base);
    void *const ram = mmap(wanted,0x4000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    CHECK(ram == wanted); if (ram != wanted) return 1;
    nw_banks_set(NW_PA_RAM,base,0x4000);
    char path[] = "/tmp/macemu-ppc-code-read-XXXXXX";
    const int fd = mkstemp(path); CHECK(fd >= 0);
    uint8 payload[512]; memset(payload,0xa5,sizeof payload);
    payload[0]=0x48; payload[1]=0; payload[2]=4; payload[3]=1; // bl +0x400
    if (fd >= 0) { CHECK(write(fd,payload,sizeof payload) == sizeof payload); close(fd); }
    void *file = fd >= 0 ? Sys_open(path,true) : NULL; CHECK(file != NULL);
    unsigned cases = 0;
    if (file) {
        const uint32 nop = 0x60000000u;
        for (unsigned verify : {0u,1u})
        for (unsigned crossing : {0u,1u})
        for (unsigned short_read : {0u,1u}) {
            ++cases; nw_jit_invalidate_all(); nw_jit_itlb_flush(); mmu.reset();
            const uint32 start = base + (crossing ? 0xffc : 0x200);
            const uint32 second = base+0x1100, untouched = base+0x3000;
            memset(ram,0x5a,0x4000);
            vm_write_memory_4(start,nop);
            vm_write_memory_4(second,nop); vm_write_memory_4(untouched,nop);
            nw_jit_fn old = nw_jit_compile(&nop,1,start,start&~0xfffu,0,0);
            nw_jit_fn next = nw_jit_compile(&nop,1,second,second&~0xfffu,0,0);
            nw_jit_fn other = nw_jit_compile(&nop,1,untouched,untouched&~0xfffu,0,0);
            CHECK(old && next && other);
            CHECK(Sys_read(file,vm_do_get_real_address(start),0,short_read ? 1024 : 512) == 512);
            CHECK(!memcmp(vm_do_get_real_address(start),payload,512));
            CHECK(vm_read_memory_1(start-1) == 0x5a && vm_read_memory_1(start+512) == 0x5a);
            const bool published = nw_jit_cache_get(start&~0xfffu,start,0,0,NULL) == NULL;
            CHECK(published);
            CHECK(nw_jit_cache_get(second&~0xfffu,second,0,0,NULL) == (crossing ? NULL : next));
            CHECK(nw_jit_cache_get(untouched&~0xfffu,untouched,0,0,NULL) == other);
            cpu->pc()=start; cpu->last_fetch_pa_=start; cpu->cr().set(0x12345678); cpu->xer().set(0xe0000000);
            cpu->lr()=cpu->ctr()=0; cpu->spcflags().init(); cpu->dec_=1000000;
            cpu->dec_tb_base_=cpu->tb_host_ticks(); cpu->dec_pending_=false;
            const uint64 misses=nw_jit_verify_misses();
            nw_jit_set_host_chain(NULL); nw_jit_set_mode(verify ? NW_JIT_VERIFY : NW_JIT_ON);
            if (published) {
                CHECK(cpu->nw_jit_try(0x48000401u) == 1);
                CHECK(cpu->pc() == start+0x400 && cpu->lr() == start+4);
                CHECK(cpu->cr().get() == 0x12345678 && cpu->xer().get() == 0xe0000000 && !cpu->ctr());
                CHECK(nw_jit_verify_misses() == misses);
            }
            // EOF, invalid handles and non-guest output buffers do not publish
            // a guest write or invalidate unrelated cached code.
            uint8 host[512];
            CHECK(Sys_read(file,vm_do_get_real_address(untouched),512,512) == 0);
            CHECK(Sys_read(NULL,vm_do_get_real_address(untouched),0,512) == 0);
            CHECK(Sys_read(file,host,0,512) == 512 && !memcmp(host,payload,512));
            CHECK(nw_jit_cache_get(untouched&~0xfffu,untouched,0,0,NULL) == other);
        }
        Sys_close(file);
    }
    unlink(path); nw_jit_invalidate_all(); nw_banks_set(NW_PA_RAM,0,0); munmap(ram,0x4000);
    nw_jit_set_host_chain(powerpc_cpu::jit_host_chain); nw_jit_set_mode(NW_JIT_ON);
    printf("Raw file-read publication: %u full/short/page-boundary ON/VERIFY cases\n",cases);
    return failed ? 1 : 0;
}
int ppc_core_test_access::scalar_p6(powerpc_cpu *cpu)
{
    struct conversion { double input; int64 value[4]; unsigned flags[4]; }; // flags: FI=1, FR=2, invalid=4
    const conversion inputs[] = {
        {0.0,{0,0,0,0},{0,0,0,0}}, {-0.0,{0,0,0,0},{0,0,0,0}},
        {0.5,{0,0,1,0},{1,1,3,1}}, {-0.5,{0,0,0,-1},{1,1,1,3}},
        {1.5,{2,1,2,1},{3,1,3,1}}, {-1.5,{-2,-1,-1,-2},{3,1,1,3}},
        {2.5,{2,2,3,2},{1,1,3,1}}, {-2.5,{-2,-2,-2,-3},{1,1,1,3}},
        {1.25,{1,1,2,1},{1,1,3,1}}, {-1.25,{-1,-1,-1,-2},{1,1,1,3}},
        {2147483647.0,{2147483647,2147483647,2147483647,2147483647},{0,0,0,0}},
        {2147483647.25,{2147483647,2147483647,2147483647,2147483647},{1,1,4,1}},
        {2147483647.5,{2147483647,2147483647,2147483647,2147483647},{4,1,4,1}},
        {2147483648.0,{2147483647,2147483647,2147483647,2147483647},{4,4,4,4}},
        {-2147483648.0,{-2147483648LL,-2147483648LL,-2147483648LL,-2147483648LL},{0,0,0,0}},
        {-2147483648.25,{-2147483648LL,-2147483648LL,-2147483648LL,-2147483648LL},{1,1,1,4}},
        {-2147483648.5,{-2147483648LL,-2147483648LL,-2147483648LL,-2147483648LL},{1,1,1,4}},
        {-2147483649.0,{-2147483648LL,-2147483648LL,-2147483648LL,-2147483648LL},{4,4,4,4}},
        {0x1p-1074,{0,0,1,0},{1,1,3,1}}, {-0x1p-1074,{0,0,0,-1},{1,1,1,3}}
    };
    cpu->enable_guest_mmu(true);
    nw_jit_set_host_fp_exception(checked_fp_exception);
    ppc32_mmu &mmu = ppc32_guest_mmu();
    fenv_t environment; fegetenv(&environment);
    unsigned cases = 0;
    nw_jit_set_host_chain(NULL); nw_jit_set_mode(NW_JIT_VERIFY);
    const uint32 prefix = (63u<<26)|(7u<<21)|(4u<<11)|(40u<<1);
    const uint32 suffix = (63u<<26)|(5u<<21)|(6u<<11)|(72u<<1);
    for (unsigned convert : {0u,1u})
    for (unsigned rc : {0u,1u})
    for (unsigned alias : {0u,1u}) {
        const unsigned fd = alias ? 1 : 3;
        const uint32 op = (63u<<26)|(fd<<21)|(1u<<11)|((convert ? 15u : 14u)<<1)|rc;
        uint32 ops[] = {prefix,op,suffix}; const uint32 pc = 0x16000;
        nw_jit_invalidate_all();
        nw_jit_fn fn = nw_jit_compile(ops,3,pc,pc,0,0); CHECK(fn != NULL); if (!fn) continue;
        for (unsigned ci = 0; ci < sizeof inputs/sizeof inputs[0] + 6; ++ci)
        for (unsigned rn = 0; rn < 4; ++rn)
        for (unsigned sticky : {0u,0x03000100u,0x83000100u})
        for (unsigned enable : {0u,0x80u,0x8u,0x88u})
        for (unsigned fe : {0u,0x100u,0x800u,0x900u})
        for (unsigned ip : {0u,0x40u}) {
            const unsigned rounding = convert ? 1 : rn;
            uint64 source; int64 word; unsigned status; bool snan = false;
            if (ci < sizeof inputs/sizeof inputs[0]) {
                source = bits(inputs[ci].input); word = inputs[ci].value[rounding]; status = inputs[ci].flags[rounding];
            } else {
                const uint64 special[] = {0x7ff0000000000000ULL,0xfff0000000000000ULL,0x7ff8123456789abcULL,0xfff8123456789abcULL,0x7ff0123456789abcULL,0xfff0123456789abcULL};
                unsigned si = ci - sizeof inputs/sizeof inputs[0]; source = special[si];
                word = si == 0 ? 2147483647 : -2147483648LL; status = 4; snan = si >= 4;
            }
            const uint32 causes = status & 4 ? 0x100u | (snan ? 0x01000000u : 0) : status & 1 ? 0x02000000u : 0;
            const uint32 initial_fpscr = sticky | enable | rn | 0x75000u;
            uint32 result_fpscr = (initial_fpscr & ~0x60000u) | causes;
            if (causes & ~initial_fpscr) result_fpscr |= 0x80000000u;
            if (status & 1) result_fpscr |= 0x20000u;
            if (status & 2) result_fpscr |= 0x40000u;
            result_fpscr &= ~0x60000000u;
            if (result_fpscr & 0x01000100u) result_fpscr |= 0x20000000u;
            if ((result_fpscr & 0x20000000u) && (enable & 0x80u) || (result_fpscr & 0x02000000u) && (enable & 8u)) result_fpscr |= 0x40000000u;
            const bool except = fe && (result_fpscr & 0x40000000u);
            const bool suppressed = (status & 4) && (enable & 0x80u);
            for (unsigned engine = 0; engine < 4; ++engine) {
                ++cases; const unsigned failures = failed;
                nw_jit_cpu before = {}; before.pc = pc; before.msr = 0x2000u | fe | ip;
                before.fpscr = initial_fpscr; before.cr = 0xb2345678u; before.dec = cpu->dec_;
                for (unsigned r = 0; r < 32; ++r) before.fpr[r] = bits(double(r + 32));
                before.fpr[1] = source;
                cpu->pc() = pc; cpu->last_fetch_pa_ = pc; mmu.set_msr(before.msr);
                cpu->fpscr() = before.fpscr; cpu->cr().set(before.cr); cpu->xer().set(0); cpu->lr() = cpu->ctr() = 0;
                cpu->srr0_ = cpu->srr1_ = 0; cpu->spcflags().init(); cpu->regs().reserve_valid = 0;
                for (unsigned r = 0; r < 32; ++r) { cpu->fpr_dw(r) = before.fpr[r]; cpu->gpr(r) = 0; }
                nw_jit_cpu result = before;
                fesetround(engine & 1 ? FE_UPWARD : FE_DOWNWARD); feclearexcept(FE_ALL_EXCEPT); feraiseexcept(FE_DIVBYZERO);
                const int host_flags = fetestexcept(FE_ALL_EXCEPT), host_round = fegetround();
                fp_callback_round = host_round; fp_callback_flags = host_flags;
                if (engine == 0) {
                    const uint64 serial = cpu->exception_serial_;
                    for (uint32 instruction_op : ops) { instruction(cpu,instruction_op); if (cpu->exception_serial_ != serial) break; }
                } else if (engine == 1) (void)nw_jit_interp_n(&result,ops,3,pc);
                else if (engine == 2) { result.host = cpu; nw_jit_cpu_bind(&result); nw_jit_tail_begin(); fn(&result); }
                else { const uint64 misses = nw_jit_verify_misses(); CHECK(cpu->nw_jit_verify_block(result,fn,ops,3) == 1); CHECK(nw_jit_verify_misses() == misses); }
                if (engine == 0 || engine == 3) {
                    result.pc = cpu->pc(); result.fpscr = cpu->fpscr(); result.cr = cpu->cr().get(); result.msr = mmu.msr(); result.srr0 = cpu->srr0_; result.srr1 = cpu->srr1_;
                    for (unsigned r = 0; r < 32; ++r) result.fpr[r] = cpu->fpr_dw(r);
                }
                CHECK(result.fpscr == result_fpscr);
                CHECK(result.cr == (rc ? (before.cr & ~0x0f000000u) | ((result_fpscr >> 4) & 0x0f000000u) : before.cr));
                CHECK(result.pc == (except ? (ip ? 0xfff00700u : 0x700u) : pc + 12));
                CHECK(result.msr == (except ? before.msr & ~0x0204ef32u : before.msr));
                if (except) { CHECK(result.srr0 == pc + 4); CHECK(result.srr1 == ((before.msr & ~0x783f0000u) | 0x100000u)); if (engine == 2) { CHECK(cpu->pc() == result.pc); CHECK(cpu->srr0_ == result.srr0 && cpu->srr1_ == result.srr1); CHECK(cpu->fpscr() == result.fpscr); } }
                for (unsigned r = 0; r < 32; ++r) {
                    const uint64 expected = r == 7 ? before.fpr[4] ^ 0x8000000000000000ULL :
                        r == fd ? suppressed ? before.fpr[fd] : uint64(word) : r == 5 && !except ? before.fpr[6] : before.fpr[r];
                    CHECK(result.fpr[r] == expected);
                }
                CHECK(fegetround() == host_round && fetestexcept(FE_ALL_EXCEPT) == host_flags);
                if (failed != failures && failed < 50) fprintf(stderr,"P6 conversion case=%u engine=%u ci=%u rn=%u rc=%u alias=%u sticky=%08x en=%x fe=%x fpscr=%08x expected=%08x\n",cases,engine,ci,rn,rc,alias,sticky,enable,fe,result.fpscr,result_fpscr);
            }
        }
    }
    const unsigned conversion_cases = cases;
    struct compare_case { uint64 a,b; uint32 cc; bool snan,nan; };
    const compare_case pairs[] = {
        {0,0x8000000000000000ULL,2,false,false},
        {0x8000000000000000ULL,0,2,false,false},
        {0x3ff0000000000000ULL,0x4000000000000000ULL,8,false,false},
        {0x4000000000000000ULL,0x3ff0000000000000ULL,4,false,false},
        {0x7ff0000000000000ULL,0xfff0000000000000ULL,4,false,false},
        {0x0000000000000001ULL,0,4,false,false},
        {0x7ff8123456789abcULL,0x3ff0000000000000ULL,1,false,true},
        {0x3ff0000000000000ULL,0xfff8123456789abcULL,1,false,true},
        {0x7ff0123456789abcULL,0x3ff0000000000000ULL,1,true,true},
        {0x3ff0000000000000ULL,0xfff0123456789abcULL,1,true,true},
        {0x7ff8123456789abcULL,0xfff0123456789abcULL,1,true,true},
        {0x7ff0123456789abcULL,0xfff8123456789abcULL,1,true,true}
    };
    for (unsigned ordered : {0u,1u})
    for (unsigned crfd = 0; crfd < 8; ++crfd) {
        const uint32 op = (63u<<26)|(crfd<<23)|(1u<<16)|(2u<<11)|(ordered ? 64u : 0u);
        uint32 ops[] = {prefix,op,suffix}; const uint32 pc = 0x17000;
        nw_jit_invalidate_all();
        nw_jit_fn fn = nw_jit_compile(ops,3,pc,pc,0,0); CHECK(fn != NULL); if (!fn) continue;
        for (const compare_case &pair : pairs)
        for (unsigned sticky : {0u,0x01080000u,0x81080000u})
        for (unsigned enable : {0u,0x80u})
        for (unsigned fe : {0u,0x100u,0x800u,0x900u})
        for (unsigned ip : {0u,0x40u})
        for (unsigned engine = 0; engine < 4; ++engine) {
            ++cases; const unsigned failures = failed;
            const uint32 causes = pair.snan ? 0x01000000u | (ordered && !enable ? 0x80000u : 0u) : ordered && pair.nan ? 0x80000u : 0u;
            nw_jit_cpu before = {}; before.pc = pc; before.msr = 0x2000u | fe | ip;
            before.fpscr = sticky | enable | 0x72003u; before.cr = 0xb2345678u; before.dec = cpu->dec_;
            uint32 expected_fpscr = (before.fpscr & ~0xf000u) | (pair.cc<<12) | causes;
            if (causes & ~before.fpscr) expected_fpscr |= 0x80000000u;
            expected_fpscr &= ~0x60000000u;
            if (expected_fpscr & 0x01080000u) expected_fpscr |= 0x20000000u;
            if ((expected_fpscr & 0x20000000u) && enable) expected_fpscr |= 0x40000000u;
            const unsigned sh = 28 - 4 * crfd;
            const uint32 expected_cr = (before.cr & ~(15u<<sh)) | (pair.cc<<sh);
            const bool except = fe && (expected_fpscr & 0x40000000u);
            for (unsigned r = 0; r < 32; ++r) before.fpr[r] = bits(double(r+32));
            before.fpr[1] = pair.a; before.fpr[2] = pair.b;
            cpu->pc() = pc; cpu->last_fetch_pa_ = pc; mmu.set_msr(before.msr);
            cpu->fpscr() = before.fpscr; cpu->cr().set(before.cr); cpu->xer().set(0); cpu->lr() = cpu->ctr() = 0;
            cpu->srr0_ = cpu->srr1_ = 0; cpu->spcflags().init(); cpu->regs().reserve_valid = 0;
            for (unsigned r = 0; r < 32; ++r) { cpu->fpr_dw(r) = before.fpr[r]; cpu->gpr(r) = 0; }
            nw_jit_cpu result = before;
            fesetround(engine & 1 ? FE_TOWARDZERO : FE_TONEAREST); feclearexcept(FE_ALL_EXCEPT); feraiseexcept(FE_DIVBYZERO);
            const int host_flags = fetestexcept(FE_ALL_EXCEPT), host_round = fegetround();
            fp_callback_round = host_round; fp_callback_flags = host_flags;
            if (engine == 0) { const uint64 serial = cpu->exception_serial_; for (uint32 instruction_op : ops) { instruction(cpu,instruction_op); if (cpu->exception_serial_ != serial) break; } }
            else if (engine == 1) (void)nw_jit_interp_n(&result,ops,3,pc);
            else if (engine == 2) { result.host = cpu; nw_jit_cpu_bind(&result); nw_jit_tail_begin(); fn(&result); }
            else { const uint64 misses = nw_jit_verify_misses(); CHECK(cpu->nw_jit_verify_block(result,fn,ops,3) == 1); CHECK(nw_jit_verify_misses() == misses); }
            if (engine == 0 || engine == 3) {
                result.pc = cpu->pc(); result.fpscr = cpu->fpscr(); result.cr = cpu->cr().get(); result.msr = mmu.msr(); result.srr0 = cpu->srr0_; result.srr1 = cpu->srr1_;
                for (unsigned r = 0; r < 32; ++r) result.fpr[r] = cpu->fpr_dw(r);
            }
            CHECK(result.fpscr == expected_fpscr); CHECK(result.cr == expected_cr);
            CHECK(result.pc == (except ? (ip ? 0xfff00700u : 0x700u) : pc + 12));
            CHECK(result.msr == (except ? before.msr & ~0x0204ef32u : before.msr));
            if (except) { CHECK(result.srr0 == pc + 4); CHECK(result.srr1 == ((before.msr & ~0x783f0000u) | 0x100000u)); if (engine == 2) { CHECK(cpu->pc() == result.pc); CHECK(cpu->srr0_ == result.srr0 && cpu->srr1_ == result.srr1); CHECK(cpu->fpscr() == result.fpscr); } }
            for (unsigned r = 0; r < 32; ++r) {
                const uint64 expected = r == 7 ? before.fpr[4] ^ 0x8000000000000000ULL : r == 5 && !except ? before.fpr[6] : before.fpr[r];
                CHECK(result.fpr[r] == expected);
            }
            CHECK(fegetround() == host_round && fetestexcept(FE_ALL_EXCEPT) == host_flags);
            if (failed != failures && failed < 50) fprintf(stderr,"P6 compare ordered=%u cr=%u engine=%u en=%x fe=%x fpscr=%08x expected=%08x\n",ordered,crfd,engine,enable,fe,result.fpscr,expected_fpscr);
        }
    }
    const unsigned compare_cases = cases - conversion_cases;
    const uint64 move_values[] = {0x7ff0123456789abcULL,0xfff0123456789abcULL,0x7ff8123456789abcULL,0xfff8123456789abcULL,0,0x8000000000000000ULL,0x7ff0000000000000ULL,0xfff0000000000000ULL,1,0x8000000000000001ULL};
    for (unsigned kind = 0; kind < 5; ++kind)
    for (unsigned alias = 0; alias < (kind == 4 ? 5u : 2u); ++alias) {
        const unsigned fd = alias == 1 ? 1 : alias == 2 ? 2 : alias == 3 ? 3 : 4;
        const unsigned ra = 1, rb = alias == 4 ? 1 : 2, fc = alias == 4 ? 1 : 3;
        const unsigned real_fd = kind != 4 && alias == 1 ? rb : alias == 4 ? 1 : fd;
        for (unsigned rc : {0u,1u}) {
            const unsigned xo[] = {72,40,264,136,23};
            const uint32 op = (63u<<26)|(real_fd<<21)|((kind == 4 ? ra : 0u)<<16)|(rb<<11)|((kind == 4 ? fc : 0u)<<6)|(xo[kind]<<1)|rc;
            const uint32 pc = 0x18000; nw_jit_invalidate_all();
            nw_jit_fn fn = nw_jit_compile(&op,1,pc,pc,0,0); CHECK(fn != NULL); if (!fn) continue;
            for (uint64 value : move_values)
            for (unsigned engine = 0; engine < 4; ++engine) {
                ++cases; nw_jit_cpu before = {}; before.pc = pc; before.msr = 0x2900u;
                before.cr = 0x12345678u; before.fpscr = 0xe10750f3u; before.dec = cpu->dec_;
                for (unsigned r = 0; r < 32; ++r) before.fpr[r] = bits(double(r+32));
                before.fpr[ra] = value; before.fpr[rb] = alias == 4 ? value : 0xfff0123456789abcULL;
                before.fpr[fc] = alias == 4 ? value : 0x7ff8abcdef123456ULL;
                if (kind != 4) before.fpr[rb] = value;
                const uint64 operand = before.fpr[rb], abs = before.fpr[ra] & 0x7fffffffffffffffULL;
                const bool positive = abs <= 0x7ff0000000000000ULL && (!(before.fpr[ra]>>63) || abs == 0);
                const uint64 expected = kind == 0 ? operand : kind == 1 ? operand ^ 0x8000000000000000ULL : kind == 2 ? operand & 0x7fffffffffffffffULL : kind == 3 ? operand | 0x8000000000000000ULL : positive ? before.fpr[fc] : before.fpr[rb];
                cpu->pc() = pc; cpu->last_fetch_pa_ = pc; mmu.set_msr(before.msr);
                cpu->fpscr() = before.fpscr; cpu->cr().set(before.cr); cpu->xer().set(0); cpu->lr() = cpu->ctr() = 0;
                cpu->srr0_ = cpu->srr1_ = 0; cpu->spcflags().init(); cpu->regs().reserve_valid = 0;
                for (unsigned r = 0; r < 32; ++r) { cpu->fpr_dw(r) = before.fpr[r]; cpu->gpr(r) = 0; }
                nw_jit_cpu result = before;
                fesetround(engine & 1 ? FE_UPWARD : FE_DOWNWARD); feclearexcept(FE_ALL_EXCEPT); feraiseexcept(FE_DIVBYZERO);
                const int host_flags = fetestexcept(FE_ALL_EXCEPT), host_round = fegetround();
                if (engine == 0) instruction(cpu,op);
                else if (engine == 1) (void)nw_jit_interp_n(&result,&op,1,pc);
                else if (engine == 2) { result.host = cpu; nw_jit_cpu_bind(&result); nw_jit_tail_begin(); fn(&result); }
                else { const uint64 misses = nw_jit_verify_misses(); CHECK(cpu->nw_jit_verify_block(result,fn,&op,1) == 1); CHECK(nw_jit_verify_misses() == misses); }
                if (engine == 0 || engine == 3) { result.pc = cpu->pc(); result.fpscr = cpu->fpscr(); result.cr = cpu->cr().get(); for (unsigned r = 0; r < 32; ++r) result.fpr[r] = cpu->fpr_dw(r); }
                CHECK(result.pc == pc + 4); CHECK(!result.fault); CHECK(result.fpscr == before.fpscr);
                CHECK(result.cr == (rc ? (before.cr & ~0x0f000000u) | ((before.fpscr >> 4) & 0x0f000000u) : before.cr));
                for (unsigned r = 0; r < 32; ++r) CHECK(result.fpr[r] == (r == real_fd ? expected : before.fpr[r]));
                CHECK(fegetround() == host_round && fetestexcept(FE_ALL_EXCEPT) == host_flags);
            }
        }
    }
    fesetenv(&environment); nw_jit_set_host_chain(powerpc_cpu::jit_host_chain); nw_jit_set_mode(NW_JIT_ON);
    printf("P6 scalar conversion: %u literal engine cases; compare: %u cases; NaN/moves: %u cases\n", conversion_cases, compare_cases, cases-conversion_cases-compare_cases);
    nw_jit_set_host_fp_exception(powerpc_cpu::jit_host_fp_exception);
    return failed ? 1 : 0;
}
int ppc_core_test_access::frsp_p6(powerpc_cpu *cpu)
{
    // Literal outcomes from exact rational boundary values, not either engine.
    // flags: FI=1, FR=2, UX=4, OX=8, VXSNAN=16, preserve FR/FI=32.
    struct round_case { uint64 input, result[4]; unsigned flags[4]; uint64 adjusted[4]; unsigned adjusted_flags[4]; };
    const round_case inputs[] = {
        {0x0000000000000000ULL,{0x0000000000000000ULL,0x0000000000000000ULL,0x0000000000000000ULL,0x0000000000000000ULL},{0,0,0,0},{0x0000000000000000ULL,0x0000000000000000ULL,0x0000000000000000ULL,0x0000000000000000ULL},{0,0,0,0}},
        {0x3ff0000000000000ULL,{0x3ff0000000000000ULL,0x3ff0000000000000ULL,0x3ff0000000000000ULL,0x3ff0000000000000ULL},{0,0,0,0},{0x3ff0000000000000ULL,0x3ff0000000000000ULL,0x3ff0000000000000ULL,0x3ff0000000000000ULL},{0,0,0,0}},
        {0x3ff0000010000000ULL,{0x3ff0000000000000ULL,0x3ff0000000000000ULL,0x3ff0000020000000ULL,0x3ff0000000000000ULL},{1,1,3,1},{0x3ff0000000000000ULL,0x3ff0000000000000ULL,0x3ff0000020000000ULL,0x3ff0000000000000ULL},{1,1,3,1}},
        {0x3ff0000010000001ULL,{0x3ff0000020000000ULL,0x3ff0000000000000ULL,0x3ff0000020000000ULL,0x3ff0000000000000ULL},{3,1,3,1},{0x3ff0000020000000ULL,0x3ff0000000000000ULL,0x3ff0000020000000ULL,0x3ff0000000000000ULL},{3,1,3,1}},
        {0x3ff000000fffffffULL,{0x3ff0000000000000ULL,0x3ff0000000000000ULL,0x3ff0000020000000ULL,0x3ff0000000000000ULL},{1,1,3,1},{0x3ff0000000000000ULL,0x3ff0000000000000ULL,0x3ff0000020000000ULL,0x3ff0000000000000ULL},{1,1,3,1}},
        {0x3ff0000030000000ULL,{0x3ff0000040000000ULL,0x3ff0000020000000ULL,0x3ff0000040000000ULL,0x3ff0000020000000ULL},{3,1,3,1},{0x3ff0000040000000ULL,0x3ff0000020000000ULL,0x3ff0000040000000ULL,0x3ff0000020000000ULL},{3,1,3,1}},
        {0x47efffffe0000000ULL,{0x47efffffe0000000ULL,0x47efffffe0000000ULL,0x47efffffe0000000ULL,0x47efffffe0000000ULL},{0,0,0,0},{0x47efffffe0000000ULL,0x47efffffe0000000ULL,0x47efffffe0000000ULL,0x47efffffe0000000ULL},{0,0,0,0}},
        {0x47efffffe8000000ULL,{0x47efffffe0000000ULL,0x47efffffe0000000ULL,0x7ff0000000000000ULL,0x47efffffe0000000ULL},{1,1,9,1},{0x47efffffe0000000ULL,0x47efffffe0000000ULL,0x3bf0000000000000ULL,0x47efffffe0000000ULL},{1,1,11,1}},
        {0x47effffff0000000ULL,{0x7ff0000000000000ULL,0x47efffffe0000000ULL,0x7ff0000000000000ULL,0x47efffffe0000000ULL},{9,1,9,1},{0x3bf0000000000000ULL,0x47efffffe0000000ULL,0x3bf0000000000000ULL,0x47efffffe0000000ULL},{11,1,11,1}},
        {0x47f0000000000000ULL,{0x7ff0000000000000ULL,0x47efffffe0000000ULL,0x7ff0000000000000ULL,0x47efffffe0000000ULL},{9,9,9,9},{0x3bf0000000000000ULL,0x3bf0000000000000ULL,0x3bf0000000000000ULL,0x3bf0000000000000ULL},{8,8,8,8}},
        {0x7fefffffffffffffULL,{0x7ff0000000000000ULL,0x47efffffe0000000ULL,0x7ff0000000000000ULL,0x47efffffe0000000ULL},{9,9,9,9},{0x73f0000000000000ULL,0x73efffffe0000000ULL,0x73f0000000000000ULL,0x73efffffe0000000ULL},{11,9,11,9}},
        {0x3810000000000000ULL,{0x3810000000000000ULL,0x3810000000000000ULL,0x3810000000000000ULL,0x3810000000000000ULL},{0,0,0,0},{0x3810000000000000ULL,0x3810000000000000ULL,0x3810000000000000ULL,0x3810000000000000ULL},{0,0,0,0}},
        {0x380fffffc0000000ULL,{0x380fffffc0000000ULL,0x380fffffc0000000ULL,0x380fffffc0000000ULL,0x380fffffc0000000ULL},{0,0,0,0},{0x440fffffc0000000ULL,0x440fffffc0000000ULL,0x440fffffc0000000ULL,0x440fffffc0000000ULL},{4,4,4,4}},
        {0x380fffffe0000000ULL,{0x3810000000000000ULL,0x380fffffc0000000ULL,0x3810000000000000ULL,0x380fffffc0000000ULL},{7,5,7,5},{0x440fffffe0000000ULL,0x440fffffe0000000ULL,0x440fffffe0000000ULL,0x440fffffe0000000ULL},{4,4,4,4}},
        {0x3800000000000000ULL,{0x3800000000000000ULL,0x3800000000000000ULL,0x3800000000000000ULL,0x3800000000000000ULL},{0,0,0,0},{0x4400000000000000ULL,0x4400000000000000ULL,0x4400000000000000ULL,0x4400000000000000ULL},{4,4,4,4}},
        {0x36a0000000000000ULL,{0x36a0000000000000ULL,0x36a0000000000000ULL,0x36a0000000000000ULL,0x36a0000000000000ULL},{0,0,0,0},{0x42a0000000000000ULL,0x42a0000000000000ULL,0x42a0000000000000ULL,0x42a0000000000000ULL},{4,4,4,4}},
        {0x3690000000000000ULL,{0x0000000000000000ULL,0x0000000000000000ULL,0x36a0000000000000ULL,0x0000000000000000ULL},{5,5,7,5},{0x4290000000000000ULL,0x4290000000000000ULL,0x4290000000000000ULL,0x4290000000000000ULL},{4,4,4,4}},
        {0x3690000000000001ULL,{0x36a0000000000000ULL,0x0000000000000000ULL,0x36a0000000000000ULL,0x0000000000000000ULL},{7,5,7,5},{0x4290000000000000ULL,0x4290000000000000ULL,0x4290000020000000ULL,0x4290000000000000ULL},{5,5,7,5}},
        {0x368fffffffffffffULL,{0x0000000000000000ULL,0x0000000000000000ULL,0x36a0000000000000ULL,0x0000000000000000ULL},{5,5,7,5},{0x4290000000000000ULL,0x428fffffe0000000ULL,0x4290000000000000ULL,0x428fffffe0000000ULL},{7,5,7,5}},
        {0x0000000000000001ULL,{0x0000000000000000ULL,0x0000000000000000ULL,0x36a0000000000000ULL,0x0000000000000000ULL},{5,5,7,5},{0x08d0000000000000ULL,0x08d0000000000000ULL,0x08d0000000000000ULL,0x08d0000000000000ULL},{4,4,4,4}},
        {0x0000000000000003ULL,{0x0000000000000000ULL,0x0000000000000000ULL,0x36a0000000000000ULL,0x0000000000000000ULL},{5,5,7,5},{0x08e8000000000000ULL,0x08e8000000000000ULL,0x08e8000000000000ULL,0x08e8000000000000ULL},{4,4,4,4}},
        {0x0010000000000000ULL,{0x0000000000000000ULL,0x0000000000000000ULL,0x36a0000000000000ULL,0x0000000000000000ULL},{5,5,7,5},{0x0c10000000000000ULL,0x0c10000000000000ULL,0x0c10000000000000ULL,0x0c10000000000000ULL},{4,4,4,4}},
        {0x07b0000000000000ULL,{0x0000000000000000ULL,0x0000000000000000ULL,0x36a0000000000000ULL,0x0000000000000000ULL},{5,5,7,5},{0x13b0000000000000ULL,0x13b0000000000000ULL,0x13b0000000000000ULL,0x13b0000000000000ULL},{4,4,4,4}},
        {0x7ff0000000000000ULL,{0x7ff0000000000000ULL,0x7ff0000000000000ULL,0x7ff0000000000000ULL,0x7ff0000000000000ULL},{32,32,32,32},{0x7ff0000000000000ULL,0x7ff0000000000000ULL,0x7ff0000000000000ULL,0x7ff0000000000000ULL},{32,32,32,32}},
        {0x7ff8123456789abcULL,{0x7ff8123440000000ULL,0x7ff8123440000000ULL,0x7ff8123440000000ULL,0x7ff8123440000000ULL},{0,0,0,0},{0x7ff8123440000000ULL,0x7ff8123440000000ULL,0x7ff8123440000000ULL,0x7ff8123440000000ULL},{0,0,0,0}},
        {0x7ff0000000000001ULL,{0x7ff8000000000000ULL,0x7ff8000000000000ULL,0x7ff8000000000000ULL,0x7ff8000000000000ULL},{16,16,16,16},{0x7ff8000000000000ULL,0x7ff8000000000000ULL,0x7ff8000000000000ULL,0x7ff8000000000000ULL},{16,16,16,16}},
        {0x7ff0123456789abcULL,{0x7ff8123440000000ULL,0x7ff8123440000000ULL,0x7ff8123440000000ULL,0x7ff8123440000000ULL},{16,16,16,16},{0x7ff8123440000000ULL,0x7ff8123440000000ULL,0x7ff8123440000000ULL,0x7ff8123440000000ULL},{16,16,16,16}},
    };
    struct control { unsigned enable, fe, ip; };
    const control controls[] = {
        {0,0,0},{0,0x900,0x40},{0x80,0,0},{0x80,0x100,0},
        {0x80,0x800,0},{0x80,0x900,0x40},{0x40,0,0},{0x40,0x900,0x40},
        {0x20,0,0},{0x20,0x100,0},{0x20,0x800,0x40},{0x20,0x900,0},
        {8,0,0},{8,0x900,0x40},{0xe8,0,0},{0xe8,0x900,0x40}
    };
    cpu->enable_guest_mmu(true); nw_jit_set_mode(NW_JIT_VERIFY);
    nw_jit_set_host_fp_exception(checked_fp_exception); nw_jit_set_host_chain(NULL);
    ppc32_mmu &mmu = ppc32_guest_mmu();
    const uint32 base = 0x10000000u, pc = base+0x1000;
    void *const wanted = (void *)(VMBaseDiff+base);
    void *const ram = mmap(wanted,0x2000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    CHECK(ram == wanted); if (ram != wanted) return 1;
    nw_banks_set(NW_PA_RAM,base,0x2000);
    fenv_t environment; fegetenv(&environment);
    const int host_modes[] = {FE_TONEAREST,FE_TOWARDZERO,FE_UPWARD,FE_DOWNWARD};
    unsigned cases = 0, publication_cases = 0;
    for (unsigned alias : {0u,1u})
    for (unsigned rc : {0u,1u}) {
        const unsigned fd = alias ? 1 : 3;
        const uint32 ops[] = {(63u<<26)|(7u<<21)|(4u<<11)|(40u<<1),
            (63u<<26)|(fd<<21)|(1u<<11)|(12u<<1)|rc,
            (63u<<26)|(5u<<21)|(6u<<11)|(72u<<1)};
        nw_jit_invalidate_all();
        for (unsigned i = 0; i < 3; ++i) vm_write_memory_4(pc+4*i,ops[i]);
        vm_write_memory_4(pc+12,0); // builder stops at the unsupported sentinel
        nw_jit_fn fn = nw_jit_compile(ops,3,pc,pc,0,0); CHECK(fn != NULL); if (!fn) continue;
        for (const auto &test : inputs)
        for (unsigned negative : {0u,1u})
        for (unsigned rn = 0; rn < 4; ++rn)
        for (const auto &control : controls)
        for (unsigned sticky : {0u,0x1b000000u,0x9b000000u})
        for (unsigned engine = 0; engine < 6; ++engine) {
            ++cases; if (engine >= 4) ++publication_cases;
            const unsigned failures = failed;
            const uint64 sign = uint64(negative)<<63, source = test.input|sign;
            const unsigned rounding = negative && rn >= 2 ? rn ^ 1u : rn;
            const unsigned exp = unsigned(test.input>>52);
            const bool tiny = test.input && exp < 897;
            const bool adjusted = (tiny && (control.enable & 0x20u)) ||
                ((test.flags[rounding] & 8u) && (control.enable & 0x40u));
            const unsigned status = adjusted ? test.adjusted_flags[rounding] : test.flags[rounding];
            const uint64 answer = (adjusted ? test.adjusted[rounding] : test.result[rounding])|sign;
            const uint32 causes = (status&1 ? 0x02000000u : 0) | (status&4 ? 0x08000000u : 0) |
                (status&8 ? 0x10000000u : 0) | (status&16 ? 0x01000000u : 0);
            const bool suppressed = (status&16) && (control.enable&0x80u);
            nw_jit_cpu before = {}; before.pc=pc; before.msr=0x2000u|control.fe|control.ip;
            before.cr=0xb2345678u; before.xer=0xe0000000u;
            before.fpscr=sticky|control.enable|rn|0x75000u;
            // Independently derive initial and resulting summary bits from causes/enables.
            auto summaries = [](uint32 f) {
                f &= ~0x60000000u;
                if (f & 0x01f80700u) f |= 0x20000000u;
                if ((f&0x20000000u) && (f&0x80u) || (f&0x10000000u) && (f&0x40u) ||
                    (f&0x08000000u) && (f&0x20u) || (f&0x04000000u) && (f&0x10u) ||
                    (f&0x02000000u) && (f&8u)) f |= 0x40000000u;
                return f;
            };
            before.fpscr=summaries(before.fpscr);
            uint32 expected_fpscr=before.fpscr;
            if (!(status&32)) expected_fpscr=(expected_fpscr&~0x60000u)|(status&1 ? 0x20000u : 0)|(status&2 ? 0x40000u : 0);
            if (!suppressed) {
                const uint64 magnitude=answer&0x7fffffffffffffffULL;
                const uint32 classification=magnitude>0x7ff0000000000000ULL ? 17 : !magnitude ? negative ? 18 : 2 :
                    magnitude==0x7ff0000000000000ULL ? negative ? 9 : 5 :
                    !adjusted && magnitude<0x3810000000000000ULL ? negative ? 24 : 20 : negative ? 8 : 4;
                expected_fpscr=(expected_fpscr&~0x1f000u)|(classification<<12);
            }
            if (causes&~before.fpscr) expected_fpscr|=0x80000000u;
            expected_fpscr=summaries(expected_fpscr|causes);
            const bool except=control.fe && (expected_fpscr&0x40000000u);
            before.srr0=0x12345678; before.srr1=0x87654321;
            for (unsigned r=0;r<32;++r) {
                before.fpr[r]=bits(double(r+32)); before.gpr[r]=0x12340000u+r;
                for (unsigned w=0;w<4;++w) before.vr[r][w]=0x89100000u+r*4+w;
            }
            before.fpr[1]=source;
            mmu.reset(); mmu.set_msr(before.msr); cpu->pc()=pc; cpu->last_fetch_pa_=pc;
            cpu->dec_=1000000; cpu->dec_tb_base_=cpu->tb_host_ticks(); cpu->dec_pending_=false; before.dec=cpu->dec_;
            cpu->fpscr()=before.fpscr; cpu->cr().set(before.cr); cpu->xer().set(before.xer); cpu->lr()=cpu->ctr()=0;
            cpu->srr0_=before.srr0; cpu->srr1_=before.srr1; cpu->spcflags().init(); cpu->regs().reserve_valid=0;
            for (unsigned r=0;r<32;++r) {
                cpu->fpr_dw(r)=before.fpr[r]; cpu->gpr(r)=before.gpr[r];
                for (unsigned w=0;w<4;++w) cpu->vr(r).w[w]=before.vr[r][w];
            }
            nw_jit_cpu result=before;
            fesetround(host_modes[engine&3]); feclearexcept(FE_ALL_EXCEPT); feraiseexcept(FE_DIVBYZERO);
            fp_callback_round=fegetround(); fp_callback_flags=fetestexcept(FE_ALL_EXCEPT);
            const uint64 serial=cpu->exception_serial_, misses=nw_jit_verify_misses();
            if (!engine) {
                for (uint32 op : ops) { instruction(cpu,op); if (cpu->exception_serial_!=serial) break; }
            } else if (engine==1) (void)nw_jit_interp_n(&result,ops,3,pc);
            else if (engine==2) { result.host=cpu; nw_jit_cpu_bind(&result); nw_jit_tail_begin(); fn(&result); }
            else if (engine==3) CHECK(cpu->nw_jit_verify_block(result,fn,ops,3)==1);
            else { nw_jit_set_mode(engine==4 ? NW_JIT_ON : NW_JIT_VERIFY); CHECK(cpu->nw_jit_try(ops[0])==1); }
            CHECK(nw_jit_verify_misses()==misses);
            if (engine==0 || engine>=3) {
                result.pc=cpu->pc(); result.fpscr=cpu->fpscr(); result.cr=cpu->cr().get(); result.msr=mmu.msr();
                result.srr0=cpu->srr0_; result.srr1=cpu->srr1_;
                for (unsigned r=0;r<32;++r) {
                    result.fpr[r]=cpu->fpr_dw(r); result.gpr[r]=cpu->gpr(r);
                    for (unsigned w=0;w<4;++w) result.vr[r][w]=cpu->vr(r).w[w];
                }
            }
            CHECK(result.fpscr==expected_fpscr);
            CHECK(result.cr==(rc ? (before.cr&~0x0f000000u)|((expected_fpscr>>4)&0x0f000000u) : before.cr));
            CHECK(result.pc==(except ? control.ip ? 0xfff00700u : 0x700u : pc+12));
            CHECK(result.msr==(except ? before.msr&~0x0204ef32u : before.msr));
            CHECK(result.srr0==(except ? pc+4 : before.srr0));
            CHECK(result.srr1==(except ? (before.msr&~0x783f0000u)|0x100000u : before.srr1));
            if (engine==0 || engine==2 || engine>=4) CHECK(cpu->exception_serial_==serial+unsigned(except));
            for (unsigned r=0;r<32;++r) {
                CHECK(result.fpr[r]==(r==7 ? before.fpr[4]^0x8000000000000000ULL :
                    r==fd ? suppressed ? before.fpr[r] : answer : r==5 && !except ? before.fpr[6] : before.fpr[r]));
                CHECK(result.gpr[r]==before.gpr[r]);
                for (unsigned w=0;w<4;++w) CHECK(result.vr[r][w]==before.vr[r][w]);
            }
            CHECK(fegetround()==fp_callback_round && fetestexcept(FE_ALL_EXCEPT)==fp_callback_flags);
            if (failed!=failures && failed<100) fprintf(stderr,"frsp engine=%u source=%016llx rn=%u en=%x fe=%x status=%x got=%016llx/%08x expected=%016llx/%08x\n",engine,(unsigned long long)source,rn,control.enable,control.fe,status,(unsigned long long)result.fpr[fd],result.fpscr,(unsigned long long)answer,expected_fpscr);
        }
    }
    printf("P6 frsp: %u literal engine cases, including %u production entry/commit cases\n",cases,publication_cases);
    fesetenv(&environment); nw_jit_invalidate_all(); nw_banks_set(NW_PA_RAM,0,0); munmap(ram,0x2000);
    nw_jit_set_host_fp_exception(powerpc_cpu::jit_host_fp_exception);
    nw_jit_set_host_chain(powerpc_cpu::jit_host_chain); nw_jit_set_mode(NW_JIT_ON);
    return failed ? 1 : 0;
}
int ppc_core_test_access::basic_special_p6(powerpc_cpu *cpu)
{
    // Literal special-result and finite-control expectations. Mask bits select
    // divide/subtract/add/multiply; cancellation zeros acquire the RN sign.
    struct basic_case { unsigned mask; uint64 a,b,result; uint32 cause; bool clear, cancel; };
    const basic_case inputs[] = {
        {15,0x7ff8123456789abcULL,0x3ff0000000000000ULL,0x7ff8123456789abcULL,0x00000000u,true,false},
        {15,0x3ff0000000000000ULL,0xfff8abcdef123456ULL,0xfff8abcdef123456ULL,0x00000000u,true,false},
        {15,0x7ff0123456789abcULL,0x3ff0000000000000ULL,0x7ff8123456789abcULL,0x01000000u,true,false},
        {15,0x3ff0000000000000ULL,0xfff0abcdef123456ULL,0xfff8abcdef123456ULL,0x01000000u,true,false},
        {15,0x7ff8123456789abcULL,0xfff0abcdef123456ULL,0x7ff8123456789abcULL,0x01000000u,true,false},
        {15,0x7ff0123456789abcULL,0xfff8abcdef123456ULL,0x7ff8123456789abcULL,0x01000000u,true,false},
        {15,0x7ff8123456789abcULL,0xfff8abcdef123456ULL,0x7ff8123456789abcULL,0x00000000u,true,false},
        {15,0x7ff0123456789abcULL,0xfff0abcdef123456ULL,0x7ff8123456789abcULL,0x01000000u,true,false},
        {15,0x7ff0000000000001ULL,0x0000000000000000ULL,0x7ff8000000000001ULL,0x01000000u,true,false},
        {15,0x7ff8123456789abcULL,0x7ff8123456789abcULL,0x7ff8123456789abcULL,0x00000000u,true,false},
        {15,0x7ff0123456789abcULL,0x7ff0123456789abcULL,0x7ff8123456789abcULL,0x01000000u,true,false},
        {4,0x7ff0000000000000ULL,0xfff0000000000000ULL,0x7ff8000000000000ULL,0x00800000u,true,false},
        {2,0x7ff0000000000000ULL,0xfff0000000000000ULL,0x7ff0000000000000ULL,0x00000000u,true,false},
        {1,0x7ff0000000000000ULL,0xfff0000000000000ULL,0x7ff8000000000000ULL,0x00400000u,true,false},
        {4,0xfff0000000000000ULL,0x7ff0000000000000ULL,0x7ff8000000000000ULL,0x00800000u,true,false},
        {2,0xfff0000000000000ULL,0x7ff0000000000000ULL,0xfff0000000000000ULL,0x00000000u,true,false},
        {1,0xfff0000000000000ULL,0x7ff0000000000000ULL,0x7ff8000000000000ULL,0x00400000u,true,false},
        {4,0x7ff0000000000000ULL,0x7ff0000000000000ULL,0x7ff0000000000000ULL,0x00000000u,true,false},
        {2,0x7ff0000000000000ULL,0x7ff0000000000000ULL,0x7ff8000000000000ULL,0x00800000u,true,false},
        {1,0x7ff0000000000000ULL,0x7ff0000000000000ULL,0x7ff8000000000000ULL,0x00400000u,true,false},
        {4,0xfff0000000000000ULL,0xfff0000000000000ULL,0xfff0000000000000ULL,0x00000000u,true,false},
        {2,0xfff0000000000000ULL,0xfff0000000000000ULL,0x7ff8000000000000ULL,0x00800000u,true,false},
        {1,0xfff0000000000000ULL,0xfff0000000000000ULL,0x7ff8000000000000ULL,0x00400000u,true,false},
        {1,0x0000000000000000ULL,0x0000000000000000ULL,0x7ff8000000000000ULL,0x00200000u,true,false},
        {4,0x0000000000000000ULL,0x0000000000000000ULL,0x0000000000000000ULL,0x00000000u,true,false},
        {2,0x0000000000000000ULL,0x0000000000000000ULL,0x0000000000000000ULL,0x00000000u,true,true},
        {1,0x0000000000000000ULL,0x8000000000000000ULL,0x7ff8000000000000ULL,0x00200000u,true,false},
        {4,0x0000000000000000ULL,0x8000000000000000ULL,0x0000000000000000ULL,0x00000000u,true,true},
        {2,0x0000000000000000ULL,0x8000000000000000ULL,0x0000000000000000ULL,0x00000000u,true,false},
        {1,0x8000000000000000ULL,0x0000000000000000ULL,0x7ff8000000000000ULL,0x00200000u,true,false},
        {4,0x8000000000000000ULL,0x0000000000000000ULL,0x0000000000000000ULL,0x00000000u,true,true},
        {2,0x8000000000000000ULL,0x0000000000000000ULL,0x8000000000000000ULL,0x00000000u,true,false},
        {1,0x8000000000000000ULL,0x8000000000000000ULL,0x7ff8000000000000ULL,0x00200000u,true,false},
        {4,0x8000000000000000ULL,0x8000000000000000ULL,0x8000000000000000ULL,0x00000000u,true,false},
        {2,0x8000000000000000ULL,0x8000000000000000ULL,0x0000000000000000ULL,0x00000000u,true,true},
        {1,0x3ff0000000000000ULL,0x0000000000000000ULL,0x7ff0000000000000ULL,0x04000000u,true,false},
        {1,0x3ff0000000000000ULL,0x8000000000000000ULL,0xfff0000000000000ULL,0x04000000u,true,false},
        {1,0xbff0000000000000ULL,0x0000000000000000ULL,0xfff0000000000000ULL,0x04000000u,true,false},
        {1,0xbff0000000000000ULL,0x8000000000000000ULL,0x7ff0000000000000ULL,0x04000000u,true,false},
        {8,0x0000000000000000ULL,0x7ff0000000000000ULL,0x7ff8000000000000ULL,0x00100000u,true,false},
        {8,0x7ff0000000000000ULL,0x0000000000000000ULL,0x7ff8000000000000ULL,0x00100000u,true,false},
        {8,0x0000000000000000ULL,0xfff0000000000000ULL,0x7ff8000000000000ULL,0x00100000u,true,false},
        {8,0xfff0000000000000ULL,0x0000000000000000ULL,0x7ff8000000000000ULL,0x00100000u,true,false},
        {8,0x8000000000000000ULL,0x7ff0000000000000ULL,0x7ff8000000000000ULL,0x00100000u,true,false},
        {8,0x7ff0000000000000ULL,0x8000000000000000ULL,0x7ff8000000000000ULL,0x00100000u,true,false},
        {8,0x8000000000000000ULL,0xfff0000000000000ULL,0x7ff8000000000000ULL,0x00100000u,true,false},
        {8,0xfff0000000000000ULL,0x8000000000000000ULL,0x7ff8000000000000ULL,0x00100000u,true,false},
        {15,0x7ff0000000000000ULL,0x3ff0000000000000ULL,0x7ff0000000000000ULL,0x00000000u,true,false},
        {15,0xfff0000000000000ULL,0x3ff0000000000000ULL,0xfff0000000000000ULL,0x00000000u,true,false},
        {4,0x3ff0000000000000ULL,0x7ff0000000000000ULL,0x7ff0000000000000ULL,0x00000000u,true,false},
        {2,0x3ff0000000000000ULL,0x7ff0000000000000ULL,0xfff0000000000000ULL,0x00000000u,true,false},
        {8,0x3ff0000000000000ULL,0x7ff0000000000000ULL,0x7ff0000000000000ULL,0x00000000u,true,false},
        {1,0x3ff0000000000000ULL,0x7ff0000000000000ULL,0x0000000000000000ULL,0x00000000u,true,false},
        {4,0x3ff0000000000000ULL,0xfff0000000000000ULL,0xfff0000000000000ULL,0x00000000u,true,false},
        {2,0x3ff0000000000000ULL,0xfff0000000000000ULL,0x7ff0000000000000ULL,0x00000000u,true,false},
        {8,0x3ff0000000000000ULL,0xfff0000000000000ULL,0xfff0000000000000ULL,0x00000000u,true,false},
        {1,0x3ff0000000000000ULL,0xfff0000000000000ULL,0x8000000000000000ULL,0x00000000u,true,false},
        {8,0x0000000000000000ULL,0x4000000000000000ULL,0x0000000000000000ULL,0x00000000u,true,false},
        {1,0x0000000000000000ULL,0x4000000000000000ULL,0x0000000000000000ULL,0x00000000u,true,false},
        {8,0x8000000000000000ULL,0x4000000000000000ULL,0x8000000000000000ULL,0x00000000u,true,false},
        {1,0x8000000000000000ULL,0x4000000000000000ULL,0x8000000000000000ULL,0x00000000u,true,false},
        {8,0x4000000000000000ULL,0x0000000000000000ULL,0x0000000000000000ULL,0x00000000u,true,false},
        {8,0x4000000000000000ULL,0x8000000000000000ULL,0x8000000000000000ULL,0x00000000u,true,false},
        {1,0x4000000000000000ULL,0x8000000000000000ULL,0xfff0000000000000ULL,0x04000000u,true,false},
        {1,0x7ff0000000000000ULL,0x0000000000000000ULL,0x7ff0000000000000ULL,0x00000000u,true,false},
        {1,0x7ff0000000000000ULL,0x8000000000000000ULL,0xfff0000000000000ULL,0x00000000u,true,false},
        {1,0xfff0000000000000ULL,0x0000000000000000ULL,0xfff0000000000000ULL,0x00000000u,true,false},
        {1,0xfff0000000000000ULL,0x8000000000000000ULL,0x7ff0000000000000ULL,0x00000000u,true,false},
        {2,0x0000000000000001ULL,0x0000000000000001ULL,0x0000000000000000ULL,0x00000000u,false,true},
        {4,0x0000000000000000ULL,0x4000000000000000ULL,0x4000000000000000ULL,0x00000000u,false,false},
        {2,0x0000000000000000ULL,0x4000000000000000ULL,0xc000000000000000ULL,0x00000000u,false,false},
        {4,0x3ff0000000000000ULL,0xbff0000000000000ULL,0x0000000000000000ULL,0x00000000u,false,true},
        {2,0x3ff0000000000000ULL,0x3ff0000000000000ULL,0x0000000000000000ULL,0x00000000u,false,true},
        {8,0x0000000000000000ULL,0x0000000000000000ULL,0x0000000000000000ULL,0x00000000u,true,false},
        {8,0x0000000000000000ULL,0x8000000000000000ULL,0x8000000000000000ULL,0x00000000u,true,false},
        {8,0x8000000000000000ULL,0x0000000000000000ULL,0x8000000000000000ULL,0x00000000u,true,false},
        {8,0x8000000000000000ULL,0x8000000000000000ULL,0x0000000000000000ULL,0x00000000u,true,false},
        {8,0x7ff0000000000000ULL,0x7ff0000000000000ULL,0x7ff0000000000000ULL,0x00000000u,true,false},
        {8,0x7ff0000000000000ULL,0xfff0000000000000ULL,0xfff0000000000000ULL,0x00000000u,true,false},
        {8,0xfff0000000000000ULL,0x7ff0000000000000ULL,0xfff0000000000000ULL,0x00000000u,true,false},
        {8,0xfff0000000000000ULL,0xfff0000000000000ULL,0x7ff0000000000000ULL,0x00000000u,true,false},
        {4,0x3ff0000000000000ULL,0x4000000000000000ULL,0x4008000000000000ULL,0x00000000u,false,false},
        {2,0x3ff0000000000000ULL,0x4000000000000000ULL,0xbff0000000000000ULL,0x00000000u,false,false},
        {8,0x3ff0000000000000ULL,0x4000000000000000ULL,0x4000000000000000ULL,0x00000000u,false,false},
        {1,0x3ff0000000000000ULL,0x4000000000000000ULL,0x3fe0000000000000ULL,0x00000000u,false,false},
    };
    struct control { unsigned enable, fe, ip; };
    const control controls[] = {
        {0,0,0},{0,0x900,0x40},{0x80,0,0},{0x80,0x100,0},
        {0x80,0x800,0},{0x80,0x900,0x40},{0x10,0,0},{0x10,0x100,0},
        {0x10,0x800,0x40},{0x10,0x900,0},{0x90,0,0},{0x90,0x900,0x40},
        {8,0,0},{8,0x900,0x40},{0xe8,0,0},{0xf8,0x900,0x40}
    };
    cpu->enable_guest_mmu(true); nw_jit_set_mode(NW_JIT_VERIFY);
    nw_jit_set_host_fp_exception(checked_fp_exception); nw_jit_set_host_chain(NULL);
    ppc32_mmu &mmu = ppc32_guest_mmu();
    const uint32 base = 0x10000000u, pc = base+0x1000;
    void *const wanted = (void *)(VMBaseDiff+base);
    void *const ram = mmap(wanted,0x2000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    CHECK(ram == wanted); if (ram != wanted) return 1;
    nw_banks_set(NW_PA_RAM,base,0x2000);
    fenv_t environment; fegetenv(&environment);
    const int host_modes[] = {FE_TONEAREST,FE_TOWARDZERO,FE_UPWARD,FE_DOWNWARD};
    unsigned cases = 0, publication_cases = 0;
    const bool baseline = getenv("PPC_BASIC_BASELINE") != NULL;
    for (unsigned precision : {59u,63u})
    for (unsigned kind = 0; kind < 4; ++kind)
    for (unsigned alias = 0; alias < 6; ++alias)
    for (unsigned rc : {0u,1u}) {
        if (baseline && (alias || rc)) continue;
        const unsigned fa=1, fb=alias>=4 ? 1 : 2;
        const unsigned fd=alias==1 || alias==5 ? 1 : alias==2 ? fb : alias==3 ? 0 : 4;
        const unsigned kinds[]={18,20,21,25}, xo=kinds[kind];
        const uint32 ops[] = {(63u<<26)|(7u<<21)|(8u<<11)|(40u<<1),
            (precision<<26)|(fd<<21)|(fa<<16)|(fb<<(kind==3 ? 6 : 11))|(xo<<1)|rc,
            (63u<<26)|(5u<<21)|(6u<<11)|(72u<<1)};
        nw_jit_invalidate_all();
        for (unsigned i = 0; i < 3; ++i) vm_write_memory_4(pc+4*i,ops[i]);
        vm_write_memory_4(pc+12,0); // builder stops at the unsupported sentinel
        nw_jit_fn fn = nw_jit_compile(ops,3,pc,pc,0,0); CHECK(fn != NULL); if (!fn) continue;
        for (const auto &test : inputs)
        for (unsigned rn = 0; rn < 4; ++rn)
        for (const auto &control : controls)
        for (unsigned sticky : {0u,0x1ff00000u,0x9ff00000u})
        for (unsigned engine = 0; engine < 6; ++engine) {
            if (!(test.mask & (1u<<kind)) || (alias>=4 && test.a!=test.b)) continue;
            if (baseline && (rn || sticky || (engine!=0 && engine!=2) || control.ip || control.fe ||
                (control.enable!=0 && control.enable!=0x80 && control.enable!=0x10))) continue;
            ++cases; if (engine>=4) ++publication_cases;
            const unsigned failures=failed;
            const uint64 answer=test.cancel && rn==3 ? 0x8000000000000000ULL : test.result;
            const uint32 causes=test.cause;
            const bool suppressed=(causes&0x01f00000u) && (control.enable&0x80u) ||
                (causes&0x04000000u) && (control.enable&0x10u);
            nw_jit_cpu before={}; before.pc=pc; before.msr=0x2000u|control.fe|control.ip;
            before.cr=0xb2345678u; before.xer=0xe0000000u;
            before.fpscr=sticky|control.enable|rn|0x15000u|(test.clear ? 0x60000u : 0);
            auto summaries=[](uint32 f) {
                f&=~0x60000000u;
                if (f&0x01f80700u) f|=0x20000000u;
                if ((f&0x20000000u)&&(f&0x80u) || (f&0x10000000u)&&(f&0x40u) ||
                    (f&0x08000000u)&&(f&0x20u) || (f&0x04000000u)&&(f&0x10u) ||
                    (f&0x02000000u)&&(f&8u)) f|=0x40000000u;
                return f;
            };
            before.fpscr=summaries(before.fpscr);
            uint32 expected_fpscr=before.fpscr;
            if (test.clear) expected_fpscr&=~0x60000u;
            if (!suppressed) {
                const uint64 magnitude=answer&0x7fffffffffffffffULL;
                const bool negative=answer>>63;
                const uint32 classification=magnitude>0x7ff0000000000000ULL ? 17 : !magnitude ? negative ? 18 : 2 :
                    magnitude==0x7ff0000000000000ULL ? negative ? 9 : 5 : negative ? 8 : 4;
                expected_fpscr=(expected_fpscr&~0x1f000u)|(classification<<12);
            }
            if (causes&~before.fpscr) expected_fpscr|=0x80000000u;
            expected_fpscr=summaries(expected_fpscr|causes);
            const bool except=control.fe && (expected_fpscr&0x40000000u);
            before.srr0=0x12345678; before.srr1=0x87654321;
            for (unsigned r=0;r<32;++r) {
                before.fpr[r]=bits(double(r+32)); before.gpr[r]=0x12340000u+r;
                for (unsigned w=0;w<4;++w) before.vr[r][w]=0x89100000u+r*4+w;
            }
            before.fpr[fa]=test.a; before.fpr[fb]=test.b;
            // Encoded fixed-zero operand fields must not read FPR0.
            before.fpr[0]=0xfff0000000000001ULL;
            mmu.reset(); mmu.set_msr(before.msr); cpu->pc()=pc; cpu->last_fetch_pa_=pc;
            cpu->dec_=1000000; cpu->dec_tb_base_=cpu->tb_host_ticks(); cpu->dec_pending_=false; before.dec=cpu->dec_;
            cpu->fpscr()=before.fpscr; cpu->cr().set(before.cr); cpu->xer().set(before.xer); cpu->lr()=cpu->ctr()=0;
            cpu->srr0_=before.srr0; cpu->srr1_=before.srr1; cpu->spcflags().init(); cpu->regs().reserve_valid=0;
            for (unsigned r=0;r<32;++r) {
                cpu->fpr_dw(r)=before.fpr[r]; cpu->gpr(r)=before.gpr[r];
                for (unsigned w=0;w<4;++w) cpu->vr(r).w[w]=before.vr[r][w];
            }
            nw_jit_cpu result=before;
            fesetround(host_modes[engine&3]); feclearexcept(FE_ALL_EXCEPT); feraiseexcept(FE_DIVBYZERO);
            fp_callback_round=fegetround(); fp_callback_flags=fetestexcept(FE_ALL_EXCEPT);
            const uint64 serial=cpu->exception_serial_, misses=nw_jit_verify_misses();
            if (!engine) {
                for (uint32 op : ops) { instruction(cpu,op); if (cpu->exception_serial_!=serial) break; }
            } else if (engine==1) (void)nw_jit_interp_n(&result,ops,3,pc);
            else if (engine==2) { result.host=cpu; nw_jit_cpu_bind(&result); nw_jit_tail_begin(); fn(&result); }
            else if (engine==3) CHECK(cpu->nw_jit_verify_block(result,fn,ops,3)==1);
            else { nw_jit_set_mode(engine==4 ? NW_JIT_ON : NW_JIT_VERIFY); CHECK(cpu->nw_jit_try(ops[0])==1); }
            CHECK(nw_jit_verify_misses()==misses);
            if (engine==0 || engine>=3) {
                result.pc=cpu->pc(); result.fpscr=cpu->fpscr(); result.cr=cpu->cr().get(); result.msr=mmu.msr();
                result.srr0=cpu->srr0_; result.srr1=cpu->srr1_;
                for (unsigned r=0;r<32;++r) {
                    result.fpr[r]=cpu->fpr_dw(r); result.gpr[r]=cpu->gpr(r);
                    for (unsigned w=0;w<4;++w) result.vr[r][w]=cpu->vr(r).w[w];
                }
            }
            CHECK(result.fpscr==expected_fpscr);
            CHECK(result.cr==(rc ? (before.cr&~0x0f000000u)|((expected_fpscr>>4)&0x0f000000u) : before.cr));
            CHECK(result.pc==(except ? control.ip ? 0xfff00700u : 0x700u : pc+12));
            CHECK(result.msr==(except ? before.msr&~0x0204ef32u : before.msr));
            CHECK(result.srr0==(except ? pc+4 : before.srr0));
            CHECK(result.srr1==(except ? (before.msr&~0x783f0000u)|0x100000u : before.srr1));
            if (engine==0 || engine==2 || engine>=4) CHECK(cpu->exception_serial_==serial+unsigned(except));
            for (unsigned r=0;r<32;++r) {
                CHECK(result.fpr[r]==(r==7 ? before.fpr[8]^0x8000000000000000ULL :
                    r==fd ? suppressed ? before.fpr[r] : answer : r==5 && !except ? before.fpr[6] : before.fpr[r]));
                CHECK(result.gpr[r]==before.gpr[r]);
                for (unsigned w=0;w<4;++w) CHECK(result.vr[r][w]==before.vr[r][w]);
            }
            CHECK(fegetround()==fp_callback_round && fetestexcept(FE_ALL_EXCEPT)==fp_callback_flags);
            if (failed!=failures && failed<100) fprintf(stderr,"basic engine=%u prim=%u xo=%u alias=%u a=%016llx b=%016llx rn=%u en=%x fe=%x got=%016llx/%08x expected=%016llx/%08x\n",engine,precision,xo,alias,(unsigned long long)test.a,(unsigned long long)test.b,rn,control.enable,control.fe,(unsigned long long)result.fpr[fd],result.fpscr,(unsigned long long)answer,expected_fpscr);
        }
    }
    printf("P6 basic special: %u literal engine cases, including %u production entry/commit cases\n",cases,publication_cases);
    for (unsigned prim : {59u,63u}) for (unsigned rc : {0u,1u}) for (unsigned ip : {0u,0x40u}) {
        const uint32 op=(prim<<26)|(4u<<21)|(1u<<11)|(22u<<1)|rc, msr=0x2000u|ip;
        CHECK(!nw_jit_op_supported(op));
        mmu.reset(); mmu.set_msr(msr); cpu->pc()=pc; cpu->fpscr()=0x12345678u; cpu->cr().set(0xabcdef01u);
        const uint64 serial=cpu->exception_serial_, old=cpu->fpr_dw(4);
        instruction(cpu,op); CHECK(cpu->exception_serial_==serial+1);
        CHECK(cpu->pc()==(ip ? 0xfff00700u : 0x700u)); CHECK(cpu->srr0_==pc);
        CHECK(cpu->srr1_==((msr&~0x783f0000u)|0x80000u));
        CHECK(cpu->fpr_dw(4)==old && cpu->fpscr()==0x12345678u && cpu->cr().get()==0xabcdef01u);
    }
    fesetenv(&environment); nw_jit_invalidate_all(); nw_banks_set(NW_PA_RAM,0,0); munmap(ram,0x2000);
    nw_jit_set_host_fp_exception(powerpc_cpu::jit_host_fp_exception);
    nw_jit_set_host_chain(powerpc_cpu::jit_host_chain); nw_jit_set_mode(NW_JIT_ON);
    return failed ? 1 : 0;
}
/* Randomised comparison of the inline single-precision arithmetic against the out-of-line helpers: the same block
 * is compiled twice (NW_JIT_LEGACY_FP clear and set) and both run on identical state. Every FPR, FPSCR, CR, PC/MSR,
 * the host FP flags and the host rounding mode must match. Operands cover the cases the inline path takes and the
 * ones it must hand to the helper (zeros, subnormals, infinities, NaNs, doubles that are not singles, exponent
 * boundaries), under random FPSCR states including enabled exceptions, rounding modes and sticky bits. */
/* Byte/halfword stores (and the loads that fill the same table) through the data TLB against the always-translate path
 * (NW_JIT_LEGACY_SUBST). A long random sequence runs twice from identical memory; every step must leave the same bytes,
 * fault and updated base register. The sequence mixes loads (which fill entries as writable without a permission check),
 * stores to a read-only alias of the same RAM (must fault even after a load filled it), page-crossing halfwords, dropped
 * entries and word stores (which fill and must stay coherent with the byte path). */
int ppc_core_test_access::sub_store_sweep(powerpc_cpu *cpu)
{
    cpu->enable_guest_mmu(true); nw_jit_set_mode(NW_JIT_ON); nw_jit_set_host_chain(NULL);
    ppc32_mmu &mmu = ppc32_guest_mmu();
    const uint32 base = 0x10000000u, ro = 0x20000000u, code = base + 0x3000u;
    const unsigned span = 0x4000;
    void *const wanted = (void *)(VMBaseDiff+base);
    void *const ram = mmap(wanted,span + 0x2000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);	/* slack: a halfword on the last byte of the bank writes one byte past it */
    CHECK(ram == wanted); if (ram != wanted) return 1;
    nw_banks_set(NW_PA_RAM,base,span);
    nw_jit_set_code_pages(base,span,0,0);	/* per-page code bits: without them every page counts as code and no store may use the table */
    uint64 state = 0x2545f4914f6cdd1dULL;
    auto next = [&]() { state ^= state >> 12; state ^= state << 25; state ^= state >> 27; return state * 0x2545f4914f6cdd1dULL; };
    struct step { uint32 op, ra_val, rb_val, rs_val; int kind; };	/* kind: 0 op, 1 drop the page of ra, 2 flush */
    const unsigned steps = getenv("PPC_SUB_STEPS") ? unsigned(strtoul(getenv("PPC_SUB_STEPS"),NULL,10)) : 6000;
    std::vector<step> seq;
    for (unsigned i = 0; i < steps; ++i) {
        step st = {};
        const unsigned form = unsigned(next() % 30);
        const unsigned rs = 5, ra = 3, rb = 4;
        /* 0 stb 1 stbx 2 stbu 3 sth 4 sthx 5 sthu 6 lbz 7 lhz 8 lwz 9 stw 10 stbux 11 sthux 12 lha 13 lhzu 14 lhau
         * 15 lbzu 16 lbzx 17 lhzx 18 lhax 19 lbzux 20 lhzux 21 lhaux 22 lfd 23 lfdx 24 lfdu 25 lfdux 26 stfd 27 stfdx
         * 28 stfdu 29 stfdux */
        uint32 op = 0;
        const int16 disp = int16(next() % 0x40) - 0x10;
        switch (form) {
        case 12: op = (42u<<26)|(6u<<21)|(ra<<16)|uint16(disp); break;
        case 13: op = (41u<<26)|(6u<<21)|(ra<<16)|uint16(disp); break;
        case 14: op = (43u<<26)|(6u<<21)|(ra<<16)|uint16(disp); break;
        case 15: op = (35u<<26)|(6u<<21)|(ra<<16)|uint16(disp); break;
        case 16: op = (31u<<26)|(6u<<21)|(ra<<16)|(rb<<11)|(87u<<1); break;
        case 17: op = (31u<<26)|(6u<<21)|(ra<<16)|(rb<<11)|(279u<<1); break;
        case 18: op = (31u<<26)|(6u<<21)|(ra<<16)|(rb<<11)|(343u<<1); break;
        case 19: op = (31u<<26)|(6u<<21)|(ra<<16)|(rb<<11)|(119u<<1); break;
        case 20: op = (31u<<26)|(6u<<21)|(ra<<16)|(rb<<11)|(311u<<1); break;
        case 21: op = (31u<<26)|(6u<<21)|(ra<<16)|(rb<<11)|(375u<<1); break;
        case 22: op = (50u<<26)|(1u<<21)|(ra<<16)|uint16(disp); break;
        case 23: op = (31u<<26)|(1u<<21)|(ra<<16)|(rb<<11)|(599u<<1); break;
        case 24: op = (51u<<26)|(1u<<21)|(ra<<16)|uint16(disp); break;
        case 25: op = (31u<<26)|(1u<<21)|(ra<<16)|(rb<<11)|(631u<<1); break;
        case 26: op = (54u<<26)|(1u<<21)|(ra<<16)|uint16(disp); break;
        case 27: op = (31u<<26)|(1u<<21)|(ra<<16)|(rb<<11)|(727u<<1); break;
        case 28: op = (55u<<26)|(1u<<21)|(ra<<16)|uint16(disp); break;
        case 29: op = (31u<<26)|(1u<<21)|(ra<<16)|(rb<<11)|(759u<<1); break;
        case 0: op = (38u<<26)|(rs<<21)|(ra<<16)|uint16(disp); break;
        case 1: op = (31u<<26)|(rs<<21)|(ra<<16)|(rb<<11)|(215u<<1); break;
        case 2: op = (39u<<26)|(rs<<21)|(ra<<16)|uint16(disp); break;
        case 3: op = (44u<<26)|(rs<<21)|(ra<<16)|uint16(disp); break;
        case 4: op = (31u<<26)|(rs<<21)|(ra<<16)|(rb<<11)|(407u<<1); break;
        case 5: op = (45u<<26)|(rs<<21)|(ra<<16)|uint16(disp); break;
        case 6: op = (34u<<26)|(6u<<21)|(ra<<16)|uint16(disp); break;
        case 7: op = (40u<<26)|(6u<<21)|(ra<<16)|uint16(disp); break;
        case 8: op = (32u<<26)|(6u<<21)|(ra<<16)|uint16(disp); break;
        case 9: op = (36u<<26)|(rs<<21)|(ra<<16)|uint16(disp); break;
        case 10: op = (31u<<26)|(rs<<21)|(ra<<16)|(rb<<11)|(247u<<1); break;
        default: op = (31u<<26)|(rs<<21)|(ra<<16)|(rb<<11)|(439u<<1); break;
        }
        static const uint32 edge[] = {0, 1, 0xff8, 0xffc, 0xffe, 0xfff, 0x1000, 0x1ffe, 0x1fff, 0x2000, 0x2ffe, 0x2fff, 0x3000, 0x3ff8, 0x3ffa};
        uint32 off = next() % 6 ? (uint32(next()) % 0x3fe0) : edge[next() % (sizeof edge / sizeof edge[0])];
        const uint32 window = next() % 5 == 0 ? ro : base;
        /* A word store hits the table whenever its entry is writable, whether or not the page holds translated code
         * (the entry's history decides it, and the two runs fill differently); everything else here is store-proven,
         * so only stw stays off the executing page. */
        if (form == 9 && window == base && off + 4 > (code - base) - 8 && off < (code - base) + 0x1000u + 8) off &= 0x1fffu;
        const bool indexed = form == 1 || form == 4 || form == 10 || form == 11 || (form >= 16 && form <= 21) ||
                             form == 23 || form == 25 || form == 27 || form == 29;
        st.op = op; st.rs_val = uint32(next());
        if (indexed) { st.rb_val = uint32(next() & 0xff0); st.ra_val = window + off - st.rb_val; }
        else { st.rb_val = 0; st.ra_val = window + off - uint32(int32(disp)); }
        seq.push_back(st);
        const unsigned extra = unsigned(next() % 20);
        if (extra == 0) { step d = {}; d.kind = 1; d.ra_val = (next() % 2 ? ro : base) + ((uint32(next()) % 4) << 12); seq.push_back(d); }
        if (extra == 1) { step f = {}; f.kind = 2; seq.push_back(f); }
    }
    struct trace { uint32 fault, fault_ea, ra; uint64 hash; };
    std::vector<trace> out[2];
    const unsigned saved_legacy = nw_jit_legacy;
    unsigned fast_hits_before = 0, fast_hits_after = 0;
    for (unsigned v = 0; v < 2; ++v) {
        if (v || getenv("PPC_SUB_SELF")) nw_jit_legacy |= NW_JIT_LEGACY_SUBST | NW_JIT_LEGACY_MEM; else nw_jit_legacy &= ~(NW_JIT_LEGACY_SUBST | NW_JIT_LEGACY_MEM);
        mmu.reset(); mmu.set_msr(0x2000u | ppc32_mmu::MSR_DR);
        mmu.set_dbat(0, (base & 0xfffe0000u) | 3u, (base & 0xfffe0000u) | 2u);		/* read/write */
        mmu.set_dbat(1, (ro & 0xfffe0000u) | 3u, (base & 0xfffe0000u) | 1u);		/* read only alias of the same RAM */
        nw_jit_dtlb_flush_src(NW_JIT_DTLB_FL_BAT);
        uint64 rs2 = 0x9e3779b97f4a7c15ULL;
        for (unsigned i = 0; i < span; ++i) { rs2 ^= rs2 >> 12; rs2 ^= rs2 << 25; rs2 ^= rs2 >> 27; ((uint8 *)ram)[i] = uint8((rs2 * 0x2545f4914f6cdd1dULL) >> 56); }
        if (v == 0) fast_hits_before = unsigned(nw_jit_dtlb_hits());
        for (const step &st : seq) {
            if (st.kind == 1) { nw_jit_dtlb_drop_page(st.ra_val, NW_JIT_DTLB_FL_TLB); continue; }
            if (st.kind == 2) { nw_jit_dtlb_flush_src(NW_JIT_DTLB_FL_TLB); continue; }
            const uint32 ops[] = {st.op};
            nw_jit_invalidate_all_src(NW_JIT_FL_OTHER);
            nw_jit_fn fn = nw_jit_compile(ops,1,code,code,0,0);
            CHECK(fn != NULL); if (!fn) continue;
            nw_jit_cpu c = {};
            c.pc = code; c.msr = 0x2000u | ppc32_mmu::MSR_DR; c.host = cpu;
            c.gpr[3] = st.ra_val; c.gpr[4] = st.rb_val; c.gpr[5] = st.rs_val;
            c.fpr[1] = (uint64(st.rs_val) << 32) | uint64(~st.rs_val);
            mmu.set_msr(c.msr); cpu->pc() = code; cpu->last_fetch_pa_ = code;
            cpu->dec_ = 1000000; cpu->dec_tb_base_ = cpu->tb_host_ticks(); cpu->dec_pending_ = false; c.dec = cpu->dec_;
            cpu->cr().set(0); cpu->xer().set(0); cpu->lr() = cpu->ctr() = 0; cpu->spcflags().init();
            for (unsigned r = 0; r < 32; ++r) cpu->gpr(r) = c.gpr[r];
            nw_jit_cpu_bind(&c); nw_jit_tail_begin();
            fn(&c);
            trace t = {};
            t.fault = c.fault; t.fault_ea = c.fault ? c.fault_ea : 0; t.ra = c.gpr[3];
            uint64 h = 1469598103934665603ULL;
            for (unsigned i = 0; i < span; ++i) h = (h ^ ((uint8 *)ram)[i]) * 1099511628211ULL;
            t.hash = h ^ c.gpr[6] ^ (c.fpr[1] * 0x9e3779b97f4a7c15ULL);
            if (getenv("PPC_SUB_TRACE") && out[v].size() < 4) {
                unsigned sum = 0; for (unsigned i = 0; i < span; ++i) sum += ((uint8 *)ram)[i];
                fprintf(stderr, "sub-trace v=%u step=%zu op=%08x ra=%08x rb=%08x rs=%08x fault=%u sum=%u gpr6=%08x hash=%016llx\n", v, out[v].size(), st.op, st.ra_val, st.rb_val, st.rs_val, c.fault, sum, c.gpr[6], (unsigned long long)t.hash);
            }
            out[v].push_back(t);
        }
        if (v == 0) fast_hits_after = unsigned(nw_jit_dtlb_hits());
    }
    nw_jit_legacy = saved_legacy;
    CHECK(out[0].size() == out[1].size());
    unsigned diffs = 0, faults = 0;
    for (size_t i = 0; i < out[0].size() && i < out[1].size(); ++i) {
        const bool same = out[0][i].fault == out[1][i].fault && out[0][i].fault_ea == out[1][i].fault_ea &&
                          out[0][i].ra == out[1][i].ra && out[0][i].hash == out[1][i].hash;
        if (out[0][i].fault) ++faults;
        CHECK(same);
        if (!same && ++diffs <= 12) {
            const step *st = NULL; size_t k = 0;
            for (const step &q : seq) { if (q.kind == 0) { if (k == i) { st = &q; break; } ++k; } }
            fprintf(stderr, "sub-store step %zu op=%08x ra=%08x: fault %u/%u ea %08x/%08x ra' %08x/%08x hash %s\n", i, st ? st->op : 0, st ? st->ra_val : 0,
                out[0][i].fault, out[1][i].fault, out[0][i].fault_ea, out[1][i].fault_ea, out[0][i].ra, out[1][i].ra, out[0][i].hash == out[1][i].hash ? "same" : "DIFF");
        }
    }
    printf("sub-store sweep: %zu ops, %u faults, %u data-TLB hits in the fast run\n", out[0].size(), faults, fast_hits_after - fast_hits_before);
    CHECK(fast_hits_after - fast_hits_before > 100);
    mmu.reset(); nw_jit_set_code_pages(0,0,0,0); nw_banks_set(NW_PA_RAM,0,0); munmap(ram,span + 0x2000);
    return failed ? 1 : 0;
}

/* Throughput of tight guest loops through the real dispatch path (nw_jit_try + native chaining), in guest
 * instructions per microsecond. Reports; asserts only that each kernel produced its expected result. Kernels: a byte
 * copy (lbzu/stbu/bdnz), the ROM's BlockMove inner loop (lfd/stfd/bdnz), and a compare loop shaped like the MacBench
 * integer test (several not-taken conditional branches per iteration, one taken back-edge), a short string-store
 * routine and a sub-word store fill. NW_JIT_LEGACY in the environment still applies (set before start-up). */
int ppc_core_test_access::loop_bench(powerpc_cpu *cpu)
{
    cpu->enable_guest_mmu(true); nw_jit_set_mode(NW_JIT_ON); nw_jit_set_host_chain(powerpc_cpu::jit_host_chain);
    const uint32 base = 0x10000000u, sentinel = base + 0x7000u;
    const uint32 msr_run = 0x2000u | ppc32_mmu::MSR_IR | ppc32_mmu::MSR_DR;
    ppc32_guest_mmu().reset(); ppc32_guest_mmu().set_msr(msr_run);
    ppc32_guest_mmu().set_ibat(0, (base & 0xfffe0000u) | 3u, (base & 0xfffe0000u) | 2u);
    ppc32_guest_mmu().set_dbat(0, (base & 0xfffe0000u) | 3u, (base & 0xfffe0000u) | 2u);
    nw_jit_dtlb_flush_src(NW_JIT_DTLB_FL_BAT);
    const unsigned span = 0x8000;
    void *const wanted = (void *)(VMBaseDiff+base);
    void *const ram = mmap(wanted,span,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    CHECK(ram == wanted); if (ram != wanted) return 1;
    nw_banks_set(NW_PA_RAM,base,span);
    nw_jit_set_code_pages(base,span,0,0);
    struct kernel { const char *name; std::vector<uint32> code; unsigned insns_per_iter; unsigned iters; std::vector<uint32> code2; std::function<void()> init; uint32 expect_r3; };
    auto lbzu = [](unsigned rd, unsigned ra, int d) { return (35u<<26)|(rd<<21)|(ra<<16)|uint16(d); };
    auto stbu = [](unsigned rs, unsigned ra, int d) { return (39u<<26)|(rs<<21)|(ra<<16)|uint16(d); };
    auto lfd = [](unsigned fd, unsigned ra, int d) { return (50u<<26)|(fd<<21)|(ra<<16)|uint16(d); };
    auto stfd = [](unsigned fs, unsigned ra, int d) { return (54u<<26)|(fs<<21)|(ra<<16)|uint16(d); };
    auto addi = [](unsigned rd, unsigned ra, int i) { return (14u<<26)|(rd<<21)|(ra<<16)|uint16(i); };
    auto bdnz = [](int byte_disp) { return (16u<<26)|(16u<<21)|(uint16(byte_disp) & 0xfffcu); };
    auto bc = [](unsigned bo, unsigned bi, int byte_disp) { return (16u<<26)|(bo<<21)|(bi<<16)|(uint16(byte_disp) & 0xfffcu); };
    auto x31 = [](unsigned rd, unsigned ra, unsigned rb, unsigned xo) { return (31u<<26)|(rd<<21)|(ra<<16)|(rb<<11)|(xo<<1); };
    const uint32 BLR = 0x4e800020u;
    std::vector<kernel> kernels;
    kernels.push_back({"byte copy      ", {lbzu(5,3,1), stbu(5,4,1), bdnz(-8), BLR}, 3, 3000});
    kernels.push_back({"BlockMove loop ", {lfd(0,3,0), lfd(1,3,8), addi(3,3,16), stfd(0,4,0), stfd(1,4,8), addi(4,4,16), bdnz(-24), BLR}, 7, 200});
    /* addi r10,r10,1; extsh r4,r9; cmpw r4,r6; bge exit; lha r0,0(r8); extsh r5,r10; cmpw r5,r0; bge exit;
     * lbzx r4,r7,r4; lbzx r0,r7,r5; extsb r4,r4; extsb r0,r0; cmpw r4,r0; beq top; exit: blr */
    kernels.push_back({"compare loop   ", {addi(10,10,1), x31(9,4,0,922), x31(0,4,6,0), bc(4,0,0x2c), (42u<<26)|(0u<<21)|(8u<<16),
        x31(10,5,0,922), x31(0,5,0,0), bc(4,0,0x1c), x31(4,7,4,87), x31(0,7,5,87), x31(4,4,0,954), x31(0,0,0,954), x31(0,4,0,0), bc(12,2,-0x34), BLR}, 14, 2047});
    /* Calls and returns: a leaf one page away and in the same page, and a call through CTR (bctrl) with a conditional loop. */
    {
        const uint32 func_far = base + 0x5000u, code_at = base + 0x6000u;
        auto bl = [](int byte_disp) { return (18u<<26)|(uint32(byte_disp) & 0x3fffffcu)|1u; };
        const uint32 MFLR = x31(31,8,0,339), MTLR = x31(31,8,0,467);
        kernel same = {"call, same page ", {MFLR, bl(20), bdnz(-4), MTLR, BLR, 0x60000000u, addi(3,3,1), BLR}, 4, 1500, {}, nullptr, 1500};
        kernel far = {"call, other page", {MFLR, bl(int(func_far) - int(code_at) - 4), bdnz(-4), MTLR, BLR}, 4, 1500, {addi(3,3,1), BLR}, nullptr, 1500};
        /* mtctr r6; bctrl; addic. r7,r7,-1; bne top; with the leaf in the other page */
        kernel ctr = {"bctrl + blr     ", {MFLR, x31(6,9,0,467), (19u<<26)|(20u<<21)|(528u<<1)|1u, (13u<<26)|(7u<<21)|(7u<<16)|uint16(-1), bc(4,2,-12), MTLR, BLR}, 6, 1500, {addi(3,3,1), BLR}, nullptr, 1500};
        ctr.init = [&]() { cpu->gpr(6) = func_far; cpu->gpr(7) = 1500; };
        kernels.push_back(same); kernels.push_back(far); kernels.push_back(ctr);
        /* conditional returns: beqlr not taken then taken, in a leaf one page away */
        kernel cret = {"conditional blr ", {MFLR, bl(int(func_far) - int(code_at) - 4), bdnz(-4), MTLR, BLR}, 5, 1500, {addi(3,3,1), (19u<<26)|(12u<<21)|(2u<<16)|(16u<<1), BLR}, nullptr, 1500};
        kernels.push_back(cret);
    }
    for (kernel &k : kernels) {
        const uint32 code = base + 0x6000u;
        for (size_t i = 0; i < k.code2.size(); ++i) vm_write_memory_4(base + 0x5000u + uint32(i*4), k.code2[i]);
        if (!k.code2.empty()) nw_jit_itlb_fill(base + 0x5000u, base + 0x5000u);
        for (size_t i = 0; i < k.code.size(); ++i) vm_write_memory_4(code + uint32(i*4), k.code[i]);
        for (unsigned i = 0; i < 0x4000; ++i) ((uint8 *)ram)[i] = 0;
        for (unsigned i = 0; i < 4096; ++i) ((uint8 *)ram)[i] = uint8(i * 7);	/* copy source */
        ((uint8 *)ram)[0x2000] = 0x07; ((uint8 *)ram)[0x2001] = 0xff;		/* lha r0,0(r8) = 0x07ff: the loop runs 2047 times */
        double best = 1e30; uint64 insns = 0;
        for (unsigned rep = 0; rep < 5; ++rep) {
            nw_jit_invalidate_all(); nw_jit_itlb_flush(); nw_jit_itlb_fill(code, code);	/* the fetch path fills this in the emulator */
            if (!k.code2.empty()) nw_jit_itlb_fill(base + 0x5000u, base + 0x5000u);
            cpu->pc() = code; cpu->lr() = sentinel; cpu->ctr() = k.iters;
            for (unsigned r = 0; r < 32; ++r) cpu->gpr(r) = 0;
            cpu->gpr(3) = base - 1; cpu->gpr(4) = base + 0x1000u - 1;
            if (k.code.size() == 8) { cpu->gpr(3) = base; cpu->gpr(4) = base + 0x1000u; }
            if (k.code.size() == 15) { cpu->gpr(8) = base + 0x2000u; cpu->gpr(7) = base + 0x3000u; cpu->gpr(6) = 1; cpu->gpr(9) = 0; cpu->gpr(10) = 0;
                cpu->gpr(4) = 0; /* unused: lbzx r4,r7,r4 reads base+0x3000 */ }
            if (k.init) k.init();
            cpu->cr().set(0); cpu->xer().set(0); cpu->spcflags().init();
            cpu->dec_ = 0x7fffffffu; cpu->dec_tb_base_ = cpu->tb_host_ticks(); cpu->dec_pending_ = false;
            ppc32_guest_mmu().set_msr(msr_run);
            const unsigned batches = unsigned(4000000ull / (uint64(k.iters) * k.insns_per_iter)) + 1;
            struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
            uint64 calls = 0;
            for (unsigned batch = 0; batch < batches; ++batch) {
                cpu->pc() = code; cpu->lr() = sentinel; cpu->ctr() = k.iters;
                cpu->gpr(3) = k.code.size() == 8 ? base : base - 1; cpu->gpr(4) = k.code.size() == 8 ? base + 0x1000u : base + 0x1000u - 1;
                if (k.code.size() == 15) { cpu->gpr(4) = 0; cpu->gpr(10) = 0; }
                if (k.init) k.init();
                if (k.expect_r3) cpu->gpr(3) = 0;
                while (cpu->pc() != sentinel && calls < 20000000ull) {
                    cpu->last_fetch_pa_ = cpu->pc();
                    const uint32 op = vm_read_memory_4(cpu->pc());
                    if (getenv("PPC_LOOP_TRACE") && calls < 12) fprintf(stderr, "  [%s] call %llu pc=%08x op=%08x lr=%08x ctr=%x r3=%x\n", k.name, (unsigned long long)calls, cpu->pc(), op, cpu->lr(), unsigned(cpu->ctr()), cpu->gpr(3));
                    if (!cpu->nw_jit_try(op)) { CHECK(false); fprintf(stderr, "loop bench: nw_jit_try declined pc=%08x op=%08x supported=%d mode=%d msr=%08x pa=%08x\n", cpu->pc(), op, nw_jit_op_supported(op), nw_jit_mode(), ppc32_guest_mmu().msr(), cpu->last_fetch_pa_); break; }
                    ++calls;
                }
            }
            clock_gettime(CLOCK_MONOTONIC, &t1);
            const double ns = (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
            insns = uint64(batches) * k.iters * k.insns_per_iter;
            if (ns / insns < best) best = ns / insns;
            CHECK(cpu->pc() == sentinel);
            if (k.code.size() == 4) CHECK(vm_read_memory_1(base + 0x1000u + 10) == uint8(10 * 7));
            if (k.code.size() == 15) CHECK(cpu->gpr(10) == k.iters);
            if (k.expect_r3) CHECK(cpu->gpr(3) == k.expect_r3);
            if (calls >= 20000000ull) { CHECK(false); fprintf(stderr, "loop bench: %s did not finish: pc=%08x ctr=%u r3=%08x r10=%u\n", k.name, cpu->pc(), unsigned(cpu->ctr()), cpu->gpr(3), cpu->gpr(10)); break; }
        }
        { uint64 lf = 0, lm = 0; nw_jit_link_stats(&lf, &lm); printf("loop bench %s: %.1f guest insns/us (%.2f ns per insn)  [links made %llu, fast hops %llu, helper hops %llu, hop stops cap %llu nochain %llu pcmis %llu itlb %llu aline %llu cmiss %llu]\n", k.name, 1000.0 / best, best, (unsigned long long)lm, (unsigned long long)lf, (unsigned long long)nw_jit_chain_hops(), (unsigned long long)nw_jit_hop_stop_count(0), (unsigned long long)nw_jit_hop_stop_count(1), (unsigned long long)nw_jit_hop_stop_count(2), (unsigned long long)nw_jit_hop_stop_count(3), (unsigned long long)nw_jit_hop_stop_count(4), (unsigned long long)nw_jit_hop_stop_count(5)); }
    }
    nw_jit_invalidate_all(); nw_jit_set_code_pages(0,0,0,0); nw_banks_set(NW_PA_RAM,0,0); munmap(ram,span);
    return failed ? 1 : 0;
}


/* Block exits that leave the page (a call to another page through bl or through CTR, and the return) are cached across
 * pages, so the cache must not outlive a change of the instruction translation. The same effective address is mapped to
 * a leaf in one physical page, called until every exit is linked, then remapped (an IBAT change, as mtspr does it:
 * set_ibat and an instruction TLB flush) to a different leaf; the next calls must run the new one. Same for a
 * segment-register change and tlbie, which go through the same instruction-TLB events. */
int ppc_core_test_access::ibtc_remap(powerpc_cpu *cpu)
{
    cpu->enable_guest_mmu(true); nw_jit_set_mode(NW_JIT_ON); nw_jit_set_host_chain(powerpc_cpu::jit_host_chain);
    const uint32 base = 0x10000000u, sentinel = base + 0x7000u, code = base + 0x6000u, win = base + 0x400000u;
    const uint32 pa1 = base + 0x20000u, pa2 = base + 0x40000u;
    const unsigned span = 0x60000;
    const uint32 msr_run = 0x2000u | ppc32_mmu::MSR_IR | ppc32_mmu::MSR_DR;
    ppc32_mmu &mmu = ppc32_guest_mmu();
    mmu.reset(); mmu.set_msr(msr_run);
    void *const wanted = (void *)(VMBaseDiff+base);
    void *const ram = mmap(wanted,span,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    CHECK(ram == wanted); if (ram != wanted) return 1;
    nw_banks_set(NW_PA_RAM,base,span);
    nw_jit_set_code_pages(base,span,0,0);
    auto bat = [&](unsigned i, uint32 ea, uint32 pa) { mmu.set_ibat(i, (ea & 0xfffe0000u) | 3u, (pa & 0xfffe0000u) | 2u); };
    mmu.set_ibat(0, (base & 0xfffe0000u) | 3u, (base & 0xfffe0000u) | 2u);
    mmu.set_dbat(0, (base & 0xfffe0000u) | 3u, (base & 0xfffe0000u) | 2u);
    auto x31 = [](unsigned rd, unsigned ra, unsigned rb, unsigned xo) { return (31u<<26)|(rd<<21)|(ra<<16)|(rb<<11)|(xo<<1); };
    auto addi = [](unsigned rd, unsigned ra, int i) { return (14u<<26)|(rd<<21)|(ra<<16)|uint16(i); };
    auto addis = [](unsigned rd, unsigned ra, int i) { return (15u<<26)|(rd<<21)|(ra<<16)|uint16(i); };
    const uint32 BLR = 0x4e800020u;
    /* mflr r31; bl win; bl win; lis r12,hi(win); mtctr r12; bctrl; bctrl; mtlr r31; blr: two direct calls to the other page,
     * two through CTR; after the first call has been resolved by the helper the rest are the cached exits. */
    const uint32 BL = (18u<<26)|1u, BCTRL = (19u<<26)|(20u<<21)|(528u<<1)|1u;
    const uint32 prog[] = { x31(31,8,0,339), BL | (uint32(win - (code + 4)) & 0x3fffffcu), BL | (uint32(win - (code + 8)) & 0x3fffffcu),
                            addis(12,0,int16(win >> 16)), x31(12,9,0,467), BCTRL, BCTRL, x31(31,8,0,467), BLR };
    for (size_t i = 0; i < sizeof prog / sizeof prog[0]; ++i) vm_write_memory_4(code + uint32(i * 4), prog[i]);
    vm_write_memory_4(pa1, addi(3,3,1)); vm_write_memory_4(pa1 + 4, BLR);
    vm_write_memory_4(pa2, addi(3,3,100)); vm_write_memory_4(pa2 + 4, BLR);
    auto run = [&]() {
        cpu->pc() = code; cpu->lr() = sentinel; cpu->gpr(3) = 0; cpu->ctr() = 0;
        cpu->cr().set(0); cpu->xer().set(0); cpu->spcflags().init();
        cpu->dec_ = 0x7fffffffu; cpu->dec_tb_base_ = cpu->tb_host_ticks(); cpu->dec_pending_ = false;
        mmu.set_msr(msr_run);
        uint64 calls = 0;
        while (cpu->pc() != sentinel && calls < 100000) {
            uint32 ipa = 0;
            if (!nw_jit_itlb_lookup(cpu->pc(), &ipa)) {		/* what the fetch path does on a miss */
                ppc32_xlate_result r = mmu.translate(cpu->pc(), PPC32_XLATE_IR, 4);
                CHECK(r.ok); if (!r.ok) break;
                ipa = r.pa; nw_jit_itlb_fill(cpu->pc(), ipa);
            }
            cpu->last_fetch_pa_ = ipa;
            const uint32 op = vm_read_memory_4(ipa);
            if (!cpu->nw_jit_try(op)) { CHECK(false); break; }
            ++calls;
        }
        CHECK(cpu->pc() == sentinel);
        return cpu->gpr(3);
    };
    /* the effective address moves from one physical page to another, by a different instruction-translation event each round */
    for (int round = 0; round < 10; ++round) {
        const uint32 from = round & 1 ? pa2 : pa1, to = round & 1 ? pa1 : pa2;
        const uint32 inc_from = from == pa1 ? 1u : 100u, inc_to = to == pa1 ? 1u : 100u;
        nw_jit_invalidate_all(); nw_jit_itlb_flush();
        bat(1, win, from);
        for (int i = 0; i < 8; ++i) CHECK(run() == 4 * inc_from);
        uint64 lf = 0, lm = 0; nw_jit_link_stats(&lf, &lm);
        CHECK(lf > 0);
        bat(1, win, to);					/* the mapping changes ... */
        switch (round / 2) {					/* ... and the emulator reports it the way that instruction does */
        case 0: nw_jit_itlb_flush(); break;			/* mtspr IBATxL */
        case 1: nw_jit_dtlb_drop_page(win, NW_JIT_DTLB_FL_TLB); break;	/* tlbie */
        case 2: nw_jit_dtlb_flush_src(NW_JIT_DTLB_FL_TLB); break;	/* tlbia */
        case 3: nw_jit_dtlb_flush_src(NW_JIT_DTLB_FL_SDR1); break;	/* mtsdr1 */
        default: nw_jit_mtsr_note(1, 0, 0x100); break;		/* mtsr with a different VSID */
        }
        for (int i = 0; i < 4; ++i) CHECK(run() == 4 * inc_to);
    }
    /* A change of MSR[IR|DR|PR] only (the exits carry the MSR bits they were resolved under): the exits stay valid. */
    {
        const uint64 fills_before = nw_jit_ibtc_fills();
        bat(1, win, pa1); nw_jit_invalidate_all(); nw_jit_itlb_flush();
        for (int i = 0; i < 4; ++i) CHECK(run() == 4);
        const uint64 fills = nw_jit_ibtc_fills();
        for (int i = 0; i < 4; ++i) { nw_jit_itlb_note_msr(msr_run, msr_run ^ ppc32_mmu::MSR_PR); nw_jit_itlb_note_msr(msr_run ^ ppc32_mmu::MSR_PR, msr_run); mmu.set_msr(msr_run); CHECK(run() == 4); }
        CHECK(nw_jit_ibtc_fills() == fills);
        (void)fills_before;
    }
    /* a segment-register change: the translation of the window goes through segment 1 with a different VSID mapped by BATs off */
    nw_jit_invalidate_all(); nw_jit_set_code_pages(0,0,0,0); mmu.reset(); nw_banks_set(NW_PA_RAM,0,0); munmap(ram,span);
    return failed ? 1 : 0;
}

/* Direct block links and inline conditional/CTR branches against the all-through-the-helper path (NW_JIT_LEGACY_LINK).
 * Random structured programs (straight code, forward skips on every BO form, counted CTR loops, CR-driven loops, calls
 * and returns, stores into the program's own page followed by icbi) run to completion four times: cold and warm
 * (links already made) in each mode, from the same initial registers and memory. Registers, CR, XER, LR, CTR and the
 * data page must be identical. Some programs straddle a page boundary so exits cross pages. */
int ppc_core_test_access::link_sweep(powerpc_cpu *cpu)
{
    cpu->enable_guest_mmu(true); nw_jit_set_mode(NW_JIT_ON); nw_jit_set_host_chain(powerpc_cpu::jit_host_chain);
    const unsigned span = 0x20000;
    const uint32 base = 0x10000000u, sentinel = base + 0x7000u, data = base + 0x1000u, code_base = base + 0x2000u;
    const uint32 msr_run = 0x2000u | ppc32_mmu::MSR_IR | ppc32_mmu::MSR_DR;
    ppc32_mmu &mmu = ppc32_guest_mmu();
    mmu.reset(); mmu.set_msr(msr_run);
    mmu.set_ibat(0, (base & 0xfffe0000u) | 3u, (base & 0xfffe0000u) | 2u);
    mmu.set_dbat(0, (base & 0xfffe0000u) | 3u, (base & 0xfffe0000u) | 2u);
    nw_jit_dtlb_flush_src(NW_JIT_DTLB_FL_BAT);
    void *const wanted = (void *)(VMBaseDiff+base);
    void *const ram = mmap(wanted,span,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    CHECK(ram == wanted); if (ram != wanted) return 1;
    nw_banks_set(NW_PA_RAM,base,span);
    nw_jit_set_code_pages(base,span,0,0);
    uint64 state = 0x853c49e6748fea9bULL;
    auto next = [&]() { state ^= state >> 12; state ^= state << 25; state ^= state >> 27; return state * 0x2545f4914f6cdd1dULL; };
    auto rnd = [&](unsigned n) { return unsigned(next() % n); };
    auto x31 = [](unsigned rd, unsigned ra, unsigned rb, unsigned xo) { return (31u<<26)|(rd<<21)|(ra<<16)|(rb<<11)|(xo<<1); };
    auto addi = [](unsigned rd, unsigned ra, int i) { return (14u<<26)|(rd<<21)|(ra<<16)|uint16(i); };
    auto addis = [](unsigned rd, unsigned ra, int i) { return (15u<<26)|(rd<<21)|(ra<<16)|uint16(i); };
    auto ori = [](unsigned ra, unsigned rs, unsigned u) { return (24u<<26)|(rs<<21)|(ra<<16)|(u & 0xffffu); };
    auto bc = [](unsigned bo, unsigned bi, int byte_disp) { return (16u<<26)|(bo<<21)|(bi<<16)|(uint16(byte_disp) & 0xfffcu); };
    const unsigned iterations = getenv("PPC_LINK_PROGRAMS") ? unsigned(strtoul(getenv("PPC_LINK_PROGRAMS"),NULL,10)) : 1500;
    unsigned ran = 0, mismatches = 0, total_insns = 0, smc_loops = 0, smc_forward = 0;
    struct pstate { uint32 gpr[32], cr, xer, lr, ctr, pc, srr0; uint64 hash; bool finished; };
    auto check = [&](const std::vector<uint32> &w, uint32 start_off, unsigned prog, const std::function<void()> &init) {
        const uint32 pc0 = code_base + start_off;
    pstate res[4];
    struct tr { uint32 pc, gpr[10], cr, ctr, lr; };
    std::vector<tr> traces[4];
    const bool only_this = !getenv("PPC_LINK_ONLY") || prog == unsigned(atoi(getenv("PPC_LINK_ONLY")));
    if (!only_this) return;
    for (unsigned variant = 0; variant < 4; ++variant) {
        const bool legacy = variant >= 2;
        const bool warm = variant & 1;
        if (!warm) {
            if (legacy) nw_jit_legacy |= NW_JIT_LEGACY_LINK; else nw_jit_legacy &= ~NW_JIT_LEGACY_LINK;
            nw_jit_invalidate_all(); nw_jit_itlb_flush();
            nw_jit_itlb_fill(code_base, code_base); nw_jit_itlb_fill(code_base + 0x1000u, code_base + 0x1000u);
        }
        uint64 s2 = 0x9e3779b97f4a7c15ULL ^ (uint64(prog) * 0x100000001b3ULL);
        auto n2 = [&]() { s2 ^= s2 >> 12; s2 ^= s2 << 25; s2 ^= s2 >> 27; return s2 * 0x2545f4914f6cdd1dULL; };
        /* program text (restored when a previous run's self-modifying store changed it) */
        bool changed = false;
        for (size_t i = 0; i < w.size(); ++i) if (vm_read_memory_4(pc0 + uint32(i * 4)) != w[i]) { changed = true; vm_write_memory_4(pc0 + uint32(i * 4), w[i]); }
        if (changed && warm) nw_jit_invalidate_all();
        for (unsigned i = 0; i < 0x1000; ++i) ((uint8 *)ram)[0x1000 + i] = uint8(n2() >> 56);
        for (unsigned r = 0; r < 32; ++r) cpu->gpr(r) = uint32(n2()) & 0xff;
        cpu->gpr(10) = data;
        if (init) init();
        cpu->cr().set(uint32(n2())); cpu->xer().set(uint32(n2()) & 0xe0000000u);
        cpu->lr() = sentinel; cpu->ctr() = uint32(n2()) & 0xf;
        cpu->pc() = pc0; cpu->spcflags().init();
        cpu->dec_ = 0x7fffffffu; cpu->dec_tb_base_ = cpu->tb_host_ticks(); cpu->dec_pending_ = false;
        mmu.set_msr(msr_run);
        uint64 calls_n = 0;
        uint32 prev_pc = 0;
        while (cpu->pc() != sentinel && calls_n < 300000) {
            if (cpu->pc() < base || cpu->pc() >= base + span) { if (getenv("PPC_LINK_DUMP") || !calls_n) fprintf(stderr, "  pc left the program: %08x after %08x (variant %u, call %llu)\n", cpu->pc(), prev_pc, variant, (unsigned long long)calls_n); break; }
            prev_pc = cpu->pc();
            if (getenv("PPC_LINK_TRACE")) { tr t = {cpu->pc(), {0}, cpu->cr().get(), cpu->ctr(), cpu->lr()}; for (unsigned r = 0; r < 10; ++r) t.gpr[r] = cpu->gpr(r + 3); traces[variant].push_back(t); }
            cpu->last_fetch_pa_ = cpu->pc();
            const uint32 op = vm_read_memory_4(cpu->pc());
            if (!cpu->nw_jit_try(op)) {
                /* The translator declines an access it cannot prove mapped (a store into the executing page, a pointer at
                 * the edge of memory); the emulator interprets that instruction, taking any DSI. */
                instruction(cpu, op);
            }
            ++calls_n;
        }
        pstate &st = res[variant];
        st.finished = cpu->pc() == sentinel;
        for (unsigned r = 0; r < 32; ++r) st.gpr[r] = cpu->gpr(r);
        st.cr = cpu->cr().get(); st.xer = cpu->xer().get(); st.lr = cpu->lr(); st.ctr = cpu->ctr(); st.pc = cpu->pc(); st.srr0 = cpu->srr0_;
        uint64 h = 1469598103934665603ULL;
        for (unsigned i = 0x1000; i < 0x2000; ++i) h = (h ^ ((uint8 *)ram)[i]) * 1099511628211ULL;
        st.hash = h;
        total_insns += unsigned(calls_n);
    }
    ++ran;
    bool ok = true;
    for (unsigned v = 1; v < 4; ++v) {
        bool same = res[v].finished == res[0].finished && !memcmp(res[v].gpr, res[0].gpr, sizeof res[0].gpr) && res[v].cr == res[0].cr &&
                    res[v].xer == res[0].xer && res[v].lr == res[0].lr && res[v].ctr == res[0].ctr && res[v].hash == res[0].hash &&
                    res[v].pc == res[0].pc && res[v].srr0 == res[0].srr0;
        ok &= same;
    }
    CHECK(ok);
    if (!ok && ++mismatches <= 4) {
        fprintf(stderr, "link sweep: program %u (%zu words, start +%x) differs or did not finish (finished %d %d %d %d)\n", prog, w.size(), start_off,
            res[0].finished, res[1].finished, res[2].finished, res[3].finished);
        for (unsigned v = 1; v < 4; ++v) {
            for (unsigned r = 0; r < 32; ++r) if (res[v].gpr[r] != res[0].gpr[r]) fprintf(stderr, "  v%u r%u %08x vs %08x\n", v, r, res[v].gpr[r], res[0].gpr[r]);
            if (res[v].cr != res[0].cr) fprintf(stderr, "  v%u cr %08x vs %08x\n", v, res[v].cr, res[0].cr);
            if (res[v].ctr != res[0].ctr) fprintf(stderr, "  v%u ctr %08x vs %08x\n", v, res[v].ctr, res[0].ctr);
            if (res[v].lr != res[0].lr) fprintf(stderr, "  v%u lr %08x vs %08x\n", v, res[v].lr, res[0].lr);
            if (res[v].xer != res[0].xer) fprintf(stderr, "  v%u xer %08x vs %08x\n", v, res[v].xer, res[0].xer);
            if (res[v].hash != res[0].hash) fprintf(stderr, "  v%u memory differs\n", v);
        }
        if (getenv("PPC_LINK_TRACE")) {
            for (unsigned v = 1; v < 4; ++v) {
                size_t k = 0; while (k < traces[0].size() && k < traces[v].size() && !memcmp(&traces[0][k], &traces[v][k], sizeof(tr))) ++k;
                fprintf(stderr, "  trace v%u first differs at call %zu of %zu/%zu\n", v, k, traces[0].size(), traces[v].size());
                for (size_t j = (k > 3 ? k - 3 : 0); j < k + 2; ++j) {
                    if (j < traces[0].size()) fprintf(stderr, "    v0[%zu] pc=%08x r3..r12=%x %x %x %x %x %x %x %x %x %x cr=%08x ctr=%x lr=%08x\n", j, traces[0][j].pc, traces[0][j].gpr[0], traces[0][j].gpr[1], traces[0][j].gpr[2], traces[0][j].gpr[3], traces[0][j].gpr[4], traces[0][j].gpr[5], traces[0][j].gpr[6], traces[0][j].gpr[7], traces[0][j].gpr[8], traces[0][j].gpr[9], traces[0][j].cr, traces[0][j].ctr, traces[0][j].lr);
                    if (j < traces[v].size()) fprintf(stderr, "    v%u[%zu] pc=%08x r3..r12=%x %x %x %x %x %x %x %x %x %x cr=%08x ctr=%x lr=%08x\n", v, j, traces[v][j].pc, traces[v][j].gpr[0], traces[v][j].gpr[1], traces[v][j].gpr[2], traces[v][j].gpr[3], traces[v][j].gpr[4], traces[v][j].gpr[5], traces[v][j].gpr[6], traces[v][j].gpr[7], traces[v][j].gpr[8], traces[v][j].gpr[9], traces[v][j].cr, traces[v][j].ctr, traces[v][j].lr);
                }
            }
        }
        if (getenv("PPC_LINK_DUMP")) { for (size_t i = 0; i < w.size(); ++i) fprintf(stderr, "    %08x: %08x\n", unsigned(code_base + start_off + i * 4), w[i]); }
    }
    };
    for (unsigned prog = 0; prog < iterations; ++prog) {
        std::vector<uint32> w;
        std::vector<std::pair<size_t,size_t> > calls;	/* bl placeholder index, unused */
        std::vector<size_t> call_sites, computed_sites;
        struct smc { size_t store_at; };
        std::vector<size_t> smc_sites, smc_loop;	/* site, innermost enclosing loop start (or npos) */
        std::vector<size_t> loop_stack;
        auto emit = [&](uint32 op) { w.push_back(op); };
        auto data_reg = [&]() { return 3u + rnd(7); };	/* r3..r9 */
        auto alu = [&]() {
            const unsigned rd = data_reg(), ra = 3u + rnd(7), rb = 3u + rnd(7);
            switch (rnd(12)) {
            case 0: emit(addi(rd, ra, int(rnd(200)) - 100)); break;
            case 1: emit(x31(rd, ra, rb, 266)); break;
            case 2: emit(x31(rd, ra, rb, 40)); break;
            case 3: emit(x31(ra, rd, rb, 444)); break;
            case 4: emit(x31(ra, rd, rb, 28)); break;
            case 5: emit(x31(ra, rd, rb, 316)); break;
            case 6: emit((21u<<26)|(rd<<21)|(ra<<16)|(rnd(32)<<11)|(rnd(32)<<6)|(31u<<1)); break;
            case 7: emit(x31(rd, ra, rb, 235)); break;
            case 8: emit((13u<<26)|(rd<<21)|(ra<<16)|uint16(int(rnd(50)) - 25)); break;	/* addic. */
            case 9: emit(x31(rd, ra, 0, 104)); break;				/* neg */
            case 10: emit(ori(ra, rd, rnd(0x10000))); break;
            default: emit(x31(rd, ra, rb, 24)); break;				/* slw */
            }
        };
        auto mem = [&]() {
            const unsigned r = data_reg();
            if (rnd(80) == 0) { emit((32u<<26)|(r<<21)|0x20u); return; }	/* lwz r,0x20(0): unmapped, takes a DSI */
            const unsigned form = rnd(6);
            const unsigned d = (rnd(0x1f0) & ~3u);
            static const unsigned prims[] = {32, 36, 34, 38, 40, 44};
            const unsigned size = form == 0 || form == 1 ? 4 : form == 2 || form == 3 ? 1 : 2;
            emit((prims[form]<<26)|(r<<21)|(10u<<16)|(d & ~(size - 1)));
        };
        auto cmp = [&](unsigned k) {
            const unsigned ra = 3u + rnd(7), rb = 3u + rnd(7);
            switch (rnd(4)) {
            case 0: emit((31u<<26)|(k<<23)|(ra<<16)|(rb<<11)); break;
            case 1: emit((11u<<26)|(k<<23)|(ra<<16)|uint16(int(rnd(100)) - 50)); break;
            case 2: emit((31u<<26)|(k<<23)|(ra<<16)|(rb<<11)|(32u<<1)); break;
            default: emit((10u<<26)|(k<<23)|(ra<<16)|rnd(100)); break;
            }
        };
        auto emit_smc = [&]() { smc_sites.push_back(w.size()); smc_loop.push_back(loop_stack.empty() ? size_t(-1) : loop_stack.back()); for (int i = 0; i < 6; ++i) emit(0x60000000u); };
        std::function<void(int, bool)> body, segment;
        auto skip = [&](int depth, bool in_ctr) {
            const unsigned k = rnd(8);
            cmp(k);
            const unsigned n = 1 + rnd(3);
            static const unsigned bos[] = {12, 4, 13, 5, 20, 21};
            static const unsigned bos_ctr[] = {16, 18, 8, 10, 0, 2, 17, 25};
            const unsigned bo = !in_ctr && rnd(3) == 0 ? bos_ctr[rnd(8)] : bos[rnd(6)];
            if (!in_ctr && (bo & 4) == 0) {	/* a CTR form: give CTR a small value first */
                emit(addi(11, 0, 1 + rnd(4))); emit(x31(11, 9, 0, 467));
            }
            emit(bc(bo, 4 * k + rnd(4), int(n + 1) * 4));
            for (unsigned i = 0; i < n; ++i) { if (rnd(4) == 0) mem(); else alu(); }
            (void)depth;
        };
        body = [&](int depth, bool in_ctr) {
            const unsigned n = 1 + rnd(4);
            for (unsigned i = 0; i < n; ++i) {
                switch (rnd(8)) {
                case 0: case 1: alu(); break;
                case 2: mem(); break;
                case 3: skip(depth, in_ctr); break;
                case 4: if (depth < 2) segment(depth + 1, in_ctr); else alu(); break;
                case 5: if (!loop_stack.empty() && rnd(2)) emit_smc(); else alu(); break;
                default: alu(); break;
                }
            }
        };
        segment = [&](int depth, bool in_ctr) {
            switch (rnd(8)) {
            case 0: case 1: {					/* CTR loop (bdnz and its other forms) */
                if (in_ctr) { body(depth, in_ctr); break; }
                emit(addi(11, 0, 1 + rnd(6))); emit(x31(11, 9, 0, 467));
                const size_t start = w.size();
                loop_stack.push_back(start);
                body(depth, true);
                loop_stack.pop_back();
                static const unsigned loop_bo[] = {16, 17, 18 + 100};	/* bdnz, bdnz (hint), placeholder */
                const unsigned bo = loop_bo[rnd(2)];
                emit(bc(bo, 0, -int(w.size() - start) * 4));
                break;
            }
            case 2: case 3: {					/* CR loop */
                const unsigned counter = 24u + unsigned(depth);
                emit(addi(counter, 0, 1 + rnd(5)));
                const size_t start = w.size();
                loop_stack.push_back(start);
                body(depth, in_ctr);
                loop_stack.pop_back();
                emit(addi(counter, counter, -1));
                const unsigned k = rnd(8);
                emit((11u<<26)|(k<<23)|(counter<<16)|0);	/* cmpwi k,counter,0 */
                const size_t b = w.size();
                /* bgt (BO 12, bit gt) loops while counter > 0 */
                emit(bc(12, 4 * k + 1, -int(b - start) * 4));
                break;
            }
            case 4: {						/* forward unconditional jump over dead code */
                const unsigned n = 1 + rnd(3);
                emit((18u<<26)|((n + 1) * 4));
                for (unsigned i = 0; i < n; ++i) alu();
                break;
            }
            case 5:						/* call a leaf, directly or through CTR */
                if (!in_ctr && rnd(3) == 0) {			/* (mtctr would end an enclosing CTR loop early) */
                    computed_sites.push_back(w.size());
                    emit((18u<<26)|4u|1u);				/* bl .+4: LR = the next word */
                    emit(x31(12, 8, 0, 339));				/* mflr r12 */
                    emit(0);						/* addi r12,r12,delta (patched) */
                    emit(x31(12, 9, 0, 467));				/* mtctr r12 */
                    emit((19u<<26)|(20u<<21)|(528u<<1)|1u);		/* bctrl */
                } else {
                    call_sites.push_back(w.size()); emit(0);
                }
                break;
            case 6:						/* store into the program's own page, then icbi */
                emit_smc(); break;
            default: skip(depth, in_ctr); break;
            }
        };
        emit(x31(31, 8, 0, 339));						/* mflr r31 */
        emit(addis(10, 0, int16(data >> 16))); emit(ori(10, 10, data & 0xffff));
        emit(addis(13, 0, int16(code_base >> 16))); emit(ori(13, 13, code_base & 0xffff));
        const unsigned nseg = 3 + rnd(6);
        for (unsigned i = 0; i < nseg; ++i) segment(0, false);
        emit(x31(31, 8, 0, 467));						/* mtlr r31 */
        emit(0x4e800020u);							/* blr */
        if (rnd(4) == 0)				/* the leaf in the next page: calls and returns that cross pages */
            while (w.size() < 1024 + rnd(8)) emit(0x60000000u);
        const size_t leaf = w.size();
        if (rnd(2)) {					/* a conditional return first: beqlr and friends, bdnzlr */
            const unsigned k = rnd(8);
            cmp(k);
            static const unsigned ret_bo[] = {12, 4, 13, 5};	/* CR forms: a CTR form would end an enclosing CTR loop early */
            emit((19u<<26)|(ret_bo[rnd(4)]<<21)|((4 * k + rnd(4))<<16)|(16u<<1));
        }
        for (unsigned i = 0, n = 1 + rnd(4); i < n; ++i) alu();
        emit(0x4e800020u);
        for (size_t site : call_sites) w[site] = (18u<<26)|uint32((int(leaf) - int(site)) * 4) & 0x3fffffcu | 1u;
        for (size_t site : computed_sites) w[site + 2] = addi(12, 12, int((int(leaf) - int(site + 1)) * 4));
        for (size_t si = 0; si < smc_sites.size(); ++si) {
            const size_t site = smc_sites[si];
            /* Rewrite an addi with a different immediate: lis/ori the new word, stw it, icbi, isync. Inside a loop the target
             * is an earlier instruction of that loop (already executed, so possibly linked); otherwise a later one. */
            size_t target = 0;
            auto addi_at = [&](size_t i) { return (w[i] >> 26) == 14 && ((w[i] >> 21) & 31) >= 3 && ((w[i] >> 21) & 31) <= 9 && ((w[i] >> 16) & 31) >= 3; };
            if (smc_loop[si] != size_t(-1)) { for (size_t i = smc_loop[si]; i < site; ++i) if (addi_at(i)) { target = i; break; } }
            else for (size_t i = site + 6; i < w.size(); ++i) if (addi_at(i)) { target = i; break; }
            if (!target) continue;
            if (smc_loop[si] != size_t(-1)) ++smc_loops; else ++smc_forward;
            const uint32 nw = (w[target] & 0xffff0000u) | uint16(int(rnd(200)) - 100);
            const uint32 off = uint32(target * 4) + 0;	/* offset from code_base (program offset added below) */
            (void)off;
            w[site + 0] = addis(20, 0, int16(nw >> 16)); w[site + 1] = ori(20, 20, nw & 0xffff);
            w[site + 2] = (36u<<26)|(20u<<21)|(13u<<16);			/* stw r20, disp(r13): disp patched with the offset */
            w[site + 3] = addi(21, 13, 0);					/* addi r21, r13, disp */
            w[site + 4] = x31(0, 0, 21, 982);					/* icbi 0,r21 */
            w[site + 5] = 0x4c00012cu;						/* isync */
            w[site + 2] |= uint16(target * 4); w[site + 3] |= uint16(target * 4);
        }
        /* straddle the page boundary on some programs */
        const uint32 start_off = rnd(3) == 0 ? (0x1000u - uint32(w.size() / 2) * 4u) & ~3u : 0u;
        const uint32 pc0 = code_base + start_off;
        /* patch the SMC displacements for the start offset */
        for (size_t site : smc_sites) {
            if ((w[site + 2] >> 26) != 36) continue;
            const uint32 disp = uint16(w[site + 2]) + start_off;
            w[site + 2] = (w[site + 2] & 0xffff0000u) | (disp & 0xffff);
            w[site + 3] = (w[site + 3] & 0xffff0000u) | (disp & 0xffff);
        }
        check(w, start_off, prog, std::function<void()>());
    }
    /* Directed: a pointer walks off the start of the mapped bank inside a loop whose exits are already linked, so the fault
     * arrives after many fast hops and must leave through the C helper (a pending fault never takes a link). */
    {
        auto stbu = [](unsigned rs, unsigned ra, int d) { return (39u<<26)|(rs<<21)|(ra<<16)|uint16(d); };
        auto stwu = [](unsigned rs, unsigned ra, int d) { return (37u<<26)|(rs<<21)|(ra<<16)|uint16(d); };
        auto bdnz = [](int byte_disp) { return (16u<<26)|(16u<<21)|(uint16(byte_disp) & 0xfffcu); };
        const uint32 BLR = 0x4e800020u;
        auto sthu = [](unsigned rs, unsigned ra, int d) { return (45u<<26)|(rs<<21)|(ra<<16)|uint16(d); };
        std::vector<std::vector<uint32> > directed;
        directed.push_back({stbu(5,3,-1), bdnz(-4), BLR});
        directed.push_back({stwu(5,3,-4), addi(5,5,3), bdnz(-8), BLR});
        directed.push_back({stbu(5,3,-1), x31(6,6,5,266), (11u<<26)|(5u<<16)|0x7eu, bc(12,1,8), addi(7,7,1), bdnz(-20), BLR});
        directed.push_back({sthu(5,3,-2), stwu(5,4,4), x31(8,8,5,266), bdnz(-12), BLR});
        for (unsigned d = 0; d < directed.size() * 6; ++d) {
            const std::vector<uint32> &w = directed[d % directed.size()];
            const uint32 back = 0x100u + (d / directed.size()) * 0x91u;		/* iterations before the pointer leaves the bank */
            const uint32 start_off = (d / directed.size()) & 1 ? (0x1000u - uint32(w.size()) * 2u) & ~3u : 0u;
            ran = ran;
            check(w, start_off, 100000 + d, [&]() { cpu->gpr(3) = base + back; cpu->gpr(4) = data + 0x400; cpu->gpr(5) = 0x41; cpu->ctr() = 5000; });
        }
    }
    nw_jit_legacy &= ~NW_JIT_LEGACY_LINK;
    uint64 lf = 0, lm = 0; nw_jit_link_stats(&lf, &lm);
    printf("link sweep: %u programs, %u native calls, %u mismatches, %llu links made, %llu fast hops, %u self-modifying stores inside loops, %u ahead of the store\n", ran, total_insns, mismatches, (unsigned long long)lm, (unsigned long long)lf, smc_loops, smc_forward);
    CHECK(lm > 1000 && lf > 10000);
    nw_jit_invalidate_all(); nw_jit_set_code_pages(0,0,0,0); mmu.reset(); nw_banks_set(NW_PA_RAM,0,0); munmap(ram,span);
    return failed ? 1 : 0;
}

int ppc_core_test_access::fp_fast_sweep(powerpc_cpu *cpu)
{
    cpu->enable_guest_mmu(true); nw_jit_set_mode(NW_JIT_VERIFY);
    nw_jit_set_host_fp_exception(checked_fp_exception); nw_jit_set_host_chain(NULL);
    ppc32_mmu &mmu = ppc32_guest_mmu();
    const uint32 base = 0x10000000u, pc_fast = base+0x1000, pc_slow = base+0x1800;
    void *const wanted = (void *)(VMBaseDiff+base);
    void *const ram = mmap(wanted,0x2000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    CHECK(ram == wanted); if (ram != wanted) return 1;
    nw_banks_set(NW_PA_RAM,base,0x2000);
    uint64 state = 0x9e3779b97f4a7c15ULL;
    auto next = [&]() { state ^= state >> 12; state ^= state << 25; state ^= state >> 27; return state * 0x2545f4914f6cdd1dULL; };
    auto single_bits = [&](int lo, int hi) {
        const int e = lo + int(next() % unsigned(hi - lo + 1));
        const uint32 f = (uint32(next() & 1) << 31) | (uint32(e + 127) << 23) | uint32(next() & 0x7fffff);
        float x; memcpy(&x, &f, 4); return bits(double(x));
    };
    const uint64 specials[] = {0, 0x8000000000000000ULL, 0x7ff0000000000000ULL, 0xfff0000000000000ULL,
        0x7ff8000000000000ULL, 0x7ff0000000000001ULL, 0xfff8123456789abcULL, 0x0000000000000001ULL, 0x000fffffffffffffULL,
        0x3810000000000000ULL, 0x380fffffe0000000ULL, 0x47efffffe0000000ULL, 0x47f0000000000000ULL,
        0x3ff0000000000000ULL, 0xbff0000000000000ULL, 0x3ff0000020000000ULL, 0x3fefffffe0000000ULL,
        0x7fefffffffffffffULL, 0x0010000000000000ULL, 0x36a0000000000000ULL};
    auto operand = [&]() -> uint64 {
        const unsigned r = unsigned(next() % 100);
        if (r < 55) return single_bits(-40, 40);
        if (r < 68) return single_bits(-126, 127);
        if (r < 78) {	/* a double that is generally not a single */
            const int e = -1000 + int(next() % 2001);
            return (next() & 0x8000000000000000ULL) | (uint64(e + 1023) << 52) | (next() & 0x000fffffffffffffULL);
        }
        if (r < 88) return specials[next() % (sizeof specials / sizeof specials[0])];
        /* exponent fields at and around the inline path's limits */
        static const unsigned exps[] = {0, 1, 2, 896, 897, 898, 1149, 1150, 1151, 2045, 2046, 2047};
        return (next() & 0x8000000000000000ULL) | (uint64(exps[next() % (sizeof exps / sizeof exps[0])]) << 52) |
               ((next() & 3) ? (next() & 0x000fffffe0000000ULL) : (next() & 0x000fffffffffffffULL));
    };
    auto summaries = [](uint32 f) {
        f &= ~0x60000000u;
        if (f & 0x01f80700u) f |= 0x20000000u;
        if ((f&0x20000000u)&&(f&0x80u) || (f&0x10000000u)&&(f&0x40u) || (f&0x08000000u)&&(f&0x20u) ||
            (f&0x04000000u)&&(f&0x10u) || (f&0x02000000u)&&(f&8u)) f |= 0x40000000u;
        return f;
    };
    struct kind_info { unsigned xo; bool fma, mul; const char *name; };
    const kind_info kinds[] = {{21,false,false,"fadds"},{20,false,false,"fsubs"},{25,false,true,"fmuls"},
        {29,true,false,"fmadds"},{28,true,false,"fmsubs"},{31,true,false,"fnmadds"},{30,true,false,"fnmsubs"}};
    const unsigned rounds = getenv("PPC_FP_SWEEP_ROUNDS") ? unsigned(strtoul(getenv("PPC_FP_SWEEP_ROUNDS"),NULL,10)) : 400;
    unsigned cases = 0, fast_taken_hint = 0;
    for (const kind_info &kind : kinds)
    for (unsigned rc : {0u,1u})
    for (unsigned shape = 0; shape < 24; ++shape) {
        /* registers 1..5, with aliasing between destination and sources in most shapes */
        const unsigned fa = 1 + shape % 3, fc = kind.fma || kind.mul ? 2 + shape % 2 : 0, fb = kind.mul ? 0 : 3 + (shape / 3) % 2;
        const unsigned fd = shape % 4 == 0 ? fa : shape % 4 == 1 ? (fc ? fc : fb) : shape % 4 == 2 ? fb : 5;
        const uint32 op = (59u<<26)|(fd<<21)|(fa<<16)|(fb<<11)|(fc<<6)|(kind.xo<<1)|rc;
        const uint32 ops[] = {op};
        nw_jit_legacy &= ~NW_JIT_LEGACY_FP;
        nw_jit_fn fast = nw_jit_compile(ops,1,pc_fast,pc_fast,0,0);
        nw_jit_legacy |= NW_JIT_LEGACY_FP;
        nw_jit_fn slow = nw_jit_compile(ops,1,pc_slow,pc_slow,0,0);
        nw_jit_legacy &= ~NW_JIT_LEGACY_FP;
        CHECK(fast != NULL && slow != NULL); if (!fast || !slow) continue;
        for (unsigned round = 0; round < rounds; ++round) {
            ++cases;
            nw_jit_cpu before = {};
            before.msr = 0x2000u | ((next() & 7) == 0 ? 0x900u : 0u);
            before.cr = uint32(next());
            uint32 fpscr = (next() & 3) ? 0 : uint32(next()) & ~0x60000000u;
            if (next() & 1) fpscr |= uint32(next()) & 0x7f000u;		/* FPRF, FR, FI */
            if ((next() & 7) == 0) fpscr |= uint32(next()) & 0xffu;	/* enables, NI, RN */
            if ((next() & 7) == 0) fpscr |= uint32(next()) & 0x1ff00000u;	/* sticky causes */
            before.fpscr = summaries(fpscr);
            before.srr0 = 0x12345678; before.srr1 = 0x87654321;
            for (unsigned r = 0; r < 32; ++r) before.fpr[r] = next();
            before.fpr[fa] = operand();
            if (fc) before.fpr[fc] = operand();
            if (fb) before.fpr[fb] = operand();
            nw_jit_cpu out[2];
            uint32 pcs[2] = {pc_fast, pc_slow};
            nw_jit_fn fns[2] = {fast, slow};
            int round_after[2], flags_after[2];
            uint32 cpu_srr[2][2], cpu_fpscr[2], cpu_cr[2], cpu_pc[2];
            for (unsigned v = 0; v < 2; ++v) {
                before.pc = pcs[v]; before.dec = cpu->dec_;
                out[v] = before;
                mmu.reset(); mmu.set_msr(before.msr); cpu->pc() = pcs[v]; cpu->last_fetch_pa_ = pcs[v];
                cpu->dec_ = 1000000; cpu->dec_tb_base_ = cpu->tb_host_ticks(); cpu->dec_pending_ = false; out[v].dec = cpu->dec_;
                cpu->fpscr() = before.fpscr; cpu->cr().set(before.cr); cpu->xer().set(0); cpu->lr() = cpu->ctr() = 0;
                cpu->srr0_ = before.srr0; cpu->srr1_ = before.srr1; cpu->spcflags().init(); cpu->regs().reserve_valid = 0;
                for (unsigned r = 0; r < 32; ++r) { cpu->fpr_dw(r) = before.fpr[r]; cpu->gpr(r) = 0; }
                fesetround(FE_TONEAREST); feclearexcept(FE_ALL_EXCEPT);
                out[v].host = cpu; nw_jit_cpu_bind(&out[v]); nw_jit_tail_begin();
                fns[v](&out[v]);
                round_after[v] = fegetround(); flags_after[v] = fetestexcept(FE_ALL_EXCEPT);
                cpu_srr[v][0] = cpu->srr0_; cpu_srr[v][1] = cpu->srr1_; cpu_fpscr[v] = cpu->fpscr(); cpu_cr[v] = cpu->cr().get(); cpu_pc[v] = cpu->pc();
            }
            bool same = true;
            for (unsigned r = 0; r < 32; ++r) same &= out[0].fpr[r] == out[1].fpr[r];
            same &= out[0].fpscr == out[1].fpscr && out[0].cr == out[1].cr && out[0].msr == out[1].msr;
            /* SRR0 is the instruction's own address when an exception was taken, so compare it relative to the block. */
            auto rel = [&](uint32 srr0, uint32 pc) { return srr0 == before.srr0 ? 0u : srr0 - pc; };
            same &= rel(out[0].srr0, pc_fast) == rel(out[1].srr0, pc_slow) && out[0].srr1 == out[1].srr1 && out[0].fault == out[1].fault;
            /* A program exception leaves the PC at its vector (absolute); otherwise it stays in the block. */
            auto relpc = [](uint32 after, uint32 start) { return after >= start && after < start + 0x100 ? after - start : after; };
            same &= relpc(out[0].pc, pc_fast) == relpc(out[1].pc, pc_slow);
            same &= round_after[0] == FE_TONEAREST && round_after[1] == FE_TONEAREST;
            same &= flags_after[0] == 0 && flags_after[1] == 0;
            same &= rel(cpu_srr[0][0], pc_fast) == rel(cpu_srr[1][0], pc_slow) && cpu_srr[0][1] == cpu_srr[1][1] && cpu_fpscr[0] == cpu_fpscr[1] &&
                    cpu_cr[0] == cpu_cr[1] && relpc(cpu_pc[0], pc_fast) == relpc(cpu_pc[1], pc_slow);
            CHECK(same);
            if (!same && failed < 40) {
                fprintf(stderr, "fp-fast %s rc=%u fd=%u fa=%u fb=%u fc=%u fpscr=%08x a=%016llx b=%016llx c=%016llx\n  fast fpr[fd]=%016llx fpscr=%08x cr=%08x flags=%x round=%x\n  slow fpr[fd]=%016llx fpscr=%08x cr=%08x flags=%x\n",
                    kind.name, rc, fd, fa, fb, fc, before.fpscr,
                    (unsigned long long)before.fpr[fa], (unsigned long long)(fb ? before.fpr[fb] : 0), (unsigned long long)(fc ? before.fpr[fc] : 0),
                    (unsigned long long)out[0].fpr[fd], out[0].fpscr, out[0].cr, flags_after[0], round_after[0],
                    (unsigned long long)out[1].fpr[fd], out[1].fpscr, out[1].cr, flags_after[1]);
                fprintf(stderr, "  pc rel %d/%d msr %08x/%08x srr %08x,%08x/%08x,%08x fault %d/%d cpu fpscr %08x/%08x cr %08x/%08x pc %d/%d\n",
                    int(out[0].pc - pc_fast), int(out[1].pc - pc_slow), out[0].msr, out[1].msr, out[0].srr0, out[0].srr1, out[1].srr0, out[1].srr1,
                    int(out[0].fault), int(out[1].fault), cpu_fpscr[0], cpu_fpscr[1], cpu_cr[0], cpu_cr[1], int(cpu_pc[0] - pc_fast), int(cpu_pc[1] - pc_slow));
                for (unsigned r = 0; r < 32; ++r) if (out[0].fpr[r] != out[1].fpr[r]) fprintf(stderr, "  fpr[%u] %016llx/%016llx\n", r, (unsigned long long)out[0].fpr[r], (unsigned long long)out[1].fpr[r]);
            }
            if (out[0].fpr[fd] != before.fpr[fd]) ++fast_taken_hint;
            if (getenv("PPC_FP_SWEEP_TRACE") && cases <= 12)
                fprintf(stderr, "trace %s fpscr %08x->%08x fault=%d a=%016llx b=%016llx before_fd=%016llx after_fd=%016llx\n", kind.name, before.fpscr, out[0].fpscr,
                    int(out[0].fault), (unsigned long long)before.fpr[fa], (unsigned long long)(fb ? before.fpr[fb] : before.fpr[fc]),
                    (unsigned long long)before.fpr[fd], (unsigned long long)out[0].fpr[fd]);
        }
    }
    /* lfs/stfs and the indexed forms: inline data-TLB access against the helper path, over mapped, page-crossing and
     * unmapped addresses, with stfs values on both sides of the denormalising exponent range. */
    {
        mmu.reset(); mmu.set_msr(0x2000u | ppc32_mmu::MSR_DR);
        mmu.set_dbat(0, (base & 0xfffe0000u) | 3u, (base & 0xfffe0000u) | 2u);
        nw_jit_dtlb_flush_src(NW_JIT_DTLB_FL_BAT);
        const unsigned mem_rounds = rounds >= 8 ? rounds / 2 : 4;
        unsigned mem_cases = 0, mem_faults = 0;
        for (unsigned form = 0; form < 4; ++form)
        for (unsigned shape = 0; shape < 12; ++shape) {
            const bool store = form >= 2, indexed = form & 1;
            const unsigned rd = 1 + shape % 4, ra = shape % 3 == 0 ? 0 : 3, rb = 4;
            const int16 disp = int16((next() % 64) * 4 - 16);
            const uint32 op = indexed ? (31u<<26)|(rd<<21)|(ra<<16)|(rb<<11)|((store ? 663u : 535u)<<1)
                                      : ((store ? 52u : 48u)<<26)|(rd<<21)|(ra<<16)|(uint16(disp));
            const uint32 ops[] = {op};
            nw_jit_legacy &= ~NW_JIT_LEGACY_FP;
            nw_jit_fn fast = nw_jit_compile(ops,1,pc_fast,pc_fast,0,0);
            nw_jit_legacy |= NW_JIT_LEGACY_FP;
            nw_jit_fn slow = nw_jit_compile(ops,1,pc_slow,pc_slow,0,0);
            nw_jit_legacy &= ~NW_JIT_LEGACY_FP;
            CHECK(fast != NULL && slow != NULL); if (!fast || !slow) continue;
            for (unsigned round = 0; round < mem_rounds; ++round) {
                ++mem_cases;
                nw_jit_cpu before = {};
                before.msr = 0x2000u | ppc32_mmu::MSR_DR;
                for (unsigned r = 0; r < 32; ++r) before.fpr[r] = next();
                /* stfs sources: mostly ordinary, plus values on both sides of exponent 874..896 and the specials */
                before.fpr[rd] = (next() & 3) ? operand() : (next() & 0x8000000000000000ULL) | (uint64(868 + next() % 36) << 52) | (next() & 0x000fffffffffffffULL);
                static const uint32 mem_pick[] = {0x0, 0x10, 0xff8, 0xffc, 0xffd, 0xfff, 0x1000, 0x1ffc, 0x1ffd, 0x1ffe, 0x1fff, 0x2000, 0x2004, 0x20000, 0x30000};
                uint32 ea = base + (next() % 8 ? (uint32(next()) % 0x1ff0) & ~3u | (next() % 5 == 0 ? (uint32(next()) & 3) : 0) : mem_pick[next() % (sizeof mem_pick / sizeof mem_pick[0])]);
                if (next() % 40 == 0) ea = 0x30000000u + uint32(next() % 0x1000);
                if (indexed) { before.gpr[ra] = ra ? ea - (before.gpr[rb] = uint32(next() & 0xff0)) : 0; if (!ra) before.gpr[rb] = ea; }
                else before.gpr[ra] = ra ? ea - uint32(int32(disp)) : 0;
                if (!indexed && !ra) { /* EA is just the displacement: an unmapped low address */ }
                uint8 saved[0x2000];
                for (unsigned i = 0; i < 0x2000; ++i) saved[i] = uint8(next() % 9 ? next() : 0xff);
                nw_jit_cpu out[2]; uint8 after[2][0x2000]; uint32 fault_ea[2];
                for (unsigned v = 0; v < 2; ++v) {
                    before.pc = v ? pc_slow : pc_fast; before.dec = cpu->dec_;
                    out[v] = before;
                    memcpy(ram, saved, 0x2000);
                    mmu.set_msr(before.msr); cpu->pc() = before.pc; cpu->last_fetch_pa_ = before.pc;
                    cpu->dec_ = 1000000; cpu->dec_tb_base_ = cpu->tb_host_ticks(); cpu->dec_pending_ = false; out[v].dec = cpu->dec_;
                    cpu->fpscr() = 0; cpu->cr().set(0); cpu->xer().set(0); cpu->lr() = cpu->ctr() = 0; cpu->spcflags().init();
                    for (unsigned r = 0; r < 32; ++r) { cpu->fpr_dw(r) = before.fpr[r]; cpu->gpr(r) = before.gpr[r]; }
                    out[v].host = cpu; nw_jit_cpu_bind(&out[v]); nw_jit_tail_begin();
                    (v ? slow : fast)(&out[v]);
                    memcpy(after[v], ram, 0x2000);
                    fault_ea[v] = out[v].fault ? out[v].fault_ea : 0;
                }
                bool same = out[0].fault == out[1].fault && fault_ea[0] == fault_ea[1] && !memcmp(after[0], after[1], 0x2000);
                for (unsigned r = 0; r < 32; ++r) same &= out[0].fpr[r] == out[1].fpr[r];
                same &= out[0].fault_st == out[1].fault_st;
                if (out[0].fault) ++mem_faults;
                if (getenv("PPC_FP_SWEEP_TRACE") && (mem_cases <= 10 || (ea >= base + 0x2000 && mem_cases < 400)))
                    fprintf(stderr, "memtrace op=%08x ea=%08x fault=%u/%u rd:%016llx -> %016llx msr=%08x\n", op, ea, out[0].fault, out[1].fault,
                        (unsigned long long)before.fpr[rd], (unsigned long long)out[0].fpr[rd], mmu.msr());
                CHECK(same);
                if (!same && failed < 40)
                    fprintf(stderr, "fp-mem op=%08x ea=%08x fpr[rd]=%016llx fault %u/%u ea %08x/%08x fpr %016llx/%016llx\n", op, ea,
                        (unsigned long long)before.fpr[rd], out[0].fault, out[1].fault, fault_ea[0], fault_ea[1],
                        (unsigned long long)out[0].fpr[rd], (unsigned long long)out[1].fpr[rd]);
            }
        }
        printf("FP load/store sweep: %u cases (%u faulted)\n", mem_cases, mem_faults);
        mmu.reset();
    }
    if (getenv("PPC_FP_BENCH")) {
        /* The decoder's inner loop (fmadds/fmadds/fsubs/fmuls/fadds twice over), compiled with and without the inline path. */
        auto fp = [](unsigned xo, unsigned d, unsigned a, unsigned b, unsigned c) {
            return (59u<<26)|(d<<21)|(a<<16)|(b<<11)|(c<<6)|(xo<<1);
        };
        uint32 ops[10];
        for (unsigned half = 0; half < 2; ++half) {
            ops[half*5+0] = fp(29,3,0,3,4); ops[half*5+1] = fp(29,10,2,3,7); ops[half*5+2] = fp(20,4,10,7,0);
            ops[half*5+3] = fp(25,3,1,0,10); ops[half*5+4] = fp(21,4,5,4,0);
        }
        double per_op[2] = {0, 0};
        for (unsigned v = 0; v < 2; ++v) {
            if (v) nw_jit_legacy |= NW_JIT_LEGACY_FP; else nw_jit_legacy &= ~NW_JIT_LEGACY_FP;
            const uint32 pcb = base + 0x1400 + v * 0x100;	/* page 1, like all code here; stores go to page 0 */
            nw_jit_fn fn = nw_jit_compile(ops,10,pcb,pcb,0,0);
            CHECK(fn != NULL); if (!fn) continue;
            nw_jit_cpu st = {};
            st.pc = pcb; st.msr = 0x2000u; st.host = cpu; nw_jit_cpu_bind(&st);
            const unsigned batches = 4000, runs = 500;
            struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
            for (unsigned b = 0; b < batches; ++b) {
                for (unsigned r = 0; r < 32; ++r) st.fpr[r] = bits(1.0 + 0.0625 * r);
                st.fpscr = 0;
                for (unsigned i = 0; i < runs; ++i) { st.pc = pcb; nw_jit_tail_begin(); fn(&st); }
            }
            clock_gettime(CLOCK_MONOTONIC, &t1);
            const double ns = (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
            per_op[v] = ns / (double(batches) * runs * 10);
        }
        nw_jit_legacy &= ~NW_JIT_LEGACY_FP;
        printf("FP bench: %.1f ns per op inline, %.1f ns per op through the helpers (%.1fx)\n", per_op[0], per_op[1], per_op[0] > 0 ? per_op[1] / per_op[0] : 0.0);
        /* lfs/stfs through translated memory (DBAT identity map, so the inline path hits the data TLB). */
        mmu.reset(); mmu.set_msr(0x2000u | ppc32_mmu::MSR_DR);
        mmu.set_dbat(0, (base & 0xfffe0000u) | 3u, (base & 0xfffe0000u) | 2u);
        nw_jit_dtlb_flush_src(NW_JIT_DTLB_FL_BAT);
        uint32 mops[8];
        const char *mixmode = getenv("PPC_FP_BENCH_MIX");	/* "loads", "stores", or both (default) */
        for (unsigned i = 0; i < 4; ++i) {
            mops[2*i] = (48u<<26)|((1+i)<<21)|(3u<<16)|(i*4); mops[2*i+1] = (52u<<26)|((1+i)<<21)|(3u<<16)|(0x100+i*4);
            if (mixmode && !strcmp(mixmode, "loads")) mops[2*i+1] = (48u<<26)|((5+i)<<21)|(3u<<16)|(0x100+i*4);
            if (mixmode && !strcmp(mixmode, "stores")) mops[2*i] = (52u<<26)|((1+i)<<21)|(3u<<16)|(i*4);
        }
        double mem_op[2] = {0, 0};
        for (unsigned v = 0; v < 2; ++v) {
            if (v) nw_jit_legacy |= NW_JIT_LEGACY_FP; else nw_jit_legacy &= ~NW_JIT_LEGACY_FP;
            const uint32 pcb = base + 0x1c00 + v * 0x100;	/* code on another page than the data: no self-modifying exits */
            nw_jit_fn fn = nw_jit_compile(mops,8,pcb,pcb,0,0);
            CHECK(fn != NULL); if (!fn) continue;
            nw_jit_cpu st = {};
            st.pc = pcb; st.msr = 0x2000u | ppc32_mmu::MSR_DR; st.host = cpu; st.gpr[3] = base + 0x200; nw_jit_cpu_bind(&st);
            for (unsigned warm = 0; warm < 4; ++warm) { st.pc = pcb; st.fault = 0; nw_jit_tail_begin(); fn(&st); }
            if (getenv("PPC_FP_SWEEP_TRACE")) fprintf(stderr, "mem bench v=%u: fault=%u fault_ea=%08x pc=%08x\n", v, st.fault, st.fault_ea, st.pc);
            const unsigned runs = getenv("PPC_FP_BENCH_RUNS") ? unsigned(strtoul(getenv("PPC_FP_BENCH_RUNS"),NULL,10)) : 2000000;
            struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
            for (unsigned i = 0; i < runs; ++i) { st.pc = pcb; st.fault = 0; nw_jit_tail_begin(); fn(&st); }
            clock_gettime(CLOCK_MONOTONIC, &t1);
            mem_op[v] = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / (double(runs) * 8);
        }
        nw_jit_legacy &= ~NW_JIT_LEGACY_FP;
        mmu.reset();
        printf("FP load/store bench: %.1f ns per op inline, %.1f ns per op through the helpers (%.1fx)\n", mem_op[0], mem_op[1], mem_op[0] > 0 ? mem_op[1] / mem_op[0] : 0.0);
    }
    nw_banks_set(NW_PA_RAM,0,0); munmap(ram,0x2000);
    printf("FP fast-path sweep: %u cases (%u wrote their destination)\n", cases, fast_taken_hint);
    return failed ? 1 : 0;
}
int ppc_core_test_access::multiply_p6(powerpc_cpu *cpu)
{
    // Offline exact-rational product literals; FI=1, FR=2, UX=4, OX=8.
    struct product_case { unsigned precision; uint64 a,b; bool tiny; uint64 result[4]; unsigned flags[4]; uint64 adjusted[4]; unsigned adjusted_flags[4]; };
    const product_case inputs[] = {
#include "ppc_multiply_literals.inc"
    };
    struct control { unsigned enable, fe, ip; };
    const control controls[] = {
        {0,0,0},{0,0x900,0x40},{0x80,0,0},{0x80,0x100,0},
        {0x80,0x800,0},{0x80,0x900,0x40},{0x40,0,0},{0x40,0x900,0x40},
        {0x20,0,0},{0x20,0x100,0},{0x20,0x800,0x40},{0x20,0x900,0},
        {8,0,0},{8,0x900,0x40},{0xe8,0,0},{0xe8,0x900,0x40}
    };
    cpu->enable_guest_mmu(true); nw_jit_set_mode(NW_JIT_VERIFY);
    nw_jit_set_host_fp_exception(checked_fp_exception); nw_jit_set_host_chain(NULL);
    ppc32_mmu &mmu = ppc32_guest_mmu();
    const uint32 base = 0x10000000u, pc = base+0x1000;
    void *const wanted = (void *)(VMBaseDiff+base);
    void *const ram = mmap(wanted,0x2000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    CHECK(ram == wanted); if (ram != wanted) return 1;
    nw_banks_set(NW_PA_RAM,base,0x2000);
    fenv_t environment; fegetenv(&environment);
    const int host_modes[] = {FE_TONEAREST,FE_TOWARDZERO,FE_UPWARD,FE_DOWNWARD};
    unsigned cases = 0, publication_cases = 0;
    const bool baseline = getenv("PPC_MULTIPLY_BASELINE") != NULL;
    for (unsigned precision : {59u,63u})
    for (unsigned alias = 0; alias < 6; ++alias)
    for (unsigned rc : {0u,1u}) {
        if (baseline && (alias || rc)) continue;
        const unsigned fa=1, fb=alias>=4 ? 1 : 2;
        const unsigned fd=alias==1 || alias==5 ? fa : alias==2 ? fb : alias==3 ? 0 : 4;
        const unsigned xo=25;
        const uint32 ops[] = {(63u<<26)|(7u<<21)|(8u<<11)|(40u<<1),
            (precision<<26)|(fd<<21)|(fa<<16)|(fb<<6)|(xo<<1)|rc,
            (63u<<26)|(5u<<21)|(6u<<11)|(72u<<1)};
        nw_jit_invalidate_all();
        for (unsigned i = 0; i < 3; ++i) vm_write_memory_4(pc+4*i,ops[i]);
        vm_write_memory_4(pc+12,0); // builder stops at the unsupported sentinel
        nw_jit_fn fn = nw_jit_compile(ops,3,pc,pc,0,0); CHECK(fn != NULL); if (!fn) continue;
        for (const auto &test : inputs)
        for (unsigned negative : {0u,1u})
        for (unsigned rn = 0; rn < 4; ++rn)
        for (const auto &control : controls)
        for (unsigned sticky : {0u,0x1ff00000u,0x9ff00000u})
        for (unsigned engine = 0; engine < 6; ++engine) {
            if (precision!=test.precision || (alias>=4 && (test.a!=test.b || negative))) continue;
            if (baseline && (rn || sticky || (engine!=0 && engine!=2) || control.ip || control.fe ||
                (control.enable!=0 && control.enable!=0x40 && control.enable!=0x20 && control.enable!=8))) continue;
            ++cases; if (engine>=4) ++publication_cases;
            const unsigned failures=failed;
            const uint64 sign=uint64(negative)<<63;
            const unsigned rounding=negative && rn>=2 ? rn^1u : rn;
            const bool adjusted=(test.tiny && (control.enable&0x20u)) || ((test.flags[rounding]&8u) && (control.enable&0x40u));
            const unsigned status=adjusted ? test.adjusted_flags[rounding] : test.flags[rounding];
            const uint64 answer=(adjusted ? test.adjusted[rounding] : test.result[rounding])|sign;
            const uint32 causes=(status&1 ? 0x02000000u : 0)|(status&4 ? 0x08000000u : 0)|(status&8 ? 0x10000000u : 0);
            const bool suppressed=false;
            nw_jit_cpu before={}; before.pc=pc; before.msr=0x2000u|control.fe|control.ip;
            before.cr=0xb2345678u; before.xer=0xe0000000u;
            before.fpscr=sticky|control.enable|rn|0x75000u;
            auto summaries=[](uint32 f) {
                f&=~0x60000000u;
                if (f&0x01f80700u) f|=0x20000000u;
                if ((f&0x20000000u)&&(f&0x80u) || (f&0x10000000u)&&(f&0x40u) ||
                    (f&0x08000000u)&&(f&0x20u) || (f&0x04000000u)&&(f&0x10u) ||
                    (f&0x02000000u)&&(f&8u)) f|=0x40000000u;
                return f;
            };
            before.fpscr=summaries(before.fpscr);
            uint32 expected_fpscr=before.fpscr;
            expected_fpscr=(expected_fpscr&~0x60000u)|(status&1 ? 0x20000u : 0)|(status&2 ? 0x40000u : 0);
            if (!suppressed) {
                const uint64 magnitude=answer&0x7fffffffffffffffULL;
                const bool negative=answer>>63;
                const uint32 classification=magnitude>0x7ff0000000000000ULL ? 17 : !magnitude ? negative ? 18 : 2 :
                    magnitude==0x7ff0000000000000ULL ? negative ? 9 : 5 :
                    !adjusted && magnitude<(precision==59 ? 0x3810000000000000ULL : 0x0010000000000000ULL) ? negative ? 24 : 20 : negative ? 8 : 4;
                expected_fpscr=(expected_fpscr&~0x1f000u)|(classification<<12);
            }
            if (causes&~before.fpscr) expected_fpscr|=0x80000000u;
            expected_fpscr=summaries(expected_fpscr|causes);
            const bool except=control.fe && (expected_fpscr&0x40000000u);
            before.srr0=0x12345678; before.srr1=0x87654321;
            for (unsigned r=0;r<32;++r) {
                before.fpr[r]=bits(double(r+32)); before.gpr[r]=0x12340000u+r;
                for (unsigned w=0;w<4;++w) before.vr[r][w]=0x89100000u+r*4+w;
            }
            before.fpr[fa]=test.a|sign; before.fpr[fb]=test.b;
            // Encoded fixed-zero operand fields must not read FPR0.
            before.fpr[0]=0xfff0000000000001ULL;
            mmu.reset(); mmu.set_msr(before.msr); cpu->pc()=pc; cpu->last_fetch_pa_=pc;
            cpu->dec_=1000000; cpu->dec_tb_base_=cpu->tb_host_ticks(); cpu->dec_pending_=false; before.dec=cpu->dec_;
            cpu->fpscr()=before.fpscr; cpu->cr().set(before.cr); cpu->xer().set(before.xer); cpu->lr()=cpu->ctr()=0;
            cpu->srr0_=before.srr0; cpu->srr1_=before.srr1; cpu->spcflags().init(); cpu->regs().reserve_valid=0;
            for (unsigned r=0;r<32;++r) {
                cpu->fpr_dw(r)=before.fpr[r]; cpu->gpr(r)=before.gpr[r];
                for (unsigned w=0;w<4;++w) cpu->vr(r).w[w]=before.vr[r][w];
            }
            nw_jit_cpu result=before;
            fesetround(host_modes[engine&3]); feclearexcept(FE_ALL_EXCEPT); feraiseexcept(FE_DIVBYZERO);
            fp_callback_round=fegetround(); fp_callback_flags=fetestexcept(FE_ALL_EXCEPT);
            const uint64 serial=cpu->exception_serial_, misses=nw_jit_verify_misses();
            if (!engine) {
                for (uint32 op : ops) { instruction(cpu,op); if (cpu->exception_serial_!=serial) break; }
            } else if (engine==1) (void)nw_jit_interp_n(&result,ops,3,pc);
            else if (engine==2) { result.host=cpu; nw_jit_cpu_bind(&result); nw_jit_tail_begin(); fn(&result); }
            else if (engine==3) CHECK(cpu->nw_jit_verify_block(result,fn,ops,3)==1);
            else { nw_jit_set_mode(engine==4 ? NW_JIT_ON : NW_JIT_VERIFY); CHECK(cpu->nw_jit_try(ops[0])==1); }
            CHECK(nw_jit_verify_misses()==misses);
            if (engine==0 || engine>=3) {
                result.pc=cpu->pc(); result.fpscr=cpu->fpscr(); result.cr=cpu->cr().get(); result.msr=mmu.msr();
                result.srr0=cpu->srr0_; result.srr1=cpu->srr1_;
                for (unsigned r=0;r<32;++r) {
                    result.fpr[r]=cpu->fpr_dw(r); result.gpr[r]=cpu->gpr(r);
                    for (unsigned w=0;w<4;++w) result.vr[r][w]=cpu->vr(r).w[w];
                }
            }
            CHECK(result.fpscr==expected_fpscr);
            CHECK(result.cr==(rc ? (before.cr&~0x0f000000u)|((expected_fpscr>>4)&0x0f000000u) : before.cr));
            CHECK(result.pc==(except ? control.ip ? 0xfff00700u : 0x700u : pc+12));
            CHECK(result.msr==(except ? before.msr&~0x0204ef32u : before.msr));
            CHECK(result.srr0==(except ? pc+4 : before.srr0));
            CHECK(result.srr1==(except ? (before.msr&~0x783f0000u)|0x100000u : before.srr1));
            if (engine==0 || engine==2 || engine>=4) CHECK(cpu->exception_serial_==serial+unsigned(except));
            for (unsigned r=0;r<32;++r) {
                CHECK(result.fpr[r]==(r==7 ? before.fpr[8]^0x8000000000000000ULL :
                    r==fd ? suppressed ? before.fpr[r] : answer : r==5 && !except ? before.fpr[6] : before.fpr[r]));
                CHECK(result.gpr[r]==before.gpr[r]);
                for (unsigned w=0;w<4;++w) CHECK(result.vr[r][w]==before.vr[r][w]);
            }
            CHECK(fegetround()==fp_callback_round && fetestexcept(FE_ALL_EXCEPT)==fp_callback_flags);
            if (failed!=failures && failed<100) fprintf(stderr,"multiply engine=%u prim=%u xo=%u alias=%u a=%016llx b=%016llx rn=%u en=%x fe=%x got=%016llx/%08x expected=%016llx/%08x\n",engine,precision,xo,alias,(unsigned long long)test.a,(unsigned long long)test.b,rn,control.enable,control.fe,(unsigned long long)result.fpr[fd],result.fpscr,(unsigned long long)answer,expected_fpscr);
        }
    }
    printf("P6 multiply: %u literal engine cases, including %u production entry/commit cases\n",cases,publication_cases);
    fesetenv(&environment); nw_jit_invalidate_all(); nw_banks_set(NW_PA_RAM,0,0); munmap(ram,0x2000);
    nw_jit_set_host_fp_exception(powerpc_cpu::jit_host_fp_exception);
    nw_jit_set_host_chain(powerpc_cpu::jit_host_chain); nw_jit_set_mode(NW_JIT_ON);
    return failed ? 1 : 0;
}
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

    scalar_p6(cpu);
    frsp_p6(cpu);
    basic_special_p6(cpu);
    multiply_p6(cpu);
    io_publication(cpu);
    // Literal boundary expectations qualify every OE/Rc form independently
    // of the JIT helpers. Run KPX, the local C path, ARM64 and private replay.
    struct integer_boundary { unsigned kind; uint32 a, result[2]; unsigned carry[2], overflow[2]; };
    const integer_boundary integer_cases[] = {
        {0u, 0x00000000u, {0x00000000u, 0x00000000u}, {0u, 1u}, {0u, 0u}},
        {0u, 0x00000001u, {0xffffffffu, 0xffffffffu}, {0u, 1u}, {0u, 0u}},
        {0u, 0xffffffffu, {0x00000001u, 0x00000001u}, {0u, 1u}, {0u, 0u}},
        {0u, 0x7fffffffu, {0x80000001u, 0x80000001u}, {0u, 1u}, {0u, 0u}},
        {0u, 0x80000000u, {0x80000000u, 0x80000000u}, {0u, 1u}, {1u, 1u}},
        {0u, 0x80000001u, {0x7fffffffu, 0x7fffffffu}, {0u, 1u}, {0u, 0u}},
        {0u, 0x7ffffffeu, {0x80000002u, 0x80000002u}, {0u, 1u}, {0u, 0u}},
        {0u, 0xfffffffeu, {0x00000002u, 0x00000002u}, {0u, 1u}, {0u, 0u}},
        {0u, 0x12345678u, {0xedcba988u, 0xedcba988u}, {0u, 1u}, {0u, 0u}},
        {1u, 0x00000000u, {0xffffffffu, 0x00000000u}, {0u, 1u}, {0u, 0u}},
        {1u, 0x00000001u, {0x00000000u, 0x00000001u}, {1u, 1u}, {0u, 0u}},
        {1u, 0xffffffffu, {0xfffffffeu, 0xffffffffu}, {1u, 1u}, {0u, 0u}},
        {1u, 0x7fffffffu, {0x7ffffffeu, 0x7fffffffu}, {1u, 1u}, {0u, 0u}},
        {1u, 0x80000000u, {0x7fffffffu, 0x80000000u}, {1u, 1u}, {1u, 0u}},
        {1u, 0x80000001u, {0x80000000u, 0x80000001u}, {1u, 1u}, {0u, 0u}},
        {1u, 0x7ffffffeu, {0x7ffffffdu, 0x7ffffffeu}, {1u, 1u}, {0u, 0u}},
        {1u, 0xfffffffeu, {0xfffffffdu, 0xfffffffeu}, {1u, 1u}, {0u, 0u}},
        {1u, 0x12345678u, {0x12345677u, 0x12345678u}, {1u, 1u}, {0u, 0u}},
        {2u, 0x00000000u, {0x00000000u, 0x00000001u}, {0u, 0u}, {0u, 0u}},
        {2u, 0x00000001u, {0x00000001u, 0x00000002u}, {0u, 0u}, {0u, 0u}},
        {2u, 0xffffffffu, {0xffffffffu, 0x00000000u}, {0u, 1u}, {0u, 0u}},
        {2u, 0x7fffffffu, {0x7fffffffu, 0x80000000u}, {0u, 0u}, {0u, 1u}},
        {2u, 0x80000000u, {0x80000000u, 0x80000001u}, {0u, 0u}, {0u, 0u}},
        {2u, 0x80000001u, {0x80000001u, 0x80000002u}, {0u, 0u}, {0u, 0u}},
        {2u, 0x7ffffffeu, {0x7ffffffeu, 0x7fffffffu}, {0u, 0u}, {0u, 0u}},
        {2u, 0xfffffffeu, {0xfffffffeu, 0xffffffffu}, {0u, 0u}, {0u, 0u}},
        {2u, 0x12345678u, {0x12345678u, 0x12345679u}, {0u, 0u}, {0u, 0u}},
        {3u, 0x00000000u, {0xfffffffeu, 0xffffffffu}, {1u, 1u}, {0u, 0u}},
        {3u, 0x00000001u, {0xfffffffdu, 0xfffffffeu}, {1u, 1u}, {0u, 0u}},
        {3u, 0xffffffffu, {0xffffffffu, 0x00000000u}, {0u, 1u}, {0u, 0u}},
        {3u, 0x7fffffffu, {0x7fffffffu, 0x80000000u}, {1u, 1u}, {1u, 0u}},
        {3u, 0x80000000u, {0x7ffffffeu, 0x7fffffffu}, {1u, 1u}, {0u, 0u}},
        {3u, 0x80000001u, {0x7ffffffdu, 0x7ffffffeu}, {1u, 1u}, {0u, 0u}},
        {3u, 0x7ffffffeu, {0x80000000u, 0x80000001u}, {1u, 1u}, {0u, 0u}},
        {3u, 0xfffffffeu, {0x00000000u, 0x00000001u}, {1u, 1u}, {0u, 0u}},
        {3u, 0x12345678u, {0xedcba986u, 0xedcba987u}, {1u, 1u}, {0u, 0u}},
        {4u, 0x00000000u, {0xffffffffu, 0x00000000u}, {0u, 1u}, {0u, 0u}},
        {4u, 0x00000001u, {0xfffffffeu, 0xffffffffu}, {0u, 0u}, {0u, 0u}},
        {4u, 0xffffffffu, {0x00000000u, 0x00000001u}, {0u, 0u}, {0u, 0u}},
        {4u, 0x7fffffffu, {0x80000000u, 0x80000001u}, {0u, 0u}, {0u, 0u}},
        {4u, 0x80000000u, {0x7fffffffu, 0x80000000u}, {0u, 0u}, {0u, 1u}},
        {4u, 0x80000001u, {0x7ffffffeu, 0x7fffffffu}, {0u, 0u}, {0u, 0u}},
        {4u, 0x7ffffffeu, {0x80000001u, 0x80000002u}, {0u, 0u}, {0u, 0u}},
        {4u, 0xfffffffeu, {0x00000001u, 0x00000002u}, {0u, 0u}, {0u, 0u}},
        {4u, 0x12345678u, {0xedcba987u, 0xedcba988u}, {0u, 0u}, {0u, 0u}},
    };
    const unsigned integer_xo[] = {104, 234, 202, 232, 200}; // neg/addme/addze/subfme/subfze
    nw_jit_set_mode(NW_JIT_ON);
    mmu.reset();
    unsigned integer_form = 0;
    for (unsigned kind = 0; kind < 5; ++kind)
    for (unsigned oe = 0; oe < 2; ++oe)
    for (unsigned rc = 0; rc < 2; ++rc)
    for (unsigned alias = 0; alias < 3; ++alias) {
        const unsigned ra = alias == 2 ? 0 : 4, rd = alias == 1 ? ra : 3;
        const uint32 op = (31u<<26)|(rd<<21)|(ra<<16)|(integer_xo[kind]<<1)|(oe<<10)|rc;
        const uint32 start = 0x120000u + 64 * integer_form++;
        CHECK(nw_jit_op_supported(op)); CHECK(nw_jit_op_verify_safe(op));
        nw_jit_fn fn = nw_jit_compile(&op, 1, start, start & ~0xfffu, 0, 0);
        CHECK(fn != NULL);
        for (const integer_boundary &c : integer_cases) {
            if (c.kind != kind) continue;
            for (unsigned ca = 0; ca < 2; ++ca)
            for (unsigned incoming = 0; incoming < 4; ++incoming) {
                nw_jit_cpu input = {}; input.pc = start;
                input.cr = 0xf2345678u;
                input.xer = 0x01234567u | (ca<<29) | (incoming<<30);
                for (unsigned r = 0; r < 32; ++r) input.gpr[r] = 0x41000000u + 0x010101u * r;
                input.gpr[ra] = c.a;
                uint32 expected_xer = input.xer;
                if (kind) expected_xer = (expected_xer & ~0x20000000u) | (c.carry[ca]<<29);
                if (oe) {
                    expected_xer &= ~0x40000000u;
                    if (c.overflow[ca]) expected_xer |= 0xc0000000u;
                }
                const uint32 result = c.result[ca];
                const uint32 nibble = (result == 0 ? 2u : (result & 0x80000000u ? 8u : 4u)) | (expected_xer>>31);
                const uint32 expected_cr = rc ? (input.cr & 0x0fffffffu) | (nibble<<28) : input.cr;
                for (unsigned engine = 0; engine < 4; ++engine) {
                    nw_jit_cpu shadow = input;
                    if (engine == 1) {
                        CHECK(nw_jit_interp_n(&shadow, &op, 1, start) == 0);
                    } else if (engine == 2) {
                        if (!fn) continue;
                        nw_jit_cpu_bind(&shadow); nw_jit_tail_begin(); fn(&shadow);
                    } else {
                        cpu->pc() = start; cpu->last_fetch_pa_ = start;
                        cpu->cr().set(input.cr); cpu->xer().set(input.xer);
                        cpu->spcflags().init(); cpu->regs().reserve_valid = 0;
                        for (unsigned r = 0; r < 32; ++r) cpu->gpr(r) = input.gpr[r];
                        if (engine == 3) {
                            if (!fn) continue;
                            shadow.dec = cpu->dec_; shadow.fpscr = cpu->fpscr();
                            shadow.lr = cpu->lr(); shadow.ctr = cpu->ctr(); shadow.vscr = cpu->vscr().get();
                            for (unsigned r = 0; r < 32; ++r) {
                                shadow.fpr[r] = cpu->fpr_dw(r);
                                for (unsigned w = 0; w < 4; ++w) shadow.vr[r][w] = cpu->vr(r).w[w];
                            }
                            const uint64 misses = nw_jit_verify_misses();
                            CHECK(cpu->nw_jit_verify_block(shadow, fn, &op, 1) == 1);
                            CHECK(nw_jit_verify_misses() == misses);
                        } else {
                            instruction(cpu, op);
                            shadow.pc = cpu->pc(); shadow.xer = cpu->xer().get(); shadow.cr = cpu->cr().get();
                            for (unsigned r = 0; r < 32; ++r) shadow.gpr[r] = cpu->gpr(r);
                        }
                    }
                    CHECK(shadow.pc == start + 4 && !shadow.fault);
                    CHECK(shadow.xer == expected_xer); CHECK(shadow.cr == expected_cr);
                    for (unsigned r = 0; r < 32; ++r) CHECK(shadow.gpr[r] == (r == rd ? result : input.gpr[r]));
                }
            }
        }
    }

    // Independent KPX/native vector comparisons cover every named private
    // integer helper, all set partitions of D/A/B/C and adversarial lane bits.
    // Private VMX FP comparisons follow the integer cases below.
    struct vector_form { const char *name; unsigned xo, form; };
    const vector_form vector_forms[] = {
        {"vaddubm",0,0}, {"vor",1156,0}, {"vand",1028,0}, {"vandc",1092,0},
        {"vxor",1220,0}, {"vsububm",1024,0}, {"vslh",324,0},
        {"vcmpequw",134,0}, {"vcmpequw.",1158,0}, {"vcmpequb",6,0}, {"vcmpequb.",1030,0},
        {"vminsb",770,0}, {"vsr",708,0}, {"vsrw",644,0}, {"vspltisw",908,1},
        {"vsl",452,0}, {"vslo",1036,0}, {"vsro",1100,0}, {"vspltisb",780,1},
        {"mtvscr",1604,2}, {"mfvscr",1540,3}, {"vsrb",516,0}, {"vslb",260,0},
        {"vspltish",844,1}, {"vspltw",652,4}, {"vspltb",524,4},
        {"vmrghb",12,0}, {"vmrglb",268,0}, {"vmrghw",140,0}, {"vmrglw",396,0},
        {"vsumsws",1928,0}, {"vsel",42,5}, {"vperm",43,5}, {"vmsumshm",40,5},
        {"vmsumshs",41,5}, {"vsldoi",44,6}, {"vmladduhm",34,5},
        {"vadduwm",128,0}, {"vsraw",900,0}, {"vpkswss",462,0}, {"vsubshs",1856,0},
        {"vaddcuw",384,0},
        {"vaddsbs",768,0},
        {"vaddshs",832,0},
        {"vaddsws",896,0},
        {"vaddubs",512,0},
        {"vadduhm",64,0},
        {"vadduhs",576,0},
        {"vadduws",640,0},
        {"vavgsb",1282,0},
        {"vavgsh",1346,0},
        {"vavgsw",1410,0},
        {"vavgub",1026,0},
        {"vavguh",1090,0},
        {"vavguw",1154,0},
        {"vcmpequh",70,0},
        {"vcmpequh.",1094,0},
        {"vcmpgtsb",774,0},
        {"vcmpgtsb.",1798,0},
        {"vcmpgtsh",838,0},
        {"vcmpgtsh.",1862,0},
        {"vcmpgtsw",902,0},
        {"vcmpgtsw.",1926,0},
        {"vcmpgtub",518,0},
        {"vcmpgtub.",1542,0},
        {"vcmpgtuh",582,0},
        {"vcmpgtuh.",1606,0},
        {"vcmpgtuw",646,0},
        {"vcmpgtuw.",1670,0},
        {"vmaxsb",258,0},
        {"vmaxsh",322,0},
        {"vmaxsw",386,0},
        {"vmaxub",2,0},
        {"vmaxuh",66,0},
        {"vmaxuw",130,0},
        {"vmhaddshs",32,5},
        {"vmhraddshs",33,5},
        {"vminsh",834,0},
        {"vminsw",898,0},
        {"vminub",514,0},
        {"vminuh",578,0},
        {"vminuw",642,0},
        {"vmrghh",76,0},
        {"vmrglh",332,0},
        {"vmsummbm",37,5},
        {"vmsumubm",36,5},
        {"vmsumuhm",38,5},
        {"vmsumuhs",39,5},
        {"vmulesb",776,0},
        {"vmulesh",840,0},
        {"vmuleub",520,0},
        {"vmuleuh",584,0},
        {"vmulosb",264,0},
        {"vmulosh",328,0},
        {"vmuloub",8,0},
        {"vmulouh",72,0},
        {"vnor",1284,0},
        {"vpkpx",782,0},
        {"vpkshss",398,0},
        {"vpkshus",270,0},
        {"vpkswus",334,0},
        {"vpkuhum",14,0},
        {"vpkuhus",142,0},
        {"vpkuwum",78,0},
        {"vpkuwus",206,0},
        {"vrlb",4,0},
        {"vrlh",68,0},
        {"vrlw",132,0},
        {"vslw",388,0},
        {"vsplth",588,4},
        {"vsrab",772,0},
        {"vsrah",836,0},
        {"vsrh",580,0},
        {"vsubcuw",1408,0},
        {"vsubsbs",1792,0},
        {"vsubsws",1920,0},
        {"vsububs",1536,0},
        {"vsubuhm",1088,0},
        {"vsubuhs",1600,0},
        {"vsubuwm",1152,0},
        {"vsubuws",1664,0},
        {"vsum2sws",1672,0},
        {"vsum4sbs",1800,0},
        {"vsum4shs",1608,0},
        {"vsum4ubs",1544,0},
        {"vupkhpx",846,7},
        {"vupkhsb",526,7},
        {"vupkhsh",590,7},
        {"vupklpx",974,7},
        {"vupklsb",654,7},
        {"vupklsh",718,7}
    };
    const unsigned vector_aliases[][4] = {
        {1,1,1,1}, {1,1,1,2}, {1,1,2,1}, {1,1,2,2}, {1,1,2,3},
        {1,2,1,1}, {1,2,1,2}, {1,2,1,3}, {1,2,2,1}, {1,2,2,2},
        {1,2,2,3}, {1,2,3,1}, {1,2,3,2}, {1,2,3,3}, {1,2,3,4}
    };
    unsigned vector_case = 0;
    for (const vector_form &v : vector_forms)
    for (const auto &alias : vector_aliases) {
        const unsigned vd = alias[0], va = alias[1], vb = alias[2], vc = alias[3];
        uint32 op = (4u<<26)|(vd<<21)|v.xo;
        if (v.form != 2 && v.form != 3 && v.form != 7) op |= va<<16;
        if (v.form != 1 && v.form != 3) op |= vb<<11;
        if (v.form == 5 || v.form == 6) op |= vc<<6;
        if (v.form == 2) op &= ~(31u<<21);
        const uint32 start = 0x140000u + 64 * vector_case++;
        nw_jit_set_mode(NW_JIT_ON); mmu.reset(); mmu.set_msr(NW_MSR_VEC);
        nw_jit_fn fn = nw_jit_compile(&op, 1, start, start & ~0xfffu, 0, 0);
        CHECK(fn != NULL); CHECK(nw_jit_op_verify_safe(op));
        for (unsigned pattern = 0; pattern < 16; ++pattern)
        for (unsigned sat = 0; sat < 2; ++sat) {
            nw_jit_cpu input = {}; input.pc = start; input.msr = NW_MSR_VEC;
            input.cr = 0x12345678; input.xer = 0xe1234567; input.vscr = 0x10000u | sat;
            for (unsigned r = 0; r < 32; ++r) {
                input.gpr[r] = 0x12340000u + r;
                for (unsigned w = 0; w < 4; ++w) {
                    const uint32 values[] = {0x01020304u * (1 + r + w), 0u, UINT32_MAX,
                        0x7fffffffu, 0x80000000u, 0x80007fffu, 0x7fff8000u, 0x03020100u + 0x04040404u * w};
                    uint32 mixed = 0x9e3779b9u * (r + 37 * w + 521 * pattern + 1);
                    mixed ^= mixed >> 16; mixed *= 0x85ebca6bu;
                    mixed ^= mixed >> 13; mixed *= 0xc2b2ae35u; mixed ^= mixed >> 16;
                    input.vr[r][w] = pattern < 8 ? values[pattern] : mixed;
                }
            }
            auto prepare_vector_cpu = [&]() {
                cpu->pc() = start; cpu->last_fetch_pa_ = start;
                cpu->cr().set(input.cr); cpu->xer().set(input.xer); cpu->vscr().set(input.vscr);
                cpu->fpscr() = 0; cpu->lr() = 0; cpu->ctr() = 0; cpu->regs().reserve_valid = 0;
                for (unsigned r = 0; r < 32; ++r) {
                    cpu->gpr(r) = input.gpr[r]; cpu->fpr_dw(r) = 0;
                    for (unsigned w = 0; w < 4; ++w) cpu->vr(r).w[w] = input.vr[r][w];
                }
            };
            prepare_vector_cpu(); instruction(cpu, op);
            const uint32 expected_cr = cpu->cr().get(), expected_vscr = cpu->vscr().get();
            uint32 expected[32][4];
            for (unsigned r = 0; r < 32; ++r) for (unsigned w = 0; w < 4; ++w) expected[r][w] = cpu->vr(r).w[w];
            for (unsigned engine = 0; engine < 3; ++engine) {
                nw_jit_cpu shadow = input;
                if (engine == 0) CHECK(nw_jit_interp_n(&shadow, &op, 1, start) == 0);
                else if (engine == 1) {
                    if (!fn) continue;
                    nw_jit_cpu_bind(&shadow); nw_jit_tail_begin(); fn(&shadow);
                } else {
                    if (!fn) continue;
                    prepare_vector_cpu(); shadow.dec = cpu->dec_;
                    const uint64 misses = nw_jit_verify_misses();
                    CHECK(cpu->nw_jit_verify_block(shadow, fn, &op, 1) == 1);
                    CHECK(nw_jit_verify_misses() == misses);
                }
                if (shadow.cr != expected_cr || shadow.vscr != expected_vscr)
                    fprintf(stderr,"vector %s alias=%u%u%u%u pattern=%u sat=%u engine=%u cr=%08x/%08x vscr=%08x/%08x\n",v.name,vd,va,vb,vc,pattern,sat,engine,shadow.cr,expected_cr,shadow.vscr,expected_vscr);
                CHECK(shadow.pc == start + 4 && !shadow.fault);
                CHECK(shadow.cr == expected_cr && shadow.vscr == expected_vscr && shadow.xer == input.xer);
                for (unsigned r = 0; r < 32; ++r) {
                    CHECK(shadow.gpr[r] == input.gpr[r]); CHECK(shadow.fpr[r] == 0);
                    for (unsigned w = 0; w < 4; ++w) {
                        if (shadow.vr[r][w] != expected[r][w])
                            fprintf(stderr,"vector %s alias=%u%u%u%u pattern=%u sat=%u engine=%u r%u.w%u=%08x/%08x\n",v.name,vd,va,vb,vc,pattern,sat,engine,r,w,shadow.vr[r][w],expected[r][w]);
                        CHECK(shadow.vr[r][w] == expected[r][w]);
                    }
                }
            }
        }
    }

    // Vector FP reference/native/private comparisons. Scalar RN and host
    // rounding/exception flags must not control an AltiVec instruction.
    const vector_form vector_fp_forms[] = {
        {"vaddfp",10,0},
        {"vcfsx",842,4},
        {"vcfux",778,4},
        {"vcmpbfp",966,0},
        {"vcmpbfp.",1990,0},
        {"vcmpeqfp",198,0},
        {"vcmpeqfp.",1222,0},
        {"vcmpgefp",454,0},
        {"vcmpgefp.",1478,0},
        {"vcmpgtfp",710,0},
        {"vcmpgtfp.",1734,0},
        {"vctsxs",970,4},
        {"vctuxs",906,4},
        {"vexptefp",394,7},
        {"vlogefp",458,7},
        {"vmaddfp",46,5},
        {"vmaxfp",1034,0},
        {"vminfp",1098,0},
        {"vnmsubfp",47,5},
        {"vrefp",266,7},
        {"vrfim",714,7},
        {"vrfin",522,7},
        {"vrfip",650,7},
        {"vrfiz",586,7},
        {"vrsqrtefp",330,7},
        {"vsubfp",74,0},
    };
    fenv_t vector_fp_host_env; fegetenv(&vector_fp_host_env);
    unsigned vector_fp_case = 0;
    for (const auto &v : vector_fp_forms) for (const auto &alias : vector_aliases) {
        uint32 op = (4u<<26)|(alias[0]<<21)|v.xo;
        if (v.form != 7) op |= alias[1]<<16;
        op |= alias[2]<<11;
        if (v.form == 5) op |= alias[3]<<6;
        const uint32 start = 0x300000 + 64 * vector_fp_case++;
        nw_jit_set_mode(NW_JIT_ON); mmu.reset(); mmu.set_msr(NW_MSR_VEC);
        nw_jit_fn fn = nw_jit_compile(&op, 1, start, start & ~0xfffu, 0, 0);
        CHECK(fn != NULL); CHECK(nw_jit_op_verify_safe(op));
        for (unsigned pattern = 0; pattern < 16; ++pattern)
        for (unsigned host = 0; host < 4; ++host)
        for (unsigned nj = 0; nj < 2; ++nj) {
            nw_jit_cpu input = {}; input.pc = start; input.msr = NW_MSR_VEC;
            input.cr = 0x12345678; input.xer = 0xe1234567; input.fpscr = 0x12340000 | ((host + pattern) & 3);
            input.vscr = (nj<<16) | (pattern & 1);
            for (unsigned r = 0; r < 32; ++r) for (unsigned w = 0; w < 4; ++w) {
                const uint32 values[] = {0u,0x80000000u,0x3f000000u,0xbf000000u,0x7f800000u,0xff800000u,
                    0x7fc10000u+r,0xff810000u+r,1u,0x80000001u,0x007fffffu,0x807fffffu,
                    0x7f7fffffu,0x4f000000u,0xcf000000u,0x3f800001u+0x123u*r+0x456u*w};
                input.vr[r][w] = values[(pattern + w) & 15];
            }
            uint64 host_control = 0, host_status = 0;
            auto host_fp_preserved = [&]() {
#if defined(__aarch64__)
                uint64 control, status;
                __asm__ volatile("mrs %0, fpcr\n\tmrs %1, fpsr" : "=r"(control), "=r"(status) :: "memory");
                return control == host_control && status == host_status;
#else
                return true;
#endif
            };
            auto prepare_fp_vector = [&]() {
                cpu->pc() = start; cpu->last_fetch_pa_ = start;
                cpu->cr().set(input.cr); cpu->xer().set(input.xer); cpu->vscr().set(input.vscr);
                cpu->fpscr() = input.fpscr; cpu->lr() = cpu->ctr() = 0; cpu->regs().reserve_valid = 0;
                for (unsigned r = 0; r < 32; ++r) {
                    cpu->gpr(r) = 0; cpu->fpr_dw(r) = 0;
                    for (unsigned w = 0; w < 4; ++w) cpu->vr(r).w[w] = input.vr[r][w];
                }
                fesetround(modes[host]); feclearexcept(FE_ALL_EXCEPT); feraiseexcept(FE_DIVBYZERO);
#if defined(__aarch64__)
                // Host FZ and default-NaN must not control guest vector FP.
                __asm__ volatile("mrs %0, fpcr" : "=r"(host_control) :: "memory");
                host_control |= UINT64_C(0x3000000);
                __asm__ volatile("msr fpcr, %0\n\tmrs %1, fpsr" : "+r"(host_control), "=r"(host_status) :: "memory");
#endif
            };
            prepare_fp_vector(); const int reference_flags = fetestexcept(FE_ALL_EXCEPT);
            instruction(cpu, op);
            CHECK(fegetround() == modes[host] && fetestexcept(FE_ALL_EXCEPT) == reference_flags && cpu->fpscr() == input.fpscr && host_fp_preserved());
            const uint32 cr = cpu->cr().get(), vscr = cpu->vscr().get(); uint32 expected[32][4];
            for (unsigned r = 0; r < 32; ++r) for (unsigned w = 0; w < 4; ++w) expected[r][w] = cpu->vr(r).w[w];
            for (unsigned engine = 0; engine < 3; ++engine) {
                nw_jit_cpu result = input; prepare_fp_vector(); result.dec = cpu->dec_;
                const int flags = fetestexcept(FE_ALL_EXCEPT);
                if (engine == 0) CHECK(nw_jit_interp_n(&result, &op, 1, start) == 0);
                if (engine == 1) { nw_jit_cpu_bind(&result); nw_jit_tail_begin(); if (fn) fn(&result); }
                if (engine == 2) {
                    const uint64 misses = nw_jit_verify_misses();
                    if (fn) CHECK(cpu->nw_jit_verify_block(result, fn, &op, 1) == 1);
                    CHECK(nw_jit_verify_misses() == misses);
                }
                CHECK(fegetround() == modes[host] && fetestexcept(FE_ALL_EXCEPT) == flags && host_fp_preserved());
                CHECK(result.cr == cr && result.vscr == vscr && result.fpscr == input.fpscr);
                CHECK(result.pc == start + 4 && !result.fault && result.xer == input.xer);
                for (unsigned r = 0; r < 32; ++r) for (unsigned w = 0; w < 4; ++w) {
                    if (result.vr[r][w] != expected[r][w])
                        fprintf(stderr,"FP vector %s alias=%u%u%u%u pattern=%u host=%u nj=%u engine=%u r%u.w%u=%08x/%08x\n",v.name,alias[0],alias[1],alias[2],alias[3],pattern,host,nj,engine,r,w,result.vr[r][w],expected[r][w]);
                    CHECK(result.vr[r][w] == expected[r][w]);
                }
            }
        }
    }
    fesetenv(&vector_fp_host_env);

    // ISA estimate error bounds use a higher-precision mathematical oracle,
    // not matching implementations. Each set is ordered for monotonicity.
    // The chosen domains avoid overflow/underflow ambiguity in the estimates.
    for (unsigned kind : {266u, 330u, 394u, 458u}) {
        double previous = kind == 266 || kind == 330 ? INFINITY : -INFINITY;
        for (unsigned sample = 0; sample < 256; ++sample) {
            nw_jit_cpu state = {}; state.pc = 0x2e0000; state.msr = NW_MSR_VEC;
            const float x = kind == 394 ? -120.0f + float(sample) * (240.0f / 255.0f) :
                float(pow(2.0, -120.0 + double(sample) * (240.0 / 255.0)));
            uint32 raw; memcpy(&raw, &x, 4);
            for (unsigned w = 0; w < 4; ++w) state.vr[2][w] = raw;
            const uint32 op = (4u<<26)|(3u<<21)|(2u<<11)|kind;
            CHECK(nw_jit_interp_n(&state, &op, 1, state.pc) == 0);
            float actual; memcpy(&actual, &state.vr[3][0], 4);
            const long double oracle = kind == 266 ? 1.0L / x : kind == 330 ? 1.0L / sqrtl(x) :
                kind == 394 ? powl(2.0L, x) : logl(x) / logl(2.0L);
            const long double error = fabsl(actual - oracle);
            if (kind == 458) {
                CHECK(error <= 1.0L / 32);
                if (fabsl(x - 1.0L) > 1.0L / 8) CHECK(error <= fabsl(oracle) / 8);
            } else CHECK(error <= fabsl(oracle) / (kind == 394 ? 16 : 4096));
            CHECK(isfinite(actual));
            CHECK(kind == 266 || kind == 330 ? actual <= previous : actual >= previous);
            previous = actual;
        }
    }
    fesetround(FE_TONEAREST); feclearexcept(FE_ALL_EXCEPT);

    // Every decoder slot is classified independently from the private helper
    // descriptor, including record bits and all VA vC values. Invalid
    // slots must not accidentally enter the verifier's positive allowlist.
    for (unsigned low = 0; low < 2048; ++low) {
        const uint32 op = (4u<<26) | low;
        const char *name = cpu->decode(op)->name;
        bool integer = false;
        for (const auto &form : vector_forms) if (!strcmp(name, form.name)) integer = true;
        for (const auto &form : vector_fp_forms) if (!strcmp(name, form.name)) integer = true;
        // The fifth SHB bit is reserved; neither decoder nor VERIFY accepts it.
        if (!strcmp(name, "vsldoi") && (op & 1024)) integer = false;
        CHECK(bool(nw_jit_op_verify_safe(op)) == integer);
    }
    // Fixed-zero fields from the ISA, independently enumerated here. Every
    // bit is mutated separately: both JIT entry points must reject it, and
    // New World decode must deliver a precise illegal-instruction exception.
    const struct { unsigned xo; uint32 zero; } reserved_fields[] = {
        {526,31u<<16},{654,31u<<16},{590,31u<<16},{718,31u<<16},{846,31u<<16},{974,31u<<16},
        {394,31u<<16},{458,31u<<16},{266,31u<<16},{330,31u<<16},{714,31u<<16},{522,31u<<16},{650,31u<<16},{586,31u<<16},
        {780,31u<<11},{844,31u<<11},{908,31u<<11},
        {1540,(31u<<16)|(31u<<11)}, {1604,(31u<<21)|(31u<<16)}, {44,1u<<10}};
    unsigned reserved_case = 0;
    for (const auto &field : reserved_fields) {
        const uint32 canonical = ((4u<<26)|(31u<<21)|(31u<<16)|(31u<<11)|field.xo) & ~field.zero;
        CHECK(nw_jit_op_supported(canonical) && nw_jit_op_verify_safe(canonical));
        CHECK(strcmp(cpu->decode(canonical)->name, "invalid") != 0);
        for (unsigned bit = 0; bit < 32; ++bit) if (field.zero & (1u<<bit)) {
            const uint32 op = canonical | (1u<<bit), start = 0x6b0000 + 64 * reserved_case++;
            CHECK(!nw_jit_op_supported(op) && !nw_jit_op_verify_safe(op));
            CHECK(!strcmp(cpu->decode(op)->name, "invalid"));
            CHECK(nw_jit_compile(&op, 1, start, start & ~0xfffu, 0, 0) == NULL);
            nw_jit_cpu shadow = {}; shadow.pc = start; shadow.cr = 0x12345678; shadow.vscr = 0x10001;
            const nw_jit_cpu before = shadow;
            CHECK(nw_jit_interp_n(&shadow, &op, 1, start) == -1);
            CHECK(!memcmp(&shadow, &before, sizeof shadow));
            nw_jit_verify_trace trace;
            shadow.verify_mem = nw_jit_verify_trace::replay; shadow.verify_context = &trace;
            nw_jit_helper_vmx(&shadow, op);
            CHECK(shadow.fault == NW_JIT_FAULT_VERIFY && trace.count == 0);
            shadow = before;
            mmu.set_msr(NW_MSR_VEC); cpu->pc() = start; cpu->cr().set(shadow.cr); cpu->vscr().set(shadow.vscr);
            const uint64 serial = cpu->exception_serial_;
            instruction(cpu, op);
            CHECK(cpu->exception_serial_ == serial + 1 && cpu->pc() == 0x700 && cpu->srr0_ == start && cpu->srr1_ == (NW_MSR_VEC | 0x80000u));
            CHECK(cpu->cr().get() == shadow.cr && cpu->vscr().get() == shadow.vscr);
        }
    }

    // Unknown major-opcode-4 slots must use architectural fallback in ON
    // as well as VERIFY. The decoder is independent of the JIT allowlist.
    for (unsigned low = 0; low < 2048; ++low)
    for (unsigned field : {21u,16u,11u})
    for (unsigned value = 0; value < 32; ++value) {
        const uint32 op = (4u<<26) | low | (value<<field);
        bool known = strcmp(cpu->decode(op)->name, "invalid") != 0;
        if ((op & 63u) == 44 && (op & 1024u)) known = false;
        CHECK(bool(nw_jit_op_supported(op)) == known);
        CHECK(bool(nw_jit_op_verify_safe(op)) == known);
    }
    for (unsigned engine = 0; engine < 2; ++engine) {
        const uint32 start = 0x6a0000 + engine * 64, op = (4u<<26)|2047u;
        mmu.reset(); mmu.set_msr(NW_MSR_VEC); cpu->pc() = start;
        cpu->cr().set(0x12345678); cpu->xer().set(0xe1234567);
        nw_jit_cpu shadow = {}; shadow.pc = start; shadow.host = cpu; shadow.msr = mmu.msr();
        shadow.cr = cpu->cr().get(); shadow.xer = cpu->xer().get();
        const uint64 serial = cpu->exception_serial_;
        nw_jit_fn fn = engine ? nw_jit_compile(&op, 1, start, start & ~0xfffu, 0, 0) : NULL;
        if (fn) { nw_jit_cpu_bind(&shadow); nw_jit_tail_begin(); fn(&shadow); }
        else instruction(cpu, op);
        CHECK(cpu->exception_serial_ == serial + 1);
        CHECK(cpu->pc() == 0x700 && cpu->srr0_ == start && cpu->srr1_ == (NW_MSR_VEC | 0x80000u));
        CHECK(cpu->cr().get() == 0x12345678 && cpu->xer().get() == 0xe1234567);
    }

    // An adversarial production callback proves that all formerly delegated
    // integer and FP forms avoid the live CPU in production/private replay.
    nw_jit_set_host_vmx(forbidden_vmx_callback); delegated_vmx_calls = 0;
    for (unsigned index = 41; index < sizeof vector_forms / sizeof vector_forms[0] + sizeof vector_fp_forms / sizeof vector_fp_forms[0]; ++index)
    for (unsigned verify = 0; verify < 2; ++verify) {
        const auto &form = index < sizeof vector_forms / sizeof vector_forms[0] ? vector_forms[index] :
            vector_fp_forms[index - sizeof vector_forms / sizeof vector_forms[0]];
        uint32 op = (4u<<26)|(3u<<21)|(1u<<16)|(2u<<11)|form.xo|(form.form == 5 ? 4u<<6 : 0);
        if (form.form == 7) op &= ~(31u<<16);
        nw_jit_verify_trace trace;
        nw_jit_cpu shadow = {}; shadow.pc = 0x2f0000; shadow.host = cpu;
        shadow.verify_context = &trace;
        if (verify) shadow.verify_mem = nw_jit_verify_trace::replay;
        const uint32 live_cr = cpu->cr().get(), live_vscr = cpu->vscr().get(), live_pc = cpu->pc(), live_msr = mmu.msr();
        uint32 live_vr[32][4];
        for (unsigned r = 0; r < 32; ++r) for (unsigned w = 0; w < 4; ++w) live_vr[r][w] = cpu->vr(r).w[w];
        nw_jit_helper_vmx(&shadow, op);
        CHECK(!delegated_vmx_calls && !shadow.fault && trace.complete());
        CHECK(cpu->cr().get() == live_cr && cpu->vscr().get() == live_vscr && cpu->pc() == live_pc && mmu.msr() == live_msr);
        for (unsigned r = 0; r < 32; ++r) for (unsigned w = 0; w < 4; ++w) CHECK(cpu->vr(r).w[w] == live_vr[r][w]);
    }
    nw_jit_set_host_vmx(powerpc_cpu::jit_host_vmx);

    // ISA literal boundaries for private integer and FP VMX. These expectations do
    // not call either implementation to compute results. Each destination
    // aliases A, B, C or neither; sources remain distinct.
    struct vector_literal { const char *name; uint32 xo, a[4], b[4], c[4], out[4], sat, va, cr, nj = 1, scale = 1; };
    std::vector<vector_literal> vector_literals = {
        {"vaddcuw", 384, {0xffffffffu, 0x80000000u, 0x00000000u, 0x7fffffffu}, {0x00000001u, 0x80000000u, 0xffffffffu, 0x00000001u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x00000001u, 0x00000001u, 0x00000000u, 0x00000000u}, 0, 0, 0x12345678u},
        {"vsubcuw", 1408, {0xffffffffu, 0x80000000u, 0x00000000u, 0x7fffffffu}, {0x00000001u, 0x80000000u, 0xffffffffu, 0x00000001u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x00000001u, 0x00000001u, 0x00000000u, 0x00000001u}, 0, 0, 0x12345678u},
        {"vaddubs", 512, {0xff017f80u, 0xff017f80u, 0xff017f80u, 0xff017f80u}, {0x01018080u, 0x01018080u, 0x01018080u, 0x01018080u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0xff02ffffu, 0xff02ffffu, 0xff02ffffu, 0xff02ffffu}, 1, 0, 0x12345678u},
        {"vsububs", 1536, {0x010080ffu, 0x010080ffu, 0x010080ffu, 0x010080ffu}, {0x02010101u, 0x02010101u, 0x02010101u, 0x02010101u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x00007ffeu, 0x00007ffeu, 0x00007ffeu, 0x00007ffeu}, 1, 0, 0x12345678u},
        {"vaddsbs", 768, {0x7f807f80u, 0x7f807f80u, 0x7f807f80u, 0x7f807f80u}, {0x01ff8001u, 0x01ff8001u, 0x01ff8001u, 0x01ff8001u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x7f80ff81u, 0x7f80ff81u, 0x7f80ff81u, 0x7f80ff81u}, 1, 0, 0x12345678u},
        {"vaddshs", 832, {0x7fff8000u, 0x7fff8000u, 0x7fff8000u, 0x7fff8000u}, {0x0001ffffu, 0x0001ffffu, 0x0001ffffu, 0x0001ffffu}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x7fff8000u, 0x7fff8000u, 0x7fff8000u, 0x7fff8000u}, 1, 0, 0x12345678u},
        {"vaddsws", 896, {0x7fffffffu, 0x80000000u, 0xffffffffu, 0x00000000u}, {0x00000001u, 0xffffffffu, 0x00000001u, 0x80000000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x7fffffffu, 0x80000000u, 0x00000000u, 0x80000000u}, 1, 0, 0x12345678u},
        {"vsubsws", 1920, {0x80000000u, 0x7fffffffu, 0x00000000u, 0xffffffffu}, {0x00000001u, 0xffffffffu, 0x80000000u, 0x7fffffffu}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x80000000u, 0x7fffffffu, 0x7fffffffu, 0x80000000u}, 1, 0, 0x12345678u},
        {"vavgsb", 1282, {0x80817ffeu, 0x80817ffeu, 0x80817ffeu, 0x80817ffeu}, {0xff017f01u, 0xff017f01u, 0xff017f01u, 0xff017f01u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0xc0c17f00u, 0xc0c17f00u, 0xc0c17f00u, 0xc0c17f00u}, 0, 0, 0x12345678u},
        {"vavguw", 1154, {0xffffffffu, 0x00000000u, 0xffffffffu, 0x00000001u}, {0xffffffffu, 0x00000001u, 0x00000000u, 0x00000000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0xffffffffu, 0x00000001u, 0x80000000u, 0x00000001u}, 0, 0, 0x12345678u},
        {"vcmpgtsw.", 1926, {0x80000000u, 0x00000001u, 0x7fffffffu, 0x00000000u}, {0x00000000u, 0x00000000u, 0x7fffffffu, 0xffffffffu}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x00000000u, 0xffffffffu, 0x00000000u, 0xffffffffu}, 0, 0, 0x12345608u},
        {"vcmpequh.", 1094, {0xabcd0001u, 0xabcd0001u, 0xabcd0001u, 0xabcd0001u}, {0xabcd0002u, 0xabcd0002u, 0xabcd0002u, 0xabcd0002u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0xffff0000u, 0xffff0000u, 0xffff0000u, 0xffff0000u}, 0, 0, 0x12345608u},
        {"vrlb", 4, {0x81ff0180u, 0x81ff0180u, 0x81ff0180u, 0x81ff0180u}, {0x01010101u, 0x01010101u, 0x01010101u, 0x01010101u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x03ff0201u, 0x03ff0201u, 0x03ff0201u, 0x03ff0201u}, 0, 0, 0x12345678u},
        {"vrlh", 68, {0x80010001u, 0x80010001u, 0x80010001u, 0x80010001u}, {0x00010001u, 0x00010001u, 0x00010001u, 0x00010001u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x00030002u, 0x00030002u, 0x00030002u, 0x00030002u}, 0, 0, 0x12345678u},
        {"vrlw", 132, {0x80000001u, 0x12345678u, 0xffffffffu, 0x00000001u}, {0x00000000u, 0x00000004u, 0x0000001fu, 0x0000001fu}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x80000001u, 0x23456781u, 0xffffffffu, 0x80000000u}, 0, 0, 0x12345678u},
        {"vsrab", 772, {0x807fff01u, 0x807fff01u, 0x807fff01u, 0x807fff01u}, {0x07070101u, 0x07070101u, 0x07070101u, 0x07070101u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0xff00ff00u, 0xff00ff00u, 0xff00ff00u, 0xff00ff00u}, 0, 0, 0x12345678u},
        {"vsrah", 836, {0x80007fffu, 0x80007fffu, 0x80007fffu, 0x80007fffu}, {0x000f000fu, 0x000f000fu, 0x000f000fu, 0x000f000fu}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0xffff0000u, 0xffff0000u, 0xffff0000u, 0xffff0000u}, 0, 0, 0x12345678u},
        {"vmsumuhs", 39, {0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu}, {0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu}, 1, 1, 0x12345678u},
        {"vmhaddshs", 32, {0x80008000u, 0x80008000u, 0x80008000u, 0x80008000u}, {0x80008000u, 0x80008000u, 0x80008000u, 0x80008000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x7fff7fffu, 0x7fff7fffu, 0x7fff7fffu, 0x7fff7fffu}, 1, 1, 0x12345678u},
        {"vmhraddshs", 33, {0x00010001u, 0x00010001u, 0x00010001u, 0x00010001u}, {0x40004000u, 0x40004000u, 0x40004000u, 0x40004000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x00010001u, 0x00010001u, 0x00010001u, 0x00010001u}, 0, 1, 0x12345678u},
        {"vmsummbm", 37, {0xff010080u, 0xff010080u, 0xff010080u, 0xff010080u}, {0x02020202u, 0x02020202u, 0x02020202u, 0x02020202u}, {0x00000000u, 0x00000001u, 0x00000002u, 0x00000003u}, {0xffffff00u, 0xffffff01u, 0xffffff02u, 0xffffff03u}, 0, 1, 0x12345678u},
        {"vpkshus", 270, {0x7fff8000u, 0x7fff8000u, 0x7fff8000u, 0x7fff8000u}, {0x00ff0100u, 0x00ff0100u, 0x00ff0100u, 0x00ff0100u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0xff00ff00u, 0xff00ff00u, 0xffffffffu, 0xffffffffu}, 1, 0, 0x12345678u},
        {"vpkuhus", 142, {0x010000ffu, 0x010000ffu, 0x010000ffu, 0x010000ffu}, {0x00010000u, 0x00010000u, 0x00010000u, 0x00010000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0xffffffffu, 0xffffffffu, 0x01000100u, 0x01000100u}, 1, 0, 0x12345678u},
        {"vupkhsb", 526, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x807f00ffu, 0x807f00ffu, 0x807f00ffu, 0x807f00ffu}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0xff80007fu, 0x0000ffffu, 0xff80007fu, 0x0000ffffu}, 0, 0, 0x12345678u},
        {"vupkhpx", 846, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x80017fffu, 0x80017fffu, 0x80017fffu, 0x80017fffu}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0xff000001u, 0x001f1f1fu, 0xff000001u, 0x001f1f1fu}, 0, 0, 0x12345678u},
        {"vsum2sws", 1672, {0x7fffffffu, 0x00000001u, 0x80000000u, 0xffffffffu}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x00000000u, 0x7fffffffu, 0x00000000u, 0x80000000u}, 1, 0, 0x12345678u},
        {"vsum4sbs", 1800, {0x7f7f7f7fu, 0x7f7f7f7fu, 0x7f7f7f7fu, 0x7f7f7f7fu}, {0x7fffffffu, 0x00000000u, 0xffffff00u, 0x80000000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x7fffffffu, 0x000001fcu, 0x000000fcu, 0x800001fcu}, 1, 0, 0x12345678u},
        {"vsum4ubs", 1544, {0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu}, {0xffffffffu, 0x00000000u, 0xfffffc00u, 0x00000001u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0xffffffffu, 0x000003fcu, 0xfffffffcu, 0x000003fdu}, 1, 0, 0x12345678u},
        {"vrfin ties even", 522, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3f000000u, 0xbf000000u, 0x3fc00000u, 0xc0200000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x00000000u, 0x80000000u, 0x40000000u, 0xc0000000u}, 0, 0, 0x12345678u, 0, 0},
        {"vrfin ignores NJ", 522, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3f000000u, 0xbf000000u, 0x3fc00000u, 0xc0200000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x00000000u, 0x80000000u, 0x40000000u, 0xc0000000u}, 0, 0, 0x12345678u, 1, 0},
        {"vaddfp RN and NJ", 10, {0x3f800000u, 0x4b800000u, 0xcb800000u, 0x00800000u}, {0x33800000u, 0x3f800000u, 0xbf800000u, 0x807fffffu}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3f800000u, 0x4b800000u, 0xcb800000u, 0x00000001u}, 0, 0, 0x12345678u, 0, 0},
        {"vaddfp RN and NJ", 10, {0x3f800000u, 0x4b800000u, 0xcb800000u, 0x00800000u}, {0x33800000u, 0x3f800000u, 0xbf800000u, 0x807fffffu}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3f800000u, 0x4b800000u, 0xcb800000u, 0x00800000u}, 0, 0, 0x12345678u, 1, 0},
        {"vaddfp NaN selection", 10, {0x7fc12345u, 0xff812345u, 0x7f800000u, 0xff800000u}, {0x7f811111u, 0x7fc99999u, 0xff800000u, 0x7f800000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x7fc12345u, 0xffc12345u, 0x7fc00000u, 0x7fc00000u}, 0, 0, 0x12345678u, 0, 0},
        {"vmaxfp zero and NaN", 1034, {0x00000000u, 0x80000000u, 0x7fc12345u, 0x7f800001u}, {0x80000000u, 0x00000000u, 0x3f800000u, 0x40000000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x00000000u, 0x00000000u, 0x7fc12345u, 0x7fc00001u}, 0, 0, 0x12345678u, 0, 0},
        {"vminfp zero and NaN", 1098, {0x00000000u, 0x80000000u, 0x7fc12345u, 0x7f800001u}, {0x80000000u, 0x00000000u, 0x3f800000u, 0x40000000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x80000000u, 0x80000000u, 0x7fc12345u, 0x7fc00001u}, 0, 0, 0x12345678u, 0, 0},
        {"vmaddfp fused and zero", 46, {0x3f800001u, 0x00000000u, 0x80000000u, 0x80000000u}, {0xbf800000u, 0x80000000u, 0x80000000u, 0x80000000u}, {0x3f7ffffeu, 0x40000000u, 0x40000000u, 0xc0000000u}, {0xa8800000u, 0x00000000u, 0x80000000u, 0x00000000u}, 0, 1, 0x12345678u, 0, 0},
        {"vnmsubfp fused", 47, {0x3f800001u, 0x3f800001u, 0x3f800001u, 0x3f800001u}, {0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u}, {0x3f7ffffeu, 0x3f7ffffeu, 0x3f7ffffeu, 0x3f7ffffeu}, {0x28800000u, 0x28800000u, 0x28800000u, 0x28800000u}, 0, 1, 0x12345678u, 0, 0},
        {"vctsxs limits and NaN", 970, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x7f800000u, 0xff800000u, 0x7fc12345u, 0x4f000000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x7fffffffu, 0x80000000u, 0x00000000u, 0x7fffffffu}, 1, 0, 0x12345678u, 0, 0},
        {"vctuxs limits and negative fraction", 906, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x7f800000u, 0xff800000u, 0x7fc12345u, 0xbf000000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0xffffffffu, 0x00000000u, 0x00000000u, 0x00000000u}, 1, 0, 0x12345678u, 0, 0},
        {"vctsxs scaled fractions", 970, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3fe00000u, 0xbfe00000u, 0x00000000u, 0x80000000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x00000003u, 0xfffffffdu, 0x00000000u, 0x00000000u}, 0, 0, 0x12345678u, 0, 1},
        {"vctsxs scale 31", 970, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3f800000u, 0xbf800000u, 0x7fc12345u, 0x00000000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x7fffffffu, 0x80000000u, 0x00000000u, 0x00000000u}, 1, 0, 0x12345678u, 0, 31},
        {"vcfsx rounded integers", 842, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x7fffffffu, 0x80000000u, 0xffffffffu, 0x00000000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x4f000000u, 0xcf000000u, 0xbf800000u, 0x00000000u}, 0, 0, 0x12345678u, 0, 0},
        {"vcfux scale 31", 778, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0xffffffffu, 0x80000000u, 0x00000001u, 0x00000000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x3f800000u, 0x30000000u, 0x00000000u}, 0, 0, 0x12345678u, 0, 31},
        {"vcmpbfp. mixed and NaN", 1990, {0x00000000u, 0x40000000u, 0xc0000000u, 0x7fc12345u}, {0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x00000000u, 0x80000000u, 0x40000000u, 0xc0000000u}, 0, 0, 0x12345608u, 0, 0},
        {"vcmpbfp. all within", 1990, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0, 0, 0x12345628u, 0, 0},
        {"vcmpeqfp. unordered", 1222, {0x3f800000u, 0x80000000u, 0x7fc12345u, 0x3f800000u}, {0x3f800000u, 0x00000000u, 0x7fc12345u, 0xbf800000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0xffffffffu, 0xffffffffu, 0x00000000u, 0x00000000u}, 0, 0, 0x12345608u, 0, 0},
        {"vrfip subnormal", 650, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x00000001u, 0x80000001u, 0x00000000u, 0x80000000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3f800000u, 0x80000000u, 0x00000000u, 0x80000000u}, 0, 0, 0x12345678u, 0, 0},
        {"vrfim subnormal", 714, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x00000001u, 0x80000001u, 0x00000000u, 0x80000000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x00000000u, 0xbf800000u, 0x00000000u, 0x80000000u}, 0, 0, 0x12345678u, 0, 0},
        {"vrfip subnormal", 650, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x00000001u, 0x80000001u, 0x00000000u, 0x80000000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x00000000u, 0x80000000u, 0x00000000u, 0x80000000u}, 0, 0, 0x12345678u, 1, 0},
        {"vrfim subnormal", 714, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x00000001u, 0x80000001u, 0x00000000u, 0x80000000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x00000000u, 0x80000000u, 0x00000000u, 0x80000000u}, 0, 0, 0x12345678u, 1, 0},
        {"vrefp exact and zero", 266, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x00000000u, 0x80000000u, 0x3f800000u, 0xc0000000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x7f800000u, 0xff800000u, 0x3f800000u, 0xbf000000u}, 0, 0, 0x12345678u, 0, 0},
        {"vrsqrtefp exact and invalid", 330, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x00000000u, 0x80000000u, 0x40800000u, 0xbf800000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x7f800000u, 0xff800000u, 0x3f000000u, 0x7fc00000u}, 0, 0, 0x12345678u, 0, 0},
        {"vexptefp exact and infinity", 394, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0xff800000u, 0x80000000u, 0x00000000u, 0x7f800000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x00000000u, 0x3f800000u, 0x3f800000u, 0x7f800000u}, 0, 0, 0x12345678u, 0, 0},
        {"vlogefp exact and invalid", 458, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x00000000u, 0x3f800000u, 0x40000000u, 0xbf800000u}, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0xff800000u, 0x00000000u, 0x3f800000u, 0x7fc00000u}, 0, 0, 0x12345678u, 0, 0},
    };
    // Enumerate every architectural immediate value, with expected lane
    // constants or exact powers of two independent of either implementation.
    const uint32 splat_words[] = {0x00112233,0x44556677,0x8899aabb,0xccddeeff};
    const uint32 splat_values[][16] = {
        {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff},
        {0x0011,0x2233,0x4455,0x6677,0x8899,0xaabb,0xccdd,0xeeff},
        {0x00112233,0x44556677,0x8899aabb,0xccddeeff}};
    for (unsigned width = 0; width < 3; ++width) {
        for (unsigned simm = 0; simm < 32; ++simm) {
            vector_literal test = {}; test.name = "SIMM full range"; test.xo = 780 + width * 64;
            test.cr = 0x12345678; test.nj = 1; test.scale = simm;
            const uint32 value = uint32(simm < 16 ? int(simm) : int(simm) - 32);
            const uint32 word = width == 0 ? (value & 255) * 0x01010101u : width == 1 ? (value & 65535) * 0x00010001u : value;
            for (unsigned w = 0; w < 4; ++w) test.out[w] = word;
            vector_literals.push_back(test);
        }
        for (unsigned lane = 0; lane < (16u>>width); ++lane) {
            vector_literal test = {}; test.name = "splat index full range"; test.xo = 524 + width * 64;
            test.cr = 0x12345678; test.nj = 1; test.scale = lane;
            const uint32 value = splat_values[width][lane];
            for (unsigned w = 0; w < 4; ++w) { test.b[w] = splat_words[w];
                test.out[w] = width == 0 ? value * 0x01010101u : width == 1 ? value * 0x00010001u : value; }
            vector_literals.push_back(test);
        }
    }
    for (unsigned shb = 0; shb < 16; ++shb) {
        vector_literal test = {}; test.name = "vsldoi SHB full range"; test.xo = 44;
        test.cr = 0x12345678; test.nj = 1; test.scale = shb;
        for (unsigned w = 0; w < 4; ++w) {
            test.a[w] = w * 0x04040404u + 0x00010203u;
            test.b[w] = 0x10111213u + w * 0x04040404u;
            test.out[w] = (shb + 4 * w) * 0x01010101u + 0x00010203u;
        }
        vector_literals.push_back(test);
    }
    for (unsigned scale = 0; scale < 32; ++scale)
    for (unsigned form = 0; form < 8; ++form) {
        vector_literal test = {}; test.name = "FP conversion UIMM full range";
        const unsigned xos[] = {842,778,970,906,970,906,970,906}; test.xo = xos[form];
        test.cr = 0x12345678; test.scale = scale;
        if (form < 2) {
            const uint32 inputs[] = {0x40000000,0xc0000000,0x7fffffff,0xffffffff};
            const uint32 fp[][4] = {{0x4e800000,0xce800000,0x4f000000,0xbf800000}, {0x4e800000,0x4f400000,0x4f000000,0x4f800000}};
            for (unsigned w = 0; w < 4; ++w) { test.b[w] = inputs[w]; test.out[w] = fp[form][w] - (scale<<23); }
        } else if (form < 4) {
            test.b[0] = 0x30000000; test.b[1] = 0xb0000000; test.b[2] = 0x3f800000; test.b[3] = 0xbf000000;
            test.out[0] = scale == 31 ? 1 : 0;
            test.out[1] = form == 2 && scale == 31 ? UINT32_MAX : 0;
            test.out[2] = form == 2 && scale == 31 ? 0x7fffffffu : 1u<<scale;
            test.out[3] = form == 2 && scale ? 0u - (1u<<(scale-1)) : 0;
            test.sat = form == 3 || scale == 31;
        } else if (form < 6) {
            test.b[0] = 0x7f800000; test.b[1] = 0xff800000; test.b[2] = 0x7fc12345; test.b[3] = 0x80000000;
            test.out[0] = form == 4 ? 0x7fffffff : UINT32_MAX; test.out[1] = form == 4 ? 0x80000000 : 0; test.sat = 1;
        } else {
            // Very large finite inputs must saturate before an integer cast.
            test.b[0] = 0x7f7fffff; test.b[1] = 0xff7fffff; test.b[2] = 0x7f800001; test.b[3] = 0;
            test.out[0] = form == 6 ? 0x7fffffff : UINT32_MAX; test.out[1] = form == 6 ? 0x80000000 : 0; test.sat = 1;
        }
        vector_literals.push_back(test);
    }
    unsigned vector_literal_case = 0;
    for (const auto &test : vector_literals)
    for (unsigned vd : {1u, 2u, 3u, 4u})
    for (unsigned sticky = 0; sticky < 2; ++sticky) {
        const uint32 start = 0x280000 + 64 * vector_literal_case++;
        const uint32 op = (4u<<26)|(vd<<21)|(1u<<16)|(2u<<11)|test.xo|(test.va ? 4u<<6 : 0);
        // Unary unpack/FP encodings reserve vA; conversions use it as UIMM.
        uint32 encoded = op;
        if (test.xo == 526 || test.xo == 846 || test.xo == 522 || test.xo == 714 || test.xo == 650 || test.xo == 586 ||
            test.xo == 266 || test.xo == 330 || test.xo == 394 || test.xo == 458) encoded &= ~(31u<<16);
        if (test.xo == 842 || test.xo == 778 || test.xo == 970 || test.xo == 906 ||
            test.xo == 780 || test.xo == 844 || test.xo == 908 || test.xo == 524 || test.xo == 588 || test.xo == 652)
            encoded = (encoded & ~(31u<<16)) | (test.scale<<16);
        if (test.xo == 780 || test.xo == 844 || test.xo == 908) encoded &= ~(31u<<11);
        if (test.xo == 44) encoded = (encoded & ~(31u<<6)) | (test.scale<<6);
        mmu.reset(); mmu.set_msr(NW_MSR_VEC); nw_jit_set_mode(NW_JIT_ON);
        nw_jit_fn fn = nw_jit_compile(&encoded, 1, start, start & ~0xfffu, 0, 0); CHECK(fn != NULL);
        nw_jit_cpu input = {}; input.pc = start; input.msr = NW_MSR_VEC;
        input.cr = 0x12345678; input.xer = 0xe1234567; input.vscr = (test.nj<<16) | sticky;
        for (unsigned w = 0; w < 4; ++w) {
            input.vr[1][w] = test.a[w]; input.vr[2][w] = test.b[w]; input.vr[4][w] = test.c[w];
        }
        auto prepare_literal = [&]() {
            cpu->pc() = start; cpu->last_fetch_pa_ = start; cpu->vscr().set(input.vscr);
            cpu->cr().set(input.cr); cpu->xer().set(input.xer); cpu->fpscr() = 0;
            cpu->lr() = cpu->ctr() = 0; cpu->regs().reserve_valid = 0;
            for (unsigned r = 0; r < 32; ++r) {
                cpu->gpr(r) = 0; cpu->fpr_dw(r) = 0;
                for (unsigned w = 0; w < 4; ++w) cpu->vr(r).w[w] = input.vr[r][w];
            }
        };
        for (unsigned engine = 0; engine < 4; ++engine) {
            nw_jit_cpu result = input;
            if (engine == 0) {
                prepare_literal(); instruction(cpu, encoded);
                result.cr = cpu->cr().get(); result.vscr = cpu->vscr().get(); result.pc = cpu->pc();
                for (unsigned r = 0; r < 32; ++r) for (unsigned w = 0; w < 4; ++w) result.vr[r][w] = cpu->vr(r).w[w];
            } else if (engine == 1) CHECK(nw_jit_interp_n(&result, &encoded, 1, start) == 0);
            else if (engine == 2) { nw_jit_cpu_bind(&result); nw_jit_tail_begin(); if (fn) fn(&result); }
            else {
                prepare_literal(); result.dec = cpu->dec_;
                const uint64 misses = nw_jit_verify_misses();
                if (fn) CHECK(cpu->nw_jit_verify_block(result, fn, &encoded, 1) == 1);
                CHECK(nw_jit_verify_misses() == misses);
            }
            if (result.cr != test.cr || result.vscr != ((test.nj<<16) | sticky | test.sat))
                fprintf(stderr, "literal %s vd=%u engine=%u CR/VSCR=%08x/%08x\n", test.name, vd, engine, result.cr, result.vscr);
            CHECK(result.cr == test.cr && result.vscr == ((test.nj<<16) | sticky | test.sat));
            CHECK(result.pc == start + 4 && !result.fault && result.xer == input.xer);
            for (unsigned r = 0; r < 32; ++r) for (unsigned w = 0; w < 4; ++w) {
                const uint32 expected = r == vd ? test.out[w] : input.vr[r][w];
                if (result.vr[r][w] != expected)
                    fprintf(stderr, "literal %s vd=%u engine=%u r%u.w%u=%08x/%08x\n", test.name, vd, engine, r, w, result.vr[r][w], expected);
                CHECK(result.vr[r][w] == expected);
            }
        }
    }

    // Literal saturated outputs and SAT set/retained/cleared behavior cannot
    // be certified solely by matching the interpreter's helper calculations.
    for (unsigned kind = 0; kind < 3; ++kind) {
        mmu.reset(); mmu.set_msr(NW_MSR_VEC); nw_jit_set_mode(NW_JIT_VERIFY);
        nw_jit_cpu input = {}; input.pc = 0x160000 + kind * 64; input.msr = NW_MSR_VEC;
        input.vscr = 0x10000; input.cr = 0x12345678;
        for (unsigned w = 0; w < 4; ++w) {
            input.vr[1][w] = kind == 0 ? 0x7fffffff : 0x80000000;
            input.vr[2][w] = kind == 0 ? 0 : kind == 1 ? 0x80008000 : 0x7fffffff;
            if (kind == 1) input.vr[1][w] = 0x80008000;
        }
        input.vr[7][3] = 0x10000;
        const uint32 op = (4u<<26)|(3u<<21)|(1u<<16)|(2u<<11)|(kind == 0 ? 1928u : kind == 1 ? (4u<<6)|41u : 462u);
        const uint32 ops[] = {op, nw_ppc_mfvscr(5), ((4u<<26)|(6u<<21)|1156u), nw_ppc_mtvscr(7), nw_ppc_mfvscr(8)};
        nw_jit_fn fn = nw_jit_compile(ops, 5, input.pc, input.pc & ~0xfffu, 0, 0); CHECK(fn != NULL);
        for (unsigned engine = 0; engine < 2; ++engine) {
            nw_jit_cpu shadow = input;
            if (engine == 0) { nw_jit_cpu_bind(&shadow); nw_jit_tail_begin(); if (fn) fn(&shadow); }
            else {
                cpu->pc() = input.pc; cpu->last_fetch_pa_ = input.pc; cpu->cr().set(input.cr);
                cpu->xer().set(0); cpu->vscr().set(input.vscr); cpu->fpscr() = 0;
                cpu->lr() = cpu->ctr() = 0; cpu->regs().reserve_valid = 0;
                for (unsigned r = 0; r < 32; ++r) {
                    cpu->gpr(r) = 0; cpu->fpr_dw(r) = 0;
                    for (unsigned w = 0; w < 4; ++w) cpu->vr(r).w[w] = input.vr[r][w];
                }
                shadow.dec = cpu->dec_;
                const uint64 misses = nw_jit_verify_misses();
                if (fn) CHECK(cpu->nw_jit_verify_block(shadow, fn, ops, 5) == 1);
                CHECK(nw_jit_verify_misses() == misses);
            }
            CHECK(shadow.vscr == 0x10000 && shadow.vr[5][3] == 0x10001 && shadow.vr[8][3] == 0x10000);
            for (unsigned w = 0; w < 4; ++w) {
                const uint32 expected = kind == 0 ? (w == 3 ? 0x7fffffffu : 0) : kind == 1 ? 0x7fffffffu : w < 2 ? 0x80008000u : 0x7fff7fffu;
                CHECK(shadow.vr[3][w] == expected);
            }
            CHECK(shadow.cr == input.cr && shadow.pc == input.pc + 20 && !shadow.fault);
        }
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
        // The deliberately poisoned supervisor entry contains a stack pointer;
        // remove it before this fixture ends and later production stores run.
        nw_jit_dtlb_drop_page(cpu->gpr(4),NW_JIT_DTLB_FL_RESET);
    }

    // Real RAM store/load replay, translated aliases, update/reversed/narrow
    // forms and FP memory. Only KPX owns the mapped memory; replay sees tape.
    const uint32 ram_base = 0x10000000u;
    void *const wanted = (void *)(VMBaseDiff + ram_base);
    void *const ram = mmap(wanted, 0x20000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    CHECK(ram == wanted);
    if (ram == wanted) {
        nw_banks_set(NW_PA_RAM, ram_base, 0x20000);
        auto vector_start = [&](uint32 start, uint32 ea, bool zero_ra) {
            nw_jit_cpu input = {}; input.pc = start; input.msr = mmu.msr(); input.dec = cpu->dec_;
            input.cr = 0x12345678; input.xer = 0xe1234567; input.vscr = 0x10000;
            for (unsigned r = 0; r < 32; ++r) {
                input.gpr[r] = 0x12340000u + r;
                for (unsigned w = 0; w < 4; ++w) input.vr[r][w] = 0x01020304u + 0x10101010u * r + 0x04040404u * w;
            }
            input.gpr[0] = 0xdeadbeef; input.gpr[4] = zero_ra ? 0x98765432u : ea;
            input.gpr[6] = zero_ra ? ea : 0;
            cpu->pc() = start; cpu->last_fetch_pa_ = start; cpu->cr().set(input.cr); cpu->xer().set(input.xer);
            cpu->vscr().set(input.vscr); cpu->fpscr() = 0; cpu->lr() = cpu->ctr() = 0;
            cpu->regs().reserve_valid = 0; cpu->spcflags().init(); cpu->dec_pending_ = false; cpu->dec_tick_div_ = 0;
            for (unsigned r = 0; r < 32; ++r) {
                cpu->gpr(r) = input.gpr[r]; cpu->fpr_dw(r) = 0;
                for (unsigned w = 0; w < 4; ++w) cpu->vr(r).w[w] = input.vr[r][w];
            }
            return input;
        };
        // Enter the actual interpreter loop and execute one instruction at a
        // privately mapped high-prefix exception vector. Its device store
        // requests a stop only after exception entry and handler execution.
        // Prefix blocks exercise both native-tail and C successor gates.
        const uint32 vector_page = 0xfff00000u;
        void *const vector_wanted = (void *)(VMBaseDiff + vector_page);
        void *const vectors = mmap(vector_wanted, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        CHECK(vectors == vector_wanted);
        if (vectors == vector_wanted) {
            extern uint32 RAMBase, RAMSize, ROMBase;
            const uint32 saved_ram_base = RAMBase, saved_ram_size = RAMSize, saved_rom_base = ROMBase;
            RAMBase = ram_base; RAMSize = 0x20000;
            // The synthetic sparse ROM range ends just after the vector page.
            ROMBase = vector_page - 0x4ff000u;
            nw_banks_set(NW_PA_ROM, vector_page, 4096);
            vm_write_memory_4(vector_page + 0xf20, nw_ppc_addi(27,0,0x5a5a));
            vm_write_memory_4(vector_page + 0x700, nw_ppc_addi(27,0,0x5a5a));
            vm_write_memory_4(vector_page + 0xf24, nw_ppc_stw(27,30,0));
            vm_write_memory_4(vector_page + 0x704, nw_ppc_stw(27,30,0));
            vm_write_memory_4(vector_page + 0x300, nw_ppc_addi(27,0,0x5a5a));
            vm_write_memory_4(vector_page + 0x304, nw_ppc_stw(27,30,0));
            device.on_write = ppc_core_test_access::stop_on_device; device.on_write_context = cpu;
            struct vector_trap { uint32 op, vector, cause; bool vec; unsigned width; bool store; };
            std::vector<vector_trap> trapped = {
                {(4u<<26)|(3u<<21)|(1u<<16)|(2u<<11),0xf20,0,false,0,false},
                {(4u<<26)|(3u<<21)|(1u<<16)|(2u<<11)|10u,0xf20,0,false,0,false},
                {(4u<<26)|(3u<<21)|1540u,0xf20,0,false,0,false},
                {(4u<<26)|(2u<<11)|1604u,0xf20,0,false,0,false},
                {(31u<<26)|(3u<<21)|(4u<<16)|(6u<<11)|(6u<<1),0xf20,0,false,0,false},
                {(31u<<26)|(3u<<21)|(4u<<16)|(6u<<11)|(38u<<1),0xf20,0,false,0,false}
            };
            for (bool vec : {false,true}) {
                trapped.push_back({(4u<<26)|2047u,0x700,0x80000,vec,0,false});
                trapped.push_back({(4u<<26)|(3u<<21)|(1u<<16)|(2u<<11)|522u,0x700,0x80000,vec,0,false});
            }
            const struct { unsigned xo, width; bool store; } memory_traps[] = {
                {7,1,false},{39,2,false},{71,4,false},{103,16,false},{359,16,false},
                {135,1,true},{167,2,true},{199,4,true},{231,16,true},{487,16,true}
            };
            for (const auto &form : memory_traps) for (bool vec : {false,true})
                trapped.push_back({(31u<<26)|(3u<<21)|(4u<<16)|(6u<<11)|(form.xo<<1),
                    vec ? 0x300u : 0xf20u,0,vec,form.width,form.store});
            unsigned exception_case = 0;
            for (int mode : {NW_JIT_OFF,NW_JIT_ON,NW_JIT_VERIFY})
            for (unsigned prefix = 0; prefix < 2; ++prefix)
            for (unsigned native_tail = 0; native_tail < 2; ++native_tail)
            for (unsigned translated = 0; translated < 2; ++translated)
            for (unsigned user = 0; user < 2; ++user)
            for (unsigned which = 0; which < trapped.size(); ++which) {
                const vector_trap &trap = trapped[which];
                if (trap.vector == 0x300 && !translated) continue; // DSI requires data translation.
                const uint32 start = ram_base + 0x8000 + 64 * exception_case++;
                CHECK(start + 0x28 < ram_base + 0x20000);
                mmu.reset();
                const bool illegal = trap.vector == 0x700;
                const uint32 msr = ppc32_mmu::MSR_IP | ppc32_mmu::MSR_EE | 0x00001000u | // MSR[ME], retained on exception entry
                    (user ? ppc32_mmu::MSR_PR : 0) | (trap.vec ? NW_MSR_VEC : 0) |
                    (translated ? ppc32_mmu::MSR_IR | ppc32_mmu::MSR_DR : 0);
                mmu.set_msr(msr);
                if (translated) mmu.set_ibat(0, ram_base | 3u, ram_base | 2u);
                // Unavailable suppresses both missing translations and mapped
                // destructive devices. Enabled vectors fault before any data access.
                const uint32 ea = user && trap.vector == 0xf20 ? NW_IO_VIA_PMU_BASE + 15u : 0x6000000fu;
                if (translated && ea != 0x6000000fu)
                    mmu.set_dbat(0, (NW_IO_VIA_PMU_BASE & 0xfffe0000u) | 3u, (NW_IO_VIA_PMU_BASE & 0xfffe0000u) | 2u);
                nw_jit_cpu before = vector_start(start, ea, false);
                cpu->dar_ = 0x1234567u; cpu->dsisr_ = 0x76543210u;
                cpu->gpr(30) = before.gpr[30] = NW_IO_VIA_PMU_BASE;
                const uint32 fault_pc = start + (prefix ? 0x20u : 0);
                vm_write_memory_4(fault_pc, trap.op);
                vm_write_memory_4(fault_pc + 4, nw_ppc_addi(28,0,99));
                if (prefix) {
                    const uint32 ops[] = {nw_ppc_addi(3,3,7),nw_ppc_b(0x1c,0)};
                    vm_write_memory_4(start, ops[0]); vm_write_memory_4(start + 4, ops[1]);
                    nw_jit_set_mode(NW_JIT_ON);
                    const uint32 key = (translated ? 3u : 0) | (user ? 4u : 0);
                    CHECK(nw_jit_compile(ops, 2, start, start & ~0xfffu, key, 0) != NULL);
                    CHECK((nw_jit_compile(&trap.op, 1, fault_pc, fault_pc & ~0xfffu, key, 0) == NULL) == illegal);
                    nw_jit_itlb_fill(fault_pc, fault_pc);
                }
                cpu->dec_ = 1000000; cpu->dec_tb_base_ = cpu->tb_host_ticks(); cpu->dec_pending_ = false;
                const uint64 serial = cpu->exception_serial_;
                const unsigned writes = device.writes, reads = device.reads;
                nw_jit_set_host_chain(native_tail ? powerpc_cpu::jit_host_chain : NULL);
                nw_jit_set_mode(mode);
                cpu->execute_depth = 1; cpu->execute(start); CHECK(cpu->execute_depth == 1); cpu->execute_depth = 0;
                const uint32 vec = vector_page + trap.vector;
                CHECK(cpu->exception_serial_ == serial + 1);
                if (cpu->pc() != vec + 8 || cpu->gpr(27) != 0x5a5a)
                    fprintf(stderr, "exception fixture mode=%d translated=%u user=%u which=%u pc=%08x srr=%08x/%08x r27=%08x\n", mode, translated, user, which, cpu->pc(), cpu->srr0_, cpu->srr1_, cpu->gpr(27));
                CHECK(cpu->pc() == vec + 8 && cpu->srr0_ == fault_pc && cpu->srr1_ == ((msr & ~0x783f0000u) | trap.cause));
                if (trap.vector == 0x300) {
                    CHECK(cpu->dar_ == (ea & ~(trap.width - 1u)));
                    CHECK(cpu->dsisr_ == (0x40000000u | (trap.store ? 0x02000000u : 0)));
                } else CHECK(cpu->dar_ == 0x1234567u && cpu->dsisr_ == 0x76543210u);
                CHECK(mmu.msr() == (msr & ~ppc32_mmu::MSR_EXC_CLEAR));
                CHECK(cpu->cr().get() == before.cr && cpu->xer().get() == before.xer && cpu->vscr().get() == before.vscr && !cpu->fpscr());
                for (unsigned r = 0; r < 32; ++r) {
                    CHECK(cpu->gpr(r) == (r == 27 ? 0x5a5au : r == 3 && prefix ? before.gpr[r] + 7 : before.gpr[r])); CHECK(!cpu->fpr_dw(r));
                    for (unsigned w = 0; w < 4; ++w) CHECK(cpu->vr(r).w[w] == before.vr[r][w]);
                }
                CHECK(device.writes == writes + 1 && device.reads == reads && device.value == 0x5a5a);
                CHECK(!cpu->dec_pending_ && !cpu->spcflags().test(SPCFLAG_CPU_EXEC_RETURN));
            }
            CHECK(exception_case == 1200);

            // Pending interrupts precede the instruction's synchronous fault;
            // masked requests survive it. A negative DEC write must commit
            // its integer prefix before either DEC delivery or the next fault.
            for (uint32 vec : {0x500u,0x900u}) {
                vm_write_memory_4(vector_page + vec,nw_ppc_addi(27,0,0x5a5a));
                vm_write_memory_4(vector_page + vec + 4,nw_ppc_stw(27,30,0));
            }
            const int saved_external = nw_io_ext_irq;
            CHECK(saved_external == 0);
            nw_jit_invalidate_all();
            unsigned event_case = 0;
            const vector_trap event_traps[] = {trapped[0],trapped[6],trapped[17]};
            for (int mode : {NW_JIT_OFF,NW_JIT_ON,NW_JIT_VERIFY})
            for (unsigned native_tail = 0; native_tail < 2; ++native_tail)
            for (unsigned translated = 0; translated < 2; ++translated)
            for (unsigned user = 0; user < 2; ++user)
            for (unsigned event = 0; event < 6; ++event)
            for (const auto &trap : event_traps) {
                const bool prefix_dec = event >= 4, ee = event == 0 || event == 1 || event == 4;
                const bool external = event == 1 || event == 3;
                if (prefix_dec && user) continue; // DEC writes are privileged.
                const uint32 start = ram_base + 0x8000 + 64 * event_case++;
                mmu.reset();
                const uint32 msr = ppc32_mmu::MSR_IP | 0x1000u | ppc32_mmu::MSR_DR |
                    (ee ? ppc32_mmu::MSR_EE : 0) | (user ? ppc32_mmu::MSR_PR : 0) |
                    (trap.vec ? NW_MSR_VEC : 0) | (translated ? ppc32_mmu::MSR_IR : 0);
                mmu.set_msr(msr);
                if (translated) mmu.set_ibat(0,ram_base | 3u,ram_base | 2u);
                nw_jit_cpu before = vector_start(start,0x6000000f,false);
                cpu->gpr(29) = before.gpr[29] = 0x80000000u;
                cpu->gpr(30) = before.gpr[30] = NW_IO_VIA_PMU_BASE;
                cpu->dar_ = 0x1234567; cpu->dsisr_ = 0x76543210;
                const uint32 fault_pc = start + (prefix_dec ? 8 : 0);
                vm_write_memory_4(fault_pc,trap.op);
                vm_write_memory_4(fault_pc + 4,nw_ppc_addi(28,0,99));
                nw_jit_set_mode(NW_JIT_ON);
                const uint32 key = (translated ? 1u : 0) | 2u | (user ? 4u : 0);
                if (prefix_dec) {
                    const uint32 ops[] = {nw_ppc_addi(3,3,7),nw_ppc_mtspr(NW_PPC_SPR_DEC,29)};
                    vm_write_memory_4(start,ops[0]); vm_write_memory_4(start + 4,ops[1]);
                    CHECK(nw_jit_compile(ops,2,start,start & ~0xfffu,key,0) != NULL);
                }
                CHECK((nw_jit_compile(&trap.op,1,fault_pc,fault_pc & ~0xfffu,key,0) == NULL) == (trap.vector == 0x700));
                nw_jit_itlb_fill(fault_pc,fault_pc);
                cpu->dec_ = 1000000; cpu->dec_tb_base_ = cpu->tb_host_ticks(); cpu->dec_pending_ = !prefix_dec;
                nw_io_ext_irq = external;
                const uint64 serial = cpu->exception_serial_;
                const unsigned writes = device.writes, reads = device.reads;
                nw_jit_set_host_chain(native_tail ? powerpc_cpu::jit_host_chain : NULL);
                nw_jit_set_mode(mode);
                cpu->execute_depth = 1; cpu->execute(start); CHECK(cpu->execute_depth == 1); cpu->execute_depth = 0;
                const uint32 vec = ee ? (external ? 0x500u : 0x900u) : trap.vector;
                CHECK(cpu->exception_serial_ == serial + 1 && cpu->pc() == vector_page + vec + 8);
                CHECK(cpu->srr0_ == fault_pc && cpu->srr1_ == ((msr & ~0x783f0000u) | (ee ? 0 : trap.cause)));
                CHECK(mmu.msr() == (msr & ~ppc32_mmu::MSR_EXC_CLEAR));
                CHECK(cpu->dec_pending_ == (!ee || external) && cpu->dec_ == (prefix_dec ? 0x80000000u : 1000000u));
                if (!ee && trap.vector == 0x300)
                    CHECK(cpu->dar_ == 0x60000000u && cpu->dsisr_ == 0x40000000u);
                else CHECK(cpu->dar_ == 0x1234567u && cpu->dsisr_ == 0x76543210u);
                CHECK(cpu->cr().get() == before.cr && cpu->xer().get() == before.xer && cpu->vscr().get() == before.vscr && !cpu->fpscr());
                for (unsigned r = 0; r < 32; ++r) {
                    CHECK(cpu->gpr(r) == (r == 27 ? 0x5a5au : r == 3 && prefix_dec ? before.gpr[r] + 7 : before.gpr[r]));
                    CHECK(!cpu->fpr_dw(r));
                    for (unsigned w = 0; w < 4; ++w) CHECK(cpu->vr(r).w[w] == before.vr[r][w]);
                }
                CHECK(device.writes == writes + 1 && device.reads == reads);
                nw_io_ext_irq = saved_external;
            }
            CHECK(event_case == 360);

            // Instruction translation must precede opcode classification and
            // any cached successor. The first BAT ends exactly at a page edge;
            // only its completed prefix may survive the next fetch's fault.
            vm_write_memory_4(vector_page + 0x400,nw_ppc_addi(27,0,0x5a5a));
            vm_write_memory_4(vector_page + 0x404,nw_ppc_stw(27,30,0));
            uint8 htab[65536] = {};
            mmu.set_physical_memory(htab,sizeof htab);
            const uint32 fetch_pc = ram_base + 0x20000, fetch_pa = ram_base + 0x4000;
            unsigned fetch_case = 0;
            for (int mode : {NW_JIT_OFF,NW_JIT_ON,NW_JIT_VERIFY})
            for (unsigned native_tail = 0; native_tail < 2; ++native_tail)
            for (unsigned user = 0; user < 2; ++user)
            for (unsigned fault = 0; fault < 5; ++fault)
            for (unsigned prefix = 0; prefix < 3; ++prefix)
            for (unsigned content = 0; content < 3; ++content)
            for (unsigned event = 0; event < 4; ++event) {
                ++fetch_case; nw_jit_invalidate_all(); nw_jit_itlb_flush(); mmu.reset(); memset(htab,0,sizeof htab);
                const bool ee = event >= 2;
                const uint32 msr = ppc32_mmu::MSR_IP | ppc32_mmu::MSR_IR |
                    (user ? ppc32_mmu::MSR_PR : 0) | (ee ? ppc32_mmu::MSR_EE : 0);
                mmu.set_msr(msr); mmu.set_ibat(0,ram_base | 3u,ram_base | 2u);
                // BAT/PTE protection, segment N, guarded PTE, or absent translation.
                if (fault == 1) mmu.set_ibat(1,fetch_pc | 3u,ram_base);
                if (fault == 2) mmu.set_sr(1,0x10000001u);
                if (fault == 3 || fault == 4) {
                    mmu.set_sr(1,fault == 3 ? 1u : 0x60000001u);
                    const unsigned pteg = ((1u ^ ((fetch_pc >> 12) & 0xffffu)) * 64u) & 0xffffu;
                    nw_be32_store(htab,pteg,0x80000000u | (1u<<7) | ((fetch_pc>>22)&63u));
                    nw_be32_store(htab,pteg + 4,fetch_pa | (fault == 3 ? 0xau : 0));
                }
                const uint32 start = fetch_pc - (prefix == 1 ? 4 : prefix == 2 ? 8 : 0);
                nw_jit_cpu before = vector_start(start,0x6000000f,false);
                cpu->gpr(30) = before.gpr[30] = NW_IO_VIA_PMU_BASE;
                cpu->dar_ = 0x1234567; cpu->dsisr_ = 0x76543210;
                cpu->dec_ = 1000000; cpu->dec_tb_base_ = cpu->tb_host_ticks(); cpu->dec_pending_ = event != 0;
                nw_io_ext_irq = event == 3;
                const uint32 target = content == 0 ? nw_ppc_addi(28,0,99) : content == 1 ? nw_ppc_lvx(3,4,6) : (4u<<26)|2047u;
                vm_write_memory_4(fetch_pa,target); vm_write_memory_4(fetch_pa + 4,nw_ppc_stw(27,30,0));
                // A compiled successor exists even though fetch permission does
                // not. Precompilation must not synthesize an ITLB entry.
                nw_jit_set_mode(NW_JIT_ON);
                const uint32 suffix = nw_ppc_addi(28,0,99);
                CHECK(nw_jit_compile(&suffix,1,fetch_pc,fetch_pa & ~0xfffu,1u | (user ? 4u : 0),0) != NULL);
                if (prefix) {
                    uint32 ops[] = {nw_ppc_addi(3,3,7),nw_ppc_b(4,0)};
                    vm_write_memory_4(start,ops[0]); if (prefix == 2) vm_write_memory_4(start + 4,ops[1]);
                    CHECK(nw_jit_compile(ops,prefix == 1 ? 1 : 2,start,start & ~0xfffu,1u | (user ? 4u : 0),0) != NULL);
                }
                uint32 ignored; CHECK(!nw_jit_itlb_lookup(fetch_pc,&ignored));
                const uint64 serial = cpu->exception_serial_, misses = nw_jit_verify_misses();
                const unsigned reads = device.reads, writes = device.writes;
                nw_jit_set_mode(mode); nw_jit_set_host_chain(native_tail ? powerpc_cpu::jit_host_chain : NULL);
                cpu->execute_depth = 1; cpu->execute(start); CHECK(cpu->execute_depth == 1); cpu->execute_depth = 0;
                const uint32 vec = ee ? (event == 3 ? 0x500u : 0x900u) : 0x400u;
                const uint32 cause = ee ? 0 : fault == 0 ? 0x40000000u : (fault == 1 || fault == 4) ? 0x08000000u : 0x10000000u;
                CHECK(cpu->exception_serial_ == serial + 1 && cpu->pc() == vector_page + vec + 8);
                CHECK(cpu->srr0_ == (ee ? start : fetch_pc) && cpu->srr1_ == ((msr & ~0x783f0000u) | cause));
                CHECK(mmu.msr() == (msr & ~ppc32_mmu::MSR_EXC_CLEAR));
                CHECK(cpu->dar_ == 0x1234567u && cpu->dsisr_ == 0x76543210u);
                CHECK(cpu->dec_pending_ == (event == 1 || event == 3));
                CHECK(cpu->cr().get() == before.cr && cpu->xer().get() == before.xer && cpu->vscr().get() == before.vscr && !cpu->fpscr());
                for (unsigned r = 0; r < 32; ++r) {
                    CHECK(cpu->gpr(r) == (r == 27 ? 0x5a5au : r == 3 && prefix && !ee ? before.gpr[r] + 7 : before.gpr[r]));
                    CHECK(!cpu->fpr_dw(r));
                    for (unsigned w = 0; w < 4; ++w) CHECK(cpu->vr(r).w[w] == before.vr[r][w]);
                }
                CHECK(device.reads == reads && device.writes == writes + 1 && nw_jit_verify_misses() == misses);
                if (fault == 3 || fault == 4) {
                    const unsigned pteg = ((1u ^ ((fetch_pc >> 12) & 0xffffu)) * 64u) & 0xffffu;
                    CHECK(nw_be32_load(htab,pteg + 4) == (fetch_pa | (fault == 3 ? 0xau : 0)));
                }
                nw_io_ext_irq = saved_external;
            }
            CHECK(fetch_case == 2160);
            mmu.set_physical_memory(NULL,0);

            // Warm translations through guest_fetch, then change the fetch
            // context through real mtmsr/rfi or exception-entry paths. Both the
            // sticky entry and a displaced table entry must reject old context.
            unsigned fetch_context_case = 0;
            for (int mode : {NW_JIT_OFF,NW_JIT_ON,NW_JIT_VERIFY})
            for (unsigned native_tail = 0; native_tail < 2; ++native_tail)
            for (unsigned writer = 0; writer < 5; ++writer)
            for (unsigned sticky = 0; sticky < 2; ++sticky) {
                ++fetch_context_case; nw_jit_invalidate_all(); nw_jit_itlb_flush(); mmu.reset();
                const uint32 msr = ppc32_mmu::MSR_IP | ppc32_mmu::MSR_IR;
                mmu.set_msr(msr); mmu.set_ibat(0,ram_base | 3u,ram_base | 2u);
                const uint32 target = writer < 2 ? 0x20000000u : vector_page + (writer == 2 ? 0x300 : writer == 3 ? 0x400 : 0x500);
                // Valid only for supervisor fetches. Exception-entry tests map
                // the logical handler to a different physical marker first.
                mmu.set_ibat(1,(target & 0xfffe0000u) | 2u,ram_base | 2u);
                const uint32 warm_pa = ram_base | (target & 0x1ffffu);
                vm_write_memory_4(warm_pa,nw_ppc_addi(28,0,99));
                vm_write_memory_4(warm_pa + 4,nw_ppc_stw(27,30,0));
                const uint32 start = ram_base + 0x8000;
                nw_jit_cpu before = vector_start(start,0x6000000f,false);
                cpu->gpr(30) = before.gpr[30] = NW_IO_VIA_PMU_BASE;
                cpu->gpr(29) = before.gpr[29] = msr | ppc32_mmu::MSR_PR;
                cpu->ctr() = target;
                cpu->dar_ = 0x1234567; cpu->dsisr_ = 0x76543210;
                cpu->dec_ = 1000000; cpu->dec_tb_base_ = cpu->tb_host_ticks();
                uint32 fetched; cpu->pc() = target;
                CHECK(cpu->guest_fetch(&fetched) && fetched == nw_ppc_addi(28,0,99));
                CHECK(cpu->last_fetch_pa_ == warm_pa);
                if (!sticky) { cpu->pc() = start; vm_write_memory_4(start,nw_ppc_addi(3,3,7)); CHECK(cpu->guest_fetch(&fetched)); }
                const uint64 serial = cpu->exception_serial_, misses = nw_jit_verify_misses();
                const unsigned reads = device.reads, writes = device.writes;
                nw_jit_set_mode(mode); nw_jit_set_host_chain(native_tail ? powerpc_cpu::jit_host_chain : NULL);
                uint32 expected_srr0, expected_srr1, expected_vec;
                if (writer < 2) {
                    vm_write_memory_4(start,nw_ppc_addi(3,3,7));
                    vm_write_memory_4(start + 4,writer == 0 ? (31u<<26)|(29u<<21)|(146u<<1) : (19u<<26)|(50u<<1));
                    vm_write_memory_4(start + 8,0x4e800420u); // bctr after mtmsr
                    cpu->srr0_ = target; cpu->srr1_ = msr | ppc32_mmu::MSR_PR;
                    expected_srr0 = target; expected_srr1 = msr | ppc32_mmu::MSR_PR | 0x40000000u; expected_vec = 0x400;
                    cpu->pc() = start;
                } else {
                    cpu->pc() = start;
                    if (writer == 2) cpu->take_data_dsi(0x60000000u,false,0x40000000u);
                    else if (writer == 3) cpu->take_isi(0x40000000u);
                    else cpu->take_external();
                    expected_srr0 = start; expected_srr1 = msr | (writer == 3 ? 0x40000000u : 0); expected_vec = writer == 2 ? 0x300 : writer == 3 ? 0x400 : 0x500;
                }
                cpu->execute_depth = 1; cpu->execute(cpu->pc()); CHECK(cpu->execute_depth == 1); cpu->execute_depth = 0;
                if (cpu->pc() != vector_page + expected_vec + 8)
                    fprintf(stderr,"fetch context case=%u mode=%d writer=%u sticky=%u pc=%08x srr=%08x/%08x\n",fetch_context_case,mode,writer,sticky,cpu->pc(),cpu->srr0_,cpu->srr1_);
                CHECK(cpu->exception_serial_ == serial + 1 && cpu->pc() == vector_page + expected_vec + 8);
                CHECK(cpu->srr0_ == expected_srr0 && cpu->srr1_ == expected_srr1);
                CHECK(mmu.msr() == (msr & ~ppc32_mmu::MSR_EXC_CLEAR));
                CHECK(cpu->dar_ == (writer == 2 ? 0x60000000u : 0x1234567u) && cpu->dsisr_ == (writer == 2 ? 0x40000000u : 0x76543210u));
                CHECK(cpu->cr().get() == before.cr && cpu->xer().get() == before.xer && cpu->vscr().get() == before.vscr && !cpu->fpscr());
                for (unsigned r = 0; r < 32; ++r) {
                    CHECK(cpu->gpr(r) == (r == 27 ? 0x5a5au : r == 3 && writer < 2 ? before.gpr[r] + 7 : before.gpr[r]));
                    CHECK(!cpu->fpr_dw(r));
                    for (unsigned w = 0; w < 4; ++w) CHECK(cpu->vr(r).w[w] == before.vr[r][w]);
                }
                CHECK(device.reads == reads && device.writes == writes + 1 && nw_jit_verify_misses() == misses);
            }
            CHECK(fetch_context_case == 60);

            // Literal encoding masks come from the published X-form diagrams,
            // independently of the shared production constraint table. All
            // register numbers and stream IDs stay valid where encoded.
            const uint32 vx31[] = {6,38,7,39,71,103,359,135,167,199,231,487};
            std::vector<uint32> invalid31;
            for (uint32 xo : vx31) invalid31.push_back((31u<<26)|(3u<<21)|(4u<<16)|(6u<<11)|(xo<<1)|1u);
            for (uint32 xo : {342u,374u,822u}) {
                const uint32 zero = xo == 822 ? 0x019ff801u : 0x01800001u;
                for (unsigned bit = 0; bit < 26; ++bit)
                    if (zero & (1u<<bit)) invalid31.push_back((31u<<26)|(xo<<1)|(1u<<bit));
            }
            CHECK(invalid31.size() == 31);
            // These malformed original-AltiVec forms use the allowed program
            // exception policy in New World. This is not a hardware profile
            // assertion about all primary-opcode-31 invalid encodings.
            unsigned invalid31_case = 0;
            for (int mode : {NW_JIT_OFF,NW_JIT_ON,NW_JIT_VERIFY})
            for (unsigned native_tail = 0; native_tail < 2; ++native_tail)
            for (unsigned user = 0; user < 2; ++user)
            for (unsigned vec = 0; vec < 2; ++vec)
            for (unsigned prefix = 0; prefix < 2; ++prefix)
            for (uint32 op : invalid31) {
                ++invalid31_case; nw_jit_invalidate_all(); nw_jit_itlb_flush(); mmu.reset();
                const uint32 msr = ppc32_mmu::MSR_IP | ppc32_mmu::MSR_IR | ppc32_mmu::MSR_DR |
                    (user ? ppc32_mmu::MSR_PR : 0) | (vec ? NW_MSR_VEC : 0);
                mmu.set_msr(msr); mmu.set_ibat(0,ram_base | 3u,ram_base | 2u);
                const uint32 start = ram_base + 0x8000, fault_pc = start + (prefix ? 8 : 0);
                nw_jit_cpu before = vector_start(start,NW_IO_VIA_PMU_BASE,false);
                cpu->gpr(30) = before.gpr[30] = NW_IO_VIA_PMU_BASE;
                mmu.set_dbat(0,(NW_IO_VIA_PMU_BASE & 0xfffe0000u) | 3u,(NW_IO_VIA_PMU_BASE & 0xfffe0000u) | 2u);
                vm_write_memory_4(fault_pc,op); vm_write_memory_4(fault_pc + 4,nw_ppc_addi(28,0,99));
                // A safe stop makes an erroneous acceptance terminate too.
                vm_write_memory_4(fault_pc + 8,nw_ppc_stw(27,30,0));
                if (prefix) {
                    vm_write_memory_4(start,nw_ppc_addi(3,3,7)); vm_write_memory_4(start + 4,nw_ppc_b(4,0));
                }
                CHECK(cpu->decode(op)->format == powerpc_cpu::INVALID_form && !nw_jit_op_supported(op));
                nw_jit_cpu rejected = before;
                CHECK(nw_jit_interp_one(&rejected,op) == -1 && !memcmp(&rejected,&before,sizeof before));
                nw_jit_set_mode(NW_JIT_ON);
                CHECK(nw_jit_compile(&op,1,fault_pc,fault_pc & ~0xfffu,3u | (user ? 4u : 0),0) == NULL);
                cpu->dar_ = 0x1234567; cpu->dsisr_ = 0x76543210;
                cpu->dec_ = 1000000; cpu->dec_tb_base_ = cpu->tb_host_ticks();
                const uint64 serial = cpu->exception_serial_, misses = nw_jit_verify_misses();
                const unsigned reads = device.reads, writes = device.writes;
                nw_jit_set_mode(mode); nw_jit_set_host_chain(native_tail ? powerpc_cpu::jit_host_chain : NULL);
                cpu->execute_depth = 1; cpu->execute(start); CHECK(cpu->execute_depth == 1); cpu->execute_depth = 0;
                CHECK(cpu->exception_serial_ == serial + 1 && cpu->pc() == vector_page + 0x708);
                CHECK(cpu->srr0_ == fault_pc && cpu->srr1_ == ((msr & ~0x783f0000u) | 0x80000u));
                CHECK(mmu.msr() == (msr & ~ppc32_mmu::MSR_EXC_CLEAR));
                CHECK(cpu->dar_ == 0x1234567u && cpu->dsisr_ == 0x76543210u);
                CHECK(cpu->cr().get() == before.cr && cpu->xer().get() == before.xer && cpu->vscr().get() == before.vscr && !cpu->fpscr());
                for (unsigned r = 0; r < 32; ++r) {
                    CHECK(cpu->gpr(r) == (r == 27 ? 0x5a5au : r == 3 && prefix ? before.gpr[r] + 7 : before.gpr[r]));
                    CHECK(!cpu->fpr_dw(r));
                    for (unsigned w = 0; w < 4; ++w) CHECK(cpu->vr(r).w[w] == before.vr[r][w]);
                }
                CHECK(device.reads == reads && device.writes == writes + 1 && nw_jit_verify_misses() == misses);
            }
            CHECK(invalid31_case == 1488);
            // Positive recognition: all 32 values in each register field,
            // both T/A variants and all four stream IDs (also with dssall).
            unsigned valid31_case = 0;
            for (uint32 xo : vx31)
            for (unsigned field : {21u,16u,11u})
            for (uint32 reg = 0; reg < 32; ++reg) {
                const uint32 op = (31u<<26)|(xo<<1)|(reg<<field);
                CHECK(cpu->decode(op)->format != powerpc_cpu::INVALID_form && nw_jit_op_supported(op)); ++valid31_case;
            }
            for (uint32 xo : {342u,374u,822u})
            for (uint32 transient = 0; transient < 2; ++transient)
            for (uint32 stream = 0; stream < 4; ++stream) {
                const uint32 op = (31u<<26)|(transient<<25)|(stream<<21)|(xo<<1)|
                    (xo == 822 ? 0 : (4u<<16)|(6u<<11));
                CHECK(cpu->decode(op)->format != powerpc_cpu::INVALID_form && nw_jit_op_supported(op)); ++valid31_case;
            }
            CHECK(valid31_case == 1176);

            // Streaming hints in the modeled no-stream implementation and
            // VRSAVE accesses do not require VEC. Unmapped touch addresses
            // cannot cause DSI; DR remains enabled as required for dst forms.
            nw_jit_invalidate_all();
            unsigned control_case = 0;
            for (int mode : {NW_JIT_OFF,NW_JIT_ON,NW_JIT_VERIFY})
            for (unsigned native_tail = 0; native_tail < 2; ++native_tail)
            for (unsigned translated = 0; translated < 2; ++translated)
            for (unsigned user = 0; user < 2; ++user)
            for (bool vec : {false,true})
            for (uint32 save : {0u,0xffffffffu,0x81234567u}) {
                const uint32 start = ram_base + 0x8000 + 128 * control_case++;
                mmu.reset();
                const uint32 msr = ppc32_mmu::MSR_IP | ppc32_mmu::MSR_DR | (vec ? NW_MSR_VEC : 0) |
                    (translated ? ppc32_mmu::MSR_IR : 0) | (user ? ppc32_mmu::MSR_PR : 0);
                mmu.set_msr(msr);
                if (translated) mmu.set_ibat(0,ram_base | 3u,ram_base | 2u);
                nw_jit_cpu before = vector_start(start,0x6000000f,false);
                cpu->gpr(29) = before.gpr[29] = save; cpu->vrsave() = ~save;
                cpu->gpr(30) = before.gpr[30] = NW_IO_VIA_PMU_BASE;
                mmu.set_dbat(0,(NW_IO_VIA_PMU_BASE & 0xfffe0000u) | 3u,(NW_IO_VIA_PMU_BASE & 0xfffe0000u) | 2u);
                const uint32 ops[] = {nw_ppc_addi(3,3,7),nw_ppc_mtspr(NW_PPC_SPR_VRSAVE,29),nw_ppc_mfspr(28,NW_PPC_SPR_VRSAVE),
                    (31u<<26)|(4u<<16)|(6u<<11)|(342u<<1),(31u<<26)|(1u<<25)|(4u<<16)|(6u<<11)|(342u<<1),
                    (31u<<26)|(4u<<16)|(6u<<11)|(374u<<1),(31u<<26)|(1u<<25)|(4u<<16)|(6u<<11)|(374u<<1),
                    (31u<<26)|(822u<<1),(31u<<26)|(1u<<25)|(822u<<1),nw_ppc_addi(27,0,0x5a5a),nw_ppc_stw(27,30,0)};
                for (unsigned i = 0; i < sizeof ops / sizeof ops[0]; ++i) vm_write_memory_4(start + 4 * i,ops[i]);
                for (unsigned i = 1; i < 9; ++i) CHECK(!powerpc_cpu::is_altivec_insn(ops[i]));
                const uint64 serial = cpu->exception_serial_;
                const unsigned writes = device.writes, reads = device.reads;
                cpu->srr0_ = 0x12345678; cpu->srr1_ = 0x87654321;
                cpu->dar_ = 0x1234567; cpu->dsisr_ = 0x76543210;
                cpu->dec_ = 1000000; cpu->dec_tb_base_ = cpu->tb_host_ticks(); cpu->dec_pending_ = false;
                nw_jit_set_host_chain(native_tail ? powerpc_cpu::jit_host_chain : NULL);
                nw_jit_set_mode(mode);
                cpu->execute_depth = 1; cpu->execute(start); CHECK(cpu->execute_depth == 1); cpu->execute_depth = 0;
                if (cpu->exception_serial_ != serial || cpu->pc() != start + sizeof ops)
                    fprintf(stderr,"control fault case=%u mode=%d tail=%u ir=%u user=%u vec=%u save=%08x pc=%08x srr=%08x/%08x r28=%08x vrsave=%08x\n",control_case,mode,native_tail,translated,user,vec,save,cpu->pc(),cpu->srr0_,cpu->srr1_,cpu->gpr(28),cpu->vrsave());
                CHECK(cpu->exception_serial_ == serial && cpu->pc() == start + sizeof ops && mmu.msr() == msr);
                CHECK(cpu->vrsave() == save && cpu->srr0_ == 0x12345678 && cpu->srr1_ == 0x87654321);
                CHECK(cpu->dar_ == 0x1234567u && cpu->dsisr_ == 0x76543210u);
                CHECK(cpu->cr().get() == before.cr && cpu->xer().get() == before.xer && cpu->vscr().get() == before.vscr && !cpu->fpscr());
                for (unsigned r = 0; r < 32; ++r) {
                    CHECK(cpu->gpr(r) == (r == 27 ? 0x5a5au : r == 28 ? save : r == 3 ? before.gpr[r] + 7 : before.gpr[r]));
                    CHECK(!cpu->fpr_dw(r));
                    for (unsigned w = 0; w < 4; ++w) CHECK(cpu->vr(r).w[w] == before.vr[r][w]);
                }
                CHECK(device.writes == writes + 1 && device.reads == reads && !cpu->dec_pending_);
            }
            CHECK(control_case == 144);
            cpu->vrsave() = 0; nw_jit_invalidate_all();
            device.on_write = NULL; device.on_write_context = NULL;
            nw_jit_set_host_chain(powerpc_cpu::jit_host_chain);
            RAMBase = saved_ram_base; RAMSize = saved_ram_size; ROMBase = saved_rom_base;
            munmap(vectors, 4096); nw_banks_set(NW_PA_ROM, 0, 0);
            nw_jit_set_mode(NW_JIT_VERIFY);
        } else if (vectors != MAP_FAILED) munmap(vectors, 4096);

        auto vector_run = [&](nw_jit_cpu input, const uint32 *ops, unsigned n, bool verify) {
            nw_jit_set_mode(verify ? NW_JIT_VERIFY : NW_JIT_ON);
            const uint32 key = mmu.msr() & ppc32_mmu::MSR_DR ? 2 : 0;
            nw_jit_fn fn = nw_jit_compile(ops, n, input.pc, cpu->last_fetch_pa_ & ~0xfffu, key, 0); CHECK(fn != NULL);
            const uint64 misses = nw_jit_verify_misses();
            if (fn) CHECK(verify ? cpu->nw_jit_verify_block(input, fn, ops, n) == 1 : cpu->nw_jit_try(ops[0]) == 1);
            CHECK(nw_jit_verify_misses() == misses);
            if (!verify) input = *cpu->nw_jc_;
            nw_jit_set_mode(NW_JIT_VERIFY);
            return input;
        };
        // P6: use the production entry/commit masks, rather than calling the
        // generated function directly. Literal outcomes also exercise live
        // exception publication and preservation of an aliased destination.
        struct scalar_commit_case { unsigned xo; uint64 source, word; uint32 causes, rounded; };
        const scalar_commit_case scalar_commit[] = {
            {14,0x3ff8000000000000ULL,2,0x02000000u,0x60000u},
            {15,0x3ff8000000000000ULL,1,0x02000000u,0x20000u},
            {14,0x41dfffffffe00000ULL,0x7fffffffu,0x100u,0}, // +2147483647.5
            {14,0x7ff8123456789abcULL,0xffffffff80000000ULL,0x100u,0},
            {14,0x7ff0123456789abcULL,0xffffffff80000000ULL,0x01000100u,0},
            {32,0x7ff8123456789abcULL,0,0x80000u,0},
            {32,0x7ff0123456789abcULL,0,0x01000000u,0},
            {0,0x7ff0123456789abcULL,0,0x01000000u,0}
        };
        fenv_t scalar_environment; fegetenv(&scalar_environment);
        unsigned scalar_commit_cases = 0;
        for (const auto &test : scalar_commit)
        for (unsigned rc : {0u,1u})
        for (unsigned alias : {0u,1u})
        for (unsigned enable : {0u,0x80u,8u})
        for (unsigned fe : {0u,0x100u,0x800u,0x900u})
        for (unsigned ip : {0u,0x40u})
        for (unsigned engine = 0; engine < 3; ++engine) {
            const bool conversion = test.xo == 14 || test.xo == 15;
            if (!conversion && (rc || alias)) continue;
            ++scalar_commit_cases;
            const uint32 start = ram_base + 0x15000, msr = 0xa000u | fe | ip;
            nw_jit_invalidate_all(); nw_jit_itlb_flush(); mmu.reset(); mmu.set_msr(msr);
            cpu->dec_ = 1000000; cpu->dec_tb_base_ = cpu->tb_host_ticks(); cpu->dec_pending_ = false;
            nw_jit_cpu before = vector_start(start,ram_base + 0x14000,false);
            before.fpscr = cpu->fpscr() = 0x65000u | enable;
            const unsigned fd = alias ? 1 : 3;
            for (unsigned r = 0; r < 32; ++r) before.fpr[r] = cpu->fpr_dw(r) = bits(double(r + 32));
            before.fpr[1] = cpu->fpr_dw(1) = test.source;
            cpu->srr0_ = 0x12345678; cpu->srr1_ = 0x87654321;
            const uint32 op = (63u<<26) | ((conversion ? fd : 5u<<2)<<21) |
                (conversion ? 1u<<11 : (1u<<16)|(2u<<11)) | (test.xo<<1) | rc;
            const uint32 ops[] = {nw_ppc_addi(3,3,7),op,nw_ppc_addi(7,0,99)};
            for (unsigned i = 0; i < 3; ++i) vm_write_memory_4(start + 4*i,ops[i]);
            uint32 causes = test.causes;
            if (test.xo == 32 && causes == 0x01000000u && !(enable & 0x80u)) causes |= 0x80000u;
            uint32 fpscr = (conversion ? before.fpscr & ~0x60000u : (before.fpscr & ~0xf000u)|0x1000u) | causes | test.rounded;
            if (causes) fpscr |= 0x80000000u;
            if (causes & 0x01080100u) fpscr |= 0x20000000u;
            if (((fpscr & 0x20000000u) && (enable & 0x80u)) || ((causes & 0x02000000u) && (enable & 8u))) fpscr |= 0x40000000u;
            const bool except = fe && (fpscr & 0x40000000u), suppressed = conversion && (causes & 0x100u) && (enable & 0x80u);
            const uint32 expected_cr = conversion ? rc ? (before.cr & ~0x0f000000u)|((fpscr>>4)&0x0f000000u) : before.cr : (before.cr & ~0xf00u)|0x100u;
            const uint64 serial = cpu->exception_serial_;
            fesetround(FE_UPWARD); feclearexcept(FE_ALL_EXCEPT); feraiseexcept(FE_DIVBYZERO);
            const int host_flags = fetestexcept(FE_ALL_EXCEPT);
            nw_jit_set_host_chain(NULL); nw_jit_set_mode(engine == 0 ? NW_JIT_OFF : engine == 1 ? NW_JIT_ON : NW_JIT_VERIFY);
            if (!engine) {
                for (uint32 instruction_op : ops) { instruction(cpu,instruction_op); if (cpu->exception_serial_ != serial) break; }
            } else (void)vector_run(before,ops,3,engine == 2);
            CHECK(cpu->exception_serial_ == serial + unsigned(except));
            CHECK(cpu->pc() == (except ? (ip ? 0xfff00700u : 0x700u) : start + 12));
            CHECK(mmu.msr() == (except ? msr & ~0x0204ef32u : msr));
            CHECK(cpu->fpscr() == fpscr && cpu->cr().get() == expected_cr);
            CHECK(cpu->srr0_ == (except ? start + 4 : 0x12345678u));
            CHECK(cpu->srr1_ == (except ? (msr & ~0x783f0000u)|0x100000u : 0x87654321u));
            CHECK(cpu->xer().get() == before.xer && !cpu->lr() && !cpu->ctr() && cpu->vscr().get() == before.vscr);
            for (unsigned r = 0; r < 32; ++r) {
                CHECK(cpu->gpr(r) == (r == 3 ? before.gpr[r] + 7 : r == 7 && !except ? 99u : before.gpr[r]));
                CHECK(cpu->fpr_dw(r) == (conversion && r == fd && !suppressed ? test.word : before.fpr[r]));
                for (unsigned w = 0; w < 4; ++w) CHECK(cpu->vr(r).w[w] == before.vr[r][w]);
            }
            CHECK(fegetround() == FE_UPWARD && fetestexcept(FE_ALL_EXCEPT) == host_flags);
        }
        CHECK(scalar_commit_cases == 1656);
        printf("P6 scalar production commit: %u literal engine cases\n",scalar_commit_cases);
        fesetenv(&scalar_environment); nw_jit_invalidate_all();
        nw_jit_set_host_chain(powerpc_cpu::jit_host_chain); nw_jit_set_mode(NW_JIT_VERIFY);
        // P4: literal system/exception expectations, with an integer prefix
        // proving instruction-PC ownership and a suffix that must be suppressed.
        unsigned system_cases = 0;
        std::vector<uint32> system_ops = {0x4c000064u,0x4c00012cu,0x44000002u};
        for (unsigned x : {83u,146u,210u,242u,595u,659u,306u,370u,566u,54u,86u,246u,278u,470u,598u,758u,854u,982u})
            system_ops.push_back((31u<<26)|(3u<<21)|(4u<<16)|(6u<<11)|(x<<1));
        for (unsigned s : {0u,4u,5u,6u,18u,19u,22u,25u,26u,27u,256u,268u,269u,272u,273u,274u,275u,284u,285u,287u,
                           528u,529u,530u,531u,532u,533u,534u,535u,536u,537u,538u,539u,540u,541u,542u,543u,
                           928u,935u,936u,937u,938u,939u,940u,941u,942u,955u,1008u,1017u,1023u,777u})
            for (unsigned x : {339u,467u}) system_ops.push_back((31u<<26)|(3u<<21)|((s&31)<<16)|((s>>5)<<11)|(x<<1));
        for (unsigned x : {371u}) for (unsigned s : {268u,269u})
            system_ops.push_back((31u<<26)|(3u<<21)|((s&31)<<16)|((s>>5)<<11)|(x<<1));
        for (unsigned to = 0; to < 32; ++to) {
            system_ops.push_back((3u<<26)|(to<<21)|(4u<<16)|0xffffu);
            system_ops.push_back((31u<<26)|(to<<21)|(4u<<16)|(6u<<11)|(4u<<1));
        }
        for (unsigned engine = 0; engine < 3; ++engine)
        for (unsigned user = 0; user < 2; ++user)
        for (unsigned ip = 0; ip < 2; ++ip)
        for (uint32 op : system_ops) {
            ++system_cases; const unsigned failed_before = failed;
            const uint32 start = ram_base + 0x15000;
            nw_jit_invalidate_page(start); nw_jit_itlb_flush(); mmu.reset();
            const uint32 msr = NW_MSR_VEC | ppc32_mmu::MSR_FP | ppc32_mmu::MSR_EE |
                               (user ? ppc32_mmu::MSR_PR : 0) | (ip ? ppc32_mmu::MSR_IP : 0);
            mmu.set_msr(msr);
            cpu->dec_ = 1000; cpu->dec_tb_base_ = cpu->tb_host_ticks(); cpu->tb_offset_ = 0;
            nw_jit_cpu input = vector_start(start,0xffffffffu,false);
            input.gpr[3] = cpu->gpr(3) = 0x55667780u;
            input.gpr[4] = cpu->gpr(4) = (op >> 26) == 3 || ((op >> 26) == 31 && ((op >> 1) & 1023) == 4) ? 0xffffffffu : ram_base + 0x14000;
            input.gpr[6] = cpu->gpr(6) = 0;
            cpu->srr0_ = start + 0x103; cpu->srr1_ = ppc32_mmu::MSR_EE | ppc32_mmu::MSR_FP;
            cpu->fpu_retry_on_ = false; cpu->cache_range.start = cpu->cache_range.end = 0;
            for (unsigned i = 0; i < 4; ++i) cpu->sprg_[i] = 0;
            cpu->dar_ = 0x12345678; cpu->dsisr_ = 0x87654321;
            for (unsigned i = 0; i < 16; ++i) mmu.set_sr(i,0x1000 + i);
            const uint32 ops[] = {nw_ppc_addi(3,3,7),op,nw_ppc_addi(7,0,99)};
            for (unsigned i = 0; i < 3; ++i) vm_write_memory_4(start + i * 4,ops[i]);
            const uint64 serial = cpu->exception_serial_, misses = nw_jit_verify_misses();
            const unsigned p = op >> 26, x = (op >> 1) & 1023, spr = ((op >> 16) & 31) | (((op >> 11) & 31) << 5);
            bool privilege = user && ((p == 19 && x == 50) || (p == 31 &&
                (x == 83 || x == 146 || x == 210 || x == 242 || x == 595 || x == 659 || x == 306 || x == 370 || x == 566 || x == 470)));
            if (p == 31 && x == 339) privilege = (user && !(spr == 256 || spr == 268 || spr == 269 || spr == 928 || (spr >= 935 && spr <= 942))) || spr == 0 || spr == 4 || spr == 5 || spr == 6;
            if (p == 31 && x == 467) privilege = (user && spr != 256) || spr == 0 || spr == 287 || spr == 955 || spr == 928 || (spr >= 935 && spr <= 942);
            // Signed -1 vs 0: LT and unsigned GT; immediate -1 additionally EQ.
            const unsigned to = (op >> 21) & 31;
            const bool trap = p == 3 ? bool(to & 4) : p == 31 && x == 4 ? bool(to & 17) : false;
            const bool exception = privilege || trap || p == 17;
            nw_jit_set_mode(engine == 2 ? NW_JIT_VERIFY : engine == 1 ? NW_JIT_ON : NW_JIT_OFF);
            if (!engine) { instruction(cpu,ops[0]); instruction(cpu,op); }
            else {
                const uint32 key = user ? 4 : 0;
                nw_jit_fn fn = nw_jit_compile(ops,2,start,start & ~0xfffu,key,0); CHECK(fn != NULL);
                if (fn) CHECK(engine == 2 ? cpu->nw_jit_verify_block(input,fn,ops,2) == 1 : cpu->nw_jit_try(ops[0]) == 1);
            }
            CHECK(nw_jit_verify_misses() == misses);
            CHECK(cpu->exception_serial_ == serial + unsigned(exception));
            const uint32 expected_pc = exception ? (ip ? 0xfff00000u : 0) | (p == 17 ? 0xc00u : 0x700u) :
                p == 19 && x == 50 ? start + 0x100 : start + 8;
            CHECK(cpu->pc() == expected_pc && cpu->gpr(7) == input.gpr[7]);
            CHECK(cpu->cr().get() == input.cr && cpu->xer().get() == input.xer && cpu->ctr() == 0 && cpu->lr() == 0);
            CHECK(cpu->dar_ == (!exception && p == 31 && x == 467 && spr == 19 ? 0x55667787u : 0x12345678u));
            CHECK(cpu->dsisr_ == (!exception && p == 31 && x == 467 && spr == 18 ? 0x55667787u : 0x87654321u));
            if (exception) {
                CHECK(cpu->srr0_ == start + (p == 17 ? 8 : 4));
                CHECK(cpu->srr1_ == ((msr & ~0x783f0000u) | (privilege ? 0x40000u : trap ? 0x20000u : 0)));
                CHECK(mmu.msr() == (msr & ~ppc32_mmu::MSR_EXC_CLEAR));
                CHECK(cpu->gpr(3) == 0x55667787u);
            } else {
                CHECK(mmu.msr() == (p == 31 && x == 146 ? 0x55667787u : p == 19 && x == 50 ? cpu->srr1_ : msr));
                if (p == 31 && (x == 210 || x == 242)) CHECK(mmu.sr(x == 210 ? 4 : 0) == 0x55667787u);
                if (p == 31 && (x == 595 || x == 659)) CHECK(cpu->gpr(3) == 0x1000u + (x == 595 ? 4 : 0));
                if (p == 31 && x == 83) CHECK(cpu->gpr(3) == msr);
                if (p == 31 && x == 339 && spr == 22) CHECK(cpu->gpr(3) == 1000);
                if (p == 31 && x == 467 && spr == 22) CHECK(cpu->dec_ == 0x55667787u);
                if (p == 31 && x == 339 && spr == 777) CHECK(cpu->gpr(3) == 0x55667787u); // unavailable supervisor SPR is NOP
            }
            if (failed != failed_before) fprintf(stderr,"P4 case %u engine=%u user=%u ip=%u op=%08x\n",system_cases,engine,user,ip,op);
        }
        printf("P4 system observations: %u literal cases\n",system_cases);
        // A whole cache line must complete before SMC; fault/ROM/I/O paths
        // perform no partial zeroing and every replay is free of live effects.
        for (unsigned engine = 0; engine < 3; ++engine)
        for (unsigned translated = 0; translated < 2; ++translated)
        for (unsigned outcome = 0; outcome < 5; ++outcome) {
            const uint32 start = ram_base + 0x15000;
            nw_jit_invalidate_page(start); mmu.reset();
            mmu.set_msr(translated ? ppc32_mmu::MSR_DR : 0);
            if (translated) mmu.set_dbat(0,0x20000002u,ram_base | (outcome == 2 ? 1 : 2));
            const uint32 dest = ram_base + (outcome == 1 ? 0x15020 : 0x14020);
            const uint32 ea = outcome == 4 ? NW_IO_VIA_PMU_BASE : translated ? 0x20000000u + (dest - ram_base) : dest;
            if (translated && outcome == 4) mmu.set_dbat(1,(ea & ~0x1ffffu) | 2u,(ea & ~0x1ffffu) | 2u);
            nw_jit_cpu input = vector_start(start,ea + 31,false);
            memset(static_cast<uint8 *>(ram) + (dest - ram_base) - 1,0xa5,34);
            const uint32 op = (31u<<26)|(4u<<16)|(6u<<11)|(1014u<<1);
            const uint32 ops[] = {nw_ppc_addi(3,3,7),op,nw_ppc_addi(7,0,99)};
            for (unsigned i = 0; i < 3; ++i) vm_write_memory_4(start + i * 4,ops[i]);
            if (outcome == 3) { nw_banks_set(NW_PA_RAM,ram_base,0x14000); nw_banks_set(NW_PA_ROM,ram_base + 0x14000,0x1000); }
            const uint32 marker = nw_ppc_addi(10,0,1);
            nw_jit_set_mode(NW_JIT_ON);
            CHECK(nw_jit_compile(&marker,1,dest,dest & ~0xfffu,0,0) != NULL);
            nw68_bus bus = {};
            bus.code = [](void *, uint32_t ea, uint16_t *word, uint32_t *pa) { *word = 0x4e71; *pa = ea; return true; };
            nw68_instruction nop = {}; nop.operation = NW68_NOP; nop.pc = dest + 0x100;
            nop.opcode = nop.words[0] = 0x4e71; nop.length = 2; nop.word_count = 1;
            nw68_state state = {}; state.pc = nop.pc; nw68_frame frame;
            const uint32 page = dest & ~0xfffu;
            CHECK(nw68_run(nop,state,bus,0,&page,1,frame) == NW68_EXIT_NATIVE);
            nw68_instruction cached[NW68_BLOCK_MAX]; unsigned count = 0;
            CHECK(nw68_cached_block(nop.pc,0,page,nop.opcode,0x4e71,cached,&count));
            const uint64 misses = nw_jit_verify_misses();
            uint64 generation = nw68_code_generation();
            nw_jit_set_mode(engine == 2 ? NW_JIT_VERIFY : engine == 1 ? NW_JIT_ON : NW_JIT_OFF);
            if (!engine) { instruction(cpu,ops[0]); instruction(cpu,op); }
            else {
                nw_jit_fn fn = nw_jit_compile(ops,2,start,start & ~0xfffu,translated ? 2 : 0,0); CHECK(fn != NULL);
                generation = nw68_code_generation();
                if (fn) CHECK(engine == 2 ? cpu->nw_jit_verify_block(input,fn,ops,2) == 1 : cpu->nw_jit_try(ops[0]) == 1);
            }
            CHECK(nw_jit_verify_misses() == misses && cpu->gpr(7) == input.gpr[7]);
            const bool fault = translated && outcome == 2, ignored = outcome >= 3;
            CHECK((nw68_code_generation() != generation) == (!fault && !ignored));
            CHECK(nw68_cached_block(nop.pc,0,page,nop.opcode,0x4e71,cached,&count) == (fault || ignored));
            const nw_jit_fn survivor = nw_jit_cache_get(dest & ~0xfffu,dest,0,0,NULL);
            CHECK((survivor != NULL) == (fault || ignored));
            CHECK(cpu->pc() == (fault ? 0x300u : start + 8));
            for (unsigned i = 0; i < 32; ++i) CHECK(vm_read_memory_1(dest + i) == (fault || ignored ? 0xa5 : 0));
            CHECK(vm_read_memory_1(dest - 1) == 0xa5 && vm_read_memory_1(dest + 32) == 0xa5);
            if (fault) CHECK(cpu->srr0_ == start + 4 && cpu->dar_ == (ea & ~31u));
            nw_banks_set(NW_PA_ROM,0,0); nw_banks_set(NW_PA_RAM,ram_base,0x20000);
        }
        // P5: every BO/BI/LK, CR predicate and CTR boundary. KPX is separate
        // from a literal branch oracle; warm both potential successor blocks.
        unsigned branch_cases = 0;
        for (unsigned native_tail = 0; native_tail < 2; ++native_tail)
        for (unsigned family = 0; family < 3; ++family)
        for (unsigned bo = 0; bo < 32; ++bo)
        for (unsigned bi = 0; bi < 32; ++bi)
        for (unsigned lk = 0; lk < 2; ++lk)
        for (unsigned predicate = 0; predicate < 2; ++predicate)
        for (uint32 count : {0u,1u,2u,0xffffffffu}) {
            if (family == 2 && !(bo & 4)) continue; // bcctr CTR-decrement forms are architecturally invalid
            ++branch_cases;
            const uint32 start = ram_base + 0x15000, target = start + 0x80, fall = start + 8;
            nw_jit_invalidate_page(start); nw_jit_itlb_flush(); mmu.reset(); mmu.set_msr(ppc32_mmu::MSR_FP | NW_MSR_VEC);
            nw_jit_cpu input = vector_start(start,ram_base + 0x14000,false);
            cpu->cr().set(predicate ? (1u << (31 - bi)) : 0);
            cpu->ctr() = family == 2 ? target | 3 : count; cpu->lr() = target | 3;
            cpu->dec_ = 1000000; cpu->dec_tb_base_ = cpu->tb_host_ticks();
            const uint32 branch = family == 0 ? (16u<<26)|(bo<<21)|(bi<<16)|0x7cu|lk :
                (19u<<26)|(bo<<21)|(bi<<16)|((family == 1 ? 16u : 528u)<<1)|lk;
            const uint32 first[] = {nw_ppc_addi(3,3,7),branch};
            const uint32 second[] = {nw_ppc_addi(20,3,9),0x4c00012cu}; // explicit stop prevents recursive returns
            for (unsigned i = 0; i < 2; ++i) { vm_write_memory_4(start + i * 4,first[i]); vm_write_memory_4(target + i * 4,second[i]); vm_write_memory_4(fall + i * 4,second[i]); }
            const uint32 ctr = family == 2 ? target | 3 : (bo & 4) ? count : count - 1;
            const bool take = ((bo & 4) || ((ctr != 0) != bool(bo & 2))) && ((bo & 16) || predicate == unsigned(bool(bo & 8)));
            const uint32 chosen = take ? target : fall;
            nw_jit_set_mode(NW_JIT_ON);
            CHECK(nw_jit_compile(first,2,start,start & ~0xfffu,0,0) != NULL);
            CHECK(nw_jit_compile(second,2,target,start & ~0xfffu,0,0) != NULL);
            CHECK(nw_jit_compile(second,2,fall,start & ~0xfffu,0,0) != NULL);
            nw_jit_itlb_fill(start,start);
            nw_jit_set_host_chain(native_tail ? powerpc_cpu::jit_host_chain : NULL);
            const uint64 hops = nw_jit_chain_hops();
            CHECK(cpu->nw_jit_try(first[0]) == 1);
            CHECK(cpu->pc() == chosen + 8 && cpu->gpr(3) == input.gpr[3] + 7 && cpu->gpr(20) == input.gpr[3] + 16);
            CHECK(cpu->ctr() == ctr && cpu->lr() == (lk ? start + 8 : target | 3));
            CHECK(cpu->cr().get() == (predicate ? (1u << (31 - bi)) : 0) && cpu->xer().get() == input.xer);
            CHECK(nw_jit_chain_hops() == hops + 1);
        }
        CHECK(branch_cases == 81920);
        printf("P5 dynamic branches: %u literal successor cases\n",branch_cases);
        nw_jit_invalidate_all(); nw_jit_itlb_flush();
        nw_jit_set_host_chain(powerpc_cpu::jit_host_chain); nw_jit_set_mode(NW_JIT_VERIFY);

        // System replay rejects changed requests and malformed observations
        // before it can reach shared CPU/MMU callbacks or mutable generations.
        for (unsigned mutation = 0; mutation < 9; ++mutation) {
            const uint32 start = ram_base + 0x15000, op = (31u<<26)|(3u<<21)|(146u<<1);
            nw_jit_cpu shadow = {}; shadow.pc = start; shadow.msr = ppc32_mmu::MSR_FP; shadow.gpr[3] = 0x1230;
            shadow.host = cpu; // a non-null host must still be ignored in replay
            nw_jit_verify_trace trace;
            trace.record_system(start,op,0x1230,0,{0,0,NW_SYS_OK});
            if (mutation == 1) ++trace.accesses[0].pc;
            if (mutation == 2) trace.accesses[0].ea ^= 0x800;
            if (mutation == 3) ++trace.accesses[0].width;
            if (mutation == 4) ++trace.accesses[0].store;
            if (mutation == 5) trace.accesses[0].kind = nw_jit_verify_trace::memory;
            if (mutation == 6) trace.accesses[0].fault = NW_SYS_DSI;
            if (mutation == 7) trace.count = 0;
            shadow.verify_mem = nw_jit_verify_trace::replay; shadow.verify_context = &trace;
            shadow.verify_system = mutation == 8 ? NULL : nw_jit_verify_trace::replay_system;
            const uint32 msr = mmu.msr(), sr = mmu.sr(0), pc = cpu->pc();
            nw_jit_set_mode(NW_JIT_VERIFY);
            nw_jit_fn fn = nw_jit_compile(&op,1,start,start & ~0xfffu,0,0); CHECK(fn != NULL);
            if (fn) fn(&shadow);
            CHECK(shadow.fault == (mutation ? NW_JIT_FAULT_VERIFY : 0));
            CHECK(shadow.msr == (mutation ? ppc32_mmu::MSR_FP : 0x1230u));
            CHECK(mmu.msr() == msr && mmu.sr(0) == sr && cpu->pc() == pc);
            if (!mutation) CHECK(trace.complete() && shadow.pc == start + 4);
        }

        // Dirty vector/FP state must survive conditional/LR/CTR chains through
        // the opposite execution class. Every stop is tested on both paths.
        unsigned branch_state_cases = 0;
        for (unsigned native_tail = 0; native_tail < 2; ++native_tail)
        for (unsigned family = 0; family < 3; ++family)
        for (unsigned vector = 0; vector < 2; ++vector)
        for (unsigned stop = 0; stop < 6; ++stop) {
            ++branch_state_cases;
            const uint32 start = ram_base + 0x15000, second_pc = start + 0x40, third_pc = start + 0x80;
            nw_jit_invalidate_all(); nw_jit_itlb_flush(); mmu.reset();
            const uint32 msr = ppc32_mmu::MSR_EE | (stop == 1 ? vector ? NW_MSR_VEC : ppc32_mmu::MSR_FP : ppc32_mmu::MSR_FP | NW_MSR_VEC);
            mmu.set_msr(msr);
            nw_jit_cpu before = vector_start(start,ram_base + 0x14000,false);
            cpu->lr() = second_pc | 3; cpu->ctr() = second_pc | 3;
            cpu->fpr_dw(1) = bits(1.25); cpu->fpr_dw(2) = bits(2.5);
            cpu->fpscr() = 0; cpu->vscr().set(0x10000);
            for (unsigned w = 0; w < 4; ++w) { cpu->vr(1).w[w] = UINT32_MAX; cpu->vr(2).w[w] = 0x01010101; }
            cpu->dec_ = 1000000; cpu->dec_tb_base_ = cpu->tb_host_ticks(); cpu->dec_pending_ = stop == 2;
            if (stop == 3) cpu->spcflags().set(SPCFLAG_CPU_HANDLE_INTERRUPT);
            const uint32 branch = family == 0 ? (16u<<26)|(20u<<21)|0x3cu : (19u<<26)|(20u<<21)|((family == 1 ? 16u : 528u)<<1);
            const uint32 first[] = {vector ? (4u<<26)|(3u<<21)|(1u<<16)|(2u<<11)|512u : nw_ppc_fadds(3,1,2),branch};
            const uint32 second[] = {vector ? nw_ppc_fadds(9,1,2) : (4u<<26)|(9u<<21)|(1u<<16)|(2u<<11)|1220u,
                                    (16u<<26)|(20u<<21)|0x3cu};
            const uint32 third[] = {vector ? (4u<<26)|(5u<<21)|(3u<<16)|(2u<<11)|1220u : nw_ppc_fadds(5,3,2),0x4c00012cu};
            for (unsigned i = 0; i < 2; ++i) { vm_write_memory_4(start + 4*i,first[i]); vm_write_memory_4(second_pc + 4*i,second[i]); vm_write_memory_4(third_pc + 4*i,third[i]); }
            nw_jit_set_mode(NW_JIT_ON);
            CHECK(nw_jit_compile(first,2,start,start & ~0xfffu,0,0) != NULL);
            CHECK(nw_jit_compile(second,2,second_pc,start & ~0xfffu,0,0) != NULL);
            CHECK(nw_jit_compile(third,2,third_pc,start & ~0xfffu,0,0) != NULL);
            if (stop == 4) nw_jit_cache_put(start & ~0xfffu,second_pc,0,0,NW_JIT_INTERPRET,1);
            if (stop != 5) nw_jit_itlb_fill(start,start);
            nw_jit_set_host_chain(native_tail ? powerpc_cpu::jit_host_chain : NULL);
            const uint64 hops = nw_jit_chain_hops();
            CHECK(cpu->nw_jit_try(first[0]) == 1);
            CHECK(cpu->pc() == (stop ? second_pc : third_pc + 8));
            CHECK(nw_jit_chain_hops() == hops + (stop ? 0 : 2));
            CHECK(cpu->lr() == (second_pc | 3) && cpu->ctr() == (second_pc | 3));
            CHECK(cpu->fpscr() == (!vector || !stop ? 0x4000u : 0) && cpu->vscr().get() == (vector ? 0x10001u : 0x10000u));
            for (unsigned r = 0; r < 32; ++r) {
                CHECK(cpu->gpr(r) == before.gpr[r]);
                const uint64 fp = r == 1 ? bits(1.25) : r == 2 ? bits(2.5) :
                    !vector && r == 3 ? bits(3.75) : !vector && r == 5 && !stop ? bits(6.25) : vector && r == 9 && !stop ? bits(3.75) : 0;
                CHECK(cpu->fpr_dw(r) == fp);
                for (unsigned w = 0; w < 4; ++w) {
                    const uint32 vr = r == 1 ? UINT32_MAX : r == 2 ? 0x01010101 :
                        vector && r == 3 ? UINT32_MAX : vector && r == 5 && !stop ? 0xfefefefe : !vector && r == 9 && !stop ? 0xfefefefe : before.vr[r][w];
                    CHECK(cpu->vr(r).w[w] == vr);
                }
            }
            CHECK(cpu->cr().get() == before.cr && cpu->xer().get() == before.xer && mmu.msr() == msr);
        }
        CHECK(branch_state_cases == 72);
        // Repeated indirect self tails are bounded and reuse one native frame. With the indirect branch target cache off, every
        // hop goes through the C helper and the bound is exact; with it on, each helper hop buys up to a link budget of
        // cached hops, so the bound is that many times larger.
        for (unsigned native_tail = 0; native_tail < 2; ++native_tail) for (unsigned cached = 0; cached < (native_tail ? 2u : 1u); ++cached) {
            const unsigned saved_legacy = nw_jit_legacy;
            if (cached) nw_jit_legacy &= ~NW_JIT_LEGACY_IBTC; else nw_jit_legacy |= NW_JIT_LEGACY_IBTC;
            const uint32 start = ram_base + 0x15000;
            nw_jit_invalidate_all(); nw_jit_itlb_flush(); mmu.reset();
            vector_start(start,ram_base + 0x14000,false); cpu->lr() = start;
            cpu->dec_ = 1000000; cpu->dec_tb_base_ = cpu->tb_host_ticks();
            const uint32 ops[] = {nw_ppc_addi(3,3,1),nw_ppc_blr()};
            vm_write_memory_4(start,ops[0]); vm_write_memory_4(start + 4,ops[1]);
            nw_jit_set_mode(NW_JIT_ON); CHECK(nw_jit_compile(ops,2,start,start & ~0xfffu,0,0) != NULL);
            nw_jit_itlb_fill(start,start); nw_jit_set_host_chain(native_tail ? powerpc_cpu::jit_host_chain : NULL);
            const unsigned steps = native_tail ? unsigned(nw_jit_tail_max()) + 1 : 9;
            for (unsigned repeat = 0; repeat < 1000; ++repeat) {
                const uint32 before = cpu->gpr(3); const uint64 hops = nw_jit_chain_hops();
                CHECK(cpu->nw_jit_try(ops[0]) == 1 && cpu->pc() == start);
                if (!cached) CHECK(cpu->gpr(3) == before + steps && nw_jit_chain_hops() == hops + steps - 1);
                else CHECK(cpu->gpr(3) - before >= steps && cpu->gpr(3) - before <= steps * 32u && nw_jit_chain_hops() == hops + steps - 1);
            }
            nw_jit_legacy = saved_legacy;
        }
        printf("P5 mixed state: %u cases; 2000 bounded indirect loops\n",branch_state_cases);
        nw_jit_invalidate_all(); nw_jit_itlb_flush();

        // Independent address/LK expectations include absolute, negative and
        // wrapping branch displacements; replay does not fetch the target.
        unsigned branch_address_cases = 0;
        for (unsigned family = 0; family < 4; ++family)
        for (unsigned aa = 0; aa < (family < 2 ? 2u : 1u); ++aa)
        for (unsigned lk = 0; lk < 2; ++lk)
        for (unsigned predicate = 0; predicate < 2; ++predicate)
        for (int32 disp : {0x40,-0x40,0x7ffc,-0x8000}) {
            ++branch_address_cases;
            const uint32 start = ram_base + 0x15000;
            nw_jit_invalidate_all(); mmu.reset();
            nw_jit_cpu input = vector_start(start,ram_base + 0x14000,false);
            input.cr = predicate ? 0x80000000u : 0; cpu->cr().set(input.cr);
            input.lr = cpu->lr() = uint32(disp) | 3; input.ctr = cpu->ctr() = uint32(disp) | 3;
            const uint32 op = family == 0 ? (16u<<26)|(12u<<21)|(uint32(disp)&0xfffcu)|(aa<<1)|lk :
                family == 1 ? (18u<<26)|(uint32(disp)&0x3fffffcu)|(aa<<1)|lk :
                (19u<<26)|(12u<<21)|((family == 2 ? 16u : 528u)<<1)|lk;
            vm_write_memory_4(start,op);
            const nw_jit_cpu result = vector_run(input,&op,1,true);
            const bool taken = family == 1 || predicate;
            const uint32 target = family < 2 ? (aa ? uint32(disp) : start + uint32(disp)) : uint32(disp) & ~3u;
            CHECK(cpu->pc() == (taken ? target : start + 4) && result.pc == cpu->pc());
            CHECK(cpu->lr() == (lk ? start + 4 : input.lr) && cpu->ctr() == input.ctr);
        }
        CHECK(branch_address_cases == 96);
        nw_jit_invalidate_all(); nw_jit_itlb_flush();

        // Negative control reintroduces the stale VR copy that source history
        // identifies as the glyph corruption mechanism. The same branch and
        // generated pixel store must write new pixels with the repaired hook.
        for (unsigned family = 0; family < 3; ++family)
        for (unsigned stale = 0; stale < 2; ++stale) {
            const uint32 start = ram_base + 0x15000, target = start + 0x40, pixels = ram_base + 0x14000;
            nw_jit_invalidate_all(); nw_jit_itlb_flush(); mmu.reset(); mmu.set_msr(NW_MSR_VEC);
            nw_jit_cpu before = vector_start(start,pixels,false);
            for (unsigned w = 0; w < 4; ++w) { cpu->vr(1).w[w] = UINT32_MAX; cpu->vr(2).w[w] = 0x01010101; }
            cpu->lr() = cpu->ctr() = target;
            cpu->dec_ = 1000000; cpu->dec_tb_base_ = cpu->tb_host_ticks();
            const uint32 branch = family == 0 ? (16u<<26)|(20u<<21)|0x3cu : (19u<<26)|(20u<<21)|((family == 1 ? 16u : 528u)<<1);
            const uint32 first[] = {(4u<<26)|(3u<<21)|(1u<<16)|(2u<<11)|1220u,branch};
            const uint32 second[] = {nw_ppc_stvx(3,4,6),0x4c00012cu};
            for (unsigned i = 0; i < 2; ++i) { vm_write_memory_4(start + 4*i,first[i]); vm_write_memory_4(target + 4*i,second[i]); }
            nw_jit_set_mode(NW_JIT_ON);
            CHECK(nw_jit_compile(first,2,start,start & ~0xfffu,0,0) != NULL);
            CHECK(nw_jit_compile(second,2,target,start & ~0xfffu,0,0) != NULL);
            nw_jit_itlb_fill(start,start);
            nw_jit_set_host_chain(stale ? stale_vector_tail : powerpc_cpu::jit_host_chain);
            CHECK(cpu->nw_jit_try(first[0]) == 1 && cpu->pc() == target + 8);
            for (unsigned w = 0; w < 4; ++w) {
                CHECK(vm_read_memory_4(pixels + 4*w) == (stale ? before.vr[3][w] : 0xfefefefe));
                if (stale) CHECK(vm_read_memory_4(pixels + 4*w) != 0xfefefefe);
            }
        }
        printf("P5 stale-vector negative control: three corrupted pixel transfers reproduced and three repaired controls passed\n");
        nw_jit_set_host_chain(powerpc_cpu::jit_host_chain);
        nw_jit_invalidate_all(); nw_jit_itlb_flush();

        // Octet shifts must work inside dependent blocks, not only as the
        // final operation. Use logical byte arrays as an independent oracle;
        // controls outside bits 121:124 must not affect the shift amount.
        const unsigned octet_aliases[][3] = {{1,2,3},{1,1,3},{1,2,1},{1,2,2},{1,1,1}};
        unsigned octet_cases = 0;
        for (unsigned verify = 0; verify < 2; ++verify)
        for (unsigned translated = 0; translated < 2; ++translated)
        for (unsigned native_tail = 0; native_tail < (verify ? 1u : 2u); ++native_tail)
        for (unsigned length : {3u,15u,31u})
        for (const auto &alias : octet_aliases)
        for (unsigned control = 0; control < 256; ++control) {
            const unsigned sh = (control >> 3) & 15;
            ++octet_cases;
            mmu.reset(); mmu.set_msr(NW_MSR_VEC | (translated ? ppc32_mmu::MSR_DR : 0));
            if (translated) mmu.set_dbat(0,0x20000002u,ram_base | 2u);
            const uint32 start = ram_base + 0x16000;
            nw_jit_invalidate_page(start);
            nw_jit_cpu before = vector_start(start,ram_base + 0x4000,false);
            before.lr = cpu->lr() = start + 0x100;
            before.vr[alias[2]][3] = (before.vr[alias[2]][3] & ~255u) | control;
            cpu->vr(alias[2]).w[3] = before.vr[alias[2]][3];
            uint8 expected[32][16];
            for (unsigned r = 0; r < 32; ++r) for (unsigned b = 0; b < 16; ++b)
                expected[r][b] = uint8(before.vr[r][b / 4] >> (24 - 8 * (b % 4)));
            uint32 ops[32];
            for (unsigned i = 0; i < length; ++i) {
                const unsigned d = i % 4 == 1 ? 7 : alias[0];
                const unsigned a = i % 4 == 2 ? 7 : alias[1];
                const unsigned b = i % 4 == 1 ? alias[0] : alias[2];
                const unsigned xo = i % 4 == 0 ? ((i / 4 + sh) % 2 ? 1100 : 1036) :
                    i % 4 == 1 ? 1220 : i % 4 == 2 ? ((sh << 6) | 44) : 1156;
                ops[i] = (4u<<26)|(d<<21)|(a<<16)|(b<<11)|xo;
                CHECK(!nw_jit_op_ends_block(ops[i]));
                uint8 av[16], bv[16]; memcpy(av,expected[a],16); memcpy(bv,expected[b],16);
                const unsigned count = xo == 1036 || xo == 1100 ? (bv[15] >> 3) & 15 : sh;
                for (unsigned j = 0; j < 16; ++j) {
                    if (xo == 1036) expected[d][j] = j + count < 16 ? av[j + count] : 0;
                    else if (xo == 1100) expected[d][j] = j >= count ? av[j - count] : 0;
                    else if (xo == 1220) expected[d][j] = av[j] ^ bv[j];
                    else if (xo == 1156) expected[d][j] = av[j] | bv[j];
                    else expected[d][j] = j + count < 16 ? av[j + count] : bv[j + count - 16];
                }
                vm_write_memory_4(start + 4 * i,ops[i]);
            }
            ops[length] = nw_ppc_blr(); vm_write_memory_4(start + 4 * length,ops[length]);
            nw_jit_set_host_chain(native_tail ? powerpc_cpu::jit_host_chain : NULL);
            const nw_jit_cpu result = vector_run(before,ops,length + 1,verify != 0);
            CHECK(result.pc == before.lr && !result.fault);
            CHECK(cpu->pc() == before.lr && cpu->cr().get() == before.cr && cpu->xer().get() == before.xer);
            CHECK(cpu->vscr().get() == before.vscr && !cpu->fpscr());
            for (unsigned r = 0; r < 32; ++r) {
                CHECK(cpu->gpr(r) == before.gpr[r] && !cpu->fpr_dw(r));
                for (unsigned b = 0; b < 16; ++b)
                    CHECK(uint8(cpu->vr(r).w[b / 4] >> (24 - 8 * (b % 4))) == expected[r][b]);
            }
        }
        CHECK(octet_cases == 23040);
        nw_jit_invalidate_page(ram_base + 0x16000);
        nw_jit_set_host_chain(powerpc_cpu::jit_host_chain);
        // Let the actual cold-cache builder discover the block, rather than
        // supplying an explicitly compiled instruction list. Both its class
        // boundary and 32-op limit must include the shift's dependent suffix.
        for (unsigned verify = 0; verify < 2; ++verify)
        for (unsigned right = 0; right < 2; ++right)
        for (unsigned length : {31u,32u})
        for (unsigned sh = 0; sh < 16; ++sh) {
            mmu.reset(); mmu.set_msr(NW_MSR_VEC);
            const uint32 start = ram_base + 0x16000;
            nw_jit_invalidate_page(start);
            nw_jit_cpu before = vector_start(start,ram_base + 0x4000,false);
            before.vr[2][3] = cpu->vr(2).w[3] = sh << 3;
            const uint32 first = right ? nw_ppc_vsro(1,1,2) : nw_ppc_vslo(1,1,2);
            vm_write_memory_4(start,first);
            for (unsigned i = 1; i < length; ++i)
                vm_write_memory_4(start + 4 * i,(4u<<26)|(3u<<21)|(1u<<16)|(1u<<11)|1156u);
            vm_write_memory_4(start + 4 * length,nw_ppc_addi(3,3,1));
            const uint64 misses = nw_jit_verify_misses();
            nw_jit_set_mode(verify ? NW_JIT_VERIFY : NW_JIT_ON);
            CHECK(cpu->nw_jit_try(first) == 1);
            int n = 0;
            CHECK(nw_jit_cache_get(start & ~0xfffu,start,0,0,&n) != NULL && n == int(length));
            CHECK(cpu->pc() == start + 4 * length && cpu->gpr(3) == before.gpr[3]);
            CHECK(nw_jit_verify_misses() == misses && cpu->vscr().get() == before.vscr);
            for (unsigned r = 0; r < 32; ++r) for (unsigned b = 0; b < 16; ++b) {
                const int index = right ? int(b) - int(sh) : int(b) + int(sh);
                const uint8 expected = r != 1 && r != 3 ? uint8(before.vr[r][b / 4] >> (24 - 8 * (b % 4))) :
                    index < 0 || index >= 16 ? 0 : uint8(before.vr[1][index / 4] >> (24 - 8 * (index % 4)));
                CHECK(uint8(cpu->vr(r).w[b / 4] >> (24 - 8 * (b % 4))) == expected);
            }
            nw_jit_set_mode(NW_JIT_VERIFY);
        }
        nw_jit_invalidate_page(ram_base + 0x16000);
        // A completed shift is retained on a following DSI; its dependent
        // suffix is not executed. Exercise real KPX exception entry and replay.
        for (unsigned verify = 0; verify < 2; ++verify)
        for (unsigned store = 0; store < 2; ++store)
        for (unsigned right = 0; right < 2; ++right)
        for (unsigned sh = 0; sh < 16; ++sh) {
            mmu.reset(); mmu.set_msr(NW_MSR_VEC | ppc32_mmu::MSR_DR);
            const uint32 start = ram_base + 0x16000;
            nw_jit_invalidate_page(start);
            nw_jit_cpu before = vector_start(start,0x60000000u,false);
            before.vr[2][3] = cpu->vr(2).w[3] = sh << 3;
            const uint32 ops[] = {right ? nw_ppc_vsro(1,1,2) : nw_ppc_vslo(1,1,2),
                (31u<<26)|(3u<<21)|(4u<<16)|(6u<<11)|((store ? 231u : 103u)<<1),
                nw_ppc_vsro(2,1,2)};
            const uint64 serial = cpu->exception_serial_;
            vector_run(before,ops,3,verify != 0);
            CHECK(cpu->exception_serial_ == serial + 1 && cpu->pc() == 0x300);
            CHECK(cpu->srr0_ == start + 4 && cpu->dar_ == 0x60000000u);
            CHECK(cpu->cr().get() == before.cr && cpu->xer().get() == before.xer && cpu->vscr().get() == before.vscr);
            for (unsigned r = 0; r < 32; ++r) for (unsigned b = 0; b < 16; ++b) {
                const int index = right ? int(b) - int(sh) : int(b) + int(sh);
                const uint8 expected = r != 1 ? uint8(before.vr[r][b / 4] >> (24 - 8 * (b % 4))) :
                    index < 0 || index >= 16 ? 0 : uint8(before.vr[1][index / 4] >> (24 - 8 * (index % 4)));
                CHECK(uint8(cpu->vr(r).w[b / 4] >> (24 - 8 * (b % 4))) == expected);
            }
        }
        nw_jit_invalidate_page(ram_base + 0x16000);
        // A DSI vector can equal the next sequential PC. Reference replay
        // must observe exception delivery, not execute a cached suffix there.
        for (bool store : {false,true}) {
            mmu.reset(); mmu.set_msr(NW_MSR_VEC | ppc32_mmu::MSR_DR);
            nw_jit_cpu before = vector_start(0x2fc,0x60000000,false);
            nw_jit_invalidate_page(0); // The two fixtures replace the same instruction.
            const uint32 ops[] = {(31u<<26)|(3u<<21)|(4u<<16)|(6u<<11)|((store ? 231u : 103u)<<1),nw_ppc_addi(7,0,99)};
            nw_jit_fn fn = nw_jit_compile(ops,2,before.pc,0,2,0);
            CHECK(fn != NULL);
            const uint64 serial = cpu->exception_serial_, misses = nw_jit_verify_misses();
            if (fn) CHECK(cpu->nw_jit_verify_block(before,fn,ops,2) == 1);
            CHECK(cpu->exception_serial_ == serial + 1 && cpu->pc() == 0x300);
            CHECK(cpu->gpr(7) == 0x12340007u && cpu->srr0_ == 0x2fc && cpu->dar_ == 0x60000000u);
            CHECK(nw_jit_verify_misses() == misses);
        }
        struct vector_memory_form { unsigned xo, width; bool store; };
        const vector_memory_form vector_memory[] = {{7,1,false},{39,2,false},{71,4,false},{103,16,false},{359,16,false},
            {135,1,true},{167,2,true},{199,4,true},{231,16,true},{487,16,true}};
        unsigned vector_memory_case = 0;
        for (unsigned verify = 0; verify < 2; ++verify)
        for (unsigned translated = 0; translated < 2; ++translated)
        for (unsigned zero_ra = 0; zero_ra < 2; ++zero_ra)
        for (const auto &form : vector_memory)
        for (unsigned offset = 0; offset < 16; ++offset) {
            mmu.reset(); mmu.set_msr(NW_MSR_VEC | (translated ? ppc32_mmu::MSR_DR : 0));
            if (translated) mmu.set_dbat(0, 0x20000002u, ram_base | 2u);
            const uint32 ea = (translated ? 0x20000000u : ram_base) + 0x4000 + offset;
            const uint32 start = 0x200000 + 64 * vector_memory_case++;
            const nw_jit_cpu before = vector_start(start, ea, zero_ra);
            for (unsigned i = 0; i < 16; ++i) vm_write_memory_1(ram_base + 0x4000 + i, 0xa0u + i);
            const uint32 op = (31u<<26)|(3u<<21)|((zero_ra ? 0u : 4u)<<16)|(6u<<11)|(form.xo<<1);
            CHECK(nw_jit_op_verify_safe(op));
            const nw_jit_cpu result = vector_run(before, &op, 1, verify != 0);
            const unsigned aligned = offset & ~(form.width - 1u);
            for (unsigned i = 0; i < 16; ++i) {
                const unsigned word = i / 4, shift = 24 - 8 * (i & 3);
                const uint8 original = uint8(before.vr[3][word] >> shift);
                const bool selected = i >= aligned && i < aligned + form.width;
                const uint8 expected_v = !form.store && selected ? uint8(0xa0 + i) : original;
                CHECK(uint8(cpu->vr(3).w[word] >> shift) == expected_v);
                CHECK(vm_read_memory_1(ram_base + 0x4000 + i) == (form.store && selected ? original : uint8(0xa0 + i)));
            }
            for (unsigned r = 0; r < 32; ++r) {
                CHECK(cpu->gpr(r) == before.gpr[r]);
                if (r != 3) for (unsigned w = 0; w < 4; ++w) CHECK(cpu->vr(r).w[w] == before.vr[r][w]);
            }
            CHECK(result.pc == start + 4 && !result.fault && cpu->cr().get() == before.cr && cpu->vscr().get() == before.vscr);
        }
        // Protected reads/stores retain vector outputs and suppress a same-class
        // suffix; full vectors record their 16-byte probe, elements their width.
        for (unsigned verify = 0; verify < 2; ++verify)
        for (const auto &form : vector_memory) {
            mmu.reset(); mmu.set_msr(NW_MSR_VEC | ppc32_mmu::MSR_DR);
            mmu.set_dbat(0, 0x20000002u, ram_base | (form.store ? 1u : 0u));
            const uint32 start = 0x220000 + 64 * vector_memory_case++;
            const nw_jit_cpu before = vector_start(start, 0x2000000f, false);
            const uint32 ops[] = {(31u<<26)|(3u<<21)|(4u<<16)|(6u<<11)|(form.xo<<1), ((4u<<26)|(7u<<21)|(8u<<16)|(9u<<11)|1156u)};
            const nw_jit_cpu result = vector_run(before, ops, 2, verify != 0);
            CHECK(result.fault == NW_JIT_FAULT_DSI && result.pc == start);
            CHECK(result.fault_ea == (0x2000000fu & ~(form.width - 1u)) && result.fault_width == form.width && result.fault_st == form.store);
            CHECK(cpu->pc() == 0x300 && cpu->srr0_ == start && cpu->dar_ == result.fault_ea);
            for (unsigned r = 0; r < 32; ++r) for (unsigned w = 0; w < 4; ++w) CHECK(cpu->vr(r).w[w] == before.vr[r][w]);
        }
        for (unsigned verify = 0; verify < 2; ++verify)
        for (const auto &form : vector_memory) {
            if (!form.store) continue;
            mmu.reset(); mmu.set_msr(NW_MSR_VEC);
            const uint32 start = ram_base + 0x17000 + 64 * (vector_memory_case++ % 32);
            const nw_jit_cpu before = vector_start(start, start + 0x2f, false);
            const uint32 ops[] = {(31u<<26)|(3u<<21)|(4u<<16)|(6u<<11)|(form.xo<<1), ((4u<<26)|(7u<<21)|(8u<<16)|(9u<<11)|1156u)};
            const nw_jit_cpu result = vector_run(before, ops, 2, verify != 0);
            const uint32 ea = (start + 0x2f) & ~(form.width - 1u);
            CHECK(result.fault == NW_JIT_FAULT_SMC && result.pc == start);
            CHECK(result.fault_ea == ea + (verify && form.width == 16 ? 12 : 0));
            CHECK(result.fault_width == (verify && form.width == 16 ? 4u : form.width));
            for (unsigned i = 0; i < form.width; ++i) {
                const unsigned lane = (ea + i) & 15;
                CHECK(vm_read_memory_1(ea + i) == uint8(before.vr[3][lane / 4] >> (24 - 8 * (lane & 3))));
            }
            for (unsigned w = 0; w < 4; ++w) CHECK(cpu->vr(7).w[w] == before.vr[7][w]);
        }
        // Destructive devices run only in the reference. Aligned vectors issue
        // four ordered word transactions; element forms issue one transaction.
        for (unsigned translated = 0; translated < 2; ++translated)
        for (const auto &load : vector_memory) {
            if (load.store) continue;
            mmu.reset(); mmu.set_msr(NW_MSR_VEC | (translated ? ppc32_mmu::MSR_DR : 0));
            if (translated) mmu.set_dbat(0, (NW_IO_VIA_PMU_BASE & 0xfffe0000u) | 2u, (NW_IO_VIA_PMU_BASE & 0xfffe0000u) | 2u);
            device = {}; device.value = 0x01020304;
            const uint32 start = 0x230000 + 64 * vector_memory_case++;
            const nw_jit_cpu before = vector_start(start, NW_IO_VIA_PMU_BASE + 15, false);
            const unsigned store_xo = load.xo == 359 ? 487 : load.xo + 128;
            const uint32 ops[] = {(31u<<26)|(3u<<21)|(4u<<16)|(6u<<11)|(load.xo<<1),
                                  (31u<<26)|(3u<<21)|(4u<<16)|(6u<<11)|(store_xo<<1)};
            uint8 poison[4096] = {}; poison[0] = 0x7f;
            nw_jit_dtlb_fill(NW_IO_VIA_PMU_BASE, NW_IO_VIA_PMU_BASE, 1, (uint64)(uintptr)poison, 0, 1);
            const nw_jit_cpu result = vector_run(before, ops, 2, true);
            const unsigned count = load.width == 16 ? 4 : 1;
            CHECK(device.reads == count && device.writes == count && !result.fault);
            const uint32 last = 0x01020304u + count - 1;
            CHECK(device.value == (load.width == 1 ? last & 0xffu : load.width == 2 ? last & 0xffffu : last));
            CHECK(poison[0] == 0x7f && poison[1] == 0);
        }
        // Shift-mask formation uses only the logical EA, including r0-as-zero.
        for (unsigned xo : {6u, 38u})
        for (unsigned zero_ra = 0; zero_ra < 2; ++zero_ra)
        for (unsigned offset = 0; offset < 16; ++offset) {
            mmu.reset(); mmu.set_msr(NW_MSR_VEC);
            const uint32 start = 0x240000 + 64 * vector_memory_case++;
            const nw_jit_cpu before = vector_start(start, ram_base + offset, zero_ra != 0);
            const uint32 op = (31u<<26)|(3u<<21)|((zero_ra ? 0u : 4u)<<16)|(6u<<11)|(xo<<1);
            CHECK(nw_jit_op_verify_safe(op));
            const nw_jit_cpu result = vector_run(before, &op, 1, true);
            for (unsigned i = 0; i < 16; ++i) {
                const unsigned shift = xo == 6 ? offset : 16 - offset;
                CHECK(uint8(result.vr[3][i / 4] >> (24 - 8 * (i & 3))) == shift + i);
            }
            CHECK(result.vscr == before.vscr && result.pc == start + 4 && !result.fault);
        }
        // The older scalar fixtures initialize an all-zero vector status.
        cpu->vscr().set(0);
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
            nw_jit_dtlb_flush_src(NW_JIT_DTLB_FL_BAT);	// a guest mtdbat would announce this; byte/halfword loads now fill the table too
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

        // String transfers use independently expected byte packing and a
        // poisoned suffix. Indexed loads keep RB outside the destination set;
        // stores cover all seven-bit XER counts, immediate NB=0 means 32.
        unsigned string_case = 0;
        auto string_start = [&](uint32 address, uint32 start, uint32 xer) {
            for (unsigned r = 0; r < 32; ++r) {
                cpu->gpr(r) = 0xa0b00000u + r * 0x10203u;
                cpu->fpr_dw(r) = 0; memset(cpu->vr(r).w, 0, 16);
            }
            cpu->gpr(31) = address; cpu->gpr(5) = address;
            cpu->pc() = start; cpu->last_fetch_pa_ = start;
            cpu->cr().set(0x12345678); cpu->xer().set(xer); cpu->fpscr() = 0;
            cpu->lr() = 0; cpu->ctr() = 0; cpu->vscr().set(0);
            nw_jit_cpu shadow = {}; shadow.pc = start; shadow.dec = cpu->dec_;
            shadow.msr = mmu.msr(); shadow.cr = cpu->cr().get(); shadow.xer = xer;
            for (unsigned r = 0; r < 32; ++r) shadow.gpr[r] = cpu->gpr(r);
            return shadow;
        };
        for (unsigned translated = 0; translated < 2; ++translated)
        for (unsigned store = 0; store < 2; ++store)
        for (unsigned immediate = 0; immediate < 2; ++immediate)
        for (unsigned encoded = 0; encoded < (immediate ? 32u : store ? 128u : 121u); ++encoded) {
            mmu.reset(); mmu.set_msr(translated ? ppc32_mmu::MSR_DR : 0);
            const uint32 logical = translated ? 0x20000000u : ram_base;
            if (translated) mmu.set_dbat(0, logical | 2u, ram_base | 2u);
            const unsigned count = immediate && !encoded ? 32 : encoded;
            const uint32 start = 0x50000 + 64 * string_case++;
            nw_jit_cpu shadow = string_start(logical + 0x1003, start, 0xa0000000u | encoded);
            uint32 expected[32]; memcpy(expected, shadow.gpr, sizeof expected);
            for (unsigned i = 0; i < 132; ++i) vm_write_memory_1(ram_base + 0x1003 + i, 0x30u + i);
            for (unsigned i = 0; !store && i < count; ++i) {
                const unsigned r = (1 + i / 4) & 31;
                if (!(i & 3)) expected[r] = 0;
                expected[r] |= uint32(uint8(0x30u + i)) << (24 - 8 * (i & 3));
            }
            const uint32 op = (31u<<26)|(1u<<21)|
                (immediate ? (31u<<16)|(encoded<<11)|((store ? 725u : 597u)<<1)
                           : (31u<<11)|((store ? 661u : 533u)<<1));
            const uint32 ops[] = {op, nw_ppc_addi(5,0,99)};
            nw_jit_fn fn = nw_jit_compile(ops, 2, start, start & ~0xfffu, translated ? 2 : 0, 0);
            CHECK(fn != NULL);
            const uint64 misses = nw_jit_verify_misses();
            if (fn) CHECK(cpu->nw_jit_verify_block(shadow, fn, ops, 2) == 1);
            CHECK(nw_jit_verify_misses() == misses);
            for (unsigned r = 0; r < 32; ++r) {
                const uint32 wanted_value = r == 5 ? 99 : expected[r];
                CHECK(shadow.gpr[r] == wanted_value && cpu->gpr(r) == wanted_value);
            }
            CHECK(shadow.cr == 0x12345678 && shadow.xer == (0xa0000000u | encoded));
            CHECK(shadow.pc == start + 8 && !shadow.fault);
            for (unsigned i = 0; store && i < count; ++i)
                CHECK(vm_read_memory_1(ram_base + 0x1003 + i) ==
                    uint8(expected[(1 + i / 4) & 31] >> (24 - 8 * (i & 3))));
            CHECK(vm_read_memory_1(ram_base + 0x1003 + count) == uint8(0x30u + count));
        }
        // Register wrapping and the immediate partial register are independent
        // of EA register values. The source/destination runs through r31/r0.
        for (unsigned store = 0; store < 2; ++store) {
            mmu.reset(); mmu.set_msr(0);
            nw_jit_cpu shadow = string_start(ram_base + 0x1200, 0x68000 + store * 64, 0);
            const uint32 ops[] = {(31u<<26)|(30u<<21)|(5u<<16)|(11u<<11)|((store ? 725u : 597u)<<1)};
            for (unsigned i = 0; i < 12; ++i) vm_write_memory_1(ram_base + 0x1200 + i, 0x10u + i);
            const uint32 source[] = {shadow.gpr[30], shadow.gpr[31], shadow.gpr[0]};
            nw_jit_fn fn = nw_jit_compile(ops, 1, shadow.pc, shadow.pc & ~0xfffu, 0, 0); CHECK(fn != NULL);
            const uint64 misses = nw_jit_verify_misses();
            if (fn) CHECK(cpu->nw_jit_verify_block(shadow, fn, ops, 1) == 1);
            CHECK(nw_jit_verify_misses() == misses);
            if (!store) {
                CHECK(shadow.gpr[30] == 0x10111213 && shadow.gpr[31] == 0x14151617 && shadow.gpr[0] == 0x18191a00);
            } else for (unsigned i = 0; i < 11; ++i)
                CHECK(vm_read_memory_1(ram_base + 0x1200 + i) == uint8(source[i / 4] >> (24 - 8 * (i & 3))));
        }
        // A successful word/halfword followed by a protected subaccess keeps
        // completed registers/stores, precise EA/width and suppresses suffix.
        // Includes a three-byte load whose final byte faults: RD stays intact.
        for (unsigned store = 0; store < 2; ++store)
        for (unsigned prefix : {1u, 2u, 3u, 4u, 5u, 6u, 7u}) {
            mmu.reset(); mmu.set_msr(ppc32_mmu::MSR_DR);
            mmu.set_dbat(0, 0x20000002u, ram_base | 2u);
            mmu.set_dbat(1, 0x20020002u, ram_base + 0x20000u);
            nw_jit_dtlb_flush_src(NW_JIT_DTLB_FL_BAT);	// a guest mtdbat would announce this; byte/halfword loads now fill the table too
            const uint32 start = 0x69000 + string_case++ * 64;
            nw_jit_cpu shadow = string_start(0x20020000u - prefix, start, 0);
            for (unsigned i = 0; i < prefix; ++i) vm_write_memory_1(ram_base + 0x20000 - prefix + i, 0x10u + i);
            const uint32 before1 = shadow.gpr[1], before2 = shadow.gpr[2], before5 = shadow.gpr[5];
            const uint32 ops[] = {(31u<<26)|(1u<<21)|(31u<<16)|((prefix + 1)<<11)|((store ? 725u : 597u)<<1), nw_ppc_addi(5,0,99)};
            nw_jit_fn fn = nw_jit_compile(ops, 2, start, start & ~0xfffu, 2, 0); CHECK(fn != NULL);
            const uint64 misses = nw_jit_verify_misses();
            if (fn) CHECK(cpu->nw_jit_verify_block(shadow, fn, ops, 2) == 1);
            CHECK(nw_jit_verify_misses() == misses);
            const uint32 fault_ea = 0x20020000u;
            const uint32 fault_width = 1;
            CHECK(shadow.fault == NW_JIT_FAULT_DSI && shadow.pc == start);
            CHECK(shadow.fault_ea == fault_ea && shadow.fault_width == fault_width && shadow.fault_st == store);
            CHECK(cpu->dar_ == fault_ea && cpu->srr0_ == start && shadow.gpr[5] == before5);
            CHECK(shadow.gpr[1] == (store || prefix < 4 ? before1 : 0x10111213u));
            CHECK(shadow.gpr[2] == before2);
            for (unsigned i = 0; store && i < prefix; ++i)
                CHECK(vm_read_memory_1(ram_base + 0x20000 - prefix + i) ==
                    uint8((i < 4 ? before1 : before2) >> (24 - 8 * (i & 3))));
        }
        // The next logical BAT maps back to the first physical page, not
        // to adjacent host memory. Read/write every crossed subaccess afresh.
        for (unsigned store = 0; store < 2; ++store) {
            mmu.reset(); mmu.set_msr(ppc32_mmu::MSR_DR);
            mmu.set_dbat(0, 0x20000002u, ram_base | 2u);
            mmu.set_dbat(1, 0x20020002u, ram_base | 2u);
            nw_jit_dtlb_flush_src(NW_JIT_DTLB_FL_BAT);	// a guest mtdbat would announce this; byte/halfword loads now fill the table too
            nw_jit_cpu shadow = string_start(0x2001fffdu, 0x7a000 + store * 64, 0);
            for (unsigned i = 0; i < 7; ++i) vm_write_memory_1(i < 3 ? ram_base + 0x1fffd + i : ram_base + i - 3, 0x11u * (i + 1));
            vm_write_memory_1(ram_base + 4, 0xa5);
            const uint32 source[] = {shadow.gpr[1], shadow.gpr[2]};
            const uint32 ops[] = {(31u<<26)|(1u<<21)|(31u<<16)|(7u<<11)|((store ? 725u : 597u)<<1)};
            nw_jit_fn fn = nw_jit_compile(ops, 1, shadow.pc, shadow.pc & ~0xfffu, 2, 0); CHECK(fn != NULL);
            const uint64 misses = nw_jit_verify_misses();
            if (fn) CHECK(cpu->nw_jit_verify_block(shadow, fn, ops, 1) == 1);
            CHECK(nw_jit_verify_misses() == misses && !shadow.fault);
            if (!store) CHECK(shadow.gpr[1] == 0x11223344 && shadow.gpr[2] == 0x55667700);
            else for (unsigned i = 0; i < 7; ++i)
                CHECK(vm_read_memory_1(i < 3 ? ram_base + 0x1fffd + i : ram_base + i - 3) == uint8(source[i / 4] >> (24 - 8 * (i & 3))));
            CHECK(vm_read_memory_1(ram_base + 4) == 0xa5);
        }
        for (unsigned fault = 0; fault < 2; ++fault) {
            mmu.reset(); mmu.set_msr(ppc32_mmu::MSR_DR);
            mmu.set_dbat(0, 0x20000002u, ram_base | 2u);
            mmu.set_dbat(1, 0x20020002u, ram_base | (fault ? 0u : 2u));
            nw_jit_dtlb_flush_src(NW_JIT_DTLB_FL_BAT);	// a guest mtdbat would announce this; byte/halfword loads now fill the table too
            nw_jit_cpu shadow = string_start(0x2001fffb, 0x7c000 + fault * 64, 0);
            const uint32 before2 = shadow.gpr[2], before5 = shadow.gpr[5];
            for (unsigned i = 0; i < 8; ++i) vm_write_memory_1(i < 5 ? ram_base + 0x1fffb + i : ram_base + i - 5, 0x11u * (i + 1));
            const uint32 ops[] = {nw_ppc_lswi(1,31,8), nw_ppc_addi(5,0,99)};
            nw_jit_set_mode(NW_JIT_ON);
            shadow.host = cpu; nw_jit_cpu_bind(&shadow); nw_jit_tail_begin();
            nw_jit_fn fn = nw_jit_compile(ops, 2, shadow.pc, shadow.pc & ~0xfffu, 2, 0); CHECK(fn != NULL);
            if (fn) fn(&shadow);
            CHECK(shadow.gpr[1] == 0x11223344 && shadow.gpr[2] == (fault ? before2 : 0x55667788));
            CHECK(shadow.gpr[5] == (fault ? before5 : 99));
            CHECK(shadow.fault == (fault ? NW_JIT_FAULT_DSI : 0));
            if (fault) CHECK(shadow.fault_ea == 0x20020000 && shadow.fault_width == 2 && !shadow.fault_st);
            nw_jit_set_mode(NW_JIT_VERIFY);
        }
        // Destructive I/O string reads retain word/halfword/byte widths and
        // execute once in KPX; replay must perform no second device operation.
        for (unsigned translated = 0; translated < 2; ++translated) {
            mmu.reset(); mmu.set_msr(translated ? ppc32_mmu::MSR_DR : 0);
            if (translated) mmu.set_dbat(0, (NW_IO_VIA_PMU_BASE & 0xfffe0000u) | 2u, (NW_IO_VIA_PMU_BASE & 0xfffe0000u) | 2u);
            nw_jit_cpu shadow = string_start(NW_IO_VIA_PMU_BASE + 1, 0x7b000 + translated * 64, 0);
            device.value = 10;
            const unsigned reads = device.reads, writes = device.writes;
            const uint32 ops[] = {nw_ppc_lswi(1,31,7), nw_ppc_stswi(1,31,7)};
            nw_jit_fn fn = nw_jit_compile(ops, 2, shadow.pc, shadow.pc & ~0xfffu, translated ? 2 : 0, 0); CHECK(fn != NULL);
            const uint64 misses = nw_jit_verify_misses();
            if (fn) CHECK(cpu->nw_jit_verify_block(shadow, fn, ops, 2) == 1);
            CHECK(nw_jit_verify_misses() == misses && !shadow.fault);
            CHECK(device.reads == reads + 3 && device.writes == writes + 7 && device.value == 12);
            CHECK(shadow.gpr[1] == 10 && shadow.gpr[2] == 0x000b0c00);
        }

        // A bounded tape overflow keeps the completed reference authoritative
        // and never repeats the block's destructive device stores.
        {
            mmu.reset(); mmu.set_msr(0);
            const uint32 start = 0x7d000;
            nw_jit_cpu shadow = string_start(NW_IO_VIA_PMU_BASE + 1, start, 127);
            uint32 ops[NW_JIT_MAX_BLOCK];
            for (unsigned i = 0; i < NW_JIT_MAX_BLOCK; ++i)
                ops[i] = (31u<<26)|(1u<<21)|(31u<<11)|(661u<<1);
            nw_jit_fn fn = nw_jit_compile(ops, NW_JIT_MAX_BLOCK, start, start & ~0xfffu, 0, 0); CHECK(fn != NULL);
            const unsigned writes = device.writes;
            const uint64 misses = nw_jit_verify_misses();
            if (fn) CHECK(cpu->nw_jit_verify_block(shadow, fn, ops, NW_JIT_MAX_BLOCK) == 1);
            CHECK(device.writes == writes + 127 * NW_JIT_MAX_BLOCK);
            CHECK(cpu->pc() == start + 4 * NW_JIT_MAX_BLOCK && shadow.pc == start);
            CHECK(nw_jit_verify_misses() == misses && cpu->nw_verify_trace_ == NULL && shadow.verify_mem == NULL);
        }
        // SMC is a completed substore, not a reason to stop the instruction.
        // Continue out of the active code page and retain the final SMC EA.
        for (unsigned translated = 0; translated < 2; ++translated) {
            mmu.reset(); mmu.set_msr(translated ? ppc32_mmu::MSR_DR : 0);
            const uint32 logical = translated ? 0x20000000u : ram_base;
            if (translated) mmu.set_dbat(0, logical | 2u, ram_base | 2u);
            nw_jit_cpu shadow = string_start(logical + 0xffc, logical + 0x800, 0);
            cpu->last_fetch_pa_ = ram_base + 0x800;
            const uint32 ops[] = {nw_ppc_stswi(1,31,12), nw_ppc_addi(5,0,99)};
            uint32 source[3] = {shadow.gpr[1], shadow.gpr[2], shadow.gpr[3]};
            nw_jit_fn fn = nw_jit_compile(ops, 2, shadow.pc, ram_base, translated ? 2 : 0, 0); CHECK(fn != NULL);
            const uint64 misses = nw_jit_verify_misses();
            if (fn) CHECK(cpu->nw_jit_verify_block(shadow, fn, ops, 2) == 1);
            CHECK(nw_jit_verify_misses() == misses);
            CHECK(shadow.fault == NW_JIT_FAULT_SMC && shadow.fault_ea == logical + 0xfff && shadow.fault_width == 1);
            CHECK(shadow.pc + 4 == cpu->pc() && shadow.gpr[5] != 99);
            for (unsigned i = 0; i < 12; ++i)
                CHECK(vm_read_memory_1(ram_base + 0xffc + i) == uint8(source[i / 4] >> (24 - 8 * (i & 3))));
        }

        // Exercise the production host callbacks too, independently of the
        // observation tape: completed code writes, later DSI and suffix stops.
        for (unsigned verify = 0; verify < 2; ++verify)
        for (unsigned fault = 0; fault < 2; ++fault) {
            mmu.reset(); mmu.set_msr(ppc32_mmu::MSR_DR);
            mmu.set_dbat(0, 0x20000002u, ram_base | 2u);
            mmu.set_dbat(1, 0x20020002u, ram_base | (fault ? 0u : 2u));
            nw_jit_dtlb_flush_src(NW_JIT_DTLB_FL_BAT);	// a guest mtdbat would announce this; byte/halfword loads now fill the table too
            const uint32 start = 0x2001f800u + 64 * (verify * 2 + fault);
            nw_jit_cpu shadow = string_start(0x2001fffcu, start, 0);
            cpu->last_fetch_pa_ = ram_base + 0x1f800;
            const uint32 source[] = {shadow.gpr[1], shadow.gpr[2], shadow.gpr[3]};
            for (unsigned i = 0; i < 8; ++i) vm_write_memory_1(ram_base + i, 0xa5);
            const uint32 ops[] = {nw_ppc_stswi(1,31,12), nw_ppc_addi(5,0,99)};
            nw_jit_fn fn = nw_jit_compile(ops, 2, start, ram_base + 0x1f000, 2, 0); CHECK(fn != NULL);
            const uint64 misses = nw_jit_verify_misses();
            if (verify) {
                if (fn) CHECK(cpu->nw_jit_verify_block(shadow, fn, ops, 2) == 1);
                CHECK(nw_jit_verify_misses() == misses);
                if (fault) CHECK(cpu->dar_ == 0x20020000 && cpu->srr0_ == start);
            } else {
                nw_jit_set_mode(NW_JIT_ON);
                shadow.host = cpu; nw_jit_cpu_bind(&shadow); nw_jit_tail_begin();
                if (fn) fn(&shadow);
                nw_jit_set_mode(NW_JIT_VERIFY);
            }
            CHECK(shadow.fault == (fault ? NW_JIT_FAULT_DSI : NW_JIT_FAULT_SMC));
            CHECK(shadow.fault_ea == (fault ? 0x20020000u : 0x2001ffffu));
            CHECK(shadow.fault_width == 1 && shadow.fault_st == 1 && shadow.pc == start);
            CHECK(shadow.gpr[5] != 99);
            for (unsigned i = 0; i < 12; ++i)
                CHECK(vm_read_memory_1(i < 4 ? ram_base + 0x1fffc + i : ram_base + i - 4) ==
                    (fault && i >= 4 ? 0xa5 : uint8(source[i / 4] >> (24 - 8 * (i & 3)))));
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
        // Carry-producing overflow forms followed by a nonoverflowing OE
        // instruction prove sticky SO, OV replacement and CR0's new SO value
        // across selective state copying and both production successor paths.
        for (unsigned native_tail = 0; native_tail < 2; ++native_tail)
        for (unsigned kind = 0; kind < 5; ++kind)
        for (unsigned pending = 0; pending < 2; ++pending) {
            const uint32 start = ram_base + 0x18000 + 0x100 * (native_tail * 10 + kind * 2 + pending);
            mmu.reset(); mmu.set_msr(ppc32_mmu::MSR_EE);
            nw_jit_cpu before = string_start(ram_base, start, 0x41234567u);
            // Every first form overflows; its CA differs from the incoming CA.
            const uint32 a = kind == 2 || kind == 3 ? 0x7fffffffu : 0x80000000u;
            const unsigned ca = kind == 2 || kind == 4 ? 1u : 0u;
            const uint32 result = kind == 0 || kind == 2 || kind == 4 ? 0x80000000u : 0x7fffffffu;
            const unsigned carry = kind == 0 ? ca : kind == 1 || kind == 3;
            before.gpr[4] = a; cpu->gpr(4) = a;
            before.xer = 0x41234567u | (ca<<29); cpu->xer().set(before.xer);
            cpu->cr().set(0x12345678u); cpu->lr() = start + 0xc0;
            cpu->dec_ = 1000000; cpu->dec_tb_base_ = cpu->tb_host_ticks(); cpu->dec_pending_ = pending;
            cpu->spcflags().init();
            const uint32 op = (31u<<26)|(3u<<21)|(4u<<16)|(integer_xo[kind]<<1)|0x401u;
            const uint32 first[] = {nw_ppc_addi(21,20,9), op, nw_ppc_b(0x38,0)};
            const uint32 second[] = {(31u<<26)|(5u<<21)|(3u<<16)|((carry ? 712u : 714u)<<1)|1u,
                nw_ppc_addi(22,3,1), nw_ppc_blr()};
            // subfzeo maps positive -> negative; addzeo with CA=0 preserves
            // either sign, so the second instruction clears OV without overflow.
            const uint32 second_result = carry ? ~result + 1u : result;
            const unsigned second_ca = carry ? result == 0 : 0;
            const uint32 first_xer = 0xc1234567u | (carry<<29);
            const uint32 second_xer = 0x81234567u | (second_ca<<29);
            const uint32 first_cr = ((result & 0x80000000u ? 9u : 5u)<<28) | 0x02345678u;
            const uint32 second_cr = ((second_result & 0x80000000u ? 9u : 5u)<<28) | 0x02345678u;
            nw_jit_set_mode(NW_JIT_ON);
            CHECK(nw_jit_compile(first, 3, start, start & ~0xfffu, 0, 0) != NULL);
            CHECK(nw_jit_compile(second, 3, start + 0x40, start & ~0xfffu, 0, 0) != NULL);
            for (unsigned i = 0; i < 3; ++i) {
                vm_write_memory_4(start + i * 4, first[i]);
                vm_write_memory_4(start + 0x40 + i * 4, second[i]);
            }
            nw_jit_itlb_fill(start, start);
            nw_jit_set_host_chain(native_tail ? powerpc_cpu::jit_host_chain : NULL);
            const uint64 hops = nw_jit_chain_hops();
            CHECK(cpu->nw_jit_try(first[0]) == 1);
            CHECK(cpu->pc() == start + (pending ? 0x40 : 0xc0));
            CHECK(nw_jit_chain_hops() == hops + (pending ? 0 : 1));
            CHECK(nw_jit_tail_n() == (!pending && native_tail ? 3 : 0));
            CHECK(cpu->xer().get() == (pending ? first_xer : second_xer));
            CHECK(cpu->cr().get() == (pending ? first_cr : second_cr));
            for (unsigned r = 0; r < 32; ++r) {
                uint32 value = before.gpr[r];
                if (r == 3) value = result;
                if (r == 21) value = before.gpr[20] + 9;
                if (!pending && r == 5) value = second_result;
                if (!pending && r == 22) value = result + 1;
                CHECK(cpu->gpr(r) == value);
                CHECK(cpu->fpr_dw(r) == before.fpr[r]);
                for (unsigned w = 0; w < 4; ++w) CHECK(cpu->vr(r).w[w] == before.vr[r][w]);
            }
            CHECK(cpu->dec_pending_ == (pending != 0));
            cpu->dec_pending_ = false;
            nw_jit_set_host_chain(powerpc_cpu::jit_host_chain);
            nw_jit_set_mode(NW_JIT_VERIFY);
        }
        // Octet-shift blocks preserve state across integer predecessors and
        // successors, with either production chain path and pending gates.
        for (unsigned right = 0; right < 2; ++right)
        for (unsigned native_tail = 0; native_tail < 2; ++native_tail)
        for (unsigned stop = 0; stop < 4; ++stop)
        for (unsigned sh = 0; sh < 16; ++sh) {
            mmu.reset(); mmu.set_msr(ppc32_mmu::MSR_EE | (stop == 1 ? 0 : NW_MSR_VEC));
            const uint32 start = ram_base + 0x16000;
            nw_jit_invalidate_page(start);
            nw_jit_cpu before = vector_start(start,ram_base + 0x4000,false);
            before.vr[2][3] = cpu->vr(2).w[3] = sh << 3;
            cpu->lr() = start + 0xc0;
            cpu->dec_ = 1000000; cpu->dec_tb_base_ = cpu->tb_host_ticks(); cpu->dec_pending_ = stop == 2;
            if (stop == 3) cpu->spcflags().set(SPCFLAG_CPU_HANDLE_INTERRUPT);
            const uint32 first[] = {nw_ppc_addi(3,3,7),nw_ppc_b(0x3c,0)};
            const uint32 second[] = {right ? nw_ppc_vsro(1,1,2) : nw_ppc_vslo(1,1,2),
                (4u<<26)|(3u<<21)|(1u<<16)|(1u<<11)|1156u,nw_ppc_b(0x38,0)};
            const uint32 third[] = {nw_ppc_addi(21,20,9),nw_ppc_blr()};
            nw_jit_set_mode(NW_JIT_ON);
            CHECK(nw_jit_compile(first,2,start,start & ~0xfffu,0,0) != NULL);
            CHECK(nw_jit_compile(second,3,start + 0x40,start & ~0xfffu,0,0) != NULL);
            CHECK(nw_jit_compile(third,2,start + 0x80,start & ~0xfffu,0,0) != NULL);
            for (unsigned i = 0; i < 2; ++i) {
                vm_write_memory_4(start + 4 * i,first[i]);
                vm_write_memory_4(start + 0x80 + 4 * i,third[i]);
            }
            for (unsigned i = 0; i < 3; ++i) vm_write_memory_4(start + 0x40 + 4 * i,second[i]);
            nw_jit_itlb_fill(start,start);
            nw_jit_set_host_chain(native_tail ? powerpc_cpu::jit_host_chain : NULL);
            const uint64 hops = nw_jit_chain_hops();
            CHECK(cpu->nw_jit_try(first[0]) == 1);
            CHECK(cpu->pc() == start + (stop ? 0x40 : 0xc0));
            CHECK(nw_jit_chain_hops() == hops + (stop ? 0 : 2));
            CHECK(nw_jit_tail_n() == (!stop && native_tail ? 5 : 0));
            CHECK(cpu->cr().get() == before.cr && cpu->xer().get() == before.xer && cpu->vscr().get() == before.vscr);
            for (unsigned r = 0; r < 32; ++r) {
                CHECK(cpu->gpr(r) == (r == 3 ? before.gpr[r] + 7 : !stop && r == 21 ? before.gpr[20] + 9 : before.gpr[r]));
                CHECK(!cpu->fpr_dw(r));
                for (unsigned b = 0; b < 16; ++b) {
                    const int index = right ? int(b) - int(sh) : int(b) + int(sh);
                    const uint8 expected = stop || (r != 1 && r != 3) ? uint8(before.vr[r][b / 4] >> (24 - 8 * (b % 4))) :
                        index < 0 || index >= 16 ? 0 : uint8(before.vr[1][index / 4] >> (24 - 8 * (index % 4)));
                    CHECK(uint8(cpu->vr(r).w[b / 4] >> (24 - 8 * (b % 4))) == expected);
                }
            }
            CHECK(cpu->dec_pending_ == (stop == 2));
            cpu->spcflags().init(); cpu->dec_pending_ = false;
            nw_jit_set_host_chain(powerpc_cpu::jit_host_chain);
            nw_jit_set_mode(NW_JIT_VERIFY);
        }
        nw_jit_invalidate_page(ram_base + 0x16000);
        // Integer -> vector -> integer transitions retain every unselected
        // register and the saturated VSCR on native and ordinary C successors.
        // A disabled vector unit or pending event stops before the vector op.
        for (unsigned fp = 0; fp < 2; ++fp)
        for (unsigned native_tail = 0; native_tail < 2; ++native_tail)
        for (unsigned stop = 0; stop < 4; ++stop) {
            mmu.reset(); mmu.set_msr(ppc32_mmu::MSR_EE | (stop == 1 ? 0 : NW_MSR_VEC));
            const uint32 start = ram_base + 0x15000 + 0x100 * (fp * 8 + native_tail * 4 + stop);
            nw_jit_cpu before = vector_start(start, ram_base + 0x4000, false);
            for (unsigned w = 0; w < 4; ++w) {
                cpu->vr(1).w[w] = before.vr[1][w] = fp ? 0x3fc00000u : 0x7fffffffu;
                cpu->vr(2).w[w] = before.vr[2][w] = fp ? 0x40000000u : 0;
            }
            cpu->fpscr() = before.fpscr = 0x12340003;
            cpu->lr() = start + 0xc0;
            cpu->dec_ = 1000000; cpu->dec_tb_base_ = cpu->tb_host_ticks(); cpu->dec_pending_ = stop == 2;
            cpu->spcflags().init();
            if (stop == 3) cpu->spcflags().set(SPCFLAG_CPU_HANDLE_INTERRUPT);
            const uint32 first[] = {nw_ppc_addi(3,3,7), nw_ppc_b(0x3c,0)};
            const uint32 integer_ops[] = {(4u<<26)|(3u<<21)|(1u<<16)|(2u<<11)|1928u,
                nw_ppc_mfvscr(5),
                (4u<<26)|(10u<<21)|(1u<<16)|(1u<<11)|(2u<<6)|39u, // vmsumuhs
                (4u<<26)|(11u<<21)|(1u<<16)|(2u<<11)|270u, // vpkshus
                (4u<<26)|(12u<<21)|(10u<<16)|(2u<<11)|1670u, // vcmpgtuw.
                nw_ppc_b(0x2c,0)};
            const uint32 fp_ops[] = {
                (4u<<26)|(3u<<21)|(1u<<16)|(2u<<11)|10u, // 1.5 + 2.0 = 3.5
                (4u<<26)|(10u<<21)|(1u<<16)|(3u<<11)|(2u<<6)|46u, // fused 1.5*2 + 3.5 = 6.5
                (4u<<26)|(11u<<21)|(10u<<11)|970u, // truncates to 6
                (4u<<26)|(12u<<21)|(10u<<16)|(1u<<11)|1734u,
                nw_ppc_mfvscr(5), nw_ppc_b(0x2c,0)};
            const uint32 *second = fp ? fp_ops : integer_ops;
            const uint32 third[] = {nw_ppc_addi(21,20,9), nw_ppc_blr()};
            nw_jit_set_mode(NW_JIT_ON);
            CHECK(nw_jit_compile(first, 2, start, start & ~0xfffu, 0, 0) != NULL);
            CHECK(nw_jit_compile(second, 6, start + 0x40, start & ~0xfffu, 0, 0) != NULL);
            CHECK(nw_jit_compile(third, 2, start + 0x80, start & ~0xfffu, 0, 0) != NULL);
            for (unsigned i = 0; i < 2; ++i) {
                vm_write_memory_4(start + i * 4, first[i]);
                vm_write_memory_4(start + 0x80 + i * 4, third[i]);
            }
            for (unsigned i = 0; i < 6; ++i) vm_write_memory_4(start + 0x40 + i * 4, second[i]);
            nw_jit_itlb_fill(start, start);
            nw_jit_set_host_chain(native_tail ? powerpc_cpu::jit_host_chain : NULL);
            const uint64 hops = nw_jit_chain_hops();
            CHECK(cpu->nw_jit_try(first[0]) == 1);
            CHECK(cpu->pc() == start + (stop ? 0x40 : 0xc0));
            CHECK(nw_jit_chain_hops() == hops + (stop ? 0 : 2));
            CHECK(nw_jit_tail_n() == (!stop && native_tail ? 8 : 0));
            CHECK(cpu->vscr().get() == (stop || fp ? 0x10000u : 0x10001u));
            for (unsigned r = 0; r < 32; ++r) {
                uint32 value = before.gpr[r];
                if (r == 3) value += 7;
                if (!stop && r == 21) value = before.gpr[20] + 9;
                CHECK(cpu->gpr(r) == value); CHECK(cpu->fpr_dw(r) == before.fpr[r]);
                for (unsigned w = 0; w < 4; ++w) {
                    uint32 vector = before.vr[r][w];
                    if (!stop && r == 3) vector = fp ? 0x40600000u : w == 3 ? 0x7fffffffu : 0;
                    if (!stop && r == 5) vector = w == 3 ? (fp ? 0x10000u : 0x10001u) : 0;
                    if (!stop && r == 10) vector = fp ? 0x40d00000u : UINT32_MAX;
                    if (!stop && r == 11) vector = fp ? 6u : w < 2 ? 0xff00ff00u : 0;
                    if (!stop && r == 12) vector = UINT32_MAX;
                    CHECK(cpu->vr(r).w[w] == vector);
                }
            }
            CHECK(cpu->cr().get() == (stop ? before.cr : ((before.cr & ~0xf0u) | 0x80u)) && cpu->xer().get() == before.xer);
            CHECK(cpu->fpscr() == before.fpscr);
            CHECK(cpu->dec_pending_ == (stop == 2));
            cpu->spcflags().init(); cpu->dec_pending_ = false;
            nw_jit_set_host_chain(powerpc_cpu::jit_host_chain);
            nw_jit_set_mode(NW_JIT_VERIFY);
        }
        cpu->vscr().set(0);
        // Production selective marshalling and both successor paths. Disable
        // only the native-tail callback in this private fixture to force the
        // ordinary C successor loop; the same compiled blocks run both ways.
        for (unsigned native_tail = 0; native_tail < 2; ++native_tail)
        for (unsigned stop = 0; stop < 4; ++stop) {
            mmu.reset();
            mmu.set_msr(ppc32_mmu::MSR_DR | ppc32_mmu::MSR_EE | (stop == 1 ? 0 : ppc32_mmu::MSR_FP));
            const uint32 start = ram_base + 0x10000 + 0x100 * (native_tail * 4 + stop);
            nw_jit_cpu before = string_start(ram_base, start, 0x80000000u);
            cpu->fpr_dw(1) = bits(1.25); cpu->fpr_dw(2) = bits(2.5);
            for (unsigned r = 3; r < 32; ++r) cpu->fpr_dw(r) = bits(100.0 + r);
            cpu->lr() = start + 0xc0;
            cpu->dec_ = 1000000; cpu->dec_tb_base_ = cpu->tb_host_ticks(); cpu->dec_pending_ = stop == 2;
            cpu->spcflags().init();
            if (stop == 3) cpu->spcflags().set(SPCFLAG_CPU_HANDLE_INTERRUPT);
            const uint32 first[] = {nw_ppc_addi(3,3,7), nw_ppc_b(0x3c,0)};
            const uint32 second[] = {nw_ppc_fadds(3,1,2), nw_ppc_b(0x3c,0)};
            const uint32 third[] = {nw_ppc_addi(21,20,9), nw_ppc_addi(22,3,1), nw_ppc_blr()};
            nw_jit_set_mode(NW_JIT_ON);
            CHECK(nw_jit_compile(first, 2, start, start & ~0xfffu, 2, 0) != NULL);
            CHECK(nw_jit_compile(second, 2, start + 0x40, start & ~0xfffu, 2, 0) != NULL);
            CHECK(nw_jit_compile(third, 3, start + 0x80, start & ~0xfffu, 2, 0) != NULL);
            for (unsigned i = 0; i < 2; ++i) {
                vm_write_memory_4(start + i * 4, first[i]);
                vm_write_memory_4(start + 0x40 + i * 4, second[i]);
            }
            for (unsigned i = 0; i < 3; ++i) vm_write_memory_4(start + 0x80 + i * 4, third[i]);
            nw_jit_itlb_fill(start, start);
            nw_jit_set_host_chain(native_tail ? powerpc_cpu::jit_host_chain : NULL);
            const uint64 hops = nw_jit_chain_hops();
            CHECK(cpu->nw_jit_try(first[0]) == 1);
            CHECK(cpu->pc() == start + (stop ? 0x40 : 0xc0));
            CHECK(nw_jit_chain_hops() == hops + (stop ? 0 : 2));
            CHECK(nw_jit_tail_n() == (!stop && native_tail ? 5 : 0));
            CHECK(cpu->nw_jc_->gpr_live == ((1u<<3)|(1u<<20)|(1u<<21)|(1u<<22)));
            for (unsigned r = 0; r < 32; ++r) {
                uint32 value = before.gpr[r];
                if (r == 3) value += 7;
                if (!stop && r == 21) value = before.gpr[20] + 9;
                if (!stop && r == 22) value = before.gpr[3] + 8;
                CHECK(cpu->gpr(r) == value);
                const uint64 fp = r == 0 ? 0 : r == 1 ? bits(1.25) : r == 2 ? bits(2.5) :
                                  r == 3 && !stop ? bits(3.75) : bits(100.0 + r);
                CHECK(cpu->fpr_dw(r) == fp);
                for (unsigned w = 0; w < 4; ++w) CHECK(cpu->vr(r).w[w] == 0);
            }
            CHECK(cpu->cr().get() == 0x12345678 && cpu->xer().get() == 0x80000000u);
            CHECK(cpu->lr() == start + 0xc0 && cpu->ctr() == 0);
            CHECK(cpu->dec_pending_ == (stop == 2));
            cpu->spcflags().init(); cpu->dec_pending_ = false;
            nw_jit_set_host_chain(powerpc_cpu::jit_host_chain);
            nw_jit_set_mode(NW_JIT_VERIFY);
        }
        // A fault in a chained string instruction must deliver the live DSI
        // at the successor PC, preserving the entry and subaccess prefixes.
        for (unsigned native_tail = 0; native_tail < 2; ++native_tail)
        for (unsigned store = 0; store < 2; ++store) {
            mmu.reset(); mmu.set_msr(ppc32_mmu::MSR_DR);
            mmu.set_dbat(0, 0x20000002u, ram_base | 2u);
            mmu.set_dbat(1, 0x20020002u, ram_base);
            nw_jit_dtlb_flush_src(NW_JIT_DTLB_FL_BAT);	// a guest mtdbat would announce this; byte/halfword loads now fill the table too
            const uint32 start = ram_base + 0x11000 + 0x100 * (native_tail * 2 + store);
            nw_jit_cpu before = string_start(0x2001fffc, start, 0x80000000);
            cpu->lr() = start + 0xc0;
            cpu->dec_ = 1000000; cpu->dec_tb_base_ = cpu->tb_host_ticks(); cpu->dec_pending_ = false;
            cpu->spcflags().init();
            vm_write_memory_4(ram_base + 0x1fffc, 0x11223344);
            const uint32 first[] = {nw_ppc_addi(3,3,7), nw_ppc_b(0x3c,0)};
            const uint32 second[] = {(31u<<26)|(30u<<21)|(5u<<16)|(9u<<11)|((store ? 725u : 597u)<<1), nw_ppc_addi(22,0,99), nw_ppc_blr()};
            nw_jit_set_mode(NW_JIT_ON);
            CHECK(nw_jit_compile(first, 2, start, start & ~0xfffu, 2, 0) != NULL);
            CHECK(nw_jit_compile(second, 3, start + 0x40, start & ~0xfffu, 2, 0) != NULL);
            for (unsigned i = 0; i < 2; ++i) vm_write_memory_4(start + i * 4, first[i]);
            for (unsigned i = 0; i < 3; ++i) vm_write_memory_4(start + 0x40 + i * 4, second[i]);
            nw_jit_itlb_fill(start, start);
            nw_jit_set_host_chain(native_tail ? powerpc_cpu::jit_host_chain : NULL);
            const uint64 hops = nw_jit_chain_hops();
            CHECK(cpu->nw_jit_try(first[0]) == 1);
            CHECK(nw_jit_chain_hops() == hops + 1);
            CHECK(cpu->pc() == 0x300 && cpu->srr0_ == start + 0x40 && cpu->dar_ == 0x20020000);
            CHECK((cpu->dsisr_ & 0x02000000u) == (store ? 0x02000000u : 0));
            CHECK((mmu.msr() & ppc32_mmu::MSR_DR) == 0);
            CHECK(cpu->nw_jc_->fault_ea == 0x20020000 && cpu->nw_jc_->fault_width == (store ? 1u : 4u));
            CHECK(cpu->nw_jc_->fault_st == store && cpu->nw_jc_->pc == start + 0x40);
            for (unsigned r = 0; r < 32; ++r) {
                uint32 value = before.gpr[r];
                if (r == 3) value += 7;
                if (!store && r == 30) value = 0x11223344;
                CHECK(cpu->gpr(r) == value);
            }
            CHECK(vm_read_memory_4(ram_base + 0x1fffc) == (store ? before.gpr[30] : 0x11223344));
            CHECK(cpu->cr().get() == 0x12345678 && cpu->xer().get() == 0x80000000u);
            nw_jit_set_host_chain(powerpc_cpu::jit_host_chain);
            nw_jit_set_mode(NW_JIT_VERIFY);
        }
        // Atomic replay uses physical reservation addresses and ordered
        // translation probes, including a conditional store that stores nothing.
        auto atomic_run = [&](const uint32 *ops, unsigned n, bool verify) {
            nw_jit_cpu shadow = {};
            shadow.pc = cpu->pc(); shadow.dec = cpu->dec_; shadow.msr = mmu.msr();
            shadow.cr = cpu->cr().get(); shadow.xer = cpu->xer().get();
            shadow.lr = cpu->lr(); shadow.ctr = cpu->ctr(); shadow.vscr = cpu->vscr().get();
            for (unsigned r = 0; r < 32; ++r) {
                shadow.gpr[r] = cpu->gpr(r); shadow.fpr[r] = cpu->fpr_dw(r);
                for (unsigned w = 0; w < 4; ++w) shadow.vr[r][w] = cpu->vr(r).w[w];
            }
            nw_jit_set_mode(verify ? NW_JIT_VERIFY : NW_JIT_ON);
            const uint32 key = ((mmu.msr() & ppc32_mmu::MSR_IR) ? 1u : 0u) | ((mmu.msr() & ppc32_mmu::MSR_DR) ? 2u : 0u);
            nw_jit_fn fn = nw_jit_compile(ops, n, shadow.pc, cpu->last_fetch_pa_ & ~0xfffu, key, 0); CHECK(fn != NULL);
            const uint64 misses = nw_jit_verify_misses();
            if (fn) CHECK(verify ? cpu->nw_jit_verify_block(shadow, fn, ops, n) == 1 : cpu->nw_jit_try(ops[0]) == 1);
            CHECK(nw_jit_verify_misses() == misses);
            if (verify) {
                CHECK(shadow.reserve_valid == cpu->regs().reserve_valid && shadow.reserve_ea == cpu->regs().reserve_addr);
                CHECK(shadow.verify_mem == NULL && shadow.verify_xlate == NULL && shadow.host == NULL);
            } else shadow = *cpu->nw_jc_;
            nw_jit_set_mode(NW_JIT_VERIFY);
            return shadow;
        };
        const uint32 reserve_load = (31u<<26)|(3u<<21)|(4u<<16)|(6u<<11)|(20u<<1);
        const uint32 conditional_store = (31u<<26)|(3u<<21)|(5u<<16)|(6u<<11)|(150u<<1)|1u;
        unsigned atomic_case = 0;
        for (unsigned verify = 0; verify < 2; ++verify)
        for (unsigned translated = 0; translated < 3; ++translated)
        for (unsigned so = 0; so < 2; ++so)
        for (unsigned scenario = 0; scenario < 6; ++scenario) {
            mmu.reset(); mmu.set_msr(translated ? ppc32_mmu::MSR_DR : 0);
            const uint32 logical = translated ? 0x20000000u : ram_base;
            const uint32 alias = translated == 2 ? 0x30000000u : logical;
            if (translated) mmu.set_dbat(0, logical | 2u, ram_base | 2u);
            if (translated == 2) mmu.set_dbat(1, alias | 2u, ram_base | 2u);
            const bool code = scenario >= 4, pair = scenario == 0 || scenario == 4;
            const uint32 start = code ? logical + 0x14800 + 64 * (atomic_case % 8) : 0x90000 + 64 * atomic_case;
            ++atomic_case;
            nw_jit_cpu before = string_start(logical + 0x14008, start, so << 31);
            cpu->gpr(4) = logical + 0x14008; cpu->gpr(5) = alias + 0x14008; cpu->gpr(6) = 0;
            cpu->cr().set(0xf2345678); cpu->last_fetch_pa_ = code ? ram_base + (start & 0x1ffffu) : start;
            cpu->regs().reserve_valid = scenario == 2;
            cpu->regs().reserve_addr = ram_base + 0x1400c;
            cpu->dec_pending_ = false; cpu->spcflags().init();
            vm_write_memory_4(ram_base + 0x14008, 0x11223344);
            uint32 ops[4]; unsigned n = 0;
            if (pair || scenario == 3) { ops[n++] = reserve_load; ops[n++] = nw_ppc_addi(3,3,7); }
            if (scenario != 3) ops[n++] = conditional_store;
            ops[n++] = nw_ppc_addi(7,0,99);
            const nw_jit_cpu result = atomic_run(ops, n, verify != 0);
            const bool success = pair;
            CHECK(cpu->gpr(3) == (pair || scenario == 3 ? 0x1122334bu : before.gpr[3]));
            CHECK(cpu->gpr(7) == (scenario == 4 ? before.gpr[7] : 99));
            CHECK(vm_read_memory_4(ram_base + 0x14008) == (success ? 0x1122334bu : 0x11223344u));
            CHECK(cpu->cr().get() == (scenario == 3 ? 0xf2345678u : ((so | (success ? 2u : 0u)) << 28) | 0x02345678u));
            CHECK(cpu->xer().get() == (so << 31));
            CHECK(cpu->regs().reserve_valid == (scenario == 3));
            CHECK(cpu->regs().reserve_addr == (pair || scenario == 3 ? ram_base + 0x14008 : ram_base + 0x1400c));
            CHECK(result.fault == (scenario == 4 ? NW_JIT_FAULT_SMC : 0));
            CHECK(cpu->pc() == start + (scenario == 4 ? 12 : 4 * n));
            if (scenario == 4) CHECK(result.fault_ea == alias + 0x14008 && result.fault_width == 4 && result.fault_st == 1);
            if (scenario == 3) {
                // A reservation survives a block exit; a different logical
                // address for the same PA must still complete its store.
                const uint32 next = cpu->pc(); cpu->last_fetch_pa_ = next;
                const nw_jit_cpu stored = atomic_run(&conditional_store, 1, verify != 0);
                CHECK(!stored.fault && cpu->pc() == next + 4);
                CHECK(!cpu->regs().reserve_valid && vm_read_memory_4(ram_base + 0x14008) == 0x1122334b);
                CHECK(cpu->cr().get() == ((so | 2u) << 28 | 0x02345678u));
            }
        }
        // Failed translations preserve the previous reservation and CR, even
        // without a reservation; a protected load cannot publish a new one.
        for (unsigned verify = 0; verify < 2; ++verify)
        for (unsigned load = 0; load < 2; ++load)
        for (unsigned valid = 0; valid < 2; ++valid) {
            mmu.reset(); mmu.set_msr(ppc32_mmu::MSR_DR);
            mmu.set_dbat(0, 0x20000002u, ram_base | (load ? 0u : 1u));
            const uint32 start = 0xa0000 + 64 * atomic_case++;
            nw_jit_cpu before = string_start(0x20014008, start, 0x80000000);
            cpu->gpr(4) = cpu->gpr(5) = 0x20014008; cpu->gpr(6) = 0;
            cpu->cr().set(0xf2345678); cpu->regs().reserve_valid = valid; cpu->regs().reserve_addr = ram_base + 0x1400c;
            vm_write_memory_4(ram_base + 0x14008, 0x11223344);
            const uint32 ops[] = {load ? reserve_load : conditional_store, nw_ppc_addi(7,0,99)};
            const nw_jit_cpu result = atomic_run(ops, 2, verify != 0);
            CHECK(result.fault == NW_JIT_FAULT_DSI && result.pc == start);
            CHECK(result.fault_ea == 0x20014008 && result.fault_width == 4 && result.fault_st == !load);
            CHECK(cpu->pc() == 0x300 && cpu->dar_ == 0x20014008 && cpu->srr0_ == start);
            CHECK(cpu->regs().reserve_valid == valid && cpu->regs().reserve_addr == ram_base + 0x1400c);
            CHECK(cpu->cr().get() == 0xf2345678 && cpu->gpr(3) == before.gpr[3] && cpu->gpr(7) == before.gpr[7]);
            CHECK(vm_read_memory_4(ram_base + 0x14008) == 0x11223344);
        }
        // Destructive device accesses execute only once; production callbacks
        // must use the device decoder rather than a raw host pointer at the PA.
        for (unsigned verify = 0; verify < 2; ++verify)
        for (unsigned translated = 0; translated < 2; ++translated) {
            mmu.reset(); mmu.set_msr(translated ? ppc32_mmu::MSR_DR : 0);
            if (translated) mmu.set_dbat(0, (NW_IO_VIA_PMU_BASE & 0xfffe0000u) | 2u, (NW_IO_VIA_PMU_BASE & 0xfffe0000u) | 2u);
            const uint32 start = 0xb0000 + 64 * atomic_case++;
            string_start(NW_IO_VIA_PMU_BASE, start, 0x80000000);
            cpu->gpr(4) = cpu->gpr(5) = NW_IO_VIA_PMU_BASE; cpu->gpr(6) = 0;
            cpu->regs().reserve_valid = 0; cpu->regs().reserve_addr = 0;
            device.value = 10; const unsigned reads = device.reads, writes = device.writes;
            const uint32 ops[] = {reserve_load, nw_ppc_addi(3,3,7), conditional_store};
            const nw_jit_cpu result = atomic_run(ops, 3, verify != 0);
            CHECK(!result.fault && cpu->gpr(3) == 17 && device.value == 17);
            CHECK(device.reads == reads + 1 && device.writes == writes + 1);
            CHECK(cpu->cr().get() == 0x32345678 && !cpu->regs().reserve_valid);
        }
        // Production tails and C successors carry the live reservation through
        // an aliasing conditional store, permission fault or pending-event stop.
        for (unsigned native_tail = 0; native_tail < 2; ++native_tail)
        for (unsigned outcome = 0; outcome < 4; ++outcome) {
            mmu.reset(); mmu.set_msr(ppc32_mmu::MSR_DR | ppc32_mmu::MSR_EE);
            mmu.set_dbat(0, 0x20000002u, ram_base | 2u);
            mmu.set_dbat(1, 0x30000002u, ram_base | (outcome == 1 ? 1u : 2u));
            const uint32 start = ram_base + 0x17000 + 0x100 * (native_tail * 4 + outcome);
            const nw_jit_cpu before = string_start(0x20014008, start, 0x80000000);
            cpu->gpr(4) = 0x20014008; cpu->gpr(5) = 0x30014008 + (outcome == 3 ? 4 : 0); cpu->gpr(6) = 0;
            cpu->cr().set(0xf2345678); cpu->regs().reserve_valid = 0; cpu->regs().reserve_addr = 0;
            cpu->lr() = start + 0xc0;
            cpu->dec_ = 1000000; cpu->dec_tb_base_ = cpu->tb_host_ticks(); cpu->dec_pending_ = outcome == 2;
            cpu->spcflags().init();
            vm_write_memory_4(ram_base + 0x14008, 0x11223344); vm_write_memory_4(ram_base + 0x1400c, 0x55667788);
            const uint32 first[] = {reserve_load, nw_ppc_addi(3,3,7), nw_ppc_b(0x38,0)};
            const uint32 second[] = {conditional_store, nw_ppc_addi(7,0,99), nw_ppc_blr()};
            nw_jit_set_mode(NW_JIT_ON);
            CHECK(nw_jit_compile(first, 3, start, start & ~0xfffu, 2, 0) != NULL);
            CHECK(nw_jit_compile(second, 3, start + 0x40, start & ~0xfffu, 2, 0) != NULL);
            for (unsigned i = 0; i < 3; ++i) {
                vm_write_memory_4(start + i * 4, first[i]); vm_write_memory_4(start + 0x40 + i * 4, second[i]);
            }
            nw_jit_itlb_fill(start, start);
            nw_jit_set_host_chain(native_tail ? powerpc_cpu::jit_host_chain : NULL);
            const uint64 hops = nw_jit_chain_hops();
            CHECK(cpu->nw_jit_try(first[0]) == 1);
            CHECK(nw_jit_chain_hops() == hops + (outcome == 2 ? 0 : 1));
            CHECK(cpu->gpr(3) == 0x1122334b && cpu->gpr(7) == (outcome == 1 || outcome == 2 ? before.gpr[7] : 99));
            CHECK(cpu->regs().reserve_valid == (outcome == 1 || outcome == 2) && cpu->regs().reserve_addr == ram_base + 0x14008);
            CHECK(cpu->cr().get() == (outcome == 1 || outcome == 2 ? 0xf2345678u : outcome == 3 ? 0x12345678u : 0x32345678u));
            CHECK(vm_read_memory_4(ram_base + 0x14008) == (outcome == 0 ? 0x1122334bu : 0x11223344u));
            CHECK(vm_read_memory_4(ram_base + 0x1400c) == 0x55667788);
            CHECK(cpu->pc() == (outcome == 1 ? 0x300 : outcome == 2 ? start + 0x40 : start + 0xc0));
            if (outcome == 1) CHECK(cpu->dar_ == 0x30014008 && cpu->srr0_ == start + 0x40 && cpu->nw_jc_->fault_width == 4 && cpu->nw_jc_->fault_st == 1);
            CHECK(cpu->dec_pending_ == (outcome == 2));
            cpu->dec_pending_ = false;
            nw_jit_set_host_chain(powerpc_cpu::jit_host_chain);
            nw_jit_set_mode(NW_JIT_VERIFY);
        }
        // A ROM conditional store follows the existing ignored-store policy:
        // EQ succeeds, the reservation clears and physical ROM bytes stay put.
        nw_banks_set(NW_PA_RAM, ram_base, 0x1e000);
        nw_banks_set(NW_PA_ROM, ram_base + 0x1e000, 0x2000);
        for (unsigned verify = 0; verify < 2; ++verify) {
            mmu.reset(); mmu.set_msr(0);
            string_start(ram_base + 0x1e008, 0xc0000 + verify * 64, 0);
            cpu->gpr(4) = cpu->gpr(5) = ram_base + 0x1e008; cpu->gpr(6) = 0;
            cpu->regs().reserve_valid = 0; cpu->regs().reserve_addr = 0;
            vm_write_memory_4(ram_base + 0x1e008, 0x11223344);
            const uint32 ops[] = {reserve_load, nw_ppc_addi(3,3,7), conditional_store};
            const nw_jit_cpu result = atomic_run(ops, 3, verify != 0);
            CHECK(!result.fault && cpu->gpr(3) == 0x1122334b && !cpu->regs().reserve_valid);
            CHECK(cpu->cr().get() == 0x22345678 && vm_read_memory_4(ram_base + 0x1e008) == 0x11223344);
        }
        nw_banks_set(NW_PA_RAM, ram_base, 0x20000);
        // Unmapped PA callbacks fail before dereferencing a host pointer or
        // altering an old reservation, including a conditional-store mismatch.
        mmu.reset(); mmu.set_msr(0);
        for (unsigned store = 0; store < 2; ++store) {
            cpu->regs().reserve_valid = 1; cpu->regs().reserve_addr = ram_base;
            int fault = 0;
            if (store) CHECK(powerpc_cpu::jit_host_stwcx(cpu, 0x60000000, 77, 0x1000, &fault) == 0);
            else CHECK(powerpc_cpu::jit_host_lwarx(cpu, 0x60000000, 0x1000, &fault) == 0);
            CHECK(fault == NW_JIT_FAULT_DSI && cpu->regs().reserve_valid == 1 && cpu->regs().reserve_addr == ram_base);
        }
        cpu->regs().reserve_valid = 0; cpu->regs().reserve_addr = 0;
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
        (31u<<26)|(3u<<21)|(1u<<11)|(597u<<1), // stale lswi, empty tape
        (31u<<26)|(3u<<21)|(1u<<11)|(725u<<1), // stale stswi, empty tape
        (31u<<26)|(3u<<21)|(22u<<16)|(339u<<1), // mfspr DEC
        (31u<<26)|(3u<<21)|(22u<<16)|(467u<<1), // mtspr DEC
        (4u<<26)|2047u // invalid VMX must not borrow the live CPU
    };
    unsigned unsafe_case = 0;
    for (uint32 op : unsafe) {
        nw_jit_verify_trace trace;
        nw_jit_cpu shadow = {}; shadow.pc = 0x30000 + 64 * unsafe_case++;
        shadow.verify_mem = nw_jit_verify_trace::replay; shadow.verify_context = &trace;
        shadow.gpr[3] = 0x30; shadow.gpr[4] = 0x8000;
        uint32 ops[] = {op};
        nw_jit_fn fn = nw_jit_compile(ops, 1, shadow.pc, shadow.pc & ~0xfffu, 0, 0);
        if ((op >> 26) == 4) {
            CHECK(fn == NULL);
            CHECK(nw_jit_interp_n(&shadow, ops, 1, shadow.pc) == -1);
            CHECK(!shadow.fault && trace.count == 0 && trace.cursor == 0);
            continue;
        }
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

    // The data-TLB memo the 68k layer uses for reference-bit commits and writable previews: a recorded-read note
    // lasts until the entry is refilled or dropped, and a store lookup needs a writable entry.
    {
        const uint32 ea = 0x6e000;
        uint32 pa = 0;
        nw_jit_dtlb_drop_page(ea, NW_JIT_DTLB_FL_RESET);
        CHECK(!nw_jit_dtlb_take_rec(ea, 0));			// no entry: nothing to mark
        nw_jit_dtlb_fill(ea, 0x9000, 0, 0, 0);
        CHECK(nw_jit_dtlb_lookup_pr(ea, 0, &pa, 0) && pa == 0x9000);
        CHECK(!nw_jit_dtlb_lookup_pr(ea, 1, &pa, 0));		// read-only entry misses a store lookup
        CHECK(!nw_jit_dtlb_take_rec(ea, 1));			// other privilege: a miss, and it does not mark
        CHECK(!nw_jit_dtlb_take_rec(ea, 0));			// first note marks the entry
        CHECK(nw_jit_dtlb_take_rec(ea, 0));			// and the second reports it done
        nw_jit_dtlb_fill(ea, 0x9000, 1, 0, 0);			// a refill forgets it
        CHECK(nw_jit_dtlb_lookup_pr(ea, 1, &pa, 0) && pa == 0x9000);
        CHECK(!nw_jit_dtlb_take_rec(ea, 0));
        CHECK(nw_jit_dtlb_take_rec(ea, 0));
        nw_jit_dtlb_drop_page(ea, NW_JIT_DTLB_FL_RESET);	// a tlbie drops the entry and with it the note
        CHECK(!nw_jit_dtlb_take_rec(ea, 0));
        nw_jit_dtlb_fill(ea, 0x9000, 0, 0, 0);
        CHECK(!nw_jit_dtlb_take_rec(ea, 0));			// the refill after the drop starts unrecorded
        // A writable flag is not proof of a recorded store translation: only the explicit mark is.
        nw_jit_dtlb_fill(ea, 0x9000, 1, 0, 0);
        CHECK(!nw_jit_dtlb_store_rec(ea, 0, &pa));
        nw_jit_dtlb_mark_store_rec(ea, 1);			// other privilege: no live entry, no mark
        CHECK(!nw_jit_dtlb_store_rec(ea, 0, &pa));
        nw_jit_dtlb_mark_store_rec(ea, 0);
        CHECK(nw_jit_dtlb_store_rec(ea, 0, &pa) && pa == 0x9000);
        CHECK(!nw_jit_dtlb_store_rec(ea, 1, &pa));
        nw_jit_dtlb_fill(ea, 0x9000, 1, 0, 0);			// a refill clears the mark
        CHECK(!nw_jit_dtlb_store_rec(ea, 0, &pa));
        nw_jit_dtlb_fill(ea, 0x9000, 0, 0, 0);			// a read-only entry cannot be marked
        nw_jit_dtlb_mark_store_rec(ea, 0);
        CHECK(!nw_jit_dtlb_store_rec(ea, 0, &pa));
        nw_jit_dtlb_drop_page(ea, NW_JIT_DTLB_FL_RESET);
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
    // Typed observations fail closed on missing/incorrect translations.
    // A memory event cannot be substituted for a reservation translation, and
    // a different translated PA must not satisfy an existing reservation.
    for (unsigned mutation = 0; mutation < 4; ++mutation) {
        nw_jit_verify_trace trace;
        if (mutation == 1) trace.record(0xd0000, 0x8000, 4, false, 0x10000000);
        else trace.record_translation(0xd0000, 0x8000, mutation == 2 ? 8 : 4, false, 0x10000000);
        trace.record(0xd0000, 0x8000, 4, false, 42);
        trace.record_translation(0xd0008, 0x9000, 4, true, mutation == 3 ? 0x10000004 : 0x10000000);
        trace.record(0xd0008, 0x9000, 4, true, 49);
        uint8 poison[32]; memset(poison, 0x7f, sizeof poison);
        nw_jit_cpu shadow = {}; shadow.pc = 0xd0000; shadow.gpr[4] = 0x8000; shadow.gpr[5] = 0x9000;
        shadow.mem = poison; shadow.mem_base = 0x8000; shadow.mem_size = sizeof poison;
        shadow.verify_mem = nw_jit_verify_trace::replay; shadow.verify_context = &trace;
        shadow.verify_xlate = nw_jit_verify_trace::replay_translation;
        const uint32 ops[] = {(31u<<26)|(3u<<21)|(4u<<16)|(20u<<1), nw_ppc_addi(3,3,7),
                              (31u<<26)|(3u<<21)|(5u<<16)|(150u<<1)|1u};
        nw_jit_fn fn = nw_jit_compile(ops, 3, 0xd0000 + mutation * 0x40, 0xd0000, 0, 0); CHECK(fn != NULL);
        // Tape PCs match the freshly compiled fixture for every mutation.
        for (unsigned i = 0; i < trace.count; ++i) trace.accesses[i].pc += mutation * 0x40;
        shadow.pc += mutation * 0x40;
        nw_jit_cpu_bind(&shadow); nw_jit_tail_begin();
        if (fn) fn(&shadow);
        CHECK(trace.complete() == (mutation == 0));
        if (!mutation) CHECK(shadow.gpr[3] == 49 && shadow.cr == 0x20000000 && !shadow.reserve_valid && shadow.reserve_ea == 0x10000000);
        CHECK(poison[0] == 0x7f && poison[31] == 0x7f);
    }
    // Full-vector replay must reject missing or reordered subaccesses,
    // translation substitutions, wrong widths and altered store values.
    // Poisoned live DTLB/RAM cannot be used as a substitute for the tape.
    for (unsigned store = 0; store < 2; ++store)
    for (unsigned mutation = 0; mutation < 8; ++mutation) {
        nw_jit_verify_trace trace;
        const uint32 start = 0xe0000 + 64 * (store * 8 + mutation);
        if (mutation == 1) trace.record(start, 0x8000, 16, store != 0, 0x10000000);
        else trace.record_translation(start, mutation == 2 ? 0x8010 : 0x8000,
            mutation == 3 ? 4 : 16, store != 0, 0x10000000,
            mutation == 7 ? NW_JIT_FAULT_DSI : 0);
        if (mutation != 7) for (unsigned w = 0; w < 4; ++w) {
            if (mutation == 5 && w == 3) continue;
            trace.record(start, 0x8000 + 4 * (mutation == 4 ? 3 - w : w),
                mutation == 6 ? 2 : 4, store != 0, 0x01020304u + w + (mutation == 0 && store ? 1 : 0));
        }
        uint8 poison[4096]; memset(poison, 0x7f, sizeof poison);
        nw_jit_dtlb_fill(0x8000, 0x10000000, 1, (uint64)(uintptr)poison, 0, 1);
        nw_jit_cpu shadow = {}; shadow.pc = start; shadow.gpr[4] = 0x800f;
        shadow.mem = poison; shadow.mem_base = 0x8000; shadow.mem_size = sizeof poison;
        shadow.verify_mem = nw_jit_verify_trace::replay; shadow.verify_context = &trace;
        shadow.verify_xlate = nw_jit_verify_trace::replay_translation;
        for (unsigned w = 0; w < 4; ++w) shadow.vr[3][w] = 0x01020304u + w;
        if (store) nw_jit_helper_stvx(&shadow, 3, 4, 0);
        else nw_jit_helper_lvx(&shadow, 3, 4, 0);
        const bool valid = mutation == 7 || (!store && mutation == 0);
        CHECK(trace.complete() == valid);
        CHECK(shadow.fault == (mutation == 7 ? NW_JIT_FAULT_DSI : mutation && mutation != 0 ? NW_JIT_FAULT_VERIFY : 0));
        for (unsigned i = 0; i < sizeof poison; ++i) CHECK(poison[i] == 0x7f);
        if (mutation == 7) CHECK(shadow.fault_ea == 0x8000 && shadow.fault_width == 16 && shadow.fault_st == store);
        // Failed loads must leave the complete destination unchanged.
        if (!store && mutation) for (unsigned w = 0; w < 4; ++w) CHECK(shadow.vr[3][w] == 0x01020304u + w);
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
    CHECK(nw_jit_op_verify_safe(0x4c000064u)); // rfi
    CHECK(nw_jit_op_verify_safe((31u<<26)|(20u<<1))); // lwarx
    CHECK(nw_jit_op_verify_safe((31u<<26)|(150u<<1)|1u)); // stwcx.
    CHECK(nw_jit_op_verify_safe((4u<<26)|10u)); // private VMX FP
    for (unsigned xo : {533u, 597u, 661u, 725u}) CHECK(nw_jit_op_verify_safe((31u<<26)|(xo<<1)));
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
    if (!fgets(line, sizeof line, file) || (strcmp(line, "NW-PPC-VERIFY 1\n") && strcmp(line, "NW-PPC-VERIFY 2\n") && strcmp(line, "NW-PPC-VERIFY 3\n"))) { fclose(file); return 2; }
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
        if (sscanf(line, "exception %x %x %x %x", &input.srr0, &input.srr1, &expected.srr0, &expected.srr1) == 4) continue;
        unsigned gi, gr, gj;
        unsigned iv, ip, rv, rp, nv, np;
        if (sscanf(line, "reservation %u %x %u %x %u %x", &iv, &ip, &rv, &rp, &nv, &np) == 6) {
            if (iv > 1 || rv > 1 || nv > 1) { fclose(file); return 2; }
            input.reserve_valid = iv; input.reserve_ea = ip;
            expected.reserve_valid = rv; expected.reserve_ea = rp; continue;
        }
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
        if (sscanf(line, "system %x %x %u %u %u %llx", &pc, &ea, &width, &store, &fault, &a) == 6) {
            if (trace.count == trace.capacity || fault > NW_SYS_SMC || !nw_jit_op_system(ea)) { fclose(file); return 2; }
            nw_jit_system_result result = {uint32(a),uint32(a >> 32),fault};
            trace.record_system(pc,ea,width,store,result); continue;
        }
        if (sscanf(line, "translation %x %x %u %u %u %llx", &pc, &ea, &width, &store, &fault, &a) == 6) {
            if ((width != 4 && width != 16) || store > 1 || a > UINT32_MAX || trace.count == trace.capacity) { fclose(file); return 2; }
            trace.record_translation(pc, ea, width, store != 0, uint32(a), fault); continue;
        }
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
    input.verify_xlate = nw_jit_verify_trace::replay_translation;
    input.verify_system = nw_jit_verify_trace::replay_system;
    nw_jit_cpu_bind(&input); nw_jit_tail_begin(); fn(&input);
    CHECK(trace.complete());
    CHECK((input.fault == NW_JIT_FAULT_SMC ? input.pc + 4 : input.pc) == expected.pc);
    CHECK(input.cr == expected.cr); CHECK(input.xer == expected.xer);
    CHECK(input.fpscr == expected.fpscr); CHECK(input.lr == expected.lr);
    CHECK(input.ctr == expected.ctr); CHECK(input.dec == expected.dec);
    CHECK(input.msr == expected.msr); CHECK(input.vscr == expected.vscr);
    CHECK(input.fault == expected.fault);
    CHECK(input.reserve_valid == expected.reserve_valid); CHECK(input.reserve_ea == expected.reserve_ea);
    if (expected.fault == NW_JIT_FAULT_EXC) { CHECK(input.srr0 == expected.srr0); CHECK(input.srr1 == expected.srr1); }
    if (expected.fault != NW_JIT_FAULT_EXC && expected.fault) { CHECK(input.fault_ea == expected.fault_ea); CHECK(input.fault_width == expected.fault_width); CHECK(input.fault_st == expected.fault_st); }
    for (unsigned i = 0; i < 32; ++i) {
        CHECK(input.gpr[i] == expected.gpr[i]); CHECK(input.fpr[i] == expected.fpr[i]);
        for (unsigned w = 0; w < 4; ++w) CHECK(input.vr[i][w] == expected.vr[i][w]);
    }
    printf("PPC isolated replay: %u passed, %u failed (original bits=%x)\n", passed, failed, bits);
    return failed ? 1 : 0;
}

extern "C" int ppc_test_main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--vmx-policy")) {
        for (unsigned prim : {4u,31u})
        for (unsigned low = 0; low < 2048; ++low)
        for (unsigned field : {21u,16u,11u})
        for (unsigned value = 0; value < 32; ++value) {
            const uint32 op = (prim<<26) | low | (value<<field);
            printf("VMX %08x %d\n", op, nw_jit_op_supported(op));
        }
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--basic-special-p6")) { powerpc_cpu *cpu=new powerpc_cpu; int status=ppc_core_test_access::basic_special_p6(cpu); printf("P6 basic special tests: %u passed, %u failed\n",passed,failed); return status; }
    if (argc == 2 && !strcmp(argv[1], "--link-sweep")) { powerpc_cpu *cpu = new powerpc_cpu; int status = ppc_core_test_access::link_sweep(cpu); printf("Link sweep: %u passed, %u failed\n",passed,failed); return status; }
    if (argc == 2 && !strcmp(argv[1], "--ibtc-remap")) { powerpc_cpu *cpu = new powerpc_cpu; int status = ppc_core_test_access::ibtc_remap(cpu); printf("IBTC remap: %u passed, %u failed\n",passed,failed); return status; }
    if (argc == 2 && !strcmp(argv[1], "--loop-bench")) { powerpc_cpu *cpu = new powerpc_cpu; int status = ppc_core_test_access::loop_bench(cpu); printf("Loop bench: %u passed, %u failed\n",passed,failed); return status; }
    if (argc == 2 && !strcmp(argv[1], "--sub-store-sweep")) { powerpc_cpu *cpu = new powerpc_cpu; int status = ppc_core_test_access::sub_store_sweep(cpu); printf("Sub-word store sweep: %u passed, %u failed\n",passed,failed); return status; }
    if (argc == 2 && !strcmp(argv[1], "--fp-fast-sweep")) { powerpc_cpu *cpu = new powerpc_cpu; int status = ppc_core_test_access::fp_fast_sweep(cpu); printf("FP fast sweep: %u passed, %u failed\n",passed,failed); return status; }
    if (argc == 2 && !strcmp(argv[1], "--multiply-p6")) { powerpc_cpu *cpu=new powerpc_cpu; int status=ppc_core_test_access::multiply_p6(cpu); printf("P6 multiply tests: %u passed, %u failed\n",passed,failed); return status; }
    if (argc == 2 && !strcmp(argv[1], "--frsp-p6")) { powerpc_cpu *cpu = new powerpc_cpu; int status = ppc_core_test_access::frsp_p6(cpu); printf("P6 frsp tests: %u passed, %u failed\n",passed,failed); return status; }
    if (argc == 2 && !strcmp(argv[1], "--scalar-p6")) { powerpc_cpu *cpu = new powerpc_cpu; int status = ppc_core_test_access::scalar_p6(cpu); printf("P6 scalar tests: %u passed, %u failed\n",passed,failed); return status; }
    if (argc == 2 && !strcmp(argv[1], "--io-publication")) { powerpc_cpu *cpu = new powerpc_cpu; int status = ppc_core_test_access::io_publication(cpu); printf("Raw file-read tests: %u passed, %u failed\n",passed,failed); return status; }
    if (argc == 3 && !strcmp(argv[1], "--replay")) return replay_capture(argv[2]);
    return ppc_core_test_access::run();
}
