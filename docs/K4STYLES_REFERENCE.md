# K4Styles Reference

Complete reference for `K4Styles::Colors`, `K4Styles::Dimensions`, `K4Styles::Fonts`, and the helper functions declared under `src/ui/styling/`.

**Source of truth:**
- `src/ui/styling/k4constants.h` — `K4Styles::Colors::*` and `K4Styles::Dimensions::*` (lightweight, no Qt includes).
- `src/ui/styling/k4styles.h` — `K4Styles::Fonts::*`, free-function stylesheet helpers, `K4Styles::Dialog::*` cached helpers.

The split exists so files that touch only constants (panadapter renderers, hardware modules) don't pull in `<QPainter>` / `<QFont>` / `<QLinearGradient>`. `k4styles.h` includes `k4constants.h`, so `K4Styles::Colors::*` and `K4Styles::Dimensions::*` remain reachable from anywhere that includes either header.

Keep this doc in sync when you add a constant or helper. Quick cross-check:

```bash
rg 'constexpr const char \*' src/ui/styling/k4constants.h   # all color constants (Alpha:: are ints)
rg '^constexpr int' src/ui/styling/k4constants.h            # all dimension constants
rg '^\s*QString\s+\w+\(' src/ui/styling/k4styles.h          # free-function stylesheet helpers
rg '^\s*const QString &\w+' src/ui/styling/k4styles.h       # Dialog:: cached-QString helpers
rg 'constexpr const char \*' src/ui/styling/k4styles.h      # font-family constants (Fonts::Primary, Fonts::Data)
```

---

## Colors (`K4Styles::Colors::*`)

Every hex value is written **once**; a second name for the same colour aliases the first (`HoverMid1 = GradientTop`).
`TokenStandardTests` fails on a repeated hex. Contrast ratios are WCAG 2.x against `Background` (`#1a1a1a`) unless
another surface is named.

### Base Neutrals

| Constant | Value | Usage |
|----------|-------|-------|
| `White` | `#FFFFFF` | Lit fills (active menu-bar button, AUTO on), strokes over the spectrum with an `Alpha::` |
| `Black` | `#000000` | Halos, shadows, scrims with an `Alpha::`; clear colour before the mini-pan renders |

### Surfaces

| Constant | Value | Usage |
|----------|-------|-------|
| `DarkBackground` | `#0d0d0d` | Dialogs, inputs, mini-pan clear |
| `SpectrumBackground` | `#141414` | Panadapter GPU clear colour |
| `Background` | `#1a1a1a` | Main window |
| `PopupBackground` | `#1e1e1e` | Floating popups, one step raised |

### Text

| Constant | Value | Contrast | Usage |
|----------|-------|----------|-------|
| `TextPrimary` | = `White` | 17.4:1 | Primary text, button labels |
| `TextSecondary` | `#999999` | 6.11:1 | Labels, units, idle values (RIT/XIT off), status-bar text, dimmed-but-meaningful indicators |
| `TextSecondaryOnRaised` | `#AAAAAA` | 4.60:1 on the button face, 4.83:1 on `OverlayNavButton` | Secondary text drawn on buttons and nav keys (auto value, EDIT, NORM, empty EQ preset) |
| `TextDisabled` | `#666666` | 3.03:1 (exempt) | Disabled controls **only** — WCAG 1.4.3 does not apply to inactive components |
| `TextOnLight` | `#333333` | 12.6:1 on `White`, 6.37:1 on `SelectedBottom` | Text on selected / lit fills |
| `TextOnAccent` | = `Black` | 15.3:1 on `StatusGreen`, 4.69:1 on `OverlayBackground` | Text on saturated fills (B SET, SUB/DIV on, VFO A/B badges and squares) |
| `TextRed` | `#FF6666` | 6.09:1 | All red text: over-S9 scale, errors, warnings, the transmitting TX label and its arrows, TEST, the error `K4`, critical temperature/voltage, KPA1500 FAULT |

`TxRed` is never text (4.35:1) and never under white text (4.0:1); `TxActiveFill` is the red fill that carries text.
A view whose selection fill is `AccentAmber` also sets `selection-color: DarkBackground` (10.6:1); left unset, the
selected item takes the palette's HighlightedText, white, at 1.83:1. Pressed nav keys (NORM, EDIT) brighten their text to
`TextPrimary`, because `TextSecondaryOnRaised` on `OverlayNavButtonPressed` is 3.40:1.

