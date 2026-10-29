#ifndef AUDIODIAGNOSTICS_H
#define AUDIODIAGNOSTICS_H

#include <QtGlobal>

// Level diagnostics measured on the audio threads and FORMATTED elsewhere.
//
// WHY: the RX audio thread and the TX thread run against deadlines, and a qCDebug there formats a
// string, allocates and writes a log line whenever someone enables the category (CONVENTIONS.md
// Rule 16). So those threads fill one of these plain records and push it into a lock-free ring
// (audio/spscring.h); AudioController drains the rings on its main-thread sampling timer and calls
// the log functions below. The log lines are the same as before; only the thread that writes them
// changed.
namespace AudioDiagnostics {

// qk4.audio.rx: one second of received audio. Levels are linear (1.0 = full scale).
struct RxLevelReport {
    float peakMain = 0.0f;
    float rmsMain = 0.0f;
    float peakSub = 0.0f;
    float rmsSub = 0.0f;
    float peakOut = 0.0f; // before the clamp or limiter, so it can read above full scale
    int clipped = 0;      // samples over full scale before that stage
    float mainSlider = 0.0f;
    float subSlider = 0.0f;
    float boost = 1.0f;
};

// qk4.audio.tx: one encoded TX frame (the first of a transmission, then every Nth).
struct TxFrameReport {
    int encodeMode = 0;
    unsigned sequence = 0;
    int frameSamples = 0;
    float framePeak = 0.0f;
    float peakSinceKey = 0.0f;
    bool fromTci = false;
    float gain = 0.0f; // effective gain; the slider is its cube root
    int wirePayloadBytes = 0;
    qint64 em0WirePeak = -1; // EM0 only: largest |sample| in the S32LE payload; -1 otherwise
};

// Main thread (any non-real-time thread). Write the report to its category at debug.
void logRxLevels(const RxLevelReport &r);
void logTxFrame(const TxFrameReport &r);

} // namespace AudioDiagnostics

#endif // AUDIODIAGNOSTICS_H
