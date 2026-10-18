/*
 *  suite_qd.c - QuickDraw suite: drawing on the screen, where SheepForce's QuickDraw hooks apply.
 *
 *  Each test draws in a window of 64 x 64, 512 x 512 and (nearly) the whole screen: solid fills, inversion, pattern
 *  fills, frames, lines, CopyBits from 32-, 16- and 8-bit offscreen sources, srcXor and srcOr transfer modes, an
 *  overlapping blit (ScrollRect), text and icons. Every repetition ends by reading a pixel back from the screen,
 *  which makes QuickDraw wait for drawing still queued on the GPU, so the time is the time to finish the drawing.
 *
 *  Correctness: before timing, each test draws the same thing on the screen and in an offscreen GWorld (QuickDraw's
 *  own code), reads the screen back and compares the pixels the window really shows (a floating window such as the
 *  Control Strip may cover part of it). Each test names the SheepForce hook it should reach (0 bitblt, 2 fillrect,
 *  3 pattern fill, 5 lines; "rom" when QuickDraw itself draws), and the host's hit counters show which it did.
 *
 *  With SheepForce QuickDraw off (the GPU-off pass) the same tests run on the host CPU or in the ROM.
 *
 *  (C) 2026 Bill Cavalieri
 */

#include "suites.h"
#include "bench_host.h"
#include <Quickdraw.h>
#include <Windows.h>
#include <Fonts.h>
#include <Memory.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char kSuite[] = "quickdraw";

typedef struct {
    WindowPtr win;
    Rect area;              /* local: 0,0,w,h */
    GWorldPtr src32, src16, src8, sync;
    int op;
    int count;              /* operations per iteration */
} QdCtx;

typedef struct {
    const char *name;
    const char *hook;
    int count;              /* operations per iteration (for small areas, more) */
} QdOp;

enum { opPaint, opInvert, opPattern, opFrame, opLines, opCopy32, opCopy16, opCopy8, opXor, opOr, opScroll, opText,
       opIcon, opCount };

static const QdOp kOps[opCount] = {
    { "PaintRect, solid", "2", 1 },
    { "InvertRect", "2", 1 },
    { "FillRect, gray pattern", "3", 1 },
    { "FrameRect, 16 nested", "5", 16 },
    { "LineTo, 32 diagonals", "rom", 32 },
    { "CopyBits srcCopy, 32-bit source", "0", 1 },
    { "CopyBits srcCopy, 16-bit source", "0", 1 },
    { "CopyBits srcCopy, 8-bit source", "0", 1 },
    { "CopyBits srcXor, 32-bit source", "0", 1 },
    { "CopyBits srcOr, 32-bit source", "0", 1 },
    { "ScrollRect by 8 (overlapping blit)", "0", 1 },
    { "DrawString, 12 lines of Geneva 12", "rom", 12 },
    { "PlotIconID, 32 x 32 icons", "rom", 16 },
};

static void Color(int i)
{
    static const RGBColor c[4] = { { 0xFFFF, 0x0000, 0x0000 }, { 0x0000, 0x8000, 0xFFFF }, { 0x2000, 0xC000, 0x2000 },
                                   { 0x0000, 0x0000, 0x0000 } };
    RGBForeColor((RGBColor *)&c[i & 3]);
}

static BitMap *Bits(GWorldPtr g)
{
    return (BitMap *)*GetGWorldPixMap(g);
}

static BitMap *WinBits(WindowPtr w)
{
    return &((GrafPtr)w)->portBits;
}

