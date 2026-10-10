/*
 *  sndchan.c - Sheep Shears self-test: open a sound channel, play a short tone, dispose of the channel.
 *
 *  Start-up Items app, run by tools/shears/tests/sndchan.sh. Opening a channel makes the Sound Manager open the output
 *  device component (SheepBlaster) and the Apple Mixer; disposing of it closes them again. The app reports through the
 *  Shears LOG command (visible with NW_VERBOSE=1) and repeats the cycle four times. (Keeping the mixer open across
 *  the device's Close, tried as a cure for a crash elsewhere, hangs the guest after the first cycle: this test shows it.)
 *
 *  (C) 2026 Bill Cavalieri
 */

#include <Quickdraw.h>
#include <Events.h>
#include <Sound.h>
#include <Memory.h>
#include <stdio.h>
#include "shears_client.h"

typedef struct {        /* SoundHeader (standard, 8-bit) */
    Ptr samplePtr;
    unsigned long length, sampleRate, loopStart, loopEnd;
    unsigned char encode, baseFrequency;
    unsigned char sampleArea[2];
} MySoundHeader;

static void Wait(long ticks)
{
    EventRecord ev;
    unsigned long end = TickCount() + ticks;
    while (TickCount() < end)
        WaitNextEvent(everyEvent, &ev, 2, NULL);
}

int main(void)
{
    EventRecord ev;
    char line[100];
    int cycle;
    long i;
    unsigned char *tone;
    MySoundHeader hdr;

    if (!ShearsOpen())
        return 0;
    InitGraf(&qd.thePort);
    if (!Hello())
        return 0;
    Wait(12 * 60);

    tone = (unsigned char *)NewPtr(5512);           /* a quarter of a second at 22050 Hz: a square-ish 441 Hz */
    for (i = 0; i < 5512; i++)
        tone[i] = (unsigned char)(((i / 25) & 1) ? 160 : 96);
    hdr.samplePtr = (Ptr)tone;
    hdr.length = 5512;
    hdr.sampleRate = 22050UL << 16;
    hdr.loopStart = hdr.loopEnd = 0;
    hdr.encode = 0;
    hdr.baseFrequency = 60;

    for (cycle = 1; cycle <= 4; cycle++) {
        SndChannelPtr ch = NULL;
        SndCommand cmd;
        OSErr err = SndNewChannel(&ch, sampledSynth, 0, NULL);
        sprintf(line, "SC %d new err=%d", cycle, (int)err);
        ShearsLog(line);
        if (err == noErr) {
            cmd.cmd = bufferCmd;
            cmd.param1 = 0;
            cmd.param2 = (long)&hdr;
            err = SndDoCommand(ch, &cmd, false);
            sprintf(line, "SC %d play err=%d", cycle, (int)err);
            ShearsLog(line);
            Wait(40);
            err = SndDisposeChannel(ch, true);
            sprintf(line, "SC %d dispose err=%d", cycle, (int)err);
            ShearsLog(line);
        }
        Wait(60);
        sprintf(line, "SC %d alive", cycle);
        ShearsLog(line);
    }
    ShearsLog("SC done");
    for (;;)
        WaitNextEvent(everyEvent, &ev, 30, NULL);
    return 0;
}
