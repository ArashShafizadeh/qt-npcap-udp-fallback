#include "reassembly/crc32.h"

namespace qfr {

quint32 crc32(const QByteArray &data)
{
    quint32 crc = 0xFFFFFFFFu;

    for (const char value : data) {
        crc ^= static_cast<quint8>(value);
        for (int bit = 0; bit < 8; ++bit) {
            const quint32 mask = static_cast<quint32>(-(static_cast<qint32>(crc & 1u)));
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }

    return ~crc;
}

} // namespace qfr
