/*
 *  nw_sound_input.h - Sound input (SPB) from the host microphone
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */
#ifndef NW_SOUND_INPUT_H
#define NW_SOUND_INPUT_H

#include "sysdeps.h"
#include "main.h"

/* Pref "mic" (or a test tone in the environment): the guest gets a sound input device. */
bool nw_sound_input_wanted(void);
/* Head-patch _SoundDispatch (once the Sound Manager is up). */
void nw_sound_input_install(void);
/* EmulOp entry points. */
void nw_spb_dispatch(M68kRegisters *r);
int32 nw_spb_tick(uint32 *task);

/* The host source: 44100 Hz stereo 16-bit frames, pushed from any thread. */
void nw_mic_push(const int16 *lr, uint32 frames);
/* Counters since the last call: frames pushed by the source, frames the guest took, frames dropped (ring full). */
void nw_mic_take_stats(uint64 *pushed, uint64 *taken, uint64 *dropped);
/* Backend hooks (MacOSX/mic_capture_macosx.cpp). Start returns false when no capture is possible. */
bool nw_mic_source_start(void);
void nw_mic_source_stop(void);

/* macOS microphone permission: 0 not asked yet, 1 restricted, 2 denied, 3 allowed (hosts without the question say 3). */
int nw_mic_authorization(void);
/* Ask the person at the Mac (once; the answer comes later). */
void nw_mic_request_access(void);

#endif
