#include "ui/pages/cwkeyerpage.h"
#include "ui/styling/k4styles.h"
#include "controllers/cwcontroller.h"
#include "hardware/halikeydevice.h"
#include "models/radiostate.h"
#include "settings/radiosettings.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QFrame>
#include <QSignalBlocker>

CwKeyerPage::CwKeyerPage(HalikeyDevice *halikeyDevice, RadioState *radioState, CwController *cwController,
                         QWidget *parent)
    : QWidget(parent), m_halikeyDevice(halikeyDevice), m_radioState(radioState), m_cwController(cwController) {
    setStyleSheet(K4Styles::Dialog::pageBackground());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    // Title
    auto *titleLabel = new QLabel("HaliKey", this);
    titleLabel->setStyleSheet(K4Styles::Dialog::titleLabel());
    layout->addWidget(titleLabel);

    // Description (dynamic based on device type)
    m_cwKeyerDescLabel = new QLabel(this);
    m_cwKeyerDescLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                          .arg(K4Styles::Colors::TextGray)
                                          .arg(K4Styles::Dimensions::FontSizeButton));
    m_cwKeyerDescLabel->setWordWrap(true);
    layout->addWidget(m_cwKeyerDescLabel);

    // Device Type selector
    auto *deviceTypeLayout = new QHBoxLayout();
    auto *deviceTypeLabel = new QLabel("Device Type:", this);
    deviceTypeLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    deviceTypeLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_cwKeyerDeviceTypeCombo = new QComboBox(this);
    m_cwKeyerDeviceTypeCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    m_cwKeyerDeviceTypeCombo->addItem("HaliKey V1.4", 0);
    m_cwKeyerDeviceTypeCombo->addItem("HaliKey MIDI", 1);

    int savedDeviceType = RadioSettings::instance()->halikeyDeviceType();
    m_cwKeyerDeviceTypeCombo->setCurrentIndex(savedDeviceType);
    updateCwKeyerDescription();

    connect(m_cwKeyerDeviceTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        int type = m_cwKeyerDeviceTypeCombo->itemData(index).toInt();
        RadioSettings::instance()->setHalikeyDeviceType(type);
        // Close even a port still opening: it was opened as the old device type.
        if (m_halikeyDevice) {
            m_halikeyDevice->closePort();
        }
        updateCwKeyerDescription();
        populateCwKeyerPorts();
    });

    deviceTypeLayout->addWidget(deviceTypeLabel);
    deviceTypeLayout->addWidget(m_cwKeyerDeviceTypeCombo, 1);
    layout->addLayout(deviceTypeLayout);

    // Separator line
    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(K4Styles::Dialog::separator());
    line->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line);

    // Status indicator
    auto *statusLayout = new QHBoxLayout();
    auto *statusTitleLabel = new QLabel("Status:", this);
    statusTitleLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    statusTitleLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_cwKeyerStatusLabel = new QLabel("Not Connected", this);
    m_cwKeyerStatusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::ErrorRed));

    statusLayout->addWidget(statusTitleLabel);
    statusLayout->addWidget(m_cwKeyerStatusLabel);
    statusLayout->addStretch();
    layout->addLayout(statusLayout);

    // Separator line
    auto *line2 = new QFrame(this);
    line2->setFrameShape(QFrame::HLine);
    line2->setStyleSheet(K4Styles::Dialog::separator());
    line2->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line2);

    // Connection Settings section
    auto *sectionLabel = new QLabel("Connection Settings", this);
    sectionLabel->setStyleSheet(K4Styles::Dialog::sectionHeader());
    layout->addWidget(sectionLabel);

    // Port selection
    auto *portLayout = new QHBoxLayout();
    auto *portLabel = new QLabel("Port:", this);
    portLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    portLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_cwKeyerPortCombo = new QComboBox(this);
    m_cwKeyerPortCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    populateCwKeyerPorts();

    m_cwKeyerRefreshBtn = new QPushButton("Refresh", this);
    m_cwKeyerRefreshBtn->setStyleSheet(K4Styles::Dialog::actionButtonSmall());
    connect(m_cwKeyerRefreshBtn, &QPushButton::clicked, this, &CwKeyerPage::onCwKeyerRefreshClicked);

    portLayout->addWidget(portLabel);
    portLayout->addWidget(m_cwKeyerPortCombo, 1);
    portLayout->addWidget(m_cwKeyerRefreshBtn);
    layout->addLayout(portLayout);

    // Connect/Disconnect button
    m_cwKeyerConnectBtn = new QPushButton("Connect", this);
    m_cwKeyerConnectBtn->setStyleSheet(K4Styles::Dialog::actionButton());
    connect(m_cwKeyerConnectBtn, &QPushButton::clicked, this, &CwKeyerPage::onCwKeyerConnectClicked);
    layout->addWidget(m_cwKeyerConnectBtn);

    // Separator line
    auto *line3 = new QFrame(this);
    line3->setFrameShape(QFrame::HLine);
    line3->setStyleSheet(K4Styles::Dialog::separator());
    line3->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line3);

    // Keyer section: the PaddleKeyer settings (port of K4-Companion's keyer).
    auto *keyerLabel = new QLabel("Keyer", this);
    keyerLabel->setStyleSheet(K4Styles::Dialog::sectionHeader());
    layout->addWidget(keyerLabel);

    auto addRow = [this, layout](const QString &label, QWidget *field, QWidget *trailing = nullptr) {
        auto *row = new QHBoxLayout();
        auto *l = new QLabel(label, this);
        l->setStyleSheet(K4Styles::Dialog::formLabel());
        l->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);
        row->addWidget(l);
        row->addWidget(field, 1);
        if (trailing)
            row->addWidget(trailing);
        layout->addLayout(row);
    };

    auto *modeCombo = new QComboBox(this);
    modeCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    modeCombo->addItem("Follow K4 (Iambic A / B)", 0);
    modeCombo->addItem("Plain Iambic (no memory)", 1);
    modeCombo->addItem("Ultimatic", 2);
    modeCombo->setCurrentIndex(qBound(0, RadioSettings::instance()->halikeyKeyerMode(), 2));
    connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [modeCombo](int index) {
        RadioSettings::instance()->setHalikeyKeyerMode(modeCombo->itemData(index).toInt());
    });
    addRow("Keyer Mode:", modeCombo);

    auto *inputCombo = new QComboBox(this);
    inputCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    inputCombo->addItem("Paddles", false);
    inputCombo->addItem("Straight key / external keyer", true);
    inputCombo->setCurrentIndex(RadioSettings::instance()->halikeyStraightKey() ? 1 : 0);
    connect(inputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [inputCombo](int index) {
        RadioSettings::instance()->setHalikeyStraightKey(inputCombo->itemData(index).toBool());
    });
    addRow("Input:", inputCombo);

    auto *ditMemSlider = new QSlider(Qt::Horizontal, this);
    ditMemSlider->setRange(0, 100);
    ditMemSlider->setValue(RadioSettings::instance()->halikeyDitMemoryDelay());
    ditMemSlider->setStyleSheet(
        K4Styles::sliderHorizontal(K4Styles::Colors::DarkBackground, K4Styles::Colors::AccentAmber));
    auto *ditMemValue = new QLabel(QString("%1%").arg(ditMemSlider->value()), this);
    ditMemValue->setStyleSheet(QString("color: %1; font-size: %2px;")
                                   .arg(K4Styles::Colors::TextWhite)
                                   .arg(K4Styles::Dimensions::FontSizePopup));
    ditMemValue->setFixedWidth(K4Styles::Dimensions::SliderValueLabelWidth);
    // WHY applied on release, not on every value: each setting is a full save of every setting.
    // Keyboard and click steps arrive with the slider up and are applied at once.
    connect(ditMemSlider, &QSlider::valueChanged, this, [ditMemSlider, ditMemValue](int value) {
        ditMemValue->setText(QString("%1%").arg(value));
        if (!ditMemSlider->isSliderDown())
            RadioSettings::instance()->setHalikeyDitMemoryDelay(value);
    });
    connect(ditMemSlider, &QSlider::sliderReleased, this,
            [ditMemSlider]() { RadioSettings::instance()->setHalikeyDitMemoryDelay(ditMemSlider->value()); });
    addRow("Dit Memory Delay:", ditMemSlider, ditMemValue);

    auto *muteCheck = new QCheckBox("Mute the K4's CW monitor while keying", this);
    muteCheck->setStyleSheet(K4Styles::Dialog::checkBox());
    muteCheck->setChecked(RadioSettings::instance()->halikeyMuteRadioMonitor());
    connect(muteCheck, &QCheckBox::toggled, this,
            [](bool on) { RadioSettings::instance()->setHalikeyMuteRadioMonitor(on); });
    layout->addWidget(muteCheck);

    auto *keyerHelpLabel =
        new QLabel("Iambic A or B follows the K4's own keyer setting. Dit Memory Delay applies to Iambic B: a dit "
                   "tapped in the first part of a dah is ignored rather than remembered, which stops unwanted extra "
                   "dits from a quick squeeze. 33% is the tested default. Mute the K4's monitor only if you hear the "
                   "radio's sidetone come back late through the receive audio.",
                   this);
    keyerHelpLabel->setStyleSheet(K4Styles::Dialog::helpText());
    keyerHelpLabel->setWordWrap(true);
    layout->addWidget(keyerHelpLabel);

    // Separator line
    auto *line5 = new QFrame(this);
    line5->setFrameShape(QFrame::HLine);
    line5->setStyleSheet(K4Styles::Dialog::separator());
    line5->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line5);

    // Sidetone Settings section
    auto *sidetoneLabel = new QLabel("Sidetone Settings", this);
    sidetoneLabel->setStyleSheet(K4Styles::Dialog::sectionHeader());
    layout->addWidget(sidetoneLabel);

    // Sidetone volume slider
    auto *volumeLayout = new QHBoxLayout();
    auto *volumeLabel = new QLabel("Volume:", this);
    volumeLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    volumeLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_sidetoneVolumeSlider = new QSlider(Qt::Horizontal, this);
    m_sidetoneVolumeSlider->setRange(0, 100);
    m_sidetoneVolumeSlider->setValue(RadioSettings::instance()->sidetoneVolume());
    m_sidetoneVolumeSlider->setStyleSheet(
        K4Styles::sliderHorizontal(K4Styles::Colors::DarkBackground, K4Styles::Colors::AccentAmber));

    m_sidetoneVolumeValueLabel = new QLabel(QString("%1%").arg(RadioSettings::instance()->sidetoneVolume()), this);
    m_sidetoneVolumeValueLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                                  .arg(K4Styles::Colors::TextWhite)
                                                  .arg(K4Styles::Dimensions::FontSizePopup));
    m_sidetoneVolumeValueLabel->setFixedWidth(K4Styles::Dimensions::SliderValueLabelWidth);

    // Applied on release for the same reason as Dit Memory Delay above.
    connect(m_sidetoneVolumeSlider, &QSlider::valueChanged, this, [this](int value) {
        m_sidetoneVolumeValueLabel->setText(QString("%1%").arg(value));
        if (!m_sidetoneVolumeSlider->isSliderDown())
            RadioSettings::instance()->setSidetoneVolume(value);
    });
    connect(m_sidetoneVolumeSlider, &QSlider::sliderReleased, this,
            [this]() { RadioSettings::instance()->setSidetoneVolume(m_sidetoneVolumeSlider->value()); });

    volumeLayout->addWidget(volumeLabel);
    volumeLayout->addWidget(m_sidetoneVolumeSlider, 1);
    volumeLayout->addWidget(m_sidetoneVolumeValueLabel);
    layout->addLayout(volumeLayout);

    // Sidetone help text
    auto *sidetoneHelpLabel =
        new QLabel("Local sidetone volume for CW keying feedback. Frequency is linked to K4's CW pitch setting.", this);
    sidetoneHelpLabel->setStyleSheet(K4Styles::Dialog::helpText());
    sidetoneHelpLabel->setWordWrap(true);
    layout->addWidget(sidetoneHelpLabel);

    auto *line6 = new QFrame(this);
    line6->setFrameShape(QFrame::HLine);
    line6->setStyleSheet(K4Styles::Dialog::separator());
    line6->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line6);

    // Remote keying: the K4's key-down initial delay (KZL). Same control as K4-Companion's "Key Down
    // Initial Delay": 0-1000 ms in 10 ms steps, showing what the radio holds.
    auto *remoteLabel = new QLabel("Remote Keying", this);
    remoteLabel->setStyleSheet(K4Styles::Dialog::sectionHeader());
    layout->addWidget(remoteLabel);

    auto *delayLayout = new QHBoxLayout();
    auto *delayLabel = new QLabel("Key-Down Delay:", this);
    delayLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    delayLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_keyDownDelaySlider = new QSlider(Qt::Horizontal, this);
    m_keyDownDelaySlider->setRange(0, 1000);
    m_keyDownDelaySlider->setSingleStep(10);
    m_keyDownDelaySlider->setPageStep(100);
    m_keyDownDelaySlider->setStyleSheet(
        K4Styles::sliderHorizontal(K4Styles::Colors::DarkBackground, K4Styles::Colors::AccentAmber));

    m_keyDownDelayValueLabel = new QLabel(this);
    m_keyDownDelayValueLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                                .arg(K4Styles::Colors::TextWhite)
                                                .arg(K4Styles::Dimensions::FontSizePopup));
    m_keyDownDelayValueLabel->setFixedWidth(K4Styles::Dimensions::SliderValueLabelWidth);

    // WHY send on release, not on every value: a drag would otherwise put dozens of KZL SETs on the
    // wire. Keyboard and click steps arrive with the slider up and are sent at once.
    connect(m_keyDownDelaySlider, &QSlider::valueChanged, this, [this](int value) {
        m_keyDownDelayValueLabel->setText(QString("%1 ms").arg(value));
        if (!m_keyDownDelaySlider->isSliderDown())
            sendKeyDownDelay();
    });
    connect(m_keyDownDelaySlider, &QSlider::sliderReleased, this, &CwKeyerPage::sendKeyDownDelay);

    delayLayout->addWidget(delayLabel);
    delayLayout->addWidget(m_keyDownDelaySlider, 1);
    delayLayout->addWidget(m_keyDownDelayValueLabel);
    layout->addLayout(delayLayout);

    auto *delayHelpLabel =
        new QLabel("How long the K4 holds a remote key-down before it transmits, so keying sent over "
                   "the network arrives in time. Raise it if CW breaks up over a slow or busy link; "
                   "lower it for a quicker start on a fast LAN. Stored in the K4, whose default is "
                   "80 ms. Applies to HaliKey and KPOD+ keying.",
                   this);
    delayHelpLabel->setStyleSheet(K4Styles::Dialog::helpText());
    delayHelpLabel->setWordWrap(true);
    layout->addWidget(delayHelpLabel);

    if (m_radioState)
        connect(m_radioState, &RadioState::keyDownInitialDelayChanged, this, &CwKeyerPage::showKeyDownDelay);
    showKeyDownDelay(m_radioState ? m_radioState->keyDownInitialDelayMs() : -1);

    layout->addStretch();

    // Connect to HalikeyDevice signals for status updates
    if (m_halikeyDevice) {
        connect(m_halikeyDevice, &HalikeyDevice::connected, this, &CwKeyerPage::updateCwKeyerStatus);
        connect(m_halikeyDevice, &HalikeyDevice::disconnected, this, &CwKeyerPage::updateCwKeyerStatus);
    }

    // Initialize status
    updateCwKeyerStatus();
}

