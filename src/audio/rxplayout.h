#ifndef RXPLAYOUT_H
#define RXPLAYOUT_H

// RX playout sizing and the jitter buffer. Header-only, Qt-free, so the numbers can be tested
// without an audio device.
//
// The K4 sends RX audio in bundles whose length is set by the SL tier: 20, 40, 60 or 120 ms of audio
// per packet (utils/radioutils.h, slTierToFrameSamples). Sizes that follow from the packet are
// expressed in that packet length, never in fixed milliseconds, because a fixed size would be wrong
// for three of the four tiers. The jitter headroom is not a size of that kind: it is measured
// (JitterBuffer, below).
//
// Total RX latency is everything buffered between the socket and the speaker: the app queue plus
// the QAudioSink buffer. Only the queue can be trimmed - audio already written to the sink plays.
// So the sink is kept as small as it can be without starving the device, and the queue holds the
// rest, where the jitter buffer can reach it.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace RxPlayout {

// 12 kHz x 2 channels x sizeof(float).
constexpr int BYTES_PER_MS = 96;
constexpr int FRAME_BYTES = 8; // one stereo Float32 sample pair

// The audio thread tops the sink up on a Qt::PreciseTimer at this period.
constexpr int FEED_INTERVAL_MS = 5;

// WHY 30 ms: what the sink must still hold at the moment a whole packet is written into it, so the
// device does not run dry before the packet lands. It covers, added up:
//   - one feed period (5 ms): the feed only looks at the sink once per period;
//   - one backend period (~21 ms): Qt's ring is drained by the OS audio thread a period at a time.
//     The largest common default is PipeWire's 1024-frame quantum at 48 kHz (21.3 ms); CoreAudio's
//     512 frames at 48 kHz is 10.7 ms and WASAPI shared mode 10 ms;
//   - a few ms of wake-up lateness on the (non-real-time) audio thread.
// Bluetooth adds its own latency AFTER this buffer, inside the OS, which no setting here changes.
constexpr int DEVICE_MARGIN_MS = 30;

// Packet length in ms for a frame size in samples per channel at 12 kHz (240 -> 20 ms).
constexpr int packetMsForFrameSamples(int frameSamples) {
    return frameSamples > 0 ? frameSamples / 12 : 20;
}

// The QAudioSink buffer for a packet length: one whole packet plus the device margin.
//
// WHY one packet: the feed writes whole packets, so the sink must be able to take one while it
// still holds the margin. Anything above that is latency the queue's trimming cannot touch - which
// is how the old fixed 500 ms buffer let a network burst play up to half a second late.
//   SL0 (20 ms) -> 50 ms, SL1-2 (40 ms) -> 70 ms, SL3-5 (60 ms) -> 90 ms, SL6-7 (120 ms) -> 150 ms.
constexpr int sinkBufferMs(int packetMs) {
    return packetMs + DEVICE_MARGIN_MS;
}

constexpr int sinkBufferBytes(int packetMs) {
    return sinkBufferMs(packetMs) * BYTES_PER_MS;
}

// The backlog trim's floor, in packets of the current tier: the policy below discards a backlog at
// once (rather than easing it down) only when queue + sink passes the larger of this mark and one
// packet over its own target depth. The mark is the one the adapt-by-underrun policy trimmed at,
// kept so a burst is still cut back promptly: five packets, capped at 300 ms, never under three.
constexpr int HIGH_WATER_PACKETS = 5;
constexpr int HIGH_WATER_MIN_PACKETS = 3;

// WHY a ceiling in ms as well: five packets is 100 ms at SL0 but 600 ms at SL7, and a backlog that
// size is the late audio this policy exists to remove. Above the ceiling the mark is three packets.
constexpr int HIGH_WATER_MAX_MS = 300;

