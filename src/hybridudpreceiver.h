#pragma once

#include "npcapreceiver.h"

#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QVector>

class QUdpSocket;

class HybridUdpReceiver final : public QObject
{
    Q_OBJECT

public:
    enum class Source {
        QtUdp,
        Npcap
    };
    Q_ENUM(Source)

    struct Statistics {
        quint64 qtUdpPackets = 0;
        quint64 npcapPackets = 0;
        quint64 deliveredPackets = 0;
        quint64 crossPathDuplicates = 0;
        quint64 deliveredBytes = 0;
    };

    explicit HybridUdpReceiver(QObject *parent = nullptr);
    ~HybridUdpReceiver() override;

    bool start(const QHostAddress &bindAddress,
               quint16 firstPort,
               quint16 lastPort,
               bool enableNpcap,
               QString *statusMessage = nullptr);
    void stop();

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] Statistics statistics() const;

signals:
    void datagramReceived(const ParsedUdpDatagram &datagram,
                          HybridUdpReceiver::Source source,
                          const QString &sourceDetail);
    void statisticsChanged(const HybridUdpReceiver::Statistics &statistics);
    void statusChanged(const QString &message);
    void errorOccurred(const QString &message);

private:
    struct SeenByPath {
        qint64 qtUdpTimestampMs = -1;
        qint64 npcapTimestampMs = -1;
    };

    void receiveQtDatagrams(QUdpSocket *socket, quint16 destinationPort);
    void processDatagram(const ParsedUdpDatagram &datagram,
                         Source source,
                         const QString &sourceDetail);
    QByteArray fingerprint(const ParsedUdpDatagram &datagram) const;
    void pruneFingerprintCache(qint64 nowMs);
    void emitStatistics();

    QVector<QUdpSocket *> sockets;
    NpcapReceiver npcapReceiver;
    QElapsedTimer clock;
    QHash<QByteArray, SeenByPath> recentPackets;
    Statistics currentStatistics;
    bool running = false;
};

Q_DECLARE_METATYPE(HybridUdpReceiver::Statistics)