The "button face" is `GradientTop` → `GradientMid1` at 0.75, the lightest point behind
the glyph band of a gradient button.

### Accent and Status

| Constant | Value | Usage |
|----------|-------|-------|
| `AccentAmber` | `#FFB000` | TX indicators, titles, highlights (9.5:1) |
| `VfoACyan` | `#00BFFF` | VFO A: square, passband, markers, overlays |
| `StatusGreen` | `#00FF00` | Active/on/connected status indicators |
| `VfoBGreen` | = `StatusGreen` | VFO B theme |
| `TxRed` | `#FF0000` | LEDs, meters, markers — never text, never under text |
| `TxActiveFill` | `#E00000` | PTT key while transmitting; white on it is 5.04:1 (`#EE0000` is the lightest pure red that passes, 4.53:1) |

### Meter Gradient Colors (S/Po Meter) — data colours

| Constant | Value | Usage |
|----------|-------|-------|
| `MeterGreenDark` | `#00CC00` | Low signal (green start) |
| `MeterGreen` | = `StatusGreen` | Normal signal range |
| `MeterYellowGreen` | `#CCFF00` | Transition to yellow |
| `MeterYellow` | `#FFFF00` | Moderate signal |
| `MeterOrange` | `#FF6600` | High signal |
| `MeterOrangeRed` | `#FF3300` | Approaching max |
| `MeterRed` | = `TxRed` | Peak/overload signal |
| `MeterIdDark` | `#2E7D32` | Id meter low range |
| `MeterIdLight` | `#66BB6A` | Id meter high range |

### Borders and Non-text Strokes

| Constant | Value | Usage |
|----------|-------|-------|
| `DialogBorder` | = `TextOnLight` (`#333333`) | Dialog borders, separators, slider grooves, menu-bar hover |
| `PanelBorder` | `#444444` | Frame around panadapter / VFO panels |
| `BorderDisabled` | = `PanelBorder` | Disabled checkbox-button border (was `#404040`) |
| `DisabledBackground` | = `PanelBorder` | Disabled fills |
| `BadgeOffBackground` | = `GradientBottom` (`#2a2a2a`) | SUB/DIV badge when off; `TextSecondary` on it is 5.04:1 (3.42:1 on the old `#444444`) |
| `BorderNormal` | `#606060` | Default button border |
| `MutedGraphic` | = `TextDisabled` (`#666666`) | Idle frames, ticks, unlit bars, unknown-state glyphs, input borders — 3.03:1 meets the 3:1 non-text rule (`#606060` would be 2.8:1) |
| `BorderHover` | `#808080` | Button border on hover |
| `BorderPressed` | `#909090` | Button border when pressed |
| `BorderSelected` | = `TextSecondaryOnRaised` (`#AAAAAA`) | Selected/active button border |

### Overlay Panel Colors (Menu, Macros, full-screen overlays)

The five near-blacks below carry a slight blue cast. They are kept apart from the neutral surfaces on purpose
until the maintainer decides; see *Bluish overlay surfaces* below.

| Constant | Value | Usage |
|----------|-------|-------|
| `OverlayContentBg` | `#18181C` | Main content area background |
| `OverlayHeaderBg` | `#222228` | Header bar + nav panel background |
| `OverlayColumnHeaderBg` | `#1E1E24` | Column header background |
| `OverlayItemBg` | `#19191E` | Unselected item row background |
| `OverlayNavButton` | `#3A3A45` | Nav button normal state |
| `OverlayNavButtonPressed` | `#505060` | Nav button pressed state |
| `OverlayDivider` | `#28282D` | Divider lines between items |
| `OverlayDividerLight` | `#3C3C41` | Lighter divider (demarcation) |
| `SelectionLight` | `#DCDCDC` | Menu selection, light zone |
| `SelectionDark` | `#505055` | Menu selection, dark zone |
| `OverlayBackground` | `#777777` | VFO A/B badges on the waterfall (was `#707070`: black text was 4.24:1) |

#### Bluish overlay surfaces

