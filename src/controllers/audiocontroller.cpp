#include "audiocontroller.h"
#include "connectioncontroller.h"
#include "audio/audioengine.h"
#include "audio/txaudiopipeline.h"
#include "logging/logcategories.h"
#include "logging/logsink.h"
#include "audio/opusdecoder.h"
#include "models/radiostate.h"
#include "network/networkmetrics.h"
#include "network/protocol.h"
#include "network/tciserver.h"
#include "network/tcpclient.h"
#include "settings/radiosettings.h"
#include "utils/radioutils.h"

#include <chrono>

namespace {
// 20 Hz: often enough for the buffer sparkline, a tenth of the events the audio thread used to post.
constexpr int kMetricsSampleMs = 50;
// No RX packet for this many ticks (250 ms, two SL7 packets) and the stream counts as stopped.
constexpr int kStreamIdleTicks = 5;
// One RX playout line at most this often, and only when something happened.
constexpr int kRxLogEveryTicks = 2000 / kMetricsSampleMs;
} // namespace

AudioController::AudioController(ConnectionController *connController, RadioState *radioState, QObject *parent)
    : QObject(parent), m_connectionController(connController), m_radioState(radioState),
      m_audioEngine(new AudioEngine(nullptr)), m_txPipeline(new TxAudioPipeline(nullptr)) {

    // Load saved audio device settings BEFORE moveToThread (only stores strings/floats,
    // no Qt audio objects exist yet, so direct calls are safe)
    QString savedMicDevice = RadioSettings::instance()->micDevice();
    if (!savedMicDevice.isEmpty()) {
        m_txPipeline->setMicDevice(savedMicDevice);
    }
    QString savedSpeakerDevice = RadioSettings::instance()->speakerDevice();
    if (!savedSpeakerDevice.isEmpty()) {
        m_audioEngine->setOutputDevice(savedSpeakerDevice);
        m_txPipeline->setOutputDeviceHint(savedSpeakerDevice);
    }
    m_txPipeline->setMicGain(RadioSettings::instance()->micGain() / 100.0f);
    m_txPipeline->setTciTxGain(RadioSettings::instance()->tciTxGain() / 100.0f);

    // Move AudioEngine to dedicated thread for glitch-free audio playback
    m_audioThread = new QThread(this);
    m_audioThread->setObjectName("AudioEngine");
    Logging::markRealtimeOnStart(m_audioThread);
    m_audioEngine->moveToThread(m_audioThread);
    m_audioThread->start();

    // Transmit audio on a thread of its own. WHY: opening a microphone is synchronous and can take
    // over a second; on the RX thread it stalled the speaker for that long. See TxAudioPipeline.
    m_txThread = new QThread(this);
    m_txThread->setObjectName("AudioTX");
    Logging::markRealtimeOnStart(m_txThread);
    m_txPipeline->moveToThread(m_txThread);
    m_txThread->start();

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
            m_rxPacketsDecoded.fetch_add(1, std::memory_order_relaxed);
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

    // Audio activity, buffer depth and playout counters → network health metrics, SAMPLED here on
    // the main thread at 20 Hz. WHY not signals: the buffer depth was emitted from the audio thread
    // on every feed tick and the packet sequence from the I/O thread on every packet - together
    // 150-300 queued events a second into the GUI event loop for a sparkline that redraws at 4 Hz.
    m_metricsTimer = new QTimer(this);
    m_metricsTimer->setInterval(kMetricsSampleMs);
    connect(m_metricsTimer, &QTimer::timeout, this, &AudioController::sampleAudioMetrics);
    m_metricsTimer->start();

    // TX audio path: encode + packetize run on the TX thread; the wire-ready packet goes straight
    // to TcpClient::sendRaw on the I/O thread, queued (one hop).
    connect(m_txPipeline, &TxAudioPipeline::txPacketReady, m_connectionController->tcpClient(), &TcpClient::sendRaw);

    // Straight through to whoever can put it on screen. Queued by default - emitted on the TX
    // thread and consumed by the UI.
    connect(m_txPipeline, &TxAudioPipeline::micUnavailable, this, &AudioController::micUnavailable);

    // Formatted here on the main thread, once per transmission, never on the TX thread.
    connect(m_txPipeline, &TxAudioPipeline::pttLatencyMeasured, this,
            [](qint64 firstPacketUs, qint64 micOpenUs, bool fromMicrophone) {
                if (firstPacketUs < 0) {
                    qCInfo(lcAudioTx, "PTT released before any TX audio was sent (%s; mic open took %.1f ms)",
                           fromMicrophone ? "microphone" : "TCI", micOpenUs / 1000.0);
                    return;
                }
                qCInfo(lcAudioTx, "PTT -> first TX audio packet %.1f ms (%s; mic open %.1f ms)", firstPacketUs / 1000.0,
                       fromMicrophone ? "microphone" : "TCI", micOpenUs / 1000.0);
            });

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

    if (m_txThread) {
        QMetaObject::invokeMethod(m_txPipeline, "stop", Qt::BlockingQueuedConnection);
        m_txThread->quit();
        m_txThread->wait(2000);
    }
    delete m_txPipeline;
    m_txPipeline = nullptr;

    if (m_audioThread) {
        QMetaObject::invokeMethod(m_audioEngine, "stop", Qt::BlockingQueuedConnection);
        m_audioThread->quit();
        m_audioThread->wait(2000);
    }
    delete m_audioEngine;
    m_audioEngine = nullptr;
}

