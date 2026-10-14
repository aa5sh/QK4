#ifndef WINDOWFIT_H
#define WINDOWFIT_H

#include <QSize>
#include <algorithm>
#include <cmath>

// How big the main window opens, and how far the interface must shrink to fit the screen.
//
// Pure logic on sizes, so every screen can be tested without owning one — mirroring
// audio/audiodeviceselection.h, hardware/usbdevicelifecycle.h and the rest.
//
// WHY this exists. The window once declared a fixed minimum of 1340x840 that was smaller than its
// own layout: the side panel needs more height than 840 leaves once an in-window menu bar (Windows,
// Linux) takes its share, so the TX buttons were squeezed under their orange sub-labels. On a 1080p
// laptop at 150% scaling the screen offers ~640 logical px for a window that needs ~860, and the
// whole left panel collapsed. The layout is a front panel and is never rearranged; the window takes
// its minimum from the layout, and when the screen is too small the whole interface is scaled
// uniformly instead.
namespace WindowFit {

// WHY: 5% steps keep the chosen factor stable — a screen a few pixels different must not produce a
// new factor, and therefore a relaunch, every time.
constexpr double kScaleStep = 0.05;
// WHY: below half size the 8 px secondary text is unreadable at 100% OS scaling. A screen that
// small gets a window that hangs off the edge rather than one nobody can read.
constexpr double kMinScale = 0.5;
constexpr double kMaxScale = 1.0;

// The size the window opens at: the preferred size, never below what the layout needs, and never
// larger than the screen offers — unless the layout itself does not fit, in which case the layout
// wins, because a window smaller than its contents is the bug this file exists to prevent.
inline QSize initialWindowSize(QSize layoutMin, QSize preferred, QSize available) {
    const int w = std::max(layoutMin.width(), std::min(preferred.width(), available.width()));
    const int h = std::max(layoutMin.height(), std::min(preferred.height(), available.height()));
    return {w, h};
}

// The interface scale at which a window needing `required` fits `available`. Both sizes are logical
// pixels measured under `currentScale`, the factor already applied to this run. A layout's logical
// size does not depend on the factor; the screen's does, inversely — at 0.75 a screen offers 4/3 the
// logical pixels it does at 1.0. So the answer is currentScale x the tighter of the two ratios. Rounded
// down to a whole step so the result really fits, and capped at 1.0 — Auto only ever shrinks, so a
// screen with room returns the interface to its native size.
inline double fitScale(QSize required, QSize available, double currentScale) {
    if (required.isEmpty() || available.isEmpty() || currentScale <= 0.0)
        return kMaxScale;
    const double ratio =
        std::min(double(available.width()) / required.width(), double(available.height()) / required.height());
    // WHY: the epsilon absorbs floating-point error so an exact fit at a step (0.75 computed as
    // 0.7499999) is not rounded down a whole step.
    const double steps = std::floor(currentScale * ratio / kScaleStep + 1e-9);
    // WHY: steps x 0.05 lands a hair off the decimal (14 x 0.05 = 0.7000000000000001), which is what
    // gets saved and shown. Rounding to whole percent gives the double nearest the step itself.
    return std::clamp(std::round(steps * kScaleStep * 100.0) / 100.0, kMinScale, kMaxScale);
}

} // namespace WindowFit

#endif // WINDOWFIT_H
