/*
 *  sheepforce_rave_harness.mm - the SheepForce RAVE engine against a fake guest
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
 * Drives rave.cpp exactly as the guest's RAVE manager and a PowerPC client would: through
 * SheepForceRaveMethod(slot, registers), with big-endian guest memory, float arguments in
 * "f1", and the draw-context methods registered by drawPrivateNew. Pixels are then read back
 * from the guest pixmap and checked. Built and run by tools/run_sheepforce_tests.py.
 */
#include "sysdeps.h"
#include "sheepforce.h"
#include "rave_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/mman.h>
#include <map>
#include <vector>

/* ---- fake guest memory ---------------------------------------------------------------- */
static uint8 *g_mem;
static const size_t kMem = 32u << 20;
uint32 rave_h_read16(uint32 a) { return ((uint32)g_mem[a] << 8) | g_mem[a + 1]; }
uint32 rave_h_read32(uint32 a) { const uint8 *p = g_mem + a; return ((uint32)p[0] << 24) | ((uint32)p[1] << 16) | ((uint32)p[2] << 8) | p[3]; }
void rave_h_write32(uint32 a, uint32 v) { uint8 *p = g_mem + a; p[0] = (uint8)(v >> 24); p[1] = (uint8)(v >> 16); p[2] = (uint8)(v >> 8); p[3] = (uint8)v; }
void rave_h_write8(uint32 a, uint8 v) { g_mem[a] = v; }
uint8 *rave_h_host(uint32 a) { return g_mem + a; }

/* Symbols the emulator normally provides. */
bool PrefsFindBool(const char *name) { return !strcmp(name, "sheepforce") || !strcmp(name, "sheepforce_qd"); }
rgb_color mac_pal[256];
rgb_color mac_gamma[256];
uint32_t nw_la_ram_base, nw_la_ram_size, nw_la_rom_base, nw_la_kdp_pa;

static uint32 g_brk = 0x10000;
static uint32 galloc(uint32 n) { uint32 a = (g_brk + 15u) & ~15u; g_brk = a + n; return a; }

/* ---- fake RAVE manager ---------------------------------------------------------------- */
static std::map<uint32, uint32> g_methods;	/* (ctx<<8 | tag) -> slot */
static uint32 host_slot_tvect(int slot) { return 0x1000u + (uint32)slot * 16u; }
static uint32 host_register_draw(uint32 ctx, uint32 tag, uint32 method)
{
	g_methods[(ctx << 8) | tag] = (method - 0x1000u) / 16u;
	return 0;
}

static float f32(float v) { return v; }

/* Call a method like a PowerPC caller: integer args in r3.., floats in f1.. */
struct Args {
	RaveGuestCall c;
	Args() { memset(&c, 0, sizeof c); c.sp = 0x8000; }
	Args &r(int i, uint32 v) { c.r[i] = v; return *this; }
	Args &f(int i, double v) { c.f[i] = v; return *this; }
};
static RaveGuestResult call(uint32 slot, const Args &a)
{
	RaveGuestResult res;
	memset(&res, 0, sizeof res);
	SheepForceRaveMethod(slot, &a.c, &res);
	return res;
}

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static void put_float(uint32 a, float v) { uint32 u; memcpy(&u, &v, 4); rave_h_write32(a, u); }

/* TQAVGouraud at guest address a */
static uint32 vertex(float x, float y, float z, float invw, float r, float g, float b, float al)
{
	uint32 a = galloc(32);
	put_float(a + 0, x); put_float(a + 4, y); put_float(a + 8, z); put_float(a + 12, invw);
	put_float(a + 16, r); put_float(a + 20, g); put_float(a + 24, b); put_float(a + 28, al);
	return a;
}

struct Pix { int a, r, g, b; };
static Pix px32(uint32 base, int row, int x, int y, bool argb)
{
	const uint8 *p = g_mem + base + (size_t)y * row + (size_t)x * 4;
	Pix q = { argb ? p[0] : -1, p[1], p[2], p[3] };
	return q;
}
static bool near(int a, int b, int tol) { return abs(a - b) <= tol; }
static bool is_rgb(const Pix &p, int r, int g, int b, int tol = 2) { return near(p.r, r, tol) && near(p.g, g, tol) && near(p.b, b, tol); }

struct Ctx {
	uint32 guest, base, row;
	int w, h, pix;
};

static Ctx make_ctx(int w, int h, int pix, uint32 flags, uint32 origin_x = 0, uint32 origin_y = 0, int devw = 0, int devh = 0)
{
	Ctx c;
	if (!devw) devw = w + (int)origin_x;
	if (!devh) devh = h + (int)origin_y;
	const int bpp = (pix == 1 || pix == 2) ? 2 : 4;
	c.w = w; c.h = h; c.pix = pix;
	c.row = (uint32)(devw * bpp);
	c.base = galloc(c.row * (uint32)devh);
	memset(g_mem + c.base, 0x55, c.row * (uint32)devh);
	const uint32 dev = galloc(32), rect = galloc(16);
	rave_h_write32(dev + 0, 0);		/* kQADeviceMemory */
	rave_h_write32(dev + 4, c.row);
	rave_h_write32(dev + 8, (uint32)pix);
	rave_h_write32(dev + 12, (uint32)devw);
	rave_h_write32(dev + 16, (uint32)devh);
	rave_h_write32(dev + 20, c.base);
	rave_h_write32(rect + 0, origin_x);
	rave_h_write32(rect + 4, origin_x + (uint32)w);
	rave_h_write32(rect + 8, origin_y);
	rave_h_write32(rect + 12, origin_y + (uint32)h);
	c.guest = galloc(160);
	memset(g_mem + c.guest, 0, 160);
	RaveGuestResult r = call(RAVE_SLOT_ENGINE_BASE + 0, Args().r(0, c.guest).r(1, dev).r(2, rect).r(3, 0).r(4, flags));
	CHECK(r.r3 == 0, "drawPrivateNew returned %u", r.r3);
	return c;
}

