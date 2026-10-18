/*
 *  main.c - SheepBench: a benchmark for Mac OS 9 in SheepShaver that reports emulator measurements (raw units,
 *  JIT on vs off, percent of native ARM64 speed, SheepForce GPU on vs off) instead of a comparison with a G3.
 *
 *  The main window picks the suites and the comparisons and runs them; Results shows one row per test; Save writes
 *  the tab-separated report; Compare puts two saved reports side by side; About lists what the host reported.
 *  On a SheepShaver without the SheepBench commands (or on a real Mac) everything still runs and the host fields
 *  read "unknown".
 *
 *  Auto mode (the tests, tools/sheepbench/test.sh): a file "SheepBench Auto" in the System Folder holds lines such as
 *  "suites cpu memory", "jit 1", "gpu 1", "quick 1". SheepBench then runs at once, sends the report to the host,
 *  logs "SHEEPBENCH DONE" and quits. "report 1" also writes the report to "SheepBench Report" in the System Folder
 *  (for a run without a host), "shutdown 1" then asks the Finder to shut down, "show 1" leaves the Results and About
 *  windows open and keeps running (for screenshots).
 *
 *  (C) 2026 Bill Cavalieri
 */

#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Events.h>
#include <AppleEvents.h>
#include <Devices.h>
#include <Files.h>
#include <Gestalt.h>
#include <StandardFile.h>
#include <ToolUtils.h>
#include <OSUtils.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench_host.h"
#include "bench_report.h"
#include "bench_time.h"
#include "suites.h"
#include "textwin.h"

#define SHEEPBENCH_VERSION "1.0"

#ifndef kOnSystemDisk
#define kOnSystemDisk (-32768)
#endif
#ifndef kSystemFolderType
#define kSystemFolderType 'macs'
#endif

enum { mApple = 128, mFile = 129, mEdit = 130 };
enum { iAbout = 1 };
enum { iRun = 1, iResults, iSave, iCompare, iSep, iClose, iQuit };
enum MainItems {
    itRun = 1, itStop, itCPU, it68k, itMemory, itMMU, itQuickDraw, it3D, itDisk, itJit, itGpu, itProgress, itStatus,
    itSuitesLabel, itCompareLabel, itQuick, itHost
};

static DialogPtr gMain;
static TextWin *gResults, *gAbout;
static Boolean gQuit, gRunning, gStopAsked;
static double gProgress;            /* 0..1 */
static Boolean gCanSwitchJit, gCanSwitchGpu;
static Boolean gAuto;

static void Pascal(Str255 out, const char *s)
{
    size_t n = strlen(s);
    if (n > 255)
        n = 255;
    out[0] = (unsigned char)n;
    memcpy(out + 1, s, n);
}

static void C(char *out, const unsigned char *p)
{
    memcpy(out, p + 1, p[0]);
    out[p[0]] = 0;
}

/* ---- main window helpers ---- */

static Handle Item(short item, Rect *box)
{
    short type;
    Handle h;
    Rect r;
    GetDialogItem(gMain, item, &type, &h, &r);
    if (box)
        *box = r;
    return h;
}

static Boolean Checked(short item)
{
    return GetControlValue((ControlHandle)Item(item, NULL)) != 0;
}

static void SetCheck(short item, Boolean on)
{
    SetControlValue((ControlHandle)Item(item, NULL), on ? 1 : 0);
}

static void Enable(short item, Boolean on)
{
    HiliteControl((ControlHandle)Item(item, NULL), on ? 0 : 255);
}

static void SetText(short item, const char *text)
{
    Str255 s;
    Pascal(s, text);
    SetDialogItemText(Item(item, NULL), s);
}

void UiStatus(const char *text)
{
    GrafPtr old;
    if (!gMain)
        return;
    GetPort(&old);
    SetPort((GrafPtr)gMain);
    SetText(itStatus, text);
    SetPort(old);
}

static void DrawProgress(void)
{
    Rect r, f;
    GrafPtr old;
    GetPort(&old);
    SetPort((GrafPtr)gMain);
    Item(itProgress, &r);
    PenNormal();
    FrameRect(&r);
    InsetRect(&r, 1, 1);
    f = r;
    f.right = f.left + (short)((r.right - r.left) * (gProgress < 0 ? 0 : gProgress > 1 ? 1 : gProgress));
    PaintRect(&f);
    f.left = f.right;
    f.right = r.right;
    EraseRect(&f);
    SetPort(old);
}

