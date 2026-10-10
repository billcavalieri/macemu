/*
 *  miccheck.c - Sheep Shears self-test: sound input (the Sound Manager's SPB calls) from the host microphone.
 *
 *  Start-up Items app, run by tools/vms/test.sh mic with a test tone as the microphone (NW_MIC_TONE). It reports through the
 *  Shears LOG command (visible with NW_VERBOSE=1):
 *    list    SPBVersion, the device list, SPBOpenDevice, the device's settings
 *    async   one second recorded asynchronously at 22254 Hz, 8-bit mono, then the pitch heard in it
 *    sync    half a second recorded synchronously at 44100 Hz, 16-bit stereo, then the pitch
 *    stream  continuous recording with an interrupt routine, stopped with SPBStopRecording after a second
 *
 *  (C) 2026 Bill Cavalieri
 */

#include <Quickdraw.h>
#include <Events.h>
#include <MixedMode.h>
#include <Sound.h>
#include <Files.h>
#include <TextUtils.h>
#include <Gestalt.h>
#include <stdio.h>
#include "shears_client.h"

typedef struct {
    long inRefNum;
    unsigned long count, milliseconds, bufferLength;
    Ptr bufferPtr;
    UniversalProcPtr completionRoutine, interruptRoutine;
    long userLong;
    short error;
    long unused1;
} MySPB;

static volatile long gDone, gInterrupts, gInterruptBytes;

static pascal void Completion(MySPB *pb)
{
    gDone = 1;
}

static pascal void Interrupt(MySPB *pb, Ptr buf, short peak, long sampleSize)
{
    gInterrupts++;
    gInterruptBytes += pb->bufferLength;
}

/* ProcInfo: pascal stack-based; each parameter's size code (1 = 1 byte, 2 = 2, 3 = 4) is two bits from bit 6 up. */
enum {
    uppCompletion = 0x00C0,            /* void (SPBPtr) */
    uppInterrupt = 0x3BC0,             /* void (SPBPtr, Ptr, short, long) */
    siWritePermission = 1
};

static void Logf(const char *fmt, long a, long b, long c)
{
    char line[160];
    sprintf(line, fmt, a, b, c);
    ShearsLog(line);
}

/* Pitch from the zero crossings; returns Hz*10, and the peak (0..127 for 8-bit, 0..32767 for 16-bit). */
static long Pitch(const unsigned char *p, long frames, int bits, int chans, long rate, long *peak)
{
    long i, crossings = 0, mx = 0, prev = 0;
    for (i = 0; i < frames; i++) {
        long v;
        if (bits == 8)
            v = (long)p[i * chans] - 128;
        else
            v = (short)((p[i * chans * 2] << 8) | p[i * chans * 2 + 1]);
        if (v > mx) mx = v;
        if (-v > mx) mx = -v;
        if ((prev < 0) != (v < 0) && i > 0)
            crossings++;
        prev = v;
    }
    *peak = mx;
    return frames ? (crossings * rate * 10L / 2L) / frames : 0;
}

static void Wait(long ticks)
{
    EventRecord ev;
    unsigned long end = TickCount() + ticks;
    while (TickCount() < end)
        WaitNextEvent(everyEvent, &ev, 2, NULL);
}

