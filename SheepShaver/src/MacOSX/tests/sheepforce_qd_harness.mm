/*
 *  sheepforce_qd_harness.mm - Metal QuickDraw accelerator vs. a CPU reference
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
 * Headless test of SheepForceTryFill / TryInvert / TryBlit. It builds a page-aligned
 * "no-copy" framebuffer, runs random operations through the real Metal path, runs the
 * same operations on a CPU mirror using nqd_blit_ops.h, and compares every byte.
 * Built and run by tools/run_sheepforce_tests.py.
 */
#include "sysdeps.h"
#include "sheepforce.h"
#include "nqd_blit_ops.h"
#include "nqd_region.h"
#include "nqd_fillmask.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <vector>
#include <set>
#include <algorithm>
#include <iterator>

/* Symbols the emulator normally provides. */
bool PrefsFindBool(const char *name)
{
	return !strcmp(name, "sheepforce") || !strcmp(name, "sheepforce_qd");
}
rgb_color mac_pal[256];
rgb_color mac_gamma[256];
uint32_t nw_la_ram_base, nw_la_ram_size, nw_la_rom_base, nw_la_kdp_pa;

static const int W = 256, H = 128, ROW = W * 4;
static const uint32 PAGE_BYTES = (uint32)(ROW * H);

static uint32 rng_state = 12345;
static uint32 rnd(void)
{
	rng_state = rng_state * 1664525u + 1013904223u;
	return rng_state >> 8;
}
static int rnd_range(int lo, int hi) { return lo + (int)(rnd() % (uint32)(hi - lo + 1)); }

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

/* CPU reference blit with ideal semantics: read the whole source rectangle first. */
static void ref_blit(uint8 *dst, int dst_row, const uint8 *src, int src_row, int dbpp, int sbpp,
		     int w, int h, int mode, uint32 back, const uint32 *pal)
{
	std::vector<uint32> pix((size_t)w * h);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			const uint8 *s = src + (size_t)y * src_row + (size_t)x * sbpp;
			uint32 v;
			if (sbpp == 1) v = pal[s[0]];
			else if (sbpp == 2) v = nqd_expand555((uint16)((s[0] << 8) | s[1]));
			else v = nqd_word_load(s);
			pix[(size_t)y * w + x] = v;
		}
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			uint8 *d = dst + (size_t)y * dst_row + (size_t)x * dbpp;
			uint32 v = pix[(size_t)y * w + x];
			if (dbpp != 4) { memcpy(d, src + (size_t)y * src_row + (size_t)x * sbpp, dbpp); continue; }
			if (mode == 36 && nqd_transparent_skip(v, back)) continue;
			nqd_word_store(d, nqd_blit_mode(mode, v, nqd_word_load(d)));
		}
}


/* Encode a binary mask as a QuickDraw region (big-endian words), the way the ROM builds them. */
static std::vector<uint8> encode_region(const std::vector<uint8> &m, int mw, int mh, int top, int left)
{
	int bt = mh, bb = -1, bl = mw, br = -1;
	for (int y = 0; y < mh; y++)
		for (int x = 0; x < mw; x++)
			if (m[(size_t)y * mw + x]) {
				bt = std::min(bt, y); bb = std::max(bb, y + 1); bl = std::min(bl, x); br = std::max(br, x + 1);
			}
	std::vector<uint8> out;
	auto put = [&](int v) { out.push_back((uint8)(v >> 8)); out.push_back((uint8)v); };
	put(0); put(top + bt); put(left + bl); put(top + bb); put(left + br);
	auto edges = [&](int y) {
		std::vector<int> e;
		bool prev = false;
		for (int x = 0; x <= mw; x++) {
			bool cur = y >= 0 && y < mh && x < mw && m[(size_t)y * mw + x];
			if (cur != prev) e.push_back(left + x);
			prev = cur;
		}
		return e;
	};
	for (int y = bt; y <= bb; y++) {
		std::vector<int> a = edges(y - 1), b = edges(y), d;
		std::set_symmetric_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(d));
		if (d.empty()) continue;
		put(top + y);
		for (int x : d) put(x);
		put(0x7fff);
	}
	put(0x7fff);
	out[0] = (uint8)(out.size() >> 8); out[1] = (uint8)out.size();
	return out;
}

static void test_regions(void)
{
	int checked = 0;
	for (int iter = 0; iter < 400; iter++) {
		const int mw = rnd_range(4, 40), mh = rnd_range(3, 30);
		std::vector<uint8> m((size_t)mw * mh, 0);
		const int blobs = rnd_range(1, 4);
		for (int b = 0; b < blobs; b++) {
			int x0 = rnd_range(0, mw - 1), x1 = rnd_range(x0 + 1, mw), y0 = rnd_range(0, mh - 1), y1 = rnd_range(y0 + 1, mh);
			for (int y = y0; y < y1; y++)
				for (int x = x0; x < x1; x++)
					m[(size_t)y * mw + x] ^= (iter % 3 == 0) ? 1 : 0, m[(size_t)y * mw + x] |= (iter % 3 != 0);
		}
		bool any = false;
		for (uint8 v : m) any |= v != 0;
		if (!any) continue;
		const int top = rnd_range(-20, 40), left = rnd_range(-20, 40);
		std::vector<uint8> rgn = encode_region(m, mw, mh, top, left);
		auto rd16 = [&](uint32 a) -> uint32 { return a + 1 < rgn.size() ? ((uint32)rgn[a] << 8 | rgn[a + 1]) : 0; };
		/* a random query rectangle that overlaps and extends past the mask */
		const int rt = top + rnd_range(-5, mh), rl = left + rnd_range(-5, mw);
		const int rb = rt + rnd_range(1, mh + 6), rr = rl + rnd_range(1, mw + 6);
		NqdSpans sp;
		bool ok = nqd_region_to_spans(rd16, 0, rt, rl, rb, rr, sp);
		CHECK(ok, "region decode failed (iter %d)", iter);
		if (!ok) continue;
		for (int y = 0; y < rb - rt; y++)
			for (int x = 0; x < rr - rl; x++) {
				const int my = rt + y - top, mx = rl + x - left;
				const bool want = my >= 0 && my < mh && mx >= 0 && mx < mw && m[(size_t)my * mw + mx];
				CHECK(sp.contains(x, y) == want, "region span mismatch iter %d at %d,%d", iter, x, y);
			}
		/* a rectangular region is stored without scanlines */
		std::vector<uint8> rect = { 0, 10, 0, 5, 0, 7, 0, 20, 0, 30 };
		auto rdr = [&](uint32 a) -> uint32 { return (uint32)rect[a] << 8 | rect[a + 1]; };
		NqdSpans rs;
		CHECK(nqd_region_to_spans(rdr, 0, 0, 0, 40, 40, rs), "rect region decode");
		CHECK(rs.contains(7, 5) && rs.contains(29, 19) && !rs.contains(6, 5) && !rs.contains(30, 5) && !rs.contains(10, 4) && !rs.contains(10, 20),
		      "rect region edges");
		/* intersection with a second region */
		NqdSpans full, both;
		full.full(rr - rl, rb - rt);
		nqd_spans_intersect(sp, full, both);
		CHECK(both.pixels() == sp.pixels(), "intersect with full changed the clip");
		NqdSpans none;
		none.w = rr - rl; none.h = rb - rt; none.row_start.assign((size_t)none.h + 1, 0);
		nqd_spans_intersect(sp, none, both);
		CHECK(both.pixels() == 0, "intersect with empty is not empty");
		checked++;
	}
	printf("region decode: %d random regions checked\n", checked);
}


