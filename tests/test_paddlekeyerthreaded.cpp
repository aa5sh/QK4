// SPDX-License-Identifier: GPL-3.0-or-later
//
// PaddleKeyer as the app runs it: its own thread, the real steady clock and its sleeps, paddle
// events from another thread, releases and close from a third. The virtual-clock suites pin what
// is keyed; this one pins that the threads agree. That covers the locking, the queue wake-up and
// the shutdown join. Run it under ThreadSanitizer (scheduled-sanitizers.yml) as well as plainly.
// Timing assertions are loose on purpose: a loaded CI runner is not a quiet bench.

#include "audio/sidetonescheduler.h"
#include "hardware/keyerruntime.h"
#include "hardware/paddlekeyer.h"

#include <QtTest/QtTest>
#include <atomic>
#include <mutex>
#include <thread>

namespace {

class CaptureLink : public CwKeying::Link {
public:
    void send(const std::string &c) override {
        std::lock_guard lock(m_mutex);
        m_sent.push_back(c);
    }
    std::vector<std::string> sent() {
        std::lock_guard lock(m_mutex);
        return m_sent;
    }

private:
    std::mutex m_mutex;
    std::vector<std::string> m_sent;
};

class NullTimer : public CwKeying::OneShotTimer {
public:
    void start(double, std::function<void()>) override {}
    void cancel() override {}
};

struct Rig {
    SteadyKeyerClock clock;
    CaptureLink link;
    NullTimer timer;
    // No audio stream: sidetone requests are refused and the keyer works from its own estimates,
    // as it does while no output device is open.
    SidetoneScheduler sidetone{clock, []() -> std::optional<double> { return std::nullopt; }};
    PaddleKeyer keyer{clock, link, sidetone, timer};
    std::thread thread;

    explicit Rig(int wpm = 40) {
        keyer.setSpeed(wpm);
        thread = std::thread([this] { keyer.run(); });
    }
    ~Rig() {
        if (thread.joinable()) {
            keyer.close();
            thread.join();
        }
    }
    void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
};

int openKzd(const std::vector<std::string> &sent) {
    int open = 0;
    for (const auto &c : sent)
        open += c.rfind("KZD", 0) == 0 ? 1 : c.rfind("KZU", 0) == 0 ? -1 : 0;
    return open;
}

} // namespace

class TestPaddleKeyerThreaded : public QObject {
    Q_OBJECT

private slots:
    // A dit held for 200 ms at 40 WPM: a run of dits, each KZU exactly 30 ms, the gaps 30 ms,
    // and nothing left open after the release.
    void heldDitOnRealThreads() {
        Rig rig(40);
        std::thread device([&] {
            rig.keyer.paddleEvent(PaddleKeyer::Event::DitDown, 0);
            rig.sleepMs(200);
            rig.keyer.paddleEvent(PaddleKeyer::Event::DitUp, 0);
        });
        device.join();
        rig.sleepMs(150);
        const auto sent = rig.link.sent();
        QVERIFY(sent.size() >= 6);
        QCOMPARE(sent.front(), std::string("KZD0000;"));
        for (size_t i = 1; i < sent.size(); ++i) {
            if (sent[i].rfind("KZU", 0) == 0)
                QCOMPARE(sent[i], std::string("KZU0030;"));
            else
                QCOMPARE(sent[i], std::string("KZD0030;"));
        }
        QCOMPARE(openKzd(sent), 0);
    }

    // A squeeze from the device thread while the main thread releases mid-burst: the radio is
    // left unkeyed and the keyer thread still exits on close.
    void releaseFromAnotherThreadMidBurst() {
        Rig rig(30);
        std::thread device([&] {
            rig.keyer.paddleEvent(PaddleKeyer::Event::DitDown, 0);
            rig.keyer.paddleEvent(PaddleKeyer::Event::DahDown, 0);
        });
        device.join();
        rig.sleepMs(250);
        rig.keyer.releasePaddles();
        rig.sleepMs(250);
        const auto sent = rig.link.sent();
        QVERIFY(!sent.empty());
        QCOMPARE(openKzd(sent), 0);
        rig.keyer.close();
        rig.thread.join();
    }

    // Inhibit mid-burst (KPOD+ took over, mode left CW): keying stops, nothing open, and events
    // while inhibited key nothing.
    void inhibitStopsKeying() {
        Rig rig(30);
        rig.keyer.paddleEvent(PaddleKeyer::Event::DitDown, 0);
        rig.sleepMs(150);
        rig.keyer.setInhibited(true);
        rig.sleepMs(150);
        const size_t before = rig.link.sent().size();
        rig.keyer.paddleEvent(PaddleKeyer::Event::DahDown, 0);
        rig.sleepMs(150);
        const auto sent = rig.link.sent();
        QCOMPARE(sent.size(), before);
        QCOMPARE(openKzd(sent), 0);
    }

    // A straight key held at quit gets its KZU from close(), before the thread is joined.
    void closeEndsAStraightKeyHeldDown() {
        Rig rig(30);
        rig.keyer.setStraightKeying(true);
        rig.keyer.paddleEvent(PaddleKeyer::Event::DitDown, 0);
        rig.sleepMs(100);
        rig.keyer.close();
        rig.thread.join();
        const auto sent = rig.link.sent();
        QCOMPARE(sent.size(), size_t(2));
        QCOMPARE(sent[0].substr(0, 3), std::string("KZD"));
        QCOMPARE(sent[1].substr(0, 3), std::string("KZU"));
        const int ms = std::stoi(sent[1].substr(3, 4));
        QVERIFY2(ms >= 90 && ms < 1000, sent[1].c_str());
    }

    // A straight key worked on the device thread while another thread releases: the release reads
    // the key-down time the device thread writes, so ThreadSanitizer flags any read outside the
    // writer's lock. Plainly, every KZU stays within the 5 s cap and nothing is left open.
    void straightKeyReleaseRacesKeyDown() {
        Rig rig(30);
        rig.keyer.setStraightKeying(true);
        std::atomic<bool> done{false};
        std::thread device([&] {
            for (int i = 0; i < 5000; ++i) {
                rig.keyer.paddleEvent(PaddleKeyer::Event::DitDown, 0.001);
                rig.keyer.paddleEvent(PaddleKeyer::Event::DitUp, 0.001);
            }
            done = true;
        });
        while (!done)
            rig.keyer.releasePaddles();
        device.join();
        rig.keyer.close();
        rig.thread.join();
        const auto sent = rig.link.sent();
        QCOMPARE(openKzd(sent), 0);
        for (const auto &c : sent)
            QVERIFY2(std::stoi(c.substr(3, 4)) <= 5000, c.c_str());
    }

    // Close with nothing keyed returns promptly: the loop wakes on the Close event.
    void closeWhenIdleJoinsPromptly() {
        Rig rig;
        QElapsedTimer t;
        t.start();
        rig.keyer.close();
        rig.thread.join();
        QVERIFY(t.elapsed() < 500);
        QVERIFY(rig.link.sent().empty());
    }
};

QTEST_GUILESS_MAIN(TestPaddleKeyerThreaded)
#include "test_paddlekeyerthreaded.moc"
