/*
 *  textwin.h - A window that shows lines of fixed-width text with a vertical scroll bar: the results, a comparison
 *  of two reports, and About.
 *
 *  (C) 2026 Bill Cavalieri
 */

#ifndef TEXTWIN_H
#define TEXTWIN_H

#include <MacTypes.h>
#include <Windows.h>

typedef struct TextWin TextWin;

/* Makes a window with the given lines (copied). `title` is a C string. */
TextWin *TextWinNew(const char *title, short width, short height);
void TextWinSetLines(TextWin *t, const char *text);     /* lines separated by '\n' */
void TextWinClose(TextWin *t);
TextWin *TextWinFind(WindowPtr w);
WindowPtr TextWinWindow(TextWin *t);
void TextWinUpdate(TextWin *t);
void TextWinClick(TextWin *t, Point where);
void TextWinActivate(TextWin *t, Boolean on);
void TextWinGrow(TextWin *t, Point where);
void TextWinKey(TextWin *t, char key);                  /* page up/down, home, end, arrows */

#endif
