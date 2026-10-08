/*
 *  installer.c - Sheep Shears installer (PowerPC, built with Retro68). Copies the tool into Startup Items and the
 *  control panel into Control Panels, starts the tool, and can remove both again. The two files are read from the
 *  folder "Sheep Shears Files" next to this installer, so it runs from the installer disk.
 *
 *  Installing over an older copy quits the running tool first (Quit Apple event), then replaces it. Nothing needs a
 *  restart: the tool is started at the end.
 *
 *  If a file named "Sheep Shears Auto Install" is in the System Folder it installs without a window and quits (the file
 *  may name the folder to install from);
 *  tools/shears/test.sh install uses that to test the copy logic headless.
 *
 *  (C) 2026 Bill Cavalieri
 */

#include <Processes.h>
#include <Files.h>
#include <AppleEvents.h>
#include <LowMem.h>
#include <string.h>
#include "shears_client.h"

/* Not in the Multiversal headers. */
#ifndef kHasBeenInited
#define kHasBeenInited      0x0100
#endif
#ifndef launchNoFileFlags
#define launchNoFileFlags   0x0800
#endif
#ifndef launchDontSwitch
#define launchDontSwitch    0x0200
#endif
#include "shears_ui.h"

enum { itemInstall = 1, itemQuit = 2, itemRemove = 3, itemStatus = 4 };

#define TOOL_NAME   "\pSheep Shears Tool"
#define PANEL_NAME  "\pSheep Shears"
#define FILES_NAME  "\pSheep Shears Files"
#define AUTO_NAME   "\pSheep Shears Auto Install"

static FSSpec gHere;            /* this application */
static Str255 gSourcePath;      /* auto mode only: where to read the files from instead of next to the installer */

static OSErr FindSelf(void)
{
    ProcessSerialNumber psn;
    ProcessInfoRec info;
    GetCurrentProcess(&psn);
    info.processInfoLength = sizeof info;
    info.processName = NULL;
    info.processAppSpec = &gHere;
    return GetProcessInformation(&psn, &info);
}

/* A file inside the folder "Sheep Shears Files" next to the installer. */
static OSErr SourceSpec(ConstStr255Param name, FSSpec *spec)
{
    FSSpec folder;
    CInfoPBRec pb;
    OSErr err = gSourcePath[0] ? FSMakeFSSpec(0, 0, gSourcePath, &folder)
                               : FSMakeFSSpec(gHere.vRefNum, gHere.parID, FILES_NAME, &folder);
    if (err != noErr)
        return err;
    memset(&pb, 0, sizeof pb);
    pb.dirInfo.ioNamePtr = folder.name;
    pb.dirInfo.ioVRefNum = folder.vRefNum;
    pb.dirInfo.ioDrDirID = folder.parID;
    pb.dirInfo.ioFDirIndex = 0;
    err = PBGetCatInfoSync(&pb);
    if (err != noErr)
        return err;
    return FSMakeFSSpec(folder.vRefNum, pb.dirInfo.ioDrDirID, name, spec);
}

static OSErr FolderSpec(OSType kind, ConstStr255Param name, FSSpec *spec)
{
    short vRef;
    long dirID;
    OSErr err = FindFolder(kOnSystemDisk, kind, kCreateFolder, &vRef, &dirID);
    if (err != noErr)
        return err;
    err = FSMakeFSSpec(vRef, dirID, name, spec);
    return err == fnfErr ? noErr : err;
}

static OSErr CopyFork(const FSSpec *from, const FSSpec *to, Boolean resource)
{
    short in, out;
    OSErr err, err2;
    static char buffer[16384];
    long count;

    err = resource ? FSpOpenRF((FSSpecPtr)from, fsRdPerm, &in) : FSpOpenDF((FSSpecPtr)from, fsRdPerm, &in);
    if (err != noErr)
        return err;
    err = resource ? FSpOpenRF((FSSpecPtr)to, fsWrPerm, &out) : FSpOpenDF((FSSpecPtr)to, fsWrPerm, &out);
    if (err != noErr) {
        FSClose(in);
        return err;
    }
    for (;;) {
        count = sizeof buffer;
        err = FSRead(in, &count, buffer);
        if (count > 0) {
            long written = count;
            err2 = FSWrite(out, &written, buffer);
            if (err2 != noErr) {
                err = err2;
                break;
            }
        }
        if (err != noErr)
            break;
    }
    if (err == eofErr)
        err = noErr;
    FSClose(in);
    FSClose(out);
    return err;
}

