#pragma once

#include "udppacketparser.h"

#include <QObject>

#include <memory>

class NpcapReceiverPrivate;

class NpcapReceiver final : public QObject
{
    Q_OBJECT

public:
    explicit NpcapReceiver(QObject *parent = nullptr);
    ~NpcapReceiver() override;

    NpcapReceiver(const NpcapReceiver &) = delete;
    NpcapReceiver &operator=(const NpcapReceiver &) = delete;

    bool start(const QHostAddress &destinationAddress,
               quint16 firstDestinationPort,
               quint16 lastDestinationPort,
               QString *statusMessage = nullptr);
    void stop();
    [[nodiscard]] bool isRunning() const;

signals:
    void datagramCaptured(const ParsedUdpDatagram &datagram,
                          const QString &adapterName);
    void captureError(const QString &message);

private:
    std::unique_ptr<NpcapReceiverPrivate> d;
};

Q_DECLARE_METATYPE(ParsedUdpDatagram)