static void SetProgress(double p)
{
    gProgress = p;
    DrawProgress();
}

static void UpdateButtons(void)
{
    Enable(itRun, !gRunning);
    Enable(itStop, gRunning);
    Enable(itJit, gCanSwitchJit && !gRunning);
    Enable(itGpu, gCanSwitchGpu && !gRunning);
    if (!gCanSwitchJit)
        SetCheck(itJit, false);
    if (!gCanSwitchGpu)
        SetCheck(itGpu, false);
}

/* ---- host state ---- */

static Boolean InfoIs(const char *key, const char *value)
{
    const char *v = HostInfo(key);
    return v && strcmp(v, value) == 0;
}

static void ReadHost(void)
{
    char line[200];
    const char *build;
    if (HostOpen() && HostInfoRefresh()) {
        const char *hz = HostInfo("tb_hz");
        if (hz)
            TbSetHz(atof(hz));
        build = HostInfo("build");
        gCanSwitchJit = InfoIs("bench_switch_allowed", "1") && InfoIs("jit_at_start", "on");
        gCanSwitchGpu = InfoIs("bench_switch_allowed", "1") && InfoIs("sheepforce", "1") && InfoIs("sheepforce_qd", "1");
        sprintf(line, "Host: SheepShaver %s, JIT %s.%s", build ? build : "", HostInfo("jit") ? HostInfo("jit") : "?",
                InfoIs("bench_switch_allowed", "1") ? "" : " Comparisons are off in the VM's settings.");
    } else {
        gCanSwitchJit = gCanSwitchGpu = false;
        strcpy(line, "Host: unknown (not a SheepShaver with SheepBench support).");
    }
    SetText(itHost, line);
}

/* ---- event handling (also used between repetitions while a run is going) ---- */

static void DoMenu(long choice);
static void RunSelected(void);

static void ShowAbout(void);

static void HandleUpdate(WindowPtr w)
{
    TextWin *t = TextWinFind(w);
    if (t) {
        TextWinUpdate(t);
    } else if (w == (WindowPtr)gMain) {
        SetPort(w);
        BeginUpdate(w);
        UpdateDialog(gMain, w->visRgn);
        EndUpdate(w);
        DrawProgress();
    } else {
        /* a test's own window (QuickDraw suite): nothing to redraw, but the update must be taken or it comes back
         * from every WaitNextEvent */
        BeginUpdate(w);
        EndUpdate(w);
    }
}

static void HandleMainHit(short item)
{
    switch (item) {
    case itRun:
        if (!gRunning)
            RunSelected();
        break;
    case itStop:
        gStopAsked = true;
        break;
    case itCPU: case it68k: case itMemory: case itMMU: case itQuickDraw: case it3D: case itDisk: case itJit:
    case itGpu: case itQuick:
        if (!gRunning)
            SetCheck(item, !Checked(item));
        break;
    }
}

static void HandleEvent(EventRecord *ev)
{
    WindowPtr w;
    short part;
    TextWin *t;
    switch (ev->what) {
    case kHighLevelEvent:
        AEProcessAppleEvent(ev);
        return;
    case updateEvt:
        HandleUpdate((WindowPtr)ev->message);
        return;
    case activateEvt:
        if ((t = TextWinFind((WindowPtr)ev->message)) != NULL) {
            TextWinActivate(t, (ev->modifiers & activeFlag) != 0);
            return;
        }
        break;
    case keyDown:
    case autoKey: {
        char key = (char)(ev->message & charCodeMask);
        if (ev->modifiers & cmdKey) {
            if (key == '.') {
                gStopAsked = true;
                return;
            }
            if (ev->what == keyDown)
                DoMenu(MenuKey(key));
            return;
        }
        if ((t = TextWinFind(FrontWindow())) != NULL) {
            TextWinKey(t, key);
            return;
        }
        break;
    }
    case mouseDown:
        part = FindWindow(ev->where, &w);
        switch (part) {
        case inMenuBar:
            DoMenu(MenuSelect(ev->where));
            return;
        case inSysWindow:
            return;
        case inDrag: {
            Rect limit = qd.screenBits.bounds;
            DragWindow(w, ev->where, &limit);
            return;
        }
        case inGoAway:
            if (TrackGoAway(w, ev->where)) {
                if ((t = TextWinFind(w)) != NULL) {
                    if (t == gResults)
                        gResults = NULL;
                    if (t == gAbout)
                        gAbout = NULL;
                    TextWinClose(t);
                } else if (w == (WindowPtr)gMain && !gRunning) {
                    gQuit = true;
                }
            }
            return;
        case inGrow:
            if ((t = TextWinFind(w)) != NULL)
                TextWinGrow(t, ev->where);
            return;
        case inContent:
            if (w != FrontWindow()) {
                SelectWindow(w);
                return;
            }
            if ((t = TextWinFind(w)) != NULL) {
                TextWinClick(t, ev->where);
                return;
            }
            break;
        }
        break;
    }
    if (IsDialogEvent(ev)) {
        DialogPtr which;
        short item;
        if (DialogSelect(ev, &which, &item) && which == gMain)
            HandleMainHit(item);
    }
}

