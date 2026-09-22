/*
 *  audio.cpp - Audio support
 *
 *  Basilisk II (C) 1997-2008 Christian Bauer
 *  Portions written by Marc Hellwig
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 *  SEE ALSO
 *    Inside Macintosh: Sound, chapter 5 "Sound Components"
 */

#include "sysdeps.h"
#include <string.h>
#include "cpu_emulation.h"
#include "macos_util.h"
#include "emul_op.h"
#include "main.h"
#include "audio.h"
#include "audio_defs.h"
#if defined(SHEEPSHAVER)
#include "nw_io.h"
#endif
#include "user_strings.h"
#include "cdrom.h"
#if defined(SHEEPSHAVER)
#include "xlowmem.h"
#endif

#define DEBUG 0
#include "debug.h"


// Supported sample rates, sizes and channels
vector<uint32> audio_sample_rates;
vector<uint16> audio_sample_sizes;
vector<uint8> audio_channel_counts;

// Global variables
struct audio_status AudioStatus;	// Current audio status (sample rate etc.)
bool audio_open = false;			// Flag: audio is initialized and ready
int audio_frames_per_block;			// Number of audio frames per block
uint32 audio_component_flags;		// Component feature flags
uint32 audio_data = 0;				// Mac address of global data area
static int open_count = 0;			// Open/close nesting count
#if defined(SHEEPSHAVER)
static int sb_pull_on;				// A mixer source exists; pull GetSourceData
static int sb_in_play;				// Inside PlaySourceBuffer
static int sb_in_mixer;				// Inside a mixer Execute68k
static int sb_in_completion;			// Inside the buffer-done callback
static uint32 sb_done_glue;			// 68k stub that calls the buffer completion
static uint32 sb_done_upp;			// Completion to call after playback
static uint32 sb_done_pb;
static int sb_done_wait;			// Frames still queued when the completion was armed
#endif

bool AudioAvailable = false;		// Flag: audio output available (from the software point of view)

int SoundInSource = 2;
int SoundInPlaythrough = 7;
int SoundInGain = 65536; // FIXED 4-byte from 0.5 to 1.5; this is middle value (1) as int

/*
 *  Reset audio emulation
 */

void AudioReset(void)
{
	audio_data = 0;
}


/*
 *  Get audio info
 */

static int32 AudioGetInfo(uint32 infoPtr, uint32 selector, uint32 sourceID)
{
	D(bug(" AudioGetInfo %c%c%c%c, infoPtr %08lx, source ID %08lx\n", selector >> 24, (selector >> 16) & 0xff, (selector >> 8) & 0xff, selector & 0xff, infoPtr, sourceID));
	M68kRegisters r;

	switch (selector) {
		case siSampleSize:
			WriteMacInt16(infoPtr, AudioStatus.sample_size);
			break;

		case siSampleSizeAvailable: {
			r.d[0] = audio_sample_sizes.size() * 2;
			Execute68kTrap(0xa122, &r);	// NewHandle()
			uint32 h = r.a[0];
			if (h == 0)
				return memFullErr;
			WriteMacInt16(infoPtr + sil_count, audio_sample_sizes.size());
			WriteMacInt32(infoPtr + sil_infoHandle, h);
			uint32 sp = ReadMacInt32(h);
			for (unsigned i=0; i<audio_sample_sizes.size(); i++)
				WriteMacInt16(sp + i*2, audio_sample_sizes[i]);
			break;
		}

		case siNumberChannels:
			WriteMacInt16(infoPtr, AudioStatus.channels);
			break;

		case siChannelAvailable: {
			r.d[0] = audio_channel_counts.size() * 2;
			Execute68kTrap(0xa122, &r);	// NewHandle()
			uint32 h = r.a[0];
			if (h == 0)
				return memFullErr;
			WriteMacInt16(infoPtr + sil_count, audio_channel_counts.size());
			WriteMacInt32(infoPtr + sil_infoHandle, h);
			uint32 sp = ReadMacInt32(h);
			for (unsigned i=0; i<audio_channel_counts.size(); i++)
				WriteMacInt16(sp + i*2, audio_channel_counts[i]);
			break;
		}

		case siSampleRate:
			WriteMacInt32(infoPtr, AudioStatus.sample_rate);
			break;

		case siSampleRateAvailable: {
			r.d[0] = audio_sample_rates.size() * 4;
			Execute68kTrap(0xa122, &r);	// NewHandle()
			uint32 h = r.a[0];
			if (h == 0)
				return memFullErr;
			WriteMacInt16(infoPtr + sil_count, audio_sample_rates.size());
			WriteMacInt32(infoPtr + sil_infoHandle, h);
			uint32 lp = ReadMacInt32(h);
			for (unsigned i=0; i<audio_sample_rates.size(); i++)
				WriteMacInt32(lp + i*4, audio_sample_rates[i]);
			break;
		}

		case siSpeakerMute:
			WriteMacInt16(infoPtr, audio_get_speaker_mute());
			break;

		case siSpeakerVolume:
			WriteMacInt32(infoPtr, audio_get_speaker_volume());
			break;

		case siHardwareMute:
			WriteMacInt16(infoPtr, audio_get_main_mute());
			break;

		case siHardwareVolume:
			WriteMacInt32(infoPtr, audio_get_main_volume());
			break;

		case siHardwareVolumeSteps:
			WriteMacInt16(infoPtr, 7);
			break;

		case siHardwareBusy:
			WriteMacInt16(infoPtr, AudioStatus.num_sources != 0);
			break;

		case siHardwareFormat:
			WriteMacInt32(infoPtr + scd_flags, 0);
			WriteMacInt32(infoPtr + scd_format, AudioStatus.sample_size == 16 ? FOURCC('t','w','o','s') : FOURCC('r','a','w',' '));
			WriteMacInt16(infoPtr + scd_numChannels, AudioStatus.channels);
			WriteMacInt16(infoPtr + scd_sampleSize, AudioStatus.sample_size);
			WriteMacInt32(infoPtr + scd_sampleRate, AudioStatus.sample_rate);
			WriteMacInt32(infoPtr + scd_sampleCount, audio_frames_per_block);
			WriteMacInt32(infoPtr + scd_buffer, 0);
			WriteMacInt32(infoPtr + scd_reserved, 0);
			break;

		case FOURCC('c','o','m','p'): /* siCompressionType: PCM only */
			WriteMacInt32(infoPtr, FOURCC('t','w','o','s'));
			break;

		case FOURCC('c','m','a','v'): { /* siCompressionAvailable */
			r.d[0] = 8;
			Execute68kTrap(0xa122, &r);
			uint32 h = r.a[0];
			if (h == 0)
				return memFullErr;
			WriteMacInt16(infoPtr + sil_count, 2);
			WriteMacInt32(infoPtr + sil_infoHandle, h);
			uint32 lp = ReadMacInt32(h);
			WriteMacInt32(lp, FOURCC('t','w','o','s'));
			WriteMacInt32(lp + 4, FOURCC('r','a','w',' '));
			break;
		}

		default:	// Delegate to Apple Mixer
			if (AudioStatus.mixer == 0)
				return badComponentSelector;
			M68kRegisters r;
			r.a[0] = infoPtr;
			r.d[0] = selector;
			r.a[1] = sourceID;
			r.a[2] = AudioStatus.mixer;
#if defined(SHEEPSHAVER)
			sb_in_mixer = 1;
#endif
			Execute68k(audio_data + adatGetInfo, &r);
#if defined(SHEEPSHAVER)
			sb_in_mixer = 0;
#endif
			D(bug("  delegated to Apple Mixer, returns %08lx\n", r.d[0]));
			return r.d[0];
	}
	return noErr;
}


