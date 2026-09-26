/*
 *  AudioBackEnd.cpp - Default Output AudioUnit and a two-slot ring
 *
 *  Based on Apple example software, Daniel Sumorok, 2004-2006.
 *  Rewritten 2026: one AudioUnit, no AUGraph. The HAL thread copies one
 *  period. The emulator thread fills the other. PCM in the ring is
 *  host-endian signed integer.
 *
 *  Basilisk II (C) 1997-2008 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "AudioBackEnd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void check_err(OSStatus err, const char *file, int line)
{
	if (err)
		fprintf(stderr, "AudioBackEnd Error: %d -> %s: %d\n", (int)err, file, line);
}

#define checkErr(err) check_err((OSStatus)(err), __FILE__, __LINE__)

static std::atomic<uint64_t> g_audio_cb_n;
static std::atomic<uint64_t> g_audio_cb_us_max;

static uint64_t mono_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

extern "C" void AudioBackEndTakeStats(uint64_t *callbacks, uint64_t *max_us)
{
	if (callbacks)
		*callbacks = g_audio_cb_n.exchange(0, std::memory_order_relaxed);
	if (max_us)
		*max_us = g_audio_cb_us_max.exchange(0, std::memory_order_relaxed);
}

static UInt32 ring_add(UInt32 index, UInt32 bytes, UInt32 size)
{
	index += bytes;
	if (index >= size)
		index -= size;
	return index;
}

AudioBackEnd::AudioBackEnd(int bitsPerSample, int numChannels, int sampleRate):
  mOutputUnit(NULL),
  mBitsPerSample(bitsPerSample),
  mSampleRate(sampleRate),
  mNumChannels(numChannels),
  mCallback(NULL),
  mCallbackArg(NULL),
  mBufferSizeFrames(0),
  mFramesProcessed(0),
  mAudioBuffer(NULL),
  mAudioBufferWriteIndex(0),
  mAudioBufferReadIndex(0),
  mBytesPerFrame(0),
  mAudioBufferSize(0),
  mPeriodBytes(0) {
  OSStatus err = Init();
  if (err) {
    fprintf(stderr, "AudioBackEnd ERROR: Cannot Init AudioBackEnd\n");
    exit(1);
  }
}

AudioBackEnd::~AudioBackEnd() {
  Stop();
  if (mOutputUnit) {
    AudioUnitUninitialize(mOutputUnit);
    AudioComponentInstanceDispose(mOutputUnit);
    mOutputUnit = NULL;
  }
  delete[] mAudioBuffer;
  mAudioBuffer = NULL;
}

OSStatus AudioBackEnd::Init() {
  OSStatus err = SetupUnit();
  checkErr(err);
  if (err)
    return err;
  err = SetupBuffers();
  checkErr(err);
  if (err)
    return err;
  err = AudioUnitInitialize(mOutputUnit);
  checkErr(err);
  if (err)
    return err;

  /* The callback's inNumberFrames is the unit's buffer, which can
   * differ from the device property read before the format was set.
   * A mismatch walks the read index off the two slots and plays silence. */
  UInt32 frames = 0;
  UInt32 size = sizeof(frames);
  if (AudioUnitGetProperty(mOutputUnit, kAudioDevicePropertyBufferFrameSize,
                           kAudioUnitScope_Global, 0, &frames, &size) == noErr &&
      frames > 0 && frames != mBufferSizeFrames) {
    mBufferSizeFrames = frames;
    delete[] mAudioBuffer;
    mPeriodBytes = mBytesPerFrame * mBufferSizeFrames;
    mAudioBufferSize = mPeriodBytes * 2;
    mAudioBuffer = new UInt8[mAudioBufferSize];
    memset(mAudioBuffer, 0, mAudioBufferSize);
  }
  printf("AudioBackEnd: %u Hz %u ch %u-bit buffer %u frames\n",
         (unsigned)mSampleRate, (unsigned)mNumChannels,
         (unsigned)mBitsPerSample, (unsigned)mBufferSizeFrames);
  fflush(stdout);
  return noErr;
}

