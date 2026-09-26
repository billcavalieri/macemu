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
#include "nw_jit.h"
#include "thunks.h"
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
static int sb_sources;				// AddSource minus RemoveSource
static uint32 sb_tm;				// TMTask that pulls the mixer
static uint32 sb_tm_proc;			// Its 68k routine
static bool sb_tm_installed;
static bool sb_tm_armed;			// PrimeTime is pending
static bool sb_in_tick;				// Inside that task's pull
static int sb_in_mixer;				// Component call already inside the mixer
static bool sb_run;				// Play/Start armed it; Stop/Pause cleared it
#endif

int audio_sheepblaster_host_pull(void)
{
#if defined(SHEEPSHAVER)
	/* The card is registered at the Finder. That must not silence
	 * Built-in. Pull the ring only while this device is actually open
	 * or a buffer is queued. Execute68k from the host interrupt while
	 * the tick is inside the mixer is what bombed iTunes. */
	if (sb_tm_installed || sb_in_tick || sb_in_mixer || sb_sources > 0 || sb_run)
		return 1;
	if (nw_sheepblaster_pending() > 0)
		return 1;
#endif
	return 0;
}

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

/* Frames one GetSourceData should return. The host plays
 * audio_frames_per_block (512). 2048 is large enough for one guest
 * buffer to come out whole; the next buffer is held, not chained. */
#if defined(SHEEPSHAVER)
static int sb_chunk_frames = 2048;
#endif

static int audio_mixer_frames(void)
{
#if defined(SHEEPSHAVER)
	if (sb_chunk_frames >= 512)
		return sb_chunk_frames;
#endif
	return audio_frames_per_block > 0 ? audio_frames_per_block : 512;
}

