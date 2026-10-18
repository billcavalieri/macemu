/*
 *  suite_disk.c - Disk suite: a 32 MB scratch file on each writable volume (at most three), written and read in
 *  512-byte, 4 KB, 64 KB and 1 MB blocks, sequentially and at random offsets, through PBReadSync/PBWriteSync with
 *  the no-cache flag. Every read is checked against what was written there. The file is deleted afterwards.
 *
 *  Each test names the volume and the type of disk image behind it (from the host: .hfv/raw, qcow2, CHD,
 *  sparsebundle, the shared folder). The host reads disk images through macOS's file cache (no F_NOCACHE), so these
 *  numbers describe the emulator's disk path more than the Mac's SSD; the report says so.
 *
 *  (C) 2026 Bill Cavalieri
 */

#include "suites.h"
#include "bench_host.h"
#include <Files.h>
#include <Memory.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char kSuite[] = "disk";

#define FILE_BYTES (32L * 1024 * 1024)
#define noCacheMask 0x20

enum { seqWrite, seqRead, randRead, randWrite };

typedef struct {
    short ref;
    long block;
    long blocks;            /* in the file */
    long next;              /* sequential position, in blocks */
    uint32_t seed;
    int mode;
    unsigned char *buf;
    long bad;               /* reads that did not match */
} DiskCtx;

/* Every block holds a fixed pattern (word i: its sector number and a constant) with its own block number in the
 * first and the last word. Writing only changes those two words, so the time is the disk's, not the pattern's. */
static uint32_t Base(long i)
{
    return (uint32_t)(i / 128) * 0x01010101UL ^ 0xA5C35A3CUL;
}

static void FillBase(unsigned char *buf, long size)
{
    uint32_t *w = (uint32_t *)buf;
    long i, n = size / 4;
    for (i = 0; i < n; i++)
        w[i] = Base(i);
}

static void Fill(unsigned char *buf, long block, long size)
{
    uint32_t *w = (uint32_t *)buf;
    w[0] = (uint32_t)block ^ 0x5EED0000UL;
    w[size / 4 - 1] = ~(uint32_t)block;
}

static Boolean Matches(unsigned char *buf, long block, long size)
{
    uint32_t *w = (uint32_t *)buf;
    long i, n = size / 4;
    Boolean ok = w[0] == ((uint32_t)block ^ 0x5EED0000UL) && w[n - 1] == ~(uint32_t)block;
    for (i = 1; i < n - 1 && ok; i += 61)       /* a sample of the rest: enough to catch a wrong block */
        ok = w[i] == Base(i);
    w[0] = 0;                                    /* so a read that transfers nothing cannot pass the next time */
    w[n - 1] = 0;
    return ok;
}

static OSErr Io(DiskCtx *d, long block, Boolean write)
{
    ParamBlockRec pb;
    memset(&pb, 0, sizeof pb);
    pb.ioParam.ioRefNum = d->ref;
    pb.ioParam.ioBuffer = (Ptr)d->buf;
    pb.ioParam.ioReqCount = d->block;
    pb.ioParam.ioPosMode = fsFromStart | noCacheMask;
    pb.ioParam.ioPosOffset = block * d->block;
    return write ? PBWriteSync(&pb) : PBReadSync(&pb);
}

static uint32_t Body(void *p, uint32_t iters)
{
    DiskCtx *d = (DiskCtx *)p;
    uint32_t i;
    for (i = 0; i < iters; i++) {
        long b;
        if (d->mode == seqWrite || d->mode == seqRead) {
            b = d->next;
            d->next = (d->next + 1) % d->blocks;
        } else {
            d->seed = d->seed * 1664525UL + 1013904223UL;
            b = (long)((d->seed >> 8) % (uint32_t)d->blocks);
        }
        if (d->mode == seqWrite || d->mode == randWrite) {
            Fill(d->buf, b, d->block);
            Io(d, b, true);
        } else {
            Io(d, b, false);
            if (!Matches(d->buf, b, d->block))
                d->bad++;
        }
    }
    return iters;
}

/* "kind=qcow2 ro=0 ..." for the drive from the host's disk lines, or "unknown". */
static void DriveKind(short drive, char *out)
{
    const char *p = HostInfoText();
    strcpy(out, "unknown");
    while (p && *p) {
        const char *end = strchr(p, '\n'), *k;
        char line[200];
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (n >= sizeof line)
            n = sizeof line - 1;
        memcpy(line, p, n);
        line[n] = 0;
        if ((!strncmp(line, "disk ", 5) || !strncmp(line, "cdrom ", 6) || !strncmp(line, "extfs ", 6)) &&
            atoi(strstr(line, "drive=") ? strstr(line, "drive=") + 6 : "0") == drive && (k = strstr(line, "kind=")) != NULL) {
            sscanf(k + 5, "%31s", out);
            return;
        }
        p = end ? end + 1 : NULL;
    }
}

