/*
 *  sheepforce_rave.mm - Metal rendering for RAVE draw contexts
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

/*
 * One RaveMetalCtx per RAVE draw context: a persistent BGRA8 colour texture and an
 * optional Depth32Float texture. Triangles queue up on the host and are drawn in one
 * render pass when the context is resolved (RenderEnd / Sync). Resolving copies the
 * colour texture to a shared buffer, waits, and converts to the guest's pixel format
 * (big-endian) in guest memory. kQAPixel_xxx values below are RAVE's.
 */
#include "sysdeps.h"
#include "sheepforce.h"
#include "sheepforce_metal_private.h"
#include <string.h>
#include <vector>

enum { kPix_RGB16 = 1, kPix_ARGB16 = 2, kPix_RGB32 = 3, kPix_ARGB32 = 4 };
enum { kZ_None = 0, kZ_LT, kZ_EQ, kZ_LE, kZ_GT, kZ_NE, kZ_GE, kZ_True, kZ_False };

struct RaveMetalCtx {
	int w, h;
	id<MTLTexture> color;
	id<MTLTexture> depth;		/* nil when the context has no Z buffer */
	id<MTLTexture> msaa_color, msaa_depth;	/* multisampled targets while antialiasing is on */
	int samples;			/* 1, 2 or 4 */
	std::vector<RaveVertex> verts;
	std::vector<RaveBatch> batches;
	bool clear_color_pending, clear_depth_pending;
	float clear_rgba[4];
	float clear_z;
	bool ever_rendered;
};

struct RaveMetalTex { id<MTLTexture> tex; };
struct RaveFragU { float fog[4]; float start, end, density, maxdepth; uint32 fogmode, texop, textured, mtop; float mtfactor; uint32 mton, pad1, pad2; };
static id<MTLSamplerState> g_rave_smp[3][2];	/* [nearest, linear, linear+mip][wrap, clamp] */
static id<MTLTexture> g_rave_white;

static id<MTLRenderPipelineState> g_rave_pipe[2][2][3];	/* [blend interpolate][has depth][1, 2, 4 samples] */
static id<MTLDepthStencilState> g_rave_dss[9][2];	/* [zfunc][write] */

static int sample_index(int samples) { return samples >= 4 ? 2 : samples >= 2 ? 1 : 0; }

static id<MTLRenderPipelineState> rave_pipe(int blend, bool has_depth, int samples)
{
	const int bi = blend == 1 ? 1 : 0;
	const int di = has_depth ? 1 : 0;
	const int si = sample_index(samples);
	if (g_rave_pipe[bi][di][si])
		return g_rave_pipe[bi][di][si];
	id<MTLDevice> dev = SheepForceMetalDevice();
	id<MTLLibrary> lib = SheepForceMetalLibrary();
	if (!dev || !lib)
		return nil;
	id<MTLFunction> vs = [lib newFunctionWithName:@"sf_rave_vs"];
	id<MTLFunction> fs = [lib newFunctionWithName:@"sf_rave_fs"];
	if (!vs || !fs)
		return nil;
	MTLRenderPipelineDescriptor *pd = [[MTLRenderPipelineDescriptor alloc] init];
	pd.vertexFunction = vs;
	pd.fragmentFunction = fs;
	MTLRenderPipelineColorAttachmentDescriptor *ca = pd.colorAttachments[0];
	ca.pixelFormat = MTLPixelFormatBGRA8Unorm;
	ca.blendingEnabled = YES;
	ca.rgbBlendOperation = MTLBlendOperationAdd;
	ca.alphaBlendOperation = MTLBlendOperationAdd;
	/* PreMultiply: dst = (1 - As) * dst + src.  Interpolate: dst = (1 - As) * dst + As * src.
	 * The destination alpha is 1 - (1 - As)(1 - Ad) in both. */
	ca.sourceRGBBlendFactor = bi ? MTLBlendFactorSourceAlpha : MTLBlendFactorOne;
	ca.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
	ca.sourceAlphaBlendFactor = MTLBlendFactorOne;
	ca.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
	if (has_depth)
		pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
	pd.rasterSampleCount = (NSUInteger)(si == 2 ? 4 : si == 1 ? 2 : 1);
	NSError *err = nil;
	g_rave_pipe[bi][di][si] = [dev newRenderPipelineStateWithDescriptor:pd error:&err];
	if (!g_rave_pipe[bi][di][si])
		printf("SheepForce RAVE: pipeline failed: %s\n", err ? err.localizedDescription.UTF8String : "?");
	return g_rave_pipe[bi][di][si];
}

