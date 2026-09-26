/*
 *  SheepForce.metal - Present, QuickDraw, and RAVE shaders.
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

#include <metal_stdlib>
using namespace metal;

struct VOut { float4 p [[position]]; float2 uv; };
vertex VOut sf_vs(uint id [[vertex_id]]) {
	float2 q[3] = { float2(-1.0,-1.0), float2(3.0,-1.0), float2(-1.0,3.0) };
	VOut o;
	o.p = float4(q[id], 0.0, 1.0);
	o.uv = float2(q[id].x * 0.5 + 0.5, 1.0 - (q[id].y * 0.5 + 0.5));
	return o;
}
static float4 pal_rgb(constant uchar4 *pal, uint i) {
	uchar4 p = pal[i];
	return float4(float(p.z) / 255.0, float(p.y) / 255.0, float(p.x) / 255.0, 1.0);
}
fragment float4 sf_fs(VOut in [[stage_in]], constant uchar4 *pal [[buffer(0)]],
		device const uchar *pix [[buffer(2)]], constant uint *meta [[buffer(3)]]) {
	uint w = meta[0], h = meta[1], row = meta[2], depth = meta[3], off = meta[4];
	uint x = min(uint(in.uv.x * float(w)), w - 1u);
	uint y = min(uint(in.uv.y * float(h)), h - 1u);
	if (depth == 1u) {
		uint byte = pix[off + y * row + (x >> 3)];
		uint bit = 0x80u >> (x & 7u);
		return pal_rgb(pal, (byte & bit) ? 1u : 0u);
	}
	if (depth == 2u) {
		uint byte = pix[off + y * row + (x >> 2)];
		uint shift = (3u - (x & 3u)) * 2u;
		return pal_rgb(pal, (byte >> shift) & 3u);
	}
	if (depth == 4u) {
		uint byte = pix[off + y * row + (x >> 1)];
		uint i = ((x & 1u) == 0u) ? (byte >> 4) : (byte & 15u);
		return pal_rgb(pal, i);
	}
	if (depth == 8u) {
		return pal_rgb(pal, pix[off + y * row + x]);
	}
	if (depth == 16u) {
		uint a = off + y * row + x * 2u;
		uint v = (uint(pix[a]) << 8) | uint(pix[a + 1]);
		return float4(float((v >> 10) & 31u) / 31.0, float((v >> 5) & 31u) / 31.0, float(v & 31u) / 31.0, 1.0);
	}
	uint a = off + y * row + x * 4u;
	return float4(float(pix[a + 1]) / 255.0, float(pix[a + 2]) / 255.0, float(pix[a + 3]) / 255.0, 1.0);
}
struct FillU { uint x, y, w, h, row, bpp, color; };
kernel void sf_fill(device uchar *pix [[buffer(0)]], constant FillU &u [[buffer(1)]],
		uint2 gid [[thread_position_in_grid]]) {
	if (gid.x >= u.w || gid.y >= u.h) return;
	device uchar *d = pix + (u.y + gid.y) * u.row + (u.x + gid.x) * u.bpp;
	for (uint i = 0; i < u.bpp; i++) d[i] = (uchar)((u.color >> (8u * i)) & 0xffu);
}
kernel void sf_inv(device uchar *pix [[buffer(0)]], constant FillU &u [[buffer(1)]],
		uint2 gid [[thread_position_in_grid]]) {
	if (gid.x >= u.w || gid.y >= u.h) return;
	device uchar *d = pix + (u.y + gid.y) * u.row + (u.x + gid.x) * u.bpp;
	for (uint i = 0; i < u.bpp; i++) d[i] = ~d[i];
}
struct BlitU { uint w, h, dst_row, src_row, bpp; };
kernel void sf_blit(device uchar *dst [[buffer(0)]], device const uchar *src [[buffer(1)]],
		constant BlitU &u [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) {
	if (gid.x >= u.w || gid.y >= u.h) return;
	device uchar *d = dst + gid.y * u.dst_row + gid.x * u.bpp;
	device const uchar *s = src + gid.y * u.src_row + gid.x * u.bpp;
	for (uint i = 0; i < u.bpp; i++) d[i] = s[i];
}
struct TriV { float4 p [[position]]; float4 color; };
vertex TriV sf_tri_vs(uint id [[vertex_id]], constant packed_float2 *xy [[buffer(0)]],
		constant float4 &color [[buffer(1)]]) {
	float2 n = xy[id];
	TriV o; o.p = float4(n.x * 2.0 - 1.0, 1.0 - n.y * 2.0, 0.0, 1.0); o.color = color; return o;
}
fragment float4 sf_tri_fs(TriV in [[stage_in]]) { return in.color; }