static int32 AudioGetInfo(uint32 infoPtr, uint32 selector, uint32 sourceID)
{
	D(bug(" AudioGetInfo %c%c%c%c, infoPtr %08lx, source ID %08lx\n", selector >> 24, (selector >> 16) & 0xff, (selector >> 8) & 0xff, selector & 0xff, infoPtr, sourceID));
	M68kRegisters r;

	switch (selector) {
		case siSampleSize:
			WriteMacInt16(infoPtr, AudioStatus.sample_size);
			break;

		case siSampleSizeAvailable: {
			r.d[0] = (uint32)(audio_sample_sizes.size() * 2);
			Execute68kTrap(0xa122, &r);	// NewHandle()
			uint32 h = r.a[0];
			if (h == 0)
				return memFullErr;
			WriteMacInt16(infoPtr + sil_count, (uint32)audio_sample_sizes.size());
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
			r.d[0] = (uint32)(audio_channel_counts.size() * 2);
			Execute68kTrap(0xa122, &r);	// NewHandle()
			uint32 h = r.a[0];
			if (h == 0)
				return memFullErr;
			WriteMacInt16(infoPtr + sil_count, (uint32)audio_channel_counts.size());
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
			r.d[0] = (uint32)(audio_sample_rates.size() * 4);
			Execute68kTrap(0xa122, &r);	// NewHandle()
			uint32 h = r.a[0];
			if (h == 0)
				return memFullErr;
			WriteMacInt16(infoPtr + sil_count, (uint32)audio_sample_rates.size());
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
			WriteMacInt32(infoPtr + scd_sampleCount, audio_mixer_frames());
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
			Execute68k(audio_data + adatGetInfo, &r);
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
			Execute68k(audio_data + adatSetInfo, &r);
			D(bug("  delegated to Apple Mixer, returns %08lx\n", r.d[0]));
			return r.d[0];
	}
	return noErr;
}


/*
 *  SheepBlaster: the Apple mixer is pulled from a guest Time Manager
 *  task. The mixer calls each buffer's completion from inside
 *  GetSourceData, and the Sound Manager wrote that completion to run
 *  at interrupt time. A host timer can land in the middle of any guest
 *  instruction, and execute_68k() does not preserve r0-r12, so the
 *  host never enters the guest on its own. See SHEEPBLASTER-SOUND.md.
 */

#if defined(SHEEPSHAVER)
enum {	// TMTask struct
	tmAddr = 6,
	SIZEOF_TMTask = 22
};

enum {
	SB_PRIME_MS = 10,		// First wake after Play/Start
	SB_PULL_MS = 10,		// Wake again after a GetSourceData
	SB_IDLE_MAX_MS = 40,		// Poll while a wake came back empty
	/* QuickTime clocks the movie from this component. A buffer counts
	 * as played when GetSourceData finishes it, which is when it
	 * enters the ring, not when the speaker reaches it. The ring
	 * therefore stays one host period above empty (512 frames, the
	 * CoreAudio buffer, about 12 ms). Each wake plays the one buffer
	 * already queued and holds the buffer the completion just
	 * submitted. The clock moves by that one buffer, for whatever
	 * size the movie submitted, when the speaker is about to play it.
	 * A deeper cushion leads every movie by the same fixed amount. */
	SB_LOW_WATER = 512
};

static bool sb_active(void)
{
	return nw_sheepblaster_ready() != 0;
}

/* The mixer calls the buffer completion from inside GetSourceData, and
 * the completion calls PlaySourceBuffer. Doing that inline decodes the
 * rest of the song before the Finder runs. Keep the block and give it
 * to the mixer on the next wake. */
/* Two slots so the buffer the mixer is reading stays intact when its
 * completion submits the next one. 64 KB covers a stereo 16-bit slice
 * of up to 16384 frames; larger slices keep the caller's pointer. */
enum { SB_HOLD_BYTES = 65536, SB_HOLD_SLOT = 192 + SB_HOLD_BYTES };
static uint32 sb_hold_mem[2];
static int sb_hold_cur;
static int sb_hold_flushing;
static int sb_hold_on;
static int sb_hold_lock;

static void sb_hold_store(uint32 params)
{
	int slot = sb_hold_flushing ? (sb_hold_cur ^ 1) : sb_hold_cur;
	uint32 base = sb_hold_mem[slot];
	if (base == 0 || params == 0)
		return;
	for (int i = 0; i < 32; i++)
		WriteMacInt8(base + i, ReadMacInt8(params + i));
	uint32 pb = ReadMacInt32(params + cp_params + 4);
	if (pb) {
		for (int i = 0; i < 128; i++)
			WriteMacInt8(base + 64 + i, ReadMacInt8(pb + i));
		WriteMacInt32(base + cp_params + 4, base + 64);
		/* iTunes reuses this buffer on the next decode. Copy the
		 * samples now or the next wake plays torn audio. */
		uint32 frames = ReadMacInt32(pb + 4 + scd_sampleCount);
		uint32 src = ReadMacInt32(pb + 4 + scd_buffer);
		int ch = (int)ReadMacInt16(pb + 4 + scd_numChannels);
		int bits = (int)ReadMacInt16(pb + 4 + scd_sampleSize);
		if (ch < 1)
			ch = 1;
		if (bits < 8)
			bits = 8;
		int nbytes = 0;
		if (frames > 0 && frames <= 65536 && src >= 0x1000)
			nbytes = (int)frames * ch * (bits >> 3);
		if (nbytes > 0 && nbytes <= SB_HOLD_BYTES) {
			uint32 dst = base + 192;
			memcpy(Mac2HostAddr(dst), Mac2HostAddr(src), (size_t)nbytes);
			WriteMacInt32(base + 64 + 4 + scd_buffer, dst);
		}
	}
	sb_hold_on = 1;
}

static void sb_hold_flush(void)
{
	if (!sb_hold_on || audio_data == 0 || AudioStatus.mixer == 0)
		return;
	int slot = sb_hold_cur;
	sb_hold_on = 0;
	sb_hold_flushing = 1;
	M68kRegisters r;
	memset(&r, 0, sizeof r);
	r.a[0] = AudioStatus.mixer;
	r.a[1] = sb_hold_mem[slot];
	sb_hold_lock = 1;
	Execute68k(audio_data + adatDelegateCall, &r);
	sb_hold_lock = 0;
	sb_hold_flushing = 0;
	if (sb_hold_on)
		sb_hold_cur ^= 1;
}

static void sb_tm_prime(void)
{
	if (!sb_tm_installed || sb_tm_armed || sb_in_tick)
		return;
	M68kRegisters r;
	memset(&r, 0, sizeof r);
	r.a[0] = sb_tm;
	r.d[0] = SB_PRIME_MS;
	Execute68kTrap(0xa05a, &r);	// PrimeTime()
	sb_tm_armed = true;
}

static void sb_tm_install(void)
{
	if (sb_tm_installed)
		return;
	if (sb_tm == 0) {
		static const uint8 proc[] = {
			(uint8)(M68K_EMUL_OP_SHEEPBLASTER_TICK >> 8),	// A0 = task, D0 = ms or 0
			(uint8)(M68K_EMUL_OP_SHEEPBLASTER_TICK & 0xff),
			0x4a, 0x80,		// tst.l d0
			0x67, 0x02,		// beq.s @1
			0xa0, 0x5a,		// PrimeTime
			0x4e, 0x75		// @1 rts
		};
		sb_tm_proc = SheepProc(proc, sizeof proc);
		sb_tm = SheepMem::Reserve(SIZEOF_TMTask);
		sb_hold_mem[0] = SheepMem::Reserve(SB_HOLD_SLOT);
		sb_hold_mem[1] = SheepMem::Reserve(SB_HOLD_SLOT);
	}
	for (uint32 i = 0; i < SIZEOF_TMTask; i += 2)
		WriteMacInt16(sb_tm + i, 0);
	WriteMacInt32(sb_tm + tmAddr, sb_tm_proc);
	M68kRegisters r;
	memset(&r, 0, sizeof r);
	r.a[0] = sb_tm;
	Execute68kTrap(0xa458, &r);	// InsXTime()
	sb_tm_installed = true;
	sb_tm_armed = false;
}

static void sb_tm_remove(void)
{
	if (!sb_tm_installed)
		return;
	M68kRegisters r;
	memset(&r, 0, sizeof r);
	r.a[0] = sb_tm;
	Execute68kTrap(0xa059, &r);	// RmvTime()
	sb_tm_installed = false;
	sb_tm_armed = false;
}

/* One GetSourceData into the ring. False when the mixer had nothing. */
static bool sb_pull(void)
{
	uint32 ad = audio_data;
	WriteMacInt32(ad + adatStreamInfo, 0);
	WriteMacInt32(ad + adatData + scd_sampleCount, (uint32)audio_mixer_frames());
	M68kRegisters r;
	memset(&r, 0, sizeof r);
	r.a[0] = ad + adatStreamInfo;
	r.a[1] = AudioStatus.mixer;
	Execute68k(ad + adatGetSourceData, &r);
	if (audio_data != ad)
		return false;
	uint32 info = ReadMacInt32(ad + adatStreamInfo);
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
	if (buf < 0x1000 || frames == 0 || frames > 65536 || !pcm)
		return false;
	nw_sheepblaster_enable(1);
	nw_sheepblaster_play(Mac2HostAddr(buf), frames,
		format ? format : FOURCC('t','w','o','s'), ch, bits, rate);
	return true;
}

/* The first real PlaySourceBuffer used to wait for the timer, and the
 * tick then filled a cushion the speaker had not reached. Queue one
 * buffer and stop once the speaker has a host period. A pull that has
 * already taken 12 ms does not start another. */
static void sb_prime_pull(void)
{
	if (audio_data == 0 || AudioStatus.mixer == 0 || sb_sources <= 0 || !sb_run || sb_in_tick)
		return;
	sb_in_tick = true;
	nw_jit_pull_set(1);
	uint64 t_pull = GetTicks_usec();
	for (int pulls = 0; pulls < 8 && sb_run && !sb_in_mixer; pulls++) {
		if (nw_sheepblaster_pending() >= SB_LOW_WATER)
			break;
		if (pulls > 0 && GetTicks_usec() - t_pull > 12000ull)
			break;
		if (sb_hold_on)
			sb_hold_flush();
		if (!sb_pull())
			break;
	}
	nw_jit_pull_set(0);
	sb_in_tick = false;
}
#endif

/*
 *  SheepBlaster Time Manager task body. Returns the delay before the
 *  next run in milliseconds, or 0 to stop; *task is the TMTask.
 */

int32 AudioSheepBlasterTick(uint32 *task)
{
#if defined(SHEEPSHAVER)
	*task = sb_tm;
	sb_tm_armed = false;
	if (!sb_tm_installed || sb_in_tick)
		return 0;
	if (sb_in_mixer) {
		sb_tm_armed = true;
		return SB_PULL_MS;
	}
	if (audio_data == 0 || AudioStatus.mixer == 0 || sb_sources <= 0 || !sb_run)
		return 0;
	/* Speaker still has a host period. Stay out of the mixer, and
	 * wake before that period runs out. */
	int pending = nw_sheepblaster_pending();
	if (pending >= SB_LOW_WATER) {
		int ms = (pending - SB_LOW_WATER) / 44;
		if (ms < 1)
			ms = 1;
		if (ms > 11)
			ms = 11;
		sb_tm_armed = true;
		return ms;
	}
	static int sb_got, sb_empty;
	static uint64 sb_last_us;
	uint64 wake = GetTicks_usec();
	uint64 elapsed = sb_last_us ? wake - sb_last_us : 10000ull;
	if (elapsed > 50000ull)
		elapsed = 50000ull;
	int owed = (int)((elapsed * 44100ull) / 1000000ull);
	if (owed < 1)
		owed = 1;
	sb_in_tick = true;
	/* One queued buffer per pull. The completion's next buffer is
	 * held, so this wake cannot walk the rest of the movie. Stop
	 * once the speaker has a host period, or after 12 ms. */
	int gained = 0;
	uint64 spent = 0;
	uint64 t_pull = GetTicks_usec();
	nw_jit_pull_set(1);
	for (int pulls = 0; sb_run && !sb_in_mixer && pulls < 3 && gained < owed; pulls++) {
		if (nw_sheepblaster_pending() >= SB_LOW_WATER)
			break;
		if (pulls > 0 && GetTicks_usec() - t_pull > 12000ull)
			break;
		if (sb_hold_on)
			sb_hold_flush();
		int before = nw_sheepblaster_pending();
		uint64 t0 = GetTicks_usec();
		if (!sb_pull()) {
			spent += GetTicks_usec() - t0;
			break;
		}
		spent += GetTicks_usec() - t0;
		int after = nw_sheepblaster_pending();
		if (after > before)
			gained += after - before;
	}
	nw_jit_pull_set(0);
	sb_in_tick = false;
	sb_last_us = GetTicks_usec();
	{
		static uint64 acc_us, acc_fr, win;
		if (win == 0)
			win = GetTicks_usec();
		acc_us += spent;
		if (gained > 0)
			acc_fr += (uint64)gained;
		uint64 now = GetTicks_usec();
		if (now - win >= 1000000ull) {
#if NW_BOOT_LOG
			double audio_s = (double)acc_fr / 44100.0;
			double cpu_s = (double)acc_us / 1000000.0;
			double ratio = audio_s > 0.001 ? cpu_s / audio_s : 0.0;
			printf("NW-BOOT G1: sb-cost cpu_us=%llu frames=%llu cpu_per_audio=%.3f\n",
			       (unsigned long long)acc_us, (unsigned long long)acc_fr, ratio);
			nw_jit_itunes_log(acc_fr);
			nw_jit_pull_log();
			fflush(stdout);
#endif
			acc_us = acc_fr = 0;
			win = now;
		}
	}
	if (!sb_tm_installed || audio_data == 0 || AudioStatus.mixer == 0 || sb_sources <= 0 || !sb_run) {
		sb_got = 0;
		sb_empty = 0;
		return 0;
	}
	if (gained > 0) {
		sb_got = 1;
		sb_empty = 0;
		sb_tm_armed = true;
		/* One host buffer is 512 frames, 11.6 ms at 44100 (86 Hz).
		 * The pull already used `spent` of that. Adding a fixed 10 ms
		 * made the period about 16 ms, 60 buffers a second, ~32000
		 * frames. A pull slower than one period still waits 10 ms so
		 * a decode does not own the CPU. */
		int ms = 12 - (int)(spent / 1000ull);
		if (ms < 1)
			ms = 1;
		if (ms > SB_PULL_MS || spent > 12000ull)
			ms = SB_PULL_MS;
		return ms;
	}
	/* A movie stalls while a frame is drawn, or while the host window
	 * is in the background. Stopping here is what left QuickTime silent
	 * after a click or a focus change. Stay armed and poll slower. */
	if (sb_got)
		sb_empty++;
	sb_tm_armed = true;
	return sb_empty >= 3 ? SB_IDLE_MAX_MS : SB_PULL_MS;
#else
	*task = 0;
	return 0;
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
		static int n_other;
		bool probe = selector == kComponentOpenSelect ||
			selector == kComponentCloseSelect ||
			selector == kSoundComponentGetInfoSelect;
		if (!probe && n_other < 20) {
			n_other++;
			printf("NW-BOOT G1: audio-sel other #%d sel=%d\n", n_other, (int)selector);
			fflush(stdout);
		} else if (n_sel < 8) {
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

				// Allocate global data area. System heap: the first
				// Open can come from an application (the Sound control
				// panel), and its heap is gone once it quits while this
				// area and its 68k routines stay in use.
				r.d[0] = SIZEOF_adat;
				Execute68kTrap(0xa440, &r);	// ResrvMemSys()
				r.d[0] = SIZEOF_adat;
				Execute68kTrap(0xa71e, &r);	// NewPtrSysClear()
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
					sb_tm_remove();
					sb_sources = 0;
					sb_run = false;
#endif
					if (AudioStatus.mixer) {
#if defined(SHEEPSHAVER)
						/* 9.2.1 hands back an odd mixer id (this run:
						 * 0081000b). CloseMixer dereferences it and the
						 * guest takes SysError(2) as QuickTime opens. */
						if (AudioStatus.mixer & 1) {
							printf("NW-BOOT G1: audio-close skip mixer=%08x\n",
							       (unsigned)AudioStatus.mixer);
							fflush(stdout);
						} else
#endif
						{
						r.a[0] = AudioStatus.mixer;
						Execute68k(audio_data + adatCloseMixer, &r);
						D(bug(" CloseMixer() returns %08lx, mixer %08lx\n", r.d[0], AudioStatus.mixer));
						}
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
		case kSoundComponentInitOutputDeviceSelect: {
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
			WriteMacInt32(audio_data + adatData + scd_sampleCount, audio_mixer_frames());
			WriteMacInt32(audio_data + adatData + scd_buffer, 0);
			WriteMacInt32(audio_data + adatData + scd_reserved, 0);
			WriteMacInt32(audio_data + adatStreamInfo, 0);

			printf("NW-BOOT G1: audio-init enter\n");
			fflush(stdout);
			// Open Apple Mixer in the system heap; the calling
			// application's heap can go away while the mixer is in use
			uint32 zone = ReadMacInt32(0x118);	// TheZone
			WriteMacInt32(0x118, ReadMacInt32(0x2a6));	// SysZone
			r.a[0] = audio_data + adatMixer;
			r.d[0] = 0;
			r.a[1] = audio_data + adatData;
			Execute68k(audio_data + adatOpenMixer, &r);
			WriteMacInt32(0x118, zone);
			AudioStatus.mixer = ReadMacInt32(audio_data + adatMixer);
			printf("NW-BOOT G1: audio-mixer err=%08x mixer=%08x\n",
			       (unsigned)r.d[0], (unsigned)AudioStatus.mixer);
			fflush(stdout);
#if defined(SHEEPSHAVER)
			if (sb_active() && AudioStatus.mixer) {
				nw_sheepblaster_enable(1);
				sb_tm_install();
			}
			if (r.d[0] != 0)
				return noErr;
#endif
			return r.d[0];
		}

		case kSoundComponentGetSourceSelect:
			D(bug(" GetSource source %08lx\n", ReadMacInt32(p)));
			WriteMacInt32(ReadMacInt32(p), AudioStatus.mixer);
			return noErr;

		// Sound component functions (delegated)
		case kSoundComponentAddSourceSelect:
			D(bug(" AddSource\n"));
#if defined(SHEEPSHAVER)
			if (sb_active())
				sb_sources++;
			else
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
			if (sb_active()) {
				if (sb_sources > 0)
					sb_sources--;
				if (sb_sources <= 0)
					sb_run = false;
				/* Stop from inside GetSourceData must not enter the
				 * mixer again. That reentry bus-errored QuickTime
				 * (error type 1, pc 0x00000c0c) right after RemoveSource. */
				if (sb_in_tick)
					return noErr;
			} else
#endif
			if (AudioStatus.num_sources > 0)
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
			if (audio_data == 0 || AudioStatus.mixer == 0)
				return noErr;
			D(bug(" StartSource count %d\n", ReadMacInt16(p + 4)));
			D(bug(" starting Apple Mixer\n"));
			r.d[0] = ReadMacInt16(p + 4);
			r.a[0] = ReadMacInt32(p);
			r.a[1] = AudioStatus.mixer;
			Execute68k(audio_data + adatStartSource, &r);
			D(bug(" returns %08lx\n", r.d[0]));
#if defined(SHEEPSHAVER)
			if (sb_active()) {
				sb_run = true;
				sb_tm_prime();
			}
#endif
			return noErr;

		case kSoundComponentPlaySourceBufferSelect:
#if defined(SHEEPSHAVER)
			if (sb_active()) {
				static int n_psb;
				uint32 pb = ReadMacInt32(p + 4);
				uint32 completion = pb ? ReadMacInt32(pb + 52) : 0;
				if (n_psb < 8) {
					n_psb++;
					printf("NW-BOOT G1: sheepblaster psb #%d pb=%08x frames=%u completion=%08x sources=%d\n",
					       n_psb, (unsigned)pb,
					       pb ? (unsigned)ReadMacInt32(pb + 4 + scd_sampleCount) : 0u,
					       (unsigned)completion, sb_sources);
					fflush(stdout);
				}
				if (audio_data == 0 || AudioStatus.mixer == 0)
					return noErr;
				/* Inside GetSourceData, or while the mixer is taking the
				 * buffer we just flushed. That completion is the next
				 * movie slice. Queuing it in this same pull would mark
				 * it played before the speaker reaches it. Hold it. */
				if (sb_in_tick || sb_hold_lock) {
					sb_hold_store(params);
					sb_run = true;
					return noErr;
				}
				r.a[0] = AudioStatus.mixer;
				r.a[1] = params;
				sb_in_mixer++;
				Execute68k(audio_data + adatDelegateCall, &r);
				sb_in_mixer--;
				sb_run = true;
				/* The 512-frame probe has no completion; Stop follows
				 * it and a pull here would decode into a ring that is
				 * about to be dropped. The real buffer (completion set)
				 * queues the first slice before QuickTime moves on. */
				if (completion != 0)
					sb_prime_pull();
				sb_tm_prime();
				return r.d[0];
			}
#endif
			goto delegate;

		case kSoundComponentStopSourceSelect:
			D(bug(" StopSource\n"));
#if defined(SHEEPSHAVER)
			sb_run = false;
			if (sb_in_tick)
				return noErr;
#endif
			goto delegate;

		case kSoundComponentPauseSourceSelect:
			D(bug(" PauseSource\n"));
#if defined(SHEEPSHAVER)
			sb_run = false;
			if (sb_in_tick)
				return noErr;
#endif
delegate:	// Delegate call to Apple Mixer
			D(bug(" delegating call to Apple Mixer\n"));
			if (audio_data == 0 || AudioStatus.mixer == 0)
				return noErr;
#if defined(SHEEPSHAVER)
			sb_in_mixer++;
#endif
			r.a[0] = AudioStatus.mixer;
			r.a[1] = params;
			Execute68k(audio_data + adatDelegateCall, &r);
#if defined(SHEEPSHAVER)
			sb_in_mixer--;
#endif
			D(bug(" returns %08lx\n", r.d[0]));
			return r.d[0];

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
