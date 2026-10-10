/*
 *  mic_permission_macosx.mm - macOS microphone permission for the guest's sound input
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 *
 *  The first time a guest opens the sound input, macOS asks the person at the Mac to allow the microphone for
 *  SheepShaver. Until they answer, and if they say no, the capture delivers silence; nothing here waits for the
 *  answer, because the emulation thread must not block.
 */
#import <AVFoundation/AVFoundation.h>
#include "nw_sound_input.h"

int nw_mic_authorization(void)
{
	switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]) {
		case AVAuthorizationStatusAuthorized:
			return 3;
		case AVAuthorizationStatusDenied:
			return 2;
		case AVAuthorizationStatusRestricted:
			return 1;
		default:
			return 0;
	}
}

void nw_mic_request_access(void)
{
	[AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
		(void)granted;
	}];
}
