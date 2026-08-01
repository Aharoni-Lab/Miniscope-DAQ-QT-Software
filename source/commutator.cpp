#include "commutator.h"

#include <QSerialPort>
#include <QSerialPortInfo>
#include <QJsonArray>
#include <QTimer>

#include <cmath>

namespace {

// Turn commands smaller than this serialize to "0.000000" in the wire format
// below, so they are not worth a write. The device integrates turn commands, so
// skipping them loses nothing (1e-6 turns is 0.00036 degrees - orders of
// magnitude below both the BNO's noise floor and one motor step).
constexpr double kMinTurnCommand = 1e-6;

// How long after connecting the one-shot status report is emitted.
constexpr int kStatusReportDelayMs = 5000;

// Cap on buffered controller output, in case a controller talks without ever
// sending a newline.
constexpr int kMaxReadBufferBytes = 4096;

// Read a [x, y, z] JSON array into a QVector3D, falling back to def if the value
// is missing or not a 3-element array.
QVector3D parseAxis(const QJsonValue &value, const QVector3D &def)
{
    const QJsonArray a = value.toArray();
    if (a.size() != 3)
        return def;
    return QVector3D(float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble()));
}

// Human-readable list of the serial ports currently present, for error messages
// when the configured port can't be opened.
QString availablePortList()
{
    QStringList names;
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts())
        names << info.portName();
    return names.isEmpty() ? QStringLiteral("(none detected)") : names.join(", ");
}

} // namespace

Commutator::Commutator(QObject *parent, QJsonObject ucCommutator)
    : QObject(parent)
{
    m_portName = ucCommutator["port"].toString();
    m_sourceDeviceName = ucCommutator["deviceName"].toString();
    m_ledEnabled = ucCommutator["led"].toBool(true);

    const double rate = ucCommutator["sampleRate"].toDouble(10.0);
    m_sampleIntervalMs = (rate > 0.0) ? int(1000.0 / rate) : 100;

    const QVector3D headAxis = parseAxis(ucCommutator["headstageAxis"], QVector3D(0.0f, 0.0f, 1.0f));
    const QVector3D commAxis = parseAxis(ucCommutator["commutatorAxis"], QVector3D(0.0f, 0.0f, 1.0f));
    const double fallbackThreshold = ucCommutator["fallbackThreshold"].toDouble(-0.9);
    const TwistCalculator::FallbackMode mode =
        (ucCommutator["fallbackMode"].toString("global").compare("local", Qt::CaseInsensitive) == 0)
            ? TwistCalculator::FallbackMode::Local
            : TwistCalculator::FallbackMode::Global;
    m_twist.configure(headAxis, commAxis, fallbackThreshold, mode);
}

void Commutator::startRunning()
{
    if (m_portName.isEmpty()) {
        emit sendMessage("ERROR: Commutator is enabled but no serial 'port' is set in the config; "
                         "commutator disabled. Ports available: " + availablePortList());
        return;
    }

    m_running = true;
    openPort();   // if it fails, handleNewQuaternion keeps retrying
    QTimer::singleShot(kStatusReportDelayMs, this, &Commutator::reportStatus);
}

