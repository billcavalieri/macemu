/* Finite PPC multiplication, using an exact 106-bit integer product.
 * Callers own NaN/infinity/zero policy and architectural exception entry.
 * For single precision, both operands must be representable as singles.
 */
#ifndef NW_JIT_FP_MULTIPLY_H
#define NW_JIT_FP_MULTIPLY_H
#include <stdint.h>
struct nw_fp_product { uint64_t value; uint32_t causes, rounded, classification; };

// Finite single operands must be exactly representable as single values.
// Other inputs retain the older extension profile (the ISA leaves it undefined).
static inline bool nw_fp_single_operand(uint64_t raw)
{
    const uint64_t abs = raw & UINT64_C(0x7fffffffffffffff);
    const unsigned exponent = unsigned(abs >> 52);
    if (!abs) return true;
    if (exponent < 874 || exponent > 1150) return false;
    const unsigned discarded = 29 + (exponent < 897 ? 897 - exponent : 0);
    return !(abs & ((UINT64_C(1) << discarded) - 1));
}

static inline nw_fp_product nw_fp_multiply(uint64_t a, uint64_t c, bool single, uint32_t fpscr)
{
    // The exact product has at most 106 bits. Round that integer once,
    // keeping an unbounded exponent until the selected exception adjustment.
    const uint64_t sign = (a ^ c) & UINT64_C(0x8000000000000000);
    const unsigned ab = unsigned((a >> 52) & 2047u), cb = unsigned((c >> 52) & 2047u);
    const uint64_t as = (a & UINT64_C(0x000fffffffffffff)) | (ab ? UINT64_C(0x0010000000000000) : 0);
    const uint64_t cs = (c & UINT64_C(0x000fffffffffffff)) | (cb ? UINT64_C(0x0010000000000000) : 0);
    const unsigned precision = single ? 24 : 53;
    const int emin = single ? -126 : -1022, emax = single ? 127 : 1023;
    const int base = (ab ? int(ab) - 1075 : -1074) + (cb ? int(cb) - 1075 : -1074);
    typedef unsigned __int128 wide;
    const wide product = wide(as) * cs;
    const uint64_t high = uint64_t(product >> 64);
    const unsigned top = high ? 127u - unsigned(__builtin_clzll(high)) : 63u - unsigned(__builtin_clzll(uint64_t(product)));
    const int exponent = base + int(top);
    const bool tiny = exponent < emin, adjusted_underflow = tiny && (fpscr & 0x20u);
    const int grid = (tiny && !adjusted_underflow ? emin : exponent) - int(precision - 1);
    const int shift = grid - base;
    uint64_t units;
    wide remainder;
    if (shift <= 0) { units = uint64_t(product << unsigned(-shift)); remainder = 0; }
    else if (shift >= 128) { units = 0; remainder = product; }
    else { units = uint64_t(product >> unsigned(shift)); remainder = product & ((wide(1) << unsigned(shift)) - 1); }
    const unsigned rn = fpscr & 3u;
    const bool inexact = remainder != 0;
    const bool increment = rn == 0 ? shift > 0 && shift <= 128 &&
        (remainder > (wide(1) << unsigned(shift - 1)) ||
         (remainder == (wide(1) << unsigned(shift - 1)) && (units & 1))) :
        rn == 2 ? inexact && !sign : rn == 3 ? inexact && sign : false;
    units += increment;
    uint32_t rounded = (inexact ? 0x20000u : 0) | (increment ? 0x40000u : 0);
    uint32_t causes = inexact ? 0x02000000u : 0;
    if (tiny && (adjusted_underflow || inexact)) causes |= 0x08000000u;
    const int result_top = units ? 63 - __builtin_clzll(units) : 0;
    const bool overflow = units && grid + result_top > emax;
    if (overflow) causes |= 0x10000000u;
    uint64_t result;
    uint32_t classification;
    if (overflow && !(fpscr & 0x40u)) {
        const bool infinity = rn == 0 || (rn == 2 && !sign) || (rn == 3 && sign);
        result = sign | (infinity ? UINT64_C(0x7ff0000000000000) : single ?
            UINT64_C(0x47efffffe0000000) : UINT64_C(0x7fefffffffffffff));
        rounded = 0x20000u; // Undefined disabled-overflow FR profile: zero.
        causes |= 0x02000000u;
        classification = infinity ? sign ? 9 : 5 : sign ? 8 : 4;
    } else if (!units) { result = sign; classification = sign ? 18 : 2; }
    else {
        const int adjustment = adjusted_underflow ? single ? 192 : 1536 : overflow ? single ? -192 : -1536 : 0;
        const int delivered_exponent = grid + result_top + adjustment;
        if (delivered_exponent >= -1022) {
            result = sign | (uint64_t(delivered_exponent + 1023) << 52) |
                ((result_top > 52 ? units >> (result_top - 52) : units << (52 - result_top)) & UINT64_C(0x000fffffffffffff));
        } else result = sign | (units << unsigned(grid + 1074));
        classification = tiny && !adjusted_underflow && grid + result_top < emin ? sign ? 24 : 20 : sign ? 8 : 4;
    }
    return {result, causes, rounded, classification};
}

#endif
