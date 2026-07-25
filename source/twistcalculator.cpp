#include "twistcalculator.h"

#include <cmath>

namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;
}

void TwistCalculator::configure(const QVector3D &headstageAxis, const QVector3D &commutatorAxis,
                                double fallbackThreshold, FallbackMode fallbackMode)
{
    m_headstageAxis = headstageAxis;
    m_commutatorAxis = commutatorAxis;
    m_fallbackThreshold = fallbackThreshold;
    m_fallbackMode = fallbackMode;
}

double TwistCalculator::update(const QQuaternion &rotation)
{
    // A zero quaternion is the IMU's "no data" sentinel; ignore it.
    if (rotation.length() == 0.0f)
        return 0.0;

    const QQuaternion current = rotation.normalized();
    double twist = 0.0;

    if (m_hasPrev) {
        const QQuaternion last = m_prev;
        const QQuaternion conjugate = last.conjugated();

        // Incremental rotation from the last sample to the current one.
        const QQuaternion delta = (current * conjugate).normalized();

        // The headstage tether axis (local), rotated into the last sample's
        // global orientation: q * v * q^-1 with v the pure-vector quaternion.
        const QQuaternion localAxis(0.0f, m_headstageAxis);
        const QQuaternion projection = ((last * localAxis) * conjugate).normalized();

        const QVector3D deltaV = delta.vector();
        const QVector3D projectionV = projection.vector();

        // How much of the incremental rotation is about the tether axis
        // (expressed locally) vs. about the commutator's own axis (global).
        const double localDot = QVector3D::dotProduct(deltaV, projectionV);
        const double localTwist = 2.0 * std::atan2(localDot, static_cast<double>(delta.scalar()));

        const double globalDot = QVector3D::dotProduct(deltaV, m_commutatorAxis);
        const double globalTwist = 2.0 * std::atan2(globalDot, static_cast<double>(delta.scalar()));

        // Cosine of the angle between the projected tether axis and the
        // commutator axis (both unit vectors -> just the dot product). Near -1
        // the blended estimate below has a pole, so fall back there.
        const double cosAngle = QVector3D::dotProduct(projectionV, m_commutatorAxis);

        if (cosAngle > m_fallbackThreshold)
            twist = (localTwist + globalTwist) / (1.0 + cosAngle);
        else
            twist = (m_fallbackMode == FallbackMode::Global) ? globalTwist : localTwist;
    }

    m_prev = current;
    m_hasPrev = true;

    if (std::isnan(twist))
        return 0.0;
    // Negated: the commutator rotates to cancel the headstage twist. Radians
    // -> turns, the unit the {turn:x} command expects.
    return -twist / kTwoPi;
}
