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
// Each command is one LF-terminated line (see writeCommand): the controller
// parses a command only on the LF, and several JSON objects arriving before one
// leave all but the first unprocessed.
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

    // --- Wire format (static + pure, so it is unit-testable without a port) ---

    // The bytes for one command: exactly one LF-terminated line.
    static QByteArray wireFrame(const QString &command);
    // The {turn:x} command for a computed twist, or an empty string when there
    // is nothing worth sending (0, sub-resolution, NaN/inf). Always fixed
    // notation - never exponent form, which the controller is not documented to
    // parse.
    static QString turnCommand(double turns);

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

    // One-shot report (armed by startRunning) naming which link of the chain is
    // live: orientation samples in, turn commands out.
    void reportStatus();

    // Logs each complete line the controller sends back (its {print:true} reply,
    // and any unprompted firmware output).
    void handleReadyRead();

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

    // Liveness counters behind reportStatus().
    qint64 m_samplesReceived = 0;
    qint64 m_turnCommandsSent = 0;
    qint64 m_repliesReceived = 0;   // lines the controller sent back
    QString m_lastTurnCommand;

    // Partial line assembly for the controller's replies (see handleReadyRead).
    QByteArray m_readBuffer;
};

#endif // COMMUTATOR_H
