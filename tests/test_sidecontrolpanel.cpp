// Left side panel layout (src/ui/widgets/sidecontrolpanel.cpp), rendered offscreen at the smallest
// size its layout allows. That is the size the main window gives it when the window is at its own
// minimum, so nothing in the panel may overlap there.
//
// WHY: testers on Windows and Linux saw the TX buttons' orange sub-labels painted over the buttons.
// The window's hand-written minimum was smaller than this panel needed, and the layout squeezed each
// button-and-label pair from 49 px to 42. This holds the panel to its own minimum and checks the
// pairs, and every other control, stay apart.

#include "ui/widgets/rightsidepanel.h"
#include "ui/widgets/sidecontrolpanel.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

class SideControlPanelLayoutTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_tempDir;

    // Rect in the panel's coordinates.
    static QRect inPanel(const QWidget *panel, const QWidget *w) {
        return QRect(w->mapTo(panel, QPoint(0, 0)), w->size());
    }

private slots:
    void initTestCase() {
        // The panel reads the saved volumes; keep it off the real preferences.
        QVERIFY(m_tempDir.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tempDir.path());
    }

    // The first overlap found with the panel at `height`, or an empty string.
    static QString firstOverlap(int height, int *pairsOut = nullptr) {
        // WHY: hosted as a child, the way MainWindow holds it. A top-level widget takes its minimum
        // from its layout and would refuse the squeeze this has to be able to reproduce.
        QWidget host;
        auto &panel = *new SideControlPanel(&host);
        panel.setGeometry(0, 0, panel.minimumSizeHint().width(), height);
        host.resize(panel.size());
        host.show();
        QCoreApplication::processEvents();

        // Each TX button shares a container with its sub-label.
        int pairs = 0;
        QString found;
        for (QLabel *label : panel.findChildren<QLabel *>()) {
            QWidget *container = label->parentWidget();
            if (container == &panel || !label->isVisible())
                continue;
            const auto buttons = container->findChildren<QPushButton *>(Qt::FindDirectChildrenOnly);
            if (buttons.size() != 1)
                continue;
            ++pairs;
            if (found.isEmpty() && inPanel(&panel, buttons.first()).intersects(inPanel(&panel, label)))
                found = QString("'%1' sub-label overlaps its button").arg(label->text());
        }
        if (pairsOut)
            *pairsOut = pairs;

        // And no two of the panel's own controls overlap each other. Overlays are hidden here.
        QList<QWidget *> children;
        for (QWidget *w : panel.findChildren<QWidget *>(Qt::FindDirectChildrenOnly))
            if (w->isVisible())
                children << w;
        for (int i = 0; i < children.size() && found.isEmpty(); ++i)
            for (int j = i + 1; j < children.size() && found.isEmpty(); ++j)
                if (children[i]->geometry().intersects(children[j]->geometry()))
                    found = QString("%1 overlaps %2")
                                .arg(children[i]->metaObject()->className(), children[j]->metaObject()->className());
        return found;
    }

    void nothingOverlapsAtMinimumSize() {
        SideControlPanel probe;
        int pairs = 0;
        const QString overlap = firstOverlap(probe.minimumSizeHint().height(), &pairs);
        QCOMPARE(pairs, 6);
        QVERIFY2(overlap.isEmpty(), qPrintable(overlap));
    }

    void squeezeOfTheReportedBugIsDetected() {
        // 22 px short is what the window's old 1340x840 minimum left the panel once a Windows or
        // Linux menu bar took its share. If this stops detecting an overlap the check above has
        // gone blind, not the bug away.
        SideControlPanel probe;
        QVERIFY(!firstOverlap(probe.minimumSizeHint().height() - 22).isEmpty());
    }

    // The first orange sub-label in `panel` whose text does not fit its width, or an empty string.
    // kGapPx keeps neighbours (REM ANT | SUB ANT) from touching even when each one just fits.
    static QString firstClippedSubLabel(QWidget &panel) {
        static constexpr qreal kGapPx = 2.0;
        QWidget host;
        panel.setParent(&host);
        panel.setGeometry(0, 0, panel.minimumSizeHint().width(), panel.minimumSizeHint().height());
        host.resize(panel.size());
        host.show();
        QCoreApplication::processEvents();
        for (QLabel *label : panel.findChildren<QLabel *>()) {
            if (!label->isVisible() || label->text().isEmpty() || label->text().contains('\n'))
                continue;
            const qreal needed = QFontMetricsF(label->font(), label).horizontalAdvance(label->text()) + kGapPx;
            if (label->width() < needed)
                return QString("\"%1\" needs %2 px, has %3").arg(label->text()).arg(needed).arg(label->width());
        }
        return {};
    }

    void everyLeftPanelSubLabelFits() {
        // At the 10 px text floor REM ANT and SUB ANT were drawn edge to edge.
        auto *panel = new SideControlPanel;
        const QString clipped = firstClippedSubLabel(*panel);
        QVERIFY2(clipped.isEmpty(), qPrintable(clipped));
    }

    void everyRightPanelSubLabelFits() {
        // At the 10 px text floor DIVERSITY, under SUB, was cut to "IVERSITY".
        auto *panel = new RightSidePanel;
        const QString clipped = firstClippedSubLabel(*panel);
        QVERIFY2(clipped.isEmpty(), qPrintable(clipped));
    }
};

QTEST_MAIN(SideControlPanelLayoutTests)
#include "test_sidecontrolpanel.moc"
