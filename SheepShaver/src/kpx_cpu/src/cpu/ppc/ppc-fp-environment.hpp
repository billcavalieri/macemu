/* Guest scalar FP control belongs to an instruction, never to its host thread.
 * Keep callbacks, nested CPUs and integer-only execution in the caller's environment.
 * Callers must compile FP expressions with dynamic rounding enabled. */
#ifndef PPC_FP_ENVIRONMENT_HPP
#define PPC_FP_ENVIRONMENT_HPP
#include <stdint.h>
#include <fenv.h>

static inline int ppc_native_rounding(unsigned rn)
{
    static const int modes[] = { FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD };
    return modes[rn & 3];
}

class ppc_fp_environment {
#if defined(__aarch64__)
    uint64_t control_, status_;
#else
    fenv_t saved_;
#endif
public:
    explicit ppc_fp_environment(unsigned fpscr) {
#if defined(__aarch64__)
        __asm__ volatile("mrs %0, fpcr\n\tmrs %1, fpsr" : "=r"(control_), "=r"(status_) :: "memory");
        // ARM RN order is nearest, +inf, -inf, zero. Disable host traps,
        // flush-to-zero and default-NaN; PPC arithmetic must retain denormals.
        const uint64_t rn[] = { 0, 3, 1, 2 };
        const uint64_t guest = (control_ & ~UINT64_C(0x3c89f00)) | (rn[fpscr & 3] << 22);
        __asm__ volatile("msr fpcr, %0\n\tmsr fpsr, xzr" :: "r"(guest) : "memory");
#else
        feholdexcept(&saved_);
        fesetround(ppc_native_rounding(fpscr));
#endif
    }
    ~ppc_fp_environment() {
#if defined(__aarch64__)
        __asm__ volatile("msr fpcr, %0\n\tmsr fpsr, %1" :: "r"(control_), "r"(status_) : "memory");
#else
        fesetenv(&saved_);
#endif
    }
    ppc_fp_environment(const ppc_fp_environment &) = delete;
    ppc_fp_environment &operator=(const ppc_fp_environment &) = delete;
};

// FEX and VX are derived, even when arithmetic exception tracking is disabled.
static inline uint32_t ppc_fpscr_summaries(uint32_t f)
{
    f &= ~UINT32_C(0x60000000);
    if (f & UINT32_C(0x01f80700)) f |= UINT32_C(0x20000000);
    if (((f >> 22) & f & UINT32_C(0xf8)) != 0) f |= UINT32_C(0x40000000);
    return f;
}
#endif
