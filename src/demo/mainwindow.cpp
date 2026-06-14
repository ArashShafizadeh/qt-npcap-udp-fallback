#include "demo/mainwindow.h"

#include "demo/packetsimulator.h"

#include <QDateTime>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    buildUi();

    connect(&m_receiver, &QUdpSocket::readyRead, this, &MainWindow::drainDatagrams);
    connect(&m_reassembler, &qfr::FrameReassembler::eventOccurred,
            this, &MainWindow::appendLog);
    connect(&m_reassembler, &qfr::FrameReassembler::statisticsChanged,
            this, &MainWindow::updateStatistics);
    connect(&m_reassembler, &qfr::FrameReassembler::messageReady,
            this,
            [this](quint8 streamId, quint8 batchId, const QByteArray &message) {
                appendLog(QStringLiteral("DELIVERED stream=%1 batch=%2 payload=%3 bytes")
                              .arg(streamId)
                              .arg(batchId)
                              .arg(message.size()));
            });
    connect(&m_reassembler, &qfr::FrameReassembler::missingBatchDetected,
            this,
            [this](quint8 streamId, quint8 batchId) {
                appendLog(QStringLiteral("MISSING stream=%1 batch=%2")
                              .arg(streamId)
                              .arg(batchId));
            });

    auto *cleanupTimer = new QTimer(this);
    cleanupTimer->setInterval(250);
    connect(cleanupTimer, &QTimer::timeout, &m_reassembler, [this]() {
        m_reassembler.purgeExpired();
    });
    cleanupTimer->start();

    toggleReceiver();
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("Qt UDP Frame Reassembler"));
    resize(960, 680);

    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);

    auto *receiverRow = new QHBoxLayout;
    receiverRow->addWidget(new QLabel(QStringLiteral("UDP port:"), central));
    m_portSpin = new QSpinBox(central);
    m_portSpin->setRange(1024, 65535);
    m_portSpin->setValue(45454);
    receiverRow->addWidget(m_portSpin);

    m_startButton = new QPushButton(QStringLiteral("Start receiver"), central);
    receiverRow->addWidget(m_startButton);
    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::toggleReceiver);

    auto *resetButton = new QPushButton(QStringLiteral("Reset state"), central);
    receiverRow->addWidget(resetButton);
    connect(resetButton, &QPushButton::clicked, &m_reassembler, &qfr::FrameReassembler::reset);

    receiverRow->addStretch();
    m_statusLabel = new QLabel(QStringLiteral("Stopped"), central);
    receiverRow->addWidget(m_statusLabel);
    root->addLayout(receiverRow);

    auto *scenarios = new QGroupBox(QStringLiteral("Built-in UDP scenarios"), central);
    auto *scenarioGrid = new QGridLayout(scenarios);

    struct ScenarioButton {
        const char *label;
        void (MainWindow::*slot)();
    };

    const ScenarioButton buttons[] = {
        {"Ordered fragments", &MainWindow::sendOrderedScenario},
        {"Out-of-order fragments", &MainWindow::sendOutOfOrderScenario},
        {"Duplicate fragment", &MainWindow::sendDuplicateScenario},
        {"Interleaved batches", &MainWindow::sendInterleavedScenario},
        {"Missing batch detection", &MainWindow::sendMissingBatchScenario},
        {"Incomplete batch timeout", &MainWindow::sendTimeoutScenario},
    };

    for (int index = 0; index < 6; ++index) {
        auto *button = new QPushButton(QString::fromLatin1(buttons[index].label), scenarios);
        scenarioGrid->addWidget(button, index / 3, index % 3);
        connect(button, &QPushButton::clicked, this, buttons[index].slot);
    }

    root->addWidget(scenarios);

    m_statsLabel = new QLabel(central);
    m_statsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_statsLabel);

    m_log = new QPlainTextEdit(central);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(2000);
    root->addWidget(m_log, 1);

    setCentralWidget(central);
    updateStatistics();
}

void MainWindow::toggleReceiver()
{
    if (m_listening) {
        m_receiver.close();
        m_listening = false;
        m_startButton->setText(QStringLiteral("Start receiver"));
        m_statusLabel->setText(QStringLiteral("Stopped"));
        return;
    }

    const quint16 port = static_cast<quint16>(m_portSpin->value());
    m_listening = m_receiver.bind(QHostAddress::LocalHost,
                                  port,
                                  QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);

    if (!m_listening) {
        appendLog(QStringLiteral("Bind failed: %1").arg(m_receiver.errorString()));
        m_statusLabel->setText(QStringLiteral("Bind failed"));
        return;
    }

    m_startButton->setText(QStringLiteral("Stop receiver"));
    m_statusLabel->setText(QStringLiteral("Listening on 127.0.0.1:%1").arg(port));
    appendLog(m_statusLabel->text());
}

void MainWindow::drainDatagrams()
{
    while (m_receiver.hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<int>(m_receiver.pendingDatagramSize()));
        m_receiver.readDatagram(datagram.data(), datagram.size());
        m_reassembler.feedDatagram(datagram);
    }
}

QByteArray MainWindow::makePayload(const QString &label, int bytes) const
{
    QByteArray output = label.toUtf8() + QByteArrayLiteral(" | ");
    const QByteArray pattern = QByteArrayLiteral("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    while (output.size() < bytes) {
        output.append(pattern);
    }
    output.truncate(bytes);
    return output;
}

