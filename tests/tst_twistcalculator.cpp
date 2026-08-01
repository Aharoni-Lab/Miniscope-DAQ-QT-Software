#include <QtTest>
#include <QQuaternion>
#include <QVector3D>

#include "twistcalculator.h"

#include <cmath>

// Unit tests for the C++ port of Open Ephys's QuaternionToTwist. The purpose is
// to pin two things that a port can silently get wrong: the SIGN (the commutator
// must unwind, not wind up) and the quaternion-multiplication CONVENTION (Qt's
// QQuaternion vs. System.Numerics.Quaternion in the original C#). The turns
// returned for a known rotation about the tether axis are the ground truth.
class TestTwistCalculator : public QObject
{
    Q_OBJECT

private:
    static constexpr double kTwoPi = 6.283185307179586;

    // Standard upright mount: tether leaves the headstage along +Z and enters
    // the commutator along +Z.
    static TwistCalculator uprightZ()
    {
        TwistCalculator t;
        t.configure(QVector3D(0, 0, 1), QVector3D(0, 0, 1), -0.9,
                    TwistCalculator::FallbackMode::Global);
        return t;
    }

    static QQuaternion rotZ(double degrees)
    {
        return QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), float(degrees));
    }

private slots:
    void firstSampleReturnsZero();
    void zeroQuaternionReturnsZero();
    void noRotationReturnsZero();
    void pureTetherRotationGivesExpectedTurns();
    void nonIdentityPreviousStillGivesDelta();
    void accumulatesAcrossSamples();
    void rotationOrthogonalToTetherGivesNoTwist();
    void reverseRotationFlipsSign();
    void resetForgetsPreviousSample();
};

void TestTwistCalculator::firstSampleReturnsZero()
{
    TwistCalculator t = uprightZ();
    QCOMPARE(t.update(rotZ(37.0)), 0.0);   // nothing to compare against yet
}

void TestTwistCalculator::zeroQuaternionReturnsZero()
{
    TwistCalculator t = uprightZ();
    t.update(rotZ(0.0));                    // seed
    QCOMPARE(t.update(QQuaternion(0, 0, 0, 0)), 0.0);   // IMU "no data" sentinel
}

void TestTwistCalculator::noRotationReturnsZero()
{
    TwistCalculator t = uprightZ();
    t.update(rotZ(20.0));
    QVERIFY(std::abs(t.update(rotZ(20.0))) < 1e-4);   // identical orientation -> no turn
}

void TestTwistCalculator::pureTetherRotationGivesExpectedTurns()
{
    TwistCalculator t = uprightZ();
    t.update(rotZ(0.0));                    // seed at identity
    // +90 deg about the tether axis -> the commutator turns to cancel it:
    // -(pi/2)/(2pi) = -0.25 turns.
    const double turns = t.update(rotZ(90.0));
    QVERIFY2(std::abs(turns - (-0.25)) < 1e-4, qPrintable(QString::number(turns)));
}

void TestTwistCalculator::nonIdentityPreviousStillGivesDelta()
{
    // The convention test: with a non-identity previous orientation, the delta
    // must be current*prev^-1 (not the reverse). Prev 30 deg, current 120 deg
    // about Z is a +90 deg increment -> -0.25 turns. A flipped multiplication
    // order would yield +0.25.
    TwistCalculator t = uprightZ();
    t.update(rotZ(30.0));
    const double turns = t.update(rotZ(120.0));
    QVERIFY2(std::abs(turns - (-0.25)) < 1e-4, qPrintable(QString::number(turns)));
}

void TestTwistCalculator::accumulatesAcrossSamples()
{
    // Two 45 deg increments sum to the same total as one 90 deg step, because
    // the operator is incremental.
    TwistCalculator t = uprightZ();
    t.update(rotZ(0.0));
    const double a = t.update(rotZ(45.0));
    const double b = t.update(rotZ(90.0));
    QVERIFY2(std::abs((a + b) - (-0.25)) < 1e-4,
             qPrintable(QString::number(a) + " + " + QString::number(b)));
}

void TestTwistCalculator::rotationOrthogonalToTetherGivesNoTwist()
{
    // Pure pitch/roll (about X) does not twist the tether about Z, so it must
    // command ~no rotation.
    TwistCalculator t = uprightZ();
    t.update(rotZ(0.0));
    const QQuaternion pitched = QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), 60.0f);
    QVERIFY(std::abs(t.update(pitched)) < 1e-4);
}

void TestTwistCalculator::reverseRotationFlipsSign()
{
    // Rotating the other way about the tether axis reverses the commutator's
    // direction: -90 deg -> +0.25 turns (vs. -0.25 for +90 deg).
    TwistCalculator t = uprightZ();
    t.update(rotZ(0.0));
    const double turns = t.update(rotZ(-90.0));
    QVERIFY2(std::abs(turns - 0.25) < 1e-4, qPrintable(QString::number(turns)));
}

void TestTwistCalculator::resetForgetsPreviousSample()
{
    TwistCalculator t = uprightZ();
    t.update(rotZ(10.0));
    t.reset();
    QCOMPARE(t.update(rotZ(80.0)), 0.0);   // treated as a fresh first sample
}

// GUILESS: this test uses only QQuaternion/QVector3D value types, which need no
// QGuiApplication or platform plugin. A plain QTEST_MAIN would start a
// QGuiApplication (Qt6::Gui is linked) and abort on headless CI with "no Qt
// platform plugin could be initialized".
QTEST_GUILESS_MAIN(TestTwistCalculator)
#include "tst_twistcalculator.moc"
