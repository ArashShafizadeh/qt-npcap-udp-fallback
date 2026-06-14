#pragma once

#include <QByteArray>
#include <QList>
#include <QtGlobal>

class PacketSimulator
{
public:
    static QList<QByteArray> fragmentMessage(const QByteArray &message,
                                             quint8 streamId,
                                             quint8 batchId,
                                             int maximumPayloadBytes = 512);
};
