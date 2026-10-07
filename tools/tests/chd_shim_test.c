/* chd_shim_test <image> write|check|read <seed>: write = open read/write, write and verify; check = open read/write, verify only (the image must hold what a write run left); read = read-only, writes pseudo-random data at fixed offsets (inside one hunk, across
 * hunk boundaries, all-zero over data, at the end) then reads it back; `read` only verifies. Prints size and mode. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chd_shim.h"

static void fill(unsigned char *b, size_t n, unsigned seed, int zero)
{
    unsigned s = seed * 2654435761u + 1;
    for (size_t i = 0; i < n; i++) { s = s * 1664525u + 1013904223u; b[i] = zero ? 0 : (unsigned char)(s >> 24); }
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage\n"); return 2; }
    int write_mode = !strcmp(argv[2], "write"), check_only = !strcmp(argv[2], "check");
    unsigned seed = (unsigned)atoi(argv[3]);
    char err[512] = ""; int status = 0;
    ss_chd *c = ss_chd_open(argv[1], !(write_mode || check_only), &status, err, sizeof err);
    if (!c) { printf("open failed (status %d): %s\n", status, err); return 1; }
    int64_t size = ss_chd_size(c);
    printf("size %lld read_only %d\n", (long long)size, ss_chd_is_read_only(c));
    int64_t offs[] = { 512, 1024, 4096 - 512, 8192 + 512, 40000 - 512, 100000, size - 1024, size - 512 };
    size_t lens[]  = { 512, 1024, 1024, 512, 4096, 70000, 512, 512 };
    int zero[]     = { 0, 0, 0, 1, 0, 0, 0, 1 };
    int bad = 0;
    for (unsigned i = 0; i < sizeof offs / sizeof offs[0]; i++) {
        if (offs[i] < 0 || offs[i] + (int64_t)lens[i] > size) continue;
        unsigned char *w = malloc(lens[i]), *r = malloc(lens[i]);
        fill(w, lens[i], seed + i, zero[i]);
        if (write_mode && ss_chd_write(c, w, offs[i], lens[i]) != 0) { printf("write %u failed\n", i); bad++; }
        if (ss_chd_read(c, r, offs[i], lens[i]) != 0 || memcmp(r, w, lens[i])) { printf("verify %u failed at %lld\n", i, (long long)offs[i]); bad++; }
        free(w); free(r);
    }
    ss_chd_close(c);
    printf(bad ? "FAIL\n" : "OK\n");
    return bad != 0;
}
