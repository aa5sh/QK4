// SPDX-License-Identifier: GPL-3.0-or-later
//
// Production implementations of the seams in keyertiming.h: a steady clock with precise sleeps,
// a link onto the K4 connection's KZ egress, and a one-shot timer on the main thread. The tests use virtual
// ones instead (tests/paddlekeyersim.h).

#ifndef KEYERRUNTIME_H
#define KEYERRUNTIME_H

#include "hardware/keyertiming.h"

#include <QObject>
#include <atomic>
#include <functional>

// std::chrono::steady_clock in seconds. sleep() is as precise as the platform allows: a
// high-resolution waitable timer on Windows (the default timer tick there is 15.6 ms), and the
// native sleep elsewhere. The keyer learns and subtracts whatever overrun is left (wake_overrun).
class SteadyKeyerClock : public CwKeying::Clock {
public:
    double now() override;
    void sleep(double seconds) override;
};

// Forwards each command to a target link set at runtime (the K4 connection's KZ egress), or drops
// it while there is none. The target is an atomic pointer, so the keyer thread sends without a lock
// while the main thread wires it. The target must outlive every send: it is cleared only after
// the keyer thread is joined.
class ForwardingLink : public CwKeying::Link {
public:
    void setTarget(CwKeying::Link *target);
    void send(std::string_view command) override;

private:
    std::atomic<CwKeying::Link *> m_target{nullptr};
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
