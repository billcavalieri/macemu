/*
 *  nw_sound_input.cpp - Sound input (the Sound Manager's SPB calls) from the host microphone
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 *
 *  A New World Mac OS 9 finds its sound input through a native driver for AWACS hardware this machine does not
 *  have, so the old way (a replacement .AppleSoundInput driver, SoundIn* in audio.cpp) never reaches the guest. What
 *  every caller does reach is the Sound Manager's SPB* routines. Those are 68k code behind the _SoundDispatch
 *  trap (0xA800), D0 = (selector << 16) | 0x14, and a PowerPC caller gets there through InterfaceLib's Mixed Mode
 *  glue. So the trap is head-patched: SPB selectors are answered here, everything else goes on to the Sound Manager.
 *
 *  The samples come from a ring that a host source fills (the microphone through CoreAudio, or a test tone when the
 *  environment has NW_MIC_TONE=<hz>). The ring is 44100 Hz stereo 16-bit; what the guest asked for (rate, 8 or
 *  16 bit, mono or stereo) is made when the samples are taken out of it. Asynchronous recordings are fed by a guest
 *  Time Manager task, like the SheepBlaster output (see SHEEPBLASTER-SOUND.md): completion and interrupt routines
 *  run in real guest context, and the host never enters 68k code from its own timer.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "sysdeps.h"
#include "nw_log.h"
#include "cpu_emulation.h"
#include "prefs.h"
#include "thunks.h"
#include "emul_op.h"
#include "main.h"
#include "macos_util.h"
#include "xlowmem.h"
#include "timer.h"
#include "nw_sound_input.h"

#include <atomic>
#include <set>
#include <vector>
#include <ctype.h>
#include <math.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* ---- the sample ring (44100 Hz, stereo, 16-bit; one producer thread, the emulation thread consumes) ---- */

static const uint32 RING_FRAMES = 1u << 16;
static int16 ring[RING_FRAMES * 2];
static std::atomic<uint32> ring_wr(0), ring_rd(0);
static std::atomic<uint64> pushed_frames(0), taken_frames(0), dropped_frames(0);

void nw_mic_push(const int16 *lr, uint32 frames)
{
	uint32 wr = ring_wr.load(std::memory_order_relaxed);
	uint32 rd = ring_rd.load(std::memory_order_acquire);
	for (uint32 i = 0; i < frames; i++) {
		if (wr - rd >= RING_FRAMES) {
			dropped_frames.fetch_add(frames - i, std::memory_order_relaxed);
			break;
		}
		ring[(wr & (RING_FRAMES - 1)) * 2] = lr[i * 2];
		ring[(wr & (RING_FRAMES - 1)) * 2 + 1] = lr[i * 2 + 1];
		wr++;
	}
	ring_wr.store(wr, std::memory_order_release);
	pushed_frames.fetch_add(frames, std::memory_order_relaxed);
}

static uint32 ring_avail(void)
{
	return ring_wr.load(std::memory_order_acquire) - ring_rd.load(std::memory_order_relaxed);
}

static void ring_pop(int16 *lr)
{
	uint32 rd = ring_rd.load(std::memory_order_relaxed);
	lr[0] = ring[(rd & (RING_FRAMES - 1)) * 2];
	lr[1] = ring[(rd & (RING_FRAMES - 1)) * 2 + 1];
	ring_rd.store(rd + 1, std::memory_order_release);
	taken_frames.fetch_add(1, std::memory_order_relaxed);
}

/* Recording starts with what the microphone hears now, not what it heard before the guest asked. */
static void ring_flush(void)
{
	ring_rd.store(ring_wr.load(std::memory_order_acquire), std::memory_order_release);
}

void nw_mic_take_stats(uint64 *pushed, uint64 *taken, uint64 *dropped)
{
	*pushed = pushed_frames.exchange(0);
	*taken = taken_frames.exchange(0);
	*dropped = dropped_frames.exchange(0);
}

/* Hosts without a capture backend have no microphone. */
__attribute__((weak)) bool nw_mic_source_start(void) { return false; }
__attribute__((weak)) void nw_mic_source_stop(void) {}

/* ---- the test tone: a host thread that fills the ring in real time ---- */

static pthread_t tone_thread;
static std::atomic<bool> tone_run(false);
static bool tone_joinable;

static void *tone_main(void *)
{
	const char *e = getenv("NW_MIC_TONE");
	double hz = e ? atof(e) : 440.0;
	if (hz < 20.0)
		hz = 440.0;
	double phase = 0.0;
	const double step = 2.0 * M_PI * hz / 44100.0;
	while (tone_run.load()) {
		int16 buf[441 * 2];		// 10 ms
		for (int i = 0; i < 441; i++) {
			int16 v = (int16)(sin(phase) * 16000.0);
			phase += step;
			if (phase > 2.0 * M_PI)
				phase -= 2.0 * M_PI;
			buf[i * 2] = buf[i * 2 + 1] = v;
		}
		nw_mic_push(buf, 441);
		usleep(10000);
	}
	return NULL;
}

static bool tone_wanted(void)
{
	static const bool on = getenv("NW_MIC_TONE") != NULL;
	return on;
}

/* The microphone starts when the guest opens the device and stops when it closes it, so the host shows no
 * microphone indicator for a VM that does not record. */
static int source_users;

static void source_start(void)
{
	if (source_users++ > 0)
		return;
	if (tone_wanted()) {
		tone_run = true;
		tone_joinable = pthread_create(&tone_thread, NULL, tone_main, NULL) == 0;
		NW_DIAG("NW-MIC: test tone source started\n");
	} else {
		bool ok = nw_mic_source_start();
		NW_DIAG("NW-MIC: microphone source %s\n", ok ? "started" : "unavailable (no data will arrive)");
	}
}

static void source_stop(void)
{
	if (source_users == 0 || --source_users > 0)
		return;
	if (tone_wanted()) {
		tone_run = false;
		if (tone_joinable)
			pthread_join(tone_thread, NULL);
		tone_joinable = false;
	} else {
		nw_mic_source_stop();
	}
}

/* ---- device state ---- */

