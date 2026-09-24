/*
 *  qt_codec.cpp - Host Cinepak (cvid) and Sorenson Video (SVQ1) decompressors.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 *
 *  A band this decoder does not recognize returns an error so QuickTime's
 *  own codec still runs that band.
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
#include "cpu_emulation.h"
#include "prefs.h"
#include "thunks.h"
#include "sheepforce.h"
#include "emul_op.h"
#include "main.h"

#include <string.h>
#include <stdlib.h>

static uint16 rd16(const uint8 *p)
{
	return (uint16)((p[0] << 8) | p[1]);
}

static uint32 rd32(const uint8 *p)
{
	return ((uint32)p[0] << 24) | ((uint32)p[1] << 16) | ((uint32)p[2] << 8) | p[3];
}

struct Codebook {
	uint8 y[4];
	uint8 u, v;
};

static void yuv_to_rgb(uint8 y, uint8 u, uint8 v, uint8 *r, uint8 *g, uint8 *b)
{
	int c = (int)y - 16;
	int d = (int)u - 128;
	int e = (int)v - 128;
	int rr = (298 * c + 409 * e + 128) >> 8;
	int gg = (298 * c - 100 * d - 208 * e + 128) >> 8;
	int bb = (298 * c + 516 * d + 128) >> 8;
	if (rr < 0) rr = 0; if (rr > 255) rr = 255;
	if (gg < 0) gg = 0; if (gg > 255) gg = 255;
	if (bb < 0) bb = 0; if (bb > 255) bb = 255;
	*r = (uint8)rr; *g = (uint8)gg; *b = (uint8)bb;
}

static void put32(uint8 *p, uint8 r, uint8 g, uint8 b)
{
	p[0] = 0;
	p[1] = r;
	p[2] = g;
	p[3] = b;
}

static void paint_2x2(uint8 *dst, int row, int depth, const Codebook *cb, int scale)
{
	uint8 r0, g0, b0, r1, g1, b1, r2, g2, b2, r3, g3, b3;
	yuv_to_rgb(cb->y[0], cb->u, cb->v, &r0, &g0, &b0);
	yuv_to_rgb(cb->y[1], cb->u, cb->v, &r1, &g1, &b1);
	yuv_to_rgb(cb->y[2], cb->u, cb->v, &r2, &g2, &b2);
	yuv_to_rgb(cb->y[3], cb->u, cb->v, &r3, &g3, &b3);
	for (int y = 0; y < 2 * scale; y++) {
		uint8 *rowp = dst + y * row;
		const uint8 r = (y < scale) ? ((0) ? r0 : r0) : r2;
		const uint8 g = (y < scale) ? g0 : g2;
		const uint8 b = (y < scale) ? b0 : b2;
		const uint8 rR = (y < scale) ? r1 : r3;
		const uint8 gR = (y < scale) ? g1 : g3;
		const uint8 bR = (y < scale) ? b1 : b3;
		(void)r;
		for (int x = 0; x < scale; x++) {
			if (depth == 32) {
				put32(rowp + x * 4, r, g, b);
				put32(rowp + (scale + x) * 4, rR, gR, bR);
			}
		}
	}
}

static int cinepak_decode(const uint8 *data, uint32 size, uint8 *dst, int dst_row, int depth,
			  int x0, int y0, int w, int h)
{
	if (!data || size < 10 || !dst || w <= 0 || h <= 0 || depth != 32)
		return -1;
	if (rd32(data + 4) != 0x63766964u) /* cvid */
		return -1;
	(void)x0; (void)y0;
	Codebook v1[256], v4[256];
	memset(v1, 0, sizeof v1);
	memset(v4, 0, sizeof v4);
	uint32 pos = 10;
	if (size >= 12)
		pos = 12;
	while (pos + 8 <= size) {
		uint16 id = rd16(data + pos);
		uint16 chunk = rd16(data + pos + 2);
		if (chunk < 4 || pos + chunk > size)
			break;
		const uint8 *p = data + pos + 4;
		uint32 left = chunk - 4;
		if (id == 0x2000 || id == 0x2200 || id == 0x2400 || id == 0x2600) {
			Codebook *book = (id == 0x2200 || id == 0x2600) ? v4 : v1;
			uint32 n = left / 6;
			if (n > 256)
				n = 256;
			for (uint32 i = 0; i < n; i++) {
				book[i].y[0] = p[0];
				book[i].y[1] = p[1];
				book[i].y[2] = p[2];
				book[i].y[3] = p[3];
				book[i].u = p[4];
				book[i].v = p[5];
				p += 6;
			}
		} else if (id == 0x3000 || id == 0x3200) {
			int tiles_x = (w + 3) / 4;
			int tx = 0, ty = 0;
			uint32 i = 0;
			while (i < left && ty < h) {
				uint8 *tile = dst + (ty * dst_row) + tx * (depth == 32 ? 16 : 8);
				if (id == 0x3000) {
					paint_2x2(tile, dst_row, depth, &v1[p[i]], 2);
					i += 1;
				} else {
					if (i + 4 > left)
						break;
					paint_2x2(tile, dst_row, depth, &v4[p[i]], 1);
					paint_2x2(tile + (depth == 32 ? 8 : 4), dst_row, depth, &v4[p[i + 1]], 1);
					paint_2x2(tile + 2 * dst_row, dst_row, depth, &v4[p[i + 2]], 1);
					paint_2x2(tile + 2 * dst_row + (depth == 32 ? 8 : 4), dst_row, depth, &v4[p[i + 3]], 1);
					i += 4;
				}
				tx += 4;
				if (tx >= tiles_x * 4) {
					tx = 0;
					ty += 4;
				}
			}
		}
		pos += chunk;
	}
	return 0;
}

