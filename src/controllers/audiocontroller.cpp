#include "audiocontroller.h"
#include "connectioncontroller.h"
#include "audio/audioengine.h"
#include "logging/logcategories.h"
#include "logging/logsink.h"
#include "audio/opusdecoder.h"
#include "models/radiostate.h"
#include "network/networkmetrics.h"
#include "network/protocol.h"
#include "network/tcpclient.h"
#include "settings/radiosettings.h"
#include "utils/radioutils.h"

namespace {
// 20 Hz: often enough for the buffer sparkline, a tenth of the events the audio thread used to post.
constexpr int kMetricsSampleMs = 50;
// One RX playout line at most this often, and only when something happened.
constexpr int kRxLogEveryTicks = 2000 / kMetricsSampleMs;
} // namespace

AudioController::AudioController(ConnectionController *connController, RadioState *radioState, QObject *parent)
    : QObject(parent), m_connectionController(connController), m_radioState(radioState),
      m_audioEngine(new AudioEngine(nullptr)) {

    // Load saved audio device settings BEFORE moveToThread (only stores strings/floats,
    // no Qt audio objects exist yet, so direct calls are safe)
    QString savedMicDevice = RadioSettings::instance()->micDevice();
    if (!savedMicDevice.isEmpty()) {
        m_audioEngine->setMicDevice(savedMicDevice);
    }
    QString savedSpeakerDevice = RadioSettings::instance()->speakerDevice();
    if (!savedSpeakerDevice.isEmpty()) {
        m_audioEngine->setOutputDevice(savedSpeakerDevice);
    }
    m_audioEngine->setMicGain(RadioSettings::instance()->micGain() / 100.0f);
    m_audioEngine->setTciTxGain(RadioSettings::instance()->tciTxGain() / 100.0f);

    // Move AudioEngine to dedicated thread for glitch-free audio playback
    m_audioThread = new QThread(this);
    m_audioThread->setObjectName("AudioEngine");
    Logging::markRealtimeOnStart(m_audioThread);
    m_audioEngine->moveToThread(m_audioThread);
    m_audioThread->start();

    // RX audio path: Protocol → decode → enqueue, entirely on the I/O thread.
    //
    // WHY the decoder is captured by the handler rather than held here: it is used only on the I/O
    // thread, so its lifetime belongs to the handler that runs there. Qt keeps a slot object alive
    // for the duration of a call, so the decoder cannot be freed underneath a decode in progress;
    // it goes when the connection does. (K4 sends 12 kHz stereo: left = Main, right = Sub.)
    //
    // m_audioEngine->enqueueAudio() is the producer side of a single-producer ring: this lambda,
    // on the I/O thread, is its only caller.
    auto decoder = std::make_shared<OpusDecoder>();
    decoder->initialize(12000, 2);
    auto *protocol = m_connectionController->tcpClient()->protocol();
    m_rxDecodeContext = protocol;
    m_rxDecodeConnection =
        connect(protocol, &Protocol::audioDataReady, protocol, [this, decoder](const QByteArray &payload) {
            QByteArray pcmData = decoder->decodeK4Packet(payload);
            if (!pcmData.isEmpty()) {
                m_audioEngine->enqueueAudio(pcmData);
                // Fan-out for TCI listeners. Deliberately after enqueueAudio so the speaker path is
                // never delayed by a consumer, and deliberately not downstream of it so a listener does
                // not inherit the jitter buffer's drop-oldest policy.
                //
                // Posts a cross-thread event only while TciController has the fan-out connected (between
                // its start and stop); with no receiver the emit is a no-op.
                emit rxAudioAvailable(pcmData);
            }
        });

    // Audio buffer status → network health metrics
    connect(protocol, &Protocol::audioSequenceReceived, m_connectionController->networkMetrics(),
            &NetworkMetrics::onAudioSequence);
    connect(m_audioEngine, &AudioEngine::bufferStatus, m_connectionController->networkMetrics(),
            &NetworkMetrics::onBufferStatus);

    // Playout counters → network health metrics, sampled here on the main thread.
    m_metricsTimer = new QTimer(this);
    m_metricsTimer->setInterval(kMetricsSampleMs);
    connect(m_metricsTimer, &QTimer::timeout, this, &AudioController::sampleAudioMetrics);
    m_metricsTimer->start();

    // TX audio path (PR 12): encode + packetize run on the audio thread; the
    // resulting wire-ready packet goes straight to TcpClient::sendRaw, which
    // auto-marshals from the audio thread to its own IO thread. AutoConnection
    // resolves to QueuedConnection across the audio→IO boundary (one hop).
    connect(m_audioEngine, &AudioEngine::txPacketReady, m_connectionController->tcpClient(), &TcpClient::sendRaw);

    // Straight through to whoever can put it on screen. Queued by default - emitted on the audio
    // thread and consumed by the UI.
    connect(m_audioEngine, &AudioEngine::micUnavailable, this, &AudioController::micUnavailable);

    // SL tier changes → update TX frame size
    connect(m_radioState, &RadioState::streamingLatencyChanged, this, &AudioController::onStreamingLatencyChanged);

    // SUB RX mute/unmute
    connect(m_radioState, &RadioState::subRxEnabledChanged, this, [this](bool enabled) {
        if (m_audioEngine) {
            m_audioEngine->setSubMuted(!enabled);
        }
    });

    // Balance and mix from RadioState
    connect(m_radioState, &RadioState::balanceChanged, this, [this](int mode, int offset) {
        if (m_audioEngine) {
            m_audioEngine->setBalanceMode(mode);
            m_audioEngine->setBalanceOffset(offset);
        }
    });
    connect(m_radioState, &RadioState::audioMixChanged, this, [this](int left, int right) {
        if (m_audioEngine) {
            m_audioEngine->setAudioMix(left, right);
        }
    });
}

