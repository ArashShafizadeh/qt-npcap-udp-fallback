#include "demo/packetsimulator.h"
#include "reassembly/framereassembler.h"
#include "reassembly/packetformat.h"

#include <QSignalSpy>
#include <QtTest>
#include <algorithm>

class FrameReassemblerTest final : public QObject
{
    Q_OBJECT

private slots:
    void parsesEncodedFragment();
    void reassemblesOutOfOrderFragments();
    void ignoresIdenticalDuplicates();
    void supportsInterleavedBatches();
    void handlesBatchWrapAround();
    void reportsMissingBatch();
    void expiresIncompleteAssembly();
    void rejectsConflictingDuplicate();
};

void FrameReassemblerTest::parsesEncodedFragment()
{
    const QByteArray message("hello-world");
    const QList<QByteArray> datagrams = PacketSimulator::fragmentMessage(message, 7, 9, 5);
    QCOMPARE(datagrams.size(), 3);

    const qfr::ParseResult result = qfr::PacketFormat::parse(datagrams.first());
    QVERIFY(result.ok());
    QCOMPARE(result.fragment.streamId, quint8(7));
    QCOMPARE(result.fragment.batchId, quint8(9));
    QCOMPARE(result.fragment.fragmentCount, quint8(3));
    QCOMPARE(result.fragment.fragmentIndex, quint8(0));
    QCOMPARE(result.fragment.totalMessageLength, quint32(message.size()));
}

void FrameReassemblerTest::reassemblesOutOfOrderFragments()
{
    qfr::FrameReassembler reassembler;
    QSignalSpy completed(&reassembler, &qfr::FrameReassembler::messageReady);

    const QByteArray message(4096, 'A');
    QList<QByteArray> datagrams = PacketSimulator::fragmentMessage(message, 1, 1, 500);
    std::reverse(datagrams.begin(), datagrams.end());

    for (const QByteArray &datagram : datagrams) {
        reassembler.feedDatagram(datagram, 1000);
    }

    QCOMPARE(completed.count(), 1);
    QCOMPARE(completed.takeFirst().at(2).toByteArray(), message);
}

void FrameReassemblerTest::ignoresIdenticalDuplicates()
{
    qfr::FrameReassembler reassembler;
    QSignalSpy completed(&reassembler, &qfr::FrameReassembler::messageReady);

    const QByteArray message(2000, 'B');
    QList<QByteArray> datagrams = PacketSimulator::fragmentMessage(message, 2, 1, 400);
    datagrams.insert(2, datagrams.at(1));

    for (const QByteArray &datagram : datagrams) {
        reassembler.feedDatagram(datagram, 1000);
    }

    QCOMPARE(completed.count(), 1);
    QCOMPARE(reassembler.statistics().duplicateFragments, quint64(1));
}

void FrameReassemblerTest::supportsInterleavedBatches()
{
    qfr::FrameReassembler reassembler;
    QSignalSpy completed(&reassembler, &qfr::FrameReassembler::messageReady);

    const QByteArray first(1800, 'C');
    const QByteArray second(2200, 'D');
    const QList<QByteArray> firstPackets = PacketSimulator::fragmentMessage(first, 3, 10, 300);
    const QList<QByteArray> secondPackets = PacketSimulator::fragmentMessage(second, 3, 11, 300);

    const int maximum = std::max(firstPackets.size(), secondPackets.size());
    for (int index = 0; index < maximum; ++index) {
        if (index < firstPackets.size()) {
            reassembler.feedDatagram(firstPackets.at(index), 1000 + index);
        }
        if (index < secondPackets.size()) {
            reassembler.feedDatagram(secondPackets.at(index), 1000 + index);
        }
    }

    QCOMPARE(completed.count(), 2);
    QCOMPARE(completed.at(0).at(2).toByteArray(), first);
    QCOMPARE(completed.at(1).at(2).toByteArray(), second);
}

void FrameReassemblerTest::handlesBatchWrapAround()
{
    qfr::FrameReassembler reassembler;
    QSignalSpy completed(&reassembler, &qfr::FrameReassembler::messageReady);

    const QList<quint8> batches{quint8(254), quint8(255), quint8(0), quint8(1)};
    for (const quint8 batch : batches) {
        const QByteArray message = QByteArray("batch-") + QByteArray::number(batch);
        const QList<QByteArray> packets = PacketSimulator::fragmentMessage(message, 4, batch, 4);
        for (const QByteArray &packet : packets) {
            reassembler.feedDatagram(packet, 1000 + batch);
        }
    }

    QCOMPARE(completed.count(), 4);
    QCOMPARE(reassembler.statistics().lateBatchesDropped, quint64(0));
}

void FrameReassemblerTest::reportsMissingBatch()
{
    qfr::FrameReassembler reassembler;
    QSignalSpy missing(&reassembler, &qfr::FrameReassembler::missingBatchDetected);

    for (const quint8 batch : {quint8(20), quint8(22)}) {
        const QList<QByteArray> packets = PacketSimulator::fragmentMessage(QByteArray(100, 'E'), 5, batch, 30);
        for (const QByteArray &packet : packets) {
            reassembler.feedDatagram(packet, 1000 + batch);
        }
    }

    QCOMPARE(missing.count(), 1);
    QCOMPARE(static_cast<quint8>(missing.takeFirst().at(1).toUInt()), quint8(21));
}

void FrameReassemblerTest::expiresIncompleteAssembly()
{
    qfr::FrameReassembler::Limits limits;
    limits.assemblyTimeoutMs = 100;
    qfr::FrameReassembler reassembler(limits);

    QList<QByteArray> packets = PacketSimulator::fragmentMessage(QByteArray(1000, 'F'), 6, 1, 200);
    QVERIFY(packets.size() > 1);
    reassembler.feedDatagram(packets.first(), 1000);

    QCOMPARE(reassembler.purgeExpired(1101), 1);
    QCOMPARE(reassembler.statistics().expiredAssemblies, quint64(1));
}

void FrameReassemblerTest::rejectsConflictingDuplicate()
{
    qfr::FrameReassembler reassembler;
    QList<QByteArray> packets = PacketSimulator::fragmentMessage(QByteArray(1000, 'G'), 7, 1, 200);
    QVERIFY(packets.size() > 1);

    QVERIFY(reassembler.feedDatagram(packets.first(), 1000)
            == qfr::FrameReassembler::FeedResult::FragmentAccepted);

    qfr::ParseResult parsed = qfr::PacketFormat::parse(packets.first());
    QVERIFY(parsed.ok());
    parsed.fragment.payload[0] = static_cast<char>(parsed.fragment.payload.at(0) ^ 0x01);
    const QByteArray conflicting = qfr::PacketFormat::encode(parsed.fragment);

    QVERIFY(reassembler.feedDatagram(conflicting, 1001)
            == qfr::FrameReassembler::FeedResult::ConflictingFragmentRejected);
    QCOMPARE(reassembler.activeAssemblyCount(), 0);
}

QTEST_MAIN(FrameReassemblerTest)
#include "tst_framereassembler.moc"
