// Main-window sizing (src/utils/windowfit.h): the size the window opens at, and the interface
// scale at which it fits a screen. Screens are given in logical pixels, the units Qt lays out in —
// a 1920x1080 panel at 150% OS scaling is 1280x720 logical.

#include "utils/windowfit.h"

#include <QTest>

namespace {

// What the main window needed when this was written: layout minimum plus menu bar, plus title bar
// and frame. Exact values do not matter to the rules under test; realistic ones keep the cases
// readable.
const QSize kRequired(1180, 900);

// Available area of a 1080p screen at the given OS scaling, minus a 48 px taskbar.
QSize screen1080(double osScale) {
    return {int(1920 / osScale), int(1080 / osScale) - 48};
}

bool fits(QSize required, QSize available, double fromScale, double toScale) {
    const double shrink = toScale / fromScale;
    return required.width() * shrink <= available.width() + 1e-9 &&
           required.height() * shrink <= available.height() + 1e-9;
}

} // namespace

class WindowFitTests : public QObject {
    Q_OBJECT

private slots:
    void initialSizeIsPreferredWhenItFits() {
        QCOMPARE(WindowFit::initialWindowSize({1157, 862}, {1340, 0}, {1920, 1032}), QSize(1340, 862));
    }

    void initialSizeNeverBelowLayoutMinimum() {
        // A preferred size smaller than the layout is the original bug: the window opened at 840
        // tall, below its own 862 px layout, and the side panel was squeezed.
        QCOMPARE(WindowFit::initialWindowSize({1157, 862}, {1340, 840}, {1920, 1032}), QSize(1340, 862));
    }

    void initialSizeBoundedByScreen() {
        QCOMPARE(WindowFit::initialWindowSize({1157, 700}, {1340, 0}, {1280, 900}), QSize(1280, 700));
    }

    void layoutWinsOverATooSmallScreen() {
        // Too small a screen gets a window that hangs off the edge, never one smaller than its
        // contents. Shrinking the interface to fit is fitScale's job, not this one's.
        QCOMPARE(WindowFit::initialWindowSize({1157, 862}, {1340, 0}, {1280, 672}), QSize(1280, 862));
    }

    void fullSizeWhenItFits() { QCOMPARE(WindowFit::fitScale(kRequired, screen1080(1.0), 1.0), 1.0); }

    void laptopAt150Percent() {
        const double s = WindowFit::fitScale(kRequired, screen1080(1.5), 1.0);
        QVERIFY2(s <= 0.75, qPrintable(QString::number(s)));
        QVERIFY(fits(kRequired, screen1080(1.5), 1.0, s));
    }

    void laptopAt125Percent() {
        const double s = WindowFit::fitScale(kRequired, screen1080(1.25), 1.0);
        QCOMPARE(s, 0.9);
        QVERIFY(fits(kRequired, screen1080(1.25), 1.0, s));
    }

    void exactFitAtAStepIsNotRoundedDown() { QCOMPARE(WindowFit::fitScale({1000, 1000}, {750, 750}, 1.0), 0.75); }

    void clampedAtTheFloor() { QCOMPARE(WindowFit::fitScale(kRequired, {400, 300}, 1.0), WindowFit::kMinScale); }

    void returnsToFullSizeOnABiggerScreen() {
        // Launched last time on the laptop at 0.75, now docked to a 2560x1440 monitor. Under 0.75 the
        // layout is the same size in logical pixels and the screen is larger: 2560/0.75 wide.
        QCOMPARE(WindowFit::fitScale(kRequired, {3413, 1856}, 0.75), 1.0);
    }

    void stableUnderTheScaleItChose() {
        // Relaunched at the factor it chose, the same screen must produce the same factor —
        // otherwise Auto would relaunch again.
        const QSize available = screen1080(1.5);
        const double first = WindowFit::fitScale(kRequired, available, 1.0);
        const QSize availableAtFirst(int(available.width() / first), int(available.height() / first));
        QCOMPARE(WindowFit::fitScale(kRequired, availableAtFirst, first), first);
    }

    void degenerateInputLeavesFullSize() {
        QCOMPARE(WindowFit::fitScale({}, screen1080(1.0), 1.0), 1.0);
        QCOMPARE(WindowFit::fitScale(kRequired, {}, 1.0), 1.0);
        QCOMPARE(WindowFit::fitScale(kRequired, screen1080(1.0), 0.0), 1.0);
    }

    void sweepResultAlwaysFitsAndIsAStep() {
        for (int rw = 600; rw <= 2400; rw += 150)
            for (int rh = 500; rh <= 1600; rh += 110)
                for (int aw = 640; aw <= 3840; aw += 320)
                    for (int ah = 400; ah <= 2160; ah += 160)
                        for (double cur : {0.75, 1.0, 1.25}) {
                            const QSize req(rw, rh), avail(aw, ah);
                            const double s = WindowFit::fitScale(req, avail, cur);
                            QVERIFY(s >= WindowFit::kMinScale && s <= WindowFit::kMaxScale);
                            const double steps = s / WindowFit::kScaleStep;
                            QVERIFY(qAbs(steps - qRound(steps)) < 1e-6);
                            if (s > WindowFit::kMinScale)
                                QVERIFY2(fits(req, avail, cur, s), qPrintable(QString("%1x%2 in %3x%4 at %5 -> %6")
                                                                                  .arg(rw)
                                                                                  .arg(rh)
                                                                                  .arg(aw)
                                                                                  .arg(ah)
                                                                                  .arg(cur)
                                                                                  .arg(s)));
                        }
    }
};

QTEST_GUILESS_MAIN(WindowFitTests)
#include "test_windowfit.moc"
