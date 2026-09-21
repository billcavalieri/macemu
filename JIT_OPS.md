# SheepShaver NW JIT — opcode checklist

Generated **2026-09-21** from `powerpc_ii_table` in `SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-decode.cpp` (345 names) against `nw_jit_op_supported` / `nw_jit_op_ends_block` in `SheepShaver/src/nw_jit.cpp`.

Regenerate: `python3 tools/gen_jit_ops_md.py` (ports `nw_jit_op_supported` against `powerpc_ii_table`).

## Status legend

| Mark | Meaning |
|------|---------|
| `[x]` | **done** — representative encoding(s) pass the allowlist; no known form caveat; AltiVec name referenced in `nw_jit.cpp` |
| `[~]` | **partial** — allowlisted with a form caveat (Rc, RA≠0, BO, AA, ends_block), or AltiVec XO hit without a named emit reference |
| `[ ]` | **todo** — not in `nw_jit_op_supported` (falls to interpreter / `skip_unsup`) |
| `[-]` | **exclude** — do not mill |

**Wave** follows the mill-everything plan (W1 hot partials → W7 AltiVec FP → W8 sweep).

A checked box means the JIT **will attempt** the op. It is not a forever VERIFY sign-off.

## Summary

| Status | Count |
|--------|------:|
| done | 308 |
| partial | 34 |
| todo | 0 |
| exclude | 3 |
| **total** | **345** |

| Family | Count |
|--------|------:|
| altivec | 154 |
| integer | 72 |
| fp | 50 |
| mem | 40 |
| control | 15 |
| spr | 13 |
| none | 1 |

## Checklist

