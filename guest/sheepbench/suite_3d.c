/*
 *  suite_3d.c - 3D suite: every RAVE engine that takes an offscreen 640 x 480 ARGB32 draw context (SheepForce Metal,
 *  Apple's software engine, ...), each drawing flat, Gouraud and textured triangles, then Z-buffered triangles with
 *  alpha blending and with fog, at 100, 1000 and 10000 triangles a frame. Reported in frames per second (the
 *  number of triangles is in the test's name). A frame is QARenderStart, the triangles, QARenderEnd and QASync, so it
 *  includes getting the picture back into guest memory.
 *
 *  Correctness: one frame per engine with a red triangle on a blue background, read back at a point inside and one
 *  outside; an engine that draws something else has its rows marked FAILED.
 *
 *  RAVE is reached through the CFM library "QuickDraw(TM) 3D Accelerator" at run time (GetSharedLibrary and
 *  FindSymbol), so SheepBench runs where it is not installed. The few RAVE types and constants used are declared here
 *  from the RAVE interface as SheepShaver's own rave.cpp uses it (Apple's headers are not part of this project).
 *
 *  The QuickDraw 3D scene test (a mesh in a QuickDraw 3D view) is in suite_qd3d.c.
 *
 *  (C) 2026 Bill Cavalieri
 */

#include "suites.h"
#include "bench_host.h"
#include <MixedMode.h>
#include <Memory.h>
#include <stdio.h>
#include <string.h>

static const char kSuite[] = "3d";

/* RAVE, as far as this test needs it. */
typedef struct { long deviceType; long rowBytes, pixelType, width, height; void *baseAddr; } QADevice;   /* memory device */
typedef struct { long left, right, top, bottom; } QARect;
typedef struct { float x, y, z, invW, r, g, b, a; } QAVGouraud;
typedef struct { float x, y, z, invW, r, g, b, a, uOverW, vOverW, kd_r, kd_g, kd_b, ks_r, ks_g, ks_b; } QAVTexture;
typedef struct { long width, height, rowBytes; void *pixmap; } QAImage;
enum { kQADeviceMemory = 0, kQAPixel_ARGB32 = 4, kQAGestalt_ASCIIName = 6 };
enum { kTagZFunction = 0, kTagBlend = 9, kTagTextureFilter = 11, kTagTextureOp = 12, kTagTexture = 13,
       kTagFogMode = 17, kTagFogColorA = 18, kTagFogColorR = 19, kTagFogColorG = 20, kTagFogColorB = 21,
       kTagFogStart = 22, kTagFogEnd = 23 };
enum { kBlendInterpolate = 1, kFogLinear = 2 };
/* Draw context methods: the context is a table of TVECT pointers, method `tag` at word 2 + tag. */
enum { mSetFloat = 0, mSetInt = 1, mSetPtr = 2, mDrawTriGouraud = 8, mDrawTriTexture = 9, mRenderStart = 13,
       mRenderEnd = 14, mSync = 17 };

typedef void *(*FirstEngineFn)(const QADevice *);
typedef void *(*NextEngineFn)(const QADevice *, void *);
typedef long (*GestaltFn)(void *, long, void *);
typedef long (*CtxNewFn)(const QADevice *, const QARect *, void *, void *, unsigned long, void **);
typedef long (*CtxDeleteFn)(void *);
typedef long (*TexNewFn)(void *, unsigned long, long, const QAImage *, void **);
typedef long (*TexDeleteFn)(void *, void *);
typedef void (*SetFloatFn)(void *, long, float);
typedef void (*SetIntFn)(void *, long, unsigned long);
typedef void (*SetPtrFn)(void *, long, const void *);
typedef void (*TriGFn)(void *, const QAVGouraud *, const QAVGouraud *, const QAVGouraud *, unsigned long);
typedef void (*TriTFn)(void *, const QAVTexture *, const QAVTexture *, const QAVTexture *, unsigned long);
typedef void (*RenderStartFn)(void *, const QARect *, void *);
typedef long (*RenderEndFn)(void *, const QARect *);
typedef long (*SyncFn)(void *);

static struct {
    FirstEngineFn first;
    NextEngineFn next;
    GestaltFn gestalt;
    CtxNewFn ctxNew;
    CtxDeleteFn ctxDelete;
    TexNewFn texNew;
    TexDeleteFn texDelete;
} rave;