| Value | Name | Nearest neutral | Used by |
|-------|------|-----------------|---------|
| `#18181C` | `OverlayContentBg` | `#1a1a1a` Background | MenuOverlay and MacroDialog content area |
| `#19191E` | `OverlayItemBg` | `#1a1a1a` Background | Menu and macro item rows |
| `#1E1E24` | `OverlayColumnHeaderBg` | `#1e1e1e` PopupBackground | Macro column header |
| `#222228` | `OverlayHeaderBg` | `#222222` (none) | Menu/macro header bar and nav panel |
| `#28282D` | `OverlayDivider` | `#2a2a2a` GradientBottom | Row dividers |

### Button Gradient Stops

| Stop | Normal | Hover | Light | Selected | Selected hover | Disabled |
|------|--------|-------|-------|----------|----------------|----------|
| 0 (Top) | `GradientTop` `#4a4a4a` | `HoverTop` `#5a5a5a` | `LightGradientTop` `#747474` | `SelectedTop` `#E0E0E0` | `SelectedHoverTop` `#F0F0F0` | `DisabledGradientTop` = `GradientMid1` |
| 0.4 (Mid1) | `GradientMid1` `#3a3a3a` | `HoverMid1` = `GradientTop` | `LightGradientMid1` `#6E6E6E` | `SelectedMid1` `#D0D0D0` | `SelectedHoverMid1` = `SelectedTop` | `DisabledGradientMid1` = `GradientBottom` |
| 0.6 (Mid2) | `GradientMid2` `#353535` | `HoverMid2` `#454545` | `LightGradientMid2` `#6a6a6a` | `SelectedMid2` `#C8C8C8` | `SelectedHoverMid2` `#D8D8D8` | `DisabledGradientMid2` `#252525` |
| 1 (Bottom) | `GradientBottom` `#2a2a2a` | `HoverBottom` = `GradientMid1` | `LightGradientBottom` = `BorderNormal` | `SelectedBottom` `#B8B8B8` | `SelectedHoverBottom` = `SelectedMid2` | `DisabledGradientBottom` = `Background` |

The light stops carry white text, so each is checked on its own rather than at the glyph band: two-line labels
(ATU TUNE) reach the top edge, and the pressed state reverses the gradient. White on `LightGradientTop` is 4.67:1
(3.54:1 on the old `#888888`).

### Feature Colours

| Constant | Value | Usage |
|----------|-------|-------|
| `ErrorBgDark` | `#331111` | Error toast background (with `TextRed`, 5.7:1) |
| `FilterIndicatorGold` | `#FFD040` | Filter-edge indicator; brighter than `AccentAmber` against the amber passband. `FilterIndicatorWidget` only |
| `ChartSeriesRtt` / `Jitter` / `Buffer` | = `VfoACyan` / `AccentAmber` / `MeterIdLight` | NetHealthPopup traces |
| `BandPlanCw` `Data` `Phone` `Beacon` `All` | `#CB3B45` `#E0852F` `#6E83A6` `#AB47BC` `#90A4AE` | Band-plan segment fills (data colours) |
| `BandPlanOutOfBand` | = `GradientMid1` | Past the band edge, under the hatch |
| `BandPlanTag` | = `DarkBackground` | Band-name tag (was `#0F0F0F`) |

### Alpha (`K4Styles::Colors::Alpha::*`, 0–255)

Translucency is a named alpha applied to a token, never a literal `QColor(r, g, b, a)` or `rgba()`:
`K4Styles::withAlpha(Colors::Black, Alpha::Halo)` for QPainter, `K4Styles::cssRgba(...)` for style sheets.

| Constant | Value | Usage |
|----------|-------|-------|
| `Halo` | `153` | `drawHaloText` ring and axis-tick halo |
| `SpanButtonScrim` / `SpanButtonHover` | `153` / `204` | Span +/- buttons over the spectrum (`Black` / `GradientTop`) |
| `LabelScrim` | `210` | Mini-pan ±kHz labels (was 160: 2.2:1 over a white pixel, now 4.8:1) |
| `CloseButtonHover` | `51` | Text-decode close button hover |
| `BandPlanSegment` / `BandPlanOutOfBand` | `120` / `200` | Band-plan fills |
| `BandPlanHatch` / `BandPlanDivider` / `BandPlanMarker` / `BandPlanCaret` | `40` / `70` / `220` / `235` | White strokes in the band-plan strip |
| `PassbandFill` | `64` | Secondary VFO passband |
| `CardTint` / `CardBorder` | `18` / `35` | KPA1500 card: white tint and outline |
| `LcdBorder` / `LcdShade` | `20` / `40` | KPA1500 status box: white outline, black fill |

