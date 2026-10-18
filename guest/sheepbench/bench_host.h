/*
 *  bench_host.h - SheepBench's link to the SheepShaver host: the Sheep Shears mailbox commands benchInfo (7),
 *  benchNative (8), benchSetJIT (9) and benchResult (10). See SheepShaver/src/include/shears.h for the mailbox and
 *  SheepApp/Shears/BenchProtocol.swift for the payloads.
 *
 *  The opcode is only executed when low memory carries both 'Baah' (0x2800, any SheepShaver) and 'SHRS' (0x2860,
 *  a SheepShaver that answers the mailbox): stock SheepShaver quits on the unknown native selector. Anywhere else
 *  every host field reads "unknown" and the comparisons that need the host are not offered.
 *
 *  (C) 2026 Bill Cavalieri
 */

#ifndef BENCH_HOST_H
#define BENCH_HOST_H

#include <MacTypes.h>
#include <stdint.h>

enum { hostCmdLog = 3, hostCmdInfo = 7, hostCmdNative = 8, hostCmdSetJIT = 9, hostCmdResult = 10 };
enum { hostOk = 0, hostUnsupported = 1, hostBadRequest = 2, hostDisabled = 3, hostNoReply = -1 };

/* True when the host answers the mailbox (both signatures present and the mailbox allocated). */
Boolean HostOpen(void);
Boolean HostPresent(void);

/* One call. Returns the host status (hostOk ...) or hostNoReply; the reply payload is copied to `reply` (up to
 * `cap` bytes) and its length stored in *replyLen. */
int HostCall(unsigned long command, const void *payload, unsigned long length, void *reply, unsigned long cap,
             unsigned long *replyLen);

/* A line in the emulator's log (visible with NW_VERBOSE=1); used by the tests. */
void HostLog(const char *text);

/* benchInfo: refreshes the key=value text. False without a host. */
Boolean HostInfoRefresh(void);
/* The value for `key` from the last refresh, or NULL. Up to eight results can be held at once. */
const char *HostInfo(const char *key);
/* The whole text ("" without a host). */
const char *HostInfoText(void);

/* benchNative: kernel run natively. False without a host or for a kernel it does not know. */
Boolean HostNative(uint32_t kernel, uint32_t iters, uint32_t param, double *nsPerIter, uint32_t *checksum);

/* benchSetJIT. Returns hostOk, hostDisabled (the VM's setting is off), hostUnsupported (*why: 1 started in a
 * mode that cannot be switched, 2 asked to turn on what was off at start-up) or hostNoReply. */
int HostSetModes(Boolean ppcJIT, Boolean m68kJIT, Boolean qdGPU, Boolean restore, long *why);

/* benchResult: sends the whole report in pieces. */
Boolean HostSendReport(const char *text, unsigned long length);

#endif
