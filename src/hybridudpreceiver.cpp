#include "hybridudpreceiver.h"

#include <QCryptographicHash>
#include <QNetworkDatagram>
#include <QUdpSocket>
#include <QStringList>

#include <utility>

namespace {

constexpr qint64 kDuplicateWindowMs = 100;
constexpr qint64 kCacheRetentionMs = 1000;
constexpr qsizetype kMaximumCacheEntries = 8192;
constexpr int kMaximumPortCount = 256;

} // namespace

HybridUdpReceiver::HybridUdpReceiver(QObject *parent)
    : QObject(parent)
    , npcapReceiver(this)
{
    qRegisterMetaType<Statistics>("HybridUdpReceiver::Statistics");

    connect(&npcapReceiver,
            &NpcapReceiver::datagramCaptured,
            this,
            [this](const ParsedUdpDatagram &datagram, const QString &adapterName) {
                processDatagram(datagram, Source::Npcap, adapterName);
            },
            Qt::QueuedConnection);

    connect(&npcapReceiver,
            &NpcapReceiver::captureError,
            this,
            &HybridUdpReceiver::errorOccurred,
            Qt::QueuedConnection);
}

HybridUdpReceiver::~HybridUdpReceiver()
{
    stop();
}

bool HybridUdpReceiver::start(const QHostAddress &bindAddress,
                              quint16 firstPort,
                              quint16 lastPort,
                              bool enableNpcap,
                              QString *statusMessage)
{
    stop();

    const quint16 normalizedFirstPort = qMin(firstPort, lastPort);
    const quint16 normalizedLastPort = qMax(firstPort, lastPort);
    const int portCount = int(normalizedLastPort) - int(normalizedFirstPort) + 1;

    if (portCount <= 0 || portCount > kMaximumPortCount) {
        const QString error = QStringLiteral("Choose between 1 and %1 UDP ports.")
                                  .arg(kMaximumPortCount);
        if (statusMessage != nullptr) {
            *statusMessage = error;
        }
        emit errorOccurred(error);
        return false;
    }

    QStringList bindErrors;
    for (int offset = 0; offset < portCount; ++offset) {
        const quint16 port = quint16(int(normalizedFirstPort) + offset);
        auto *socket = new QUdpSocket(this);
        const bool bound = socket->bind(bindAddress,
                                        port,
                                        QUdpSocket::ShareAddress
                                            | QUdpSocket::ReuseAddressHint);
        if (!bound) {
            bindErrors.append(QStringLiteral("%1: %2")
                                  .arg(port)
                                  .arg(socket->errorString()));
            socket->deleteLater();
            continue;
        }

        connect(socket,
                &QUdpSocket::readyRead,
                this,
                [this, socket, port]() {
                    receiveQtDatagrams(socket, port);
                });
        connect(socket,
                &QUdpSocket::errorOccurred,
                this,
                [this, port, socket](QAbstractSocket::SocketError) {
                    emit errorOccurred(QStringLiteral("UDP port %1: %2")
                                           .arg(port)
                                           .arg(socket->errorString()));
                });
        sockets.append(socket);
    }

    if (sockets.isEmpty()) {
        const QString error = QStringLiteral("No UDP port could be bound. %1")
                                  .arg(bindErrors.join(QStringLiteral(" | ")));
        if (statusMessage != nullptr) {
            *statusMessage = error;
        }
        emit errorOccurred(error);
        return false;
    }

    currentStatistics = Statistics{};
    recentPackets.clear();
    clock.start();
    running = true;

    QStringList statusParts;
    statusParts.append(QStringLiteral("Qt UDP listening on %1, ports %2-%3")
                           .arg(bindAddress.toString())
                           .arg(normalizedFirstPort)
                           .arg(normalizedLastPort));
    if (!bindErrors.isEmpty()) {
        statusParts.append(QStringLiteral("Bind warnings: %1")
                               .arg(bindErrors.join(QStringLiteral(" | "))));
    }

    if (enableNpcap) {
        QString npcapStatus;
        const bool npcapStarted = npcapReceiver.start(bindAddress,
                                                      normalizedFirstPort,
                                                      normalizedLastPort,
                                                      &npcapStatus);
        statusParts.append(npcapStatus);
        if (!npcapStarted) {
            statusParts.append(QStringLiteral("Running in standard UDP-only mode."));
        }
    } else {
        statusParts.append(QStringLiteral("Npcap fallback is disabled."));
    }

    const QString status = statusParts.join(QStringLiteral(" | "));
    if (statusMessage != nullptr) {
        *statusMessage = status;
    }
    emit statusChanged(status);
    emitStatistics();
    return true;
}

