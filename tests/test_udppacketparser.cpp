#include "udppacketparser.h"

#include <QTest>
#include <QtEndian>

namespace {

void appendBe16(QByteArray &bytes, quint16 value)
{
    const quint16 bigEndian = qToBigEndian(value);
    bytes.append(reinterpret_cast<const char *>(&bigEndian), sizeof(bigEndian));
}

QByteArray makeEthernetUdpFrame(const QByteArray &payload,
                                quint16 destinationPort,
                                bool vlan = false,
                                bool fragmented = false)
{
    QByteArray frame;
    frame.append(QByteArray(6, char(0x11)));
    frame.append(QByteArray(6, char(0x22)));

    if (vlan) {
        appendBe16(frame, 0x8100);
        appendBe16(frame, 1);
    }
    appendBe16(frame, 0x0800);

    const quint16 ipLength = quint16(20 + 8 + payload.size());
    frame.append(char(0x45));
    frame.append(char(0));
    appendBe16(frame, ipLength);
    appendBe16(frame, 0x1234);
    appendBe16(frame, fragmented ? 0x2000 : 0x0000);
    frame.append(char(64));
    frame.append(char(17));
    appendBe16(frame, 0);
    frame.append(char(192));
    frame.append(char(168));
    frame.append(char(1));
    frame.append(char(10));
    frame.append(char(192));
    frame.append(char(168));
    frame.append(char(1));
    frame.append(char(20));

    appendBe16(frame, 42000);
    appendBe16(frame, destinationPort);
    appendBe16(frame, quint16(8 + payload.size()));
    appendBe16(frame, 0);
    frame.append(payload);
    return frame;
}

} // namespace

class TestUdpPacketParser final : public QObject
{
    Q_OBJECT

private slots:
    void parsesEthernetUdp();
    void parsesVlanUdp();
    void rejectsPortOutsideRange();
    void rejectsFragmentedIpv4();
};

void TestUdpPacketParser::parsesEthernetUdp()
{
    const QByteArray frame = makeEthernetUdpFrame(QByteArrayLiteral("hello"), 50000);
    UdpPacketParser::Filter filter;
    filter.destinationAddress = QHostAddress(QStringLiteral("192.168.1.20"));
    filter.firstDestinationPort = 50000;
    filter.lastDestinationPort = 50000;

    ParsedUdpDatagram datagram;
    QVERIFY(UdpPacketParser::parse(reinterpret_cast<const uchar *>(frame.constData()),
                                   frame.size(),
                                   UdpPacketParser::DataLinkType::Ethernet,
                                   filter,
                                   &datagram));
    QCOMPARE(datagram.sourceAddress, QHostAddress(QStringLiteral("192.168.1.10")));
    QCOMPARE(datagram.destinationAddress, QHostAddress(QStringLiteral("192.168.1.20")));
    QCOMPARE(datagram.sourcePort, quint16(42000));
    QCOMPARE(datagram.destinationPort, quint16(50000));
    QCOMPARE(datagram.payload, QByteArrayLiteral("hello"));
}

void TestUdpPacketParser::parsesVlanUdp()
{
    const QByteArray frame = makeEthernetUdpFrame(QByteArrayLiteral("vlan"), 50001, true);
    UdpPacketParser::Filter filter;
    filter.firstDestinationPort = 50000;
    filter.lastDestinationPort = 50010;

    ParsedUdpDatagram datagram;
    QVERIFY(UdpPacketParser::parse(reinterpret_cast<const uchar *>(frame.constData()),
                                   frame.size(),
                                   UdpPacketParser::DataLinkType::Ethernet,
                                   filter,
                                   &datagram));
    QCOMPARE(datagram.payload, QByteArrayLiteral("vlan"));
}

void TestUdpPacketParser::rejectsPortOutsideRange()
{
    const QByteArray frame = makeEthernetUdpFrame(QByteArrayLiteral("ignored"), 51000);
    UdpPacketParser::Filter filter;
    filter.firstDestinationPort = 50000;
    filter.lastDestinationPort = 50010;

    ParsedUdpDatagram datagram;
    QVERIFY(!UdpPacketParser::parse(reinterpret_cast<const uchar *>(frame.constData()),
                                    frame.size(),
                                    UdpPacketParser::DataLinkType::Ethernet,
                                    filter,
                                    &datagram));
}

void TestUdpPacketParser::rejectsFragmentedIpv4()
{
    const QByteArray frame = makeEthernetUdpFrame(QByteArrayLiteral("fragment"),
                                                   50000,
                                                   false,
                                                   true);
    UdpPacketParser::Filter filter;
    filter.firstDestinationPort = 50000;
    filter.lastDestinationPort = 50000;

    ParsedUdpDatagram datagram;
    QVERIFY(!UdpPacketParser::parse(reinterpret_cast<const uchar *>(frame.constData()),
                                    frame.size(),
                                    UdpPacketParser::DataLinkType::Ethernet,
                                    filter,
                                    &datagram));
}

QTEST_MAIN(TestUdpPacketParser)
#include "test_udppacketparser.moc"
