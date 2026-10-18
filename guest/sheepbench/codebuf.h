/*
 *  codebuf.h - Machine code written at run time (CPU loops, the MMU suite's self-modifying code): page-aligned
 *  pages, made executable with MakeDataExecutable, called through a transition vector like any CFM function pointer.
 *
 *  (C) 2026 Bill Cavalieri
 */

#ifndef CODEBUF_H
#define CODEBUF_H

#include <MacTypes.h>
#include <Memory.h>
#include <stdint.h>

typedef struct {
    Ptr raw;
    unsigned char *base;    /* 4096-aligned */
    int pages;
    uint32_t tvec[2];       /* code address, TOC */
} CodeBuf;

static Boolean CodeBufOpen(CodeBuf *cb, int pages)
{
    cb->pages = pages;
    cb->raw = NewPtrClear((Size)(pages + 1) * 4096);
    if (!cb->raw)
        return false;
    cb->base = (unsigned char *)(((uint32_t)cb->raw + 4095) & ~4095UL);
    return true;
}

static void *CodeBufPage(CodeBuf *cb, int page)
{
    return cb->base + page * 4096;
}

/* Flushes the data cache and invalidates the instruction cache over all pages (the words count is informative). */
static void CodeBufCommit(CodeBuf *cb, int words)
{
    (void)words;
    MakeDataExecutable(cb->base, (unsigned long)cb->pages * 4096);
}

/* A function pointer (a transition vector) for code at `code`, with this program's TOC. */
static void *CodeBufEntry(CodeBuf *cb, void *code)
{
    uint32_t toc;
    __asm__ volatile("mr %0,2" : "=r"(toc));
    cb->tvec[0] = (uint32_t)code;
    cb->tvec[1] = toc;
    return cb->tvec;
}

static void CodeBufClose(CodeBuf *cb)
{
    if (cb->raw)
        DisposePtr(cb->raw);
    cb->raw = NULL;
}

#endif
