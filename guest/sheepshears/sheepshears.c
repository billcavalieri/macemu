/*
 *  sheepshears.c - Sheep Shears guest tool for Mac OS 9 under SheepShaver (PowerPC, built with Retro68).
 *
 *  A background-only application. It reports the real pointer position to the host (PTR_POS) so the host can
 *  release the mouse when it is pushed through an edge of the guest screen. It talks to the host through a
 *  4 KB mailbox and one fake PowerPC opcode that only SheepShaver executes; see SheepShaver/src/include/shears.h.
 *
 *  It checks the SheepShaver signature word first and never executes the opcode on anything else, because on
 *  other hardware that opcode is illegal.
 *
 *  Put it in the Startup Items folder. Build with tools/shears/build.sh --apps-only (needs Retro68, see
 *  SheepShaver/src/MacOSX/SHEEP-SHEARS-PLAN.md).
 *
 *  (C) 2026 Bill Cavalieri
 */

#include <MacTypes.h>
#include <Quickdraw.h>
#include <Events.h>
#include <Memory.h>
#include <AppleEvents.h>
#include <Processes.h>
#include <string.h>
#include "shears_client.h"

static Boolean gQuit;

/* Asks the Finder to shut down, as Special > Shut Down does (the Finder suite's 'shut', class 'FNDR'; the core
   suite has no such event): Mac OS asks every application to quit (each may ask about
   unsaved work) and then powers off, which the emulator turns into quitting. */
static OSErr AskFinderToShutDown(void)
{
    AEAddressDesc target;
    AppleEvent event, reply;
    OSType finder = 'MACS';
    OSErr err = AECreateDesc(typeApplSignature, &finder, sizeof finder, &target);
    if (err != noErr)
        return err;
    err = AECreateAppleEvent('FNDR', 'shut', &target, kAutoGenerateReturnID, kAnyTransactionID, &event);   /* Finder suite: shut down */
    AEDisposeDesc(&target);
    if (err != noErr)
        return err;
    reply.descriptorType = typeNull;
    reply.dataHandle = NULL;
    err = AESend(&event, &reply, kAENoReply, kAENormalPriority, kAEDefaultTimeout, NULL, NULL);
    AEDisposeDesc(&event);
    return err;
}

static pascal OSErr HandleQuit(const AppleEvent *event, AppleEvent *reply, long refcon)
{
    gQuit = true;
    return noErr;
}

static pascal OSErr HandleIgnore(const AppleEvent *event, AppleEvent *reply, long refcon)
{
    return noErr;
}

int main(void)
{
    EventRecord ev;
    Point lastPos = { -32768, -32768 };
    Boolean lastButton = false;
    unsigned long lastSent = 0, retryAt = 0;
    Boolean connected = false;
    Boolean shutdownWanted = false;
    unsigned long hostTicks = 15;           /* what the host asks for: short while it is grabbed and wants the pointer */
    unsigned long sleepTicks = 15;
    unsigned long lastMoved = 0;

    if (!ShearsOpen())
        return 0;                       /* not SheepShaver (the opcode is never executed there), or out of memory */

    InitGraf(&qd.thePort);

    AEInstallEventHandler(kCoreEventClass, kAEQuitApplication, NewAEEventHandlerUPP(HandleQuit), 0, false);
    AEInstallEventHandler(kCoreEventClass, kAEOpenApplication, NewAEEventHandlerUPP(HandleIgnore), 0, false);
    AEInstallEventHandler(kCoreEventClass, kAEOpenDocuments, NewAEEventHandlerUPP(HandleIgnore), 0, false);
    AEInstallEventHandler(kCoreEventClass, kAEPrintDocuments, NewAEEventHandlerUPP(HandleIgnore), 0, false);

    while (!gQuit) {
        unsigned long now = TickCount();

        if (!connected) {
            if (now >= retryAt) {
                connected = Hello();
                retryAt = now + 600;    /* the host may be switched off or older: ask again in ten seconds */
                if (connected) {
                    if (gMailbox->length >= 16) {
                        unsigned long t = ((unsigned long *)gMailbox->payload)[3];
                        hostTicks = (t < 1) ? 1 : (t > 60) ? 60 : t;
                    }
                    SendSettings(PrefsRead());          /* the user's choices from the control panel */
                    ShearsLog("Sheep Shears 1 started");
                    lastSent = 0;
                }
            }
        } else {
            /* One call per wake-up. It reports the pointer when it moved (or once a second as a heartbeat) and
               otherwise just polls; either reply says how long to sleep before the next one. Every wake-up of
               this application costs the emulator real CPU, so the host asks for fast polling only while it is
               grabbed and wants to know where the pointer is. */
            Point pos = LMGetMouseLocation2();      /* the global pointer, low-memory Mouse at 0x830 */
            Boolean button = Button();
            Boolean ok;
            if (pos.h != lastPos.h || pos.v != lastPos.v || button != lastButton || now - lastSent >= 60) {
                Rect screen = (**GetMainDevice()).gdRect;
                unsigned long words[5];
                words[0] = (unsigned long)(long)pos.h;
                words[1] = (unsigned long)(long)pos.v;
                words[2] = (unsigned long)(screen.right - screen.left);
                words[3] = (unsigned long)(screen.bottom - screen.top);
                words[4] = button ? 1UL : 0UL;
                ok = ShearsCall(cmdPtrPos, words, 5);
                if (ok) {
                    if (pos.h != lastPos.h || pos.v != lastPos.v || button != lastButton)
                        lastMoved = now;
                    lastPos = pos;
                    lastButton = button;
                    lastSent = now;
                }
            } else {
                ok = ShearsCall(cmdPoll, NULL, 0);
            }
            if (ok) {
                if (gMailbox->length >= 8 && (((unsigned long *)gMailbox->payload)[1] & FLAG_SHUTDOWN))
                    shutdownWanted = true;      /* acted on below, once the reply has been read */
                if (gMailbox->length >= 4) {
                    unsigned long t = ((unsigned long *)gMailbox->payload)[0];
                    hostTicks = (t < 1) ? 1 : (t > 60) ? 60 : t;
                }

                /* Fast only while the pointer is moving: a resting pointer is reported once a second, which is
                   enough for the host (its release rule needs a fresh position, not a changing one). */
                sleepTicks = (now - lastMoved < 60) ? hostTicks : (hostTicks < 15 ? 15 : hostTicks);
            } else {
                connected = false;              /* the host went away or refused: back to asking */
                retryAt = now + 600;
            }
        }

        if (shutdownWanted && connected) {
            /* The host asked for a proper shutdown. Tell it what happened so it stops asking: 0 = the Finder has
               the request, anything else is the Mac OS error. */
            unsigned long words[2];
            shutdownWanted = false;
            words[0] = ACTION_SHUTDOWN;
            words[1] = (unsigned long)(long)AskFinderToShutDown();
            ShearsCall(cmdSysResult, words, 2);
        }

        if (WaitNextEvent(everyEvent, &ev, sleepTicks, NULL) && ev.what == kHighLevelEvent)
            AEProcessAppleEvent(&ev);
    }
    return 0;
}
