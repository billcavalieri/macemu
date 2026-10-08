/*
 *  shears_ui.h - A one-dialog application skeleton for the Sheep Shears control panel and installer (PowerPC,
 *  Retro68). The dialog is modeless inside a normal event loop so the application still answers the Quit Apple
 *  event (Mac OS sends it at shutdown) and the window can be dragged.
 *
 *  (C) 2026 Bill Cavalieri
 */

#ifndef SHEARS_UI_H
#define SHEARS_UI_H

#include <MacTypes.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Events.h>
#include <AppleEvents.h>
#include <string.h>

static Boolean gUiQuit;

static pascal OSErr UiHandleQuit(const AppleEvent *event, AppleEvent *reply, long refcon)
{
    gUiQuit = true;
    return noErr;
}

static pascal OSErr UiHandleIgnore(const AppleEvent *event, AppleEvent *reply, long refcon)
{
    return noErr;
}

static void UiInit(void)
{
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    AEInstallEventHandler(kCoreEventClass, kAEQuitApplication, NewAEEventHandlerUPP(UiHandleQuit), 0, false);
    AEInstallEventHandler(kCoreEventClass, kAEOpenApplication, NewAEEventHandlerUPP(UiHandleIgnore), 0, false);
    AEInstallEventHandler(kCoreEventClass, kAEOpenDocuments, NewAEEventHandlerUPP(UiHandleIgnore), 0, false);
    AEInstallEventHandler(kCoreEventClass, kAEPrintDocuments, NewAEEventHandlerUPP(UiHandleIgnore), 0, false);
}

static void UiPascal(Str255 out, const char *text)
{
    size_t n = strlen(text);
    if (n > 255) n = 255;
    out[0] = (unsigned char)n;
    memcpy(out + 1, text, n);
}

static void UiSetText(DialogPtr d, short item, const char *text)
{
    short type;
    Handle h;
    Rect box;
    Str255 s;
    GetDialogItem(d, item, &type, &h, &box);
    UiPascal(s, text);
    SetDialogItemText(h, s);
}

static void UiSetCheck(DialogPtr d, short item, Boolean on)
{
    short type;
    Handle h;
    Rect box;
    GetDialogItem(d, item, &type, &h, &box);
    SetControlValue((ControlHandle)h, on ? 1 : 0);
}

static Boolean UiGetCheck(DialogPtr d, short item)
{
    short type;
    Handle h;
    Rect box;
    GetDialogItem(d, item, &type, &h, &box);
    return GetControlValue((ControlHandle)h) != 0;
}

static void UiEnable(DialogPtr d, short item, Boolean on)
{
    short type;
    Handle h;
    Rect box;
    GetDialogItem(d, item, &type, &h, &box);
    HiliteControl((ControlHandle)h, on ? 0 : 255);
}

/* Runs until Quit. `hit` is called with the dialog and the item number for every click on an enabled item. */
static void UiRun(DialogPtr dialog, void (*hit)(DialogPtr, short))
{
    EventRecord ev;
    while (!gUiQuit) {
        if (!WaitNextEvent(everyEvent, &ev, 30, NULL))
            continue;
        if (ev.what == kHighLevelEvent) {
            AEProcessAppleEvent(&ev);
            continue;
        }
        if (ev.what == mouseDown) {
            WindowPtr w;
            short part = FindWindow(ev.where, &w);
            if (part == inDrag && w == (WindowPtr)dialog) {
                Rect limit = qd.screenBits.bounds;
                DragWindow(w, ev.where, &limit);
                continue;
            }
            if (part == inSysWindow)
                continue;
        }
        {
            DialogPtr which;
            short item;
            if (IsDialogEvent(&ev) && DialogSelect(&ev, &which, &item) && which == dialog)
                hit(dialog, item);
        }
    }
}

#endif
