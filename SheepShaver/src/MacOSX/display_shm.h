/*
 *  display_shm.h - The shared-memory region through which a VM process hands its picture to the library window
 *  (another process). One region per VM, POSIX shared memory "/sheep.<uid>.<vm id>":
 *
 *      [ header: 16 KB ][ slot 0 ][ slot 1 ][ slot 2 ]
 *
 *  A slot is a guest-sized BGRA8 image (bytesPerRow apart, aligned for Metal). The VM renders straight into a slot
 *  with the GPU (the slots are wrapped as no-copy Metal buffers and buffer-backed textures), so the picture is never
 *  copied by the CPU. The viewer wraps the same pages the same way and draws them.
 *
 *  Hand-over, all fields accessed with __atomic builtins:
 *    - The VM renders into a slot that is neither `latestSlot` nor `readingSlot` nor still being rendered. When the GPU
 *      has finished it stores latestSlot, publishNs, then increments frameCounter (release).
 *    - The viewer reads frameCounter (acquire); if it changed it takes latestSlot, stores it in readingSlot, re-reads
 *      latestSlot (and repeats if it moved), draws from that slot, then clears readingSlot to DISPLAY_SHM_NO_SLOT.
 *    - The viewer stores viewerHeartbeatNs (CLOCK_UPTIME_RAW) and viewerIntervalNs (its refresh interval) on every
 *      display tick while it is showing this VM. The VM renders only while the heartbeat is fresh, and not more often
 *      than the interval. No viewer, no GPU work.
 *    - A change of size bumps `generation` after width/height/bytesPerRow are written.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */
#ifndef DISPLAY_SHM_H
#define DISPLAY_SHM_H

#include <stdint.h>

#define DISPLAY_SHM_MAGIC         0x5348454550444953ull   /* "SHEEPDIS" */
#define DISPLAY_SHM_VERSION       1u
#define DISPLAY_SHM_SLOTS         3u
#define DISPLAY_SHM_HEADER_BYTES  16384u
#define DISPLAY_SHM_NO_SLOT       0xffffffffu
/* The VM treats a heartbeat older than this as "nobody is looking". */
#define DISPLAY_SHM_HEARTBEAT_NS  250000000ull

typedef struct DisplayShmHeader {
	uint64_t magic;               /*   0 */
	uint32_t version;             /*   8 */
	uint32_t slots;               /*  12 */
	uint64_t slotBytes;           /*  16  size of one slot (a multiple of the page size) */
	uint32_t headerBytes;         /*  24 */
	uint32_t maxWidth;            /*  28  the largest picture the slots can hold */
	uint32_t maxHeight;           /*  32 */
	uint32_t pixelFormat;         /*  36  MTLPixelFormatBGRA8Unorm (80) */
	uint32_t generation;          /*  40  atomic */
	uint32_t width;               /*  44 */
	uint32_t height;              /*  48 */
	uint32_t bytesPerRow;         /*  52 */
	uint32_t latestSlot;          /*  56  atomic */
	uint32_t readingSlot;         /*  60  atomic, written by the viewer */
	uint64_t frameCounter;        /*  64  atomic */
	uint64_t publishNs;           /*  72  atomic */
	uint64_t viewerHeartbeatNs;   /*  80  atomic, written by the viewer */
	uint64_t viewerIntervalNs;    /*  88  atomic, written by the viewer */
	uint32_t vmPid;               /*  96 */
	uint32_t reserved;            /* 100 */
} DisplayShmHeader;

#ifdef __cplusplus
static_assert(sizeof(DisplayShmHeader) == 104, "DisplayShmHeader layout");
static_assert(__builtin_offsetof(DisplayShmHeader, frameCounter) == 64, "frameCounter offset");
static_assert(__builtin_offsetof(DisplayShmHeader, viewerHeartbeatNs) == 80, "heartbeat offset");
#else
_Static_assert(sizeof(DisplayShmHeader) == 104, "DisplayShmHeader layout");
#endif

#endif
