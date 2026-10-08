/*
 *  shears.h - Sheep Shears: transport between the guest tools and the host app
 *
 *  A guest application executes the fake native opcode NATIVE_SHEARS with r3 pointing at a mailbox in guest
 *  memory. ShearsCall() copies the request out, hands it to the host handler (Swift in the app, registered with
 *  ShearsHostSetHandler) and copies the reply back. The core holds no protocol logic; it only checks the pointer
 *  and the lengths. All words in the mailbox are big-endian, as in the guest.
 *
 *  Mailbox (4096 bytes, 4-byte aligned, in guest RAM):
 *    0  magic 'SHRS'   4  version   8  seq   12  command   16  status   20  payload length   24, 28  reserved
 *    32 payload (up to 4064 bytes)
 *  The guest fills magic, version, seq, command, length and payload. The host replies in place: status, length and
 *  payload (seq is echoed). The value returned in r3 is 'SHRS' when the call was handled and 0 when the mailbox was
 *  rejected (bad pointer, bad magic or length); the guest tool backs off on 0.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

#ifndef SHEARS_H
#define SHEARS_H

#include "sysdeps.h"

#define SHEARS_MAGIC 0x53485253u		// 'SHRS'
#define SHEARS_MAILBOX_SIZE 4096
#define SHEARS_HEADER_SIZE 32
#define SHEARS_MAX_PAYLOAD (SHEARS_MAILBOX_SIZE - SHEARS_HEADER_SIZE)

/* Host handler: request and reply are whole mailbox images (header and payload, big-endian). Returns the reply
 * length in bytes (at least SHEARS_HEADER_SIZE), or a negative value if the request was refused. */
typedef int (*shears_handler_t)(const uint8 *request, uint32 request_len, uint8 *reply, uint32 reply_cap);

#ifdef __cplusplus
extern "C" {
#endif
void ShearsHostSetHandler(shears_handler_t handler);
#ifdef __cplusplus
}
#endif

// Called for NATIVE_SHEARS, mailbox is a guest address. Returns the value for r3.
uint32 ShearsCall(uint32 mailbox);

#endif
