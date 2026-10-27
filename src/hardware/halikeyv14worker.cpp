#include "halikeyv14worker.h"
#include "halikeyworkerbase.h"
#include "hardware/keyerruntime.h"
#include "hardware/seriallinepoller.h"
#include <QDebug>
#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

HaliKeyV14Worker::HaliKeyV14Worker(const QString &portName, QObject *parent) : HaliKeyWorkerBase(portName, parent) {}

HaliKeyV14Worker::~HaliKeyV14Worker() {
    closeNativePort();
}

void HaliKeyV14Worker::prepareShutdown() {
    // Nothing to unblock: the monitor loop polls every 1 ms and sees m_running go false within a
    // poll. The port is closed by the worker thread itself as the loop exits, never from here.
}

void HaliKeyV14Worker::start() {
    if (!openNativePort()) {
        return;
    }
    emit portOpened();
    m_running = true;
    monitorLoop();
}

bool HaliKeyV14Worker::openNativePort() {
#ifdef Q_OS_WIN
    QString path = "\\\\.\\" + m_portName;
    m_handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED, nullptr);
    if (m_handle == INVALID_HANDLE_VALUE) {
        m_handle = nullptr;
        QString error = "Failed to open port " + m_portName;
        qCWarning(hwHalikey) << "HaliKeyV14Worker:" << error;
        emit errorOccurred(error);
        return false;
    }

    // Configure serial port
    DCB dcb = {};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(m_handle, &dcb)) {
        QString error = "Failed to get port state for " + m_portName;
        qCWarning(hwHalikey) << "HaliKeyV14Worker:" << error;
        closeNativePort();
        emit errorOccurred(error);
        return false;
    }
    dcb.BaudRate = CBR_9600;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    if (!SetCommState(m_handle, &dcb)) {
        QString error = "Failed to configure port " + m_portName;
        qCWarning(hwHalikey) << "HaliKeyV14Worker:" << error;
        closeNativePort();
        emit errorOccurred(error);
        return false;
    }

    // WHY no SetCommMask: monitorLoop() polls GetCommModemStatus on a 1 ms timer rather than
    // blocking in WaitCommEvent, so no event mask is needed. See the WHY NOT WaitCommEvent
    // block in monitorLoop() for why the event-driven approach was abandoned.

    return true;
#else
    QByteArray devPath;
#ifdef Q_OS_MACOS
    devPath = ("/dev/" + m_portName).toUtf8();
    // macOS uses cu. prefix for outgoing connections
    if (!devPath.contains("/dev/cu.") && !devPath.contains("/dev/tty.")) {
        devPath = ("/dev/cu." + m_portName).toUtf8();
    }
#else
    // Linux
    if (m_portName.startsWith("/dev/")) {
        devPath = m_portName.toUtf8();
    } else {
        devPath = ("/dev/" + m_portName).toUtf8();
    }
#endif

    m_fd = ::open(devPath.constData(), O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (m_fd < 0) {
        QString error = QString("Failed to open port %1: %2").arg(m_portName, QString::fromLocal8Bit(strerror(errno)));
        qCWarning(hwHalikey) << "HaliKeyV14Worker:" << error;
        emit errorOccurred(error);
        return false;
    }

    // Configure raw serial
    struct termios tio = {};
    if (tcgetattr(m_fd, &tio) < 0) {
        QString error = QString("Failed to get port attributes for %1").arg(m_portName);
        qCWarning(hwHalikey) << "HaliKeyV14Worker:" << error;
        closeNativePort();
        emit errorOccurred(error);
        return false;
    }

    cfmakeraw(&tio);
    cfsetispeed(&tio, B9600);
    cfsetospeed(&tio, B9600);
    tio.c_cflag |= CLOCAL; // Ignore modem control lines for opening
    tcsetattr(m_fd, TCSANOW, &tio);

    // Enable DTR and RTS so the HaliKey has power to sense paddle contacts
    int bits = TIOCM_DTR | TIOCM_RTS;
    ioctl(m_fd, TIOCMBIS, &bits);

    return true;
#endif
}

void HaliKeyV14Worker::closeNativePort() {
#ifdef Q_OS_WIN
    if (m_handle) {
        CloseHandle(m_handle);
        m_handle = nullptr;
    }
#else
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
#endif
}

