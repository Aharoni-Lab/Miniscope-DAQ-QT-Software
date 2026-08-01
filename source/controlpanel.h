#ifndef CONTROLPANEL_H
#define CONTROLPANEL_H

#include <QObject>
#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <QTimer>
#include <QVariant>

// Windowless session controller behind the Acquire view's session bar: owns
// the recording state machine (start/stop, timed auto-stop, external
// trigger, the pre/post recording executables) and the session message log.
// QML reaches it through backend.sessionControl; the wiring to DataSaver and
// the devices (recordStart/recordStop/sendNote signals) is unchanged from
// the old control-panel window this class used to drive.
class ControlPanel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(int currentRecordTime READ currentRecordTime NOTIFY currentRecordTimeChanged)
    // Effective auto-stop length: 0 (no limit) while the external trigger
    // drives recording, the config's value otherwise.
    Q_PROPERTY(int recordLengthInSeconds READ recordLengthInSeconds NOTIFY recordLengthChanged)
    Q_PROPERTY(bool extTriggerEnabled READ extTriggerEnabled WRITE setExtTriggerEnabled NOTIFY extTriggerEnabledChanged)
    Q_PROPERTY(QStringList messageLog READ messageLog NOTIFY messageLogged)

public:
    explicit ControlPanel(QObject *parent = nullptr, QJsonObject userConfig = QJsonObject());

    bool recording() const { return m_recording; }
    int currentRecordTime() const { return m_currentRecordTime; }
    int recordLengthInSeconds() const { return m_extTriggerEnabled ? 0 : m_ucRecordLengthinSeconds; }
    bool extTriggerEnabled() const { return m_extTriggerEnabled; }
    void setExtTriggerEnabled(bool enabled);
    QStringList messageLog() const { return m_messageLog; }

    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void submitNote(const QString &note);

public slots:
    void receiveMessage(QString msg);
    void onRecordingFailed();
    void recordTimerTick();
    // Frame-synced external trigger from a Miniscope: HIGH starts the
    // recording, LOW stops it.
    void extTriggerTriggered(bool state);

signals:
    // Session plumbing (backend wires these to DataSaver and the devices).
    void recordStart(QMap<QString, QVariant> ucInfo);
    void recordStop();
    void sendNote(QString note);
    void setExtTriggerTrackingState(bool state);

    // QML notifications.
    void recordingChanged();
    void currentRecordTimeChanged();
    void recordLengthChanged();
    void extTriggerEnabledChanged();
    void messageLogged(QString timeStamp, QString msg);

private:
    void setRecordingState(bool recording);
    // Launch the executable configured under the given key
    // ("executableOnStartRecording" / "executableOnStopRecording"), if enabled.
    void runRecordingHook(const QString &key);

    QJsonObject m_userConfig;
    QTimer *recordTimer;
    int m_currentRecordTime = 0;
    int m_ucRecordLengthinSeconds = 0;
    bool m_recording = false;
    bool m_extTriggerEnabled = false;
    QStringList m_messageLog;
};

#endif // CONTROLPANEL_H
