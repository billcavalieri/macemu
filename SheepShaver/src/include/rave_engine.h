/*
 *  rave_engine.h - hooks between the SheepForce RAVE engine and its host
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 *  Licensed under the GNU General Public License, version 2 or later.
 */
#ifndef RAVE_ENGINE_H
#define RAVE_ENGINE_H

#include "sysdeps.h"

/* What the engine needs from whoever runs it: the emulator (guest thunks, calls into the
 * guest's RAVE manager) or the test harness (a fake manager). */
struct RaveHost {
	uint32 (*slot_tvect)(int slot);		/* guest TVECT that calls SheepForceRaveMethod(slot, ...) */
	uint32 (*register_draw)(uint32 draw_context, uint32 tag, uint32 method_tvect);	/* QARegisterDrawMethod */
	void (*before_write)(void);			/* about to write device pixels from the CPU: wait for the GPU */
	void (*after_write)(int x, int y, int w, int h);	/* screen pixels changed (device coordinates) */
};
extern void SheepForceRaveSetHost(const RaveHost *host);

/* Engine slots (also the harness's way to call a method directly). */
enum { RAVE_SLOT_ENGINE_BASE = 0, RAVE_SLOT_DRAW_BASE = 100, RAVE_SLOT_GETMETHOD = 200 };

#endif