#define W 640
#define H 480
#define MAX_TRIS 10000

typedef struct {
    void *ctx;
    void **m;               /* the context's method table */
    int kind;               /* 0 flat, 1 Gouraud, 2 textured, 3 Z + blend, 4 Z + fog */
    int tris;
    QAVGouraud *g;          /* 3 per triangle */
    QAVTexture *t;
} FrameCtx;

static Boolean LoadRave(void)
{
    static int loaded = -1;
    ConnectionID conn;
    Ptr main, sym;
    SymClass cls;
    Str255 err;
    static const char *const names[] = { "\pQADeviceGetFirstEngine", "\pQADeviceGetNextEngine", "\pQAEngineGestalt",
                                         "\pQADrawContextNew", "\pQADrawContextDelete", "\pQATextureNew",
                                         "\pQATextureDelete" };
    void **slots[] = { (void **)&rave.first, (void **)&rave.next, (void **)&rave.gestalt, (void **)&rave.ctxNew,
                       (void **)&rave.ctxDelete, (void **)&rave.texNew, (void **)&rave.texDelete };
    int i;
    if (loaded >= 0)
        return loaded;
    loaded = 0;
    if (GetSharedLibrary("\pQuickDraw\xAA 3D Accelerator", 'pwpc', kReferenceCFrag, &conn, &main, err) != noErr)
        return false;
    for (i = 0; i < 7; i++) {
        if (FindSymbol(conn, (ConstStr255Param)names[i], &sym, &cls) != noErr)
            return false;
        *slots[i] = sym;
    }
    loaded = 1;
    return true;
}

static void SetF(FrameCtx *f, long tag, float v) { ((SetFloatFn)f->m[2 + mSetFloat])(f->ctx, tag, v); }
static void SetI(FrameCtx *f, long tag, unsigned long v) { ((SetIntFn)f->m[2 + mSetInt])(f->ctx, tag, v); }

static void Frame(FrameCtx *f)
{
    int i;
    ((RenderStartFn)f->m[2 + mRenderStart])(f->ctx, NULL, NULL);
    if (f->kind == 2) {
        TriTFn tri = (TriTFn)f->m[2 + mDrawTriTexture];
        for (i = 0; i < f->tris; i++)
            tri(f->ctx, &f->t[i * 3], &f->t[i * 3 + 1], &f->t[i * 3 + 2], 0);
    } else {
        TriGFn tri = (TriGFn)f->m[2 + mDrawTriGouraud];
        for (i = 0; i < f->tris; i++)
            tri(f->ctx, &f->g[i * 3], &f->g[i * 3 + 1], &f->g[i * 3 + 2], 0);
    }
    ((RenderEndFn)f->m[2 + mRenderEnd])(f->ctx, NULL);
    ((SyncFn)f->m[2 + mSync])(f->ctx);
}

static uint32_t FrameBody(void *p, uint32_t iters)
{
    uint32_t i;
    for (i = 0; i < iters; i++)
        Frame((FrameCtx *)p);
    return iters;
}

/* Triangles over the whole frame, about 40 pixels across, at varying depth; flat ones share one colour. */
static void MakeTriangles(QAVGouraud *g, QAVTexture *t, int n, Boolean flat)
{
    int i, k;
    uint32_t seed = 4321;
    for (i = 0; i < n; i++) {
        float cx, cy, z;
        seed = seed * 1664525u + 1013904223u;
        cx = (float)(seed >> 8 & 0x3FF) * (W - 40) / 1024.0f + 20;
        seed = seed * 1664525u + 1013904223u;
        cy = (float)(seed >> 8 & 0x3FF) * (H - 40) / 1024.0f + 20;
        z = (float)(i % 97) / 100.0f + 0.01f;
        for (k = 0; k < 3; k++) {
            QAVGouraud *v = &g[i * 3 + k];
            QAVTexture *u = &t[i * 3 + k];
            v->x = cx + (k == 1 ? 20.0f : -20.0f) * (k == 2 ? 0.0f : 1.0f);
            v->y = cy + (k == 2 ? 20.0f : -15.0f);
            v->z = z;
            v->invW = 1.0f;
            v->r = flat ? 0.9f : (k == 0 ? 1.0f : 0.1f);
            v->g = flat ? 0.4f : (k == 1 ? 1.0f : 0.1f);
            v->b = flat ? 0.1f : (k == 2 ? 1.0f : 0.1f);
            v->a = 0.5f;
            memset(u, 0, sizeof *u);
            u->x = v->x; u->y = v->y; u->z = v->z; u->invW = 1.0f;
            u->r = u->g = u->b = u->a = 1.0f;
            u->uOverW = k == 1 ? 1.0f : 0.0f;
            u->vOverW = k == 2 ? 1.0f : 0.0f;
            u->kd_r = u->kd_g = u->kd_b = 1.0f;
        }
    }
}