constexpr int highWaterBytes(int pktBytes) {
    if (pktBytes <= 0)
        return 0;
    const int byPackets = HIGH_WATER_PACKETS * pktBytes;
    const int ceiling = HIGH_WATER_MAX_MS * BYTES_PER_MS;
    const int floor = HIGH_WATER_MIN_PACKETS * pktBytes;
    const int capped = byPackets < ceiling ? byPackets : ceiling;
    return capped > floor ? capped : floor;
}

// How many bytes of the head packet to write this feed tick.
//
// A whole packet when it fits. When it does not, and the sink has fallen to the margin (it is about
// to starve), as much as fits: this keeps audio flowing if a backend grants a buffer smaller than
// one packet, which bufferSize() after start() may report (Qt: "It should not be assumed that the
// buffer size set is the actual buffer size used"). Otherwise nothing - wait for room.
constexpr int bytesToWrite(int headBytes, int bytesFree, int sinkFillBytes) {
    if (headBytes <= 0 || bytesFree <= 0)
        return 0;
    if (headBytes <= bytesFree)
        return headBytes;
    if (sinkFillBytes <= DEVICE_MARGIN_MS * BYTES_PER_MS)
        return bytesFree - bytesFree % FRAME_BYTES; // whole stereo frames only
    return 0;
}

// The jitter buffer: how much audio to hold, derived from the jitter actually measured, and the
// two corrections that keep the buffer there - silence written ahead of the next packet to grow it,
// queued audio skipped to shrink it. Fixed-size state, no allocation, no lock, no I/O: it runs on
// the audio thread inside the feed tick (CONVENTIONS.md Rule 16). Driven identically by AudioEngine
// and by the simulation in tests/test_rxplayout.cpp.
//
// WHY measured jitter rather than adapting by underrun: the policy it replaced started after one
// packet and moved playout later only when the sink ran dry, so it ended each session exactly as
// deep as the worst lateness seen so far and no deeper - the next, slightly later packet underran,
// at any time, however long the session (about one session in nine at SL1-2 with 30 ms of Wi-Fi
// jitter on a 21 ms device). This one holds the spread of arrival times seen over the last ten
// seconds, as VoIP jitter buffers do, so a packet as late as any recent one is already covered.
//
// Terms (all audio in bytes of decoded 12 kHz stereo Float32, all times in microseconds):
//
//   relative delay  d = (when the feed first saw a packet) - (the stream's start + the audio sent
//                   before it). A constant network delay cancels out; what is left varies only
//                   with jitter (and slowly with clock drift, which the window follows).
//   jitter J        max d - min d over a 10 s window of 1 s buckets: a peak held for ten seconds,
//                   then dropped bucket by bucket. Grows on the packet that shows it.
//   lateness        d - min d for one packet: how much later than the earliest recent packet.
//   delay L         per packet: the audio ahead of it, net of what the device has already claimed,
//                   plus its lateness. It is the delay an ON-TIME packet gets, so it is comparable
//                   across packets whatever each one's own lateness. No underrun is possible while
//                   L - lateness >= 0 for every packet; L >= J keeps that true for any packet no
//                   later than the recent worst.
//   target          J + TARGET_SLACK_MS, bounded to [TARGET_SLACK_MS, JITTER_MAX_MS + TARGET_SLACK_MS].
//                   Total latency for an on-time packet is target + its own length (+ device).
//
// "Net of what the device has already claimed": the OS takes audio out of the sink a device period
// at a time (10.7 ms CoreAudio, 10 ms WASAPI, 21.3 ms PipeWire), so the raw sink fill jumps by a
// period at each pull. Measuring against the raw fill made the target swing with the phase between
// pulls and packets. Instead the policy notes when it last saw a pull and subtracts the audio played
// since: the margin left at the NEXT pull, which is what decides an underrun. When the sink has been
// empty the device has no phase to measure, its next pull can come at once, and a whole period -
// the largest drain seen, or DEVICE_PERIOD_PRIOR_MS before one is seen - counts as claimed.
//
// Arrival time is the feed tick that first sees the packet, not the I/O thread's receive time: the
// jitter the playout must absorb includes the feed's own 5 ms granularity and any lateness of its
// timer, so measuring there counts both, and nothing crosses threads to do it.
//
// Corrections, each at a point where the audio is already discontinuous or about to be:
//   - grow: a packet whose L is below target gets (target - L) + GROW_HEADROOM_MS of silence
//     written ahead of it. At the start and after an underrun this IS the prebuffer; during play it
//     replaces the underrun the shortfall would otherwise cause later. The headroom keeps a device
//     clock that runs fast from asking for a sub-millisecond insert every few seconds.
//   - shrink: once a second, if the smallest L over that second exceeded the target by at least
//     SHRINK_MIN_MS, skip the excess (at most SHRINK_STEP_MAX_MS) from the head of the queue. So a
//     jitter spike that has left the window is paid back at up to 40 ms a second, not at once.
//   - backlog trim: when the buffered audio passes the larger of highWaterBytes() and one packet
//     over target depth, skip at once to target depth. This is the burst after a stall.
//
// A packet more than JITTER_MAX_MS later than the window's earliest is a stall, not jitter, and is
// not planned for: a 400 ms Wi-Fi hiccup would otherwise hold 400 ms of latency for ten seconds.
// Its packets, and the backlog arriving behind it, stay out of the window until the stream is back
// on its old timeline. If it is not back within STALL_SETTLE_MS (the radio stopped sending - we
// transmitted - rather than the network holding packets back) the timeline is re-anchored at the
// stall's earliest packet, keeping the jitter learned before it.
class JitterBuffer {
public:
    static constexpr int TARGET_SLACK_MS = 2;
    static constexpr int GROW_HEADROOM_MS = 3;
    static constexpr int SHRINK_MIN_MS = 5;
    static constexpr int SHRINK_STEP_MAX_MS = 40;
    static constexpr int JITTER_MAX_MS = 200;
    static constexpr int STALL_SETTLE_MS = 1000;
    // WHY 25 ms: above the largest common device period (PipeWire's 1024 frames at 48 kHz, 21.3 ms),
    // so the first packet's prebuffer cannot fall short before a real pull has been seen. Used only
    // until then; any excess goes by the shrink a second later.
    static constexpr int DEVICE_PERIOD_PRIOR_MS = 25;
    static constexpr int DEVICE_PERIOD_MAX_MS = 60; // a larger drain is a rebuild or a glitch
    static constexpr int WINDOW_BUCKETS = 10;
    static constexpr std::int64_t BUCKET_US = 1000000;
    static constexpr std::int64_t SHRINK_PERIOD_US = 1000000;

