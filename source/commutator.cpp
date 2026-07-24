#include "commutator.h"

#include <QSerialPort>
#include <QSerialPortInfo>
#include <QJsonArray>

#include <cmath>

namespace {

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

    // Fresh link: discard any stale orientation reference so the first sample
    // after (re)connecting doesn't emit a large catch-up turn.
    m_twist.reset();
    m_haveSampleClock = false;
    writeCommand(QStringLiteral("{enable:true}"));
    writeCommand(m_ledEnabled ? QStringLiteral("{led:true}") : QStringLiteral("{led:false}"));

    const bool wasOutage = m_reportedDisconnect || m_reportedOpenError;
    emit sendMessage(QString(wasOutage ? "Commutator reconnected on " : "Commutator connected on ")
                     + m_portName + ", driven by \"" + m_sourceDeviceName + "\" head orientation.");
    m_reportedOpenError = false;
    m_reportedDisconnect = false;
    return true;
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
    if (portOk()) {
        writeCommand(QStringLiteral("{enable:false}"));
        m_port->flush();
    }
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

    // Bonsai skips 0/NaN/inf turns; a still animal produces ~0 and needs no
    // command. QString::number is locale-independent (no stray decimal commas).
    if (std::isfinite(turns) && turns != 0.0)
        writeCommand(QStringLiteral("{turn:%1}").arg(QString::number(turns, 'g', 10)));
}

void Commutator::writeCommand(const QString &command)
{
    if (portOk())
        m_port->write(command.toUtf8());
}
