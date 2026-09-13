/*
 *  nw_script.cpp - New World operator script (Debug builds only)
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

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "video.h"
#include "nw_devices.h"
#include "nw_script.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>

#if defined(NW_BOOT_LOG) && NW_BOOT_LOG

namespace {

enum step_kind { ST_WAIT_UNTIL, ST_KEY, ST_MOUSE_TO, ST_BUTTON, ST_SHOT, ST_LOG, ST_PAUSE, ST_DUMP };

struct step {
	step_kind kind;
	int a, b;			/* KEY: code, down; MOUSE_TO: x, y; BUTTON: down; PAUSE: ms */
	uint64 t_us;		/* WAIT_UNTIL */
	std::string text;	/* SHOT path, LOG text */
};

struct periodic {
	uint64 period_us;
	uint64 next_us;
	std::string path;
};

std::vector<step> steps;
size_t head;
std::vector<periodic> periodics;
uint64 t0_us;			/* first tick */
uint64 not_before_us;	/* pacing between emitted events */
int shot_index;
int mouse_tries;
bool active;

enum { KEY_GAP_US = 40000, MOUSE_STEP_US = 30000, MOUSE_MAX_STEP = 4, MOUSE_TOL = 2, MOUSE_MAX_TRIES = 2000 };

enum { K_SHIFT = 0x38, K_CMD = 0x37, K_OPT = 0x3a, K_CTRL = 0x3b };

/* US layout: Mac virtual key code for each printable ASCII character, and
 * whether shift is held. -1 = not typeable. */
int key_for_char(char c, bool *shift)
{
	/* index = key code 0x00..0x32 (0x0a ISO section, 0x24 return, 0x30 tab
	 * are not printable characters) */
	static const char plain[0x34] = "asdfhgzxcv\0bqweryt123465=97-80]ou[ip\0lj'k;\\,/nm.\0 `";
	static const char shifted[0x34] = "ASDFHGZXCV\0BQWERYT!@#$^%+(&_*)}OU{IP\0LJ\"K:|<?NM>\0\0~";
	*shift = false;
	if (c == '\n' || c == '\r') return 0x24;
	if (c == '\t') return 0x30;
	for (int i = 0; i < 0x33; i++) {
		if (plain[i] && plain[i] == c) return i;
	}
	for (int i = 0; i < 0x33; i++) {
		if (shifted[i] && shifted[i] == c) { *shift = true; return i; }
	}
	return -1;
}

int key_for_name(const std::string &n)
{
	static const struct { const char *name; int code; } names[] = {
		{ "cmd", K_CMD }, { "command", K_CMD }, { "shift", K_SHIFT }, { "opt", K_OPT },
		{ "option", K_OPT }, { "ctrl", K_CTRL }, { "control", K_CTRL },
		{ "return", 0x24 }, { "enter", 0x4c }, { "tab", 0x30 }, { "space", 0x31 },
		{ "delete", 0x33 }, { "backspace", 0x33 }, { "esc", 0x35 }, { "escape", 0x35 },
		{ "left", 0x7b }, { "right", 0x7c }, { "down", 0x7d }, { "up", 0x7e },
	};
	for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
		if (n == names[i].name) return names[i].code;
	if (n.size() > 2 && n[0] == '0' && (n[1] == 'x' || n[1] == 'X'))
		return (int)strtol(n.c_str(), NULL, 16);
	if (n.size() == 1) {
		bool sh;
		const int k = key_for_char(n[0], &sh);
		return k;
	}
	return -1;
}

void push(step_kind k, int a = 0, int b = 0, const std::string &text = std::string(), uint64 t = 0)
{
	step s;
	s.kind = k; s.a = a; s.b = b; s.text = text; s.t_us = t;
	steps.push_back(s);
}

void push_tap(int code)
{
	push(ST_KEY, code, 1);
	push(ST_KEY, code, 0);
}