void AudioController::startAudio(float mainVolume, float subVolume, float micGain) {
    // Queued, not blocking: nothing here needs to wait for the audio thread.
    // The RX sink is sized from the SL tier, so the tier goes ahead of start() in the same queue.
    const int sl = m_connectionController->currentRadio().streamingLatency;
    const int frameSamples = RadioUtils::slTierToFrameSamples(sl);
    QMetaObject::invokeMethod(m_audioEngine, "setRxPacketFrameSamples", Qt::QueuedConnection, Q_ARG(int, frameSamples));
    QMetaObject::invokeMethod(m_audioEngine, "start", Qt::QueuedConnection);

    // Volume setters are atomic — safe as direct calls from any thread
    m_audioEngine->setMainVolume(mainVolume);
    m_audioEngine->setSubVolume(subVolume);
    // Atomic stores in the TX pipeline - safe as direct calls.
    m_txPipeline->setMicGain(micGain);

    // Set initial TX frame size from radio profile's SL tier
    m_txPipeline->setFrameSamples(frameSamples);

    // Set encode mode from radio profile (the TX thread reads this atomic on every frame to pick
    // the wire format).
    m_txPipeline->setEncodeMode(m_connectionController->currentRadio().encodeMode);
}

void AudioController::stopAudio() {
    if (m_txPipeline) {
        QMetaObject::invokeMethod(m_txPipeline, "stop", Qt::QueuedConnection);
    }
    if (m_audioEngine) {
        m_audioEngine->setTransmitting(false);
        QMetaObject::invokeMethod(m_audioEngine, "stop", Qt::QueuedConnection);
    }
}

void AudioController::shutdown() {
    // Synchronous audio teardown — required from MainWindow::closeEvent so
    // QAudioSink/QAudioSource are destroyed while the event loop is still
    // alive, not during libc atexit (which races PipeWire's RT worker on Linux).
    if (m_txPipeline && m_txThread && m_txThread->isRunning()) {
        QMetaObject::invokeMethod(m_txPipeline, "stop", Qt::BlockingQueuedConnection);
    }
    if (m_audioEngine && m_audioThread && m_audioThread->isRunning()) {
        QMetaObject::invokeMethod(m_audioEngine, "stop", Qt::BlockingQueuedConnection);
    }
}

void AudioController::setPttActive(bool active) {
    // Ignore PTT-on when disconnected; always honor PTT-off so a stuck-active
    // state can be cleared regardless of connection state.
    if (active && !m_connectionController->isConnected())
        return;

    // Forward to the TX pipeline (TX thread). setPttActive there gates the encode pipeline, opens
    // or closes the microphone, and resets the sequence counter and partial-frame tail. The press
    // time travels with it so the delay to the first packet is measured from here.
    const qint64 nowNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    QMetaObject::invokeMethod(m_txPipeline, "setPttActive", Qt::QueuedConnection, Q_ARG(bool, active),
                              Q_ARG(qint64, nowNs));
    // The K4 stops sending RX audio while transmitting; the RX side must not count that as underruns.
    m_audioEngine->setTransmitting(active);

    // Emitted AFTER the early return above, so a PTT-on that was refused for being disconnected
    // does not announce a transmit that is not happening.
    emit pttActiveChanged(active);
}

void AudioController::setTxSource(TxSource source) {
    if (!m_txPipeline)
        return;
    // Direct, and deliberately so: the source must be in effect BEFORE the PTT that follows, and
    // setPttActive is queued. A queued pair would arrive in order, but a direct source write
    // followed by a queued PTT cannot be reordered at all.
    //
    // Safe from any thread because setTxSource on the pipeline writes only an atomic; it queues its
    // own buffer flush to the TX thread rather than touching TX-thread state here.
    //
    // Taking a new generation invalidates any setTxSourceAfterPtt() switch still queued, so an
    // older "back to Microphone" cannot land on top of this one.
    std::lock_guard<std::mutex> lock(m_txSourceOrder->mutex);
    ++m_txSourceOrder->generation;
    m_txPipeline->setTxSource(static_cast<TxAudioPipeline::TxSource>(source));
}