enum {	// OSType info selectors of SPBGetDeviceInfo / SPBSetDeviceInfo
	siSampleRate = 'srat', siSampleSize = 'ssiz', siNumberChannels = 'chan', siCompressionType = 'comp',
	siChannelAvailable = 'chav', siSampleRateAvailable = 'srav', siSampleSizeAvailable = 'ssav',
	siCompressionAvailable = 'cmav', siContinuous = 'cont', siAsync = 'asyn', siInputSource = 'sour', siInputAvailable = 'inav', siInputGainMin = 'igmn', siInputGainMax = 'igmx', siOSTypeInputSource = 'inpt', siDeviceConnected = 'dcon',
	siInputSourceNames = 'snam', siInputGain = 'gain', siPlayThruOnOff = 'plth', siLevelMeterOnOff = 'lmet',
	siRecordingQuality = 'qual', siTwosComplementOnOff = 'twos', siDeviceBufferInfo = 'dbin',
	siOptionsDialog = 'optd', siDeviceIcon = 'icon', siHardwareBusy = 'hwbs', siActiveChannels = 'chac',
	siStereoInputGain = 'sgai', siCompressionFactor = 'cfct', siUserInterruptProc = 'user', siDeviceName = 'name'
};

enum {
	siNoSoundInHardware = -220, siBadSoundInDevice = -221, siNoBufferSpecified = -222, siInvalidSampleRate = -225,
	siInvalidSampleSize = -226, siDeviceBusyErr = -227, siBadDeviceName = -228, siBadRefNum = -229,
	siUnknownInfoType = -231, siUnknownQuality = -232
};

/* What the Sound control panel lists: the source in the Name column, the device beside it. */
#define DEVICE_NAME "SheepBlaster"
/* What SPBOpenDevice hands out. On a real Mac it is the input driver's Device Manager reference number, a negative
 * 16-bit number in a long, and code inside the Sound Manager (its record window, for one) also passes it straight to
 * the Device Manager. So it has to be a reference number that is harmless there: these are units far past the end of
 * the unit table, which the Device Manager refuses with badUnitErr. A positive value is taken for a FILE reference
 * number: 2, the low word of the first scheme used here, is the System file, and the record window closed it. */
static const uint32 REF_FIRST_UNIT = 0x4000;
static uint32 make_ref(uint32 n)
{
	return (uint32)(int32)(int16)~(REF_FIRST_UNIT + (n & 0x0fff));
}
static const uint32 FIXED_22254 = 0x56ee8ba3;	// the classic Mac sample rate, 22254.54545 Hz

struct Config {
	uint32 rate = FIXED_22254;
	int bits = 8;
	int chans = 1;
	bool twos = false;			// 8-bit samples: offset binary ('raw ') unless this is set
	int16 source = 1;			// 1-based index into the source names
	uint32 gain = 0x00010000;	// Fixed
	int16 playthru = 0;
	bool meter = false;
	std::set<uint32> refs;		// every ref that is open
	uint32 next_ref = 1;
	uint32 write_ref = 0;		// the ref with write permission, if any
};
static Config cfg;

static double rate_hz(void)
{
	return (double)cfg.rate / 65536.0;
}

static uint32 frame_bytes(void)
{
	return (uint32)(cfg.chans * cfg.bits / 8);
}

/* ---- recording ---- */

struct SPBRec {		// the SPB structure in guest memory
	enum { inRefNum = 0, count = 4, milliseconds = 8, bufferLength = 12, bufferPtr = 16, completionRoutine = 20,
		interruptRoutine = 24, userLong = 28, error = 32 };
};

struct Job {
	bool active = false, paused = false;
	uint32 pb = 0, ref = 0;
	uint32 total = 0;		// bytes to record; 0 = until stopped
	uint32 done = 0;		// bytes recorded in all
	uint32 fill = 0;		// bytes in the buffer now
	uint32 buf = 0, buflen = 0;
	uint32 completion = 0, interrupt = 0;
	int peak = 0;		// 0..255, since the last interrupt routine call
	bool in_callbacks = false;
	bool stop_req = false;
	bool to_file = false;		// SPBRecordToFile: the samples go to an open file
	uint32 file_ref = 0;
	bool done_pending = false;	// the recording is complete; the file write and the completion routine wait for a normal-context call
};
static Job job;
static std::vector<uint8> file_q;	// samples recorded to a file, not yet written
static int meter_peak;		// 0..255, since the level was last read

/* resampler state: input frames at 44100 Hz, linear interpolation to the requested rate */
static double rs_phase;
static int16 rs_a[2], rs_b[2];
static bool rs_primed;

static void rs_reset(void)
{
	rs_phase = 0.0;
	rs_primed = false;
}

/* One output frame; false when the ring does not have enough input yet (nothing is consumed then). */
static bool rs_next(double step, int16 *out)
{
	if (!rs_primed) {
		if (ring_avail() < 2)
			return false;
		ring_pop(rs_a);
		ring_pop(rs_b);
		rs_phase = 0.0;
		rs_primed = true;
	}
	uint32 need = (uint32)rs_phase;
	if (ring_avail() < need)
		return false;
	for (uint32 i = 0; i < need; i++) {
		rs_a[0] = rs_b[0];
		rs_a[1] = rs_b[1];
		ring_pop(rs_b);
	}
	rs_phase -= need;
	for (int c = 0; c < 2; c++)
		out[c] = (int16)(rs_a[c] + (rs_b[c] - rs_a[c]) * rs_phase);
	rs_phase += step;
	return true;
}

/* Produce up to `bytes` bytes of the requested format at guest address dst; returns how many were made. */
static uint32 produce(uint32 dst, uint32 bytes)
{
	uint32 fb = frame_bytes();
	double step = 44100.0 / rate_hz();
	double gain = (double)cfg.gain / 65536.0;
	uint32 made = 0;
	while (made + fb <= bytes) {
		int16 f[2];
		if (!rs_next(step, f))
			break;
		int s[2];
		for (int c = 0; c < 2; c++) {
			int v = (int)(f[c] * gain);
			s[c] = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
		}
		int mono = (s[0] + s[1]) / 2;
		for (int c = 0; c < cfg.chans; c++) {
			int v = cfg.chans == 1 ? mono : s[c];
			int a = (v < 0 ? -v : v) >> 7;
			if (a > 255)
				a = 255;
			if (a > job.peak)
				job.peak = a;
			if (a > meter_peak)
				meter_peak = a;
			if (cfg.bits == 16) {
				WriteMacInt16(dst + made, (uint16)(int16)v);
				made += 2;
			} else {
				uint8 b = (uint8)(v >> 8);
				WriteMacInt8(dst + made, cfg.twos ? b : (uint8)(b + 128));
				made += 1;
			}
		}
	}
	return made;
}

