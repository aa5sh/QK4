# utils/

Shared helper functions. Any helper needed in more than one translation unit goes here — Architecture Rule 1.

## Files

- `radioutils.{cpp,h}` — `RadioUtils::` namespace. Frequency → band (11 bands + gaps), band edges, tuning step tables, span-dial stepping with K4 quirks, and `ditMsForWpm()` — the single 1200/WPM conversion and keyer-speed clamp shared by the iambic keyer's element clock and the sidetone's block length. Four copies of that arithmetic had drifted apart before it landed here. Also: filter preset and edge stepping, EQ command building, SL tier → frame size and jitter-buffer targets, fixed-tune mode mapping, AGC and TX-bandwidth labels, frequency snapping and host/IPv4 validation. `radioutils.h` is the list.
- `bandplan.{cpp,h}` — `BandPlan::` namespace. Segment lookup (CW / data / beacon / phone) per IARU region, driving the panadapter band-plan overlay. Region 4 (`RegionUS`) is the US FCC layout with practical Extra-class mode edges. Covered by `test_bandplan`.
- `windowfit.h` — `WindowFit::` namespace. Main-window sizing rules: the size the window opens at (never below its layout's minimum) and the interface scale at which it fits a screen — shrinking on a small screen, growing to 125% on a large one with room for it (no step between 100% and 125%). Pure arithmetic on `QSize`, covered by `test_windowfit`.
- `wheelaccumulator.{cpp,h}` — `WheelAccumulator` class. Accumulates `QWheelEvent` angle deltas into discrete steps (optionally per key, e.g. per modifier), so a trackpad does not fire one tuning step per small delta.
- `macroids.h` — `MacroIds::` namespace. `QString` constants naming every macro slot (Startup, PF1–PF4, Fn.F1–F8, K-pod buttons, Keyboard-F1–F12, built-in functions) and the forbidden-startup-command check.

## Rule 1 — No duplicated static functions

If you catch yourself copy-pasting a helper into a second `.cpp` file, promote it to `utils/` with a namespace. Copy-pasting is a defect; fix it immediately.

## Shape

Most utilities are free functions in a namespace, with no state and no side effects. Two exceptions: `WheelAccumulator` is a small stateful class, and it and `windowfit.h` use QtGui types (`QWheelEvent`, `QSize`, `QRect`). Nothing here depends on widgets.

## See also

- `CONVENTIONS.md` → Architecture Rule 1.
- `docs/k4-protocol-quirks.md` → "Span dial skips 6 kHz going up" — the K4 behaviour the span
  helpers model, including the one case they do not yet match.
