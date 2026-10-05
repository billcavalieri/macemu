// Sanitize the production integer kernel against offline rational literals.
#include <cstdio>
#include <cstdint>
#include <cfenv>
#include <initializer_list>
#include <cstring>
#include "nw_jit_fp_multiply.h"
struct product_case { unsigned precision; uint64_t a,b; bool tiny; uint64_t result[4]; unsigned flags[4]; uint64_t adjusted[4]; unsigned adjusted_flags[4]; };
static const product_case inputs[] = {
#include "ppc_multiply_literals.inc"
};
int main()
{
    unsigned cases=0;
    for (const auto &test: inputs) for (unsigned negative=0;negative<2;++negative)
    for (unsigned rn=0;rn<4;++rn) for (unsigned enables: {0u,8u,0x20u,0x40u,0x80u,0xe8u}) {
        const uint64_t sign=uint64_t(negative)<<63;
        const unsigned rounding=negative && rn>=2 ? rn^1u : rn;
        const bool adjusted=(test.tiny && (enables&0x20u)) || ((test.flags[rounding]&8u) && (enables&0x40u));
        const unsigned status=adjusted ? test.adjusted_flags[rounding] : test.flags[rounding];
        const uint64_t expected=(adjusted ? test.adjusted[rounding] : test.result[rounding])|sign;
        const uint32_t causes=(status&1 ? 0x02000000u : 0)|(status&4 ? 0x08000000u : 0)|(status&8 ? 0x10000000u : 0);
        // Special zero behavior belongs to the caller, not this kernel.
        if (!test.a || !test.b) continue;
        if (test.precision==59 && (!nw_fp_single_operand(test.a) || !nw_fp_single_operand(test.b))) return 2;
        fenv_t before={},after={}; fegetenv(&before);
        const auto result=nw_fp_multiply(test.a|sign,test.b,test.precision==59,enables|rn);
        fegetenv(&after);
        const uint64_t mag=expected&0x7fffffffffffffffULL;
        const unsigned classification=!mag ? negative ? 18 : 2 : mag==0x7ff0000000000000ULL ? negative ? 9 : 5 :
            !adjusted && mag<(test.precision==59 ? 0x3810000000000000ULL : 0x0010000000000000ULL) ? negative ? 24 : 20 : negative ? 8 : 4;
        if (result.value!=expected || result.causes!=causes || result.rounded!=((status&1 ? 0x20000u : 0)|(status&2 ? 0x40000u : 0)) || result.classification!=classification ||
            memcmp(&before,&after,sizeof(before))!=0) {
            fprintf(stderr,"multiply sanitizer mismatch prim=%u a=%016llx b=%016llx rn=%u enables=%x\n",test.precision,(unsigned long long)test.a,(unsigned long long)test.b,rn,enables);return 1;
        }
        ++cases;
    }
    printf("Private multiply sanitizers: %u exact-rational executions, preserved environment, zero failures\n",cases);
}
