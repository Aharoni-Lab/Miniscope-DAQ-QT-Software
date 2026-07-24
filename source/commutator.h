#ifndef COMMUTATOR_H
#define COMMUTATOR_H

#include <QObject>
#include <QJsonObject>
#include <QQuaternion>
#include <QElapsedTimer>
#include <QString>
#include <QSerialPort>

#include "twistcalculator.h"

// Drives an Open Ephys commutator (github.com/open-ephys/commutator-controller)
// directly over its USB serial port, so the tether unwinds as the animal turns
// without needing the separate Bonsai plugin. It consumes a Miniscope's live BNO
// head-orientation quaternion, converts each sample to an incremental rotation
// with TwistCalculator, and writes the device's tiny text protocol:
//   {turn:<double>}   relative rotation in full motor turns
//   {enable:<bool>}   motor driver on/off
//   {led:<bool>}      indicator LED on/off
//
// Lives on its own QThread (like DataSaver); quaternions arrive from the source
// Miniscope's display-frame handler via a queued connection, so no serial I/O
// ever runs on the GUI/render thread. Configured from the user config's optional
// top-level "commutator" object.
class Commutator : public QObject
{
    Q_OBJECT
public:
    explicit Commutator(QObject *parent, QJsonObject ucCommutator);

    // deviceName of the Miniscope whose head orientation should drive rotation.
    // Empty means "the first Miniscope with head orientation streaming enabled",
    // resolved by the backend.
    QString sourceDeviceName() const { return m_sourceDeviceName; }

public slots:
    // Thread entry point (connect to QThread::started): opens the serial port and
    // enables the motor + indicator LED. Emits sendMessage() on success/failure.
    void startRunning();
    // Disables the motor and closes the port. Idempotent.
    void stopRunning();

    // Feed the newest head-orientation quaternion (queued from the Miniscope).
    // Decimated to the configured sample rate before a turn command is emitted.
    // Also drives reconnection: while the port is down it retries opening it
    // (throttled) instead of sending, so a mid-session USB unplug/replug of the
    // commutator recovers on its own.
    void handleNewQuaternion(double w, double x, double y, double z);

    // Drops the port when the OS reports the device vanished (unplug), so the
    // reconnect path in handleNewQuaternion takes over.
    void handlePortError(QSerialPort::SerialPortError error);

signals:
    void sendMessage(QString msg);

private:
    // (Re)open the serial port and, on success, enable the motor + LED and reset
    // the twist state. Returns true if the port is open afterwards. Emits the
    // open-failure message only once per outage so retries don't spam the log.
    bool openPort();
    bool portOk() const;
    void writeCommand(const QString &command);

    QString m_portName;
    QString m_sourceDeviceName;
    int m_sampleIntervalMs = 100;   // 10 Hz default
    bool m_ledEnabled = true;

    QSerialPort *m_port = nullptr;
    TwistCalculator m_twist;
    QElapsedTimer m_sampleClock;
    bool m_haveSampleClock = false;
    bool m_running = false;           // between startRunning() and stopRunning()

    // Reconnection bookkeeping: retry opening the port at ~1 Hz while it is
    // down, and report each outage/recovery once.
    QElapsedTimer m_reconnectClock;
    bool m_reportedOpenError = false;
    bool m_reportedDisconnect = false;
};

#endif // COMMUTATOR_H