bool HaliKeyV14Worker::readPinState(bool &ditState, bool &dahState, bool &pttState) {
    // WHY: HaliKey V1.4 firmware exposes raw modem-status pin states. Empirically (verified via
    // qk4.hardware.debug logs in this user's setup, NORMAL paddle wiring):
    //
    //   physical dah lever  → DCD AND DSR transition together (always paired)
    //   physical dit lever  → CTS only
    //   foot pedal          → CTS only (indistinguishable from dit lever on the wire)
    //
    // We map the three logical signals as follows:
    //
    //   ditState = false                (V1.4 cannot report the dit lever on its own line — see
    //                                    pttState below, which carries it.)
    //   dahState = DCD || DSR           (paddle dah lever — both pins fire together; OR collapses
    //                                    them into one stable edge.)
    //   pttState = CTS                  (the dit lever, in CW. CwController maps it.)
    //
    // WHY the dit lever arrives on a field named pttState: a footswitch wired inline with the
    // paddles also drives CTS, so on this firmware the two are the same signal and the name kept
    // both readings open. Footswitch PTT is now withdrawn from both transports (see
    // cwcontroller.h), so CTS means the dit lever and nothing else; the field name is left alone
    // because it is the transport-level line name shared with the MIDI worker, not a claim about
    // what QK4 does with it.
#ifdef Q_OS_WIN
    DWORD modemStatus = 0;
    if (!GetCommModemStatus(m_handle, &modemStatus)) {
        return false;
    }
    bool cts = (modemStatus & MS_CTS_ON) != 0;
    bool dsr = (modemStatus & MS_DSR_ON) != 0;
    bool dcd = (modemStatus & MS_RLSD_ON) != 0;
    // Diagnostic: log each raw modem-line transition so we can see whether DCD and DSR
    // (which together form the dah lever via `dcd || dsr`) actually rise/fall as a pair.
    // A lingering DSR after DCD drops would latch dahState true → stuck dah. Chatty by
    // design; gated behind hw.halikey.debug. Compares against last-logged raw bits.
    if (cts != m_lastRawCts || dsr != m_lastRawDsr || dcd != m_lastRawDcd) {
        qCDebug(hwHalikey) << "HaliKeyV14Worker: raw pins  CTS:" << cts << " DCD:" << dcd << " DSR:" << dsr
                           << " => dah(dcd||dsr):" << (dcd || dsr);
        m_lastRawCts = cts;
        m_lastRawDsr = dsr;
        m_lastRawDcd = dcd;
    }
    ditState = false;
    dahState = dcd || dsr;
    pttState = cts;
    return true;
#else
    int status = 0;
    if (ioctl(m_fd, TIOCMGET, &status) < 0) {
        return false;
    }
    bool cts = (status & TIOCM_CTS) != 0;
    bool dsr = (status & TIOCM_DSR) != 0;
    bool dcd = (status & TIOCM_CD) != 0;
    ditState = false;
    dahState = dcd || dsr;
    pttState = cts;
    return true;
#endif
}

void HaliKeyV14Worker::monitorLoop() {
    // WHY one 1 ms poll on every platform, through SerialLinePoller: this is K4-Companion's serial
    // path (SerialInput.signal_loop), which its keyer was bench-tuned against. Three things it does
    // that the per-platform loops here did not:
    //   - the lines' state at open is rest, never an edge, so an adapter idling with a line asserted
    //     cannot look like a paddle held down;
    //   - a straight key waits for 20 ms without a change, so contact bounce never keys a short
    //     element;
    //   - each edge carries the time since that line's previous change, which is what a straight
    //     key's KZD/KZU are built from.
    // Paddles are reported on the first poll that sees them. The keyer reads its paddles from
    // time-stamped history, so a bounce inside an element changes nothing on the air. The old
    // two-read confirm only added 0.5-2 ms of latency. Linux used to wait in TIOCMIWAIT instead of
    // polling; a blocking wait cannot time the 20 ms quiet a straight key needs.
    //
    // Line map (see readPinState): dit lever = CTS, dah lever = DCD || DSR.
    using Signal = PaddleKeyer::Signal;
    bool ditLine = false;
    bool dahLine = false;
    auto sample = [&]() {
        bool unused = false, dah = false, cts = false;
        if (!readPinState(unused, dah, cts))
            return false;
        ditLine = cts;
        dahLine = dah;
        return true;
    };
    // One read per poll, shared by both lines, so the pair is always a joint sample.
    const SerialLinePoller::ReadLine read = [&](Signal s) { return s == Signal::CTS ? ditLine : dahLine; };

    auto fail = [this]() {
        if (!m_running)
            return;
#ifdef Q_OS_WIN
        const QString error = QStringLiteral("Failed to read pin state");
#else
        const QString error = QString("HaliKey monitor error: %1").arg(QString::fromLocal8Bit(strerror(errno)));
#endif
        qCWarning(hwHalikey) << "HaliKeyV14Worker:" << error;
        emit errorOccurred(error);
    };

    SteadyKeyerClock clock;
    bool straight = m_straightKey.load(std::memory_order_acquire);
    if (!sample()) {
        fail();
        closeNativePort();
        return;
    }
    SerialLinePoller poller(Signal::CTS, Signal::DCD, straight);
    poller.start(read);

#ifdef Q_OS_WIN
    // WHY a high-resolution waitable timer: GetCommModemStatus polled with Sleep(1) runs at the
    // 15.6 ms system tick, and raising it with timeBeginPeriod is process-global. Falls back to
    // Sleep(1) if the high-resolution timer can't be created.
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
    HANDLE pollTimer =
        CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (pollTimer) {
        LARGE_INTEGER due;
        due.QuadPart = -10000LL; // first fire in 1 ms (100 ns units, negative = relative)
        SetWaitableTimer(pollTimer, &due, 1 /* ms period */, nullptr, nullptr, FALSE);
    }
#endif

    while (m_running) {
#ifdef Q_OS_WIN
        if (pollTimer)
            WaitForSingleObject(pollTimer, 5); // ~1 ms tick; 5 ms cap bounds m_running latency
        else
            Sleep(1);
#else
        usleep(1000);
#endif
        if (!sample()) {
            fail();
            break;
        }
        // Switching between paddles and a straight key changes the debounce and which lines are
        // watched; the lines as they stand now are the new rest state.
        const bool nowStraight = m_straightKey.load(std::memory_order_acquire);
        if (nowStraight != straight) {
            straight = nowStraight;
            poller = SerialLinePoller(Signal::CTS, Signal::DCD, straight);
            poller.start(read);
            continue;
        }
        for (const auto &change : poller.poll(read, clock.now())) {
            const bool dah = change.signal != Signal::CTS;
            qCDebug(hwHalikey) << "HaliKeyV14Worker:" << (dah ? "dah" : "dit") << (change.asserted ? "down" : "up")
                               << "after" << change.deltaTime * 1000 << "ms";
            emit paddleChanged(dah, change.asserted, change.deltaTime);
        }
    }

#ifdef Q_OS_WIN
    if (pollTimer)
        CloseHandle(pollTimer);
#endif
    closeNativePort();
}
