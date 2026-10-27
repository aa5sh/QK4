#ifndef SIDETONEGENERATOR_H
#define SIDETONEGENERATOR_H

#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#include <QObject>
#include <atomic>
#include <optional>
#include <vector>

#include "hardware/keyertiming.h"

class SidetoneScheduler;

/**
 * @brief Local CW sidetone output for the HaliKey keyer. Lives on its own HighPriority thread
 *        (HardwareController's m_sidetoneThread).
 *
 * A pull-mode QAudioSink: each time the device wants audio, the block is rendered on the spot.
 * SidetoneScheduler says where in the block each key edge falls, against the block's play-out
 * time (time of fill plus the audio already queued ahead of it). That places every edge on an
 * exact frame, so each tone sounds its true length with a fixed delay. Envelope and tone follow
 * K4-Companion's mix_sidetone (Dale Farnsworth, GPL-3.0): 4 ms raised-cosine rise and fall, an
 * exact-period sine table, continuous phase.
 *
 * WHY its own sink for now rather than mixed into the RX output: the RX path is being reworked.
 * Phase 2 of docs/cw-keyer-port-plan.md moves this render into AudioEngine's output, so the tone
 * shares the RX device and clock. The scheduler is already the seam that move needs.
 *
 * All public slots are Q_INVOKABLE and expected to be posted via QueuedConnection.
 */
class SidetoneGenerator : public QObject {
    Q_OBJECT

public:
    SidetoneGenerator(SidetoneScheduler &scheduler, CwKeying::Clock &clock, QObject *parent = nullptr);
    ~SidetoneGenerator() override;

    Q_INVOKABLE void setFrequency(int hz);
    Q_INVOKABLE void setVolume(float volume);

    // Initialize/shutdown audio (called on sidetone thread via invokeMethod)
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void setOutputDevice(const QString &deviceId);

    // The sidetone's own clock, as the scheduler sees it: the keyer clock while a stream is open,
    // nullopt otherwise. Thread-safe.
    std::optional<double> streamTime() const;

    // Renders `maxlen` bytes of output into `data`. Called by the pull-mode source on the sidetone
    // thread.
    qint64 render(char *data, qint64 maxlen);

private:
    class Source;

    void initAudio();
    void closeAudio();
    void onSystemDefaultOutputChanged();
    void rebuildTone();
    void renderBlock(float *out, int frames, double dacTime, double now);

    SidetoneScheduler &m_scheduler;
    CwKeying::Clock &m_clock;

    QAudioSink *m_audioSink = nullptr;
    Source *m_source = nullptr;
    QString m_selectedOutputDeviceId;
    QString m_activeOutputDeviceId;          // id of the device the sink was actually opened on
    QMediaDevices *m_mediaDevices = nullptr; // OS device/default-change monitor
    QAudioFormat m_format;

    std::atomic<bool> m_streamActive{false};
    std::atomic<int> m_frequency{600};
    std::atomic<float> m_volume{0.3f};

    // Render state (sidetone thread only).
    int m_rate = 0;
    int m_blockFrames = 0;
    double m_riseStep = 0;
    double m_fallStep = 0;
    bool m_keyed = false;
    double m_envelope = 0.0;
    std::vector<float> m_cycle; // one exact period of the tone at unit amplitude
    size_t m_cycleIndex = 0;
    int m_toneFrequency = 0;
    qint64 m_framesWritten = 0;
    std::vector<float> m_scratch;
    std::vector<float> m_positions;

    static constexpr double kRiseSeconds = .004;
    static constexpr double kFallSeconds = .004;
    static constexpr double kBlockSeconds = .004;
    // WHY 20 ms: pull mode refills as the device drains, so the buffer is the whole local delay
    // between "keyed" and "heard". Short enough to key against at speed, and long enough that the
    // shared-mode backends here (WASAPI, PipeWire, CoreAudio) don't underrun. A bench number, not
    // a measured one; the scheduler holds whatever delay results constant.
    static constexpr double kBufferSeconds = .020;
};

#endif // SIDETONEGENERATOR_H
