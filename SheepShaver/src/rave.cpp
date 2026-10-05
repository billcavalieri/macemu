/*
 *  rave.cpp - SheepForce RAVE drawing engine (Apple's RAVE API, backed by Metal)
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
 *  A RAVE engine is not a Component. The guest's RAVE manager (the CFM library
 *  "QuickDraw(TM) 3D Accelerator", 0xAA being the trademark sign) exports
 *  QARegisterEngine(engineGetMethod). The manager then calls engineGetMethod(tag,
 *  &method) for each engine method, and our drawPrivateNew registers the draw-context
 *  methods with QARegisterDrawMethod. All of those are C callbacks the PowerPC guest
 *  makes, so each one is a guest TVECT that runs the native op NATIVE_RAVE_METHOD with a
 *  slot number in r0 (see NativeSlotTVECT). The constants below are written from the RAVE
 *  interface (RAVE.h / RAVESystem.h, Apple's QuickDraw 3D SDK, an external requirement
 *  that is not part of this repository).
 */

#include "sysdeps.h"
#include "prefs.h"
#include "sheepforce.h"
#include "rave_engine.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <vector>
#ifdef SHEEPFORCE_RAVE_HARNESS
/* The test harness supplies a flat fake guest memory. */
extern uint32 rave_h_read32(uint32 addr);
extern uint32 rave_h_read16(uint32 addr);
extern void rave_h_write32(uint32 addr, uint32 v);
extern void rave_h_write8(uint32 addr, uint8 v);
extern uint8 *rave_h_host(uint32 addr);
#define RAVE_R32 rave_h_read32
#define RAVE_R16 rave_h_read16
#define RAVE_W32 rave_h_write32
#define RAVE_W8 rave_h_write8
#define RAVE_HOST rave_h_host
#else
#include "cpu_emulation.h"
#include "thunks.h"
#include "macos_util.h"
#include "video.h"
#include "nw_io.h"
#define RAVE_R32 ReadMacInt32
#define RAVE_R16 ReadMacInt16
#define RAVE_W32 WriteMacInt32
#define RAVE_W8 WriteMacInt8
#define RAVE_HOST Mac2HostAddr
#endif

/* TQAError */
enum {
	kQANoErr = 0, kQAError = 1, kQAOutOfMemory = 2, kQANotSupported = 3,
	kQAOutOfDate = 4, kQAParamErr = 5, kQAGestaltUnknown = 6
};

/* TQAEngineMethodTag, passed to engineGetMethod */
enum {
	kQADrawPrivateNew = 0, kQADrawPrivateDelete = 1, kQAEngineCheckDevice = 2, kQAEngineGestalt = 3,
	kQATextureNew = 4, kQATextureDetach = 5, kQATextureDelete = 6, kQABitmapNew = 7,
	kQABitmapDetach = 8, kQABitmapDelete = 9, kQAColorTableNew = 10, kQAColorTableDelete = 11,
	kQATextureBindColorTable = 12, kQABitmapBindColorTable = 13, kQAAccessTexture = 14,
	kQAAccessTextureEnd = 15, kQAAccessBitmap = 16, kQAAccessBitmapEnd = 17, kQAEngineMethodCount = 18
};

/* TQAGestaltSelector */
enum {
	kQAGestalt_OptionalFeatures = 0, kQAGestalt_FastFeatures = 1, kQAGestalt_VendorID = 2,
	kQAGestalt_EngineID = 3, kQAGestalt_Revision = 4, kQAGestalt_ASCIINameLength = 5,
	kQAGestalt_ASCIIName = 6, kQAGestalt_TextureMemory = 7, kQAGestalt_FastTextureMemory = 8,
	kQAGestalt_DrawContextPixelTypesAllowed = 9, kQAGestalt_DrawContextPixelTypesPreferred = 10,
	kQAGestalt_TexturePixelTypesAllowed = 11, kQAGestalt_TexturePixelTypesPreferred = 12,
	kQAGestalt_BitmapPixelTypesAllowed = 13, kQAGestalt_BitmapPixelTypesPreferred = 14,
	kQAGestalt_OptionalFeatures2 = 15, kQAGestalt_MultiTextureMax = 16
};

/* TQADrawMethodTag, passed to QARegisterDrawMethod */
enum {
	kQASetFloat = 0, kQASetInt = 1, kQASetPtr = 2, kQAGetFloat = 3, kQAGetInt = 4, kQAGetPtr = 5,
	kQADrawPoint = 6, kQADrawLine = 7, kQADrawTriGouraud = 8, kQADrawTriTexture = 9,
	kQADrawVGouraud = 10, kQADrawVTexture = 11, kQADrawBitmap = 12, kQARenderStart = 13,
	kQARenderEnd = 14, kQARenderAbort = 15, kQAFlush = 16, kQASync = 17,
	kQASubmitVerticesGouraud = 18, kQASubmitVerticesTexture = 19, kQADrawTriMeshGouraud = 20,
	kQADrawTriMeshTexture = 21, kQASetNoticeMethod = 22, kQAGetNoticeMethod = 23,
	kQASubmitMultiTextureParams = 24, kQAAccessDrawBuffer = 25, kQAAccessDrawBufferEnd = 26,
	kQAAccessZBuffer = 27, kQAAccessZBufferEnd = 28, kQAClearDrawBuffer = 29, kQAClearZBuffer = 30,
	kQATextureFromContext = 31, kQABitmapFromContext = 32, kQABusy = 33, kQASwapBuffers = 34,
	kQADrawMethodCount = 35
};

/* Tags used so far (TQATagInt / TQATagFloat) and their values. */
enum {
	kTag_ZFunction = 0, kTag_ColorBG_a = 1, kTag_ColorBG_r = 2, kTag_ColorBG_g = 3, kTag_ColorBG_b = 4,
	kTag_Width = 5, kTag_ZMinOffset = 6, kTag_ZMinScale = 7, kTag_Blend = 9, kTag_ZBufferMask = 28,
	kTag_Antialias = 8, kTag_MultiTexture = 26, kTag_MultiTextureEnable = 33, kTag_MultiTextureCurrent = 34,
	kTag_MultiTextureOp = 35, kTag_MultiTextureFilter = 36, kTag_MultiTextureWrapU = 37, kTag_MultiTextureWrapV = 38,
	kTag_MultiTextureFactor = 51,
	kTag_Texture = 13, kTag_TextureFilter = 11, kTag_TextureOp = 12, kTag_FogMode = 17,
	kTag_FogColor_a = 18, kTag_FogColor_r = 19, kTag_FogColor_g = 20, kTag_FogColor_b = 21,
	kTag_FogStart = 22, kTag_FogEnd = 23, kTag_FogDensity = 24, kTag_FogMaxDepth = 25,
	kTagGL_TextureWrapU = 101, kTagGL_TextureWrapV = 102,
	kTag_DepthBG = 112, kTagCount = 160
};

/* kQATextureOp_xxx (mask) and kQAGL_Clamp */
enum { kQATextureOp_Shrink = 1 << 3, kQAGL_Clamp = 1 };

/* kQATexture_xxx flags */
enum { kQATexture_Mipmap = 1 << 1, kQATexture_FlipOrigin = 1 << 6 };

/* TQAImagePixelType values used by textures and color tables */
enum { kQAPixel_CL4 = 5, kQAPixel_CL8 = 6, kQAPixel_RGB8_332 = 9, kQAPixel_ARGB16_4444 = 10,
       kQAPixel_ACL16_88 = 11, kQAPixel_I8 = 12, kQAPixel_AI16_88 = 13 };

/* TQAVertexMode */
enum { kQAVertexMode_Point = 0, kQAVertexMode_Line, kQAVertexMode_Polyline, kQAVertexMode_Tri,
       kQAVertexMode_Strip, kQAVertexMode_Fan };

/* TQAImagePixelType used for draw contexts */
enum { kQAPixel_RGB16 = 1, kQAPixel_ARGB16 = 2, kQAPixel_RGB32 = 3, kQAPixel_ARGB32 = 4 };

/* TQADeviceType; TQADevice = { long deviceType; union { TQADeviceMemory memoryDevice; GDHandle gDevice; } }
 * TQADeviceMemory = { long rowBytes; long pixelType; long width; long height; void *baseAddr; } */