static uint32_t Pixel(const uint32_t *buf, int x, int y)
{
    return buf[y * W + x];
}

/* A red triangle on a blue background: inside must be red, a far corner blue. */
static Boolean CheckEngine(FrameCtx *f, uint32_t *buf, char *why)
{
    QAVGouraud v[3];
    int k;
    uint32_t in, out;
    for (k = 0; k < 3; k++) {
        v[k].x = k == 1 ? 300.0f : 100.0f;
        v[k].y = k == 2 ? 300.0f : 100.0f;
        v[k].z = 0.5f;
        v[k].invW = 1.0f;
        v[k].r = 1.0f; v[k].g = 0.0f; v[k].b = 0.0f; v[k].a = 1.0f;
    }
    SetF(f, 1, 1.0f); SetF(f, 2, 0.0f); SetF(f, 3, 0.0f); SetF(f, 4, 1.0f);     /* background: opaque blue */
    ((RenderStartFn)f->m[2 + mRenderStart])(f->ctx, NULL, NULL);
    ((TriGFn)f->m[2 + mDrawTriGouraud])(f->ctx, &v[0], &v[1], &v[2], 0);
    ((RenderEndFn)f->m[2 + mRenderEnd])(f->ctx, NULL);
    ((SyncFn)f->m[2 + mSync])(f->ctx);
    in = Pixel(buf, 150, 150) & 0xFFFFFF;
    out = Pixel(buf, 600, 440) & 0xFFFFFF;
    if (in != 0xFF0000 || out != 0x0000FF) {
        sprintf(why, "drew %06lx inside a red triangle and %06lx on blue", (unsigned long)in, (unsigned long)out);
        return false;
    }
    return true;
}

static void RunEngine(void *engine, const char *engineName, QADevice *dev, uint32_t *buf, QAVGouraud *flat,
                      QAVGouraud *shaded, QAVTexture *tex, void *texture)
{
    static const char *const kinds[] = { "flat", "Gouraud", "textured", "Z buffer + alpha blend", "Z buffer + fog" };
    static const int counts[] = { 100, 1000, 10000 };
    QARect rect = { 0, W, 0, H };
    FrameCtx f;
    char why[96], name[96];
    int kind, c;
    Boolean ok;
    memset(&f, 0, sizeof f);
    if (rave.ctxNew(dev, &rect, NULL, engine, 0, &f.ctx) != 0 || !f.ctx) {
        sprintf(name, "%s: draw context", engineName);
        RowSkip(TestBegin(kSuite, name, unitPerSec), "the engine refused a 640 x 480 ARGB32 offscreen context");
        return;
    }
    f.m = (void **)f.ctx;
    ok = CheckEngine(&f, buf, why);
    for (kind = 0; kind < 5 && !gCancelled; kind++)
        for (c = 0; c < 3 && !gCancelled; c++) {
            BenchRow *row;
            Measurement m;
            sprintf(name, "%s: %s, %d triangles", engineName, kinds[kind], counts[c]);
            row = TestBegin(kSuite, name, unitPerSec);
            if (!row)
                continue;
            if (!ok) {
                RowFail(row, why);
                continue;
            }
            if (kind == 2 && !texture) {
                RowSkip(row, "the engine made no texture");
                continue;
            }
            f.kind = kind;
            f.tris = counts[c];
            f.g = kind == 0 ? flat : shaded;
            f.t = tex;
            SetI(&f, kTagBlend, kind == 3 ? kBlendInterpolate : 0);
            SetI(&f, kTagFogMode, kind == 4 ? kFogLinear : 0);
            if (kind == 4) {
                SetF(&f, kTagFogColorA, 1.0f); SetF(&f, kTagFogColorR, 0.5f);
                SetF(&f, kTagFogColorG, 0.5f); SetF(&f, kTagFogColorB, 0.5f);
                SetF(&f, kTagFogStart, 0.1f); SetF(&f, kTagFogEnd, 0.9f);
            }
            if (kind == 2) {
                ((SetPtrFn)f.m[2 + mSetPtr])(f.ctx, kTagTexture, texture);
                SetI(&f, kTagTextureOp, 0);
                SetI(&f, kTagTextureFilter, 1);
            }
            Measure(FrameBody, &f, 1, &m);
            if (m.cancelled)
                gCancelled = true;
            TestRecord(row, &m, unitPerSec, 1);
        }
    rave.ctxDelete(f.ctx);
}