| | Op | Family | Wave | Encoding | Notes |
|---|----|--------|------|----------|-------|
| [x] | `lvebx` | altivec | — | `X_form` 31/7 | kpx: `EXECUTE_VECTOR_LOADSTORE(load, V16QIm, RA_or_0, RB)`; X_form prim=31 xo=7 CFLOW_NORMAL; in allowlist |
| [x] | `lvehx` | altivec | — | `X_form` 31/39 | kpx: `EXECUTE_VECTOR_LOADSTORE(load, V8HIm, RA_or_0, RB)`; X_form prim=31 xo=39 CFLOW_NORMAL; in allowlist |
| [x] | `lvewx` | altivec | — | `X_form` 31/71 | kpx: `EXECUTE_VECTOR_LOADSTORE(load, V4SI, RA_or_0, RB)`; X_form prim=31 xo=71 CFLOW_NORMAL; in allowlist |
| [x] | `lvsl` | altivec | — | `X_form` 31/6 | kpx: `EXECUTE_1(vector_load_for_shift, 1)`; X_form prim=31 xo=6 CFLOW_NORMAL; in allowlist |
| [x] | `lvsr` | altivec | — | `X_form` 31/38 | kpx: `EXECUTE_1(vector_load_for_shift, 0)`; X_form prim=31 xo=38 CFLOW_NORMAL; in allowlist |
| [~] | `lvx` | altivec | W6 | `X_form` 31/103 | kpx: `EXECUTE_VECTOR_LOADSTORE(load, V2DI, RA_or_0, RB)`; X_form prim=31 xo=103 CFLOW_NORMAL; hint ignored (lvxl same path).; in allowlist |
| [~] | `lvxl` | altivec | W6 | `X_form` 31/359 | kpx: `EXECUTE_VECTOR_LOADSTORE(load, V2DI, RA_or_0, RB)`; X_form prim=31 xo=359 CFLOW_NORMAL; hint ignored.; in allowlist |
| [x] | `stvebx` | altivec | — | `X_form` 31/135 | kpx: `EXECUTE_VECTOR_LOADSTORE(store, V16QIm, RA_or_0, RB)`; X_form prim=31 xo=135 CFLOW_NORMAL; in allowlist |
| [x] | `stvehx` | altivec | — | `X_form` 31/167 | kpx: `EXECUTE_VECTOR_LOADSTORE(store, V8HIm, RA_or_0, RB)`; X_form prim=31 xo=167 CFLOW_NORMAL; in allowlist |
| [x] | `stvewx` | altivec | — | `X_form` 31/199 | kpx: `EXECUTE_VECTOR_LOADSTORE(store, V4SI, RA_or_0, RB)`; X_form prim=31 xo=199 CFLOW_NORMAL; in allowlist |
| [~] | `stvx` | altivec | W6 | `X_form` 31/231 | kpx: `EXECUTE_VECTOR_LOADSTORE(store, V2DI, RA_or_0, RB)`; X_form prim=31 xo=231 CFLOW_NORMAL; hint ignored.; in allowlist |
| [~] | `stvxl` | altivec | W6 | `X_form` 31/487 | kpx: `EXECUTE_VECTOR_LOADSTORE(store, V2DI, RA_or_0, RB)`; X_form prim=31 xo=487 CFLOW_NORMAL; hint ignored.; in allowlist |
| [x] | `vaddcuw` | altivec | — | `VX_form` 4/384 | kpx: `EXECUTE_VECTOR_ARITH(addcuw, V4SI, V4SI, V4SI, NONE)`; VX_form prim=4 xo=384 CFLOW_NORMAL; in allowlist |
| [x] | `vaddfp` | altivec | — | `VX_form` 4/10 | kpx: `EXECUTE_VECTOR_ARITH(fadds, V4SF, V4SF, V4SF, NONE)`; VX_form prim=4 xo=10 CFLOW_NORMAL; in allowlist |
| [x] | `vaddsbs` | altivec | — | `VX_form` 4/768 | kpx: `EXECUTE_VECTOR_ARITH(add, V16QI_SAT<int8>, V16QI_SAT<int8>, V16QI_SAT<int8>, NONE)`; VX_form prim=4 xo=768 CFLOW_NORMAL; in allowlist |
| [x] | `vaddshs` | altivec | — | `VX_form` 4/832 | kpx: `EXECUTE_VECTOR_ARITH(add, V8HI_SAT<int16>, V8HI_SAT<int16>, V8HI_SAT<int16>, NONE)`; VX_form prim=4 xo=832 CFLOW_NORMAL; in allowlist |
| [x] | `vaddsws` | altivec | — | `VX_form` 4/896 | kpx: `EXECUTE_VECTOR_ARITH(add_64, V4SI_SAT<int32>, V4SI_SAT<int32>, V4SI_SAT<int32>, NONE)`; VX_form prim=4 xo=896 CFLOW_NORMAL; in allowlist |
| [x] | `vaddubm` | altivec | — | `VX_form` 4/0 | kpx: `EXECUTE_VECTOR_ARITH(add, V16QI, V16QI, V16QI, NONE)`; VX_form prim=4 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `vaddubs` | altivec | — | `VX_form` 4/512 | kpx: `EXECUTE_VECTOR_ARITH(add, V16QI_SAT<uint8>, V16QI_SAT<uint8>, V16QI_SAT<uint8>, NONE)`; VX_form prim=4 xo=512 CFLOW_NORMAL; in allowlist |
| [x] | `vadduhm` | altivec | — | `VX_form` 4/64 | kpx: `EXECUTE_VECTOR_ARITH(add, V8HI, V8HI, V8HI, NONE)`; VX_form prim=4 xo=64 CFLOW_NORMAL; in allowlist |
| [x] | `vadduhs` | altivec | — | `VX_form` 4/576 | kpx: `EXECUTE_VECTOR_ARITH(add, V8HI_SAT<uint16>, V8HI_SAT<uint16>, V8HI_SAT<uint16>, NONE)`; VX_form prim=4 xo=576 CFLOW_NORMAL; in allowlist |
| [x] | `vadduwm` | altivec | — | `VX_form` 4/128 | kpx: `EXECUTE_VECTOR_ARITH(add, V4SI, V4SI, V4SI, NONE)`; VX_form prim=4 xo=128 CFLOW_NORMAL; in allowlist |
| [x] | `vadduws` | altivec | — | `VX_form` 4/640 | kpx: `EXECUTE_VECTOR_ARITH(add_64, V4SI_SAT<uint32>, V4SI_SAT<uint32>, V4SI_SAT<uint32>, NONE)`; VX_form prim=4 xo=640 CFLOW_NORMAL; in allowlist |
| [x] | `vand` | altivec | — | `VX_form` 4/1028 | kpx: `EXECUTE_VECTOR_ARITH(and_64, V2DI, V2DI, V2DI, NONE)`; VX_form prim=4 xo=1028 CFLOW_NORMAL; in allowlist |
| [x] | `vandc` | altivec | — | `VX_form` 4/1092 | kpx: `EXECUTE_VECTOR_ARITH(andc_64, V2DI, V2DI, V2DI, NONE)`; VX_form prim=4 xo=1092 CFLOW_NORMAL; in allowlist |
| [x] | `vavgsb` | altivec | — | `VX_form` 4/1282 | kpx: `EXECUTE_VECTOR_ARITH(avgsb, V16QI, V16QI, V16QI, NONE)`; VX_form prim=4 xo=1282 CFLOW_NORMAL; in allowlist |
| [x] | `vavgsh` | altivec | — | `VX_form` 4/1346 | kpx: `EXECUTE_VECTOR_ARITH(avgsh, V8HI, V8HI, V8HI, NONE)`; VX_form prim=4 xo=1346 CFLOW_NORMAL; in allowlist |
| [x] | `vavgsw` | altivec | — | `VX_form` 4/1410 | kpx: `EXECUTE_VECTOR_ARITH(avgsw, V4SI, V4SI, V4SI, NONE)`; VX_form prim=4 xo=1410 CFLOW_NORMAL; in allowlist |
| [x] | `vavgub` | altivec | — | `VX_form` 4/1026 | kpx: `EXECUTE_VECTOR_ARITH(avgub, V16QI, V16QI, V16QI, NONE)`; VX_form prim=4 xo=1026 CFLOW_NORMAL; in allowlist |
| [x] | `vavguh` | altivec | — | `VX_form` 4/1090 | kpx: `EXECUTE_VECTOR_ARITH(avguh, V8HI, V8HI, V8HI, NONE)`; VX_form prim=4 xo=1090 CFLOW_NORMAL; in allowlist |
| [x] | `vavguw` | altivec | — | `VX_form` 4/1154 | kpx: `EXECUTE_VECTOR_ARITH(avguw, V4SI, V4SI, V4SI, NONE)`; VX_form prim=4 xo=1154 CFLOW_NORMAL; in allowlist |
| [x] | `vcfsx` | altivec | — | `VX_form` 4/842 | kpx: `EXECUTE_VECTOR_ARITH(cvt_si2fp<int32>, V4SF, UIMM, V4SIs, NONE)`; VX_form prim=4 xo=842 CFLOW_NORMAL; in allowlist |
| [x] | `vcfux` | altivec | — | `VX_form` 4/778 | kpx: `EXECUTE_VECTOR_ARITH(cvt_si2fp<uint32>, V4SF, UIMM, V4SI, NONE)`; VX_form prim=4 xo=778 CFLOW_NORMAL; in allowlist |
| [x] | `vcmpbfp` | altivec | — | `VXR_form` 4/966 | kpx: `EXECUTE_VECTOR_COMPARE(cmpbfp, V4SI, V4SF, V4SF, 0)`; VXR_form prim=4 xo=966 CFLOW_NORMAL; in allowlist |
| [x] | `vcmpeqfp` | altivec | — | `VXR_form` 4/198 | kpx: `EXECUTE_VECTOR_COMPARE(cmp_eq<float>, V4SI, V4SF, V4SF, 1)`; VXR_form prim=4 xo=198 CFLOW_NORMAL; in allowlist |
| [x] | `vcmpequb` | altivec | — | `VXR_form` 4/6 | kpx: `EXECUTE_VECTOR_COMPARE(cmp_eq<uint8>, V16QI, V16QI, V16QI, 1)`; VXR_form prim=4 xo=6 CFLOW_NORMAL; in allowlist |
| [x] | `vcmpequh` | altivec | — | `VXR_form` 4/70 | kpx: `EXECUTE_VECTOR_COMPARE(cmp_eq<uint16>, V8HI, V8HI, V8HI, 1)`; VXR_form prim=4 xo=70 CFLOW_NORMAL; in allowlist |
| [x] | `vcmpequw` | altivec | — | `VXR_form` 4/134 | kpx: `EXECUTE_VECTOR_COMPARE(cmp_eq<uint32>, V4SI, V4SI, V4SI, 1)`; VXR_form prim=4 xo=134 CFLOW_NORMAL; in allowlist |
| [x] | `vcmpgefp` | altivec | — | `VXR_form` 4/454 | kpx: `EXECUTE_VECTOR_COMPARE(cmp_ge<float>, V4SI, V4SF, V4SF, 1)`; VXR_form prim=4 xo=454 CFLOW_NORMAL; in allowlist |
| [x] | `vcmpgtfp` | altivec | — | `VXR_form` 4/710 | kpx: `EXECUTE_VECTOR_COMPARE(cmp_gt<float>, V4SI, V4SF, V4SF, 1)`; VXR_form prim=4 xo=710 CFLOW_NORMAL; in allowlist |
| [x] | `vcmpgtsb` | altivec | — | `VXR_form` 4/774 | kpx: `EXECUTE_VECTOR_COMPARE(cmp_gt<int8>, V16QI, V16QIs, V16QIs, 1)`; VXR_form prim=4 xo=774 CFLOW_NORMAL; in allowlist |
| [x] | `vcmpgtsh` | altivec | — | `VXR_form` 4/838 | kpx: `EXECUTE_VECTOR_COMPARE(cmp_gt<int16>, V8HI, V8HIs, V8HIs, 1)`; VXR_form prim=4 xo=838 CFLOW_NORMAL; in allowlist |
| [x] | `vcmpgtsw` | altivec | — | `VXR_form` 4/902 | kpx: `EXECUTE_VECTOR_COMPARE(cmp_gt<int32>, V4SI, V4SIs, V4SIs, 1)`; VXR_form prim=4 xo=902 CFLOW_NORMAL; in allowlist |
| [x] | `vcmpgtub` | altivec | — | `VXR_form` 4/518 | kpx: `EXECUTE_VECTOR_COMPARE(cmp_gt<uint8>, V16QI, V16QI, V16QI, 1)`; VXR_form prim=4 xo=518 CFLOW_NORMAL; in allowlist |
| [x] | `vcmpgtuh` | altivec | — | `VXR_form` 4/582 | kpx: `EXECUTE_VECTOR_COMPARE(cmp_gt<uint16>, V8HI, V8HI, V8HI, 1)`; VXR_form prim=4 xo=582 CFLOW_NORMAL; in allowlist |
| [x] | `vcmpgtuw` | altivec | — | `VXR_form` 4/646 | kpx: `EXECUTE_VECTOR_COMPARE(cmp_gt<uint32>, V4SI, V4SI, V4SI, 1)`; VXR_form prim=4 xo=646 CFLOW_NORMAL; in allowlist |
| [x] | `vctsxs` | altivec | — | `VX_form` 4/970 | kpx: `EXECUTE_VECTOR_ARITH(cvt_fp2si, V4SI_SAT<int32>, UIMM, V4SF, NONE)`; VX_form prim=4 xo=970 CFLOW_NORMAL; in allowlist |
| [x] | `vctuxs` | altivec | — | `VX_form` 4/906 | kpx: `EXECUTE_VECTOR_ARITH(cvt_fp2si, V4SI_SAT<uint32>, UIMM, V4SF, NONE)`; VX_form prim=4 xo=906 CFLOW_NORMAL; in allowlist |
| [x] | `vexptefp` | altivec | — | `VX_form` 4/394 | kpx: `EXECUTE_VECTOR_ARITH(exp2, V4SF, NONE, V4SF, NONE)`; VX_form prim=4 xo=394 CFLOW_NORMAL; in allowlist |
| [x] | `vlogefp` | altivec | — | `VX_form` 4/458 | kpx: `EXECUTE_VECTOR_ARITH(log2, V4SF, NONE, V4SF, NONE)`; VX_form prim=4 xo=458 CFLOW_NORMAL; in allowlist |
| [x] | `vmaddfp` | altivec | — | `VA_form` 4/46 | kpx: `EXECUTE_VECTOR_ARITH(vmaddfp, V4SF, V4SF, V4SF, V4SF)`; VA_form prim=4 xo=46 CFLOW_NORMAL; in allowlist |
| [x] | `vmaxfp` | altivec | — | `VX_form` 4/1034 | kpx: `EXECUTE_VECTOR_ARITH(max<float>, V4SF, V4SF, V4SF, NONE)`; VX_form prim=4 xo=1034 CFLOW_NORMAL; in allowlist |
| [x] | `vmaxsb` | altivec | — | `VX_form` 4/258 | kpx: `EXECUTE_VECTOR_ARITH(max<int8>, V16QI, V16QI, V16QI, NONE)`; VX_form prim=4 xo=258 CFLOW_NORMAL; in allowlist |
| [x] | `vmaxsh` | altivec | — | `VX_form` 4/322 | kpx: `EXECUTE_VECTOR_ARITH(max<int16>, V8HI, V8HI, V8HI, NONE)`; VX_form prim=4 xo=322 CFLOW_NORMAL; in allowlist |
| [x] | `vmaxsw` | altivec | — | `VX_form` 4/386 | kpx: `EXECUTE_VECTOR_ARITH(max<int32>, V4SI, V4SI, V4SI, NONE)`; VX_form prim=4 xo=386 CFLOW_NORMAL; in allowlist |
| [x] | `vmaxub` | altivec | — | `VX_form` 4/2 | kpx: `EXECUTE_VECTOR_ARITH(max<uint8>, V16QI, V16QI, V16QI, NONE)`; VX_form prim=4 xo=2 CFLOW_NORMAL; in allowlist |
| [x] | `vmaxuh` | altivec | — | `VX_form` 4/66 | kpx: `EXECUTE_VECTOR_ARITH(max<uint16>, V8HI, V8HI, V8HI, NONE)`; VX_form prim=4 xo=66 CFLOW_NORMAL; in allowlist |
| [x] | `vmaxuw` | altivec | — | `VX_form` 4/130 | kpx: `EXECUTE_VECTOR_ARITH(max<uint32>, V4SI, V4SI, V4SI, NONE)`; VX_form prim=4 xo=130 CFLOW_NORMAL; in allowlist |
| [x] | `vmhaddshs` | altivec | — | `VA_form` 4/32 | kpx: `EXECUTE_VECTOR_ARITH(mhraddsh<0>, V8HI_SAT<int16>, V8HI_SAT<int16>, V8HI_SAT<int16>, V8HI_SAT<int16>)`; VA_form prim=4 xo=32 CFLOW_NORMAL; in allowlist |
| [x] | `vmhraddshs` | altivec | — | `VA_form` 4/33 | kpx: `EXECUTE_VECTOR_ARITH(mhraddsh<0x4000>, V8HI_SAT<int16>, V8HI_SAT<int16>, V8HI_SAT<int16>, V8HI_SAT<int16>)`; VA_form prim=4 xo=33 CFLOW_NORMAL; in allowlist |
| [x] | `vminfp` | altivec | — | `VX_form` 4/1098 | kpx: `EXECUTE_VECTOR_ARITH(min<float>, V4SF, V4SF, V4SF, NONE)`; VX_form prim=4 xo=1098 CFLOW_NORMAL; in allowlist |
| [x] | `vminsb` | altivec | — | `VX_form` 4/770 | kpx: `EXECUTE_VECTOR_ARITH(min<int8>, V16QI, V16QI, V16QI, NONE)`; VX_form prim=4 xo=770 CFLOW_NORMAL; in allowlist |
| [x] | `vminsh` | altivec | — | `VX_form` 4/834 | kpx: `EXECUTE_VECTOR_ARITH(min<int16>, V8HI, V8HI, V8HI, NONE)`; VX_form prim=4 xo=834 CFLOW_NORMAL; in allowlist |
| [x] | `vminsw` | altivec | — | `VX_form` 4/898 | kpx: `EXECUTE_VECTOR_ARITH(min<int32>, V4SI, V4SI, V4SI, NONE)`; VX_form prim=4 xo=898 CFLOW_NORMAL; in allowlist |
| [x] | `vminub` | altivec | — | `VX_form` 4/514 | kpx: `EXECUTE_VECTOR_ARITH(min<uint8>, V16QI, V16QI, V16QI, NONE)`; VX_form prim=4 xo=514 CFLOW_NORMAL; in allowlist |
| [x] | `vminuh` | altivec | — | `VX_form` 4/578 | kpx: `EXECUTE_VECTOR_ARITH(min<uint16>, V8HI, V8HI, V8HI, NONE)`; VX_form prim=4 xo=578 CFLOW_NORMAL; in allowlist |
| [x] | `vminuw` | altivec | — | `VX_form` 4/642 | kpx: `EXECUTE_VECTOR_ARITH(min<uint32>, V4SI, V4SI, V4SI, NONE)`; VX_form prim=4 xo=642 CFLOW_NORMAL; in allowlist |
| [x] | `vmladduhm` | altivec | — | `VA_form` 4/34 | kpx: `EXECUTE_VECTOR_ARITH(mladduh, V8HI, V8HI, V8HI, V8HI)`; VA_form prim=4 xo=34 CFLOW_NORMAL; in allowlist |
| [x] | `vmrghb` | altivec | — | `VX_form` 4/12 | kpx: `EXECUTE_VECTOR_MERGE(V16QIm, V16QIm, V16QIm, 0)`; VX_form prim=4 xo=12 CFLOW_NORMAL; in allowlist |
| [x] | `vmrghh` | altivec | — | `VX_form` 4/76 | kpx: `EXECUTE_VECTOR_MERGE(V8HIm, V8HIm, V8HIm, 0)`; VX_form prim=4 xo=76 CFLOW_NORMAL; in allowlist |
| [x] | `vmrghw` | altivec | — | `VX_form` 4/140 | kpx: `EXECUTE_VECTOR_MERGE(V4SI, V4SI, V4SI, 0)`; VX_form prim=4 xo=140 CFLOW_NORMAL; in allowlist |
| [x] | `vmrglb` | altivec | — | `VX_form` 4/268 | kpx: `EXECUTE_VECTOR_MERGE(V16QIm, V16QIm, V16QIm, 1)`; VX_form prim=4 xo=268 CFLOW_NORMAL; in allowlist |
| [x] | `vmrglh` | altivec | — | `VX_form` 4/332 | kpx: `EXECUTE_VECTOR_MERGE(V8HIm, V8HIm, V8HIm, 1)`; VX_form prim=4 xo=332 CFLOW_NORMAL; in allowlist |
| [x] | `vmrglw` | altivec | — | `VX_form` 4/396 | kpx: `EXECUTE_VECTOR_MERGE(V4SI, V4SI, V4SI, 1)`; VX_form prim=4 xo=396 CFLOW_NORMAL; in allowlist |
| [x] | `vmsummbm` | altivec | — | `VA_form` 4/37 | kpx: `EXECUTE_VECTOR_ARITH_MIXED(smul, V4SI, V16QI_SAT<int8>, V16QI_SAT<uint8>, V4SI)`; VA_form prim=4 xo=37 CFLOW_NORMAL; in allowlist |
| [x] | `vmsumshm` | altivec | — | `VA_form` 4/40 | kpx: `EXECUTE_VECTOR_ARITH_MIXED(smul, V4SI, V8HI_SAT<int16>, V8HI_SAT<int16>, V4SI)`; VA_form prim=4 xo=40 CFLOW_NORMAL; in allowlist |
| [x] | `vmsumshs` | altivec | — | `VA_form` 4/41 | kpx: `EXECUTE_VECTOR_ARITH_MIXED(smul_64, V4SI_SAT<int32>, V8HI_SAT<int16>, V8HI_SAT<int16>, V4SIs)`; VA_form prim=4 xo=41 CFLOW_NORMAL; in allowlist |
| [x] | `vmsumubm` | altivec | — | `VA_form` 4/36 | kpx: `EXECUTE_VECTOR_ARITH_MIXED(mul, V4SI, V16QI, V16QI, V4SI)`; VA_form prim=4 xo=36 CFLOW_NORMAL; in allowlist |
| [x] | `vmsumuhm` | altivec | — | `VA_form` 4/38 | kpx: `EXECUTE_VECTOR_ARITH_MIXED(mul, V4SI, V8HI, V8HI, V4SI)`; VA_form prim=4 xo=38 CFLOW_NORMAL; in allowlist |
| [x] | `vmsumuhs` | altivec | — | `VA_form` 4/39 | kpx: `EXECUTE_VECTOR_ARITH_MIXED(mul, V4SI_SAT<uint32>, V8HI, V8HI, V4SI)`; VA_form prim=4 xo=39 CFLOW_NORMAL; in allowlist |
| [x] | `vmulesb` | altivec | — | `VX_form` 4/776 | kpx: `EXECUTE_VECTOR_ARITH_ODD(0, smul, V8HIm, V16QIm_SAT<int8>, V16QIm_SAT<int8>, NONE)`; VX_form prim=4 xo=776 CFLOW_NORMAL; in allowlist |
| [x] | `vmulesh` | altivec | — | `VX_form` 4/840 | kpx: `EXECUTE_VECTOR_ARITH_ODD(0, smul, V4SI, V8HIm_SAT<int16>, V8HIm_SAT<int16>, NONE)`; VX_form prim=4 xo=840 CFLOW_NORMAL; in allowlist |
| [x] | `vmuleub` | altivec | — | `VX_form` 4/520 | kpx: `EXECUTE_VECTOR_ARITH_ODD(0, mul, V8HIm, V16QIm, V16QIm, NONE)`; VX_form prim=4 xo=520 CFLOW_NORMAL; in allowlist |
| [x] | `vmuleuh` | altivec | — | `VX_form` 4/584 | kpx: `EXECUTE_VECTOR_ARITH_ODD(0, mul, V4SI, V8HIm, V8HIm, NONE)`; VX_form prim=4 xo=584 CFLOW_NORMAL; in allowlist |
| [x] | `vmulosb` | altivec | — | `VX_form` 4/264 | kpx: `EXECUTE_VECTOR_ARITH_ODD(1, smul, V8HIm, V16QIm_SAT<int8>, V16QIm_SAT<int8>, NONE)`; VX_form prim=4 xo=264 CFLOW_NORMAL; in allowlist |
| [x] | `vmulosh` | altivec | — | `VX_form` 4/328 | kpx: `EXECUTE_VECTOR_ARITH_ODD(1, smul, V4SI, V8HIm_SAT<int16>, V8HIm_SAT<int16>, NONE)`; VX_form prim=4 xo=328 CFLOW_NORMAL; in allowlist |
| [x] | `vmuloub` | altivec | — | `VX_form` 4/8 | kpx: `EXECUTE_VECTOR_ARITH_ODD(1, mul, V8HIm, V16QIm, V16QIm, NONE)`; VX_form prim=4 xo=8 CFLOW_NORMAL; in allowlist |
| [x] | `vmulouh` | altivec | — | `VX_form` 4/72 | kpx: `EXECUTE_VECTOR_ARITH_ODD(1, mul, V4SI, V8HIm, V8HIm, NONE)`; VX_form prim=4 xo=72 CFLOW_NORMAL; in allowlist |
| [x] | `vnmsubfp` | altivec | — | `VA_form` 4/47 | kpx: `EXECUTE_VECTOR_ARITH(vnmsubfp, V4SF, V4SF, V4SF, V4SF)`; VA_form prim=4 xo=47 CFLOW_NORMAL; in allowlist |
| [x] | `vnor` | altivec | — | `VX_form` 4/1284 | kpx: `EXECUTE_VECTOR_ARITH(nor_64, V2DI, V2DI, V2DI, NONE)`; VX_form prim=4 xo=1284 CFLOW_NORMAL; in allowlist |
| [x] | `vor` | altivec | — | `VX_form` 4/1156 | kpx: `EXECUTE_VECTOR_ARITH(or_64, V2DI, V2DI, V2DI, NONE)`; VX_form prim=4 xo=1156 CFLOW_NORMAL; in allowlist |
| [x] | `vperm` | altivec | — | `VA_form` 4/43 | kpx: `EXECUTE_0(vector_permute)`; VA_form prim=4 xo=43 CFLOW_NORMAL; in allowlist |
| [x] | `vpkpx` | altivec | — | `VX_form` 4/782 | kpx: `EXECUTE_0(vector_pack_pixel)`; VX_form prim=4 xo=782 CFLOW_NORMAL; in allowlist |
| [x] | `vpkshss` | altivec | — | `VX_form` 4/398 | kpx: `EXECUTE_VECTOR_PACK(V16QIm_SAT<int8>, V8HIm, V8HIm)`; VX_form prim=4 xo=398 CFLOW_NORMAL; in allowlist |
| [x] | `vpkshus` | altivec | — | `VX_form` 4/270 | kpx: `EXECUTE_VECTOR_PACK(V16QIm_SAT<uint8>, V8HIm, V8HIm)`; VX_form prim=4 xo=270 CFLOW_NORMAL; in allowlist |
| [x] | `vpkswss` | altivec | — | `VX_form` 4/462 | kpx: `EXECUTE_VECTOR_PACK(V8HIm_SAT<int16>, V4SI, V4SI)`; VX_form prim=4 xo=462 CFLOW_NORMAL; in allowlist |
| [x] | `vpkswus` | altivec | — | `VX_form` 4/334 | kpx: `EXECUTE_VECTOR_PACK(V8HIm_SAT<uint16>, V4SI, V4SI)`; VX_form prim=4 xo=334 CFLOW_NORMAL; in allowlist |
| [x] | `vpkuhum` | altivec | — | `VX_form` 4/14 | kpx: `EXECUTE_VECTOR_PACK(V16QIm, V8HIm, V8HIm)`; VX_form prim=4 xo=14 CFLOW_NORMAL; in allowlist |
| [x] | `vpkuhus` | altivec | — | `VX_form` 4/142 | kpx: `EXECUTE_VECTOR_PACK(V16QIm_USAT<uint8>, V8HIm, V8HIm)`; VX_form prim=4 xo=142 CFLOW_NORMAL; in allowlist |
| [x] | `vpkuwum` | altivec | — | `VX_form` 4/78 | kpx: `EXECUTE_VECTOR_PACK(V8HIm, V4SI, V4SI)`; VX_form prim=4 xo=78 CFLOW_NORMAL; in allowlist |
| [x] | `vpkuwus` | altivec | — | `VX_form` 4/206 | kpx: `EXECUTE_VECTOR_PACK(V8HIm_USAT<uint16>, V4SI, V4SI)`; VX_form prim=4 xo=206 CFLOW_NORMAL; in allowlist |
| [x] | `vrefp` | altivec | — | `VX_form` 4/266 | kpx: `EXECUTE_VECTOR_ARITH(fres, V4SF, NONE, V4SF, NONE)`; VX_form prim=4 xo=266 CFLOW_NORMAL; in allowlist |
| [x] | `vrfim` | altivec | — | `VX_form` 4/714 | kpx: `EXECUTE_VECTOR_ARITH(frsim, V4SF, NONE, V4SF, NONE)`; VX_form prim=4 xo=714 CFLOW_NORMAL; in allowlist |
| [x] | `vrfin` | altivec | — | `VX_form` 4/522 | kpx: `EXECUTE_VECTOR_ARITH(frsin, V4SF, NONE, V4SF, NONE)`; VX_form prim=4 xo=522 CFLOW_NORMAL; in allowlist |
| [x] | `vrfip` | altivec | — | `VX_form` 4/650 | kpx: `EXECUTE_VECTOR_ARITH(frsip, V4SF, NONE, V4SF, NONE)`; VX_form prim=4 xo=650 CFLOW_NORMAL; in allowlist |
| [x] | `vrfiz` | altivec | — | `VX_form` 4/586 | kpx: `EXECUTE_VECTOR_ARITH(frsiz, V4SF, NONE, V4SF, NONE)`; VX_form prim=4 xo=586 CFLOW_NORMAL; in allowlist |
| [x] | `vrlb` | altivec | — | `VX_form` 4/4 | kpx: `EXECUTE_VECTOR_ARITH(vrl<uint8>, V16QI, V16QI, V16QI, NONE)`; VX_form prim=4 xo=4 CFLOW_NORMAL; in allowlist |
| [x] | `vrlh` | altivec | — | `VX_form` 4/68 | kpx: `EXECUTE_VECTOR_ARITH(vrl<uint16>, V8HI, V8HI, V8HI, NONE)`; VX_form prim=4 xo=68 CFLOW_NORMAL; in allowlist |
| [x] | `vrlw` | altivec | — | `VX_form` 4/132 | kpx: `EXECUTE_VECTOR_ARITH(vrl<uint32>, V4SI, V4SI, V4SI, NONE)`; VX_form prim=4 xo=132 CFLOW_NORMAL; in allowlist |
| [x] | `vrsqrtefp` | altivec | — | `VX_form` 4/330 | kpx: `EXECUTE_VECTOR_ARITH(frsqrte, V4SF, NONE, V4SF, NONE)`; VX_form prim=4 xo=330 CFLOW_NORMAL; in allowlist |
| [x] | `vsel` | altivec | — | `VA_form` 4/42 | kpx: `EXECUTE_VECTOR_ARITH(vsel, V4SI, V4SI, V4SI, V4SI)`; VA_form prim=4 xo=42 CFLOW_NORMAL; in allowlist |
| [x] | `vsl` | altivec | — | `VX_form` 4/452 | kpx: `EXECUTE_1(vector_shift, -1)`; VX_form prim=4 xo=452 CFLOW_NORMAL; in allowlist |
| [x] | `vslb` | altivec | — | `VX_form` 4/260 | kpx: `EXECUTE_VECTOR_ARITH(vsl<uint8>, V16QI, V16QI, V16QI, NONE)`; VX_form prim=4 xo=260 CFLOW_NORMAL; in allowlist |
| [x] | `vsldoi` | altivec | — | `VA_form` 4/44 | kpx: `EXECUTE_VECTOR_SHIFT_OCTET(-1, V16QIm, V16QIm, V16QIm, SHB)`; VA_form prim=4 xo=44 CFLOW_NORMAL; in allowlist |
| [x] | `vslh` | altivec | — | `VX_form` 4/324 | kpx: `EXECUTE_VECTOR_ARITH(vsl<uint16>, V8HI, V8HI, V8HI, NONE)`; VX_form prim=4 xo=324 CFLOW_NORMAL; in allowlist |
| [~] | `vslo` | altivec | W6 | `VX_form` 4/1036 | kpx: `EXECUTE_VECTOR_SHIFT_OCTET(-1, V16QIm, V16QIm, NONE, SHBO)`; VX_form prim=4 xo=1036 CFLOW_NORMAL; Allowlisted but **ends_block** (one-op; Starting Up lock).; **ends_block**; in allowlist |
| [x] | `vslw` | altivec | — | `VX_form` 4/388 | kpx: `EXECUTE_VECTOR_ARITH(vsl<uint32>, V4SI, V4SI, V4SI, NONE)`; VX_form prim=4 xo=388 CFLOW_NORMAL; in allowlist |
| [x] | `vspltb` | altivec | — | `VX_form` 4/524 | kpx: `EXECUTE_VECTOR_SPLAT(nop, V16QI, V16QIm, false)`; VX_form prim=4 xo=524 CFLOW_NORMAL; in allowlist |
| [x] | `vsplth` | altivec | — | `VX_form` 4/588 | kpx: `EXECUTE_VECTOR_SPLAT(nop, V8HI, V8HIm, false)`; VX_form prim=4 xo=588 CFLOW_NORMAL; in allowlist |
| [x] | `vspltisb` | altivec | — | `VX_form` 4/780 | kpx: `EXECUTE_VECTOR_SPLAT(sign_extend_5_32, V16QI, UIMM, true)`; VX_form prim=4 xo=780 CFLOW_NORMAL; in allowlist |
| [x] | `vspltish` | altivec | — | `VX_form` 4/844 | kpx: `EXECUTE_VECTOR_SPLAT(sign_extend_5_32, V8HI, UIMM, true)`; VX_form prim=4 xo=844 CFLOW_NORMAL; in allowlist |
| [x] | `vspltisw` | altivec | — | `VX_form` 4/908 | kpx: `EXECUTE_VECTOR_SPLAT(sign_extend_5_32, V4SI, UIMM, true)`; VX_form prim=4 xo=908 CFLOW_NORMAL; in allowlist |
| [x] | `vspltw` | altivec | — | `VX_form` 4/652 | kpx: `EXECUTE_VECTOR_SPLAT(nop, V4SI, V4SI, false)`; VX_form prim=4 xo=652 CFLOW_NORMAL; in allowlist |
| [x] | `vsr` | altivec | — | `VX_form` 4/708 | kpx: `EXECUTE_1(vector_shift, +1)`; VX_form prim=4 xo=708 CFLOW_NORMAL; in allowlist |
| [x] | `vsrab` | altivec | — | `VX_form` 4/772 | kpx: `EXECUTE_VECTOR_ARITH(vsr<int8>, V16QI, V16QIs, V16QI, NONE)`; VX_form prim=4 xo=772 CFLOW_NORMAL; in allowlist |
| [x] | `vsrah` | altivec | — | `VX_form` 4/836 | kpx: `EXECUTE_VECTOR_ARITH(vsr<int16>, V8HI, V8HIs, V8HI, NONE)`; VX_form prim=4 xo=836 CFLOW_NORMAL; in allowlist |
| [x] | `vsraw` | altivec | — | `VX_form` 4/900 | kpx: `EXECUTE_VECTOR_ARITH(vsr<int32>, V4SI, V4SIs, V4SIs, NONE)`; VX_form prim=4 xo=900 CFLOW_NORMAL; in allowlist |
| [x] | `vsrb` | altivec | — | `VX_form` 4/516 | kpx: `EXECUTE_VECTOR_ARITH(vsr<uint8>, V16QI, V16QI, V16QI, NONE)`; VX_form prim=4 xo=516 CFLOW_NORMAL; in allowlist |
| [x] | `vsrh` | altivec | — | `VX_form` 4/580 | kpx: `EXECUTE_VECTOR_ARITH(vsr<uint16>, V8HI, V8HI, V8HI, NONE)`; VX_form prim=4 xo=580 CFLOW_NORMAL; in allowlist |
| [~] | `vsro` | altivec | W6 | `VX_form` 4/1100 | kpx: `EXECUTE_VECTOR_SHIFT_OCTET(+1, V16QIm, V16QIm, NONE, SHBO)`; VX_form prim=4 xo=1100 CFLOW_NORMAL; Allowlisted but **ends_block** (one-op; Starting Up lock).; **ends_block**; in allowlist |
| [x] | `vsrw` | altivec | — | `VX_form` 4/644 | kpx: `EXECUTE_VECTOR_ARITH(vsr<uint32>, V4SI, V4SI, V4SI, NONE)`; VX_form prim=4 xo=644 CFLOW_NORMAL; in allowlist |
| [x] | `vsubcuw` | altivec | — | `VX_form` 4/1408 | kpx: `EXECUTE_VECTOR_ARITH(subcuw, V4SI, V4SI, V4SI, NONE)`; VX_form prim=4 xo=1408 CFLOW_NORMAL; in allowlist |
| [x] | `vsubfp` | altivec | — | `VX_form` 4/74 | kpx: `EXECUTE_VECTOR_ARITH(fsubs, V4SF, V4SF, V4SF, NONE)`; VX_form prim=4 xo=74 CFLOW_NORMAL; in allowlist |
| [x] | `vsubsbs` | altivec | — | `VX_form` 4/1792 | kpx: `EXECUTE_VECTOR_ARITH(sub, V16QI_SAT<int8>, V16QI_SAT<int8>, V16QI_SAT<int8>, NONE)`; VX_form prim=4 xo=1792 CFLOW_NORMAL; in allowlist |
| [x] | `vsubshs` | altivec | — | `VX_form` 4/1856 | kpx: `EXECUTE_VECTOR_ARITH(sub, V8HI_SAT<int16>, V8HI_SAT<int16>, V8HI_SAT<int16>, NONE)`; VX_form prim=4 xo=1856 CFLOW_NORMAL; in allowlist |
| [x] | `vsubsws` | altivec | — | `VX_form` 4/1920 | kpx: `EXECUTE_VECTOR_ARITH(sub_64, V4SI_SAT<int32>, V4SI_SAT<int32>, V4SI_SAT<int32>, NONE)`; VX_form prim=4 xo=1920 CFLOW_NORMAL; in allowlist |
| [x] | `vsububm` | altivec | — | `VX_form` 4/1024 | kpx: `EXECUTE_VECTOR_ARITH(sub, V16QI, V16QI, V16QI, NONE)`; VX_form prim=4 xo=1024 CFLOW_NORMAL; in allowlist |
| [x] | `vsububs` | altivec | — | `VX_form` 4/1536 | kpx: `EXECUTE_VECTOR_ARITH(sub, V16QI_SAT<uint8>, V16QI_SAT<uint8>, V16QI_SAT<uint8>, NONE)`; VX_form prim=4 xo=1536 CFLOW_NORMAL; in allowlist |
| [x] | `vsubuhm` | altivec | — | `VX_form` 4/1088 | kpx: `EXECUTE_VECTOR_ARITH(sub, V8HI, V8HI, V8HI, NONE)`; VX_form prim=4 xo=1088 CFLOW_NORMAL; in allowlist |
| [x] | `vsubuhs` | altivec | — | `VX_form` 4/1600 | kpx: `EXECUTE_VECTOR_ARITH(sub, V8HI_SAT<uint16>, V8HI_SAT<uint16>, V8HI_SAT<uint16>, NONE)`; VX_form prim=4 xo=1600 CFLOW_NORMAL; in allowlist |
| [x] | `vsubuwm` | altivec | — | `VX_form` 4/1152 | kpx: `EXECUTE_VECTOR_ARITH(sub, V4SI, V4SI, V4SI, NONE)`; VX_form prim=4 xo=1152 CFLOW_NORMAL; in allowlist |
| [x] | `vsubuws` | altivec | — | `VX_form` 4/1664 | kpx: `EXECUTE_VECTOR_ARITH(sub_64, V4SI_SAT<uint32>, V4SI_SAT<uint32>, V4SI_SAT<uint32>, NONE)`; VX_form prim=4 xo=1664 CFLOW_NORMAL; in allowlist |
| [x] | `vsum2sws` | altivec | — | `VX_form` 4/1672 | kpx: `EXECUTE_VECTOR_SUM(2, V4SI_SAT<int32>, V4SIs, V4SIs)`; VX_form prim=4 xo=1672 CFLOW_NORMAL; in allowlist |
| [x] | `vsum4sbs` | altivec | — | `VX_form` 4/1800 | kpx: `EXECUTE_VECTOR_SUM(4, V4SI_SAT<int32>, V16QIs, V4SIs)`; VX_form prim=4 xo=1800 CFLOW_NORMAL; in allowlist |
| [x] | `vsum4shs` | altivec | — | `VX_form` 4/1608 | kpx: `EXECUTE_VECTOR_SUM(4, V4SI_SAT<int32>, V8HIs, V4SIs)`; VX_form prim=4 xo=1608 CFLOW_NORMAL; in allowlist |
| [x] | `vsum4ubs` | altivec | — | `VX_form` 4/1544 | kpx: `EXECUTE_VECTOR_SUM(4, V4SI_SAT<uint32>, V16QI, V4SI)`; VX_form prim=4 xo=1544 CFLOW_NORMAL; in allowlist |
| [x] | `vsumsws` | altivec | — | `VX_form` 4/1928 | kpx: `EXECUTE_VECTOR_SUM(1, V4SI_SAT<int32>, V4SIs, V4SIs)`; VX_form prim=4 xo=1928 CFLOW_NORMAL; in allowlist |
| [x] | `vupkhpx` | altivec | — | `VX_form` 4/846 | kpx: `EXECUTE_1(vector_unpack_pixel, 0)`; VX_form prim=4 xo=846 CFLOW_NORMAL; in allowlist |
| [x] | `vupkhsb` | altivec | — | `VX_form` 4/526 | kpx: `EXECUTE_VECTOR_UNPACK(0, V8HIms, V16QIms)`; VX_form prim=4 xo=526 CFLOW_NORMAL; in allowlist |
| [x] | `vupkhsh` | altivec | — | `VX_form` 4/590 | kpx: `EXECUTE_VECTOR_UNPACK(0, V4SIs, V8HIms)`; VX_form prim=4 xo=590 CFLOW_NORMAL; in allowlist |
| [x] | `vupklpx` | altivec | — | `VX_form` 4/974 | kpx: `EXECUTE_1(vector_unpack_pixel, 1)`; VX_form prim=4 xo=974 CFLOW_NORMAL; in allowlist |
| [x] | `vupklsb` | altivec | — | `VX_form` 4/654 | kpx: `EXECUTE_VECTOR_UNPACK(1, V8HIms, V16QIms)`; VX_form prim=4 xo=654 CFLOW_NORMAL; in allowlist |
| [x] | `vupklsh` | altivec | — | `VX_form` 4/718 | kpx: `EXECUTE_VECTOR_UNPACK(1, V4SIs, V8HIms)`; VX_form prim=4 xo=718 CFLOW_NORMAL; in allowlist |
| [x] | `vxor` | altivec | — | `VX_form` 4/1220 | kpx: `EXECUTE_VECTOR_ARITH(xor_64, V2DI, V2DI, V2DI, NONE)`; VX_form prim=4 xo=1220 CFLOW_NORMAL; in allowlist |
| [x] | `b` | control | — | `I_form` 18/0 | kpx: `EXECUTE_BRANCH(PC, immediate_value<BO_MAKE(0,0,0,0)>, LI, AA_BIT_G, LK_BIT_G)`; I_form prim=18 xo=0 CFLOW_BRANCH; **ends_block**; in allowlist |
| [x] | `bc` | control | — | `B_form` 16/0 | kpx: `EXECUTE_BRANCH(PC, operand_BO, BD, AA_BIT_G, LK_BIT_G)`; B_form prim=16 xo=0 CFLOW_BRANCH; **ends_block**; in allowlist |
| [x] | `bcctr` | control | — | `XL_form` 19/528 | kpx: `EXECUTE_BRANCH(CTR, operand_BO, ZERO, AA_BIT_0, LK_BIT_G)`; XL_form prim=19 xo=528 CFLOW_BRANCH; **ends_block**; in allowlist |
| [x] | `bclr` | control | — | `XL_form` 19/16 | kpx: `EXECUTE_BRANCH(LR, operand_BO, ZERO, AA_BIT_0, LK_BIT_G)`; XL_form prim=19 xo=16 CFLOW_BRANCH; **ends_block**; in allowlist |
| [x] | `crand` | control | — | `XL_form` 19/257 | kpx: `EXECUTE_CR_OP(and)`; XL_form prim=19 xo=257 CFLOW_NORMAL; in allowlist |
| [x] | `crandc` | control | — | `XL_form` 19/129 | kpx: `EXECUTE_CR_OP(andc)`; XL_form prim=19 xo=129 CFLOW_NORMAL; in allowlist |
| [x] | `creqv` | control | — | `XL_form` 19/289 | kpx: `EXECUTE_CR_OP(eqv)`; XL_form prim=19 xo=289 CFLOW_NORMAL; in allowlist |
| [x] | `crnand` | control | — | `XL_form` 19/225 | kpx: `EXECUTE_CR_OP(nand)`; XL_form prim=19 xo=225 CFLOW_NORMAL; in allowlist |
| [x] | `crnor` | control | — | `XL_form` 19/33 | kpx: `EXECUTE_CR_OP(nor)`; XL_form prim=19 xo=33 CFLOW_NORMAL; in allowlist |
| [x] | `cror` | control | — | `XL_form` 19/449 | kpx: `EXECUTE_CR_OP(or)`; XL_form prim=19 xo=449 CFLOW_NORMAL; in allowlist |
| [x] | `crorc` | control | — | `XL_form` 19/417 | kpx: `EXECUTE_CR_OP(orc)`; XL_form prim=19 xo=417 CFLOW_NORMAL; in allowlist |
| [x] | `crxor` | control | — | `XL_form` 19/193 | kpx: `EXECUTE_CR_OP(xor)`; XL_form prim=19 xo=193 CFLOW_NORMAL; in allowlist |
| [x] | `isync` | control | — | `X_form` 19/150 | kpx: `EXECUTE_0(isync)`; X_form prim=19 xo=150 CFLOW_NORMAL; **ends_block**; in allowlist |
| [~] | `rfi` | control | — | `XL_form` 19/50 | kpx: `EXECUTE_0(rfi)`; XL_form prim=19 xo=50 CFLOW_JUMP; Block-end helper + ret.; **ends_block**; in allowlist |
| [x] | `sc` | control | — | `SC_form` 17/0 | kpx: `EXECUTE_0(syscall)`; SC_form prim=17 xo=0 CFLOW_NORMAL; **ends_block**; in allowlist |
| [x] | `fabs` | fp | — | `X_form` 63/264 | kpx: `EXECUTE_FP_ARITH(double, fabs, RD, RB, NONE, NONE, RC_BIT_G, false)`; X_form prim=63 xo=264 CFLOW_NORMAL; in allowlist |
| [x] | `fadd` | fp | — | `A_form` 63/21 | kpx: `EXECUTE_FP_ARITH(double, fadd, RD, RA, RB, NONE, RC_BIT_G, true)`; A_form prim=63 xo=21 CFLOW_NORMAL; in allowlist |
| [x] | `fadds` | fp | — | `A_form` 59/21 | kpx: `EXECUTE_FP_ARITH(float, fadd, RD, RA, RB, NONE, RC_BIT_G, true)`; A_form prim=59 xo=21 CFLOW_NORMAL; in allowlist |
| [x] | `fcmpo` | fp | — | `X_form` 63/32 | kpx: `EXECUTE_1(fp_compare, true)`; X_form prim=63 xo=32 CFLOW_NORMAL; in allowlist |
| [x] | `fcmpu` | fp | — | `X_form` 63/0 | kpx: `EXECUTE_1(fp_compare, false)`; X_form prim=63 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `fctiw` | fp | — | `X_form` 63/14 | kpx: `EXECUTE_2(fp_int_convert, operand_FPSCR_RN, RC_BIT_G)`; X_form prim=63 xo=14 CFLOW_NORMAL; in allowlist |
| [x] | `fctiwz` | fp | — | `X_form` 63/15 | kpx: `EXECUTE_2(fp_int_convert, operand_ONE, RC_BIT_G)`; X_form prim=63 xo=15 CFLOW_NORMAL; in allowlist |
| [x] | `fdiv` | fp | — | `A_form` 63/18 | kpx: `EXECUTE_FP_ARITH(double, fdiv, RD, RA, RB, NONE, RC_BIT_G, true)`; A_form prim=63 xo=18 CFLOW_NORMAL; in allowlist |
| [x] | `fdivs` | fp | — | `A_form` 59/18 | kpx: `EXECUTE_FP_ARITH(float, fdiv, RD, RA, RB, NONE, RC_BIT_G, true)`; A_form prim=59 xo=18 CFLOW_NORMAL; in allowlist |
| [x] | `fmadd` | fp | — | `A_form` 63/29 | kpx: `EXECUTE_FP_ARITH(double, fmadd, RD, RA, RC, RB, RC_BIT_G, true)`; A_form prim=63 xo=29 CFLOW_NORMAL; in allowlist |
| [x] | `fmadds` | fp | — | `A_form` 59/29 | kpx: `EXECUTE_FP_ARITH(float, fmadd, RD, RA, RC, RB, RC_BIT_G, true)`; A_form prim=59 xo=29 CFLOW_NORMAL; in allowlist |
| [x] | `fmr` | fp | — | `X_form` 63/72 | kpx: `EXECUTE_FP_ARITH(double, fnop, RD, RB, NONE, NONE, RC_BIT_G, false)`; X_form prim=63 xo=72 CFLOW_NORMAL; in allowlist |
| [x] | `fmsub` | fp | — | `A_form` 63/28 | kpx: `EXECUTE_FP_ARITH(double, fmsub, RD, RA, RC, RB, RC_BIT_G, true)`; A_form prim=63 xo=28 CFLOW_NORMAL; in allowlist |
| [x] | `fmsubs` | fp | — | `A_form` 59/28 | kpx: `EXECUTE_FP_ARITH(float, fmsub, RD, RA, RC, RB, RC_BIT_G, true)`; A_form prim=59 xo=28 CFLOW_NORMAL; in allowlist |
| [x] | `fmul` | fp | — | `A_form` 63/25 | kpx: `EXECUTE_FP_ARITH(double, fmul, RD, RA, RC, NONE, RC_BIT_G, true)`; A_form prim=63 xo=25 CFLOW_NORMAL; in allowlist |
| [x] | `fmuls` | fp | — | `A_form` 59/25 | kpx: `EXECUTE_FP_ARITH(float, fmul, RD, RA, RC, NONE, RC_BIT_G, true)`; A_form prim=59 xo=25 CFLOW_NORMAL; in allowlist |
| [x] | `fnabs` | fp | — | `X_form` 63/136 | kpx: `EXECUTE_FP_ARITH(double, fnabs, RD, RB, NONE, NONE, RC_BIT_G, false)`; X_form prim=63 xo=136 CFLOW_NORMAL; in allowlist |
| [x] | `fneg` | fp | — | `X_form` 63/40 | kpx: `EXECUTE_FP_ARITH(double, fneg, RD, RB, NONE, NONE, RC_BIT_G, false)`; X_form prim=63 xo=40 CFLOW_NORMAL; in allowlist |
| [x] | `fnmadd` | fp | — | `A_form` 63/31 | kpx: `EXECUTE_FP_ARITH(double, fnmadd, RD, RA, RC, RB, RC_BIT_G, true)`; A_form prim=63 xo=31 CFLOW_NORMAL; in allowlist |
| [x] | `fnmadds` | fp | — | `A_form` 59/31 | kpx: `EXECUTE_FP_ARITH(double, fnmadds, RD, RA, RC, RB, RC_BIT_G, true)`; A_form prim=59 xo=31 CFLOW_NORMAL; in allowlist |
| [x] | `fnmsub` | fp | — | `A_form` 63/30 | kpx: `EXECUTE_FP_ARITH(double, fnmsub, RD, RA, RC, RB, RC_BIT_G, true)`; A_form prim=63 xo=30 CFLOW_NORMAL; in allowlist |
| [x] | `fnmsubs` | fp | — | `A_form` 59/30 | kpx: `EXECUTE_FP_ARITH(double, fnmsubs, RD, RA, RC, RB, RC_BIT_G, true)`; A_form prim=59 xo=30 CFLOW_NORMAL; in allowlist |
| [x] | `fres` | fp | — | `A_form` 59/24 | kpx: `EXECUTE_FP_ARITH(double, fres, RD, RB, NONE, NONE, RC_BIT_G, true)`; A_form prim=59 xo=24 CFLOW_NORMAL; in allowlist |
| [x] | `frsp` | fp | — | `X_form` 63/12 | kpx: `EXECUTE_1(fp_round, RC_BIT_G)`; X_form prim=63 xo=12 CFLOW_NORMAL; in allowlist |
| [x] | `frsqrte` | fp | — | `A_form` 63/26 | kpx: `EXECUTE_FP_ARITH(double, frsqrte, RD, RB, NONE, NONE, RC_BIT_G, true)`; A_form prim=63 xo=26 CFLOW_NORMAL; in allowlist |
| [x] | `fsel` | fp | — | `A_form` 63/23 | kpx: `EXECUTE_FP_ARITH(double, fsel, RD, RA, RC, RB, RC_BIT_G, false)`; A_form prim=63 xo=23 CFLOW_NORMAL; in allowlist |
| [x] | `fsub` | fp | — | `A_form` 63/20 | kpx: `EXECUTE_FP_ARITH(double, fsub, RD, RA, RB, NONE, RC_BIT_G, true)`; A_form prim=63 xo=20 CFLOW_NORMAL; in allowlist |
| [x] | `fsubs` | fp | — | `A_form` 59/20 | kpx: `EXECUTE_FP_ARITH(float, fsub, RD, RA, RB, NONE, RC_BIT_G, true)`; A_form prim=59 xo=20 CFLOW_NORMAL; in allowlist |
| [x] | `lfd` | fp | — | `D_form` 50/0 | kpx: `EXECUTE_FP_LOADSTORE(RA_or_0, D, true, true, false)`; D_form prim=50 xo=0 CFLOW_NORMAL; in allowlist |
| [~] | `lfdu` | fp | — | `D_form` 51/0 | kpx: `EXECUTE_FP_LOADSTORE(RA, D, true, true, true)`; D_form prim=51 xo=0 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [~] | `lfdux` | fp | — | `X_form` 31/631 | kpx: `EXECUTE_FP_LOADSTORE(RA, RB, true, true, true)`; X_form prim=31 xo=631 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [x] | `lfdx` | fp | — | `X_form` 31/599 | kpx: `EXECUTE_FP_LOADSTORE(RA_or_0, RB, true, true, false)`; X_form prim=31 xo=599 CFLOW_NORMAL; in allowlist |
| [x] | `lfs` | fp | — | `D_form` 48/0 | kpx: `EXECUTE_FP_LOADSTORE(RA_or_0, D, true, false, false)`; D_form prim=48 xo=0 CFLOW_NORMAL; in allowlist |
| [~] | `lfsu` | fp | — | `D_form` 49/0 | kpx: `EXECUTE_FP_LOADSTORE(RA, D, true, false, true)`; D_form prim=49 xo=0 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [~] | `lfsux` | fp | — | `X_form` 31/567 | kpx: `EXECUTE_FP_LOADSTORE(RA, RB, true, false, true)`; X_form prim=31 xo=567 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [x] | `lfsx` | fp | — | `X_form` 31/535 | kpx: `EXECUTE_FP_LOADSTORE(RA_or_0, RB, true, false, false)`; X_form prim=31 xo=535 CFLOW_NORMAL; in allowlist |
| [x] | `mcrfs` | fp | — | `X_form` 63/64 | kpx: `EXECUTE_0(mcrfs)`; X_form prim=63 xo=64 CFLOW_NORMAL; in allowlist |
| [x] | `mffs` | fp | — | `X_form` 63/583 | kpx: `EXECUTE_1(mffs, RC_BIT_G)`; X_form prim=63 xo=583 CFLOW_NORMAL; in allowlist |
| [x] | `mtfsb0` | fp | — | `X_form` 63/70 | kpx: `EXECUTE_2(mtfsb, immediate_value<0>, RC_BIT_G)`; X_form prim=63 xo=70 CFLOW_NORMAL; in allowlist |
| [x] | `mtfsb1` | fp | — | `X_form` 63/38 | kpx: `EXECUTE_2(mtfsb, immediate_value<1>, RC_BIT_G)`; X_form prim=63 xo=38 CFLOW_NORMAL; in allowlist |
| [x] | `mtfsf` | fp | — | `XFL_form` 63/711 | kpx: `EXECUTE_3(mtfsf, operand_FM, operand_fp_dw_RB, RC_BIT_G)`; XFL_form prim=63 xo=711 CFLOW_NORMAL; in allowlist |
| [x] | `mtfsfi` | fp | — | `X_form` 63/134 | kpx: `EXECUTE_2(mtfsfi, operand_IMM, RC_BIT_G)`; X_form prim=63 xo=134 CFLOW_NORMAL; in allowlist |
| [x] | `stfd` | fp | — | `D_form` 54/0 | kpx: `EXECUTE_FP_LOADSTORE(RA_or_0, D, false, true, false)`; D_form prim=54 xo=0 CFLOW_NORMAL; in allowlist |
| [~] | `stfdu` | fp | — | `D_form` 55/0 | kpx: `EXECUTE_FP_LOADSTORE(RA, D, false, true, true)`; D_form prim=55 xo=0 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [~] | `stfdux` | fp | — | `X_form` 31/759 | kpx: `EXECUTE_FP_LOADSTORE(RA, RB, false, true, true)`; X_form prim=31 xo=759 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [x] | `stfdx` | fp | — | `X_form` 31/727 | kpx: `EXECUTE_FP_LOADSTORE(RA_or_0, RB, false, true, false)`; X_form prim=31 xo=727 CFLOW_NORMAL; in allowlist |
| [x] | `stfs` | fp | — | `D_form` 52/0 | kpx: `EXECUTE_FP_LOADSTORE(RA_or_0, D, false, false, false)`; D_form prim=52 xo=0 CFLOW_NORMAL; in allowlist |
| [~] | `stfsu` | fp | — | `D_form` 53/0 | kpx: `EXECUTE_FP_LOADSTORE(RA, D, false, false, true)`; D_form prim=53 xo=0 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [~] | `stfsux` | fp | — | `X_form` 31/695 | kpx: `EXECUTE_FP_LOADSTORE(RA, RB, false, false, true)`; X_form prim=31 xo=695 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [x] | `stfsx` | fp | — | `X_form` 31/663 | kpx: `EXECUTE_FP_LOADSTORE(RA_or_0, RB, false, false, false)`; X_form prim=31 xo=663 CFLOW_NORMAL; in allowlist |
| [x] | `add` | integer | — | `XO_form` 31/266 | kpx: `EXECUTE_ADDITION(RA, RB, NONE, CA_BIT_0, OE_BIT_G, RC_BIT_G)`; XO_form prim=31 xo=266 CFLOW_NORMAL; in allowlist |
| [x] | `addc` | integer | — | `XO_form` 31/10 | kpx: `EXECUTE_ADDITION(RA, RB, NONE, CA_BIT_1, OE_BIT_G, RC_BIT_G)`; XO_form prim=31 xo=10 CFLOW_NORMAL; in allowlist |
| [x] | `adde` | integer | — | `XO_form` 31/138 | kpx: `EXECUTE_ADDITION(RA, RB, XER_CA, CA_BIT_1, OE_BIT_G, RC_BIT_G)`; XO_form prim=31 xo=138 CFLOW_NORMAL; in allowlist |
| [x] | `addi` | integer | — | `D_form` 14/0 | kpx: `EXECUTE_ADDITION(RA_or_0, SIMM, NONE, CA_BIT_0, OE_BIT_0, RC_BIT_0)`; D_form prim=14 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `addic` | integer | — | `D_form` 12/0 | kpx: `EXECUTE_ADDITION(RA, SIMM, NONE, CA_BIT_1, OE_BIT_0, RC_BIT_0)`; D_form prim=12 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `addic.` | integer | — | `D_form` 13/0 | kpx: `EXECUTE_ADDITION(RA, SIMM, NONE, CA_BIT_1, OE_BIT_0, RC_BIT_1)`; D_form prim=13 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `addis` | integer | — | `D_form` 15/0 | kpx: `EXECUTE_ADDITION(RA_or_0, SIMM_shifted, NONE, CA_BIT_0, OE_BIT_0, RC_BIT_0)`; D_form prim=15 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `addme` | integer | — | `XO_form` 31/234 | kpx: `EXECUTE_ADDITION(RA, MINUS_ONE, XER_CA, CA_BIT_1, OE_BIT_G, RC_BIT_G)`; XO_form prim=31 xo=234 CFLOW_NORMAL; in allowlist |
| [x] | `addze` | integer | — | `XO_form` 31/202 | kpx: `EXECUTE_ADDITION(RA, ZERO, XER_CA, CA_BIT_1, OE_BIT_G, RC_BIT_G)`; XO_form prim=31 xo=202 CFLOW_NORMAL; in allowlist |
| [x] | `and` | integer | — | `X_form` 31/28 | kpx: `EXECUTE_GENERIC_ARITH(and, RA, RS, RB, NONE, OE_BIT_0, RC_BIT_G)`; X_form prim=31 xo=28 CFLOW_NORMAL; in allowlist |
| [x] | `andc` | integer | — | `X_form` 31/60 | kpx: `EXECUTE_GENERIC_ARITH(andc, RA, RS, RB, NONE, OE_BIT_0, RC_BIT_G)`; X_form prim=31 xo=60 CFLOW_NORMAL; in allowlist |
| [x] | `andi.` | integer | — | `D_form` 28/0 | kpx: `EXECUTE_GENERIC_ARITH(and, RA, RS, UIMM, NONE, OE_BIT_0, RC_BIT_1)`; D_form prim=28 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `andis.` | integer | — | `D_form` 29/0 | kpx: `EXECUTE_GENERIC_ARITH(and, RA, RS, UIMM_shifted, NONE, OE_BIT_0, RC_BIT_1)`; D_form prim=29 xo=0 CFLOW_NORMAL; in allowlist |
| [~] | `cmp` | integer | — | `X_form` 31/0 | kpx: `EXECUTE_COMPARE(RB, int32)`; X_form prim=31 xo=0 CFLOW_NORMAL; L=0 only.; allowlist ok=L=0; fail=L=1; in allowlist |
| [~] | `cmpi` | integer | — | `D_form` 11/0 | kpx: `EXECUTE_COMPARE(SIMM, int32)`; D_form prim=11 xo=0 CFLOW_NORMAL; L=0 only.; allowlist ok=L=0; fail=L=1; in allowlist |
| [~] | `cmpl` | integer | — | `X_form` 31/32 | kpx: `EXECUTE_COMPARE(RB, uint32)`; X_form prim=31 xo=32 CFLOW_NORMAL; L=0 only.; allowlist ok=L=0; fail=L=1; in allowlist |
| [~] | `cmpli` | integer | — | `D_form` 10/0 | kpx: `EXECUTE_COMPARE(UIMM, uint32)`; D_form prim=10 xo=0 CFLOW_NORMAL; L=0 only.; allowlist ok=L=0; fail=L=1; in allowlist |
| [x] | `cntlzw` | integer | — | `X_form` 31/26 | kpx: `EXECUTE_GENERIC_ARITH(cntlzw, RA, RS, NONE, NONE, OE_BIT_0, RC_BIT_G)`; X_form prim=31 xo=26 CFLOW_NORMAL; in allowlist |
| [x] | `dcba` | integer | — | `X_form` 31/758 | kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=758 CFLOW_NORMAL; kpx nop.; in allowlist |
| [x] | `dcbf` | integer | — | `X_form` 31/86 | kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=86 CFLOW_NORMAL; kpx nop.; in allowlist |
| [x] | `dcbi` | integer | — | `X_form` 31/470 | kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=470 CFLOW_NORMAL; kpx nop.; in allowlist |
| [x] | `dcbst` | integer | — | `X_form` 31/54 | kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=54 CFLOW_NORMAL; kpx nop.; in allowlist |
| [x] | `dcbt` | integer | — | `X_form` 31/278 | kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=278 CFLOW_NORMAL; kpx nop.; in allowlist |
| [x] | `dcbtst` | integer | — | `X_form` 31/246 | kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=246 CFLOW_NORMAL; kpx nop.; in allowlist |
| [x] | `dcbz` | integer | — | `X_form` 31/1014 | kpx: `EXECUTE_2(dcbz, operand_RA_or_0, operand_RB)`; X_form prim=31 xo=1014 CFLOW_NORMAL; in allowlist |
| [x] | `divw` | integer | — | `XO_form` 31/491 | kpx: `EXECUTE_3(divide, true, OE_BIT_G, RC_BIT_G)`; XO_form prim=31 xo=491 CFLOW_NORMAL; in allowlist |
| [x] | `divwu` | integer | — | `XO_form` 31/459 | kpx: `EXECUTE_3(divide, false, OE_BIT_G, RC_BIT_G)`; XO_form prim=31 xo=459 CFLOW_NORMAL; in allowlist |
| [x] | `dss` | integer | — | `X_form` 31/822 | kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=822 CFLOW_NORMAL; kpx nop.; in allowlist |
| [x] | `dst` | integer | — | `X_form` 31/342 | kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=342 CFLOW_NORMAL; kpx nop.; in allowlist |
| [x] | `dstst` | integer | — | `X_form` 31/374 | kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=374 CFLOW_NORMAL; kpx nop.; in allowlist |
| [-] | `eciwx` | integer | exclude | `X_form` 31/310 | kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=310 CFLOW_NORMAL; External control; no device model.; not in `nw_jit_op_supported` |
| [-] | `ecowx` | integer | exclude | `X_form` 31/438 | kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=438 CFLOW_NORMAL; External control; no device model.; not in `nw_jit_op_supported` |
| [x] | `eieio` | integer | — | `X_form` 31/854 | kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=854 CFLOW_NORMAL; in allowlist |
| [x] | `eqv` | integer | — | `X_form` 31/284 | kpx: `EXECUTE_GENERIC_ARITH(eqv, RA, RS, RB, NONE, OE_BIT_0, RC_BIT_G)`; X_form prim=31 xo=284 CFLOW_NORMAL; in allowlist |
| [x] | `extsb` | integer | — | `X_form` 31/954 | kpx: `EXECUTE_GENERIC_ARITH(sign_extend_8_32, RA, RS, NONE, NONE, OE_BIT_0, RC_BIT_G)`; X_form prim=31 xo=954 CFLOW_NORMAL; in allowlist |
| [x] | `extsh` | integer | — | `X_form` 31/922 | kpx: `EXECUTE_GENERIC_ARITH(sign_extend_16_32, RA, RS, NONE, NONE, OE_BIT_0, RC_BIT_G)`; X_form prim=31 xo=922 CFLOW_NORMAL; in allowlist |
| [~] | `icbi` | integer | — | `X_form` 31/982 | kpx: `EXECUTE_2(icbi, operand_RA_or_0, operand_RB)`; X_form prim=31 xo=982 CFLOW_NORMAL; ends_block.; **ends_block**; in allowlist |
| [x] | `mfvscr` | integer | — | `VX_form` 4/1540 | kpx: `EXECUTE_0(mfvscr)`; VX_form prim=4 xo=1540 CFLOW_NORMAL; in allowlist |
| [x] | `mtvscr` | integer | — | `VX_form` 4/1604 | kpx: `EXECUTE_0(mtvscr)`; VX_form prim=4 xo=1604 CFLOW_NORMAL; in allowlist |
| [x] | `mulhw` | integer | — | `XO_form` 31/75 | kpx: `EXECUTE_4(multiply, true, true, OE_BIT_0, RC_BIT_G)`; XO_form prim=31 xo=75 CFLOW_NORMAL; in allowlist |
| [x] | `mulhwu` | integer | — | `XO_form` 31/11 | kpx: `EXECUTE_4(multiply, true, false, OE_BIT_0, RC_BIT_G)`; XO_form prim=31 xo=11 CFLOW_NORMAL; in allowlist |
| [x] | `mulli` | integer | — | `D_form` 7/0 | kpx: `EXECUTE_GENERIC_ARITH(smul, RD, RA, SIMM, NONE, OE_BIT_0, RC_BIT_0)`; D_form prim=7 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `mullw` | integer | — | `XO_form` 31/235 | kpx: `EXECUTE_4(multiply, false, true, OE_BIT_G, RC_BIT_G)`; XO_form prim=31 xo=235 CFLOW_NORMAL; in allowlist |
| [x] | `nand` | integer | — | `X_form` 31/476 | kpx: `EXECUTE_GENERIC_ARITH(nand, RA, RS, RB, NONE, OE_BIT_0, RC_BIT_G)`; X_form prim=31 xo=476 CFLOW_NORMAL; in allowlist |
| [x] | `neg` | integer | — | `XO_form` 31/104 | kpx: `EXECUTE_GENERIC_ARITH(neg, RD, RA, NONE, NONE, OE_BIT_G, RC_BIT_G)`; XO_form prim=31 xo=104 CFLOW_NORMAL; in allowlist |
| [x] | `nor` | integer | — | `XO_form` 31/124 | kpx: `EXECUTE_GENERIC_ARITH(nor, RA, RS, RB, NONE, OE_BIT_0, RC_BIT_G)`; XO_form prim=31 xo=124 CFLOW_NORMAL; in allowlist |
| [x] | `or` | integer | — | `XO_form` 31/444 | kpx: `EXECUTE_GENERIC_ARITH(or, RA, RS, RB, NONE, OE_BIT_0, RC_BIT_G)`; XO_form prim=31 xo=444 CFLOW_NORMAL; in allowlist |
| [x] | `orc` | integer | — | `XO_form` 31/412 | kpx: `EXECUTE_GENERIC_ARITH(orc, RA, RS, RB, NONE, OE_BIT_0, RC_BIT_G)`; XO_form prim=31 xo=412 CFLOW_NORMAL; in allowlist |
| [x] | `ori` | integer | — | `D_form` 24/0 | kpx: `EXECUTE_GENERIC_ARITH(or, RA, RS, UIMM, NONE, OE_BIT_0, RC_BIT_0)`; D_form prim=24 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `oris` | integer | — | `D_form` 25/0 | kpx: `EXECUTE_GENERIC_ARITH(or, RA, RS, UIMM_shifted, NONE, OE_BIT_0, RC_BIT_0)`; D_form prim=25 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `rlwimi` | integer | — | `M_form` 20/0 | kpx: `EXECUTE_3(rlwimi, operand_SH, operand_MASK, RC_BIT_G)`; M_form prim=20 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `rlwinm` | integer | — | `M_form` 21/0 | kpx: `EXECUTE_GENERIC_ARITH(ppc_rlwinm, RA, RS, SH, MASK, OE_BIT_0, RC_BIT_G)`; M_form prim=21 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `rlwnm` | integer | — | `M_form` 23/0 | kpx: `EXECUTE_GENERIC_ARITH(ppc_rlwnm, RA, RS, RB, MASK, OE_BIT_0, RC_BIT_G)`; M_form prim=23 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `slw` | integer | — | `X_form` 31/24 | kpx: `EXECUTE_SHIFT(shll, RA, RS, RB, andi<0x3f>, CA_BIT_0, RC_BIT_G)`; X_form prim=31 xo=24 CFLOW_NORMAL; in allowlist |
| [x] | `sraw` | integer | — | `X_form` 31/792 | kpx: `EXECUTE_SHIFT(shra, RA, RS, RB, andi<0x3f>, CA_BIT_1, RC_BIT_G)`; X_form prim=31 xo=792 CFLOW_NORMAL; in allowlist |
| [x] | `srawi` | integer | — | `X_form` 31/824 | kpx: `EXECUTE_SHIFT(shra, RA, RS, SH, andi<0x1f>, CA_BIT_1, RC_BIT_G)`; X_form prim=31 xo=824 CFLOW_NORMAL; in allowlist |
| [x] | `srw` | integer | — | `X_form` 31/536 | kpx: `EXECUTE_SHIFT(shrl, RA, RS, RB, andi<0x3f>, CA_BIT_0, RC_BIT_G)`; X_form prim=31 xo=536 CFLOW_NORMAL; in allowlist |
| [x] | `subf` | integer | — | `XO_form` 31/40 | kpx: `EXECUTE_ADDITION(RA_compl, RB, ONE, CA_BIT_0, OE_BIT_G, RC_BIT_G)`; XO_form prim=31 xo=40 CFLOW_NORMAL; in allowlist |
| [x] | `subfc` | integer | — | `XO_form` 31/8 | kpx: `EXECUTE_ADDITION(RA_compl, RB, ONE, CA_BIT_1, OE_BIT_G, RC_BIT_G)`; XO_form prim=31 xo=8 CFLOW_NORMAL; in allowlist |
| [x] | `subfe` | integer | — | `XO_form` 31/136 | kpx: `EXECUTE_ADDITION(RA_compl, RB, XER_CA, CA_BIT_1, OE_BIT_G, RC_BIT_G)`; XO_form prim=31 xo=136 CFLOW_NORMAL; in allowlist |
| [x] | `subfic` | integer | — | `D_form` 8/0 | kpx: `EXECUTE_ADDITION(RA_compl, SIMM, ONE, CA_BIT_1, OE_BIT_0, RC_BIT_0)`; D_form prim=8 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `subfme` | integer | — | `XO_form` 31/232 | kpx: `EXECUTE_ADDITION(RA_compl, XER_CA, MINUS_ONE, CA_BIT_1, OE_BIT_G, RC_BIT_G)`; XO_form prim=31 xo=232 CFLOW_NORMAL; in allowlist |
| [x] | `subfze` | integer | — | `XO_form` 31/200 | kpx: `EXECUTE_ADDITION(RA_compl, XER_CA, ZERO, CA_BIT_1, OE_BIT_G, RC_BIT_G)`; XO_form prim=31 xo=200 CFLOW_NORMAL; in allowlist |
| [x] | `sync` | integer | — | `X_form` 31/598 | kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=598 CFLOW_NORMAL; in allowlist |
| [~] | `tlbia` | integer | — | `X_form` 31/370 | kpx: `EXECUTE_0(tlbia)`; X_form prim=31 xo=370 CFLOW_NORMAL; ends_block.; in allowlist |
| [~] | `tlbie` | integer | — | `X_form` 31/306 | kpx: `EXECUTE_0(tlbie)`; X_form prim=31 xo=306 CFLOW_NORMAL; ends_block.; **ends_block**; in allowlist |
| [x] | `tlbsync` | integer | — | `X_form` 31/566 | kpx: `EXECUTE_0(tlbsync)`; X_form prim=31 xo=566 CFLOW_NORMAL; in allowlist |
| [x] | `tw` | integer | — | `X_form` 31/4 | kpx: `EXECUTE_0(trap)`; X_form prim=31 xo=4 CFLOW_TRAP; in allowlist |
| [x] | `twi` | integer | — | `D_form` 3/0 | kpx: `EXECUTE_0(trap)`; D_form prim=3 xo=0 CFLOW_TRAP; in allowlist |
| [x] | `xor` | integer | — | `X_form` 31/316 | kpx: `EXECUTE_GENERIC_ARITH(xor, RA, RS, RB, NONE, OE_BIT_0, RC_BIT_G)`; X_form prim=31 xo=316 CFLOW_NORMAL; in allowlist |
| [x] | `xori` | integer | — | `D_form` 26/0 | kpx: `EXECUTE_GENERIC_ARITH(xor, RA, RS, UIMM, NONE, OE_BIT_0, RC_BIT_0)`; D_form prim=26 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `xoris` | integer | — | `D_form` 27/0 | kpx: `EXECUTE_GENERIC_ARITH(xor, RA, RS, UIMM_shifted, NONE, OE_BIT_0, RC_BIT_0)`; D_form prim=27 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `lbz` | mem | — | `D_form` 34/0 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, D, true, 1, false, false)`; D_form prim=34 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `lbzu` | mem | — | `D_form` 35/0 | kpx: `EXECUTE_LOADSTORE(nop, RA, D, true, 1, true, false)`; D_form prim=35 xo=0 CFLOW_NORMAL; in allowlist |
| [~] | `lbzux` | mem | — | `X_form` 31/119 | kpx: `EXECUTE_LOADSTORE(nop, RA, RB, true, 1, true, false)`; X_form prim=31 xo=119 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [x] | `lbzx` | mem | — | `X_form` 31/87 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, RB, true, 1, false, false)`; X_form prim=31 xo=87 CFLOW_NORMAL; in allowlist |
| [x] | `lha` | mem | — | `D_form` 42/0 | kpx: `EXECUTE_LOADSTORE(sign_extend_16_32, RA_or_0, D, true, 2, false, false)`; D_form prim=42 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `lhau` | mem | — | `D_form` 43/0 | kpx: `EXECUTE_LOADSTORE(sign_extend_16_32, RA, D, true, 2, true, false)`; D_form prim=43 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `lhaux` | mem | — | `X_form` 31/375 | kpx: `EXECUTE_LOADSTORE(sign_extend_16_32, RA, RB, true, 2, true, false)`; X_form prim=31 xo=375 CFLOW_NORMAL; in allowlist |
| [x] | `lhax` | mem | — | `X_form` 31/343 | kpx: `EXECUTE_LOADSTORE(sign_extend_16_32, RA_or_0, RB, true, 2, false, false)`; X_form prim=31 xo=343 CFLOW_NORMAL; in allowlist |
| [x] | `lhbrx` | mem | — | `X_form` 31/790 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, RB, true, 2, false, true)`; X_form prim=31 xo=790 CFLOW_NORMAL; in allowlist |
| [x] | `lhz` | mem | — | `D_form` 40/0 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, D, true, 2, false, false)`; D_form prim=40 xo=0 CFLOW_NORMAL; in allowlist |
| [~] | `lhzu` | mem | — | `D_form` 41/0 | kpx: `EXECUTE_LOADSTORE(nop, RA, D, true, 2, true, false)`; D_form prim=41 xo=0 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [~] | `lhzux` | mem | — | `X_form` 31/311 | kpx: `EXECUTE_LOADSTORE(nop, RA, RB, true, 2, true, false)`; X_form prim=31 xo=311 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [x] | `lhzx` | mem | — | `X_form` 31/279 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, RB, true, 2, false, false)`; X_form prim=31 xo=279 CFLOW_NORMAL; in allowlist |
| [x] | `lmw` | mem | — | `D_form` 46/0 | kpx: `EXECUTE_LOADSTORE_MULTIPLE(RA_or_0, D, true)`; D_form prim=46 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `lswi` | mem | — | `X_form` 31/597 | kpx: `EXECUTE_LOAD_STRING(RA_or_0, true, NB)`; X_form prim=31 xo=597 CFLOW_NORMAL; in allowlist |
| [x] | `lswx` | mem | — | `X_form` 31/533 | kpx: `EXECUTE_LOAD_STRING(RA_or_0, false, XER_COUNT)`; X_form prim=31 xo=533 CFLOW_NORMAL; in allowlist |
| [x] | `lwarx` | mem | — | `X_form` 31/20 | kpx: `EXECUTE_1(lwarx, operand_RA_or_0)`; X_form prim=31 xo=20 CFLOW_NORMAL; in allowlist |
| [x] | `lwbrx` | mem | — | `X_form` 31/534 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, RB, true, 4, false, true)`; X_form prim=31 xo=534 CFLOW_NORMAL; in allowlist |
| [x] | `lwz` | mem | — | `D_form` 32/0 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, D, true, 4, false, false)`; D_form prim=32 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `lwzu` | mem | — | `D_form` 33/0 | kpx: `EXECUTE_LOADSTORE(nop, RA, D, true, 4, true, false)`; D_form prim=33 xo=0 CFLOW_NORMAL; in allowlist |
| [~] | `lwzux` | mem | — | `X_form` 31/55 | kpx: `EXECUTE_LOADSTORE(nop, RA, RB, true, 4, true, false)`; X_form prim=31 xo=55 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [x] | `lwzx` | mem | — | `X_form` 31/23 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, RB, true, 4, false, false)`; X_form prim=31 xo=23 CFLOW_NORMAL; in allowlist |
| [x] | `stb` | mem | — | `D_form` 38/0 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, D, false, 1, false, false)`; D_form prim=38 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `stbu` | mem | — | `D_form` 39/0 | kpx: `EXECUTE_LOADSTORE(nop, RA, D, false, 1, true, false)`; D_form prim=39 xo=0 CFLOW_NORMAL; in allowlist |
| [~] | `stbux` | mem | — | `X_form` 31/247 | kpx: `EXECUTE_LOADSTORE(nop, RA, RB, false, 1, true, false)`; X_form prim=31 xo=247 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [x] | `stbx` | mem | — | `X_form` 31/215 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, RB, false, 1, false, false)`; X_form prim=31 xo=215 CFLOW_NORMAL; in allowlist |
| [x] | `sth` | mem | — | `D_form` 44/0 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, D, false, 2, false, false)`; D_form prim=44 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `sthbrx` | mem | — | `X_form` 31/918 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, RB, false, 2, false, true)`; X_form prim=31 xo=918 CFLOW_NORMAL; in allowlist |
| [x] | `sthu` | mem | — | `D_form` 45/0 | kpx: `EXECUTE_LOADSTORE(nop, RA, D, false, 2, true, false)`; D_form prim=45 xo=0 CFLOW_NORMAL; in allowlist |
| [~] | `sthux` | mem | — | `X_form` 31/439 | kpx: `EXECUTE_LOADSTORE(nop, RA, RB, false, 2, true, false)`; X_form prim=31 xo=439 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [x] | `sthx` | mem | — | `X_form` 31/407 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, RB, false, 2, false, false)`; X_form prim=31 xo=407 CFLOW_NORMAL; in allowlist |
| [x] | `stmw` | mem | — | `D_form` 47/0 | kpx: `EXECUTE_LOADSTORE_MULTIPLE(RA_or_0, D, false)`; D_form prim=47 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `stswi` | mem | — | `X_form` 31/725 | kpx: `EXECUTE_STORE_STRING(RA_or_0, true, NB)`; X_form prim=31 xo=725 CFLOW_NORMAL; in allowlist |
| [x] | `stswx` | mem | — | `X_form` 31/661 | kpx: `EXECUTE_STORE_STRING(RA_or_0, false, XER_COUNT)`; X_form prim=31 xo=661 CFLOW_NORMAL; in allowlist |
| [x] | `stw` | mem | — | `D_form` 36/0 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, D, false, 4, false, false)`; D_form prim=36 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `stwbrx` | mem | — | `X_form` 31/662 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, RB, false, 4, false, true)`; X_form prim=31 xo=662 CFLOW_NORMAL; in allowlist |
| [x] | `stwcx.` | mem | — | `X_form` 31/150 | kpx: `EXECUTE_1(stwcx, operand_RA_or_0)`; X_form prim=31 xo=150 CFLOW_NORMAL; in allowlist |
| [x] | `stwu` | mem | — | `D_form` 37/0 | kpx: `EXECUTE_LOADSTORE(nop, RA, D, false, 4, true, false)`; D_form prim=37 xo=0 CFLOW_NORMAL; in allowlist |
| [~] | `stwux` | mem | — | `X_form` 31/183 | kpx: `EXECUTE_LOADSTORE(nop, RA, RB, false, 4, true, false)`; X_form prim=31 xo=183 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [x] | `stwx` | mem | — | `X_form` 31/151 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, RB, false, 4, false, false)`; X_form prim=31 xo=151 CFLOW_NORMAL; in allowlist |
| [-] | `invalid` | none | exclude | `INVALID_form` 0/0 | kpx: `EXECUTE_0(illegal)`; INVALID_form prim=0 xo=0 CFLOW_TRAP; Not a real insn. Prim=6 skip noise is usually this class.; not in `nw_jit_op_supported` |
| [x] | `mcrf` | spr | — | `XL_form` 19/0 | kpx: `EXECUTE_0(mcrf)`; XL_form prim=19 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `mcrxr` | spr | — | `X_form` 31/512 | kpx: `EXECUTE_0(mcrxr)`; X_form prim=31 xo=512 CFLOW_NORMAL; in allowlist |
| [x] | `mfcr` | spr | — | `X_form` 31/19 | kpx: `EXECUTE_GENERIC_ARITH(nop, RD, CR, NONE, NONE, OE_BIT_0, RC_BIT_0)`; X_form prim=31 xo=19 CFLOW_NORMAL; in allowlist |
| [x] | `mfmsr` | spr | — | `X_form` 31/83 | kpx: `EXECUTE_0(mfmsr)`; X_form prim=31 xo=83 CFLOW_NORMAL; in allowlist |
| [~] | `mfspr` | spr | — | `XFX_form` 31/339 | kpx: `EXECUTE_1(mfspr, operand_SPR)`; XFX_form prim=31 xo=339 CFLOW_NORMAL; Supported; non-user SPR ends block via helper.; in allowlist |
| [x] | `mfsr` | spr | — | `X_form` 31/595 | kpx: `EXECUTE_0(mfsr)`; X_form prim=31 xo=595 CFLOW_NORMAL; in allowlist |
| [x] | `mfsrin` | spr | — | `X_form` 31/659 | kpx: `EXECUTE_0(mfsrin)`; X_form prim=31 xo=659 CFLOW_NORMAL; in allowlist |
| [~] | `mftb` | spr | — | `XFX_form` 31/371 | kpx: `EXECUTE_1(mftbr, operand_TBR)`; XFX_form prim=31 xo=371 CFLOW_NORMAL; TBL/TBU only.; in allowlist |
| [x] | `mtcrf` | spr | — | `XFX_form` 31/144 | kpx: `EXECUTE_0(mtcrf)`; XFX_form prim=31 xo=144 CFLOW_NORMAL; in allowlist |
| [~] | `mtmsr` | spr | — | `X_form` 31/146 | kpx: `EXECUTE_0(mtmsr)`; X_form prim=31 xo=146 CFLOW_NORMAL; ends_block.; **ends_block**; in allowlist |
| [x] | `mtspr` | spr | — | `XFX_form` 31/467 | kpx: `EXECUTE_1(mtspr, operand_SPR)`; XFX_form prim=31 xo=467 CFLOW_NORMAL; Supported; guest helper for non-user.; in allowlist |
| [~] | `mtsr` | spr | — | `X_form` 31/210 | kpx: `EXECUTE_0(mtsr)`; X_form prim=31 xo=210 CFLOW_NORMAL; ends_block.; **ends_block**; in allowlist |
| [~] | `mtsrin` | spr | — | `X_form` 31/242 | kpx: `EXECUTE_0(mtsrin)`; X_form prim=31 xo=242 CFLOW_NORMAL; ends_block.; **ends_block**; in allowlist |

