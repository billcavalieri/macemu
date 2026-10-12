/*
 *  nqd_fillmask.h - the QuickDraw hook code 3 (ACCL_FILLMASK) acceptance rule, as one pure function
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
 *  What the probe runs on a live guest established (see ACCEL-COVERAGE.md):
 *    - the parameter block is QuickDraw's own drawing frame. It holds the pattern the ROM is about to paint,
 *      already expanded to the destination's pixel format and already resolved for the pen mode and the fore
 *      and back colours:
 *          +0x294  pointer to the expanded pattern (a solid colour's 16 words lie in the block itself, at +0x2ac)
 *          +0x298  bytes per pattern row
 *          +0x29c  horizontal mask: row bytes - 4
 *          +0x2a0  vertical mask: pattern bytes - 4
 *          +0x2a4  horizontal position: the byte offset in a row of the rectangle's first pixel
 *          +0x2a8  vertical position: the byte offset of the rectangle's first row
 *      so rectangle pixel (x, y) is the word at  ((vpos + y * row) & vmask) + ((hpos + x * 4) & hmask).
 *      Which of the port's patterns that came from (pen, background or fill) and which pen mode asked for it
 *      no longer matters; reading the port's fillPixPat, as this rule first did, was right only for fills.
 *    - the clip is the intersection of the regions at a few fixed block offsets that overlap the rectangle.
 *  A plan is built for a transfer mode of 8 (copy) with pen mode 8 or 12, a 32-bit destination inside the
 *  no-copy framebuffer, a well-formed pattern of at most 256 x 256 pixels, and at least one but at most 16
 *  clip regions. Anything else is a rejection with a reason, and the ROM draws.
 *
 *  The function is a template over a guest-memory accessor so the emulator, the probe and the harness
 *  run the identical code. G must provide:
 *      uint32_t r8(uint32_t), r16(uint32_t), r32(uint32_t)   big-endian guest reads
 *      bool     owns(uint32_t mac)                           inside the no-copy framebuffer
 *      uint32_t fb_mac()                                     guest address of the framebuffer start
 *      uint64_t fb_bytes()                                   its length
 */
#ifndef NQD_FILLMASK_H
#define NQD_FILLMASK_H

#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>
#include "nqd_region.h"

/* Block offsets (the same as accl_params in video_defs.h; gfxaccel.cpp static_asserts the match). */
enum {
	NQD_FM_TRANSFER   = 0x0c,
	NQD_FM_PEN        = 0x10,
	NQD_FM_FORE       = 0x1c,
	NQD_FM_BACK       = 0x20,
	NQD_FM_DEST_BASE  = 0x64,
	NQD_FM_DEST_ROW   = 0x68,
	NQD_FM_DEST_BOUNDS= 0x6c,
	NQD_FM_DEST_PIXEL = 0x7c,
	NQD_FM_DEST_RECT  = 0xd4,
	NQD_FM_EXPAT      = 0x294,
	NQD_FM_PATROW     = 0x298,
	NQD_FM_PATHMASK   = 0x29c,
	NQD_FM_PATVMASK   = 0x2a0,
	NQD_FM_PATHPOS    = 0x2a4,
	NQD_FM_PATVPOS    = 0x2a8,
	NQD_FM_BLOCK_SIZE = 0x500,
	NQD_FM_MAX_REGIONS = 16,
	NQD_FM_MAX_DIM    = 4096,
	NQD_FM_MAX_TILE   = 256
};

enum NqdFillReject {
	NQD_FM_OK = 0,
	NQD_FM_REJ_MODE,		/* transfer mode is not 8, or pen mode is not 8 or 12 */
	NQD_FM_REJ_DEPTH,		/* destination is not 32 bits per pixel */
	NQD_FM_REJ_ROWBYTES,		/* row bytes is not a positive multiple that holds the rectangle */
	NQD_FM_REJ_NOT_OWNED,		/* destination base is not the no-copy framebuffer */
	NQD_FM_REJ_RECT,		/* empty, oversized or negative-origin rectangle */
	NQD_FM_REJ_BOUNDS,		/* the write extent leaves the framebuffer */
	NQD_FM_REJ_PATTERN,		/* the expanded pattern is missing, malformed or larger than 256 x 256 */
	NQD_FM_REJ_TILED,		/* a pattern of more than one colour, and the policy takes only solid ones */
	NQD_FM_REJ_REGION_BAD,		/* a region that overlaps could not be decoded */
	NQD_FM_REJ_REGION_CAP,		/* more than 16 overlapping regions */
	NQD_FM_REJ_NO_REGION,		/* no region found: the clip is unknown */
	NQD_FM_REJ_COUNT
};

