#include "udppacketparser.h"

#include <QtEndian>

namespace {

constexpr quint16 kEtherTypeIpv4 = 0x0800;
constexpr quint16 kEtherTypeVlan = 0x8100;
constexpr quint16 kEtherTypeProviderBridge = 0x88A8;
constexpr quint16 kEtherTypeQinQ = 0x9100;
constexpr quint8 kIpv4Version = 4;
constexpr quint8 kProtocolUdp = 17;

quint16 readBe16(const uchar *data)
{
    return qFromBigEndian<quint16>(data);
}

quint32 readIpv4(const uchar *data)
{
    return (quint32(data[0]) << 24)
           | (quint32(data[1]) << 16)
           | (quint32(data[2]) << 8)
           | quint32(data[3]);
}

bool setError(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
    return false;
}

bool isAnyIpv4(const QHostAddress &address)
{
    return address.isNull()
           || address == QHostAddress::Any
           || address == QHostAddress::AnyIPv4;
}

} // namespace

bool UdpPacketParser::parse(const uchar *frame,
                            qsizetype capturedLength,
                            DataLinkType dataLinkType,
                            const Filter &filter,
                            ParsedUdpDatagram *result,
                            QString *errorMessage)
{
    if (result == nullptr) {
        return setError(errorMessage, QStringLiteral("Result pointer is null."));
    }
    *result = ParsedUdpDatagram{};

    if (frame == nullptr || capturedLength <= 0) {
        return setError(errorMessage, QStringLiteral("Captured frame is empty."));
    }

    qsizetype ipOffset = 0;

    switch (dataLinkType) {
    case DataLinkType::Ethernet: {
        constexpr qsizetype ethernetHeaderLength = 14;
        if (capturedLength < ethernetHeaderLength) {
            return setError(errorMessage, QStringLiteral("Ethernet header is truncated."));
        }

        quint16 etherType = readBe16(frame + 12);
        ipOffset = ethernetHeaderLength;

        while (etherType == kEtherTypeVlan
               || etherType == kEtherTypeProviderBridge
               || etherType == kEtherTypeQinQ) {
            constexpr qsizetype vlanTagLength = 4;
            if (capturedLength < ipOffset + vlanTagLength) {
                return setError(errorMessage, QStringLiteral("VLAN tag is truncated."));
            }
            etherType = readBe16(frame + ipOffset + 2);
            ipOffset += vlanTagLength;
        }

        if (etherType != kEtherTypeIpv4) {
            return setError(errorMessage, QStringLiteral("Frame is not IPv4."));
        }
        break;
    }
    case DataLinkType::RawIp:
        ipOffset = 0;
        break;
    case DataLinkType::NullLoopback:
        constexpr qsizetype nullHeaderLength = 4;
        if (capturedLength < nullHeaderLength) {
            return setError(errorMessage, QStringLiteral("DLT_NULL header is truncated."));
        }
        ipOffset = nullHeaderLength;
        break;
    }

    constexpr qsizetype minimumIpv4HeaderLength = 20;
    if (capturedLength < ipOffset + minimumIpv4HeaderLength) {
        return setError(errorMessage, QStringLiteral("IPv4 header is truncated."));
    }

    const uchar *ip = frame + ipOffset;
    const quint8 version = quint8((ip[0] >> 4) & 0x0F);
    const qsizetype ipHeaderLength = qsizetype(ip[0] & 0x0F) * 4;

    if (version != kIpv4Version) {
        return setError(errorMessage, QStringLiteral("Packet is not IPv4."));
    }
    if (ipHeaderLength < minimumIpv4HeaderLength
        || capturedLength < ipOffset + ipHeaderLength) {
        return setError(errorMessage, QStringLiteral("Invalid IPv4 header length."));
    }
    if (ip[9] != kProtocolUdp) {
        return setError(errorMessage, QStringLiteral("IPv4 payload is not UDP."));
    }

    const quint16 fragmentField = readBe16(ip + 6);
    constexpr quint16 moreFragmentsMask = 0x2000;
    constexpr quint16 fragmentOffsetMask = 0x1FFF;
    if ((fragmentField & moreFragmentsMask) != 0
        || (fragmentField & fragmentOffsetMask) != 0) {
        return setError(errorMessage, QStringLiteral("Fragmented IPv4 datagrams are not supported."));
    }

    const quint16 ipTotalLength = readBe16(ip + 2);
    if (ipTotalLength < ipHeaderLength + 8) {
        return setError(errorMessage, QStringLiteral("IPv4 total length is invalid."));
    }
    if (capturedLength < ipOffset + ipTotalLength) {
        return setError(errorMessage, QStringLiteral("Captured IPv4 packet is truncated."));
    }

    const QHostAddress sourceAddress(readIpv4(ip + 12));
    const QHostAddress destinationAddress(readIpv4(ip + 16));

    if (!isAnyIpv4(filter.destinationAddress)
        && destinationAddress != filter.destinationAddress) {
        return setError(errorMessage, QStringLiteral("Destination IPv4 address does not match the filter."));
    }

    const uchar *udp = ip + ipHeaderLength;
    const quint16 sourcePort = readBe16(udp);
    const quint16 destinationPort = readBe16(udp + 2);
    const quint16 udpLength = readBe16(udp + 4);

    const quint16 firstPort = qMin(filter.firstDestinationPort,
                                   filter.lastDestinationPort);
    const quint16 lastPort = qMax(filter.firstDestinationPort,
                                  filter.lastDestinationPort);
    if (destinationPort < firstPort || destinationPort > lastPort) {
        return setError(errorMessage, QStringLiteral("UDP destination port does not match the filter."));
    }
    if (udpLength < 8) {
        return setError(errorMessage, QStringLiteral("UDP length is invalid."));
    }
    if (qsizetype(udpLength) > qsizetype(ipTotalLength) - ipHeaderLength) {
        return setError(errorMessage, QStringLiteral("UDP length exceeds the IPv4 payload."));
    }

    const qsizetype payloadLength = qsizetype(udpLength) - 8;
    const char *payloadData = reinterpret_cast<const char *>(udp + 8);

    result->sourceAddress = sourceAddress;
    result->destinationAddress = destinationAddress;
    result->sourcePort = sourcePort;
    result->destinationPort = destinationPort;
    result->payload = QByteArray(payloadData, payloadLength);

    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
}
