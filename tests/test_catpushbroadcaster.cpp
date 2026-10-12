#include "models/radiostate.h"
#include "network/catpushbroadcaster.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QList>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

class TestCatPushBroadcaster : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void testDefaultAiModeIsZero();
    void testSetAiModeUpdatesLevel();
    void testRemoveClientDropsSubscription();
    void testPushFrequencyOnRadioStateChange();
    void testPollingClientReceivesNothing();
    void testTwoClientsDifferentAiModes();
    void testCompoundProcessingDiff();
    void testRitXitCompoundEmitsAllThree();
    void testPushPowerCarriesRangeLetter();
    void testPushBandwidthInTenHertzUnits();
    void testPushNoiseBlankerLevelChange();
    void testPushNoiseReductionLevelChange();
    void testPushVoxOnlyForTheCurrentMode();

private:
    struct SocketPair {
        QTcpSocket *clientSide;
        QTcpSocket *serverSide;
    };

    SocketPair makeSocketPair();
    QByteArray waitForBytes(QTcpSocket *sock, int minBytes, int timeoutMs = 500);

    QTcpServer *m_listener = nullptr;
    QList<QTcpSocket *> m_managedSockets;
    RadioState *m_radioState = nullptr;
    CatPushBroadcaster *m_broadcaster = nullptr;
};

void TestCatPushBroadcaster::initTestCase() {
    m_listener = new QTcpServer(this);
    QVERIFY(m_listener->listen(QHostAddress::LocalHost, 0));
}

void TestCatPushBroadcaster::cleanupTestCase() {
    if (m_listener) {
        m_listener->close();
        delete m_listener;
        m_listener = nullptr;
    }
}

void TestCatPushBroadcaster::init() {
    m_radioState = new RadioState();
    m_broadcaster = new CatPushBroadcaster(m_radioState);
}

void TestCatPushBroadcaster::cleanup() {
    delete m_broadcaster;
    m_broadcaster = nullptr;
    delete m_radioState;
    m_radioState = nullptr;

    for (QTcpSocket *sock : m_managedSockets) {
        if (!sock) {
            continue;
        }
        if (sock->state() == QAbstractSocket::ConnectedState) {
            sock->disconnectFromHost();
            if (sock->state() != QAbstractSocket::UnconnectedState) {
                sock->waitForDisconnected(200);
            }
        }
        sock->deleteLater();
    }
    m_managedSockets.clear();
    QCoreApplication::processEvents();
}

TestCatPushBroadcaster::SocketPair TestCatPushBroadcaster::makeSocketPair() {
    QTcpSocket *clientSide = new QTcpSocket();
    m_managedSockets.append(clientSide);
    clientSide->connectToHost(QHostAddress::LocalHost, m_listener->serverPort());
    [&]() { QVERIFY(clientSide->waitForConnected(500)); }();

    [&]() { QVERIFY(m_listener->waitForNewConnection(500)); }();
    QTcpSocket *serverSide = m_listener->nextPendingConnection();
    Q_ASSERT(serverSide);
    m_managedSockets.append(serverSide);

    QCoreApplication::processEvents();
    return {clientSide, serverSide};
}

QByteArray TestCatPushBroadcaster::waitForBytes(QTcpSocket *sock, int minBytes, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (sock->bytesAvailable() < minBytes && timer.elapsed() < timeoutMs) {
        sock->waitForReadyRead(50);
        QCoreApplication::processEvents();
    }
    return sock->readAll();
}

void TestCatPushBroadcaster::testDefaultAiModeIsZero() {
    auto pair = makeSocketPair();
    m_broadcaster->addClient(pair.serverSide);
    QCOMPARE(m_broadcaster->clientAiMode(pair.serverSide), 0);
    QCOMPARE(m_broadcaster->subscriberCount(), 1);
}

