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
#include <vector>
#include <string.h>

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
static id<MTLTexture> g_scan;
static id<MTLBuffer> g_pal;
static id<MTLBuffer> g_fb;
static bool g_logged_fail;
static int g_scan_w, g_scan_h;
static uint g_scan_depth;

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
	"fragment float4 sf_fs(VOut in [[stage_in]], texture2d<float> tex [[texture(0)]],\n"
	"    constant uchar4 *pal [[buffer(0)]], constant uint &depth [[buffer(1)]]) {\n"
	"  constexpr sampler smp(filter::nearest, address::clamp_to_edge);\n"
	"  float4 c = tex.sample(smp, in.uv);\n"
	"  if (depth == 8u) {\n"
	"    uint i = min(uint(c.x * 255.0 + 0.5), 255u);\n"
	"    uchar4 p = pal[i];\n"
	"    return float4(float(p.z) / 255.0, float(p.y) / 255.0, float(p.x) / 255.0, 1.0);\n"
	"  }\n"
	"  /* 32-bit guest bytes are 00,R,G,B. RGBA8 reads that as (0, R, G, B). */\n"
	"  return float4(c.g, c.b, c.a, 1.0);\n"
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

static bool SheepForceLoadLibrary(void)
{
	NSError *err = nil;
	g_lib = [g_dev newLibraryWithSource:SheepForceShaderSource() options:nil error:&err];
	if (!g_lib) {
		if (!g_logged_fail) {
			g_logged_fail = true;
			printf("SheepForce: Metal library failed\n");
		}
		return false;
	}
	MTLRenderPipelineDescriptor *pd = [[MTLRenderPipelineDescriptor alloc] init];
	pd.vertexFunction = [g_lib newFunctionWithName:@"sf_vs"];
	pd.fragmentFunction = [g_lib newFunctionWithName:@"sf_fs"];
	pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
	g_present_pipe = [g_dev newRenderPipelineStateWithDescriptor:pd error:&err];
	g_fill_pipe = [g_dev newComputePipelineStateWithFunction:[g_lib newFunctionWithName:@"sf_fill"] error:&err];
	g_inv_pipe = [g_dev newComputePipelineStateWithFunction:[g_lib newFunctionWithName:@"sf_inv"] error:&err];
	g_blit_pipe = [g_dev newComputePipelineStateWithFunction:[g_lib newFunctionWithName:@"sf_blit"] error:&err];
	MTLRenderPipelineDescriptor *td = [[MTLRenderPipelineDescriptor alloc] init];
	td.vertexFunction = [g_lib newFunctionWithName:@"sf_tri_vs"];
	td.fragmentFunction = [g_lib newFunctionWithName:@"sf_tri_fs"];
	td.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
	g_tri_pipe = [g_dev newRenderPipelineStateWithDescriptor:td error:&err];
	return g_present_pipe != nil && g_fill_pipe != nil && g_tri_pipe != nil;
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
	NSView *view = (NSView *)ns_view;
	if (!view)
		return;
	g_view = view;
	g_layer = [CAMetalLayer layer];
	g_layer.device = g_dev;
	g_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
	g_layer.framebufferOnly = NO;
	g_layer.opaque = YES;
	g_layer.frame = view.bounds;
	g_layer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
	CGFloat scale = view.window.backingScaleFactor > 0 ? view.window.backingScaleFactor : 1;
	g_layer.contentsScale = scale;
	g_layer.drawableSize = CGSizeMake(view.bounds.size.width * scale, view.bounds.size.height * scale);
	[view setLayer:g_layer];
	[view setWantsLayer:YES];
	printf("SheepForce: Metal scanout on (%g x %g)\n",
	       view.bounds.size.width, view.bounds.size.height);
}

static void SheepForceLayoutLayer(void)
{
	if (!g_layer || !g_view)
		return;
	g_layer.frame = g_view.bounds;
	CGFloat scale = g_view.window.backingScaleFactor > 0 ? g_view.window.backingScaleFactor : 1;
	g_layer.contentsScale = scale;
	g_layer.drawableSize = CGSizeMake(g_view.bounds.size.width * scale,
					   g_view.bounds.size.height * scale);
}

void SheepForceShutdown(void)
{
	g_view = nil;
	g_layer = nil;
	g_scan = nil;
	g_fb = nil;
	g_pal = nil;
	g_present_pipe = nil;
	g_tri_pipe = nil;
	g_lib = nil;
	g_queue = nil;
	g_dev = nil;
}

static bool SheepForceBindFB(void)
{
	uint8 *host = SheepForcePageHost(0);
	uint32 bytes = SheepForcePageBytes() * (uint32)SheepForcePageCount();
	if (!g_dev || !host || bytes == 0)
		return false;
	if (g_fb && g_fb.contents == host && g_fb.length >= bytes)
		return true;
	g_fb = [g_dev newBufferWithBytesNoCopy:host length:bytes options:MTLResourceStorageModeShared deallocator:nil];
	return g_fb != nil;
}