OSStatus AudioBackEnd::Start()
{
  if (IsRunning())
    return noErr;
  mFramesProcessed = 0;
  mAudioBufferWriteIndex.store(0, std::memory_order_relaxed);
  mAudioBufferReadIndex.store(0, std::memory_order_relaxed);
  if (mAudioBuffer && mAudioBufferSize)
    memset(mAudioBuffer, 0, mAudioBufferSize);
  OSStatus err = AudioOutputUnitStart(mOutputUnit);
  if (err) {
    printf("AudioBackEnd: start failed %d\n", (int)err);
    fflush(stdout);
  }
  return err;
}

OSStatus AudioBackEnd::Stop() {
  if (!mOutputUnit || !IsRunning())
    return noErr;
  return AudioOutputUnitStop(mOutputUnit);
}

Boolean AudioBackEnd::IsRunning() {
  if (!mOutputUnit)
    return false;
  UInt32 running = 0;
  UInt32 size = sizeof(running);
  if (AudioUnitGetProperty(mOutputUnit, kAudioOutputUnitProperty_IsRunning,
                           kAudioUnitScope_Global, 0, &running, &size) != noErr)
    return false;
  return running != 0;
}

OSStatus AudioBackEnd::SetupUnit() {
  AudioComponentDescription desc;
  memset(&desc, 0, sizeof(desc));
  desc.componentType = kAudioUnitType_Output;
  desc.componentSubType = kAudioUnitSubType_DefaultOutput;
  desc.componentManufacturer = kAudioUnitManufacturer_Apple;

  AudioComponent comp = AudioComponentFindNext(NULL, &desc);
  if (!comp)
    return -1;
  OSStatus err = AudioComponentInstanceNew(comp, &mOutputUnit);
  if (err)
    return err;

  AURenderCallbackStruct output;
  output.inputProc = OutputProc;
  output.inputProcRefCon = this;
  err = AudioUnitSetProperty(mOutputUnit,
                             kAudioUnitProperty_SetRenderCallback,
                             kAudioUnitScope_Input,
                             0,
                             &output,
                             sizeof(output));
  if (err)
    return err;

  AudioDeviceID dev = kAudioDeviceUnknown;
  UInt32 size = sizeof(dev);
  err = AudioUnitGetProperty(mOutputUnit,
                             kAudioOutputUnitProperty_CurrentDevice,
                             kAudioUnitScope_Global,
                             0,
                             &dev,
                             &size);
  if (err)
    return err;
  mOutputDevice.Init(dev, false);
  mBufferSizeFrames = mOutputDevice.mBufferSizeFrames;
  if (mBufferSizeFrames == 0)
    mBufferSizeFrames = 512;
  return noErr;
}

OSStatus AudioBackEnd::SetupBuffers() {
  AudioStreamBasicDescription asbd;
  memset(&asbd, 0, sizeof(asbd));
  asbd.mFormatID = kAudioFormatLinearPCM;
  asbd.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
  asbd.mChannelsPerFrame = (UInt32)mNumChannels;
  asbd.mSampleRate = mSampleRate;
  asbd.mBitsPerChannel = (UInt32)mBitsPerSample;
  asbd.mFramesPerPacket = 1;
  asbd.mBytesPerFrame = (asbd.mBitsPerChannel / 8) * asbd.mChannelsPerFrame;
  asbd.mBytesPerPacket = asbd.mBytesPerFrame;

  mBytesPerFrame = asbd.mBytesPerFrame;
  if (mBytesPerFrame == 0)
    return -1;

  OSStatus err = AudioUnitSetProperty(mOutputUnit, kAudioUnitProperty_StreamFormat,
                                      kAudioUnitScope_Input, 0, &asbd, sizeof(asbd));
  if (err)
    return err;

  delete[] mAudioBuffer;
  mPeriodBytes = mBytesPerFrame * mBufferSizeFrames;
  mAudioBufferSize = mPeriodBytes * 2;
  mAudioBuffer = new UInt8[mAudioBufferSize];
  memset(mAudioBuffer, 0, mAudioBufferSize);
  return noErr;
}

