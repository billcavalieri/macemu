#ifndef NW_JIT_VERIFY_H
#define NW_JIT_VERIFY_H

#include "nw_jit.h"

/* Ordered observations from independent KPX execution. Replay owns no host
 * CPU, RAM pointers, MMU or device callbacks. A wrong access fails closed;
 * a wrong store is reported even if subsequent register results agree. */
struct nw_jit_verify_trace {
    enum { capacity = NW_JIT_MAX_BLOCK * 32 };
    struct access {
        uint32_t pc, ea, width, store, fault;
        uint64_t value;
    } accesses[capacity];
    unsigned count = 0, cursor = 0;
    bool overflow = false, mismatch = false, smc = false;

    void record(uint32_t pc, uint32_t ea, unsigned width, bool store,
                uint64_t value, uint32_t fault = 0) {
        if (count == capacity) { overflow = true; return; }
        accesses[count++] = {pc, ea, width, store ? 1u : 0u, fault, value};
        if (fault == NW_JIT_FAULT_SMC) smc = true;
    }
    static uint32_t replay(void *context, uint32_t pc, uint32_t ea,
                           unsigned width, bool store, uint64_t *value) {
        nw_jit_verify_trace &t = *static_cast<nw_jit_verify_trace *>(context);
        if (t.cursor == t.count) { t.mismatch = true; return NW_JIT_FAULT_VERIFY; }
        const access &a = t.accesses[t.cursor++];
        if (a.pc != pc || a.ea != ea || a.width != width || a.store != unsigned(store)) {
            t.mismatch = true; return NW_JIT_FAULT_VERIFY;
        }
        if (width != 1 && width != 2 && width != 4 && width != 8) {
            t.mismatch = true; return NW_JIT_FAULT_VERIFY;
        }
        const uint64_t mask = width == 8 ? UINT64_MAX : (UINT64_C(1) << (8 * width)) - 1;
        if (store && !a.fault && ((*value ^ a.value) & mask)) t.mismatch = true;
        if (store && a.fault == NW_JIT_FAULT_SMC && ((*value ^ a.value) & mask)) t.mismatch = true;
        if (!store) *value = a.value;
        return a.fault;
    }
    bool complete() const { return !overflow && !mismatch && cursor == count; }
};

#endif
