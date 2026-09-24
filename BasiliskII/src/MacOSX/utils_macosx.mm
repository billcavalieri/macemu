/*
 *	utils_macosx.mm - Mac OS X utility functions.
 *
 *  Copyright (C) 2011 Alexei Svitkine
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

#include <Cocoa/Cocoa.h>
#include <ApplicationServices/ApplicationServices.h>
#include "sysdeps.h"
#include "utils_macosx.h"

#include <sys/sysctl.h>
#include <Metal/Metal.h>

bool MetalIsAvailable() {
	const int EL_CAPITAN = 15; // Darwin major version of El Capitan
	char s[16];
	size_t size = sizeof(s);
	int v;
	if (sysctlbyname("kern.osrelease", s, &size, NULL, 0) || sscanf(s, "%d", &v) != 1 || v < EL_CAPITAN) return false;
	id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
	bool r = dev != nil;
	[dev release];
	return r;
}

static void strip_menu_key_equivalents(NSMenu *menu)
{
	if (!menu)
		return;
	for (NSMenuItem *item in menu.itemArray) {
		item.keyEquivalent = @"";
		item.keyEquivalentModifierMask = 0;
		if (item.hasSubmenu)
			strip_menu_key_equivalents(item.submenu);
	}
}

void disable_SDL2_macosx_menu_bar_keyboard_shortcuts() {
	if (![NSThread isMainThread]) {
		dispatch_sync(dispatch_get_main_queue(), ^{
			disable_SDL2_macosx_menu_bar_keyboard_shortcuts();
		});
		return;
	}
	/* Cmd-Q (Quit) and Cmd-W (Close) must reach the guest, not Cocoa. */
	strip_menu_key_equivalents([NSApp mainMenu]);
	for (NSMenuItem * menu_item in [NSApp mainMenu].itemArray) {
		if ([menu_item.title isEqualToString:@"View"]) {
			[[NSApp mainMenu] removeItem:menu_item];
			break;
		}
	}
}

void set_menu_bar_visible_osx(bool visible)
{
	[NSMenu setMenuBarVisible:(visible ? YES : NO)];
}

void set_current_directory()
{
	NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
	chdir([[[[NSBundle mainBundle] bundlePath] stringByDeletingLastPathComponent] UTF8String]);
	[pool release];
}

void macosx_force_host_cursor(bool show)
{
	if (!show)
		return;
	/* SDL_SetRelativeMouseMode hides via a Cocoa counter SDL_ShowCursor
	 * does not always drain. Unhide until the pointer is actually back. */
	CGAssociateMouseAndMouseCursorPosition(true);
	for (int i = 0; i < 8; i++) {
		[NSCursor unhide];
		CGDisplayShowCursor(kCGDirectMainDisplay);
	}
}
