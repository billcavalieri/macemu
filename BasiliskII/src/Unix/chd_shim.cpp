/*
 *  chd_shim.cpp - C interface to MAME's CHD file code (hard-disk CHDs)
 *
 *  Compiled by tools/build_chdlib.sh (C++20, MAME include paths) into libsschd.a. See chd_shim.h.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "chd.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <unistd.h>

#include "chd_shim.h"
#include "osdcore.h"
#include <cstdlib>

// What MAME's osd layer provides for the work queue (osdsync.cpp, linked for chd_file_compressor, which the app never
// runs): the environment and the processor count.
const char *osd_getenv(const char *name) { return getenv(name); }
int osd_num_processors() { long n = sysconf(_SC_NPROCESSORS_ONLN); return n > 0 ? (int)n : 1; }

struct ss_chd {
	std::unique_ptr<chd_file> parent;	// the compressed original when writes go to a diff
	std::unique_ptr<chd_file> file;		// the file that is read and written
	bool read_only = true;
	bool diff = false;			// a diff over a parent: all-zero hunks need special care (see store)
	uint32_t hunk_bytes = 0;
	uint32_t hunk_count = 0;
	uint32_t cached = ~0u;
	std::vector<uint8_t> cache;
};

static void set_err(char *err, size_t errlen, const std::string &msg)
{
	if (err && errlen)
		snprintf(err, errlen, "%s", msg.c_str());
}

static std::string describe(const std::error_condition &e)
{
	return e.message();
}

static bool is_zero(const uint8_t *p, size_t n)
{
	for (size_t i = 0; i < n; i++)
		if (p[i])
			return false;
	return true;
}

ss_chd *ss_chd_open(const char *path, int read_only, int *status, char *err, size_t errlen)
{
	*status = SS_CHD_NOT_CHD;
	{
		FILE *f = fopen(path, "rb");
		if (!f)
			return nullptr;
		char magic[8] = {0};
		size_t n = fread(magic, 1, 8, f);
		fclose(f);
		if (n != 8 || memcmp(magic, "MComprHD", 8) != 0)
			return nullptr;
	}
	// From here on the magic matches: a failure is UNUSABLE, never "not a CHD".
	*status = SS_CHD_UNUSABLE;

	std::unique_ptr<chd_file> probe(new chd_file);
	if (std::error_condition e = probe->open(path, false)) {
		set_err(err, errlen, "cannot read CHD: " + describe(e));
		return nullptr;
	}
	if (probe->parent_missing() || probe->parent_sha1() != util::sha1_t::null) {
		set_err(err, errlen, "CHD files with a parent are not supported");
		return nullptr;
	}
	// check_is_*() return an error when the file is NOT of that kind.
	const bool is_hd = !probe->check_is_hd();
	if (!is_hd && (!probe->check_is_cd() || !probe->check_is_gd() || !probe->check_is_dvd() || !probe->check_is_av() ||
	               probe->unit_bytes() != 512)) {
		set_err(err, errlen, "only hard-disk CHDs are supported (CD, GD-ROM, DVD and A/V images are not)");
		return nullptr;
	}
	if (probe->hunk_bytes() == 0 || probe->hunk_bytes() % 512 != 0 || probe->hunk_count() == 0) {
		set_err(err, errlen, "CHD hunk size is not a multiple of 512 bytes");
		return nullptr;
	}

	std::unique_ptr<ss_chd> c(new ss_chd);
	c->hunk_bytes = probe->hunk_bytes();
	c->hunk_count = probe->hunk_count();
	c->cache.resize(c->hunk_bytes);

	if (read_only || probe->version() < 5) {
		c->file = std::move(probe);
		c->read_only = true;
	} else if (!probe->compressed()) {
		// Uncompressed version 5: write in place when the file can be opened for writing, else read-only.
		std::unique_ptr<chd_file> rw(new chd_file);
		if (!rw->open(path, true)) {
			c->file = std::move(rw);
			c->read_only = false;
		} else {
			c->file = std::move(probe);
			c->read_only = true;
		}
	} else {
		// Compressed version 5: MAME cannot rewrite a compressed file. The original becomes the parent of an
		// uncompressed diff, which takes the guest's writes.
		const std::string diffpath = std::string(path) + ".ssdiff.chd";
		c->parent = std::move(probe);
		std::unique_ptr<chd_file> d(new chd_file);
		std::error_condition e;
		if (access(diffpath.c_str(), F_OK) == 0) {
			e = d->open(diffpath, true, c->parent.get());
			if (e) {
				set_err(err, errlen, "cannot reuse " + diffpath + " (" + describe(e) + "); it does not belong to this image. Move it away to start over");
				return nullptr;
			}
		} else {
			const chd_codec_type none[4] = { CHD_CODEC_NONE, CHD_CODEC_NONE, CHD_CODEC_NONE, CHD_CODEC_NONE };
			e = d->create(diffpath, c->parent->logical_bytes(), c->parent->hunk_bytes(), none, *c->parent);
			if (e) {
				// Cannot create the diff (read-only folder): fall back to read-only access to the original.
				c->file = std::move(c->parent);
				c->read_only = true;
				*status = SS_CHD_OPENED;
				return c.release();
			}
		}
		c->file = std::move(d);
		c->read_only = false;
		c->diff = true;
	}
	*status = SS_CHD_OPENED;
	return c.release();
}

int64_t ss_chd_size(ss_chd *c)
{
	return (int64_t)c->hunk_count * (int64_t)c->hunk_bytes;
}

int ss_chd_is_read_only(ss_chd *c)
{
	return c->read_only;
}

static int load(ss_chd *c, uint32_t h)
{
	if (c->cached == h)
		return 0;
	std::error_condition e = c->file->read_hunk(h, c->cache.data());
	if (e) {
		c->cached = ~0u;
		return e.value() ? e.value() : -1;
	}
	c->cached = h;
	return 0;
}

// Writes the cached hunk. MAME skips an all-zero write to a hunk it has not allocated, which in a diff would leave
// the parent's data visible; allocate the hunk with a non-zero byte first, then overwrite it with the zeros.
static int store(ss_chd *c, uint32_t h)
{
	if (c->diff && is_zero(c->cache.data(), c->hunk_bytes)) {
		std::vector<uint8_t> mark(c->hunk_bytes, 0);
		mark[0] = 1;
		if (std::error_condition e = c->file->write_hunk(h, mark.data()))
			return e.value() ? e.value() : -1;
	}
	std::error_condition e = c->file->write_hunk(h, c->cache.data());
	return e ? (e.value() ? e.value() : -1) : 0;
}

int ss_chd_read(ss_chd *c, void *buf, int64_t offset, size_t length)
{
	uint8_t *dst = (uint8_t *)buf;
	while (length) {
		uint32_t h = (uint32_t)(offset / c->hunk_bytes);
		uint32_t o = (uint32_t)(offset % c->hunk_bytes);
		if (h >= c->hunk_count)
			return -1;
		if (int r = load(c, h))
			return r;
		size_t n = c->hunk_bytes - o;
		if (n > length)
			n = length;
		memcpy(dst, &c->cache[o], n);
		dst += n; offset += n; length -= n;
	}
	return 0;
}

int ss_chd_write(ss_chd *c, const void *buf, int64_t offset, size_t length)
{
	if (c->read_only)
		return -1;
	const uint8_t *src = (const uint8_t *)buf;
	while (length) {
		uint32_t h = (uint32_t)(offset / c->hunk_bytes);
		uint32_t o = (uint32_t)(offset % c->hunk_bytes);
		if (h >= c->hunk_count)
			return -1;
		size_t n = c->hunk_bytes - o;
		if (n > length)
			n = length;
		if (n != c->hunk_bytes) {
			if (int r = load(c, h))
				return r;
		} else {
			c->cached = h;
		}
		memcpy(&c->cache[o], src, n);
		if (int r = store(c, h)) {
			c->cached = ~0u;
			return r;
		}
		src += n; offset += n; length -= n;
	}
	return 0;
}

void ss_chd_close(ss_chd *c)
{
	if (c) {
		c->file.reset();
		c->parent.reset();
		delete c;
	}
}