/* Level of what the source has delivered since the last look, with nothing recorded: take it all out of the ring. */
static void measure_level(void)
{
	int16 f[2];
	while (ring_avail() > 0) {
		ring_pop(f);
		int v = (int)(((f[0] + f[1]) / 2) * ((double)cfg.gain / 65536.0));	// the gain slider moves the meter too
		int a = (v < 0 ? -v : v) >> 7;
		if (a > meter_peak)
			meter_peak = a > 255 ? 255 : a;
	}
}

static uint32 ms_to_bytes(uint32 ms)
{
	return (uint32)((double)ms * rate_hz() / 1000.0) * frame_bytes();
}

static uint32 bytes_to_ms(uint32 bytes)
{
	return (uint32)((double)(bytes / frame_bytes()) * 1000.0 / rate_hz());
}

/* ---- calling guest routines (only from the Time Manager task, in guest context) ---- */

static uint32 th_completion, th_interrupt;

static void make_thunks(void)
{
	if (th_completion)
		return;
	static const uint8 comp[] = {	// A0 = SPB, A1 = routine: pascal void proc(SPBPtr)
		0x2f, 0x08,		// move.l a0,-(sp)
		0x4e, 0x91,		// jsr (a1)
		0x4e, 0x75		// rts
	};
	static const uint8 intr[] = {	// A0 = SPB, A1 = routine, A2 = buffer, D1.w = peak, D2 = sample size
		0x2f, 0x08,		// move.l a0,-(sp)
		0x2f, 0x0a,		// move.l a2,-(sp)
		0x3f, 0x01,		// move.w d1,-(sp)
		0x2f, 0x02,		// move.l d2,-(sp)
		0x4e, 0x91,		// jsr (a1)
		0x4e, 0x75		// rts
	};
	th_completion = SheepProc(comp, sizeof comp);
	th_interrupt = SheepProc(intr, sizeof intr);
}

static void call_completion(uint32 pb, uint32 proc)
{
	if (proc == 0)
		return;
	M68kRegisters r;
	memset(&r, 0, sizeof r);
	r.a[0] = pb;
	r.a[1] = proc;
	Execute68k(th_completion, &r);
}

static void call_interrupt(uint32 pb, uint32 proc, uint32 buf, int peak, uint32 sample_size)
{
	if (proc == 0)
		return;
	M68kRegisters r;
	memset(&r, 0, sizeof r);
	r.a[0] = pb;
	r.a[1] = proc;
	r.a[2] = buf;
	r.d[1] = (uint32)peak;
	r.d[2] = sample_size;
	Execute68k(th_interrupt, &r);
}

/* Write what has been recorded for a file to it (File Manager Write at the mark). Never from the Time Manager task:
 * the File Manager is not safe at interrupt time. */
static uint32 sys_alloc(uint32 size);
static uint32 pbwrite_mem;
static uint32 file_prod_buf;		// where the tick makes samples for a file recording; job.buf is only the write buffer
static int16 file_flush(void)
{
	if (!job.to_file || file_q.empty())
		return noErr;
	if (!pbwrite_mem)
		pbwrite_mem = sys_alloc(64);
	/* The tick can run while the File Manager is writing and add to the queue, so take what is there now. */
	std::vector<uint8> data;
	data.swap(file_q);
	int16 err = noErr;
	size_t off = 0;
	while (off < data.size() && err == noErr) {
		uint32 n = (uint32)(data.size() - off);
		if (n > job.buflen)
			n = job.buflen;
		for (uint32 i = 0; i < n; i++)
			WriteMacInt8(job.buf + i, data[off + i]);
		for (uint32 i = 0; i < 64; i += 2)
			WriteMacInt16(pbwrite_mem + i, 0);
		WriteMacInt16(pbwrite_mem + 24, (uint16)job.file_ref);	// ioRefNum
		WriteMacInt32(pbwrite_mem + 32, job.buf);				// ioBuffer
		WriteMacInt32(pbwrite_mem + 36, n);					// ioReqCount
		WriteMacInt16(pbwrite_mem + 44, 0);					// ioPosMode: fsAtMark (0; 1 is fsFromStart)
		M68kRegisters r;
		memset(&r, 0, sizeof r);
		r.a[0] = pbwrite_mem;
		Execute68kTrap(0xa003, &r);							// _Write
		err = (int16)r.d[0];
		static const bool trace = getenv("NW_MIC_TRACE") != NULL;
		if (trace)
			NW_DIAG("NW-MIC: file write %u bytes (queued %u) -> error %d, written %u\n", (unsigned)n, (unsigned)data.size(), (int)err,
			       (unsigned)ReadMacInt32(pbwrite_mem + 40));
		off += n;
	}
	if (err != noErr)
		NW_DIAG("NW-MIC: writing the recording to its file failed (%d)\n", (int)err);
	return err;
}

/* Finish the active recording: update the SPB, then run the completion routine. */
static void finish_job(int16 err)
{
	if (!job.active)
		return;
	if (job.to_file) {
		int16 werr = file_flush();
		if (err == noErr)
			err = werr;
	}
	Job j = job;
	job = Job();
	file_q.clear();
	NW_DIAG("NW-MIC: recording ends: error %d, %u bytes%s\n", (int)err, (unsigned)j.done, j.to_file ? ", to a file" : "");
	WriteMacInt32(j.pb + SPBRec::count, j.done);
	WriteMacInt32(j.pb + SPBRec::milliseconds, bytes_to_ms(j.done));
	WriteMacInt16(j.pb + SPBRec::error, (uint16)err);
	call_completion(j.pb, j.completion);
}

/* Memory that lives as long as the guest: the system heap. (SheepMem::Reserve is a stack that other code releases in
 * order, so a block taken from it inside someone else's scope would be handed out again.) */
static uint32 sys_alloc(uint32 size)
{
	M68kRegisters r;
	memset(&r, 0, sizeof r);
	r.d[0] = size;
	Execute68kTrap(0xa71e, &r);		// NewPtrSysClear()
	return r.a[0];
}

/* ---- the Time Manager task that feeds asynchronous recordings ---- */

static uint32 tm_task, tm_proc;
static bool tm_installed, tm_armed;
enum { tmAddr = 6, SIZEOF_TMTask = 22, TICK_MS = 10 };

static void tm_prime(void)
{
	if (!tm_installed || tm_armed)
		return;
	M68kRegisters r;
	memset(&r, 0, sizeof r);
	r.a[0] = tm_task;
	r.d[0] = TICK_MS;
	Execute68kTrap(0xa05a, &r);		// PrimeTime()
	tm_armed = true;
}