static void method(const Ctx &c, int tag, const Args &a_in)
{
	Args a = a_in;
	a.c.r[0] = c.guest;
	uint32 slot = g_methods[(c.guest << 8) | (uint32)tag];
	RaveGuestResult r = call(slot, a);
	(void)r;
}
static RaveGuestResult method_r(const Ctx &c, int tag, const Args &a_in)
{
	Args a = a_in;
	a.c.r[0] = c.guest;
	return call(g_methods[(c.guest << 8) | (uint32)tag], a);
}

enum { M_SETFLOAT = 0, M_SETINT, M_SETPTR, M_GETFLOAT, M_GETINT, M_GETPTR, M_POINT, M_LINE, M_TRI, M_TRITEX,
       M_VGOURAUD, M_VTEX, M_BITMAP, M_RSTART, M_REND, M_RABORT, M_FLUSH, M_SYNC, M_SUBMITG, M_SUBMITT,
       M_MESHG, M_MESHT };

static void set_bg(const Ctx &c, float a, float r, float g, float b)
{
	method(c, M_SETFLOAT, Args().r(1, 1).f(0, a));
	method(c, M_SETFLOAT, Args().r(1, 2).f(0, r));
	method(c, M_SETFLOAT, Args().r(1, 3).f(0, g));
	method(c, M_SETFLOAT, Args().r(1, 4).f(0, b));
}
static void begin(const Ctx &c) { method(c, M_RSTART, Args().r(1, 0).r(2, 0)); }
static void end(const Ctx &c) { RaveGuestResult r = method_r(c, M_REND, Args().r(1, 0)); CHECK(r.r3 == 0, "renderEnd %u", r.r3); }
static void tri(const Ctx &c, uint32 a, uint32 b, uint32 d) { method(c, M_TRI, Args().r(1, a).r(2, b).r(3, d).r(4, 0)); }

