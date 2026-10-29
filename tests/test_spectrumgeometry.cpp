#include "dsp/spectrumgeometry.h"
#include <QtTest>

using namespace SpectrumGeometry;

class TestSpectrumGeometry : public QObject {
    Q_OBJECT

private slots:
    void modeParsesRadioStateSpelling();
    void unknownModeStringsAreUnknown();
    void dataSubModeOutOfRangeIsDataA();

    void cwShiftsTheDisplayCentreUp();
    void cwReverseShiftsTheDisplayCentreDown();
    void otherModesKeepTheDisplayCentre();
    void cwMarkerSitsAtThePassbandCentre();

    void ssbPassbandFollowsIfShift();
    void cwPassbandMatchesItsMarker();
    void rttyPassbandStraddlesMarkAndSpace();
    void pskPassbandIsCentredOnTheDial();
    void dataAPassbandIsUpperSideband();
    void amFmAndUnknownIgnoreIfShift();

    void normalFrameIsUsable();
    void binCountWiderThanTheTextureIsRejected();
    void binsPastThePayloadAreRejected();
    void degenerateFramesAreRejected();
};

void TestSpectrumGeometry::modeParsesRadioStateSpelling() {
    QCOMPARE(modeFromString("LSB"), Mode::Lsb);
    QCOMPARE(modeFromString("USB"), Mode::Usb);
    QCOMPARE(modeFromString("CW"), Mode::Cw);
    QCOMPARE(modeFromString("CW-R"), Mode::CwR);
    QCOMPARE(modeFromString("DATA"), Mode::Data);
    QCOMPARE(modeFromString("DATA-R"), Mode::DataR);
    QCOMPARE(modeFromString("AM"), Mode::Am);
    QCOMPARE(modeFromString("FM"), Mode::Fm);
}

void TestSpectrumGeometry::unknownModeStringsAreUnknown() {
    QCOMPARE(modeFromString(""), Mode::Unknown);
    QCOMPARE(modeFromString("cw"), Mode::Unknown);
    QCOMPARE(modeFromString("AFSK"), Mode::Unknown);
}

void TestSpectrumGeometry::dataSubModeOutOfRangeIsDataA() {
    QCOMPARE(dataSubModeFromInt(0), DataSubMode::DataA);
    QCOMPARE(dataSubModeFromInt(1), DataSubMode::AfskA);
    QCOMPARE(dataSubModeFromInt(2), DataSubMode::FskD);
    QCOMPARE(dataSubModeFromInt(3), DataSubMode::PskD);
    QCOMPARE(dataSubModeFromInt(-1), DataSubMode::DataA);
    QCOMPARE(dataSubModeFromInt(7), DataSubMode::DataA);
}

void TestSpectrumGeometry::cwShiftsTheDisplayCentreUp() {
    // IF shift is in decahertz: 50 -> 500 Hz.
    QCOMPARE(displayCenterHz(7030000, 50, Mode::Cw), qint64(7030500));
}

void TestSpectrumGeometry::cwReverseShiftsTheDisplayCentreDown() {
    QCOMPARE(displayCenterHz(7030000, 50, Mode::CwR), qint64(7029500));
}

void TestSpectrumGeometry::otherModesKeepTheDisplayCentre() {
    for (Mode m : {Mode::Unknown, Mode::Lsb, Mode::Usb, Mode::Data, Mode::DataR, Mode::Am, Mode::Fm})
        QCOMPARE(displayCenterHz(14074000, 150, m), qint64(14074000));
}

void TestSpectrumGeometry::cwMarkerSitsAtThePassbandCentre() {
    QCOMPARE(markerFreqHz(7030000, 60, Mode::Cw), qint64(7030600));
    QCOMPARE(markerFreqHz(7030000, 60, Mode::CwR), qint64(7029400));
    QCOMPARE(markerFreqHz(14200000, 150, Mode::Usb), qint64(14200000));
}