bool Commutator::openPort()
{
    // Start from a clean port object each attempt (a port dropped on unplug is
    // closed but still allocated).
    if (m_port) {
        m_port->close();
        m_port->deleteLater();
        m_port = nullptr;
    }

    m_port = new QSerialPort(this);
    m_port->setPortName(m_portName);
    // The controller is a USB-CDC virtual serial port, which ignores line
    // settings, but set sane 115200 8N1 values so a real UART adapter also works.
    m_port->setBaudRate(QSerialPort::Baud115200);
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setParity(QSerialPort::NoParity);
    m_port->setStopBits(QSerialPort::OneStop);
    m_port->setFlowControl(QSerialPort::NoFlowControl);
    connect(m_port, &QSerialPort::errorOccurred, this, &Commutator::handlePortError);
    connect(m_port, &QSerialPort::readyRead, this, &Commutator::handleReadyRead);

    if (!m_port->open(QIODevice::ReadWrite)) {
        // Report once. Suppressed if we already announced a disconnect (the
        // unplug case) so a mid-session outage doesn't also spam open failures.
        if (!m_reportedOpenError && !m_reportedDisconnect) {
            emit sendMessage("ERROR: Commutator could not open serial port '" + m_portName + "': "
                             + m_port->errorString() + ". Retrying until it appears. Ports available: "
                             + availablePortList());
            m_reportedOpenError = true;
        }
        m_port->deleteLater();
        m_port = nullptr;
        return false;
    }

    // USB-CDC microcontroller boards commonly gate their serial link on DTR, and
    // some only start transmitting once the host asserts it. Qt asserts it for
    // NoFlowControl, but do it explicitly rather than depend on that default.
    m_port->setDataTerminalReady(true);

    // Fresh link: discard any stale orientation reference so the first sample
    // after (re)connecting doesn't emit a large catch-up turn.
    m_twist.reset();
    m_haveSampleClock = false;
    m_readBuffer.clear();
    writeCommand(QStringLiteral("{enable:true}"));
    writeCommand(m_ledEnabled ? QStringLiteral("{led:true}") : QStringLiteral("{led:false}"));
    // Ask the controller to report back (firmware version, enable/led state,
    // motor power). The reply is the only host-visible proof that our commands
    // are being parsed at all, and it lands in the session log - a silent
    // controller means the commands are not getting through, an "enable: false"
    // in the reply means they are but the motor is not armed.
    writeCommand(QStringLiteral("{print:true}"));

    const bool wasOutage = m_reportedDisconnect || m_reportedOpenError;
    emit sendMessage(QString(wasOutage ? "Commutator reconnected on " : "Commutator connected on ")
                     + m_portName + ", driven by \"" + m_sourceDeviceName + "\" head orientation.");
    m_reportedOpenError = false;
    m_reportedDisconnect = false;
    return true;
}

