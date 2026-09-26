/*
 *  AudioDevice.cpp - Core Audio device properties
 *
 *  Apple sample, 2004. Property calls updated to AudioObjectGetPropertyData.
 *
 *  Basilisk II (C) 1997-2008 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "AudioDevice.h"

#include <stdlib.h>
#include <string.h>
#include <CoreFoundation/CoreFoundation.h>

static AudioObjectPropertyScope device_scope(bool isInput)
{
	return isInput ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput;
}

static OSStatus device_get(AudioObjectID id, AudioObjectPropertySelector sel,
			   AudioObjectPropertyScope scope, void *out, UInt32 *size)
{
	AudioObjectPropertyAddress addr;
	addr.mSelector = sel;
	addr.mScope = scope;
	addr.mElement = kAudioObjectPropertyElementMain;
	return AudioObjectGetPropertyData(id, &addr, 0, NULL, size, out);
}

static OSStatus device_set(AudioObjectID id, AudioObjectPropertySelector sel,
			   AudioObjectPropertyScope scope, const void *in, UInt32 size)
{
	AudioObjectPropertyAddress addr;
	addr.mSelector = sel;
	addr.mScope = scope;
	addr.mElement = kAudioObjectPropertyElementMain;
	return AudioObjectSetPropertyData(id, &addr, 0, NULL, size, in);
}

void AudioDevice::Init(AudioDeviceID devid, bool isInput)
{
	mID = devid;
	mIsInput = isInput;
	mSafetyOffset = 0;
	mBufferSizeFrames = 0;
	memset(&mFormat, 0, sizeof(mFormat));
	if (mID == kAudioDeviceUnknown)
		return;

	AudioObjectPropertyScope scope = device_scope(mIsInput);
	UInt32 size = sizeof(mSafetyOffset);
	device_get(mID, kAudioDevicePropertySafetyOffset, scope, &mSafetyOffset, &size);
	size = sizeof(mBufferSizeFrames);
	device_get(mID, kAudioDevicePropertyBufferFrameSize, scope, &mBufferSizeFrames, &size);
	size = sizeof(mFormat);
	device_get(mID, kAudioDevicePropertyStreamFormat, scope, &mFormat, &size);
}

void AudioDevice::SetBufferSize(UInt32 size)
{
	AudioObjectPropertyScope scope = device_scope(mIsInput);
	device_set(mID, kAudioDevicePropertyBufferFrameSize, scope, &size, sizeof(size));
	UInt32 got = sizeof(mBufferSizeFrames);
	device_get(mID, kAudioDevicePropertyBufferFrameSize, scope, &mBufferSizeFrames, &got);
}

int AudioDevice::CountChannels()
{
	AudioObjectPropertyAddress addr;
	addr.mSelector = kAudioDevicePropertyStreamConfiguration;
	addr.mScope = device_scope(mIsInput);
	addr.mElement = kAudioObjectPropertyElementMain;
	UInt32 propSize = 0;
	if (AudioObjectGetPropertyDataSize(mID, &addr, 0, NULL, &propSize) != noErr || propSize == 0)
		return 0;
	AudioBufferList *buflist = (AudioBufferList *)malloc(propSize);
	if (!buflist)
		return 0;
	int result = 0;
	if (AudioObjectGetPropertyData(mID, &addr, 0, NULL, &propSize, buflist) == noErr) {
		for (UInt32 i = 0; i < buflist->mNumberBuffers; ++i)
			result += (int)buflist->mBuffers[i].mNumberChannels;
	}
	free(buflist);
	return result;
}

char *AudioDevice::GetName(char *buf, UInt32 maxlen)
{
	if (!buf || maxlen == 0)
		return buf;
	buf[0] = 0;
	CFStringRef name = NULL;
	UInt32 size = sizeof(name);
	AudioObjectPropertyAddress addr;
	addr.mSelector = kAudioObjectPropertyName;
	addr.mScope = kAudioObjectPropertyScopeGlobal;
	addr.mElement = kAudioObjectPropertyElementMain;
	if (AudioObjectGetPropertyData(mID, &addr, 0, NULL, &size, &name) == noErr && name) {
		CFStringGetCString(name, buf, (CFIndex)maxlen, kCFStringEncodingUTF8);
		CFRelease(name);
	}
	return buf;
}