/* Between repetitions: answer events (window drags, Stop, the menu bar) without starting anything new. */
static Boolean Yield(void)
{
    EventRecord ev;
    static unsigned long last;
    int n = 0;
    if (TickCount() - last < 6 && !gStopAsked)
        return gStopAsked;
    last = TickCount();
    while (n++ < 20 && WaitNextEvent(everyEvent, &ev, 0, NULL)) {
        if (ev.what == mouseDown || ev.what == keyDown || ev.what == updateEvt || ev.what == kHighLevelEvent ||
            ev.what == activateEvt)
            HandleEvent(&ev);
    }
    if (gQuit)
        gStopAsked = true;
    return gStopAsked;
}

/* ---- run info ---- */

static void TwoDigits(char *s, int v)
{
    s[0] = (char)('0' + v / 10 % 10);
    s[1] = (char)('0' + v % 10);
    s[2] = 0;
}

static void CollectInfo(void)
{
    char v[200], d[3], h[3], mi[3];
    long g;
    DateTimeRec dt;
    unsigned long secs;
    const char *p;
    GDHandle main = GetMainDevice();

    ReportInfo(&gReport, "sheepbench", SHEEPBENCH_VERSION);
    GetDateTime(&secs);
    SecondsToDate(secs, &dt);
    TwoDigits(d, dt.day);
    TwoDigits(h, dt.hour);
    TwoDigits(mi, dt.minute);
    sprintf(v, "%d-%02d-%s %s:%s", dt.year, dt.month, d, h, mi);
    ReportInfo(&gReport, "date", v);
    if (Gestalt('sysv', &g) == noErr) {
        sprintf(v, "%lx.%lx.%lx", (g >> 8) & 0xff, (g >> 4) & 0xf, g & 0xf);
        ReportInfo(&gReport, "mac_os", v);
    }
    if (Gestalt('ram ', &g) == noErr) {
        sprintf(v, "%ld", g >> 20);
        ReportInfo(&gReport, "guest_ram_mb", v);
    }
    if (Gestalt('cput', &g) == noErr) {
        sprintf(v, "%lx", g);
        ReportInfo(&gReport, "gestalt_cpu_type", v);
    }
    if (Gestalt('pgsz', &g) == noErr) {
        sprintf(v, "%ld", g);
        ReportInfo(&gReport, "page_size", v);
    }
    if (Gestalt('vm  ', &g) == noErr)
        ReportInfo(&gReport, "virtual_memory", (g & 1) ? "on" : "off");
    if (main) {
        Rect r = (**main).gdRect;
        sprintf(v, "%dx%d, %d bit", r.right - r.left, r.bottom - r.top, (**(**main).gdPMap).pixelSize);
        ReportInfo(&gReport, "screen", v);
    }
    sprintf(v, "%.0f", TbHz());
    ReportInfo(&gReport, "timebase_hz_used", v);
    ReportInfo(&gReport, "host", HostPresent() ? "SheepShaver (SheepBench commands)" : "unknown");
    /* Everything the host said, "key=value" as "key<TAB>value"; drive lines as "disk 3<TAB>kind=..." */
    for (p = HostInfoText(); *p; ) {
        const char *end = strchr(p, '\n'), *eq;
        size_t len = end ? (size_t)(end - p) : strlen(p);
        char line[220], key[64];
        if (len >= sizeof line)
            len = sizeof line - 1;
        memcpy(line, p, len);
        line[len] = 0;
        eq = strchr(line, '=');
        if (eq) {
            const char *sp = strchr(line, ' ');
            if (sp && sp < eq) {                /* "disk drive=3 kind=..." */
                const char *num = sp + 1 + strlen("drive=");
                int kl = (int)(sp - line);
                sprintf(key, "%.*s %d", kl, line, atoi(num));
                ReportInfo(&gReport, key, strchr(num, ' ') ? strchr(num, ' ') + 1 : "");
            } else {
                int kl = (int)(eq - line);
                if (kl >= (int)sizeof key)
                    kl = sizeof key - 1;
                memcpy(key, line, kl);
                key[kl] = 0;
                ReportInfo(&gReport, key, eq + 1);
            }
        }
        if (!end)
            break;
        p = end + 1;
    }
    ReportInfo(&gReport, "derived_columns", "pct_native slowdown_x jit_speedup_x gpu_speedup_x");
    ReportInfo(&gReport, "pct_native", "100 x native time / guest time for the same C kernel; 100 = native speed");
}