/*
 *  Set audio info
 */

static int32 AudioSetInfo(uint32 infoPtr, uint32 selector, uint32 sourceID)
{
	D(bug(" AudioSetInfo %c%c%c%c, infoPtr %08lx, source ID %08lx\n", selector >> 24, (selector >> 16) & 0xff, (selector >> 8) & 0xff, selector & 0xff, infoPtr, sourceID));
	M68kRegisters r;

	switch (selector) {
		case siSampleSize:
			D(bug("  set sample size %08lx\n", infoPtr));
			if (AudioStatus.num_sources)
				return siDeviceBusyErr;
			if (infoPtr == AudioStatus.sample_size)
				return noErr;
			for (unsigned i=0; i<audio_sample_sizes.size(); i++)
				if (audio_sample_sizes[i] == infoPtr) {
					if (audio_set_sample_size(i))
						return noErr;
					else
						return siInvalidSampleSize;
				}
			return siInvalidSampleSize;

		case siSampleRate:
			D(bug("  set sample rate %08lx\n", infoPtr));
			if (AudioStatus.num_sources)
				return siDeviceBusyErr;
			if (infoPtr == AudioStatus.sample_rate)
				return noErr;
			for (unsigned i=0; i<audio_sample_rates.size(); i++)
				if (audio_sample_rates[i] == infoPtr) {
					if (audio_set_sample_rate(i))
						return noErr;
					else
						return siInvalidSampleRate;
				}
			return siInvalidSampleRate;

		case siNumberChannels:
			D(bug("  set number of channels %08lx\n", infoPtr));
			if (AudioStatus.num_sources)
				return siDeviceBusyErr;
			if (infoPtr == AudioStatus.channels)
				return noErr;
			for (unsigned i=0; i<audio_channel_counts.size(); i++)
				if (audio_channel_counts[i] == infoPtr) {
					if (audio_set_channels(i))
						return noErr;
					else
						return badChannel;
				}
			return badChannel;

		case siSpeakerMute:
			audio_set_speaker_mute(uint16(infoPtr) != 0);
			break;

		case siSpeakerVolume:
			D(bug("  set speaker volume %08lx\n", infoPtr));
			audio_set_speaker_volume(infoPtr);
			break;

		case siHardwareMute:
			audio_set_main_mute(uint16(infoPtr) != 0);
			break;

		case siHardwareVolume:
			D(bug("  set hardware volume %08lx\n", infoPtr));
			audio_set_main_volume(infoPtr);
			break;

		default:	// Delegate to Apple Mixer
			if (AudioStatus.mixer == 0)
				return badComponentSelector;
			r.a[0] = infoPtr;
			r.d[0] = selector;
			r.a[1] = sourceID;
			r.a[2] = AudioStatus.mixer;
#if defined(SHEEPSHAVER)
			sb_in_mixer = 1;
#endif
			Execute68k(audio_data + adatSetInfo, &r);
#if defined(SHEEPSHAVER)
			sb_in_mixer = 0;
#endif
			D(bug("  delegated to Apple Mixer, returns %08lx\n", r.d[0]));
			return r.d[0];
	}
	return noErr;
}


/*
 *  Pull one decoded buffer from the Apple mixer into the ring.
 *  QuickTime writes compressed audio into the mixer and waits for
 *  this. The equalizer moves only after the mixer is asked for frames.
 *  Call only while the 68k emulator registers are live.
 */

#if defined(SHEEPSHAVER)
static void sb_call_completion(void)
{
	/* The callback plays a 1-frame tail and arms itself again.
	 * Entering it a second time is the illegal instruction
	 * (error type 3). */
	if (sb_in_completion)
		return;
	uint32 completion = sb_done_upp;
	uint32 pb = sb_done_pb;
	sb_done_upp = 0;
	sb_done_pb = 0;
	sb_done_wait = 0;
	if (completion == 0 || pb == 0)
		return;
	/* Do not run this routine. Every call raised error type 3 and
	 * never returned, so the OK button could not be clicked. The
	 * alert audio is already in the ring. */
	static int n_skip;
	if (n_skip < 6) {
		n_skip++;
		uint16 op = ReadMacInt16(completion);
		printf("NW-BOOT G1: sheepblaster skip #%d pb=%08x completion=%08x op=%04x\n",
		       n_skip, (unsigned)pb, (unsigned)completion, (unsigned)op);
		fflush(stdout);
	}
}
#endif

