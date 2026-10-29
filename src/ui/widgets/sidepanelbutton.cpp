#include "ui/widgets/sidepanelbutton.h"
#include "ui/styling/k4styles.h"

#include <QFontMetricsF>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtMath>

namespace SidePanelButton {

namespace D = K4Styles::Dimensions;

QPushButton *create(const QString &text, Shade shade, QWidget *parent, QSize size) {
    auto *btn = new QPushButton(text, parent);
    // WHY fixed: these keys never resize with the window, and a fixed size is what makes light and
    // dark, left and right, identical. Size lives here, not in the sheet: a style sheet's height has
    // no effect on widgets and min-height counts from the content rect.
    btn->setFixedSize(size);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(shade == Shade::Light ? K4Styles::sidePanelButtonLight() : K4Styles::sidePanelButton());
    btn->setAttribute(Qt::WA_LayoutUsesWidgetRect);
    return btn;
}

int subLabelHeight(const QWidget *context) {
    QFont font = context->font();
    font.setPixelSize(D::FontSizeSmall);
    return qCeil(QFontMetricsF(font, context).height());
}

QLabel *createSubLabel(const QString &text, QWidget *parent) {
    auto *label = new QLabel(text, parent);
    label->setStyleSheet(
        QString("color: %1; font-size: %2px;").arg(K4Styles::Colors::AccentAmber).arg(D::FontSizeSmall));
    label->setAlignment(Qt::AlignCenter);
    label->setFixedHeight(subLabelHeight(label));
    return label;
}

QWidget *createWithSubLabel(const QString &text, const QString &subText, Shade shade, QPushButton *&btnOut,
                            QWidget *parent, QSize size) {
    auto *cell = new QWidget(parent);
    auto *layout = new QVBoxLayout(cell);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(D::SideButtonSubLabelGap);
    btnOut = create(text, shade, cell, size);
    layout->addWidget(btnOut, 0, Qt::AlignHCenter);
    layout->addWidget(createSubLabel(subText, cell));
    return cell;
}

QGridLayout *createGrid() {
    auto *grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    // WHY explicit spacing and not the style's PM_Layout*Spacing: these are custom-drawn keys, and the
    // native metrics follow platform guidelines (Aqua spaces push buttons 12 px), so the panels would
    // differ between macOS, Windows and Linux.
    grid->setHorizontalSpacing(D::SideButtonColumnGap);
    grid->setVerticalSpacing(D::SideButtonRowGap);
    // WHY: QGridLayout makes columns equal only when their minimum widths and stretch factors match.
    // The stretch stays 0: a stretching grid counts as expanding and would ignore its centring.
    for (int column = 0; column < 2; ++column)
        grid->setColumnMinimumWidth(column, D::SideButtonWidth);
    return grid;
}

} // namespace SidePanelButton
