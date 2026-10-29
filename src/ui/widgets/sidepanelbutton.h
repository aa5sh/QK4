#ifndef SIDEPANELBUTTON_H
#define SIDEPANELBUTTON_H

#include "ui/styling/k4constants.h"

#include <QSize>
#include <QString>

class QGridLayout;
class QLabel;
class QPushButton;
class QWidget;

// The one builder for side-panel buttons: left and right panels, the memory row, the KPA1500
// mini-panel. Geometry comes from the K4Styles::Dimensions::Side* tokens; the shade picks colour only.
namespace SidePanelButton {

enum class Shade { Dark, Light };

inline constexpr QSize kFunctionSize{K4Styles::Dimensions::SideButtonWidth, K4Styles::Dimensions::SideButtonHeight};

// A fixed-size button with the shade's sheet and Qt::WA_LayoutUsesWidgetRect set.
QPushButton *create(const QString &text, Shade shade, QWidget *parent, QSize size = kFunctionSize);

// The button with its orange sub-label below, in a container that holds nothing else. An empty
// sub-label still reserves its line so rows stay aligned.
QWidget *createWithSubLabel(const QString &text, const QString &subText, Shade shade, QPushButton *&btnOut,
                            QWidget *parent, QSize size = kFunctionSize);

// One line of sub-label text tall, measured from the font it is drawn with.
QLabel *createSubLabel(const QString &text, QWidget *parent);
int subLabelHeight(const QWidget *context);

// Two equal columns of function buttons with the standard column and row gaps, no margins. Add it to
// its parent layout aligned Qt::AlignHCenter so it keeps its own width in a wider panel.
QGridLayout *createGrid();

} // namespace SidePanelButton

#endif // SIDEPANELBUTTON_H
