/* Standalone bounds/undefined-behavior and state-isolation stress for the
 * private integer and FP VMX kernels. ISA expected-value tests live in the real-CPU
 * harness; this test does not use implementation agreement as an oracle. */
#include <cstdio>
#include <cstring>
#include "nw_jit.h"
#include "nw_jit_vmx_integer.h"
#include "nw_jit_vmx_fp.h"
int main() {
    const unsigned aliases[][4] = {{0,0,0,0},{0,0,0,1},{0,0,1,0},{0,0,1,1},{0,0,1,2},
        {0,1,0,0},{0,1,0,1},{0,1,0,2},{0,1,1,0},{0,1,1,1},{0,1,1,2},{0,1,2,0},{0,1,2,1},{0,1,2,2},{0,1,2,3}};
    unsigned ran = 0; uint32_t seed = 0x31415926;
    for (unsigned low = 0; low < 2048; ++low) {
        const uint32_t base = (4u<<26) | low;
        if (!nw_vmx_integer_descriptor(base)) continue;
        const unsigned va = base & 63;
        const bool four = va == 32 || va == 33 || (va >= 36 && va <= 39);
        for (const auto &layout : aliases) for (unsigned pattern = 0; pattern < 32; ++pattern) {
            uint32_t op = base | (layout[0]<<21) | (layout[1]<<16) | (layout[2]<<11);
            if (four) op = (op & ~(31u<<6)) | (layout[3]<<6);
            nw_jit_cpu state = {}; state.pc = 0x1000; state.cr = 0x12345678; state.vscr = 0x10000 | (pattern & 1);
            for (unsigned r = 0; r < 32; ++r) {
                state.gpr[r] = 0x12340000+r; state.fpr[r] = 0x3ff0000000000000ull+r;
                for (unsigned w = 0; w < 4; ++w) {
                    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
                    state.vr[r][w] = pattern == 0 ? 0 : pattern == 1 ? UINT32_MAX : pattern == 2 ? 0x80000000 : pattern == 3 ? 0x7fffffff : seed;
                }
            }
            const nw_jit_cpu before = state;
            if (!nw_vmx_integer(&state, op) || state.pc != before.pc || (state.vscr & ~1u) != (before.vscr & ~1u) ||
                memcmp(state.gpr,before.gpr,sizeof state.gpr) || memcmp(state.fpr,before.fpr,sizeof state.fpr)) return 1;
            for (unsigned r = 0; r < 32; ++r) if (r != layout[0] && memcmp(state.vr[r],before.vr[r],sizeof state.vr[r])) return 2;
            ++ran;
        }
    }
    const unsigned integer_ran = ran;
    const uint32_t values[] = {0,0x80000000,1,0x80000001,0x007fffff,0x807fffff,0x00800000,0x80800000,
        0x7f800000,0xff800000,0x7fc12345,0xff812345,0x7f7fffff,0xff7fffff,0x4f000000,0xcf000000,
        0x3f000000,0xbf000000,0x3f800001,0x3f7ffffe};
    const int modes[] = {FE_TONEAREST,FE_TOWARDZERO,FE_UPWARD,FE_DOWNWARD};
    unsigned fp_ran = 0;
    for (unsigned low = 0; low < 2048; ++low) {
        const uint32_t base = (4u<<26) | low;
        const unsigned kind = nw_vmx_fp_kind(base);
        if (!kind) continue;
        for (const auto &layout : aliases) for (unsigned pattern = 0; pattern < 32; ++pattern)
        for (unsigned host = 0; host < 4; ++host) {
            uint32_t op = base | (layout[0]<<21) | (layout[1]<<16) | (layout[2]<<11);
            if (kind == 46 || kind == 47) op = (op & ~(31u<<6)) | (layout[3]<<6);
            if (kind == 842 || kind == 778 || kind == 970 || kind == 906) op = (op & ~(31u<<16)) | (pattern<<16);
            nw_jit_cpu state = {}; state.pc = 0x1000; state.cr = 0x12345678; state.fpscr = 0x12340000 | host;
            state.vscr = ((pattern & 1)<<16) | ((pattern>>1)&1);
            for (unsigned r = 0; r < 32; ++r) {
                state.gpr[r] = 0x12340000+r; state.fpr[r] = 0x3ff0000000000000ull+r;
                for (unsigned w = 0; w < 4; ++w) state.vr[r][w] = values[(pattern + r + w) % 20];
            }
            const nw_jit_cpu before = state;
            fesetround(modes[host]); feclearexcept(FE_ALL_EXCEPT); feraiseexcept(FE_DIVBYZERO);
            const int flags = fetestexcept(FE_ALL_EXCEPT);
            if (!nw_vmx_fp(&state, op) || state.pc != before.pc || state.fpscr != before.fpscr ||
                (state.vscr & ~1u) != (before.vscr & ~1u) || (before.vscr & 1u) > (state.vscr & 1u) ||
                (state.cr & ~0xf0u) != (before.cr & ~0xf0u) || fegetround() != modes[host] || fetestexcept(FE_ALL_EXCEPT) != flags ||
                memcmp(state.gpr,before.gpr,sizeof state.gpr) || memcmp(state.fpr,before.fpr,sizeof state.fpr)) return 3;
            for (unsigned r = 0; r < 32; ++r) if (r != layout[0] && memcmp(state.vr[r],before.vr[r],sizeof state.vr[r])) return 4;
            ++fp_ran;
        }
    }
    fesetround(FE_TONEAREST); feclearexcept(FE_ALL_EXCEPT);
    printf("Private FP VMX sanitizers: %u executions, preserved state/environment, zero failures\n", fp_ran);
    printf("Private integer VMX sanitizers: %u executions, preserved state, zero failures\n", integer_ran);
}
