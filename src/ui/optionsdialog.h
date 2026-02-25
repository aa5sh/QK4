#ifndef OPTIONSDIALOG_H
#define OPTIONSDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QCheckBox>
#include <QLineEdit>
#include <QComboBox>
#include <QSlider>
#include <QPushButton>
#include <QShowEvent>
#include <QHideEvent>

class RadioState;
class AudioEngine;
class MicMeterWidget;
class KpodDevice;
class CatServer;
class HalikeyDevice;

class OptionsDialog : public QDialog {
    Q_OBJECT

public:
    enum Page { PageAbout = 0, PageAudioInput, PageAudioOutput, PageRigControl, PageCwKeyer, PageKpod, PageCount };

    explicit OptionsDialog(RadioState *radioState, AudioEngine *audioEngine, KpodDevice *kpodDevice,
                           CatServer *catServer, HalikeyDevice *halikeyDevice, QWidget *parent = nullptr);
    ~OptionsDialog();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private slots:
    void onMicTestToggled(bool checked);
    void onMicLevelChanged(float level);
    void onMicDeviceChanged(int index);
    void onMicGainChanged(int value);
    void updateKpodStatus();
    void updateCwKeyerStatus();
    void onCwKeyerConnectClicked();
    void onCwKeyerRefreshClicked();

private:
    void setupUi();
    void refreshCurrentPage();
    void refreshPage(int index);
    QWidget *createAboutPage();
    QWidget *createKpodPage();
    QWidget *createAudioInputPage();
    QWidget *createAudioOutputPage();
    QWidget *createRigControlPage();
    QWidget *createCwKeyerPage();
    void updateCatServerStatus();
    void populateMicDevices();
    void populateSpeakerDevices();
    void populateCwKeyerPorts();

    RadioState *m_radioState;
    AudioEngine *m_audioEngine;
    KpodDevice *m_kpodDevice;
    CatServer *m_catServer;
    HalikeyDevice *m_halikeyDevice;
    QListWidget *m_tabList;
    QStackedWidget *m_pageStack;
    bool m_pageCreated[PageCount] = {};

    // KPOD page elements (for real-time updates)
    QCheckBox *m_kpodEnableCheckbox = nullptr;
    QLabel *m_kpodStatusLabel = nullptr;
    QLabel *m_kpodProductLabel = nullptr;
    QLabel *m_kpodManufacturerLabel = nullptr;
    QLabel *m_kpodVendorIdLabel = nullptr;
    QLabel *m_kpodProductIdLabel = nullptr;
    QLabel *m_kpodDeviceTypeLabel = nullptr;
    QLabel *m_kpodFirmwareLabel = nullptr;
    QLabel *m_kpodDeviceIdLabel = nullptr;
    QLabel *m_kpodHelpLabel = nullptr;

    // Audio Input settings
    QComboBox *m_micDeviceCombo = nullptr;
    QSlider *m_micGainSlider = nullptr;
    QLabel *m_micGainValueLabel = nullptr;
    QPushButton *m_micTestBtn = nullptr;
    MicMeterWidget *m_micMeter = nullptr;
    bool m_micTestActive = false;

    // Audio Output settings
    QComboBox *m_speakerDeviceCombo = nullptr;

    // CAT Server page elements
    QCheckBox *m_catServerEnableCheckbox = nullptr;
    QLineEdit *m_catServerPortEdit = nullptr;
    QLabel *m_catServerStatusLabel = nullptr;
    QLabel *m_catServerClientsLabel = nullptr;

    void onSpeakerDeviceChanged(int index);

    // CW Keyer page elements
    QComboBox *m_cwKeyerDeviceTypeCombo = nullptr;
    QLabel *m_cwKeyerDescLabel = nullptr;
    QComboBox *m_cwKeyerPortCombo = nullptr;
    QPushButton *m_cwKeyerRefreshBtn = nullptr;
    QPushButton *m_cwKeyerConnectBtn = nullptr;
    QLabel *m_cwKeyerStatusLabel = nullptr;
    QSlider *m_sidetoneVolumeSlider = nullptr;
    QLabel *m_sidetoneVolumeValueLabel = nullptr;
    void updateCwKeyerDescription();
};

#endif // OPTIONSDIALOG_H
