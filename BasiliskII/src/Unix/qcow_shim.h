/*
 *  qcow_shim.h - C interface to QEMU's block layer (qcow v1, qcow2, qcow3)
 *
 *  The shim is compiled by tools/build_qemu_blocklib.sh against the pinned QEMU tree and archived into
 *  libssqcow.a. Nothing outside the shim includes a QEMU header.
 *
 *  All QEMU work runs on one private worker thread, which is the "main thread" of QEMU's block layer in a tools
 *  build (the thread that ran qemu_init_main_loop owns the main AioContext, and the block layer asserts that
 *  open/close/read/write happen there). Callers may use any thread; each call hands its request to the worker and
 *  waits for the answer. QEMU's own thread pool does the host pread/pwrite.
 */

#ifndef QCOW_SHIM_H
#define QCOW_SHIM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ss_qcow ss_qcow;

/* Bits reported through the info argument of ss_qcow_open. */
#define SS_QCOW_HAS_BACKING    1   /* the image's header names a backing file */
#define SS_QCOW_BACKING_FAILED 2   /* the open failed because the backing chain could not be opened */

/* version: 1 (original qcow driver) or 2/3 (qcow2 driver). A backing file is resolved as QEMU does (path relative to
 * the overlay, format from the header or probed, opened read-only, chains of any depth; guest writes go to the overlay
 * only). On failure returns NULL and writes a message to err. info (may be NULL) receives SS_QCOW_* bits. */
ss_qcow *ss_qcow_open(const char *path, int version, int read_only, int *info, char *err, size_t errlen);

/* "overlay (qcow2) -> base (qcow2) -> ..." as resolved at open time; valid until close. */
const char *ss_qcow_chain(ss_qcow *q);

/* Virtual size in bytes, or a negative errno. */
int64_t ss_qcow_size(ss_qcow *q);

/* 1 when the image was opened (or had to be opened) read-only. */
int ss_qcow_is_read_only(ss_qcow *q);

/* Return 0 on success, a negative errno on failure. */
int ss_qcow_pread(ss_qcow *q, void *buf, int64_t offset, size_t length);
int ss_qcow_pwrite(ss_qcow *q, const void *buf, int64_t offset, size_t length);

/* Flushes and closes. */
void ss_qcow_close(ss_qcow *q);

#ifdef __cplusplus
}
#endif

#endif