static void Record(long ref, const char *label, long rateInt, short bits, short chans, long ms, Boolean async)
{
    MySPB pb;
    long fixed = rateInt << 16, bytes = 0, frames, peak, hz10, i;
    short size = bits, ch = chans;
    OSErr err;
    Ptr buf;
    err = SPBSetDeviceInfo(ref, 'srat', (Ptr)&fixed);
    Logf("MIC set rate err=%ld", err, 0, 0);
    err = SPBSetDeviceInfo(ref, 'ssiz', (Ptr)&size);
    Logf("MIC set size err=%ld", err, 0, 0);
    err = SPBSetDeviceInfo(ref, 'chan', (Ptr)&ch);
    Logf("MIC set channels err=%ld", err, 0, 0);
    bytes = ms;
    err = SPBMillisecondsToBytes(ref, &bytes);
    Logf("MIC ms->bytes err=%ld ms=%ld bytes=%ld", err, ms, bytes);
    buf = NewPtrClear(bytes + 16);
    memset(&pb, 0, sizeof pb);
    pb.inRefNum = ref;
    pb.count = bytes;
    pb.bufferLength = bytes;
    pb.bufferPtr = buf;
    gDone = 0;
    if (async)
        pb.completionRoutine = NewRoutineDescriptor((ProcPtr)Completion, uppCompletion, GetCurrentArchitecture());
    err = SPBRecord((SPBPtr)&pb, async);
    {
        char line[120];     /* pb.error is greater than 0 while an asynchronous recording is in progress */
        sprintf(line, "MIC %s start err=%d pberr=%d", label, (int)err, (int)pb.error);
        ShearsLog(line);
    }
    if (async) {
        for (i = 0; i < 300 && !gDone; i++)
            Wait(2);
    }
    frames = pb.count / (bits / 8 * chans);
    hz10 = Pitch((unsigned char *)buf, frames, bits, chans, rateInt, &peak);
    {
        char line[160];
        sprintf(line, "MIC %s done=%ld error=%d count=%ld frames=%ld peak=%ld pitch_x10=%ld", label, (long)gDone, (int)pb.error, (long)pb.count, frames, peak, hz10);
        ShearsLog(line);
    }
    DisposePtr(buf);
}


/* SPBRecordToFile with no buffer of our own, as SimpleSound does: 1 s counted, then 1 s stopped by SPBStopRecording. */
static void RecordToFile(long ref, const char *label, Boolean stopIt)
{
    MySPB pb;
    FSSpec spec;
    short fref = 0;
    long fixed = 22254L << 16, bytes = 1000, eof = 0, n, peak, hz10, i;
    short s8 = 8, c1 = 1, status = 0, meter = 0;
    long totS, numS, totMs, numMs;
    OSErr err;
    Ptr data;
    SPBSetDeviceInfo(ref, 'srat', (Ptr)&fixed);
    SPBSetDeviceInfo(ref, 'ssiz', (Ptr)&s8);
    SPBSetDeviceInfo(ref, 'chan', (Ptr)&c1);
    SPBMillisecondsToBytes(ref, &bytes);
    FSMakeFSSpec(0, 0, "\pmic.raw", &spec);
    FSpDelete(&spec);
    err = FSpCreate(&spec, 'ShMc', 'BINA', 0);
    if (err == noErr)
        err = FSpOpenDF(&spec, fsRdWrPerm, &fref);
    Logf("MIC file create/open err=%ld", err, 0, 0);
    memset(&pb, 0, sizeof pb);
    pb.inRefNum = ref;
    pb.count = stopIt ? 0 : bytes;
    pb.completionRoutine = NewRoutineDescriptor((ProcPtr)Completion, uppCompletion, GetCurrentArchitecture());
    gDone = 0;
    err = SPBRecordToFile(fref, (SPBPtr)&pb, true);
    Logf("MIC file record start err=%ld", err, 0, 0);
    if (stopIt) {
        Wait(60);
        err = SPBStopRecording(ref);
        Logf("MIC file stop err=%ld", err, 0, 0);
    }
    for (i = 0; i < 200 && !gDone; i++) {
        Wait(3);
        SPBGetRecordingStatus(ref, &status, &meter, &totS, &numS, &totMs, &numMs);
    }
    GetEOF(fref, &eof);
    data = NewPtrClear(eof + 16);
    SetFPos(fref, fsFromStart, 0);
    n = eof;
    FSRead(fref, &n, data);
    hz10 = Pitch((unsigned char *)data, n, 8, 1, 22254, &peak);
    {
        char line[160];
        sprintf(line, "MIC %s done=%ld error=%d count=%ld eof=%ld pitch_x10=%ld peak=%ld", label, (long)gDone, (int)pb.error, (long)pb.count, eof, hz10, peak);
        ShearsLog(line);
    }
    DisposePtr(data);
    FSClose(fref);
}

