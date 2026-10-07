/*
 *  nw_log.h - logging levels
 *
 *  A normal (Release) run prints status lines (what was found, what mode it runs in), warnings and errors, and
 *  nothing that repeats or is driven by a timer. Diagnostics are printed with NW_DIAG(): they are on in Debug
 *  builds (NW_BOOT_LOG=1), and in any build when the environment has NW_VERBOSE=1. NW_VERBOSE=0 silences them
 *  in a Debug build. Code that is only worth compiling for a trace stays under #if NW_BOOT_LOG.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */
#ifndef NW_LOG_H
#define NW_LOG_H

#include <stdio.h>
#include <stdlib.h>

inline int nw_log_on(void)
{
	static const int on = []() -> int {
		const char *e = getenv("NW_VERBOSE");
		if (e && e[0])
			return e[0] != '0';
#if defined(NW_BOOT_LOG) && NW_BOOT_LOG
		return 1;
#else
		return 0;
#endif
	}();
	return on;
}

#define NW_DIAG(...) do { if (nw_log_on()) printf(__VA_ARGS__); } while (0)

#endif
