#include "cwcontroller.h"

#include <QLoggingCategory>

#include "audio/sidetonegenerator.h"
#include "connectioncontroller.h"
#include "hardware/halikeydevice.h"
#include "hardware/keyerruntime.h"
#include "hardware/kpodplusdevice.h"
#include "hardware/paddlekeyer.h"
#include "models/radiostate.h"
#include "network/tcpclient.h"
#include "settings/radiosettings.h"

Q_LOGGING_CATEGORY(cwKeyer, "cw.keyer")

CwController::CwController(RadioState *radioState, ConnectionController *connection, PaddleKeyer *keyer,
                           FunctionLink *link, SidetoneGenerator *sidetone, HalikeyDevice *halikey,
                           KpodPlusDevice *kpodPlus, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_connection(connection), m_keyer(keyer), m_link(link),
      m_sidetone(sidetone), m_halikey(halikey), m_kpodPlus(kpodPlus) {

    // =========================================================================
    // Keyer → K4
    // =========================================================================
    // sendCAT queues to the I/O thread itself; one producer (the keyer thread, or the main thread
    // for the monitor restore) keeps KZD/KZU in order. Never gated here: see invariant 7.
    auto *tc = m_connection->tcpClient();
    m_link->setSender([tc](const std::string &command) { tc->sendCAT(QString::fromLatin1(command)); });

    // =========================================================================
    // Configuration from the K4 (the source of truth) and from settings
    // =========================================================================
    auto *settings = RadioSettings::instance();
    m_cachedIsV14.store(settings->halikeyDeviceType() != 1, std::memory_order_release);
    m_keyer->setStraightKeying(settings->halikeyStraightKey());
    m_keyer->setDitMemoryDelayPercent(settings->halikeyDitMemoryDelay());
    m_keyer->setMuteRadioMonitor(settings->halikeyMuteRadioMonitor());
    if (m_radioState->monitorLevelCW() >= 0)
        m_keyer->setMonitorLevel(m_radioState->monitorLevelCW());
    if (m_radioState->keyDownInitialDelayMs() >= 0)
        m_keyer->setKeyDownInitialDelayMs(m_radioState->keyDownInitialDelayMs());
    applySpeed();
    applyMode();
    applyReversal();
    if (m_radioState->cwPitch() > 0)
        QMetaObject::invokeMethod(m_sidetone, "setFrequency", Qt::QueuedConnection,
                                  Q_ARG(int, m_radioState->cwPitch()));

    connect(m_radioState, &RadioState::keyerSpeedChanged, this, [this](int wpm) {
        applySpeed();
        // K4 is the source of truth — mirror the speed onto the KPOD+ keyer.
        if (m_kpodPlus->isPolling())
            m_kpodPlus->setKeyerSpeed(wpm);
    });

    connect(m_radioState, &RadioState::keyerPaddleChanged, this, [this](QChar iambic, QChar paddle, int /*weight*/) {
        applySpeed(); // the weight
        applyMode();
        applyReversal();
        if (m_kpodPlus->isPolling())
            m_kpodPlus->setKeyerParams(iambic == 'B' ? 1 : 0, paddle == 'R');
    });

    connect(m_radioState, &RadioState::cwPitchChanged, this, [this](int pitchHz) {
        QMetaObject::invokeMethod(m_sidetone, "setFrequency", Qt::QueuedConnection, Q_ARG(int, pitchHz));
        // K4 is the source of truth — mirror the CW pitch onto the KPOD+ keyer.
        if (m_kpodPlus->isPolling())
            m_kpodPlus->setCwPitch(pitchHz);
    });

    connect(m_radioState, &RadioState::monitorLevelChanged, this, [this](int mode, int level) {
        if (mode == 0)
            m_keyer->setMonitorLevel(level);
    });

    connect(m_radioState, &RadioState::keyDownInitialDelayChanged, this,
            [this](int ms) { m_keyer->setKeyDownInitialDelayMs(ms); });

    connect(settings, &RadioSettings::halikeyDeviceTypeChanged, this, [this](int type) {
        m_cachedIsV14.store(type != 1, std::memory_order_release);
        m_keyer->releasePaddles();
    });
    connect(settings, &RadioSettings::halikeyKeyerModeChanged, this, [this](int) { applyMode(); });
    m_halikey->setStraightKey(settings->halikeyStraightKey());
    connect(settings, &RadioSettings::halikeyStraightKeyChanged, this, [this](bool straight) {
        m_keyer->setStraightKeying(straight);
        m_halikey->setStraightKey(straight);
        applyReversal();
    });
    connect(settings, &RadioSettings::halikeyDitMemoryDelayChanged, this,
            [this](int percent) { m_keyer->setDitMemoryDelayPercent(percent); });
    connect(settings, &RadioSettings::halikeyMuteRadioMonitorChanged, this,
            [this](bool mute) { m_keyer->setMuteRadioMonitor(mute); });

    // =========================================================================
    // Inhibit: radio ready, in CW, KPOD+ not owning CW
    // =========================================================================
    connect(m_radioState, &RadioState::modeChanged, this, [this](RadioState::Mode) { updateInhibit(); });
    connect(m_connection, &ConnectionController::radioReady, this, [this]() {
        m_radioReady = true;
        updateInhibit();
    });
    connect(m_connection, &ConnectionController::connectionStateChanged, this,
            [this](TcpClient::ConnectionState state) {
                if (state != TcpClient::Connected) {
                    m_radioReady = false;
                    updateInhibit();
                }
            });
    updateInhibit();

    // =========================================================================
    // HaliKey → keyer
    // =========================================================================
    // V1.4 serial: one edge per change from the worker's 1 ms poll, on the worker thread
    // (invariant 1). The poller has already applied rest state, straight-key debounce and the
    // per-line delta.
    connect(
        m_halikey, &HalikeyDevice::paddleChanged, this,
        [this](bool dah, bool asserted, double deltaTime) { onPaddle(dah, asserted, deltaTime); },
        Qt::DirectConnection);

    // MIDI: raw messages with RtMidi's delta, on RtMidi's thread (invariant 1). MoMIDI's own time
    // stamps are decoded by the keyer.
    m_halikey->setMidiSink([this](int status, int note, int velocity, double deltaTime) {
        if (!m_cachedIsV14.load(std::memory_order_acquire))
            m_keyer->midiMessage(status, note, velocity, deltaTime);
    });

    // A lost device releases the key: a held paddle's release would otherwise never arrive.
    connect(m_halikey, &HalikeyDevice::disconnected, this, [this]() { m_keyer->releasePaddles(); });

    // =========================================================================
    // KPOD+ EP02 keyer data routing — unchanged
    // =========================================================================
    // The KPOD+ delivers complete KZ/KX strings in 32-byte transfers, zero-padded after the
    // last ';'. By targeting TcpClient as the receiver (lives on the I/O thread) with a
    // queued connection we skip the main thread entirely on this hot path. sendCATBytes
    // trims NUL padding on the I/O thread and hands off to sendCAT().
    connect(m_kpodPlus, &KpodPlusDevice::keyerDataReceived, m_connection->tcpClient(), &TcpClient::sendCATBytes,
            Qt::QueuedConnection);
}

CwController::~CwController() {
    // CONVENTIONS Rule 11. What actually protects the device-thread handlers is MainWindow::closeEvent
    // calling HardwareController::shutdownDevices() while ConnectionController is still alive; this
    // is the second line of defence.
    disconnect(this);
    m_halikey->setMidiSink(nullptr);
}

void CwController::applySpeed() {
    int wpm = m_radioState->keyerSpeed();
    if (wpm <= 0)
        wpm = 20;
    const int weight = m_radioState->keyingWeight();
    m_keyer->setSpeed(wpm, weight > 0 ? weight / 100.0 : 1.0);
}

void CwController::applyMode() {
    switch (RadioSettings::instance()->halikeyKeyerMode()) {
    case 1:
        m_keyer->setMode(PaddleKeyer::Mode::PlainIambic);
        break;
    case 2:
        m_keyer->setMode(PaddleKeyer::Mode::Ultimatic);
        break;
    default:
        m_keyer->setMode(m_radioState->iambicMode() == 'A' ? PaddleKeyer::Mode::IambicA : PaddleKeyer::Mode::IambicB);
        break;
    }
}

void CwController::applyReversal() {
    // A straight key is on the dit line whatever the paddle orientation.
    const bool reversed = m_radioState->paddleOrientation() == 'R' && !RadioSettings::instance()->halikeyStraightKey();
    const bool changed = m_reversed.exchange(reversed, std::memory_order_acq_rel) != reversed;
    m_keyer->setDitNotes({reversed ? 21 : 20});
    m_keyer->setDahNotes({reversed ? 20 : 21});
    // WHY only on a change: KP arrives again whenever the weight or iambic mode moves, which must
    // not cut a burst in progress. A real swap must, or a held lever would change meaning mid-key.
    if (changed)
        m_keyer->releasePaddles();
}

void CwController::updateInhibit() {
    const auto mode = m_radioState->mode();
    const bool inCw = mode == RadioState::CW || mode == RadioState::CW_R;
    const bool inhibited = !m_radioReady || !inCw || m_kpodPlusGate;
    m_keyer->setInhibited(inhibited);
}

void CwController::onPaddle(bool dah, bool asserted, double deltaTime) {
    if (!m_cachedIsV14.load(std::memory_order_acquire))
        return; // MIDI reaches the keyer through the raw sink
    if (m_reversed.load(std::memory_order_acquire))
        dah = !dah;
    if (dah)
        m_keyer->paddleEvent(asserted ? PaddleKeyer::Event::DahDown : PaddleKeyer::Event::DahUp, deltaTime);
    else
        m_keyer->paddleEvent(asserted ? PaddleKeyer::Event::DitDown : PaddleKeyer::Event::DitUp, deltaTime);
}

void CwController::setKeyDownInitialDelay(int ms) {
    m_connection->sendCAT(QString("KZL%1;").arg(qBound(0, ms, 1000), 4, 10, QChar('0')));
    m_connection->sendCAT(QStringLiteral("KZL;"));
}

void CwController::stopKeying() {
    m_keyer->releasePaddles();
}

void CwController::setKpodPlusGate(bool active) {
    // The single most useful line in a CW bench log: it says who is generating the elements.
    qCInfo(cwKeyer) << "KPOD+ keyer gate"
                    << (active ? "UP - the KPOD+ owns CW; QK4's keyer keys nothing"
                               : "DOWN - QK4's own keyer owns CW again");
    // Invariant 5: the store first, then the inhibit (which releases anything keyed).
    m_connection->setKpodPlusKeyerActive(active);
    m_kpodPlusGate = active;
    updateInhibit();
}
