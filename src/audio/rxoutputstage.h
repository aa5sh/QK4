#ifndef AUDIO_RXOUTPUTSTAGE_H
#define AUDIO_RXOUTPUTSTAGE_H

#include <cmath>

// The last stage of RX playback: the operator's boost above the MAIN/SUB sliders, and the limiter
// that keeps a boosted signal inside full scale. Pure functions, so they can be tested without an
// audio device.
namespace RxOutputStage {

// Options -> Audio Output offers 0, +3, +6, +9 and +12 dB.
//
// WHY a boost at all: the sliders top out at unity, and the radio's stream is quiet - on a K4D at
// R40, CW with no antenna, MAIN sat at -30 dBFS peak and -42 dBFS RMS - so an operator with the
// OS volume down could run out of slider. The boost multiplies the sliders rather than extending
// their range, so a saved slider position plays at the same level as before.
constexpr int kMaxBoostDb = 12;
constexpr int kBoostStepDb = 3;

// Snaps to the nearest offered step, 0 to kMaxBoostDb.
inline int snapBoostDb(int db) {
    if (db <= 0)
        return 0;
    if (db >= kMaxBoostDb)
        return kMaxBoostDb;
    return ((db + kBoostStepDb / 2) / kBoostStepDb) * kBoostStepDb;
}

// Linear amplitude gain for a boost in dB: 10^(dB/20). +6 dB is about 2x, +12 dB about 4x.
inline float boostGain(int db) {
    return static_cast<float>(std::pow(10.0, snapBoostDb(db) / 20.0));
}

// Samples below this magnitude pass through unchanged: about -1.9 dBFS.
constexpr float kLimiterThreshold = 0.8f;

// Soft limiter for the boosted path. Identity up to the threshold, then a tanh knee that meets the
// straight line with the same slope and approaches full scale, never exceeding it. A strong
// signal pushed over the top by the boost is rounded off instead of hard-clipped, so a static crash
// stays a crash rather than a crackle of clipped samples.
inline float softLimit(float x) {
    const float magnitude = std::fabs(x);
    if (!(magnitude > kLimiterThreshold))
        return x; // also passes NaN through untouched rather than inventing a level
    const float headroom = 1.0f - kLimiterThreshold;
    const float shaped = kLimiterThreshold + headroom * std::tanh((magnitude - kLimiterThreshold) / headroom);
    return x < 0.0f ? -shaped : shaped;
}

} // namespace RxOutputStage

#endif // AUDIO_RXOUTPUTSTAGE_H