static id<MTLDepthStencilState> rave_dss(int zfunc, bool write)
{
	if (zfunc < 0 || zfunc > kZ_False)
		zfunc = kZ_None;
	const int wi = write ? 1 : 0;
	if (g_rave_dss[zfunc][wi])
		return g_rave_dss[zfunc][wi];
	MTLDepthStencilDescriptor *dd = [[MTLDepthStencilDescriptor alloc] init];
	switch (zfunc) {
	case kZ_LT: dd.depthCompareFunction = MTLCompareFunctionLess; break;
	case kZ_EQ: dd.depthCompareFunction = MTLCompareFunctionEqual; break;
	case kZ_LE: dd.depthCompareFunction = MTLCompareFunctionLessEqual; break;
	case kZ_GT: dd.depthCompareFunction = MTLCompareFunctionGreater; break;
	case kZ_NE: dd.depthCompareFunction = MTLCompareFunctionNotEqual; break;
	case kZ_GE: dd.depthCompareFunction = MTLCompareFunctionGreaterEqual; break;
	case kZ_False: dd.depthCompareFunction = MTLCompareFunctionNever; break;
	default: dd.depthCompareFunction = MTLCompareFunctionAlways; break;	/* None, True */
	}
	dd.depthWriteEnabled = write && zfunc != kZ_None && zfunc != kZ_False;
	g_rave_dss[zfunc][wi] = [SheepForceMetalDevice() newDepthStencilStateWithDescriptor:dd];
	return g_rave_dss[zfunc][wi];
}

void *SheepForceRaveTexNew(int levels, const int *w, const int *h, const uint8 *const *rgba)
{
	id<MTLDevice> dev = SheepForceMetalDevice();
	if (!dev || levels < 1 || levels > 16 || w[0] < 1 || h[0] < 1)
		return NULL;
	MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
		width:(NSUInteger)w[0] height:(NSUInteger)h[0] mipmapped:NO];
	d.mipmapLevelCount = (NSUInteger)levels;
	d.usage = MTLTextureUsageShaderRead;
	id<MTLTexture> t = [dev newTextureWithDescriptor:d];
	if (!t)
		return NULL;
	for (int i = 0; i < levels; i++)
		[t replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger)w[i], (NSUInteger)h[i]) mipmapLevel:(NSUInteger)i
			withBytes:rgba[i] bytesPerRow:(NSUInteger)w[i] * 4];
	RaveMetalTex *r = new RaveMetalTex();
	r->tex = t;
	return r;
}

void SheepForceRaveTexDelete(void *tex)
{
	delete (RaveMetalTex *)tex;
}

static id<MTLSamplerState> rave_sampler(int filter, bool clamp)
{
	const int fi = filter <= 0 ? 0 : filter == 1 ? 1 : 2;
	const int ci = clamp ? 1 : 0;
	if (g_rave_smp[fi][ci])
		return g_rave_smp[fi][ci];
	MTLSamplerDescriptor *sd = [[MTLSamplerDescriptor alloc] init];
	sd.minFilter = sd.magFilter = fi == 0 ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
	sd.mipFilter = fi == 2 ? MTLSamplerMipFilterLinear : fi == 1 ? MTLSamplerMipFilterNearest : MTLSamplerMipFilterNotMipmapped;
	sd.sAddressMode = sd.tAddressMode = clamp ? MTLSamplerAddressModeClampToEdge : MTLSamplerAddressModeRepeat;
	g_rave_smp[fi][ci] = [SheepForceMetalDevice() newSamplerStateWithDescriptor:sd];
	return g_rave_smp[fi][ci];
}

