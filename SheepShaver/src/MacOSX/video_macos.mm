/*
 *  video_macos.mm - AppKit window and framebuffer. No SDL.
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
#include "sysdeps.h"
#include "video.h"
#include "prefs.h"
#include "cpu_emulation.h"
#include "vm_alloc.h"
#include "sheepforce.h"
#include "adb.h"
#include "main.h"
#include "nw_devices.h"

#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>

@interface SheepHost : NSObject
+ (void)showWithDisplay:(NSView *)display;
+ (void)redraw;
@end

extern "C" const uint8 *VideoHostFramebuffer(int *w, int *h, int *row)
{
	*w = SheepForceWidth();
	*h = SheepForceHeight();
	*row = SheepForceRowBytes();
	return SheepForcePageHost(SheepForceVisiblePage());
}

enum {
	csMode = 0,
	csData = 2,
	csPage = 6,
	csBaseAddr = 8
};

static uint8 *the_buffer;
static uint32 the_buffer_size;
static NSWindow *g_window;
static NSView *g_view;
static bool g_quit_requested;

extern "C" void VideoHostRequestQuit(void)
{
	g_quit_requested = true;
}

static void *vm_acquire_framebuffer(uint32 size)
{
#if defined(HAVE_MACH_VM) || (defined(HAVE_MMAP_VM) && defined(__aarch64__))
	return vm_acquire_reserved(size);
#else
	return vm_acquire(size, VM_MAP_DEFAULT | VM_MAP_32BIT);
#endif
}

static uint32 apple_id(int w, int h)
{
	if (w == 640 && h == 480) return APPLE_640x480;
	if (w == 800 && h == 600) return APPLE_800x600;
	if (w == 1024 && h == 768) return APPLE_1024x768;
	if (w == 1152 && h == 870) return APPLE_1152x900;
	if (w == 1280 && h == 1024) return APPLE_1280x1024;
	if (w == 1600 && h == 1200) return APPLE_1600x1200;
	return APPLE_CUSTOM;
}

static void add_mode(int index, int w, int h)
{
	VModes[index].viType = DIS_WINDOW;
	VModes[index].viRowBytes = TrivialBytesPerRow((uint32)w, APPLE_32_BIT);
	VModes[index].viXsize = (uint16)w;
	VModes[index].viYsize = (uint16)h;
	VModes[index].viAppleMode = APPLE_32_BIT;
	VModes[index].viAppleID = apple_id(w, h);
}

static void open_window(int w, int h)
{
	[NSApplication sharedApplication];
	ProcessSerialNumber psn = { 0, kCurrentProcess };
	TransformProcessType(&psn, kProcessTransformToForegroundApplication);
	[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
	[NSApp finishLaunching];
	g_view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, w, h)];
	[SheepHost showWithDisplay:g_view];
}

static void pump_events(void)
{
	NSEvent *event;
	while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
					   untilDate:nil
					      inMode:NSDefaultRunLoopMode
					     dequeue:YES]))
		[NSApp sendEvent:event];
	if (g_quit_requested)
		QuitEmulator();
}

bool VideoInit(void)
{
	int width = 1024;
	int height = 768;
	const char *mode_str = PrefsFindString("screen");
	if (mode_str)
		sscanf(mode_str, "win/%d/%d", &width, &height);
	if (width < 640) width = 640;
	if (height < 480) height = 480;

	static const int sizes[][2] = {
		{ 640, 480 }, { 800, 600 }, { 1024, 768 },
		{ 1280, 1024 }, { 1600, 1200 }
	};
	int n = 0;
	cur_mode = 0;
	for (int i = 0; i < 5; i++) {
		if (sizes[i][0] > width || sizes[i][1] > height)
			continue;
		add_mode(n, sizes[i][0], sizes[i][1]);
		if (sizes[i][0] == width && sizes[i][1] == height)
			cur_mode = n;
		n++;
	}
	if (n == 0 || VModes[cur_mode].viXsize != (uint16)width) {
		add_mode(n, width, height);
		cur_mode = n;
		n++;
	}
	VModes[n].viType = DIS_INVALID;

	display_type = DIS_WINDOW;
	const int pitch = (int)VModes[cur_mode].viRowBytes;
	const int aligned_height = (height + 15) & ~15;
	const int pages = SheepForceEnabled() ? 2 : 1;
	the_buffer_size = (uint32)(aligned_height + 2) * (uint32)pitch * (uint32)pages;
	the_buffer = (uint8 *)vm_acquire_framebuffer(the_buffer_size);
	if (!the_buffer)
		return false;
	memset(the_buffer, 0, the_buffer_size);
	screen_base = Host2MacAddr(the_buffer);

	open_window(width, height);
	SheepForceSetGeometry(the_buffer, screen_base, the_buffer_size / (uint32)pages,
			      width, height, pitch, 32);
	SheepForceStartup(g_view);
	video_activated = true;
	printf("NW-BOOT video AppKit %dx%d fb %08x\n", width, height, (unsigned)screen_base);
	fflush(stdout);
	return true;
}

void VideoExit(void)
{
	SheepForceShutdown();
	g_view = nil;
	g_window = nil;
	video_activated = false;
}

void VideoVBL(void)
{
	VideoHostPresent();
}

void VideoHostPresent(void)
{
	pump_events();
	SheepForcePresent(0, 0, SheepForceWidth(), SheepForceHeight());
	[SheepHost redraw];
}

void VideoDriverVBL(void)
{
	nw_display_vbl_clear();
}
void VideoQuitFullScreen(void) {}
void video_set_palette(void) {}
void video_set_gamma(int) {}
void video_set_cursor(void) {}
bool video_can_change_cursor(void) { return false; }
void video_set_dirty_area(int, int, int, int) {}
void VideoRefresh(void) {}
void VideoInterrupt(void) { VideoHostPresent(); }

int16 video_mode_change(VidLocals *csSave, uint32 ParamPtr)
{
	if (SheepForceEnabled() &&
	    csSave->saveData == ReadMacInt32(ParamPtr + csData) &&
	    csSave->saveMode == ReadMacInt16(ParamPtr + csMode)) {
		int page = (int)ReadMacInt16(ParamPtr + csPage);
		csSave->savePage = (uint16)page;
		SheepForceSetVisiblePage(page);
		SheepForceSync();
		WriteMacInt32(ParamPtr + csBaseAddr, SheepForcePageMac(page));
		csSave->saveBaseAddr = SheepForcePageMac(page);
		return noErr;
	}
	if (csSave->saveData == ReadMacInt32(ParamPtr + csData) &&
	    csSave->saveMode == ReadMacInt16(ParamPtr + csMode))
		return noErr;
	return paramErr;
}
