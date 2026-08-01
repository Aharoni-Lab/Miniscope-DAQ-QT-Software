#include "miniscopeprotocol.h"

#include <cmath>

namespace MiniscopeProtocol {

PackedCommand packI2CPacket(const QVector<quint8> &packet)
{
    PackedCommand cmd;
    if (packet.isEmpty() || packet.length() > 6)
        return cmd;   // invalid: nothing to send / no wire format for > 6 bytes

    quint64 raw;
    if (packet.length() < 6) {
        // Short form: address, then the packet length, then the payload.
        raw = (quint64)packet[0];
        raw |= (((quint64)packet.length()) & 0xFF) << 8;
        for (int j = 1; j < packet.length(); j++)
            raw |= ((quint64)packet[j]) << (8 * (j + 1));
    } else {
        // Full form: 6 bytes leave no room for a length byte, so the address's
        // LSB (always 0 in a real I2C write address) is set to flag this form.
        raw = (quint64)packet[0] | 0x01;
        for (int j = 1; j < packet.length(); j++)
            raw |= ((quint64)packet[j]) << (8 * j);
    }

    cmd.words[0] = (quint16)(raw & 0xFFFF);
    cmd.words[1] = (quint16)((raw >> 16) & 0xFFFF);
    cmd.words[2] = (quint16)((raw >> 32) & 0xFFFF);
    cmd.valid = true;
    return cmd;
}

QVector<QVector<quint8>> serdesModePackets(double pixelClock)
{
    if (pixelClock <= 0)
        return {};
    if (pixelClock <= 50) {
        return { {0xC0, 0x1F, 0b00010000},    // DES: 12-bit low frequency
                 {0xB0, 0x05, 0b00100000} };  // SER
    }
    return { {0xC0, 0x1F, 0b00010001},        // DES: 10-bit high frequency
             {0xB0, 0x05, 0b00100001} };      // SER
}

void unpackBnoQuaternion(qint16 w, qint16 x, qint16 y, qint16 z, float *out5)
{
    const double dw = w, dx = x, dy = y, dz = z;
    const double norm = std::sqrt(dw * dw + dx * dx + dy * dy + dz * dz);
    out5[0] = dw / 16384.0;
    out5[1] = dx / 16384.0;
    out5[2] = dy / 16384.0;
    out5[3] = dz / 16384.0;
    out5[4] = std::abs((norm / 16384.0) - 1);
}

} // namespace MiniscopeProtocol