static id<MTLTexture> rave_white(void)
{
	if (!g_rave_white) {
		MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
			width:1 height:1 mipmapped:NO];
		g_rave_white = [SheepForceMetalDevice() newTextureWithDescriptor:d];
		const uint8 px[4] = { 255, 255, 255, 255 };
		[g_rave_white replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:px bytesPerRow:4];
	}
	return g_rave_white;
}

void *SheepForceRaveCtxNew(int width, int height, bool depth)
{
	id<MTLDevice> dev = SheepForceMetalDevice();
	if (!dev || width < 1 || height < 1 || width > 8192 || height > 8192)
		return NULL;
	RaveMetalCtx *c = new RaveMetalCtx();
	c->w = width;
	c->h = height;
	MTLTextureDescriptor *cd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
		width:(NSUInteger)width height:(NSUInteger)height mipmapped:NO];
	cd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
	cd.storageMode = MTLStorageModePrivate;
	c->color = [dev newTextureWithDescriptor:cd];
	if (depth) {
		MTLTextureDescriptor *dd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
			width:(NSUInteger)width height:(NSUInteger)height mipmapped:NO];
		dd.usage = MTLTextureUsageRenderTarget;
		dd.storageMode = MTLStorageModePrivate;
		c->depth = [dev newTextureWithDescriptor:dd];
	}
	if (!c->color || (depth && !c->depth)) {
		delete c;
		return NULL;
	}
	c->samples = 1;
	c->clear_color_pending = true;
	c->clear_depth_pending = depth;
	c->clear_z = 1.0f;
	return c;
}

void SheepForceRaveCtxDelete(void *ctx)
{
	delete (RaveMetalCtx *)ctx;
}

void SheepForceRaveCtxClear(void *ctx, const float rgba[4], bool clear_depth, float depth, int antialias)
{
	RaveMetalCtx *c = (RaveMetalCtx *)ctx;
	if (!c)
		return;
	/* kQAAntiAlias_Off / Fast / Mid / Best -> 1, 2, 4, 4 samples. */
	const int want = antialias <= 0 ? 1 : antialias == 1 ? 2 : 4;
	if (want != c->samples) {
		c->samples = want;
		c->msaa_color = nil;
		c->msaa_depth = nil;
		id<MTLDevice> dev = SheepForceMetalDevice();
		if (want > 1 && dev && [dev supportsTextureSampleCount:(NSUInteger)want]) {
			MTLTextureDescriptor *cd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
				width:(NSUInteger)c->w height:(NSUInteger)c->h mipmapped:NO];
			cd.textureType = MTLTextureType2DMultisample;
			cd.sampleCount = (NSUInteger)want;
			cd.usage = MTLTextureUsageRenderTarget;
			cd.storageMode = MTLStorageModePrivate;
			c->msaa_color = [dev newTextureWithDescriptor:cd];
			if (c->depth) {
				MTLTextureDescriptor *dd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
					width:(NSUInteger)c->w height:(NSUInteger)c->h mipmapped:NO];
				dd.textureType = MTLTextureType2DMultisample;
				dd.sampleCount = (NSUInteger)want;
				dd.usage = MTLTextureUsageRenderTarget;
				dd.storageMode = MTLStorageModePrivate;
				c->msaa_depth = [dev newTextureWithDescriptor:dd];
			}
			if (!c->msaa_color || (c->depth && !c->msaa_depth)) {
				c->msaa_color = nil;
				c->msaa_depth = nil;
				c->samples = 1;
			}
		} else if (want > 1) {
			c->samples = 1;
		}
	}
	/* A new frame replaces whatever was queued but not yet drawn. */
	c->verts.clear();
	c->batches.clear();
	memcpy(c->clear_rgba, rgba, sizeof c->clear_rgba);
	c->clear_color_pending = true;
	c->clear_depth_pending = clear_depth && c->depth != nil;
	c->clear_z = depth;
}

