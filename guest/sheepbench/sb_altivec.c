/*
 *  sb_altivec.c - The AltiVec version of SB_K_VEC (compiled with -maltivec; only called when Gestalt reports
 *  AltiVec). y = y * 0.5 + x with vmaddfp: the multiply is exact, so the result equals the C kernel's.
 *
 *  Retro68's GCC has no altivec.h, so this uses the compiler's AltiVec types and built-ins directly. VRSAVE is set
 *  around the loop: Mac OS 9 saves only the vector registers VRSAVE marks when it switches context.
 *
 *  (C) 2026 Bill Cavalieri
 */

#include <Gestalt.h>
#include "suites.h"

#define gestaltPowerPCProcessorFeatures 'ppcf'
#define gestaltPowerPCHasVectorInstructions 4

Boolean HasAltivec(void)
{
    long features = 0;
    return Gestalt(gestaltPowerPCProcessorFeatures, &features) == noErr &&
           (features & (1L << gestaltPowerPCHasVectorInstructions)) != 0;
}

typedef __vector float vfloat;

/* The constant comes from the caller (four floats of 0.5, 16-byte aligned): lvx ignores the low four address bits,
 * and neither Retro68's PEF data section nor the stack is reliably 16-byte aligned, so a vector constant or a vector
 * on the stack would load from the wrong address. */
void AltivecVec(float *x, float *y, uint32_t n, uint32_t iters, const float *half4)
{
    vfloat half;
    uint32_t it, i, vrsave;
    __asm__ volatile("lvx %0,0,%1" : "=v"(half) : "r"(half4), "m"(*(const float(*)[4])half4));
    __asm__ volatile("mfspr %0,256" : "=r"(vrsave));
    __asm__ volatile("mtspr 256,%0" : : "r"(0xFFFFFFFFUL));
    for (it = 0; it < iters; it++)
        for (i = 0; i < n; i += 4) {
            vfloat vy = *(vfloat *)(y + i), vx = *(vfloat *)(x + i);
            *(vfloat *)(y + i) = __builtin_altivec_vmaddfp(vy, half, vx);
        }
    __asm__ volatile("mtspr 256,%0" : : "r"(vrsave));
}
