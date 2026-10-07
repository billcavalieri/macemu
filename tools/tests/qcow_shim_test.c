/* Host test for the qcow shim: qcow_shim_test <image> <version> write|read <seed> [size_mib_to_touch]
 * write: writes pseudo-random sectors at fixed offsets (across cluster boundaries and the end of the image),
 *        then reads them back through the shim; read: only verifies what an earlier write run left behind. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qcow_shim.h"

static void fill(unsigned char *b, size_t n, unsigned seed)
{
    unsigned s = seed * 2654435761u + 1;
    for (size_t i = 0; i < n; i++) { s = s * 1664525u + 1013904223u; b[i] = (unsigned char)(s >> 24); }
}

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: %s image version write|read seed\n", argv[0]); return 2; }
    int write_mode = !strcmp(argv[3], "write");
    unsigned seed = (unsigned)atoi(argv[4]);
    char err[512] = ""; int info = 0;
    ss_qcow *q = ss_qcow_open(argv[1], atoi(argv[2]), !write_mode, &info, err, sizeof err);
    if (!q) { fprintf(stderr, "open failed (info %d): %s\n", info, err); return info & SS_QCOW_BACKING_FAILED ? 3 : 1; }
    printf("chain %s\n", ss_qcow_chain(q));
    int64_t size = ss_qcow_size(q);
    printf("size %lld read_only %d\n", (long long)size, ss_qcow_is_read_only(q));
    int64_t offsets[] = { 0, 512, 65536 - 512, 65536, 1048576 + 1024, 3 * 65536 + 4096, size - 4096, size - 512 };
    size_t lens[] = { 512, 512, 512, 4096, 512, 65536 + 512, 3584, 512 };
    int bad = 0;
    for (unsigned i = 0; i < sizeof offsets / sizeof offsets[0]; i++) {
        if (offsets[i] < 0 || offsets[i] + (int64_t)lens[i] > size) continue;
        unsigned char *w = malloc(lens[i]), *r = malloc(lens[i]);
        fill(w, lens[i], seed + i);
        if (write_mode && ss_qcow_pwrite(q, w, offsets[i], lens[i]) != 0) { printf("write %u failed\n", i); bad++; }
        if (ss_qcow_pread(q, r, offsets[i], lens[i]) != 0 || memcmp(r, w, lens[i])) { printf("verify %u failed at %lld\n", i, (long long)offsets[i]); bad++; }
        free(w); free(r);
    }
    ss_qcow_close(q);
    printf(bad ? "FAIL\n" : "OK\n");
    return bad != 0;
}