/* One operation, drawn into the current port (the window or the reference GWorld). */
static void Draw(QdCtx *c, int op, int i)
{
    Rect r = c->area, s;
    int k, w = r.right - r.left, h = r.bottom - r.top;
    switch (op) {
    case opPaint:
        Color(i);
        PaintRect(&r);
        break;
    case opInvert:
        InvertRect(&r);
        break;
    case opPattern:
        Color(i);
        FillRect(&r, &qd.gray);
        break;
    case opFrame:
        Color(i);
        for (k = 0; k < 16 && r.right - r.left > 2 && r.bottom - r.top > 2; k++) {
            FrameRect(&r);
            InsetRect(&r, w / 40 + 1, h / 40 + 1);
        }
        break;
    case opLines:
        Color(i);
        for (k = 0; k < 32; k++) {
            MoveTo(0, (short)(k * h / 32));
            LineTo((short)(w - 1), (short)(h - 1 - k * h / 32));
        }
        break;
    case opCopy32: case opCopy16: case opCopy8: case opXor: case opOr: {
        GWorldPtr g = op == opCopy16 ? c->src16 : op == opCopy8 ? c->src8 : c->src32;
        short mode = op == opXor ? srcXor : op == opOr ? srcOr : srcCopy;
        s = (*GetGWorldPixMap(g))->bounds;
        ForeColor(blackColor);
        BackColor(whiteColor);
        CopyBits(Bits(g), &qd.thePort->portBits, &s, &r, mode, NULL);
        break;
    }
    case opScroll: {
        RgnHandle upd = NewRgn();
        ScrollRect(&r, 0, 8, upd);
        DisposeRgn(upd);
        break;
    }
    case opText:
        Color(i);
        TextFont(kFontIDGeneva);
        TextSize(12);
        for (k = 0; k < 12; k++) {
            MoveTo(4, (short)(14 + k * 15));
            DrawString("\pThe quick brown fox jumps over the lazy dog 0123456789");
        }
        break;
    case opIcon:
        for (k = 0; k < 16; k++) {
            SetRect(&s, (short)((k % 4) * 36), (short)((k / 4) * 36), (short)((k % 4) * 36 + 32), (short)((k / 4) * 36 + 32));
            PlotIconID(&s, 0, 0, (short)(-3999 - (k & 1)));        /* the generic folder and document icons */
        }
        break;
    }
}

/* Reads one pixel back from the screen: QuickDraw then waits for anything still queued on the GPU. */
static void Sync(QdCtx *c)
{
    Rect one = { 0, 0, 1, 1 };
    CGrafPtr oldPort;
    GDHandle oldDev;
    GetGWorld(&oldPort, &oldDev);
    SetGWorld(c->sync, NULL);
    CopyBits(WinBits(c->win), Bits(c->sync), &one, &one, srcCopy, NULL);
    SetGWorld(oldPort, oldDev);
}

static uint32_t Body(void *p, uint32_t iters)
{
    QdCtx *c = (QdCtx *)p;
    uint32_t i;
    SetPort((GrafPtr)c->win);
    for (i = 0; i < iters; i++)
        Draw(c, c->op, (int)i);
    Sync(c);
    return iters;
}

static GWorldPtr NewWorld(short depth, short w, short h)
{
    GWorldPtr g = NULL;
    Rect r;
    SetRect(&r, 0, 0, w, h);
    if (NewGWorld(&g, depth, &r, NULL, NULL, 0) != noErr || !g)
        return NULL;
    LockPixels(GetGWorldPixMap(g));
    return g;
}

/* A source picture: coloured bands and a checkerboard, the same at every depth. */
static void FillSource(GWorldPtr g)
{
    CGrafPtr oldPort;
    GDHandle oldDev;
    Rect r = (*GetGWorldPixMap(g))->bounds, b;
    int k;
    GetGWorld(&oldPort, &oldDev);
    SetGWorld(g, NULL);
    EraseRect(&r);
    for (k = 0; k < 8; k++) {
        b = r;
        b.top = (short)(r.top + k * (r.bottom - r.top) / 8);
        b.bottom = (short)(r.top + (k + 1) * (r.bottom - r.top) / 8);
        Color(k);
        PaintRect(&b);
    }
    ForeColor(blackColor);
    for (k = 0; k < 64; k++) {
        SetRect(&b, (short)((k % 8) * 16), (short)((k / 8) * 16), (short)((k % 8) * 16 + 8), (short)((k / 8) * 16 + 8));
        if ((k + k / 8) & 1)
            PaintRect(&b);
    }
    SetGWorld(oldPort, oldDev);
}

static void Reset(void)
{
    ForeColor(blackColor);
    BackColor(whiteColor);
    PenNormal();
    TextFont(0);
    TextSize(0);
}

