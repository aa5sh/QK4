#include "models/menumodel.h"
#include "ui/overlays/menuoverlay.h"

#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QtTest>

#include <algorithm>

// The menu overlay against the disconnect → reconnect sequence that crashed on Linux (#124): the K4
// pushes its MEDF menu at connect, MainWindow clears the model on disconnect, and the radio pushes
// the menu again on the next connect.
class TestMenuOverlay : public QObject {
    Q_OBJECT

private slots:
    void reconnectAfterTheMenuWasOpenedReadsNoFreedItems();
    void anOpenMenuFollowsTheMenuResentAfterReconnect();
    void aClearDuringEditLeavesEditMode();
    void rowsFollowValueUpdatesAndTheXvtrSelector();
    void onlyReadOnlyRowsShowTheLock();

private:
    static void sendMenu(MenuModel &model);
    static QStringList rowTexts(const MenuOverlayWidget &overlay);
    static bool hasButtonLabelled(const MenuOverlayWidget &overlay, const QString &text);
};

void TestMenuOverlay::sendMenu(MenuModel &model) {
    QVERIFY(model.parseMEDF("MEDF0007,AGC Hold Time,RX AGC,DEC,1,0,200,0,0,1;"));
    QVERIFY(model.parseMEDF("MEDF0086,XVTR Band # Select,XV,XVTR,1,1,12,1,1,1;"));
    QVERIFY(model.parseMEDF("MEDF0078,XVTR Band <n> I.F.,XV,XVTR,1,0,1,0,0,1;"));
}

// "name=value" per row, in display order.
QStringList TestMenuOverlay::rowTexts(const MenuOverlayWidget &overlay) {
    QStringList rows;
    const auto widgets = overlay.findChildren<MenuItemWidget *>();
    for (const MenuItemWidget *row : widgets) {
        const auto labels = row->findChildren<QLabel *>(Qt::FindDirectChildrenOnly);
        if (labels.size() == 3) {
            rows << labels.first()->text() + QLatin1Char('=') + labels.last()->text();
        }
    }
    return rows;
}

bool TestMenuOverlay::hasButtonLabelled(const MenuOverlayWidget &overlay, const QString &text) {
    const auto buttons = overlay.findChildren<QPushButton *>();
    return std::any_of(buttons.cbegin(), buttons.cend(), [&](const QPushButton *b) { return b->text() == text; });
}

// Regression for #124: open the menu, disconnect, reconnect, and the radio's next menu value
// change walked rows built from items the disconnect had destroyed. XVTR Band # Select is the
// change in the reported stack: it makes the overlay read every row's name.
void TestMenuOverlay::reconnectAfterTheMenuWasOpenedReadsNoFreedItems() {
    MenuModel model;
    MenuOverlayWidget overlay(&model);
    sendMenu(model);

    overlay.show();
    QCOMPARE(rowTexts(overlay).size(), 3);
    overlay.hide();

    model.clear();
    sendMenu(model);
    QVERIFY(model.parseME("ME0086.0005;"));
    QVERIFY(model.parseME("ME0007.0123;"));

    overlay.show();
    QCOMPARE(rowTexts(overlay), (QStringList{"AGC Hold Time=123", "XVTR Band # Select=5", "XVTR Band 5 I.F.=0"}));
}

// Left open across the disconnect, the menu empties with the model and fills again when the radio
// resends it, without the operator closing and reopening it.
void TestMenuOverlay::anOpenMenuFollowsTheMenuResentAfterReconnect() {
    MenuModel model;
    MenuOverlayWidget overlay(&model);
    sendMenu(model);
    overlay.show();
    QCOMPARE(rowTexts(overlay).size(), 3);

    model.clear();
    QVERIFY(rowTexts(overlay).isEmpty());

    sendMenu(model);
    QVERIFY(model.parseME("ME0007.0042;"));
    QTRY_COMPARE(rowTexts(overlay), (QStringList{"AGC Hold Time=42", "XVTR Band # Select=1", "XVTR Band 1 I.F.=0"}));
    QVERIFY(overlay.isVisible());
}

void TestMenuOverlay::aClearDuringEditLeavesEditMode() {
    MenuModel model;
    MenuOverlayWidget overlay(&model);
    sendMenu(model);
    overlay.show();

    QTest::keyClick(&overlay, Qt::Key_Return);
    QVERIFY(hasButtonLabelled(overlay, QStringLiteral("+")));

    model.clear();
    QVERIFY(!hasButtonLabelled(overlay, QStringLiteral("+")));
    QVERIFY(hasButtonLabelled(overlay, QString::fromUtf8("\xE2\x96\xB2")));
}

void TestMenuOverlay::rowsFollowValueUpdatesAndTheXvtrSelector() {
    MenuModel model;
    MenuOverlayWidget overlay(&model);
    sendMenu(model);
    overlay.show();

    QVERIFY(model.parseME("ME0007.0150;"));
    QVERIFY(model.parseME("ME0086.0009;"));
    QCOMPARE(rowTexts(overlay), (QStringList{"AGC Hold Time=150", "XVTR Band # Select=9", "XVTR Band 9 I.F.=0"}));
}

void TestMenuOverlay::onlyReadOnlyRowsShowTheLock() {
    MenuModel model;
    MenuOverlayWidget overlay(&model);
    QVERIFY(model.parseMEDF("MEDF0007,AGC Hold Time,RX AGC,DEC,1,0,200,0,0,1;"));
    QVERIFY(model.parseMEDF("MEDF0001,Serial Number,INFO,DEC,2,0,99999,0,12345,1;"));
    overlay.show();

    QStringList locks;
    const auto rows = overlay.findChildren<MenuItemWidget *>();
    for (const MenuItemWidget *row : rows) {
        locks << row->findChildren<QLabel *>(Qt::FindDirectChildrenOnly).at(1)->text();
    }
    QCOMPARE(locks, (QStringList{QString(), QString::fromUtf8("\xF0\x9F\x94\x92")}));
}

QTEST_MAIN(TestMenuOverlay)
#include "test_menuoverlay.moc"
