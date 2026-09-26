/*
 *  sheepforce_metal.mm - Metal scanout, QuickDraw writes, and RAVE triangles.
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
#include "sheepforce.h"
#include "video.h"
#include "cpu_emulation.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Cocoa/Cocoa.h>

static id<MTLDevice> g_dev;
static id<MTLCommandQueue> g_queue;
static id<MTLLibrary> g_lib;
static id<MTLRenderPipelineState> g_present_pipe;
static id<MTLComputePipelineState> g_fill_pipe;
static id<MTLComputePipelineState> g_inv_pipe;
static id<MTLComputePipelineState> g_blit_pipe;
static id<MTLRenderPipelineState> g_tri_pipe;
static CAMetalLayer *g_layer;
static NSView *g_view;
static id<MTLBuffer> g_pal;
static id<MTLBuffer> g_fb;
static id<MTLBuffer> g_meta;
static id<MTLCommandBuffer> g_pending;
static id<MTLBuffer> g_src;
static int g_flight_x, g_flight_y, g_flight_w, g_flight_h;
static bool g_flight_on;
static uint8 *g_fb_host;
static const uint8 *g_presented;
static uint32 g_presented_bytes;
static bool g_fb_nocopy;
static bool g_logged_fail;
static bool g_logged_copy;
static bool g_dirty = true;
static int g_layout_w, g_layout_h;
static uint g_meta_depth = 32;

static NSString *SheepForceShaderSource(void)
{
	return @
	"#include <metal_stdlib>\n"
	"using namespace metal;\n"
	"struct VOut { float4 p [[position]]; float2 uv; };\n"
	"vertex VOut sf_vs(uint id [[vertex_id]]) {\n"
	"  float2 q[3] = { float2(-1.0,-1.0), float2(3.0,-1.0), float2(-1.0,3.0) };\n"
	"  VOut o; o.p = float4(q[id], 0.0, 1.0);\n"
	"  o.uv = float2(q[id].x * 0.5 + 0.5, 1.0 - (q[id].y * 0.5 + 0.5));\n"
	"  return o;\n"
	"}\n"
	"static float4 pal_rgb(constant uchar4 *pal, uint i) {\n"
	"  uchar4 p = pal[i];\n"
	"  return float4(float(p.z) / 255.0, float(p.y) / 255.0, float(p.x) / 255.0, 1.0);\n"
	"}\n"
	"fragment float4 sf_fs(VOut in [[stage_in]], constant uchar4 *pal [[buffer(0)]],\n"
	"    device const uchar *pix [[buffer(2)]], constant uint *meta [[buffer(3)]]) {\n"
	"  uint w = meta[0], h = meta[1], row = meta[2], depth = meta[3], off = meta[4];\n"
	"  uint x = min(uint(in.uv.x * float(w)), w - 1u);\n"
	"  uint y = min(uint(in.uv.y * float(h)), h - 1u);\n"
	"  if (depth == 1u) {\n"
	"    uint byte = pix[off + y * row + (x >> 3)];\n"
	"    uint bit = 0x80u >> (x & 7u);\n"
	"    return pal_rgb(pal, (byte & bit) ? 1u : 0u);\n"
	"  }\n"
	"  if (depth == 2u) {\n"
	"    uint byte = pix[off + y * row + (x >> 2)];\n"
	"    uint shift = (3u - (x & 3u)) * 2u;\n"
	"    return pal_rgb(pal, (byte >> shift) & 3u);\n"
	"  }\n"
	"  if (depth == 4u) {\n"
	"    uint byte = pix[off + y * row + (x >> 1)];\n"
	"    uint i = ((x & 1u) == 0u) ? (byte >> 4) : (byte & 15u);\n"
	"    return pal_rgb(pal, i);\n"
	"  }\n"
	"  if (depth == 8u) {\n"
	"    return pal_rgb(pal, pix[off + y * row + x]);\n"
	"  }\n"
	"  if (depth == 16u) {\n"
	"    uint a = off + y * row + x * 2u;\n"
	"    uint v = (uint(pix[a]) << 8) | uint(pix[a + 1]);\n"
	"    return float4(float((v >> 10) & 31u) / 31.0, float((v >> 5) & 31u) / 31.0, float(v & 31u) / 31.0, 1.0);\n"
	"  }\n"
	"  uint a = off + y * row + x * 4u;\n"
	"  return float4(float(pix[a + 1]) / 255.0, float(pix[a + 2]) / 255.0, float(pix[a + 3]) / 255.0, 1.0);\n"
	"}\n"
	"struct FillU { uint x, y, w, h, row, bpp, color; };\n"
	"kernel void sf_fill(device uchar *pix [[buffer(0)]], constant FillU &u [[buffer(1)]],\n"
	"    uint2 gid [[thread_position_in_grid]]) {\n"
	"  if (gid.x >= u.w || gid.y >= u.h) return;\n"
	"  device uchar *d = pix + (u.y + gid.y) * u.row + (u.x + gid.x) * u.bpp;\n"
	"  for (uint i = 0; i < u.bpp; i++) d[i] = (uchar)((u.color >> (8u * i)) & 0xffu);\n"
	"}\n"
	"kernel void sf_inv(device uchar *pix [[buffer(0)]], constant FillU &u [[buffer(1)]],\n"
	"    uint2 gid [[thread_position_in_grid]]) {\n"
	"  if (gid.x >= u.w || gid.y >= u.h) return;\n"
	"  device uchar *d = pix + (u.y + gid.y) * u.row + (u.x + gid.x) * u.bpp;\n"
	"  for (uint i = 0; i < u.bpp; i++) d[i] = ~d[i];\n"
	"}\n"
	"struct BlitU { uint w, h, dst_row, src_row, bpp; };\n"
	"kernel void sf_blit(device uchar *dst [[buffer(0)]], device const uchar *src [[buffer(1)]],\n"
	"    constant BlitU &u [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) {\n"
	"  if (gid.x >= u.w || gid.y >= u.h) return;\n"
	"  device uchar *d = dst + gid.y * u.dst_row + gid.x * u.bpp;\n"
	"  device const uchar *s = src + gid.y * u.src_row + gid.x * u.bpp;\n"
	"  for (uint i = 0; i < u.bpp; i++) d[i] = s[i];\n"
	"}\n"
	"struct TriV { float4 p [[position]]; float4 color; };\n"
	"vertex TriV sf_tri_vs(uint id [[vertex_id]], constant packed_float2 *xy [[buffer(0)]],\n"
	"    constant float4 &color [[buffer(1)]]) {\n"
	"  float2 n = xy[id];\n"
	"  TriV o; o.p = float4(n.x * 2.0 - 1.0, 1.0 - n.y * 2.0, 0.0, 1.0); o.color = color; return o;\n"
	"}\n"
	"fragment float4 sf_tri_fs(TriV in [[stage_in]]) { return in.color; }\n";
}

static bool SheepForceMakePipes(void)
{
	if (!g_dev || !g_lib)
		return false;
	NSError *err = nil;
	id<MTLFunction> vs = [g_lib newFunctionWithName:@"sf_vs"];
	id<MTLFunction> fs = [g_lib newFunctionWithName:@"sf_fs"];
	id<MTLFunction> fill = [g_lib newFunctionWithName:@"sf_fill"];
	id<MTLFunction> inv = [g_lib newFunctionWithName:@"sf_inv"];
	id<MTLFunction> blit = [g_lib newFunctionWithName:@"sf_blit"];
	id<MTLFunction> tvs = [g_lib newFunctionWithName:@"sf_tri_vs"];
	id<MTLFunction> tfs = [g_lib newFunctionWithName:@"sf_tri_fs"];
	if (!vs || !fs || !fill || !inv || !blit || !tvs || !tfs)
		return false;
	MTLRenderPipelineDescriptor *pd = [[MTLRenderPipelineDescriptor alloc] init];
	pd.vertexFunction = vs;
	pd.fragmentFunction = fs;
	pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
	g_present_pipe = [g_dev newRenderPipelineStateWithDescriptor:pd error:&err];
	g_fill_pipe = [g_dev newComputePipelineStateWithFunction:fill error:&err];
	g_inv_pipe = [g_dev newComputePipelineStateWithFunction:inv error:&err];
	g_blit_pipe = [g_dev newComputePipelineStateWithFunction:blit error:&err];
	MTLRenderPipelineDescriptor *td = [[MTLRenderPipelineDescriptor alloc] init];
	td.vertexFunction = tvs;
	td.fragmentFunction = tfs;
	td.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
	g_tri_pipe = [g_dev newRenderPipelineStateWithDescriptor:td error:&err];
	return g_present_pipe != nil && g_fill_pipe != nil && g_tri_pipe != nil;
}

static bool SheepForceLoadLibrary(void)
{
	NSError *err = nil;
	g_lib = [g_dev newDefaultLibrary];
	if (!SheepForceMakePipes()) {
		g_lib = [g_dev newLibraryWithSource:SheepForceShaderSource() options:nil error:&err];
		if (!SheepForceMakePipes()) {
			if (!g_logged_fail) {
				g_logged_fail = true;
				printf("SheepForce: Metal library failed\n");
			}
			return false;
		}
	}
	return true;
}

void SheepForceMarkDirty(void)
{
	g_dirty = true;
}

static bool flight_hits(int x, int y, int w, int h)
{
	return g_flight_on && w > 0 && h > 0 && g_flight_w > 0 && g_flight_h > 0
		&& x < g_flight_x + g_flight_w && g_flight_x < x + w
		&& y < g_flight_y + g_flight_h && g_flight_y < y + h;
}

static void note_flight(uint8 *dest, int rowbytes, int width_bytes, int height)
{
	uint8 *base = SheepForcePageHost(0);
	g_flight_on = false;
	if (!base || !dest || dest < base || rowbytes < 1 || width_bytes < 1 || height < 1)
		return;
	uint32 off = (uint32)(dest - base);
	g_flight_x = (int)(off % (uint32)rowbytes);
	g_flight_y = (int)(off / (uint32)rowbytes);
	g_flight_w = width_bytes;
	g_flight_h = height;
	g_flight_on = true;
}

void SheepForceFlushCPU(uint8 *dest, int rowbytes, int width_bytes, int height)
{
	if (!g_pending)
		return;
	if (dest && rowbytes > 0) {
		uint8 *base = SheepForcePageHost(0);
		if (!base || dest < base)
			return;
		uint32 off = (uint32)(dest - base);
		int x = (int)(off % (uint32)rowbytes);
		int y = (int)(off / (uint32)rowbytes);
		if (!flight_hits(x, y, width_bytes, height))
			return;
	}
	[g_pending waitUntilCompleted];
	g_pending = nil;
	g_flight_on = false;
}

static void SheepForceTrack(id<MTLCommandBuffer> cb)
{
	g_pending = cb;
}

static void on_main(void (^block)(void))
{
	if ([NSThread isMainThread])
		block();
	else
		dispatch_sync(dispatch_get_main_queue(), block);
}

void SheepForceStartup(void *ns_view)
{
	if (g_dev)
		return;
	if (SheepForceEnabled()) {
		printf("NW-BOOT SheepForce by Bill Cavalieri\n");
		fflush(stdout);
	}
	g_dev = MTLCreateSystemDefaultDevice();
	if (!g_dev) {
		if (!g_logged_fail) {
			g_logged_fail = true;
			printf("SheepForce: no Metal device\n");
		}
		return;
	}
	g_queue = [g_dev newCommandQueue];
	if (!SheepForceLoadLibrary()) {
		g_dev = nil;
		return;
	}
	NSView *view = (__bridge NSView *)ns_view;
	if (!view)
		return;
	on_main(^{
		g_view = view;
		[view setWantsLayer:YES];
		CAMetalLayer *layer = [view.layer isKindOfClass:[CAMetalLayer class]] ?
			(CAMetalLayer *)view.layer : [CAMetalLayer layer];
		if (view.layer != layer) {
			[view setLayer:layer];
			[view setWantsLayer:YES];
		}
		g_layer = layer;
		g_layer.device = g_dev;
		g_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
		g_layer.framebufferOnly = YES;
		g_layer.opaque = YES;
		g_layer.allowsNextDrawableTimeout = YES;
		g_layer.contentsGravity = kCAGravityResize;
		g_layer.magnificationFilter = kCAFilterNearest;
		g_layer.minificationFilter = kCAFilterNearest;
		SheepForceLayoutDisplay();
	});
	g_meta = [g_dev newBufferWithLength:5 * sizeof(uint) options:MTLResourceStorageModeShared];
	g_pal = [g_dev newBufferWithLength:256 * 4 options:MTLResourceStorageModeShared];
	memset(g_pal.contents, 0, 256 * 4);
	g_dirty = true;
	printf("SheepForce: Metal scanout on the window\n");
}

void SheepForceLayoutDisplay(void)
{
	if (![NSThread isMainThread]) {
		on_main(^{ SheepForceLayoutDisplay(); });
		return;
	}
	if (!g_layer || !g_view)
		return;
	/* Backing-layer frame is NSView's. Setting it ourselves clips scanout
	 * to a strip at the top (1-bit grey shown as a checkerboard band). */
	if (g_layer != g_view.layer)
		g_layer.frame = g_view.bounds;
	/* Match the window scale so AppKit mouse points agree with the
	 * picture. drawableSize stays at guest pixels. */
	{
		NSWindow *win = g_view.window;
		g_layer.contentsScale = win ? win.backingScaleFactor : 1;
	}
	const int gw = SheepForceWidth();
	const int gh = SheepForceHeight();
	if (gw >= 1 && gh >= 1)
		g_layer.drawableSize = CGSizeMake(gw, gh);
	NSSize b = g_view.bounds.size;
	g_layout_w = (int)b.width;
	g_layout_h = (int)b.height;
}

