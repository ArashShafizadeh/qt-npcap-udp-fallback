#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#endif

#include "npcapreceiver.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibrary>
#include <QStringList>

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32

namespace {

struct PcapHandle;

struct PcapAddress
{
    PcapAddress *next;
    sockaddr *addr;
    sockaddr *netmask;
    sockaddr *broadaddr;
    sockaddr *dstaddr;
};

struct PcapInterface
{
    PcapInterface *next;
    char *name;
    char *description;
    PcapAddress *addresses;
    unsigned int flags;
};

struct PcapTimeval
{
    long tv_sec;
    long tv_usec;
};

struct PcapPacketHeader
{
    PcapTimeval ts;
    unsigned int caplen;
    unsigned int len;
};

struct BpfProgram
{
    unsigned int bf_len;
    void *bf_insns;
};

using PcapFindAllDevsFn = int (*)(PcapInterface **, char *);
using PcapFreeAllDevsFn = void (*)(PcapInterface *);
using PcapOpenLiveFn = PcapHandle *(*)(const char *, int, int, int, char *);
using PcapCloseFn = void (*)(PcapHandle *);
using PcapNextExFn = int (*)(PcapHandle *, PcapPacketHeader **, const unsigned char **);
using PcapDataLinkFn = int (*)(PcapHandle *);
using PcapCompileFn = int (*)(PcapHandle *, BpfProgram *, const char *, int, unsigned int);
using PcapSetFilterFn = int (*)(PcapHandle *, BpfProgram *);
using PcapFreeCodeFn = void (*)(BpfProgram *);
using PcapSetNonBlockFn = int (*)(PcapHandle *, int, char *);
using PcapGetErrFn = char *(*)(PcapHandle *);

constexpr int kPcapErrorBufferSize = 256;
constexpr unsigned int kPcapIfLoopback = 0x00000001u;
constexpr unsigned int kPcapNetmaskUnknown = 0xFFFFFFFFu;
constexpr int kDltNull = 0;
constexpr int kDltEthernet = 1;
constexpr int kDltRaw = 12;

bool isAnyIpv4(const QHostAddress &address)
{
    return address.isNull()
           || address == QHostAddress::Any
           || address == QHostAddress::AnyIPv4;
}

bool isLoopbackInterface(const PcapInterface *device)
{
    if (device == nullptr) {
        return true;
    }
    if ((device->flags & kPcapIfLoopback) != 0U) {
        return true;
    }

    const QString name = QString::fromLocal8Bit(device->name != nullptr ? device->name : "");
    const QString description = QString::fromLocal8Bit(
        device->description != nullptr ? device->description : "");
    return name.contains(QStringLiteral("loopback"), Qt::CaseInsensitive)
           || description.contains(QStringLiteral("loopback"), Qt::CaseInsensitive);
}

bool deviceHasIpv4(const PcapInterface *device, const QHostAddress &address)
{
    if (device == nullptr) {
        return false;
    }

    const bool anyAddress = isAnyIpv4(address);
    bool conversionOk = false;
    const quint32 wantedAddress = anyAddress ? 0U : address.toIPv4Address(&conversionOk);
    if (!anyAddress && !conversionOk) {
        return false;
    }

    for (PcapAddress *entry = device->addresses; entry != nullptr; entry = entry->next) {
        if (entry->addr == nullptr || entry->addr->sa_family != AF_INET) {
            continue;
        }

        const auto *ipv4 = reinterpret_cast<const sockaddr_in *>(entry->addr);
        const auto *bytes = reinterpret_cast<const unsigned char *>(&ipv4->sin_addr.s_addr);
        const quint32 foundAddress = (quint32(bytes[0]) << 24)
                                     | (quint32(bytes[1]) << 16)
                                     | (quint32(bytes[2]) << 8)
                                     | quint32(bytes[3]);
        if (anyAddress || foundAddress == wantedAddress) {
            return true;
        }
    }
    return false;
}

std::optional<UdpPacketParser::DataLinkType> convertDataLinkType(int dataLink)
{
    switch (dataLink) {
    case kDltEthernet:
        return UdpPacketParser::DataLinkType::Ethernet;
    case kDltRaw:
        return UdpPacketParser::DataLinkType::RawIp;
    case kDltNull:
        return UdpPacketParser::DataLinkType::NullLoopback;
    default:
        return std::nullopt;
    }
}

} // namespace