    void reset() { *this = JitterBuffer(); }

    // 1. Every feed tick, first: the sink's fill before anything is written this tick.
    void observeSink(int sinkFillBytes, std::int64_t nowUs) {
        if (m_haveWrite && sinkFillBytes < m_fillAfterWrite) {
            const int drain = m_fillAfterWrite - sinkFillBytes;
            if (drain <= DEVICE_PERIOD_MAX_MS * BYTES_PER_MS && drain > m_devicePeriodBytes)
                m_devicePeriodBytes = drain;
        }
        if (!m_haveWrite || sinkFillBytes == 0)
            m_lastPullUs = nowUs - usFor(devicePeriodBytes()); // idle: the next pull may come at once
        else if (sinkFillBytes < m_fillAfterWrite)
            m_lastPullUs = m_lastTickUs; // pulled since the last tick; the earliest it can have been
        m_lastTickUs = nowUs;
    }

    // 2. Once per packet, in order, on the tick that first sees it. bufferedThroughBytes is the sink
    // fill plus everything queued up to and including this packet.
    void observePacket(int packetBytes, int bufferedThroughBytes, std::int64_t nowUs) {
        if (packetBytes <= 0)
            return;
        if (!m_started) {
            m_started = true;
            m_anchorUs = nowUs;
            m_sentUs = 0;
        }
        std::int64_t d = nowUs - m_anchorUs - m_sentUs;
        m_sentUs += usFor(packetBytes);

        if (!m_inStall && m_haveWindow && d - m_windowMinUs > std::int64_t(JITTER_MAX_MS) * 1000) {
            m_inStall = true;
            m_stallSinceUs = nowUs;
            m_stallMinUs = d;
            ++m_stalls;
        }
        if (m_inStall) {
            m_stallMinUs = std::min(m_stallMinUs, d);
            if (d - m_windowMinUs <= m_jitterUs + std::int64_t(TARGET_SLACK_MS) * 1000) {
                m_inStall = false; // back on the old timeline: the network held packets back
            } else if (nowUs - m_stallSinceUs >= std::int64_t(STALL_SETTLE_MS) * 1000) {
                const std::int64_t shift = m_stallMinUs - m_windowMinUs; // the radio paused
                m_anchorUs += shift;
                d -= shift;
                m_inStall = false;
            }
        }
        std::int64_t latenessUs;
        if (m_inStall) {
            latenessUs = d - m_stallMinUs;
            m_shrinkValid = false;
        } else {
            addToWindow(nowUs, d);
            latenessUs = d - m_windowMinUs;
        }

        const std::int64_t delay = std::int64_t(bufferedThroughBytes) + m_pendingSilence - m_pendingSkip - packetBytes -
                                   bytesFor(sinceLastPullUs(nowUs)) + bytesFor(latenessUs);
        const int target = targetBytes();
        if (delay < target) {
            int grow = alignUp(int(target - delay) + GROW_HEADROOM_MS * BYTES_PER_MS);
            const int cancelled = std::min(m_pendingSkip, grow);
            m_pendingSkip -= cancelled;
            grow -= cancelled;
            m_pendingSilence += grow;
            ++m_silenceInserts;
            m_shrinkValid = false;
        } else if (!m_inStall) {
            if (!m_shrinkValid) {
                m_shrinkValid = true;
                m_shrinkMinDelay = delay;
                m_shrinkSinceUs = nowUs;
            } else {
                m_shrinkMinDelay = std::min(m_shrinkMinDelay, delay);
            }
        }
    }