enum { kQADeviceMemory = 0, kQADeviceGDevice = 1 };

/* kQAContext_xxx */
enum { kQAContext_NoZBuffer = 1 << 0, kQAContext_DeepZ = 1 << 1 };

/* Native-op slots. Engine methods use their tag, draw methods 100 + tag. */
enum { SLOT_ENGINE_BASE = RAVE_SLOT_ENGINE_BASE, SLOT_DRAW_BASE = RAVE_SLOT_DRAW_BASE, SLOT_GETMETHOD = RAVE_SLOT_GETMETHOD };

static const char *const engine_method_name[kQAEngineMethodCount] = {
	"drawPrivateNew", "drawPrivateDelete", "engineCheckDevice", "engineGestalt",
	"textureNew", "textureDetach", "textureDelete", "bitmapNew", "bitmapDetach", "bitmapDelete",
	"colorTableNew", "colorTableDelete", "textureBindColorTable", "bitmapBindColorTable",
	"accessTexture", "accessTextureEnd", "accessBitmap", "accessBitmapEnd"
};


static const char kEngineName[] = "SheepForce Metal";
static RaveHost g_host;
static bool g_registered;

void SheepForceRaveSetHost(const RaveHost *host)
{
	g_host = *host;
}

static bool rave_trace(void)
{
	static int cached = -1;
	if (cached < 0)
		cached = PrefsFindBool("sheepforce_probe") ? 1 : 0;
	return cached != 0;
}

/* n-th integer argument of a guest call (n counts from 0 for r3); beyond r10 it is on the stack. */
static uint32 arg(const RaveGuestCall *c, int n)
{
	return n < 8 ? c->r[n] : RAVE_R32(c->sp + 24 + 4 * (uint32)(n - 8));
}

static float rf32(uint32 addr)
{
	const uint32 u = RAVE_R32(addr);
	float f;
	memcpy(&f, &u, sizeof f);
	return f;
}

/*
 *  Draw contexts
 */

/*
 *  Textures and colour tables. The guest hands out opaque TQATexture* / TQAColorTable* values;
 *  ours are small integers (index + 1). Image data is copied when the texture is created, so
 *  TextureDetach has nothing to wait for.
 */
struct RaveColorTable {
	std::vector<uint32> rgb;		/* 0x00RRGGBB entries */
	bool transparent0;
};

struct RaveTexture {
	uint32 pixel_type, flags;
	int levels;
	int w[16], h[16], row[16];
	std::vector<uint8> raw[16];		/* level data exactly as the guest gave it */
	uint32 table;				/* bound colour table handle, 0 if none */
	void *metal;
};

static std::vector<RaveTexture *> g_tex;
static std::vector<RaveColorTable *> g_tables;

static uint8 e5(uint32 v) { return (uint8)((v << 3) | (v >> 2)); }
static uint8 e4(uint32 v) { return (uint8)(v * 17); }

static bool texture_pixel_type_ok(uint32 t)
{
	return t == kQAPixel_RGB16 || t == kQAPixel_ARGB16 || t == kQAPixel_RGB32 || t == kQAPixel_ARGB32 ||
	       t == kQAPixel_CL4 || t == kQAPixel_CL8 || t == kQAPixel_RGB8_332 || t == kQAPixel_ARGB16_4444 ||
	       t == kQAPixel_ACL16_88 || t == kQAPixel_I8 || t == kQAPixel_AI16_88;
}

static bool is_cl(uint32 t) { return t == kQAPixel_CL4 || t == kQAPixel_CL8 || t == kQAPixel_ACL16_88; }

static void table_entry(const RaveColorTable *ct, uint32 idx, uint8 *out, bool want_alpha)
{
	uint32 rgb = 0;
	bool a_on = true;
	if (ct && idx < ct->rgb.size()) {
		rgb = ct->rgb[idx];
		a_on = !(ct->transparent0 && idx == 0);
	}
	out[0] = (uint8)(rgb >> 16);
	out[1] = (uint8)(rgb >> 8);
	out[2] = (uint8)rgb;
	if (want_alpha)
		out[3] = a_on ? 255 : 0;
}

/* One image level to 8-bit RGBA, top row first. */
static void to_rgba(const RaveTexture *t, int level, const RaveColorTable *ct, std::vector<uint8> &out)
{
	const int w = t->w[level], h = t->h[level], row = t->row[level];
	out.assign((size_t)w * (size_t)h * 4, 255);
	const bool flip = (t->flags & kQATexture_FlipOrigin) != 0;
	for (int y = 0; y < h; y++) {
		const uint8 *s = &t->raw[level][(size_t)(flip ? h - 1 - y : y) * (size_t)row];
		uint8 *d = &out[(size_t)y * (size_t)w * 4];
		for (int x = 0; x < w; x++, d += 4) {
			switch (t->pixel_type) {
			case kQAPixel_RGB16: case kQAPixel_ARGB16: {
				const uint32 v = ((uint32)s[x * 2] << 8) | s[x * 2 + 1];
				d[0] = e5((v >> 10) & 31); d[1] = e5((v >> 5) & 31); d[2] = e5(v & 31);
				d[3] = t->pixel_type == kQAPixel_ARGB16 ? ((v & 0x8000) ? 255 : 0) : 255;
				break;
			}
			case kQAPixel_RGB32: case kQAPixel_ARGB32:
				d[0] = s[x * 4 + 1]; d[1] = s[x * 4 + 2]; d[2] = s[x * 4 + 3];
				d[3] = t->pixel_type == kQAPixel_ARGB32 ? s[x * 4] : 255;
				break;
			case kQAPixel_CL4:
				table_entry(ct, (x & 1) ? (s[x >> 1] & 15u) : (s[x >> 1] >> 4), d, true);
				break;
			case kQAPixel_CL8:
				table_entry(ct, s[x], d, true);
				break;
			case kQAPixel_RGB8_332: {
				const uint32 v = s[x];
				d[0] = (uint8)(((v >> 5) & 7) * 255 / 7); d[1] = (uint8)(((v >> 2) & 7) * 255 / 7); d[2] = (uint8)((v & 3) * 85);
				break;
			}
			case kQAPixel_ARGB16_4444: {
				const uint32 v = ((uint32)s[x * 2] << 8) | s[x * 2 + 1];
				d[3] = e4((v >> 12) & 15); d[0] = e4((v >> 8) & 15); d[1] = e4((v >> 4) & 15); d[2] = e4(v & 15);
				break;
			}
			case kQAPixel_ACL16_88:
				table_entry(ct, s[x * 2 + 1], d, false);
				d[3] = s[x * 2];
				break;
			case kQAPixel_I8:
				d[0] = d[1] = d[2] = s[x];
				break;
			case kQAPixel_AI16_88:
				d[0] = d[1] = d[2] = s[x * 2 + 1];
				d[3] = s[x * 2];
				break;
			}
		}
	}
}

static void texture_build(RaveTexture *t)
{
	if (t->metal) {
		SheepForceRaveTexDelete(t->metal);
		t->metal = NULL;
	}
	const RaveColorTable *ct = t->table >= 1 && t->table <= g_tables.size() ? g_tables[t->table - 1] : NULL;
	if (is_cl(t->pixel_type) && !ct)
		return;			/* drawn untextured until a table is bound */
	std::vector<std::vector<uint8>> rgba((size_t)t->levels);
	std::vector<const uint8 *> ptrs((size_t)t->levels);
	for (int i = 0; i < t->levels; i++) {
		to_rgba(t, i, ct, rgba[(size_t)i]);
		ptrs[(size_t)i] = rgba[(size_t)i].data();
	}
	t->metal = SheepForceRaveTexNew(t->levels, t->w, t->h, ptrs.data());
}

