#ifndef HALIKEYWORKERBASE_H
#define HALIKEYWORKERBASE_H

#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

// Shared logging category for HaliKey device + workers. Definition in halikeydevice.cpp.
Q_DECLARE_LOGGING_CATEGORY(hwHalikey)

/**
 * @brief Base class for HaliKey paddle-input workers, which live on `HalikeyDevice::m_workerThread`.
 *
 * Two concrete workers, because the transports share no event model:
 *   - `HaliKeyV14Worker`  — V1.4 serial: polls the modem lines every 1 ms and emits paddleChanged.
 *   - `HaliKeyMidiWorker` — MIDI variant: hands each raw message to a HaliKeyMidiSink on RtMidi's
 *                           thread.
 *
 * WHY `prepareShutdown()` exists: it runs on the main thread before the worker thread is joined.
 * The MIDI worker uses it to close the port, which stops RtMidi's callback thread. The serial loop
 * sees the stop request within a poll, so it needs nothing.
 */
// Where raw MIDI messages go, called straight from RtMidi's own thread with no hop: the keyer
// stamps a paddle edge the moment it arrives, so a queued hop would be timing error. Shared
// between HalikeyDevice (which sets the target) and the MIDI worker (which calls it), and guarded,
// because the target can be replaced while RtMidi's thread is calling.
struct HaliKeyMidiSink {
    using Fn = std::function<void(int status, int note, int velocity, double deltaTime)>;
    void set(Fn fn) {
        std::lock_guard lock(mutex);
        target = std::move(fn);
    }
    void call(int status, int note, int velocity, double deltaTime) {
        std::lock_guard lock(mutex);
        if (target)
            target(status, note, velocity, deltaTime);
    }

private:
    std::mutex mutex;
    Fn target;
};

class HaliKeyWorkerBase : public QObject {
    Q_OBJECT

public:
    explicit HaliKeyWorkerBase(const QString &portName, QObject *parent = nullptr);
    ~HaliKeyWorkerBase() override = default;

    virtual void prepareShutdown() {} // Override for platform-specific unblocking

    // A straight key (or external keyer) on the dit line rather than paddles. The serial worker
    // debounces a straight key for 20 ms and ignores the dah line. Thread-safe; takes effect on
    // the next poll.
    void setStraightKey(bool straight) { m_straightKey.store(straight, std::memory_order_release); }

public slots:
    virtual void start() = 0; // Called when thread starts
    void stop();              // Requests the worker to stop; never cleared, so it holds even before start()

signals:
    // One paddle line changed (serial worker): which lever, its new state, and the time in seconds
    // since that line's previous reported change (0 the first time). Emitted on the worker thread
    // the moment the 1 ms poll sees it. Port of K4-Companion's serial callback.
    void paddleChanged(bool dah, bool asserted, double deltaTime);

    void errorOccurred(const QString &error);
    void portOpened();

protected:
    bool stopRequested() const { return m_stopRequested.load(std::memory_order_acquire); }

    QString m_portName;
    std::atomic<bool> m_straightKey{false};

private:
    // WHY a request flag rather than a running flag the loop sets on entry: stop() can arrive
    // before start() has run, and a loop that stored `running = true` would overwrite it and never
    // exit, leaving the thread unjoinable.
    std::atomic<bool> m_stopRequested{false};
};

#endif // HALIKEYWORKERBASE_H