/* Draws ops on the screen and in a reference GWorld, reads the screen back and compares where the window shows. */
static Boolean Verify(QdCtx *c, char *why)
{
    short w = c->area.right, h = c->area.bottom;
    GWorldPtr ref = NewWorld(0, w, h), back = NewWorld(0, w, h), mask = NewWorld(1, w, h);
    CGrafPtr oldPort;
    GDHandle oldDev;
    Boolean ok = true;
    int i, x, y;
    why[0] = 0;
    if (!ref || !back || !mask) {
        strcpy(why, "no memory for the check");
        ok = false;
        goto done;
    }
    GetGWorld(&oldPort, &oldDev);
    /* the same drawing twice: the screen (through the hooks) and the reference */
    SetPort((GrafPtr)c->win);
    Reset();
    EraseRect(&c->area);
    if (c->op >= opCopy32 && c->op <= opOr)
        Draw(c, opPaint, 2);        /* something under the transfer modes */
    for (i = 0; i < 3; i++)
        Draw(c, c->op, i);
    Reset();
    SetGWorld(ref, NULL);
    Reset();
    EraseRect(&c->area);
    if (c->op >= opCopy32 && c->op <= opOr)
        Draw(c, opPaint, 2);
    for (i = 0; i < 3; i++)
        Draw(c, c->op, i);
    Reset();
    /* the screen back, and which pixels of the window are visible */
    SetGWorld(back, NULL);
    CopyBits(WinBits(c->win), Bits(back), &c->area, &c->area, srcCopy, NULL);
    SetGWorld(mask, NULL);
    EraseRect(&c->area);
    ForeColor(blackColor);
    PaintRgn(((GrafPtr)c->win)->visRgn);
    SetGWorld(oldPort, oldDev);
    {
        PixMapPtr pr = *GetGWorldPixMap(ref), pb = *GetGWorldPixMap(back), pm = *GetGWorldPixMap(mask);
        int bytes = pr->pixelSize / 8, rbr = pr->rowBytes & 0x3FFF, rbb = pb->rowBytes & 0x3FFF, rbm = pm->rowBytes & 0x3FFF;
        unsigned char *br = (unsigned char *)GetPixBaseAddr(GetGWorldPixMap(ref));
        unsigned char *bb = (unsigned char *)GetPixBaseAddr(GetGWorldPixMap(back));
        unsigned char *bm = (unsigned char *)GetPixBaseAddr(GetGWorldPixMap(mask));
        long seen = 0;
        if (bytes < 1 || pb->pixelSize != pr->pixelSize) {
            sprintf(why, "can only compare 8, 16 or 32-bit screens (%d)", pr->pixelSize);
            ok = false;
            goto done;
        }
        for (y = 0; y < h && ok; y++)
            for (x = 0; x < w; x++) {
                if (!(bm[y * rbm + x / 8] & (0x80 >> (x & 7))))
                    continue;
                seen++;
                /* 32-bit pixels: the top byte is unused by QuickDraw and not shown; SheepShaver's native invert
                 * flips it where QuickDraw leaves it, so it is not compared. */
                if (memcmp(br + y * rbr + x * bytes + (bytes == 4), bb + y * rbb + x * bytes + (bytes == 4),
                           bytes - (bytes == 4)) != 0) {
                    unsigned long want = 0, got = 0;
                    int k;
                    for (k = 0; k < bytes; k++) {
                        want = want << 8 | br[y * rbr + x * bytes + k];
                        got = got << 8 | bb[y * rbb + x * bytes + k];
                    }
                    sprintf(why, "screen %lx, QuickDraw's own drawing %lx at %d,%d", got, want, x, y);
                    ok = false;
                    break;
                }
            }
        if (ok && seen == 0) {
            strcpy(why, "the test window is not visible");
            ok = false;
        }
    }
done:
    if (ref) DisposeGWorld(ref);
    if (back) DisposeGWorld(back);
    if (mask) DisposeGWorld(mask);
    return ok;
}

