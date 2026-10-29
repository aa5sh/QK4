# utils/

Shared helper functions. Any helper needed in more than one translation unit goes here — Architecture Rule 1.

## Files

- `radioutils.{cpp,h}` — `RadioUtils::` namespace. Frequency → band (11 bands + gaps), band edges, tuning step tables, span-dial stepping with K4 quirks, and `ditMsForWpm()` — the single 1200/WPM conversion and keyer-speed clamp shared by the iambic keyer's element clock and the sidetone's block length. Four copies of that arithmetic had drifted apart before it landed here. Also: filter preset and edge stepping, EQ command building, SL tier → frame size, fixed-tune mode mapping, AGC and TX-bandwidth labels, frequency snapping and host/IPv4 validation. `radioutils.h` is the list.
- `bandplan.{cpp,h}` — `BandPlan::` namespace. Segment lookup (CW / data / beacon / phone) per IARU region, driving the panadapter band-plan overlay. Region 4 (`RegionUS`) is the US FCC layout with practical Extra-class mode edges. Covered by `test_bandplan`.
- `windowfit.h` — `WindowFit::` namespace. Main-window sizing rules: the size the window opens at (never below its layout's minimum) and the interface scale at which it fits a screen — shrinking on a small screen, growing to 125% on a large one with room for it (no step between 100% and 125%). Pure arithmetic on `QSize`, covered by `test_windowfit`.
- `wheelaccumulator.{cpp,h}` — `WheelAccumulator` class. Accumulates `QWheelEvent` angle deltas into discrete steps (optionally per key, e.g. per modifier), so a trackpad does not fire one tuning step per small delta.
- `latencyhistogram.h` — `LatencyHistogram`, fixed µs buckets with relaxed atomic counters: one thread records without locking or allocating, any thread reads a snapshot and its quantile bounds. Used for the KZ egress latency; covered by `test_kzegress`.
- `rtscheduling.{cpp,h}` — `RtScheduling::` namespace. `promoteCurrentThread(Class, name)`, called by a thread on itself, gives it the best scheduling the OS grants and falls back quietly: real-time is the Mach time-constraint policy (else QoS user-interactive) on macOS, MMCSS "Pro Audio" (else `TIME_CRITICAL`, else `HIGHEST`) on Windows, `SCHED_FIFO` 15 with `SCHED_RESET_ON_FORK` (else the High chain) plus 1 ns timer slack on Linux. High is QoS user-interactive on macOS, `THREAD_PRIORITY_HIGHEST` on Windows, and on Linux nice −10, else the lowest nice `RLIMIT_NICE` allows (20 − limit), else unchanged. Results go into a lock-free table that an ordinary thread logs with `takeUnreported()`. The per-OS decisions are pure functions over injectable calls, so `test_rtscheduling` checks all three on every platform. Not done: rtkit and the Flatpak Realtime portal (both D-Bus; QtDBus is not a dependency), so Linux without `RLIMIT_RTPRIO`/`CAP_SYS_NICE` falls back. `RtScheduling::Threads` is the table of every promoted thread and its class (CW threads real-time; the I/O, RX audio and TX audio threads High); `promoteOnStart(thread, role)` promotes an event-loop `QThread` on itself through a direct connection to `started`, on every start. Real-time: the keyer, sidetone, HaliKey V1.4 poller and KPOD+ EP02 threads. High: the I/O thread (`ConnectionController`), `AudioEngine` and `AudioTX` (`AudioController`). Process-wide: `optOutOfPowerThrottling()` (Windows, from `main()`: no EcoQoS and no timer-resolution throttling when minimised or occluded) and `LatencyCriticalActivity`, which `ConnectionController` holds while connected (macOS: an `NSProcessInfo` latency-critical, user-initiated activity, so App Nap and timer coalescing stay off; implemented in `rtscheduling_mac.mm`, Objective-C++ with ARC).
- `spscring.h` — `SpscRing<T, N>`, a preallocated single-producer single-consumer ring of trivially copyable items; push and pop never lock, allocate or wait. Several producers may share one if a mutex they all hold serialises them. The keyer's event and request queues.
- `rtsemaphore.{cpp,h}` — `RtSemaphore`, a counting semaphore on the OS primitive (dispatch semaphore, `sem_t`, Win32 semaphore), so any thread can wake a real-time thread without sharing a mutex with it. Wakes the keyer.
- `threadshutdown.{cpp,h}` — `ThreadShutdown::` namespace, the one way an owner stops a thread (CONVENTIONS Rules 11 and 14). `callBlocking()` is a `BlockingQueuedConnection` with a deadline, for the last call into a worker (`stop()`, `disconnectFromHost()`); `retire()` quits an event-loop thread and joins it, `join()` joins a loop-less one already told to stop. A thread that misses its deadline is disowned: unparented, logged once in the owner's category, left running with everything on it, and deleted if it ever finishes. Deleting it instead would crash (a running `QThread`) or hang (one made by `QThread::create()`, since Qt 6.3). Covered by `test_threadshutdown`.
- `macroids.h` — `MacroIds::` namespace. `QString` constants naming every macro slot (Startup, PF1–PF4, Fn.F1–F8, K-pod buttons, Keyboard-F1–F12, built-in functions) and the forbidden-startup-command check.

## Rule 1 — No duplicated static functions

If you catch yourself copy-pasting a helper into a second `.cpp` file, promote it to `utils/` with a namespace. Copy-pasting is a defect; fix it immediately.

## Shape

Most utilities are free functions in a namespace, with no state and no side effects. Two exceptions: `WheelAccumulator` is a small stateful class, and it and `windowfit.h` use QtGui types (`QWheelEvent`, `QSize`, `QRect`). Nothing here depends on widgets.

## See also

- `CONVENTIONS.md` → Architecture Rule 1.
- `docs/k4-protocol-quirks.md` → "Span dial skips 6 kHz going up" — the K4 behaviour the span
  helpers model, including the one case they do not yet match.
