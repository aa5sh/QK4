# ui/dialogs/

Top-level auxiliary windows. Most are non-blocking, but they are full windows or full-area overlays rather than inline widgets.

## Files

- `optionsdialog` — the **Options** window (opened from Tools → Settings…). Hosts the `ui/pages/` tab pages; `optionsdialog.cpp` adds them in order.
- `radiomanagerdialog` — the **Server Manager**: saved K4 connections, host/port/password, "Use TLS (Encrypted)", encode mode and streaming latency. Auto-discovery uses `K4Discovery` (`network/`), a UDP broadcast probe on the local network. It is not mDNS.
- `macrodialog` — the macro editor (PF1–4 / F1–12). A `QWidget` overlay that `PopupManager` sizes to cover the spectrum area.
- `textdecodewindow` — floating window showing the K4's own on-radio decoder output (CW / AFSK / FSK / PSK / DATA / SSB). One per receiver (Main / Sub). Not a `K4PopupBase` subclass; the header says why.

## Pattern

1. Dialogs are created on first open. `OptionsDialog` is then kept for reuse: `m_optionsDialog` on MainWindow is `nullptr` until the Settings action's lambda creates it. The Server Manager is the exception: `MainWindow::showRadioManager()` creates it on each open and deletes it on close.
2. Use `K4Styles::Dialog::*` for every label/combo/line-edit styling — never inline `QString("color: %1; ...")`.
3. Persist any user-edited state via `RadioSettings` (`settings/`).

## See also

- `ui/pages/README.md` — tab pages hosted by OptionsDialog.
- `settings/radiosettings.h` — persistence.