void SheepForceShutdown(void)
{
	SheepForceFlushCPU(NULL, 0, 0, 0);
	g_view = nil;
	g_layer = nil;
	g_fb = nil;
	g_fb_host = NULL;
	g_presented = NULL;
	g_presented_bytes = 0;
	g_fb_nocopy = false;
	g_pal = nil;
	g_meta = nil;
	g_src = nil;
	g_pending = nil;
	g_flight_on = false;
	g_present_pipe = nil;
	g_tri_pipe = nil;
	g_lib = nil;
	g_queue = nil;
	g_dev = nil;
}

bool SheepForceAdoptHostFB(uint8 *host, uint32 bytes)
{
	if (!g_dev) {
		g_dev = MTLCreateSystemDefaultDevice();
		if (g_dev)
			g_queue = [g_dev newCommandQueue];
	}
	if (!g_dev || !host || bytes == 0)
		return false;
	const NSUInteger page = (NSUInteger)getpagesize();
	NSUInteger length = ((NSUInteger)bytes + page - 1) & ~(page - 1);
	if (g_fb && g_fb_host == host && g_fb.length >= length && g_fb_nocopy)
		return true;
	g_fb = nil;
	g_fb_host = host;
	g_fb_nocopy = false;
	/* Guest PA must stay in the NATMEM window. Wrap those pages as the
	 * Metal buffer so scanout has no second copy. Do not remap: OVERWRITE
	 * of that window hung boot at the grey screen (illegal at 00017840). */
	if (((uintptr_t)host & (page - 1)) == 0) {
		g_fb = [g_dev newBufferWithBytesNoCopy:host length:length
			options:MTLResourceStorageModeShared deallocator:nil];
		if (g_fb) {
			g_fb_nocopy = true;
			printf("SheepForce: guest FB wrapped as Metal buffer %p %u\n",
			       host, (unsigned)length);
			fflush(stdout);
			return true;
		}
	}
	g_fb = [g_dev newBufferWithLength:length options:MTLResourceStorageModeShared];
	if (!g_logged_copy) {
		g_logged_copy = true;
		printf("SheepForce: framebuffer is not a shared Metal buffer; CPU copy scanout\n");
	}
	return g_fb != nil;
}

