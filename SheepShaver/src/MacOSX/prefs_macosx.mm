/*
 *  prefs_macosx.mm - Mac preferences entry. The window is SwiftUI.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "sysdeps.h"
#include "prefs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#import <Cocoa/Cocoa.h>
#import <AudioToolbox/AudioToolbox.h>

void prefs_init(void)
{
}

void prefs_exit(void)
{
}

extern "C" const char *SheepPrefsGetString(const char *key)
{
	const char *s = PrefsFindString(key);
	return s ? s : "";
}

extern "C" void SheepPrefsSetString(const char *key, const char *value)
{
	PrefsReplaceString(key, value ? value : "");
}

extern "C" int SheepPrefsGetBool(const char *key)
{
	return PrefsFindBool(key) ? 1 : 0;
}

extern "C" void SheepPrefsSetBool(const char *key, int value)
{
	PrefsReplaceBool(key, value != 0);
}

extern "C" int SheepPrefsGetInt(const char *key)
{
	return (int)PrefsFindInt32(key);
}

extern "C" void SheepPrefsSetInt(const char *key, int value)
{
	PrefsReplaceInt32(key, value);
}

extern "C" void SheepPrefsSave(void)
{
	SavePrefs();
}

extern "C" const char *SheepPrefsPath(void)
{
	extern std::string UserPrefsPath;
	return UserPrefsPath.c_str();
}

static AudioQueueRef gChimeQueue;

static void chime_played(void *user, AudioQueueRef queue, AudioQueueBufferRef buffer)
{
	(void)user;
	(void)queue;
	(void)buffer;
}

/*
 *  Host startup chime. The emulator occupies the main thread, and the
 *  event pump does not stay in the run loop, so NSSound never starts.
 *  AudioQueue plays on the HAL thread.
 */
void PlayStartupSound(void)
{
	if (PrefsFindBool("nosound") || !PrefsFindBool("bootchime"))
		return;

	NSString *path = [[NSBundle mainBundle]
		pathForResource:@"sheep_baa_chime" ofType:@"wav"];
	if (path == nil) {
		printf("boot chime: sheep_baa_chime.wav is not in the app bundle\n");
		fflush(stdout);
		return;
	}

	AudioFileID file = NULL;
	NSURL *url = [NSURL fileURLWithPath:path];
	if (AudioFileOpenURL((CFURLRef)url, kAudioFileReadPermission, 0, &file) != 0 || file == NULL) {
		printf("boot chime: could not open %s\n", path.UTF8String);
		fflush(stdout);
		return;
	}

	AudioStreamBasicDescription fmt;
	memset(&fmt, 0, sizeof(fmt));
	UInt32 size = sizeof(fmt);
	if (AudioFileGetProperty(file, kAudioFilePropertyDataFormat, &size, &fmt) != 0) {
		AudioFileClose(file);
		return;
	}

	UInt64 nbytes64 = 0;
	size = sizeof(nbytes64);
	AudioFileGetProperty(file, kAudioFilePropertyAudioDataByteCount, &size, &nbytes64);
	if (nbytes64 == 0 || nbytes64 > 8 * 1024 * 1024) {
		AudioFileClose(file);
		return;
	}

	UInt32 nbytes = (UInt32)nbytes64;
	void *pcm = malloc(nbytes);
	if (pcm == NULL) {
		AudioFileClose(file);
		return;
	}
	if (AudioFileReadBytes(file, false, 0, &nbytes, pcm) != 0) {
		free(pcm);
		AudioFileClose(file);
		return;
	}
	AudioFileClose(file);

	if (gChimeQueue != NULL) {
		AudioQueueStop(gChimeQueue, true);
		AudioQueueDispose(gChimeQueue, true);
		gChimeQueue = NULL;
	}

	if (AudioQueueNewOutput(&fmt, chime_played, NULL, NULL, NULL, 0, &gChimeQueue) != 0) {
		free(pcm);
		printf("boot chime: AudioQueueNewOutput failed\n");
		fflush(stdout);
		return;
	}

	AudioQueueBufferRef buffer = NULL;
	if (AudioQueueAllocateBuffer(gChimeQueue, nbytes, &buffer) != 0) {
		free(pcm);
		AudioQueueDispose(gChimeQueue, true);
		gChimeQueue = NULL;
		return;
	}
	memcpy(buffer->mAudioData, pcm, nbytes);
	buffer->mAudioDataByteSize = nbytes;
	free(pcm);

	AudioQueueEnqueueBuffer(gChimeQueue, buffer, 0, NULL);
	AudioQueueSetParameter(gChimeQueue, kAudioQueueParam_Volume, 1.0f);
	if (AudioQueueStart(gChimeQueue, NULL) != 0) {
		printf("boot chime: AudioQueueStart failed\n");
		fflush(stdout);
	}
}