/* ---- running ---- */

typedef struct {
    short item;
    const char *name;
    void (*run)(void);
    Boolean gpu;            /* also runs in the SheepForce QuickDraw off pass */
} SuiteDef;

static const SuiteDef kSuites[] = {
    { itCPU, "cpu", SuiteCPU, false },
    { it68k, "68k", Suite68k, false },
    { itMemory, "memory", SuiteMemory, false },
    { itMMU, "mmu", SuiteMMU, false },
    { itQuickDraw, "quickdraw", SuiteQuickDraw, true },
    { it3D, "3d", Suite3D, false },
    { itDisk, "disk", SuiteDisk, false },
};
#define NSUITES ((int)(sizeof kSuites / sizeof kSuites[0]))

/* Switches the modes and waits until the host has applied them (it does so between blocks). */
static Boolean SwitchModes(Boolean ppc, Boolean m68k, Boolean qd, Boolean restore)
{
    long why = 0;
    unsigned long until;
    char line[80];
    int st = HostSetModes(ppc, m68k, qd, restore, &why);
    if (st != hostOk) {
        sprintf(line, "The host refused to switch (status %d, reason %ld).", st, why);
        Say(line);
        return false;
    }
    until = TickCount() + 120;
    while (TickCount() < until) {
        EventRecord ev;
        WaitNextEvent(everyEvent, &ev, 1, NULL);
        if (HostInfoRefresh() && InfoIs("switch_pending", "0"))
            return true;
    }
    Say("The host did not apply the switch in time.");
    return false;
}

static void ShowResults(void);

static void RunPass(int pass, const Boolean *chosen, int passIndex, int passes)
{
    int i, done = 0, total = 0;
    gPass = pass;
    for (i = 0; i < NSUITES; i++)
        if (chosen[i] && (pass != passGpuOff || kSuites[i].gpu))
            total++;
    for (i = 0; i < NSUITES && !gCancelled; i++) {
        if (!chosen[i] || (pass == passGpuOff && !kSuites[i].gpu))
            continue;
        SetProgress(((double)passIndex + (double)done / (total ? total : 1)) / passes);
        kSuites[i].run();
        done++;
        if (gStopAsked)
            gCancelled = true;
    }
}

static void RunSuites(const Boolean *chosen, Boolean jit, Boolean gpu, Boolean quick)
{
    int passes = 1 + (jit ? 1 : 0) + (gpu ? 1 : 0), passIndex = 0;
    char line[120];
    gRunning = true;
    gStopAsked = false;
    gCancelled = false;
    UpdateButtons();
    gRules.minRepSeconds = quick ? 0.010 : 0.020;
    gRules.maxReps = quick ? 9 : 25;
    gRules.budgetSeconds = quick ? 1.0 : 3.0;
    gRules.scale = 1.0;
    ReportClear(&gReport);
    HostInfoRefresh();
    CollectInfo();
    ReportInfo(&gReport, "quick", quick ? "yes" : "no");
    ReportInfo(&gReport, "compare_jit", jit ? "yes" : "no");
    ReportInfo(&gReport, "compare_gpu", gpu ? "yes" : "no");
    Say("SHEEPBENCH START");

    RunPass(passMain, chosen, passIndex++, passes);
    if (jit && !gCancelled) {
        if (SwitchModes(false, false, InfoIs("sheepforce", "1") && InfoIs("sheepforce_qd", "1"), false)) {
            gRules.scale = 0.5;             /* the interpreter is slow: spend less time per test */
            RunPass(passJitOff, chosen, passIndex, passes);
            gRules.scale = 1.0;
            SwitchModes(true, true, true, true);
        }
        passIndex++;
    }
    if (gpu && !gCancelled) {
        if (SwitchModes(InfoIs("jit_at_start", "on"), InfoIs("jit68k_host_at_start", "on"), false, false)) {
            RunPass(passGpuOff, chosen, passIndex, passes);
            SwitchModes(true, true, true, true);
        }
        passIndex++;
    }
    gPass = passMain;
    SetProgress(1.0);
    sprintf(line, "%s: %d tests.", gCancelled ? "Stopped" : "Done", gReport.count);
    Say(line);
    gRunning = false;
    UpdateButtons();
    if (HostPresent()) {
        char *text;
        long n = ReportText(&gReport, false, &text);
        if (text) {
            if (HostSendReport(text, n))
                HostLog("SHEEPBENCH REPORT SENT");
            DisposePtr(text);
        }
    }
    ShowResults();
}