class NpcapReceiverPrivate
{
public:
    explicit NpcapReceiverPrivate(NpcapReceiver *owner)
        : q(owner)
    {
    }

    ~NpcapReceiverPrivate()
    {
        stop();
    }

    struct Capture
    {
        PcapHandle *handle = nullptr;
        UdpPacketParser::DataLinkType dataLinkType = UdpPacketParser::DataLinkType::Ethernet;
        QString name;
    };

    template<typename FunctionType>
    FunctionType resolve(const char *symbol)
    {
        if (!library) {
            return nullptr;
        }
        return reinterpret_cast<FunctionType>(library->resolve(symbol));
    }

    bool loadLibrary(QString *detail)
    {
        const QString systemRoot = qEnvironmentVariable(
            "SystemRoot", QStringLiteral("C:/Windows"));
        const QString applicationDirectory = QCoreApplication::applicationDirPath();

        const QStringList candidates{
            QDir(applicationDirectory).filePath(QStringLiteral("wpcap.dll")),
            QDir(systemRoot).filePath(QStringLiteral("System32/Npcap/wpcap.dll")),
            QDir(systemRoot).filePath(QStringLiteral("SysWOW64/Npcap/wpcap.dll")),
            QStringLiteral("wpcap.dll"),
            QStringLiteral("wpcap")
        };

        QStringList loadErrors;
        for (const QString &candidate : candidates) {
            auto candidateLibrary = std::make_unique<QLibrary>(candidate);
            if (candidateLibrary->load()) {
                library = std::move(candidateLibrary);
                if (detail != nullptr) {
                    *detail = QStringLiteral("Npcap loaded from %1").arg(candidate);
                }
                break;
            }
            loadErrors.append(QStringLiteral("%1: %2")
                                  .arg(candidate, candidateLibrary->errorString()));
        }

        if (!library) {
            if (detail != nullptr) {
                *detail = QStringLiteral(
                              "Npcap was not found. Install Npcap or Wireshark with Npcap; "
                              "the standard Qt UDP path remains available. Details: ")
                          + loadErrors.join(QStringLiteral(" | "));
            }
            return false;
        }

        findAllDevs = resolve<PcapFindAllDevsFn>("pcap_findalldevs");
        freeAllDevs = resolve<PcapFreeAllDevsFn>("pcap_freealldevs");
        openLive = resolve<PcapOpenLiveFn>("pcap_open_live");
        closeHandle = resolve<PcapCloseFn>("pcap_close");
        nextEx = resolve<PcapNextExFn>("pcap_next_ex");
        dataLink = resolve<PcapDataLinkFn>("pcap_datalink");
        compileFilter = resolve<PcapCompileFn>("pcap_compile");
        setFilter = resolve<PcapSetFilterFn>("pcap_setfilter");
        freeCode = resolve<PcapFreeCodeFn>("pcap_freecode");
        setNonBlock = resolve<PcapSetNonBlockFn>("pcap_setnonblock");
        getError = resolve<PcapGetErrFn>("pcap_geterr");

        if (findAllDevs == nullptr || freeAllDevs == nullptr || openLive == nullptr
            || closeHandle == nullptr || nextEx == nullptr || dataLink == nullptr
            || compileFilter == nullptr || setFilter == nullptr || freeCode == nullptr) {
            if (detail != nullptr) {
                *detail = QStringLiteral(
                    "wpcap.dll was loaded, but one or more required pcap functions are missing.");
            }
            unloadLibrary();
            return false;
        }
        return true;
    }

