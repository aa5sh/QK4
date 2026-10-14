#ifndef DISPLAYPAGE_H
#define DISPLAYPAGE_H

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QWidget>

/**
 * @brief Options page for how the interface is drawn. Today that is the interface scale: Auto
 *        (fit the screen) or a fixed percentage. Qt applies a scale only at startup, so the page
 *        offers a restart; the setting persists via RadioSettings and is read by InterfaceScale
 *        before the next QApplication exists.
 */
class DisplayPage : public QWidget {
    Q_OBJECT

public:
    explicit DisplayPage(QWidget *parent = nullptr);
    ~DisplayPage() = default;

private slots:
    void onScaleChanged(int index);
    void onRestartClicked();

private:
    void updateStatus();

    QComboBox *m_scaleCombo = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_restartButton = nullptr;
};

#endif // DISPLAYPAGE_H
