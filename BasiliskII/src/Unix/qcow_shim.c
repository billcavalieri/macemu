/*
 *  qcow_shim.c - C interface to QEMU's block layer (qcow v1, qcow2, qcow3)
 *
 *  Compiled by tools/build_qemu_blocklib.sh with the include paths and defines of the pinned QEMU tree and archived
 *  into libssqcow.a. See qcow_shim.h for the threading model.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qobject/qdict.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "qemu/rcu.h"
#include "system/block-backend.h"
#include "block/block_int.h"
#include "crypto/init.h"

#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>

#include "qcow_shim.h"

struct ss_qcow {
    BlockBackend *blk;
    int64_t size;
    int read_only;
    int dirty;
    int has_backing;
    char chain[1024];
    struct ss_qcow *next;       /* open images, for the idle flush */
};

enum { J_OPEN, J_READ, J_WRITE, J_CLOSE };

struct job {
    int op;
    ss_qcow *q;
    void *buf;
    int64_t offset;
    size_t length;
    const char *path;
    int version;
    int read_only;
    int flags;                  /* SS_QCOW_* out */
    char *err;
    size_t errlen;
    int64_t result;
    int done;
    struct job *next;
};

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t work_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t done_cond = PTHREAD_COND_INITIALIZER;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static struct job *queue_head, *queue_tail;
static ss_qcow *open_images;
static int worker_ready;        /* 0 starting, 1 running, -1 failed */
static char worker_err[256];

static void set_err(char *err, size_t errlen, const char *msg)
{
    if (err && errlen) {
        snprintf(err, errlen, "%s", msg);
    }
}

static BlockBackend *open_image(struct job *j, int no_backing, Error **errp)
{
    QDict *opts = qdict_new();
    qdict_put_str(opts, "driver", j->version == 1 ? "qcow" : "qcow2");
    qdict_put_str(opts, "file.driver", "file");
    qdict_put_str(opts, "file.filename", j->path);
    int flags = (no_backing ? BDRV_O_NO_BACKING : 0) | (j->read_only ? 0 : BDRV_O_RDWR);
    return blk_new_open(NULL, NULL, opts, flags, errp);
}

/* QEMU follows the backing names in the headers without noticing a loop (an image that names itself, or two that name
 * each other, never finishes opening), and has no depth limit. Before the chain is opened, follow the names the same
 * way (qcow and qcow2 keep the backing name at the same header offsets) and refuse a file that comes round again or a
 * chain deeper than SS_MAX_CHAIN. A base that is not a qcow image ends the scan: it has no backing name of its own that
 * the qcow drivers would follow. Returns 0 when the chain is fine, else -1 with a message. */
#define SS_MAX_CHAIN 64

static int scan_chain(const char *top, char *msg, size_t msglen)
{
    dev_t devs[SS_MAX_CHAIN + 1];
    ino_t inos[SS_MAX_CHAIN + 1];
    char cur[PATH_MAX];
    snprintf(cur, sizeof cur, "%s", top);
    for (int depth = 0; depth <= SS_MAX_CHAIN; depth++) {
        struct stat st;
        if (stat(cur, &st) != 0) {
            return 0;               /* QEMU reports a missing file with its own message */
        }
        for (int i = 0; i < depth; i++) {
            if (devs[i] == st.st_dev && inos[i] == st.st_ino) {
                snprintf(msg, msglen, "backing chain loops back to \"%s\"", cur);
                return -1;
            }
        }
        devs[depth] = st.st_dev;
        inos[depth] = st.st_ino;
        int fd = open(cur, O_RDONLY);
        if (fd < 0) {
            return 0;
        }
        uint8_t h[20];
        ssize_t n = pread(fd, h, sizeof h, 0);
        if (n != (ssize_t)sizeof h || memcmp(h, "QFI\xfb", 4) != 0) {
            close(fd);
            return 0;
        }
        uint64_t off = 0;
        for (int i = 0; i < 8; i++) {
            off = (off << 8) | h[8 + i];
        }
        uint32_t len = ((uint32_t)h[16] << 24) | ((uint32_t)h[17] << 16) | ((uint32_t)h[18] << 8) | h[19];
        if (off == 0 || len == 0) {
            close(fd);
            return 0;               /* no backing file: the end of the chain */
        }
        if (len >= PATH_MAX) {
            close(fd);
            snprintf(msg, msglen, "backing file name in \"%s\" is too long", cur);
            return -1;
        }
        char name[PATH_MAX];
        n = pread(fd, name, len, (off_t)off);
        close(fd);
        if (n != (ssize_t)len) {
            return 0;
        }
        name[len] = 0;
        if (name[0] == '/') {
            snprintf(cur, sizeof cur, "%s", name);
        } else {
            char dir[PATH_MAX];
            snprintf(dir, sizeof dir, "%s", cur);
            char *slash = strrchr(dir, '/');
            if (slash) {
                *slash = 0;
                snprintf(cur, sizeof cur, "%s/%s", dir, name);
            } else {
                snprintf(cur, sizeof cur, "%s", name);
            }
        }
    }
    snprintf(msg, msglen, "backing chain is deeper than %d images", SS_MAX_CHAIN);
    return -1;
}