struct RaveCtx {
	uint32 guest;			/* the manager's TQADrawContext */
	void *mctx;			/* Metal side */
	uint32 base;			/* device memory (guest address of pixel 0,0 of the device) */
	int row, pix, w, h;		/* device row bytes, pixel type, context size in pixels */
	int rl, rt;			/* context origin in the device */
	bool zbuf;
	bool screen;			/* the device is a display (damage must be reported) */
	uint32 clip_rgn;		/* RgnHandle in global coordinates, or 0 */
	int gl, gt;			/* the context rectangle's top-left in the clip region's coordinates */
	uint32 ints[kTagCount];
	float floats[kTagCount];
	std::vector<RaveVertex> mesh;	/* vertices from SubmitVertices* */
	std::vector<float> mesh_mt;	/* SubmitMultiTextureParams: invW, uOverW, vOverW per vertex */
};

static std::vector<RaveCtx *> g_ctx;	/* drawPrivate = index + 1 */

static RaveCtx *ctx_from_guest(uint32 guest)
{
	if (!guest)
		return NULL;
	const uint32 id = RAVE_R32(guest);
	return id >= 1 && id <= g_ctx.size() ? g_ctx[id - 1] : NULL;
}

static void ctx_defaults(RaveCtx *c)
{
	memset(c->ints, 0, sizeof c->ints);
	memset(c->floats, 0, sizeof c->floats);
	c->ints[kTag_ZBufferMask] = 1;		/* kQAZBufferMask_Enable */
	c->floats[kTag_DepthBG] = 1.0f;
	c->floats[kTag_Width] = 1.0f;
	c->floats[kTag_ZMinScale] = 1.0f;
}

static bool pixel_type_ok(uint32 t)
{
	return t == kQAPixel_RGB16 || t == kQAPixel_ARGB16 || t == kQAPixel_RGB32 || t == kQAPixel_ARGB32;
}

struct DevInfo {
	uint32 base;		/* guest address of device pixel (0,0) */
	int row, pix;		/* row bytes, kQAPixel_xxx */
	int w, h;		/* device size in pixels */
	int org_x, org_y;	/* context rectangles are in a coordinate space whose origin is this device pixel */
	bool screen;
};

/* TQADevice: { long deviceType; union { TQADeviceMemory memoryDevice; GDHandle gDevice; } } */
static bool parse_device(uint32 dev, DevInfo *d)
{
	memset(d, 0, sizeof *d);
	if (!dev)
		return false;
	const uint32 type = RAVE_R32(dev);
	if (type == kQADeviceMemory) {
		d->row = (int32)RAVE_R32(dev + 4);
		d->pix = (int)RAVE_R32(dev + 8);
		d->w = (int32)RAVE_R32(dev + 12);
		d->h = (int32)RAVE_R32(dev + 16);
		d->base = RAVE_R32(dev + 20);
	} else if (type == kQADeviceGDevice) {
		/* GDevice -> gdPMap (handle to PixMap) at +22, gdRect at +34 (top, left, bottom, right). */
		const uint32 gh = RAVE_R32(dev + 4);
		const uint32 gd = gh ? RAVE_R32(gh) : 0;
		if (!gd)
			return false;
		const uint32 pmh = RAVE_R32(gd + 22);
		const uint32 pm = pmh ? RAVE_R32(pmh) : 0;
		if (!pm)
			return false;
		d->base = RAVE_R32(pm + 0);
		d->row = (int)(RAVE_R32(pm + 4) >> 16 & 0x3fff);
		const int depth = (int)(RAVE_R32(pm + 32) >> 16);	/* pixelSize */
		d->pix = depth == 32 ? kQAPixel_RGB32 : depth == 16 ? kQAPixel_RGB16 : 0;
		const int gt = (int16)(RAVE_R32(gd + 34) >> 16), gl = (int16)RAVE_R32(gd + 34);
		const int gb = (int16)(RAVE_R32(gd + 38) >> 16), gr = (int16)RAVE_R32(gd + 38);
		d->w = gr - gl;
		d->h = gb - gt;
		d->org_x = gl;
		d->org_y = gt;
		d->screen = true;
	} else {
		return false;
	}
	return pixel_type_ok((uint32)d->pix) && d->w >= 1 && d->h >= 1 && d->w <= 8192 && d->h <= 8192 &&
	       d->base != 0 && d->row > 0;
}

static uint32 device_check(uint32 dev)
{
	DevInfo d;
	return parse_device(dev, &d) ? kQANoErr : kQANotSupported;
}

static uint32 draw_private_new(const RaveGuestCall *c)
{
	const uint32 ctxg = arg(c, 0), dev = arg(c, 1), rect = arg(c, 2), clip = arg(c, 3), flags = arg(c, 4);
	if (!ctxg || !dev || !rect)
		return kQAParamErr;
	DevInfo d;
	if (!parse_device(dev, &d))
		return kQANotSupported;
	uint32 clip_rgn = 0;
	if (clip) {
		if (RAVE_R32(clip) != 0)	/* only a Macintosh region (kQAClipRgn) */
			return kQANotSupported;
		clip_rgn = RAVE_R32(clip + 4);
	}
	if (!g_host.slot_tvect || !g_host.register_draw)
		return kQAError;
	const int left = (int32)RAVE_R32(rect + 0) - d.org_x, right = (int32)RAVE_R32(rect + 4) - d.org_x;
	const int top = (int32)RAVE_R32(rect + 8) - d.org_y, bottom = (int32)RAVE_R32(rect + 12) - d.org_y;
	if (right - left < 1 || bottom - top < 1 || left < 0 || top < 0 || right > d.w || bottom > d.h)
		return kQAParamErr;
	RaveCtx *x = new RaveCtx();
	x->guest = ctxg;
	x->row = d.row;
	x->pix = d.pix;
	x->base = d.base;
	x->screen = d.screen;
	x->rl = left;
	x->rt = top;
	x->w = right - left;
	x->h = bottom - top;
	x->zbuf = (flags & kQAContext_NoZBuffer) == 0;
	x->clip_rgn = clip_rgn;
	x->gl = (int32)RAVE_R32(rect + 0);
	x->gt = (int32)RAVE_R32(rect + 8);
	ctx_defaults(x);
	x->mctx = SheepForceRaveCtxNew(x->w, x->h, x->zbuf);
	if (!x->mctx) {
		delete x;
		return kQAOutOfMemory;
	}
	g_ctx.push_back(x);
	RAVE_W32(ctxg + 0, (uint32)g_ctx.size());	/* drawPrivate */
	RAVE_W32(ctxg + 4, 3);				/* version: kQAVersion_1_5 */
	for (uint32 tag = 0; tag < kQADrawMethodCount; tag++)
		g_host.register_draw(ctxg, tag, g_host.slot_tvect((int)(SLOT_DRAW_BASE + tag)));
	/* Which engine QuickDraw 3D really uses is worth knowing, so the first contexts are always logged. */
	static unsigned logged;
	if (rave_trace() || logged < 20) {
		logged++;
		printf("SheepForce RAVE: draw context %u created %dx%d at %d,%d pix %d z %d%s %s device %08x clip %08x\n",
		       (unsigned)g_ctx.size(), x->w, x->h, x->rl, x->rt, x->pix, (int)x->zbuf,
		       (flags & kQAContext_DeepZ) ? " deepZ" : "", x->screen ? "screen" : "memory", (unsigned)x->base,
		       (unsigned)clip_rgn);
		fflush(stdout);
	}
	return kQANoErr;
}

static void draw_private_delete(uint32 priv)
{
	if (priv >= 1 && priv <= g_ctx.size() && g_ctx[priv - 1]) {
		SheepForceRaveCtxDelete(g_ctx[priv - 1]->mctx);
		delete g_ctx[priv - 1];
		g_ctx[priv - 1] = NULL;
	}
}