    bool start(const QHostAddress &destinationAddress,
               quint16 firstDestinationPort,
               quint16 lastDestinationPort,
               QString *statusMessage)
    {
        stop();

        filter.destinationAddress = destinationAddress;
        filter.firstDestinationPort = qMin(firstDestinationPort, lastDestinationPort);
        filter.lastDestinationPort = qMax(firstDestinationPort, lastDestinationPort);

        QString loadStatus;
        if (!loadLibrary(&loadStatus)) {
            if (statusMessage != nullptr) {
                *statusMessage = loadStatus;
            }
            return false;
        }

        char errorBuffer[kPcapErrorBufferSize] = {};
        PcapInterface *allDevices = nullptr;
        if (findAllDevs(&allDevices, errorBuffer) != 0 || allDevices == nullptr) {
            if (statusMessage != nullptr) {
                *statusMessage = QStringLiteral("Npcap could not enumerate adapters: %1")
                                     .arg(QString::fromLocal8Bit(errorBuffer));
            }
            if (allDevices != nullptr) {
                freeAllDevs(allDevices);
            }
            unloadLibrary();
            return false;
        }

        QString filterExpression;
        if (filter.firstDestinationPort == filter.lastDestinationPort) {
            filterExpression = QStringLiteral("udp and dst port %1")
                                   .arg(filter.firstDestinationPort);
        } else {
            filterExpression = QStringLiteral("udp and dst portrange %1-%2")
                                   .arg(filter.firstDestinationPort)
                                   .arg(filter.lastDestinationPort);
        }
        if (!isAnyIpv4(destinationAddress)) {
            filterExpression += QStringLiteral(" and dst host %1")
                                    .arg(destinationAddress.toString());
        }
        const QByteArray filterBytes = filterExpression.toLatin1();

        QStringList openedAdapters;
        QStringList skippedAdapters;

        for (PcapInterface *device = allDevices; device != nullptr; device = device->next) {
            if (device->name == nullptr || isLoopbackInterface(device)
                || !deviceHasIpv4(device, destinationAddress)) {
                continue;
            }

            std::memset(errorBuffer, 0, sizeof(errorBuffer));
            PcapHandle *handle = openLive(device->name,
                                          65535,
                                          1,
                                          50,
                                          errorBuffer);
            if (handle == nullptr) {
                skippedAdapters.append(QStringLiteral("%1: %2")
                                           .arg(QString::fromLocal8Bit(device->name),
                                                QString::fromLocal8Bit(errorBuffer)));
                continue;
            }

            BpfProgram program{};
            const int compileResult = compileFilter(handle,
                                                    &program,
                                                    filterBytes.constData(),
                                                    1,
                                                    kPcapNetmaskUnknown);
            const int applyResult = compileResult == 0 ? setFilter(handle, &program) : -1;
            if (program.bf_insns != nullptr) {
                freeCode(&program);
            }

            if (compileResult != 0 || applyResult != 0) {
                QString pcapError;
                if (getError != nullptr) {
                    const char *errorText = getError(handle);
                    if (errorText != nullptr) {
                        pcapError = QString::fromLocal8Bit(errorText);
                    }
                }
                skippedAdapters.append(QStringLiteral("%1: filter failed (%2)")
                                           .arg(QString::fromLocal8Bit(device->name), pcapError));
                closeHandle(handle);
                continue;
            }

            if (setNonBlock != nullptr) {
                std::memset(errorBuffer, 0, sizeof(errorBuffer));
                setNonBlock(handle, 1, errorBuffer);
            }

            const int rawDataLinkType = dataLink(handle);
            const auto parsedDataLinkType = convertDataLinkType(rawDataLinkType);
            const QString adapterName = QString::fromLocal8Bit(
                device->description != nullptr && *device->description != '\0'
                    ? device->description
                    : device->name);

            if (!parsedDataLinkType.has_value()) {
                skippedAdapters.append(QStringLiteral("%1: unsupported data-link type %2")
                                           .arg(adapterName)
                                           .arg(rawDataLinkType));
                closeHandle(handle);
                continue;
            }

            captures.push_back(Capture{handle, *parsedDataLinkType, adapterName});
            openedAdapters.append(adapterName);
        }

        freeAllDevs(allDevices);

        if (captures.empty()) {
            if (statusMessage != nullptr) {
                *statusMessage = QStringLiteral("Npcap found no usable adapter for %1.")
                                     .arg(destinationAddress.toString());
                if (!skippedAdapters.isEmpty()) {
                    *statusMessage += QStringLiteral(" Details: ")
                                      + skippedAdapters.join(QStringLiteral(" | "));
                }
            }
            unloadLibrary();
            return false;
        }

        stopRequested.store(false, std::memory_order_release);
        running.store(true, std::memory_order_release);

        for (const Capture &capture : captures) {
            captureThreads.emplace_back([this, capture]() {
                captureLoop(capture);
            });
        }

        if (statusMessage != nullptr) {
            *statusMessage = QStringLiteral("Npcap capture active on: %1")
                                 .arg(openedAdapters.join(QStringLiteral(", ")));
            if (!skippedAdapters.isEmpty()) {
                *statusMessage += QStringLiteral(". Skipped: ")
                                  + skippedAdapters.join(QStringLiteral(" | "));
            }
        }
        return true;
    }

