#ifndef AUDIO_AUDIOLOGGING_H
#define AUDIO_AUDIOLOGGING_H

#include <QLoggingCategory>

// Shared logging category for the audio subsystem (AudioController, AudioEngine,
// OpusDecoder, OpusEncoder, SidetoneGenerator). Definition lives in audiocontroller.cpp.
//
// Debug output is off by default (every QK4 category defaults to QtInfoMsg). Turn it on with:
//     QT_LOGGING_RULES="qk4.audio.debug=true"
Q_DECLARE_LOGGING_CATEGORY(qk4Audio)

// TX wire-format diagnostic, separate so it can be switched on without the rest of the
// audio subsystem's chatter. Off by default: it scans every sample of the frames it
// reports, which the RT audio thread should not do in normal operation.
//
//     QT_LOGGING_RULES="qk4.audio.tx.debug=true"
Q_DECLARE_LOGGING_CATEGORY(qk4AudioTx)

// RX playback level, once a second: MAIN and SUB as decoded (before the sliders), the mix that
// goes to the device, and how many samples the final clamp clipped. Off by default for the same
// reason as TX - it touches every sample on the RT audio thread.
//
//     QT_LOGGING_RULES="qk4.audio.rx.debug=true"
Q_DECLARE_LOGGING_CATEGORY(qk4AudioRx)

#endif // AUDIO_AUDIOLOGGING_H