static bool SheepForceBindFB(void)
{
	uint8 *host = SheepForcePageHost(0);
	uint32 bytes = SheepForcePageBytes() * (uint32)SheepForcePageCount();
	return SheepForceAdoptHostFB(host, bytes);
}

void SheepForceSync(void)
{
	SheepForceFlushCPU(NULL, 0, 0, 0);
}

void SheepForceLoadPalette(void)
{
	if (!g_pal)
		return;
	uint8 *pal = (uint8 *)g_pal.contents;
	for (int i = 0; i < 256; i++) {
		pal[i * 4 + 0] = mac_gamma[mac_pal[i].red].red;
		pal[i * 4 + 1] = mac_gamma[mac_pal[i].green].green;
		pal[i * 4 + 2] = mac_gamma[mac_pal[i].blue].blue;
		pal[i * 4 + 3] = 255;
	}
	g_dirty = true;
}

static void SheepForceDispatch(id<MTLComputePipelineState> pipe, const void *uni, size_t uni_len, uint w, uint h)
{
	if (!pipe || w == 0 || h == 0 || !SheepForceBindFB())
		return;
	id<MTLCommandBuffer> cb = [g_queue commandBuffer];
	id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
	[enc setComputePipelineState:pipe];
	[enc setBuffer:g_fb offset:0 atIndex:0];
	[enc setBytes:uni length:uni_len atIndex:1];
	NSUInteger tw = pipe.threadExecutionWidth > 0 ? pipe.threadExecutionWidth : 16;
	[enc dispatchThreads:MTLSizeMake(w, h, 1) threadsPerThreadgroup:MTLSizeMake(tw, 1, 1)];
	[enc endEncoding];
	[cb commit];
	SheepForceTrack(cb);
	g_dirty = true;
}

