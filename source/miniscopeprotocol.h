#ifndef MINISCOPEPROTOCOL_H
#define MINISCOPEPROTOCOL_H

#include <QVector>
#include <QtGlobal>

// Pure helpers for the Miniscope DAQ's I2C-over-UVC control protocol, shared by
// every capture backend (OpenCV property smuggling, libuvc, and future ones).
// The DAQ firmware tunnels host<->scope traffic through standard UVC
// Processing-Unit controls, so the byte packing here must match the firmware
// exactly; keeping it in one tested place stops the backends from drifting.
namespace MiniscopeProtocol {

// An I2C command packed for transport. The firmware receives it as three
// consecutive 16-bit UVC control writes (CONTRAST, GAMMA, SHARPNESS carry the
// low, middle and high words of `raw` respectively).
//
// Wire format ('packet' is I2C address followed by payload bytes):
//   < 6 bytes: byte0 = address, byte1 = total packet length, bytes2.. = payload
//   = 6 bytes: byte0 = address with its LSB forced to 1 (flags the length-less
//              full-width form), bytes1..5 = payload
//   > 6 bytes: unsupported (valid == false); the firmware has no framing for it
struct PackedCommand {
    quint64 raw = 0;
    quint16 words[3] = {0, 0, 0};   // CONTRAST, GAMMA, SHARPNESS payloads
    bool valid = false;
};

PackedCommand packI2CPacket(const QVector<quint8> &packet);

// Unpack the BNO085 head-orientation quaternion the DAQ streams back through
// UVC control reads. Inputs are the raw signed 16-bit register values (unit
// quaternion scaled by 2^14); out5 receives {w, x, y, z, normError} where
// normError is |norm - 1| — the caller uses it to reject corrupted reads.
void unpackBnoQuaternion(qint16 w, qint16 x, qint16 y, qint16 z, float *out5);

} // namespace MiniscopeProtocol

#endif // MINISCOPEPROTOCOL_H