void SheepForceSync(void)
{
	if (!g_queue)
		return;
	id<MTLCommandBuffer> cb = [g_queue commandBuffer];
	[cb commit];
	[cb waitUntilCompleted];
}

static void SheepForceDispatch(id<MTLComputePipelineState> pipe, const void *uni, size_t uni_len, uint w, uint h)
{
	if (!pipe || w == 0 || h == 0 || !SheepForceBindFB())
		return;
	id<MTLBuffer> ub = [g_dev newBufferWithBytes:uni length:uni_len options:MTLResourceStorageModeShared];
	id<MTLCommandBuffer> cb = [g_queue commandBuffer];
	id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
	[enc setComputePipelineState:pipe];
	[enc setBuffer:g_fb offset:0 atIndex:0];
	[enc setBuffer:ub offset:0 atIndex:1];
	NSUInteger tw = pipe.threadExecutionWidth > 0 ? pipe.threadExecutionWidth : 16;
	[enc dispatchThreads:MTLSizeMake(w, h, 1) threadsPerThreadgroup:MTLSizeMake(tw, 1, 1)];
	[enc endEncoding];
	[cb commit];
	[cb waitUntilCompleted];
}

bool SheepForceTryFill(uint8 *dest, int bpp, int rowbytes, int width_bytes, int height, uint32 color)
{
	uint8 *base = SheepForcePageHost(0);
	if (!g_dev || !base || bpp < 1 || height <= 0 || width_bytes < bpp || dest < base)
		return false;
	if (!SheepForceBindFB())
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
	SheepForceDispatch(g_fill_pipe, &u, sizeof u, u.w, u.h);
	return true;
}

bool SheepForceTryInvert(uint8 *dest, int bpp, int rowbytes, int width_bytes, int height)
{
	uint8 *base = SheepForcePageHost(0);
	if (!g_dev || !base || bpp < 1 || height <= 0 || dest < base)
		return false;
	if (!SheepForceBindFB())
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
	SheepForceDispatch(g_inv_pipe, &u, sizeof u, u.w, u.h);
	return true;
}

bool SheepForceTryBlit(uint8 *dest, const uint8 *src, int bpp, int dst_row, int src_row, int width_bytes, int height)
{
	uint8 *base = SheepForcePageHost(0);
	if (!g_dev || !base || !src || bpp < 1 || height <= 0 || dest < base || src < base)
		return false;
	if (!SheepForceBindFB())
		return false;
	struct { uint w, h, dst_row, src_row, bpp; } u;
	u.w = (uint)(width_bytes / bpp);
	u.h = (uint)height;
	u.dst_row = (uint)dst_row;
	u.src_row = (uint)src_row;
	u.bpp = (uint)bpp;
	uint32 doff = (uint32)(dest - base);
	uint32 soff = (uint32)(src - base);
	id<MTLBuffer> ub = [g_dev newBufferWithBytes:&u length:sizeof u options:MTLResourceStorageModeShared];
	id<MTLCommandBuffer> cb = [g_queue commandBuffer];
	id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
	[enc setComputePipelineState:g_blit_pipe];
	[enc setBuffer:g_fb offset:doff atIndex:0];
	[enc setBuffer:g_fb offset:soff atIndex:1];
	[enc setBuffer:ub offset:0 atIndex:2];
	NSUInteger tw = g_blit_pipe.threadExecutionWidth > 0 ? g_blit_pipe.threadExecutionWidth : 16;
	[enc dispatchThreads:MTLSizeMake(u.w, u.h, 1) threadsPerThreadgroup:MTLSizeMake(tw, 1, 1)];
	[enc endEncoding];
	[cb commit];
	[cb waitUntilCompleted];
	return true;
}

