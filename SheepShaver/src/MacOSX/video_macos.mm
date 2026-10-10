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
#include "nw_io.h"
#include "timer.h"

#include <atomic>

#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>
#include <string.h>
#include <unistd.h>

extern "C" int HostLaunchEmbedded(void);

@interface SheepHost : NSObject
+ (NSView *)showWithWidth:(int)width height:(int)height;
+ (const char *)showEmbeddedWithWidth:(int)width height:(int)height;
+ (void)setGuestWidth:(int)width height:(int)height;
+ (void)applyMacCursor;
+ (void)moveMacCursorX:(int)x y:(int)y visible:(int)visible;
+ (const char *)waitForConfig;
+ (void)reportProblemText:(const char *)text;
@end

/* A problem the user should see in a window (for instance a disk image another VM holds). Safe from any thread;
 * shown at once, or as soon as the window exists. */
extern "C" void HostReportProblem(const char *text)
{
	if (text)
		[SheepHost reportProblemText:text];
}

static void host_cursor_moved(int x, int y, int visible)
{
	[SheepHost moveMacCursorX:x y:y visible:visible];
}

extern "C" const uint8 *VideoHostCursorBytes(void)
{
	return MacCursor;
}

extern "C" const char *SheepHostWaitForConfig(void)
{
	return [SheepHost waitForConfig];
}

enum {
	csMode = 0,
	csData = 2,
	csPage = 6,
	csBaseAddr = 8
};

static uint8 *the_buffer;
static uint32 the_buffer_size;
static bool g_quit_requested;
static bool g_in_present;
static std::atomic<uint64_t> g_present_n;
static std::atomic<uint64_t> g_present_us_max;
static std::atomic<uint64_t> g_picture_changes;
static uint32 g_picture_hash;
static int g_picture_have;

extern "C" void VideoPlaybackTake(uint64_t *presents, uint64_t *present_us_max, uint64_t *pictures)
{
	if (presents)
		*presents = g_present_n.exchange(0, std::memory_order_relaxed);
	if (present_us_max)
		*present_us_max = g_present_us_max.exchange(0, std::memory_order_relaxed);
	if (pictures)
		*pictures = g_picture_changes.exchange(0, std::memory_order_relaxed);
}

extern "C" void VideoHostRequestQuit(void)
{
	g_quit_requested = true;
}

extern "C" void VideoHostKey(int code, int down)
{
	if (code < 0 || code > 0x7f)
		return;
	nw_adb_key((uint8)code, down);
}

extern "C" void VideoHostMouseMove(int dx, int dy)
{
	nw_adb_mouse_move(dx, dy);
}

extern "C" void VideoHostSetRelMouse(int on)
{
	ADBSetRelMouseMode(on != 0);
}

extern "C" void VideoHostMouseAbs(int x, int y)
{
	ADBSetAbsMouse(x, y);
}

extern "C" void VideoHostMouseButton(int button, int down)
{
	if (down)
		ADBMouseDown(button);
	else
		ADBMouseUp(button);
}

/* Software cursor: hide the host arrow only over the guest picture, and
 * only after VSL is drawing it. NSCursor.set() is process-wide and blanks
 * the title bar and other windows. */
