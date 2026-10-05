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
/* Fill clipped to per-row spans, with a solid colour or an 8x8 one-bit pattern (bit 7 of byte 0 is the
 * top-left tile pixel; pat0 holds bytes 0-3, pat1 bytes 4-7, each big-endian). (ox, oy) is the tile
 * coordinate of the rectangle's top-left pixel. 32-bit pixels only. */
struct SpanU { uint x, y, w, h, row, fore, back, pat0, pat1; int ox, oy; };
kernel void sf_fillspans(device uchar *pix [[buffer(0)]], constant SpanU &u [[buffer(1)]],
		device const int *row_start [[buffer(2)]], device const int *runs [[buffer(3)]],
		uint2 gid [[thread_position_in_grid]]) {
	if (gid.x >= u.w || gid.y >= u.h) return;
	int a = row_start[gid.y], b = row_start[gid.y + 1];
	bool in = false;
	for (int i = a; i < b; i++)
		if (int(gid.x) >= runs[2 * i] && int(gid.x) < runs[2 * i + 1]) { in = true; break; }
	if (!in) return;
	uint tx = uint(int(gid.x) + u.ox) & 7u, ty = uint(int(gid.y) + u.oy) & 7u;
	uint byte = ty < 4u ? ((u.pat0 >> (8u * (3u - ty))) & 0xffu) : ((u.pat1 >> (8u * (7u - ty))) & 0xffu);
	uint c = ((byte >> (7u - tx)) & 1u) ? u.fore : u.back;
	device uchar *d = pix + (u.y + gid.y) * u.row + (u.x + gid.x) * 4u;
	for (uint i = 0; i < 4u; i++) d[i] = (uchar)((c >> (8u * i)) & 0xffu);
}
/* Fill clipped to per-row spans with a pixel-pattern tile: the pixel for rectangle pixel (x, y) is tile[(y + oy) % th][(x + ox) % tw].
 * Pixels are the guest's 32-bit words in memory order, as sf_fill writes them. */
struct TileU { uint x, y, w, h, row, tw, th, ox, oy; };
kernel void sf_filltile(device uchar *pix [[buffer(0)]], constant TileU &u [[buffer(1)]],
		device const int *row_start [[buffer(2)]], device const int *runs [[buffer(3)]],
		device const uint *tile [[buffer(4)]], uint2 gid [[thread_position_in_grid]]) {
	if (gid.x >= u.w || gid.y >= u.h) return;
	int a = row_start[gid.y], b = row_start[gid.y + 1];
	bool in = false;
	for (int i = a; i < b; i++)
		if (int(gid.x) >= runs[2 * i] && int(gid.x) < runs[2 * i + 1]) { in = true; break; }
	if (!in) return;
	uint c = tile[((gid.y + u.oy) % u.th) * u.tw + ((gid.x + u.ox) % u.tw)];
	device uchar *d = pix + (u.y + gid.y) * u.row + (u.x + gid.x) * 4u;
	for (uint i = 0; i < 4u; i++) d[i] = (uchar)((c >> (8u * i)) & 0xffu);
}
kernel void sf_inv(device uchar *pix [[buffer(0)]], constant FillU &u [[buffer(1)]],
		uint2 gid [[thread_position_in_grid]]) {
	if (gid.x >= u.w || gid.y >= u.h) return;
	device uchar *d = pix + (u.y + gid.y) * u.row + (u.x + gid.x) * u.bpp;
	for (uint i = 0; i < u.bpp; i++) d[i] = ~d[i];
}
/* Blit. One thread per destination pixel. Pixels are handled as the guest holds
 * them in memory, so a 32-bit pixel is the word whose byte 0 is the pad byte and
 * bytes 1..3 are R, G, B. 8-bit sources go through the 256-entry word palette and
 * 15/16-bit sources are expanded 5-5-5 with a shift of 3, exactly as the CPU path
 * in gfxaccel.cpp does. Modes are the QuickDraw transfer modes. */
struct BlitU { uint w, h, dst_row, src_row, sbpp, dbpp, dst_off, src_off, mode, back; };
static uint qd_fetch(device const uchar *s, uint sbpp, constant uint *pal) {
	if (sbpp == 1u) return pal[s[0]];
	if (sbpp == 2u) {
		uint v = (uint(s[0]) << 8) | uint(s[1]);
		uint r = ((v >> 10) & 31u) * 8u, g = ((v >> 5) & 31u) * 8u, b = (v & 31u) * 8u;
		return (r << 8) | (g << 16) | (b << 24);
	}
	return uint(s[0]) | (uint(s[1]) << 8) | (uint(s[2]) << 16) | (uint(s[3]) << 24);
}
static uint qd_bytes(uint a, uint b, uint op) {
	/* Per-component arithmetic on bytes 1..3; the pad byte comes out 0. */
	uint r = 0u;
	for (uint i = 1u; i < 4u; i++) {
		int x = int((a >> (8u * i)) & 0xffu), y = int((b >> (8u * i)) & 0xffu), v;
		if (op == 0u) v = (x + y) & 0xff;
		else if (op == 1u) v = (x - y) & 0xff;
		else if (op == 2u) v = max(x, y);
		else v = min(x, y);
		r |= uint(v) << (8u * i);
	}
	return r;
}
static uint qd_op(uint mode, uint s, uint d) {
	switch (mode) {
	case 0u: return s;
	case 1u: return d | s;
	case 2u: return d ^ s;
	case 3u: return d & ~s;
	case 4u: return ~s;
	case 5u: return d | ~s;
	case 6u: return d ^ ~s;
	case 7u: return d & s;
	case 34u: return qd_bytes(d, s, 0u);	/* addOver */
	case 37u: return qd_bytes(d, s, 2u);	/* adMax */
	case 38u: return qd_bytes(d, s, 1u);	/* subOver */
	case 39u: return qd_bytes(d, s, 3u);	/* adMin */
	}
	return s;
}
kernel void sf_blit(device uchar *dst [[buffer(0)]], device const uchar *src [[buffer(1)]],
		constant BlitU &u [[buffer(2)]], constant uint *pal [[buffer(3)]],
		uint2 gid [[thread_position_in_grid]]) {
	if (gid.x >= u.w || gid.y >= u.h) return;
	/* Buffer bindings stay at 0. A CopyBits rect is not 16-byte aligned,
	 * and setBuffer:offset: drops that blit. */
	device uchar *d = dst + u.dst_off + gid.y * u.dst_row + gid.x * u.dbpp;
	device const uchar *s = src + u.src_off + gid.y * u.src_row + gid.x * u.sbpp;
	if (u.dbpp != 4u) {
		for (uint i = 0; i < u.dbpp; i++) d[i] = s[i];
		return;
	}
	uint sp = qd_fetch(s, u.sbpp, pal);
	uint dp = uint(d[0]) | (uint(d[1]) << 8) | (uint(d[2]) << 16) | (uint(d[3]) << 24);
	if (u.mode == 36u) {	/* transparent: source pixels equal to the background are skipped */
		if (((sp ^ u.back) & 0xffffff00u) == 0u) return;
	}
	uint r = qd_op(u.mode, sp, dp);
	d[0] = uchar(r & 0xffu);
	d[1] = uchar((r >> 8) & 0xffu);
	d[2] = uchar((r >> 16) & 0xffu);
	d[3] = uchar((r >> 24) & 0xffu);
}
/* First half of an overlapping blit: copy the source rectangle out of the
 * framebuffer so the second half cannot read pixels it has already written. */