bool SheepForceTryFill(uint8 *dest, int bpp, int rowbytes, int width_bytes, int height, uint32 color)
{
	uint8 *base = SheepForcePageHost(0);
	if (!g_dev || !base || bpp < 1 || height <= 0 || width_bytes < bpp || dest < base)
		return false;
	if (!SheepForceBindFB() || !g_fb_nocopy)
		return false;
	uint32 off = (uint32)(dest - base);
	struct { uint x, y, w, h, row, bpp, color; } u;
	u.x = (uint)((off % (uint32)rowbytes) / (uint32)bpp);
	u.y = (uint)(off / (uint32)rowbytes);
	u.w = (uint)(width_bytes / bpp);
	u.h = (uint)height;
	u.row = (uint)rowbytes;
	u.bpp = (uint)bpp;
	u.color = color;
	note_flight(dest, rowbytes, width_bytes, height);
	SheepForceDispatch(g_fill_pipe, &u, sizeof u, u.w, u.h);
	return true;
}

bool SheepForceTryInvert(uint8 *dest, int bpp, int rowbytes, int width_bytes, int height)
{
	uint8 *base = SheepForcePageHost(0);
	if (!g_dev || !base || bpp < 1 || height <= 0 || dest < base)
		return false;
	if (!SheepForceBindFB() || !g_fb_nocopy)
		return false;
	uint32 off = (uint32)(dest - base);
	struct { uint x, y, w, h, row, bpp, color; } u;
	u.x = (uint)((off % (uint32)rowbytes) / (uint32)bpp);
	u.y = (uint)(off / (uint32)rowbytes);
	u.w = (uint)(width_bytes / bpp);
	u.h = (uint)height;
	u.row = (uint)rowbytes;
	u.bpp = (uint)bpp;
	u.color = 0;
	note_flight(dest, rowbytes, width_bytes, height);
	SheepForceDispatch(g_inv_pipe, &u, sizeof u, u.w, u.h);
	return true;
}