static inline const char *nqd_fm_reject_name(int r)
{
	static const char *names[NQD_FM_REJ_COUNT] = { "accepted", "mode", "depth", "rowbytes", "not-owned", "rect", "bounds",
		"pattern", "tiled", "region-bad", "region-cap", "no-region" };
	return r >= 0 && r < NQD_FM_REJ_COUNT ? names[r] : "?";
}

/* Which block regions count as clip, and what a region that will not decode means. */
struct NqdFillPolicy {
	bool skip_bad_regions;		/* treat an undecodable region as not a region (instead of rejecting) */
	bool clip_offsets_only;		/* only regions at the offsets that repeatedly held the real clip */
	bool allow_tiled;		/* accept patterns of more than one colour (pref sheepforce_fillmask_tiles) */
	bool survey;			/* count only: skip the mode/pattern/colour checks so the clip's pixel count can be reported for refused ops */
};
/* The policy the hook uses. */
static inline NqdFillPolicy nqd_fm_production_policy(void)
{
	NqdFillPolicy p = { true, true, true, false };
	return p;
}
static inline bool nqd_fm_clip_offset(uint32_t off)
{
	return off == 0x1a4 || off == 0x1a8 || off == 0x1ac || off == 0x1e0 || off == 0x20c;
}

struct NqdFillmaskPlan {
	uint32_t block;			/* the block address the plan was built for */
	int dest_x, dest_y, width, height;
	int32_t row_bytes;
	uint32_t dest_base;
	uint32_t fore, back;		/* the block's pens, bytes in memory order (reported only; the pattern already has them) */
	uint32_t colour;		/* a solid pattern's colour, bytes in memory order as SheepForceTryFill takes them */
	uint32_t pen;
	int regions;
	bool tiled;			/* more than one colour: paint the tile, not the solid colour */
	int tile_w, tile_h;		/* tile size in pixels */
	int tile_ox, tile_oy;		/* the tile pixel under the rectangle's first pixel */
	std::vector<uint32_t> tile;	/* tile_h * tile_w pixel words, memory order like colour; tile pixel for rect pixel (x, y) is ((x + tile_ox) mod w, (y + tile_oy) mod h) */
	int skipped_regions;		/* undecodable regions skipped under skip_bad_regions */
	uint32_t bad_region;		/* the region that caused a REGION_BAD / REGION_CAP rejection */
	uint64_t start, end;		/* byte offsets in the framebuffer: first pixel and one past the last written byte */
	NqdSpans spans;
};

/* Guest pointers read from the parameter block are only followed below this address. The default suits a 512 MB
 * guest; the emulator lowers it to the real RAM size (a pointer between the end of RAM and the old fixed limit
 * made the host read unmapped memory and crash with a small RAM setting). */
static inline uint32_t &nqd_fm_limit()
{
	static uint32_t limit = 0x1ff00000u;
	return limit;
}
static inline void nqd_fm_set_ram_size(uint32_t ram_size)
{
	if (ram_size > 0x20000u && ram_size - 0x10000u < nqd_fm_limit())
		nqd_fm_limit() = ram_size - 0x10000u;
}
static inline bool nqd_fm_ptr_ok(uint32_t a)
{
	return a >= 0x1000 && a < nqd_fm_limit() && (a & 1) == 0;
}

/* The write extent of the plan's rectangle: false on a stride that cannot hold it or on leaving fb_bytes. */
static inline bool nqd_fm_extent(uint32_t base, uint32_t fb_mac, uint64_t fb_bytes, int32_t row, int x, int y, int w, int h,
				 uint64_t *start, uint64_t *end)
{
	if (row < 4 || x < 0 || y < 0 || w < 1 || h < 1 || base < fb_mac)
		return false;
	if ((uint64_t)(x + w) * 4u > (uint64_t)row)
		return false;
	const uint64_t s = (uint64_t)(base - fb_mac) + (uint64_t)y * (uint64_t)row + (uint64_t)x * 4u;
	const uint64_t e = s + (uint64_t)(h - 1) * (uint64_t)row + (uint64_t)w * 4u;
	if (e > fb_bytes)
		return false;
	*start = s;
	*end = e;
	return true;
}

