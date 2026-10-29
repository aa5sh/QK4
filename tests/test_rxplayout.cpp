#include "audio/rxplayout.h"
#include "utils/radioutils.h"

#include <QtTest>

#include <algorithm>
#include <deque>
#include <random>
#include <vector>

// RX playout sizing and jitter policy (audio/rxplayout.h), plus a millisecond simulation of the
// whole RX path - network arrivals, the app queue, the feed timer, the sink and an OS device that
// drains it a period at a time - run with the new policy and with the one it replaced. There is no
// K4 or audio device in CI, so the simulation is the evidence for the sizes chosen.
namespace {

struct SimConfig {
    int packetMs = 20;
    int devicePeriodMs = 10;
    int jitterMs = 0;   // each packet delayed by a uniform 0..jitterMs, order kept (TCP)
    int stallAtMs = -1; // packets due in [stallAtMs, stallAtMs + stallMs) all arrive at the end
    int stallMs = 0;
    int durationMs = 20000;
    bool legacy = false; // the old engine: 500 ms sink, queue-only trimming at enqueue
};

struct SimResult {
    int underruns = 0;            // over the whole run
    int underrunsAfterWarmup = 0; // after the first ten seconds
    int dropped = 0;
    int maxBufferedLastSecondMs = 0; // queue + sink, the latency a listener hears
    int maxBufferedAfterStallMs = 0; // max over the second that starts 1 s after the stall ends
};

SimResult simulate(const SimConfig &c) {
    using namespace RxPlayout;
    const int pkt = c.packetMs * BYTES_PER_MS;
    const int cap = c.legacy ? 500 * BYTES_PER_MS : sinkBufferBytes(c.packetMs);
    constexpr int kMaxQueue = 1000 * BYTES_PER_MS;

    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> jitter(0, std::max(0, c.jitterMs));
    std::vector<int> arrivals;
    int last = 0;
    for (int due = c.packetMs; due < c.durationMs; due += c.packetMs) {
        int at = due + jitter(rng);
        if (c.stallAtMs >= 0 && due >= c.stallAtMs && due < c.stallAtMs + c.stallMs)
            at = c.stallAtMs + c.stallMs + (due - c.stallAtMs) / c.packetMs; // the backlog, at link speed
        at = std::max(at, last);
        last = at;
        arrivals.push_back(at);
    }

    SimResult r;
    std::deque<int> queue; // bytes left in each queued packet
    int queued = 0;
    int sink = 0;
    bool playing = false;
    size_t next = 0;
    const int stallEnd = c.stallAtMs + c.stallMs;

    for (int t = 0; t < c.durationMs; ++t) {
        while (next < arrivals.size() && arrivals[next] <= t) {
            ++next;
            if (c.legacy && queued + pkt > HIGH_WATER_PACKETS * pkt) { // the old marks, queue only
                while (queued > TARGET_PACKETS * pkt && !queue.empty()) {
                    queued -= queue.front();
                    queue.pop_front();
                    ++r.dropped;
                }
            }
            while (queued + pkt > kMaxQueue && !queue.empty()) {
                queued -= queue.front();
                queue.pop_front();
                ++r.dropped;
            }
            queue.push_back(pkt);
            queued += pkt;
        }

        if (t % FEED_INTERVAL_MS == 0 && !queue.empty()) {
            if (!c.legacy && overHighWater(queued, sink, 0, pkt)) {
                while (queued + sink > targetBytes(pkt) && queue.size() > 1) {
                    queued -= queue.front();
                    queue.pop_front();
                    ++r.dropped;
                }
            }
            while (!queue.empty()) {
                const int n = c.legacy ? (queue.front() <= cap - sink ? queue.front() : 0)
                                       : bytesToWrite(queue.front(), cap - sink, sink);
                if (n <= 0)
                    break;
                sink += n;
                queued -= n;
                queue.front() -= n;
                if (queue.front() > 0)
                    break;
                queue.pop_front();
            }
            playing = playing || sink > 0;
        }

        if (t % c.devicePeriodMs == 0 && playing) {
            const int need = c.devicePeriodMs * BYTES_PER_MS;
            if (sink < need) {
                ++r.underruns;
                if (t >= 10000)
                    ++r.underrunsAfterWarmup;
            }
            sink -= std::min(sink, need);
        }

        const int bufferedMs = (queued + sink) / BYTES_PER_MS;
        if (t >= c.durationMs - 1000)
            r.maxBufferedLastSecondMs = std::max(r.maxBufferedLastSecondMs, bufferedMs);
        if (c.stallAtMs >= 0 && t >= stallEnd + 1000 && t < stallEnd + 2000)
            r.maxBufferedAfterStallMs = std::max(r.maxBufferedAfterStallMs, bufferedMs);
    }
    return r;
}

} // namespace