static void tm_install(void)
{
	if (tm_installed)
		return;
	if (tm_task == 0) {
		static const uint8 proc[] = {
			(uint8)(M68K_EMUL_OP_SPB_TICK >> 8), (uint8)(M68K_EMUL_OP_SPB_TICK & 0xff),	// D0 = ms, or 0 to stop
			0x4a, 0x80,		// tst.l d0
			0x67, 0x02,		// beq.s @1
			0xa0, 0x5a,		// PrimeTime
			0x4e, 0x75		// @1 rts
		};
		tm_proc = SheepProc(proc, sizeof proc);
		tm_task = sys_alloc(SIZEOF_TMTask);
	}
	for (uint32 i = 0; i < SIZEOF_TMTask; i += 2)
		WriteMacInt16(tm_task + i, 0);
	WriteMacInt32(tm_task + tmAddr, tm_proc);
	M68kRegisters r;
	memset(&r, 0, sizeof r);
	r.a[0] = tm_task;
	Execute68kTrap(0xa458, &r);		// InsXTime()
	tm_installed = true;
	tm_armed = false;
}

/* Move what the ring has into the job's buffer; run interrupt and completion routines. */
static void pump(void)
{
	while (job.active && !job.paused && !job.stop_req && !job.done_pending) {
		if (job.to_file) {		// into the host queue; written to the file later, in a normal call
			uint32 room = 4096;
			if (job.total && job.total - job.done < room)
				room = job.total - job.done;
			uint32 made = room ? produce(file_prod_buf, room) : 0;
			if (made == 0)
				break;
			for (uint32 i = 0; i < made; i++)
				file_q.push_back(ReadMacInt8(file_prod_buf + i));
			job.done += made;
			if (job.total && job.done >= job.total)
				job.done_pending = true;
			continue;
		}
		uint32 room = job.buflen - job.fill;
		if (job.total)
			room = job.total - job.done < room ? job.total - job.done : room;
		if (room == 0)
			break;
		uint32 made = produce(job.buf + job.fill, room);
		if (made == 0)
			break;
		job.fill += made;
		job.done += made;
		bool finished = job.total && job.done >= job.total;
		if (job.fill >= job.buflen || finished) {
			if (job.interrupt) {
				job.in_callbacks = true;
				Job j = job;
				int peak = job.peak;
				job.peak = 0;
				call_interrupt(j.pb, j.interrupt, j.buf, peak, (uint32)cfg.bits);
				job.in_callbacks = false;
				if (!job.active)
					return;
			}
			job.fill = 0;
		}
		if (finished) {
			finish_job(noErr);
			return;
		}
	}
	if (job.active && job.stop_req && !job.to_file)
		finish_job(noErr);
}

/* Called at the start of every SPB call, which is a normal (not interrupt) context: finish what the tick left for it. */
static void file_service(void)
{
	if (!job.active || !job.to_file || job.in_callbacks)
		return;
	if (job.done_pending)
		finish_job(noErr);
	else
		file_flush();
}

int32 nw_spb_tick(uint32 *task)
{
	*task = tm_task;
	tm_armed = false;
	if (!tm_installed)
		return 0;
	if (job.in_callbacks)
		return TICK_MS;
	pump();
	if (job.active && job.done_pending)
		return 0;		// complete; the next SPB call writes the file and runs the completion routine
	if (!job.active) {
		NW_DIAG("NW-MIC: recording finished (ring: pushed/taken/dropped %llu/%llu/%llu)\n",
		       (unsigned long long)pushed_frames.load(), (unsigned long long)taken_frames.load(),
		       (unsigned long long)dropped_frames.load());
		return 0;
	}
	tm_armed = true;
	return TICK_MS;
}

/* ---- the SPB routines ---- */

static void put_pstring(uint32 dst, const char *s)
{
	size_t n = strlen(s);
	WriteMacInt8(dst, (uint8)n);
	for (size_t i = 0; i < n; i++)
		WriteMacInt8(dst + 1 + i, (uint8)s[i]);
}

static bool name_ok(uint32 name_ptr)
{
	if (name_ptr == 0)
		return true;
	uint8 len = ReadMacInt8(name_ptr);
	if (len == 0)
		return true;
	/* "Built-in" was the name until the device was renamed; a program that saved it still finds the device. */
	static const char *names[] = { DEVICE_NAME, "Built-in" };
	for (const char *dev : names) {
		if (len != strlen(dev))
			continue;
		bool same = true;
		for (int i = 0; i < len && same; i++)
			same = tolower((char)ReadMacInt8(name_ptr + 1 + i)) == tolower(dev[i]);
		if (same)
			return true;
	}
	return false;
}

static uint32 new_handle(uint32 size)
{
	M68kRegisters r;
	memset(&r, 0, sizeof r);
	r.d[0] = size;
	Execute68kTrap(0xa322, &r);		// NewHandleClear()
	return r.d[0] == 0 ? r.a[0] : 0;
}