void TestCatPushBroadcaster::testSetAiModeUpdatesLevel() {
    auto pair = makeSocketPair();
    m_broadcaster->addClient(pair.serverSide);

    const int modes[] = {0, 1, 2, 4};
    for (int m : modes) {
        m_broadcaster->setClientAiMode(pair.serverSide, m);
        QCOMPARE(m_broadcaster->clientAiMode(pair.serverSide), m);
    }
}

void TestCatPushBroadcaster::testRemoveClientDropsSubscription() {
    auto pair = makeSocketPair();
    m_broadcaster->addClient(pair.serverSide);
    m_broadcaster->setClientAiMode(pair.serverSide, 2);
    QCOMPARE(m_broadcaster->subscriberCount(), 1);

    m_broadcaster->removeClient(pair.serverSide);
    QCOMPARE(m_broadcaster->subscriberCount(), 0);
    QCOMPARE(m_broadcaster->clientAiMode(pair.serverSide), 0);
}

void TestCatPushBroadcaster::testPushFrequencyOnRadioStateChange() {
    auto pair = makeSocketPair();
    m_broadcaster->addClient(pair.serverSide);
    m_broadcaster->setClientAiMode(pair.serverSide, 2);

    m_radioState->parseCATCommand("FA00014074000;");
    QCoreApplication::processEvents();

    QByteArray received = waitForBytes(pair.clientSide, 14);
    QCOMPARE(received, QByteArray("FA00014074000;"));
}

// #134. Without the letter a subscriber reads 0.6 W as 6 W, and XVTR milliwatts could not be pushed
// at all - they were skipped for exactly that reason.
void TestCatPushBroadcaster::testPushPowerCarriesRangeLetter() {
    auto pair = makeSocketPair();
    m_broadcaster->addClient(pair.serverSide);
    m_broadcaster->setClientAiMode(pair.serverSide, 2);

    m_radioState->parseCATCommand("PC006L;");
    QCoreApplication::processEvents();
    QCOMPARE(waitForBytes(pair.clientSide, 7), QByteArray("PC006L;"));

    m_radioState->parseCATCommand("PC050X;");
    QCoreApplication::processEvents();
    QCOMPARE(waitForBytes(pair.clientSide, 7), QByteArray("PC050X;"));
}

// #134. A subscriber was pushed BW3000; for a 3000 Hz filter, which reads as 30 kHz.
void TestCatPushBroadcaster::testPushBandwidthInTenHertzUnits() {
    auto pair = makeSocketPair();
    m_broadcaster->addClient(pair.serverSide);
    m_broadcaster->setClientAiMode(pair.serverSide, 2);

    m_radioState->parseCATCommand("BW0300;");
    QCoreApplication::processEvents();
    QCOMPARE(waitForBytes(pair.clientSide, 7), QByteArray("BW0300;"));
}

// #134. Only on/off was compared, so a level change - which the radio reports - never reached a
// subscriber. Captured NB0301 is level 03, off, filter 1.
void TestCatPushBroadcaster::testPushNoiseBlankerLevelChange() {
    m_radioState->parseCATCommand("NB0301;");
    QCoreApplication::processEvents(); // seeds the cache

    auto pair = makeSocketPair();
    m_broadcaster->addClient(pair.serverSide);
    m_broadcaster->setClientAiMode(pair.serverSide, 2);

    m_radioState->parseCATCommand("NB0501;");
    QCoreApplication::processEvents();
    QCOMPARE(waitForBytes(pair.clientSide, 7), QByteArray("NB0501;"));
}

// #134. Same as NB: a level change never reached a subscriber, and the frame was NR0/NR1.
void TestCatPushBroadcaster::testPushNoiseReductionLevelChange() {
    m_radioState->parseCATCommand("NR050;");
    QCoreApplication::processEvents(); // seeds the cache

    auto pair = makeSocketPair();
    m_broadcaster->addClient(pair.serverSide);
    m_broadcaster->setClientAiMode(pair.serverSide, 2);

    m_radioState->parseCATCommand("NR070;");
    QCoreApplication::processEvents();
    QCOMPARE(waitForBytes(pair.clientSide, 6), QByteArray("NR070;"));
}