    void captureLoop(const Capture &capture)
    {
        while (!stopRequested.load(std::memory_order_acquire)) {
            PcapPacketHeader *header = nullptr;
            const unsigned char *packet = nullptr;
            const int result = nextEx(capture.handle, &header, &packet);

            if (result == 1 && header != nullptr && packet != nullptr) {
                ParsedUdpDatagram datagram;
                if (UdpPacketParser::parse(packet,
                                           qsizetype(header->caplen),
                                           capture.dataLinkType,
                                           filter,
                                           &datagram)) {
                    emit q->datagramCaptured(datagram, capture.name);
                }
            } else if (result == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            } else if (result == -1) {
                QString errorText = QStringLiteral("Unknown Npcap capture error.");
                if (getError != nullptr) {
                    const char *rawError = getError(capture.handle);
                    if (rawError != nullptr) {
                        errorText = QString::fromLocal8Bit(rawError);
                    }
                }
                emit q->captureError(QStringLiteral("%1: %2")
                                         .arg(capture.name, errorText));
                break;
            } else if (result == -2) {
                break;
            }
        }
    }

    void stop()
    {
        stopRequested.store(true, std::memory_order_release);

        for (std::thread &thread : captureThreads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        captureThreads.clear();

        if (closeHandle != nullptr) {
            for (Capture &capture : captures) {
                if (capture.handle != nullptr) {
                    closeHandle(capture.handle);
                    capture.handle = nullptr;
                }
            }
        }
        captures.clear();
        running.store(false, std::memory_order_release);
        unloadLibrary();
    }

    void unloadLibrary()
    {
        findAllDevs = nullptr;
        freeAllDevs = nullptr;
        openLive = nullptr;
        closeHandle = nullptr;
        nextEx = nullptr;
        dataLink = nullptr;
        compileFilter = nullptr;
        setFilter = nullptr;
        freeCode = nullptr;
        setNonBlock = nullptr;
        getError = nullptr;

        if (library) {
            library->unload();
            library.reset();
        }
    }

    NpcapReceiver *q = nullptr;
    std::unique_ptr<QLibrary> library;

    PcapFindAllDevsFn findAllDevs = nullptr;
    PcapFreeAllDevsFn freeAllDevs = nullptr;
    PcapOpenLiveFn openLive = nullptr;
    PcapCloseFn closeHandle = nullptr;
    PcapNextExFn nextEx = nullptr;
    PcapDataLinkFn dataLink = nullptr;
    PcapCompileFn compileFilter = nullptr;
    PcapSetFilterFn setFilter = nullptr;
    PcapFreeCodeFn freeCode = nullptr;
    PcapSetNonBlockFn setNonBlock = nullptr;
    PcapGetErrFn getError = nullptr;

    UdpPacketParser::Filter filter;
    std::vector<Capture> captures;
    std::vector<std::thread> captureThreads;
    std::atomic_bool stopRequested{false};
    std::atomic_bool running{false};
};

#else

class NpcapReceiverPrivate
{
public:
    explicit NpcapReceiverPrivate(NpcapReceiver *) {}
    bool running = false;
};

#endif

NpcapReceiver::NpcapReceiver(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<NpcapReceiverPrivate>(this))
{
    qRegisterMetaType<ParsedUdpDatagram>("ParsedUdpDatagram");
}

NpcapReceiver::~NpcapReceiver()
{
    stop();
}

bool NpcapReceiver::start(const QHostAddress &destinationAddress,
                          quint16 firstDestinationPort,
                          quint16 lastDestinationPort,
                          QString *statusMessage)
{
#ifdef _WIN32
    return d->start(destinationAddress,
                    firstDestinationPort,
                    lastDestinationPort,
                    statusMessage);
#else
    Q_UNUSED(destinationAddress)
    Q_UNUSED(firstDestinationPort)
    Q_UNUSED(lastDestinationPort)
    if (statusMessage != nullptr) {
        *statusMessage = QStringLiteral(
            "Npcap fallback is Windows-only. The standard Qt UDP path remains available.");
    }
    return false;
#endif
}

void NpcapReceiver::stop()
{
#ifdef _WIN32
    d->stop();
#else
    d->running = false;
#endif
}

bool NpcapReceiver::isRunning() const
{
#ifdef _WIN32
    return d->running.load(std::memory_order_acquire);
#else
    return d->running;
#endif
}