static int16 get_info(uint32 ref, uint32 type, uint32 data)
{
	(void)ref;
	switch (type) {
		case siSampleRate:
			WriteMacInt32(data, cfg.rate);
			return noErr;
		case siSampleSize:
			WriteMacInt16(data, (uint16)cfg.bits);
			return noErr;
		case siNumberChannels:
		case siActiveChannels:
			WriteMacInt16(data, (uint16)cfg.chans);
			return noErr;
		case siCompressionType:
			WriteMacInt32(data, FOURCC('N','O','N','E'));
			return noErr;
		case siChannelAvailable:
			WriteMacInt16(data, 2);
			return noErr;
		case siSampleRateAvailable: {
			static const uint32 rates[] = { 0x2b110000, 0x56220000, FIXED_22254, 0xac440000 };
			uint32 h = new_handle(sizeof rates);
			if (h == 0)
				return memFullErr;
			for (unsigned i = 0; i < 4; i++)
				WriteMacInt32(ReadMacInt32(h) + i * 4, rates[i]);
			WriteMacInt16(data, 4);
			WriteMacInt32(data + 2, h);
			return noErr;
		}
		case siSampleSizeAvailable: {
			uint32 h = new_handle(4);
			if (h == 0)
				return memFullErr;
			WriteMacInt16(ReadMacInt32(h), 8);
			WriteMacInt16(ReadMacInt32(h) + 2, 16);
			WriteMacInt16(data, 2);
			WriteMacInt32(data + 2, h);
			return noErr;
		}
		case siCompressionAvailable: {
			uint32 h = new_handle(4);
			if (h == 0)
				return memFullErr;
			WriteMacInt32(ReadMacInt32(h), FOURCC('N','O','N','E'));
			WriteMacInt16(data, 1);
			WriteMacInt32(data + 2, h);
			return noErr;
		}
		case siContinuous:
		case siAsync:
			WriteMacInt16(data, 1);
			return noErr;
		case siInputSource:
			WriteMacInt16(data, (uint16)cfg.source);
			return noErr;
		case siInputAvailable:
			WriteMacInt16(data, 1);		// the one source is available
			return noErr;
		case siOSTypeInputSource:
			WriteMacInt32(data, FOURCC('m','i','c',' '));
			return noErr;
		case siDeviceConnected:
			WriteMacInt16(data, 1);		// siDeviceIsConnected
			return noErr;
		case siInputSourceNames: {
			static const uint8 str[] = { 0x00, 0x01, 0x0c, 'S','h','e','e','p','B','l','a','s','t','e','r' };
			uint32 h = new_handle(sizeof str);
			if (h == 0)
				return memFullErr;
			for (unsigned i = 0; i < sizeof str; i++)
				WriteMacInt8(ReadMacInt32(h) + i, str[i]);
			WriteMacInt32(data, h);
			return noErr;
		}
		case siInputGain:
			WriteMacInt32(data, cfg.gain);
			return noErr;
		case siInputGainMin:
			WriteMacInt32(data, 0x00008000);		// 0.5, Fixed
			return noErr;
		case siInputGainMax:
			WriteMacInt32(data, 0x00018000);		// 1.5
			return noErr;
		case siPlayThruOnOff:
			WriteMacInt16(data, (uint16)cfg.playthru);
			return noErr;
		case siLevelMeterOnOff:
			/* Two integers: the meter's state, then the level (0..255). A record window draws its level from this. */
			WriteMacInt16(data, cfg.meter ? 1 : 0);
			if (cfg.meter && !job.active && cfg.write_ref)
				measure_level();
			WriteMacInt16(data + 2, cfg.meter ? (uint16)meter_peak : 0);
			meter_peak = 0;
			return noErr;
		case siTwosComplementOnOff:
			WriteMacInt16(data, cfg.twos ? 1 : 0);
			return noErr;
		case siOptionsDialog:
			WriteMacInt16(data, 0);
			return noErr;
		case siHardwareBusy:
			WriteMacInt16(data, job.active ? 1 : 0);
			return noErr;
		case siDeviceName:
			put_pstring(data, DEVICE_NAME);
			return noErr;
		default:
			NW_DIAG("NW-MIC: SPBGetDeviceInfo '%c%c%c%c' not handled\n", (char)(type >> 24), (char)(type >> 16),
			       (char)(type >> 8), (char)type);
			return siUnknownInfoType;
	}
}

static int16 set_info(uint32 ref, uint32 type, uint32 data)
{
	(void)ref;
	switch (type) {
		case siSampleRate: {
			uint32 f = ReadMacInt32(data);
			double hz = (double)f / 65536.0;
			if (hz < 4000.0 || hz > 48000.0)
				return siInvalidSampleRate;
			cfg.rate = f;
			rs_reset();
			return noErr;
		}
		case siSampleSize: {
			int bits = (int16)ReadMacInt16(data);
			if (bits != 8 && bits != 16)
				return siInvalidSampleSize;
			cfg.bits = bits;
			return noErr;
		}
		case siNumberChannels: {
			int ch = (int16)ReadMacInt16(data);
			if (ch != 1 && ch != 2)
				return paramErr;
			cfg.chans = ch;
			return noErr;
		}
		case siCompressionType:
			return ReadMacInt32(data) == FOURCC('N','O','N','E') || ReadMacInt32(data) == FOURCC('r','a','w',' ') ||
				ReadMacInt32(data) == FOURCC('t','w','o','s') ? (int16)noErr : (int16)-223;
		case siInputSource:
			cfg.source = (int16)ReadMacInt16(data);
			return noErr;
		case siInputGain:
			cfg.gain = ReadMacInt32(data);
			return noErr;
		case siPlayThruOnOff:
			cfg.playthru = (int16)ReadMacInt16(data);
			return noErr;
		case siLevelMeterOnOff:
			cfg.meter = ReadMacInt16(data) != 0;
			return noErr;
		case siTwosComplementOnOff:
			cfg.twos = ReadMacInt16(data) != 0;
			return noErr;
		case siRecordingQuality: {
			uint32 q = ReadMacInt32(data);
			if (q == FOURCC('b','e','s','t')) {
				cfg.rate = 0xac440000; cfg.bits = 16; cfg.chans = 2;
			} else if (q == FOURCC('b','e','t','r')) {
				cfg.rate = 0xac440000; cfg.bits = 16; cfg.chans = 1;
			} else if (q == FOURCC('g','o','o','d')) {
				cfg.rate = FIXED_22254; cfg.bits = 8; cfg.chans = 1;
			} else {
				return siUnknownQuality;
			}
			rs_reset();
			return noErr;
		}
		case siUserInterruptProc:
			return noErr;
		default:
			NW_DIAG("NW-MIC: SPBSetDeviceInfo '%c%c%c%c' not handled\n", (char)(type >> 24), (char)(type >> 16),
			       (char)(type >> 8), (char)type);
			return siUnknownInfoType;
	}
}

static bool valid_ref(uint32 ref)
{
	return cfg.refs.count(ref) != 0;
}