void AudioSheepBlasterComplete(void)
{
#if defined(SHEEPSHAVER)
	/* PlaySourceBuffer must have returned, and the frames must have
	 * left the ring. Calling the completion from inside that call
	 * made the Sound panel execute an illegal instruction (type 3). */
	if (sb_in_completion || sb_done_upp == 0 || sb_in_play || sb_in_mixer)
		return;
	if (nw_sheepblaster_pending() > sb_done_wait)
		return;
	sb_call_completion();
#endif
}

void AudioSheepBlasterService(void)
{
#if defined(SHEEPSHAVER)
	static int in_service;
	static int skip;
	if (!nw_audio_service_ok())
		return;
	if (in_service || sb_in_play || sb_in_mixer || !sb_pull_on)
		return;
	if (audio_data == 0 || AudioStatus.mixer == 0 || !nw_sheepblaster_ready())
		return;
	if (skip > 0) {
		skip--;
		return;
	}
	/* Keep about a fifth of a second queued. More than that and a
	 * pull would run ahead of the speakers. */
	if (nw_sheepblaster_pending() > 8000)
		return;
	uint32 mode = ReadMacInt32(XLM_RUN_MODE);
	if (mode != MODE_68K && mode != MODE_EMUL_OP)
		return;
	in_service = 1;
	if (mode != MODE_EMUL_OP)
		WriteMacInt32(XLM_RUN_MODE, MODE_EMUL_OP);
	WriteMacInt32(audio_data + adatStreamInfo, 0);
	M68kRegisters r;
	memset(&r, 0, sizeof r);
	r.a[0] = audio_data + adatStreamInfo;
	r.a[1] = AudioStatus.mixer;
	Execute68k(audio_data + adatGetSourceData, &r);
	uint32 info = ReadMacInt32(audio_data + adatStreamInfo);
	uint32 frames = 0, format = 0, rate = 0, buf = 0;
	int ch = 0, bits = 0;
	if (info != 0) {
		frames = ReadMacInt32(info + scd_sampleCount);
		buf = ReadMacInt32(info + scd_buffer);
		format = ReadMacInt32(info + scd_format);
		rate = ReadMacInt32(info + scd_sampleRate);
		ch = (int)ReadMacInt16(info + scd_numChannels);
		bits = (int)ReadMacInt16(info + scd_sampleSize);
	}
	static int n_log;
	if (n_log < 12) {
		n_log++;
		printf("NW-BOOT G1: sheepblaster src #%d err=%08x info=%08x frames=%u fmt=%08x rate=%u ch=%d bits=%d\n",
		       n_log, (unsigned)r.d[0], (unsigned)info, (unsigned)frames,
		       (unsigned)format, (unsigned)(rate >> 16), ch, bits);
		fflush(stdout);
	}
	int pcm = format == FOURCC('t','w','o','s') ||
		format == FOURCC('r','a','w',' ') ||
		format == FOURCC('s','o','w','t') || format == 0;
	if (buf >= 0x1000 && frames != 0 && frames <= 65536 && pcm) {
		nw_sheepblaster_enable(1);
		nw_sheepblaster_play(Mac2HostAddr(buf), frames,
			format ? format : FOURCC('t','w','o','s'), ch, bits, rate);
	} else
		skip = 8;
	WriteMacInt32(XLM_RUN_MODE, mode);
	in_service = 0;
#endif
}


/*
 *  Sound output component dispatch
 */