template <class G>
static int nqd_fillmask_build(G &g, uint32_t p, NqdFillmaskPlan *pl, NqdFillPolicy pol = nqd_fm_production_policy())
{
	pl->block = p;
	pl->regions = 0;
	pl->skipped_regions = 0;
	pl->bad_region = 0;
	const uint32_t pen = g.r32(p + NQD_FM_PEN), xfer = g.r32(p + NQD_FM_TRANSFER);
	if (!pol.survey && (xfer != 8 || (pen != 8 && pen != 12)))
		return NQD_FM_REJ_MODE;
	pl->pen = pen;
	if (g.r32(p + NQD_FM_DEST_PIXEL) != 32)
		return NQD_FM_REJ_DEPTH;
	pl->row_bytes = (int32_t)g.r32(p + NQD_FM_DEST_ROW);
	if (pl->row_bytes < 4)
		return NQD_FM_REJ_ROWBYTES;
	pl->dest_base = g.r32(p + NQD_FM_DEST_BASE);
	if (!g.owns(pl->dest_base))
		return NQD_FM_REJ_NOT_OWNED;
	const int rt = (int16_t)g.r16(p + NQD_FM_DEST_RECT + 0), rl = (int16_t)g.r16(p + NQD_FM_DEST_RECT + 2);
	const int rb = (int16_t)g.r16(p + NQD_FM_DEST_RECT + 4), rr = (int16_t)g.r16(p + NQD_FM_DEST_RECT + 6);
	pl->width = rr - rl;
	pl->height = rb - rt;
	if (pl->width <= 0 || pl->height <= 0 || pl->width > NQD_FM_MAX_DIM || pl->height > NQD_FM_MAX_DIM)
		return NQD_FM_REJ_RECT;
	pl->dest_x = rl - (int16_t)g.r16(p + NQD_FM_DEST_BOUNDS + 2);
	pl->dest_y = rt - (int16_t)g.r16(p + NQD_FM_DEST_BOUNDS + 0);
	if (pl->dest_x < 0 || pl->dest_y < 0)
		return NQD_FM_REJ_RECT;
	if ((uint64_t)(pl->dest_x + pl->width) * 4u > (uint64_t)pl->row_bytes)
		return NQD_FM_REJ_ROWBYTES;
	if (!nqd_fm_extent(pl->dest_base, g.fb_mac(), g.fb_bytes(), pl->row_bytes, pl->dest_x, pl->dest_y, pl->width, pl->height,
			   &pl->start, &pl->end))
		return NQD_FM_REJ_BOUNDS;

	/* The pattern, as the ROM expanded it for this draw. */
	pl->tiled = false;
	pl->tile.clear();
	pl->tile_w = pl->tile_h = 1;
	pl->tile_ox = pl->tile_oy = 0;
	pl->fore = htonl(g.r32(p + NQD_FM_FORE));
	pl->back = htonl(g.r32(p + NQD_FM_BACK));
	pl->colour = 0;
	if (!pol.survey) {
		const uint32_t expat = g.r32(p + NQD_FM_EXPAT), row = g.r32(p + NQD_FM_PATROW);
		const uint32_t hmask = g.r32(p + NQD_FM_PATHMASK), vmask = g.r32(p + NQD_FM_PATVMASK);
		const uint32_t hpos = g.r32(p + NQD_FM_PATHPOS), vpos = g.r32(p + NQD_FM_PATVPOS);
		/* Sizes are powers of two (the masks are used as masks), and the positions lie inside them. */
		if (row < 4 || row > NQD_FM_MAX_TILE * 4u || (row & (row - 1)) != 0 || hmask != row - 4)
			return NQD_FM_REJ_PATTERN;
		const uint32_t bytes = vmask + 4;
		if (bytes < row || bytes > row * (uint32_t)NQD_FM_MAX_TILE || (bytes & (bytes - 1)) != 0)
			return NQD_FM_REJ_PATTERN;
		if ((hpos & 3) != 0 || hpos > hmask || (vpos & (row - 1)) != 0 || vpos > vmask)
			return NQD_FM_REJ_PATTERN;
		if (!nqd_fm_ptr_ok(expat) || !nqd_fm_ptr_ok(expat + bytes))
			return NQD_FM_REJ_PATTERN;
		const int tw = (int)(row / 4), th = (int)(bytes / row);
		const uint32_t first = g.r32(expat);
		bool solid = true;
		for (uint32_t i = 4; i < bytes && solid; i += 4)
			solid = ((g.r32(expat + i) ^ first) & 0x00ffffffu) == 0;
		if (solid) {
			pl->colour = htonl(first);
		} else {
			if (!pol.allow_tiled)
				return NQD_FM_REJ_TILED;
			pl->tile_w = tw;
			pl->tile_h = th;
			pl->tile_ox = (int)(hpos / 4);
			pl->tile_oy = (int)(vpos / row);
			pl->tile.resize((size_t)tw * (size_t)th);
			for (size_t i = 0; i < pl->tile.size(); i++)
				pl->tile[i] = htonl(g.r32(expat + (uint32_t)i * 4u));
			pl->tiled = true;
		}
	}

	/* The clip: every region in the block that overlaps the rectangle. */
	auto rd16 = [&g](uint32_t a) -> uint32_t { return g.r16(a); };
	pl->spans.full(pl->width, pl->height);
	int result = NQD_FM_OK;
	for (uint32_t off = 0; off < NQD_FM_BLOCK_SIZE && result == NQD_FM_OK; off += 4) {
		if (pol.clip_offsets_only && !nqd_fm_clip_offset(off))
			continue;
		const uint32_t w = g.r32(p + off);
		if (!nqd_fm_ptr_ok(w))
			continue;
		const uint32_t base = g.r32(w);
		if (!nqd_fm_ptr_ok(base))
			continue;
		const unsigned sz = g.r16(base);
		const int t = (int16_t)g.r16(base + 2), l = (int16_t)g.r16(base + 4);
		const int b = (int16_t)g.r16(base + 6), r = (int16_t)g.r16(base + 8);
		if (sz < 10 || sz > 8192 || b <= t || r <= l)
			continue;			/* not a region */
		if (b <= rt || t >= rb || r <= rl || l >= rr)
			continue;			/* does not overlap the rectangle */
		if (pl->regions >= NQD_FM_MAX_REGIONS) {
			pl->bad_region = base;
			result = NQD_FM_REJ_REGION_CAP;
			break;
		}
		NqdSpans one, both;
		if (!nqd_region_to_spans(rd16, base, rt, rl, rb, rr, one)) {
			if (pol.skip_bad_regions) {
				pl->skipped_regions++;
				continue;
			}
			pl->bad_region = base;
			result = NQD_FM_REJ_REGION_BAD;
			break;
		}
		nqd_spans_intersect(pl->spans, one, both);
		pl->spans = both;
		pl->regions++;
	}
	if (result != NQD_FM_OK)
		return result;
	return pl->regions > 0 ? NQD_FM_OK : NQD_FM_REJ_NO_REGION;
}

