/*
 *  suite_qd3d.c - The QuickDraw 3D scene test (part of the 3D suite): a grid of 1000 and 10000 triangles drawn by
 *  QuickDraw 3D's interactive renderer into a 640 x 480 offscreen pixmap, with an ambient light and a perspective
 *  camera, in frames per second. Once with the engine QuickDraw 3D picks and once asking for SheepForce Metal.
 *  The interactive renderer draws through RAVE, so this measures RAVE as applications use it, plus QuickDraw 3D's
 *  own transform work on the guest CPU.
 *
 *  Correctness: one frame with a single triangle over the middle of a black picture: the middle must not be black,
 *  a corner must be.
 *
 *  QuickDraw 3D is reached at run time ("QuickDraw(TM) 3D", GetSharedLibrary/FindSymbol), so SheepBench runs
 *  without it. Its types are declared here as far as this test needs them (Apple's headers are not part of this
 *  project): enums are four bytes, as in the QuickDraw 3D interface.
 *
 *  (C) 2026 Bill Cavalieri
 */

#include "suites.h"
#include <MixedMode.h>
#include <Memory.h>
#include <stdio.h>
#include <string.h>

static const char kSuite[] = "3d";

typedef void *TQ3Object;
typedef struct { float x, y; } Q3Point2D;
typedef struct { float x, y, z; } Q3Point3D;
typedef struct { float a, r, g, b; } Q3ColorARGB;
typedef struct { float r, g, b; } Q3ColorRGB;
typedef struct { Q3Point2D min, max; } Q3Area;
typedef struct { unsigned char *image; unsigned long width, height, rowBytes; long bitOrder; } Q3Bitmap;
typedef struct {
    long clearImageMethod;
    Q3ColorARGB clearImageColor;
    Q3Area pane;
    long paneState;
    Q3Bitmap mask;
    long maskState;
    long doubleBufferState;
} Q3DrawContextData;
typedef struct { void *image; unsigned long width, height, rowBytes, pixelSize; long pixelType, bitOrder, byteOrder; } Q3Pixmap;
typedef struct { Q3DrawContextData drawContextData; Q3Pixmap pixmap; } Q3PixmapDrawContextData;
typedef struct {
    Q3Point3D cameraLocation, pointOfInterest, upVector;
    float hither, yon;
    Q3Point2D origin;
    float width, height;
    float fov, aspectRatioXToY;
} Q3ViewAngleAspectCameraData;
typedef struct { long isOn; float brightness; Q3ColorRGB color; } Q3LightData;
typedef struct { Q3Point3D point; TQ3Object attributeSet; } Q3Vertex3D;
typedef struct { Q3Vertex3D vertices[3]; TQ3Object triangleAttributeSet; } Q3TriangleData;

enum { kQ3Success = 1, kQ3ViewStatusRetraverse = 1, kQ3ClearMethodWithColor = 1, kQ3PixelTypeARGB32 = 1,
       kQ3EndianBig = 0 };
#define kQ3RendererTypeInteractive 0x6374776EUL     /* 'ctwn' */

static struct {
    long (*initialize)(void);
    TQ3Object (*viewNew)(void);
    TQ3Object (*rendererNewFromType)(unsigned long);
    long (*viewSetRenderer)(TQ3Object, TQ3Object);
    TQ3Object (*pixmapDrawContextNew)(const Q3PixmapDrawContextData *);
    long (*viewSetDrawContext)(TQ3Object, TQ3Object);
    TQ3Object (*cameraNew)(const Q3ViewAngleAspectCameraData *);
    long (*viewSetCamera)(TQ3Object, TQ3Object);
    TQ3Object (*ambientLightNew)(const Q3LightData *);
    TQ3Object (*lightGroupNew)(void);
    TQ3Object (*groupAddObject)(TQ3Object, TQ3Object);
    long (*viewSetLightGroup)(TQ3Object, TQ3Object);
    long (*viewStartRendering)(TQ3Object);
    long (*viewEndRendering)(TQ3Object);
    long (*triangleSubmit)(const Q3TriangleData *, TQ3Object);
    long (*objectDispose)(TQ3Object);
    long (*setPreferences)(TQ3Object, long, long);
    long (*viewSync)(TQ3Object);
} q3;

