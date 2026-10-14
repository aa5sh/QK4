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
