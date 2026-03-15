#ifndef IAMBICKEYER_H
#define IAMBICKEYER_H

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

class IambicKeyer : public QObject {
    Q_OBJECT

public:
    enum Mode { IambicA, IambicB };
    Q_ENUM(Mode)

    explicit IambicKeyer(QObject *parent = nullptr);

    void setMode(Mode mode);
    void setReversed(bool reversed);
    void setSpeed(int wpm);

    // Accept PHYSICAL paddle state — reversal applied internally
    void setDitPaddle(bool pressed);
    void setDahPaddle(bool pressed);

    // Emergency stop (disconnect, etc.)
    void stop();

    bool isKeying() const;

signals:
    void elementStarted(bool isDit);
    void characterSpace();
    void restartAfterPause(int ms);
    void keyingFinished();

private:
    enum State { Idle, PlayingDit, PlayingDah };

    void handlePaddleChange();
    void enterElement(bool isDit);
    void onTimerFired();
    void goIdle();

    QTimer *m_elementTimer;
    State m_state = Idle;
    Mode m_mode = IambicA;
    bool m_reversed = false;
    bool m_ditPaddle = false; // logical (post-reversal)
    bool m_dahPaddle = false; // logical (post-reversal)
    bool m_squeezed = false;  // both paddles held during current element
    int m_ditMs = 60;         // 1200 / WPM
    QElapsedTimer m_idleSince;
};

#endif // IAMBICKEYER_H