AudioController::~AudioController() {
    // WHY first, and ON the I/O thread: the decode handler runs there and uses this object and the
    // engine. On the normal quit path ConnectionController, and Protocol with it, is already gone,
    // the handler went with it, and this is a no-op. If the I/O link outlives this controller, the
    // disconnect is run as a blocking call on the I/O thread: that thread runs one event at a time,
    // so once the call returns no decode is in flight and none can start. Safe from deadlock
    // because the I/O thread never waits on the main thread (only main → I/O blocking calls
    // exist). With the thread not running, nothing can be in flight and a plain disconnect does.
    if (QObject *context = m_rxDecodeContext.data()) {
        QThread *ioThread = context->thread();
        if (ioThread && ioThread->isRunning() && ioThread != QThread::currentThread()) {
            QMetaObject::invokeMethod(
                context, [connection = m_rxDecodeConnection]() { QObject::disconnect(connection); },
                Qt::BlockingQueuedConnection);
        } else {
            disconnect(m_rxDecodeConnection);
        }
    }

    if (m_audioThread) {
        QMetaObject::invokeMethod(m_audioEngine, "stop", Qt::BlockingQueuedConnection);
        m_audioThread->quit();
        m_audioThread->wait(2000);
    }
    delete m_audioEngine;
    m_audioEngine = nullptr;
}

void AudioController::startAudio(float mainVolume, float subVolume, float micGain) {
    // Must NOT use BlockingQueuedConnection — setupAudioInput() (now deferred to
    // first mic use) can trigger macOS permission dialogs that need the main thread
    // runloop, which would deadlock if the main thread were blocked here.
    // The RX sink is sized from the SL tier, so the tier goes ahead of start() in the same queue.
    const int sl = m_connectionController->currentRadio().streamingLatency;
    const int frameSamples = RadioUtils::slTierToFrameSamples(sl);
    QMetaObject::invokeMethod(m_audioEngine, "setRxPacketFrameSamples", Qt::QueuedConnection, Q_ARG(int, frameSamples));
    QMetaObject::invokeMethod(m_audioEngine, "start", Qt::QueuedConnection);

    // Volume setters are atomic — safe as direct calls from any thread
    m_audioEngine->setMainVolume(mainVolume);
    m_audioEngine->setSubVolume(subVolume);
    m_audioEngine->setMicGain(micGain);

    // Set initial TX frame size from radio profile's SL tier
    m_audioEngine->setFrameSamples(frameSamples);

    // Set encode mode from radio profile (audio thread reads this atomic on
    // every captured frame to pick the wire format).
    m_audioEngine->setEncodeMode(m_connectionController->currentRadio().encodeMode);
}