static RaveBatch batch_for(const RaveCtx *c, uint32 nverts, bool textured)
{
	RaveBatch b;
	memset(&b, 0, sizeof b);
	b.count = nverts;
	const int z = c->zbuf ? (int)c->ints[kTag_ZFunction] : 0;
	b.zfunc = (uint8)z;
	b.zwrite = (z != 0 && c->ints[kTag_ZBufferMask] != 0) ? 1 : 0;
	b.blend = (uint8)c->ints[kTag_Blend];
	b.fogmode = (uint8)c->ints[kTag_FogMode];
	b.fog[0] = c->floats[kTag_FogColor_a]; b.fog[1] = c->floats[kTag_FogColor_r];
	b.fog[2] = c->floats[kTag_FogColor_g]; b.fog[3] = c->floats[kTag_FogColor_b];
	b.fog[4] = c->floats[kTag_FogStart]; b.fog[5] = c->floats[kTag_FogEnd];
	b.fog[6] = c->floats[kTag_FogDensity]; b.fog[7] = c->floats[kTag_FogMaxDepth];
	if (textured) {
		const uint32 h = c->ints[kTag_Texture];
		if (h >= 1 && h <= g_tex.size() && g_tex[h - 1] && g_tex[h - 1]->metal) {
			b.tex = g_tex[h - 1]->metal;
			b.texop = (uint8)c->ints[kTag_TextureOp];
			b.filter = (uint8)c->ints[kTag_TextureFilter];
			b.clamp = ((c->ints[kTag_TextureOp] & kQATextureOp_Shrink) ||
				   c->ints[kTagGL_TextureWrapU] == kQAGL_Clamp || c->ints[kTagGL_TextureWrapV] == kQAGL_Clamp) ? 1 : 0;
		}
	}
	if (b.tex && c->ints[kTag_MultiTextureEnable] >= 1) {
		const uint32 h2 = c->ints[kTag_MultiTexture];
		if (h2 >= 1 && h2 <= g_tex.size() && g_tex[h2 - 1] && g_tex[h2 - 1]->metal) {
			b.tex2 = g_tex[h2 - 1]->metal;
			b.mtop = (uint8)c->ints[kTag_MultiTextureOp];
			b.mtfilter = (uint8)c->ints[kTag_MultiTextureFilter];
			b.mtclamp = (c->ints[kTag_MultiTextureWrapU] == kQAGL_Clamp || c->ints[kTag_MultiTextureWrapV] == kQAGL_Clamp) ? 1 : 0;
			b.mtfactor = c->floats[kTag_MultiTextureFactor];
		}
	}
	return b;
}

static void read_gouraud(uint32 a, RaveVertex *v)
{
	memset(v, 0, sizeof *v);
	v->x = rf32(a + 0); v->y = rf32(a + 4); v->z = rf32(a + 8); v->invW = rf32(a + 12);
	v->r = rf32(a + 16); v->g = rf32(a + 20); v->b = rf32(a + 24); v->a = rf32(a + 28);
}

static void read_texture(uint32 a, RaveVertex *v)
{
	read_gouraud(a, v);
	v->uow = rf32(a + 32); v->vow = rf32(a + 36);
	v->kd[0] = rf32(a + 40); v->kd[1] = rf32(a + 44); v->kd[2] = rf32(a + 48);
	v->ks[0] = rf32(a + 52); v->ks[1] = rf32(a + 56); v->ks[2] = rf32(a + 60);
}

static void emit(RaveCtx *c, const RaveVertex *v, uint32 n, bool textured = false, bool multi = false)
{
	RaveBatch b = batch_for(c, n, textured);
	if (!multi)
		b.tex2 = NULL;		/* second-layer coordinates only come with submitted vertex arrays */
	SheepForceRaveCtxDraw(c->mctx, &b, v);
}

/* A line or point as a quad of the current width. */
static void emit_line(RaveCtx *c, const RaveVertex &a, const RaveVertex &b)
{
	float dx = b.x - a.x, dy = b.y - a.y;
	const float len = sqrtf(dx * dx + dy * dy);
	const float hw = 0.5f * (c->floats[kTag_Width] > 0.0f ? c->floats[kTag_Width] : 1.0f);
	if (len < 1e-6f) {
		dx = 1.0f;
		dy = 0.0f;
	} else {
		dx /= len;
		dy /= len;
	}
	const float nx = -dy * hw, ny = dx * hw;
	RaveVertex q[4] = { a, a, b, b };
	q[0].x += nx; q[0].y += ny;
	q[1].x -= nx; q[1].y -= ny;
	q[2].x += nx; q[2].y += ny;
	q[3].x -= nx; q[3].y -= ny;
	const RaveVertex tri[6] = { q[0], q[1], q[2], q[2], q[1], q[3] };
	emit(c, tri, 6);
}

static void emit_point(RaveCtx *c, const RaveVertex &p)
{
	const float hw = 0.5f * (c->floats[kTag_Width] > 0.0f ? c->floats[kTag_Width] : 1.0f);
	RaveVertex q[4] = { p, p, p, p };
	q[0].x -= hw; q[0].y -= hw;
	q[1].x += hw; q[1].y -= hw;
	q[2].x -= hw; q[2].y += hw;
	q[3].x += hw; q[3].y += hw;
	const RaveVertex tri[6] = { q[0], q[1], q[2], q[2], q[1], q[3] };
	emit(c, tri, 6);
}

/*
 *  The context's 2D clip is a QuickDraw region (rgnSize, rgnBBox, then per scan line: y, the
 *  inversion points, 0x7fff; a final y of 0x7fff ends it). Fill mask[w*h] with 1 where the
 *  context pixel (x, y) lies inside it.
 */
static bool region_mask(uint32 handle, int gl, int gt, int w, int h, std::vector<uint8> &mask)
{
	const uint32 rgn = handle ? RAVE_R32(handle) : 0;
	if (!rgn)
		return false;
	const unsigned size = RAVE_R16(rgn);
	if (size < 10 || size > 32768)
		return false;
	const int t = (int16)RAVE_R16(rgn + 2), l = (int16)RAVE_R16(rgn + 4);
	const int b = (int16)RAVE_R16(rgn + 6), r = (int16)RAVE_R16(rgn + 8);
	mask.assign((size_t)w * (size_t)h, 0);
	auto fill = [&](int y, const int *pts, int np) {
		if (y < gt || y >= gt + h)
			return;
		uint8 *row = &mask[(size_t)(y - gt) * (size_t)w];
		for (int i = 0; i + 1 < np; i += 2)
			for (int x = pts[i] > gl ? pts[i] : gl; x < pts[i + 1] && x < gl + w; x++)
				row[x - gl] = 1;
	};
	if (size == 10) {
		const int pts[2] = { l, r };
		for (int y = t; y < b; y++)
			fill(y, pts, 2);
		return true;
	}
	int pts[64], np = 0, cur = t;
	uint32 a = rgn + 10;
	const uint32 end = rgn + size;
	for (;;) {
		if (a + 2 > end)
			return false;
		const int y = (int16)RAVE_R16(a);
		a += 2;
		for (; cur < y && cur < b; cur++)
			fill(cur, pts, np);
		if (y == 0x7fff)
			return true;
		int line[64], nl = 0;
		for (;;) {
			if (a + 2 > end)
				return false;
			const int x = (int16)RAVE_R16(a);
			a += 2;
			if (x == 0x7fff)
				break;
			if (nl < 64)
				line[nl++] = x;
		}
		int merged[128], nm = 0, i = 0, j = 0;
		while (i < np || j < nl) {
			if (j >= nl || (i < np && pts[i] < line[j]))
				merged[nm++] = pts[i++];
			else if (i >= np || line[j] < pts[i])
				merged[nm++] = line[j++];
			else {
				i++;
				j++;
			}
		}
		np = nm > 64 ? 64 : nm;
		memcpy(pts, merged, (size_t)np * sizeof(int));
		cur = y;
	}
}

static uint32 resolve(RaveCtx *c)
{
	const size_t bpp = (c->pix == kQAPixel_RGB16 || c->pix == kQAPixel_ARGB16) ? 2u : 4u;
	uint8 *origin = RAVE_HOST(c->base) + (size_t)c->rt * (size_t)c->row + (size_t)c->rl * bpp;
	if (g_host.before_write)
		g_host.before_write();
	std::vector<uint8> mask;
	const bool clipped = c->clip_rgn && region_mask(c->clip_rgn, c->gl, c->gt, c->w, c->h, mask);
	if (!SheepForceRaveCtxResolve(c->mctx, origin, c->row, c->pix, 0, 0, c->w, c->h, clipped ? mask.data() : NULL))
		return kQAError;
	if (c->screen && g_host.after_write)
		g_host.after_write(c->rl, c->rt, c->w, c->h);
	return kQANoErr;
}

