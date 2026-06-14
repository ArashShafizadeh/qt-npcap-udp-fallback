#pragma once

#include "reassembly/packetformat.h"

#include <QHash>
#include <QObject>
#include <QVector>

namespace qfr {

class FrameReassembler final : public QObject
{
    Q_OBJECT

public:
    struct Limits
    {
        int assemblyTimeoutMs = 2000;
        int maximumActiveAssemblies = 64;
        quint32 maximumMessageBytes = 4u * 1024u * 1024u;
    };

    struct Statistics
    {
        quint64 datagramsReceived = 0;
        quint64 malformedDatagrams = 0;
        quint64 fragmentsAccepted = 0;
        quint64 duplicateFragments = 0;
        quint64 conflictingFragments = 0;
        quint64 completedMessages = 0;
        quint64 crcFailures = 0;
        quint64 lengthFailures = 0;
        quint64 expiredAssemblies = 0;
        quint64 capacityEvictions = 0;
        quint64 lateBatchesDropped = 0;
        quint64 missingBatches = 0;
    };

    enum class FeedResult
    {
        FragmentAccepted,
        MessageCompleted,
        DuplicateFragmentIgnored,
        LateBatchDropped,
        MalformedDatagramRejected,
        ConflictingFragmentRejected,
        MessageIntegrityRejected
    };
    Q_ENUM(FeedResult)

    explicit FrameReassembler(QObject *parent = nullptr);
    explicit FrameReassembler(const Limits &limits, QObject *parent = nullptr);

    FeedResult feedDatagram(const QByteArray &datagram, qint64 nowMs = -1);
    int purgeExpired(qint64 nowMs = -1);
    void reset();

    Limits limits() const { return m_limits; }
    void setLimits(const Limits &limits);
    Statistics statistics() const { return m_statistics; }
    int activeAssemblyCount() const { return m_assemblies.size(); }

signals:
    void messageReady(quint8 streamId, quint8 batchId, const QByteArray &message);
    void missingBatchDetected(quint8 streamId, quint8 batchId);
    void eventOccurred(const QString &message);
    void statisticsChanged();

private:
    struct Assembly
    {
        quint8 streamId = 0;
        quint8 batchId = 0;
        quint8 fragmentCount = 0;
        quint8 flags = 0;
        quint32 totalMessageLength = 0;
        quint32 messageCrc32 = 0;
        QVector<QByteArray> parts;
        QVector<bool> received;
        int receivedCount = 0;
        quint64 receivedBytes = 0;
        qint64 firstSeenMs = 0;
        qint64 lastSeenMs = 0;
    };

    struct StreamOrder
    {
        bool synchronized = false;
        quint8 lastPublishedBatch = 0;
    };

    static quint16 assemblyKey(quint8 streamId, quint8 batchId);
    static quint8 forwardDistance(quint8 newer, quint8 older);
    static bool isNewer(quint8 candidate, quint8 reference);
    static qint64 currentTimeMs();

    bool isLateOrPublished(quint8 streamId, quint8 batchId) const;
    void reportMissingBatches(quint8 streamId, quint8 newestBatch);
    void evictOldestAssembly();
    FeedResult finishAssembly(quint16 key);
    void notifyStatisticsChanged();

    Limits m_limits;
    Statistics m_statistics;
    QHash<quint16, Assembly> m_assemblies;
    QHash<int, StreamOrder> m_streamOrder;
};

} // namespace qfr