bool SheepForceTryBlit(uint8 *dest, const uint8 *src, int bpp, int dst_row, int src_row, int width_bytes, int height)
{
	uint8 *base = SheepForcePageHost(0);
	if (!g_dev || !g_blit_pipe || !base || !src || !dest || bpp < 1 || height <= 0
	    || width_bytes < bpp || dest < base || dst_row < width_bytes || src_row < width_bytes)
		return false;
	if (!SheepForceBindFB() || !g_fb_nocopy)
		return false;
	uint32 fb_bytes = SheepForcePageBytes() * (uint32)SheepForcePageCount();
	bool src_in = src >= base && (uint32)(src - base) < fb_bytes;
	if (src_in) {
		uint32 s0 = (uint32)(src - base);
		uint32 d0 = (uint32)(dest - base);
		uint32 sbytes = (uint32)src_row * (uint32)(height - 1) + (uint32)width_bytes;
		uint32 dbytes = (uint32)dst_row * (uint32)(height - 1) + (uint32)width_bytes;
		if (s0 < d0 + dbytes && d0 < s0 + sbytes)
			return false;
	}
	struct { uint w, h, dst_row, src_row, bpp; } u;
	u.w = (uint)(width_bytes / bpp);
	u.h = (uint)height;
	u.dst_row = (uint)dst_row;
	u.bpp = (uint)bpp;
	id<MTLBuffer> src_buf = g_fb;
	uint32 soff = 0;
	if (src_in) {
		soff = (uint32)(src - base);
		u.src_row = (uint)src_row;
	} else {
		size_t need = (size_t)width_bytes * (size_t)height;
		if (!g_src || g_src.length < need) {
			g_src = [g_dev newBufferWithLength:need options:MTLResourceStorageModeShared];
			if (!g_src)
				return false;
		}
		uint8 *packed = (uint8 *)g_src.contents;
		for (int y = 0; y < height; y++)
			memcpy(packed + (size_t)y * (size_t)width_bytes,
			       src + (size_t)y * (size_t)src_row, (size_t)width_bytes);
		src_buf = g_src;
		u.src_row = (uint)width_bytes;
	}
	uint32 doff = (uint32)(dest - base);
	note_flight(dest, dst_row, width_bytes, height);
	id<MTLCommandBuffer> cb = [g_queue commandBuffer];
	id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
	[enc setComputePipelineState:g_blit_pipe];
	[enc setBuffer:g_fb offset:doff atIndex:0];
	[enc setBuffer:src_buf offset:soff atIndex:1];
	[enc setBytes:&u length:sizeof u atIndex:2];
	NSUInteger tw = g_blit_pipe.threadExecutionWidth > 0 ? g_blit_pipe.threadExecutionWidth : 16;
	[enc dispatchThreads:MTLSizeMake(u.w, u.h, 1) threadsPerThreadgroup:MTLSizeMake(tw, 1, 1)];
	[enc endEncoding];
	[cb commit];
	SheepForceTrack(cb);
	g_dirty = true;
	return true;
}