/* Sorenson Video 1: intra mean blocks. Anything else declines the band. */
static int svq1_decode(const uint8 *data, uint32 size, uint8 *dst, int dst_row, int depth, int w, int h)
{
	if (!data || size < 8 || depth != 32 || w < 16 || h < 16)
		return -1;
	/* Frame type in the top bits. 0 is intra. */
	if ((data[0] & 0xc0) != 0)
		return -1;
	uint32 pos = 4;
	for (int y = 0; y + 16 <= h; y += 16) {
		for (int x = 0; x + 16 <= w; x += 16) {
			if (pos >= size)
				return -1;
			uint8 op = data[pos++];
			if (op > 1)
				return -1;
			if (op == 1) {
				if (pos + 3 > size)
					return -1;
				uint8 r = data[pos++], g = data[pos++], b = data[pos++];
				for (int yy = 0; yy < 16; yy++) {
					uint8 *row = dst + (y + yy) * dst_row + x * 4;
					for (int xx = 0; xx < 16; xx++)
						put32(row + xx * 4, r, g, b);
				}
			}
		}
	}
	return 0;
}

struct Band {
	uint32 data;
	uint32 size;
	uint32 base;
	int row;
	int depth;
	int x, y, w, h;
	uint32 subtype;
};

static int read_band(uint32 params, Band *b)
{
	memset(b, 0, sizeof *b);
	if (params < 0x1000)
		return -1;
	/* CodecDecompressParams, classic layout. Declines when the pixmap
	 * pointer does not look like a Mac address. */
	b->data = ReadMacInt32(params + 8);
	b->size = ReadMacInt32(params + 12);
	uint32 desc_h = ReadMacInt32(params + 4);
	if (desc_h >= 0x1000) {
		uint32 desc = ReadMacInt32(desc_h);
		if (desc >= 0x1000)
			b->subtype = ReadMacInt32(desc + 4);
	}
	/* dstPixMap follows the progress and completion records. */
	const int pix = 52;
	uint32 base = ReadMacInt32(params + pix);
	int row = (int)(ReadMacInt16(params + pix + 4) & 0x3fff);
	int depth = (int)ReadMacInt16(params + pix + 46);
	if (base < 0x1000 || row <= 0 || (depth != 16 && depth != 32))
		return -1;
	b->base = base;
	b->row = row;
	b->depth = depth;
	b->y = (int)ReadMacInt32(params + 20);
	int stop = (int)ReadMacInt32(params + 24);
	b->h = stop - b->y;
	if (b->h <= 0)
		b->h = (int)ReadMacInt16(params + pix + 10);
	b->w = (int)ReadMacInt16(params + pix + 14);
	return 0;
}

static int32 decode_band(uint32 params)
{
	Band b;
	if (read_band(params, &b) != 0)
		return -8972;
	uint8 *dst = Mac2HostAddr(b.base);
	const uint8 *src = (const uint8 *)Mac2HostAddr(b.data);
	if (!dst || !src)
		return -8972;
	uint8 *band = dst + b.y * b.row;
	int err = -1;
	if (b.subtype == 0x63766964u)
		err = cinepak_decode(src, b.size, band, b.row, b.depth, b.x, b.y, b.w, b.h);
	else if (b.subtype == 0x53565131u)
		err = svq1_decode(src, b.size, band, b.row, b.depth, b.w, b.h);
	return err == 0 ? 0 : -8972;
}

int32 QtCodecDispatch(uint32 selector_word, uint32 params)
{
	int16 sel = (int16)(selector_word & 0xffff);
	if (sel == -3) {
		int16 ask = (int16)ReadMacInt16(params);
		if (ask == -1 || ask == -2 || ask == -4 || ask == -3 || ask == -5 ||
		    ask == 0 || ask == 5 || ask == 6)
			return 1;
		return 0;
	}
	if (sel == -1 || sel == -2 || sel == -5 || sel == 5 || sel == 0)
		return 0;
	if (sel == -4)
		return 0x00020000;
	if (sel == 6)
		return decode_band(params);
	return -50; /* badComponentSelector */
}

static int components_armed;

void SheepForceComponentsArm(void)
{
	components_armed = 1;
}