int main(void)
{
	g_mem = (uint8 *)mmap(NULL, kMem, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
	if (g_mem == MAP_FAILED) { printf("mmap failed\n"); return 2; }
	SheepForceSetGeometry((uint8 *)g_mem, 0x1000, 64, 8, 8, 8, 32);	/* only so SheepForceStartup is happy */
	SheepForceStartup(NULL);
	RaveHost host = { host_slot_tvect, host_register_draw, NULL, NULL };
	SheepForceRaveSetHost(&host);

	/* engine methods the manager asks for */
	for (uint32 tag = 0; tag < 18; tag++) {
		const uint32 out = galloc(4);
		RaveGuestResult r = call(RAVE_SLOT_GETMETHOD, Args().r(0, tag).r(1, out));
		CHECK(r.r3 == 0 && rave_h_read32(out) != 0, "getMethod(%u) = %u, tvect %08x", tag, r.r3, rave_h_read32(out));
	}
	{
		const uint32 out = galloc(4);
		CHECK(call(RAVE_SLOT_GETMETHOD, Args().r(0, 99).r(1, out)).r3 != 0, "getMethod(99) must fail");
	}
	/* gestalt: name, features */
	{
		const uint32 buf = galloc(64);
		CHECK(call(3, Args().r(0, 6).r(1, buf)).r3 == 0 && !strcmp((char *)g_mem + buf, "SheepForce Metal"), "gestalt name");
		CHECK(call(3, Args().r(0, 5).r(1, buf)).r3 == 0 && rave_h_read32(buf) == 16, "gestalt name length");
		CHECK(call(3, Args().r(0, 0).r(1, buf)).r3 == 0 && (rave_h_read32(buf) & ((1u << 4) | (1u << 5))) == ((1u << 4) | (1u << 5)), "gestalt optional blend bits");
		CHECK(call(3, Args().r(0, 77).r(1, buf)).r3 == 6, "gestalt unknown selector");
	}
	/* device check: memory 32-bit yes, GDevice no, 24-bit no */
	{
		const uint32 dev = galloc(32);
		rave_h_write32(dev + 0, 0); rave_h_write32(dev + 4, 1024); rave_h_write32(dev + 8, 4);
		rave_h_write32(dev + 12, 256); rave_h_write32(dev + 16, 64); rave_h_write32(dev + 20, galloc(1024 * 64));
		CHECK(call(2, Args().r(0, dev)).r3 == 0, "checkDevice accepts a 32-bit memory device");
		rave_h_write32(dev + 0, 1);
		CHECK(call(2, Args().r(0, dev)).r3 == 3, "checkDevice declines a GDevice");
		rave_h_write32(dev + 0, 0); rave_h_write32(dev + 8, 8);
		CHECK(call(2, Args().r(0, dev)).r3 == 3, "checkDevice declines kQAPixel_RGB24");
	}

	/* 1. flat triangle on a background, 32-bit ARGB */
	{
		Ctx c = make_ctx(64, 48, 4, 0);
		CHECK(g_methods.count((c.guest << 8) | 34) == 1, "all 35 draw methods registered");
		set_bg(c, 1, 0, 0, 1);
		begin(c);
		tri(c, vertex(8, 8, 0.5f, 1, 1, 0, 0, 1), vertex(56, 8, 0.5f, 1, 1, 0, 0, 1), vertex(8, 40, 0.5f, 1, 1, 0, 0, 1));
		end(c);
		Pix in = px32(c.base, c.row, 16, 16, true), out = px32(c.base, c.row, 50, 40, true);
		CHECK(in.a == 255 && is_rgb(in, 255, 0, 0), "inside flat triangle: a%d r%d g%d b%d", in.a, in.r, in.g, in.b);
		CHECK(out.a == 255 && is_rgb(out, 0, 0, 255), "background: a%d r%d g%d b%d", out.a, out.r, out.g, out.b);
		/* coverage against a software rasteriser, ignoring pixels near an edge */
		int bad = 0, counted = 0;
		for (int y = 0; y < c.h; y++)
			for (int x = 0; x < c.w; x++) {
				float px = x + 0.5f, py = y + 0.5f;
				float d1 = (56 - 8) * (py - 8) - (8 - 8) * (px - 8);			/* edge A->B */
				float d2 = (8 - 56) * (py - 8) - (40 - 8) * (px - 56);		/* edge B->C */
				float d3 = (8 - 8) * (py - 40) - (8 - 40) * (px - 8);			/* edge C->A */
				float dist = fminf(fminf(fabsf(d1) / 48.0f, fabsf(d2) / 57.7f), fabsf(d3) / 32.0f);
				if (dist < 1.2f) continue;
				bool inside = (d1 >= 0 && d2 >= 0 && d3 >= 0) || (d1 <= 0 && d2 <= 0 && d3 <= 0);
				Pix p = px32(c.base, c.row, x, y, true);
				bool red = p.r > 128;
				counted++;
				if (red != inside) bad++;
			}
		CHECK(bad == 0, "flat triangle coverage: %d of %d pixels differ from the reference", bad, counted);
	}

	/* 2. Gouraud: colour at the centroid is the mean of the vertex colours */
	{
		Ctx c = make_ctx(60, 60, 4, 0);
		set_bg(c, 1, 0, 0, 0);
		begin(c);
		tri(c, vertex(0, 0, 0.5f, 1, 1, 0, 0, 1), vertex(60, 0, 0.5f, 1, 0, 1, 0, 1), vertex(0, 60, 0.5f, 1, 0, 0, 1, 1));
		end(c);
		Pix m = px32(c.base, c.row, 20, 20, true);	/* barycentre = (20.5/60 ~ 1/3 each) */
		CHECK(near(m.r, 85, 6) && near(m.g, 85, 6) && near(m.b, 85, 6), "Gouraud centroid r%d g%d b%d", m.r, m.g, m.b);
		Pix corner = px32(c.base, c.row, 1, 1, true);
		CHECK(corner.r > 230 && corner.g < 20 && corner.b < 20, "Gouraud near red vertex r%d g%d b%d", corner.r, corner.g, corner.b);
	}

	/* 3. Z buffer: kQAZFunction_LT, far triangle drawn after the near one must lose */
	{
		Ctx c = make_ctx(40, 40, 4, 0);
		set_bg(c, 1, 0, 0, 0);
		method(c, M_SETINT, Args().r(1, 0).r(2, 1));		/* ZFunction = LT */
		begin(c);
		tri(c, vertex(2, 2, 0.2f, 1, 0, 1, 0, 1), vertex(38, 2, 0.2f, 1, 0, 1, 0, 1), vertex(2, 38, 0.2f, 1, 0, 1, 0, 1));	/* near, green */
		tri(c, vertex(2, 2, 0.8f, 1, 1, 0, 0, 1), vertex(38, 2, 0.8f, 1, 1, 0, 0, 1), vertex(2, 38, 0.8f, 1, 1, 0, 0, 1));	/* far, red */
		end(c);
		Pix p = px32(c.base, c.row, 10, 10, true);
		CHECK(is_rgb(p, 0, 255, 0), "z test keeps the near triangle: r%d g%d b%d", p.r, p.g, p.b);
		/* and with GT the far one wins */
		Ctx d = make_ctx(40, 40, 4, 0);
		set_bg(d, 1, 0, 0, 0);
		method(d, M_SETINT, Args().r(1, 0).r(2, 6));		/* ZFunction = GE (clear depth is 1.0) */
		method(d, M_SETFLOAT, Args().r(1, 112).f(0, 0.0));	/* DepthBG = 0 so GE can pass */
		begin(d);
		tri(d, vertex(2, 2, 0.2f, 1, 0, 1, 0, 1), vertex(38, 2, 0.2f, 1, 0, 1, 0, 1), vertex(2, 38, 0.2f, 1, 0, 1, 0, 1));
		tri(d, vertex(2, 2, 0.8f, 1, 1, 0, 0, 1), vertex(38, 2, 0.8f, 1, 1, 0, 0, 1), vertex(2, 38, 0.8f, 1, 1, 0, 0, 1));
		end(d);
		Pix q = px32(d.base, d.row, 10, 10, true);
		CHECK(is_rgb(q, 255, 0, 0), "z test GE keeps the far triangle: r%d g%d b%d", q.r, q.g, q.b);
	}

	/* 4. blending: Interpolate and PreMultiply with alpha 0.5 over blue */
	{
		Ctx c = make_ctx(32, 32, 4, 0);
		set_bg(c, 1, 0, 0, 1);
		method(c, M_SETINT, Args().r(1, 9).r(2, 1));		/* Blend = Interpolate */
		begin(c);
		tri(c, vertex(0, 0, 0.5f, 1, 1, 0, 0, 0.5f), vertex(32, 0, 0.5f, 1, 1, 0, 0, 0.5f), vertex(0, 32, 0.5f, 1, 1, 0, 0, 0.5f));
		end(c);
		Pix p = px32(c.base, c.row, 6, 6, true);
		CHECK(near(p.r, 128, 3) && near(p.g, 0, 3) && near(p.b, 128, 3), "Interpolate blend r%d g%d b%d", p.r, p.g, p.b);
		Ctx d = make_ctx(32, 32, 4, 0);
		set_bg(d, 1, 0, 0, 1);
		method(d, M_SETINT, Args().r(1, 9).r(2, 0));		/* Blend = PreMultiply */
		begin(d);
		tri(d, vertex(0, 0, 0.5f, 1, 0.5f, 0, 0, 0.5f), vertex(32, 0, 0.5f, 1, 0.5f, 0, 0, 0.5f), vertex(0, 32, 0.5f, 1, 0.5f, 0, 0, 0.5f));
		end(d);
		Pix q = px32(d.base, d.row, 6, 6, true);
		CHECK(near(q.r, 128, 3) && near(q.g, 0, 3) && near(q.b, 128, 3), "PreMultiply blend r%d g%d b%d", q.r, q.g, q.b);
	}

	/* 5. triangle meshes, strips, fans */
	{
		Ctx c = make_ctx(40, 40, 4, 0);
		set_bg(c, 1, 0, 0, 0);
		begin(c);
		const uint32 verts = galloc(32 * 4);
		float v[4][8] = { {2, 2, .5f, 1, 1, 1, 0, 1}, {38, 2, .5f, 1, 1, 1, 0, 1}, {2, 38, .5f, 1, 1, 1, 0, 1}, {38, 38, .5f, 1, 1, 1, 0, 1} };
		for (int i = 0; i < 4; i++) for (int k = 0; k < 8; k++) put_float(verts + (uint32)(i * 32 + k * 4), v[i][k]);
		method(c, M_SUBMITG, Args().r(1, 4).r(2, verts));
		const uint32 tris = galloc(32);
		const uint32 idx[2][4] = { {0, 0, 1, 2}, {0, 1, 3, 2} };
		for (int t = 0; t < 2; t++) for (int k = 0; k < 4; k++) rave_h_write32(tris + (uint32)(t * 16 + k * 4), idx[t][k]);
		method(c, M_MESHG, Args().r(1, 2).r(2, tris));
		end(c);
		CHECK(is_rgb(px32(c.base, c.row, 10, 10, true), 255, 255, 0) && is_rgb(px32(c.base, c.row, 30, 30, true), 255, 255, 0),
		      "triangle mesh covers both halves of the quad");
		/* strip: quad as 4 vertices */
		Ctx d = make_ctx(40, 40, 4, 0);
		set_bg(d, 1, 0, 0, 0);
		begin(d);
		float s[4][8] = { {2, 2, .5f, 1, 0, 1, 1, 1}, {2, 38, .5f, 1, 0, 1, 1, 1}, {38, 2, .5f, 1, 0, 1, 1, 1}, {38, 38, .5f, 1, 0, 1, 1, 1} };
		for (int i = 0; i < 4; i++) for (int k = 0; k < 8; k++) put_float(verts + (uint32)(i * 32 + k * 4), s[i][k]);
		const uint32 flags = galloc(16);
		method(d, M_VGOURAUD, Args().r(1, 4).r(2, 4).r(3, verts).r(4, flags));	/* strip */
		end(d);
		CHECK(is_rgb(px32(d.base, d.row, 10, 10, true), 0, 255, 255) && is_rgb(px32(d.base, d.row, 30, 30, true), 0, 255, 255),
		      "triangle strip covers the quad");
		Ctx e = make_ctx(40, 40, 4, 0);
		set_bg(e, 1, 0, 0, 0);
		begin(e);
		float fan[4][8] = { {2, 2, .5f, 1, 1, 0, 1, 1}, {38, 2, .5f, 1, 1, 0, 1, 1}, {38, 38, .5f, 1, 1, 0, 1, 1}, {2, 38, .5f, 1, 1, 0, 1, 1} };
		for (int i = 0; i < 4; i++) for (int k = 0; k < 8; k++) put_float(verts + (uint32)(i * 32 + k * 4), fan[i][k]);
		method(e, M_VGOURAUD, Args().r(1, 4).r(2, 5).r(3, verts).r(4, flags));	/* fan */
		end(e);
		CHECK(is_rgb(px32(e.base, e.row, 10, 10, true), 255, 0, 255) && is_rgb(px32(e.base, e.row, 30, 30, true), 255, 0, 255),
		      "triangle fan covers the quad");
	}

	/* 6. lines and points of width 3 */
	{
		Ctx c = make_ctx(40, 40, 4, 0);
		set_bg(c, 1, 0, 0, 0);
		method(c, M_SETFLOAT, Args().r(1, 5).f(0, 3.0));
		begin(c);
		method(c, M_LINE, Args().r(1, vertex(4, 20, .5f, 1, 1, 1, 1, 1)).r(2, vertex(36, 20, .5f, 1, 1, 1, 1, 1)));
		method(c, M_POINT, Args().r(1, vertex(20, 6, .5f, 1, 1, 0, 0, 1)));
		end(c);
		CHECK(is_rgb(px32(c.base, c.row, 20, 20, true), 255, 255, 255) && is_rgb(px32(c.base, c.row, 20, 19, true), 255, 255, 255)
		      && is_rgb(px32(c.base, c.row, 20, 22, true), 0, 0, 0, 2), "3-pixel-wide line");
		CHECK(is_rgb(px32(c.base, c.row, 20, 6, true), 255, 0, 0), "3-pixel point");
	}

	/* 7. pixel formats: RGB32 (top byte 0), ARGB16 / RGB16 packing, context inside a larger device */
	{
		Ctx c = make_ctx(16, 16, 3, 0);
		set_bg(c, 1, 0, 1, 0);
		begin(c); end(c);
		const uint8 *p = g_mem + c.base + 4 * 16 * 5 + 4 * 5;
		CHECK(p[0] == 0 && p[1] == 0 && p[2] == 255 && p[3] == 0, "RGB32 background bytes %02x %02x %02x %02x", p[0], p[1], p[2], p[3]);
		Ctx d = make_ctx(16, 16, 2, 0);
		set_bg(d, 1, 1, 0, 0);
		begin(d); end(d);
		const uint8 *q = g_mem + d.base + 2 * 16 * 5 + 2 * 5;
		CHECK(q[0] == 0xfc && q[1] == 0x00, "ARGB16 red background %02x%02x (expect fc00)", q[0], q[1]);
		Ctx e = make_ctx(16, 16, 1, 0);
		set_bg(e, 1, 0, 0, 1);
		begin(e); end(e);
		const uint8 *s = g_mem + e.base + 2 * 16 * 5 + 2 * 5;
		CHECK(s[0] == 0x00 && s[1] == 0x1f, "RGB16 blue background %02x%02x (expect 001f)", s[0], s[1]);
		/* a 16x16 context at (8,4) of a 32x32 device leaves the surrounding pixels alone */
		Ctx f = make_ctx(16, 16, 4, 0, 8, 4, 32, 32);
		set_bg(f, 1, 1, 1, 1);
		begin(f); end(f);
		CHECK(px32(f.base, f.row, 10, 6, true).r == 255, "context inside a larger device is written at its origin");
		CHECK(px32(f.base, f.row, 2, 2, true).r == 0x55 && px32(f.base, f.row, 30, 30, true).r == 0x55,
		      "pixels outside the context rectangle are untouched");
	}

	/* 8. no Z buffer (kQAContext_NoZBuffer): draw order decides */
	{
		Ctx c = make_ctx(32, 32, 4, 1);
		set_bg(c, 1, 0, 0, 0);
		method(c, M_SETINT, Args().r(1, 0).r(2, 1));		/* LT would reject the second; no Z buffer ignores it */
		begin(c);
		tri(c, vertex(0, 0, 0.1f, 1, 0, 1, 0, 1), vertex(32, 0, 0.1f, 1, 0, 1, 0, 1), vertex(0, 32, 0.1f, 1, 0, 1, 0, 1));
		tri(c, vertex(0, 0, 0.9f, 1, 1, 0, 0, 1), vertex(32, 0, 0.9f, 1, 1, 0, 0, 1), vertex(0, 32, 0.9f, 1, 1, 0, 0, 1));
		end(c);
		CHECK(is_rgb(px32(c.base, c.row, 6, 6, true), 255, 0, 0), "without a Z buffer the later triangle wins");
	}

	/* 9. state tags read back, and the context is deleted cleanly */
	{
		Ctx c = make_ctx(8, 8, 4, 0);
		method(c, M_SETINT, Args().r(1, 11).r(2, 2));
		CHECK(method_r(c, M_GETINT, Args().r(1, 11)).r3 == 2, "getInt returns what setInt stored");
		method(c, M_SETFLOAT, Args().r(1, 22).f(0, 12.5));
		RaveGuestResult r = method_r(c, M_GETFLOAT, Args().r(1, 22));
		CHECK(r.is_float && fabs(r.f1 - 12.5) < 1e-6, "getFloat returns what setFloat stored");
		call(RAVE_SLOT_ENGINE_BASE + 1, Args().r(0, rave_h_read32(c.guest)));	/* drawPrivateDelete */
	}

	/* 10. a GDevice (the screen): GDevice -> PixMap handle chain, context rectangle in global coordinates */
	{
		const int W = 96, H = 64;
		const uint32 row = (uint32)W * 4, base = galloc(row * (uint32)H);
		memset(g_mem + base, 0x55, row * (uint32)H);
		const uint32 pm = galloc(64), pmh = galloc(4), gd = galloc(64), gdh = galloc(4), dev = galloc(8), rect = galloc(16);
		rave_h_write32(pm + 0, base);
		rave_h_write32(pm + 4, (0x8000u | row) << 16);		/* rowBytes with the PixMap flag bit */
		rave_h_write32(pm + 32, (32u << 16) | 1u);		/* pixelSize 32, cmpCount 1 */
		rave_h_write32(pmh, pm);
		rave_h_write32(gd + 22, pmh);
		rave_h_write32(gd + 34, (0u << 16) | 0u);		/* top 0, left 0 */
		rave_h_write32(gd + 38, ((uint32)H << 16) | (uint32)W);	/* bottom, right */
		rave_h_write32(gdh, gd);
		rave_h_write32(dev + 0, 1);
		rave_h_write32(dev + 4, gdh);
		CHECK(call(2, Args().r(0, dev)).r3 == 0, "checkDevice accepts a 32-bit GDevice");
		rave_h_write32(rect + 0, 16); rave_h_write32(rect + 4, 64); rave_h_write32(rect + 8, 8); rave_h_write32(rect + 12, 40);
		const uint32 ctx = galloc(160);
		memset(g_mem + ctx, 0, 160);
		CHECK(call(0, Args().r(0, ctx).r(1, dev).r(2, rect).r(3, 0).r(4, 0)).r3 == 0, "drawPrivateNew on a GDevice");
		Ctx c; c.guest = ctx; c.base = base; c.row = row; c.w = 48; c.h = 32; c.pix = 3;
		set_bg(c, 1, 0, 1, 0);
		begin(c);
		tri(c, vertex(0, 0, 0.5f, 1, 1, 0, 0, 1), vertex(48, 0, 0.5f, 1, 1, 0, 0, 1), vertex(0, 32, 0.5f, 1, 1, 0, 0, 1));
		end(c);
		CHECK(is_rgb(px32(base, row, 20, 12, false), 255, 0, 0), "GDevice context: triangle at its screen position");
		CHECK(is_rgb(px32(base, row, 60, 36, false), 0, 255, 0), "GDevice context: background at its screen position");
		CHECK(px32(base, row, 4, 4, false).r == 0x55 && px32(base, row, 80, 50, false).r == 0x55, "screen outside the context untouched");
	}

	/* 11. a clip region: a plain rectangle band over a narrower band (a "T" turned upside down) */
	{
		const int W = 32, H = 32;
		const uint32 row = (uint32)W * 4, base = galloc(row * (uint32)H);
		memset(g_mem + base, 0x55, row * (uint32)H);
		const uint32 dev = galloc(32), rect = galloc(16), clip = galloc(8), rh = galloc(4), rgn = galloc(64);
		rave_h_write32(dev + 0, 0); rave_h_write32(dev + 4, row); rave_h_write32(dev + 8, 4);
		rave_h_write32(dev + 12, W); rave_h_write32(dev + 16, H); rave_h_write32(dev + 20, base);
		rave_h_write32(rect + 0, 0); rave_h_write32(rect + 4, W); rave_h_write32(rect + 8, 0); rave_h_write32(rect + 12, H);
		/* region: size 10 + 2*(3+5+4+1+... words); words: y 0: [0,32]; y 16: toggles 0,8,24,32; y 32: 8,24; end */
		const uint16 words[] = { 0, 0, 32, 0x7fff,  16, 0, 8, 24, 32, 0x7fff,  32, 8, 24, 0x7fff,  0x7fff };
		uint32 a = rgn + 10;
		for (unsigned i = 0; i < sizeof words / sizeof words[0]; i++, a += 2) {
			g_mem[a] = (uint8)(words[i] >> 8); g_mem[a + 1] = (uint8)words[i];
		}
		const uint16 size = (uint16)(a - rgn);
		g_mem[rgn] = (uint8)(size >> 8); g_mem[rgn + 1] = (uint8)size;
		const int16 bb[4] = { 0, 0, 32, 32 };		/* top left bottom right */
		for (int i = 0; i < 4; i++) { g_mem[rgn + 2 + 2 * i] = (uint8)((uint16)bb[i] >> 8); g_mem[rgn + 3 + 2 * i] = (uint8)bb[i]; }
		rave_h_write32(rh, rgn);
		rave_h_write32(clip + 0, 0);			/* kQAClipRgn */
		rave_h_write32(clip + 4, rh);
		const uint32 ctx = galloc(160);
		memset(g_mem + ctx, 0, 160);
		CHECK(call(0, Args().r(0, ctx).r(1, dev).r(2, rect).r(3, clip).r(4, 0)).r3 == 0, "drawPrivateNew with a clip region");
		Ctx c; c.guest = ctx; c.base = base; c.row = row; c.w = W; c.h = H; c.pix = 4;
		set_bg(c, 1, 1, 0, 0);
		begin(c); end(c);
		CHECK(is_rgb(px32(base, row, 2, 8, true), 255, 0, 0) && is_rgb(px32(base, row, 30, 8, true), 255, 0, 0), "clip: top band is painted");
		CHECK(is_rgb(px32(base, row, 16, 24, true), 255, 0, 0), "clip: the narrow band is painted");
		CHECK(px32(base, row, 2, 24, true).r == 0x55 && px32(base, row, 30, 24, true).r == 0x55, "clip: pixels outside the region are untouched");
		/* a clip type other than a Macintosh region is declined */
		rave_h_write32(clip + 0, 1);
		CHECK(call(0, Args().r(0, galloc(160)).r(1, dev).r(2, rect).r(3, clip).r(4, 0)).r3 == 3, "a non-region clip is declined");
	}

	/* 12. textures: nearest-filtered 2x2 ARGB32 on a quad, modulate, decal, CL8, fog */
	{
		auto vtex = [&](float x, float y, float invw, float u, float v, float r, float g, float b, float a,
				float kd, float ks) {
			uint32 at = galloc(64);
			const float f[16] = { x, y, 0.5f, invw, r, g, b, a, u * invw, v * invw, kd, kd, kd, ks, ks, ks };
			for (int i = 0; i < 16; i++) put_float(at + (uint32)i * 4, f[i]);
			return at;
		};
		auto make_tex = [&](uint32 type, uint32 flags, int w, int h, const std::vector<uint8> &data, int bits) {
			const uint32 px = galloc((uint32)data.size());
			memcpy(g_mem + px, data.data(), data.size());
			const uint32 img = galloc(16), out = galloc(4);
			rave_h_write32(img + 0, (uint32)w); rave_h_write32(img + 4, (uint32)h);
			rave_h_write32(img + 8, (uint32)((w * bits + 7) / 8)); rave_h_write32(img + 12, px);
			CHECK(call(4, Args().r(0, flags).r(1, type).r(2, img).r(3, out)).r3 == 0, "textureNew type %u", type);
			return rave_h_read32(out);
		};
		/* 2x2: red, green / blue, white (ARGB32 big-endian bytes A R G B) */
		const std::vector<uint8> px = { 255, 255, 0, 0, 255, 0, 255, 0,   255, 0, 0, 255, 255, 255, 255, 255 };
		const uint32 tex = make_tex(4, 0, 2, 2, px, 32);
		CHECK(tex != 0, "texture handle");
		auto quad = [&](const Ctx &c, float invw, float kd, float ks, float r, float g, float b, float a) {
			uint32 v00 = vtex(0, 0, invw, 0, 0, r, g, b, a, kd, ks), v10 = vtex(32, 0, invw, 1, 0, r, g, b, a, kd, ks);
			uint32 v01 = vtex(0, 32, invw, 0, 1, r, g, b, a, kd, ks), v11 = vtex(32, 32, invw, 1, 1, r, g, b, a, kd, ks);
			method(c, M_TRITEX, Args().r(1, v00).r(2, v10).r(3, v01).r(4, 0));
			method(c, M_TRITEX, Args().r(1, v10).r(2, v11).r(3, v01).r(4, 0));
		};
		Ctx c = make_ctx(32, 32, 4, 0);
		set_bg(c, 1, 0, 0, 0);
		method(c, M_SETPTR, Args().r(1, 13).r(2, tex));			/* kQATag_Texture */
		method(c, M_SETINT, Args().r(1, 11).r(2, 0));			/* TextureFilter: nearest */
		method(c, M_SETINT, Args().r(1, 12).r(2, 0));			/* TextureOp: none */
		begin(c);
		quad(c, 1, 1, 0, 1, 1, 1, 1);
		end(c);
		CHECK(is_rgb(px32(c.base, c.row, 8, 8, true), 255, 0, 0), "texel (0,0) red: %d %d %d", px32(c.base, c.row, 8, 8, true).r, px32(c.base, c.row, 8, 8, true).g, px32(c.base, c.row, 8, 8, true).b);
		CHECK(is_rgb(px32(c.base, c.row, 24, 8, true), 0, 255, 0), "texel (1,0) green");
		CHECK(is_rgb(px32(c.base, c.row, 8, 24, true), 0, 0, 255), "texel (0,1) blue");
		CHECK(is_rgb(px32(c.base, c.row, 24, 24, true), 255, 255, 255), "texel (1,1) white");
		/* modulate by kd = 0.5 */
		Ctx d = make_ctx(32, 32, 4, 0);
		set_bg(d, 1, 0, 0, 0);
		method(d, M_SETPTR, Args().r(1, 13).r(2, tex));
		method(d, M_SETINT, Args().r(1, 12).r(2, 1));			/* Modulate */
		begin(d);
		quad(d, 1, 0.5f, 0, 1, 1, 1, 1);
		end(d);
		CHECK(is_rgb(px32(d.base, d.row, 24, 24, true), 128, 128, 128, 3), "modulate halves white");
		/* highlight adds ks = 0.25 */
		Ctx e = make_ctx(32, 32, 4, 0);
		set_bg(e, 1, 0, 0, 0);
		method(e, M_SETPTR, Args().r(1, 13).r(2, tex));
		method(e, M_SETINT, Args().r(1, 12).r(2, 2));			/* Highlight */
		begin(e);
		quad(e, 1, 1, 0.25f, 1, 1, 1, 1);
		end(e);
		CHECK(is_rgb(px32(e.base, e.row, 8, 8, true), 255, 64, 64, 3), "highlight adds to red: %d %d %d", px32(e.base, e.row, 8, 8, true).r, px32(e.base, e.row, 8, 8, true).g, px32(e.base, e.row, 8, 8, true).b);
		/* decal with a transparent texel shows the vertex colour */
		const std::vector<uint8> px2 = { 0, 255, 0, 0,  255, 0, 255, 0,   255, 0, 0, 255,  255, 255, 255, 255 };
		const uint32 tex2 = make_tex(4, 0, 2, 2, px2, 32);
		Ctx f = make_ctx(32, 32, 4, 0);
		set_bg(f, 1, 0, 0, 0);
		method(f, M_SETPTR, Args().r(1, 13).r(2, tex2));
		method(f, M_SETINT, Args().r(1, 12).r(2, 4));			/* Decal */
		begin(f);
		quad(f, 1, 1, 0, 1, 1, 0, 1);					/* vertex colour yellow */
		end(f);
		CHECK(is_rgb(px32(f.base, f.row, 8, 8, true), 255, 255, 0), "decal: transparent texel shows the vertex colour");
		CHECK(is_rgb(px32(f.base, f.row, 24, 8, true), 0, 255, 0), "decal: opaque texel shows the texture");
		/* CL8 texture with a colour table */
		const uint32 tbl = galloc(256 * 4);
		for (int i = 0; i < 256; i++) rave_h_write32(tbl + (uint32)i * 4, (uint32)((i << 16) | ((255 - i) << 8) | 7));
		const uint32 tout = galloc(4);
		CHECK(call(10, Args().r(0, 0).r(1, tbl).r(2, 0).r(3, tout)).r3 == 0, "colorTableNew");
		const std::vector<uint8> idx = { 10, 200, 40, 255 };
		const uint32 cl = make_tex(6, 0, 2, 2, idx, 8);
		CHECK(call(12, Args().r(0, cl).r(1, rave_h_read32(tout))).r3 == 0, "textureBindColorTable");
		Ctx g = make_ctx(32, 32, 4, 0);
		set_bg(g, 1, 0, 0, 0);
		method(g, M_SETPTR, Args().r(1, 13).r(2, cl));
		method(g, M_SETINT, Args().r(1, 12).r(2, 0));
		begin(g);
		quad(g, 1, 1, 0, 1, 1, 1, 1);
		end(g);
		Pix cp = px32(g.base, g.row, 24, 8, true);	/* index 200 */
		CHECK(is_rgb(cp, 200, 55, 7), "CL8 texture through the colour table: %d %d %d", cp.r, cp.g, cp.b);
		/* mipmapped texture is accepted: 4x4, 3 levels */
		{
			const uint32 p0 = galloc(64), p1 = galloc(16), p2 = galloc(4), img = galloc(48), out = galloc(4);
			memset(g_mem + p0, 255, 64); memset(g_mem + p1, 255, 16); memset(g_mem + p2, 255, 4);
			const uint32 ps[3] = { p0, p1, p2 };
			for (int i = 0; i < 3; i++) {
				rave_h_write32(img + (uint32)i * 16 + 0, 4u >> i); rave_h_write32(img + (uint32)i * 16 + 4, 4u >> i);
				rave_h_write32(img + (uint32)i * 16 + 8, (4u >> i) * 4u); rave_h_write32(img + (uint32)i * 16 + 12, ps[i]);
			}
			CHECK(call(4, Args().r(0, 2).r(1, 4).r(2, img).r(3, out)).r3 == 0, "mipmapped textureNew");
		}
		/* depth fog, linear: invW 0.5 -> w = 2, start 0, end 4 -> half way between blue fog and red */
		Ctx h = make_ctx(32, 32, 4, 0);
		set_bg(h, 1, 0, 0, 0);
		method(h, M_SETINT, Args().r(1, 17).r(2, 2));			/* FogMode = Linear */
		method(h, M_SETFLOAT, Args().r(1, 18).f(0, 1.0)); method(h, M_SETFLOAT, Args().r(1, 19).f(0, 0.0));
		method(h, M_SETFLOAT, Args().r(1, 20).f(0, 0.0)); method(h, M_SETFLOAT, Args().r(1, 21).f(0, 1.0));
		method(h, M_SETFLOAT, Args().r(1, 22).f(0, 0.0)); method(h, M_SETFLOAT, Args().r(1, 23).f(0, 4.0));
		begin(h);
		tri(h, vertex(0, 0, 0.5f, 0.5f, 1, 0, 0, 1), vertex(32, 0, 0.5f, 0.5f, 1, 0, 0, 1), vertex(0, 32, 0.5f, 0.5f, 1, 0, 0, 1));
		end(h);
		Pix fp = px32(h.base, h.row, 6, 6, true);
		CHECK(near(fp.r, 128, 3) && fp.g < 3 && near(fp.b, 128, 3), "linear fog: %d %d %d", fp.r, fp.g, fp.b);
		/* exponential fog, density 0.5, w = 2: exp(-1) = 0.368 of the colour */
		Ctx k = make_ctx(32, 32, 4, 0);
		set_bg(k, 1, 0, 0, 0);
		method(k, M_SETINT, Args().r(1, 17).r(2, 3));
		method(k, M_SETFLOAT, Args().r(1, 19).f(0, 0.0)); method(k, M_SETFLOAT, Args().r(1, 20).f(0, 0.0)); method(k, M_SETFLOAT, Args().r(1, 21).f(0, 0.0));
		method(k, M_SETFLOAT, Args().r(1, 24).f(0, 0.5));
		begin(k);
		tri(k, vertex(0, 0, 0.5f, 0.5f, 1, 0, 0, 1), vertex(32, 0, 0.5f, 0.5f, 1, 0, 0, 1), vertex(0, 32, 0.5f, 0.5f, 1, 0, 0, 1));
		end(k);
		CHECK(near(px32(k.base, k.row, 6, 6, true).r, 94, 4), "exponential fog: %d (expect about 94)", px32(k.base, k.row, 6, 6, true).r);
	}

	/* 13. multitexture (a submitted vertex array with SubmitMultiTextureParams) and antialiasing */
	{
		auto make_tex = [&](int w, int h, const std::vector<uint8> &data) {
			const uint32 px = galloc((uint32)data.size());
			memcpy(g_mem + px, data.data(), data.size());
			const uint32 img = galloc(16), out = galloc(4);
			rave_h_write32(img + 0, (uint32)w); rave_h_write32(img + 4, (uint32)h);
			rave_h_write32(img + 8, (uint32)(w * 4)); rave_h_write32(img + 12, px);
			CHECK(call(4, Args().r(0, 0).r(1, 4).r(2, img).r(3, out)).r3 == 0, "textureNew");
			return rave_h_read32(out);
		};
		const std::vector<uint8> gray = { 255, 200, 200, 200, 255, 200, 200, 200, 255, 200, 200, 200, 255, 200, 200, 200 };
		const std::vector<uint8> second = { 255, 255, 0, 0,  255, 128, 128, 128,  255, 128, 128, 128,  255, 128, 128, 128 };
		const uint32 t0 = make_tex(2, 2, gray), t1 = make_tex(2, 2, second);
		auto run = [&](int op, float factor) {
			Ctx c = make_ctx(32, 32, 4, 0);
			set_bg(c, 1, 0, 0, 0);
			method(c, M_SETPTR, Args().r(1, 13).r(2, t0));
			method(c, M_SETINT, Args().r(1, 12).r(2, 0));
			method(c, M_SETINT, Args().r(1, 11).r(2, 0));
			method(c, M_SETINT, Args().r(1, 33).r(2, 1));		/* MultiTextureEnable = 1 */
			method(c, M_SETPTR, Args().r(1, 26).r(2, t1));		/* MultiTexture */
			method(c, M_SETINT, Args().r(1, 35).r(2, (uint32)op));	/* MultiTextureOp */
			method(c, M_SETINT, Args().r(1, 36).r(2, 0));		/* MultiTextureFilter nearest */
			method(c, M_SETFLOAT, Args().r(1, 51).f(0, factor));	/* MultiTextureFactor */
			const uint32 verts = galloc(64 * 4), mt = galloc(12 * 4);
			const float corner[4][2] = { {0, 0}, {32, 0}, {0, 32}, {32, 32} };
			for (int i = 0; i < 4; i++) {
				const float f[16] = { corner[i][0], corner[i][1], 0.5f, 1, 1, 1, 1, 1, corner[i][0] / 32, corner[i][1] / 32, 1, 1, 1, 0, 0, 0 };
				for (int k = 0; k < 16; k++) put_float(verts + (uint32)(i * 64 + k * 4), f[k]);
				put_float(mt + (uint32)(i * 12 + 0), 1.0f);
				put_float(mt + (uint32)(i * 12 + 4), corner[i][0] / 32);
				put_float(mt + (uint32)(i * 12 + 8), corner[i][1] / 32);
			}
			method(c, M_SUBMITT, Args().r(1, 4).r(2, verts));
			method(c, 24, Args().r(1, 4).r(2, mt));			/* SubmitMultiTextureParams */
			const uint32 tris = galloc(32);
			const uint32 idx[2][4] = { {0, 0, 1, 2}, {0, 1, 3, 2} };
			for (int k = 0; k < 2; k++) for (int j = 0; j < 4; j++) rave_h_write32(tris + (uint32)(k * 16 + j * 4), idx[k][j]);
			method(c, M_MESHT, Args().r(1, 2).r(2, tris));
			end(c);
			return c;
		};
		/* g_methods only knows tags up to 34, slot 24 is the real SubmitMultiTextureParams tag */
		Ctx m = run(1, 0);
		Pix a0 = px32(m.base, m.row, 8, 8, true), a1 = px32(m.base, m.row, 24, 24, true);
		CHECK(is_rgb(a0, 200, 0, 0, 3), "multitexture modulate, red texel: %d %d %d", a0.r, a0.g, a0.b);
		CHECK(is_rgb(a1, 100, 100, 100, 3), "multitexture modulate, gray texel: %d %d %d", a1.r, a1.g, a1.b);
		Ctx ad = run(0, 0);
		Pix b0 = px32(ad.base, ad.row, 8, 8, true), b1 = px32(ad.base, ad.row, 24, 24, true);
		CHECK(is_rgb(b0, 255, 200, 200, 3), "multitexture add, red texel: %d %d %d", b0.r, b0.g, b0.b);
		CHECK(is_rgb(b1, 255, 255, 255, 3), "multitexture add, gray texel (clamped): %d %d %d", b1.r, b1.g, b1.b);
		Ctx fx = run(3, 0.5f);
		Pix c0 = px32(fx.base, fx.row, 8, 8, true);
		CHECK(is_rgb(c0, 228, 100, 100, 4), "multitexture fixed 0.5, red texel: %d %d %d", c0.r, c0.g, c0.b);

		/* antialiasing: a diagonal edge has intermediate pixels only with kQATag_Antialias set */
		auto edge_pixels = [&](int aa) {
			Ctx c = make_ctx(40, 40, 4, 0);
			set_bg(c, 1, 0, 0, 0);
			method(c, M_SETINT, Args().r(1, 8).r(2, (uint32)aa));
			begin(c);
			tri(c, vertex(2, 2, 0.5f, 1, 1, 1, 1, 1), vertex(38, 10, 0.5f, 1, 1, 1, 1, 1), vertex(10, 38, 0.5f, 1, 1, 1, 1, 1));
			end(c);
			int mid = 0;
			for (int y = 0; y < 40; y++) for (int x = 0; x < 40; x++) {
				Pix p = px32(c.base, c.row, x, y, true);
				if (p.r > 20 && p.r < 235) mid++;
			}
			return mid;
		};
		const int plain = edge_pixels(0), best = edge_pixels(3);
		CHECK(plain == 0, "no antialiasing: %d intermediate pixels", plain);
		CHECK(best > 20, "antialiasing on: %d intermediate pixels on the edges", best);
	}

	printf("sheepforce_rave_harness: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
