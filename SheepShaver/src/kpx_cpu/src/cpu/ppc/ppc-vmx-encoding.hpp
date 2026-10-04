/* Fixed-zero instruction fields in the original AltiVec ISA. Immediate
 * values and operand-register numbers are separate from these constraints.
 * Keep this table shared by New World decode, JIT policy and its generator. */
#ifndef PPC_VMX_ENCODING_HPP
#define PPC_VMX_ENCODING_HPP
#include <stdint.h>
struct ppc_vmx_zero_field { uint32_t xo, select, zero; };
static const ppc_vmx_zero_field ppc_vmx_zero_fields[] = {
    {526u, 0x000007ffu, 0x001f0000u},
    {654u, 0x000007ffu, 0x001f0000u},
    {590u, 0x000007ffu, 0x001f0000u},
    {718u, 0x000007ffu, 0x001f0000u},
    {846u, 0x000007ffu, 0x001f0000u},
    {974u, 0x000007ffu, 0x001f0000u},
    {394u, 0x000007ffu, 0x001f0000u},
    {458u, 0x000007ffu, 0x001f0000u},
    {266u, 0x000007ffu, 0x001f0000u},
    {330u, 0x000007ffu, 0x001f0000u},
    {714u, 0x000007ffu, 0x001f0000u},
    {522u, 0x000007ffu, 0x001f0000u},
    {650u, 0x000007ffu, 0x001f0000u},
    {586u, 0x000007ffu, 0x001f0000u},
    {780u, 0x000007ffu, 0x0000f800u},
    {844u, 0x000007ffu, 0x0000f800u},
    {908u, 0x000007ffu, 0x0000f800u},
    {1540u, 0x000007ffu, 0x001ff800u},
    {1604u, 0x000007ffu, 0x03ff0000u},
    {44u, 0x0000003fu, 0x00000400u},
};
// Opcode 31 AltiVec X forms. Invalid fixed-zero forms deliberately use
// program exceptions in New World; the base architecture also permits
// boundedly undefined results for invalid operand/reserved-bit forms.
static const ppc_vmx_zero_field ppc_vmx31_zero_fields[] = {
    {12u, 0x000007feu, 0x00000001u},  // lvsl
    {76u, 0x000007feu, 0x00000001u},  // lvsr
    {14u, 0x000007feu, 0x00000001u},  // lvebx
    {78u, 0x000007feu, 0x00000001u},  // lvehx
    {142u, 0x000007feu, 0x00000001u}, // lvewx
    {206u, 0x000007feu, 0x00000001u}, // lvx
    {718u, 0x000007feu, 0x00000001u}, // lvxl
    {270u, 0x000007feu, 0x00000001u}, // stvebx
    {334u, 0x000007feu, 0x00000001u}, // stvehx
    {398u, 0x000007feu, 0x00000001u}, // stvewx
    {462u, 0x000007feu, 0x00000001u}, // stvx
    {974u, 0x000007feu, 0x00000001u}, // stvxl
    {684u, 0x000007feu, 0x01800001u}, // dst/dstt
    {748u, 0x000007feu, 0x01800001u}, // dstst/dststt
    {1644u, 0x000007feu, 0x019ff801u},// dss/dssall
};
static inline bool ppc_vmx_reserved_zero(uint32_t op)
{
    if ((op >> 26) == 31) {
        for (const auto &field : ppc_vmx31_zero_fields)
            if ((op & field.select) == field.xo) return !(op & field.zero);
        return true;
    }
    if ((op >> 26) != 4) return true;
    for (const auto &field : ppc_vmx_zero_fields)
        if ((op & field.select) == field.xo) return !(op & field.zero);
    return true;
}
#endif
