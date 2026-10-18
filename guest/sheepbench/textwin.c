/*
 *  textwin.c - Scrolling window of fixed-width text lines (see textwin.h).
 *
 *  (C) 2026 Bill Cavalieri
 */

#include "textwin.h"
#include <Quickdraw.h>
#include <Fonts.h>
#include <Memory.h>
#include <Events.h>
#include <string.h>

#define LINE_H 11
#define BAR_W 15
#define MARGIN 4

struct TextWin {
    WindowPtr w;
    ControlHandle bar;
    char *text;             /* lines, each ended by '\n' */
    long *starts;           /* offsets of lines */
    int lines;
    int top;                /* first visible line */
};

#define MAX_WINS 8
static TextWin *gWins[MAX_WINS];

static void Pascal(Str255 out, const char *s)
{
    size_t n = strlen(s);
    if (n > 255)
        n = 255;
    out[0] = (unsigned char)n;
    memcpy(out + 1, s, n);
}

static int Visible(TextWin *t)
{
    return (t->w->portRect.bottom - t->w->portRect.top - BAR_W - MARGIN) / LINE_H;
}

static void PlaceBar(TextWin *t)
{
    Rect r = t->w->portRect;
    int max = t->lines - Visible(t);
    MoveControl(t->bar, r.right - BAR_W, r.top - 1);
    SizeControl(t->bar, BAR_W + 1, r.bottom - r.top - BAR_W + 2);
    if (max < 0)
        max = 0;
    SetControlMaximum(t->bar, max);
    if (t->top > max)
        t->top = max;
    SetControlValue(t->bar, t->top);
}

TextWin *TextWinNew(const char *title, short width, short height)
{
    TextWin *t = (TextWin *)NewPtrClear(sizeof(TextWin));
    Rect r, sb;
    Str255 pt;
    int i;
    if (!t)
        return NULL;
    sb = qd.screenBits.bounds;
    if (width > sb.right - 40)
        width = sb.right - 40;
    if (height > sb.bottom - 80)
        height = sb.bottom - 80;
    SetRect(&r, 20, 60, 20 + width, 60 + height);
    for (i = 0; i < MAX_WINS; i++)
        if (gWins[i])
            OffsetRect(&r, 16, 16);
    Pascal(pt, title);
    t->w = NewCWindow(NULL, &r, pt, true, documentProc + 8 /* zoom box */, (WindowPtr)-1, true, (long)t);
    if (!t->w) {
        DisposePtr((Ptr)t);
        return NULL;
    }
    SetPort(t->w);
    TextFont(kFontIDMonaco);
    TextSize(9);
    r = t->w->portRect;
    SetRect(&r, r.right - BAR_W, r.top - 1, r.right + 1, r.bottom - BAR_W + 1);
    t->bar = NewControl(t->w, &r, "\p", true, 0, 0, 0, scrollBarProc, 0);
    for (i = 0; i < MAX_WINS; i++)
        if (!gWins[i]) {
            gWins[i] = t;
            break;
        }
    return t;
}

void TextWinSetLines(TextWin *t, const char *text)
{
    long n = (long)strlen(text), i;
    int count = 1;
    if (t->text)
        DisposePtr(t->text);
    if (t->starts)
        DisposePtr((Ptr)t->starts);
    for (i = 0; i < n; i++)
        if (text[i] == '\n')
            count++;
    t->text = NewPtr(n + 2);
    t->starts = (long *)NewPtr(sizeof(long) * (count + 1));
    t->lines = 0;
    if (!t->text || !t->starts)
        return;
    memcpy(t->text, text, n);
    if (n == 0 || text[n - 1] != '\n')
        t->text[n++] = '\n';
    t->text[n] = 0;
    t->starts[t->lines++] = 0;
    for (i = 0; i < n - 1; i++)
        if (t->text[i] == '\n')
            t->starts[t->lines++] = i + 1;
    t->top = 0;
    PlaceBar(t);
    SetPort(t->w);
    InvalRect(&t->w->portRect);
}