static void RunVolume(short vRef, const char *volName, short drive)
{
    static const long sizes[] = { 512, 4096, 65536, 1048576 };
    static const char *const modes[] = { "sequential write", "sequential read", "random read", "random write" };
    char kind[32], label[96];
    FSSpec spec;
    short ref;
    Handle temp;
    void *raw;
    unsigned char *buf;
    long s, written;
    OSErr err;
    DriveKind(drive, kind);
    sprintf(label, "%s (%s)", volName, kind);
    FSMakeFSSpec(vRef, 2, "\pSheepBench Disk Test", &spec);
    FSpDelete(&spec);
    if ((err = FSpCreate(&spec, 'ShBn', 'tmp ', smSystemScript)) != noErr ||
        (err = FSpOpenDF(&spec, fsRdWrPerm, &ref)) != noErr) {
        char why[64], name[120];
        sprintf(why, "cannot create the test file (error %d)", err);
        sprintf(name, "%s: test file", label);
        RowSkip(TestBegin(kSuite, name, unitMBPerSec), why);
        return;
    }
    raw = BenchAlloc(1048576 + 32, &temp);
    if (!raw || SetEOF(ref, FILE_BYTES) != noErr) {
        char name[120];
        sprintf(name, "%s: test file", label);
        RowSkip(TestBegin(kSuite, name, unitMBPerSec), raw ? "not enough free space (32 MB)" : "not enough memory");
        goto done;
    }
    buf = (unsigned char *)(((uint32_t)raw + 31) & ~31UL);
    /* Lay the file down once with 1 MB blocks, so every block holds its pattern before anything is read. */
    {
        DiskCtx d;
        memset(&d, 0, sizeof d);
        d.ref = ref;
        d.block = 1048576;
        d.buf = buf;
        FillBase(buf, d.block);
        for (written = 0; written < FILE_BYTES / d.block; written++) {
            Fill(buf, written, d.block);
            if (Io(&d, written, true) != noErr)
                break;
        }
        if (written < FILE_BYTES / d.block) {
            char name[120];
            sprintf(name, "%s: test file", label);
            RowSkip(TestBegin(kSuite, name, unitMBPerSec), "writing the test file failed");
            goto done;
        }
    }
    for (s = 0; s < 4 && !gCancelled; s++) {
        int mode;
        for (mode = 0; mode < 4 && !gCancelled; mode++) {
            DiskCtx d;
            BenchRow *row;
            Measurement m;
            char name[120];
            Boolean iops = (mode == randRead || mode == randWrite) && sizes[s] <= 4096;
            if (sizes[s] >= 1024)
                sprintf(name, "%s: %s, %ld KB", label, modes[mode], sizes[s] / 1024);
            else
                sprintf(name, "%s: %s, %ld B", label, modes[mode], sizes[s]);
            row = TestBegin(kSuite, name, iops ? unitPerSec : unitMBPerSec);
            if (!row)
                continue;
            memset(&d, 0, sizeof d);
            d.ref = ref;
            d.block = sizes[s];
            d.blocks = FILE_BYTES / sizes[s];
            d.buf = buf;
            d.mode = mode;
            d.seed = 99 + (uint32_t)s;
            /* A block written with another size still holds a pattern of its own: lay the file down in this size. */
            FillBase(buf, d.block);
            if (mode == seqWrite) {
                long b;
                for (b = 0; b < d.blocks && b < 4096; b++) {
                    Fill(buf, b, d.block);
                    Io(&d, b, true);
                }
                d.blocks = b;           /* small blocks: the first 4096 only, so laying them down stays short */
            } else {
                d.blocks = sizes[s] >= 8192 ? FILE_BYTES / sizes[s] : 4096;
            }
            Measure(Body, &d, 1, &m);
            if (m.cancelled)
                gCancelled = true;
            if (d.bad) {
                char why[64];
                sprintf(why, "%ld reads returned other data than was written", d.bad);
                RowFail(row, why);
                Say(why);
                continue;
            }
            TestRecord(row, &m, iops ? unitPerSec : unitMBPerSec, iops ? 1 : (double)sizes[s]);
        }
    }
done:
    FSClose(ref);
    FSpDelete(&spec);
    FlushVol(NULL, vRef);
    BenchFree(raw, temp);
}

void SuiteDisk(void)
{
    short index;
    int n = 0;
    if (gPass == passMain)
        ReportInfo(&gReport, "disk_note", "the host reads disk images through the macOS file cache: these figures "
                                           "measure the emulator's disk path more than the SSD");
    for (index = 1; n < 3 && !gCancelled; index++) {
        HParamBlockRec pb;
        Str255 name;
        char cname[64];
        memset(&pb, 0, sizeof pb);
        pb.volumeParam.ioNamePtr = name;
        pb.volumeParam.ioVolIndex = index;
        if (PBHGetVInfoSync(&pb) != noErr)
            break;
        if (pb.volumeParam.ioVAtrb & 0x8080)      /* locked by hardware or software */
            continue;
        if ((double)pb.volumeParam.ioVFrBlk * (double)pb.volumeParam.ioVAlBlkSiz < 40.0 * 1024 * 1024)
            continue;
        memcpy(cname, name + 1, name[0] < 63 ? name[0] : 63);
        cname[name[0] < 63 ? name[0] : 63] = 0;
        RunVolume(pb.volumeParam.ioVRefNum, cname, pb.volumeParam.ioVDrvInfo);
        n++;
    }
    if (n == 0)
        RowSkip(TestBegin(kSuite, "volumes", unitMBPerSec), "no writable volume with 40 MB free");
}
