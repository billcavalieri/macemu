/*
 *  nw_script.h - New World operator script (Debug builds only)
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef NW_SCRIPT_H
#define NW_SCRIPT_H

/*
 * The counterpart of the golden capture's QMP driver (capture.py): timed
 * keyboard and mouse input into the modelled ADB bus plus guest frame-buffer
 * snapshots, so an unattended run can be walked through the Finder and the
 * installer and looked at afterwards. Only what a person at the window
 * could do; nothing in the guest is touched.
 *
 * Enabled by NW_SCRIPT=<file> in the environment (Debug, NW_BOOT_LOG builds).
 * One command per line, `#` comments, times in seconds since the guest
 * started; `at` lines must be in increasing order.
 *
 *   at <sec> shot <path>          P6 PPM of the frame buffer (%d = shot index)
 *   every <sec> shot <path>       the same, repeated
 *   at <sec> text <string>        type it (US layout, shift as needed)
 *   at <sec> key <k>[+<k>...]     e.g. `key cmd+o`, `key return`, `key 0x24`
 *   at <sec> mouse <x> <y>        move the guest cursor to that pixel
 *   at <sec> click [<x> <y>]      button 0 down/up (after a move, if given)
 *   at <sec> dblclick [<x> <y>]
 *   at <sec> down | up            button 0 held / released
 *   at <sec> log <text>           `NW-BOOT SCRIPT <text>` in the log
 *   at <sec> dump <hexaddr> <hexlen> <path>   guest RAM (logical) to a file
 *
 * Cursor moves are closed-loop on the guest's Mouse low-memory global,
 * so acceleration and clamping in the guest do not matter.
 */

#ifdef __cplusplus
extern "C" {
#endif

void nw_script_init(void);	/* reads NW_SCRIPT; no-op when unset */
void nw_script_tick(void);	/* from the CPU thread's coarse tick */

#ifdef __cplusplus
}
#endif

#endif