static void RunSelected(void)
{
    Boolean chosen[NSUITES];
    int i;
    for (i = 0; i < NSUITES; i++)
        chosen[i] = Checked(kSuites[i].item);
    RunSuites(chosen, Checked(itJit), Checked(itGpu), Checked(itQuick));
}

/* ---- results, save, compare, about ---- */

static void Pad(char *line, int *used, const char *text, int width, Boolean right)
{
    int n = (int)strlen(text), i;
    if (n > width)
        n = width;
    if (right)
        for (i = n; i < width; i++)
            line[(*used)++] = ' ';
    memcpy(line + *used, text, n);
    *used += n;
    if (!right)
        for (i = n; i < width; i++)
            line[(*used)++] = ' ';
    line[(*used)++] = ' ';
    line[*used] = 0;
}

static char *ResultsText(const BenchReport *r)
{
    long cap = 512 + (long)r->count * 220;
    char *t = NewPtr(cap), line[300], v[24];
    int i, u;
    const char *last = "";
    if (!t)
        return NULL;
    t[0] = 0;
    sprintf(t, "%-40s %10s %-7s %6s %10s %7s %8s %10s %7s  %s\n", "Test", "Value", "Unit", "noise", "JIT off",
            "x JIT", "% native", "GPU off", "x GPU", "Status");
    strcat(t, "(x JIT, % native and x GPU are derived: ratios of two measurements)\n");
    for (i = 0; i < r->count; i++) {
        const BenchRow *row = &r->rows[i];
        if (strcmp(row->suite, last) != 0) {
            sprintf(line, "\n[%s]\n", row->suite);
            strcat(t, line);
            last = row->suite;
        }
        u = 0;
        Pad(line, &u, row->test, 40, false);
        FormatValue(v, row->value); Pad(line, &u, v, 10, true);
        Pad(line, &u, row->unit, 7, false);
        if (row->value > 0) sprintf(v, "%.1f%%", row->noise * 100); else v[0] = 0;
        Pad(line, &u, v, 6, true);
        FormatValue(v, row->jitOff); Pad(line, &u, v, 10, true);
        FormatValue(v, RowJitSpeedup(row)); Pad(line, &u, v, 7, true);
        FormatValue(v, RowPctNative(row)); Pad(line, &u, v, 8, true);
        FormatValue(v, row->gpuOff); Pad(line, &u, v, 10, true);
        FormatValue(v, RowGpuSpeedup(row)); Pad(line, &u, v, 7, true);
        switch (row->status) {
        case rowOk: strcpy(v, "ok"); break;
        case rowNoisy: strcpy(v, "noisy"); break;
        case rowSkipped: strcpy(v, "skipped"); break;
        default: strcpy(v, "FAILED"); break;
        }
        line[u++] = ' ';
        strcpy(line + u, v);
        if (row->status == rowSkipped || row->status == rowFailed) {
            strcat(line, ": ");
            strcat(line, row->why);
        }
        if (row->hook[0]) {
            strcat(line, "  hook ");
            strcat(line, row->hook);
            if (row->hookHits[0]) {
                strcat(line, " hits ");
                strcat(line, row->hookHits);
            }
        }
        strcat(line, "\n");
        if ((long)(strlen(t) + strlen(line)) < cap - 1)
            strcat(t, line);
    }
    return t;
}