/* Replaces `to` with a copy of `from`: both forks and the Finder information (type, creator). */
static OSErr CopyFile(const FSSpec *from, const FSSpec *to)
{
    FInfo info;
    OSErr err = FSpGetFInfo((FSSpecPtr)from, &info);
    if (err != noErr)
        return err;
    err = FSpDelete((FSSpecPtr)to);
    if (err != noErr && err != fnfErr)
        return err;
    err = FSpCreate((FSSpecPtr)to, info.fdCreator, info.fdType, smSystemScript);
    if (err != noErr)
        return err;
    err = CopyFork(from, to, false);
    if (err == noErr)
        err = CopyFork(from, to, true);
    if (err == noErr) {
        info.fdFlags &= ~kHasBeenInited;        /* the Finder gives it its icon and position again */
        err = FSpSetFInfo((FSSpecPtr)to, &info);
    }
    if (err != noErr)
        FSpDelete((FSSpecPtr)to);
    return err;
}

static Boolean FindTool(ProcessSerialNumber *psn)
{
    ProcessInfoRec info;
    psn->highLongOfPSN = 0;
    psn->lowLongOfPSN = kNoProcess;
    while (GetNextProcess(psn) == noErr) {
        info.processInfoLength = sizeof info;
        info.processName = NULL;
        info.processAppSpec = NULL;
        if (GetProcessInformation(psn, &info) == noErr && info.processSignature == 'ShSh' && info.processType == 'APPL'
            && (info.processMode & modeOnlyBackground))
            return true;
    }
    return false;
}

/* Asks a running tool to quit and waits (up to four seconds) until it has. */
static void QuitTool(void)
{
    ProcessSerialNumber psn;
    AEAddressDesc target;
    AppleEvent event, reply;
    unsigned long until;
    if (!FindTool(&psn))
        return;
    if (AECreateDesc(typeProcessSerialNumber, &psn, sizeof psn, &target) != noErr)
        return;
    if (AECreateAppleEvent(kCoreEventClass, kAEQuitApplication, &target, kAutoGenerateReturnID, kAnyTransactionID, &event) == noErr) {
        reply.descriptorType = typeNull;
        reply.dataHandle = NULL;
        AESend(&event, &reply, kAENoReply, kAENormalPriority, kAEDefaultTimeout, NULL, NULL);
        AEDisposeDesc(&event);
    }
    AEDisposeDesc(&target);
    until = TickCount() + 240;
    while (TickCount() < until && FindTool(&psn)) {
        EventRecord ev;
        WaitNextEvent(0, &ev, 10, NULL);
    }
}

static OSErr StartTool(const FSSpec *tool)
{
    LaunchParamBlockRec lp;
    memset(&lp, 0, sizeof lp);
    lp.launchBlockID = extendedBlock;
    lp.launchEPBLength = extendedBlockLen;
    lp.launchFileFlags = 0;
    lp.launchControlFlags = launchContinue | launchNoFileFlags | launchDontSwitch;
    lp.launchAppSpec = (FSSpecPtr)tool;
    return LaunchApplication(&lp);
}

static const char *Describe(OSErr err, char *buffer)
{
    /* The common failures in words; anything else as its Mac OS error number. */
    switch (err) {
    case fnfErr:     return "A file of the installer disk is missing (the folder \"Sheep Shears Files\" must stay next to the installer).";
    case dirNFErr:   return "A folder is missing.";
    case vLckdErr:
    case wPrErr:     return "The disk is locked.";
    case fBsyErr:    return "A file is in use. Quit the Sheep Shears tool and try again.";
    case dskFulErr:  return "The disk is full.";
    case permErr:    return "The file is protected.";
    default: {
        long n = err;
        char digits[16];
        int i = 0, j;
        Boolean neg = n < 0;
        if (neg) n = -n;
        do { digits[i++] = (char)('0' + n % 10); n /= 10; } while (n > 0 && i < 15);
        strcpy(buffer, "Mac OS error ");
        j = (int)strlen(buffer);
        if (neg) buffer[j++] = '-';
        while (i > 0) buffer[j++] = digits[--i];
        buffer[j] = 0;
        return buffer;
    }
    }
}

