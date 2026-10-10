/*
 *  mic_capture_macosx.cpp - Host microphone capture for the guest's sound input (CoreAudio)
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 *
 *  One AUHAL unit on the default input device. Its callback converts what the device delivers (any rate, one or
 *  more channels, float) to 44100 Hz stereo 16-bit with linear interpolation and hands it to nw_mic_push(), the
 *  ring that nw_sound_input.cpp reads. The unit runs only while the guest has the sound input open, so the macOS
 *  microphone indicator is on only then, and the permission prompt appears the first time a guest records.
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
#include "sysdeps.h"
#include "nw_log.h"
#include "nw_sound_input.h"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <stdlib.h>
#include <string.h>

static AudioUnit g_unit;
static bool g_running;
static double g_in_rate = 48000.0;
static UInt32 g_in_channels = 1;
static float *g_buf;			// interleaved float, g_in_channels per frame
static UInt32 g_buf_frames;
static AudioBufferList *g_abl;

/* linear-interpolating converter state: input position between g_prev and the current frame */
static double g_pos;
static float g_prev[2];
static bool g_have_prev;

static inline int16_t to_s16(float v)
{
	v *= 32767.0f;
	return (int16_t)(v > 32767.0f ? 32767.0f : v < -32768.0f ? -32768.0f : v);
}

static OSStatus input_callback(void *, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts, UInt32 bus,
                               UInt32 frames, AudioBufferList *)
{
	if (frames > g_buf_frames)
		return noErr;		// bigger than the buffer made for it: drop (never happens at the sizes CoreAudio uses)
	g_abl->mBuffers[0].mDataByteSize = frames * g_in_channels * sizeof(float);
	g_abl->mBuffers[0].mData = g_buf;
	OSStatus err = AudioUnitRender(g_unit, flags, ts, bus, frames, g_abl);
	if (err != noErr)
		return err;
	const double step = g_in_rate / 44100.0;		// input frames per output frame
	int16_t out[512 * 2];
	int n = 0;
	for (UInt32 i = 0; i < frames; i++) {
		float cur[2];
		cur[0] = g_buf[i * g_in_channels];
		cur[1] = g_in_channels > 1 ? g_buf[i * g_in_channels + 1] : cur[0];
		if (!g_have_prev) {
			g_prev[0] = cur[0];
			g_prev[1] = cur[1];
			g_have_prev = true;
			g_pos = 0.0;
			continue;
		}
		// g_pos is how far past g_prev the next output sample lies, in input frames (0..1 lands between prev and cur)
		while (g_pos < 1.0) {
			float f = (float)g_pos;
			out[n * 2] = to_s16(g_prev[0] + (cur[0] - g_prev[0]) * f);
			out[n * 2 + 1] = to_s16(g_prev[1] + (cur[1] - g_prev[1]) * f);
			g_pos += step;
			if (++n == 512) {
				nw_mic_push(out, (uint32)n);
				n = 0;
			}
		}
		g_pos -= 1.0;
		g_prev[0] = cur[0];
		g_prev[1] = cur[1];
	}
	if (n)
		nw_mic_push(out, (uint32)n);
	return noErr;
}

static bool fail(const char *what, OSStatus err)
{
	NW_DIAG("NW-MIC: microphone: %s failed (%d)\n", what, (int)err);
	if (g_unit) {
		AudioComponentInstanceDispose(g_unit);
		g_unit = NULL;
	}
	return false;
}