void AudioController::setTxSourceAfterPtt(TxSource source) {
    if (!m_txPipeline)
        return;
    // Queued, and that is the whole point of this method existing next to setTxSource().
    //
    // Qt delivers queued calls to one thread in posting order, so this lands after the
    // setPttActive() posted immediately before it. Writing the source directly on an unkey instead
    // opened a window - one audio block wide - in which the TX thread saw PTT still asserted
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
        m_txPipeline,
        [pipeline = m_txPipeline, order = m_txSourceOrder, generation, source]() {
            std::lock_guard<std::mutex> lock(order->mutex);
            if (order->generation != generation)
                return; // superseded by a later source write
            pipeline->setTxSource(static_cast<TxAudioPipeline::TxSource>(source));
        },
        Qt::QueuedConnection);
}

void AudioController::feedTciTxAudio(const QByteArray &f32Mono48k) {
    if (!m_txPipeline)
        return;
    // Queued: the encode pipeline runs on the TX thread.
    QMetaObject::invokeMethod(m_txPipeline, "feedTciTxAudio", Qt::QueuedConnection, Q_ARG(QByteArray, f32Mono48k));
}

QMetaObject::Connection AudioController::routeTciTxAudio(TciServer *server) {
    if (!m_txPipeline || !server)
        return {};
    return connect(server, &TciServer::txAudioReceived, m_txPipeline, &TxAudioPipeline::feedTciTxAudio,
                   Qt::QueuedConnection);
}

bool AudioController::isPttActive() const {
    // Lock-free atomic read from the TX pipeline — safe from any thread.
    return m_txPipeline ? m_txPipeline->isPttActive() : false;
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
    if (m_txPipeline)
        QMetaObject::invokeMethod(m_txPipeline, "setMicDevice", Qt::QueuedConnection, Q_ARG(QString, deviceId));
}

void AudioController::setOutputDevice(const QString &deviceId) {
    if (m_audioEngine)
        QMetaObject::invokeMethod(m_audioEngine, "setOutputDevice", Qt::QueuedConnection, Q_ARG(QString, deviceId));
    if (m_txPipeline)
        QMetaObject::invokeMethod(m_txPipeline, "setOutputDeviceHint", Qt::QueuedConnection, Q_ARG(QString, deviceId));
}

void AudioController::setMicGain(float gain) {
    if (m_txPipeline)
        QMetaObject::invokeMethod(m_txPipeline, "setMicGain", Qt::QueuedConnection, Q_ARG(float, gain));
}

void AudioController::setTciTxGain(float gain) {
    if (m_txPipeline)
        QMetaObject::invokeMethod(m_txPipeline, "setTciTxGain", Qt::QueuedConnection, Q_ARG(float, gain));
}

void AudioController::onStreamingLatencyChanged(int tier) {
    const int frameSamples = RadioUtils::slTierToFrameSamples(tier);
    m_txPipeline->setFrameSamples(frameSamples);
    // The RX sink is resized too: the tier sets how much audio each received packet carries.
    QMetaObject::invokeMethod(m_audioEngine, "setRxPacketFrameSamples", Qt::QueuedConnection, Q_ARG(int, frameSamples));
}

void AudioController::sampleAudioMetrics() {
    if (!m_audioEngine)
        return;
    NetworkMetrics *metrics = m_connectionController->networkMetrics();
    const quint32 packets = m_rxPacketsDecoded.load(std::memory_order_relaxed);
    if (packets != m_sampledRxPackets) {
        m_sampledRxPackets = packets;
        m_ticksSinceRxPacket = 0;
        metrics->onAudioSequence();
    } else if (m_ticksSinceRxPacket < kStreamIdleTicks) {
        ++m_ticksSinceRxPacket;
    }
    // Depth only while audio is flowing, as before: the engine's last value goes stale when the
    // stream stops, and an idle connection should not draw a flat line at it. "Flowing" spans a
    // few ticks so SL6-7's 120 ms packets still give a sample every tick.
    if (m_ticksSinceRxPacket < kStreamIdleTicks)
        metrics->onBufferStatus(m_audioEngine->rxBufferedBytes());
    const quint32 underruns = m_audioEngine->rxUnderruns();
    const quint32 drops = m_audioEngine->rxDroppedPackets();
    metrics->onRxPlayoutCounters(underruns, drops);

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