void CwKeyerPage::refresh() {
    populateCwKeyerPorts();
    updateCwKeyerStatus();
    showKeyDownDelay(m_radioState ? m_radioState->keyDownInitialDelayMs() : -1);
}

void CwKeyerPage::showKeyDownDelay(int ms) {
    const bool known = ms >= 0;
    m_keyDownDelaySlider->setEnabled(known);
    if (known) {
        const QSignalBlocker block(m_keyDownDelaySlider);
        m_keyDownDelaySlider->setValue(ms);
    }
    m_keyDownDelayValueLabel->setText(known ? QString("%1 ms").arg(ms) : QStringLiteral("--"));
}

void CwKeyerPage::sendKeyDownDelay() {
    if (!m_cwController || !m_radioState)
        return;
    // Snap to the K4's 10 ms steps.
    const int ms = (m_keyDownDelaySlider->value() + 5) / 10 * 10;
    if (ms != m_radioState->keyDownInitialDelayMs())
        m_cwController->setKeyDownInitialDelay(ms);
}

void CwKeyerPage::populateCwKeyerPorts() {
    if (!m_cwKeyerPortCombo)
        return;

    // Block signals to avoid triggering save during repopulation
    m_cwKeyerPortCombo->blockSignals(true);
    m_cwKeyerPortCombo->clear();

    int deviceType = m_cwKeyerDeviceTypeCombo ? m_cwKeyerDeviceTypeCombo->currentData().toInt() : 0;
    QString savedPort = RadioSettings::instance()->halikeyPortName();
    int selectedIndex = -1;

    if (deviceType == 1) {
        // MIDI device -- enumerate MIDI ports
        QStringList midiDevices = HalikeyDevice::availableMidiDevices();
        for (int i = 0; i < midiDevices.size(); i++) {
            m_cwKeyerPortCombo->addItem(midiDevices[i], midiDevices[i]);
            if (midiDevices[i] == savedPort) {
                selectedIndex = i;
            }
        }
        // Auto-select first HaliKey MIDI device if no saved selection matched
        if (selectedIndex < 0) {
            for (int i = 0; i < midiDevices.size(); i++) {
                if (midiDevices[i].contains("HaliKey", Qt::CaseInsensitive)) {
                    selectedIndex = i;
                    break;
                }
            }
        }
    } else {
        // V14 device -- enumerate serial ports
        auto ports = HalikeyDevice::availablePortsDetailed();
        for (int i = 0; i < ports.size(); i++) {
            m_cwKeyerPortCombo->addItem(ports[i].portName, ports[i].portName);
            if (ports[i].portName == savedPort) {
                selectedIndex = i;
            }
        }
    }

    if (selectedIndex >= 0) {
        m_cwKeyerPortCombo->setCurrentIndex(selectedIndex);
    }
    m_cwKeyerPortCombo->blockSignals(false);

    // Save selection when changed (reconnect since we may be called multiple times)
    m_cwKeyerPortCombo->disconnect(this);
    connect(m_cwKeyerPortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0) {
            QString portName = m_cwKeyerPortCombo->itemData(index).toString();
            RadioSettings::instance()->setHalikeyPortName(portName);
        }
    });
}

