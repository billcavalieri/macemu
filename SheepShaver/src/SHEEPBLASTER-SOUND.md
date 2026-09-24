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
