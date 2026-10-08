/*
 *  shears_client.h - Guest side of the Sheep Shears mailbox (see SheepShaver/src/include/shears.h for the format).
 *  Shared by the tool and the self-tests. Header-only; every function is static.
 *
 *  (C) 2026 Bill Cavalieri
 */

#ifndef SHEARS_CLIENT_H
#define SHEARS_CLIENT_H

#include <MacTypes.h>
#include <Memory.h>
#include <Files.h>
#include <string.h>

#define TOOL_VERSION        1UL
#define SHEARS_MAGIC        0x53485253UL    /* 'SHRS' */
#define SHEARS_OPCODE       0x18000B42UL    /* 0x18000000 | NATIVE_SHEARS (45) << 6 | 2, see shears.cpp */
#define SHEEPSHAVER_SIG     0x42616168UL    /* 'Baah' at 0x2800, written by SheepShaver at start-up */
#define CAP_POINTER         1UL
#define CAP_SHUTDOWN        2UL
#define FLAG_SHUTDOWN       2UL        /* in the second payload word of POLL and PTR_POS replies */
#define ACTION_SHUTDOWN     1UL
#define FEATURE_EDGE        1UL        /* SETTINGS word: release the mouse at a screen edge */
#define FEATURE_CLIPBOARD   2UL        /*                share the clipboard with the Mac */
#define FEATURE_ALL         3UL

/* Not in the Multiversal headers. */
#ifndef kOnSystemDisk
#define kOnSystemDisk       (-32768)
#endif
#ifndef kCreateFolder
#define kCreateFolder       true
#endif

enum { cmdHello = 1, cmdPoll = 2, cmdLog = 3, cmdPtrPos = 4, cmdSysResult = 5, cmdSettings = 6 };
enum { statusOk = 0 };

typedef struct {
    unsigned long magic, version, seq, command, status, length, reserved0, reserved1;
    unsigned char payload[4096 - 32];
} Mailbox;

static Mailbox *gMailbox;
static unsigned long gSeq;

/* Executes the fake opcode. r3 = mailbox; the result is 'SHRS' when the host handled the call. */
static unsigned long ShearsTrap(void *mailbox)
{
    register unsigned long r3 __asm__("r3") = (unsigned long)mailbox;
    __asm__ volatile(".long %1" : "+r"(r3) : "i"(SHEARS_OPCODE) : "memory", "cc");
    return r3;
}

static Boolean ShearsCall(unsigned long command, const unsigned long *words, int count)
{
    int i;
    Mailbox *mb = gMailbox;
    mb->magic = SHEARS_MAGIC;
    mb->version = TOOL_VERSION;
    mb->seq = ++gSeq;
    mb->command = command;
    mb->status = 0xFFFFFFFFUL;
    mb->length = (unsigned long)count * 4;
    for (i = 0; i < count; i++)
        ((unsigned long *)mb->payload)[i] = words[i];
    return ShearsTrap(mb) == SHEARS_MAGIC && mb->status == statusOk;
}

static void ShearsLog(const char *text)
{
    unsigned long n = strlen(text);
    if (n > 200) n = 200;
    gMailbox->magic = SHEARS_MAGIC;
    gMailbox->version = TOOL_VERSION;
    gMailbox->seq = ++gSeq;
    gMailbox->command = cmdLog;
    gMailbox->status = 0xFFFFFFFFUL;
    gMailbox->length = n;
    memcpy(gMailbox->payload, text, n);
    ShearsTrap(gMailbox);
}

static Boolean Hello(void)
{
    unsigned long words[2];
    words[0] = TOOL_VERSION;
    words[1] = CAP_POINTER | CAP_SHUTDOWN;
    return ShearsCall(cmdHello, words, 2);
}

/* Tells the host which features the user left on. The reply carries what is now in effect (both sides must allow it). */
static Boolean SendSettings(unsigned long features)
{
    return ShearsCall(cmdSettings, &features, 1);
}

/* The settings live in the file "Sheep Shears Prefs" in the Preferences folder: 'SHPF', version 1, then the word of
   features that are ON. A missing, short or unreadable file means everything is on. */
#define PREFS_NAME "\pSheep Shears Prefs"

static OSErr PrefsSpec(FSSpec *spec)
{
    short vRef;
    long dirID;
    OSErr err = FindFolder(kOnSystemDisk, kPreferencesFolderType, kCreateFolder, &vRef, &dirID);
    if (err != noErr)
        return err;
    err = FSMakeFSSpec(vRef, dirID, (ConstStr255Param)PREFS_NAME, spec);
    return err == fnfErr ? noErr : err;     /* a missing file is fine: the spec still names where it goes */
}

static unsigned long PrefsRead(void)
{
    FSSpec spec;
    short ref;
    long count = 12;
    unsigned long words[3];
    unsigned long result = FEATURE_ALL;
    if (PrefsSpec(&spec) != noErr || FSpOpenDF(&spec, fsRdPerm, &ref) != noErr)
        return FEATURE_ALL;
    if (FSRead(ref, &count, words) == noErr && count == 12 && words[0] == 0x53485046UL && words[1] == 1)
        result = words[2] & FEATURE_ALL;
    FSClose(ref);
    return result;
}

static OSErr PrefsWrite(unsigned long features)
{
    FSSpec spec;
    short ref;
    long count = 12;
    unsigned long words[3];
    OSErr err = PrefsSpec(&spec);
    if (err != noErr)
        return err;
    err = FSpCreate(&spec, 'ShSh', 'pref', smSystemScript);
    if (err != noErr && err != dupFNErr)
        return err;
    err = FSpOpenDF(&spec, fsRdWrPerm, &ref);
    if (err != noErr)
        return err;
    words[0] = 0x53485046UL;            /* 'SHPF' */
    words[1] = 1;
    words[2] = features & FEATURE_ALL;
    err = SetEOF(ref, 0);
    if (err == noErr)
        err = FSWrite(ref, &count, words);
    FSClose(ref);
    return err;
}

/* Allocates the mailbox (system heap, locked by nature). False on a machine that is not SheepShaver or without memory. */
static Boolean ShearsOpen(void)
{
    if (*(volatile unsigned long *)0x2800 != SHEEPSHAVER_SIG)
        return false;                   /* not SheepShaver: never execute the fake opcode */
    gMailbox = (Mailbox *)NewPtrSysClear(sizeof(Mailbox));
    return gMailbox != NULL;
}

#endif
