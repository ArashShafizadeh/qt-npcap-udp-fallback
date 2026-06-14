#include "reassembly/packetformat.h"

#include <QtEndian>

namespace qfr {
namespace {

quint16 readBe16(const char *data)
{
    return qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(data));
}

quint32 readBe32(const char *data)
{
    return qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(data));
}

void appendBe16(QByteArray &output, quint16 value)
{
    const quint16 encoded = qToBigEndian(value);
    output.append(reinterpret_cast<const char *>(&encoded), sizeof(encoded));
}

void appendBe32(QByteArray &output, quint32 value)
{
    const quint32 encoded = qToBigEndian(value);
    output.append(reinterpret_cast<const char *>(&encoded), sizeof(encoded));
}

ParseResult failure(ParseError error, const QString &detail)
{
    ParseResult result;
    result.error = error;
    result.detail = detail;
    return result;
}

} // namespace

ParseResult PacketFormat::parse(const QByteArray &datagram, quint32 maximumMessageBytes)
{
    if (datagram.size() < HeaderSize) {
        return failure(ParseError::DatagramTooSmall,
                       QStringLiteral("Datagram has %1 bytes; at least %2 are required")
                           .arg(datagram.size())
                           .arg(HeaderSize));
    }

    const char *raw = datagram.constData();
    const quint32 magic = readBe32(raw);
    if (magic != Magic) {
        return failure(ParseError::InvalidMagic, QStringLiteral("Unexpected protocol magic"));
    }

    const quint8 version = static_cast<quint8>(raw[4]);
    if (version != Version) {
        return failure(ParseError::UnsupportedVersion,
                       QStringLiteral("Unsupported protocol version %1").arg(version));
    }

    Fragment fragment;
    fragment.streamId = static_cast<quint8>(raw[5]);
    fragment.batchId = static_cast<quint8>(raw[6]);
    fragment.fragmentCount = static_cast<quint8>(raw[7]);
    fragment.fragmentIndex = static_cast<quint8>(raw[8]);
    fragment.flags = static_cast<quint8>(raw[9]);

    const quint16 payloadLength = readBe16(raw + 10);
    fragment.totalMessageLength = readBe32(raw + 12);
    fragment.messageCrc32 = readBe32(raw + 16);

    if (fragment.fragmentCount == 0) {
        return failure(ParseError::InvalidFragmentCount,
                       QStringLiteral("fragmentCount must be greater than zero"));
    }

    if (fragment.fragmentIndex >= fragment.fragmentCount) {
        return failure(ParseError::InvalidFragmentIndex,
                       QStringLiteral("fragmentIndex %1 is outside fragmentCount %2")
                           .arg(fragment.fragmentIndex)
                           .arg(fragment.fragmentCount));
    }

    const int actualPayloadLength = datagram.size() - HeaderSize;
    if (actualPayloadLength != payloadLength) {
        return failure(ParseError::PayloadLengthMismatch,
                       QStringLiteral("Header declares %1 payload bytes but datagram contains %2")
                           .arg(payloadLength)
                           .arg(actualPayloadLength));
    }

    if (fragment.totalMessageLength == 0 || fragment.totalMessageLength > maximumMessageBytes) {
        return failure(ParseError::MessageLengthOutOfRange,
                       QStringLiteral("Total message length %1 exceeds configured limits")
                           .arg(fragment.totalMessageLength));
    }

    fragment.payload = datagram.mid(HeaderSize, payloadLength);

    ParseResult result;
    result.fragment = fragment;
    return result;
}

QByteArray PacketFormat::encode(const Fragment &fragment)
{
    if (fragment.fragmentCount == 0 || fragment.fragmentIndex >= fragment.fragmentCount) {
        return {};
    }

    if (fragment.payload.size() > 0xFFFF) {
        return {};
    }

    QByteArray output;
    output.reserve(HeaderSize + fragment.payload.size());

    appendBe32(output, Magic);
    output.append(static_cast<char>(Version));
    output.append(static_cast<char>(fragment.streamId));
    output.append(static_cast<char>(fragment.batchId));
    output.append(static_cast<char>(fragment.fragmentCount));
    output.append(static_cast<char>(fragment.fragmentIndex));
    output.append(static_cast<char>(fragment.flags));
    appendBe16(output, static_cast<quint16>(fragment.payload.size()));
    appendBe32(output, fragment.totalMessageLength);
    appendBe32(output, fragment.messageCrc32);
    output.append(fragment.payload);

    return output;
}

QString parseErrorName(ParseError error)
{
    switch (error) {
    case ParseError::None:
        return QStringLiteral("none");
    case ParseError::DatagramTooSmall:
        return QStringLiteral("datagram-too-small");
    case ParseError::InvalidMagic:
        return QStringLiteral("invalid-magic");
    case ParseError::UnsupportedVersion:
        return QStringLiteral("unsupported-version");
    case ParseError::InvalidFragmentCount:
        return QStringLiteral("invalid-fragment-count");
    case ParseError::InvalidFragmentIndex:
        return QStringLiteral("invalid-fragment-index");
    case ParseError::PayloadLengthMismatch:
        return QStringLiteral("payload-length-mismatch");
    case ParseError::MessageLengthOutOfRange:
        return QStringLiteral("message-length-out-of-range");
    }

    return QStringLiteral("unknown");
}

} // namespace qfr
