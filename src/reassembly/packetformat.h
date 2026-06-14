#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace qfr {

struct Fragment
{
    quint8 streamId = 0;
    quint8 batchId = 0;
    quint8 fragmentCount = 0;
    quint8 fragmentIndex = 0;
    quint8 flags = 0;
    quint32 totalMessageLength = 0;
    quint32 messageCrc32 = 0;
    QByteArray payload;
};

enum class ParseError
{
    None,
    DatagramTooSmall,
    InvalidMagic,
    UnsupportedVersion,
    InvalidFragmentCount,
    InvalidFragmentIndex,
    PayloadLengthMismatch,
    MessageLengthOutOfRange
};

struct ParseResult
{
    Fragment fragment;
    ParseError error = ParseError::None;
    QString detail;

    bool ok() const { return error == ParseError::None; }
};

class PacketFormat
{
public:
    static constexpr int HeaderSize = 20;
    static constexpr quint8 Version = 1;
    static constexpr quint32 Magic = 0x51554652u; // ASCII: QUFR

    static ParseResult parse(const QByteArray &datagram,
                             quint32 maximumMessageBytes = 4u * 1024u * 1024u);

    static QByteArray encode(const Fragment &fragment);
};

QString parseErrorName(ParseError error);

} // namespace qfr
