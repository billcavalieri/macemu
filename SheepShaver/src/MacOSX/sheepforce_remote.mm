/*
 *  sheepforce_remote.mm - The library window's side of the shared-memory picture (display_shm.h): maps a VM's frame
 *  region, keeps the viewer heartbeat going, and copies the newest frame into a CAMetalLayer's drawable on the GPU.
 *
 *  There is no CPU copy and no shader: the slots are no-copy Metal buffers over the shared pages, wrapped as
 *  buffer-backed textures, and a blit puts the newest one into the layer. The layer's drawableSize is the guest size, so
 *  CoreAnimation scales it to the view with nearest filtering, exactly as the VM's own window does.
 *
 *  Called from the main thread (a display link tick); the heartbeat tells the VM it may draw.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "display_shm.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

struct RemoteDisplay {
	uint8_t *base;
	size_t total;
	DisplayShmHeader *hdr;
	id<MTLDevice> dev;
	id<MTLCommandQueue> queue;
	id<MTLBuffer> buf[DISPLAY_SHM_SLOTS];
	id<MTLTexture> tex[DISPLAY_SHM_SLOTS];
	uint32_t texGeneration;
	uint32_t texWidth, texHeight;
	uint64_t lastFrame;
	bool blitPending;			/* the GPU is still reading readingSlot */
	uint64_t drawn;
};

static uint64_t remote_now_ns(void)
{
	return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}

/* nullptr when the VM's region is not there (yet) or is not ours to read */
extern "C" void *RemoteDisplayOpen(const char *name)
{
	const int fd = shm_open(name, O_RDWR, 0600);
	if (fd < 0)
		return NULL;
	struct stat st;
	if (fstat(fd, &st) != 0 || (size_t)st.st_size < DISPLAY_SHM_HEADER_BYTES) {
		close(fd);
		return NULL;
	}
	void *base = mmap(NULL, (size_t)st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (base == MAP_FAILED)
		return NULL;
	DisplayShmHeader *hdr = (DisplayShmHeader *)base;
	if (__atomic_load_n(&hdr->magic, __ATOMIC_ACQUIRE) != DISPLAY_SHM_MAGIC || hdr->version != DISPLAY_SHM_VERSION
	    || hdr->slots != DISPLAY_SHM_SLOTS || hdr->headerBytes != DISPLAY_SHM_HEADER_BYTES
	    || DISPLAY_SHM_HEADER_BYTES + (size_t)hdr->slots * hdr->slotBytes > (size_t)st.st_size) {
		munmap(base, (size_t)st.st_size);
		return NULL;
	}
	RemoteDisplay *r = new RemoteDisplay();
	memset(r, 0, sizeof *r);
	r->base = (uint8_t *)base;
	r->total = (size_t)st.st_size;
	r->hdr = hdr;
	r->dev = MTLCreateSystemDefaultDevice();
	r->queue = [r->dev newCommandQueue];
	if (!r->dev || !r->queue) {
		munmap(base, r->total);
		delete r;
		return NULL;
	}
	for (unsigned i = 0; i < DISPLAY_SHM_SLOTS; i++) {
		r->buf[i] = [r->dev newBufferWithBytesNoCopy:r->base + DISPLAY_SHM_HEADER_BYTES + (size_t)i * hdr->slotBytes
						      length:(NSUInteger)hdr->slotBytes options:MTLResourceStorageModeShared deallocator:nil];
		if (!r->buf[i]) {
			munmap(base, r->total);
			delete r;
			return NULL;
		}
	}
	return r;
}

extern "C" void RemoteDisplayClose(void *handle)
{
	RemoteDisplay *r = (RemoteDisplay *)handle;
	if (!r)
		return;
	/* Tell the VM nobody is looking, and let a blit that is still reading a slot finish (its handler touches `r`) */
	__atomic_store_n(&r->hdr->viewerHeartbeatNs, 0, __ATOMIC_RELEASE);
	for (int i = 0; i < 200 && __atomic_load_n(&r->blitPending, __ATOMIC_ACQUIRE); i++)
		usleep(1000);
	if (__atomic_load_n(&r->blitPending, __ATOMIC_ACQUIRE))
		return;					/* the GPU is stuck: leak rather than free what it may still write */
	__atomic_store_n(&r->hdr->readingSlot, DISPLAY_SHM_NO_SLOT, __ATOMIC_RELEASE);
	for (unsigned i = 0; i < DISPLAY_SHM_SLOTS; i++) {
		r->tex[i] = nil;
		r->buf[i] = nil;
	}
	munmap(r->base, r->total);
	r->queue = nil;
	r->dev = nil;
	delete r;
}

/* The layer the frames are drawn into: guest-sized BGRA drawables on our device, scaled by CoreAnimation, nearest. */
extern "C" void RemoteDisplayConfigureLayer(void *handle, void *layer_ptr)
{
	RemoteDisplay *r = (RemoteDisplay *)handle;
	CAMetalLayer *layer = (__bridge CAMetalLayer *)layer_ptr;
	if (!r || !layer)
		return;
	layer.device = r->dev;
	layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
	layer.framebufferOnly = NO;			/* the drawable is the destination of a blit */
	layer.opaque = YES;
	layer.allowsNextDrawableTimeout = YES;
	layer.contentsGravity = kCAGravityResize;
	layer.magnificationFilter = kCAFilterNearest;
	layer.minificationFilter = kCAFilterNearest;
}

/* Guest picture size as the VM last published it (0 x 0 before the first frame). */
extern "C" void RemoteDisplayGeometry(void *handle, int *width, int *height)
{
	RemoteDisplay *r = (RemoteDisplay *)handle;
	if (!r) {
		*width = *height = 0;
		return;
	}
	*width = (int)__atomic_load_n(&r->hdr->width, __ATOMIC_ACQUIRE);
	*height = (int)__atomic_load_n(&r->hdr->height, __ATOMIC_ACQUIRE);
}

extern "C" uint64_t RemoteDisplayFramesDrawn(void *handle)
{
	RemoteDisplay *r = (RemoteDisplay *)handle;
	return r ? r->drawn : 0;
}

/* Age in nanoseconds of the newest published frame (diagnostics) */
extern "C" uint64_t RemoteDisplayNewestAgeNs(void *handle)
{
	RemoteDisplay *r = (RemoteDisplay *)handle;
	if (!r)
		return 0;
	const uint64_t t = __atomic_load_n(&r->hdr->publishNs, __ATOMIC_ACQUIRE);
	return t ? remote_now_ns() - t : 0;
}

static bool remote_textures(RemoteDisplay *r)
{
	const uint32_t gen = __atomic_load_n(&r->hdr->generation, __ATOMIC_ACQUIRE);
	if (gen == 0)
		return false;				/* the VM has not drawn a frame yet */
	if (r->tex[0] && r->texGeneration == gen)
		return true;
	const uint32_t w = r->hdr->width, h = r->hdr->height, bpr = r->hdr->bytesPerRow;
	if (w < 1 || h < 1 || (uint64_t)bpr * h > r->hdr->slotBytes)
		return false;
	MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
							width:w height:h mipmapped:NO];
	d.usage = MTLTextureUsageShaderRead;
	d.storageMode = MTLStorageModeShared;
	for (unsigned i = 0; i < DISPLAY_SHM_SLOTS; i++) {
		r->tex[i] = [r->buf[i] newTextureWithDescriptor:d offset:0 bytesPerRow:bpr];
		if (!r->tex[i])
			return false;
	}
	r->texGeneration = gen;
	r->texWidth = w;
	r->texHeight = h;
	return true;
}