static void ShowResults(void)
{
    char *text;
    if (gAuto)
        return;
    if (!gResults)
        gResults = TextWinNew("SheepBench Results", 900, 560);
    if (!gResults)
        return;
    text = ResultsText(&gReport);
    if (text) {
        TextWinSetLines(gResults, text);
        DisposePtr(text);
    }
    SelectWindow(TextWinWindow(gResults));
}

static void SaveReport(void)
{
    StandardFileReply reply;
    char *text;
    long n;
    short ref;
    OSErr err;
    if (gReport.count == 0) {
        SysBeep(10);
        return;
    }
    StandardPutFile("\pSave the report as:", "\pSheepBench Report", &reply);
    if (!reply.sfGood)
        return;
    n = ReportText(&gReport, true, &text);
    if (!text)
        return;
    if (reply.sfReplacing)
        FSpDelete(&reply.sfFile);
    err = FSpCreate(&reply.sfFile, 'ttxt', 'TEXT', reply.sfScript);
    if (err == noErr)
        err = FSpOpenDF(&reply.sfFile, fsRdWrPerm, &ref);
    if (err == noErr) {
        err = FSWrite(ref, &n, text);
        FSClose(ref);
    }
    DisposePtr(text);
    if (err != noErr)
        SysBeep(10);
}

static Boolean LoadReport(BenchReport *r, const char *prompt)
{
    StandardFileReply reply;
    SFTypeList types;
    short ref;
    long n;
    char *text;
    Boolean ok = false;
    Str255 p;
    types[0] = 'TEXT';
    Pascal(p, prompt);
    UiStatus(prompt);
    StandardGetFile(NULL, 1, types, &reply);
    if (!reply.sfGood || FSpOpenDF(&reply.sfFile, fsRdPerm, &ref) != noErr)
        return false;
    if (GetEOF(ref, &n) == noErr && n > 0 && n < 4 * 1024 * 1024 && (text = NewPtr(n)) != NULL) {
        if (FSRead(ref, &n, text) == noErr)
            ok = ReportParse(r, text, n);
        DisposePtr(text);
    }
    FSClose(ref);
    if (!ok)
        UiStatus("That file is not a SheepBench report.");
    return ok;
}

static void CompareReports(void)
{
    BenchReport *a = (BenchReport *)NewPtr(sizeof(BenchReport)), *b = (BenchReport *)NewPtr(sizeof(BenchReport));
    char *t = NULL, line[300], v1[24], v2[24], v3[24];
    TextWin *w;
    int i;
    long cap;
    if (!a || !b)
        goto done;
    if (!LoadReport(a, "Choose the first report (A)."))
        goto done;
    if (!LoadReport(b, "Choose the second report (B)."))
        goto done;
    cap = 2048 + (long)(a->count + b->count) * 160;
    t = NewPtr(cap);
    if (!t)
        goto done;
    sprintf(t, "A: %s %s   B: %s %s\n", ReportInfoValue(a, "date") ? ReportInfoValue(a, "date") : "?",
            ReportInfoValue(a, "build") ? ReportInfoValue(a, "build") : "",
            ReportInfoValue(b, "date") ? ReportInfoValue(b, "date") : "?",
            ReportInfoValue(b, "build") ? ReportInfoValue(b, "build") : "");
    sprintf(line, "%-12s %-40s %10s %10s %-7s %8s\n", "Suite", "Test", "A", "B", "Unit", "B vs A");
    strcat(t, line);
    strcat(t, "(B vs A is derived: above 1 means B is faster)\n\n");
    for (i = 0; i < a->count; i++) {
        const BenchRow *ra = &a->rows[i], *rb = ReportFind(b, ra->suite, ra->test);
        double x = NO_VALUE;
        FormatValue(v1, ra->value);
        FormatValue(v2, rb ? rb->value : NO_VALUE);
        if (rb && ra->value > 0 && rb->value > 0)
            x = ra->higherIsBetter ? rb->value / ra->value : ra->value / rb->value;
        FormatValue(v3, x);
        sprintf(line, "%-12.12s %-40.40s %10s %10s %-7s %8s\n", ra->suite, ra->test, v1, v2, ra->unit, v3);
        if ((long)(strlen(t) + strlen(line)) < cap - 1)
            strcat(t, line);
    }
    for (i = 0; i < b->count; i++) {
        const BenchRow *rb = &b->rows[i];
        if (!ReportFind(a, rb->suite, rb->test)) {
            FormatValue(v2, rb->value);
            sprintf(line, "%-12.12s %-40.40s %10s %10s %-7s %8s\n", rb->suite, rb->test, "", v2, rb->unit, "");
            if ((long)(strlen(t) + strlen(line)) < cap - 1)
                strcat(t, line);
        }
    }
    w = TextWinNew("SheepBench Comparison", 760, 520);
    if (w)
        TextWinSetLines(w, t);
done:
    if (t)
        DisposePtr(t);
    if (a)
        DisposePtr((Ptr)a);
    if (b)
        DisposePtr((Ptr)b);
}