void CwKeyerPage::updateCwKeyerDescription() {
    if (!m_cwKeyerDescLabel || !m_cwKeyerDeviceTypeCombo)
        return;

    int type = m_cwKeyerDeviceTypeCombo->currentData().toInt();
    if (type == 1) {
        m_cwKeyerDescLabel->setText("Connect a HaliKey MIDI paddle interface to send CW via the K4's keyer. "
                                    "The HaliKey MIDI uses standard MIDI note events to detect paddle and PTT inputs.");
    } else {
        m_cwKeyerDescLabel->setText("Connect a HaliKey paddle interface to send CW via the K4's keyer. "
                                    "The HaliKey uses serial port flow control signals to detect paddle inputs.");
    }
}

void CwKeyerPage::onCwKeyerRefreshClicked() {
    populateCwKeyerPorts();
}

void CwKeyerPage::onCwKeyerConnectClicked() {
    if (!m_halikeyDevice)
        return;

    if (m_halikeyDevice->isConnected()) {
        // Disconnect
        m_halikeyDevice->closePort();
    } else {
        // Connect -- use port name from item data (without annotation suffix)
        QString portName = m_cwKeyerPortCombo->currentData().toString();
        if (!portName.isEmpty()) {
            RadioSettings::instance()->setHalikeyPortName(portName);
            m_halikeyDevice->openPort(portName);
        }
    }
}

void CwKeyerPage::updateCwKeyerStatus() {
    if (!m_cwKeyerStatusLabel || !m_cwKeyerConnectBtn)
        return;

    bool isConnected = m_halikeyDevice && m_halikeyDevice->isConnected();

    if (isConnected) {
        m_cwKeyerStatusLabel->setText(QString("Connected to %1").arg(m_halikeyDevice->portName()));
        m_cwKeyerStatusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::StatusGreen));
        m_cwKeyerConnectBtn->setText("Disconnect");
    } else {
        m_cwKeyerStatusLabel->setText("Not Connected");
        m_cwKeyerStatusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::ErrorRed));
        m_cwKeyerConnectBtn->setText("Connect");
    }
}