### Removed Names

These no longer exist; for reading older code: `TextWhite` → `TextPrimary`, `TextGray` / `TextFaded` →
`TextSecondary`, `TextDark` → `TextOnLight`, `ErrorRed` → `TextRed`, `InactiveGray` → `TextSecondary` where it was
informative text, `MutedGraphic` where it was a border or tick, `TextDisabled` where the control was disabled.

---

## Dimensions (`K4Styles::Dimensions::*`)

### Border & Radius

| Constant | Value | Usage |
|----------|-------|-------|
| `BorderWidth` | `2` | Standard button border width (px) |
| `BorderRadius` | `6` | Standard button corner radius (px) |
| `BorderRadiusLarge` | `8` | Large button/popup corner radius (px) |

### Shadow (Popup Widgets)

| Constant | Value | Usage |
|----------|-------|-------|
| `ShadowRadius` | `16` | Shadow blur radius (px) |
| `ShadowOffsetX` | `2` | Shadow horizontal offset (px) |
| `ShadowOffsetY` | `4` | Shadow vertical offset (px) |
| `ShadowMargin` | `20` | Space around popup for shadow (`ShadowRadius + 4`) |
| `ShadowLayers` | `8` | Number of blur layers for soft shadow |

### Button Heights

| Constant | Value | Usage |
|----------|-------|-------|
| `ButtonHeightLarge` | `44` | Menu overlay nav buttons (px) |
| `ButtonHeightMedium` | `36` | Bottom menu bar, popup buttons (px) |
| `ButtonHeightSmall` | `28` | Function buttons in side panels (px) |
| `ButtonHeightMini` | `24` | Compact toggle buttons (MON, NORM, BAL) (px) |

### Side-Panel Button Geometry

One standard for the left and right panels, the memory row and the KPA1500 mini-panel. Light and dark
buttons share all of it and differ only in colour. Build buttons with `SidePanelButton`
(`src/ui/widgets/sidepanelbutton.h`), never by hand: `create()` (fixed size, sheet,
`WA_LayoutUsesWidgetRect`), `createWithSubLabel()`, `createSubLabel()` / `subLabelHeight()` (one line of
the sub-label font, measured with `QFontMetricsF`), `createGrid()` (two equal columns, add it with
`Qt::AlignHCenter`). Checked by `SidePanelButtonStandardTests` in `tests/test_sidecontrolpanel.cpp`.

| Constant | Value | Usage |
|----------|-------|-------|
| `SideButtonHeight` | `28` (= `ButtonHeightSmall`) | Every side-panel key; two lines of 10 px text inside the 2 px borders |
| `SideButtonWidth` | `57` | Function keys; the narrowest column every sub-label fits (DIVERSITY) |
| `SideButtonColumnGap` | `4` | Between keys side by side (grids, memory row, MON/NORM/BAL, icon row) |
| `SideButtonSubLabelGap` | `4` | Key bottom edge to its orange sub-label (also M1–M4 to MESSAGE) |
| `SideButtonRowGap` | `12` | Sub-label (or key, where none) to the next row's key |
| `SideButtonGridWidth` | `118` | `2 × SideButtonWidth + SideButtonColumnGap` |
| `SidePanelMarginH` / `SidePanelMarginV` | `6` / `8` | Panel edge to content; `MarginH` is also the KPA1500 card's inset |
| `SidePanelItemSpacing` | `4` | Between consecutive items of one group |
| `SidePanelGroupGap` | `16` | Between groups (TX grid, each knob pair, KPA1500 slot) |
| `SidePanelMinorGap` | `8` | Between rows of one block (MON row, sliders, icon row) |
| `SidePanelSectionGap` | `32` | Right panel: PF keys to the VFO keys below |
| `SidePanelInnerGap` | `4` | Inside a painted card: label → bar → value, text to the status-box edge |
| `SideCompactButtonHeight` | `24` | MON / NORM / BAL |
| `SideCompactBorderWidth` / `SideCompactBorderRadius` | `1` / `4` | `compactButton()` border; status-box radius |
| `SideIconSize` / `SideIconInset` | `24` / `2` | ? and globe glyph, and its clearance inside the border |
| `SideIconButtonSize` | `32` | `SideIconSize + 2 × (BorderWidth + SideIconInset)` |
| `SidePanelMeterAllowance` | `12` | Extra right-panel width per side, for the KPA1500 meters |