static void RunRave(void)
{
    Handle bufTemp;
    void *bufRaw;
    uint32_t *buf;
    QADevice dev;
    QAVGouraud *flat, *shaded;
    QAVTexture *tex;
    void *engine;
    int n = 0;
    if (!LoadRave()) {
        RowSkip(TestBegin(kSuite, "RAVE engines", unitPerSec), "RAVE (QuickDraw 3D Accelerator) is not installed");
        return;
    }
    buf = (uint32_t *)(bufRaw = BenchAlloc(W * H * 4 + 16, &bufTemp));
    flat = (QAVGouraud *)NewPtr(sizeof(QAVGouraud) * 3 * MAX_TRIS);
    shaded = (QAVGouraud *)NewPtr(sizeof(QAVGouraud) * 3 * MAX_TRIS);
    tex = (QAVTexture *)NewPtr(sizeof(QAVTexture) * 3 * MAX_TRIS);
    if (!buf || !flat || !shaded || !tex) {
        RowSkip(TestBegin(kSuite, "RAVE engines", unitPerSec), "not enough memory");
        goto done;
    }
    buf = (uint32_t *)(((uint32_t)buf + 15) & ~15UL);
    MakeTriangles(flat, tex, MAX_TRIS, true);
    MakeTriangles(shaded, tex, MAX_TRIS, false);
    memset(&dev, 0, sizeof dev);
    dev.deviceType = kQADeviceMemory;
    dev.rowBytes = W * 4;
    dev.pixelType = kQAPixel_ARGB32;
    dev.width = W;
    dev.height = H;
    dev.baseAddr = buf;
    for (engine = rave.first(&dev); engine && n < 6 && !gCancelled; engine = rave.next(&dev, engine), n++) {
        char name[64];
        static uint32_t texels[64 * 64];
        QAImage img;
        void *texture = NULL;
        int i;
        memset(name, 0, sizeof name);
        if (rave.gestalt(engine, kQAGestalt_ASCIIName, name) != 0 || !name[0])
            sprintf(name, "engine %d", n + 1);
        name[40] = 0;
        {
            char line[96];
            sprintf(line, "3d: engine %d is %s (%08lx)", n + 1, name, (unsigned long)engine);
            HostLog(line);
        }
        for (i = 0; i < 64 * 64; i++)
            texels[i] = 0xFF000000UL | (((i / 64 + i % 64) & 8) ? 0xFFFFFF : 0x2060C0);
        img.width = 64;
        img.height = 64;
        img.rowBytes = 64 * 4;
        img.pixmap = texels;
        if (rave.texNew(engine, 0, kQAPixel_ARGB32, &img, &texture) != 0)
            texture = NULL;
        RunEngine(engine, name, &dev, buf, flat, shaded, tex, texture);
        if (texture)
            rave.texDelete(engine, texture);
    }
    if (n == 0)
        RowSkip(TestBegin(kSuite, "RAVE engines", unitPerSec), "no engine takes an offscreen ARGB32 context");
done:
    if (flat) DisposePtr((Ptr)flat);
    if (shaded) DisposePtr((Ptr)shaded);
    if (tex) DisposePtr((Ptr)tex);
    BenchFree(bufRaw, bufTemp);
}

void Suite3D(void)
{
    RunRave();
    SuiteQD3D();
}
