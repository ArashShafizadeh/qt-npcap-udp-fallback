#include "mainwindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkInterface>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QUdpSocket>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , receiver(this)
{
    setWindowTitle(QStringLiteral("Qt + Npcap UDP Fallback Demo"));
    resize(980, 680);

    auto *centralWidget = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(centralWidget);

    auto *configurationBox = new QGroupBox(QStringLiteral("Receiver configuration"), centralWidget);
    auto *configurationLayout = new QGridLayout(configurationBox);

    addressCombo = new QComboBox(configurationBox);
    addressCombo->setEditable(true);
    populateAddresses();

    firstPortSpin = new QSpinBox(configurationBox);
    firstPortSpin->setRange(1, 65535);
    firstPortSpin->setValue(50000);

    lastPortSpin = new QSpinBox(configurationBox);
    lastPortSpin->setRange(1, 65535);
    lastPortSpin->setValue(50000);

    npcapCheck = new QCheckBox(QStringLiteral("Enable Npcap raw-frame fallback"),
                               configurationBox);
    npcapCheck->setChecked(true);

    startStopButton = new QPushButton(QStringLiteral("Start receiver"), configurationBox);

    configurationLayout->addWidget(new QLabel(QStringLiteral("Bind IPv4 address:"), configurationBox), 0, 0);
    configurationLayout->addWidget(addressCombo, 0, 1, 1, 3);
    configurationLayout->addWidget(new QLabel(QStringLiteral("First port:"), configurationBox), 1, 0);
    configurationLayout->addWidget(firstPortSpin, 1, 1);
    configurationLayout->addWidget(new QLabel(QStringLiteral("Last port:"), configurationBox), 1, 2);
    configurationLayout->addWidget(lastPortSpin, 1, 3);
    configurationLayout->addWidget(npcapCheck, 2, 0, 1, 3);
    configurationLayout->addWidget(startStopButton, 2, 3);

    rootLayout->addWidget(configurationBox);

    auto *statisticsBox = new QGroupBox(QStringLiteral("Live statistics"), centralWidget);
    auto *statisticsLayout = new QGridLayout(statisticsBox);
    qtUdpCountLabel = new QLabel(QStringLiteral("0"), statisticsBox);
    npcapCountLabel = new QLabel(QStringLiteral("0"), statisticsBox);
    deliveredCountLabel = new QLabel(QStringLiteral("0"), statisticsBox);
    duplicateCountLabel = new QLabel(QStringLiteral("0"), statisticsBox);
    byteCountLabel = new QLabel(QStringLiteral("0"), statisticsBox);

    statisticsLayout->addWidget(new QLabel(QStringLiteral("Qt UDP packets"), statisticsBox), 0, 0);
    statisticsLayout->addWidget(qtUdpCountLabel, 0, 1);
    statisticsLayout->addWidget(new QLabel(QStringLiteral("Npcap packets"), statisticsBox), 0, 2);
    statisticsLayout->addWidget(npcapCountLabel, 0, 3);
    statisticsLayout->addWidget(new QLabel(QStringLiteral("Delivered"), statisticsBox), 1, 0);
    statisticsLayout->addWidget(deliveredCountLabel, 1, 1);
    statisticsLayout->addWidget(new QLabel(QStringLiteral("Cross-path duplicates"), statisticsBox), 1, 2);
    statisticsLayout->addWidget(duplicateCountLabel, 1, 3);
    statisticsLayout->addWidget(new QLabel(QStringLiteral("Delivered bytes"), statisticsBox), 2, 0);
    statisticsLayout->addWidget(byteCountLabel, 2, 1);

    rootLayout->addWidget(statisticsBox);

    auto *testBox = new QGroupBox(QStringLiteral("Standard UDP smoke test"), centralWidget);
    auto *testLayout = new QHBoxLayout(testBox);
    testPayloadEdit = new QLineEdit(QStringLiteral("hello from Qt"), testBox);
    testButton = new QPushButton(QStringLiteral("Send to first port"), testBox);
    testLayout->addWidget(testPayloadEdit, 1);
    testLayout->addWidget(testButton);
    rootLayout->addWidget(testBox);

    logEdit = new QPlainTextEdit(centralWidget);
    logEdit->setReadOnly(true);
    logEdit->setMaximumBlockCount(3000);
    logEdit->setPlaceholderText(QStringLiteral("Received datagrams and status messages appear here."));
    rootLayout->addWidget(logEdit, 1);

    statusLabel = new QLabel(QStringLiteral("Stopped"), this);
    statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addPermanentWidget(statusLabel, 1);

    setCentralWidget(centralWidget);

    connect(startStopButton, &QPushButton::clicked, this, &MainWindow::toggleReceiver);
    connect(testButton, &QPushButton::clicked, this, &MainWindow::sendLocalTestDatagram);
    connect(&receiver,
            &HybridUdpReceiver::datagramReceived,
            this,
            &MainWindow::appendDatagram);
    connect(&receiver,
            &HybridUdpReceiver::statisticsChanged,
            this,
            &MainWindow::updateStatistics);
    connect(&receiver,
            &HybridUdpReceiver::statusChanged,
            this,
            [this](const QString &message) {
                statusLabel->setText(message);
                appendLog(message);
            });
    connect(&receiver,
            &HybridUdpReceiver::errorOccurred,
            this,
            [this](const QString &message) {
                appendLog(QStringLiteral("ERROR: %1").arg(message));
            });
}