The side sheets (`sidePanelButton*()`, `compactButton()`) carry colour, border, radius and font only —
`padding: 0px` and no size. Size is set on the widget: a style sheet `height` has no effect on widgets and
`min-height` counts from the content rect (Qt Style Sheets Reference). Spacing is explicit rather than the
style's `PM_Layout*Spacing`, which follows native guidelines (Aqua spaces push buttons 12 px) and would make
the panels differ per platform.

### Popup Layout

| Constant | Value | Usage |
|----------|-------|-------|
| `PopupButtonWidth` | `70` | Standard popup button width (px) |
| `PopupButtonHeight` | `44` | Standard popup button height (px) |
| `PopupButtonSpacing` | `8` | Spacing between popup buttons (px) |
| `MenuBarButtonWidth` | `90` | Bottom menu bar buttons (fits "MAIN RX") (px) |
| `PopupContentMargin` | `12` | Padding inside popup content area (px) |

### Common UI Heights

| Constant | Value | Usage |
|----------|-------|-------|
| `SeparatorHeight` | `1` | Horizontal/vertical separator lines (px) |
| `MenuItemHeight` | `40` | Menu overlay items, frequency labels (px) |
| `MenuBarHeight` | `52` | Bottom menu bar container height (px) |

### Common UI Widths

| Constant | Value | Usage |
|----------|-------|-------|
| `SmallIconSize` | `20` | Lock icons, health indicator, small controls (px) |
| `CompactButtonSize` | `32` | Side panel icons, EQ +/- buttons (px) |
| `FormLabelWidth` | `80` | Form field labels, numeric value displays (px) |
| `VfoSquareSize` | `45` | VFO A/B indicator squares and mode labels (px) |
| `NavButtonWidth` | `54` | Navigation buttons in overlays (px) |
| `LeftSidePanelWidth` | `130` | Left side panel: `SideButtonGridWidth + 2 × SidePanelMarginH` (px) |
| `RightSidePanelWidth` | `154` | Right side panel: `LeftSidePanelWidth + 2 × SidePanelMeterAllowance` (px) |
| `NavPanelWidth` | `130` | Menu overlay navigation/search panel (px) |
| `MemoryButtonWidth` | `42` | M1–M4, REC, STORE, RCL buttons (px) |

### Type Scale