static Boolean LoadQD3D(void)
{
    static int loaded = -1;
    static const char *const names[] = {
        "\pQ3Initialize", "\pQ3View_New", "\pQ3Renderer_NewFromType", "\pQ3View_SetRenderer",
        "\pQ3PixmapDrawContext_New", "\pQ3View_SetDrawContext", "\pQ3ViewAngleAspectCamera_New", "\pQ3View_SetCamera",
        "\pQ3AmbientLight_New", "\pQ3LightGroup_New", "\pQ3Group_AddObject", "\pQ3View_SetLightGroup",
        "\pQ3View_StartRendering", "\pQ3View_EndRendering", "\pQ3Triangle_Submit", "\pQ3Object_Dispose",
        "\pQ3InteractiveRenderer_SetPreferences", "\pQ3View_Sync" };
    void **slots = (void **)&q3;
    ConnectionID conn;
    Ptr main, sym;
    SymClass cls;
    Str255 err;
    int i;
    if (loaded >= 0)
        return loaded;
    loaded = 0;
    if (GetSharedLibrary("\pQuickDraw\xAA 3D", 'pwpc', kReferenceCFrag, &conn, &main, err) != noErr)
        return false;
    for (i = 0; i < (int)(sizeof names / sizeof names[0]); i++) {
        if (FindSymbol(conn, (ConstStr255Param)names[i], &sym, &cls) != noErr)
            return false;
        slots[i] = sym;
    }
    if (q3.initialize() != kQ3Success)
        return false;
    loaded = 1;
    return true;
}

#define W 640
#define H 480

typedef struct {
    TQ3Object view;
    Q3TriangleData *tris;
    int count;
} SceneCtx;

static void Draw(SceneCtx *s)
{
    int i;
    q3.viewStartRendering(s->view);
    do {
        for (i = 0; i < s->count; i++)
            q3.triangleSubmit(&s->tris[i], s->view);
    } while (q3.viewEndRendering(s->view) == kQ3ViewStatusRetraverse);
    q3.viewSync(s->view);
}

static uint32_t Body(void *p, uint32_t iters)
{
    uint32_t i;
    for (i = 0; i < iters; i++)
        Draw((SceneCtx *)p);
    return iters;
}

/* A grid of triangles in the plane z = 0, seen from 3 units away. */
static void Grid(Q3TriangleData *t, int count)
{
    int side = 1, i, k;
    while (side * side * 2 < count)
        side++;
    for (i = 0; i < count; i++) {
        int cell = i / 2, cx = cell % side, cy = cell / side;
        float x0 = -1.0f + 2.0f * cx / side, y0 = -1.0f + 2.0f * cy / side, d = 2.0f / side;
        Q3Point3D p[4] = { { x0, y0, 0 }, { x0 + d, y0, 0 }, { x0, y0 + d, 0 }, { x0 + d, y0 + d, 0 } };
        static const int idx[2][3] = { { 0, 1, 2 }, { 1, 3, 2 } };
        for (k = 0; k < 3; k++) {
            t[i].vertices[k].point = p[idx[i & 1][k]];
            t[i].vertices[k].attributeSet = NULL;
        }
        t[i].triangleAttributeSet = NULL;
    }
}

