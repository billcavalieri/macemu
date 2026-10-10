/*
 *  recdlg.c - Sheep Shears self-test: the Sound Manager's record window, opened and closed without a hand on the mouse.
 *
 *  Start-up Items app, run by tools/shears/tests/recdlg.sh. It does what SimpleSound's File > New does: opens the sound
 *  input once to try the recording qualities, then calls SndRecordToFile. A filter procedure lets the window run for a
 *  few seconds (its level meter is polled all that time) and then answers "Cancel". Opening the window opens the sound
 *  input and a sound output channel; closing it closes both. The app reports through the Shears LOG command (visible
 *  with NW_VERBOSE=1) and then only has to stay alive.
 *
 *  Build options (CMakeLists.txt): RD_QUALITY, RD_PROBE, and RD_SECONDS or RD_CALLS for how long the window stays.
 *
 *  (C) 2026 Bill Cavalieri
 */

#include <Quickdraw.h>
#include <Events.h>
#include <Dialogs.h>
#include <Files.h>
#include <MixedMode.h>
#include <stdio.h>
#include "shears_client.h"
#include "shears_ui.h"

#ifndef RD_CALLS
#define RD_CALLS 200
#endif
#ifndef RD_QUALITY
#define RD_QUALITY 'good'       /* 22 kHz, 8 bit, mono; SimpleSound asks for 'best': 44.1 kHz, 16 bit, stereo */
#endif

#include <Sound.h>
#include <Gestalt.h>

static long gCalls;

static unsigned long gStart;

static pascal Boolean Filter(DialogPtr dlg, EventRecord *ev, short *itemHit)
{
    ++gCalls;
#ifdef RD_SECONDS       /* by the clock instead: the window stays open that long, as it does under a person's hand */
    if (gStart == 0)
        gStart = TickCount();
    if (TickCount() - gStart >= RD_SECONDS * 60UL) {
#else
    if (gCalls >= RD_CALLS) {
#endif
        *itemHit = 2;       /* Cancel */
        return true;
    }
    return false;
}

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
    FSSpec spec;
    short fref = 0;
    OSErr err;
    int n;
    UniversalProcPtr filter;
    Point corner;

    if (!ShearsOpen())
        return 0;
    UiInit();
    if (!Hello())
        return 0;
    Wait(12 * 60);

    FSMakeFSSpec(0, 0, "\precdlg.aiff", &spec);
    FSpDelete(&spec);
    err = FSpCreate(&spec, 'ShRd', 'AIFF', 0);
    if (err == noErr)
        err = FSpOpenDF(&spec, fsRdWrPerm, &fref);
    sprintf(line, "RD file err=%d", (int)err);
    ShearsLog(line);
    /* pascal Boolean (DialogPtr, EventRecord *, short *): a one-byte result, three four-byte parameters */
    filter = NewRoutineDescriptor((ProcPtr)Filter, 0x0FD0, GetCurrentArchitecture());
#ifdef RD_PROBE     /* what SimpleSound does before it opens the window */
    {
        long ref = 0, snd = 0, q;
        static const long quals[4] = { 'best', 'betr', 'good', 'phon' };
        Gestalt('snd ', &snd);
        err = SPBOpenDevice(NULL, 1, &ref);
        sprintf(line, "RD probe open err=%d gestalt=%lx", (int)err, snd);
        ShearsLog(line);
        for (n = 0; n < 4; n++) {
            q = quals[n];
            err = SPBSetDeviceInfo(ref, 'qual', (Ptr)&q);
            sprintf(line, "RD probe quality %d err=%d", n, (int)err);
            ShearsLog(line);
        }
        err = SPBCloseDevice(ref);
        Gestalt('snd ', &snd);
        sprintf(line, "RD probe close err=%d", (int)err);
        ShearsLog(line);
    }
#endif
    ShearsLog("RD before");
    corner.v = 100;
    corner.h = 100;
    err = SndRecordToFile((ProcPtr)filter, corner, RD_QUALITY, fref);
    sprintf(line, "RD after err=%d calls=%ld", (int)err, gCalls);
    ShearsLog(line);
    FSClose(fref);
    FSpDelete(&spec);
    for (n = 1; n <= 5; n++) {
        Wait(60);
        sprintf(line, "RD alive %d", n);
        ShearsLog(line);
    }
    ShearsLog("RD done");
    for (;;)
        WaitNextEvent(everyEvent, &ev, 30, NULL);
    return 0;
}
