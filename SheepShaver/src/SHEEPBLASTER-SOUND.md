# SheepBlaster: Sound and SimpleSound together

Both of these must be true at the same time:

- The Sound control panel can select SheepBlaster and play an alert. The menu clock keeps moving.
- SimpleSound can play a list item and return to the desktop. The menu clock keeps moving.

Status: both work in the same session with the design below (runs sb8 and sb9). The Sound panel played two alerts (7676 and 9260 frames). SimpleSound then played Indigo and stayed open in the background. The clock moved 6:31 to 6:33, and shutdown was clean (disk attribute 0x100).

## Current design

The host never calls the completion routine. The Apple Mixer does.

- AddSource, RemoveSource, StartSource, StopSource, GetInfo, SetInfo and PlaySourceBuffer are delegated to the mixer. The mixer calls `completionRtn(&pb)` itself, from inside GetSourceData, in real guest context. It also follows the chaining return.
- Samples are pulled by a guest Time Manager task. The 68k routine is `EMUL_OP_SHEEPBLASTER_TICK; tst.l d0; beq.s +2; PrimeTime; rts`. It is installed with InsXTime when the output device is initialized, and removed with RmvTime when the last instance closes.
- Each tick (`AudioSheepBlasterTick`) calls GetSourceData once, and only when the ring holds fewer than 11025 frames. The mixer chunk is 512 frames, about 12 ms. A longer clip's buffers shrink to 92 frames, and one 4096-frame GetSourceData ran every completion in that chain before returning. The steady part of that clip then froze for 0.4 s, six times, about a second apart, while the codec kept running. While the ring is ahead the tick does not enter the mixer. It sleeps for the surplus above the cushion, at most 40 ms. StartSource and PlaySourceBuffer set the running flag and prime the task (first wake at 10 ms). StopSource and PauseSource clear it, and so does the last RemoveSource. Pulling 32 blocks in one interrupt after StopSource hardlocked QuickTime at `pc=00ec1544`.
- `audio_data` is allocated in the system heap (`ResrvMem` SYS, then `NewPtrSysClear`). OpenMixer runs with `TheZone` set to `SysZone`.
- `nw_host_tick` only presents video. It never enters 68k code.
- EmulOp skips `nw_register_output` and the debug scan for the sound ops. Registration allocates memory, which is not allowed at interrupt time.

## What was tried

1. Hand PlaySourceBuffer to the Apple mixer and wait for it to finish.
2. Copy the samples into the SheepBlaster ring and return immediately. Do not call the completion routine.
3. Call the completion routine with Execute68k from inside PlaySourceBuffer.
4. Call that routine again from the 60 Hz host tick, including a second time if it queues another buffer.
5. Call that routine from native PowerPC, with the 68k emulator registers filled in.
6. Call that routine only while the 68k stack is live, still with A5 left at 0.
7. Call that routine on the 68k stack with A5 set from low memory 0x904 (CurrentA5).
8. Same call, but keep the 68k stack 4-byte aligned (`subq.l #4` instead of `subq.w #2`).
9. Register the component as an input and an output (flags `0x00000f0f`).
10. Register it as output only (flags `0x00000f00`).
11. Call SetDefaultSoundOutput during registration.
12. Scan guest RAM for the built-in sound driver and patch it.
13. Pull GetSourceData from the mixer while an alert is starting.
14. Push the sound parameter block itself (not its address) and abandon the call after a few timer ticks.
15. Delegate to the mixer and pull from a guest Time Manager task, with everything in the system heap. This works.

## Why each one failed