static int16 start_record(uint32 pb, bool async, bool to_file = false, uint32 file_ref = 0)
{
	if (pb == 0)
		return paramErr;
	uint32 ref = ReadMacInt32(pb + SPBRec::inRefNum);
	if (!valid_ref(ref))
		return siBadRefNum;
	if (job.active)
		return siDeviceBusyErr;
	uint32 count = ReadMacInt32(pb + SPBRec::count);
	uint32 ms = ReadMacInt32(pb + SPBRec::milliseconds);
	uint32 buflen = ReadMacInt32(pb + SPBRec::bufferLength);
	uint32 buf = ReadMacInt32(pb + SPBRec::bufferPtr);
	static const bool trace = getenv("NW_MIC_TRACE") != NULL;
	if (trace)
		NW_DIAG("NW-MIC: SPB at %08x: ref %08x count %u ms %u bufferLength %u bufferPtr %08x completion %08x interrupt %08x\n", (unsigned)pb,
	       (unsigned)ref, (unsigned)count, (unsigned)ms, (unsigned)buflen, (unsigned)buf, (unsigned)ReadMacInt32(pb + SPBRec::completionRoutine),
	       (unsigned)ReadMacInt32(pb + SPBRec::interruptRoutine));
	Job j;
	j.pb = pb;
	j.ref = ref;
	j.buf = buf;
	j.completion = ReadMacInt32(pb + SPBRec::completionRoutine);
	j.interrupt = ReadMacInt32(pb + SPBRec::interruptRoutine);
	j.total = count ? count : ms ? ms_to_bytes(ms) : 0;
	j.to_file = to_file;
	j.file_ref = file_ref;
	if (to_file && (buf == 0 || buflen == 0)) {
		/* SimpleSound passes no buffer: the Sound Manager keeps its own for a recording to a file. */
		static uint32 file_buf;
		if (!file_buf)
			file_buf = sys_alloc(32768);
		if (!file_buf)
			return memFullErr;
		buf = j.buf = file_buf;
		buflen = 32768;
	}
	if (to_file && !file_prod_buf)
		file_prod_buf = sys_alloc(4096);
	if (buf == 0)
		return siNoBufferSpecified;
	if (buflen == 0)
		buflen = j.total;
	if (buflen == 0)
		return siNoBufferSpecified;
	j.buflen = buflen;
	if (!j.to_file && !j.interrupt && j.total > buflen)
		j.total = buflen;
	if (j.total == 0 && !j.interrupt && !j.to_file)
		return siNoBufferSpecified;
	WriteMacInt16(pb + SPBRec::error, 0);
	job = j;
	job.active = true;
	rs_reset();
	ring_flush();
	NW_DIAG("NW-MIC: record%s %s total=%u bufferLength=%u rate=%.1f bits=%d chans=%d interrupt=%s\n", to_file ? " to file" : "", async ? "async" : "sync",
	       (unsigned)job.total, (unsigned)job.buflen, rate_hz(), cfg.bits, cfg.chans, job.interrupt ? "yes" : "no");
	if (async) {
		/* Like a Device Manager ioResult: greater than 0 while the recording is in progress. A caller with no
		 * completion routine (SimpleSound's record window) watches this field and stops when it is 0 or less. */
		WriteMacInt16(pb + SPBRec::error, 1);
		make_thunks();
		tm_install();
		tm_prime();
		return noErr;
	}
	/* Synchronous: the guest waits, as it would on a real Mac. The host source keeps filling the ring. */
	uint64 deadline = GetTicks_usec() + (uint64)(job.total ? bytes_to_ms(job.total) : 5000) * 1000 + 2000000;
	make_thunks();
	while (job.active && GetTicks_usec() < deadline) {
		if (job.to_file) {
			pump();
			file_flush();
			if (job.done_pending)
				break;
			usleep(2000);
			continue;
		}
		uint32 room = job.buflen - job.fill;
		if (job.total)
			room = job.total - job.done < room ? job.total - job.done : room;
		uint32 made = room ? produce(job.buf + job.fill, room) : 0;
		job.fill += made;
		job.done += made;
		if (job.total && job.done >= job.total)
			break;
		if (job.fill >= job.buflen) {
			if (job.interrupt) {
				call_interrupt(job.pb, job.interrupt, job.buf, job.peak, (uint32)cfg.bits);
				job.peak = 0;
			}
			job.fill = 0;
		}
		if (made == 0)
			usleep(2000);
	}
	Job d = job;
	job = Job();
	WriteMacInt32(d.pb + SPBRec::count, d.done);
	WriteMacInt32(d.pb + SPBRec::milliseconds, bytes_to_ms(d.done));
	WriteMacInt16(d.pb + SPBRec::error, 0);
	return noErr;
}

static uint32 stk16(uint32 sp, int off) { return ReadMacInt16(sp + off); }
static uint32 stk32(uint32 sp, int off) { return ReadMacInt32(sp + off); }

/* One SPB call. `sp` addresses the return address; the arguments follow it (the last one first), then the result
 * slot. Returns false when the call is not ours. */