/*
 *  Draw methods. r3 is the draw context; floats arrive in f1.
 */

static void write_rect(uint32 addr, int l, int r, int t, int b)
{
	if (!addr)
		return;
	RAVE_W32(addr + 0, (uint32)l);
	RAVE_W32(addr + 4, (uint32)r);
	RAVE_W32(addr + 8, (uint32)t);
	RAVE_W32(addr + 12, (uint32)b);
}

static void draw_method(uint32 tag, const RaveGuestCall *call, RaveGuestResult *res)
{
	RaveCtx *c = ctx_from_guest(arg(call, 0));
	if (!c) {
		res->r3 = kQAParamErr;
		return;
	}
	switch (tag) {
	case kQASetFloat: {
		const uint32 t = arg(call, 1);
		if (t < kTagCount)
			c->floats[t] = (float)call->f[0];
		break;
	}
	case kQASetInt:
	case kQASetPtr: {
		const uint32 t = arg(call, 1);
		if (t < kTagCount)
			c->ints[t] = arg(call, 2);
		break;
	}
	case kQAGetFloat: {
		const uint32 t = arg(call, 1);
		res->f1 = t < kTagCount ? c->floats[t] : 0.0f;
		res->is_float = true;
		break;
	}
	case kQAGetInt:
	case kQAGetPtr: {
		const uint32 t = arg(call, 1);
		res->r3 = t < kTagCount ? c->ints[t] : 0;
		break;
	}
	case kQADrawPoint: {
		RaveVertex v;
		read_gouraud(arg(call, 1), &v);
		emit_point(c, v);
		break;
	}
	case kQADrawLine: {
		RaveVertex a, b;
		read_gouraud(arg(call, 1), &a);
		read_gouraud(arg(call, 2), &b);
		emit_line(c, a, b);
		break;
	}
	case kQADrawTriGouraud: {
		RaveVertex v[3];
		for (int i = 0; i < 3; i++)
			read_gouraud(arg(call, 1 + i), &v[i]);
		emit(c, v, 3);
		break;
	}
	case kQADrawVGouraud: {
		const uint32 n = arg(call, 1), mode = arg(call, 2), verts = arg(call, 3);
		if (n > 65536 || !verts)
			break;
		std::vector<RaveVertex> v(n);
		for (uint32 i = 0; i < n; i++)
			read_gouraud(verts + i * 32, &v[i]);
		std::vector<RaveVertex> out;
		switch (mode) {
		case kQAVertexMode_Point:
			for (uint32 i = 0; i < n; i++)
				emit_point(c, v[i]);
			break;
		case kQAVertexMode_Line:
			for (uint32 i = 0; i + 1 < n; i += 2)
				emit_line(c, v[i], v[i + 1]);
			break;
		case kQAVertexMode_Polyline:
			for (uint32 i = 0; i + 1 < n; i++)
				emit_line(c, v[i], v[i + 1]);
			break;
		case kQAVertexMode_Tri:
			for (uint32 i = 0; i + 2 < n; i += 3) {
				out.push_back(v[i]); out.push_back(v[i + 1]); out.push_back(v[i + 2]);
			}
			break;
		case kQAVertexMode_Strip:
			for (uint32 i = 0; i + 2 < n; i++) {
				const bool odd = (i & 1) != 0;
				out.push_back(v[i]); out.push_back(v[odd ? i + 2 : i + 1]); out.push_back(v[odd ? i + 1 : i + 2]);
			}
			break;
		case kQAVertexMode_Fan:
			for (uint32 i = 1; i + 1 < n; i++) {
				out.push_back(v[0]); out.push_back(v[i]); out.push_back(v[i + 1]);
			}
			break;
		}
		if (!out.empty())
			emit(c, out.data(), (uint32)out.size());
		break;
	}
	case kQASubmitVerticesGouraud: {
		const uint32 n = arg(call, 1), verts = arg(call, 2);
		c->mesh.clear();
		if (n > 1000000 || !verts)
			break;
		c->mesh.resize(n);
		for (uint32 i = 0; i < n; i++)
			read_gouraud(verts + i * 32, &c->mesh[i]);
		break;
	}
	case kQADrawTriMeshGouraud: {
		const uint32 n = arg(call, 1), tris = arg(call, 2);
		if (n > 1000000 || !tris)
			break;
		std::vector<RaveVertex> out;
		out.reserve((size_t)n * 3);
		for (uint32 i = 0; i < n; i++) {
			const uint32 t = tris + i * 16;
			bool ok = true;
			for (int k = 0; k < 3; k++) {
				const uint32 idx = RAVE_R32(t + 4 + 4 * (uint32)k);
				if (idx >= c->mesh.size()) { ok = false; break; }
			}
			if (!ok)
				continue;
			for (int k = 0; k < 3; k++)
				out.push_back(c->mesh[RAVE_R32(t + 4 + 4 * (uint32)k)]);
		}
		if (!out.empty())
			emit(c, out.data(), (uint32)out.size());
		break;
	}
	case kQARenderStart: {
		/* Start of a frame: clear colour and depth to the background tags. */
		const float rgba[4] = { c->floats[kTag_ColorBG_a], c->floats[kTag_ColorBG_r],
					c->floats[kTag_ColorBG_g], c->floats[kTag_ColorBG_b] };
		SheepForceRaveCtxClear(c->mctx, rgba, c->zbuf, c->floats[kTag_DepthBG], (int)c->ints[kTag_Antialias]);
		break;
	}
	case kQARenderEnd:
		res->r3 = resolve(c);
		write_rect(arg(call, 1), 0, c->w, 0, c->h);
		break;
	case kQARenderAbort:
		res->r3 = kQANoErr;
		break;
	case kQAFlush:
		res->r3 = kQANoErr;	/* drawing is queued until the frame ends */
		break;
	case kQASync:
		res->r3 = resolve(c);
		break;
	case kQABusy:
		res->r3 = 0;
		break;
	case kQASwapBuffers:
		res->r3 = kQANoErr;
		break;
	case kQADrawTriTexture: {
		RaveVertex v[3];
		for (int i = 0; i < 3; i++)
			read_texture(arg(call, 1 + i), &v[i]);
		emit(c, v, 3, true);
		break;
	}
	case kQADrawVTexture: {
		const uint32 n = arg(call, 1), mode = arg(call, 2), verts = arg(call, 3);
		if (n > 65536 || !verts || mode < kQAVertexMode_Tri || mode > kQAVertexMode_Fan)
			break;
		std::vector<RaveVertex> v(n), out;
		for (uint32 i = 0; i < n; i++)
			read_texture(verts + i * 64, &v[i]);
		if (mode == kQAVertexMode_Tri) {
			for (uint32 i = 0; i + 2 < n; i += 3) { out.push_back(v[i]); out.push_back(v[i + 1]); out.push_back(v[i + 2]); }
		} else if (mode == kQAVertexMode_Strip) {
			for (uint32 i = 0; i + 2 < n; i++) {
				const bool odd = (i & 1) != 0;
				out.push_back(v[i]); out.push_back(v[odd ? i + 2 : i + 1]); out.push_back(v[odd ? i + 1 : i + 2]);
			}
		} else {
			for (uint32 i = 1; i + 1 < n; i++) { out.push_back(v[0]); out.push_back(v[i]); out.push_back(v[i + 1]); }
		}
		if (!out.empty())
			emit(c, out.data(), (uint32)out.size(), true);
		break;
	}
	case kQASubmitVerticesTexture: {
		const uint32 n = arg(call, 1), verts = arg(call, 2);
		c->mesh.clear();
		if (n > 1000000 || !verts)
			break;
		c->mesh.resize(n);
		for (uint32 i = 0; i < n; i++)
			read_texture(verts + i * 64, &c->mesh[i]);
		break;
	}
	case kQADrawTriMeshTexture: {
		const uint32 n = arg(call, 1), tris = arg(call, 2);
		if (n > 1000000 || !tris)
			break;
		std::vector<RaveVertex> out;
		out.reserve((size_t)n * 3);
		for (uint32 i = 0; i < n; i++) {
			const uint32 t = tris + i * 16;
			const uint32 i0 = RAVE_R32(t + 4), i1 = RAVE_R32(t + 8), i2 = RAVE_R32(t + 12);
			if (i0 >= c->mesh.size() || i1 >= c->mesh.size() || i2 >= c->mesh.size())
				continue;
			const uint32 id3[3] = { i0, i1, i2 };
			for (int k = 0; k < 3; k++) {
				RaveVertex v = c->mesh[id3[k]];
				if (id3[k] * 3 + 2 < c->mesh_mt.size()) {
					v.invW2 = c->mesh_mt[id3[k] * 3 + 0];
					v.uow2 = c->mesh_mt[id3[k] * 3 + 1];
					v.vow2 = c->mesh_mt[id3[k] * 3 + 2];
				}
				out.push_back(v);
			}
		}
		if (!out.empty())
			emit(c, out.data(), (uint32)out.size(), true, true);
		break;
	}
	case kQASubmitMultiTextureParams: {
		/* params[i] belongs to vertex i of the last SubmitVerticesTexture: invW, uOverW, vOverW. */
		const uint32 n = arg(call, 1), params = arg(call, 2);
		c->mesh_mt.clear();
		if (n > 1000000 || !params)
			break;
		c->mesh_mt.resize((size_t)n * 3);
		for (uint32 i = 0; i < n * 3; i++)
			c->mesh_mt[i] = rf32(params + i * 4);
		break;
	}
	case kQADrawBitmap:
		/* Not advertised in the gestalt, so a conforming client does not call this. */
		break;
	default:
		res->r3 = kQANotSupported;
		break;
	}
}