1. Mixer wait. The alert log shows one empty `sheepblaster src` (`frames=0`) and then no return. The desktop locks.
2. Alert audio plays and the desktop can stay up. SimpleSound still waits, because this path never runs the completion. One Sound session also jumped once to `0x0002cc78` (instruction word 0) and quit with error type 3, without locking the machine.
3. The completion runs nested inside PlaySourceBuffer. It calls RemoveSource, queues a 1-frame tail, and the second entry is error type 3.
4. The 1-frame tail's completion is `sheepblaster end` with no `end back`. Error type 3. The desktop stays inside that call, so OK cannot finish.
5. Native call uses the PowerPC stack as the 68k stack. Error type 3.
6. `sheepblaster end` with no `end back`. The guest then executes zeros at `0x0002a278` (6144 times). Desktop locks.
7. A5 was valid (`a5=1e7ff65c`) and the call still did not return. Desktop locks. A5 was not the cause.
8. Alignment was not the cause either. The latest alert log is `end #1` with `a5=1e7ff23c` and no `end back`. The sound had already been copied (`out` lines).
9. Selecting SheepBlaster locks. The log never reaches `sheepblaster play`. The panel opens, asks for info, and closes, then the guest loops.
10. Selecting SheepBlaster works. This part stays.
11. Boot log stops on `audio-reg default enter`. The desktop never appears. Do not call this during registration.
12. Patching the built-in driver removed icons and later caused error type 3. The scan may log matches. It must not write them. The later 512 MB scan also stalls the desktop, so it is not armed from the tick.
13. An empty pull before the alert (`frames=0`) preceded the type 3 jump to `0x0002cc78`. Do not pull until a buffer has been given to the mixer.
14. Wrong argument. Disassembly shows the completion reads `*pb`, so it takes `SoundParamBlockPtr *` as documented. Abandoning a call also left the interrupted code with broken registers. Removed.

## Root causes

Three separate bugs. Each one breaks at least one of the two programs.

1. The host ignored chaining. The completion is `pascal Boolean (*)(SoundParamBlockPtr *pb)`. When it returns true, `*pb` points to the next buffer to play. A host-side call that drops the result loses the tail buffer.
2. Host-initiated Execute68k is unsafe. `nw_host_tick` runs from `tick_decrementer` at any PowerPC instruction, and `HandleInterrupt` returns early on New World (the NanoKernel owns interrupts). `execute_68k` saves only r13–r31, not r0–r12 or XER, so the interrupted code resumes with overwritten registers. This explains the zeros at `0x0002a278`, the jump to `0x0002cc78`, and error type 3. Never enter 68k code from the host tick.
3. Driver state lived in the caller's heap. `audio_data`, the old completion glue and the mixer were allocated in whatever zone was current at open time. For the Sound panel that was its application heap (zone `0x1e4d7a60`). When Sound quit, the heap went away and the next client used freed memory.

## What is still true

- The completion starts with `4e56` (`link a6`). It is not a Mixed Mode descriptor (`0xAAFE`).
- The log line `sheepblaster end back` no longer applies. The host does not call the completion.
- Component flags stay `0x00000f00`. No SetDefaultSoundOutput, AWACS patch, FindNextComponent or CaptureComponent.
- Clicking the alert that is already selected in the Sound panel plays nothing. Pick a different one when testing.
- QuickTime Player quit with error type 1 (bus error) at guest pc `0x00000c0c`, in the same instant as StopSource (selector 262) and RemoveSource (selector 258), after 512-frame `twos` chunks had already played. A Time Manager pull must not call GetSourceData while a component call is already inside the mixer, and Stop, Pause, and Remove must not enter the mixer again while that pull is running.

## Playback smoothness

The guest clock stays at 25 MHz per host second, so a stutter is a missed frame or a silence gap, not a slow emulator. Three plays of Sample Movie (`NW_JIT_STATS=1`) showed holes of 0.7–1.5 s at the start while the codec kept running, then about 10–15 fps. The same movie was smooth before the audio task ran. That task was a 10 ms Time Manager interrupt, and under the watermark it called GetSourceData twice, which runs QuickTime's completion on the CPU thread. The tick pulls while the ring holds fewer than 11025 frames and play has not stopped. Each wake takes the frames the speaker used since the last wake, and stops at 3 pulls or 3 ms. The completion's next buffer is held, so another pull of the same 1536-frame buffer is a copy, not another decode. The next wake is still 10 ms. An empty wake stays at 10 ms. Three empty wakes after audio has played stop the task. A full ring still sleeps instead of entering the mixer.