## Todo / partial by wave

### W6 (6)

- [~] `lvx` — kpx: `EXECUTE_VECTOR_LOADSTORE(load, V2DI, RA_or_0, RB)`; X_form prim=31 xo=103 CFLOW_NORMAL; hint ignored (lvxl same path).; in allowlist
- [~] `lvxl` — kpx: `EXECUTE_VECTOR_LOADSTORE(load, V2DI, RA_or_0, RB)`; X_form prim=31 xo=359 CFLOW_NORMAL; hint ignored.; in allowlist
- [~] `stvx` — kpx: `EXECUTE_VECTOR_LOADSTORE(store, V2DI, RA_or_0, RB)`; X_form prim=31 xo=231 CFLOW_NORMAL; hint ignored.; in allowlist
- [~] `stvxl` — kpx: `EXECUTE_VECTOR_LOADSTORE(store, V2DI, RA_or_0, RB)`; X_form prim=31 xo=487 CFLOW_NORMAL; hint ignored.; in allowlist
- [~] `vslo` — kpx: `EXECUTE_VECTOR_SHIFT_OCTET(-1, V16QIm, V16QIm, NONE, SHBO)`; VX_form prim=4 xo=1036 CFLOW_NORMAL; Allowlisted but **ends_block** (one-op; Starting Up lock).; **ends_block**; in allowlist
- [~] `vsro` — kpx: `EXECUTE_VECTOR_SHIFT_OCTET(+1, V16QIm, V16QIm, NONE, SHBO)`; VX_form prim=4 xo=1100 CFLOW_NORMAL; Allowlisted but **ends_block** (one-op; Starting Up lock).; **ends_block**; in allowlist