void SheepForceRaveCtxDraw(void *ctx, const RaveBatch *batch, const RaveVertex *vertices)
{
	RaveMetalCtx *c = (RaveMetalCtx *)ctx;
	if (!c || !batch || !vertices || batch->count == 0)
		return;
	RaveBatch b = *batch;
	b.first = (uint32)c->verts.size();
	c->verts.insert(c->verts.end(), vertices, vertices + batch->count);
	/* Merge with the previous batch when the state is the same. */
	if (!c->batches.empty()) {
		RaveBatch &p = c->batches.back();
		if (p.zfunc == b.zfunc && p.zwrite == b.zwrite && p.blend == b.blend && p.texop == b.texop &&
		    p.filter == b.filter && p.clamp == b.clamp && p.fogmode == b.fogmode && p.tex == b.tex &&
		    p.tex2 == b.tex2 && p.mtop == b.mtop && p.mtfilter == b.mtfilter && p.mtclamp == b.mtclamp && p.mtfactor == b.mtfactor &&
		    memcmp(p.fog, b.fog, sizeof p.fog) == 0 && p.first + p.count == b.first) {
			p.count += b.count;
			return;
		}
	}
	c->batches.push_back(b);
}

bool SheepForceRaveCtxResolve(void *ctx, uint8 *dst, int row_bytes, int pixel_type,
			      int x0, int y0, int x1, int y1, const uint8 *mask)
{
	RaveMetalCtx *c = (RaveMetalCtx *)ctx;
	id<MTLDevice> dev = SheepForceMetalDevice();
	id<MTLCommandQueue> queue = SheepForceMetalQueue();
	if (!c || !dev || !queue || !dst)
		return false;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > c->w) x1 = c->w;
	if (y1 > c->h) y1 = c->h;
	if (x1 <= x0 || y1 <= y0)
		return true;
	const bool has_depth = c->depth != nil;

	id<MTLCommandBuffer> cb = [queue commandBuffer];
	if (c->clear_color_pending || c->clear_depth_pending || !c->batches.empty()) {
		MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
		const bool aa = c->samples > 1 && c->msaa_color != nil;
		if (aa) {
			rp.colorAttachments[0].texture = c->msaa_color;
			rp.colorAttachments[0].resolveTexture = c->color;
			rp.colorAttachments[0].storeAction = MTLStoreActionStoreAndMultisampleResolve;
		} else {
			rp.colorAttachments[0].texture = c->color;
			rp.colorAttachments[0].storeAction = MTLStoreActionStore;
		}
		if (c->clear_color_pending || !c->ever_rendered) {
			rp.colorAttachments[0].loadAction = MTLLoadActionClear;
			rp.colorAttachments[0].clearColor = MTLClearColorMake(c->clear_rgba[1], c->clear_rgba[2],
				c->clear_rgba[3], c->clear_rgba[0]);	/* stored as a, r, g, b like RAVE's tags */
		} else {
			rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
		}
		if (has_depth) {
			rp.depthAttachment.texture = aa && c->msaa_depth ? c->msaa_depth : c->depth;
			rp.depthAttachment.storeAction = MTLStoreActionStore;
			if (c->clear_depth_pending || !c->ever_rendered) {
				rp.depthAttachment.loadAction = MTLLoadActionClear;
				rp.depthAttachment.clearDepth = c->clear_z;
			} else {
				rp.depthAttachment.loadAction = MTLLoadActionLoad;
			}
		}
		id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
		if (!c->batches.empty()) {
			id<MTLBuffer> vb = [dev newBufferWithBytes:c->verts.data()
				length:c->verts.size() * sizeof(RaveVertex) options:MTLResourceStorageModeShared];
			const float u[2] = { (float)c->w, (float)c->h };
			[enc setVertexBuffer:vb offset:0 atIndex:0];
			[enc setVertexBytes:u length:sizeof u atIndex:1];
			[enc setCullMode:MTLCullModeNone];
			for (const RaveBatch &b : c->batches) {
				id<MTLRenderPipelineState> ps = rave_pipe(b.blend, has_depth, aa ? c->samples : 1);
				if (!ps)
					continue;
				[enc setRenderPipelineState:ps];
				if (has_depth)
					[enc setDepthStencilState:rave_dss(b.zfunc, b.zwrite != 0)];
				RaveFragU fu;
				memset(&fu, 0, sizeof fu);
				fu.fog[0] = b.fog[1]; fu.fog[1] = b.fog[2]; fu.fog[2] = b.fog[3]; fu.fog[3] = b.fog[0];
				fu.start = b.fog[4]; fu.end = b.fog[5]; fu.density = b.fog[6]; fu.maxdepth = b.fog[7];
				fu.fogmode = b.fogmode;
				fu.texop = b.texop;
				fu.textured = b.tex ? 1 : 0;
				fu.mton = (b.tex && b.tex2) ? 1 : 0;
				fu.mtop = b.mtop;
				fu.mtfactor = b.mtfactor;
				[enc setFragmentBytes:&fu length:sizeof fu atIndex:0];
				[enc setFragmentTexture:b.tex ? ((RaveMetalTex *)b.tex)->tex : rave_white() atIndex:0];
				[enc setFragmentSamplerState:rave_sampler(b.filter, b.clamp != 0) atIndex:0];
				[enc setFragmentTexture:b.tex2 ? ((RaveMetalTex *)b.tex2)->tex : rave_white() atIndex:1];
				[enc setFragmentSamplerState:rave_sampler(b.mtfilter, b.mtclamp != 0) atIndex:1];
				[enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:b.first vertexCount:b.count];
			}
		}
		[enc endEncoding];
		c->ever_rendered = true;
		c->clear_color_pending = c->clear_depth_pending = false;
		c->verts.clear();
		c->batches.clear();
	}
	const NSUInteger rw = (NSUInteger)(x1 - x0), rh = (NSUInteger)(y1 - y0);
	id<MTLBuffer> rb = [dev newBufferWithLength:rw * rh * 4 options:MTLResourceStorageModeShared];
	if (!rb)
		return false;
	id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
	[blit copyFromTexture:c->color sourceSlice:0 sourceLevel:0
		sourceOrigin:MTLOriginMake((NSUInteger)x0, (NSUInteger)y0, 0) sourceSize:MTLSizeMake(rw, rh, 1)
		toBuffer:rb destinationOffset:0 destinationBytesPerRow:rw * 4 destinationBytesPerImage:rw * rh * 4];
	[blit endEncoding];
	[cb commit];
	[cb waitUntilCompleted];

	const uint8 *src = (const uint8 *)rb.contents;
	for (NSUInteger y = 0; y < rh; y++) {
		const uint8 *s = src + y * rw * 4;
		uint8 *d = dst + (size_t)(y0 + (int)y) * (size_t)row_bytes;
		const uint8 *mrow = mask ? mask + (size_t)(y0 + (int)y) * (size_t)c->w + (size_t)x0 : NULL;
		switch (pixel_type) {
		case kPix_ARGB32:
		case kPix_RGB32:
			d += (size_t)x0 * 4;
			for (NSUInteger x = 0; x < rw; x++, s += 4, d += 4) {
				if (mrow && !mrow[x])
					continue;
				d[0] = pixel_type == kPix_ARGB32 ? s[3] : 0;
				d[1] = s[2];
				d[2] = s[1];
				d[3] = s[0];
			}
			break;
		case kPix_RGB16:
		case kPix_ARGB16:
			d += (size_t)x0 * 2;
			for (NSUInteger x = 0; x < rw; x++, s += 4, d += 2) {
				if (mrow && !mrow[x])
					continue;
				uint16 v = (uint16)(((s[2] >> 3) << 10) | ((s[1] >> 3) << 5) | (s[0] >> 3));
				if (pixel_type == kPix_ARGB16 && s[3] >= 128)
					v |= 0x8000;
				d[0] = (uint8)(v >> 8);
				d[1] = (uint8)v;
			}
			break;
		default:
			return false;
		}
	}
	return true;
}