static bool spb_call(uint32 selector, uint32 sp, uint16 *pop)
{
	uint32 args = sp + 4;
	int16 err = noErr;
	uint32 res = 0;		// where the OSErr result goes
	switch (selector) {
		case 0x0000:	// SPBVersion: NumVersion
			WriteMacInt32(args, 0x03108000);
			*pop = 0;
			return true;
		case 0x0110:	// SPBSignOutDevice(short)
			*pop = 2; res = args + 2;
			break;
		case 0x030c:	// SPBSignInDevice(short, name)
			*pop = 6; res = args + 6;
			break;
		case 0x0514: {	// SPBGetIndexedDevice(short count, Str255 name, Handle *icon)
			*pop = 10; res = args + 10;
			uint32 icon = stk32(sp, 4), name = stk32(sp, 8), count = (int16)stk16(sp, 12);
			if (count != 1) {
				err = siBadSoundInDevice;		// what the Sound control panel stops its walk on
				break;
			}
			if (name)
				put_pstring(name, DEVICE_NAME);
			if (icon) {
				uint32 h = new_handle(256);		// a blank 32x32 icon and its mask
				WriteMacInt32(icon, h);
			}
			break;
		}
		case 0x0518: {	// SPBOpenDevice(name, short permission, long *ref)
			*pop = 10; res = args + 10;
			uint32 refp = stk32(sp, 4);
			int perm = (int16)stk16(sp, 8);
			uint32 name = stk32(sp, 10);
			if (!name_ok(name)) {
				err = siBadDeviceName;
				break;
			}
			if (perm == 1 && cfg.write_ref) {
				err = siDeviceBusyErr;
				break;
			}
			uint32 ref = make_ref(cfg.next_ref++);
			while (cfg.refs.count(ref))
				ref = make_ref(cfg.next_ref++);
			cfg.refs.insert(ref);
			if (perm == 1) {
				cfg.write_ref = ref;
				source_start();		// only a writer records; the Sound panel opens the device for reading every few seconds
			}
			if (refp)
				WriteMacInt32(refp, ref);
			if (perm == 1)
				NW_DIAG("NW-MIC: SPBOpenDevice for recording -> ref %08x\n", (unsigned)ref);
			break;
		}
		case 0x021c: {	// SPBCloseDevice(long ref)
			*pop = 4; res = args + 4;
			uint32 ref = stk32(sp, 4);
			if (!valid_ref(ref)) {
				err = siBadRefNum;
				break;
			}
			if (job.active && job.ref == ref)
				finish_job(noErr);
			cfg.refs.erase(ref);
			if (ref == cfg.write_ref) {
				cfg.write_ref = 0;
				source_stop();
			}
			if (ref == cfg.write_ref || !cfg.write_ref)
				NW_DIAG("NW-MIC: SPBCloseDevice ref %08x\n", (unsigned)ref);
			break;
		}
		case 0x0320: {	// SPBRecord(SPBPtr, Boolean async)
			*pop = 6; res = args + 6;
			bool async = ReadMacInt8(args) != 0;
			err = start_record(stk32(sp, 6), async);
			break;
		}
		case 0x0424:	// SPBRecordToFile(short fRefNum, SPBPtr, Boolean)
			*pop = 8; res = args + 8;
			err = start_record(stk32(sp, 6), ReadMacInt8(args) != 0, true, stk16(sp, 10));
			break;
		case 0x0228:	// SPBPauseRecording(long)
			*pop = 4; res = args + 4;
			if (job.active)
				job.paused = true;
			break;
		case 0x022c:	// SPBResumeRecording(long)
			*pop = 4; res = args + 4;
			if (job.active) {
				job.paused = false;
				tm_prime();
			}
			break;
		case 0x0230:	// SPBStopRecording(long)
			*pop = 4; res = args + 4;
			if (job.active) {
				if (!job.to_file && (job.in_callbacks || !tm_installed)) {
					job.stop_req = true;
				} else {
					pump();		// what has come in since the last tick
					finish_job(noErr);
				}
			}
			break;
		case 0x0e34: {	// SPBGetRecordingStatus(long ref, short *status, short *meter, ulong *totS, ulong *numS, ulong *totMs, ulong *numMs)
			*pop = 28; res = args + 28;
			uint32 numMs = stk32(sp, 4), totMs = stk32(sp, 8), numS = stk32(sp, 12), totS = stk32(sp, 16),
				meter = stk32(sp, 20), status = stk32(sp, 24);
			uint32 fb = frame_bytes();
			if (status)
				WriteMacInt16(status, job.active ? (job.paused ? 2 : 1) : 0);
			if (meter) {
				if (!job.active && cfg.meter && cfg.write_ref)
					measure_level();		// the level meter works without a recording, as on the hardware
				WriteMacInt16(meter, (uint16)meter_peak);
				meter_peak = 0;
			}
			if (totS)
				WriteMacInt32(totS, job.total / fb);
			if (numS)
				WriteMacInt32(numS, job.done / fb);
			if (totMs)
				WriteMacInt32(totMs, bytes_to_ms(job.total));
			if (numMs)
				WriteMacInt32(numMs, bytes_to_ms(job.done));
			break;
		}
		case 0x0638:	// SPBGetDeviceInfo(long ref, OSType, void *)
			*pop = 12; res = args + 12;
			err = valid_ref(stk32(sp, 12)) ? get_info(stk32(sp, 12), stk32(sp, 8), stk32(sp, 4)) : (int16)siBadRefNum;
			break;
		case 0x063c:	// SPBSetDeviceInfo(long ref, OSType, void *)
			*pop = 12; res = args + 12;
			err = valid_ref(stk32(sp, 12)) ? set_info(stk32(sp, 12), stk32(sp, 8), stk32(sp, 4)) : (int16)siBadRefNum;
			break;
		case 0x0440:	// SPBMillisecondsToBytes(long ref, long *ms)
		case 0x0444: {	// SPBBytesToMilliseconds(long ref, long *bytes)
			*pop = 8; res = args + 8;
			uint32 p = stk32(sp, 4);
			if (!valid_ref(stk32(sp, 8)) || p == 0) {
				err = siBadRefNum;
				break;
			}
			uint32 v = ReadMacInt32(p);
			WriteMacInt32(p, selector == 0x0440 ? ms_to_bytes(v) : bytes_to_ms(v));
			break;
		}
		default:
			return false;
	}
	WriteMacInt16(res, (uint16)err);
	return true;
}

/* ---- the head patch on _SoundDispatch ---- */


static uint32 gestalt_old;		// the Gestalt selector function that was there for 'snd '
static uint32 th_gestalt;

/* Gestalt('snd '): tell programs there is a sound input device. SimpleSound's New and Create commands, and other
 * programs, are greyed out without the "has a sound input device" bit. The selector function is
 * pascal OSErr (OSType selector, long *response): the stack is the return address, response, selector, result. */
static void gestalt_call(M68kRegisters *r)
{
	/* the stub pops 8 bytes of arguments itself; nothing is passed back through shared memory */
	uint32 sp = r->a[7] + 60;
	uint32 resp = ReadMacInt32(sp + 4), sel = ReadMacInt32(sp + 8);
	static const bool trace = getenv("NW_MIC_TRACE") != NULL;
	if (trace)
		NW_DIAG("NW-MIC: Gestalt function: ret %08x sel '%c%c%c%c' resp %08x res-slot %04x\n", (unsigned)ReadMacInt32(sp), (char)(sel >> 24), (char)(sel >> 16), (char)(sel >> 8), (char)sel, (unsigned)resp, (unsigned)ReadMacInt16(sp + 12));
	int16 err = (int16)paramErr;
	if (th_gestalt && gestalt_old) {
		M68kRegisters c;
		memset(&c, 0, sizeof c);
		c.a[1] = gestalt_old;
		c.d[1] = sel;
		c.a[2] = resp;
		Execute68k(th_gestalt, &c);
		err = (int16)c.d[0];
		if (err == noErr && resp)
			WriteMacInt32(resp, ReadMacInt32(resp) | 0x000000f0);	// built-in input, has an input device, play and record, 16-bit
	}
	WriteMacInt16(sp + 12, (uint16)err);
}