Damage uploads skip a tile whose pixels match the shadow. The host callback and the mixer chunk are both 512 frames, about 12 ms. Not yet listened to.

## QuickTime

QuickTime Player plays through the same path. In the choppy run it queued buffers of 5095, 13670, 24576 and 12288 frames, while the tick only kept 4096 frames queued, so the ring ran dry between ticks.

The next run, after raising that to 32 pulls and 11025 frames in one tick, hardlocked on a double-click of Sample Movie. The log shows one `PlaySourceBuffer` of 512 frames with a null completion, then `StopSource` (selector 262), then five `GetSourceData` pulls of 4096 frames, then `RemoveSource` and close. After that `clock10` stays at `pc=00ec1544` with `d_fr=0` (no screen updates) until the process is killed. The guest also reads unclaimed I/O at `ff8148fc`–`ff814904` from `pc=0084e630`. Pulling after Stop, inside the Time Manager interrupt, is what wedged it. The tick stops pulling as soon as Stop or Pause clears the run flag, and it will not take more than 512 frames or 6 buffers in one wake.

## MP3 decode cost

Measured on a full song (Debug app, 173 one-second samples). `cpu_per_audio` stayed between 0.068 and 0.082, average 0.069. Host time inside GetSourceData was about 53 ms per wall second, and about 33500 frames were queued per wall second (44100 would be realtime). The decode is not the budget. No pull schedule is blocked by it.

Inside those calls, `vr` was 0 and `vmx` was 0. iTunes is not running an AltiVec decoder on this path. `fp_gate` was about 90 per second. `op6` was about 110 per second, all at `680ff208`. The busiest compiled blocks were `6806e8c4`, `6806e8c0`, and `6806de40` (68k emulator ROM) and `50312b68` (NanoKernel). `no_chain_pc` was about 358000 per second inside the pull.

During the song, 3056 of 3105 `clock10` samples had `d_fr=0`. The screen was not changing. The sampled program counter was the idle loop `0027bae0` or iTunes (`1ded`/`1dfa`), not the decoder. The guest was running, and it was not drawing.

`cpu_per_audio` is only the mixer copy. iTunes owns the CPU in `1dedfd38`–`1dedfe58` and `1dfa2354`–`1dfa6b84`, and `_OSDispatch` does not advance during the song, so iTunes never yields.

Measured on the full song, 255 samples. `itunes-cost` `host_per_audio` was 2.3 to 3.9, average 3.55. About 1.8 s of host time and 13.5 million compiled instructions in iTunes produced about 22,600 frames. `interp` was 0 and `skip` was 0, so the loops are compiled. `fp_gate` was about 180 per sample and `class_change` was about 0 after the first second. The samples are not one wall second apart: the sound task cannot print while it is inside the decoder, so the lines are about 3.7 s apart.

`itunes-loop` at `1dfa2354` is `addi`, `rlwinm`, `lfs`, `lfsx`, `fmuls`, `fmadds`. At `1dedfd38` it is `lfs`, `fmuls`, `fmadds`, `fadds`, `fsubs`, `stfs`, and an indexed integer op. Integer and floating-point alternate, so the JIT keeps them in separate blocks. `fmadds` is compiled as a call to `nw_jit_helper_fmadds`, not an ARM floating-point instruction. That is the cost. Do not change the pull schedule for this.

## Not yet tested

- SimpleSound first, then the Sound panel.
- Quit SimpleSound with Cmd-Q, then play an alert from the Sound panel.
- Several alerts in a row in each program.

## Sound input (the host microphone)

Off by default. Turn it on with the VM setting "Let the virtual machine record from the microphone" (pref `mic true`).
Code: `nw_sound_input.cpp` (the guest side and the sample ring), `MacOSX/mic_capture_macosx.cpp` (CoreAudio capture),
`MacOSX/mic_permission_macosx.mm` (the macOS permission question).

