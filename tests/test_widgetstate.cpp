// What a widget shows must match the state behind it. Each case reproduces a review finding where a
// widget's screen drifted from its own model: typed frequency digits overwritten by the radio, a DLY
// label replaced after the PWR/DLY swap, an NB filter shown as NONE, a refused LINE OUT click left
// checked, a reused confirm popup that stopped reporting cancels, and a macro editor that Tab closed
// behind the dialog's back. Rendered offscreen.

#include "ui/dialogs/macrodialog.h"
#include "ui/overlays/dxspotoverlay.h"
#include "ui/popups/confirmpopup.h"
#include "ui/popups/lineoutpopup.h"
#include "ui/widgets/dualcontrolbutton.h"
#include "ui/widgets/featuremenubar.h"
#include "ui/widgets/frequencydisplaywidget.h"
#include "ui/widgets/sidecontrolpanel.h"

#include <QApplication>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class WidgetStateTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_tempDir;

    static QPushButton *buttonWithText(QWidget *parent, const QString &text) {
        for (QPushButton *b : parent->findChildren<QPushButton *>()) {
            if (b->text() == text)
                return b;
        }
        return nullptr;
    }

private slots:
    void initTestCase() {
        // Macro and side-panel code read saved settings; keep them off the real preferences.
        QVERIFY(m_tempDir.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tempDir.path());
    }

    // --- FrequencyDisplayWidget ---

    void frequencyUpdateDuringEntryKeepsTypedDigits() {
        FrequencyDisplayWidget w;
        w.setFrequency("7024980");
        w.show();
        w.beginEntry();
        QVERIFY(w.isEditing());

        QTest::keyClicks(&w, "00014");
        w.setFrequency("7025000"); // radio report arrives mid-entry
        QCOMPARE(w.frequency().left(5), QString("00014"));
    }

    void frequencyEntrySendsTypedDigitsNotRadioUpdate() {
        FrequencyDisplayWidget w;
        w.setFrequency("7024980");
        w.show();
        QSignalSpy entered(&w, &FrequencyDisplayWidget::frequencyEntered);
        w.beginEntry();
        QTest::keyClicks(&w, "0014074000");
        w.setFrequency("7025000");
        QTest::keyClick(&w, Qt::Key_Return);

        QCOMPARE(entered.count(), 1);
        QCOMPARE(entered.at(0).at(0).toString(), QString("14074000"));
    }

    void frequencyCancelShowsLatestRadioValue() {
        FrequencyDisplayWidget w;
        w.setFrequency("7024980");
        w.show();
        w.beginEntry();
        QTest::keyClicks(&w, "999");
        w.setFrequency("7025000");
        QTest::keyClick(&w, Qt::Key_Escape);

        QVERIFY(!w.isEditing());
        QCOMPARE(w.displayText(), QString("7.025.000"));
    }

    void frequencyUpdateOutsideEntryShowsImmediately() {
        FrequencyDisplayWidget w;
        w.setFrequency("14.074.000");
        QCOMPARE(w.displayText(), QString("14.074.000"));
    }

    // --- SideControlPanel PWR/DLY ---

    void powerUpdateAfterSwapKeepsDelayLabel() {
        QWidget host;
        auto *panel = new SideControlPanel(&host);
        host.show();

        DualControlButton *pwr = nullptr;
        for (auto *b : panel->findChildren<DualControlButton *>()) {
            if (b->primaryLabel() == "PWR")
                pwr = b;
        }
        QVERIFY(pwr);

        // First click activates the button, the second swaps PWR and DLY.
        QTest::mouseClick(pwr, Qt::LeftButton);
        pwr->setShowIndicator(true);
        QTest::mouseClick(pwr, Qt::LeftButton);
        QCOMPARE(pwr->primaryLabel(), QString("DLY"));

        panel->setPower(50.0, LevelsState::PowerRange::Qro);
        QCOMPARE(pwr->primaryLabel(), QString("DLY"));
        QCOMPARE(pwr->alternateLabel(), QString("PWR"));
        QCOMPARE(pwr->alternateValue(), QString("50"));

        panel->setPower(1.5, LevelsState::PowerRange::Xvtr);
        QCOMPARE(pwr->primaryLabel(), QString("DLY"));
        QCOMPARE(pwr->alternateLabel(), QString("mW"));
    }

    // --- FeatureMenuBar NB filter ---

    void nbLevelOpensWithCurrentFilter() {
        QWidget host;
        auto *bar = new FeatureMenuBar(&host);
        bar->setNbFilter(2);
        bar->showForFeature(FeatureMenuBar::NbLevel);
        QVERIFY(buttonWithText(bar, "FILTER\nWIDE"));
        QVERIFY(!buttonWithText(bar, "FILTER\nNONE"));
    }

    // --- LineOutPopupWidget ---

    void lineOutRightRefusedWhileLinked() {
        LineOutPopupWidget popup;
        QPushButton *left = buttonWithText(&popup, "LEFT");
        QPushButton *right = buttonWithText(&popup, "RIGHT");
        QVERIFY(left && right);

        popup.setRightEqualsLeft(true);
        right->click(); // refused: R=L forces the left channel
        QVERIFY(left->isChecked());
        QVERIFY(!right->isChecked());

        popup.setRightEqualsLeft(false);
        QVERIFY(left->isChecked());
        QVERIFY(!right->isChecked());

        right->click();
        QVERIFY(!left->isChecked());
        QVERIFY(right->isChecked());
    }

    void lineOutLinkButtonFollowsState() {
        LineOutPopupWidget popup;
        QPushButton *link = buttonWithText(&popup, "RIGHT\n=LEFT");
        QVERIFY(link);
        link->click();
        QVERIFY(popup.rightEqualsLeft());
        QVERIFY(link->isChecked());
        link->click();
        QVERIFY(!popup.rightEqualsLeft());
        QVERIFY(!link->isChecked());
    }

    // --- ConfirmPopup ---

    void confirmPopupReportsCancelAfterEarlierConfirm() {
        ConfirmPopup popup("TITLE", "Body", "YES", "NO");
        QSignalSpy confirmed(&popup, &ConfirmPopup::confirmed);
        QSignalSpy cancelled(&popup, &ConfirmPopup::cancelled);

        popup.show();
        buttonWithText(&popup, "YES")->click();
        QCOMPARE(confirmed.count(), 1);
        QCOMPARE(cancelled.count(), 0);

        popup.show();
        buttonWithText(&popup, "NO")->click();
        QCOMPARE(confirmed.count(), 1);
        QCOMPARE(cancelled.count(), 1);
    }

    // --- MacroDialog ---

    void macroTabSwitchesFieldAndStaysInEditMode() {
        MacroDialog dialog;
        dialog.resize(800, 600);
        dialog.show();
        dialog.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));

        QTest::keyClick(&dialog, Qt::Key_Return); // edit the first macro's label
        QPushButton *done = buttonWithText(&dialog, "DONE");
        QVERIFY(done);

        auto *labelEdit = qobject_cast<QLineEdit *>(QApplication::focusWidget());
        QVERIFY(labelEdit);
        QCOMPARE(labelEdit->maxLength(), 12);

        QTest::keyClick(labelEdit, Qt::Key_Tab);
        QCOMPARE(done->text(), QString("DONE"));
        auto *commandEdit = qobject_cast<QLineEdit *>(QApplication::focusWidget());
        QVERIFY(commandEdit);
        QCOMPARE(commandEdit->maxLength(), 64);
        QVERIFY(commandEdit->isVisible());
        QVERIFY(!labelEdit->isVisible());

        QTest::keyClick(commandEdit, Qt::Key_Backtab);
        QCOMPARE(done->text(), QString("DONE"));
        QCOMPARE(QApplication::focusWidget(), labelEdit);
        QVERIFY(labelEdit->isVisible());
        QVERIFY(!commandEdit->isVisible());
    }

    void spotLabelsKeepOutOfTheAmplitudeAxisColumn() {
        // A spot whose frequency falls under the dBm/S-unit labels used to be drawn on top of them.
        // Its label now starts just right of the reserved column, and still tunes to the spot.
        DxSpotOverlay overlay;
        overlay.resize(400, 200);
        overlay.setFrequencyRange(14'000'000, 10'000); // 14.000 MHz +/- 5 kHz across 400 px
        overlay.setLeftReservedPx(70);
        DxSpot spot;
        spot.spottedCall = QStringLiteral("EA1EYL");
        spot.frequencyHz = 13'995'500; // x = 20 px, inside the column
        overlay.setSpots({spot});

        QSignalSpy clicked(&overlay, &DxSpotOverlay::spotClicked);
        const int rowY = 200 / 2 - 8; // inside row 0, which sits just above the midpoint
        QTest::mouseClick(&overlay, Qt::LeftButton, {}, QPoint(20, rowY));
        QCOMPARE(clicked.count(), 0);
        QTest::mouseClick(&overlay, Qt::LeftButton, {}, QPoint(75, rowY));
        QCOMPARE(clicked.count(), 1);
        QCOMPARE(clicked.at(0).at(0).toLongLong(), 13'995'500LL);
    }
};

QTEST_MAIN(WidgetStateTests)
#include "test_widgetstate.moc"