quint8 MainWindow::nextStreamId()
{
    ++m_streamCounter;
    if (m_streamCounter == 0) {
        ++m_streamCounter;
    }
    return m_streamCounter;
}

void MainWindow::sendDatagrams(const QList<QByteArray> &datagrams, int intervalMs)
{
    if (!m_listening) {
        appendLog(QStringLiteral("Receiver is not running"));
        return;
    }

    const quint16 port = static_cast<quint16>(m_portSpin->value());
    for (int index = 0; index < datagrams.size(); ++index) {
        const QByteArray datagram = datagrams.at(index);
        QTimer::singleShot(index * intervalMs, this, [this, datagram, port]() {
            m_sender.writeDatagram(datagram, QHostAddress::LocalHost, port);
        });
    }
}

void MainWindow::sendOrderedScenario()
{
    const quint8 stream = nextStreamId();
    appendLog(QStringLiteral("--- Ordered fragments, stream=%1 ---").arg(stream));
    sendDatagrams(PacketSimulator::fragmentMessage(makePayload(QStringLiteral("ordered"), 4096),
                                                   stream,
                                                   1,
                                                   500));
}

void MainWindow::sendOutOfOrderScenario()
{
    const quint8 stream = nextStreamId();
    QList<QByteArray> datagrams = PacketSimulator::fragmentMessage(
        makePayload(QStringLiteral("out-of-order"), 4096), stream, 1, 500);
    std::reverse(datagrams.begin(), datagrams.end());
    appendLog(QStringLiteral("--- Reversed fragment order, stream=%1 ---").arg(stream));
    sendDatagrams(datagrams);
}

void MainWindow::sendDuplicateScenario()
{
    const quint8 stream = nextStreamId();
    QList<QByteArray> datagrams = PacketSimulator::fragmentMessage(
        makePayload(QStringLiteral("duplicate"), 4096), stream, 1, 500);
    if (datagrams.size() > 2) {
        datagrams.insert(2, datagrams.at(1));
    }
    appendLog(QStringLiteral("--- Duplicate fragment, stream=%1 ---").arg(stream));
    sendDatagrams(datagrams);
}

void MainWindow::sendInterleavedScenario()
{
    const quint8 stream = nextStreamId();
    const QList<QByteArray> first = PacketSimulator::fragmentMessage(
        makePayload(QStringLiteral("interleaved-A"), 3072), stream, 10, 420);
    const QList<QByteArray> second = PacketSimulator::fragmentMessage(
        makePayload(QStringLiteral("interleaved-B"), 3584), stream, 11, 420);

    QList<QByteArray> woven;
    const int maximum = std::max(first.size(), second.size());
    for (int index = 0; index < maximum; ++index) {
        if (index < first.size()) {
            woven.push_back(first.at(index));
        }
        if (index < second.size()) {
            woven.push_back(second.at(index));
        }
    }

    appendLog(QStringLiteral("--- Interleaved batches 10 and 11, stream=%1 ---").arg(stream));
    sendDatagrams(woven);
}

void MainWindow::sendMissingBatchScenario()
{
    const quint8 stream = nextStreamId();
    QList<QByteArray> datagrams = PacketSimulator::fragmentMessage(
        makePayload(QStringLiteral("batch-20"), 1024), stream, 20, 350);
    datagrams.append(PacketSimulator::fragmentMessage(
        makePayload(QStringLiteral("batch-22"), 1024), stream, 22, 350));
    appendLog(QStringLiteral("--- Send batches 20 then 22; batch 21 is missing, stream=%1 ---")
                  .arg(stream));
    sendDatagrams(datagrams);
}

void MainWindow::sendTimeoutScenario()
{
    const quint8 stream = nextStreamId();
    QList<QByteArray> datagrams = PacketSimulator::fragmentMessage(
        makePayload(QStringLiteral("timeout"), 4096), stream, 1, 500);
    if (!datagrams.isEmpty()) {
        datagrams.removeLast();
    }
    appendLog(QStringLiteral("--- Last fragment omitted; assembly will expire, stream=%1 ---")
                  .arg(stream));
    sendDatagrams(datagrams);
}

void MainWindow::updateStatistics()
{
    const qfr::FrameReassembler::Statistics stats = m_reassembler.statistics();
    m_statsLabel->setText(
        QStringLiteral("Datagrams: %1 | Accepted fragments: %2 | Completed: %3 | Duplicates: %4 | "
                       "Malformed: %5 | Expired: %6 | Late: %7 | Missing batches: %8 | Active: %9")
            .arg(stats.datagramsReceived)
            .arg(stats.fragmentsAccepted)
            .arg(stats.completedMessages)
            .arg(stats.duplicateFragments)
            .arg(stats.malformedDatagrams)
            .arg(stats.expiredAssemblies)
            .arg(stats.lateBatchesDropped)
            .arg(stats.missingBatches)
            .arg(m_reassembler.activeAssemblyCount()));
}

void MainWindow::appendLog(const QString &message)
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    m_log->appendPlainText(QStringLiteral("[%1] %2").arg(timestamp, message));
}