    // 3. After the tick's packets are observed, when anything is queued. bufferedBytes is sink fill +
    // queue; packetBytes the current tier's packet.
    void decide(int bufferedBytes, int packetBytes, std::int64_t nowUs) {
        const int target = targetBytes();
        const int depth = target + packetBytes;
        const int trimAbove = std::max(highWaterBytes(packetBytes), depth + packetBytes);
        const int net = bufferedBytes + m_pendingSilence - m_pendingSkip - bytesFor(sinceLastPullUs(nowUs));
        if (net > trimAbove) {
            m_pendingSkip += alignDown(net - depth);
            m_shrinkValid = false;
            ++m_backlogTrims;
            return;
        }
        if (m_shrinkValid && nowUs - m_shrinkSinceUs >= SHRINK_PERIOD_US) {
            const std::int64_t excess = m_shrinkMinDelay - target;
            if (excess >= SHRINK_MIN_MS * BYTES_PER_MS) {
                m_pendingSkip += alignDown(int(std::min<std::int64_t>(excess, SHRINK_STEP_MAX_MS * BYTES_PER_MS)));
                ++m_shrinkSkips;
            }
            m_shrinkValid = false;
        }
    }

    // 4. Bytes to discard from the head of the queue now, whole stereo frames, at most what is
    // queued. Whatever cannot be applied now is dropped: it was measured against this queue.
    int takeSkip(int queuedBytes) {
        const int n = alignDown(std::max(0, std::min(m_pendingSkip, queuedBytes)));
        m_pendingSkip = 0;
        return n;
    }

    // 5. Silence to write to the sink before the next packet. Packets wait until it is all written.
    int pendingSilenceBytes() const { return m_pendingSilence; }
    void silenceWritten(int bytes) { m_pendingSilence -= std::max(0, std::min(bytes, m_pendingSilence)); }

    // 6. Last, every tick: the sink's fill after this tick's writes.
    void noteWritten(int sinkFillBytes) {
        m_fillAfterWrite = sinkFillBytes;
        m_haveWrite = true;
    }

