/*
 *  disk_chd.cpp - hard-disk CHD images through MAME's CHD file code
 *
 *  Same contract as vhd_unix.cpp: DISK_UNKNOWN if the file is not a CHD, DISK_INVALID if the magic matches but the
 *  image cannot be used (version 1/2, CD/GD-ROM/DVD/A-V images, a file that already has a parent, damage), DISK_VALID
 *  only with a live object. Compiled only where HAVE_CHD is defined (the Xcode SheepShaver target). The write modes
 *  (in place, diff beside a compressed original, read-only) are decided in chd_shim.cpp.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "sysdeps.h"

#if defined(HAVE_CHD)

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "disk_unix.h"
#include "macos_util.h"
#include "chd_shim.h"

struct disk_chd : disk_generic {
	disk_chd(ss_chd *img) : img(img), start_byte(0), file_size(0) { }

	~disk_chd()
	{
		ss_chd_close(img);
	}

	bool is_read_only() { return ss_chd_is_read_only(img) != 0; }
	loff_t size() { return file_size; }

	size_t read(void *buf, loff_t offset, size_t length)
	{
		if (offset < 0 || offset >= file_size)
			return 0;
		if ((loff_t)length > file_size - offset)
			length = (size_t)(file_size - offset);
		if (length == 0)
			return 0;
		return ss_chd_read(img, buf, start_byte + offset, length) == 0 ? length : 0;
	}

	size_t write(void *buf, loff_t offset, size_t length)
	{
		if (is_read_only() || offset < 0 || offset >= file_size)
			return 0;
		if ((loff_t)length > file_size - offset)
			length = (size_t)(file_size - offset);
		if (length == 0)
			return 0;
		return ss_chd_write(img, buf, start_byte + offset, length) == 0 ? length : 0;
	}

	// Same header handling as a raw file (a converted DiskCopy image has a header to skip).
	void layout()
	{
		int64_t total = ss_chd_size(img);
		uint8 data[256];
		memset(data, 0, sizeof(data));
		size_t n = total < (int64_t)sizeof(data) ? (size_t)total : sizeof(data);
		if (n)
			ss_chd_read(img, data, 0, n);
		FileDiskLayout((loff_t)total, data, start_byte, file_size);
	}

	ss_chd *img;
	loff_t start_byte, file_size;
};

disk_generic::status disk_chd_factory(const char *path, bool read_only, disk_generic **disk)
{
	struct stat st;
	if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
		return disk_generic::DISK_UNKNOWN;

	char err[512] = "";
	int status = SS_CHD_NOT_CHD;
	ss_chd *img = ss_chd_open(path, read_only, &status, err, sizeof(err));
	if (!img) {
		if (status == SS_CHD_NOT_CHD)
			return disk_generic::DISK_UNKNOWN;
		printf("WARNING: cannot use CHD image %s: %s\n", path, err);
		return disk_generic::DISK_INVALID;
	}

	disk_chd *c = new disk_chd(img);
	c->layout();
	*disk = c;
	return disk_generic::DISK_VALID;
}

#endif // HAVE_CHD
