# ui/styling/

Shared styling infrastructure. Used by every other `ui/` subdirectory.

## Files

- `k4constants.h` — colour tokens (each hex written once), `Colors::Alpha::*`, dimensions and the six-role type scale (`FontSizeCaption` … `FontSizeDisplay`). No Qt includes. **Single source of truth for every visual constant.**
- `k4styles.{cpp,h}` — font helpers, `withAlpha()` / `cssRgba()`, stylesheet functions, `Dialog::` cached-QString helpers.
- `k4popupbase.{cpp,h}` — Base class for every popup (see `ui/popups/`). Handles window flags, shadow rendering, Escape/click-outside dismissal, screen-boundary detection.
- `k4glyphs.{h}` — procedurally drawn UI glyphs for the status bar and `PowerStatusButton`. Each factory returns a closure that renders the glyph in a supplied color, so glyphs follow the palette instead of shipping as fixed-color assets.

## Rule

Never inline hex colors, `QColor(r, g, b)`, `Qt::white`/`Qt::black`, style-sheet `rgba()` or pixel font sizes in widget code. Always reference `K4Styles::Colors::*` and `K4Styles::Dimensions::*`. `tests/test_tokenstandard.cpp` fails the build otherwise, and checks every text/background pair for WCAG 4.5:1.

## Reference

- `docs/K4STYLES_REFERENCE.md` — complete reference for every color, dimension, helper, and `Dialog::` function.
- Sanity grep: `rg 'constexpr const char \*' src/ui/styling/k4styles.h` lists every color constant.