    int targetBytes() const {
        const std::int64_t j = std::min<std::int64_t>(m_jitterUs, std::int64_t(JITTER_MAX_MS) * 1000);
        return alignUp(bytesFor(j) + TARGET_SLACK_MS * BYTES_PER_MS);
    }
    int jitterMs() const { return int(m_jitterUs / 1000); }
    int devicePeriodBytes() const {
        return m_devicePeriodBytes > 0 ? m_devicePeriodBytes : DEVICE_PERIOD_PRIOR_MS * BYTES_PER_MS;
    }
    bool devicePeriodMeasured() const { return m_devicePeriodBytes > 0; }
    bool inStall() const { return m_inStall; }

    // Corrections since reset, for diagnostics and tests.
    int silenceInserts() const { return m_silenceInserts; }
    int shrinkSkips() const { return m_shrinkSkips; }
    int backlogTrims() const { return m_backlogTrims; }
    int stalls() const { return m_stalls; }

private:
    struct Bucket {
        std::int64_t id = -1;
        std::int64_t minUs = 0;
        std::int64_t maxUs = 0;
    };

    static constexpr int bytesFor(std::int64_t us) { return int(us * BYTES_PER_MS / 1000); }
    static constexpr std::int64_t usFor(int bytes) { return std::int64_t(bytes) * 1000 / BYTES_PER_MS; }
    static constexpr int alignDown(int bytes) { return bytes - bytes % FRAME_BYTES; }
    static constexpr int alignUp(int bytes) { return alignDown(bytes + FRAME_BYTES - 1); }

    std::int64_t sinceLastPullUs(std::int64_t nowUs) const {
        return std::clamp<std::int64_t>(nowUs - m_lastPullUs, 0, std::int64_t(DEVICE_PERIOD_MAX_MS) * 1000);
    }

    void addToWindow(std::int64_t nowUs, std::int64_t d) {
        const std::int64_t id = nowUs / BUCKET_US;
        Bucket &b = m_buckets[static_cast<std::size_t>(id % WINDOW_BUCKETS)];
        if (b.id != id) {
            b = Bucket{id, d, d};
        } else {
            b.minUs = std::min(b.minUs, d);
            b.maxUs = std::max(b.maxUs, d);
        }
        std::int64_t lo = d;
        std::int64_t hi = d;
        for (const Bucket &x : m_buckets) {
            if (x.id <= id - WINDOW_BUCKETS || x.id < 0)
                continue; // expired, or never used
            lo = std::min(lo, x.minUs);
            hi = std::max(hi, x.maxUs);
        }
        m_windowMinUs = lo;
        m_jitterUs = hi - lo;
        m_haveWindow = true;
    }

    std::array<Bucket, WINDOW_BUCKETS> m_buckets{};
    bool m_haveWindow = false;
    std::int64_t m_windowMinUs = 0;
    std::int64_t m_jitterUs = 0;

    bool m_started = false;
    std::int64_t m_anchorUs = 0;
    std::int64_t m_sentUs = 0; // audio received since the anchor, as playing time

    bool m_inStall = false;
    std::int64_t m_stallSinceUs = 0;
    std::int64_t m_stallMinUs = 0;

    int m_devicePeriodBytes = 0; // largest drain seen between two ticks; 0 = none yet
    int m_fillAfterWrite = 0;
    bool m_haveWrite = false;
    std::int64_t m_lastPullUs = 0;
    std::int64_t m_lastTickUs = 0;

    int m_pendingSilence = 0;
    int m_pendingSkip = 0;

    bool m_shrinkValid = false;
    std::int64_t m_shrinkMinDelay = 0;
    std::int64_t m_shrinkSinceUs = 0;

    int m_silenceInserts = 0;
    int m_shrinkSkips = 0;
    int m_backlogTrims = 0;
    int m_stalls = 0;
};

} // namespace RxPlayout

#endif // RXPLAYOUT_H