// #134. A VOX change in another mode group pushed VX1 - VOX on - to a client in DATA, where it
// was off. Only the current mode's group is reported now.
void TestCatPushBroadcaster::testPushVoxOnlyForTheCurrentMode() {
    m_radioState->parseCATCommand("MD6;");
    m_radioState->parseCATCommand("VXD0;");
    QCoreApplication::processEvents();

    auto pair = makeSocketPair();
    m_broadcaster->addClient(pair.serverSide);
    m_broadcaster->setClientAiMode(pair.serverSide, 2);

    m_radioState->parseCATCommand("VXC1;"); // CW group: not the current mode
    QCoreApplication::processEvents();
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 100) {
        pair.clientSide->waitForReadyRead(20);
        QCoreApplication::processEvents();
    }
    QCOMPARE(pair.clientSide->bytesAvailable(), qint64(0));

    m_radioState->parseCATCommand("VXD1;"); // DATA group: the current mode
    QCoreApplication::processEvents();
    QCOMPARE(waitForBytes(pair.clientSide, 5), QByteArray("VXD1;"));
}

void TestCatPushBroadcaster::testPollingClientReceivesNothing() {
    auto pair = makeSocketPair();
    m_broadcaster->addClient(pair.serverSide);

    m_radioState->parseCATCommand("FA00014074000;");
    QCoreApplication::processEvents();

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 100) {
        pair.clientSide->waitForReadyRead(20);
        QCoreApplication::processEvents();
    }
    QCOMPARE(pair.clientSide->bytesAvailable(), qint64(0));
}

void TestCatPushBroadcaster::testTwoClientsDifferentAiModes() {
    auto pairA = makeSocketPair();
    auto pairB = makeSocketPair();

    m_broadcaster->addClient(pairA.serverSide);
    m_broadcaster->addClient(pairB.serverSide);
    m_broadcaster->setClientAiMode(pairA.serverSide, 0);
    m_broadcaster->setClientAiMode(pairB.serverSide, 2);

    m_radioState->parseCATCommand("FA00014074000;");
    QCoreApplication::processEvents();

    QByteArray receivedB = waitForBytes(pairB.clientSide, 14);
    QCOMPARE(receivedB, QByteArray("FA00014074000;"));

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 100) {
        pairA.clientSide->waitForReadyRead(20);
        QCoreApplication::processEvents();
    }
    QCOMPARE(pairA.clientSide->bytesAvailable(), qint64(0));
}

void TestCatPushBroadcaster::testCompoundProcessingDiff() {
    auto pair = makeSocketPair();
    m_broadcaster->addClient(pair.serverSide);
    m_broadcaster->setClientAiMode(pair.serverSide, 2);

    m_radioState->parseCATCommand("NB051;");
    QCoreApplication::processEvents();

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 100) {
        pair.clientSide->waitForReadyRead(20);
        QCoreApplication::processEvents();
    }
    QCOMPARE(pair.clientSide->bytesAvailable(), qint64(0));

    m_radioState->parseCATCommand("NB050;");
    QCoreApplication::processEvents();

    QByteArray received = waitForBytes(pair.clientSide, 7);
    QCOMPARE(received, QByteArray("NB0500;"));
}

void TestCatPushBroadcaster::testRitXitCompoundEmitsAllThree() {
    auto pair = makeSocketPair();
    m_broadcaster->addClient(pair.serverSide);
    m_broadcaster->setClientAiMode(pair.serverSide, 2);

    m_radioState->parseCATCommand("RT1;");
    QCoreApplication::processEvents();

    QByteArray received = waitForBytes(pair.clientSide, 16);
    QCOMPARE(received, QByteArray("RO+0000;RT1;XT0;"));
}

QTEST_MAIN(TestCatPushBroadcaster)
#include "test_catpushbroadcaster.moc"
