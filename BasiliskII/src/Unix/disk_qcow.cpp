/*
 *  disk_qcow.cpp - qcow v1, qcow2 and qcow3 disk images through QEMU's block layer
 *
 *  Same contract as vhd_unix.cpp: DISK_UNKNOWN if the file is not a qcow image, DISK_INVALID if the magic matches
 *  but the image cannot be used (unsupported version, encryption, damage, a backing file that cannot be opened), DISK_VALID only with a live
 *  object. A backing file is resolved as QEMU does; if it cannot be, the whole start-up fails (sys_disk_startup_failed).
 *  QEMU's own drivers do the work (see qcow_shim.h and tools/build_qemu_blocklib.sh); compiled only where
 *  HAVE_QCOW2 is defined (the Xcode SheepShaver target).
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "sysdeps.h"

#if defined(HAVE_QCOW2)

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "disk_unix.h"
#include "macos_util.h"
#include "qcow_shim.h"

struct disk_qcow : disk_generic {
	disk_qcow(ss_qcow *img, bool read_only) : img(img), ro(read_only), start_byte(0), file_size(0) { }

	~disk_qcow()
	{
		ss_qcow_close(img);
	}

	bool is_read_only() { return ro; }

	// The virtual image, minus the header FileDiskLayout recognizes in a converted DiskCopy image (the same bytes a
	// raw file skips).
	loff_t size() { return file_size; }

	size_t read(void *buf, loff_t offset, size_t length)
	{
		if (offset < 0 || offset >= file_size)
			return 0;
		if ((loff_t)length > file_size - offset)
			length = (size_t)(file_size - offset);
		if (length == 0)
			return 0;
		return ss_qcow_pread(img, buf, start_byte + offset, length) == 0 ? length : 0;
	}

	size_t write(void *buf, loff_t offset, size_t length)
	{
		if (ro || offset < 0 || offset >= file_size)
			return 0;
		if ((loff_t)length > file_size - offset)
			length = (size_t)(file_size - offset);
		if (length == 0)
			return 0;
		return ss_qcow_pwrite(img, buf, start_byte + offset, length) == 0 ? length : 0;
	}

	void layout()
	{
		int64_t total = ss_qcow_size(img);
		uint8 data[256];
		memset(data, 0, sizeof(data));
		size_t n = total < (int64_t)sizeof(data) ? (size_t)total : sizeof(data);
		if (n)
			ss_qcow_pread(img, data, 0, n);
		FileDiskLayout((loff_t)total, data, start_byte, file_size);
	}

	ss_qcow *img;
	bool ro;
	loff_t start_byte, file_size;
};

disk_generic::status disk_qcow_factory(const char *path, bool read_only, disk_generic **disk)
{
	struct stat st;
	if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
		return disk_generic::DISK_UNKNOWN;

	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return disk_generic::DISK_UNKNOWN;
	uint8 head[8];
	ssize_t n = pread(fd, head, sizeof(head), 0);
	close(fd);
	if (n != (ssize_t)sizeof(head) || memcmp(head, "QFI\xfb", 4) != 0)
		return disk_generic::DISK_UNKNOWN;

	// The magic matches: from here on a failure must not fall back to mounting the file as a raw disk.
	int version = (head[4] << 24) | (head[5] << 16) | (head[6] << 8) | head[7];
	if (version < 1 || version > 3) {
		printf("WARNING: %s: unsupported qcow version %d\n", path, version);
		return disk_generic::DISK_INVALID;
	}

	char err[1536] = "";
	int info = 0;
	ss_qcow *img = ss_qcow_open(path, version, read_only, &info, err, sizeof(err));
	if (!img) {
		if (info & SS_QCOW_BACKING_FAILED) {
			// An overlay without its base would read as a different disk. Do not start with it missing.
			char msg[1792];
			snprintf(msg, sizeof(msg), "Cannot start: the qcow image %s needs a backing file that cannot be used: %s",
			         path, err);
			printf("ERROR: %s\n", msg);
			fflush(stdout);
			sys_disk_startup_failed(msg);
		} else {
			printf("WARNING: cannot open qcow image %s: %s\n", path, err);
		}
		return disk_generic::DISK_INVALID;
	}
	if (info & SS_QCOW_HAS_BACKING)
		printf("qcow image %s opens with its backing chain: %s\n", path, ss_qcow_chain(img));

	disk_qcow *q = new disk_qcow(img, ss_qcow_is_read_only(img) != 0);
	q->layout();
	*disk = q;
	return disk_generic::DISK_VALID;
}

#endif // HAVE_QCOW2