bool nw_mic_source_start(void)
{
	if (g_running)
		return true;
	int auth = nw_mic_authorization();
	if (auth == 2 || auth == 1) {
		printf("WARNING: the microphone is %s for SheepShaver. Allow it in System Settings > Privacy & Security > Microphone; the guest records silence until then.\n",
		       auth == 2 ? "turned off" : "restricted");
		fflush(stdout);
	} else if (auth == 0) {
		nw_mic_request_access();
	}
	AudioComponentDescription desc = {};
	desc.componentType = kAudioUnitType_Output;
	desc.componentSubType = kAudioUnitSubType_HALOutput;
	desc.componentManufacturer = kAudioUnitManufacturer_Apple;
	AudioComponent comp = AudioComponentFindNext(NULL, &desc);
	if (!comp)
		return fail("find the HAL unit", -1);
	OSStatus err = AudioComponentInstanceNew(comp, &g_unit);
	if (err != noErr)
		return fail("create the unit", err);

	UInt32 on = 1, off = 0;
	err = AudioUnitSetProperty(g_unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &on, sizeof on);
	if (err != noErr)
		return fail("enable input", err);
	err = AudioUnitSetProperty(g_unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &off, sizeof off);
	if (err != noErr)
		return fail("disable output", err);

	AudioObjectPropertyAddress addr = { kAudioHardwarePropertyDefaultInputDevice, kAudioObjectPropertyScopeGlobal,
		kAudioObjectPropertyElementMain };
	AudioDeviceID dev = kAudioObjectUnknown;
	UInt32 sz = sizeof dev;
	err = AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, NULL, &sz, &dev);
	if (err != noErr || dev == kAudioObjectUnknown)
		return fail("find the default input device", err ? err : -1);
	err = AudioUnitSetProperty(g_unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &dev, sizeof dev);
	if (err != noErr)
		return fail("select the input device", err);

	AudioStreamBasicDescription hw = {};
	sz = sizeof hw;
	err = AudioUnitGetProperty(g_unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 1, &hw, &sz);
	if (err != noErr)
		return fail("read the device format", err);
	g_in_rate = hw.mSampleRate > 0 ? hw.mSampleRate : 48000.0;
	g_in_channels = hw.mChannelsPerFrame >= 2 ? 2 : 1;

	AudioStreamBasicDescription fmt = {};
	fmt.mSampleRate = g_in_rate;
	fmt.mFormatID = kAudioFormatLinearPCM;
	fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
	fmt.mChannelsPerFrame = g_in_channels;
	fmt.mBitsPerChannel = 32;
	fmt.mBytesPerFrame = fmt.mChannelsPerFrame * 4;
	fmt.mFramesPerPacket = 1;
	fmt.mBytesPerPacket = fmt.mBytesPerFrame;
	err = AudioUnitSetProperty(g_unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &fmt, sizeof fmt);
	if (err != noErr)
		return fail("set the capture format", err);

	g_buf_frames = 8192;
	free(g_buf);
	g_buf = (float *)calloc(g_buf_frames * g_in_channels, sizeof(float));
	if (!g_abl)
		g_abl = (AudioBufferList *)calloc(1, sizeof(AudioBufferList));
	g_abl->mNumberBuffers = 1;
	g_abl->mBuffers[0].mNumberChannels = g_in_channels;

	AURenderCallbackStruct cb = { input_callback, NULL };
	err = AudioUnitSetProperty(g_unit, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 0, &cb, sizeof cb);
	if (err != noErr)
		return fail("set the input callback", err);
	err = AudioUnitInitialize(g_unit);
	if (err != noErr)
		return fail("initialize the unit", err);
	g_have_prev = false;
	err = AudioOutputUnitStart(g_unit);
	if (err != noErr)
		return fail("start capture", err);
	g_running = true;
	NW_DIAG("NW-MIC: microphone capture started (%.0f Hz, %u channel%s)\n", g_in_rate, (unsigned)g_in_channels,
	       g_in_channels == 1 ? "" : "s");
	return true;
}

void nw_mic_source_stop(void)
{
	if (!g_unit)
		return;
	AudioOutputUnitStop(g_unit);
	AudioUnitUninitialize(g_unit);
	AudioComponentInstanceDispose(g_unit);
	g_unit = NULL;
	g_running = false;
	NW_DIAG("NW-MIC: microphone capture stopped\n");
}