int32 AudioDispatch(uint32 params, uint32 globals)
{
	D(bug("AudioDispatch params %08lx (size %d), what %d\n", params, ReadMacInt8(params + cp_paramSize), (int16)ReadMacInt16(params + cp_what)));
	M68kRegisters r;
	uint32 p = params + cp_params;
	int16 selector = (int16)ReadMacInt16(params + cp_what);
	{
		static int n_sel;
		if (n_sel < 24) {
			n_sel++;
			printf("NW-BOOT G1: audio-sel #%d sel=%d\n", n_sel, (int)selector);
			fflush(stdout);
		}
	}
	switch (selector) {
		case kComponentOpenSelect:
		case kComponentCloseSelect:
		case kComponentCanDoSelect:
		case kComponentVersionSelect:
		case kComponentRegisterSelect:
		case kSoundComponentInitOutputDeviceSelect:
		case kSoundComponentGetSourceSelect:
		case kSoundComponentGetInfoSelect:
		case kSoundComponentSetInfoSelect:
		case kSoundComponentAddSourceSelect:
		case kSoundComponentRemoveSourceSelect:
		case kSoundComponentStartSourceSelect:
		case kSoundComponentStopSourceSelect:
		case kSoundComponentPauseSourceSelect:
		case kSoundComponentPlaySourceBufferSelect:
			break;
		default: {
			static int n_pass;
			if (n_pass < 8) {
				n_pass++;
				printf("NW-BOOT G1: audio-pass #%d sel=%d\n", n_pass, (int)selector);
				fflush(stdout);
			}
			/* Component Manager selectors we do not implement must
			 * fail. noErr for Target (-6) makes the caller jump
			 * through an empty procedure, which is an illegal
			 * instruction. Sound selectors stay noErr so QuickTime
			 * does not show -32766. */
			if (selector < 0)
				return badComponentSelector;
			return noErr;
		}
	}

	switch (selector) {

		// General component functions
		case kComponentOpenSelect:
			{
				static int n_open;
				if (n_open < 8) {
					n_open++;
					printf("NW-BOOT G1: audio-open #%d sources=%d\n", n_open, AudioStatus.num_sources);
					fflush(stdout);
				}
			}
			if (audio_data == 0) {

				// Allocate global data area
				r.d[0] = SIZEOF_adat;
				Execute68kTrap(0xa040, &r);	// ResrvMem()
				r.d[0] = SIZEOF_adat;
				Execute68kTrap(0xa31e, &r);	// NewPtrClear()
				if (r.a[0] == 0)
					return memFullErr;
				audio_data = r.a[0];
				D(bug(" global data at %08lx\n", audio_data));

				// Put in 68k routines
				int p = audio_data + adatDelegateCall;
				WriteMacInt16(p, 0x598f); p += 2;	// subq.l	#4,sp
				WriteMacInt16(p, 0x2f09); p += 2;	// move.l	a1,-(sp)
				WriteMacInt16(p, 0x2f08); p += 2;	// move.l	a0,-(sp)
				WriteMacInt16(p, 0x7024); p += 2;	// moveq	#$24,d0
				WriteMacInt16(p, 0xa82a); p += 2;	// ComponentDispatch
				WriteMacInt16(p, 0x201f); p += 2;	// move.l	(sp)+,d0
				WriteMacInt16(p, M68K_RTS); p += 2;	// rts
				if (p - audio_data != adatOpenMixer)
					goto adat_error;
				WriteMacInt16(p, 0x558f); p += 2;	// subq.l	#2,sp
				WriteMacInt16(p, 0x2f09); p += 2;	// move.l	a1,-(sp)
				WriteMacInt16(p, 0x2f00); p += 2;	// move.l	d0,-(sp)
				WriteMacInt16(p, 0x2f08); p += 2;	// move.l	a0,-(sp)
				WriteMacInt16(p, 0x203c); p += 2;	// move.l	#$06140018,d0
				WriteMacInt32(p, 0x06140018); p+= 4;
				WriteMacInt16(p, 0xa800); p += 2;	// SoundDispatch
				WriteMacInt16(p, 0x301f); p += 2;	// move.w	(sp)+,d0
				WriteMacInt16(p, 0x48c0); p += 2;	// ext.l	d0
				WriteMacInt16(p, M68K_RTS); p += 2;	// rts
				if (p - audio_data != adatCloseMixer)
					goto adat_error;
				WriteMacInt16(p, 0x558f); p += 2;	// subq.l	#2,sp
				WriteMacInt16(p, 0x2f08); p += 2;	// move.l	a0,-(sp)
				WriteMacInt16(p, 0x203c); p += 2;	// move.l	#$02180018,d0
				WriteMacInt32(p, 0x02180018); p+= 4;
				WriteMacInt16(p, 0xa800); p += 2;	// SoundDispatch
				WriteMacInt16(p, 0x301f); p += 2;	// move.w	(sp)+,d0
				WriteMacInt16(p, 0x48c0); p += 2;	// ext.l	d0
				WriteMacInt16(p, M68K_RTS); p += 2;	// rts
				if (p - audio_data != adatGetInfo)
					goto adat_error;
				WriteMacInt16(p, 0x598f); p += 2;	// subq.l	#4,sp
				WriteMacInt16(p, 0x2f0a); p += 2;	// move.l	a2,-(sp)
				WriteMacInt16(p, 0x2f09); p += 2;	// move.l	a1,-(sp)
				WriteMacInt16(p, 0x2f00); p += 2;	// move.l	d0,-(sp)
				WriteMacInt16(p, 0x2f08); p += 2;	// move.l	a0,-(sp)
				WriteMacInt16(p, 0x2f3c); p += 2;	// move.l	#$000c0103,-(sp)
				WriteMacInt32(p, 0x000c0103); p+= 4;
				WriteMacInt16(p, 0x7000); p += 2;	// moveq	#0,d0
				WriteMacInt16(p, 0xa82a); p += 2;	// ComponentDispatch
				WriteMacInt16(p, 0x201f); p += 2;	// move.l	(sp)+,d0
				WriteMacInt16(p, M68K_RTS); p += 2;	// rts
				if (p - audio_data != adatSetInfo)
					goto adat_error;
				WriteMacInt16(p, 0x598f); p += 2;	// subq.l	#4,sp
				WriteMacInt16(p, 0x2f0a); p += 2;	// move.l	a2,-(sp)
				WriteMacInt16(p, 0x2f09); p += 2;	// move.l	a1,-(sp)
				WriteMacInt16(p, 0x2f00); p += 2;	// move.l	d0,-(sp)
				WriteMacInt16(p, 0x2f08); p += 2;	// move.l	a0,-(sp)
				WriteMacInt16(p, 0x2f3c); p += 2;	// move.l	#$000c0104,-(sp)
				WriteMacInt32(p, 0x000c0104); p+= 4;
				WriteMacInt16(p, 0x7000); p += 2;	// moveq	#0,d0
				WriteMacInt16(p, 0xa82a); p += 2;	// ComponentDispatch
				WriteMacInt16(p, 0x201f); p += 2;	// move.l	(sp)+,d0
				WriteMacInt16(p, M68K_RTS); p += 2;	// rts
				if (p - audio_data != adatPlaySourceBuffer)
					goto adat_error;
				WriteMacInt16(p, 0x598f); p += 2;	// subq.l	#4,sp
				WriteMacInt16(p, 0x2f0a); p += 2;	// move.l	a2,-(sp)
				WriteMacInt16(p, 0x2f09); p += 2;	// move.l	a1,-(sp)
				WriteMacInt16(p, 0x2f08); p += 2;	// move.l	a0,-(sp)
				WriteMacInt16(p, 0x2f00); p += 2;	// move.l	d0,-(sp)
				WriteMacInt16(p, 0x2f3c); p += 2;	// move.l	#$000c0108,-(sp)
				WriteMacInt32(p, 0x000c0108); p+= 4;
				WriteMacInt16(p, 0x7000); p += 2;	// moveq	#0,d0
				WriteMacInt16(p, 0xa82a); p += 2;	// ComponentDispatch
				WriteMacInt16(p, 0x201f); p += 2;	// move.l	(sp)+,d0
				WriteMacInt16(p, M68K_RTS); p += 2;	// rts
				if (p - audio_data != adatGetSourceData)
					goto adat_error;
				WriteMacInt16(p, 0x598f); p += 2;	// subq.l	#4,sp
				WriteMacInt16(p, 0x2f09); p += 2;	// move.l	a1,-(sp)
				WriteMacInt16(p, 0x2f08); p += 2;	// move.l	a0,-(sp)
				WriteMacInt16(p, 0x2f3c); p += 2;	// move.l	#$00040004,-(sp)
				WriteMacInt32(p, 0x00040004); p+= 4;
				WriteMacInt16(p, 0x7000); p += 2;	// moveq	#0,d0
				WriteMacInt16(p, 0xa82a); p += 2;	// ComponentDispatch
				WriteMacInt16(p, 0x201f); p += 2;	// move.l	(sp)+,d0
				WriteMacInt16(p, M68K_RTS); p += 2;	// rts
				if (p - audio_data != adatStartSource)
					goto adat_error;
				WriteMacInt16(p, 0x598f); p += 2;	// subq.l	#4,sp
				WriteMacInt16(p, 0x2f09); p += 2;	// move.l	a1,-(sp)
				WriteMacInt16(p, 0x3f00); p += 2;	// move.w	d0,-(sp)
				WriteMacInt16(p, 0x2f08); p += 2;	// move.l	a0,-(sp)
				WriteMacInt16(p, 0x2f3c); p += 2;	// move.l	#$00060105,-(sp)
				WriteMacInt32(p, 0x00060105); p+= 4;
				WriteMacInt16(p, 0x7000); p += 2;	// moveq	#0,d0
				WriteMacInt16(p, 0xa82a); p += 2;	// ComponentDispatch
				WriteMacInt16(p, 0x201f); p += 2;	// move.l	(sp)+,d0
				WriteMacInt16(p, M68K_RTS); p += 2;	// rts
				if (p - audio_data != adatData)
					goto adat_error;
			}
			AudioAvailable = true;
			if (open_count == 0)
				audio_enter_stream();
			open_count++;
			return noErr;

adat_error:	printf("FATAL: audio component data block initialization error\n");
			QuitEmulator();
			return openErr;

		case kComponentCloseSelect:
			open_count--;
			if (open_count == 0) {
				if (audio_data) {
#if defined(SHEEPSHAVER)
					sb_pull_on = 0;
					sb_done_upp = 0;
					sb_done_pb = 0;
#endif
					if (AudioStatus.mixer) {
						// Close Apple Mixer
						r.a[0] = AudioStatus.mixer;
#if defined(SHEEPSHAVER)
						sb_in_mixer = 1;
#endif
						Execute68k(audio_data + adatCloseMixer, &r);
#if defined(SHEEPSHAVER)
						sb_in_mixer = 0;
#endif
						D(bug(" CloseMixer() returns %08lx, mixer %08lx\n", r.d[0], AudioStatus.mixer));
						AudioStatus.mixer = 0;
					}
					r.a[0] = audio_data;
					Execute68kTrap(0xa01f, &r);	// DisposePtr()
					audio_data = 0;
				}
				AudioStatus.num_sources = 0;
				audio_exit_stream();
			}
			return noErr;

		case kComponentCanDoSelect:
			switch ((int16)ReadMacInt16(p)) {
				case kComponentOpenSelect:
				case kComponentCloseSelect:
				case kComponentCanDoSelect:
				case kComponentVersionSelect:
				case kComponentRegisterSelect:
				case kSoundComponentInitOutputDeviceSelect:
				case kSoundComponentGetSourceSelect:
				case kSoundComponentGetInfoSelect:
				case kSoundComponentSetInfoSelect:
				case kSoundComponentStartSourceSelect:
					return 1;
				default:
					return 0;
			}

		case kComponentVersionSelect:
			return 0x00010003;

		case kComponentRegisterSelect:
			return noErr;

		// Sound component functions (not delegated)
		case kSoundComponentInitOutputDeviceSelect:
			D(bug(" InitOutputDevice\n"));
			if (!audio_open)
				return noHardwareErr;
			if (AudioStatus.mixer)
				return noErr;

			// Init sound component data
			WriteMacInt32(audio_data + adatData + scd_flags, 0);
			WriteMacInt32(audio_data + adatData + scd_format, AudioStatus.sample_size == 16 ? FOURCC('t','w','o','s') : FOURCC('r','a','w',' '));
			WriteMacInt16(audio_data + adatData + scd_numChannels, AudioStatus.channels);
			WriteMacInt16(audio_data + adatData + scd_sampleSize, AudioStatus.sample_size);
			WriteMacInt32(audio_data + adatData + scd_sampleRate, AudioStatus.sample_rate);
			WriteMacInt32(audio_data + adatData + scd_sampleCount, audio_frames_per_block);
			WriteMacInt32(audio_data + adatData + scd_buffer, 0);
			WriteMacInt32(audio_data + adatData + scd_reserved, 0);
			WriteMacInt32(audio_data + adatStreamInfo, 0);

			// Open Apple Mixer
			r.a[0] = audio_data + adatMixer;
			r.d[0] = 0;
			r.a[1] = audio_data + adatData;
#if defined(SHEEPSHAVER)
			sb_in_mixer = 1;
#endif
			Execute68k(audio_data + adatOpenMixer, &r);
#if defined(SHEEPSHAVER)
			sb_in_mixer = 0;
#endif
			AudioStatus.mixer = ReadMacInt32(audio_data + adatMixer);
			printf("NW-BOOT G1: audio-mixer err=%08x mixer=%08x\n",
			       (unsigned)r.d[0], (unsigned)AudioStatus.mixer);
			fflush(stdout);
#if defined(SHEEPSHAVER)
			nw_sheepblaster_enable(1);
			if (sb_done_glue == 0) {
				/* Boolean completion(SoundParamBlockPtr *pb).
				 * Pascal: 2-byte result, one pointer argument. */
				M68kRegisters gr;
				memset(&gr, 0, sizeof gr);
				gr.d[0] = 32;
				Execute68kTrap(0xa31e, &gr);	// NewPtrClear()
				if (gr.a[0] != 0) {
					sb_done_glue = gr.a[0];
					uint32 gp = sb_done_glue;
					WriteMacInt16(gp, 0x544f); gp += 2;	// subq.w #2,sp
					WriteMacInt16(gp, 0x2f09); gp += 2;	// move.l a1,-(sp)
					WriteMacInt16(gp, M68K_JSR_A0); gp += 2;
					WriteMacInt16(gp, 0x301f); gp += 2;	// move.w (sp)+,d0
					WriteMacInt16(gp, M68K_RTS);
				}
			}
			if (r.d[0] != 0)
				return noErr;
#endif
			return r.d[0];

		case kSoundComponentGetSourceSelect:
			D(bug(" GetSource source %08lx\n", ReadMacInt32(p)));
			WriteMacInt32(ReadMacInt32(p), AudioStatus.mixer);
			return noErr;

		// Sound component functions (delegated)
		case kSoundComponentAddSourceSelect:
			D(bug(" AddSource\n"));
#if defined(SHEEPSHAVER)
			sb_pull_on = 1;
			if (!nw_sheepblaster_ready())
#endif
			AudioStatus.num_sources++;
			{
				static int n_add;
				if (n_add < 8) {
					n_add++;
					printf("NW-BOOT G1: audio-add #%d sources=%d\n", n_add, AudioStatus.num_sources);
					fflush(stdout);
				}
			}
			goto delegate;

		case kSoundComponentRemoveSourceSelect:
			D(bug(" RemoveSource\n"));
#if defined(SHEEPSHAVER)
			/* The alert channel is already gone. Calling its completion
			 * after this enters a hardware poll that never returns and
			 * leaves a half-drawn window on the desktop. */
			sb_done_upp = 0;
			sb_done_pb = 0;
			if (!nw_sheepblaster_ready() && AudioStatus.num_sources > 0)
#endif
			AudioStatus.num_sources--;
			{
				static int n_rm;
				if (n_rm < 4) {
					n_rm++;
					printf("NW-BOOT G1: audio-rm #%d sources=%d\n", n_rm, AudioStatus.num_sources);
					fflush(stdout);
				}
			}
			goto delegate;

		case kSoundComponentGetInfoSelect:
			return AudioGetInfo(ReadMacInt32(p), ReadMacInt32(p + 4), ReadMacInt32(p + 8));

		case kSoundComponentSetInfoSelect:
			return AudioSetInfo(ReadMacInt32(p), ReadMacInt32(p + 4), ReadMacInt32(p + 8));

		case kSoundComponentStartSourceSelect:
#if defined(SHEEPSHAVER)
			sb_pull_on = 1;
#endif
			if (audio_data == 0 || AudioStatus.mixer == 0)
				return noErr;
			D(bug(" StartSource count %d\n", ReadMacInt16(p + 4)));
			D(bug(" starting Apple Mixer\n"));
			r.d[0] = ReadMacInt16(p + 4);
			r.a[0] = ReadMacInt32(p);
			r.a[1] = AudioStatus.mixer;
#if defined(SHEEPSHAVER)
			sb_in_mixer = 1;
#endif
			Execute68k(audio_data + adatStartSource, &r);
#if defined(SHEEPSHAVER)
			sb_in_mixer = 0;
#endif
			D(bug(" returns %08lx\n", r.d[0]));
			return noErr;

		case kSoundComponentStopSourceSelect:
			D(bug(" StopSource\n"));
			goto delegate;

		case kSoundComponentPauseSourceSelect:
			D(bug(" PauseSource\n"));
delegate:	// Delegate call to Apple Mixer
			D(bug(" delegating call to Apple Mixer\n"));
			if (audio_data == 0 || AudioStatus.mixer == 0)
				return noErr;
			r.a[0] = AudioStatus.mixer;
			r.a[1] = params;
#if defined(SHEEPSHAVER)
			sb_in_mixer = 1;
#endif
			Execute68k(audio_data + adatDelegateCall, &r);
#if defined(SHEEPSHAVER)
			sb_in_mixer = 0;
#endif
			D(bug(" returns %08lx\n", r.d[0]));
			return r.d[0];

		case kSoundComponentPlaySourceBufferSelect:
#if defined(SHEEPSHAVER)
			/* A nested call is the completion routine asking for
			 * another buffer. Returning here stops that recursion.
			 * Do not hand the buffer to the mixer. The mixer waits
			 * for a hardware interrupt that never comes, and that
			 * wait froze SimpleSound and the menu clock. */
			if (sb_in_play)
				return noErr;
			sb_in_play = 1;
			if (nw_sheepblaster_ready()) {
				uint32 pb = ReadMacInt32(p + 4);
				if (pb == 0)
					pb = ReadMacInt32(p);
				if (pb != 0) {
					uint32 rec = ReadMacInt32(pb);
					int framed = rec >= 32 && rec < 512;
					uint32 base = framed ? pb + 4 : pb;
					uint32 frames = ReadMacInt32(base + scd_sampleCount);
					uint32 buf = ReadMacInt32(base + scd_buffer);
					uint32 format = ReadMacInt32(base + scd_format);
					uint32 rate = ReadMacInt32(base + scd_sampleRate);
					int ch = (int)ReadMacInt16(base + scd_numChannels);
					int bits = (int)ReadMacInt16(base + scd_sampleSize);
					int pcm = format == FOURCC('t','w','o','s') ||
						format == FOURCC('r','a','w',' ') ||
						format == FOURCC('s','o','w','t') || format == 0;
					int queued = 0;
					if (pcm && buf != 0 && frames != 0 && frames <= 65536) {
						nw_sheepblaster_enable(1);
						queued = nw_sheepblaster_play(Mac2HostAddr(buf), frames,
							format ? format : FOURCC('t','w','o','s'),
							ch, bits, rate);
					}
					/* completionRtn is at +52. Call it only after these
					 * frames have left the ring, and not on this stack. */
					if (!sb_in_completion && framed && rec >= 56 && sb_done_glue != 0 && sb_done_upp == 0) {
						uint32 completion = ReadMacInt32(pb + 52);
						if (completion >= 0x1000 && (completion & 1) == 0 &&
						    completion + 2 < RAMSize) {
							if (rec >= 62)
								WriteMacInt16(pb + 60, 0);
							sb_done_upp = completion;
							sb_done_pb = pb;
							/* Fire when the ring is empty, so the frames
							 * just queued have been played. */
							sb_done_wait = 0;
							(void)queued;
							printf("NW-BOOT G1: sheepblaster arm pb=%08x completion=%08x op=%04x queued=%d wait=%d\n",
							       (unsigned)pb, (unsigned)completion,
							       (unsigned)ReadMacInt16(completion),
							       queued, sb_done_wait);
							fflush(stdout);
						}
					}
				}
			}
			sb_in_play = 0;
#endif
			return noErr;

		default:
			if (selector >= 0x100)
				goto delegate;
			/* 0x80008002 is badComponentSelector. QuickTime prints
			 * the low 16 bits as -32766 and drops the soundtrack. */
			return noErr;
	}
}

