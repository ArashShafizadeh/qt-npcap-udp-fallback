#include "reassembly/framereassembler.h"

#include "reassembly/crc32.h"

#include <QDateTime>
#include <QStringList>
#include <algorithm>
#include <limits>

namespace qfr {

FrameReassembler::FrameReassembler(QObject *parent)
    : FrameReassembler(Limits{}, parent)
{
}

FrameReassembler::FrameReassembler(const Limits &limits, QObject *parent)
    : QObject(parent)
    , m_limits(limits)
{
    setLimits(limits);
}

void FrameReassembler::setLimits(const Limits &limits)
{
    m_limits.assemblyTimeoutMs = std::max(1, limits.assemblyTimeoutMs);
    m_limits.maximumActiveAssemblies = std::max(1, limits.maximumActiveAssemblies);
    m_limits.maximumMessageBytes = std::max<quint32>(1u, limits.maximumMessageBytes);
}

quint16 FrameReassembler::assemblyKey(quint8 streamId, quint8 batchId)
{
    return static_cast<quint16>((static_cast<quint16>(streamId) << 8u) | batchId);
}

quint8 FrameReassembler::forwardDistance(quint8 newer, quint8 older)
{
    return static_cast<quint8>(newer - older);
}

bool FrameReassembler::isNewer(quint8 candidate, quint8 reference)
{
    const quint8 distance = forwardDistance(candidate, reference);
    return distance > 0 && distance < 128;
}

qint64 FrameReassembler::currentTimeMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

bool FrameReassembler::isLateOrPublished(quint8 streamId, quint8 batchId) const
{
    const auto iterator = m_streamOrder.constFind(static_cast<int>(streamId));
    if (iterator == m_streamOrder.cend() || !iterator->synchronized) {
        return false;
    }

    if (batchId == iterator->lastPublishedBatch) {
        return true;
    }

    return !isNewer(batchId, iterator->lastPublishedBatch);
}

void FrameReassembler::notifyStatisticsChanged()
{
    emit statisticsChanged();
}

int FrameReassembler::purgeExpired(qint64 nowMs)
{
    if (nowMs < 0) {
        nowMs = currentTimeMs();
    }

    QVector<quint16> expiredKeys;
    expiredKeys.reserve(m_assemblies.size());

    for (auto iterator = m_assemblies.cbegin(); iterator != m_assemblies.cend(); ++iterator) {
        if ((nowMs - iterator->lastSeenMs) >= m_limits.assemblyTimeoutMs) {
            expiredKeys.push_back(iterator.key());
        }
    }

    for (const quint16 key : expiredKeys) {
        const Assembly assembly = m_assemblies.take(key);
        ++m_statistics.expiredAssemblies;
        emit eventOccurred(QStringLiteral("Expired incomplete stream=%1 batch=%2 (%3/%4 fragments)")
                               .arg(assembly.streamId)
                               .arg(assembly.batchId)
                               .arg(assembly.receivedCount)
                               .arg(assembly.fragmentCount));
    }

    if (!expiredKeys.isEmpty()) {
        notifyStatisticsChanged();
    }

    return expiredKeys.size();
}

void FrameReassembler::evictOldestAssembly()
{
    if (m_assemblies.isEmpty()) {
        return;
    }

    quint16 oldestKey = 0;
    qint64 oldestTimestamp = std::numeric_limits<qint64>::max();

    for (auto iterator = m_assemblies.cbegin(); iterator != m_assemblies.cend(); ++iterator) {
        if (iterator->lastSeenMs < oldestTimestamp) {
            oldestTimestamp = iterator->lastSeenMs;
            oldestKey = iterator.key();
        }
    }

    const Assembly evicted = m_assemblies.take(oldestKey);
    ++m_statistics.capacityEvictions;
    emit eventOccurred(QStringLiteral("Evicted oldest incomplete stream=%1 batch=%2 to enforce capacity")
                           .arg(evicted.streamId)
                           .arg(evicted.batchId));
}

void FrameReassembler::reportMissingBatches(quint8 streamId, quint8 newestBatch)
{
    StreamOrder &order = m_streamOrder[static_cast<int>(streamId)];
    if (!order.synchronized) {
        return;
    }

    const quint8 distance = forwardDistance(newestBatch, order.lastPublishedBatch);
    if (distance <= 1 || distance >= 128) {
        return;
    }

    quint8 missing = static_cast<quint8>(order.lastPublishedBatch + 1u);
    for (int guard = 0; guard < 127 && missing != newestBatch; ++guard) {
        ++m_statistics.missingBatches;
        emit missingBatchDetected(streamId, missing);
        emit eventOccurred(QStringLiteral("Missing stream=%1 batch=%2")
                               .arg(streamId)
                               .arg(missing));
        missing = static_cast<quint8>(missing + 1u);
    }
}

FrameReassembler::FeedResult FrameReassembler::finishAssembly(quint16 key)
{
    const Assembly assembly = m_assemblies.take(key);

    QByteArray message;
    message.reserve(static_cast<int>(assembly.totalMessageLength));
    for (const QByteArray &part : assembly.parts) {
        message.append(part);
    }

    if (static_cast<quint32>(message.size()) != assembly.totalMessageLength) {
        ++m_statistics.lengthFailures;
        emit eventOccurred(QStringLiteral("Rejected stream=%1 batch=%2: reassembled length %3, expected %4")
                               .arg(assembly.streamId)
                               .arg(assembly.batchId)
                               .arg(message.size())
                               .arg(assembly.totalMessageLength));
        notifyStatisticsChanged();
        return FeedResult::MessageIntegrityRejected;
    }

    if (crc32(message) != assembly.messageCrc32) {
        ++m_statistics.crcFailures;
        emit eventOccurred(QStringLiteral("Rejected stream=%1 batch=%2: CRC32 mismatch")
                               .arg(assembly.streamId)
                               .arg(assembly.batchId));
        notifyStatisticsChanged();
        return FeedResult::MessageIntegrityRejected;
    }

    if (isLateOrPublished(assembly.streamId, assembly.batchId)) {
        ++m_statistics.lateBatchesDropped;
        emit eventOccurred(QStringLiteral("Dropped late/duplicate completed stream=%1 batch=%2")
                               .arg(assembly.streamId)
                               .arg(assembly.batchId));
        notifyStatisticsChanged();
        return FeedResult::LateBatchDropped;
    }

    StreamOrder &order = m_streamOrder[static_cast<int>(assembly.streamId)];
    if (order.synchronized) {
        reportMissingBatches(assembly.streamId, assembly.batchId);
    }

    order.synchronized = true;
    order.lastPublishedBatch = assembly.batchId;
    ++m_statistics.completedMessages;

    emit messageReady(assembly.streamId, assembly.batchId, message);
    emit eventOccurred(QStringLiteral("Completed stream=%1 batch=%2 bytes=%3")
                           .arg(assembly.streamId)
                           .arg(assembly.batchId)
                           .arg(message.size()));
    notifyStatisticsChanged();
    return FeedResult::MessageCompleted;
}

FrameReassembler::FeedResult FrameReassembler::feedDatagram(const QByteArray &datagram, qint64 nowMs)
{
    if (nowMs < 0) {
        nowMs = currentTimeMs();
    }

    ++m_statistics.datagramsReceived;
    purgeExpired(nowMs);

    const ParseResult parsed = PacketFormat::parse(datagram, m_limits.maximumMessageBytes);
    if (!parsed.ok()) {
        ++m_statistics.malformedDatagrams;
        emit eventOccurred(QStringLiteral("Rejected malformed datagram [%1]: %2")
                               .arg(parseErrorName(parsed.error), parsed.detail));
        notifyStatisticsChanged();
        return FeedResult::MalformedDatagramRejected;
    }

    const Fragment &fragment = parsed.fragment;
    if (isLateOrPublished(fragment.streamId, fragment.batchId)) {
        ++m_statistics.lateBatchesDropped;
        emit eventOccurred(QStringLiteral("Dropped late packet stream=%1 batch=%2 fragment=%3")
                               .arg(fragment.streamId)
                               .arg(fragment.batchId)
                               .arg(fragment.fragmentIndex));
        notifyStatisticsChanged();
        return FeedResult::LateBatchDropped;
    }

    const quint16 key = assemblyKey(fragment.streamId, fragment.batchId);
    auto iterator = m_assemblies.find(key);

    if (iterator == m_assemblies.end()) {
        if (m_assemblies.size() >= m_limits.maximumActiveAssemblies) {
            evictOldestAssembly();
        }

        Assembly assembly;
        assembly.streamId = fragment.streamId;
        assembly.batchId = fragment.batchId;
        assembly.fragmentCount = fragment.fragmentCount;
        assembly.flags = fragment.flags;
        assembly.totalMessageLength = fragment.totalMessageLength;
        assembly.messageCrc32 = fragment.messageCrc32;
        assembly.parts.resize(fragment.fragmentCount);
        assembly.received.resize(fragment.fragmentCount);
        std::fill(assembly.received.begin(), assembly.received.end(), false);
        assembly.firstSeenMs = nowMs;
        assembly.lastSeenMs = nowMs;

        iterator = m_assemblies.insert(key, assembly);
    }

    Assembly &assembly = iterator.value();
    const bool metadataMatches =
        assembly.fragmentCount == fragment.fragmentCount
        && assembly.flags == fragment.flags
        && assembly.totalMessageLength == fragment.totalMessageLength
        && assembly.messageCrc32 == fragment.messageCrc32;

    if (!metadataMatches) {
        ++m_statistics.conflictingFragments;
        m_assemblies.erase(iterator);
        emit eventOccurred(QStringLiteral("Rejected conflicting metadata for stream=%1 batch=%2")
                               .arg(fragment.streamId)
                               .arg(fragment.batchId));
        notifyStatisticsChanged();
        return FeedResult::ConflictingFragmentRejected;
    }

    const int index = fragment.fragmentIndex;
    assembly.lastSeenMs = nowMs;

    if (assembly.received[index]) {
        if (assembly.parts[index] == fragment.payload) {
            ++m_statistics.duplicateFragments;
            emit eventOccurred(QStringLiteral("Ignored duplicate stream=%1 batch=%2 fragment=%3")
                                   .arg(fragment.streamId)
                                   .arg(fragment.batchId)
                                   .arg(index));
            notifyStatisticsChanged();
            return FeedResult::DuplicateFragmentIgnored;
        }

        ++m_statistics.conflictingFragments;
        m_assemblies.erase(iterator);
        emit eventOccurred(QStringLiteral("Rejected conflicting duplicate stream=%1 batch=%2 fragment=%3")
                               .arg(fragment.streamId)
                               .arg(fragment.batchId)
                               .arg(index));
        notifyStatisticsChanged();
        return FeedResult::ConflictingFragmentRejected;
    }

    if (assembly.receivedBytes + static_cast<quint64>(fragment.payload.size())
        > m_limits.maximumMessageBytes) {
        ++m_statistics.lengthFailures;
        m_assemblies.erase(iterator);
        emit eventOccurred(QStringLiteral("Rejected stream=%1 batch=%2: buffered bytes exceed limit")
                               .arg(fragment.streamId)
                               .arg(fragment.batchId));
        notifyStatisticsChanged();
        return FeedResult::MessageIntegrityRejected;
    }

    assembly.received[index] = true;
    assembly.parts[index] = fragment.payload;
    ++assembly.receivedCount;
    assembly.receivedBytes += static_cast<quint64>(fragment.payload.size());
    ++m_statistics.fragmentsAccepted;

    if (assembly.receivedCount == assembly.fragmentCount) {
        return finishAssembly(key);
    }

    notifyStatisticsChanged();
    return FeedResult::FragmentAccepted;
}

void FrameReassembler::reset()
{
    m_assemblies.clear();
    m_streamOrder.clear();
    m_statistics = Statistics{};
    emit eventOccurred(QStringLiteral("Reassembler reset"));
    notifyStatisticsChanged();
}

} // namespace qfr