OSStatus AudioBackEnd::OutputProc(void *inRefCon,
                                  AudioUnitRenderActionFlags *,
                                  const AudioTimeStamp *,
                                  UInt32,
                                  UInt32 inNumberFrames,
                                  AudioBufferList *ioData) {
  AudioBackEnd *This = (AudioBackEnd *)inRefCon;
  const uint64_t t0 = mono_us();
  UInt8 *dst = (UInt8 *)ioData->mBuffers[0].mData;
  UInt32 bytes = inNumberFrames * This->mBytesPerFrame;

  if (!This->mAudioBuffer || This->mAudioBufferSize == 0) {
    memset(dst, 0, bytes);
    return noErr;
  }

  UInt32 read = This->mAudioBufferReadIndex.load(std::memory_order_acquire);
  UInt32 until_end = This->mAudioBufferSize - read;
  if (until_end < bytes) {
    memcpy(dst, &This->mAudioBuffer[read], until_end);
    memcpy(dst + until_end, This->mAudioBuffer, bytes - until_end);
  } else {
    memcpy(dst, &This->mAudioBuffer[read], bytes);
  }
  This->mAudioBufferReadIndex.store(ring_add(read, bytes, This->mAudioBufferSize),
                                    std::memory_order_release);

  This->mFramesProcessed += inNumberFrames;
  while (This->mFramesProcessed >= This->mBufferSizeFrames) {
    This->mFramesProcessed -= This->mBufferSizeFrames;
    if (This->mCallback)
      This->mCallback(This->mCallbackArg);
  }
  const uint64_t dt = mono_us() - t0;
  g_audio_cb_n.fetch_add(1, std::memory_order_relaxed);
  uint64_t prev = g_audio_cb_us_max.load(std::memory_order_relaxed);
  while (dt > prev && !g_audio_cb_us_max.compare_exchange_weak(prev, dt, std::memory_order_relaxed))
    ;
  return noErr;
}

void AudioBackEnd::setCallback(playthruCallback func, void *arg) {
  mCallback = func;
  mCallbackArg = arg;
}

UInt32 AudioBackEnd::BufferSizeFrames() {
  return mBufferSizeFrames;
}

int AudioBackEnd::sendAudioBuffer(void *buffer, int numFrames, int big_endian) {
  if (!mAudioBuffer || mPeriodBytes == 0)
    return 0;

  UInt32 w = mAudioBufferWriteIndex.load(std::memory_order_relaxed);
  w = ring_add(w, mPeriodBytes, mAudioBufferSize);
  UInt8 *dst = &mAudioBuffer[w];

  UInt32 nbytes = 0;
  if (buffer && numFrames > 0)
    nbytes = mBytesPerFrame * (UInt32)numFrames;
  if (nbytes > mPeriodBytes)
    nbytes = mPeriodBytes;

  if (nbytes == 0 || buffer == NULL) {
    memset(dst, 0, mPeriodBytes);
  } else if (big_endian && mBytesPerFrame >= 2) {
    const UInt8 *src = (const UInt8 *)buffer;
    UInt32 i = 0;
    for (; i + 1 < nbytes; i += 2) {
      dst[i] = src[i + 1];
      dst[i + 1] = src[i];
    }
    if (nbytes < mPeriodBytes)
      memset(dst + nbytes, 0, mPeriodBytes - nbytes);
  } else {
    memcpy(dst, buffer, nbytes);
    if (nbytes < mPeriodBytes)
      memset(dst + nbytes, 0, mPeriodBytes - nbytes);
  }

  mAudioBufferWriteIndex.store(w, std::memory_order_release);
  return numFrames;
}
