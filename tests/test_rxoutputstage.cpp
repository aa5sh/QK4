// RX playback boost and soft limiter (src/audio/rxoutputstage.h).
//
// The boost multiplies the MAIN/SUB sliders (0 to +12 dB in 3 dB steps); above 0 dB the output goes
// through softLimit() instead of a hard clamp. These pin the two properties that matter to an
// operator: a step is exactly its dB, and the limiter never touches audio below its threshold nor
// lets anything past full scale.

#include "audio/rxoutputstage.h"

#include <QTest>
#include <cmath>

using namespace RxOutputStage;

class TestRxOutputStage : public QObject {
    Q_OBJECT

private slots:
    void boostStepsAreExactlyTheirDecibels_data() {
        QTest::addColumn<int>("db");
        for (int db = 0; db <= kMaxBoostDb; db += kBoostStepDb)
            QTest::newRow(qPrintable(QString("+%1 dB").arg(db))) << db;
    }

    void boostStepsAreExactlyTheirDecibels() {
        QFETCH(int, db);
        const double measured = 20.0 * std::log10(static_cast<double>(boostGain(db)));
        QVERIFY2(std::fabs(measured - db) < 1e-4, qPrintable(QString("%1 dB came out %2").arg(db).arg(measured)));
    }

    void zeroBoostIsUnity() { QCOMPARE(boostGain(0), 1.0f); }

    // A saved value outside the offered steps - an older or hand-edited settings file - lands on a
    // step rather than becoming an arbitrary gain.
    void boostSnapsToTheOfferedSteps() {
        QCOMPARE(snapBoostDb(-5), 0);
        QCOMPARE(snapBoostDb(1), 0);
        QCOMPARE(snapBoostDb(2), 3);
        QCOMPARE(snapBoostDb(7), 6);
        QCOMPARE(snapBoostDb(8), 9);
        QCOMPARE(snapBoostDb(12), 12);
        QCOMPARE(snapBoostDb(40), 12);
    }

    // Below the threshold the limiter must be bit-exact: it exists for peaks, not for the signal.
    void limiterIsTransparentBelowItsThreshold() {
        for (int i = -8000; i <= 8000; ++i) {
            const float x = i / 10000.0f; // -0.8 .. +0.8
            QCOMPARE(softLimit(x), x);
        }
    }

    void limiterNeverExceedsFullScale() {
        for (float x : {0.81f, 0.9f, 1.0f, 1.5f, 2.0f, 4.0f, 16.0f, 1e6f}) {
            QVERIFY(softLimit(x) <= 1.0f);
            QVERIFY(softLimit(-x) >= -1.0f);
        }
    }

    // Louder in stays louder out (no fold-back), and it is symmetric, so it adds no DC offset.
    void limiterIsMonotonicAndSymmetric() {
        float previous = 0.0f;
        for (int i = 0; i <= 40000; ++i) {
            const float x = i / 10000.0f; // 0 .. 4.0
            const float y = softLimit(x);
            QVERIFY(y >= previous);
            QCOMPARE(softLimit(-x), -y);
            previous = y;
        }
    }

    // No step at the knee: a signal crossing the threshold does not jump.
    void limiterIsContinuousAtTheKnee() {
        const float below = softLimit(kLimiterThreshold);
        const float above = softLimit(std::nextafter(kLimiterThreshold, 1.0f));
        QVERIFY(std::fabs(above - below) < 1e-5f);
    }

    // What the boost is for: +12 dB on a full-scale peak lands inside full scale instead of being
    // flattened at it.
    void boostedFullScalePeakIsRoundedNotClipped() {
        const float y = softLimit(1.0f * boostGain(12));
        QVERIFY(y > kLimiterThreshold);
        QVERIFY(y <= 1.0f);
    }

    void limiterPassesNanThrough() { QVERIFY(std::isnan(softLimit(std::nanf("")))); }
};

QTEST_APPLESS_MAIN(TestRxOutputStage)
#include "test_rxoutputstage.moc"
