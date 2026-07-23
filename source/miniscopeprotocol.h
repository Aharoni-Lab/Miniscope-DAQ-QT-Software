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

// --- Miniscope DAQ USB identity + UVC control map ----------------------------
// Shared by every backend that talks to the device directly (libuvc on Linux,
// IOKit on macOS) so the descriptor facts can't drift between platforms.
constexpr quint16 kUsbVendorId  = 0x04b4;   // Cypress FX3
constexpr quint16 kUsbProductId = 0x00f9;   // Miniscope DAQ
constexpr quint8  kProcessingUnitId = 2;    // from the DAQ's UVC descriptor

// UVC Processing-Unit control selectors the DAQ firmware overloads.
enum PuSelector : quint8 {
    SEL_BRIGHTNESS = 0x02, // BNO quaternion z
    SEL_CONTRAST   = 0x03, // I2C low word (write) / DAQ frame number (read)
    SEL_GAIN       = 0x04, // BNO quaternion y
    SEL_HUE        = 0x06, // BNO quaternion x
    SEL_SATURATION = 0x07, // data-stream "start" (write) / BNO quaternion w (read)
    SEL_SHARPNESS  = 0x08, // I2C high word
    SEL_GAMMA      = 0x09  // I2C mid word (write) / external trigger state (read)
};

// Which selector carries each packed I2C word (see PackedCommand below), and
// which selectors the BNO quaternion components are read from, in w/x/y/z
// order. Backends iterate these instead of re-stating the map.
constexpr quint8 kI2CWordSelectors[3] = {SEL_CONTRAST, SEL_GAMMA, SEL_SHARPNESS};
constexpr quint8 kBnoSelectors[4] = {SEL_SATURATION, SEL_HUE, SEL_GAIN, SEL_BRIGHTNESS};

// Settle time after a control write: the DAQ's control endpoint is slow to
// clear, and a follow-up command sent too soon overwrites the previous one
// (breaking the packet layout). >100 us is generally enough in practice.
constexpr int kCtrlSettleUs = 200;

// An I2C command packed for transport. The firmware receives it as three
// consecutive 16-bit UVC control writes; words[i] travels on the control
// named by kI2CWordSelectors[i] (low word first).
//
// Wire format ('packet' is I2C address followed by payload bytes):
//   < 6 bytes: byte0 = address, byte1 = total packet length, bytes2.. = payload
//   = 6 bytes: byte0 = address with its LSB forced to 1 (flags the length-less
//              full-width form), bytes1..5 = payload
//   > 6 bytes: unsupported (valid == false); the firmware has no framing for it
struct PackedCommand {
    quint16 words[3] = {0, 0, 0};
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