void push_click(bool have_xy, int x, int y, int n)
{
	if (have_xy)
		push(ST_MOUSE_TO, x, y);
	for (int i = 0; i < n; i++) {
		push(ST_BUTTON, 1);
		push(ST_PAUSE, 60);
		push(ST_BUTTON, 0);
		if (i + 1 < n)
			push(ST_PAUSE, 120);
	}
}

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::vector<std::string> split(const std::string &s)
{
	std::vector<std::string> v;
	size_t i = 0;
	while (i < s.size()) {
		while (i < s.size() && is_space(s[i])) i++;
		size_t j = i;
		while (j < s.size() && !is_space(s[j])) j++;
		if (j > i) v.push_back(s.substr(i, j - i));
		i = j;
	}
	return v;
}

/* rest of the line after the n-th token */
std::string rest_after(const std::string &line, int n)
{
	size_t i = 0;
	for (int k = 0; k < n; k++) {
		while (i < line.size() && is_space(line[i])) i++;
		while (i < line.size() && !is_space(line[i])) i++;
	}
	while (i < line.size() && is_space(line[i])) i++;
	std::string r = line.substr(i);
	while (!r.empty() && is_space(r[r.size() - 1]))
		r.erase(r.size() - 1);
	return r;
}

bool parse_command(const std::string &line, const std::vector<std::string> &tok, size_t ci, int lineno)
{
	const std::string &cmd = tok[ci];
	if (cmd == "shot" && tok.size() > ci + 1) {
		push(ST_SHOT, 0, 0, tok[ci + 1]);
	} else if (cmd == "text") {
		const std::string s = rest_after(line, (int)ci + 1);
		for (size_t i = 0; i < s.size(); i++) {
			bool sh;
			const int k = key_for_char(s[i], &sh);
			if (k < 0) {
				printf("NW-BOOT SCRIPT line %d: cannot type '%c'\n", lineno, s[i]);
				continue;
			}
			if (sh) push(ST_KEY, K_SHIFT, 1);
			push_tap(k);
			if (sh) push(ST_KEY, K_SHIFT, 0);
		}
	} else if (cmd == "key" && tok.size() > ci + 1) {
		std::vector<int> codes;
		const std::string &spec = tok[ci + 1];
		size_t i = 0;
		while (i <= spec.size()) {
			size_t j = spec.find('+', i);
			if (j == std::string::npos) j = spec.size();
			const int k = key_for_name(spec.substr(i, j - i));
			if (k < 0) {
				printf("NW-BOOT SCRIPT line %d: unknown key '%s'\n", lineno, spec.substr(i, j - i).c_str());
				return false;
			}
			codes.push_back(k);
			i = j + 1;
		}
		for (size_t k = 0; k < codes.size(); k++) push(ST_KEY, codes[k], 1);
		for (size_t k = codes.size(); k-- > 0;) push(ST_KEY, codes[k], 0);
	} else if (cmd == "mouse" && tok.size() > ci + 2) {
		push(ST_MOUSE_TO, atoi(tok[ci + 1].c_str()), atoi(tok[ci + 2].c_str()));
	} else if (cmd == "click" || cmd == "dblclick") {
		const bool xy = tok.size() > ci + 2;
		push_click(xy, xy ? atoi(tok[ci + 1].c_str()) : 0, xy ? atoi(tok[ci + 2].c_str()) : 0, cmd == "click" ? 1 : 2);
	} else if (cmd == "down") {
		push(ST_BUTTON, 1);
	} else if (cmd == "up") {
		push(ST_BUTTON, 0);
	} else if (cmd == "dump" && tok.size() > ci + 3) {
		push(ST_DUMP, (int)strtoul(tok[ci + 1].c_str(), NULL, 16), (int)strtoul(tok[ci + 2].c_str(), NULL, 16), tok[ci + 3]);
	} else if (cmd == "log") {
		push(ST_LOG, 0, 0, rest_after(line, (int)ci + 1));
	} else {
		printf("NW-BOOT SCRIPT line %d: unknown command '%s'\n", lineno, cmd.c_str());
		return false;
	}
	return true;
}

