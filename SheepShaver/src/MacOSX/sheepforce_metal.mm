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
#include "nw_log.h"
#include "sheepforce.h"
#include "video.h"
#include "cpu_emulation.h"
#include "nqd_blit_ops.h"
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
static id<MTLComputePipelineState> g_fillspans_pipe;
static id<MTLComputePipelineState> g_filltile_pipe;
static id<MTLComputePipelineState> g_inv_pipe;
static id<MTLComputePipelineState> g_blit_pipe;
static id<MTLComputePipelineState> g_gather_pipe;
static CAMetalLayer *g_layer;
static NSView *g_view;
static id<MTLBuffer> g_pal;
static id<MTLBuffer> g_fb;
static id<MTLBuffer> g_meta;
static id<MTLCommandBuffer> g_pending;
/* Tiny QuickDraw operations are batched into one open command buffer and one compute encoder.
 * It is committed when the CPU needs the result, before a present, or when it gets long. */
static id<MTLCommandBuffer> g_open_cb;
static id<MTLComputeCommandEncoder> g_open_enc;
static unsigned g_open_count;
static const unsigned kOpenBatchMax = 128;
static id<MTLBuffer> g_src;
static id<MTLBuffer> g_scr;	/* GPU-only copy of an overlapping blit's source */
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
	"float2 q[3] = { float2(-1.0,-1.0), float2(3.0,-1.0), float2(-1.0,3.0) };\n"
	"VOut o;\n"
	"o.p = float4(q[id], 0.0, 1.0);\n"
	"o.uv = float2(q[id].x * 0.5 + 0.5, 1.0 - (q[id].y * 0.5 + 0.5));\n"
	"return o;\n"
	"}\n"
	"static float4 pal_rgb(constant uchar4 *pal, uint i) {\n"
	"uchar4 p = pal[i];\n"
	"return float4(float(p.z) / 255.0, float(p.y) / 255.0, float(p.x) / 255.0, 1.0);\n"
	"}\n"
	"fragment float4 sf_fs(VOut in [[stage_in]], constant uchar4 *pal [[buffer(0)]],\n"
	"device const uchar *pix [[buffer(2)]], constant uint *meta [[buffer(3)]]) {\n"
	"uint w = meta[0], h = meta[1], row = meta[2], depth = meta[3], off = meta[4];\n"
	"uint x = min(uint(in.uv.x * float(w)), w - 1u);\n"
	"uint y = min(uint(in.uv.y * float(h)), h - 1u);\n"
	"if (depth == 1u) {\n"
	"uint byte = pix[off + y * row + (x >> 3)];\n"
	"uint bit = 0x80u >> (x & 7u);\n"
	"return pal_rgb(pal, (byte & bit) ? 1u : 0u);\n"
	"}\n"
	"if (depth == 2u) {\n"
	"uint byte = pix[off + y * row + (x >> 2)];\n"
	"uint shift = (3u - (x & 3u)) * 2u;\n"
	"return pal_rgb(pal, (byte >> shift) & 3u);\n"
	"}\n"
	"if (depth == 4u) {\n"
	"uint byte = pix[off + y * row + (x >> 1)];\n"
	"uint i = ((x & 1u) == 0u) ? (byte >> 4) : (byte & 15u);\n"
	"return pal_rgb(pal, i);\n"
	"}\n"
	"if (depth == 8u) {\n"
	"return pal_rgb(pal, pix[off + y * row + x]);\n"
	"}\n"
	"if (depth == 16u) {\n"
	"uint a = off + y * row + x * 2u;\n"
	"uint v = (uint(pix[a]) << 8) | uint(pix[a + 1]);\n"
	"return float4(float((v >> 10) & 31u) / 31.0, float((v >> 5) & 31u) / 31.0, float(v & 31u) / 31.0, 1.0);\n"
	"}\n"
	"uint a = off + y * row + x * 4u;\n"
	"return float4(float(pix[a + 1]) / 255.0, float(pix[a + 2]) / 255.0, float(pix[a + 3]) / 255.0, 1.0);\n"
	"}\n"
	"struct FillU { uint x, y, w, h, row, bpp, color; };\n"
	"kernel void sf_fill(device uchar *pix [[buffer(0)]], constant FillU &u [[buffer(1)]],\n"
	"uint2 gid [[thread_position_in_grid]]) {\n"
	"if (gid.x >= u.w || gid.y >= u.h) return;\n"
	"device uchar *d = pix + (u.y + gid.y) * u.row + (u.x + gid.x) * u.bpp;\n"
	"for (uint i = 0; i < u.bpp; i++) d[i] = (uchar)((u.color >> (8u * i)) & 0xffu);\n"
	"}\n"
	"struct SpanU { uint x, y, w, h, row, fore, back, pat0, pat1; int ox, oy; };\n"
	"kernel void sf_fillspans(device uchar *pix [[buffer(0)]], constant SpanU &u [[buffer(1)]],\n"
	"device const int *row_start [[buffer(2)]], device const int *runs [[buffer(3)]],\n"
	"uint2 gid [[thread_position_in_grid]]) {\n"
	"if (gid.x >= u.w || gid.y >= u.h) return;\n"
	"int a = row_start[gid.y], b = row_start[gid.y + 1];\n"
	"bool in = false;\n"
	"for (int i = a; i < b; i++)\n"
	"if (int(gid.x) >= runs[2 * i] && int(gid.x) < runs[2 * i + 1]) { in = true; break; }\n"
	"if (!in) return;\n"
	"uint tx = uint(int(gid.x) + u.ox) & 7u, ty = uint(int(gid.y) + u.oy) & 7u;\n"
	"uint byte = ty < 4u ? ((u.pat0 >> (8u * (3u - ty))) & 0xffu) : ((u.pat1 >> (8u * (7u - ty))) & 0xffu);\n"
	"uint c = ((byte >> (7u - tx)) & 1u) ? u.fore : u.back;\n"
	"device uchar *d = pix + (u.y + gid.y) * u.row + (u.x + gid.x) * 4u;\n"
	"for (uint i = 0; i < 4u; i++) d[i] = (uchar)((c >> (8u * i)) & 0xffu);\n"
	"}\n"
	"struct TileU { uint x, y, w, h, row, tw, th, ox, oy; };\n"
	"kernel void sf_filltile(device uchar *pix [[buffer(0)]], constant TileU &u [[buffer(1)]],\n"
	"device const int *row_start [[buffer(2)]], device const int *runs [[buffer(3)]],\n"
	"device const uint *tile [[buffer(4)]], uint2 gid [[thread_position_in_grid]]) {\n"
	"if (gid.x >= u.w || gid.y >= u.h) return;\n"
	"int a = row_start[gid.y], b = row_start[gid.y + 1];\n"
	"bool in = false;\n"
	"for (int i = a; i < b; i++)\n"
	"if (int(gid.x) >= runs[2 * i] && int(gid.x) < runs[2 * i + 1]) { in = true; break; }\n"
	"if (!in) return;\n"
	"uint c = tile[((gid.y + u.oy) % u.th) * u.tw + ((gid.x + u.ox) % u.tw)];\n"
	"device uchar *d = pix + (u.y + gid.y) * u.row + (u.x + gid.x) * 4u;\n"
	"for (uint i = 0; i < 4u; i++) d[i] = (uchar)((c >> (8u * i)) & 0xffu);\n"
	"}\n"
	"kernel void sf_inv(device uchar *pix [[buffer(0)]], constant FillU &u [[buffer(1)]],\n"
	"uint2 gid [[thread_position_in_grid]]) {\n"
	"if (gid.x >= u.w || gid.y >= u.h) return;\n"
	"device uchar *d = pix + (u.y + gid.y) * u.row + (u.x + gid.x) * u.bpp;\n"
	"for (uint i = 0; i < u.bpp; i++) d[i] = ~d[i];\n"
	"}\n"
	"struct BlitU { uint w, h, dst_row, src_row, sbpp, dbpp, dst_off, src_off, mode, back; };\n"
	"static uint qd_fetch(device const uchar *s, uint sbpp, constant uint *pal) {\n"
	"if (sbpp == 1u) return pal[s[0]];\n"
	"if (sbpp == 2u) {\n"
	"uint v = (uint(s[0]) << 8) | uint(s[1]);\n"
	"uint r = ((v >> 10) & 31u) * 8u, g = ((v >> 5) & 31u) * 8u, b = (v & 31u) * 8u;\n"
	"return (r << 8) | (g << 16) | (b << 24);\n"
	"}\n"
	"return uint(s[0]) | (uint(s[1]) << 8) | (uint(s[2]) << 16) | (uint(s[3]) << 24);\n"
	"}\n"
	"static uint qd_bytes(uint a, uint b, uint op) {\n"
	"uint r = 0u;\n"
	"for (uint i = 1u; i < 4u; i++) {\n"
	"int x = int((a >> (8u * i)) & 0xffu), y = int((b >> (8u * i)) & 0xffu), v;\n"
	"if (op == 0u) v = (x + y) & 0xff;\n"
	"else if (op == 1u) v = (x - y) & 0xff;\n"
	"else if (op == 2u) v = max(x, y);\n"
	"else v = min(x, y);\n"
	"r |= uint(v) << (8u * i);\n"
	"}\n"
	"return r;\n"
	"}\n"
	"static uint qd_op(uint mode, uint s, uint d) {\n"
	"switch (mode) {\n"
	"case 0u: return s;\n"
	"case 1u: return d | s;\n"
	"case 2u: return d ^ s;\n"
	"case 3u: return d & ~s;\n"
	"case 4u: return ~s;\n"
	"case 5u: return d | ~s;\n"
	"case 6u: return d ^ ~s;\n"
	"case 7u: return d & s;\n"
	"case 34u: return qd_bytes(d, s, 0u);\n"
	"case 37u: return qd_bytes(d, s, 2u);\n"
	"case 38u: return qd_bytes(d, s, 1u);\n"
	"case 39u: return qd_bytes(d, s, 3u);\n"
	"}\n"
	"return s;\n"
	"}\n"
	"kernel void sf_blit(device uchar *dst [[buffer(0)]], device const uchar *src [[buffer(1)]],\n"
	"constant BlitU &u [[buffer(2)]], constant uint *pal [[buffer(3)]],\n"
	"uint2 gid [[thread_position_in_grid]]) {\n"
	"if (gid.x >= u.w || gid.y >= u.h) return;\n"
	"device uchar *d = dst + u.dst_off + gid.y * u.dst_row + gid.x * u.dbpp;\n"
	"device const uchar *s = src + u.src_off + gid.y * u.src_row + gid.x * u.sbpp;\n"
	"if (u.dbpp != 4u) {\n"
	"for (uint i = 0; i < u.dbpp; i++) d[i] = s[i];\n"
	"return;\n"
	"}\n"
	"uint sp = qd_fetch(s, u.sbpp, pal);\n"
	"uint dp = uint(d[0]) | (uint(d[1]) << 8) | (uint(d[2]) << 16) | (uint(d[3]) << 24);\n"
	"if (u.mode == 36u) {\n"
	"if (((sp ^ u.back) & 0xffffff00u) == 0u) return;\n"
	"}\n"
	"uint r = qd_op(u.mode, sp, dp);\n"
	"d[0] = uchar(r & 0xffu);\n"
	"d[1] = uchar((r >> 8) & 0xffu);\n"
	"d[2] = uchar((r >> 16) & 0xffu);\n"
	"d[3] = uchar((r >> 24) & 0xffu);\n"
	"}\n"
	"struct GatherU { uint wbytes, h, src_row, src_off; };\n"
	"kernel void sf_gather(device const uchar *src [[buffer(0)]], device uchar *scr [[buffer(1)]],\n"
	"constant GatherU &u [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) {\n"
	"if (gid.x >= u.wbytes || gid.y >= u.h) return;\n"
	"scr[gid.y * u.wbytes + gid.x] = src[u.src_off + gid.y * u.src_row + gid.x];\n"
	"}\n"
	"struct RaveV { float x, y, z, invW, r, g, b, a, uow, vow, kdr, kdg, kdb, ksr, ksg, ksb, uow2, vow2, invW2, pad; };\n"
	"struct RaveU { float w, h; };\n"
	"struct RaveOut { float4 p [[position]]; float4 c; float3 uvw; float3 kd; float3 ks; float3 uvw2; };\n"
	"vertex RaveOut sf_rave_vs(uint id [[vertex_id]], constant RaveV *v [[buffer(0)]], constant RaveU &u [[buffer(1)]]) {\n"
	"RaveV a = v[id];\n"
	"RaveOut o;\n"
	"o.p = float4(a.x / u.w * 2.0 - 1.0, 1.0 - a.y / u.h * 2.0, a.z, 1.0);\n"
	"o.c = float4(a.r, a.g, a.b, a.a);\n"
	"o.uvw = float3(a.uow, a.vow, a.invW);\n"
	"o.kd = float3(a.kdr, a.kdg, a.kdb);\n"
	"o.ks = float3(a.ksr, a.ksg, a.ksb);\n"
	"o.uvw2 = float3(a.uow2, a.vow2, a.invW2);\n"
	"return o;\n"
	"}\n"
	"struct RaveF { float4 fog; float fogStart, fogEnd, fogDensity, fogMax; uint fogMode, texOp, textured, mtOp; float mtFactor; uint mtOn, pad1, pad2; };\n"
	"fragment float4 sf_rave_fs(RaveOut in [[stage_in]], constant RaveF &f [[buffer(0)]],\n"
	"texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]],\n"
	"texture2d<float> tex2 [[texture(1)]], sampler smp2 [[sampler(1)]]) {\n"
	"float4 c = in.c;\n"
	"if (f.textured != 0u) {\n"
	"float2 uv = in.uvw.xy / in.uvw.z;\n"
	"float4 t = tex.sample(smp, uv);\n"
	"if (f.mtOn != 0u) {\n"
	"float4 t2 = tex2.sample(smp2, in.uvw2.xy / in.uvw2.z);\n"
	"if (f.mtOp == 0u) t = float4(min(t.rgb + t2.rgb, float3(1.0)), t.a);\n"
	"else if (f.mtOp == 1u) t = t * t2;\n"
	"else if (f.mtOp == 2u) t = mix(t, t2, t2.a);\n"
	"else t = mix(t, t2, f.mtFactor);\n"
	"}\n"
	"if ((f.texOp & 4u) != 0u) {\n"
	"t.rgb = t.a * t.rgb + (1.0 - t.a) * c.rgb;\n"
	"t.a = c.a;\n"
	"} else {\n"
	"t.a = t.a * c.a;\n"
	"}\n"
	"if ((f.texOp & 1u) != 0u) t.rgb = min(t.rgb * in.kd, float3(1.0));\n"
	"if ((f.texOp & 2u) != 0u) t.rgb = min(t.rgb + in.ks, float3(1.0));\n"
	"c = t;\n"
	"}\n"
	"if (f.fogMode >= 2u && in.uvw.z > 0.0) {\n"
	"float w = 1.0 / in.uvw.z;\n"
	"if (f.fogMax > 0.0) w = min(w, f.fogMax);\n"
	"float k;\n"
	"if (f.fogMode == 2u) k = (f.fogEnd - w) / (f.fogEnd - f.fogStart);\n"
	"else if (f.fogMode == 3u) k = exp(-f.fogDensity * w);\n"
	"else k = exp(-(f.fogDensity * w) * (f.fogDensity * w));\n"
	"k = clamp(k, 0.0, 1.0);\n"
	"c.rgb = mix(f.fog.rgb, c.rgb, k);\n"
	"}\n"
	"return c;\n"
	"}\n";
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
	id<MTLFunction> fillspans = [g_lib newFunctionWithName:@"sf_fillspans"];
	id<MTLFunction> filltile = [g_lib newFunctionWithName:@"sf_filltile"];
	id<MTLFunction> blit = [g_lib newFunctionWithName:@"sf_blit"];
	id<MTLFunction> gather = [g_lib newFunctionWithName:@"sf_gather"];
	if (!vs || !fs || !fill || !inv || !blit || !gather || !fillspans || !filltile)
		return false;
	MTLRenderPipelineDescriptor *pd = [[MTLRenderPipelineDescriptor alloc] init];
	pd.vertexFunction = vs;
	pd.fragmentFunction = fs;
	pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
	g_present_pipe = [g_dev newRenderPipelineStateWithDescriptor:pd error:&err];
	g_fill_pipe = [g_dev newComputePipelineStateWithFunction:fill error:&err];
	g_inv_pipe = [g_dev newComputePipelineStateWithFunction:inv error:&err];
	g_fillspans_pipe = [g_dev newComputePipelineStateWithFunction:fillspans error:&err];
	g_filltile_pipe = [g_dev newComputePipelineStateWithFunction:filltile error:&err];
	g_blit_pipe = [g_dev newComputePipelineStateWithFunction:blit error:&err];
	g_gather_pipe = [g_dev newComputePipelineStateWithFunction:gather error:&err];
	/* A missing accelerator pipeline only disables its hook; present and tri gate startup. */
	if (!g_fill_pipe || !g_inv_pipe || !g_blit_pipe || !g_gather_pipe || !g_fillspans_pipe || !g_filltile_pipe)
		printf("SheepForce: QuickDraw pipeline missing (fill %d inv %d blit %d gather %d fillspans %d filltile %d): %s\n",
		       g_fill_pipe != nil, g_inv_pipe != nil, g_blit_pipe != nil, g_gather_pipe != nil, g_fillspans_pipe != nil, g_filltile_pipe != nil,
		       err ? err.localizedDescription.UTF8String : "");
	return g_present_pipe != nil && g_fill_pipe != nil;
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

