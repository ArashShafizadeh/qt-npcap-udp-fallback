#include "demo/packetsimulator.h"

#include "reassembly/crc32.h"
#include "reassembly/packetformat.h"

#include <algorithm>

QList<QByteArray> PacketSimulator::fragmentMessage(const QByteArray &message,
                                                    quint8 streamId,
                                                    quint8 batchId,
                                                    int maximumPayloadBytes)
{
    QList<QByteArray> datagrams;
    if (message.isEmpty() || maximumPayloadBytes <= 0) {
        return datagrams;
    }

    const int count = (message.size() + maximumPayloadBytes - 1) / maximumPayloadBytes;
    if (count <= 0 || count > 255) {
        return datagrams;
    }

    const quint32 checksum = qfr::crc32(message);
    for (int index = 0; index < count; ++index) {
        qfr::Fragment fragment;
        fragment.streamId = streamId;
        fragment.batchId = batchId;
        fragment.fragmentCount = static_cast<quint8>(count);
        fragment.fragmentIndex = static_cast<quint8>(index);
        fragment.totalMessageLength = static_cast<quint32>(message.size());
        fragment.messageCrc32 = checksum;
        fragment.payload = message.mid(index * maximumPayloadBytes, maximumPayloadBytes);
        datagrams.push_back(qfr::PacketFormat::encode(fragment));
    }

    return datagrams;
}