/* One display tick. Tells the VM somebody is looking (heartbeat and refresh interval), and if a new frame was published
 * copies it into the layer. Returns 1 when a frame was drawn. */
extern "C" int RemoteDisplayDraw(void *handle, void *layer_ptr, uint64_t refresh_interval_ns)
{
	RemoteDisplay *r = (RemoteDisplay *)handle;
	CAMetalLayer *layer = (__bridge CAMetalLayer *)layer_ptr;
	if (!r || !layer)
		return 0;
	DisplayShmHeader *hdr = r->hdr;
	__atomic_store_n(&hdr->viewerIntervalNs, refresh_interval_ns, __ATOMIC_RELAXED);
	__atomic_store_n(&hdr->viewerHeartbeatNs, remote_now_ns(), __ATOMIC_RELEASE);
	const uint64_t counter = __atomic_load_n(&hdr->frameCounter, __ATOMIC_ACQUIRE);
	if (counter == r->lastFrame || __atomic_load_n(&r->blitPending, __ATOMIC_ACQUIRE))
		return 0;
	if (!remote_textures(r))
		return 0;
	/* Hold the newest slot against reuse while the GPU reads it (re-check: it may have moved while we claimed it) */
	uint32_t slot = DISPLAY_SHM_NO_SLOT;
	for (int attempt = 0; attempt < 8; attempt++) {
		const uint32_t latest = __atomic_load_n(&hdr->latestSlot, __ATOMIC_ACQUIRE);
		if (latest >= DISPLAY_SHM_SLOTS)
			return 0;
		__atomic_store_n(&hdr->readingSlot, latest, __ATOMIC_SEQ_CST);
		if (__atomic_load_n(&hdr->latestSlot, __ATOMIC_SEQ_CST) == latest) {
			slot = latest;
			break;
		}
	}
	if (slot >= DISPLAY_SHM_SLOTS) {
		__atomic_store_n(&hdr->readingSlot, DISPLAY_SHM_NO_SLOT, __ATOMIC_RELEASE);
		return 0;
	}
	const CGSize want = CGSizeMake(r->texWidth, r->texHeight);
	if (layer.drawableSize.width != want.width || layer.drawableSize.height != want.height)
		layer.drawableSize = want;
	id<CAMetalDrawable> drawable = [layer nextDrawable];
	if (!drawable) {
		__atomic_store_n(&hdr->readingSlot, DISPLAY_SHM_NO_SLOT, __ATOMIC_RELEASE);
		return 0;
	}
	id<MTLCommandBuffer> cb = [r->queue commandBuffer];
	id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
	[blit copyFromTexture:r->tex[slot] sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
		   sourceSize:MTLSizeMake(r->texWidth, r->texHeight, 1)
		    toTexture:drawable.texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
	[blit endEncoding];
	[cb presentDrawable:drawable];
	__atomic_store_n(&r->blitPending, true, __ATOMIC_RELEASE);
	r->lastFrame = counter;
	r->drawn++;
	[cb addCompletedHandler:^(id<MTLCommandBuffer>) {
		__atomic_store_n(&hdr->readingSlot, DISPLAY_SHM_NO_SLOT, __ATOMIC_RELEASE);
		__atomic_store_n(&r->blitPending, false, __ATOMIC_RELEASE);
	}];
	[cb commit];
	return 1;
}