void AudioController::stopAudio() {
    if (m_audioEngine) {
        QMetaObject::invokeMethod(m_audioEngine, "stop", Qt::QueuedConnection);
    }
}

void AudioController::shutdown() {
    // Synchronous audio teardown — required from MainWindow::closeEvent so
    // QAudioSink/QAudioSource are destroyed while the event loop is still
    // alive, not during libc atexit (which races PipeWire's RT worker on Linux).
    if (m_audioEngine && m_audioThread && m_audioThread->isRunning()) {
        QMetaObject::invokeMethod(m_audioEngine, "stop", Qt::BlockingQueuedConnection);
    }
}

void AudioController::setPttActive(bool active) {
    // Ignore PTT-on when disconnected; always honor PTT-off so a stuck-active
    // state can be cleared regardless of connection state.
    if (active && !m_connectionController->isConnected())
        return;

    // Forward to AudioEngine (audio thread). setPttActive there atomically
    // gates the encode pipeline, opens the mic on rising edge if needed, and
    // resets the txSequence counter + flushes the partial-frame tail.
    QMetaObject::invokeMethod(m_audioEngine, "setPttActive", Qt::QueuedConnection, Q_ARG(bool, active));

    // Emitted AFTER the early return above, so a PTT-on that was refused for being disconnected
    // does not announce a transmit that is not happening.
    emit pttActiveChanged(active);
}

void AudioController::setTxSource(TxSource source) {
    if (!m_audioEngine)
        return;
    // Direct, and deliberately so: the source must be in effect BEFORE the PTT that follows, and
    // setPttActive is queued. A queued pair would arrive in order, but a direct source write
    // followed by a queued PTT cannot be reordered at all.
    //
    // Safe from any thread because setTxSource on the engine writes only an atomic; it queues its
    // own buffer flush to the audio thread rather than touching audio-thread state here.
    //
    // Taking a new generation invalidates any setTxSourceAfterPtt() switch still queued, so an
    // older "back to Microphone" cannot land on top of this one.
    std::lock_guard<std::mutex> lock(m_txSourceOrder->mutex);
    ++m_txSourceOrder->generation;
    m_audioEngine->setTxSource(static_cast<AudioEngine::TxSource>(source));
}

void AudioController::setTxSourceAfterPtt(TxSource source) {
    if (!m_audioEngine)
        return;
    // Queued, and that is the whole point of this method existing next to setTxSource().
    //
    // Qt delivers queued calls to one thread in posting order, so this lands after the
    // setPttActive() posted immediately before it. Writing the source directly on an unkey instead
    // opened a window - one audio block wide - in which the audio thread saw PTT still asserted
    // and the source already back to Microphone, and duly encoded the room and sent it while the
    // radio was still transmitting the TCI transmission.
    //
    // STALE SWITCHES ARE DROPPED. If a newer source write happens before this one is delivered - an
    // immediate re-key selecting Tci directly - this switch no longer describes the transmission
    // it was queued for, and applying it would put the microphone under the new one.
    quint64 generation;
    {
        std::lock_guard<std::mutex> lock(m_txSourceOrder->mutex);
        generation = ++m_txSourceOrder->generation;
    }
    QMetaObject::invokeMethod(
        m_audioEngine,
        [engine = m_audioEngine, order = m_txSourceOrder, generation, source]() {
            std::lock_guard<std::mutex> lock(order->mutex);
            if (order->generation != generation)
                return; // superseded by a later source write
            engine->setTxSource(static_cast<AudioEngine::TxSource>(source));
        },
        Qt::QueuedConnection);
}