bool parse(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) {
		printf("NW-BOOT SCRIPT cannot open %s\n", path);
		return false;
	}
	char buf[1024];
	int lineno = 0;
	while (fgets(buf, sizeof buf, f)) {
		lineno++;
		std::string line(buf);
		const size_t hash = line.find('#');
		if (hash != std::string::npos) line.erase(hash);
		const std::vector<std::string> tok = split(line);
		if (tok.empty()) continue;
		if (tok[0] == "at" && tok.size() >= 3) {
			const uint64 t = (uint64)(atof(tok[1].c_str()) * 1e6);
			push(ST_WAIT_UNTIL, 0, 0, std::string(), t);
			parse_command(line, tok, 2, lineno);
		} else if (tok[0] == "every" && tok.size() >= 4 && tok[2] == "shot") {
			periodic p;
			p.period_us = (uint64)(atof(tok[1].c_str()) * 1e6);
			p.next_us = p.period_us;
			p.path = tok[3];
			if (p.period_us) periodics.push_back(p);
		} else {
			printf("NW-BOOT SCRIPT line %d: expected `at <sec> ...` or `every <sec> shot ...`\n", lineno);
		}
	}
	fclose(f);
	return true;
}

std::string expand_path(const std::string &pat)
{
	char out[1024];
	if (pat.find('%') != std::string::npos)
		snprintf(out, sizeof out, pat.c_str(), shot_index);
	else
		snprintf(out, sizeof out, "%s", pat.c_str());
	shot_index++;
	return out;
}

void take_shot(const std::string &pat, uint64 now_us)
{
	if (!screen_base || cur_mode < 0) {
		printf("NW-BOOT SCRIPT shot: no video mode yet\n");
		return;
	}
	const std::string name = expand_path(pat);
	const int w = VModes[cur_mode].viXsize, h = VModes[cur_mode].viYsize;
	const uint32 rb = VModes[cur_mode].viRowBytes;
	const int bpp = 1 << (VModes[cur_mode].viAppleMode - APPLE_1_BIT);
	FILE *f = fopen(name.c_str(), "wb");
	if (!f) {
		printf("NW-BOOT SCRIPT shot: cannot write %s\n", name.c_str());
		return;
	}
	fprintf(f, "P6\n%d %d\n255\n", w, h);
	const uint8 *fb = Mac2HostAddr(screen_base);
	std::vector<uint8> row((size_t)w * 3);
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			uint8 *rgb = &row[(size_t)x * 3];
			if (bpp == 32) {
				const uint8 *p = fb + y * rb + x * 4;
				rgb[0] = p[1]; rgb[1] = p[2]; rgb[2] = p[3];
			} else if (bpp == 16) {
				const uint16 v = (uint16)((fb[y * rb + x * 2] << 8) | fb[y * rb + x * 2 + 1]);
				rgb[0] = (uint8)(((v >> 10) & 31) << 3); rgb[1] = (uint8)(((v >> 5) & 31) << 3); rgb[2] = (uint8)((v & 31) << 3);
			} else if (bpp == 8) {
				const uint8 v = fb[y * rb + x];	/* no CLUT read here: gray ramp */
				rgb[0] = rgb[1] = rgb[2] = (uint8)(255 - v);
			} else {
				const uint8 v = (fb[y * rb + x / 8] >> (7 - x % 8)) & 1;
				rgb[0] = rgb[1] = rgb[2] = v ? 0 : 255;
			}
		}
		fwrite(&row[0], 1, row.size(), f);
	}
	fclose(f);
	printf("NW-BOOT SCRIPT t=%.1f shot %s %dx%d bpp=%d\n", (now_us - t0_us) / 1e6, name.c_str(), w, h, bpp);
}

/* Mouse (low-memory $830): Point, v then h. */
void guest_cursor(int *x, int *y)
{
	*y = (int16)ReadMacInt16(0x830);
	*x = (int16)ReadMacInt16(0x832);
}

} // namespace

void nw_script_init(void)
{
	const char *path = getenv("NW_SCRIPT");
	if (!path || !*path)
		return;
	if (!parse(path))
		return;
	active = !steps.empty() || !periodics.empty();
	printf("NW-BOOT SCRIPT %s: %zu steps, %zu periodic\n", path, steps.size(), periodics.size());
}