/* Returns noErr, and a message for the dialog. */
static OSErr Install(const char **message, char *scratch)
{
    FSSpec srcTool, srcPanel, dstTool, dstPanel;
    OSErr err;

    err = SourceSpec(TOOL_NAME, &srcTool);
    if (err == noErr) err = SourceSpec(PANEL_NAME, &srcPanel);
    if (err == noErr) err = FolderSpec(kStartupFolderType, TOOL_NAME, &dstTool);
    if (err == noErr) err = FolderSpec(kControlPanelFolderType, PANEL_NAME, &dstPanel);
    if (err != noErr) {
        *message = Describe(err, scratch);
        return err;
    }
    QuitTool();
    err = CopyFile(&srcTool, &dstTool);
    if (err == noErr)
        err = CopyFile(&srcPanel, &dstPanel);
    if (err != noErr) {
        *message = Describe(err, scratch);
        return err;
    }
    err = StartTool(&dstTool);
    *message = err == noErr
        ? "Installed. The tool is running now and starts every time the guest starts. The Control Panel is called Sheep Shears."
        : "Installed. The tool did not start by itself (it works only in SheepShaver); it will start at the next restart.";
    return noErr;
}

static OSErr Remove(const char **message, char *scratch)
{
    FSSpec dstTool, dstPanel;
    OSErr err = FolderSpec(kStartupFolderType, TOOL_NAME, &dstTool);
    OSErr e2;
    if (err == noErr) err = FolderSpec(kControlPanelFolderType, PANEL_NAME, &dstPanel);
    if (err != noErr) {
        *message = Describe(err, scratch);
        return err;
    }
    QuitTool();
    err = FSpDelete(&dstTool);
    e2 = FSpDelete(&dstPanel);
    if (err == fnfErr) err = noErr;
    if (e2 == fnfErr) e2 = noErr;
    if (err == noErr) err = e2;
    *message = err == noErr ? "Removed." : Describe(err, scratch);
    return err;
}

static void Hit(DialogPtr d, short item)
{
    const char *message;
    char scratch[64];
    switch (item) {
    case itemInstall:
        UiSetText(d, itemStatus, "Installing...");
        Install(&message, scratch);
        UiSetText(d, itemStatus, message);
        break;
    case itemRemove:
        Remove(&message, scratch);
        UiSetText(d, itemStatus, message);
        break;
    case itemQuit:
        gUiQuit = true;
        break;
    }
}

int main(void)
{
    DialogPtr d;
    FSSpec autoFlag;
    FInfo flagInfo;

    UiInit();
    if (FindSelf() != noErr)
        return 0;

    if (FolderSpec(kSystemFolderType, AUTO_NAME, &autoFlag) == noErr && FSpGetFInfo(&autoFlag, &flagInfo) == noErr) {
        const char *message;
        char scratch[64];
        OSErr err;
        short ref;
        long count = 255;
        /* The flag file may hold a folder path (e.g. Unix:Sheep Shears:Sheep Shears Files, a folder shared from
           the host) to install from; tools/shears/test.sh extfs checks that route. */
        if (FSpOpenDF(&autoFlag, fsRdPerm, &ref) == noErr) {
            char path[256];
            if (FSRead(ref, &count, path) == noErr || count > 0) {
                long i;
                while (count > 0 && (path[count - 1] == '\r' || path[count - 1] == '\n' || path[count - 1] == ' '))
                    count--;
                for (i = 0; i < count; i++)
                    gSourcePath[i + 1] = (unsigned char)path[i];
                gSourcePath[0] = (unsigned char)(count > 1 ? count : 0);   /* the one-byte file of the other test is no path */
            }
            FSClose(ref);
        }
        err = Install(&message, scratch);
        if (ShearsOpen()) {
            ShearsLog(err == noErr ? "installer: done" : "installer: failed");
            ShearsLog(message);
        }
        return 0;
    }

    d = GetNewDialog(128, NULL, (WindowPtr)-1L);
    if (d == NULL)
        return 0;
    UiSetText(d, itemStatus, "Installs the Sheep Shears tool (mouse release and clipboard) and its control panel into this Mac OS system.");
    ShowWindow((WindowPtr)d);
    SelectWindow((WindowPtr)d);
    UiRun(d, Hit);
    DisposeDialog(d);
    return 0;
}