### exclude (3)

- [-] `eciwx` — kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=310 CFLOW_NORMAL; External control; no device model.; not in `nw_jit_op_supported`
- [-] `ecowx` — kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=438 CFLOW_NORMAL; External control; no device model.; not in `nw_jit_op_supported`
- [-] `invalid` — kpx: `EXECUTE_0(illegal)`; INVALID_form prim=0 xo=0 CFLOW_TRAP; Not a real insn. Prim=6 skip noise is usually this class.; not in `nw_jit_op_supported`

## Already done (checked)

Quick list of `[x]` names for scanning:

`add`, `addc`, `adde`, `addi`, `addic`, `addic.`, `addis`, `addme`, `addze`, `and`
`andc`, `andi.`, `andis.`, `b`, `bc`, `bcctr`, `bclr`, `cntlzw`, `crand`, `crandc`
`creqv`, `crnand`, `crnor`, `cror`, `crorc`, `crxor`, `dcba`, `dcbf`, `dcbi`, `dcbst`
`dcbt`, `dcbtst`, `dcbz`, `divw`, `divwu`, `dss`, `dst`, `dstst`, `eieio`, `eqv`
`extsb`, `extsh`, `fabs`, `fadd`, `fadds`, `fcmpo`, `fcmpu`, `fctiw`, `fctiwz`, `fdiv`
`fdivs`, `fmadd`, `fmadds`, `fmr`, `fmsub`, `fmsubs`, `fmul`, `fmuls`, `fnabs`, `fneg`
`fnmadd`, `fnmadds`, `fnmsub`, `fnmsubs`, `fres`, `frsp`, `frsqrte`, `fsel`, `fsub`, `fsubs`
`isync`, `lbz`, `lbzu`, `lbzx`, `lfd`, `lfdx`, `lfs`, `lfsx`, `lha`, `lhau`
`lhaux`, `lhax`, `lhbrx`, `lhz`, `lhzx`, `lmw`, `lswi`, `lswx`, `lvebx`, `lvehx`
`lvewx`, `lvsl`, `lvsr`, `lwarx`, `lwbrx`, `lwz`, `lwzu`, `lwzx`, `mcrf`, `mcrfs`
`mcrxr`, `mfcr`, `mffs`, `mfmsr`, `mfsr`, `mfsrin`, `mfvscr`, `mtcrf`, `mtfsb0`, `mtfsb1`
`mtfsf`, `mtfsfi`, `mtspr`, `mtvscr`, `mulhw`, `mulhwu`, `mulli`, `mullw`, `nand`, `neg`
`nor`, `or`, `orc`, `ori`, `oris`, `rlwimi`, `rlwinm`, `rlwnm`, `sc`, `slw`
`sraw`, `srawi`, `srw`, `stb`, `stbu`, `stbx`, `stfd`, `stfdx`, `stfs`, `stfsx`
`sth`, `sthbrx`, `sthu`, `sthx`, `stmw`, `stswi`, `stswx`, `stvebx`, `stvehx`, `stvewx`
`stw`, `stwbrx`, `stwcx.`, `stwu`, `stwx`, `subf`, `subfc`, `subfe`, `subfic`, `subfme`
`subfze`, `sync`, `tlbsync`, `tw`, `twi`, `vaddcuw`, `vaddfp`, `vaddsbs`, `vaddshs`, `vaddsws`
`vaddubm`, `vaddubs`, `vadduhm`, `vadduhs`, `vadduwm`, `vadduws`, `vand`, `vandc`, `vavgsb`, `vavgsh`
`vavgsw`, `vavgub`, `vavguh`, `vavguw`, `vcfsx`, `vcfux`, `vcmpbfp`, `vcmpeqfp`, `vcmpequb`, `vcmpequh`
`vcmpequw`, `vcmpgefp`, `vcmpgtfp`, `vcmpgtsb`, `vcmpgtsh`, `vcmpgtsw`, `vcmpgtub`, `vcmpgtuh`, `vcmpgtuw`, `vctsxs`
`vctuxs`, `vexptefp`, `vlogefp`, `vmaddfp`, `vmaxfp`, `vmaxsb`, `vmaxsh`, `vmaxsw`, `vmaxub`, `vmaxuh`
`vmaxuw`, `vmhaddshs`, `vmhraddshs`, `vminfp`, `vminsb`, `vminsh`, `vminsw`, `vminub`, `vminuh`, `vminuw`
`vmladduhm`, `vmrghb`, `vmrghh`, `vmrghw`, `vmrglb`, `vmrglh`, `vmrglw`, `vmsummbm`, `vmsumshm`, `vmsumshs`
`vmsumubm`, `vmsumuhm`, `vmsumuhs`, `vmulesb`, `vmulesh`, `vmuleub`, `vmuleuh`, `vmulosb`, `vmulosh`, `vmuloub`
`vmulouh`, `vnmsubfp`, `vnor`, `vor`, `vperm`, `vpkpx`, `vpkshss`, `vpkshus`, `vpkswss`, `vpkswus`
`vpkuhum`, `vpkuhus`, `vpkuwum`, `vpkuwus`, `vrefp`, `vrfim`, `vrfin`, `vrfip`, `vrfiz`, `vrlb`
`vrlh`, `vrlw`, `vrsqrtefp`, `vsel`, `vsl`, `vslb`, `vsldoi`, `vslh`, `vslw`, `vspltb`
`vsplth`, `vspltisb`, `vspltish`, `vspltisw`, `vspltw`, `vsr`, `vsrab`, `vsrah`, `vsraw`, `vsrb`
`vsrh`, `vsrw`, `vsubcuw`, `vsubfp`, `vsubsbs`, `vsubshs`, `vsubsws`, `vsububm`, `vsububs`, `vsubuhm`
`vsubuhs`, `vsubuwm`, `vsubuws`, `vsum2sws`, `vsum4sbs`, `vsum4shs`, `vsum4ubs`, `vsumsws`, `vupkhpx`, `vupkhsb`
`vupkhsh`, `vupklpx`, `vupklsb`, `vupklsh`, `vxor`, `xor`, `xori`, `xoris`

## Notes on reading encodings

- **prim/xo** come from the kpx decode table (`D_form` xo is often 0; real primary opcode is `prim`).
- **AltiVec VX/VXR**: table `xo` is the 11-bit vector opcode field (`op & 0x7ff`).
- **AltiVec VA**: table `xo` is the 6-bit VA opcode (`op & 0x3f`); allowlist also has a small VA set (vperm/vsldoi/…).
- **kpx** line is the interpreter execute template — use it as the semantic oracle when milling.
- Status is derived from a Python port of `nw_jit_op_supported`; keep the C function as source of truth.