/* The pixel the plan paints at rect pixel (x, y), if the clip lets it through. */
static inline uint32_t nqd_fm_pixel(const NqdFillmaskPlan &pl, int x, int y)
{
	return pl.tiled ? pl.tile[(size_t)((y + pl.tile_oy) % pl.tile_h) * (size_t)pl.tile_w + (size_t)((x + pl.tile_ox) % pl.tile_w)] : pl.colour;
}

/*
 *  The CPU executor: paints the plan's colour into the spans. fb is the host address of the framebuffer's
 *  first byte and fb_bytes its length; the extent is re-checked so a bad plan cannot write outside it.
 */
static inline bool nqd_fillmask_cpu(const NqdFillmaskPlan &pl, uint8_t *fb, uint64_t fb_bytes)
{
	if (!fb || pl.end > fb_bytes || pl.start >= pl.end || pl.width < 1 || pl.height < 1 || pl.row_bytes < 4 ||
	    pl.spans.w != pl.width || pl.spans.h != pl.height || pl.spans.row_start.size() != (size_t)pl.height + 1 ||
	    pl.spans.row_start[0] != 0 || (size_t)pl.spans.row_start[(size_t)pl.height] * 2 != pl.spans.runs.size())
		return false;
	/* Check every run before the first write, so a bad plan changes nothing. */
	for (int y = 0; y < pl.height; y++) {
		if (pl.spans.row_start[(size_t)y] > pl.spans.row_start[(size_t)y + 1])
			return false;
		for (int32_t i = pl.spans.row_start[(size_t)y]; i < pl.spans.row_start[(size_t)y + 1]; i++) {
			const int x0 = pl.spans.runs[(size_t)i * 2], x1 = pl.spans.runs[(size_t)i * 2 + 1];
			if (x0 < 0 || x1 > pl.width || x0 > x1)
				return false;
		}
	}
	if (pl.tiled && (pl.tile_w < 1 || pl.tile_h < 1 || pl.tile_ox < 0 || pl.tile_oy < 0 || pl.tile.size() != (size_t)pl.tile_w * (size_t)pl.tile_h))
		return false;
	for (int y = 0; y < pl.height; y++) {
		uint8_t *row = fb + pl.start + (uint64_t)y * (uint64_t)pl.row_bytes;
		for (int32_t i = pl.spans.row_start[(size_t)y]; i < pl.spans.row_start[(size_t)y + 1]; i++)
			for (int x = pl.spans.runs[(size_t)i * 2]; x < pl.spans.runs[(size_t)i * 2 + 1]; x++) {
				const uint32_t c = nqd_fm_pixel(pl, x, y);
				memcpy(row + (size_t)x * 4, &c, 4);
			}
	}
	return true;
}

#endif