/* "overlay -> base1 (fmt) -> base2 (fmt)": the chain as QEMU resolved it, for the log. */
static void describe_chain(BlockDriverState *bs, char *out, size_t len)
{
    out[0] = 0;
    size_t used = 0;
    for (; bs && used < len; bs = bdrv_cow_bs(bs)) {
        int n = snprintf(out + used, len - used, "%s%s (%s)", used ? " -> " : "", bs->filename,
                         bdrv_get_format_name(bs));
        if (n < 0 || (size_t)n >= len - used) {
            break;
        }
        used += (size_t)n;
    }
}

static void do_open(struct job *j)
{
    Error *local = NULL;
    /* First look at the overlay alone: its header says whether it has a backing file. */
    BlockBackend *blk = open_image(j, 1, &local);
    if (!blk) {
        set_err(j->err, j->errlen, local ? error_get_pretty(local) : "open failed");
        error_free(local);
        j->result = -1;
        return;
    }
    int has_backing = blk_bs(blk)->backing_file[0] != 0;
    if (has_backing) {
        /* The chain is opened the way qemu-img does: the backing file named in the header (relative to the overlay's
         * directory), in the format the header names or probes, read-only, and so on down the chain. Any failure is
         * reported as a backing-chain failure; the caller does not start with a half-resolved chain. */
        char base[PATH_MAX];
        snprintf(base, sizeof base, "%s", blk_bs(blk)->backing_file);
        blk_unref(blk);
        char loopmsg[512];
        if (scan_chain(j->path, loopmsg, sizeof loopmsg) != 0) {
            set_err(j->err, j->errlen, loopmsg);
            j->flags = SS_QCOW_HAS_BACKING | SS_QCOW_BACKING_FAILED;
            j->result = -1;
            return;
        }
        blk = open_image(j, 0, &local);
        if (!blk) {
            char msg[1024];
            snprintf(msg, sizeof msg, "backing file \"%s\": %s", base,
                     local ? error_get_pretty(local) : "open failed");
            set_err(j->err, j->errlen, msg);
            error_free(local);
            j->flags = SS_QCOW_HAS_BACKING | SS_QCOW_BACKING_FAILED;
            j->result = -1;
            return;
        }
        if (!blk_bs(blk)->backing || !bdrv_cow_bs(blk_bs(blk))) {
            set_err(j->err, j->errlen, "backing file named in the header was not opened");
            blk_unref(blk);
            j->flags = SS_QCOW_HAS_BACKING | SS_QCOW_BACKING_FAILED;
            j->result = -1;
            return;
        }
    }
    int64_t size = blk_getlength(blk);
    if (size < 0) {
        set_err(j->err, j->errlen, strerror((int)-size));
        blk_unref(blk);
        j->flags = has_backing ? SS_QCOW_HAS_BACKING : 0;
        j->result = -1;
        return;
    }
    ss_qcow *q = g_new0(ss_qcow, 1);
    q->blk = blk;
    q->size = size;
    q->read_only = j->read_only;
    q->has_backing = has_backing;
    describe_chain(blk_bs(blk), q->chain, sizeof q->chain);
    q->next = open_images;
    open_images = q;
    j->q = q;
    j->flags = has_backing ? SS_QCOW_HAS_BACKING : 0;
    j->result = 0;
}

static void do_close(ss_qcow *q)
{
    ss_qcow **p = &open_images;
    while (*p && *p != q) {
        p = &(*p)->next;
    }
    if (*p) {
        *p = q->next;
    }
    if (!q->read_only) {
        blk_flush(q->blk);
    }
    blk_unref(q->blk);
    g_free(q);
}

static void flush_dirty(void)
{
    for (ss_qcow *q = open_images; q; q = q->next) {
        if (q->dirty) {
            q->dirty = 0;
            blk_flush(q->blk);
        }
    }
}

static int any_dirty(void)
{
    for (ss_qcow *q = open_images; q; q = q->next) {
        if (q->dirty) {
            return 1;
        }
    }
    return 0;
}

