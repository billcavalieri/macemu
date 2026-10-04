/* Portable private AltiVec FP execution. No live CPU or device access. */
#ifndef NW_JIT_VMX_FP_H
#define NW_JIT_VMX_FP_H
#include "cpu/ppc/ppc-fp-environment.hpp"
#include <math.h>
#include <string.h>
#if defined(__clang__)
#pragma STDC FENV_ACCESS ON
#endif
static unsigned nw_vmx_fp_kind(uint32_t op)
{
    if ((op >> 26) != 4) return 0;
    if ((op & 63) == 46 || (op & 63) == 47) return op & 63;
    switch (op & 2047) {
    case 10: case 74: case 842: case 778: case 970: case 906:
    case 394: case 458: case 1034: case 1098: case 266: case 330:
    case 714: case 522: case 650: case 586:
    case 966: case 1990: case 198: case 1222: case 454: case 1478: case 710: case 1734:
        return op & 2047;
    }
    return 0;
}
static bool nw_vmx_fp_nan(uint32_t x) { return (x & 0x7fffffff) > 0x7f800000; }
static float nw_vmx_fp_value(uint32_t x, bool nj)
{
    if (nj && !(x & 0x7f800000)) x &= 0x80000000;
    float f; memcpy(&f, &x, 4); return f;
}
static uint32_t nw_vmx_fp_bits(float f) { uint32_t x; memcpy(&x, &f, 4); return x; }
static bool nw_vmx_fp(nw_jit_cpu *cpu, uint32_t op)
{
    const unsigned kind = nw_vmx_fp_kind(op);
    if (!kind) return false;
    const ppc_fp_environment env(0); // Vector RN is independent of scalar FPSCR.
    const unsigned vd = (op >> 21) & 31, va = (op >> 16) & 31, vb = (op >> 11) & 31, vc = (op >> 6) & 31;
    const unsigned base = kind & 1023;
    const bool nj = (cpu->vscr & 0x10000) && kind != 522;
    uint32_t out[4];
    for (unsigned i = 0; i < 4; ++i) {
        const uint32_t ax = cpu->vr[va][i], bx = cpu->vr[vb][i], cx = cpu->vr[vc][i];
        const float a = nw_vmx_fp_value(ax, nj), b = nw_vmx_fp_value(bx, nj), c = nw_vmx_fp_value(cx, nj);
        float value = 0;
        if (base == 966 || base == 198 || base == 454 || base == 710) {
            const bool unordered = nw_vmx_fp_nan(ax) || nw_vmx_fp_nan(bx);
            out[i] = base == 966 ? (unordered ? 0xc0000000u : (a > b ? 0x80000000u : 0) | (a < -b ? 0x40000000u : 0)) :
                unordered ? 0 : (base == 198 ? a == b : base == 454 ? a >= b : a > b) ? UINT32_MAX : 0;
            continue;
        }
        if (kind == 970 || kind == 906) {
            if (nw_vmx_fp_nan(bx)) { out[i] = 0; continue; }
            const double scaled = ldexp(double(b), int(va));
            const double low = kind == 970 ? -2147483648.0 : 0.0;
            const double high = kind == 970 ? 2147483647.0 : 4294967295.0;
            if (scaled < low) { out[i] = kind == 970 ? 0x80000000u : 0; cpu->vscr |= 1; }
            else if (scaled > high) { out[i] = kind == 970 ? 0x7fffffffu : UINT32_MAX; cpu->vscr |= 1; }
            else out[i] = uint32_t(int64_t(trunc(scaled)));
            continue;
        }
        const bool binary = kind == 10 || kind == 74 || kind == 1034 || kind == 1098 || kind == 46 || kind == 47;
        const bool fused = kind == 46 || kind == 47;
        const bool from_integer = kind == 842 || kind == 778;
        uint32_t nan = 0;
        if (!from_integer) {
            if (binary && nw_vmx_fp_nan(ax)) nan = ax;
            else if (nw_vmx_fp_nan(bx)) nan = bx;
            else if (fused && nw_vmx_fp_nan(cx)) nan = cx;
        }
        if (nan) { out[i] = nan | 0x00400000u; continue; }
        switch (kind) {
        case 10: value = a + b; break;
        case 74: value = a - b; break;
        case 46: value = fmaf(a, c, b); break;
        case 47: value = -fmaf(a, c, -b); break;
        case 842: value = float(ldexp(double(int64_t(bx) - (bx & 0x80000000u ? INT64_C(0x100000000) : 0)), -int(va))); break;
        case 778: value = float(ldexp(double(bx), -int(va))); break;
        case 1034: value = a == 0 && b == 0 ? nw_vmx_fp_value(ax & bx & 0x80000000u, false) : a >= b ? a : b; break;
        case 1098: value = a == 0 && b == 0 ? nw_vmx_fp_value((ax | bx) & 0x80000000u, false) : a <= b ? a : b; break;
        case 394: value = exp2f(b); break;
        case 458: value = log2f(b); break;
        case 266: value = 1.0f / b; break;
        case 330: value = float(1.0 / sqrt(double(b))); break;
        case 714: value = floorf(b); break;
        case 522: value = nearbyintf(b); break;
        case 650: value = ceilf(b); break;
        case 586: value = truncf(b); break;
        }
        uint32_t bits = nw_vmx_fp_bits(value);
        if (nw_vmx_fp_nan(bits)) bits = 0x7fc00000; // Invalid arithmetic result.
        if (nj && !(bits & 0x7f800000)) bits &= 0x80000000;
        out[i] = bits;
    }
    memcpy(cpu->vr[vd], out, sizeof out);
    if ((base == 966 || base == 198 || base == 454 || base == 710) && (kind & 1024)) {
        const bool all = out[0] == UINT32_MAX && out[1] == UINT32_MAX && out[2] == UINT32_MAX && out[3] == UINT32_MAX;
        const bool none = !(out[0] | out[1] | out[2] | out[3]);
        cpu->cr = (cpu->cr & ~0xf0u) | (none ? 0x20u : base != 966 && all ? 0x80u : 0);
    }
    return true;
}
#endif