/*
 *  Engine methods
 */

static uint32 color_table_new(const RaveGuestCall *c)
{
	const uint32 type = arg(c, 0), data = arg(c, 1), transparent = arg(c, 2), out = arg(c, 3);
	const uint32 n = type == 0 ? 256 : type == 1 ? 16 : 0;	/* kQAColorTable_CL8_RGB32, _CL4_RGB32 */
	if (!n || !data || !out)
		return kQAParamErr;
	RaveColorTable *t = new RaveColorTable();
	for (uint32 i = 0; i < n; i++)
		t->rgb.push_back(RAVE_R32(data + i * 4) & 0xffffff);
	t->transparent0 = transparent != 0;
	g_tables.push_back(t);
	RAVE_W32(out, (uint32)g_tables.size());
	return kQANoErr;
}

static uint32 texture_new(const RaveGuestCall *c)
{
	const uint32 flags = arg(c, 0), type = arg(c, 1), images = arg(c, 2), out = arg(c, 3);
	if (!images || !out || !texture_pixel_type_ok(type))
		return kQANotSupported;
	int w = (int32)RAVE_R32(images + 0), h = (int32)RAVE_R32(images + 4);
	if (w < 1 || h < 1 || w > 4096 || h > 4096)
		return kQAParamErr;
	int levels = 1;
	if (flags & kQATexture_Mipmap)
		for (int ww = w, hh = h; ww > 1 || hh > 1; levels++) {
			ww = ww > 1 ? ww / 2 : 1;
			hh = hh > 1 ? hh / 2 : 1;
		}
	if (levels > 16)
		return kQAParamErr;
	RaveTexture *t = new RaveTexture();
	t->pixel_type = type;
	t->flags = flags;
	t->levels = levels;
	t->table = 0;
	t->metal = NULL;
	const int bits = type == kQAPixel_CL4 ? 4 : (type == kQAPixel_CL8 || type == kQAPixel_RGB8_332 || type == kQAPixel_I8) ? 8
		       : (type == kQAPixel_RGB32 || type == kQAPixel_ARGB32) ? 32 : 16;
	for (int i = 0; i < levels; i++) {
		const uint32 im = images + (uint32)i * 16;
		t->w[i] = (int32)RAVE_R32(im + 0);
		t->h[i] = (int32)RAVE_R32(im + 4);
		t->row[i] = (int32)RAVE_R32(im + 8);
		const uint32 px = RAVE_R32(im + 12);
		const size_t need = (size_t)t->row[i] * (size_t)t->h[i];
		if (t->w[i] < 1 || t->h[i] < 1 || !px || t->row[i] < (t->w[i] * bits + 7) / 8 || need > (64u << 20)) {
			delete t;
			return kQAParamErr;
		}
		t->raw[i].assign(RAVE_HOST(px), RAVE_HOST(px) + need);
	}
	g_tex.push_back(t);
	texture_build(t);
	RAVE_W32(out, (uint32)g_tex.size());
	return kQANoErr;
}

static uint32 engine_gestalt(uint32 selector, uint32 response)
{
	if (!response)
		return kQAParamErr;
	uint32 v = 0;
	switch (selector) {
	case kQAGestalt_OptionalFeatures:
		/* DeepZ, Texture, TextureHQ (trilinear), TextureColor, Blend, BlendAlpha, CL4, CL8, NoDither,
		 * FogDepth: what the Metal path really does. */
		v = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 5) | (1u << 13) | (1u << 14) |
		    (1u << 16) | (1u << 18) | (1u << 6) | (1u << 19);
		break;
	case kQAGestalt_OptionalFeatures2:
		v = 0;
		break;
	case kQAGestalt_FastFeatures:
		v = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 4) | (1u << 5) | (1u << 10) | (1u << 11);	/* Line, Gouraud, Texture, Blend, Antialiasing, FogDepth, MultiTextures */
		break;
	case kQAGestalt_VendorID:
		v = 0x5348464f;		/* 'SHFO' */
		break;
	case kQAGestalt_EngineID:
	case kQAGestalt_Revision:
		v = 1;
		break;
	case kQAGestalt_ASCIINameLength:
		v = (uint32)strlen(kEngineName);
		break;
	case kQAGestalt_ASCIIName:
		for (size_t i = 0; i <= strlen(kEngineName); i++)
			RAVE_W8(response + (uint32)i, (uint8)kEngineName[i]);
		return kQANoErr;
	case kQAGestalt_TextureMemory:
	case kQAGestalt_FastTextureMemory:
		v = 64u << 20;
		break;
	case kQAGestalt_MultiTextureMax:
		v = 2;			/* the primary layer and one more */
		break;
	case kQAGestalt_BitmapPixelTypesAllowed:
	case kQAGestalt_BitmapPixelTypesPreferred:
		v = 0;
		break;
	case kQAGestalt_TexturePixelTypesAllowed:
		v = (1u << kQAPixel_RGB16) | (1u << kQAPixel_ARGB16) | (1u << kQAPixel_RGB32) | (1u << kQAPixel_ARGB32) |
		    (1u << kQAPixel_CL4) | (1u << kQAPixel_CL8) | (1u << kQAPixel_RGB8_332) | (1u << kQAPixel_ARGB16_4444) |
		    (1u << kQAPixel_ACL16_88) | (1u << kQAPixel_I8) | (1u << kQAPixel_AI16_88);
		break;
	case kQAGestalt_TexturePixelTypesPreferred:
		v = (1u << kQAPixel_RGB32) | (1u << kQAPixel_ARGB32);
		break;
	case kQAGestalt_DrawContextPixelTypesAllowed:
		v = (1u << kQAPixel_RGB16) | (1u << kQAPixel_ARGB16) | (1u << kQAPixel_RGB32) | (1u << kQAPixel_ARGB32);
		break;
	case kQAGestalt_DrawContextPixelTypesPreferred:
		v = (1u << kQAPixel_RGB32) | (1u << kQAPixel_ARGB32);
		break;
	default:
		return kQAGestaltUnknown;
	}
	RAVE_W32(response, v);
	return kQANoErr;
}