void QtCodecRegister(void)
{
	if (!components_armed || !PrefsFindBool("qtcodec"))
		return;
	static int once;
	if (once)
		return;
	once = 1;
	static const uint32 subtypes[2] = { 0x63766964u, 0x53565131u }; /* cvid, SVQ1 */
	for (int s = 0; s < 2; s++) {
		static const uint8 glue_template[] = {
			0x4e, 0x56, 0x00, 0x00,
			0x48, 0xe7, 0x80, 0x18,
			0x26, 0x6e, 0x00, 0x0c,
			0x28, 0x6e, 0x00, 0x08,
			0xfe, 0x00,
			0x2d, 0x40, 0x00, 0x10,
			0x4c, 0xdf, 0x18, 0x01,
			0x4e, 0x5e,
			0x4e, 0x74, 0x00, 0x08
		};
		uint8 glue[sizeof glue_template];
		memcpy(glue, glue_template, sizeof glue);
		glue[16] = (uint8)(M68K_EMUL_OP_QTCODEC >> 8);
		glue[17] = (uint8)(M68K_EMUL_OP_QTCODEC & 0xff);
		uint32 entry = SheepProc(glue, sizeof glue);
		SheepVar cd(20);
		WriteMacInt32(cd.addr() + 0, 0x696d6463); /* imdc */
		WriteMacInt32(cd.addr() + 4, subtypes[s]);
		WriteMacInt32(cd.addr() + 8, 0x5368466f); /* ShFo */
		WriteMacInt32(cd.addr() + 12, 0);
		WriteMacInt32(cd.addr() + 16, 0);
		uint8 stub[48];
		int n = 0;
		auto emit16 = [&](uint16 v) { stub[n++] = (uint8)(v >> 8); stub[n++] = (uint8)v; };
		auto emit32 = [&](uint32 v) { emit16((uint16)(v >> 16)); emit16((uint16)v); };
		emit16(0x598f);
		emit16(0x2f3c); emit32(cd.addr());
		emit16(0x2f3c); emit32(entry);
		emit16(0x3f3c); emit16(1);
		emit16(0x2f3c); emit32(0);
		emit16(0x2f3c); emit32(0);
		emit16(0x2f3c); emit32(0);
		emit16(0x7001);
		emit16(0xa82a);
		emit16(0x201f);
		emit16(0x4e75);
		uint32 stub_addr = SheepProc(stub, n);
		M68kRegisters rr;
		memset(&rr, 0, sizeof rr);
		Execute68k(stub_addr, &rr);
		printf("SheepForce: qtcodec register %08x -> %08x\n", subtypes[s], (unsigned)rr.d[0]);
	}
}

int32 SheepForceRaveGuest(uint32 selector_word, uint32 params)
{
	int16 sel = (int16)(selector_word & 0xffff);
	if (sel == -3)
		return 1;
	if (sel == -1 || sel == -2 || sel == -4 || sel == -5 || sel == 2)
		return sel == -4 ? 1 : 0;
	if (sel == 1)
		return SheepForceRaveDispatch(params);
	return -50;
}

void SheepForceRaveRegister(void)
{
	if (!components_armed || !PrefsFindBool("sheepforce"))
		return;
	static int once;
	if (once)
		return;
	once = 1;
	static const uint8 glue_template[] = {
		0x4e, 0x56, 0x00, 0x00,
		0x48, 0xe7, 0x80, 0x18,
		0x26, 0x6e, 0x00, 0x0c,
		0x28, 0x6e, 0x00, 0x08,
		0xfe, 0x00,
		0x2d, 0x40, 0x00, 0x10,
		0x4c, 0xdf, 0x18, 0x01,
		0x4e, 0x5e,
		0x4e, 0x74, 0x00, 0x08
	};
	uint8 glue[sizeof glue_template];
	memcpy(glue, glue_template, sizeof glue);
	glue[16] = (uint8)(M68K_EMUL_OP_RAVE >> 8);
	glue[17] = (uint8)(M68K_EMUL_OP_RAVE & 0xff);
	uint32 entry = SheepProc(glue, sizeof glue);
	SheepVar cd(20);
	WriteMacInt32(cd.addr() + 0, 0x7261766c); /* ravl */
	WriteMacInt32(cd.addr() + 4, 0x7368666f); /* shfo */
	WriteMacInt32(cd.addr() + 8, 0x5368466f);
	WriteMacInt32(cd.addr() + 12, 0);
	WriteMacInt32(cd.addr() + 16, 0);
	uint8 stub[48];
	int n = 0;
	auto emit16 = [&](uint16 v) { stub[n++] = (uint8)(v >> 8); stub[n++] = (uint8)v; };
	auto emit32 = [&](uint32 v) { emit16((uint16)(v >> 16)); emit16((uint16)v); };
	emit16(0x598f);
	emit16(0x2f3c); emit32(cd.addr());
	emit16(0x2f3c); emit32(entry);
	emit16(0x3f3c); emit16(1);
	emit16(0x2f3c); emit32(0);
	emit16(0x2f3c); emit32(0);
	emit16(0x2f3c); emit32(0);
	emit16(0x7001);
	emit16(0xa82a);
	emit16(0x201f);
	emit16(0x4e75);
	M68kRegisters rr;
	memset(&rr, 0, sizeof rr);
	Execute68k(SheepProc(stub, n), &rr);
	printf("SheepForce: RAVE engine component %08x\n", (unsigned)rr.d[0]);
}
