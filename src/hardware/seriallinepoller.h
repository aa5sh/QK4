// SPDX-License-Identifier: GPL-3.0-or-later
//
// Modem-line change detection for a serial paddle, ported from K4-Companion's SerialInput signal
// loop (GPL-3.0). Polled every 1 ms by the serial worker. The lines' state at open is taken as
// rest and never reported, so an adapter that idles with DSR+DCD asserted does not look like a
// held squeeze. Paddles are reported on the poll that sees the change; a straight key waits for
// 20 ms of quiet. Each report carries the time since that line's previous report.

#ifndef SERIALLINEPOLLER_H
#define SERIALLINEPOLLER_H

#include "hardware/paddlekeyer.h"

#include <functional>
#include <optional>
#include <vector>

class SerialLinePoller {
public:
    using ReadLine = std::function<bool(PaddleKeyer::Signal)>;

    struct Change {
        PaddleKeyer::Signal signal;
        bool asserted;
        double deltaTime;
    };

    static constexpr double kStraightKeyDebounceSeconds = 0.020;

    SerialLinePoller(PaddleKeyer::Signal dit, PaddleKeyer::Signal dah, bool straightKeying);

    // Reads the rest state. False if the port could not be read.
    bool start(const ReadLine &read);
    // One poll at time `now`. Returns what to report, dit first.
    std::vector<Change> poll(const ReadLine &read, double now);

private:
    PaddleKeyer::Signal m_dit;
    PaddleKeyer::Signal m_dah;
    bool m_straight;
    double m_debounce;
    bool m_ditState = false;
    bool m_dahState = false;
    std::optional<double> m_ditTime;
    std::optional<double> m_dahTime;
    std::optional<double> m_previousDitTime;
    std::optional<double> m_previousDahTime;
};

#endif // SERIALLINEPOLLER_H
