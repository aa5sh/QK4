// Form-control theming (src/ui/styling/k4styles.cpp, applicationStyleSheet()).
//
// A control whose constructor forgets setStyleSheet falls back to the application stylesheet, not
// the operating system's theme. Windows 11 in dark mode drew forgotten controls dark-on-dark; the
// platform here is Fusion with a dark palette, which reproduces that failure, and every control is
// created bare so the fallback is the only thing styling it.

#include "ui/styling/k4styles.h"

#include <QApplication>
#include <algorithm>
#include <cmath>
#include <QCheckBox>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QStyleFactory>
#include <QStyleOption>
#include <QTest>
#include <QVBoxLayout>

namespace {

// WCAG 2.1 SC 1.4.11: user-interface components need 3:1 against what is next to them.
constexpr double kMinNonTextContrast = 3.0;
// WCAG 2.1 SC 1.4.3 (AA): text needs 4.5:1 against its background.
constexpr double kMinTextContrast = 4.5;

double luminance(const QColor &c) {
    auto channel = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
}

double contrast(const QColor &a, const QColor &b) {
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

const QColor kDialogBackground(K4Styles::Colors::DarkBackground);

// Hosts the control on the dialog background and renders it.
struct Rendered {
    QWidget host;
    QImage image;
    explicit Rendered(QWidget *control) {
        host.setObjectName("host");
        host.setStyleSheet(QString("QWidget#host { background-color: %1; }").arg(K4Styles::Colors::DarkBackground));
        host.setAttribute(Qt::WA_StyledBackground);
        auto *layout = new QVBoxLayout(&host);
        layout->addWidget(control);
        host.resize(240, 60);
        host.show();
        QCoreApplication::processEvents();
        image = host.grab().toImage();
    }
};

// Share of the rectangle's outline (in host coordinates) that stands out from the background.
double visibleOutlineShare(const QImage &image, const QRect &r) {
    int visible = 0;
    int total = 0;
    auto sample = [&](int x, int y) {
        ++total;
        if (contrast(image.pixelColor(x, y), kDialogBackground) >= kMinNonTextContrast)
            ++visible;
    };
    for (int x = r.left(); x <= r.right(); ++x) {
        sample(x, r.top());
        sample(x, r.bottom());
    }
    for (int y = r.top() + 1; y < r.bottom(); ++y) {
        sample(r.left(), y);
        sample(r.right(), y);
    }
    return total ? double(visible) / total : 0.0;
}

} // namespace

class TestFormControlTheme : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QApplication::setStyle(QStyleFactory::create("Fusion"));
        QPalette dark;
        dark.setColor(QPalette::Window, QColor("#202020"));
        dark.setColor(QPalette::Base, QColor("#101010"));
        dark.setColor(QPalette::Button, QColor("#202020"));
        dark.setColor(QPalette::WindowText, Qt::white);
        dark.setColor(QPalette::Text, Qt::white);
        dark.setColor(QPalette::ButtonText, Qt::white);
        QApplication::setPalette(dark);
        qApp->setStyleSheet(K4Styles::applicationStyleSheet());
    }

    void uncheckedCheckBoxIsVisible() {
        auto *box = new QCheckBox("Use TLS (Encrypted)");
        Rendered r(box);
        QStyleOptionButton opt;
        opt.initFrom(box);
        const QRect indicator = box->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &opt, box);
        QVERIFY(indicator.isValid());
        const QRect inHost(box->mapTo(&r.host, indicator.topLeft()), indicator.size());
        QVERIFY2(visibleOutlineShare(r.image, inHost) >= 0.5,
                 "unchecked checkbox outline does not stand out from the dialog background");
    }

    // Text colours on the dark surfaces they are drawn on. TextFaded sits on Background and on the
    // menu/macro overlay rows; TextRed carries the over-S9 meter scale on Background.
    void textColorsMeetAaContrast_data() {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("background");
        const QList<QPair<const char *, const char *>> texts = {
            {"TextWhite", K4Styles::Colors::TextWhite},
            {"TextGray", K4Styles::Colors::TextGray},
            {"TextFaded", K4Styles::Colors::TextFaded},
            {"TextRed", K4Styles::Colors::TextRed},
        };
        const QList<QPair<const char *, const char *>> backgrounds = {
            {"Background", K4Styles::Colors::Background},
            {"OverlayItemBg", K4Styles::Colors::OverlayItemBg},
            {"OverlayContentBg", K4Styles::Colors::OverlayContentBg},
        };
        for (const auto &[textName, text] : texts) {
            for (const auto &[bgName, bg] : backgrounds)
                QTest::newRow(qPrintable(QString("%1 on %2").arg(textName, bgName))) << QString(text) << QString(bg);
        }
    }

    void textColorsMeetAaContrast() {
        QFETCH(QString, text);
        QFETCH(QString, background);
        const double ratio = contrast(QColor(text), QColor(background));
        QVERIFY2(ratio >= kMinTextContrast,
                 qPrintable(QString("%1 on %2 is %3:1, under 4.5:1").arg(text, background).arg(ratio, 0, 'f', 2)));
    }

    // TextRed exists because TxRed is too dark for small text; if the two ever converge, the
    // over-S9 marks are back to the contrast they were fixed for.
    void txRedIsNotATextColor() {
        QVERIFY(contrast(QColor(K4Styles::Colors::TxRed), QColor(K4Styles::Colors::Background)) < kMinTextContrast);
        QVERIFY(QColor(K4Styles::Colors::TextRed) != QColor(K4Styles::Colors::TxRed));
    }

    // The FontSize* constants are held to the floor by static_asserts in k4constants.h. This catches
    // the ways around them: a literal pixel size, a literal stylesheet font-size, or setPointSize.
    void noLiteralFontSizeBelowTheFloor() {
        const QRegularExpression pixelLiteral(QStringLiteral("setPixelSize\\(\\s*(\\d+)\\s*\\)"));
        const QRegularExpression cssLiteral(QStringLiteral("font-size:\\s*(\\d+)px"));
        const QRegularExpression pointSize(QStringLiteral("\\bsetPointSizeF?\\("));
        QStringList offenders;
        int scanned = 0;
        QDirIterator it(QStringLiteral(QK4_SOURCE_DIR), {"*.cpp", "*.h"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            QFile f(path);
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QString text = QString::fromUtf8(f.readAll());
            ++scanned;
            for (const QRegularExpression *re : {&pixelLiteral, &cssLiteral}) {
                auto matches = re->globalMatch(text);
                while (matches.hasNext()) {
                    const auto m = matches.next();
                    if (m.captured(1).toInt() < K4Styles::Dimensions::FontSizeFloor)
                        offenders << QString("%1: %2").arg(path, m.captured(0));
                }
            }
            if (text.contains(pointSize) && !path.endsWith("k4styles.h"))
                offenders << QString("%1: setPointSize").arg(path);
        }
        QVERIFY2(scanned > 50, "source scan found too few files - is QK4_SOURCE_DIR right?");
        QVERIFY2(offenders.isEmpty(), qPrintable("text below the 10 px floor or in points:\n" + offenders.join('\n')));
    }

    // Every OS-drawn form control QK4 creates must have a fallback rule. Adding, say, a QRadioButton
    // fails here until applicationStyleSheet() covers it.
    void everyFormControlTypeInSourceHasAFallbackRule() {
        // Type -> the selector that styles it (stylesheet type selectors also match subclasses).
        const QList<QPair<QString, QString>> types = {
            {"QCheckBox", "QCheckBox"},           {"QRadioButton", "QRadioButton"},   {"QComboBox", "QComboBox"},
            {"QFontComboBox", "QComboBox"},       {"QLineEdit", "QLineEdit"},         {"QSpinBox", "QSpinBox"},
            {"QDoubleSpinBox", "QDoubleSpinBox"}, {"QDateTimeEdit", "QDateTimeEdit"}, {"QDateEdit", "QDateTimeEdit"},
            {"QTimeEdit", "QDateTimeEdit"},
        };
        const QString sheet = K4Styles::applicationStyleSheet();

        QString source;
        QDirIterator it(QStringLiteral(QK4_SOURCE_DIR), {"*.cpp", "*.h"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QFile f(it.next());
            QVERIFY(f.open(QIODevice::ReadOnly));
            source += QString::fromUtf8(f.readAll());
        }
        QVERIFY2(source.contains("new QCheckBox("), "source scan found nothing - is QK4_SOURCE_DIR right?");

        for (const auto &[type, selector] : types) {
            if (!source.contains(QRegularExpression(QString("\\bnew %1\\(").arg(type))))
                continue;
            QVERIFY2(sheet.contains(QRegularExpression(QString("(^|[},\\s])%1\\s*[{:]").arg(selector))),
                     qPrintable(QString("%1 is used in src/ but K4Styles::applicationStyleSheet() has no %2 rule")
                                    .arg(type, selector)));
        }
    }
};

int main(int argc, char *argv[]) {
    // Headless in CI, and independent of whatever desktop a developer runs it on.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    TestFormControlTheme t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_formcontroltheme.moc"
