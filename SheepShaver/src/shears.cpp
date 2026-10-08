/*
 *  shears.cpp - Sheep Shears: transport between the guest tools and the host app (see shears.h)
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

#include <stdio.h>
#include <string.h>
#include <atomic>

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "thunks.h"
#include "shears.h"

// The guest tools hard-code the opcode word for this selector. Selectors are append-only; moving this one would
// break every installed tool.
static_assert(NATIVE_SHEARS == 45, "NATIVE_SHEARS is part of the guest ABI; append new selectors after it");

static std::atomic<shears_handler_t> shears_handler(NULL);

extern "C" void ShearsHostSetHandler(shears_handler_t handler)
{
	shears_handler.store(handler);
}

uint32 ShearsCall(uint32 mailbox)
{
	// The mailbox is a guest logical address of a block in RAM. Anything else is refused without being touched.
	if ((mailbox & 3) != 0 || mailbox < 0x1000 || mailbox > RAMSize || RAMSize - mailbox < SHEARS_MAILBOX_SIZE)
		return 0;
	if (ReadMacInt32(mailbox) != SHEARS_MAGIC)
		return 0;
	uint32 length = ReadMacInt32(mailbox + 20);
	if (length > SHEARS_MAX_PAYLOAD)
		return 0;

	uint8 request[SHEARS_MAILBOX_SIZE];
	uint32 request_len = SHEARS_HEADER_SIZE + length;
	for (uint32 i = 0; i < request_len; i += 4)
		*(uint32 *)(request + i) = htonl(ReadMacInt32(mailbox + i));

	uint8 reply[SHEARS_MAILBOX_SIZE];
	memset(reply, 0, sizeof reply);
	memcpy(reply, request, SHEARS_HEADER_SIZE);
	int reply_len = -1;
	shears_handler_t handler = shears_handler.load();
	if (handler)
		reply_len = handler(request, request_len, reply, sizeof reply);
	if (reply_len < SHEARS_HEADER_SIZE || reply_len > SHEARS_MAILBOX_SIZE)
		return 0;

	// Only status, length and payload are written back; the guest's own header words stay as they were.
	uint32 reply_payload = ntohl(*(uint32 *)(reply + 20));
	if (reply_payload > SHEARS_MAX_PAYLOAD || SHEARS_HEADER_SIZE + reply_payload != (uint32)reply_len)
		return 0;
	WriteMacInt32(mailbox + 16, ntohl(*(uint32 *)(reply + 16)));
	WriteMacInt32(mailbox + 20, reply_payload);
	for (uint32 i = 0; i < reply_payload; i += 4)
		WriteMacInt32(mailbox + SHEARS_HEADER_SIZE + i, ntohl(*(uint32 *)(reply + SHEARS_HEADER_SIZE + i)));
	return SHEARS_MAGIC;
}
