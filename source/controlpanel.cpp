#include "controlpanel.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonValue>
#include <QProcess>
#include <QTime>

ControlPanel::ControlPanel(QObject *parent, QJsonObject userConfig) :
    QObject(parent),
    m_userConfig(userConfig)
{
    m_ucRecordLengthinSeconds = m_userConfig.value("recordLengthInSeconds").toInt(0);

    recordTimer = new QTimer(this);
    QObject::connect(recordTimer, &QTimer::timeout, this, &ControlPanel::recordTimerTick);
}

void ControlPanel::receiveMessage(QString msg)
{
    // Every user-facing diagnostic in the app arrives here as a sendMessage:
    // reconnect warnings, "frame buffer is full", "Recording NOT started", device
    // connect failures. Without this they would live only in m_messageLog, an
    // in-memory ring that dies with the process - absent from exactly the log a
    // user sends us when reporting a field problem. Deliberately uncategorised:
    // these are always relevant, unlike the msDiag bench heartbeats, so
    // QT_LOGGING_RULES should not be able to silence them.
    //
    // Safe to log from here only because no installed message handler emits a
    // signal (see fileMessageHandler in main.cpp, which writes and chains) - one
    // that reached sendMessage would recurse.
    qInfo().noquote() << msg;

    const QString stamp = QTime::currentTime().toString("HH:mm:ss");
    m_messageLog.append(stamp + "  " + msg);
    while (m_messageLog.size() > 500) // enough scrollback; never unbounded
        m_messageLog.removeFirst();
    emit messageLogged(stamp, msg);
}

void ControlPanel::runRecordingHook(const QString &key)
{
    if (!m_userConfig.contains(key))
        return;
    const QJsonObject exeInfo = m_userConfig.value(key).toObject();
    if (!exeInfo.value("enabled").toBool(true))
        return;
    // The documented location is nested (<key>.arguments); a top-level
    // "arguments" key is the spelling this code historically read, kept as a
    // fallback.
    const QJsonArray argArray = exeInfo.contains("arguments")
                                    ? exeInfo.value("arguments").toArray()
                                    : m_userConfig.value("arguments").toArray();
    QStringList argList;
    for (const QJsonValue &arg : argArray)
        argList.append(arg.toString());
    QProcess::startDetached(exeInfo.value("filePath").toString(""), argList);
}

void ControlPanel::startRecording()
{
    if (m_recording)
        return;

    runRecordingHook("executableOnStartRecording");

    // Recording metadata for DataSaver: the config's scalar values (names for
    // the directory structure, recordLengthInSeconds, ...). The old control
    // panel let these be edited in-place before recording; editing now
    // happens in the Setup form, so the config is the single source.
    QMap<QString, QVariant> ucInfo;
    const QStringList keys = m_userConfig.keys();
    for (const QString &k : keys) {
        const QJsonValue v = m_userConfig.value(k);
        if (v.isString())
            ucInfo[k] = v.toString();
        else if (v.isDouble())
            ucInfo[k] = v.toDouble();
    }
    emit recordStart(ucInfo);

    m_currentRecordTime = 0;
    emit currentRecordTimeChanged();
    recordTimer->start(1000);
    setRecordingState(true);
    receiveMessage("Recording started.");
}

void ControlPanel::stopRecording()
{
    if (!m_recording)
        return;

    runRecordingHook("executableOnStopRecording");

    emit recordStop();
    if (recordTimer->isActive())
        recordTimer->stop();
    setRecordingState(false);
    receiveMessage("Recording stopped.");
}

void ControlPanel::onRecordingFailed()
{
    // DataSaver could not create/continue the recording files: reset the state
    // so the UI does not claim to be recording while nothing is being saved.
    if (!m_recording)
        return;
    if (recordTimer->isActive())
        recordTimer->stop();
    m_currentRecordTime = 0;
    emit currentRecordTimeChanged();
    setRecordingState(false);
    receiveMessage("Recording FAILED - nothing is being saved. See error messages above.");
}

void ControlPanel::recordTimerTick()
{
    m_currentRecordTime++;
    emit currentRecordTimeChanged();

    // Timed auto-stop. Unlike the old panel this routes through
    // stopRecording(), so the on-stop executable also runs for timed stops.
    const int limit = recordLengthInSeconds();
    if (limit != 0 && m_currentRecordTime >= limit)
        stopRecording();
}

void ControlPanel::submitNote(const QString &note)
{
    if (note.trimmed().isEmpty())
        return;
    emit sendNote(note);
    receiveMessage("Note logged.");
}

void ControlPanel::setExtTriggerEnabled(bool enabled)
{
    if (m_extTriggerEnabled == enabled)
        return;
    m_extTriggerEnabled = enabled;
    emit extTriggerEnabledChanged();
    emit recordLengthChanged(); // trigger mode disables the timed auto-stop
    emit setExtTriggerTrackingState(enabled);
}

void ControlPanel::extTriggerTriggered(bool state)
{
    if (state) {
        receiveMessage("Trigger: HIGH");
        startRecording();
    }
    else {
        receiveMessage("Trigger: LOW");
        stopRecording();
    }
}

void ControlPanel::setRecordingState(bool recording)
{
    if (m_recording == recording)
        return;
    m_recording = recording;
    emit recordingChanged();
}
