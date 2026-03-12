#ifndef PANADAPTER_CONSTANTS_H
#define PANADAPTER_CONSTANTS_H

// Shared rendering constants for PanadapterRhiWidget and MiniPanRhiWidget.
// Colors and theme values live in K4Styles; these are GPU rendering parameters.

namespace PanadapterConstants {

// RTTY shift (Hz) — fixed at 170 Hz for standard amateur RTTY.
// FSK Mark-Tone is user-configurable from K4 front panel (default 915 Hz);
// the shift between Mark and Space is always 170 Hz.
constexpr float RttyShiftHz = 170.0f;
constexpr float RttyHalfShiftHz = RttyShiftHz / 2.0f;

// Line widths (pixels)
constexpr float MarkerLineWidth = 2.0f;
constexpr float RttyDashLineWidth = 1.5f;
constexpr float PassbandEdgeWidth = 2.0f;

// Dash pattern (pixels)
constexpr float DashLengthPx = 6.0f;
constexpr float DashGapPx = 4.0f;

// Passband alpha (0–255)
constexpr int PassbandFillAlpha = 64;
constexpr int PassbandEdgeAlpha = 180;

// MiniPan passband alpha — higher than main pan for visibility at small scale
constexpr int MiniPanFillAlpha = 100;

// MiniPan span Hz — Narrow used for CW/FSK/AFSK, Wide for all others
constexpr int SpanNarrowHz = 2000;
constexpr int SpanWideHz = 10000;

} // namespace PanadapterConstants

#endif // PANADAPTER_CONSTANTS_H