/* Every GPU write since the last wait stays in flight until a CPU access waits
 * on g_pending, so the tracked rectangle is the union of all of them, not just
 * the latest. A write that cannot be located marks the whole surface in flight. */
static void note_flight(uint8 *dest, int rowbytes, int width_bytes, int height)
{
	uint8 *base = SheepForcePageHost(0);
	int x = 0, y = 0, w = 1 << 28, h = 1 << 28;
	if (base && dest && dest >= base && rowbytes >= 1 && width_bytes >= 1 && height >= 1) {
		uint32 off = (uint32)(dest - base);
		x = (int)(off % (uint32)rowbytes);
		y = (int)(off / (uint32)rowbytes);
		w = width_bytes;
		h = height;
	}
	if (g_flight_on) {
		int x1 = g_flight_x + g_flight_w, y1 = g_flight_y + g_flight_h;
		if (x < g_flight_x) g_flight_x = x;
		if (y < g_flight_y) g_flight_y = y;
		if (x + w > x1) x1 = x + w;
		if (y + h > y1) y1 = y + h;
		g_flight_w = x1 - g_flight_x;
		g_flight_h = y1 - g_flight_y;
	} else {
		g_flight_x = x;
		g_flight_y = y;
		g_flight_w = w;
		g_flight_h = h;
		g_flight_on = true;
	}
}