extern "C" int VideoGuestCursorHidesHost(void)
{
	if (PrefsFindBool("hardcursor"))
		return 0;
	if (private_data == NULL || !private_data->interruptsEnabled)
		return 0;
	return 1;
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

static void add_mode(int index, int w, int h, uint32 depth_mode)
{
	VModes[index].viType = DIS_WINDOW;
	VModes[index].viRowBytes = TrivialBytesPerRow((uint32)w, (int)depth_mode);
	VModes[index].viXsize = (uint16)w;
	VModes[index].viYsize = (uint16)h;
	VModes[index].viAppleMode = depth_mode;
	VModes[index].viAppleID = apple_id(w, h);
}

static int depth_bits_for_mode(uint32 apple_mode)
{
	return 1 << (int)(apple_mode - APPLE_1_BIT);
}

static void apply_guest_mode(int index)
{
	cur_mode = index;
	const int w = VModes[index].viXsize;
	const int h = VModes[index].viYsize;
	const int row = (int)VModes[index].viRowBytes;
	const int depth = depth_bits_for_mode(VModes[index].viAppleMode);
	const int pages = SheepForceEnabled() ? 2 : 1;
	SheepForceSetGeometry(the_buffer, screen_base, the_buffer_size / (uint32)pages,
			      w, h, row, depth);
	[SheepHost setGuestWidth:w height:h];
	SheepForceLayoutDisplay();
	SheepForceMarkDirty();
}

/* The scanout target of an embedded VM (Swift: SheepHost.detachToWindow / attachToLibrary) */
extern "C" void VideoHostRequestWindowSink(void *ns_view) { SheepForceRequestWindowSink(ns_view); }
extern "C" void VideoHostRequestShmSink(void) { SheepForceRequestShmSink(); }
extern "C" int VideoHostShmSinkActive(void) { return SheepForceShmActive() ? 1 : 0; }

extern "C" void VideoHostRun(void)
{
	[NSApp run];
}

bool VideoInit(void)
{
	video_cursor_moved = host_cursor_moved;
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
	static const uint32 depths[] = {
		APPLE_1_BIT, APPLE_2_BIT, APPLE_4_BIT,
		APPLE_8_BIT, APPLE_16_BIT, APPLE_32_BIT
	};
	int n = 0;
	cur_mode = 0;
	for (int i = 0; i < 5; i++) {
		if (sizes[i][0] > width || sizes[i][1] > height)
			continue;
		for (int d = 0; d < 6; d++) {
			add_mode(n, sizes[i][0], sizes[i][1], depths[d]);
			if (sizes[i][0] == width && sizes[i][1] == height && depths[d] == APPLE_32_BIT)
				cur_mode = n;
			n++;
		}
	}
	if (n == 0 || VModes[cur_mode].viXsize != (uint16)width) {
		for (int d = 0; d < 6; d++) {
			add_mode(n, width, height, depths[d]);
			if (depths[d] == APPLE_32_BIT)
				cur_mode = n;
			n++;
		}
	}
	VModes[n].viType = DIS_INVALID;

	display_type = DIS_WINDOW;
	const int pitch32 = (int)TrivialBytesPerRow((uint32)width, APPLE_32_BIT);
	const int aligned_height = (height + 15) & ~15;
	const int pages = SheepForceEnabled() ? 2 : 1;
	the_buffer_size = (uint32)(aligned_height + 2) * (uint32)pitch32 * (uint32)pages;
	const uint32 page = (uint32)getpagesize();
	the_buffer_size = (the_buffer_size + page - 1) & ~(page - 1);
	the_buffer = (uint8 *)vm_acquire_framebuffer(the_buffer_size);
	if (!the_buffer)
		return false;
	memset(the_buffer, 0, the_buffer_size);
	screen_base = Host2MacAddr(the_buffer);
	for (int i = 0; i < 256; i++) {
		mac_gamma[i].red = mac_gamma[i].green = mac_gamma[i].blue = (uint8)i;
		mac_pal[i].red = mac_pal[i].green = mac_pal[i].blue = (uint8)i;
	}
	mac_pal[0].red = mac_pal[0].green = mac_pal[0].blue = 255;
	mac_pal[1].red = mac_pal[1].green = mac_pal[1].blue = 0;

	/* Embedded VM: no window; the picture goes to shared memory for the library window (display_shm.h) */
	NSView *display = nil;
	const char *shm_name = NULL;
	if (HostLaunchEmbedded())
		shm_name = [SheepHost showEmbeddedWithWidth:width height:height];
	else
		display = [SheepHost showWithWidth:width height:height];
	apply_guest_mode(cur_mode);
	SheepForceStartup((__bridge void *)display);
	if (shm_name) {
		if (!SheepForceShmCreate(shm_name, width, height))
			printf("SheepForce: could not create the shared memory scanout %s\n", shm_name);
		free((void *)shm_name);
	}
	SheepForceAdoptHostFB(the_buffer, the_buffer_size);
	SheepForceLoadPalette();
	/* Stay absolute until a click grab. Ctrl-G is the release. */
	VideoHostSetRelMouse(0);
	video_activated = true;
	printf("NW-BOOT video AppKit %dx%d fb %08x\n", width, height, (unsigned)screen_base);
	fflush(stdout);
	return true;
}

void VideoExit(void)
{
	CGAssociateMouseAndMouseCursorPosition(true);
	SheepForceShutdown();
	video_activated = false;
}

void VideoVBL(void)
{
	VideoHostPresent();
}

/*
 *  Screenshots for the control channel (the MCP server). The guest picture is read from guest memory, not from the
 *  window, so it works while the window is hidden or covered. The copy is made on the emulation thread, from
 *  VideoHostPresent, after any GPU drawing still in flight has finished; the caller waits for it.
 */
struct snapshot_request {
	std::atomic<bool> wanted{false};
	dispatch_semaphore_t done = NULL;
	uint8 *pixels = NULL;
	uint32 length = 0;
	int info[4] = {0, 0, 0, 0};		/* width, height, row bytes, depth in bits */
	uint8 palette[256 * 3];
} static g_snap;
static dispatch_semaphore_t g_snap_lock;	/* one request at a time */

static void service_snapshot(void)
{
	const int w = VModes[cur_mode].viXsize, h = VModes[cur_mode].viYsize;
	const int rb = VModes[cur_mode].viRowBytes;
	const uint8 *src = SheepForceEnabled() ? SheepForcePageHost(SheepForceVisiblePage()) : the_buffer;
	g_snap.pixels = NULL;
	if (src && w > 0 && h > 0 && rb > 0) {
		SheepForceFlushCPU(NULL, 0, 0, 0);		/* wait for QuickDraw/GPU writes still in flight */
		g_snap.length = (uint32)rb * (uint32)h;
		g_snap.pixels = (uint8 *)malloc(g_snap.length);
		if (g_snap.pixels)
			memcpy(g_snap.pixels, src, g_snap.length);
		g_snap.info[0] = w; g_snap.info[1] = h; g_snap.info[2] = rb;
		g_snap.info[3] = depth_bits_for_mode(VModes[cur_mode].viAppleMode);
		for (int i = 0; i < 256; i++) {
			g_snap.palette[i * 3 + 0] = mac_gamma[mac_pal[i].red].red;
			g_snap.palette[i * 3 + 1] = mac_gamma[mac_pal[i].green].green;
			g_snap.palette[i * 3 + 2] = mac_gamma[mac_pal[i].blue].blue;
		}
	}
	g_snap.wanted.store(false);
	dispatch_semaphore_signal(g_snap.done);
}

/* Fills info (width, height, row bytes, depth), palette (256 RGB triples) and *pixels (malloc'd, rowbytes*height bytes;
 * release with VideoHostSnapshotFree). Returns 0 on success, -1 when the guest did not answer in time. Any thread. */
extern "C" int VideoHostSnapshot(int *info, uint8 *palette, uint8 **pixels, uint32 *length)
{
	if (!g_snap_lock) {
		static dispatch_once_t once;
		dispatch_once(&once, ^{ g_snap_lock = dispatch_semaphore_create(1); g_snap.done = dispatch_semaphore_create(0); });
	}
	if (dispatch_semaphore_wait(g_snap_lock, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC)) != 0)
		return -1;
	g_snap.wanted.store(true);
	int rc = 0;
	if (dispatch_semaphore_wait(g_snap.done, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC)) != 0) {
		g_snap.wanted.store(false);
		/* the request may be serviced right now; wait it out so the slot is clean for the next one */
		dispatch_semaphore_wait(g_snap.done, dispatch_time(DISPATCH_TIME_NOW, 200 * NSEC_PER_MSEC));
		rc = -1;
	} else if (!g_snap.pixels) {
		rc = -1;
	} else {
		memcpy(info, g_snap.info, sizeof(g_snap.info));
		memcpy(palette, g_snap.palette, sizeof(g_snap.palette));
		*pixels = g_snap.pixels;
		*length = g_snap.length;
		g_snap.pixels = NULL;
	}
	dispatch_semaphore_signal(g_snap_lock);
	return rc;
}

