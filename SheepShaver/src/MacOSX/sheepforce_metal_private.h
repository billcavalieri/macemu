/*
 *  sheepforce_metal_private.h - Metal objects shared by the SheepForce .mm files
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 *  Licensed under the GNU General Public License, version 2 or later.
 */
#import <Metal/Metal.h>

id<MTLDevice> SheepForceMetalDevice(void);
id<MTLCommandQueue> SheepForceMetalQueue(void);
id<MTLLibrary> SheepForceMetalLibrary(void);