// not currently using these functions
/*
 *  Sound input driver Open() routine
 */

int16 SoundInOpen(uint32 pb, uint32 dce)
{
	D(bug("SoundInOpen\n"));
	return noErr;
}


/*
 *  Sound input driver Prime() routine
 */

int16 SoundInPrime(uint32 pb, uint32 dce)
{
	D(bug("SoundInPrime\n"));
	//!!
	
	uint16 code = ReadMacInt16(pb + csCode);
	D(bug("SoundInControl %d\n", code));

	if (code == 1) {
		D(bug(" SoundInKillIO\n"));
		//!!
		return noErr;
	}

	if (code != 2)
		return -231;	// siUnknownInfoType

	return noErr;
}


/*
 *  Sound input driver Control() routine
 */

int16 SoundInControl(uint32 pb, uint32 dce)
{
	uint16 code = ReadMacInt16(pb + csCode);
	D(bug("SoundInControl %d\n", code));

	if (code == 1) {
		D(bug(" SoundInKillIO\n"));
		//!!
		return noErr;
	}

	if (code != 2)
		return -231;	// siUnknownInfoType
	
	uint32 selector = ReadMacInt32(pb + csParam); // 4-byte selector (should match via FOURCC above)

	switch (selector) {
		case siInitializeDriver: {
//			If possible, the driver initializes the device to a sampling rate of 22 kHz, a sample size of 8 bits, mono recording, no compression, automatic gain control on, and all other features off.
			return noErr;
		}
			
		case siCloseDriver: {
//			The sound input device driver should stop any recording in progress, deallocate the input hardware, and initialize local variables to default settings.
			return noErr;
		}
			
		case siInputSource: {
			SoundInSource = ReadMacInt16(pb + csParam + 4);
			return noErr;
		}
			
		case siPlayThruOnOff: {
			SoundInPlaythrough = ReadMacInt16(pb + csParam + 4);
			return noErr;
		}
			
		case siOptionsDialog: {
			return noErr;
		}
			
		case siInputGain: {
			SoundInGain = ReadMacInt32(pb + csParam + 4);
			return noErr;
		}
			
		default:
			return -231;	// siUnknownInfoType
	}
}


