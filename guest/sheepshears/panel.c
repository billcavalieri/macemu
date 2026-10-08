/*
 *  panel.c - Sheep Shears control panel (PowerPC, built with Retro68). Lets the user switch the mouse edge release
 *  and the clipboard sharing off or on; both are on by default. A control panel application ('appc'), so it
 *  opens from the Control Panels folder like any other.
 *
 *  The choices are saved in "Sheep Shears Prefs" in the Preferences folder (read by the tool when it starts) and
 *  are sent to the host at once, so the change takes effect without a restart.
 *
 *  (C) 2026 Bill Cavalieri
 */

#include <Processes.h>
#include "shears_client.h"
#include "shears_ui.h"

enum { itemDone = 1, itemEdge = 2, itemClipboard = 3, itemStatus = 4 };

static Boolean gHaveHost;

static Boolean ToolIsRunning(void)
{
    ProcessSerialNumber psn = { 0, kNoProcess };
    ProcessInfoRec info;
    while (GetNextProcess(&psn) == noErr) {
        info.processInfoLength = sizeof info;
        info.processName = NULL;
        info.processAppSpec = NULL;
        if (GetProcessInformation(&psn, &info) == noErr && info.processSignature == 'ShSh' && info.processType == 'APPL'
            && (info.processMode & modeOnlyBackground))
            return true;
    }
    return false;
}

static void ShowStatus(DialogPtr d)
{
    if (!gHaveHost)
        UiSetText(d, itemStatus, "Sheep Shears only works in SheepShaver. These settings are saved but have no effect here.");
    else if (ToolIsRunning())
        UiSetText(d, itemStatus, "The Sheep Shears tool is running. Changes apply immediately.");
    else
        UiSetText(d, itemStatus, "The Sheep Shears tool is not running. Run the Sheep Shears installer again, or restart the guest.");
}

static void Apply(DialogPtr d)
{
    unsigned long features = 0;
    if (UiGetCheck(d, itemEdge)) features |= FEATURE_EDGE;
    if (UiGetCheck(d, itemClipboard)) features |= FEATURE_CLIPBOARD;
    PrefsWrite(features);
    if (gHaveHost)
        SendSettings(features);
}

static void Hit(DialogPtr d, short item)
{
    switch (item) {
    case itemDone:
        gUiQuit = true;
        break;
    case itemEdge:
    case itemClipboard:
        UiSetCheck(d, item, !UiGetCheck(d, item));
        Apply(d);
        ShowStatus(d);
        break;
    }
}

int main(void)
{
    DialogPtr d;
    unsigned long features;

    UiInit();
    gHaveHost = ShearsOpen();

    d = GetNewDialog(128, NULL, (WindowPtr)-1L);
    if (d == NULL)
        return 0;
    features = PrefsRead();
    UiSetCheck(d, itemEdge, (features & FEATURE_EDGE) != 0);
    UiSetCheck(d, itemClipboard, (features & FEATURE_CLIPBOARD) != 0);
    ShowStatus(d);
    ShowWindow((WindowPtr)d);
    SelectWindow((WindowPtr)d);
    UiRun(d, Hit);
    DisposeDialog(d);
    return 0;
}