static void SheepForceUploadPage(void)
{
	const int w = SheepForceWidth();
	const int h = SheepForceHeight();
	const int row = SheepForceRowBytes();
	const int depth = SheepForceDepth();
	uint8 *page = SheepForcePageHost(SheepForceVisiblePage());
	if (!page || w <= 0 || h <= 0 || row <= 0)
		return;
	MTLPixelFormat fmt = (depth == 8) ? MTLPixelFormatR8Unorm : MTLPixelFormatRGBA8Unorm;
	if (!g_scan || g_scan_w != w || g_scan_h != h || g_scan_depth != (uint)depth) {
		MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt width:w height:h mipmapped:NO];
		td.usage = MTLTextureUsageShaderRead;
		g_scan = [g_dev newTextureWithDescriptor:td];
		g_scan_w = w;
		g_scan_h = h;
		g_scan_depth = (uint)depth;
	}
	if (depth == 8) {
		uint8 pal[256 * 4];
		for (int i = 0; i < 256; i++) {
			pal[i * 4 + 0] = mac_gamma[mac_pal[i].red].red;
			pal[i * 4 + 1] = mac_gamma[mac_pal[i].green].green;
			pal[i * 4 + 2] = mac_gamma[mac_pal[i].blue].blue;
			pal[i * 4 + 3] = 255;
		}
		if (!g_pal)
			g_pal = [g_dev newBufferWithLength:sizeof pal options:MTLResourceStorageModeShared];
		memcpy(g_pal.contents, pal, sizeof pal);
		[g_scan replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0 withBytes:page bytesPerRow:row];
	} else if (depth == 32) {
		[g_scan replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0 withBytes:page bytesPerRow:row];
	} else {
		std::vector<uint8> tmp((size_t)w * (size_t)h * 4);
		for (int y = 0; y < h; y++) {
			const uint8 *s = page + y * row;
			uint8 *d = &tmp[(size_t)y * (size_t)w * 4];
			for (int x = 0; x < w; x++) {
				uint16 v = (uint16)((s[0] << 8) | s[1]);
				d[0] = 0;
				d[1] = (uint8)(((v >> 10) & 31) * 255 / 31);
				d[2] = (uint8)(((v >> 5) & 31) * 255 / 31);
				d[3] = (uint8)((v & 31) * 255 / 31);
				s += 2;
				d += 4;
			}
		}
		[g_scan replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0 withBytes:tmp.data() bytesPerRow:w * 4];
	}
}

bool SheepForcePresent(int x, int y, int w, int h)
{
	(void)x; (void)y; (void)w; (void)h;
	if (!g_layer || !g_present_pipe || SheepForceWidth() <= 0)
		return false;
	SheepForceSync();
	SheepForceUploadPage();
	if (!g_scan)
		return false;
	static int g_fb_logged;
	if (!g_fb_logged) {
		g_fb_logged = 1;
		uint8 *page = SheepForcePageHost(SheepForceVisiblePage());
		uint32 nonzero = 0;
		uint32 n = SheepForceRowBytes() * SheepForceHeight();
		if (page && n > 16) {
			for (uint32 i = 0; i < n; i += 64)
				nonzero += page[i] | page[i + 1] | page[i + 2] | page[i + 3];
		}
		printf("SheepForce: framebuffer %s (%u x %u)\n",
		       nonzero ? "has pixels" : "is empty",
		       (unsigned)SheepForceWidth(), (unsigned)SheepForceHeight());
		fflush(stdout);
	}
	SheepForceLayoutLayer();
	if (g_layer.drawableSize.width < 1 || g_layer.drawableSize.height < 1)
		return false;
	id<CAMetalDrawable> drawable = [g_layer nextDrawable];
	if (!drawable)
		return false;
	uint depth = (SheepForceDepth() == 8) ? 8u : 32u;
	id<MTLBuffer> db = [g_dev newBufferWithBytes:&depth length:sizeof depth options:MTLResourceStorageModeShared];
	if (!g_pal) {
		g_pal = [g_dev newBufferWithLength:256 * 4 options:MTLResourceStorageModeShared];
		memset(g_pal.contents, 0, 256 * 4);
	}
	MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
	rp.colorAttachments[0].texture = drawable.texture;
	rp.colorAttachments[0].loadAction = MTLLoadActionClear;
	rp.colorAttachments[0].storeAction = MTLStoreActionStore;
	rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
	id<MTLCommandBuffer> cb = [g_queue commandBuffer];
	id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
	[enc setRenderPipelineState:g_present_pipe];
	[enc setFragmentTexture:g_scan atIndex:0];
	[enc setFragmentBuffer:g_pal offset:0 atIndex:0];
	[enc setFragmentBuffer:db offset:0 atIndex:1];
	[enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
	[enc endEncoding];
	[cb presentDrawable:drawable];
	[cb commit];
	return true;
}

int SheepForceRaveTriangle(uint8 *pixmap, int width, int height, int rowbytes, int depth_bits,
			   float x0, float y0, float x1, float y1, float x2, float y2,
			   uint8_t r, uint8_t g, uint8_t b)
{
	if (!g_dev || !g_tri_pipe || !pixmap || width <= 0 || height <= 0 || depth_bits != 32)
		return -1;
	MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:width height:height mipmapped:NO];
	td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
	id<MTLTexture> tex = [g_dev newTextureWithDescriptor:td];
	[tex replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0 withBytes:pixmap bytesPerRow:rowbytes];
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
	[tex getBytes:pixmap bytesPerRow:rowbytes fromRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0];
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