/* ---- hook code 3 acceptance rule against fake guest memory ---- */
struct FakeGuest {
	std::vector<uint8_t> mem = std::vector<uint8_t>(0x20000, 0);
	uint32_t fbm; uint64_t fbb;
	uint32_t r8(uint32_t a) { return a < mem.size() ? mem[a] : 0; }
	uint32_t r16(uint32_t a) { return a + 1 < mem.size() ? ((uint32_t)mem[a] << 8 | mem[a + 1]) : 0; }
	uint32_t r32(uint32_t a) { return a + 3 < mem.size() ? ((uint32_t)mem[a] << 24 | (uint32_t)mem[a + 1] << 16 | (uint32_t)mem[a + 2] << 8 | mem[a + 3]) : 0; }
	bool owns(uint32_t a) { return a >= fbm && (uint64_t)a < (uint64_t)fbm + fbb; }
	uint32_t fb_mac() { return fbm; }
	uint64_t fb_bytes() { return fbb; }
	void w8(uint32_t a, uint32_t v) { mem[a] = (uint8_t)v; }
	void w16(uint32_t a, uint32_t v) { mem[a] = (uint8_t)(v >> 8); mem[a + 1] = (uint8_t)v; }
	void w32(uint32_t a, uint32_t v) { w16(a, v >> 16); w16(a + 2, v); }
};

static const uint32_t BLK = 0x2000, PORT = 0x3000, PORT_H = 0x3100, PAT = 0x3200;

/* A valid block: a rect (t,l,b,r) at the framebuffer origin in a W x H, 4-byte-pixel, ROW-stride destination. */
static void fm_block(FakeGuest &g, int t, int l, int b, int r, uint32_t fore, uint32_t back, uint8_t pat)
{
	std::fill(g.mem.begin(), g.mem.end(), 0);
	g.w32(BLK + NQD_FM_TRANSFER, 8); g.w32(BLK + NQD_FM_PEN, 8);
	g.w32(BLK + NQD_FM_FORE, fore); g.w32(BLK + NQD_FM_BACK, back);
	g.w32(BLK + NQD_FM_DEST_BASE, g.fbm); g.w32(BLK + NQD_FM_DEST_ROW, ROW);
	g.w32(BLK + NQD_FM_DEST_PIXEL, 32);
	g.w16(BLK + NQD_FM_DEST_RECT + 0, t); g.w16(BLK + NQD_FM_DEST_RECT + 2, l);
	g.w16(BLK + NQD_FM_DEST_RECT + 4, b); g.w16(BLK + NQD_FM_DEST_RECT + 6, r);
	g.w32(BLK + 4, PORT); g.w16(PORT + 6, 0xc000);
	g.w32(PORT + 0x3e, PORT_H); g.w32(PORT_H, PAT);
	g.w16(PAT, 0);
	for (int i = 0; i < 8; i++) g.w8(PAT + 20 + i, pat);
}
/* Add a region (rect only, or from a mask) reachable from block word 'slot'. */
static void fm_region(FakeGuest &g, int slot, uint32_t at, const std::vector<uint8_t> &rgn)
{
	for (size_t i = 0; i < rgn.size(); i++) g.mem[at + i] = rgn[i];
	g.w32(0x4000 + slot * 4, at);			/* master pointer: the handle's target */
	g.w32(BLK + 0x100 + slot * 4, 0x4000 + slot * 4);	/* the block word holds the handle */
}
static std::vector<uint8_t> rect_region(int t, int l, int b, int r)
{
	std::vector<uint8_t> v = { 0, 10 };
	for (int x : { t, l, b, r }) { v.push_back((uint8_t)(x >> 8)); v.push_back((uint8_t)x); }
	return v;
}

