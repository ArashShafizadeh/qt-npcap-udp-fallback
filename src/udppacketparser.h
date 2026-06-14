#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QString>
#include <QtGlobal>

struct ParsedUdpDatagram
{
    QHostAddress sourceAddress;
    QHostAddress destinationAddress;
    quint16 sourcePort = 0;
    quint16 destinationPort = 0;
    QByteArray payload;
};

class UdpPacketParser final
{
public:
    enum class DataLinkType {
        NullLoopback = 0,
        Ethernet = 1,
        RawIp = 12
    };

    struct Filter {
        QHostAddress destinationAddress = QHostAddress::AnyIPv4;
        quint16 firstDestinationPort = 0;
        quint16 lastDestinationPort = 65535;
    };

    static bool parse(const uchar *frame,
                      qsizetype capturedLength,
                      DataLinkType dataLinkType,
                      const Filter &filter,
                      ParsedUdpDatagram *result,
                      QString *errorMessage = nullptr);

private:
    UdpPacketParser() = delete;
};