/* Submit the open batch (if any) to the GPU. It becomes the pending command buffer. */
static void SheepForceCommitOpen(void)
{
	if (!g_open_cb)
		return;
	[g_open_enc endEncoding];
	[g_open_cb commit];
	g_pending = g_open_cb;
	g_open_cb = nil;
	g_open_enc = nil;
	g_open_count = 0;
}

/* The encoder to append the next operation to; nil if Metal is not ready. */
static id<MTLComputeCommandEncoder> SheepForceOpenEncoder(void)
{
	if (g_open_cb && g_open_count >= kOpenBatchMax)
		SheepForceCommitOpen();
	if (!g_open_cb) {
		if (!g_queue)
			return nil;
		g_open_cb = [g_queue commandBuffer];
		g_open_enc = [g_open_cb computeCommandEncoder];
		g_open_count = 0;
	}
	g_open_count++;
	return g_open_enc;
}

void SheepForceFlushCPU(uint8 *dest, int rowbytes, int width_bytes, int height)
{
	if (!g_pending && !g_open_cb)
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
	SheepForceCommitOpen();
	[g_pending waitUntilCompleted];
	g_pending = nil;
	g_flight_on = false;
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
	g_open_cb = nil;
	g_open_enc = nil;
	g_open_count = 0;
	g_flight_on = false;
	g_present_pipe = nil;
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
			NW_DIAG("SheepForce: guest FB wrapped as Metal buffer %p %u\n",
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
	id<MTLComputeCommandEncoder> enc = SheepForceOpenEncoder();
	if (!enc)
		return;
	[enc setComputePipelineState:pipe];
	[enc setBuffer:g_fb offset:0 atIndex:0];
	[enc setBytes:uni length:uni_len atIndex:1];
	NSUInteger tw = pipe.threadExecutionWidth > 0 ? pipe.threadExecutionWidth : 16;
	[enc dispatchThreads:MTLSizeMake(w, h, 1) threadsPerThreadgroup:MTLSizeMake(tw, 1, 1)];
	g_dirty = true;
}

/* True when a rows x row-stride rectangle starting at byte offset off lies inside the framebuffer buffer. */
static bool fb_span_ok(uint32 off, int row, int wbytes, int h)
{
	if (!g_fb || row < 1 || wbytes < 1 || h < 1 || wbytes > row)
		return false;
	uint64_t end = (uint64_t)off + (uint64_t)row * (uint64_t)(h - 1) + (uint64_t)wbytes;
	return end <= (uint64_t)g_fb.length;
}

bool SheepForceTryFill(uint8 *dest, int bpp, int rowbytes, int width_bytes, int height, uint32 color)
{
	uint8 *base = SheepForcePageHost(0);
	if (!g_dev || !g_fill_pipe || !base || bpp < 1 || height <= 0 || width_bytes < bpp || dest < base
	    || rowbytes < 1)
		return false;
	if (!SheepForceBindFB() || !g_fb_nocopy)
		return false;
	uint32 off = (uint32)(dest - base);
	if (!fb_span_ok(off, rowbytes, width_bytes, height))
		return false;
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
	if (!g_dev || !g_inv_pipe || !base || bpp < 1 || height <= 0 || dest < base || rowbytes < 1)
		return false;
	if (!SheepForceBindFB() || !g_fb_nocopy)
		return false;
	uint32 off = (uint32)(dest - base);
	if (!fb_span_ok(off, rowbytes, width_bytes, height))
		return false;
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

bool SheepForceTryFillSpans(const SheepForceSpanFill *op)
{
	uint8 *base = SheepForcePageHost(0);
	if (!g_dev || !g_fillspans_pipe || !base || !op || !op->dest || op->dest < base || op->width < 1 ||
	    op->height < 1 || op->rowbytes < 4 || !op->row_start || !op->runs)
		return false;
	if (!SheepForceBindFB() || !g_fb_nocopy)
		return false;
	const uint32 off = (uint32)(op->dest - base);
	if (!fb_span_ok(off, op->rowbytes, op->width * 4, op->height))
		return false;
	if (op->row_start[0] != 0 || op->row_start[op->height] < 0)
		return false;
	const size_t nruns = (size_t)op->row_start[op->height];
	struct { uint x, y, w, h, row, fore, back, pat0, pat1; int ox, oy; } u;
	u.x = (uint)((off % (uint32)op->rowbytes) / 4u);
	u.y = (uint)(off / (uint32)op->rowbytes);
	u.w = (uint)op->width;
	u.h = (uint)op->height;
	u.row = (uint)op->rowbytes;
	u.fore = op->fore;
	u.back = op->back;
	u.pat0 = (uint)op->pat[0] << 24 | (uint)op->pat[1] << 16 | (uint)op->pat[2] << 8 | (uint)op->pat[3];
	u.pat1 = (uint)op->pat[4] << 24 | (uint)op->pat[5] << 16 | (uint)op->pat[6] << 8 | (uint)op->pat[7];
	u.ox = op->pat_ox;
	u.oy = op->pat_oy;
	if (nruns == 0)
		return true;			/* clipped away entirely: nothing to draw */
	id<MTLComputeCommandEncoder> enc = SheepForceOpenEncoder();
	if (!enc)
		return false;
	note_flight(op->dest, op->rowbytes, op->width * 4, op->height);
	[enc setComputePipelineState:g_fillspans_pipe];
	[enc setBuffer:g_fb offset:0 atIndex:0];
	[enc setBytes:&u length:sizeof u atIndex:1];
	const size_t rs_bytes = (size_t)(op->height + 1) * sizeof(int32_t), run_bytes = nruns * 2 * sizeof(int32_t);
	if (rs_bytes <= 4096)
		[enc setBytes:op->row_start length:rs_bytes atIndex:2];
	else
		[enc setBuffer:[g_dev newBufferWithBytes:op->row_start length:rs_bytes options:MTLResourceStorageModeShared] offset:0 atIndex:2];
	if (run_bytes <= 4096)
		[enc setBytes:op->runs length:run_bytes atIndex:3];
	else
		[enc setBuffer:[g_dev newBufferWithBytes:op->runs length:run_bytes options:MTLResourceStorageModeShared] offset:0 atIndex:3];
	NSUInteger tw = g_fillspans_pipe.threadExecutionWidth > 0 ? g_fillspans_pipe.threadExecutionWidth : 16;
	[enc dispatchThreads:MTLSizeMake(u.w, u.h, 1) threadsPerThreadgroup:MTLSizeMake(tw, 1, 1)];
	g_dirty = true;
	return true;
}

bool SheepForceTryFillTile(const SheepForceTileFill *op)
{
	uint8 *base = SheepForcePageHost(0);
	if (!g_dev || !g_filltile_pipe || !base || !op || !op->dest || op->dest < base || op->width < 1 || op->height < 1 ||
	    op->rowbytes < 4 || !op->row_start || !op->runs || !op->tile || op->tile_w < 1 || op->tile_h < 1 ||
	    op->tile_w > 4096 || op->tile_h > 4096)
		return false;
	if (!SheepForceBindFB() || !g_fb_nocopy)
		return false;
	const uint32 off = (uint32)(op->dest - base);
	if (!fb_span_ok(off, op->rowbytes, op->width * 4, op->height))
		return false;
	if (op->row_start[0] != 0 || op->row_start[op->height] < 0)
		return false;
	const size_t nruns = (size_t)op->row_start[op->height];
	if (nruns == 0)
		return true;			/* clipped away entirely */
	struct { uint x, y, w, h, row, tw, th, ox, oy; } u;
	u.x = (uint)((off % (uint32)op->rowbytes) / 4u);
	u.y = (uint)(off / (uint32)op->rowbytes);
	u.w = (uint)op->width;
	u.h = (uint)op->height;
	u.row = (uint)op->rowbytes;
	u.tw = (uint)op->tile_w;
	u.th = (uint)op->tile_h;
	u.ox = (uint)(op->tile_ox % op->tile_w);
	u.oy = (uint)(op->tile_oy % op->tile_h);
	id<MTLComputeCommandEncoder> enc = SheepForceOpenEncoder();
	if (!enc)
		return false;
	note_flight(op->dest, op->rowbytes, op->width * 4, op->height);
	[enc setComputePipelineState:g_filltile_pipe];
	[enc setBuffer:g_fb offset:0 atIndex:0];
	[enc setBytes:&u length:sizeof u atIndex:1];
	const size_t rs_bytes = (size_t)(op->height + 1) * sizeof(int32_t), run_bytes = nruns * 2 * sizeof(int32_t);
	const size_t tile_bytes = (size_t)op->tile_w * (size_t)op->tile_h * sizeof(uint32_t);
	if (rs_bytes <= 4096)
		[enc setBytes:op->row_start length:rs_bytes atIndex:2];
	else
		[enc setBuffer:[g_dev newBufferWithBytes:op->row_start length:rs_bytes options:MTLResourceStorageModeShared] offset:0 atIndex:2];
	if (run_bytes <= 4096)
		[enc setBytes:op->runs length:run_bytes atIndex:3];
	else
		[enc setBuffer:[g_dev newBufferWithBytes:op->runs length:run_bytes options:MTLResourceStorageModeShared] offset:0 atIndex:3];
	if (tile_bytes <= 4096)
		[enc setBytes:op->tile length:tile_bytes atIndex:4];
	else
		[enc setBuffer:[g_dev newBufferWithBytes:op->tile length:tile_bytes options:MTLResourceStorageModeShared] offset:0 atIndex:4];
	NSUInteger tw = g_filltile_pipe.threadExecutionWidth > 0 ? g_filltile_pipe.threadExecutionWidth : 16;
	[enc dispatchThreads:MTLSizeMake(u.w, u.h, 1) threadsPerThreadgroup:MTLSizeMake(tw, 1, 1)];
	g_dirty = true;
	return true;
}

/* Which blits the kernel implements exactly; nqd_blit_ops.h is shared with the CPU fallback. */
static bool blit_supported(const SheepForceBlitOp *op)
{
	if (op->width <= 0 || op->height <= 0 || op->dst_row < 1 || op->src_row < 1)
		return false;
	if (!nqd_blit_mode_ok(op->mode, op->dbpp, op->sbpp))
		return false;
	return op->sbpp != 1 || op->dbpp != 4 || op->pal != NULL;
}

bool SheepForceTryBlit(const SheepForceBlitOp *op)
{
	uint8 *base = SheepForcePageHost(0);
	if (!g_dev || !g_blit_pipe || !base || !op || !op->src || !op->dest || op->dest < base
	    || !blit_supported(op))
		return false;
	if (!SheepForceBindFB() || !g_fb_nocopy)
		return false;
	const int dwb = op->width * op->dbpp, swb = op->width * op->sbpp;
	const uint32 d0 = (uint32)(op->dest - base);
	if (!fb_span_ok(d0, op->dst_row, dwb, op->height))
		return false;
	uint32 fb_bytes = (uint32)g_fb.length;
	bool src_in = op->src >= base && (uint32)(op->src - base) < fb_bytes;
	uint32 s0 = src_in ? (uint32)(op->src - base) : 0;
	if (src_in && !fb_span_ok(s0, op->src_row, swb, op->height))
		return false;
	bool overlap = false;
	if (src_in) {
		uint32 sbytes = (uint32)op->src_row * (uint32)(op->height - 1) + (uint32)swb;
		uint32 dbytes = (uint32)op->dst_row * (uint32)(op->height - 1) + (uint32)dwb;
		overlap = s0 < d0 + dbytes && d0 < s0 + sbytes;
	}
	struct { uint w, h, dst_row, src_row, sbpp, dbpp, dst_off, src_off, mode, back; } u;
	u.w = (uint)op->width;
	u.h = (uint)op->height;
	u.dst_row = (uint)op->dst_row;
	u.sbpp = (uint)op->sbpp;
	u.dbpp = (uint)op->dbpp;
	u.dst_off = d0;
	u.mode = (uint)op->mode;
	u.back = op->back_word;
	id<MTLBuffer> src_buf = g_fb;
	id<MTLBuffer> gather_to = nil;
	if (src_in) {
		u.src_off = s0;
		u.src_row = (uint)op->src_row;
		if (overlap) {
			if (!g_gather_pipe)
				return false;
			size_t need = (size_t)swb * (size_t)op->height;
			if (!g_scr || g_scr.length < need) {
				g_scr = [g_dev newBufferWithLength:need options:MTLResourceStorageModePrivate];
				if (!g_scr)
					return false;
			}
			gather_to = g_scr;
			src_buf = g_scr;
			u.src_off = 0;
			u.src_row = (uint)swb;
		}
	} else {
		/* One staging buffer. The previous icon is still reading it, so everything
		 * queued so far has to finish before it is overwritten. */
		if (g_pending || g_open_cb) {
			SheepForceCommitOpen();
			[g_pending waitUntilCompleted];
			g_pending = nil;
			g_flight_on = false;
		}
		size_t need = (size_t)swb * (size_t)op->height;
		if (!g_src || g_src.length < need) {
			g_src = [g_dev newBufferWithLength:need options:MTLResourceStorageModeShared];
			if (!g_src)
				return false;
		}
		uint8 *packed = (uint8 *)g_src.contents;
		for (int y = 0; y < op->height; y++)
			memcpy(packed + (size_t)y * (size_t)swb,
			       op->src + (size_t)y * (size_t)op->src_row, (size_t)swb);
		src_buf = g_src;
		u.src_off = 0;
		u.src_row = (uint)swb;
	}
	note_flight(op->dest, op->dst_row, dwb, op->height);
	id<MTLComputeCommandEncoder> enc = SheepForceOpenEncoder();
	if (!enc)
		return false;
	if (gather_to) {
		struct { uint wbytes, h, src_row, src_off; } gu = { (uint)swb, (uint)op->height, (uint)op->src_row, s0 };
		[enc setComputePipelineState:g_gather_pipe];
		[enc setBuffer:g_fb offset:0 atIndex:0];
		[enc setBuffer:gather_to offset:0 atIndex:1];
		[enc setBytes:&gu length:sizeof gu atIndex:2];
		NSUInteger gw = g_gather_pipe.threadExecutionWidth > 0 ? g_gather_pipe.threadExecutionWidth : 16;
		[enc dispatchThreads:MTLSizeMake(gu.wbytes, gu.h, 1) threadsPerThreadgroup:MTLSizeMake(gw, 1, 1)];
	}
	uint32 zero_pal[256];
	if (!op->pal)
		memset(zero_pal, 0, sizeof zero_pal);
	[enc setComputePipelineState:g_blit_pipe];
	[enc setBuffer:g_fb offset:0 atIndex:0];
	[enc setBuffer:src_buf offset:0 atIndex:1];
	[enc setBytes:&u length:sizeof u atIndex:2];
	[enc setBytes:(op->pal ? op->pal : zero_pal) length:256 * sizeof(uint32) atIndex:3];
	NSUInteger tw = g_blit_pipe.threadExecutionWidth > 0 ? g_blit_pipe.threadExecutionWidth : 16;
	[enc dispatchThreads:MTLSizeMake(u.w, u.h, 1) threadsPerThreadgroup:MTLSizeMake(tw, 1, 1)];
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
	SheepForceCommitOpen();
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
	/* Debug: with SHEEPFORCE_DUMP=<file.ppm> the visible page is written out every 10 s,
	 * so a run can be checked without capturing the screen. */
	static const char *dump_path = getenv("SHEEPFORCE_DUMP");
	static CFAbsoluteTime last_dump;
	if (dump_path && depth == 32 && g_presented && g_presented_bytes >= (uint32)(row * gh)) {
		CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
		if (now - last_dump > 10.0) {
			last_dump = now;
			SheepForceFlushCPU(NULL, 0, 0, 0);
			if (FILE *f = fopen(dump_path, "wb")) {
				fprintf(f, "P6\n%d %d\n255\n", gw, gh);
				for (int yy = 0; yy < gh; yy++)
					for (int xx = 0; xx < gw; xx++) {
						const uint8 *px = g_presented + (size_t)yy * row + (size_t)xx * 4;
						fputc(px[1], f); fputc(px[2], f); fputc(px[3], f);
					}
				fclose(f);
			}
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
	/* The same sampled bytes (every 16th), folded by four independent FNV lanes so the multiplies overlap;
	 * the value is only ever compared for equality (movie frame detection), so its exact bits are free to change. */
	uint32 h0 = 2166136261u, h1 = 2166136261u + 1u, h2 = 2166136261u + 2u, h3 = 2166136261u + 3u;
	const uint8 *p = g_presented;
	uint32 i = 0;
	for (; i + 64 <= g_presented_bytes; i += 64) {
		h0 = (h0 ^ p[i]) * 16777619u;
		h1 = (h1 ^ p[i + 16]) * 16777619u;
		h2 = (h2 ^ p[i + 32]) * 16777619u;
		h3 = (h3 ^ p[i + 48]) * 16777619u;
	}
	for (; i < g_presented_bytes; i += 16)
		h0 = (h0 ^ p[i]) * 16777619u;
	uint32 hash = ((h0 * 31u + h1) * 31u + h2) * 31u + h3;
	if (g_pal && g_pal.length >= 1024) {
		const uint8 *pal = (const uint8 *)g_pal.contents;
		for (int i = 0; i < 1024; i += 4) {
			hash ^= pal[i];
			hash *= 16777619u;
		}
	}
	return hash;
}

/* Used by sheepforce_rave.mm. */
id<MTLDevice> SheepForceMetalDevice(void) { return g_dev; }
id<MTLCommandQueue> SheepForceMetalQueue(void) { return g_queue; }
id<MTLLibrary> SheepForceMetalLibrary(void) { return g_lib; }