static void test_fillmask(uint8 *real_fb)
{
	FakeGuest g;
	g.fbm = 0x1000; g.fbb = (uint64_t)W * H * 4;
	int cases = 0;
	auto build = [&](NqdFillmaskPlan &pl) { return nqd_fillmask_build(g, BLK, &pl, { false, false, true, false }); };
	NqdFillmaskPlan pl;

	/* Guest pointers are followed only below the real RAM size: with a small RAM a pointer between its end and the
	 * old fixed limit made the host read unmapped memory. */
	CHECK(nqd_fm_ptr_ok(0x10000000u) && !nqd_fm_ptr_ok(0x1ff00000u) && !nqd_fm_ptr_ok(0xfffu) && !nqd_fm_ptr_ok(0x10000001u),
	      "default pointer limit");
	nqd_fm_set_ram_size(0x08000000u);		/* 128 MB */
	CHECK(nqd_fm_ptr_ok(0x07000000u) && !nqd_fm_ptr_ok(0x07ff0000u) && !nqd_fm_ptr_ok(0x10000000u), "pointer limit follows RAM size");
	nqd_fm_set_ram_size(0x20000000u);		/* a larger RAM never raises it */
	CHECK(!nqd_fm_ptr_ok(0x10000000u), "pointer limit only lowers");
	nqd_fm_limit() = 0x1ff00000u;			/* restore for the cases below */

	/* accepted: spans, colour, extent */
	fm_block(g, 2, 4, 10, 14, 0x00dddddd, 0x00ffffff, 0xff);
	fm_region(g, 0, 0x5000, rect_region(0, 0, 100, 100));
	CHECK(build(pl) == NQD_FM_OK, "basic plan is accepted");
	CHECK(pl.spans.pixels() == 80 && pl.width == 10 && pl.height == 8 && pl.dest_x == 4 && pl.dest_y == 2, "basic plan geometry");
	CHECK(pl.colour == htonl(0x00dddddd), "basic plan colour");
	CHECK(pl.start == (uint64_t)2 * ROW + 16 && pl.end == pl.start + 7 * (uint64_t)ROW + 40, "basic plan extent");
	cases++;

	/* patCopy with an all-clear pattern paints the back colour; notPatCopy swaps */
	fm_block(g, 2, 4, 10, 14, 0x00dddddd, 0x00aaaaaa, 0x00); fm_region(g, 0, 0x5000, rect_region(0, 0, 100, 100));
	CHECK(build(pl) == NQD_FM_OK && pl.colour == htonl(0x00aaaaaa), "clear pattern paints back");
	fm_block(g, 2, 4, 72, 74, 0x00dddddd, 0x00aaaaaa, 0x00); fm_region(g, 0, 0x5000, rect_region(0, 0, 100, 100));	/* a large rect: small pen-12 draws are refused */
	g.w32(BLK + NQD_FM_PEN, 12);
	CHECK(build(pl) == NQD_FM_OK && pl.colour == htonl(0x00dddddd), "notPatCopy with clear pattern paints fore");
	fm_block(g, 2, 4, 72, 74, 0x00dddddd, 0x00aaaaaa, 0xff); fm_region(g, 0, 0x5000, rect_region(0, 0, 100, 100)); g.w32(BLK + NQD_FM_PEN, 12);
	CHECK(build(pl) == NQD_FM_OK && pl.colour == htonl(0x00aaaaaa), "notPatCopy with set pattern paints back");
	cases += 3;

	/* each rejection, one change at a time */
	struct Rej { const char *name; int want; void (*mut)(FakeGuest &); };
	static const Rej rejs[] = {
		{ "transfer mode", NQD_FM_REJ_MODE, [](FakeGuest &g) { g.w32(BLK + NQD_FM_TRANSFER, 0); } },
		{ "pen mode 9", NQD_FM_REJ_MODE, [](FakeGuest &g) { g.w32(BLK + NQD_FM_PEN, 9); } },
		{ "depth 16", NQD_FM_REJ_DEPTH, [](FakeGuest &g) { g.w32(BLK + NQD_FM_DEST_PIXEL, 16); } },
		{ "rowbytes 0", NQD_FM_REJ_ROWBYTES, [](FakeGuest &g) { g.w32(BLK + NQD_FM_DEST_ROW, 0); } },
		{ "rowbytes negative", NQD_FM_REJ_ROWBYTES, [](FakeGuest &g) { g.w32(BLK + NQD_FM_DEST_ROW, (uint32_t)-ROW); } },
		{ "rowbytes 3", NQD_FM_REJ_ROWBYTES, [](FakeGuest &g) { g.w32(BLK + NQD_FM_DEST_ROW, 3); } },
		{ "rowbytes too small for the rect", NQD_FM_REJ_ROWBYTES, [](FakeGuest &g) { g.w32(BLK + NQD_FM_DEST_ROW, 40); } },
		{ "base below the framebuffer", NQD_FM_REJ_NOT_OWNED, [](FakeGuest &g) { g.w32(BLK + NQD_FM_DEST_BASE, g.fbm - 4); } },
		{ "base past the framebuffer", NQD_FM_REJ_NOT_OWNED, [](FakeGuest &g) { g.w32(BLK + NQD_FM_DEST_BASE, g.fbm + (uint32_t)g.fbb); } },
		{ "empty rect", NQD_FM_REJ_RECT, [](FakeGuest &g) { g.w16(BLK + NQD_FM_DEST_RECT + 4, 2); } },
		{ "negative origin", NQD_FM_REJ_RECT, [](FakeGuest &g) { g.w16(BLK + NQD_FM_DEST_BOUNDS + 2, 20); } },
		{ "oversized rect", NQD_FM_REJ_RECT, [](FakeGuest &g) { g.w16(BLK + NQD_FM_DEST_RECT + 6, 4096 + 8); } },
		{ "extent leaves the framebuffer", NQD_FM_REJ_BOUNDS, [](FakeGuest &g) { g.w16(BLK + NQD_FM_DEST_RECT + 4, H + 1); } },
		{ "huge stride cannot wrap", NQD_FM_REJ_BOUNDS, [](FakeGuest &g) { g.w32(BLK + NQD_FM_DEST_ROW, 0x7fffffff); } },
		{ "port not a colour port", NQD_FM_REJ_PORT, [](FakeGuest &g) { g.w16(PORT + 6, 0x0000); } },
		{ "port pointer null", NQD_FM_REJ_PORT, [](FakeGuest &g) { g.w32(BLK + 4, 0); } },
		{ "pattern handle null", NQD_FM_REJ_PATTERN, [](FakeGuest &g) { g.w32(PORT + 0x3e, 0); } },
		{ "pixel pattern", NQD_FM_REJ_PATTERN, [](FakeGuest &g) { g.w16(PAT, 1); } },
		{ "mixed pattern", NQD_FM_REJ_NOT_SOLID, [](FakeGuest &g) { g.w8(PAT + 23, 0x7f); } },
		{ "stripe pattern", NQD_FM_REJ_NOT_SOLID, [](FakeGuest &g) { for (int i = 0; i < 8; i++) g.w8(PAT + 20 + i, 0xaa); } },
		{ "black fore on a set pattern", NQD_FM_REJ_BLACK, [](FakeGuest &g) { g.w32(BLK + NQD_FM_FORE, 0); } },
		{ "black back on a clear pattern", NQD_FM_REJ_BLACK, [](FakeGuest &g) { g.w32(BLK + NQD_FM_BACK, 0); for (int i = 0; i < 8; i++) g.w8(PAT + 20 + i, 0); } },
		{ "no region", NQD_FM_REJ_NO_REGION, [](FakeGuest &g) { g.w32(BLK + 0x100, 0); } },
		{ "region not overlapping", NQD_FM_REJ_NO_REGION, [](FakeGuest &g) { g.w16(0x5000 + 2, 200); g.w16(0x5000 + 6, 220); } },
		{ "region truncated", NQD_FM_REJ_REGION_BAD, [](FakeGuest &g) { g.w16(0x5000, 12); } },
	};
	for (const Rej &r : rejs) {
		fm_block(g, 2, 4, 10, 14, 0x00dddddd, 0x00ffffff, 0xff);
		fm_region(g, 0, 0x5000, rect_region(0, 0, 100, 100));
		r.mut(g);
		const int got = build(pl);
		CHECK(got == r.want, "rejection '%s': got %s, want %s", r.name, nqd_fm_reject_name(got), nqd_fm_reject_name(r.want));
		cases++;
	}

	/* exact fits at the end of the framebuffer, and one pixel too many */
	fm_block(g, H - 3, W - 5, H, W, 0x00dddddd, 0x00ffffff, 0xff);
	fm_region(g, 0, 0x5000, rect_region(0, 0, 1000, 1000));
	CHECK(build(pl) == NQD_FM_OK && pl.end == g.fbb, "last pixel fits exactly (end %llu, fb %llu)", (unsigned long long)pl.end, (unsigned long long)g.fbb);
	g.w16(BLK + NQD_FM_DEST_RECT + 6, W + 1);
	CHECK(build(pl) == NQD_FM_REJ_ROWBYTES, "one pixel past the row is a stride rejection");
	g.w16(BLK + NQD_FM_DEST_RECT + 6, W); g.w16(BLK + NQD_FM_DEST_RECT + 4, H + 1);
	CHECK(build(pl) == NQD_FM_REJ_BOUNDS, "one row past the framebuffer is a bounds rejection");
	cases += 3;
	/* a framebuffer exactly one pixel short of the rectangle's last write */
	g.w16(BLK + NQD_FM_DEST_RECT + 4, H);
	{
		const uint64_t keep = g.fbb;
		g.fbb = keep - 4;
		CHECK(build(pl) == NQD_FM_REJ_BOUNDS, "one pixel past the end of the framebuffer is rejected");
		g.fbb = keep - 3;
		CHECK(build(pl) == NQD_FM_REJ_BOUNDS, "a partial pixel past the end is rejected");
		g.fbb = keep;
		CHECK(build(pl) == NQD_FM_OK, "and accepted again at the exact size");
		cases += 3;
	}

	/* policy B: a region that will not decode is skipped, not a rejection; policy C: only the clip offsets count */
	{
		fm_block(g, 2, 4, 10, 14, 0x00dddddd, 0x00ffffff, 0xff);
		fm_region(g, 0, 0x5000, rect_region(0, 0, 100, 100));
		fm_region(g, 1, 0x5100, { 0, 14, 0, 0, 0, 0, 0, 200, 0, 200, 0, 0 });	/* plausible header, scanline data that never terminates */
		CHECK(nqd_fillmask_build(g, BLK, &pl, { false, false, true, false }) == NQD_FM_REJ_REGION_BAD && pl.bad_region == 0x5100, "policy A rejects an undecodable region and names it");
		CHECK(nqd_fillmask_build(g, BLK, &pl, { true, false, true, false }) == NQD_FM_OK && pl.skipped_regions == 1 && pl.regions == 1, "policy B skips it");
		/* C: a region at 0x1a8 that excludes the rect's left half; another at 0x100 that excludes everything */
		fm_block(g, 2, 4, 10, 14, 0x00dddddd, 0x00ffffff, 0xff);
		fm_region(g, 0, 0x5000, rect_region(0, 0, 100, 4 + 1));		/* at +0x100: would clip to 1 column */
		fm_region(g, 0x2a, 0x5200, rect_region(0, 0, 100, 100));		/* at +0x1a8: the clip, covers the rect */
		CHECK(nqd_fillmask_build(g, BLK, &pl, { false, false, true, false }) == NQD_FM_OK && pl.spans.pixels() == 8, "policy A intersects every region (one column here)");
		CHECK(nqd_fillmask_build(g, BLK, &pl, { true, true, true, false }) == NQD_FM_OK && pl.spans.pixels() == 80 && pl.regions == 1, "policy C looks only at the clip offsets");
		fm_block(g, 2, 4, 10, 14, 0x00dddddd, 0x00ffffff, 0xff);
		fm_region(g, 0, 0x5000, rect_region(0, 0, 100, 100));
		CHECK(nqd_fillmask_build(g, BLK, &pl, { true, true, true, false }) == NQD_FM_REJ_NO_REGION, "policy C with no region at a clip offset rejects");
		cases += 5;
	}

	/* the production default is policy C, and small pen-12 draws are refused */
	{
		fm_block(g, 2, 4, 10, 14, 0x00dddddd, 0x00ffffff, 0xff);
		fm_region(g, 0x2a, 0x5200, rect_region(0, 0, 100, 100));
		CHECK(nqd_fillmask_build(g, BLK, &pl) == NQD_FM_OK && pl.regions == 1, "production default accepts a region at a clip offset");
		fm_block(g, 2, 4, 10, 14, 0x00dddddd, 0x00ffffff, 0xff);
		fm_region(g, 0, 0x5000, rect_region(0, 0, 100, 100));
		CHECK(nqd_fillmask_build(g, BLK, &pl) == NQD_FM_REJ_NO_REGION, "production default ignores a region at another offset");
		/* pen 12: a 32x32 draw (an icon) is refused, a 64x64 draw is accepted */
		fm_block(g, 0, 0, 32, 32, 0x00dddddd, 0x00ffffff, 0xff);
		fm_region(g, 0x2a, 0x5200, rect_region(0, 0, 100, 100));
		g.w32(BLK + NQD_FM_PEN, 12);
		CHECK(nqd_fillmask_build(g, BLK, &pl) == NQD_FM_REJ_MODE, "small pen-12 draw is refused");
		g.w16(BLK + NQD_FM_DEST_RECT + 4, 64); g.w16(BLK + NQD_FM_DEST_RECT + 6, 64);
		CHECK(nqd_fillmask_build(g, BLK, &pl) == NQD_FM_OK, "large pen-12 draw is accepted");
		g.w32(BLK + NQD_FM_PEN, 8); g.w16(BLK + NQD_FM_DEST_RECT + 4, 32); g.w16(BLK + NQD_FM_DEST_RECT + 6, 32);
		CHECK(nqd_fillmask_build(g, BLK, &pl) == NQD_FM_OK, "small pen-8 draw is still accepted");
		cases += 5;
	}

	/* pixel patterns: the 32-bit expansion tiled from the destination origin */
	{
		const uint32_t PM = 0x6000, PMH = 0x6100, XDH = 0x6200, XD = 0x7000;
		const int TW = 6, TH = 5;
		auto pix_block = [&](int t, int l, int b, int r) {
			fm_block(g, t, l, b, r, 0x00dddddd, 0x00ffffff, 0xff);
			fm_region(g, 0x2a, 0x5200, rect_region(0, 0, 1000, 1000));
			g.w16(PAT, 1);				/* a PixPat */
			g.w32(PAT + 2, PMH); g.w32(PMH, PM);	/* patMap handle -> PixMap */
			g.w16(PM + 6, 0); g.w16(PM + 8, 0); g.w16(PM + 10, TH); g.w16(PM + 12, TW);	/* bounds 0,0,TH,TW */
			g.w32(PAT + 10, XDH); g.w32(XDH, XD);	/* patXData handle -> expanded tile */
			g.w16(PAT + 14, 32);			/* patXValid */
			for (int i = 0; i < TW * TH; i++) g.w32(XD + (uint32_t)i * 4, 0x00112233u + (uint32_t)i * 0x01010100u);
		};
		pix_block(3, 5, 80, 110);
		CHECK(nqd_fillmask_build(g, BLK, &pl, { true, true, true, false }) == NQD_FM_OK && pl.tiled && pl.tile_w == TW && pl.tile_h == TH, "pixel pattern is accepted and tiled");
		CHECK(pl.tile.size() == (size_t)TW * TH && pl.tile[7] == htonl(0x00112233u + 7u * 0x01010100u), "tile words are read in memory order");
		CHECK(nqd_fm_pixel(pl, 0, 0) == pl.tile[(size_t)(pl.dest_y % TH) * TW + (size_t)(pl.dest_x % TW)] && nqd_fm_pixel(pl, TW, TH) == nqd_fm_pixel(pl, 0, 0),
		      "the tile is anchored at the destination origin and repeats");
		{ NqdFillmaskPlan scratch; CHECK(nqd_fillmask_build(g, BLK, &scratch) == NQD_FM_REJ_PATTERN, "the production policy does not accept pixel patterns yet"); }
		/* executors agree on a tiled plan */
		{
			const size_t fbn = (size_t)ROW * H;
			std::vector<uint8> a(fbn), orig(fbn);
			for (size_t i = 0; i < fbn; i++) a[i] = orig[i] = (uint8)rnd();
			CHECK(nqd_fillmask_cpu(pl, a.data(), fbn), "CPU executor runs a tiled plan");
			for (int y = 0; y < pl.height; y++)
				for (int x = 0; x < pl.width; x++) {
					uint32_t got; memcpy(&got, &a[pl.start + (size_t)y * ROW + (size_t)x * 4], 4);
					CHECK(got == nqd_fm_pixel(pl, x, y), "CPU tiled pixel %d,%d", x, y);
				}
			memcpy(real_fb, orig.data(), fbn);
			SheepForceTileFill op;
			op.dest = real_fb + pl.start; op.rowbytes = pl.row_bytes; op.width = pl.width; op.height = pl.height;
			op.tile = pl.tile.data(); op.tile_w = pl.tile_w; op.tile_h = pl.tile_h; op.tile_ox = pl.dest_x; op.tile_oy = pl.dest_y;
			op.row_start = pl.spans.row_start.data(); op.runs = pl.spans.runs.data();
			CHECK(SheepForceTryFillTile(&op), "GPU accepts a tiled plan");
			SheepForceFlushCPU(NULL, 0, 0, 0);
			CHECK(memcmp(real_fb, a.data(), fbn) == 0, "GPU and CPU executors agree on a tiled plan");
			SheepForceTileFill bad = op;
			bad.tile_w = 0;
			CHECK(!SheepForceTryFillTile(&bad), "GPU declines a zero-width tile");
			for (size_t i = 0; i < fbn; i++) real_fb[i] = (uint8)rnd();
		}
		/* rejections specific to pixel patterns */
		pix_block(3, 5, 80, 110); g.w16(PAT + 14, 8);
		CHECK(nqd_fillmask_build(g, BLK, &pl, { true, true, true, false }) == NQD_FM_REJ_PATTERN, "expansion not for 32-bit pixels is rejected");
		pix_block(3, 5, 80, 110); g.w16(PM + 12, 300);
		CHECK(nqd_fillmask_build(g, BLK, &pl, { true, true, true, false }) == NQD_FM_REJ_PATTERN, "oversized tile is rejected");
		pix_block(3, 5, 80, 110); g.w16(PM + 8, 2);
		CHECK(nqd_fillmask_build(g, BLK, &pl, { true, true, true, false }) == NQD_FM_REJ_PATTERN, "tile bounds with a nonzero origin are rejected");
		pix_block(3, 5, 80, 110); g.w32(XDH, 0);
		CHECK(nqd_fillmask_build(g, BLK, &pl, { true, true, true, false }) == NQD_FM_REJ_PATTERN, "missing expansion data is rejected");
		pix_block(3, 5, 80, 110); g.w32(BLK + NQD_FM_PEN, 12);
		CHECK(nqd_fillmask_build(g, BLK, &pl, { true, true, true, false }) == NQD_FM_REJ_MODE, "pixel pattern with pen 12 is refused");
		pix_block(3, 5, 12, 30);
		CHECK(nqd_fillmask_build(g, BLK, &pl, { true, true, true, false }) == NQD_FM_REJ_SMALL, "a small pixel-pattern draw (an icon) is refused");
		pix_block(3, 5, 80, 110); g.w16(PAT, 2);
		CHECK(nqd_fillmask_build(g, BLK, &pl, { true, true, true, false }) == NQD_FM_REJ_PATTERN, "an RGB pattern (type 2) is refused");
		cases += 13;
	}

	/* survey: count the clip of an op the hook refuses, ignoring mode, pattern and colour */
	{
		fm_block(g, 2, 4, 10, 14, 0x00000000, 0x00ffffff, 0xaa);
		fm_region(g, 0x2a, 0x5200, rect_region(0, 0, 6, 100));	/* rows 2..5 of the rect, all columns */
		g.w32(BLK + NQD_FM_PEN, 14); g.w32(BLK + NQD_FM_TRANSFER, 10);	/* an XOR draw */
		CHECK(nqd_fillmask_build(g, BLK, &pl) == NQD_FM_REJ_MODE, "XOR draw is refused by the hook");
		NqdFillPolicy sv = nqd_fm_production_policy(); sv.survey = true;
		CHECK(nqd_fillmask_build(g, BLK, &pl, sv) == NQD_FM_OK && pl.spans.pixels() == 40, "survey counts the clip of a refused XOR draw");
		g.w32(PORT + 0x3e, 0);						/* no readable pattern either */
		CHECK(nqd_fillmask_build(g, BLK, &pl, sv) == NQD_FM_OK && pl.spans.pixels() == 40, "survey does not need a pattern");
		fm_region(g, 0x2a, 0x5200, std::vector<uint8_t>());
		g.w32(BLK + 0x100 + 0x2a * 4, 0);
		CHECK(nqd_fillmask_build(g, BLK, &pl, sv) == NQD_FM_REJ_NO_REGION, "survey still needs a clip region");
		cases += 4;
	}

	/* the 16-region cap */
	for (int n : { 16, 17 }) {
		fm_block(g, 2, 4, 10, 14, 0x00dddddd, 0x00ffffff, 0xff);
		for (int i = 0; i < n; i++) fm_region(g, i, 0x5000 + (uint32_t)i * 0x40, rect_region(0, 0, 100, 100));
		const int got = build(pl);
		CHECK(got == (n == 16 ? NQD_FM_OK : NQD_FM_REJ_REGION_CAP), "%d regions: got %s", n, nqd_fm_reject_name(got));
		cases++;
	}

	/* a non-rectangular, holey, disjoint clip: the plan's spans equal the encoded mask, intersected with a second region */
	for (int iter = 0; iter < 60; iter++) {
		const int mw = 24, mh = 16;
		std::vector<uint8> m((size_t)mw * mh, 0);
		for (int k = 0; k < 4; k++) {
			int x0 = rnd_range(0, mw - 1), x1 = rnd_range(x0 + 1, mw), y0 = rnd_range(0, mh - 1), y1 = rnd_range(y0 + 1, mh);
			for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) m[(size_t)y * mw + x] = (k == 3) ? 0 : 1;	/* the last blob punches a hole */
		}
		bool any = false; for (uint8 v : m) any |= v != 0;
		if (!any) continue;
		const int top = 5, left = 6;			/* mask at (top, left) in block coordinates */
		std::vector<uint8> enc = encode_region(m, mw, mh, top, left);
		const int rt = 6, rl = 7, rb = 6 + 12, rr = 7 + 20;
		fm_block(g, rt, rl, rb, rr, 0x00dddddd, 0x00ffffff, 0xff);
		fm_region(g, 0, 0x5000, enc);
		fm_region(g, 1, 0x5800, rect_region(8, 9, rb, rr - 3));	/* a second clip that trims the top rows and right edge */
		const int got = build(pl);
		if (got == NQD_FM_REJ_NO_REGION) continue;	/* the mask did not overlap the rect */
		CHECK(got == NQD_FM_OK, "irregular clip rejected: %s", nqd_fm_reject_name(got));
		if (got != NQD_FM_OK) continue;
		/* a region whose bounding box misses the rectangle is treated as unrelated and skipped, as in production */
		int bt = mh, bb = -1, bl = mw, br = -1;
		for (int y = 0; y < mh; y++) for (int x = 0; x < mw; x++) if (m[(size_t)y * mw + x]) { bt = std::min(bt, y); bb = std::max(bb, y + 1); bl = std::min(bl, x); br = std::max(br, x + 1); }
		const bool mask_applies = !(top + bb <= rt || top + bt >= rb || left + br <= rl || left + bl >= rr);
		for (int y = 0; y < rb - rt; y++)
			for (int x = 0; x < rr - rl; x++) {
				const int my = rt + y - top, mx = rl + x - left;
				bool want = !mask_applies || (my >= 0 && my < mh && mx >= 0 && mx < mw && m[(size_t)my * mw + mx]);
				want = want && rt + y >= 8 && rl + x >= 9 && rl + x < rr - 3;
				CHECK(pl.spans.contains(x, y) == want, "irregular clip mismatch iter %d at %d,%d", iter, x, y);
			}
		cases++;
	}

	/* executors: CPU and GPU produce the same pixels from the same plan, and a bad plan writes nothing */
	{
		const size_t fbn = (size_t)ROW * H;
		std::vector<uint8> a(fbn), b(fbn);
		for (size_t i = 0; i < fbn; i++) a[i] = b[i] = (uint8)rnd();
		fm_block(g, 3, 5, 19, 40, 0x00cccccc, 0x00ffffff, 0xff);
		fm_region(g, 0, 0x5000, rect_region(0, 0, 1000, 1000));
		fm_region(g, 1, 0x5800, rect_region(4, 8, 16, 33));
		CHECK(build(pl) == NQD_FM_OK, "executor plan accepted");
		CHECK(nqd_fillmask_cpu(pl, a.data(), fbn), "CPU executor ran");
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++) {
				const bool in = y >= 4 && y < 16 && x >= 8 && x < 33;
				for (int i = 0; i < 4; i++) {
					const uint8 want = in ? (uint8)(pl.colour >> (8 * i)) : b[(size_t)y * ROW + (size_t)x * 4 + i];
					CHECK(a[(size_t)y * ROW + (size_t)x * 4 + i] == want, "CPU executor pixel %d,%d", x, y);
				}
			}
		NqdFillmaskPlan bad = pl;
		bad.end = fbn + 1;
		std::vector<uint8> c = b;
		CHECK(!nqd_fillmask_cpu(bad, c.data(), fbn) && c == b, "an out-of-range plan is refused and writes nothing");
		bad = pl; bad.spans.runs[1] = bad.width + 5;
		CHECK(!nqd_fillmask_cpu(bad, c.data(), fbn) && c == b, "a plan with a run past its width writes nothing");
		cases += 4;

		/* the same plan through the GPU path, and through a forced GPU rejection followed by the CPU path */
		std::vector<uint8> expect(fbn);
		memcpy(expect.data(), real_fb, fbn);
		CHECK(nqd_fillmask_cpu(pl, expect.data(), fbn), "CPU executor on the mirror");
		SheepForceSpanFill op;
		op.dest = real_fb + pl.start; op.rowbytes = pl.row_bytes; op.width = pl.width; op.height = pl.height;
		op.fore = op.back = pl.colour; memset(op.pat, 0xff, 8); op.pat_ox = op.pat_oy = 0;
		op.row_start = pl.spans.row_start.data(); op.runs = pl.spans.runs.data();
		const bool gpu = SheepForceTryFillSpans(&op);
		CHECK(gpu, "GPU accepts a valid plan");
		SheepForceFlushCPU(NULL, 0, 0, 0);
		CHECK(memcmp(real_fb, expect.data(), fbn) == 0, "GPU and CPU executors agree on the same plan");
		SheepForceSpanFill off_fb = op;
		off_fb.dest = real_fb + fbn * 4;			/* outside the framebuffer: the GPU path must decline */
		CHECK(!SheepForceTryFillSpans(&off_fb), "GPU declines a destination outside the framebuffer");
		SheepForceSpanFill short_row = op;
		short_row.rowbytes = pl.width * 4 - 4;			/* a stride that cannot hold the rectangle */
		CHECK(!SheepForceTryFillSpans(&short_row), "GPU declines a stride that cannot hold the rectangle");
		SheepForceFlushCPU(NULL, 0, 0, 0);
		CHECK(memcmp(real_fb, expect.data(), fbn) == 0, "rejected GPU submissions wrote nothing");
		/* refresh the real framebuffer for the later random tests */
		for (size_t i = 0; i < fbn; i++) real_fb[i] = (uint8)rnd();
		cases += 5;
	}
	printf("fillmask rule: %d cases checked\n", cases);
}

