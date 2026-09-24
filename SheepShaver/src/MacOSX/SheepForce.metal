/*
 *  SheepForce.metal - Present shader. Compiled at runtime from sheepforce_metal.mm.
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
fragment float4 sf_fs(VOut in [[stage_in]], texture2d<float> tex [[texture(0)]],
		constant uchar4 *pal [[buffer(0)]], constant uint &depth [[buffer(1)]]) {
	constexpr sampler smp(filter::nearest, address::clamp_to_edge);
	float4 c = tex.sample(smp, in.uv);
	if (depth == 8u) {
		uint i = min(uint(c.x * 255.0 + 0.5), 255u);
		uchar4 p = pal[i];
		return float4(float(p.z) / 255.0, float(p.y) / 255.0, float(p.x) / 255.0, 1.0);
	}
	/* 32-bit guest bytes are 00,R,G,B. RGBA8 reads that as (0, R, G, B). */
	return float4(c.g, c.b, c.a, 1.0);
}
