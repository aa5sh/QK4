#include "keyingweightpopup.h"
#include "k4styles.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>
#include <QWheelEvent>

namespace {
const int ContentHeight = 52;
const int ContentMargin = 12;
const int MinWeight = 90;
const int MaxWeight = 125;
const int WeightStep = 5;
const int TitleWidth = 150;
} // namespace

KeyingWeightPopupWidget::KeyingWeightPopupWidget(QWidget *parent) : QWidget(parent) {
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    setupUi();
    hide();
}

void KeyingWeightPopupWidget::setupUi() {
    setFixedHeight(ContentHeight + 2 * K4Styles::Dimensions::ShadowMargin);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(
        K4Styles::Dimensions::ShadowMargin + ContentMargin, K4Styles::Dimensions::ShadowMargin + 6,
        K4Styles::Dimensions::ShadowMargin + ContentMargin, K4Styles::Dimensions::ShadowMargin + 6);
    layout->setSpacing(6);

    // Title label
    m_titleLabel = new QLabel("KEYING WEIGHT", this);
    m_titleLabel->setFixedSize(TitleWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setStyleSheet(QString("QLabel {"
                                        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
                                        "    stop:0 %1, stop:0.4 %2, stop:0.6 %3, stop:1 %4);"
                                        "  color: %5;"
                                        "  border: %6px solid %7;"
                                        "  border-radius: %8px;"
                                        "  font-size: %9px;"
                                        "  font-weight: 600;"
                                        "}")
                                    .arg(K4Styles::Colors::GradientTop)
                                    .arg(K4Styles::Colors::GradientMid1)
                                    .arg(K4Styles::Colors::GradientMid2)
                                    .arg(K4Styles::Colors::GradientBottom)
                                    .arg(K4Styles::Colors::TextWhite)
                                    .arg(K4Styles::Dimensions::BorderWidth)
                                    .arg(K4Styles::Colors::BorderNormal)
                                    .arg(K4Styles::Dimensions::BorderRadius)
                                    .arg(K4Styles::Dimensions::PopupTitleSize));

    // Value display label - shows weight as ratio (e.g., "1.00")
    m_valueLabel = new QLabel("1.00", this);
    m_valueLabel->setFixedSize(80, K4Styles::Dimensions::ButtonHeightMedium);
    m_valueLabel->setAlignment(Qt::AlignCenter);
    m_valueLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: 600;")
                                    .arg(K4Styles::Colors::TextWhite)
                                    .arg(K4Styles::Dimensions::PopupValueSize));

    // Decrement button
    m_decrementBtn = new QPushButton("-", this);
    m_decrementBtn->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_decrementBtn->setCursor(Qt::PointingHandCursor);
    m_decrementBtn->setStyleSheet(K4Styles::menuBarButtonSmall());

    // Increment button
    m_incrementBtn = new QPushButton("+", this);
    m_incrementBtn->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_incrementBtn->setCursor(Qt::PointingHandCursor);
    m_incrementBtn->setStyleSheet(K4Styles::menuBarButtonSmall());

    // Close button
    m_closeBtn = new QPushButton("\u21A9", this); // U+21A9 leftwards arrow with hook
    m_closeBtn->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setStyleSheet(K4Styles::menuBarButton());

    // Add to layout
    layout->addWidget(m_titleLabel);
    layout->addWidget(m_valueLabel);
    layout->addWidget(m_decrementBtn);
    layout->addWidget(m_incrementBtn);
    layout->addWidget(m_closeBtn);

    // Connect signals
    connect(m_decrementBtn, &QPushButton::clicked, this, [this]() { adjustValue(-WeightStep); });
    connect(m_incrementBtn, &QPushButton::clicked, this, [this]() { adjustValue(WeightStep); });
    connect(m_closeBtn, &QPushButton::clicked, this, &KeyingWeightPopupWidget::hidePopup);

    updateValueDisplay();
}

void KeyingWeightPopupWidget::updateValueDisplay() {
    double ratio = m_weight / 100.0;
    m_valueLabel->setText(QString::number(ratio, 'f', 2));
}

void KeyingWeightPopupWidget::adjustValue(int delta) {
    int newWeight = qBound(MinWeight, m_weight + delta, MaxWeight);
    if (newWeight != m_weight) {
        m_weight = newWeight;
        updateValueDisplay();
        emit weightChanged(newWeight);
    }
}

void KeyingWeightPopupWidget::setWeight(int weight) {
    m_weight = qBound(MinWeight, weight, MaxWeight);
    updateValueDisplay();
}

void KeyingWeightPopupWidget::showAboveWidget(QWidget *referenceWidget) {
    if (!referenceWidget)
        return;

    m_referenceWidget = referenceWidget;

    layout()->activate();
    adjustSize();

    QPoint refGlobal = referenceWidget->mapToGlobal(QPoint(0, 0));
    int refCenterX = refGlobal.x() + referenceWidget->width() / 2;

    int contentWidth = width() - 2 * K4Styles::Dimensions::ShadowMargin;
    int popupX = refCenterX - contentWidth / 2 - K4Styles::Dimensions::ShadowMargin;
    int popupY = refGlobal.y() - height() - 4;

    QRect screenGeom = referenceWidget->screen()->availableGeometry();
    if (popupX < screenGeom.left() - K4Styles::Dimensions::ShadowMargin) {
        popupX = screenGeom.left() - K4Styles::Dimensions::ShadowMargin;
    } else if (popupX + width() > screenGeom.right() + K4Styles::Dimensions::ShadowMargin) {
        popupX = screenGeom.right() + K4Styles::Dimensions::ShadowMargin - width();
    }
    if (popupY < screenGeom.top() - K4Styles::Dimensions::ShadowMargin) {
        popupY = refGlobal.y() + referenceWidget->height() + 4 - K4Styles::Dimensions::ShadowMargin;
    }

    move(popupX, popupY);
    show();
    setFocus();
    update();
}

void KeyingWeightPopupWidget::hidePopup() {
    hide();
}

void KeyingWeightPopupWidget::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        hidePopup();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void KeyingWeightPopupWidget::wheelEvent(QWheelEvent *event) {
    int steps = m_wheelAccumulator.accumulate(event);
    if (steps != 0)
        adjustValue(steps * WeightStep);
    event->accept();
}

void KeyingWeightPopupWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Calculate tight bounding box
    int left = m_titleLabel->geometry().left() - 8;
    int right = m_closeBtn->geometry().right() + 8;
    QRect contentRect(left, K4Styles::Dimensions::ShadowMargin + 1, right - left, ContentHeight - 3);

    // Draw drop shadow
    K4Styles::drawDropShadow(painter, contentRect, 8);

    // Gradient background
    QLinearGradient grad = K4Styles::buttonGradient(contentRect.top(), contentRect.bottom());

    painter.setBrush(grad);
    painter.setPen(QPen(K4Styles::borderColor(), 1));
    painter.drawRoundedRect(contentRect, 8, 8);

    // Draw vertical delimiter lines
    painter.setPen(QPen(K4Styles::borderColor(), 1));
    int lineTop = contentRect.top() + 7;
    int lineBottom = contentRect.bottom() - 7;

    auto drawDelimiter = [&](QWidget *widget) {
        if (widget && widget->isVisible()) {
            int x = widget->geometry().right() + 3;
            painter.drawLine(x, lineTop, x, lineBottom);
        }
    };

    drawDelimiter(m_titleLabel);
    drawDelimiter(m_incrementBtn);
}