void MainWindow::populateAddresses()
{
    addressCombo->clear();
    addressCombo->addItem(QHostAddress(QHostAddress::AnyIPv4).toString());

    const QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    for (const QHostAddress &address : addresses) {
        if (address.protocol() != QAbstractSocket::IPv4Protocol
            || address.isLoopback()) {
            continue;
        }
        if (addressCombo->findText(address.toString()) < 0) {
            addressCombo->addItem(address.toString());
        }
    }
}

void MainWindow::toggleReceiver()
{
    if (receiver.isRunning()) {
        receiver.stop();
        setControlsRunning(false);
        return;
    }

    const QHostAddress bindAddress(addressCombo->currentText().trimmed());
    if (bindAddress.isNull() && addressCombo->currentText().trimmed() != QStringLiteral("0.0.0.0")) {
        appendLog(QStringLiteral("ERROR: Enter a valid IPv4 bind address."));
        return;
    }

    QString status;
    const bool started = receiver.start(bindAddress,
                                        quint16(firstPortSpin->value()),
                                        quint16(lastPortSpin->value()),
                                        npcapCheck->isChecked(),
                                        &status);
    if (!started) {
        appendLog(QStringLiteral("ERROR: %1").arg(status));
        setControlsRunning(false);
        return;
    }

    setControlsRunning(true);
}

void MainWindow::sendLocalTestDatagram()
{
    if (!receiver.isRunning()) {
        appendLog(QStringLiteral("Start the receiver before sending a test datagram."));
        return;
    }

    QHostAddress destination(addressCombo->currentText().trimmed());
    if (destination == QHostAddress::Any || destination == QHostAddress::AnyIPv4) {
        destination = QHostAddress::LocalHost;
    }

    QUdpSocket sender;
    const QByteArray payload = testPayloadEdit->text().toUtf8();
    const qint64 bytesWritten = sender.writeDatagram(payload,
                                                     destination,
                                                     quint16(firstPortSpin->value()));
    if (bytesWritten < 0) {
        appendLog(QStringLiteral("Test send failed: %1").arg(sender.errorString()));
    } else {
        appendLog(QStringLiteral("Sent %1 test bytes to %2:%3")
                      .arg(bytesWritten)
                      .arg(destination.toString())
                      .arg(firstPortSpin->value()));
    }
}

void MainWindow::appendDatagram(const ParsedUdpDatagram &datagram,
                                HybridUdpReceiver::Source source,
                                const QString &sourceDetail)
{
    const QString sourceName = source == HybridUdpReceiver::Source::QtUdp
                                   ? QStringLiteral("Qt UDP")
                                   : QStringLiteral("Npcap");
    const QString preview = QString::fromUtf8(datagram.payload.left(80))
                                .replace(QLatin1Char('\n'), QStringLiteral("\\n"))
                                .replace(QLatin1Char('\r'), QStringLiteral("\\r"));

    appendLog(QStringLiteral("[%1 via %2/%3] %4:%5 -> %6:%7 | %8 bytes | \"%9\"")
                  .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")),
                       sourceName,
                       sourceDetail,
                       datagram.sourceAddress.toString())
                  .arg(datagram.sourcePort)
                  .arg(datagram.destinationAddress.toString())
                  .arg(datagram.destinationPort)
                  .arg(datagram.payload.size())
                  .arg(preview));
}

void MainWindow::updateStatistics(const HybridUdpReceiver::Statistics &statistics)
{
    qtUdpCountLabel->setText(QString::number(statistics.qtUdpPackets));
    npcapCountLabel->setText(QString::number(statistics.npcapPackets));
    deliveredCountLabel->setText(QString::number(statistics.deliveredPackets));
    duplicateCountLabel->setText(QString::number(statistics.crossPathDuplicates));
    byteCountLabel->setText(QString::number(statistics.deliveredBytes));
}

void MainWindow::appendLog(const QString &message)
{
    logEdit->appendPlainText(message);
}

void MainWindow::setControlsRunning(bool isRunning)
{
    addressCombo->setEnabled(!isRunning);
    firstPortSpin->setEnabled(!isRunning);
    lastPortSpin->setEnabled(!isRunning);
    npcapCheck->setEnabled(!isRunning);
    startStopButton->setText(isRunning
                                 ? QStringLiteral("Stop receiver")
                                 : QStringLiteral("Start receiver"));
}
