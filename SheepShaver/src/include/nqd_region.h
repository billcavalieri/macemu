/*
 *  nqd_region.h - QuickDraw regions as per-row spans (shared by gfxaccel.cpp and the harness)
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
 *  A QuickDraw region is: size (16 bits, bytes), bounding box (top, left, bottom, right),
 *  then for a non-rectangular region a list of scanlines. Each scanline is a y followed by
 *  x "inversion points" and ends with 0x7fff; the list ends with a y of 0x7fff. Starting at
 *  y, every point toggles the in/out state of that x for all following rows, until the next
 *  scanline changes it.
 *
 *  NqdSpans holds a clip for a w x h rectangle as, per row, a sorted list of [x0, x1)
 *  pixel runs relative to the rectangle's left edge. The header is plain C++ and reads
 *  guest memory through a callable, so the emulator and the harness share it.
 */
#ifndef NQD_REGION_H
#define NQD_REGION_H

#include <stdint.h>
#include <string.h>
#include <vector>
#include <algorithm>

struct NqdSpans {
	int w, h;
	std::vector<int32_t> row_start;		/* h + 1 entries: index of each row's first run (in runs) */
	std::vector<int32_t> runs;		/* x0, x1 pairs, 2 entries per run */

	void full(int width, int height)
	{
		w = width;
		h = height;
		row_start.assign((size_t)h + 1, 0);
		runs.clear();
		for (int y = 0; y < h; y++) {
			row_start[(size_t)y] = (int32_t)(runs.size() / 2);
			runs.push_back(0);
			runs.push_back(w);
		}
		row_start[(size_t)h] = (int32_t)(runs.size() / 2);
	}
	bool contains(int x, int y) const
	{
		for (int32_t i = row_start[(size_t)y]; i < row_start[(size_t)y + 1]; i++)
			if (x >= runs[(size_t)i * 2] && x < runs[(size_t)i * 2 + 1])
				return true;
		return false;
	}
	size_t pixels() const
	{
		size_t n = 0;
		for (size_t i = 0; i + 1 < runs.size(); i += 2)
			n += (size_t)(runs[i + 1] - runs[i]);
		return n;
	}
};

/* a ∩ b, row by row. Both must describe the same w x h rectangle. */
static inline void nqd_spans_intersect(const NqdSpans &a, const NqdSpans &b, NqdSpans &out)
{
	out.w = a.w;
	out.h = a.h;
	out.row_start.assign((size_t)a.h + 1, 0);
	out.runs.clear();
	for (int y = 0; y < a.h; y++) {
		out.row_start[(size_t)y] = (int32_t)(out.runs.size() / 2);
		int32_t i = a.row_start[(size_t)y], ie = a.row_start[(size_t)y + 1];
		int32_t j = b.row_start[(size_t)y], je = b.row_start[(size_t)y + 1];
		while (i < ie && j < je) {
			const int lo = std::max(a.runs[(size_t)i * 2], b.runs[(size_t)j * 2]);
			const int hi = std::min(a.runs[(size_t)i * 2 + 1], b.runs[(size_t)j * 2 + 1]);
			if (lo < hi) {
				out.runs.push_back(lo);
				out.runs.push_back(hi);
			}
			if (a.runs[(size_t)i * 2 + 1] < b.runs[(size_t)j * 2 + 1])
				i++;
			else
				j++;
		}
	}
	out.row_start[(size_t)a.h] = (int32_t)(out.runs.size() / 2);
}

/*
 *  The spans of the region at 'rgn' inside the rectangle [rl, rr) x [rt, rb), all in the
 *  region's own coordinates. rd16(addr) reads a big-endian 16-bit guest value. Returns false
 *  if the data is not a plausible region (the caller then declines the operation).
 */
template <class Rd16>
static bool nqd_region_to_spans(Rd16 rd16, uint32_t rgn, int rt, int rl, int rb, int rr, NqdSpans &out)
{
	const unsigned size = rd16(rgn);
	if (size < 10 || size > 8192 || rb <= rt || rr <= rl)
		return false;
	const int bt = (int16_t)rd16(rgn + 2), bl = (int16_t)rd16(rgn + 4);
	const int bb = (int16_t)rd16(rgn + 6), br = (int16_t)rd16(rgn + 8);
	if (bb <= bt || br <= bl)
		return false;
	const int w = rr - rl, h = rb - rt;
	out.w = w;
	out.h = h;
	out.row_start.assign((size_t)h + 1, 0);
	out.runs.clear();
	if (size == 10) {					/* rectangular region: its bounding box */
		for (int y = 0; y < h; y++) {
			out.row_start[(size_t)y] = (int32_t)(out.runs.size() / 2);
			if (rt + y >= bt && rt + y < bb) {
				const int x0 = std::max(bl, rl) - rl, x1 = std::min(br, rr) - rl;
				if (x0 < x1) {
					out.runs.push_back(x0);
					out.runs.push_back(x1);
				}
			}
		}
		out.row_start[(size_t)h] = (int32_t)(out.runs.size() / 2);
		return true;
	}
	/* Parse the scanlines into states: from row y on, the in/out points are pts. */
	struct State { int y; std::vector<int> pts; };
	std::vector<State> states;
	std::vector<int> pts;
	uint32_t a = rgn + 10;
	const uint32_t end = rgn + size;
	for (;;) {
		if (a + 2 > end)
			return false;
		const int y = (int16_t)rd16(a);
		a += 2;
		if (y == 0x7fff)
			break;
		std::vector<int> line;
		for (;;) {
			if (a + 2 > end)
				return false;
			const int x = (int16_t)rd16(a);
			a += 2;
			if (x == 0x7fff)
				break;
			line.push_back(x);
		}
		std::vector<int> merged;
		size_t i = 0, j = 0;
		while (i < pts.size() || j < line.size()) {		/* symmetric difference of sorted point sets */
			if (j >= line.size() || (i < pts.size() && pts[i] < line[j]))
				merged.push_back(pts[i++]);
			else if (i >= pts.size() || line[j] < pts[i])
				merged.push_back(line[j++]);
			else {
				i++;
				j++;
			}
		}
		pts.swap(merged);
		states.push_back({ y, pts });
	}
	size_t si = 0;
	for (int y = 0; y < h; y++) {
		out.row_start[(size_t)y] = (int32_t)(out.runs.size() / 2);
		const int gy = rt + y;
		while (si + 1 < states.size() && states[si + 1].y <= gy)
			si++;
		if (states.empty() || states[si].y > gy)
			continue;
		const std::vector<int> &p = states[si].pts;
		for (size_t k = 0; k + 1 < p.size(); k += 2) {
			const int x0 = std::max(p[k], rl) - rl, x1 = std::min(p[k + 1], rr) - rl;
			if (x0 < x1) {
				out.runs.push_back(x0);
				out.runs.push_back(x1);
			}
		}
	}
	out.row_start[(size_t)h] = (int32_t)(out.runs.size() / 2);
	return true;
}

#endif