// Surface whatever the controller says (it replies to {print:true}, and firmware
// may report errors unprompted) as log lines, so the device's own view of its
// state is visible instead of being guessed at from the host side.
void Commutator::handleReadyRead()
{
    if (!m_port)
        return;
    m_readBuffer.append(m_port->readAll());
    // A controller that talks without ever sending a newline must not grow this
    // without bound.
    if (m_readBuffer.size() > kMaxReadBufferBytes)
        m_readBuffer = m_readBuffer.right(kMaxReadBufferBytes);

    int newline;
    while ((newline = m_readBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_readBuffer.left(newline).trimmed();
        m_readBuffer.remove(0, newline + 1);
        if (!line.isEmpty()) {
            m_repliesReceived++;
            emit sendMessage("Commutator reports: " + QString::fromUtf8(line));
        }
    }
}

void Commutator::handlePortError(QSerialPort::SerialPortError error)
{
    // ResourceError is what a USB unplug surfaces as; treat a couple of related
    // fatal states the same way. Close the port so the reconnect path reopens it.
    if (error != QSerialPort::ResourceError && error != QSerialPort::PermissionError)
        return;

    if (m_port && m_port->isOpen())
        m_port->close();
    if (!m_reportedDisconnect) {
        emit sendMessage("Commutator disconnected on " + m_portName
                         + "; will reconnect automatically when it returns.");
        m_reportedDisconnect = true;
    }
}

void Commutator::stopRunning()
{
    if (portOk())
        writeCommand(QStringLiteral("{enable:false}"));   // writeCommand flushes
    if (m_port) {
        m_port->close();
        m_port->deleteLater();
        m_port = nullptr;
    }
    m_running = false;
}

bool Commutator::portOk() const
{
    return m_port && m_port->isOpen();
}

void Commutator::handleNewQuaternion(double w, double x, double y, double z)
{
    if (!m_running)
        return;
    m_samplesReceived++;

    // Port down (never opened, or unplugged): retry opening it at ~1 Hz instead
    // of sending. The scope's orientation stream keeps calling us while running,
    // so this doubles as the reconnect clock - no separate timer needed.
    if (!portOk()) {
        if (!m_reconnectClock.isValid() || m_reconnectClock.elapsed() >= 1000) {
            m_reconnectClock.restart();
            openPort();
        }
        return;
    }

    // Decimate the (frame-rate) orientation stream to the configured sample
    // rate, so twist is computed between evenly-spaced samples (matching the
    // Bonsai SampleInterval -> QuaternionToTwist pipeline) and serial traffic
    // stays bounded.
    if (m_haveSampleClock && m_sampleClock.elapsed() < m_sampleIntervalMs)
        return;
    m_sampleClock.restart();
    m_haveSampleClock = true;

    const double turns = m_twist.update(QQuaternion(float(w), float(x), float(y), float(z)));

    const QString command = turnCommand(turns);
    if (command.isEmpty())
        return;   // still animal / no usable twist: nothing to send
    writeCommand(command);
    m_lastTurnCommand = command;
    m_turnCommandsSent++;
}

// One LF-terminated line per command.
//
// The controller only parses a command when it sees the LF, and if several JSON
// objects arrive before one, ONLY THE FIRST IS PROCESSED - see the protocol docs
// (open-ephys.github.io/commutator-docs -> User Guide -> Remote Control). Writing
// bare "{enable:true}{led:true}{turn:...}" therefore executed at most the first
// command on an RP2040 (USB-C) controller and never turned the motor, even though
// the port opened cleanly. Teensy (Micro-USB) controllers don't need the LF but
// accept it, so the terminator is right for every version.
QByteArray Commutator::wireFrame(const QString &command)
{
    return command.toUtf8() + '\n';
}

QString Commutator::turnCommand(double turns)
{
    // Bonsai skips 0/NaN/inf turns; a still animal produces ~0 and needs no
    // command. QString::number is locale-independent (no stray decimal commas),
    // and 'f' rather than 'g' so a tiny value can never go out in exponent form
    // ("1e-05"), which the controller's JSON parser is not documented to accept.
    if (!std::isfinite(turns) || std::abs(turns) < kMinTurnCommand)
        return QString();
    return QStringLiteral("{turn:%1}").arg(QString::number(turns, 'f', 6));
}

// Flushed per command: this is a real-time control signal, not bulk data, so it
// must not sit in the buffer waiting for the next write.
void Commutator::writeCommand(const QString &command)
{
    if (!portOk())
        return;
    m_port->write(wireFrame(command));
    m_port->flush();
}

// Emitted once, a few seconds after connecting. This feature could fail while
// looking healthy - the port opens, the LED obeys, and nothing ever turns - so
// it now says out loud whether orientation samples and turn commands are
// actually flowing, and which link is missing when they are not.
void Commutator::reportStatus()
{
    if (!m_running)
        return;

    // Opening the WRONG port succeeds - any serial device accepts an open - so
    // "connected" alone proves nothing. Nothing answering the {print:true} sent
    // at connect is the one host-side hint that the port belongs to something
    // else entirely, which is easy to do when several COM ports are present.
    if (portOk() && m_repliesReceived == 0)
        emit sendMessage("Warning: nothing on " + m_portName + " answered the commutator's status "
                         "query. If the tether does not turn, make sure this is the commutator's "
                         "port and not another device's. Ports available: " + availablePortList());

    if (m_samplesReceived == 0)
        emit sendMessage("Warning: Commutator has received no head-orientation samples. It needs a "
                         "Miniscope with headOrientation enabled and streaming to drive it.");
    else if (m_turnCommandsSent == 0)
        emit sendMessage("Warning: Commutator received " + QString::number(m_samplesReceived)
                         + " head-orientation samples but computed no rotation from them. Check "
                           "\"headstageAxis\" / \"commutatorAxis\" in the config's commutator section.");
    else
        emit sendMessage("Commutator is driving the motor: " + QString::number(m_turnCommandsSent)
                         + " turn commands sent so far (last " + m_lastTurnCommand + ").");
}
