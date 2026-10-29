// HaliKey port enumeration off the GUI thread (src/hardware/halikeyportscan.h): which port is
// selected, results arrive queued on the requesting thread from a different one, a repeat request
// is absorbed, and a request for another device type replaces the scan in flight.

#include "hardware/halikeyportscan.h"
#include <QAtomicInt>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <memory>

class TestHaliKeyPortScan : public QObject {
    Q_OBJECT

private slots:
    void chooseIndex_data() {
        QTest::addColumn<QStringList>("ports");
        QTest::addColumn<QString>("saved");
        QTest::addColumn<int>("type");
        QTest::addColumn<int>("expected");
        const QStringList serial{"cu.Bluetooth", "tty.usbserial-A", "tty.usbserial-B"};
        const QStringList midi{"IAC Bus", "Some Synth", "HaliKey MIDI"};
        QTest::newRow("serial saved found") << serial << "tty.usbserial-B" << 0 << 2;
        QTest::newRow("serial saved gone") << serial << "tty.usbserial-Z" << 0 << -1;
        QTest::newRow("serial nothing saved") << serial << QString() << 0 << -1;
        QTest::newRow("midi saved found") << midi << "Some Synth" << 1 << 1;
        QTest::newRow("midi saved gone picks HaliKey") << midi << "Gone" << 1 << 2;
        QTest::newRow("midi HaliKey match is case-blind") << QStringList{"x", "HALIKEY 2"} << QString() << 1 << 1;
        QTest::newRow("midi no HaliKey") << QStringList{"a", "b"} << QString() << 1 << -1;
        QTest::newRow("serial never guesses HaliKey") << QStringList{"a", "HaliKey"} << QString() << 0 << -1;
        QTest::newRow("empty list") << QStringList{} << "tty.usbserial-A" << 0 << -1;
    }

    void chooseIndex() {
        QFETCH(QStringList, ports);
        QFETCH(QString, saved);
        QFETCH(int, type);
        QFETCH(int, expected);
        QCOMPARE(HaliKeyPortScan::chooseIndex(ports, saved, type), expected);
    }

    void resultArrivesQueuedFromAnotherThread() {
        HaliKeyPortScan scan;
        QSignalSpy spy(&scan, &HaliKeyPortScan::finished);
        auto enumThread = std::make_shared<QThread *>(nullptr);
        scan.request(0, [enumThread]() {
            *enumThread = QThread::currentThread();
            return QStringList{"tty.a", "tty.b"};
        });
        QVERIFY(scan.isScanning());
        QCOMPARE(spy.count(), 0); // never synchronous: the dialog paints before the list is known
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 0);
        QCOMPARE(spy.at(0).at(1).toStringList(), (QStringList{"tty.a", "tty.b"}));
        QVERIFY(*enumThread != nullptr);
        QVERIFY(*enumThread != QThread::currentThread());
        QVERIFY(!scan.isScanning());
    }

    void repeatRequestIsAbsorbed() {
        HaliKeyPortScan scan;
        QSignalSpy spy(&scan, &HaliKeyPortScan::finished);
        auto gate = std::make_shared<QSemaphore>();
        auto calls = std::make_shared<QAtomicInt>(0);
        auto enumerate = [gate, calls]() {
            calls->fetchAndAddOrdered(1);
            gate->acquire();
            return QStringList{"tty.a"};
        };
        scan.request(0, enumerate);
        scan.request(0, enumerate); // the page's refresh() right after construction
        gate->release(2);
        QVERIFY(spy.wait(5000));
        QTest::qWait(50);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(calls->loadAcquire(), 1);
    }

    void otherTypeReplacesScanInFlight() {
        HaliKeyPortScan scan;
        QSignalSpy spy(&scan, &HaliKeyPortScan::finished);
        auto gate = std::make_shared<QSemaphore>();
        scan.request(0, [gate]() {
            gate->acquire();
            return QStringList{"tty.serial"};
        });
        scan.request(1, []() { return QStringList{"HaliKey MIDI"}; }); // operator switched to MIDI
        gate->release();
        QVERIFY(spy.wait(5000));
        QTest::qWait(50);
        QCOMPARE(spy.count(), 1); // the serial list is never shown under the MIDI device type
        QCOMPARE(spy.at(0).at(0).toInt(), 1);
        QCOMPARE(spy.at(0).at(1).toStringList(), QStringList{"HaliKey MIDI"});
    }

    void destroyedWhileScanningIsSafe() {
        auto gate = std::make_shared<QSemaphore>();
        {
            HaliKeyPortScan scan;
            scan.request(0, [gate]() {
                gate->acquire();
                return QStringList{"tty.a"};
            });
        }
        gate->release();
        // The worker finishes after its scanner is gone; nothing may be delivered or crash.
        QTest::qWait(100);
    }
};

QTEST_GUILESS_MAIN(TestHaliKeyPortScan)
#include "test_halikeyportscan.moc"