static uint32 engine_method(uint32 tag, const RaveGuestCall *c)
{
	if (rave_trace())
		printf("SheepForce RAVE: engine %s(%08x, %08x, %08x)\n",
		       tag < kQAEngineMethodCount ? engine_method_name[tag] : "?",
		       (unsigned)arg(c, 0), (unsigned)arg(c, 1), (unsigned)arg(c, 2));
	switch (tag) {
	case kQAEngineGestalt:
		return engine_gestalt(arg(c, 0), arg(c, 1));
	case kQAEngineCheckDevice:
		return device_check(arg(c, 0));
	case kQADrawPrivateNew:
		return draw_private_new(c);
	case kQADrawPrivateDelete:
		draw_private_delete(arg(c, 0));
		return kQANoErr;
	case kQATextureNew:
		return texture_new(c);
	case kQATextureDetach:
		return kQANoErr;
	case kQATextureDelete: {
		const uint32 h = arg(c, 0);
		if (h >= 1 && h <= g_tex.size() && g_tex[h - 1]) {
			if (g_tex[h - 1]->metal)
				SheepForceRaveTexDelete(g_tex[h - 1]->metal);
			delete g_tex[h - 1];
			g_tex[h - 1] = NULL;
		}
		return kQANoErr;
	}
	case kQAColorTableNew:
		return color_table_new(c);
	case kQAColorTableDelete: {
		const uint32 h = arg(c, 0);
		if (h >= 1 && h <= g_tables.size()) {
			delete g_tables[h - 1];
			g_tables[h - 1] = NULL;
		}
		return kQANoErr;
	}
	case kQATextureBindColorTable: {
		const uint32 th = arg(c, 0), ch = arg(c, 1);
		if (th < 1 || th > g_tex.size() || !g_tex[th - 1])
			return kQAParamErr;
		g_tex[th - 1]->table = ch;
		texture_build(g_tex[th - 1]);
		return kQANoErr;
	}
	case kQABitmapDelete:
		return kQANoErr;
	default:
		return kQANotSupported;
	}
}

/*
 *  engineGetMethod(tag, TQAEngineMethod *method): hand the manager the guest TVECT of
 *  the requested engine method.
 */
static uint32 engine_get_method(const RaveGuestCall *c)
{
	static uint32 tvect[kQAEngineMethodCount];
	const uint32 tag = arg(c, 0), out = arg(c, 1);
	if (rave_trace())
		printf("SheepForce RAVE: getMethod(%u -> %s)\n", (unsigned)tag,
		       tag < kQAEngineMethodCount ? engine_method_name[tag] : "?");
	if (tag >= kQAEngineMethodCount || !out || !g_host.slot_tvect)
		return kQAParamErr;
	if (!tvect[tag])
		tvect[tag] = g_host.slot_tvect((int)(SLOT_ENGINE_BASE + tag));
	if (!tvect[tag])
		return kQAOutOfMemory;
	RAVE_W32(out, tvect[tag]);
	return kQANoErr;
}

/* Entry from the native op (kpx_cpu/sheepshaver_glue.cpp): r0 = slot. */
void SheepForceRaveMethod(uint32 slot, const RaveGuestCall *c, RaveGuestResult *res)
{
	res->r3 = kQANoErr;
	res->f1 = 0;
	res->is_float = false;
	if (slot == SLOT_GETMETHOD)
		res->r3 = engine_get_method(c);
	else if (slot < SLOT_DRAW_BASE)
		res->r3 = engine_method(slot - SLOT_ENGINE_BASE, c);
	else if (slot < SLOT_DRAW_BASE + kQADrawMethodCount)
		draw_method(slot - SLOT_DRAW_BASE, c, res);
	else
		res->r3 = kQAParamErr;
}

#ifndef SHEEPFORCE_RAVE_HARNESS
/*
 *  Emulator glue: the host hooks, and registration with the guest's manager. Called inside
 *  a native op once the manager's QARegisterEngine has been found, so that calling back
 *  into guest code is safe.
 */
static uint32 g_register_draw_tvect;

static uint32 host_slot_tvect(int slot)
{
	return NativeSlotTVECT(NATIVE_RAVE_METHOD, slot);
}

static uint32 host_register_draw(uint32 ctx, uint32 tag, uint32 method)
{
	typedef int32 (*fn3)(uint32, uint32, uint32);
	if (!g_register_draw_tvect)
		return kQAError;
	return (uint32)CallMacOS3(fn3, g_register_draw_tvect, ctx, tag, method);
}

static void host_before_write(void)
{
	SheepForceSync();
}

static void host_after_write(int x, int y, int w, int h)
{
	nw_fb_damage_rect(x, y, w, h);
	video_set_dirty_area(x, y, w, h);
	SheepForceMarkDirty();
}

void SheepForceRaveRegisterEngine(uint32 register_engine_tvect, uint32 register_draw_method_tvect)
{
	typedef uint32 (*fn1)(uint32);
	if (g_registered || !register_engine_tvect || !register_draw_method_tvect)
		return;
	g_registered = true;
	g_register_draw_tvect = register_draw_method_tvect;
	RaveHost host = { host_slot_tvect, host_register_draw, host_before_write, host_after_write };
	SheepForceRaveSetHost(&host);
	const uint32 get_method = host_slot_tvect(SLOT_GETMETHOD);
	if (!get_method) {
		printf("SheepForce: RAVE engine: no memory for the getMethod stub\n");
		return;
	}
	const int32 err = (int32)CallMacOS1(fn1, register_engine_tvect, get_method);
	printf("SheepForce: RAVE engine \"%s\" registered with the manager (QARegisterEngine -> %d)\n",
	       kEngineName, (int)err);
	fflush(stdout);
}

/*
 *  Self-test through the real manager and real PowerPC calling conventions: create a draw
 *  context on a memory device with our engine via QADrawContextNew, then draw through the
 *  method table the manager hands out. Floats (QASetFloat) go through a tiny PowerPC stub
 *  that loads f1 and tail-calls the method, which is what a compiled client does.
 *  Prints "SheepForce RAVE selftest: PASS" or what failed. Run with the probe pref.
 */
static uint32 f_bits(float f)
{
	uint32 u;
	memcpy(&u, &f, sizeof u);
	return u;
}

