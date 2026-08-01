#include <QtTest>

#include "commutator.h"

#include <cmath>
#include <limits>

// Wire format of the commutator's JSON command protocol
// (open-ephys.github.io/commutator-docs -> User Guide -> Remote Control).
//
// This is the layer that failed silently in the field: the port opened, the
// status LED obeyed, and the motor never turned, because the commands went out
// unterminated. The controller parses a command only when it sees a LF, and if
// several JSON objects arrive before one, ONLY THE FIRST is processed - so
// writing "{enable:true}{led:true}{turn:0.1}" as one unterminated stream is a
// no-op on RP2040 (USB-C) controllers. Nothing about that is visible from the
// host side, hence these tests.
class TestCommutatorProtocol : public QObject
{
    Q_OBJECT

private slots:
    void everyCommandIsOneTerminatedLine();
    void commandsConcatenateIntoSeparateLines();
    void turnCommandUsesFixedNotation();
    void negligibleAndNonFiniteTurnsAreSkipped();
    void turnCommandKeepsSignAndMagnitude();
    void turnCommandIsLocaleIndependent();
};

void TestCommutatorProtocol::everyCommandIsOneTerminatedLine()
{
    const QByteArray frame = Commutator::wireFrame(QStringLiteral("{enable:true}"));
    QCOMPARE(frame, QByteArray("{enable:true}\n"));
    QVERIFY2(frame.endsWith('\n'), "commands must be LF-terminated or the controller never parses them");
    QCOMPARE(frame.count('\n'), 1);   // exactly one command per line
}

void TestCommutatorProtocol::commandsConcatenateIntoSeparateLines()
{
    // The init pair is written back-to-back and may be flushed as one chunk;
    // with the terminators the controller still sees two complete commands.
    const QByteArray stream = Commutator::wireFrame(QStringLiteral("{enable:true}"))
                              + Commutator::wireFrame(QStringLiteral("{led:true}"));
    const QList<QByteArray> lines = stream.split('\n');
    QCOMPARE(lines.size(), 3);   // two commands + trailing empty
    QCOMPARE(lines[0], QByteArray("{enable:true}"));
    QCOMPARE(lines[1], QByteArray("{led:true}"));
    QVERIFY(lines[2].isEmpty());
}

void TestCommutatorProtocol::turnCommandUsesFixedNotation()
{
    // 'g' formatting would emit "1e-05" here, which the controller's JSON parser
    // is not documented to accept.
    const QString small = Commutator::turnCommand(0.00001);
    QVERIFY2(!small.contains(QLatin1Char('e'), Qt::CaseInsensitive),
             qPrintable("exponent notation reached the wire: " + small));
    QCOMPARE(small, QStringLiteral("{turn:0.000010}"));
}

void TestCommutatorProtocol::negligibleAndNonFiniteTurnsAreSkipped()
{
    // A still animal produces ~0: no command at all, rather than "{turn:0.000000}".
    QVERIFY(Commutator::turnCommand(0.0).isEmpty());
    QVERIFY(Commutator::turnCommand(1e-9).isEmpty());
    QVERIFY(Commutator::turnCommand(-1e-9).isEmpty());
    QVERIFY(Commutator::turnCommand(std::numeric_limits<double>::quiet_NaN()).isEmpty());
    QVERIFY(Commutator::turnCommand(std::numeric_limits<double>::infinity()).isEmpty());
    QVERIFY(Commutator::turnCommand(-std::numeric_limits<double>::infinity()).isEmpty());
}

void TestCommutatorProtocol::turnCommandKeepsSignAndMagnitude()
{
    // The sign is the direction of rotation, so it must survive verbatim.
    QCOMPARE(Commutator::turnCommand(1.1), QStringLiteral("{turn:1.100000}"));
    QCOMPARE(Commutator::turnCommand(-0.5), QStringLiteral("{turn:-0.500000}"));
}

void TestCommutatorProtocol::turnCommandIsLocaleIndependent()
{
    // A German/French locale would render 0.25 as "0,25" through QString::arg,
    // which is not valid JSON - QString::number(double, char, int) must be used.
    QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
    QCOMPARE(Commutator::turnCommand(0.25), QStringLiteral("{turn:0.250000}"));
    QLocale::setDefault(QLocale(QLocale::C));
}

// Guiless, like tst_twistcalculator: this is pure string/number logic, but the
// target links Qt6::Gui (commutator.cpp pulls in QQuaternion), so a plain
// QTEST_MAIN would construct a QGuiApplication and abort on headless CI with
// "no Qt platform plugin could be initialized".
QTEST_GUILESS_MAIN(TestCommutatorProtocol)
#include "tst_commutatorprotocol.moc"