void nw_script_tick(void)
{
	if (!active)
		return;
	const uint64 now = GetTicks_usec();
	if (!t0_us)
		t0_us = now;
	const uint64 el = now - t0_us;

	for (size_t i = 0; i < periodics.size(); i++) {
		if (el >= periodics[i].next_us) {
			take_shot(periodics[i].path, now);
			periodics[i].next_us += periodics[i].period_us;
		}
	}

	if (now < not_before_us)
		return;
	while (head < steps.size()) {
		step &s = steps[head];
		switch (s.kind) {
		case ST_WAIT_UNTIL:
			if (el < s.t_us)
				return;
			break;
		case ST_PAUSE:
			not_before_us = now + (uint64)s.a * 1000u;
			head++;
			return;
		case ST_KEY:
			nw_adb_key((uint8)s.a, s.b);
			not_before_us = now + KEY_GAP_US;
			head++;
			return;
		case ST_BUTTON:
			nw_adb_mouse_button(0, s.a);
			printf("NW-BOOT SCRIPT t=%.1f button %s\n", el / 1e6, s.a ? "down" : "up");
			not_before_us = now + KEY_GAP_US;
			head++;
			return;
		case ST_MOUSE_TO: {
			int cx, cy;
			guest_cursor(&cx, &cy);
			if (mouse_tries == 0)
				printf("NW-BOOT SCRIPT t=%.1f mouse from %d,%d (MTemp %d,%d) toward %d,%d\n",
				       el / 1e6, cx, cy, (int)(int16)ReadMacInt16(0x828), (int)(int16)ReadMacInt16(0x82a), s.a, s.b);
			if (cx - s.a <= MOUSE_TOL && s.a - cx <= MOUSE_TOL && cy - s.b <= MOUSE_TOL && s.b - cy <= MOUSE_TOL) {
				printf("NW-BOOT SCRIPT t=%.1f mouse at %d,%d (%d steps)\n", el / 1e6, cx, cy, mouse_tries);
				mouse_tries = 0;
				not_before_us = now + KEY_GAP_US;
				head++;
				return;
			}
			if (++mouse_tries > MOUSE_MAX_TRIES) {
				printf("NW-BOOT SCRIPT t=%.1f mouse gave up at %d,%d (MTemp %d,%d, target %d,%d)\n",
				       el / 1e6, cx, cy, (int)(int16)ReadMacInt16(0x828), (int)(int16)ReadMacInt16(0x82a), s.a, s.b);
				mouse_tries = 0;
				head++;
				return;
			}
			/* Big steps far out, single pixels near the target: the guest's
			 * cursor device applies its own acceleration to each report. */
			int dx = s.a - cx, dy = s.b - cy;
			const bool near = dx < 2 * MOUSE_MAX_STEP && dx > -2 * MOUSE_MAX_STEP && dy < 2 * MOUSE_MAX_STEP && dy > -2 * MOUSE_MAX_STEP;
			const int lim = near ? 1 : MOUSE_MAX_STEP;
			if (dx > lim) dx = lim; else if (dx < -lim) dx = -lim;
			if (dy > lim) dy = lim; else if (dy < -lim) dy = -lim;
			nw_adb_mouse_move(dx, dy);
			/* let the guest apply the report before reading Mouse again */
			not_before_us = now + (near ? 3 * MOUSE_STEP_US : MOUSE_STEP_US);
			return;
		}
		case ST_SHOT:
			take_shot(s.text, now);
			break;
		case ST_LOG:
			printf("NW-BOOT SCRIPT t=%.1f %s\n", el / 1e6, s.text.c_str());
			break;
		case ST_DUMP: {
			/* guest logical addresses in RAM only (the script has no MMU) */
			const uint32 la = (uint32)s.a, len = (uint32)s.b;
			if (la >= RAMSize || len > RAMSize - la) {
				printf("NW-BOOT SCRIPT dump: %08x+%x is not in RAM\n", la, len);
				break;
			}
			FILE *f = fopen(s.text.c_str(), "wb");
			if (f) {
				fwrite(Mac2HostAddr(la), 1, len, f);
				fclose(f);
			}
			printf("NW-BOOT SCRIPT t=%.1f dump %08x+%x -> %s\n", el / 1e6, la, len, s.text.c_str());
			break;
		}
		}
		head++;
	}
	if (periodics.empty())
		active = false;
}

int nw_script_active(void)
{
	return active ? 1 : 0;
}

#else

void nw_script_init(void) {}
void nw_script_tick(void) {}
int nw_script_active(void) { return 0; }

#endif
