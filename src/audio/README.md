# audio/

RX and TX audio pipeline. Owned by `controllers/audiocontroller.cpp`.

## Files

- `audiodiagnostics.{cpp,h}` — the `qk4.audio.rx` (1 s RX levels) and `qk4.audio.tx` (per-transmission TX frame) diagnostics as plain records. The audio threads fill them into lock-free rings; `AudioController` drains and formats them on the main thread, so enabling the categories never puts a formatted log write on a real-time thread (CONVENTIONS.md Rule 16).
- `audioengine.{cpp,h}` — RX playback: `QAudioSink` with a jitter buffer. A 5 ms `Qt::PreciseTimer` feed writes whole packets into a sink sized to one SL-tier packet plus a 30 ms device margin; playback starts after one packet (`PREBUFFER_PACKETS = 1`, 20–120 ms of audio depending on SL). Counts sink underruns and dropped packets. Handles volume, balance and mix routing.
- `txaudiopipeline.{cpp,h}` — the TX path on its own thread ("AudioTX"): `QAudioSource` microphone capture, TCI transmit audio, conversion to 12 kHz, SL-tier framing, EM0–EM3 encoding (owns the `OpusEncoder`) and packetising. The microphone is opened at PTT-on when it is the TX source and closed at PTT-off, so the OS mic indicator is lit only while transmitting from it; a TCI transmission never opens it. Measures PTT → first packet once per transmission (`pttLatencyMeasured`, logged under `qk4.audio.tx` on the main thread).
- `spscring.h` — header-only, Qt-free. Fixed-capacity lock-free ring for one producer and one consumer, slots filled and read in place. Carries decoded RX packets from the I/O thread to the audio thread with no lock or allocation. Tested, including under ThreadSanitizer, in `tests/test_spscring.cpp`.
- `rxplayout.h` — header-only, Qt-free. RX sink size per SL tier (packet + 30 ms: 50/70/90/150 ms) and the jitter policy (trim the queue when queue + sink passes 5 packets, capped at 300 ms but at least 3 packets, back to 2 packets). Replaced a fixed 500 ms sink, in which a network burst sat out of the policy's reach and played up to half a second late. Tested, with a simulated network and device, in `tests/test_rxplayout.cpp`.
- `opusdecoder.{cpp,h}` — Opus decode wrapper. K4 sends 12kHz stereo (left=Main, right=Sub). All four encode modes normalise by one rule, `native × K4_GAIN_BOOST / full_scale`; the per-mode scales were measured 2026-09-19, not tuned by ear. See `docs/k4-protocol-quirks.md` §11.
- `opusencoder.{cpp,h}` — Opus encode wrapper for TX. 12kHz mono; frame size reconfigured per K4 SL tier. `encodeFloat()` is what the TX path uses — Opus takes float natively, so the Opus modes never quantise.
- `rawaudioformat.h` — header-only, Qt-free. Builds the EM0 (S32LE, 24-bit) and EM1 (S16LE) TX wire payloads from a captured **Float32** mono frame. Holds `EM0_FULL_SCALE` (2^23 — EM0 is 24-bit in a 32-bit container), which `OpusDecoder::NORMALIZE_K4_RAW` is derived from so the two directions of EM0 cannot drift apart. See the WHY block for AUD-003 and the 2026-09-19 calibration.
- `audiodeviceselection.h` — header-only, Qt-free. Decides which device a stream belongs on when the device list changes. Follows the system default when nothing is pinned, and claims a PINNED device the moment it reappears — a pinned device absent at startup falls back silently, and without this the stream never returned to it.
- `audiodecimator.h` — header-only, Qt-free. Decimates a captured mic stream to the K4's 12 kHz. The factor comes from the rate the DEVICE reports, not one we demand: asking for a fixed 48 kHz produced zero bytes from an AirPods Pro while Qt reported the format as supported. See its WHY block (INT-005).
- `audioresampler.h` — header-only, Qt-free. Polyphase rational resampler for the capture rates `AudioDecimator` cannot take (44.1, 22.05, 16, 11.025, 8 kHz…). Before it, those microphones were refused and PTT keyed nothing (#180). Integer rates deliberately stay on `AudioDecimator` so their sound does not change. Tested in `tests/test_audioresampler.cpp`.
- `micrate.h` — header-only, Qt Core only. The wording the operator sees on screen (via `TxAudioPipeline::micUnavailable`) when no microphone can be used, plus `isUsable()`, the same accept rule `setupAudioInput()` applies. Records why a mid-session rate change needs a restart. Tested in `tests/test_micrate.cpp`.
- `rxoutputstage.h` — header-only, Qt-free. The last stage of RX playback: the 0 to +12 dB boost above the MAIN/SUB sliders (Options → Audio Output) and the soft limiter used when it is above 0 dB. Tested in `tests/test_rxoutputstage.cpp`.
- `sidetonegenerator.{cpp,h}` — Real-time CW sidetone synthesis at 48kHz on its own audio sink. Already places its edges with `SidetoneScheduler`; phase 2 of `docs/cw-keyer-port-plan.md` moves it into a mixer stage in the RX output.
- `sidetonescheduler.{cpp,h}` — Sample-accurate sidetone edge placement, ported from K4-Companion (GPL-3.0): the keyer schedules "key down at T", the output callback places it on an exact frame against the block's play-out time. Delay fixed at the first edge (lead + block), never ratcheted by non-audio lateness, eased 1 ms/s in silence.

## Threading

- `TxAudioPipeline` lives on `AudioController::m_txThread` ("AudioTX"). Opening a microphone is synchronous and can take over a second (device enumeration, `QAudioSource::start()`, an OS permission prompt or Bluetooth profile switch); on the RX thread that stalled the speaker for as long. The K4 is half-duplex, so on its own thread it delays only the transmission it belongs to. Its mic poll is a 10 ms default timer.
- `AudioEngine` lives on `AudioController::m_audioThread` (`moveToThread` right after construction). Its feed timer is a child, so it moves with it. The feed is a 5 ms `Qt::PreciseTimer` (the small sink depends on it). The feed writes by bytes free, not by tick count, so a late tick does not drop audio.
- `OpusDecoder` and `OpusEncoder` are plain classes, not `QObject`s, so neither has a thread affinity to get wrong. The decoder is owned by the handler on `Protocol::audioDataReady` (context `Protocol`, so it runs on the I/O thread) and lives exactly as long as that connection; `~AudioController` disconnects it on the I/O thread, so no decode can be in flight afterwards. The encoder is a member of the TX pipeline, by value, used only on the thread that pipeline runs on.
- `SidetoneGenerator` lives on `HardwareController::m_sidetoneThread`, promoted to real-time by `RtScheduling::promoteCurrentThread` (see `src/utils/README.md`). Its pump timer is a `Qt::PreciseTimer`.

## RX path

K4 → Protocol (I/O thread) → OpusDecoder (I/O thread) → `AudioEngine::enqueueAudio` (copies into a lock-free SPSC ring slot) → feed timer (audio thread: jitter policy, mix in place, write) → QAudioSink → speakers. No mutex on the path; MX routing is one packed atomic.

## TX path

Microphone → QAudioSource (at the device's own rate, open only while transmitting from it) → AudioDecimator (multiples of 12 kHz) or
AudioResampler (any other rate) → TxAudioPipeline (TX thread) → OpusEncoder (EM2/EM3) or `RawAudioFormat` (EM0/EM1) →
`TxAudioPipeline::txPacketReady` → `TcpClient::sendRaw` (queued to the I/O thread) → K4.

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
