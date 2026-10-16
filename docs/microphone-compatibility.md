# Microphone sample rates and PTT

## Root cause and protocol

The supplied tests establish a working baseline at 24 kHz mono Float with AirPods:
ownership, capture, EM3 frames of 720 samples, then inbound `TX;`/`TQ1;`;
release stops streaming and the radio reports `RX;`/`TQ0;`.
Bose at 16 kHz and MacBook Air at 44.1 kHz acquire ownership but fail the explicit
integer-multiple-of-12000 check in `AudioEngine::setupAudioInput()`. No microphone
frames can be produced. These logs and the code identify capture initialization
as the failure, rather than an unrelated radio restriction.

The PTT button in mainwindow.cpp engages `Route::StreamedFromHere`.
`TransmitOwner` opens the audio gate without sending CAT `TX;`: the tunnel audio
header keys the K4. `AudioEngine::encodeAndSendFrame()` retains the existing EM3
encoder and Protocol packet builder; `txPacketReady` reaches TcpClient::sendRaw
through AudioController. Stopping streaming unkeys this route. Inbound CAT `TX;`
is a report, not evidence that an outbound CAT command is needed. XMIT's separate
CAT-keyed route remains intact.

## Implementation

Capture still requests the preferred device rate in mono Float, preserving the
proven AirPods 24 kHz request. If that representation is unsupported, capture uses
the preferred native PCM format. After start, QAudioSource::format() determines
the conversion rate, channels and PCM interpretation. This describes the PCM Qt
delivers; Qt does not expose a separate hardware/backend conversion rate here.
Mono and stereo UInt8, Int16, Int32 and Float PCM are normalized using Qt; stereo
is averaged. Incomplete PCM frames carry across reads.

MicResampler is a causal polyphase Blackman-windowed sinc FIR with unity DC gain,
a 5 kHz cutoff and approximately 2 ms group delay. Its filter length scales with
capture rate (97 taps at 24 kHz, 193 at 48 kHz). Integer phase accounting preserves
fractional positions and gives exactly floor(N * 12000 / captureRate) output
samples for N input frames, regardless of read boundaries. Filter history stays
continuous while the microphone remains open across PTT edges. 12 kHz passes
through directly. Coefficients are precomputed when opening; runtime uses about
2.3 million multiply-accumulates/second at 48 kHz. Filtering replaces the old
box average, so the high-frequency response improves rather than remaining
bit-identical. Capture rate, framing, encoding and the PTT mechanism are preserved.

No new dependency: the existing libopus API does not expose a general resampler;
Qt Multimedia negotiates PCM but provides no public streaming converter for this
use. The project's AudioUpsampler handles integer interpolation for TCI RX only,
not rational microphone downsampling. Its windowed-sinc approach is also suitable
here, with a separate converter to avoid changing RX or digital/TCI behavior.

Capture initialization errors and unexpected stopped/suspended streams close the
audio gate before signalling ownership cleanup. A stream delivering no bytes
warns after 500 ms and fails closed after 2 seconds while keyed. Streamed PTT
ends by stopping audio; CAT-keyed XMIT cleanup also sends RX through the existing
arbiter. Ordinary PTT release keeps the microphone open as before.

## Verification and hardware status

Automated tests cover 12/16/24/44.1/48 kHz, arbitrary chunk boundaries, bit-identical
chunked versus contiguous conversion, one minute of exact cumulative sample
counts, 1 kHz amplitude/periodicity, bounded tone peaks, rejection of a 7 kHz
aliasing tone, stereo Int16 capture split within PCM frames, and gate/ownership
cleanup after injected failures for streamed and CAT-keyed routes.

| Device | Supplied pre-fix result | Post-fix hardware status |
| --- | --- | --- |
| Bose QC, 16 kHz | Capture rate rejected; no TX | Needs hardware retest |
| MacBook Air built-in, 44.1 kHz | Capture rate rejected; no TX | Needs hardware retest |
| AirPods, 24 kHz | Working EM3/PTT/TX/RX baseline | Needs regression retest |

No physical microphone or K4 is available in the execution environment. Synthetic
24 kHz tests verify conversion, not Bluetooth negotiation or over-the-air audio.
For each device, select it in Audio Input, use a dummy load, press/release PTT
several times and confirm actual-format/resampler logs, the first TX frame, radio
TX/RX reports and ownership release. Compare AirPods startup timing and voice
quality to the baseline. While keyed, disconnect the microphone and verify the
gate closes, ownership clears and the radio returns to RX; reconnect and re-key.

Remaining limits: one or two channels, capture rates 12–192 kHz, and Qt's supported
PCM formats. Bluetooth HFP/A2DP routing, permissions, backend bugs and devices
that falsely advertise working formats can still prevent capture. Such failures
are diagnosed and stopped; resampling cannot repair missing input. Tone tests
quantify signal quality but do not replace listening on the three devices.

Validated in Linux with Qt 6.10.2, GCC 15, Release:

```
cmake -S . -B /tmp/qk4-mic-build -DQK4_TESTS_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/qk4-mic-build --target test_micresampler test_miccapture test_transmitowner test_audiodecimator test_opusdecoder test_protocol -j 4
ctest --test-dir /tmp/qk4-mic-build -R '^(MicResamplerTests|MicCaptureTests|TransmitOwnerTests|AudioDecimatorTests|OpusDecoderTests|ProtocolTests)$' --output-on-failure
```

All six suites passed. Capture tests exercise all five rates through the actual
AudioEngine EM3 encoder/packet signal at 720 samples per frame and verify no
further packets after failure. Tone residual versus a delayed ideal sine is below
0.001 RMS. Both changed controller translation units also passed GCC syntax
checks with the Qt/Opus include flags. The full GUI application was not built;
macOS/CoreAudio and physical device validation remain outstanding.

## Changed files

- `src/audio/micresampler.h`: streaming FIR converter.
- `src/audio/audioengine.cpp`, `src/audio/audioengine.h`: format selection, PCM
  decoding, resampling, capture diagnostics and failure shutdown.
- `src/controllers/audiocontroller.cpp`, `src/controllers/audiocontroller.h`:
  forward capture failure to the main thread.
- `src/controllers/transmitcontroller.cpp`: release microphone ownership on
  failure; log radio TX/RX changes.
- `CMakeLists.txt`, `tests/CMakeLists.txt`: register the header and focused tests.
- `tests/test_micresampler.cpp`, `tests/test_miccapture.cpp`: DSP and capture tests.
- `docs/microphone-compatibility.md`: investigation, validation and hardware checklist.