static void ShowAbout(void)
{
    char *t = NewPtr(8192);
    const char *p;
    if (!t)
        return;
    HostInfoRefresh();
    sprintf(t, "SheepBench %s - emulator measurements for SheepShaver.\n\n", SHEEPBENCH_VERSION);
    if (HostPresent()) {
        strcat(t, "What the host reports:\n\n");
        p = HostInfoText();
        strncat(t, p, 8000 - strlen(t));
    } else {
        strcat(t, "Host: unknown. This is not a SheepShaver that answers SheepBench's commands, so the build, the\n"
                  "JIT and SheepForce state, the disk image types and the percent-of-native column are not available.\n");
    }
    if (!gAbout)
        gAbout = TextWinNew("About SheepBench", 560, 520);
    if (gAbout)
        TextWinSetLines(gAbout, t);
    DisposePtr(t);
}

static void DoMenu(long choice)
{
    short menu = (short)(choice >> 16), item = (short)(choice & 0xFFFF);
    switch (menu) {
    case mApple:
        if (item == iAbout) {
            ShowAbout();
        } else {
            Str255 name;
            GetMenuItemText(GetMenuHandle(mApple), item, name);
            OpenDeskAcc(name);
        }
        break;
    case mFile:
        switch (item) {
        case iRun: if (!gRunning) RunSelected(); break;
        case iResults: ShowResults(); break;
        case iSave: if (!gRunning) SaveReport(); break;
        case iCompare: if (!gRunning) CompareReports(); break;
        case iClose: {
            TextWin *t = TextWinFind(FrontWindow());
            if (t) {
                if (t == gResults) gResults = NULL;
                if (t == gAbout) gAbout = NULL;
                TextWinClose(t);
            }
            break;
        }
        case iQuit:
            if (gRunning)
                gStopAsked = true;
            gQuit = true;
            break;
        }
        break;
    case mEdit:
        SystemEdit(item - 1);
        break;
    }
    HiliteMenu(0);
}

/* ---- auto mode ---- */

static Boolean gAutoReport, gAutoShutdown, gAutoShow;

static Boolean ReadAuto(Boolean *chosen, Boolean *jit, Boolean *gpu, Boolean *quick)
{
    short vRef, ref;
    long dir, n = 0;
    FSSpec spec;
    char buf[512], *line;
    int i;
    if (FindFolder(kOnSystemDisk, kSystemFolderType, false, &vRef, &dir) != noErr ||
        FSMakeFSSpec(vRef, dir, "\pSheepBench Auto", &spec) != noErr || FSpOpenDF(&spec, fsRdPerm, &ref) != noErr)
        return false;
    n = sizeof buf - 1;
    FSRead(ref, &n, buf);
    FSClose(ref);
    buf[n < 0 ? 0 : n] = 0;
    for (i = 0; i < NSUITES; i++)
        chosen[i] = false;
    *jit = *gpu = *quick = false;
    for (line = buf; line && *line; ) {
        char *end = strpbrk(line, "\r\n");
        if (end)
            *end = 0;
        if (strncmp(line, "suites ", 7) == 0) {
            for (i = 0; i < NSUITES; i++)
                if (strstr(line + 7, kSuites[i].name))
                    chosen[i] = true;
        } else if (strncmp(line, "jit ", 4) == 0) {
            *jit = line[4] == '1';
        } else if (strncmp(line, "gpu ", 4) == 0) {
            *gpu = line[4] == '1';
        } else if (strncmp(line, "quick ", 6) == 0) {
            *quick = line[6] == '1';
        } else if (strncmp(line, "report ", 7) == 0) {
            gAutoReport = line[7] == '1';
        } else if (strncmp(line, "shutdown ", 9) == 0) {
            gAutoShutdown = line[9] == '1';
        } else if (strncmp(line, "show ", 5) == 0) {
            gAutoShow = line[5] == '1';
        }
        line = end ? end + 1 : NULL;
    }
    return true;
}

