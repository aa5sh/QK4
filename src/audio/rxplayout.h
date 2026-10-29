#ifndef RXPLAYOUT_H
#define RXPLAYOUT_H

// RX playout sizing and jitter policy. Header-only, Qt-free, so the numbers can be tested without
// an audio device.
//
// The K4 sends RX audio in bundles whose length is set by the SL tier: 20, 40, 60 or 120 ms of audio
// per packet (utils/radioutils.h, slTierToFrameSamples). Everything here is expressed in that packet
// length, never in fixed milliseconds, because a fixed size would be wrong for three of the four
// tiers.
//
// Total RX latency is everything buffered between the socket and the speaker: the app queue plus
// the QAudioSink buffer. Only the queue can be trimmed - audio already written to the sink plays.
// So the sink is kept as small as it can be without starving the device, and the queue holds the
// rest, where the drop-oldest policy can reach it.
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

// The jitter policy, in packets. The queue is trimmed when the TOTAL buffered audio (queue + sink)
// would pass the high-water mark, back down to TARGET. Counting the sink is what makes the numbers
// mean latency; before, the sink absorbed up to 500 ms that the policy could not see.
constexpr int TARGET_PACKETS = 2;
constexpr int HIGH_WATER_PACKETS = 5;

// WHY a ceiling in ms as well: five packets is 100 ms at SL0 but 600 ms at SL7, and a backlog that
// size is the late audio this policy exists to remove. Above the ceiling the mark is one packet over
// the target instead, which still leaves the policy room to work at the longest packets.
constexpr int HIGH_WATER_MAX_MS = 300;

constexpr int highWaterBytes(int pktBytes) {
    if (pktBytes <= 0)
        return 0;
    const int byPackets = HIGH_WATER_PACKETS * pktBytes;
    const int ceiling = HIGH_WATER_MAX_MS * BYTES_PER_MS;
    const int floor = (TARGET_PACKETS + 1) * pktBytes;
    const int capped = byPackets < ceiling ? byPackets : ceiling;
    return capped > floor ? capped : floor;
}

// True when the buffered audio, plus the packet about to be queued, passes the high-water mark.
constexpr bool overHighWater(int queuedBytes, int sinkFillBytes, int incomingBytes, int pktBytes) {
    return pktBytes > 0 && queuedBytes + sinkFillBytes + incomingBytes > highWaterBytes(pktBytes);
}

// Trim-to depth for the total buffered audio.
constexpr int targetBytes(int pktBytes) {
    return pktBytes > 0 ? TARGET_PACKETS * pktBytes : 0;
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

} // namespace RxPlayout

#endif // RXPLAYOUT_H
