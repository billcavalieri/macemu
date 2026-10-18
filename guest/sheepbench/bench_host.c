/*
 *  bench_host.c - SheepBench's link to the SheepShaver host (see bench_host.h).
 *
 *  (C) 2026 Bill Cavalieri
 */

#include "bench_host.h"
#include <Memory.h>
#include <Events.h>
#include <string.h>

#define SHEARS_MAGIC    0x53485253UL    /* 'SHRS' */
#define SHEARS_OPCODE   0x18000B42UL    /* 0x18000000 | NATIVE_SHEARS (45) << 6 | 2 */
#define SIG_SHEEPSHAVER 0x42616168UL    /* 'Baah' at 0x2800: any SheepShaver */
#define SIG_SHEARS      0x53485253UL    /* 'SHRS' at 0x2860: this SheepShaver answers the mailbox */
#define TOOL_VERSION    1UL

typedef struct {
    unsigned long magic, version, seq, command, status, length, reserved0, reserved1;
    unsigned char payload[4096 - 32];
} Mailbox;

static Mailbox *gBox;
static unsigned long gSeq;
static char gInfo[4096 - 32 + 1];

static unsigned long Trap(void *mailbox)
{
    register unsigned long r3 __asm__("r3") = (unsigned long)mailbox;
    __asm__ volatile(".long %1" : "+r"(r3) : "i"(SHEARS_OPCODE) : "memory", "cc");
    return r3;
}

Boolean HostOpen(void)
{
    if (gBox)
        return true;
    if (*(volatile unsigned long *)0x2800 != SIG_SHEEPSHAVER || *(volatile unsigned long *)0x2860 != SIG_SHEARS)
        return false;
    gBox = (Mailbox *)NewPtrSysClear(sizeof(Mailbox));
    return gBox != NULL;
}

Boolean HostPresent(void)
{
    return gBox != NULL;
}

int HostCall(unsigned long command, const void *payload, unsigned long length, void *reply, unsigned long cap,
             unsigned long *replyLen)
{
    unsigned long n;
    if (replyLen)
        *replyLen = 0;
    if (!gBox || length > sizeof gBox->payload)
        return hostNoReply;
    gBox->magic = SHEARS_MAGIC;
    gBox->version = TOOL_VERSION;
    gBox->seq = ++gSeq;
    gBox->command = command;
    gBox->status = 0xFFFFFFFFUL;
    gBox->length = length;
    if (length)
        memcpy(gBox->payload, payload, length);
    if (Trap(gBox) != SHEARS_MAGIC)
        return hostNoReply;
    n = gBox->length;
    if (n > sizeof gBox->payload)
        return hostNoReply;
    if (reply && cap) {
        if (n > cap)
            n = cap;
        memcpy(reply, gBox->payload, n);
        if (replyLen)
            *replyLen = n;
    }
    return (int)gBox->status;
}

void HostLog(const char *text)
{
    unsigned long n = strlen(text);
    if (n > 200)
        n = 200;
    HostCall(hostCmdLog, text, n, NULL, 0, NULL);
}

Boolean HostInfoRefresh(void)
{
    unsigned long n = 0;
    if (HostCall(hostCmdInfo, NULL, 0, gInfo, sizeof gInfo - 1, &n) != hostOk) {
        gInfo[0] = 0;
        return false;
    }
    gInfo[n] = 0;
    return true;
}

const char *HostInfoText(void)
{
    return gInfo;
}

/* Lines are "key=value"; drive lines ("disk drive=3 ...") have a space before the first '=' and are not keys. The
 * value is copied into one of eight buffers used in turn, so several can be held at once. */
const char *HostInfo(const char *key)
{
    static char values[8][128];
    static int turn;
    char *value = values[turn = (turn + 1) & 7];
    size_t kl = strlen(key);
    const char *p = gInfo;
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len > kl && strncmp(p, key, kl) == 0 && p[kl] == '=') {
            size_t vl = len - kl - 1;
            if (vl >= sizeof values[0])
                vl = sizeof values[0] - 1;
            memcpy(value, p + kl + 1, vl);
            value[vl] = 0;
            return value;
        }
        if (!end)
            break;
        p = end + 1;
    }
    return NULL;
}

Boolean HostNative(uint32_t kernel, uint32_t iters, uint32_t param, double *nsPerIter, uint32_t *checksum)
{
    uint32_t req[3], rep[3];
    unsigned long n = 0;
    req[0] = kernel;
    req[1] = iters;
    req[2] = param;
    if (HostCall(hostCmdNative, req, sizeof req, rep, sizeof rep, &n) != hostOk || n < sizeof rep)
        return false;
    *nsPerIter = ((double)rep[0] * 4294967296.0 + (double)rep[1]) / 1000.0;
    *checksum = rep[2];
    return true;
}

int HostSetModes(Boolean ppcJIT, Boolean m68kJIT, Boolean qdGPU, Boolean restore, long *why)
{
    uint32_t req[4], rep[1];
    unsigned long n = 0;
    int st;
    req[0] = ppcJIT ? 1 : 0;
    req[1] = m68kJIT ? 1 : 0;
    req[2] = qdGPU ? 1 : 0;
    req[3] = restore ? 1 : 0;
    st = HostCall(hostCmdSetJIT, req, sizeof req, rep, sizeof rep, &n);
    if (why)
        *why = n >= 4 ? (long)rep[0] : 0;
    return st;
}

Boolean HostSendReport(const char *text, unsigned long length)
{
    unsigned long done = 0, index = 0;
    static unsigned char chunk[3800];
    do {
        unsigned long n = length - done;
        uint32_t *w = (uint32_t *)chunk;
        if (n > sizeof chunk - 8)
            n = sizeof chunk - 8;
        w[0] = index++;
        w[1] = done + n >= length ? 1 : 0;
        memcpy(chunk + 8, text + done, n);
        if (HostCall(hostCmdResult, chunk, n + 8, NULL, 0, NULL) != hostOk)
            return false;
        done += n;
    } while (done < length);
    return true;
}
