#ifndef AUDIOOUTPUTPAGE_H
#define AUDIOOUTPUTPAGE_H

#include <QWidget>
#include <QComboBox>

class AudioController;

/**
 * @brief OptionsDialog "Audio Output" tab. Selects the system speaker device; writes selection
 *        back to AudioEngine via AudioController's task-level API.
 */
class AudioOutputPage : public QWidget {
    Q_OBJECT

public:
    explicit AudioOutputPage(AudioController *audioController, QWidget *parent = nullptr);

    void refresh();

private slots:
    void onSpeakerDeviceChanged(int index);
    void onRxBoostChanged(int index);

private:
    void populateSpeakerDevices();

    AudioController *m_audioController;
    QComboBox *m_speakerDeviceCombo = nullptr;
    QComboBox *m_rxBoostCombo = nullptr;
};

#endif // AUDIOOUTPUTPAGE_H
