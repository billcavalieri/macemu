/*
 *  prefs_macosx.mm - Mac preferences entry. The window is SwiftUI.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "sysdeps.h"
#include "prefs.h"

void prefs_init(void)
{
}

void prefs_exit(void)
{
}

extern "C" const char *SheepPrefsGetString(const char *key)
{
	const char *s = PrefsFindString(key);
	return s ? s : "";
}

extern "C" void SheepPrefsSetString(const char *key, const char *value)
{
	PrefsReplaceString(key, value ? value : "");
}

extern "C" int SheepPrefsGetBool(const char *key)
{
	return PrefsFindBool(key) ? 1 : 0;
}

extern "C" void SheepPrefsSetBool(const char *key, int value)
{
	PrefsReplaceBool(key, value != 0);
}

extern "C" int SheepPrefsGetInt(const char *key)
{
	return (int)PrefsFindInt32(key);
}

extern "C" void SheepPrefsSetInt(const char *key, int value)
{
	PrefsReplaceInt32(key, value);
}

extern "C" void SheepPrefsSave(void)
{
	SavePrefs();
}