bool SheepForceRaveSelfTest(void)
{
	static const char lib[] = "\x19QuickDraw\xaa 3D Accelerator";
	typedef uint32 (*fn1)(uint32);
	typedef uint32 (*fn2)(uint32, uint32);
	typedef uint32 (*fn3)(uint32, uint32, uint32);
	typedef uint32 (*fn4)(uint32, uint32, uint32, uint32);
	typedef uint32 (*fn5)(uint32, uint32, uint32, uint32, uint32);
	typedef uint32 (*fn6)(uint32, uint32, uint32, uint32, uint32, uint32);
	char s1[64], s2[64], s3[64], s4[64];
	auto pstr = [](char *dst, const char *src) { dst[0] = (char)strlen(src); memcpy(dst + 1, src, strlen(src) + 1); };
	pstr(s1, "QADeviceGetFirstEngine");
	pstr(s2, "QADeviceGetNextEngine");
	pstr(s3, "QAEngineGestalt");
	pstr(s4, "QADrawContextNew");
	const uint32 first = FindLibSymbol(lib, s1), next = FindLibSymbol(lib, s2);
	const uint32 gestalt = FindLibSymbol(lib, s3), ctx_new = FindLibSymbol(lib, s4);
	pstr(s1, "QADrawContextDelete");
	const uint32 ctx_delete = FindLibSymbol(lib, s1);
	if (!first || !next || !gestalt || !ctx_new) {
		printf("SheepForce RAVE selftest: manager entry points missing (%08x %08x %08x %08x)\n",
		       (unsigned)first, (unsigned)next, (unsigned)gestalt, (unsigned)ctx_new);
		return false;
	}
	const int W = 64, H = 48;
	const uint32 buf = Mac_sysalloc((uint32)(W * H * 4));
	const uint32 dev = Mac_sysalloc(32), rect = Mac_sysalloc(16), scratch = Mac_sysalloc(1024);
	if (!buf || !dev || !rect || !scratch) {
		printf("SheepForce RAVE selftest: no guest memory\n");
		return false;
	}
	for (int i = 0; i < W * H; i++)
		RAVE_W32(buf + (uint32)i * 4, 0x55555555);
	for (int i = 0; i < 24; i += 4)
		RAVE_W32(dev + (uint32)i, 0);
	RAVE_W32(dev + 4, (uint32)(W * 4));
	RAVE_W32(dev + 8, kQAPixel_ARGB32);
	RAVE_W32(dev + 12, (uint32)W);
	RAVE_W32(dev + 16, (uint32)H);
	RAVE_W32(dev + 20, buf);
	RAVE_W32(rect + 0, 0); RAVE_W32(rect + 4, (uint32)W); RAVE_W32(rect + 8, 0); RAVE_W32(rect + 12, (uint32)H);

	/* find our engine among those that accept the device */
	uint32 eng = CallMacOS1(fn1, first, dev), ours = 0;
	for (int n = 0; eng && n < 8 && !ours; n++) {
		RAVE_W32(scratch, 0);
		if (CallMacOS3(fn3, gestalt, eng, kQAGestalt_ASCIIName, scratch + 64) == 0 &&
		    !strcmp((const char *)RAVE_HOST(scratch + 64), kEngineName))
			ours = eng;
		else
			eng = CallMacOS2(fn2, next, dev, eng);
	}
	if (!ours) {
		printf("SheepForce RAVE selftest: our engine is not offered for a memory device\n");
		return false;
	}
	const uint32 out_ctx = scratch + 8;
	RAVE_W32(out_ctx, 0);
	const uint32 err = CallMacOS6(fn6, ctx_new, dev, rect, 0, ours, 0, out_ctx);
	const uint32 ctx = RAVE_R32(out_ctx);
	if (err != 0 || !ctx) {
		printf("SheepForce RAVE selftest: QADrawContextNew -> %d, context %08x\n", (int)err, (unsigned)ctx);
		return false;
	}
	const uint32 set_float = RAVE_R32(ctx + 8), set_int = RAVE_R32(ctx + 12), draw_tri = RAVE_R32(ctx + 40);
	const uint32 render_start = RAVE_R32(ctx + 60), render_end = RAVE_R32(ctx + 64);

	/* PowerPC stub: lfs f1,0(r5); lwz r12,0(r6); lwz r2,4(r6); mtctr r12; bctr */
	const uint32 stub = Mac_sysalloc(32), stub_tv = Mac_sysalloc(8);
	RAVE_W32(stub + 0, 0xC0250000); RAVE_W32(stub + 4, 0x81860000); RAVE_W32(stub + 8, 0x80460004);
	RAVE_W32(stub + 12, 0x7D8903A6); RAVE_W32(stub + 16, 0x4E800420);
	RAVE_W32(stub_tv + 0, stub); RAVE_W32(stub_tv + 4, 0);
	const uint32 fv = scratch + 16;
	auto setf = [&](uint32 tag, float v) {
		RAVE_W32(fv, f_bits(v));
		CallMacOS4(fn4, stub_tv, ctx, tag, fv, set_float);
	};
	auto vert = [&](uint32 at, float x, float y, float r, float g, float b) {
		const float v[8] = { x, y, 0.5f, 1.0f, r, g, b, 1.0f };
		for (int i = 0; i < 8; i++)
			RAVE_W32(at + (uint32)i * 4, f_bits(v[i]));
	};
	setf(1, 1.0f); setf(2, 0.0f); setf(3, 0.0f); setf(4, 1.0f);	/* background: opaque blue */
	CallMacOS3(fn3, render_start, ctx, 0, 0);
	const uint32 v0 = scratch + 64, v1 = scratch + 96, v2 = scratch + 128;
	vert(v0, 8, 8, 1, 0, 0); vert(v1, 56, 8, 1, 0, 0); vert(v2, 8, 40, 1, 0, 0);
	CallMacOS5(fn5, draw_tri, ctx, v0, v1, v2, 0);
	CallMacOS2(fn2, render_end, ctx, 0);
	(void)set_int;
	auto px = [&](int x, int y) { return RAVE_R32(buf + (uint32)(y * W + x) * 4); };
	const uint32 in = px(16, 16), outside = px(50, 40);
	bool pass = in == 0xffff0000u && outside == 0xff0000ffu;
	printf("SheepForce RAVE selftest: %s (inside %08x expect ffff0000, outside %08x expect ff0000ff)\n",
	       pass ? "PASS" : "FAIL", (unsigned)in, (unsigned)outside);

	/* Textures through the manager: QATextureNew(engine, flags, type, images, &texture), then a
	 * textured quad (QASetPtr/QASetInt/QADrawTriTexture) with a 2x2 ARGB32 texture. */
	pstr(s1, "QATextureNew");
	const uint32 tex_new = FindLibSymbol(lib, s1);
	const uint32 set_ptr = RAVE_R32(ctx + 16), draw_tri_tex = RAVE_R32(ctx + 44);
	if (tex_new) {
		const uint32 texpx = scratch + 192, img = scratch + 224, tex_out = scratch + 256;
		const uint32 texels[4] = { 0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffffff };	/* red green / blue white */
		for (int i = 0; i < 4; i++)
			RAVE_W32(texpx + (uint32)i * 4, texels[i]);
		RAVE_W32(img + 0, 2); RAVE_W32(img + 4, 2); RAVE_W32(img + 8, 8); RAVE_W32(img + 12, texpx);
		RAVE_W32(tex_out, 0);
		const uint32 terr = CallMacOS5(fn5, tex_new, ours, 0, kQAPixel_ARGB32, img, tex_out) == 0 ? 0 : 1;
		(void)terr;
		/* the manager's QATextureNew has six parameters: engine, flags, type, images, &texture */
		const uint32 tex = RAVE_R32(tex_out);
		if (tex) {
			CallMacOS3(fn3, set_ptr, ctx, 13, tex);	/* kQATag_Texture */
			CallMacOS3(fn3, set_int, ctx, 12, 0);		/* kQATag_TextureOp = none */
			CallMacOS3(fn3, set_int, ctx, 11, 0);		/* kQATag_TextureFilter = nearest */
			CallMacOS3(fn3, render_start, ctx, 0, 0);
			const float quad[4][4] = { {0, 0, 0, 0}, {64, 0, 1, 0}, {0, 48, 0, 1}, {64, 48, 1, 1} };
			uint32 tv[4];
			for (int i = 0; i < 4; i++) {
				tv[i] = scratch + 288 + (uint32)i * 64;
				const float f[16] = { quad[i][0], quad[i][1], 0.5f, 1.0f, 1, 1, 1, 1, quad[i][2], quad[i][3], 1, 1, 1, 0, 0, 0 };
				for (int k = 0; k < 16; k++)
					RAVE_W32(tv[i] + (uint32)k * 4, f_bits(f[k]));
			}
			CallMacOS5(fn5, draw_tri_tex, ctx, tv[0], tv[1], tv[2], 0);
			CallMacOS5(fn5, draw_tri_tex, ctx, tv[1], tv[3], tv[2], 0);
			CallMacOS2(fn2, render_end, ctx, 0);
			const uint32 tl = px(16, 12), br = px(48, 36);
			const bool tpass = (tl & 0xffffff) == 0xff0000 && (br & 0xffffff) == 0xffffff;
			printf("SheepForce RAVE selftest: texture %s (top-left %08x expect red, bottom-right %08x expect white)\n",
			       tpass ? "PASS" : "FAIL", (unsigned)tl, (unsigned)br);
			pass = pass && tpass;
		} else {
			printf("SheepForce RAVE selftest: texture FAIL (QATextureNew gave no texture)\n");
			pass = false;
		}
	}
	if (ctx_delete)
		CallMacOS1(fn1, ctx_delete, ctx);
	Mac_sysfree(buf);
	Mac_sysfree(dev);
	Mac_sysfree(rect);
	Mac_sysfree(scratch);
	Mac_sysfree(stub);
	Mac_sysfree(stub_tv);
	fflush(stdout);
	return pass;
}

/* Take our engine out of the manager's selection (QAEngineDisable(vendor, engineID)). */
void SheepForceRaveDisableEngine(void)
{
	static const char lib[] = "\x19QuickDraw\xaa 3D Accelerator";
	static const char sym[] = "\x0fQAEngineDisable";
	typedef uint32 (*fn2)(uint32, uint32);
	const uint32 disable = FindLibSymbol(lib, sym);
	if (!disable)
		return;
	const uint32 err = CallMacOS2(fn2, disable, 0x5348464f, 1);
	printf("SheepForce RAVE: engine disabled (QAEngineDisable -> %d)\n", (int)err);
	fflush(stdout);
}
#endif
