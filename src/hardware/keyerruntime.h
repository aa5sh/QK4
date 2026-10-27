// SPDX-License-Identifier: GPL-3.0-or-later
//
// Production implementations of the seams in keyertiming.h: a steady clock with precise sleeps,
// a link onto the K4 connection, and a one-shot timer on the main thread. The tests use virtual
// ones instead (tests/paddlekeyersim.h).

#ifndef KEYERRUNTIME_H
#define KEYERRUNTIME_H

#include "hardware/keyertiming.h"

#include <QObject>
#include <atomic>
#include <functional>
#include <mutex>

// std::chrono::steady_clock in seconds. sleep() is as precise as the platform allows: a
// high-resolution waitable timer on Windows (the default timer tick there is 15.6 ms), and the
// native sleep elsewhere. The keyer learns and subtracts whatever overrun is left (wake_overrun).
class SteadyKeyerClock : public CwKeying::Clock {
public:
    double now() override;
    void sleep(double seconds) override;
};

// Forwards each command to a sender set at runtime. Thread-safe: the keyer thread sends while the
// main thread may be (re)wiring the sender.
class FunctionLink : public CwKeying::Link {
public:
    using Sender = std::function<void(const std::string &)>;
    void setSender(Sender sender);
    void send(const std::string &command) override;

private:
    std::mutex m_mutex;
    Sender m_sender;
};

// One-shot timer whose callback runs on the thread this object lives on (the main thread).
// start() and cancel() may be called from any thread. A cancel, or a later start, makes a timer
// already in flight fire into nothing.
class QtOneShotTimer : public QObject, public CwKeying::OneShotTimer {
    Q_OBJECT

public:
    explicit QtOneShotTimer(QObject *parent = nullptr);
    void start(double delaySeconds, std::function<void()> fire) override;
    void cancel() override;

private:
    std::atomic<quint64> m_generation{0};
};

#endif // KEYERRUNTIME_H
