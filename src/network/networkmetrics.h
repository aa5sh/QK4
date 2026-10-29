#ifndef NETWORKMETRICS_H
#define NETWORKMETRICS_H

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <deque>

/**
 * @brief Rolling-window TCP health metrics: RTT (mean + jitter over RTT_WINDOW samples), audio
 *        packet activity, buffer-fill snapshots, and a derived Green/Yellow/Orange/Red
 *        `HealthTier`. Drives `NetHealthWidget`.
 *
 * Also keeps a 10-second timestamped trail of RTT, jitter, and buffer-depth so the hover popup
 * can plot live sparklines (the scalar getters only expose the latest value).
 */
class NetworkMetrics : public QObject {
    Q_OBJECT
public:
    enum HealthTier { Green = 0, Yellow = 1, Orange = 2, Red = 3 };
    Q_ENUM(HealthTier)

    // One point in a metric's rolling history. tMs is a monotonic timestamp (m_clock), value is
    // in the metric's natural display unit (ms for RTT/jitter/buffer).
    struct TimedSample {
        qint64 tMs;
        float value;
    };

    // WHY 96: the RX queue holds decoded audio at 12 kHz, stereo, Float32 - 12000 x 2 x 4 bytes per
    // second, so 96 bytes are one millisecond of playback.
    static constexpr double kDecodedBytesPerMs = 96.0;

    explicit NetworkMetrics(QObject *parent = nullptr);

    HealthTier healthTier() const { return m_tier; }
    int rttCurrent() const { return m_rttCurrent; }
    double rttAvg() const { return m_rttAvg; }
    double rttJitter() const { return m_rttJitter; }
    double bufferMs() const { return m_bufferBytes / kDecodedBytesPerMs; }

    // 10-second rolling history for the live sparkline popup. Read on the main thread only.
    const std::deque<TimedSample> &rttHistory() const { return m_rttHistory; }
    const std::deque<TimedSample> &jitterHistory() const { return m_jitterHistory; }
    const std::deque<TimedSample> &bufferHistory() const { return m_bufferHistory; }
    qint64 historyWindowMs() const { return HISTORY_WINDOW_MS; }
    qint64 clockNowMs() const { return m_clock.elapsed(); }

public slots:
    void onLatencyChanged(int ms);
    void onAudioSequence();
    void onBufferStatus(int queueBytes);
    void onConnectionStateChanged(bool connected);

signals:
    void healthTierChanged(HealthTier tier);

private slots:
    void onSummaryTimer();

private:
    void computeHealthTier();
    void updateRttStats();
    // Append a sample and drop entries older than HISTORY_WINDOW_MS (with a hard size cap as a
    // backstop against a pathologically fast producer).
    void pushHistory(std::deque<TimedSample> &history, float value);

    QTimer *m_summaryTimer;

    // Monotonic clock for history timestamps (immune to wall-clock jumps).
    QElapsedTimer m_clock;

    // 30-second sparkline trail. RTT/jitter append ~1/s (per PONG); buffer appends ~100/s
    // (AudioEngine feed timer, 10 ms). 30 s × 100/s = 3000 buffer samples → cap with headroom.
    static constexpr int HISTORY_WINDOW_MS = 30000;
    static constexpr int HISTORY_MAX_SAMPLES = 4000;
    std::deque<TimedSample> m_rttHistory;
    std::deque<TimedSample> m_jitterHistory;
    std::deque<TimedSample> m_bufferHistory;

    // RTT tracking (sliding window)
    static constexpr int RTT_WINDOW = 30;
    std::deque<int> m_rttSamples;
    int m_rttCurrent = -1;
    double m_rttAvg = 0.0;
    double m_rttJitter = 0.0;

    // Audio buffer depth (latest snapshot).
    int m_bufferBytes = 0;

    // Audio activity tracking
    bool m_audioActive = false;    // Received packets this interval
    bool m_audioWasActive = false; // Received packets in a previous interval
    int m_stalePingCount = 0;      // Consecutive intervals with no new RTT sample

    // Connection state
    bool m_connected = false;

    // Health tier
    HealthTier m_tier = Red;
};

#endif // NETWORKMETRICS_H
