#include <QtTest/QtTest>
#include "audio/micresampler.h"
#include <algorithm>

class TestMicResampler : public QObject {
    Q_OBJECT
private slots:
    void streaming_data() {
        QTest::addColumn<int>("rate");
        for (int rate : {12000, 16000, 24000, 44100, 48000})
            QTest::newRow(qPrintable(QString::number(rate))) << rate;
    }
    void streaming() {
        QFETCH(int, rate);
        std::vector<float> input(rate * 3 + 127);
        for (int i = 0; i < int(input.size()); ++i)
            input[i] = 0.8f * std::sin(2 * 3.141592653589793 * 1000 * i / rate);
        MicResampler whole, chunks;
        QVERIFY(whole.configure(rate));
        QVERIFY(chunks.configure(rate));
        std::vector<float> expected, actual;
        whole.process(input.data(), input.size(), expected);
        int pos = 0;
        while (pos < int(input.size())) {
            int n = std::min(1 + (pos * 17 % 997), int(input.size()) - pos);
            chunks.process(input.data() + pos, n, actual);
            pos += n;
        }
        QCOMPARE(actual.size(), input.size() * 12000 / rate);
        QVERIFY(actual == expected); // Bit-identical regardless of callback boundaries.
        double power = 0, errorPower = 0;
        const int delay = rate == 12000 ? 0 : int(std::ceil(24.0 * rate / 12000));
        for (size_t i = 1000; i < actual.size(); ++i) {
            QVERIFY(std::isfinite(actual[i]));
            QVERIFY(std::abs(actual[i]) < 0.82);
            power += actual[i] * actual[i];
            const double inputTime = (i + 1) * rate / 12000.0 - 1 - delay;
            const double ideal = 0.8 * std::sin(2 * 3.141592653589793 * 1000 * inputTime / rate);
            errorPower += (actual[i] - ideal) * (actual[i] - ideal);
        }
        QVERIFY(std::sqrt(errorPower / (actual.size() - 1000)) < 0.001);
        const double rms = std::sqrt(power / (actual.size() - 1000));
        QVERIFY(std::abs(rms - 0.8 / std::sqrt(2.0)) < 0.002);
        // A steady 1 kHz tone repeats exactly every 12 output samples.
        for (size_t i = 1000; i + 12 < actual.size(); ++i)
            QVERIFY(std::abs(actual[i] - actual[i + 12]) < 0.00001);
    }
    void rejectsAliasing_data() {
        streaming_data();
    }
    void rejectsAliasing() {
        QFETCH(int, rate);
        if (rate == 12000)
            return;
        MicResampler r;
        QVERIFY(r.configure(rate));
        std::vector<float> input(rate), output;
        for (int i = 0; i < rate; ++i)
            input[i] = 0.8f * std::sin(2 * 3.141592653589793 * 7000 * i / rate);
        r.process(input.data(), input.size(), output);
        double power = 0;
        for (size_t i = 1000; i < output.size(); ++i)
            power += output[i] * output[i];
        QVERIFY(std::sqrt(power / (output.size() - 1000)) < 0.001);
    }
    void longDurationCounts() {
        for (int rate : {12000, 16000, 24000, 44100, 48000}) {
            MicResampler r;
            QVERIFY(r.configure(rate));
            std::vector<float> input(4093, 0.25f), output;
            qint64 total = 0, produced = 0;
            const qint64 target = qint64(rate) * 60 + 13;
            while (total < target) {
                int n = std::min<qint64>(input.size(), target - total);
                output.clear();
                r.process(input.data(), n, output);
                total += n;
                produced += output.size();
                QCOMPARE(produced, total * 12000 / rate);
            }
        }
    }
};
QTEST_GUILESS_MAIN(TestMicResampler)
#include "test_micresampler.moc"
