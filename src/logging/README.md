# src/logging/

QK4's diagnostic logging. Links against `Qt6::Core` only, so every piece is testable without a GUI.

## Files

- `logcategories.{h,cpp}` — the one declaration and definition of every logging category. Every
  category is defined at `QtInfoMsg`: info and warnings print, debug is off until a rule turns it
  on. `Logging::allCategories()` lists them for tests and the Options page.

## Category names

One scheme: `qk4.<area>[.<detail>]`, lower case, dots only. C++ symbols are `lc<Area><Detail>`.
Add a category here, never with a local `Q_LOGGING_CATEGORY` (`LogCategoriesTests` fails the build).

| Category | Symbol | Covers | Before 2026-10-29 |
|---|---|---|---|
| `qk4.app` | `lcApp` | startup, fonts, TLS backend, command line, interface scale | uncategorised |
| `qk4.ui` | `lcUi` | MainWindow | `qk4.main` |
| `qk4.ui.menu` | `lcUiMenu` | K4 menu overlay | `qk4.menu` |
| `qk4.ui.bandnav` | `lcUiBandNav` | band navigation | `qk4.bandnav` |
| `qk4.ui.modepopup` | `lcUiModePopup` | mode popup | `qk4.modepopup` |
| `qk4.ui.textdecode` | `lcUiTextDecode` | text decode window | `qk4.textdecode` |
| `qk4.ui.macro` | `lcUiMacro` | macros and the macro dialog | `qk4.macro`, `ui.macro` |
| `qk4.link` | `lcLink` | connection lifecycle (ConnectionController) | `qk4.connection` |
| `qk4.link.tcp` | `lcLinkTcp` | TCP, TLS-PSK, authentication, ping | `net.tcp` |
| `qk4.link.discovery` | `lcLinkDiscovery` | K4 discovery by UDP broadcast | `K4Discovery` |
| `qk4.tx.owner` | `lcTxOwner` | transmit arbiter | `tx.owner` |
| `qk4.cat.tx` | `lcCatTx` | every CAT command sent to the K4 | `CAT.TX` |
| `qk4.cat.rx` | `lcCatRx` | every CAT response from the K4 | `CAT.RX` |
| `qk4.audio` | `lcAudio` | audio devices, codecs, pipeline | `qk4.audio` |
| `qk4.audio.rx` | `lcAudioRx` | RX playback levels, once a second | `qk4.audio.rx` |
| `qk4.audio.tx` | `lcAudioTx` | what each TX frame put on the wire | `qk4.audio.tx` |
| `qk4.audio.sidetone` | `lcAudioSidetone` | sidetone timing per tone onset | `qk4.audio.sidetone` |
| `qk4.cw.keyer` | `lcCwKeyer` | CW orchestration (CwController) | `cw.keyer` |
| `qk4.cw.halikey` | `lcCwHalikey` | HaliKey device and V1.4 serial worker | `hw.halikey` |
| `qk4.cw.midi` | `lcCwMidi` | HaliKey MIDI worker | `hw.midi` |
| `qk4.hardware` | `lcHardware` | HardwareController | `qk4.hardware` |
| `qk4.kpod` | `lcKpod` | KPOD (hidapi) | `hw.kpod` |
| `qk4.kpod.udev` | `lcKpodUdev` | KPOD hotplug on Linux | `hw.kpod.udev` |
| `qk4.kpod.plus` | `lcKpodPlus` | KPOD+ (libusb), including EP02 `KZ@` | `hw.kpodplus` |
| `qk4.catserver` | `lcCatServer` | CAT server for WSJT-X and friends | `net.cat` |
| `qk4.catserver.push` | `lcCatServerPush` | CAT server auto-information push | `net.catpush` |
| `qk4.tci` | `lcTci` | TCI server | `net.tci` |
| `qk4.tci.ws` | `lcTciWs` | TCI WebSocket transport | `net.ws` |
| `qk4.dxcluster` | `lcDxCluster` | DX cluster clients | `net.dxcluster` |
| `qk4.kpa1500` | `lcKpa1500` | KPA1500 client | `net.kpa` |
| `qk4.kpa1500.ui` | `lcKpa1500Ui` | KPA1500 UI controller | `qk4.kpa1500` |
| `qk4.spectrum` | `lcSpectrum` | SpectrumController | `qk4.spectrum` |
| `qk4.spectrum.render` | `lcSpectrumRender` | panadapter renderer | `qk4.dsp` |
| `qk4.spectrum.minipan` | `lcSpectrumMiniPan` | mini-pan renderer | `dsp.minipan` |
| `qk4.spectrum.diag` | `lcSpectrumDiag` | waterfall tier and geometry diagnostics | `qk4.pan.diag` |

## Turning debug on

`QT_LOGGING_RULES` works as Qt documents it. Separate rules with `;`. A trailing `*` matches the
category's children but not the category itself, so name both:

```bash
QT_LOGGING_RULES="qk4.audio.debug=true;qk4.audio.*.debug=true" QK4
```

`QT_LOGGING_DEBUG=1` makes Qt report where it loaded its rules from.
