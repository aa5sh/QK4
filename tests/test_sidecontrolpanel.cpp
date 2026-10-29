// Left side panel layout (src/ui/widgets/sidecontrolpanel.cpp), rendered offscreen at the smallest
// size its layout allows. That is the size the main window gives it when the window is at its own
// minimum, so nothing in the panel may overlap there.
//
// WHY: testers on Windows and Linux saw the TX buttons' orange sub-labels painted over the buttons.
// The window's hand-written minimum was smaller than this panel needed, and the layout squeezed each
// button-and-label pair from 49 px to 42. This holds the panel to its own minimum and checks the
// pairs, and every other control, stay apart.

#include "ui/widgets/rightsidepanel.h"
#include "ui/styling/k4styles.h"
#include "ui/widgets/kpa1500minipanel.h"
#include "ui/widgets/sidecontrolpanel.h"
#include "ui/widgets/sidepanelbutton.h"

#include <QApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QFontMetricsF>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <climits>
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
        // The app runs with this sheet installed (main.cpp); it changes how every widget is laid out.
        qApp->setStyleSheet(K4Styles::applicationStyleSheet());
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

    // Buttons in `panel` that break the side-panel button rules, or an empty string.
    static QString firstBadButton(QWidget &panel) {
        QWidget host;
        panel.setParent(&host);
        panel.setGeometry(0, 0, panel.minimumSizeHint().width(), panel.minimumSizeHint().height());
        host.resize(panel.size());
        host.show();
        QCoreApplication::processEvents();
        for (QPushButton *button : panel.findChildren<QPushButton *>()) {
            if (!button->isVisible())
                continue;
            const QString name = QString(button->text()).replace('\n', ' ');
            // WHY: on macOS the native style's smaller layout rect let the sub-label be laid over
            // the button's bottom edge; the offscreen style used here cannot show that, so the rule
            // that prevents it is asserted directly.
            if (!button->testAttribute(Qt::WA_LayoutUsesWidgetRect))
                return QString("'%1' lacks Qt::WA_LayoutUsesWidgetRect").arg(name);
            const QSize need = button->sizeHint();
            if (!button->text().isEmpty() && need.height() > button->height())
                return QString("'%1' needs %2 px of height, has %3").arg(name).arg(need.height()).arg(button->height());
            if (!button->text().isEmpty() && need.width() > button->width())
                return QString("'%1' needs %2 px of width, has %3").arg(name).arg(need.width()).arg(button->width());
        }
        return {};
    }

    void leftPanelButtonsKeepTheirFullHeight() {
        auto *panel = new SideControlPanel;
        const QString bad = firstBadButton(*panel);
        QVERIFY2(bad.isEmpty(), qPrintable(bad));
    }

    void rightPanelButtonsKeepTheirFullHeight() {
        auto *panel = new RightSidePanel;
        const QString bad = firstBadButton(*panel);
        QVERIFY2(bad.isEmpty(), qPrintable(bad));
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

// The side-panel button standard (K4Styles::Dimensions::Side*, built by SidePanelButton): both panels and
// the KPA1500 mini-panel, laid out together at their minimum height as MainWindow holds them.
class SidePanelButtonStandardTests : public QObject {
    Q_OBJECT

    QTemporaryDir m_tempDir;
    QWidget m_host;
    SideControlPanel *m_left = nullptr;
    RightSidePanel *m_right = nullptr;
    Kpa1500MiniPanel *m_kpa = nullptr;

    // Function buttons: a button sharing its container with an orange sub-label.
    static QList<QPushButton *> functionButtons(QWidget *panel) {
        QList<QPushButton *> found;
        for (QPushButton *button : panel->findChildren<QPushButton *>()) {
            QWidget *cell = button->parentWidget();
            if (cell != panel && cell->findChildren<QLabel *>(Qt::FindDirectChildrenOnly).size() == 1)
                found << button;
        }
        return found;
    }

    static QRect inPanel(const QWidget *panel, const QWidget *w) { return QRect(w->mapTo(panel, QPoint()), w->size()); }

    static QLabel *subLabelOf(const QPushButton *button) {
        return button->parentWidget()->findChildren<QLabel *>(Qt::FindDirectChildrenOnly).first();
    }

private slots:
    void initTestCase() {
        QVERIFY(m_tempDir.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tempDir.path());
        qApp->setStyleSheet(K4Styles::applicationStyleSheet());

        auto *row = new QHBoxLayout(&m_host);
        row->setContentsMargins(0, 0, 0, 0);
        m_left = new SideControlPanel;
        m_right = new RightSidePanel;
        m_kpa = new Kpa1500MiniPanel;
        m_kpa->setConnected(true);
        m_right->embedKpa1500Panel(m_kpa);
        row->addWidget(m_left);
        row->addWidget(m_right);
        m_host.resize(m_host.minimumSizeHint());
        m_host.show();
        QCoreApplication::processEvents();
    }

    void everyFunctionButtonHasTheOneSize() {
        // Light (TX, PF) and dark keys, both panels and the KPA1500 card: one size.
        QList<QPushButton *> buttons = functionButtons(m_left) + functionButtons(m_right);
        QCOMPARE(buttons.size(), 6 + 10 + 4 + 4);
        buttons += m_kpa->findChildren<QPushButton *>();
        for (QPushButton *button : buttons)
            QVERIFY2(button->size() == SidePanelButton::kFunctionSize, qPrintable(button->text()));
    }

    void lightAndDarkDifferOnlyInColour() {
        QWidget parent;
        for (const QString &text : {QStringLiteral("TUNE"), QStringLiteral("ATU\nTUNE"), QStringLiteral("DIVERSITY")}) {
            QPushButton *light = SidePanelButton::create(text, SidePanelButton::Shade::Light, &parent);
            QPushButton *dark = SidePanelButton::create(text, SidePanelButton::Shade::Dark, &parent);
            QCOMPARE(light->size(), dark->size());
            QCOMPARE(light->sizeHint(), dark->sizeHint());
            QCOMPARE(light->minimumSizeHint(), dark->minimumSizeHint());
            QVERIFY(light->styleSheet() != dark->styleSheet());
        }
    }

    void subLabelsSitTheSameGapBelowEveryButton() {
        for (QWidget *panel : {static_cast<QWidget *>(m_left), static_cast<QWidget *>(m_right)}) {
            for (QPushButton *button : functionButtons(panel)) {
                const QRect b = inPanel(panel, button);
                const QRect l = inPanel(panel, subLabelOf(button));
                QCOMPARE(l.top() - b.bottom() - 1, K4Styles::Dimensions::SideButtonSubLabelGap);
                QCOMPARE(l.height(), SidePanelButton::subLabelHeight(subLabelOf(button)));
            }
        }
    }

    void columnsAndRowsKeepOneRhythm() {
        const int pitch = K4Styles::Dimensions::SideButtonHeight + K4Styles::Dimensions::SideButtonSubLabelGap +
                          SidePanelButton::subLabelHeight(m_left) + K4Styles::Dimensions::SideButtonRowGap;
        int grids = 0;
        for (QWidget *panel : {static_cast<QWidget *>(m_left), static_cast<QWidget *>(m_right)}) {
            for (QGridLayout *grid : panel->findChildren<QGridLayout *>()) {
                if (grid->parentWidget() != panel)
                    continue;
                ++grids;
                auto buttonAt = [&](int row, int column) {
                    return inPanel(panel, grid->itemAtPosition(row, column)->widget()->findChild<QPushButton *>());
                };
                for (int row = 0; row < grid->rowCount(); ++row) {
                    QCOMPARE(buttonAt(row, 1).left() - buttonAt(row, 0).right() - 1,
                             K4Styles::Dimensions::SideButtonColumnGap);
                    if (row > 0)
                        QCOMPARE(buttonAt(row, 0).top() - buttonAt(row - 1, 0).top(), pitch);
                }
            }
        }
        QCOMPARE(grids, 4); // TX grid; main, PF and bottom grids
    }

    void panelsLineUp() {
        // Left: the grid fills the panel inside its margins. Right: the same grid, centred, so both
        // insets are equal, and the KPA1500 keys stand in the same two columns.
        const QRect leftFirst = inPanel(m_left, functionButtons(m_left).first());
        QCOMPARE(leftFirst.left(), K4Styles::Dimensions::SidePanelMarginH);
        QCOMPARE(leftFirst.top(), K4Styles::Dimensions::SidePanelMarginV);

        int minX = INT_MAX;
        int maxX = 0;
        for (QPushButton *button : functionButtons(m_right)) {
            minX = qMin(minX, inPanel(m_right, button).left());
            maxX = qMax(maxX, inPanel(m_right, button).right() + 1);
        }
        QCOMPARE(maxX - minX, K4Styles::Dimensions::SideButtonGridWidth);
        QCOMPARE(minX, m_right->width() - maxX);
        QCOMPARE(minX, K4Styles::Dimensions::SidePanelMarginH + K4Styles::Dimensions::SidePanelMeterAllowance);
        QCOMPARE(inPanel(m_right, functionButtons(m_right).first()).top(), leftFirst.top());
        for (QPushButton *button : m_kpa->findChildren<QPushButton *>()) {
            const int x = inPanel(m_right, button).left();
            QVERIFY2(x == minX ||
                         x == minX + K4Styles::Dimensions::SideButtonWidth + K4Styles::Dimensions::SideButtonColumnGap,
                     qPrintable(button->text()));
        }
    }

    void kpaButtonsSitDirectlyOnTheCard() {
        // WHY: the app's background rule paints every plain QWidget; a container behind each key drew
        // an opaque square over the card's lighter tint.
        for (QPushButton *button : m_kpa->findChildren<QPushButton *>())
            QVERIFY2(button->parentWidget() == m_kpa, qPrintable(button->text()));
    }

    void statusGroupSitsOnTheBottomEdge() {
        // Taller than its minimum, the spare height goes above the clock, not below the version label.
        QWidget host;
        auto *panel = new SideControlPanel(&host);
        const int extra = 260;
        panel->setGeometry(0, 0, panel->width(), panel->minimumSizeHint().height() + extra);
        host.resize(panel->size());
        host.show();
        QCoreApplication::processEvents();
        QLabel *version = nullptr;
        for (QLabel *label : panel->findChildren<QLabel *>(Qt::FindDirectChildrenOnly))
            if (label->text().startsWith(QLatin1String("QK4 V.")))
                version = label;
        QVERIFY(version);
        QCOMPARE(version->geometry().bottom() + 1, panel->height() - K4Styles::Dimensions::SidePanelMarginV);
        QCOMPARE(functionButtons(panel).first()->mapTo(panel, QPoint()).y(), K4Styles::Dimensions::SidePanelMarginV);
    }
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    int status = 0;
    {
        SideControlPanelLayoutTests layout;
        status |= QTest::qExec(&layout, argc, argv);
    }
    {
        SidePanelButtonStandardTests standard;
        status |= QTest::qExec(&standard, argc, argv);
    }
    return status;
}
#include "test_sidecontrolpanel.moc"
