#ifndef TWISTCALCULATOR_H
#define TWISTCALCULATOR_H

#include <QQuaternion>
#include <QVector3D>

// C++ port of the Open Ephys bonsai-commutator "QuaternionToTwist" operator
// (github.com/open-ephys/bonsai-commutator). Turns a stream of head-orientation
// quaternions into the incremental rotation ("twist") about the tether axis that
// has happened since the previous sample, in units of full turns - exactly what
// the commutator's {turn:x} command consumes to unwind the cable.
//
// Stateful, like the Bonsai operator: it remembers the last quaternion and
// returns the delta each call, so feed it the (decimated) orientation stream and
// send every non-zero result straight to the device. No serial/Qt-Quick deps, so
// it is unit-tested in isolation (tests/tst_twistcalculator.cpp).
class TwistCalculator
{
public:
    enum class FallbackMode { Global, Local };

    // headstageAxis: the direction the tether leaves the headstage, in the IMU's
    //   own reference frame (usually +Z). commutatorAxis: the direction the
    //   tether enters the rotating commutator element, in the global frame
    //   (usually +Z for an upright mount). Negating either axis flips the sign
    //   of the twist - the knob to turn if the commutator winds up instead of
    //   unwinding. fallbackThreshold / fallbackMode handle the algorithm's pole
    //   where the two axes are nearly opposed (see the Bonsai source).
    void configure(const QVector3D &headstageAxis, const QVector3D &commutatorAxis,
                   double fallbackThreshold, FallbackMode fallbackMode);

    // Forget the previous quaternion; the next update() re-seeds and returns 0.
    // Call whenever the orientation stream restarts (e.g. a device reconnect) so
    // the first post-gap sample doesn't emit a spurious catch-up turn.
    void reset() { m_hasPrev = false; }

    // Feed the next orientation sample; returns the number of turns to rotate the
    // commutator to cancel the twist since the previous sample. Returns 0 on the
    // first sample, on a zero-length (invalid) quaternion, or on a NaN result.
    double update(const QQuaternion &rotation);

private:
    QVector3D m_headstageAxis{0.0f, 0.0f, 1.0f};
    QVector3D m_commutatorAxis{0.0f, 0.0f, 1.0f};
    double m_fallbackThreshold = -0.9;
    FallbackMode m_fallbackMode = FallbackMode::Global;

    bool m_hasPrev = false;
    QQuaternion m_prev;
};

#endif // TWISTCALCULATOR_H