void nw_spb_dispatch(M68kRegisters *r)
{
	if (r->d[0] == 0x4753544c) {		// 'GSTL': the Gestalt selector function, not a Sound Manager call
		gestalt_call(r);
		return;
	}
	uint32 selector = (r->d[0] >> 16) & 0xffff;
	uint32 sp = r->a[7] + 60;		// past the 15 registers the stub saved: the return address
	uint16 pop = 0;
	/* The low word of D0 picks a group of routines. SPB is group 0x14; 0x08, 0x0c and 0x18 are other parts of the
	 * Sound Manager with selectors that look the same (0x022c with 0x18 is not SPBResumeRecording). */
	if ((r->d[0] & 0xffff) == 0x14)
		file_service();
	bool ours = (r->d[0] & 0xffff) == 0x14 && spb_call(selector, sp, &pop);
	static const bool trace = getenv("NW_MIC_TRACE") != NULL;
	if (trace && selector != 0x0040)
		NW_DIAG("NW-MIC: _SoundDispatch %08x %s  args %08x %08x %08x %08x\n", (unsigned)r->d[0], ours ? "handled" : "passed on",
		       (unsigned)ReadMacInt32(sp + 4), (unsigned)ReadMacInt32(sp + 8), (unsigned)ReadMacInt32(sp + 12), (unsigned)ReadMacInt32(sp + 16));
	if (ours) {
		/* The answer goes into the register frame the stub saved, not into memory: the Time Manager and the Sound
		 * Manager's own interrupt code can call the dispatcher while the stub runs, and a shared flag was overwritten
		 * by them. D0 = -1 says "handled" (a real D0 is a selector and group, never -1) and D1 is the argument bytes. */
		WriteMacInt32(r->a[7], 0xffffffff);
		WriteMacInt32(r->a[7] + 4, pop);
	}
}

bool nw_sound_input_wanted(void)
{
	return PrefsFindBool("mic") || tone_wanted();
}

void nw_sound_input_install(void)
{
	static bool done;
	if (done)
		return;
	done = true;
	M68kRegisters rr;
	memset(&rr, 0, sizeof rr);
	rr.d[0] = 0xa800;
	Execute68kTrap(0xa746, &rr);		// GetToolTrapAddress: the Sound Manager's dispatcher
	uint32 orig = rr.a[0];
	/* A PowerPC caller does not go through the trap table: InterfaceLib's glue calls Mixed Mode descriptors that
	 * lead straight to the dispatcher's code. So the patch is in the dispatcher itself, whose first two instructions
	 * (movea.l ($2b6).w,a0 / movea.l $110(a0),a0) are replaced by a jump to the stub, which runs them when the
	 * call is not ours. */
	if (ReadMacInt32(orig) != 0x207802b6 || ReadMacInt32(orig + 4) != 0x20680110) {
		NW_DIAG("NW-MIC: the Sound Manager dispatcher at %08x is not what was expected (%08x %08x); sound input not installed\n",
		       (unsigned)orig, (unsigned)ReadMacInt32(orig), (unsigned)ReadMacInt32(orig + 4));
		return;
	}
	uint8 stub[56];
	int n = 0;
	auto w16 = [&](uint16 v) { stub[n++] = (uint8)(v >> 8); stub[n++] = (uint8)v; };
	auto w32 = [&](uint32 v) { w16((uint16)(v >> 16)); w16((uint16)v); };
	w16(0x48e7); w16(0xfffe);			// movem.l d0-d7/a0-a6,-(sp)
	w16(M68K_EMUL_OP_SPB);
	w16(0x4cdf); w16(0x7fff);			// movem.l (sp)+,d0-d7/a0-a6   (the handler may have put -1 in D0 and the bytes in D1)
	w16(0x0c80); w32(0xffffffff);		// cmpi.l #-1,d0
	w16(0x670e);						// beq.s +14: handled (skip the pass-through below)
	w16(0x2078); w16(0x02b6);			// the two instructions the jump replaced
	w16(0x2068); w16(0x0110);
	w16(0x4ef9); w32(orig + 8);			// jmp back into the dispatcher
	w16(0x205f);						// movea.l (sp)+,a0   the return address
	w16(0xdec1);						// adda.w d1,sp       the arguments
	w16(0x4ed0);						// jmp (a0)
	uint32 patch = SheepProc(stub, (uint32)n);
	WriteMacInt16(orig, 0x4ef9);
	WriteMacInt32(orig + 2, patch);
	WriteMacInt16(orig + 6, 0x4e71);	// nop
	/* Gestalt('snd '): a selector function that adds the input bits to whatever the old one answers. */
	static const uint8 old_call[] = {	// A1 = old function, D1 = selector, A2 = response: result in D0
		0x55, 0x8f,		// subq.l #2,sp
		0x2f, 0x01,		// move.l d1,-(sp)
		0x2f, 0x0a,		// move.l a2,-(sp)
		0x4e, 0x91,		// jsr (a1)
		0x30, 0x1f,		// move.w (sp)+,d0
		0x48, 0xc0,		// ext.l d0
		0x4e, 0x75		// rts
	};
	th_gestalt = SheepProc(old_call, sizeof old_call);
	uint8 g[40];
	int gn = 0;
	auto g16 = [&](uint16 v) { g[gn++] = (uint8)(v >> 8); g[gn++] = (uint8)v; };
	auto g32 = [&](uint32 v) { g16((uint16)(v >> 16)); g16((uint16)v); };
	g16(0x48e7); g16(0xfffe);			// movem.l d0-d7/a0-a6,-(sp)
	g16(0x203c); g32(0x4753544c);		// move.l #'GSTL',d0
	g16(M68K_EMUL_OP_SPB);
	g16(0x4cdf); g16(0x7fff);			// movem.l (sp)+,d0-d7/a0-a6
	g16(0x205f);						// movea.l (sp)+,a0   the return address
	g16(0xdefc); g16(0x0008);			// adda.w #8,sp       the arguments
	g16(0x4ed0);						// jmp (a0)
	uint32 gcode = SheepProc(g, (uint32)gn);
	uint32 gproc = sys_alloc(6);		// a jump to it
	WriteMacInt16(gproc, 0x4ef9);
	WriteMacInt32(gproc + 2, gcode);
	M68kRegisters gr;
	memset(&gr, 0, sizeof gr);
	gr.d[0] = FOURCC('s','n','d',' ');
	gr.a[0] = gproc;
	Execute68kTrap(0xa5ad, &gr);		// ReplaceGestalt()
	gestalt_old = gr.a[0];
	NW_DIAG("NW-MIC: Gestalt 'snd ' replaced (error %d, old function %08x)\n", (int)(int16)gr.d[0], (unsigned)gestalt_old);
	NW_DIAG("NW-MIC: sound input installed in the Sound Manager dispatcher at %08x (stub %08x)%s\n", (unsigned)orig,
	       (unsigned)patch, tone_wanted() ? ", test tone" : "");
}