int main(void)
{
    EventRecord ev;
    unsigned long start;
    long ref = 0, rate = 0, i;
    short size = 0, chans = 0;
    OSErr err;
    Str255 name;
    Handle icon = NULL;
    NumVersion v;
    MySPB pb;
    Ptr buf;

    if (!ShearsOpen())
        return 0;
    InitGraf(&qd.thePort);
    if (!Hello())
        return 0;
    start = TickCount();
    while (TickCount() - start < 12 * 60)
        WaitNextEvent(everyEvent, &ev, 6, NULL);

    {
        long snd = 0;
        OSErr gerr = Gestalt('snd ', &snd);
        Logf("MIC Gestalt snd err=%ld value=%lx", gerr, snd, 0);
    }
    /* InterfaceLib returns a NumVersion through a pointer the caller passes in r3 (the PowerPC convention Apple's
       compilers use for any structure). This compiler expects it back in a register and passes nothing, so calling
       SPBVersion() as declared makes the glue store through whatever r3 held: a system error, type 2, more often than
       not. Call it the way the glue is written. */
    ((void (*)(NumVersion *))SPBVersion)(&v);
    Logf("MIC SPBVersion %ld", *(long *)&v, 0, 0);
    err = SPBGetIndexedDevice(1, name, &icon);
    Logf("MIC device 1 err=%ld icon=%ld", err, (long)icon, 0);
    if (err == noErr) {
        char line[80];
        name[name[0] + 1] = 0;
        sprintf(line, "MIC device name '%s'", (char *)name + 1);
        ShearsLog(line);
    }
    err = SPBGetIndexedDevice(2, name, &icon);
    Logf("MIC device 2 err=%ld (expect an error)", err, 0, 0);
    err = SPBOpenDevice(NULL, siWritePermission, &ref);
    Logf("MIC open err=%ld ref=%lx", err, ref, 0);
    if (err != noErr) {
        ShearsLog("MIC FAILED open");
        ShearsLog("MIC done");
        for (;;) WaitNextEvent(everyEvent, &ev, 30, NULL);
    }
    err = SPBGetDeviceInfo(ref, 'srat', (Ptr)&rate);
    Logf("MIC rate err=%ld rate=%lx", err, rate, 0);
    err = SPBGetDeviceInfo(ref, 'ssiz', (Ptr)&size);
    Logf("MIC size err=%ld size=%ld", err, size, 0);
    err = SPBGetDeviceInfo(ref, 'chan', (Ptr)&chans);
    Logf("MIC channels err=%ld chans=%ld", err, chans, 0);
    {
        long ref2 = 0;
        err = SPBOpenDevice(NULL, siWritePermission, &ref2);
        Logf("MIC second writer err=%ld (expect -227)", err, 0, 0);
    }

    Record(ref, "async", 22254, 8, 1, 1000, true);
    Record(ref, "sync", 44100, 16, 2, 500, false);

    /* continuous: an interrupt routine each 4096 bytes (8-bit mono 22254 Hz: about 5 per second) */
    {
        long fixed = 22254L << 16;
        short s8 = 8, c1 = 1;
        SPBSetDeviceInfo(ref, 'srat', (Ptr)&fixed);
        SPBSetDeviceInfo(ref, 'ssiz', (Ptr)&s8);
        SPBSetDeviceInfo(ref, 'chan', (Ptr)&c1);
    }
    buf = NewPtrClear(4096);
    memset(&pb, 0, sizeof pb);
    pb.inRefNum = ref;
    pb.bufferLength = 4096;
    pb.bufferPtr = buf;
    pb.completionRoutine = NewRoutineDescriptor((ProcPtr)Completion, uppCompletion, GetCurrentArchitecture());
    pb.interruptRoutine = NewRoutineDescriptor((ProcPtr)Interrupt, uppInterrupt, GetCurrentArchitecture());
    gDone = 0; gInterrupts = 0; gInterruptBytes = 0;
    err = SPBRecord((SPBPtr)&pb, true);
    Logf("MIC stream start err=%ld", err, 0, 0);
    Wait(60);
    err = SPBStopRecording(ref);
    Logf("MIC stream stop err=%ld", err, 0, 0);
    Wait(10);
    Logf("MIC stream interrupts=%ld bytes=%ld completion=%ld", gInterrupts, gInterruptBytes, gDone);
    DisposePtr(buf);

    RecordToFile(ref, "file", false);
    RecordToFile(ref, "filestop", true);

    err = SPBCloseDevice(ref);
    Logf("MIC close err=%ld", err, 0, 0);
    ShearsLog("MIC done");
    for (;;)
        WaitNextEvent(everyEvent, &ev, 30, NULL);
    return 0;
}