struct GatherU { uint wbytes, h, src_row, src_off; };
kernel void sf_gather(device const uchar *src [[buffer(0)]], device uchar *scr [[buffer(1)]],
		constant GatherU &u [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) {
	if (gid.x >= u.wbytes || gid.y >= u.h) return;
	scr[gid.y * u.wbytes + gid.x] = src[u.src_off + gid.y * u.src_row + gid.x];
}
/* RAVE draw contexts: pixel-space triangles; colour, uv/w, 1/w and the texture factors
 * are interpolated across the screen (1/w and uv/w are what make textures perspective-correct). */
struct RaveV { float x, y, z, invW, r, g, b, a, uow, vow, kdr, kdg, kdb, ksr, ksg, ksb, uow2, vow2, invW2, pad; };
struct RaveU { float w, h; };
struct RaveOut { float4 p [[position]]; float4 c; float3 uvw; float3 kd; float3 ks; float3 uvw2; };
vertex RaveOut sf_rave_vs(uint id [[vertex_id]], constant RaveV *v [[buffer(0)]], constant RaveU &u [[buffer(1)]]) {
	RaveV a = v[id];
	RaveOut o;
	o.p = float4(a.x / u.w * 2.0 - 1.0, 1.0 - a.y / u.h * 2.0, a.z, 1.0);
	o.c = float4(a.r, a.g, a.b, a.a);
	o.uvw = float3(a.uow, a.vow, a.invW);
	o.kd = float3(a.kdr, a.kdg, a.kdb);
	o.ks = float3(a.ksr, a.ksg, a.ksb);
	o.uvw2 = float3(a.uow2, a.vow2, a.invW2);
	return o;
}
struct RaveF { float4 fog; float fogStart, fogEnd, fogDensity, fogMax; uint fogMode, texOp, textured, mtOp; float mtFactor; uint mtOn, pad1, pad2; };
fragment float4 sf_rave_fs(RaveOut in [[stage_in]], constant RaveF &f [[buffer(0)]],
		texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]],
		texture2d<float> tex2 [[texture(1)]], sampler smp2 [[sampler(1)]]) {
	float4 c = in.c;
	if (f.textured != 0u) {
		float2 uv = in.uvw.xy / in.uvw.z;
		float4 t = tex.sample(smp, uv);
		if (f.mtOn != 0u) {		/* second layer: 0 add, 1 modulate, 2 blend by its alpha, 3 fixed factor */
			float4 t2 = tex2.sample(smp2, in.uvw2.xy / in.uvw2.z);
			if (f.mtOp == 0u) t = float4(min(t.rgb + t2.rgb, float3(1.0)), t.a);
			else if (f.mtOp == 1u) t = t * t2;
			else if (f.mtOp == 2u) t = mix(t, t2, t2.a);
			else t = mix(t, t2, f.mtFactor);
		}
		if ((f.texOp & 4u) != 0u) {		/* decal: texture alpha picks texture over vertex colour */
			t.rgb = t.a * t.rgb + (1.0 - t.a) * c.rgb;
			t.a = c.a;
		} else {
			t.a = t.a * c.a;
		}
		if ((f.texOp & 1u) != 0u) t.rgb = min(t.rgb * in.kd, float3(1.0));	/* modulate */
		if ((f.texOp & 2u) != 0u) t.rgb = min(t.rgb + in.ks, float3(1.0));	/* highlight */
		c = t;
	}
	if (f.fogMode >= 2u && in.uvw.z > 0.0) {
		float w = 1.0 / in.uvw.z;
		if (f.fogMax > 0.0) w = min(w, f.fogMax);
		float k;
		if (f.fogMode == 2u) k = (f.fogEnd - w) / (f.fogEnd - f.fogStart);
		else if (f.fogMode == 3u) k = exp(-f.fogDensity * w);
		else k = exp(-(f.fogDensity * w) * (f.fogDensity * w));
		k = clamp(k, 0.0, 1.0);
		c.rgb = mix(f.fog.rgb, c.rgb, k);
	}
	return c;
}
