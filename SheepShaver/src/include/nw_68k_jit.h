#ifndef NW_68K_JIT_H
#define NW_68K_JIT_H
#include <stdint.h>
class powerpc_cpu;
void nw_log_mkdir(const char *dir);
void nw_68k_hist_note(uint32_t ppc_pc, uint32_t opcode_pc);
bool nw_68k_hist_enabled();
void nw_68k_note_exit(uint16_t opcode, unsigned reason);
void nw_68k_note_exit_at(uint16_t opcode, unsigned reason, uint32_t pc);
void nw_68k_measure_begin(uint32_t first, uint32_t end);
uint64_t nw_68k_measure_end();
uint64_t nw_68k_fallback_count();
uint64_t nw_68k_native_count();
void nw_68k_op_summary();
/* Unified normal guest/Execute68k entry. True means execution advanced. */
int nw_68k_dispatch(powerpc_cpu *);
void nw_68k_observe(powerpc_cpu *, uint32_t ppc_pc, int depth);
void nw_68k_end_execution(powerpc_cpu *, int depth);
bool nw_68k_reference_active(const powerpc_cpu *);
bool nw_68k_wants_boundary(uint32_t ppc_pc);
void nw_68k_reference_chain(uint32_t ppc_pc, uint32_t opcode_pc, uint32_t dispatch);
void nw_68k_jit_execute(powerpc_cpu *, uint32_t entry);
#endif