extern "C" void VideoHostSnapshotFree(uint8 *pixels)
{
	free(pixels);
}

/* The guest screen as the guest sees it: size in pixels and depth in bits. Any thread. */
extern "C" void VideoHostGuestScreen(int *width, int *height, int *depth)
{
	*width = VModes[cur_mode].viXsize;
	*height = VModes[cur_mode].viYsize;
	*depth = depth_bits_for_mode(VModes[cur_mode].viAppleMode);
}

/* The guest's cursor position (low-memory Mouse, a Point: v then h), in guest pixels. Any thread. */
extern "C" void VideoHostGuestMouse(int *x, int *y)
{
	*y = (int16)ReadMacInt16(0x830);
	*x = (int16)ReadMacInt16(0x832);
}

void VideoHostPresent(void)
{
	if (g_snap.wanted.load(std::memory_order_acquire))
		service_snapshot();
	if (g_quit_requested)
		QuitEmulator();
	if (g_in_present)
		return;
	g_in_present = true;
	static uint64 last;
	uint64 now = GetTicks_usec();
	if (now - last >= 8000 || last == 0) {
		last = now;
		const uint64 t0 = GetTicks_usec();
		SheepForcePresent(0, 0, SheepForceWidth(), SheepForceHeight());
		const uint64 dt = GetTicks_usec() - t0;
		g_present_n.fetch_add(1, std::memory_order_relaxed);
		uint64 prev = g_present_us_max.load(std::memory_order_relaxed);
		while (dt > prev && !g_present_us_max.compare_exchange_weak(prev, dt, std::memory_order_relaxed))
			;
		int have = 0;
		const uint32 hash = SheepForcePresentedHash(&have);
		nw_fb_fps_proxy_note(hash, have);
		if (have) {
			if (g_picture_have && hash != g_picture_hash)
				g_picture_changes.fetch_add(1, std::memory_order_relaxed);
			g_picture_hash = hash;
			g_picture_have = 1;
		}
	}
	g_in_present = false;
}