void TestSpectrumGeometry::ssbPassbandFollowsIfShift() {
    const Passband usb = passbandFor(Mode::Usb, DataSubMode::DataA, 14200000, 2400, 150, 170);
    QCOMPARE(usb.lowHz, qint64(14200300));
    QCOMPARE(usb.highHz, qint64(14202700));
    const Passband lsb = passbandFor(Mode::Lsb, DataSubMode::DataA, 7200000, 2400, 150, 170);
    QCOMPARE(lsb.lowHz, qint64(7197300));
    QCOMPARE(lsb.highHz, qint64(7199700));
}

void TestSpectrumGeometry::cwPassbandMatchesItsMarker() {
    for (Mode m : {Mode::Cw, Mode::CwR}) {
        const Passband pb = passbandFor(m, DataSubMode::DataA, 7030000, 400, 55, 170);
        QCOMPARE((pb.lowHz + pb.highHz) / 2, markerFreqHz(7030000, 55, m));
        QCOMPARE(pb.highHz - pb.lowHz, qint64(400));
    }
}

void TestSpectrumGeometry::rttyPassbandStraddlesMarkAndSpace() {
    // The dial is the mark tone; space is one 170 Hz shift below. Centre is half a shift below.
    for (Mode m : {Mode::Data, Mode::DataR}) {
        for (DataSubMode s : {DataSubMode::AfskA, DataSubMode::FskD}) {
            const Passband pb = passbandFor(m, s, 14080000, 500, 150, 170);
            QCOMPARE(pb.lowHz, qint64(14080000 - 85 - 250));
            QCOMPARE(pb.highHz, qint64(14080000 - 85 + 250));
        }
    }
}

void TestSpectrumGeometry::pskPassbandIsCentredOnTheDial() {
    const Passband pb = passbandFor(Mode::Data, DataSubMode::PskD, 14070000, 300, 150, 170);
    QCOMPARE(pb.lowHz, qint64(14069850));
    QCOMPARE(pb.highHz, qint64(14070150));
}

void TestSpectrumGeometry::dataAPassbandIsUpperSideband() {
    for (Mode m : {Mode::Data, Mode::DataR}) {
        const Passband pb = passbandFor(m, DataSubMode::DataA, 14074000, 3000, 150, 170);
        QCOMPARE(pb.lowHz, qint64(14074000));
        QCOMPARE(pb.highHz, qint64(14077000));
    }
}

void TestSpectrumGeometry::amFmAndUnknownIgnoreIfShift() {
    for (Mode m : {Mode::Am, Mode::Fm, Mode::Unknown}) {
        const Passband pb = passbandFor(m, DataSubMode::DataA, 29000000, 6000, 150, 170);
        QCOMPARE(pb.lowHz, qint64(28997000));
        QCOMPARE(pb.highHz, qint64(29003000));
    }
}

void TestSpectrumGeometry::normalFrameIsUsable() {
    QCOMPARE(usableBinCount(16 + 1024, 16, 1024, 4096), 1024);
    QCOMPARE(usableBinCount(4096, 0, 4096, 4096), 4096);
}

void TestSpectrumGeometry::binCountWiderThanTheTextureIsRejected() {
    // A count above the texture width centred the bins at a negative offset and wrote outside the
    // waterfall and spectrum buffers.
    QCOMPARE(usableBinCount(16 + 5000, 16, 5000, 4096), 0);
    QCOMPARE(usableBinCount(16 + 4097, 16, 4097, 4096), 0);
}

void TestSpectrumGeometry::binsPastThePayloadAreRejected() {
    QCOMPARE(usableBinCount(1000, 16, 1024, 4096), 0);
    QCOMPARE(usableBinCount(1040, 17, 1024, 4096), 0);
}

void TestSpectrumGeometry::degenerateFramesAreRejected() {
    QCOMPARE(usableBinCount(1040, 16, 0, 4096), 0);
    QCOMPARE(usableBinCount(1040, 16, -5, 4096), 0);
    QCOMPARE(usableBinCount(1040, -1, 1024, 4096), 0);
}

QTEST_APPLESS_MAIN(TestSpectrumGeometry)
#include "test_spectrumgeometry.moc"