class TestRxPlayout : public QObject {
    Q_OBJECT
private slots:
    void sinkSizePerSlTier_data();
    void sinkSizePerSlTier();
    void jitterMarksScaleWithPacket();
    void bytesToWriteWholePacketWhenItFits();
    void bytesToWritePartialOnlyWhenStarving();
    void burstBacklogIsTrimmed_data();
    void burstBacklogIsTrimmed();
    void steadyJitterSettlesWithoutUnderruns_data();
    void steadyJitterSettlesWithoutUnderruns();
};

void TestRxPlayout::sinkSizePerSlTier_data() {
    QTest::addColumn<int>("sl");
    QTest::addColumn<int>("sinkMs");
    QTest::newRow("SL0") << 0 << 50;
    QTest::newRow("SL1") << 1 << 70;
    QTest::newRow("SL2") << 2 << 70;
    QTest::newRow("SL3") << 3 << 90;
    QTest::newRow("SL5") << 5 << 90;
    QTest::newRow("SL6") << 6 << 150;
    QTest::newRow("SL7") << 7 << 150;
}

void TestRxPlayout::sinkSizePerSlTier() {
    QFETCH(int, sl);
    QFETCH(int, sinkMs);
    const int packetMs = RxPlayout::packetMsForFrameSamples(RadioUtils::slTierToFrameSamples(sl));
    QCOMPARE(RxPlayout::sinkBufferMs(packetMs), sinkMs);
    // One whole packet must always fit with the margin left over, or the feed could never write.
    QVERIFY(RxPlayout::sinkBufferBytes(packetMs) >=
            packetMs * RxPlayout::BYTES_PER_MS + RxPlayout::DEVICE_MARGIN_MS * RxPlayout::BYTES_PER_MS);
    // Far below the 500 ms it replaced, for every tier.
    QVERIFY(RxPlayout::sinkBufferMs(packetMs) <= 150);
}

void TestRxPlayout::jitterMarksScaleWithPacket() {
    // In packets, never fixed milliseconds: a fixed target would sit below one SL7 packet and drop
    // every packet at high SL.
    QCOMPARE(RxPlayout::targetBytes(1920), 2 * 1920);
    QCOMPARE(RxPlayout::targetBytes(11520), 2 * 11520);
    QVERIFY(RxPlayout::targetBytes(11520) > 11520);
    // Five packets, capped at 300 ms but never within one packet of the target.
    QCOMPARE(RxPlayout::highWaterBytes(1920), 5 * 1920);   // SL0: 100 ms
    QCOMPARE(RxPlayout::highWaterBytes(3840), 5 * 3840);   // SL1-2: 200 ms
    QCOMPARE(RxPlayout::highWaterBytes(5760), 5 * 5760);   // SL3-5: 300 ms
    QCOMPARE(RxPlayout::highWaterBytes(11520), 3 * 11520); // SL6-7: 360 ms, not 600
    QCOMPARE(RxPlayout::highWaterBytes(0), 0);
    QCOMPARE(RxPlayout::targetBytes(0), 0);
    QCOMPARE(RxPlayout::targetBytes(-5), 0);
    QVERIFY(!RxPlayout::overHighWater(4 * 1920, 0, 1920, 1920));
    QVERIFY(RxPlayout::overHighWater(4 * 1920, 1, 1920, 1920));
    // The sink counts: the same queue is over the mark once the sink holds audio too.
    QVERIFY(RxPlayout::overHighWater(3 * 1920, 3 * 1920, 0, 1920));
    QVERIFY(!RxPlayout::overHighWater(100000, 100000, 0, 0));
}

void TestRxPlayout::bytesToWriteWholePacketWhenItFits() {
    QCOMPARE(RxPlayout::bytesToWrite(1920, 4800, 0), 1920);
    QCOMPARE(RxPlayout::bytesToWrite(1920, 1920, 2880), 1920);
    QCOMPARE(RxPlayout::bytesToWrite(0, 4800, 0), 0);
    QCOMPARE(RxPlayout::bytesToWrite(1920, 0, 4800), 0);
}