All font sizes are in **pixels** — use with `QFont::setPixelSize()`, `K4Styles::Fonts::paintFont()` or
`dataFont()`, never `setPointSize()` (points scale with the platform's logical DPI: 72 on macOS, 96 on Windows).

Six roles. `static_assert`s in `k4constants.h` hold them to the floor and in strictly increasing order, and hold
every old name equal to its role. Text at `FontSizeReadout` or smaller on the dark background uses Medium (500)
weight or bolder — the app font is Inter Medium, so stylesheet labels get it by default; custom-painted text
passes `QFont::Medium` to `paintFont()`/`dataFont()`.

| Role | Constant | px | Usage |
|------|----------|----|-------|
| floor | `FontSizeFloor` | `10` | Nothing is drawn smaller |
| caption | `FontSizeCaption` | `10` | Sub-labels, scales, secondary and compact button text |
| body | `FontSizeBody` | `11` | Labels, popup selection buttons, help text |
| readout | `FontSizeReadout` | `12` | Values, button text, popup titles, status bar |
| heading | `FontSizeHeading` | `14` | Dialog forms, menu rows, notifications |
| indicator | `FontSizeIndicator` | `18` | TX/RX/SPLIT, VFO A/B badges, +/- and arrow keys, page titles |
| display | `FontSizeDisplay` | `32` | VFO frequency |

Removed names, for reading older code: `FontSizeTiny` / `Small` / `Normal` / `Medium` → caption; `FontSizeLarge`,
`PopupButtonSize` → body; `FontSizeButton`, `PopupTitleSize`, `PopupValueSize` → readout; `FontSizeNotification`
(was 13), `FontSizePopup` → heading; `FontSizeTitle` (was 16) → indicator; `FontSizeFrequency` → display.

### User-adjustable Ranges

| Constant | Value | Usage |
|----------|-------|-------|
| `FontSizeSpot` | = body (`11`) | Default DX cluster spot-label size (Options → DX Cluster) |
| `FontSizeSpotMin` / `Max` | `10` / `16` | Spot slider range |
| `FontSizeTextDecode` | = body (`11`) | Default decoded-text size (A−/A+, saved per receiver) |
| `FontSizeTextDecodeMin` / `Max` | `10` / `24` | A−/A+ range |


### Slider Dimensions

| Constant | Value | Usage |
|----------|-------|-------|
| `SliderGrooveHeight` | `6` | Horizontal slider groove height (px) |
| `SliderHandleWidth` | `16` | Slider handle width (px) |
| `SliderHandleHeight` | `16` | Explicit handle height (robust on fractional DPI) (px) |
| `SliderHandleMargin` | `-5` | Vertical margin for handle positioning (px) |
| `SliderBorderRadius` | `3` | Groove border radius (px) |
| `SliderHandleRadius` | `8` | Handle border radius (half of width) (px) |
| `SliderMinHeight` | `20` | Minimum widget height to prevent handle clipping (px) |
| `SliderValueLabelWidth` | `40` | Width for percentage value labels (px) |

### VFO Indicator Dimensions

| Constant | Value | Usage |
|----------|-------|-------|
| `VfoIndicatorWidth` | `34` | VFO A/B label width on panadapter (px) |
| `VfoIndicatorHeight` | `30` | VFO A/B label height on panadapter (px) |

### Network Metrics Chart (`NetHealthPopup` sparklines)

| Constant | Value | Usage |
|----------|-------|-------|
| `ChartPopupContentWidth` | `242` | Card content width, excluding shadow margin (px) |
| `ChartRowHeight` | `46` | Height of one metric sparkline row (px) |
| `ChartValueColumnWidth` | `100` | Left column: legend label + current value + scale gutter (px) |
| `ChartLineWidth` | `2` | Sparkline trace stroke width (px) |
| `ChartPlotMargin` | `4` | Inset between a row's plot area and its edges (px) |
| `ChartLegendDotSize` | `6` | Diameter of the series legend dot (px) |
| `ChartPopupGap` | `4` | Gap between the popup card and its anchor widget (px) |

Series colors are aliases, not new values: `ChartSeriesRtt` = `VfoACyan`,
`ChartSeriesJitter` = `AccentAmber`, `ChartSeriesBuffer` = `MeterIdLight`.

### Dialog Dimensions

| Constant | Value | Usage |
|----------|-------|-------|
| `DialogMargin` | `20` | Dialog content margins (px) |
| `TabListWidth` | `150` | Options dialog tab list width (px) |
| `InputFieldWidthSmall` | `100` | Port number, small inputs (px) |
| `InputFieldWidthMedium` | `120` | Version labels, medium fields (px) |
| `CheckboxSize` | `18` | Checkbox indicator dimensions (px) |
| `PaddingSmall` | `6` | Small padding (inputs) (px) |
| `PaddingMedium` | `10` | Medium padding (buttons) (px) |
| `PaddingLarge` | `15` | Large padding (list items) (px) |

---

## Fonts (`K4Styles::Fonts::*`)

### Font Family Constants

| Constant | Value | Usage |
|----------|-------|-------|
| `Primary` | `"Inter"` | UI text, labels, buttons, menus |
| `Data` | `"Inter"` | Frequencies, numeric data (tabular figures via `QFont::setFeature("tnum")` in `dataFont()`; Qt 6.7+) |

### Font Helpers

```cpp
// Build a font for custom-painted widgets with pixel-based sizing.
// setPixelSize() is used (not setPointSize()) so fonts render at the same
// logical size on macOS (72 PPI) and Windows (96 PPI).
QFont K4Styles::Fonts::paintFont(int pixelSize, QFont::Weight weight = QFont::Bold);

// Like paintFont() but enables tabular figures so digit widths stay equal —
// prevents visual shifting when numeric values change (e.g., frequency displays).
QFont K4Styles::Fonts::dataFont(int pixelSize, QFont::Weight weight = QFont::Bold);
```

---

## Helper Functions

### Stylesheet Helpers (`QString`-returning)

```cpp
// Popup buttons
QString K4Styles::popupButtonNormal();    // Dark gradient (unselected band/mode buttons)
QString K4Styles::popupButtonSelected();  // Light/white selected state
QString K4Styles::popupValueLabel(const QString &color); // Borderless value readout in a compact popup row

// Menu bar buttons (BottomMenuBar, FeatureMenuBar)
QString K4Styles::menuBarButton();          // Standard (MENU, Fn, DISPLAY, BAND)
QString K4Styles::menuBarButtonActive();    // Active/inverted (white bg, dark text)
QString K4Styles::menuBarButtonSmall();     // Compact +/- controls
QString K4Styles::menuBarButtonPttPressed();// PTT pressed state (red / white text)

// Sliders + form controls
QString K4Styles::sliderHorizontal(const QString &grooveColor, const QString &handleColor);
QString K4Styles::checkboxButton(int size = 32);  // Checkable toggle button
QString K4Styles::radioButton();                  // Mode selection (DISPLAY ALL / USE SUBSET)

// Side panel buttons
QString K4Styles::compactButton();              // MON, NORM, BAL (dark gradient, SideCompactBorderWidth border)
QString K4Styles::sidePanelButton();            // Dark side-panel keys (right panel, memory, KPA1500, ?, globe)
QString K4Styles::sidePanelButtonLight();       // Light keys (TX, PF, STORE/RCL) — same geometry as dark
QString K4Styles::panelButtonWithDisabled();    // KPA1500 panel buttons (STANDBY, ATU, ANT, TUNE)

// Dialog + control buttons
QString K4Styles::dialogButton();               // Dialog action buttons (Connect, Save, Delete)
QString K4Styles::controlButton(bool selected = false);  // Decode window controls
```

### QPainter Helpers

```cpp
// Gradients
QLinearGradient K4Styles::buttonGradient(int top, int bottom, bool hovered = false);
QLinearGradient K4Styles::selectedGradient(int top, int bottom);
QLinearGradient K4Styles::meterGradient(qreal x1, qreal y1, qreal x2, qreal y2);

// Colors
QColor K4Styles::withAlpha(const char *color, int alpha);  // token at an Alpha:: opacity
QString K4Styles::cssRgba(const char *color, int alpha);   // same, as a style-sheet rgba() value
QColor K4Styles::borderColor();          // BorderNormal (#606060)
QColor K4Styles::borderColorSelected();  // BorderSelected (#AAAAAA)

// Shadow
void K4Styles::drawDropShadow(QPainter &painter, const QRect &contentRect, int cornerRadius = 8);

// Text over the spectrum/waterfall: 1 px ring of 60 % black, then the text. DX spot labels and the
// panadapter's amplitude and frequency scales. `baseline` is as for QPainter::drawText(QPointF, QString).
void K4Styles::drawHaloText(QPainter &painter, const QPointF &baseline, const QString &text, const QColor &color);
```

### Dialog::* Cached Stylesheet Helpers

Under `K4Styles::Dialog::`. Each returns a `const QString &` to a static cached string (zero per-call allocation) unless explicitly dynamic. Use these in `OptionsDialog` and its tab pages — eliminates the 26+ inline `QString("color: %1; font-size: %2px;").arg(...)` patterns that used to be scattered across `src/ui/`.

```cpp
// Static/cached — `const QString &`
const QString &K4Styles::Dialog::pageBackground();   // Page background-color
const QString &K4Styles::Dialog::separator();        // Horizontal/vertical separator line
const QString &K4Styles::Dialog::titleLabel();       // Amber bold title (AccentAmber, FontSizeIndicator)
const QString &K4Styles::Dialog::formLabel();        // Gray form label (TextSecondary, FontSizeHeading)
const QString &K4Styles::Dialog::formValue();        // White bold value (TextPrimary, FontSizeHeading)
const QString &K4Styles::Dialog::helpText();         // Gray italic help (TextSecondary, FontSizeBody)
const QString &K4Styles::Dialog::sectionHeader();    // White bold header (TextPrimary, FontSizeHeading)
const QString &K4Styles::Dialog::comboBox();         // Full combo box styling (dark bg, arrow, item view)
const QString &K4Styles::Dialog::lineEdit();         // Full line edit (dark bg, focus border)
const QString &K4Styles::Dialog::spinBox();          // Full spin box (dark bg, focus border, drawn arrows)
const QString &K4Styles::Dialog::checkBox();         // Checkbox + indicator sizing (enabled)
const QString &K4Styles::Dialog::checkBoxDisabled(); // Checkbox (disabled / gray text)
const QString &K4Styles::Dialog::actionButton();     // Dialog action button (dark bg, padding 10px 20px)
const QString &K4Styles::Dialog::actionButtonSmall();// Small action button (padding 6px 12px)

const QString &K4Styles::applicationStyleSheet();    // App-wide fallback: checkBox + comboBox + lineEdit + spinBox, installed in main()

// Dynamic — `QString` (fresh each call; color/size vary)
const QString &K4Styles::Dialog::statusLabel(const QString &color);  // Bold status label, dynamic color
QString K4Styles::Dialog::labelText(const QString &color, int sizePx);
QString K4Styles::Dialog::labelTextBold(const QString &color, int sizePx);
```

**Rule:** if you catch yourself writing `QString("color: %1; font-size: %2px;").arg(K4Styles::Colors::X).arg(K4Styles::Dimensions::Y)`, use `K4Styles::Dialog::labelText(K4Styles::Colors::X, K4Styles::Dimensions::Y)` instead. Single source of truth keeps styling consistent and makes global tweaks one-line.

---

## Usage Examples

### Stylesheet-based Button

```cpp
auto *btn = new QPushButton("BAND", this);
btn->setStyleSheet(K4Styles::popupButtonNormal());

// On selection:
btn->setStyleSheet(K4Styles::popupButtonSelected());
```

### Custom-painted Widget

```cpp
void MyWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this);

    // Draw gradient background
    QLinearGradient grad = K4Styles::buttonGradient(0, height(), m_hovered);
    painter.fillRect(rect(), grad);

    // Draw border
    painter.setPen(QPen(K4Styles::borderColor(), K4Styles::Dimensions::BorderWidth));
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1),
                           K4Styles::Dimensions::BorderRadius,
                           K4Styles::Dimensions::BorderRadius);

    // Draw text
    painter.setPen(QColor(K4Styles::Colors::TextPrimary));
    painter.drawText(rect(), Qt::AlignCenter, "Label");
}
```

### Popup with Shadow

```cpp
void MyPopup::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QRect content = contentRect();  // From K4PopupBase

    // Draw shadow (K4PopupBase does this automatically)
    K4Styles::drawDropShadow(painter, content, K4Styles::Dimensions::BorderRadiusLarge);

    // Draw popup background
    painter.setBrush(QColor(K4Styles::Colors::PopupBackground));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(content,
                           K4Styles::Dimensions::BorderRadiusLarge,
                           K4Styles::Dimensions::BorderRadiusLarge);
}
```

### Dialog Field Pair

```cpp
auto *label = new QLabel("Callsign:", this);
label->setStyleSheet(K4Styles::Dialog::formLabel());

auto *edit = new QLineEdit(this);
edit->setStyleSheet(K4Styles::Dialog::lineEdit());
```

---

## Enforcement

`tests/test_tokenstandard.cpp` (`TokenStandardTests`):

- every (text token, background) pair the UI draws meets 4.5:1, scrims and translucent fills judged as composited;
  the only pairs allowed under it are disabled-control text (WCAG-exempt), listed and required to stay below 4.5:1 so a
  pair that starts passing has to leave the list;
- no hex value is repeated in `k4constants.h`;
- outside `k4constants.h` (comments excluded): no `setPointSize`, no literal size in `setPixelSize` / `paintFont` /
  `dataFont` / `font-size:`, no hex colour, no `QColor(<digit>` or `QColor::fromRgb[F](<digit>`, no
  `Qt::white`/`Qt::black`, no style-sheet `rgb()`/`rgba()` literal or `black`/`white` keyword. Exceptions are an
  explicit per-file, per-rule allow-list in the test.

---

## Keeping This Doc Current

This file is hand-maintained against `src/ui/styling/k4constants.h` (constants) and `src/ui/styling/k4styles.h` (helpers). It has previously drifted significantly. When you add or change a constant / helper:

1. Update the appropriate header first (the source of truth):
   - Colors / dimensions → `src/ui/styling/k4constants.h`
   - Fonts / stylesheet helpers / `Dialog::` helpers → `src/ui/styling/k4styles.h`
2. Add or update the corresponding row in this doc.
3. Spot-check with the greps at the top of this file:

```bash
rg 'constexpr const char \*' src/ui/styling/k4constants.h | wc -l   # color count sanity check
rg '^constexpr int' src/ui/styling/k4constants.h | wc -l            # dimension count sanity check
```

If the counts produced by those greps don't match what you see tabled above, this doc is stale again. Fix the tables; don't ship with mismatch.