/* qd_hits from the host as counts per hook code. */
static void Hits(unsigned long h[8])
{
    const char *s;
    int i;
    for (i = 0; i < 8; i++)
        h[i] = 0;
    if (!HostPresent() || !HostInfoRefresh() || !(s = HostInfo("qd_hits")))
        return;
    while (*s) {
        int code = atoi(s);
        const char *colon = strchr(s, ':'), *comma;
        if (!colon)
            break;
        if (code >= 0 && code < 8)
            h[code] = strtoul(colon + 1, NULL, 10);
        comma = strchr(colon, ',');
        if (!comma)
            break;
        s = comma + 1;
    }
}

static void RunSize(const char *sizeName, short w, short h, short top, short left)
{
    QdCtx c;
    Rect r;
    int op;
    memset(&c, 0, sizeof c);
    SetRect(&r, left, top, left + w, top + h);
    c.win = NewCWindow(NULL, &r, "\p", true, plainDBox, (WindowPtr)-1, false, 0);
    if (!c.win)
        return;
    SetRect(&c.area, 0, 0, w, h);
    c.src32 = NewWorld(32, 128, 128);
    c.src16 = NewWorld(16, 128, 128);
    c.src8 = NewWorld(8, 128, 128);
    c.sync = NewWorld(0, 1, 1);
    if (c.src32) FillSource(c.src32);
    if (c.src16) FillSource(c.src16);
    if (c.src8) FillSource(c.src8);
    for (op = 0; op < opCount && !gCancelled; op++) {
        char name[80], why[96];
        BenchRow *row;
        Measurement m;
        unsigned long before[8], after[8];
        int k;
        sprintf(name, "%s, %s", kOps[op].name, sizeName);
        row = TestBegin(kSuite, name, unitPerSec);
        if (!row)
            continue;
        if (gPass == passMain)
            strcpy(row->hook, kOps[op].hook);
        if (!c.sync || (!c.src32 && op >= opCopy32 && op <= opOr) || (op == opCopy16 && !c.src16) || (op == opCopy8 && !c.src8)) {
            if (gPass == passMain)
                RowSkip(row, "no memory for the offscreen pictures");
            continue;
        }
        c.op = op;
        if (!Verify(&c, why)) {
            char line[160];
            sprintf(line, "%s%s", why, gPass == passGpuOff ? " (GPU off)" : gPass == passJitOff ? " (JIT off)" : "");
            RowFail(row, line);
            Say(line);
            continue;
        }
        SetPort((GrafPtr)c.win);
        Reset();
        Hits(before);
        Measure(Body, &c, 1, &m);
        Hits(after);
        Reset();
        if (m.cancelled)
            gCancelled = true;
        TestRecord(row, &m, unitPerSec, (double)kOps[op].count);
        if (gPass == passMain && HostPresent()) {
            char *p = row->hookHits;
            row->hookHits[0] = 0;
            for (k = 0; k < 8; k++)
                if (after[k] > before[k] && strlen(row->hookHits) < sizeof row->hookHits - 12)
                    p += sprintf(p, "%s%d:%lu", row->hookHits[0] ? "," : "", k, after[k] - before[k]);
            if (!row->hookHits[0])
                strcpy(row->hookHits, "none");
        }
    }
    if (c.src32) DisposeGWorld(c.src32);
    if (c.src16) DisposeGWorld(c.src16);
    if (c.src8) DisposeGWorld(c.src8);
    if (c.sync) DisposeGWorld(c.sync);
    DisposeWindow(c.win);
}

void SuiteQuickDraw(void)
{
    GDHandle main = GetMainDevice();
    Rect screen = (**main).gdRect;
    short sw = screen.right - screen.left, sh = screen.bottom - screen.top;
    HideCursor();
    /* Windows start on multiples of 8 so a pattern lines up the same on the screen and in the reference. */
    RunSize("64 x 64", 64, 64, 48, 48);
    if (sw >= 560 && sh >= 560)
        RunSize("512 x 512", 512, 512, 48, 48);
    else
        RowSkip(TestBegin(kSuite, "512 x 512 tests", unitPerSec), "the screen is smaller than 560 x 560");
    RunSize("full screen", sw, (short)(sh - 24), 24, 0);
    ShowCursor();
}
