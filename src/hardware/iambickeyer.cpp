#include "hardware/iambickeyer.h"

IambicKeyer::IambicKeyer(QObject *parent) : QObject(parent) {
    m_elementTimer = new QTimer(this);
    m_elementTimer->setSingleShot(true);
    m_elementTimer->setTimerType(Qt::PreciseTimer);
    connect(m_elementTimer, &QTimer::timeout, this, &IambicKeyer::onTimerFired);
}

void IambicKeyer::setMode(Mode mode) {
    m_mode = mode;
}

void IambicKeyer::setReversed(bool reversed) {
    m_reversed = reversed;
}

void IambicKeyer::setSpeed(int wpm) {
    if (wpm > 0)
        m_ditMs = 1200 / wpm;
}

void IambicKeyer::setDitPaddle(bool pressed) {
    // Map physical paddle to logical, applying reversal
    if (m_reversed)
        m_dahPaddle = pressed;
    else
        m_ditPaddle = pressed;
    handlePaddleChange();
}

void IambicKeyer::setDahPaddle(bool pressed) {
    if (m_reversed)
        m_ditPaddle = pressed;
    else
        m_dahPaddle = pressed;
    handlePaddleChange();
}

void IambicKeyer::handlePaddleChange() {
    // Track squeeze state during active element
    if (m_state != Idle && m_ditPaddle && m_dahPaddle)
        m_squeezed = true;

    // Start keying if idle and any paddle is down
    if (m_state == Idle) {
        if (m_ditPaddle && !m_dahPaddle)
            enterElement(true);
        else if (m_dahPaddle && !m_ditPaddle)
            enterElement(false);
        else if (m_ditPaddle && m_dahPaddle)
            enterElement(true); // squeeze from idle starts with dit
    }
}

void IambicKeyer::enterElement(bool isDit) {
    m_state = isDit ? PlayingDit : PlayingDah;
    m_squeezed = false;

    // Re-check current paddles for squeeze detection within this element
    if (m_ditPaddle && m_dahPaddle)
        m_squeezed = true;

    // Dit = 1 unit on + 1 unit off = 2 ditMs; Dah = 3 units on + 1 unit off = 4 ditMs
    int interval = isDit ? m_ditMs * 2 : m_ditMs * 4;
    m_elementTimer->start(interval);
    emit elementStarted(isDit);
}

void IambicKeyer::onTimerFired() {
    bool wasDit = (m_state == PlayingDit);

    if (m_ditPaddle && m_dahPaddle) {
        // Both held — squeeze alternation
        enterElement(!wasDit);
    } else if (wasDit && m_dahPaddle) {
        // Opposite paddle held — cross-paddle
        enterElement(false);
    } else if (!wasDit && m_ditPaddle) {
        // Opposite paddle held — cross-paddle
        enterElement(true);
    } else if (wasDit && m_ditPaddle) {
        // Same paddle held — repeat
        enterElement(true);
    } else if (!wasDit && m_dahPaddle) {
        // Same paddle held — repeat
        enterElement(false);
    } else if (m_squeezed && m_mode == IambicB) {
        // Iambic B: one extra opposite element after squeeze release
        enterElement(!wasDit);
    } else {
        // Nothing held, no squeeze memory — done
        goIdle();
    }
}

void IambicKeyer::goIdle() {
    m_state = Idle;
    m_elementTimer->stop();
    m_squeezed = false;
    emit keyingFinished();
}

void IambicKeyer::stop() {
    if (m_state != Idle) {
        m_state = Idle;
        m_elementTimer->stop();
        m_squeezed = false;
        m_ditPaddle = false;
        m_dahPaddle = false;
        emit keyingFinished();
    }
}

bool IambicKeyer::isKeying() const {
    return m_state != Idle;
}
