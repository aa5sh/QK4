#include <QtTest/QtTest>
#include <QBuffer>
#include "audio/audioengine.h"
#include "audio/audiologging.h"
#include "models/transmitowner.h"

Q_LOGGING_CATEGORY(qk4Audio, "qk4.audio")
Q_LOGGING_CATEGORY(qk4AudioTx, "qk4.audio.tx")
Q_LOGGING_CATEGORY(qk4AudioRx, "qk4.audio.rx")

class TestMicCapture : public QObject {
    Q_OBJECT
private slots:
    void captureEmitsExistingEm3Frames_data() {
        QTest::addColumn<int>("rate");
        for (int rate : {12000, 16000, 24000, 44100, 48000})
            QTest::newRow(qPrintable(QString::number(rate))) << rate;
    }
    void captureEmitsExistingEm3Frames() {
        QFETCH(int, rate);
        AudioEngine engine;
        engine.m_inputFormat.setSampleRate(rate);
        engine.m_inputFormat.setChannelCount(1);
        engine.m_inputFormat.setSampleFormat(QAudioFormat::Float);
        QVERIFY(engine.m_micResampler.configure(rate));
        engine.m_micEnabled = true;
        engine.m_pttActive = true;
        engine.setFrameSamples(720); // Supplied working AirPods log.
        engine.setEncodeMode(3);
        QBuffer device;
        QVERIFY(device.open(QIODevice::ReadWrite));
        engine.m_audioSourceDevice = &device;
        QSignalSpy packets(&engine, &AudioEngine::txPacketReady);
        QSignalSpy errors(&engine, &AudioEngine::microphoneFailed);
        std::vector<float> input(rate, 0.1f);
        const QByteArray captured(reinterpret_cast<const char *>(input.data()), rate * sizeof(float));
        for (int pos = 0; pos < captured.size();) {
            const int n = qMin(1301, int(captured.size()) - pos);
            device.buffer() = captured.mid(pos, n);
            device.seek(0);
            engine.onMicDataReady();
            pos += n;
        }
        QCOMPARE(packets.count(), 12000 / 720);
        QCOMPARE(engine.m_micBuffer.size() - engine.m_micReadOffset, (12000 % 720) * int(sizeof(float)));
        engine.failMicrophone(QStringLiteral("injected mid-stream error"));
        QCOMPARE(errors.count(), 1);
        device.seek(0);
        engine.onMicDataReady();
        QCOMPARE(packets.count(), 12000 / 720);
    }
    void stereoPcmWithPartialBytes() {
        AudioEngine engine;
        engine.m_inputFormat.setSampleRate(44100);
        engine.m_inputFormat.setChannelCount(2);
        engine.m_inputFormat.setSampleFormat(QAudioFormat::Int16);
        QVERIFY(engine.m_micResampler.configure(44100));
        engine.m_micEnabled = true;
        QBuffer device;
        QVERIFY(device.open(QIODevice::ReadWrite));
        engine.m_audioSourceDevice = &device;
        QByteArray captured;
        for (int i = 0; i < 44100; ++i) {
            const qint16 left = 16384, right = 0;
            captured.append(reinterpret_cast<const char *>(&left), 2);
            captured.append(reinterpret_cast<const char *>(&right), 2);
        }
        // Force byte boundaries inside stereo sample frames.
        for (int pos = 0; pos < captured.size();) {
            const int n = qMin(337, int(captured.size()) - pos);
            device.buffer() = captured.mid(pos, n);
            device.seek(0);
            engine.onMicDataReady();
            pos += n;
        }
        QVERIFY(engine.m_capturePending.isEmpty());
        QVERIFY(!engine.m_resampledCapture.empty());
        QVERIFY(std::abs(engine.m_resampledCapture.back() - 0.25f) < 0.0001f);
        engine.closeMic();
    }
    void audioFailureClosesGateAndReleasesOwner_data() {
        QTest::addColumn<bool>("catKeyed");
        QTest::newRow("PTT streamed") << false;
        QTest::newRow("XMIT CAT and streamed") << true;
    }
    void audioFailureClosesGateAndReleasesOwner() {
        QFETCH(bool, catKeyed);
        using namespace TransmitOwner;
        AudioEngine engine;
        State state;
        engage(state, catKeyed ? Owner::Xmit : Owner::PttButton,
               catKeyed ? Route::CatKeyedAndStreamed : Route::StreamedFromHere);
        engine.m_pttActive = true;
        engine.m_micEnabled = true;
        engine.m_micBuffer = QByteArray(100, 'x');
        int errors = 0;
        connect(&engine, &AudioEngine::microphoneFailed, &engine, [&]() {
            ++errors;
            QVERIFY(!engine.isPttActive()); // Must precede ownership/RX cleanup.
            const auto effects = releaseAll(state);
            QCOMPARE(effects.sendRx, catKeyed);
            QVERIFY(!state.transmitting());
        });
        engine.failMicrophone(QStringLiteral("injected capture failure"));
        QCOMPARE(errors, 1);
        QVERIFY(!engine.isMicOpen());
        QVERIFY(engine.m_micBuffer.isEmpty());
        QVERIFY(!engine.m_micPollTimer->isActive());
        engine.failMicrophone(QStringLiteral("duplicate stopped notification"));
        QCOMPARE(errors, 1);
    }
};
QTEST_GUILESS_MAIN(TestMicCapture)
#include "test_miccapture.moc"