Checked: the Sound control panel's Input tab lists the device and its level meter moves (with the real microphone
too); SimpleSound's File > New records, shows the level, plays the take back and saves it; `tools/shears/test.sh mic`
(25 checks in a guest program) and `tools/shears/test.sh recdlg` (the Sound Manager's record window opened and
cancelled by a guest program). Not working: Speech Recognition still says it cannot start; it opens the device, reads
its name and source, closes it and never records.

### Why it is not a SheepBlaster input

None of the things that look like a way in work on a New World Mac OS 9:

- The guest finds its input through a native driver for AWACS hardware this machine does not have, so the Sound
  panel's Input tab lists nothing ("... the device is in use by another application").
- Registering SheepBlaster with the input flag bits (`0x0f0f`) locks the panel (attempt 9 above).
- The classic `.AppleSoundInput` driver replacement (`SoundIn*` in `audio.cpp`) is installed by the `vCheckLoad` patch,
  which the 9.2.1 ROM does not have. Such a driver put into the unit table by hand is opened and closed once at boot and
  then ignored; with driver flags `0x4d04` the boot stalls. The Input tab never asks the Device Manager for it.

What every caller does reach is the Sound Manager's SPB routines (SPBGetIndexedDevice, SPBOpenDevice, SPBRecord, ...).
They are 68k code behind `_SoundDispatch` (0xA800), `D0 = (selector << 16) | group`. The group picks a part of the
Sound Manager: SPB is 0x14; 0x08, 0x0c and 0x18 are other parts whose selectors look the same (0x022c in group 0x18 is not
SPBResumeRecording). The selector's high byte is the argument size in words (SPBOpenDevice is 0x0518: ten bytes).

A PowerPC program reaches them through InterfaceLib's glue (Mixed Mode), and that glue does not read the trap table: it
calls routine descriptors that lead into the dispatcher's code. So `SetToolTrapAddress` is not enough, and the patch is in
the dispatcher itself, at the address the trap table holds. Its first eight bytes
(`movea.l ($2b6).w,a0 / movea.l $110(a0),a0`) are replaced by `jmp stub`. The stub saves the registers and runs the
EmulOp `OP_SPB`. The handler's answer comes back in the saved registers (D0 = -1 for "answered here", D1 = the
argument bytes to pop), not in memory: the Time Manager and the Sound Manager's own interrupt-time code can call the
dispatcher while the stub is running, and a shared flag word was overwritten by them. When the call is not ours the
stub runs the two displaced instructions and jumps back. The patch checks the eight bytes first and does nothing if they
are not the ones expected. Calls it does not know in group 0x14 (SndRecord, SndRecordToFile, SetupAIFFHeader, ...) go on
to the Sound Manager, which implements them on top of the SPB calls that come back here.

`Gestalt('snd ')` gets the "built-in input, has an input device, play and record, 16-bit" bits through a replaced
selector function (it calls the old one and adds them). Without the "has an input device" bit SimpleSound greys out
New. `ReplaceGestalt` wants the function in the system heap (gestaltLocationErr, -5553, otherwise): a six-byte jump.

### What it does

- One device and one source, both called "SheepBlaster". One writer at a time (a second SPBOpenDevice with write
  permission gets siDeviceBusyErr, -227). The capture runs only while a writer has the device open: the Sound panel
  opens it for reading every few seconds.
- The default is what a Mac of that era had: 22254.5 Hz, 8-bit offset-binary samples, mono. SPBSetDeviceInfo takes
  any rate from 4000 to 48000 Hz, 8 or 16 bit, one or two channels, a gain (0.5 to 1.5), and the quality names.
- SPBRecord: synchronous (the guest waits, as on a real Mac), or asynchronous with a completion routine and, for
  continuous recording (count and milliseconds 0), an interrupt routine called every `bufferLength` bytes. They run in
  a guest Time Manager task (`OP_SPB_TICK`, every 10 ms while recording), never from a host timer, like the output
  pull above. SPBStopRecording ends a recording and runs the completion routine without an error.