void AudioController::feedTciTxAudio(const QByteArray &f32Mono48k) {
    if (!m_audioEngine)
        return;
    // Queued: the caller is on the TCI thread, the encode pipeline runs on the audio thread.
    QMetaObject::invokeMethod(m_audioEngine, "feedTciTxAudio", Qt::QueuedConnection, Q_ARG(QByteArray, f32Mono48k));
}

bool AudioController::isPttActive() const {
    // Lock-free atomic read from AudioEngine — safe from any thread.
    return m_audioEngine ? m_audioEngine->isPttActive() : false;
}

float AudioController::mainVolume() const {
    return m_audioEngine ? m_audioEngine->mainVolume() : 1.0f;
}

float AudioController::subVolume() const {
    return m_audioEngine ? m_audioEngine->subVolume() : 1.0f;
}

void AudioController::setMainVolume(float vol) {
    if (m_audioEngine)
        m_audioEngine->setMainVolume(vol);
}

void AudioController::setSubVolume(float vol) {
    if (m_audioEngine)
        m_audioEngine->setSubVolume(vol);
}

void AudioController::setRxBoostDb(int db) {
    if (m_audioEngine)
        m_audioEngine->setRxBoostDb(db);
}

void AudioController::setBalanceMode(int mode) {
    if (m_audioEngine)
        m_audioEngine->setBalanceMode(mode);
}

void AudioController::setBalanceOffset(int offset) {
    if (m_audioEngine)
        m_audioEngine->setBalanceOffset(offset);
}

void AudioController::setAudioMix(int left, int right) {
    if (m_audioEngine)
        m_audioEngine->setAudioMix(left, right);
}

void AudioController::setSubMuted(bool muted) {
    if (m_audioEngine)
        m_audioEngine->setSubMuted(muted);
}

void AudioController::setMicDevice(const QString &deviceId) {
    if (m_audioEngine)
        QMetaObject::invokeMethod(m_audioEngine, "setMicDevice", Qt::QueuedConnection, Q_ARG(QString, deviceId));
}

void AudioController::setOutputDevice(const QString &deviceId) {
    if (m_audioEngine)
        QMetaObject::invokeMethod(m_audioEngine, "setOutputDevice", Qt::QueuedConnection, Q_ARG(QString, deviceId));
}

void AudioController::setMicGain(float gain) {
    if (m_audioEngine)
        QMetaObject::invokeMethod(m_audioEngine, "setMicGain", Qt::QueuedConnection, Q_ARG(float, gain));
}

void AudioController::setTciTxGain(float gain) {
    if (m_audioEngine)
        QMetaObject::invokeMethod(m_audioEngine, "setTciTxGain", Qt::QueuedConnection, Q_ARG(float, gain));
}

void AudioController::onStreamingLatencyChanged(int tier) {
    const int frameSamples = RadioUtils::slTierToFrameSamples(tier);
    m_audioEngine->setFrameSamples(frameSamples);
    // The RX sink is resized too: the tier sets how much audio each received packet carries.
    QMetaObject::invokeMethod(m_audioEngine, "setRxPacketFrameSamples", Qt::QueuedConnection, Q_ARG(int, frameSamples));
}

void AudioController::sampleAudioMetrics() {
    if (!m_audioEngine)
        return;
    const quint32 underruns = m_audioEngine->rxUnderruns();
    const quint32 drops = m_audioEngine->rxDroppedPackets();
    m_connectionController->networkMetrics()->onRxPlayoutCounters(underruns, drops);

    // Formatted here, on the main thread, never on the audio thread (CONVENTIONS.md Rule 16).
    if (++m_ticksSinceRxLog < kRxLogEveryTicks)
        return;
    m_ticksSinceRxLog = 0;
    if (underruns != m_loggedUnderruns || drops != m_loggedDrops) {
        qCInfo(lcAudioRx, "RX playout: %u underrun(s), %u packet(s) dropped to cut latency in the last %d s",
               underruns - m_loggedUnderruns, drops - m_loggedDrops, kRxLogEveryTicks * kMetricsSampleMs / 1000);
        m_loggedUnderruns = underruns;
        m_loggedDrops = drops;
    }
}
