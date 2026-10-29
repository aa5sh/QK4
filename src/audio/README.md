# audio/

RX and TX audio pipeline. Owned by `controllers/audiocontroller.cpp`.

## Files

- `audioengine.{cpp,h}` — `QAudioSink` + `QAudioSource` with a jitter buffer. A 10 ms feed timer writes as many bytes as the sink has free; playback starts after one SL-tier packet (`PREBUFFER_PACKETS = 1`, 20–120 ms of audio depending on SL). Handles volume, balance, mix routing, and owns the `OpusEncoder` used for TX.
- `opusdecoder.{cpp,h}` — Opus decode wrapper. K4 sends 12kHz stereo (left=Main, right=Sub). All four encode modes normalise by one rule, `native × K4_GAIN_BOOST / full_scale`; the per-mode scales were measured 2026-09-19, not tuned by ear. See `docs/k4-protocol-quirks.md` §11.
- `opusencoder.{cpp,h}` — Opus encode wrapper for TX. 12kHz mono; frame size reconfigured per K4 SL tier. `encodeFloat()` is what the TX path uses — Opus takes float natively, so the Opus modes never quantise.
- `rawaudioformat.h` — header-only, Qt-free. Builds the EM0 (S32LE, 24-bit) and EM1 (S16LE) TX wire payloads from a captured **Float32** mono frame. Holds `EM0_FULL_SCALE` (2^23 — EM0 is 24-bit in a 32-bit container), which `OpusDecoder::NORMALIZE_K4_RAW` is derived from so the two directions of EM0 cannot drift apart. See the WHY block for AUD-003 and the 2026-09-19 calibration.
- `audiodeviceselection.h` — header-only, Qt-free. Decides which device a stream belongs on when the device list changes. Follows the system default when nothing is pinned, and claims a PINNED device the moment it reappears — a pinned device absent at startup falls back silently, and without this the stream never returned to it.
- `audiodecimator.h` — header-only, Qt-free. Decimates a captured mic stream to the K4's 12 kHz. The factor comes from the rate the DEVICE reports, not one we demand: asking for a fixed 48 kHz produced zero bytes from an AirPods Pro while Qt reported the format as supported. See its WHY block (INT-005).
- `audioresampler.h` — header-only, Qt-free. Polyphase rational resampler for the capture rates `AudioDecimator` cannot take (44.1, 22.05, 16, 11.025, 8 kHz…). Before it, those microphones were refused and PTT keyed nothing (#180). Integer rates deliberately stay on `AudioDecimator` so their sound does not change. Tested in `tests/test_audioresampler.cpp`.
- `micrate.h` — header-only, Qt Core only. The wording the operator sees on screen (via `AudioEngine::micUnavailable`) when no microphone can be used, plus `isUsable()`, the same accept rule `setupAudioInput()` applies. Records why a mid-session rate change needs a restart. Tested in `tests/test_micrate.cpp`.
- `rxoutputstage.h` — header-only, Qt-free. The last stage of RX playback: the 0 to +12 dB boost above the MAIN/SUB sliders (Options → Audio Output) and the soft limiter used when it is above 0 dB. Tested in `tests/test_rxoutputstage.cpp`.
- `sidetonegenerator.{cpp,h}` — Real-time CW sidetone synthesis at 48kHz on its own audio sink. Already places its edges with `SidetoneScheduler`; phase 2 of `docs/cw-keyer-port-plan.md` moves it into a mixer stage in the RX output.
- `sidetonescheduler.{cpp,h}` — Sample-accurate sidetone edge placement, ported from K4-Companion (GPL-3.0): the keyer schedules "key down at T", the output callback places it on an exact frame against the block's play-out time. Delay fixed at the first edge (lead + block), never ratcheted by non-audio lateness, eased 1 ms/s in silence.
- `audiologging.h` — declares the audio logging categories: `qk4.audio` (`qk4Audio`), `qk4.audio.tx` (`qk4AudioTx`, TX wire-format diagnostic) and `qk4.audio.rx` (`qk4AudioRx`, RX playback level). The TX and RX ones are off by default because they scan every sample on the audio thread. Definitions live in `controllers/audiocontroller.cpp`.

## Threading

- `AudioEngine` lives on `AudioController::m_audioThread` (`moveToThread` right after construction). Its feed and mic-poll timers are its children, so they move with it. Both are 10 ms default (`Qt::CoarseTimer`) timers; the feed writes by bytes free, not by tick count, so a late tick does not drop audio.
- `OpusDecoder` is created on the main thread and keeps main-thread affinity, but it is only called from the lambda on `Protocol::audioDataReady`, whose context object is `Protocol`. That lambda runs on the I/O thread, so decoding is single-threaded on the I/O thread.
- `OpusEncoder` is created by `AudioEngine`'s constructor with no parent, so it keeps main-thread affinity when `AudioEngine` moves. `AudioEngine` calls it directly on the audio thread. No signals are involved.
- Both Opus wrappers are `QObject`s with no signals or slots, used off their affinity thread. Qt does not guarantee that calling a `QObject`'s functions from another thread works (Qt docs, "Threads and QObjects" → QObject Reentrancy). This is a known gap in the code, not a pattern to copy.
- `SidetoneGenerator` lives on `HardwareController::m_sidetoneThread` (`QThread::HighPriority`). Its pump timer is a `Qt::PreciseTimer`.

## RX path

K4 → Protocol (I/O thread) → OpusDecoder (I/O thread) → `AudioEngine::enqueueAudio` (mutex-protected) → feed timer (audio thread) → QAudioSink → speakers.

## TX path

Microphone → QAudioSource (at the device's own rate) → AudioDecimator (multiples of 12 kHz) or
AudioResampler (any other rate) → AudioEngine → OpusEncoder (EM2/EM3) or `RawAudioFormat` (EM0/EM1) →
`AudioEngine::txPacketReady` → `TcpClient::sendRaw` (queued to the I/O thread) → K4.

A microphone that cannot be used is reported on screen, not only in the log: a local PTT press keys
the K4 by streaming audio, so no microphone means a PTT button that does nothing.

`QAudioDevice::isFormatSupported()` cannot be trusted as proof a device will deliver: it returns true
for rates some devices produce nothing at. A keyed transmitter receiving no mic data for 500 ms is
reported under `qk4.audio` — that silence used to be indistinguishable from a quiet room.

The path stays Float32 end to end and quantises exactly once, at the wire format: EM0 to 24 bits, EM1 to 16,
and the Opus modes not at all. Quantising earlier (which is what it used to do, right after mic gain) cost
bits in proportion to the attenuation and was audible as tonal distortion on quiet audio.
`QT_LOGGING_RULES="qk4.audio.tx.debug=true"` reports what each TX frame actually put on the wire —
the only observer QK4 has for a format the radio alone reads.

## See also

- `docs/k4-protocol-quirks.md` → "`SL` (streaming latency) is not echoed" — the SL tier → frame
  bundling map, and why the tier cannot be read back from the radio.
