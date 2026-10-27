// SPDX-License-Identifier: GPL-3.0-or-later
//
// Seams the paddle keyer is written against: a clock, the link to the K4, and a one-shot timer.
// Production wires them to steady_clock, TcpClient and a QTimer; the tests wire them to a virtual
// clock so a whole keying run is deterministic.
//
// The keyer and its sidetone scheduler are a port of the keyer in Dale Farnsworth's K4-Companion
// (GPL-3.0, https://github.com/DaleFarnsworth/K4-Companion). See docs/cw-keyer-port-plan.md.

#ifndef KEYERTIMING_H
#define KEYERTIMING_H

#include <cmath>
#include <functional>
#include <string>

namespace CwKeying {

// WHY seconds as double: the port has to reproduce K4-Companion's arithmetic bit for bit, so the
// golden runs in tests/data/paddlekeyer/ replay exactly. Python's float is an IEEE double and every
// expression is kept in its original order.
class Clock {
public:
    virtual ~Clock() = default;
    virtual double now() = 0;
    virtual void sleep(double seconds) = 0;
};

class Link {
public:
    virtual ~Link() = default;
    virtual void send(const std::string &command) = 0;
};

class OneShotTimer {
public:
    virtual ~OneShotTimer() = default;
    virtual void start(double delaySeconds, std::function<void()> fire) = 0;
    virtual void cancel() = 0;
};

// WHY: Python's round() rounds half to even. std::nearbyint does the same under the default
// FE_TONEAREST mode; std::lround would round half away from zero and move boundary runs.
inline long long pyRound(double x) {
    return static_cast<long long>(std::nearbyint(x));
}

} // namespace CwKeying

#endif // KEYERTIMING_H