static void WriteAutoReport(void)
{
    short vRef, ref;
    long dir, n;
    FSSpec spec;
    char *text;
    if (FindFolder(kOnSystemDisk, kSystemFolderType, false, &vRef, &dir) != noErr)
        return;
    FSMakeFSSpec(vRef, dir, "\pSheepBench Report", &spec);
    FSpDelete(&spec);
    if (FSpCreate(&spec, 'ttxt', 'TEXT', smSystemScript) != noErr || FSpOpenDF(&spec, fsRdWrPerm, &ref) != noErr)
        return;
    n = ReportText(&gReport, true, &text);
    if (text) {
        FSWrite(ref, &n, text);
        DisposePtr(text);
    }
    FSClose(ref);
    FlushVol(NULL, vRef);
}

/* Asks the Finder to shut down, as Special > Shut Down does (see guest/sheepshears/sheepshears.c). */
static void AskFinderToShutDown(void)
{
    AEAddressDesc target;
    AppleEvent event, reply;
    OSType finder = 'MACS';
    if (AECreateDesc(typeApplSignature, &finder, sizeof finder, &target) != noErr)
        return;
    if (AECreateAppleEvent('FNDR', 'shut', &target, kAutoGenerateReturnID, kAnyTransactionID, &event) == noErr) {
        reply.descriptorType = typeNull;
        reply.dataHandle = NULL;
        AESend(&event, &reply, kAENoReply, kAENormalPriority, kAEDefaultTimeout, NULL, NULL);
        AEDisposeDesc(&event);
    }
    AEDisposeDesc(&target);
}

/* ---- start-up ---- */

static pascal OSErr QuitHandler(const AppleEvent *e, AppleEvent *r, long refcon)
{
    if (gRunning)
        gStopAsked = true;
    gQuit = true;
    return noErr;
}

static pascal OSErr IgnoreHandler(const AppleEvent *e, AppleEvent *r, long refcon)
{
    return noErr;
}

int main(void)
{
    EventRecord ev;
    Handle bar;
    Boolean chosen[NSUITES], jit, gpu, quick;
    int i;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    MaxApplZone();
    AEInstallEventHandler(kCoreEventClass, kAEQuitApplication, NewAEEventHandlerUPP(QuitHandler), 0, false);
    AEInstallEventHandler(kCoreEventClass, kAEOpenApplication, NewAEEventHandlerUPP(IgnoreHandler), 0, false);
    AEInstallEventHandler(kCoreEventClass, kAEOpenDocuments, NewAEEventHandlerUPP(IgnoreHandler), 0, false);
    AEInstallEventHandler(kCoreEventClass, kAEPrintDocuments, NewAEEventHandlerUPP(IgnoreHandler), 0, false);

    bar = GetNewMBar(128);
    SetMenuBar(bar);
    AppendResMenu(GetMenuHandle(mApple), 'DRVR');
    DrawMenuBar();

    gMain = GetNewDialog(128, NULL, (WindowPtr)-1);
    if (!gMain)
        return 1;
    SetPort((GrafPtr)gMain);
    for (i = 0; i < NSUITES; i++)
        SetCheck(kSuites[i].item, true);
    ReadHost();
    SetCheck(itJit, gCanSwitchJit);
    SetCheck(itGpu, gCanSwitchGpu);
    UpdateButtons();
    UiStatus("Choose the suites and click Run.");
    gBenchYield = Yield;

    if (ReadAuto(chosen, &jit, &gpu, &quick)) {
        gAuto = true;
        HostLog("SHEEPBENCH AUTO");
        for (i = 0; i < 30; i++)            /* let the window draw */
            if (WaitNextEvent(everyEvent, &ev, 2, NULL))
                HandleEvent(&ev);
        RunSuites(chosen, jit && gCanSwitchJit, gpu && gCanSwitchGpu, quick);
        if (gAutoReport)
            WriteAutoReport();
        HostLog("SHEEPBENCH DONE");
        if (gAutoShutdown)
            AskFinderToShutDown();
        if (!gAutoShow)
            return 0;
        gAuto = false;
        ShowAbout();
        ShowResults();
    }

    while (!gQuit) {
        if (WaitNextEvent(everyEvent, &ev, 30, NULL))
            HandleEvent(&ev);
    }
    return 0;
}