bool SheepForcePresent(int x, int y, int w, int h)
{
	(void)x; (void)y; (void)w; (void)h;
	if (!g_layer || !g_present_pipe || SheepForceWidth() <= 0)
		return false;
	if (!g_view.window || g_view.window.miniaturized)
		return false;
	if (g_view.bounds.size.width < 1 || g_view.bounds.size.height < 1) {
		[g_view.window layoutIfNeeded];
		SheepForceLayoutDisplay();
		if (g_view.bounds.size.width < 1 || g_view.bounds.size.height < 1)
			return false;
	}
	if (!SheepForceBindFB() || !g_fb || !g_meta)
		return false;
	g_presented = NULL;
	g_presented_bytes = 0;
	const int gw = SheepForceWidth();
	const int gh = SheepForceHeight();
	const int row = SheepForceRowBytes();
	const int depth = SheepForceDepth();
	const int page = SheepForceVisiblePage();
	NSRect bounds = g_view.bounds;
	if ((int)bounds.size.width != g_layout_w || (int)bounds.size.height != g_layout_h
	    || fabs(g_layer.drawableSize.width - gw) > 0.5
	    || fabs(g_layer.drawableSize.height - gh) > 0.5)
		SheepForceLayoutDisplay();
	if (g_layer.drawableSize.width < 1 || g_layer.drawableSize.height < 1)
		SheepForceLayoutDisplay();
	if (g_layer.drawableSize.width < 1 || g_layer.drawableSize.height < 1)
		return false;
	if (!g_fb_nocopy && g_fb_host) {
		SheepForceFlushCPU(NULL, 0, 0, 0);
		uint32 bytes = SheepForcePageBytes() * (uint32)SheepForcePageCount();
		if (bytes > g_fb.length)
			bytes = (uint32)g_fb.length;
		memcpy(g_fb.contents, g_fb_host, bytes);
	}
	{
		const uint32 pb = SheepForcePageBytes();
		const uint32 off = (uint32)page * pb;
		const uint8 *base = (const uint8 *)g_fb.contents;
		if (base && pb >= 64 && off + pb <= (uint32)g_fb.length) {
			g_presented = base + off;
			g_presented_bytes = pb;
		}
	}
	id<CAMetalDrawable> drawable = [g_layer nextDrawable];
	if (!drawable)
		return false;
	uint *meta = (uint *)g_meta.contents;
	meta[0] = (uint)gw;
	meta[1] = (uint)gh;
	meta[2] = (uint)row;
	meta[3] = (uint)depth;
	meta[4] = (uint)page * SheepForcePageBytes();
	g_meta_depth = meta[3];
	MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
	rp.colorAttachments[0].texture = drawable.texture;
	rp.colorAttachments[0].loadAction = MTLLoadActionClear;
	rp.colorAttachments[0].storeAction = MTLStoreActionStore;
	rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
	id<MTLCommandBuffer> cb = [g_queue commandBuffer];
	id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
	[enc setRenderPipelineState:g_present_pipe];
	[enc setFragmentBuffer:g_pal offset:0 atIndex:0];
	[enc setFragmentBuffer:g_fb offset:0 atIndex:2];
	[enc setFragmentBuffer:g_meta offset:0 atIndex:3];
	[enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
	[enc endEncoding];
	[cb presentDrawable:drawable];
	[cb commit];
	g_dirty = false;
	return true;
}

uint32 SheepForcePresentedHash(int *have)
{
	if (have)
		*have = 0;
	if (!g_presented || g_presented_bytes < 64)
		return 0;
	if (have)
		*have = 1;
	/* Every 16 bytes of the page Metal just showed, then the palette.
	 * Sixteen rows of the guest mapping stayed constant for a whole
	 * movie that was on screen; an 8-bit movie can also move by the
	 * color table alone. */
	uint32 hash = 2166136261u;
	const uint8 *p = g_presented;
	for (uint32 i = 0; i < g_presented_bytes; i += 16) {
		hash ^= p[i];
		hash *= 16777619u;
	}
	if (g_pal && g_pal.length >= 1024) {
		const uint8 *pal = (const uint8 *)g_pal.contents;
		for (int i = 0; i < 1024; i += 4) {
			hash ^= pal[i];
			hash *= 16777619u;
		}
	}
	return hash;
}

static void SheepForceMac32ToBGRA(uint8 *dst, const uint8 *src, int width, int height, int src_row, int dst_row)
{
	for (int y = 0; y < height; y++) {
		const uint8 *s = src + y * src_row;
		uint8 *d = dst + y * dst_row;
		for (int x = 0; x < width; x++) {
			d[0] = s[3];
			d[1] = s[2];
			d[2] = s[1];
			d[3] = 255;
			s += 4;
			d += 4;
		}
	}
}

static void SheepForceBGRAToMac32(uint8 *dst, const uint8 *src, int width, int height, int dst_row, int src_row)
{
	for (int y = 0; y < height; y++) {
		uint8 *d = dst + y * dst_row;
		const uint8 *s = src + y * src_row;
		for (int x = 0; x < width; x++) {
			d[0] = 0;
			d[1] = s[2];
			d[2] = s[1];
			d[3] = s[0];
			s += 4;
			d += 4;
		}
	}
}

int SheepForceRaveTriangle(uint8 *pixmap, int width, int height, int rowbytes, int depth_bits,
			   float x0, float y0, float x1, float y1, float x2, float y2,
			   uint8_t r, uint8_t g, uint8_t b)
{
	if (!g_dev || !g_tri_pipe || !pixmap || width <= 0 || height <= 0 || depth_bits != 32)
		return -1;
	if (rowbytes < width * 4)
		return -1;
	SheepForceFlushCPU(NULL, 0, 0, 0);
	const int bgra_row = width * 4;
	uint8 *bgra = (uint8 *)malloc((size_t)bgra_row * (size_t)height);
	if (!bgra)
		return -1;
	SheepForceMac32ToBGRA(bgra, pixmap, width, height, rowbytes, bgra_row);
	MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:width height:height mipmapped:NO];
	td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
	id<MTLTexture> tex = [g_dev newTextureWithDescriptor:td];
	[tex replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0 withBytes:bgra bytesPerRow:bgra_row];
	float xy[6] = { x0 / width, y0 / height, x1 / width, y1 / height, x2 / width, y2 / height };
	float color[4] = { r / 255.f, g / 255.f, b / 255.f, 1.f };
	id<MTLBuffer> xb = [g_dev newBufferWithBytes:xy length:sizeof xy options:MTLResourceStorageModeShared];
	id<MTLBuffer> cbuff = [g_dev newBufferWithBytes:color length:sizeof color options:MTLResourceStorageModeShared];
	MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
	rp.colorAttachments[0].texture = tex;
	rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
	rp.colorAttachments[0].storeAction = MTLStoreActionStore;
	id<MTLCommandBuffer> cb = [g_queue commandBuffer];
	id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
	[enc setRenderPipelineState:g_tri_pipe];
	[enc setVertexBuffer:xb offset:0 atIndex:0];
	[enc setVertexBuffer:cbuff offset:0 atIndex:1];
	[enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
	[enc endEncoding];
	[cb commit];
	[cb waitUntilCompleted];
	[tex getBytes:bgra bytesPerRow:bgra_row fromRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0];
	SheepForceBGRAToMac32(pixmap, bgra, width, height, rowbytes, bgra_row);
	free(bgra);
	g_dirty = true;
	return 0;
}

void SheepForceRaveSync(void)
{
	SheepForceSync();
}

int32 SheepForceRaveDispatch(uint32 mac_params)
{
	if (!mac_params)
		return -1;
	SheepForceRaveCall c;
	c.pixmap = ReadMacInt32(mac_params + 0);
	c.width = (int16)ReadMacInt16(mac_params + 4);
	c.height = (int16)ReadMacInt16(mac_params + 6);
	c.rowbytes = (int16)ReadMacInt16(mac_params + 8);
	c.depth = (int16)ReadMacInt16(mac_params + 10);
	c.selector = (int16)ReadMacInt16(mac_params + 12);
	if (c.selector == 2) {
		SheepForceRaveSync();
		return 0;
	}
	if (c.selector != 1 || c.pixmap == 0)
		return -1;
	uint8 *pix = Mac2HostAddr(c.pixmap);
	float xyv[6];
	memcpy(xyv, Mac2HostAddr(mac_params + 14), sizeof xyv);
	float x0 = xyv[0], y0 = xyv[1], x1 = xyv[2], y1 = xyv[3], x2 = xyv[4], y2 = xyv[5];
	uint8 r = ReadMacInt8(mac_params + 38);
	uint8 g = ReadMacInt8(mac_params + 39);
	uint8 b = ReadMacInt8(mac_params + 40);
	return SheepForceRaveTriangle(pix, c.width, c.height, c.rowbytes, c.depth, x0, y0, x1, y1, x2, y2, r, g, b);
}
