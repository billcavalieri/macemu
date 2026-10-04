/* Private integer VMX execution. Architectural lanes are numbered from the
 * most significant byte; stage the whole destination to preserve all aliases.
 * No host CPU, MMU, memory, device, or floating-point environment is consulted. */
#ifndef NW_JIT_VMX_INTEGER_H
#define NW_JIT_VMX_INTEGER_H

enum nw_vmx_integer_kind {
    VI_ADD, VI_SUB, VI_AVG, VI_MAX, VI_MIN, VI_EQ, VI_GT,
    VI_CARRY, VI_NOBORROW, VI_ROTATE, VI_SHL, VI_SHR, VI_SAR, VI_NOR,
    VI_MUL_EVEN, VI_MUL_ODD, VI_MERGE_HIGH, VI_MERGE_LOW,
    VI_PACK, VI_UNPACK_HIGH, VI_UNPACK_LOW, VI_PIXEL_PACK,
    VI_PIXEL_HIGH, VI_PIXEL_LOW, VI_SPLAT, VI_SUM2, VI_SUM4,
    VI_MADD_HIGH, VI_MADD_ROUND, VI_MSUM, VI_MSUM_MIXED
};
enum { VI_SIGNED = 1, VI_SAT = 2, VI_SIGNED_INPUT = 4 };
struct nw_vmx_integer_desc { unsigned kind, width, flags; };
static const nw_vmx_integer_desc *nw_vmx_integer_descriptor(uint32_t op)
{
    if ((op >> 26) != 4) return NULL;
    switch (op & 63u) {
    case 32: { static const nw_vmx_integer_desc d = {VI_MADD_HIGH, 2, VI_SIGNED | VI_SAT}; return &d; } // vmhaddshs
    case 33: { static const nw_vmx_integer_desc d = {VI_MADD_ROUND, 2, VI_SIGNED | VI_SAT}; return &d; } // vmhraddshs
    case 36: { static const nw_vmx_integer_desc d = {VI_MSUM, 1, 0}; return &d; } // vmsumubm
    case 37: { static const nw_vmx_integer_desc d = {VI_MSUM_MIXED, 1, 0}; return &d; } // vmsummbm
    case 38: { static const nw_vmx_integer_desc d = {VI_MSUM, 2, 0}; return &d; } // vmsumuhm
    case 39: { static const nw_vmx_integer_desc d = {VI_MSUM, 2, VI_SAT}; return &d; } // vmsumuhs
    }
    switch (op & 2047u) {
    case 64: { static const nw_vmx_integer_desc d = {VI_ADD, 2, 0}; return &d; } // vadduhm
    case 1088: { static const nw_vmx_integer_desc d = {VI_SUB, 2, 0}; return &d; } // vsubuhm
    case 1152: { static const nw_vmx_integer_desc d = {VI_SUB, 4, 0}; return &d; } // vsubuwm
    case 512: { static const nw_vmx_integer_desc d = {VI_ADD, 1, VI_SAT}; return &d; } // vaddubs
    case 576: { static const nw_vmx_integer_desc d = {VI_ADD, 2, VI_SAT}; return &d; } // vadduhs
    case 640: { static const nw_vmx_integer_desc d = {VI_ADD, 4, VI_SAT}; return &d; } // vadduws
    case 1536: { static const nw_vmx_integer_desc d = {VI_SUB, 1, VI_SAT}; return &d; } // vsububs
    case 1600: { static const nw_vmx_integer_desc d = {VI_SUB, 2, VI_SAT}; return &d; } // vsubuhs
    case 1664: { static const nw_vmx_integer_desc d = {VI_SUB, 4, VI_SAT}; return &d; } // vsubuws
    case 768: { static const nw_vmx_integer_desc d = {VI_ADD, 1, VI_SIGNED | VI_SAT}; return &d; } // vaddsbs
    case 832: { static const nw_vmx_integer_desc d = {VI_ADD, 2, VI_SIGNED | VI_SAT}; return &d; } // vaddshs
    case 896: { static const nw_vmx_integer_desc d = {VI_ADD, 4, VI_SIGNED | VI_SAT}; return &d; } // vaddsws
    case 1792: { static const nw_vmx_integer_desc d = {VI_SUB, 1, VI_SIGNED | VI_SAT}; return &d; } // vsubsbs
    case 1920: { static const nw_vmx_integer_desc d = {VI_SUB, 4, VI_SIGNED | VI_SAT}; return &d; } // vsubsws
    case 1026: { static const nw_vmx_integer_desc d = {VI_AVG, 1, 0}; return &d; } // vavgub
    case 1090: { static const nw_vmx_integer_desc d = {VI_AVG, 2, 0}; return &d; } // vavguh
    case 1154: { static const nw_vmx_integer_desc d = {VI_AVG, 4, 0}; return &d; } // vavguw
    case 2: { static const nw_vmx_integer_desc d = {VI_MAX, 1, 0}; return &d; } // vmaxub
    case 66: { static const nw_vmx_integer_desc d = {VI_MAX, 2, 0}; return &d; } // vmaxuh
    case 130: { static const nw_vmx_integer_desc d = {VI_MAX, 4, 0}; return &d; } // vmaxuw
    case 514: { static const nw_vmx_integer_desc d = {VI_MIN, 1, 0}; return &d; } // vminub
    case 578: { static const nw_vmx_integer_desc d = {VI_MIN, 2, 0}; return &d; } // vminuh
    case 642: { static const nw_vmx_integer_desc d = {VI_MIN, 4, 0}; return &d; } // vminuw
    case 1282: { static const nw_vmx_integer_desc d = {VI_AVG, 1, VI_SIGNED}; return &d; } // vavgsb
    case 1346: { static const nw_vmx_integer_desc d = {VI_AVG, 2, VI_SIGNED}; return &d; } // vavgsh
    case 1410: { static const nw_vmx_integer_desc d = {VI_AVG, 4, VI_SIGNED}; return &d; } // vavgsw
    case 258: { static const nw_vmx_integer_desc d = {VI_MAX, 1, VI_SIGNED}; return &d; } // vmaxsb
    case 322: { static const nw_vmx_integer_desc d = {VI_MAX, 2, VI_SIGNED}; return &d; } // vmaxsh
    case 386: { static const nw_vmx_integer_desc d = {VI_MAX, 4, VI_SIGNED}; return &d; } // vmaxsw
    case 834: { static const nw_vmx_integer_desc d = {VI_MIN, 2, VI_SIGNED}; return &d; } // vminsh
    case 898: { static const nw_vmx_integer_desc d = {VI_MIN, 4, VI_SIGNED}; return &d; } // vminsw
    case 70: case 1094: { static const nw_vmx_integer_desc d = {VI_EQ, 2, 0}; return &d; } // vcmpequh
    case 518: case 1542: { static const nw_vmx_integer_desc d = {VI_GT, 1, 0}; return &d; } // vcmpgtub
    case 582: case 1606: { static const nw_vmx_integer_desc d = {VI_GT, 2, 0}; return &d; } // vcmpgtuh
    case 646: case 1670: { static const nw_vmx_integer_desc d = {VI_GT, 4, 0}; return &d; } // vcmpgtuw
    case 774: case 1798: { static const nw_vmx_integer_desc d = {VI_GT, 1, VI_SIGNED}; return &d; } // vcmpgtsb
    case 838: case 1862: { static const nw_vmx_integer_desc d = {VI_GT, 2, VI_SIGNED}; return &d; } // vcmpgtsh
    case 902: case 1926: { static const nw_vmx_integer_desc d = {VI_GT, 4, VI_SIGNED}; return &d; } // vcmpgtsw
    case 384: { static const nw_vmx_integer_desc d = {VI_CARRY, 4, 0}; return &d; } // vaddcuw
    case 1408: { static const nw_vmx_integer_desc d = {VI_NOBORROW, 4, 0}; return &d; } // vsubcuw
    case 1284: { static const nw_vmx_integer_desc d = {VI_NOR, 4, 0}; return &d; } // vnor
    case 4: { static const nw_vmx_integer_desc d = {VI_ROTATE, 1, 0}; return &d; } // vrlb
    case 68: { static const nw_vmx_integer_desc d = {VI_ROTATE, 2, 0}; return &d; } // vrlh
    case 132: { static const nw_vmx_integer_desc d = {VI_ROTATE, 4, 0}; return &d; } // vrlw
    case 388: { static const nw_vmx_integer_desc d = {VI_SHL, 4, 0}; return &d; } // vslw
    case 580: { static const nw_vmx_integer_desc d = {VI_SHR, 2, 0}; return &d; } // vsrh
    case 772: { static const nw_vmx_integer_desc d = {VI_SAR, 1, VI_SIGNED}; return &d; } // vsrab
    case 836: { static const nw_vmx_integer_desc d = {VI_SAR, 2, VI_SIGNED}; return &d; } // vsrah
    case 520: { static const nw_vmx_integer_desc d = {VI_MUL_EVEN, 1, 0}; return &d; } // vmuleub
    case 776: { static const nw_vmx_integer_desc d = {VI_MUL_EVEN, 1, VI_SIGNED}; return &d; } // vmulesb
    case 584: { static const nw_vmx_integer_desc d = {VI_MUL_EVEN, 2, 0}; return &d; } // vmuleuh
    case 840: { static const nw_vmx_integer_desc d = {VI_MUL_EVEN, 2, VI_SIGNED}; return &d; } // vmulesh
    case 8: { static const nw_vmx_integer_desc d = {VI_MUL_ODD, 1, 0}; return &d; } // vmuloub
    case 264: { static const nw_vmx_integer_desc d = {VI_MUL_ODD, 1, VI_SIGNED}; return &d; } // vmulosb
    case 72: { static const nw_vmx_integer_desc d = {VI_MUL_ODD, 2, 0}; return &d; } // vmulouh
    case 328: { static const nw_vmx_integer_desc d = {VI_MUL_ODD, 2, VI_SIGNED}; return &d; } // vmulosh
    case 76: { static const nw_vmx_integer_desc d = {VI_MERGE_HIGH, 2, 0}; return &d; } // vmrghh
    case 332: { static const nw_vmx_integer_desc d = {VI_MERGE_LOW, 2, 0}; return &d; } // vmrglh
    case 14: { static const nw_vmx_integer_desc d = {VI_PACK, 2, 0}; return &d; } // vpkuhum
    case 78: { static const nw_vmx_integer_desc d = {VI_PACK, 4, 0}; return &d; } // vpkuwum
    case 142: { static const nw_vmx_integer_desc d = {VI_PACK, 2, VI_SAT}; return &d; } // vpkuhus
    case 206: { static const nw_vmx_integer_desc d = {VI_PACK, 4, VI_SAT}; return &d; } // vpkuwus
    case 398: { static const nw_vmx_integer_desc d = {VI_PACK, 2, VI_SIGNED | VI_SAT}; return &d; } // vpkshss
    case 270: { static const nw_vmx_integer_desc d = {VI_PACK, 2, VI_SIGNED_INPUT | VI_SAT}; return &d; } // vpkshus
    case 334: { static const nw_vmx_integer_desc d = {VI_PACK, 4, VI_SIGNED_INPUT | VI_SAT}; return &d; } // vpkswus
    case 526: { static const nw_vmx_integer_desc d = {VI_UNPACK_HIGH, 1, VI_SIGNED}; return &d; } // vupkhsb
    case 654: { static const nw_vmx_integer_desc d = {VI_UNPACK_LOW, 1, VI_SIGNED}; return &d; } // vupklsb
    case 590: { static const nw_vmx_integer_desc d = {VI_UNPACK_HIGH, 2, VI_SIGNED}; return &d; } // vupkhsh
    case 718: { static const nw_vmx_integer_desc d = {VI_UNPACK_LOW, 2, VI_SIGNED}; return &d; } // vupklsh
    case 782: { static const nw_vmx_integer_desc d = {VI_PIXEL_PACK, 4, 0}; return &d; } // vpkpx
    case 846: { static const nw_vmx_integer_desc d = {VI_PIXEL_HIGH, 2, 0}; return &d; } // vupkhpx
    case 974: { static const nw_vmx_integer_desc d = {VI_PIXEL_LOW, 2, 0}; return &d; } // vupklpx
    case 588: { static const nw_vmx_integer_desc d = {VI_SPLAT, 2, 0}; return &d; } // vsplth
    case 1672: { static const nw_vmx_integer_desc d = {VI_SUM2, 4, VI_SIGNED | VI_SAT}; return &d; } // vsum2sws
    case 1800: { static const nw_vmx_integer_desc d = {VI_SUM4, 1, VI_SIGNED | VI_SAT}; return &d; } // vsum4sbs
    case 1608: { static const nw_vmx_integer_desc d = {VI_SUM4, 2, VI_SIGNED | VI_SAT}; return &d; } // vsum4shs
    case 1544: { static const nw_vmx_integer_desc d = {VI_SUM4, 1, VI_SAT}; return &d; } // vsum4ubs
    }
    return NULL;
}
static uint32_t nw_vmx_lane(const uint32_t *v, unsigned width, unsigned lane)
{
    const unsigned bits = width * 8, per_word = 4 / width;
    const uint32_t mask = UINT32_MAX >> (32 - bits);
    return (v[lane / per_word] >> (32 - bits * (1 + lane % per_word))) & mask;
}
static int64_t nw_vmx_signed(uint32_t value, unsigned width)
{
    const unsigned bits = width * 8;
    return (value & (UINT32_C(1) << (bits - 1))) ? int64_t(value) - (INT64_C(1) << bits) : value;
}
static void nw_vmx_put(uint32_t *v, unsigned width, unsigned lane, int64_t value)
{
    const unsigned bits = width * 8, per_word = 4 / width;
    const uint32_t mask = UINT32_MAX >> (32 - bits);
    v[lane / per_word] |= (uint32_t(value) & mask) << (32 - bits * (1 + lane % per_word));
}
static int64_t nw_vmx_saturate(nw_jit_cpu *cpu, int64_t value, unsigned width, bool sign)
{
    const unsigned bits = width * 8;
    const int64_t min = sign ? -(INT64_C(1) << (bits - 1)) : 0;
    const int64_t max = (INT64_C(1) << (bits - (sign ? 1 : 0))) - 1;
    if (value < min) { cpu->vscr |= 1; return min; }
    if (value > max) { cpu->vscr |= 1; return max; }
    return value;
}
static bool nw_vmx_integer(nw_jit_cpu *cpu, uint32_t op)
{
    const nw_vmx_integer_desc *d = nw_vmx_integer_descriptor(op);
    if (!d) return false;
    const unsigned vd = (op >> 21) & 31, va = (op >> 16) & 31, vb = (op >> 11) & 31, vc = (op >> 6) & 31;
    const unsigned width = d->width, count = 16 / width, bits = width * 8;
    const bool sign = d->flags & VI_SIGNED;
    const uint32_t *a = cpu->vr[va], *b = cpu->vr[vb], *c = cpu->vr[vc];
    uint32_t out[4] = {};
    for (unsigned i = 0; i < count; ++i) {
        const uint32_t ua = nw_vmx_lane(a, width, i), ub = nw_vmx_lane(b, width, i);
        const int64_t sa = sign ? nw_vmx_signed(ua, width) : ua;
        const int64_t sb = sign ? nw_vmx_signed(ub, width) : ub;
        int64_t value = 0;
        unsigned dest_width = width, dest_lane = i;
        switch (d->kind) {
        case VI_ADD: value = sa + sb; break;
        case VI_SUB: value = sa - sb; break;
        case VI_AVG: value = (sa + sb + 1) >> 1; break;
        case VI_MAX: value = sa > sb ? sa : sb; break;
        case VI_MIN: value = sa < sb ? sa : sb; break;
        case VI_EQ: value = ua == ub ? UINT32_MAX : 0; break;
        case VI_GT: value = sa > sb ? UINT32_MAX : 0; break;
        case VI_CARRY: value = (uint64_t(ua) + ub) >> 32; break;
        case VI_NOBORROW: value = ua >= ub; break;
        case VI_NOR: value = ~(ua | ub); break;
        case VI_ROTATE: {
            const unsigned sh = ub & (bits - 1);
            value = sh ? (ua << sh) | (ua >> (bits - sh)) : ua; break;
        }
        case VI_SHL: value = uint64_t(ua) << (ub & (bits - 1)); break;
        case VI_SHR: value = ua >> (ub & (bits - 1)); break;
        case VI_SAR: value = sa >> (ub & (bits - 1)); break;
        case VI_MUL_EVEN: case VI_MUL_ODD: {
            if (i >= count / 2) continue;
            const unsigned lane = 2 * i + (d->kind == VI_MUL_ODD);
            const uint32_t x = nw_vmx_lane(a, width, lane), y = nw_vmx_lane(b, width, lane);
            value = (sign ? nw_vmx_signed(x, width) : x) * (sign ? nw_vmx_signed(y, width) : y);
            dest_width = 2 * width; break;
        }
        case VI_MERGE_HIGH: case VI_MERGE_LOW: {
            const unsigned lane = i / 2 + (d->kind == VI_MERGE_LOW ? count / 2 : 0);
            value = nw_vmx_lane(i & 1 ? b : a, width, lane); break;
        }
        case VI_PACK: {
            // Two input vectors narrow into one output, with signed-source
            // unsigned-destination packs distinguished from unsigned packs.
            dest_width = width / 2;
            const uint32_t x = nw_vmx_lane(a, width, i);
            const uint32_t y = nw_vmx_lane(b, width, i);
            const bool signed_input = sign || (d->flags & VI_SIGNED_INPUT);
            int64_t vx = signed_input ? nw_vmx_signed(x, width) : x;
            int64_t vy = signed_input ? nw_vmx_signed(y, width) : y;
            if (d->flags & VI_SAT) {
                vx = nw_vmx_saturate(cpu, vx, dest_width, sign);
                vy = nw_vmx_saturate(cpu, vy, dest_width, sign);
            }
            nw_vmx_put(out, dest_width, i, vx);
            nw_vmx_put(out, dest_width, i + count, vy);
            continue;
        }
        case VI_UNPACK_HIGH: case VI_UNPACK_LOW: {
            if (i >= count / 2) continue;
            const unsigned lane = i + (d->kind == VI_UNPACK_LOW ? count / 2 : 0);
            value = nw_vmx_signed(nw_vmx_lane(b, width, lane), width);
            dest_width = 2 * width; break;
        }
        case VI_PIXEL_PACK: {
            const uint32_t x = a[i], y = b[i];
            nw_vmx_put(out, 2, i, ((x >> 9) & 0xfc00) | ((x >> 6) & 0x3e0) | ((x >> 3) & 0x1f));
            nw_vmx_put(out, 2, i + 4, ((y >> 9) & 0xfc00) | ((y >> 6) & 0x3e0) | ((y >> 3) & 0x1f));
            continue;
        }
        case VI_PIXEL_HIGH: case VI_PIXEL_LOW: {
            if (i >= 4) continue;
            const uint32_t h = nw_vmx_lane(b, 2, i + (d->kind == VI_PIXEL_LOW ? 4 : 0));
            value = (h & 0x8000 ? 0xff000000 : 0) | ((h & 0x7c00) << 6) | ((h & 0x3e0) << 3) | (h & 0x1f);
            dest_width = 4; break;
        }
        case VI_SPLAT: value = nw_vmx_lane(b, width, va & (count - 1)); break;
        case VI_SUM2: {
            if (i & 1) continue;
            value = nw_vmx_signed(b[i + 1], 4) + nw_vmx_signed(a[i], 4) + nw_vmx_signed(a[i + 1], 4);
            dest_lane = i + 1; break;
        }
        case VI_SUM4: {
            if (i >= 4) continue;
            value = sign ? nw_vmx_signed(b[i], 4) : b[i];
            for (unsigned j = 0; j < 4 / width; ++j) {
                const uint32_t x = nw_vmx_lane(a, width, i * (4 / width) + j);
                value += sign ? nw_vmx_signed(x, width) : x;
            }
            dest_width = 4; break;
        }
        case VI_MADD_HIGH: case VI_MADD_ROUND: {
            const int64_t product = sa * sb + (d->kind == VI_MADD_ROUND ? 0x4000 : 0);
            value = (product >> 15) + nw_vmx_signed(nw_vmx_lane(c, 2, i), 2); break;
        }
        case VI_MSUM: case VI_MSUM_MIXED: {
            if (i >= 4) continue;
            value = c[i];
            for (unsigned j = 0; j < 4 / width; ++j) {
                const unsigned lane = i * (4 / width) + j;
                const uint32_t x = nw_vmx_lane(a, width, lane), y = nw_vmx_lane(b, width, lane);
                value += (d->kind == VI_MSUM_MIXED ? nw_vmx_signed(x, width) : int64_t(x)) * y;
            }
            dest_width = 4; break;
        }
        }
        if (d->flags & VI_SAT) value = nw_vmx_saturate(cpu, value, dest_width, sign);
        nw_vmx_put(out, dest_width, dest_lane, value);
    }
    memcpy(cpu->vr[vd], out, sizeof out);
    if ((d->kind == VI_EQ || d->kind == VI_GT) && (op & 1024)) {
        const bool all = out[0] == UINT32_MAX && out[1] == UINT32_MAX && out[2] == UINT32_MAX && out[3] == UINT32_MAX;
        const bool none = !(out[0] | out[1] | out[2] | out[3]);
        cpu->cr = (cpu->cr & ~0xf0u) | (all ? 0x80u : none ? 0x20u : 0);
    }
    return true;
}
#endif