void HybridUdpReceiver::stop()
{
    npcapReceiver.stop();

    for (QUdpSocket *socket : std::as_const(sockets)) {
        socket->close();
        socket->deleteLater();
    }
    sockets.clear();
    recentPackets.clear();

    if (running) {
        running = false;
        emit statusChanged(QStringLiteral("Receiver stopped."));
    }
}

bool HybridUdpReceiver::isRunning() const
{
    return running;
}

HybridUdpReceiver::Statistics HybridUdpReceiver::statistics() const
{
    return currentStatistics;
}

void HybridUdpReceiver::receiveQtDatagrams(QUdpSocket *socket,
                                           quint16 destinationPort)
{
    while (socket->hasPendingDatagrams()) {
        const QNetworkDatagram networkDatagram = socket->receiveDatagram();
        if (!networkDatagram.isValid()) {
            continue;
        }

        ParsedUdpDatagram datagram;
        datagram.sourceAddress = networkDatagram.senderAddress();
        datagram.destinationAddress = networkDatagram.destinationAddress();
        datagram.sourcePort = networkDatagram.senderPort();
        datagram.destinationPort = destinationPort;
        datagram.payload = networkDatagram.data();
        processDatagram(datagram, Source::QtUdp, QStringLiteral("QUdpSocket"));
    }
}

void HybridUdpReceiver::processDatagram(const ParsedUdpDatagram &datagram,
                                        Source source,
                                        const QString &sourceDetail)
{
    if (!running) {
        return;
    }

    if (source == Source::QtUdp) {
        ++currentStatistics.qtUdpPackets;
    } else {
        ++currentStatistics.npcapPackets;
    }

    const qint64 nowMs = clock.elapsed();
    const QByteArray packetFingerprint = fingerprint(datagram);
    SeenByPath &seen = recentPackets[packetFingerprint];

    const qint64 oppositeTimestamp = source == Source::QtUdp
                                         ? seen.npcapTimestampMs
                                         : seen.qtUdpTimestampMs;
    const bool duplicateAcrossPaths = oppositeTimestamp >= 0
                                      && nowMs - oppositeTimestamp <= kDuplicateWindowMs;

    if (source == Source::QtUdp) {
        seen.qtUdpTimestampMs = nowMs;
    } else {
        seen.npcapTimestampMs = nowMs;
    }

    if (duplicateAcrossPaths) {
        ++currentStatistics.crossPathDuplicates;
        emitStatistics();
        return;
    }

    ++currentStatistics.deliveredPackets;
    currentStatistics.deliveredBytes += quint64(datagram.payload.size());
    emit datagramReceived(datagram, source, sourceDetail);
    emitStatistics();

    if (recentPackets.size() > kMaximumCacheEntries) {
        pruneFingerprintCache(nowMs);
    }
}

QByteArray HybridUdpReceiver::fingerprint(const ParsedUdpDatagram &datagram) const
{
    QByteArray input;
    input.reserve(64 + datagram.payload.size());
    input.append(datagram.sourceAddress.toString().toUtf8());
    input.append('\0');
    input.append(QByteArray::number(datagram.sourcePort));
    input.append(':');
    input.append(QByteArray::number(datagram.destinationPort));
    input.append('\0');
    input.append(datagram.payload);
    return QCryptographicHash::hash(input, QCryptographicHash::Sha256);
}

void HybridUdpReceiver::pruneFingerprintCache(qint64 nowMs)
{
    for (auto iterator = recentPackets.begin(); iterator != recentPackets.end();) {
        const qint64 newestTimestamp = qMax(iterator->qtUdpTimestampMs,
                                            iterator->npcapTimestampMs);
        if (newestTimestamp < 0 || nowMs - newestTimestamp > kCacheRetentionMs) {
            iterator = recentPackets.erase(iterator);
        } else {
            ++iterator;
        }
    }

    if (recentPackets.size() > kMaximumCacheEntries) {
        recentPackets.clear();
    }
}

void HybridUdpReceiver::emitStatistics()
{
    emit statisticsChanged(currentStatistics);
}