static void *worker_main(void *arg)
{
    (void)arg;
    /* Whatever QEMU's start-up does to the fault signals belongs to the emulator, not to QEMU. */
    struct sigaction old_segv, old_bus, old_ill;
    sigaction(SIGSEGV, NULL, &old_segv);
    sigaction(SIGBUS, NULL, &old_bus);
    sigaction(SIGILL, NULL, &old_ill);

    Error *local = NULL;
    error_init("ssqcow");
    module_call_init(MODULE_INIT_TRACE);
    int ok = qemu_init_main_loop(&local) == 0;
    if (ok) {
        ok = qcrypto_init(&local) == 0;
    }
    if (ok) {
        module_call_init(MODULE_INIT_QOM);
        bdrv_init();
        rcu_register_thread();
    }

    sigaction(SIGSEGV, &old_segv, NULL);
    sigaction(SIGBUS, &old_bus, NULL);
    sigaction(SIGILL, &old_ill, NULL);

    pthread_mutex_lock(&lock);
    if (!ok) {
        snprintf(worker_err, sizeof worker_err, "QEMU block layer init failed: %s",
                 local ? error_get_pretty(local) : "unknown error");
        worker_ready = -1;
        pthread_cond_broadcast(&done_cond);
        pthread_mutex_unlock(&lock);
        return NULL;
    }
    worker_ready = 1;
    pthread_cond_broadcast(&done_cond);

    for (;;) {
        while (!queue_head) {
            if (any_dirty()) {
                /* Idle: push cached qcow2 metadata and data out a second after the last write. */
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_sec += 1;
                if (pthread_cond_timedwait(&work_cond, &lock, &ts) != 0 && !queue_head) {
                    pthread_mutex_unlock(&lock);
                    flush_dirty();
                    pthread_mutex_lock(&lock);
                }
            } else {
                pthread_cond_wait(&work_cond, &lock);
            }
        }
        struct job *j = queue_head;
        queue_head = j->next;
        if (!queue_head) {
            queue_tail = NULL;
        }
        pthread_mutex_unlock(&lock);

        switch (j->op) {
        case J_OPEN:
            do_open(j);
            break;
        case J_READ:
            j->result = blk_pread(j->q->blk, j->offset, (int64_t)j->length, j->buf, 0);
            break;
        case J_WRITE:
            j->result = blk_pwrite(j->q->blk, j->offset, (int64_t)j->length, j->buf, 0);
            if (j->result == 0) {
                j->q->dirty = 1;
            }
            break;
        case J_CLOSE:
            do_close(j->q);
            j->result = 0;
            break;
        }

        pthread_mutex_lock(&lock);
        j->done = 1;
        pthread_cond_broadcast(&done_cond);
    }
    return NULL;
}

static void start_worker(void)
{
    pthread_t t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, 1 << 20);      /* QEMU coroutines run on their own stacks; the thread needs little */
    if (pthread_create(&t, &attr, worker_main, NULL) != 0) {
        worker_ready = -1;
        snprintf(worker_err, sizeof worker_err, "cannot start the QEMU worker thread");
    }
    pthread_attr_destroy(&attr);
}

/* Hand a job to the worker and wait for it. Returns 0 when the worker could not be started. */
static int run_job(struct job *j)
{
    pthread_once(&once, start_worker);
    pthread_mutex_lock(&lock);
    while (worker_ready == 0) {
        pthread_cond_wait(&done_cond, &lock);
    }
    if (worker_ready < 0) {
        set_err(j->err, j->errlen, worker_err);
        j->result = -1;
        pthread_mutex_unlock(&lock);
        return 0;
    }
    j->done = 0;
    j->next = NULL;
    if (queue_tail) {
        queue_tail->next = j;
    } else {
        queue_head = j;
    }
    queue_tail = j;
    pthread_cond_signal(&work_cond);
    while (!j->done) {
        pthread_cond_wait(&done_cond, &lock);
    }
    pthread_mutex_unlock(&lock);
    return 1;
}

ss_qcow *ss_qcow_open(const char *path, int version, int read_only, int *info, char *err, size_t errlen)
{
    struct job j;
    memset(&j, 0, sizeof j);
    j.op = J_OPEN;
    j.path = path;
    j.version = version;
    j.read_only = read_only;
    j.err = err;
    j.errlen = errlen;
    int ran = run_job(&j);
    if (info) {
        *info = ran ? j.flags : 0;
    }
    if (!ran || j.result != 0) {
        return NULL;
    }
    return j.q;
}

const char *ss_qcow_chain(ss_qcow *q)
{
    return q->chain;
}

int64_t ss_qcow_size(ss_qcow *q)
{
    return q->size;
}

int ss_qcow_is_read_only(ss_qcow *q)
{
    return q->read_only;
}

int ss_qcow_pread(ss_qcow *q, void *buf, int64_t offset, size_t length)
{
    struct job j;
    memset(&j, 0, sizeof j);
    j.op = J_READ;
    j.q = q;
    j.buf = buf;
    j.offset = offset;
    j.length = length;
    if (!run_job(&j)) {
        return -EIO;
    }
    return (int)j.result;
}

int ss_qcow_pwrite(ss_qcow *q, const void *buf, int64_t offset, size_t length)
{
    struct job j;
    memset(&j, 0, sizeof j);
    j.op = J_WRITE;
    j.q = q;
    j.buf = (void *)buf;
    j.offset = offset;
    j.length = length;
    if (!run_job(&j)) {
        return -EIO;
    }
    return (int)j.result;
}

void ss_qcow_close(ss_qcow *q)
{
    struct job j;
    memset(&j, 0, sizeof j);
    j.op = J_CLOSE;
    j.q = q;
    run_job(&j);
}
