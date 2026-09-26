/*
 *  AudioBackEnd.h - Default Output AudioUnit and a two-slot ring
 *
 *  Based on Apple example software, Daniel Sumorok, 2004-2006.
 *  Rewritten 2026: no AUGraph. Pull callback, native-endian PCM.
 *
 *  Basilisk II (C) 1997-2008 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#ifndef __AudioBackEnd_H__
#define __AudioBackEnd_H__

#include <CoreAudio/CoreAudio.h>
#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <atomic>
#include <stdint.h>
#include "AudioDevice.h"

typedef void (*playthruCallback)(void *arg);

class AudioBackEnd  {
 public:
  AudioBackEnd(int bitsPerSample, int numChannels, int sampleRate);
  ~AudioBackEnd();
  OSStatus Init();
  OSStatus Start();
  OSStatus Stop();
  Boolean IsRunning();
  void setCallback(playthruCallback func, void *arg);
  UInt32 BufferSizeFrames();
  /* big_endian: guest mixer is Mac big-endian; SheepBlaster is already native. */
  int sendAudioBuffer(void *buffer, int numFrames, int big_endian = 0);
 private:
  OSStatus SetupUnit();
  OSStatus SetupBuffers();

  static OSStatus OutputProc(void *inRefCon,
                             AudioUnitRenderActionFlags *ioActionFlags,
                             const AudioTimeStamp *inTimeStamp,
                             UInt32 inBusNumber,
                             UInt32 inNumberFrames,
                             AudioBufferList *  ioData);

  AudioDevice mOutputDevice;

  AudioUnit mOutputUnit;
  int mBitsPerSample;
  int mSampleRate;
  int mNumChannels;
  playthruCallback mCallback;
  void *mCallbackArg;
  UInt32 mBufferSizeFrames;
  UInt32 mFramesProcessed;
  UInt8 *mAudioBuffer;
  std::atomic<UInt32> mAudioBufferWriteIndex;
  std::atomic<UInt32> mAudioBufferReadIndex;
  UInt32 mBytesPerFrame;
  UInt32 mAudioBufferSize;
  UInt32 mPeriodBytes;
};

#endif