/*
 *  Sound input driver Status() routine
 */

int16 SoundInStatus(uint32 pb, uint32 dce) // A0 points to Device Manager parameter block (pb) and A1 to device control entry (dce)
{
	uint16 code = ReadMacInt16(pb + csCode);
	D(bug("SoundInStatus %d\n", code));
	if (code != 2)
		return -231;	// siUnknownInfoType
	
	// two choices on return
	// 1: if under 18 bytes, place # of bytes at (pb+csParam) and write from (pb+csParam+4) on
	// 2: if over 18 bytes, place 0 at (pb+csParam) and directly write into address pointed to by (pb+csParam+4)
	uint32 selector = ReadMacInt32(pb + csParam); // 4-byte selector (should match via FOURCC above)
	uint32 bufferptr = ReadMacInt32(pb + csParam + 4); // 4-byte address to the buffer in vm memory

	switch (selector) {
		case siDeviceName: { // return name in STR255 format
			const uint8 str[] = { // size 9
				0x08,		// 1-byte length
				0x42, 0x75, // Bu
				0x69, 0x6c, // il
				0x74, 0x2d, // t-
				0x69, 0x6e  // in
			};
//			const uint8 str[] = { // size 12
//                0x0b,       // 1-byte length
//                0x53, 0x68, // Sh
//                0x65, 0x65, // ee
//                0x70, 0x73, // ps
//                0x68, 0x61, // ha
//                0x76, 0x65, // ve
//                0x72        // r
//			};
			WriteMacInt32(pb + csParam, 0); // response will be written directly into buffer
			Host2Mac_memcpy(bufferptr, str, sizeof(str));
			
			return noErr;
		}

		case siDeviceIcon: {
			// todo: add soundin ICN, borrow from CD ROM for now
			WriteMacInt32(pb + csParam, 0);
			
			M68kRegisters r;
			r.d[0] = sizeof(CDROMIcon);
			Execute68kTrap(0xa122, &r);	// NewHandle()
			uint32 h = r.a[0];
			if (h == 0)
				return memFullErr;
			WriteMacInt32(bufferptr, h);
			uint32 sp = ReadMacInt32(h);
			Host2Mac_memcpy(sp, CDROMIcon, sizeof(CDROMIcon));
			
			return noErr;
			
			// 68k code causes crash in sheep and link error in basilisk
//			M68kRegisters r;
//			static const uint8 proc[] = {
//				0x55, 0x8f,							// 	subq.l	#2,sp
//				0xa9, 0x94,							// 	CurResFile
//				0x42, 0x67,							// 	clr.w	-(sp)
//				0xa9, 0x98,							// 	UseResFile
//				0x59, 0x8f,							// 	subq.l	#4,sp
//				0x48, 0x79, 0x49, 0x43, 0x4e, 0x23,	// 	move.l	#'ICN#',-(sp)
//				0x3f, 0x3c, 0xbf, 0x76,				// 	move.w	#-16522,-(sp)
//				0xa9, 0xa0,							// 	GetResource
//				0x24, 0x5f,							// 	move.l	(sp)+,a2
//				0xa9, 0x98,							// 	UseResFile
//				0x20, 0x0a,							// 	move.l	a2,d0
//				0x66, 0x04,							// 	bne		1
//				0x70, 0x00,							//  moveq	#0,d0
//				M68K_RTS >> 8, M68K_RTS & 0xff,
//				0x2f, 0x0a,							//1 move.l	a2,-(sp)
//				0xa9, 0x92,							//  DetachResource
//				0x20, 0x4a,							//  move.l	a2,a0
//				0xa0, 0x4a,							//	HNoPurge
//				0x70, 0x01,							//	moveq	#1,d0
//				M68K_RTS >> 8, M68K_RTS & 0xff
//			};
//			Execute68k(Host2MacAddr((uint8 *)proc), &r);
//			if (r.d[0]) {
//				WriteMacInt32(pb + csParam, 4); // Length of returned data
//				WriteMacInt32(pb + csParam + 4, r.a[2]); // Handle to icon suite
//				return noErr;
//			} else
//				return -192;		// resNotFound
		}
			
		case siInputSource: {
			// return -231 if only 1 or index of current source if more

			WriteMacInt32(pb + csParam, 2);
			WriteMacInt16(pb + csParam + 4, SoundInSource); // index of selected source
			return noErr;
		}
			
		case siInputSourceNames: {
			// return -231 if only 1 or handle to STR# resource if more
			
			const uint8 str[] = {
				0x00, 0x02, // 2-byte count of #strings
				// byte size indicator (up to 255 length supported)
				0x0a,       // size is 10
				0x4d, 0x69,	// Mi
				0x63, 0x72,	// cr
				0x6f, 0x70,	// op
				0x68, 0x6f,	// ho
				0x6e, 0x65,	// ne
				0x0b,		// size is 11
				0x49, 0x6e, // start of string in ASCII, In
				0x74, 0x65, // te
				0x72, 0x6e, // rn
				0x61, 0x6c, // al
				0x20, 0x43, //  C
				0x44,  		// D
			};

			WriteMacInt32(pb + csParam, 0);

			M68kRegisters r;
			r.d[0] = sizeof(str);
			Execute68kTrap(0xa122, &r);	// NewHandle()
			uint32 h = r.a[0];
			if (h == 0)
				return memFullErr;
			WriteMacInt32(bufferptr, h);
			uint32 sp = ReadMacInt32(h);
			Host2Mac_memcpy(sp, str, sizeof(str));
			
			return noErr;
		}
			
		case siOptionsDialog: {
			// 0 if no options box supported and 1 if so
			WriteMacInt32(pb + csParam, 2); // response not in buffer, need to copy integer
			WriteMacInt16(pb + csParam + 4, 1); // Integer data type
			return noErr;
		}
			
		case siPlayThruOnOff: {
			// playthrough volume, 0 is off and 7 is max
			WriteMacInt32(pb + csParam, 2);
			WriteMacInt16(pb + csParam + 4, SoundInPlaythrough);
			return noErr;
		}
			
		case siNumberChannels: {
			// 1 is mono and 2 is stereo
			WriteMacInt32(pb + csParam, 2);
			WriteMacInt16(pb + csParam + 4, 2);
			return noErr;
		}
	
		case siSampleRate: {
			WriteMacInt32(pb + csParam, 0);
			WriteMacInt32(bufferptr, 0xac440000); // 44100.00000 Hz, of Fixed data type
			return noErr;
		}
	
		case siSampleRateAvailable: {
			WriteMacInt32(pb + csParam, 0);
            
            M68kRegisters r;
            r.d[0] = 4;
            Execute68kTrap(0xa122, &r);    // NewHandle()
            uint32 h = r.a[0];
            if (h == 0)
                return memFullErr;
            WriteMacInt16(bufferptr, 1); // 1 sample rate available
            WriteMacInt32(bufferptr + 2, h); // handle to sample rate list
            uint32 sp = ReadMacInt32(h);
            WriteMacInt32(sp, 0xac440000); // 44100.00000 Hz, of Fixed data type

			return noErr;
		}
			
		case siInputGain: {
			WriteMacInt32(pb + csParam, 4);
			WriteMacInt32(pb + csParam + 4, SoundInGain);
			return noErr;
		}
								   
			
		default:
			return -231;	// siUnknownInfoType
	}
}


/*
 *  Sound input driver Close() routine
 */

int16 SoundInClose(uint32 pb, uint32 dce)
{
	D(bug("SoundInClose\n"));
	return noErr;
}
