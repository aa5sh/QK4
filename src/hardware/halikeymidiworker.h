#ifndef HALIKEYMIDIWORKER_H
#define HALIKEYMIDIWORKER_H

#include "halikeyworkerbase.h"
#include <QMutex>
#include <memory>

class QTimer;
class RtMidiIn;

/**
 * @brief HaliKey worker for the MIDI variant of the paddle device. RtMidi calls back on its own
 *        thread, and each message goes straight to the HaliKeyMidiSink (the keyer, which decodes
 *        MoMIDI's timing). The worker thread only runs the presence poll. `prepareShutdown()`
 *        closes the port so RtMidi's callback thread quits before QThread tears down.
 */
class HaliKeyMidiWorker : public HaliKeyWorkerBase {
    Q_OBJECT

public:
    HaliKeyMidiWorker(const QString &deviceName, std::shared_ptr<HaliKeyMidiSink> sink, QObject *parent = nullptr);
    ~HaliKeyMidiWorker() override;

    // Closes MIDI port and stops RtMidi callback thread before QThread teardown.
    // Called from the MAIN thread, while the presence poll below may be using m_midiIn on the
    // worker thread — which is why m_midiIn is guarded by m_midiMutex. RtMidi synchronises its own
    // callback thread, but nothing in RtMidi protects the RtMidiIn object from being destroyed
    // while another thread is calling into it.
    void prepareShutdown() override;

public slots:
    void start() override;

private:
    static void midiCallback(double deltaTime, std::vector<unsigned char> *message, void *userData);
    static void logNote(int note, bool down); // hw.midi diagnostics, on RtMidi's thread

    // Has the port we opened disappeared from the enumeration? Runs on the worker thread.
    void checkPortPresence();

    std::unique_ptr<RtMidiIn> m_midiIn;

    // Guards m_midiIn against the main thread's prepareShutdown()/destructor racing the presence
    // poll on the worker thread. Never held while emitting.
    QMutex m_midiMutex;

    // Presence poll. Created on the worker thread in start(), so it is owned by that thread's
    // event loop and dies with it; the main thread never touches it.
    QTimer *m_presenceTimer = nullptr;

    // Consecutive polls that did not find the port. Two are required before reporting removal, so
    // a momentary enumeration glitch (a MIDIServer restart, a driver reload) cannot disconnect a
    // device that is still plugged in.
    int m_missedPolls = 0;

    std::shared_ptr<HaliKeyMidiSink> m_sink;

    // Set by the first control change (MoMIDI's version message). Read and written only in the
    // RtMidi callback; atomic because RtMidi does not promise which thread that runs on.
    std::atomic<bool> m_momidiDetected{false};
};

#endif // HALIKEYMIDIWORKER_H
