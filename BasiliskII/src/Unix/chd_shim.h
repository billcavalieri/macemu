/*
 *  chd_shim.h - C interface to MAME's CHD file code (hard-disk CHDs)
 *
 *  The shim is compiled by tools/build_chdlib.sh into libsschd.a together with the CHD sources of the pinned
 *  third_party/mame tree. Nothing outside the shim includes a MAME header.
 *
 *  Open modes (decided inside ss_chd_open, see DISK-IMAGES.md):
 *    - version 5, uncompressed, writeable: written in place by MAME's write_hunk
 *    - version 5, compressed, writeable: the compressed file is the (read-only) parent; guest writes go to an
 *      uncompressed version 5 diff "<path>.ssdiff.chd" created beside it, reused on later opens when its parent hash
 *      matches
 *    - version 3 and 4, or any file opened read-only: read-only
 *  Calls are made from one thread (the emulator thread); there is no locking.
 */

#ifndef CHD_SHIM_H
#define CHD_SHIM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ss_chd ss_chd;

enum {
	SS_CHD_NOT_CHD = 0,		/* the file does not start with the CHD magic */
	SS_CHD_OPENED = 1,
	SS_CHD_UNUSABLE = 2		/* CHD magic, but not usable: version 1/2, CD/GD/DVD/AV, has a parent, damaged, ... */
};

/* Returns NULL when *status is NOT_CHD or UNUSABLE (err then holds a message). */
ss_chd *ss_chd_open(const char *path, int read_only, int *status, char *err, size_t errlen);

int64_t ss_chd_size(ss_chd *c);			/* hunk count times hunk bytes */
int ss_chd_is_read_only(ss_chd *c);

/* 0 on success, a nonzero MAME error value otherwise. */
int ss_chd_read(ss_chd *c, void *buf, int64_t offset, size_t length);
int ss_chd_write(ss_chd *c, const void *buf, int64_t offset, size_t length);

void ss_chd_close(ss_chd *c);

#ifdef __cplusplus
}
#endif

#endif