void SuiteQD3D(void)
{
    static const int counts[] = { 1000, 10000 };
    Handle temp;
    void *raw = NULL;
    uint32_t *pixels;
    Q3TriangleData *tris = NULL, one;
    int pref, c;
    if (!LoadQD3D()) {
        RowSkip(TestBegin(kSuite, "QuickDraw 3D scene", unitPerSec), "QuickDraw 3D is not installed");
        return;
    }
    raw = BenchAlloc(W * H * 4 + 16, &temp);
    tris = (Q3TriangleData *)NewPtr(sizeof(Q3TriangleData) * 10000);
    if (!raw || !tris) {
        RowSkip(TestBegin(kSuite, "QuickDraw 3D scene", unitPerSec), "not enough memory");
        goto done;
    }
    pixels = (uint32_t *)(((uint32_t)raw + 15) & ~15UL);
    for (pref = 0; pref < 2 && !gCancelled; pref++) {
        Q3PixmapDrawContextData dc;
        Q3ViewAngleAspectCameraData cam;
        Q3LightData light = { 1, 1.0f, { 1.0f, 1.0f, 1.0f } };
        TQ3Object view = q3.viewNew(), renderer = q3.rendererNewFromType(kQ3RendererTypeInteractive);
        TQ3Object context, camera, ambient, group;
        const char *which = pref ? "SheepForce Metal asked for" : "default engine";
        char name[96], why[96];
        Boolean ok;
        SceneCtx s;
        if (!view || !renderer) {
            sprintf(name, "QuickDraw 3D scene (%s)", which);
            RowSkip(TestBegin(kSuite, name, unitPerSec), "no view or interactive renderer");
            if (view) q3.objectDispose(view);
            if (renderer) q3.objectDispose(renderer);
            continue;
        }
        if (pref)
            q3.setPreferences(renderer, 0x5348464FL, 1);     /* SheepForce Metal: vendor 'SHFO', engine 1 */
        q3.viewSetRenderer(view, renderer);
        memset(&dc, 0, sizeof dc);
        dc.drawContextData.clearImageMethod = kQ3ClearMethodWithColor;
        dc.drawContextData.clearImageColor.a = 1.0f;
        dc.pixmap.image = pixels;
        dc.pixmap.width = W;
        dc.pixmap.height = H;
        dc.pixmap.rowBytes = W * 4;
        dc.pixmap.pixelSize = 32;
        dc.pixmap.pixelType = kQ3PixelTypeARGB32;
        dc.pixmap.bitOrder = dc.pixmap.byteOrder = kQ3EndianBig;
        context = q3.pixmapDrawContextNew(&dc);
        memset(&cam, 0, sizeof cam);
        cam.cameraLocation.z = 3.0f;
        cam.upVector.y = 1.0f;
        cam.hither = 0.5f;
        cam.yon = 10.0f;
        cam.origin.x = -1.0f;
        cam.origin.y = 1.0f;
        cam.width = cam.height = 2.0f;
        cam.fov = 0.8f;
        cam.aspectRatioXToY = (float)W / H;
        camera = q3.cameraNew(&cam);
        ambient = q3.ambientLightNew(&light);
        group = q3.lightGroupNew();
        if (!context || !camera || !ambient || !group) {
            sprintf(name, "QuickDraw 3D scene (%s)", which);
            RowSkip(TestBegin(kSuite, name, unitPerSec), "QuickDraw 3D refused the pixmap, camera or light");
            goto next;
        }
        q3.groupAddObject(group, ambient);
        q3.viewSetDrawContext(view, context);
        q3.viewSetCamera(view, camera);
        q3.viewSetLightGroup(view, group);
        /* check: a triangle over the middle */
        memset(&one, 0, sizeof one);
        one.vertices[0].point.x = -0.5f; one.vertices[0].point.y = -0.5f;
        one.vertices[1].point.x = 0.5f; one.vertices[1].point.y = -0.5f;
        one.vertices[2].point.y = 0.5f;
        s.view = view;
        s.tris = &one;
        s.count = 1;
        memset(pixels, 0x55, W * H * 4);
        Draw(&s);
        ok = (pixels[(H / 2 + 20) * W + W / 2] & 0xFFFFFF) != 0 && (pixels[2 * W + 2] & 0xFFFFFF) == 0;
        sprintf(why, "middle %08lx, corner %08lx", (unsigned long)pixels[(H / 2 + 20) * W + W / 2],
                (unsigned long)pixels[2 * W + 2]);
        for (c = 0; c < 2 && !gCancelled; c++) {
            BenchRow *row;
            Measurement m;
            sprintf(name, "QuickDraw 3D scene (%s), %d triangles", which, counts[c]);
            row = TestBegin(kSuite, name, unitPerSec);
            if (!row)
                continue;
            if (!ok) {
                RowFail(row, why);
                continue;
            }
            Grid(tris, counts[c]);
            s.tris = tris;
            s.count = counts[c];
            Measure(Body, &s, 1, &m);
            if (m.cancelled)
                gCancelled = true;
            TestRecord(row, &m, unitPerSec, 1);
        }
next:
        if (group) q3.objectDispose(group);
        if (ambient) q3.objectDispose(ambient);
        if (camera) q3.objectDispose(camera);
        if (context) q3.objectDispose(context);
        q3.objectDispose(renderer);
        q3.objectDispose(view);
    }
done:
    if (tris) DisposePtr((Ptr)tris);
    BenchFree(raw, temp);
}