TextWin *TextWinFind(WindowPtr w)
{
    int i;
    for (i = 0; i < MAX_WINS; i++)
        if (gWins[i] && gWins[i]->w == w)
            return gWins[i];
    return NULL;
}

WindowPtr TextWinWindow(TextWin *t)
{
    return t->w;
}

void TextWinClose(TextWin *t)
{
    int i;
    for (i = 0; i < MAX_WINS; i++)
        if (gWins[i] == t)
            gWins[i] = NULL;
    DisposeWindow(t->w);
    if (t->text)
        DisposePtr(t->text);
    if (t->starts)
        DisposePtr((Ptr)t->starts);
    DisposePtr((Ptr)t);
}

static void DrawText_(TextWin *t)
{
    Rect r = t->w->portRect;
    int i, vis = Visible(t) + 1;
    r.right -= BAR_W;
    r.bottom -= BAR_W;
    SetPort(t->w);
    ClipRect(&r);
    EraseRect(&r);
    for (i = 0; i < vis && t->top + i < t->lines; i++) {
        long s = t->starts[t->top + i], e = s;
        while (t->text[e] && t->text[e] != '\n')
            e++;
        MoveTo(MARGIN, MARGIN + (i + 1) * LINE_H - 2);
        DrawText(t->text, (short)s, (short)(e - s));
    }
    ClipRect(&t->w->portRect);
}

void TextWinUpdate(TextWin *t)
{
    SetPort(t->w);
    BeginUpdate(t->w);
    DrawText_(t);
    DrawControls(t->w);
    DrawGrowIcon(t->w);
    EndUpdate(t->w);
}

void TextWinActivate(TextWin *t, Boolean on)
{
    SetPort(t->w);
    HiliteControl(t->bar, on ? 0 : 255);
    DrawGrowIcon(t->w);
}

static void ScrollTo(TextWin *t, int top)
{
    int max = GetControlMaximum(t->bar);
    if (top > max)
        top = max;
    if (top < 0)
        top = 0;
    if (top == t->top)
        return;
    t->top = top;
    SetControlValue(t->bar, top);
    DrawText_(t);
}

void TextWinClick(TextWin *t, Point where)
{
    ControlHandle c;
    short part;
    SetPort(t->w);
    GlobalToLocal(&where);
    part = FindControl(where, t->w, &c);
    if (!part || c != t->bar)
        return;
    if (part == inThumb) {
        TrackControl(c, where, NULL);
        t->top = -1;
        ScrollTo(t, GetControlValue(c));
        return;
    }
    do {
        int page = Visible(t) - 1;
        if (page < 1)
            page = 1;
        switch (part) {
        case inUpButton: ScrollTo(t, t->top - 1); break;
        case inDownButton: ScrollTo(t, t->top + 1); break;
        case inPageUp: ScrollTo(t, t->top - page); break;
        case inPageDown: ScrollTo(t, t->top + page); break;
        }
        {
            unsigned long until = TickCount() + 4;
            while (TickCount() < until && StillDown())
                ;
        }
    } while (StillDown());
}

void TextWinKey(TextWin *t, char key)
{
    int page = Visible(t) - 1;
    switch ((unsigned char)key) {
    case 0x0B: ScrollTo(t, t->top - page); break;      /* page up */
    case 0x0C: ScrollTo(t, t->top + page); break;      /* page down */
    case 0x01: ScrollTo(t, 0); break;                  /* home */
    case 0x04: ScrollTo(t, t->lines); break;           /* end */
    case 0x1E: ScrollTo(t, t->top - 1); break;         /* up arrow */
    case 0x1F: ScrollTo(t, t->top + 1); break;         /* down arrow */
    }
}

void TextWinGrow(TextWin *t, Point where)
{
    Rect limits;
    long size;
    SetRect(&limits, 200, 100, 2000, 2000);
    size = GrowWindow(t->w, where, &limits);
    if (!size)
        return;
    SizeWindow(t->w, (short)(size & 0xFFFF), (short)(size >> 16), true);
    SetPort(t->w);
    InvalRect(&t->w->portRect);
    PlaceBar(t);
}