- SPBRecordToFile: the samples go to the open file at its mark. The tick only queues them; the File Manager is not safe
  at interrupt time, so the write happens at the start of the next SPB call (a normal context), and a recording that
  reached its count is finished there too. With no buffer in the SPB (SimpleSound passes none) a 32 KB one is used.
- The level meter works without a recording: siLevelMeterOnOff returns two integers, the state and the level
  (0 to 255), and SPBGetRecordingStatus returns the level too; both measure what has come in since the last look.
- Not done: compression other than none, the options dialog, play-through.
- The samples come from a ring of about a second and a half at 44100 Hz stereo 16-bit. The source is the host
  microphone (one AUHAL unit on the default input device) or, with `NW_MIC_TONE=<hz>`, a test tone made on a host thread.
  The guest's rate and format are made when the samples are taken out (linear interpolation). A recording starts with
  what the microphone hears now.
- The `PLAY` line has `mic_in` (frames the source made), `mic_out` (frames the guest took) and `mic_drop`.
- `NW_MIC_TRACE=1` (with `NW_VERBOSE=1`) logs every `_SoundDispatch` call, its first arguments and whether it was
  answered here. It slows the guest: a record window polls the level in a tight loop.

### What the programs expect that the documentation does not shout about

Each of these broke something real.

- **The reference SPBOpenDevice returns is a Device Manager reference number**, a negative 16-bit number in a long,
  and code inside the Sound Manager (its record window) hands it to the Device Manager as well. The first scheme here
  counted up from 0x4d1c0001; the low word 2 is a file reference number, the System file's, and the record window closed
  it. SimpleSound quit with a type 2 or 3 error when the window went away and the desktop fell apart. The references are
  now units far past the end of the unit table (0xffffbffe downwards), which the Device Manager refuses with badUnitErr.
- **The SPB's `error` field is above 0 while an asynchronous recording runs** (like an ioResult) and 0 or negative when
  it is over. SimpleSound's record window has no completion routine and watches that field: with 0 in it the recording
  stopped ten milliseconds after it began.
- **siNumberChannels is `'chan'`**; SPBGetIndexedDevice past the last device must return siBadSoundInDevice (-221), or the
  Sound panel walks the list for ever.
- A record window asks for siAsync, siRecordingQuality, siOSTypeInputSource (`'inpt'`, answered `'mic '`),
  siDeviceConnected, siInputAvailable and the gain range (`'igmn'`, `'igmx'`).

### Things that bit during the work

- `SheepMem::Reserve` is a stack that other code releases in order (`SheepVar`). A block taken from it inside
  `nw_register_output` was handed out again when that function's `SheepVar` went away. Anything that has to live as
  long as the guest comes from the system heap (`NewPtrSysClear`, trap 0xA71E; 0xA713 is not it and hangs the boot).
- File Manager position modes: fsAtMark is 0. With 1 (fsFromStart) every write landed at the start of the file.
- InterfaceLib returns a NumVersion (SPBVersion, SndSoundManagerVersion) through a pointer the caller passes in r3.
  Retro68's compiler expects it in a register and passes nothing, so a guest test that calls SPBVersion() as declared
  makes the glue store through whatever r3 held and takes a system error more often than not. That cost a long hunt
  for an emulator bug that was not there. `miccheck.c` calls it through a cast.
- Keeping the Apple Mixer open across the output component's Close (tried as a cure for a crash that turned out to be
  the reference numbers) hangs the guest after the first channel is disposed. `tools/shears/test.sh sndchan` shows it.
  The output code is as it was.

### A crash this work did not cause and did not fix

`tools/shears/test.sh sndchan` (a guest program that opens a sound channel, plays a quarter-second tone and disposes
of the channel, four times, sound input off) took a system error in one run of four: reported at 68k pc 002029fa, like
the crash at the end of songs in iTunes. It is the quickest way found so far to bring that crash out.

### Permission

The app has the `com.apple.security.device.audio-input` entitlement and `NSMicrophoneUsageDescription`. The first time
a guest opens the input for recording with the setting on, macOS asks to allow the microphone for SheepShaver. Until it
is allowed (and if it is refused) the guest records silence, and a warning line is printed.