void VideoDriverVBL(void)
{
	nw_display_vbl_clear();
	if (!VideoVBLShouldService())
		return;
	VSLDoInterruptService(private_data->vslServiceID);
}

void VideoQuitFullScreen(void) {}

void video_set_palette(void)
{
	SheepForceLoadPalette();
}

void video_set_gamma(int)
{
	SheepForceLoadPalette();
}

void video_set_cursor(void)
{
	[SheepHost applyMacCursor];
}

bool video_can_change_cursor(void)
{
	/* Always. The arrow is drawn above the picture, so a movie cannot cover it. */
	return true;
}

void video_set_dirty_area(int, int, int, int)
{
	SheepForceMarkDirty();
}

void VideoRefresh(void) {}
void VideoInterrupt(void) { VideoHostPresent(); }

int16 video_mode_change(VidLocals *csSave, uint32 ParamPtr)
{
	const uint32 data = ReadMacInt32(ParamPtr + csData);
	const uint16 mode = ReadMacInt16(ParamPtr + csMode);
	const int page = (int)ReadMacInt16(ParamPtr + csPage);

	if (csSave->saveData == data && csSave->saveMode == mode) {
		if (SheepForceEnabled()) {
			csSave->savePage = (uint16)page;
			SheepForceSetVisiblePage(page);
			WriteMacInt32(ParamPtr + csBaseAddr, SheepForcePageMac(page));
			csSave->saveBaseAddr = SheepForcePageMac(page);
		}
		return noErr;
	}
	for (int i = 0; VModes[i].viType != DIS_INVALID; i++) {
		if (mode == VModes[i].viAppleMode && data == VModes[i].viAppleID) {
			csSave->saveMode = mode;
			csSave->saveData = data;
			csSave->savePage = (uint16)page;
			apply_guest_mode(i);
			uint32 base = SheepForceEnabled() ? SheepForcePageMac(page) : screen_base;
			SheepForceSetVisiblePage(page);
			WriteMacInt32(ParamPtr + csBaseAddr, base);
			csSave->saveBaseAddr = base;
			return noErr;
		}
	}
	return paramErr;
}
