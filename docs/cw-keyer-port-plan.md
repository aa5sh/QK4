# CW keyer port: K4-Companion's keyer into QK4

**Status:** phase 1 code complete, **not yet bench-tested** (2026-10-27).
- `99e4d6e`: keyer, sidetone scheduler and serial poller ported. Dale's 554 recorded runs replay
  byte for byte.
- Wired into the app: `PaddleKeyer` replaces `IambicKeyer` on the HaliKey path. V1.4 lines and raw
  MIDI feed it; it sends `KZD`/`KZU`. `SidetoneGenerator` is now a pull-mode output driven by
  `SidetoneScheduler`, still on its own sink (phase 2 moves it into the RX output). The HaliKey page
  has the keyer mode, input type, dit memory delay and monitor mute. Threaded test is clean under
  ThreadSanitizer.
- Deferred: `SerialLinePoller` is not wired. The V1.4 worker's existing per-platform loops (1–2 ms
  confirm) still feed the keyer, which keeps its hardware-verified Windows/Linux behaviour. The
  catch is that a **serial straight key has no 20 ms debounce yet**, so contact bounce could key
  short extra elements. A MIDI straight key is debounced by its firmware.
**Source:** Dale Farnsworth's [K4-Companion](https://github.com/DaleFarnsworth/K4-Companion),
commit `040f711` (v2.2), GPL-3.0, the same licence as QK4. Ported files carry an SPDX header and
credit him.
**Scope:** HaliKey paddle and straight-key keying over serial and MIDI, and its sidetone.
**KPOD+ keying is out of scope and must not change.**

## Why his keying is more reliable

| # | K4-Companion | QK4 today | Effect |
|---|---|---|---|
| 1 | `KZD####` / `KZU####`: the timing travels in the command (gap before, length of) | `KZ.` / `KZ-` sent at element start, untimed (`cwcontroller.cpp:139-170`) | The K4 replays exact intervals, so network jitter and thread stalls can't change what goes on air |
| 2 | Edges stamped at capture: MoMIDI's own clock, 1 ms serial poll. Decisions read the paddles **as of when they were due**, from a 64-entry history | MIDI time discarded (`halikeymidiworker.cpp:174`); decisions read the live lever when the timer fires | A 0.44 s host stall no longer turns S + T into V |
| 3 | Iambic memories measured from when the previous element was **heard** (and the last look), on an absolute grid with wake-overrun learning | Latches on lever edges, `QTimer` in whole ms | Bench-swept at 20–50 WPM: squeeze, tap, swap, dah-first, stall |
| 4 | An element is committed only once it is wanted. No queue-then-retract race | n/a | No phantom dits |
| 5 | Sidetone edges placed on exact frames against the device's play-out time; delay fixed at the first edge, never ratcheted by non-audio lateness, eases back 1 ms/s | One pre-built block per element pushed into a separate sink; partial writes ignored (`sidetonegenerator.cpp:201`) | Every element sounds its true length, with a constant delay |
| 6 | Optional `ML0000` for the burst so the K4's late monitor copy isn't heard on top | n/a | No echo (bench question; see below) |

Items 1 and 2 fix what goes on air. Items 3 to 6 fix the feel and the monitoring.

## Decisions (2026-10-27)

- **Sidetone clock: Qt output with frame counting.** No PortAudio. Dale himself falls back to
  frame counting where PortAudio's DAC times stray by more than 1 ms, and on Linux and Pi they
  often do. The clock source sits behind `SidetoneScheduler::StreamTimeFn` and the block-timing
  call, so PortAudio could be added later without touching the keyer.
- **Sidetone mixed into the RX output** (one device, one clock). The tone crossfades over RX
  audio as Dale's does. This replaces `SidetoneGenerator`'s separate sink.
- **K4 monitor mute: built, off by default.** If the bench shows the K4 already suppresses its
  monitor for `KZD`/`KZU` (it does for `KZ.`/`KZ-`), it stays off and the K4 menu setting covers
  the rest.