void TestRxPlayout::bytesToWritePartialOnlyWhenStarving() {
    const int margin = RxPlayout::DEVICE_MARGIN_MS * RxPlayout::BYTES_PER_MS;
    // Plenty still in the sink: wait for room for the whole packet.
    QCOMPARE(RxPlayout::bytesToWrite(11520, 5000, margin + 8), 0);
    // About to starve: write what fits, in whole stereo frames.
    QCOMPARE(RxPlayout::bytesToWrite(11520, 5003, margin), 5000);
    QCOMPARE(RxPlayout::bytesToWrite(11520, 5003, 0) % RxPlayout::FRAME_BYTES, 0);
}

void TestRxPlayout::burstBacklogIsTrimmed_data() {
    QTest::addColumn<int>("packetMs");
    QTest::addColumn<int>("devicePeriodMs");
    for (int p : {20, 40, 60, 120}) {
        for (int d : {10, 21}) {
            QTest::newRow(qPrintable(QStringLiteral("%1ms packets, %2ms device").arg(p).arg(d))) << p << d;
        }
    }
}

// A 400 ms stall (an I/O thread or Wi-Fi hiccup) then everything at once. The old engine moved the
// burst into its 500 ms sink, out of the policy's reach, and played it late for the rest of the
// session. The new one must be back to the target within a second of the burst.
void TestRxPlayout::burstBacklogIsTrimmed() {
    QFETCH(int, packetMs);
    QFETCH(int, devicePeriodMs);
    SimConfig c;
    c.packetMs = packetMs;
    c.devicePeriodMs = devicePeriodMs;
    c.jitterMs = 5;
    c.stallAtMs = 5000;
    c.stallMs = 400;
    const SimResult now = simulate(c);
    c.legacy = true;
    const SimResult before = simulate(c);

    qInfo("%3d ms packets, %2d ms device: after a 400 ms burst, buffered %3d ms (was %3d ms); "
          "end of run %3d ms (was %3d ms); dropped %d (was %d); underruns %d (was %d)",
          packetMs, devicePeriodMs, now.maxBufferedAfterStallMs, before.maxBufferedAfterStallMs,
          now.maxBufferedLastSecondMs, before.maxBufferedLastSecondMs, now.dropped, before.dropped, now.underruns,
          before.underruns);

    // Bounded by the high-water mark plus one packet in flight - never the 500 ms sink.
    const int bound =
        RxPlayout::highWaterBytes(packetMs * RxPlayout::BYTES_PER_MS) / RxPlayout::BYTES_PER_MS + packetMs;
    QVERIFY2(now.maxBufferedAfterStallMs <= bound, qPrintable(QString::number(now.maxBufferedAfterStallMs)));
    QVERIFY(now.maxBufferedLastSecondMs <= bound);
    // And strictly less than the backlog the old engine kept.
    QVERIFY(now.maxBufferedAfterStallMs < before.maxBufferedAfterStallMs);
}

void TestRxPlayout::steadyJitterSettlesWithoutUnderruns_data() {
    QTest::addColumn<int>("packetMs");
    QTest::addColumn<int>("devicePeriodMs");
    QTest::addColumn<int>("jitterMs");
    // LAN (a few ms) and Wi-Fi (tens of ms) arrival jitter, against both device periods.
    for (int p : {20, 40, 60, 120}) {
        for (int d : {10, 21}) {
            for (int j : {3, 15, 30}) {
                QTest::newRow(qPrintable(QStringLiteral("%1ms/%2ms/jitter %3").arg(p).arg(d).arg(j))) << p << d << j;
            }
        }
    }
}

// Jitter makes the first packets underrun (playback starts after one packet, by design, for the
// lowest latency); each underrun moves playout later until the buffer covers the jitter. After that
// warm-up there must be none, or the sink is too small for the device.
void TestRxPlayout::steadyJitterSettlesWithoutUnderruns() {
    QFETCH(int, packetMs);
    QFETCH(int, devicePeriodMs);
    QFETCH(int, jitterMs);
    SimConfig c;
    c.packetMs = packetMs;
    c.devicePeriodMs = devicePeriodMs;
    c.jitterMs = jitterMs;
    c.durationMs = 60000;
    const SimResult r = simulate(c);
    QVERIFY2(r.underrunsAfterWarmup == 0,
             qPrintable(
                 QStringLiteral("%1 underruns after warm-up, %2 total").arg(r.underrunsAfterWarmup).arg(r.underruns)));
    QVERIFY(r.maxBufferedLastSecondMs <=
            RxPlayout::highWaterBytes(packetMs * RxPlayout::BYTES_PER_MS) / RxPlayout::BYTES_PER_MS + packetMs);
}

QTEST_GUILESS_MAIN(TestRxPlayout)
#include "test_rxplayout.moc"