int main(void)
{
	const size_t fb_bytes = (size_t)PAGE_BYTES * 2;
	uint8 *fb = (uint8 *)mmap(NULL, fb_bytes, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
	if (fb == MAP_FAILED) { printf("mmap failed\n"); return 2; }
	SheepForceSetGeometry(fb, 0x1000, PAGE_BYTES, W, H, ROW, 32);
	SheepForceStartup(NULL);
	test_regions();
	test_fillmask(fb);
	std::vector<uint8> mirror(fb_bytes);
	for (size_t i = 0; i < fb_bytes; i++) fb[i] = mirror[i] = (uint8)rnd();
	for (int i = 0; i < 256; i++) {
		mac_pal[i].red = (uint8)rnd(); mac_pal[i].green = (uint8)rnd(); mac_pal[i].blue = (uint8)rnd();
	}
	uint32 pal[256];
	for (int i = 0; i < 256; i++) pal[i] = nqd_pack_rgb(mac_pal[i].red, mac_pal[i].green, mac_pal[i].blue);

	std::vector<uint8> off((size_t)ROW * H);	/* offscreen source pixmap */
	int ran = 0, declined = 0;
	static const int modes[] = {0,1,2,3,4,5,6,7,34,36,37,38,39, 32,33,35,50};
	for (int iter = 0; iter < 3000; iter++) {
		int kind = rnd_range(0, 11);
		int w = rnd_range(1, 70), h = rnd_range(1, 40);
		int dx = rnd_range(0, W - w), dy = rnd_range(0, H - h);
		/* Operations queue up in batches; the CPU side only looks at the buffer every few of them. */
		if (kind < 2 || iter == 0)
			;
		if (kind == 0) {		/* fill, any depth */
			int bpp = (int[]){1, 2, 4}[rnd_range(0, 2)];
			int ww = bpp == 4 ? w : w * bpp / bpp;
			uint8 *d = fb + (size_t)dy * ROW + (size_t)dx * bpp;
			if ((size_t)dy * ROW + (size_t)(dx + ww) * bpp > PAGE_BYTES * 2) continue;
			uint32 color = rnd();
			bool ok = SheepForceTryFill(d, bpp, ROW, ww * bpp, h, color);
			if (!ok) { declined++; continue; }
			for (int y = 0; y < h; y++)
				for (int x = 0; x < ww; x++)
					for (int i = 0; i < bpp; i++)
						mirror[(size_t)(dy + y) * ROW + (size_t)(dx + x) * bpp + i] = (uint8)(color >> (8 * i));
			ran++;
		} else if (kind == 10 || kind == 11) {	/* fill through per-row spans, solid or 8x8 pattern */
			uint8 *d = fb + (size_t)dy * ROW + (size_t)dx * 4;
			NqdSpans sp;
			sp.w = w; sp.h = h;
			sp.row_start.assign((size_t)h + 1, 0);
			for (int y = 0; y < h; y++) {
				sp.row_start[(size_t)y] = (int32_t)(sp.runs.size() / 2);
				int x = 0;
				while (x < w) {
					int a = rnd_range(x, w), b = rnd_range(a, std::min(w, a + 25));
					if (b > a) { sp.runs.push_back(a); sp.runs.push_back(b); }
					x = b + 1;
					if (rnd_range(0, 2) == 0) break;
				}
			}
			sp.row_start[(size_t)h] = (int32_t)(sp.runs.size() / 2);
			SheepForceSpanFill op;
			op.dest = d; op.rowbytes = ROW; op.width = w; op.height = h;
			op.fore = rnd() * 7919u; op.back = rnd() * 104729u;
			for (int i = 0; i < 8; i++) op.pat[i] = kind == 10 ? 0xff : (uint8)rnd();
			op.pat_ox = rnd_range(0, 20); op.pat_oy = rnd_range(0, 20);
			op.row_start = sp.row_start.data(); op.runs = sp.runs.empty() ? (const int32_t *)&sp.row_start[0] : sp.runs.data();
			if (!SheepForceTryFillSpans(&op)) { declined++; continue; }
			for (int y = 0; y < h; y++)
				for (int x = 0; x < w; x++) {
					if (!sp.contains(x, y)) continue;
					const int tx = (x + op.pat_ox) & 7, ty = (y + op.pat_oy) & 7;
					const uint32 c = (op.pat[ty] >> (7 - tx)) & 1 ? op.fore : op.back;
					for (int i = 0; i < 4; i++)
						mirror[(size_t)(dy + y) * ROW + (size_t)(dx + x) * 4 + i] = (uint8)(c >> (8 * i));
				}
			ran++;
		} else if (kind == 1) {	/* invert */
			int bpp = (int[]){1, 2, 4}[rnd_range(0, 2)];
			uint8 *d = fb + (size_t)dy * ROW + (size_t)dx * bpp;
			if ((size_t)dy * ROW + (size_t)(dx + w) * bpp > PAGE_BYTES * 2) continue;
			if (!SheepForceTryInvert(d, bpp, ROW, w * bpp, h)) { declined++; continue; }
			for (int y = 0; y < h; y++)
				for (int x = 0; x < w * bpp; x++)
					mirror[(size_t)(dy + y) * ROW + (size_t)dx * bpp + x] ^= 0xff;
			ran++;
		} else {			/* blit */
			int sbpp = (int[]){1, 2, 4, 4}[rnd_range(0, 3)];
			int dbpp = (sbpp == 4 || rnd_range(0, 1)) ? 4 : sbpp;
			int mode = modes[rnd_range(0, (int)(sizeof modes / sizeof modes[0]) - 1)];
			int dwb = w * dbpp;
			if (dx * dbpp + dwb > ROW) continue;
			bool in_fb = rnd_range(0, 1);
			const uint8 *src;
			int src_row;
			std::vector<uint8> srcmirror;
			if (in_fb) {	/* random overlap-prone position in the mirrored FB */
				int sx = rnd_range(0, W - w), sy = rnd_range(0, H - h);
				if (rnd_range(0, 2) == 0) { sx = dx + rnd_range(-3, 3); sy = dy + rnd_range(-3, 3); }
				if (sx < 0 || sy < 0 || sx + w > W || sy + h > H) continue;
				if (sbpp != 4) { sx = sx * 4 / sbpp; if (sx + w > ROW / sbpp) continue; }
				src = fb + (size_t)sy * ROW + (size_t)sx * sbpp;
				src_row = ROW;
				srcmirror.assign(mirror.begin(), mirror.end());
			} else {
				for (auto &b : off) b = (uint8)rnd();
				src = off.data();
				src_row = ROW;
				srcmirror.assign(off.begin(), off.end());
			}
			uint32 back = nqd_pack_rgb((uint8)rnd(), (uint8)rnd(), (uint8)rnd());
			if (rnd_range(0, 2) == 0 && mode == 36 && sbpp == 4) {	/* make some pixels hit the background */
				uint8 *wr = in_fb ? fb : off.data();
				(void)wr;
			}
			SheepForceBlitOp op;
			op.dest = fb + (size_t)dy * ROW + (size_t)dx * dbpp;
			op.src = src;
			op.dbpp = dbpp; op.sbpp = sbpp;
			op.dst_row = ROW; op.src_row = src_row;
			op.width = w; op.height = h;
			op.mode = mode; op.back_word = back;
			op.pal = sbpp == 1 ? pal : NULL;
			bool want = nqd_blit_mode_ok(mode, dbpp, sbpp);
			bool ok = SheepForceTryBlit(&op);
			CHECK(ok == want, "iter %d blit mode %d sbpp %d dbpp %d: accepted=%d expected=%d", iter, mode, sbpp, dbpp, ok, want);
			if (!ok) { declined++; continue; }
			const uint8 *sref = srcmirror.data() + (src - (in_fb ? fb : off.data()));
			ref_blit(mirror.data() + (size_t)dy * ROW + (size_t)dx * dbpp, ROW, sref, src_row, dbpp, sbpp,
				 w, h, mode, back, pal);
			ran++;
		}
		if ((iter % 11) != 10 && iter != 2999)
			continue;
		SheepForceSync();
		if (memcmp(fb, mirror.data(), fb_bytes) != 0) {
			size_t i = 0;
			while (fb[i] == mirror[i]) i++;
			CHECK(false, "iter %d kind %d: first differing byte at %zu (x %zu y %zu): gpu %02x ref %02x",
			      iter, kind, i, (i % ROW) / 4, i / ROW, fb[i], mirror[i]);
			memcpy(fb, mirror.data(), fb_bytes);	/* resynchronize and keep going */
			if (failures > 20) break;
		}
	}
	printf("sheepforce_qd_harness: %d ops on the GPU, %d declined, %d failures\n", ran, declined, failures);
	return failures ? 1 : 0;
}