- **Modes:** Iambic A, Iambic B (default), Plain Iambic, Ultimatic, plus straight key / external
  keyer passthrough (#130).
- **Dit memory delay: in, default 33 %** (Dale's app default). Iambic B only.
- **Auto character/word spacing: out.** It doesn't fix reliability. With it off, Dale's
  `auto_space_until()` returns None everywhere, so leaving it out changes nothing else.

## What changes, file by file

**New**
- `src/hardware/keyertiming.h`: the Clock / Link / OneShotTimer seams and `pyRound`.
- `src/hardware/paddlekeyer.{h,cpp}`: the port of `Keyer` (`k4companion:19313-21356`).
- `src/audio/sidetonescheduler.{h,cpp}`: the port of the sidetone half of `Audio`
  (`9122-9440`, `10473-10727`, `11001-11068`).
- `src/hardware/seriallinepoller.{h,cpp}`: the port of `SerialInput.signal_loop`
  (`18890-18970`).
- Production seams: steady_clock plus a precise sleep (Windows high-resolution waitable timer, as
  the V1.4 worker already uses); a `Link` onto `TcpClient::sendCAT` on the I/O thread; a QTimer for
  the monitor restore.

**Replaced** (HaliKey only)
- `IambicKeyer` → `PaddleKeyer`, on the existing "Keyer" HighPriority thread. `run()` blocks on
  its event queue, so the thread has no Qt event loop.
- `SidetoneGenerator` → `SidetoneScheduler` plus a mixer stage in `AudioEngine`'s output.
- `HalikeyDevice` / `halikey_edge.h`: edge dedupe moves into the keyer (MIDI up-without-down) and
  the poller (rest state, straight-key debounce).
- `halikeyv14worker`: the per-platform loops become one 1 ms poll through `SerialLinePoller`,
  keeping the native port open/close and the Windows timer.
- `halikeymidiworker`: hands raw `(status, note, velocity, deltaTime)` to
  `PaddleKeyer::midiMessage` directly, with no queued hop (Dale calls it on RtMidi's thread).
- `CwController`, HaliKey half: no more `KZ.`/`KZ-`/`KZ `/`KZP` wiring. Speed, weight and pitch
  from RadioState feed `PaddleKeyer::setSpeed` and the sidetone.

**Kept exactly as is (KPOD+)**
- `kpodplusdevice.*`, `kpodplususbworker.*`, the EP02 thread.
- `cwcontroller.cpp:261-262` (EP02 → `TcpClient::sendCATBytes`), moved verbatim if the file is
  restructured.
- `ConnectionController::m_kpodPlusKeyerActive` and `HardwareController::kpodPlusOwnsCw` →
  `CwController::setKpodPlusGate`. `PaddleKeyer` must refuse to key while the gate is set:
  `releasePaddles()` when it closes, and drop events while it's set.
- The `keyerSpeedChanged` / `keyerPaddleChanged` / `cwPitchChanged` lambdas keep their
  `m_kpodPlus->set…` calls unchanged.
- `test_kpodplususbworker` must stay green and untouched.

**Retired**
- `iambickeyer.*`, `sidetonegenerator.*`, `halikey_edge.h`
- `test_iambickeyer`, `test_halikeydevice`. Their cases are superseded by the sweeps; the
  Iambic A semantics change, see below.

**Docs:** `src/hardware/README.md`, `src/audio/README.md`, `docs/k4-protocol-quirks.md` §8,
`docs/halikey-cw-trace.md`, `docs/controllers.md`, and the CLAUDE.md test table.

## Phases

1. **Keyer on the air.** `PaddleKeyer`, `SerialLinePoller` and the MIDI path; `KZD`/`KZU` out.
   `SidetoneScheduler` lands too, because the keyer's decisions depend on when a tone was heard.
   Until phase 2 it's fed by a stand-in block clock (a QTimer at the old sink's block rate), so
   on-air keying can be bench-tested alone. Tests: all four suites below.
2. **Sidetone in the RX output.** A mixer stage in `AudioEngine`'s output callback calls
   `noteBlockTiming` / `nextKeyEdge` per block, with DAC time = time of fill + frames still
   queued in the sink. Envelope and tone come from Dale (4 ms raised cosine, exact-period table,
   continuous phase). It needs AudioEngine's output in pull mode. Check that first: it's part of
   the audio path currently being reworked.
3. **Monitor mute decision** from the bench, then remove or keep the setting.

## Tests (registered, passing)

| Suite | File | What it pins |
|---|---|---|
| PaddleKeyerSweepsTests | `tests/test_paddlekeyersweeps.cpp` | **Golden replay of Dale's 554 recorded runs, byte for byte** (`tests/data/paddlekeyer/`); squeeze rule at 20–50 WPM; squeeze rule at Dit Mem 33 % and proof the setting bites; dah-first and stall verdicts at 20–50 WPM; KZU always nominal; mute off changes nothing but the ML |
| PaddleKeyerTests | `tests/test_paddlekeyer.cpp` | MoMIDI velocity/aftertouch decoding, velocity 0/127 → host delta, note-on vel 0 = down, unmatched up dropped; straight key: 5 s cap, minimum tone, single KZU, release closes an open KZD; close; every KZD closed in every run; release mid-squeeze; Ultimatic, Plain Iambic, Iambic A; weighting; monitor restore |
| SerialLinePollerTests | `tests/test_seriallinepoller.cpp` | Rest state not an edge; paddles on the same poll with per-line delta; dit first; 20 ms straight-key debounce and bounce restart; dah ignored when straight |
| SidetoneSchedulerTests | `tests/test_sidetonescheduler.cpp` | First edge fixes delay at lead + block; keyer-late and skipped-block downs dragged; other late downs raise the delay; late ups dragged; exact-frame placement; cancel before/after placement; ease in silence; edge deadline |

`tests/paddlekeyersim.h` is the C++ port of `paddle_sweep.py`'s `sim()`: virtual clock, 48 kHz,
384-frame blocks, 16 ms lead, 0.1 ms hop. The keyer's sleeps advance the clock and run the blocks
and deliveries inside them.

The golden files were recorded by Dale at `040f711`. His harness couldn't be re-run here (it
executes the downloaded script, which was not permitted), and its stub lacks two `Audio` fields the
current code reads. So the recordings may predate a late commit or two. A run that differs
because of that shows up as a single named failing run; check it against his code before
changing either.

## Porting rules

- Seconds are `double` and every expression keeps the Python order, so results are bit-identical.
- `round()` is `CwKeying::pyRound` (half to even). `math.ceil` / `math.floor` map to
  `std::ceil` / `std::floor`. `whole_ms(s) = ceil(s * 1000) / 1000`. `int(x)` truncates.
- MIDI: `0x90` is key-down whatever the velocity; only `0x80` releases. Every mapped or unmapped
  `0x90`/`0x80` advances the device clock. CC/aftertouch value × 126 ms. Velocity 0 or 127 →
  RtMidi's delta. Re-anchor when a delta or an offset jump reaches 1 s. Velocity is an `int`: the
  harness sends aftertouch counts above 127, as Dale's does.
- Serial: open the port with DTR and RTS asserted (pyserial's default, which `QSerialPort` doesn't
  apply), and treat the line state at open as rest.
- Shutdown: `releasePaddles()` → post Close → join the keyer thread → detach the device callbacks
  → `releasePaddles(true)`. Detaching before the join can hang the join.
- Shared state written by the audio callback and read by the keyer (`sidetone_block_timing`,
  `sidetone_down_placed`, the lag and edge shift, the asked/late/cancelled/dropped stamps, the
  key-event queue) sits behind one short `SidetoneScheduler` mutex. K4-Companion relies on the GIL
  for the same atomicity. The mutex is never held across a sleep, a send or a call out, so the
  callback waits at most a few hundred nanoseconds. If phase-2 profiling shows priority inversion
  on any platform, the fallback is per-field atomics with a seqlock for the tuples.
- Keyer-side state follows K4-Companion's locks: `first_element_lock` becomes `m_firstElementMutex`;
  paddle stamps and history get their own leaf mutex; straight-key on-air state gets a mutex plus
  an atomic `k4Keyed` exchange, so two releases can never send two KZUs. No lock is held across
  `Link::send` or `Clock::sleep`, which the virtual-clock harness relies on.
- Deliberate deviation: after `close()`, paddle events are dropped at the door instead of queued
  for a thread that has gone.

## Behaviour changes testers will notice

- **Iambic A changes meaning.** QK4's current A reads live paddles only, after the CW-015/016 fix.
  Dale's A keeps the dit memory (a dit closed after a dah was heard is sent) but no dah memory and
  no squeeze-release memory. The port follows Dale; `iambicAKeepsDitMemoryOnly` pins it.
- **Serial pin map becomes a per-paddle setting.** Defaults have to keep today's HaliKey V1.4
  wiring (dit = CTS, dah = DCD or DSR). Dale's model has one line per paddle, so the dah default
  needs either "DCD or DSR" support in the poller or a check of which line the V1.4 actually
  drives. Decide before phase 1 lands.
- Weighting follows the K4's `KP`, which QK4 already parses.

## Bench questions (before phase 1 ships)

1. ~~What does `KZL` mean?~~ **Answered 2026-10-27** from the K4 CAT reference: remote paddle
   key-down initial delay, 0–1000 ms, default 80. QK4 had been overwriting it with 1200 / WPM.
   Both writes are removed; RadioState now reads it from the RDY dump
   (`keyDownInitialDelayMs()`), and `PaddleKeyer::setKeyDownInitialDelayMs` is fed from there.
   See `k4-protocol-quirks.md` §8.
2. Does the K4 suppress its own monitor for `KZD`/`KZU`, as it does for `KZ.`/`KZ-`? This decides
   phase 3.
3. Is `ML0000;` echoed? If it is, the local sidetone volume must not follow the echo.
4. Does `KY \x04;RX;` (Esc) stop `KZD` playback already queued in the K4?
5. Which modem line does the HaliKey V1.4 assert for dah: DCD, DSR or both?
